#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# The effects' cost on the host, a slot at a time (tests/host/efx_bench.c): host instructions a sample, full mix and 90.
#   tests/host/efx_bench.sh
set -eu
cd "$(dirname "$0")/../.."
mkdir -p build/host
${CC:-cc} -O2 -ffp-contract=off -std=gnu99 -Wall -Wextra -Wdouble-promotion -Werror -DPLAT_HOST -DMST_HEADROOM=1.0f \
    -Iapp/dsp -Ibase/include -Itests/host -o build/host/efx_bench tests/host/efx_bench.c app/dsp/fx.c app/dsp/efx.c
build/host/efx_bench
