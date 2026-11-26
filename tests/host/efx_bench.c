/* SPDX-License-Identifier: MPL-2.0 */
/* Each catalog effect in a track's slot (efx_slot_run, as on its bus): host instructions a sample, steady (after 100
 * blocks), at its defaults, on stereo noise, at full mix and at mix 90. On the FM-1 (2026-11-26, 396 MHz) a slot cost
 * about 4.4 of its cycles per host instruction: lofi 92 here, ~400 cycles there (tests/host/efx_bench.sh) */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "fx.h"
#include "fastmath.h"
#include "instr_count.h"
static uint32_t rng = 1;
static float noise(void) { rng = rng * 1664525u + 1013904223u; return (float)(int32_t)rng * (1.0f / 2147483648.0f); }
static efx_slot_t sl;
static double run(int ef, int mix)
{
    static float L[256], R[256];
    int16_t *mem;
    memset(&sl, 0, sizeof sl);
    memset(efx_pool, 0, sizeof efx_pool);
    sl.ef = (uint8_t)ef; sl.mix = (uint8_t)mix;
    for (int j = 0; j < 3; j++) sl.k[j] = (float)EFX[ef].def[j];
    for (int j = 0; j < 4; j++) sl.xt[j] = EFX[ef].xdef[j];
    mem = efx_need(ef, sl.xt[0], 0) ? efx_pool[0] : 0;
    uint64_t i0 = 0, tot = 0;
    for (int b = 0; b < 400; b++) {
        for (int i = 0; i < 256; i++) { L[i] = 0.1f * noise(); R[i] = 0.1f * noise(); }
        if (b == 100) i0 = instr_now();
        efx_slot_run(&sl, 0, 120.0f, 0, mem, L, R, 256);
    }
    tot = instr_now() - i0;
    return (double)tot / (300.0 * 256.0);
}
int main(void)
{
    for (int ef = 1; ef < NEF; ef++)
        printf("%-10s %7.1f %7.1f\n", EFX[ef].name, run(ef, 127), run(ef, 90));
    return 0;
}
