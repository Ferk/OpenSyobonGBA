#include "traps.h"

#include <gba.h>
#include <string.h>

#include "audio.h"
#include "camera.h"
#include "enemy.h"
#include "generated/level1_data.h"
#include "generated/level1_2_data.h"
#include "generated/level1_2b_data.h"
#include "generated/level1_2u_data.h"
#include "messages.h"
#include "player.h"
#include "text.h"
#include "items_16.h"
#include "spike_block_16.h"
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
#define QUESTION_BALL_TILE_INDEX (ITEM_TILE_INDEX + 16)
#define BRICK_FRAGMENT_TILE_INDEX 120
#define SPIKE_BLOCK_TOP_TILE_INDEX 124
#define SPIKE_BLOCK_LEFT_TILE_INDEX 128
#define SPIKE_BLOCK_RIGHT_TILE_INDEX 132
#define SIDE_PIPE_TILE_INDEX 136
#define GOAL_TILE_INDEX 152
#define PLATFORM_TILE_INDEX 176
#define PLATFORM_VISUAL_SLOTS 4
/* Keep this past trap overlays/checkpoint tiles; enemies start at tile 256. */
#define FALLING_TILE_DYNAMIC_INDEX 224
#define FALLING_TILE_DYNAMIC_SLOTS 16
#define CHECKPOINT_TILE_INDEX 192
#define EVASIVE_BLOCK_PALETTE_BANK 1
#define ITEM_PALETTE_BANK 3
#define UNDERGROUND_TILE_PALETTE_BANK 5
#define RGB15(r, g, b) ((uint16_t)((r) | ((g) << 5) | ((b) << 10)))
#define TRAPS_MAX_DYNAMIC_SPRITES 16

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
#define SIDE_PIPE_PLAYER_SINK_SPEED REF_POS_TO_FIX(240)
#define SIDE_PIPE_PLAYER_POP_UP REF_POS_TO_FIX(1100)
#define SIDE_PIPE_PLAYER_BLAST_SPEED REF_POS_TO_FIX(2000)
#define PIPE_RISE_ACCEL REF_ACCEL_TO_FIX(80)
#define PIPE_RISE_MAX_SPEED REF_VEL_TO_FIX(1600)
#define PIPE_TRANSITION_FRAME 20
#define SIDE_PIPE_KILL_FRAME 48
#define PIPE_TRANSITION_1_2 1
#define PIPE_TRANSITION_1_2_UNDERGROUND 2
#define PIPE_TRANSITION_1_2B 3
#define KEY_DOWN_MASK 0x0080
#define KEY_LEFT_MASK 0x0020
#define KEY_RIGHT_MASK 0x0010
#define FALLING_ON_PASS_UNDER 51
#define FALLING_WHEN_APPROACHED 52
#define STAGE_UPWARD_HAZARD 181
#define STAGE_UPWARD_HAZARD_TOGGLE 182
#define STAGE_UPWARD_HAZARD_LEFT 188
#define STAGE_HINT_MESSAGE 187
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
#define QUESTION_DEFAULT_SPAWN_LIMIT 1
#define QUESTION_COIN_POPUP_LIFETIME 16
#define QUESTION_GENERATOR_DESPAWN_LEFT 128
#define QUESTION_GENERATOR_DESPAWN_RIGHT 384
#define STAGE_PIPE_HAZARD 180
#define PLATFORM_VERTICAL_WRAP 5
#define ITEM_EMERGE_FRAMES 16
#define ITEM_WALK_SPEED REF_VEL_TO_FIX(100)
#define ITEM_FAST_SPEED REF_VEL_TO_FIX(200)
#define HURT_ITEM_TILE_INDEX BAD_ITEM_TILE_INDEX
#define BRICK_FRAGMENT_LIFETIME 44
#define TRAP_CHANNEL_NONE 0
#define FAKE_GOAL_BOUNCE REF_VEL_TO_FIX(-900)
#define FAKE_GOAL_MOVE_SPEED REF_VEL_TO_FIX(400)
#define FAKE_GOAL_TOP_PALETTE_BANK 6
TrapManager traps_current;

static OBJATTR trap_oam[TRAPS_MAX_ENTITIES];
static uint16_t trap_rng = 0xace1;
static uint8_t platform_visual_metatiles[PLATFORM_VISUAL_SLOTS];
static uint8_t platform_visual_count;

static const uint16_t underground_tile_obj_palette[16] = {
    RGB15(31, 0, 31), RGB15(20, 27, 31), RGB15(11, 11, 12), RGB15(23, 23, 24),
    RGB15(31, 27, 10), RGB15(11, 21, 7), RGB15(7, 14, 4), RGB15(15, 15, 16),
    RGB15(7, 7, 8), RGB15(27, 27, 28), RGB15(19, 19, 20), RGB15(5, 5, 6),
    RGB15(31, 31, 31), RGB15(0, 28, 0), RGB15(0, 18, 0), RGB15(0, 0, 0),
};
static OBJATTR dynamic_trap_oam[TRAPS_MAX_DYNAMIC_SPRITES];

static void emit_trap_channel(TrapManager *manager, Level *level,
                              Player *player, uint8_t channel);

static uint8_t aabb_overlap(int ax, int ay, int aw, int ah,
                            int bx, int by, int bw, int bh)
{
    return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
}

static uint16_t trap_random(uint16_t max)
{
    trap_rng = (uint16_t)(trap_rng * 109u + 1021u);
    return max ? (uint16_t)(trap_rng % max) : 0;
}

static uint8_t obj_tile_pixel(uint16_t tile_index, uint8_t x, uint8_t y)
{
    const uint16_t *tile = &SPRITE_GFX[tile_index * 16];
    uint16_t packed = tile[y * 2 + x / 4];
    uint8_t shift = (uint8_t)((x & 3) * 4);

    return (uint8_t)((packed >> shift) & 0x0f);
}

static void set_obj_tile_pixel(uint16_t tile_index, uint8_t x, uint8_t y,
                               uint8_t color)
{
    uint16_t *tile = &SPRITE_GFX[tile_index * 16];
    uint16_t *packed = &tile[y * 2 + x / 4];
    uint8_t shift = (uint8_t)((x & 3) * 4);
    uint16_t mask = (uint16_t)(0x000fu << shift);

    *packed = (uint16_t)((*packed & ~mask) |
                         (((uint16_t)color & 0x000f) << shift));
}

static uint8_t pipe_32_pixel(uint8_t x, uint8_t y)
{
    uint8_t meta_col = x / LEVEL_METATILE_SIZE;
    uint8_t meta_row = y / LEVEL_METATILE_SIZE;
    uint8_t local_x = x & 7;
    uint8_t local_y = y & 7;
    uint8_t char_col = (x & 8) ? 1 : 0;
    uint8_t char_row = (y & 8) ? 1 : 0;
    uint16_t tile = (uint16_t)(PIPE_TILE_INDEX +
                    (meta_row * 2 + meta_col) * 4 +
                    char_row * 2 + char_col);

    return obj_tile_pixel(tile, local_x, local_y);
}

static void set_pipe_32_pixel(uint16_t base_tile, uint8_t x, uint8_t y,
                              uint8_t color)
{
    uint8_t meta_col = x / LEVEL_METATILE_SIZE;
    uint8_t meta_row = y / LEVEL_METATILE_SIZE;
    uint8_t local_x = x & 7;
    uint8_t local_y = y & 7;
    uint8_t char_col = (x & 8) ? 1 : 0;
    uint8_t char_row = (y & 8) ? 1 : 0;
    uint16_t tile = (uint16_t)(base_tile +
                    (meta_row * 2 + meta_col) * 4 +
                    char_row * 2 + char_col);

    set_obj_tile_pixel(tile, local_x, local_y, color);
}

static void build_side_pipe_tiles(void)
{
    memset(&SPRITE_GFX[SIDE_PIPE_TILE_INDEX * 16], 0, 16 * 16 * sizeof(uint16_t));

    for (uint8_t y = 0; y < 32; ++y) {
        for (uint8_t x = 0; x < 32; ++x) {
            uint8_t color = pipe_32_pixel((uint8_t)(31 - y), x);

            set_pipe_32_pixel(SIDE_PIPE_TILE_INDEX, x, y, color);
        }
    }
}

static void copy_metatile_to_obj_tiles(uint16_t obj_tile_index, uint8_t metatile)
{
    memcpy(&SPRITE_GFX[obj_tile_index * 16],
           &tiles_16Tiles[metatile * 4 * 8],
           4 * 16 * sizeof(uint16_t));
}

static void reset_platform_visual_cache(void)
{
    platform_visual_count = 0;
    memset(platform_visual_metatiles, 0xff, sizeof(platform_visual_metatiles));
}

static uint16_t platform_tile_index_for_metatile(uint8_t metatile)
{
    if (metatile == 255 || metatile == METATILE_EMPTY) {
        metatile = METATILE_SOLID;
    }

    for (uint8_t i = 0; i < platform_visual_count; ++i) {
        if (platform_visual_metatiles[i] == metatile) {
            return PLATFORM_TILE_INDEX + i * 4;
        }
    }

    if (platform_visual_count < PLATFORM_VISUAL_SLOTS) {
        uint8_t slot = platform_visual_count++;

        platform_visual_metatiles[slot] = metatile;
        copy_metatile_to_obj_tiles(PLATFORM_TILE_INDEX + slot * 4, metatile);
        return PLATFORM_TILE_INDEX + slot * 4;
    }

    return PLATFORM_TILE_INDEX;
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

static void set_trap_metatile_cell(Level *level, const Trap *trap,
                                   uint16_t source_x, uint16_t source_y,
                                   uint8_t metatile)
{
    uint8_t palette = (metatile == METATILE_EMPTY) ? 0 : trap->visual_palette;

    level_set_metatile_cell_palette(level, source_x, source_y, metatile, palette);
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
    trap->vx = 0;
    trap->vy = 0;
    trap->timer = 0;
    trap->subtype = subtype;
    trap->spawn_dir = 0;
    trap->visual_palette = 0;
    trap->visual_metatile = 255;
    trap->spent_metatile = 255;
    trap->spawn_interval = 0;
    trap->spawn_limit = QUESTION_DEFAULT_SPAWN_LIMIT;
    trap->item_variant = 0;
    trap->spawn_enemy_kind = ENEMY_NONE;
    trap->spawn_enemy_sprite = ENEMY_SPRITE_WALKER;
    trap->spawn_enemy_palette = 0;
    trap->spawn_enemy_param = 0;
    trap->spawn_enemy_count = 1;
    trap->spawn_sound = 1;
    trap->trigger_channel = 0;
    trap->listen_channel = 0;
    trap->channel_spawn_interval = 0;
    trap->trigger_delay = 0;
    trap->spawn_offset_x = 0;
    trap->spawn_offset_y = 0;
    trap->spawn_vx = 0;
    trap->spawn_vy = 0;
    trap->spawn_random_vx = 0;
    trap->spawn_random_vy = 0;
    trap->spawn_spacing_x = 0;
    trap->spawn_spacing_y = 0;
    trap->repeat_rearm_offset_x = 0;
    trap->repeat_rearm_offset_y = 0;
    trap->goal_walk_frames = 0;
    trap->hint_text = 0;
    trap->spawn_count = 0;
    trap->trigger_pending = 0;

    return trap;
}

static uint8_t question_spawn_interval(const Trap *trap, uint8_t fallback)
{
    return trap->spawn_interval != 0 ? trap->spawn_interval : fallback;
}

static uint8_t question_spawn_limit_reached(const Trap *trap)
{
    return trap->spawn_limit != 0 && trap->spawn_count >= trap->spawn_limit;
}

static uint8_t trap_spawn_limit_reached(const Trap *trap)
{
    return trap->spawn_limit != 0 && trap->spawn_count >= trap->spawn_limit;
}

static uint8_t trap_has_repeat_rearm_offset(const Trap *trap)
{
    return trap->repeat_rearm_offset_x != 0 || trap->repeat_rearm_offset_y != 0;
}

static void mark_question_spawned(Trap *trap)
{
    if (trap->spawn_count < 255) {
        trap->spawn_count++;
    }
}

static void mark_trap_spawned(Trap *trap)
{
    if (trap->spawn_count < 255) {
        trap->spawn_count++;
    }
}

static uint8_t falling_tile_crushes_player(const TrapEntity *entity,
                                           int previous_y,
                                           int entity_y,
                                           int player_y)
{
    int previous_bottom = previous_y + entity->h;
    int current_bottom = entity_y + entity->h;
    int crush_line = player_y + 4;

    return entity->vy > 0 &&
           previous_bottom <= crush_line &&
           current_bottom > crush_line;
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

            if (trigger->kind != TRAP_MOVING_PLATFORM &&
                trigger->visual_metatile != 255) {
                uint8_t palette = (trigger->visual_metatile == METATILE_EMPTY) ?
                    0 : trigger->visual_palette;

                level_set_metatile_cell_palette(level, source_x, source_y,
                                                trigger->visual_metatile, palette);
            }

            if (trigger->collision != 255) {
                set_cell_collision(manager, source_x, source_y,
                                   (LevelCollision)trigger->collision,
                                   trigger->hidden);
            }
        }
    }
}

static void apply_falling_stage_body_cells(TrapManager *manager, Level *level,
                                           const TrapTrigger *trigger)
{
    uint16_t cols = (uint16_t)(FIX16_TO_INT(trigger->w) / LEVEL_METATILE_SIZE);
    uint16_t rows = (uint16_t)(FIX16_TO_INT(trigger->h) / LEVEL_METATILE_SIZE);
    uint8_t visual = trigger->visual_metatile;

    if (cols == 0) {
        cols = 1;
    }
    if (rows == 0) {
        rows = 1;
    }
    if (visual == 255 || visual == METATILE_EMPTY) {
        visual = METATILE_BRICK;
    }

    for (uint16_t y = 0; y < rows; ++y) {
        for (uint16_t x = 0; x < cols; ++x) {
            uint16_t source_x = trigger->source_x + x;
            uint16_t source_y = trigger->source_y + y;

            if (source_x >= level->width || source_y >= level->height) {
                continue;
            }

            level_set_metatile_cell_palette(level, source_x, source_y,
                                            visual, trigger->visual_palette);
            set_cell_collision(manager, source_x, source_y,
                               LEVEL_COLLISION_SOLID, 0);
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
    trap->visual_palette = trigger->visual_palette;
    trap->visual_metatile = trigger->visual_metatile;
    trap->spent_metatile = trigger->spent_metatile;
    trap->spawn_interval = trigger->spawn_interval;
    trap->spawn_limit = trigger->spawn_limit;
    trap->item_variant = trigger->item_variant;
    trap->spawn_enemy_kind = trigger->spawn_enemy_kind;
    trap->spawn_enemy_sprite = trigger->spawn_enemy_sprite;
    trap->spawn_enemy_palette = trigger->spawn_enemy_palette;
    trap->spawn_enemy_param = trigger->spawn_enemy_param;
    trap->spawn_enemy_count = trigger->spawn_enemy_count;
    trap->spawn_sound = trigger->spawn_sound;
    trap->trigger_channel = trigger->trigger_channel;
    trap->listen_channel = trigger->listen_channel;
    trap->channel_spawn_interval = trigger->channel_spawn_interval;
    trap->trigger_delay = trigger->trigger_delay;
    trap->spawn_offset_x = trigger->spawn_offset_x;
    trap->spawn_offset_y = trigger->spawn_offset_y;
    trap->spawn_vx = trigger->spawn_vx;
    trap->spawn_vy = trigger->spawn_vy;
    trap->spawn_random_vx = trigger->spawn_random_vx;
    trap->spawn_random_vy = trigger->spawn_random_vy;
    trap->spawn_spacing_x = trigger->spawn_spacing_x;
    trap->spawn_spacing_y = trigger->spawn_spacing_y;
    trap->repeat_rearm_offset_x = trigger->repeat_rearm_offset_x;
    trap->repeat_rearm_offset_y = trigger->repeat_rearm_offset_y;
    trap->goal_walk_frames = trigger->goal_walk_frames;
    trap->hint_text = trigger->hint_text;
    trap->spawn_count = 0;
    trap->trigger_pending = 0;
    trap->vx = trigger->vx;
    trap->vy = trigger->vy;

    apply_generated_cells(manager, level, trigger);

    if (trigger->kind == TRAP_FALLING_FLOOR &&
        trigger->subtype == FALLING_WHEN_APPROACHED) {
        apply_falling_stage_body_cells(manager, level, trigger);
    }

    if (trigger->kind == TRAP_ENTER_PIPE || trigger->kind == TRAP_SIDE_PIPE ||
        trigger->kind == TRAP_EVASIVE_BLOCK ||
        trigger->kind == TRAP_MOVING_PLATFORM) {
        add_dynamic_collider(manager, trap);
    }

    if (trigger->kind == TRAP_STAGE_SPAWNER && trigger->subtype == STAGE_PIPE_HAZARD) {
        trap->state = TRAP_ACTIVE;
        trap->timer = 0;
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
            entity->palette = 0;
            entity->param = 0;
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
    memcpy(&SPRITE_PALETTE[UNDERGROUND_TILE_PALETTE_BANK * 16],
           underground_tile_obj_palette, sizeof(underground_tile_obj_palette));
    memcpy(&SPRITE_PALETTE[FAKE_GOAL_TOP_PALETTE_BANK * 16],
           tiles_16Pal, 16 * sizeof(uint16_t));
    SPRITE_PALETTE[FAKE_GOAL_TOP_PALETTE_BANK * 16 + 4] = RGB15(0, 31, 25);
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
    build_side_pipe_tiles();
    memcpy(&SPRITE_GFX[ITEM_TILE_INDEX * 16], items_16Tiles, items_16TilesLen);
    memcpy(&SPRITE_GFX[SPIKE_BLOCK_TOP_TILE_INDEX * 16],
           spike_block_16Tiles, spike_block_16TilesLen);
    reset_platform_visual_cache();
    platform_tile_index_for_metatile(METATILE_SOLID);

    for (uint8_t i = 0; i < 4; ++i) {
        memcpy(&SPRITE_GFX[(BRICK_FRAGMENT_TILE_INDEX + i) * 16],
               &tiles_16Tiles[(METATILE_BRICK * 4 + i) * 8],
               16 * sizeof(uint16_t));
    }

    for (uint8_t i = 0; i < 6; ++i) {
        memcpy(&SPRITE_GFX[(CHECKPOINT_TILE_INDEX + i * 4) * 16],
               &tiles_16Tiles[(METATILE_CHECKPOINT_00 * 4 + i * 4) * 8],
               4 * 16 * sizeof(uint16_t));
    }

    for (uint8_t i = 0; i < 4; ++i) {
        copy_metatile_to_obj_tiles(GOAL_TILE_INDEX + i * 4,
                                   METATILE_GOAL_00 + i);
    }

}

static uint16_t rendered_metatile_tile_index(uint8_t metatile, uint8_t sub_x, uint8_t sub_y)
{
    uint16_t base = metatile;

    if (metatile == METATILE_GROUND_TOP && sub_y == 1) {
        base = METATILE_GROUND_DIRT;
    }

    return (uint16_t)(base * 4 + sub_y * 2 + sub_x);
}

static void load_falling_tile_sprite(uint8_t slot, uint8_t metatile)
{
    if (slot >= FALLING_TILE_DYNAMIC_SLOTS) {
        return;
    }

    for (uint8_t sub_y = 0; sub_y < 2; ++sub_y) {
        for (uint8_t sub_x = 0; sub_x < 2; ++sub_x) {
            uint16_t dst_tile = FALLING_TILE_DYNAMIC_INDEX + slot * 4 + sub_y * 2 + sub_x;
            uint16_t src_tile = rendered_metatile_tile_index(metatile, sub_x, sub_y);

            memcpy(&SPRITE_GFX[dst_tile * 16],
                   &tiles_16Tiles[src_tile * 8],
                   16 * sizeof(uint16_t));
        }
    }
}

static uint8_t falling_tile_obj_palette(uint8_t bg_palette)
{
    if (bg_palette == 1) {
        return UNDERGROUND_TILE_PALETTE_BANK;
    }

    return EVASIVE_BLOCK_PALETTE_BANK;
}

static uint8_t block_overlay_obj_palette(uint8_t bg_palette)
{
    return falling_tile_obj_palette(bg_palette);
}

static uint8_t is_stage_pipe_hazard(const Trap *trap)
{
    return trap->kind == TRAP_STAGE_SPAWNER &&
           trap->subtype == STAGE_PIPE_HAZARD;
}

static uint8_t player_triggers_spike_block(const Player *player,
                                           int block_x, int block_y)
{
    const int tolerance = 1;
    int player_x = FIX16_TO_INT(player->x);
    int player_y = FIX16_TO_INT(player->y);
    int player_right = player_x + PLAYER_WIDTH_PX;
    int player_bottom = player_y + PLAYER_HEIGHT_PX;
    int block_right = block_x + LEVEL_METATILE_SIZE;
    int block_bottom = block_y + LEVEL_METATILE_SIZE;
    uint8_t horizontal_overlap = player_right > block_x + tolerance &&
                                 player_x < block_right - tolerance;
    uint8_t vertical_overlap = player_bottom > block_y + tolerance &&
                               player_y < block_bottom - tolerance;

    if (horizontal_overlap &&
        player_bottom >= block_y - tolerance &&
        player_bottom <= block_y + tolerance) {
        return 1;
    }

    if (horizontal_overlap &&
        player_y >= block_bottom - tolerance &&
        player_y <= block_bottom + tolerance) {
        return 1;
    }

    return vertical_overlap &&
           ((player->blocked_right &&
             player_right >= block_x - tolerance &&
             player_right <= block_x + tolerance) ||
            (player->blocked_left &&
             player_x >= block_right - tolerance &&
             player_x <= block_right + tolerance));
}

static void update_spike_block(Trap *trap, Player *player)
{
    if (!player->alive) {
        return;
    }

    int trap_x = FIX16_TO_INT(trap->x);
    int trap_y = FIX16_TO_INT(trap->y);
    int player_x = FIX16_TO_INT(player->x);
    int player_y = FIX16_TO_INT(player->y);

    if (trap->state == TRAP_IDLE) {
        if (!player_triggers_spike_block(player, trap_x, trap_y)) {
            return;
        }

        trap->state = TRAP_ACTIVE;
        audio_play_trap_trigger();
        messages_show(MESSAGE_ENEMY_BLOCK,
                      trap->x + FIX16_FROM_INT(16), trap->y, 45);
        player_kill(player);
        return;
    }

    if (aabb_overlap(player_x, player_y, PLAYER_WIDTH_PX, PLAYER_HEIGHT_PX,
                     trap_x, trap_y - 10, 16, 10) ||
        aabb_overlap(player_x, player_y, PLAYER_WIDTH_PX, PLAYER_HEIGHT_PX,
                     trap_x - 10, trap_y + 2, 10, 12) ||
        aabb_overlap(player_x, player_y, PLAYER_WIDTH_PX, PLAYER_HEIGHT_PX,
                     trap_x + 16, trap_y + 2, 10, 12)) {
        messages_show(MESSAGE_ENEMY_BLOCK,
                      trap->x + FIX16_FROM_INT(16), trap->y, 45);
        player_kill(player);
    }
}

static void reveal_hidden_block(TrapManager *manager, Trap *trap, Level *level)
{
    if (trap->state != TRAP_IDLE) {
        return;
    }

    trap->state = TRAP_ACTIVE;
    audio_play_block_hit();
    set_trap_metatile_cell(level, trap, trap->source_x, trap->source_y, METATILE_BRICK);
    set_cell_collision(manager, trap->source_x, trap->source_y,
                       LEVEL_COLLISION_SOLID, 0);
}

static void trigger_falling_floor(TrapManager *manager, Trap *trap, Level *level,
                                  Player *player)
{
    if (trap->state != TRAP_IDLE) {
        return;
    }

    trap->state = TRAP_ACTIVE;
    trap->trigger_pending = 0;
    trap->timer = 16;
    trap->vy = 0;
    audio_play_trap_trigger();
    emit_trap_channel(manager, level, player, trap->trigger_channel);

    uint16_t cols = (uint16_t)FIX16_TO_INT(trap->w) / LEVEL_METATILE_SIZE;
    uint16_t rows = (uint16_t)FIX16_TO_INT(trap->h) / LEVEL_METATILE_SIZE;
    uint8_t palettes[FALLING_TILE_DYNAMIC_SLOTS];

    if (cols == 0) {
        cols = 1;
    }
    if (rows == 0) {
        rows = 1;
    }

    for (uint16_t y = 0; y < rows; ++y) {
        for (uint16_t x = 0; x < cols; ++x) {
            uint8_t slot = (uint8_t)(y * cols + x);
            uint16_t source_x = trap->source_x + x;
            uint16_t source_y = trap->source_y + y;
            uint8_t visual = trap->visual_metatile;
            uint8_t palette = trap->visual_palette;

            if (slot >= FALLING_TILE_DYNAMIC_SLOTS) {
                slot = FALLING_TILE_DYNAMIC_SLOTS - 1;
            }

            if ((visual == 255 || visual == METATILE_EMPTY) &&
                source_x < level->width && source_y < level->height) {
                visual = level->metatiles[source_y][source_x];
                palette = level->palettes[source_y][source_x];
            }
            if (visual == 255 || visual == METATILE_EMPTY) {
                visual = METATILE_BRICK;
            }

            palettes[slot] = palette;
            load_falling_tile_sprite(slot, visual);
        }
    }

    for (uint16_t y = 0; y < rows; ++y) {
        for (uint16_t x = 0; x < cols; ++x) {
            set_trap_metatile_cell(level, trap, trap->source_x + x, trap->source_y + y,
                                   METATILE_EMPTY);
            set_cell_collision(manager, trap->source_x + x, trap->source_y + y,
                               LEVEL_COLLISION_EMPTY, 0);
        }
    }

    for (uint16_t y = 0; y < rows; ++y) {
        for (uint16_t x = 0; x < cols; ++x) {
            uint8_t slot = (uint8_t)(y * cols + x);
            TrapEntity *entity = spawn_entity(
                manager, TRAP_ENTITY_FALLING_TILE,
                trap->x + FIX16_FROM_INT(x * LEVEL_METATILE_SIZE),
                trap->y + FIX16_FROM_INT(y * LEVEL_METATILE_SIZE),
                0, 0, 16, 16);

            if (slot >= FALLING_TILE_DYNAMIC_SLOTS) {
                slot = FALLING_TILE_DYNAMIC_SLOTS - 1;
            }

            if (entity) {
                entity->frame = slot;
                entity->palette = falling_tile_obj_palette(palettes[slot]);
            }
        }
    }
}

static void trigger_bump_shooter(TrapManager *manager, Trap *trap, Level *level,
                                 Player *player)
{
    if (trap->state != TRAP_IDLE) {
        return;
    }

    trap->state = TRAP_SPENT;
    trap->trigger_pending = 0;
    audio_play_block_hit();
    audio_play_trap_trigger();
    spawn_entity(manager, TRAP_ENTITY_PROJECTILE,
                 trap->x + FIX16_FROM_INT(4),
                 trap->y - FIX16_FROM_INT(8),
                 0, -PROJECTILE_SPEED, 8, 8);
    emit_trap_channel(manager, level, player, trap->trigger_channel);
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
    uint8_t spent_metatile = (trap->spent_metatile == 255)
        ? METATILE_SOLID
        : trap->spent_metatile;

    trap->state = TRAP_SPENT;
    set_trap_metatile_cell(level, trap, trap->source_x, trap->source_y, spent_metatile);
    set_cell_collision(manager, trap->source_x, trap->source_y,
                       spent_metatile == METATILE_EMPTY
                           ? LEVEL_COLLISION_EMPTY
                           : LEVEL_COLLISION_SOLID,
                       spent_metatile == METATILE_EMPTY);
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
        entity->param = trap->item_variant;
    }
}

static void spawn_hurt_item_at(TrapManager *manager, fix16_t x, fix16_t y)
{
    TrapEntity *entity = spawn_entity(manager, TRAP_ENTITY_HURT_ITEM,
                                      x, y, 0, 0, 16, 16);

    if (entity) {
        entity->timer = 0;
    }
}

static void spawn_question_ball_burst(TrapManager *manager, const Trap *trap)
{
    static const int16_t ref_offsets[][2] = {
        { -8 * 3000 - 1000, -4 * 3000 },
        { -10 * 3000 + 1000, -1 * 3000 },
        { 4 * 3000 + 1000, -2 * 3000 },
        { 5 * 3000 - 1000, -3 * 3000 },
        { 6 * 3000 + 1000, -4 * 3000 },
        { 7 * 3000 - 1000, -2 * 3000 },
        { 8 * 3000 + 1000, -2 * 3000 - 1000 },
    };

    for (uint8_t i = 0; i < sizeof(ref_offsets) / sizeof(ref_offsets[0]); ++i) {
        spawn_hurt_item_at(manager,
                           trap->x + REF_POS_TO_FIX(ref_offsets[i][0]),
                           trap->y + REF_POS_TO_FIX(ref_offsets[i][1]));
    }
}

static void trigger_pickup_item(TrapManager *manager, Trap *trap, Level *level,
                                Player *player)
{
    if (trap->state != TRAP_IDLE) {
        return;
    }

    trap->state = TRAP_SPENT;
    trap->trigger_pending = 0;
    audio_play_trap_trigger();

    if (trap->item_variant == 1) {
        spawn_question_ball_burst(manager, trap);
    }

    emit_trap_channel(manager, level, player, trap->trigger_channel);
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
    trap->trigger_pending = 0;

    switch (trap->subtype) {
    case QUESTION_ENEMY:
        audio_play_trap_trigger();
        spend_question_block(manager, trap, level);
        enemies_spawn_from_block(&enemy_current, trap->x, trap->y,
                                 ENEMY_SPRITE_WALKER,
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
        trap->timer = 0;
        trap->spawn_count = 0;
        set_trap_metatile_cell(level, trap, trap->source_x, trap->source_y, METATILE_SOLID);
        set_cell_collision(manager, trap->source_x, trap->source_y,
                           LEVEL_COLLISION_SOLID, 0);
        audio_play_trap_trigger();
        break;
    case QUESTION_HIDDEN_POISON:
        audio_play_trap_trigger();
        spawn_block_item(manager, trap, TRAP_ENTITY_BAD_ITEM, ITEM_FAST_SPEED);
        spend_question_block(manager, trap, level);
        break;
    case QUESTION_COIN_GENERATOR:
        trap->state = TRAP_ACTIVE;
        trap->timer = 0;
        trap->spawn_count = 0;
        set_trap_metatile_cell(level, trap, trap->source_x, trap->source_y, METATILE_SOLID);
        set_cell_collision(manager, trap->source_x, trap->source_y,
                           LEVEL_COLLISION_SOLID, 0);
        break;
    default:
        spawn_coin_popup(manager, trap);
        spend_question_block(manager, trap, level);
        break;
    }

    emit_trap_channel(manager, level, (Player *)player, trap->trigger_channel);
}

static fix16_t random_fixed_positive(fix16_t range)
{
    if (range <= 0) {
        return 0;
    }

    uint32_t units = (uint32_t)(range >> 8);
    if (units == 0) {
        units = 1;
    }

    return (fix16_t)(trap_random((uint16_t)units) << 8);
}

static fix16_t random_fixed_centered(fix16_t range)
{
    return random_fixed_positive(range) - range / 2;
}

static void spawn_trap_enemy_recipe(Trap *trap, const Player *player)
{
    if (trap->spawn_enemy_kind == ENEMY_NONE || trap->spawn_enemy_count == 0) {
        return;
    }

    for (uint8_t i = 0; i < trap->spawn_enemy_count; ++i) {
        fix16_t vx = trap->spawn_vx;
        fix16_t vy = trap->spawn_vy;

        if (trap->spawn_random_vx > 0) {
            vx += random_fixed_centered(trap->spawn_random_vx);
        }
        if (trap->spawn_random_vy > 0) {
            fix16_t random_vy = random_fixed_positive(trap->spawn_random_vy);
            vy += (vy < 0) ? -random_vy : random_vy;
        }

        int8_t spawn_dir = trap->spawn_dir;
        fix16_t spawn_x = trap->x + trap->spawn_offset_x + trap->spawn_spacing_x * i;
        fix16_t spawn_y = trap->y + trap->spawn_offset_y + trap->spawn_spacing_y * i;

        if (spawn_dir == 0 && player) {
            int player_center = FIX16_TO_INT(player->x) + PLAYER_WIDTH_PX / 2;
            int spawn_center = FIX16_TO_INT(spawn_x) + 8;

            spawn_dir = (spawn_center <= player_center) ? 1 : -1;
        }
        if (spawn_dir == 0) {
            spawn_dir = -1;
        }

        enemies_spawn_direct_config(
            &enemy_current, (EnemyKind)trap->spawn_enemy_kind,
            spawn_x, spawn_y,
            vx, vy, trap->spawn_enemy_sprite, trap->spawn_enemy_palette,
            trap->spawn_enemy_param, spawn_dir);
    }
}

static void trigger_stage_spawner(TrapManager *manager, Trap *trap, Level *level,
                                  Player *player)
{
    if (trap->state != TRAP_IDLE) {
        return;
    }
    uint8_t has_rearm_offset = trap_has_repeat_rearm_offset(trap);

    if (!has_rearm_offset && trap_spawn_limit_reached(trap)) {
        trap->state = TRAP_SPENT;
        return;
    }

    trap->state = TRAP_SPENT;
    trap->trigger_pending = 0;

    if (trap->spawn_enemy_kind != ENEMY_NONE) {
        if (trap->spawn_sound) {
            audio_play_trap_trigger();
        }
        spawn_trap_enemy_recipe(trap, player);
        mark_trap_spawned(trap);
        if (has_rearm_offset) {
            trap->state = TRAP_ACTIVE;
        }
    } else if (trap->subtype == STAGE_HINT_MESSAGE) {
        messages_show_modal_text(trap->hint_text ? trap->hint_text : TXT_HINT_STAGE_1,
                                 trap->x, trap->y);
    }

    emit_trap_channel(manager, level, player, trap->trigger_channel);
}

static void update_stage_pipe_hazard(Trap *trap, const Player *player)
{
    uint8_t interval = trap->spawn_interval ? trap->spawn_interval : 48;
    int trap_x = FIX16_TO_INT(trap->x);
    int player_x = FIX16_TO_INT(player->x);

    if (trap_x < player_x - LEVEL_STREAM_MARGIN_PX ||
        trap_x > player_x + LEVEL_STREAM_RIGHT_PX) {
        return;
    }

    trap->timer++;
    if (trap->timer < interval) {
        return;
    }

    if (trap_spawn_limit_reached(trap)) {
        return;
    }

    trap->timer = 0;
    spawn_trap_enemy_recipe(trap, player);
    mark_trap_spawned(trap);
}

static void trigger_checkpoint(TrapManager *manager, Trap *trap, Level *level,
                               Player *player)
{
    if (trap->state != TRAP_IDLE) {
        return;
    }

    (void)level;
    trap->state = TRAP_SPENT;
    trap->trigger_pending = 0;
    player_set_checkpoint(player,
                          trap->x - FIX16_FROM_INT(PLAYER_WIDTH_PX),
                          trap->y + trap->h - FIX16_FROM_INT(PLAYER_HEIGHT_PX));
    audio_play_trap_trigger();
    emit_trap_channel(manager, level, player, trap->trigger_channel);
}

static void trigger_goal(TrapManager *manager, Trap *trap, Level *level,
                         Player *player)
{
    if (trap->state != TRAP_IDLE || !player->alive) {
        return;
    }

    trap->state = TRAP_ACTIVE;
    trap->trigger_pending = 0;
    player_begin_goal(player, trap->x, trap->goal_walk_frames);
    audio_stop_bgm();
    audio_play_goal();
    messages_show(MESSAGE_STAGE_CLEAR,
                  player->x - FIX16_FROM_INT(8),
                  player->y - FIX16_FROM_INT(12),
                  90);
    emit_trap_channel(manager, level, player, trap->trigger_channel);
}

static void trigger_fake_goal(TrapManager *manager, Trap *trap, Level *level,
                              Player *player)
{
    if (!player->alive) {
        return;
    }

    int player_x = FIX16_TO_INT(player->x);
    int player_y = FIX16_TO_INT(player->y);
    int trap_x = FIX16_TO_INT(trap->x);
    int trap_y = FIX16_TO_INT(trap->y);
    int trap_h = FIX16_TO_INT(trap->h);
    int bounce_left = trap_x + FIX16_TO_INT(REF_POS_TO_FIX(500));
    int bounce_right = trap_x + FIX16_TO_INT(REF_POS_TO_FIX(2000));
    int body_left = trap_x + FIX16_TO_INT(REF_POS_TO_FIX(500));
    int body_right = trap_x + FIX16_TO_INT(REF_POS_TO_FIX(2000));
    int bottom = player_y + PLAYER_HEIGHT_PX;

    if (player->vy >= 0 &&
        player_x + PLAYER_WIDTH_PX > bounce_left &&
        player_x < bounce_right &&
        bottom > trap_y + FIX16_TO_INT(REF_POS_TO_FIX(800)) &&
        bottom < trap_y + FIX16_TO_INT(REF_POS_TO_FIX(1600)) + FIX16_TO_INT(player->vy)) {
        trap->state = TRAP_ACTIVE;
        trap->spawn_dir = player_x < trap_x ? -1 : 1;
        trap->vx = trap->spawn_dir < 0 ? -FAKE_GOAL_MOVE_SPEED : FAKE_GOAL_MOVE_SPEED;
        player->y = trap->y - REF_POS_TO_FIX(4000);
        player->vy = FAKE_GOAL_BOUNCE;
        player->on_ground = 0;
        audio_play_trap_trigger();
        return;
    }

    if (player_x + PLAYER_WIDTH_PX > body_left &&
        player_x < body_right &&
        player_y < trap_y + trap_h - FIX16_TO_INT(REF_POS_TO_FIX(500)) &&
        bottom > trap_y + FIX16_TO_INT(REF_POS_TO_FIX(5750))) {
        messages_show_text(trap_random(2) ? TXT_FAKE_GOAL_ATTACK :
                           TXT_FAKE_GOAL_BETRAYED,
                           player->x - FIX16_FROM_INT(8),
                           player->y - FIX16_FROM_INT(12),
                           90);
        player_kill(player);
        emit_trap_channel(manager, level, player, trap->trigger_channel);
    }
}

static void update_fake_goal(Trap *trap, Player *player)
{
    int player_x = FIX16_TO_INT(player->x);
    int player_y = FIX16_TO_INT(player->y);
    int trap_x = FIX16_TO_INT(trap->x);

    if (trap->state == TRAP_IDLE) {
        int left_y = FIX16_TO_INT(REF_STAGE_Y_TO_FIX(30000));
        int right_y = FIX16_TO_INT(REF_STAGE_Y_TO_FIX(24000));

        if (player_y >= left_y &&
            player_x >= trap_x - FIX16_TO_INT(REF_POS_TO_FIX(3000 * 5)) &&
            player_x <= trap_x) {
            trap->state = TRAP_ACTIVE;
            trap->spawn_dir = -1;
            trap->vx = -FAKE_GOAL_MOVE_SPEED;
        } else if (player_y >= right_y &&
                   player_x <= trap_x + FIX16_TO_INT(REF_POS_TO_FIX(3000 * 8)) &&
                   player_x >= trap_x) {
            trap->state = TRAP_ACTIVE;
            trap->spawn_dir = 1;
            trap->vx = FAKE_GOAL_MOVE_SPEED;
        }
    }

    if (trap->state == TRAP_ACTIVE) {
        trap->x += trap->vx;
    }
}

static void trigger_hint_block(TrapManager *manager, Trap *trap, Level *level,
                               Player *player)
{
    audio_play_trap_trigger();
    trap->trigger_pending = 0;
    messages_show_modal_text(trap->hint_text ? trap->hint_text : TXT_HINT_STAGE_1,
                             trap->x - FIX16_FROM_INT(24),
                             trap->y - FIX16_FROM_INT(8));
    emit_trap_channel(manager, level, player, trap->trigger_channel);
}

static void activate_linked_trap(TrapManager *manager, Level *level,
                                 Player *player, Trap *trap)
{
    switch (trap->kind) {
    case TRAP_FALLING_FLOOR:
        trigger_falling_floor(manager, trap, level, player);
        break;
    case TRAP_BUMP_SHOOTER:
        trigger_bump_shooter(manager, trap, level, player);
        break;
    case TRAP_QUESTION_BLOCK:
        trigger_question_block(manager, trap, level, player);
        break;
    case TRAP_STAGE_SPAWNER:
        trigger_stage_spawner(manager, trap, level, player);
        break;
    case TRAP_CHECKPOINT:
        if (player) {
            trigger_checkpoint(manager, trap, level, player);
        }
        break;
    case TRAP_GOAL:
        if (player) {
            trigger_goal(manager, trap, level, player);
        }
        break;
    case TRAP_FAKE_GOAL:
        if (player) {
            trigger_fake_goal(manager, trap, level, player);
        }
        break;
    case TRAP_HINT_BLOCK:
        trigger_hint_block(manager, trap, level, player);
        break;
    case TRAP_PICKUP_ITEM:
        if (player) {
            trigger_pickup_item(manager, trap, level, player);
        }
        break;
    default:
        break;
    }
}

static void emit_trap_channel(TrapManager *manager, Level *level,
                              Player *player, uint8_t channel)
{
    if (channel == TRAP_CHANNEL_NONE) {
        return;
    }

    for (uint8_t i = 0; i < manager->trap_count; ++i) {
        Trap *listener = &manager->traps[i];

        if (listener->listen_channel != channel ||
            listener->trigger_pending) {
            continue;
        }

        if (listener->channel_spawn_interval != 0) {
            listener->spawn_interval = listener->channel_spawn_interval;
            listener->timer = 0;
        }

        if (listener->state != TRAP_IDLE) {
            continue;
        }

        if (listener->trigger_delay == 0) {
            activate_linked_trap(manager, level, player, listener);
        } else {
            listener->timer = listener->trigger_delay;
            listener->trigger_pending = 1;
        }
    }
}

static void update_linked_traps(TrapManager *manager, Level *level,
                                Player *player)
{
    for (uint8_t i = 0; i < manager->trap_count; ++i) {
        Trap *trap = &manager->traps[i];

        if (!trap->trigger_pending) {
            continue;
        }

        if (trap->timer > 0) {
            trap->timer--;
            continue;
        }

        trap->trigger_pending = 0;
        activate_linked_trap(manager, level, player, trap);
    }
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

uint8_t traps_player_can_enter_pipe(const TrapManager *manager,
                                    const struct Player *player)
{
    for (uint8_t i = 0; i < manager->trap_count; ++i) {
        const Trap *trap = &manager->traps[i];

        if (trap->kind == TRAP_ENTER_PIPE && trap->state == TRAP_IDLE &&
            player_on_enter_pipe(trap, player)) {
            return 1;
        }
    }

    return 0;
}

static uint8_t player_on_side_pipe(const Trap *trap, const struct Player *player)
{
    int player_x = FIX16_TO_INT(player->x);
    int player_y = FIX16_TO_INT(player->y);
    int pipe_x = FIX16_TO_INT(trap->x);
    int pipe_y = FIX16_TO_INT(trap->y);
    int pipe_w = FIX16_TO_INT(trap->w);

    return player->alive &&
           player->on_ground &&
           player_x + PLAYER_WIDTH_PX >= pipe_x - 1 &&
           player_x + PLAYER_WIDTH_PX <= pipe_x + 8 &&
           player_x < pipe_x + pipe_w &&
           player_y > pipe_y + 4 &&
           player_y + PLAYER_HEIGHT_PX < pipe_y + 38;
}

static void update_side_pipe(TrapManager *manager, Trap *trap, struct Player *player)
{
    if (trap->state == TRAP_IDLE) {
        if (player_on_side_pipe(trap, player) && (keys_held() & KEY_RIGHT_MASK)) {
            trap->state = TRAP_ACTIVE;
            trap->timer = 1;
            player->control_locked = 1;
            player->hidden = 1;
            player->vx = 0;
            player->vy = 0;
            audio_play_trap_trigger();
        }
        return;
    }

    if (trap->state != TRAP_ACTIVE) {
        return;
    }

    player->control_locked = 1;
    player->vx = 0;
    player->vy = 0;

    if (trap->subtype == 1) {
        player->hidden = 1;
        if (trap->timer <= PIPE_PLAYER_SINK_FRAMES) {
            player->x += SIDE_PIPE_PLAYER_SINK_SPEED;
        }
        if (trap->timer == PIPE_TRANSITION_FRAME) {
            manager->stage_transition_requested = 1;
            manager->stage_transition_target = PIPE_TRANSITION_1_2B;
            trap->state = TRAP_SPENT;
        } else if (trap->timer < 255) {
            trap->timer++;
        }
        return;
    }

    player->hidden = trap->timer <= PIPE_PLAYER_SINK_FRAMES;

    if (trap->timer <= PIPE_PLAYER_SINK_FRAMES) {
        player->x += SIDE_PIPE_PLAYER_SINK_SPEED;
    }
    if (trap->timer == PIPE_PLAYER_SINK_FRAMES) {
        player->y -= SIDE_PIPE_PLAYER_POP_UP;
    }
    if (trap->timer == 20) {
        audio_play_trap_trigger();
    }
    if (trap->timer >= 24) {
        player->hidden = 0;
        player->x -= SIDE_PIPE_PLAYER_BLAST_SPEED;
        player->facing_right = 0;
    }
    if (trap->timer >= SIDE_PIPE_KILL_FRAME) {
        player->hidden = 0;
        trap->state = TRAP_SPENT;
        player_kill(player);
        return;
    }

    trap->timer++;
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

    for (uint16_t i = 0; i < level1_2_trap_count; ++i) {
        add_generated_trap(manager, level, &level1_2_traps[i]);
    }
}

void traps_load_1_2_underground(TrapManager *manager, Level *level)
{
    memset(manager, 0, sizeof(*manager));

    for (uint16_t i = 0; i < level1_2u_trap_count; ++i) {
        add_generated_trap(manager, level, &level1_2u_traps[i]);
    }
}

void traps_load_1_2b(TrapManager *manager, Level *level)
{
    memset(manager, 0, sizeof(*manager));

    for (uint16_t i = 0; i < level1_2b_trap_count; ++i) {
        add_generated_trap(manager, level, &level1_2b_traps[i]);
    }
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

static uint8_t player_can_stand_on_platform(const Trap *trap, const Player *player,
                                            int old_platform_y)
{
    int player_x = FIX16_TO_INT(player->x);
    int player_y = FIX16_TO_INT(player->y);
    int player_bottom = player_y + PLAYER_HEIGHT_PX;
    int platform_x = FIX16_TO_INT(trap->x);
    int platform_w = FIX16_TO_INT(trap->w);

    if (player->vy < -REF_VEL_TO_FIX(100)) {
        return 0;
    }

    return player_x + PLAYER_WIDTH_PX > platform_x + 5 &&
           player_x < platform_x + platform_w - 5 &&
           player_bottom >= old_platform_y - 1 &&
           player_bottom <= old_platform_y + 10;
}

static void update_moving_platform(Trap *trap, const Level *level, Player *player)
{
    fix16_t old_x = trap->x;
    fix16_t old_y = trap->y;
    int old_platform_y = FIX16_TO_INT(old_y);
    int min_y = level_camera_min_y_px(level) - 32;
    int max_y = level_camera_max_y_px(level) + LEVEL_SCREEN_METATILE_ROWS * 16 + 32;

    trap->x += trap->vx;
    trap->y += trap->vy;

    if (trap->subtype == PLATFORM_VERTICAL_WRAP) {
        int platform_y = FIX16_TO_INT(trap->y);

        if (platform_y < min_y) {
            trap->y = FIX16_FROM_INT(max_y);
            old_platform_y = max_y;
        } else if (platform_y > max_y) {
            trap->y = FIX16_FROM_INT(min_y);
            old_platform_y = min_y;
        }
    }

    if (!player->alive || !player_can_stand_on_platform(trap, player, old_platform_y)) {
        return;
    }

    player->x += trap->x - old_x;
    player->y = trap->y - FIX16_FROM_INT(PLAYER_HEIGHT_PX);
    player->vy = 0;
    player->on_ground = 1;
}

void traps_update(TrapManager *manager, Level *level, struct Player *player)
{
    int player_x = FIX16_TO_INT(player->x);
    int player_y = FIX16_TO_INT(player->y);

    update_linked_traps(manager, level, player);

    for (uint8_t i = 0; i < manager->trap_count; ++i) {
        Trap *trap = &manager->traps[i];

        if (trap->kind == TRAP_FALLING_FLOOR && trap->state == TRAP_IDLE) {
            int trap_x = FIX16_TO_INT(trap->x);
            int trap_y = FIX16_TO_INT(trap->y);
            int trap_w = FIX16_TO_INT(trap->w);

            if (trap->subtype == FALLING_ON_PASS_UNDER) {
                int left_margin = FIX16_TO_INT(REF_POS_TO_FIX(3200));
                int right_margin = FIX16_TO_INT(REF_POS_TO_FIX(200));
                int trigger_y = trap_y + FIX16_TO_INT(REF_POS_TO_FIX(3000));

                if (player_x + PLAYER_WIDTH_PX > trap_x + left_margin &&
                    player_x < trap_x + trap_w - right_margin &&
                    player_y + PLAYER_HEIGHT_PX > trigger_y) {
                    trigger_falling_floor(manager, trap, level, player);
                }
            } else if (trap->subtype == FALLING_WHEN_APPROACHED) {
                int left_margin = FIX16_TO_INT(REF_POS_TO_FIX(2200));
                int right_margin = FIX16_TO_INT(REF_POS_TO_FIX(2700));
                int trigger_y = trap_y - FIX16_TO_INT(REF_POS_TO_FIX(3000));

                if (player_x + PLAYER_WIDTH_PX > trap_x + left_margin &&
                    player_x < trap_x + trap_w - right_margin &&
                    player_y + PLAYER_HEIGHT_PX > trigger_y) {
                    trigger_falling_floor(manager, trap, level, player);
                }
            } else if (player->on_ground &&
                       player_x + PLAYER_WIDTH_PX > trap_x + 4 &&
                       player_x < trap_x + trap_w - 4 &&
                       player_y + PLAYER_HEIGHT_PX >= trap_y &&
                       player_y + PLAYER_HEIGHT_PX <= trap_y + 4) {
                trigger_falling_floor(manager, trap, level, player);
            }
        } else if (trap->kind == TRAP_STAGE_SPAWNER && trap->state == TRAP_IDLE) {
            int trap_x = FIX16_TO_INT(trap->x);
            int trap_y = FIX16_TO_INT(trap->y);
            int trap_w = FIX16_TO_INT(trap->w);
            int trap_h = FIX16_TO_INT(trap->h);

            if (aabb_overlap(player_x, player_y, PLAYER_WIDTH_PX, PLAYER_HEIGHT_PX,
                             trap_x, trap_y, trap_w, trap_h)) {
                trigger_stage_spawner(manager, trap, level, player);
            }
        } else if (trap->kind == TRAP_STAGE_SPAWNER &&
                   trap->state == TRAP_ACTIVE &&
                   trap->subtype == STAGE_PIPE_HAZARD) {
            update_stage_pipe_hazard(trap, player);
        } else if (trap->kind == TRAP_STAGE_SPAWNER &&
                   trap->state == TRAP_ACTIVE) {
            int trap_x = FIX16_TO_INT(trap->x);
            int trap_y = FIX16_TO_INT(trap->y);
            int trap_w = FIX16_TO_INT(trap->w);
            int trap_h = FIX16_TO_INT(trap->h);
            int rearm_x = trap_x + FIX16_TO_INT(trap->repeat_rearm_offset_x);
            int rearm_y = trap_y + FIX16_TO_INT(trap->repeat_rearm_offset_y);
            uint8_t has_rearm_offset = trap_has_repeat_rearm_offset(trap);

            if ((has_rearm_offset &&
                 aabb_overlap(player_x, player_y, PLAYER_WIDTH_PX, PLAYER_HEIGHT_PX,
                              rearm_x, rearm_y, trap_w, trap_h)) ||
                (!has_rearm_offset &&
                 !aabb_overlap(player_x, player_y, PLAYER_WIDTH_PX, PLAYER_HEIGHT_PX,
                               trap_x, trap_y, trap_w, trap_h))) {
                trap->state = TRAP_IDLE;
            }
        } else if (trap->kind == TRAP_SPIKE_BLOCK) {
            update_spike_block(trap, player);
        } else if (trap->kind == TRAP_ENTER_PIPE) {
            update_enter_pipe(manager, trap, player);
        } else if (trap->kind == TRAP_SIDE_PIPE) {
            update_side_pipe(manager, trap, player);
        } else if (trap->kind == TRAP_MOVING_PLATFORM) {
            update_moving_platform(trap, level, player);
        } else if (trap->kind == TRAP_CHECKPOINT && trap->state == TRAP_IDLE) {
            int trap_x = FIX16_TO_INT(trap->x);
            int trap_y = FIX16_TO_INT(trap->y);
            int trap_w = FIX16_TO_INT(trap->w);
            int trap_h = FIX16_TO_INT(trap->h);

            if (aabb_overlap(player_x, player_y, PLAYER_WIDTH_PX, PLAYER_HEIGHT_PX,
                             trap_x, trap_y, trap_w, trap_h)) {
                trigger_checkpoint(manager, trap, level, player);
            }
        } else if (trap->kind == TRAP_GOAL && trap->state == TRAP_IDLE) {
            if (player_hits_goal_trigger(trap, player)) {
                trigger_goal(manager, trap, level, player);
            }
        } else if (trap->kind == TRAP_FAKE_GOAL) {
            update_fake_goal(trap, player);
            trigger_fake_goal(manager, trap, level, player);
        } else if (trap->kind == TRAP_GOAL && trap->state == TRAP_ACTIVE &&
                   !player->alive) {
            trap->state = TRAP_IDLE;
        } else if (trap->kind == TRAP_PICKUP_ITEM && trap->state == TRAP_IDLE) {
            int trap_x = FIX16_TO_INT(trap->x);
            int trap_y = FIX16_TO_INT(trap->y);
            int trap_w = FIX16_TO_INT(trap->w);
            int trap_h = FIX16_TO_INT(trap->h);

            if (aabb_overlap(player_x, player_y, PLAYER_WIDTH_PX, PLAYER_HEIGHT_PX,
                             trap_x, trap_y, trap_w, trap_h)) {
                trigger_pickup_item(manager, trap, level, player);
            }
        } else if (trap->kind == TRAP_QUESTION_BLOCK && trap->state == TRAP_ACTIVE) {
            if (trap->subtype == QUESTION_COIN_GENERATOR) {
                if (trap->timer == 0) {
                    spawn_coin_popup(manager, trap);
                    mark_question_spawned(trap);
                    if (question_spawn_limit_reached(trap)) {
                        spend_question_block(manager, trap, level);
                    } else {
                        trap->timer = question_spawn_interval(trap, QUESTION_COIN_INTERVAL);
                    }
                } else {
                    trap->timer--;
                }
            } else if (trap->subtype == QUESTION_GENERATOR_ACTIVE) {
                if (trap->timer == 0) {
                    spawn_block_item(manager, trap, TRAP_ENTITY_BAD_ITEM, ITEM_FAST_SPEED);
                    mark_question_spawned(trap);
                    if (question_spawn_limit_reached(trap)) {
                        spend_question_block(manager, trap, level);
                    } else {
                        trap->timer = question_spawn_interval(trap, QUESTION_GENERATOR_INTERVAL);
                    }
                } else {
                    trap->timer--;
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

        int previous_entity_y = FIX16_TO_INT(entity->y);

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
            entity->kind == TRAP_ENTITY_STAR_ITEM ||
            entity->kind == TRAP_ENTITY_HURT_ITEM) {
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

        if (entity->kind == TRAP_ENTITY_GOOD_ITEM && entity->param == 2 &&
            enemies_transform_near_good_mushroom(&enemy_current,
                                                 entity->x, entity->y)) {
            entity->active = 0;
            audio_play_trap_trigger();
            continue;
        }

        int offscreen_top = entity->kind == TRAP_ENTITY_FALLING_TILE ? -192 : -96;
        if (entity_y > level_death_y_px(level) + 64 || entity_y < offscreen_top) {
            entity->active = 0;
            continue;
        }

        if (player->alive &&
            aabb_overlap(player_x, player_y, PLAYER_WIDTH_PX, PLAYER_HEIGHT_PX,
                         entity_x, entity_y, entity->w, entity->h)) {
            if (entity->kind == TRAP_ENTITY_FALLING_TILE) {
                if (falling_tile_crushes_player(entity, previous_entity_y,
                                                entity_y, player_y)) {
                    player_kill(player);
                }
            } else if (entity->kind == TRAP_ENTITY_GOOD_ITEM ||
                entity->kind == TRAP_ENTITY_COIN_POPUP) {
                if (entity->kind == TRAP_ENTITY_GOOD_ITEM) {
                    if (entity->param == 2) {
                        player_power_up(player);
                    }
                    messages_show(entity->param == 1
                                      ? MESSAGE_PLAYER_NOT_POISON
                                      : MESSAGE_PLAYER_TASTY,
                                  player->x + FIX16_FROM_INT(PLAYER_WIDTH_PX),
                                  player->y, 45);
                }
                entity->active = 0;
                audio_play_trap_trigger();
            } else {
                MessageId message = MESSAGE_PLAYER_BAD_MUSHROOM;
                if (entity->kind == TRAP_ENTITY_STAR_ITEM) {
                    message = MESSAGE_PLAYER_STABBED;
                } else if (entity->kind == TRAP_ENTITY_HURT_ITEM) {
                    message = MESSAGE_PLAYER_TASTY;
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
            } else if (entity->kind == TRAP_ENTITY_FALLING_TILE) {
                tile = FALLING_TILE_DYNAMIC_INDEX + entity->frame * 4;
            } else if (entity->kind == TRAP_ENTITY_COIN_POPUP) {
                tile = COIN_TILE_INDEX;
            } else if (entity->kind == TRAP_ENTITY_GOOD_ITEM) {
                tile = GOOD_ITEM_TILE_INDEX;
            } else if (entity->kind == TRAP_ENTITY_BAD_ITEM) {
                tile = BAD_ITEM_TILE_INDEX;
            } else if (entity->kind == TRAP_ENTITY_STAR_ITEM) {
                tile = STAR_ITEM_TILE_INDEX;
            } else if (entity->kind == TRAP_ENTITY_HURT_ITEM) {
                tile = HURT_ITEM_TILE_INDEX;
            } else if (entity->kind == TRAP_ENTITY_BRICK_FRAGMENT) {
                tile = BRICK_FRAGMENT_TILE_INDEX + entity->frame;
            }
            uint16_t size = (entity->kind == TRAP_ENTITY_PROJECTILE ||
                             entity->kind == TRAP_ENTITY_COIN_POPUP ||
                             entity->kind == TRAP_ENTITY_BRICK_FRAGMENT)
                                ? ATTR1_SIZE_8
                                : ATTR1_SIZE_16;
            uint16_t palette = 0;

            if (entity->kind == TRAP_ENTITY_FALLING_TILE) {
                palette = entity->palette;
            } else if (entity->kind == TRAP_ENTITY_BRICK_FRAGMENT) {
                palette = EVASIVE_BLOCK_PALETTE_BANK;
            } else if (entity->kind == TRAP_ENTITY_COIN_POPUP ||
                       entity->kind == TRAP_ENTITY_GOOD_ITEM ||
                       entity->kind == TRAP_ENTITY_BAD_ITEM ||
                       entity->kind == TRAP_ENTITY_STAR_ITEM ||
                       entity->kind == TRAP_ENTITY_HURT_ITEM) {
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
        fix16_t cull_x = trap->x;
        fix16_t cull_y = trap->y;
        int cull_w = FIX16_TO_INT(trap->w);
        int cull_h = FIX16_TO_INT(trap->h);

        if (is_stage_pipe_hazard(trap)) {
            cull_x = CELL_WORLD_X(trap->source_x > 0 ? trap->source_x - 1 : 0);
            cull_y = CELL_WORLD_Y(trap->source_y);
            cull_w = 32;
            cull_h = 64;
        } else if (trap->kind == TRAP_CHECKPOINT ||
                   trap->kind == TRAP_SIDE_PIPE) {
            cull_w = 32;
            cull_h = 32;
        } else if (trap->kind == TRAP_SPIKE_BLOCK) {
            cull_x -= FIX16_FROM_INT(16);
            cull_y -= FIX16_FROM_INT(16);
            cull_w = 48;
            cull_h = 48;
        } else if (trap->kind == TRAP_GOAL || trap->kind == TRAP_FAKE_GOAL) {
            cull_w = 16;
        }

        if (cull_w < 1) {
            cull_w = LEVEL_METATILE_SIZE;
        }
        if (cull_h < 1) {
            cull_h = LEVEL_METATILE_SIZE;
        }

        if (!camera_sprite_visible(camera_world_to_screen_x(camera, cull_x),
                                   camera_world_to_screen_y(camera, cull_y),
                                   cull_w, cull_h)) {
            continue;
        }

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
        } else if (trap->kind == TRAP_SPIKE_BLOCK && trap->state != TRAP_IDLE) {
            static const int8_t offsets[3][2] = {
                {0, -12},
                {-12, 0},
                {12, 0},
            };
            static const uint16_t tiles[3] = {
                SPIKE_BLOCK_TOP_TILE_INDEX,
                SPIKE_BLOCK_LEFT_TILE_INDEX,
                SPIKE_BLOCK_RIGHT_TILE_INDEX,
            };
            uint16_t palette = block_overlay_obj_palette(trap->visual_palette);
            int base_x = camera_world_to_screen_x(camera, trap->x);
            int base_y = camera_world_to_screen_y(camera, trap->y);

            for (uint8_t part = 0; part < 3 && dynamic_index < TRAPS_MAX_DYNAMIC_SPRITES; ++part) {
                int screen_x = base_x + offsets[part][0];
                int screen_y = base_y + offsets[part][1];
                OBJATTR *obj = &dynamic_trap_oam[dynamic_index];

                if (!camera_sprite_visible(screen_x, screen_y,
                                           LEVEL_METATILE_SIZE, LEVEL_METATILE_SIZE)) {
                    obj->attr0 = ATTR0_DISABLED;
                } else {
                    obj->attr0 = (uint16_t)((screen_y & 0x00ff) |
                                            ATTR0_COLOR_16 | ATTR0_SQUARE);
                    obj->attr1 = (uint16_t)((screen_x & 0x01ff) | ATTR1_SIZE_16);
                    obj->attr2 = (uint16_t)(OBJ_CHAR(tiles[part]) |
                                            ATTR2_PALETTE(palette));
                }

                OAM[DYNAMIC_TRAP_OAM_BASE + dynamic_index] = *obj;
                dynamic_index++;
            }
        } else if (trap->kind == TRAP_PICKUP_ITEM && trap->state == TRAP_IDLE) {
            int screen_x = camera_world_to_screen_x(camera, trap->x);
            int screen_y = camera_world_to_screen_y(camera, trap->y);
            OBJATTR *obj = &dynamic_trap_oam[dynamic_index];

            if (!camera_sprite_visible(screen_x, screen_y,
                                       LEVEL_METATILE_SIZE, LEVEL_METATILE_SIZE)) {
                obj->attr0 = ATTR0_DISABLED;
            } else {
                obj->attr0 = (uint16_t)((screen_y & 0x00ff) | ATTR0_COLOR_16 | ATTR0_SQUARE);
                obj->attr1 = (uint16_t)((screen_x & 0x01ff) | ATTR1_SIZE_16);
                obj->attr2 = (uint16_t)(OBJ_CHAR(QUESTION_BALL_TILE_INDEX) |
                                        ATTR2_PALETTE(ITEM_PALETTE_BANK));
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
        } else if (trap->kind == TRAP_SIDE_PIPE) {
            int base_x = camera_world_to_screen_x(camera, trap->x);
            int base_y = camera_world_to_screen_y(camera, trap->y);

            for (int row = 0; row < 2 && dynamic_index + 1 < TRAPS_MAX_DYNAMIC_SPRITES; ++row) {
                for (int col = 0; col < 2 && dynamic_index < TRAPS_MAX_DYNAMIC_SPRITES; ++col) {
                    int screen_x = base_x + col * LEVEL_METATILE_SIZE;
                    int screen_y = base_y + row * LEVEL_METATILE_SIZE;
                    uint16_t tile = SIDE_PIPE_TILE_INDEX + (row * 2 + col) * 4;
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
        } else if (trap->kind == TRAP_MOVING_PLATFORM) {
            int base_x = camera_world_to_screen_x(camera, trap->x);
            int base_y = camera_world_to_screen_y(camera, trap->y);
            int cols = (FIX16_TO_INT(trap->w) + LEVEL_METATILE_SIZE - 1) /
                       LEVEL_METATILE_SIZE;
            uint16_t palette = block_overlay_obj_palette(trap->visual_palette);
            uint16_t tile_index = platform_tile_index_for_metatile(trap->visual_metatile);

            if (cols < 1) {
                cols = 1;
            }

            for (int col = 0; col < cols && dynamic_index < TRAPS_MAX_DYNAMIC_SPRITES; ++col) {
                int screen_x = base_x + col * LEVEL_METATILE_SIZE;
                OBJATTR *obj = &dynamic_trap_oam[dynamic_index];

                if (!camera_sprite_visible(screen_x, base_y,
                                           LEVEL_METATILE_SIZE, LEVEL_METATILE_SIZE)) {
                    continue;
                } else {
                    obj->attr0 = (uint16_t)((base_y & 0x00ff) |
                                            ATTR0_COLOR_16 | ATTR0_SQUARE);
                    obj->attr1 = (uint16_t)((screen_x & 0x01ff) | ATTR1_SIZE_16);
                    obj->attr2 = (uint16_t)(OBJ_CHAR(tile_index) |
                                            ATTR2_PALETTE(palette));
                }

                OAM[DYNAMIC_TRAP_OAM_BASE + dynamic_index] = *obj;
                dynamic_index++;
            }
        } else if (trap->kind == TRAP_ENTER_PIPE || is_stage_pipe_hazard(trap)) {
            fix16_t draw_x = trap->x;
            fix16_t draw_y = trap->y;
            int rows = FIX16_TO_INT(trap->h) / LEVEL_METATILE_SIZE;

            if (is_stage_pipe_hazard(trap)) {
                draw_x = CELL_WORLD_X(trap->source_x > 0 ? trap->source_x - 1 : 0);
                draw_y = CELL_WORLD_Y(trap->source_y);
                rows = 4;
            } else if (rows < 1) {
                rows = 1;
            }

            int base_x = camera_world_to_screen_x(camera, draw_x);
            int base_y = camera_world_to_screen_y(camera, draw_y);

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
        } else if (trap->kind == TRAP_GOAL || trap->kind == TRAP_FAKE_GOAL) {
            int base_x = camera_world_to_screen_x(camera, trap->x);
            int base_y = camera_world_to_screen_y(camera, trap->y);
            int rows = FIX16_TO_INT(trap->h) / LEVEL_METATILE_SIZE;
            uint16_t palette = block_overlay_obj_palette(trap->visual_palette);

            if (rows < 1) {
                rows = 1;
            }

            for (int row = 0; row < rows && dynamic_index < TRAPS_MAX_DYNAMIC_SPRITES; ++row) {
                int screen_y = base_y + row * LEVEL_METATILE_SIZE;
                uint8_t art_row = row < 4 ? (uint8_t)row : 3;
                OBJATTR *obj = &dynamic_trap_oam[dynamic_index];

                if (!camera_sprite_visible(base_x, screen_y,
                                           LEVEL_METATILE_SIZE, LEVEL_METATILE_SIZE)) {
                    continue;
                }

                uint16_t row_palette = palette;

                if (trap->kind == TRAP_FAKE_GOAL && row == 0) {
                    row_palette = FAKE_GOAL_TOP_PALETTE_BANK;
                }

                obj->attr0 = (uint16_t)((screen_y & 0x00ff) |
                                        ATTR0_COLOR_16 | ATTR0_SQUARE);
                obj->attr1 = (uint16_t)((base_x & 0x01ff) | ATTR1_SIZE_16);
                obj->attr2 = (uint16_t)(OBJ_CHAR(GOAL_TILE_INDEX + art_row * 4) |
                                        ATTR2_PALETTE(row_palette));
                OAM[DYNAMIC_TRAP_OAM_BASE + dynamic_index] = *obj;
                dynamic_index++;
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

        if (trap->kind == TRAP_MOVING_PLATFORM) {
            return LEVEL_COLLISION_PASS_THROUGH;
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
            trigger_bump_shooter(manager, trap, level, (Player *)player);
            triggered = 1;
        } else if (trap->kind == TRAP_QUESTION_BLOCK) {
            trigger_question_block(manager, trap, level, player);
            triggered = 1;
        } else if (trap->kind == TRAP_EVASIVE_BLOCK) {
            bump_evasive_block(trap, world_x_px, world_y_px);
        } else if (trap->kind == TRAP_HINT_BLOCK) {
            trigger_hint_block(manager, trap, level, (Player *)player);
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
