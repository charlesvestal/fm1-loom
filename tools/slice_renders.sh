#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Listen to the slice engine: the factory break as made (tools/gen_samples.py), then tests/scenarios/slice.xy
# in the host simulator (the break played straight from the steps, re-sequenced, the same at 90 BPM, "chop"
# on sixteenths, the keys by hand) into build/slice_renders/, with its screens and a page to listen through,
# build/slice_renders/index.html. Silent: nothing is played.
#   tools/slice_renders.sh
set -e
cd "$(dirname "$0")/.."
PY="${PYTHON:-python3}"
OUT=build/slice_renders
PYTHON="$PY" sh build.sh --host >/dev/null
"$PY" tools/gen_samples.py build/gen/samples.h build/gen/wav >/dev/null
mkdir -p "$OUT"
rm -f "$OUT"/*.wav "$OUT"/*.png
cp build/gen/wav/break.wav "$OUT/break_22k.wav"
app/build/host/loom_host tests/scenarios/slice.xy "$OUT" >/dev/null || { echo "slice.xy failed"; exit 1; }
{
    echo '<!doctype html><meta charset="utf-8"><title>LOOM-8 slice renders</title>'
    echo '<style>body{font:15px system-ui;margin:24px;max-width:760px}li{margin:10px 0}audio{display:block;width:100%}</style>'
    echo '<h1>The slice engine</h1><ol>'
    while read -r f what; do
        echo "<li><b>$f</b>: $what<audio controls src=\"$f\"></audio></li>"
    done <<'EOF'
break_22k.wav the placeholder break as stored: one bar at 120 BPM, 22.05 kHz (tools/gen_samples.py)
slice_straight.wav loom break: eight slices, F3 .. C4 on every other step: the break played straight, twice
slice_resequenced.wav the same slices re-sequenced (kick, kick, snare, ghost, the double kick, snare ...)
slice_resequenced_90.wav the same pattern at 90 BPM: the slices follow the tempo
slice_chop.wav chop: sixteen slices, mono, a slice on every sixteenth
slice_keys.wav the keys by hand: F3, G3, A3 (held), B3, F#3, G#3
EOF
    echo '</ol><p>'
    for p in slice_engine slice_playing slice_chop slice_knobs; do
        echo "<img src=\"$p.png\" width=\"240\" alt=\"$p\">"
    done
    echo '</p>'
} > "$OUT/index.html"
echo "slice renders: $OUT/index.html"
