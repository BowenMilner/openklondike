// Platform-independent session adapter for the native UIKit presentation.
#include "native_session.h"
#include "certified_deals.h"
#include "history.h"
#include "tick.h"
#include <limits.h>
#include <math.h>
#include <stdlib.h>

struct OKSession {
    Game game;
    Solver *solver;
    History history;
    SimClock clock;
    bool thorough;
};

static void mark_proof(OKSession *session) {
    if (solver_status(session->solver) == SOLVER_WINNABLE)
        history_mark_winnable(&session->history, &session->game);
}

static void analyze(OKSession *session, bool thorough) {
    session->thorough = thorough;
    solver_destroy(session->solver);
    session->solver = solver_create_limited(&session->game, thorough ? 500000 : 0);
    mark_proof(session);
}

OKSession *ok_session_create(DrawMode mode, unsigned choice) {
    OKSession *session = calloc(1, sizeof(*session));
    if (session && !ok_session_new(session, mode, choice)) {
        ok_session_destroy(session);
        return NULL;
    }
    return session;
}

void ok_session_destroy(OKSession *session) {
    if (!session) return;
    solver_destroy(session->solver);
    free(session);
}

const Game *ok_session_game(const OKSession *session) {
    return session ? &session->game : NULL;
}

bool ok_session_new(OKSession *session, DrawMode mode, unsigned choice) {
    Game next;
    if (!session || (mode != DRAW_ONE && mode != DRAW_THREE) ||
        !certified_deal(&next, mode, choice)) return false;
    Solver *solver = solver_create(&next);
    if (!solver || solver_status(solver) != SOLVER_WINNABLE) {
        solver_destroy(solver);
        return false;
    }
    solver_destroy(session->solver);
    session->solver = solver;
    session->game = next;
    session->thorough = false;
    history_clear(&session->history);
    sim_clock_reset(&session->clock);
    mark_proof(session);
    return true;
}

bool ok_session_load(OKSession *session, const Game *snapshot) {
    if (!session || !snapshot) return false;
    // Corrupt save counters must not overflow or cause unbounded scoring work.
    if (snapshot->score < 0 || snapshot->score > INT_MAX - 1000 ||
        snapshot->moves < 0 || snapshot->moves > INT_MAX - 1000 ||
        snapshot->stock_passes < 0 || snapshot->stock_passes > INT_MAX - 1000 ||
        snapshot->timer_frames < 0 || snapshot->timer_frames > INT_MAX - 600 ||
        snapshot->time_penalty_steps != snapshot->timer_frames / 600 ||
        snapshot->waste_drawn < 0 || snapshot->waste_drawn > 3) return false;
    // The solver validates every pile, all 52 unique cards and game invariants
    // before allocating its search. No live state changes on rejected saves.
    Solver *solver = solver_create(snapshot);
    if (!solver) return false;
    solver_destroy(session->solver);
    session->solver = solver;
    session->game = *snapshot;
    session->thorough = false;
    session->game.events = 0;
    history_clear(&session->history);
    sim_clock_reset(&session->clock);
    mark_proof(session);
    return true;
}

void ok_session_step(OKSession *session, double elapsed_seconds) {
    if (!session) return;
    if (!isfinite(elapsed_seconds)) elapsed_seconds = 0;
    int steps = sim_clock_advance(&session->clock, elapsed_seconds);
    for (int i = 0; i < steps; i++) {
        if (session->game.timer_frames < INT_MAX - 1) game_tick(&session->game);
    }
    if (solver_status(session->solver) == SOLVER_CHECKING) {
        // Keep UI input responsive; the state cap always means UNKNOWN.
        solver_step(session->solver, 32);
        mark_proof(session);
    }
}

SolverStatus ok_session_status(const OKSession *session) {
    return session ? solver_status(session->solver) : SOLVER_UNKNOWN;
}

static bool source_valid(PileKind kind, int index) {
    return (kind == LOC_WASTE && index == 0) ||
        (kind == LOC_FOUNDATION && index >= 0 && index < 4) ||
        (kind == LOC_TABLEAU && index >= 0 && index < 7);
}

static bool destination_valid(PileKind kind, int index) {
    return (kind == LOC_FOUNDATION && index >= 0 && index < 4) ||
        (kind == LOC_TABLEAU && index >= 0 && index < 7);
}

static bool commit(OKSession *session, const Game *next) {
    history_push(&session->history, &session->game);
    session->game = *next;
    analyze(session, false);
    return true;
}

bool ok_session_draw(OKSession *session) {
    if (!session) return false;
    Game next = session->game;
    game_frame_begin(&next);
    if (!game_draw(&next)) return false;
    return commit(session, &next);
}

bool ok_session_auto_move(OKSession *session, PileKind kind, int index, int card) {
    if (!session || !source_valid(kind, index)) return false;
    Game next = session->game;
    game_frame_begin(&next);
    if (!game_auto_move(&next, kind, index, card)) return false;
    return commit(session, &next);
}

bool ok_session_move(OKSession *session, PileKind kind, int index, int card,
                     PileKind destination, int destination_index) {
    if (!session || !source_valid(kind, index) ||
        !destination_valid(destination, destination_index)) return false;
    Game next = session->game;
    game_frame_begin(&next);
    if (!game_move(&next, kind, index, card, destination, destination_index)) return false;
    return commit(session, &next);
}

bool ok_session_can_undo(const OKSession *session) {
    return session && session->history.count > 0;
}

bool ok_session_can_restore(const OKSession *session) {
    return session && session->history.has_winnable;
}

bool ok_session_undo(OKSession *session) {
    if (!session || !history_undo(&session->history, &session->game)) return false;
    sim_clock_reset(&session->clock);
    analyze(session, false);
    return true;
}

bool ok_session_restore(OKSession *session) {
    if (!session || !history_restore_winnable(&session->history, &session->game)) return false;
    sim_clock_reset(&session->clock);
    analyze(session, false);
    return true;
}

void ok_session_check(OKSession *session) {
    if (session && !session->thorough) analyze(session, true);
}

bool ok_session_hint(const OKSession *session, SolverMove *move) {
    return session && solver_solution_move(session->solver, 0, move);
}

size_t ok_session_search_states(const OKSession *session) {
    return session ? solver_state_count(session->solver) : 0;
}

bool ok_session_can_check_deeper(const OKSession *session) {
    return session && !session->thorough && solver_status(session->solver) == SOLVER_UNKNOWN;
}
