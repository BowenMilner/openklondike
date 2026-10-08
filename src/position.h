#ifndef OK_POSITION_H
#define OK_POSITION_H
#include "game.h"
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
// Versioned, platform-independent game-position file. Includes full deck state
// for reproduction; never display or log its decoded face-down identities.
#define OK_POSITION_BYTES 79
bool position_encode(const Game *game, uint8_t out[OK_POSITION_BYTES]);
bool position_decode(const uint8_t *data, size_t length, Game *out);
#ifdef __cplusplus
}
#endif
#endif
