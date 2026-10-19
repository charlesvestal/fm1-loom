#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# An engine's cost on the host, a preset at a time (tests/host/eng_bench.c): the full voice path and the engine alone.
#   tests/host/eng_bench.sh [engine ...]      (default: knit rayon jacquard)
set -eu
cd "$(dirname "$0")/../.."
PY="${PYTHON:-python3}"
mkdir -p build/gen build/host
[ -f build/gen/wavetables.h ] || "$PY" tools/gen_wavetables.py build/gen/wavetables.h
[ -f build/gen/samples.h ] || "$PY" tools/gen_samples.py build/gen/samples.h >/dev/null
"$PY" tools/gen_presets.py build/gen/presets.h
${CC:-cc} -O2 -ffp-contract=off -std=gnu99 -Wall -Wextra -Wdouble-promotion -Werror -Wno-unused-function \
    -DPLAT_HOST -Iapp/dsp -Ibase/include -Ibuild/gen \
    ${BENCH_FLAGS:-} -o build/host/eng_bench tests/host/eng_bench.c app/dsp/drumline.c app/dsp/sample.c
build/host/eng_bench "$@"
