// Headless integration checks for the native iOS session adapter.
#include "../ios/native_session.h"
#include "../src/certified_deals.h"
#include "../src/solver.h"
#include "../src/tick.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FAIL(msg) do { fprintf(stderr, "FAIL: native session — %s\n", msg); exit(1); } while (0)
#define PASS(name) printf("PASS: native_session_%s\n", name)

static Card card(int rank, int suit, int face_up) {
    return (Card){(uint8_t)rank, (uint8_t)suit, (uint8_t)face_up};
}

static void push(Pile *pile, Card value) {
    if (pile->count >= 52) FAIL("test fixture overflowed a pile");
    pile->cards[pile->count++] = value;
}

static bool same_snapshot(const Game *left, const Game *right) {
    Game a = *left, b = *right;
    a.events = b.events = 0;
    return memcmp(&a, &b, sizeof(a)) == 0;
}

static void finish_session_search(OKSession *session) {
    unsigned i;
    for (i = 0; i < 10000; i++) {
        SolverStatus status = ok_session_status(session);
        if (status != SOLVER_CHECKING) return;
        ok_session_step(session, 0.0);
    }
    FAIL("bounded session slices did not finish the small fixture search");
}

// A complete, valid stock-only dead end. Aces, Queens and Nines are buried;
// the visible Kings and Tens cannot accept another exposed tableau card.
static Game stock_deadend(DrawMode mode) {
    Game g = {0};
    bool used[4][14] = {{false}};
    Card tops[7] = {
        {13, 0, 1}, {13, 1, 1}, {13, 2, 1}, {13, 3, 1},
        {10, 0, 1}, {10, 1, 1}, {10, 2, 1},
    };
    Card hidden[21];
    int col, rank, suit, n = 0, cursor = 0;
    g.draw_mode = mode;
    g.phase = PHASE_PLAY;
    for (col = 0; col < 7; col++) used[tops[col].suit][tops[col].rank] = true;
    for (suit = 0; suit < 4; suit++) {
        const int blocked[] = {1, 9, 12};
        int i;
        for (i = 0; i < 3; i++) {
            rank = blocked[i];
            if (used[suit][rank]) FAIL("dead-end fixture has a duplicate blocked card");
            used[suit][rank] = true;
            hidden[n++] = card(rank, suit, 0);
        }
    }
    for (suit = 0; suit < 4; suit++) {
        for (rank = 1; rank <= 13; rank++) {
            if (used[suit][rank]) continue;
            used[suit][rank] = true;
            if (n < 21) hidden[n++] = card(rank, suit, 0);
            else push(&g.stock, card(rank, suit, 0));
        }
    }
    if (n != 21 || g.stock.count != 24) FAIL("dead-end fixture does not contain all 52 cards");
    for (col = 0; col < 7; col++) {
        int i;
        for (i = 0; i < col; i++) push(&g.tableau[col], hidden[cursor++]);
        push(&g.tableau[col], tops[col]);
    }
    return g;
}

// A valid full-deck snapshot with one exposed Ace that can be auto-moved to
// an empty foundation. The other 51 cards remain face down in stock.
static Game ace_auto_move_position(DrawMode mode) {
    Game g = {0};
    bool used[4][14] = {{false}};
    int suit, rank;
    g.draw_mode = mode;
    g.phase = PHASE_PLAY;
    push(&g.tableau[0], card(1, 2, 1));
    used[2][1] = true;
    for (suit = 0; suit < 4; suit++) {
        for (rank = 1; rank <= 13; rank++) {
            if (used[suit][rank]) continue;
            used[suit][rank] = true;
            push(&g.stock, card(rank, suit, 0));
        }
    }
    return g;
}

static bool hint_is_legal(const Game *g, const SolverMove *move) {
    if (move->type == SOLVER_MOVE_DRAW)
        return move->from_kind == LOC_STOCK && g->stock.count > 0;
    if (move->type != SOLVER_MOVE_CARD) return false;
    if (move->from_kind < LOC_WASTE || move->from_kind > LOC_TABLEAU) return false;
    if (move->to_kind != LOC_FOUNDATION && move->to_kind != LOC_TABLEAU) return false;
    if (move->from_kind == LOC_WASTE) {
        if (move->from_index != 0 || move->card_index >= g->waste.count) return false;
        if (!g->waste.cards[move->card_index].face_up) return false;
    } else if (move->from_kind == LOC_FOUNDATION) {
        if (move->from_index >= 4 || move->card_index >= g->foundation[move->from_index].count) return false;
        if (!g->foundation[move->from_index].cards[move->card_index].face_up) return false;
    } else {
        if (move->from_index >= 7 || move->card_index >= g->tableau[move->from_index].count) return false;
        if (!g->tableau[move->from_index].cards[move->card_index].face_up) return false;
    }
    return game_can_drop(g, (PileKind)move->from_kind, move->from_index,
                         move->card_index, (PileKind)move->to_kind, move->to_index);
}

static bool apply_session_hint(OKSession *session, const SolverMove *move) {
    if (move->type == SOLVER_MOVE_DRAW) return ok_session_draw(session);
    return ok_session_move(session, (PileKind)move->from_kind, move->from_index,
                           move->card_index, (PileKind)move->to_kind, move->to_index);
}

static void assert_current_hint_is_legal(OKSession *session) {
    SolverMove hint;
    SolverStatus status = ok_session_status(session);
    bool available = ok_session_hint(session, &hint);
    if (status == SOLVER_WINNABLE) {
        if (!available || !hint_is_legal(ok_session_game(session), &hint))
            FAIL("winnable status exposed a stale or illegal hint for another position");
    } else if (available) {
        FAIL("session exposed a solution hint without a current winning proof");
    }
}

static void test_certified_new_games_both_modes(void) {
    const DrawMode modes[] = {DRAW_ONE, DRAW_THREE};
    size_t i;
    for (i = 0; i < sizeof(modes) / sizeof(modes[0]); i++) {
        OKSession *session = ok_session_create(modes[i], 0);
        const Game *g;
        SolverMove hint;
        if (!session) FAIL("session could not create a certified opening");
        g = ok_session_game(session);
        if (!g || g->draw_mode != modes[i] || g->phase != PHASE_PLAY)
            FAIL("new session returned the wrong mode or a finished board");
        if (ok_session_status(session) != SOLVER_WINNABLE)
            FAIL("new certified deal was not immediately known to be winnable");
        if (!ok_session_hint(session, &hint) || !hint_is_legal(g, &hint))
            FAIL("certified opening did not provide a legal, visible next action");
        ok_session_destroy(session);
    }
    PASS("certified_new_games_both_modes");
}

static void test_bad_snapshot_load_is_rejected_without_mutation(void) {
    OKSession *session = ok_session_create(DRAW_ONE, 3);
    Game original, bad;
    if (!session) FAIL("session setup failed for snapshot validation");
    original = *ok_session_game(session);

    if (ok_session_load(session, NULL)) FAIL("NULL snapshot was accepted");
    if (!same_snapshot(ok_session_game(session), &original))
        FAIL("NULL load changed the active game");

    bad = original;
    bad.stock.cards[0] = bad.tableau[0].cards[0]; // duplicate card identity
    if (ok_session_load(session, &bad)) FAIL("duplicate-card snapshot was accepted");
    if (!same_snapshot(ok_session_game(session), &original))
        FAIL("invalid snapshot partially replaced the active game");

    bad = original;
    bad.draw_mode = (DrawMode)99;
    if (ok_session_load(session, &bad)) FAIL("unsupported draw mode was accepted");
    if (!same_snapshot(ok_session_game(session), &original))
        FAIL("unsupported-mode load changed the active game");

    ok_session_destroy(session);
    PASS("snapshot_validation_is_atomic");
}

static void test_hint_uses_real_rules_and_session_routes_it(void) {
    OKSession *session = ok_session_create(DRAW_THREE, 5);
    Game expected;
    SolverMove hint;
    if (!session) FAIL("session setup failed for hint routing");
    if (!ok_session_hint(session, &hint)) FAIL("certified game did not expose its next move");
    expected = *ok_session_game(session);
    if (!hint_is_legal(&expected, &hint))
        FAIL("hint referenced a hidden card or illegal source/destination");
    if (!solver_apply_move(&expected, &hint))
        FAIL("hint could not be replayed through the shared game rules");
    if (!apply_session_hint(session, &hint))
        FAIL("session rejected its own legal hint action");
    if (!same_snapshot(ok_session_game(session), &expected))
        FAIL("session hint action changed a different source/destination than the engine");
    assert_current_hint_is_legal(session);
    ok_session_destroy(session);
    PASS("hint_source_destination_and_apply");
}

static void test_draw_and_undo_restore_the_exact_position(void) {
    OKSession *session = ok_session_create(DRAW_ONE, 9);
    Game before, expected;
    if (!session) FAIL("session setup failed for draw/undo");
    before = *ok_session_game(session);
    expected = before;
    if (!game_draw(&expected)) FAIL("draw fixture had no stock action");
    if (!ok_session_draw(session)) FAIL("session rejected a legal stock draw");
    if (!same_snapshot(ok_session_game(session), &expected))
        FAIL("session draw diverged from the shared game rules");
    assert_current_hint_is_legal(session);
    if (!ok_session_can_undo(session)) FAIL("stock draw was not added to undo history");
    if (!ok_session_undo(session)) FAIL("session could not undo the stock draw");
    if (!same_snapshot(ok_session_game(session), &before))
        FAIL("undo did not restore the exact pre-draw game snapshot");
    assert_current_hint_is_legal(session);
    if (ok_session_can_undo(session)) FAIL("one undo remained available twice");
    finish_session_search(session);
    if (ok_session_status(session) != SOLVER_WINNABLE)
        FAIL("undo restored a certified opening without its winning proof");
    ok_session_destroy(session);
    PASS("draw_undo_and_proof_recheck");
}

static void test_auto_move_and_undo_use_shared_rules(void) {
    OKSession *session = ok_session_create(DRAW_ONE, 11);
    Game fixture = ace_auto_move_position(DRAW_ONE);
    Game expected;
    if (!session) FAIL("session setup failed for auto-move");
    if (!ok_session_load(session, &fixture)) FAIL("valid full-deck snapshot was rejected");
    expected = fixture;
    if (!game_auto_move(&expected, LOC_TABLEAU, 0, 0))
        FAIL("auto-move fixture has no legal Ace destination");
    if (!ok_session_auto_move(session, LOC_TABLEAU, 0, 0))
        FAIL("session rejected the legal Ace auto-move");
    if (!same_snapshot(ok_session_game(session), &expected))
        FAIL("session auto-move did not match game_auto_move");
    assert_current_hint_is_legal(session);
    if (!ok_session_can_undo(session) || !ok_session_undo(session))
        FAIL("auto-move was not undoable");
    if (!same_snapshot(ok_session_game(session), &fixture))
        FAIL("auto-move undo did not restore its source snapshot");
    assert_current_hint_is_legal(session);
    ok_session_destroy(session);
    PASS("auto_move_undo");
}

static void test_recovery_checkpoint_is_a_proved_position(void) {
    OKSession *session = ok_session_create(DRAW_ONE, 13);
    SolverMove hint;
    Solver *proof;
    if (!session) FAIL("session setup failed for recovery checkpoint");
    if (ok_session_status(session) != SOLVER_WINNABLE ||
        !ok_session_hint(session, &hint) || !hint_is_legal(ok_session_game(session), &hint))
        FAIL("starting recovery point was not proved winnable");
    if (!apply_session_hint(session, &hint)) FAIL("could not make a legal move from the proof path");
    finish_session_search(session);
    if (ok_session_status(session) != SOLVER_WINNABLE)
        FAIL("proof-path position lost its winning status");
    if (!ok_session_can_restore(session))
        FAIL("proved position was not made available as a recovery checkpoint");
    if (!ok_session_restore(session)) FAIL("session could not restore its proved checkpoint");
    if (ok_session_status(session) != SOLVER_WINNABLE)
        FAIL("recovery did not return a position still marked winnable");
    proof = solver_create(ok_session_game(session));
    if (!proof || solver_status(proof) != SOLVER_WINNABLE)
        FAIL("restored checkpoint is not independently recognized as winnable");
    solver_destroy(proof);
    ok_session_destroy(session);
    PASS("proved_recovery_checkpoint");
}

static void test_exact_deadend_loss_in_both_modes_and_no_false_recovery(void) {
    const DrawMode modes[] = {DRAW_ONE, DRAW_THREE};
    size_t i;
    for (i = 0; i < sizeof(modes) / sizeof(modes[0]); i++) {
        OKSession *session = ok_session_create(modes[i], 17);
        Game dead = stock_deadend(modes[i]);
        if (!session) FAIL("session setup failed for dead-end search");
        if (!ok_session_load(session, &dead)) FAIL("valid dead-end snapshot was rejected");
        if (ok_session_status(session) == SOLVER_WINNABLE)
            FAIL("the previous opening's proof remained attached to a different loaded board");
        finish_session_search(session);
        if (ok_session_status(session) != SOLVER_UNWINNABLE)
            FAIL("complete legal-graph exhaustion did not report the dead end");
        if (ok_session_can_restore(session))
            FAIL("loading a separate dead-end game kept another deal's recovery checkpoint");
        ok_session_destroy(session);
    }
    PASS("exact_loss_both_modes_and_load_reset");
}

static void test_elapsed_time_is_fixed_step_and_capped_after_stall(void) {
    OKSession *session = ok_session_create(DRAW_ONE, 19);
    int initial, after_normal, after_stall;
    unsigned i;
    if (!session) FAIL("session setup failed for timer");
    initial = ok_session_game(session)->timer_frames;
    for (i = 0; i < 12; i++) ok_session_step(session, 1.0 / 60.0);
    after_normal = ok_session_game(session)->timer_frames;
    if (after_normal - initial < 10 || after_normal - initial > 12)
        FAIL("timer did not advance at the expected fixed-step rate");
    ok_session_step(session, -3.0);
    if (ok_session_game(session)->timer_frames != after_normal)
        FAIL("negative elapsed time moved the game clock backward");
    ok_session_step(session, 3600.0);
    after_stall = ok_session_game(session)->timer_frames;
    if (after_stall - after_normal > SIM_MAX_STEPS)
        FAIL("a long pause caused a huge timer catch-up jump");
    ok_session_destroy(session);
    PASS("elapsed_time_and_stall_cap");
}

static void test_analysis_progress_and_repeat_check(void) {
    OKSession *session = ok_session_create(DRAW_ONE, 14);
    Game dead = stock_deadend(DRAW_ONE);
    if (!session || !ok_session_load(session, &dead)) FAIL("analysis progress setup failed");
    if (ok_session_search_states(session) != 1) FAIL("new uncached analysis did not expose its root");
    ok_session_check(session);
    finish_session_search(session);
    size_t count = ok_session_search_states(session);
    if (ok_session_status(session) != SOLVER_UNWINNABLE || count != 25)
        FAIL("progress count did not match the exhausted stock graph");
    ok_session_check(session);
    if (ok_session_status(session) != SOLVER_UNWINNABLE || ok_session_search_states(session) != count)
        FAIL("repeat deeper check discarded the completed result and restarted the same search");
    if (ok_session_can_check_deeper(session)) FAIL("completed deep check was offered again");
    ok_session_destroy(session);
    PASS("analysis_progress_and_repeat_check");
}

int main(void) {
    test_analysis_progress_and_repeat_check();
    test_certified_new_games_both_modes();
    test_bad_snapshot_load_is_rejected_without_mutation();
    test_hint_uses_real_rules_and_session_routes_it();
    test_draw_and_undo_restore_the_exact_position();
    test_auto_move_and_undo_use_shared_rules();
    test_recovery_checkpoint_is_a_proved_position();
    test_exact_deadend_loss_in_both_modes_and_no_false_recovery();
    test_elapsed_time_is_fixed_step_and_capped_after_stall();
    return 0;
}
