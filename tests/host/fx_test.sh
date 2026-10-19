#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Build and run the effects' host test (tests/host/fx_test.c): WAVs in build/host/fx_*.wav.
set -eu
cd "$(dirname "$0")/../.."
mkdir -p build/host
${CC:-cc} -O2 -ffp-contract=off -std=c99 -Wall -Wextra -Wdouble-promotion -Werror -DPLAT_HOST -DMST_HEADROOM=1.0f -Iapp/dsp -Ibase/include \
    -o build/host/fx_test tests/host/fx_test.c app/dsp/fx.c
build/host/fx_test
