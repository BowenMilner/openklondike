# Native iOS 1.1.0 QA

Verified on 2026-10-08. The app shares OpenKlondike's C rules, certified deals,
exact solver and history. A new UIKit presentation replaces the Metal renderer
in the iOS target. The legacy desktop, web and Android code remains available.

## Automated checks

- `make test`: 63 checks passed across game, layout, legacy renderer, gestures,
  solver, history, app-loop and eight native-session tests.
- Native-session coverage includes certified openings in both modes, atomic
  snapshot validation, exact legal hints, draw/auto-move and undo, checkpoint
  restore, proved dead ends in both modes, and bounded elapsed-time catch-up.
- An additional temporary AddressSanitizer/UndefinedBehaviorSanitizer harness
  replayed complete legal hint paths from certified choice zero to PHASE_WON:
  173 actions in Draw 1 and 60 in Draw 3, without sanitizer errors.
- Device build: Xcode 27.0, iPhoneOS SDK 27.0, arm64, minimum iOS 15;
  version 1.1.0/build 3. Final IPA integrity/platform/scene/class validation
  passed. All 52 distinct deck PNGs passed dimension and aspect-ratio checks.

## Running app acceptance

Used the existing iPhone 17 Pro Max Simulator with iOS 26.5. No additional
Simulator runtime was downloaded. Both orientations were visually inspected.

- Tapped an exposed Ace to its foundation, then Undo returned it visibly.
- Executed eight verified hint actions in Draw 1, including foundations, stock
  draws and tableau builds. The position remained proved winnable.
- Draw 3 displays a real three-card waste fan. Drew through the 24-card stock,
  recycled it, then Undo restored the last three visible waste cards.
- Normal-mode stock movement survived stopping and relaunching the app, with
  the same visible waste card restored. Capture mode skips save/restore.
- Portrait and landscape controls, card proportions, card artwork and text
  remained visible. No white or missing cards were observed in these scenarios.

QA repairs included reattaching reused card views after undo/recycling,
clearing old hints on a new deal, highlighting the exact legal hint destination,
using suit-neutral empty foundations, keeping disabled Undo readable, and
breaking the display-link retain cycle with a weak proxy.

## Captures

`--capture-deal` and `--capture-draw-three` select real certified choice-zero
openings. They do not script gameplay or fake status. The video records actual
UI taps in Draw 1; its deliverable is 50 seconds at 1080×2346/30 fps, without
speed changes or audio. Raw app PNGs are 1320×2868 portrait and 2868×1320
landscape. `scripts/native_promo.py` places unmodified app PNGs into three
1320×2868 promotional layouts, exported through a browser.

The CC0 English-pattern deck is pinned and attributed in `ios/Cards/LICENSE`.
Card images are 500×700, with an original felt texture and red geometric back.

## Limits

These checks are not physical iPhone or LiveContainer acceptance. The scene
lifecycle from the prior crash fix is preserved, but 1.1.0 must still be imported
and launched in LiveContainer 3.8.10 on the user's iOS 27 phone.

The opening bank is finite. A solver budget limit reports Not determined,
never Unwinnable; only exhaustive search proves loss. Full hidden-card state
is private to the engine/solver and is not exposed in card labels or hints.
Current board state persists; undo history and the rescue checkpoint are scoped
to the live session and rebuilt when a saved board is loaded.

## Landscape follow-up — 1.1.1/build 4

The user confirmed the 1.1.0 UI worked on the phone and reported cramped
landscape face-up stacks. The landscape top row now uses 72% size cards with
unchanged proportions; tighter header/status spacing moves it upward. The
layout reserves at least a quarter-card strip per exposed card, compressing
backs first, and sizes very deep runs to fit. It budgets each actual column
independently. Portrait geometry is unchanged.

Four new geometry checks cover the reported visible stack lengths without
card identities, a full 13-card run above six hidden cards, independent fan
budgets, and unchanged portrait composition: 67 total checks pass.

A separate review found that UNKNOWN is an expected bounded-search result:
100,000 states for an ordinary check, with allocation/resource uncertainty also
remaining unknown. The screenshot cannot identify its exact cause. The label
now reads Not determined. Settings offers the existing user-triggered
500,000-state Check position retry once ordinary analysis is undetermined;
it still uses frame slices and does not imply a guaranteed result. Repeated
retries use the same cap. Exact-loss semantics are unchanged.

The new UIKit build was launched on the existing iOS 26.5 Simulator. Actual
tap moves created two face-up tableau builds, and landscape inspection showed
readable exposed ranks. Final device IPA validation passed. Physical-device
acceptance of 1.1.1 remains separate; prior phone acceptance applies to 1.1.0.

The final landscape footer keeps its original 44-point button height and uses
smaller vertical content insets so the full icons and titles fit. Real Undo
and replay passed in landscape. Repeated iOS builds now clear the staged deck
before copying it, preventing duplicate nested Cards resources; the IPA checker
rejects this packaging regression and Makefile edits invalidate iOS targets.
