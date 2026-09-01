#include "messages.h"

#include <gba.h>
#include <string.h>

#include "camera.h"
#include "font4x6.h"
#include "text.h"

#define MESSAGE_TILE_BASE 512
#define MESSAGE_BLANK_TILE MESSAGE_TILE_BASE
#define MESSAGE_BOX_TILE (MESSAGE_TILE_BASE + 1)
#define MESSAGE_GLYPH_TILE_BASE (MESSAGE_TILE_BASE + 2)
#define MESSAGE_MAX_LINES 7
#define MESSAGE_MAX_LINE_CHARS 30
#define MESSAGE_MAX_TILES ((MESSAGE_MAX_LINE_CHARS + 1) / 2 * MESSAGE_MAX_LINES)
#define MESSAGE_SCREENBLOCK 30
#define MESSAGE_COLOR_TEXT 15
#define MESSAGE_COLOR_BOX 14
#define MESSAGE_PALBANK 15
#define MESSAGE_SE_PALBANK ((uint16_t)(MESSAGE_PALBANK << 12))

typedef struct ActiveMessage {
    const char *text;
    fix16_t x;
    fix16_t y;
    uint8_t timer;
    uint8_t line_count;
    uint8_t line_len[MESSAGE_MAX_LINES];
    uint16_t line_start[MESSAGE_MAX_LINES];
    uint8_t line_tile_offset[MESSAGE_MAX_LINES];
    uint8_t old_line_count;
    uint8_t old_len[MESSAGE_MAX_LINES];
    uint8_t old_x[MESSAGE_MAX_LINES];
    uint8_t old_y[MESSAGE_MAX_LINES];
    uint8_t old_box_x;
    uint8_t old_box_y;
    uint8_t old_box_w;
    uint8_t old_box_h;
    uint8_t needs_redraw;
    uint8_t modal;
} ActiveMessage;

static ActiveMessage active_message;
static uint8_t taunt_index;

static const char *const enemy_taunts[] = {
    TXT_ENEMY_YAHOO,
    TXT_ENEMY_I_WON,
    TXT_ENEMY_DEATH_PLACE,
    TXT_ENEMY_NEVER_AGAIN,
    TXT_ENEMY_STRONGEST,
    TXT_ENEMY_COME_BACK,
    TXT_ENEMY_NO_RETREAT,
    TXT_ENEMY_HAHA,
};

static const char *message_text(MessageId id)
{
    switch (id) {
    case MESSAGE_PLAYER_TASTY:
        return TXT_MSG_TASTY;
    case MESSAGE_PLAYER_NOT_POISON:
        return TXT_MSG_NOT_POISON;
    case MESSAGE_PLAYER_BAD_MUSHROOM:
        return TXT_MSG_BAD_MUSHROOM;
    case MESSAGE_PLAYER_STABBED:
        return TXT_MSG_STABBED;
    case MESSAGE_ENEMY_SLEEP:
        return TXT_ENEMY_SLEEP;
    case MESSAGE_ENEMY_DELISH:
        return TXT_ENEMY_DELISH;
    case MESSAGE_ENEMY_BLOCK:
        return TXT_ENEMY_BLOCK;
    case MESSAGE_STAGE_CLEAR:
        return TXT_STAGE_CLEAR;
    case MESSAGE_HINT_STAGE_1:
        return TXT_HINT_STAGE_1;
    case MESSAGE_ENEMY_TAUNT: {
        const char *text = enemy_taunts[taunt_index];
        taunt_index = (uint8_t)((taunt_index + 1) %
                                (sizeof(enemy_taunts) / sizeof(enemy_taunts[0])));
        return text;
    }
    default:
        return "";
    }
}

static void clear_old_message(void)
{
    uint16_t *map = (uint16_t *)SCREEN_BASE_BLOCK(MESSAGE_SCREENBLOCK);

    if (active_message.old_box_w > 0) {
        for (uint8_t y = 0; y < active_message.old_box_h; ++y) {
            for (uint8_t x = 0; x < active_message.old_box_w; ++x) {
                map[(active_message.old_box_y + y) * 32 +
                    ((active_message.old_box_x + x) & 31)] = MESSAGE_BLANK_TILE;
            }
        }
    } else {
        for (uint8_t line = 0; line < active_message.old_line_count; ++line) {
            for (uint8_t i = 0; i < active_message.old_len[line]; ++i) {
                map[active_message.old_y[line] * 32 +
                    ((active_message.old_x[line] + i) & 31)] = MESSAGE_BLANK_TILE;
            }
        }
    }

    active_message.old_line_count = 0;
    active_message.old_box_w = 0;
    active_message.old_box_h = 0;
}

static void draw_glyph_row(uint32_t *row, char ch, uint8_t y, uint8_t x_base)
{
    uint8_t bits = y < FONT4X6_HEIGHT ? font4x6_row(ch, y) : 0;

    for (uint8_t x = 0; x < FONT4X6_WIDTH; ++x) {
        if (bits & (1 << (FONT4X6_WIDTH - 1 - x))) {
            *row |= (uint32_t)MESSAGE_COLOR_TEXT << ((x_base + x) * 4);
        }
    }
}

static void build_text_tile(uint8_t tile_offset, const char *text, uint8_t char_index,
                            uint8_t line_len, uint8_t fill_background)
{
    uint32_t *tile = (uint32_t *)CHAR_BASE_BLOCK(0) +
                     (MESSAGE_GLYPH_TILE_BASE + tile_offset) * 8;

    for (uint8_t y = 0; y < 8; ++y) {
        uint32_t row = fill_background ?
            (uint32_t)MESSAGE_COLOR_BOX * 0x11111111u : 0;

        if (char_index < line_len && text[char_index] != '\0') {
            draw_glyph_row(&row, text[char_index], y, 0);
        }
        if ((uint8_t)(char_index + 1) < line_len && text[char_index + 1] != '\0') {
            draw_glyph_row(&row, text[char_index + 1], y, 4);
        }

        tile[y] = row;
    }
}

void messages_init(void)
{
    memset(&active_message, 0, sizeof(active_message));
    memset(&((uint32_t *)CHAR_BASE_BLOCK(0))[MESSAGE_BLANK_TILE * 8],
           0, 8 * sizeof(uint32_t));

    uint32_t *box_tile = (uint32_t *)CHAR_BASE_BLOCK(0) + MESSAGE_BOX_TILE * 8;
    for (uint8_t y = 0; y < 8; ++y) {
        box_tile[y] = 0xEEEEEEEE;
    }

    uint16_t *map = (uint16_t *)SCREEN_BASE_BLOCK(MESSAGE_SCREENBLOCK);
    for (uint16_t i = 0; i < 32 * 32; ++i) {
        map[i] = MESSAGE_BLANK_TILE;
    }

    REG_BG1CNT = BG_16_COLOR | CHAR_BASE(0) | SCREEN_BASE(MESSAGE_SCREENBLOCK) |
                 BG_PRIORITY(0) | TEXTBG_SIZE_256x256;
    REG_BG1HOFS = 0;
    REG_BG1VOFS = 0;
    BG_PALETTE[MESSAGE_PALBANK * 16 + MESSAGE_COLOR_BOX] = RGB5(0, 0, 0);
    BG_PALETTE[MESSAGE_PALBANK * 16 + MESSAGE_COLOR_TEXT] = RGB5(31, 31, 31);
}

void messages_update(void)
{
    if (!active_message.modal && active_message.timer > 0) {
        active_message.timer--;
    }
}

void messages_show(MessageId id, fix16_t x, fix16_t y, uint8_t frames)
{
    messages_show_text(message_text(id), x, y, frames);
}

static void messages_show_text_mode(const char *text, fix16_t x, fix16_t y,
                                    uint8_t frames, uint8_t modal)
{
    if (!text) {
        text = "";
    }

    active_message.text = text;
    active_message.x = x;
    active_message.y = y;
    active_message.timer = frames;
    active_message.modal = modal;
    active_message.line_count = 0;
    memset(active_message.line_len, 0, sizeof(active_message.line_len));
    memset(active_message.line_start, 0, sizeof(active_message.line_start));
    memset(active_message.line_tile_offset, 0, sizeof(active_message.line_tile_offset));

    uint8_t line = 0;
    uint8_t line_len = 0;
    uint16_t line_start = 0;
    uint8_t tile_offset = 0;

    for (uint16_t i = 0; active_message.text[i] != '\0' && line < MESSAGE_MAX_LINES; ++i) {
        char ch = active_message.text[i];

        if (ch == '\r') {
            continue;
        }

        if (ch == '\n') {
            active_message.line_start[line] = line_start;
            active_message.line_len[line] = line_len;
            active_message.line_tile_offset[line] = tile_offset;
            tile_offset += (uint8_t)((line_len + 1) / 2);
            line++;
            active_message.line_count = line;
            line_start = (uint16_t)(i + 1);
            line_len = 0;
            continue;
        }

        if (line_len < MESSAGE_MAX_LINE_CHARS) {
            line_len++;
        }
    }

    if (line < MESSAGE_MAX_LINES &&
        (line_len > 0 || active_message.line_count == 0)) {
        active_message.line_start[line] = line_start;
        active_message.line_len[line] = line_len;
        active_message.line_tile_offset[line] = tile_offset;
        line++;
        active_message.line_count = line;
    }

    for (uint8_t line_i = 0; line_i < active_message.line_count; ++line_i) {
        const char *line_text =
            &active_message.text[active_message.line_start[line_i]];
        uint8_t tile_base = active_message.line_tile_offset[line_i];
        uint8_t tile_len = (uint8_t)((active_message.line_len[line_i] + 1) / 2);

        for (uint8_t tile = 0; tile < tile_len && tile_base + tile < MESSAGE_MAX_TILES;
             ++tile) {
            build_text_tile((uint8_t)(tile_base + tile), line_text,
                            (uint8_t)(tile * 2),
                            active_message.line_len[line_i],
                            active_message.modal);
        }
    }

    active_message.needs_redraw = 1;
}

void messages_show_text(const char *text, fix16_t x, fix16_t y, uint8_t frames)
{
    messages_show_text_mode(text, x, y, frames, 0);
}

void messages_show_modal_text(const char *text, fix16_t x, fix16_t y)
{
    messages_show_text_mode(text, x, y, 1, 1);
}

uint8_t messages_is_modal(void)
{
    return active_message.modal && active_message.text &&
           active_message.text[0] != '\0';
}

void messages_dismiss(void)
{
    active_message.timer = 0;
    active_message.modal = 0;
    active_message.text = "";
    active_message.needs_redraw = 1;
}

void messages_draw(const struct Camera *camera)
{
    if ((!active_message.modal && active_message.timer == 0) || !active_message.text ||
        active_message.text[0] == '\0') {
        if (active_message.old_line_count > 0 || active_message.old_box_w > 0) {
            clear_old_message();
        }
        return;
    }

    int screen_x = camera_world_to_screen_x(camera, active_message.x);
    int screen_y = camera_world_to_screen_y(camera, active_message.y) - 10;
    uint8_t new_len[MESSAGE_MAX_LINES];
    uint8_t new_x[MESSAGE_MAX_LINES];
    uint8_t new_y[MESSAGE_MAX_LINES];
    uint8_t box_x = 0;
    uint8_t box_y = 0;
    uint8_t box_w = 0;
    uint8_t box_h = 0;
    uint8_t have_box = 0;

    for (uint8_t line = 0; line < active_message.line_count; ++line) {
        int tile_x = screen_x / 8;
        int tile_y = screen_y / 8 + line;
        uint8_t tile_len = (uint8_t)((active_message.line_len[line] + 1) / 2);

        if (tile_x < 0) {
            tile_x = 0;
        }
        if (tile_x + tile_len > 30) {
            tile_x = 30 - tile_len;
        }
        if (tile_y < 0) {
            tile_y = 0;
        }
        if (tile_y > 19) {
            tile_y = 19;
        }

        new_len[line] = tile_len;
        new_x[line] = (uint8_t)tile_x;
        new_y[line] = (uint8_t)tile_y;

        if (tile_len > 0) {
            uint8_t right = (uint8_t)(tile_x + tile_len);
            if (!have_box || (uint8_t)tile_x < box_x) {
                box_x = (uint8_t)tile_x;
            }
            if (!have_box || (uint8_t)tile_y < box_y) {
                box_y = (uint8_t)tile_y;
            }
            if (right > box_w) {
                box_w = right;
            }
            if ((uint8_t)(tile_y + 1) > box_h) {
                box_h = (uint8_t)(tile_y + 1);
            }
            have_box = 1;
        }
    }

    if (box_w > box_x) {
        box_w = (uint8_t)(box_w - box_x);
    }
    if (box_h > box_y) {
        box_h = (uint8_t)(box_h - box_y);
    }
    if (active_message.modal && box_w > 0 && box_h > 0) {
        if (box_x > 0) {
            box_x--;
            box_w++;
        }
        if (box_x + box_w < 30) {
            box_w++;
        }
        if (box_y > 0) {
            box_y--;
            box_h++;
        }
        if (box_y + box_h < 20) {
            box_h++;
        }
    }

    if (!active_message.needs_redraw &&
        active_message.old_line_count == active_message.line_count &&
        active_message.old_box_x == box_x &&
        active_message.old_box_y == box_y &&
        active_message.old_box_w == box_w &&
        active_message.old_box_h == box_h) {
        uint8_t unchanged = 1;
        for (uint8_t line = 0; line < active_message.line_count; ++line) {
            if (active_message.old_len[line] != new_len[line] ||
                active_message.old_x[line] != new_x[line] ||
                active_message.old_y[line] != new_y[line]) {
                unchanged = 0;
                break;
            }
        }
        if (unchanged) {
            return;
        }
    }

    if (active_message.old_line_count > 0 || active_message.old_box_w > 0) {
        clear_old_message();
    }

    uint16_t *map = (uint16_t *)SCREEN_BASE_BLOCK(MESSAGE_SCREENBLOCK);
    if (active_message.modal) {
        for (uint8_t y = 0; y < box_h; ++y) {
            for (uint8_t x = 0; x < box_w; ++x) {
                map[(box_y + y) * 32 + ((box_x + x) & 31)] =
                    MESSAGE_BOX_TILE | MESSAGE_SE_PALBANK;
            }
        }
        active_message.old_box_x = box_x;
        active_message.old_box_y = box_y;
        active_message.old_box_w = box_w;
        active_message.old_box_h = box_h;
    }

    for (uint8_t line = 0; line < active_message.line_count; ++line) {
        uint8_t tile_base = active_message.line_tile_offset[line];
        uint16_t palette = active_message.modal ? MESSAGE_SE_PALBANK : 0;

        for (uint8_t i = 0; i < new_len[line]; ++i) {
            map[new_y[line] * 32 + new_x[line] + i] =
                (uint16_t)(MESSAGE_GLYPH_TILE_BASE + tile_base + i) | palette;
        }

        active_message.old_len[line] = new_len[line];
        active_message.old_x[line] = new_x[line];
        active_message.old_y[line] = new_y[line];
    }

    active_message.old_line_count = active_message.line_count;
    active_message.needs_redraw = 0;
}
