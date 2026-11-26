/* SPDX-License-Identifier: MPL-2.0 */
/* The effects' knobs turned while they play: no clicks (dsp/efx.c). Every effect of the catalog (the reverb in each of
 * its modes) in every place it runs: a synth track's slot (the synth: a held note, mellow, its slot 1), a kit's slot
 * (the synth's kit path, synth_kit_slots, on a steady pair), a send (fx.c, as the engine runs it: a block late for
 * those that take the lag) and the master's slot (fx.c master_process). A steady signal through it (the note;
 * elsewhere two sines and a little low-passed noise at about a track's level) and, mid-stream, one of its knobs, its
 * mix, an extra or the effect itself stepped from one value to another (small, half the range, the whole range up and
 * down; a list's values in turn; the effect from off, to off, from drive). The click: the high-passed energy (above 8
 * kHz, 24 dB an octave) in 2 ms windows from the step to 40 ms after it, over the largest of the same windows' energy
 * within 150 ms either side in renders that never moved (at the value before, at the value after and, a knob's
 * sweep, at three between; another effect, dry: it passes there; no less than 60 dB under their whole signal): a turn
 * may make what the values it passes make, not more. In dB; the table prints each effect's worst per place, and the
 * test fails on any above SLEW_DB (6 dB; an SPU reverb's size SPU_DB). Before the glides (2026-11-25) a step of the
 * tilt's tilt across its range made +9 dB on a kit, a chorus's depth +22, a send's lofi's bits +59, a slot turned off
 * on a synth track +32 (its voices' gain off the bus); a moving LFO, a warp of a delay's time makes a few dB.
 *   tests/host/slew_test.sh [-v]     (-v: every step's number) */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "synth.h"
#include "fx.h"
#include "efx.h"
#include "fastmath.h"

#define N 256                                  /* the engine's block */
#define NB 150                                 /* blocks a render: 0.87 s */
#define TB 110                                 /* the step's block (0.64 s: the reverbs well filled) */
#define LEN (NB * N)
#define T0 (TB * N)
#define W 88                                   /* the window: 2 ms */
#define HOP 22
#define AFTER (int)(0.040f * 44100.0f)         /* the turn's reach */
#define AROUND (int)(0.150f * 44100.0f)        /* the references' */
#define FLOOR 1e-6                             /* the references' least: -60 dB under the signal (a quiet high end
                                                * makes no step audible below that) */
#ifndef SLEW_DB
#define SLEW_DB 6.0f
#endif

static uint8_t par[NTRK + 1][NTP];
static _Alignas(16) uint8_t smem[SYNTH_BYTES];
static synth_t *S = (synth_t *)smem;
static uint8_t fxb[FXSLOT_BYTES] __attribute__((aligned(8)));
static uint8_t msb[MASTER_BYTES] __attribute__((aligned(8)));
static fxslot_t *FX = (fxslot_t *)fxb;
static master_t *MS = (master_t *)msb;
static float sig[2][LEN];                      /* the steady pair */
#define NR 6                                   /* the step's render, then the references: before's, after's and (a
                                                * knob's sweep) three values between */
static float outv[NR][2][LEN];
static int nref;

enum { WH_TRACK, WH_KIT, WH_SEND, WH_MASTER, NWH };
static const char *const WH_NAME[NWH] = {"track", "kit", "send", "master"};
/* the controls: knobs 1..3, the mix, the extras 1..4, the effect itself */
enum { C_K1, C_K2, C_K3, C_MIX, C_X1, C_X2, C_X3, C_X4, C_EF, NC };
static const char *const C_NAME[NC] = {"k1", "k2", "k3", "mix", "x1", "x2", "x3", "x4", "effect"};

typedef struct { int ef, mode; uint8_t v[NC]; } setting_t;   /* (v[C_EF] unused: ef) */

static void make_signal(void)
{
    uint32_t r = 4242;
    float lp = 0.0f, lq = 0.0f;
    for (int i = 0; i < LEN; i++) {
        float u = (float)i * (1.0f / 44100.0f), nl, nr;
        r = r * 1664525u + 1013904223u;
        nl = (float)(int32_t)r * (1.0f / 2147483648.0f);
        r = r * 1664525u + 1013904223u;
        nr = (float)(int32_t)r * (1.0f / 2147483648.0f);
        lp += 0.13f * (nl - lp);               /* (~1 kHz) */
        lq += 0.13f * (nr - lq);
        sig[0][i] = 0.3f * fm_sin_turns(u * 220.0f) + 0.15f * fm_sin_turns(u * 331.0f + 0.1f) + 0.06f * lp;
        sig[1][i] = 0.3f * fm_sin_turns(u * 220.0f + 0.05f) + 0.15f * fm_sin_turns(u * 277.0f) + 0.06f * lq;
    }
}

/* a slot's values into a track's par (slot 1) */
static void par_slot(uint8_t *p, const setting_t *s)
{
    p[TP_SLOT[0][SL_EF]] = (uint8_t)s->ef;
    p[TP_SLOT[0][SL_K1]] = s->v[C_K1];
    p[TP_SLOT[0][SL_K2]] = s->v[C_K2];
    p[TP_SLOT[0][SL_K3]] = s->v[C_K3];
    p[TP_SLOT[0][SL_MIX]] = s->v[C_MIX];
    for (int j = 0; j < 4; j++)
        p[TP_SLOT[0][SL_X1 + j]] = s->v[C_X1 + j];
}
static void fx_put(const setting_t *s)
{
    const uint8_t k[3] = {s->v[C_K1], s->v[C_K2], s->v[C_K3]}, x[4] = {s->v[C_X1], s->v[C_X2], s->v[C_X3], s->v[C_X4]};
    fx_set(FX, s->ef, k, x);
}
static void ms_put(const setting_t *s)
{
    master_set(MS, MS_CTYPE, s->ef);
    master_set(MS, MS_CAMT, s->v[C_K1]);
    master_set(MS, MS_CTONE, s->v[C_K2]);
    master_set(MS, MS_CK3, s->v[C_K3]);
    master_set(MS, MS_CMIX, s->v[C_MIX]);
    for (int j = 0; j < 4; j++)
        master_set(MS, MS_CX1 + j, s->v[C_X1 + j]);
}

/* one render: setting a, then b from block TB (a == b: never moved), into o */
static void render(int pl, const setting_t *a, const setting_t *b, float (*o)[LEN])
{
    static float L[N], R[N], S1[N], S2[N], in[N];
    efx_send_used = (efx_mask_t){{0}};
    memset(efx_pool, 0, sizeof efx_pool);
    if (pl == WH_TRACK || pl == WH_KIT) {
        for (int t = 0; t <= NTRK; t++)
            memset(par[t], 0, NTP);
        if (pl == WH_TRACK) {
            for (int t = 0; t < NTRK; t++)
                sound_default(par[t], EG_KNIT);
            par[0][TP_A] = 0;
            par[0][TP_S] = 127;
            par[0][TP_CUT] = 50;                 /* (a mellow note, ~0.5 kHz: a click is not hidden in its edges) */
            par[0][TP_RES] = 0;
        } else
            par[0][TP_ENGINE] = EG_DRUMLINE;
        par_slot(par[0], a);
        synth_init(S, par);
        synth_tempo(S, 120.0f);
        if (pl == WH_TRACK)
            synth_note_on(S, 0, 57, 100, 0, 1);
    } else if (pl == WH_SEND) {
        fx_init(FX);
        fx_tempo(FX, 120.0f);
        fx_put(a);
    } else {
        master_init(MS);
        master_tempo(MS, 120.0f);
        ms_put(a);
    }
    for (int bk = 0; bk < NB; bk++) {
        const int at = bk * N;
        if (bk == TB && b != a) {
            if (pl == WH_TRACK || pl == WH_KIT)
                par_slot(par[0], b);
            else if (pl == WH_SEND)
                fx_put(b);
            else
                ms_put(b);
        }
        memset(L, 0, sizeof L);
        memset(R, 0, sizeof R);
        memset(S1, 0, sizeof S1);
        memset(S2, 0, sizeof S2);
        if (pl == WH_TRACK) {
            synth_prepare(S, N);
            synth_render(S, 0, NVOICE, L, R, S1, S2, N);
        } else if (pl == WH_KIT) {
            memcpy(L, sig[0] + at, sizeof L);
            memcpy(R, sig[1] + at, sizeof R);
            synth_prepare(S, N);
            synth_kit_slots(S, 0, L, R, 0, N);
        } else if (pl == WH_SEND) {
            fx_memory(FX, efx_pool[0], fx_need(FX));
            for (int i = 0; i < N; i++)
                in[i] = 0.5f * (sig[0][at + i] + sig[1][at + i]);
            if (fx_lags(FX))                   /* (the engine's: a block late, the lag taken back) */
                fx_process_lag(FX, in, L, R, N, N);
            else
                fx_process(FX, in, L, R, N);
        } else {
            static float z[N];
            memcpy(S1, sig[0] + at, sizeof S1);
            memcpy(S2, sig[1] + at, sizeof S2);
            master_memory(MS, efx_pool[0], master_need(MS));
            master_process(MS, z, z, S1, S2, L, R, N);
        }
        memcpy(o[0] + at, L, sizeof L);
        memcpy(o[1] + at, R, sizeof R);
    }
}

/* the outputs high-passed: a fourth-order Butterworth at 8 kHz (two RBJ sections), both sides, into hp[k][c] */
static float hp[NR][2][LEN];
static void high_pass(void)
{
    static const double Q[2] = {0.54119610, 1.30656296};
    const double w = 2.0 * 3.14159265358979 * 8000.0 / 44100.0, cw = cos(w), sw = sin(w);
    for (int k = 0; k < nref; k++)
        for (int c = 0; c < 2; c++) {
            const float *x = outv[k][c];
            float *y = hp[k][c];
            for (int i = 0; i < LEN; i++)
                y[i] = x[i];
            for (int q = 0; q < 2; q++) {
                const double al = sw / (2.0 * Q[q]), a0 = 1.0 + al;
                const double b0 = (1.0 + cw) / 2.0 / a0, b1 = -(1.0 + cw) / a0, a1 = -2.0 * cw / a0, a2 = (1.0 - al) / a0;
                double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;
                for (int i = 0; i < LEN; i++) {
                    const double v = (double)y[i], o = b0 * v + b1 * x1 + b0 * x2 - a1 * y1 - a2 * y2;
                    x2 = x1, x1 = v, y2 = y1, y1 = o;
                    y[i] = (float)o;
                }
            }
        }
}
static double win_e(int k, int at)              /* a window's high-passed energy, both sides */
{
    double e = 0.0;
    for (int c = 0; c < 2; c++)
        for (int i = at; i < at + W; i++)
            e += (double)hp[k][c][i] * (double)hp[k][c][i];
    return e;
}
/* the step's worst window over the references' largest, dB (and whether anything came out at all) */
static float click_db(void)
{
    double worst = 0.0, ref = 1e-12;
    high_pass();
    for (int at = T0 - AROUND; at <= T0 + AROUND; at += HOP)
        for (int k = 1; k < nref; k++) {
            double e = win_e(k, at), r = 0.0;
            for (int c = 0; c < 2; c++)           /* (the floor: 60 dB under the signal itself) */
                for (int i = at; i < at + W; i++)
                    r += (double)outv[k][c][i] * (double)outv[k][c][i];
            e = e > r * FLOOR ? e : r * FLOOR;
            ref = e > ref ? e : ref;
        }
    for (int at = T0 - W / 2; at <= T0 + AFTER; at += HOP) {
        double e = win_e(0, at);
        worst = e > worst ? e : worst;
    }
    return (float)(10.0 * log10(worst / ref + 1e-12));
}

static int verbose;
static const char *efn(const setting_t *a, const setting_t *b)   /* a step's effect's name (the reverb's mode too) */
{
    static char nm[32];
    const int ef = b->ef ? b->ef : a->ef;
    snprintf(nm, sizeof nm, "%s%s%s", EFX[ef].name, ef == EF_REVERB ? " " : "", ef == EF_REVERB ? RV_NAME[a->mode] : "");
    return nm;
}
static float step_db(int pl, const setting_t *a, const setting_t *b, const char *what, int c)
{
    float d;
    render(pl, a, b, outv[0]);
    render(pl, a, a, outv[1]);
    render(pl, b, b, outv[2]);
    nref = 3;
    if (c == C_EF && a->ef && b->ef) {           /* (another effect: through dry, the old one fading out) */
        setting_t m = *b;
        m.ef = EF_OFF;
        render(pl, &m, &m, outv[nref++]);
    } else if (c >= 0 && c < C_EF)               /* (a knob's sweep passes every value between) */
        for (int k = 1; k <= 3; k++) {
            setting_t m = *a;
            m.v[c] = (uint8_t)(a->v[c] + (b->v[c] - a->v[c]) * k / 4);
            render(pl, &m, &m, outv[nref++]);
        }
    for (int c = 0; c < 2; c++)                  /* (nothing to hear: no click) */
        for (int i = 0; i < LEN; i++)
            if (!(outv[0][c][i] == outv[0][c][i]))
                return 99.0f;
    d = click_db();
    {   /* (SLEW_WAV="track tilt to off": that step's three renders, and the step less the after's, as text) */
        const char *w = getenv("SLEW_WAV");
        char me[80];
        snprintf(me, sizeof me, "%s %s %s", WH_NAME[pl], efn(a, b), what);
        if (w && !strcmp(w, me)) {
            FILE *f = fopen("build/host/slew_dump.txt", "w");
            for (int i = T0 - 2000; f && i < T0 + 4000; i++)
                fprintf(f, "%d %.7f %.7f %.7f %.7f %.7f %.7f\n", i - T0, (double)outv[0][0][i], (double)outv[1][0][i],
                        (double)outv[2][0][i], (double)hp[0][0][i], (double)outv[0][1][i], (double)hp[0][1][i]);
            if (f)
                fclose(f);
        }
    }
    if (verbose)
        printf("      %-8s %-18s %-24s %+6.1f dB\n", WH_NAME[pl], efn(a, b), what, (double)d);
    return d;
}

static void defaults(setting_t *s, int ef, int mode)
{
    memset(s, 0, sizeof *s);
    s->ef = ef;
    s->mode = mode;
    for (int j = 0; j < 3; j++)
        s->v[C_K1 + j] = EFX[ef].def[j];
    s->v[C_MIX] = 100;
    for (int j = 0; j < 4; j++)
        s->v[C_X1 + j] = EFX[ef].xdef[j];
    if (ef == EF_REVERB)
        s->v[C_X1] = (uint8_t)mode;
}
static int has(int ef, int c)
{
    if (c <= C_K3)
        return EFX[ef].word[c] != 0;
    if (c == C_MIX)
        return 1;
    if (c <= C_X4)
        return EFX[ef].word[3 + c - C_X1] != 0 && !(ef == EF_REVERB && c == C_X1);   /* (the mode: its own rows) */
    return 1;
}

/* the most a step may make: SLEW_DB; an SPU reverb's size (its addresses glide a tick at a time, written at the
 * nearest sample: a full turn of an echo mode warps its repeats for seconds, with a faint jitter) SPU_DB */
#define SPU_DB 10.0f
static float limit(int ef, int mode, int c)
{
    return ef == EF_REVERB && mode != RV_PLATE && !rv_lush(mode) && c == C_K1 ? SPU_DB : SLEW_DB;
}
static int fails;
static float worst_all;
int main(int argc, char **argv)
{
    static const uint8_t ST[4][2] = {{64, 72}, {32, 96}, {0, 127}, {127, 0}};
    static const char *const STN[4] = {"small", "half", "full up", "full down"};
    verbose = argc > 1 && !strcmp(argv[1], "-v");
    make_signal();
    printf("the effects' knobs stepped while they play: the worst click a place, dB over what either value makes "
           "(fails above %+.0f)\n", (double)SLEW_DB);
    printf("  %-18s %-7s", "effect", "control");
    for (int pl = 0; pl < NWH; pl++)
        printf(" %10s", WH_NAME[pl]);
    printf("\n");
    for (int ef = 1; ef < NEF; ef++)
        for (int mode = 0; mode < (ef == EF_REVERB ? NRV : 1); mode++) {
            char en[32];
            snprintf(en, sizeof en, "%s%s%s", EFX[ef].name, ef == EF_REVERB ? " " : "", ef == EF_REVERB ? RV_NAME[mode] : "");
            for (int c = 0; c < NC; c++) {
                float w[NWH];
                if (!has(ef, c))
                    continue;
                for (int pl = 0; pl < NWH; pl++) {
                    setting_t a, b;
                    w[pl] = -99.0f;
                    if (pl == WH_SEND && c == C_MIX)          /* (a send's mix is its return: the engine's, glided) */
                        continue;
                    if (c == C_EF) {                           /* the effect: from off, to off, from another */
                        static const char *const EN[3] = {"from off", "to off", "from drive"};
                        for (int k = 0; k < 3; k++) {
                            float d;
                            defaults(&a, ef, mode);
                            b = a;
                            if (k == 0)
                                a.ef = EF_OFF;
                            else if (k == 1)
                                b.ef = EF_OFF;
                            else
                                a.ef = ef == EF_DRIVE ? EF_RAT : EF_DRIVE;
                            d = step_db(pl, &a, &b, EN[k], C_EF);
                            w[pl] = d > w[pl] ? d : w[pl];
                        }
                        continue;
                    }
                    if (EFX[ef].kind[c < 3 ? c : 0] == EK_LIST && c < 3) {   /* (juno's mode: each in turn) */
                        for (int k = 0; k < 3; k++) {
                            char what[40];
                            float d;
                            defaults(&a, ef, mode);
                            b = a;
                            a.v[c] = (uint8_t)k;
                            b.v[c] = (uint8_t)((k + 1) % 3);
                            snprintf(what, sizeof what, "%s %d -> %d", C_NAME[c], k, (k + 1) % 3);
                            d = step_db(pl, &a, &b, what, -1);
                            w[pl] = d > w[pl] ? d : w[pl];
                        }
                        continue;
                    }
                    for (int k = 0; k < 4; k++) {
                        char what[40];
                        float d;
                        defaults(&a, ef, mode);
                        b = a;
                        a.v[c] = ST[k][0];
                        b.v[c] = ST[k][1];
                        snprintf(what, sizeof what, "%s %s", C_NAME[c], STN[k]);
                        d = step_db(pl, &a, &b, what, c);
                        w[pl] = d > w[pl] ? d : w[pl];
                    }
                }
                printf("  %-18s %-7s", en, C_NAME[c]);
                for (int pl = 0; pl < NWH; pl++) {
                    if (w[pl] <= -99.0f)
                        printf(" %10s", "-");
                    else
                        printf(" %9.1f%s", (double)w[pl], w[pl] > limit(ef, mode, c) ? "!" : " ");
                    fails += w[pl] > limit(ef, mode, c);
                    worst_all = w[pl] > worst_all ? w[pl] : worst_all;
                }
                printf("\n");
                fflush(stdout);
            }
        }
    printf("worst %+.1f dB; %d over their limit\n", (double)worst_all, fails);
    return fails ? 1 : 0;
}
