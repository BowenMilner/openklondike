#ifndef OPENKLONDIKE_SOLVER_H
#define OPENKLONDIKE_SOLVER_H

#include "game.h"
#include <stddef.h>
#include <stdint.h>

typedef enum {
    SOLVER_CHECKING = 0,
    SOLVER_WINNABLE,
    SOLVER_UNWINNABLE,
    SOLVER_UNKNOWN
} SolverStatus;

typedef enum {
    SOLVER_MOVE_DRAW = 0,
    SOLVER_MOVE_CARD = 1
} SolverMoveType;

// DRAW calls game_draw (which also recycles when the stock is empty). CARD
// identifies the source card index and destination pile. Tableau runs are
// represented by their bottom card index, exactly as game_move expects.
typedef struct {
    uint8_t type;
    uint8_t from_kind;
    uint8_t from_index;
    uint8_t card_index;
    uint8_t to_kind;
    uint8_t to_index;
} SolverMove;

typedef struct Solver Solver;

// Starts a complete-graph search. Invalid inputs return NULL. The default
// state cap is deliberately bounded for mobile memory; reaching it is UNKNOWN.
Solver *solver_create(const Game *snapshot);
// Testing/embedding variant. max_states==0 selects the documented default.
Solver *solver_create_limited(const Game *snapshot, size_t max_states);
void solver_cancel(Solver *solver);
void solver_destroy(Solver *solver);
SolverStatus solver_step(Solver *solver, unsigned expansion_budget);
SolverStatus solver_status(const Solver *solver);
size_t solver_solution_length(const Solver *solver);
bool solver_solution_move(const Solver *solver, size_t index, SolverMove *out);
size_t solver_state_count(const Solver *solver);

// Applies one solver action using the same game rules used by the UI.
bool solver_apply_move(Game *game, const SolverMove *move);
// Verifies a complete winning line against actual game rules and caches every
// exact intermediate position, allowing the app to recognize proof-path moves.
bool solver_verify_and_cache(const Game *start, const SolverMove *moves,
                             size_t move_count);
// Exact game-position equality used by the proof cache (ignores score/timer).
bool solver_same_position(const Game *a, const Game *b);

#endif
