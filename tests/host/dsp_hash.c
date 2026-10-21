/* SPDX-License-Identifier: GPL-3.0-only */
/* Fingerprints of the DSP's output, for refactors that must not change a sample: every synth engine through
 * every filter type and character type, every factory preset, every drum model through every key filter and
 * the dynamic filter, every send effect and reverb mode, the master at its EQ / compressor's
 * settings. One line per case: its name and a 64-bit FNV-1a of the output's float bits. Run it before and
 * after a change and compare (tests/host/dsp_hash.sh does both: `dsp_hash.sh save`, then `dsp_hash.sh`).
 * Nothing here plays audio. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "synth.h"
#include "drumline.h"
#include "fx.h"
#include "fastmath.h"

#define N 256
static uint64_t h;
static float energy;                              /* (shown: a silent case would prove nothing) */
static void hash_init(void) { h = 1469598103934665603ull; energy = 0.0f; }
static void hash_buf(const float *x, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        uint32_t u;
        int k;
        memcpy(&u, &x[i], 4);
        energy += x[i] * x[i];
        for (k = 0; k < 4; k++) {
            h ^= (u >> (8 * k)) & 0xFFu;
            h *= 1099511628211ull;
        }
    }
}
static void out(const char *name) { printf("%-40s %016llx %10.4g\n", name, (unsigned long long)h, (double)energy); }

static uint32_t rng = 12345;
static float noise(void)
{
    rng = rng * 1664525u + 1013904223u;
    return (float)(int32_t)rng * (1.0f / 2147483648.0f);
}

/* ---- the synth ---- */
static uint8_t par[NTRK + 1][NTP];          /* (+ the conductor's row: the shared LFO, off) */
static _Alignas(16) uint8_t smem[SYNTH_BYTES];
static synth_t *S = (synth_t *)smem;
static float L[N], R[N], S1[N], S2[N];

static void synth_blocks(int nb)
{
    int b;
    for (b = 0; b < nb; b++) {
        memset(L, 0, sizeof L); memset(R, 0, sizeof R); memset(S1, 0, sizeof S1); memset(S2, 0, sizeof S2);
        synth_prepare(S, N);
        synth_render(S, NVOICE / 2, NVOICE, L, R, S1, S2, N);   /* (two ranges, as the two cores) */
        synth_render(S, 0, NVOICE / 2, L, R, S1, S2, N);
        hash_buf(L, N); hash_buf(R, N); hash_buf(S1, N); hash_buf(S2, N);
    }
}
static void synth_case(const char *name)
{
    int t;
    synth_init(S, par);
    synth_tempo(S, 120.0f);
    hash_init();
    synth_note_on(S, 0, 48, 100, 0, 1);
    synth_note_on(S, 0, 55, 90, 0, 2);
    synth_note_on(S, 0, 63, 110, 0, 3);
    synth_note_on(S, 1, 36, 120, 0, 4);
    synth_blocks(30);
    synth_lock(S, 0, TP_CUT, 20);                 /* a lock moves the filter */
    synth_note_on(S, 0, 72, 64, 0, 5);            /* a steal-free fifth note */
    synth_blocks(20);
    synth_lock(S, 0, TP_CUT, LOCK_NONE);
    for (t = 1; t <= 5; t++)
        synth_note_off(S, t == 4 ? 1 : 0, (uint32_t)t);
    synth_blocks(30);
    out(name);
}
static void synth_cases(void)
{
    int e, ft, ct, k;
    char name[64];
    for (e = 0; e < NENGINE; e++) {
        if (!eg_synth(e))
            continue;
        for (ft = 0; ft < NFT; ft++)
            for (ct = 0; ct < NCT; ct++) {
                int t;
                for (t = 0; t < NTRK; t++)
                    sound_default(par[t], e);
                par[0][TP_FTYPE] = (uint8_t)ft;
                par[0][TP_RES] = 90;
                par[0][TP_FENV] = 100;
                par[0][TP_E1] = 90; par[0][TP_E2] = 30; par[0][TP_E3] = 100; par[0][TP_E4] = 110;
                par[0][TP_CTYPE] = (uint8_t)ct;
                par[0][TP_CAMT] = 90; par[0][TP_CTONE] = 70; par[0][TP_CMIX] = 110;
                par[0][TP_LTYPE] = LT_WAVE; par[0][TP_LDEST] = LD_FILTER; par[0][TP_LAMT] = 100;
                par[0][TP_SEND1] = 60; par[0][TP_SEND2] = 30;
                par[1][TP_FTYPE] = (uint8_t)((ft + 1) % NFT);
                if (e == EG_SAMPLE) {             /* the sung ah, looped */
                    par[0][TP_E1] = 4; par[0][TP_E2] = 0; par[0][TP_E3] = 127; par[0][TP_E4] = SM_LOOP;
                }
                snprintf(name, sizeof name, "synth %s %d %d", EG_NAME[e], ft, ct);
                synth_case(name);
            }
    }
    for (k = 0; k < NPRESETS; k++) {
        int t;
        for (t = 0; t < NTRK; t++)
            sound_default(par[t], EG_KNIT);
        memcpy(par[0], PRESETS[k].par, NTP_SOUND);
        memcpy(par[1], PRESETS[(k + 7) % NPRESETS].par, NTP_SOUND);
        snprintf(name, sizeof name, "preset %d %s", k, PRESETS[k].name);
        synth_case(name);
    }
}

/* ---- the drumline ---- */
static uint8_t dpar[NTP];
static uint8_t dbuf[DRUMLINE_BYTES] __attribute__((aligned(8)));
static drumline_t *dl = (drumline_t *)dbuf;

static void drum_blocks(int nb)
{
    int b;
    for (b = 0; b < nb; b++) {
        memset(L, 0, sizeof L); memset(R, 0, sizeof R); memset(S1, 0, sizeof S1); memset(S2, 0, sizeof S2);
        drumline_render(dl, L, R, S1, S2, 64);
        hash_buf(L, 64); hash_buf(R, 64); hash_buf(S1, 64); hash_buf(S2, 64);
    }
}
static void drum_cases(void)
{
    int m, f, s, k;
    char name[64];
    for (m = 0; m < NDM; m++)
        for (f = 0; f < NDF + 1; f++) {
            memset(dpar, 0, sizeof dpar);
            for (s = 0; s < DL_NSOUND; s++)
                for (k = 0; k < DP_N; k++)
                    dpar[TP_DRUM(s, k)] = DL_DEFAULT_KIT[s][k];
            dpar[TP_R] = 64; dpar[TP_S] = 20;
            for (s = 0; s < DL_NSOUND; s++) {         /* (the keys' own sends) */
                dpar[TP_DRUM(s, DP_SEND1)] = 64;
                dpar[TP_DRUM(s, DP_SEND2)] = 32;
            }
            dpar[TP_DRUM(0, DP_MODEL)] = (uint8_t)m;
            for (k = 0; k < 4; k++)
                dpar[TP_DRUM(0, DP_K1 + k)] = DM_DEFAULT[m][k];
            if (f < NDF) {
                dpar[TP_DRUM(0, DP_FTYPE)] = (uint8_t)f;
                dpar[TP_DRUM(0, DP_CUT)] = 60; dpar[TP_DRUM(0, DP_RES)] = 80;
                dpar[TP_DRUM(0, DP_FENV)] = 90; dpar[TP_DRUM(0, DP_DRIVE)] = 70;
                dpar[TP_DRUM(0, DP_ATTACK)] = 10; dpar[TP_DRUM(0, DP_DECAY)] = 60;
            }
            drumline_init(dl, dpar);
            if (f == NDF)
                drumline_filter(dl, FT_DYNAMIC, 40, 80, 70, 100);
            hash_init();
            drumline_hit(dl, 0, 120, 0, 0, 0);
            drum_blocks(40);
            drumline_hit(dl, 0, 70, 5, 1, 0);
            drumline_hit(dl, 1, 100, 0, 0, 0);
            drumline_hit(dl, 2, 90, 0, 0, 1);
            drum_blocks(80);
            snprintf(name, sizeof name, "drum %s %d", DM_NAME[m], f);
            out(name);
        }
}

/* ---- the effects and the master ---- */
static int16_t dly_l[FX_DLY_LEN], dly_r[FX_DLY_LEN], rv[FX_RV_LEN];
static uint8_t fxb[FXSLOT_BYTES] __attribute__((aligned(8)));
static uint8_t msb[MASTER_BYTES] __attribute__((aligned(8)));
static fxslot_t *fx = (fxslot_t *)fxb;
static master_t *ms = (master_t *)msb;
static float in[N], P1[N], P2[N], M1[N], M2[N];

static void fx_cases(void)
{
    int t, b, i, v;
    char name[64];
    static const uint8_t mid[4] = {64, 64, 64, 64}, hi[4] = {120, 110, 20, 100};
    for (t = 0; t < NFXPICK; t++)
        for (v = 0; v < 3; v++) {
            fx_init(fx, dly_l, dly_r, rv);
            fx_tempo(fx, 120.0f);
            fx_set(fx, t, v == 0 ? (t >= FXT_REVERB ? RV_SPU_DEFAULT : FXT_DEFAULT[t]) : v == 1 ? mid : hi);
            if (v == 2)
                fx_filter(fx, 30, 90);
            rng = 777;
            hash_init();
            for (b = 0; b < 320; b++) {                 /* (an SPU mode clears its area first) */
                for (i = 0; i < N; i++)
                    in[i] = b >= 150 && b < 170 ? 0.4f * noise() : 0.0f;
                memset(L, 0, sizeof L); memset(R, 0, sizeof R);
                fx_process(fx, in, L, R, b % 3 ? N : 100);
                hash_buf(L, N); hash_buf(R, N);
            }
            snprintf(name, sizeof name, "fx %s %d", fx_pick_name(t), v);
            out(name);
        }
}
static void master_case(const char *name, const int *set)
{
    int b, i;
    master_init(ms);
    for (i = 0; set[i] >= 0; i += 2)
        master_set(ms, set[i], set[i + 1]);
    rng = 99;
    hash_init();
    for (b = 0; b < 200; b++) {
        float g = b < 100 ? 0.5f : b < 150 ? 1.5f : 0.05f;      /* quiet, loud (the limiter), soft */
        for (i = 0; i < N; i++) {
            P1[i] = g * noise(); P2[i] = g * noise();
            M1[i] = g * fm_sin_turns((float)(b * N + i) * (110.0f / 44100.0f)); M2[i] = g * noise();
        }
        memset(L, 0, sizeof L); memset(R, 0, sizeof R);    /* (a short block leaves the rest: not the last case's) */
        master_process(ms, P1, P2, M1, M2, L, R, b % 5 ? N : 77);
        hash_buf(L, N); hash_buf(R, N);
    }
    out(name);
}
static void master_cases(void)
{
    static const int off[] = {-1};
    static const int full[] = {MS_EQ_LO, 100, MS_EQ_MID, 20, MS_EQ_HI, 90, MS_COMP, 127,
                               MS_PERC, 80, MS_MEL, 120, MS_LEVEL, 110, -1};
    static const int eq[] = {MS_EQ_LO, 80, MS_COMP, 80, -1};
    static const int lev[] = {MS_PERC, 30, MS_MEL, 127, MS_LEVEL, 127, MS_COMP, 40, -1};
    master_case("master off", off);
    master_case("master eq comp levels 127", full);
    master_case("master eq comp", eq);
    master_case("master levels comp", lev);
}

int main(void)
{
    synth_cases();
    drum_cases();
    fx_cases();
    master_cases();
    return 0;
}
