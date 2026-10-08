#include "history.h"
#include <string.h>
void history_clear(History *h) { h->start=0; h->count=0; h->has_winnable=false; }
void history_push(History *h, const Game *before) {
    size_t slot=(h->start+h->count)%HISTORY_CAPACITY;
    h->entries[slot]=*before;
    if(h->count==HISTORY_CAPACITY) h->start=(h->start+1)%HISTORY_CAPACITY;
    else h->count++;
}
bool history_undo(History *h, Game *out) {
    if(!h->count) return false;
    *out=h->entries[(h->start+--h->count)%HISTORY_CAPACITY];
    out->events=0;
    return true;
}
void history_mark_winnable(History *h, const Game *g) { h->winnable=*g; h->winnable.events=0; h->has_winnable=true; }
bool history_restore_winnable(History *h, Game *out) {
    if(!h->has_winnable) return false;
    *out=h->winnable;out->events=0;h->start=0;h->count=0;
    return true;
}
