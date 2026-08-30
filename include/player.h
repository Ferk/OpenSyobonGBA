#ifndef PLAYER_H
#define PLAYER_H

#include <stdint.h>

#include "fixed.h"
#include "level.h"

#define PLAYER_WIDTH_PX 16
#define PLAYER_HEIGHT_PX 20

struct TrapManager;
struct Camera;

typedef struct Player {
    fix16_t x;
    fix16_t y;
    fix16_t vx;
    fix16_t vy;
    uint8_t on_ground;
    uint8_t facing_right;
    uint8_t alive;
    uint8_t death_timer;
    uint8_t jump_buffer;
    uint8_t coyote_timer;
    uint8_t jump_timer;
    uint8_t control_locked;
    uint8_t hidden;
    uint8_t goal_clear_done;
    uint8_t goal_phase;
    uint16_t goal_timer;
    uint16_t goal_walk_frames;
    uint8_t walk_frame;
    fix16_t walk_distance;
    uint8_t checkpoint_active;
    fix16_t checkpoint_x;
    fix16_t checkpoint_y;
    uint16_t death_count;
} Player;

void player_init_video(void);
void player_spawn(Player *player);
void player_spawn_at(Player *player, fix16_t x, fix16_t y);
void player_set_checkpoint(Player *player, fix16_t x, fix16_t y);
void player_begin_goal(Player *player, fix16_t goal_x, uint16_t walk_frames);
uint8_t player_goal_clear_done(const Player *player);
void player_update(Player *player, Level *level, struct TrapManager *traps);
void player_sync_input(void);
void player_draw(const Player *player, const struct Camera *camera);
void player_kill(Player *player);
uint16_t player_death_count(const Player *player);

#endif
