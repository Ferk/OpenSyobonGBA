#include <gba.h>
#include <stdint.h>
#include <string.h>

#include "audio.h"
#include "camera.h"
#include "enemy.h"
#include "font4x6.h"
#include "generated/level1_data.h"
#include "generated/level1_2_data.h"
#include "generated/level1_2b_data.h"
#include "generated/level1_2u_data.h"
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
#define KEY_START_MASK KEY_START
#define KEY_A_MASK KEY_A
#define KEY_B_MASK KEY_B
#define KEY_SELECT_MASK KEY_SELECT
#define KEY_LEFT_MASK KEY_LEFT
#define KEY_RIGHT_MASK KEY_RIGHT
#define REF_POS_TO_FIX(v) ((fix16_t)(((int64_t)(v) * 16 * FIX16_ONE) / (29 * 100)))
#define REF_STAGE_Y_TO_FIX(v) REF_POS_TO_FIX((v) + 12 * 100 - (LEVEL_VIEW_SOURCE_ROW_OFFSET * 29 * 100))

typedef enum StageId {
    STAGE_ID_1_1 = 0,
    STAGE_ID_1_2,
    STAGE_ID_1_2_UNDERGROUND,
    STAGE_ID_1_2B,
    STAGE_ID_COUNT,
} StageId;

typedef enum GameState {
    GAME_STATE_LIVES = 0,
    GAME_STATE_PLAYING,
} GameState;

static uint16_t modal_previous_keys;
static uint8_t modal_input_armed;
static uint8_t bg0_priority_cache = 1;

static uint16_t keys_held(void)
{
    return (uint16_t)(~REG_KEYINPUT & 0x03ff);
}

#ifdef DEBUG_STAGE_SELECT
static uint16_t menu_previous_keys;
#endif

static void clear_oam(void)
{
    for (uint16_t i = 0; i < 128; ++i) {
        OAM[i].attr0 = ATTR0_DISABLED;
        OAM[i].attr1 = 0;
        OAM[i].attr2 = 0;
    }
}

static void set_bg0_priority(uint16_t priority)
{
    if (bg0_priority_cache != priority) {
        REG_BG0CNT = (uint16_t)((REG_BG0CNT & ~BG_PRIORITY(3)) |
                                BG_PRIORITY(priority));
        bg0_priority_cache = (uint8_t)priority;
    }
}

static void layer_sprites_between_dialog_and_world(void)
{
    for (uint16_t i = 0; i < 128; ++i) {
        if ((OAM[i].attr0 & ATTR0_DISABLED) != ATTR0_DISABLED) {
            uint16_t attr2 = OAM[i].attr2;

            if ((attr2 & ATTR2_PRIORITY(3)) != ATTR2_PRIORITY(2)) {
                OAM[i].attr2 = (uint16_t)((attr2 & ~ATTR2_PRIORITY(3)) |
                                          ATTR2_PRIORITY(2));
            }
        }
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

static void append_text(char *text, const char *suffix)
{
    uint8_t out = 0;
    uint8_t in = 0;

    while (text[out] != '\0') {
        out++;
    }

    while (suffix[in] != '\0') {
        text[out++] = suffix[in++];
    }
    text[out] = '\0';
}

#ifdef DEBUG_STAGE_SELECT
static const char *stage_label(StageId stage)
{
    if (stage == STAGE_ID_1_2_UNDERGROUND) {
        return "1-2U ";
    }

    if (stage == STAGE_ID_1_2B) {
        return "1-2B ";
    }

    if (stage == STAGE_ID_1_2) {
        return "1-2 ";
    }

    return "1-1 ";
}
#endif

static void draw_lives_screen(StageId stage, int lives)
{
    char text[20] = "";
    uint8_t len = 0;
    uint8_t tile_len;
    uint16_t *map = (uint16_t *)SCREEN_BASE_BLOCK(30);
    uint16_t blank_entry = LIFE_TEXT_BLANK_TILE;

#ifdef DEBUG_STAGE_SELECT
    append_text(text, stage_label(stage));
#else
    (void)stage;
#endif
    append_text(text, "X ");
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

#ifdef DEBUG_STAGE_SELECT
static void change_stage(StageId *stage, int delta)
{
    int next = (int)*stage + delta;

    if (next < 0) {
        next = STAGE_ID_COUNT - 1;
    } else if (next >= STAGE_ID_COUNT) {
        next = 0;
    }

    *stage = (StageId)next;
}
#endif

static void show_lives_screen(StageId stage, Player *player,
                              uint8_t *lives_timer, GameState *game_state)
{
    draw_lives_screen(stage, 2 - (int)player->death_count);
    *lives_timer = RESTART_SCREEN_FRAMES;
    *game_state = GAME_STATE_LIVES;
}

static void apply_camera_left_limit(Player *player, const Camera *camera)
{
    int left_limit = camera_player_left_limit_px(camera);

    if (player->alive && FIX16_TO_INT(player->x) < left_limit) {
        player->x = FIX16_FROM_INT(left_limit);
        if (player->vx < 0) {
            player->vx = 0;
        }
    }
}

static void draw_game_scene(const Player *player, const Camera *camera,
                            uint8_t modal_layers)
{
    level_stream_bg0(&level_current, camera_x_px(camera), camera_y_px(camera));
    player_draw(player, camera);
    traps_draw(&traps_current, camera);
    enemies_draw(&enemy_current, camera);
    if (modal_layers) {
        layer_sprites_between_dialog_and_world();
    }
    messages_draw(camera);
}

static void load_stage(StageId stage)
{
    if (stage == STAGE_ID_1_2_UNDERGROUND) {
        level_load_1_2_underground(&level_current);
        traps_load_1_2_underground(&traps_current, &level_current);
        enemies_load_1_2_underground(&enemy_current, &level_current);
    } else if (stage == STAGE_ID_1_2B) {
        level_load_1_2b(&level_current);
        traps_load_1_2b(&traps_current, &level_current);
        enemies_load_1_2b(&enemy_current, &level_current);
    } else if (stage == STAGE_ID_1_2) {
        level_load_1_2(&level_current);
        traps_load_1_2(&traps_current, &level_current);
        enemies_load_1_2(&enemy_current, &level_current);
    } else {
        level_load_1_1(&level_current);
        traps_load_1_1(&traps_current, &level_current);
        enemies_load_1_1(&enemy_current, &level_current);
    }
}

static uint8_t generated_stage_object(StageId stage, uint8_t kind, fix16_t *x, fix16_t *y)
{
    const GeneratedMapObject *objects = level1_objects;
    uint16_t object_count = level1_object_count;

    if (stage == STAGE_ID_1_2) {
        objects = level1_2_objects;
        object_count = level1_2_object_count;
    } else if (stage == STAGE_ID_1_2_UNDERGROUND) {
        objects = level1_2u_objects;
        object_count = level1_2u_object_count;
    } else if (stage == STAGE_ID_1_2B) {
        objects = level1_2b_objects;
        object_count = level1_2b_object_count;
    }

    for (uint16_t i = 0; i < object_count; ++i) {
        if (objects[i].kind == kind) {
            *x = objects[i].x;
            *y = objects[i].y;
            return 1;
        }
    }

    return 0;
}

static uint8_t generated_player_start(StageId stage, fix16_t *x, fix16_t *y)
{
    return generated_stage_object(stage, GENERATED_OBJECT_PLAYER_START, x, y);
}

static uint8_t generated_pipe_exit(StageId stage, fix16_t *x, fix16_t *y)
{
    return generated_stage_object(stage, GENERATED_OBJECT_PIPE_EXIT, x, y);
}

static void start_stage(Player *player, Camera *camera, StageId stage)
{
    level_init_video();
    bg0_priority_cache = 1;
    load_stage(stage);
    player_spawn(player);
    if (!player->checkpoint_active) {
        fix16_t start_x = 0;
        fix16_t start_y = 0;

        if (generated_player_start(stage, &start_x, &start_y)) {
            player->x = start_x;
            player->y = start_y;
        }
    }
    camera_init(camera);
    level_force_stream_update();
    messages_init();
    audio_play_bgm();
    REG_DISPCNT = MODE_0 | BG0_ON | BG1_ON | OBJ_ON | OBJ_1D_MAP;
}

static void start_stage_from_pipe(Player *player, Camera *camera, StageId stage)
{
    start_stage(player, camera, stage);

    fix16_t exit_x = 0;
    fix16_t exit_y = 0;
    if (generated_pipe_exit(stage, &exit_x, &exit_y)) {
        player->x = exit_x;
        player->y = exit_y;
        camera_init(camera);
        level_force_stream_update();
    }
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
    StageId current_stage = STAGE_ID_1_1;
    GameState game_state = GAME_STATE_LIVES;
    uint8_t lives_timer = RESTART_SCREEN_FRAMES;

    camera_init(&camera);
    draw_lives_screen(current_stage, 2);

    while (1) {
        if (game_state == GAME_STATE_LIVES) {
#ifdef DEBUG_STAGE_SELECT
            uint16_t keys = keys_held();
            uint16_t pressed = keys & (uint16_t)~menu_previous_keys;
            uint8_t select_held = (keys & KEY_SELECT_MASK) != 0;

            if (select_held && (pressed & KEY_LEFT_MASK)) {
                change_stage(&current_stage, -1);
                player.checkpoint_active = 0;
                draw_lives_screen(current_stage, 2 - (int)player.death_count);
            } else if (select_held && (pressed & KEY_RIGHT_MASK)) {
                change_stage(&current_stage, 1);
                player.checkpoint_active = 0;
                draw_lives_screen(current_stage, 2 - (int)player.death_count);
            }

            if (!select_held && (pressed & (KEY_START_MASK | KEY_A_MASK | KEY_B_MASK))) {
                lives_timer = 0;
            }

            menu_previous_keys = keys;

            if (select_held) {
                VBlankIntrWait();
                audio_update();
                continue;
            }
#endif
            if (lives_timer > 0) {
                lives_timer--;
            } else {
                start_stage(&player, &camera, current_stage);
                game_state = GAME_STATE_PLAYING;
#ifdef DEBUG_STAGE_SELECT
                menu_previous_keys = 0;
#endif
            }

            VBlankIntrWait();
            audio_update();
            continue;
        }

        if (messages_is_modal()) {
            uint16_t keys = keys_held();
            uint16_t pressed = modal_input_armed ?
                (keys & (uint16_t)~modal_previous_keys) : 0;
            uint8_t dismissed_modal = 0;

            set_bg0_priority(3);
            if (!modal_input_armed) {
                modal_input_armed = 1;
            } else if (pressed & (KEY_A_MASK | KEY_B_MASK)) {
                messages_dismiss();
                player_sync_input();
                set_bg0_priority(1);
                modal_input_armed = 0;
                dismissed_modal = 1;
            }
            modal_previous_keys = keys;

            VBlankIntrWait();
            audio_update();

            draw_game_scene(&player, &camera, !dismissed_modal);
            continue;
        }
        set_bg0_priority(1);
        modal_input_armed = 0;

        player_update(&player, &level_current, &traps_current);
        traps_update(&traps_current, &level_current, &player);
        enemies_update(&enemy_current, &level_current, &player);
        messages_update();

#ifdef DEBUG_STAGE_SELECT
        {
            uint16_t keys = keys_held();
            uint16_t pressed = keys & (uint16_t)~menu_previous_keys;

            if ((pressed & KEY_SELECT_MASK) && player.alive) {
                player.death_count++;
                player.vx = 0;
                player.vy = 0;
                player.alive = 0;
                player.death_timer = 0;
                audio_stop_bgm();
                audio_play_death();
                show_lives_screen(current_stage, &player, &lives_timer, &game_state);
                menu_previous_keys = keys;
                VBlankIntrWait();
                audio_update();
                continue;
            }

            menu_previous_keys = keys;
        }
#endif

        if (!player.alive && player.death_timer == 0) {
            show_lives_screen(current_stage, &player, &lives_timer, &game_state);
            VBlankIntrWait();
            audio_update();
            continue;
        }

        if (traps_stage_clear_requested(&traps_current)) {
            uint16_t death_count = player_death_count(&player);

            traps_ack_stage_clear(&traps_current);
            if (current_stage == STAGE_ID_1_1) {
                current_stage = STAGE_ID_1_2;
            }
            memset(&player, 0, sizeof(player));
            player.death_count = death_count;
            show_lives_screen(current_stage, &player, &lives_timer, &game_state);
            camera_init(&camera);
            continue;
        }

        if (traps_stage_transition_requested(&traps_current)) {
            uint16_t death_count = player_death_count(&player);
            StageId target = (StageId)traps_stage_transition_target(&traps_current);

            traps_ack_stage_transition(&traps_current);
            if (target >= STAGE_ID_COUNT) {
                target = current_stage;
            }
            current_stage = target;
            memset(&player, 0, sizeof(player));
            player.death_count = death_count;
            start_stage_from_pipe(&player, &camera, current_stage);
            game_state = GAME_STATE_PLAYING;
#ifdef DEBUG_STAGE_SELECT
            menu_previous_keys = 0;
#endif
            continue;
        }

        camera_update(&camera, &player, &level_current);
        apply_camera_left_limit(&player, &camera);

        VBlankIntrWait();
        audio_update();

        draw_game_scene(&player, &camera, 0);
    }
}
