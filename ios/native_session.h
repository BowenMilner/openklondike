#ifndef STILL_SOLVABLE_NATIVE_SESSION_H
#define STILL_SOLVABLE_NATIVE_SESSION_H
#ifdef __cplusplus
extern "C" {
#endif
#include "game.h"
#include "solver.h"
typedef struct OKSession OKSession;
OKSession *ok_session_create(DrawMode mode, unsigned choice);
void ok_session_destroy(OKSession *session);
const Game *ok_session_game(const OKSession *session);
// Rendering/persistence snapshot; face-down rank/suit are solver-private and
// must never appear in labels, accessibility, hints, screenshots or logging.
bool ok_session_new(OKSession *session, DrawMode mode, unsigned choice);
bool ok_session_load(OKSession *session, const Game *snapshot);
void ok_session_step(OKSession *session, double elapsed_seconds);
SolverStatus ok_session_status(const OKSession *session);
bool ok_session_draw(OKSession *session);
bool ok_session_auto_move(OKSession *session, PileKind kind, int index, int card);
bool ok_session_move(OKSession *session, PileKind kind, int index, int card,
                     PileKind destination, int destination_index);
bool ok_session_can_undo(const OKSession *session);
bool ok_session_can_restore(const OKSession *session);
bool ok_session_undo(OKSession *session);
bool ok_session_restore(OKSession *session);
void ok_session_check(OKSession *session);
// Hints reveal only the next legal action, never hidden card identities.
bool ok_session_hint(const OKSession *session, SolverMove *move);
#ifdef __cplusplus
}
#endif
#endif
