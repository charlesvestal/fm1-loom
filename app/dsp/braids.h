/* SPDX-License-Identifier: MIT */
/* LOOM-8's two Braids engines: cane (the physical models) and voile (the voices). (gingham, the toys and chips, went
 * 2026-10-19.) Included by synth.c only (static code on its voice_t, no unit of its own).
 *
 * Ported from Mutable Instruments Braids (github.com/pichenettes/eurorack, braids/digital_oscillator.cc,
 * macro_oscillator.cc and the tables braids/resources/ makes), Copyright 2012 Emilie Gillet, MIT License
 * (the licence text: LICENSING.md); the port Copyright (c) 2026 Charles Vestal, MIT. Braids is fixed point at
 * 96 kHz (some models at half that); here every model is float at LOOM-8's 44.1 kHz: the delay lines' lengths
 * from the period in samples at 44.1 kHz, the one-poles and resonators moved to the same cutoff (a pole p at
 * 96 kHz is p^(96/44.1) here), the envelopes and decays in seconds, the oscillators' increments in turns of
 * 44.1 kHz. What each knob does is Braids': E2 is its TIMBRE, E3 its COLOR; E1 picks the model (Braids' shape)
 * and E4 is LOOM-8's own (cane: damp, voile: breath).
 *
 *   cane    pluck  PLUK: a Karplus-Strong string read at the note's rate from a line of one period (Braids'
 *                  stretched, randomised averaging); timbre the decay, color the pluck's width; damp softens
 *                  the pluck (a low-pass on the noise burst)
 *           bow    BOWD: a bowed string, two lines (bow to bridge, bow to nut), the friction curve, the bridge's
 *                  low-pass and the body's resonance; timbre the bow's pressure, color its place; damp the
 *                  bridge's losses
 *           reed   BLOW: a clarinet (a bore line, the reed's table; STK's numbers, at STK's 44.1 kHz again);
 *                  timbre the breath's steadiness (less noise), color the body's tone; damp the bore's losses
 *           flute  FLUT: a flute (STK's, as Braids'), a bore and a jet line and the jet's cubic; timbre the
 *                  breath's noise (high: clean), color how hard it is blown; damp darkens the bore
 *           bell   BELL: eleven partials of a bell (Braids' table), struck; timbre how long it rings (at the
 *                  top it drones), color detunes the partials apart; damp shortens the upper ones
 *           drum   DRUM: six partials of a drum head and filtered noise on two of them; timbre the decay,
 *                  color from tone to rattle; damp shortens the upper partials
 *   voile   vosim  VOSM: two formants (sines reset each period, under a bell-shaped window); timbre and color
 *                  the two formants' pitches
 *           vowel  VOWL: Braids' speech synth (three 4-bit formants, a consonant at each note); timbre the
 *                  vowel, color the formants' shift; breath the consonant's length
 *           choir  VFOF: five resonant formants on a saw, singers' data from bass to soprano; timbre the vowel
 *                  (a e i o u), color the voice
 *           whisper TWNQ: two resonant peaks (the note's and one color sets) on noise; timbre their
 *                  sharpness; breath 127 is all noise (Braids'), lower brings a buzz in under it
 *   (voile's breath: noise into the excitation, 0 clean)
 *
 * Changes from Braids, and why:
 *   - one note a voice: Braids' three overlapping plucked strings are LOOM-8's voices; the waveguides keep one
 *     model's lines in one of CN_SLOTS slots (int16, CN_LEN a slot: RAM, beside synth_t; synth.c gives a slot
 *     to a cane note and keeps cane to CN_SLOTS voices). Below the lines' reach (bow under ~37 Hz, flute under
 *     ~62 Hz) the line plays an octave up, as Braids' does;
 *   - the lines hold int16 (Braids' bowed and fluted ones int8): the same range, 256 times finer;
 *   - the bowing friction and the flute's jet are their formulas, not the tables' steps; the bell and the drum
 *     run their partials as rotating phasors (no sine table), at the full rate, partials above 0.45 x the rate
 *     silent; their decays per sample;
 *   - vowel: the three formants' sum is clamped before the window (Braids' int16 wraps at the loudest corner);
 *   - DC blockers after vosim, vowel and reed (their output carries the window's / the breath's DC);
 *   - in tune at 44.1 kHz (tests/host/braids_test measures it): pluck reads its line (N - p / 2) / N slower, p
 *     the averaging's chance (each pass of the averaging moves the wave half an entry on: +13 cents at C6
 *     otherwise); the bow's loop 0.9 samples and the reed's half loop 0.5 short of the period (the bridge's
 *     one-pole, the bore's two-tap average); the bow's bridge part at least 4 samples (at Braids' nearest it
 *     fell under one sample at high notes and stopped speaking);
 *   - flute: Braids' bore + jet of two periods with color moving the jet's length (and so the bore's) pulled
 *     the pitch by hundreds of cents here; the bore is STK's 1.5 periods (less the loop low-pass's delay, plus
 *     a fifth of the DC blocker's lead) and the jet half a period, and color is the breath's pressure
 *     (x 0.8 .. 1.2) instead; its DC blocker at 0.995 (Braids' 0.99 at 96 kHz is a 150 Hz high-pass in the
 *     loop: it led the pitch 60 cents at middle C), and the output's DC out;
 *   - choir: all five formants at one gain, as Braids' (its code reads the first formant's amplitude for each);
 *   - whisper: the buzz under the noise is LOOM-8's (breath, 127 = Braids' TWNQ);
 *   - Braids' parameter interpolation and its sync input are not here (LOOM-8's knobs move once a chunk). */

/* ------------------------------------------------------------- cane ---- */
enum { CN_PLUCK, CN_BOW, CN_REED, CN_FLUTE, CN_BELL, CN_DRUM };
#define CN_NECK 1216                     /* bow: bow to nut; bow to bridge after it (CN_LEN - CN_NECK) */
#define CN_BORE 1152                     /* flute: the bore; the jet after it */
#define CN_PLK 1024                      /* pluck: the longest string line (+1 guard) */
typedef union { int16_t s[CN_LEN]; float f[CN_LEN / 2]; } cn_slot_t;
static cn_slot_t cn_mem[CN_SLOTS];

/* the bell's and the drum's partials: offsets (1/128 semitone), strike amplitudes, decays per 1/4000 s
 * (Braids' render block), long and short (timbre between) */
static const int16_t BELL_P[11] = {-1284, -1283, -184, -183, 385, 1175, 1536, 2233, 2434, 2934, 3110};
static const int16_t BELL_A[11] = {8192, 5488, 8192, 14745, 21872, 13680, 11960, 10895, 10895, 6144, 10895};
static const uint16_t BELL_DL[11] = {65533, 65533, 65533, 65532, 65531, 65531, 65530, 65529, 65527, 65523, 65519};
static const uint16_t BELL_DS[11] = {65308, 65283, 65186, 65123, 64839, 64889, 64632, 64409, 64038, 63302, 62575};
static const int16_t DRUM_P[6] = {0, 0, 1041, 1747, 1846, 3072};
static const int16_t DRUM_A[6] = {16986, 2654, 3981, 5308, 3981, 2985};
static const uint16_t DRUM_DL[6] = {65533, 65531, 65531, 65531, 65531, 65516};
static const uint16_t DRUM_DS[6] = {65083, 64715, 64715, 64715, 64715, 62312};
/* the partials' state in the slot's floats: re, im, amplitude, cos, sin, decay a sample (6 x NP) */
#define PT_RE 0
#define PT_IM 11
#define PT_AMP 22
#define PT_C 33
#define PT_S 44
#define PT_D 55
#define PT_K 66                                  /* (the frequency and knobs they were made from: 4) */
/* the outputs' gains: each model's raw note near LOOM-8's engines' (a cycle peaking near 1; the pluck raised
 * 2026-10-19: its decaying phrases sat ~6 dB under the bank, its attack's peak now ~2.9 at the knobs' corners) */
static const float CN_GAIN[NCANE] = {4.2f, 2.7f, 1.45f, 0.8f, 2.0f, 1.2f};

/* what a voice of each model costs in the cores' split's units (an ordinary voice 4, ~1.1 % of a block a unit): as the
 * FM-1 measured them (tools/measure_load.sh 2026-10-18, two cores: cane's bell twice an ordinary voice, voile's choir
 * one and a half, the rest about one) */
static const uint8_t CN_COST[NCANE] = {4, 4, 4, 5, 8, 5};
static const uint8_t VO_COST[NVOILE] = {4, 4, 6, 4};
static inline float bd_mtof(float n) { return 440.0f * fm_exp2f((n - 69.0f) * (1.0f / 12.0f)); }
static inline float bd_note(float hz) { return 69.0f + 12.0f * fm_log2f(hz * (1.0f / 440.0f)); }
/* a one-pole's coefficient k at 96 kHz moved to 44.1 kHz (the same cutoff) */
static inline float bd_k96(float k) { return 1.0f - fm_exp2f(fm_log2f(1.0f - fm_minf(k, 0.999f)) * (96000.0f / SY_SR)); }
/* atan, within 0.005 rad */
static inline float bd_atan(float x)
{
    float a = fm_fabsf(x), r;
    r = a <= 1.0f ? a * (0.7853982f + 0.273f * (1.0f - a)) : 1.5707963f - (1.0f / a) * (0.7853982f + 0.273f * (1.0f - 1.0f / a));
    return x < 0.0f ? -r : r;
}
#define FL_DCP 0.995f                            /* the flute's DC blocker (Braids' 0.99 at 96 kHz led the pitch) */
/* fm_clampf's value (lo < hi; x not NaN) as the FPU's max and min: two instructions on the FM-1, where the
 * compares and selects take four */
static inline float bd_clamp(float x, float lo, float hi) { return fm_minf(fm_maxf(x, lo), hi); }
static inline int16_t bd_s16(float x) { return (int16_t)bd_clamp(x, -32767.0f, 32767.0f); }
/* a ring's tap di (+ fr) samples behind p, linearly interpolated (Braids' Mix of the two) */
SY_INLINE float bd_tap(const int16_t *d, int n, int p, int di, float fr)
{
    int a = p - di, b;
    if (a < 0)
        a += n;
    b = a - 1;
    if (b < 0)
        b += n;
    return (float)d[a] + (float)(d[b] - d[a]) * fr;
}
static inline float bd_od(float x) { return fm_tanhf(2.0f * x) * 1.0373147f; }   /* ws_moderate_overdrive */
/* bd_od for |x| <= 1 (voile's vowel and whisper): below |2x| = 0.5 fm_tanhf's own Taylor branch (the same value),
 * above it a polynomial in |2x| - 1.25 (within 1.3e-7 of tanh to |2x| = 2: fm_tanhf's exp2 and divide are
 * costly on the FM-1) */
static inline float vo_od(float x)
{
    float y = 2.0f * x, a = fm_fabsf(y), u, r;
    if (a < 0.5f)
        return fm_tanhf(y) * 1.0373147f;
    u = a - 1.25f;
    r = 1.690596691e-03f;
    r = r * u - 1.836245297e-03f;
    r = r * u - 4.638960119e-03f;
    r = r * u + 1.562794298e-02f;
    r = r * u - 1.920663565e-02f;
    r = r * u - 1.267151069e-02f;
    r = r * u + 1.083122119e-01f;
    r = r * u - 2.378656268e-01f;
    r = r * u + 2.804148197e-01f;
    r = r * u + 8.482835889e-01f;
    return (y < 0.0f ? -r : r) * 1.0373147f;
}
static inline int cn_model(float k) { int m = (int)(k + 0.5f); return m < 0 ? 0 : m >= NCANE ? NCANE - 1 : m; }

/* o[]: 0 the model the state is for (+1; 0 none), 1 a ring's place, 2 the second ring's, 3 samples since the
 * strike (the bow's and the breath's envelopes), 4 the loop's low-pass, 5 / 6 the body's biquad or the flute's
 * DC blocker, 7 reed's body filter, 8 pluck's noise low-pass / the drum's first, 9 pluck's read phase / the
 * drum's second, 10 pluck's write place / the drum's third, 11 pluck's burst left (-1: a strike to make),
 * 12 pluck's line length, 13 pluck's top step, 14 / 15 the DC blocker */
static void cn_clear(voice_t *v)
{
    int i;
    if (v->tws < CN_SLOTS)
        for (i = 0; i < CN_LEN; i++)
            cn_mem[v->tws].s[i] = 0;
    for (i = 1; i < 16; i++)
        v->o[i] = 0.0f;
}
static void cn_init(voice_t *v)
{
    v->o[0] = 0.0f;
    cn_clear(v);
    v->o[11] = -1.0f;
}
/* a note: a strike to make at the next chunk; clear = a new note on the voice (the waveguides start from rest,
 * as Braids' do), else a mono retrigger (the bow and the breath start again on the lines as they are) */
static void cn_strike(voice_t *v, int clear)
{
    if (clear)
        v->o[0] = 0.0f;                          /* the next prep clears the slot */
    v->o[11] = -1.0f;
}
static void cn_prep(voice_t *v)
{
    cfg_t *c = &v->c;
    int m = cn_model(c->ek[0]);
    float f = fm_clampf(c->freq, 16.0f, 9000.0f), tb = c->e[1], co = c->e[2], dp = c->e[3];
    c->stereo = 0;
    c->ex[19] = (float)m;
    if ((int)v->o[0] != m + 1) {                 /* a new model (or a new note): its state from rest */
        cn_clear(v);
        v->o[0] = (float)(m + 1);
        v->o[11] = -1.0f;
    }
    switch (m) {
    case CN_PLUCK: {
        float p0 = tb * 32767.0f, loss = 4096.0f - f * (262144.0f / 48000.0f);
        if (loss < 256.0f)
            loss = 256.0f;
        loss = p0 < 16384.0f ? loss * (16384.0f - p0) * (1.0f / 16384.0f) : 0.0f;
        c->ex[1] = (float)(int)loss;
        c->ex[2] = p0 < 16384.0f ? 65535.0f : (float)(131072 - ((int)p0 >> 3) * 31);
        c->ex[3] = 1.0f - dp * 0.97f;             /* the burst's low-pass: open at damp 0 */
        c->ex[4] = 0.125f + 0.75f * co;           /* the burst: a part of the line (Braids' 8192 + 1.5 color) */
        c->ex[5] = f;
        c->ex[6] = 0.5f * fm_minf(c->ex[2], 65536.0f) * (1.0f / 65536.0f);   /* the averaging's drift a pass */
        if (v->o[12] >= 2.0f)
            c->ex[0] = fm_minf(((float)v->o[12] - c->ex[6]) * f * INV_SR, v->o[13]);
        break;
    }
    case CN_BOW: {
        float d = SY_SR / f - 0.9f, br = fm_maxf(d * (6.0f + (float)((int)(co * 32767.0f) >> 9)) * (1.0f / 256.0f), fm_minf(4.0f, 0.2f * d)), nk = d - br;
        while (nk > (float)(CN_NECK - 2) || br > (float)(CN_LEN - CN_NECK - 2)) {
            nk *= 0.5f;
            br *= 0.5f;
        }
        c->ex[0] = fm_floorf(nk);
        c->ex[1] = nk - c->ex[0];
        c->ex[2] = fm_floorf(br);
        c->ex[3] = br - c->ex[2];
        c->ex[4] = (172.0f - (float)((int)(tb * 32767.0f) >> 8)) * (1.0f / (32.0f * 32768.0f));
        c->ex[5] = 0.521676f + dp * 0.38f;        /* the bridge's one-pole (Braids' 0.55 at 48 kHz), damp darker */
        c->ex[6] = (0.95f - 0.1f * dp) * (1.0f - c->ex[5]);
        break;
    }
    case CN_REED: {
        float d = 0.5f * SY_SR / f - 0.5f, np;
        while (d > (float)(CN_LEN - 2))
            d *= 0.5f;
        c->ex[0] = fm_floorf(d);
        c->ex[1] = d - c->ex[0];
        np = fm_clampf(bd_note(f) - 64.0f + co * 128.0f, 0.0f, 127.0f);
        c->ex[2] = bd_k96(fm_minf(0.7f, 0.4f * fm_exp2f((np - 69.0f) * (1.0f / 12.0f))));
        c->ex[3] = 0.8f * (28000.0f - tb * 16383.5f);   /* the breath's noise */
        c->ex[4] = -(0.94995f - 0.12f * dp);             /* the bore's reflection */
        break;
    }
    case CN_FLUTE: {
        /* STK's flute (Braids' is it): the bore 1.5 periods, the jet half a period (the two loops agree on the
         * note), less the loop's low-pass delay, plus the DC blocker's lead; color the breath's pressure */
        float pr = SY_SR / f, k = bd_k96(fm_minf(0.7f, 0.4f * fm_exp2f((bd_note(f) - 69.0f) * (1.0f / 12.0f))) * (1.0f - 0.6f * dp));
        float w = FM_TWO_PI * f * INV_SR, q = 1.0f - k, sw = fm_sinf(w), cw = fm_cosf(w);
        float tlp = bd_atan(q * sw / (1.0f - q * cw)) / w, tdc = bd_atan((1.0f - FL_DCP) / w) / w;
        float bo = 1.5f * pr - tlp + 0.2f * tdc, jt = 0.5f * pr;
        while (bo > (float)(CN_BORE - 2) || jt > (float)(CN_LEN - CN_BORE - 2)) {
            bo *= 0.5f;
            jt *= 0.5f;
        }
        c->ex[0] = fm_floorf(bo);
        c->ex[1] = bo - c->ex[0];
        c->ex[2] = fm_floorf(jt);
        c->ex[3] = jt - c->ex[2];
        c->ex[4] = k;
        c->ex[5] = (2100.0f - tb * 2047.9f) * (1.0f / 4096.0f);
        c->ex[6] = 0.8f + 0.4f * co;
        break;
    }
    case CN_BELL:
    case CN_DRUM: {
        int bell = m == CN_BELL, np = bell ? 11 : 6, i;
        float *pt = v->tws < CN_SLOTS ? cn_mem[v->tws].f : 0, p0 = tb * 32767.0f;
        int bal = ((32767 - (int)p0) >> 8);
        bal = bal * bal >> 7;
        if (!pt)
            break;
        /* the partials only when what makes them moved (the prep comes every fourth chunk even when nothing
         * did); PT_K holds what they were made from (a cleared slot: 0, never a note's frequency) */
        if (pt[PT_K] == f && pt[PT_K + 1] == tb && pt[PT_K + 2] == co && pt[PT_K + 3] == dp)
            np = 0;
        pt[PT_K] = f;
        pt[PT_K + 1] = tb;
        pt[PT_K + 2] = co;
        pt[PT_K + 3] = dp;
        for (i = 0; i < np; i++) {
            float off = (float)(bell ? BELL_P[i] : DRUM_P[i]), w, dl, ds, d, rate;
            if (bell)
                off += (float)((i & 1) ? ((int)(co * 32767.0f) >> 7) : -((int)(co * 32767.0f) >> 7));
            w = f * fm_exp2f(off * (1.0f / 1536.0f)) * INV_SR;
            if (w < 0.45f) {
                pt[PT_C + i] = fm_cosf(FM_TWO_PI * w);
                pt[PT_S + i] = fm_sinf(FM_TWO_PI * w);
            } else {                             /* past the band: silent (still) */
                pt[PT_C + i] = 1.0f;
                pt[PT_S + i] = 0.0f;
                pt[PT_AMP + i] = 0.0f;
            }
            dl = (float)(bell ? BELL_DL[i] : DRUM_DL[i]);
            ds = (float)(bell ? BELL_DS[i] : DRUM_DS[i]);
            d = (dl - (dl - ds) * (float)bal * (1.0f / 128.0f)) * (1.0f / 65536.0f);
            rate = fm_log2f(d) * (4000.0f * SUB / SY_SR) * (1.0f + dp * 4.0f * (float)i / (float)(np - 1));
            pt[PT_D + i] = p0 < 32000.0f ? fm_exp2f(rate) : 1.0f;   /* timbre at the top: no loss, it drones */
        }
        if (!bell) {                             /* the drum's noise: three one-poles at the note -12 + 64 x color */
            float fc = bd_mtof(bd_note(f) - 12.0f + co * 64.0f);
            int p1 = (int)(co * 32767.0f);
            c->ex[0] = 2.0f * fm_sinf(FM_PI * fm_minf(fc * INV_SR, 0.125f));
            c->ex[1] = p1 < 12888 ? (float)(p1 + 4096) * (1.0f / 16384.0f) : 1.0f;
            c->ex[2] = p1 < 16384 ? 0.0f : (float)((p1 - 16384) * 12888 >> 14) * (1.0f / 16384.0f);
        }
        break;
    }
    default: break;
    }
}
/* every chunk: a strike waiting; the partials' phasors trimmed to unit length */
static void cn_chunk(voice_t *v)
{
    cfg_t *c = &v->c;
    int m = (int)c->ex[19], i;
    float *pt = v->tws < CN_SLOTS ? cn_mem[v->tws].f : 0;
    if (!pt || (int)v->o[0] != m + 1)
        return;
    if (v->o[11] < 0.0f) {                       /* the strike */
        float f = fm_clampf(c->freq, 16.0f, 9000.0f);
        v->o[11] = 0.0f;
        v->o[3] = 0.0f;
        if (m == CN_PLUCK) {
            int n = CN_PLK;
            while (n > 8 && (float)n * f * INV_SR > 2.0f)
                n >>= 1;
            v->o[12] = (float)n;
            v->o[13] = 2.0f * (float)n * f * INV_SR;
            v->o[9] = 0.0f;
            v->o[10] = 0.0f;
            v->o[11] = (float)(int)((float)n * c->ex[4]);
            v->o[8] = 0.0f;
            c->ex[0] = fm_minf(((float)n - c->ex[6]) * f * INV_SR, v->o[13]);
        } else if (m == CN_BELL) {
            for (i = 0; i < 11; i++) {
                pt[PT_RE + i] = 0.0f;            /* Braids' 1 << 30: a quarter turn in */
                pt[PT_IM + i] = 1.0f;
                if (pt[PT_S + i] != 0.0f)
                    pt[PT_AMP + i] = (float)BELL_A[i] * (1.0f / 131072.0f);
            }
        } else if (m == CN_DRUM) {
            int reset = pt[PT_AMP] < 1024.0f / 65536.0f;
            for (i = 0; i < 6; i++) {
                if (reset) {
                    pt[PT_RE + i] = 0.0f;
                    pt[PT_IM + i] = 1.0f;
                }
                if (pt[PT_S + i] != 0.0f)
                    pt[PT_AMP + i] = (float)DRUM_A[i] * (1.0f / 65536.0f);
            }
        }
    }
    if (m == CN_BELL || m == CN_DRUM)
        for (i = 0; i < (m == CN_BELL ? 11 : 6); i++) {
            float g = 1.5f - 0.5f * (pt[PT_RE + i] * pt[PT_RE + i] + pt[PT_IM + i] * pt[PT_IM + i]);
            pt[PT_RE + i] *= g;
            pt[PT_IM + i] *= g;
        }
}

static void cn_pluck(voice_t *v, int16_t *dl, float *xl, int m, float g)
{
    const cfg_t *c = &v->c;
    int n = (int)v->o[12], w = (int)v->o[10], ini = (int)v->o[11], i;
    float ph = v->o[9], step = c->ex[0], nlp = v->o[8], kn = c->ex[3];
    int32_t loss = (int32_t)c->ex[1];
    uint32_t up = (uint32_t)c->ex[2], r = v->rng;
    if (n < 8) {
        for (i = 0; i < m; i++)
            xl[i] = 0.0f;
        return;
    }
    for (i = 0; i < m; i++) {
        float out;
        if (ini > 0) {                           /* the pluck: noise into the line, played as it goes in */
            int32_t x;
            ini--;
            nlp += kn * (frand(&r) - nlp);
            x = ((int32_t)dl[ini] + 3 * (int32_t)(nlp * 32767.0f)) >> 2;
            dl[ini] = (int16_t)x;
            if (ini == 0)
                dl[n] = dl[0];
            out = (float)x;
        } else {
            int rd, k;
            ph += step;
            if (ph >= (float)n)
                ph -= (float)n;
            rd = (int)ph + 2;
            if (rd >= n)
                rd -= n;
            while (w != rd) {                    /* the averaging chases the read head */
                int nx = w + 1 == n ? 0 : w + 1;
                if ((xs(&r) & 0xFFFFu) <= up) {
                    int32_t s = ((int32_t)dl[w] + dl[nx]) / 2;
                    if (loss)
                        s = s * (32768 - loss) >> 15;
                    dl[w] = (int16_t)s;
                }
                if (w == 0)
                    dl[n] = dl[0];
                w = nx;
            }
            k = (int)ph;
            out = (float)dl[k] + (float)(dl[k + 1] - dl[k]) * (ph - (float)k);
        }
        xl[i] = out * (1.0f / 32768.0f) * g;
    }
    v->rng = r;
    v->o[8] = nlp;
    v->o[9] = ph;
    v->o[10] = (float)w;
    v->o[11] = (float)ini;
}
#define BOW_ATT (0.025f * SY_SR)                 /* the bow's speed: up over 25 ms, back to 80 % over 5 */
#define BOW_DEC (0.005f * SY_SR)
static void cn_bow(voice_t *v, int16_t *dl, float *xl, int m, float g)
{
    const cfg_t *c = &v->c;
    int16_t *dn = dl, *db = dl + CN_NECK;
    const int nb = CN_LEN - CN_NECK;
    int pn = (int)v->o[1], pb = (int)v->o[2], ni = (int)c->ex[0], bi = (int)c->ex[2], i;
    float nf = c->ex[1], bf = c->ex[3], ks = c->ex[4], bp = c->ex[5], bg = c->ex[6];
    float t = v->o[3], lp = v->o[4], y0 = v->o[5], y1 = v->o[6];
    for (i = 0; i < m; i++) {
        float bv = bd_tap(db, nb, pb, bi, bf), nv = bd_tap(dn, CN_NECK, pn, ni, nf), br, nr, bow, vd, d, nw, tmp;
        lp = bv * bg + lp * bp;
        br = -lp;
        nr = -nv;
        bow = t < BOW_ATT ? t * (6553.6f / BOW_ATT)
                          : t < BOW_ATT + BOW_DEC ? 6553.6f - (t - BOW_ATT) * (1310.7f / BOW_DEC) : 5242.9f;
        t = fm_minf(t + 1.0f, BOW_ATT + BOW_DEC);
        vd = bow - (br + nr);
        d = fm_minf(fm_fabsf(vd) * ks, 3.99f) + 0.75f;   /* the friction: 1 / (|dv| + 0.75)^4, at most 1 */
        d *= d;
        nw = fm_minf(1.0f / (d * d), 1.0f) * vd;
        dn[pn] = bd_s16(br + nw);
        db[pb] = bd_s16(nr + nw);
        if (++pn >= CN_NECK)
            pn = 0;
        if (++pb >= nb)
            pb = 0;
        tmp = bv * 0.19998f + y0 * 1.671405f - y1 * 0.701929f;   /* the body: Braids' at 48 kHz, moved */
        xl[i] = bd_clamp((tmp - y1) * (1.0f / 32768.0f), -1.0f, 1.0f) * g;
        y1 = y0;
        y0 = tmp;
    }
    v->o[1] = (float)pn;
    v->o[2] = (float)pb;
    v->o[3] = t;
    v->o[4] = fm_flush(lp);
    v->o[5] = fm_flush(y0);
    v->o[6] = fm_flush(y1);
}
static void cn_reed(voice_t *v, int16_t *dl, float *xl, int m, float g)
{
    const cfg_t *c = &v->c;
    int p = (int)v->o[1], di = (int)c->ex[0], i;
    float fr = c->ex[1], k = c->ex[2], nd = c->ex[3], rf = c->ex[4];
    float lp = v->o[4], bo = v->o[7], dx = v->o[14], dy = v->o[15];
    uint32_t r = v->rng;
    for (i = 0; i < m; i++) {
        float br = 26214.0f + frand(&r) * nd, dv = bd_tap(dl, CN_LEN, p, di, fr), pd, reed, out;
        pd = dv * 0.5f + lp;                     /* the bore's two-tap average */
        lp = dv * 0.5f;
        pd = pd * rf - br;
        reed = bd_clamp(pd * -0.30005f + 22938.0f, -32767.0f, 32767.0f);
        out = bd_clamp(pd * reed * (1.0f / 32768.0f) + br, -32767.0f, 32767.0f);
        dl[p] = bd_s16(out);
        if (++p >= CN_LEN)
            p = 0;
        bo += k * (out - bo);                    /* the body */
        dy = bo - dx + 0.9985f * dy;             /* (and its DC) */
        dx = bo;
        xl[i] = dy * (1.0f / 32768.0f) * g;
    }
    v->rng = r;
    v->o[1] = (float)p;
    v->o[4] = lp;
    v->o[7] = bo;
    v->o[14] = dx;
    v->o[15] = fm_flush(dy);
}
#define FL_ATT (0.005f * SY_SR / 3.0f)           /* the breath: Braids' 1.7 ms up, 3.3 ms to 80 % */
#define FL_DEC (0.01f * SY_SR / 3.0f)
static void cn_flute(voice_t *v, int16_t *dl, float *xl, int m, float g)
{
    const cfg_t *c = &v->c;
    int16_t *db = dl, *dj = dl + CN_BORE;
    const int nj = CN_LEN - CN_BORE;
    int pb = (int)v->o[1], pj = (int)v->o[2], bi = (int)c->ex[0], ji = (int)c->ex[2], i;
    float bf = c->ex[1], jf = c->ex[3], k = c->ex[4], bn = c->ex[5], bp = c->ex[6];
    float t = v->o[3], lp = v->o[4], dx = v->o[5], dy = v->o[6], ox = v->o[14], oy = v->o[15];
    uint32_t r = v->rng;
    for (i = 0; i < m; i++) {
        float bv = bd_tap(db, CN_BORE, pb, bi, bf) * 2.0f, jv = bd_tap(dj, nj, pj, ji, jf) * 2.0f, br, rf, pd, xj;
        br = t < FL_ATT ? t * (42598.4f / FL_ATT) : t < FL_ATT + FL_DEC ? 42598.4f - (t - FL_ATT) * (8519.7f / FL_DEC) : 34078.7f;
        t = fm_minf(t + 1.0f, FL_ATT + FL_DEC);
        br *= bp;
        br += frand(&r) * bn * br;
        lp = -k * bv + (1.0f - k) * lp;          /* the bore's end: an inverting low-pass, then its DC out */
        dy = FL_DCP * dy + lp - dx;
        dx = lp;
        rf = dy;
        pd = br - rf * 0.5f;
        dj[pj] = bd_s16(pd * 0.5f);
        xj = bd_clamp(jv, 0.0f, 65535.0f) * (1.0f / 32768.0f);
        pd = fm_minf(xj * xj * xj - xj, 1.0f) * 32767.0f + rf * 0.5f;   /* the jet: x^3 - x */
        db[pb] = bd_s16(pd * 0.5f);
        if (++pb >= CN_BORE)
            pb = 0;
        if (++pj >= nj)
            pj = 0;
        oy = bv - ox + 0.9985f * oy;             /* the output's DC (the breath's) out */
        ox = bv;
        xl[i] = bd_clamp(oy * (0.5f / 32768.0f), -1.0f, 1.0f) * g;
    }
    v->rng = r;
    v->o[14] = ox;
    v->o[15] = fm_flush(oy);
    v->o[1] = (float)pb;
    v->o[2] = (float)pj;
    v->o[3] = t;
    v->o[4] = fm_flush(lp);
    v->o[5] = dx;
    v->o[6] = fm_flush(dy);
}
/* the partials over m samples: each a phasor turning, at its amplitude; the decays a sub-block at a time
 * (PT_D holds the decay of SUB samples: 0.36 ms steps, far under a step's hearing). Two partials at a time over
 * the m samples, added into xl in the partials' order (the same sums as a partial after another each sample):
 * their state stays in registers (the FM-1's floats share its sixteen general registers; eleven at once spilled
 * nearly all of them every sample) */
SY_INLINE void cn_part2(float *pt, int j, float *xl, int m, float g, const int two, const int first, const int last)
{
    float r0 = pt[PT_RE + j], i0 = pt[PT_IM + j], a0 = pt[PT_AMP + j], c0 = pt[PT_C + j], s0 = pt[PT_S + j];
    float r1 = two ? pt[PT_RE + j + 1] : 0.0f, i1 = two ? pt[PT_IM + j + 1] : 0.0f, a1 = two ? pt[PT_AMP + j + 1] : 0.0f;
    float c1 = two ? pt[PT_C + j + 1] : 0.0f, s1 = two ? pt[PT_S + j + 1] : 0.0f;
    int i;
    for (i = 0; i < m; i++) {
        float nr = r0 * c0 - i0 * s0, x;
        i0 = r0 * s0 + i0 * c0;
        r0 = nr;
        x = (first ? 0.0f : xl[i]) + i0 * a0;
        if (two) {
            nr = r1 * c1 - i1 * s1;
            i1 = r1 * s1 + i1 * c1;
            r1 = nr;
            x += i1 * a1;
        }
        xl[i] = last ? bd_clamp(x, -1.0f, 1.0f) * g : x;
    }
    pt[PT_RE + j] = r0;
    pt[PT_IM + j] = i0;
    if (two) {
        pt[PT_RE + j + 1] = r1;
        pt[PT_IM + j + 1] = i1;
    }
}
/* the bell's eleven partials (the last pass clips the sum and gives it the gain g) */
static void cn_bell(voice_t *v, float *pt, float *xl, int m, float g)
{
    int j;
    (void)v;
    cn_part2(pt, 0, xl, m, g, 1, 1, 0);
    for (j = 2; j < 10; j += 2)
        cn_part2(pt, j, xl, m, g, 1, 0, 0);
    cn_part2(pt, 10, xl, m, g, 0, 0, 1);
    for (j = 0; j < 11; j++)
        pt[PT_AMP + j] = fm_flush(pt[PT_AMP + j] * (m == SUB ? pt[PT_D + j] : fm_exp2f(fm_log2f(pt[PT_D + j]) * (float)m * (1.0f / SUB))));
}
/* the drum's partial j over m samples (in Braids' int16 units): its phasor turned, its sample into the partials'
 * sum hs and the mix t (the noise's share on partials 1 and 3, n2 the noise); the last one makes the output */
SY_INLINE void cn_dpart(voice_t *v, float *pt, const int j, const float *n2, float *hs, float *t, float *xl, int m,
                        float g)
{
    float re = pt[PT_RE + j], im = pt[PT_IM + j], am = pt[PT_AMP + j] * 32767.0f, co = pt[PT_C + j], sn = pt[PT_S + j];
    float hg = v->c.ex[1], ng = v->c.ex[2];
    int i;
    for (i = 0; i < m; i++) {
        float nr = re * co - im * sn, p;
        im = re * sn + im * co;
        re = nr;
        p = im * am;
        if (j == 0) {
            hs[i] = 0.0f + p;
            t[i] = p;
        } else if (j == 5) {
            float h = hs[i] + p;
            xl[i] = bd_clamp((t[i] + h * hg) * (1.0f / 32768.0f), -1.0f, 1.0f) * g;
        } else {
            hs[i] += p;
            if (j == 1)
                t[i] = t[i] + p * n2[i] * (1.0f / 256.0f) * (0.75f - ng);
            else if (j == 3)
                t[i] = t[i] + p * n2[i] * (1.0f / 512.0f) * ng;
        }
    }
    pt[PT_RE + j] = re;
    pt[PT_IM + j] = im;
    pt[PT_AMP + j] = fm_flush(am * (1.0f / 32767.0f) *
                              (m == SUB ? pt[PT_D + j] : fm_exp2f(fm_log2f(pt[PT_D + j]) * (float)m * (1.0f / SUB))));
}
/* the noise, then the six partials a pass each (their state in registers, not spilled each sample): every sum in
 * Braids' order (s = p0 + p1 x noise + p3 x noise + their sum x hg), so the same samples as all six each sample */
static void cn_drum(voice_t *v, float *pt, float *xl, int m, float g)
{
    float f = v->c.ex[0], l0 = v->o[8], l1 = v->o[9], l2 = v->o[10], n2[SUB], hs[SUB], t[SUB];
    uint32_t r = v->rng;
    int i;
    for (i = 0; i < m; i++) {
        float nz = bd_clamp(frand(&r) * 32768.0f, -16384.0f, 16384.0f);
        l0 += (nz - l0) * f;
        l1 += (l0 - l1) * f;
        l2 += (l1 - l2) * f;
        n2[i] = l2;
    }
    v->rng = r;
    v->o[8] = fm_flush(l0);
    v->o[9] = fm_flush(l1);
    v->o[10] = fm_flush(l2);
    cn_dpart(v, pt, 0, n2, hs, t, xl, m, g);
    cn_dpart(v, pt, 1, n2, hs, t, xl, m, g);
    cn_dpart(v, pt, 2, n2, hs, t, xl, m, g);
    cn_dpart(v, pt, 3, n2, hs, t, xl, m, g);
    cn_dpart(v, pt, 4, n2, hs, t, xl, m, g);
    cn_dpart(v, pt, 5, n2, hs, t, xl, m, g);
}
static void cn_run(voice_t *v, float *xl, float *xr, int m)
{
    int md = (int)v->c.ex[19], i;
    float g = CN_GAIN[md];
    (void)xr;
    if (v->tws >= CN_SLOTS || (int)v->o[0] != md + 1) {   /* no slot (never expected): silence */
        for (i = 0; i < m; i++)
            xl[i] = 0.0f;
        return;
    }
    switch (md) {
    case CN_PLUCK: cn_pluck(v, cn_mem[v->tws].s, xl, m, g); break;   /* (each model's gain at its last step) */
    case CN_BOW: cn_bow(v, cn_mem[v->tws].s, xl, m, g); break;
    case CN_REED: cn_reed(v, cn_mem[v->tws].s, xl, m, g); break;
    case CN_FLUTE: cn_flute(v, cn_mem[v->tws].s, xl, m, g); break;
    case CN_BELL: cn_bell(v, cn_mem[v->tws].f, xl, m, g); break;
    default: cn_drum(v, cn_mem[v->tws].f, xl, m, g); break;
    }
}

/* ------------------------------------------------------------ voile ---- */
enum { VO_VOSIM, VO_VOWEL, VO_CHOIR, VO_WHISPER };
static const float VO_GAIN[NVOILE] = {1.4f, 1.0f, 3.5f, 2.8f};
/* vowel: Braids' phonemes, the three formants' frequencies (x shift x 0.0916 Hz) and amplitudes (0..15) */
static const uint8_t VOWELS[9][6] = {{27, 40, 89, 15, 13, 1}, {18, 51, 62, 13, 12, 6}, {15, 69, 93, 14, 12, 7},
                                     {10, 84, 110, 13, 10, 8}, {23, 44, 87, 15, 12, 1}, {13, 29, 80, 13, 8, 0},
                                     {6, 46, 81, 12, 3, 0},    {9, 51, 95, 15, 3, 0},   {6, 73, 99, 7, 3, 14}};
static const uint8_t CONSONANTS[8][6] = {{6, 54, 121, 9, 9, 0},   {18, 50, 51, 12, 10, 5}, {11, 24, 70, 13, 8, 0},
                                         {15, 69, 74, 14, 12, 7}, {16, 37, 111, 14, 8, 1}, {18, 51, 62, 14, 12, 6},
                                         {6, 26, 81, 5, 5, 5},    {6, 73, 99, 7, 10, 14}};
/* Braids' wav_formant_sine and _square: 16 phases x 16 amplitudes (waveforms.py) */
static const int8_t FMT_SINE[256] = {
    0, 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 2,  2,  3,  3,  4,  5,  6,  7,  8,  10, 12, 14, 17, 20, 24,
    0, 3,  4,  5,  6,  7,  9,  10, 12, 15, 18, 21, 26, 31, 37, 45, 0, 4,  5,  6,  8,  9,  11, 13, 16, 19, 23, 28, 34, 40, 49, 58,
    0, 5,  6,  7,  8,  10, 12, 15, 17, 21, 25, 30, 36, 44, 53, 63, 0, 4,  5,  6,  8,  9,  11, 13, 16, 19, 23, 28, 34, 40, 49, 58,
    0, 3,  4,  5,  6,  7,  9,  10, 12, 15, 18, 21, 26, 31, 37, 45, 0, 2,  2,  3,  3,  4,  5,  6,  7,  8,  10, 12, 14, 17, 20, 24,
    0, 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, -2, -2, -3, -3, -4, -5, -6, -7, -8, -10, -12, -14, -17, -20, -24,
    0, -3, -4, -5, -6, -7, -9, -10, -12, -15, -18, -21, -26, -31, -37, -45, 0, -4, -5, -6, -8, -9, -11, -13, -16, -19, -23, -28, -34, -40, -49, -58,
    0, -5, -6, -7, -8, -10, -12, -15, -17, -21, -25, -30, -36, -44, -53, -63, 0, -4, -5, -6, -8, -9, -11, -13, -16, -19, -23, -28, -34, -40, -49, -58,
    0, -3, -4, -5, -6, -7, -9, -10, -12, -15, -18, -21, -26, -31, -37, -45, 0, -2, -2, -3, -3, -4, -5, -6, -7, -8, -10, -12, -14, -17, -20, -24};
static const int8_t FMT_SQ[16] = {0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 6, 8, 9, 11, 13, 16};   /* (+ the first half, - the second) */
/* choir: Braids' formant_f_data (1/128 semitone; voice bass, tenor, countertenor, alto, soprano x vowel a e i o u
 * x formant) */
static const int16_t FOF_F[5][5][5] = {
    {{9519, 10738, 12448, 12636, 12892}, {8620, 11720, 12591, 12932, 13158}, {7579, 11891, 12768, 13122, 13323},
     {8620, 10013, 12591, 12768, 13010}, {8324, 9519, 12591, 12831, 13048}},
    {{9696, 10821, 12810, 13010, 13263}, {8620, 11827, 12768, 13228, 13477}, {7908, 12038, 12932, 13263, 13452},
     {8620, 10156, 12768, 12932, 13085}, {8324, 9519, 12852, 13010, 13296}},
    {{9730, 10902, 12892, 13085, 13330}, {8832, 11953, 12852, 13085, 13296}, {7749, 12014, 13010, 13330, 13483},
     {8781, 10211, 12852, 13085, 13296}, {8448, 9627, 12892, 13085, 13363}},
    {{10156, 10960, 12932, 13427, 14195}, {8620, 11692, 12852, 13296, 14195}, {8324, 11827, 12852, 13550, 14195},
     {8881, 10156, 12956, 13427, 14195}, {8160, 9860, 12708, 13427, 14195}},
    {{10156, 10960, 13010, 13667, 14195}, {8324, 12187, 12932, 13489, 14195}, {7749, 12337, 13048, 13667, 14195},
     {8881, 10156, 12956, 13609, 14195}, {8160, 9860, 12852, 13609, 14195}}};
static inline int vo_model(float k) { int m = (int)(k + 0.5f); return m < 0 ? 0 : m >= NVOILE ? NVOILE - 1 : m; }
/* Braids' bell window for VOSIM (lut_bell: up over the first 1/16 of the period, down over the rest), as
 * smoothsteps (within 1 % of its Hann halves) */
static inline float vo_bell(float p)
{
    float x;
    if (p < 0.0625f) {
        x = p * 16.0f;
        return x * x * (3.0f - 2.0f * x);
    }
    x = (p - 0.0625f) * (16.0f / 15.0f);
    return 1.0f - x * x * (3.0f - 2.0f * x);
}
/* the consonant's (o[6]) formants into ex[7..13] */
static void vo_consonant(voice_t *v)
{
    cfg_t *c = &v->c;
    int k = (int)v->o[6], i;
    float sh = (200.0f + (float)((int)(c->e[2] * 32767.0f) >> 6)) * (96000.0f / 4294967296.0f * 4096.0f) * INV_SR;
    if (k < 0 || k > 7)
        k = 0;
    for (i = 0; i < 3; i++) {
        c->ex[7 + i] = (float)CONSONANTS[k][i] * sh;
        c->ex[10 + i] = (float)CONSONANTS[k][3 + i];
    }
    c->ex[13] = k >= 6 ? 0.0312f : 0.0f;         /* the last two: noisy resets (Braids' 4095) */
}
/* o[]: 0 the model (+1), 1 the period's phase; vosim 2 / 8 and 3 / 9 the formants' phasors (cos / sin), 4 / 5 its DC
 * blocker; vowel 2..4 the formants' phases, 5 the consonant's samples left, 6 which one, 7 a strike to make, 8 / 9
 * its DC blocker; choir 2..6 the formants' low-passes, 7..11 their band-passes, 12 / 13 the timbre and color
 * ex[1..5] were made from (14: 1 when they were); whisper 2..5 the two resonators, 6 the buzz's phase */
static void vo_init(voice_t *v)
{
    int i;
    for (i = 0; i < 16; i++)
        v->o[i] = 0.0f;
    v->o[7] = 1.0f;
}
static void vo_strike(voice_t *v) { v->o[7] = 1.0f; }
static void vo_prep(voice_t *v)
{
    cfg_t *c = &v->c;
    int m = vo_model(c->ek[0]), i;
    float f = c->freq, tb = c->e[1], co = c->e[2], b = c->e[3];
    c->stereo = 0;
    c->ex[19] = (float)m;
    if ((int)v->o[0] != m + 1) {
        float s7 = v->o[7];
        vo_init(v);
        v->o[0] = (float)(m + 1);
        v->o[7] = s7;
        if (m == VO_VOSIM)
            v->o[2] = v->o[3] = 1.0f;            /* (its formants: phasors at 1 + 0i) */
    }
    c->ex[0] = fm_minf(f * INV_SR, 0.45f);
    c->ex[18] = b;
    switch (m) {
    case VO_VOSIM:                               /* the formants' turns a sample, as rotations */
        c->ex[1] = fm_minf(bd_mtof(tb * 127.99f) * INV_SR, 0.49f);
        c->ex[2] = fm_minf(bd_mtof(co * 127.99f) * INV_SR, 0.49f);
        c->ex[3] = fm_cosf(FM_TWO_PI * c->ex[1]);
        c->ex[4] = fm_sinf(FM_TWO_PI * c->ex[1]);
        c->ex[5] = fm_cosf(FM_TWO_PI * c->ex[2]);
        c->ex[6] = fm_sinf(FM_TWO_PI * c->ex[2]);
        break;
    case VO_VOWEL: {
        int p0 = (int)(tb * 32767.0f), vi = p0 >> 12;
        float bal = (float)(p0 & 0xFFF) * (1.0f / 4096.0f);
        float sh = (200.0f + (float)((int)(co * 32767.0f) >> 6)) * (96000.0f / 4294967296.0f * 4096.0f) * INV_SR;
        for (i = 0; i < 3; i++) {
            c->ex[1 + i] = ((float)VOWELS[vi][i] * (1.0f - bal) + (float)VOWELS[vi + 1][i] * bal) * sh;
            c->ex[4 + i] = (float)(((int)VOWELS[vi][3 + i] * (0x1000 - (p0 & 0xFFF)) + (int)VOWELS[vi + 1][3 + i] * (p0 & 0xFFF)) >> 12);
        }
        vo_consonant(v);
        c->ex[14] = b * (0.08f * SY_SR);          /* the consonant's length: 0 .. 80 ms (Braids' 40 at 64) */
        break;
    }
    case VO_CHOIR: {
        float x = co * 4.0f, y = tb * 4.0f, xf, yf;
        int xi = (int)x, yi = (int)y;
        if (v->o[14] > 0.5f && v->o[12] == tb && v->o[13] == co)
            break;                               /* the formants as they are (the note does not move them) */
        v->o[12] = tb;
        v->o[13] = co;
        v->o[14] = 1.0f;
        if (xi > 3)
            xi = 3;
        if (yi > 3)
            yi = 3;
        xf = x - (float)xi;
        yf = y - (float)yi;
        for (i = 0; i < 5; i++) {
            float a = (float)FOF_F[xi][yi][i] + (float)(FOF_F[xi + 1][yi][i] - FOF_F[xi][yi][i]) * xf;
            float d = (float)FOF_F[xi][yi + 1][i] + (float)(FOF_F[xi + 1][yi + 1][i] - FOF_F[xi][yi + 1][i]) * xf;
            float fc = bd_mtof((a + (d - a) * yf) * (1.0f / 128.0f));
            c->ex[1 + i] = 2.0f * fm_sinf(FM_PI * fm_minf(fc * INV_SR, 0.125f));
        }
        break;
    }
    default: {                                   /* whisper: two resonators, the note's and color's */
        float r = fm_exp2f(fm_log2f((65240.0f + tb * 255.99f) * (1.0f / 65536.0f)) * (48000.0f / SY_SR));
        float n = bd_note(f), w1 = fm_minf(f * INV_SR, 0.24f), w2 = fm_minf(bd_mtof(n + (co - 0.5f) * 128.0f) * INV_SR, 0.24f);
        float r2 = r * r, ws[2];
        ws[0] = w1;
        ws[1] = w2;
        for (i = 0; i < 2; i++) {               /* each at unit RMS for white noise */
            float cs = fm_cosf(FM_TWO_PI * ws[i]), q = (1.0f + r2) * (1.0f + r2) - 4.0f * r2 * cs * cs;
            c->ex[1 + i] = 2.0f * r * cs;
            c->ex[3 + i] = fm_sqrtf((1.0f - r2) * fm_maxf(q, 1e-9f) / (1.0f + r2));
        }
        c->ex[5] = r2;
        c->ex[7] = 0.03f;                        /* the buzz: at the peaks it rings, so a little */
        c->ex[6] = 1.0f + (8191.0f - (float)((int)(tb * 32767.0f) >> 2)) * (1.0f / 8192.0f);   /* the makeup */
        break;
    }
    }
}
static void vo_chunk(voice_t *v)
{
    cfg_t *c = &v->c;
    if ((int)c->ex[19] == VO_VOWEL && v->o[7] > 0.5f) {   /* the vowel's consonant at a note */
        v->o[7] = 0.0f;
        v->o[6] = (float)((xs(&v->rng) >> 8) & 7u);
        v->o[5] = c->ex[14];
        vo_consonant(v);
    }
    v->o[7] = 0.0f;
}
/* the two formants as rotating phasors (o[2] / o[8] and o[3] / o[9]: cos / sin), back to 1 + 0i each period
 * (Braids' phases reset); a rotation is four multiplies where a sine is a polynomial */
static void vo_vosim(voice_t *v, float *xl, int m, float g)
{
    const cfg_t *c = &v->c;
    float p = v->o[1], ar = v->o[2], ai = v->o[8], br_ = v->o[3], bi = v->o[9], dx = v->o[4], dy = v->o[5];
    float d = c->ex[0], ca = c->ex[3], sa = c->ex[4], cb = c->ex[5], sb = c->ex[6], br = c->ex[18];
    uint32_t r = v->rng;
    int i;
    for (i = 0; i < m; i++) {
        float s, w, t;
        p += d;
        t = ar * ca - ai * sa;
        ai = ar * sa + ai * ca;
        ar = t;
        t = br_ * cb - bi * sb;
        bi = br_ * sb + bi * cb;
        br_ = t;
        if (p >= 1.0f) {                         /* a period: the formants start again */
            p -= 1.0f;
            ar = br_ = 1.0f;
            ai = bi = 0.0f;
            s = 0.0f;
        } else {
            w = vo_bell(p);
            s = (0.75f + 0.5f * ai + 0.25f * bi) * w;
            s = s + (frand(&r) * w - s) * br;    /* breath: noise under the window */
        }
        dy = (s - 0.75f) - dx + 0.9985f * dy;
        dx = s - 0.75f;
        xl[i] = dy * g;
    }
    v->rng = r;
    v->o[1] = p;
    v->o[2] = ar;
    v->o[8] = ai;
    v->o[3] = br_;
    v->o[9] = bi;
    v->o[4] = dx;
    v->o[5] = fm_flush(dy);
}
static void vo_vowel(voice_t *v, float *xl, int m, float g)
{
    const cfg_t *c = &v->c;
    float p = v->o[1], f1 = v->o[2], f2 = v->o[3], f3 = v->o[4], left = v->o[5], d = c->ex[0], dx = v->o[8], dy = v->o[9];
    int cons = left > 0.0f, i;
    const float *inc = cons ? c->ex + 7 : c->ex + 1, *amp = cons ? c->ex + 10 : c->ex + 4;
    int a1 = (int)amp[0], a2 = (int)amp[1], a3 = (int)amp[2];
    float nz = cons ? c->ex[13] : 0.0f;
    uint32_t r = v->rng;
    for (i = 0; i < m; i++) {
        int s, k3;
        float x, pn;
        p += d;
        if (p >= 1.0f)
            p -= 1.0f;
        f1 = step1(f1, inc[0]);
        f2 = step1(f2, inc[1]);
        f3 = step1(f3, inc[2]);
        k3 = (int)(f3 * 16.0f);
        s = FMT_SINE[((int)(f1 * 16.0f) << 4) | a1] + FMT_SINE[((int)(f2 * 16.0f) << 4) | a2] + (k3 < 8 ? FMT_SQ[a3] : -FMT_SQ[a3]);
        x = (float)(s < -128 ? -128 : s > 128 ? 128 : s) * (float)(255 - (int)(p * 256.0f));
        pn = nz > 0.0f ? p + frand(&r) * nz : p;
        if ((pn >= 0.0f && pn < d) || (pn >= 1.0f && pn < 1.0f + d)) {   /* the period: the formants' phases back to 0 */
            f1 = f2 = f3 = 0.0f;
            x = 0.0f;
        }
        x = vo_od(x * (1.0f / 32768.0f));
        dy = x - dx + 0.995f * dy;              /* its DC out (the window's ramp makes it one-sided) */
        dx = x;
        xl[i] = dy * g;
    }
    if (cons) {
        left -= (float)m;
        if (left < 0.0f)
            left = 0.0f;
    }
    v->rng = r;
    v->o[1] = p;
    v->o[2] = f1;
    v->o[3] = f2;
    v->o[4] = f3;
    v->o[5] = left;
    v->o[8] = dx;
    v->o[9] = fm_flush(dy);
}
/* Braids' SVF (Q 64): its band-pass */
SY_INLINE float vo_svf(float *lp, float *bp, float k, float in)
{
    float h;
    *lp = bd_clamp(*lp + k * *bp, -32767.0f, 32767.0f);
    h = in - *bp * (1.0f / 64.0f) - *lp;
    *bp = bd_clamp(*bp + k * h, -32767.0f, 32767.0f);
    return *bp;
}
/* the choir's formants j and j + 1 over the m samples into acc (the last pair: the output, at gain g). Two at a
 * time keep their state in registers (the FM-1's floats share its sixteen general registers: five at once
 * spilled most of them every sample); the sum in the order of five at once, so the same samples */
SY_INLINE void vo_fof2(voice_t *v, int j, const float *ins, float *acc, float *xl, int m, float g, const int last)
{
    float l0 = v->o[2 + j], b0 = v->o[7 + j], k0 = v->c.ex[1 + j], l1 = v->o[3 + j], b1 = v->o[8 + j], k1 = v->c.ex[2 + j];
    int i;
    for (i = 0; i < m; i++) {
        float in = ins[i], s = acc[i] + vo_svf(&l0, &b0, k0, in);
        s += vo_svf(&l1, &b1, k1, in);
        if (last)
            xl[i] = bd_clamp(s * (1.0f / (8.0f * 32768.0f)), -1.0f, 1.0f) * g;
        else
            acc[i] = s;
    }
    v->o[2 + j] = fm_flush(l0);
    v->o[7 + j] = fm_flush(b0);
    v->o[3 + j] = fm_flush(l1);
    v->o[8 + j] = fm_flush(b1);
}
static void vo_choir(voice_t *v, float *xl, int m, float g)
{
    const cfg_t *c = &v->c;
    float p = v->o[1], d = c->ex[0], id = 1.0f / d, br = c->ex[18], lp = v->o[2], bp = v->o[7], k = c->ex[1];
    float ins[SUB], acc[SUB];
    uint32_t r = v->rng;
    int i;
    for (i = 0; i < m; i++) {                    /* the saw (and the breath), and the first formant */
        float in;
        p = step1(p, d);
        in = (2.0f * p - 1.0f - blep(p, d, id)) * 16384.0f;
        in += (frand(&r) * 16384.0f - in) * br;  /* breath */
        ins[i] = in;
        acc[i] = 0.0f + vo_svf(&lp, &bp, k, in);
    }
    v->rng = r;
    v->o[1] = p;
    v->o[2] = fm_flush(lp);
    v->o[7] = fm_flush(bp);
    vo_fof2(v, 1, ins, acc, xl, m, g, 0);
    vo_fof2(v, 3, ins, acc, xl, m, g, 1);
}
static void vo_whisper(voice_t *v, float *xl, int m, float g)
{
    const cfg_t *c = &v->c;
    float y11 = v->o[2], y12 = v->o[3], y21 = v->o[4], y22 = v->o[5], ph = v->o[6], d = c->ex[0], id = 1.0f / d;
    float c1 = c->ex[1], c2 = c->ex[2], s1 = c->ex[3], s2 = c->ex[4], q2 = c->ex[5], mk = c->ex[6], br = c->ex[18];
    float gb = c->ex[7] * (1.0f - br);
    uint32_t r = v->rng;
    int i;
    for (i = 0; i < m; i++) {
        float x, a, b;
        ph = step1(ph, d);
        x = frand(&r) * 0.5f * br + (2.0f * ph - 1.0f - blep(ph, d, id)) * gb;   /* (Braids' noise >> 1) */
        a = x * s1 + y11 * c1 - y12 * q2;
        b = x * s2 + y21 * c2 - y22 * q2;
        y12 = y11;
        y11 = a;
        y22 = y21;
        y21 = b;
        xl[i] = vo_od(bd_clamp((a + b) * 0.1f * mk, -1.0f, 1.0f)) * g;
    }
    v->rng = r;
    v->o[2] = fm_flush(y11);
    v->o[3] = fm_flush(y12);
    v->o[4] = fm_flush(y21);
    v->o[5] = fm_flush(y22);
    v->o[6] = ph;
}
static void vo_run(voice_t *v, float *xl, float *xr, int m)   /* (each model's gain at its last step) */
{
    int md = (int)v->c.ex[19];
    float g = VO_GAIN[md];
    (void)xr;
    switch (md) {
    case VO_VOSIM: vo_vosim(v, xl, m, g); break;
    case VO_VOWEL: vo_vowel(v, xl, m, g); break;
    case VO_CHOIR: vo_choir(v, xl, m, g); break;
    default: vo_whisper(v, xl, m, g); break;
    }
}
