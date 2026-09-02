#include "level.h"

#include <gba.h>
#include <string.h>

#include "generated/level1_data.h"
#include "generated/level1_2_data.h"
#include "generated/level1_2b_data.h"
#include "generated/level1_2u_data.h"
#include "tiles_16.h"

#define BG_CHARBLOCK 0
#define BG_SCREENBLOCK 28
#define BG_MAP_WIDTH_TILES 64
#define BG_MAP_HEIGHT_TILES 32

#define TILE_EMPTY 0
#define BG_SE_PALBANK(bank) ((uint16_t)((bank) << 12))
#define RGB15(r, g, b) ((uint16_t)((r) | ((g) << 5) | ((b) << 10)))

Level level_current;

static int stream_first_world_tile_x;
static int stream_first_world_tile_y;
static uint8_t stream_map_origin_col;
static uint8_t stream_map_origin_row;
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
    for (uint8_t row = 0; row < BG_MAP_HEIGHT_TILES; ++row) {
        uint8_t map_y = (uint8_t)((stream_map_origin_row + row) &
                                  (BG_MAP_HEIGHT_TILES - 1));

        *bg0_map_entry(map_x, map_y) =
            tile_id_at_world_tile(level, world_tile_x, first_world_tile_y + row);
    }
}

static void stream_draw_row(const Level *level, uint8_t map_y, int first_world_tile_x,
                            int world_tile_y)
{
    for (uint8_t col = 0; col < BG_MAP_WIDTH_TILES; ++col) {
        uint8_t map_x = (uint8_t)((stream_map_origin_col + col) &
                                  (BG_MAP_WIDTH_TILES - 1));

        *bg0_map_entry(map_x, map_y) = (col < LEVEL_STREAM_TILE_COLS)
            ? tile_id_at_world_tile(level, first_world_tile_x + col, world_tile_y)
            : TILE_EMPTY;
    }
}

static void stream_fill_window(const Level *level, int first_world_tile_x,
                               int first_world_tile_y)
{
    stream_map_origin_col = 0;
    stream_map_origin_row = 0;

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

static uint8_t wrapped_map_row(int map_y)
{
    return (uint8_t)(map_y & (BG_MAP_HEIGHT_TILES - 1));
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

static void copy_generated_level_data(Level *level,
                                      const uint8_t *source,
                                      const uint8_t *metatiles,
                                      const uint8_t *palettes,
                                      uint16_t width,
                                      uint16_t height)
{
    for (uint16_t y = 0; y < height; ++y) {
        memcpy(level->source[y], source + y * width, width);
        memcpy(level->metatiles[y], metatiles + y * width, width);
        memcpy(level->palettes[y], palettes + y * width, width);
    }
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
    stream_map_origin_row = 0;
    stream_dirty = 1;
}

void level_load_1_1(Level *level)
{
    memset(level, 0, sizeof(*level));
    level->width = LEVEL1_WIDTH;
    level->height = LEVEL1_HEIGHT;
    level->camera_margin_top = LEVEL1_CAMERA_MARGIN_TOP;
    level->camera_margin_bottom = LEVEL1_CAMERA_MARGIN_BOTTOM;
    copy_generated_level_data(level, (const uint8_t *)level1_source,
                              (const uint8_t *)level1_metatiles,
                              (const uint8_t *)level1_palettes,
                              LEVEL1_WIDTH, LEVEL1_HEIGHT);
    level_force_stream_update();
}

void level_load_1_2(Level *level)
{
    memset(level, 0, sizeof(*level));
    level->width = LEVEL1_2_WIDTH;
    level->height = LEVEL1_2_HEIGHT;
    level->camera_margin_top = LEVEL1_2_CAMERA_MARGIN_TOP;
    level->camera_margin_bottom = LEVEL1_2_CAMERA_MARGIN_BOTTOM;
    copy_generated_level_data(level, (const uint8_t *)level1_2_source,
                              (const uint8_t *)level1_2_metatiles,
                              (const uint8_t *)level1_2_palettes,
                              LEVEL1_2_WIDTH, LEVEL1_2_HEIGHT);
    level_force_stream_update();
}

void level_load_1_2_underground(Level *level)
{
    memset(level, 0, sizeof(*level));
    level->width = LEVEL1_2U_WIDTH;
    level->height = LEVEL1_2U_HEIGHT;
    level->camera_margin_top = LEVEL1_2U_CAMERA_MARGIN_TOP;
    level->camera_margin_bottom = LEVEL1_2U_CAMERA_MARGIN_BOTTOM;
    copy_generated_level_data(level, (const uint8_t *)level1_2u_source,
                              (const uint8_t *)level1_2u_metatiles,
                              (const uint8_t *)level1_2u_palettes,
                              LEVEL1_2U_WIDTH, LEVEL1_2U_HEIGHT);
    level_force_stream_update();
}

void level_load_1_2b(Level *level)
{
    memset(level, 0, sizeof(*level));
    level->width = LEVEL1_2B_WIDTH;
    level->height = LEVEL1_2B_HEIGHT;
    level->camera_margin_top = LEVEL1_2B_CAMERA_MARGIN_TOP;
    level->camera_margin_bottom = LEVEL1_2B_CAMERA_MARGIN_BOTTOM;
    copy_generated_level_data(level, (const uint8_t *)level1_2b_source,
                              (const uint8_t *)level1_2b_metatiles,
                              (const uint8_t *)level1_2b_palettes,
                              LEVEL1_2B_WIDTH, LEVEL1_2B_HEIGHT);
    level_force_stream_update();
}

void level_stream_bg0(const Level *level, int camera_x_px, int camera_y_px)
{
    int first_world_tile_x = floor_div_int(camera_x_px + LEVEL_STREAM_LEFT_PX, 8);
    int first_world_tile_y = floor_div_int(camera_y_px + LEVEL_STREAM_TOP_PX, 8);

    if (stream_dirty) {
        stream_fill_window(level, first_world_tile_x, first_world_tile_y);
    } else {
        int delta_x = first_world_tile_x - stream_first_world_tile_x;
        int delta_y = first_world_tile_y - stream_first_world_tile_y;

        if (delta_x <= -LEVEL_STREAM_TILE_COLS ||
            delta_x >= LEVEL_STREAM_TILE_COLS ||
            delta_y <= -BG_MAP_HEIGHT_TILES ||
            delta_y >= BG_MAP_HEIGHT_TILES) {
            stream_fill_window(level, first_world_tile_x, first_world_tile_y);
        } else {
            int old_first_world_tile_x = stream_first_world_tile_x;
            int old_first_world_tile_y = stream_first_world_tile_y;

            if (delta_x > 0) {
                for (int i = 0; i < delta_x; ++i) {
                    uint8_t map_x = wrapped_map_col(stream_map_origin_col +
                                                    LEVEL_STREAM_TILE_COLS + i);
                    stream_draw_column(level, map_x,
                                       old_first_world_tile_x +
                                       LEVEL_STREAM_TILE_COLS + i,
                                       old_first_world_tile_y);
                }

                stream_map_origin_col = wrapped_map_col(stream_map_origin_col + delta_x);
                stream_first_world_tile_x = first_world_tile_x;
            } else if (delta_x < 0) {
                for (int i = delta_x; i < 0; ++i) {
                    uint8_t map_x = wrapped_map_col(stream_map_origin_col + i);
                    stream_draw_column(level, map_x,
                                       old_first_world_tile_x + i,
                                       old_first_world_tile_y);
                }

                stream_map_origin_col = wrapped_map_col(stream_map_origin_col + delta_x);
                stream_first_world_tile_x = first_world_tile_x;
            }

            if (delta_y > 0) {
                for (int i = 0; i < delta_y; ++i) {
                    uint8_t map_y = wrapped_map_row(stream_map_origin_row +
                                                    BG_MAP_HEIGHT_TILES + i);
                    stream_draw_row(level, map_y,
                                    stream_first_world_tile_x,
                                    old_first_world_tile_y +
                                    BG_MAP_HEIGHT_TILES + i);
                }

                stream_map_origin_row = wrapped_map_row(stream_map_origin_row + delta_y);
                stream_first_world_tile_y = first_world_tile_y;
            } else if (delta_y < 0) {
                for (int i = delta_y; i < 0; ++i) {
                    uint8_t map_y = wrapped_map_row(stream_map_origin_row + i);
                    stream_draw_row(level, map_y,
                                    stream_first_world_tile_x,
                                    old_first_world_tile_y + i);
                }

                stream_map_origin_row = wrapped_map_row(stream_map_origin_row + delta_y);
                stream_first_world_tile_y = first_world_tile_y;
            }
        }
    }

    REG_BG0HOFS = (uint16_t)((stream_map_origin_col * 8 +
                              camera_x_px - stream_first_world_tile_x * 8) & 511);
    REG_BG0VOFS = (uint16_t)((stream_map_origin_row * 8 +
                              camera_y_px - stream_first_world_tile_y * 8) & 255);
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
    int bottom_world_y = ((int)level->height - LEVEL_VIEW_SOURCE_ROW_OFFSET) *
                         LEVEL_METATILE_SIZE;
    int viewport_height = LEVEL_SCREEN_METATILE_ROWS * LEVEL_METATILE_SIZE;
    int min_camera_y = level_camera_min_y_px(level);
    int max_camera_y = bottom_world_y - viewport_height -
                       (int)level->camera_margin_bottom;

    return max_camera_y > min_camera_y ? max_camera_y : min_camera_y;
}

int level_camera_min_y_px(const Level *level)
{
    int top_world_y = -LEVEL_VIEW_SOURCE_ROW_OFFSET * LEVEL_METATILE_SIZE;
    int min_camera_y = top_world_y + (int)level->camera_margin_top;

    return min_camera_y < 0 ? min_camera_y : 0;
}
