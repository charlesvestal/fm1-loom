#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# The hardware profiler's kernels on the host (tests/host/hw_bench.c): a line each, name, ns and instructions a sample.
#   tests/host/hw_bench.sh
set -eu
cd "$(dirname "$0")/../.."
mkdir -p build/host
${CC:-cc} -O2 -ffp-contract=off -std=gnu99 -Wall -Wextra -Wdouble-promotion -Werror -Wno-unused-function \
    -DPLAT_HOST -Iapp/dsp -Ibase/include -o build/host/hw_bench tests/host/hw_bench.c
build/host/hw_bench
