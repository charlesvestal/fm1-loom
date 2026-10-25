/* SPDX-License-Identifier: GPL-3.0-only */
/* The Braids engines' cost and sound on their own (dsp/braids.h through synth.c's eng_* hooks): each cane and
 * voile model rendered as the voice path calls it (a prep when the knobs or the pitch move, eng_chunk every
 * 64 samples, eng_run SUB samples at a time), at three notes and two knob settings, with a slow vibrato so the
 * prep runs every chunk. Prints each model's engine instructions a sample (and ns a sample, the best of the
 * repetitions); "save" keeps the rendered sound, a later run compares with it (the largest and the RMS
 * difference against the signal's RMS, in dB).
 *   tests/host/braids_bench.sh [save] [novib] */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>
#ifndef SYNTH_C
#define SYNTH_C "../../app/dsp/synth.c"     /* (another synth.c to measure: -DSYNTH_C='"path"') */
#endif
#include SYNTH_C
#include "instr_count.h"                       /* instr_now, ns_now */
static double now_ns(void) { return (double)ns_now(); }

#define SECS 2
#define NS (44100 * SECS)
static const char *const CN_NM[NCANE] = {"pluck", "bow", "reed", "flute", "bell", "drum"};
static const char *const VO_NM[NVOILE] = {"vosim", "vowel", "choir", "whisper"};
static const int NOTES[3] = {40, 57, 81};
static const float KN[2][3] = {{0.5f, 0.5f, 0.5f}, {0.9f, 0.2f, 0.3f}};   /* timbre, color, breath / damp */
static voice_t vv;
static float vib_amt = 0.003f;                 /* ("novib": none, the prep every fourth chunk only) */
static float out[NS];

/* one note of the engine eng, model md: NS samples into out (when o) */
static void render(int eng, int md, int note, const float *kn, float *o)
{
    voice_t *v = &vv;
    int i, j, k;
    memset(v, 0, sizeof *v);
    v->engine = (uint8_t)eng;
    v->tws = 0;
    v->rng = 0x12345678u;
    eng_init(v);
    v->c.ek[0] = (float)md;
    v->c.e[1] = kn[0];
    v->c.e[2] = kn[1];
    v->c.e[3] = kn[2];
    for (i = 0; i < NS; i += SY_CHUNK) {
        float vib = vib_amt * fm_sin_turns((float)i * (5.0f / SY_SR));
        v->c.freq = 440.0f * fm_exp2f(((float)note - 69.0f) * (1.0f / 12.0f)) * (1.0f + vib);
        if (i == 0 || vib_amt > 0.0f || ((i / SY_CHUNK) & 3) == 0)
            eng_prep(v, 120.0f);
        if (i == 0) {
            if (eng == EG_CANE)
                cn_strike(v, 1);
            else
                vo_strike(v);
        }
        eng_chunk(v);
        for (j = 0; j < SY_CHUNK; j += SUB) {
            float xl[SUB], xr[SUB];
            eng_run(v, xl, xr, SUB);
            if (o)
                for (k = 0; k < SUB; k++)
                    o[i + j + k] = xl[k];
        }
    }
}

int main(int argc, char **argv)
{
    int save = argc > 1 && !strcmp(argv[1], "save"), e, md, nt, ks, rep;
    FILE *ref;
    if (argc > 1 && !strcmp(argv[argc - 1], "novib"))
        vib_amt = 0.0f;
    ref = fopen(vib_amt > 0.0f ? "build/host/braids_bench.ref" : "build/host/braids_bench_novib.ref", save ? "wb" : "rb");
    double worst_db = -1e9;
    static float rbuf[NS];
    if (!ref && !save)
        printf("(no reference: run with save first; timing only)\n");
    printf("engine only, instructions a sample (ns a sample, best of 5), notes 40 57 81 x 2 knob settings\n");
    for (e = 0; e < 2; e++) {
        int eng = e ? EG_VOILE : EG_CANE, nm = e ? NVOILE : NCANE;
        for (md = 0; md < nm; md++) {
            uint64_t ins = 0;
            double best = 1e30, sig = 0.0, dmax = 0.0, dsum = 0.0;
            long cnt = 0;
            for (rep = 0; rep < 5; rep++) {
                double t0 = now_ns(), t;
                uint64_t i0 = instr_now();
                for (nt = 0; nt < 3; nt++)
                    for (ks = 0; ks < 2; ks++)
                        render(eng, md, NOTES[nt], KN[ks], 0);
                t = now_ns() - t0;
                if (rep == 0)
                    ins = instr_now() - i0;
                if (t < best)
                    best = t;
            }
            for (nt = 0; nt < 3; nt++)          /* the sound, against the reference */
                for (ks = 0; ks < 2; ks++) {
                    int i;
                    render(eng, md, NOTES[nt], KN[ks], out);
                    if (save && ref)
                        fwrite(out, sizeof(float), NS, ref);
                    else if (ref && fread(rbuf, sizeof(float), NS, ref) == NS)
                        for (i = 0; i < NS; i++) {
                            double d = fabs((double)out[i] - (double)rbuf[i]);
                            sig += (double)rbuf[i] * (double)rbuf[i];
                            dsum += d * d;
                            if (d > dmax)
                                dmax = d;
                            cnt++;
                        }
                }
            printf("  %-6s %-8s %7.1f instr  %6.2f ns", e ? "voile" : "cane", e ? VO_NM[md] : CN_NM[md],
                   (double)ins / (6.0 * NS), best / (6.0 * NS));
            if (cnt) {
                double rs = sqrt(sig / (double)cnt);
                if (dmax == 0.0)
                    printf("   sound: identical\n");
                else {
                    double a = 20.0 * log10(dmax / rs), b = 20.0 * log10(sqrt(dsum / (double)cnt) / rs);
                    printf("   sound: max diff %.1f dB, rms diff %.1f dB (re the signal's rms)\n", a, b);
                    if (a > worst_db)
                        worst_db = a;
                }
            } else
                printf("\n");
        }
    }
    if (ref)
        fclose(ref);
    if (save)
        printf("saved the reference\n");
    return 0;
}
