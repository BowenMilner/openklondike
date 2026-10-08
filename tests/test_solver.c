// Solver regression tests use the real Klondike move engine and solver.
// `make test` runs this without a renderer or platform backend.
#include "../src/game.c"
#include "../src/solver.c"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FAIL(msg) do { fprintf(stderr, "FAIL: solver — %s\n", msg); exit(1); } while (0)
#define PASS(name) printf("PASS: %s\n", name)

#define STRESS_HASH_SLOTS (1u << 17)
static uint64_t stress_position_hashes[STRESS_HASH_SLOTS];
static size_t stress_unique_positions;

static void note_stress_position(const Game *g) {
    const Pile *piles[13];
    uint64_t hash = UINT64_C(14695981039346656037);
    size_t slot, probes;
    int i, j;
    piles[0] = &g->stock; piles[1] = &g->waste;
    for (i = 0; i < 4; i++) piles[2 + i] = &g->foundation[i];
    for (i = 0; i < 7; i++) piles[6 + i] = &g->tableau[i];
    hash ^= (uint8_t)g->draw_mode; hash *= UINT64_C(1099511628211);
    for (i = 0; i < 13; i++) {
        hash ^= (uint8_t)piles[i]->count; hash *= UINT64_C(1099511628211);
        for (j = 0; j < piles[i]->count; j++) {
            Card c = piles[i]->cards[j];
            hash ^= c.rank; hash *= UINT64_C(1099511628211);
            hash ^= c.suit; hash *= UINT64_C(1099511628211);
            hash ^= c.face_up; hash *= UINT64_C(1099511628211);
        }
    }
    if (hash == 0) hash = 1;
    slot = (size_t)hash & (STRESS_HASH_SLOTS - 1u);
    for (probes = 0; probes < STRESS_HASH_SLOTS; probes++, slot = (slot + 1u) & (STRESS_HASH_SLOTS - 1u)) {
        uint64_t stored = stress_position_hashes[slot];
        if (stored == hash) return;
        if (stored == 0) {
            stress_position_hashes[slot] = hash;
            stress_unique_positions++;
            return;
        }
    }
    FAIL("stress corpus exceeded its exact-position hash table");
}

static void push_card(Pile *p, Card c) {
    if (p->count >= 52) FAIL("test fixture overflowed a pile");
    p->cards[p->count++] = c;
}

static Card card(int rank, int suit, int face_up) {
    return (Card){(uint8_t)rank, (uint8_t)suit, (uint8_t)face_up};
}

// A complete valid position with exactly one reversible tableau move. The
// red 5 can move from 6♠ to 6♣, then back. No Ace is exposed and no other
// exposed rank has a legal destination, so exhaustive search should visit
// only these two distinct positions and prove the cycle has no winning exit.
static Game make_closed_cycle(DrawMode mode) {
    Game g = {0};
    bool used[4][14] = {{false}};
    Card tops[7] = {
        {5, 2, 1},   // red 5 hearts, above an already face-up black 6
        {6, 0, 1},   // black 6 clubs: the only initial destination
        {2, 1, 1},   // red 2 diamonds
        {2, 2, 1},   // red 2 hearts
        {8, 0, 1},   // black 8 clubs
        {10, 2, 1},  // red 10 hearts
        {12, 3, 1},  // black Queen of spades
    };
    int col, rank, suit;
    g.draw_mode = mode;
    g.phase = PHASE_PLAY;

    for (col = 0; col < 7; col++) {
        used[tops[col].suit][tops[col].rank] = true;
        if (col == 0) used[3][6] = true; // 6♠ exposed under the 5♥
    }
    used[2][7] = true; // reserved valid predecessor for the face-up run test
    for (col = 0; col < 7; col++) {
        // Distribute all other cards face down under a visible top. Eight
        // hidden cards in column 0 plus six in each other column uses 44.
        int wanted_down = col == 0 ? 7 : 6;
        int added = 0;
        for (suit = 0; suit < 4 && added < wanted_down; suit++) {
            for (rank = 1; rank <= 13 && added < wanted_down; rank++) {
                if (used[suit][rank]) continue;
                used[suit][rank] = true;
                push_card(&g.tableau[col], card(rank, suit, 0));
                added++;
            }
        }
        if (added != wanted_down) FAIL("cycle fixture did not have enough hidden cards");
        if (col == 0) {
            push_card(&g.tableau[col], card(7, 2, 0));
            push_card(&g.tableau[col], card(6, 3, 1));
        }
        push_card(&g.tableau[col], tops[col]);
    }
    return g;
}

static Game make_adjacent_column_count_pair(void) {
    Game g = {0};
    bool used[4][14] = {{false}};
    Card tops[7] = {
        {2, 0, 1}, {3, 1, 1}, {4, 0, 1}, {5, 2, 1},
        {8, 3, 1}, {13, 2, 1}, {12, 3, 1},
    };
    int col, rank, suit;
    g.draw_mode = DRAW_ONE;
    g.phase = PHASE_PLAY;
    for (col = 0; col < 7; col++) {
        used[tops[col].suit][tops[col].rank] = true;
        push_card(&g.tableau[col], tops[col]);
    }
    for (suit = 0; suit < 4; suit++) {
        for (rank = 1; rank <= 13; rank++) {
            if (used[suit][rank]) continue;
            used[suit][rank] = true;
            push_card(&g.stock, card(rank, suit, 0));
        }
    }
    return g;
}

// A full-deck snapshot with an Ace in its foundation and a red 2 exposed in
// the tableau. It provides a legal foundation take-back to validate against
// game_move, independent of whether a bundled opening certificate needs one.
static Game make_takeback_position(void) {
    Game g = {0};
    bool used[4][14] = {{false}};
    Card tops[7] = {
        {2, 0, 1}, {2, 3, 1}, {4, 0, 1}, {4, 1, 1},
        {6, 0, 1}, {8, 2, 1}, {10, 3, 1},
    };
    int col, rank, suit;
    g.draw_mode = DRAW_ONE;
    g.phase = PHASE_PLAY;
    push_card(&g.foundation[0], card(1, 2, 1));
    used[2][1] = true;
    for (col = 0; col < 7; col++) used[tops[col].suit][tops[col].rank] = true;

    for (col = 0; col < 7; col++) {
        int wanted_down = col < 2 ? 7 : 6; // seven + seven + five sixes = 44
        int added = 0;
        for (suit = 0; suit < 4 && added < wanted_down; suit++) {
            for (rank = 1; rank <= 13 && added < wanted_down; rank++) {
                if (used[suit][rank]) continue;
                used[suit][rank] = true;
                push_card(&g.tableau[col], card(rank, suit, 0));
                added++;
            }
        }
        if (added != wanted_down) FAIL("take-back fixture did not have enough hidden cards");
        push_card(&g.tableau[col], tops[col]);
    }
    return g;
}

// A standard Klondike layout whose only actions are stock draws and recycling.
// All four Aces, Queens and Nines are buried face down. Every exposed tableau
// card is either a King or a Ten, and all cards that could build on those ranks
// are buried, so neither waste nor tableau can make progress.
static Game make_standard_stock_deadend(DrawMode mode) {
    Game g = {0};
    bool used[4][14] = {{false}};
    Card tops[7] = {
        {13, 0, 1}, {13, 1, 1}, {13, 2, 1}, {13, 3, 1},
        {10, 0, 1}, {10, 1, 1}, {10, 2, 1},
    };
    Card hidden[21];
    int col, rank, suit, n = 0, next = 0;
    g.draw_mode = mode;
    g.phase = PHASE_PLAY;
    for (col = 0; col < 7; col++) {
        used[tops[col].suit][tops[col].rank] = true;
    }
    for (suit = 0; suit < 4; suit++) {
        // All Aces stay buried, never reach a foundation. All Queens stay
        // under Kings, and all Nines stay under Tens.
        const int blocked_ranks[] = {1, 9, 12};
        int i;
        for (i = 0; i < 3; i++) {
            rank = blocked_ranks[i];
            if (used[suit][rank]) FAIL("dead-end visible top conflicts with a blocked rank");
            used[suit][rank] = true;
            hidden[n++] = card(rank, suit, 0);
        }
    }
    for (suit = 0; suit < 4; suit++) {
        for (rank = 1; rank <= 13; rank++) {
            if (used[suit][rank]) continue;
            used[suit][rank] = true;
            if (n < 21) hidden[n++] = card(rank, suit, 0);
            else push_card(&g.stock, card(rank, suit, 0));
        }
    }
    if (n != 21 || g.stock.count != 24)
        FAIL("standard stock dead-end fixture does not split 21/24 cards");
    for (col = 0; col < 7; col++) {
        int hidden_count = col; // column heights are exactly 1..7
        int i;
        for (i = 0; i < hidden_count; i++)
            push_card(&g.tableau[col], hidden[next++]);
        push_card(&g.tableau[col], tops[col]);
    }
    return g;
}

static SolverStatus finish_search(Solver *solver) {
    unsigned calls;
    for (calls = 0; calls < 10000; calls++) {
        SolverStatus status = solver_step(solver, 256);
        if (status != SOLVER_CHECKING) return status;
    }
    FAIL("search did not finish within the test's bounded number of slices");
    return SOLVER_UNKNOWN;
}

// This test translation unit includes solver.c directly, which lets the
// uncached-search regressions isolate the public search API from certificates
// inserted earlier in the same process. No solver may be alive when called.
static void clear_solver_proof_cache(void) {
    size_t i;
    for (i = 0; i < CACHE_MAX_ENTRIES; i++) {
        if (g_cache[i].used) bundle_release(g_cache[i].bundle);
        memset(&g_cache[i], 0, sizeof(g_cache[i]));
    }
    memset(g_cache_index, 0, sizeof(g_cache_index));
    g_cache_next = 0;
    g_cache_mutations = 0;
    if (g_bundle_bytes != 0) FAIL("proof cache reset left a referenced bundle behind");
}

static SolverStatus finish_known_winnable_search(Solver *solver) {
    unsigned calls;
    for (calls = 0; calls < 10000; calls++) {
        SolverStatus status = solver_step(solver, 256);
        if (status == SOLVER_UNWINNABLE)
            FAIL("complete search falsely rejected a position derived from a verified win");
        if (status != SOLVER_CHECKING) return status;
    }
    FAIL("known-winnable midgame search did not finish within the test bound");
    return SOLVER_UNKNOWN;
}

static Game make_uncached_certified_midgame(DrawMode mode, unsigned choice,
                                            size_t prefix_moves) {
    Game game = {0};
    Solver *certificate;
    size_t i, length;
    if (!certified_deal(&game, mode, choice))
        FAIL("could not make deterministic certified opening for midgame regression");
    certificate = solver_create(&game);
    if (!certificate || solver_status(certificate) != SOLVER_WINNABLE)
        FAIL("certified opening did not provide its verified winning line");
    length = solver_solution_length(certificate);
    if (length <= prefix_moves)
        FAIL("certificate is too short to make the requested midgame fixture");
    for (i = 0; i < prefix_moves; i++) {
        SolverMove move;
        if (!solver_solution_move(certificate, i, &move) ||
            !solver_apply_move(&game, &move))
            FAIL("verified certificate prefix was rejected by the move engine");
    }
    solver_destroy(certificate);
    if (game.phase != PHASE_PLAY)
        FAIL("midgame fixture reached the win before the selected prefix endpoint");
    clear_solver_proof_cache();
    return game;
}

static void assert_standard_opening(const Game *g) {
    bool seen[4][14] = {{false}};
    int total = 0, col, i, suit, rank;
    if (g->stock.count != 24 || g->waste.count != 0)
        FAIL("certified opening has the wrong stock/waste counts");
    for (i = 0; i < 4; i++)
        if (g->foundation[i].count != 0) FAIL("opening deal put a card on a foundation");
    for (col = 0; col < 7; col++) {
        const Pile *p = &g->tableau[col];
        if (p->count != col + 1) FAIL("opening tableau is not the standard 1..7 deal");
        for (i = 0; i < p->count; i++) {
            Card c = p->cards[i];
            if (c.face_up != (i == p->count - 1))
                FAIL("opening tableau did not expose exactly its top card");
            if (seen[c.suit][c.rank]) FAIL("opening has a duplicated card");
            seen[c.suit][c.rank] = true;
            total++;
        }
    }
    for (i = 0; i < g->stock.count; i++) {
        Card c = g->stock.cards[i];
        if (c.face_up) FAIL("opening stock card was face up");
        if (seen[c.suit][c.rank]) FAIL("opening has a duplicated stock card");
        seen[c.suit][c.rank] = true;
        total++;
    }
    if (total != 52) FAIL("opening does not contain all 52 cards");
    for (suit = 0; suit < 4; suit++)
        for (rank = 1; rank <= 13; rank++)
            if (!seen[suit][rank]) FAIL("opening is missing a card");
}

static void test_every_bundled_certificate_replays_in_both_modes(void) {
    DrawMode modes[] = {DRAW_ONE, DRAW_THREE};
    size_t mi;
    size_t proof_positions = 0;
    for (mi = 0; mi < sizeof(modes) / sizeof(modes[0]); mi++) {
        DrawMode mode = modes[mi];
        size_t count = certified_deal_count(mode), choice;
        if (count == 0) FAIL("certificate bank is empty for a draw mode");
        for (choice = 0; choice < count; choice++) {
            Game deal = {0}, replay;
            Solver *proof;
            size_t n, step;
            if (!certified_deal(&deal, mode, (unsigned)choice)) {
                fprintf(stderr, "FAIL: solver — certificate replay failed for mode=%d choice=%zu/%zu\n",
                        (int)mode, choice, count);
                exit(1);
            }
            if (deal.draw_mode != mode) FAIL("bank returned the wrong draw mode");
            assert_standard_opening(&deal);

            // certified_deal verifies and seeds its proof path into the same
            // exact-position cache used by the app. Replay that cached line
            // independently through solver_apply_move/game_move/game_draw.
            proof = solver_create(&deal);
            if (!proof || solver_status(proof) != SOLVER_WINNABLE)
                FAIL("verified opening did not expose a cached winning proof");
            n = solver_solution_length(proof);
            if (n == 0) FAIL("opening certificate unexpectedly contains no moves");
            proof_positions += n + 1;
            replay = deal;
            note_stress_position(&replay);
            for (step = 0; step < n; step++) {
                SolverMove move;
                if (!solver_solution_move(proof, step, &move))
                    FAIL("certificate ended before its declared move count");
                if (!solver_apply_move(&replay, &move))
                    FAIL("certificate contains a move rejected by actual game rules");
                note_stress_position(&replay);
            }
            if (replay.phase != PHASE_WON)
                FAIL("certificate replay ended before all four foundations were complete");
            if (solver_solution_move(proof, n, &(SolverMove){0}))
                FAIL("cached proof exposed a move beyond its declared endpoint");
            solver_destroy(proof);
        }
    }
    if (proof_positions <= 8192)
        FAIL("certificate corpus did not exercise more positions than the proof-cache index capacity");
    if (stress_unique_positions <= 8192)
        FAIL("certificate corpus did not contain enough distinct states to pressure the proof-cache index");
    PASS("solver_all_bundled_certificates_both_modes");
}

static void test_proof_cache_eviction_pressure(void) {
    DrawMode modes[] = {DRAW_ONE, DRAW_THREE};
    size_t mi;
    int pass;
    // Reinsert the full certified corpus in the opposite order after the first
    // test has already overflowed the bounded FIFO. This drives repeated slot
    // eviction, tombstone reuse and periodic hash-index rebuilds. Every just-
    // verified opening must still be discoverable as WINNABLE immediately.
    for (pass = 0; pass < 2; pass++) {
        for (mi = 0; mi < sizeof(modes) / sizeof(modes[0]); mi++) {
            DrawMode mode = modes[mi];
            size_t count = certified_deal_count(mode), n;
            for (n = 0; n < count; n++) {
                size_t choice = pass == 0 ? n : count - 1 - n;
                Game deal = {0};
                Solver *solver;
                if (!certified_deal(&deal, mode, (unsigned)choice))
                    FAIL("certificate verification failed under proof-cache eviction pressure");
                solver = solver_create(&deal);
                if (!solver || solver_status(solver) != SOLVER_WINNABLE ||
                    solver_solution_length(solver) == 0)
                    FAIL("newly verified proof was not found after cache eviction/index churn");
                solver_destroy(solver);
            }
        }
    }
    PASS("solver_proof_cache_eviction_pressure");
}

static void test_uncached_midgame_search_never_false_loses_and_replays(void) {
    typedef struct {
        DrawMode mode;
        unsigned choice;
        size_t prefix_moves;
        size_t small_cap;
    } MidgameFixture;
    // This is 86 legal actions into a deterministic 172-action certified
    // DRAW_ONE solution; the second is 15 actions into a certified DRAW_THREE
    // solution. Their exact states are uncached before each search. The old
    // depth-first order exhausted its useful work without proving the first
    // position. Best-first ordering must solve both without ever calling a
    // bounded partial graph unwinnable.
    const MidgameFixture fixtures[] = {
        {DRAW_ONE, 29, 86, 1000},
        {DRAW_THREE, 0, 15, 50},
    };
    size_t fixture_index;
    for (fixture_index = 0;
         fixture_index < sizeof(fixtures) / sizeof(fixtures[0]);
         fixture_index++) {
        const size_t full_cap = 100000;
        const MidgameFixture *fixture = &fixtures[fixture_index];
        Game midgame = make_uncached_certified_midgame(
            fixture->mode, fixture->choice, fixture->prefix_moves);
        Solver *capped = solver_create_limited(&midgame, fixture->small_cap);
        Solver *solver;
        Game replay;
        size_t length, i;

        if (!capped || solver_status(capped) != SOLVER_CHECKING)
            FAIL("midgame fixture was not uncached when the bounded search began");
        if (finish_search(capped) != SOLVER_UNKNOWN)
            FAIL("small state cap did not leave the known-winnable midgame unknown");
        if (solver_status(capped) == SOLVER_UNWINNABLE)
            FAIL("a partial midgame graph was incorrectly called unwinnable");
        solver_destroy(capped);

        solver = solver_create_limited(&midgame, full_cap);
        if (!solver || solver_status(solver) != SOLVER_CHECKING)
            FAIL("full search did not start from an uncached legal midgame");
        if (solver_step(solver, 1) == SOLVER_UNWINNABLE)
            FAIL("one partial expansion falsely proved a known-winnable midgame lost");
        if (finish_known_winnable_search(solver) != SOLVER_WINNABLE)
            FAIL("uncached legal midgame was not proved winnable within 100,000 states");

        length = solver_solution_length(solver);
        if (length == 0) FAIL("uncached midgame proof did not include a replayable line");
        replay = midgame;
        for (i = 0; i < length; i++) {
            SolverMove move;
            if (!solver_solution_move(solver, i, &move))
                FAIL("midgame proof ended before its declared move count");
            if (!solver_apply_move(&replay, &move))
                FAIL("midgame proof contains a move rejected by the shared game rules");
        }
        if (replay.phase != PHASE_WON)
            FAIL("midgame proof replay did not complete all four foundations");
        if (solver_solution_move(solver, length, &(SolverMove){0}))
            FAIL("midgame proof exposed a move beyond its declared endpoint");
        solver_destroy(solver);
    }
    PASS("solver_uncached_midgame_replay_both_modes");
}

static void test_position_identity_is_exact_and_ignores_bookkeeping(void) {
    Game same, changed;
    same = make_adjacent_column_count_pair();
    same.draw_mode = DRAW_THREE;
    if (!game_draw(&same) || !game_draw(&same))
        FAIL("could not expose stock and waste for identity checks");
    changed = same;
    changed.score += 11;
    changed.timer_frames += 60;
    changed.time_penalty_steps++;
    changed.moves += 4;
    changed.stock_passes++;
    changed.waste_drawn = (changed.waste_drawn + 1) % 4;
    changed.events ^= EV_DRAW;
    if (!solver_same_position(&same, &changed))
        FAIL("score, time, move bookkeeping or frame events changed the puzzle identity");

    changed = same;
    if (changed.stock.count < 2) FAIL("identity fixture lacks two stock cards");
    Card swap = changed.stock.cards[0];
    changed.stock.cards[0] = changed.stock.cards[1];
    changed.stock.cards[1] = swap;
    if (solver_same_position(&same, &changed)) FAIL("stock order was omitted from position identity");

    changed = same;
    changed.draw_mode = DRAW_ONE;
    if (solver_same_position(&same, &changed)) FAIL("draw mode was omitted from position identity");

    Game face_pair = make_closed_cycle(DRAW_ONE);
    changed = face_pair;
    // The hidden 7♥ is immediately below the valid 6♠-5♥ run. Turning it
    // face up preserves a legal visible run while changing only a face flag.
    changed.tableau[0].cards[changed.tableau[0].count - 3].face_up = 1;
    if (solver_same_position(&face_pair, &changed))
        FAIL("face-up state was omitted from position identity");

    changed = same;
    if (changed.waste.count < 2) FAIL("identity fixture lacks two waste cards");
    swap = changed.waste.cards[0];
    changed.waste.cards[0] = changed.waste.cards[1];
    changed.waste.cards[1] = swap;
    if (solver_same_position(&same, &changed)) FAIL("waste order was omitted from position identity");

    // Moving Q♠ from the last column onto K♥ in the previous column keeps the
    // flattened card and face-up streams identical. Only the two neighboring
    // pile counts distinguish these valid positions.
    Game count_pair = make_adjacent_column_count_pair();
    changed = count_pair;
    if (!game_move(&changed, LOC_TABLEAU, 6, 0, LOC_TABLEAU, 5))
        FAIL("count-pair fixture has no legal Queen-on-King move");
    if (solver_same_position(&count_pair, &changed))
        FAIL("neighboring pile boundaries were omitted from position identity");

    PASS("solver_position_identity");
}

static void test_cycle_exhaustion_proves_loss(void) {
    Game g = make_closed_cycle(DRAW_ONE);
    Solver *solver = solver_create_limited(&g, 8);
    if (!solver) FAIL("valid cycle fixture was rejected");
    if (solver_status(solver) != SOLVER_CHECKING) FAIL("new search did not start checking");
    if (finish_search(solver) != SOLVER_UNWINNABLE)
        FAIL("exhausted reversible cycle was not proved unwinnable");
    if (solver_state_count(solver) != 2)
        FAIL("cycle states were not deduplicated by exact puzzle position");
    solver_destroy(solver);
    PASS("solver_cycle_exhaustion");
}

static void test_standard_stock_recycle_is_ordered_and_exhaustive(void) {
    DrawMode modes[] = {DRAW_ONE, DRAW_THREE};
    size_t mi;
    for (mi = 0; mi < sizeof(modes) / sizeof(modes[0]); mi++) {
        DrawMode mode = modes[mi];
        Game start = make_standard_stock_deadend(mode), replay = start;
        SolverMove draw = {SOLVER_MOVE_DRAW, LOC_STOCK, 0, 0, LOC_STOCK, 0};
        Solver *solver;
        int expected = mode == DRAW_ONE ? 25 : 9;
        if (!validate_game(&start)) FAIL("standard stock dead-end is structurally invalid");

        if (!solver_apply_move(&replay, &draw)) FAIL("solver draw action did not use game_draw");
        if (replay.stock.count != (mode == DRAW_ONE ? 23 : 21) ||
            replay.waste.count != (mode == DRAW_ONE ? 1 : 3))
            FAIL("initial stock draw count/order differs from the selected rule");
        while (replay.stock.count > 0)
            if (!solver_apply_move(&replay, &draw)) FAIL("solver could not complete stock pass");
        if (replay.waste.count != 24) FAIL("stock pass did not expose all 24 stock cards");
        if (!solver_apply_move(&replay, &draw)) FAIL("solver did not recycle the full waste pile");
        if (replay.stock.count != 24 || replay.waste.count != 0 || replay.stock_passes != 1)
            FAIL("waste recycle did not restore stock and increment the pass");
        for (int i = 0; i < replay.stock.count; i++)
            if (replay.stock.cards[i].face_up) FAIL("recycled stock card remained face up");
        if (!solver_same_position(&start, &replay))
            FAIL("one complete stock pass did not restore the exact card order");

        solver = solver_create_limited(&start, 64);
        if (!solver) FAIL("stock-only cycle position was rejected");
        if (finish_search(solver) != SOLVER_UNWINNABLE)
            FAIL("complete stock-cycle graph was not exhausted as unwinnable");
        if (solver_state_count(solver) != (size_t)expected)
            FAIL("stock draw/recycle graph has missing, duplicated or misordered states");
        solver_destroy(solver);
    }
    PASS("solver_standard_stock_recycle_draw_one_and_three");
}

static void test_budget_and_cancel_are_unknown(void) {
    Game deal = make_closed_cycle(DRAW_ONE);
    Solver *solver;

    solver = solver_create_limited(&deal, 1);
    if (!solver) FAIL("valid opening deal was rejected");
    if (solver_step(solver, 0) != SOLVER_CHECKING)
        FAIL("zero expansion budget should leave the search unresolved");
    if (solver_step(solver, 1) != SOLVER_UNKNOWN)
        FAIL("state-cap exhaustion was reported as a proof instead of unknown");
    if (solver_status(solver) == SOLVER_UNWINNABLE)
        FAIL("a bounded partial graph was incorrectly called unwinnable");
    solver_destroy(solver);

    solver = solver_create(&deal);
    if (!solver) FAIL("could not create cancellable solver");
    solver_cancel(solver);
    if (solver_status(solver) != SOLVER_UNKNOWN)
        FAIL("cancellation was not reported as unknown");
    if (solver_step(solver, 1000) != SOLVER_UNKNOWN)
        FAIL("cancelled search resumed instead of remaining unknown");
    solver_destroy(solver);
    PASS("solver_budget_cancel_unknown");
}

static void test_invalid_and_unwinnable_are_distinct(void) {
    Game incomplete = {0};
    Solver *solver;
    if (solver_create(&incomplete) != NULL)
        FAIL("an incomplete deck was accepted as a search position");

    Game cycle = make_closed_cycle(DRAW_ONE);
    // A valid but unresolved root must not be called a loss before it is
    // expanded; invalid input is represented by NULL instead.
    solver = solver_create_limited(&cycle, 8);
    if (!solver) FAIL("valid cycle deal was rejected after invalid input");
    if (solver_status(solver) != SOLVER_CHECKING)
        FAIL("valid but unresolved deal did not remain checking");
    solver_destroy(solver);
    PASS("solver_invalid_is_not_loss");
}

static void test_foundation_takeback_uses_game_rules(void) {
    Game g = make_takeback_position();
    SolverMove move = {
        SOLVER_MOVE_CARD, LOC_FOUNDATION, 0, 0, LOC_TABLEAU, 0
    };
    Solver *validator = solver_create_limited(&g, 1);
    if (!validator) FAIL("take-back fixture is structurally invalid");
    solver_destroy(validator);
    if (!game_can_drop(&g, LOC_FOUNDATION, 0, 0, LOC_TABLEAU, 0))
        FAIL("engine rejected the legal Ace-on-red-2 take-back");
    if (!solver_apply_move(&g, &move)) FAIL("solver adapter rejected a legal foundation take-back");
    if (g.foundation[0].count != 0 || g.tableau[0].count == 0)
        FAIL("foundation take-back did not relocate the card");
    Card ace = g.tableau[0].cards[g.tableau[0].count - 1];
    if (ace.rank != 1 || ace.suit != 2 || !ace.face_up)
        FAIL("foundation take-back did not preserve the face-up Ace of hearts");

    // The game routes an Ace to the first empty foundation, which may not
    // correspond to the card's suit. The solver must accept this real state.
    if (!game_auto_move(&g, LOC_TABLEAU, 0, g.tableau[0].count - 1))
        FAIL("engine could not return the Ace to its first empty foundation");
    if (g.foundation[0].count != 1 || g.foundation[0].cards[0].suit != 2)
        FAIL("engine did not place hearts in the first empty foundation slot");
    validator = solver_create_limited(&g, 1);
    if (!validator) FAIL("solver rejected the engine's arbitrary-suit foundation slot");
    solver_destroy(validator);

    move.to_kind = LOC_WASTE;
    if (solver_apply_move(&g, &move)) FAIL("solver adapter accepted an illegal waste target");
    move.to_kind = LOC_FOUNDATION;
    move.from_index = 9;
    if (solver_apply_move(&g, &move)) FAIL("solver adapter accepted an out-of-range foundation index");
    PASS("solver_foundation_takeback");
}

int main(void) {
    test_every_bundled_certificate_replays_in_both_modes();
    test_proof_cache_eviction_pressure();
    test_uncached_midgame_search_never_false_loses_and_replays();
    test_position_identity_is_exact_and_ignores_bookkeeping();
    test_cycle_exhaustion_proves_loss();
    test_standard_stock_recycle_is_ordered_and_exhaustive();
    test_budget_and_cancel_are_unknown();
    test_invalid_and_unwinnable_are_distinct();
    test_foundation_takeback_uses_game_rules();
    return 0;
}
