#ifndef ENEMY_H
#define ENEMY_H

#include <stdint.h>

#include "fixed.h"
#include "level.h"
#include "player.h"

#define ENEMY_MAX_ACTIVE 32
#define ENEMY_MAX_SPAWNS 64

typedef enum EnemySprite {
    ENEMY_SPRITE_GHOST = 0,
    ENEMY_SPRITE_TALL,
    ENEMY_SPRITE_HAZARD,
    ENEMY_SPRITE_ATYPE3,
    ENEMY_SPRITE_FACE_HIDDEN,
    ENEMY_SPRITE_FACE_GRIN,
    ENEMY_SPRITE_NYASSUN,
    ENEMY_SPRITE_NYASSUN_ALERT,
    ENEMY_SPRITE_TALL32,
    ENEMY_SPRITE_CUCKOO32,
    ENEMY_SPRITE_FIRE_PROJECTILE,
    ENEMY_SPRITE_SUPERJIEN,
    ENEMY_SPRITE_GIANT,
} EnemySprite;

struct Camera;
struct TrapManager;

typedef enum EnemyKind {
    ENEMY_NONE = 0,
    ENEMY_WALKER,
    ENEMY_JUMPER,
    ENEMY_STATIC_HAZARD,
    ENEMY_CEILING_FALLER,
    ENEMY_PIPE_SHOT,
    ENEMY_FIREBAR,
    ENEMY_SUPERJIEN,
    ENEMY_GIANT,
} EnemyKind;

typedef enum EnemyState {
    ENEMY_STATE_EMPTY = 0,
    ENEMY_STATE_WAITING,
    ENEMY_STATE_ACTIVE,
    ENEMY_STATE_DEAD,
} EnemyState;

typedef struct Enemy {
    EnemyKind kind;
    EnemyState state;
    fix16_t x;
    fix16_t y;
    fix16_t vx;
    fix16_t vy;
    uint8_t w;
    uint8_t h;
    int8_t dir;
    uint8_t timer;
    uint8_t on_ground;
    uint8_t launched;
    uint8_t sprite;
    uint8_t palette;
    uint8_t param;
    uint8_t emerge_timer;
} Enemy;

typedef struct EnemySpawn {
    fix16_t x;
    fix16_t y;
    uint8_t w;
    uint8_t h;
    EnemyKind kind;
    uint8_t sprite;
    uint8_t palette;
    uint8_t param;
    int8_t dir;
    uint8_t spawned;
} EnemySpawn;

typedef struct EnemyManager {
    Enemy enemies[ENEMY_MAX_ACTIVE];
    EnemySpawn spawns[ENEMY_MAX_SPAWNS];
    uint8_t count;
    uint8_t spawn_count;
} EnemyManager;

extern EnemyManager enemy_current;

void enemies_init_video(void);
void enemies_load_1_1(EnemyManager *manager, const Level *level);
void enemies_load_1_2(EnemyManager *manager, const Level *level);
void enemies_load_1_2_underground(EnemyManager *manager, const Level *level);
void enemies_load_1_2b(EnemyManager *manager, const Level *level);
void enemies_spawn_direct(EnemyManager *manager, EnemyKind kind, fix16_t x,
                          fix16_t y, uint8_t sprite, int8_t dir);
void enemies_spawn_direct_velocity(EnemyManager *manager, EnemyKind kind,
                                   fix16_t x, fix16_t y,
                                   fix16_t vx, fix16_t vy,
                                   uint8_t sprite, int8_t dir);
void enemies_spawn_from_block(EnemyManager *manager, fix16_t x, fix16_t y,
                              uint8_t sprite, int8_t dir);
uint8_t enemies_transform_near_good_mushroom(EnemyManager *manager,
                                             fix16_t x, fix16_t y);
void enemies_update(EnemyManager *manager, Level *level, Player *player,
                    struct TrapManager *traps);
void enemies_draw(EnemyManager *manager, const struct Camera *camera);

#endif
