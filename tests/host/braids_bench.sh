#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# The Braids engines' cost a sample and their sound against a saved reference (tests/host/braids_bench.c).
#   tests/host/braids_bench.sh save     before a change: build/host/braids_bench.ref
#   tests/host/braids_bench.sh          after it: the cost, and how far the sound moved
# SCALAR=1 builds without the host's vector units (nearer the FM-1's scalar core).
set -eu
cd "$(dirname "$0")/../.."
CC="${CC:-cc}"
PY="${PYTHON:-python3}"
mkdir -p build/gen build/host
[ -f build/gen/wavetables.h ] || "$PY" tools/gen_wavetables.py build/gen/wavetables.h
[ -f build/gen/samples.h ] || "$PY" tools/gen_samples.py build/gen/samples.h >/dev/null
"$PY" tools/gen_presets.py build/gen/presets.h
V=""
[ "${SCALAR:-0}" = 1 ] && V="-fno-vectorize -fno-slp-vectorize"
$CC -O2 $V -ffp-contract=off -std=c99 -Wall -Wextra -Wdouble-promotion -Wno-unused-function -DPLAT_HOST -Iapp/dsp -Ibase/include -Ibuild/gen \
    -o build/host/braids_bench tests/host/braids_bench.c app/dsp/drumline.c app/dsp/sample.c
build/host/braids_bench "$@"
