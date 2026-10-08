# Solver and recovery QA

Run the native checks with `make test`. The solver, history, and app-loop checks
are headless and use the actual game rules; the app-loop check stubs platform
rendering, sound, input, and window services.

The solver suite replays all 448 bundled certificates (256 draw-one and 192
draw-three) through the game engine, checks their standard 1-to-7 tableau
layout and complete 52-card deck, then independently reapplies every certified
move and requires the game to reach `PHASE_WON`. It also checks that position
identity includes pile boundaries, card order, face-up state, and draw mode,
while ignoring score, timer, move count, and frame-event bookkeeping.

Loss and uncertainty tests include a valid two-position reversible tableau
cycle and standard-layout stock-only dead ends with 24 stock cards. The stock
tests exercise the selected draw count, full stock recycle order, and exact
graph exhaustion (25 states for draw-one and 9 for draw-three). A tiny state
cap and cancellation must report `UNKNOWN`; neither may report a loss. The
suite also checks a legal foundation take-back through the game engine,
including an Ace of hearts in the first empty foundation slot, which need not
match its suit index.

The cache-pressure test verifies the full certificate corpus and revisits it
in both orders after the proof cache has exceeded its 8,192-slot index and
4,096-entry FIFO capacity. It checks that each newly verified opening remains
discoverable as winnable through the exact-position cache after eviction and
index rebuild churn.

History tests cover empty operations, full snapshot restoration, ring wrap at
capacity, and a separately retained winnable checkpoint surviving branch
history overwrites and undo operations. The app-loop harness checks certified
new deals in both draw modes, invalidation of stale analysis after a move,
warning only after a proven loss, menu Undo and Restore, and that `UNKNOWN`
never opens the loss warning.

The stale-proof warning fixture injects a valid dead-end board before a legal
stock tap. It tests analysis replacement and warning wiring; it does not
demonstrate a naturally occurring fatal move from the same winnable opening.

The native solver test executable completed in 0.47 seconds in the measured
run on the development Mac; this is the test suite runtime, not a cold-search
latency guarantee for arbitrary deals. These checks do not render through the
GPU and do not replace Simulator or physical-device acceptance. The separate
layout renderer test validates emitted CPU-side card art and geometry only.
