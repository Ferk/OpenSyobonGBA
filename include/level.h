#ifndef LEVEL_H
#define LEVEL_H

#include <stdint.h>

#define LEVEL_SOURCE_ROWS 17
#define LEVEL_SOURCE_COLS 150

#define LEVEL_METATILE_SIZE 16
#define LEVEL_SCREEN_METATILE_COLS 15
#define LEVEL_SCREEN_METATILE_ROWS 10
#define LEVEL_VIEW_SOURCE_ROW_OFFSET 5

#define LEVEL_STREAM_MARGIN_PX 64
#define LEVEL_STREAM_LEFT_PX (-LEVEL_STREAM_MARGIN_PX)
#define LEVEL_STREAM_RIGHT_PX 304
#define LEVEL_STREAM_TILE_COLS ((LEVEL_STREAM_RIGHT_PX - LEVEL_STREAM_LEFT_PX) / 8)
#define LEVEL_STREAM_TOP_PX (-32)

typedef enum MetatileId {
    METATILE_EMPTY = 0,
    METATILE_BRICK,
    METATILE_QUESTION,
    METATILE_SOLID,
    METATILE_GROUND_TOP,
    METATILE_GROUND_DIRT,
    METATILE_HIDDEN,
    METATILE_PLATFORM,
    METATILE_HILL_00,
    METATILE_HILL_01,
    METATILE_HILL_02,
    METATILE_HILL_03,
    METATILE_HILL_04,
    METATILE_HILL_10,
    METATILE_HILL_11,
    METATILE_HILL_12,
    METATILE_HILL_13,
    METATILE_HILL_14,
    METATILE_HILL_20,
    METATILE_HILL_21,
    METATILE_HILL_22,
    METATILE_HILL_23,
    METATILE_HILL_24,
    METATILE_CLOUD_00,
    METATILE_CLOUD_01,
    METATILE_CLOUD_02,
    METATILE_CLOUD_03,
    METATILE_CLOUD_10,
    METATILE_CLOUD_11,
    METATILE_CLOUD_12,
    METATILE_CLOUD_13,
    METATILE_PIPE_TOP_LEFT,
    METATILE_PIPE_TOP_RIGHT,
    METATILE_PIPE_BODY_LEFT,
    METATILE_PIPE_BODY_RIGHT,
    METATILE_SPIKES,
    METATILE_BG_SPIKES_00,
    METATILE_BG_SPIKES_01,
    METATILE_BG_SPIKES_02,
    METATILE_BG_SPIKES_03,
    METATILE_CASTLE_00,
    METATILE_CASTLE_01,
    METATILE_CASTLE_02,
    METATILE_CASTLE_03,
    METATILE_CASTLE_04,
    METATILE_CASTLE_05,
    METATILE_CASTLE_10,
    METATILE_CASTLE_11,
    METATILE_CASTLE_12,
    METATILE_CASTLE_13,
    METATILE_CASTLE_14,
    METATILE_CASTLE_15,
    METATILE_CASTLE_20,
    METATILE_CASTLE_21,
    METATILE_CASTLE_22,
    METATILE_CASTLE_23,
    METATILE_CASTLE_24,
    METATILE_CASTLE_25,
    METATILE_CHECKPOINT_00,
    METATILE_CHECKPOINT_01,
    METATILE_CHECKPOINT_10,
    METATILE_CHECKPOINT_11,
    METATILE_CHECKPOINT_20,
    METATILE_CHECKPOINT_21,
    METATILE_GOAL_00,
    METATILE_GOAL_10,
    METATILE_GOAL_20,
    METATILE_GOAL_30,
} MetatileId;

typedef enum LevelCollision {
    LEVEL_COLLISION_EMPTY = 0,
    LEVEL_COLLISION_SOLID,
    LEVEL_COLLISION_PASS_THROUGH,
    LEVEL_COLLISION_DEATH,
} LevelCollision;

typedef struct Level {
    uint16_t width;
    uint16_t height;
    uint8_t source[LEVEL_SOURCE_ROWS][LEVEL_SOURCE_COLS];
    uint8_t metatiles[LEVEL_SOURCE_ROWS][LEVEL_SOURCE_COLS];
    uint8_t palettes[LEVEL_SOURCE_ROWS][LEVEL_SOURCE_COLS];
} Level;

extern Level level_current;

void level_init_video(void);
void level_load_1_1(Level *level);
void level_load_1_2(Level *level);
void level_stream_bg0(const Level *level, int camera_x_px, int camera_y_px);
void level_force_stream_update(void);
uint8_t level_metatile_at(const Level *level, int world_x_px, int world_y_px);
LevelCollision level_collision_at(const Level *level, int world_x_px, int world_y_px);
void level_set_metatile_cell(Level *level, uint16_t source_x, uint16_t source_y, uint8_t metatile);
void level_restore_source_cell(Level *level, uint16_t source_x, uint16_t source_y);
int level_death_y_px(const Level *level);
int level_camera_max_y_px(const Level *level);

#endif
