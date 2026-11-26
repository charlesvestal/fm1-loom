/* SPDX-License-Identifier: MPL-2.0 */
/* Fingerprints of the DSP's output, for refactors that must not change a sample: every synth engine through
 * every filter type and character type, every insert type, every factory preset, every drum model through every
 * key filter and the dynamic filter, a kit's character, every send effect and reverb mode, the master at its EQ /
 * compressor's settings and its color. One line per case: its name and a 64-bit FNV-1a of the output's float bits. Run it before and
 * after a change and compare (tests/host/dsp_hash.sh does both: `dsp_hash.sh save`, then `dsp_hash.sh`).
 * Nothing here plays audio. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "synth.h"
#include "drumline.h"
#include "fx.h"
#include "fastmath.h"
#include "breaks.h"                           /* the break pack as the user bank */
#include "bass303.h"                          /* the acid engine, on its own */
#include "d909.h"                             /* d909_res_get: the 909 cymbals' samples */

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
/* the character's types (before the one effects list: the case names keep their numbers) as the catalog's */
static const uint8_t CT_EF[8] = {EF_OFF, EF_DRIVE, EF_FOLD, EF_CRUSH, EF_RING, EF_TILT, EF_TAPE, EF_SAMPLER};
/* the inserts' cases (format 27's, by name: their knobs at their defaults, then moved) as slot 2 now (project.c's
 * conversion): effect, knobs 1..3, mix, extra 1 */
static const struct { const char *name; uint8_t ef, k[2][5], x[2]; } INS_CASE[9] = {
    {"tremolo", EF_TREMOLO, {{4, 80, 0, 127}, {100, 70, 40, 127}}, {0, 0}},
    {"phaser", EF_PHASER, {{50, 90, 62, 127}, {50, 70, 127, 127}}, {60, 40}},
    {"univibe", EF_UNIVIBE, {{70, 90, 0, 127}, {100, 70, 40, 127}}, {0, 0}},
    {"rat", EF_RAT, {{70, 50, 80, 127}, {100, 70, 40, 127}}, {0, 0}},
    {"autowah", EF_AUTOWAH, {{80, 70, 50, 127}, {100, 70, 40, 127}}, {0, 0}},
    {"chorus", EF_CHORUS, {{46, 70, 20, 64}, {127, 70, 20, 40}}, {127, 127}},
    {"echo", EF_DELAY, {{83, 58, 64, 45}, {44, 62, 64, 40}}, {64, 64}},
    {"juno", EF_JUNO, {{0, 70, 0, 64}, {2, 40, 0, 70}}, {0, 0}},
    {"rumble", EF_RUMBLE, {{55, 80, 0, 70}, {100, 70, 0, 40}}, {0, 0}}};
static void synth_cases(void)
{
    int e, ft, ct, k, it;
    char name[64];
    for (e = 0; e < NENGINE; e++) {
        if (!eg_synth(e))
            continue;
        for (ft = 0; ft < NFT; ft++)
            for (ct = 0; ct < 8; ct++) {
                int t;
                for (t = 0; t < NTRK; t++)
                    sound_default(par[t], e);
                par[0][TP_FTYPE] = (uint8_t)ft;
                par[0][TP_RES] = 90;
                par[0][TP_FENV] = 100;
                par[0][TP_E1] = 90; par[0][TP_E2] = 30; par[0][TP_E3] = 100; par[0][TP_E4] = 110;
                par[0][TP_CTYPE] = CT_EF[ct];
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
    for (it = 0; it < 9; it++)                    /* every insert (slot 2) on knit (its knobs at their defaults, then
                                                   * moved) */
        for (k = 0; k < 2; k++) {
            int t;
            for (t = 0; t < NTRK; t++)
                sound_default(par[t], EG_KNIT);
            par[0][TP_ITYPE] = INS_CASE[it].ef;
            for (t = 0; t < 3; t++)
                par[0][TP_I1 + t] = INS_CASE[it].k[k][t];
            par[0][TP_S2MIX] = INS_CASE[it].k[k][3];
            par[0][TP_S2X1] = INS_CASE[it].x[k];
            par[0][TP_SEND1] = 60;
            snprintf(name, sizeof name, "insert %s %d", INS_CASE[it].name, k);
            synth_case(name);
        }
    for (k = 0; k < NPRESETS; k++) {   /* (by name, beside the next engine's first preset: a preset added keeps the
                                        * others' fingerprints) */
        int t, e = PRESETS[k].engine, j = -1;
        for (t = 1; t <= NENGINE && j < 0; t++)
            j = preset_first((e + t) % NENGINE);
        for (t = 0; t < NTRK; t++)
            sound_default(par[t], EG_KNIT);
        preset_sound(par[0], &PRESETS[k]);
        preset_sound(par[1], &PRESETS[j]);
        snprintf(name, sizeof name, "preset %s", PRESETS[k].name);
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
/* the acid engine (dsp/bass303.c) on its own: each knob at its ends with the rest at their defaults, both waves,
 * the drives, the overload guard's lite mode, the pitch multiplier; a phrase of notes with accents and slides, a
 * rest long enough to go idle, a note after it */
static bass303_t acid;
static float abuf[BASS303_MAX_BLOCK];
static void acid_phrase(int lite)
{
    static const int8_t NOTE[] = {36, 48, 39, 41, 36, 43, 46, 48}, ACC[] = {1, 0, 0, 1, 0, 0, 1, 0},
                        SLD[] = {0, 1, 0, 0, 1, 1, 0, 0};
    int i, b;
    bass303_set_lite(&acid, lite);
    for (i = 0; i < 8; i++) {
        bass303_note_on(&acid, NOTE[i], ACC[i], SLD[i]);
        for (b = 0; b < 20; b++) {
            if (b == 14 && !SLD[(i + 1) & 7])
                bass303_note_off(&acid);
            bass303_render(&acid, abuf, b & 1 ? 37 : 91);   /* (blocks not a multiple of the sub-blocks) */
            hash_buf(abuf, b & 1 ? 37 : 91);
        }
    }
    bass303_note_off(&acid);
    for (b = 0; b < 200; b++) {                           /* past the idle hold-off (500 ms) */
        bass303_render(&acid, abuf, 128);
        hash_buf(abuf, 128);
    }
    bass303_note_on(&acid, 40, 1, 0);
    for (b = 0; b < 40; b++) {
        bass303_render(&acid, abuf, 128);
        hash_buf(abuf, 128);
    }
}
static void acid_case(const char *name, int k, int v, int wave, int drv, float pmul)
{
    char nm[64];
    int lite;
    for (lite = 0; lite < 2; lite++) {
        bass303_init(&acid);
        bass303_set(&acid, BASS303_WAVE, wave);
        bass303_set(&acid, BASS303_DRVTYPE, drv);
        bass303_set(&acid, BASS303_DRIVE, drv ? 90 : 0);
        if (k >= 0)
            bass303_set(&acid, k, v);
        bass303_pitch(&acid, pmul);
        hash_init();
        acid_phrase(lite);
        snprintf(nm, sizeof nm, "acid %s%s", name, lite ? " lite" : "");
        out(nm);
    }
}
static void acid_cases(void)
{
    static const char *const DRV[] = {"off", "soft", "rat"};
    char name[64];
    int w, d, k;
    for (w = 0; w < 2; w++)
        for (d = 0; d < 3; d++) {
            snprintf(name, sizeof name, "%s %s", w ? "sqr" : "saw", DRV[d]);
            acid_case(name, -1, 0, w, d, 1.0f);
            for (k = 0; k < BASS303_NPARAMS; k++) {
                if (k == BASS303_WAVE || k == BASS303_DRVTYPE)
                    continue;
                snprintf(name, sizeof name, "%s %s k%d 0", w ? "sqr" : "saw", DRV[d], k);
                acid_case(name, k, 0, w, d, 1.0f);
                snprintf(name, sizeof name, "%s %s k%d max", w ? "sqr" : "saw", DRV[d], k);
                acid_case(name, k, bass303_max(k), w, d, 1.0f);
            }
            snprintf(name, sizeof name, "%s %s pitch 0.5", w ? "sqr" : "saw", DRV[d]);
            acid_case(name, -1, 0, w, d, 0.5f);
            snprintf(name, sizeof name, "%s %s pitch 1.5", w ? "sqr" : "saw", DRV[d]);
            acid_case(name, -1, 0, w, d, 1.4983f);
        }
}

/* the 909 cymbals on their samples (the resource region as the FM-1 holds it: build/gen/res909.bin): each through
 * every shape, with and without drive, at the pitch's ends and middle, to past its envelope's end and hit again */
static uint8_t res909[96 * 1024];
static uint32_t res909_len;
static const uint8_t *res_get(uint32_t *len)
{
    *len = res909_len;
    return res909_len ? res909 : 0;
}
static void smp_cases(void)
{
    static const uint8_t PITCH[] = {0, 64, 127}, DRIVE[] = {0, 90};
    FILE *f = fopen("build/gen/res909.bin", "rb");
    int m, sh, d, pt, s, k;
    char name[64];
    res909_len = f ? (uint32_t)fread(res909, 1, sizeof res909, f) : 0u;
    if (f)
        fclose(f);
    d909_res_get = res_get;
    for (m = DM_909CH; m <= DM_909CRASH; m++)
        for (sh = 0; sh < 7; sh++)
            for (d = 0; d < 2; d++)
                for (pt = 0; pt < 3; pt++) {
                    memset(dpar, 0, sizeof dpar);
                    for (s = 0; s < DL_NSOUND; s++)
                        for (k = 0; k < DP_N; k++)
                            dpar[TP_DRUM(s, k)] = DL_DEFAULT_KIT[s][k];
                    dpar[TP_R] = 64;                  /* (the kit's length 1x) */
                    dpar[TP_DRUM(0, DP_MODEL)] = (uint8_t)m;
                    dpar[TP_DRUM(0, DP_K1)] = PITCH[pt];
                    dpar[TP_DRUM(0, DP_K2)] = DM_DEFAULT[m][1];
                    dpar[TP_DRUM(0, DP_K3)] = DRIVE[d];
                    dpar[TP_DRUM(0, DP_K4)] = (uint8_t)sh;   /* (a list: its index) */
                    drumline_init(dl, dpar);
                    hash_init();
                    drumline_hit(dl, 0, 120, 0, 0, 0);
                    drum_blocks(m >= DM_909RIDE ? 2200 : 700);
                    drumline_hit(dl, 0, 90, 0, 0, 0);
                    drum_blocks(300);
                    snprintf(name, sizeof name, "909 %s shape %d drive %d pitch %d", DM_NAME[m], sh, DRIVE[d], PITCH[pt]);
                    out(name);
                }
    d909_res_get = 0;
}

static uint8_t fxb[FXSLOT_BYTES] __attribute__((aligned(8)));
static uint8_t msb[MASTER_BYTES] __attribute__((aligned(8)));
static fxslot_t *fx = (fxslot_t *)fxb;
static master_t *ms = (master_t *)msb;
static float in[N], P1[N], P2[N], M1[N], M2[N];

/* the sends as they were set before the one effects list (an old pick and four knobs: fx.h fx_pick_slot), their
 * memory the engine's (the pool, as much as the effect needs: the reverb's modes their own lines) */
static const uint8_t FXT_DEF[NFXT][4] = {{50, 86, 20, 127}, {83, 64, 56, 64}, {80, 80, 64, 40}, {50, 90, 50, 80},
                                         {100, 40, 44, 72}}, RV_SPU_DEF[4] = {127, 64, 64, 0};
static void fx_old(int pick, const uint8_t *e)
{
    uint8_t o[9];
    fx_pick_slot(pick, e, 127, 0, 0, o);
    fx_set(fx, o[0], o + 1, o + 5);
}
static void fx_cases(void)
{
    static const char *const FXN[NFXT] = {"chorus", "delay", "lofi", "phaser", "reverb"};
    int t, b, i, v;
    char name[64];
    static const uint8_t mid[4] = {64, 64, 64, 64}, hi[4] = {120, 110, 20, 100};
    for (t = 0; t < NFXPICK; t++)
        for (v = 0; v < 3; v++) {
            const uint8_t *kn = v == 0 ? (t >= FXT_REVERB ? RV_SPU_DEF : FXT_DEF[t]) : v == 1 ? mid : hi;
            uint8_t o[9];
            fx_pick_slot(t, kn, 127, 0, 0, o);
            fx_init(fx);                         /* (as the slot started: the chorus at its defaults, at rest) */
            fx_old(FXT_CHORUS, FXT_DEF[FXT_CHORUS]);
            fx_memory(fx, efx_pool[0], efx_need(o[0], o[0] == EF_REVERB ? o[5] : 0, 1));
            fx_process(fx, in, L, R, 0);
            fx_tempo(fx, 120.0f);
            fx_old(t, kn);
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
            snprintf(name, sizeof name, "fx %s %d", t >= FXT_REVERB ? RV_NAME[fx_pick_mode(t)] : FXN[t], v);
            out(name);
        }
}
/* a kit's character (slot 1, synth_kit_slots) on a stereo pair: every type, a noisy chord in, then silence (its tails) */
static void kitchar_cases(void)
{
    int ct, b, i, v, t;
    char name[64];
    for (ct = 1; ct < 8; ct++)
        for (v = 0; v < 2; v++) {
            for (t = 0; t <= NTRK; t++)
                memset(par[t], 0, NTP);
            par[0][TP_ENGINE] = EG_DRUMLINE;
            par[0][TP_CTYPE] = CT_EF[ct];
            par[0][TP_CAMT] = v ? 30 : 100;
            par[0][TP_CTONE] = v ? 20 : 90;
            par[0][TP_CMIX] = v ? 127 : 90;
            synth_init(S, par);
            rng = 4242;
            hash_init();
            for (b = 0; b < 120; b++) {
                for (i = 0; i < N; i++) {
                    float u = (float)(b * N + i) * (1.0f / 44100.0f), g = b < 80 ? 0.6f : 0.0f;
                    L[i] = g * (fm_sin_turns(u * 98.0f) + 0.3f * noise());
                    R[i] = g * (fm_sin_turns(u * 147.0f) + 0.3f * noise());
                }
                if (b == 60)
                    par[0][TP_CAMT] = (uint8_t)(par[0][TP_CAMT] + 20);   /* (a turn: the ring's glide) */
                synth_prepare(S, b % 3 ? N : 100);
                synth_kit_slots(S, 0, L, R, 0, b % 3 ? N : 100);
                hash_buf(L, N); hash_buf(R, N);
            }
            snprintf(name, sizeof name, "kit char %d %d", ct, v);
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
    static const int clip[] = {MS_CLIP, 90, MS_COMP, 60, -1};
    master_case("master off", off);
    master_case("master eq comp levels 127", full);
    master_case("master eq comp", eq);
    master_case("master levels comp", lev);
    master_case("master clip 90", clip);
    {
        static const int tape[] = {MS_CTYPE, MC_TAPE, MS_CAMT, 90, MS_CTONE, 60, MS_CMIX, 127, -1};
        static const int tape2[] = {MS_CTYPE, MC_TAPE, MS_CAMT, 20, MS_CTONE, 110, MS_CMIX, 70, MS_COMP, 50, -1};
        static const int smp[] = {MS_CTYPE, MC_SAMPLER, MS_CAMT, 90, MS_CTONE, 40, MS_CMIX, 127, -1};
        static const int smp2[] = {MS_CTYPE, MC_SAMPLER, MS_CAMT, 30, MS_CTONE, 100, MS_CMIX, 64, -1};
        master_case("master color tape", tape);
        master_case("master color tape 2", tape2);
        master_case("master color sampler", smp);
        master_case("master color sampler 2", smp2);
    }
}

int main(void)
{
    breaks_attach();                             /* the breaks installed, as a player has them */
    synth_cases();
    drum_cases();
    smp_cases();
    acid_cases();
    fx_cases();
    kitchar_cases();
    master_cases();
    return 0;
}
