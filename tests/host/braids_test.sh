#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# The Braids engines' host test (tests/host/braids_test.c): cane, voile and gingham bounded, in tune, at their
# levels, cane's cap, the arp; their cost a voice by model. Built with the device's rules (C99, float only in the
# DSP, no fused multiply-add).
#   tests/host/braids_test.sh
set -e
cd "$(dirname "$0")/../.."
CC="${CC:-cc}"
PY="${PYTHON:-python3}"
mkdir -p build/gen build/host
[ -f build/gen/wavetables.h ] || "$PY" tools/gen_wavetables.py build/gen/wavetables.h
[ -f build/gen/samples.h ] || "$PY" tools/gen_samples.py build/gen/samples.h >/dev/null
"$PY" tools/gen_presets.py build/gen/presets.h
$CC -O2 -ffp-contract=off -std=c99 -Wall -Wextra -Wdouble-promotion -Werror -DPLAT_HOST -Iapp/dsp -Ibase/include -Ibuild/gen \
    -o build/host/braids_test tests/host/braids_test.c app/dsp/synth.c app/dsp/drumline.c app/dsp/sample.c
build/host/braids_test
