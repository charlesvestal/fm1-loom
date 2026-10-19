/* SPDX-License-Identifier: GPL-3.0-only */
/* LOOM-8 drumline (drumline.h): the kit engine's eleven drum models on NDVOICE voices, each key through
 * its own envelope and filter.
 *
 * Ten models are analytic: a few oscillators, a noise source and a filter or two, shaped by exponential
 * envelopes (env *= k a sample): no tables, so they cost nothing in flash and a voice stays well under 80
 * instructions a sample. The eleventh, sample, plays a sample of the bank (dsp/sample.h: IMA ADPCM read in
 * place, a block of 65 decoded into the voice's cache at a time). The kick, snare, hat, clap, conga and tom descend
 * from X0X's 909 / 808 ports (drum909.c, drum808.c): the kick's pitch drop and click, the snare's two
 * detuned sines over band-passed noise, the hat's six square waves through high-passes, the clap's
 * four bursts; the shaker, tambourine, string (Karplus-Strong) and fm are our own.
 *
 * Each key of the kit is a model instance: par[TP_DRUM(key, DP_MODEL)] picks the model, DP_K1..K4
 * are the model's own four knobs (drumline.h DM_KNOB, read at the hit), DP_RANDOM varies those four
 * a little differently every hit, DP_PAN and DP_VOLUME (-30..+20 dB) place it, DP_MODE says how it
 * plays (one-shot, gated, loop, choke groups). Then the key's own stage, after the model: its filter (a
 * TPT state-variable section, 12 dB: low-pass, high-pass or band-pass; DP_CUT, DP_RES, the envelope's
 * reach DP_FENV), its drive (a tanh shape, the level made up at a typical drum peak) and its envelope
 * (DP_ATTACK a linear rise, DP_HOLD, DP_DECAY an exponential fall to -60 dB, off at 127: the model's own
 * length; DP_LAG a delay), the envelope as a function of the time since the hit, evaluated once a
 * sub-block (32 samples) and ramped between: a key left neutral costs nothing. The track's
 * par[TP_A..TP_R] are the kit's envelope, lag / attack / random / length: a delay before every hit, a
 * fade-in, a track-wide randomisation (the kick's pitch excepted) and a scale on every model's length.
 *
 * Levels: a balanced gain per key: every model is trimmed (SPEC[].trim) so that its defaults at
 * velocity 127 and volume 0 dB peak between -12 and -6 dBFS, the kick and snare on top (-6), the hats
 * and the shaker under them. The key's DP_VOLUME, DP_PAN and the hit's velocity are applied here;
 * the track's TP_SEND1/2/T scale the three send buffers (mono, pre track fader, pre filter). The track's TP_LEVEL and TP_PAN (and the punch-in mute) belong to the
 * engine layer, which applies them to all five buffers after drumline_render. Pan, volume and the
 * sends are re-read every SY_CHUNK samples and ramped across the chunk, so a knob turn is heard
 * without zipper noise. The dynamic filter (drumline_filter, FT_DYNAMIC) runs on the summed dry
 * output of the track: a 12 dB low cut and hi cut, a one-knob compressor and a tanh-shaped drive.
 *
 * Threads: drumline_hit / release / choke / filter and drumline_render run in one context (the
 * audio interrupt: the engine queues the keys' hits and the page's knobs). par[] is written by the
 * main loop a byte at a time. */
#include "drumline.h"
#include "synth.h"
#include "fastmath.h"

#define SR SY_SR
#define CR SY_CHUNK                   /* pan / volume / sends re-read this often */
#define SUB 32                        /* a voice renders this many at once into a 128 B stack buffer */
#define RESO_LEN 512                  /* a string's delay line, int16: 86 Hz at the longest */
#define NRESO 2                       /* strings: string sounds past two steal the older's line */
_Static_assert((RESO_LEN & (RESO_LEN - 1)) == 0, "a string's line wraps by a mask");

const char *const DM_NAME[NDM] = {"kick", "snare", "hat", "clap", "shaker", "tambourine", "string",
                                  "conga", "tom", "fm", "sample"};
const char *const DM_KNOB[NDM][4] = {
    [DM_KICK] = {"pitch", "length", "punch", "tone"},    [DM_SNARE] = {"pitch", "length", "wires", "punch"},
    [DM_HIHAT] = {"pitch", "length", "metal", "focus"},  [DM_CLAP] = {"tone", "length", "spread", "tail"},
    [DM_SHAKER] = {"color", "length", "swell", "return"}, [DM_TAMB] = {"tone", "length", "jingle", "hiss"},
    [DM_RESO] = {"pitch", "length", "bright", "pluck"},  [DM_CONGA] = {"pitch", "length", "slide", "drive"},
    [DM_TOM] = {"pitch", "length", "sweep", "rate"},     [DM_FM] = {"pitch", "length", "mod", "feedback"},
    [DM_SAMPLE] = {"sample", "pitch", "start", "reverse"},
};
/* the kit tuned to C (the pitch knobs, from start()'s mappings; measured by tests/host/drumline_test.c):
 *   kick   82: 16 Hz x 2^(82/127 x 2.5)       = 49.0 Hz  G1      length 42: -60 dB at about 0.17 s
 *   snare  79: 90 Hz x 2^(79/127 x 1.8)       = 195.6 Hz G3      wires 84 (was 64)
 *   hat    93: the 808's six metal oscillators x 2^((93/127 - 0.73) x 2) = x 1.003 (+5 cents)
 *   tom    70: 50 Hz x 2^(70/127 x 2.5)       = 130.0 Hz C3      (100: 195.7 Hz G3, the high tom)
 *   conga 109: 90 Hz x 2^(109/127 x 1.8)      = 262.6 Hz C4
 *   string 36: 60 Hz x 2^(36/127 x 4)         = 131.7 Hz C3      (a knob step is 38 cents here)
 *   fm    103: 55 Hz x 2^(round(103/127 x 48) / 12) = 55 x 2^(39/12) = 523.3 Hz C5
 * Each model's DM_DEFAULT is its key's row below (the tom: the low tom's); the tambourine has no key. */
const uint8_t DM_DEFAULT[NDM][4] = {
    [DM_KICK] = {82, 42, 64, 64},   [DM_SNARE] = {79, 40, 84, 64},  [DM_HIHAT] = {93, 23, 64, 88},
    [DM_CLAP] = {100, 64, 64, 38},  [DM_SHAKER] = {31, 64, 64, 101}, [DM_TAMB] = {80, 34, 72, 56},
    [DM_RESO] = {36, 70, 70, 100},  [DM_CONGA] = {109, 46, 64, 0},  [DM_TOM] = {70, 46, 38, 64},
    [DM_FM] = {103, 100, 46, 15},  [DM_SAMPLE] = {0, 64, 0, 0},
};
const char *const PM_NAME[NPM] = {"one-shot", "gated", "loop", "choke 1", "choke 2", "choke 3"};
const char *const DL_SOUND_NAME[DL_NSOUND] = {"kick", "snare", "hat", "open", "clap", "ltom", "htom", "conga",
                                              "shaker", "string", "fm"};
/* the default kit up the eleven black keys: kick, snare, closed hat, open hat (the same model with a long
 * length, choke group 1 with the closed), clap, low tom, high tom, conga, shaker, string, fm. The
 * tambourine has no key of its own: turn any key's model knob to it.
 * DP_MODEL, DP_K1..K4, DP_RANDOM, DP_PAN, DP_VOLUME (76 = 0 dB), DP_MODE, then the key's envelope and filter,
 * neutral (KEYN: attack 0, hold 0, decay off, lag 0; cutoff open, no resonance, no envelope, no drive, off), then
 * its sends to FX I and FX II (a little room on the clap and the fm key; the rest dry) */
#define VOL0 76
#define KEYN 0, 0, 127, 0, 127, 0, 64, 0, DF_OFF
const uint8_t DL_DEFAULT_KIT[DL_NSOUND][DP_N] = {
    {DM_KICK, 82, 42, 64, 64, 0, 64, VOL0, PM_ONESHOT, KEYN, 0, 0},
    {DM_SNARE, 79, 40, 84, 64, 0, 64, VOL0, PM_ONESHOT, KEYN, 0, 0},
    {DM_HIHAT, 93, 23, 64, 88, 0, 74, VOL0, PM_CHOKE1, KEYN, 0, 0},
    {DM_HIHAT, 93, 88, 64, 88, 0, 74, VOL0, PM_CHOKE1, KEYN, 0, 0},
    {DM_CLAP, 100, 64, 64, 38, 0, 54, VOL0, PM_ONESHOT, KEYN, 0, 24},
    {DM_TOM, 70, 46, 38, 64, 0, 44, VOL0, PM_ONESHOT, KEYN, 0, 0},
    {DM_TOM, 100, 46, 38, 64, 0, 84, VOL0, PM_ONESHOT, KEYN, 0, 0},
    {DM_CONGA, 109, 46, 64, 0, 0, 54, VOL0, PM_ONESHOT, KEYN, 0, 0},
    {DM_SHAKER, 31, 64, 64, 101, 0, 80, VOL0, PM_ONESHOT, KEYN, 0, 0},
    {DM_RESO, 36, 70, 70, 100, 0, 50, VOL0, PM_ONESHOT, KEYN, 0, 0},
    {DM_FM, 103, 100, 46, 15, 0, 64, VOL0, PM_ONESHOT, KEYN, 0, 12},
};
volatile uint8_t drumline_hitv[DL_NSOUND];

/* a model's length range, seconds to -60 dB at the length knob's ends, and its trim (the balanced gain) */
typedef struct { float dec_lo, dec_hi, trim; } dm_spec_t;
static const dm_spec_t SPEC[NDM] = {
    [DM_KICK] = {0.05f, 1.5f, 0.7f},    [DM_SNARE] = {0.06f, 1.0f, 0.37f}, [DM_HIHAT] = {0.02f, 2.5f, 1.8f},
    [DM_CLAP] = {0.05f, 1.2f, 0.6f},     [DM_SHAKER] = {0.02f, 0.6f, 0.67f},  [DM_TAMB] = {0.03f, 1.5f, 1.5f},
    [DM_RESO] = {0.05f, 2.5f, 0.585f},  [DM_CONGA] = {0.05f, 0.9f, 0.55f},  [DM_TOM] = {0.08f, 1.5f, 0.47f},
    [DM_FM] = {0.03f, 1.5f, 0.535f},     [DM_SAMPLE] = {0.05f, 1.5f, 0.5f},
};
/* the 808's six metal oscillators (sc808: 205.3, 304.4, 369.6, 522.7, 540, 800 Hz) and the tambourine's
 * jingles: six inharmonic partials of a small brass disc */
static const float METAL_HZ[6] = {205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f};
static const float JINGLE_HZ[6] = {4120.0f, 5230.0f, 6010.0f, 7340.0f, 8560.0f, 9870.0f};

typedef struct {
    uint8_t on, sound, model, line;   /* line: the string model's delay line, 0..NRESO-1 */
    uint8_t mode, held, hvel, hflags; /* the play mode; the key is still down; the hit, for a loop's restart */
    int8_t hpitch;
    uint8_t pad[3];
    uint32_t age, rng;
    int32_t cnt, bursts;              /* the clap / shaker: samples to the next burst, bursts left */
    int32_t wait;                     /* lag: samples of silence before the hit starts */
    float att, attk;                  /* attack: the fade-in gain and its step a sample */
    float env, k;                     /* the amplitude envelope and its per-sample multiplier */
    float env2, k2;                   /* a second envelope: click, body, burst, attack, FM index */
    float ph, inc, ph2, inc2;         /* oscillators, phase in turns [-0.5, 0.5) */
    float pd, pdk, pda;               /* pitch drop: inc * (1 + pda * pd), pd decays by pdk */
    float f1, f2, f3, f4;             /* filter states */
    float c1, c2, c3;                 /* filter coefficients */
    float a, b, g;                    /* two model amounts and the model's output gain */
    float gl, gr, gs1, gs2;           /* gains now: left, right, the two sends (set each chunk) */
    float dgl, dgr;                   /* left and right ramp toward the chunk's targets */
    uint32_t mp[6], mi[6];            /* the hat's / tambourine's six square phases and increments */
    int32_t len, w;                   /* the string: its length, write position (the sample: position, its start) */
    float fr, lp, fb, damp, pk;       /* its fractional length, loop filter, feedback, damping, peak (the
                                       * sample: the position's fraction) */
    /* the key's own stage (DP_ATTACK..DP_FTYPE) */
    uint32_t et;                      /* samples since the hit began (after the lag) */
    float eatt, ehold, edec;          /* the attack's end, the hold's end (samples); the decay's log a sample (0: off) */
    float eg;                         /* the envelope now (at the sub-block's start) */
    float fz1, fz2;                   /* the filter's states */
    uint8_t ftype, sidx, sci, rsv2;   /* the filter's type (DF_*); the sample model's sample, its cache + 1 */
} dvoice_t;

/* the dynamic filter: coefficients from drumline_filter, states per channel */
typedef struct {
    uint8_t on, lo_on, hi_on, drv_on;
    float lc, hc;                     /* one-pole coefficients, two in series each = 12 dB */
    float thr, slope, makeup;         /* the compressor: threshold (linear), dB taken per dB over, gain */
    float drive, dcomp;               /* tanh drive and its level compensation */
    float env, gain;                  /* the follower and the gain it last chose */
    float lo[4], hi[4];               /* low cut L1 L2 R1 R2, hi cut the same */
} dynf_t;

#define NDSC 4                        /* sample caches: sample keys sounding at once (a fifth takes the oldest's) */
struct drumline {
    const uint8_t *par;
    dvoice_t v[NDVOICE];
    smp_cache_t sc[NDSC];             /* the sample model's decoded windows (dsp/sample.h) */
    int8_t sc_owner[NDSC];            /* the voice holding each, -1 */
    dynf_t f;
    int8_t line_owner[NRESO];         /* the voice holding each string, -1 */
    int16_t line[NRESO][RESO_LEN];
};
_Static_assert(sizeof(struct drumline) <= DRUMLINE_BYTES, "drumline_t does not fit DRUMLINE_BYTES");
#ifdef PLAT_HOST
int drumline_sizeof(void) { return (int)sizeof(struct drumline); }
#endif

static inline float noise(uint32_t *r)
{
    *r = *r * 1664525u + 1013904223u;
    return (float)(int32_t)*r * (1.0f / 2147483648.0f);
}
/* a key's envelope time from its knob: 0 = none, else 1 ms .. 10 s (as the synth's envelopes, synth.c
 * env_seconds; the screen prints the same) */
static inline float key_secs(int v) { return v <= 0 ? 0.0f : 0.001f * fm_exp2f((float)v * (13.2877124f / 127.0f)); }
/* the multiplier that takes an envelope to -60 dB in t seconds */
static inline float dec_k(float t) { return fm_expf(-6.9078f / (fm_maxf(t, 0.001f) * SR)); }
/* a one-pole's coefficient for a cutoff in Hz */
static inline float lp_c(float hz) { return 1.0f - fm_expf(-FM_TWO_PI * fm_minf(hz, 18000.0f) / SR); }
/* a two-pole resonator (bandpass) at hz, bandwidth bw: y = x c3 + c1 y1 + c2 y2 */
static void reso_set(dvoice_t *v, float hz, float bw)
{
    float r = fm_expf(-FM_PI * bw / SR);
    hz = fm_minf(hz, 18000.0f);
    v->c1 = 2.0f * r * fm_cosf(FM_TWO_PI * hz / SR);
    v->c2 = -r * r;
    v->c3 = (1.0f - r) * 2.0f;
}
/* a sine of a phase in turns [-0.5, 0.5): the parabola 4x(1 - |x|) with Blinn's correction, harmonics
 * under -60 dB, a third of fm_sin_pi's cost: these are drums */
static inline float sin_t(float ph)
{
    float x = ph + ph, y = 4.0f * x * (1.0f - fm_fabsf(x));
    return y + 0.225f * (y * fm_fabsf(y) - y);
}
static inline float sin_t_any(float ph)                        /* any phase in turns */
{
    return sin_t(ph - fm_floorf(ph + 0.5f));
}
/* a cubic soft clip: x (1.5 - 0.5 x^2) inside +-1, flat outside */
static inline float clip3(float y)
{
    return y > 1.0f ? 1.0f : y < -1.0f ? -1.0f : y * (1.5f - 0.5f * y * y);
}
#define WRAP(ph) do { if ((ph) >= 0.5f) (ph) -= 1.0f; } while (0)
#define HF_SHORT 1                    /* hflags: the hit's short_gate and long_tail */
#define HF_LONG 2

void drumline_init(drumline_t *d, const uint8_t *par)
{
    uint32_t i;
    uint8_t *z = (uint8_t *)d;
    for (i = 0; i < sizeof *d; i++)
        z[i] = 0;
    d->par = par;
    d->f.gain = 1.0f;
    for (i = 0; i < NDSC; i++)
        d->sc_owner[i] = -1;
    for (i = 0; i < NRESO; i++)
        d->line_owner[i] = -1;
    for (i = 0; i < NDVOICE; i++)
        d->v[i].rng = 0x9E3779B9u * (i + 1);
}

/* a sample cache for voice vi: a free one (or one whose voice has moved on), else the oldest sample voice's
 * (that voice ends). Returns the cache + 1 (dvoice_t.sci) */
static uint8_t sc_claim(drumline_t *d, int vi)
{
    int i, best = -1;
    uint32_t oldest = 0;
    for (i = 0; i < NDSC; i++) {
        int o = d->sc_owner[i];
        if (o < 0 || o == vi || !d->v[o].on || d->v[o].model != DM_SAMPLE || d->v[o].sci != i + 1)
            best = i;
    }
    if (best < 0)
        for (i = 0; i < NDSC; i++)
            if (d->v[d->sc_owner[i]].age >= oldest) {
                oldest = d->v[d->sc_owner[i]].age;
                best = i;
            }
    if (d->sc_owner[best] >= 0 && d->sc_owner[best] != vi)
        d->v[d->sc_owner[best]].on = 0;
    d->sc_owner[best] = (int8_t)vi;
    smp_cache_reset(&d->sc[best]);
    return (uint8_t)(best + 1);
}

/* a voice for the sound: its own (a retrigger), a free one, else the oldest */
static dvoice_t *alloc(drumline_t *d, int sound)
{
    int i, best = -1;
    uint32_t oldest = 0;
    for (i = 0; i < NDVOICE; i++)
        if (d->v[i].on && d->v[i].sound == sound)
            return &d->v[i];
    for (i = 0; i < NDVOICE; i++)
        if (!d->v[i].on)
            return &d->v[i];
    for (i = 0; i < NDVOICE; i++)
        if (d->v[i].age >= oldest) {
            oldest = d->v[i].age;
            best = i;
        }
    return &d->v[best];
}

static void fade_out(dvoice_t *v)                     /* a choke: 5 ms to silence */
{
    float k = dec_k(0.005f);
    v->held = 0;
    if (v->wait > 0) {                                /* not started yet: nothing to fade */
        v->on = 0;
        return;
    }
    v->k = k;
    v->k2 = k;
    v->bursts = 0;
    if (v->model == DM_RESO) {
        v->fb = 0.3f;
        v->env2 = 0.0f;
    }
}

void drumline_choke(drumline_t *d)
{
    int i;
    for (i = 0; i < NDVOICE; i++)
        if (d->v[i].on)
            fade_out(&d->v[i]);
}

void drumline_release(drumline_t *d, int sound)
{
    int i;
    for (i = 0; i < NDVOICE; i++) {
        dvoice_t *v = &d->v[i];
        if (!v->on || v->sound != sound)
            continue;
        v->held = 0;
        if (v->mode == PM_GATED)
            fade_out(v);
    }
}

/* the knob with its random variations: the key's DP_RANDOM and the track's (TP_S), each up to about a
 * third of the range either way; the kick's pitch takes only the key's */
static float knob(const uint8_t *p, int i, float amt, uint32_t *rng)
{
    float x = (float)p[DP_K1 + i] * (1.0f / 127.0f);
    if (amt > 0.0f)
        x = fm_clampf(x + noise(rng) * amt * 0.33f, 0.0f, 1.0f);
    return x;
}

/* the hit itself, on a voice already chosen: with_lag = the envelope's delay applies (not on a loop's
 * restart) */
static void start(drumline_t *d, dvoice_t *v, int sound, int vel, int pitch_semi, int flags, int with_lag)
{
    const uint8_t *par = d->par, *p = par + TP_DRUM(sound, 0);
    const dm_spec_t *sp;
    int model = p[DP_MODEL] % NDM, i;
    float k1, k2, k3, k4, dec, hz = 0.0f, semi = (float)pitch_semi;
    float rnd_key = (float)p[DP_RANDOM] * (1.0f / 127.0f), rnd_trk = (float)par[TP_S] * (1.0f / 127.0f);
    float rnd = fm_minf(1.0f, rnd_key + rnd_trk);
    float lscale = fm_exp2f(((float)par[TP_R] - 64.0f) * (1.0f / 32.0f));   /* length: 0.25x .. 4x */
    sp = &SPEC[model];
    if (v->model == DM_RESO && model != DM_RESO && d->line_owner[v->line] == (int)(v - d->v))
        d->line_owner[v->line] = -1;                  /* it leaves its string (also one choked while waiting) */
    {   /* the voice's constant part: the gains start at the chunk's first control read */
        uint32_t rng = v->rng;
        uint8_t line = v->line;
        int keep_line = v->on && v->model == DM_RESO && model == DM_RESO;
        uint8_t *z = (uint8_t *)v;
        uint32_t n;
        for (n = 0; n < sizeof *v; n++)
            z[n] = 0;
        v->rng = rng;
        v->line = keep_line ? line : 0;
        v->on = 1;
        v->sound = (uint8_t)sound;
        v->model = (uint8_t)model;
        v->mode = (uint8_t)(p[DP_MODE] % NPM);
        v->held = (uint8_t)(v->mode == PM_GATED || v->mode == PM_LOOP);
        v->hvel = (uint8_t)vel;
        v->hpitch = (int8_t)fm_clampf(semi, -127.0f, 127.0f);
        v->hflags = (uint8_t)flags;
        if (!keep_line && model == DM_RESO) {         /* a string: a free one, else the older owner's */
            int li = -1, oldest = -1;
            for (i = 0; i < NRESO; i++)
                if (d->line_owner[i] < 0) {
                    li = i;
                    break;
                }
            if (li < 0) {
                uint32_t age = 0;
                for (i = 0; i < NRESO; i++) {
                    dvoice_t *o = &d->v[d->line_owner[i]];
                    if (o->age >= age) {
                        age = o->age;
                        li = i;
                        oldest = d->line_owner[i];
                    }
                }
                d->v[oldest].on = 0;
            }
            d->line_owner[li] = (int8_t)(v - d->v);
            v->line = (uint8_t)li;
            for (i = 0; i < RESO_LEN; i++)
                d->line[li][i] = 0;
        }
    }
    k1 = knob(p, 0, model == DM_KICK ? rnd_key : rnd, &v->rng);
    k2 = knob(p, 1, rnd, &v->rng);
    k3 = knob(p, 2, rnd, &v->rng);
    k4 = knob(p, 3, rnd, &v->rng);
    dec = sp->dec_lo * fm_powf(sp->dec_hi / sp->dec_lo, k2) * lscale;
    if (flags & HF_SHORT)
        dec = fm_minf(dec, 0.06f);
    if (flags & HF_LONG)
        dec *= 4.0f;
    v->env = 1.0f;
    v->k = dec_k(dec);
    v->env2 = 1.0f;
    v->k2 = v->k;
    v->g = (float)vel * (1.0f / 127.0f) * sp->trim;   /* the velocity: a gain; the chunk adds volume / pan */
    /* the track envelope: lag = the start waits 0..60 ms (and the key's own lag, another 0..60), attack = a
     * fade-in of 0..100 ms */
    v->wait = with_lag ? (int32_t)((float)(par[TP_A] + p[DP_LAG]) * (0.060f / 127.0f) * SR) : 0;
    {   /* the key's envelope: attack, hold, decay (off at 127) */
        float a = key_secs(p[DP_ATTACK]) * SR, hd = key_secs(p[DP_HOLD]) * SR;
        float dk = p[DP_DECAY] >= 127 ? 0.0f : fm_maxf(key_secs(p[DP_DECAY]), 0.001f);
        if (model == DM_SAMPLE && (flags & HF_SHORT))
            dk = dk > 0.0f ? fm_minf(dk, 0.06f) : 0.06f;   /* a short step: as the models' 60 ms */
        if (flags & HF_LONG)
            dk *= 4.0f;
        v->eatt = a;
        v->ehold = a + hd;
        v->edec = dk > 0.0f ? -6.9078f / (fm_maxf(dk, 0.001f) * SR) : 0.0f;
        v->eg = a > 0.0f ? 0.0f : 1.0f;
        v->et = 0;
    }
    {
        float at = (float)par[TP_D] * (0.100f / 127.0f) * SR;
        v->att = at < 1.0f ? 1.0f : 0.0f;
        v->attk = at < 1.0f ? 0.0f : 1.0f / at;
    }

    switch (model) {
    case DM_KICK:                                     /* a sine falling from punch x its pitch over ~30 ms;
                                                       * punch = the drop, the click and the drive; tone = the
                                                       * body's low-pass and the click's band (the beater: noise
                                                       * band-passed after the low-pass, so a dark kick still
                                                       * has its attack) */
        hz = 16.0f * fm_exp2f(k1 * 2.5f + semi * (1.0f / 12.0f));
        v->pda = 1.0f + 5.0f * k3;
        v->pd = 1.0f;
        v->pdk = dec_k(0.025f + 0.03f * k3);
        v->k2 = dec_k(0.004f);                        /* the click: a 4 ms noise burst */
        v->a = 1.0f + 2.0f * k3;                      /* drive */
        v->b = 1.0f * k3;                             /* click level */
        v->c1 = lp_c(200.0f * fm_exp2f(6.0f * k4));
        v->c2 = lp_c(800.0f + 1200.0f * k4);          /* the click: noise in a band (the beater: a high-pass, two */
        v->c3 = lp_c(2500.0f + 5000.0f * k4);         /* low-passes, all with tone), after the body's low-pass */
        break;
    case DM_SNARE:                                    /* two sines (1 : 1.58) over band-passed noise: wires =
                                                       * the wires' share, punch = the drop and the drive */
        hz = 90.0f * fm_exp2f(k1 * 1.8f + semi * (1.0f / 12.0f));
        v->inc2 = hz * 1.58f / SR;
        v->pda = 0.2f + 1.0f * k4;
        v->pd = 1.0f;
        v->pdk = dec_k(0.012f);
        v->k2 = dec_k(dec * 0.5f);                    /* the body falls faster than the wires */
        v->a = (1.0f - 0.6f * k3) * (1.0f + 1.5f * k4);   /* body drive */
        v->b = 0.15f + 1.1f * k3;                     /* the wires */
        reso_set(v, 3400.0f, 5000.0f);                /* the wires: bright, wide (they sat at 2 kHz, 2.5 wide: dull) */
        break;
    case DM_HIHAT: {                                  /* metal: noise (left) to the six squares and their
                                                       * ring-mod (right); focus: the high-passed shimmer (left)
                                                       * to a bandpass focus (right) */
        float ratio = fm_exp2f((k1 - 0.73f) * 2.0f + semi * (1.0f / 12.0f));
        for (i = 0; i < 6; i++) {
            v->mi[i] = (uint32_t)(METAL_HZ[i] * ratio * (4294967296.0f / SR));
            v->mp[i] = v->rng * (uint32_t)(i * 0x3C6EF372u);
        }
        v->pdk = lp_c(3000.0f * fm_sqrtf(ratio));    /* the high-passes' coefficient (no pitch drop here) */
        v->k2 = dec_k(0.003f);                        /* a touch of click at the start */
        v->a = k3;
        v->b = k4;
        reso_set(v, 7000.0f * fm_sqrtf(ratio), 1500.0f);
        v->g *= 1.0f + 0.5f * k4;
        break;
    }
    case DM_CLAP:                                     /* four bursts, spread = their spacing and their
                                                       * length, then a tail (its level and its band) */
        hz = 400.0f * fm_exp2f(k1 * 2.3f + semi * (1.0f / 12.0f));
        v->k2 = dec_k(0.005f + 0.015f * k3);
        v->cnt = (int32_t)((0.005f + 0.012f * k3) * SR);
        v->a = (float)v->cnt;
        v->bursts = 3;
        v->b = 0.25f + 0.9f * k4;                     /* the tail's level */
        reso_set(v, hz, 900.0f + 600.0f * k4);
        break;
    case DM_SHAKER:                                   /* noise through a resonator (color), a fade-in
                                                       * (swell), then the shake back (return = how soon) */
        hz = 2000.0f * fm_exp2f(k1 * 2.2f + semi * (1.0f / 12.0f));
        v->k2 = dec_k(0.002f + 0.06f * k3);           /* the swell: 2 .. 60 ms */
        v->cnt = (int32_t)(0.2f * fm_exp2f(-3.0f * k4) * SR);
        v->bursts = 1;
        reso_set(v, hz, 2500.0f - 1500.0f * k1);
        v->g *= 1.0f + 0.5f * k1;                     /* a narrow band is quieter in RMS, peakier */
        break;
    case DM_TAMB: {                                   /* jingles: six high partials (jingle = how long they
                                                       * ring, their share) and a high-passed noise (hiss = its share and
                                                       * its ring-out); tone = brightness */
        float ratio = fm_exp2f((k1 - 0.73f) * 0.8f + semi * (1.0f / 12.0f));
        for (i = 0; i < 6; i++) {
            v->mi[i] = (uint32_t)(JINGLE_HZ[i] * ratio * (4294967296.0f / SR));
            v->mp[i] = v->rng * (uint32_t)(i * 0x3C6EF372u + 0x9E3779B9u);
        }
        v->c1 = lp_c(2500.0f * fm_exp2f(1.5f * k1));
        v->k2 = dec_k(fm_maxf(0.01f, dec * (0.15f + 0.85f * k3)));   /* the partials */
        v->k = dec_k(fm_maxf(0.01f, dec * (0.1f + 0.9f * k4)));      /* the noise */
        v->a = 0.35f + 0.65f * k4;                    /* the noise's share */
        v->b = 0.3f + 0.7f * k3;                      /* the partials' share */
        break;
    }
    case DM_RESO: {                                   /* a noise burst into a Karplus-Strong string: bright =
                                                       * the loop's damping, pluck = the burst's length,
                                                       * level and brightness */
        float len;
        hz = 60.0f * fm_exp2f(k1 * 4.0f + semi * (1.0f / 12.0f));
        len = SR / hz;
        if (len > (float)(RESO_LEN - 2))
            len = (float)(RESO_LEN - 2);
        if (len < 4.0f)
            len = 4.0f;
        v->len = (int32_t)len;
        v->fr = len - (float)v->len;
        v->fb = fm_exp2f(-9.966f / (dec * hz));       /* -60 dB after dec seconds of round trips */
        if (v->fb > 0.999f)
            v->fb = 0.999f;
        v->damp = 0.15f + 0.85f * k3;                 /* the loop's low-pass: bright */
        v->k2 = dec_k(fm_maxf(0.004f, (1.5f + 3.0f * k4) * len / SR));
        v->a = 0.5f + 0.5f * k4;                      /* the burst's level */
        v->c1 = lp_c(2000.0f * fm_exp2f(3.0f * k4));  /* and its brightness */
        v->pk = 1.0f;
        break;
    }
    case DM_CONGA:                                    /* a pitch slide (slide: how far and how fast) with a
                                                       * second partial, through a soft clip (drive) */
        hz = 90.0f * fm_exp2f(k1 * 1.8f + semi * (1.0f / 12.0f));
        v->inc2 = hz * 2.4f / SR;
        v->pda = 0.1f + 1.4f * k3;
        v->pd = 1.0f;
        v->pdk = dec_k(0.004f + 0.02f * k3);
        v->k2 = dec_k(dec * 0.35f);
        v->a = 1.0f + 7.0f * k4;                      /* drive */
        v->b = 0.25f + 0.5f * k3;                     /* the partial's share */
        v->c1 = 1.0f / (1.0f + 0.6f * k4);            /* the drive's level compensation */
        break;
    case DM_TOM:                                      /* a sine sweeping down (sweep = how far, rate = how
                                                       * fast: the syndrum), a little filtered noise */
        hz = 50.0f * fm_exp2f(k1 * 2.5f + semi * (1.0f / 12.0f));
        v->pda = 3.0f * k3;
        v->pd = 1.0f;
        v->pdk = dec_k(0.5f * fm_exp2f(-4.6f * k4));
        v->k2 = dec_k(0.03f);
        v->c1 = lp_c(1500.0f);
        break;
    case DM_SAMPLE: {                                 /* a sample of the bank: pitch about its own rate, start,
                                                       * reverse (from its end back to the start) */
        int idx = p[DP_K1] < smp_slots() ? p[DP_K1] : smp_slots() - 1;
        const smp_t *sm = smp_get_far(idx);   /* (drumline_hit runs from RAM: sample.h) */
        v->sidx = (uint8_t)idx;
        v->sci = sc_claim(d, (int)(v - d->v));
        v->k = v->k2 = 1.0f;                          /* (its length is the sample's: the env only chokes) */
        v->env2 = 0.0f;
        if (!sm) {                                    /* an empty slot: nothing, the voice ends at once */
            v->env = 0.0f;
            break;
        }
        {   /* the pitch knob (40..88: +-24 semitones), its random a fifth of the others', the hit's transpose */
            float st = fm_clampf(knob(p, 1, rnd * 0.2f, &v->rng) * 127.0f - 64.0f, -24.0f, 24.0f) + semi;
            v->inc = fm_minf(((float)sm->rate / SR) * fm_exp2f(st * (1.0f / 12.0f)), 4.0f);
        }
        v->w = (int32_t)(fm_minf(k3, 0.99f) * (float)sm->n);   /* the start */
        v->a = (p[DP_K4] & 1u) ? -1.0f : 1.0f;
        v->len = v->a > 0.0f ? v->w : (int32_t)sm->n - 1;
        v->fr = 0.0f;
        break;
    }
    case DM_FM: {                                     /* 2-op: stepped pitch, mod = the index and the
                                                       * modulator's ratio, feedback = the modulator's
                                                       * self-modulation */
        float steps = fm_floorf(k1 * 48.0f + 0.5f) + semi;
        hz = 55.0f * fm_exp2f(steps * (1.0f / 12.0f));
        v->inc2 = hz * (1.0f + 3.0f * k3) / SR;
        v->pda = 1.2f * k3;                           /* the index in turns, falling with env2 */
        v->k2 = dec_k(dec * 0.4f);
        v->a = 0.4f * k4;
        break;
    }
    }
    if (model != DM_SAMPLE)
        v->inc = hz / SR;
}

void drumline_hit(drumline_t *d, int sound, int vel, int pitch_semi, int short_gate, int long_tail)
{
    const uint8_t *p;
    dvoice_t *v;
    int mode, i;
    if (sound < 0 || sound >= DL_NSOUND)
        return;
    if (vel < 1) vel = 1;
    if (vel > 127) vel = 127;
    drumline_hitv[sound] = (uint8_t)(vel >= 127 ? 255 : vel * 2);
    p = d->par + TP_DRUM(sound, 0);
    mode = p[DP_MODE] % NPM;
    if (mode >= PM_CHOKE1)                            /* a choke group: the others of the group go */
        for (i = 0; i < NDVOICE; i++)
            if (d->v[i].on && d->v[i].mode == mode && d->v[i].sound != sound)
                fade_out(&d->v[i]);
    v = alloc(d, sound);
    start(d, v, sound, vel, pitch_semi, (short_gate ? HF_SHORT : 0) | (long_tail ? HF_LONG : 0), 1);
}

/* ---- one voice, m samples (<= SUB) of mono into t[] ---- */
static void render_voice(dvoice_t *v, int16_t *line, smp_cache_t *sc, float *t, int m)
{
    int i;
    float env = v->env, k = v->k, env2 = v->env2, k2 = v->k2, ph = v->ph, inc = v->inc, g = v->g;
    uint32_t rng = v->rng;
    switch (v->model) {
    case DM_KICK: {
        float pd = v->pd, pdk = v->pdk, pda = v->pda, drive = v->a, click = v->b, c = v->c1, f1 = v->f1;
        float ch = v->c2, cl = v->c3, f2 = v->f2, f3 = v->f3, f4 = v->f4;
        for (i = 0; i < m; i++) {
            float y, n;
            pd *= pdk;
            ph += inc * (1.0f + pda * pd);
            WRAP(ph);
            y = clip3(sin_t(ph) * env * drive);
            f1 += c * (y - f1);                       /* tone: the body's low-pass */
            n = noise(&rng) * env2 * click;
            f3 += cl * (n - f3);                      /* the click, in its band */
            f4 += cl * (f3 - f4);
            f2 += ch * (f4 - f2);
            t[i] = (f1 + f4 - f2) * g;
            env *= k;
            env2 *= k2;
        }
        v->pd = pd;
        v->f1 = f1;
        v->f2 = fm_flush(f2);
        v->f3 = fm_flush(f3);
        v->f4 = fm_flush(f4);
        break;
    }
    case DM_SNARE: {
        float pd = v->pd, pdk = v->pdk, pda = v->pda, ph2 = v->ph2, inc2 = v->inc2;
        float c1 = v->c1, c2 = v->c2, c3 = v->c3, f1 = v->f1, f2 = v->f2, body = v->a, snap = v->b;
        for (i = 0; i < m; i++) {
            float r = 1.0f + pda * pd, y, n;
            pd *= pdk;
            ph += inc * r;
            ph2 += inc2 * r;
            WRAP(ph);
            WRAP(ph2);
            n = noise(&rng) * c3 + c1 * f1 + c2 * f2;
            f2 = f1;
            f1 = n;
            y = clip3((sin_t(ph) * 0.6f + sin_t(ph2) * 0.4f) * env2 * body) + n * env * snap;
            t[i] = y * g;
            env *= k;
            env2 *= k2;
        }
        v->pd = pd;
        v->ph2 = ph2;
        v->f1 = f1;
        v->f2 = f2;
        break;
    }
    case DM_HIHAT: {
        uint32_t p0 = v->mp[0], p1 = v->mp[1], p2 = v->mp[2], p3 = v->mp[3], p4 = v->mp[4], p5 = v->mp[5];
        const uint32_t i0 = v->mi[0], i1 = v->mi[1], i2 = v->mi[2], i3 = v->mi[3], i4 = v->mi[4], i5 = v->mi[5];
        float c = v->pdk, f1 = v->f1, f2 = v->f2, f3 = v->f3, f4 = v->f4, c1 = v->c1, c2 = v->c2, c3 = v->c3;
        float era = v->a, tone = v->b, nz = 1.0f - era, mt = era * 0.6f, rm = era * 0.4f;
        for (i = 0; i < m; i++) {
            uint32_t cnt;
            float x, y, bp;
            p0 += i0; p1 += i1; p2 += i2; p3 += i3; p4 += i4; p5 += i5;
            cnt = (p0 >> 31) + (p1 >> 31) + (p2 >> 31) + (p3 >> 31) + (p4 >> 31) + (p5 >> 31);
            x = (float)(int32_t)(6 - 2 * (int32_t)cnt) * (1.0f / 6.0f) * mt;
            x += (float)(int32_t)(1 - 2 * (int32_t)((p0 ^ p3) >> 31)) * rm;   /* the ring-mod of two */
            x += noise(&rng) * nz;
            f1 += c * (x - f1);                       /* two one-pole high-passes */
            y = x - f1;
            f2 += c * (y - f2);
            y -= f2;
            bp = y * c3 + c1 * f3 + c2 * f4;          /* the bandpass focus */
            f4 = f3;
            f3 = bp;
            y += (bp - y) * tone;
            t[i] = y * (env + 0.5f * env2) * g;
            env *= k;
            env2 *= k2;
        }
        v->mp[0] = p0; v->mp[1] = p1; v->mp[2] = p2; v->mp[3] = p3; v->mp[4] = p4; v->mp[5] = p5;
        v->f1 = f1;
        v->f2 = f2;
        v->f3 = fm_flush(f3);
        v->f4 = fm_flush(f4);
        break;
    }
    case DM_CLAP: {
        float c1 = v->c1, c2 = v->c2, c3 = v->c3, f1 = v->f1, f2 = v->f2, tail = v->b;
        int32_t cnt = v->cnt, bursts = v->bursts, gap = (int32_t)v->a;
        for (i = 0; i < m; i++) {
            float n = noise(&rng) * c3 + c1 * f1 + c2 * f2;
            f2 = f1;
            f1 = n;
            if (bursts && --cnt == 0) {               /* the next burst */
                cnt = gap;
                bursts--;
                env2 = 1.0f;
            }
            t[i] = n * (env2 + tail * env) * 0.45f * g;
            env *= k;
            env2 *= k2;
        }
        v->cnt = cnt;
        v->bursts = bursts;
        v->f1 = f1;
        v->f2 = f2;
        break;
    }
    case DM_SHAKER: {
        float c1 = v->c1, c2 = v->c2, c3 = v->c3, f1 = v->f1, f2 = v->f2;
        int32_t cnt = v->cnt, bursts = v->bursts;
        for (i = 0; i < m; i++) {
            float n = noise(&rng) * c3 + c1 * f1 + c2 * f2;
            f2 = f1;
            f1 = n;
            if (bursts && --cnt == 0) {               /* the shake back: a second, softer hit */
                bursts--;
                env2 = 1.0f;
                env = fm_maxf(env, 0.7f);
            }
            t[i] = n * env * (1.0f - env2) * 0.8f * g;
            env *= k;
            env2 *= k2;
        }
        v->cnt = cnt;
        v->bursts = bursts;
        v->f1 = f1;
        v->f2 = f2;
        break;
    }
    case DM_TAMB: {
        uint32_t p0 = v->mp[0], p1 = v->mp[1], p2 = v->mp[2], p3 = v->mp[3], p4 = v->mp[4], p5 = v->mp[5];
        const uint32_t i0 = v->mi[0], i1 = v->mi[1], i2 = v->mi[2], i3 = v->mi[3], i4 = v->mi[4], i5 = v->mi[5];
        float c = v->c1, f1 = v->f1, f2 = v->f2, nz = v->a, mt = v->b;
        for (i = 0; i < m; i++) {
            uint32_t cnt;
            float x, y;
            p0 += i0; p1 += i1; p2 += i2; p3 += i3; p4 += i4; p5 += i5;
            cnt = (p0 >> 31) + (p1 >> 31) + (p2 >> 31) + (p3 >> 31) + (p4 >> 31) + (p5 >> 31);
            x = (float)(int32_t)(6 - 2 * (int32_t)cnt) * (1.0f / 6.0f) * env2 * mt;
            x += noise(&rng) * env * nz;
            f1 += c * (x - f1);                       /* two one-pole high-passes */
            y = x - f1;
            f2 += c * (y - f2);
            y -= f2;
            t[i] = y * g;
            env *= k;
            env2 *= k2;
        }
        v->mp[0] = p0; v->mp[1] = p1; v->mp[2] = p2; v->mp[3] = p3; v->mp[4] = p4; v->mp[5] = p5;
        v->f1 = f1;
        v->f2 = f2;
        break;
    }
    case DM_RESO: {
        int32_t w = v->w, len = v->len;
        float fr = v->fr, lp = v->lp, fb = v->fb, damp = v->damp, pk = 0.0f, bl = v->a, c = v->c1, f1 = v->f1;
        if (env2 > 1e-5f) {                           /* the strike still sounding */
            for (i = 0; i < m; i++) {
                int32_t r0 = (w - len) & (RESO_LEN - 1), r1 = (r0 - 1) & (RESO_LEN - 1);
                float a = (float)line[r0], b = (float)line[r1], y, x;
                y = (a + (b - a) * fr) * (1.0f / 16384.0f);
                lp += damp * (y - lp);                /* the loop's low-pass: the string darkens */
                f1 += c * (noise(&rng) - f1);         /* the strike: a burst, bright or soft */
                x = (f1 * env2 * bl + lp * fb) * 16384.0f;
                line[w] = (int16_t)(x > 32767.0f ? 32767.0f : x < -32767.0f ? -32767.0f : x);
                w = (w + 1) & (RESO_LEN - 1);
                t[i] = y * 1.4f * g;
                pk = fm_maxf(pk, fm_fabsf(y));
                env2 *= k2;
            }
        } else {                                      /* past -100 dB: the string rings on its own. The
                                                       * line holds |values| <= 32767, so |y| and the
                                                       * low-pass stay under 2 and lp * fb * 16384 fits an
                                                       * int16 without the clamp */
            for (i = 0; i < m; i++) {
                int32_t r0 = (w - len) & (RESO_LEN - 1), r1 = (r0 - 1) & (RESO_LEN - 1);
                float a = (float)line[r0], b = (float)line[r1], y;
                y = (a + (b - a) * fr) * (1.0f / 16384.0f);
                lp += damp * (y - lp);
                line[w] = (int16_t)(lp * fb * 16384.0f);
                w = (w + 1) & (RESO_LEN - 1);
                t[i] = y * 1.4f * g;
                pk = fm_maxf(pk, fm_fabsf(y));
            }
        }
        v->w = w;
        v->lp = fm_flush(lp);
        v->f1 = f1;
        v->pk = fm_maxf(pk, v->pk * 0.7f);            /* held over a period: a quiet stretch of the
                                                       * string is not its end */
        break;
    }
    case DM_CONGA: {
        float pd = v->pd, pdk = v->pdk, pda = v->pda, ph2 = v->ph2, inc2 = v->inc2, p2g = v->b, drive = v->a;
        float comp = v->c1;
        for (i = 0; i < m; i++) {
            float r = 1.0f + pda * pd, y;
            pd *= pdk;
            ph += inc * r;
            ph2 += inc2 * r;
            WRAP(ph);
            WRAP(ph2);
            y = sin_t(ph) * env + sin_t(ph2) * env2 * p2g;
            t[i] = clip3(y * drive) * comp * g;
            env *= k;
            env2 *= k2;
        }
        v->pd = pd;
        v->ph2 = ph2;
        break;
    }
    case DM_TOM: {
        float pd = v->pd, pdk = v->pdk, pda = v->pda, c = v->c1, f1 = v->f1;
        for (i = 0; i < m; i++) {
            float y;
            pd *= pdk;
            ph += inc * (1.0f + pda * pd);
            WRAP(ph);
            f1 += c * (noise(&rng) - f1);
            y = sin_t(ph) * env + f1 * env2 * 0.25f;
            t[i] = y * g;
            env *= k;
            env2 *= k2;
        }
        v->pd = pd;
        v->f1 = f1;
        break;
    }
    case DM_FM: {
        float ph2 = v->ph2, inc2 = v->inc2, idx = v->pda, fb = v->a, mo = v->f1;
        for (i = 0; i < m; i++) {
            ph += inc;
            ph2 += inc2;
            WRAP(ph);
            WRAP(ph2);
            mo = sin_t_any(ph2 + mo * fb);               /* the modulator, with its own feedback */
            t[i] = sin_t_any(ph + mo * idx * env2) * env * g;
            env *= k;
            env2 *= k2;
        }
        v->ph2 = ph2;
        v->f1 = mo;
        break;
    }
    case DM_SAMPLE: {                                 /* the sample, interpolated, forward or backward */
        const smp_t *sm = smp_get(v->sidx);           /* (0: the user bank is being rewritten: the voice stops) */
        int32_t pos = v->len;
        float fr = v->fr;
        i = sm && sc && env > 0.0f ? smp_play(sc, sm, &pos, &fr, inc, v->a > 0.0f ? 1 : -1, v->w, (int32_t)sm->n, t, m) : 0;
        if (k == 1.0f) {
            float eg = env * g;
            for (int j = 0; j < i; j++)
                t[j] *= eg;
        } else
            for (int j = 0; j < i; j++) {
                t[j] *= env * g;
                env *= k;
            }
        if (i < m) {                                  /* its end (or its start, reversed): the voice ends */
            for (; i < m; i++)
                t[i] = 0.0f;
            env = 0.0f;
        }
        v->len = pos;
        v->fr = fr;
        env2 = 0.0f;
        break;
    }
    default:
        for (i = 0; i < m; i++)
            t[i] = 0.0f;
        env = 0.0f;
        break;
    }
    v->env = env;
    v->env2 = env2;
    v->ph = ph;
    v->rng = rng;
    v->age += (uint32_t)m;
    if (v->model == DM_RESO) {
        if (env2 < 1e-3f && v->pk < 1e-4f && v->age > 4u * (uint32_t)v->len)
            v->on = 0;
    } else if (env < 1e-4f && env2 < 1e-4f) {
        v->on = 0;
    }
}

/* tanh's shape for a few instructions: x (27 + x^2) / (27 + 9 x^2) (a Pade form), flat past +-3 */
static inline float tanh_r(float x)
{
    float x2;
    if (x > 3.0f)
        return 1.0f;
    if (x < -3.0f)
        return -1.0f;
    x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

/* ---- the key's own stage: filter, drive, envelope, after the model ---- */
/* the key's envelope at t samples after the hit: a linear rise to the attack's end, flat to the hold's end,
 * then an exponential fall (none: decay off) */
static inline float key_env(const dvoice_t *v, float t)
{
    if (t < v->eatt)
        return t / v->eatt;
    if (t < v->ehold || v->edec == 0.0f)
        return 1.0f;
    return fm_expf(v->edec * (t - v->ehold));
}
/* m samples (<= SUB) of the voice's output in t[], in place. A neutral key (no filter, no drive, the
 * envelope flat at 1) is left as it is */
static void key_post(const uint8_t *p, dvoice_t *v, float *t, int m)
{
    int i, ft = p[DP_FTYPE] % NDF, filt = ft != DF_OFF, drv = p[DP_DRIVE] > 0;
    float e0 = v->eg, e1, de;
    if (ft != v->ftype) {                            /* another type: from rest */
        v->fz1 = v->fz2 = 0.0f;
        v->ftype = (uint8_t)ft;
    }
    uint32_t t1 = v->et + (uint32_t)m;
    if (!filt && !drv && v->eatt == 0.0f && v->edec == 0.0f) {
        v->et = t1;
        return;
    }
    e1 = key_env(v, (float)t1);
    de = (e1 - e0) * (1.0f / (float)m);
    if (filt) {                                      /* a TPT state-variable section, its cutoff on the envelope */
        /* cutoff 50 Hz .. 20 kHz, the envelope's reach +-6 octaves, Q 0.5 .. 10 */
        float oct = (float)p[DP_CUT] * (8.64385619f / 127.0f) + ((float)p[DP_FENV] - 64.0f) * (6.0f / 64.0f) * 0.5f * (e0 + e1);
        float hz = 50.0f * fm_exp2f(oct), g, a1, a2, a3, k = 2.0f * fm_exp2f((float)p[DP_RES] * (-4.3f / 127.0f));
        float z1 = v->fz1, z2 = v->fz2;
        hz = hz > 0.45f * SR ? 0.45f * SR : hz < 20.0f ? 20.0f : hz;
        g = fm_tanf(FM_PI * hz / SR);
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
        for (i = 0; i < m; i++) {
            float x = t[i], v3 = x - z2, v1 = a1 * z1 + a2 * v3, v2 = z2 + a2 * z1 + a3 * v3;
            z1 = 2.0f * v1 - z1;
            z2 = 2.0f * v2 - z2;
            t[i] = ft == DF_LP ? v2 : ft == DF_HP ? x - k * v1 - v2 : k * v1;
        }
        v->fz1 = fm_flush(z1);
        v->fz2 = fm_flush(z2);
    }
    if (drv) {                                       /* gain 1..16; a peak of 0.5 (a drum's, about) keeps its level */
        float d = (float)p[DP_DRIVE] * (1.0f / 127.0f), dg = 1.0f + 15.0f * d * d, mk = 0.5f / tanh_r(0.5f * dg);
        for (i = 0; i < m; i++)
            t[i] = tanh_r(t[i] * dg) * mk;
    }
    if (e0 != 1.0f || e1 != 1.0f) {
        float e = e0;
        for (i = 0; i < m; i++) {
            e += de;
            t[i] *= e;
        }
    }
    v->eg = e1;
    v->et = t1;
    if (v->edec < 0.0f && (float)t1 > v->ehold && e1 < 1e-4f)
        v->on = 0;                                   /* the key's decay has run out: the voice ends */
}

/* the key's volume in dB: 0 silent, 1..76 = -30..0 dB, 76..127 = 0..+20 dB */
static float volume_lin(int v)
{
    if (v <= 0)
        return 0.0f;
    if (v < VOL0)
        return fm_db2lin((float)(v - VOL0) * (30.0f / (float)(VOL0 - 1)));
    return fm_db2lin((float)(v - VOL0) * (20.0f / (float)(127 - VOL0)));
}

/* the chunk's targets from par[]: volume, pan, the key's sends; ramped over m samples */
static void control(const uint8_t *par, dvoice_t *v, int m)
{
    const uint8_t *p = par + TP_DRUM(v->sound, 0);
    float a = (float)p[DP_PAN] * (1.0f / 127.0f), lin, l, r, s1, s2, im;
    lin = volume_lin(p[DP_VOLUME]);
    l = lin * fm_sin_turns(0.25f * (1.0f - a));       /* constant power: cos, sin of a quarter turn */
    r = lin * fm_sin_turns(0.25f * a);
    s1 = lin * (float)p[DP_SEND1] * (1.0f / 127.0f);
    s2 = lin * (float)p[DP_SEND2] * (1.0f / 127.0f);
    if (v->age == 0) {                                /* a fresh voice starts at its gains */
        v->gl = l; v->gr = r; v->gs1 = s1; v->gs2 = s2;
    }
    im = 1.0f / (float)m;
    v->dgl = (l - v->gl) * im;
    v->dgr = (r - v->gr) * im;
    v->gs1 = s1;                                      /* the sends step: a send's zipper is buried in the effect */
    v->gs2 = s2;
}

/* ---- the dynamic filter ---- */
void drumline_filter(drumline_t *d, int type, int locut, int comp, int drive, int hicut)
{
    dynf_t *f = &d->f;
    float c = fm_clampf((float)comp * (1.0f / 127.0f), 0.0f, 1.0f);
    float dr = fm_clampf((float)drive * (1.0f / 127.0f), 0.0f, 1.0f);
    if (type != FT_DYNAMIC) {
        if (f->on) {                                  /* leaving: the states start clean next time */
            int i;
            for (i = 0; i < 4; i++)
                f->lo[i] = f->hi[i] = 0.0f;
            f->env = 0.0f;
            f->gain = 1.0f;
        }
        f->on = 0;
        return;
    }
    f->on = 1;
    f->lo_on = (uint8_t)(locut > 0);
    f->hi_on = (uint8_t)(hicut < 127);
    f->drv_on = (uint8_t)(drive > 0);
    /* low cut 20 Hz .. 2 kHz, hi cut 300 Hz .. 20 kHz; each one-pole's cutoff is raised a little so that
     * the pair in series is -3 dB at the frequency shown */
    f->lc = lp_c(20.0f * fm_exp2f((float)locut * (6.64f / 127.0f)) * 0.64f);
    f->hc = lp_c(300.0f * fm_exp2f((float)hicut * (6.06f / 127.0f)) * 1.55f);
    /* the compressor: the threshold falls to -24 dB, the ratio rises to 10:1, with make-up gain */
    f->thr = fm_db2lin(-24.0f * c);
    f->slope = 1.0f - 1.0f / (1.0f + 9.0f * c);
    f->makeup = fm_db2lin(18.0f * c);
    /* the drive: a tanh shape on the signal times 1..16 */
    f->drive = 1.0f + 15.0f * dr * dr;
    f->dcomp = 0.6f + 0.4f / f->drive;
}


/* m samples (<= SUB) of the dry pair through the filter, in place */
static void dynf_run(dynf_t *f, float *l, float *r, int m)
{
    int i;
    float pk = 0.0f, g0 = f->gain, g1, dg;
    if (f->lo_on) {
        float c = f->lc, a0 = f->lo[0], a1 = f->lo[1], b0 = f->lo[2], b1 = f->lo[3];
        for (i = 0; i < m; i++) {
            float x = l[i], y;
            a0 += c * (x - a0);
            y = x - a0;
            a1 += c * (y - a1);
            l[i] = y - a1;
            x = r[i];
            b0 += c * (x - b0);
            y = x - b0;
            b1 += c * (y - b1);
            r[i] = y - b1;
        }
        f->lo[0] = fm_flush(a0); f->lo[1] = fm_flush(a1); f->lo[2] = fm_flush(b0); f->lo[3] = fm_flush(b1);
    }
    if (f->hi_on) {
        float c = f->hc, a0 = f->hi[0], a1 = f->hi[1], b0 = f->hi[2], b1 = f->hi[3];
        for (i = 0; i < m; i++) {
            a0 += c * (l[i] - a0);
            a1 += c * (a0 - a1);
            l[i] = a1;
            b0 += c * (r[i] - b0);
            b1 += c * (b0 - b1);
            r[i] = b1;
        }
        f->hi[0] = fm_flush(a0); f->hi[1] = fm_flush(a1); f->hi[2] = fm_flush(b0); f->hi[3] = fm_flush(b1);
    }
    /* the compressor: a peak follower (instant up, ~100 ms down, stepped per sub-block), the gain
     * ramped across the block */
    for (i = 0; i < m; i++)
        pk = fm_maxf(pk, fm_maxf(fm_fabsf(l[i]), fm_fabsf(r[i])));
    if (pk > f->env)
        f->env = pk;
    else
        f->env += (pk - f->env) * (0.0073f * (float)m * (1.0f / 32.0f));
    g1 = f->makeup;
    if (f->env > f->thr)
        g1 *= fm_exp2f(-f->slope * fm_log2f(f->env / f->thr));
    f->gain = g1;
    dg = (g1 - g0) * (1.0f / (float)m);
    if (f->drv_on) {
        float dv = f->drive, dc = f->dcomp;
        for (i = 0; i < m; i++) {
            g0 += dg;
            l[i] = tanh_r(l[i] * g0 * dv) * dc;
            r[i] = tanh_r(r[i] * g0 * dv) * dc;
        }
    } else {
        for (i = 0; i < m; i++) {
            g0 += dg;
            l[i] *= g0;
            r[i] *= g0;
        }
    }
}

void drumline_render(drumline_t *d, float *l, float *r, float *s1, float *s2, uint32_t n)
{
    uint32_t c;
    for (c = 0; c < n; c += CR) {
        int m = (int)(n - c < CR ? n - c : CR), vi, o;
        for (vi = 0; vi < NDVOICE; vi++)
            if (d->v[vi].on)
                control(d->par, &d->v[vi], m);
        for (o = 0; o < m; o += SUB) {
            float t[SUB], dl[SUB], dr[SUB];
            int mm = m - o < SUB ? m - o : SUB, i, any = 0;
            float *p1 = s1 + c + o, *p2 = s2 + c + o;
            for (i = 0; i < mm; i++)
                dl[i] = dr[i] = 0.0f;
            for (vi = 0; vi < NDVOICE; vi++) {
                dvoice_t *v = &d->v[vi];
                float gl, gr, att, attk;
                const float gs1 = v->gs1, gs2 = v->gs2, dgl = v->dgl, dgr = v->dgr;
                int from = 0;
                if (!v->on)
                    continue;
                if (v->wait > 0) {                    /* lag: the hit has not started yet */
                    if (v->wait >= mm) {
                        v->wait -= mm;
                        continue;
                    }
                    from = v->wait;
                    v->wait = 0;
                    for (i = 0; i < from; i++)
                        t[i] = 0.0f;
                }
                any = 1;
                render_voice(v, d->line[v->line],
                             v->sci && d->sc_owner[v->sci - 1] == vi ? &d->sc[v->sci - 1] : 0, t + from, mm - from);
                key_post(d->par + TP_DRUM(v->sound, 0), v, t + from, mm - from);
                gl = v->gl;
                gr = v->gr;
                att = v->att;
                attk = v->attk;
                for (i = 0; i < mm; i++) {
                    float x = t[i] * att;
                    att += attk;
                    if (att > 1.0f)
                        att = 1.0f;
                    gl += dgl;
                    gr += dgr;
                    dl[i] += x * gl;
                    dr[i] += x * gr;
                    p1[i] += x * gs1;
                    p2[i] += x * gs2;
                }
                v->gl = gl;
                v->gr = gr;
                v->att = att;
                if (!v->on) {
                    if (v->model == DM_RESO && d->line_owner[v->line] == vi)
                        d->line_owner[v->line] = -1;
                    if (v->held && v->mode == PM_LOOP)   /* loop: from the start again while held */
                        start(d, v, v->sound, v->hvel, v->hpitch, v->hflags, 0);
                }
            }
            if (d->f.on && (any || d->f.env > 1e-5f))
                dynf_run(&d->f, dl, dr, mm);
            for (i = 0; i < mm; i++) {
                l[c + o + i] += dl[i];
                r[c + o + i] += dr[i];
            }
        }
    }
}

int drumline_sounding(const drumline_t *d)
{
    int i;
    for (i = 0; i < NDVOICE; i++)
        if (d->v[i].on)
            return 1;
    return 0;
}
int drumline_voices(const drumline_t *d)
{
    int i, n = 0;
    for (i = 0; i < NDVOICE; i++)
        n += d->v[i].on != 0;
    return n;
}
