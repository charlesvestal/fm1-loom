/* SPDX-License-Identifier: GPL-3.0-only */
/* The effects (dsp/fx.c) on their own: every send effect (and every reverb mode) takes an impulse and a
 * noise burst without NaN and stays bounded, at its defaults and at mid knobs; the delay's first echo lands
 * on the beat at 120 BPM (time 1/4 = 0.5 s, dotted 1/8 = 0.375 s) and ping-pongs; the delay and the reverb
 * return only the wet (plain send / return); the delay's tone brightens its repeats, the plate's damp darkens
 * its tail; the reverbs decay (the SPU modes too, each, and grow with size); the lofi's width spreads the
 * sides; the master is bit-transparent with the EQ flat (apart from the
 * limiter's 64-sample lookahead), its limiter never passes 1.0, its compressor pulls loud input down, its
 * EQ moves the bands; its PUMP ducks by its depth on its source's hits and swells back with the release (at 0
 * it changes nothing, sample for sample; accented hits deeper); and the cost of each in instructions a sample (macOS: proc_pid_rusage). Each effect's
 * burst at its defaults is written to build/host/fx_<name>.wav for inspection (nothing here plays audio).
 *   tests/host/fx_test.sh */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "fx.h"
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

#define SR 44100
#define BLK 64
static int bad;
static void check(const char *what, int ok)
{
    printf("%-64s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

static int16_t dly_l[FX_DLY_LEN], dly_r[FX_DLY_LEN], rv[FX_RV_LEN];
static uint8_t fxb[FXSLOT_BYTES] __attribute__((aligned(8)));
static uint8_t msb[MASTER_BYTES] __attribute__((aligned(8)));
static fxslot_t *fx = (fxslot_t *)fxb;
static master_t *ms = (master_t *)msb;
#define MS_INIT() master_init(ms)

static uint32_t rng = 12345;
static float noise(void)
{
    rng = rng * 1664525u + 1013904223u;
    return (float)(int32_t)rng * (1.0f / 2147483648.0f);
}

static void wav_open(FILE **f, const char *name)
{
    static const uint8_t hdr[44] = {'R','I','F','F',0,0,0,0,'W','A','V','E','f','m','t',' ',16,0,0,0,1,0,2,0,
        0x44,0xAC,0,0,0x10,0xB1,2,0,4,0,16,0,'d','a','t','a',0,0,0,0};
    *f = fopen(name, "wb");
    if (*f)
        fwrite(hdr, 1, 44, *f);
}
static void wav_put(FILE *f, const float *l, const float *r, int n)
{
    int i;
    for (i = 0; f && i < n; i++) {
        int16_t s[2];
        s[0] = (int16_t)fm_clampf(l[i] * 32767.0f, -32767.0f, 32767.0f);
        s[1] = (int16_t)fm_clampf(r[i] * 32767.0f, -32767.0f, 32767.0f);
        fwrite(s, 2, 2, f);
    }
}
static void wav_close(FILE *f)
{
    uint32_t len;
    if (!f)
        return;
    len = (uint32_t)ftell(f);
    fseek(f, 4, SEEK_SET);
    { uint32_t v = len - 8; fwrite(&v, 4, 1, f); }
    fseek(f, 40, SEEK_SET);
    { uint32_t v = len - 44; fwrite(&v, 4, 1, f); }
    fclose(f);
}

/* run an effect on an impulse then 0.2 s of noise at 0.5, 3 s in all: peak, NaN, the RMS of the
 * second after the noise and of the last second, where the left's and right's peaks fall */
typedef struct { float peak, rms, rms_after, rms_end; int nan; int peak_l_at, peak_r_at; } run_t;
static void run_fx(int type, const uint8_t knob[4], run_t *r, const char *wav)
{
    static float in[BLK], ol[BLK], orr[BLK];
    int b, i, nb = 3 * SR / BLK;
    float pl = 0.0f, pr = 0.0f;
    FILE *w = 0;
    memset(r, 0, sizeof *r);
    fx_init(fx, dly_l, dly_r, rv);
    fx_tempo(fx, 120.0f);
    fx_set(fx, type, knob);
    if (wav)
        wav_open(&w, wav);
    for (b = 0; b < nb; b++) {
        int t0 = b * BLK;
        for (i = 0; i < BLK; i++) {
            int t = t0 + i;
            in[i] = t == 0 ? 1.0f : (t >= 2205 && t < 2205 + SR / 5) ? 0.5f * noise() : 0.0f;
        }
        memset(ol, 0, sizeof ol);
        memset(orr, 0, sizeof orr);
        fx_process(fx, in, ol, orr, BLK);
        wav_put(w, ol, orr, BLK);
        for (i = 0; i < BLK; i++) {
            int t = t0 + i;
            float al = fm_fabsf(ol[i]), ar = fm_fabsf(orr[i]);
            if (ol[i] != ol[i] || orr[i] != orr[i])
                r->nan = 1;
            r->rms += 0.5f * (ol[i] * ol[i] + orr[i] * orr[i]);
            if (al > r->peak) r->peak = al;
            if (ar > r->peak) r->peak = ar;
            if (t >= 1000 && t < 2205) {              /* before the noise: only the impulse's echoes */
                if (al > pl) { pl = al; r->peak_l_at = t; }
                if (ar > pr) { pr = ar; r->peak_r_at = t; }
            }
            if (t >= 2205 + SR / 5 && t < 2205 + SR / 5 + SR)
                r->rms_after += ol[i] * ol[i];
            if (t >= 2 * SR)
                r->rms_end += ol[i] * ol[i];
        }
    }
    wav_close(w);
    r->rms = fm_sqrtf(r->rms / (float)(nb * BLK));
    r->rms_after = fm_sqrtf(r->rms_after / (float)SR);
    r->rms_end = fm_sqrtf(r->rms_end / (float)SR);
}

static float db(float x) { return 20.0f * 0.30103f * fm_log2f(x + 1e-12f); }

/* an impulse into the slot, then n samples of L and R */
static void impulse(int type, const uint8_t knob[4], float *l, float *r, int n)
{
    static float in[BLK], ol[BLK], orr[BLK];
    int b, i;
    fx_init(fx, dly_l, dly_r, rv);
    fx_tempo(fx, 120.0f);
    fx_set(fx, type, knob);
    for (b = 0; type > FXT_REVERB && b < 40; b++) {       /* an SPU mode clears its area first (silent) */
        memset(in, 0, sizeof in);
        fx_process(fx, in, ol, orr, BLK);
    }
    for (b = 0; b < n / BLK; b++) {
        memset(in, 0, sizeof in);
        if (b == 0)
            in[0] = 1.0f;
        memset(ol, 0, sizeof ol);
        memset(orr, 0, sizeof orr);
        fx_process(fx, in, ol, orr, BLK);
        for (i = 0; i < BLK; i++) {
            l[b * BLK + i] = ol[i];
            r[b * BLK + i] = orr[i];
        }
    }
}
/* brightness: the first difference's energy over the energy, in a window */
static float bright(const float *y, int from, int to)
{
    float e = 0.0f, d = 0.0f;
    int i;
    for (i = from; i < to; i++) {
        e += y[i] * y[i];
        d += (y[i] - y[i - 1]) * (y[i] - y[i - 1]);
    }
    return d / (e + 1e-20f);
}
static float imp_l[3 * SR], imp_r[3 * SR];

/* the SPU mode's impulse response: its energy's centre in time (s) over 4 s */
static float centre_time(int pick, const uint8_t kn[4])
{
    static float in[BLK], ol[BLK], orr[BLK];
    float e = 0.0f, et = 0.0f;
    int b, i;
    fx_init(fx, dly_l, dly_r, rv);
    fx_set(fx, pick, kn);
    for (b = 0; b < 40; b++) {                           /* its area cleared */
        memset(in, 0, sizeof in);
        fx_process(fx, in, ol, orr, BLK);
    }
    for (b = 0; b < 4 * SR / BLK; b++) {
        memset(in, 0, sizeof in);
        if (b == 0)
            in[0] = 1.0f;
        memset(ol, 0, sizeof ol);
        memset(orr, 0, sizeof orr);
        fx_process(fx, in, ol, orr, BLK);
        for (i = 0; i < BLK; i++) {
            float v = ol[i] * ol[i] + orr[i] * orr[i];
            e += v;
            et += v * (float)(b * BLK + i) / SR;
        }
    }
    return et / (e + 1e-20f);
}

/* the master's PUMP on a steady 0.1 (DC on the melodic group: below the limiter, the EQ out, so the
 * output is 0.1 x the gain, 64 samples late): g[b] = the block's lowest gain in dB, keyed (src, vel) at block k0
 * (and again at k1 if >= 0) */
static void pump_trace(float *g, int nb, int k0, int k1, int src, int vel)
{
    static float z[BLK], dc[BLK], ol[BLK], orr[BLK];
    int b, i;
    for (i = 0; i < BLK; i++)
        z[i] = 0.0f;
    for (b = 0; b < nb; b++) {
        float lo = 1e9f;
        if (b == k0 || b == k1)
            master_key(ms, src, vel, src == PB_KICK);
        for (i = 0; i < BLK; i++)
            dc[i] = 0.1f;
        master_process(ms, z, z, dc, dc, ol, orr, BLK);
        for (i = 0; i < BLK; i++)
            lo = fm_minf(lo, ol[i]);
        g[b] = 20.0f * 0.30103f * fm_log2f(fm_maxf(lo, 1e-9f) * 10.0f);
    }
}
static float trace_min(const float *g, int a, int b)
{
    float m = 1e9f;
    for (; a < b; a++)
        m = fm_minf(m, g[a]);
    return m;
}

int main(void)
{
    static float in[BLK], ol[BLK], orr[BLK];
    int i, b, t;
    char name[96];
    run_t r;

    printf("-- every effect at its defaults on an impulse and a noise burst (peak, RMS of the 3 s, the second\n"
           "   after the burst, the last second; dBFS), written to build/host/fx_<name>.wav\n");
    for (t = 0; t < NFXPICK; t++) {
        const uint8_t *def = t > FXT_REVERB ? RV_SPU_DEFAULT : FXT_DEFAULT[fx_pick_type(t)];
        snprintf(name, sizeof name, "build/host/fx_%s.wav", fx_pick_name(t));
        for (i = 0; name[i]; i++)
            if (name[i] == ' ')
                name[i] = '_';
        run_fx(t, def, &r, name);
        snprintf(name, sizeof name, "  %-10s %6.1f  %6.1f  %6.1f  %6.1f", fx_pick_name(t), (double)db(r.peak),
                 (double)db(r.rms), (double)db(r.rms_after), (double)db(r.rms_end));
        check(name, !r.nan && r.peak > 0.001f && r.peak < 4.0f);
    }
    printf("-- and at mid knobs (64)\n");
    for (t = 0; t < NFXPICK; t++) {
        static const uint8_t mid[4] = {64, 64, 64, 64};
        run_fx(t, mid, &r, 0);
        snprintf(name, sizeof name, "  %-10s %6.1f  %6.1f  %6.1f  %6.1f", fx_pick_name(t), (double)db(r.peak),
                 (double)db(r.rms), (double)db(r.rms_after), (double)db(r.rms_end));
        check(name, !r.nan && r.peak > 0.001f && r.peak < 4.0f);
    }
    {   /* the defaults and the knobs' names */
        static const uint8_t want[NFXT][4] = {{50, 86, 20, 127}, {72, 64, 56, 64}, {80, 80, 64, 40},
                                              {50, 90, 50, 80},  {100, 40, 44, 72}};
        static const char *const names[NFXT][4] = {
            {"rate", "depth", "feedback", "width"}, {"time", "fine", "repeats", "tone"},
            {"rate", "bits", "smooth", "width"},    {"freq", "depth", "rate", "feedback"},
            {"size", "motion", "tone", "damp"}};
        static const char *const picks[NFXPICK] = {"chorus", "delay", "lofi", "phaser", "plate", "room", "studio S",
                                                   "studio M", "studio L", "hall", "half echo", "space echo",
                                                   "chaos echo", "one echo", "lush"};
        int ok = 1, k;
        check("five send effects (no distortion), the reverb with eleven modes", NFXT == 5 && NRV == 11 && NFXPICK == 15 &&
                                                                              !strcmp(FXT_NAME[FXT_REVERB], "reverb"));
        for (t = 0; t < NFXT; t++)
            for (k = 0; k < 4; k++)
                ok &= FXT_DEFAULT[t][k] == want[t][k];
        check("FXT_DEFAULT: chorus 50 86 20 127, delay 72 64 56 64, ... plate 100 40 44 72", ok);
        ok = 1;
        for (t = 0; t < NFXPICK; t++) {
            ok &= !strcmp(fx_pick_name(t), picks[t]);
            for (k = 0; k < 4; k++)
                ok &= !strcmp(fx_knob_name(t, k), t > FXT_REVERB ? (const char *[]){"size", "decay", "tone", "damp"}[k]
                                                                  : names[fx_pick_type(t)][k]);
        }
        check("picks: chorus .. phaser, plate .. one echo; the SPU modes' knobs size decay tone damp", ok);
        ok = 1;
        for (t = 0; t < NFXPICK; t++)                    /* the strip fits a name in 46 pixels: 8 letters */
            for (k = 0; k < 4; k++)
                ok &= strlen(fx_knob_name(t, k)) <= 8;
        check("knob names at most 8 letters", ok);
    }
    check("fx_has: every effect with its lines", fx_has(fx, FXT_REVERB) && fx_has(fx, FXT_REVERB + RV_HALL) &&
                                                 fx_has(fx, FXT_DELAY) && fx_has(fx, FXT_LOFI) && !fx_has(fx, NFXPICK));
    fx_init(fx, dly_l, dly_r, 0);
    check("fx_has: no reverb (no mode) without its line", !fx_has(fx, FXT_REVERB) && !fx_has(fx, FXT_REVERB + RV_ROOM) &&
                                                          fx_has(fx, FXT_CHORUS));
    {   /* the knobs at their ends, every effect: still finite and bounded */
        int ok = 1, k;
        for (t = 0; t < NFXPICK && ok; t++)
            for (k = 0; k < 2 && ok; k++) {
                uint8_t kn[4] = {(uint8_t)(k ? 127 : 0), (uint8_t)(k ? 127 : 0), (uint8_t)(k ? 0 : 127), (uint8_t)(k ? 127 : 0)};
                run_fx(t, kn, &r, 0);
                ok = !r.nan && r.peak < 8.0f;
                if (!ok)
                    printf("  %s knobs %d: peak %.2f nan %d\n", fx_pick_name(t), k, (double)r.peak, r.nan);
            }
        check("knobs at their ends: finite, under +18 dBFS", ok);
    }

    printf("-- delay at 120 BPM: time 1/4 = 0.5 s, dotted 1/8 = 0.375 s\n");
    {
        /* time 100: (100 * 8) >> 7 = 6, 1/4; time 85: 5, 1/8D; fine 64 (none); repeats 0.47; tone open.
         * The impulse's echoes are looked for in a run longer than run_fx's window */
        static const uint8_t kn[2][4] = {{100, 64, 64, 127}, {85, 64, 64, 127}};
        static const int at[2] = {22050, 16538};
        int s;
        for (s = 0; s < 2; s++) {
            float pl = 0.0f, pr = 0.0f;
            int at_l = 0, at_r = 0;
            fx_init(fx, dly_l, dly_r, rv);
            fx_tempo(fx, 120.0f);
            fx_set(fx, FXT_DELAY, kn[s]);
            for (b = 0; b < 50000 / BLK; b++) {
                memset(in, 0, sizeof in);
                if (b == 0)
                    in[0] = 1.0f;
                memset(ol, 0, sizeof ol);
                memset(orr, 0, sizeof orr);
                fx_process(fx, in, ol, orr, BLK);
                for (i = 0; i < BLK; i++) {
                    int tt = b * BLK + i;
                    if (tt > 100 && fm_fabsf(ol[i]) > pl) { pl = fm_fabsf(ol[i]); at_l = tt; }
                    if (tt > 100 && fm_fabsf(orr[i]) > pr) { pr = fm_fabsf(orr[i]); at_r = tt; }
                }
            }
            snprintf(name, sizeof name, "  %s: first echo left at %d (%d), right at %d (%d)", s ? "1/8D" : "1/4 ",
                     at_l, at[s], at_r, 2 * at[s]);
            check(name, at_l >= at[s] - 2 && at_l <= at[s] + 3 && at_r >= 2 * at[s] - 2 && at_r <= 2 * at[s] + 4 &&
                        pl > 0.3f && pr > 0.1f);
        }
        {   /* the delay returns only its echoes: the send itself is not passed through */
            static const uint8_t kd[4] = {100, 64, 127, 127};
            impulse(FXT_DELAY, kd, imp_l, imp_r, 2048);
            check("delay: no dry in the return", imp_l[0] == 0.0f && imp_r[0] == 0.0f);
        }
        {   /* tone: the right's first echo (once through the repeats' low-pass) is brighter at 110 than at 20 */
            static const uint8_t kt[2][4] = {{72, 64, 100, 20}, {72, 64, 100, 110}};
            float br[2];
            int s2;
            for (s2 = 0; s2 < 2; s2++) {                     /* 1/8 at 120 BPM: 11025 samples; right at 22050 */
                impulse(FXT_DELAY, kt[s2], imp_l, imp_r, 24064);
                br[s2] = bright(imp_r, 22050 - 16, 22050 + 2000);
            }
            snprintf(name, sizeof name, "  tone 20 -> 110: the repeats brighter (%.4f -> %.4f)", (double)br[0], (double)br[1]);
            check(name, br[1] > br[0] * 4.0f);
        }
    }

    printf("-- reverb decays, and is silent when left alone; no dry; its damp\n");
    {
        static const uint8_t kn[4] = {64, 40, 40, 72};
        int d;
        float br[2];
        for (d = 0; d < 2; d++) {                                /* damp 0 and 127: the send never passes */
            uint8_t kd[4] = {64, 40, 64, (uint8_t)(d ? 127 : 0)};
            impulse(FXT_REVERB, kd, imp_l, imp_r, SR);           /* the combs answer ~550 samples later */
            check(d ? "  reverb damp 127: no dry in the return" : "  reverb damp 0: no dry in the return",
                  imp_l[0] == 0.0f && imp_r[0] == 0.0f);
            br[d] = bright(imp_l, 4410, SR);                     /* the tail from 0.1 s */
        }
        snprintf(name, sizeof name, "  damp 0 -> 127: the tail darker (%.4f -> %.4f)", (double)br[0], (double)br[1]);
        check(name, br[1] < br[0] * 0.5f);
        run_fx(FXT_REVERB, kn, &r, 0);
        snprintf(name, sizeof name, "  after the burst %.1f dB, two seconds on %.1f dB", (double)db(r.rms_after),
                 (double)db(r.rms_end));
        check(name, r.rms_after > 0.001f && r.rms_end < r.rms_after * 0.1f);
        for (b = 0; b < 20 * SR / BLK; b++) {                    /* 20 s of silence: it must empty */
            memset(in, 0, sizeof in);
            memset(ol, 0, sizeof ol);
            memset(orr, 0, sizeof orr);
            fx_process(fx, in, ol, orr, BLK);
        }
        {
            float pk = 0.0f;
            for (i = 0; i < BLK; i++)
                pk = fm_maxf(pk, fm_fabsf(ol[i]));
            check("reverb tail gone after 20 s", pk < 1e-5f);
        }
    }

    printf("-- the slot's filter page and a change of effect\n");
    {
        static const uint8_t kn[4] = {127, 127, 0, 0};             /* the lofi at 44.1 kHz, 16 bits, unsmoothed: a wire */
        float lo = 0.0f, hi = 0.0f, lo2 = 0.0f, hi2 = 0.0f;
        fx_init(fx, dly_l, dly_r, rv);
        fx_set(fx, FXT_LOFI, kn);
        fx_filter(fx, 0, 127);
        for (b = 0; b < 40; b++) {
            for (i = 0; i < BLK; i++)
                in[i] = 0.3f * fm_sin_turns((float)(b * BLK + i) * (60.0f / SR)) +
                        0.3f * fm_sin_turns((float)(b * BLK + i) * (8000.0f / SR));
            memset(ol, 0, sizeof ol);
            memset(orr, 0, sizeof orr);
            fx_process(fx, in, ol, orr, BLK);
        }
        fx_filter(fx, 100, 127);                                 /* a high-pass at ~1.6 kHz */
        for (b = 0; b < 60; b++) {
            for (i = 0; i < BLK; i++)
                in[i] = 0.3f * fm_sin_turns((float)(b * BLK + i) * (60.0f / SR));
            memset(ol, 0, sizeof ol);
            memset(orr, 0, sizeof orr);
            fx_process(fx, in, ol, orr, BLK);
            if (b >= 40)
                for (i = 0; i < BLK; i++)
                    lo += ol[i] * ol[i];
        }
        for (b = 0; b < 60; b++) {
            for (i = 0; i < BLK; i++)
                in[i] = 0.3f * fm_sin_turns((float)(b * BLK + i) * (8000.0f / SR));
            memset(ol, 0, sizeof ol);
            memset(orr, 0, sizeof orr);
            fx_process(fx, in, ol, orr, BLK);
            if (b >= 40)
                for (i = 0; i < BLK; i++)
                    hi += ol[i] * ol[i];
        }
        check("high-pass 100: 60 Hz under 8 kHz by 20 dB", lo < hi * 0.01f && hi > 0.01f);
        fx_filter(fx, 0, 40);                                    /* a low-pass at ~165 Hz */
        for (b = 0; b < 60; b++) {
            for (i = 0; i < BLK; i++)
                in[i] = 0.3f * fm_sin_turns((float)(b * BLK + i) * (60.0f / SR));
            memset(ol, 0, sizeof ol);
            memset(orr, 0, sizeof orr);
            fx_process(fx, in, ol, orr, BLK);
            if (b >= 40)
                for (i = 0; i < BLK; i++)
                    lo2 += ol[i] * ol[i];
        }
        for (b = 0; b < 60; b++) {
            for (i = 0; i < BLK; i++)
                in[i] = 0.3f * fm_sin_turns((float)(b * BLK + i) * (8000.0f / SR));
            memset(ol, 0, sizeof ol);
            memset(orr, 0, sizeof orr);
            fx_process(fx, in, ol, orr, BLK);
            if (b >= 40)
                for (i = 0; i < BLK; i++)
                    hi2 += ol[i] * ol[i];
        }
        check("low-pass 40: 8 kHz under 60 Hz by 20 dB", hi2 < lo2 * 0.01f && lo2 > 0.01f);
        {   /* switch to the reverb mid-stream: no NaN, no leftover */
            static const uint8_t kr[4] = {64, 64, 64, 127};
            int ok = 1;
            fx_set(fx, FXT_REVERB, kr);
            for (b = 0; b < 100; b++) {
                memset(in, 0, sizeof in);
                memset(ol, 0, sizeof ol);
                memset(orr, 0, sizeof orr);
                fx_process(fx, in, ol, orr, BLK);
                for (i = 0; i < BLK; i++)
                    ok &= ol[i] == ol[i] && fm_fabsf(ol[i]) < 0.01f;
            }
            check("a change of effect starts quiet", ok);
        }
    }

    printf("-- lofi: width spreads the sides\n");
    {
        int c;
        for (c = 0; c < 2; c++) {                                /* width 0: L = R exactly; width 127: they differ */
            uint8_t kn[4] = {64, 93, 78, (uint8_t)(c ? 127 : 0)};
            float diff = 0.0f, e2 = 0.0f;
            int same = 1;
            fx_init(fx, dly_l, dly_r, rv);
            fx_set(fx, FXT_LOFI, kn);
            for (b = 0; b < 60; b++) {
                for (i = 0; i < BLK; i++)
                    in[i] = 0.3f * noise();
                memset(ol, 0, sizeof ol);
                memset(orr, 0, sizeof orr);
                fx_process(fx, in, ol, orr, BLK);
                for (i = 0; i < BLK; i++) {
                    same &= ol[i] == orr[i];
                    diff += (ol[i] - orr[i]) * (ol[i] - orr[i]);
                    e2 += ol[i] * ol[i];
                }
            }
            snprintf(name, sizeof name, "  lofi width %d: the sides %s (L-R at %.1f dB of L)", c ? 127 : 0,
                     c ? "differ" : "are the same", (double)(10.0f * 0.30103f * fm_log2f(diff / e2 + 1e-12f)));
            check(name, c ? (!same && diff > e2 * 0.5f) : same);
        }
    }

    printf("-- master\n");
    {
        static float pl[BLK], pr[BLK], mll[BLK], mlr[BLK], ol2[BLK], or2[BLK], hist_l[BLK * 4], hist_r[BLK * 4];
        int ok = 1, n = 0;
        MS_INIT();
        memset(hist_l, 0, sizeof hist_l);
        memset(hist_r, 0, sizeof hist_r);
        for (b = 0; b < 200; b++) {
            for (i = 0; i < BLK; i++) {
                pl[i] = 0.3f * noise(); pr[i] = 0.3f * noise(); mll[i] = 0.3f * noise(); mlr[i] = 0.3f * noise();
                hist_l[(n + i) % (BLK * 4)] = pl[i] + mll[i];
                hist_r[(n + i) % (BLK * 4)] = pr[i] + mlr[i];
            }
            master_process(ms, pl, pr, mll, mlr, ol2, or2, BLK);
            for (i = 0; i < BLK; i++) {
                int t0 = n + i - 64;                               /* the limiter looks 64 samples ahead */
                if (t0 >= 0)
                    ok &= ol2[i] == hist_l[t0 % (BLK * 4)] && or2[i] == hist_r[t0 % (BLK * 4)];
            }
            n += BLK;
        }
        check("EQ flat (64): bit-transparent (drums + melodic, 64 samples late)", ok);
        /* the EQ set: the bands move */
        {
            float lo0 = 0.0f, lo1 = 0.0f, hi0 = 0.0f, hi1 = 0.0f;
            int pass;
            for (pass = 0; pass < 2; pass++) {
                master_set(ms, MS_EQ_LO, pass ? 127 : 64);
                master_set(ms, MS_EQ_HI, pass ? 0 : 64);
                for (b = 0; b < 120; b++) {
                    for (i = 0; i < BLK; i++) {
                        pl[i] = pr[i] = 0.2f * fm_sin_turns((float)(b * BLK + i) * (60.0f / SR));
                        mll[i] = mlr[i] = 0.0f;
                    }
                    master_process(ms, pl, pr, mll, mlr, ol2, or2, BLK);
                    if (b >= 60)
                        for (i = 0; i < BLK; i++)
                            *(pass ? &lo1 : &lo0) += ol2[i] * ol2[i];
                }
                for (b = 0; b < 120; b++) {
                    for (i = 0; i < BLK; i++) {
                        pl[i] = pr[i] = 0.2f * fm_sin_turns((float)(b * BLK + i) * (10000.0f / SR));
                        mll[i] = mlr[i] = 0.0f;
                    }
                    master_process(ms, pl, pr, mll, mlr, ol2, or2, BLK);
                    if (b >= 60)
                        for (i = 0; i < BLK; i++)
                            *(pass ? &hi1 : &hi0) += ol2[i] * ol2[i];
                }
            }
            snprintf(name, sizeof name, "EQ low 127, high 0: low +12 dB (%.1f dB), high -12 dB (%.1f dB)",
                     (double)(10.0f * 0.30103f * fm_log2f(lo1 / lo0)), (double)(10.0f * 0.30103f * fm_log2f(hi1 / hi0)));
            check(name, lo1 > lo0 * 10.0f && lo1 < lo0 * 20.0f && hi1 < hi0 * 0.1f && hi1 > hi0 * 0.05f);
        }
        MS_INIT();
        {   /* the limiter: a signal at 4x full scale never passes 1.0 */
            float pk = 0.0f;
            master_set(ms, MS_LEVEL, 127);
            for (b = 0; b < 400; b++) {
                for (i = 0; i < BLK; i++) {
                    pl[i] = pr[i] = 2.0f * noise();
                    mll[i] = mlr[i] = 2.0f * ((b & 8) ? 1.0f : -1.0f);
                }
                master_process(ms, pl, pr, mll, mlr, ol2, or2, BLK);
                for (i = 0; i < BLK; i++) {
                    ok &= ol2[i] == ol2[i];
                    pk = fm_maxf(pk, fm_maxf(fm_fabsf(ol2[i]), fm_fabsf(or2[i])));
                }
            }
            snprintf(name, sizeof name, "limiter on a 4x signal: peak %.3f, no NaN", (double)pk);
            check(name, ok && pk <= 1.0f);
        }
        MS_INIT();
        {   /* the compressor at 100: a loud sine is pulled down, the meter shows it */
            float g0 = 0.0f, g1 = 0.0f;
            master_set(ms, MS_COMP, 100);
            for (b = 0; b < 400; b++) {
                for (i = 0; i < BLK; i++) {
                    pl[i] = pr[i] = 0.0f;
                    mll[i] = mlr[i] = 0.5f * fm_sin_turns((float)(b * BLK + i) * (200.0f / SR));
                }
                master_process(ms, pl, pr, mll, mlr, ol2, or2, BLK);
                if (b >= 300)
                    for (i = 0; i < BLK; i++)
                        g1 += ol2[i] * ol2[i];
            }
            snprintf(name, sizeof name, "compressor 100 on a -6 dB sine: %.1f dB of reduction shown", (double)master_gr_db(ms));
            check(name, master_gr_db(ms) > 3.0f);
            master_set(ms, MS_COMP, 0);
            for (b = 0; b < 400; b++) {
                for (i = 0; i < BLK; i++) {
                    pl[i] = pr[i] = 0.0f;
                    mll[i] = mlr[i] = 0.05f * fm_sin_turns((float)(b * BLK + i) * (200.0f / SR));
                }
                master_process(ms, pl, pr, mll, mlr, ol2, or2, BLK);
                if (b >= 300)
                    for (i = 0; i < BLK; i++)
                        g0 += ol2[i] * ol2[i];
            }
            master_set(ms, MS_COMP, 100);
            g1 = 0.0f;
            for (b = 0; b < 400; b++) {
                for (i = 0; i < BLK; i++) {
                    pl[i] = pr[i] = 0.0f;
                    mll[i] = mlr[i] = 0.05f * fm_sin_turns((float)(b * BLK + i) * (200.0f / SR));
                }
                master_process(ms, pl, pr, mll, mlr, ol2, or2, BLK);
                if (b >= 300)
                    for (i = 0; i < BLK; i++)
                        g1 += ol2[i] * ol2[i];
            }
            check("compressor 100 lifts a quiet signal (makeup)", g1 > g0 * 2.0f);
        }
        {   /* group levels: perc at 0 mutes the percussion group */
            float e = 0.0f;
            MS_INIT();
            master_set(ms, MS_PERC, 0);
            for (b = 0; b < 200; b++) {
                for (i = 0; i < BLK; i++) {
                    pl[i] = pr[i] = 0.3f * noise();
                    mll[i] = mlr[i] = 0.0f;
                }
                master_process(ms, pl, pr, mll, mlr, ol2, or2, BLK);
                if (b >= 150)                                      /* the level glides ~10 ms, then lands */
                    for (i = 0; i < BLK; i++)
                        e += ol2[i] * ol2[i];
            }
            check("perc level 0 silences the percussion group (after its glide)", e == 0.0f);
        }
        {   /* PUMP (X0X's): a kick ducks the mix by the depth and swells back over the release */
            static float g[700], g2[700];
            static float ref_l[200 * BLK], ref_r[200 * BLK];
            int same = 1;
            float d, d2, at;
            /* depth 0 (a new project's): a hit changes nothing, sample for sample, at the compressor's 30 too */
            for (int c = 0; c < 2; c++) {
                MS_INIT();
                master_set(ms, MS_COMP, 30);
                rng = 777;
                for (b = 0; b < 200; b++) {
                    if (c && b % 20 == 3)
                        for (int s2 = 0; s2 < NPB; s2++)
                            master_key(ms, s2, 127, s2 == PB_KICK);
                    for (i = 0; i < BLK; i++) {
                        pl[i] = 0.3f * noise(); pr[i] = 0.3f * noise(); mll[i] = 0.3f * noise(); mlr[i] = 0.3f * noise();
                    }
                    master_process(ms, pl, pr, mll, mlr, ol2, or2, BLK);
                    for (i = 0; i < BLK; i++) {
                        if (!c) {
                            ref_l[b * BLK + i] = ol2[i];
                            ref_r[b * BLK + i] = or2[i];
                        } else
                            same &= ol2[i] == ref_l[b * BLK + i] && or2[i] == ref_r[b * BLK + i];
                    }
                }
            }
            check("pump 0: hits change nothing (bit-identical, compressor at 30)", same);
            /* 24 dB (127), accent off, by the kick, the compressor off: down 24 dB within ~30 ms, back with the
             * release (107 ms: a third of the dip left after one time constant), whole after ~0.7 s */
            MS_INIT();
            master_set(ms, MS_PUMP, 127);
            master_set(ms, MS_PUMP_ACC, 0);
            pump_trace(g, 700, 20, -1, PB_KICK, 64);
            d = trace_min(g, 20, 60);
            for (b = 20; b < 700 && g[b] > -21.6f; b++)    /* (block 0 is the limiter's lookahead: silence) */
                ;
            at = (float)(b - 20) * (float)BLK * 1000.0f / SR;
            snprintf(name, sizeof name, "pump 24 dB: a kick ducks %.1f dB, 90 %% of it in %.0f ms", (double)-d, (double)at);
            check(name, d < -23.0f && d > -24.5f && at < 45.0f && trace_min(g, 2, 20) > -0.01f);
            for (b = 20; b < 700 && g[b] > d + 0.01f; b++)
                ;
            {
                int tb = b + (int)(master_pump_ms(MS_PUMP_REL_DEF) * 0.001f * SR / BLK + 0.5f);
                snprintf(name, sizeof name, "  release 107 ms: %.1f dB of the dip left one time constant on", (double)-g[tb]);
                check(name, g[tb] < -6.5f && g[tb] > -11.0f);
                snprintf(name, sizeof name, "  swelled back: %.2f dB after 0.8 s", (double)-g[20 + (int)(0.8f * SR / BLK)]);
                check(name, g[20 + (int)(0.8f * SR / BLK)] > -0.1f);
            }
            /* a longer release swells back slower */
            MS_INIT();
            master_set(ms, MS_PUMP, 127);
            master_set(ms, MS_PUMP_ACC, 0);
            master_set(ms, MS_PUMP_REL, 100);
            pump_trace(g2, 700, 20, -1, PB_KICK, 64);
            snprintf(name, sizeof name, "  release %.0f ms: %.1f dB left at 300 ms (107 ms: %.1f)", (double)master_pump_ms(100),
                     (double)-g2[20 + (int)(0.3f * SR / BLK)], (double)-g[20 + (int)(0.3f * SR / BLK)]);
            check(name, g2[20 + (int)(0.3f * SR / BLK)] < g[20 + (int)(0.3f * SR / BLK)] - 3.0f);
            /* accent on: a soft hit pumps less than a full one (0.5 .. 1 of the depth) */
            MS_INIT();
            master_set(ms, MS_PUMP, 127);
            pump_trace(g, 100, 10, -1, PB_KICK, 127);
            MS_INIT();
            master_set(ms, MS_PUMP, 127);
            pump_trace(g2, 100, 10, -1, PB_KICK, 40);
            d = trace_min(g, 10, 60);
            d2 = trace_min(g2, 10, 60);
            snprintf(name, sizeof name, "  accent: velocity 127 ducks %.1f dB, 40 ducks %.1f dB", (double)-d, (double)-d2);
            check(name, d < -23.0f && d2 > -17.5f && d2 < -15.0f);
            /* 12 dB (64) by track 3: the kick and track 2 leave it, track 3 ducks it 12 dB, the metronome not */
            MS_INIT();
            master_set(ms, MS_PUMP, 64);
            master_set(ms, MS_PUMP_BY, PB_T1 + 2);
            pump_trace(g, 60, 10, 30, PB_KICK, 127);
            d = trace_min(g, 2, 60);
            pump_trace(g, 60, 10, 30, PB_T1 + 1, 127);
            d = fm_minf(d, trace_min(g, 2, 60));
            pump_trace(g, 60, 10, 30, PB_METRO, 127);
            d = fm_minf(d, trace_min(g, 2, 60));
            pump_trace(g2, 60, 10, -1, PB_T1 + 2, 127);
            d2 = trace_min(g2, 10, 60);
            snprintf(name, sizeof name, "  pump by T3: others %.2f dB, T3 %.1f dB (12.1)", (double)d, (double)d2);
            check(name, d > -0.01f && d2 < -11.5f && d2 > -12.5f);
            check("  the duck shown (master_pump_db)", master_pump_db(ms) > 1.0f);
            {   /* a kit's hit keys it: the drums (the kick itself) not ducked, the melodic group ducked; a melodic
                 * track's note: the drums ducked too */
                static float zz[BLK], dd[BLK], o1[BLK], o2[BLK];
                float lo_k = 1e9f, lo_m = 1e9f;
                for (int pass = 0; pass < 2; pass++) {
                    MS_INIT();
                    master_set(ms, MS_PUMP, 127);
                    master_set(ms, MS_PUMP_ACC, 0);
                    if (pass)
                        master_set(ms, MS_PUMP_BY, PB_T1 + 1);
                    for (b = 0; b < 60; b++) {
                        if (b == 20)
                            master_key(ms, pass ? PB_T1 + 1 : PB_KICK, 127, !pass);
                        for (i = 0; i < BLK; i++)
                            zz[i] = 0.0f, dd[i] = 0.1f;
                        master_process(ms, dd, dd, zz, zz, o1, o2, BLK);
                        for (i = 0; i < BLK; i++)
                            if (b > 20)
                                *(pass ? &lo_m : &lo_k) = fm_minf(pass ? lo_m : lo_k, o1[i]);
                    }
                }
                snprintf(name, sizeof name, "  a kit's hit leaves the drums whole (%.2f), a melodic note ducks them (%.3f)",
                         (double)lo_k, (double)lo_m);
                check(name, lo_k > 0.099f && lo_m < 0.02f);
            }
            /* with the compressor: the pump adds to its reduction */
            MS_INIT();
            master_set(ms, MS_COMP, 60);
            pump_trace(g, 60, -1, -1, PB_KICK, 127);
            master_set(ms, MS_PUMP, 127);
            pump_trace(g2, 60, 10, -1, PB_KICK, 127);
            snprintf(name, sizeof name, "  under the compressor: %.1f dB, with a kick %.1f dB", (double)g[59],
                     (double)trace_min(g2, 10, 60));
            check(name, trace_min(g2, 10, 60) < g[59] - 18.0f);
        }
    }

    printf("-- the reverb's SPU modes\n");
    {
        int ok = 1;
        for (t = FXT_REVERB + RV_ROOM; t < NFXPICK; t++) {   /* each decays (the chaos echo: at decay 0) */
            uint8_t kn[4] = {RV_SPU_DEFAULT[0], RV_SPU_DEFAULT[1], RV_SPU_DEFAULT[2], RV_SPU_DEFAULT[3]};
            int chaos = t == FXT_REVERB + RV_CHAOS_ECHO;
            float pk = 0.0f;
            if (chaos)
                kn[1] = 0;
            run_fx(t, kn, &r, 0);
            for (b = 0; b < 20 * SR / BLK; b++) {             /* 20 s of silence: it empties */
                memset(in, 0, sizeof in);
                memset(ol, 0, sizeof ol);
                memset(orr, 0, sizeof orr);
                fx_process(fx, in, ol, orr, BLK);
            }
            for (i = 0; i < BLK; i++)
                pk = fm_maxf(pk, fm_fabsf(ol[i]) + fm_fabsf(orr[i]));
            snprintf(name, sizeof name, "  %-10s after the burst %6.1f dB, a second on %6.1f dB, 20 s on %s", fx_pick_name(t),
                     (double)db(r.rms_after), (double)db(r.rms_end), pk < 1e-5f ? "silent" : "NOT silent");
            check(name, !r.nan && r.peak < 4.0f && r.rms_after > 0.001f && r.rms_end < r.rms_after * (chaos ? 0.5f : 0.9f) &&
                        pk < 1e-5f);
        }
        {   /* the chaos echo at its own decay: almost endless, still bounded */
            static const uint8_t kn[4] = {127, 127, 64, 0};
            float pk = 0.0f;
            fx_init(fx, dly_l, dly_r, rv);
            fx_set(fx, FXT_REVERB + RV_CHAOS_ECHO, kn);
            for (b = 0; b < 30 * SR / BLK; b++) {
                for (i = 0; i < BLK; i++)
                    in[i] = b * BLK + i < SR ? 0.5f * noise() : 0.0f;
                memset(ol, 0, sizeof ol);
                memset(orr, 0, sizeof orr);
                fx_process(fx, in, ol, orr, BLK);
                for (i = 0; i < BLK; i++) {
                    ok &= ol[i] == ol[i] && orr[i] == orr[i];
                    pk = fm_maxf(pk, fm_maxf(fm_fabsf(ol[i]), fm_fabsf(orr[i])));
                }
            }
            snprintf(name, sizeof name, "  chaos echo at decay 127, 30 s: peak %.2f, no NaN", (double)pk);
            check(name, ok && pk < 4.0f);
        }
        {   /* size: the hall's tail later at x4 (the console's) than at x1 (psxverb's) */
            static const uint8_t k0[4] = {0, 64, 64, 0}, k1[4] = {127, 64, 64, 0};
            float c0 = centre_time(FXT_REVERB + RV_HALL, k0), c1 = centre_time(FXT_REVERB + RV_HALL, k1);
            snprintf(name, sizeof name, "  hall size 0 -> 127: the energy's centre %.3f s -> %.3f s", (double)c0, (double)c1);
            check(name, c1 > c0 * 2.0f);
        }
        {   /* damp darkens the tail */
            static const uint8_t k0[4] = {127, 64, 64, 0}, k1[4] = {127, 64, 64, 127};
            float br[2];
            impulse(FXT_REVERB + RV_STUDIO_L, k0, imp_l, imp_r, SR);
            br[0] = bright(imp_l, 4410, SR);
            impulse(FXT_REVERB + RV_STUDIO_L, k1, imp_l, imp_r, SR);
            br[1] = bright(imp_l, 4410, SR);
            snprintf(name, sizeof name, "  studio L damp 0 -> 127: the tail darker (%.4f -> %.4f)", (double)br[0], (double)br[1]);
            check(name, br[1] < br[0] * 0.7f);
        }
        {   /* another mode mid-tail: the area emptied, the new one starts quiet */
            static const uint8_t kn[4] = {127, 64, 64, 0};
            float pk = 0.0f;
            run_fx(FXT_REVERB + RV_HALL, kn, &r, 0);
            fx_set(fx, FXT_REVERB + RV_ROOM, kn);
            for (b = 0; b < 100; b++) {
                memset(in, 0, sizeof in);
                memset(ol, 0, sizeof ol);
                memset(orr, 0, sizeof orr);
                fx_process(fx, in, ol, orr, BLK);
                for (i = 0; i < BLK; i++)
                    pk = fm_maxf(pk, fm_fabsf(ol[i]));
            }
            snprintf(name, sizeof name, "  hall -> room mid-tail: the room starts quiet (peak %.6f)", (double)pk);
            check(name, pk < 1e-4f);
        }
    }

    printf("-- cost, instructions a sample\n");
    {
        static float in2[256], a[256], c[256], p1[256], p2[256], m1[256], m2[256];
        uint64_t i0, di;
        for (t = 0; t < NFXPICK; t++) {
            static const uint8_t mid[4] = {64, 64, 64, 64};
            fx_init(fx, dly_l, dly_r, rv);
            fx_set(fx, t, t > FXT_REVERB ? RV_SPU_DEFAULT : mid);
            for (b = 0; b < 40; b++)                          /* (an SPU mode clears its area first) */
                fx_process(fx, in2, a, c, 256);
            for (b = 0; b < 20; b++) {
                for (i = 0; i < 256; i++)
                    in2[i] = 0.3f * noise();
                fx_process(fx, in2, a, c, 256);
            }
            i0 = instr_now();
            for (b = 0; b < 20; b++)
                fx_process(fx, in2, a, c, 256);
            di = instr_now() - i0;
            printf("  %-11s %5.0f\n", fx_pick_name(t), (double)di / (20.0 * 256.0));
        }
        MS_INIT();
        for (i = 0; i < 256; i++) {
            p1[i] = p2[i] = 0.3f * noise();
            m1[i] = m2[i] = 0.3f * noise();
        }
        for (b = 0; b < 10; b++)
            master_process(ms, p1, p2, m1, m2, a, c, 256);
        i0 = instr_now();
        for (b = 0; b < 20; b++)
            master_process(ms, p1, p2, m1, m2, a, c, 256);
        di = instr_now() - i0;
        printf("  master off  %5.0f  (levels and the limiter)\n", (double)di / (20.0 * 256.0));
        master_set(ms, MS_EQ_LO, 80);
        master_set(ms, MS_COMP, 80);
        for (b = 0; b < 10; b++)
            master_process(ms, p1, p2, m1, m2, a, c, 256);
        i0 = instr_now();
        for (b = 0; b < 20; b++)
            master_process(ms, p1, p2, m1, m2, a, c, 256);
        di = instr_now() - i0;
        printf("  master all  %5.0f  (EQ, compressor, limiter)\n", (double)di / (20.0 * 256.0));
        master_set(ms, MS_PUMP, 64);
        for (b = 0; b < 10; b++)
            master_process(ms, p1, p2, m1, m2, a, c, 256);
        i0 = instr_now();
        for (b = 0; b < 20; b++) {
            if (!(b & 3))
                master_key(ms, PB_KICK, 127, 1);
            master_process(ms, p1, p2, m1, m2, a, c, 256);
        }
        di = instr_now() - i0;
        printf("  master pump %5.0f  (all, and the pump pumping)\n", (double)di / (20.0 * 256.0));
    }
    {   /* the engine runs the chorus, the delay and the plate a block behind (fx_process_lag): their output must be
         * where it was, but for the lines' rounding (a noise burst, then its tail; the send a block late) */
        static float xa[SR * 3], ya_l[SR * 3], ya_r[SR * 3], yb_l[SR * 3], yb_r[SR * 3];
        static const int picks[3] = {FXT_CHORUS, FXT_DELAY, FXT_REVERB};
        static const uint8_t kn[3][4] = {{50, 86, 90, 127}, {40, 64, 110, 100}, {100, 40, 44, 72}};
        const int L = 256, n = SR * 3;
        printf("-- a block behind (fx_process_lag, 256 samples): the same output, but for the lines' rounding\n");
        for (i = 0; i < n; i++)               /* (never silent: a sleeping line would skip a block in one run only) */
            xa[i] = (i < SR / 3 ? 0.4f : 0.001f) * noise();
        for (t = 0; t < 3; t++) {
            float pk = 0.0f, err = 0.0f;
            for (b = 0; b < 2; b++) {
                float *yl = b ? yb_l : ya_l, *yr = b ? yb_r : ya_r;
                fx_init(fx, dly_l, dly_r, rv);
                fx_set(fx, picks[t], kn[t]);
                fx_tempo(fx, 120.0f);
                for (i = 0; i < n; i++)
                    yl[i] = yr[i] = 0.0f;
                for (i = 0; i + L <= n; i += L) {
                    static float blk[256];
                    int j;
                    for (j = 0; j < L; j++)
                        blk[j] = b ? (i + j >= L ? xa[i + j - L] : 1e-30f) : xa[i + j];   /* (not 0: awake) */
                    fx_process_lag(fx, blk, yl + i, yr + i, (uint32_t)L, b ? L : 0);
                }
            }
            for (i = 0; i < n; i++) {
                pk = fm_maxf(pk, fm_maxf(fm_fabsf(ya_l[i]), fm_fabsf(ya_r[i])));
                err = fm_maxf(err, fm_maxf(fm_fabsf(ya_l[i] - yb_l[i]), fm_fabsf(ya_r[i] - yb_r[i])));
            }
            snprintf(name, sizeof name, "  %-8s peak %6.1f dB, the largest difference %6.1f dB under it", FXT_NAME[picks[t]],
                     (double)db(pk), (double)(db(pk) - db(err)));
            check(name, pk > 0.01f && db(pk) - db(err) > (picks[t] == FXT_REVERB ? 45.0f : 60.0f));   /* (the plate's
                                                   * 12-bit combs: its own rounding, recirculated, is of that size) */
        }
    }
    printf("%s\n", bad ? "FX TESTS FAILED" : "fx tests passed");
    return bad != 0;
}
