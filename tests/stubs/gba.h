#ifndef TEST_GBA_H
#define TEST_GBA_H

#include <stdint.h>

/* Host-only hardware stand-ins. Runtime logic tests do not render GBA video. */
typedef struct { uint16_t attr0, attr1, attr2, fill; } OBJATTR;
extern OBJATTR test_oam[128];
extern uint16_t test_sprite_gfx[16384];
extern uint16_t test_sprite_palette[256];
extern uint16_t test_keys;
extern uint32_t test_bg_chars[8192];
extern uint16_t test_bg_map[1024];
extern uint16_t test_bg_palette[256];
extern uint16_t test_bg_control, test_bg_x, test_bg_y;
#define CHAR_BASE_BLOCK(n) test_bg_chars
#define SCREEN_BASE_BLOCK(n) test_bg_map
#define BG_PALETTE test_bg_palette
#define REG_BG1CNT test_bg_control
#define REG_BG1HOFS test_bg_x
#define REG_BG1VOFS test_bg_y
#define BG_16_COLOR 0
#define CHAR_BASE(n) ((n) << 2)
#define SCREEN_BASE(n) ((n) << 8)
#define BG_PRIORITY(n) (n)
#define TEXTBG_SIZE_256x256 0
#define RGB5(r, g, b) ((r) | ((g) << 5) | ((b) << 10))
#define OAM test_oam
#define SPRITE_GFX test_sprite_gfx
#define SPRITE_PALETTE test_sprite_palette
#define REG_KEYINPUT test_keys
#define EWRAM_BSS
#define ATTR0_DISABLED 0x0200
#define ATTR0_COLOR_16 0
#define ATTR0_SQUARE 0
#define ATTR1_SIZE_8 0
#define ATTR1_SIZE_16 0x4000
#define ATTR1_FLIP_X 0x1000
#define ATTR2_PALETTE(n) ((n) << 12)
#define ATTR2_PRIORITY(n) ((n) << 10)
#define OBJ_CHAR(n) (n)

#endif
