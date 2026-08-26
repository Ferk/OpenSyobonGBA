#include "messages.h"

#include <gba.h>
#include <string.h>

#include "camera.h"
#include "font4x6.h"
#include "text.h"

#define MESSAGE_TILE_BASE 512
#define MESSAGE_BLANK_TILE MESSAGE_TILE_BASE
#define MESSAGE_GLYPH_TILE_BASE (MESSAGE_TILE_BASE + 1)
#define MESSAGE_MAX_CHARS 30
#define MESSAGE_SCREENBLOCK 30
#define MESSAGE_COLOR_TEXT 15

typedef struct ActiveMessage {
    const char *text;
    fix16_t x;
    fix16_t y;
    uint8_t timer;
    uint8_t len;
    uint8_t old_len;
    uint8_t old_x;
    uint8_t old_y;
    uint8_t needs_redraw;
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
    case MESSAGE_PLAYER_BAD_MUSHROOM:
        return TXT_MSG_BAD_MUSHROOM;
    case MESSAGE_PLAYER_STABBED:
        return TXT_MSG_STABBED;
    case MESSAGE_ENEMY_SLEEP:
        return TXT_ENEMY_SLEEP;
    case MESSAGE_ENEMY_DELISH:
        return TXT_ENEMY_DELISH;
    case MESSAGE_STAGE_CLEAR:
        return TXT_STAGE_CLEAR;
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

    for (uint8_t i = 0; i < active_message.old_len; ++i) {
        map[active_message.old_y * 32 + ((active_message.old_x + i) & 31)] =
            MESSAGE_BLANK_TILE;
    }
    active_message.old_len = 0;
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

static void build_text_tile(uint8_t tile_offset, const char *text, uint8_t char_index)
{
    uint32_t *tile = (uint32_t *)CHAR_BASE_BLOCK(0) +
                     (MESSAGE_GLYPH_TILE_BASE + tile_offset) * 8;

    for (uint8_t y = 0; y < 8; ++y) {
        uint32_t row = 0;

        if (text[char_index] != '\0') {
            draw_glyph_row(&row, text[char_index], y, 0);
        }
        if (text[char_index + 1] != '\0') {
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

    uint16_t *map = (uint16_t *)SCREEN_BASE_BLOCK(MESSAGE_SCREENBLOCK);
    for (uint16_t i = 0; i < 32 * 32; ++i) {
        map[i] = MESSAGE_BLANK_TILE;
    }

    REG_BG1CNT = BG_16_COLOR | CHAR_BASE(0) | SCREEN_BASE(MESSAGE_SCREENBLOCK) |
                 BG_PRIORITY(0) | TEXTBG_SIZE_256x256;
    REG_BG1HOFS = 0;
    REG_BG1VOFS = 0;
}

void messages_update(void)
{
    if (active_message.timer > 0) {
        active_message.timer--;
    }
}

void messages_show(MessageId id, fix16_t x, fix16_t y, uint8_t frames)
{
    active_message.text = message_text(id);
    active_message.x = x;
    active_message.y = y;
    active_message.timer = frames;
    active_message.len = 0;

    while (active_message.text[active_message.len] != '\0' &&
           active_message.len < MESSAGE_MAX_CHARS) {
        active_message.len++;
    }

    for (uint8_t tile = 0; tile < (active_message.len + 1) / 2; ++tile) {
        build_text_tile(tile, active_message.text, (uint8_t)(tile * 2));
    }

    active_message.needs_redraw = 1;
}

void messages_draw(const struct Camera *camera)
{
    if (active_message.timer == 0 || !active_message.text ||
        active_message.text[0] == '\0') {
        if (active_message.old_len > 0) {
            clear_old_message();
        }
        return;
    }

    int screen_x = camera_world_to_screen_x(camera, active_message.x);
    int screen_y = camera_world_to_screen_y(camera, active_message.y) - 10;
    int tile_x = screen_x / 8;
    int tile_y = screen_y / 8;
    uint8_t tile_len = (uint8_t)((active_message.len + 1) / 2);

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

    if (!active_message.needs_redraw &&
        active_message.old_len == tile_len &&
        active_message.old_x == (uint8_t)tile_x &&
        active_message.old_y == (uint8_t)tile_y) {
        return;
    }

    if (active_message.old_len > 0) {
        clear_old_message();
    }

    uint16_t *map = (uint16_t *)SCREEN_BASE_BLOCK(MESSAGE_SCREENBLOCK);
    for (uint8_t i = 0; i < tile_len; ++i) {
        map[tile_y * 32 + tile_x + i] = (uint16_t)(MESSAGE_GLYPH_TILE_BASE + i);
    }

    active_message.old_len = tile_len;
    active_message.old_x = (uint8_t)tile_x;
    active_message.old_y = (uint8_t)tile_y;
    active_message.needs_redraw = 0;
}
