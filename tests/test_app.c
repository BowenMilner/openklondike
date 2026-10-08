// App-loop integration tests with platform services stubbed at their public
// seams. The real main.c frame logic, game rules, solver, history and clock run.
#define PLATFORM_IOS 1
#include "../src/main.c"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FAIL(msg) do { fprintf(stderr, "FAIL: app — %s\n", msg); exit(1); } while (0)
#define PASS(name) printf("PASS: %s\n", name)

static Input next_input;
static bool stock_hit;
static bool fake_card_hit;
static SolverStatus rendered_solver_status;
static double mock_time;
static bool mock_sound_enabled;

Input input_poll(void) {
    Input result = next_input;
    memset(&next_input, 0, sizeof(next_input));
    return result;
}

double GetTime(void) { mock_time += 0.001; return mock_time; }

void render_set_solver_status(SolverStatus status) { rendered_solver_status = status; }
bool render_menu_button_hit(int x, int y) { (void)x; (void)y; return false; }
void render_init(void) {}
void render_cleanup(void) {}
void render_frame(const Game *g, const DragState *d) { (void)g; (void)d; }
void render_menu(const char *t, const char *const *l, int n, int s, int gap) {
    (void)t; (void)l; (void)n; (void)s; (void)gap;
}
void render_bounce_begin(const Game *g) { (void)g; }
bool render_bounce_step(int steps) { (void)steps; return false; }
void render_bounce_end(void) {}
bool render_hit(const Game *g, int x, int y, PileKind *kind, int *index, int *card_index) {
    (void)x; (void)y;
    if (!fake_card_hit || !g || g->tableau[0].count == 0) return false;
    *kind = LOC_TABLEAU; *index = 0; *card_index = g->tableau[0].count - 1;
    return true;
}
bool render_stock_hit(int x, int y) { (void)x; (void)y; return stock_hit; }
bool render_card_pos(const Game *g, PileKind k, int i, int c, int *x, int *y) {
    (void)g; (void)k; (void)i; (void)c; (void)x; (void)y; return false;
}
bool render_drop_target(const Game *g, const DragState *d, PileKind *k, int *i) {
    (void)g; (void)d; (void)k; (void)i; return false;
}
int render_card_width(void) { return 80; }
void render_set_scaled(bool scaled) { (void)scaled; }
bool render_use_scaled(void) { return true; }
int menu_hit_test(Vector2 p) { (void)p; return -1; }

void sound_init(void) {}
void sound_shutdown(void) {}
bool sound_is_enabled(void) { return mock_sound_enabled; }
void sound_toggle(void) { mock_sound_enabled = !mock_sound_enabled; }
void sound_play(SfxId id) { (void)id; }

void window_init(const char *title) { (void)title; }
void window_close(void) {}
bool window_should_close(void) { return false; }
void window_toggle_fullscreen(void) {}
bool window_focus_lost(void) { return false; }

bool recorder_start(const char *path) { (void)path; return false; }
void recorder_stop(void) {}
bool recorder_toggle(void) { return false; }
bool recorder_active(void) { return false; }

static void push(Pile *pile, Card c) { pile->cards[pile->count++] = c; }

static SolverStatus finish_app_search(Solver *solver) {
    unsigned calls;
    for (calls = 0; calls < 10000; calls++) {
        SolverStatus status = solver_step(solver, 256);
        if (status != SOLVER_CHECKING) return status;
    }
    FAIL("app test search did not finish within its bounded slices");
    return SOLVER_UNKNOWN;
}

static Game solved_position(DrawMode mode) {
    Game g = {0};
    int suit, rank;
    g.draw_mode = mode;
    g.phase = PHASE_PLAY;
    for (suit = 0; suit < 4; suit++)
        for (rank = 1; rank <= 13; rank++)
            push(&g.foundation[suit], (Card){(uint8_t)rank, (uint8_t)suit, 1});
    return g;
}

static Game stock_deadend(DrawMode mode) {
    Game g = {0};
    bool used[4][14] = {{false}};
    Card tops[7] = {
        {13, 0, 1}, {13, 1, 1}, {13, 2, 1}, {13, 3, 1},
        {10, 0, 1}, {10, 1, 1}, {10, 2, 1},
    };
    Card hidden[21];
    int col, rank, suit, hidden_n = 0, cursor = 0;
    g.draw_mode = mode;
    g.phase = PHASE_PLAY;
    for (col = 0; col < 7; col++) used[tops[col].suit][tops[col].rank] = true;
    for (suit = 0; suit < 4; suit++) {
        const int blocked[] = {1, 9, 12};
        for (int i = 0; i < 3; i++) {
            rank = blocked[i];
            used[suit][rank] = true;
            hidden[hidden_n++] = (Card){(uint8_t)rank, (uint8_t)suit, 0};
        }
    }
    for (suit = 0; suit < 4; suit++) {
        for (rank = 1; rank <= 13; rank++) {
            if (used[suit][rank]) continue;
            used[suit][rank] = true;
            Card c = {(uint8_t)rank, (uint8_t)suit, 0};
            if (hidden_n < 21) hidden[hidden_n++] = c;
            else push(&g.stock, c);
        }
    }
    if (hidden_n != 21 || g.stock.count != 24) FAIL("bad app dead-end fixture");
    for (col = 0; col < 7; col++) {
        for (int i = 0; i < col; i++) push(&g.tableau[col], hidden[cursor++]);
        push(&g.tableau[col], tops[col]);
    }
    return g;
}

static void dispose_context(void) {
    solver_destroy(ios_ctx.solver);
    if (ios_ctx.game) game_destroy(ios_ctx.game);
    memset(&ios_ctx, 0, sizeof(ios_ctx));
    memset(&next_input, 0, sizeof(next_input));
    stock_hit = false;
    fake_card_hit = false;
    mock_time = 0.0;
    rendered_solver_status = SOLVER_UNKNOWN;
    ios_initialized = false;
    app_init();
}

static void attach_game(const Game *g) {
    ios_ctx.game = (Game *)malloc(sizeof(*ios_ctx.game));
    if (!ios_ctx.game) FAIL("out of memory in app harness");
    *ios_ctx.game = *g;
    ios_ctx.state = STATE_PLAYING;
}

static void test_new_game_uses_certified_bank_for_both_modes(void) {
    DrawMode modes[] = {DRAW_ONE, DRAW_THREE};
    for (size_t i = 0; i < sizeof(modes) / sizeof(modes[0]); i++) {
        dispose_context();
        ios_ctx.draw_mode = modes[i];
        next_input.select_pressed = true; // New Game is the first menu item.
        app_frame();
        if (!ios_ctx.game) FAIL("New Game failed to produce a deal");
        if (ios_ctx.game->draw_mode != modes[i]) FAIL("New Game ignored the selected draw rule");
        if (ios_ctx.game->phase != PHASE_PLAY) FAIL("New Game returned a finished game");
        if (ios_ctx.proof_status != SOLVER_WINNABLE || !ios_ctx.history.has_winnable)
            FAIL("New Game was displayed before its cached winning certificate was available");
    }
    PASS("app_new_game_certified_both_modes");
}

static void test_move_invalidates_stale_proof_and_proven_loss_warns(void) {
    Game solved = solved_position(DRAW_ONE);
    Game dead = stock_deadend(DRAW_ONE);
    dispose_context();
    attach_game(&solved);
    analyze_position(&ios_ctx, false);
    if (ios_ctx.proof_status != SOLVER_WINNABLE) FAIL("solved baseline was not recognized as winnable");
    if (!ios_ctx.history.has_winnable) FAIL("winnable position was not recorded for recovery");

    // Simulate a just-mutated live board while the old proof still says win.
    // The ensuing real stock tap must create a fresh analysis for the changed
    // position instead of carrying over that stale result.
    *ios_ctx.game = dead;
    if (ios_ctx.proof_status != SOLVER_WINNABLE) FAIL("test did not establish the stale-proof precondition");
    stock_hit = true;
    next_input.left_pressed = true;
    app_frame();
    stock_hit = false;
    if (ios_ctx.proof_status == SOLVER_WINNABLE)
        FAIL("stock action left the stale winning result attached to a different position");
    if (ios_ctx.history.count != 1) FAIL("stock action was not recorded for undo");

    for (int i = 0; i < 8 && ios_ctx.state != STATE_WARNING; i++) app_frame();
    if (ios_ctx.proof_status != SOLVER_UNWINNABLE || ios_ctx.state != STATE_WARNING)
        FAIL("only fully exhausted loss should open the warning state");
    if (rendered_solver_status != SOLVER_UNWINNABLE)
        FAIL("warning state did not publish the proven loss to the board renderer");
    PASS("app_stale_proof_and_proven_loss_warning");
}

static void test_menu_restore_returns_to_winnable_checkpoint(void) {
    Game solved = solved_position(DRAW_THREE);
    Game dead = stock_deadend(DRAW_THREE);
    dispose_context();
    attach_game(&solved);
    analyze_position(&ios_ctx, false);
    if (ios_ctx.proof_status != SOLVER_WINNABLE) FAIL("restore fixture has no winning proof");
    *ios_ctx.game = dead;
    analyze_position(&ios_ctx, false);
    if (finish_app_search(ios_ctx.solver) != SOLVER_UNWINNABLE)
        FAIL("restore fixture did not reach a proven dead end");
    ios_ctx.proof_status = solver_status(ios_ctx.solver);
    ios_ctx.state = STATE_WARNING;
    ios_ctx.selected = 1; // Resume, Restore, Check, New Game ...

    next_input.select_pressed = true;
    app_frame();
    if (ios_ctx.state != STATE_PLAYING || ios_ctx.proof_status != SOLVER_WINNABLE)
        FAIL("Restore menu action did not return to the saved winnable position");
    if (!ios_ctx.history.has_winnable || ios_ctx.history.count != 0)
        FAIL("Restore did not preserve its checkpoint and clear branch undo history");
    PASS("app_menu_restore_rechecks_winnable_checkpoint");
}

static void test_menu_undo_returns_to_winnable_snapshot(void) {
    Game solved = solved_position(DRAW_ONE);
    Game dead = stock_deadend(DRAW_ONE);
    dispose_context();
    attach_game(&dead);
    history_mark_winnable(&ios_ctx.history, &solved);
    history_push(&ios_ctx.history, &solved);
    ios_ctx.proof_status = SOLVER_UNWINNABLE;
    ios_ctx.state = STATE_WARNING;
    ios_ctx.selected = 1; // Undo is the second menu item.

    next_input.select_pressed = true;
    app_frame();
    if (ios_ctx.state != STATE_PLAYING || ios_ctx.proof_status != SOLVER_WINNABLE)
        FAIL("Undo menu action did not restore and recheck the prior winnable snapshot");
    if (ios_ctx.history.count != 0) FAIL("Undo left its restored entry available twice");
    PASS("app_menu_undo_rechecks_winnable_snapshot");
}

static void test_unknown_never_opens_loss_warning(void) {
    Game dead = stock_deadend(DRAW_ONE);
    dispose_context();
    attach_game(&dead);
    ios_ctx.solver = solver_create_limited(ios_ctx.game, 1);
    if (!ios_ctx.solver) FAIL("could not start bounded unknown fixture");
    ios_ctx.proof_status = SOLVER_CHECKING;
    app_frame();
    if (ios_ctx.proof_status != SOLVER_UNKNOWN)
        FAIL("state cap was not exposed as unknown to the app");
    if (ios_ctx.state != STATE_PLAYING)
        FAIL("unknown/budget exhaustion opened the unwinnable warning");
    PASS("app_unknown_does_not_warn");
}

static void test_scene_reconnect_preserves_live_game(void) {
    dispose_context();
    next_input.select_pressed = true;
    app_frame();
    Game* game = ios_ctx.game;
    Solver* solver = ios_ctx.solver;
    if (!game || !solver) FAIL("scene reconnect fixture has no game");
    Game snapshot = *game;
    app_init();
    if (ios_ctx.game != game || ios_ctx.solver != solver ||
        memcmp(ios_ctx.game, &snapshot, sizeof(snapshot)) != 0)
        FAIL("scene reconnect reset or abandoned the live game");
    PASS("app_scene_reconnect_preserves_live_game");
}

int main(void) {
    test_new_game_uses_certified_bank_for_both_modes();
    test_move_invalidates_stale_proof_and_proven_loss_warns();
    test_menu_restore_returns_to_winnable_checkpoint();
    test_menu_undo_returns_to_winnable_snapshot();
    test_unknown_never_opens_loss_warning();
    test_scene_reconnect_preserves_live_game();
    dispose_context();
    return 0;
}
