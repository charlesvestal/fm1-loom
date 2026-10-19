#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Fingerprints of the DSP's output (tests/host/dsp_hash.c), for changes that must not move a sample:
#   tests/host/dsp_hash.sh save     before the change: build/host/dsp_hash.ref
#   tests/host/dsp_hash.sh          after it: compare with the reference (exit 1 on a difference)
set -eu
cd "$(dirname "$0")/../.."
CC="${CC:-cc}"
PY="${PYTHON:-python3}"
mkdir -p build/gen build/host
[ -f build/gen/wavetables.h ] || "$PY" tools/gen_wavetables.py build/gen/wavetables.h
[ -f build/gen/samples.h ] || "$PY" tools/gen_samples.py build/gen/samples.h >/dev/null
"$PY" tools/gen_presets.py build/gen/presets.h
$CC -O2 -ffp-contract=off -std=c99 -Wall -Wextra -Wdouble-promotion -Werror -DPLAT_HOST -Iapp/dsp -Ibase/include -Ibuild/gen \
    -o build/host/dsp_hash tests/host/dsp_hash.c app/dsp/synth.c app/dsp/drumline.c \
    app/dsp/sample.c app/dsp/fx.c
build/host/dsp_hash > build/host/dsp_hash.txt
if [ "${1:-}" = save ]; then
    cp build/host/dsp_hash.txt build/host/dsp_hash.ref
    echo "dsp_hash: $(wc -l < build/host/dsp_hash.ref) cases saved to build/host/dsp_hash.ref"
elif diff build/host/dsp_hash.ref build/host/dsp_hash.txt > build/host/dsp_hash.diff; then
    echo "dsp_hash: $(wc -l < build/host/dsp_hash.txt) cases, all the same"
else
    echo "dsp_hash: $(grep -c '^>' build/host/dsp_hash.diff) of $(wc -l < build/host/dsp_hash.txt) cases differ:"
    grep '^>' build/host/dsp_hash.diff | head -20
    exit 1
fi
