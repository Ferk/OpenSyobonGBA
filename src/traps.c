#include "traps.h"

#include <gba.h>
#include <string.h>

#include "audio.h"
#include "camera.h"
#include "enemy.h"
#include "generated/level1_data.h"
#include "messages.h"
#include "player.h"
#include "items_16.h"
#include "tiles_16.h"
#include "traps_16.h"

#define FIRST_TRAP_SPRITE 16
#define TRAP_TILE_INDEX 48
#define PROJECTILE_TILE_INDEX 56
#define EVASIVE_BLOCK_TILE_INDEX 80
#define PIPE_TILE_INDEX 84
#define ITEM_TILE_INDEX 100
#define COIN_TILE_INDEX ITEM_TILE_INDEX
#define GOOD_ITEM_TILE_INDEX (ITEM_TILE_INDEX + 4)
#define BAD_ITEM_TILE_INDEX (ITEM_TILE_INDEX + 8)
#define STAR_ITEM_TILE_INDEX (ITEM_TILE_INDEX + 12)
#define BRICK_FRAGMENT_TILE_INDEX 116
#define FALLING_BRICK_TILE_INDEX 120
#define CHECKPOINT_TILE_INDEX 176
#define EVASIVE_BLOCK_PALETTE_BANK 1
#define ITEM_PALETTE_BANK 3
#define TRAPS_MAX_DYNAMIC_SPRITES 32

#define REF_POS_TO_FIX(v) ((fix16_t)(((int64_t)(v) * 16 * FIX16_ONE) / (29 * 100)))
#define REF_STAGE_X_TO_FIX(v) REF_POS_TO_FIX(v)
#define REF_STAGE_Y_TO_FIX(v) REF_POS_TO_FIX((v) + 12 * 100 - (LEVEL_VIEW_SOURCE_ROW_OFFSET * 29 * 100))
#define REF_VEL_TO_FIX(v) (REF_POS_TO_FIX(v) / 2)
#define REF_ACCEL_TO_FIX(v) (REF_POS_TO_FIX(v) / 4)
#define CELL_WORLD_X(cell_x) FIX16_FROM_INT((cell_x) * LEVEL_METATILE_SIZE)
#define CELL_WORLD_Y(cell_y) FIX16_FROM_INT(((cell_y) - LEVEL_VIEW_SOURCE_ROW_OFFSET) * LEVEL_METATILE_SIZE)
#define DYNAMIC_TRAP_OAM_BASE (FIRST_TRAP_SPRITE + TRAPS_MAX_ENTITIES)

#define FALL_GRAVITY REF_ACCEL_TO_FIX(120)
#define FALL_MAX_SPEED REF_VEL_TO_FIX(1600)
#define PROJECTILE_SPEED REF_VEL_TO_FIX(900)
#define PROJECTILE_GRAVITY REF_ACCEL_TO_FIX(50)
#define EVASIVE_BLOCK_HOME_X CELL_WORLD_X(8)
#define EVASIVE_BLOCK_HOME_Y CELL_WORLD_Y(9)
#define EVASIVE_BLOCK_VERTICAL 0
#define EVASIVE_BLOCK_HORIZONTAL 1
#define EVASIVE_BLOCK_PLAYER_OFFSET REF_POS_TO_FIX(4200)
#define EVASIVE_BLOCK_HORIZONTAL_DODGE REF_POS_TO_FIX(3000)
#define PIPE_SHAKE_START_FRAME 44
#define PIPE_RISE_START_FRAME 110
#define PIPE_KILL_FRAME 160
#define PIPE_PLAYER_SINK_FRAMES 16
#define PIPE_PLAYER_SINK_SPEED REF_POS_TO_FIX(240)
#define PIPE_RISE_ACCEL REF_ACCEL_TO_FIX(80)
#define PIPE_RISE_MAX_SPEED REF_VEL_TO_FIX(1600)
#define PIPE_TRANSITION_FRAME 20
#define PIPE_TRANSITION_1_2_UNDERGROUND 2
#define KEY_DOWN_MASK 0x0080
#define QUESTION_ENEMY 101
#define QUESTION_GOOD_MUSHROOM 102
#define QUESTION_BAD_MUSHROOM 103
#define QUESTION_STAR 104
#define QUESTION_POISON_GENERATOR 110
#define QUESTION_COIN_GENERATOR 112
#define QUESTION_HIDDEN_POISON 114
#define QUESTION_GENERATOR_ACTIVE 200
#define QUESTION_COIN_LIMIT 20
#define QUESTION_COIN_INTERVAL 3
#define QUESTION_GENERATOR_INTERVAL 16
#define QUESTION_COIN_POPUP_LIFETIME 16
#define QUESTION_GENERATOR_DESPAWN_LEFT 128
#define QUESTION_GENERATOR_DESPAWN_RIGHT 384
#define ITEM_EMERGE_FRAMES 16
#define ITEM_WALK_SPEED REF_VEL_TO_FIX(100)
#define ITEM_FAST_SPEED REF_VEL_TO_FIX(200)
#define BRICK_FRAGMENT_LIFETIME 44
TrapManager traps_current;

static OBJATTR trap_oam[TRAPS_MAX_ENTITIES];
static OBJATTR dynamic_trap_oam[TRAPS_MAX_DYNAMIC_SPRITES];

static uint8_t aabb_overlap(int ax, int ay, int aw, int ah,
                            int bx, int by, int bw, int bh)
{
    return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
}

static uint8_t trap_contains_point(const Trap *trap, int world_x_px, int world_y_px)
{
    int x = FIX16_TO_INT(trap->x);
    int y = FIX16_TO_INT(trap->y);
    int w = FIX16_TO_INT(trap->w);
    int h = FIX16_TO_INT(trap->h);

    return world_x_px >= x && world_x_px < x + w &&
           world_y_px >= y && world_y_px < y + h;
}

static uint8_t player_hits_goal_trigger(const Trap *trap, const Player *player)
{
    int player_x = FIX16_TO_INT(player->x);
    int player_y = FIX16_TO_INT(player->y);
    int trap_x = FIX16_TO_INT(trap->x);
    int trap_y = FIX16_TO_INT(trap->y);
    int trap_w = FIX16_TO_INT(trap->w);
    int trap_h = FIX16_TO_INT(trap->h);
    int margin = FIX16_TO_INT(REF_POS_TO_FIX(200));
    int top_slop = FIX16_TO_INT(REF_POS_TO_FIX(200));
    int bottom_trim = FIX16_TO_INT(REF_POS_TO_FIX(3000));

    return player->alive &&
           player_x + PLAYER_WIDTH_PX > trap_x + margin &&
           player_x < trap_x + trap_w - margin &&
           player_y + PLAYER_HEIGHT_PX > trap_y &&
           player_y < trap_y + trap_h + top_slop - bottom_trim;
}

static int floor_div_int(int numerator, int denominator)
{
    if (numerator >= 0) {
        return numerator / denominator;
    }

    return -(((-numerator) + denominator - 1) / denominator);
}

static uint8_t point_to_source_cell(int world_x_px, int world_y_px,
                                    uint16_t *source_x, uint16_t *source_y)
{
    int cell_x = floor_div_int(world_x_px, LEVEL_METATILE_SIZE);
    int cell_y = floor_div_int(world_y_px, LEVEL_METATILE_SIZE) +
                 LEVEL_VIEW_SOURCE_ROW_OFFSET;

    if (cell_x < 0 || cell_x >= LEVEL_SOURCE_COLS ||
        cell_y < 0 || cell_y >= LEVEL_SOURCE_ROWS) {
        return 0;
    }

    *source_x = (uint16_t)cell_x;
    *source_y = (uint16_t)cell_y;
    return 1;
}

static void set_cell_collision(TrapManager *manager, uint16_t source_x,
                               uint16_t source_y, LevelCollision collision,
                               uint8_t hidden)
{
    if (source_x >= LEVEL_SOURCE_COLS || source_y >= LEVEL_SOURCE_ROWS) {
        return;
    }

    manager->cell_collision[source_y][source_x] = (uint8_t)collision;
    manager->cell_hidden[source_y][source_x] = hidden;
}

static void add_dynamic_collider(TrapManager *manager, const Trap *trap)
{
    if (manager->dynamic_collider_count >= TRAPS_MAX_DYNAMIC_COLLIDERS) {
        return;
    }

    manager->dynamic_colliders[manager->dynamic_collider_count++] =
        (uint8_t)(trap - manager->traps);
}

static uint16_t keys_held(void)
{
    return (uint16_t)(~REG_KEYINPUT & 0x03ff);
}

static Trap *add_trap(TrapManager *manager, TrapKind kind,
                      uint16_t source_x, uint16_t source_y,
                      uint16_t width_cells, uint16_t height_cells)
{
    if (manager->trap_count >= TRAPS_MAX_TRAPS) {
        return 0;
    }

    Trap *trap = &manager->traps[manager->trap_count++];

    trap->kind = kind;
    trap->state = TRAP_IDLE;
    trap->source_x = source_x;
    trap->source_y = source_y;
    trap->x = CELL_WORLD_X(source_x);
    trap->y = CELL_WORLD_Y(source_y);
    trap->w = FIX16_FROM_INT(width_cells * LEVEL_METATILE_SIZE);
    trap->h = FIX16_FROM_INT(height_cells * LEVEL_METATILE_SIZE);
    trap->vy = 0;
    trap->timer = 0;
    trap->subtype = 0;

    return trap;
}

static Trap *add_question_block(TrapManager *manager, uint16_t source_x,
                                uint16_t source_y, uint8_t subtype)
{
    Trap *trap = add_trap(manager, TRAP_QUESTION_BLOCK, source_x, source_y, 1, 1);

    if (trap) {
        trap->subtype = subtype;
    }

    return trap;
}

static Trap *add_question_block_visual(TrapManager *manager, Level *level,
                                       uint16_t source_x, uint16_t source_y,
                                       uint8_t subtype, uint8_t visual)
{
    Trap *trap = add_question_block(manager, source_x, source_y, subtype);

    if (trap) {
        level_set_metatile_cell(level, source_x, source_y, visual);
        if (visual == METATILE_EMPTY) {
            set_cell_collision(manager, source_x, source_y,
                               LEVEL_COLLISION_EMPTY, 1);
        } else {
            set_cell_collision(manager, source_x, source_y,
                               LEVEL_COLLISION_SOLID, 0);
        }
    }

    return trap;
}

static uint8_t has_cell_trap(const TrapManager *manager, uint16_t source_x,
                             uint16_t source_y)
{
    for (uint8_t i = 0; i < manager->trap_count; ++i) {
        const Trap *trap = &manager->traps[i];

        if (trap->source_x == source_x && trap->source_y == source_y &&
            (trap->kind == TRAP_QUESTION_BLOCK ||
             trap->kind == TRAP_INVISIBLE_BLOCK ||
             trap->kind == TRAP_EVASIVE_BLOCK ||
             trap->kind == TRAP_HINT_BLOCK)) {
            return 1;
        }
    }

    return 0;
}

static void register_source_coin_blocks(TrapManager *manager, Level *level)
{
    for (uint16_t y = 0; y < level->height; ++y) {
        for (uint16_t x = 0; x < level->width; ++x) {
            uint8_t source = level->source[y][x];

            if (has_cell_trap(manager, x, y)) {
                continue;
            }

            if (source == 2) {
                add_question_block_visual(manager, level, x, y, 0, METATILE_QUESTION);
            } else if (source == 7) {
                add_question_block_visual(manager, level, x, y, 0, METATILE_EMPTY);
            }
        }
    }
}

static Trap *add_world_trap(TrapManager *manager, TrapKind kind,
                            fix16_t x, fix16_t y, fix16_t w, fix16_t h,
                            uint8_t subtype)
{
    if (manager->trap_count >= TRAPS_MAX_TRAPS) {
        return 0;
    }

    Trap *trap = &manager->traps[manager->trap_count++];

    trap->kind = kind;
    trap->state = TRAP_IDLE;
    trap->source_x = 0;
    trap->source_y = 0;
    trap->x = x;
    trap->y = y;
    trap->w = w;
    trap->h = h;
    trap->vy = 0;
    trap->timer = 0;
    trap->subtype = subtype;
    trap->spawn_dir = 0;

    return trap;
}

static void apply_generated_cells(TrapManager *manager, Level *level,
                                  const TrapTrigger *trigger)
{
    uint16_t cols = (uint16_t)(FIX16_TO_INT(trigger->w) / LEVEL_METATILE_SIZE);
    uint16_t rows = (uint16_t)(FIX16_TO_INT(trigger->h) / LEVEL_METATILE_SIZE);

    if (cols == 0) {
        cols = 1;
    }
    if (rows == 0) {
        rows = 1;
    }

    for (uint16_t y = 0; y < rows; ++y) {
        for (uint16_t x = 0; x < cols; ++x) {
            uint16_t source_x = trigger->source_x + x;
            uint16_t source_y = trigger->source_y + y;

            if (source_x >= level->width || source_y >= level->height) {
                continue;
            }

            if (trigger->visual_metatile != 255) {
                level_set_metatile_cell(level, source_x, source_y,
                                        trigger->visual_metatile);
            }

            if (trigger->collision != 255) {
                set_cell_collision(manager, source_x, source_y,
                                   (LevelCollision)trigger->collision,
                                   trigger->hidden);
            }
        }
    }
}

static void add_generated_trap(TrapManager *manager, Level *level,
                               const TrapTrigger *trigger)
{
    Trap *trap = add_world_trap(manager, trigger->kind,
                                trigger->x, trigger->y,
                                trigger->w, trigger->h,
                                trigger->subtype);

    if (!trap) {
        return;
    }

    trap->source_x = trigger->source_x;
    trap->source_y = trigger->source_y;
    trap->spawn_dir = trigger->spawn_dir;

    apply_generated_cells(manager, level, trigger);

    if (trigger->kind == TRAP_ENTER_PIPE || trigger->kind == TRAP_SIDE_PIPE ||
        trigger->kind == TRAP_EVASIVE_BLOCK) {
        add_dynamic_collider(manager, trap);
    }
}

static TrapEntity *spawn_entity(TrapManager *manager, TrapEntityKind kind,
                               fix16_t x, fix16_t y, fix16_t vx, fix16_t vy,
                               uint8_t w, uint8_t h)
{
    for (uint8_t i = 0; i < TRAPS_MAX_ENTITIES; ++i) {
        TrapEntity *entity = &manager->entities[i];

        if (!entity->active) {
            entity->kind = kind;
            entity->active = 1;
            entity->x = x;
            entity->y = y;
            entity->vx = vx;
            entity->vy = vy;
            entity->w = w;
            entity->h = h;
            entity->timer = 0;
            entity->frame = 0;
            return entity;
        }
    }

    return 0;
}

static void load_trap_tiles(void)
{
    memcpy(SPRITE_PALETTE, traps_16Pal, 16 * sizeof(uint16_t));
    memcpy(&SPRITE_PALETTE[EVASIVE_BLOCK_PALETTE_BANK * 16],
           tiles_16Pal, 16 * sizeof(uint16_t));
    memcpy(&SPRITE_PALETTE[ITEM_PALETTE_BANK * 16],
           items_16Pal, 16 * sizeof(uint16_t));
    memcpy(&SPRITE_GFX[TRAP_TILE_INDEX * 16], traps_16Tiles, traps_16TilesLen);
    memcpy(&SPRITE_GFX[EVASIVE_BLOCK_TILE_INDEX * 16],
           &tiles_16Tiles[METATILE_QUESTION * 4 * 8],
           4 * 16 * sizeof(uint16_t));
    memcpy(&SPRITE_GFX[PIPE_TILE_INDEX * 16],
           &tiles_16Tiles[METATILE_PIPE_TOP_LEFT * 4 * 8],
           4 * 16 * sizeof(uint16_t));
    memcpy(&SPRITE_GFX[(PIPE_TILE_INDEX + 4) * 16],
           &tiles_16Tiles[METATILE_PIPE_TOP_RIGHT * 4 * 8],
           4 * 16 * sizeof(uint16_t));
    memcpy(&SPRITE_GFX[(PIPE_TILE_INDEX + 8) * 16],
           &tiles_16Tiles[METATILE_PIPE_BODY_LEFT * 4 * 8],
           4 * 16 * sizeof(uint16_t));
    memcpy(&SPRITE_GFX[(PIPE_TILE_INDEX + 12) * 16],
           &tiles_16Tiles[METATILE_PIPE_BODY_RIGHT * 4 * 8],
           4 * 16 * sizeof(uint16_t));
    memcpy(&SPRITE_GFX[ITEM_TILE_INDEX * 16], items_16Tiles, items_16TilesLen);

    for (uint8_t i = 0; i < 4; ++i) {
        memcpy(&SPRITE_GFX[(BRICK_FRAGMENT_TILE_INDEX + i) * 16],
               &tiles_16Tiles[(METATILE_BRICK * 4 + i) * 8],
               16 * sizeof(uint16_t));
    }

    memcpy(&SPRITE_GFX[FALLING_BRICK_TILE_INDEX * 16],
           &tiles_16Tiles[METATILE_BRICK * 4 * 8],
           4 * 16 * sizeof(uint16_t));

    for (uint8_t i = 0; i < 6; ++i) {
        memcpy(&SPRITE_GFX[(CHECKPOINT_TILE_INDEX + i * 4) * 16],
               &tiles_16Tiles[(METATILE_CHECKPOINT_00 * 4 + i * 4) * 8],
               4 * 16 * sizeof(uint16_t));
    }
}

static void reveal_hidden_block(TrapManager *manager, Trap *trap, Level *level)
{
    if (trap->state != TRAP_IDLE) {
        return;
    }

    trap->state = TRAP_ACTIVE;
    audio_play_block_hit();
    level_set_metatile_cell(level, trap->source_x, trap->source_y, METATILE_BRICK);
    set_cell_collision(manager, trap->source_x, trap->source_y,
                       LEVEL_COLLISION_SOLID, 0);
}

static void trigger_falling_floor(TrapManager *manager, Trap *trap, Level *level)
{
    if (trap->state != TRAP_IDLE) {
        return;
    }

    trap->state = TRAP_ACTIVE;
    trap->timer = 16;
    trap->vy = 0;
    audio_play_trap_trigger();

    for (uint16_t x = 0; x < (uint16_t)FIX16_TO_INT(trap->w) / LEVEL_METATILE_SIZE; ++x) {
        level_set_metatile_cell(level, trap->source_x + x, trap->source_y, METATILE_EMPTY);
        level_set_metatile_cell(level, trap->source_x + x, trap->source_y + 1, METATILE_EMPTY);
        set_cell_collision(manager, trap->source_x + x, trap->source_y,
                           LEVEL_COLLISION_EMPTY, 0);
    }

    for (uint16_t x = 0; x < (uint16_t)FIX16_TO_INT(trap->w) / LEVEL_METATILE_SIZE; ++x) {
        TrapEntity *entity = spawn_entity(manager, TRAP_ENTITY_FALLING_TILE,
                                          trap->x + FIX16_FROM_INT(x * LEVEL_METATILE_SIZE),
                                          trap->y, 0, 0, 16, 16);

        if (entity && trap->subtype == 51) {
            entity->frame = 1;
        }
    }
}

static void trigger_bump_shooter(TrapManager *manager, Trap *trap)
{
    if (trap->state != TRAP_IDLE) {
        return;
    }

    trap->state = TRAP_SPENT;
    audio_play_block_hit();
    audio_play_trap_trigger();
    spawn_entity(manager, TRAP_ENTITY_PROJECTILE,
                 trap->x + FIX16_FROM_INT(4),
                 trap->y - FIX16_FROM_INT(8),
                 0, -PROJECTILE_SPEED, 8, 8);
}

void traps_break_brick(TrapManager *manager, Level *level,
                       uint16_t source_x, uint16_t source_y)
{
    static const fix16_t vx[4] = {
        -REF_VEL_TO_FIX(300), REF_VEL_TO_FIX(300),
        -REF_VEL_TO_FIX(240), REF_VEL_TO_FIX(240),
    };
    static const fix16_t vy[4] = {
        -REF_VEL_TO_FIX(1000), -REF_VEL_TO_FIX(1000),
        -REF_VEL_TO_FIX(1400), -REF_VEL_TO_FIX(1400),
    };

    level->source[source_y][source_x] = 0;
    level_set_metatile_cell(level, source_x, source_y, METATILE_EMPTY);
    set_cell_collision(manager, source_x, source_y, LEVEL_COLLISION_EMPTY, 0);
    audio_play_brick_break();

    for (uint8_t i = 0; i < 4; ++i) {
        TrapEntity *fragment = spawn_entity(
            manager, TRAP_ENTITY_BRICK_FRAGMENT,
            FIX16_FROM_INT(source_x * LEVEL_METATILE_SIZE + (i & 1) * 8),
            FIX16_FROM_INT((source_y - LEVEL_VIEW_SOURCE_ROW_OFFSET) *
                           LEVEL_METATILE_SIZE + (i >> 1) * 8),
            vx[i], vy[i], 8, 8);

        if (fragment) {
            fragment->timer = BRICK_FRAGMENT_LIFETIME;
            fragment->frame = i;
        }
    }
}

static void spend_question_block(TrapManager *manager, Trap *trap, Level *level)
{
    trap->state = TRAP_SPENT;
    level_set_metatile_cell(level, trap->source_x, trap->source_y, METATILE_SOLID);
    set_cell_collision(manager, trap->source_x, trap->source_y,
                       LEVEL_COLLISION_SOLID, 0);
}

static void spawn_coin_popup(TrapManager *manager, Trap *trap)
{
    TrapEntity *coin = spawn_entity(manager, TRAP_ENTITY_COIN_POPUP,
                                    trap->x + FIX16_FROM_INT(4),
                                    trap->y - FIX16_FROM_INT(2),
                                    0, -REF_VEL_TO_FIX(800), 8, 8);

    if (coin) {
        coin->timer = QUESTION_COIN_POPUP_LIFETIME;
    }
}

static void spawn_block_item(TrapManager *manager, Trap *trap,
                             TrapEntityKind kind, fix16_t speed)
{
    TrapEntity *entity = spawn_entity(manager, kind,
                                      trap->x, trap->y,
                                      speed, 0, 16, 16);

    if (entity) {
        entity->timer = ITEM_EMERGE_FRAMES;
    }
}

static int8_t resolve_question_enemy_dir(const Trap *trap, const Player *player)
{
    if (trap->spawn_dir < 0) {
        return -1;
    }
    if (trap->spawn_dir > 0) {
        return 1;
    }

    int block_center = FIX16_TO_INT(trap->x) + FIX16_TO_INT(trap->w) / 2;
    int player_center = FIX16_TO_INT(player->x) + PLAYER_WIDTH_PX / 2;

    return (block_center <= player_center) ? 1 : -1;
}

static void trigger_question_block(TrapManager *manager, Trap *trap, Level *level,
                                   const Player *player)
{
    if (trap->state != TRAP_IDLE) {
        return;
    }

    audio_play_block_hit();

    switch (trap->subtype) {
    case QUESTION_ENEMY:
        audio_play_trap_trigger();
        spend_question_block(manager, trap, level);
        enemies_spawn_from_block(&enemy_current, trap->x, trap->y,
                                 ENEMY_SPRITE_GHOST,
                                 resolve_question_enemy_dir(trap, player));
        break;
    case QUESTION_GOOD_MUSHROOM:
        audio_play_trap_trigger();
        spawn_block_item(manager, trap, TRAP_ENTITY_GOOD_ITEM, ITEM_WALK_SPEED);
        spend_question_block(manager, trap, level);
        break;
    case QUESTION_BAD_MUSHROOM:
        audio_play_trap_trigger();
        spawn_block_item(manager, trap, TRAP_ENTITY_BAD_ITEM, ITEM_WALK_SPEED);
        spend_question_block(manager, trap, level);
        break;
    case QUESTION_STAR:
        audio_play_trap_trigger();
        spawn_block_item(manager, trap, TRAP_ENTITY_STAR_ITEM, ITEM_FAST_SPEED);
        spend_question_block(manager, trap, level);
        break;
    case QUESTION_POISON_GENERATOR:
        trap->state = TRAP_ACTIVE;
        trap->subtype = QUESTION_GENERATOR_ACTIVE;
        trap->timer = 1;
        level_set_metatile_cell(level, trap->source_x, trap->source_y, METATILE_SOLID);
        set_cell_collision(manager, trap->source_x, trap->source_y,
                           LEVEL_COLLISION_SOLID, 0);
        audio_play_trap_trigger();
        break;
    case QUESTION_HIDDEN_POISON:
        audio_play_trap_trigger();
        spawn_block_item(manager, trap, TRAP_ENTITY_BAD_ITEM, ITEM_FAST_SPEED);
        trap->state = TRAP_SPENT;
        level_set_metatile_cell(level, trap->source_x, trap->source_y, METATILE_EMPTY);
        set_cell_collision(manager, trap->source_x, trap->source_y,
                           LEVEL_COLLISION_EMPTY, 1);
        break;
    case QUESTION_COIN_GENERATOR:
        trap->state = TRAP_ACTIVE;
        trap->timer = QUESTION_COIN_INTERVAL;
        trap->vy = 0;
        level_set_metatile_cell(level, trap->source_x, trap->source_y, METATILE_SOLID);
        set_cell_collision(manager, trap->source_x, trap->source_y,
                           LEVEL_COLLISION_SOLID, 0);
        break;
    default:
        spawn_coin_popup(manager, trap);
        spend_question_block(manager, trap, level);
        break;
    }
}

static void trigger_stage_spawner(Trap *trap)
{
    if (trap->state != TRAP_IDLE) {
        return;
    }

    trap->state = TRAP_SPENT;
    audio_play_trap_trigger();

    if (trap->subtype == 100) {
        enemies_spawn_direct(&enemy_current, ENEMY_PIPE_SHOT,
                             trap->x + REF_POS_TO_FIX(1000),
                             REF_STAGE_Y_TO_FIX(32000),
                             ENEMY_SPRITE_ATYPE3, -1);
    } else if (trap->subtype == 101) {
        enemies_spawn_direct(&enemy_current, ENEMY_PIPE_SHOT,
                             trap->x + REF_POS_TO_FIX(6000),
                             REF_STAGE_Y_TO_FIX(-4000),
                             ENEMY_SPRITE_ATYPE3, 1);
    } else if (trap->subtype == 102) {
        for (uint8_t i = 0; i < 4; ++i) {
            enemies_spawn_direct(&enemy_current, ENEMY_WALKER,
                                 trap->x + REF_POS_TO_FIX(i * 3000),
                                 REF_STAGE_Y_TO_FIX(-3000),
                                 ENEMY_SPRITE_GHOST, -1);
        }
    }
}

static void trigger_checkpoint(Trap *trap, Level *level, Player *player)
{
    if (trap->state != TRAP_IDLE) {
        return;
    }

    (void)level;
    trap->state = TRAP_SPENT;
    player_set_checkpoint(player,
                          trap->x - FIX16_FROM_INT(PLAYER_WIDTH_PX),
                          trap->y + trap->h - FIX16_FROM_INT(PLAYER_HEIGHT_PX));
    audio_play_trap_trigger();
}

static void trigger_goal(Trap *trap, Player *player)
{
    if (trap->state != TRAP_IDLE || !player->alive) {
        return;
    }

    trap->state = TRAP_ACTIVE;
    player_begin_goal(player, trap->x);
    audio_stop_bgm();
    audio_play_goal();
    messages_show(MESSAGE_STAGE_CLEAR,
                  player->x - FIX16_FROM_INT(8),
                  player->y - FIX16_FROM_INT(12),
                  90);
}

static void trigger_hint_block(Trap *trap)
{
    audio_play_trap_trigger();
    messages_show(MESSAGE_HINT_STAGE_1,
                  trap->x - FIX16_FROM_INT(24),
                  trap->y - FIX16_FROM_INT(8),
                  120);
}

static void update_evasive_block(Trap *trap, const struct Player *player)
{
    if (trap->subtype == EVASIVE_BLOCK_HORIZONTAL) {
        return;
    }

    int player_x = FIX16_TO_INT(player->x);
    int player_y = FIX16_TO_INT(player->y);
    int trap_x = FIX16_TO_INT(trap->x);
    int trap_y = FIX16_TO_INT(trap->y);
    uint8_t player_rising_under_block =
        player->alive &&
        player->vy < 0 &&
        player_x + PLAYER_WIDTH_PX > trap_x - 8 &&
        player_x < trap_x + LEVEL_METATILE_SIZE + 8 &&
        player_y > trap_y &&
        player_y < trap_y + LEVEL_METATILE_SIZE + 48;

    if (player_rising_under_block) {
        fix16_t target_y = player->y - EVASIVE_BLOCK_PLAYER_OFFSET;

        if (target_y < trap->y) {
            trap->y = target_y;
        }
    }
}

static void bump_evasive_block(Trap *trap, int hit_x, int hit_y)
{
    if (trap->subtype == EVASIVE_BLOCK_HORIZONTAL) {
        int trap_mid_x = FIX16_TO_INT(trap->x) + LEVEL_METATILE_SIZE / 2;

        if (hit_x < trap_mid_x) {
            trap->x += EVASIVE_BLOCK_HORIZONTAL_DODGE;
        } else {
            trap->x -= EVASIVE_BLOCK_HORIZONTAL_DODGE;
        }
    } else if (trap->y > FIX16_FROM_INT(hit_y) - EVASIVE_BLOCK_PLAYER_OFFSET) {
        trap->y = FIX16_FROM_INT(hit_y) - EVASIVE_BLOCK_PLAYER_OFFSET;
    }
}

static uint8_t player_on_enter_pipe(const Trap *trap, const struct Player *player)
{
    int player_x = FIX16_TO_INT(player->x);
    int player_y = FIX16_TO_INT(player->y);
    int pipe_x = FIX16_TO_INT(trap->x);
    int pipe_y = FIX16_TO_INT(trap->y);
    int pipe_w = FIX16_TO_INT(trap->w);

    return player->alive &&
           player->on_ground &&
           player_x + PLAYER_WIDTH_PX > pipe_x + 8 &&
           player_x < pipe_x + pipe_w - 8 &&
           player_y + PLAYER_HEIGHT_PX >= pipe_y - 2 &&
           player_y + PLAYER_HEIGHT_PX <= pipe_y + 8;
}

static void update_enter_pipe(TrapManager *manager, Trap *trap, struct Player *player)
{
    if (trap->state == TRAP_IDLE) {
        if (player_on_enter_pipe(trap, player) && (keys_held() & KEY_DOWN_MASK)) {
            trap->state = TRAP_ACTIVE;
            trap->timer = 1;
            trap->vy = REF_VEL_TO_FIX(100);
            player->control_locked = 1;
            player->hidden = 1;
            player->vx = 0;
            player->vy = 0;
            player->x = trap->x + (trap->w - FIX16_FROM_INT(PLAYER_WIDTH_PX)) / 2;
            audio_play_trap_trigger();
        }
        return;
    }

    if (trap->state != TRAP_ACTIVE) {
        return;
    }

    player->control_locked = 1;
    player->hidden = 1;
    player->vx = 0;
    player->vy = 0;
    player->x = trap->x + (trap->w - FIX16_FROM_INT(PLAYER_WIDTH_PX)) / 2;

    if (trap->timer <= PIPE_PLAYER_SINK_FRAMES) {
        player->y += PIPE_PLAYER_SINK_SPEED;
    }

    if (trap->subtype == 1) {
        if (trap->timer == PIPE_TRANSITION_FRAME) {
            manager->stage_transition_requested = 1;
            manager->stage_transition_target = PIPE_TRANSITION_1_2_UNDERGROUND;
            trap->state = TRAP_SPENT;
        } else if (trap->timer < 255) {
            trap->timer++;
        }
        return;
    }

    if (trap->timer == 23) {
        trap->x -= FIX16_ONE;
    } else if (trap->timer >= PIPE_SHAKE_START_FRAME &&
               trap->timer < PIPE_RISE_START_FRAME) {
        fix16_t shake = 0;

        if (trap->timer <= 60) {
            shake = FIX16_FROM_INT(2);
        } else if (trap->timer <= 77) {
            shake = FIX16_FROM_INT(4);
        } else if (trap->timer <= 94) {
            shake = FIX16_FROM_INT(6);
        }

        if (shake != 0) {
            trap->x += (trap->timer & 1) ? -shake : shake;
        }
    } else if (trap->timer >= PIPE_RISE_START_FRAME) {
        trap->y -= trap->vy;
        trap->vy += PIPE_RISE_ACCEL;
        if (trap->vy > PIPE_RISE_MAX_SPEED) {
            trap->vy = PIPE_RISE_MAX_SPEED;
        }
    }

    if (trap->timer == PIPE_KILL_FRAME) {
        player_kill(player);
        trap->state = TRAP_SPENT;
    } else if (trap->timer < 255) {
        trap->timer++;
    }
}

void traps_init_video(void)
{
    for (uint8_t i = 0; i < TRAPS_MAX_ENTITIES; ++i) {
        trap_oam[i].attr0 = ATTR0_DISABLED;
        trap_oam[i].attr1 = 0;
        trap_oam[i].attr2 = 0;
    }

    for (uint8_t i = 0; i < TRAPS_MAX_DYNAMIC_SPRITES; ++i) {
        dynamic_trap_oam[i].attr0 = ATTR0_DISABLED;
        dynamic_trap_oam[i].attr1 = 0;
        dynamic_trap_oam[i].attr2 = 0;
    }

    load_trap_tiles();
}

void traps_load_1_1(TrapManager *manager, Level *level)
{
    memset(manager, 0, sizeof(*manager));

    for (uint16_t i = 0; i < level1_trap_count; ++i) {
        add_generated_trap(manager, level, &level1_traps[i]);
    }
}

void traps_load_1_2(TrapManager *manager, Level *level)
{
    memset(manager, 0, sizeof(*manager));

    for (uint16_t y = 0; y < level->height; ++y) {
        for (uint16_t x = 0; x < level->width; ++x) {
            if (level->source[y][x] == 7) {
                add_trap(manager, TRAP_INVISIBLE_BLOCK, x, y, 1, 1);
                set_cell_collision(manager, x, y, LEVEL_COLLISION_EMPTY, 1);
            }
        }
    }

    add_question_block_visual(manager, level, 13, 8, QUESTION_HIDDEN_POISON,
                              METATILE_EMPTY);

    Trap *hint = add_trap(manager, TRAP_HINT_BLOCK, 4, 9, 1, 1);
    if (hint) {
        hint->subtype = 1;
        level_set_metatile_cell(level, 4, 9, METATILE_HINT_BLOCK);
        set_cell_collision(manager, 4, 9, LEVEL_COLLISION_SOLID, 0);
    }

    register_source_coin_blocks(manager, level);

    Trap *entry_pipe = add_world_trap(manager, TRAP_ENTER_PIPE,
                                      REF_STAGE_X_TO_FIX(14 * 29 * 100 + 500),
                                      REF_STAGE_Y_TO_FIX((9 * 29 - 12) * 100),
                                      REF_POS_TO_FIX(6000),
                                      REF_POS_TO_FIX(12000 - 200),
                                      1);
    if (entry_pipe) {
        add_dynamic_collider(manager, entry_pipe);
    }

    Trap *side_pipe = add_world_trap(manager, TRAP_SIDE_PIPE,
                                    REF_STAGE_X_TO_FIX(12 * 29 * 100),
                                    REF_STAGE_Y_TO_FIX((11 * 29 - 12) * 100),
                                    REF_POS_TO_FIX(3000),
                                    REF_POS_TO_FIX(6000 - 200),
                                    0);
    if (side_pipe) {
        add_dynamic_collider(manager, side_pipe);
    }

    add_world_trap(manager, TRAP_STAGE_SPAWNER,
                   REF_POS_TO_FIX(14 * 29 * 100 + 1000),
                   REF_STAGE_Y_TO_FIX(-6000),
                   REF_POS_TO_FIX(5000), REF_POS_TO_FIX(70000), 100);
}

void traps_load_1_2_underground(TrapManager *manager, Level *level)
{
    memset(manager, 0, sizeof(*manager));

    for (uint16_t y = 0; y < level->height; ++y) {
        for (uint16_t x = 0; x < level->width; ++x) {
            if (level->source[y][x] == 7) {
                add_trap(manager, TRAP_INVISIBLE_BLOCK, x, y, 1, 1);
                set_cell_collision(manager, x, y, LEVEL_COLLISION_EMPTY, 1);
            }
        }
    }

    register_source_coin_blocks(manager, level);
}

void traps_prepare_player_collision(TrapManager *manager, const struct Player *player)
{
    for (uint8_t i = 0; i < manager->trap_count; ++i) {
        Trap *trap = &manager->traps[i];

        if (trap->kind == TRAP_EVASIVE_BLOCK) {
            update_evasive_block(trap, player);
        }
    }
}

static uint8_t entity_solid_at(const Level *level, int x, int y)
{
    return level_collision_at(level, x, y) == LEVEL_COLLISION_SOLID;
}

static uint8_t moving_entity_hits_solid(const Level *level, const TrapEntity *entity,
                                        int x, int y)
{
    int left = x + 1;
    int right = x + entity->w - 2;
    int top = y + 1;
    int bottom = y + entity->h - 1;

    return entity_solid_at(level, left, top) ||
           entity_solid_at(level, right, top) ||
           entity_solid_at(level, left, bottom) ||
           entity_solid_at(level, right, bottom);
}

static void update_moving_item(TrapEntity *entity, const Level *level)
{
    if (entity->timer > 0) {
        entity->y -= FIX16_FROM_INT(1);
        entity->timer--;
        return;
    }

    entity->x += entity->vx;

    int px = FIX16_TO_INT(entity->x);
    int py = FIX16_TO_INT(entity->y);

    if (moving_entity_hits_solid(level, entity, px, py)) {
        if (entity->vx > 0) {
            int tile_right = (px + entity->w - 1) / LEVEL_METATILE_SIZE;
            entity->x = FIX16_FROM_INT(tile_right * LEVEL_METATILE_SIZE - entity->w);
        } else if (entity->vx < 0) {
            int tile_left = px / LEVEL_METATILE_SIZE;
            entity->x = FIX16_FROM_INT((tile_left + 1) * LEVEL_METATILE_SIZE);
        }
        entity->vx = -entity->vx;
    }

    entity->vy += FALL_GRAVITY;
    if (entity->vy > FALL_MAX_SPEED) {
        entity->vy = FALL_MAX_SPEED;
    }

    entity->y += entity->vy;
    px = FIX16_TO_INT(entity->x);
    py = FIX16_TO_INT(entity->y);

    if (!moving_entity_hits_solid(level, entity, px, py)) {
        return;
    }

    if (entity->vy > 0) {
        int tile_bottom = (py + entity->h - 1) / LEVEL_METATILE_SIZE;
        entity->y = FIX16_FROM_INT(tile_bottom * LEVEL_METATILE_SIZE - entity->h);
        if (entity->kind == TRAP_ENTITY_STAR_ITEM) {
            entity->vy = -REF_VEL_TO_FIX(1400);
        } else {
            entity->vy = 0;
        }
    } else if (entity->vy < 0) {
        int tile_top = py / LEVEL_METATILE_SIZE;
        entity->y = FIX16_FROM_INT((tile_top + 1) * LEVEL_METATILE_SIZE);
        entity->vy = 0;
    }
}

void traps_update(TrapManager *manager, Level *level, struct Player *player)
{
    int player_x = FIX16_TO_INT(player->x);
    int player_y = FIX16_TO_INT(player->y);

    for (uint8_t i = 0; i < manager->trap_count; ++i) {
        Trap *trap = &manager->traps[i];

        if (trap->kind == TRAP_FALLING_FLOOR && trap->state == TRAP_IDLE) {
            int trap_x = FIX16_TO_INT(trap->x);
            int trap_y = FIX16_TO_INT(trap->y);
            int trap_w = FIX16_TO_INT(trap->w);

            if (trap->subtype == 51) {
                int left_margin = FIX16_TO_INT(REF_POS_TO_FIX(3200));
                int right_margin = FIX16_TO_INT(REF_POS_TO_FIX(200));
                int trigger_y = trap_y + FIX16_TO_INT(REF_POS_TO_FIX(3000));

                if (player_x + PLAYER_WIDTH_PX > trap_x + left_margin &&
                    player_x < trap_x + trap_w - right_margin &&
                    player_y + PLAYER_HEIGHT_PX > trigger_y) {
                    trigger_falling_floor(manager, trap, level);
                }
            } else if (player->on_ground &&
                       player_x + PLAYER_WIDTH_PX > trap_x + 4 &&
                       player_x < trap_x + trap_w - 4 &&
                       player_y + PLAYER_HEIGHT_PX >= trap_y &&
                       player_y + PLAYER_HEIGHT_PX <= trap_y + 4) {
                trigger_falling_floor(manager, trap, level);
            }
        } else if (trap->kind == TRAP_STAGE_SPAWNER && trap->state == TRAP_IDLE) {
            int trap_x = FIX16_TO_INT(trap->x);
            int trap_y = FIX16_TO_INT(trap->y);
            int trap_w = FIX16_TO_INT(trap->w);
            int trap_h = FIX16_TO_INT(trap->h);

            if (aabb_overlap(player_x, player_y, PLAYER_WIDTH_PX, PLAYER_HEIGHT_PX,
                             trap_x, trap_y, trap_w, trap_h)) {
                trigger_stage_spawner(trap);
            }
        } else if (trap->kind == TRAP_ENTER_PIPE) {
            update_enter_pipe(manager, trap, player);
        } else if (trap->kind == TRAP_CHECKPOINT && trap->state == TRAP_IDLE) {
            int trap_x = FIX16_TO_INT(trap->x);
            int trap_y = FIX16_TO_INT(trap->y);
            int trap_w = FIX16_TO_INT(trap->w);
            int trap_h = FIX16_TO_INT(trap->h);

            if (aabb_overlap(player_x, player_y, PLAYER_WIDTH_PX, PLAYER_HEIGHT_PX,
                             trap_x, trap_y, trap_w, trap_h)) {
                trigger_checkpoint(trap, level, player);
            }
        } else if (trap->kind == TRAP_GOAL && trap->state == TRAP_IDLE) {
            if (player_hits_goal_trigger(trap, player)) {
                trigger_goal(trap, player);
            }
        } else if (trap->kind == TRAP_GOAL && trap->state == TRAP_ACTIVE &&
                   !player->alive) {
            trap->state = TRAP_IDLE;
        } else if (trap->kind == TRAP_QUESTION_BLOCK && trap->state == TRAP_ACTIVE) {
            if (trap->subtype == QUESTION_COIN_GENERATOR) {
                if (trap->timer >= QUESTION_COIN_INTERVAL) {
                    trap->timer = 0;
                    spawn_coin_popup(manager, trap);
                    trap->vy += FIX16_ONE;
                }

                if (FIX16_TO_INT(trap->vy) >= QUESTION_COIN_LIMIT) {
                    spend_question_block(manager, trap, level);
                } else {
                    trap->timer++;
                }
            } else if (trap->subtype == QUESTION_GENERATOR_ACTIVE) {
                trap->timer++;
                if (trap->timer >= QUESTION_GENERATOR_INTERVAL) {
                    trap->timer = 0;
                    spawn_block_item(manager, trap, TRAP_ENTITY_BAD_ITEM, ITEM_FAST_SPEED);
                }
            }
        }
    }

    if (player_goal_clear_done(player)) {
        manager->stage_clear_requested = 1;
    }

    for (uint8_t i = 0; i < TRAPS_MAX_ENTITIES; ++i) {
        TrapEntity *entity = &manager->entities[i];

        if (!entity->active) {
            continue;
        }

        if (entity->kind == TRAP_ENTITY_BRICK_FRAGMENT ||
            entity->kind == TRAP_ENTITY_COIN_POPUP) {
            if (entity->timer > 0) {
                entity->timer--;
            } else {
                entity->active = 0;
                continue;
            }
        }

        if (entity->kind == TRAP_ENTITY_GOOD_ITEM ||
            entity->kind == TRAP_ENTITY_BAD_ITEM ||
            entity->kind == TRAP_ENTITY_STAR_ITEM) {
            update_moving_item(entity, level);
        } else {
            entity->x += entity->vx;
            entity->y += entity->vy;
        }

        if (entity->kind == TRAP_ENTITY_PROJECTILE) {
            entity->vy += PROJECTILE_GRAVITY;
        } else if (entity->kind == TRAP_ENTITY_FALLING_TILE ||
                   entity->kind == TRAP_ENTITY_BRICK_FRAGMENT) {
            entity->vy += FALL_GRAVITY;
            if (entity->vy > FALL_MAX_SPEED) {
                entity->vy = FALL_MAX_SPEED;
            }
        } else if (entity->kind == TRAP_ENTITY_COIN_POPUP) {
            entity->vy += PROJECTILE_GRAVITY;
        }

        int entity_x = FIX16_TO_INT(entity->x);
        int entity_y = FIX16_TO_INT(entity->y);

        if (entity->kind == TRAP_ENTITY_BAD_ITEM &&
            (entity_x < player_x - QUESTION_GENERATOR_DESPAWN_LEFT ||
             entity_x > player_x + QUESTION_GENERATOR_DESPAWN_RIGHT)) {
            entity->active = 0;
            continue;
        }

        if (entity_y > level_death_y_px(level) + 64 || entity_y < -96) {
            entity->active = 0;
            continue;
        }

        if (player->alive &&
            aabb_overlap(player_x, player_y, PLAYER_WIDTH_PX, PLAYER_HEIGHT_PX,
                         entity_x, entity_y, entity->w, entity->h)) {
            if (entity->kind == TRAP_ENTITY_GOOD_ITEM ||
                entity->kind == TRAP_ENTITY_COIN_POPUP) {
                if (entity->kind == TRAP_ENTITY_GOOD_ITEM) {
                    messages_show(MESSAGE_PLAYER_TASTY,
                                  player->x + FIX16_FROM_INT(PLAYER_WIDTH_PX),
                                  player->y, 45);
                }
                entity->active = 0;
                audio_play_trap_trigger();
            } else {
                MessageId message = MESSAGE_PLAYER_BAD_MUSHROOM;
                if (entity->kind == TRAP_ENTITY_STAR_ITEM) {
                    message = MESSAGE_PLAYER_STABBED;
                }
                entity->active = 0;
                messages_show(message,
                              player->x + FIX16_FROM_INT(PLAYER_WIDTH_PX),
                              player->y, 55);
                player_kill(player);
            }
        }
    }
}

void traps_draw(TrapManager *manager, const struct Camera *camera)
{
    for (uint8_t i = 0; i < TRAPS_MAX_ENTITIES; ++i) {
        TrapEntity *entity = &manager->entities[i];
        OBJATTR *obj = &trap_oam[i];
        int screen_x = camera_world_to_screen_x(camera, entity->x);
        int screen_y = camera_world_to_screen_y(camera, entity->y);

        if (!entity->active ||
            !camera_sprite_visible(screen_x, screen_y, entity->w, entity->h)) {
            obj->attr0 = ATTR0_DISABLED;
        } else {
            uint16_t tile = TRAP_TILE_INDEX;
            if (entity->kind == TRAP_ENTITY_PROJECTILE) {
                tile = PROJECTILE_TILE_INDEX;
            } else if (entity->kind == TRAP_ENTITY_FALLING_TILE &&
                       entity->frame == 1) {
                tile = FALLING_BRICK_TILE_INDEX;
            } else if (entity->kind == TRAP_ENTITY_COIN_POPUP) {
                tile = COIN_TILE_INDEX;
            } else if (entity->kind == TRAP_ENTITY_GOOD_ITEM) {
                tile = GOOD_ITEM_TILE_INDEX;
            } else if (entity->kind == TRAP_ENTITY_BAD_ITEM) {
                tile = BAD_ITEM_TILE_INDEX;
            } else if (entity->kind == TRAP_ENTITY_STAR_ITEM) {
                tile = STAR_ITEM_TILE_INDEX;
            } else if (entity->kind == TRAP_ENTITY_BRICK_FRAGMENT) {
                tile = BRICK_FRAGMENT_TILE_INDEX + entity->frame;
            }
            uint16_t size = (entity->kind == TRAP_ENTITY_PROJECTILE ||
                             entity->kind == TRAP_ENTITY_COIN_POPUP ||
                             entity->kind == TRAP_ENTITY_BRICK_FRAGMENT)
                                ? ATTR1_SIZE_8
                                : ATTR1_SIZE_16;
            uint16_t palette = 0;

            if (entity->kind == TRAP_ENTITY_BRICK_FRAGMENT ||
                (entity->kind == TRAP_ENTITY_FALLING_TILE && entity->frame == 1)) {
                palette = EVASIVE_BLOCK_PALETTE_BANK;
            } else if (entity->kind == TRAP_ENTITY_COIN_POPUP ||
                       entity->kind == TRAP_ENTITY_GOOD_ITEM ||
                       entity->kind == TRAP_ENTITY_BAD_ITEM ||
                       entity->kind == TRAP_ENTITY_STAR_ITEM) {
                palette = ITEM_PALETTE_BANK;
            }

            obj->attr0 = (uint16_t)((screen_y & 0x00ff) | ATTR0_COLOR_16 | ATTR0_SQUARE);
            obj->attr1 = (uint16_t)((screen_x & 0x01ff) | size);
            obj->attr2 = (uint16_t)(OBJ_CHAR(tile) | ATTR2_PALETTE(palette));
        }

        OAM[FIRST_TRAP_SPRITE + i] = *obj;
    }

    uint8_t dynamic_index = 0;
    for (uint8_t i = 0; i < manager->trap_count; ++i) {
        Trap *trap = &manager->traps[i];

        if (trap->kind == TRAP_EVASIVE_BLOCK) {
            int screen_x = camera_world_to_screen_x(camera, trap->x);
            int screen_y = camera_world_to_screen_y(camera, trap->y);
            OBJATTR *obj = &dynamic_trap_oam[dynamic_index];

            if (!camera_sprite_visible(screen_x, screen_y,
                                       LEVEL_METATILE_SIZE, LEVEL_METATILE_SIZE)) {
                obj->attr0 = ATTR0_DISABLED;
            } else {
                obj->attr0 = (uint16_t)((screen_y & 0x00ff) | ATTR0_COLOR_16 | ATTR0_SQUARE);
                obj->attr1 = (uint16_t)((screen_x & 0x01ff) | ATTR1_SIZE_16);
                obj->attr2 = (uint16_t)(OBJ_CHAR(EVASIVE_BLOCK_TILE_INDEX) |
                                        ATTR2_PALETTE(EVASIVE_BLOCK_PALETTE_BANK));
            }

            OAM[DYNAMIC_TRAP_OAM_BASE + dynamic_index] = *obj;
            dynamic_index++;
        } else if (trap->kind == TRAP_CHECKPOINT && trap->state == TRAP_IDLE) {
            int base_x = camera_world_to_screen_x(camera, trap->x);
            int base_y = camera_world_to_screen_y(camera, trap->y);

            for (int row = 0; row < 2 && dynamic_index + 1 < TRAPS_MAX_DYNAMIC_SPRITES; ++row) {
                for (int col = 0; col < 2 && dynamic_index < TRAPS_MAX_DYNAMIC_SPRITES; ++col) {
                    int screen_x = base_x + col * LEVEL_METATILE_SIZE;
                    int screen_y = base_y + row * LEVEL_METATILE_SIZE;
                    uint16_t tile = CHECKPOINT_TILE_INDEX + (row * 2 + col) * 4;
                    OBJATTR *obj = &dynamic_trap_oam[dynamic_index];

                    if (!camera_sprite_visible(screen_x, screen_y,
                                               LEVEL_METATILE_SIZE, LEVEL_METATILE_SIZE)) {
                        obj->attr0 = ATTR0_DISABLED;
                    } else {
                        obj->attr0 = (uint16_t)((screen_y & 0x00ff) |
                                                ATTR0_COLOR_16 | ATTR0_SQUARE);
                        obj->attr1 = (uint16_t)((screen_x & 0x01ff) | ATTR1_SIZE_16);
                        obj->attr2 = (uint16_t)(OBJ_CHAR(tile) |
                                                ATTR2_PALETTE(EVASIVE_BLOCK_PALETTE_BANK));
                    }

                    OAM[DYNAMIC_TRAP_OAM_BASE + dynamic_index] = *obj;
                    dynamic_index++;
                }
            }
        } else if (trap->kind == TRAP_ENTER_PIPE || trap->kind == TRAP_SIDE_PIPE) {
            int base_x = camera_world_to_screen_x(camera, trap->x);
            int base_y = camera_world_to_screen_y(camera, trap->y);
            int rows = FIX16_TO_INT(trap->h) / LEVEL_METATILE_SIZE;

            if (rows < 1) {
                rows = 1;
            }

            for (int row = 0; row < rows && dynamic_index + 1 < TRAPS_MAX_DYNAMIC_SPRITES; ++row) {
                for (int col = 0; col < 2 && dynamic_index < TRAPS_MAX_DYNAMIC_SPRITES; ++col) {
                    int screen_x = base_x + col * LEVEL_METATILE_SIZE;
                    int screen_y = base_y + row * LEVEL_METATILE_SIZE;
                    uint16_t tile = PIPE_TILE_INDEX +
                                    (row == 0 ? 0 : 8) +
                                    (col == 0 ? 0 : 4);
                    OBJATTR *obj = &dynamic_trap_oam[dynamic_index];

                    if (!camera_sprite_visible(screen_x, screen_y,
                                               LEVEL_METATILE_SIZE, LEVEL_METATILE_SIZE)) {
                        obj->attr0 = ATTR0_DISABLED;
                    } else {
                        obj->attr0 = (uint16_t)((screen_y & 0x00ff) |
                                                ATTR0_COLOR_16 | ATTR0_SQUARE);
                        obj->attr1 = (uint16_t)((screen_x & 0x01ff) | ATTR1_SIZE_16);
                        obj->attr2 = (uint16_t)(OBJ_CHAR(tile) |
                                                ATTR2_PALETTE(EVASIVE_BLOCK_PALETTE_BANK) |
                                                ATTR2_PRIORITY(0));
                    }

                    OAM[DYNAMIC_TRAP_OAM_BASE + dynamic_index] = *obj;
                    dynamic_index++;
                }
            }
        }

        if (dynamic_index >= TRAPS_MAX_DYNAMIC_SPRITES) {
            break;
        }
    }

    for (; dynamic_index < TRAPS_MAX_DYNAMIC_SPRITES; ++dynamic_index) {
        dynamic_trap_oam[dynamic_index].attr0 = ATTR0_DISABLED;
        OAM[DYNAMIC_TRAP_OAM_BASE + dynamic_index] = dynamic_trap_oam[dynamic_index];
    }
}

LevelCollision traps_collision_at(const TrapManager *manager, int world_x_px, int world_y_px)
{
    uint16_t source_x = 0;
    uint16_t source_y = 0;

    if (point_to_source_cell(world_x_px, world_y_px, &source_x, &source_y) &&
        !manager->cell_hidden[source_y][source_x] &&
        manager->cell_collision[source_y][source_x] != LEVEL_COLLISION_EMPTY) {
        return (LevelCollision)manager->cell_collision[source_y][source_x];
    }

    for (uint8_t i = 0; i < manager->dynamic_collider_count; ++i) {
        const Trap *trap = &manager->traps[manager->dynamic_colliders[i]];

        if (!trap_contains_point(trap, world_x_px, world_y_px)) {
            continue;
        }

        if (trap->kind == TRAP_EVASIVE_BLOCK) {
            return LEVEL_COLLISION_SOLID;
        }

        if (trap->kind == TRAP_ENTER_PIPE || trap->kind == TRAP_SIDE_PIPE) {
            return LEVEL_COLLISION_SOLID;
        }
    }

    return LEVEL_COLLISION_EMPTY;
}

uint8_t traps_hides_collision_at(const TrapManager *manager, int world_x_px, int world_y_px)
{
    uint16_t source_x = 0;
    uint16_t source_y = 0;

    if (!point_to_source_cell(world_x_px, world_y_px, &source_x, &source_y)) {
        return 0;
    }

    return manager->cell_hidden[source_y][source_x];
}

uint8_t traps_on_player_bump(TrapManager *manager, Level *level, const Player *player,
                             int world_x_px, int world_y_px)
{
    uint8_t triggered = 0;

    for (uint8_t i = 0; i < manager->trap_count; ++i) {
        Trap *trap = &manager->traps[i];

        if (!trap_contains_point(trap, world_x_px, world_y_px)) {
            continue;
        }

        if (trap->kind == TRAP_INVISIBLE_BLOCK) {
            reveal_hidden_block(manager, trap, level);
            triggered = 1;
        } else if (trap->kind == TRAP_BUMP_SHOOTER) {
            trigger_bump_shooter(manager, trap);
            triggered = 1;
        } else if (trap->kind == TRAP_QUESTION_BLOCK) {
            trigger_question_block(manager, trap, level, player);
            triggered = 1;
        } else if (trap->kind == TRAP_EVASIVE_BLOCK) {
            bump_evasive_block(trap, world_x_px, world_y_px);
        } else if (trap->kind == TRAP_HINT_BLOCK) {
            trigger_hint_block(trap);
            triggered = 1;
        }
    }

    return triggered;
}

uint8_t traps_stage_clear_requested(const TrapManager *manager)
{
    return manager->stage_clear_requested;
}

void traps_ack_stage_clear(TrapManager *manager)
{
    manager->stage_clear_requested = 0;
}

uint8_t traps_stage_transition_requested(const TrapManager *manager)
{
    return manager->stage_transition_requested;
}

uint8_t traps_stage_transition_target(const TrapManager *manager)
{
    return manager->stage_transition_target;
}

void traps_ack_stage_transition(TrapManager *manager)
{
    manager->stage_transition_requested = 0;
    manager->stage_transition_target = 0;
}
