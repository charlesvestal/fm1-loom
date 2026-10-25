/* SPDX-License-Identifier: MPL-2.0 */
/* The host tests' and benchmarks' two measures of work: the instructions this process has executed (macOS keeps a
 * count per process, read through libproc's resource usage record: instr_now; elsewhere the CPU clock instead), and a
 * monotonic clock in nanoseconds (ns_now). */
#pragma once
#include <stdint.h>
#include <time.h>
#ifdef __APPLE__
#include <libproc.h>
#include <unistd.h>
#endif

static inline uint64_t instr_now(void)
{
#ifdef __APPLE__
    rusage_info_current use;
    return proc_pid_rusage(getpid(), RUSAGE_INFO_CURRENT, (rusage_info_t *)&use) == 0 ? use.ri_instructions : 0u;
#else
    return (uint64_t)clock();
#endif
}

static inline uint64_t ns_now(void)
{
#ifdef __APPLE__
    return clock_gettime_nsec_np(CLOCK_UPTIME_RAW);   /* (monotonic: the benchmarks take differences) */
#else
    return (uint64_t)clock() * (1000000000u / CLOCKS_PER_SEC);
#endif
}
