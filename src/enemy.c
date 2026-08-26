#include "enemy.h"

#include <gba.h>
#include <string.h>

#include "camera.h"
#include "enemies_16.h"
#include "messages.h"

#define FIRST_ENEMY_SPRITE 64
#define ENEMY_TILE_INDEX 128
#define ENEMY_WALKER_TILE ENEMY_TILE_INDEX
#define ENEMY_JUMPER_TILE (ENEMY_TILE_INDEX + 4)
#define ENEMY_FALLER_TILE (ENEMY_TILE_INDEX + 8)
#define ENEMY_ATYPE3_TILE (ENEMY_TILE_INDEX + 12)
#define ENEMY_CLOUD_FACE_TILE_BASE (ENEMY_TILE_INDEX + 16)
#define FIRST_ENEMY_EXTRA_SPRITE 96
#define ENEMY_SPAWN_AHEAD_PX 304
#define ENEMY_SPAWN_BEHIND_PX 96

#define REF_POS_TO_FIX(v) ((fix16_t)(((int64_t)(v) * 16 * FIX16_ONE) / (29 * 100)))
#define REF_VEL_TO_FIX(v) (REF_POS_TO_FIX(v) / 2)
#define REF_ACCEL_TO_FIX(v) (REF_POS_TO_FIX(v) / 4)
#define CELL_WORLD_X(cell_x) FIX16_FROM_INT((cell_x) * LEVEL_METATILE_SIZE)
#define CELL_WORLD_Y(cell_y) FIX16_FROM_INT(((cell_y) - LEVEL_VIEW_SOURCE_ROW_OFFSET) * LEVEL_METATILE_SIZE)
#define REF_VIEW_TOP_PX (LEVEL_VIEW_SOURCE_ROW_OFFSET * 29 - 12)

#define ENEMY_GRAVITY REF_ACCEL_TO_FIX(120)
#define ENEMY_MAX_FALL_SPEED REF_VEL_TO_FIX(1200)
#define WALKER_SPEED REF_VEL_TO_FIX(100)
#define JUMPER_SPEED REF_VEL_TO_FIX(300)
#define JUMPER_LAUNCH_SPEED (-REF_VEL_TO_FIX(1600))
#define JUMPER_LAUNCH_DELAY 100
#define CEILING_FALL_SPEED REF_VEL_TO_FIX(1200)
#define PLAYER_STOMP_BOUNCE (-REF_VEL_TO_FIX(950))

EnemyManager enemy_current;

static OBJATTR enemy_oam[ENEMY_MAX_ACTIVE];
static OBJATTR enemy_extra_oam[ENEMY_MAX_ACTIVE];

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
    int left = x + 1;
    int right = x + enemy->w - 2;
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
    int left = x + 2;
    int right = x + enemy->w - 3;
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
    int left = x + 2;
    int right = x + enemy->w - 3;

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

static uint8_t enemy_has_floor_ahead(const Level *level, const Enemy *enemy)
{
    int x = FIX16_TO_INT(enemy->x);
    int y = FIX16_TO_INT(enemy->y);
    int probe_x = (enemy->dir > 0) ? x + enemy->w + 2 : x - 2;
    int probe_y = y + enemy->h + 4;
    LevelCollision collision = level_collision_at(level, probe_x, probe_y);

    return collision == LEVEL_COLLISION_SOLID ||
           collision == LEVEL_COLLISION_PASS_THROUGH;
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
    enemy->w = 16;
    enemy->h = 16;
    enemy->dir = dir;
    enemy->timer = JUMPER_LAUNCH_DELAY;
    enemy->on_ground = 0;
    enemy->launched = 0;
    enemy->sprite = ENEMY_SPRITE_GHOST;
    enemy->emerge_timer = 0;

    return enemy;
}

static fix16_t ref_px_to_world_x(int ref_px)
{
    return (fix16_t)(((int64_t)ref_px * LEVEL_METATILE_SIZE * FIX16_ONE) / 29);
}

static fix16_t ref_px_to_world_y(int ref_px)
{
    return (fix16_t)(((int64_t)(ref_px - REF_VIEW_TOP_PX) *
                      LEVEL_METATILE_SIZE * FIX16_ONE) / 29);
}

static Enemy *add_enemy_reference(EnemyManager *manager, EnemyKind kind,
                                  int ref_x_px, int ref_y_px, int8_t dir)
{
    Enemy *enemy = add_enemy(manager, kind, 0, LEVEL_VIEW_SOURCE_ROW_OFFSET, dir);

    if (!enemy) {
        return 0;
    }

    enemy->x = ref_px_to_world_x(ref_x_px);
    enemy->y = ref_px_to_world_y(ref_y_px);

    return enemy;
}

static void add_spawn(EnemyManager *manager, fix16_t x, fix16_t y,
                      EnemyKind kind, uint8_t sprite, int8_t dir)
{
    if (manager->spawn_count >= ENEMY_MAX_SPAWNS) {
        return;
    }

    EnemySpawn *spawn = &manager->spawns[manager->spawn_count++];

    spawn->x = x;
    spawn->y = y;
    spawn->kind = kind;
    spawn->sprite = sprite;
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
    enemy->sprite = spawn->sprite;
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
    enemy->vy = 0;
    enemy->sprite = sprite;
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

static void update_jumper(Enemy *enemy, const Level *level)
{
    if (!enemy->launched && enemy->on_ground) {
        if (enemy->timer > 0) {
            enemy->timer--;
        } else {
            enemy->vx = (enemy->dir > 0) ? JUMPER_SPEED : -JUMPER_SPEED;
            enemy->vy = JUMPER_LAUNCH_SPEED;
            enemy->y -= FIX16_FROM_INT(1);
            enemy->launched = 1;
        }
    }

    if (enemy->launched && enemy->on_ground) {
        enemy->vx = 0;
    }

    if (enemy->vx != 0 && enemy->on_ground && !enemy_has_floor_ahead(level, enemy)) {
        enemy->dir = (int8_t)-enemy->dir;
        enemy->vx = -enemy->vx;
    }

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
    }
}

static void apply_gravity(Enemy *enemy)
{
    enemy->vy += ENEMY_GRAVITY;
    if (enemy->vy > ENEMY_MAX_FALL_SPEED) {
        enemy->vy = ENEMY_MAX_FALL_SPEED;
    }
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
        enemy->kind != ENEMY_STATIC_HAZARD) {
        enemy->state = ENEMY_STATE_DEAD;
        enemy->timer = 18;
        enemy->vx = 0;
        enemy->vy = 0;
        player->y = FIX16_FROM_INT(enemy_y - PLAYER_HEIGHT_PX - 1);
        player->vy = PLAYER_STOMP_BOUNCE;
        player->on_ground = 0;
    } else {
        MessageId message = MESSAGE_ENEMY_TAUNT;

        if (enemy->sprite == ENEMY_SPRITE_CLOUD ||
            enemy->sprite == ENEMY_SPRITE_CLOUD_FACE) {
            enemy->sprite = ENEMY_SPRITE_CLOUD_FACE;
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
    memcpy(&SPRITE_GFX[ENEMY_TILE_INDEX * 16], enemies_16Tiles, enemies_16TilesLen);
}

void enemies_load_1_1(EnemyManager *manager, const Level *level)
{
    memset(manager, 0, sizeof(*manager));

    for (uint16_t y = 0; y < level->height; ++y) {
        for (uint16_t x = 0; x < level->width; ++x) {
            if (level->source[y][x] == 50) {
                add_spawn(manager, CELL_WORLD_X(x), CELL_WORLD_Y(y),
                          ENEMY_WALKER, ENEMY_SPRITE_GHOST, -1);
            } else if (level->source[y][x] == 51) {
                add_spawn(manager, CELL_WORLD_X(x), CELL_WORLD_Y(y),
                          ENEMY_WALKER, ENEMY_SPRITE_TALL, -1);
            }
        }
    }

    Enemy *cloud = add_enemy_reference(manager, ENEMY_STATIC_HAZARD,
                                       103 * 29, 5 * 29 - 12, -1);
    if (cloud) {
        cloud->sprite = ENEMY_SPRITE_CLOUD;
        cloud->w = 64;
        cloud->h = 32;
    }
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
    enemy->emerge_timer = 16;
}

void enemies_update(EnemyManager *manager, const Level *level, Player *player)
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

        if (enemy->kind == ENEMY_WALKER) {
            update_walker(enemy, level);
        } else if (enemy->kind == ENEMY_JUMPER) {
            update_jumper(enemy, level);
        }

        if (enemy->kind != ENEMY_CEILING_FALLER) {
            apply_gravity(enemy);
        }
        move_enemy_y(enemy, level);

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

            if (enemy->sprite == ENEMY_SPRITE_CLOUD) {
                obj->attr0 = ATTR0_DISABLED;
                OAM[FIRST_ENEMY_SPRITE + i] = *obj;
                continue;
            }

            if (enemy->sprite == ENEMY_SPRITE_CLOUD_FACE) {
                if (extra_index + 7 >= ENEMY_MAX_ACTIVE) {
                    obj->attr0 = ATTR0_DISABLED;
                    OAM[FIRST_ENEMY_SPRITE + i] = *obj;
                    continue;
                }

                for (uint8_t part = 0; part < 8; ++part) {
                    OBJATTR *part_obj = (part == 0) ? obj : &enemy_extra_oam[extra_index++];
                    int part_x = screen_x + (part & 3) * 16;
                    int part_y = screen_y + (part >> 2) * 16;

                    part_obj->attr0 = (uint16_t)((part_y & 0x00ff) |
                                                 ATTR0_COLOR_16 | ATTR0_SQUARE);
                    part_obj->attr1 = (uint16_t)((part_x & 0x01ff) | ATTR1_SIZE_16);
                    part_obj->attr2 = (uint16_t)(OBJ_CHAR(ENEMY_CLOUD_FACE_TILE_BASE +
                                                           part * 4) | ATTR2_PALETTE(0));
                }

                OAM[FIRST_ENEMY_SPRITE + i] = *obj;
                continue;
            }

            if (enemy->sprite == ENEMY_SPRITE_TALL || enemy->kind == ENEMY_JUMPER) {
                tile = ENEMY_JUMPER_TILE;
            } else if (enemy->sprite == ENEMY_SPRITE_ATYPE3) {
                tile = ENEMY_ATYPE3_TILE;
            } else if (enemy->sprite == ENEMY_SPRITE_HAZARD ||
                       enemy->kind == ENEMY_CEILING_FALLER ||
                       enemy->kind == ENEMY_STATIC_HAZARD) {
                tile = ENEMY_FALLER_TILE;
            }

            obj->attr0 = (uint16_t)((screen_y & 0x00ff) | ATTR0_COLOR_16 | ATTR0_SQUARE);
            obj->attr1 = (uint16_t)((screen_x & 0x01ff) | ATTR1_SIZE_16 |
                                    (enemy->dir > 0 ? ATTR1_FLIP_X : 0));
            obj->attr2 = (uint16_t)(OBJ_CHAR(tile) | ATTR2_PALETTE(0));
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
