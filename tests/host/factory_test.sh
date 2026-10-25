#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# The factory set in the user bank, as on the FM-1 (tests/host/factory_test.c): sample.c without the factory samples
# built in (SMP_FACT_MOVED), against a bank with them and one without.
set -eu
cd "$(dirname "$0")/../.."
mkdir -p build/host build/gen
PY="${PYTHON:-python3}"
"$PY" tools/gen_samples.py build/gen/samples.h build/gen/wav >/dev/null
"$PY" tools/loom_samples.py build build/gen/factory_bank.bin 'piano two=build/gen/wav/piano.wav:60%16k' \
    'sweep two=build/gen/wav/sweep.wav:57' >/dev/null
"$PY" tools/loom_samples.py build --no-factory build/gen/user_bank_nofactory.bin 'piano two=build/gen/wav/piano.wav:60%16k' \
    'sweep two=build/gen/wav/sweep.wav:57' >/dev/null
${CC:-cc} -O2 -std=c99 -Wall -Wextra -Werror -DPLAT_HOST -DSMP_FACT_MOVED -Iapp/dsp -Ibase/include -Ibuild/gen \
    -o build/host/factory_test tests/host/factory_test.c app/dsp/sample.c -lm
build/host/factory_test
