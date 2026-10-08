#ifndef OPENKLONDIKE_HISTORY_H
#define OPENKLONDIKE_HISTORY_H
#include "game.h"
#include <stddef.h>
#define HISTORY_CAPACITY 256
// Bounded undo history plus an independently proved recovery checkpoint.
typedef struct {
    Game entries[HISTORY_CAPACITY];
    size_t start, count;
    bool has_winnable;
    Game winnable;
} History;
void history_clear(History *h);
void history_push(History *h, const Game *before);
bool history_undo(History *h, Game *out);
void history_mark_winnable(History *h, const Game *g);
bool history_restore_winnable(History *h, Game *out);
#endif
