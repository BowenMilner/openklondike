#include "position.h"
#include "solver.h"
#include <string.h>

static const uint8_t magic[8] = {'O','K','P','O','S',0,1,0};
static uint32_t checksum(const uint8_t *data, size_t length) {
    uint32_t hash = UINT32_C(2166136261);
    for (size_t i = 0; i < length; i++) hash = (hash ^ data[i]) * UINT32_C(16777619);
    return hash;
}
static void write_sum(uint8_t *p, uint32_t sum) {
    for (int i = 0; i < 4; i++) p[i] = (uint8_t)(sum >> (24 - i * 8));
}
static uint32_t read_sum(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static Pile *pile_at(Game *g, int index) {
    if (index == 0) return &g->stock;
    if (index == 1) return &g->waste;
    if (index < 6) return &g->foundation[index - 2];
    return &g->tableau[index - 6];
}
bool position_encode(const Game *game, uint8_t out[OK_POSITION_BYTES]) {
    if (!game || !out || !solver_same_position(game, game) ||
        game->waste_drawn < 0 || game->waste_drawn > (game->draw_mode == DRAW_ONE ? 1 : 3)) return false;
    uint8_t data[OK_POSITION_BYTES] = {0};
    memcpy(data, magic, sizeof(magic));
    data[8] = (uint8_t)game->draw_mode;
    data[9] = (uint8_t)game->waste_drawn;
    Game copy = *game;
    size_t cursor = 23;
    for (int i = 0; i < 13; i++) {
        const Pile *p = pile_at(&copy, i);
        data[10 + i] = (uint8_t)p->count;
        for (int c = 0; c < p->count; c++) {
            Card card = p->cards[c];
            data[cursor++] = (uint8_t)(card.suit * 13 + card.rank + (card.face_up ? 128 : 0));
        }
    }
    if (cursor != 75) return false;
    write_sum(data + 75, checksum(data, 75));
    memcpy(out, data, sizeof(data));
    return true;
}
bool position_decode(const uint8_t *data, size_t length, Game *out) {
    if (!data || !out || length != OK_POSITION_BYTES ||
        memcmp(data, magic, sizeof(magic)) || read_sum(data + 75) != checksum(data, 75) ||
        (data[8] != DRAW_ONE && data[8] != DRAW_THREE) ||
        data[9] > (data[8] == DRAW_ONE ? 1 : 3)) return false;
    Game game = {0};
    game.draw_mode = (DrawMode)data[8];
    game.waste_drawn = data[9];
    game.phase = PHASE_PLAY;
    size_t cursor = 23;
    for (int i = 0; i < 13; i++) {
        Pile *p = pile_at(&game, i);
        p->count = data[10 + i];
        if (p->count > 52 || cursor + p->count > 75) return false;
        for (int c = 0; c < p->count; c++) {
            uint8_t byte = data[cursor++], id = byte & 127;
            if (id < 1 || id > 52) return false;
            p->cards[c] = (Card){(uint8_t)((id - 1) % 13 + 1),
                                (uint8_t)((id - 1) / 13), (uint8_t)(byte >> 7)};
        }
    }
    if (cursor != 75) return false;
    if (game.foundation[0].count == 13 && game.foundation[1].count == 13 &&
        game.foundation[2].count == 13 && game.foundation[3].count == 13) game.phase = PHASE_WON;
    if (!solver_same_position(&game, &game)) return false;
    *out = game;
    return true;
}
