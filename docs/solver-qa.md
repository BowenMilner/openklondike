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
It also checks that reconnecting an iOS scene preserves the live game and solver.

The stale-proof warning fixture injects a valid dead-end board before a legal
stock tap. It tests analysis replacement and warning wiring; it does not
demonstrate a naturally occurring fatal move from the same winnable opening.

The native solver test executable completed in 0.47 seconds in the measured
run on the development Mac; this is the test suite runtime, not a cold-search
latency guarantee for arbitrary deals. These checks do not render through the
GPU and do not replace Simulator or physical-device acceptance. The separate
layout renderer test validates emitted CPU-side card art and geometry only.

## iOS 27 launch regression (1.0.1)

A physical-device LiveContainer 3.8.10 crash report identified a UIKit trap in
`___UIApplicationEvaluateRuntimeIssueForNoSceneLifecycleAdoption_block_invoke`.
Version 1.0.0 used an AppDelegate-owned window with no scene manifest. Version
1.0.1 adopts `UIWindowScene`, creates its window in `OKSceneDelegate`, and handles
focus and renderer cleanup through scene callbacks. This follows Apple's
[scene lifecycle requirement](https://developer.apple.com/documentation/uikit/transitioning-to-the-uikit-scene-based-life-cycle)
for apps linked against the iOS 27 SDK.

The device IPA build now runs `scripts/check_ios_package.py` against the final
ZIP. It checks archive integrity, device platform, a single-scene manifest, and
that the named scene delegate and window connection callback are compiled into
the packaged executable. The old 1.0.0 IPA fails this check. These package checks
and a Simulator build do not confirm a corrected launch inside LiveContainer;
that requires importing 1.0.1 and retesting on the phone.

## Native iOS 1.1.0

The UIKit rebuild has separate [native iOS QA](native-ios-qa.md), including
real Simulator gameplay and media provenance. The engine and proof semantics
above remain shared. Legacy rendering tests do not validate the UIKit board.

## Search ordering and exact-position export — 1.2.0

The old depth-first frontier could take long reversible detours. Eighteen sampled
known-winnable positions returned UNKNOWN when their first discovered winning
path exceeded the 1,024-action synchronous proof bound, even below the state cap.
The frontier now uses a heap ordered by foundation progress, face-up tableau
cards and stock progress, preferring shallower paths at equal priority. This
changes search order only: every legal successor remains eligible, identities
are exact, foundation take-backs and unlimited recycling remain legal, and only
complete graph exhaustion proves loss.

Across 168 deterministic, uncached midgame snapshots with independently known
winning certificate suffixes, the previous search resolved 150 and the new
search resolved 167 at 100,000 states. Every new proof replayed legally to a win;
the longest was 114 actions. Total expanded states fell from 590,137 to 473,432.
One draw-one sample remained UNKNOWN at both 100,000 and 500,000 states. These
are headless Mac measurements on certificate-derived positions, not a universal
success rate or an iPhone latency guarantee.

To reproduce the corpus, select DRAW_ONE choices 0–31 and DRAW_THREE choices
0–23 from `certified_deal`, get each verified certificate of length n, and replay
to floor(n/4), floor(n/2) and floor(3n/4). Run each of these 168 snapshots in a
fresh process (empty proof cache), cap 100,000, with 1,024 expansions per call.
Compare `ec8ab50:src/solver.c` with this version, replaying every winning result.
The remaining capped sample is DRAW_ONE choice 17 at certificate prefix 147
(three quarters of its 196 actions). All fixtures use bundled deals; none is
the user's private saved board.

The 1,024-action synchronous proof bound remains. Longer first winning paths
still report UNKNOWN; cached joins above the bound are skipped. The change does
not guarantee a decision for every position. This preserves bounded proof work
on the UI thread. Search still runs in 32-expansion native frame slices, with a
100,000-state normal limit and 500,000-state deeper limit. The heap adds eight
bytes per search node (about 4 MB at the deeper limit).

New regressions clear the private proof cache before checking independent
midgame positions in both modes. Small caps must remain UNKNOWN; full searches
must return a legal winning proof. Existing exact dead-end counts remain two
states for the reversible cycle, 25 for draw-one stock recycling and nine for
draw-three. Session tests cover state progress and preventing an identical
second deeper restart. Four position-file suites cover full-deck roundtrips in
both modes, corruption/version/size rejection, malformed checksummed payloads,
atomic rejection and invalid deck rejection.

The reported 17-move phone position has not been reproduced: the screenshot does
not identify the face-down deck. The new Export game route is needed to validate
that exact case rather than assume it matches a test fixture.
