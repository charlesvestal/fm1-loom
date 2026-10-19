/* SPDX-License-Identifier: GPL-3.0-only */
/* The Braids engines (dsp/braids.h: cane, voile, after Mutable Instruments Braids, MIT) on their own:
 * every model bounded and finite at its knobs' corners and across the notes; each model sounds (and the knobs
 * change it); the waveguides and the oscillators in tune (the measured pitch against the note's); the models'
 * raw levels near each other's; cane keeps to its CN_SLOTS voices, and stays bounded while notes steal and a
 * track turns to cane and back; the arp steps; a model turned mid-note stays bounded. Then the cost: instructions a sample of one voice (lp24, the whole
 * voice) for each model against an ordinary engine's, and what that makes the cores' split units.
 *   tests/host/braids_test.sh */
#include <stdio.h>
#include <string.h>
#include <math.h>
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
static uint64_t instr_now(void) { return (uint64_t)clock(); }
#endif
#include "synth.h"
int synth_sizeof(void);

static int bad;
static void check(const char *what, int ok)
{
    printf("%-72s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

#define N 256
#define SR 44100
static uint8_t par[NTRK][NTP];
static _Alignas(16) uint8_t mem[SYNTH_BYTES];
static synth_t *S = (synth_t *)mem;
static float L[N], R[N], S1[N], S2[N];
static int finite_ok = 1;
static float peak;

static int finite_f(float x) { return x == x && x < 1e30f && x > -1e30f; }
static void block(void)
{
    uint32_t i;
    memset(L, 0, sizeof L);
    memset(R, 0, sizeof R);
    memset(S1, 0, sizeof S1);
    memset(S2, 0, sizeof S2);
    synth_prepare(S, N);
    synth_render(S, 0, NVOICE, L, R, S1, S2, N);
    for (i = 0; i < N; i++) {
        if (!finite_f(L[i]) || !finite_f(R[i]))
            finite_ok = 0;
        if (fabsf(L[i]) > peak)
            peak = fabsf(L[i]);
        if (fabsf(R[i]) > peak)
            peak = fabsf(R[i]);
    }
}

#define NENG 2
static const int ENG[NENG] = {EG_CANE, EG_VOILE};
static const int NMOD[NENG] = {NCANE, NVOILE};
static const char *const MNAME[NENG][6] = {{"pluck", "bow", "reed", "flute", "bell", "drum"},
                                           {"vosim", "vowel", "choir", "whisper"}};

/* a plain sound: the filter wide open (lp12 at 20 kHz), full sustain, no LFO, no character */
static void reset(int eng, int model, int e2, int e3, int e4)
{
    int t;
    for (t = 0; t < NTRK; t++) {
        sound_default(par[t], eng);
        par[t][TP_E1] = (uint8_t)model;
        par[t][TP_E2] = (uint8_t)e2;
        par[t][TP_E3] = (uint8_t)e3;
        par[t][TP_E4] = (uint8_t)e4;
        par[t][TP_FTYPE] = FT_LP12;
        par[t][TP_CUT] = 127;
        par[t][TP_RES] = 0;
        par[t][TP_FENV] = 64;
        par[t][TP_KTRK] = 0;
        par[t][TP_A] = 0;
        par[t][TP_S] = 127;
        par[t][TP_R] = 40;
        par[t][TP_LTYPE] = LT_OFF;
        par[t][TP_CTYPE] = CT_OFF;
        par[t][TP_VOLUME] = 127;
        par[t][TP_LEVEL] = 108;          /* unity: tp_level_gain(108) x 100/127 = 0.99 */
        par[t][TP_PAN] = 64;
    }
    synth_init(S, par);
    synth_tempo(S, 120.0f);
}
static void held(int note, int skip, int nb, float *buf)
{
    int b;
    synth_note_on(S, 0, note, 127, 0, 1);
    for (b = 0; b < skip; b++)
        block();
    for (b = 0; b < nb; b++) {
        block();
        if (buf)
            memcpy(buf + b * N, L, sizeof L);
    }
}
static double rms(const float *x, int n)
{
    double a = 0.0;
    int i;
    for (i = 0; i < n; i++)
        a += (double)x[i] * (double)x[i];
    return sqrt(a / n);
}
/* the pitch: the normalised autocorrelation's best lag within +-40 % of the note's period, parabolic between lags */
static double pitch_of(const float *x, int n, double hz)
{
    double best = -2.0, p0 = SR / hz;
    int lo = (int)(p0 * 0.6), hi = (int)(p0 * 1.4) + 1, k, bk = lo, i;
    static double ac[4096];
    if (hi > 4000)
        hi = 4000;
    for (k = lo - 1; k <= hi + 1; k++) {
        double a = 0.0, e1 = 0.0, e2 = 0.0;
        for (i = 0; i + k < n; i++) {
            a += (double)x[i] * (double)x[i + k];
            e1 += (double)x[i] * (double)x[i];
            e2 += (double)x[i + k] * (double)x[i + k];
        }
        ac[k] = a / sqrt(e1 * e2 + 1e-30);
    }
    for (k = lo; k <= hi; k++)
        if (ac[k] > best && ac[k] >= ac[k - 1] && ac[k] >= ac[k + 1]) {
            best = ac[k];
            bk = k;
        }
    {
        double a = ac[bk - 1], b = ac[bk], c = ac[bk + 1], d = a - 2.0 * b + c, off = d != 0.0 ? 0.5 * (a - c) / d : 0.0;
        return SR / ((double)bk + off);
    }
}
static double cents(double f, double ref) { return 1200.0 * log2(f / ref); }
static double mtof(int n) { return 440.0 * pow(2.0, (n - 69) / 12.0); }

static float buf[N * 200];

int main(void)
{
    int e, m, n, b, k;
    printf("synth_t %d bytes of %d\n", synth_sizeof(), SYNTH_BYTES);

    /* bounded and finite: every model, knobs at corners and the middle, notes 24..108 */
    for (e = 0; e < NENG; e++) {
        int ok = 1;
        float worst = 0.0f;
        for (m = 0; m < NMOD[e]; m++)
            for (k = 0; k < 9; k++) {
                static const int V[3] = {0, 64, 127};
                int e4 = k / 3 == 0 ? 0 : k / 3 == 1 ? 64 : 127;
                for (n = 24; n <= 108; n += 21) {
                    reset(ENG[e], m, V[k % 3], V[(k + 1) % 3], e4);
                    finite_ok = 1;
                    peak = 0.0f;
                    held(n, 0, 30, 0);
                    if (!finite_ok || peak > 2.0f) {
                        printf("    %s %s knobs %d %d %d note %d: peak %.2f%s\n", EG_NAME[ENG[e]], MNAME[e][m], V[k % 3],
                               V[(k + 1) % 3], e4, n, (double)peak, finite_ok ? "" : " NOT FINITE");
                        ok = 0;
                    }
                    if (peak > worst)
                        worst = peak;
                }
            }
        printf("    %s: the loudest corner %.2f\n", EG_NAME[ENG[e]], (double)worst);
        check(e == 0 ? "cane: every model bounded and finite, knobs' corners, notes 24..108"
                     : "voile: every model bounded and finite, knobs' corners, notes 24..108", ok);
    }

    /* each model sounds, at its engine's default knobs: its raw level (the loudest 100 ms of a held A3's first
     * second, RMS) and peak */
    {
        int ok = 1;
        double lo = 1e9, hi = 0.0;
        printf("\nlevels: a held A3 (57), the default knobs, the raw voice (the loudest 100 ms, dBFS RMS / peak)\n");
        for (e = 0; e < 4; e++)
            for (m = 0; m < (e < 3 ? NMOD[e] : 1); m++) {
                double r = 0.0;
                int w;
                uint8_t d[NTP];
                if (e < 3) {
                    sound_default(d, ENG[e]);
                    reset(ENG[e], m, d[TP_E2], d[TP_E3], d[TP_E4]);
                } else {
                    reset(EG_KNIT, 40, 0, 20, 0);
                }
                peak = 0.0f;
                held(57, 0, 172, buf);
                for (w = 0; w + 4410 <= 172 * N; w += 2205) {
                    double x = rms(buf + w, 4410);
                    if (x > r)
                        r = x;
                }
                printf("  %-8s %-8s %6.1f / %6.1f\n", e < 3 ? EG_NAME[ENG[e]] : "weft", e < 3 ? MNAME[e][m] : "(scale)",
                       20.0 * log10(r + 1e-12), 20.0 * log10((double)peak + 1e-12));
                if (e == 3)
                    break;
                if (r < 0.01)
                    ok = 0;
                if (r < lo)
                    lo = r;
                if (r > hi)
                    hi = r;
            }
        check("every model sounds (over -40 dBFS RMS)", ok);
        printf("    the models' levels %.1f .. %.1f dBFS\n", 20.0 * log10(lo), 20.0 * log10(hi));
        check("the models' raw levels within 9 dB of each other", 20.0 * log10(hi / lo) < 9.0);
    }

    /* in tune: the pitched models' measured pitch against the note's, notes 36..84 */
    {
        static const struct { int e, m, e2, e3; double tol; } P[] = {
            {0, 0, 64, 64, 3.0},  {0, 1, 64, 64, 10.0}, {0, 2, 64, 64, 5.0},  {0, 3, 64, 64, 15.0},
            {1, 0, 40, 80, 5.0},  {1, 2, 0, 64, 5.0},   {2, 0, 127, 0, 5.0},  {2, 1, 64, 64, 5.0},
            {2, 3, 40, 40, 5.0}};
        printf("\npitch: the measured fundamental against the note, cents\n");
        for (k = 0; k < (int)(sizeof P / sizeof P[0]); k++) {
            int ok = 1;
            char what[96];
            printf("  %-8s %-6s", EG_NAME[ENG[P[k].e]], MNAME[P[k].e][P[k].m]);
            for (n = 36; n <= 84; n += 12) {
                double f, c;
                reset(ENG[P[k].e], P[k].m, P[k].e2, P[k].e3, 0);
                held(n, 40, 40, buf);
                f = pitch_of(buf, 40 * N, mtof(n));
                c = cents(f, mtof(n));
                printf(" %3d:%+6.1f", n, c);
                if (fabs(c) > P[k].tol)
                    ok = 0;
            }
            printf("\n");
            sprintf(what, "%s %s in tune (within %.0f cents, notes 36..84)", EG_NAME[ENG[P[k].e]], MNAME[P[k].e][P[k].m],
                    P[k].tol);
            check(what, ok);
        }
    }

    /* the knobs change the sound: timbre and color at 20 and 110, the RMS difference of the two renders */
    {
        int ok = 1;
        static float a[N * 40];
        for (e = 0; e < NENG; e++)
            for (m = 0; m < NMOD[e]; m++) {
                int kn;
                for (kn = 0; kn < 2; kn++) {
                    double d;
                    int i;
                    reset(ENG[e], m, kn ? 64 : 20, kn ? 20 : 64, 0);
                    held(57, 10, 40, a);
                    reset(ENG[e], m, kn ? 64 : 110, kn ? 110 : 64, 0);
                    held(57, 10, 40, buf);
                    for (i = 0; i < 40 * N; i++)
                        buf[i] -= a[i];
                    d = rms(buf, 40 * N) / (rms(a, 40 * N) + 1e-9);
                    if (d < 0.05) {
                        printf("    %s %s: %s changes little (%.3f)\n", EG_NAME[ENG[e]], MNAME[e][m], kn ? "color" : "timbre", d);
                        ok = 0;
                    }
                }
            }
        check("timbre and color change every model's sound", ok);
    }

    /* cane's cap: eight notes on one track, then another track's; never more than CN_SLOTS cane voices */
    {
        int ok = 1;
        reset(EG_CANE, 0, 64, 64, 0);
        for (n = 0; n < 8; n++)
            synth_note_on(S, 0, 48 + 3 * n, 100, 0, (uint32_t)n + 1);
        for (b = 0; b < 20; b++)
            block();
        printf("    eight cane notes: %d voices sounding\n", synth_voices_used(S));
        ok &= synth_voices_used(S) == CN_SLOTS;
        for (n = 0; n < 3; n++)
            synth_note_on(S, 1, 60 + 4 * n, 100, 0, (uint32_t)n + 100);
        for (b = 0; b < 20; b++)
            block();
        ok &= synth_voices_used(S) == CN_SLOTS && finite_ok;
        check("cane keeps to its CN_SLOTS voices", ok);
        reset(EG_CANE, 1, 64, 64, 0);            /* others beside it keep theirs */
        par[1][TP_ENGINE] = EG_KNIT;
        for (n = 0; n < 4; n++)
            synth_note_on(S, 0, 48 + 3 * n, 100, 0, (uint32_t)n + 1);
        for (n = 0; n < 4; n++)
            synth_note_on(S, 1, 60 + 3 * n, 100, 0, (uint32_t)n + 10);
        for (b = 0; b < 20; b++)
            block();
        check("other engines beside cane keep their voices", synth_voices_used(S) == 8);
        /* churn: fast notes on two cane tracks and a weft one, stolen and released; bounded throughout */
        reset(EG_CANE, 0, 100, 64, 0);
        par[1][TP_E1] = 1;
        par[2][TP_ENGINE] = EG_KNIT;
        finite_ok = 1;
        peak = 0.0f;
        for (b = 0; b < 400; b++) {
            int t = b % 3;
            synth_note_on(S, t, 40 + (b * 7) % 40, 100, 0, (uint32_t)b + 1);
            if (b > 3)
                synth_note_off(S, (b - 3) % 3, (uint32_t)b - 2);
            if (b % 50 == 25)                    /* a track turned to cane and back mid-flight */
                par[2][TP_ENGINE] = par[2][TP_ENGINE] == EG_KNIT ? EG_CANE : EG_KNIT;
            block();
        }
        printf("    churn: peak %.2f\n", (double)peak);
        check("cane under churn: bounded, finite", finite_ok && peak < 4.0f);
    }

    /* the model turned mid-note: bounded */
    {
        finite_ok = 1;
        peak = 0.0f;
        for (e = 0; e < NENG; e++) {
            reset(ENG[e], 0, 64, 64, 0);
            synth_note_on(S, 0, 50, 100, 0, 1);
            for (b = 0; b < 60; b++) {
                par[0][TP_E1] = (uint8_t)((b / 5) % NMOD[e]);
                block();
            }
        }
        check("a model turned mid-note: bounded, finite", finite_ok && peak < 4.0f);
    }

    /* the cost: one voice held, lp24, its instructions a sample */
    {
        double base = 0.0;
        printf("\ncost: one voice, lp24, instructions a sample (the whole voice: engine, filter, envelopes, outputs)\n");
        for (k = 0; k < 2; k++) {
            uint64_t t0, t;
            int eg = k ? EG_RAYON : EG_KNIT;
            reset(EG_CANE, 0, 64, 64, 0);
            sound_default(par[0], eg);
            par[0][TP_FTYPE] = FT_LP24;
            par[0][TP_S] = 127;
            synth_note_on(S, 0, 57, 100, 0, 1);
            for (b = 0; b < 10; b++)
                block();
            t0 = instr_now();
            for (b = 0; b < 400; b++)
                block();
            t = instr_now() - t0;
            printf("  %-8s (default)   %6.1f\n", EG_NAME[eg], (double)t / (400.0 * N));
            base += (double)t / (800.0 * N);
        }
        for (e = 0; e < NENG; e++)
            for (m = 0; m < NMOD[e]; m++) {
                uint64_t t0, t;
                double c, worst = 0.0;
                int nt;
                for (nt = 0; nt < 2; nt++) {     /* a low note and a high one: the worse */
                    uint8_t d[NTP];
                    sound_default(d, ENG[e]);
                    reset(ENG[e], m, d[TP_E2], d[TP_E3], 64);
                    par[0][TP_FTYPE] = FT_LP24;
                    par[0][TP_E2] = 127;         /* timbre up: the longest ring (pluck, bell, drum) */
                    synth_note_on(S, 0, nt ? 81 : 40, 100, 0, 1);
                    for (b = 0; b < 10; b++)
                        block();
                    t0 = instr_now();
                    for (b = 0; b < 400; b++)
                        block();
                    t = instr_now() - t0;
                    c = (double)t / (400.0 * N);
                    if (c > worst)
                        worst = c;
                }
                printf("  %-8s %-8s %6.1f  (%.2f x the ordinary voice; %.1f split units)\n", EG_NAME[ENG[e]], MNAME[e][m],
                       worst, base > 0.0 ? worst / base : 0.0, base > 0.0 ? 4.0 * worst / base : 0.0);
            }
    }
    printf(bad ? "\n%d FAILED\n" : "\nall ok\n", bad);
    return bad != 0;
}
