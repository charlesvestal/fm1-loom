/* SPDX-License-Identifier: GPL-3.0-only */
/* The two cores' split of the voice pool (app/engine.c split_at), measured on the host: a stretch's
 * critical path is the longer of the second core's voices [0, k) and the first core's voices [k, NVOICE)
 * plus its drum tracks. For a few loads (N synth voices, with and without a kit playing), the mean
 * instructions a sample of that path with the old fixed split (k = 8) and with split_at's, which this
 * file copies (keep the two the same; a voice's cost is synth_voice_cost's: brocade's by its saws). brocade:
 * a quarter of the notes on a nine-saw brocade track, the rest weft. The rest of the stretch (the FX, the master) runs after both and
 * is the same either way. Nothing here plays audio (macOS: proc_pid_rusage; elsewhere it prints 0).
 *   tests/host/split_bench.sh */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "synth.h"
#include "drumline.h"
#include "fastmath.h"
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

#define N 128
static uint8_t par[NTRK][NTP];
static _Alignas(16) uint8_t smem[SYNTH_BYTES];
static synth_t *S = (synth_t *)smem;
static uint8_t dbuf[DRUMLINE_BYTES] __attribute__((aligned(8)));
static drumline_t *dl = (drumline_t *)dbuf;
static float L[N], R[N], S1[N], S2[N];

static int split_at(int drums)                  /* app/engine.c split_at, with one drum unit */
{
    int pre[NVOICE + 1], i, k, drum = 0, best = NVOICE / 2, cost = 1 << 30;
    if (drums) {
        int nv = drumline_voices(dl);
        if (nv)
            drum += 1 + 2 * nv;
    }
    pre[0] = 0;
    for (i = 0; i < NVOICE; i++)
        pre[i + 1] = pre[i] + synth_voice_cost(S, i);
    for (k = 0; k <= NVOICE; k++) {
        int a = pre[k], b = pre[NVOICE] - pre[k] + drum, c = a > b ? a : b;
        int dk = k > NVOICE / 2 ? k - NVOICE / 2 : NVOICE / 2 - k, db = best > NVOICE / 2 ? best - NVOICE / 2 : NVOICE / 2 - best;
        if (c < cost || (c == cost && dk < db)) {
            cost = c;
            best = k;
        }
    }
    return best;
}

/* blocks of a held chord of nv voices (engine e), the kit hitting every 8th block when drums: the mean
 * critical path a sample */
static double run(int e, int nv, int drums, int dynamic, int *k_out)
{
    int t, b, s, k;
    uint64_t total = 0;
    for (t = 0; t < NTRK; t++)                   /* knit: track 1 a unison of seven, the others one voice */
        sound_default(par[t], e);
    if (e == EG_KNIT)
        par[0][TP_E3] = 7;
    synth_init(S, par);
    synth_tempo(S, 120.0f);
    memset(par[7], 0, NTP);
    for (s = 0; s < DL_NSOUND; s++)
        for (k = 0; k < DP_N; k++)
            par[7][TP_DRUM(s, k)] = DL_DEFAULT_KIT[s][k];
    par[7][TP_R] = 64;
    drumline_init(dl, par[7]);
    for (t = 0; t < nv; t++)
        synth_note_on(S, t % 4, 40 + 3 * t, 100, 0, (uint32_t)t + 1);
    for (b = 0; b < 300; b++) {
        uint64_t i0, c2, c1;
        if (drums && b % 8 == 0) {
            drumline_hit(dl, 0, 110, 0, 0, 0);
            drumline_hit(dl, 2, 90, 0, 0, 0);
            if (b % 16 == 0)
                drumline_hit(dl, 1, 100, 0, 0, 0);
        }
        memset(L, 0, sizeof L); memset(R, 0, sizeof R); memset(S1, 0, sizeof S1); memset(S2, 0, sizeof S2);
        synth_prepare(S, N);
        k = dynamic ? split_at(drums) : NVOICE / 2;
        i0 = instr_now();
        synth_render(S, 0, k, L, R, S1, S2, N);                  /* the second core */
        c2 = instr_now() - i0;
        i0 = instr_now();
        synth_render(S, k, NVOICE, L, R, S1, S2, N);             /* the first: its voices, the kit */
        if (drums && drumline_sounding(dl))
            drumline_render(dl, L, R, S1, S2, N);
        c1 = instr_now() - i0;
        if (b >= 20)
            total += c2 > c1 ? c2 : c1;
        *k_out = k;
    }
    return (double)total / (280.0 * N);
}

int main(void)
{
    static const int NV[] = {2, 4, 6, 8, 12, 16};
    int d, i, e;
    printf("-- the cores' split: the critical path, instructions a sample (fixed k = 8 -> split_at)\n");
    for (e = 0; e < 3; e++) {
        int eng = e == 2 ? EG_RAYON : e ? EG_JACQUARD : EG_KNIT;
        for (d = 0; d < 2; d++)
            for (i = 0; i < (int)(sizeof NV / sizeof NV[0]); i++) {
                int k0, k1;
                double a = run(eng, NV[i], d, 0, &k0), b = run(eng, NV[i], d, 1, &k1);
                printf("  %-10s %2d voices%s  %7.0f -> %7.0f  (%+5.1f %%, k %d)\n", EG_NAME[eng], NV[i],
                       d ? " + kit" : "      ", a, b, a > 0.0 ? 100.0 * (b - a) / a : 0.0, k1);
            }
    }
    return 0;
}
