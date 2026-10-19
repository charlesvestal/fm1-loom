/* SPDX-License-Identifier: GPL-3.0-only */
/* The hardware profiler's kernels (app/dsp/bench.c) on the host: each kernel's fastest batch of 64 runs of BENCH_N
 * samples (of 30; the host's clock is too coarse for one run) in nanoseconds and instructions a sample, a line each (name, ns, instructions; tab-separated), for
 * tools/hw_bench.py to set beside the FM-1's. Instructions need macOS (proc_pid_rusage); elsewhere 0.
 *   tests/host/hw_bench.sh */
#define XY_BENCH 1
#include "../../app/dsp/bench.c"
#include <stdio.h>
#include <time.h>
#ifdef __APPLE__
#include <unistd.h>
#include <libproc.h>
#include <sys/resource.h>
static uint64_t instr_now(void)
{
    struct rusage_info_v4 ri;
    if (!proc_pid_rusage(getpid(), RUSAGE_INFO_V4, (rusage_info_t *)&ri))
        return ri.ri_instructions;
    return 0;
}
#else
static uint64_t instr_now(void) { return 0; }
#endif
static uint64_t ns_now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000u + (uint64_t)t.tv_nsec;
}

int main(void)
{
    static float ram[BENCH_RAM_BYTES / sizeof(float)];
    volatile float sink = 0.0f;
    for (int k = 0; k < bench_count(); k++) {
        uint64_t best_ns = (uint64_t)-1, best_i = (uint64_t)-1;
        bench_prep(k, ram);
        sink = bench_run(k);
        for (int r = 0; r < 30; r++) {
            uint64_t i0 = instr_now(), n0 = ns_now(), dn, di;
            for (int b = 0; b < 64; b++)
                sink = bench_run(k);
            dn = ns_now() - n0;
            di = instr_now() - i0;
            if (dn < best_ns)
                best_ns = dn;
            if (di < best_i)
                best_i = di;
        }
        printf("%s\t%.3f\t%.1f\n", bench_name(k), (double)best_ns / (64.0 * BENCH_N),
               (double)best_i / (64.0 * BENCH_N));
    }
    (void)sink;
    return 0;
}
