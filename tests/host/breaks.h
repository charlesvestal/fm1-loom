/* SPDX-License-Identifier: GPL-3.0-only */
/* The break pack as the host tests' user bank: build/gen/breaks.bin (tools/loom_samples.py build ... breaks, made by
 * the tests' scripts), its windows apart as on the FM-1. Since 0.7 the four breaks are not in the image: the factory
 * rows SMP_BREAK.. are found in the user bank by name (dsp/sample.c), so a test that plays them attaches the pack,
 * as a player who installed it has it. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sample.h"

static void breaks_attach(void)
{
    static uint8_t w0[SMP_REG0_SIZE], w1[SMP_REG1_SIZE], w2[SMP_REG2_SIZE];
    static const uint8_t *w[SMP_NREG] = {w0, w1, w2};
    uint8_t *const dst[SMP_NREG] = {w0, w1, w2};
    FILE *f = fopen("build/gen/breaks.bin", "rb");
    if (!f) {
        fprintf(stderr, "no build/gen/breaks.bin (tools/loom_samples.py build build/gen/breaks.bin breaks)\n");
        exit(1);
    }
    for (int r = 0; r < SMP_NREG; r++) {
        memset(dst[r], 0xFF, smp_reg_size(r));
        if (fread(dst[r], 1, smp_reg_size(r), f) < smp_reg_size(r))
            break;                                   /* (the image ends in this window) */
    }
    fclose(f);
    smp_user_attach(w);
    if (smp_user_scan() != 4) {
        fprintf(stderr, "build/gen/breaks.bin: not the four breaks\n");
        exit(1);
    }
}
