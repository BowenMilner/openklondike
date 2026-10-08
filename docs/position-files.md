# Reproduce an unresolved game

On iPhone, open Settings (the three-dot button), choose **Export game**, then
save or share the `.solitaire` file. Updating the existing LiveContainer app
should retain its saved game; do not start a new deal before exporting the
position being investigated.

The export contains the full card position, including face-down cards and stock
order, plus the draw mode. It contains no personal data, device identifier,
account information, gameplay timer or move history. Hidden card identities
remain concealed in the app and diagnostic output. Sharing happens only through
the destination chosen in the system share sheet.

Build the offline diagnostic tool and run it against the exported file:

```sh
make solve-position
./build/solve_position /path/to/game.solitaire
./build/solve_position /path/to/game.solitaire 2000000
```

The default limit is 500,000 states; the optional limit must be between 1 and
2,000,000. This is an offline tool, not the app's frame-sliced UI search. Its
output contains only the result, state count, proof length and CPU time. Every
winning result is replayed through the game rules to require `PHASE_WON`.
Exit codes are 0 for a resolved result, 2 for an undetermined result and 3 for
invalid input or proof failure. There is no in-app import action yet.

## Version 1 format

The binary file is exactly 79 bytes. It has an eight-byte format/version header,
two draw settings, thirteen pile counts, fifty-two encoded cards and a four-byte
FNV-1a checksum. Piles are stock, waste, four foundations, then seven tableau
columns. Card order and face-up state are preserved. The reader rejects unknown
versions, corruption, wrong sizes, invalid settings, duplicate/missing cards and
invalid pile structure, and leaves the destination unchanged after rejection.
The checksum detects accidental corruption; it is not an authentication scheme.
