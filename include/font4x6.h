#ifndef FONT4X6_H
#define FONT4X6_H

#include <stdint.h>

#define FONT4X6_WIDTH 4
#define FONT4X6_HEIGHT 6
#define FONT4X6_FIRST_CHAR 32
#define FONT4X6_LAST_CHAR 95
#define FONT4X6_COUNT (FONT4X6_LAST_CHAR - FONT4X6_FIRST_CHAR + 1)

extern const uint8_t font4x6_data[FONT4X6_COUNT][FONT4X6_HEIGHT];

uint8_t font4x6_row(char ch, uint8_t row);

#endif
