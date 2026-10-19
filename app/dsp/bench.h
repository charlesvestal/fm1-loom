/* SPDX-License-Identifier: GPL-3.0-only */
/* LOOM-8's hardware profiler (a dev build only: XY_BENCH=1 ./build.sh; tools/hw_bench.py): small kernels, the
 * building blocks of a voice or an effect (oscillators, filters, curves, delay lines, a reverb), each run over a
 * block of samples on the FM-1 and on the host, so a new engine or effect can be costed from its parts before it
 * is written. The code-size kernels ("code N") spread the same work over N distinct copies of a function, to see
 * what the XIP (flash) cache costs as code grows; "code ram" runs one copy from RAM.
 *
 * The caller times bench_run (app/samples.c SB_BENCH on the FM-1, tests/host/hw_bench.c on the host); a kernel
 * keeps no state between bench_prep and the next prep but its own. Nothing here runs in a normal build. */
#pragma once
#include <stdint.h>
#ifndef XY_BENCH
#include "loom_build.h"                 /* XY_BENCH (tools/gen_build.py; the host's bench defines it itself) */
#endif

#define BENCH_N 256                     /* samples a run (a block) */
#define BENCH_RAM_BYTES 2048u           /* the float buffers the caller lends (in, out: BENCH_N each) */

int bench_count(void);
const char *bench_name(int k);
/* ram: BENCH_RAM_BYTES of RAM, 4-aligned (the FM-1: project.c's io_buf, free between frames) */
void bench_prep(int k, void *ram);
float bench_run(int k);                 /* one run of BENCH_N samples; the result only keeps the work alive */
