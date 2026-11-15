#ifndef TEST_GBA_H
#define TEST_GBA_H

#include <stdint.h>

/* Host-only hardware stand-ins. Runtime logic tests do not render GBA video. */
typedef struct { uint16_t attr0, attr1, attr2, fill; } OBJATTR;
extern OBJATTR test_oam[128];
extern uint16_t test_sprite_gfx[16384];
extern uint16_t test_sprite_palette[256];
extern uint16_t test_keys;
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
