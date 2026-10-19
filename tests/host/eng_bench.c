/* SPDX-License-Identifier: GPL-3.0-only */
/* An engine's cost on the host: for each preset of the engines named (or knit, rayon and jacquard), a chord of four
 * held through synth_render (the full voice path: engine, filter, character, outputs), and the engine's run
 * alone (eng_run on a copy of a sounding voice, its chunk configuration fixed). Instructions a voice-sample
 * (macOS: proc_pid_rusage) and nanoseconds. It includes synth.c to reach the static engine functions.
 * Nothing here plays audio.
 *   tests/host/eng_bench.sh [engine ...]
 *   BENCH_FLAGS='-DSYNTH_C="/path/synth.c"' tests/host/eng_bench.sh    (another synth.c: before / after)
 *   BENCH_MONO=1 tests/host/eng_bench.sh     (knit's width at 0: its voice path mono) */
#ifndef SYNTH_C
#define SYNTH_C "../../app/dsp/synth.c"
#endif
#include SYNTH_C
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
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
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
}

#define NB 256
#define NV 4
static uint8_t par[NTRK][NTP];
static _Alignas(16) uint8_t smem[SYNTH_BYTES];
static synth_t *S = (synth_t *)smem;
static float L[NB], R[NB], S1[NB], S2[NB];
static volatile float sink;

static void alone(double *ins, double *ns);
static double early_i, early_n, eng_ei, eng_en;
/* the full path: instructions and ns a voice-sample, over 200 blocks and over the first 20 (the attack); the
 * engine alone at the third block (early_*) */
static void full(int k, double *ins, double *ns)
{
    int t, b;
    uint64_t i0, n0, ti = 0, tn = 0;
    for (t = 0; t < NTRK; t++)
        sound_default(par[t], EG_KNIT);
    memcpy(par[0], PRESETS[k].par, NTP_SOUND);
    par[0][TP_VMODE] = VM_POLY;                  /* (a chord of four even for the mono presets) */
    if (getenv("BENCH_MONO") && PRESETS[k].par[TP_ENGINE] == EG_KNIT)
        par[0][TP_E4] = 0;                       /* (BENCH_MONO: width 0, the voice path mono) */
    synth_init(S, par);
    synth_tempo(S, 120.0f);
    synth_note_on(S, 0, 48, 100, 0, 1);
    synth_note_on(S, 0, 55, 90, 0, 2);
    synth_note_on(S, 0, 60, 110, 0, 3);
    synth_note_on(S, 0, 64, 80, 0, 4);
    for (b = 0; b < 200; b++) {
        memset(L, 0, sizeof L); memset(R, 0, sizeof R); memset(S1, 0, sizeof S1); memset(S2, 0, sizeof S2);
        synth_prepare(S, NB);
        i0 = instr_now();
        n0 = ns_now();
        synth_render(S, 0, NVOICE, L, R, S1, S2, NB);
        tn += ns_now() - n0;
        ti += instr_now() - i0;
        sink += L[3];
        if (b == 19) {
            early_i = (double)ti / (20.0 * NB * NV);
            early_n = (double)tn / (20.0 * NB * NV);
        }
        if (b == 2)
            alone(&eng_ei, &eng_en);
    }
    *ins = (double)ti / (200.0 * NB * NV);
    *ns = (double)tn / (200.0 * NB * NV);
}
/* the engine alone, on a copy of the chord's first sounding voice as it is now */
static void alone(double *ins, double *ns)
{
    static voice_t v;
    float xl[SUB], xr[SUB];
    int i, rep;
    uint64_t i0, n0;
    v = S->v[0];
    for (i = 0; i < NVOICE; i++)
        if (S->v[i].engine == v.engine && v_sounding(&S->v[i])) {
            v = S->v[i];
            break;
        }
    v.pos = 0;                                   /* (whole sub-blocks, as voice_samples runs them) */
    rep = 40000;
    i0 = instr_now();
    n0 = ns_now();
    for (i = 0; i < rep; i++) {
        eng_run(&v, xl, xr, SUB);
        sink += xl[5] + xr[7];
    }
    *ns = (double)(ns_now() - n0) / ((double)rep * SUB);
    *ins = (double)(instr_now() - i0) / ((double)rep * SUB);
}

int main(int argc, char **argv)
{
    static const int DEF[] = {EG_KNIT, EG_RAYON, EG_JACQUARD};
    int engines[NENGINE], ne = 0, a, k;
    for (a = 1; a < argc; a++)
        for (k = 0; k < NENGINE; k++)
            if (!strcmp(argv[a], EG_NAME[k]))
                engines[ne++] = k;
    if (!ne)
        for (k = 0; k < 3; k++)
            engines[ne++] = DEF[k];
    printf("instructions (and ns) a voice-sample: the full path over 4.6 s and over its first 116 ms; the\n"
           "engine alone at the attack (the third block) and held (after 4.6 s)\n");
    printf("%-9s %-14s %14s %14s %14s %14s\n", "engine", "preset", "full", "full attack", "eng attack", "eng held");
    for (a = 0; a < ne; a++) {
        double sfi = 0, sfn = 0, sei = 0, sen = 0, s1 = 0, s2 = 0, s3 = 0, s4 = 0;
        int n = 0;
        for (k = 0; k < NPRESETS; k++) {
            double fi, fn, ei, en;
            if (PRESETS[k].par[TP_ENGINE] != engines[a])
                continue;
            full(k, &fi, &fn);
            alone(&ei, &en);
            printf("%-9s %-14s %6.1f (%5.1f) %6.1f (%5.1f) %6.1f (%5.1f) %6.1f (%5.1f)\n", EG_NAME[engines[a]],
                   PRESETS[k].name, fi, fn, early_i, early_n, eng_ei, eng_en, ei, en);
            sfi += fi; sfn += fn; sei += ei; sen += en;
            s1 += early_i; s2 += early_n; s3 += eng_ei; s4 += eng_en;
            n++;
        }
        if (n)
            printf("%-9s %-14s %6.1f (%5.1f) %6.1f (%5.1f) %6.1f (%5.1f) %6.1f (%5.1f)\n\n", EG_NAME[engines[a]],
                   "MEAN", sfi / n, sfn / n, s1 / n, s2 / n, s3 / n, s4 / n, sei / n, sen / n);
    }
    return 0;
}
