#include "enemy.h"

#include <gba.h>
#include <string.h>

#include "camera.h"
#include "enemies_16.h"
#include "generated/level1_data.h"
#include "generated/level1_2_data.h"
#include "generated/level1_2b_data.h"
#include "generated/level1_2u_data.h"
#include "messages.h"
#include "traps.h"

#define FIRST_ENEMY_SPRITE 64
#define ENEMY_TILE_INDEX 256
#define ENEMY_WALKER_TILE ENEMY_TILE_INDEX
#define ENEMY_SHELL_WALKER_TILE (ENEMY_TILE_INDEX + 4)
#define ENEMY_SHELL_TILE (ENEMY_TILE_INDEX + 8)
#define ENEMY_FALLER_TILE ENEMY_SHELL_TILE
#define ENEMY_ATYPE3_TILE (ENEMY_TILE_INDEX + 12)
#define ENEMY_FACE_TOP_LEFT (ENEMY_TILE_INDEX + 16)
#define ENEMY_FACE_TOP_RIGHT (ENEMY_TILE_INDEX + 20)
#define ENEMY_FACE_BOTTOM_LEFT (ENEMY_TILE_INDEX + 24)
#define ENEMY_FACE_BOTTOM_RIGHT (ENEMY_TILE_INDEX + 28)
#define ENEMY_NYASSUN_TILE (ENEMY_TILE_INDEX + 32)
#define ENEMY_NYASSUN_ALERT_TILE (ENEMY_TILE_INDEX + 48)
#define ENEMY_VERTICAL32_TILE (ENEMY_TILE_INDEX + 64)
#define ENEMY_CUCKOO32_TILE (ENEMY_TILE_INDEX + 72)
#define ENEMY_FIRE_PROJECTILE_TILE (ENEMY_TILE_INDEX + 80)
#define ENEMY_SUPERJIEN_TILE (ENEMY_TILE_INDEX + 84)
#define ENEMY_GIANT_TILE (ENEMY_TILE_INDEX + 88)
#define FIRST_ENEMY_EXTRA_SPRITE 96
#define ENEMY_SPAWN_AHEAD_PX 304
#define ENEMY_SPAWN_BEHIND_PX 96

#define REF_POS_TO_FIX(v) ((fix16_t)(((int64_t)(v) * 16 * FIX16_ONE) / (29 * 100)))
#define REF_VEL_TO_FIX(v) (REF_POS_TO_FIX(v) / 2)
#define REF_ACCEL_TO_FIX(v) (REF_POS_TO_FIX(v) / 4)
#define CELL_WORLD_X(cell_x) FIX16_FROM_INT((cell_x) * LEVEL_METATILE_SIZE)
#define CELL_WORLD_Y(cell_y) FIX16_FROM_INT(((cell_y) - LEVEL_VIEW_SOURCE_ROW_OFFSET) * LEVEL_METATILE_SIZE)
#define ENEMY_GRAVITY REF_ACCEL_TO_FIX(120)
#define ENEMY_MAX_FALL_SPEED REF_VEL_TO_FIX(1200)
#define WALKER_SPEED REF_VEL_TO_FIX(100)
#define SHELL_SPEED REF_VEL_TO_FIX(800)
#define SUPERJIEN_SPEED REF_VEL_TO_FIX(120)
#define GIANT_SPEED REF_VEL_TO_FIX(160)
#define SUPERJIEN_HOP_SPEED (-REF_VEL_TO_FIX(1600))
#define SUPERJIEN_HOP_COOLDOWN 40
#define CEILING_FALL_SPEED REF_VEL_TO_FIX(1200)
#define PIPE_SHOT_UP_SPEED (-REF_VEL_TO_FIX(800))
#define PIPE_SHOT_DOWN_SPEED REF_VEL_TO_FIX(1200)
#define PIPE_SHOT_GRAVITY REF_ACCEL_TO_FIX(120)
#define PIPE_SHOT_MAX_FALL_SPEED REF_VEL_TO_FIX(1200)
#define PLAYER_STOMP_BOUNCE (-REF_VEL_TO_FIX(950))
#define ENEMY_DARK_PALETTE_BANK 4
#define FIREBAR_SEGMENT_SPACING 18

EnemyManager enemy_current;

static OBJATTR enemy_oam[ENEMY_MAX_ACTIVE];
static OBJATTR enemy_extra_oam[ENEMY_MAX_ACTIVE];

static const int8_t firebar_unit_offsets[16][2] = {
    {18, 0},   {17, 7},   {13, 13},  {7, 17},
    {0, 18},   {-7, 17},  {-13, 13}, {-17, 7},
    {-18, 0},  {-17, -7}, {-13, -13}, {-7, -17},
    {0, -18},  {7, -17},  {13, -13}, {17, -7},
};

static uint8_t default_enemy_width(EnemyKind kind)
{
    return (kind == ENEMY_WALKER ||
            kind == ENEMY_SHELL_WALKER ||
            kind == ENEMY_SHELL) ? 17 :
           (kind == ENEMY_SUPERJIEN) ? 18 : 16;
}

static uint8_t aabb_overlap(int ax, int ay, int aw, int ah,
                            int bx, int by, int bw, int bh)
{
    return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
}

static uint8_t level_solid_at(const Level *level, int x, int y)
{
    LevelCollision collision = level_collision_at(level, x, y);

    return collision == LEVEL_COLLISION_SOLID;
}

static uint8_t enemy_hits_solid(const Level *level, const Enemy *enemy, int x, int y)
{
    int left = x;
    int right = x + enemy->w - 1;
    int top = y + 1;
    int bottom = y + enemy->h - 1;

    return level_solid_at(level, left, top) ||
           level_solid_at(level, right, top) ||
           level_solid_at(level, left, bottom) ||
           level_solid_at(level, right, bottom);
}

static uint8_t enemy_supported(const Level *level, const Enemy *enemy)
{
    int x = FIX16_TO_INT(enemy->x);
    int y = FIX16_TO_INT(enemy->y);
    int foot_y = y + enemy->h + 1;
    int left = x;
    int right = x + enemy->w - 1;
    LevelCollision left_collision = level_collision_at(level, left, foot_y);
    LevelCollision right_collision = level_collision_at(level, right, foot_y);

    return left_collision == LEVEL_COLLISION_SOLID ||
           right_collision == LEVEL_COLLISION_SOLID ||
           left_collision == LEVEL_COLLISION_PASS_THROUGH ||
           right_collision == LEVEL_COLLISION_PASS_THROUGH;
}

static uint8_t enemy_lands_on_platform(const Level *level, const Enemy *enemy,
                                       int old_y, int new_y)
{
    int old_bottom = old_y + enemy->h;
    int new_bottom = new_y + enemy->h;
    int x = FIX16_TO_INT(enemy->x);
    int left = x;
    int right = x + enemy->w - 1;

    if (new_bottom <= old_bottom) {
        return 0;
    }

    for (int y = old_bottom; y <= new_bottom; ++y) {
        if ((y & (LEVEL_METATILE_SIZE - 1)) == 0 &&
            (level_collision_at(level, left, y) == LEVEL_COLLISION_PASS_THROUGH ||
             level_collision_at(level, right, y) == LEVEL_COLLISION_PASS_THROUGH)) {
            return 1;
        }
    }

    return 0;
}

static Enemy *add_enemy(EnemyManager *manager, EnemyKind kind,
                        uint16_t source_x, uint16_t source_y, int8_t dir)
{
    if (manager->count >= ENEMY_MAX_ACTIVE) {
        return 0;
    }

    Enemy *enemy = &manager->enemies[manager->count++];

    enemy->kind = kind;
    enemy->state = (kind == ENEMY_CEILING_FALLER) ? ENEMY_STATE_WAITING : ENEMY_STATE_ACTIVE;
    enemy->x = CELL_WORLD_X(source_x);
    enemy->y = CELL_WORLD_Y(source_y);
    enemy->vx = 0;
    enemy->vy = 0;
    enemy->w = default_enemy_width(kind);
    enemy->h = 16;
    enemy->dir = dir;
    enemy->timer = 0;
    enemy->on_ground = 0;
    enemy->launched = 0;
    enemy->sprite = ENEMY_SPRITE_WALKER;
    enemy->palette = 0;
    enemy->param = 0;
    enemy->emerge_timer = 0;

    if (kind == ENEMY_PIPE_SHOT) {
        enemy->vy = (dir < 0) ? PIPE_SHOT_UP_SPEED : PIPE_SHOT_DOWN_SPEED;
    } else if (kind == ENEMY_SUPERJIEN) {
        enemy->timer = 0;
    }

    return enemy;
}

static void add_spawn(EnemyManager *manager, fix16_t x, fix16_t y,
                      uint8_t w, uint8_t h,
                      EnemyKind kind, uint8_t sprite, uint8_t palette,
                      uint8_t param, int8_t dir)
{
    if (manager->spawn_count >= ENEMY_MAX_SPAWNS) {
        return;
    }

    EnemySpawn *spawn = &manager->spawns[manager->spawn_count++];

    spawn->x = x;
    spawn->y = y;
    spawn->w = w;
    spawn->h = h;
    spawn->kind = kind;
    spawn->sprite = sprite;
    spawn->palette = palette;
    spawn->param = param;
    spawn->dir = dir;
    spawn->spawned = 0;
}

static void spawn_enemy_record(EnemyManager *manager, EnemySpawn *spawn)
{
    Enemy *enemy = add_enemy(manager, spawn->kind, 0, LEVEL_VIEW_SOURCE_ROW_OFFSET,
                             spawn->dir);

    if (!enemy) {
        return;
    }

    enemy->x = spawn->x;
    enemy->y = spawn->y;
    enemy->w = spawn->w;
    if ((spawn->kind == ENEMY_WALKER ||
         spawn->kind == ENEMY_SHELL_WALKER ||
         spawn->kind == ENEMY_SHELL ||
         spawn->kind == ENEMY_SUPERJIEN) &&
        enemy->w == 16) {
        enemy->w = default_enemy_width(spawn->kind);
    }
    enemy->h = spawn->h;
    enemy->sprite = spawn->sprite;
    enemy->palette = spawn->palette;
    enemy->param = spawn->param;
    spawn->spawned = 1;
}

void enemies_spawn_direct(EnemyManager *manager, EnemyKind kind, fix16_t x,
                          fix16_t y, uint8_t sprite, int8_t dir)
{
    Enemy *enemy = add_enemy(manager, kind, 0, LEVEL_VIEW_SOURCE_ROW_OFFSET, dir);

    if (!enemy) {
        return;
    }

    enemy->state = ENEMY_STATE_ACTIVE;
    enemy->x = x;
    enemy->y = y;
    enemy->vx = 0;
    enemy->vy = (kind == ENEMY_PIPE_SHOT)
                    ? ((dir < 0) ? PIPE_SHOT_UP_SPEED : PIPE_SHOT_DOWN_SPEED)
                    : 0;
    enemy->sprite = sprite;
    enemy->param = 0;
    enemy->emerge_timer = 0;
}

void enemies_spawn_direct_velocity(EnemyManager *manager, EnemyKind kind,
                                   fix16_t x, fix16_t y,
                                   fix16_t vx, fix16_t vy,
                                   uint8_t sprite, int8_t dir)
{
    Enemy *enemy = add_enemy(manager, kind, 0, LEVEL_VIEW_SOURCE_ROW_OFFSET, dir);

    if (!enemy) {
        return;
    }

    enemy->state = ENEMY_STATE_ACTIVE;
    enemy->x = x;
    enemy->y = y;
    enemy->vx = vx;
    enemy->vy = vy;
    enemy->sprite = sprite;
    enemy->param = 0;
    enemy->emerge_timer = 0;
}

static void update_spawns(EnemyManager *manager, const Player *player)
{
    int player_x = FIX16_TO_INT(player->x);

    for (uint8_t i = 0; i < manager->spawn_count; ++i) {
        EnemySpawn *spawn = &manager->spawns[i];
        int spawn_x = FIX16_TO_INT(spawn->x);

        if (!spawn->spawned &&
            spawn_x >= player_x - ENEMY_SPAWN_BEHIND_PX &&
            spawn_x <= player_x + ENEMY_SPAWN_AHEAD_PX) {
            spawn_enemy_record(manager, spawn);
        }
    }
}

static void move_enemy_x(Enemy *enemy, const Level *level)
{
    enemy->x += enemy->vx;

    int px = FIX16_TO_INT(enemy->x);
    int py = FIX16_TO_INT(enemy->y);

    if (!enemy_hits_solid(level, enemy, px, py)) {
        return;
    }

    if (enemy->vx > 0) {
        int tile_right = (px + enemy->w - 1) / LEVEL_METATILE_SIZE;
        enemy->x = FIX16_FROM_INT(tile_right * LEVEL_METATILE_SIZE - enemy->w);
        enemy->dir = -1;
    } else if (enemy->vx < 0) {
        int tile_left = px / LEVEL_METATILE_SIZE;
        enemy->x = FIX16_FROM_INT((tile_left + 1) * LEVEL_METATILE_SIZE);
        enemy->dir = 1;
    }

    enemy->vx = 0;
}

static void move_enemy_y(Enemy *enemy, const Level *level)
{
    int old_y = FIX16_TO_INT(enemy->y);

    enemy->y += enemy->vy;

    int px = FIX16_TO_INT(enemy->x);
    int py = FIX16_TO_INT(enemy->y);

    enemy->on_ground = 0;

    if (enemy->vy > 0 && enemy_lands_on_platform(level, enemy, old_y, py)) {
        int bottom_tile = (py + enemy->h) / LEVEL_METATILE_SIZE;
        enemy->y = FIX16_FROM_INT(bottom_tile * LEVEL_METATILE_SIZE - enemy->h);
        enemy->vy = 0;
        enemy->on_ground = 1;
        return;
    }

    if (!enemy_hits_solid(level, enemy, px, py)) {
        return;
    }

    if (enemy->vy > 0) {
        int tile_bottom = (py + enemy->h - 1) / LEVEL_METATILE_SIZE;
        enemy->y = FIX16_FROM_INT(tile_bottom * LEVEL_METATILE_SIZE - enemy->h);
        enemy->on_ground = 1;
    } else if (enemy->vy < 0) {
        int tile_top = py / LEVEL_METATILE_SIZE;
        enemy->y = FIX16_FROM_INT((tile_top + 1) * LEVEL_METATILE_SIZE);
    }

    enemy->vy = 0;
}

static void update_walker(Enemy *enemy, const Level *level)
{
    enemy->vx = (enemy->dir > 0) ? WALKER_SPEED : -WALKER_SPEED;
    move_enemy_x(enemy, level);
}

static void update_superjien(Enemy *enemy, const Level *level,
                             const Player *player)
{
    if (enemy->timer > 0) {
        enemy->timer--;
    }

    if (enemy->param == 1 && enemy->timer == 0 && enemy->on_ground &&
        player->vy <= -REF_VEL_TO_FIX(600)) {
        int enemy_x = FIX16_TO_INT(enemy->x);
        int player_right = FIX16_TO_INT(player->x) + PLAYER_WIDTH_PX;

        if (player_right > enemy_x - 48 && player_right < enemy_x + 56) {
            enemy->vy = SUPERJIEN_HOP_SPEED;
            enemy->y -= FIX16_FROM_INT(5);
            enemy->on_ground = 0;
            enemy->timer = SUPERJIEN_HOP_COOLDOWN;
        }
    }

    enemy->vx = (enemy->dir > 0) ? SUPERJIEN_SPEED : -SUPERJIEN_SPEED;
    move_enemy_x(enemy, level);
}

static uint8_t giant_can_break_cell(const Level *level, uint16_t source_x,
                                    uint16_t source_y)
{
    uint8_t source = level->source[source_y][source_x];
    uint8_t metatile = level->metatiles[source_y][source_x];

    if (source >= 1 && source <= 6) {
        return 1;
    }

    switch (metatile) {
    case METATILE_BRICK:
    case METATILE_QUESTION:
    case METATILE_SOLID:
    case METATILE_GROUND_TOP:
    case METATILE_GROUND_DIRT:
    case METATILE_HINT_BLOCK:
        return 1;
    default:
        return 0;
    }
}

static void break_giant_overlap_bricks(Enemy *enemy, Level *level,
                                       TrapManager *traps)
{
    int left = FIX16_TO_INT(enemy->x);
    int right = left + enemy->w - 1;
    int top = FIX16_TO_INT(enemy->y);
    int bottom = top + enemy->h;
    int source_left = left / LEVEL_METATILE_SIZE;
    int source_right = right / LEVEL_METATILE_SIZE;
    int source_top = top / LEVEL_METATILE_SIZE + LEVEL_VIEW_SOURCE_ROW_OFFSET;
    int source_bottom = bottom / LEVEL_METATILE_SIZE + LEVEL_VIEW_SOURCE_ROW_OFFSET;

    if (source_left < 0) {
        source_left = 0;
    }
    if (source_top < 0) {
        source_top = 0;
    }
    if (source_right >= level->width) {
        source_right = level->width - 1;
    }
    if (source_bottom >= level->height) {
        source_bottom = level->height - 1;
    }

    for (int y = source_top; y <= source_bottom; ++y) {
        for (int x = source_left; x <= source_right; ++x) {
            if (giant_can_break_cell(level, (uint16_t)x, (uint16_t)y)) {
                traps_break_brick(traps, level, (uint16_t)x, (uint16_t)y);
            }
        }
    }
}

static void update_giant(Enemy *enemy)
{
    enemy->vx = (enemy->dir > 0) ? GIANT_SPEED : -GIANT_SPEED;
    enemy->x += enemy->vx;
}

static void update_shell_walker(Enemy *enemy, const Level *level)
{
    enemy->vx = (enemy->dir > 0) ? WALKER_SPEED : -WALKER_SPEED;
    move_enemy_x(enemy, level);
}

static void update_shell(Enemy *enemy, const Level *level)
{
    if (enemy->param == 0) {
        enemy->vx = 0;
        return;
    }

    enemy->vx = (enemy->dir > 0) ? SHELL_SPEED : -SHELL_SPEED;
    move_enemy_x(enemy, level);
}

static void update_ceiling_faller(Enemy *enemy, const Player *player)
{
    int enemy_x = FIX16_TO_INT(enemy->x);
    int enemy_y = FIX16_TO_INT(enemy->y);
    int player_x = FIX16_TO_INT(player->x);
    int player_y = FIX16_TO_INT(player->y);

    if (enemy->state == ENEMY_STATE_WAITING &&
        player_x + PLAYER_WIDTH_PX > enemy_x - 8 &&
        player_x < enemy_x + enemy->w + 8 &&
        player_y > enemy_y) {
        enemy->state = ENEMY_STATE_ACTIVE;
        enemy->vy = CEILING_FALL_SPEED;
        if (enemy->sprite == ENEMY_SPRITE_NYASSUN) {
            enemy->sprite = ENEMY_SPRITE_NYASSUN_ALERT;
        }
    }
}

static void apply_gravity(Enemy *enemy)
{
    enemy->vy += ENEMY_GRAVITY;
    if (enemy->vy > ENEMY_MAX_FALL_SPEED) {
        enemy->vy = ENEMY_MAX_FALL_SPEED;
    }
}

static uint8_t firebar_segment_count(const Enemy *enemy)
{
    uint8_t count = enemy->param % 100;

    return (count == 0) ? 5 : count;
}

static void moving_shell_hit_enemies(EnemyManager *manager, Enemy *shell)
{
    if (shell->kind != ENEMY_SHELL || shell->param == 0 ||
        shell->state != ENEMY_STATE_ACTIVE) {
        return;
    }

    int shell_x = FIX16_TO_INT(shell->x);
    int shell_y = FIX16_TO_INT(shell->y);

    for (uint8_t i = 0; i < manager->count; ++i) {
        Enemy *enemy = &manager->enemies[i];

        if (enemy == shell || enemy->state != ENEMY_STATE_ACTIVE ||
            enemy->kind == ENEMY_SHELL || enemy->kind == ENEMY_STATIC_HAZARD ||
            enemy->kind == ENEMY_FIREBAR) {
            continue;
        }

        if (aabb_overlap(shell_x, shell_y, shell->w, shell->h,
                         FIX16_TO_INT(enemy->x), FIX16_TO_INT(enemy->y),
                         enemy->w, enemy->h)) {
            enemy->state = ENEMY_STATE_DEAD;
            enemy->timer = 8;
            enemy->vx = 0;
            enemy->vy = 0;
        }
    }
}

static void firebar_segment_position(const Enemy *enemy, uint8_t segment,
                                     int *x, int *y)
{
    uint8_t angle = (uint8_t)(enemy->timer >> 2) & 15;
    const int8_t *unit = firebar_unit_offsets[angle];
    int anchor_x = FIX16_TO_INT(enemy->x);
    int anchor_y = FIX16_TO_INT(enemy->y);

    *x = anchor_x + unit[0] * segment - 8;
    *y = anchor_y + unit[1] * segment - 8;
}

static void update_firebar(Enemy *enemy, Player *player)
{
    if (enemy->dir < 0) {
        enemy->timer -= 3;
    } else {
        enemy->timer += 3;
    }

    int player_x = FIX16_TO_INT(player->x);
    int player_y = FIX16_TO_INT(player->y);
    uint8_t segments = firebar_segment_count(enemy);

    for (uint8_t segment = 0; segment <= segments; ++segment) {
        int segment_x;
        int segment_y;

        firebar_segment_position(enemy, segment, &segment_x, &segment_y);
        if (player->alive &&
            aabb_overlap(player_x, player_y, PLAYER_WIDTH_PX, PLAYER_HEIGHT_PX,
                         segment_x + 4, segment_y + 4, 8, 8)) {
            messages_show(MESSAGE_ENEMY_TAUNT,
                          FIX16_FROM_INT(segment_x + 16),
                          FIX16_FROM_INT(segment_y), 45);
            player_kill(player);
            return;
        }
    }
}

static void stomp_shell_walker(Enemy *enemy, Player *player)
{
    int enemy_y = FIX16_TO_INT(enemy->y);

    enemy->kind = ENEMY_SHELL;
    enemy->sprite = ENEMY_SPRITE_SHELL;
    enemy->h = 16;
    enemy->param = 0;
    enemy->vx = 0;
    enemy->vy = 0;
    player->y = FIX16_FROM_INT(enemy_y - PLAYER_HEIGHT_PX - 1);
    player->vy = PLAYER_STOMP_BOUNCE;
    player->on_ground = 0;
}

static void stomp_shell(Enemy *enemy, Player *player)
{
    int enemy_x = FIX16_TO_INT(enemy->x);
    int enemy_y = FIX16_TO_INT(enemy->y);
    int player_center = FIX16_TO_INT(player->x) + PLAYER_WIDTH_PX / 2;
    int enemy_center = enemy_x + enemy->w / 2;

    if (enemy->param != 0) {
        enemy->param = 0;
        enemy->vx = 0;
    } else {
        enemy->param = 1;
        enemy->dir = (player_center <= enemy_center) ? 1 : -1;
    }

    player->y = FIX16_FROM_INT(enemy_y - PLAYER_HEIGHT_PX - 1);
    player->vy = PLAYER_STOMP_BOUNCE;
    player->on_ground = 0;
}

static void handle_player_collision(Enemy *enemy, Player *player)
{
    int enemy_x = FIX16_TO_INT(enemy->x);
    int enemy_y = FIX16_TO_INT(enemy->y);
    int player_x = FIX16_TO_INT(player->x);
    int player_y = FIX16_TO_INT(player->y);

    if (!player->alive || enemy->state != ENEMY_STATE_ACTIVE ||
        !aabb_overlap(player_x, player_y, PLAYER_WIDTH_PX, PLAYER_HEIGHT_PX,
                      enemy_x, enemy_y, enemy->w, enemy->h)) {
        return;
    }

    if (player->vy > 0 && player_y + PLAYER_HEIGHT_PX <= enemy_y + 8 &&
        enemy->kind != ENEMY_CEILING_FALLER &&
        enemy->kind != ENEMY_STATIC_HAZARD &&
        enemy->kind != ENEMY_PIPE_SHOT &&
        enemy->kind != ENEMY_SUPERJIEN &&
        enemy->kind != ENEMY_GIANT) {
        if (enemy->kind == ENEMY_SHELL_WALKER) {
            stomp_shell_walker(enemy, player);
            return;
        }
        if (enemy->kind == ENEMY_SHELL) {
            stomp_shell(enemy, player);
            return;
        }

        enemy->state = ENEMY_STATE_DEAD;
        enemy->timer = 18;
        enemy->vx = 0;
        enemy->vy = 0;
        player->y = FIX16_FROM_INT(enemy_y - PLAYER_HEIGHT_PX - 1);
        player->vy = PLAYER_STOMP_BOUNCE;
        player->on_ground = 0;
    } else {
        if (enemy->kind == ENEMY_SHELL && enemy->param == 0) {
            return;
        }

        MessageId message = MESSAGE_ENEMY_TAUNT;

        if (enemy->sprite == ENEMY_SPRITE_FACE_HIDDEN ||
            enemy->sprite == ENEMY_SPRITE_FACE_GRIN) {
            enemy->sprite = ENEMY_SPRITE_FACE_GRIN;
            message = MESSAGE_ENEMY_DELISH;
        } else if (enemy->sprite == ENEMY_SPRITE_ATYPE3) {
            message = MESSAGE_ENEMY_SLEEP;
        }

        messages_show(message,
                      enemy->x + FIX16_FROM_INT(enemy->w),
                      enemy->y, 60);
        player_kill(player);
    }
}

uint8_t enemies_transform_near_good_mushroom(EnemyManager *manager,
                                             fix16_t x, fix16_t y)
{
    int item_x = FIX16_TO_INT(x);
    int item_y = FIX16_TO_INT(y);

    for (uint8_t i = 0; i < manager->count; ++i) {
        Enemy *enemy = &manager->enemies[i];

        if (enemy->state != ENEMY_STATE_ACTIVE) {
            continue;
        }
        if (!((enemy->kind == ENEMY_WALKER &&
               enemy->sprite == ENEMY_SPRITE_WALKER) ||
              enemy->kind == ENEMY_SUPERJIEN)) {
            continue;
        }

        int enemy_x = FIX16_TO_INT(enemy->x);
        int enemy_y = FIX16_TO_INT(enemy->y);

        if (item_x + 16 > enemy_x + FIX16_TO_INT(REF_POS_TO_FIX(500)) &&
            item_x < enemy_x + enemy->w - FIX16_TO_INT(REF_POS_TO_FIX(500)) &&
            item_y + 16 > enemy_y - FIX16_TO_INT(REF_POS_TO_FIX(800)) &&
            item_y + 16 < enemy_y + FIX16_TO_INT(REF_POS_TO_FIX(4800))) {
            enemy->kind = ENEMY_GIANT;
            enemy->sprite = ENEMY_SPRITE_GIANT;
            enemy->w = 32;
            enemy->h = 32;
            enemy->param = 0;
            enemy->x -= REF_POS_TO_FIX(1050);
            enemy->y -= REF_POS_TO_FIX(1050);
            return 1;
        }
    }

    return 0;
}

static uint8_t enemy_pipe_shot_uses_gravity(const Enemy *enemy)
{
    return enemy->sprite != ENEMY_SPRITE_ATYPE3;
}

void enemies_init_video(void)
{
    for (uint8_t i = 0; i < ENEMY_MAX_ACTIVE; ++i) {
        enemy_oam[i].attr0 = ATTR0_DISABLED;
        enemy_oam[i].attr1 = 0;
        enemy_oam[i].attr2 = 0;
        enemy_extra_oam[i].attr0 = ATTR0_DISABLED;
        enemy_extra_oam[i].attr1 = 0;
        enemy_extra_oam[i].attr2 = 0;
    }

    memcpy(SPRITE_PALETTE, enemies_16Pal, 16 * sizeof(uint16_t));
    for (uint8_t i = 0; i < 16; ++i) {
        uint16_t color = enemies_16Pal[i];
        uint16_t r = color & 31;
        uint16_t g = (color >> 5) & 31;
        uint16_t b = (color >> 10) & 31;

        SPRITE_PALETTE[ENEMY_DARK_PALETTE_BANK * 16 + i] =
            (uint16_t)((r / 2) | ((g / 2) << 5) | ((b / 2) << 10));
    }
    memcpy(&SPRITE_GFX[ENEMY_TILE_INDEX * 16], enemies_16Tiles, enemies_16TilesLen);
}

static void load_generated_spawns(EnemyManager *manager,
                                  const GeneratedEnemySpawn *spawns,
                                  uint16_t spawn_count)
{
    for (uint16_t i = 0; i < spawn_count; ++i) {
        const GeneratedEnemySpawn *spawn = &spawns[i];

        if (spawn->kind == ENEMY_NONE) {
            continue;
        }

        add_spawn(manager, spawn->x, spawn->y, spawn->w, spawn->h, spawn->kind,
                  spawn->sprite, spawn->palette, spawn->param, spawn->dir);
    }
}

void enemies_load_1_1(EnemyManager *manager, const Level *level)
{
    (void)level;
    memset(manager, 0, sizeof(*manager));
    load_generated_spawns(manager, level1_enemy_spawns, level1_enemy_spawn_count);
}

void enemies_load_1_2(EnemyManager *manager, const Level *level)
{
    (void)level;
    memset(manager, 0, sizeof(*manager));
    load_generated_spawns(manager, level1_2_enemy_spawns,
                          level1_2_enemy_spawn_count);
}

void enemies_load_1_2_underground(EnemyManager *manager, const Level *level)
{
    (void)level;
    memset(manager, 0, sizeof(*manager));
    load_generated_spawns(manager, level1_2u_enemy_spawns,
                          level1_2u_enemy_spawn_count);
}

void enemies_load_1_2b(EnemyManager *manager, const Level *level)
{
    (void)level;
    memset(manager, 0, sizeof(*manager));
    load_generated_spawns(manager, level1_2b_enemy_spawns,
                          level1_2b_enemy_spawn_count);
}

void enemies_spawn_from_block(EnemyManager *manager, fix16_t x, fix16_t y,
                              uint8_t sprite, int8_t dir)
{
    Enemy *enemy = add_enemy(manager, ENEMY_WALKER, 0, LEVEL_VIEW_SOURCE_ROW_OFFSET, dir);

    if (!enemy) {
        return;
    }

    enemy->x = x;
    enemy->y = y;
    enemy->vx = 0;
    enemy->vy = 0;
    enemy->sprite = sprite;
    enemy->param = 0;
    enemy->emerge_timer = 16;
}

void enemies_update(EnemyManager *manager, Level *level, Player *player,
                    TrapManager *traps)
{
    update_spawns(manager, player);

    for (uint8_t i = 0; i < manager->count; ++i) {
        Enemy *enemy = &manager->enemies[i];

        if (enemy->state == ENEMY_STATE_EMPTY) {
            continue;
        }

        if (enemy->state == ENEMY_STATE_DEAD) {
            if (enemy->timer > 0) {
                enemy->timer--;
            } else {
                enemy->state = ENEMY_STATE_EMPTY;
            }
            continue;
        }

        if (enemy->emerge_timer > 0) {
            enemy->y -= FIX16_FROM_INT(1);
            enemy->emerge_timer--;
            continue;
        }

        if (enemy->kind == ENEMY_PIPE_SHOT) {
            enemy->x += enemy->vx;
            enemy->y += enemy->vy;

            if (enemy_pipe_shot_uses_gravity(enemy)) {
                enemy->vy += PIPE_SHOT_GRAVITY;
                if (enemy->vy > PIPE_SHOT_MAX_FALL_SPEED) {
                    enemy->vy = PIPE_SHOT_MAX_FALL_SPEED;
                }
            }

            if (FIX16_TO_INT(enemy->y) < -96 ||
                FIX16_TO_INT(enemy->y) > level_death_y_px(level) + 64) {
                enemy->state = ENEMY_STATE_EMPTY;
                continue;
            }
            handle_player_collision(enemy, player);
            continue;
        }

        if (enemy->kind == ENEMY_CEILING_FALLER) {
            update_ceiling_faller(enemy, player);
            if (enemy->state == ENEMY_STATE_WAITING) {
                continue;
            }
        } else {
            enemy->on_ground = enemy_supported(level, enemy);
        }

        if (enemy->kind == ENEMY_STATIC_HAZARD) {
            handle_player_collision(enemy, player);
            continue;
        }

        if (enemy->kind == ENEMY_FIREBAR) {
            update_firebar(enemy, player);
            continue;
        }

        if (enemy->kind == ENEMY_WALKER) {
            update_walker(enemy, level);
        } else if (enemy->kind == ENEMY_SUPERJIEN) {
            update_superjien(enemy, level, player);
        } else if (enemy->kind == ENEMY_GIANT) {
            update_giant(enemy);
        } else if (enemy->kind == ENEMY_SHELL_WALKER) {
            update_shell_walker(enemy, level);
        } else if (enemy->kind == ENEMY_SHELL) {
            update_shell(enemy, level);
        }

        if (enemy->kind != ENEMY_CEILING_FALLER) {
            apply_gravity(enemy);
        }
        move_enemy_y(enemy, level);

        if (enemy->kind == ENEMY_GIANT) {
            break_giant_overlap_bricks(enemy, level, traps);
        }
        if (enemy->kind == ENEMY_SHELL) {
            moving_shell_hit_enemies(manager, enemy);
        }

        if (FIX16_TO_INT(enemy->y) > level_death_y_px(level) + 64) {
            enemy->state = ENEMY_STATE_EMPTY;
            continue;
        }

        handle_player_collision(enemy, player);
    }
}

void enemies_draw(EnemyManager *manager, const struct Camera *camera)
{
    uint8_t extra_index = 0;

    for (uint8_t i = 0; i < ENEMY_MAX_ACTIVE; ++i) {
        Enemy *enemy = &manager->enemies[i];
        OBJATTR *obj = &enemy_oam[i];
        int screen_x = camera_world_to_screen_x(camera, enemy->x);
        int screen_y = camera_world_to_screen_y(camera, enemy->y);

        if (i >= manager->count || enemy->state == ENEMY_STATE_EMPTY ||
            !camera_sprite_visible(screen_x, screen_y, enemy->w, enemy->h)) {
            obj->attr0 = ATTR0_DISABLED;
        } else {
            uint16_t tile = ENEMY_WALKER_TILE;

            if (enemy->kind == ENEMY_FIREBAR) {
                uint8_t segments = firebar_segment_count(enemy);

                if (extra_index + segments > ENEMY_MAX_ACTIVE) {
                    obj->attr0 = ATTR0_DISABLED;
                    OAM[FIRST_ENEMY_SPRITE + i] = *obj;
                    continue;
                }

                for (uint8_t segment = 0; segment <= segments; ++segment) {
                    OBJATTR *part_obj = (segment == 0) ? obj : &enemy_extra_oam[extra_index++];
                    int part_x;
                    int part_y;

                    firebar_segment_position(enemy, segment, &part_x, &part_y);
                    part_x = camera_world_to_screen_x(camera, FIX16_FROM_INT(part_x));
                    part_y = camera_world_to_screen_y(camera, FIX16_FROM_INT(part_y));

                    if (!camera_sprite_visible(part_x, part_y, 16, 16)) {
                        part_obj->attr0 = ATTR0_DISABLED;
                        continue;
                    }

                    part_obj->attr0 = (uint16_t)((part_y & 0x00ff) |
                                                 ATTR0_COLOR_16 | ATTR0_SQUARE);
                    part_obj->attr1 = (uint16_t)((part_x & 0x01ff) | ATTR1_SIZE_16);
                    part_obj->attr2 = (uint16_t)(OBJ_CHAR(ENEMY_FALLER_TILE) |
                                                 ATTR2_PALETTE(enemy->palette));
                }

                OAM[FIRST_ENEMY_SPRITE + i] = *obj;
                continue;
            }

            if (enemy->sprite == ENEMY_SPRITE_FACE_HIDDEN) {
                obj->attr0 = ATTR0_DISABLED;
                OAM[FIRST_ENEMY_SPRITE + i] = *obj;
                continue;
            }

            if (enemy->sprite == ENEMY_SPRITE_FACE_GRIN) {
                static const uint16_t face_tiles[4] = {
                    ENEMY_FACE_TOP_LEFT,
                    ENEMY_FACE_TOP_RIGHT,
                    ENEMY_FACE_BOTTOM_LEFT,
                    ENEMY_FACE_BOTTOM_RIGHT,
                };

                if (extra_index + 3 > ENEMY_MAX_ACTIVE) {
                    obj->attr0 = ATTR0_DISABLED;
                    OAM[FIRST_ENEMY_SPRITE + i] = *obj;
                    continue;
                }

                for (uint8_t part = 0; part < 4; ++part) {
                    OBJATTR *part_obj = (part == 0) ? obj : &enemy_extra_oam[extra_index++];
                    int part_x = screen_x + (part & 1) * 16;
                    int part_y = screen_y + (part >> 1) * 16;

                    part_obj->attr0 = (uint16_t)((part_y & 0x00ff) |
                                                 ATTR0_COLOR_16 | ATTR0_SQUARE);
                    part_obj->attr1 = (uint16_t)((part_x & 0x01ff) | ATTR1_SIZE_16);
                    part_obj->attr2 = (uint16_t)(OBJ_CHAR(face_tiles[part]) |
                                                 ATTR2_PALETTE(enemy->palette));
                }

                OAM[FIRST_ENEMY_SPRITE + i] = *obj;
                continue;
            }

            if (enemy->sprite == ENEMY_SPRITE_NYASSUN ||
                enemy->sprite == ENEMY_SPRITE_NYASSUN_ALERT ||
                enemy->sprite == ENEMY_SPRITE_GIANT) {
                uint16_t base_tile = ENEMY_GIANT_TILE;

                if (enemy->sprite == ENEMY_SPRITE_NYASSUN) {
                    base_tile = ENEMY_NYASSUN_TILE;
                } else if (enemy->sprite == ENEMY_SPRITE_NYASSUN_ALERT) {
                    base_tile = ENEMY_NYASSUN_ALERT_TILE;
                }

                static const uint8_t part_offsets[4] = {0, 4, 8, 12};

                if (extra_index + 3 > ENEMY_MAX_ACTIVE) {
                    obj->attr0 = ATTR0_DISABLED;
                    OAM[FIRST_ENEMY_SPRITE + i] = *obj;
                    continue;
                }

                for (uint8_t part = 0; part < 4; ++part) {
                    OBJATTR *part_obj = (part == 0) ? obj : &enemy_extra_oam[extra_index++];
                    uint8_t tile_part = (enemy->dir > 0) ? (part ^ 1) : part;
                    int part_x = screen_x + (part & 1) * 16;
                    int part_y = screen_y + (part >> 1) * 16;

                    part_obj->attr0 = (uint16_t)((part_y & 0x00ff) |
                                                 ATTR0_COLOR_16 | ATTR0_SQUARE);
                    part_obj->attr1 = (uint16_t)((part_x & 0x01ff) | ATTR1_SIZE_16 |
                                                 (enemy->dir > 0 ? ATTR1_FLIP_X : 0));
                    part_obj->attr2 = (uint16_t)(OBJ_CHAR(base_tile + part_offsets[tile_part]) |
                                                 ATTR2_PALETTE(enemy->palette));
                }

                OAM[FIRST_ENEMY_SPRITE + i] = *obj;
                continue;
            }

            if (enemy->sprite == ENEMY_SPRITE_VERTICAL32 ||
                enemy->sprite == ENEMY_SPRITE_CUCKOO32) {
                uint16_t base_tile = (enemy->sprite == ENEMY_SPRITE_VERTICAL32)
                    ? ENEMY_VERTICAL32_TILE
                    : ENEMY_CUCKOO32_TILE;

                if (extra_index + 1 > ENEMY_MAX_ACTIVE) {
                    obj->attr0 = ATTR0_DISABLED;
                    OAM[FIRST_ENEMY_SPRITE + i] = *obj;
                    continue;
                }

                for (uint8_t part = 0; part < 2; ++part) {
                    OBJATTR *part_obj = (part == 0) ? obj : &enemy_extra_oam[extra_index++];
                    int part_y = screen_y + part * 16;

                    part_obj->attr0 = (uint16_t)((part_y & 0x00ff) |
                                                 ATTR0_COLOR_16 | ATTR0_SQUARE);
                    part_obj->attr1 = (uint16_t)((screen_x & 0x01ff) | ATTR1_SIZE_16 |
                                                 (enemy->dir > 0 ? ATTR1_FLIP_X : 0));
                    part_obj->attr2 = (uint16_t)(OBJ_CHAR(base_tile + part * 4) |
                                                 ATTR2_PALETTE(enemy->palette));
                }

                OAM[FIRST_ENEMY_SPRITE + i] = *obj;
                continue;
            }

            if (enemy->sprite == ENEMY_SPRITE_SHELL_WALKER ||
                enemy->kind == ENEMY_SHELL_WALKER) {
                tile = ENEMY_SHELL_WALKER_TILE;
            } else if (enemy->sprite == ENEMY_SPRITE_ATYPE3) {
                tile = ENEMY_ATYPE3_TILE;
            } else if (enemy->sprite == ENEMY_SPRITE_NYASSUN) {
                tile = ENEMY_NYASSUN_TILE;
            } else if (enemy->sprite == ENEMY_SPRITE_NYASSUN_ALERT) {
                tile = ENEMY_NYASSUN_ALERT_TILE;
            } else if (enemy->sprite == ENEMY_SPRITE_SHELL) {
                tile = ENEMY_SHELL_TILE;
            } else if (enemy->sprite == ENEMY_SPRITE_FIRE_PROJECTILE) {
                tile = ENEMY_FIRE_PROJECTILE_TILE;
            } else if (enemy->sprite == ENEMY_SPRITE_SUPERJIEN) {
                tile = ENEMY_SUPERJIEN_TILE;
            } else if (enemy->sprite == ENEMY_SPRITE_HAZARD ||
                       enemy->kind == ENEMY_CEILING_FALLER ||
                       enemy->kind == ENEMY_STATIC_HAZARD) {
                tile = ENEMY_FALLER_TILE;
            }

            obj->attr0 = (uint16_t)((screen_y & 0x00ff) | ATTR0_COLOR_16 | ATTR0_SQUARE);
            obj->attr1 = (uint16_t)((screen_x & 0x01ff) | ATTR1_SIZE_16 |
                                    (enemy->dir > 0 ? ATTR1_FLIP_X : 0));
            obj->attr2 = (uint16_t)(OBJ_CHAR(tile) | ATTR2_PALETTE(enemy->palette) |
                                    (enemy->kind == ENEMY_PIPE_SHOT
                                         ? ATTR2_PRIORITY(0)
                                         : ATTR2_PRIORITY(0)));
        }

        OAM[FIRST_ENEMY_SPRITE + i] = *obj;
    }

    for (uint8_t i = 0; i < extra_index; ++i) {
        OAM[FIRST_ENEMY_EXTRA_SPRITE + i] = enemy_extra_oam[i];
    }

    for (uint8_t i = extra_index; i < ENEMY_MAX_ACTIVE; ++i) {
        enemy_extra_oam[i].attr0 = ATTR0_DISABLED;
        OAM[FIRST_ENEMY_EXTRA_SPRITE + i] = enemy_extra_oam[i];
    }
}
