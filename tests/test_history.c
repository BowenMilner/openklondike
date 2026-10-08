// Unit tests for the bounded undo ring and the independent winnable checkpoint.
// This test uses the real History implementation and needs no rendering backend.
#include "../src/history.c"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FAIL(msg) do { fprintf(stderr, "FAIL: history — %s\n", msg); exit(1); } while (0)
#define PASS(name) printf("PASS: %s\n", name)

static Game snapshot(unsigned id) {
    Game g = {0};
    g.draw_mode = id & 1 ? DRAW_THREE : DRAW_ONE;
    g.phase = PHASE_PLAY;
    g.score = (int)(id * 17u);
    g.timer_frames = (int)(id * 31u);
    g.time_penalty_steps = (int)(id % 19u);
    g.moves = (int)id;
    g.stock_passes = (int)(id % 7u);
    g.waste_drawn = (int)(id % 4u);
    g.events = EV_DRAW | EV_MOVE;
    g.stock.count = 1;
    g.stock.cards[0] = (Card){(uint8_t)(id % 13u + 1u), (uint8_t)(id % 4u), 0};
    g.waste.count = 1;
    g.waste.cards[0] = (Card){(uint8_t)((id + 5u) % 13u + 1u), (uint8_t)((id + 1u) % 4u), 1};
    g.foundation[id % 4u].count = 1;
    g.foundation[id % 4u].cards[0] = (Card){(uint8_t)(id % 13u + 1u), (uint8_t)(id % 4u), 1};
    g.tableau[id % 7u].count = 2;
    g.tableau[id % 7u].cards[0] = (Card){(uint8_t)((id + 3u) % 13u + 1u), (uint8_t)((id + 2u) % 4u), 0};
    g.tableau[id % 7u].cards[1] = (Card){(uint8_t)((id + 4u) % 13u + 1u), (uint8_t)((id + 3u) % 4u), 1};
    return g;
}

// `events` are frame-local and intentionally cleared by history operations;
// all persistent game and pile state must match byte-for-byte otherwise.
static void assert_snapshot_equal(const Game *actual, const Game *expected, const char *message) {
    Game a = *actual, e = *expected;
    a.events = e.events = 0;
    if (memcmp(&a, &e, sizeof(Game)) != 0) FAIL(message);
    if (actual->events != 0) FAIL("restored frame events were not cleared");
}

static void test_empty_operations(void) {
    History h = {0};
    Game out = snapshot(91), untouched = out;

    if (history_undo(&h, &out)) FAIL("undo succeeded on an empty history");
    if (memcmp(&out, &untouched, sizeof(out)) != 0) FAIL("empty undo changed its output");
    if (history_restore_winnable(&h, &out)) FAIL("restore succeeded without a checkpoint");
    if (memcmp(&out, &untouched, sizeof(out)) != 0) FAIL("empty restore changed its output");

    history_clear(&h);
    if (history_undo(&h, &out) || history_restore_winnable(&h, &out))
        FAIL("clear left history or a checkpoint available");
    PASS("history_empty_operations");
}

static void test_undo_restores_complete_snapshot(void) {
    History h = {0};
    Game before = snapshot(23), out = {0};
    Game live = before;
    history_push(&h, &live);

    // Change score, timer and multiple piles after saving the pre-move state.
    live.score = 999;
    live.timer_frames = 123456;
    live.time_penalty_steps = 206;
    live.moves = 700;
    live.tableau[6].count = 0;
    live.stock.count = 9;

    if (!history_undo(&h, &out)) FAIL("undo failed with one saved move");
    assert_snapshot_equal(&out, &before, "undo did not restore score, timer, counters and piles");
    if (h.count != 0) FAIL("undo did not remove the restored entry");
    if (history_undo(&h, &out)) FAIL("undo succeeded twice for one saved move");
    PASS("history_undo_snapshot");
}

static void test_undo_ring_wraps_oldest_first(void) {
    History h = {0};
    Game out = {0};

    // Force two full wraps. Only the most recent HISTORY_CAPACITY snapshots
    // may survive, in reverse insertion order when undoing.
    for (unsigned id = 0; id < HISTORY_CAPACITY + 2u; id++) {
        Game g = snapshot(id + 1000u);
        history_push(&h, &g);
        if (h.count > HISTORY_CAPACITY) FAIL("ring count exceeded its capacity");
    }
    if (h.count != HISTORY_CAPACITY) FAIL("ring did not retain exactly its capacity");
    for (unsigned offset = 0; offset < HISTORY_CAPACITY; offset++) {
        unsigned id = 1000u + HISTORY_CAPACITY + 1u - offset;
        Game expected = snapshot(id);
        if (!history_undo(&h, &out)) FAIL("ring ended before all retained moves were undone");
        assert_snapshot_equal(&out, &expected, "ring wrap returned a stale or misordered snapshot");
    }
    if (h.count != 0 || history_undo(&h, &out)) FAIL("ring remained nonempty after all undos");
    PASS("history_ring_wrap");
}

static void test_winnable_checkpoint_is_independent(void) {
    History h = {0};
    Game checkpoint = snapshot(42), out = {0};
    history_mark_winnable(&h, &checkpoint);

    // Overwrite the full ring several times and consume some undo entries.
    // The recovery point must stay exact and remain available after restore.
    for (unsigned id = 0; id < HISTORY_CAPACITY * 3u; id++) {
        Game g = snapshot(id + 3000u);
        history_push(&h, &g);
    }
    for (int i = 0; i < HISTORY_CAPACITY / 2; i++)
        if (!history_undo(&h, &out)) FAIL("unexpectedly exhausted undo history before restore");

    if (!history_restore_winnable(&h, &out)) FAIL("saved winnable checkpoint was lost");
    assert_snapshot_equal(&out, &checkpoint, "checkpoint did not preserve the full game and score/time");
    if (h.count != 0) FAIL("restoring checkpoint did not clear branch undo history");
    if (history_undo(&h, &out)) FAIL("undo crossed back into the discarded branch");

    // Restore is a recovery point, not a one-shot undo entry.
    out.score = -123;
    out.timer_frames = -456;
    out.events = EV_INVALID;
    if (!history_restore_winnable(&h, &out)) FAIL("checkpoint was consumed by its first restore");
    assert_snapshot_equal(&out, &checkpoint, "second restore did not return to the checkpoint");
    PASS("history_checkpoint_independence");
}

static void test_clear_removes_checkpoint_and_ring(void) {
    History h = {0};
    Game g = snapshot(61), out = snapshot(62);
    history_mark_winnable(&h, &g);
    history_push(&h, &g);
    history_clear(&h);
    if (history_undo(&h, &out)) FAIL("clear left undo entries");
    if (history_restore_winnable(&h, &out)) FAIL("clear left a winnable checkpoint");
    PASS("history_clear");
}

int main(void) {
    test_empty_operations();
    test_undo_restores_complete_snapshot();
    test_undo_ring_wraps_oldest_first();
    test_winnable_checkpoint_is_independent();
    test_clear_removes_checkpoint_and_ring();
    return 0;
}
