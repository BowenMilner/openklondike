#ifndef OPENKLONDIKE_CERTIFIED_DEALS_H
#define OPENKLONDIKE_CERTIFIED_DEALS_H

#include "game.h"
#include <stddef.h>

// Returns the number of verified-certificate candidates for this draw mode,
// including eight color-preserving or color-inverting suit variations.
size_t certified_deal_count(DrawMode mode);

// Copies one standard 1..7-column opening from the bundled bank, applies a
// color-safe suit variation, and verifies its full certificate through the
// game rules before returning.
// `choice` wraps within the bank; false means no usable verified deal exists.
bool certified_deal(Game *out, DrawMode mode, unsigned choice);

#endif
