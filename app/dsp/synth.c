/* SPDX-License-Identifier: GPL-3.0-only */
/* LOOM-8 voices: sixteen voices playing fourteen engines (synth.h), each with two ADSRs, a filter (six
 * types), a character stage (five types: drive, fold, crush, ring, tilt), a steal fade and sends; per track an LFO, a mono stack, parameter locks and
 * performance offsets.
 *
 * Time runs in SY_CHUNK (64-sample) chunks: at a chunk boundary a voice reads its track's parameters
 * (the knob, or its lock, plus the LFO's offset on the destination) and turns them into the units the
 * sample loop wants (increments, envelope rates, gains); inside the chunk only the per-sample state
 * moves, and the filter's coefficients refresh every 16 samples. A render of any length splits itself
 * at those boundaries, and every voice carries its own position in the chunk, so a block rendered as
 * 100 + 156 frames equals one of 256, and voices 0..8 on one core and 8..16 on the other never share a
 * word that either writes: the track-level values they read (LFO, pump, wobble) are computed into
 * per-chunk slots by synth_prepare() before the block is split. The LFO's fade envelope is the
 * voice's own (it counts from its note), so it scales the track's slot values per voice.
 *
 * Levels: an engine's raw cycle peaks near 1; the amp envelope, velocity (0.25 + 0.75 vel^2), the
 * preset volume ((v/127)^2) and the mix level (v/127) bring a default note to about -9 dBFS, so a
 * chord of six leaves the master's limiter something to do but not much. Float, no libm (fastmath.h),
 * no malloc, no stack buffer over 128 bytes in the render path (the audio interrupt's stack).
 *
 * brocade's saw bank is NuSaw's (the NuSaw plugin, Copyright (c) Charles Vestal, MIT; through its FM-1 port,
 * fm1-nusaw): the detune offsets and curve, the mix and its power normalisation, the pans, the keytracked
 * high-pass. cane, voile and gingham are Mutable Instruments Braids' models (Copyright 2012 Emilie Gillet, MIT),
 * ported in dsp/braids.h (MIT), included below. */
#include "synth.h"
#include "fastmath.h"
#include "drumline.h"
#include "sample.h"
#include "wavetables.h"

#define SUB 16                                /* filter coefficient refresh, steal fade steps */
#define NCK 8                                 /* chunk slots a prepare may cover (n <= 512) */
#define STEAL_RATE (1.0f / (0.015f * SY_SR))  /* the 15 ms steal fade, per sample */
#define INV_SR (1.0f / SY_SR)
#define CHUNK_S ((float)SY_CHUNK / SY_SR)     /* a chunk in seconds */
#define BRIGHT_SLEW 0.13f                    /* a chunk's share of the way: about 10 ms to 1 / e */
#define LD_PITCH (-2)                         /* the LFO destination "pitch" (no TP_* holds it) */
#define LD_NONE (-1)
/* the sample loops must be inlined into their callers (the compiler folds their constant arguments
 * away); left to itself it outlines the bigger ones, at a call with thirty spills a sub-block */
#if defined(__GNUC__) || defined(__clang__)
#define SY_INLINE static inline __attribute__((always_inline))
#else
#define SY_INLINE static inline
#endif

/* ------------------------------------------------------------------ names --- */
const char *const RL_NAME[NRL] = {"bass", "lead", "pad", "keys", "pluck", "bell", "stab", "strings", "brass", "voice",
                                  "chord", "fx", "drum", "loop"};
const char *const EG_NAME[NENGINE] = {"weft", "stack", "wavetable", "orbit", "tines", "organ", "sync",
                                      "swarm", "kit", "sample", "slice", "brocade", "cane", "voile", "gingham"};
const char *const EG_KNOB[NENGINE][4] = {
    {"bend", "wave", "sweep", "spread"},   {"wave", "ratio", "detune", "spread"}, {"table", "scan", "bend", "ripple"},
    {"bright", "ratio", "edge", "wobble"}, {"bright", "grit", "bite", "bell"},    {"stops", "pedal", "rotor", "rate"},
    {"reach", "sub", "hiss", "thin"},      {"swarm", "ring", "tone", "spread"},   {"-", "-", "-", "-"},
    {"sample", "start", "length", "mode"},  {"sample", "slices", "pitch", "gate"}, {"saws", "detune", "mix", "spread"},
    {"model", "timbre", "color", "damp"},   {"model", "timbre", "color", "breath"}, {"model", "timbre", "color", "arp"}};
/* each engine's plain sound (INIT), 0..127; the organ's four and stack's ratio are discrete 1..8, brocade's saws 1..7,
 * the Braids engines' models and gingham's arp a name each (synth.h NCANE ...) */
static const uint8_t EG_DEF[NENGINE][4] = {{40, 0, 20, 0}, {0, 2, 12, 64}, {0, 40, 0, 0}, {40, 25, 0, 0},
                                           {50, 0, 40, 50}, {3, 4, 2, 5},  {30, 0, 0, 0}, {30, 0, 30, 20},
                                           {0, 0, 0, 0},    {5, 0, 127, SM_ONESHOT}, {SMP_BREAK, 1, 64, 127},
                                           {7, 50, 90, 70}, {0, 64, 64, 0}, {2, 0, 64, 0}, {1, 78, 84, 0}};
static const char *const VMODE_N[3] = {"poly", "mono", "legato"};
static const char *const FTYPE_N[NFT] = {"lp24", "ladder", "lp12", "hp12", "dynamic", "bp12"};
static const char *const LTYPE_N[NLT] = {"off", "wave", "wobble", "drift", "pump"};
static const char *const VSHAPE_N[4] = {"square", "sine", "ramp", "saw"};
static const char *const TSHAPE_N[5] = {"sine", "saw", "expo", "square", "blip"};
static const char *const LDEST_N[NLD] = {"synth", "envelope", "filter", "voice", "synth free", "env free", "filter free",
                                         "voice free"};
static const char *const DUCK_N[11] = {"track 1", "track 2", "track 3", "track 4", "track 5", "track 6", "track 7",
                                       "track 8", "conductor", "throws", "metro"};
static const char *const PLAYER_N[NPL] = {"off", "arp", "strum", "hold"};
static const char *const ARPSPD_N[9] = {"1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/32T", "1/64"};
static const char *const ARPPAT_N[6] = {"up", "down", "bounce", "bounce+", "played", "random"};
static const char *const ARPLEN_N[3] = {"short", "medium", "long"};
static const char *const ARPSTY_N[6] = {"off", "pedal", "thirds", "double", "skip", "swap"};
static const char *const ARPGLD_N[3] = {"off", "low", "high"};
static const char *const ARPSTE_N[3] = {"mono", "narrow", "wide"};
static const char *const MPAT_N[4] = {"up", "down", "updown", "random"};
static const char *const BRAIN_N[3] = {"no", "diatonic", "transp"};
static const char *const BEND_N[9] = {"off", "1", "2", "3", "4", "5", "6", "7", "octave"};
static const char *const WTNAME_N[WT_NTABLE] = {"shapes", "nasal", "grit", "vocal", "spiral", "fold", "octaves",
                                                "glass", "chirp"};
static const char *const SYNC_N[LS_SYNC_N] = {"2/1", "1", "1/2", "1/4", "1/8", "1/16", "1/32", "1/64"};
static const float SYNC_BEATS[LS_SYNC_N] = {8.0f, 4.0f, 2.0f, 1.0f, 0.5f, 0.25f, 0.125f, 0.0625f};
static const char *const ENVP_N[4] = {"attack", "decay", "sustain", "release"};
static const char *const FLTP_N[4] = {"cutoff", "reso", "env", "keytrack"};
static const char *const AMPP_N[4] = {"volume", "pitch", "pan", "character"};
static const char *const OCT_N[5] = {"-2", "-1", "0", "+1", "+2"};
static const char *const CTYPE_N[NCT] = {"off", "drive", "fold", "crush", "ring", "tilt", "tape", "sampler"};
static const char *const SMODE_N[NSM] = {"one-shot", "loop", "reverse", "ping-pong"};
static const char *const SLICES_N[SL_NCOUNT] = {"4", "8", "16", "32"};
static const char *const DFTYPE_N[NDF] = {"off", "lp", "hp", "bp"};
static const char *const CANE_N[NCANE] = {"pluck", "bow", "reed", "flute", "bell", "drum"};   /* (dsp/braids.h) */
static const char *const VOILE_N[NVOILE] = {"vosim", "vowel", "choir", "whisper"};
static const char *const GING_N[NGINGHAM] = {"toy", "chord", "ring", "peak", "band", "noise"};
static const char *const ARP_N[NARP] = {"off", "oct", "fifth", "power", "major", "minor", "maj7", "min7", "sus4"};
/* ring: the modulator's ratios to the note, tone / 8 */
static const float RING_R[16] = {0.25f, 0.5f, 0.75f, 1.0f, 1.25f, 1.414f, 1.5f, 1.75f, 2.0f, 2.5f, 2.76f, 3.0f, 3.5f,
                                 4.0f, 5.0f, 7.0f};
static const char *const RING_N[16] = {"0.25:1", "0.5:1", "0.75:1", "1:1", "1.25:1", "1.41:1", "1.5:1", "1.75:1", "2:1",
                                       "2.5:1", "2.76:1", "3:1", "3.5:1", "4:1", "5:1", "7:1"};   /* (as stack's ratio) */
/* the LFO's module / knob to a parameter index */
static const int16_t LD_TP[4][4] = {{TP_E1, TP_E2, TP_E3, TP_E4},
                                    {TP_A, TP_D, TP_S, TP_R},
                                    {TP_CUT, TP_RES, TP_FENV, TP_KTRK},
                                    {TP_VOLUME, LD_PITCH, TP_PAN, TP_CAMT}};

/* --------------------------------------------------------------- structs --- */
enum { ENV_OFF, ENV_ATTACK, ENV_DECAY, ENV_SUSTAIN, ENV_RELEASE };
typedef struct { uint8_t stage; float level; } env_t;

/* a voice's chunk configuration: the parameters in sample-loop units */
typedef struct {
    float freq;                       /* Hz, after glide, bend, vibrato */
    float amp_ar, amp_dc, amp_sl, amp_rc, flt_ar, flt_dc, flt_sl, flt_rc;
    float cut_oct;                    /* log2(cutoff / 50 Hz) without the envelope */
    float fenv_oct;                   /* the filter envelope's reach, octaves */
    float kq;                         /* 1 / Q */
    float kres;                       /* the ladder's feedback 0..4 */
    float gl, gr;                     /* dry gains (pan, level, volume, velocity, wobble, pump) */
    float g1, g2;                     /* sends, relative to the dry level */
    float pitch_off;                  /* cents added to the note (bend, perf, vibrato, an LFO on pitch) */
    float e[4];                       /* the engine knobs 0..1 */
    float ek[4];                      /* the same, raw (the discrete knobs: 1..8, a table index) */
    float ex[23];                     /* the engine's derived values (eng_prep; brocade's sub: ex[20..22]) */
    float cx[8];                      /* the character stage's values for the chunk (char_prep) */
    float cin[3];                     /* what cx[0..5] were made from (amount, tone, the ring's frequency) */
    uint8_t cin_type;                 /* and the type (0xFF: none yet) */
    uint8_t ftype, stereo, mute, eng;
    uint8_t ctype;                    /* the character stage running (CT_OFF: skipped, also at mix 0) */
} cfg_t;

typedef struct {
    uint8_t trk, held, engine, steal_phase;   /* held: the key is down; steal: 1 fading out, 2 in */
    uint8_t note, pend_note, pend_vel, pend_held;
    uint32_t tag, pend_tag, age;
    float vel, pitch, pitch_cur, glide_k;     /* semitones; glide_k: the chunk's approach fraction */
    int16_t pend_glide;
    uint16_t pos;                     /* sample in the chunk */
    uint8_t ck;                       /* chunk slot since prepare */
    uint8_t nchunk;                   /* chunks so far (the slow refreshes count on it, not on the block size) */
    env_t amp, flt;                   /* stepped once a sub-block (SUB samples) */
    uint8_t fresh;                    /* configured mid-block: refresh the coefficients before the next sample;
                                       * bit 1: started since the last render, its first configuration due there */
    uint8_t vi;                       /* the voice's index (its sample cache, smp_vc) */
    float cost_m;                     /* its render's measured time, clock ticks a frame (a running mean; 0 none yet) */
    uint8_t tws, tws_pend;            /* cane: the line slot it plays from, and its pending note's (CN_NONE) */
    float amp_cur, amp_step;          /* the amp level, ramped inside the sub-block */
    float steal_gain, sg_step;
    float glc, grc, gl_step, gr_step; /* the dry gains, ramped */
    float a1, a2, a3, b1, b2, b3;     /* the filter's coefficients (TPT SVF, lp24's second stage; the ladder's g and k) */
    float fhz, fkq, fkres;            /* what they were made from (a held note's are its last sub-block's: kept) */
    float fco, flv, ffe;              /* and what fhz was (the cutoff, the filter envelope's level and reach) */
    uint8_t fty;
    float f[8];                       /* filter: ic1 ic2 jc1 jc2 of L, then R (the ladder: its four poles a side) */
    float peak;
    float tsec;                       /* seconds since the note (the LFO's fade envelope counts on it) */
    uint32_t rng;
    cfg_t c;
    float env_in[8];                  /* the envelope knobs the coefficients were made from */
    float pan_in, pan_l, pan_r;       /* the pan the gains were made from, and those gains */
    float pitch_in, e_in[4];          /* the pitch and the knobs the frequency and increments were made from */
    float o[16];                      /* the engine's state */
    float cz[8];                      /* the character stage's: low-pass / hold L R, DC L R, phase, -, the
                                       * sampler's output filter L R (tape: cz[4..5] its wow's phases) */
} voice_t;
/* the sample engine's decoded windows (dsp/sample.h), a voice each: beside the struct, in RAM (the pool is
 * the scarcer); each is touched only by its own voice's render, on whichever core renders it */
static smp_cache_t smp_vc[NVOICE];

#define NTP_LOCK TP_DRUM0             /* the synth's locks: the kit's own parameters never reach a voice */
typedef struct {
    uint8_t lock[NTP_LOCK];
    perf_t perf;
    int16_t bend_c;
    uint8_t pb_snap;                  /* a note started: the performance pitch jumps to its value, no glide */
    float pb_last, pb_base, pb_step;  /* the performance pitch (cents), glided across a stretch's chunks from the
                                       * last stretch's value: bends and the warble smooth in long stretches */
    uint8_t mono_n, mono_note[8], mono_vel[8];
    uint32_t mono_tag[8];
    int8_t mono_voice;
    float last_pitch;                 /* the previous note: poly glide starts here */
    float cost_m;                     /* its voices' measured time (voice_t cost_m): a new note's first guess */
    float lfo_ph, lfo_sh, lfo_sm;     /* the LFO's phase, the drift's target and its slewed value */
    float duck, duck_hold;            /* the pump's level 0..1 and the hold time left, seconds */
    float lf_in, lf_out;              /* the fade envelope: seconds to fade in (or out), 0 = none */
    float lscale;                     /* the destination's range / 127: the offset in the knob's units */
    int16_t ldest;                    /* the resolved destination: a TP_* index, LD_PITCH or LD_NONE */
    uint32_t rng;
    float loff[NCK], lpitch[NCK], lgain[NCK];   /* the chunk slots: offset (param units), cents, gain */
} trk_t;

struct synth {
    uint8_t (*par)[NTP];
    float bpm;
    uint32_t counter;                 /* voice ages */
    uint16_t pos;                     /* chunk phase at the next prepare */
    uint8_t nck, max_voices;
    trk_t t[NTRK];
    voice_t v[NVOICE];
};
_Static_assert(sizeof(struct synth) <= SYNTH_BYTES, "synth_t outgrew SYNTH_BYTES");
#ifdef PLAT_HOST
uint16_t synth_host_solo = 0xFFFFu;           /* the host's stems: bits 0-7 the tracks, 8 / 9 the FX returns */
int synth_sizeof(void) { return (int)sizeof(struct synth); }
#endif

volatile uint16_t synth_peak[NTRK];
uint32_t (*synth_clock)(void);
volatile uint32_t synth_prof[2][NENGINE + 1];
volatile uint32_t synth_vblk[NVOICE];                 /* each voice's time this block (the profiler; engine.c clears it) */
volatile uint8_t synth_vinfo[NVOICE];                 /* and what it was: engine | 32 started this block | 64 core 1 | 128 sounded */
volatile uint32_t synth_vworst[3];                   /* the slowest voice render since the reader cleared it: its time, its
                                                      * engine, its frames */

/* ------------------------------------------------------------ small maths --- */
static inline uint32_t xs(uint32_t *r)
{
    uint32_t x = *r;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *r = x;
    return x;
}
static inline float frand(uint32_t *r) { return (float)(int32_t)xs(r) * (1.0f / 2147483648.0f); }   /* -1..1 */
static inline float blep(float t, float dt, float idt)      /* the PolyBLEP residual at phase t, step dt */
{
    if (t < dt) {
        t *= idt;
        return t + t - t * t - 1.0f;
    }
    if (t > 1.0f - dt) {
        t = (t - 1.0f) * idt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}
static inline float wrap1(float p) { return p - fm_floorf(p); }
static inline float step1(float p, float d)      /* a phase accumulator: p + d wrapped to [0, 1), d < 1 */
{
    p += d;
    return p >= 1.0f ? p - 1.0f : p;
}
static inline float soft3(float x)               /* a cubic soft clip: linear near 0, flat at +-1.5 */
{
    x = fm_minf(fm_maxf(x, -1.5f), 1.5f);        /* (the FPU's max and min: two instructions; fm_clampf takes four) */
    return x - x * x * x * (4.0f / 27.0f);
}
static float env_seconds(float p) { return p < 0.001f ? 0.001f : 0.001f * fm_exp2f(p * 13.2877124f); }  /* 1 ms .. 10 s */
/* the envelopes step once a sub-block of SUB samples: the attack's increment, the decay's and the
 * release's multiplier (to 0.1 % in secs) for that step */
static float env_rate(float secs) { return (float)SUB / (secs * SY_SR); }
static float env_coef(float secs) { return fm_expf(-6.9f * (float)SUB / (secs * SY_SR)); }
static float lfo_hz(int v, float bpm)
{
    if (v < LS_SYNC_N)
        return bpm * (1.0f / 60.0f) / SYNC_BEATS[v];
    return 0.1f * fm_exp2f((float)(v - LS_SYNC_N) * (8.22881869f / 119.0f));   /* 0.1 .. 30 Hz */
}
/* the LFO's fade envelope knob (synth.h): seconds to fade in, or to fade out */
static void fade_secs(int v, float *in, float *out)
{
    *in = *out = 0.0f;
    if (v < 64)
        *in = (float)(64 - v) * (3.0f / 64.0f);
    else if (v > 64)
        *out = fm_maxf((float)(127 - v) * (3.0f / 63.0f), 0.05f);
}

static inline void env_step(env_t *e, float ar, float dc, float sl, float rc)
{
    switch (e->stage) {
    case ENV_ATTACK:
        e->level += ar;
        if (e->level >= 1.0f) {
            e->level = 1.0f;
            e->stage = ENV_DECAY;
        }
        break;
    case ENV_DECAY:
        e->level = sl + (e->level - sl) * dc;
        if (e->level <= sl + 0.0001f) {
            e->level = sl;
            e->stage = ENV_SUSTAIN;
        }
        break;
    case ENV_SUSTAIN: e->level = sl; break;
    case ENV_RELEASE:
        e->level *= rc;
        if (e->level < 0.0001f) {
            e->level = 0.0f;
            e->stage = ENV_OFF;
        }
        break;
    default: e->level = 0.0f; break;
    }
}

/* ----------------------------------------------------------- parameters --- */
static inline int P(const synth_t *s, int trk, int p)      /* the knob, or its lock */
{
    uint8_t l = s->t[trk].lock[p];
    return l != LOCK_NONE ? l : s->par[trk][p];
}
/* the effective value: the LFO adds to its destination (clamped to the byte; the knob's own range is
 * applied by the engine that reads it) */
static inline float pf(const synth_t *s, int trk, int p, int dest, float off)
{
    float x = (float)P(s, trk, p);
    if (p == dest)
        x = fm_clampf(x + off, 0.0f, 127.0f);
    return x;
}

/* ----------------------------------------------------------- the engines --- */
/* each: eng_prep() turns c.e[] / c.ek[] and c.freq into c.ex[] once a chunk (or after a steal);
 * eng_run() writes m <= SUB raw samples to xl (and xr when c.stereo); the amplitude of a cycle is
 * about 1; eng_chunk() moves the engine's own slow parts every chunk */
static void br_init(voice_t *v);
static void cn_init(voice_t *v);
static void vo_init(voice_t *v);
static void gh_init(voice_t *v);
static void eng_init(voice_t *v)
{
    int i;
    for (i = 0; i < 16; i++)
        v->o[i] = 0.0f;
    smp_cache_reset(&smp_vc[v->vi % NVOICE]);
    if (v->engine == EG_ORGAN)
        for (i = 0; i < 6; i++)
            v->o[i * 2] = 1.0f;                  /* the rotating phasors start at 1 + 0i */
    if (v->engine == EG_WEFT)
        v->o[1] = 0.25f;                         /* the spread copy starts a quarter cycle on */
    if (v->engine == EG_BROCADE)
        br_init(v);
    if (v->engine == EG_ORBIT)
        v->o[5] = 1.0f;                          /* orbit: its fall starts full */
    v->o[15] = 1.0f;                             /* tines: its brightness envelopes start full */
    v->o[14] = 1.0f;
    if (v->engine == EG_ORGAN)
        v->o[13] = 1.0f, v->o[14] = 0.0f;        /* the rotor's gain and its ramp */
    if (v->engine == EG_CANE)
        cn_init(v);
    else if (v->engine == EG_VOILE)
        vo_init(v);
    else if (v->engine == EG_GINGHAM)
        gh_init(v);
}

/* a rising saw crossfading into a pulse that is low first and high after pw: the two in step, so the
 * morph keeps its level (a pulse high first would cancel the saw's fundamental halfway). The pulse's
 * second edge sits at p + 1 - pw in (0.5, 2): its wrap is a compare (the same bits as wrap1's floor) */
SY_INLINE float saw_pulse(float p, float dt, float idt, float pw, float mix)
{
    float b0 = blep(p, dt, idt), saw = 2.0f * p - 1.0f - b0, pl, q;   /* the wrap's residual serves both */
    if (mix <= 0.0f)
        return saw;
    q = p + 1.0f - pw;
    q = q >= 1.0f ? q - 1.0f : q;
    pl = (p < pw ? -1.0f : 1.0f) - b0 + blep(q, dt, idt);
    if (mix >= 1.0f)
        return pl;
    return saw + (pl - saw) * mix;
}
/* weft: phase distortion, the Casio CZ's way. One phasor read through a bent phase into a cosine: bend
 * moves the bend point (0 = a plain sine, toward 127 a bright saw); wave morphs the bent phase from the
 * saw's one bend into the pulse's (two steep climbs, two holds), then crossfades on into the resonant
 * wave: a sine at sweep times the note (1..16) under a window that falls across each cycle (the CZ's
 * resonance, a formant that moves with sweep); spread opens a detuned copy across the field. The steep
 * parts keep at least 2.5 samples (softer corners on high notes) and the resonance stays under 0.4 x the
 * sample rate. The resonant wave carries some DC: a one-pole blocker while it sounds.
 *
 * The bent phases run straight between four corners (the bend point d, 0.5, 0.5 + d and the wrap), so between
 * corners every cosine is a steady sine: a sample at a corner (and a sub-block's first) reads the waves exactly,
 * the samples after it rotate a phasor by the stretch's step (four multiplies, no polynomial). The steps'
 * cosines and sines are made here, three a copy: plain, the bent phase's in stretches A (0..d), B and D, C;
 * resonant, the pulse's in A and C (B and D hold it still) and the resonance's in A and in B, C, D. Rotating
 * for at most a sub-block (16 samples) keeps the difference from reading every sample exactly under -100 dB. */
/* n phasor steps (turns): cos, sin each (apart: one polynomial in the code for the preps) */
__attribute__((noinline)) static void cos_sin_steps(float *out, const float *a, int n)
{
    int i;
    for (i = 0; i < 2 * n; i++)
        out[i] = fm_sin_turns(a[i >> 1] + (i & 1 ? 0.0f : 0.25f));
}
static void weft_prep(voice_t *v)
{
    cfg_t *c = &v->c;
    float dt = fm_minf(c->freq * INV_SR, 0.45f), w = c->e[1], sp = c->e[3], d, kd, kd2, mo, r, ga, gb, norm, a[6];
    int k;
    d = 0.5f * fm_exp2f(-4.64f * c->e[0]);                         /* the bend point 0.5 .. 0.02 */
    d = fm_maxf(d, fm_minf(2.5f * dt, 0.5f));
    kd = 0.5f / d;
    kd2 = 0.5f / (1.0f - d);
    mo = w < 0.5f ? w * 2.0f : w * 2.0f - 1.0f;                   /* the morph inside its half */
    r = fm_minf(1.0f + 15.0f * c->e[2], fm_maxf(0.4f / dt, 1.0f));
    ga = fm_cosf((1.0f - sp) * 0.78539816f);                      /* the field: both in the middle .. one a side */
    gb = fm_sinf((1.0f - sp) * 0.78539816f);
    norm = 1.0f / (ga + gb);
    c->stereo = sp > 0.001f;
    c->ex[0] = dt;
    c->ex[1] = fm_minf(c->freq * fm_exp2f(sp * 0.01f) * INV_SR, 0.45f);   /* the copy: up to 12 cents up */
    c->ex[2] = d;
    c->ex[3] = mo;
    c->ex[4] = r;
    c->ex[5] = ga * norm;
    c->ex[6] = gb * norm;
    c->ex[7] = w < 0.5f ? 0.0f : 1.0f;                            /* 0: saw .. pulse, 1: pulse .. resonant */
    for (k = 0; k <= (c->stereo != 0); k++) {                     /* the steps' phasors, ex[8..13] and [14..19] */
        float dk = c->ex[k];
        a[3 * k] = kd * dk;
        if (w < 0.5f) {
            a[3 * k + 1] = (1.0f - mo) * kd2 * dk;
            a[3 * k + 2] = ((1.0f - mo) * kd2 + mo * kd) * dk;
        } else {
            a[3 * k + 1] = r * kd * dk;
            a[3 * k + 2] = r * kd2 * dk;
        }
    }
    cos_sin_steps(c->ex + 8, a, 3 * k);
    v->o[14] = 1.0f;                             /* (the phasors: read again) */
}
/* a copy's stretch (from its phase p): the next corner and the stretch's rotations; and its waves read at p */
typedef struct { float y, z, u, uc, c, s, c2, s2, nb; int turn; } weft_seg_t;
SY_INLINE void weft_stretch(weft_seg_t *g, float p, float d, const float *rot, const int reso)
{
    int st = p < d ? 0 : p < 0.5f ? 1 : p < 0.5f + d ? 2 : 3, j;
    g->nb = st == 0 ? d : st == 1 ? 0.5f : st == 2 ? 0.5f + d : 1.0f;   /* the next corner */
    if (!reso) {
        j = st == 0 ? 0 : st == 2 ? 4 : 2;
    } else {
        g->turn = !(st & 1);                     /* (the pulse holds in B, D) */
        g->c2 = rot[st == 0 ? 2 : 4];
        g->s2 = rot[st == 0 ? 3 : 5];
        j = 0;
    }
    g->c = rot[j];
    g->s = rot[j + 1];
}
SY_INLINE void weft_read(weft_seg_t *g, float p, float d, float kd, float kd2, float mo, float r, const int reso)
{
    float qs = p < d ? p * kd : 0.5f + (p - d) * kd2;                               /* the saw's bent phase */
    float qp = p < 0.5f ? fm_minf(p * kd, 0.5f) : 0.5f + fm_minf((p - 0.5f) * kd, 0.5f);   /* the pulse's */
    if (!reso) {
        float q = qs + (qp - qs) * mo;
        g->y = fm_sin_turns(q + 0.25f);                                      /* cos 2 pi q: the sample */
        g->z = fm_sin_turns(q);
    } else {
        float rq = r * qs;
        g->y = fm_sin_turns(qp + 0.25f);                                     /* the pulse: cos, sin */
        g->z = fm_sin_turns(qp);
        g->u = fm_sin_turns(rq);                                             /* the resonance: sin, cos */
        g->uc = fm_sin_turns(rq + 0.25f);
    }
}
/* one copy's m samples into x (the resonant half through its DC blocker), from phase *pp; ph keeps the phasors
 * from run to run (read again when seed: a sub-block's start, or a new configuration) */
SY_INLINE void weft_osc(const cfg_t *c, float *pp, float dp, const float *rot, float *x, int m, float *bx, float *by,
                        float *ph, int seed, const int reso)
{
    float p = *pp, d = c->ex[2], kd = 0.5f / d, kd2 = 0.5f / (1.0f - d), mo = c->ex[3], r = c->ex[4];
    float xa = *bx, ya = *by, a = 0.0f;
    weft_seg_t g = {0};
    int i;
    if (seed) {
        g.nb = -1.0f;                            /* (the first sample is read) */
    } else {                                     /* (the stretch the last sample was in) */
        weft_stretch(&g, p, d, rot, reso);
        g.y = ph[0];
        g.z = ph[1];
        g.u = ph[2];
        g.uc = ph[3];
    }
    for (i = 0; i < m; i++) {
        p += dp;                                 /* (step1: past a corner, maybe the wrap) */
        if (p >= g.nb) {
            if (p >= 1.0f)
                p -= 1.0f;
            weft_stretch(&g, p, d, rot, reso);
            weft_read(&g, p, d, kd, kd2, mo, r, reso);
        } else {
            float t;
            if (!reso || g.turn) {
                t = g.y * g.c - g.z * g.s;
                g.z = g.z * g.c + g.y * g.s;
                g.y = t;
            }
            if (reso) {
                t = g.u * g.c2 + g.uc * g.s2;
                g.uc = g.uc * g.c2 - g.u * g.s2;
                g.u = t;
            }
        }
        if (!reso) {
            a = g.y;
        } else {
            float rs = (1.0f - p) * g.u * 1.2f, y;
            a = g.y + (rs - g.y) * mo;
            y = a - xa + 0.999f * ya;
            xa = a;
            ya = y;
        }
        x[i] = reso ? ya : a;
    }
    if (!reso)                                   /* the blocker passes the wave unchanged from here, so */
        xa = ya = a;                             /* turning wave into the resonant half is seamless */
    *pp = p;
    *bx = xa;
    *by = fm_flush(ya);
    ph[0] = g.y;
    ph[1] = g.z;
    ph[2] = g.u;
    ph[3] = g.uc;
}
static void weft_osc_pd(const cfg_t *c, float *o, int k, float *x, int m, int seed)
{
    weft_osc(c, &o[k], c->ex[k], c->ex + 8 + 6 * k, x, m, &o[2 + 2 * k], &o[3 + 2 * k], &o[6 + 4 * k], seed, 0);
}
static void weft_osc_res(const cfg_t *c, float *o, int k, float *x, int m, int seed)
{
    weft_osc(c, &o[k], c->ex[k], c->ex + 8 + 6 * k, x, m, &o[2 + 2 * k], &o[3 + 2 * k], &o[6 + 4 * k], seed, 1);
}
/* the copies' state: phases o[0..1], blockers o[2..5], phasors o[6..13]; o[14] 1: read them again (eng_init
 * and weft_prep set it). Every sub-block's start reads the waves again, so a run split anywhere (two renders,
 * the cores' halves) makes the same samples */
static void weft_run(voice_t *v, float *xl, float *xr, int m)
{
    const cfg_t *c = &v->c;
    const int reso = c->ex[7] > 0.5f, seed = (v->pos & (SUB - 1)) == 0 || v->o[14] != 0.0f;
    int i;
    if (reso)
        weft_osc_res(c, v->o, 0, xl, m, seed);
    else
        weft_osc_pd(c, v->o, 0, xl, m, seed);
    if (c->stereo) {
        float ga = c->ex[5], gb = c->ex[6];
        if (reso)
            weft_osc_res(c, v->o, 1, xr, m, seed);
        else
            weft_osc_pd(c, v->o, 1, xr, m, seed);
        for (i = 0; i < m; i++) {
            float a = xl[i], b = xr[i];
            xl[i] = a * ga + b * gb;
            xr[i] = a * gb + b * ga;
        }
    }
    v->o[14] = 0.0f;
}

/* stack: two oscillators, the second at ratio N:1 (1..8, the knob's own range) and detuned, one each
 * side of the field as spread opens; wave = saw (0) through a square at the middle to narrowing pulses */
static void stack_prep(voice_t *v)
{
    cfg_t *c = &v->c;
    float w = c->e[3], ratio = fm_clampf(fm_floorf(c->ek[1] + 0.5f), 1.0f, 8.0f), cents = c->e[2] * 50.0f, ga, gb, norm;
    c->ex[0] = fm_minf(c->freq * INV_SR, 0.45f);
    c->ex[1] = fm_minf(c->freq * ratio * fm_exp2f(cents * (1.0f / 1200.0f)) * INV_SR, 0.45f);
    if (c->e[0] < 0.5f) {                        /* saw .. square */
        c->ex[2] = c->e[0] * 2.0f;
        c->ex[3] = 0.5f;
    } else {                                     /* square .. a 10 % pulse */
        c->ex[2] = 1.0f;
        c->ex[3] = 0.5f - (c->e[0] - 0.5f) * 0.8f;
    }
    ga = fm_cosf((1.0f - w) * 0.78539816f);
    gb = fm_sinf((1.0f - w) * 0.78539816f);
    norm = 1.0f / (ga + gb);
    c->ex[5] = ga * norm;                        /* the field's gains, normalised */
    c->ex[6] = gb * norm;
    c->ex[7] = 1.0f / c->ex[0];                  /* the PolyBLEPs' 1 / increment */
    c->ex[8] = 1.0f / c->ex[1];
    c->stereo = w > 0.001f;
}
/* one oscillator's m samples into x. Away from its two edges (the saw's wrap, the pulse's step at pw) both
 * PolyBLEPs are exactly 0 and the pulse is -1 or 1, so there saw_pulse is the plain saw + (pulse - saw) mix:
 * the run takes that whenever the phase is inside one of the two stretches clear of both (the pulse's ends
 * kept 1e-6 further off than its edge's reach, the rounding of p + 1 - pw), and saw_pulse itself elsewhere.
 * The same bits either way. */
static void stack_osc(float *pp, float d, float id, float pw, float mix, float *x, int m)
{
    float p = *pp, a1 = d, b1 = pw - d - 1e-6f, a2 = pw + d + 1e-6f, b2 = 1.0f - d;   /* the clear stretches */
    float lo = 2.0f, hi = 0.0f, s = 0.0f;        /* the stretch the phase is in or heading for (none yet) */
    int i;
    for (i = 0; i < m; i++) {
        p = step1(p, d);
        if (p >= lo && p <= hi) {
            float saw = 2.0f * p - 1.0f;
            x[i] = mix <= 0.0f ? saw : mix >= 1.0f ? s : saw + (s - saw) * mix;
        } else {
            x[i] = saw_pulse(p, d, id, pw, mix);
            if (p <= b1 || p > b2)               /* (before the pulse's edge, or past the wrap's reach) */
                lo = a1, hi = b1, s = -1.0f;
            else
                lo = a2, hi = b2, s = 1.0f;
        }
    }
    *pp = p;
}
SY_INLINE void stack_loop(voice_t *v, float *xl, float *xr, int m, const int stereo)
{
    const cfg_t *c = &v->c;
    float mix = c->ex[2], pw = c->ex[3], ga = c->ex[5], gb = c->ex[6];
    int i;
    if (mix <= 0.0f) {                           /* the plain saws: the two in one pass */
        float p1 = v->o[0], p2 = v->o[1], d1 = c->ex[0], d2 = c->ex[1], i1 = c->ex[7], i2 = c->ex[8];
        for (i = 0; i < m; i++) {
            p1 = step1(p1, d1);
            p2 = step1(p2, d2);
            xl[i] = 2.0f * p1 - 1.0f - blep(p1, d1, i1);   /* (saw_pulse's saw) */
            xr[i] = 2.0f * p2 - 1.0f - blep(p2, d2, i2);
        }
        v->o[0] = p1;
        v->o[1] = p2;
    } else {
        stack_osc(&v->o[0], c->ex[0], c->ex[7], pw, mix, xl, m);
        stack_osc(&v->o[1], c->ex[1], c->ex[8], pw, mix, xr, m);
    }
    for (i = 0; i < m; i++) {
        float a = xl[i], b = xr[i];
        if (stereo) {
            xl[i] = a * ga + b * gb;
            xr[i] = a * gb + b * ga;
        } else {
            xl[i] = (a + b) * 0.5f;
        }
    }
}
static void stack_run(voice_t *v, float *xl, float *xr, int m)
{
    if (v->c.stereo)
        stack_loop(v, xl, xr, m, 1);
    else
        stack_loop(v, xl, xr, m, 0);
}

/* wavetable: a frame crossfade (scan) through one of nine tables, bend bends the phase (symmetry), ripple
 * phase-modulates it by a second oscillator that climbs from a slow wobble to the note's own rate */
static void wt_prep(voice_t *v)
{
    cfg_t *c = &v->c;
    float pos = c->e[1] * (float)(WT_NFRAME - 1), fr = fm_floorf(pos), dr = c->e[3];
    c->ex[0] = fm_minf(c->freq * INV_SR, 0.45f);
    c->ex[1] = fm_clampf(fm_floorf(c->ek[0] + 0.5f), 0.0f, (float)(WT_NTABLE - 1));
    c->ex[2] = fm_minf(fr, (float)(WT_NFRAME - 2));
    c->ex[3] = pos - c->ex[2];
    c->ex[4] = 0.5f + c->e[2] * 0.45f;           /* the warp point: 0.5 = straight */
    c->ex[5] = dr > 0.001f ? c->freq * fm_exp2f(-7.0f + 8.0f * dr) * INV_SR : 0.0f;   /* 1/128 .. 2x the note */
    c->ex[6] = dr * dr * 0.6f;                   /* depth, turns */
    c->stereo = 0;
}
static void wt_run(voice_t *v, float *xl, float *xr, int m)
{
    const cfg_t *c = &v->c;
    const int16_t *ta = WT[(int)c->ex[1]][(int)c->ex[2]], *tb = ta + WT_N;
    float p = v->o[0], mp = v->o[1], d = c->ex[0], md = c->ex[5], fb = c->ex[3], w = c->ex[4];
    float ka = 0.5f / w, kb = 0.5f / (1.0f - w), depth = c->ex[6];
    int i;
    (void)xr;
    for (i = 0; i < m; i++) {
        float q, s, fa, a, b;
        int k;
        p = step1(p, d);
        q = p < w ? p * ka : 0.5f + (p - w) * kb;
        if (md > 0.0f) {
            mp = step1(mp, md);
            q = wrap1(q + depth * fm_sin_turns(mp));
        }
        s = q * (float)WT_N;
        k = (int)s;
        fa = s - (float)k;
        k &= WT_N - 1;
        a = (float)ta[k] + ((float)ta[(k + 1) & (WT_N - 1)] - (float)ta[k]) * fa;
        b = (float)tb[k] + ((float)tb[(k + 1) & (WT_N - 1)] - (float)tb[k]) * fa;
        xl[i] = (a + (b - a) * fb) * (1.0f / 32768.0f);
    }
    v->o[0] = p;
    v->o[1] = mp;
}

/* orbit: two-operator FM for strings: bright = index, ratio from a detuned 1:1 (0..50: slow beating) to
 * harmonic divisions (51..100), edge = modulator feedback (sine toward saw), wobble = the speed of a pitch wobble that fades in over the first second of a note (0 = none).
 * Its second page (TP_X1..X3, ORBIT_X_P): fall, the index falling from the note's start (0 = held, as before; else a
 * time constant 20 ms .. 4 s), velocity then scaling the start; floor, what of the index remains (0..100 %);
 * keyscale, the index lower for higher notes (0 .. -6 dB an octave above C3). At 0, 0, 0 the engine is as it was */
static const float AXIS_RATIO[12] = {0.5f, 0.6666667f, 0.75f, 1.25f, 1.3333334f, 1.5f, 1.75f, 2.0f, 3.0f, 4.0f, 6.0f, 8.0f};
static void orbit_prep(voice_t *v)
{
    cfg_t *c = &v->c;
    float r = c->e[1] * 100.0f, ratio;
    if (r <= 50.0f)
        ratio = fm_exp2f(r * 0.6f * (1.0f / 1200.0f));          /* 0 .. 30 cents */
    else {
        int k = (int)((r - 51.0f) * (11.99f / 49.0f));
        ratio = AXIS_RATIO[k < 0 ? 0 : k > 11 ? 11 : k];
    }
    c->ex[6] = fm_minf(c->freq * INV_SR, 0.45f);                 /* the base increments; orbit_chunk wobbles them */
    c->ex[7] = fm_minf(c->freq * ratio * INV_SR, 0.45f);
    c->ex[0] = c->ex[6];
    c->ex[1] = c->ex[7];
    c->ex[2] = c->e[0] * c->e[0] * 5.0f;                        /* index, turns */
    if (c->ex[10] > 0.0f && c->freq > 130.81f)                  /* keyscale: the index an octave above C3 x 2^-k */
        c->ex[2] *= fm_exp2f(-c->ex[10] * fm_log2f(c->freq * (1.0f / 130.81f)));
    if (c->ex[8] > 0.0f)                                         /* with a fall, velocity sets the start */
        c->ex[2] *= 0.4f + 0.6f * v->vel;
    c->ex[11] = c->ex[2];                                        /* (the index before the fall: orbit_chunk) */
    c->ex[3] = c->e[2] * 1.3f;                                  /* feedback */
    c->ex[4] = c->e[3] > 0.004f ? (0.3f + c->e[3] * 7.7f) * INV_SR : 0.0f;   /* the wobble's rate, 0.3 .. 8 Hz */
    c->stereo = 0;
}
static void orbit_run(voice_t *v, float *xl, float *xr, int m)
{
    const cfg_t *c = &v->c;
    float cp = v->o[0], mp = v->o[1], fbv = v->o[2];
    float dc = c->ex[0], dm = c->ex[1], idx = c->ex[2] * 0.25f, fb = c->ex[3] * 0.25f;
    int i;
    (void)xr;
    for (i = 0; i < m; i++) {
        float mo;
        mp = step1(mp, dm);
        cp = step1(cp, dc);
        mo = fm_sin_turns(mp + fbv * fb);
        fbv = mo;
        xl[i] = fm_sin_turns(cp + mo * idx);
    }
    v->o[0] = cp;
    v->o[1] = mp;
    v->o[2] = fbv;
}
static void orbit_chunk(voice_t *v)               /* the fall; the wobble: +-20 cents at the knob's speed, fading in */
{
    cfg_t *c = &v->c;
    float w;
    if (c->ex[8] > 0.0f) {                       /* the fall: o[5] 1 at the note's start, toward 0 */
        c->ex[2] = c->ex[11] * (c->ex[9] + (1.0f - c->ex[9]) * v->o[5]);
        v->o[5] *= c->ex[8];
    }
    if (c->ex[4] <= 0.0f)
        return;
    v->o[3] = wrap1(v->o[3] + c->ex[4] * (float)SY_CHUNK);
    v->o[4] += (1.0f - v->o[4]) * 0.0015f;       /* the fade: most of the way in a second */
    w = 1.0f + 0.012f * v->o[4] * fm_sin_turns(v->o[3]);
    c->ex[0] = fm_minf(c->ex[6] * w, 0.45f);
    c->ex[1] = fm_minf(c->ex[7] * w, 0.45f);
}

/* tines: a tine (carrier with a 1:1 modulator whose index decays, as a struck reed's brightness),
 * grit = a 2:1 modulator, bite = a short index burst at the start, bell = a high partial
 * (14x) that dies quickly. The brightness envelopes live in o[13..15] and step once a chunk.
 * The carrier and the 1:1 modulator share their phase (both start at 0 and step alike). The modulator and the
 * tine partial are plain sines: a sub-block reads them once, from the phases, and rotates phasors (the steps'
 * cosines and sines made here), the 2:1 one is 2 sin cos of the modulator's; only the carrier, read through
 * the modulation, takes a polynomial every sample. (Under -100 dB from reading every sine every sample.) */
static void tines_prep(voice_t *v)
{
    cfg_t *c = &v->c;
    c->ex[0] = fm_minf(c->freq * INV_SR, 0.45f);
    c->ex[1] = fm_minf(c->freq * 14.0f * INV_SR, 0.45f);
    c->ex[2] = (0.3f + c->e[0] * c->e[0] * 2.4f) * (0.4f + 0.6f * v->vel);   /* tone: index */
    c->ex[3] = c->e[1] * 1.2f;                                              /* texture: the 2:1 index */
    c->ex[4] = c->e[2] * 3.0f;                                              /* punch: the burst's index */
    c->ex[5] = c->e[3] * 0.5f;                                              /* tine level */
    cos_sin_steps(c->ex + 6, c->ex, 2);                                     /* the steps: cos, sin */
    v->o[7] = 0.0f;                                                         /* (the phasors: read again) */
    c->stereo = 0;
}
static void tines_chunk(voice_t *v)                 /* the brightness envelopes, once a chunk */
{
    v->o[15] = 0.25f + (v->o[15] - 0.25f) * 0.9982f;   /* the index settles over ~0.8 s */
    v->o[14] *= 0.93f;                                 /* the punch burst: ~30 ms */
    v->o[13] = v->o[13] * 0.994f;                      /* the tine partial: ~0.25 s */
}
SY_INLINE void tines_loop(voice_t *v, float *xl, int m, const int has_tine, const int has_tex)
{
    const cfg_t *c = &v->c;
    float mp = v->o[1], tp = v->o[2], mc = v->o[3], ms = v->o[4], tc = v->o[5], ts = v->o[6];
    float d = c->ex[0], dt = c->ex[1], idx = c->ex[2] * v->o[15] + c->ex[4] * v->o[14], tex = c->ex[3] * v->o[15];
    float tine = c->ex[5] * v->o[13], kc = c->ex[6], ks = c->ex[7], tkc = c->ex[8], tks = c->ex[9];
    int i;
    for (i = 0; i < m; i++) {
        float mo, x;
        mp = step1(mp, d);                       /* the phases and their phasors step */
        x = ms * kc + mc * ks;
        mc = mc * kc - ms * ks;
        ms = x;
        if (has_tine) {
            tp = step1(tp, dt);
            x = ts * tkc + tc * tks;
            tc = tc * tkc - ts * tks;
            ts = x;
        }
        mo = ms * idx;
        if (has_tex)                             /* (texture 0, felt piano's and soft felt's: a sine less) */
            mo += ms * (mc + mc) * tex;
        x = fm_sin_turns(mp + mo * 0.25f) * 0.9f;
        if (has_tine)
            x += ts * tine;
        xl[i] = x;
    }
    v->o[0] = mp;
    v->o[1] = mp;
    v->o[2] = tp;
    v->o[3] = mc;
    v->o[4] = ms;
    v->o[5] = tc;
    v->o[6] = ts;
}
/* the state: the phases o[0..2], their phasors o[3..6] (cos, sin), o[7] 1 while they hold (eng_init and
 * tines_prep clear it); every sub-block's start reads the phasors again from the phases, so a run split anywhere
 * makes the same samples */
static void tines_run(voice_t *v, float *xl, float *xr, int m)
{
    const int tex = v->c.ex[3] * v->o[15] != 0.0f, tine = v->c.ex[5] * v->o[13] > 0.0005f;
    (void)xr;
    if ((v->pos & (SUB - 1)) == 0 || v->o[7] == 0.0f)
    {
        v->o[3] = fm_sin_turns(v->o[1] + 0.25f);
        v->o[4] = fm_sin_turns(v->o[1]);
        if (tine) {
            v->o[5] = fm_sin_turns(v->o[2] + 0.25f);
            v->o[6] = fm_sin_turns(v->o[2]);
        }
    }
    if (tine) {                                  /* the tine partial dies in a quarter second */
        if (tex)
            tines_loop(v, xl, m, 1, 1);
        else
            tines_loop(v, xl, m, 1, 0);
    } else if (tex) {
        tines_loop(v, xl, m, 0, 1);
    } else {
        tines_loop(v, xl, m, 0, 0);
    }
    v->o[7] = 1.0f;
}

/* organ: six drawbars (16' 8' 4' 2 2/3' 2' 1 1/3') as rotating phasors (a complex multiply a sine:
 * no polynomial). stops picks one of eight registrations, transistor to church; pedal 1..8 is the 16'
 * bar; the rotor's amount and rate are 1..8 (1 = none / slowest) */
static const float ORGAN_SET[8][5] = {{1.0f, 0.15f, 0.0f, 0.6f, 0.0f},    /* 1 transistor: reedy, hollow */
                                      {1.0f, 0.6f, 0.0f, 0.4f, 0.2f},     /* 2 combo: bright, thin */
                                      {1.0f, 0.7f, 0.5f, 0.1f, 0.0f},     /* 3 jazz */
                                      {1.0f, 0.9f, 0.7f, 0.7f, 0.5f},     /* 4 gospel: all bars out */
                                      {1.0f, 0.8f, 0.6f, 0.3f, 0.3f},     /* 5 rock */
                                      {1.0f, 0.5f, 0.35f, 0.5f, 0.6f},    /* 6 theatre */
                                      {1.0f, 0.4f, 0.1f, 0.2f, 0.05f},    /* 7 pipe: flutes */
                                      {1.0f, 0.8f, 0.9f, 0.9f, 0.9f}};    /* 8 church: the full chorus */
static const float ORGAN_RATIO[6] = {0.5f, 1.0f, 2.0f, 3.0f, 4.0f, 6.0f};
static inline int knob8(float raw) { int k = (int)(raw + 0.5f); return k < 1 ? 1 : k > 8 ? 8 : k; }
static void organ_prep(voice_t *v)
{
    cfg_t *c = &v->c;
    int k = knob8(c->ek[0]) - 1, j;
    float bass = (float)(knob8(c->ek[1]) - 1) * (1.0f / 7.0f), amt = (float)(knob8(c->ek[2]) - 1) * (1.0f / 7.0f);
    float tr = 0.5f * fm_exp2f((float)(knob8(c->ek[3]) - 1) * (4.0f / 7.0f)) * INV_SR, sum, base;   /* 0.5 .. 8 Hz */
    sum = bass;
    c->ex[0] = bass * 0.8f;
    for (j = 1; j < 6; j++) {
        c->ex[j] = ORGAN_SET[k][j - 1];
        sum += c->ex[j];
    }
    sum = 1.4f / (sum + 0.5f);                  /* the bars share a level near the other engines' */
    for (j = 0; j < 6; j++)
        c->ex[j] *= sum;
    c->ex[18] = tr;
    c->ex[19] = amt;
    base = c->freq * INV_SR * (1.0f + amt * 0.004f * fm_sin_turns(v->o[12]));   /* the tremolo's pitch wobble */
    for (j = 0; j < 6; j++) {
        float a = fm_minf(base * ORGAN_RATIO[j], 0.45f) * FM_TWO_PI, re = v->o[j * 2], im = v->o[j * 2 + 1], r2;
        c->ex[6 + j] = fm_cosf(a);
        c->ex[12 + j] = fm_sinf(a);
        r2 = re * re + im * im;                  /* renormalise the phasor once a chunk */
        if (r2 > 0.5f) {
            float g = fm_rsqrtf(r2);
            v->o[j * 2] = re * g;
            v->o[j * 2 + 1] = im * g;
        } else {
            v->o[j * 2] = 1.0f;
            v->o[j * 2 + 1] = 0.0f;
        }
    }
    c->stereo = 0;
}
void organ_bars(const uint8_t *par, float bars[6])
{
    int k = knob8((float)par[TP_E1]) - 1, j;
    float mx = 0.0f;
    bars[0] = (float)(knob8((float)par[TP_E2]) - 1) * (1.0f / 7.0f) * 0.8f;
    for (j = 1; j < 6; j++)
        bars[j] = ORGAN_SET[k][j - 1];
    for (j = 0; j < 6; j++)
        mx = fm_maxf(mx, bars[j]);
    for (j = 0; j < 6; j++)
        bars[j] /= mx;
}
static void organ_run(voice_t *v, float *xl, float *xr, int m)
{
    const cfg_t *c = &v->c;
    float re[6], im[6];
    const float *cr = c->ex + 6, *ci = c->ex + 12;
    int i, j;
    (void)xr;
    for (j = 0; j < 6; j++) {
        re[j] = v->o[j * 2];
        im[j] = v->o[j * 2 + 1];
    }
    {   /* the tremolo: its gain ramps across the chunk (organ_chunk aims it) */
        float g0 = v->o[13], dg = v->o[14];
        for (i = 0; i < m; i++) {
            float s = 0.0f;
            for (j = 0; j < 6; j++) {
                float nr = re[j] * cr[j] - im[j] * ci[j];
                im[j] = re[j] * ci[j] + im[j] * cr[j];
                re[j] = nr;
                s += im[j] * c->ex[j];
            }
            g0 += dg;
            xl[i] = s * g0;
        }
        v->o[13] = g0;
    }
    for (j = 0; j < 6; j++) {
        v->o[j * 2] = re[j];
        v->o[j * 2 + 1] = im[j];
    }
}
static void organ_chunk(voice_t *v)              /* the tremolo's phase and the chunk's gain ramp */
{
    const cfg_t *c = &v->c;
    float g1;
    v->o[12] = wrap1(v->o[12] + c->ex[18] * (float)SY_CHUNK);
    g1 = 1.0f - c->ex[19] * 0.5f * (0.5f - 0.5f * fm_sin_turns(v->o[12]));
    v->o[14] = (g1 - v->o[13]) * (1.0f / (float)SY_CHUNK);
}

/* sync: a saw reset by a master at the note's pitch, reach = the follower's ratio (which harmonics
 * ring), sub = a sine an octave down, hiss = noise, thin = a high-pass before the filter; under it all the
 * master's own triangle (SYNC_ROOT), so the pitch heard is the note's. The
 * follower's output runs one sample late so both samples around a reset get their PolyBLEP share. */
#define SYNC_ROOT 0.5f                           /* a triangle at the master's pitch: the note stays the note when
                                                  * the follower lands near a harmonic (a plain saw up there alone) */
static void hs_prep(voice_t *v)
{
    cfg_t *c = &v->c;
    float hz = 20.0f * fm_exp2f(c->e[3] * 7.2f), g;                         /* 20 Hz .. 2.9 kHz */
    c->ex[0] = fm_minf(c->freq * INV_SR, 0.45f);
    c->ex[1] = fm_minf(c->freq * (1.0f + c->e[0] * 7.0f) * INV_SR, 0.45f);
    c->ex[2] = c->e[1] * 0.6f;
    c->ex[3] = c->e[2] * c->e[2] * 0.25f;
    g = fm_tanf(FM_PI * fm_minf(hz, 18000.0f) * INV_SR);
    c->ex[4] = g;
    c->ex[5] = 1.0f / (1.0f + g * (g + 1.41421356f));
    c->ex[6] = c->e[3] > 0.001f;
    c->stereo = 0;
}
static void hs_run(voice_t *v, float *xl, float *xr, int m)
{
    const cfg_t *c = &v->c;
    float mp = v->o[0], sp = v->o[1], prev = v->o[2], subp = v->o[3], h1 = v->o[4], h2 = v->o[5];
    float dm = c->ex[0], ds = c->ex[1], idm = 1.0f / dm, ids = 1.0f / ds, sub = c->ex[2], nz = c->ex[3];
    float g = c->ex[4], a1 = c->ex[5], a2 = g * a1, hk = 1.41421356f;
    int hp = c->ex[6] > 0.5f, has_sub = sub > 0.001f, has_nz = nz > 0.001f, i;
    (void)xr;
    for (i = 0; i < m; i++) {
        float y, x, t, h, cont;
        mp += dm;
        cont = sp + ds;                          /* the follower as it would run on */
        if (mp >= 1.0f) {                        /* the master wraps: reset the follower t samples ago */
            mp -= 1.0f;
            t = mp * idm;
            sp = t * ds;
            y = 2.0f * sp - 1.0f;
            h = y - (2.0f * cont - 1.0f);        /* the step, to the fresh ramp from the old one */
            prev += h * 0.5f * t * t;
            y -= h * 0.5f * (1.0f - t) * (1.0f - t);
        } else if (cont >= 1.0f) {               /* the follower's own wrap */
            sp = cont - 1.0f;
            t = sp * ids;
            y = 2.0f * sp - 1.0f;
            prev -= t * t;
            y += (1.0f - t) * (1.0f - t);
        } else {
            sp = cont;
            y = 2.0f * sp - 1.0f;
        }
        x = prev;
        prev = y;
        if (has_sub) {
            subp = step1(subp, dm * 0.5f);
            x += fm_sin_turns(subp) * sub;
        }
        if (has_nz)
            x += frand(&v->rng) * nz;
        if (hp) {                                /* 2-pole TPT high-pass */
            float v3 = x - h2, v1 = a1 * h1 + a2 * v3, v2 = h2 + g * v1;
            h1 = 2.0f * v1 - h1;
            h2 = 2.0f * v2 - h2;
            x = x - hk * v1 - v2;
        }
        xl[i] = x + (1.0f - 4.0f * fm_fabsf(mp - 0.5f)) * SYNC_ROOT;   /* the root under it (after thin) */
    }
    v->o[0] = mp;
    v->o[1] = sp;
    v->o[2] = prev;
    v->o[3] = subp;
    v->o[4] = fm_flush(h1);
    v->o[5] = fm_flush(h2);
}

/* swarm: two sines a detune (spread) apart, their pitch pushed around by low-passed noise (swarm), a 1:1
 * modulator for tone (FM) that also rings their level (ring: AM, half-wave features, grit) */
static void dis_prep(voice_t *v)
{
    cfg_t *c = &v->c;
    float sw = c->e[0], det = c->e[3] * 0.04f;
    c->ex[0] = fm_minf(c->freq * (1.0f - det) * INV_SR, 0.45f);
    c->ex[1] = fm_minf(c->freq * (1.0f + det) * INV_SR, 0.45f);
    c->ex[2] = 0.002f + sw * sw * 0.3f;          /* the noise's lowpass: slow drifts .. a hiss */
    c->ex[3] = sw * 0.8f;                        /* pitch swing, as a fraction of the increment (< 1: the step wraps once) */
    c->ex[4] = c->e[1];                          /* am depth */
    c->ex[5] = c->e[2] * c->e[2] * 3.0f;         /* fm index */
    c->ex[6] = fm_minf(c->freq * INV_SR, 0.45f);
    c->stereo = 0;
}
static void dis_run(voice_t *v, float *xl, float *xr, int m)
{
    const cfg_t *c = &v->c;
    float p1 = v->o[0], p2 = v->o[1], mp = v->o[2], n1 = v->o[3];
    float d1 = c->ex[0], d2 = c->ex[1], k = c->ex[2], sw = c->ex[3], idx = c->ex[5] * 0.25f, dm = c->ex[6];
    float am = c->ex[4] * 0.5f, g0 = 1.0f - am;
    int i;
    (void)xr;
    for (i = 0; i < m; i++) {
        float w, ms, mo;
        n1 += (frand(&v->rng) - n1) * k;
        w = 1.0f + n1 * sw;                      /* -1..1 noise: up to +-80 % of the pitch */
        mp = step1(mp, dm);
        ms = fm_sin_turns(mp);
        mo = ms * idx;
        p1 = step1(p1, d1 * w);
        p2 = step1(p2, d2 * w);
        xl[i] = (fm_sin_turns(p1 + mo) + fm_sin_turns(p2 + mo)) * 0.5f * (g0 + am * ms);
    }
    v->o[0] = p1;
    v->o[1] = p2;
    v->o[2] = mp;
    v->o[3] = n1;
}

/* brocade: the supersaw, after NuSaw (the NuSaw plugin, MIT, and its FM-1 port fm1-nusaw): up to seven
 * PolyBLEP saws a voice, free-running from random phases (nine cost the FM-1 a third more: a voice there took 13 %
 * of a block; an older project's 8 or 9 plays 7). saws (1..7): a centre saw and pairs either side,
 * detuned by NuSaw's offsets (the JP-8000's own table at six or seven, k(k+1) / M(M+1) of the widest pair
 * otherwise; an even count leaves its outer pair's lower saw out); detune: NuSaw's curve, the widest pair up
 * to +-11 % of the note (about two semitones); mix: the side saws against the centre one (NuSaw's mix^1.5 x
 * 0.667, the sum kept at one saw's power); spread: the pairs across the field, the outer ones furthest. Then
 * NuSaw's keytracked high-pass (12 dB at a third of the note, the JP-8000's), which keeps the beating out of
 * the bass. The filter, the envelopes and the LFO are the voice's own, not NuSaw's.
 * o[0..6] the phases, o[7] and o[8] the spread and pairs the pans were made from (o[8] -1: none yet), o[9] the
 * saws sounding (fractional while one fades; -1: the note's first sub-block), o[10..13] the high-pass (low and
 * band, L then R). ex[0] the increment, ex[1] the detune's, ex[2] the knob's count, ex[3] the offsets' row,
 * ex[4] the side gain, ex[5..10] the pairs' L and R gains (x sqrt 2: the centre at 1), ex[11] the high-pass's coefficient, ex[12], ex[13], ex[15..19] the seven saws' 1 / increment (BR_IDT:
 * the PolyBLEP's, a division a saw once a prepare, not once a sub-block), ex[14] the count to sound (the knob's).
 * The sub (the Sub page, TP_X1 / TP_X2): NuSaw's, a sine at the note plus the offset (-12 .. -24 semitones),
 * added into both sides after the saws' high-pass (NuSaw's order: the high-pass keeps out of the sub), before
 * the voice's filter, at NuSaw's level (its sub_level x 0.7071 against a centre saw panned at 0.7071; here the
 * pans are x sqrt 2, so the level as it is: the sub against the saws as NuSaw's). Not computed at level 0.
 * ex[20] its gain, ex[21] its offset in semitones (both voice_chunk's, every chunk), ex[22] its increment
 * (br_prep's, from the frequency the saws take: glide, bend, the LFO); o[14] its phase (eng_init's 1: from 0).
 *
 * The saws' sample loop: a sample whose phase is at most 1 - increment needs no wrap and no PolyBLEP (the wrap
 * leaves the phase under the increment, and no phase without a wrap is), so one compare takes it, and the
 * wrap and the two corrections share the rare branch. The centre saw's pass writes the buffers (no clearing
 * pass), the pairs add theirs. The same samples as before. */
#define BR_MAX 9                                 /* (the offsets' table still holds nine; br_init draws nine phases) */
#define BR_SAWS 7                                /* the knob's top */
#define BR_PAIRS (BR_SAWS / 2)
#define BR_IDT(s) (12 + (s) + ((s) >= 2))        /* saw s's 1 / increment: around voice_chunk's ex[14] */
#define BR_FADE (1.0f / (0.012f * SY_SR))        /* a saw in or out over 12 ms */
static const float BR_COEF[5][BR_MAX] = {        /* the saws' detune offsets, by pairs (count / 2) */
    {0.0f},
    {0.0f, 1.0f, -1.0f},
    {0.0f, 1.0f / 3.0f, -1.0f / 3.0f, 1.0f, -1.0f},
    {0.0f, 128.0f / 720.0f, -128.0f / 720.0f, 408.0f / 720.0f, -412.0f / 720.0f, 704.0f / 720.0f, -1.0f},
    {0.0f, 0.1f, -0.1f, 0.3f, -0.3f, 0.6f, -0.6f, 1.0f, -1.0f}};
static inline int knob9(float raw) { int k = (int)(raw + 0.5f); return k < 1 ? 1 : k > BR_SAWS ? BR_SAWS : k; }
static inline float br_detune(float x)           /* NuSaw's detune curve: fine at first, wide at the end */
{
    if (x < 0.1f)
        return x * 0.2f;
    if (x < 0.5f)
        return 0.02f + (x - 0.1f) * 0.575f;
    return 0.25f + (x - 0.5f) * 1.5f;
}
static inline const float *br_coef(const cfg_t *c) { return BR_COEF[(int)c->ex[3] > 4 ? 4 : (int)c->ex[3]]; }
static inline float br_dt(const cfg_t *c, const float *co, int s)   /* saw s's increment */
{
    return fm_clampf(c->ex[0] + co[s] * c->ex[1], 1e-6f, 0.45f);
}
static void br_prep(voice_t *v)
{
    cfg_t *c = &v->c;
    int n = knob9(c->ek[0]), M = n / 2, k, top;
    float f = c->freq * INV_SR, mx = c->e[2], w = c->e[3];
    const float *co;
    c->ex[0] = fm_minf(f, 0.45f);
    c->ex[1] = f * 0.1102f * br_detune(c->e[1]);
    c->ex[2] = (float)n;
    c->ex[3] = (float)M;
    c->ex[4] = fm_maxf(mx * fm_sqrtf(mx) * 0.667f, 0.015f);
    if (w != v->o[7] || (float)M != v->o[8]) {   /* the pans: when the spread or the pairs moved */
        v->o[7] = w;
        v->o[8] = (float)M;
        for (k = 1; k <= BR_PAIRS; k++) {        /* pair k: its upper saw towards the right, its lower the left */
            float a = (1.0f + (M ? (float)(k < M ? k : M) / (float)M * w : 0.0f)) * 0.78539816f;
            c->ex[3 + 2 * k] = fm_cosf(a) * 1.41421356f;
            c->ex[4 + 2 * k] = fm_sinf(a) * 1.41421356f;
        }
    }
    c->ex[11] = fm_minf(2.07345f * f, 0.9f);    /* 2 sin(pi fc / sr), fc = f / 3 */
    co = br_coef(c);
    top = n;                                     /* the saws br_run may sound before the next prepare: the knob's */
    if (v->o[9] > (float)top)                    /* count, or more while the ones above it fade out */
        top = v->o[9] < (float)BR_SAWS ? (int)v->o[9] + ((float)(int)v->o[9] < v->o[9]) : BR_SAWS;
    for (k = 0; k < top; k++)
        c->ex[BR_IDT(k)] = 1.0f / br_dt(c, co, k);
    if (c->ex[20] > 0.0f)                        /* the sub's increment (voice_chunk prepares again when it starts) */
        c->ex[22] = fm_minf(f * fm_exp2f(c->ex[21] * (1.0f / 12.0f)), 0.45f);
    c->stereo = w > 0.001f && n > 1;
}
/* a PolyBLEP saw's next sample; hi = 1 - dt */
SY_INLINE float br_one(float *p, float dt, float idt, float hi)
{
    float q = *p + dt, x, t;
    if (q > hi) {                                /* the rare branch: around the wrap */
        if (q >= 1.0f) {
            q -= 1.0f;
            x = 2.0f * q - 1.0f;
            if (q < dt) {
                t = q * idt;
                x -= t + t - t * t - 1.0f;
            }
        } else {
            x = 2.0f * q - 1.0f;
            t = (q - 1.0f) * idt;
            x -= t * t + t + t + 1.0f;
        }
    } else {
        x = 2.0f * q - 1.0f;
    }
    *p = q;
    return x;
}
/* one saw (K 1) or a pair (K 2) in one pass over the buffers; set: write them (the centre saw's pass, which
 * starts the sub-block), else add: xl + x0, xl + (x0 + x1), the sums one saw at a time made. (Not three in a
 * pass: faster on the host, but the FM-1's FPU works in its sixteen core registers, and three would spill.) */
SY_INLINE void br_pass(float *ph, const float *dt, const float *idt, const float *g, float *xl, float *xr, int m,
                       const int K, const int set, const int stereo)
{
    float a = ph[0], b = K > 1 ? ph[1] : 0.0f, da = dt[0], db = K > 1 ? dt[1] : 0.0f;
    float ia = idt[0], ib = K > 1 ? idt[1] : 0.0f, ha = 1.0f - da, hb = 1.0f - db;
    int i;
    for (i = 0; i < m; i++) {
        float x = br_one(&a, da, ia, ha), sl, sr = 0.0f;
        if (K == 1) {
            sl = x * g[0];
            if (stereo)
                sr = x * g[1];
        } else {
            float y = br_one(&b, db, ib, hb);
            sl = x * g[0] + y * g[2];
            if (stereo)
                sr = x * g[1] + y * g[3];
        }
        if (set) {
            xl[i] = sl;
            if (stereo)
                xr[i] = sr;
        } else {
            xl[i] += sl;
            if (stereo)
                xr[i] += sr;
        }
    }
    ph[0] = a;
    if (K > 1)
        ph[1] = b;
}
SY_INLINE void br_hp(float *x, float *lo, float *bp, float f, int m)
{
    float l = *lo, b = *bp;
    int i;
    for (i = 0; i < m; i++) {
        float h = x[i] - l - 1.4f * b;
        b += f * h;
        l += f * b;
        x[i] = h;
    }
    *lo = fm_flush(l);
    *bp = fm_flush(b);
}
SY_INLINE void br_loop(voice_t *v, float *xl, float *xr, int m, const int stereo)
{
    const cfg_t *c = &v->c;
    const float *co = br_coef(c);
    float nf = v->o[9], tg = c->ex[14], st = (float)m * BR_FADE, gs = c->ex[4], norm, gn;
    float g[2 * BR_SAWS], dt[BR_SAWS], idt[BR_SAWS];
    int j, na;
    if (nf < 0.0f)
        nf = tg;
    else if (nf < tg)
        nf = fm_minf(nf + st, tg);
    else if (nf > tg)
        nf = fm_maxf(nf - st, tg);
    v->o[9] = nf;
    na = (int)nf;
    if ((float)na < nf)
        na++;
    if (na > BR_SAWS)
        na = BR_SAWS;
    norm = fm_rsqrtf(1.0f + (nf - 1.0f) * gs * gs);
    gn = gs * norm;
    for (j = 0; j < na; j++) {                   /* the saws' increments and gains for the sub-block */
        float gg = (j ? gn : norm) * fm_minf(nf - (float)j, 1.0f);
        dt[j] = br_dt(c, co, j);
        idt[j] = c->ex[BR_IDT(j)];
        if (!j) {
            g[0] = g[1] = gg;
        } else {                                 /* the upper saw (odd) right, the lower left */
            const float *pn = &c->ex[3 + 2 * ((j + 1) >> 1)];
            g[2 * j] = stereo ? gg * pn[~j & 1] : gg;
            g[2 * j + 1] = gg * pn[j & 1];
        }
    }
    if (na < 1) {                                /* (none: not before the voice's first chunk) */
        for (j = 0; j < m; j++) {
            xl[j] = 0.0f;
            if (stereo)
                xr[j] = 0.0f;
        }
    } else {                                     /* the centre saw (writing), then the pairs */
        br_pass(&v->o[0], dt, idt, g, xl, xr, m, 1, 1, stereo);
        for (j = 1; j + 1 < na; j += 2)
            br_pass(&v->o[j], &dt[j], &idt[j], &g[2 * j], xl, xr, m, 2, 0, stereo);
        if (j < na)                              /* an even count's last */
            br_pass(&v->o[j], &dt[j], &idt[j], &g[2 * j], xl, xr, m, 1, 0, stereo);
    }
    br_hp(xl, &v->o[10], &v->o[11], c->ex[11], m);
    if (stereo)
        br_hp(xr, &v->o[12], &v->o[13], c->ex[11], m);
    if (c->ex[20] > 0.0f) {                      /* the sub: a sine, the same into both sides */
        float p = v->o[14], d = c->ex[22], gb = c->ex[20];
        for (j = 0; j < m; j++) {
            float x;
            p = step1(p, d);                     /* (fm_sin_turns without its floor: p is in [0, 1)) */
            x = fm_sin_pi((p < 0.5f ? p : p - 1.0f) * FM_TWO_PI) * gb;
            xl[j] += x;
            if (stereo)
                xr[j] += x;
        }
        v->o[14] = p;
    }
}
static void br_run(voice_t *v, float *xl, float *xr, int m)
{
    if (v->c.stereo)
        br_loop(v, xl, xr, m, 1);
    else
        br_loop(v, xl, xr, m, 0);
}
static void br_init(voice_t *v)                  /* the saws from random phases, the count from the knob */
{
    int j;
    for (j = 0; j < BR_MAX; j++)
        v->o[j] = (float)(xs(&v->rng) >> 8) * (1.0f / 16777216.0f);
    v->o[8] = -1.0f;                             /* (no pans made yet) */
    v->o[9] = -1.0f;
}

/* cane, voile and gingham: Braids' models (dsp/braids.h, MIT) */
#define CN_NONE 0xFFu
#include "braids.h"

/* sample: a sample of the bank (dsp/sample.h) at the note's pitch; its root note plays it at its own rate
 * (sm->rate: the factory's 22.05 kHz, half a sample a frame), linearly interpolated. start: where in it
 * (0..99 %); length: how much of the rest (127 = to its end): the region. Modes: one-shot (the region once; then the voice ends), loop
 * (from the region's start, round its loop: the sample's own loop if it lies inside the region, else the
 * region), reverse (the region once, backwards), pingpong (into the loop, then forth and back across it).
 * o[0] the position (a whole sample: exact in a float), o[1] its fraction, o[2] the direction (+-1), o[3]
 * 1 = done, o[4] 1 = started; the block being read is decoded in v->sc. */
static void smp_prep(voice_t *v)
{
    cfg_t *c = &v->c;
    int idx = (int)fm_clampf(fm_floorf(c->ek[0] + 0.5f), 0.0f, (float)(smp_slots() - 1));
    const smp_t *sm = smp_get(idx);
    float n, s0, s1, ls, le;
    c->stereo = 0;
    c->ex[6] = (float)idx;
    c->ex[7] = fm_clampf(fm_floorf(c->ek[3] + 0.5f), 0.0f, (float)(NSM - 1));
    if (!sm) {
        c->ex[0] = 0.0f;
        return;
    }
    n = (float)sm->n;
    s0 = fm_floorf(fm_minf(c->e[1], 0.99f) * n);
    s1 = fm_minf(s0 + fm_maxf(fm_floorf(c->e[2] * (n - s0) + 0.5f), 32.0f), n);
    ls = s0;
    le = s1;
    if (sm->le > sm->ls && (float)sm->ls >= s0 && (float)sm->le <= s1) {
        ls = (float)sm->ls;
        le = (float)sm->le;
    }
    /* the rate: the note over the root, times the sample's rate over ours (at most 3 octaves above its own) */
    c->ex[0] = fm_minf(c->freq / (440.0f * fm_exp2f(((float)sm->root - 69.0f) * (1.0f / 12.0f))) *
                       ((float)sm->rate / SY_SR), 8.0f);
    c->ex[1] = s0;
    c->ex[2] = s1;
    c->ex[3] = ls;
    c->ex[4] = le;
}
#define SMP_FADE 64.0f                           /* a one-shot's fade at its region's end: samples of it (~3 ms) */
static void smp_run(voice_t *v, float *xl, float *xr, int m)
{
    const cfg_t *c = &v->c;
    const smp_t *sm = smp_get((int)c->ex[6]);
    const int mode = (int)c->ex[7];
    const int32_t s0 = (int32_t)c->ex[1], s1 = (int32_t)c->ex[2], ls = (int32_t)c->ex[3], le = (int32_t)c->ex[4];
    const float inc = c->ex[0];
    float fr = v->o[1];
    int32_t pos = (int32_t)v->o[0], dir = v->o[2] < 0.0f ? -1 : 1;
    int i = 0, done = !sm || inc <= 0.0f || v->o[3] > 0.5f, turns = 0;
    (void)xr;
    if (!done && v->o[4] < 0.5f) {               /* the note's first sample: the region's start (reverse: its end) */
        v->o[4] = 1.0f;
        pos = mode == SM_REVERSE ? s1 - 1 : s0;
        fr = 0.0f;
        dir = mode == SM_REVERSE ? -1 : 1;
    }
    while (!done && i < m) {
        int32_t lo = s0, hi = s1;
        if (mode == SM_LOOP || mode == SM_PINGPONG) {   /* into the loop, then round (or across) it */
            hi = le;
            lo = dir < 0 || pos >= ls ? ls : s0;
        }
        {
            int i0 = i;
            float p0 = (float)pos + (dir > 0 ? fr : -fr);
            i += smp_play(&smp_vc[v->vi % NVOICE], sm, &pos, &fr, inc, dir, lo, hi, xl + i, m - i);
            if (mode == SM_ONESHOT || mode == SM_REVERSE) {   /* the region's last SMP_FADE samples fade out: a sample
                                                                * cut short (or ending loud) ends without a click */
                float rem = dir > 0 ? (float)s1 - p0 : p0 - (float)s0, j0 = rem - inc * (float)(i - i0);
                if (j0 < SMP_FADE)
                    for (int j = i0; j < i; j++, rem -= inc)
                        if (rem < SMP_FADE)
                            xl[j] *= fm_maxf(rem, 0.0f) * (1.0f / SMP_FADE);
            }
        }
        if (i >= m)
            break;
        if (mode == SM_ONESHOT || mode == SM_REVERSE || le - ls < 2 || ++turns > 4)
            done = 1;                            /* (a loop shorter than the step: once round is enough) */
        else if (mode == SM_LOOP)
            pos = ls + (pos - le) % (le - ls);
        else if (dir > 0) {                      /* pingpong: back from the loop's end */
            pos = 2 * (le - 1) - pos - (fr > 0.0f);
            fr = fr > 0.0f ? 1.0f - fr : 0.0f;
            dir = -1;
            if (pos < ls)
                pos = ls;
        } else {                                 /* and forth from its start */
            pos = 2 * ls - pos - (fr > 0.0f);
            fr = fr > 0.0f ? 1.0f - fr : 0.0f;
            dir = 1;
            if (pos >= le)
                pos = le - 1;
        }
    }
    if (done) {                                  /* the region has played (or nothing to play): silence, the voice
                                                  * is free (its amp ramps to 0 over the silence) */
        for (; i < m; i++)
            xl[i] = 0.0f;
        v->o[3] = 1.0f;
        v->amp.stage = ENV_OFF;
        v->amp.level = 0.0f;
    }
    v->o[0] = (float)pos;
    v->o[1] = fr;
    v->o[2] = (float)dir;
}

/* slice: a loop cut into even slices (synth.h EG_SLICE), the note choosing one: (note - root) mod slices. Its
 * rate: a sample that states its bars lasts that many bars at the tempo (n samples in bars * 4 beats, whatever
 * its rate), else its own (sm->rate); the pitch knob (and the bend, an LFO on the pitch) on top, so a slice
 * pitched down plays longer than its place in the bar. The gate is the share of the slice that plays, then the voice
 * ends (as one-shot); every slice fades in over SL_FADE_IN frames and out over the last SL_FADE_OUT before
 * its gate's end, so a cut never clicks. A mono retrigger, or a legato note picking another slice, fades the
 * old one out over SL_SWITCH frames first (one cache window a voice: no second read position to crossfade
 * with); legato then goes on at the same place in the new slice, the time running on.
 * o[0] the position, o[1] its fraction, o[2] the slice playing, o[3] 1 = done, o[4] 1 = started, o[5] frames
 * into the slice (its fade in), o[6] the switch's fade (0 .. 1 = silent), o[7] a switch waiting: 1 the note's
 * slice from its start (a retrigger), 2 the note's slice at the same place (legato), 3 the end (the gate
 * turned down behind the position), o[8] the gate's end the last time (in the sample) */
#define SL_FADE_IN 32.0f                         /* 0.7 ms */
#define SL_FADE_OUT 96.0f                        /* 2.2 ms */
#define SL_SWITCH 64.0f                          /* 1.5 ms */
static void slc_prep(voice_t *v, float bpm)
{
    cfg_t *c = &v->c;
    int idx = (int)fm_clampf(fm_floorf(c->ek[0] + 0.5f), 0.0f, (float)(smp_slots() - 1));
    const smp_t *sm = smp_get(idx);
    float rate, semi;
    c->stereo = 0;
    c->ex[6] = (float)idx;
    c->ex[7] = (float)sl_count((int)(c->ek[1] + 0.5f));
    if (!sm || sm->n < 64u) {
        c->ex[0] = 0.0f;
        return;
    }
    rate = sm->bars ? (float)sm->n * bpm * (1.0f / (240.0f * SY_SR)) / (float)sm->bars : (float)sm->rate / SY_SR;
    semi = fm_clampf(c->ek[2], 40.0f, 88.0f) - 64.0f + c->pitch_off * 0.01f;
    c->ex[0] = fm_clampf(rate * fm_exp2f(semi * (1.0f / 12.0f)), 0.01f, 8.0f);
    c->ex[1] = fm_maxf(c->e[3], 0.02f);
    c->ex[2] = (float)sm->root;
    c->ex[3] = 1.0f / (c->ex[0] * SL_FADE_OUT);  /* source samples to the gate's end -> the fade out's gain */
}
/* slice k of ns: its first sample s0 and the gate's end e */
static void slc_bounds(const smp_t *sm, int ns, int k, float gate, int32_t *s0, int32_t *e)
{
    int32_t a = (int32_t)((uint32_t)k * sm->n / (uint32_t)ns), b = (int32_t)((uint32_t)(k + 1) * sm->n / (uint32_t)ns);
    int32_t g = (int32_t)(gate * (float)(b - a));
    *s0 = a;
    *e = a + (g < 32 ? 32 : g > b - a ? b - a : g);
}
static void slc_run(voice_t *v, float *xl, float *xr, int m)
{
    const cfg_t *c = &v->c;
    const smp_t *sm = smp_get((int)c->ex[6]);
    const int ns = (int)c->ex[7];
    const float inc = c->ex[0], gate = c->ex[1];
    smp_cache_t *ca = &smp_vc[v->vi % NVOICE];
    float fr = v->o[1], fin = v->o[5], sw = v->o[6];
    int32_t pos = (int32_t)v->o[0], s0 = 0, e = 0;
    int cur = (int)v->o[2], pend = (int)v->o[7], want = 0, i = 0;
    int done = !sm || inc <= 0.0f || v->o[3] > 0.5f || ns < 1;
    (void)xr;
    if (!done) {
        int d = ((int)v->note - (int)c->ex[2]) % ns;
        want = d < 0 ? d + ns : d;
        if (v->o[4] < 0.5f) {                    /* the note's first sample: its slice's start */
            v->o[4] = 1.0f;
            cur = want;
            pend = 0;
            sw = fin = fr = 0.0f;
            slc_bounds(sm, ns, cur, gate, &s0, &e);
            pos = s0;
        } else if (!pend && (want != cur || cur >= ns)) {
            pend = 2;                            /* (a legato note on another slice, the slices turned) */
        }
    }
    while (!done && i < m) {
        int k, j, req = m - i;
        float lo;
        if (pend == 3 && sw >= 1.0f) {           /* faded out past a gate turned down: the end */
            done = 1;
            break;
        }
        if (pend && sw >= 1.0f) {                /* faded out: the switch */
            int32_t off = 0;
            if (pend == 2 && cur < ns) {
                slc_bounds(sm, ns, cur, gate, &s0, &e);
                off = pos - s0;
            } else {
                fr = 0.0f;
            }
            cur = want;
            slc_bounds(sm, ns, cur, gate, &s0, &e);
            pos = s0 + (off > 0 ? off : 0);
            pend = 0;
            sw = fin = 0.0f;
            if (pos >= e) {
                done = 1;
                break;
            }
        }
        slc_bounds(sm, ns, cur, gate, &s0, &e);
        if (!pend && pos >= e && cur < ns) {     /* at the gate's end: it has played; or the gate was turned
                                                  * down behind the position: a fade, then the end */
            if ((float)e >= v->o[8]) {
                done = 1;
                break;
            }
            pend = 3;
        }
        v->o[8] = (float)e;
        if (pend == 3)                           /* (meanwhile on into the slice, the gate's fade out of it) */
            slc_bounds(sm, ns, cur, 1.0f, &s0, &e);
        if (pend) {                              /* up to the switch */
            int left = (int)((1.0f - sw) * SL_SWITCH + 0.999f);
            req = left < 1 ? 1 : left < req ? left : req;
        }
        lo = pend == 3 ? 1e9f : ((float)(e - pos) - fr) * c->ex[3];
        k = smp_play(ca, sm, &pos, &fr, inc, 1, s0, e, xl + i, req);
        for (j = 0; j < k; j++) {                /* the fades: in, out at the gate's end, the switch's */
            float g = fm_minf(fm_minf((fin + (float)j) * (1.0f / SL_FADE_IN), lo - (float)j * (1.0f / SL_FADE_OUT)), 1.0f);
            if (pend)
                g *= 1.0f - fm_minf(sw + (float)j * (1.0f / SL_SWITCH), 1.0f);
            xl[i + j] *= fm_maxf(g, 0.0f);
        }
        fin = fm_minf(fin + (float)k, SL_FADE_IN);
        if (pend)
            sw = fm_minf(sw + (float)k * (1.0f / SL_SWITCH), 1.0f);
        i += k;
        if (k < req) {                           /* the gate's end: the slice has played */
            if (pend)
                sw = 1.0f;                       /* (a switch waiting: now) */
            else
                done = 1;
        }
    }
    if (done) {                                  /* silence: the voice is free (as the sample engine's one-shot) */
        for (; i < m; i++)
            xl[i] = 0.0f;
        v->o[3] = 1.0f;
        v->amp.stage = ENV_OFF;
        v->amp.level = 0.0f;
    }
    v->o[0] = (float)pos;
    v->o[1] = fr;
    v->o[2] = (float)cur;
    v->o[5] = fin;
    v->o[6] = sw;
    v->o[7] = (float)pend;
}
static void slc_retrigger(voice_t *v)            /* a mono retrigger: the playing slice fades, the note's starts */
{
    if (v->o[4] > 0.5f && v->o[3] < 0.5f)
        v->o[7] = 1.0f;
    else
        v->o[3] = v->o[4] = 0.0f;
}

static void eng_chunk(voice_t *v)                /* every chunk: the engines' own slow movements */
{
    switch (v->engine) {
    case EG_ORBIT: orbit_chunk(v); break;
    case EG_TINES: tines_chunk(v); break;
    case EG_ORGAN: organ_chunk(v); break;
    case EG_CANE: cn_chunk(v); break;
    case EG_VOILE: vo_chunk(v); break;
    case EG_GINGHAM: gh_chunk(v); break;
    default: break;
    }
}
static void eng_prep(voice_t *v, float bpm)
{
    switch (v->engine) {
    case EG_WEFT: weft_prep(v); break;
    case EG_STACK: stack_prep(v); break;
    case EG_WAVETABLE: wt_prep(v); break;
    case EG_ORBIT: orbit_prep(v); break;
    case EG_TINES: tines_prep(v); break;
    case EG_ORGAN: organ_prep(v); break;
    case EG_SYNC: hs_prep(v); break;
    case EG_SAMPLE: smp_prep(v); break;
    case EG_SLICE: slc_prep(v, bpm); break;
    case EG_BROCADE: br_prep(v); break;
    case EG_CANE: cn_prep(v); break;
    case EG_VOILE: vo_prep(v); break;
    case EG_GINGHAM: gh_prep(v); break;
    default: dis_prep(v); break;
    }
}
static void eng_run(voice_t *v, float *xl, float *xr, int m)
{
    switch (v->engine) {
    case EG_WEFT: weft_run(v, xl, xr, m); break;
    case EG_STACK: stack_run(v, xl, xr, m); break;
    case EG_WAVETABLE: wt_run(v, xl, xr, m); break;
    case EG_ORBIT: orbit_run(v, xl, xr, m); break;
    case EG_TINES: tines_run(v, xl, xr, m); break;
    case EG_ORGAN: organ_run(v, xl, xr, m); break;
    case EG_SYNC: hs_run(v, xl, xr, m); break;
    case EG_SAMPLE: smp_run(v, xl, xr, m); break;
    case EG_SLICE: slc_run(v, xl, xr, m); break;
    case EG_BROCADE: br_run(v, xl, xr, m); break;
    case EG_CANE: cn_run(v, xl, xr, m); break;
    case EG_VOILE: vo_run(v, xl, xr, m); break;
    case EG_GINGHAM: gh_run(v, xl, xr, m); break;
    default: dis_run(v, xl, xr, m); break;
    }
}

/* ------------------------------------------------------------ the voice --- */
static inline int v_sounding(const voice_t *v) { return v->amp.stage != ENV_OFF || v->steal_phase != 0; }

static float porta_ms(int glide_ms, int porta) { return glide_ms > 0 ? (float)glide_ms : (float)(porta * porta) * 0.125f; }   /* 0 .. 2 s */

static void voice_start(synth_t *s, voice_t *v, int note, int vel, int glide_ms, uint32_t tag, int held, int in_render)
{
    trk_t *t = &s->t[v->trk];
    float ms = porta_ms(glide_ms, P(s, v->trk, TP_PORTA));
    int eng = P(s, v->trk, TP_ENGINE);
    if (!eg_synth(eng))
        eng = EG_WEFT;
    v->tws = v->tws_pend;                        /* cane: the slot voice_take kept for this note */
    v->cost_m = t->cost_m;                       /* (the split's guess: what the track's voices take) */
    if (eng != v->engine || !v_sounding(v)) {
        v->engine = (uint8_t)eng;
        eng_init(v);
    }
    v->note = (uint8_t)note;
    v->tag = tag;
    v->vel = (float)vel * (1.0f / 127.0f);
    v->held = (uint8_t)held;
    v->pitch = (float)note;
    v->pitch_cur = (ms > 0.5f && t->last_pitch > 0.0f) ? t->last_pitch : v->pitch;
    v->glide_k = ms > 0.5f ? 1.0f - fm_expf(-(float)SY_CHUNK * 1000.0f / (ms * SY_SR)) : 1.0f;
    if (!in_render)                              /* a render (the steal) writes nothing the tracks share */
        v->age = s->counter++;
    v->amp.stage = ENV_ATTACK;
    v->flt.stage = ENV_ATTACK;
    v->tsec = 0.0f;
    if (v->engine == EG_TINES) {
        v->o[15] = 1.0f;
        v->o[14] = 1.0f;
        v->o[13] = 1.0f;
    }
    if (v->engine == EG_SAMPLE || v->engine == EG_SLICE) {   /* from the region's (the slice's) start again */
        v->o[3] = v->o[4] = 0.0f;
        v->o[6] = v->o[7] = 0.0f;
    }
    if (v->engine == EG_CANE)                    /* the Braids engines: a strike (Braids' trigger) */
        cn_strike(v, 1);
    else if (v->engine == EG_VOILE)
        vo_strike(v);
    else if (v->engine == EG_GINGHAM)
        gh_strike(v);
    if (!in_render)
        t->last_pitch = v->pitch;
    v->fresh = 1;
}

static int track_voices(const synth_t *s, int trk)
{
    int i, n = 0;
    for (i = 0; i < s->max_voices; i++)
        n += s->v[i].trk == trk && v_sounding(&s->v[i]);
    return n;
}

/* brocade's cap: at most br_cap() of its voices in the pool, by the saws of the track that wants one
 * (BR_VOICES at six or fewer, five at seven: its voice costs about two and a half ordinary ones there). A voice
 * counts as what it will play (a steal's pending note: its track's engine). */
#define BR_VOICES 6
static int br_cap(const synth_t *s, int trk)
{
    int n = knob9((float)P(s, trk, TP_E1));
    return n >= 7 ? 5 : BR_VOICES;
}
static int br_voice(const synth_t *s, const voice_t *v)
{
    if (!v_sounding(v))
        return 0;
    return (v->steal_phase == 1 ? P(s, v->trk, TP_ENGINE) : v->engine) == EG_BROCADE;
}
int synth_brocade_voices(const synth_t *s)
{
    int i, n = 0;
    for (i = 0; i < NVOICE; i++)
        n += br_voice(s, &s->v[i]);
    return n;
}
static int br_find(const synth_t *s, int trk)   /* the brocade voice to take when the cap is reached; -1 */
{
    int i, n = 0, best = -1, own = 0;
    float quiet = 1e9f;
    uint32_t oldest = 0xFFFFFFFFu;
    for (i = 0; i < s->max_voices; i++)
        n += br_voice(s, &s->v[i]);
    if (n < br_cap(s, trk))
        return -1;
    for (i = 0; i < s->max_voices; i++) {        /* releasing first (the quietest), then the track's own oldest */
        const voice_t *v = &s->v[i];
        if (!br_voice(s, v) || v->steal_phase == 1)
            continue;
        if (v->amp.stage == ENV_RELEASE && v->amp.level < quiet) {
            quiet = v->amp.level;
            best = i;
        }
    }
    if (best >= 0)
        return best;
    for (i = 0; i < s->max_voices; i++) {
        const voice_t *v = &s->v[i];
        int mine = v->trk == trk;
        if (!br_voice(s, v) || v->steal_phase == 1 || (own && !mine))
            continue;
        if ((mine && !own) || v->age < oldest) {
            oldest = v->age;
            best = i;
            own = mine;
        }
    }
    if (best < 0)                                /* all of them fading for a note already: the newest of */
        for (i = 0; i < s->max_voices; i++)      /* those waits for this note instead */
            if (br_voice(s, &s->v[i]) && (best < 0 || s->v[i].age > s->v[best].age))
                best = i;
    return best;
}

/* cane's cap: its waveguides play from CN_SLOTS line slots (dsp/braids.h cn_mem), so at most CN_SLOTS of its
 * voices: one that plays cane (sounding) or will (a steal's pending note that voice_take kept a slot for) */
static int cn_voice(const voice_t *v)
{
    return (v->engine == EG_CANE && v_sounding(v)) || (v->steal_phase == 1 && v->tws_pend != CN_NONE);
}
static int cn_holds(const voice_t *v, int j)     /* the voice plays from slot j, or will */
{
    return (v->engine == EG_CANE && v_sounding(v) && v->tws == j) || (v->steal_phase == 1 && v->tws_pend == j);
}
static int cn_find(const synth_t *s, int trk)    /* the cane voice to take when the cap is reached; -1 */
{
    int i, n = 0, best = -1, own = 0;
    float quiet = 1e9f;
    uint32_t oldest = 0xFFFFFFFFu;
    for (i = 0; i < NVOICE; i++)
        n += cn_voice(&s->v[i]);
    if (n < CN_SLOTS)
        return -1;
    for (i = 0; i < NVOICE; i++) {               /* releasing first (the quietest), then the track's own oldest */
        const voice_t *v = &s->v[i];
        if (!cn_voice(v) || v->steal_phase == 1)
            continue;
        if (v->amp.stage == ENV_RELEASE && v->amp.level < quiet) {
            quiet = v->amp.level;
            best = i;
        }
    }
    if (best >= 0)
        return best;
    for (i = 0; i < NVOICE; i++) {
        const voice_t *v = &s->v[i];
        int mine = v->trk == trk;
        if (!cn_voice(v) || v->steal_phase == 1 || (own && !mine))
            continue;
        if ((mine && !own) || v->age < oldest) {
            oldest = v->age;
            best = i;
            own = mine;
        }
    }
    if (best < 0)                                /* all of them fading for a note already: the newest waits */
        for (i = 0; i < NVOICE; i++)
            if (cn_voice(&s->v[i]) && (best < 0 || s->v[i].age > s->v[best].age))
                best = i;
    return best;
}
/* the slot for a cane note on voice vi: its own when it plays cane now (the note follows on its lines), else
 * one no other voice holds (cn_find made sure there is one); CN_NONE when the track is not cane */
static void cn_reserve(synth_t *s, int vi, int trk)
{
    voice_t *v = &s->v[vi];
    int j, i;
    v->tws_pend = CN_NONE;
    if (P(s, trk, TP_ENGINE) != EG_CANE)
        return;
    if (v->engine == EG_CANE && v_sounding(v) && v->tws < CN_SLOTS) {
        v->tws_pend = v->tws;
        return;
    }
    for (j = 0; j < CN_SLOTS; j++) {
        int used = 0;
        for (i = 0; i < NVOICE && !used; i++)
            used = i != vi && cn_holds(&s->v[i], j);
        if (!used) {
            v->tws_pend = (uint8_t)j;
            return;
        }
    }
}

/* a voice for a new note: a free one; else the track's oldest if it holds NVOICE_TRK already; else
 * the quietest releasing; else the oldest of all. A stolen voice fades 15 ms first. brocade and cane first
 * keep to their own caps (br_find, cn_find). */
static int find_voice(synth_t *s, int trk)
{
    int i, best = -1;
    float quiet = 1e9f;
    uint32_t oldest = 0xFFFFFFFFu;
    if (P(s, trk, TP_ENGINE) == EG_BROCADE && (best = br_find(s, trk)) >= 0)
        return best;
    if (P(s, trk, TP_ENGINE) == EG_CANE && (best = cn_find(s, trk)) >= 0)
        return best;
    if (track_voices(s, trk) >= NVOICE_TRK) {
        for (i = 0; i < s->max_voices; i++)
            if (s->v[i].trk == trk && s->v[i].steal_phase == 0 && s->v[i].age < oldest) {
                oldest = s->v[i].age;
                best = i;
            }
        if (best >= 0)
            return best;
    }
    for (i = 0; i < s->max_voices; i++)
        if (!v_sounding(&s->v[i]))
            return i;
    for (i = 0; i < s->max_voices; i++)
        if (s->v[i].amp.stage == ENV_RELEASE && s->v[i].steal_phase == 0 && s->v[i].amp.level < quiet) {
            quiet = s->v[i].amp.level;
            best = i;
        }
    if (best >= 0)
        return best;
    for (i = 0; i < s->max_voices; i++)
        if (s->v[i].steal_phase == 0 && s->v[i].age < oldest) {
            oldest = s->v[i].age;
            best = i;
        }
    if (best >= 0)
        return best;
    for (i = 0, oldest = 0xFFFFFFFFu; i < s->max_voices; i++)
        if (s->v[i].age < oldest) {
            oldest = s->v[i].age;
            best = i;
        }
    return best >= 0 ? best : 0;
}

static void voice_chunk(synth_t *s, voice_t *v);
static void voice_config_now(synth_t *s, voice_t *v)   /* a voice started mid-chunk: its parameters now */
{
    v->ck = (uint8_t)(v->ck - 1);                /* voice_chunk takes the slot the boundary read */
    v->nchunk = (uint8_t)(v->nchunk - 1);
    voice_chunk(s, v);
}

static void voice_take(synth_t *s, int vi, int trk, int note, int vel, int glide_ms, uint32_t tag)
{
    voice_t *v = &s->v[vi];
    cn_reserve(s, vi, trk);
    if (!v_sounding(v)) {
        int i;
        v->trk = (uint8_t)trk;
        v->steal_gain = 1.0f;
        v->steal_phase = 0;
        v->amp.level = v->flt.level = 0.0f;
        for (i = 0; i < 8; i++)
            v->f[i] = 0.0f;
        for (i = 0; i < 8; i++)
            v->cz[i] = 0.0f;
        v->amp_cur = 0.0f;
        v->glc = v->grc = 0.0f;
        voice_start(s, v, note, vel, glide_ms, tag, 1, 0);
        v->fresh |= 2;                           /* configured by its render (synth_render), on its core */
    } else {                                     /* fade the old note out, then start (voice_steal) */
        v->pend_note = (uint8_t)note;
        v->pend_vel = (uint8_t)vel;
        v->pend_tag = tag;
        v->pend_glide = (int16_t)(glide_ms > 32767 ? 32767 : glide_ms);
        v->pend_held = 1;
        v->trk = (uint8_t)trk;                   /* the track's from now: its note off finds it */
        v->steal_phase = 1;
        v->age = s->counter++;
    }
}

/* ---------------------------------------------------------------- notes --- */
/* a regular (not free) wave or drift LFO restarts on every note of its track */
static void lfo_retrigger(synth_t *s, int trk)
{
    trk_t *t = &s->t[trk];
    int type = P(s, trk, TP_LTYPE);
    if ((type != LT_WAVE && type != LT_DRIFT) || P(s, trk, TP_LDEST) >= LD_FREE)
        return;
    t->lfo_ph = 0.0f;
    if (type == LT_DRIFT) {
        t->lfo_sh = frand(&t->rng);
        t->lfo_sm = t->lfo_sh;
    }
}

void synth_note_on(synth_t *s, int trk, int note, int vel, int glide_ms, uint32_t tag)
{
    trk_t *t;
    int mode;
    if (trk < 0 || trk >= NTRK || note < 0 || note > 127)
        return;
    if (P(s, trk, TP_ENGINE) == EG_DRUMLINE)
        return;                                  /* dsp/drumline.c plays those */
    if (vel < 1)
        vel = 1;
    if (vel > 127)
        vel = 127;
    t = &s->t[trk];
    mode = P(s, trk, TP_VMODE);
    lfo_retrigger(s, trk);
    t->pb_snap = 1;
    if (mode == VM_POLY) {
        voice_take(s, find_voice(s, trk), trk, note, vel, glide_ms, tag);
        return;
    }
    {   /* mono / legato: one voice, a stack of held notes */
        int i, k = -1;
        voice_t *v = t->mono_voice >= 0 ? &s->v[t->mono_voice] : 0;
        for (i = 0; i < t->mono_n; i++)
            if (t->mono_note[i] == note)
                k = i;
        if (k < 0 && t->mono_n < 8) {
            t->mono_note[t->mono_n] = (uint8_t)note;
            t->mono_vel[t->mono_n] = (uint8_t)vel;
            t->mono_tag[t->mono_n] = tag;
            t->mono_n++;
        }
        if (v && v->trk == trk && v_sounding(v) && v->steal_phase == 0) {
            int was_held = v->held;
            float ms = porta_ms(glide_ms, P(s, trk, TP_PORTA));
            v->note = (uint8_t)note;
            v->tag = tag;
            v->vel = (float)vel * (1.0f / 127.0f);
            v->held = 1;
            v->pitch = (float)note;
            v->glide_k = ms > 0.5f ? 1.0f - fm_expf(-(float)SY_CHUNK * 1000.0f / (ms * SY_SR)) : 1.0f;
            t->last_pitch = v->pitch;
            if (mode == VM_MONO || !was_held || v->amp.stage == ENV_RELEASE) {   /* legato: only from silence */
                v->amp.stage = ENV_ATTACK;       /* retrigger from the level now */
                v->flt.stage = ENV_ATTACK;
                v->tsec = 0.0f;
                if (v->engine == EG_TINES)
                    v->o[15] = v->o[14] = v->o[13] = 1.0f;
                if (v->engine == EG_SAMPLE)
                    v->o[3] = v->o[4] = 0.0f;
                if (v->engine == EG_SLICE)
                    slc_retrigger(v);
                if (v->engine == EG_CANE)        /* the lines as they are, the exciter again */
                    cn_strike(v, 0);
                else if (v->engine == EG_VOILE)
                    vo_strike(v);
                else if (v->engine == EG_GINGHAM)
                    gh_strike(v);
            }
            return;
        }
        i = find_voice(s, trk);
        t->mono_voice = (int8_t)i;
        voice_take(s, i, trk, note, vel, glide_ms, tag);
    }
}

static void voice_release(voice_t *v)
{
    v->held = 0;
    if (v->amp.stage != ENV_OFF && v->amp.stage != ENV_RELEASE) {
        v->amp.stage = ENV_RELEASE;
        v->flt.stage = ENV_RELEASE;
    }
}

void synth_note_off(synth_t *s, int trk, uint32_t tag)
{
    trk_t *t;
    int i;
    if (trk < 0 || trk >= NTRK)
        return;
    t = &s->t[trk];
    for (i = 0; i < t->mono_n; i++)              /* the mono stack (kept in every mode: harmless in poly) */
        if (t->mono_tag[i] == tag) {
            int j;
            for (j = i; j < t->mono_n - 1; j++) {
                t->mono_note[j] = t->mono_note[j + 1];
                t->mono_vel[j] = t->mono_vel[j + 1];
                t->mono_tag[j] = t->mono_tag[j + 1];
            }
            t->mono_n--;
            break;
        }
    if (P(s, trk, TP_VMODE) != VM_POLY && t->mono_voice >= 0) {
        voice_t *v = &s->v[t->mono_voice];
        if (v->trk != trk)
            return;
        if (v->steal_phase == 1 && v->pend_tag == tag) {
            v->pend_held = 0;
            return;
        }
        if (v->tag != tag)
            return;
        if (t->mono_n > 0) {                     /* back to the note still held */
            int k = t->mono_n - 1;
            v->note = t->mono_note[k];
            v->tag = t->mono_tag[k];
            v->pitch = (float)v->note;
            t->last_pitch = v->pitch;
        } else {
            voice_release(v);
        }
        return;
    }
    for (i = 0; i < NVOICE; i++) {
        voice_t *v = &s->v[i];
        if (v->trk != trk)
            continue;
        if (v->held && v->tag == tag && v->steal_phase != 1)
            voice_release(v);
        if (v->steal_phase == 1 && v->pend_tag == tag)
            v->pend_held = 0;                    /* let go before its steal finished: just its release */
    }
}

void synth_all_off(synth_t *s, int trk)
{
    int i;
    for (i = 0; i < NVOICE; i++) {
        voice_t *v = &s->v[i];
        if (trk >= 0 && v->trk != trk)
            continue;
        voice_release(v);
        if (v->steal_phase == 1)
            v->pend_held = 0;
    }
    for (i = 0; i < NTRK; i++)
        if (trk < 0 || i == trk) {
            s->t[i].mono_n = 0;
            s->t[i].mono_voice = -1;
        }
}

void synth_bend(synth_t *s, int trk, int cents)
{
    if (trk >= 0 && trk < NTRK)
        s->t[trk].bend_c = (int16_t)(cents < -32000 ? -32000 : cents > 32000 ? 32000 : cents);
}
perf_t *synth_perf(synth_t *s, int trk) { return &s->t[trk < 0 ? 0 : trk >= NTRK ? NTRK - 1 : trk].perf; }
void synth_lock(synth_t *s, int trk, int param, int val)
{
    if (trk >= 0 && trk < NTRK && param >= 0 && param < NTP_LOCK)
        s->t[trk].lock[param] = (uint8_t)(val < 0 ? 0 : val > 255 ? 255 : val);
}
void synth_tempo(synth_t *s, float bpm) { s->bpm = bpm < 20.0f ? 20.0f : bpm > 400.0f ? 400.0f : bpm; }
void synth_limit(synth_t *s, int max_voices)
{
    s->max_voices = (uint8_t)(max_voices < 1 ? 1 : max_voices > NVOICE ? NVOICE : max_voices);
}
/* a voice's cost in units of ~1.1 % of a block: on the FM-1 its measured time (or, a note just started, its track's
 * voices' and a unit for its set-up); else, and in the simulator (its samples the same every run), the model below */
#define COST_TICKS_UNIT 5.98f                    /* 1.1 % of 544 clock ticks, a frame's time */
int synth_voice_cost(const synth_t *s, int vi)
{
    const voice_t *v = &s->v[vi];
    if (!v_sounding(v))
        return 0;
#ifndef PLAT_HOST
    if (v->cost_m > 0.0f) {
        int u = (int)(v->cost_m * (1.0f / COST_TICKS_UNIT) + 0.5f) + (v->fresh & 2 ? 1 : 0);
        return u < 1 ? 1 : u > 40 ? 40 : u;
    }
#endif
    if (v->engine == EG_CANE || v->engine == EG_VOILE || v->engine == EG_GINGHAM) {   /* by the model (braids.h) */
        int md = v->c.ex[19] > 0.0f ? (int)v->c.ex[19] : 0;
        return v->engine == EG_CANE ? CN_COST[md < NCANE ? md : 0] : v->engine == EG_VOILE ? VO_COST[md < NVOILE ? md : 0]
                                                                                          : GH_COST[md < NGINGHAM ? md : 0];
    }
    if (v->engine == EG_WEFT || v->engine == EG_STACK || v->engine == EG_ORGAN)
        return 5;                                /* (the FM-1, tools/measure_load.sh 2026-10-18: a quarter above the rest) */
    if (v->engine != EG_BROCADE)
        return 4;
    /* on the FM-1 (measure_load.sh, 2026-10-18): 4 + a unit a saw (11 at seven: an ordinary voice's two and a half) */
    return 4 + (int)(v->o[9] < 0.0f ? v->c.ex[14] : v->o[9] + 0.99f) + (v->c.ex[20] > 0.0f);   /* (+1: the sub) */
}
void synth_duck_trigger(synth_t *s, int src)
{
    int i;
    for (i = 0; i < NTRK; i++)
        if (P(s, i, TP_LTYPE) == LT_PUMP && P(s, i, TP_LSPEED) == src) {
            s->t[i].duck = 1.0f;
            s->t[i].duck_hold = env_seconds((float)P(s, i, TP_LDEST) * (1.0f / 127.0f));
        }
}

void synth_init(synth_t *s, uint8_t (*par)[NTP])
{
    int i, j;
    uint8_t *b = (uint8_t *)s;
    for (i = 0; i < (int)sizeof *s; i++)
        b[i] = 0;
    s->par = par;
    s->bpm = 120.0f;
    s->max_voices = NVOICE;
    for (i = 0; i < NTRK; i++) {
        trk_t *t = &s->t[i];
        for (j = 0; j < NTP_LOCK; j++)
            t->lock[j] = LOCK_NONE;
        t->mono_voice = -1;
        t->rng = 0x9E3779B9u * (uint32_t)(i + 1);
        t->ldest = LD_NONE;
        for (j = 0; j < NCK; j++)
            t->lgain[j] = 1.0f;
    }
    for (i = 0; i < NVOICE; i++) {
        voice_t *v = &s->v[i];
        v->vi = (uint8_t)i;
        v->rng = 0x2545F491u * (uint32_t)(i + 1) + 1u;
        v->steal_gain = 1.0f;
        v->engine = EG_WEFT;
        v->tws = v->tws_pend = CN_NONE;
        for (j = 0; j < 8; j++)
            v->env_in[j] = -1.0f;                /* the caches: nothing made yet */
        v->pan_in = v->pitch_in = -1.0f;
        eng_init(v);
    }
    for (i = 0; i < NTRK; i++)
        synth_peak[i] = 0;
}

/* -------------------------------------------------------------- prepare --- */
static inline float lfo_wave_value(int shape, float ph)   /* square, sine, ramp, saw */
{
    switch (shape & 3) {
    case 0: return ph < 0.5f ? 1.0f : -1.0f;
    case 1: return fm_sin_turns(ph);
    case 2: return 2.0f * ph - 1.0f;
    default: return 1.0f - 2.0f * ph;
    }
}
static inline float lfo_wave_trem(int shape, float ph)    /* sine, saw, exponential, square, blip */
{
    switch (shape) {
    case 1: return 1.0f - 2.0f * ph;
    case 2: return 2.0f * fm_exp2f(-8.0f * ph) - 1.0f;
    case 3: return ph < 0.5f ? 1.0f : -1.0f;
    case 4: return ph < 0.08f ? 1.0f : -1.0f;
    default: return fm_sin_turns(ph);
    }
}

/* the track-level values of every chunk boundary inside the next n frames: the LFO (wave, drift:
 * an offset on its destination; wobble: a gain and a vibrato; pump: a gain), one slot each. The
 * voices read the slots in order as they cross the boundaries. Also the meters. */
void synth_prepare(synth_t *s, uint32_t n)
{
    float tpk[NTRK];
    int i, k, nck = 0;
    uint32_t off = (SY_CHUNK - s->pos) % SY_CHUNK;
    for (; off < n && nck < NCK; off += SY_CHUNK)
        nck++;
    s->nck = (uint8_t)(nck ? nck : 1);
    for (i = 0; i < NTRK; i++)
        tpk[i] = 0.0f;
    for (k = 0; k < NVOICE; k++) {               /* each track's loudest voice, the voices' peaks cleared: one pass */
        voice_t *v = &s->v[k];
        if (v->trk < NTRK) {
            if (v->peak > tpk[v->trk])
                tpk[v->trk] = v->peak;
            v->peak = 0.0f;
        }
    }
    for (i = 0; i < NTRK; i++) {
        trk_t *t = &s->t[i];
        int type = P(s, i, TP_LTYPE), lx = P(s, i, TP_LX), lx2 = P(s, i, TP_LX2), engine = P(s, i, TP_ENGINE);
        float hz = type == LT_PUMP ? 0.0f : lfo_hz(P(s, i, TP_LSPEED), s->bpm), dph = hz * CHUNK_S;
        float amt = (float)P(s, i, TP_LAMT), pk = 0.0f, vib = 0.0f, vol = 0.0f;
        {   /* the performance pitch: from the last stretch's value to this one's over the chunks (one stretch
             * behind, smooth); a note on the track since: at its value at once */
            float to = (float)t->perf.pitch_c;
            t->pb_base = t->pb_snap ? to : t->pb_last;
            t->pb_step = (to - t->pb_base) / (float)s->nck;
            t->pb_base = to - t->pb_step * (float)s->nck;      /* (the last chunk exactly at the value) */
            t->pb_last = to;
            t->pb_snap = 0;
        }
        t->ldest = LD_NONE;
        t->lscale = 0.0f;
        t->lf_in = t->lf_out = 0.0f;
        if (type == LT_WAVE || type == LT_DRIFT) {
            int d = LD_TP[P(s, i, TP_LDEST) & 3][lx & 3];
            t->ldest = (int16_t)d;
            if (d >= 0) {
                const tp_info_t *in = tp_info(d, engine);
                t->lscale = (float)(in->hi - in->lo) * (1.0f / 127.0f);
            } else if (d == LD_PITCH) {
                t->lscale = 1.0f;
            }
            if (type == LT_DRIFT)
                fade_secs(lx2, &t->lf_in, &t->lf_out);
        } else if (type == LT_WOBBLE) {
            fade_secs(lx, &t->lf_in, &t->lf_out);
            vib = (amt - 64.0f) * (100.0f / 63.0f);                 /* cents */
            vol = ((float)P(s, i, TP_LDEST) - 64.0f) * (1.0f / 63.0f);
        }
        for (k = 0; k < nck; k++) {
            float ph, val = 0.0f, pitch = 0.0f, gain = 1.0f;
            ph = t->lfo_ph + dph;
            if (ph >= 1.0f) {
                ph -= 1.0f;
                t->lfo_sh = frand(&t->rng);      /* a fresh sample on every cycle */
            }
            t->lfo_ph = ph;
            switch (type) {
            case LT_WAVE:
                val = lfo_wave_value(lx2, ph) * (amt - 64.0f) * (127.0f / 63.0f) * t->lscale;
                break;
            case LT_DRIFT: {                    /* a wander: slews to each cycle's sample within a quarter cycle */
                float sm = fm_minf(4.0f * dph, 1.0f);
                t->lfo_sm += (t->lfo_sh - t->lfo_sm) * sm;
                val = (0.5f + 0.5f * t->lfo_sm) * amt * t->lscale;
                break;
            }
            case LT_WOBBLE: {
                float w = lfo_wave_trem(lx2, ph);
                pitch = w * vib;
                gain = vol >= 0.0f ? 1.0f - vol * (0.5f - 0.5f * w) : 1.0f + vol * (0.5f + 0.5f * w);
                break;
            }
            case LT_PUMP: {
                if (t->duck_hold > 0.0f)
                    t->duck_hold -= CHUNK_S;
                else
                    t->duck *= fm_expf(-6.9f * CHUNK_S / env_seconds((float)lx * (1.0f / 127.0f)));
                gain = 1.0f - amt * (1.0f / 127.0f) * t->duck;
                break;
            }
            default: break;
            }
            t->loff[k] = val;
            t->lpitch[k] = pitch;
            t->lgain[k] = gain;
        }
        pk = tpk[i];                             /* the meter: the loudest voice of the track (gathered above) */
        {
            uint32_t q = (uint32_t)(fm_minf(pk, 2.0f) * 16383.5f), old = synth_peak[i];
            old = old - (old >> 4);              /* the fall: about 1/16 a block */
            synth_peak[i] = (uint16_t)(q > old ? q : old);
        }
    }
    for (k = 0; k < NVOICE; k++)
        s->v[k].ck = 0;
    s->pos = (uint16_t)((s->pos + n) % SY_CHUNK);
}

/* ------------------------------------------------------ the character --- */
/* the character stage's chunk values (synth.h CT_*): c.cx[] from the four knobs, the LFO's offset on the
 * amount, and the note's frequency (ring). The state (cz) starts from 0 when the type changes. */
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
static float ch_lp_hz(float t) { return 400.0f * fm_exp2f(t * 5.64385619f); }      /* tone 0..1: 400 Hz .. 20 kHz */
static float ch_pivot_hz(float a) { return 150.0f * fm_exp2f(a * 5.32192809f); }   /* amount 0..1: 150 Hz .. 6 kHz */
static float ch_alpha(float hz) { return hz >= 19000.0f ? 1.0f : 1.0f - fm_expf(-FM_TWO_PI * hz * INV_SR); }
static float ch_tape_hz(float t) { return 1500.0f * fm_exp2f(t * 3.585f); }        /* tone 0..1: 1.5 .. 18 kHz */
static float ch_smp_hz(float t) { return 2000.0f * fm_exp2f(t * 3.322f); }         /* tone 0..1: 2 .. 20 kHz */
static float ch_smp_rate(float a) { return 26040.0f * fm_exp2f(-a * 1.243f); }     /* amount 0..1: 26 .. 11 kHz */
#define CH_DC (FM_TWO_PI * 12.0f * INV_SR)       /* the DC blocker's one-pole: 12 Hz */
#define CH_REF 0.7f                              /* the drive keeps the level of a peak here (an engine's, about) */
#define CH_FOLD 0.6f                             /* the fold's level once it folds (a folded wave's RMS: about a saw's) */
/* the type's values from the amount and tone (0..1) and the note's frequency (ring). tc: the tone as -1..1,
 * 0 at 64 exactly (the page prints it so: the fold's bias, the crush's balance, the tilt) */
static void char_coefs(int type, float a, float t, float freq, float *x)
{
    float tc = fm_clampf((t * 127.0f - 64.0f) * (1.0f / 63.0f), -1.0f, 1.0f);
    switch (type) {
    case CT_DRIVE: {                             /* gain 0.25 .. 32 (-12 .. +30 dB), made up at CH_REF */
        float g = 0.25f * fm_exp2f(a * 7.0f);
        x[0] = g;
        x[1] = CH_REF / sat_c(g * CH_REF) * (1.0f - 0.25f * a);   /* (squarer as it drives: a little less) */
        x[2] = ch_alpha(ch_lp_hz(t));
        break;
    }
    case CT_FOLD: {                              /* sin(pi/2 (g x + bias)): g 0.3 .. 8, the bias +-1 */
        float g = 0.3f * fm_exp2f(a * 4.74f), b = tc;
        x[0] = g * 0.25f;                        /* (in turns) */
        x[1] = fm_maxf(CH_FOLD, 2.0f / (FM_PI * g));   /* small g: linear, unity; large: about the input's level */
        x[3] = b * 0.25f;
        x[4] = fm_sin_turns(b * 0.25f);          /* the output at x = 0: taken off, so silence stays silent */
        x[5] = CH_DC;
        break;
    }
    case CT_CRUSH: {                             /* bits 16 .. 3 and the rate / 1 .. / 45, by the balance */
        float wb = fm_minf(1.0f, 1.0f + tc), wr = fm_minf(1.0f, 1.0f - tc);
        float q = fm_exp2f(15.0f - a * wb * 13.0f);
        x[0] = q;
        x[1] = 1.0f / q;
        x[2] = fm_exp2f(-a * wr * 5.5f);
        break;
    }
    case CT_RING: {                              /* x (1 - d + d sin): the level made up for the depth */
        int k = (int)(t * 127.0f + 0.5f) >> 3;   /* (as tp_text_par prints it) */
        float w = fm_minf(freq * RING_R[k < 0 ? 0 : k > 15 ? 15 : k] * INV_SR, 0.45f) * FM_TWO_PI;
        float m = fm_rsqrtf((1.0f - a) * (1.0f - a) + 0.5f * a * a);
        x[0] = (1.0f - a) * m;
        x[1] = a * m;
        x[2] = fm_cosf(w);                       /* the sine as a rotating phasor: cz[4] + i cz[5] */
        x[3] = fm_sinf(w);
        x[5] = CH_DC;
        break;
    }
    case CT_TAPE: {                              /* the drive's curve, gentler; a two-pole roll-off; the wow's depth */
        float g = 0.5f * fm_exp2f(a * 4.0f);
        x[0] = g;
        x[1] = CH_REF / sat_c(g * CH_REF) * (1.0f - 0.15f * a);
        x[2] = ch_alpha(ch_tape_hz(t));
        x[3] = 2.0f + 8.0f * a;                  /* cents (voice_chunk) */
        break;
    }
    case CT_SAMPLER: {                           /* rate and bits by the amount; the input and output filters */
        float r = ch_smp_rate(a), q = fm_exp2f(13.0f - 3.0f * a);   /* (14 .. 11 bits: a voice sits well under 0 dB) */
        x[0] = q;
        x[1] = 1.0f / q;
        x[2] = r * INV_SR;
        x[3] = ch_alpha(0.45f * r);
        x[4] = ch_alpha(ch_smp_hz(t));
        break;
    }
    case CT_TILT: {                              /* lows x 2^-tilt, highs x 2^tilt about the pivot */
        float tl = tc;
        x[0] = ch_alpha(ch_pivot_hz(a));
        x[1] = fm_exp2f(-tl);
        x[2] = fm_exp2f(tl);
        break;
    }
    default:
        break;
    }
}
static void char_prep(synth_t *s, voice_t *v, int dest, float off)
{
    cfg_t *c = &v->c;
    int trk = v->trk, type = P(s, trk, TP_CTYPE), mix = P(s, trk, TP_CMIX), i;
    if (type >= NCT || mix == 0)
        type = CT_OFF;
    if (type != c->ctype) {
        for (i = 0; i < 8; i++)
            v->cz[i] = 0.0f;
        c->ctype = (uint8_t)type;
    }
    if (type == CT_OFF)
        return;
    c->cx[7] = fm_minf((float)mix, 127.0f) * (1.0f / 127.0f);
    {   /* (the coefficients cost exps: made again only when what they come from moved) */
        float a = fm_minf(pf(s, trk, TP_CAMT, dest, off), 127.0f) * (1.0f / 127.0f);
        float t = fm_minf((float)P(s, trk, TP_CTONE), 127.0f) * (1.0f / 127.0f), f = type == CT_RING ? c->freq : 0.0f;
        if (type != c->cin_type || a != c->cin[0] || t != c->cin[1] || f != c->cin[2]) {
            char_coefs(type, a, t, c->freq, c->cx);
            c->cin_type = (uint8_t)type;
            c->cin[0] = a;
            c->cin[1] = t;
            c->cin[2] = f;
        }
    }
    if (type == CT_RING) {                       /* the phasor back to unit length, once a chunk (or at 1 + 0i) */
        float re = v->cz[4], im = v->cz[5], r2 = re * re + im * im, g = r2 > 0.5f ? fm_rsqrtf(r2) : 0.0f;
        v->cz[4] = g > 0.0f ? re * g : 1.0f;
        v->cz[5] = im * g;
    }
}

/* the Character page's picture (synth.h) */
float char_picture(const uint8_t *par, float u)
{
    int type = par[TP_CTYPE] < NCT ? par[TP_CTYPE] : CT_OFF;
    float a = (float)(par[TP_CAMT] & 127) * (1.0f / 127.0f), t = (float)(par[TP_CTONE] & 127) * (1.0f / 127.0f);
    float wet = (float)(par[TP_CMIX] & 127) * (1.0f / 127.0f), x[8], d = u, y = u;
    char_coefs(type, a, t, 220.0f, x);
    switch (type) {
    case CT_DRIVE: case CT_TAPE: y = sat_c(u * x[0]) * x[1]; break;
    case CT_FOLD: y = (sin_q(u * x[0] + x[3]) - x[4]) * x[1]; break;
    case CT_CRUSH: case CT_SAMPLER: {            /* a cycle of 200 samples, held and quantised */
        float hold = 1.0f / x[2], n = fm_floorf(u * 200.0f / hold) * hold;
        d = 0.9f * fm_sin_turns(u);
        y = fm_floorf(0.9f * fm_sin_turns(n * (1.0f / 200.0f)) * x[0] + 0.5f) * x[1];
        break;
    }
    case CT_RING: {                              /* two cycles of a sine, times the modulator (its peak at 1) */
        int k = (par[TP_CTONE] & 127) >> 3;
        d = fm_sin_turns(2.0f * u);
        y = d * (x[0] + x[1] * fm_sin_turns(2.0f * u * RING_R[k])) / (x[0] + x[1]);
        break;
    }
    case CT_TILT: {                              /* the response, 50 Hz .. 20 kHz: dB / 6 */
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
SY_INLINE float char_one(float d, const int type, float c0, float c1, float c2, float c3, float c4, float c5, float wet,
                         float *lp, float *dc, float mod, int upd)
{
    float y;
    switch (type) {
    case CT_DRIVE:
        y = sat_c(d * c0) * c1;
        *lp += c2 * (y - *lp);
        y = *lp;
        break;
    case CT_FOLD:
        y = (sin_q(d * c0 + c3) - c4) * c1;
        *dc += c5 * (y - *dc);
        y -= *dc;
        break;
    case CT_CRUSH:
        if (upd)
            *lp = fm_floorf(d * c0 + 0.5f) * c1;
        y = *lp;
        break;
    case CT_RING:                                /* (a sine times its own pitch has DC: blocked) */
        y = d * mod;
        *dc += c5 * (y - *dc);
        y -= *dc;
        break;
    case CT_TILT:
        *lp += c0 * (d - *lp);
        y = *lp * c1 + (d - *lp) * c2;
        break;
    case CT_TAPE:                                /* lp, dc: the roll-off's two poles */
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
SY_INLINE void char_loop(voice_t *v, float *xl, float *xr, int k, const int type, const int stereo)
{
    const float *cx = v->c.cx;
    const float c0 = cx[0], c1 = cx[1], c2 = cx[2], c3 = cx[3], c4 = cx[4], c5 = cx[5], wet = cx[7];
    float z0 = v->cz[0], z1 = v->cz[1], z2 = v->cz[2], z3 = v->cz[3], ph = v->cz[4], qi = v->cz[5];
    int j;
    if (type == CT_SAMPLER) {                    /* z0 / z1 the input filter, z2 / z3 the held sample, z6 / z7 the output's */
        float z6 = v->cz[6], z7 = v->cz[7];
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
        v->cz[0] = fm_flush(z0);
        v->cz[1] = fm_flush(z1);
        v->cz[2] = z2;
        v->cz[3] = z3;
        v->cz[4] = ph;
        v->cz[6] = fm_flush(z6);
        v->cz[7] = fm_flush(z7);
        return;
    }
    for (j = 0; j < k; j++) {
        float mod = 0.0f;
        int upd = 0;
        if (type == CT_RING) {                   /* (ph, qi): the phasor, turned a step */
            float nr = ph * c2 - qi * c3;
            qi = ph * c3 + qi * c2;
            ph = nr;
            mod = c0 + c1 * qi;
        } else if (type == CT_CRUSH) {
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
    v->cz[0] = fm_flush(z0);
    v->cz[1] = fm_flush(z1);
    v->cz[2] = fm_flush(z2);
    v->cz[3] = fm_flush(z3);
    v->cz[4] = ph;
    v->cz[5] = qi;
}
static void char_run(voice_t *v, float *xl, float *xr, int k)
{
    switch (v->c.ctype * 2 + (v->c.stereo != 0)) {
    case CT_DRIVE * 2: char_loop(v, xl, xr, k, CT_DRIVE, 0); break;
    case CT_DRIVE * 2 + 1: char_loop(v, xl, xr, k, CT_DRIVE, 1); break;
    case CT_FOLD * 2: char_loop(v, xl, xr, k, CT_FOLD, 0); break;
    case CT_FOLD * 2 + 1: char_loop(v, xl, xr, k, CT_FOLD, 1); break;
    case CT_CRUSH * 2: char_loop(v, xl, xr, k, CT_CRUSH, 0); break;
    case CT_CRUSH * 2 + 1: char_loop(v, xl, xr, k, CT_CRUSH, 1); break;
    case CT_RING * 2: char_loop(v, xl, xr, k, CT_RING, 0); break;
    case CT_RING * 2 + 1: char_loop(v, xl, xr, k, CT_RING, 1); break;
    case CT_TILT * 2: char_loop(v, xl, xr, k, CT_TILT, 0); break;
    case CT_TILT * 2 + 1: char_loop(v, xl, xr, k, CT_TILT, 1); break;
    case CT_TAPE * 2: char_loop(v, xl, xr, k, CT_TAPE, 0); break;
    case CT_TAPE * 2 + 1: char_loop(v, xl, xr, k, CT_TAPE, 1); break;
    case CT_SAMPLER * 2: char_loop(v, xl, xr, k, CT_SAMPLER, 0); break;
    case CT_SAMPLER * 2 + 1: char_loop(v, xl, xr, k, CT_SAMPLER, 1); break;
    default: break;
    }
}

/* ------------------------------------------------------------ the chunk --- */
static void voice_pitch(synth_t *s, voice_t *v)
{
    v->c.freq = 440.0f * fm_exp2f((v->pitch_cur - 69.0f + v->c.pitch_off * 0.01f) * (1.0f / 12.0f));
    if (v->c.freq > 18000.0f)
        v->c.freq = 18000.0f;
    eng_prep(v, s->bpm);
}

/* a chunk boundary: the parameters into c */
static void voice_chunk(synth_t *s, voice_t *v)
{
    trk_t *t = &s->t[v->trk];
    cfg_t *c = &v->c;
    int trk = v->trk, k = v->ck < s->nck ? v->ck : s->nck - 1, dest = t->ldest, i;
    float lenv = 1.0f, off, lg, vol, lev, pan, vg, res;
    v->ck = (uint8_t)(v->ck + 1);
    v->nchunk = (uint8_t)(v->nchunk + 1);
    if (!v_sounding(v))
        return;
    /* the LFO's fade envelope: this voice's time since its note */
    if (t->lf_in > 0.0f)
        lenv = fm_minf(v->tsec / t->lf_in, 1.0f);
    else if (t->lf_out > 0.0f)
        lenv = fm_maxf(1.0f - v->tsec / t->lf_out, 0.0f);
    v->tsec += CHUNK_S;
    off = t->loff[k] * lenv;
    lg = 1.0f - (1.0f - t->lgain[k]) * lenv;
    {
        float e0 = c->e[0];
        for (i = 0; i < 4; i++) {
            c->ek[i] = fm_minf(pf(s, trk, TP_E1 + i, dest, off), 127.0f);   /* a foreign lock value stays in range */
            c->e[i] = c->ek[i] * (1.0f / 127.0f);
        }
        if ((v->engine == EG_ORBIT || v->engine == EG_TINES) && !v->fresh)   /* bright, an FM index: slewed (~10 ms) */
            c->e[0] = e0 + (c->e[0] - e0) * BRIGHT_SLEW;
    }
    {   /* the envelopes' coefficients cost an exp each: only for a knob that moved */
        static const uint8_t EP[8] = {TP_A, TP_D, TP_S, TP_R, TP_FA, TP_FD, TP_FS, TP_FR};
        float *co[8];
        co[0] = &c->amp_ar; co[1] = &c->amp_dc; co[2] = &c->amp_sl; co[3] = &c->amp_rc;
        co[4] = &c->flt_ar; co[5] = &c->flt_dc; co[6] = &c->flt_sl; co[7] = &c->flt_rc;
        for (i = 0; i < 8; i++) {
            float x = pf(s, trk, EP[i], dest, off) * (1.0f / 127.0f);
            if (x == v->env_in[i])
                continue;
            v->env_in[i] = x;
            *co[i] = (i & 3) == 0 ? env_rate(env_seconds(x)) : (i & 3) == 2 ? x : env_coef(env_seconds(x));
        }
    }
    /* the filter: 50 Hz .. 20 kHz over the knob, the performance offset, keytrack, velocity */
    c->cut_oct = (pf(s, trk, TP_CUT, dest, off) + (float)t->perf.cut) * (8.64385619f / 127.0f)
                 + pf(s, trk, TP_KTRK, dest, off) * (1.0f / 127.0f) * (v->pitch - 60.0f) * (1.0f / 12.0f);
    c->fenv_oct = (pf(s, trk, TP_FENV, dest, off) - 64.0f) * (6.0f / 64.0f) * (0.5f + 0.5f * v->vel);
    res = pf(s, trk, TP_RES, dest, off) * (1.0f / 127.0f);
    c->kq = 2.0f * fm_exp2f(-res * 4.3f);        /* Q 0.5 .. 10 */
    c->kres = res * 4.0f;                        /* the ladder's feedback */
    c->ftype = (uint8_t)P(s, trk, TP_FTYPE);
    if (c->ftype >= NFT)
        c->ftype = FT_LP24;
    c->mute = t->perf.mute;
#ifdef PLAT_HOST
    if (!(synth_host_solo >> trk & 1u))          /* the host's stems (LOOM_SOLO): a track left out is muted */
        c->mute = 1;
#endif
    /* gains */
    vol = pf(s, trk, TP_VOLUME, dest, off) * (1.0f / 127.0f);
    lev = tp_level_gain(P(s, trk, TP_LEVEL)) * (100.0f / 127.0f);
    vg = (0.25f + 0.75f * v->vel * v->vel) * vol * vol * lev * lg * (1.0f - 0.6f * res);
    pan = fm_clampf((pf(s, trk, TP_PAN, dest, off) - 64.0f + (float)t->perf.pan) * (1.0f / 64.0f), -1.0f, 1.0f);
    if (pan != v->pan_in || v->fresh) {          /* the pan's cos / sin: only when it moved */
        v->pan_in = pan;
        v->pan_l = pan == 0.0f ? 0.70710678f : fm_cosf((pan + 1.0f) * 0.78539816f);   /* centre: exactly equal */
        v->pan_r = pan == 0.0f ? 0.70710678f : fm_sinf((pan + 1.0f) * 0.78539816f);
    }
    c->gl = v->pan_l * vg;
    c->gr = v->pan_r * vg;
    c->g1 = fm_minf((float)(P(s, trk, TP_SEND1) + t->perf.send1), 127.0f) * (1.0f / 127.0f);
    c->g2 = fm_minf((float)(P(s, trk, TP_SEND2) + t->perf.send2), 127.0f) * (1.0f / 127.0f);
    /* pitch: the glide, bend, performance pitch, vibrato, an LFO on the pitch (+-12 semitones at 100 %) */
    v->pitch_cur += (v->pitch - v->pitch_cur) * v->glide_k;
    c->pitch_off = (float)t->bend_c + t->pb_base + t->pb_step * (float)(k + 1) + t->lpitch[k] * lenv;
    if (dest == LD_PITCH)
        c->pitch_off += off * (1200.0f / 127.0f);
    {   /* the sound's octave (Play 2) */
        int oc = P(s, trk, TP_OCT);
        c->pitch_off += (float)((oc > 4 ? 4 : oc) - 2) * 1200.0f;
    }
    if (c->ctype == CT_TAPE) {                   /* the tape's wow and flutter (char_coefs: cx[3] cents) */
        v->cz[4] = wrap1(v->cz[4] + 0.55f * (float)SY_CHUNK * INV_SR);
        v->cz[5] = wrap1(v->cz[5] + 6.1f * (float)SY_CHUNK * INV_SR);
        c->pitch_off += c->cx[3] * c->cx[7] * (fm_sin_turns(v->cz[4]) + 0.25f * fm_sin_turns(v->cz[5]));
    }
    {   /* the frequency and the engine's increments: when the pitch or a knob moved (or every 4th
         * chunk: the organ's pitch wobble and phasor trim); brocade's sub: when it starts or its offset moved */
        float pin = v->pitch_cur + c->pitch_off * 0.01f;
        int moved = pin != v->pitch_in || v->fresh || (v->nchunk & 3) == 0;
        if (v->engine == EG_ORBIT) {             /* its second page (orbit_prep, orbit_chunk) */
            int fl = P(s, trk, TP_X1), fo = P(s, trk, TP_X2), ks = P(s, trk, TP_X3);
            float fall = fl ? fm_expf(-(float)SY_CHUNK / (0.02f * fm_exp2f((float)fl * (7.64f / 127.0f)) * SY_SR)) : 0.0f;
            float fv = (float)fo * (1.0f / 127.0f), kv = (float)ks * (1.0f / 127.0f);
            moved |= fall != c->ex[8] || kv != c->ex[10];
            c->ex[8] = fall;
            c->ex[9] = fv;
            c->ex[10] = kv;
        }
        if (v->engine == EG_BROCADE) {
            float lv = (float)P(s, trk, TP_X1) * (1.0f / 127.0f);
            float st = fm_clampf((float)P(s, trk, TP_X2) - 64.0f, -24.0f, -12.0f);
            moved |= (lv > 0.0f && c->ex[20] <= 0.0f) || st != c->ex[21];
            c->ex[20] = fm_minf(lv, 1.0f);
            c->ex[21] = st;
        }
        for (i = 0; i < 4 && !moved; i++)
            moved = c->e[i] != v->e_in[i];
        if (moved) {
            v->pitch_in = pin;
            for (i = 0; i < 4; i++)
                v->e_in[i] = c->e[i];
            voice_pitch(s, v);
        }
    }
    if (v->engine == EG_BROCADE)                 /* the saws to sound: the knob's, under the guard's cap */
        c->ex[14] = c->ex[2];
    eng_chunk(v);
    char_prep(s, v, dest, off);
}

/* the steal's turn: the old note has faded; start the pending one here, mid-chunk */
static void voice_steal(synth_t *s, voice_t *v)
{
    int held = v->pend_held;
    v->amp.stage = ENV_OFF;                      /* so voice_start sees a fresh voice */
    v->amp.level = v->flt.level = 0.0f;
    v->steal_phase = 2;
    v->amp_cur = 0.0f;
    voice_start(s, v, v->pend_note, v->pend_vel, v->pend_glide, v->pend_tag, held, 1);
    v->pitch_cur = v->pitch;                     /* a stolen voice does not glide from the old note */
    if (!held)
        voice_release(v);
    voice_config_now(s, v);
}

/* a sub-block boundary (SUB samples, aligned in the chunk, or a voice configured mid-block): the
 * envelopes step, the filter's coefficients follow the filter envelope, the ramps (amp level, steal
 * fade, dry gains) aim at their next values. Between boundaries only the per-sample state moves. */
static void voice_subblock(synth_t *s, voice_t *v, int aligned)
{
    cfg_t *c;
    float hz, g, gl, gr;
    const float hk = 1.41421356f;
    if (v->steal_phase == 1 && v->steal_gain <= 0.0f) {   /* the old note has faded: the new one starts */
        int i;
        voice_steal(s, v);
        for (i = 0; i < 8; i++)
            v->f[i] = 0.0f;
        for (i = 0; i < 8; i++)
            v->cz[i] = 0.0f;
    }
    c = &v->c;
    v->fresh = 0;
    if (aligned) {
        env_step(&v->amp, c->amp_ar, c->amp_dc, c->amp_sl, c->amp_rc);
        env_step(&v->flt, c->flt_ar, c->flt_dc, c->flt_sl, c->flt_rc);
    }
    if (c->cut_oct == v->fco && v->flt.level == v->flv && c->fenv_oct == v->ffe && v->fhz > 0.0f) {
        hz = v->fhz;
    } else {
        hz = 50.0f * fm_exp2f(c->cut_oct + v->flt.level * c->fenv_oct);
        hz = hz > 20000.0f ? 20000.0f : hz < 50.0f ? 50.0f : hz;
        v->fco = c->cut_oct;
        v->flv = v->flt.level;
        v->ffe = c->fenv_oct;
    }
    if (hz == v->fhz && c->kq == v->fkq && c->kres == v->fkres && c->ftype == v->fty) {
        /* the same as the last sub-block's (a settled envelope, no LFO on the cutoff): the coefficients stand */
    } else if (c->ftype == FT_LADDER) {          /* four one-poles: g per pole, k the feedback */
        v->b1 = fm_minf(1.0f - fm_expf(-FM_TWO_PI * hz * INV_SR), 0.9f);
        v->b2 = c->kres;
    } else {
        g = fm_tanf(FM_PI * fm_minf(hz, 0.45f * SY_SR) * INV_SR);
        v->a1 = 1.0f / (1.0f + g * (g + c->kq));
        v->a2 = g * v->a1;
        v->a3 = g * v->a2;
        if (c->ftype == FT_LP24) {
            v->b1 = 1.0f / (1.0f + g * (g + hk));
            v->b2 = g * v->b1;
            v->b3 = g * v->b2;
        }
    }
    v->fhz = hz;
    v->fkq = c->kq;
    v->fkres = c->kres;
    v->fty = c->ftype;
    v->amp_step = (v->amp.level - v->amp_cur) * (1.0f / (float)SUB);
    if (v->steal_phase == 1) {
        v->sg_step = v->steal_gain > STEAL_RATE * (float)SUB ? -STEAL_RATE : -v->steal_gain * (1.0f / (float)SUB);
    } else if (v->steal_phase == 2) {
        if (v->steal_gain + STEAL_RATE * (float)SUB >= 1.0f) {
            v->sg_step = (1.0f - v->steal_gain) * (1.0f / (float)SUB);
            v->steal_phase = 0;                  /* reaches 1 at the end of this block */
        } else {
            v->sg_step = STEAL_RATE;
        }
    } else {
        v->sg_step = 0.0f;
    }
    gl = c->mute ? 0.0f : c->gl;
    gr = c->mute ? 0.0f : c->gr;
    v->gl_step = (gl - v->glc) * (0.35f / (float)SUB);
    v->gr_step = (gr - v->grc) * (0.35f / (float)SUB);
}

/* the sample loop for one sub-block: the filter (one of six), the ramps, the outputs. Instantiated
 * for each filter type and for mono / stereo with constant arguments, so the compiler folds the
 * branches away; the generic switch is in voice_samples. chr: the filter only, its output back into
 * xl / xr for the character stage (char_run), then out_loop does the rest.
 *   lp24:    a TPT state-variable low-pass (resonant) into a second, flat one: 24 dB, aggressive
 *   ladder:  four one-poles in series with the last one's output fed back through a soft clip
 *   lp12:    the one resonant TPT stage, 12 dB
 *   hp12:    its high-pass output, 12 dB
 *   bp12:    its band-pass output, scaled by 1 / Q so the peak stays at unity
 *   dynamic: the drum tracks' filter (drumline.c): on a synth voice it is a bypass */
SY_INLINE void sample_loop(voice_t *v, float *xl, float *xr, float *l, float *r, float *s1, float *s2,
                               int k, const int ftype, const int stereo, const int chr)
{
    const cfg_t *c = &v->c;
    const float a1 = v->a1, a2 = v->a2, a3 = v->a3, b1 = v->b1, b2 = v->b2, b3 = v->b3, kq = c->kq;
    const float es = v->amp_step, sgs = v->sg_step, gls = v->gl_step, grs = v->gr_step;
    const float g1 = c->g1, g2 = c->g2, wide = stereo ? 1.41421356f : 1.0f;
    const float lcomp = 1.0f + b2 * 0.25f;       /* the ladder: its passband falls as the feedback rises */
    const int sends = g1 > 0.0f || g2 > 0.0f;
    float f0 = v->f[0], f1 = v->f[1], f2 = v->f[2], f3 = v->f[3], f4 = v->f[4], f5 = v->f[5], f6 = v->f[6], f7 = v->f[7];
    float e = v->amp_cur, sg = v->steal_gain, glc = v->glc, grc = v->grc, pk = 0.0f;
    int j;
    for (j = 0; j < k; j++) {
        float yl, yr, ol, or_, a;
        if (ftype == FT_LADDER) {
            float x = xl[j] - b2 * soft3(f3);
            f0 += b1 * (x - f0);
            f1 += b1 * (f0 - f1);
            f2 += b1 * (f1 - f2);
            f3 += b1 * (f2 - f3);
            yl = f3 * lcomp;
        } else if (ftype == FT_DYNAMIC) {
            yl = xl[j];
        } else {
            float t3 = xl[j] - f1, t1 = a1 * f0 + a2 * t3, t2 = f1 + a2 * f0 + a3 * t3;
            f0 = 2.0f * t1 - f0;
            f1 = 2.0f * t2 - f1;
            yl = ftype == FT_HP12 ? xl[j] - kq * t1 - t2 : ftype == FT_BP12 ? kq * t1 : t2;
            if (ftype == FT_LP24) {
                float w3 = yl - f3, w1 = b1 * f2 + b2 * w3, w2 = f3 + b2 * f2 + b3 * w3;
                f2 = 2.0f * w1 - f2;
                f3 = 2.0f * w2 - f3;
                yl = w2;
            }
        }
        if (stereo) {
            if (ftype == FT_LADDER) {
                float x = xr[j] - b2 * soft3(f7);
                f4 += b1 * (x - f4);
                f5 += b1 * (f4 - f5);
                f6 += b1 * (f5 - f6);
                f7 += b1 * (f6 - f7);
                yr = f7 * lcomp;
            } else if (ftype == FT_DYNAMIC) {
                yr = xr[j];
            } else {
                float u3 = xr[j] - f5, u1 = a1 * f4 + a2 * u3, u2 = f5 + a2 * f4 + a3 * u3;
                f4 = 2.0f * u1 - f4;
                f5 = 2.0f * u2 - f5;
                yr = ftype == FT_HP12 ? xr[j] - kq * u1 - u2 : ftype == FT_BP12 ? kq * u1 : u2;
                if (ftype == FT_LP24) {
                    float w3 = yr - f7, w1 = b1 * f6 + b2 * w3, w2 = f7 + b2 * f6 + b3 * w3;
                    f6 = 2.0f * w1 - f6;
                    f7 = 2.0f * w2 - f7;
                    yr = w2;
                }
            }
        } else {
            yr = yl;
        }
        if (chr) {
            xl[j] = yl;
            if (stereo)
                xr[j] = yr;
            continue;
        }
        e += es;
        sg += sgs;
        glc += gls;
        grc += grs;
        a = e * sg * wide;
        ol = yl * a * glc;
        or_ = yr * a * grc;
        l[j] += ol;
        r[j] += or_;
        if (sends) {
            float mono = (ol + or_) * 0.5f;
            s1[j] += mono * g1;
            s2[j] += mono * g2;
        }
        yl = fm_fabsf(yl);
        if (yl > pk)
            pk = yl;
    }
    v->amp_cur = e;
    v->steal_gain = sg;
    v->glc = glc;
    v->grc = grc;
    pk *= e * sg * wide * (glc > grc ? glc : grc);   /* the meter: this block's loudest, roughly */
    if (pk > v->peak)
        v->peak = pk;
    v->f[0] = fm_flush(f0); v->f[1] = fm_flush(f1); v->f[2] = fm_flush(f2); v->f[3] = fm_flush(f3);
    if (stereo) {
        v->f[4] = fm_flush(f4); v->f[5] = fm_flush(f5); v->f[6] = fm_flush(f6); v->f[7] = fm_flush(f7);
    }
}

/* a stereo voice's filter, one channel at a time (ch 0: f[0..3], 1: f[4..7]), its output back into x: the same
 * arithmetic as sample_loop's, with half the state live at once (a fused stereo loop held both channels' states,
 * the coefficients and the ramps, more than the FM-1's registers: it spilled) */
SY_INLINE void flt_loop(voice_t *v, float *x, int k, const int ftype, const int ch)
{
    const float a1 = v->a1, a2 = v->a2, a3 = v->a3, b1 = v->b1, b2 = v->b2, b3 = v->b3, kq = v->c.kq;
    const float lcomp = 1.0f + b2 * 0.25f;
    float *f = v->f + 4 * ch;
    float f0 = f[0], f1 = f[1], f2 = f[2], f3 = f[3];
    int j;
    for (j = 0; j < k; j++) {
        float y;
        if (ftype == FT_LADDER) {
            float u = x[j] - b2 * soft3(f3);
            f0 += b1 * (u - f0);
            f1 += b1 * (f0 - f1);
            f2 += b1 * (f1 - f2);
            f3 += b1 * (f2 - f3);
            y = f3 * lcomp;
        } else {
            float t3 = x[j] - f1, t1 = a1 * f0 + a2 * t3, t2 = f1 + a2 * f0 + a3 * t3;
            f0 = 2.0f * t1 - f0;
            f1 = 2.0f * t2 - f1;
            y = ftype == FT_HP12 ? x[j] - kq * t1 - t2 : ftype == FT_BP12 ? kq * t1 : t2;
            if (ftype == FT_LP24) {
                float w3 = y - f3, w1 = b1 * f2 + b2 * w3, w2 = f3 + b2 * f2 + b3 * w3;
                f2 = 2.0f * w1 - f2;
                f3 = 2.0f * w2 - f3;
                y = w2;
            }
        }
        x[j] = y;
    }
    f[0] = fm_flush(f0);
    f[1] = fm_flush(f1);
    f[2] = fm_flush(f2);
    f[3] = fm_flush(f3);
}
static void flt_channel(voice_t *v, float *x, int k, int ch)
{
    switch (v->c.ftype) {
    case FT_LP24: flt_loop(v, x, k, FT_LP24, ch); break;
    case FT_LADDER: flt_loop(v, x, k, FT_LADDER, ch); break;
    case FT_LP12: flt_loop(v, x, k, FT_LP12, ch); break;
    case FT_HP12: flt_loop(v, x, k, FT_HP12, ch); break;
    case FT_BP12: flt_loop(v, x, k, FT_BP12, ch); break;
    default: break;                              /* (dynamic: a bypass) */
    }
}

/* after the character stage: the ramps and the outputs (sample_loop's second half) */
SY_INLINE void out_loop(voice_t *v, const float *xl, const float *xr, float *l, float *r, float *s1, float *s2,
                        int k, const int stereo)
{
    const cfg_t *c = &v->c;
    const float es = v->amp_step, sgs = v->sg_step, gls = v->gl_step, grs = v->gr_step;
    const float g1 = c->g1, g2 = c->g2, wide = stereo ? 1.41421356f : 1.0f;
    const int sends = g1 > 0.0f || g2 > 0.0f;
    float e = v->amp_cur, sg = v->steal_gain, glc = v->glc, grc = v->grc, pk = 0.0f;
    int j;
    for (j = 0; j < k; j++) {
        float yl = xl[j], yr = stereo ? xr[j] : yl, ol, or_, a;
        e += es;
        sg += sgs;
        glc += gls;
        grc += grs;
        a = e * sg * wide;
        ol = yl * a * glc;
        or_ = yr * a * grc;
        l[j] += ol;
        r[j] += or_;
        if (sends) {
            float mono = (ol + or_) * 0.5f;
            s1[j] += mono * g1;
            s2[j] += mono * g2;
        }
        yl = fm_fabsf(yl);
        if (yl > pk)
            pk = yl;
    }
    v->amp_cur = e;
    v->steal_gain = sg;
    v->glc = glc;
    v->grc = grc;
    pk *= e * sg * wide * (glc > grc ? glc : grc);
    if (pk > v->peak)
        v->peak = pk;
}
static void out_mono(voice_t *v, const float *xl, float *l, float *r, float *s1, float *s2, int k)
{
    out_loop(v, xl, xl, l, r, s1, s2, k, 0);
}
static void out_stereo(voice_t *v, const float *xl, const float *xr, float *l, float *r, float *s1, float *s2,
                       int k)
{
    out_loop(v, xl, xr, l, r, s1, s2, k, 1);
}

/* m samples (inside a chunk) of one voice into the buffers */
static void voice_samples(synth_t *s, voice_t *v, float *l, float *r, float *s1, float *s2, int m)
{
    float xl[SUB], xr[SUB];
    int i = 0;
    while (i < m) {
        int k = SUB - (v->pos & (SUB - 1));
        if (k > m - i)
            k = m - i;
        if ((v->pos & (SUB - 1)) == 0 || v->fresh)
            voice_subblock(s, v, (v->pos & (SUB - 1)) == 0);
        eng_run(v, xl, xr, k);
#define SL(ft, ch) sample_loop(v, xl, xr, l + i, r + i, s1 + i, s2 + i, k, ft, 0, ch)
        if (v->c.stereo) {                       /* the filter a channel at a time, the character, the outputs */
            flt_channel(v, xl, k, 0);
            flt_channel(v, xr, k, 1);
            if (v->c.ctype != CT_OFF)
                char_run(v, xl, xr, k);
            out_stereo(v, xl, xr, l + i, r + i, s1 + i, s2 + i, k);
        } else if (v->c.ctype == CT_OFF) {
            switch (v->c.ftype) {
            case FT_LP24: SL(FT_LP24, 0); break;
            case FT_LADDER: SL(FT_LADDER, 0); break;
            case FT_LP12: SL(FT_LP12, 0); break;
            case FT_HP12: SL(FT_HP12, 0); break;
            case FT_BP12: SL(FT_BP12, 0); break;
            default: SL(FT_DYNAMIC, 0); break;
            }
        } else {                                 /* the filter, the character stage, then the outputs */
            switch (v->c.ftype) {
            case FT_LP24: SL(FT_LP24, 1); break;
            case FT_LADDER: SL(FT_LADDER, 1); break;
            case FT_LP12: SL(FT_LP12, 1); break;
            case FT_HP12: SL(FT_HP12, 1); break;
            case FT_BP12: SL(FT_BP12, 1); break;
            default: break;                      /* (dynamic: a bypass, xl / xr as they are) */
            }
            char_run(v, xl, xr, k);
            out_mono(v, xl, l + i, r + i, s1 + i, s2 + i, k);
        }
#undef SL
        i += k;
        v->pos = (uint16_t)(v->pos + k);
    }
}

void synth_render(synth_t *s, int v0, int v1, float *l, float *r, float *s1, float *s2, uint32_t n)
{
    synth_render_on(s, v0 == 0 && v1 < NVOICE, v0, v1, l, r, s1, s2, n);   /* (the second core's: [0, k), k < 16) */
}

void synth_render_on(synth_t *s, int core, int v0, int v1, float *l, float *r, float *s1, float *s2, uint32_t n)
{
    int vi;
    if (v0 < 0)
        v0 = 0;
    if (v1 > NVOICE)
        v1 = NVOICE;
    uint32_t (*clk)(void) = synth_clock;
    volatile uint32_t *prof = synth_prof[core != 0];
    int8_t ord[NVOICE];
    int no = 0, oi;
    for (vi = v0; vi < v1; vi++) {               /* on the FM-1 the voices grouped by engine (each engine's code
                                                  * fetched from the flash once, then reused from the cache by the
                                                  * next); the simulator keeps their order: its sums the same split
                                                  * or whole (synth_test) */
        int j = no++;
#ifndef PLAT_HOST
        int e = s->v[vi].engine;
        while (j > 0 && s->v[ord[j - 1]].engine > e) {
            ord[j] = ord[j - 1];
            j--;
        }
#endif
        ord[j] = (int8_t)vi;
    }
    for (oi = 0; oi < no; oi++) {
        voice_t *v = &s->v[vi = ord[oi]];
        uint32_t i = 0, t0 = 0, tc = 0, tk = 0;
        const int timed = clk && (v_sounding(v) || v->pos == 0);
        if (timed) {
            t0 = clk();
            synth_vinfo[vi] = (uint8_t)((synth_vinfo[vi] & 32u) | (v->engine & 31u) | (v->fresh & 2 ? 32u : 0u) |
                                        (prof == synth_prof[1] ? 64u : 0u) | 128u);
        }
        if (v->fresh & 2) {                      /* a note started since the last render: its parameters now, from
                                                  * this stretch's first slot (at a chunk's start the chunk's own
                                                  * configuration below is the first) */
            v->fresh &= (uint8_t)~2u;
            if (v->pos != 0) {
                uint8_t ck = v->ck;
                if (timed)
                    tk = clk();
                voice_chunk(s, v);
                if (timed)
                    tc += clk() - tk;
                v->ck = ck;
                v->nchunk = (uint8_t)(v->nchunk - 1);
            }
        }
        while (i < n) {
            uint32_t m = SY_CHUNK - v->pos;
            if (m > n - i)
                m = n - i;
            if (v->pos == 0) {
                if (timed)
                    tk = clk();
                voice_chunk(s, v);
                if (timed)
                    tc += clk() - tk;
            }
            if (v_sounding(v))
                voice_samples(s, v, l + i, r + i, s1 + i, s2 + i, (int)m);
            else
                v->pos = (uint16_t)(v->pos + m);
            if (v->pos >= SY_CHUNK)
                v->pos = 0;
            i += m;
        }
        if (timed) {
            tk = clk() - t0;
            synth_vblk[vi] += tk;
            if (n >= 32u && !(synth_vinfo[vi] & 32u) && v_sounding(v)) {   /* the split's costs: a running mean of
                                                                          * the voice's time a frame, not its first
                                                                          * render's (its set-up) */
                float x = (float)tk / (float)n;
                v->cost_m = v->cost_m > 0.0f ? v->cost_m + (x - v->cost_m) * 0.25f : x;
                s->t[v->trk < NTRK ? v->trk : 0].cost_m = v->cost_m;
            }
            prof[NENGINE] += tc;
            prof[v->engine < NENGINE ? v->engine : 0] += tk - tc;
            if (tk > synth_vworst[0]) {
                synth_vworst[0] = tk;
                synth_vworst[1] = v->engine;
                synth_vworst[2] = n;
            }
        }
    }
}

int synth_sounding(const synth_t *s, int v0, int v1)
{
    int i;
    for (i = v0 < 0 ? 0 : v0; i < v1 && i < NVOICE; i++)
        if (v_sounding(&s->v[i]))
            return 1;
    return 0;
}
int synth_slice_now(const synth_t *s, int trk, int *slice, int32_t *pos)
{
    int i, best = -1;
    for (i = 0; i < NVOICE; i++) {
        const voice_t *v = &s->v[i];
        if (v->trk != trk || v->engine != EG_SLICE || v->amp.stage == ENV_OFF || v->steal_phase == 1 ||
            v->o[4] < 0.5f || v->o[3] > 0.5f)
            continue;
        if (best < 0 || v->age > s->v[best].age)
            best = i;
    }
    if (best < 0)
        return 0;
    *slice = (int)s->v[best].o[2];
    *pos = (int32_t)s->v[best].o[0];
    return 1;
}
int synth_voices_used(const synth_t *s)
{
    int i, n = 0;
    for (i = 0; i < NVOICE; i++)
        n += v_sounding(&s->v[i]);
    return n;
}

/* ------------------------------------------------------- parameter info --- */
static const tp_info_t FIXED[NTP_SOUND + 13] = {
    {"engine", 0, NENGINE - 1, 0, PK_NAMES, EG_NAME}, {"", 0, 127, 0, PK_NN, 0}, {"", 0, 127, 0, PK_NN, 0},
    {"", 0, 127, 0, PK_NN, 0},                          {"", 0, 127, 0, PK_NN, 0},
    {"mode", 0, 2, VM_POLY, PK_NAMES, VMODE_N},         {"porta", 0, 127, 0, PK_NUM, 0},
    {"bend", 0, 8, 2, PK_NAMES, BEND_N},                {"volume", 0, 127, 100, PK_PCT, 0},
    {"attack", 0, 127, 0, PK_MS, 0},                    {"decay", 0, 127, 60, PK_MS, 0},
    {"sustain", 0, 127, 100, PK_PCT, 0},                {"release", 0, 127, 40, PK_MS, 0},
    {"attack", 0, 127, 0, PK_MS, 0},                    {"decay", 0, 127, 60, PK_MS, 0},
    {"sustain", 0, 127, 0, PK_PCT, 0},                  {"release", 0, 127, 40, PK_MS, 0},
    {"cutoff", 0, 127, 110, PK_HZ, 0},                  {"reso", 0, 127, 20, PK_PCT, 0},
    {"env", 0, 127, 64, PK_BIP, 0},                     {"keytrack", 0, 127, 64, PK_PCT, 0},
    {"type", 0, NFT - 1, FT_LP24, PK_NAMES, FTYPE_N},    {"fx I", 0, 127, 0, PK_PCT, 0},
    {"fx II", 0, 127, 0, PK_PCT, 0},
    {"type", 0, NLT - 1, LT_OFF, PK_NAMES, LTYPE_N},    {"speed", 0, 127, 3, PK_HZ_SYNC, 0},
    {"amount", 0, 127, 64, PK_BIP, 0},                  {"dest", 0, NLD - 1, LD_FILTER, PK_NAMES, LDEST_N},
    {"param", 0, 3, 0, PK_NUM, 0},                      {"shape", 0, 3, 1, PK_NAMES, VSHAPE_N},
    /* the character stage: its amount and tone print per type (tp_text_par) */
    {"type", 0, NCT - 1, CT_OFF, PK_NAMES, CTYPE_N},    {"amount", 0, 127, 64, PK_PCT, 0},
    {"tone", 0, 127, 64, PK_BIP, 0},                    {"mix", 0, 127, 127, PK_PCT, 0},
    /* TP_X1 .. TP_X4: the engine's second page: none but an engine's own (tp_info: brocade's BR_SUB_P) */
    {"-", 0, 0, 0, PK_NUM, 0}, {"-", 0, 0, 0, PK_NUM, 0}, {"-", 0, 0, 0, PK_NUM, 0}, {"-", 0, 0, 0, PK_NUM, 0},
    {"octave", 0, 4, 2, PK_NAMES, OCT_N},                /* TP_OCT */
    /* TP_PLAYER .. TP_CHAN */
    {"player", 0, NPL - 1, PL_OFF, PK_NAMES, PLAYER_N}, {"speed", 0, 8, 4, PK_NAMES, ARPSPD_N},
    {"pattern", 0, 5, 0, PK_NAMES, ARPPAT_N},           {"range", 1, 4, 1, PK_NUM, 0},
    {"hold", 0, 1, 0, PK_SWITCH, 0},                    {"length", 0, 2, 1, PK_NAMES, ARPLEN_N},
    {"style", 0, 5, 0, PK_NAMES, ARPSTY_N},             {"glide", 0, 2, 0, PK_NAMES, ARPGLD_N},
    {"stereo", 0, 2, 0, PK_NAMES, ARPSTE_N},            {"level", 0, 127, 100, PK_FADER, 0},
    {"pan", 0, 127, 64, PK_BIP, 0},                     {"follow", 0, 2, 1, PK_NAMES, BRAIN_N},
    {"channel", 0, 16, 0, PK_NUM, 0}};
/* a drumline key's values (synth.h DP_*); its model's knobs take their names and defaults from drumline.c
 * (tp_info_par). The key's envelope and filter: its Envelope and Filter pages */
static const tp_info_t DRUMP[DP_N] = {{"model", 0, NDM - 1, 0, PK_NAMES, DM_NAME}, {"knob 1", 0, 127, 64, PK_NN, 0},
                                      {"knob 2", 0, 127, 64, PK_NN, 0}, {"knob 3", 0, 127, 64, PK_NN, 0},
                                      {"knob 4", 0, 127, 64, PK_NN, 0}, {"random", 0, 127, 0, PK_PCT, 0},
                                      {"pan", 0, 127, 64, PK_BIP, 0}, {"volume", 0, 127, 76, PK_DB, 0},
                                      {"mode", 0, NPM - 1, PM_ONESHOT, PK_NAMES, PM_NAME},
                                      {"attack", 0, 127, 0, PK_DMS, 0}, {"hold", 0, 127, 0, PK_DMS, 0},
                                      {"decay", 0, 127, 127, PK_DECAY, 0}, {"lag", 0, 127, 0, PK_LAGMS, 0},
                                      {"cutoff", 0, 127, 127, PK_HZ, 0}, {"reso", 0, 127, 0, PK_PCT, 0},
                                      {"env", 0, 127, 64, PK_BIP, 0}, {"drive", 0, 127, 0, PK_PCT, 0},
                                      {"type", 0, NDF - 1, DF_OFF, PK_NAMES, DFTYPE_N}};
/* a drumline track's kit pages: Kit env (TP_A..TP_R) and Kit filter (the bus: TP_CUT..TP_KTRK) */
static const tp_info_t DRUMENV[4] = {{"lag", 0, 127, 0, PK_PCT, 0}, {"attack", 0, 127, 0, PK_PCT, 0},
                                     {"random", 0, 127, 0, PK_PCT, 0}, {"length", 0, 127, 64, PK_PCT, 0}};
static const tp_info_t DRUMFLT[4] = {{"lowcut", 0, 127, 0, PK_HZ, 0}, {"comp", 0, 127, 0, PK_PCT, 0},
                                     {"drive", 0, 127, 0, PK_PCT, 0}, {"hicut", 0, 127, 127, PK_HZ, 0}};
static const tp_info_t NONE_P = {"-", 0, 0, 0, PK_NUM, 0};
/* an engine's second page (TP_X1..X4; the Sub page in pages.c): brocade's sub, its level ("sub": the SEL list shows it
 * among the engine's) and its offset (-24 .. -12 semitones about 64, -12 by default: NuSaw's sub tune) */
static const tp_info_t BR_SUB_P[4] = {{"sub", 0, 127, 0, PK_PCT, 0}, {"offset", 40, 52, BR_SUB_DEF, PK_BIPST, 0},
                                      {"-", 0, 0, 0, PK_NUM, 0}, {"-", 0, 0, 0, PK_NUM, 0}};
_Static_assert(BR_SUB_DEF >= 40 && BR_SUB_DEF <= 52, "the sub's offset by default in its range");
/* orbit's second page: the index's fall (0 held; 20 ms .. 4 s), its floor, keyscale (orbit_prep) */
static const tp_info_t ORBIT_X_P[4] = {{"fall", 0, 127, 0, PK_NUM, 0}, {"floor", 0, 127, 0, PK_PCT, 0},
                                       {"keyscale", 0, 127, 0, PK_PCT, 0}, {"-", 0, 0, 0, PK_NUM, 0}};
static const tp_info_t STRUM_P[4] = {{"roll", 0, 63, 30, PK_NUM, 0}, {"pattern", 0, 3, 0, PK_NAMES, MPAT_N},
                                       {"-", 0, 0, 0, PK_NUM, 0}, {"hold", 0, 1, 0, PK_SWITCH, 0}};
/* the LFO's knobs per type (synth.h): speed / amount / dest / x / x2 */
static const tp_info_t LFO_SPEED_DUCK = {"source", 0, DUCK_METRO, 0, PK_NAMES, DUCK_N};
static const tp_info_t LFO_AMT[NLT] = {{"-", 0, 0, 0, PK_NUM, 0}, {"amount", 0, 127, 64, PK_BIP, 0},
                                       {"vibrato", 0, 127, 64, PK_BIP, 0}, {"amount", 0, 127, 64, PK_PCT, 0},
                                       {"amount", 0, 127, 80, PK_PCT, 0}};
static const tp_info_t LFO_DEST_TREM = {"volume", 0, 127, 96, PK_BIP, 0};
static const tp_info_t LFO_DEST_DUCK = {"hold", 0, 127, 54, PK_MS, 0};
static const tp_info_t LFO_X_TREM = {"envelope", 0, 127, 64, PK_FADE, 0};
static const tp_info_t LFO_X_DUCK = {"release", 0, 127, 76, PK_MS, 0};
static const tp_info_t LFO_X2_TREM = {"shape", 0, 4, 0, PK_NAMES, TSHAPE_N};
static const tp_info_t LFO_X2_RAND = {"envelope", 0, 127, 64, PK_FADE, 0};
static const tp_info_t LFO_PARAM_ENV = {"param", 0, 3, 0, PK_NAMES, ENVP_N};
static const tp_info_t LFO_PARAM_FLT = {"param", 0, 3, 0, PK_NAMES, FLTP_N};
static const tp_info_t LFO_PARAM_AMP = {"param", 0, 3, 0, PK_NAMES, AMPP_N};
static tp_info_t eknob[NENGINE][4];
static tp_info_t lparam[NENGINE];                /* the LFO's parameter knob on the synth module: the engine's names */
static tp_info_t dknob[NDM][4];                  /* a drum model's four knobs */
static uint8_t eknob_ready;

static void eknob_init(void)
{
    int e, k;
    for (e = 0; e < NENGINE; e++) {
        for (k = 0; k < 4; k++) {
            tp_info_t *i = &eknob[e][k];
            i->name = EG_KNOB[e][k];
            i->lo = 0;
            i->hi = 127;
            i->def = EG_DEF[e][k];
            i->kind = PK_NN;
            i->names = 0;
            if (e == EG_WAVETABLE && k == 0) {
                i->hi = WT_NTABLE - 1;
                i->kind = PK_NAMES;
                i->names = WTNAME_N;
            }
            if ((e == EG_STACK && k == 1) || e == EG_ORGAN) {   /* the discrete 1..8 knobs */
                i->lo = 1;
                i->hi = 8;
                i->kind = PK_NUM;              /* stack's ratio prints "N:1" (tp_text) */
            }
            if (e == EG_BROCADE && k == 0) {   /* the saws, 1..7 */
                i->lo = 1;
                i->hi = BR_SAWS;
                i->kind = PK_NUM;
            }
            if ((e == EG_CANE || e == EG_VOILE || e == EG_GINGHAM) && k == 0) {   /* the Braids models */
                i->hi = (uint8_t)(e == EG_CANE ? NCANE - 1 : e == EG_VOILE ? NVOILE - 1 : NGINGHAM - 1);
                i->kind = PK_NAMES;
                i->names = e == EG_CANE ? CANE_N : e == EG_VOILE ? VOILE_N : GING_N;
            }
            if (e == EG_GINGHAM && k == 3) {   /* the arp's chords */
                i->hi = NARP - 1;
                i->kind = PK_NAMES;
                i->names = ARP_N;
            }
            if (e == EG_SAMPLE) {              /* sample, start, length, mode */
                i->kind = k == 0 ? PK_SAMPLE : k == 3 ? PK_NAMES : PK_PCT;
                i->hi = (uint8_t)(k == 0 ? smp_slots() - 1 : k == 3 ? NSM - 1 : 127);
                i->names = k == 3 ? SMODE_N : 0;
            }
            if (e == EG_SLICE) {               /* sample, slices (4 8 16 32), pitch (+-24 st), gate */
                static const uint8_t KIND[4] = {PK_SAMPLE, PK_NAMES, PK_BIPST, PK_PCT};
                i->kind = KIND[k];
                i->lo = (uint8_t)(k == 2 ? 40 : 0);
                i->hi = (uint8_t)(k == 0 ? smp_slots() - 1 : k == 1 ? SL_NCOUNT - 1 : k == 2 ? 88 : 127);
                i->names = k == 1 ? SLICES_N : 0;
            }
        }
        lparam[e] = LFO_PARAM_ENV;
        lparam[e].names = EG_KNOB[e];
    }
    for (e = 0; e < NDM; e++)
        for (k = 0; k < 4; k++) {
            dknob[e][k] = DRUMP[DP_K1 + k];
            dknob[e][k].name = DM_KNOB[e][k];
            dknob[e][k].def = DM_DEFAULT[e][k];
        }
    {   /* the sample model: sample, pitch (+-24 semitones), start, reverse */
        tp_info_t *d = dknob[DM_SAMPLE];
        d[0].kind = PK_SAMPLE;
        d[0].hi = (uint8_t)(smp_slots() - 1);
        d[1].kind = PK_BIPST;
        d[1].lo = 40;
        d[1].hi = 88;
        d[2].kind = PK_PCT;
        d[3].kind = PK_SWITCH;
        d[3].hi = 1;
    }
    eknob_ready = 1;
}

const tp_info_t *tp_info(int p, int engine)
{
    if (!eknob_ready)
        eknob_init();
    if (p >= TP_E1 && p <= TP_E4) {
        if (engine < 0 || engine >= NENGINE)
            engine = EG_WEFT;
        return &eknob[engine][p - TP_E1];
    }
    if (p >= TP_X1 && p <= TP_X4)                /* the engine's second page: brocade's sub, else none */
        return engine == EG_BROCADE ? &BR_SUB_P[p - TP_X1] : engine == EG_ORBIT ? &ORBIT_X_P[p - TP_X1] : &NONE_P;
    if (engine == EG_DRUMLINE) {
        if (p >= TP_A && p <= TP_R)
            return &DRUMENV[p - TP_A];
        if (p >= TP_CUT && p <= TP_KTRK)
            return &DRUMFLT[p - TP_CUT];
    }
    if (p >= TP_DRUM0 && p < NTP)
        return &DRUMP[(p - TP_DRUM0) % DP_N];
    if (p >= 0 && p < (int)(sizeof FIXED / sizeof FIXED[0]))
        return &FIXED[p];
    return &NONE_P;
}

const tp_info_t *tp_info_par(int p, const uint8_t *par)
{
    int engine = par[TP_ENGINE], player = par[TP_PLAYER], lt = par[TP_LTYPE];
    if (!eknob_ready)
        eknob_init();
    if (p >= TP_P1 && p <= TP_AP4) {
        if (player == PL_ARP)
            return &FIXED[p];
        if (player == PL_STRUM && p <= TP_P4)
            return &STRUM_P[p - TP_P1];
        return &NONE_P;
    }
    if (p >= TP_DRUM0 && p < NTP) {
        int s = (p - TP_DRUM0) / DP_N, k = (p - TP_DRUM0) % DP_N;
        if (k >= DP_K1 && k <= DP_K4)
            return &dknob[par[TP_DRUM(s, DP_MODEL)] % NDM][k - DP_K1];
        return &DRUMP[k];
    }
    if (lt >= NLT)
        lt = LT_OFF;
    switch (p) {
    case TP_LSPEED: return lt == LT_OFF ? &NONE_P : lt == LT_PUMP ? &LFO_SPEED_DUCK : &FIXED[p];
    case TP_LAMT: return &LFO_AMT[lt];
    case TP_LDEST:
        return lt == LT_WAVE || lt == LT_DRIFT ? &FIXED[p] : lt == LT_WOBBLE ? &LFO_DEST_TREM
               : lt == LT_PUMP ? &LFO_DEST_DUCK : &NONE_P;
    case TP_LX:
        if (lt == LT_WAVE || lt == LT_DRIFT) {
            switch (par[TP_LDEST] & 3) {
            case LD_SYNTH: return &lparam[engine < NENGINE ? engine : EG_WEFT];
            case LD_ENV: return &LFO_PARAM_ENV;
            case LD_FILTER: return &LFO_PARAM_FLT;
            default: return &LFO_PARAM_AMP;
            }
        }
        return lt == LT_WOBBLE ? &LFO_X_TREM : lt == LT_PUMP ? &LFO_X_DUCK : &NONE_P;
    case TP_LX2:
        return lt == LT_WAVE ? &FIXED[p] : lt == LT_WOBBLE ? &LFO_X2_TREM : lt == LT_DRIFT ? &LFO_X2_RAND : &NONE_P;
    default: break;
    }
    return tp_info(p, engine);
}

/* number formatting without printf (the firmware's libc is small) */
static char *put_s(char *b, const char *s)
{
    while (*s)
        *b++ = *s++;
    *b = 0;
    return b;
}
static char *put_i(char *b, int v)
{
    char tmp[12];
    int n = 0;
    if (v < 0) {
        *b++ = '-';
        v = -v;
    }
    do {
        tmp[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    while (n)
        *b++ = tmp[--n];
    *b = 0;
    return b;
}
static char *put_f1(char *b, float x)           /* one decimal */
{
    int t = (int)(x * 10.0f + 0.5f);
    b = put_i(b, t / 10);
    *b++ = '.';
    return put_i(b, t % 10);
}
static void ms_text(float secs, char *num, char *unit)
{
    if (secs < 1.0f) {
        put_i(num, (int)(secs * 1000.0f + 0.5f));
        put_s(unit, "ms");
    } else {
        put_f1(num, secs);
        put_s(unit, "s");
    }
}
static void hz_text(float hz, char *num, char *unit)
{
    if (hz < 1000.0f) {
        put_i(num, (int)(hz + 0.5f));
        put_s(unit, "Hz");
    } else {
        put_f1(num, hz * 0.001f);
        put_s(unit, "kHz");
    }
}

static void tp_text_i(const tp_info_t *i, int p, int v, int engine, char *num, char *unit)
{
    unit[0] = 0;
    if (p == TP_CHAN && v == 0) {
        put_s(num, "off");
        return;
    }
    if (p == TP_PORTA && v == 0) {
        put_s(num, "off");
        return;
    }
    if (p == TP_E2 && engine == EG_STACK) {
        put_i(num, v < 1 ? 1 : v > 8 ? 8 : v);
        put_s(num + 1, ":1");
        return;
    }
    switch (i->kind) {
    case PK_PCT:
        put_i(num, (v * 100 + 63) / 127);
        put_s(unit, "%");
        break;
    case PK_BIP:
        if (v - 64 > 0)
            *num++ = '+';
        put_i(num, v - 64);
        break;
    case PK_NAMES:
        if (i->names && v >= i->lo && v <= i->hi)
            put_s(num, i->names[v - i->lo]);
        else
            put_i(num, v);
        break;
    case PK_SEMI:
        put_i(num, v);
        put_s(unit, "st");
        break;
    case PK_SWITCH: put_s(num, v ? "on" : "off"); break;
    case PK_MS: ms_text(env_seconds((float)v * (1.0f / 127.0f)), num, unit); break;
    case PK_HZ_SYNC:
        if (v < LS_SYNC_N) {
            put_s(num, SYNC_N[v]);
        } else {
            put_f1(num, lfo_hz(v, 120.0f));
            put_s(unit, "Hz");
        }
        break;
    case PK_NN: {                                /* two digits, 00..99 */
        int d = (v * 100 + 64) / 128;
        num[0] = (char)('0' + d / 10);
        num[1] = (char)('0' + d % 10);
        num[2] = 0;
        break;
    }
    case PK_HZ: hz_text(50.0f * fm_exp2f((float)v * (8.64385619f / 127.0f)), num, unit); break;
    case PK_FADE:
        if (v == 64) {
            put_s(num, "off");
        } else if (v < 64) {
            put_i(put_s(num, "in "), (64 - v) * 100 / 64);
            put_s(unit, "%");
        } else {
            put_i(put_s(num, "out "), (v - 64) * 100 / 63);
            put_s(unit, "%");
        }
        break;
    case PK_DB: {                                /* -30 .. +20 dB, 76 = 0 */
        int d = ((v - 76) * 50 + (v >= 76 ? 63 : -63)) / 127;
        if (d > 0)
            *num++ = '+';
        put_i(num, d);
        put_s(unit, "dB");
        break;
    }
    case PK_FADER: {                             /* tp_level_gain in dB: 60 log10(v / 100), tenths below 10 */
        int d;
        if (v <= 0) {
            put_s(num, "off");
            break;
        }
        d = (int)(fm_log2f((float)v * 0.01f) * 180.617997f + (v >= 100 ? 0.5f : -0.5f));   /* tenths */
        if (d > 0)
            *num++ = '+';
        else if (d < 0)
            *num++ = '-';
        d = d < 0 ? -d : d;
        num = put_i(num, d >= 100 ? (d + 5) / 10 : d / 10);
        if (d < 100 && d % 10) {
            *num++ = '.';
            put_i(num, d % 10);
        }
        put_s(unit, "dB");
        break;
    }
    case PK_SAMPLE: put_s(num, smp_name(v)); break;
    case PK_BIPST:
        if (v - 64 > 0)
            *num++ = '+';
        put_i(num, v - 64);
        put_s(unit, "st");
        break;
    case PK_DMS: case PK_DECAY:
        if (i->kind == PK_DECAY && v >= 127)
            put_s(num, "off");
        else if (v <= 0 && i->kind == PK_DMS) {
            put_s(num, "0");
            put_s(unit, "ms");
        } else
            ms_text(env_seconds((float)v * (1.0f / 127.0f)), num, unit);
        break;
    case PK_LAGMS:
        put_i(num, (v * 60 + 63) / 127);
        put_s(unit, "ms");
        break;
    default: put_i(num, v); break;
    }
}
void tp_text(int p, int v, int engine, char *num, char *unit) { tp_text_i(tp_info(p, engine), p, v, engine, num, unit); }
void tp_text_par(int p, int v, const uint8_t *par, char *num, char *unit)
{
    int ct = par[TP_CTYPE];
    if ((p == TP_CTONE && (ct == CT_DRIVE || ct == CT_TAPE || ct == CT_SAMPLER)) || (p == TP_CAMT && ct == CT_TILT)) {
        float t = (float)(v > 127 ? 127 : v) * (1.0f / 127.0f);   /* a low-pass, the tilt's pivot: Hz */
        hz_text(p == TP_CAMT ? ch_pivot_hz(t) : ct == CT_TAPE ? ch_tape_hz(t) : ct == CT_SAMPLER ? ch_smp_hz(t) : ch_lp_hz(t),
                num, unit);
        return;
    }
    if (p == TP_CAMT && ct == CT_SAMPLER) {                       /* the rate */
        hz_text(ch_smp_rate((float)(v > 127 ? 127 : v) * (1.0f / 127.0f)), num, unit);
        return;
    }
    if (p == TP_CTONE && ct == CT_RING) {                         /* the ratio */
        put_s(num, RING_N[(v > 127 ? 127 : v) * 16 / 128]);
        unit[0] = 0;
        return;
    }
    tp_text_i(tp_info_par(p, par), p, v, par[TP_ENGINE], num, unit);
}

void sound_default(uint8_t *par, int engine)
{
    int p, k;
    if (engine < 0 || engine >= NENGINE)
        engine = EG_WEFT;
    for (p = 0; p < TP_DRUM0; p++)
        par[p] = tp_info(p, engine)->def;
    par[TP_ENGINE] = (uint8_t)engine;
    for (k = 0; k < DL_NSOUND; k++)
        for (p = 0; p < DP_N; p++)
            par[TP_DRUM(k, p)] = DL_DEFAULT_KIT[k][p];
    if (engine == EG_DRUMLINE)
        par[TP_FTYPE] = FT_DYNAMIC;
}

/* -------------------------------------------------------------- presets --- */
/* ENGINE E1 E2 E3 E4 | MODE PORTA BEND VOL | A D S R | FA FD FS FR | CUT RES FENV KTRK TYPE | S1 S2 |
 * LTYPE LSPEED LAMT LDEST LX LX2 | CTYPE CAMT CTONE CMIX (the character: every preset a whole sound, its own
 * effect in it) | X1..X4 (the engine's second page: X_, the defaults) | the octave (OCT_: by the role), and the role
 * (RL_*). Sixteen an engine,
 * the first of each the engine's sound in a new project (and what choosing the engine loads), the rest by role:
 * basses, leads, keys, plucks and bells, stabs and brass, strings, pads and voices, chords, then the moving ones
 * and effects. Each after a classic where the engine can
 * (the comment above each engine's says which; docs/superpowers/presets-archetypes.md the map), each its own:
 * percussive, plucked, gated, swelling or ringing; dark, open, nasal, hollow or metallic; still or moving; and
 * most of them dry or nearly (the reverb for the sounds that want the room). The LFO fields follow synth.h: wave / drift:
 * amount, module, knob, shape (or envelope); wobble: vibrato, volume, envelope, shape. Speeds: 0..7 synced
 * (3 = 1/4), above free: 20 is about 0.16 Hz, 40 0.45 Hz, 60 1.2 Hz, 80 3 Hz, 92 5.6 Hz.
 * Times: 32 is about 10 ms, 64 100 ms, 80 330 ms, 96 1.1 s, 110 3 s. Cutoff: 59 is 800 Hz, 80 2.2 kHz,
 * 100 5.6 kHz. Each engine is a material of the loom: weft cotton, stack ply, wavetable silk, orbit linen,
 * tines felt, organ reed, sync wire, swarm fleece, sample jute, slice the loom itself, brocade the rich cloth
 * of many threads, cane the rattan and bamboo (strings, pipes, bells), voile the sheer cloth (voices), gingham
 * the checked cotton of picnics (toys). The volumes put each preset's phrase (tools/render_presets.sh) near
 * -19.5 dBFS RMS dry, its peak under -1.5 dBFS; tools/preset_distance.py says how far apart they sound. */
#define LFO_(lt, ls, la, ld, lx, lx2) lt, ls, la, ld, lx, lx2
#define CH_(ct, ca, cto, cm) ct, ca, cto, cm
#define PR(nm, rl, eng, e1, e2, e3, e4, vm, po, vol, a, d, su, r, fa, fd, fs, fr, cut, res, fenv, kt, ft, s1, s2, lfo, ch) \
    {nm, {eng, e1, e2, e3, e4, vm, po, 2, vol, a, d, su, r, fa, fd, fs, fr, cut, res, fenv, kt, ft, s1, s2, LFO_ lfo, \
          CH_ ch, X_(eng), OCT_(rl)}, rl}
#define OCT_(rl) ((rl) == RL_BASS ? 1 : 2)                  /* the octave by the role: a bass one down */
/* the engine's second page (TP_X1..X4) as sound_default leaves it: brocade's sub off at -12 semitones, else none */
#define X_(eng) 0, (eng) == EG_BROCADE ? BR_SUB_DEF : 0, 0, 0
#define NOLFO (LT_OFF, 3, 64, LD_FILTER, 0, 1)
#define NOCH (CT_OFF, 64, 64, 127)                         /* the character stage: off (type, amount, tone, mix) */
#define VIB(fade) (LT_WOBBLE, 92, 74, 64, fade, 0)          /* a vibrato of about 16 cents at 5.6 Hz */
const preset_t PRESETS[] = {
    /* weft: cotton, the everyday cloth; the Casio CZ's phase distortion. bend, wave (saw .. pulse .. resonant), sweep,
     * spread. cotton keys a CZ electric piano; denim bass the CZ's resonant bass; acid cord a TB-303 line (saw, ladder
     * high in resonance, the envelope's sweep, glide); poplin sub an 808's long sine; thread lead a Minimoog-ish saw
     * lead; twill the CZ's resonant wave swept, a screaming lead; madras a clavinet (narrow pulse, band-pass, snap);
     * button a crushed pluck; chintz bell the resonant wave struck; pulse yarn square chords; burlap a dark folded
     * stab; calico a Prophet-style brass (the filter's swell, a late vibrato); muslin pad a high-passed CZ pad;
     * chambray CZ strings (the detuned copy wide and moving); seersucker a dub siren (a square on the pitch, the delay); khaki
     * zap a laser, the pitch falling every sixteenth */
    PR("cotton keys", RL_KEYS, EG_WEFT, 70, 20, 0, 30, VM_POLY, 0, 107, 0, 84, 20, 84, 0, 72, 10, 60, 78, 10, 84, 64, FT_LP12, 0, 0, NOLFO, (CT_DRIVE, 30, 127, 127)),
    PR("denim bass", RL_BASS, EG_WEFT, 92, 40, 0, 0, VM_MONO, 0, 109, 0, 72, 95, 40, 0, 62, 0, 40, 58, 30, 100, 40, FT_LADDER, 0, 5, NOLFO, (CT_DRIVE, 60, 60, 127)),
    PR("acid cord", RL_BASS, EG_WEFT, 127, 13, 0, 14, VM_LEGATO, 18, 127, 0, 80, 60, 85, 0, 72, 0, 40, 36, 0, 112, 40, FT_LADDER, 0, 0, NOLFO, (CT_DRIVE, 80, 110, 127)),
    PR("poplin sub", RL_BASS, EG_WEFT, 0, 14, 7, 17, VM_MONO, 0, 127, 48, 100, 0, 80, 0, 60, 0, 40, 68, 0, 64, 64, FT_LP24, 0, 0, NOLFO, (CT_DRIVE, 50, 60, 127)),
    PR("thread lead", RL_LEAD, EG_WEFT, 102, 71, 78, 33, VM_MONO, 0, 104, 0, 70, 90, 64, 0, 64, 0, 40, 53, 32, 112, 64, FT_LADDER, 0, 0, VIB(30), (CT_DRIVE, 68, 125, 127)),
    PR("twill", RL_LEAD, EG_WEFT, 14, 112, 126, 24, VM_LEGATO, 30, 93, 56, 100, 127, 50, 0, 64, 30, 50, 100, 35, 100, 64, FT_LP12, 0, 0, (LT_WAVE, 60, 100, LD_SYNTH, 2, 1), (CT_DRIVE, 70, 127, 127)),
    PR("madras", RL_KEYS, EG_WEFT, 127, 57, 0, 24, VM_POLY, 0, 127, 0, 102, 0, 70, 0, 56, 0, 40, 66, 45, 112, 64, FT_BP12, 0, 0, NOLFO, (CT_DRIVE, 0, 117, 127)),
    PR("button", RL_PLUCK, EG_WEFT, 80, 100, 16, 0, VM_POLY, 0, 127, 0, 72, 0, 85, 0, 64, 0, 40, 105, 5, 76, 64, FT_LP12, 25, 0, NOLFO, (CT_CRUSH, 35, 10, 127)),
    PR("chintz bell", RL_BELL, EG_WEFT, 36, 127, 100, 40, VM_POLY, 0, 127, 0, 108, 0, 104, 0, 72, 60, 80, 116, 0, 76, 64, FT_BP12, 0, 45, NOLFO, NOCH),
    PR("pulse yarn", RL_CHORD, EG_WEFT, 95, 64, 7, 57, VM_POLY, 0, 99, 50, 80, 40, 50, 0, 56, 30, 50, 50, 12, 64, 64, FT_LP24, 0, 0, (LT_WAVE, 60, 90, LD_SYNTH + LD_FREE, 0, 1), (CT_CRUSH, 30, 90, 80)),
    PR("burlap", RL_STAB, EG_WEFT, 84, 90, 16, 46, VM_POLY, 0, 127, 0, 82, 20, 55, 0, 88, 30, 50, 45, 12, 64, 64, FT_LP24, 18, 18, NOLFO, (CT_FOLD, 70, 0, 127)),
    PR("calico", RL_BRASS, EG_WEFT, 100, 10, 0, 70, VM_POLY, 0, 123, 36, 84, 80, 64, 0, 64, 0, 66, 35, 0, 112, 64, FT_LADDER, 0, 18, VIB(20), (CT_DRIVE, 20, 127, 127)),
    PR("muslin pad", RL_PAD, EG_WEFT, 89, 0, 0, 127, VM_POLY, 0, 70, 79, 90, 110, 100, 0, 80, 64, 80, 57, 29, 64, 64, FT_HP12, 10, 70, (LT_WAVE, 72, 88, LD_SYNTH + LD_FREE, 3, 1), (CT_TILT, 45, 126, 127)),
    PR("chambray", RL_STRINGS, EG_WEFT, 92, 24, 0, 89, VM_POLY, 0, 71, 90, 90, 120, 90, 0, 80, 64, 80, 55, 0, 64, 64, FT_LP12, 0, 20, (LT_WAVE, 45, 90, LD_SYNTH + LD_FREE, 3, 1), (CT_TILT, 60, 110, 127)),
    PR("seersucker", RL_FX, EG_WEFT, 90, 64, 0, 7, VM_MONO, 0, 78, 50, 110, 120, 100, 0, 64, 0, 40, 86, 10, 76, 64, FT_LP12, 80, 10, (LT_WAVE, 82, 90, LD_AMP, 1, 0), (CT_TILT, 45, 20, 127)),
    PR("khaki zap", RL_FX, EG_WEFT, 110, 110, 12, 0, VM_MONO, 0, 127, 80, 90, 0, 60, 0, 88, 30, 40, 115, 35, 88, 64, FT_LADDER, 30, 0, (LT_WAVE, 5, 127, LD_AMP, 1, 3), (CT_TILT, 60, 20, 127)),
    /* stack: ply, strands twisted together; two oscillators. wave, ratio N:1, detune, spread. ply bass a Minimoog
     * bass (saw and its octave, the ladder's pluck); rope sub a square sub under a low filter; hawser a Reese
     * (two saws beating, the detune wandering); elastic a wobble bass (the filter on the beat); lanyard a Minimoog
     * lead (glide, a late vibrato); twine fifth a hollow 3:1; twister the hoover (PWM fast, wide, a long glide);
     * sinew a square pluck; glass bead a SOPHIE-like metallic pluck (7:1, ring-modulated); braid stab Jump's detuned saws, decaying; plait brass a
     * Prophet-5 brass (the filter's swell); cordage Juno strings (PWM slow, wide); wool pad a Jupiter-8 pad;
     * tartan an organ-like square octave, gated; tangle the ratio stepping each sixteenth, a harmonic arpeggio */
    PR("ply bass", RL_BASS, EG_STACK, 13, 2, 4, 7, VM_MONO, 0, 106, 0, 70, 95, 35, 0, 62, 0, 30, 60, 35, 100, 20, FT_LP24, 0, 0, NOLFO, (CT_DRIVE, 30, 100, 127)),
    PR("rope sub", RL_BASS, EG_STACK, 64, 1, 14, 7, VM_MONO, 0, 87, 0, 70, 120, 35, 0, 80, 30, 30, 40, 0, 64, 30, FT_LP24, 0, 0, NOLFO, (CT_DRIVE, 45, 70, 127)),
    PR("hawser", RL_BASS, EG_STACK, 7, 1, 54, 14, VM_MONO, 0, 127, 30, 100, 0, 70, 0, 60, 0, 40, 70, 10, 64, 40, FT_LP24, 0, 0, (LT_WAVE, 30, 84, LD_SYNTH + LD_FREE, 2, 1), (CT_DRIVE, 0, 120, 127)),
    PR("elastic", RL_BASS, EG_STACK, 18, 1, 2, 20, VM_LEGATO, 80, 103, 0, 60, 120, 35, 0, 72, 30, 40, 36, 0, 73, 64, FT_LADDER, 0, 0, (LT_WAVE, 3, 127, LD_FILTER, 0, 2), (CT_DRIVE, 67, 90, 127)),
    PR("lanyard", RL_LEAD, EG_STACK, 16, 2, 12, 7, VM_LEGATO, 28, 108, 0, 70, 110, 64, 0, 56, 30, 50, 66, 35, 76, 64, FT_LADDER, 0, 0, VIB(30), (CT_DRIVE, 60, 80, 127)),
    PR("twine fifth", RL_LEAD, EG_STACK, 63, 3, 30, 75, VM_MONO, 0, 127, 56, 80, 127, 30, 0, 64, 0, 40, 55, 53, 76, 64, FT_LP12, 30, 0, NOLFO, (CT_FOLD, 34, 127, 127)),
    PR("twister", RL_LEAD, EG_STACK, 40, 1, 127, 127, VM_LEGATO, 70, 76, 56, 90, 127, 70, 0, 60, 0, 50, 50, 0, 64, 64, FT_LP24, 25, 0, (LT_WAVE, 92, 127, LD_SYNTH + LD_FREE, 0, 1), (CT_DRIVE, 80, 127, 127)),
    PR("skein", RL_KEYS, EG_STACK, 117, 1, 22, 64, VM_POLY, 0, 69, 0, 84, 0, 56, 0, 56, 0, 60, 107, 10, 88, 64, FT_LP12, 0, 12, NOLFO, (CT_TILT, 45, 35, 127)),
    PR("sinew", RL_PLUCK, EG_STACK, 109, 2, 0, 0, VM_POLY, 0, 96, 0, 60, 0, 60, 0, 56, 30, 50, 45, 0, 64, 64, FT_LP24, 25, 0, NOLFO, (CT_TILT, 75, 20, 127)),
    PR("glass bead", RL_PLUCK, EG_STACK, 127, 7, 10, 60, VM_POLY, 0, 82, 0, 60, 10, 85, 0, 56, 30, 70, 61, 0, 64, 64, FT_LP24, 35, 8, NOLFO, (CT_RING, 50, 110, 127)),
    PR("braid stab", RL_STAB, EG_STACK, 7, 1, 22, 70, VM_POLY, 0, 106, 0, 100, 0, 70, 0, 80, 0, 60, 68, 25, 112, 64, FT_LP24, 0, 15, NOLFO, (CT_DRIVE, 80, 90, 127)),
    PR("plait brass", RL_BRASS, EG_STACK, 0, 1, 15, 40, VM_POLY, 0, 113, 52, 80, 100, 80, 0, 88, 0, 64, 35, 20, 100, 64, FT_LP24, 0, 0, VIB(20), (CT_TILT, 75, 80, 127)),
    PR("cordage", RL_STRINGS, EG_STACK, 56, 1, 27, 127, VM_POLY, 0, 43, 70, 100, 127, 80, 0, 80, 64, 80, 55, 0, 64, 64, FT_LP12, 0, 35, (LT_WAVE, 50, 100, LD_SYNTH + LD_FREE, 0, 1), (CT_TILT, 30, 35, 127)),
    PR("wool pad", RL_PAD, EG_STACK, 0, 2, 0, 46, VM_POLY, 0, 121, 100, 90, 127, 88, 107, 116, 90, 96, 30, 0, 121, 50, FT_LP24, 0, 65, NOLFO, (CT_DRIVE, 29, 27, 127)),
    PR("tartan", RL_CHORD, EG_STACK, 9, 2, 0, 66, VM_POLY, 0, 92, 50, 100, 40, 64, 0, 56, 0, 40, 86, 28, 88, 64, FT_BP12, 0, 0, NOLFO, (CT_TILT, 75, 110, 127)),
    PR("tangle", RL_FX, EG_STACK, 127, 5, 54, 60, VM_MONO, 0, 73, 0, 90, 100, 100, 0, 60, 0, 50, 55, 35, 64, 64, FT_LP24, 30, 10, (LT_WAVE, 5, 127, LD_SYNTH, 1, 3), NOCH),
    /* wavetable: silk, woven fine; the PPG's and Waldorf's way. table, scan, bend, ripple. Tables: 0 shapes, 1 nasal,
     * 2 grit, 3 vocal, 4 spiral, 5 fold, 6 octaves, 7 glass, 8 chirp. silk pad a PPG pad drifting through its
     * table; indigo bass a folded bass; taffeta a growl (the scan wobbling in eighths); satin lead a soft legato
     * lead; shantung the chirp table swept up across a note, a sync-like lead; lace keys glassy keys; crepe a nasal
     * stab; pongee a pluck whose scan falls (the PPG's plucked sweeps); spool bell glass rung; gauze choir the vocal
     * table band-passed; georgette strings (the warp drifting); faille a wavetable brass; chiffon an airy glass pad;
     * tussah dark octave-table chords; spiral the spiral table turning; noil grit rippled fast, a noise */
    PR("silk pad", RL_PAD, EG_WAVETABLE, 5, 65, 35, 44, VM_POLY, 0, 89, 90, 90, 110, 96, 0, 80, 64, 80, 88, 80, 64, 64, FT_LP24, 0, 20, (LT_WAVE, 34, 120, LD_SYNTH + LD_FREE, 1, 1), (CT_TILT, 60, 110, 127)),
    PR("indigo bass", RL_BASS, EG_WAVETABLE, 5, 36, 6, 24, VM_MONO, 0, 116, 48, 70, 95, 85, 0, 88, 30, 30, 72, 10, 64, 40, FT_LADDER, 0, 0, NOLFO, (CT_FOLD, 75, 74, 127)),
    PR("taffeta", RL_BASS, EG_WAVETABLE, 2, 47, 0, 84, VM_MONO, 0, 94, 10, 60, 120, 85, 0, 88, 60, 40, 56, 50, 88, 40, FT_LP12, 0, 0, (LT_WAVE, 4, 100, LD_SYNTH, 1, 1), (CT_DRIVE, 0, 30, 127)),
    PR("satin lead", RL_LEAD, EG_WAVETABLE, 0, 10, 1, 0, VM_LEGATO, 70, 92, 80, 80, 115, 64, 0, 64, 30, 60, 99, 20, 112, 64, FT_LP12, 0, 0, (LT_WOBBLE, 81, 70, 64, 14, 0), (CT_TILT, 45, 35, 127)),
    PR("shantung", RL_LEAD, EG_WAVETABLE, 8, 46, 0, 0, VM_LEGATO, 20, 111, 56, 100, 60, 78, 0, 60, 0, 40, 115, 35, 64, 64, FT_LP12, 25, 0, (LT_WAVE, 1, 30, LD_SYNTH, 1, 3), (CT_DRIVE, 0, 127, 127)),
    PR("lace keys", RL_KEYS, EG_WAVETABLE, 0, 70, 17, 0, VM_POLY, 0, 95, 0, 82, 40, 84, 0, 66, 0, 50, 95, 5, 80, 64, FT_LP12, 0, 12, NOLFO, (CT_TILT, 30, 20, 127)),
    PR("crepe", RL_STAB, EG_WAVETABLE, 1, 36, 0, 0, VM_POLY, 0, 107, 0, 80, 0, 60, 0, 76, 0, 50, 66, 25, 96, 64, FT_LP24, 0, 15, NOLFO, (CT_TILT, 30, 110, 127)),
    PR("pongee", RL_PLUCK, EG_WAVETABLE, 6, 90, 14, 0, VM_POLY, 0, 127, 0, 80, 0, 70, 0, 64, 60, 50, 108, 28, 112, 64, FT_LP12, 30, 0, (LT_WAVE, 3, 120, LD_SYNTH, 1, 3), (CT_TILT, 30, 95, 127)),
    PR("spool bell", RL_BELL, EG_WAVETABLE, 7, 90, 0, 30, VM_POLY, 0, 122, 0, 100, 20, 108, 0, 90, 0, 80, 116, 5, 64, 64, FT_LP12, 0, 50, NOLFO, (CT_RING, 45, 58, 127)),
    PR("gauze choir", RL_VOICE, EG_WAVETABLE, 3, 23, 14, 32, VM_POLY, 0, 127, 70, 90, 100, 85, 0, 88, 0, 80, 70, 41, 100, 50, FT_BP12, 0, 55, (LT_WAVE, 21, 113, LD_SYNTH + LD_FREE, 1, 1), (CT_TILT, 60, 80, 127)),
    PR("georgette", RL_STRINGS, EG_WAVETABLE, 0, 45, 37, 0, VM_POLY, 0, 71, 84, 100, 100, 96, 0, 80, 64, 80, 66, 28, 64, 64, FT_LP12, 0, 40, (LT_WAVE, 40, 84, LD_SYNTH + LD_FREE, 2, 1), (CT_TILT, 60, 35, 127)),
    PR("faille", RL_BRASS, EG_WAVETABLE, 5, 39, 0, 0, VM_POLY, 0, 91, 50, 76, 120, 66, 0, 56, 60, 66, 50, 28, 88, 64, FT_LP24, 0, 18, VIB(20), (CT_DRIVE, 0, 100, 127)),
    PR("chiffon", RL_PAD, EG_WAVETABLE, 7, 65, 0, 0, VM_POLY, 0, 127, 92, 90, 100, 101, 0, 80, 64, 80, 78, 38, 64, 64, FT_HP12, 0, 65, (LT_DRIFT, 45, 50, LD_SYNTH + LD_FREE, 1, 64), (CT_TILT, 65, 117, 127)),
    PR("tussah", RL_CHORD, EG_WAVETABLE, 6, 0, 0, 0, VM_POLY, 0, 52, 50, 60, 127, 80, 0, 60, 0, 40, 97, 20, 64, 64, FT_LP12, 15, 0, NOLFO, (CT_TILT, 90, 20, 127)),
    PR("spiral", RL_FX, EG_WAVETABLE, 4, 24, 58, 20, VM_POLY, 0, 127, 0, 110, 60, 100, 0, 60, 0, 40, 30, 20, 64, 64, FT_LP24, 20, 0, (LT_WAVE, 50, 127, LD_SYNTH + LD_FREE, 1, 1), (CT_FOLD, 30, 64, 127)),
    PR("noil", RL_FX, EG_WAVETABLE, 2, 114, 103, 127, VM_POLY, 0, 127, 50, 100, 0, 50, 0, 60, 0, 40, 80, 50, 64, 64, FT_BP12, 30, 30, (LT_WAVE, 80, 120, LD_SYNTH + LD_FREE, 3, 1), (CT_CRUSH, 50, 74, 127)),
    /* orbit: linen, long fibres, bowed; two-operator FM. bright, ratio (up to 50 a slow beat, above harmonic: 66
     * 1/2, 72 2/3, 77 3/4, 83 5/4, 88 4/3, 93 3/2, 98 7/4, 104 2, 109 3, 114 4, 119 6, 125 8), edge, wobble.
     * bowed linen FM strings; cord bass the DX7's BASS 1 (a half-ratio modulator, plucked); buckram an FM wobble
     * bass; needle a bright FM lead; lawn flute a soft sine flute; cambric FM brass (feedback for the saw, the
     * filter as the modulator's envelope); hollow the M1's house organ; holland the DX's glassy keys (8:1);
     * thimble a marimba (4:1, short); chime a bell (5:4, long); toile a kalimba (6:1); ramie an FM stab (4:3);
     * warp fifth chords at 3:2; moire two beating sines; tow a dark drone; scrim a metal noise (feedback chaos) */
    PR("bowed linen", RL_STRINGS, EG_ORBIT, 73, 30, 53, 90, VM_POLY, 0, 110, 100, 90, 120, 80, 0, 80, 0, 80, 95, 28, 64, 64, FT_LP12, 0, 50, (LT_WAVE, 34, 75, LD_FILTER + LD_FREE, 0, 1), (CT_DRIVE, 20, 90, 127)),
    PR("cord bass", RL_BASS, EG_ORBIT, 77, 66, 47, 0, VM_MONO, 0, 127, 48, 100, 0, 50, 0, 70, 10, 40, 100, 20, 100, 40, FT_LP24, 0, 0, NOLFO, (CT_DRIVE, 50, 80, 127)),
    PR("buckram", RL_BASS, EG_ORBIT, 114, 104, 71, 0, VM_MONO, 0, 127, 0, 80, 60, 70, 0, 60, 0, 40, 72, 30, 64, 30, FT_LADDER, 0, 0, (LT_WAVE, 4, 30, LD_SYNTH, 0, 1), (CT_DRIVE, 70, 70, 127)),
    PR("needle", RL_LEAD, EG_ORBIT, 98, 102, 64, 7, VM_LEGATO, 20, 90, 2, 90, 90, 45, 0, 60, 30, 50, 120, 5, 70, 64, FT_LP12, 0, 8, VIB(30), (CT_DRIVE, 25, 127, 127)),
    PR("lawn flute", RL_LEAD, EG_ORBIT, 20, 0, 0, 30, VM_LEGATO, 20, 74, 56, 90, 127, 64, 0, 60, 0, 40, 58, 50, 64, 64, FT_LP12, 20, 10, VIB(10), (CT_TILT, 30, 110, 127)),
    PR("cambric", RL_BRASS, EG_ORBIT, 64, 0, 32, 31, VM_POLY, 0, 121, 52, 76, 80, 64, 56, 84, 70, 60, 47, 5, 104, 64, FT_LADDER, 0, 0, VIB(20), (CT_TILT, 75, 80, 127)),
    PR("hollow", RL_KEYS, EG_ORBIT, 0, 104, 38, 0, VM_POLY, 0, 127, 20, 84, 0, 56, 0, 88, 0, 40, 36, 0, 64, 64, FT_HP12, 0, 0, NOLFO, (CT_DRIVE, 0, 70, 127)),
    PR("holland", RL_KEYS, EG_ORBIT, 55, 125, 0, 0, VM_POLY, 0, 82, 20, 96, 0, 70, 0, 60, 0, 50, 115, 0, 64, 64, FT_LP12, 0, 12, NOLFO, (CT_TILT, 30, 95, 127)),
    PR("thimble", RL_BELL, EG_ORBIT, 67, 114, 0, 0, VM_POLY, 0, 117, 0, 100, 0, 108, 0, 60, 0, 40, 90, 28, 64, 64, FT_BP12, 0, 40, NOLFO, (CT_TILT, 90, 110, 127)),
    PR("chime", RL_BELL, EG_ORBIT, 102, 81, 0, 0, VM_POLY, 0, 96, 0, 100, 0, 108, 0, 96, 0, 90, 127, 0, 64, 64, FT_LP12, 0, 55, NOLFO, (CT_RING, 0, 104, 127)),
    PR("toile", RL_PLUCK, EG_ORBIT, 38, 119, 0, 0, VM_POLY, 0, 116, 0, 60, 30, 70, 0, 88, 0, 40, 96, 0, 88, 64, FT_LP12, 0, 0, NOLFO, (CT_TILT, 30, 80, 127)),
    PR("ramie", RL_STAB, EG_ORBIT, 107, 88, 2, 0, VM_POLY, 0, 107, 0, 100, 0, 60, 0, 64, 0, 50, 68, 15, 76, 64, FT_BP12, 0, 15, NOLFO, (CT_DRIVE, 30, 127, 127)),
    PR("warp fifth", RL_CHORD, EG_ORBIT, 70, 92, 10, 40, VM_POLY, 0, 107, 50, 90, 127, 80, 0, 72, 30, 70, 90, 20, 88, 64, FT_BP12, 15, 0, (LT_WAVE, 45, 100, LD_SYNTH + LD_FREE, 0, 1), (CT_FOLD, 5, 64, 90)),
    PR("moire", RL_PAD, EG_ORBIT, 74, 45, 13, 60, VM_POLY, 0, 79, 80, 90, 90, 90, 30, 80, 50, 80, 72, 5, 70, 64, FT_LP12, 0, 45, (LT_DRIFT, 40, 70, LD_FILTER + LD_FREE, 0, 64), (CT_RING, 55, 80, 127)),
    PR("tow", RL_PAD, EG_ORBIT, 40, 66, 10, 23, VM_POLY, 0, 103, 96, 110, 120, 110, 0, 80, 64, 80, 40, 10, 64, 64, FT_LP24, 0, 50, (LT_DRIFT, 30, 60, LD_SYNTH + LD_FREE, 0, 64), (CT_TILT, 40, 30, 127)),
    PR("scrim", RL_FX, EG_ORBIT, 127, 88, 127, 0, VM_POLY, 0, 111, 0, 90, 0, 80, 0, 72, 60, 40, 78, 20, 112, 64, FT_HP12, 10, 10, NOLFO, (CT_TILT, 90, 110, 127)),
    /* tines: felt, a hammer's soft face; an FM electric piano. bright, grit, bite, bell. felt piano the DX7's E.PIANO 1
     * (the 14x tine); soft felt a mellow Rhodes; felt bass an FM bass; moleskin a plucked bass (all bite); corduroy a
     * Rhodes barking (driven); loden a Wurlitzer (grit, driven, its choppy tremolo); baize a clavinet (grit and
     * bite band-passed, no sustain); chamois the M1's bright house piano (all bite, short); hammer a hard, bright piano; velvet lead a
     * folded lead; lullaby a music box; melton a vibraphone (its motor's tremolo); plush a kalimba (high-passed,
     * short); flannel pad an electric piano pad; flock lo-fi keys (crushed, the tape's wow); duffel a ring-modulated
     * bell, long */
    PR("felt piano", RL_KEYS, EG_TINES, 53, 0, 40, 55, VM_POLY, 0, 103, 0, 92, 55, 66, 0, 70, 0, 50, 116, 5, 64, 64, FT_LP12, 0, 0, (LT_WOBBLE, 84, 64, 82, 64, 0), (CT_DRIVE, 5, 100, 127)),
    PR("soft felt", RL_KEYS, EG_TINES, 10, 0, 0, 10, VM_POLY, 0, 82, 20, 95, 50, 72, 0, 88, 0, 50, 76, 0, 88, 64, FT_LP12, 0, 0, NOLFO, (CT_TILT, 45, 35, 127)),
    PR("felt bass", RL_BASS, EG_TINES, 62, 40, 90, 0, VM_MONO, 0, 112, 10, 82, 90, 35, 0, 66, 0, 30, 80, 10, 64, 40, FT_LP12, 0, 0, NOLFO, (CT_DRIVE, 35, 20, 127)),
    PR("moleskin", RL_BASS, EG_TINES, 22, 0, 34, 58, VM_MONO, 0, 127, 48, 80, 0, 50, 0, 88, 30, 40, 79, 0, 88, 40, FT_LP12, 0, 0, NOLFO, (CT_DRIVE, 0, 70, 127)),
    PR("corduroy", RL_KEYS, EG_TINES, 127, 83, 39, 34, VM_POLY, 0, 127, 0, 86, 0, 40, 0, 56, 60, 50, 120, 10, 100, 64, FT_LP12, 0, 10, NOLFO, (CT_DRIVE, 80, 110, 127)),
    PR("loden", RL_KEYS, EG_TINES, 43, 63, 19, 0, VM_POLY, 0, 127, 10, 96, 0, 40, 0, 64, 0, 40, 84, 12, 64, 64, FT_LP12, 12, 0, (LT_WOBBLE, 88, 64, 20, 64, 3), (CT_DRIVE, 0, 100, 127)),
    PR("baize", RL_PLUCK, EG_TINES, 90, 110, 100, 0, VM_POLY, 0, 127, 0, 90, 0, 40, 0, 60, 0, 40, 78, 40, 90, 64, FT_BP12, 0, 0, NOLFO, (CT_TILT, 90, 110, 127)),
    PR("chamois", RL_STAB, EG_TINES, 100, 2, 83, 78, VM_POLY, 0, 102, 0, 90, 20, 70, 0, 60, 0, 40, 74, 12, 64, 64, FT_BP12, 18, 10, NOLFO, (CT_TILT, 45, 80, 127)),
    PR("hammer", RL_KEYS, EG_TINES, 127, 114, 127, 90, VM_POLY, 0, 101, 20, 84, 20, 40, 0, 88, 30, 40, 127, 5, 112, 64, FT_LP12, 0, 10, NOLFO, (CT_DRIVE, 15, 127, 127)),
    PR("velvet lead", RL_LEAD, EG_TINES, 42, 2, 115, 121, VM_POLY, 0, 127, 40, 106, 37, 96, 0, 56, 60, 50, 96, 10, 76, 64, FT_BP12, 30, 10, NOLFO, (CT_FOLD, 70, 70, 127)),
    PR("lullaby", RL_BELL, EG_TINES, 44, 0, 0, 127, VM_POLY, 0, 127, 0, 112, 0, 86, 0, 70, 0, 50, 96, 28, 64, 64, FT_BP12, 0, 45, NOLFO, (CT_RING, 55, 84, 127)),
    PR("melton", RL_BELL, EG_TINES, 0, 0, 0, 70, VM_POLY, 0, 106, 0, 92, 20, 108, 0, 80, 30, 40, 112, 12, 100, 64, FT_LP12, 0, 10, (LT_WOBBLE, 84, 64, 30, 64, 0), (CT_TILT, 30, 95, 127)),
    PR("plush", RL_PLUCK, EG_TINES, 70, 0, 127, 90, VM_POLY, 0, 105, 0, 84, 10, 85, 0, 60, 0, 40, 66, 20, 64, 64, FT_HP12, 15, 0, NOLFO, NOCH),
    PR("flannel pad", RL_PAD, EG_TINES, 15, 22, 80, 32, VM_POLY, 0, 81, 0, 100, 127, 115, 0, 56, 60, 80, 63, 5, 100, 64, FT_LP12, 10, 20, (LT_WOBBLE, 80, 66, 127, 64, 0), (CT_DRIVE, 40, 90, 127)),
    PR("flock", RL_CHORD, EG_TINES, 40, 0, 30, 20, VM_POLY, 0, 88, 30, 90, 70, 80, 0, 88, 0, 40, 50, 28, 64, 64, FT_LP12, 15, 0, (LT_WOBBLE, 30, 76, 64, 64, 0), (CT_CRUSH, 60, 50, 127)),
    PR("duffel", RL_FX, EG_TINES, 16, 0, 0, 127, VM_POLY, 0, 96, 0, 110, 0, 50, 0, 56, 0, 40, 96, 0, 100, 64, FT_LADDER, 20, 50, NOLFO, (CT_RING, 127, 127, 127)),
    /* organ: reed, air through a tongue. stops (1 transistor, 2 combo, 3 jazz, 4 gospel, 5 rock, 6 theatre, 7 pipe,
     * 8 church), pedal, rotor, rate: 1..8. reed combo a Vox-style combo; pedal bass the pedals' 16'; bellows the
     * M1's Organ 2 as a bass (percussive, mono, glide); parlour a jazz drawbar organ (888000000); drawbar the same with its
     * percussion's click (the filter's quick envelope); sunday a gospel organ, all out and driven, the rotor fast;
     * musette an accordion (reedy, its wet vibrato); harmonium a reed organ breathing in; carousel a fairground
     * organ; calliope a steam whistle; loft flute a Mellotron flute (the pipe's flute, the tape's wow and grain);
     * crypt an organ stab; chapel a soft pipe; cathedral the full chorus; swell church stops swelling in, dark;
     * steam a ring-modulated organ, the rotor fast */
    PR("reed combo", RL_KEYS, EG_ORGAN, 2, 3, 4, 7, VM_POLY, 0, 84, 0, 60, 100, 22, 0, 60, 0, 40, 120, 0, 64, 64, FT_LP12, 12, 12, NOLFO, (CT_DRIVE, 60, 117, 127)),
    PR("pedal bass", RL_BASS, EG_ORGAN, 1, 8, 3, 1, VM_MONO, 0, 127, 0, 60, 100, 30, 0, 72, 60, 40, 58, 0, 112, 40, FT_LP12, 0, 0, NOLFO, (CT_TILT, 75, 110, 127)),
    PR("bellows", RL_BASS, EG_ORGAN, 3, 8, 1, 1, VM_MONO, 30, 127, 48, 70, 56, 85, 0, 60, 0, 40, 78, 0, 64, 40, FT_LP12, 0, 0, NOLFO, (CT_DRIVE, 74, 100, 127)),
    PR("parlour", RL_KEYS, EG_ORGAN, 3, 4, 5, 6, VM_POLY, 0, 104, 20, 96, 60, 40, 0, 56, 30, 40, 90, 28, 76, 64, FT_LP12, 0, 0, NOLFO, (CT_TILT, 90, 35, 127)),
    PR("drawbar", RL_KEYS, EG_ORGAN, 3, 2, 2, 3, VM_POLY, 0, 88, 0, 102, 127, 84, 0, 56, 0, 40, 76, 20, 100, 64, FT_LP24, 0, 12, NOLFO, (CT_TILT, 75, 110, 127)),
    PR("sunday", RL_CHORD, EG_ORGAN, 4, 6, 7, 8, VM_POLY, 0, 90, 50, 60, 100, 50, 0, 80, 60, 40, 120, 0, 100, 64, FT_LP12, 0, 12, NOLFO, (CT_DRIVE, 70, 95, 127)),
    PR("musette", RL_KEYS, EG_ORGAN, 1, 1, 1, 1, VM_POLY, 0, 66, 32, 102, 127, 56, 0, 60, 0, 40, 107, 0, 64, 64, FT_LP12, 12, 0, (LT_WOBBLE, 100, 76, 64, 64, 0), (CT_TILT, 45, 110, 127)),
    PR("harmonium", RL_CHORD, EG_ORGAN, 1, 2, 3, 3, VM_POLY, 0, 94, 50, 60, 100, 56, 0, 60, 0, 40, 95, 12, 64, 64, FT_LP12, 0, 0, NOLFO, (CT_FOLD, 10, 84, 127)),
    PR("carousel", RL_KEYS, EG_ORGAN, 6, 4, 8, 8, VM_POLY, 0, 88, 0, 60, 100, 30, 0, 60, 0, 40, 93, 0, 64, 64, FT_LP12, 10, 12, NOLFO, (CT_FOLD, 120, 90, 127)),
    PR("calliope", RL_LEAD, EG_ORGAN, 7, 1, 2, 1, VM_POLY, 0, 80, 56, 80, 127, 50, 0, 60, 0, 40, 65, 35, 64, 64, FT_LP12, 20, 10, (LT_WOBBLE, 96, 90, 64, 30, 0), (CT_DRIVE, 60, 110, 127)),
    PR("loft flute", RL_LEAD, EG_ORGAN, 7, 1, 1, 1, VM_LEGATO, 15, 105, 64, 80, 127, 72, 0, 60, 0, 40, 62, 50, 64, 64, FT_LP24, 0, 0, (LT_WOBBLE, 40, 72, 64, 64, 0), (CT_CRUSH, 10, 20, 127)),
    PR("crypt", RL_STAB, EG_ORGAN, 4, 5, 1, 2, VM_POLY, 0, 127, 0, 72, 0, 55, 0, 64, 0, 40, 58, 5, 64, 64, FT_LP24, 0, 0, NOLFO, (CT_DRIVE, 70, 100, 127)),
    PR("chapel", RL_CHORD, EG_ORGAN, 7, 8, 1, 3, VM_POLY, 0, 70, 50, 60, 127, 80, 0, 64, 0, 40, 92, 0, 88, 64, FT_LP12, 15, 0, NOLFO, (CT_TILT, 30, 20, 127)),
    PR("cathedral", RL_PAD, EG_ORGAN, 8, 5, 1, 1, VM_POLY, 0, 107, 100, 110, 100, 76, 0, 80, 30, 40, 112, 20, 112, 64, FT_LP12, 0, 80, NOLFO, (CT_TILT, 60, 110, 127)),
    PR("swell", RL_PAD, EG_ORGAN, 8, 3, 1, 1, VM_POLY, 0, 89, 70, 100, 127, 110, 0, 80, 64, 80, 58, 0, 64, 64, FT_LP12, 0, 45, NOLFO, (CT_TILT, 90, 20, 127)),
    PR("steam", RL_FX, EG_ORGAN, 6, 1, 7, 5, VM_POLY, 0, 127, 80, 90, 0, 100, 0, 80, 60, 40, 126, 0, 100, 64, FT_LP12, 10, 0, NOLFO, (CT_RING, 80, 30, 127)),
    /* sync: wire, drawn hard and bright; hard sync. reach, sub, hiss, thin. wire stab a sync stab; wire bass a sync
     * bass; gauge sub the plain saw's sub under a low filter; zipper the Cars' "Let's Go" lead (the follower's pitch
     * falling across each note); filament a sync lead with vibrato; solder a screaming lead (driven, the ladder);
     * staple a crushed pluck; coil a sync clavinet; tinsel a bright sync bell; rivet a sync brass; mesh strings (the
     * reach moving slowly, as a PWM would); steel pad a ring-modulated pad; foil sync chords, gated; frayed the reach
     * wandering; barbed the hiss band-passed, a wind; spark the reach falling every sixteenth, zaps */
    PR("wire stab", RL_STAB, EG_SYNC, 57, 20, 0, 0, VM_POLY, 0, 127, 0, 66, 20, 40, 0, 80, 0, 40, 88, 20, 64, 64, FT_LP24, 0, 10, NOLFO, (CT_DRIVE, 60, 110, 127)),
    PR("wire bass", RL_BASS, EG_SYNC, 53, 80, 0, 0, VM_MONO, 0, 109, 10, 60, 120, 85, 0, 62, 0, 30, 76, 50, 90, 40, FT_LP12, 0, 0, NOLFO, (CT_DRIVE, 65, 80, 127)),
    PR("gauge sub", RL_BASS, EG_SYNC, 76, 127, 0, 14, VM_MONO, 0, 127, 30, 80, 120, 85, 0, 60, 0, 40, 36, 20, 64, 30, FT_LADDER, 0, 0, NOLFO, (CT_DRIVE, 20, 0, 127)),
    PR("zipper", RL_LEAD, EG_SYNC, 70, 0, 0, 20, VM_LEGATO, 15, 97, 40, 80, 127, 36, 0, 80, 30, 40, 96, 10, 88, 64, FT_LP12, 0, 0, (LT_WAVE, 1, 92, LD_SYNTH, 0, 3), (CT_TILT, 30, 110, 127)),
    PR("filament", RL_LEAD, EG_SYNC, 69, 0, 0, 90, VM_LEGATO, 20, 119, 56, 60, 100, 72, 0, 88, 0, 40, 120, 50, 88, 64, FT_LP12, 25, 0, VIB(30), NOCH),
    PR("solder", RL_LEAD, EG_SYNC, 90, 68, 0, 0, VM_LEGATO, 30, 108, 56, 80, 127, 36, 0, 80, 60, 50, 72, 50, 90, 64, FT_LADDER, 0, 0, VIB(40), (CT_DRIVE, 90, 110, 127)),
    PR("staple", RL_PLUCK, EG_SYNC, 90, 62, 60, 16, VM_POLY, 0, 127, 0, 90, 10, 85, 0, 40, 0, 30, 78, 0, 64, 64, FT_LP24, 35, 8, NOLFO, (CT_CRUSH, 60, 50, 127)),
    PR("coil", RL_KEYS, EG_SYNC, 60, 0, 0, 80, VM_POLY, 0, 107, 20, 70, 0, 84, 0, 64, 0, 40, 74, 30, 76, 64, FT_LP24, 0, 0, NOLFO, (CT_TILT, 75, 20, 127)),
    PR("tinsel", RL_BELL, EG_SYNC, 73, 0, 20, 84, VM_POLY, 0, 115, 0, 106, 0, 80, 0, 60, 0, 50, 95, 5, 64, 64, FT_LP12, 0, 25, NOLFO, (CT_DRIVE, 60, 120, 127)),
    PR("rivet", RL_BRASS, EG_SYNC, 54, 30, 0, 0, VM_POLY, 0, 78, 68, 92, 110, 64, 0, 80, 60, 64, 50, 28, 112, 64, FT_LP24, 0, 18, VIB(20), (CT_TILT, 60, 95, 127)),
    PR("mesh", RL_STRINGS, EG_SYNC, 28, 27, 0, 61, VM_POLY, 0, 104, 100, 90, 100, 92, 0, 80, 64, 80, 80, 0, 64, 64, FT_LP12, 0, 20, (LT_WAVE, 40, 90, LD_SYNTH + LD_FREE, 0, 1), (CT_TILT, 45, 50, 127)),
    PR("steel pad", RL_PAD, EG_SYNC, 95, 0, 12, 12, VM_POLY, 0, 76, 49, 80, 127, 112, 0, 80, 64, 80, 92, 35, 64, 64, FT_LP12, 0, 60, (LT_WAVE, 28, 86, LD_SYNTH + LD_FREE, 0, 1), (CT_RING, 78, 98, 105)),
    PR("foil", RL_CHORD, EG_SYNC, 36, 0, 7, 0, VM_POLY, 0, 78, 30, 90, 100, 80, 0, 80, 0, 40, 89, 0, 100, 64, FT_LP12, 0, 0, NOLFO, (CT_TILT, 50, 80, 127)),
    PR("frayed", RL_FX, EG_SYNC, 127, 0, 100, 64, VM_POLY, 0, 118, 80, 72, 50, 100, 0, 56, 0, 50, 83, 35, 88, 64, FT_LP12, 20, 20, (LT_DRIFT, 70, 60, LD_SYNTH, 0, 64), (CT_FOLD, 70, 60, 127)),
    PR("barbed", RL_FX, EG_SYNC, 54, 0, 106, 127, VM_POLY, 0, 119, 80, 100, 110, 100, 0, 80, 64, 80, 102, 25, 64, 64, FT_LADDER, 30, 20, (LT_DRIFT, 30, 100, LD_FILTER + LD_FREE, 0, 64), NOCH),
    PR("spark", RL_FX, EG_SYNC, 127, 62, 30, 108, VM_POLY, 0, 127, 20, 80, 0, 100, 0, 88, 0, 40, 90, 35, 64, 64, FT_LP24, 40, 0, (LT_WAVE, 5, 127, LD_SYNTH, 0, 3), (CT_TILT, 90, 95, 127)),
    /* swarm: fleece, soft and a little wild; two sines pushed by noise. swarm, ring, tone, spread. mohair soft
     * keys; fuzz bass a folded bass; vicuna a sine sub, barely moving; glint lead a high-passed screaming lead;
     * angora a theremin (the sine, a long glide, a wide vibrato); alpaca a pan flute (breath in the pitch); carded
     * an FM pluck; shearling an FM bell; burr a fuzz stab; cashmere soft chords; merino an "ooh" choir (band-passed,
     * slow); fleece pad a driven pad; gossamer a ring-modulated shimmer; worsted dark strings; roving the tone
     * stepping in eighths; breeze the swarm at full, a wind */
    PR("mohair", RL_KEYS, EG_SWARM, 30, 10, 55, 32, VM_POLY, 0, 121, 0, 90, 40, 70, 0, 60, 0, 40, 110, 5, 64, 64, FT_LP12, 0, 6, NOLFO, NOCH),
    PR("fuzz bass", RL_BASS, EG_SWARM, 24, 25, 70, 0, VM_MONO, 0, 124, 0, 70, 120, 85, 0, 56, 30, 30, 90, 35, 76, 40, FT_LADDER, 0, 0, NOLFO, (CT_FOLD, 95, 50, 127)),
    PR("vicuna", RL_BASS, EG_SWARM, 0, 0, 0, 0, VM_MONO, 0, 127, 48, 80, 90, 85, 0, 60, 0, 40, 36, 20, 64, 30, FT_LADDER, 0, 0, NOLFO, (CT_DRIVE, 0, 10, 127)),
    PR("glint lead", RL_LEAD, EG_SWARM, 18, 35, 127, 99, VM_LEGATO, 31, 109, 0, 80, 127, 78, 0, 60, 64, 40, 62, 58, 64, 64, FT_HP12, 0, 0, (LT_WOBBLE, 90, 97, 64, 64, 0), (CT_DRIVE, 117, 117, 127)),
    PR("angora", RL_LEAD, EG_SWARM, 4, 0, 0, 0, VM_LEGATO, 70, 81, 40, 80, 120, 78, 0, 80, 30, 40, 120, 0, 100, 64, FT_LP12, 25, 0, (LT_WOBBLE, 90, 110, 64, 30, 0), NOCH),
    PR("alpaca", RL_LEAD, EG_SWARM, 74, 0, 8, 10, VM_MONO, 0, 112, 56, 80, 110, 78, 0, 60, 0, 40, 85, 50, 64, 64, FT_LP12, 15, 10, VIB(40), (CT_TILT, 30, 35, 127)),
    PR("carded", RL_PLUCK, EG_SWARM, 20, 6, 77, 20, VM_POLY, 0, 127, 0, 72, 0, 60, 0, 64, 0, 40, 120, 12, 64, 64, FT_LP24, 25, 0, NOLFO, NOCH),
    PR("shearling", RL_BELL, EG_SWARM, 10, 0, 83, 50, VM_POLY, 0, 109, 0, 104, 0, 100, 0, 60, 0, 40, 118, 0, 64, 64, FT_LP12, 0, 10, NOLFO, (CT_TILT, 45, 95, 127)),
    PR("burr", RL_STAB, EG_SWARM, 87, 96, 46, 60, VM_POLY, 0, 127, 0, 76, 20, 40, 0, 60, 0, 40, 88, 20, 64, 64, FT_LP24, 0, 0, NOLFO, (CT_FOLD, 110, 80, 127)),
    PR("cashmere", RL_CHORD, EG_SWARM, 8, 0, 0, 16, VM_POLY, 0, 116, 30, 60, 70, 90, 0, 64, 0, 40, 50, 5, 112, 64, FT_LP12, 0, 0, NOLFO, (CT_TILT, 60, 110, 127)),
    PR("merino", RL_VOICE, EG_SWARM, 20, 0, 32, 67, VM_POLY, 0, 127, 100, 100, 127, 84, 0, 60, 0, 40, 50, 20, 64, 64, FT_LP24, 10, 20, (LT_WOBBLE, 70, 72, 64, 30, 0), (CT_TILT, 75, 110, 127)),
    PR("fleece pad", RL_PAD, EG_SWARM, 50, 40, 103, 103, VM_POLY, 0, 103, 100, 100, 120, 110, 0, 80, 64, 80, 95, 5, 64, 64, FT_LP12, 0, 70, (LT_DRIFT, 30, 70, LD_SYNTH + LD_FREE, 2, 64), (CT_DRIVE, 127, 127, 127)),
    PR("gossamer", RL_PAD, EG_SWARM, 78, 0, 111, 65, VM_POLY, 0, 127, 108, 85, 100, 115, 0, 60, 0, 40, 112, 0, 64, 64, FT_LP12, 10, 70, (LT_WAVE, 30, 90, LD_SYNTH + LD_FREE, 2, 1), (CT_RING, 86, 80, 127)),
    PR("worsted", RL_STRINGS, EG_SWARM, 0, 60, 40, 40, VM_POLY, 0, 77, 60, 100, 127, 100, 0, 64, 60, 40, 58, 10, 88, 64, FT_LP24, 0, 50, NOLFO, (CT_TILT, 60, 35, 127)),
    PR("roving", RL_FX, EG_SWARM, 40, 44, 60, 50, VM_POLY, 0, 127, 80, 90, 0, 50, 0, 64, 30, 40, 64, 20, 76, 64, FT_LP12, 30, 20, (LT_WAVE, 4, 110, LD_SYNTH, 2, 0), (CT_TILT, 75, 80, 127)),
    PR("breeze", RL_FX, EG_SWARM, 103, 23, 16, 70, VM_POLY, 0, 127, 100, 90, 120, 100, 0, 80, 64, 80, 66, 67, 64, 30, FT_LP12, 0, 70, (LT_WAVE, 15, 107, LD_FILTER + LD_FREE, 0, 1), NOCH),
    /* sample: jute and the bast fibres, the bank's own sounds played by the keys. sample (0 kick, 1 snare, 2 hat,
     * 3 clap, 4 ah, 5 piano, 6 sweep, 7 break; then the user bank), start, length, mode (one-shot, loop, reverse,
     * pingpong). jute keys the piano; hemp bass the kick as a short bass; kenaf the kick as an 808's long boom;
     * hessian a sung lead, crushed; abaca the hat as a ticking percussion; coir the snare ping-ponged; nettle the
     * clap crushed; flax bell the piano rung; roselle the M1's piano stab (short, high-passed); raffia the piano
     * backwards; urena the piano's middle looped, an organ-like chord; jute choir the ah looped; jute pad the ah
     * slow and dark; henequen the break played as a pitched one-shot; sisal sweep the sweep ping-ponged; istle the
     * sweep backwards, a riser */
    PR("jute keys", RL_KEYS, EG_SAMPLE, 5, 7, 110, SM_ONESHOT, VM_POLY, 0, 120, 0, 76, 127, 56, 0, 80, 0, 50, 98, 5, 88, 64, FT_LP12, 0, 0, NOLFO, (CT_DRIVE, 45, 70, 127)),
    PR("hemp bass", RL_BASS, EG_SAMPLE, 0, 0, 127, SM_ONESHOT, VM_MONO, 0, 127, 0, 70, 120, 40, 0, 60, 0, 30, 80, 10, 84, 40, FT_LP24, 0, 0, NOLFO, (CT_DRIVE, 55, 60, 127)),
    PR("kenaf", RL_BASS, EG_SAMPLE, 0, 24, 103, SM_ONESHOT, VM_MONO, 0, 127, 48, 100, 0, 80, 0, 60, 0, 40, 64, 0, 64, 30, FT_LP12, 0, 0, NOLFO, (CT_DRIVE, 60, 30, 127)),
    PR("hessian", RL_LEAD, EG_SAMPLE, 4, 49, 0, SM_PINGPONG, VM_MONO, 10, 127, 72, 70, 100, 50, 0, 66, 64, 50, 52, 31, 64, 64, FT_LP24, 0, 5, NOLFO, (CT_CRUSH, 89, 118, 127)),
    PR("abaca", RL_DRUM, EG_SAMPLE, 2, 0, 127, SM_ONESHOT, VM_POLY, 0, 127, 0, 70, 0, 50, 0, 60, 0, 40, 127, 0, 64, 64, FT_LP12, 30, 0, NOLFO, (CT_TILT, 90, 95, 127)),
    PR("coir", RL_DRUM, EG_SAMPLE, 1, 0, 127, SM_PINGPONG, VM_POLY, 0, 127, 0, 80, 70, 70, 0, 88, 0, 40, 100, 40, 76, 64, FT_LP12, 15, 0, NOLFO, (CT_CRUSH, 15, 60, 127)),
    PR("nettle", RL_DRUM, EG_SAMPLE, 3, 7, 127, SM_ONESHOT, VM_POLY, 0, 127, 0, 90, 0, 80, 0, 60, 0, 40, 76, 28, 64, 64, FT_LP24, 0, 8, NOLFO, (CT_CRUSH, 10, 44, 127)),
    PR("flax bell", RL_BELL, EG_SAMPLE, 5, 0, 127, SM_ONESHOT, VM_POLY, 0, 127, 0, 92, 0, 85, 0, 80, 0, 70, 80, 5, 64, 64, FT_LP24, 0, 25, NOLFO, (CT_RING, 70, 114, 127)),
    PR("roselle", RL_STAB, EG_SAMPLE, 5, 0, 90, SM_ONESHOT, VM_POLY, 0, 127, 0, 60, 0, 50, 0, 60, 0, 40, 45, 20, 64, 64, FT_HP12, 18, 10, NOLFO, (CT_DRIVE, 100, 100, 127)),
    PR("raffia", RL_FX, EG_SAMPLE, 5, 7, 103, SM_REVERSE, VM_POLY, 0, 127, 80, 70, 0, 60, 0, 64, 0, 50, 112, 20, 64, 64, FT_LP12, 0, 0, NOLFO, (CT_TILT, 60, 110, 127)),
    PR("urena", RL_CHORD, EG_SAMPLE, 5, 30, 56, SM_LOOP, VM_POLY, 0, 127, 0, 80, 100, 80, 0, 60, 0, 40, 68, 0, 64, 64, FT_LP12, 15, 0, (LT_WOBBLE, 84, 70, 64, 64, 0), (CT_TILT, 60, 80, 127)),
    PR("jute choir", RL_VOICE, EG_SAMPLE, 4, 30, 117, SM_LOOP, VM_POLY, 0, 127, 76, 110, 100, 104, 0, 80, 64, 80, 50, 13, 64, 50, FT_LP12, 0, 55, (LT_WOBBLE, 71, 77, 64, 20, 0), (CT_CRUSH, 64, 81, 127)),
    PR("jute pad", RL_PAD, EG_SAMPLE, 4, 40, 127, SM_LOOP, VM_POLY, 0, 91, 100, 100, 90, 110, 0, 64, 0, 40, 55, 20, 88, 64, FT_LP24, 10, 50, (LT_DRIFT, 25, 60, LD_FILTER + LD_FREE, 0, 64), (CT_TILT, 30, 95, 127)),
    PR("henequen", RL_LOOP, EG_SAMPLE, 7, 0, 96, SM_ONESHOT, VM_POLY, 0, 109, 0, 80, 127, 40, 0, 72, 0, 40, 116, 5, 112, 0, FT_LP12, 0, 0, NOLFO, (CT_DRIVE, 30, 120, 127)),
    PR("sisal sweep", RL_FX, EG_SAMPLE, 6, 24, 127, SM_PINGPONG, VM_POLY, 0, 127, 80, 90, 100, 90, 50, 80, 50, 80, 105, 20, 64, 64, FT_LADDER, 0, 20, (LT_DRIFT, 40, 70, LD_FILTER + LD_FREE, 0, 64), (CT_TILT, 30, 35, 127)),
    PR("istle", RL_FX, EG_SAMPLE, 6, 0, 110, SM_REVERSE, VM_POLY, 0, 127, 0, 70, 127, 80, 0, 60, 0, 40, 76, 30, 64, 64, FT_HP12, 0, 0, NOLFO, NOCH),
    /* slice: the loom itself and its parts, a break cut and woven again. sample (7 the break), slices (0 4, 1 8,
     * 2 16, 3 32), pitch (64 = 0, +-24 semitones), gate. Keytrack 0 (every slice the same filter), sustain full
     * and a long release: the gate decides how much plays, not the step's length. loom break the break straight;
     * chop sixteen, mono; shuttle thirty-two, driven; heddle four, an octave down, heavy; treadle up a fifth,
     * clipped; bobbin short plucked chops; selvedge thin, high-passed (the hats); warp beam legato, down, the filter
     * on the beat; batten boom bap (down a fourth, dark, driven); pirn jungle (up, sixteen, mono, bright); sley lo-fi
     * (an SP-style crush, open); picker a stutter (sixteen, short); harness ring-modulated; shed the kicks alone
     * (low-passed, resonant); lease dub (band-passed into the delay); ratchet a tape stop (the pitch falling across
     * a note, legato) */
    PR("loom break", RL_LOOP, EG_SLICE, SMP_BREAK, 1, 64, 120, VM_POLY, 0, 121, 0, 80, 127, 120, 0, 60, 0, 40, 127, 0, 64, 0, FT_LP12, 0, 8, NOLFO, (CT_TILT, 90, 95, 127)),
    PR("chop", RL_LOOP, EG_SLICE, SMP_BREAK, 2, 64, 63, VM_MONO, 0, 127, 0, 80, 127, 40, 0, 64, 0, 40, 92, 5, 64, 0, FT_LP12, 0, 8, NOLFO, (CT_DRIVE, 30, 0, 127)),
    PR("shuttle", RL_LOOP, EG_SLICE, SMP_BREAK, 3, 64, 103, VM_POLY, 0, 127, 0, 80, 127, 40, 0, 60, 0, 40, 106, 12, 64, 0, FT_LP12, 0, 0, NOLFO, (CT_DRIVE, 127, 115, 127)),
    PR("heddle", RL_LOOP, EG_SLICE, SMP_BREAK, 0, 52, 127, VM_MONO, 0, 127, 0, 80, 127, 90, 0, 60, 0, 40, 102, 28, 64, 0, FT_LP24, 0, 0, NOLFO, (CT_DRIVE, 30, 30, 127)),
    PR("treadle", RL_LOOP, EG_SLICE, SMP_BREAK, 1, 71, 95, VM_POLY, 0, 127, 0, 80, 127, 40, 0, 80, 0, 40, 100, 0, 64, 0, FT_LP12, 10, 0, NOLFO, (CT_TILT, 50, 90, 127)),
    PR("batten", RL_LOOP, EG_SLICE, SMP_BREAK, 1, 59, 96, VM_POLY, 0, 127, 0, 80, 127, 40, 0, 80, 0, 40, 36, 5, 76, 0, FT_LP24, 0, 8, NOLFO, (CT_DRIVE, 65, 40, 127)),
    PR("pirn", RL_LOOP, EG_SLICE, SMP_BREAK, 2, 69, 120, VM_MONO, 0, 127, 0, 80, 127, 40, 0, 72, 0, 40, 107, 0, 76, 0, FT_LP12, 0, 0, NOLFO, (CT_TILT, 30, 20, 127)),
    PR("sley", RL_LOOP, EG_SLICE, SMP_BREAK, 1, 61, 127, VM_POLY, 0, 127, 0, 80, 127, 90, 0, 60, 0, 40, 107, 0, 64, 0, FT_LP12, 0, 8, NOLFO, (CT_CRUSH, 60, 64, 127)),
    PR("bobbin", RL_DRUM, EG_SLICE, SMP_BREAK, 2, 64, 32, VM_POLY, 0, 127, 0, 80, 127, 70, 0, 80, 30, 30, 50, 28, 64, 0, FT_LP24, 0, 0, NOLFO, (CT_RING, 70, 117, 60)),
    PR("selvedge", RL_DRUM, EG_SLICE, SMP_BREAK, 1, 64, 69, VM_POLY, 0, 127, 0, 80, 127, 40, 0, 88, 30, 40, 70, 0, 112, 0, FT_HP12, 0, 0, NOLFO, (CT_TILT, 30, 80, 127)),
    PR("picker", RL_DRUM, EG_SLICE, SMP_BREAK, 2, 64, 34, VM_POLY, 0, 124, 0, 80, 127, 50, 0, 88, 0, 40, 119, 0, 100, 0, FT_LP12, 20, 0, NOLFO, (CT_TILT, 90, 95, 127)),
    PR("shed", RL_DRUM, EG_SLICE, SMP_BREAK, 1, 52, 127, VM_POLY, 0, 127, 0, 80, 127, 90, 0, 60, 0, 40, 30, 40, 64, 0, FT_LP24, 20, 0, NOLFO, (CT_DRIVE, 60, 50, 127)),
    PR("warp beam", RL_FX, EG_SLICE, SMP_BREAK, 1, 59, 127, VM_LEGATO, 0, 127, 80, 70, 127, 50, 0, 60, 0, 40, 73, 30, 64, 0, FT_LP12, 0, 0, (LT_WAVE, 2, 90, LD_FILTER, 0, 1), (CT_CRUSH, 127, 0, 127)),
    PR("harness", RL_FX, EG_SLICE, SMP_BREAK, 1, 64, 120, VM_POLY, 0, 127, 20, 80, 127, 90, 0, 80, 0, 40, 104, 50, 64, 0, FT_BP12, 20, 30, NOLFO, (CT_RING, 70, 10, 127)),
    PR("lease", RL_FX, EG_SLICE, SMP_BREAK, 2, 64, 73, VM_POLY, 0, 127, 20, 80, 127, 100, 0, 72, 60, 40, 115, 50, 64, 0, FT_BP12, 60, 20, NOLFO, (CT_TILT, 90, 110, 127)),
    PR("ratchet", RL_FX, EG_SLICE, SMP_BREAK, 1, 52, 127, VM_LEGATO, 0, 127, 50, 80, 127, 90, 0, 60, 0, 40, 78, 0, 64, 0, FT_BP12, 0, 0, (LT_WAVE, 1, 48, LD_SYNTH, 2, 2), (CT_TILT, 90, 110, 127)),
    /* brocade: the rich cloth, woven with many threads; the supersaw, after NuSaw (the JP-8000's). saws (1..7),
     * detune, mix, spread. gold thread the trance lead; tweed bass a supersaw bass; tabby a Reese of three saws (legato, the ladder, the filter wandering);
     * chenille a mono lead, folded; zari one saw, a Minimoog-style lead; sequin the trance pluck; samite the
     * trance stab; lampas a supersaw brass; jacquard a string ensemble, wide; damask the classic pad; velvet a dark
     * pad; shot silk the detune moving; organza thin, high-passed; kinkhab a vocal pad (band-passed, its cutoff
     * wandering); tapestry chords, plucked and held; baldachin a riser (the detune opening over two bars) */
    PR("gold thread", RL_LEAD, EG_BROCADE, 7, 46, 100, 80, VM_LEGATO, 22, 92, 2, 70, 90, 78, 0, 60, 60, 50, 112, 15, 80, 64, FT_LP24, 0, 0, VIB(40), (CT_DRIVE, 20, 127, 127)),
    PR("tweed bass", RL_BASS, EG_BROCADE, 5, 30, 104, 68, VM_MONO, 0, 127, 0, 70, 60, 85, 0, 80, 0, 30, 42, 0, 64, 40, FT_LADDER, 0, 0, NOLFO, (CT_DRIVE, 60, 60, 127)),
    PR("tabby", RL_BASS, EG_BROCADE, 3, 33, 110, 60, VM_LEGATO, 40, 115, 48, 80, 60, 55, 0, 80, 0, 40, 36, 0, 88, 40, FT_LADDER, 0, 0, (LT_WAVE, 30, 92, LD_FILTER + LD_FREE, 0, 1), (CT_FOLD, 0, 60, 127)),
    PR("chenille", RL_LEAD, EG_BROCADE, 7, 110, 127, 100, VM_MONO, 60, 112, 0, 70, 127, 36, 0, 56, 0, 40, 86, 50, 76, 64, FT_LADDER, 20, 10, (LT_WOBBLE, 88, 90, 64, 40, 0), (CT_FOLD, 15, 90, 100)),
    PR("zari", RL_LEAD, EG_BROCADE, 1, 0, 0, 7, VM_LEGATO, 25, 127, 0, 80, 110, 50, 0, 88, 0, 50, 58, 50, 64, 64, FT_LADDER, 25, 0, VIB(30), (CT_DRIVE, 50, 80, 127)),
    PR("sequin", RL_PLUCK, EG_BROCADE, 7, 50, 100, 90, VM_POLY, 0, 110, 0, 84, 10, 40, 0, 66, 10, 44, 102, 22, 100, 64, FT_LP24, 30, 8, NOLFO, (CT_DRIVE, 70, 120, 127)),
    PR("samite", RL_STAB, EG_BROCADE, 7, 60, 100, 90, VM_POLY, 0, 83, 0, 80, 0, 40, 0, 64, 30, 50, 64, 28, 76, 64, FT_LP24, 0, 0, NOLFO, (CT_TILT, 45, 110, 127)),
    PR("lampas", RL_BRASS, EG_BROCADE, 5, 25, 80, 80, VM_POLY, 0, 127, 68, 84, 80, 56, 0, 56, 0, 66, 35, 20, 88, 64, FT_LP24, 0, 0, VIB(20), (CT_TILT, 30, 50, 127)),
    PR("jacquard", RL_STRINGS, EG_BROCADE, 5, 44, 90, 103, VM_POLY, 0, 82, 100, 100, 100, 80, 0, 64, 0, 80, 74, 0, 64, 64, FT_LP12, 0, 45, NOLFO, (CT_TILT, 30, 35, 127)),
    PR("damask", RL_PAD, EG_BROCADE, 7, 40, 110, 127, VM_POLY, 0, 70, 92, 90, 100, 110, 80, 90, 60, 90, 78, 0, 74, 50, FT_LP12, 0, 70, (LT_WAVE, 25, 84, LD_FILTER + LD_FREE, 0, 1), (CT_TILT, 45, 80, 127)),
    PR("velvet", RL_PAD, EG_BROCADE, 7, 3, 62, 70, VM_POLY, 0, 127, 108, 100, 110, 110, 30, 80, 60, 80, 50, 5, 70, 64, FT_LP24, 0, 55, NOLFO, (CT_TILT, 30, 110, 127)),
    PR("shot silk", RL_PAD, EG_BROCADE, 7, 70, 110, 100, VM_POLY, 0, 73, 60, 100, 100, 110, 0, 80, 0, 80, 72, 0, 76, 64, FT_LP12, 10, 20, (LT_WAVE, 22, 88, LD_SYNTH + LD_FREE, 1, 1), (CT_TILT, 75, 95, 127)),
    PR("organza", RL_PAD, EG_BROCADE, 7, 70, 113, 110, VM_POLY, 0, 121, 108, 90, 110, 80, 0, 80, 0, 60, 51, 12, 88, 64, FT_HP12, 15, 40, NOLFO, (CT_TILT, 45, 80, 127)),
    PR("kinkhab", RL_VOICE, EG_BROCADE, 7, 26, 100, 100, VM_POLY, 0, 123, 88, 100, 120, 84, 0, 60, 0, 40, 72, 60, 64, 64, FT_BP12, 0, 20, (LT_WAVE, 30, 90, LD_FILTER + LD_FREE, 0, 1), (CT_TILT, 45, 50, 127)),
    PR("tapestry", RL_CHORD, EG_BROCADE, 5, 16, 90, 90, VM_POLY, 0, 81, 30, 90, 40, 64, 0, 72, 0, 60, 75, 0, 88, 64, FT_LP24, 0, 8, NOLFO, (CT_TILT, 45, 80, 127)),
    PR("baldachin", RL_FX, EG_BROCADE, 7, 20, 76, 103, VM_POLY, 0, 127, 90, 110, 120, 100, 0, 60, 0, 40, 30, 30, 64, 64, FT_LP24, 20, 50, (LT_WAVE, 0, 120, LD_SYNTH, 1, 2), NOCH),
    /* cane: rattan, reed and bamboo, the woven and the blown; Braids' physical models. model (pluck, bow, reed,
     * flute, bell, drum), timbre, color, damp. wicker harp a harp (Karplus-Strong, long); rattan bass a plucked
     * bass; thatch a bowed bass; willow a violin (legato, vibrato); basket reed a clarinet; bamboo a flute; sugar a
     * shawm (the reed hard-blown, driven); osier a koto (short, bright); lacquer a sitar-like pluck (folded); cane
     * cello a cello; cane field a bowed pad; rush a breathy flute pad; temple bell a bell; malacca a gong that
     * drones; hand drum a tuned drum; kooboo a low tom */
    PR("wicker harp", RL_PLUCK, EG_CANE, 0, 65, 50, 10, VM_POLY, 0, 79, 0, 100, 100, 70, 0, 56, 0, 60, 98, 20, 112, 64, FT_LP12, 25, 0, NOLFO, (CT_TILT, 60, 110, 127)),
    PR("rattan bass", RL_BASS, EG_CANE, 0, 95, 90, 40, VM_MONO, 0, 116, 0, 60, 100, 70, 0, 60, 20, 40, 88, 10, 76, 30, FT_LADDER, 0, 0, NOLFO, (CT_DRIVE, 90, 100, 127)),
    PR("thatch", RL_BASS, EG_CANE, 1, 80, 2, 54, VM_MONO, 0, 127, 40, 90, 90, 30, 0, 60, 0, 40, 86, 50, 64, 30, FT_LP24, 0, 0, NOLFO, (CT_TILT, 45, 110, 127)),
    PR("willow", RL_LEAD, EG_CANE, 1, 70, 74, 24, VM_LEGATO, 20, 127, 50, 90, 90, 60, 0, 80, 0, 40, 108, 35, 112, 64, FT_LADDER, 0, 8, VIB(30), (CT_TILT, 60, 110, 127)),
    PR("basket reed", RL_LEAD, EG_CANE, 2, 80, 36, 13, VM_LEGATO, 15, 120, 40, 80, 60, 50, 0, 60, 0, 60, 100, 0, 64, 64, FT_LP24, 15, 0, VIB(50), (CT_TILT, 60, 20, 127)),
    PR("bamboo", RL_LEAD, EG_CANE, 3, 50, 64, 0, VM_MONO, 0, 97, 36, 80, 110, 78, 0, 64, 60, 60, 114, 10, 64, 64, FT_LP12, 25, 10, VIB(60), (CT_TILT, 75, 50, 127)),
    PR("sugar", RL_BRASS, EG_CANE, 2, 93, 117, 24, VM_POLY, 0, 94, 68, 76, 120, 56, 0, 56, 30, 40, 60, 0, 76, 64, FT_LP24, 0, 0, VIB(50), (CT_DRIVE, 20, 50, 127)),
    PR("osier", RL_PLUCK, EG_CANE, 0, 44, 114, 0, VM_POLY, 0, 96, 0, 80, 0, 55, 0, 56, 0, 40, 76, 0, 88, 64, FT_BP12, 0, 8, NOLFO, NOCH),
    PR("lacquer", RL_PLUCK, EG_CANE, 0, 60, 44, 60, VM_POLY, 0, 127, 0, 60, 10, 70, 0, 60, 0, 40, 115, 0, 64, 64, FT_LP12, 0, 0, NOLFO, (CT_FOLD, 50, 90, 127)),
    PR("cane cello", RL_STRINGS, EG_CANE, 1, 46, 33, 41, VM_LEGATO, 20, 127, 100, 90, 110, 100, 0, 60, 0, 60, 95, 20, 64, 64, FT_LP24, 0, 35, VIB(40), (CT_TILT, 90, 110, 127)),
    PR("cane field", RL_PAD, EG_CANE, 1, 50, 90, 26, VM_POLY, 0, 88, 80, 100, 110, 110, 0, 60, 0, 60, 65, 20, 64, 64, FT_LP12, 0, 35, (LT_WAVE, 25, 84, LD_SYNTH + LD_FREE, 2, 1), (CT_TILT, 60, 35, 127)),
    PR("rush", RL_PAD, EG_CANE, 3, 0, 84, 19, VM_POLY, 0, 113, 100, 100, 120, 110, 0, 60, 0, 40, 94, 28, 64, 64, FT_LP12, 0, 45, (LT_WOBBLE, 40, 72, 64, 30, 0), (CT_TILT, 30, 35, 127)),
    PR("temple bell", RL_BELL, EG_CANE, 4, 100, 30, 30, VM_POLY, 0, 98, 0, 100, 127, 108, 0, 60, 0, 60, 126, 20, 64, 64, FT_LP12, 0, 50, NOLFO, (CT_TILT, 75, 20, 127)),
    PR("malacca", RL_BELL, EG_CANE, 4, 127, 86, 0, VM_POLY, 0, 127, 0, 112, 0, 80, 0, 80, 0, 40, 90, 5, 100, 64, FT_BP12, 0, 40, NOLFO, (CT_TILT, 45, 110, 127)),
    PR("hand drum", RL_DRUM, EG_CANE, 5, 70, 100, 20, VM_POLY, 0, 107, 0, 95, 0, 60, 0, 50, 0, 60, 92, 0, 110, 32, FT_LP24, 0, 25, NOLFO, (CT_FOLD, 95, 64, 127)),
    PR("kooboo", RL_DRUM, EG_CANE, 5, 11, 6, 60, VM_POLY, 0, 127, 0, 90, 0, 50, 0, 56, 60, 40, 118, 28, 100, 64, FT_LP24, 0, 8, NOLFO, (CT_DRIVE, 20, 0, 127)),
    /* voile: the sheer cloth; Braids' voices. model (vosim, vowel, choir, whisper), timbre, color, breath. voile
     * choir a choir; basso a bass voice; batiste a VOSIM bass; sheer lead a vowel lead; mousseline a talk box (the
     * vowel moving); tulle a VOSIM keys; chatter the vowels babbling; doo wop a doo-wop backing; illusion a vowel
     * plucked; tissue a VOSIM bell; organdy a choir stab (the orchestra hit's way); tarlatan robot chords (a
     * vocoder's, crushed); ninon a soprano choir; veil a whispered pad; net curtain a breathing pad; lisse a wind
     * that sings (the whisper's color drifting) */
    PR("voile choir", RL_VOICE, EG_VOILE, 2, 0, 84, 27, VM_POLY, 0, 81, 80, 100, 120, 104, 0, 60, 0, 60, 94, 20, 64, 64, FT_LP12, 0, 60, (LT_DRIFT, 30, 30, LD_SYNTH + LD_FREE, 1, 64), (CT_TILT, 90, 20, 127)),
    PR("basso", RL_BASS, EG_VOILE, 2, 100, 0, 41, VM_MONO, 0, 127, 40, 70, 110, 85, 0, 60, 0, 60, 63, 50, 64, 64, FT_LP24, 0, 0, VIB(60), (CT_TILT, 30, 95, 127)),
    PR("batiste", RL_BASS, EG_VOILE, 0, 64, 84, 0, VM_MONO, 0, 100, 48, 100, 120, 85, 0, 88, 0, 40, 44, 50, 76, 40, FT_LP24, 0, 0, NOLFO, (CT_DRIVE, 10, 50, 127)),
    PR("sheer lead", RL_LEAD, EG_VOILE, 0, 70, 102, 0, VM_LEGATO, 25, 92, 20, 90, 127, 78, 0, 60, 0, 60, 100, 35, 64, 64, FT_LP24, 0, 0, VIB(30), (CT_DRIVE, 0, 100, 127)),
    PR("mousseline", RL_LEAD, EG_VOILE, 1, 40, 64, 24, VM_LEGATO, 20, 127, 0, 70, 120, 78, 0, 60, 0, 40, 79, 50, 64, 64, FT_LP12, 0, 0, (LT_WAVE, 60, 120, LD_SYNTH, 1, 1), (CT_DRIVE, 20, 60, 127)),
    PR("tulle", RL_KEYS, EG_VOILE, 0, 127, 127, 24, VM_POLY, 0, 93, 20, 76, 20, 84, 0, 64, 30, 60, 120, 28, 88, 64, FT_LP24, 0, 0, NOLFO, (CT_TILT, 90, 110, 127)),
    PR("chatter", RL_FX, EG_VOILE, 1, 64, 40, 57, VM_POLY, 0, 127, 50, 80, 120, 100, 0, 56, 30, 60, 102, 0, 100, 64, FT_BP12, 20, 10, (LT_WAVE, 3, 100, LD_SYNTH, 1, 1), (CT_TILT, 45, 80, 127)),
    PR("doo wop", RL_VOICE, EG_VOILE, 1, 100, 30, 80, VM_POLY, 0, 127, 0, 90, 40, 50, 0, 60, 0, 60, 14, 0, 64, 127, FT_HP12, 15, 35, NOLFO, (CT_DRIVE, 76, 100, 127)),
    PR("illusion", RL_PLUCK, EG_VOILE, 1, 56, 119, 0, VM_POLY, 0, 127, 0, 74, 10, 40, 0, 60, 0, 40, 88, 5, 64, 64, FT_LP12, 25, 8, NOLFO, (CT_TILT, 75, 110, 127)),
    PR("tissue", RL_BELL, EG_VOILE, 0, 117, 120, 0, VM_POLY, 0, 127, 0, 106, 20, 108, 0, 88, 30, 40, 114, 0, 76, 64, FT_BP12, 15, 40, NOLFO, (CT_TILT, 45, 35, 127)),
    PR("organdy", RL_STAB, EG_VOILE, 2, 44, 74, 66, VM_POLY, 0, 127, 0, 100, 0, 55, 0, 56, 0, 60, 45, 28, 64, 64, FT_LP24, 18, 0, NOLFO, (CT_DRIVE, 60, 127, 127)),
    PR("tarlatan", RL_CHORD, EG_VOILE, 0, 57, 54, 0, VM_POLY, 0, 118, 0, 90, 40, 64, 0, 72, 0, 40, 50, 0, 64, 64, FT_LP24, 0, 10, NOLFO, (CT_CRUSH, 80, 94, 127)),
    PR("ninon", RL_VOICE, EG_VOILE, 2, 60, 127, 17, VM_POLY, 0, 122, 100, 90, 120, 94, 0, 88, 0, 40, 110, 0, 112, 64, FT_BP12, 0, 50, (LT_WOBBLE, 84, 72, 64, 20, 0), (CT_TILT, 30, 110, 127)),
    PR("veil", RL_PAD, EG_VOILE, 3, 107, 70, 127, VM_POLY, 0, 127, 100, 100, 110, 88, 0, 56, 30, 60, 76, 20, 100, 64, FT_LP24, 0, 50, (LT_WAVE, 20, 80, LD_SYNTH + LD_FREE, 2, 1), (CT_TILT, 90, 95, 127)),
    PR("net curtain", RL_PAD, EG_VOILE, 3, 36, 118, 94, VM_POLY, 0, 118, 100, 90, 120, 100, 0, 60, 0, 60, 112, 0, 64, 64, FT_LP12, 0, 60, NOLFO, (CT_TILT, 75, 35, 127)),
    PR("lisse", RL_FX, EG_VOILE, 3, 127, 100, 127, VM_POLY, 0, 127, 80, 100, 110, 100, 0, 60, 0, 40, 100, 20, 64, 64, FT_HP12, 20, 50, (LT_DRIFT, 20, 100, LD_SYNTH + LD_FREE, 2, 64), NOCH),
    /* gingham: the checked cotton of picnics and pinafores; Braids' toys and chips. model (toy, chord, ring, peak,
     * band, noise), timbre, color, arp (off, oct, fifth, power, major, minor, maj7, min7, sus4). picnic a toy lead;
     * check bass a toy bass; hamper the NES's triangle bass (squares under a low filter); bunting the NES's pulse lead
     * (vibrato); arcade the chip's major arpeggio, an octave and a twelfth; pinwheel a toy's maj7 arpeggio; kite a
     * sus4 arpeggio plucked; pinafore ring-modulated sines struck; parasol the peak filter as keys; pixel the peak
     * stepping; plaid pad a chord pad; apron the band filter swelling, strings; tea towel noise high-passed, a hat;
     * jam jar the noise as a chip snare; bandana the band filter's wah each eighth; deckchair a toy piano, worn (wow) */
    PR("picnic", RL_LEAD, EG_GINGHAM, 0, 110, 37, 0, VM_MONO, 0, 89, 0, 70, 90, 50, 0, 60, 0, 60, 92, 0, 64, 64, FT_LP12, 0, 0, VIB(30), (CT_TILT, 90, 20, 127)),
    PR("check bass", RL_BASS, EG_GINGHAM, 0, 114, 0, 0, VM_MONO, 0, 127, 48, 70, 60, 85, 0, 80, 60, 40, 100, 20, 88, 30, FT_LADDER, 0, 0, NOLFO, (CT_DRIVE, 100, 110, 127)),
    PR("hamper", RL_BASS, EG_GINGHAM, 1, 26, 78, 0, VM_MONO, 0, 103, 0, 80, 120, 70, 0, 72, 0, 40, 83, 0, 112, 40, FT_LP24, 0, 0, NOLFO, NOCH),
    PR("bunting", RL_LEAD, EG_GINGHAM, 1, 40, 66, 0, VM_MONO, 0, 127, 56, 90, 60, 50, 0, 60, 0, 40, 108, 50, 64, 64, FT_LP12, 0, 0, VIB(40), (CT_TILT, 75, 35, 127)),
    PR("arcade", RL_CHORD, EG_GINGHAM, 1, 127, 127, 4, VM_POLY, 0, 127, 0, 100, 70, 80, 0, 60, 0, 60, 102, 28, 64, 64, FT_BP12, 20, 10, NOLFO, NOCH),
    PR("pinwheel", RL_CHORD, EG_GINGHAM, 0, 60, 60, 6, VM_POLY, 0, 94, 50, 80, 70, 80, 0, 60, 0, 40, 120, 0, 64, 64, FT_LP12, 20, 10, NOLFO, (CT_TILT, 30, 35, 127)),
    PR("kite", RL_PLUCK, EG_GINGHAM, 1, 78, 78, 8, VM_POLY, 0, 127, 0, 72, 0, 85, 0, 60, 0, 40, 68, 0, 64, 64, FT_LP12, 15, 0, NOLFO, (CT_TILT, 45, 95, 127)),
    PR("pinafore", RL_BELL, EG_GINGHAM, 2, 66, 124, 0, VM_POLY, 0, 108, 0, 106, 20, 80, 0, 56, 30, 60, 103, 0, 76, 64, FT_LP12, 0, 40, NOLFO, (CT_TILT, 45, 95, 127)),
    PR("parasol", RL_KEYS, EG_GINGHAM, 3, 80, 107, 0, VM_POLY, 0, 97, 20, 90, 0, 40, 0, 80, 60, 40, 100, 0, 100, 64, FT_LP12, 12, 6, NOLFO, NOCH),
    PR("pixel", RL_FX, EG_GINGHAM, 3, 50, 51, 1, VM_MONO, 0, 127, 80, 80, 90, 80, 0, 80, 30, 60, 90, 10, 112, 64, FT_BP12, 20, 10, (LT_WAVE, 4, 90, LD_SYNTH, 1, 2), NOCH),
    PR("plaid pad", RL_PAD, EG_GINGHAM, 1, 78, 84, 0, VM_POLY, 0, 118, 108, 100, 90, 95, 0, 64, 60, 60, 75, 12, 76, 64, FT_LP12, 0, 60, (LT_WAVE, 25, 84, LD_FILTER + LD_FREE, 0, 1), (CT_TILT, 60, 20, 127)),
    PR("apron", RL_STRINGS, EG_GINGHAM, 4, 30, 60, 0, VM_POLY, 0, 122, 100, 110, 120, 100, 0, 80, 0, 60, 76, 12, 100, 64, FT_LP24, 0, 40, NOLFO, NOCH),
    PR("tea towel", RL_DRUM, EG_GINGHAM, 5, 127, 0, 0, VM_POLY, 0, 127, 0, 90, 30, 50, 0, 72, 0, 60, 57, 0, 88, 64, FT_HP12, 10, 8, NOLFO, (CT_TILT, 90, 80, 127)),
    PR("jam jar", RL_DRUM, EG_GINGHAM, 5, 54, 120, 0, VM_POLY, 0, 127, 0, 70, 0, 50, 0, 60, 0, 40, 98, 5, 64, 64, FT_LP24, 20, 0, NOLFO, (CT_TILT, 30, 20, 127)),
    PR("bandana", RL_FX, EG_GINGHAM, 4, 78, 16, 0, VM_MONO, 0, 127, 0, 70, 110, 100, 0, 60, 0, 40, 46, 20, 64, 64, FT_LP24, 0, 0, (LT_WAVE, 4, 120, LD_SYNTH, 1, 2), (CT_TILT, 60, 35, 127)),
    PR("deckchair", RL_KEYS, EG_GINGHAM, 0, 42, 50, 0, VM_POLY, 0, 109, 0, 90, 0, 56, 0, 72, 60, 40, 62, 5, 76, 64, FT_LP12, 0, 0, (LT_WOBBLE, 30, 74, 64, 64, 0), (CT_TILT, 90, 35, 127)),
};
const int NPRESETS = (int)(sizeof PRESETS / sizeof PRESETS[0]);

int preset_first(int engine)
{
    int i;
    for (i = 0; i < NPRESETS; i++)
        if (PRESETS[i].par[TP_ENGINE] == engine)
            return i;
    return -1;
}
