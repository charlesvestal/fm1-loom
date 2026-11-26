#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# The Mix page's audio mutes in the DSP (tests/host/mixmute_test.c): muted, wet only, a kit's key; no clicks.
#   tests/host/mixmute_test.sh [-v]
set -e
cd "$(dirname "$0")/../.."
CC="${CC:-cc}"
PY="${PYTHON:-python3}"
mkdir -p build/gen build/host
[ -f build/gen/wavetables.h ] || "$PY" tools/gen_wavetables.py build/gen/wavetables.h
[ -f build/gen/samples.h ] || "$PY" tools/gen_samples.py build/gen/samples.h >/dev/null
"$PY" tools/gen_presets.py build/gen/presets.h
[ -f build/gen/breaks.bin ] || "$PY" tools/loom_samples.py build --no-factory build/gen/breaks.bin breaks >/dev/null
$CC -O2 -ffp-contract=off -std=c99 -Wall -Wextra -Wdouble-promotion -Werror -DPLAT_HOST -Iapp/dsp -Ibase/include \
    -Ibuild/gen -o build/host/mixmute_test tests/host/mixmute_test.c app/dsp/synth.c app/dsp/efx.c app/dsp/drumline.c app/dsp/d909.c \
    app/dsp/digisophie.c app/dsp/sample.c app/dsp/bass303.c -lm
build/host/mixmute_test "$@"
