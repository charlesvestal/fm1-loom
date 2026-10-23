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
#include "digisophie.h"

#define SR SY_SR
#define CR SY_CHUNK                   /* pan / volume / sends re-read this often */
#define SUB 32                        /* a voice renders this many at once into a 128 B stack buffer */
#define RESO_LEN 512                  /* a string's delay line, int16: 86 Hz at the longest */
#define NRESO 2                       /* strings: string sounds past two steal the older's line */
_Static_assert((RESO_LEN & (RESO_LEN - 1)) == 0, "a string's line wraps by a mask");

const char *const DM_NAME[NDM] = {"kick", "snare", "hat", "clap", "shaker", "tambourine", "string",
                                  "conga", "tom", "fm", "sample", "x kick", "x snare", "x hat", "x clap", "x tom",
                                  "x rim", "x fm", "x noise", "clang", "sophie"};
const char *const DM_KNOB[NDM][4] = {
    [DM_KICK] = {"pitch", "length", "punch", "tone"},    [DM_SNARE] = {"pitch", "length", "wires", "punch"},
    [DM_HIHAT] = {"pitch", "length", "metal", "focus"},  [DM_CLAP] = {"tone", "length", "spread", "tail"},
    [DM_SHAKER] = {"color", "length", "swell", "return"}, [DM_TAMB] = {"tone", "length", "jingle", "hiss"},
    [DM_RESO] = {"pitch", "length", "bright", "pluck"},  [DM_CONGA] = {"pitch", "length", "slide", "drive"},
    [DM_TOM] = {"pitch", "length", "sweep", "rate"},     [DM_FM] = {"pitch", "length", "mod", "feedback"},
    [DM_SAMPLE] = {"sample", "pitch", "start", "reverse"},
    [DM_XKICK] = {"pitch", "length", "tone", "sweep"},   [DM_XSNARE] = {"pitch", "length", "tone", "snap"},
    [DM_XHAT] = {"pitch", "length", "tone", "res"},      [DM_XCLAP] = {"tone", "length", "drive"},
    [DM_XTOM] = {"pitch", "length", "tone", "sweep"},    [DM_XRIM] = {"pitch", "length", "tone", "sweep"},
    [DM_XFM] = {"pitch", "length", "ratio", "mod decay"}, [DM_XNOISE] = {"tone", "length", "drive"},
    [DM_CLANG] = {"pitch", "length", "color", "metal"},   [DM_SOPHIE] = {"pitch", "length", "color", "metal"},
};
/* knobs 5..8 (a model with more than four) */
const char *const DM_KNOB2[NDM][4] = {
    [DM_XKICK] = {"click", "harm", "drive"},   [DM_XSNARE] = {"wire", "body", "drive"},
    [DM_XHAT] = {"noise", "metal", "drive"},
    [DM_XTOM] = {"noise", "fm", "drive"},      [DM_XRIM] = {"noise", "drive"},
    [DM_XFM] = {"click", "index", "drive"},
    [DM_CLANG] = {"model", "sweep", "feedback", "fold"}, [DM_SOPHIE] = {"model", "feedback", "sweep", "crush"},
};
const char *const SPH_MODEL_N[7] = {"fuse", "stack", "split", "shard", "pipe", "boom", "kick"};
const char *const CLANG_MODEL_N[4] = {"fuse", "boom", "pipe", "shard"};
const uint8_t DM_NKNOB[] = {4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 7, 7, 7, 3, 7, 6, 7, 3, 8, 8};
_Static_assert(sizeof DM_NKNOB == NDM, "a knob count for each model");
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
    /* the x models: MACHINEDRUM's factory kit (its 0..100 x 1.27; pitch: the note), its BD, SD, CH, CP, LT, RS,
     * FM1 and NO */
    [DM_XKICK] = {35, 46, 46, 91},  [DM_XSNARE] = {53, 30, 97, 28}, [DM_XHAT] = {82, 10, 97, 28},
    [DM_XCLAP] = {97, 34, 23},      [DM_XTOM] = {43, 43, 97, 28},   [DM_XRIM] = {74, 13, 97, 28},
    [DM_XFM] = {59, 30, 97, 28},    [DM_XNOISE] = {97, 41, 23},
    [DM_CLANG] = {45, 60, 64, 90},                  /* a FUSE clang: A2, 0.3 s */
    [DM_SOPHIE] = {38, 54, 66, 97},                 /* Sophie's factory snare: D2, 0.24 s */
};
/* knobs 5..8 (a model with more than four) */
const uint8_t DM_DEFAULT2[NDM][4] = {
    [DM_XKICK] = {13, 15, 23}, [DM_XSNARE] = {97, 15, 23}, [DM_XHAT] = {117, 15, 23},
    [DM_XTOM] = {13, 15, 23},  [DM_XRIM] = {13, 23},       [DM_XFM] = {13, 85, 23},
    [DM_CLANG] = {0, 64, 40, 0},
    [DM_SOPHIE] = {3, 23, 82, 38},                  /* shard, feedback 18, sweep 28, crush 30 */
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
    /* the x models' lengths are their own (x_start); clang: 30 ms .. 4 s, as Sophie's DECAY. Trims: kick and snare
     * -6 dBFS, the hat and the noise -12, the rest -9 */
    [DM_XKICK] = {0.05f, 1.5f, 0.70f},  [DM_XSNARE] = {0.05f, 1.5f, 0.75f}, [DM_XHAT] = {0.05f, 1.5f, 0.67f},
    [DM_XCLAP] = {0.05f, 1.5f, 2.14f},  [DM_XTOM] = {0.05f, 1.5f, 2.11f},   [DM_XRIM] = {0.05f, 1.5f, 2.26f},
    [DM_XFM] = {0.05f, 1.5f, 0.54f},    [DM_XNOISE] = {0.05f, 1.5f, 1.46f}, [DM_CLANG] = {0.03f, 4.0f, 2.26f},
    [DM_SOPHIE] = {0.03f, 4.0f, 3.0f},                /* (its default, the snare, at -6 dBFS) */
};
/* the 808's six metal oscillators (sc808: 205.3, 304.4, 369.6, 522.7, 540, 800 Hz) and the tambourine's
 * jingles: six inharmonic partials of a small brass disc */
static const float METAL_HZ[6] = {205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f};
static const float JINGLE_HZ[6] = {4120.0f, 5230.0f, 6010.0f, 7340.0f, 8560.0f, 9870.0f};

/* the x models' filter: a TPT state-variable section (Zavalishin), the response of Web Audio's biquads (RBJ's,
 * the same analog prototypes through the same prewarped bilinear transform). g = tan(pi f / SR), gk = g + 1/Q,
 * a1 = 1 / (1 + g (g + 1/Q)) */
typedef struct { float g, gk, a1, s1, s2; } xsvf_t;

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
    float ph, inc;                    /* the oscillator, phase in turns [-0.5, 0.5) */
    float g;                          /* the model's output gain */
    float gl, gr, gs1, gs2;           /* gains now: left, right, the two sends (set each chunk) */
    float dgl, dgr;                   /* left and right ramp toward the chunk's targets */
    union {                           /* the model's own: the analytic models' and the sample's */
        struct {
            float ph2, inc2;          /* a second oscillator */
            float pd, pdk, pda;       /* pitch drop: inc * (1 + pda * pd), pd decays by pdk */
            float f1, f2, f3, f4;     /* filter states */
            float c1, c2, c3;         /* filter coefficients */
            float a, b;               /* two model amounts */
            uint32_t mp[6], mi[6];    /* the hat's / tambourine's six square phases and increments */
            int32_t len, w;           /* the string: its length, write position (the sample: position, its start) */
            float fr, lp, fb, damp, pk;   /* its fractional length, loop filter, feedback, damping, peak (the
                                           * sample: the position's fraction) */
        };
        struct {                      /* the x models (x_start), the hat's apart: oscillator 1 is ph / inc, layer 1
                                       * env2 / k2, the attack's samples left cnt; env / k the choke's gate */
            float xph2, xinc2;        /* oscillator 2 */
            float xm1, xm2;           /* the oscillators' pitch glides: multipliers a sample */
            int32_t xn1, xn2;         /* and their samples left (the generic engines: xn2 the time since the hit) */
            float xe[3], xk[3];       /* layers 2..4: gain now, multiplier a sample (the generic engines: xe[0] the
                                       * FM depth, xe[1] the noise's level) */
            xsvf_t xf[3];
            float xsa, xsn;           /* the waveshaper: drive and its normalisation (1 / tanh(drive)) */
            float xdx, xdy;           /* the DC blocker's states */
        };
        struct {                      /* the x hat: six squares (layer 1: env2 / k2), noise (layer 2) */
            uint32_t hp[6], hi[6];
            float he, hk;
            xsvf_t hf[3];             /* the metal's high-pass, its band, the noise's high-pass */
            float hsa, hsn, hdx, hdy;
        };
        struct {                      /* sophie (sophie_start): the carrier is ph / inc (turns), the level env / k */
            float sm1, sm2, sm3;      /* the three modulators' phases, turns [0, 1) */
            float spe, spm, sme, smm; /* the pitch envelope and the brightness (metal) one, their multipliers */
            float sat, sfz, snz;      /* the attack's rise, the feedback's last, the noise's low-pass */
            float sfl, sfb;           /* the filter's states */
            float sch, scl, scs;      /* the crusher: held sample, levels, step */
            float sr1, sr2, sr3;      /* the modulators' ratios */
            float scol, smet, sfbk, ssk;  /* color, metal, feedback (0..1), the sweep in octaves */
            float sbase, sdi;         /* the note's increment; the carrier's step a sample toward the next */
            float slast;              /* the last sample made (the half rate: the output between two is their mean) */
            int16_t scc, scd;         /* the crusher's count, its hold */
            uint8_t smodel, skick, sctl, stog;   /* stog: 1 = the next output sample is the last made, as it is */
        };
        struct {                      /* clang: digisophie's voice and its controls */
            struct ds_voice ds;
            struct ds_params dp;
            uint8_t dtrig, dfold;
        };
    };
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


/* ---- the x models: MACHINEDRUM's engines (github.com/Omodaka9375/machinedrum, src/drums.js, src/audio.js voice();
 * Copyright (c) 2026 Branislav Đalić, MIT: LICENSING.md) ----
 * Each is its Web Audio graph in C: oscillators whose pitch glides exponentially from a bend to the note, each layer
 * through its own envelope (a 1 ms rise, then an exponential fall to -100 dB at its length), Web Audio's biquads as
 * state-variable sections (xsvf_t: the same responses; a low- or high-pass's Q is in dB there, a band-pass's
 * linear), the waveshaper (tanh(a x) / tanh(a), unoversampled) and a DC blocker. Its kick, snare, hat and FM run
 * drums.js's drumVoice; its clap, tom, rim and noise audio.js's simpler voice (one low-pass, the shaper, then one
 * envelope with a 2 ms rise). The noise is drums.js's: one buffer of an LCG from seed 785, from its start at every
 * hit (so every hit's noise is the same, as there), shared by the voice's noise layers; the simpler voice's is
 * Math.random's (here the voice's own generator). Its knobs are the page's 0..100 (pitch: the note) at 0..127. */
#define XA_DRUM 44                    /* the layers' rise: 1 ms */
#define XA_SIMPLE 88                  /* the simpler voice's: 2 ms */
#define XQ_DB0 1.12201845f            /* Web Audio's default Q (1) of a low- or high-pass, in dB: 10^(1/20) */
#define XQ_DB07 1.08392691f           /* Q 0.7 dB */
static void xsvf_set(xsvf_t *f, float hz, float q)
{
    float g, k = 1.0f / q;
    hz = fm_clampf(hz, 10.0f, 0.45f * SR);
    g = fm_tanf(FM_PI * hz / SR);
    f->g = g;
    f->gk = g + k;
    f->a1 = 1.0f / (1.0f + g * (g + k));
    f->s1 = f->s2 = 0.0f;
}
/* one sample: the high-pass's output in *h, the band's (unscaled) in *b, the low-pass's returned */
static inline float xsvf(xsvf_t *f, float x, float *h, float *b)
{
    float hp = (x - f->gk * f->s1 - f->s2) * f->a1, v1 = f->g * hp, bp = v1 + f->s1, v2, lp;
    f->s1 = bp + v1;
    v2 = f->g * bp;
    lp = v2 + f->s2;
    f->s2 = lp + v2;
    *h = hp;
    *b = bp;
    return lp;
}
static inline float xsvf_lp(xsvf_t *f, float x) { float h, b; return xsvf(f, x, &h, &b); }
static inline float xsvf_hp(xsvf_t *f, float x) { float h, b; xsvf(f, x, &h, &b); return h; }
static inline float xsvf_bp(xsvf_t *f, float x) { float h, b; xsvf(f, x, &h, &b); return b * (f->gk - f->g); }
/* drums.js's noise buffer: an LCG, x / 2^31 - 1 */
static inline float xnoise(uint32_t *r)
{
    *r = *r * 1664525u + 1013904223u;
    return (float)*r * (1.0f / 2147483648.0f) - 1.0f;
}
/* a layer: level, then -100 dB (drums.js: 0.00001) len seconds after the hit, from the rise's end (att seconds) */
static void x_layer(float *e, float *k, float level, float len, float att, float floor)
{
    *e = level;
    *k = level > 0.0f ? fm_exp2f(fm_log2f(floor / level) / (fm_maxf(len - att, 0.002f) * SR)) : 0.0f;
}
/* an oscillator's glide: from f0 to f1 Hz over t seconds (exponentialRampToValueAtTime), then f1 */
static void x_glide(float *inc, float *m, int32_t *n, float f0, float f1, float t)
{
    f0 = fm_minf(f0, 0.45f * SR);
    f1 = fm_minf(f1, 0.45f * SR);
    *inc = f0 / SR;
    *n = (int32_t)(t * SR);
    *m = *n > 0 ? fm_exp2f(fm_log2f(f1 / f0) / (float)*n) : 1.0f;
}
static const float XHAT_R[6] = {1.0f, 1.342f, 1.807f, 2.413f, 2.731f, 3.119f};
/* the x models' knobs: which of MACHINEDRUM's PITCH DECAY TONE SWEEP NOISE FM DRIVE (0..6) each drives (-1: none;
 * a value no knob shows keeps the factory kit's: XFIX) */
enum { XP_PITCH, XP_DECAY, XP_TONE, XP_SWEEP, XP_NOISE, XP_FM, XP_DRIVE, NXP };
static const int8_t XMAP[DM_XNOISE - DM_XKICK + 1][8] = {
    {0, 1, 2, 3, 4, 5, 6, -1}, {0, 1, 2, 3, 4, 5, 6, -1}, {0, 1, 2, 3, 4, 5, 6, -1},
    {2, 1, 6, -1, -1, -1, -1, -1},                    /* the clap: TONE, DECAY, DRIVE (its pitch, sweep, FM unused) */
    {0, 1, 2, 3, 4, 5, 6, -1}, {0, 1, 2, 3, 4, 6, -1, -1},   /* the rim: no FM */
    {0, 1, 2, 3, 4, 5, 6, -1}, {2, 1, 6, -1, -1, -1, -1, -1},
};
static const float XFIX[NXP] = {60.0f, 30.0f, 76.0f, 22.0f, 92.0f, 12.0f, 18.0f};
/* the hit: pitch the note, the rest MACHINEDRUM's 0..100; lsc the length's scale (the kit's length, a short or
 * long step) */
static void x_start(dvoice_t *v, int model, float pitch, float decay, float tone, float sweep, float nz, float fm,
                    float drive, float lsc, int flags)
{
    float hz = 440.0f * fm_exp2f((pitch - 69.0f) * (1.0f / 12.0f)), d = decay * 0.01f, len, a;
    uint32_t r0 = v->rng;
    int i;
    v->env = 1.0f;                                    /* the gate: 1 until a choke */
    v->k = 1.0f;
    v->cnt = DM_ISX(model) && model != DM_XCLAP && model != DM_XTOM && model != DM_XRIM && model != DM_XNOISE
                 ? XA_DRUM : XA_SIMPLE;
    v->rng = 785u;                                    /* drums.js's noise, from its start */
    a = 1.0f + drive * (model <= DM_XHAT || model == DM_XFM ? 0.055f : 0.1f);
#define XLEN(l) do { len = (l) * lsc; if (flags & HF_SHORT) len = fm_minf(len, 0.06f); if (flags & HF_LONG) len *= 4.0f; } while (0)
    switch (model) {
    case DM_XKICK:                                    /* a sine gliding down from bend x the note, a short triangle
                                                       * an octave up (the beater), a burst of high-passed noise */
        XLEN(0.08f + d * d * 2.0f);
        x_glide(&v->inc, &v->xm1, &v->xn1, hz * (1.0f + sweep / 9.0f), hz, 0.008f + (100.0f - tone) / 1800.0f);
        x_layer(&v->env2, &v->k2, 0.9f, len, 0.001f, 1e-5f);
        x_glide(&v->xinc2, &v->xm2, &v->xn2, 2.0f * hz * (1.0f + sweep / 18.0f), 2.0f * hz, 0.015f);
        x_layer(&v->xe[0], &v->xk[0], fm * 0.01f * 0.3f, 0.015f + d * 0.09f, 0.001f, 1e-5f);
        x_layer(&v->xe[1], &v->xk[1], nz * 0.01f * 0.6f, 0.003f + tone / 5000.0f, 0.001f, 1e-5f);
        xsvf_set(&v->xf[0], 600.0f + tone * 60.0f, XQ_DB0);             /* the body */
        xsvf_set(&v->xf[1], 1800.0f + tone * 50.0f, XQ_DB07);           /* the click */
        break;
    case DM_XSNARE: {                                 /* a triangle and a sine through a low-pass, the wires (band-
                                                       * passed noise) and a high-passed snap */
        float mix = nz * 0.01f, bl = 0.8f * (1.0f - mix * 0.8f);
        XLEN(0.055f + d * d * 1.1f);
        x_glide(&v->inc, &v->xm1, &v->xn1, hz * (1.0f + sweep / 85.0f), hz, 0.014f);
        x_layer(&v->env2, &v->k2, bl, len * 0.52f, 0.001f, 1e-5f);
        x_glide(&v->xinc2, &v->xm2, &v->xn2, hz * (1.48f + fm / 240.0f) * (1.0f + sweep / 130.0f),
                hz * (1.48f + fm / 240.0f), 0.009f);
        x_layer(&v->xe[0], &v->xk[0], bl * 0.55f, len * 0.32f, 0.001f, 1e-5f);
        x_layer(&v->xe[1], &v->xk[1], mix * 0.9f, len, 0.001f, 1e-5f);
        x_layer(&v->xe[2], &v->xk[2], 0.03f + sweep / 900.0f, 0.009f, 0.001f, 1e-5f);
        xsvf_set(&v->xf[0], 1300.0f + tone * 65.0f, XQ_DB0);
        xsvf_set(&v->xf[1], 1100.0f * fm_exp2f(tone / 32.0f), 0.45f + fm / 180.0f);
        xsvf_set(&v->xf[2], 4200.0f, XQ_DB07);
        break;
    }
    case DM_XHAT: {                                   /* six squares (the 808's ratios) through a high-pass and a
                                                       * band, and high-passed noise */
        float f0 = 150.0f + hz * 0.28f, spread = fm * 0.01f;
        XLEN(0.018f + d * d * 1.55f);
        for (i = 0; i < 6; i++) {
            v->hp[i] = 0;                             /* (Web Audio's oscillators start together, at phase 0) */
            v->hi[i] = (uint32_t)(fm_minf(f0 * (XHAT_R[i] + spread * (float)i * 0.071f), 0.45f * SR) * (4294967296.0f / SR));
        }
        x_layer(&v->env2, &v->k2, 0.16f, len, 0.001f, 1e-5f);
        x_layer(&v->he, &v->hk, nz * 0.01f * 0.23f, len * 0.8f, 0.001f, 1e-5f);
        xsvf_set(&v->hf[0], 1800.0f + tone * 65.0f, XQ_DB0);
        xsvf_set(&v->hf[1], fm_minf(0.42f * SR, 6500.0f + tone * 38.0f), 0.6f + sweep / 70.0f);
        xsvf_set(&v->hf[2], 4000.0f + tone * 40.0f, XQ_DB07);
        v->hsa = a;
        v->hsn = 1.0f / tanh_r(a);
        v->hdx = v->hdy = 0.0f;
        return;
    }
    case DM_XFM: {                                    /* 2-op FM (the depth in Hz, falling on its own: MOD DECAY),
                                                       * a little high-passed noise */
        float ratio = 0.25f * fm_exp2f(tone / 20.0f), idx = hz * fm * fm * 0.0001f * 12.0f;
        XLEN(0.04f + d * d * 2.0f);
        v->inc = fm_minf(hz, 0.45f * SR) / SR;
        v->xn1 = 0;
        x_layer(&v->env2, &v->k2, 0.78f, len, 0.001f, 1e-5f);
        v->xph2 = 0.0f;
        v->xinc2 = fm_minf(hz * ratio, 0.45f * SR) / SR;
        v->xn2 = 0;
        /* the depth from the hit (no rise), in turns a sample */
        x_layer(&v->xe[0], &v->xk[0], idx / SR, 0.008f + sweep * sweep * 0.0001f * len * 1.2f, 0.0f, 1e-5f / SR);
        x_layer(&v->xe[1], &v->xk[1], nz * 0.01f * 0.15f, 0.012f, 0.001f, 1e-5f);
        xsvf_set(&v->xf[0], 14000.0f, XQ_DB0);
        xsvf_set(&v->xf[1], 2500.0f, XQ_DB07);
        break;
    }
    default: {                                        /* audio.js's voice: clap, tom, rim, noise */
        float glide;
        XLEN(0.025f + d * d * 1.8f);
        glide = 0.008f + len * 0.16f;
        v->rng = r0 * 0x9E3779B9u + 1u;               /* (Math.random: the voice's generator, on) */
        /* the voice's envelope, after the shaper: from its LEVEL (58 on these tracks of its kit) x 0.42 to an absolute
         * 0.0001, so that LEVEL sets the slope too */
        x_layer(&v->env2, &v->k2, 0.58f * 0.42f, len, 0.002f, 1e-4f);
        v->xn1 = 0;
        v->xn2 = 0;                                   /* the time since the hit (the clap's bursts) */
        v->xe[0] = 0.0f;
        v->xph2 = 0.0f;
        if (model == DM_XTOM) {                       /* a sine, gliding, FM'd by a sine at 1 + tone/16 */
            x_glide(&v->inc, &v->xm1, &v->xn1, fm_maxf(15.0f, hz * (1.0f + sweep / 14.0f)), fm_maxf(15.0f, hz), glide);
            v->xinc2 = fm_minf(hz * (1.0f + tone / 16.0f), 0.45f * SR) / SR;
            x_layer(&v->xe[0], &v->xk[0], hz * (fm / 55.0f) / SR, len, 0.0f, 0.01f / SR);
        } else if (model == DM_XRIM) {                /* a triangle, gliding, and a sine at 2.73 */
            x_glide(&v->inc, &v->xm1, &v->xn1, fm_maxf(15.0f, hz * (1.0f + sweep / 50.0f)), fm_maxf(15.0f, hz), glide);
            v->xinc2 = fm_minf(hz * 2.73f, 0.45f * SR) / SR;
        }
        v->xe[1] = nz * 0.01f * (model == DM_XCLAP || model == DM_XNOISE ? 0.9f : 0.2f);
        xsvf_set(&v->xf[0], fm_minf(19000.0f, 180.0f * fm_exp2f(tone / 15.0f)), XQ_DB07);
        break;
    }
    }
#undef XLEN
    v->xsa = a;
    v->xsn = 1.0f / tanh_r(a);
    v->xdx = v->xdy = 0.0f;
    v->ph = 0.0f;
}

/* ---- sophie: Sophie's voice (github.com/mestela/schwung-sophie; Copyright (c) 2026 Matt Estela and the digisophie
 * contributors, MIT: LICENSING.md), after SPH-16's port (its
 * render_voice: the original, checked there against Sophie's own C). Per hit (Sophie's Tail: Hit), Raw feedback;
 * its kick architecture is the seventh MODEL (Sophie's pad 1). Not here: the ring delay (its 3 KB a voice), the
 * filter types other than the low-pass (the key's filter does them), METAL DECAY, Smooth feedback, the steal's fade.
 * Its fixed low-pass stays: CUTOFF 100 still closes at about 6.7 kHz (f 0.95, q 1), part of its sound. DRIVE is the
 * factory pads' middle, 58 (the key's own DRIVE adds to it). Phases in turns; drumline's sine (sin_t, -60 dB
 * harmonics) for Sophie's (1e-4); the sweep's pitch exact every 16 samples, ramped between. Made at half the rate
 * (22.05 kHz, as digisophie: the busiest demo on Sophie's kit ran over on the FM-1 at the full rate, 102 %), the low-pass
 * after it at the full rate: about 160 instructions a sample, Sophie's envelopes as they were, its top octave gone. */
enum { SPH_FUSE, SPH_STACK, SPH_SPLIT, SPH_SHARD, SPH_PIPE, SPH_BOOM, SPH_KICK, NSPH };
#define SPH_CTL 16
#define SPH_IT (1.0f / FM_TWO_PI)        /* radians to turns */
static inline float sph_clip(float x) { return x / (1.0f + fm_fabsf(x)); }
static void sophie_start(dvoice_t *v, float note, float dec, const float *kn, const uint8_t *k2)
{
    int model = k2[0] % NSPH, kick = model == SPH_KICK;
    float color = kn[0], metal = kn[1], crush = (float)k2[3] * (1.0f / 127.0f);
    float hz = fm_clampf(440.0f * fm_exp2f((note - 69.0f) * (1.0f / 12.0f)), 20.0f, 10000.0f);
    float sweep = fm_clampf(((float)k2[2] - 64.0f) * (100.0f / 63.0f), -100.0f, 100.0f), r;
    if (kick)
        model = SPH_FUSE;
    v->smodel = (uint8_t)model;
    v->skick = (uint8_t)kick;
    v->scol = color;
    v->smet = metal;
    v->sfbk = (float)k2[1] * (1.0f / 127.0f);
    v->ssk = sweep * 0.01f * (kick ? 2.5f : 4.5f);
    v->env = 1.0f;                                    /* Sophie's level: -78 dB at DECAY */
    v->k = fm_expf(-8.9282f / (fm_clampf(dec, 0.03f, 16.0f) * SR));
    v->spe = 1.0f;
    v->spm = fm_expf(-1.0f / ((kick ? 0.075f : 0.028f) * SR));
    v->sme = 1.0f;
    v->smm = fm_expf(-1.0f / (0.135f * SR));
    v->sr1 = model == SPH_PIPE ? 1.31f + color * 6.0f : model == SPH_BOOM ? 0.60f + color * 4.2f : 0.47f + color * color * 7.6f;
    v->sr2 = 1.31f + color * 11.17f;
    v->sr3 = 2.07f + (1.0f - color) * 15.73f;
    v->sbase = hz / SR;
    v->ph = 0.0f;
    v->sm1 = 0.0f;
    r = noise(&v->rng) * 0.12f * SPH_IT;              /* a little phase variation: no machine gun */
    v->sm2 = r < 0.0f ? r + 1.0f : r;
    r = noise(&v->rng) * 0.08f * SPH_IT;
    v->sm3 = r < 0.0f ? r + 1.0f : r;
    v->sat = v->sfz = v->snz = v->sfl = v->sfb = v->sch = 0.0f;
    v->scd = (int16_t)(1 + (int)(crush * crush * 31.0f));
    v->scc = 0;
    v->scl = crush >= 0.005f ? fm_exp2f(15.0f - crush * 12.0f) : 0.0f;
    v->scs = v->scl > 0.0f ? 1.0f / v->scl : 0.0f;
    v->sctl = 0;
    v->inc = v->sbase * (v->ssk != 0.0f ? fm_clampf(fm_exp2f(v->ssk), 18.0f / hz, 18000.0f / hz) : 1.0f);
    v->sdi = 0.0f;
    v->env2 = 0.0f;
}
/* drive 58: Sophie's curve (its units 66, a gain of 5.95), and the gain that brings a full output back to 1 */
#define SPH_DG 5.95f
#define SPH_DN 1.14600f
static void sophie_render(dvoice_t *v, float *t, int m, float *envp)
{
    const int model = v->smodel, kick = v->skick, shaped = model >= SPH_PIPE;
    const int need_m2 = kick || model <= SPH_SPLIT, need_m3 = !kick && (model == SPH_STACK || model == SPH_SPLIT);
    const float color = v->scol, metal = v->smet, fbk = v->sfbk, sk = v->ssk, r1 = v->sr1, r2 = v->sr2, r3 = v->sr3;
    /* the voice is made at half the rate (22.05 kHz): each step two samples' worth (the envelopes' multipliers
     * squared, the phases two increments), the output between two steps their mean (digisophie's way); the
     * low-pass and the gain at the full rate, after */
    const float base = v->sbase, k = v->k * v->k, pm = v->spm * v->spm, mm = v->smm * v->smm, g = v->g * 0.24f;
    float pm16 = v->spm * v->spm;
    const float fbgain = fbk * (2.0f + metal * 10.0f) * SPH_IT, m2gain = metal * 2.5f * SPH_IT, m2sq = metal * metal * SPH_IT;
    const float crl = v->scl, crs = v->scs;
    float env = *envp, pe = v->spe, me = v->sme, att = v->sat, fz = v->sfz, nz = v->snz, fl = v->sfl, fb = v->sfb;
    float c = v->ph, inc = v->inc, di = v->sdi, m1p = v->sm1, m2p = v->sm2, m3p = v->sm3, ch = v->sch, y = v->slast;
    int i, ctl = v->sctl, cc = v->scc, cd = v->scd > 1 ? (v->scd + 1) / 2 : 1, tog = v->stog;
    uint32_t rng = v->rng;
    pm16 *= pm16;
    pm16 *= pm16;
    pm16 *= pm16;                                     /* (pm^16: 16 samples on) */
    for (i = 0; i < m; i++) {
        float o;
        if (tog) {                                    /* the second sample of the step: the step's own */
            o = y;
            tog = 0;
        } else {
            float bright, m1, m2 = 0.0f, m3 = 0.0f, idx, out, inc2;
            if (ctl == 0) {                           /* the sweep: the pitch 16 samples (8 steps) on, ramped to */
                float target = sk != 0.0f ? base * fm_clampf(fm_exp2f(sk * pe * pm16), 18.0f / (base * SR), 18000.0f / (base * SR)) : base;
                di = (target - inc) * (1.0f / (SPH_CTL / 2));
                ctl = SPH_CTL / 2;
            }
            ctl--;
            env *= k;
            pe *= pm;
            me *= mm;
            att += (1.0f - att) * 0.163775f;          /* (Sophie's 0.085 a sample, two of them) */
            bright = shaped ? me : pe;
            inc += di;
            inc2 = inc + inc;
            c += inc2;
            if (c >= 1.0f) c -= fm_floorf(c);
            m1p += inc2 * r1;
            if (m1p >= 1.0f) m1p -= fm_floorf(m1p);
            m1 = sin_t_any(m1p + fz * fbgain);
            if (need_m2) {
                m2p += inc2 * r2;
                if (m2p >= 1.0f) m2p -= fm_floorf(m2p);
                m2 = sin_t_any(m2p + m1 * m2gain);
            }
            if (need_m3) {
                m3p += inc2 * r3;
                if (m3p >= 1.0f) m3p -= fm_floorf(m3p);
                m3 = sin_t_any(m3p);
            }
            idx = m2sq * (2.0f + 24.0f * (0.25f + bright * 0.75f));   /* the FM depth, turns */
            if (kick) {                               /* a sine body, folded a little, a quiet FM texture */
                float body = sin_t_any(c), folded = sin_t_any(body * (1.0f + color * 2.2f) * SPH_IT);
                float tex = sin_t_any(c + m2 * (0.18f + fbk * 1.35f) * SPH_IT);
                nz += (noise(&rng) - nz) * 0.32f;
                out = body * (0.86f - color * 0.08f) + folded * (0.14f + color * 0.08f) + tex * metal * 0.18f + nz * pe * (0.012f + metal * 0.035f);
            } else
                switch (model) {
                default:                              /* FUSE: two modulators in series */
                    out = sin_t_any(c + idx * (m2 * 0.72f + m1 * 0.28f));
                    break;
                case SPH_STACK:
                    out = (sin_t_any(c + idx * 0.62f * m1) + 0.55f * sin_t_any(c * 0.501f + idx * 0.35f * m2) +
                           0.32f * sin_t_any(c * 1.997f + idx * 0.22f * m3)) * 0.56f;
                    break;
                case SPH_SPLIT:
                    out = 0.62f * sin_t_any(c + idx * m1) + 0.38f * sin_t_any(c * 1.003f - idx * 0.71f * m2) + 0.18f * m3;
                    break;
                case SPH_PIPE: {                      /* an FM carrier and an inharmonic ring */
                    float body = sin_t_any(c + idx * m1), rm = 0.3125f + metal * 0.5f + bright * 0.125f;
                    out = (body * (1.0f - rm) + body * m1 * rm) * 1.25f;
                    break;
                }
                case SPH_BOOM: {                      /* a clean fundamental, a metallic skin */
                    float em = 0.0625f + metal * bright * 0.5f;
                    out = sin_t_any(c) * (1.0f - em) + sin_t_any(c + idx * m1) * em;
                    break;
                }
                case SPH_SHARD:                       /* a 12-bit wavetable (32 steps) and noise */
                    nz += (noise(&rng) - nz) * (0.02f + color * 0.35f);
                    out = sin_t_any(fm_floorf(c * 32.0f) * (1.0f / 32.0f) + idx * 0.55f * m1) * (0.85f - metal * 0.25f) +
                          nz * metal * 0.42f * (shaped ? bright : 1.0f);
                    break;
                }
            fz = sph_clip(out * 1.4f);                /* the feedback (Raw) */
            out *= env * att;
            if (crl > 0.0f) {                         /* sample and hold (its count in steps), then fewer bits */
                if (cc <= 0) {
                    ch = out;
                    cc = cd;
                }
                cc--;
                out = fm_floorf(ch * crl + 0.5f) * crs;
            }
            out = sph_clip(out * SPH_DG) * SPH_DN;
            o = (y + out) * 0.5f;                     /* between the last step and this one */
            y = out;
            tog = 1;
        }
        fl += 0.95f * fb;                             /* the low-pass, last (CUTOFF 100), at the full rate */
        fb += 0.95f * (o - fl - fb);
        t[i] = fl * g;
    }
    v->ph = c;
    v->inc = inc;
    v->sdi = di;
    v->sm1 = m1p;
    v->sm2 = m2p;
    v->sm3 = m3p;
    v->spe = pe;
    v->sme = me < 1e-6f ? 0.0f : me;
    v->sat = att;
    v->sfz = fz;
    v->snz = nz;
    v->sfl = fm_flush(fl);
    v->sfb = fm_flush(fb);
    v->sch = ch;
    v->slast = y;
    v->scc = (int16_t)cc;
    v->sctl = (uint8_t)ctl;
    v->stog = (uint8_t)tog;
    v->rng = rng;
    *envp = env;
}
/* clang: digisophie's core, its controls from the knobs (pitch the note; model, sweep, feedback, fold on the
 * second page) */
static void clang_start(dvoice_t *v, float note, const float *kn, const uint8_t *k2, int vel)
{
    float hz = 440.0f * fm_exp2f((note - 69.0f) * (1.0f / 12.0f));
    v->dp.phase_inc = (uint16_t)fm_clampf(hz * (65536.0f / SR), 8.0f, 24576.0f);   /* (digitakt.c's units) */
    v->dp.color = (uint8_t)(kn[0] * 127.0f + 0.5f);
    v->dp.metal = (uint8_t)(kn[1] * 127.0f + 0.5f);
    v->dp.model = (uint8_t)(k2[0] & 3u);
    v->dp.sweep = (int8_t)((int)k2[1] - 64);
    v->dp.feedback = k2[2];
    v->dp.velocity = (uint8_t)vel;
    v->dfold = k2[3];
    v->dtrig = 1;
    ds_voice_init(&v->ds);
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
    case DM_XKICK: case DM_XSNARE: case DM_XHAT: case DM_XCLAP: case DM_XTOM: case DM_XRIM: case DM_XFM:
    case DM_XNOISE: {
        const uint8_t *px = par + TP_DRUMX(sound, 0);
        const int8_t *map = XMAP[model - DM_XKICK];
        float xp[NXP], kv[8] = {k1, k2, k3, k4, (float)px[0] * (1.0f / 127.0f), (float)px[1] * (1.0f / 127.0f),
                                (float)px[2] * (1.0f / 127.0f), (float)px[3] * (1.0f / 127.0f)};
        for (i = 0; i < NXP; i++)
            xp[i] = XFIX[i];
        for (i = 0; i < 8; i++)
            if (map[i] >= 0)
                xp[map[i]] = kv[i] * (map[i] == XP_PITCH ? 127.0f : 100.0f);
        x_start(v, model, xp[XP_PITCH] + semi, xp[XP_DECAY], xp[XP_TONE], xp[XP_SWEEP], xp[XP_NOISE], xp[XP_FM],
                xp[XP_DRIVE], lscale, flags);
        break;
    }
    case DM_SOPHIE: {
        float kn[2] = {k3, k4};
        sophie_start(v, fm_clampf(k1 * 127.0f + semi, 0.0f, 127.0f), dec, kn, par + TP_DRUMX(sound, 0));
        break;
    }
    case DM_CLANG: {                                  /* the length: the key's envelope (env / k), as the
                                                       * Digitakt's AMP was digisophie's */
        float kn[2] = {k3, k4};
        clang_start(v, fm_clampf(k1 * 127.0f + semi, 0.0f, 127.0f), kn, par + TP_DRUMX(sound, 0), vel);
        v->g = sp->trim;                              /* (the velocity: the core's own, into its soft clip) */
        v->env2 = 0.0f;
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
    if (model < DM_SAMPLE)
        v->inc = hz / SR;
}

#ifdef PLAT_HOST
uint32_t (*drumline_host_clock)(void);           /* (the simulator's profiler: each key's time, drumline.h) */
uint32_t drumline_host_cost[DL_NSOUND][2];
#define DHC_T(v) uint32_t v = drumline_host_clock ? drumline_host_clock() : 0
#define DHC_ADD(k, j, v) do { if (drumline_host_clock) drumline_host_cost[k][j] += drumline_host_clock() - (v); } while (0)
#else
#define DHC_T(v)
#define DHC_ADD(k, j, v)
#endif
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
    {
        DHC_T(t0);
        start(d, v, sound, vel, pitch_semi, (short_gate ? HF_SHORT : 0) | (long_tail ? HF_LONG : 0), 1);
        DHC_ADD(sound, 1, t0);
    }
}


/* the x models' sample loop pieces */
#define XRISE(n, na) ((n) > 0 ? 1.0f - (float)(n) * (1.0f / (float)(na)) : 1.0f)
static inline float x_shape(float x, float a, float nrm)        /* the waveshaper's curve (its input clamped) */
{
    return tanh_r(fm_clampf(x, -1.0f, 1.0f) * a) * nrm;
}
#define XDC_R 0.99786534f             /* the DC blocker: 15 Hz (drums.js: a 15 Hz high-pass) */
static inline float x_tri(float ph)                             /* Web Audio's triangle: from 0, rising */
{
    float q = ph + 0.25f;
    if (q >= 0.5f)
        q -= 1.0f;
    return 4.0f * fm_fabsf(q) - 1.0f;
}
/* a square's polyBLEP at an edge (phase p, increment i, 2^32 a turn) */
static inline float x_blep(uint32_t p, uint32_t i)
{
    if (p < i) {
        float x = (float)p / (float)i;
        return x + x - x * x - 1.0f;
    }
    if (p > 0u - i) {
        float x = -(float)(0u - p) / (float)i;
        return x * x + x + x + 1.0f;
    }
    return 0.0f;
}
/* a square's band-limiting, added to its naive +1 / -1 (the six naive squares are counted from their top bits): 0
 * unless an edge is within a step (p and p +- i straddle one), the usual case */
static inline float x_sqfix(uint32_t p, uint32_t i)
{
    if (!(((p ^ (p + i)) | (p ^ (p - i))) >> 31))
        return 0.0f;
    return x_blep(p, i) - x_blep(p ^ 0x80000000u, i);
}
#define XWRAP2(ph) do { if ((ph) >= 0.5f || (ph) < -0.5f) (ph) -= fm_floorf((ph) + 0.5f); } while (0)   /* (an FM'd
                                                                                 * phase may move a turn or more) */

/* the x models, m samples into t[]: env / k the gate (a choke), env2 / k2 layer 1, ph / inc oscillator 1. Returns
 * with env 0 when the layers have all faded (the voice ends) */
static void x_render(dvoice_t *v, float *t, int m, float *envp, float *env2p, float *php)
{
    int i, model = v->model;
    float env = *envp, k = v->k, env2 = *env2p, k2 = v->k2, ph = *php, inc = v->inc, g = v->g;
    int32_t cnt = v->cnt;
    uint32_t rng = v->rng;
    if (model == DM_XHAT) {
        uint32_t p0 = v->hp[0], p1 = v->hp[1], p2 = v->hp[2], p3 = v->hp[3], p4 = v->hp[4], p5 = v->hp[5];
        const uint32_t i0 = v->hi[0], i1 = v->hi[1], i2 = v->hi[2], i3 = v->hi[3], i4 = v->hi[4], i5 = v->hi[5];
        xsvf_t f0 = v->hf[0], f1 = v->hf[1], f2 = v->hf[2];
        float he = v->he, hk = v->hk, sa = v->hsa, sn = v->hsn, dx = v->hdx, dy = v->hdy;
        for (i = 0; i < m; i++) {
            float r = XRISE(cnt, XA_DRUM), sq, y;
            if (cnt > 0)
                cnt--;
            else {
                env2 *= k2;
                he *= hk;
            }
            sq = (float)(6 - 2 * (int32_t)((p0 >> 31) + (p1 >> 31) + (p2 >> 31) + (p3 >> 31) + (p4 >> 31) + (p5 >> 31))) +
                 x_sqfix(p0, i0) + x_sqfix(p1, i1) + x_sqfix(p2, i2) + x_sqfix(p3, i3) + x_sqfix(p4, i4) + x_sqfix(p5, i5);
            p0 += i0; p1 += i1; p2 += i2; p3 += i3; p4 += i4; p5 += i5;
            y = xsvf_bp(&f1, xsvf_hp(&f0, sq * env2 * r)) + xsvf_hp(&f2, xnoise(&rng) * he * r);
            y = x_shape(y, sa, sn);
            {
                float o = y - dx + XDC_R * dy;
                dx = y;
                dy = o;
                t[i] = o * g;
            }
        }
        v->hp[0] = p0; v->hp[1] = p1; v->hp[2] = p2; v->hp[3] = p3; v->hp[4] = p4; v->hp[5] = p5;
        f0.s1 = fm_flush(f0.s1); f0.s2 = fm_flush(f0.s2); f1.s1 = fm_flush(f1.s1); f1.s2 = fm_flush(f1.s2);
        f2.s1 = fm_flush(f2.s1); f2.s2 = fm_flush(f2.s2);
        v->hf[0] = f0; v->hf[1] = f1; v->hf[2] = f2;
        v->he = he;
        v->hdx = dx;
        v->hdy = fm_flush(dy);
        if (cnt == 0 && env2 < 2e-5f && he < 2e-5f)
            env = 0.0f;
    } else {
        float ph2 = v->xph2, inc2 = v->xinc2, m1 = v->xm1, m2 = v->xm2;
        int32_t n1 = v->xn1, n2 = v->xn2;
        float e0 = v->xe[0], e1 = v->xe[1], e2 = v->xe[2], k0 = v->xk[0], k1 = v->xk[1], kk2 = v->xk[2];
        xsvf_t f0 = v->xf[0], f1 = v->xf[1], f2 = v->xf[2];
        float sa = v->xsa, sn = v->xsn, dx = v->xdx, dy = v->xdy;
        switch (model) {
        case DM_XKICK:
            for (i = 0; i < m; i++) {
                float r = XRISE(cnt, XA_DRUM), y, o;
                if (cnt > 0)
                    cnt--;
                else {
                    env2 *= k2;
                    e0 *= k0;
                    e1 *= k1;
                }
                if (n1 > 0) { inc *= m1; n1--; }
                if (n2 > 0) { inc2 *= m2; n2--; }
                ph += inc;
                WRAP(ph);
                ph2 += inc2;
                WRAP(ph2);
                y = xsvf_lp(&f0, (sin_t(ph) * env2 + x_tri(ph2) * e0) * r) + xsvf_hp(&f1, xnoise(&rng) * e1 * r);
                y = x_shape(y, sa, sn);
                o = y - dx + XDC_R * dy;
                dx = y;
                dy = o;
                t[i] = o * g;
            }
            if (cnt == 0 && env2 < 2e-5f && e0 < 2e-5f && e1 < 2e-5f)
                env = 0.0f;
            break;
        case DM_XSNARE:
            for (i = 0; i < m; i++) {
                float r = XRISE(cnt, XA_DRUM), y, o, n;
                if (cnt > 0)
                    cnt--;
                else {
                    env2 *= k2;
                    e0 *= k0;
                    e1 *= k1;
                    e2 *= kk2;
                }
                if (n1 > 0) { inc *= m1; n1--; }
                if (n2 > 0) { inc2 *= m2; n2--; }
                ph += inc;
                WRAP(ph);
                ph2 += inc2;
                WRAP(ph2);
                n = xnoise(&rng) * r;
                y = xsvf_lp(&f0, (x_tri(ph) * env2 + sin_t(ph2) * e0) * r) + xsvf_bp(&f1, n * e1) + xsvf_hp(&f2, n * e2);
                y = x_shape(y, sa, sn);
                o = y - dx + XDC_R * dy;
                dx = y;
                dy = o;
                t[i] = o * g;
            }
            if (cnt == 0 && env2 < 2e-5f && e0 < 2e-5f && e1 < 2e-5f && e2 < 2e-5f)
                env = 0.0f;
            break;
        case DM_XFM:
            for (i = 0; i < m; i++) {
                float r = XRISE(cnt, XA_DRUM), y, o, mo;
                if (cnt > 0)
                    cnt--;
                else {
                    env2 *= k2;
                    e1 *= k1;
                }
                mo = sin_t(ph2);
                ph2 += inc2;
                WRAP(ph2);
                y = xsvf_lp(&f0, sin_t(ph) * env2 * r) + xsvf_hp(&f1, xnoise(&rng) * e1 * r);
                ph += inc + e0 * mo;                  /* the carrier's frequency, modulated in Hz */
                XWRAP2(ph);
                e0 *= k0;
                y = x_shape(y, sa, sn);
                o = y - dx + XDC_R * dy;
                dx = y;
                dy = o;
                t[i] = o * g;
            }
            if (cnt == 0 && env2 < 2e-5f && e1 < 2e-5f)
                env = 0.0f;
            break;
        default: {                                    /* audio.js's voice: the sources, the low-pass, the shaper,
                                                       * the envelope */
            int32_t tt = n2;
            float lv = e1;
            for (i = 0; i < m; i++) {
                float r = XRISE(cnt, XA_SIMPLE), x = 0.0f, y, nl = lv;
                if (cnt > 0)
                    cnt--;
                else
                    env2 *= k2;
                if (model == DM_XTOM) {
                    if (n1 > 0) { inc *= m1; n1--; }
                    x = sin_t(ph);
                    ph += inc + e0 * sin_t(ph2);
                    XWRAP2(ph);
                    ph2 += inc2;
                    WRAP(ph2);
                    e0 *= k0;
                } else if (model == DM_XRIM) {
                    if (n1 > 0) { inc *= m1; n1--; }
                    x = x_tri(ph) * 0.65f + sin_t(ph2) * 0.3f;
                    ph += inc;
                    WRAP(ph);
                    ph2 += inc2;
                    WRAP(ph2);
                } else if (model == DM_XCLAP) {       /* three bursts 12 ms apart (each a 9 ms fall to 0.01), the
                                                       * tail from 38 ms */
                    if (tt >= 1676)
                        nl = lv * 0.65f;
                    else {
                        int32_t u = tt - (tt >= 1058 ? 1058 : tt >= 529 ? 529 : 0);
                        nl = u < 397 ? lv + (0.01f - lv) * (float)u * (1.0f / 397.0f) : 0.01f;
                    }
                    tt++;
                }
                x += noise(&rng) * nl;
                y = x_shape(xsvf_lp(&f0, x), sa, sn);
                t[i] = y * env2 * r * g;
            }
            n2 = tt;
            if (cnt == 0 && env2 < 1e-4f)
                env = 0.0f;
            break;
        }
        }
        v->xph2 = ph2;
        v->xinc2 = inc2;
        v->xn1 = n1;
        v->xn2 = n2;
        v->xe[0] = e0; v->xe[1] = e1; v->xe[2] = e2;
        f0.s1 = fm_flush(f0.s1); f0.s2 = fm_flush(f0.s2); f1.s1 = fm_flush(f1.s1); f1.s2 = fm_flush(f1.s2);
        f2.s1 = fm_flush(f2.s1); f2.s2 = fm_flush(f2.s2);
        v->xf[0] = f0; v->xf[1] = f1; v->xf[2] = f2;
        v->xdx = dx;
        v->xdy = fm_flush(dy);
    }
    if (k != 1.0f)                                    /* a choke: the gate falls (it is 1 otherwise) */
        for (i = 0; i < m; i++) {
            t[i] *= env;
            env *= k;
        }
    v->inc = inc;
    v->cnt = cnt;
    v->rng = rng;
    *envp = env;
    *env2p = env2;
    *php = ph;
}
/* clang, m samples: digisophie's core (its 24 kHz pairs, its fold), times the key's length (env / k) */
static void clang_render(dvoice_t *v, float *t, int m, float *envp)
{
    int32_t buf[SUB];
    int i;
    float env = *envp, k = v->k, g = v->g * (1.0f / 2147483648.0f);
    ds_voice_render(&v->ds, &v->dp, v->dtrig, buf, (uint32_t)m);
    v->dtrig = 0;
    ds_fold_block(buf, (uint32_t)m, v->dfold);
    for (i = 0; i < m; i++) {
        t[i] = (float)buf[i] * env * g;
        env *= k;
    }
    *envp = env;
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
    case DM_XKICK: case DM_XSNARE: case DM_XHAT: case DM_XCLAP: case DM_XTOM: case DM_XRIM: case DM_XFM:
    case DM_XNOISE:
        x_render(v, t, m, &env, &env2, &ph);          /* (it ends the voice with env 0, layer 1 under 2e-5) */
        rng = v->rng;
        break;
    case DM_CLANG:
        clang_render(v, t, m, &env);
        break;
    case DM_SOPHIE:
        sophie_render(v, t, m, &env);
        ph = v->ph;                                   /* (its own: written back below) */
        rng = v->rng;
        break;
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

/* a voice at a time over the whole call, then the bus a sub-block at a time: on the FM-1 the models' code runs from
 * the flash through a small cache, and a sub-block at a time every voice's code came in again for each one (a kit of
 * five models taking turns forty times a block). Each sample's sum is the voices' in the same order as
 * before, the bus sees the same sub-blocks in the same order: the same output. The buffers start at 0 (the bus
 * filters l / r in place) */
void drumline_render(drumline_t *d, float *l, float *r, float *s1, float *s2, uint32_t n)
{
    uint32_t c, any = 0;
    int vi;
    for (vi = 0; vi < NDVOICE; vi++) {
        dvoice_t *v = &d->v[vi];
        for (c = 0; c < n && v->on; c += CR) {
            int m = (int)(n - c < CR ? n - c : CR), o;
            control(d->par, v, m);
            for (o = 0; o < m && v->on; o += SUB) {
                float t[SUB];
                int mm = m - o < SUB ? m - o : SUB, i, from = 0;
                float *pl = l + c + o, *pr = r + c + o, *p1 = s1 + c + o, *p2 = s2 + c + o;
                float gl, gr, att, attk;
                const float gs1 = v->gs1, gs2 = v->gs2, dgl = v->dgl, dgr = v->dgr;
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
                any |= 1u << ((c + (uint32_t)o) / SUB);
                {
                    DHC_T(t0);
                    render_voice(v, d->line[v->line],
                                 v->sci && d->sc_owner[v->sci - 1] == vi ? &d->sc[v->sci - 1] : 0, t + from, mm - from);
                    key_post(d->par + TP_DRUM(v->sound, 0), v, t + from, mm - from);
                    DHC_ADD(v->sound, 0, t0);
                }
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
                    pl[i] += x * gl;
                    pr[i] += x * gr;
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
        }
    }
    if (d->f.on)                                  /* the bus: the dynamic filter, a sub-block at a time where a voice
                                                   * sounded or its envelope still rings */
        for (c = 0; c < n; c += SUB) {
            int mm = (int)(n - c < SUB ? n - c : SUB);
            if ((any >> (c / SUB) & 1u) || d->f.env > 1e-5f)
                dynf_run(&d->f, l + c, r + c, mm);
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
