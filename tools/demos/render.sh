#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# LOOM-8's demo songs, rendered in the host simulator: each tools/demos/<song>.xy played from its song page, as a
# player would make it on the FM-1 (its own sample bank, its tracks, scenes, song, locks and throws), to a WAV, then
# AAC (.m4a). For private listening: the banks hold classic breaks and must never be committed, published or put on
# a site (they are made under build/demos/, git-ignored; the m4a files go to OUT). The mixdown is the simulator's
# output + 6 dB: the host writes what the FM-1's codec gets, 6 dB under its full scale (analyze.py --master).
#
#   tools/demos/render.sh [OUT] [SONG ...]     (OUT default build/demos/out; songs default all six)
#
# STEMS=1: also, per song, into build/demos/run_<song>/stems/: each track alone with its FX returns (T1.wav ..), the
# tracks without the FX (dry.wav), and the mix taken before the master's stages (raw: the groups summed; eq;
# comp: no limiter) (app/host.c LOOM_SOLO, LOOM_MST), as the host writes them: 6 dB under the codec's full scale and
# unlimited, so they add up to raw.wav.
# BREAKS: the breakbeat samples' folder (default ../../schwung-breakbeat/samples from fm1-xy, as fm1-parent has
# it). PYTHON: a Python with numpy and scipy (default /opt/homebrew/bin/python3.14, else python3). Builds the host
# simulator first (./build.sh --host). Prints each song's stats (voices, the modelled load) and the levels check.
set -eu
cd "$(dirname "$0")/../.."
ROOT=$(pwd)
OUT="${1:-build/demos/out}"
[ $# -gt 0 ] && shift
SONGS="${*:-jungle garage hyperpop house pop hiphop}"
if [ -z "${PYTHON:-}" ]; then
    if [ -x /opt/homebrew/bin/python3.14 ]; then PYTHON=/opt/homebrew/bin/python3.14; else PYTHON=python3; fi
fi
export PYTHON
export LOOM_LOAD="${LOOM_LOAD:-0}"   # the overload guard fed 0 %, not this computer's timing: the same render every time
BREAKS="${BREAKS:-}"
if [ -z "$BREAKS" ]; then
    for d in ../../schwung-breakbeat/samples ../schwung-breakbeat/samples "$HOME/github/fm1-parent/schwung-breakbeat/samples"; do
        if [ -f "$d/amen01.wav" ]; then BREAKS="$d"; break; fi
    done
fi
if [ -z "$BREAKS" ] || [ ! -f "$BREAKS/amen01.wav" ]; then
    echo "render: no breakbeat samples found (set BREAKS to schwung-breakbeat's samples/ folder: amen01.wav, think.wav,"
    echo "        sesame.wav, funkydrummer.wav). They are not part of this repository and never will be."
    exit 1
fi
if ! "$PYTHON" -c "import numpy, scipy" 2>/dev/null; then
    echo "render: $PYTHON lacks numpy or scipy (set PYTHON to one that has them)"
    exit 1
fi
mkdir -p build/demos "$OUT"
sh build.sh --host >/dev/null || { echo "render: the host simulator did not build"; exit 1; }
"$PYTHON" tools/demos/make_banks.py "$BREAKS" build/demos $SONGS
for s in $SONGS; do
    d=build/demos/run_$s
    rm -rf "$d" && mkdir -p "$d"
    echo "== $s"
    rc=0
    app/build/host/loom_host "tools/demos/$s.xy" "$d" > "$d/log.txt" 2>&1 || rc=$?
    # the simulator's layout audit (gfx_loom.c): none of the songs' screens is flagged; a LAYOUT line is printed
    # below as a note, not a failure (the audio is still good)
    if [ $rc -ne 0 ] && { grep -qE '^(FAIL|demo: |line [0-9]+: unknown)' "$d/log.txt" || [ ! -f "$d/loom_demo_$s.wav" ]; }; then
        tail -20 "$d/log.txt"
        exit 1
    fi
    grep -E '^(stats|demo|LAYOUT)' "$d/log.txt" || true
    "$PYTHON" tools/demos/analyze.py --master "$d/loom_demo_$s.wav" "$d/mix_$s.wav"
    if [ "${STEMS:-0}" = 1 ]; then
        mkdir -p "$d/stems"
        stem() {   # NAME VAR=VALUE ...
            n=$1; shift
            mkdir -p "$d/stems/.$n"
            env "$@" app/build/host/loom_host "tools/demos/$s.xy" "$d/stems/.$n" > /dev/null 2>&1
            mv "$d/stems/.$n/loom_demo_$s.wav" "$d/stems/$n.wav" && rm -rf "$d/stems/.$n"
        }
        for t in 1 2 3 4 5 6 7 8; do stem "T$t" LOOM_MST=raw LOOM_SOLO="$t,fx1,fx2" & done
        stem dry LOOM_MST=raw LOOM_SOLO=1,2,3,4,5,6,7,8 &
        for m in raw eq comp; do stem "$m" LOOM_MST=$m & done
        wait
        echo "   stems: $d/stems/"
    fi
    "$PYTHON" tools/demos/analyze.py "$d/mix_$s.wav"
    if command -v afconvert >/dev/null 2>&1; then
        afconvert -f m4af -d aac -b 128000 "$d/mix_$s.wav" "$OUT/loom_demo_$s.m4a"
    else
        ffmpeg -loglevel error -y -i "$d/mix_$s.wav" -c:a aac -b:a 128k "$OUT/loom_demo_$s.m4a"
    fi
    ls -l "$OUT/loom_demo_$s.m4a" | awk '{printf "   %s: %.2f MB\n", $NF, $5 / 1048576}'
done
