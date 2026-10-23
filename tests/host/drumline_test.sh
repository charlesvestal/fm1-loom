#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Build and run the drumline's host test (tests/host/drumline_test.c): WAVs in build/host/drum_*.wav
# (each key, and drum_beat.wav).
set -eu
cd "$(dirname "$0")/../.."
mkdir -p build/host build/gen
[ -f build/gen/samples.h ] || "${PYTHON:-python3}" tools/gen_samples.py build/gen/samples.h >/dev/null
[ -f build/gen/breaks.bin ] || "${PYTHON:-python3}" tools/loom_samples.py build build/gen/breaks.bin breaks >/dev/null
${CC:-cc} -O2 -ffp-contract=off -std=c99 -Wall -Wextra -Wdouble-promotion -Werror -DPLAT_HOST -Iapp/dsp -Ibase/include -Ibuild/gen \
    -o build/host/drumline_test tests/host/drumline_test.c app/dsp/drumline.c app/dsp/digisophie.c app/dsp/sample.c -lm
build/host/drumline_test
