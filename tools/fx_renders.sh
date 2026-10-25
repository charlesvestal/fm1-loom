#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Renders of the reverb's modes, for listening: build/fx_renders/*.wav and index.txt
# (tests/host/fx_render.c; nothing is played).
#   tools/fx_renders.sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p build/host build/fx_renders
${CC:-cc} -O2 -ffp-contract=off -std=c99 -Wall -Wextra -Wdouble-promotion -Werror -DPLAT_HOST -Iapp/dsp -Ibase/include \
    -o build/host/fx_render tests/host/fx_render.c app/dsp/fx.c
build/host/fx_render
