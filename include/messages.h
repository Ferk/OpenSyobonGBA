#ifndef MESSAGES_H
#define MESSAGES_H

#include <stdint.h>

#include "fixed.h"

struct Camera;

typedef enum MessageId {
    MESSAGE_NONE = 0,
    MESSAGE_PLAYER_TASTY,
    MESSAGE_PLAYER_BAD_MUSHROOM,
    MESSAGE_PLAYER_STABBED,
    MESSAGE_ENEMY_TAUNT,
    MESSAGE_ENEMY_SLEEP,
    MESSAGE_ENEMY_DELISH,
    MESSAGE_STAGE_CLEAR,
} MessageId;

void messages_init(void);
void messages_update(void);
void messages_show(MessageId id, fix16_t x, fix16_t y, uint8_t frames);
void messages_draw(const struct Camera *camera);

#endif
