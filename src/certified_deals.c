#include "certified_deals.h"

#include "solver.h"
#include "certified_deals_data.h"

#include <string.h>

/* Each map preserves both color classes or swaps them as whole classes. */
static const uint8_t suit_variants[8][4] = {
    { 0, 1, 2, 3 }, { 0, 2, 1, 3 }, { 3, 1, 2, 0 }, { 3, 2, 1, 0 },
    { 1, 0, 3, 2 }, { 2, 0, 3, 1 }, { 1, 3, 0, 2 }, { 2, 3, 0, 1 }
};
#define SUIT_VARIANTS 8u

size_t certified_deal_count(DrawMode mode) {
    if (mode == DRAW_ONE) return (sizeof(draw_one_bank) / sizeof(draw_one_bank[0])) * SUIT_VARIANTS;
    if (mode == DRAW_THREE) return (sizeof(draw_three_bank) / sizeof(draw_three_bank[0])) * SUIT_VARIANTS;
    return 0;
}

bool certified_deal(Game *out, DrawMode mode, unsigned choice) {
    const CertifiedRecord *record;
    size_t base_count, offset = 0;
    unsigned variant;
    int col, row;
    if (!out) return false;
    base_count = mode == DRAW_ONE ? sizeof(draw_one_bank) / sizeof(draw_one_bank[0])
                : mode == DRAW_THREE ? sizeof(draw_three_bank) / sizeof(draw_three_bank[0]) : 0;
    if (!base_count) return false;
    variant = (choice / (unsigned)base_count) % SUIT_VARIANTS;
    if (mode == DRAW_ONE) record = &draw_one_bank[choice % base_count];
    else record = &draw_three_bank[choice % base_count];

    memset(out, 0, sizeof(*out));
    out->draw_mode = mode;
    out->phase = PHASE_PLAY;
    for (col = 0; col < 7; col++) {
        Pile *pile = &out->tableau[col];
        pile->count = col + 1;
        for (row = 0; row <= col; row++) {
            uint8_t id = record->cards[offset++];
            pile->cards[row] = (Card){
                (uint8_t)(id % 13 + 1), suit_variants[variant][id / 13], (uint8_t)(row == col)
            };
        }
    }
    out->stock.count = 24;
    for (row = 0; row < 24; row++) {
        uint8_t id = record->cards[offset++];
        out->stock.cards[row] = (Card){ (uint8_t)(id % 13 + 1), suit_variants[variant][id / 13], 0 };
    }
    if (offset != 52 || !solver_verify_and_cache(out, record->moves, record->move_count)) {
        memset(out, 0, sizeof(*out));
        return false;
    }
    return true;
}
