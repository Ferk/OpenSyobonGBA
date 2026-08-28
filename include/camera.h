#ifndef CAMERA_H
#define CAMERA_H

#include <stdint.h>

#include "fixed.h"
#include "level.h"
#include "player.h"

#define CAMERA_VIEW_WIDTH 240
#define CAMERA_VIEW_HEIGHT 160
#define CAMERA_WIDESCREEN_CULL_LEFT (-64)
#define CAMERA_WIDESCREEN_CULL_RIGHT 304

typedef struct Camera {
    fix16_t x;
    fix16_t y;
    fix16_t max_x;
    int16_t screen_x;
    int16_t screen_y;
} Camera;

void camera_init(Camera *camera);
void camera_update(Camera *camera, const Player *player, const Level *level);
int camera_x_px(const Camera *camera);
int camera_y_px(const Camera *camera);
int camera_player_left_limit_px(const Camera *camera);
int camera_world_to_screen_x(const Camera *camera, fix16_t world_x);
int camera_world_to_screen_y(const Camera *camera, fix16_t world_y);
uint8_t camera_sprite_visible(int screen_x, int screen_y, int width, int height);

#endif
