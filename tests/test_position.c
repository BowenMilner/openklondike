#include "../src/position.h"
#include "../src/solver.h"
#include "../src/certified_deals.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void repair_checksum(uint8_t data[OK_POSITION_BYTES]) {
    uint32_t sum = UINT32_C(2166136261);
    for (int i = 0; i < 75; i++) sum = (sum ^ data[i]) * UINT32_C(16777619);
    for (int i = 0; i < 4; i++) data[75 + i] = (uint8_t)(sum >> (24 - i * 8));
}

int main(void) {
    for (int mode = DRAW_ONE; mode <= DRAW_THREE; mode += 2) {
        Game game, restored;
        uint8_t bytes[OK_POSITION_BYTES];
        assert(certified_deal(&game, (DrawMode)mode, 13));
        for (int i = 0; i < 3; i++) assert(game_draw(&game));
        assert(position_encode(&game, bytes));
        assert(position_decode(bytes, sizeof(bytes), &restored));
        assert(solver_same_position(&game, &restored));
        assert(restored.waste_drawn == game.waste_drawn);
    }
    puts("PASS: position_file_roundtrip_full_deck_both_modes");
    Game game, sentinel, out;
    uint8_t bytes[OK_POSITION_BYTES], bad[OK_POSITION_BYTES];
    assert(certified_deal(&game, DRAW_ONE, 0));
    assert(certified_deal(&sentinel, DRAW_THREE, 1));
    assert(position_encode(&game, bytes));
    out = sentinel;
    memcpy(bad, bytes, sizeof(bad)); bad[35] ^= 4;
    assert(!position_decode(bad, sizeof(bad), &out));
    assert(!position_decode(bytes, sizeof(bytes) - 1, &out));
    memcpy(bad, bytes, sizeof(bad)); bad[6] = 2;
    assert(!position_decode(bad, sizeof(bad), &out));
    assert(memcmp(&out, &sentinel, sizeof(out)) == 0);
    puts("PASS: position_file_corruption_version_and_atomic_rejection");
    memcpy(bad, bytes, sizeof(bad)); bad[9] = 3;
    repair_checksum(bad);
    assert(!position_decode(bad, sizeof(bad), &out));
    memcpy(bad, bytes, sizeof(bad)); bad[23] = 127;
    repair_checksum(bad);
    assert(!position_decode(bad, sizeof(bad), &out));
    assert(memcmp(&out, &sentinel, sizeof(out)) == 0);
    puts("PASS: position_file_malformed_payload_rejected_after_checksum");
    game.stock.cards[0] = game.stock.cards[1];
    assert(!position_encode(&game, bytes));
    puts("PASS: position_file_invalid_deck_rejected");
    return 0;
}
