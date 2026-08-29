#include "player.h"

#include <gba.h>
#include <string.h>

#include "audio.h"
#include "camera.h"
#include "player_16.h"
#include "traps.h"

#define PLAYER_SPRITE_INDEX 0
#define PLAYER_TILE_INDEX 32
#define PLAYER_PALETTE_BANK 2
#define PLAYER_TILES_PER_FRAME 4
#define PLAYER_SPRITE_WIDTH_PX 16
#define PLAYER_SPRITE_HEIGHT_PX 16

#define KEY_A_MASK 0x0001
#define KEY_LEFT_MASK 0x0020
#define KEY_RIGHT_MASK 0x0010

#define OBJ_ATTR0_Y_MASK 0x00ff
#define OBJ_ATTR1_X_MASK 0x01ff
#define REF_POS_TO_FIX(v) ((fix16_t)(((int64_t)(v) * 16 * FIX16_ONE) / (29 * 100)))
#define REF_Y_POS_TO_FIX(v) ((fix16_t)(((int64_t)(v) * 24 * FIX16_ONE) / (29 * 100)))
#define REF_VEL_TO_FIX(v) (REF_POS_TO_FIX(v) / 2)
#define REF_Y_VEL_TO_FIX(v) (REF_Y_POS_TO_FIX(v) / 2)
#define REF_ACCEL_TO_FIX(v) (REF_POS_TO_FIX(v) / 4)
#define REF_Y_ACCEL_TO_FIX(v) (REF_Y_POS_TO_FIX(v) / 4)

#define PLAYER_ACCEL REF_ACCEL_TO_FIX(40)
#define PLAYER_AIR_LIMIT REF_VEL_TO_FIX(500)
#define PLAYER_RUN_LIMIT REF_VEL_TO_FIX(800)
#define PLAYER_FRICTION REF_ACCEL_TO_FIX(60)
#define PLAYER_GRAVITY REF_Y_ACCEL_TO_FIX(100)
#define PLAYER_JUMP_SPEED (-REF_Y_VEL_TO_FIX(1200))
#define PLAYER_FAST_JUMP_SPEED (-REF_Y_VEL_TO_FIX(1400))
#define PLAYER_EXTRA_FAST_JUMP_SPEED (-REF_Y_VEL_TO_FIX(1500))
#define PLAYER_MAX_FALL_SPEED REF_Y_VEL_TO_FIX(1600)
#define PLAYER_SPEED_200 REF_VEL_TO_FIX(200)
#define PLAYER_SPEED_600 REF_VEL_TO_FIX(600)
#define PLAYER_JUMP_HOLD_CHECK_SPEED (-REF_Y_VEL_TO_FIX(900))
#define PLAYER_JUMP_LIFT REF_Y_POS_TO_FIX(400)
#define PLAYER_JUMP_BUFFER_FRAMES 20
#define PLAYER_COYOTE_FRAMES 8
#define PLAYER_DEATH_FRAMES 80
#define PLAYER_DEATH_BOUNCE (-REF_Y_VEL_TO_FIX(1050))
#define PLAYER_WALK_FRAME_DISTANCE FIX16_FROM_INT(6)
#define PLAYER_GOAL_DROP_SPEED REF_Y_VEL_TO_FIX(600)
#define PLAYER_GOAL_WALK_SPEED REF_VEL_TO_FIX(300)
#define PLAYER_GOAL_SNAP_OFFSET REF_POS_TO_FIX(2000)
#define PLAYER_GOAL_HIDE_FRAME 110
#define PLAYER_GOAL_DONE_FRAME 250
static OBJATTR shadow_oam[128];
static uint16_t previous_keys;

static uint16_t keys_held(void)
{
    return (uint16_t)(~REG_KEYINPUT & 0x03ff);
}

static LevelCollision collision_at(const Level *level, const TrapManager *traps,
                                   int world_x_px, int world_y_px)
{
    if (traps_hides_collision_at(traps, world_x_px, world_y_px)) {
        return LEVEL_COLLISION_EMPTY;
    }

    LevelCollision trap_collision = traps_collision_at(traps, world_x_px, world_y_px);

    if (trap_collision != LEVEL_COLLISION_EMPTY) {
        return trap_collision;
    }

    return level_collision_at(level, world_x_px, world_y_px);
}

static uint8_t aabb_hits_solid(const Level *level, const TrapManager *traps, int x, int y)
{
    int left = x + 1;
    int right = x + PLAYER_WIDTH_PX - 2;
    int top = y + 1;
    int bottom = y + PLAYER_HEIGHT_PX - 1;

    return collision_at(level, traps, left, top) == LEVEL_COLLISION_SOLID ||
           collision_at(level, traps, right, top) == LEVEL_COLLISION_SOLID ||
           collision_at(level, traps, left, bottom) == LEVEL_COLLISION_SOLID ||
           collision_at(level, traps, right, bottom) == LEVEL_COLLISION_SOLID;
}

static uint8_t aabb_hits_death(const Level *level, const TrapManager *traps, int x, int y)
{
    int left = x + 2;
    int right = x + PLAYER_WIDTH_PX - 3;
    int top = y + 2;
    int bottom = y + PLAYER_HEIGHT_PX - 1;

    return collision_at(level, traps, left, top) == LEVEL_COLLISION_DEATH ||
           collision_at(level, traps, right, top) == LEVEL_COLLISION_DEATH ||
           collision_at(level, traps, left, bottom) == LEVEL_COLLISION_DEATH ||
           collision_at(level, traps, right, bottom) == LEVEL_COLLISION_DEATH;
}

static uint8_t aabb_lands_on_platform(const Level *level, const TrapManager *traps,
                                      int x, int old_y, int new_y)
{
    int old_bottom = old_y + PLAYER_HEIGHT_PX;
    int new_bottom = new_y + PLAYER_HEIGHT_PX;
    int left = x + 2;
    int right = x + PLAYER_WIDTH_PX - 3;

    if (new_bottom <= old_bottom) {
        return 0;
    }

    for (int y = old_bottom; y <= new_bottom; ++y) {
        if ((y & (LEVEL_METATILE_SIZE - 1)) == 0 &&
            (collision_at(level, traps, left, y) == LEVEL_COLLISION_PASS_THROUGH ||
             collision_at(level, traps, right, y) == LEVEL_COLLISION_PASS_THROUGH)) {
            return 1;
        }
    }

    return 0;
}

static uint8_t player_is_supported(const Level *level, const TrapManager *traps,
                                   int x, int y)
{
    int foot_y = y + PLAYER_HEIGHT_PX + 1;
    int left = x + 2;
    int right = x + PLAYER_WIDTH_PX - 3;
    LevelCollision left_collision = collision_at(level, traps, left, foot_y);
    LevelCollision right_collision = collision_at(level, traps, right, foot_y);

    return left_collision == LEVEL_COLLISION_SOLID ||
           right_collision == LEVEL_COLLISION_SOLID ||
           left_collision == LEVEL_COLLISION_PASS_THROUGH ||
           right_collision == LEVEL_COLLISION_PASS_THROUGH;
}

static uint8_t goal_lands_on_floor(const Level *level, const TrapManager *traps,
                                   const Player *player, int old_y, int new_y,
                                   int *floor_y)
{
    int old_bottom = old_y + PLAYER_HEIGHT_PX;
    int new_bottom = new_y + PLAYER_HEIGHT_PX + 1;
    int left = FIX16_TO_INT(player->x) + 2;
    int right = FIX16_TO_INT(player->x) + PLAYER_WIDTH_PX - 3;

    if (new_bottom <= old_bottom) {
        return 0;
    }

    for (int y = old_bottom; y <= new_bottom; ++y) {
        LevelCollision left_collision = collision_at(level, traps, left, y);
        LevelCollision right_collision = collision_at(level, traps, right, y);

        if (left_collision == LEVEL_COLLISION_SOLID ||
            right_collision == LEVEL_COLLISION_SOLID ||
            left_collision == LEVEL_COLLISION_PASS_THROUGH ||
            right_collision == LEVEL_COLLISION_PASS_THROUGH) {
            *floor_y = y - PLAYER_HEIGHT_PX - 1;
            return 1;
        }
    }

    return 0;
}

static uint8_t player_blocked_left(const Level *level, const TrapManager *traps,
                                   const Player *player)
{
    return aabb_hits_solid(level, traps,
                           FIX16_TO_INT(player->x) - 2,
                           FIX16_TO_INT(player->y));
}

static uint8_t player_blocked_right(const Level *level, const TrapManager *traps,
                                    const Player *player)
{
    return aabb_hits_solid(level, traps,
                           FIX16_TO_INT(player->x) + 2,
                           FIX16_TO_INT(player->y));
}

static uint8_t bump_level_cell(Level *level, TrapManager *traps,
                               int world_x_px, int world_y_px)
{
    int source_x = world_x_px / LEVEL_METATILE_SIZE;
    int source_y = world_y_px / LEVEL_METATILE_SIZE + LEVEL_VIEW_SOURCE_ROW_OFFSET;

    if (source_x < 0 || source_x >= level->width ||
        source_y < 0 || source_y >= level->height) {
        return 0;
    }

    uint8_t metatile = level->metatiles[source_y][source_x];

    if (metatile == METATILE_BRICK) {
        traps_break_brick(traps, level, (uint16_t)source_x, (uint16_t)source_y);
        return 1;
    }

    if (metatile == METATILE_QUESTION) {
        level_set_metatile_cell_palette(level, (uint16_t)source_x, (uint16_t)source_y,
                                        METATILE_SOLID,
                                        level->palettes[source_y][source_x]);
        audio_play_block_hit();
        return 1;
    }

    return 0;
}

static uint8_t bump_head_points(Player *player, Level *level, TrapManager *traps,
                                int x, int hit_y)
{
    static const int8_t head_offsets[] = { 4, PLAYER_WIDTH_PX / 2, PLAYER_WIDTH_PX - 5 };
    uint8_t bumped = 0;

    for (uint8_t i = 0; i < sizeof(head_offsets); ++i) {
        int hit_x = x + head_offsets[i];

        if (traps_on_player_bump(traps, level, player, hit_x, hit_y)) {
            bumped = 1;
        } else if (bump_level_cell(level, traps, hit_x, hit_y)) {
            bumped = 1;
        }
    }

    if (bumped && player->vy < 0) {
        player->vy = (fix16_t)(-player->vy * 2 / 3);
    }

    return bumped;
}

static void hide_all_sprites(void)
{
    for (int i = 0; i < 128; ++i) {
        shadow_oam[i].attr0 = ATTR0_DISABLED;
        shadow_oam[i].attr1 = 0;
        shadow_oam[i].attr2 = 0;
    }

    memcpy(OAM, shadow_oam, sizeof(shadow_oam));
}

static void load_player_sprite(void)
{
    memcpy(&SPRITE_PALETTE[PLAYER_PALETTE_BANK * 16],
           player_16Pal, 16 * sizeof(uint16_t));
    memcpy(&SPRITE_GFX[PLAYER_TILE_INDEX * 16], player_16Tiles, player_16TilesLen);
}

static uint16_t player_frame_tile(const Player *player)
{
    uint16_t frame = 0;

    if (!player->alive) {
        frame = 3;
    } else if (!player->on_ground) {
        frame = 2;
    } else if (player->vx != 0) {
        frame = player->walk_frame;
    }

    return PLAYER_TILE_INDEX + frame * PLAYER_TILES_PER_FRAME;
}

static void move_x(Player *player, const Level *level, TrapManager *traps)
{
    fix16_t old_x = player->x;

    player->x += player->vx;

    int px = FIX16_TO_INT(player->x);
    int py = FIX16_TO_INT(player->y);

    if (!aabb_hits_solid(level, traps, px, py)) {
        fix16_t dx = player->x - old_x;
        if (dx < 0) {
            dx = -dx;
        }

        if (player->on_ground && dx > 0) {
            player->walk_distance += dx;
            while (player->walk_distance >= PLAYER_WALK_FRAME_DISTANCE) {
                player->walk_distance -= PLAYER_WALK_FRAME_DISTANCE;
                player->walk_frame ^= 1;
            }
        } else if (dx == 0) {
            player->walk_distance = 0;
            player->walk_frame = 0;
        }
        return;
    }

    if (player->vx > 0) {
        int tile_right = (px + PLAYER_WIDTH_PX - 1) / LEVEL_METATILE_SIZE;
        player->x = FIX16_FROM_INT(tile_right * LEVEL_METATILE_SIZE - PLAYER_WIDTH_PX);
    } else if (player->vx < 0) {
        int tile_left = px / LEVEL_METATILE_SIZE;
        player->x = FIX16_FROM_INT((tile_left + 1) * LEVEL_METATILE_SIZE);
    }

    player->vx = 0;
    player->walk_distance = 0;
    player->walk_frame = 0;
}

static void move_y(Player *player, Level *level, TrapManager *traps)
{
    int old_y = FIX16_TO_INT(player->y);

    player->y += player->vy;

    int px = FIX16_TO_INT(player->x);
    int py = FIX16_TO_INT(player->y);

    player->on_ground = 0;

    if (player->vy < 0) {
        int hit_y = (py / LEVEL_METATILE_SIZE) * LEVEL_METATILE_SIZE;

        if (bump_head_points(player, level, traps, px, hit_y)) {
            player->y = FIX16_FROM_INT(hit_y + LEVEL_METATILE_SIZE);
            return;
        }
    }

    if (player->vy > 0 && aabb_lands_on_platform(level, traps, px, old_y, py)) {
        int bottom_tile = (py + PLAYER_HEIGHT_PX) / LEVEL_METATILE_SIZE;
        player->y = FIX16_FROM_INT(bottom_tile * LEVEL_METATILE_SIZE - PLAYER_HEIGHT_PX);
        player->vy = 0;
        player->on_ground = 1;
        return;
    }

    if (!aabb_hits_solid(level, traps, px, py)) {
        if (player->vy >= 0 && player_is_supported(level, traps, px, py)) {
            player->vy = 0;
            player->on_ground = 1;
        }
        return;
    }

    if (player->vy > 0) {
        int tile_bottom = (py + PLAYER_HEIGHT_PX - 1) / LEVEL_METATILE_SIZE;
        player->y = FIX16_FROM_INT(tile_bottom * LEVEL_METATILE_SIZE - PLAYER_HEIGHT_PX);
        player->on_ground = 1;
    } else if (player->vy < 0) {
        int tile_top = py / LEVEL_METATILE_SIZE;
        int hit_y = tile_top * LEVEL_METATILE_SIZE;

        if (!bump_head_points(player, level, traps, px, hit_y)) {
            audio_play_block_hit();
            player->vy = (fix16_t)(-player->vy * 2 / 3);
        }
        player->y = FIX16_FROM_INT((tile_top + 1) * LEVEL_METATILE_SIZE);
    }
}

void player_init_video(void)
{
    hide_all_sprites();
    load_player_sprite();
}

void player_spawn(Player *player)
{
    uint16_t death_count = player->death_count;
    uint8_t checkpoint_active = player->checkpoint_active;
    fix16_t checkpoint_x = player->checkpoint_x;
    fix16_t checkpoint_y = player->checkpoint_y;

    if (checkpoint_active) {
        player->x = checkpoint_x;
        player->y = checkpoint_y;
    } else {
        player->x = FIX16_FROM_INT(31);
        player->y = FIX16_FROM_INT(103);
    }
    player->vx = 0;
    player->vy = 0;
    player->on_ground = 0;
    player->facing_right = 1;
    player->alive = 1;
    player->death_timer = 0;
    player->jump_buffer = 0;
    player->coyote_timer = 0;
    player->jump_timer = 0;
    player->control_locked = 0;
    player->hidden = 0;
    player->goal_clear_done = 0;
    player->goal_timer = 0;
    player->walk_frame = 0;
    player->walk_distance = 0;
    player->checkpoint_active = checkpoint_active;
    player->checkpoint_x = checkpoint_x;
    player->checkpoint_y = checkpoint_y;
    player->death_count = death_count;
    previous_keys = 0;
}

void player_spawn_at(Player *player, fix16_t x, fix16_t y)
{
    uint16_t death_count = player->death_count;

    memset(player, 0, sizeof(*player));
    player->x = x;
    player->y = y;
    player->facing_right = 1;
    player->alive = 1;
    player->death_count = death_count;
    previous_keys = 0;
}

void player_set_checkpoint(Player *player, fix16_t x, fix16_t y)
{
    player->checkpoint_active = 1;
    player->checkpoint_x = x;
    player->checkpoint_y = y;
}

void player_begin_goal(Player *player, fix16_t goal_x)
{
    player->control_locked = 1;
    player->hidden = 0;
    player->alive = 1;
    player->goal_clear_done = 0;
    player->goal_timer = 1;
    player->x = goal_x - PLAYER_GOAL_SNAP_OFFSET;
    player->vx = 0;
    player->vy = 0;
    player->facing_right = 1;
    player->on_ground = 0;
    player->jump_buffer = 0;
    player->coyote_timer = 0;
    player->jump_timer = 0;
}

uint8_t player_goal_clear_done(const Player *player)
{
    return player->goal_clear_done;
}

void player_kill(Player *player)
{
    if (!player->alive || player->death_timer > 0) {
        return;
    }

    player->alive = 0;
    player->death_timer = PLAYER_DEATH_FRAMES;
    player->death_count++;
    audio_stop_bgm();
    audio_play_death();
    player->vx = 0;
    player->vy = PLAYER_DEATH_BOUNCE;
    player->goal_timer = 0;
    player->goal_clear_done = 0;
    player->jump_buffer = 0;
    player->coyote_timer = 0;
    player->jump_timer = 0;
    player->on_ground = 0;
    player->control_locked = 0;
    player->hidden = 0;
    player->walk_frame = 0;
    player->walk_distance = 0;
}

uint16_t player_death_count(const Player *player)
{
    return player->death_count;
}

void player_update(Player *player, Level *level, struct TrapManager *traps)
{
    uint16_t keys = keys_held();
    uint16_t pressed = keys & (uint16_t)~previous_keys;

    if (player->goal_timer > 0) {
        if (player->goal_timer <= 1) {
            player->vx = 0;
            player->vy = 0;
        } else if (player->goal_timer <= 42) {
            int old_y = FIX16_TO_INT(player->y);
            int floor_y = 0;

            player->vy = PLAYER_GOAL_DROP_SPEED;
            player->y += player->vy;

            if (goal_lands_on_floor(level, traps, player, old_y,
                                    FIX16_TO_INT(player->y), &floor_y)) {
                player->y = FIX16_FROM_INT(floor_y);
                player->vy = 0;
                player->on_ground = 1;
                player->goal_timer = 43;
            }
        } else if (player->goal_timer <= 108) {
            player->vx = PLAYER_GOAL_WALK_SPEED;
            player->x += player->vx;
            player->walk_distance += player->vx;
            if (player->walk_distance < 0) {
                player->walk_distance = -player->walk_distance;
            }
            if (player->walk_distance >= PLAYER_WALK_FRAME_DISTANCE) {
                player->walk_distance = 0;
                player->walk_frame ^= 1;
            }
        } else if (player->goal_timer == PLAYER_GOAL_HIDE_FRAME) {
            player->hidden = 1;
            player->vx = 0;
            player->vy = 0;
        } else {
            player->vx = 0;
            player->vy = 0;
        }

        if (player->goal_timer >= PLAYER_GOAL_DONE_FRAME) {
            player->goal_clear_done = 1;
        } else {
            player->goal_timer++;
        }

        previous_keys = keys;
        return;
    }

    if (player->control_locked) {
        previous_keys = keys;
        return;
    }

    if (!player->alive) {
        player->y += player->vy;
        if (player->vy < PLAYER_MAX_FALL_SPEED) {
            player->vy += PLAYER_GRAVITY;
            if (player->vy > PLAYER_MAX_FALL_SPEED) {
                player->vy = PLAYER_MAX_FALL_SPEED;
            }
        }

        if (player->death_timer > 0) {
            player->death_timer--;
        }

        previous_keys = keys;
        return;
    }

    if (player_is_supported(level, traps, FIX16_TO_INT(player->x), FIX16_TO_INT(player->y))) {
        player->on_ground = 1;
        player->coyote_timer = PLAYER_COYOTE_FRAMES;
    } else if (player->coyote_timer > 0) {
        player->coyote_timer--;
    }

    if (pressed & KEY_A_MASK) {
        player->jump_buffer = PLAYER_JUMP_BUFFER_FRAMES;
    } else if (player->jump_buffer > 0) {
        player->jump_buffer--;
    }

    uint8_t blocked_left = player_blocked_left(level, traps, player);
    uint8_t blocked_right = player_blocked_right(level, traps, player);

    if (keys & KEY_LEFT_MASK) {
        player->facing_right = 0;

        if (blocked_left) {
            if (player->vx < 0) {
                player->vx = 0;
            }
        } else if (player->on_ground || player->vx > -PLAYER_AIR_LIMIT) {
            player->vx -= PLAYER_ACCEL;
        }
    }

    if (keys & KEY_RIGHT_MASK) {
        player->facing_right = 1;

        if (blocked_right) {
            if (player->vx > 0) {
                player->vx = 0;
            }
        } else if (player->on_ground || player->vx < PLAYER_AIR_LIMIT) {
            player->vx += PLAYER_ACCEL;
        }
    }

    if (!(keys & (KEY_LEFT_MASK | KEY_RIGHT_MASK)) && player->on_ground) {
        if (player->vx > 0) {
            player->vx = (player->vx > PLAYER_FRICTION) ? player->vx - PLAYER_FRICTION : 0;
        } else if (player->vx < 0) {
            player->vx = (player->vx < -PLAYER_FRICTION) ? player->vx + PLAYER_FRICTION : 0;
        }
    }

    if (player->on_ground && player->vx > PLAYER_RUN_LIMIT) {
        player->vx = PLAYER_RUN_LIMIT;
    } else if (player->on_ground && player->vx < -PLAYER_RUN_LIMIT) {
        player->vx = -PLAYER_RUN_LIMIT;
    }

    if (player->jump_buffer > 0 && player->coyote_timer > 0) {
        player->y -= PLAYER_JUMP_LIFT;
        if (player->vx >= PLAYER_SPEED_600 || player->vx <= -PLAYER_SPEED_600) {
            player->vy = PLAYER_EXTRA_FAST_JUMP_SPEED;
        } else if (player->vx >= PLAYER_SPEED_200 || player->vx <= -PLAYER_SPEED_200) {
            player->vy = PLAYER_FAST_JUMP_SPEED;
        } else {
            player->vy = PLAYER_JUMP_SPEED;
        }
        player->on_ground = 0;
        player->coyote_timer = 0;
        player->jump_buffer = 0;
        player->jump_timer = 20;
        audio_play_jump();
    }

    if ((keys & KEY_A_MASK) && player->jump_timer == 16 &&
        player->vy >= PLAYER_JUMP_HOLD_CHECK_SPEED) {
        if (player->vx >= PLAYER_SPEED_600 || player->vx <= -PLAYER_SPEED_600) {
            player->vy = PLAYER_EXTRA_FAST_JUMP_SPEED;
        } else if (player->vx >= PLAYER_SPEED_200 || player->vx <= -PLAYER_SPEED_200) {
            player->vy = PLAYER_FAST_JUMP_SPEED;
        } else {
            player->vy = -REF_Y_VEL_TO_FIX(1300);
        }
    }

    if (player->jump_timer > 0) {
        player->jump_timer--;
    }

    traps_prepare_player_collision(traps, player);

    move_x(player, level, traps);
    move_y(player, level, traps);

    if (aabb_hits_death(level, traps, FIX16_TO_INT(player->x), FIX16_TO_INT(player->y))) {
        player_kill(player);
    }

    if (player->vy < PLAYER_MAX_FALL_SPEED) {
        if (!player->on_ground) {
            player->vy += PLAYER_GRAVITY;
            if (player->vy > PLAYER_MAX_FALL_SPEED) {
                player->vy = PLAYER_MAX_FALL_SPEED;
            }
        } else {
            player->vy = 0;
        }
    }

    if (FIX16_TO_INT(player->y) > level_death_y_px(level)) {
        player_kill(player);
    }

    previous_keys = keys;
}

void player_draw(const Player *player, const struct Camera *camera)
{
    int screen_x = camera_world_to_screen_x(camera, player->x);
    int screen_y = camera_world_to_screen_y(
        camera, player->y + FIX16_FROM_INT(PLAYER_HEIGHT_PX - PLAYER_SPRITE_HEIGHT_PX));

    if (player->hidden ||
        (!player->alive && player->death_timer == 0) ||
        !camera_sprite_visible(screen_x, screen_y,
                               PLAYER_SPRITE_WIDTH_PX, PLAYER_SPRITE_HEIGHT_PX)) {
        shadow_oam[PLAYER_SPRITE_INDEX].attr0 = ATTR0_DISABLED;
    } else {
        shadow_oam[PLAYER_SPRITE_INDEX].attr0 =
            (uint16_t)((screen_y & OBJ_ATTR0_Y_MASK) | ATTR0_COLOR_16 | ATTR0_SQUARE);
        shadow_oam[PLAYER_SPRITE_INDEX].attr1 =
            (uint16_t)((screen_x & OBJ_ATTR1_X_MASK) | ATTR1_SIZE_16 |
                       (player->facing_right ? 0 : ATTR1_FLIP_X));
        shadow_oam[PLAYER_SPRITE_INDEX].attr2 =
            (uint16_t)(OBJ_CHAR(player_frame_tile(player)) |
                       ATTR2_PALETTE(PLAYER_PALETTE_BANK));
    }

    memcpy(OAM, shadow_oam, sizeof(shadow_oam));
}
