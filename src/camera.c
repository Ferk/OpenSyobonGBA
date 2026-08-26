#include "camera.h"

#define CAMERA_X_LAG_SHIFT 2
#define CAMERA_Y_LAG_SHIFT 3
#define CAMERA_Y_HIGH_LINE 42
#define CAMERA_Y_LOW_LINE 104
#define CAMERA_Y_UP_LOOKAHEAD_MAX 32

static int clamp_int(int value, int min_value, int max_value)
{
    if (value < min_value) {
        return min_value;
    }

    if (value > max_value) {
        return max_value;
    }

    return value;
}

static fix16_t approach_smooth(fix16_t current, fix16_t target, uint8_t shift)
{
    fix16_t delta = target - current;

    if (delta > -FIX16_ONE / 8 && delta < FIX16_ONE / 8) {
        return target;
    }

    return current + (delta >> shift);
}

void camera_init(Camera *camera)
{
    camera->x = 0;
    camera->y = 0;
    camera->screen_x = 0;
    camera->screen_y = 0;
}

void camera_update(Camera *camera, const Player *player, const Level *level)
{
    int player_center_x = FIX16_TO_INT(player->x) + PLAYER_WIDTH_PX / 2;
    int player_top_y = FIX16_TO_INT(player->y);
    int player_bottom_y = player_top_y + PLAYER_HEIGHT_PX;
    int max_x = level->width * LEVEL_METATILE_SIZE - CAMERA_VIEW_WIDTH;
    int target_x = player_center_x - CAMERA_VIEW_WIDTH / 2;
    int target_y = 0;

    if (player_top_y < CAMERA_Y_HIGH_LINE) {
        target_y = player_top_y - CAMERA_Y_HIGH_LINE;
    } else if (player_bottom_y > CAMERA_Y_LOW_LINE) {
        target_y = player_bottom_y - CAMERA_Y_LOW_LINE;
    }

    target_x = clamp_int(target_x, 0, max_x);
    target_y = clamp_int(target_y, -CAMERA_Y_UP_LOOKAHEAD_MAX,
                         level_camera_max_y_px(level));

    camera->x = approach_smooth(camera->x, FIX16_FROM_INT(target_x), CAMERA_X_LAG_SHIFT);
    camera->y = approach_smooth(camera->y, FIX16_FROM_INT(target_y), CAMERA_Y_LAG_SHIFT);
    camera->screen_x = (int16_t)FIX16_TO_INT(camera->x);
    camera->screen_y = (int16_t)FIX16_TO_INT(camera->y);
}

int camera_x_px(const Camera *camera)
{
    return camera->screen_x;
}

int camera_y_px(const Camera *camera)
{
    return camera->screen_y;
}

int camera_world_to_screen_x(const Camera *camera, fix16_t world_x)
{
    return FIX16_TO_INT(world_x) - camera->screen_x;
}

int camera_world_to_screen_y(const Camera *camera, fix16_t world_y)
{
    return FIX16_TO_INT(world_y) - camera->screen_y;
}

uint8_t camera_sprite_visible(int screen_x, int screen_y, int width, int height)
{
    return screen_x + width >= CAMERA_WIDESCREEN_CULL_LEFT &&
           screen_x <= CAMERA_WIDESCREEN_CULL_RIGHT &&
           screen_y + height >= -32 &&
           screen_y <= CAMERA_VIEW_HEIGHT + 32;
}
