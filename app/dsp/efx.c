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
       W_REPEATS, W_FINE, W_WOBBLE, W_SMEAR, W_SIZE, W_DAMP, NW };
static const char *const WORD[NW] = {"-", "amount", "tone", "distort", "filter", "level", "bias", "which", "depth", "ratio",
                                     "pivot", "tilt", "rate", "bits", "smooth", "width", "pan", "freq", "feedback",
                                     "vibrato", "mode", "sense", "reso", "decay", "time", "repeats", "fine", "wobble",
                                     "smear", "size", "damp"};
#define KN(a, b, c) {W_##a, W_##b, W_##c
#define XS(a, b, c, d) W_##a, W_##b, W_##c, W_##d}
const efx_info_t EFX[NEF] = {
    {"off", EFG_NONE, 0, 0, KN(NONE, NONE, NONE), XS(NONE, NONE, NONE, NONE), {0, 0, 0}, {EK_NN, EK_NN, EK_NN}, {0, 0, 0, 0}},
    {"drive", EFG_DIRT, 0, 0, KN(AMOUNT, TONE, NONE), XS(NONE, NONE, NONE, NONE), {64, 64, 0}, {EK_PCT, EK_BIP, EK_NN},
     {0, 0, 0, 0}},
    {"rat", EFG_DIRT, 0, 0, KN(DISTORT, FILTER, LEVEL), XS(NONE, NONE, NONE, NONE), {70, 50, 80}, {EK_NN, EK_NN, EK_NN},
     {0, 0, 0, 0}},
    {"fold", EFG_DIRT, 0, 0, KN(AMOUNT, BIAS, NONE), XS(NONE, NONE, NONE, NONE), {64, 64, 0}, {EK_PCT, EK_BIP, EK_NN},
     {0, 0, 0, 0}},
    {"crush", EFG_DIRT, 0, 0, KN(AMOUNT, WHICH, NONE), XS(NONE, NONE, NONE, NONE), {64, 64, 0}, {EK_PCT, EK_BIP, EK_NN},
     {0, 0, 0, 0}},
    {"ring", EFG_DIRT, 0, 1, KN(DEPTH, RATIO, NONE), XS(NONE, NONE, NONE, NONE), {64, 64, 0}, {EK_PCT, EK_RATIO, EK_NN},
     {0, 0, 0, 0}},
    {"tilt", EFG_DIRT, 0, 0, KN(PIVOT, TILT, NONE), XS(NONE, NONE, NONE, NONE), {64, 64, 0}, {EK_NN, EK_BIP, EK_NN},
     {0, 0, 0, 0}},
    {"tape", EFG_COLOR, 0, 1, KN(AMOUNT, TONE, NONE), XS(NONE, NONE, NONE, NONE), {64, 64, 0}, {EK_PCT, EK_NN, EK_NN},
     {0, 0, 0, 0}},
    {"sampler", EFG_COLOR, 0, 0, KN(AMOUNT, TONE, NONE), XS(NONE, NONE, NONE, NONE), {64, 64, 0}, {EK_PCT, EK_NN, EK_NN},
     {0, 0, 0, 0}},
    {"lofi", EFG_COLOR, 2, 0, KN(RATE, BITS, SMOOTH), XS(WIDTH, NONE, NONE, NONE), {80, 80, 64}, {EK_NN, EK_NN, EK_PCT},
     {40, 0, 0, 0}},                         /* (its width reads its output back from a short line: 4 KB) */
    {"tremolo", EFG_MOVE, 0, 0, KN(RATE, DEPTH, PAN), XS(NONE, NONE, NONE, NONE), {4, 80, 0}, {EK_SYNC, EK_PCT, EK_PCT},
     {0, 0, 0, 0}},
    {"phaser", EFG_MOVE, 0, 0, KN(FREQ, DEPTH, RATE), XS(FEEDBACK, NONE, NONE, NONE), {50, 90, 50},
     {EK_NN, EK_PCT, EK_NN}, {80, 0, 0, 0}},
    {"univibe", EFG_MOVE, 0, 0, KN(RATE, DEPTH, VIBRATO), XS(NONE, NONE, NONE, NONE), {70, 90, 0},
     {EK_SYNC, EK_PCT, EK_PCT}, {0, 0, 0, 0}},
    {"chorus", EFG_MOVE, 2, 0, KN(RATE, DEPTH, FEEDBACK), XS(WIDTH, NONE, NONE, NONE), {50, 86, 20},
     {EK_NN, EK_PCT, EK_PCT}, {127, 0, 0, 0}},   /* (its line: 4 KB) */
    {"juno", EFG_MOVE, 1, 0, KN(MODE, TONE, NONE), XS(NONE, NONE, NONE, NONE), {0, 70, 0}, {EK_LIST, EK_NN, EK_NN},
     {0, 0, 0, 0}},                          /* (its line: 2 KB) */
    {"autowah", EFG_MOVE, 0, 0, KN(SENSE, RESO, DECAY), XS(NONE, NONE, NONE, NONE), {80, 70, 50}, {EK_NN, EK_NN, EK_NN},
     {0, 0, 0, 0}},
    {"delay", EFG_SPACE, 24, 0, KN(TIME, REPEATS, TONE), XS(FINE, WIDTH, WOBBLE, SMEAR), {83, 56, 64},
     {EK_DIV, EK_NN, EK_NN}, {64, 127, 0, 0}},   /* (its two lines, 44.8 KB, and smear's 4 KB) */
    {"rumble", EFG_SPACE, 2, 0, KN(DECAY, TONE, NONE), XS(NONE, NONE, NONE, NONE), {55, 80, 0}, {EK_NN, EK_NN, EK_NN},
     {0, 0, 0, 0}},                          /* (its lines: 4 KB) */
    {"reverb", EFG_SPACE, 32, 0, KN(SIZE, DECAY, TONE), XS(MODE, DAMP, NONE, NONE), {100, 40, 44}, {EK_NN, EK_NN, EK_BIP},
     {0, 72, 0, 0}},                         /* (its line: 64 KB; the plate's defaults, its second knob its motion) */
};
#undef KN
#undef XS
const char *efx_knob_name(int ef, int k)
{
    int w = ef >= 0 && ef < NEF && k >= 0 && k < 7 ? EFX[ef].word[k] : W_NONE;
    return k >= 3 && w == W_NONE ? 0 : WORD[w];
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
#define CH_REF 0.7f                              /* the drive keeps the level of a peak here (an engine's, about) */
#define CH_FOLD 0.6f                             /* the fold's level once it folds (a folded wave's RMS: about a saw's) */
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
/* the ring's amount a, glided from was (its last) unless it starts (*run 0: a new note or type; then 1), and its gains' starting
 * values for the ramp across the chunk (x[4], x[6]: char_loop goes from them to x[0], x[1]) */
__attribute__((noinline)) float efx_ring_glide(float a, float was, float *run, float *cx)
{
    if (*run == 0.0f) {
        *run = 1.0f;
        return a;
    }
    cx[4] = cx[0];
    cx[6] = cx[1];
    was += (a - was) * CH_RING_SLEW;
    return fm_fabsf(a - was) < 0.002f ? a : was;
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
        x[0] = x[4] = (1.0f - a) * m;           /* (x[4], x[6]: where the chunk's ramp starts; efx_ring_glide moves them) */
        x[1] = x[6] = a * m;
        x[2] = fm_cosf(w);                       /* the sine as a rotating phasor: z[4] + i z[5] */
        x[3] = fm_sinf(w);
        x[5] = CH_DC;
        break;
    }
    case EF_TAPE: {                              /* the drive's curve, gentler; a two-pole roll-off; the wow's depth */
        float g = 0.5f * ch_exp2(a * 4.0f);
        x[0] = g;
        x[1] = CH_REF / sat_c(g * CH_REF) * (1.0f - 0.15f * a);
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
        *dc += c5 * (y - *dc);
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
EF_INLINE void char_loop(const float *cx, float *cz, float *xl, float *xr, int k, const int type, const int stereo)
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
    float r0 = cx[4], r1 = cx[6], dr0 = 0.0f, dr1 = 0.0f;   /* (the ring: its gains ramped across the chunk) */
    if (type == EF_RING && k > 0) {
        dr0 = (c0 - r0) / (float)k;
        dr1 = (c1 - r1) / (float)k;
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
            mod = r0 + r1 * qi;
        } else if (type == EF_CRUSH) {
            ph += c2;
            if (ph >= 1.0f) {
                ph -= 1.0f;
                upd = 1;
            }
        }
        xl[j] = char_one(xl[j], type, c0, c1, c2, c3, c4, c5, wet, &z0, &z2, mod, upd);
        if (stereo)
            xr[j] = char_one(xr[j], type, c0, c1, c2, c3, c4, c5, wet, &z1, &z3, mod, upd);
    }
    cz[0] = fm_flush(z0);
    cz[1] = fm_flush(z1);
    cz[2] = fm_flush(z2);
    cz[3] = fm_flush(z3);
    cz[4] = ph;
    cz[5] = qi;
}
void efx_char_stage(const float *cx, float *cz, int type, int stereo, float *xl, float *xr, int k)
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
