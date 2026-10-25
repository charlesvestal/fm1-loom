#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Audition the factory presets: each one plays a short phrase (three notes, then a chord held) on
# track 2 of a new project in the host simulator, through the default FX I delay and FX II reverb
# and the master, into build/presets/NN-engine-name.wav; then their levels (peak, RMS, the loudest
# 100 ms) into build/presets/levels.txt and a page to listen through, build/presets/index.html.
# Silent: nothing is played. Exits 1 when a render clips or is silent. With --roles each preset plays as its role
# is used (a bass a riff, a pad held chords, a pluck an arpeggio ...: tools/preset_scripts.c), into build/presets-roles:
# to listen to; the comparisons (tools/preset_distance.py) take the common phrase's renders.
#   tools/render_presets.sh [--roles]
set -e
cd "$(dirname "$0")/.."
CC="${CC:-cc}"
PY="${PYTHON:-python3}"
OUT=build/presets
ROLES=
if [ "${1:-}" = --roles ]; then OUT=build/presets-roles; ROLES=--roles; fi
PYTHON="$PY" sh build.sh --host >/dev/null
mkdir -p "$OUT"
rm -f "$OUT"/*.wav "$OUT"/*.xy "$OUT"/dry/*.wav
mkdir -p build/host
$CC -O2 -std=c99 -DPLAT_HOST -Iapp/dsp -Iapp -Ibase/include -Iapp/build/gen -o build/host/preset_scripts \
    tools/preset_scripts.c app/dsp/synth.c app/dsp/drumline.c app/dsp/digisophie.c app/dsp/sample.c -lm
names=$(build/host/preset_scripts "$OUT" $ROLES)
for n in $names; do
    app/build/host/loom_host "$OUT/$n.xy" "$OUT" >/dev/null || { echo "render failed: $n"; exit 1; }
done
"$PY" tools/preset_levels.py "$OUT" $names
echo "presets: $OUT/index.html, $OUT/levels.txt"
