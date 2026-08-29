#include "level.h"

#include <gba.h>
#include <string.h>

#include "generated/level1_data.h"
#include "tiles_16.h"

#define BG_CHARBLOCK 0
#define BG_SCREENBLOCK 28
#define BG_MAP_WIDTH_TILES 64
#define BG_MAP_HEIGHT_TILES 32

#define TILE_EMPTY 0
#define BG_SE_PALBANK(bank) ((uint16_t)((bank) << 12))
#define RGB15(r, g, b) ((uint16_t)((r) | ((g) << 5) | ((b) << 10)))

typedef struct SourceEntry {
    uint8_t x;
    uint8_t y;
    uint8_t value;
} SourceEntry;

typedef struct SourceRun {
    uint8_t x0;
    uint8_t x1;
    uint8_t y;
    uint8_t value;
} SourceRun;

Level level_current;

static int stream_first_world_tile_x;
static int stream_first_world_tile_y;
static uint8_t stream_map_origin_col;
static uint8_t stream_dirty = 1;

static const uint16_t bg_palette_banks[16][16] = {
    {
        RGB15(31, 0, 31), RGB15(20, 27, 31), RGB15(19, 9, 4), RGB15(28, 19, 9),
        RGB15(31, 27, 10), RGB15(11, 21, 7), RGB15(7, 14, 4), RGB15(14, 9, 5),
        RGB15(9, 6, 3), RGB15(27, 27, 28), RGB15(19, 19, 20), RGB15(5, 5, 6),
        RGB15(31, 31, 31), RGB15(0, 28, 0), RGB15(0, 18, 0), RGB15(0, 0, 0),
    },
    {
        RGB15(31, 0, 31), RGB15(20, 27, 31), RGB15(11, 11, 12), RGB15(23, 23, 24),
        RGB15(31, 27, 10), RGB15(11, 21, 7), RGB15(7, 14, 4), RGB15(15, 15, 16),
        RGB15(7, 7, 8), RGB15(27, 27, 28), RGB15(19, 19, 20), RGB15(5, 5, 6),
        RGB15(31, 31, 31), RGB15(0, 28, 0), RGB15(0, 18, 0), RGB15(0, 0, 0),
    },
    {
        RGB15(31, 0, 31), RGB15(20, 27, 31), RGB15(7, 11, 20), RGB15(16, 21, 30),
        RGB15(31, 27, 10), RGB15(11, 21, 7), RGB15(7, 14, 4), RGB15(8, 13, 22),
        RGB15(3, 5, 10), RGB15(27, 27, 28), RGB15(19, 19, 20), RGB15(5, 5, 6),
        RGB15(31, 31, 31), RGB15(0, 28, 0), RGB15(0, 18, 0), RGB15(0, 0, 0),
    },
    {
        RGB15(31, 0, 31), RGB15(20, 27, 31), RGB15(15, 8, 3), RGB15(31, 22, 8),
        RGB15(31, 27, 10), RGB15(11, 21, 7), RGB15(7, 14, 4), RGB15(19, 10, 4),
        RGB15(10, 5, 2), RGB15(27, 27, 28), RGB15(19, 19, 20), RGB15(5, 5, 6),
        RGB15(31, 31, 31), RGB15(0, 28, 0), RGB15(0, 18, 0), RGB15(0, 0, 0),
    },
};

static const SourceEntry level_1_2_entries[] = {
    { 11, 8, 7 }, { 16, 8, 7 }, { 1, 10, 83 }, { 13, 11, 44 },
};

static const SourceRun level_1_2_runs[] = {
    { 0, 39, 13, 5 }, { 43, 53, 13, 5 }, { 57, 71, 13, 5 },
    { 77, 84, 13, 5 }, { 88, 93, 13, 5 }, { 95, 136, 13, 5 },
    { 138, 144, 13, 5 },
    { 0, 39, 14, 6 }, { 43, 53, 14, 6 }, { 57, 71, 14, 6 },
    { 77, 84, 14, 6 }, { 88, 93, 14, 6 }, { 95, 136, 14, 6 },
    { 138, 145, 14, 6 },
};

static const SourceEntry level_1_2_underground_entries[] = {
    { 147, 0, 97 }, { 0, 1, 1 }, { 53, 1, 1 }, { 65, 1, 1 },
    { 75, 1, 1 }, { 131, 1, 98 }, { 0, 2, 1 }, { 84, 2, 4 },
    { 0, 3, 1 }, { 84, 3, 4 }, { 0, 4, 1 }, { 84, 4, 4 },
    { 0, 5, 1 }, { 26, 5, 7 }, { 79, 5, 2 }, { 84, 5, 4 },
    { 0, 6, 1 }, { 84, 6, 4 }, { 0, 7, 1 }, { 129, 7, 97 },
    { 130, 7, 44 }, { 0, 8, 1 }, { 19, 8, 7 }, { 81, 8, 1 },
    { 84, 8, 54 }, { 86, 8, 1 }, { 108, 8, 97 }, { 0, 9, 1 },
    { 7, 9, 98 }, { 8, 9, 2 }, { 9, 9, 2 }, { 10, 9, 98 },
    { 20, 9, 4 }, { 25, 9, 4 }, { 49, 9, 98 }, { 81, 9, 1 },
    { 86, 9, 1 }, { 87, 9, 1 }, { 121, 9, 1 }, { 122, 9, 1 },
    { 0, 10, 1 }, { 20, 10, 4 }, { 25, 10, 4 }, { 27, 10, 4 },
    { 29, 10, 51 }, { 72, 10, 7 }, { 80, 10, 1 }, { 81, 10, 1 },
    { 108, 10, 1 }, { 121, 10, 1 }, { 122, 10, 1 }, { 0, 11, 1 },
    { 16, 11, 4 }, { 18, 11, 4 }, { 20, 11, 4 }, { 25, 11, 4 },
    { 27, 11, 4 }, { 29, 11, 4 }, { 54, 11, 40 }, { 61, 11, 30 },
    { 96, 11, 1 }, { 107, 11, 1 }, { 108, 11, 1 }, { 121, 11, 1 },
    { 122, 11, 1 }, { 0, 12, 1 }, { 2, 12, 7 }, { 10, 12, 50 },
    { 12, 12, 50 }, { 14, 12, 4 }, { 16, 12, 4 }, { 18, 12, 4 },
    { 20, 12, 4 }, { 22, 12, 50 }, { 25, 12, 4 }, { 27, 12, 4 },
    { 29, 12, 4 }, { 31, 12, 4 }, { 39, 12, 7 }, { 54, 12, 41 },
    { 121, 12, 1 }, { 122, 12, 1 }, { 0, 13, 5 }, { 1, 13, 5 },
    { 60, 13, 5 }, { 61, 13, 5 }, { 121, 13, 1 }, { 122, 13, 1 },
    { 0, 14, 6 }, { 1, 14, 6 }, { 60, 14, 6 }, { 61, 14, 6 },
    { 121, 14, 1 }, { 122, 14, 1 },
};

static const SourceRun level_1_2_underground_runs[] = {
    { 4, 51, 1, 1 }, { 76, 84, 1, 4 }, { 85, 108, 1, 1 },
    { 121, 130, 1, 1 }, { 133, 149, 1, 1 }, { 133, 140, 2, 1 },
    { 133, 140, 3, 1 }, { 133, 140, 4, 1 }, { 133, 140, 5, 1 },
    { 133, 140, 6, 1 }, { 133, 140, 7, 1 }, { 123, 127, 8, 7 },
    { 133, 140, 8, 1 }, { 21, 24, 9, 7 }, { 82, 85, 9, 4 },
    { 128, 140, 9, 1 }, { 82, 85, 10, 4 }, { 86, 88, 10, 1 },
    { 128, 140, 10, 1 }, { 79, 81, 11, 1 }, { 82, 85, 11, 4 },
    { 86, 89, 11, 1 }, { 128, 140, 11, 1 }, { 36, 38, 12, 50 },
    { 78, 81, 12, 1 }, { 82, 85, 12, 4 }, { 86, 90, 12, 1 },
    { 95, 97, 12, 1 }, { 106, 108, 12, 1 }, { 128, 140, 12, 1 },
    { 3, 23, 13, 5 }, { 25, 42, 13, 5 }, { 46, 48, 13, 5 },
    { 52, 57, 13, 5 }, { 66, 71, 13, 5 }, { 75, 108, 13, 5 },
    { 128, 140, 13, 1 }, { 141, 149, 13, 5 }, { 3, 23, 14, 6 },
    { 25, 42, 14, 6 }, { 46, 48, 14, 6 }, { 52, 57, 14, 6 },
    { 66, 71, 14, 6 }, { 75, 108, 14, 6 }, { 128, 140, 14, 1 },
    { 141, 149, 14, 6 },
};

static uint16_t tile_for_metatile(uint8_t metatile, uint8_t palette,
                                  uint8_t sub_x, uint8_t sub_y)
{
    uint16_t base = TILE_EMPTY;

    switch (metatile) {
    case METATILE_GROUND_TOP:
        base = (sub_y == 0) ? METATILE_GROUND_TOP : METATILE_GROUND_DIRT;
        break;
    default:
        base = metatile;
        break;
    }

    return (uint16_t)(base * 4 + sub_y * 2 + sub_x) | BG_SE_PALBANK(palette);
}

static uint16_t *bg0_map_entry(uint8_t tile_x, uint8_t tile_y)
{
    uint16_t *base = (uint16_t *)SCREEN_BASE_BLOCK(BG_SCREENBLOCK);
    uint16_t page = tile_x / 32;
    uint16_t local_x = tile_x & 31;

    return &base[page * 1024 + tile_y * 32 + local_x];
}

static int floor_div_int(int numerator, int denominator)
{
    if (numerator >= 0) {
        return numerator / denominator;
    }

    return -(((-numerator) + denominator - 1) / denominator);
}

static uint16_t tile_id_at_world_tile(const Level *level, int world_tile_x, int world_tile_y)
{
    int source_y = floor_div_int(world_tile_y, 2) + LEVEL_VIEW_SOURCE_ROW_OFFSET;
    int source_x = floor_div_int(world_tile_x, 2);
    uint8_t sub_x = (uint8_t)(world_tile_x & 1);
    uint8_t sub_y = (uint8_t)(world_tile_y & 1);

    if (source_y < 0 || source_y >= level->height ||
        source_x < 0 || source_x >= level->width) {
        return TILE_EMPTY;
    }

    return tile_for_metatile(level->metatiles[source_y][source_x],
                             level->palettes[source_y][source_x],
                             sub_x, sub_y);
}

static void stream_draw_column(const Level *level, uint8_t map_x, int world_tile_x,
                               int first_world_tile_y)
{
    for (uint8_t map_y = 0; map_y < BG_MAP_HEIGHT_TILES; ++map_y) {
        *bg0_map_entry(map_x, map_y) =
            tile_id_at_world_tile(level, world_tile_x, first_world_tile_y + map_y);
    }
}

static void stream_fill_window(const Level *level, int first_world_tile_x,
                               int first_world_tile_y)
{
    stream_map_origin_col = 0;

    for (uint8_t map_x = 0; map_x < BG_MAP_WIDTH_TILES; ++map_x) {
        if (map_x < LEVEL_STREAM_TILE_COLS) {
            stream_draw_column(level, map_x, first_world_tile_x + map_x,
                               first_world_tile_y);
        } else {
            for (uint8_t map_y = 0; map_y < BG_MAP_HEIGHT_TILES; ++map_y) {
                *bg0_map_entry(map_x, map_y) = TILE_EMPTY;
            }
        }
    }

    stream_first_world_tile_x = first_world_tile_x;
    stream_first_world_tile_y = first_world_tile_y;
    stream_dirty = 0;
}

static uint8_t wrapped_map_col(int map_x)
{
    return (uint8_t)(map_x & (BG_MAP_WIDTH_TILES - 1));
}

static uint8_t source_to_metatile(uint8_t source)
{
    if (source == 0 || source == 7) {
        return METATILE_EMPTY;
    }

    if (source == 1) {
        return METATILE_BRICK;
    }

    if (source == 2) {
        return METATILE_QUESTION;
    }

    if (source == 4) {
        return METATILE_SOLID;
    }

    if (source == 5) {
        return METATILE_GROUND_TOP;
    }

    if (source == 6) {
        return METATILE_GROUND_DIRT;
    }

    if ((source >= 40 && source <= 89) || source == 99) {
        return METATILE_EMPTY;
    }

    return METATILE_EMPTY;
}

static uint8_t source_to_palette(uint8_t source)
{
    (void)source;
    return 0;
}

static void stamp_metatile(Level *level, uint16_t x, uint16_t y, uint8_t metatile)
{
    if (x < level->width && y < level->height) {
        level->metatiles[y][x] = metatile;
    }
}

static void stamp_decor(Level *level, uint16_t source_x, uint16_t source_y,
                        uint8_t first_metatile, uint8_t cols, uint8_t rows)
{
    for (uint8_t row = 0; row < rows; ++row) {
        for (uint8_t col = 0; col < cols; ++col) {
            stamp_metatile(level, source_x + col, source_y + row,
                           (uint8_t)(first_metatile + row * cols + col));
        }
    }
}

static void stamp_pipe_pair(Level *level, uint16_t source_x, uint16_t source_y,
                            uint8_t left_metatile, uint8_t right_metatile)
{
    stamp_metatile(level, source_x, source_y, left_metatile);
    stamp_metatile(level, source_x + 1, source_y, right_metatile);
}

static void stamp_goal(Level *level, uint16_t source_x, uint16_t source_y)
{
    uint16_t visual_y = source_y;

    if (visual_y < LEVEL_VIEW_SOURCE_ROW_OFFSET - 2) {
        visual_y = LEVEL_VIEW_SOURCE_ROW_OFFSET - 2;
    }

    uint16_t rows = (visual_y < 12) ? (uint16_t)(12 - visual_y) : 1;

    for (uint16_t row = 0; row < rows; ++row) {
        uint8_t source_row = (row < 4) ? (uint8_t)row : 3;
        stamp_metatile(level, source_x, visual_y + row,
                       (uint8_t)(METATILE_GOAL_00 + source_row));
    }
}

static void convert_source_to_metatiles(Level *level)
{
    for (uint16_t y = 0; y < level->height; ++y) {
        for (uint16_t x = 0; x < level->width; ++x) {
            level->metatiles[y][x] = source_to_metatile(level->source[y][x]);
            level->palettes[y][x] = source_to_palette(level->source[y][x]);
        }
    }

    for (uint16_t y = 0; y < level->height; ++y) {
        for (uint16_t x = 0; x < level->width; ++x) {
            if (level->source[y][x] == 80) {
                stamp_decor(level, x, y, METATILE_HILL_00, 5, 3);
            } else if (level->source[y][x] == 81) {
                stamp_decor(level, x, y, METATILE_BG_SPIKES_00, 4, 1);
            } else if (level->source[y][x] == 82) {
                stamp_decor(level, x, y, METATILE_CLOUD_00, 4, 2);
            } else if (level->source[y][x] == 83) {
                stamp_decor(level, x, y, METATILE_CASTLE_00, 6, 3);
            } else if (level->source[y][x] == 99) {
                stamp_goal(level, x, y);
            } else if (level->source[y][x] == 40) {
                stamp_pipe_pair(level, x, y, METATILE_PIPE_TOP_LEFT, METATILE_PIPE_TOP_RIGHT);
            } else if (level->source[y][x] == 41) {
                stamp_pipe_pair(level, x, y, METATILE_PIPE_BODY_LEFT, METATILE_PIPE_BODY_RIGHT);
            }
        }
    }

    stamp_decor(level, 103, 5, METATILE_CLOUD_00, 4, 2);
}

void level_init_video(void)
{
    memcpy(BG_PALETTE, bg_palette_banks, sizeof(bg_palette_banks));
    memcpy(BG_PALETTE, tiles_16Pal, 16 * sizeof(uint16_t));
    memcpy(CHAR_BASE_BLOCK(BG_CHARBLOCK), tiles_16Tiles, tiles_16TilesLen);

    REG_BG0CNT = BG_16_COLOR | CHAR_BASE(BG_CHARBLOCK) | BG_PRIORITY(1) |
                 SCREEN_BASE(BG_SCREENBLOCK) | TEXTBG_SIZE_512x256;
    REG_BG0HOFS = LEVEL_STREAM_MARGIN_PX;
    REG_BG0VOFS = 0;
    stream_first_world_tile_x = 0;
    stream_first_world_tile_y = LEVEL_STREAM_TOP_PX / 8;
    stream_map_origin_col = 0;
    stream_dirty = 1;
}

void level_load_1_1(Level *level)
{
    memset(level, 0, sizeof(*level));
    level->width = LEVEL1_WIDTH;
    level->height = LEVEL1_HEIGHT;
    memcpy(level->source, level1_source, sizeof(level1_source));
    memcpy(level->metatiles, level1_metatiles, sizeof(level1_metatiles));
    memcpy(level->palettes, level1_palettes, sizeof(level1_palettes));
    level_force_stream_update();
}

void level_load_1_2(Level *level)
{
    memset(level, 0, sizeof(*level));
    level->width = LEVEL_SOURCE_COLS;
    level->height = LEVEL_SOURCE_ROWS;

    for (uint16_t i = 0; i < sizeof(level_1_2_entries) / sizeof(level_1_2_entries[0]); ++i) {
        const SourceEntry *entry = &level_1_2_entries[i];

        level->source[entry->y][entry->x] = entry->value;
    }

    for (uint16_t i = 0; i < sizeof(level_1_2_runs) / sizeof(level_1_2_runs[0]); ++i) {
        const SourceRun *run = &level_1_2_runs[i];

        for (uint16_t x = run->x0; x <= run->x1; ++x) {
            level->source[run->y][x] = run->value;
        }
    }

    convert_source_to_metatiles(level);
    level_force_stream_update();
}

void level_load_1_2_underground(Level *level)
{
    memset(level, 0, sizeof(*level));
    level->width = LEVEL_SOURCE_COLS;
    level->height = LEVEL_SOURCE_ROWS;

    for (uint16_t i = 0; i < sizeof(level_1_2_underground_entries) /
             sizeof(level_1_2_underground_entries[0]); ++i) {
        const SourceEntry *entry = &level_1_2_underground_entries[i];

        level->source[entry->y][entry->x] = entry->value;
    }

    for (uint16_t i = 0; i < sizeof(level_1_2_underground_runs) /
             sizeof(level_1_2_underground_runs[0]); ++i) {
        const SourceRun *run = &level_1_2_underground_runs[i];

        for (uint16_t x = run->x0; x <= run->x1; ++x) {
            level->source[run->y][x] = run->value;
        }
    }

    convert_source_to_metatiles(level);
    level_force_stream_update();
}

void level_stream_bg0(const Level *level, int camera_x_px, int camera_y_px)
{
    int first_world_tile_x = floor_div_int(camera_x_px + LEVEL_STREAM_LEFT_PX, 8);
    int first_world_tile_y = LEVEL_STREAM_TOP_PX / 8;

    if (stream_dirty) {
        stream_fill_window(level, first_world_tile_x, first_world_tile_y);
    } else {
        int delta = first_world_tile_x - stream_first_world_tile_x;

        if (delta <= -LEVEL_STREAM_TILE_COLS || delta >= LEVEL_STREAM_TILE_COLS) {
            stream_fill_window(level, first_world_tile_x, first_world_tile_y);
        } else if (delta > 0) {
            int old_first_world_tile_x = stream_first_world_tile_x;

            for (int i = 0; i < delta; ++i) {
                uint8_t map_x = wrapped_map_col(stream_map_origin_col + LEVEL_STREAM_TILE_COLS + i);
                stream_draw_column(level, map_x, old_first_world_tile_x + LEVEL_STREAM_TILE_COLS + i,
                                   first_world_tile_y);
            }

            stream_map_origin_col = wrapped_map_col(stream_map_origin_col + delta);
            stream_first_world_tile_x = first_world_tile_x;
        } else if (delta < 0) {
            for (int i = delta; i < 0; ++i) {
                uint8_t map_x = wrapped_map_col(stream_map_origin_col + i);
                stream_draw_column(level, map_x, stream_first_world_tile_x + i,
                                   first_world_tile_y);
            }

            stream_map_origin_col = wrapped_map_col(stream_map_origin_col + delta);
            stream_first_world_tile_x = first_world_tile_x;
        }
    }

    REG_BG0HOFS = (uint16_t)((stream_map_origin_col * 8 +
                              camera_x_px - stream_first_world_tile_x * 8) & 511);
    REG_BG0VOFS = (uint16_t)((camera_y_px - stream_first_world_tile_y * 8) & 255);
}

void level_force_stream_update(void)
{
    stream_dirty = 1;
}

uint8_t level_metatile_at(const Level *level, int world_x_px, int world_y_px)
{
    int source_x = floor_div_int(world_x_px, LEVEL_METATILE_SIZE);
    int source_y = floor_div_int(world_y_px, LEVEL_METATILE_SIZE) + LEVEL_VIEW_SOURCE_ROW_OFFSET;

    if (source_x < 0 || source_x >= level->width ||
        source_y < 0 || source_y >= level->height) {
        return METATILE_EMPTY;
    }

    return level->metatiles[source_y][source_x];
}

LevelCollision level_collision_at(const Level *level, int world_x_px, int world_y_px)
{
    switch (level_metatile_at(level, world_x_px, world_y_px)) {
    case METATILE_BRICK:
    case METATILE_QUESTION:
    case METATILE_SOLID:
    case METATILE_GROUND_TOP:
    case METATILE_GROUND_DIRT:
    case METATILE_PIPE_TOP_LEFT:
    case METATILE_PIPE_TOP_RIGHT:
    case METATILE_PIPE_BODY_LEFT:
    case METATILE_PIPE_BODY_RIGHT:
        return LEVEL_COLLISION_SOLID;
    case METATILE_SPIKES:
        return LEVEL_COLLISION_DEATH;
    default:
        return LEVEL_COLLISION_EMPTY;
    }
}

void level_set_metatile_cell(Level *level, uint16_t source_x, uint16_t source_y, uint8_t metatile)
{
    level_set_metatile_cell_palette(level, source_x, source_y, metatile, 0);
}

void level_set_metatile_cell_palette(Level *level, uint16_t source_x, uint16_t source_y,
                                     uint8_t metatile, uint8_t palette)
{
    if (source_x >= level->width || source_y >= level->height) {
        return;
    }

    level->metatiles[source_y][source_x] = metatile;
    level->palettes[source_y][source_x] = palette & 15;
    level_force_stream_update();
}

void level_restore_source_cell(Level *level, uint16_t source_x, uint16_t source_y)
{
    if (source_x >= level->width || source_y >= level->height) {
        return;
    }

    uint8_t source = level->source[source_y][source_x];
    level->metatiles[source_y][source_x] = source_to_metatile(source);
    level->palettes[source_y][source_x] = source_to_palette(source);
    level_force_stream_update();
}

int level_death_y_px(const Level *level)
{
    (void)level;
    return (13 - LEVEL_VIEW_SOURCE_ROW_OFFSET + 5) * LEVEL_METATILE_SIZE;
}

int level_camera_max_y_px(const Level *level)
{
    int bottom_source_y = LEVEL_VIEW_SOURCE_ROW_OFFSET +
                          LEVEL_SCREEN_METATILE_ROWS - 1;

    for (uint16_t y = 0; y < level->height; ++y) {
        for (uint16_t x = 0; x < level->width; ++x) {
            if (level->metatiles[y][x] != METATILE_EMPTY &&
                (int)y > bottom_source_y) {
                bottom_source_y = y;
            }
        }
    }

    int bottom_world_y = (bottom_source_y - LEVEL_VIEW_SOURCE_ROW_OFFSET + 1) *
                         LEVEL_METATILE_SIZE;
    int viewport_height = LEVEL_SCREEN_METATILE_ROWS * LEVEL_METATILE_SIZE;
    int max_camera_y = bottom_world_y - viewport_height;

    return max_camera_y > 0 ? max_camera_y : 0;
}
