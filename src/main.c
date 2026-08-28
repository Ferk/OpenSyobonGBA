#include <gba.h>
#include <stdint.h>
#include <string.h>

#include "audio.h"
#include "camera.h"
#include "enemy.h"
#include "font4x6.h"
#include "level.h"
#include "messages.h"
#include "player.h"
#include "text.h"
#include "traps.h"

#define RESTART_SCREEN_FRAMES 30
#define LIFE_TEXT_TILE_BASE 512
#define LIFE_TEXT_BLANK_TILE LIFE_TEXT_TILE_BASE
#define LIFE_TEXT_GLYPH_TILE_BASE (LIFE_TEXT_TILE_BASE + 1)
#define LIFE_TEXT_COLOR 15
#define PLAYER_SPRITE_TILE 32
#define PLAYER_SPRITE_PALETTE 2

typedef enum GameState {
    GAME_STATE_LIVES = 0,
    GAME_STATE_PLAYING,
} GameState;

static void clear_oam(void)
{
    for (uint16_t i = 0; i < 128; ++i) {
        OAM[i].attr0 = ATTR0_DISABLED;
        OAM[i].attr1 = 0;
        OAM[i].attr2 = 0;
    }
}

static void append_int(char *text, int value)
{
    char digits[8];
    uint8_t pos = 0;
    uint8_t out = 0;
    unsigned int n;

    while (text[out] != '\0') {
        out++;
    }

    if (value < 0) {
        text[out++] = '-';
        n = (unsigned int)-value;
    } else {
        n = (unsigned int)value;
    }

    do {
        digits[pos++] = (char)('0' + n % 10);
        n /= 10;
    } while (n > 0 && pos < sizeof(digits));

    while (pos > 0) {
        text[out++] = digits[--pos];
    }
    text[out] = '\0';
}

static void draw_life_text_tile(uint8_t tile_offset, const char *text,
                                uint8_t char_index)
{
    uint32_t *tile = (uint32_t *)CHAR_BASE_BLOCK(0) +
                     (LIFE_TEXT_GLYPH_TILE_BASE + tile_offset) * 8;

    for (uint8_t y = 0; y < 8; ++y) {
        uint32_t row = 0;

        for (uint8_t slot = 0; slot < 2; ++slot) {
            char ch = text[char_index + slot];
            uint8_t bits = y < FONT4X6_HEIGHT ? font4x6_row(ch, y) : 0;

            for (uint8_t x = 0; x < FONT4X6_WIDTH; ++x) {
                if (bits & (1 << (FONT4X6_WIDTH - 1 - x))) {
                    row |= (uint32_t)LIFE_TEXT_COLOR << ((slot * 4 + x) * 4);
                }
            }
        }

        tile[y] = row;
    }
}

static void draw_lives_screen(int lives)
{
    char text[16] = "X ";
    uint8_t len = 0;
    uint8_t tile_len;
    uint16_t *map = (uint16_t *)SCREEN_BASE_BLOCK(30);
    uint16_t blank_entry = LIFE_TEXT_BLANK_TILE;

    append_int(text, lives);
    BG_PALETTE[0] = RGB5(0, 0, 0);
    BG_PALETTE[LIFE_TEXT_COLOR] = RGB5(31, 31, 31);

    while (text[len] != '\0') {
        len++;
    }
    tile_len = (uint8_t)((len + 1) / 2);

    memset(&((uint32_t *)CHAR_BASE_BLOCK(0))[LIFE_TEXT_BLANK_TILE * 8],
           0, 8 * sizeof(uint32_t));
    for (uint16_t i = 0; i < 32 * 32; ++i) {
        map[i] = blank_entry;
    }
    for (uint8_t tile = 0; tile < tile_len; ++tile) {
        draw_life_text_tile(tile, text, (uint8_t)(tile * 2));
        map[10 * 32 + 16 + tile] = (uint16_t)(LIFE_TEXT_GLYPH_TILE_BASE + tile);
    }

    REG_BG1HOFS = 0;
    REG_BG1VOFS = 0;
    REG_BG0HOFS = 0;
    REG_BG0VOFS = 0;
    REG_DISPCNT = MODE_0 | BG1_ON | OBJ_ON | OBJ_1D_MAP;
    clear_oam();
    OAM[0].attr0 = (uint16_t)(76 | ATTR0_COLOR_16 | ATTR0_SQUARE);
    OAM[0].attr1 = (uint16_t)(96 | ATTR1_SIZE_16);
    OAM[0].attr2 = (uint16_t)(OBJ_CHAR(PLAYER_SPRITE_TILE) |
                              ATTR2_PALETTE(PLAYER_SPRITE_PALETTE));
}

static void start_stage(Player *player, Camera *camera)
{
    level_init_video();
    level_load_1_1(&level_current);
    traps_load_1_1(&traps_current, &level_current);
    enemies_load_1_1(&enemy_current, &level_current);
    player_spawn(player);
    camera_init(camera);
    level_force_stream_update();
    messages_init();
    audio_play_bgm();
    REG_DISPCNT = MODE_0 | BG0_ON | BG1_ON | OBJ_ON | OBJ_1D_MAP;
}

int main(void)
{
    irqInit();
    irqSet(IRQ_VBLANK, audio_vblank);
    irqEnable(IRQ_VBLANK);

    REG_DISPCNT = MODE_0 | BG0_ON | BG1_ON | OBJ_ON | OBJ_1D_MAP;

    audio_init();

    level_init_video();
    level_load_1_1(&level_current);
    player_init_video();
    traps_init_video();
    enemies_init_video();
    messages_init();
    traps_load_1_1(&traps_current, &level_current);
    enemies_load_1_1(&enemy_current, &level_current);

    Player player = { 0 };
    Camera camera;
    GameState game_state = GAME_STATE_LIVES;
    uint8_t lives_timer = RESTART_SCREEN_FRAMES;

    camera_init(&camera);
    draw_lives_screen(2);

    while (1) {
        if (game_state == GAME_STATE_LIVES) {
            if (lives_timer > 0) {
                lives_timer--;
            } else {
                start_stage(&player, &camera);
                game_state = GAME_STATE_PLAYING;
            }

            VBlankIntrWait();
            audio_update();
            continue;
        }

        player_update(&player, &level_current, &traps_current);
        traps_update(&traps_current, &level_current, &player);
        enemies_update(&enemy_current, &level_current, &player);
        messages_update();

        if (!player.alive && player.death_timer == 0) {
            draw_lives_screen(2 - (int)player.death_count);
            lives_timer = RESTART_SCREEN_FRAMES;
            game_state = GAME_STATE_LIVES;
            VBlankIntrWait();
            audio_update();
            continue;
        }

        if (traps_stage_clear_requested(&traps_current)) {
            uint16_t death_count = player_death_count(&player);

            traps_ack_stage_clear(&traps_current);
            level_load_1_1(&level_current);
            traps_load_1_1(&traps_current, &level_current);
            enemies_load_1_1(&enemy_current, &level_current);
            memset(&player, 0, sizeof(player));
            player.death_count = death_count;
            player_spawn(&player);
            camera_init(&camera);
            level_force_stream_update();
            audio_play_bgm();
        }

        camera_update(&camera, &player, &level_current);

        VBlankIntrWait();
        audio_update();

        level_stream_bg0(&level_current, camera_x_px(&camera), camera_y_px(&camera));
        player_draw(&player, &camera);
        traps_draw(&traps_current, &camera);
        enemies_draw(&enemy_current, &camera);
        messages_draw(&camera);
    }
}
