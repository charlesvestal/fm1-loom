/* SPDX-License-Identifier: MPL-2.0 */
/* LOOM-8's effects library (efx.h): the catalog and every effect's DSP, once. Moved here from synth.c (the character,
 * the inserts) and fx.c (the send effects, the master color) sample for sample: tests/host/dsp_hash.sh holds them to
 * it. (-ffp-contract=off: the arithmetic's order is the sound; keep it as it is when moving code.) */
#include "efx.h"
#include "fastmath.h"

#define EFX_SR 44100.0f
#define INV_SR (1.0f / EFX_SR)
/* the per-sample loops must be inlined into their callers (their constant arguments folded away) */
#if defined(__GNUC__) || defined(__clang__)
#define EF_INLINE static inline __attribute__((always_inline))
#else
#define EF_INLINE static inline
#endif

/* ------------------------------------------------------------ the catalog --- */
const char *const EFG_NAME[NEFG] = {"", "dirt", "color", "movement", "space"};
/* the knobs' names, by number (efx_info_t word) */
enum { W_NONE, W_AMOUNT, W_TONE, W_DISTORT, W_FILTER, W_LEVEL, W_BIAS, W_WHICH, W_DEPTH, W_RATIO, W_PIVOT, W_TILT, W_RATE,
       W_BITS, W_SMOOTH, W_WIDTH, W_PAN, W_FREQ, W_FEEDBACK, W_VIBRATO, W_MODE, W_SENSE, W_RESO, W_DECAY, W_TIME,
       W_REPEATS, W_FINE, W_WOBBLE, W_SMEAR, W_SIZE, W_DAMP, W_TYPE, NW };
static const char *const WORD[NW] = {"-", "amount", "tone", "distort", "filter", "level", "bias", "which", "depth", "ratio",
                                     "pivot", "tilt", "rate", "bits", "smooth", "width", "pan", "freq", "feedback",
                                     "vibrato", "mode", "sense", "reso", "decay", "time", "repeats", "fine", "wobble",
                                     "smear", "size", "damp", "type"};
#define KN(a, b, c) {W_##a, W_##b, W_##c
#define XS(a, b, c, d) W_##a, W_##b, W_##c, W_##d}
const efx_info_t EFX[NEF] = {
    {"off", EFG_NONE, 0, KN(NONE, NONE, NONE), XS(NONE, NONE, NONE, NONE), {0, 0, 0}, {EK_NN, EK_NN, EK_NN}, {0, 0, 0, 0}},
    {"drive", EFG_DIRT, 0, KN(AMOUNT, TONE, NONE), XS(NONE, NONE, NONE, NONE), {64, 64, 0}, {EK_PCT, EK_BIP, EK_NN},
     {0, 0, 0, 0}},
    {"rat", EFG_DIRT, 0, KN(DISTORT, FILTER, LEVEL), XS(NONE, NONE, NONE, NONE), {70, 50, 80}, {EK_NN, EK_NN, EK_NN},
     {0, 0, 0, 0}},
    {"fold", EFG_DIRT, 0, KN(AMOUNT, BIAS, NONE), XS(NONE, NONE, NONE, NONE), {64, 64, 0}, {EK_PCT, EK_BIP, EK_NN},
     {0, 0, 0, 0}},
    {"crush", EFG_DIRT, 0, KN(AMOUNT, WHICH, NONE), XS(NONE, NONE, NONE, NONE), {64, 64, 0}, {EK_PCT, EK_BIP, EK_NN},
     {0, 0, 0, 0}},
    {"ring", EFG_DIRT, 1, KN(DEPTH, RATIO, NONE), XS(NONE, NONE, NONE, NONE), {64, 64, 0}, {EK_PCT, EK_RATIO, EK_NN},
     {0, 0, 0, 0}},
    {"tilt", EFG_DIRT, 0, KN(PIVOT, TILT, NONE), XS(NONE, NONE, NONE, NONE), {64, 64, 0}, {EK_NN, EK_BIP, EK_NN},
     {0, 0, 0, 0}},
    {"tape", EFG_COLOR, 1, KN(AMOUNT, TONE, NONE), XS(NONE, NONE, NONE, NONE), {64, 64, 0}, {EK_PCT, EK_NN, EK_NN},
     {0, 0, 0, 0}},
    {"sampler", EFG_COLOR, 0, KN(AMOUNT, TONE, NONE), XS(NONE, NONE, NONE, NONE), {64, 64, 0}, {EK_PCT, EK_NN, EK_NN},
     {0, 0, 0, 0}},
    {"lofi", EFG_COLOR, 0, KN(RATE, BITS, SMOOTH), XS(WIDTH, NONE, NONE, NONE), {80, 80, 64}, {EK_NN, EK_NN, EK_PCT},
     {40, 0, 0, 0}},                         /* (its width reads its output back from a short line) */
    {"tremolo", EFG_MOVE, 0, KN(RATE, DEPTH, PAN), XS(NONE, NONE, NONE, NONE), {4, 80, 0}, {EK_SYNC, EK_PCT, EK_PCT},
     {0, 0, 0, 0}},
    {"phaser", EFG_MOVE, 0, KN(FREQ, DEPTH, RATE), XS(FEEDBACK, NONE, NONE, NONE), {50, 90, 50},
     {EK_NN, EK_PCT, EK_NN}, {80, 0, 0, 0}},
    {"univibe", EFG_MOVE, 0, KN(RATE, DEPTH, VIBRATO), XS(NONE, NONE, NONE, NONE), {70, 90, 0},
     {EK_SYNC, EK_PCT, EK_PCT}, {0, 0, 0, 0}},
    {"chorus", EFG_MOVE, 0, KN(RATE, DEPTH, FEEDBACK), XS(WIDTH, NONE, NONE, NONE), {50, 86, 20},
     {EK_NN, EK_PCT, EK_PCT}, {127, 0, 0, 0}},
    {"juno", EFG_MOVE, 0, KN(MODE, TONE, NONE), XS(NONE, NONE, NONE, NONE), {0, 70, 0}, {EK_LIST, EK_NN, EK_NN},
     {0, 0, 0, 0}},
    {"autowah", EFG_MOVE, 0, KN(SENSE, RESO, DECAY), XS(NONE, NONE, NONE, NONE), {80, 70, 50}, {EK_NN, EK_NN, EK_NN},
     {0, 0, 0, 0}},
    {"delay", EFG_SPACE, 0, KN(TIME, REPEATS, TONE), XS(FINE, WIDTH, WOBBLE, SMEAR), {83, 56, 64},
     {EK_DIV, EK_NN, EK_NN}, {64, 127, 0, 0}},
    {"rumble", EFG_SPACE, 0, KN(DECAY, TONE, NONE), XS(NONE, NONE, NONE, NONE), {55, 80, 0}, {EK_NN, EK_NN, EK_NN},
     {0, 0, 0, 0}},
    {"reverb", EFG_SPACE, 0, KN(SIZE, DECAY, TONE), XS(TYPE, DAMP, NONE, NONE), {100, 40, 44}, {EK_NN, EK_NN, EK_BIP},
     {0, 72, 0, 0}},                         /* (the plate's defaults, its second knob its motion) */
};
#undef KN
#undef XS
const char *efx_knob_name(int ef, int k)
{
    int w = ef >= 0 && ef < NEF && k >= 0 && k < 7 ? EFX[ef].word[k] : W_NONE;
    return k >= 3 && w == W_NONE ? 0 : WORD[w];
}
const char *efx_knob_word(int ef, int mode, int k)
{
    if (ef == EF_REVERB && mode == RV_PLATE && k == 1)
        return "motion";
    if (ef == EF_REVERB && mode == RV_SHIMMER && k == 4)
        return "shimmer";                    /* (its damping fixed: the octaves fed back) */
    return efx_knob_name(ef, k);
}
/* (synth.c's SYNC_BEATS: 2/1 .. 1/64) */
float efx_lfo_hz(int v, float bpm)
{
    static const float SB[8] = {8.0f, 4.0f, 2.0f, 1.0f, 0.5f, 0.25f, 0.125f, 0.0625f};
    if (v < 8)
        return bpm * (1.0f / 60.0f) / SB[v < 0 ? 0 : v];
    return 0.1f * fm_exp2f((float)(v - 8) * (8.22881869f / 119.0f));   /* 0.1 .. 30 Hz */
}
const char *efx_list_name(int ef, int k, int v)
{
    static const char *const JUNO_MODE[3] = {"I", "II", "I+II"};
    return ef == EF_JUNO && k == 0 ? JUNO_MODE[(v < 0 ? 0 : v) % 3] : "";
}

/* -------------------------------------------- the dirt and the color: the character's types --- */
/* the stage's chunk values (x[]) from the knobs, the LFO's offset on the amount, and the note's frequency (ring). The
 * state (z) starts from 0 when the type changes */
static inline float sat_c(float x)               /* a soft saturation (tanh-like, cubic): flat at +-1 from |x| = 1.5 */
{
    x = fm_minf(fm_maxf(x, -1.5f), 1.5f);
    return x - x * x * x * (4.0f / 27.0f);
}
static inline float sin_q(float ph)              /* a quick sine of a phase in turns (any), within 0.1 %: the folder's */
{
    float x = ph - fm_floorf(ph + 0.5f), y = 8.0f * x - 16.0f * x * fm_fabsf(x);
    return y + 0.225f * (y * fm_fabsf(y) - y);
}
static __attribute__((noinline)) float ch_exp2(float x) { return fm_exp2f(x); }   /* (the coefficients': made only
                                                                                        * when a knob moves, so one copy) */
static __attribute__((noinline)) float ch_exp(float x) { return fm_expf(x); }
static float ch_lp_hz(float t) { return 400.0f * ch_exp2(t * 5.64385619f); }      /* tone 0..1: 400 Hz .. 20 kHz */
static float ch_pivot_hz(float a) { return 150.0f * ch_exp2(a * 5.32192809f); }   /* amount 0..1: 150 Hz .. 6 kHz */
static float ch_alpha(float hz) { return hz >= 19000.0f ? 1.0f : 1.0f - ch_exp(-FM_TWO_PI * hz * INV_SR); }
static float ch_tape_hz(float t) { return 1500.0f * ch_exp2(t * 3.585f); }        /* tone 0..1: 1.5 .. 18 kHz */
static float ch_smp_hz(float t) { return 2000.0f * ch_exp2(t * 3.322f); }         /* tone 0..1: 2 .. 20 kHz */
static float ch_smp_rate(float a) { return 26040.0f * ch_exp2(-a * 1.243f); }     /* amount 0..1: 26 .. 11 kHz */
#define CH_DC (FM_TWO_PI * 12.0f * INV_SR)       /* the DC blocker's one-pole: 12 Hz */
#ifndef CH_REF
#define CH_REF 0.4f                              /* the drive keeps the level of a peak here (a track's, about: -18 dBFS
                                                  * RMS; 0.7 until 2026-11-26, louder than dry below it) */
#endif
#ifndef CH_TREF
#define CH_TREF 0.6f                             /* the tape's (gentler: on a voice, its peak) */
#endif
#ifndef CH_FOLD
#define CH_FOLD 0.45f                            /* the fold's level once it folds (0.6 until 2026-11-26: louder than dry
                                                  * at its default) */
#endif
#define CH_RING_SLEW 0.135f                      /* the ring's amount glides, a chunk's step: about 10 ms (a turned knob
                                                  * clicked: the depth stepped at once) */
/* ring: the modulator's ratios to the note, tone / 8 */
static const float RING_R[16] = {0.25f, 0.5f, 0.75f, 1.0f, 1.25f, 1.414f, 1.5f, 1.75f, 2.0f, 2.5f, 2.76f, 3.0f, 3.5f,
                                 4.0f, 5.0f, 7.0f};
float efx_char_hz(int ef, int knob, float u)
{
    if (!knob)
        return ef == EF_TILT ? ch_pivot_hz(u) : ef == EF_SAMPLER ? ch_smp_rate(u) : 0.0f;
    return ef == EF_DRIVE ? ch_lp_hz(u) : ef == EF_TAPE ? ch_tape_hz(u) : ef == EF_SAMPLER ? ch_smp_hz(u) : 0.0f;
}
/* the ring's amount a, glided from was (its last) unless it starts (*run 0: a new note or type; then 1) */
__attribute__((noinline)) float efx_ring_glide(float a, float was, float *run)
{
    if (*run == 0.0f) {
        *run = 1.0f;
        return a;
    }
    was += (a - was) * CH_RING_SLEW;
    return fm_fabsf(a - was) < 0.002f ? a : was;
}
/* its gains and its mix from where they were (was: the last x[0], x[1], x[7]; 0 a fresh ring) to the new across len
 * frames: char_loop ramps x[4], x[6], x[5] to x[0], x[1], x[7] over the frames left (z[7]). (Before 2026-11-25 the
 * coefficients overwrote the ramp's start and the gains stepped a chunk at a time) */
void efx_ring_from(float *x, float *z, const float *was, int len)
{
    if (was && (was[0] != x[0] || was[1] != x[1] || was[2] != x[7])) {
        x[4] = was[0];
        x[6] = was[1];
        x[5] = was[2];
        z[7] = (float)len;
    } else if (!(z[7] > 0.0f)) {                 /* (no ramp running: flat) */
        x[4] = x[0];
        x[6] = x[1];
        x[5] = x[7];
        z[7] = 0.0f;
    }
}
/* the ring's phasor (z[4] + i z[5]) back to unit length (or at 1 + 0i) */
void efx_ring_norm(float *z)
{
    float re = z[4], im = z[5], r2 = re * re + im * im, g = r2 > 0.5f ? fm_rsqrtf(r2) : 0.0f;
    z[4] = g > 0.0f ? re * g : 1.0f;
    z[5] = im * g;
}
/* the type's values from the amount and tone (0..1) and the note's frequency (ring). tc: the tone as -1..1,
 * 0 at 64 exactly (the page prints it so: the fold's bias, the crush's balance, the tilt) */
void efx_char_coefs(int type, float a, float t, float freq, float *x)
{
    float tc = fm_clampf((t * 127.0f - 64.0f) * (1.0f / 63.0f), -1.0f, 1.0f);
    switch (type) {
    case EF_DRIVE: {                             /* gain 0.25 .. 32 (-12 .. +30 dB), made up at CH_REF */
        float g = 0.25f * ch_exp2(a * 7.0f);
        x[0] = g;
        x[1] = CH_REF / sat_c(g * CH_REF) * (1.0f - 0.25f * a);   /* (squarer as it drives: a little less) */
        x[2] = ch_alpha(ch_lp_hz(t));
        break;
    }
    case EF_FOLD: {                              /* sin(pi/2 (g x + bias)): g 0.3 .. 8, the bias +-1 */
        float g = 0.3f * ch_exp2(a * 4.74f), b = tc;
        x[0] = g * 0.25f;                        /* (in turns) */
        x[1] = fm_maxf(CH_FOLD, 2.0f / (FM_PI * g));   /* small g: linear, unity; large: about the input's level */
        x[3] = b * 0.25f;
        x[4] = fm_sin_turns(b * 0.25f);          /* the output at x = 0: taken off, so silence stays silent */
        x[5] = CH_DC;
        break;
    }
    case EF_CRUSH: {                             /* bits 16 .. 3 and the rate / 1 .. / 45, by the balance */
        float wb = fm_minf(1.0f, 1.0f + tc), wr = fm_minf(1.0f, 1.0f - tc);
        float q = ch_exp2(15.0f - a * wb * 13.0f);
        x[0] = q;
        x[1] = 1.0f / q;
        x[2] = ch_exp2(-a * wr * 5.5f);
        break;
    }
    case EF_RING: {                              /* x (1 - d + d sin): the level made up for the depth */
        int k = (int)(t * 127.0f + 0.5f) >> 3;   /* (as tp_text_par prints it) */
        float w = fm_minf(freq * RING_R[k < 0 ? 0 : k > 15 ? 15 : k] * INV_SR, 0.45f) * FM_TWO_PI;
        float m = fm_rsqrtf((1.0f - a) * (1.0f - a) + 0.5f * a * a);
        x[0] = x[4] = (1.0f - a) * m;           /* (x[4], x[6], x[5]: where the chunk's ramp starts, the gains and the
                                                 * mix: efx_ring_from moves them) */
        x[1] = x[6] = a * m;
        x[2] = fm_cosf(w);                       /* the sine as a rotating phasor: z[4] + i z[5] */
        x[3] = fm_sinf(w);
        break;
    }
    case EF_TAPE: {                              /* the drive's curve, gentler; a two-pole roll-off; the wow's depth */
        float g = 0.5f * ch_exp2(a * 4.0f);
        x[0] = g;
        x[1] = CH_TREF / sat_c(g * CH_TREF) * (1.0f - 0.15f * a);
        x[2] = ch_alpha(ch_tape_hz(t));
        x[3] = 2.0f + 8.0f * a;                  /* cents (synth.c voice_chunk) */
        break;
    }
    case EF_SAMPLER: {                           /* rate and bits by the amount; the input and output filters */
        float r = ch_smp_rate(a), q = ch_exp2(13.0f - 3.0f * a);   /* (14 .. 11 bits: a voice sits well under 0 dB) */
        x[0] = q;
        x[1] = 1.0f / q;
        x[2] = r * INV_SR;
        x[3] = ch_alpha(0.45f * r);
        x[4] = ch_alpha(ch_smp_hz(t));
        break;
    }
    case EF_TILT: {                              /* lows x 2^-tilt, highs x 2^tilt about the pivot */
        float tl = tc;
        x[0] = ch_alpha(ch_pivot_hz(a));
        x[1] = ch_exp2(-tl);
        x[2] = ch_exp2(tl);
        break;
    }
    default:
        break;
    }
}

float efx_char_picture(int type, int amt, int tone, int mix, float u)
{
    float a = (float)(amt & 127) * (1.0f / 127.0f), t = (float)(tone & 127) * (1.0f / 127.0f);
    float wet = (float)(mix & 127) * (1.0f / 127.0f), x[8], d = u, y = u;
    efx_char_coefs(type, a, t, 220.0f, x);
    switch (type) {
    case EF_DRIVE: case EF_TAPE: y = sat_c(u * x[0]) * x[1]; break;
    case EF_FOLD: y = (sin_q(u * x[0] + x[3]) - x[4]) * x[1]; break;
    case EF_CRUSH: case EF_SAMPLER: {            /* a cycle of 200 samples, held and quantised */
        float hold = 1.0f / x[2], n = fm_floorf(u * 200.0f / hold) * hold;
        d = 0.9f * fm_sin_turns(u);
        y = fm_floorf(0.9f * fm_sin_turns(n * (1.0f / 200.0f)) * x[0] + 0.5f) * x[1];
        break;
    }
    case EF_RING: {                              /* two cycles of a sine, times the modulator (its peak at 1) */
        int k = (tone & 127) >> 3;
        d = fm_sin_turns(2.0f * u);
        y = d * (x[0] + x[1] * fm_sin_turns(2.0f * u * RING_R[k])) / (x[0] + x[1]);
        break;
    }
    case EF_TILT: {                              /* the response, 50 Hz .. 20 kHz: dB / 6 */
        float w = 50.0f * fm_exp2f(u * 8.64385619f) / ch_pivot_hz(a), w2 = w * w;
        d = 0.0f;
        y = 3.01029996f * fm_log2f((x[1] * x[1] + x[2] * x[2] * w2) / (1.0f + w2)) * (1.0f / 6.0f);
        break;
    }
    default: return u;
    }
    return d + wet * (y - d);
}

/* one sample of one side through the stage: d dry, back the mix of it and the effect. lp / dc: that side's
 * states; mod: the ring's gain this sample; upd: the crush's hold takes a new sample */
EF_INLINE float char_one(float d, const int type, float c0, float c1, float c2, float c3, float c4, float c5, float wet,
                         float *lp, float *dc, float mod, int upd)
{
    float y;
    switch (type) {
    case EF_DRIVE:
        y = sat_c(d * c0) * c1;
        *lp += c2 * (y - *lp);
        y = *lp;
        break;
    case EF_FOLD:
        y = (sin_q(d * c0 + c3) - c4) * c1;
        *dc += c5 * (y - *dc);
        y -= *dc;
        break;
    case EF_CRUSH:
        if (upd)
            *lp = fm_floorf(d * c0 + 0.5f) * c1;
        y = *lp;
        break;
    case EF_RING:                                /* (a sine times its own pitch has DC: blocked) */
        y = d * mod;
        *dc += CH_DC * (y - *dc);
        y -= *dc;
        break;
    case EF_TILT:
        *lp += c0 * (d - *lp);
        y = *lp * c1 + (d - *lp) * c2;
        break;
    case EF_TAPE:                                /* lp, dc: the roll-off's two poles */
        y = sat_c(d * c0) * c1;
        *lp += c2 * (y - *lp);
        *dc += c2 * (*lp - *dc);
        y = *dc;
        break;
    default:
        y = d;
        break;
    }
    (void)c4;
    return d + wet * (y - d);
}
EF_INLINE void char_loop(float *cx, float *cz, float *xl, float *xr, int k, const int type, const int stereo)
{
    const float c0 = cx[0], c1 = cx[1], c2 = cx[2], c3 = cx[3], c4 = cx[4], c5 = cx[5], wet = cx[7];
    float z0 = cz[0], z1 = cz[1], z2 = cz[2], z3 = cz[3], ph = cz[4], qi = cz[5];
    int j;
    if (type == EF_SAMPLER) {                    /* z0 / z1 the input filter, z2 / z3 the held sample, z6 / z7 the output's */
        float z6 = cz[6], z7 = cz[7];
        for (j = 0; j < k; j++) {
            int upd;
            ph += c2;
            upd = ph >= 1.0f;
            if (upd)
                ph -= 1.0f;
            z0 += c3 * (xl[j] - z0);
            if (upd)
                z2 = fm_floorf(z0 * c0 + 0.5f) * c1;
            z6 += c4 * (z2 - z6);
            xl[j] += wet * (z6 - xl[j]);
            if (stereo) {
                z1 += c3 * (xr[j] - z1);
                if (upd)
                    z3 = fm_floorf(z1 * c0 + 0.5f) * c1;
                z7 += c4 * (z3 - z7);
                xr[j] += wet * (z7 - xr[j]);
            }
        }
        cz[0] = fm_flush(z0);
        cz[1] = fm_flush(z1);
        cz[2] = z2;
        cz[3] = z3;
        cz[4] = ph;
        cz[6] = fm_flush(z6);
        cz[7] = fm_flush(z7);
        return;
    }
    float r0 = cx[4], r1 = cx[6], rw = wet, dr0 = 0.0f, dr1 = 0.0f, drw = 0.0f, left = 0.0f;
    if (type == EF_RING && k > 0) {              /* (the ring: its gains and its mix ramped over the frames left, z[7]) */
        const float sp = cz[7] > (float)k ? cz[7] : (float)k;
        left = cz[7] - (float)k;
        rw = cx[5];
        dr0 = (c0 - r0) / sp;
        dr1 = (c1 - r1) / sp;
        drw = (wet - rw) / sp;
    }
    for (j = 0; j < k; j++) {
        float mod = 0.0f;
        int upd = 0;
        if (type == EF_RING) {                   /* (ph, qi): the phasor, turned a step */
            float nr = ph * c2 - qi * c3;
            qi = ph * c3 + qi * c2;
            ph = nr;
            r0 += dr0;
            r1 += dr1;
            rw += drw;
            mod = r0 + r1 * qi;
        } else if (type == EF_CRUSH) {
            ph += c2;
            if (ph >= 1.0f) {
                ph -= 1.0f;
                upd = 1;
            }
        }
        xl[j] = char_one(xl[j], type, c0, c1, c2, c3, c4, c5, rw, &z0, &z2, mod, upd);
        if (stereo)
            xr[j] = char_one(xr[j], type, c0, c1, c2, c3, c4, c5, rw, &z1, &z3, mod, upd);
    }
    if (type == EF_RING) {                       /* (where the ramp is: done, at its ends exactly) */
        const int on = left > 0.0f;
        cx[4] = on ? r0 : c0;
        cx[6] = on ? r1 : c1;
        cx[5] = on ? rw : wet;
        cz[7] = on ? left : 0.0f;
    }
    cz[0] = fm_flush(z0);
    cz[1] = fm_flush(z1);
    cz[2] = fm_flush(z2);
    cz[3] = fm_flush(z3);
    cz[4] = ph;
    cz[5] = qi;
}
void efx_char_stage(float *cx, float *cz, int type, int stereo, float *xl, float *xr, int k)
{
    switch (type * 2 + (stereo != 0)) {
    case EF_DRIVE * 2: char_loop(cx, cz, xl, xr, k, EF_DRIVE, 0); break;
    case EF_DRIVE * 2 + 1: char_loop(cx, cz, xl, xr, k, EF_DRIVE, 1); break;
    case EF_FOLD * 2: char_loop(cx, cz, xl, xr, k, EF_FOLD, 0); break;
    case EF_FOLD * 2 + 1: char_loop(cx, cz, xl, xr, k, EF_FOLD, 1); break;
    case EF_CRUSH * 2: char_loop(cx, cz, xl, xr, k, EF_CRUSH, 0); break;
    case EF_CRUSH * 2 + 1: char_loop(cx, cz, xl, xr, k, EF_CRUSH, 1); break;
    case EF_RING * 2: char_loop(cx, cz, xl, xr, k, EF_RING, 0); break;
    case EF_RING * 2 + 1: char_loop(cx, cz, xl, xr, k, EF_RING, 1); break;
    case EF_TILT * 2: char_loop(cx, cz, xl, xr, k, EF_TILT, 0); break;
    case EF_TILT * 2 + 1: char_loop(cx, cz, xl, xr, k, EF_TILT, 1); break;
    case EF_TAPE * 2: char_loop(cx, cz, xl, xr, k, EF_TAPE, 0); break;
    case EF_TAPE * 2 + 1: char_loop(cx, cz, xl, xr, k, EF_TAPE, 1); break;
    case EF_SAMPLER * 2: char_loop(cx, cz, xl, xr, k, EF_SAMPLER, 0); break;
    case EF_SAMPLER * 2 + 1: char_loop(cx, cz, xl, xr, k, EF_SAMPLER, 1); break;
    default: break;
    }
}
/* the stage with its numbers ramped a sample at a time from x to t across k frames, both sides (a knob or the mix
 * moving: efx_slot_run; x ends at t). char_loop's arithmetic, its numbers moving: the ring's gains ramp (x[0], x[1]),
 * its sine's step goes to t's at once (its phase runs on: no step) */
static __attribute__((noinline)) void char_ramp(float *x, const float *t, float *z, int type, float *xl, float *xr, int k)
{
    float c[8], d[8];
    float z0 = z[0], z1 = z[1], z2 = z[2], z3 = z[3], ph = z[4], qi = z[5], z6 = z[6], z7 = z[7];
    const float ik = 1.0f / (float)k;
    for (int i = 0; i < 8; i++) {
        c[i] = x[i];
        d[i] = (t[i] - x[i]) * ik;
    }
    for (int j = 0; j < k; j++) {
        const float l = xl[j], r = xr[j];
        float yl, yr, u, v;
        for (int i = 0; i < 8; i++)
            c[i] += d[i];
        switch (type) {
        case EF_DRIVE:
            z0 += c[2] * (sat_c(l * c[0]) * c[1] - z0);
            z1 += c[2] * (sat_c(r * c[0]) * c[1] - z1);
            yl = z0, yr = z1;
            break;
        case EF_FOLD:
            u = (sin_q(l * c[0] + c[3]) - c[4]) * c[1];
            v = (sin_q(r * c[0] + c[3]) - c[4]) * c[1];
            z2 += CH_DC * (u - z2);
            z3 += CH_DC * (v - z3);
            yl = u - z2, yr = v - z3;
            break;
        case EF_CRUSH:                           /* (the step 1 / q from q: the two ramped apart are no inverse) */
            ph += c[2];
            if (ph >= 1.0f) {
                ph -= 1.0f;
                u = 1.0f / c[0];
                z0 = fm_floorf(l * c[0] + 0.5f) * u;
                z1 = fm_floorf(r * c[0] + 0.5f) * u;
            }
            yl = z0, yr = z1;
            break;
        case EF_RING:
            u = ph * t[2] - qi * t[3];
            qi = ph * t[3] + qi * t[2];
            ph = u;
            u = c[0] + c[1] * qi;
            yl = l * u, yr = r * u;
            z2 += CH_DC * (yl - z2);
            z3 += CH_DC * (yr - z3);
            yl -= z2, yr -= z3;
            break;
        case EF_TILT:
            z0 += c[0] * (l - z0);
            z1 += c[0] * (r - z1);
            yl = z0 * c[1] + (l - z0) * c[2];
            yr = z1 * c[1] + (r - z1) * c[2];
            break;
        case EF_TAPE:
            z0 += c[2] * (sat_c(l * c[0]) * c[1] - z0);
            z1 += c[2] * (sat_c(r * c[0]) * c[1] - z1);
            z2 += c[2] * (z0 - z2);
            z3 += c[2] * (z1 - z3);
            yl = z2, yr = z3;
            break;
        case EF_SAMPLER:
            ph += c[2];
            z0 += c[3] * (l - z0);
            z1 += c[3] * (r - z1);
            if (ph >= 1.0f) {
                ph -= 1.0f;
                u = 1.0f / c[0];
                z2 = fm_floorf(z0 * c[0] + 0.5f) * u;
                z3 = fm_floorf(z1 * c[0] + 0.5f) * u;
            }
            z6 += c[4] * (z2 - z6);
            z7 += c[4] * (z3 - z7);
            yl = z6, yr = z7;
            break;
        default:
            yl = l, yr = r;
            break;
        }
        xl[j] = l + c[7] * (yl - l);
        xr[j] = r + c[7] * (yr - r);
    }
    z[0] = fm_flush(z0), z[1] = fm_flush(z1), z[2] = type == EF_CRUSH || type == EF_SAMPLER ? z2 : fm_flush(z2);
    z[3] = type == EF_CRUSH || type == EF_SAMPLER ? z3 : fm_flush(z3);
    z[4] = ph, z[5] = qi;
    if (type == EF_SAMPLER)
        z[6] = fm_flush(z6), z[7] = fm_flush(z7);
    for (int i = 0; i < 8; i++)
        x[i] = t[i];
}

/* ----------------------------------------------------- the sends' effects --- */
/* (fx.c's: the slot glue there; the effects here, as they were) The reverb is 9W9's (X0X's fxbus.c): four int16 combs
 * with damping in the loop and two allpasses a side, the right side reading the combs with alternate signs; here the
 * combs are circular buffers read at a modulated, interpolated tap so "size" scales their lengths and "motion" wanders
 * them (at a fixed 0.5 Hz), "damp" sets the combs' damping, and "tone" filters the wet: a low-pass below the centre, a
 * high-pass above it, flat in the middle. The delay is a ping-pong over the two int16 lines, timed in divisions of the
 * tempo (1/32 to 1/2, dotted ones between) and slewed so a change of time warps instead of clicking (9W9 again), its
 * repeats low-passed by "tone"; the chorus is the JP-8000's stepped, slewed triangle on a short int16 line (NuSaw's);
 * the lofi a sample-and-hold with a quantiser, "smooth" its anti-alias filters, its "width" a stereo spread: the right
 * side reads the lofi output back from the chorus's idle line a few milliseconds late, the lateness wandering slowly;
 * the phaser four first-order allpasses a side, the right side's sweep a quarter turn behind. Every effect returns its
 * wet sound only (the phaser's wet is its notches, the send summed with its allpassed copy).
 *
 * The reverb's other modes: the PlayStation's SPU reverbs (psxrev.h, from schwung-psxverb: MIT), which run on the same
 * line, all of it, and lush: Dattorro's plate (J. Dattorro, "Effect Design, Part 1: Reverberator and Other Filters",
 * JAES 45(9), 1997) at half the rate, on the same line (below). */
#define SR 44100.0f
#define SUB 32                                /* the slot's block: its numbers glide a step here (fx.c) */
#define RV_MOD_HZ 0.5f                        /* the reverb's wander */
#define RV_WET 0.8f                           /* the reverb's return level */
#define RV_WARP 0.25f                         /* the plate's taps at most a sample in four a sample (its motion's wander
                                               * is under a hundredth): a turned size warps its tail a few semitones */
#define LOFI_SPREAD 480.0f                    /* width 127: the right side ~11 ms behind the left */
#define CHO_N EFX_CHO_N

static void zero16(int16_t *b, int32_t n)
{
    int32_t i;
    for (i = 0; i < n; i++)
        b[i] = 0;
}
#include "psxrev.h"

const char *const RV_NAME[NRV] = {"plate", "room", "studio S", "studio M", "studio L", "hall", "half echo",
                                  "space echo", "chaos echo", "one echo", "lush", "shimmer"};
/* the reverb's lines inside the int16 buffer: 9W9's comb lengths, each with room to grow 1.9x,
 * and its four allpasses (left 556 / 441, right 579 / 457) */
static const int32_t RV_COMB[4] = {1116, 1188, 1277, 1356};
static const int32_t RV_CAP[4] = {2125, 2262, 2431, 2581};
static const int32_t RV_AP[4] = {556, 441, 579, 457};
#define RV_AP0 (2125 + 2262 + 2431 + 2581)
_Static_assert(RV_AP0 + 556 + 441 + 579 + 457 <= FX_RV_LEN, "the reverb's lines do not fit FX_RV_LEN");
/* the delay's times in sixteenths: 1/32, 1/16T, 1/32D, 1/16, 1/8T, 1/16D, 1/8, 1/4T, 1/8D, 1/4 (dotted = 1.5x, a
 * triplet 2/3); its name's note and mark (efx.h) */
static const float DL_DIV[FX_DL_N] = {0.5f, 2.0f / 3.0f, 0.75f, 1.0f, 4.0f / 3.0f, 1.5f, 2.0f, 8.0f / 3.0f, 3.0f, 4.0f};
const uint8_t FX_DL_NOTE[FX_DL_N] = {32, 16, 32, 16, 8, 16, 8, 4, 8, 4};
const char FX_DL_MARK[FX_DL_N] = {0, 'T', 'D', 0, 'T', 'D', 0, 'T', 'D', 0};

/* lush: Dattorro's plate at 22.05 kHz on the reverb line (int16, through psxrev.h's spu_rd / spu_wr: one ring whose
 * base moves a sample a tick; a line at offset o is written at o and read len ticks later at o + len). The lines in
 * Dattorro's order, each with room for the largest size (x1.5 of his lengths at 29761 Hz, scaled to 22050 Hz) and the
 * modulation: the pre-delay (its longest 750 ticks: 1152, so lush is 26 blocks of the pool exactly; where a line sits
 * on the ring changes nothing it gives), four input diffusers, then each half of the tank: a modulated allpass, a
 * delay, an allpass, a delay */
enum { LU_PRE, LU_IN1, LU_IN2, LU_IN3, LU_IN4, LU_MAPL, LU_DL1, LU_APL, LU_DL2, LU_MAPR, LU_DR1, LU_APR, LU_DR2, NLU };
_Static_assert(NLU == EFX_LU_N, "efx.h EFX_LU_N: lush's lines");
static const int32_t LU_OFF[NLU + 1] = {0, 1152, 1349, 1507, 1968, 2315, 3101, 8089, 10129, 14303, 15352, 20078,
                                        23069, 26624};
static const float LU_LEN[NLU] = {0.0f, 142.0f, 107.0f, 379.0f, 277.0f, 672.0f, 4453.0f, 1800.0f, 3720.0f, 908.0f,
                                  4217.0f, 2656.0f, 3163.0f};   /* Dattorro's, at 29761 Hz */
_Static_assert(26624 <= FX_RV_LEN, "lush's lines do not fit FX_RV_LEN");
#define SH_OFF 26624                          /* shimmer: the plate's output, a line of SH_W after lush's (its two heads) */
#define SH_W 2048
_Static_assert(SH_OFF + SH_W <= FX_RV_LEN, "shimmer's line does not fit FX_RV_LEN");
typedef efx_lush_t lush_t;
/* a new mode's clearing, a slice a call (SPU_CLEAR), counted over FX_RV_LEN whatever the line (the silence it always
 * had), the line's own part zeroed */
static void rv_clear(int16_t *w, uint32_t n, int32_t *clear)
{
    int32_t k = *clear > SPU_CLEAR ? SPU_CLEAR : *clear, at = FX_RV_LEN - *clear;
    if (at < (int32_t)n)
        zero16(w + at, (int32_t)n - at < k ? (int32_t)n - at : k);
    *clear -= k;
}

/* ---- the one pool (efx.h) ---- */
#ifdef PLAT_HOST
int16_t efx_pool[EFX_NB][EFX_INS_N] __attribute__((aligned(8)));
#else
int16_t efx_pool[EFX_NB][EFX_INS_N] __attribute__((section(".pool"), aligned(8)));
#endif
efx_mask_t efx_send_used;
uint8_t efx_send_wait;
static uint32_t efx_word(int w, int at, int nb)   /* word w's bits of blocks at .. at + nb - 1 */
{
    int lo = at - 32 * w, hi = at + nb - 32 * w;
    lo = lo < 0 ? 0 : lo;
    hi = hi > 32 ? 32 : hi;
    return nb <= 0 || hi <= lo ? 0u : (hi - lo >= 32 ? ~0u : (1u << (hi - lo)) - 1u) << lo;
}
void efx_mark(efx_mask_t *m, int at, int nb)
{
    for (int w = 0; w < EFX_NW; w++)
        m->w[w] |= efx_word(w, at, nb);
}
int efx_meets(const efx_mask_t *m, int at, int nb)
{
    for (int w = 0; w < EFX_NW; w++)
        if (m->w[w] & efx_word(w, at, nb))
            return 1;
    return 0;
}
int efx_fit(const efx_mask_t *m, int need)        /* (one pass: the first run of free blocks to reach need) */
{
    int run = 0;
    if (need <= 0)
        return 0;
    for (int b = 0; b < EFX_NB; b++) {
        run = m->w[b >> 5] >> (b & 31) & 1u ? 0 : run + 1;
        if (run == need)
            return b - need + 1;
    }
    return -1;
}
int efx_clear_step(int16_t *mem, int nb, uint8_t *left, int max)
{
    int k = *left < max ? *left : max;
    if (k <= 0 || !mem)
        return 0;
    {
        uint32_t *w = (uint32_t *)(void *)(mem + (nb - *left) * EFX_INS_N);   /* (the pool's blocks 8-aligned) */
        for (int32_t i = 0; i < k * (EFX_INS_N / 2); i++)
            w[i] = 0u;
    }
    *left = (uint8_t)(*left - k);
    return k;
}
#define RV_PLATE_N (RV_AP0 + 556 + 441 + 579 + 457)   /* the plate's combs and allpasses */
static int32_t rv_need16(int mode)            /* a reverb mode's line, int16: the plate's, an SPU mode's largest address at
                                               * x4 and two more (its "-1" reads, no address aliasing another), lush's,
                                               * shimmer's */
{
    return mode == RV_PLATE ? RV_PLATE_N : mode == RV_LUSH ? LU_OFF[NLU] : mode == RV_SHIMMER ? SH_OFF + SH_W
         : mode > RV_PLATE && mode < NRV ? spu_top(mode) + 2 : FX_RV_LEN;
}
int efx_need(int ef, int mode, int send)
{
    int32_t n;
    switch (ef) {
    case EF_CHORUS: case EF_LOFI: return send == 1 ? EFX_CHO_N / EFX_INS_N : 1;   /* (a send's line: its lag) */
    case EF_JUNO: return 1;
    case EF_TAPE: return send ? 1 : 0;        /* (its wow's line, a send's or the master's: a track's bends its voices,
                                               * a kit's has none) */
    case EF_RUMBLE: return 2;
    case EF_DELAY: n = 2 * FX_DLY_LEN + EFX_SMEAR_N; break;
    case EF_REVERB: n = rv_need16(mode); break;
    default: return 0;
    }
    if (send != 1)                            /* (a track's, the master's: its states first) */
        n += ef == EF_DELAY ? EFX_BIG_DN : EFX_BIG_N;
    return (int)((n + EFX_INS_N - 1) / EFX_INS_N);
}
int32_t efx_rv_ring(int mode, int nb, int send)
{
    int32_t n = (int32_t)nb * EFX_INS_N - (send ? 0 : EFX_BIG_N);
    return mode == RV_PLATE ? RV_PLATE_N : n > FX_RV_LEN ? FX_RV_LEN : n;
}

static inline float onepole_k(float hz) { return hz >= 20000.0f ? 1.0f : 1.0f - fm_expf(-FM_TWO_PI * hz / SR); }
static inline float k127(int v) { return (float)(v < 0 ? 0 : v > 127 ? 127 : v) * (1.0f / 127.0f); }
static inline int16_t f16(float x)
{
    float v = x * 16384.0f;
    return (int16_t)(v > 32767.0f ? 32767.0f : v < -32767.0f ? -32767.0f : v);
}
static inline float s16f(int16_t v) { return (float)v * (1.0f / 16384.0f); }
/* where a number gliding to t (~10 ms) is after m frames: an effect ramps to it across them, a sample at a time */
static inline float ramp_to(float v, float t, int m)
{
    float e = v + (t - v) * ((float)m / ((float)m + EFX_GLIDE_N));
    return fm_fabsf(t - e) < 1e-4f * (fm_fabsf(t) + 1.0f) ? t : e;
}

static void dl_reset(efx_delay_t *d, float t)  /* a delay's states at rest (t: its time now, samples) */
{
    d->w = 0; d->quiet = 2 * FX_DLY_LEN; d->lpl = d->lpr = 0.0f;
    d->ph = 0; d->al = d->ar = 0.0f;
    d->wph = d->wdl = d->wdr = d->wsl = d->wsr = d->hl = d->hr = 0.0f;
    d->sap[0] = d->sap[1] = d->sap[2] = d->sap[3] = d->sap[4] = 0;
    d->cur = t;                               /* a new delay starts on time (the slew is for changes) */
    d->wa = d->ta;                            /* (and at its width) */
    d->wb = d->tb;
}
static void rv_reset(efx_reverb_t *r, float size, int mode)   /* a reverb's, in mode (size: its numbers' first) */
{
    int i;
    for (i = 0; i < 4; i++) {
        r->cpos[i] = r->apos[i] = 0;
        r->cdmp[i] = 0.0f;
        r->cd[i] = size * (float)RV_COMB[i];
    }
    r->quiet = FX_RV_LEN; r->ph = r->hp = 0.0f;
    r->tl[0] = r->tl[1] = r->tr[0] = r->tr[1] = 0.0f;
    r->mode = (uint8_t)mode;
    if (!rv_lush(mode)) {
        spu_reset(&r->spu, mode);
    } else {
        lush_t *u = &r->lu;
        uint8_t *z = (uint8_t *)u;
        for (i = 0; i < (int)sizeof *u; i++)
            z[i] = 0;
        u->clear = FX_RV_LEN;
        u->quiet = FX_RV_LEN * 2;
    }
}
void efx_send_reset(efx_send_t *e, int mode)
{
    int i;
    e->chorus.w = 0; e->chorus.ph = 0.0f;
    e->chorus.s1 = e->chorus.s2 = e->chorus.hl = e->chorus.hr = e->chorus.fz = e->chorus.rg = 0.0f;
    dl_reset(&e->delay, e->pt[0]);
    e->lofi.w = 0;
    e->lofi.ph = e->lofi.hold = e->lofi.in1 = e->lofi.out1 = e->lofi.wph = 0.0f;
    for (i = 0; i < 4; i++)
        e->phaser.yl[i] = e->phaser.yr[i] = 0.0f;
    e->phaser.xl = e->phaser.xr = 0.0f;
    e->phaser.ph = 0.0f; e->phaser.zl = e->phaser.zr = 0.0f; e->phaser.al = e->phaser.ar = 0.0f;
    for (i = 0; i < 6; i++)
        e->pc[i] = e->pt[i];
    rv_reset(&e->reverb, e->pt[0], mode);
}

/* the delay's time in seconds at a tempo, and the times the line halved it (h) */
__attribute__((noinline)) float efx_delay_secs(int time, int fine, float bpm, int *h)
{
    float t = DL_DIV[fx_delay_div(time)] * 15.0f / (bpm > 20.0f ? bpm : 120.0f);   /* sixteenths: 60 / (bpm * 4) */
    float max_s = (float)(FX_DLY_LEN - 128) * 2.0f / SR;            /* (the line at half rate) */
    t *= 1.0f + ((float)fine - 64.0f) * (0.25f / 64.0f);              /* fine: +-25 % */
    for (*h = 0; t > max_s; ++*h)                                     /* too long for the line: half, */
        t *= 0.5f;                                                    /* still on the beat */
    return t;
}

/* the delay's REPEATS: how many repeats ring before -60 dB, 7.95 at 56 (the old knob's 0.42 there), twice as many
 * every 14 steps (0.5 at 1 .. 250 at 126); 0 none; 127 held (1: only the tone, the loop's high-pass and its soft
 * clip take it down). (Before format 27 the knob was 0.95 x / 127: project.c converts) */
float fx_delay_fb(int v)
{
    if (v <= 0)
        return 0.0f;
    if (v >= 127)
        return 1.0f;
    return fm_exp2f(-9.966f / (7.95f * fm_exp2f(((float)v - 56.0f) * (1.0f / 14.0f))));
}

/* PING WIDTH: the echoes' mid kept, their sides times w (0 centred .. 127 hard left and right), at the same power: an
 * echo on one side (e) gives e (1 + w) / 2 and e (1 - w) / 2, power e^2 (1 + w^2) / 2, so both times sqrt(2 / (1 + w^2)) */
void efx_delay_width(float *wa, float *wb, int w)
{
    float x = k127(w), g = fm_sqrtf(2.0f / (1.0f + x * x));
    *wa = w >= 127 ? 1.0f : 0.5f * g * (1.0f + x);
    *wb = w >= 127 ? 0.0f : 0.5f * g * (1.0f - x);
}

/* the knobs to the effect's numbers (pt[]: what each means is in the effect's loop) */
void efx_send_nums(efx_send_t *e, int ef, int mode, const uint8_t *k, float bpm, int wobble, int smear)
{
    efx_nums(e->pt, &e->reverb.shm, ef, mode, k, bpm, wobble, smear);
}
void efx_nums(float *pt, float *shm, int ef, int mode, const uint8_t *k, float bpm, int wobble, int smear)
{
    float a = k127(k[0]), b = k127(k[1]), c = k127(k[2]), d = k127(k[3]);
    switch (ef) {
    case EF_CHORUS:
        pt[0] = 0.05f * fm_exp2f(a * 6.9f);                           /* rate 0.05 .. 6 Hz */
        pt[1] = (0.3f + 7.7f * b) * (SR / 1000.0f);                   /* depth 0.3 .. 8 ms */
        pt[2] = 0.7f * c;                                             /* feedback */
        pt[3] = d;                                                    /* width: 0 mono .. 1 opposite */
        break;
    case EF_DELAY: {
        int h;
        pt[0] = efx_delay_secs(k[0], k[1], bpm, &h) * SR;             /* time and fine */
        pt[2] = fx_delay_fb(k[2]);                                    /* repeats: the feedback (127: held) */
        {
            float wo = k127(wobble);
            pt[4] = wo * wo * (0.004f * SR);                          /* wobble: the drift's depth, 0 .. 4 ms */
        }
        pt[5] = 0.7f * k127(smear);                                   /* smear: the allpasses' gain */
        pt[3] = onepole_k(800.0f * fm_exp2f(d * 4.6f));               /* tone: the repeats' low-pass 800 Hz .. 19 kHz */
        break;
    }
    case EF_LOFI: {
        float fs = 500.0f * fm_exp2f(a * 6.46f);                      /* 500 Hz .. 44.1 kHz */
        float bits = 2.0f + 14.0f * b;
        pt[0] = fm_minf(fs / SR, 1.0f);
        pt[1] = fm_exp2f(bits - 1.0f);                                /* quantiser levels */
        pt[2] = c;                                                    /* smooth: the anti-alias filters */
        pt[3] = d * LOFI_SPREAD;                                      /* width: the right's lateness, samples */
        pt[4] = onepole_k(fm_minf(0.45f * fs, 19000.0f));
        pt[5] = 1.0f / pt[1];
        break;
    }
    case EF_PHASER:
        pt[0] = 100.0f * fm_exp2f(a * 5.64f);                         /* 100 Hz .. 5 kHz */
        pt[1] = 3.0f * b;                                             /* depth, octaves */
        pt[2] = 0.03f * fm_exp2f(c * 8.06f);                          /* rate 0.03 .. 8 Hz */
        pt[3] = 0.9f * d;                                             /* feedback */
        break;
    case EF_REVERB:
        if (rv_lush(mode)) {
            pt[0] = 0.5f + a;                                         /* size: Dattorro's lengths x0.5 .. x1.5 */
            /* decay: the tail's time, the tank's gain from it and the size (a trip round both halves ~0.715 s x size,
             * the gain met twice: 40 log10(g) = -60 trip / T, T 0.4 .. 19 s); the damping takes more, so measured
             * (an impulse, 60 dB down): 0 0.56 s, 45 0.9 s, 64 ~1.4 s, 100 3.7 s, 127 7.2 s, whatever the size */
            pt[1] = fm_exp2f(-1.5f * 3.32192809f * 0.715f * (0.5f + a) / (0.4f * fm_exp2f(b * 5.6f)));
            pt[2] = c < 0.5f ? onepole_k(20000.0f * fm_exp2f(-(0.5f - c) * 9.29f)) : 1.0f;   /* tone: the plate's */
            pt[5] = c > 0.5f ? onepole_k(20.0f * fm_exp2f((c - 0.5f) * 13.29f)) : 0.0f;
            pt[3] = mode == RV_SHIMMER ? 0.2f : 0.05f + 0.85f * d;   /* damp: the tank's low-pass, 0.05 open .. 0.9
                                                                      * (shimmer: light, its knob the octaves) */
            *shm = mode == RV_SHIMMER ? 0.45f * d : 0.0f;
            pt[4] = (0.004f + 0.03f * a) * 22050.0f;                  /* the pre-delay grows with the size: 4 .. 34 ms */
            break;
        }
        if (mode != RV_PLATE) {                                       /* an SPU mode: */
            pt[0] = 1.0f + 3.0f * a;                                  /* size: the registers x1 .. x4 */
            pt[1] = b;                                                /* decay: the wall feedback (spu_derive) */
            pt[2] = c < 0.5f ? onepole_k(20000.0f * fm_exp2f(-(0.5f - c) * 9.29f)) : 1.0f;   /* tone: the plate's */
            pt[5] = c > 0.5f ? onepole_k(20.0f * fm_exp2f((c - 0.5f) * 13.29f)) : 0.0f;
            pt[3] = d;                                                /* damp: vIIR closes */
            pt[4] = 0.0f;
            break;
        }
        pt[0] = 0.5f * fm_exp2f(a * 1.926f);                          /* size: the combs x 0.5 .. 1.9 */
        pt[1] = 24.0f * b * b;                                        /* motion, samples of wander */
        /* tone: below the centre a low-pass closes to 800 Hz, above it a high-pass opens to 2 kHz */
        pt[2] = c < 0.5f ? onepole_k(20000.0f * fm_exp2f(-(0.5f - c) * 9.29f)) : 1.0f;
        pt[5] = c > 0.5f ? onepole_k(20.0f * fm_exp2f((c - 0.5f) * 13.29f)) : 0.0f;
        pt[3] = 0.8f * d;                                             /* damp: the combs' damping */
        pt[4] = 0.55f + 0.42f * a;                                    /* the loop's feedback: decay with size */
        break;
    default:
        break;
    }
}

/* ---- the effects: a block of m samples of x[] (the filtered send) into out (added) ---- */
static inline float cho_tap(const int16_t *cho, float p, int32_t msk)   /* the short line (msk: its length less 1),
                                                                     * read between samples */
{
    int32_t i0 = (int32_t)p;
    float a = s16f(cho[i0 & msk]);
    return a + (p - (float)i0) * (s16f(cho[(i0 + 1) & msk]) - a);
}

/* lag (fx.c fx_process_lag): the send arrives that many samples late; the effects with a line take it back by writing
 * the send that far into the line's past (a slot not read yet: every tap reads further back), so their output is
 * where it would have been, but for the line's rounding: the delay and the chorus add it to the feedback already
 * there (one int16 rounding more, and the delay's soft clip on the sum read back), the plate's combs likewise (its
 * 12 bits) */
static inline int16_t add16(int16_t a, float x)                /* a + x on the int16 line (x in its units) */
{
    float v = (float)a + x * 16384.0f;
    return (int16_t)(v > 32767.0f ? 32767.0f : v < -32767.0f ? -32767.0f : v);
}
/* a slot's (EFX_CHO_SLOT): its line 1024 long, and its lows kept (hp 0); a send's (EFX_CHO_N, hp 1) */
_Static_assert(EFX_CHO_REACH <= EFX_INS_N, "a slot's chorus reads further back than its line");
__attribute__((noinline)) void efx_chorus(efx_chorus_t *c, int16_t *cho, int32_t len, int hp, const float *p,
                                          const float *x, float *ol, float *orr, int m, int lag)
{
    const int32_t msk = len - 1;
    const float inc = p[0] / SR, range = p[1], fb = p[2], side = 1.0f - 2.0f * p[3];
    const float centre = 15.0f * (SR / 1000.0f), slew = 1.0f - fm_expf(-1.0f / (0.070f * SR));
    const float hpk = 0.028f;                 /* ~200 Hz: the wet without its lows */
    float ph = c->ph, s1 = c->s1, s2 = c->s2, hl = c->hl, hr = c->hr, fz = c->fz;
    float rg = c->rg, sd = c->sd, fc = c->fb, erg, esd, efb, drg, dsd, dfb;   /* (the depth, the side, the feedback:
                                              * gliding, a sample at a time; fresh: at them) */
    int32_t w = c->w;
    int i;
    if (m <= 0)
        return;
    if (rg == 0.0f)
        rg = range, sd = side, fc = fb;
    erg = ramp_to(rg, range, m), esd = ramp_to(sd, side, m), efb = ramp_to(fc, fb, m);
    drg = (erg - rg) / (float)m, dsd = (esd - sd) / (float)m, dfb = (efb - fc) / (float)m;
    for (i = 0; i < m; i++) {
        float tri, st, pl, pr, wl, wr;
        rg += drg, sd += dsd, fc += dfb;
        if (lag) {
            cho[w] = f16(fz * fc);
            cho[(w - lag) & msk] = add16(cho[(w - lag) & msk], x[i]);
        } else {
            cho[w] = f16(x[i] + fz * fc);
        }
        w = (w + 1) & msk;
        tri = 2.0f * fm_fabsf(2.0f * ph - 1.0f) - 1.0f;     /* the JP's stepped triangle */
        ph += inc;
        if (ph >= 1.0f)
            ph -= 1.0f;
        st = fm_floorf(tri * 4.0f + 0.5f) * 0.25f;
        s1 += (st - s1) * slew;
        s2 += (s1 - s2) * slew;
        pl = (float)w - (centre + s2 * rg);
        pr = (float)w - (centre + s2 * rg * sd);
        if (pl < 0.0f) pl += (float)len;
        if (pr < 0.0f) pr += (float)len;
        wl = cho_tap(cho, pl, msk);
        wr = cho_tap(cho, pr, msk);
        fz = wl;
        if (hp) {                             /* (a send's wet without its lows; a slot's whole: its mix keeps the
                                               * dry's lows, at full mix the wet's) */
            hl += (wl - hl) * hpk;
            hr += (wr - hr) * hpk;
            wl -= hl;
            wr -= hr;
        }
        ol[i] += wl * 0.85f;
        orr[i] += wr * 0.85f;
    }
    c->ph = ph; c->s1 = s1; c->s2 = s2; c->hl = fm_flush(hl); c->hr = fm_flush(hr); c->fz = fz;
    c->rg = erg, c->sd = esd, c->fb = efb;
    c->w = w;
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

/* smear's allpasses in the chorus line (unused by a delay): a side's two, 8.6 + 2.9 ms (L) and 9.1 + 3.1 ms (R);
 * while smear is on the time is read that much (their mean) sooner and the send waits as long (a line after them),
 * so the first repeat (no allpasses yet) and every one after stay on the beat */
static const int16_t SAP_AT[4] = {0, 379, 506, 907}, SAP_LEN[4] = {379, 127, 401, 137};
#define SAP_COMP 522.0f
#define SAP_IN 1044                              /* (the send's wait: SAP_COMP samples) */
_Static_assert(SAP_IN + (int)SAP_COMP == EFX_SMEAR_N, "efx.h EFX_SMEAR_N: smear's lines");
static inline float sap_run(efx_delay_t *f, int16_t *cho, int k, float x, float g)
{
    int16_t *b = cho + SAP_AT[k];
    int32_t p = f->sap[k];
    float d = s16f(b[p]), w = x + g * d;
    if (w > 1.0f || w < -1.0f) w = fm_tanhf(w);
    b[p] = f16(w);
    f->sap[k] = p + 1 < SAP_LEN[k] ? p + 1 : 0;
    return d - g * w;
}
/* the delay: its lines at half rate (22.05 kHz: a slot a pair of frames, their mean; the repeats low-passed by the
 * tone, at 4 kHz by default, so the 11 kHz the line keeps is enough), read at the frame's own place between slots.
 * The repeats (not the send) through the loop's high-pass (40 Hz: held repeats do not pile up low end), smear's
 * allpasses; wobble moves each side's read (the right a quarter turn on) */
#define DL_WARP 0.5f                             /* the time's slew, at most a sample in two: a turn warps its repeats
                                                  * half again up or down at the most (9W9's own slew, unbounded, went
                                                  * 15 times over on a long turn: a zip) */
static void do_delay(efx_delay_t *f, int16_t *cho, const float *pt, const float *pc, const float *x, float *ol,
                     float *orr, int m, int any, int lag)
{
    const float smear = pt[5] > 0.0f ? pc[5] : 0.0f, wob = pc[4];
    const float target = pt[0] - (smear > 0.0f ? SAP_COMP : 0.0f), fb = pc[2], tc = pc[3];
    const float ea = ramp_to(f->wa, f->ta, m), eb = ramp_to(f->wb, f->tb, m);   /* (PING WIDTH gliding) */
    const float dwa = (ea - f->wa) / (float)m, dwb = (eb - f->wb) / (float)m;
    float wa = f->wa, wb = f->wb;
    const float hk = 0.0057f;                 /* (40 Hz) */
    float dcur = f->cur, lpl = f->lpl, lpr = f->lpr, al = f->al, ar = f->ar, hl = f->hl, hr = f->hr;
    float wsl = f->wsl, wsr = f->wsr, wtl = 0.0f, wtr = 0.0f;
    int32_t w = f->w, quiet = f->quiet, ph = f->ph;
    int i;
    if (!any && quiet >= 2 * FX_DLY_LEN) {    /* silent in, an empty line: time moves on, nothing to read */
        w += (ph + m) >> 1;
        f->ph = (ph + m) & 1;
        f->w = w % FX_DLY_LEN;
        f->al = f->ar = 0.0f;
        f->cur += fm_clampf((target - dcur) * (1.0f - fm_expf(-0.0008f * (float)m)), -DL_WARP * (float)m,
                            DL_WARP * (float)m);
        f->wa = ea, f->wb = eb;
        return;
    }
    if (wob > 0.0f || wsl != 0.0f || wsr != 0.0f) {   /* wobble: a 0.31 Hz sine and a drift (noise, low-passed
                                                  * at ~0.5 Hz: about 0.5 of the sine), each side's, this block */
        uint32_t r = f->rnd ? f->rnd : 0x9E3779B9u;
        float nl, nr;
        r = r * 1664525u + 1013904223u;
        nl = (float)(int32_t)r * (1.0f / 2147483648.0f);
        r = r * 1664525u + 1013904223u;
        nr = (float)(int32_t)r * (1.0f / 2147483648.0f);
        f->rnd = r;
        f->wdl += 0.0023f * (float)m / SUB * (nl * 25.0f - f->wdl);
        f->wdr += 0.0023f * (float)m / SUB * (nr * 25.0f - f->wdr);
        f->wph += 0.31f * (float)m / SR;
        if (f->wph >= 1.0f) f->wph -= 1.0f;
        wtl = wob * 0.6f * (fm_sin_turns(f->wph) + 0.6f * f->wdl);
        wtr = wob * 0.6f * (fm_sin_turns(f->wph + 0.25f) + 0.6f * f->wdr);
    }
    for (i = 0; i < m; i++) {
        float rp, rq, tl, tr, a, b, fl, fr, xi = x[i];
        if (smear > 0.0f) {                   /* (smear: the send waits as long as the allpasses take) */
            int16_t *q = cho + SAP_IN + f->sap[4];
            float v = s16f(*q);
            *q = f16(xi);
            f->sap[4] = f->sap[4] + 1 < (int32_t)SAP_COMP ? f->sap[4] + 1 : 0;
            xi = v;
            if (x[i] != 0.0f)                 /* (a send still waiting: the line not quiet till it is in) */
                quiet = -2 * (int32_t)SAP_COMP;
        }
        dcur += fm_clampf((target - dcur) * 0.0008f, -DL_WARP, DL_WARP);
        rp = (float)w + 0.5f * (float)ph - 0.5f * dcur;
        if (rp < 0.0f)
            rp += (float)FX_DLY_LEN;
        rq = rp;
        if (wtl != 0.0f || wsl != 0.0f || wsr != 0.0f) {   /* (wobble: each side's read moved, gliding) */
            wsl += 0.002f * (wtl - wsl);
            wsr += 0.002f * (wtr - wsr);
            rq = rp - 0.5f * wsr;
            rp -= 0.5f * wsl;
            if (rp < 0.0f) rp += (float)FX_DLY_LEN;
            if (rq < 0.0f) rq += (float)FX_DLY_LEN;
            if (rp >= (float)FX_DLY_LEN) rp -= (float)FX_DLY_LEN;
            if (rq >= (float)FX_DLY_LEN) rq -= (float)FX_DLY_LEN;
        }
        tl = tap16(f->l, rp);
        tr = tap16(f->r, rq);
        lpl += tc * (tl - lpl);               /* tone: the repeats darken */
        lpr += tc * (tr - lpr);
        hl += hk * (lpl - hl);                /* the loop's high-pass */
        hr += hk * (lpr - hr);
        fl = lpl - hl;
        fr = lpr - hr;
        if (smear > 0.0f) {                   /* smear: the repeats through the allpasses */
            fr = sap_run(f, cho, 1, sap_run(f, cho, 0, fr, smear), smear);
            fl = sap_run(f, cho, 3, sap_run(f, cho, 2, fl, smear), smear);
        }
        a = (lag ? 0.0f : xi) + fr * fb;      /* ping-pong: the send and the right into the left, */
        b = fl * fb;                          /* the left into the right */
        if (a > 1.0f || a < -1.0f) a = fm_tanhf(a);
        if (b > 1.0f || b < -1.0f) b = fm_tanhf(b);
        al += a;
        ar += b;
        if (lag) {                            /* the send into the line's past (not read yet: the time is longer) */
            int32_t p = w - ((lag + 1) >> 1);
            float v;
            if (p < 0)
                p += FX_DLY_LEN;
            v = s16f(f->l[p]) + 0.5f * xi;
            if (v > 1.0f || v < -1.0f) v = fm_tanhf(v);
            f->l[p] = f16(v);
        }
        if (ph) {                             /* the pair's second frame: its mean into the slot */
            int16_t ka = f16(0.5f * al), kb = f16(0.5f * ar);
            f->l[w] = ka;
            f->r[w] = kb;
            quiet = (ka == 0 && kb == 0 && x[i] == 0.0f) ? quiet + 2 : 0;
            al = ar = 0.0f;
            if (++w >= FX_DLY_LEN)
                w = 0;
        }
        ph ^= 1;
        wa += dwa, wb += dwb;
        ol[i] += wa * tl + wb * tr;           /* PING WIDTH: at full width wa 1, wb 0, the sides as they are */
        orr[i] += wa * tr + wb * tl;
    }
    f->w = w;
    f->ph = ph;
    f->al = al;
    f->ar = ar;
    f->cur = dcur;
    f->wa = ea, f->wb = eb;
    f->lpl = fm_flush(lpl);
    f->lpr = fm_flush(lpr);
    f->hl = fm_flush(hl);
    f->hr = fm_flush(hr);
    f->wsl = fm_flush(wsl);
    f->wsr = fm_flush(wsr);
    f->quiet = quiet;
}

static void do_lofi(efx_lofi_t *f, int16_t *cho, int32_t len, const float *pt, const float *pc, const float *x, float *ol,
                    float *orr, int m)
{
    const float q = pc[1], iq = 1.0f / pc[1], qual = pc[2], aak = pc[4], step = pc[0];   /* (the levels' step from
                                              * the levels as they glide: the two glided apart stepped the gain 4000x) */
    const int fresh = f->wph == 0.0f;
    float ph = f->ph, hold = f->hold, in1 = f->in1, out1 = f->out1, late, lt = f->late, dlt = 0.0f;
    int32_t w = f->w;
    int i;
    (void)pt;
    f->wph += 0.3f * (float)m / SR;           /* the spread wanders: a 0.3 Hz wow of the right's lateness */
    if (f->wph >= 1.0f)
        f->wph -= 1.0f;
    late = pc[3] * (1.0f + 0.04f * fm_sin_turns(f->wph));
    if (fresh || fm_fabsf(late - lt) <= 0.1f) /* (its width turned: gliding there a sample at a time; the wander's own
                                               * steps are hundredths of a sample) */
        lt = late;
    else
        late = ramp_to(lt, late, m), dlt = (late - lt) / (float)m;
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
        cho[w] = f16(y);                      /* the right side reads the left back, late */
        lt += dlt;
        if (lt < 1.0f) {
            r = y;
        } else {
            float rp = (float)w - lt;
            if (rp < 0.0f)
                rp += (float)len;
            r = cho_tap(cho, rp, len - 1);
        }
        w = (w + 1) & (len - 1);
        ol[i] += y;
        orr[i] += r;
    }
    f->ph = ph; f->hold = hold; f->in1 = fm_flush(in1); f->out1 = fm_flush(out1);
    f->late = late;
    f->w = w;
}

static inline float ap_coef(float hz)
{
    float t = fm_tanf(FM_PI * fm_clampf(hz, 20.0f, 18000.0f) / SR);
    return (t - 1.0f) / (t + 1.0f);
}

/* (a stage's last input is the stage before's last output: one state a stage, and the first's input) */
__attribute__((noinline)) void efx_phaser(efx_phaser_t *f, const float *p, const float *inl, const float *inr, float *ol,
                                          float *orr, int m)
{
    const float centre = p[0], depth = p[1];
    float al_t, ar_t, al = f->al, ar = f->ar, dal, dar, zl = f->zl, zr = f->zr, im = 1.0f / (float)m;
    float fb = f->ph == 0.0f ? p[3] : f->fb, efb = ramp_to(fb, p[3], m), dfb = (efb - fb) * im;   /* (the feedback
                                              * gliding; fresh: at it) */
    float xl0 = f->xl, yl0 = f->yl[0], yl1 = f->yl[1], yl2 = f->yl[2], yl3 = f->yl[3];
    float xr0 = f->xr, yr0 = f->yr[0], yr1 = f->yr[1], yr2 = f->yr[2], yr3 = f->yr[3];
    int i;
    f->ph += p[2] * (float)m / SR;
    if (f->ph >= 1.0f)
        f->ph -= 1.0f;
    al_t = ap_coef(centre * fm_exp2f(depth * fm_sin_turns(f->ph)));
    ar_t = ap_coef(centre * fm_exp2f(depth * fm_sin_turns(f->ph + 0.25f)));
    dal = (al_t - al) * im;
    dar = (ar_t - ar) * im;
    for (i = 0; i < m; i++) {
        float xl, xr, t;
        fb += dfb;
        xl = inl[i] + zl * fb, xr = inr[i] + zr * fb;
        al += dal;
        ar += dar;
        t = al * xl + xl0 - al * yl0;         /* four first-order allpasses a side */
        xl0 = xl;
        xl = al * t + yl0 - al * yl1;
        yl0 = t;
        t = al * xl + yl1 - al * yl2;
        yl1 = xl;
        xl = al * t + yl2 - al * yl3;
        yl2 = t;
        yl3 = xl;
        t = ar * xr + xr0 - ar * yr0;
        xr0 = xr;
        xr = ar * t + yr0 - ar * yr1;
        yr0 = t;
        t = ar * xr + yr1 - ar * yr2;
        yr1 = xr;
        xr = ar * t + yr2 - ar * yr3;
        yr2 = t;
        yr3 = xr;
        zl = fm_flush(xl);
        zr = fm_flush(xr);
        ol[i] += 0.5f * (inl[i] + xl);
        orr[i] += 0.5f * (inr[i] + xr);
    }
    f->xl = xl0; f->yl[0] = yl0; f->yl[1] = yl1; f->yl[2] = yl2; f->yl[3] = yl3;
    f->xr = xr0; f->yr[0] = yr0; f->yr[1] = yr1; f->yr[2] = yr2; f->yr[3] = yr3;
    f->al = al; f->ar = ar; f->zl = zl; f->zr = zr;
    f->fb = efb;
}

static void do_reverb(efx_reverb_t *f, const float *pc, const float *x, float *ol, float *orr, int m, int any, int lag)
{
    const float damp = pc[3], fb = pc[4], hpk = 0.017f;                                /* ~120 Hz on the send */
    const float tlk = pc[2], thk = pc[5];                                              /* the tone's filters */
    float cdd[4], cd[4], cdmp[4], hp = f->hp, tl0 = f->tl[0], tl1 = f->tl[1], th0 = f->tr[0], th1 = f->tr[1];
    int16_t *cb[4], *ab[4];
    int32_t cp[4], ap[4], quiet = f->quiet;
    int c, i;
    if (!any && quiet >= FX_RV_LEN)
        return;
    f->ph += RV_MOD_HZ * (float)m / SR;
    if (f->ph >= 1.0f)
        f->ph -= 1.0f;
    cb[0] = f->line;
    for (c = 0; c < 4; c++) {                 /* where each comb's tap wants to be: size, then the wander */
        float d = pc[0] * (float)RV_COMB[c] + pc[1] * fm_sin_turns(f->ph + 0.25f * (float)c);
        d = fm_clampf(d, 8.0f, (float)(RV_CAP[c] - 3));
        cd[c] = f->cd[c];
        cdd[c] = fm_clampf((d - cd[c]) / (float)m, -RV_WARP, RV_WARP);   /* (its size turned: a warp, bounded) */
        cdmp[c] = f->cdmp[c];
        cp[c] = f->cpos[c];
        ap[c] = f->apos[c];
        if (c)
            cb[c] = cb[c - 1] + RV_CAP[c - 1];
    }
    ab[0] = f->line + RV_AP0;
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
    f->hp = fm_flush(hp);
    f->tl[0] = fm_flush(tl0); f->tl[1] = fm_flush(tl1);
    f->tr[0] = fm_flush(th0); f->tr[1] = fm_flush(th1);
    f->quiet = quiet;
}

/* a read between samples (lush's lines, the SPU's while its size glides) */
static inline float lu_rdf(const int16_t *w, uint32_t n, uint32_t b, int32_t off, float p)
{
    int32_t i = (int32_t)p;
    float fr = p - (float)i, a0 = spu_rd(w, n, b, off + i), a1 = spu_rd(w, n, b, off + i + 1);
    return a0 + fr * (a1 - a0);
}
static __attribute__((noinline)) float lu_rdm(const int16_t *w, uint32_t n, uint32_t b, int32_t off, float p)
{
    return lu_rdf(w, n, b, off, p);              /* (a size gliding: out of line, the still reads stay whole) */
}
/* spu_tick with its size gliding (sz, the last tick's szp): its addresses between samples, the reads interpolated,
 * the writes at the nearest (a write a sample further on than the last tick's fills the sample it would skip with the
 * last one's: no slot keeps the area's sound from a lap before) */
static __attribute__((noinline)) int32_t spu_tick_m(spu_t *s, int16_t *w, uint32_t n, float x, float sz, float szp,
                                                    float *lo, float *ro)
{
    const uint16_t *R = SPU_REG[s->mode - 1];
    const uint32_t b = s->base;
    const float vw = s->vwall, vi = s->viir;
    float iir, l, r, d, a[NREG];
    int32_t any = 0;
    for (int i = 0; i < NREG; i++)
        a[i] = (float)R[i] * sz;
#define RDF(p) lu_rdm(w, n, b, 0, (p) < 0.0f ? (p) + (float)n : (p))
#define WRF(i, v) (any |= spu_wr(w, n, b, (int32_t)(a[i] + 0.5f), (v)))
    uint32_t fill = 0;
    for (int i = R_MLSAME; i < NREG; i++) {      /* (a write a sample further on: the slot it skips, the last one's
                                                  * sample again before anything reads it, the mean of the two after) */
        const int32_t m_ = (int32_t)(a[i] + 0.5f), mp_ = (int32_t)((float)R[i] * szp + 0.5f);
        if (m_ > mp_ && (i == R_MLSAME || i == R_MRSAME || i == R_MLDIFF || i == R_MRDIFF || i >= R_MLAPF1)) {
            w[spu_at(n, b, m_ - 1)] = w[spu_at(n, b, m_ - 2 < 0 ? m_ - 2 + (int32_t)n : m_ - 2)];
            fill |= 1u << i;
        }
    }
    iir = RDF(a[R_MLSAME] - 1.0f);
    WRF(R_MLSAME, (x + RDF(a[R_DLSAME]) * vw - iir) * vi + iir);
    iir = RDF(a[R_MRSAME] - 1.0f);
    WRF(R_MRSAME, (x + RDF(a[R_DRSAME]) * vw - iir) * vi + iir);
    iir = RDF(a[R_MLDIFF] - 1.0f);
    WRF(R_MLDIFF, (x + RDF(a[R_DRDIFF]) * vw - iir) * vi + iir);
    iir = RDF(a[R_MRDIFF] - 1.0f);
    WRF(R_MRDIFF, (x + RDF(a[R_DLDIFF]) * vw - iir) * vi + iir);
    l = s->vcomb[0] * RDF(a[R_MLCOMB1]) + s->vcomb[1] * RDF(a[R_MLCOMB2]) + s->vcomb[2] * RDF(a[R_MLCOMB3]) +
        s->vcomb[3] * RDF(a[R_MLCOMB4]);
    r = s->vcomb[0] * RDF(a[R_MRCOMB1]) + s->vcomb[1] * RDF(a[R_MRCOMB2]) + s->vcomb[2] * RDF(a[R_MRCOMB3]) +
        s->vcomb[3] * RDF(a[R_MRCOMB4]);
    d = RDF(a[R_MLAPF1] - a[R_DAPF1]);
    l -= s->vapf1 * d;
    WRF(R_MLAPF1, l);
    l = l * s->vapf1 + d;
    d = RDF(a[R_MRAPF1] - a[R_DAPF1]);
    r -= s->vapf1 * d;
    WRF(R_MRAPF1, r);
    r = r * s->vapf1 + d;
    d = RDF(a[R_MLAPF2] - a[R_DAPF2]);
    l -= s->vapf2 * d;
    WRF(R_MLAPF2, l);
    l = l * s->vapf2 + d;
    d = RDF(a[R_MRAPF2] - a[R_DAPF2]);
    r -= s->vapf2 * d;
    WRF(R_MRAPF2, r);
    r = r * s->vapf2 + d;
#undef RDF
#undef WRF
    for (int i = R_MLSAME; fill && i < NREG; i++)
        if (fill >> i & 1u) {
            const int32_t m_ = (int32_t)(a[i] + 0.5f);
            w[spu_at(n, b, m_ - 1)] = (int16_t)((w[spu_at(n, b, m_ - 1)] + w[spu_at(n, b, m_)]) >> 1);
        }
    s->base = b + 1u < n ? b + 1u : 0u;
    *lo = l;
    *ro = r;
    return any;
}
#define SPU_DS 0.25f                             /* the size's glide a tick: its farthest address half a sample */

/* an SPU mode: the send decimated to 22.05 kHz, a tick of the console's reverb every second sample, its two
 * sides interpolated back; the plate's tone on the return. Its addresses follow the gliding size a block; turned, the
 * size glides on a tick at a time (SPU_DS: the farthest address half a sample a tick, x1 to x4 in a second or two,
 * a warp of its tail), its addresses between samples, f->hp the size as it is (the plate's high-pass otherwise) */
static void do_spu(efx_reverb_t *f, const float *pc, const float *x, float *ol, float *orr, int m, int any)
{
    spu_t *s = &f->spu;
    int16_t *w = f->line;
    const uint32_t n = f->n;
    const float tlk = pc[2], thk = pc[5], g = -SPU_GAIN[f->mode - 1];
    float tl = s->tl, th = s->th, tr = s->tr, thr = s->thr, ds = 0.0f;
    int i, mv;
    if (s->clear > 0) {                       /* a new mode: the work area emptied first, silent meanwhile */
        rv_clear(w, n, &s->clear);
        return;
    }
    if (!any && s->quiet >= FX_RV_LEN * 2) {  /* silent in, an empty area: nothing to read */
        f->hp = pc[0];
        return;
    }
    if (f->hp == 0.0f)                        /* (fresh: at its size) */
        f->hp = pc[0];
    mv = f->hp != pc[0];
    spu_derive(s, f->mode, mv ? f->hp : pc[0], pc[1], pc[3]);
    if (mv)
        ds = SPU_DS * 4.0f / (float)spu_top(f->mode);
    {   /* (the ring positions, the phase and the tick's outputs in locals: written back after the loop) */
    uint32_t ip = s->ip, up_w = s->up_w, phase = s->phase;
    float o00 = s->out[0][0], o01 = s->out[0][1], o10 = s->out[1][0], o11 = s->out[1][1];
    for (i = 0; i < m; i++) {
        float yl, yr;
        s->in[ip] = s->in[ip + 64u] = x[i];
        ip = (ip + 1u) & 63u;
        if (phase) {                          /* the second of a pair: a tick */
            float lo, ro;
            int wr;
            s->ip = (uint8_t)ip;              /* (spu_decimate reads it) */
            if (mv) {
                const float szp = f->hp;
                f->hp += fm_clampf(pc[0] - f->hp, -ds, ds);
                wr = spu_tick_m(s, w, n, -spu_decimate(s), f->hp, szp, &lo, &ro);
            } else
                wr = spu_tick(s, w, n, -spu_decimate(s), &lo, &ro);   /* vLIN = 8000h: -1 */
            s->quiet = (wr == 0 && !any) ? s->quiet + 2 : 0;
            s->up[0][up_w] = s->up[0][up_w + 32u] = lo;
            s->up[1][up_w] = s->up[1][up_w + 32u] = ro;
            up_w = (up_w + 1u) & 31u;
            spu_interp(s->up[0], up_w, &o00, &o01);
            spu_interp(s->up[1], up_w, &o10, &o11);
            yl = o01 * g;                     /* the output volume: -1 again, and the level */
            yr = o11 * g;
        } else {
            yl = o00 * g;
            yr = o10 * g;
        }
        phase ^= 1u;
        tl += tlk * (yl - tl);
        tr += tlk * (yr - tr);
        th += thk * (tl - th);
        thr += thk * (tr - thr);
        ol[i] += tl - th;
        orr[i] += tr - thr;
    }
    s->ip = (uint8_t)ip;
    s->up_w = (uint8_t)up_w;
    s->phase = (uint8_t)phase;
    s->out[0][0] = o00; s->out[0][1] = o01; s->out[1][0] = o10; s->out[1][1] = o11;
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
#define LU_RD(l, k) spu_rd(w, n, b, LU_OFF[l] + (k))
#define LU_WR(l, v) (wz |= spu_wr(w, n, b, LU_OFF[l], (v)))
/* a line read at its length, a tap at Dattorro's count k: whole samples, or (its size gliding: mv) between them */
#define LU_RL(l) (mv ? lu_rdm(w, n, b, LU_OFF[l], lf[l]) : LU_RD(l, u->len[l]))
#define LU_TAP(l, k) (mv ? lu_rdm(w, n, b, LU_OFF[l], (float)(k) * sc) : LU_RD(l, (int32_t)((float)(k) * sc)))
#define LU_AP(l, g, x)                                                                        \
    do {                                                                                       \
        float d_ = LU_RL(l), v_ = (x) - (g) * d_;                                             \
        LU_WR(l, v_);                                                                          \
        (x) = d_ + (g) * v_;                                                                   \
    } while (0)
#define LU_DS 5.6e-5f                            /* the size's glide a tick: its longest line a quarter sample (a turn
                                                  * a warp of its tail, a few cents; the whole range in 0.6 s) */
static void do_lush(efx_reverb_t *f, const float *pc, const float *x, float *ol, float *orr, int m, int any)
{
    lush_t *u = &f->lu;
    int16_t *w = f->line;
    const uint32_t n = f->n;
    const float sct = pc[0] * (22050.0f / 29761.0f), dec = pc[1], damp = pc[3], tlk = pc[2], thk = pc[5];
    const float lpk = 0.55f, exc = 12.0f * pc[0];          /* (lpk: the send's low-pass, ~5.6 kHz before decimation) */
    const float shm = f->shm;                              /* (shimmer's feedback; 0: lush) */
    float tl = u->tl, th = u->th, tr = u->tr, thr = u->thr, sc = u->scale, lf[NLU];
    int i, l, mv;
    if (u->clear > 0) {                       /* a new mode: the line emptied first, silent meanwhile */
        rv_clear(w, n, &u->clear);
        return;
    }
    if (!any && u->quiet >= FX_RV_LEN * 2)    /* silent in, an empty line: nothing to read */
        return;
    if (sc == 0.0f) {                         /* (fresh: at its size) */
        sc = sct;
        u->pre = pc[4];
    }
    mv = sc != sct || u->pre != pc[4];        /* (its size turned: its lines' lengths glide, LU_DS a tick, read between
                                               * samples, till they are there; whole samples, still, as before) */
    if (!mv && sc != u->scale) {
        for (l = LU_IN1; l < NLU; l++)
            u->len[l] = (int32_t)(LU_LEN[l] * sc);
    }
    u->scale = sc;
    u->len[LU_PRE] = (int32_t)pc[4];
    for (i = 0; i < m; i++) {
        float yl, yr;
        u->lp += lpk * (x[i] - u->lp);
        if (!u->phase) {
            u->hold = u->lp;
        } else {                              /* a tick */
            const uint32_t b = u->base;
            int32_t wz = 0;
            float s = 0.5f * (u->hold + u->lp), a, c, mo, l0, r0;
            if (mv) {                         /* (gliding: a tick's step, the lengths between samples) */
                sc += fm_clampf(sct - sc, -LU_DS, LU_DS);
                u->pre += fm_clampf(pc[4] - u->pre, -0.25f, 0.25f);
                for (l = LU_IN1; l < NLU; l++)
                    lf[l] = LU_LEN[l] * sc;
                lf[LU_PRE] = u->pre;
            }
            if (shm > 0.0f) {                 /* shimmer: the plate's output an octave up, back into its input: two
                                               * heads half the window apart closing in a sample a tick (twice the
                                               * speed: an octave), crossfading; its top softened, its bottom cut
                                               * (~100 Hz: nothing builds up in the loop) (Chordism's) */
                float d1, d2, g1, ha, hb;
                u->sh_d -= 1.0f;
                if (u->sh_d < 2.0f)
                    u->sh_d += (float)(SH_W - 4);
                d1 = u->sh_d;
                d2 = d1 + (float)(SH_W / 2 - 2);
                if (d2 >= (float)(SH_W - 2))
                    d2 -= (float)(SH_W - 4);
                g1 = 1.0f - fm_fabsf(2.0f * (d1 - 2.0f) / (float)(SH_W - 4) - 1.0f);
                ha = spu_rd(w, n, b, SH_OFF + (int32_t)d1);
                hb = spu_rd(w, n, b, SH_OFF + (int32_t)d2);
                u->sh_lp += 0.5f * (ha * g1 + hb * (1.0f - g1) - u->sh_lp);
                u->sh_hp = 0.97f * (u->sh_hp + u->sh_lp - u->sh_x1);
                u->sh_x1 = u->sh_lp;
                s += shm * u->sh_hp;
            }
            u->lfo += 0.7f / 22050.0f;
            if (u->lfo >= 1.0f)
                u->lfo -= 1.0f;
            LU_WR(LU_PRE, s);
            s = LU_RL(LU_PRE);
            LU_AP(LU_IN1, 0.75f, s);
            LU_AP(LU_IN2, 0.75f, s);
            LU_AP(LU_IN3, 0.625f, s);
            LU_AP(LU_IN4, 0.625f, s);
            /* the left half: the right's end in */
            a = s + dec * u->fb[1];
            mo = exc * fm_sin_turns(u->lfo);
            {
                float d = lu_rdf(w, n, b, LU_OFF[LU_MAPL], (mv ? lf[LU_MAPL] : (float)u->len[LU_MAPL]) + mo), v = a + 0.7f * d;
                LU_WR(LU_MAPL, v);
                a = d - 0.7f * v;
            }
            LU_WR(LU_DL1, a);
            a = LU_RL(LU_DL1);
            u->dmp[0] += (1.0f - damp) * (a - u->dmp[0]);
            a = u->dmp[0] * dec;
            LU_AP(LU_APL, 0.5f, a);
            LU_WR(LU_DL2, a);
            /* the right half: the left's end in */
            c = s + dec * u->fb[0];
            mo = exc * fm_sin_turns(u->lfo + 0.25f);
            {
                float d = lu_rdf(w, n, b, LU_OFF[LU_MAPR], (mv ? lf[LU_MAPR] : (float)u->len[LU_MAPR]) + mo), v = c + 0.7f * d;
                LU_WR(LU_MAPR, v);
                c = d - 0.7f * v;
            }
            LU_WR(LU_DR1, c);
            c = LU_RL(LU_DR1);
            u->dmp[1] += (1.0f - damp) * (c - u->dmp[1]);
            c = u->dmp[1] * dec;
            LU_AP(LU_APR, 0.5f, c);
            LU_WR(LU_DR2, c);
            u->fb[0] = LU_RL(LU_DL2);
            u->fb[1] = LU_RL(LU_DR2);
            /* Dattorro's taps (his sample counts, scaled) */
            l0 = LU_TAP(LU_DR1, 266) + LU_TAP(LU_DR1, 2974) - LU_TAP(LU_APR, 1913) + LU_TAP(LU_DR2, 1996)
                 - LU_TAP(LU_DL1, 1990) - LU_TAP(LU_APL, 187) - LU_TAP(LU_DL2, 1066);
            r0 = LU_TAP(LU_DL1, 353) + LU_TAP(LU_DL1, 3627) - LU_TAP(LU_APL, 1228) + LU_TAP(LU_DL2, 2673)
                 - LU_TAP(LU_DR1, 2111) - LU_TAP(LU_APR, 335) - LU_TAP(LU_DR2, 121);
            u->prev[0] = u->cur[0];
            u->prev[1] = u->cur[1];
            u->cur[0] = l0 * 0.45f;                 /* (0.6 was a little loud beside the dry: Charles) */
            u->cur[1] = r0 * 0.45f;
            if (shm > 0.0f)                         /* (shimmer's line: the output, for its heads) */
                wz |= spu_wr(w, n, b, SH_OFF, (u->cur[0] + u->cur[1]) * 0.5f);
            u->base = b ? b - 1u : n - 1u;
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
    if (mv) {                                 /* (there: whole samples again) */
        u->scale = sc;
        if (sc == sct && u->pre == pc[4])
            for (l = LU_IN1; l < NLU; l++)
                u->len[l] = (int32_t)(LU_LEN[l] * sc);
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
#undef LU_RL
#undef LU_TAP

void efx_send_run(efx_send_t *e, int ef, const float *x, float *ol, float *orr, int m, int any, int lag)
{
    switch (ef) {
    case EF_CHORUS: efx_chorus(&e->chorus, e->cho, CHO_N, 1, e->pc, x, ol, orr, m, lag); break;
    case EF_DELAY: do_delay(&e->delay, e->cho, e->pt, e->pc, x, ol, orr, m, any, lag); break;
    case EF_LOFI: do_lofi(&e->lofi, e->cho, CHO_N, e->pt, e->pc, x, ol, orr, m); break;
    case EF_PHASER: efx_phaser(&e->phaser, e->pc, x, x, ol, orr, m); break;
    case EF_REVERB:
        if (e->reverb.mode == RV_PLATE)
            do_reverb(&e->reverb, e->pc, x, ol, orr, m, any, lag);
        else if (rv_lush(e->reverb.mode))
            do_lush(&e->reverb, e->pc, x, ol, orr, m, any);
        else
            do_spu(&e->reverb, e->pc, x, ol, orr, m, any);
        break;
    default: break;
    }
}

int efx_send_awake(const efx_send_t *e, int ef)
{
    const efx_reverb_t *r = &e->reverb;
    switch (ef) {
    case EF_DELAY: return e->delay.quiet < 2 * FX_DLY_LEN;
    case EF_REVERB:
        return r->mode == RV_PLATE ? r->quiet < FX_RV_LEN
             : rv_lush(r->mode) ? r->lu.clear > 0 || r->lu.quiet < FX_RV_LEN * 2
             : r->spu.clear > 0 || r->spu.quiet < FX_RV_LEN * 2;
    case EF_CHORUS: case EF_LOFI: case EF_PHASER: return 1;
    default: return 0;
    }
}

/* ------------------------------------------------------------ the inserts --- */
/* (synth.c's: a track's insert, its knobs, its memory there; the effects here, as they were) */
/* (the inserts' control-rate maths, once in 16 frames: out of line, not copied into each type's loop) */
static __attribute__((noinline)) float in_exp2(float x) { return fm_exp2f(x); }
__attribute__((noinline)) float efx_sin(float ph) { return fm_sin_turns(ph); }
static __attribute__((noinline)) float in_tan(float x) { return fm_tanf(x); }
static __attribute__((noinline)) float ap_k(float hz)   /* a first-order allpass's coefficient at hz: (tan - 1) / (tan + 1) */
{
    float w = in_tan(FM_PI * fm_minf(hz, 0.45f * EFX_SR) * INV_SR);
    return (w - 1.0f) / (w + 1.0f);
}
/* a mix of the dry and the wet, mx 0..1: their gains (1 - mx, mx) made up by the mean of the two ways they add (in
 * phase: 1; apart: rsqrt((1 - mx)^2 + mx^2)): a chorus, a delay or a reverb half mixed as loud as dry or wet alone */
static __attribute__((noinline)) float mix_up(float mx)
{
    return 0.5f + 0.5f * fm_rsqrtf((1.0f - mx) * (1.0f - mx) + mx * mx);
}
/* a one-pole's coefficient at hz */
__attribute__((noinline)) float efx_op_coef(float hz) { return 1.0f - fm_expf(-FM_TWO_PI * hz * INV_SR); }
#ifndef RB_DRIVE
#define RB_DRIVE 1.5f                            /* rumble's saturation: sat_c's gain on the low-passed reverb over the
                                                  * track's peak (light: its 2nd and 3rd harmonics) */
#endif
#define RAT_LV0 80.0f                            /* rat's level at unity (its default): (level / 80)^2 */
#define RAT_SMOOTH 1323.0f                       /* its make-up's smoothing: 30 ms */
#define RAT_MAX 8.0f                             /* (at the most +18 dB) */
#define RB_WET 1.5f                              /* its level at full mix (the dry whole: on a kick its hit as dry,
                                                  * between the hits its tail; on a held sound +4 dB) */
#define RB_GAIN 4.0f                             /* its make-up: the reverb's tail, low-passed, near the track's level */
/* rumble's memory (two slots, int16 at 1/8192): its places (the diffusers', the lines'), the two diffusers, the four
 * lines (mutually prime, at 1.38 kHz: 213 .. 365 ms) */
#define RB_P 0
static const int16_t RB_AA[2] = {8, 8 + 89}, RB_AL[2] = {89, 137};
static const int16_t RB_LEN[4] = {293, 359, 431, 503}, RB_L[4] = {234, 234 + 293, 234 + 652, 234 + 1083};
static const float UV_R[4] = {1.0f, 0.068f, 0.032f, 3.19f};   /* the univibe's stages: its four caps' ratios */
/* rumble's RT60: 0.6 .. 8 s */
__attribute__((noinline)) float efx_rumble_secs(float k) { return 0.6f * fm_exp2f(k * (3.7f / 127.0f)); }
/* below its default (55) the decay also gates rumble's output once the duck has let it back (the producers' volume
 * shaping after the reverb: the lines alone, 213 ms and more, rang on ~0.6 s past the kick at any decay): to -60 dB in
 * 0.15 s at 0 .. 6 s at 54 (long: on top of the network's own fall, 54 and 55 sound alike); 0 from 55 up (no
 * gate: the network's own decay) */
#define RB_GATE_K 55
static __attribute__((noinline)) float rb_gate(float k)
{
    return k < (float)RB_GATE_K ? 0.15f * fm_exp2f(k * (5.32f / (float)RB_GATE_K)) : 0.0f;
}
#define RB_BACK (0.03f + 1.0f / 2.4f)            /* the duck's end, beats after the hit (efx_insert's du) */
/* a track's insert on its pair (efx_insert), after its character, 16 frames at a time (their coefficients at each's
 * start). tremolo: each side's gain 1 - depth (1 + sin) / 2, the right side's sine pan half a cycle on at the top (an
 * auto-pan). phaser and chorus: the sends' (efx_phaser on each side; efx_chorus on the pair's middle, mixed). univibe:
 * four allpasses at the Uni-Vibe's staggered caps, swept by its lamp (the LFO squared and lagged: it hangs dark and
 * flashes), dry and wet (chorus) or wet (vibrato). rat: after the RAT pedal: an op-amp's gain (up to 2000 above 300 Hz,
 * its bandwidth narrowing as it rises), a soft clip x / (1 + x^4)^(1/4) at twice the rate, the filter (a low-pass,
 * darker turned up), the level. autowah: a resonant low-pass (TPT state-variable) opened by the pair's level (an
 * envelope: 3 ms up, decay down) */
static void ins_nums(float *p, int ef, const float *kn, const uint8_t *xt)   /* the sends' numbers from a slot's knobs
                                                                           * and its first extra */
{
    const uint8_t k[4] = {(uint8_t)kn[0], (uint8_t)kn[1], (uint8_t)kn[2], xt[0]};
    float shm;
    efx_nums(p, &shm, ef, 0, k, 120.0f, 0, 0);
}
/* a track's delay or reverb: the send's on the pair's middle, no lag; its states at the start of its memory (efx_big_t:
 * at rest when st says it is fresh, its z[0] 0), its lines after; its numbers from the knobs and extras (the send's
 * four: the delay's time, fine, repeats, tone, its width, wobble, smear; the reverb's size, decay, tone, damp in its
 * mode), gliding as a send's; its wet mixed with the dry, 32 frames at a time */
#define DL_WET 1.26f                             /* a track's delay's echoes made up (+2 dB: as loud as the dry on a
                                                  * held sound at its defaults) */
#define RVI_WET 1.12f                            /* its reverb's (+1 dB) */
#define CHO_WET 1.19f                            /* its chorus's (+1.5 dB) */
#define BIG_GLIDE 0.07f                          /* (fx.c's GLIDE: a block's share, ~10 ms) */
static __attribute__((noinline)) void big_run(int ef, efx_ins_t *st, int16_t *mem, const float *kn, const uint8_t *xt,
                                              int mix, float bpm, float *bl, float *br, uint32_t n)
{
    efx_big_t *g = (efx_big_t *)(void *)mem;
    int16_t *ln = mem + (ef == EF_DELAY ? EFX_BIG_DN : EFX_BIG_N);
    const int dl = ef == EF_DELAY, mode = dl || xt[0] >= NRV ? RV_PLATE : xt[0];
    const uint8_t k[4] = {(uint8_t)kn[0], dl ? xt[0] : (uint8_t)kn[1], (uint8_t)kn[dl ? 1 : 2], dl ? (uint8_t)kn[2] : xt[1]};
    const float mx = (float)mix * (1.0f / 127.0f), mu = mix_up(mx), wu = dl ? DL_WET : RVI_WET;
    float shm = 0.0f;
    efx_nums(g->pt, &shm, ef, mode, k, bpm, dl ? xt[2] : 0, dl ? xt[3] : 0);
    if (dl)
        efx_delay_width(&g->u.d.ta, &g->u.d.tb, xt[1]);
    if (st->z[0] == 0.0f) {                      /* fresh: at rest */
        for (int i = 0; i < 6; i++)
            g->pc[i] = g->pt[i];
        if (dl) {
            dl_reset(&g->u.d, g->pt[0]);
            g->u.d.l = ln;
            g->u.d.r = ln + FX_DLY_LEN;
        } else {
            rv_reset(&g->u.r, g->pt[0], mode);
            g->u.r.line = ln;
            g->u.r.n = (uint32_t)efx_rv_ring(mode, efx_need(EF_REVERB, mode, 0), 0);
        }
        st->z[0] = 1.0f;
    }
    if (!dl)
        g->u.r.shm = shm;
    for (uint32_t j = 0; j < n; j += 32) {
        float x[32], ol[32], orr[32];
        int m = n - j < 32u ? (int)(n - j) : 32, i, any = 0;
        for (i = 0; i < 6; i++)
            g->pc[i] += (g->pt[i] - g->pc[i]) * BIG_GLIDE;
        for (i = 0; i < m; i++) {
            x[i] = 0.5f * (bl[j + i] + br[j + i]);
            any |= x[i] != 0.0f;
            ol[i] = orr[i] = 0.0f;
        }
        if (dl)
            do_delay(&g->u.d, ln + 2 * FX_DLY_LEN, g->pt, g->pc, x, ol, orr, m, any, 0);
        else if (mode == RV_PLATE)
            do_reverb(&g->u.r, g->pc, x, ol, orr, m, any, 0);
        else if (rv_lush(mode))
            do_lush(&g->u.r, g->pc, x, ol, orr, m, any);
        else
            do_spu(&g->u.r, g->pc, x, ol, orr, m, any);
        for (i = 0; i < m; i++) {
            bl[j + i] = mu * (bl[j + i] + mx * (wu * ol[i] - bl[j + i]));
            br[j + i] = mu * (br[j + i] + mx * (wu * orr[i] - br[j + i]));
        }
    }
}
int efx_big_awake(const int16_t *mem, int ef)
{
    const efx_big_t *g = (const efx_big_t *)(const void *)mem;
    const efx_reverb_t *r = &g->u.r;
    if (!mem || (ef != EF_DELAY && ef != EF_REVERB))
        return 0;
    if (ef == EF_DELAY)
        return g->u.d.quiet < 2 * FX_DLY_LEN;
    return r->mode == RV_PLATE ? r->quiet < FX_RV_LEN
         : rv_lush(r->mode) ? r->lu.clear > 0 || r->lu.quiet < FX_RV_LEN * 2
         : r->spu.clear > 0 || r->spu.quiet < FX_RV_LEN * 2;
}
static __attribute__((noinline)) void ins_one(int ef, efx_ins_t *st, float *php, int16_t *mem, const float *kn,
                                              const uint8_t *xt, int mix, float rate, float bpm, const float *in,
                                              float *bl, float *br, uint32_t n)
{
    float *z = st->z;
    const float k0 = kn[0] * (1.0f / 127.0f), k1 = kn[1] * (1.0f / 127.0f), k2 = kn[2] * (1.0f / 127.0f);
    const float mx = (float)mix * (1.0f / 127.0f);   /* (chorus, juno, lofi, rumble: their own mix) */
    float ph = *php;
    switch (ef) {
    case EF_TREMOLO: {
        const float d = k1 * 0.5f, pn = k2 * 0.5f, dph = rate * INV_SR;
        const float mu = fm_rsqrtf((1.0f - d) * (1.0f - d) + 0.5f * d * d), e0 = (1.0f - d) * mu, e1 = d * mu;   /* (its
                                                  * make-up: the swing as loud as the dry on average, its power) */
        float gl = e0 - e1 * efx_sin(ph), gr = e0 - e1 * efx_sin(ph + pn);
        if (z[2] != 0.0f && (fm_fabsf(gl - z[0]) > 1e-4f || fm_fabsf(gr - z[1]) > 1e-4f))
            gl = z[0], gr = z[1];                /* (its depth or pan turned: on from where its gains were, z[0..1]) */
        for (uint32_t j = 0; j < n; j += 16) {
            uint32_t m = n - j < 16u ? n - j : 16u;
            float ph2 = ph + dph * (float)m, gl2 = e0 - e1 * efx_sin(ph2), gr2 = e0 - e1 * efx_sin(ph2 + pn);
            float sl = (gl2 - gl) / (float)m, sr = (gr2 - gr) / (float)m;
            for (uint32_t i = j; i < j + m; i++) {
                bl[i] *= gl;
                br[i] *= gr;
                gl += sl;
                gr += sr;
            }
            ph = ph2 - fm_floorf(ph2);
        }
        z[0] = gl, z[1] = gr, z[2] = 1.0f;
        break;
    }
    case EF_PHASER: {                            /* the send's, a side each (its own phase): freq, depth, rate, feedback
                                                  * (its extra); wet (its notches) */
        float p[6], ol[16], orr[16];
        ins_nums(p, EF_PHASER, kn, xt);
        for (uint32_t j = 0; j < n; j += 16) {
            int m = n - j < 16u ? (int)(n - j) : 16, i;
            for (i = 0; i < m; i++)
                ol[i] = orr[i] = 0.0f;
            efx_phaser(&st->phaser, p, bl + j, br + j, ol, orr, m);
            for (i = 0; i < m; i++)
                bl[j + i] = ol[i], br[j + i] = orr[i];
        }
        break;
    }
    case EF_UNIVIBE: {                           /* z[0..3] the left's stages, z[4..7] the right's, z[8] the lamp */
        const float span = k1 * 3.2f, wet = 0.5f + 0.5f * k2, dph = rate * INV_SR, mu = mix_up(wet);
        const float kl = efx_op_coef(12.0f * 16.0f);  /* (the lamp's lag: about 12 Hz, a step each 16 frames) */
        for (uint32_t j = 0; j < n; j += 16) {
            uint32_t m = n - j < 16u ? n - j : 16u;
            float l = 0.5f + 0.5f * efx_sin(ph), f, a[4];
            z[8] += (l * l - z[8]) * kl;
            f = 220.0f * in_exp2(z[8] * span);
            for (int st = 0; st < 4; st++)
                a[st] = ap_k(f * UV_R[st]);
            for (int c = 0; c < 2; c++) {
                float *x = c ? br : bl, *zs = z + 4 * c;
                for (uint32_t i = j; i < j + m; i++) {
                    float v = x[i];
                    for (int st = 0; st < 4; st++) {
                        float y = a[st] * v + zs[st];
                        zs[st] = v - a[st] * y;
                        v = y;
                    }
                    x[i] = mu * (x[i] + wet * (v - x[i]));
                }
                for (int st = 0; st < 4; st++)
                    zs[st] = fm_flush(zs[st]);
            }
            ph += dph * (float)m;
            ph -= fm_floorf(ph);
        }
        break;
    }
    case EF_RAT: {                               /* z[4c..4c+3]: the input high-pass's low, the op-amp's, the last input
                                                  * (the 2x step), the filter */
        const float g = 1.0f + 2000.0f * k0 * k0 * k0, chp = efx_op_coef(300.0f), cg = efx_op_coef(fm_minf(1.0e6f / g, 18000.0f));
        const float ct = efx_op_coef(20000.0f * in_exp2(-5.3f * k1)), lv = kn[2] * kn[2] * (1.0f / (RAT_LV0 * RAT_LV0));
        float ei = 0.0f, eo = 0.0f, g0, g1, gs;
        for (int c = 0; c < 2; c++) {
            float *x = c ? br : bl, *zs = z + 4 * c, lo = zs[0], op = zs[1], pv = zs[2], tn = zs[3];
            for (uint32_t i = 0; i < n; i++) {
                float u0, u1, y;
                ei += x[i] * x[i];
                lo += chp * (x[i] - lo);
                op += cg * (x[i] + g * (x[i] - lo) - op);
                u0 = 0.5f * (pv + op);           /* (twice the rate: the step between, then the sample) */
                u1 = op;
                pv = op;
                {
                    float a2 = u0 * u0, s0 = fm_rsqrtf(1.0f + a2 * a2), b2 = u1 * u1, s1 = fm_rsqrtf(1.0f + b2 * b2);
                    y = 0.5f * (u0 * s0 * fm_rsqrtf(s0) + u1 * s1 * fm_rsqrtf(s1));
                }
                tn += ct * (y - tn);
                eo += tn * tn;
                x[i] = tn;
            }
            zs[0] = fm_flush(lo), zs[1] = fm_flush(op), zs[2] = fm_flush(pv), zs[3] = fm_flush(tn);
        }
        {                                        /* its make-up: the input's power over the clipped output's (each
                                                  * smoothed, RAT_SMOOTH), so at level 80 it sounds as loud as the track
                                                  * dry, at any distortion and any level in; ramped across the call */
            const float a = (float)n / ((float)n + RAT_SMOOTH);
            z[8] = fm_flush(z[8] + (ei - z[8]) * a);
            z[9] = fm_flush(z[9] + (eo - z[9]) * a);
            g1 = lv * fm_minf(fm_sqrtf((z[8] + 1e-12f) / (z[9] + 1e-12f)), RAT_MAX);
            g0 = z[10] > 0.0f ? z[10] : g1;
            gs = (g1 - g0) / (float)n;
            for (uint32_t i = 0; i < n; i++) {
                g0 += gs;
                bl[i] *= g0;
                br[i] *= g0;
            }
            z[10] = g1 > 0.0f ? g1 : 1e-9f;
        }
        break;
    }
    case EF_AUTOWAH: {                           /* z[0..1] the left's integrators, z[2..3] the right's, z[4] the level */
        const float ka = efx_op_coef(1.0f / (0.003f * FM_TWO_PI)), kr = efx_op_coef(1.0f / ((0.03f + 0.8f * k2 * k2) * FM_TWO_PI));
        const float sens = 2.0f + 60.0f * k0 * k0, kq = 2.0f - 1.9f * k1;
        float e = z[4];
        for (uint32_t j = 0; j < n; j += 16) {
            uint32_t m = n - j < 16u ? n - j : 16u;
            float fc = 180.0f * in_exp2(fm_minf(e * sens, 1.0f) * 4.3f), g = in_tan(FM_PI * fm_minf(fc, 0.45f * EFX_SR) * INV_SR);
            float a1 = 1.0f / (1.0f + g * (g + kq)), a2 = g * a1, a3 = g * a2;
            for (uint32_t i = j; i < j + m; i++) {
                float lvl = fm_fabsf(0.5f * (bl[i] + br[i]));
                e += (lvl - e) * (lvl > e ? ka : kr);
            }
            for (int c = 0; c < 2; c++) {
                float *x = c ? br : bl, i1 = z[2 * c], i2 = z[2 * c + 1];
                for (uint32_t i = j; i < j + m; i++) {
                    float v3 = x[i] - i2, v1 = a1 * i1 + a2 * v3, v2 = i2 + a2 * i1 + a3 * v3;
                    i1 = 2.0f * v1 - i1;
                    i2 = 2.0f * v2 - i2;
                    x[i] = v2;
                }
                z[2 * c] = fm_flush(i1), z[2 * c + 1] = fm_flush(i2);
            }
        }
        z[4] = fm_flush(e);
        break;
    }
    case EF_CHORUS: case EF_LOFI: {              /* the send's on the pair's middle (its line: 2 KB): its knobs, its
                                                  * extra (width); mixed. The chorus's wet whole (a send's loses its
                                                  * lows: at full mix a slot's would be thin) */
        float p[6], x[16], ol[16], orr[16];
        const float mu = mix_up(mx), wu = ef == EF_CHORUS ? CHO_WET : 1.0f;
        if (!mem)
            break;
        ins_nums(p, ef, kn, xt);
        for (uint32_t j = 0; j < n; j += 16) {
            int m = n - j < 16u ? (int)(n - j) : 16, i;
            for (i = 0; i < m; i++) {
                x[i] = 0.5f * (bl[j + i] + br[j + i]);
                ol[i] = orr[i] = 0.0f;
            }
            if (ef == EF_CHORUS)
                efx_chorus(&st->chorus, mem, EFX_INS_N, 0, p, x, ol, orr, m, 0);
            else
                do_lofi(&st->lofi, mem, EFX_INS_N, p, p, x, ol, orr, m);
            for (i = 0; i < m; i++) {
                bl[j + i] = mu * (bl[j + i] + mx * (wu * ol[i] - bl[j + i]));
                br[j + i] = mu * (br[j + i] + mx * (wu * orr[i] - br[j + i]));
            }
        }
        break;
    }
    case EF_JUNO: {                              /* z[0] where it writes; the line: its 2 KB, the pair's middle at 1/8192;
                                                  * the Juno-60's modes (I 0.513 Hz, II 0.863 Hz: 1.66 .. 5.35 ms; I+II
                                                  * 9.75 Hz: 3.3 .. 3.7 ms), its wet darkened (z[1], z[2]) */
        static const float JR[3] = {0.513f, 0.863f, 9.75f}, JD0[3] = {3.505f, 3.505f, 3.5f}, JDM[3] = {1.845f, 1.845f, 0.2f};
        const int jm = (int)kn[0] % 3;
        int16_t *ln = mem;
        const float dph = JR[jm] * INV_SR;
        const float d0 = JD0[jm] * 0.001f * EFX_SR, dm = JDM[jm] * 0.001f * EFX_SR;
        const float wet = mx, kt = efx_op_coef(3000.0f + 9000.0f * k1), mu = mix_up(mx);
        float tl = z[1], tr = z[2];
        int w = (int)z[0];
        if (!ln)
            break;
        for (uint32_t j = 0; j < n; j += 16) {
            uint32_t m = n - j < 16u ? n - j : 16u;
            float tri = 4.0f * fm_fabsf(ph - fm_floorf(ph + 0.5f)) - 1.0f;   /* (a triangle, -1..1, as the Juno's) */
            float dl = d0 + dm * tri, dr = d0 - dm * tri, sl_ = 0.0f, sr_ = 0.0f;
            if (z[3] != 0.0f && (fm_fabsf(dl - z[3]) > 1.0f || fm_fabsf(dr - z[4]) > 1.0f)) {
                const float el = ramp_to(z[3], dl, (int)m), er = ramp_to(z[4], dr, (int)m);   /* (its mode changed: its
                                                  * reads glide there, z[3..4], not jump) */
                sl_ = (el - z[3]) / (float)m, sr_ = (er - z[4]) / (float)m;
                dl = z[3], dr = z[4];
            }
            for (uint32_t i = j; i < j + m; i++) {
                dl += sl_, dr += sr_;
                float xs = 0.5f * (bl[i] + br[i]), yl, yr;
                ln[w] = (int16_t)fm_clampf(xs * 8192.0f, -32767.0f, 32767.0f);
                {
                    float pl = (float)w - dl, pr = (float)w - dr;
                    int il, ir;
                    float fl, fr;
                    pl += pl < 0.0f ? (float)EFX_INS_N : 0.0f;
                    pr += pr < 0.0f ? (float)EFX_INS_N : 0.0f;
                    il = (int)pl, ir = (int)pr;
                    fl = pl - (float)il, fr = pr - (float)ir;
                    yl = ((float)ln[il] + ((float)ln[(il + 1) & (EFX_INS_N - 1)] - (float)ln[il]) * fl) * (1.0f / 8192.0f);
                    yr = ((float)ln[ir] + ((float)ln[(ir + 1) & (EFX_INS_N - 1)] - (float)ln[ir]) * fr) * (1.0f / 8192.0f);
                }
                tl += kt * (yl - tl);            /* (the bucket brigade's darkening) */
                tr += kt * (yr - tr);
                bl[i] = mu * (bl[i] + wet * (tl - bl[i]));
                br[i] = mu * (br[i] + wet * (tr - br[i]));
                w = (w + 1) & (EFX_INS_N - 1);
            }
            z[3] = dl, z[4] = dr;
            ph += dph * (float)m;
            ph -= fm_floorf(ph);
        }
        z[0] = (float)w, z[1] = fm_flush(tl), z[2] = fm_flush(tr);
        break;
    }
    case EF_RUMBLE: {                            /* the techno rumble, the track's reverb as a low instrument (the
                                                  * producers' chain: no sub into the reverb, no modulation, a steep
                                                  * low-pass after, light saturation tamed by a low-pass, a sidechain):
                                                  * the track high-passed (12 dB at 120 Hz), at a 32nd of the
                                                  * rate (1.38 kHz: twice the lines' time, their low modes twice as
                                                  * dense, a smear, not tones) into two diffusers and a Hadamard network of four
                                                  * lines (its decay), out through four poles at tone, saturated by
                                                  * the track's peak, a pole at three times tone; ducked on each hit
                                                  * (the track's level rising past half its peak), back by an eighth.
                                                  * z[0] the 32nd's sum, z[1] its count + 32 armed, z[2..3] the
                                                  * high-pass's low and its output's peak, z[4..5] the low-pass in, z[6..7] the last two
                                                  * ticks, z[8..11] the low-pass out, z[12] the tame pole, z[13] the
                                                  * level, z[14] its peak, z[15] the second high-pass; ph the beats since the hit; the places: the
                                                  * memory's first */
        int16_t *m = mem;
        const float rt = efx_rumble_secs(kn[0]), fc = 80.0f * in_exp2(k1 * 2.0f), ko = efx_op_coef(fc), kt = efx_op_coef(3.0f * fc);
        const float kh = efx_op_coef(120.0f), ka = efx_op_coef(300.0f), wet = mx * RB_WET;
        const float rel = 1.0f - efx_op_coef(1.0f / 0.03f), prel = 1.0f - efx_op_coef(1.0f / 3.0f);
        const float dbt = INV_SR * fm_maxf(bpm, 20.0f) * (1.0f / 60.0f);
        float g[4], sum = z[0], h1 = z[2], h2 = z[3], a = z[4], b = z[5], y0 = z[6], y1 = z[7];
        float o1 = z[8], o2 = z[9], o3 = z[10], o4 = z[11], tm = z[12], env = z[13], pk = z[14], hb = z[15];
        int cnt = (int)z[1] & 31, armed = (int)z[1] >> 5;
        const float gtt = rb_gate(kn[0]);  /* the gate: its -60 dB time (0: none), its fall a sample, where it is
                                                  * (from ph: no state of its own) */
        const float gd = gtt > 0.0f ? in_exp2(-9.966f * INV_SR / gtt) : 1.0f;
        float gt = gtt > 0.0f && ph > RB_BACK ? in_exp2(-9.966f * (ph - RB_BACK) * 60.0f / (fm_maxf(bpm, 20.0f) * gtt))
                                              : 1.0f;
        float gs = 1.0f - 2.0f * (z[1] - fm_floorf(z[1]));   /* (the gate as it sounds: z[1]'s fraction, 1 - gs halved;
                                                  * its decay turned, its gate somewhere else: it glides there, ~10 ms) */
        int own = fm_fabsf(gt - gs) > 1e-3f;
        if (!own)
            gs = gt;
        if (!m)
            break;
        for (int q = 0; q < 4; q++)              /* (-60 dB in rt: 10^(-3 len / (sr/32 rt))) */
            g[q] = in_exp2(-9.966f * (float)RB_LEN[q] / (EFX_SR * (1.0f / 32.0f) * rt));
        for (uint32_t i = 0; i < n; i++) {
            float x = in ? in[i] : 0.5f * (bl[i] + br[i]), ax = fm_fabsf(x), u, y, du;
            env = ax > env ? env + 0.3f * (ax - env) : env * rel;
            pk = fm_maxf(env, pk * prel);
            if (armed && env > 0.5f * pk && env > 1e-4f)
                ph = 0.0f, armed = 0, gt = gs = 1.0f, own = 0;   /* a hit: the duck from its top, the gate open */
            else if (env < 0.2f * pk)
                armed = 1;
            ph = fm_minf(ph + dbt, 64.0f);       /* (64: the gate's place, up to 8 s at any tempo) */
            if (ph > RB_BACK)
                gt *= gd;
            if (!own)
                gs = gt;
            else if (fm_fabsf(gt - (gs += (gt - gs) * 0.0023f)) < 1e-5f)
                gs = gt, own = 0;
            h1 += kh * (x - h1);                 /* (the kick's sub out, 12 dB an octave: in the reverb it holds the
                                                  * kick's note, a drone; its body and click smear) */
            u = x - h1;
            hb += kh * (u - hb);
            u -= hb;
            h2 = fm_maxf(fm_fabsf(u), h2 * prel);   /* (and its peak: the reverb's input over it) */
            a += ka * (u - a);
            b += ka * (a - b);
            sum += b;
            if (++cnt == 32) {                   /* a tick */
                const float pr = fm_maxf(pk, 1e-4f);   /* (the lines hold the high-passed track over its own peak:
                                                  * full scale at any level and any kick; out again at the track's) */
                float w = sum * (1.0f / 32.0f) / fm_maxf(h2, 1e-4f), v[4], q0, q1, q2, q3;
                for (int q = 0; q < 2; q++) {    /* the diffusers */
                    int16_t *ap = m + RB_AA[q];
                    int pq = m[RB_P + q];
                    float vv = (float)ap[pq] * (1.0f / 8192.0f), yy = vv - 0.6f * w;
                    ap[pq] = (int16_t)fm_clampf((w + 0.6f * yy) * 8192.0f, -32767.0f, 32767.0f);
                    m[RB_P + q] = (int16_t)(pq + 1 < RB_AL[q] ? pq + 1 : 0);
                    w = yy;
                }
                for (int q = 0; q < 4; q++)
                    v[q] = (float)m[RB_L[q] + m[RB_P + 2 + q]] * (1.0f / 8192.0f);
                q0 = v[0] + v[1], q1 = v[0] - v[1], q2 = v[2] + v[3], q3 = v[2] - v[3];
                {
                    const float hv[4] = {0.5f * (q0 + q2), 0.5f * (q1 + q3), 0.5f * (q0 - q2), 0.5f * (q1 - q3)};
                    for (int q = 0; q < 4; q++) {
                        int pq = m[RB_P + 2 + q];
                        m[RB_L[q] + pq] = (int16_t)fm_clampf((w + g[q] * hv[q]) * 8192.0f, -32767.0f, 32767.0f);
                        m[RB_P + 2 + q] = (int16_t)(pq + 1 < RB_LEN[q] ? pq + 1 : 0);
                    }
                }
                y0 = y1;
                y1 = (v[0] + v[1] + v[2] + v[3]) * pr;
                sum = 0.0f, cnt = 0;
            }
            y = y0 + (y1 - y0) * (float)cnt * (1.0f / 32.0f);
            o1 += ko * (y - o1);                 /* (four poles: 24 dB an octave above tone) */
            o2 += ko * (o1 - o2);
            o3 += ko * (o2 - o3);
            o4 += ko * (o3 - o4);
            {                                    /* light saturation (odd: no tone of its own), as hard at any level */
                const float pr = fm_maxf(pk, 1e-4f), sv = o4 * (RB_DRIVE / pr);
                tm += kt * (sat_c(sv) * (RB_GAIN * pr / RB_DRIVE) - tm);   /* (then its make-up) */
            }
            du = fm_clampf((ph - 0.03f) * 2.4f, 0.0f, 1.0f);   /* (0 on the hit, back by an eighth or so) */
            du = du * du * (3.0f - 2.0f * du);
            bl[i] += wet * du * gs * tm;
            br[i] += wet * du * gs * tm;
        }
        z[0] = sum, z[1] = (float)(cnt + 32 * armed) + 0.5f * (1.0f - gs), z[2] = fm_flush(h1), z[3] = fm_flush(h2), z[4] = fm_flush(a);
        z[5] = fm_flush(b), z[6] = fm_flush(y0), z[7] = fm_flush(y1), z[8] = fm_flush(o1), z[9] = fm_flush(o2);
        z[10] = fm_flush(o3), z[11] = fm_flush(o4), z[12] = fm_flush(tm), z[13] = fm_flush(env), z[14] = fm_flush(pk);
        z[15] = fm_flush(hb);
        break;
    }
    case EF_DELAY: case EF_REVERB:
        if (mem)
            big_run(ef, st, mem, kn, xt, mix, bpm, bl, br, n);
        break;
    default:
        break;
    }
    *php = ph;
}
/* the wet-only ones (tremolo, phaser, univibe, rat, autowah) mixed with the dry, 32 frames at a time; the others mix
 * in their own loops (chorus, juno, lofi, delay, reverb; rumble's mix its level, the dry whole) */
void efx_insert(int ef, efx_ins_t *st, float *ph, int16_t *mem, const float *kn, const uint8_t *xt, int mix, float rate,
                float bpm, const float *in, float *bl, float *br, uint32_t n)
{
    float dl[32], dr[32];
    if (mix >= 127 || ef == EF_CHORUS || ef == EF_JUNO || ef == EF_LOFI || ef == EF_RUMBLE || ef == EF_DELAY ||
        ef == EF_REVERB) {
        ins_one(ef, st, ph, mem, kn, xt, mix, rate, bpm, in, bl, br, n);
        return;
    }
    for (uint32_t j = 0; j < n; j += 32) {
        const float mx = (float)mix * (1.0f / 127.0f);
        uint32_t m = n - j < 32u ? n - j : 32u, i;
        for (i = 0; i < m; i++)
            dl[i] = bl[j + i], dr[i] = br[j + i];
        ins_one(ef, st, ph, mem, kn, xt, 127, rate, bpm, in ? in + j : 0, bl + j, br + j, m);
        for (i = 0; i < m; i++) {
            bl[j + i] = dl[i] + mx * (bl[j + i] - dl[i]);
            br[j + i] = dr[i] + mx * (br[j + i] - dr[i]);
        }
    }
}

/* ---- tape's wow off the voices (the master, a send): a short line in its memory ---- */
typedef struct {
    int16_t line[2][EFX_WOW_N];                  /* the pair, a side (int16 at 2^14 to 1: +-2) */
    float ph, fph;                               /* the wow's phase and the flutter's */
    int32_t pos;
    float b;                                     /* the line against the pair as it is: 1 (0: fresh), gliding in and
                                                  * out as the tape comes and goes (its lateness: no step) */
} efx_wow_t;
_Static_assert(sizeof(efx_wow_t) <= EFX_BLOCK, "tape's wow: its line in one block of the pool");
/* the pair, in place, through the line read at a wandering delay: EFX_WOW_AT samples, dep more or less (the wow 0.55
 * Hz, its flutter 6.1 Hz an eighth as deep). bt: 1, or 0 as the tape fades out (the line crossfaded with the pair as
 * it is, across the call); started from dry (dry), its lateness grows from 0 in 46 ms (a tape spinning up: a
 * quarter tone flat that long) */
static void wow_run(efx_wow_t *w, float dep, float *l, float *r, uint32_t n, float bt, int dry)
{
    const float dw = 0.55f / EFX_SR, df = 6.1f / EFX_SR;
    float ph = w->ph, fph = w->fph, b, db;
    int32_t pos = w->pos;
    if (w->b == 0.0f && !dry)
        w->b = 1.0f;
    b = w->b;
    db = bt < b ? (bt - b) / (float)n : bt > b ? 1.0f / 2048.0f : 0.0f;
    for (uint32_t i = 0; i < n; i++) {
        float d, fr, a0, a1;
        int32_t k, k0, k1;
        w->line[0][pos] = (int16_t)(fm_clampf(l[i], -1.99f, 1.99f) * 16384.0f);
        w->line[1][pos] = (int16_t)(fm_clampf(r[i], -1.99f, 1.99f) * 16384.0f);
        ph += dw;
        fph += df;
        if (ph >= 1.0f) ph -= 1.0f;
        if (fph >= 1.0f) fph -= 1.0f;
        d = EFX_WOW_AT + dep * (fm_sin_turns(ph) + 0.125f * fm_sin_turns(fph));
        if (db > 0.0f) {                         /* (coming in: its lateness from 0, a tape spinning up) */
            b = fm_minf(b + db, 1.0f);
            d *= b;
        }
        k = (int32_t)d;
        fr = d - (float)k;
        k0 = (pos - k) & (EFX_WOW_N - 1);
        k1 = (k0 - 1) & (EFX_WOW_N - 1);
        if (db >= 0.0f) {
            a0 = (float)w->line[0][k0], a1 = (float)w->line[0][k1];
            l[i] = (a0 + fr * (a1 - a0)) * (1.0f / 16384.0f);
            a0 = (float)w->line[1][k0], a1 = (float)w->line[1][k1];
            r[i] = (a0 + fr * (a1 - a0)) * (1.0f / 16384.0f);
        } else {                                 /* (going: crossfaded with the pair as it is) */
            b = fm_maxf(b + db, 0.0f);
            a0 = (float)w->line[0][k0], a1 = (float)w->line[0][k1];
            l[i] += b * ((a0 + fr * (a1 - a0)) * (1.0f / 16384.0f) - l[i]);
            a0 = (float)w->line[1][k0], a1 = (float)w->line[1][k1];
            r[i] += b * ((a0 + fr * (a1 - a0)) * (1.0f / 16384.0f) - r[i]);
        }
        pos = (pos + 1) & (EFX_WOW_N - 1);
    }
    w->ph = ph;
    w->fph = fph;
    w->pos = pos;
    w->b = b > 0.0f ? b : 1e-9f;                 /* (0 is fresh) */
}

/* ---- a slot on a pair (efx.h) ---- */
#define EFX_RISE 8                               /* a new effect's input from silence, live: 8 pieces of 32 frames */
static inline float glide_to(float v, float t, float f)   /* f of the way to t; at t once within a hair */
{
    v += (t - v) * f;
    return fm_fabsf(t - v) < 0.02f ? t : v;
}
int efx_slot_want(efx_slot_t *sl, int ef, int mode)
{
    const int was = sl->ef, wmode = was == EF_REVERB && sl->xt[0] < NRV ? sl->xt[0] : 0;
    if (sl->live && was != EF_OFF && sl->zef == was && !sl->fo && !sl->clr &&
        (ef != was || (ef == EF_REVERB && mode != wmode))) {
        sl->fo = 1;                              /* (the old one a block more, to dry) */
        return was;
    }
    sl->fo = 0;
    return ef;
}
/* a slot whose knobs or mix move, or that dips (fo: its effect fading out; a list's knob changed: to dry at its old
 * value, the new from dry), n frames (efx_slot_run). The dirt and the color: their numbers from the glides at the
 * block's end, ramped a sample at a time (char_ramp); the others: 32 frames at a time at the glides' values there,
 * wet, their mix ramped against the dry */
static __attribute__((noinline)) void slot_moving(efx_slot_t *sl, float bpm, const float *in, int16_t *mem, float *bl,
                                                  float *br, uint32_t n)
{
    const int ef = sl->ef, dip = sl->fo;
    const int up = ef == EF_CHORUS || ef == EF_LOFI || ef == EF_JUNO || ef == EF_DELAY || ef == EF_REVERB;   /* (their
                                                  * own mix made up: mix_up) */
    for (int c = 0; c < 3; c++)                  /* (a list's knob at once: juno's mode glides its own way) */
        if (EFX[ef].kind[c] == EK_LIST)
            sl->g[c] = sl->k[c];
    if (efx_is_char(ef)) {
        float *x = sl->u.ch.x, *z = sl->u.ch.z, t[8], a, tn;
        const float f = (float)n / ((float)n + EFX_GLIDE_N);
        if (ef != sl->cef) {                     /* (no numbers yet: from where the glides are) */
            efx_char_coefs(ef, sl->g[0] * (1.0f / 127.0f), sl->g[1] * (1.0f / 127.0f), EFX_RING_C4, x);
            x[5] = x[7] = sl->gm * (1.0f / 127.0f);
            sl->cef = (uint8_t)ef;
        }
        for (int c = 0; c < 3; c++)
            sl->g[c] = glide_to(sl->g[c], sl->k[c], f);
        sl->gm = dip ? 0.0f : glide_to(sl->gm, (float)sl->mix, f);
        a = sl->g[0] * (1.0f / 127.0f);
        tn = sl->g[1] * (1.0f / 127.0f);
        for (int i = 0; i < 8; i++)
            t[i] = x[i];
        efx_char_coefs(ef, a, tn, EFX_RING_C4, t);
        t[7] = sl->gm * (1.0f / 127.0f);
        if (ef == EF_RING)                       /* (flat after: its glide and ramp are these) */
            t[5] = t[7], z[6] = 1.0f, z[7] = 0.0f;
        sl->u.ch.in[0] = a;
        sl->u.ch.in[1] = tn;
        if (ef == EF_TAPE && mem)
            wow_run((efx_wow_t *)(void *)mem, (4.0f + 28.0f * a) * t[7], bl, br, n, dip ? 0.0f : 1.0f, x[7] == 0.0f);
        char_ramp(x, t, z, ef, bl, br, (int)n);
        if (ef == EF_RING)
            efx_ring_norm(z);
        return;
    }
    for (uint32_t j = 0; j < n; j += 32) {
        const uint32_t m = n - j < 32u ? n - j : 32u;
        const float f = (float)m / ((float)m + EFX_GLIDE_N), m0 = sl->gm;
        float dl[32], dr[32], m1, w, dw;
        for (int c = 0; c < 3; c++)
            if (EFX[ef].kind[c] != EK_LIST)
                sl->g[c] = glide_to(sl->g[c], sl->k[c], f);
        m1 = dip ? m0 * (float)(n - j - m) / (float)(n - j) : glide_to(m0, (float)sl->mix, f);
        for (uint32_t i = 0; i < m; i++)
            dl[i] = bl[j + i], dr[i] = br[j + i];
        if (sl->rise) {                          /* (a new effect, live: its input from silence too, so its lines start
                                                  * without a step) */
            float r = (float)(EFX_RISE - sl->rise) * (1.0f / EFX_RISE), dr_ = 1.0f / (EFX_RISE * (float)m);
            for (uint32_t i = 0; i < m; i++) {
                r += dr_;
                bl[j + i] *= r;
                br[j + i] *= r;
            }
            sl->rise--;
        }
        efx_insert(ef, &sl->u.in, &sl->ph, mem, sl->g, sl->xt, 127, efx_lfo_hz((int)sl->g[0], bpm), bpm, in ? in + j : 0,
                   bl + j, br + j, m);
        w = m0 * (1.0f / 127.0f);
        dw = (m1 - m0) * (1.0f / 127.0f) / (float)m;
        if (up) {                                /* (made up as at rest: mix_up, ramped across the piece) */
            float u = mix_up(w), du = (mix_up(m1 * (1.0f / 127.0f)) - u) / (float)m;
            for (uint32_t i = 0; i < m; i++) {
                w += dw;
                u += du;
                bl[j + i] = u * (dl[i] + w * (bl[j + i] - dl[i]));
                br[j + i] = u * (dr[i] + w * (br[j + i] - dr[i]));
            }
        } else
            for (uint32_t i = 0; i < m; i++) {
                w += dw;
                bl[j + i] = dl[i] + w * (bl[j + i] - dl[i]);
                br[j + i] = dr[i] + w * (br[j + i] - dr[i]);
            }
        sl->gm = m1;
    }
}
void efx_slot_run(efx_slot_t *sl, int both, float bpm, const float *in, int16_t *mem, float *bl, float *br, uint32_t n)
{
    const int ef = sl->ef;
    uint32_t j;
    if (ef != sl->zef) {                         /* another effect: its states start clean (live: from dry) */
        uint8_t *b = (uint8_t *)&sl->u;
        for (j = 0; j < (uint32_t)sizeof sl->u; j++)
            b[j] = 0;
        sl->cef = 0xFFu;
        sl->ph = 0.0f;
        sl->zef = (uint8_t)ef;
        for (j = 0; j < 3; j++)
            sl->g[j] = sl->k[j];
        sl->gm = sl->live ? 0.0f : (float)sl->mix;
        sl->rise = sl->live && !efx_is_char(ef) ? EFX_RISE : 0;
    }
    for (j = 0; !sl->live && j < n; j++)         /* (live once sound comes through it) */
        sl->live = bl[j] != 0.0f || br[j] != 0.0f;
    if (ef == EF_OFF || sl->clr)                 /* (its memory not yet clear: dry) */
        return;
    if (sl->fo || sl->gm != (float)sl->mix || sl->g[0] != sl->k[0] || sl->g[1] != sl->k[1] || sl->g[2] != sl->k[2]) {
        slot_moving(sl, bpm, in, mem, bl, br, n);   /* (a knob or the mix moving) */
        return;
    }
    if (efx_is_char(ef)) {
        float *x = sl->u.ch.x, *z = sl->u.ch.z, *cin = sl->u.ch.in;
        float a = sl->k[0] * (1.0f / 127.0f), tn = sl->k[1] * (1.0f / 127.0f);
        if (ef == EF_RING)                       /* (the voices' glide, char_prep, a step a block here) */
            a = efx_ring_glide(a, cin[0], &z[6]);
        if (ef != sl->cef || a != cin[0] || tn != cin[1]) {
            efx_char_coefs(ef, a, tn, EFX_RING_C4, x);
            sl->cef = (uint8_t)ef;
            cin[0] = a;
            cin[1] = tn;
        }
        x[7] = (float)sl->mix * (1.0f / 127.0f);
        if (ef == EF_RING) {                     /* the phasor back to unit length (char_prep); flat */
            efx_ring_from(x, z, 0, 0);
            efx_ring_norm(z);
        }
        if (ef == EF_TAPE && mem)                /* (its wow, where there is no pitch to bend: ~1 .. 6 cents at 0.55
                                                  * Hz by the amount, times the mix, not mixed: no comb with the dry) */
            wow_run((efx_wow_t *)(void *)mem, (4.0f + 28.0f * a) * x[7], bl, br, n, 1.0f, 0);
        for (j = 0; !both && j < n && bl[j] == br[j]; j++)
            ;
        if (!both && j == n) {                   /* (a pair whose sides are the same: once, its right side's states
                                                  * the left's) */
            efx_char_stage(x, z, ef, 0, bl, br, (int)n);
            for (j = 0; j < n; j++)
                br[j] = bl[j];
            z[1] = z[0];
            z[3] = z[2];
            z[7] = z[6];
        } else
            efx_char_stage(x, z, ef, 1, bl, br, (int)n);
        return;
    }
    efx_insert(ef, &sl->u.in, &sl->ph, mem, sl->k, sl->xt, sl->mix, efx_lfo_hz((int)sl->k[0], bpm), bpm, in, bl, br, n);
}
