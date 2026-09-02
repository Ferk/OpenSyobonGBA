#ifndef TRAPS_H
#define TRAPS_H

#include <stdint.h>

#include "enemy.h"
#include "fixed.h"
#include "level.h"

struct Player;
struct Camera;

#define TRAPS_MAX_TRAPS 48
#define TRAPS_MAX_ENTITIES 32
#define TRAPS_MAX_DYNAMIC_COLLIDERS 16

typedef enum TrapKind {
    TRAP_INVISIBLE_BLOCK = 0,
    TRAP_FALLING_FLOOR,
    TRAP_BUMP_SHOOTER,
    TRAP_QUESTION_BLOCK,
    TRAP_EVASIVE_BLOCK,
    TRAP_STAGE_SPAWNER,
    TRAP_ENTER_PIPE,
    TRAP_SIDE_PIPE,
    TRAP_CHECKPOINT,
    TRAP_GOAL,
    TRAP_HINT_BLOCK,
    TRAP_SPIKE_BLOCK,
    TRAP_MOVING_PLATFORM,
} TrapKind;

typedef enum TrapState {
    TRAP_IDLE = 0,
    TRAP_ACTIVE,
    TRAP_SPENT,
} TrapState;

typedef enum TrapEntityKind {
    TRAP_ENTITY_NONE = 0,
    TRAP_ENTITY_FALLING_TILE,
    TRAP_ENTITY_PROJECTILE,
    TRAP_ENTITY_COIN_POPUP,
    TRAP_ENTITY_GOOD_ITEM,
    TRAP_ENTITY_BAD_ITEM,
    TRAP_ENTITY_STAR_ITEM,
    TRAP_ENTITY_BRICK_FRAGMENT,
} TrapEntityKind;

typedef struct Trap {
    TrapKind kind;
    TrapState state;
    uint16_t source_x;
    uint16_t source_y;
    fix16_t x;
    fix16_t y;
    fix16_t w;
    fix16_t h;
    fix16_t vx;
    fix16_t vy;
    uint8_t timer;
    uint8_t subtype;
    int8_t spawn_dir;
    uint8_t visual_palette;
    uint8_t visual_metatile;
    uint8_t spent_metatile;
    uint8_t spawn_interval;
    uint8_t spawn_limit;
    uint8_t item_variant;
    uint8_t spawn_enemy_kind;
    uint8_t spawn_enemy_sprite;
    uint8_t spawn_enemy_palette;
    uint8_t spawn_enemy_param;
    uint8_t spawn_enemy_count;
    uint8_t spawn_sound;
    uint8_t trigger_channel;
    uint8_t listen_channel;
    uint8_t trigger_delay;
    fix16_t spawn_offset_x;
    fix16_t spawn_offset_y;
    fix16_t spawn_vx;
    fix16_t spawn_vy;
    fix16_t spawn_random_vx;
    fix16_t spawn_random_vy;
    fix16_t spawn_spacing_x;
    fix16_t spawn_spacing_y;
    uint16_t goal_walk_frames;
    const char *hint_text;
    uint8_t spawn_count;
    uint8_t trigger_pending;
} Trap;

typedef struct TrapEntity {
    TrapEntityKind kind;
    uint8_t active;
    fix16_t x;
    fix16_t y;
    fix16_t vx;
    fix16_t vy;
    uint8_t w;
    uint8_t h;
    uint8_t timer;
    uint8_t frame;
    uint8_t palette;
    uint8_t param;
} TrapEntity;

typedef struct TrapTrigger {
    TrapKind kind;
    uint8_t subtype;
    int8_t spawn_dir;
    uint8_t visual_palette;
    uint8_t spawn_interval;
    uint8_t spawn_limit;
    uint8_t item_variant;
    uint8_t spawn_enemy_kind;
    uint8_t spawn_enemy_sprite;
    uint8_t spawn_enemy_palette;
    uint8_t spawn_enemy_param;
    uint8_t spawn_enemy_count;
    uint8_t spawn_sound;
    uint8_t trigger_channel;
    uint8_t listen_channel;
    uint8_t trigger_delay;
    fix16_t spawn_offset_x;
    fix16_t spawn_offset_y;
    fix16_t spawn_vx;
    fix16_t spawn_vy;
    fix16_t spawn_random_vx;
    fix16_t spawn_random_vy;
    fix16_t spawn_spacing_x;
    fix16_t spawn_spacing_y;
    uint16_t goal_walk_frames;
    const char *hint_text;
    uint8_t visual_metatile;
    uint8_t spent_metatile;
    uint8_t collision;
    uint8_t hidden;
    uint16_t source_x;
    uint16_t source_y;
    fix16_t x;
    fix16_t y;
    fix16_t w;
    fix16_t h;
    fix16_t vx;
    fix16_t vy;
    fix16_t trigger_x;
    fix16_t trigger_y;
} TrapTrigger;

typedef struct TrapManager {
    Trap traps[TRAPS_MAX_TRAPS];
    TrapEntity entities[TRAPS_MAX_ENTITIES];
    uint8_t dynamic_colliders[TRAPS_MAX_DYNAMIC_COLLIDERS];
    uint8_t cell_collision[LEVEL_SOURCE_ROWS][LEVEL_SOURCE_COLS];
    uint8_t cell_hidden[LEVEL_SOURCE_ROWS][LEVEL_SOURCE_COLS];
    uint8_t trap_count;
    uint8_t dynamic_collider_count;
    uint8_t stage_clear_requested;
    uint8_t stage_transition_requested;
    uint8_t stage_transition_target;
} TrapManager;

extern TrapManager traps_current;

void traps_init_video(void);
void traps_load_1_1(TrapManager *manager, Level *level);
void traps_load_1_2(TrapManager *manager, Level *level);
void traps_load_1_2_underground(TrapManager *manager, Level *level);
void traps_load_1_2b(TrapManager *manager, Level *level);
void traps_prepare_player_collision(TrapManager *manager, const struct Player *player);
void traps_update(TrapManager *manager, Level *level, struct Player *player);
void traps_draw(TrapManager *manager, const struct Camera *camera);
uint8_t traps_hides_collision_at(const TrapManager *manager, int world_x_px, int world_y_px);
LevelCollision traps_collision_at(const TrapManager *manager, int world_x_px, int world_y_px);
uint8_t traps_on_player_bump(TrapManager *manager, Level *level, const struct Player *player,
                             int world_x_px, int world_y_px);
void traps_break_brick(TrapManager *manager, Level *level, uint16_t source_x, uint16_t source_y);
uint8_t traps_stage_clear_requested(const TrapManager *manager);
void traps_ack_stage_clear(TrapManager *manager);
uint8_t traps_stage_transition_requested(const TrapManager *manager);
uint8_t traps_stage_transition_target(const TrapManager *manager);
void traps_ack_stage_transition(TrapManager *manager);
uint8_t traps_player_can_enter_pipe(const TrapManager *manager,
                                    const struct Player *player);

#endif
