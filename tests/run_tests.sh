#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# LOOM-8's tests: the units' own (the synth, the Braids engines, the drumline, the FX, the sequencer: tests/host/*_test.sh; the DSP's
# fingerprints when a reference was saved: tests/host/dsp_hash.sh save), the user sample bank's tool, the update path
# against LOOM-8's package (./build.sh first), the whole app in fm1-base's simulator (tests/scenarios/*.xy: UI,
# patterns, persistence, backup and restore; screenshots and audio in app/build/scenarios/, the layout audit), and
# the browser build. Silent: the audio is written to WAV files, never played. fm1-base's own tests (the platform,
# storage, backup and restore, the installer): base/tests/run_tests.sh.
#   tests/run_tests.sh
set -u
cd "$(dirname "$0")/.."
CC="${CC:-cc}"
PY="${PYTHON:-python3}"
export AC79_SDK="${AC79_SDK:-$HOME/fw-AC79_AIoT_SDK}"   # the package's SDK files, as ./build.sh
B=app/build
OUT=$B/tests
FAIL=0
mkdir -p "$OUT" $B/scenarios build/gen
run() {
    name="$1"; shift
    if "$@" > "$OUT/$name.log" 2>&1; then
        echo "  ok   $name"
    else
        echo "  FAIL $name (see $OUT/$name.log)"
        tail -15 "$OUT/$name.log" | sed 's/^/       /'
        FAIL=1
    fi
}
# the factory samples as WAVs, and a user bank from two of them (one at 16 kHz), as tools/loom_samples.py would
# upload it: its image (tests/scenarios/samples.xy and slice.xy load it) and its SysEx frames (upload.xy plays them
# through samples.c); the bank tool's own tests (layout over the windows, rates, bounds)
"$PY" tools/gen_samples.py build/gen/samples.h build/gen/wav >/dev/null
run sample-bank sh -c "'$PY' tools/loom_samples.py build build/gen/user_bank.bin 'piano two=build/gen/wav/piano.wav:60%16k' \
    'sweep two=build/gen/wav/sweep.wav:57' && '$PY' tools/loom_samples.py syx build/gen/user_bank.syx \
    'piano two=build/gen/wav/piano.wav:60%16k' 'sweep two=build/gen/wav/sweep.wav:57'"
run bank-tool "$PY" tests/host/bank_test.py
# the units' own tests, written beside them (their WAVs in build/host/)
for t in synth braids drumline fx seq; do
    run "$t" sh "tests/host/${t}_test.sh"
done
if command -v node >/dev/null 2>&1; then           # the samples page's bank against loom_samples.py's
    run samples-web node tests/host/samples_web.mjs "$PY"
else
    echo "  skip samples-web (no node)"
fi
if [ -f build/host/dsp_hash.ref ]; then
    run dsp-hash sh tests/host/dsp_hash.sh
else
    echo "  skip dsp-hash (no reference: tests/host/dsp_hash.sh save, before a change)"
fi
# the update path, against LOOM-8's package (fm1-base's tests; ./build.sh first)
if [ -f $B/loom.fwsc ]; then
    run ota-entry sh -c "$CC -w -o $OUT/ota_test base/tests/ota_test.c && $OUT/ota_test $B/loom.fwsc"
    run update-loader sh -c "head -c 200000 $B/loom.bin > $OUT/old_app.bin && \
        $PY base/tools/fm1pkg_make.py $OUT/old_app.bin $B/loader/ota.bin $OUT/old.fwsc >/dev/null && \
        $CC -w -o $OUT/ldr_test base/tests/ldr_test.c && $OUT/ldr_test $OUT/old.fwsc $B/loom.fwsc"
else
    echo "  skip update path (no $B/loom.fwsc: ./build.sh)"
fi
run host-build sh build.sh --host
# a bank image larger than the bank's windows is refused by the simulator's loader (as the tool and the FM-1 refuse it)
run bank-too-big sh -c "mkdir -p $OUT/big && head -c 221185 /dev/zero > $OUT/big.bin && \
    printf 'samples $OUT/big.bin\n' > $OUT/big.xy && ! $B/host/loom_host $OUT/big.xy $OUT/big > $OUT/big.out 2>&1 && \
    grep -q 'larger than' $OUT/big.out"
if command -v emcc >/dev/null 2>&1 && command -v node >/dev/null 2>&1; then   # the browser build, when it can be built
    run emu sh -c "sh build.sh --web >/dev/null && node tests/host/emu_test.mjs $B/emu/app.wasm"
else
    echo "  skip emu (no emcc or node)"
fi
for s in tests/scenarios/*.xy; do
    n=$(basename "$s" .xy)
    rm -rf "$B/scenarios/$n" && mkdir -p "$B/scenarios/$n"
    run "scenario-$n" $B/host/loom_host "$s" "$B/scenarios/$n"
done
# every page, engine and type, through the layout audit (tools/layout_sweep.py)
"$PY" tools/layout_sweep.py build/gen/layout_sweep.xy
rm -rf "$B/scenarios/layout_sweep" && mkdir -p "$B/scenarios/layout_sweep"
run layout-sweep $B/host/loom_host build/gen/layout_sweep.xy "$B/scenarios/layout_sweep"
[ $FAIL -eq 0 ] && echo "all tests passed" || echo "TESTS FAILED"
exit $FAIL
