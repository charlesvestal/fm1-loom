/* SPDX-License-Identifier: GPL-3.0-only */
/* LOOM-8 effects (fx.h): the send slots and the master section.
 *
 * Send slots: each takes a mono send and adds a stereo return. The reverb is 9W9's (X0X's fxbus.c):
 * four int16 combs with damping in the loop and two allpasses a side, the right side reading the
 * combs with alternate signs; here the combs are circular buffers read at a modulated, interpolated
 * tap so "size" scales their lengths and "motion" wanders them (at a fixed 0.5 Hz), "damp" sets the
 * combs' damping, and "tone" filters the wet: a low-pass below the centre, a high-pass above it, flat
 * in the middle. The delay is a ping-pong over the two int16 lines, timed in divisions of the tempo
 * (1/32 to 1/2, dotted ones between) and slewed so a change of time warps instead of clicking (9W9
 * again), its repeats low-passed by "tone"; the chorus is the JP-8000's stepped, slewed triangle on
 * a short int16 line (NuSaw's); the lofi a sample-and-hold with a quantiser, "smooth" its anti-alias
 * filters, its "width" a stereo spread: the right side reads the lofi output back from the chorus's
 * idle line a few milliseconds late, the lateness wandering slowly; the phaser four first-order
 * allpasses a side, the right side's sweep a quarter turn behind. Every effect returns its wet sound
 * only: the FX tracks are plain send / return (the phaser's wet is its notches, the send summed with
 * its allpassed copy).
 *
 * Knob values are turned into the effect's numbers in fx_set (main loop) and glide toward them in
 * the interrupt a block at a time (~10 ms): no zipper noise on a turn. A change of effect clears
 * the states at the next block; fx_set zeroes the new effect's lines itself, which is safe because
 * the running effect never uses another's line (the lofi borrows the chorus's, which the chorus
 * cannot be using at the same time). The slot's filter page (high-pass, low-pass) runs on the mono
 * send before the effect: one filter instead of two, and the same result for the linear effects.
 *
 * The reverb has modes: the plate above, the PlayStation's SPU reverbs (psxrev.h, from
 * schwung-psxverb: MIT), which run on the same line, all of it, and lush: Dattorro's plate (J. Dattorro, "Effect
 * Design, Part 1: Reverberator and Other Filters", JAES 45(9), 1997) at half the rate, on the same line (below).
 *
 * Master: the two groups' levels, a 3-band EQ (RBJ shelves and a peak; all three at 64 a true bypass), X0X's bus compressor behind its one-knob macro with X0X's PUMP in its gain
 * (a hit of the pump's source ducks the whole mix: the sequencer keys it, so it needs no detector and a
 * muted track keys it too; at 0 the stage is as before, sample for sample), the master level and X0X's
 * lookahead limiter.
 *
 * Everything here runs in the audio interrupt; fx_set / master_set write plain words from the main
 * loop. Float, fastmath.h; stack locals stay small (a 32-sample block). */
#include "fx.h"
#include "fastmath.h"

#define SR 44100.0f
#define SUB 32                                /* the block: parameter glides step here */
#define GLIDE 0.07f                           /* a block's share of the distance: ~10 ms to settle */
#define CHO_N 2048                            /* the chorus line: 46 ms (int16; the lofi's spread reads it too) */
#define RV_MOD_HZ 0.5f                        /* the reverb's wander */
#define RV_WET 0.8f                           /* the reverb's return level */
#define LOFI_SPREAD 480.0f                    /* width 127: the right side ~11 ms behind the left */

static void zero16(int16_t *b, int32_t n);
#include "psxrev.h"

const char *const FXT_NAME[NFXT] = {"chorus", "delay", "lofi", "phaser", "reverb"};
const char *const RV_NAME[NRV] = {"plate", "room", "studio S", "studio M", "studio L", "hall", "half echo",
                                  "space echo", "chaos echo", "one echo", "lush"};
static const char *const FXT_KNOB[NFXT][4] = {
    {"rate", "depth", "feedback", "width"}, {"time", "fine", "repeats", "tone"},
    {"rate", "bits", "smooth", "width"},    {"freq", "depth", "rate", "feedback"},
    {"size", "motion", "tone", "damp"},
};
static const char *const SPU_KNOB[4] = {"size", "decay", "tone", "damp"};
/* the defaults, 0..127 (what each gives: fx_derive's mappings) */
const uint8_t FXT_DEFAULT[NFXT][4] = {
    {50, 86, 20, 127},                        /* chorus      0.33 Hz, +-5.5 ms, feedback 0.11, fully wide */
    {72, 64, 56, 64},                         /* delay       1/8, on the beat, feedback 0.42 (~4 repeats), 4 kHz */
    {80, 80, 64, 40},                         /* lofi        8.4 kHz, 10.8 bits, half smoothed, 3.4 ms spread */
    {50, 90, 50, 80},                         /* phaser      466 Hz, +-2.1 octaves, 0.27 Hz, feedback 0.57 */
    {100, 40, 44, 72},                        /* plate       combs x1.43, 2.4 samples of wander, 7.4 kHz, damp 0.45 */
};
const uint8_t RV_SPU_DEFAULT[4] = {127, 64, 64, 0};   /* the console's sizes, its decay, flat, its own filter */
const uint8_t RV_LUSH_DEFAULT[4] = {64, 64, 56, 48};  /* (fx.h) */

const char *fx_pick_name(int pick)
{
    int t = fx_pick_type(pick);
    return t == FXT_REVERB ? RV_NAME[fx_pick_mode(pick)] : FXT_NAME[t];
}
const char *fx_knob_name(int pick, int k)
{
    int t = fx_pick_type(pick);
    k &= 3;
    return t == FXT_REVERB && fx_pick_mode(pick) != RV_PLATE ? SPU_KNOB[k] : FXT_KNOB[t][k];
}

/* the reverb's lines inside the int16 buffer: 9W9's comb lengths, each with room to grow 1.9x,
 * and its four allpasses (left 556 / 441, right 579 / 457) */
static const int32_t RV_COMB[4] = {1116, 1188, 1277, 1356};
static const int32_t RV_CAP[4] = {2125, 2262, 2431, 2581};
static const int32_t RV_AP[4] = {556, 441, 579, 457};
#define RV_AP0 (2125 + 2262 + 2431 + 2581)
_Static_assert(RV_AP0 + 556 + 441 + 579 + 457 <= FX_RV_LEN, "the reverb's lines do not fit FX_RV_LEN");
/* the delay's times in sixteenths: 1/32, 1/32D, 1/16, 1/16D, 1/8, 1/8D, 1/4, 1/2 (dotted = 1.5x) */
static const float DL_DIV[8] = {0.5f, 0.75f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f, 8.0f};

/* lush: Dattorro's plate at 22.05 kHz on the reverb line (int16, through psxrev.h's spu_rd / spu_wr: one ring whose
 * base moves a sample a tick; a line at offset o is written at o and read len ticks later at o + len). The lines in
 * Dattorro's order, each with room for the largest size (x1.5 of his lengths at 29761 Hz, scaled to 22050 Hz) and the
 * modulation: the pre-delay, four input diffusers, then each half of the tank: a modulated allpass, a delay, an
 * allpass, a delay */
enum { LU_PRE, LU_IN1, LU_IN2, LU_IN3, LU_IN4, LU_MAPL, LU_DL1, LU_APL, LU_DL2, LU_MAPR, LU_DR1, LU_APR, LU_DR2, NLU };
static const int32_t LU_OFF[NLU + 1] = {0, 1200, 1397, 1555, 2016, 2363, 3149, 8137, 10177, 14351, 15400, 20126,
                                        23117, 26672};
static const float LU_LEN[NLU] = {0.0f, 142.0f, 107.0f, 379.0f, 277.0f, 672.0f, 4453.0f, 1800.0f, 3720.0f, 908.0f,
                                  4217.0f, 2656.0f, 3163.0f};   /* Dattorro's, at 29761 Hz */
_Static_assert(26672 <= FX_RV_LEN, "lush's lines do not fit FX_RV_LEN");
typedef struct {
    int32_t len[NLU];                 /* the lines' lengths at the size now, 22.05 kHz samples */
    uint32_t base;
    int32_t clear, quiet;             /* (as the SPU's) */
    float lp, hold, lfo, dmp[2], fb[2], prev[2], cur[2], tl, th, tr, thr, scale;
    uint8_t phase;
} lush_t;

struct fxslot {
    int16_t cho[CHO_N];
    int16_t *dl, *dr, *rv;
    uint8_t type, type_t, hpv, lpv;
    uint8_t mode, mode_t;                     /* the reverb's mode: running, and as set */
    uint8_t knob[4];
    float bpm;
    float pt[6], pc[6];                       /* the effect's numbers: targets, and gliding */
    float hpc, lpc, hps, lps;                 /* the filter page */
    /* chorus */
    int32_t cw;
    float cph, cs1, cs2, chl, chr, cfbz;
    /* delay */
    int32_t dw, dquiet;
    float dcur, dlpl, dlpr;
    /* lofi */
    float lph, lhold, lin1, lout1, lwph;
    /* phaser */
    float pxl[4], pyl[4], pxr[4], pyr[4], pph, pzl, pzr, pal, par_;
    /* reverb */
    int32_t cpos[4], apos[4], rquiet;
    float cdmp[4], cd[4], rph, rhp, rtl[2], rtr[2];   /* rtl / rtr: the tone's low-pass and high-pass a side */
    union {                                   /* the SPU reverb or lush (one mode at a time: fx_states) */
        spu_t spu;
        lush_t lu;
    };
};
_Static_assert(sizeof(struct fxslot) <= FXSLOT_BYTES, "fxslot_t does not fit FXSLOT_BYTES");

static inline float onepole_k(float hz) { return hz >= 20000.0f ? 1.0f : 1.0f - fm_expf(-FM_TWO_PI * hz / SR); }
static inline float k127(int v) { return (float)(v < 0 ? 0 : v > 127 ? 127 : v) * (1.0f / 127.0f); }
static inline int16_t f16(float x)
{
    float v = x * 16384.0f;
    return (int16_t)(v > 32767.0f ? 32767.0f : v < -32767.0f ? -32767.0f : v);
}
static inline float s16f(int16_t v) { return (float)v * (1.0f / 16384.0f); }

static void zero16(int16_t *b, int32_t n)
{
    int32_t i;
    for (i = 0; i < n; i++)
        b[i] = 0;
}

static void fx_states(fxslot_t *f)              /* a fresh effect: its states at rest */
{
    int i;
    zero16(f->cho, CHO_N);
    f->cw = 0; f->cph = 0.0f; f->cs1 = f->cs2 = f->chl = f->chr = f->cfbz = 0.0f;
    f->dw = 0; f->dquiet = FX_DLY_LEN; f->dlpl = f->dlpr = 0.0f;
    f->dcur = f->pt[0];                       /* a new delay starts on time (the slew is for changes) */
    f->lph = f->lhold = f->lin1 = f->lout1 = f->lwph = 0.0f;
    for (i = 0; i < 4; i++) {
        f->pxl[i] = f->pyl[i] = f->pxr[i] = f->pyr[i] = 0.0f;
        f->cpos[i] = f->apos[i] = 0;
        f->cdmp[i] = 0.0f;
        f->cd[i] = f->pt[0] * (float)RV_COMB[i];
    }
    f->pph = 0.0f; f->pzl = f->pzr = 0.0f; f->pal = f->par_ = 0.0f;
    f->rquiet = FX_RV_LEN; f->rph = f->rhp = 0.0f;
    f->rtl[0] = f->rtl[1] = f->rtr[0] = f->rtr[1] = 0.0f;
    for (i = 0; i < 6; i++)
        f->pc[i] = f->pt[i];
    if (f->mode != RV_LUSH) {
        spu_reset(&f->spu, f->mode);
    } else {
        lush_t *u = &f->lu;
        uint8_t *z = (uint8_t *)u;
        for (i = 0; i < (int)sizeof *u; i++)
            z[i] = 0;
        u->clear = FX_RV_LEN;
        u->quiet = FX_RV_LEN * 2;
    }
}

static void delay_time(fxslot_t *f)
{
    float bpm = f->bpm > 20.0f ? f->bpm : 120.0f;
    float t = DL_DIV[(f->knob[0] * 8) >> 7] * 15.0f / bpm;           /* sixteenths: 60 / (bpm * 4) */
    float max_s = (float)(FX_DLY_LEN - 256) / SR;
    t *= 1.0f + ((float)f->knob[1] - 64.0f) * (0.25f / 64.0f);        /* fine: +-25 % */
    while (t > max_s)                                                 /* too long for the line: half, */
        t *= 0.5f;                                                    /* still on the beat */
    f->pt[0] = t * SR;
}

/* the knobs to the effect's numbers (its pt[]: what each means is in the effect's loop) */
static void fx_derive(fxslot_t *f)
{
    const uint8_t *k = f->knob;
    float a = k127(k[0]), b = k127(k[1]), c = k127(k[2]), d = k127(k[3]);
    switch (f->type_t) {
    case FXT_CHORUS:
        f->pt[0] = 0.05f * fm_exp2f(a * 6.9f);                        /* rate 0.05 .. 6 Hz */
        f->pt[1] = (0.3f + 7.7f * b) * (SR / 1000.0f);                /* depth 0.3 .. 8 ms */
        f->pt[2] = 0.7f * c;                                          /* feedback */
        f->pt[3] = d;                                                 /* width: 0 mono .. 1 opposite */
        break;
    case FXT_DELAY:
        delay_time(f);                                                /* time and fine */
        f->pt[2] = 0.95f * c;                                         /* repeats: the feedback */
        f->pt[3] = onepole_k(800.0f * fm_exp2f(d * 4.6f));            /* tone: the repeats' low-pass 800 Hz .. 19 kHz */
        break;
    case FXT_LOFI: {
        float fs = 500.0f * fm_exp2f(a * 6.46f);                      /* 500 Hz .. 44.1 kHz */
        float bits = 2.0f + 14.0f * b;
        f->pt[0] = fm_minf(fs / SR, 1.0f);
        f->pt[1] = fm_exp2f(bits - 1.0f);                             /* quantiser levels */
        f->pt[2] = c;                                                 /* smooth: the anti-alias filters */
        f->pt[3] = d * LOFI_SPREAD;                                   /* width: the right's lateness, samples */
        f->pt[4] = onepole_k(fm_minf(0.45f * fs, 19000.0f));
        f->pt[5] = 1.0f / f->pt[1];
        break;
    }
    case FXT_PHASER:
        f->pt[0] = 100.0f * fm_exp2f(a * 5.64f);                      /* 100 Hz .. 5 kHz */
        f->pt[1] = 3.0f * b;                                          /* depth, octaves */
        f->pt[2] = 0.03f * fm_exp2f(c * 8.06f);                       /* rate 0.03 .. 8 Hz */
        f->pt[3] = 0.9f * d;                                          /* feedback */
        break;
    case FXT_REVERB:
        if (f->mode_t == RV_LUSH) {
            f->pt[0] = 0.5f + a;                                      /* size: Dattorro's lengths x0.5 .. x1.5 */
            /* decay: the tail's time, the tank's gain from it and the size (a trip round both halves ~0.715 s x size,
             * the gain met twice: 40 log10(g) = -60 trip / T, T 0.4 .. 19 s); the damping takes more, so measured
             * (an impulse, 60 dB down): 0 0.56 s, 45 0.9 s, 64 ~1.4 s, 100 3.7 s, 127 7.2 s, whatever the size */
            f->pt[1] = fm_exp2f(-1.5f * 3.32192809f * 0.715f * (0.5f + a) / (0.4f * fm_exp2f(b * 5.6f)));
            f->pt[2] = c < 0.5f ? onepole_k(20000.0f * fm_exp2f(-(0.5f - c) * 9.29f)) : 1.0f;   /* tone: the plate's */
            f->pt[5] = c > 0.5f ? onepole_k(20.0f * fm_exp2f((c - 0.5f) * 13.29f)) : 0.0f;
            f->pt[3] = 0.05f + 0.85f * d;                             /* damp: the tank's low-pass, 0.05 open .. 0.9 */
            f->pt[4] = (0.004f + 0.03f * a) * 22050.0f;               /* the pre-delay grows with the size: 4 .. 34 ms */
            break;
        }
        if (f->mode_t != RV_PLATE) {                                  /* an SPU mode: */
            f->pt[0] = 1.0f + 3.0f * a;                               /* size: the registers x1 .. x4 */
            f->pt[1] = b;                                             /* decay: the wall feedback (spu_derive) */
            f->pt[2] = c < 0.5f ? onepole_k(20000.0f * fm_exp2f(-(0.5f - c) * 9.29f)) : 1.0f;   /* tone: the plate's */
            f->pt[5] = c > 0.5f ? onepole_k(20.0f * fm_exp2f((c - 0.5f) * 13.29f)) : 0.0f;
            f->pt[3] = d;                                             /* damp: vIIR closes */
            f->pt[4] = 0.0f;
            break;
        }
        f->pt[0] = 0.5f * fm_exp2f(a * 1.926f);                       /* size: the combs x 0.5 .. 1.9 */
        f->pt[1] = 24.0f * b * b;                                     /* motion, samples of wander */
        /* tone: below the centre a low-pass closes to 800 Hz, above it a high-pass opens to 2 kHz */
        f->pt[2] = c < 0.5f ? onepole_k(20000.0f * fm_exp2f(-(0.5f - c) * 9.29f)) : 1.0f;
        f->pt[5] = c > 0.5f ? onepole_k(20.0f * fm_exp2f((c - 0.5f) * 13.29f)) : 0.0f;
        f->pt[3] = 0.8f * d;                                          /* damp: the combs' damping */
        f->pt[4] = 0.55f + 0.42f * a;                                 /* the loop's feedback: decay with size */
        break;
    default:
        break;
    }
}

void fx_init(fxslot_t *f, int16_t *dly_l, int16_t *dly_r, int16_t *rv)
{
    uint32_t i;
    uint8_t *z = (uint8_t *)f;
    for (i = 0; i < sizeof *f; i++)
        z[i] = 0;
    f->dl = dly_l;
    f->dr = dly_r;
    f->rv = rv;
    f->bpm = 120.0f;
    f->lpv = 127;
    f->hpc = onepole_k(20.0f);
    f->lpc = onepole_k(20000.0f);
    if (dly_l) zero16(dly_l, FX_DLY_LEN);
    if (dly_r) zero16(dly_r, FX_DLY_LEN);
    if (rv) zero16(rv, FX_RV_LEN);
    for (i = 0; i < 4; i++)
        f->knob[i] = FXT_DEFAULT[FXT_CHORUS][i];
    f->type = f->type_t = FXT_CHORUS;
    f->mode = f->mode_t = RV_PLATE;
    fx_derive(f);
    fx_states(f);
}

int fx_has(const fxslot_t *f, int pick)
{
    int type = fx_pick_type(pick);
    if (pick < 0 || pick >= NFXPICK)
        return 0;
    if (type == FXT_REVERB)
        return f->rv != 0;
    if (type == FXT_DELAY)
        return f->dl != 0 && f->dr != 0;
    return 1;
}

void fx_set(fxslot_t *f, int pick, const uint8_t knob[4])
{
    int i, type, mode;
    if (!fx_has(f, pick))
        pick = FXT_CHORUS;
    type = fx_pick_type(pick);
    mode = fx_pick_mode(pick);
    for (i = 0; i < 4; i++)
        f->knob[i] = knob[i] > 127 ? 127 : knob[i];
    if (type != f->type_t) {                  /* the lines the new effect reads are idle now: clear them */
        if (type == FXT_DELAY) {
            zero16(f->dl, FX_DLY_LEN);
            zero16(f->dr, FX_DLY_LEN);
        } else if (type == FXT_REVERB) {
            zero16(f->rv, FX_RV_LEN);
        }
    }
    /* another reverb mode while the reverb runs: its line is in use, so the interrupt clears it
     * (fx_states: spu_reset, a slice a block) when it takes the new mode at its next block */
    f->mode_t = (uint8_t)mode;
    f->type_t = (uint8_t)type;
    fx_derive(f);
}

void fx_tempo(fxslot_t *f, float bpm)
{
    if (bpm > 20.0f && bpm != f->bpm) {
        f->bpm = bpm;
        if (f->type_t == FXT_DELAY)
            delay_time(f);
    }
}

void fx_filter(fxslot_t *f, int hp, int lp)
{
    f->hpv = (uint8_t)(hp < 0 ? 0 : hp > 127 ? 127 : hp);
    f->lpv = (uint8_t)(lp < 0 ? 0 : lp > 127 ? 127 : lp);
    f->hpc = onepole_k(20.0f * fm_exp2f(k127(hp) * 8.64f));            /* 20 Hz .. 8 kHz */
    f->lpc = onepole_k(20000.0f * fm_exp2f(-(1.0f - k127(lp)) * 8.0f)); /* 78 Hz .. 20 kHz */
}

/* ---- the effects: a block of m samples of x[] (the filtered send) into out (added) ---- */
static inline float cho_tap(const fxslot_t *f, float p)      /* the short line, read between samples */
{
    int32_t i0 = (int32_t)p;
    float a = s16f(f->cho[i0 & (CHO_N - 1)]);
    return a + (p - (float)i0) * (s16f(f->cho[(i0 + 1) & (CHO_N - 1)]) - a);
}

/* lag (fx_process_lag): the send arrives that many samples late; the effects with a line take it back by writing
 * the send that far into the line's past (a slot not read yet: every tap reads further back), so their output is
 * where it would have been, but for the line's rounding: the delay and the chorus add it to the feedback already
 * there (one int16 rounding more, and the delay's soft clip on the sum read back), the plate's combs likewise (its
 * 12 bits) */
static inline int16_t add16(int16_t a, float x)                /* a + x on the int16 line (x in its units) */
{
    float v = (float)a + x * 16384.0f;
    return (int16_t)(v > 32767.0f ? 32767.0f : v < -32767.0f ? -32767.0f : v);
}
static void do_chorus(fxslot_t *f, const float *x, float *ol, float *orr, int m, int lag)
{
    const float inc = f->pc[0] / SR, range = f->pc[1], fb = f->pc[2], side = 1.0f - 2.0f * f->pc[3];
    const float centre = 15.0f * (SR / 1000.0f), slew = 1.0f - fm_expf(-1.0f / (0.070f * SR));
    const float hpk = 0.028f;                 /* ~200 Hz: the wet without its lows */
    float ph = f->cph, s1 = f->cs1, s2 = f->cs2, hl = f->chl, hr = f->chr, fz = f->cfbz;
    int32_t w = f->cw;
    int i;
    for (i = 0; i < m; i++) {
        float tri, st, pl, pr, wl, wr;
        if (lag) {
            f->cho[w] = f16(fz * fb);
            f->cho[(w - lag) & (CHO_N - 1)] = add16(f->cho[(w - lag) & (CHO_N - 1)], x[i]);
        } else {
            f->cho[w] = f16(x[i] + fz * fb);
        }
        w = (w + 1) & (CHO_N - 1);
        tri = 2.0f * fm_fabsf(2.0f * ph - 1.0f) - 1.0f;     /* the JP's stepped triangle */
        ph += inc;
        if (ph >= 1.0f)
            ph -= 1.0f;
        st = fm_floorf(tri * 4.0f + 0.5f) * 0.25f;
        s1 += (st - s1) * slew;
        s2 += (s1 - s2) * slew;
        pl = (float)w - (centre + s2 * range);
        pr = (float)w - (centre + s2 * range * side);
        if (pl < 0.0f) pl += CHO_N;
        if (pr < 0.0f) pr += CHO_N;
        wl = cho_tap(f, pl);
        wr = cho_tap(f, pr);
        fz = wl;
        hl += (wl - hl) * hpk;
        hr += (wr - hr) * hpk;
        ol[i] += (wl - hl) * 0.85f;
        orr[i] += (wr - hr) * 0.85f;
    }
    f->cph = ph; f->cs1 = s1; f->cs2 = s2; f->chl = fm_flush(hl); f->chr = fm_flush(hr); f->cfbz = fz;
    f->cw = w;
}

static inline float tap16(const int16_t *b, float rp)
{
    int32_t i0 = (int32_t)rp, i1;
    float fr = rp - (float)i0, a, c;
    if (i0 >= FX_DLY_LEN) i0 -= FX_DLY_LEN;
    i1 = i0 + 1 >= FX_DLY_LEN ? 0 : i0 + 1;
    a = s16f(b[i0]);
    c = s16f(b[i1]);
    return a + (c - a) * fr;
}

static void do_delay(fxslot_t *f, const float *x, float *ol, float *orr, int m, int any, int lag)
{
    const float target = f->pt[0], fb = f->pc[2], tc = f->pc[3];
    float dcur = f->dcur, lpl = f->dlpl, lpr = f->dlpr;
    int32_t w = f->dw, quiet = f->dquiet;
    int i;
    if (!any && quiet >= FX_DLY_LEN) {        /* silent in, an empty line: time moves on, nothing to read */
        f->dw = (w + m) % FX_DLY_LEN;
        f->dcur += (target - dcur) * (1.0f - fm_expf(-0.0008f * (float)m));
        return;
    }
    for (i = 0; i < m; i++) {
        float rp, tl, tr, a, b;
        dcur += (target - dcur) * 0.0008f;
        rp = (float)w - dcur;
        if (rp < 0.0f)
            rp += (float)FX_DLY_LEN;
        tl = tap16(f->dl, rp);
        tr = tap16(f->dr, rp);
        lpl += tc * (tl - lpl);               /* tone: the repeats darken */
        lpr += tc * (tr - lpr);
        a = (lag ? 0.0f : x[i]) + lpr * fb;   /* ping-pong: the send and the right into the left, */
        b = lpl * fb;                         /* the left into the right */
        if (a > 1.0f || a < -1.0f) a = fm_tanhf(a);
        if (b > 1.0f || b < -1.0f) b = fm_tanhf(b);
        {
            int16_t ka = f16(a), kb = f16(b);
            f->dl[w] = ka;
            f->dr[w] = kb;
            if (lag) {                        /* the send into the line's past (not read yet: the time is longer) */
                int32_t p = w - lag < 0 ? w - lag + FX_DLY_LEN : w - lag;
                float v = s16f(f->dl[p]) + x[i];
                if (v > 1.0f || v < -1.0f) v = fm_tanhf(v);
                f->dl[p] = f16(v);
            }
            quiet = (ka == 0 && kb == 0 && x[i] == 0.0f) ? quiet + 1 : 0;
        }
        if (++w >= FX_DLY_LEN)
            w = 0;
        ol[i] += tl;
        orr[i] += tr;
    }
    f->dw = w;
    f->dcur = dcur;
    f->dlpl = fm_flush(lpl);
    f->dlpr = fm_flush(lpr);
    f->dquiet = quiet;
}

static void do_lofi(fxslot_t *f, const float *x, float *ol, float *orr, int m)
{
    const float q = f->pc[1], iq = f->pc[5], qual = f->pc[2], aak = f->pt[4], step = f->pc[0];
    float ph = f->lph, hold = f->lhold, in1 = f->lin1, out1 = f->lout1, late;
    int32_t w = f->cw;
    int i;
    f->lwph += 0.3f * (float)m / SR;          /* the spread wanders: a 0.3 Hz wow of the right's lateness */
    if (f->lwph >= 1.0f)
        f->lwph -= 1.0f;
    late = f->pc[3] * (1.0f + 0.04f * fm_sin_turns(f->lwph));
    for (i = 0; i < m; i++) {
        float xf, y, r;
        in1 += aak * (x[i] - in1);
        xf = x[i] + qual * (in1 - x[i]);      /* smooth: how much is band-limited before the hold */
        ph += step;
        if (ph >= 1.0f) {
            ph -= 1.0f;
            hold = fm_floorf(xf * q + 0.5f) * iq;
        }
        out1 += aak * (hold - out1);
        y = hold + qual * (out1 - hold);
        f->cho[w] = f16(y);                   /* the right side reads the left back, late */
        if (late < 1.0f) {
            r = y;
        } else {
            float rp = (float)w - late;
            if (rp < 0.0f)
                rp += CHO_N;
            r = cho_tap(f, rp);
        }
        w = (w + 1) & (CHO_N - 1);
        ol[i] += y;
        orr[i] += r;
    }
    f->lph = ph; f->lhold = hold; f->lin1 = fm_flush(in1); f->lout1 = fm_flush(out1);
    f->cw = w;
}

static inline float ap_coef(float hz)
{
    float t = fm_tanf(FM_PI * fm_clampf(hz, 20.0f, 18000.0f) / SR);
    return (t - 1.0f) / (t + 1.0f);
}

static void do_phaser(fxslot_t *f, const float *x, float *ol, float *orr, int m)
{
    const float centre = f->pc[0], depth = f->pc[1], fb = f->pc[3];
    float al_t, ar_t, al = f->pal, ar = f->par_, dal, dar, zl = f->pzl, zr = f->pzr, im = 1.0f / (float)m;
    int i, s;
    f->pph += f->pc[2] * (float)m / SR;
    if (f->pph >= 1.0f)
        f->pph -= 1.0f;
    al_t = ap_coef(centre * fm_exp2f(depth * fm_sin_turns(f->pph)));
    ar_t = ap_coef(centre * fm_exp2f(depth * fm_sin_turns(f->pph + 0.25f)));
    dal = (al_t - al) * im;
    dar = (ar_t - ar) * im;
    for (i = 0; i < m; i++) {
        float xl = x[i] + zl * fb, xr = x[i] + zr * fb;
        al += dal;
        ar += dar;
        for (s = 0; s < 4; s++) {
            float yl = al * xl + f->pxl[s] - al * f->pyl[s], yr = ar * xr + f->pxr[s] - ar * f->pyr[s];
            f->pxl[s] = xl; f->pyl[s] = yl; xl = yl;
            f->pxr[s] = xr; f->pyr[s] = yr; xr = yr;
        }
        zl = fm_flush(xl);
        zr = fm_flush(xr);
        ol[i] += 0.5f * (x[i] + xl);
        orr[i] += 0.5f * (x[i] + xr);
    }
    f->pal = al; f->par_ = ar; f->pzl = zl; f->pzr = zr;
}

static void do_reverb(fxslot_t *f, const float *x, float *ol, float *orr, int m, int any, int lag)
{
    const float damp = f->pc[3], fb = f->pc[4], hpk = 0.017f;                          /* ~120 Hz on the send */
    const float tlk = f->pc[2], thk = f->pc[5];                                        /* the tone's filters */
    float cdd[4], cd[4], cdmp[4], hp = f->rhp, tl0 = f->rtl[0], tl1 = f->rtl[1], th0 = f->rtr[0], th1 = f->rtr[1];
    int16_t *cb[4], *ab[4];
    int32_t cp[4], ap[4], quiet = f->rquiet;
    int c, i;
    if (!any && quiet >= FX_RV_LEN)
        return;
    f->rph += RV_MOD_HZ * (float)m / SR;
    if (f->rph >= 1.0f)
        f->rph -= 1.0f;
    cb[0] = f->rv;
    for (c = 0; c < 4; c++) {                 /* where each comb's tap wants to be: size, then the wander */
        float d = f->pc[0] * (float)RV_COMB[c] + f->pc[1] * fm_sin_turns(f->rph + 0.25f * (float)c);
        d = fm_clampf(d, 8.0f, (float)(RV_CAP[c] - 3));
        cd[c] = f->cd[c];
        cdd[c] = (d - cd[c]) / (float)m;
        cdmp[c] = f->cdmp[c];
        cp[c] = f->cpos[c];
        ap[c] = f->apos[c];
        if (c)
            cb[c] = cb[c - 1] + RV_CAP[c - 1];
    }
    ab[0] = f->rv + RV_AP0;
    ab[1] = ab[0] + RV_AP[0];
    ab[2] = ab[1] + RV_AP[1];
    ab[3] = ab[2] + RV_AP[2];
    {   /* the four combs and four allpasses unrolled, every state a local (the same operations, in the same order) */
        int16_t *const b0 = cb[0], *const b1 = cb[1], *const b2 = cb[2], *const b3 = cb[3];
        int16_t *const a0 = ab[0], *const a1 = ab[1], *const a2 = ab[2], *const a3 = ab[3];
        float cd0 = cd[0], cd1 = cd[1], cd2 = cd[2], cd3 = cd[3];
        const float dd0 = cdd[0], dd1 = cdd[1], dd2 = cdd[2], dd3 = cdd[3];
        float m0 = cdmp[0], m1 = cdmp[1], m2 = cdmp[2], m3 = cdmp[3];
        int32_t p0 = cp[0], p1 = cp[1], p2 = cp[2], p3 = cp[3], q0 = ap[0], q1 = ap[1], q2 = ap[2], q3 = ap[3];
#define LAG lag
#define RV_COMB_STEP(B, CAP, CD, DD, MD, P, NEG)                                              \
        do {                                                                                   \
            float rp_, a_, bb_, v_;                                                           \
            int32_t i0_, i1_, k_;                                                             \
            CD += DD;                                                                          \
            rp_ = (float)P - CD;                                                               \
            if (rp_ < 0.0f)                                                                    \
                rp_ += (float)(CAP);                                                           \
            i0_ = (int32_t)rp_;                                                                \
            i1_ = i0_ + 1 >= (CAP) ? 0 : i0_ + 1;                                             \
            a_ = (float)B[i0_];                                                                \
            bb_ = (float)B[i1_];                                                               \
            v_ = (a_ + (bb_ - a_) * (rp_ - (float)i0_)) * (1.0f / 2048.0f);                   \
            acc += v_;                                                                         \
            accr += NEG ? -v_ : v_;                                                            \
            MD = v_ + (MD - v_) * damp;                                                        \
            if (LAG) {                        /* the send into the comb's past, its feedback here */ \
                int32_t q_ = P - LAG < 0 ? P - LAG + (CAP) : P - LAG, j_;                     \
                k_ = (int32_t)(MD * fb * 2048.0f);                                             \
                j_ = (int32_t)B[q_] + (int32_t)(y * 2048.0f);                                  \
                j_ = j_ > 32767 ? 32767 : j_ < -32767 ? -32767 : j_;                           \
                B[q_] = (int16_t)j_;                                                           \
                wr |= j_;                                                                      \
            } else {                                                                           \
                k_ = (int32_t)((y + MD * fb) * 2048.0f);   /* 12 bits in the loop, as 9W9 */   \
            }                                                                                  \
            if (k_ > 32767) k_ = 32767;                                                        \
            if (k_ < -32767) k_ = -32767;                                                      \
            B[P] = (int16_t)k_;                                                                \
            wr |= k_;                                                                          \
            P = P + 1 >= (CAP) ? 0 : P + 1;                                                    \
        } while (0)
#define RV_AP_STEP(B, LEN, Q, S)                                                              \
        do {                                                                                   \
            float bo_ = (float)B[Q] * (1.0f / 2048.0f), nv_ = (S + bo_ * 0.5f) * 2048.0f;     \
            B[Q] = (int16_t)fm_clampf(nv_, -32767.0f, 32767.0f);                              \
            S = bo_ - S * 0.5f;                                                                \
            Q = Q + 1 >= (LEN) ? 0 : Q + 1;                                                    \
        } while (0)
        for (i = 0; i < m; i++) {
            float acc = 0.0f, accr = 0.0f, y, yr;
            int32_t wr = 0;
            hp += hpk * (x[i] - hp);
            y = x[i] - hp;
            RV_COMB_STEP(b0, RV_CAP[0], cd0, dd0, m0, p0, 0);
            RV_COMB_STEP(b1, RV_CAP[1], cd1, dd1, m1, p1, 1);
            RV_COMB_STEP(b2, RV_CAP[2], cd2, dd2, m2, p2, 0);
            RV_COMB_STEP(b3, RV_CAP[3], cd3, dd3, m3, p3, 1);
            y = acc * 0.25f;
            yr = accr * 0.25f;
            RV_AP_STEP(a0, RV_AP[0], q0, y);       /* two allpasses a side, int16 */
            RV_AP_STEP(a1, RV_AP[1], q1, y);
            RV_AP_STEP(a2, RV_AP[2], q2, yr);
            RV_AP_STEP(a3, RV_AP[3], q3, yr);
            tl0 += tlk * (y - tl0);           /* the tone: a low-pass (k = 1 passes), then a high-pass (k = 0 passes) */
            tl1 += tlk * (yr - tl1);
            th0 += thk * (tl0 - th0);
            th1 += thk * (tl1 - th1);
            ol[i] += (tl0 - th0) * RV_WET;
            orr[i] += (tl1 - th1) * RV_WET;
            quiet = (wr == 0 && x[i] == 0.0f) ? quiet + 1 : 0;
        }
#undef RV_COMB_STEP
#undef RV_AP_STEP
#undef LAG
        cd[0] = cd0; cd[1] = cd1; cd[2] = cd2; cd[3] = cd3;
        cdmp[0] = m0; cdmp[1] = m1; cdmp[2] = m2; cdmp[3] = m3;
        cp[0] = p0; cp[1] = p1; cp[2] = p2; cp[3] = p3;
        ap[0] = q0; ap[1] = q1; ap[2] = q2; ap[3] = q3;
    }
    for (c = 0; c < 4; c++) {
        f->cd[c] = cd[c];
        f->cdmp[c] = fm_flush(cdmp[c]);
        f->cpos[c] = cp[c];
        f->apos[c] = ap[c];
    }
    f->rhp = fm_flush(hp);
    f->rtl[0] = fm_flush(tl0); f->rtl[1] = fm_flush(tl1);
    f->rtr[0] = fm_flush(th0); f->rtr[1] = fm_flush(th1);
    f->rquiet = quiet;
}

/* an SPU mode: the send decimated to 22.05 kHz, a tick of the console's reverb every second sample, its two
 * sides interpolated back; the plate's tone on the return. Its addresses follow the gliding size a block */
static void do_spu(fxslot_t *f, const float *x, float *ol, float *orr, int m, int any)
{
    spu_t *s = &f->spu;
    int16_t *w = f->rv;
    const float tlk = f->pc[2], thk = f->pc[5], g = -SPU_GAIN[f->mode - 1];
    float tl = s->tl, th = s->th, tr = s->tr, thr = s->thr;
    int i;
    if (s->clear > 0) {                       /* a new mode: the work area emptied first, silent meanwhile */
        int32_t k = s->clear > SPU_CLEAR ? SPU_CLEAR : s->clear;
        zero16(w + (FX_RV_LEN - s->clear), k);
        s->clear -= k;
        return;
    }
    if (!any && s->quiet >= FX_RV_LEN * 2)    /* silent in, an empty area: nothing to read */
        return;
    spu_derive(s, f->mode, f->pc[0], f->pc[1], f->pc[3]);
    for (i = 0; i < m; i++) {
        float yl, yr;
        s->in[s->ip] = s->in[s->ip + 64u] = x[i];
        s->ip = (uint8_t)((s->ip + 1u) & 63u);
        if (s->phase) {                       /* the second of a pair: a tick */
            float lo, ro;
            int wr = spu_tick(s, w, -spu_decimate(s), &lo, &ro);   /* vLIN = 8000h: -1 */
            s->quiet = (wr == 0 && !any) ? s->quiet + 2 : 0;
            s->up[0][s->up_w] = s->up[0][s->up_w + 32u] = lo;
            s->up[1][s->up_w] = s->up[1][s->up_w + 32u] = ro;
            s->up_w = (uint8_t)((s->up_w + 1u) & 31u);
            spu_interp(s->up[0], s->up_w, &s->out[0][0], &s->out[0][1]);
            spu_interp(s->up[1], s->up_w, &s->out[1][0], &s->out[1][1]);
        }
        yl = s->out[0][s->phase] * g;           /* the output volume: -1 again, and the level */
        yr = s->out[1][s->phase] * g;
        s->phase ^= 1u;
        tl += tlk * (yl - tl);
        tr += tlk * (yr - tr);
        th += thk * (tl - th);
        thr += thk * (tr - thr);
        ol[i] += tl - th;
        orr[i] += tr - thr;
    }
    s->tl = fm_flush(tl);
    s->th = fm_flush(th);
    s->tr = fm_flush(tr);
    s->thr = fm_flush(thr);
}

/* lush: the send low-passed and decimated to 22.05 kHz (a tick every second sample, on the pair's mean), Dattorro's
 * tank, its seven taps a side, then back to 44.1 kHz linearly (half a tick late) and the plate's tone. A lag is more
 * pre-delay (as the SPU's). The lengths follow the gliding size a block (whole samples); the two tank allpasses wander
 * +-12 samples (x size) on a 0.7 Hz sine, a quarter turn apart */
#define LU_RD(l, k) spu_rd(w, b, LU_OFF[l] + (k))
#define LU_WR(l, v) (wz |= spu_wr(w, b, LU_OFF[l], (v)))
#define LU_AP(l, g, x)                                                                        \
    do {                                                                                       \
        float d_ = LU_RD(l, u->len[l]), v_ = (x) - (g) * d_;                                  \
        LU_WR(l, v_);                                                                          \
        (x) = d_ + (g) * v_;                                                                   \
    } while (0)
static inline float lu_rdf(const int16_t *w, uint32_t b, int32_t off, float p)   /* a read between samples */
{
    int32_t i = (int32_t)p;
    float fr = p - (float)i, a0 = spu_rd(w, b, off + i), a1 = spu_rd(w, b, off + i + 1);
    return a0 + fr * (a1 - a0);
}
static void do_lush(fxslot_t *f, const float *x, float *ol, float *orr, int m, int any)
{
    lush_t *u = &f->lu;
    int16_t *w = f->rv;
    const float sc = f->pc[0] * (22050.0f / 29761.0f), dec = f->pc[1], damp = f->pc[3], tlk = f->pc[2], thk = f->pc[5];
    const float lpk = 0.55f, exc = 12.0f * f->pc[0];      /* (lpk: the send's low-pass, ~5.6 kHz before decimation) */
    float tl = u->tl, th = u->th, tr = u->tr, thr = u->thr;
    int i, l;
    if (u->clear > 0) {                       /* a new mode: the line emptied first, silent meanwhile */
        int32_t k = u->clear > SPU_CLEAR ? SPU_CLEAR : u->clear;
        zero16(w + (FX_RV_LEN - u->clear), k);
        u->clear -= k;
        return;
    }
    if (!any && u->quiet >= FX_RV_LEN * 2)    /* silent in, an empty line: nothing to read */
        return;
    if (sc != u->scale) {
        for (l = LU_IN1; l < NLU; l++)
            u->len[l] = (int32_t)(LU_LEN[l] * sc);
        u->scale = sc;
    }
    u->len[LU_PRE] = (int32_t)f->pc[4];
    for (i = 0; i < m; i++) {
        float yl, yr;
        u->lp += lpk * (x[i] - u->lp);
        if (!u->phase) {
            u->hold = u->lp;
        } else {                              /* a tick */
            const uint32_t b = u->base;
            int32_t wz = 0;
            float s = 0.5f * (u->hold + u->lp), a, c, mo, l0, r0;
            u->lfo += 0.7f / 22050.0f;
            if (u->lfo >= 1.0f)
                u->lfo -= 1.0f;
            LU_WR(LU_PRE, s);
            s = LU_RD(LU_PRE, u->len[LU_PRE]);
            LU_AP(LU_IN1, 0.75f, s);
            LU_AP(LU_IN2, 0.75f, s);
            LU_AP(LU_IN3, 0.625f, s);
            LU_AP(LU_IN4, 0.625f, s);
            /* the left half: the right's end in */
            a = s + dec * u->fb[1];
            mo = exc * fm_sin_turns(u->lfo);
            {
                float d = lu_rdf(w, b, LU_OFF[LU_MAPL], (float)u->len[LU_MAPL] + mo), v = a + 0.7f * d;
                LU_WR(LU_MAPL, v);
                a = d - 0.7f * v;
            }
            LU_WR(LU_DL1, a);
            a = LU_RD(LU_DL1, u->len[LU_DL1]);
            u->dmp[0] += (1.0f - damp) * (a - u->dmp[0]);
            a = u->dmp[0] * dec;
            LU_AP(LU_APL, 0.5f, a);
            LU_WR(LU_DL2, a);
            /* the right half: the left's end in */
            c = s + dec * u->fb[0];
            mo = exc * fm_sin_turns(u->lfo + 0.25f);
            {
                float d = lu_rdf(w, b, LU_OFF[LU_MAPR], (float)u->len[LU_MAPR] + mo), v = c + 0.7f * d;
                LU_WR(LU_MAPR, v);
                c = d - 0.7f * v;
            }
            LU_WR(LU_DR1, c);
            c = LU_RD(LU_DR1, u->len[LU_DR1]);
            u->dmp[1] += (1.0f - damp) * (c - u->dmp[1]);
            c = u->dmp[1] * dec;
            LU_AP(LU_APR, 0.5f, c);
            LU_WR(LU_DR2, c);
            u->fb[0] = LU_RD(LU_DL2, u->len[LU_DL2]);
            u->fb[1] = LU_RD(LU_DR2, u->len[LU_DR2]);
            /* Dattorro's taps (his sample counts, scaled) */
#define T(k) ((int32_t)((float)(k) * sc))
            l0 = LU_RD(LU_DR1, T(266)) + LU_RD(LU_DR1, T(2974)) - LU_RD(LU_APR, T(1913)) + LU_RD(LU_DR2, T(1996))
                 - LU_RD(LU_DL1, T(1990)) - LU_RD(LU_APL, T(187)) - LU_RD(LU_DL2, T(1066));
            r0 = LU_RD(LU_DL1, T(353)) + LU_RD(LU_DL1, T(3627)) - LU_RD(LU_APL, T(1228)) + LU_RD(LU_DL2, T(2673))
                 - LU_RD(LU_DR1, T(2111)) - LU_RD(LU_APR, T(335)) - LU_RD(LU_DR2, T(121));
#undef T
            u->prev[0] = u->cur[0];
            u->prev[1] = u->cur[1];
            u->cur[0] = l0 * 0.45f;                 /* (0.6 was a little loud beside the dry: Charles) */
            u->cur[1] = r0 * 0.45f;
            u->base = b - 1u;
            u->quiet = (wz == 0 && !any) ? u->quiet + 2 : 0;
        }
        yl = u->phase ? 0.5f * (u->prev[0] + u->cur[0]) : u->cur[0];
        yr = u->phase ? 0.5f * (u->prev[1] + u->cur[1]) : u->cur[1];
        u->phase ^= 1u;
        tl += tlk * (yl - tl);
        tr += tlk * (yr - tr);
        th += thk * (tl - th);
        thr += thk * (tr - thr);
        ol[i] += (tl - th) * RV_WET;
        orr[i] += (tr - thr) * RV_WET;
    }
    u->lp = fm_flush(u->lp);
    u->dmp[0] = fm_flush(u->dmp[0]);
    u->dmp[1] = fm_flush(u->dmp[1]);
    u->tl = fm_flush(tl);
    u->th = fm_flush(th);
    u->tr = fm_flush(tr);
    u->thr = fm_flush(thr);
}
#undef LU_RD
#undef LU_WR
#undef LU_AP

int fx_lags(const fxslot_t *f) { return f->type_t == FXT_CHORUS || f->type_t == FXT_DELAY || f->type_t == FXT_REVERB; }

void fx_process(fxslot_t *f, const float *in, float *out_l, float *out_r, uint32_t n)
{
    fx_process_lag(f, in, out_l, out_r, n, 0);
}
void fx_process_lag(fxslot_t *f, const float *in, float *out_l, float *out_r, uint32_t n, int lag)
{
    uint32_t o;
    if (f->type_t != f->type || f->mode_t != f->mode) {   /* a new effect or mode: start it at rest */
        f->type = f->type_t;
        f->mode = f->mode_t;
        fx_states(f);
    }
    for (o = 0; o < n; o += SUB) {
        float x[SUB];
        int m = (int)(n - o < SUB ? n - o : SUB), i, any = 0;
        const int hp_on = f->hpv > 0, lp_on = f->lpv < 127;
        float hps = f->hps, lps = f->lps;
        const float hpc = f->hpc, lpc = f->lpc;
        for (i = 0; i < 6; i++)
            f->pc[i] += (f->pt[i] - f->pc[i]) * GLIDE;
        for (i = 0; i < m; i++) {
            float v = in[o + i];
            any |= v != 0.0f;
            if (hp_on) {
                hps += hpc * (v - hps);
                v -= hps;
            }
            if (lp_on) {
                lps += lpc * (v - lps);
                v = lps;
            }
            x[i] = v;
        }
        f->hps = fm_flush(hps);
        f->lps = fm_flush(lps);
        switch (f->type) {
        case FXT_CHORUS: do_chorus(f, x, out_l + o, out_r + o, m, lag); break;
        case FXT_DELAY: do_delay(f, x, out_l + o, out_r + o, m, any, lag); break;
        case FXT_LOFI: do_lofi(f, x, out_l + o, out_r + o, m); break;
        case FXT_PHASER: do_phaser(f, x, out_l + o, out_r + o, m); break;
        case FXT_REVERB:
            if (f->mode == RV_PLATE)
                do_reverb(f, x, out_l + o, out_r + o, m, any, lag);
            else if (f->mode == RV_LUSH)
                do_lush(f, x, out_l + o, out_r + o, m, any);
            else
                do_spu(f, x, out_l + o, out_r + o, m, any);
            break;
        default: break;
        }
    }
}

int fx_awake(const fxslot_t *f)
{
    switch (f->type) {
    case FXT_DELAY: return f->dquiet < FX_DLY_LEN;
    case FXT_REVERB:
        return f->mode == RV_PLATE ? f->rquiet < FX_RV_LEN
             : f->mode == RV_LUSH ? f->lu.clear > 0 || f->lu.quiet < FX_RV_LEN * 2
             : f->spu.clear > 0 || f->spu.quiet < FX_RV_LEN * 2;
    case FXT_CHORUS: case FXT_LOFI: case FXT_PHASER: return 1;
    default: return 0;
    }
}

/* ================================================================ master === */
/* the master's knob names, short enough for the knob strip: percussion, melodic, compressor
 * and pump are perc, mel, comp, pump; then the out page's list */
const char *const MC_NAME[NMC] = {"off", "tape", "sampler"};
const char *const MS_NAME[NMS] = {"low", "mid", "high", "-", "color", "amount", "tone", "mix",
                                  "drums", "melodic", "comp", "pump", "pump by", "release", "accent", "level"};

typedef struct { float b0, b1, b2, a1, a2; } biq_t;
typedef struct { float x1, x2, y1, y2; } biqs_t;

struct master {
    uint8_t pot[NMS];
    float gp, gm, glev;                       /* the group and master levels (targets) */
    float gp_c, gm_c, lev_c;                  /* gliding */
    /* EQ */
    biq_t eq[3];
    biqs_t eqs[2][3];
    uint8_t eq_on, comp_on;
    /* compressor (X0X's master.c) */
    float thr_db, slope, knee, a_att, a_rel, makeup, gr, ms, a_det, want, g_lin, g_step, gr_view;
    uint8_t cr;
    /* PUMP (X0X's): its depth (dB), its envelope (dB) and the target it rises to; the rise and the release
     * as coefficients of MST_CR samples (it moves where the gain is computed) */
    float pump_db, pump, pump_tgt, a_up, a_down, pump_view;
    float pg, pg_step;                /* its gain on the groups, ramped as a linear gain over MST_CR samples */
    uint8_t pcr, duck_mel;            /* (the countdown); duck_mel: a kit's hit keyed it, the melodic group ducks alone */
#define MST_CR 4
    /* limiter (X0X's) */
#define MST_LA 64
    float env, gain, inv_env;
    float la[MST_LA], la_r[MST_LA];
    int32_t la_pos, hold;
    /* color (fx.h): the type running, its numbers (master_set), its states; the tape's wow line, a side */
#define MC_LINE 128
#define MC_DELAY 64.0f                        /* the wow's centre, samples (1.5 ms: the mix that late while tape is on) */
    uint8_t col;
    float c_g, c_mk, c_lp, c_dep, c_mix, c_step, c_pre, c_q, c_iq;
    float c_z[2][2], c_ph, c_fph, c_sh[2], c_sp;
    int16_t c_line[2][MC_LINE];
    int32_t c_pos;
};
_Static_assert(sizeof(struct master) <= MASTER_BYTES, "master_t does not fit MASTER_BYTES");

/* the bus's headroom: the groups enter it 8 dB down, so a project's chords and its kit peak a few dB under full scale
 * before the EQ, the color, the compressor and the limiter (at unity they peaked at +2 .. +9 dBFS: the limiter always
 * working, the organs audibly squashed; 2026-10-19). The drums and the synths keep their balance; the FM-1's volume
 * makes up the level */
#ifndef MST_HEADROOM                          /* (tests/host/fx_test.sh: 1, the master's mechanics at unity) */
#define MST_HEADROOM 0.4f
#endif
static float level_lin(int v)                 /* 0 .. 100 .. 127: silence, unity (exactly), 2x */
{
    float x = (float)(v < 0 ? 0 : v > 127 ? 127 : v);
    return x <= 100.0f ? x * x * 0.0001f : 1.0f + (x - 100.0f) * (1.0f / 27.0f);
}
static float coef_ms(float t) { return fm_expf(-1000.0f / (t * SR)); }
float master_pump_ms(int v) { return 10.0f * fm_exp2f((float)v * (7.229f / 127.0f)); }   /* X0X's release: 10 .. 1500 */
int master_max(int p) { return p == MS_PUMP_BY ? NPB - 1 : p == MS_PUMP_ACC ? 1 : p == MS_CTYPE ? NMC - 1 : 127; }
/* PUMP's duck rises over this (X0X's MASTER_PUMP_RISE_MS): 12 ms lets the kick's attack through and then ducks;
 * at 3 ms X0X heard a click on every kick */
#define MST_PUMP_RISE_MS 12.0f

/* RBJ: a low shelf, a peak, a high shelf, gain in dB */
static void biq_shelf(biq_t *q, float hz, float db, int high)
{
    float A = fm_exp2f(db * (3.3219281f / 40.0f)), w = FM_TWO_PI * hz / SR, cs = fm_cosf(w), sn = fm_sinf(w);
    float al = sn * 0.5f * 1.4142136f, sa = 2.0f * fm_sqrtf(A) * al, ia, c = high ? -cs : cs;
    float a0 = (A + 1.0f) + (A - 1.0f) * c + sa;
    ia = 1.0f / a0;
    q->b0 = A * ((A + 1.0f) - (A - 1.0f) * c + sa) * ia;
    q->b1 = (high ? -2.0f : 2.0f) * A * ((A - 1.0f) - (A + 1.0f) * c) * ia;
    q->b2 = A * ((A + 1.0f) - (A - 1.0f) * c - sa) * ia;
    q->a1 = (high ? 2.0f : -2.0f) * ((A - 1.0f) + (A + 1.0f) * c) * ia;
    q->a2 = ((A + 1.0f) + (A - 1.0f) * c - sa) * ia;
}
static void biq_peak(biq_t *q, float hz, float db, float Q)
{
    float A = fm_exp2f(db * (3.3219281f / 40.0f)), w = FM_TWO_PI * hz / SR, cs = fm_cosf(w), sn = fm_sinf(w);
    float al = sn / (2.0f * Q), ia = 1.0f / (1.0f + al / A);
    q->b0 = (1.0f + al * A) * ia;
    q->b1 = -2.0f * cs * ia;
    q->b2 = (1.0f - al * A) * ia;
    q->a1 = -2.0f * cs * ia;
    q->a2 = (1.0f - al / A) * ia;
}

static void eq_coefs(master_t *m)
{
    float lo = ((float)m->pot[MS_EQ_LO] - 64.0f) * (12.0f / 64.0f);
    float mid = ((float)m->pot[MS_EQ_MID] - 64.0f) * (12.0f / 64.0f);
    float hi = ((float)m->pot[MS_EQ_HI] - 64.0f) * (12.0f / 64.0f);
    biq_shelf(&m->eq[0], 120.0f, lo, 0);
    biq_peak(&m->eq[1], 1000.0f, mid, 0.8f);
    biq_shelf(&m->eq[2], 6000.0f, hi, 1);
    m->eq_on = m->pot[MS_EQ_LO] != 64 || m->pot[MS_EQ_MID] != 64 || m->pot[MS_EQ_HI] != 64;
}

/* X0X's one-knob COMP: the threshold falls (-4 .. -32 dB), the ratio climbs (1.5:1 .. 6:1), the
 * makeup follows (0.85 of the reduction at the threshold); attack 2.6 ms, release 106 ms */
static void comp_coefs(master_t *m)
{
    static const float R[6] = {1.0f, 1.5f, 2.0f, 3.0f, 4.0f, 6.0f};
    int v = m->pot[MS_COMP], ri = v ? 1 + (int)((float)v * (4.0f / 127.0f) + 0.5f) : 0;
    float x = (float)v * (1.0f / 127.0f), thr = -4.0f - 28.0f * x, mk;
    uint8_t was = m->comp_on;
    mk = ri ? fm_minf(-thr * (1.0f - 1.0f / R[ri]) * 0.85f, 24.0f) : 0.0f;
    m->thr_db = thr;
    m->slope = 1.0f - 1.0f / R[ri];
    m->knee = 6.0f;
    m->a_att = coef_ms(2.6f);
    m->a_rel = coef_ms(106.0f);
    m->a_det = coef_ms(8.0f);
    m->makeup = ri ? fm_db2lin(mk) : 1.0f;              /* (the pump alone: unity under it) */
    m->pump_db = 24.0f * (float)m->pot[MS_PUMP] * (1.0f / 127.0f);
    m->a_up = coef_ms(MST_PUMP_RISE_MS / (float)MST_CR);
    m->a_down = coef_ms(master_pump_ms(m->pot[MS_PUMP_REL]) / (float)MST_CR);
    if (!m->pot[MS_PUMP])
        m->pump = m->pump_tgt = 0.0f;
    m->comp_on = ri != 0;                        /* (the pump is on the groups: process's first loop) */
    if (m->comp_on && !was) {
        m->g_lin = m->makeup;
        m->g_step = 0.0f;
        m->cr = 0;
    }
}

/* the color's numbers from its knobs (the main loop); a new type starts at rest */
static void color_coefs(master_t *m)
{
    float a = (float)m->pot[MS_CAMT] * (1.0f / 127.0f), t = (float)m->pot[MS_CTONE] * (1.0f / 127.0f);
    int type = m->pot[MS_CTYPE] < NMC ? m->pot[MS_CTYPE] : MC_OFF;
    m->c_mix = (float)m->pot[MS_CMIX] * (1.0f / 127.0f);
    if (type == MC_TAPE) {
        float g = 0.5f * fm_exp2f(a * 3.585f), x = fm_minf(g * 0.7f, 1.5f);
        m->c_g = g;
        m->c_mk = 0.7f / (x - x * x * x * (4.0f / 27.0f)) * (1.0f - 0.15f * a);   /* (sat_c's at 0.7: made up) */
        m->c_lp = onepole_k(1500.0f * fm_exp2f(t * 3.585f));
        m->c_dep = (4.0f + 28.0f * a) * m->c_mix;  /* samples of wow: ~1 .. 6 cents at 0.55 Hz */
    } else if (type == MC_SAMPLER) {
        float r = 26040.0f * fm_exp2f(-a * 1.243f);
        m->c_step = r / SR;
        m->c_pre = onepole_k(0.45f * r);
        m->c_q = fm_exp2f(13.0f - 3.0f * a);
        m->c_iq = 1.0f / m->c_q;
        m->c_lp = onepole_k(2000.0f * fm_exp2f(t * 3.322f));
    }
    if (type != m->col) {                         /* (the interrupt may be mid-block: the states zeroed first) */
        m->c_z[0][0] = m->c_z[0][1] = m->c_z[1][0] = m->c_z[1][1] = 0.0f;
        m->c_sh[0] = m->c_sh[1] = m->c_sp = 0.0f;
        m->col = (uint8_t)type;
    }
}
/* the color over a block of the bus, in place (master_process; from RAM: every call inlined) */
static inline float mc_sat(float x)
{
    x = fm_minf(fm_maxf(x, -1.5f), 1.5f);
    return x - x * x * x * (4.0f / 27.0f);
}
static inline void color_block(master_t *m, float *l, float *r, uint32_t n)
{
    const float mix = m->c_mix, lp = m->c_lp;
    float z00 = m->c_z[0][0], z01 = m->c_z[0][1], z10 = m->c_z[1][0], z11 = m->c_z[1][1];
    uint32_t i;
    if (m->col == MC_TAPE) {
        const float g = m->c_g, mk = m->c_mk, dep = m->c_dep, dw = 0.55f / SR, df = 6.1f / SR;
        float ph = m->c_ph, fph = m->c_fph;
        int32_t pos = m->c_pos;
        for (i = 0; i < n; i++) {
            float d, fr, yl, yr, a0, a1;
            int32_t k, k0, k1;
            m->c_line[0][pos] = (int16_t)(fm_clampf(l[i], -1.99f, 1.99f) * 16384.0f);
            m->c_line[1][pos] = (int16_t)(fm_clampf(r[i], -1.99f, 1.99f) * 16384.0f);
            ph += dw;
            fph += df;
            if (ph >= 1.0f) ph -= 1.0f;
            if (fph >= 1.0f) fph -= 1.0f;
            d = MC_DELAY + dep * (fm_sin_turns(ph) + 0.125f * fm_sin_turns(fph));
            k = (int32_t)d;
            fr = d - (float)k;
            k0 = (pos - k) & (MC_LINE - 1);
            k1 = (k0 - 1) & (MC_LINE - 1);
            a0 = (float)m->c_line[0][k0], a1 = (float)m->c_line[0][k1];
            yl = (a0 + fr * (a1 - a0)) * (1.0f / 16384.0f);
            a0 = (float)m->c_line[1][k0], a1 = (float)m->c_line[1][k1];
            yr = (a0 + fr * (a1 - a0)) * (1.0f / 16384.0f);
            pos = (pos + 1) & (MC_LINE - 1);
            z00 += lp * (mc_sat(yl * g) * mk - z00);
            z01 += lp * (z00 - z01);
            z10 += lp * (mc_sat(yr * g) * mk - z10);
            z11 += lp * (z10 - z11);
            l[i] = yl + mix * (z01 - yl);
            r[i] = yr + mix * (z11 - yr);
        }
        m->c_ph = ph;
        m->c_fph = fph;
        m->c_pos = pos;
    } else if (m->col == MC_SAMPLER) {
        const float step = m->c_step, pre = m->c_pre, q = m->c_q, iq = m->c_iq;
        float sp = m->c_sp, h0 = m->c_sh[0], h1 = m->c_sh[1];
        for (i = 0; i < n; i++) {
            z00 += pre * (l[i] - z00);
            z10 += pre * (r[i] - z10);
            sp += step;
            if (sp >= 1.0f) {
                sp -= 1.0f;
                h0 = fm_floorf(z00 * q + 0.5f) * iq;
                h1 = fm_floorf(z10 * q + 0.5f) * iq;
            }
            z01 += lp * (h0 - z01);
            z11 += lp * (h1 - z11);
            l[i] += mix * (z01 - l[i]);
            r[i] += mix * (z11 - r[i]);
        }
        m->c_sp = sp;
        m->c_sh[0] = h0;
        m->c_sh[1] = h1;
    }
    m->c_z[0][0] = fm_flush(z00);
    m->c_z[0][1] = fm_flush(z01);
    m->c_z[1][0] = fm_flush(z10);
    m->c_z[1][1] = fm_flush(z11);
}

void master_init(master_t *m)
{
    static const uint8_t DEF[NMS] = {64, 64, 64, 0, MC_OFF, 40, 64, 127, 100, 100, 0, 0, PB_KICK, MS_PUMP_REL_DEF, 1, 100};
    uint32_t i;
    uint8_t *z = (uint8_t *)m;
    for (i = 0; i < sizeof *m; i++)
        z[i] = 0;
    for (i = 0; i < NMS; i++)
        m->pot[i] = DEF[i];
    m->gain = 1.0f;
    m->pg = 1.0f;                                /* (the pump's gain: none ducked) */
    m->gp = m->gp_c = m->gm = m->gm_c = MST_HEADROOM;
    m->glev = m->lev_c = 1.0f;
    eq_coefs(m);
    comp_coefs(m);
    color_coefs(m);
}

void master_set(master_t *m, int p, int v)
{
    if (p < 0 || p >= NMS)
        return;
    m->pot[p] = (uint8_t)(v < 0 ? 0 : v > master_max(p) ? master_max(p) : v);
    if (p <= MS_EQ_HI)
        eq_coefs(m);
    else if (p == MS_RSV3)
        return;                                           /* (the blend's, gone) */
    else if (p <= MS_CMIX)
        color_coefs(m);
    else if (p == MS_PERC)
        m->gp = level_lin(v) * MST_HEADROOM;
    else if (p == MS_MEL)
        m->gm = level_lin(v) * MST_HEADROOM;
    else if (p == MS_LEVEL)
        m->glev = level_lin(v);
    else
        comp_coefs(m);                                    /* comp, the pump's depth, source, release, accent */
}

float master_gr_db(const master_t *m) { return m->gr_view; }
float master_pump_db(const master_t *m) { return m->pump_view; }

void master_key(master_t *m, int src, int vel, int kit)
{
    float t;
    if (!m->pot[MS_PUMP] || src != m->pot[MS_PUMP_BY])
        return;
    t = m->pot[MS_PUMP_ACC] ? m->pump_db * (0.5f + 0.5f * (float)(vel < 0 ? 0 : vel > 127 ? 127 : vel) * (1.0f / 127.0f))
                            : m->pump_db;                   /* an accented hit pumps deeper (X0X: 0.5 .. 1) */
    if (t > m->pump_tgt)
        m->pump_tgt = t;
    m->duck_mel = (uint8_t)(kit != 0);           /* a kick (a kit's hit) ducks the rest, never itself */
}

static inline float gr_of(const master_t *m, float lv)       /* the static curve, soft knee (dB) */
{
    float over = lv - m->thr_db, h = 0.5f * m->knee;
    if (over <= -h)
        return 0.0f;
    if (over >= h)
        return m->slope * over;
    over += h;
    return m->slope * over * over / (2.0f * m->knee);
}
static inline float soft_ceiling(float s)
{
    if (s > 0.89f || s < -0.89f)
        s = s > 0.0f ? 0.89f + 0.11f * fm_tanhf((s - 0.89f) * 9.0f) : -0.89f - 0.11f * fm_tanhf((-s - 0.89f) * 9.0f);
    return s;
}
static inline float glide1(float cur, float tgt)             /* a level knob: ~10 ms, then exact */
{
    if (cur == tgt)
        return cur;
    cur += (tgt - cur) * 0.0022651f;
    return fm_fabsf(tgt - cur) < 1e-5f ? tgt : cur;
}

#ifdef PLAT_HOST
int fx_host_stage = 9;                        /* the host's taps (host.c LOOM_MST): 0 the groups only, 1 + EQ, 2 + color,
                                               * 3 + comp and level, else all (the limiter) */
#define STAGE(k) (fx_host_stage >= (k))
#else
#define STAGE(k) 1
#endif
/* one biquad over a block, its state in registers (the band-major order of master_process: each band's samples
 * are the same as the sample-major loop's, operation for operation) */
static inline void biq_block(const biq_t *q, biqs_t *st, float *x, uint32_t n)
{
    const float b0 = q->b0, b1 = q->b1, b2 = q->b2, a1 = q->a1, a2 = q->a2;
    float x1 = st->x1, x2 = st->x2, y1 = st->y1, y2 = st->y2;
    uint32_t i;
    for (i = 0; i < n; i++) {
        float in = x[i], y = b0 * in + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1;
        x1 = in;
        y2 = y1;
        y1 = fm_flush(y);
        x[i] = y;
    }
    st->x1 = x1;
    st->x2 = x2;
    st->y1 = y1;
    st->y2 = y2;
}
void master_process(master_t *m, float *perc_l, float *perc_r, float *mel_l, float *mel_r, float *out_l,
                    float *out_r, uint32_t n)
{
    uint32_t i;
    float gp = m->gp_c, gm = m->gm_c, lev = m->lev_c, grmax = 0.0f, pmax = 0.0f;
    const int eq_on = m->eq_on, comp_on = m->comp_on;
    /* the groups into the bus, then the EQ a band at a time, in place in mel. (Every state is a local in its loop: a
     * load from RAM costs this core more than a multiply.) */
    {   /* (with the PUMP, X0X's, on the groups as they meet: a kit's hit ducks the melodic group alone, so the kick
         * that keys it is never ducked; a melodic track's note or the click, both) */
        const float tgp = m->gp, tgm = m->gm, a_up = m->a_up, a_down = m->a_down;
        float pump = m->pump, pump_tgt = m->pump_tgt, pg = m->pg, pg_step = m->pg_step;
        int pcr = m->pcr;
        const int mel_only = m->duck_mel;
        for (i = 0; i < n; i++) {
            float pp, ml = mel_l[i], mr = mel_r[i];   /* (both read first: a caller may pass one buffer for both) */
            gp = glide1(gp, tgp);
            gm = glide1(gm, tgm);
            if (pcr == 0) {
                if (pump_tgt > pump) {            /* rise to the hit's depth ... */
                    pump = a_up * pump + (1.0f - a_up) * pump_tgt;
                    if (pump_tgt - pump < 0.05f)
                        pump_tgt = 0.0f;          /* ... then let go: the release swells back */
                } else {
                    pump = fm_flush(a_down * pump);
                }
                if (pump > pmax)
                    pmax = pump;
                pg_step = (fm_db2lin(-pump) - pg) * (1.0f / MST_CR);
                pcr = MST_CR;
            }
            pcr--;
            pg += pg_step;
            pp = mel_only ? gp : gp * pg;
            mel_l[i] = perc_l[i] * pp + ml * gm * pg;
            mel_r[i] = perc_r[i] * pp + mr * gm * pg;
        }
        m->pump = pump;
        m->pump_tgt = pump_tgt;
        m->pg = pg;
        m->pg_step = pg_step;
        m->pcr = (uint8_t)pcr;
    }
    if (eq_on && STAGE(1)) {
        int b;
        for (b = 0; b < 3; b++) {
            biq_block(&m->eq[b], &m->eqs[0][b], mel_l, n);
            biq_block(&m->eq[b], &m->eqs[1][b], mel_r, n);
        }
    }
    if (m->col != MC_OFF && STAGE(2))
        color_block(m, mel_l, mel_r, n);
    {
        const float glev = m->glev, a_det = m->a_det, a_att = m->a_att, a_rel = m->a_rel, slope = m->slope;
        const float makeup = m->makeup;
        float ms = m->ms, want = m->want, gr = m->gr;
        float g_lin = m->g_lin, g_step = m->g_step, env = m->env, inv_env = m->inv_env, gain = m->gain;
        float *la = m->la, *la_r = m->la_r;
        int32_t la_pos = m->la_pos, hold = m->hold;
        int cr = m->cr;
        for (i = 0; i < n; i++) {
            float s = mel_l[i], sr = mel_r[i];
            lev = glide1(lev, glev);
            if (comp_on && STAGE(3)) {
                /* the level: mean square over ~8 ms of both sides, x2 so a sine reads its peak; the static
                 * curve every MST_CR samples; the reduction smoothed (attack / release) and ramped as a
                 * linear gain (X0X's master.c: read every sample, the gain chased each cycle of a bass) */
                ms = fm_flush(a_det * ms + 0.5f * ((1.0f - a_det) * s * s + (1.0f - a_det) * sr * sr));
                if (cr == 0) {
                    float lv = 2.0f * ms;
                    want = (lv > 1e-12f && slope > 0.0f) ? gr_of(m, 3.0103f * fm_log2f(lv)) : 0.0f;
                }
                gr = want > gr ? a_att * gr + (1.0f - a_att) * want : a_rel * gr + (1.0f - a_rel) * want;
                if (gr > grmax)
                    grmax = gr;
                if (cr == 0) {
                    g_step = (makeup * fm_db2lin(-gr) - g_lin) * (1.0f / MST_CR);
                    cr = MST_CR;
                }
                cr--;
                g_lin += g_step;
                s *= g_lin;
                sr *= g_lin;
            }
            s *= lev;
            sr *= lev;
            {   /* X0X's limiter: a peak is seen MST_LA samples before it comes out of the line; the gain
                 * eases down over those samples and holds for them */
                float a = fm_maxf(fm_fabsf(s), fm_fabsf(sr)), d, dr, w;
                if (a >= env) {
                    float r = 1.0f / fm_maxf(a, 0.8f);                   /* safe on its own: a clamp inside
                                                                          * the branch is folded away */
                    env = a;
                    hold = MST_LA;
                    inv_env = a > 0.8f ? r : 0.0f;
                } else if (hold) {
                    hold--;
                } else {
                    env *= 0.99985f;
                    inv_env *= 1.0001500225f;
                }
                w = env > 0.8f ? 0.8f * inv_env : 1.0f;
                gain += (w - gain) * (w < gain ? (5.0f / MST_LA) : 0.002f);
                d = la[la_pos];
                dr = la_r[la_pos];
                la[la_pos] = s;
                la_r[la_pos] = sr;
                la_pos = (la_pos + 1) & (MST_LA - 1);
                out_l[i] = soft_ceiling(d * gain);
                out_r[i] = soft_ceiling(dr * gain);
#ifdef PLAT_HOST
                if (!STAGE(4)) {                  /* (a tap before the limiter: the bus as it comes, unlimited) */
                    out_l[i] = d;
                    out_r[i] = dr;
                }
#endif
            }
        }
        m->ms = ms;
        m->want = want;
        m->gr = gr;
        m->g_lin = g_lin;
        m->g_step = g_step;
        m->env = env;
        m->inv_env = inv_env;
        m->gain = gain;
        m->la_pos = la_pos;
        m->hold = hold;
        m->cr = (uint8_t)cr;
    }
    m->gp_c = gp;
    m->gm_c = gm;
    m->lev_c = lev;
    m->gr_view = grmax > m->gr_view ? grmax : m->gr_view * 0.93f;
    m->pump_view = pmax > m->pump_view ? pmax : m->pump_view * 0.93f;
}
