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
 * knit's unison is NuSaw's (the NuSaw plugin, Copyright (c) Charles Vestal, MIT; through its FM-1 port, fm1-nusaw):
 * the detune offsets and curve, the pans, the keytracked high-pass. cane and voile are Mutable Instruments Braids'
 * models (Copyright 2012 Emilie Gillet, MIT), ported in dsp/braids.h (MIT), included below. */
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
#define SLEW_SNAP 0.002f                     /* nearer than this: there (the engine's prep stops being redone) */
/* the engine knobs that glide (~10 ms, BRIGHT_SLEW) rather than step a chunk at a time, bit i = TP_E1 + i: the
 * continuous ones; a choice (a model, a table, a ratio, the voices, the organ's switches, a sample) jumps */
static const uint8_t E_GLIDE[NENGINE] = {
    0xB, /* knit: wave, detune, width (not the voices) */  0xD, /* rayon: bright, edge, tine (not the ratio) */
    0xE, /* jacquard: scan, bend, ripple (not the table) */ 0x0, /* bellows: four switches */
    0xE, /* cane: timbre, color, damp */ 0xE,               /* voile: timbre, color, breath */
    0x0, 0x0, 0x0};                                        /* sample, slice (where a note starts), the kit */
static inline float glide_to(float from, float to)
{
    float d = to - from;
    return d < SLEW_SNAP && d > -SLEW_SNAP ? to : from + d * BRIGHT_SLEW;
}
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
const char *const EG_NAME[NENGINE] = {"knit", "rayon", "jacquard", "bellows", "cane", "voile", "sample", "slice",
                                      "kit"};
const char *const EG_KNOB[NENGINE][4] = {
    {"wave", "detune", "voices", "width"}, {"bright", "ratio", "edge", "tine"},     {"table", "scan", "bend", "ripple"},
    {"stops", "pedal", "rotor", "rate"},   {"model", "timbre", "color", "damp"},   {"model", "timbre", "color", "breath"},
    {"sample", "start", "length", "mode"}, {"sample", "slices", "pitch", "gate"},  {"-", "-", "-", "-"}};
/* each engine's plain sound (INIT), 0..127; bellows' four are discrete 1..8, knit's voices 1..7, the Braids engines'
 * models a name each (synth.h NCANE ...) */
static const uint8_t EG_DEF[NENGINE][4] = {{0, 20, 1, 0},  {40, 25, 0, 0}, {0, 40, 0, 0},  {3, 4, 2, 5},
                                           {0, 64, 64, 0}, {2, 0, 64, 0},  {5, 0, 127, SM_ONESHOT}, {SMP_BREAK, 1, 64, 127},
                                           {0, 0, 0, 0}};
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
    float ex[23];                     /* the engine's derived values (eng_prep) */
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
    float load_pred;                  /* the load this block will reach, %: the last block's (synth_load_hint) and the
                                       * voices started since (find_voice); 0: not known (the host) */
    trk_t t[NTRK];
    voice_t v[NVOICE];
};
uint32_t synth_load_steals;           /* new notes that took a sounding voice to stay inside LOAD_ADMIT (synth.h) */
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
static void knit_init(voice_t *v);
static void cn_init(voice_t *v);
static void vo_init(voice_t *v);
static void eng_init(voice_t *v)
{
    int i;
    for (i = 0; i < 16; i++)
        v->o[i] = 0.0f;
    smp_cache_reset(&smp_vc[v->vi % NVOICE]);
    if (v->engine == EG_BELLOWS)
        for (i = 0; i < 6; i++)
            v->o[i * 2] = 1.0f;                  /* the rotating phasors start at 1 + 0i */
    if (v->engine == EG_KNIT)
        knit_init(v);
    if (v->engine == EG_RAYON)
        v->o[5] = v->o[7] = 1.0f;                /* rayon: its fall and its tine start full */
    if (v->engine == EG_BELLOWS)
        v->o[13] = 1.0f, v->o[14] = 0.0f;        /* the rotor's gain and its ramp */
    if (v->engine == EG_CANE)
        cn_init(v);
    else if (v->engine == EG_VOILE)
        vo_init(v);
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
    const int8_t *ta = WT[(int)c->ex[1]][(int)c->ex[2]], *tb = ta + WT_N;
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
        xl[i] = (a + (b - a) * fb) * (1.0f / 128.0f);
    }
    v->o[0] = p;
    v->o[1] = mp;
}

/* rayon (was orbit): two-operator FM, the DX's way, for e-pianos, bells, mallets and basses. bright = the index (velocity
 * sets how much of it a note gets: 0.4 .. 1 of it); ratio from a detuned 1:1 (0..50: slow beating) to the modulator's
 * ratios (51..100: RAYON_RATIO, the bells' inharmonic ones among them); edge = the modulator's feedback (sine toward
 * saw); tine = a 14:1 partial struck with the note and dying in ~0.25 s, by velocity (the e-piano's bark, tines'
 * bell). Its second page (TP_X1..X3, RAYON_X_P): fall, the index falling from the note's start (0 = held; else a time
 * constant 20 ms .. 4 s); floor, what of it remains (0..100 %); keyscale, the index lower for higher notes (0 .. -6 dB
 * an octave above C3). o[0..2] the carrier, the modulator, the feedback; o[5] the fall (1 at the note's start); o[6]
 * the tine's phase, o[7] its envelope. ex[0] / ex[1] the increments, ex[2] the index now (rayon_chunk: the fall),
 * ex[3] the feedback, ex[4] the tine's level at the start, ex[5] its increment, ex[8..10] the second page's
 * (voice_chunk), ex[11] the index before the fall */
static const float RAYON_RATIO[16] = {0.5f, 0.6666667f, 0.75f, 1.25f, 1.3333334f, 1.4142135f, 1.5f, 1.75f, 2.0f, 2.5f,
                                      3.0f, 3.5f, 4.0f, 5.0f, 7.0f, 8.0f};
static void rayon_prep(voice_t *v)
{
    cfg_t *c = &v->c;
    float r = c->e[1] * 100.0f, ratio, vs = 0.4f + 0.6f * v->vel;
    if (r <= 50.0f)
        ratio = fm_exp2f(r * 0.6f * (1.0f / 1200.0f));          /* 0 .. 30 cents */
    else {
        int k = (int)((r - 51.0f) * (15.99f / 49.0f));
        ratio = RAYON_RATIO[k < 0 ? 0 : k > 15 ? 15 : k];
    }
    c->ex[0] = fm_minf(c->freq * INV_SR, 0.45f);
    c->ex[1] = fm_minf(c->freq * ratio * INV_SR, 0.45f);
    c->ex[2] = c->e[0] * c->e[0] * 5.0f * vs;                   /* index, turns */
    if (c->ex[10] > 0.0f && c->freq > 130.81f)                  /* keyscale: the index an octave above C3 x 2^-k */
        c->ex[2] *= fm_exp2f(-c->ex[10] * fm_log2f(c->freq * (1.0f / 130.81f)));
    c->ex[11] = c->ex[2];                                        /* (the index before the fall: rayon_chunk) */
    c->ex[3] = c->e[2] * 1.3f;                                  /* feedback */
    c->ex[5] = c->freq * 14.0f * INV_SR;                         /* the tine: 14:1, only while under 0.45 */
    c->ex[4] = c->ex[5] < 0.45f ? c->e[3] * 0.5f * vs : 0.0f;
    c->stereo = 0;
}
static void rayon_run(voice_t *v, float *xl, float *xr, int m)
{
    const cfg_t *c = &v->c;
    float cp = v->o[0], mp = v->o[1], fbv = v->o[2];
    float dc = c->ex[0], dm = c->ex[1], idx = c->ex[2] * 0.25f, fb = c->ex[3] * 0.25f, tl = c->ex[4] * v->o[7];
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
    if (tl > 1e-4f) {                            /* the tine, added */
        float tp = v->o[6], dt = c->ex[5];
        for (i = 0; i < m; i++) {
            tp = step1(tp, dt);
            xl[i] += fm_sin_turns(tp) * tl;
        }
        v->o[6] = tp;
    }
    v->o[0] = cp;
    v->o[1] = mp;
    v->o[2] = fbv;
}
static void rayon_chunk(voice_t *v)              /* the fall, the tine's decay */
{
    cfg_t *c = &v->c;
    if (c->ex[8] > 0.0f) {                       /* the fall: o[5] 1 at the note's start, toward 0 */
        c->ex[2] = c->ex[11] * (c->ex[9] + (1.0f - c->ex[9]) * v->o[5]);
        v->o[5] *= c->ex[8];
    }
    v->o[7] *= 0.994f;                           /* the tine: ~0.25 s (tines') */
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
/* the drawbars the stops and the pedal ask for (bars[0..5]) and the rotor's depth (*amt): organ_chunk glides the
 * voice's toward them (a stop changed while a note sounds: no click); a new note starts at them (organ_prep) */
static void organ_want(const cfg_t *c, float *bars, float *amt)
{
    int k = knob8(c->ek[0]) - 1, j;
    float bass = (float)(knob8(c->ek[1]) - 1) * (1.0f / 7.0f), sum = bass;
    *amt = (float)(knob8(c->ek[2]) - 1) * (1.0f / 7.0f);
    bars[0] = bass * 0.8f;
    for (j = 1; j < 6; j++) {
        bars[j] = ORGAN_SET[k][j - 1];
        sum += bars[j];
    }
    sum = 1.4f / (sum + 0.5f);                  /* the bars share a level near the other engines' */
    for (j = 0; j < 6; j++)
        bars[j] *= sum;
}
static void organ_prep(voice_t *v)
{
    cfg_t *c = &v->c;
    int j;
    float tr = 0.5f * fm_exp2f((float)(knob8(c->ek[3]) - 1) * (4.0f / 7.0f)) * INV_SR, base, amt;   /* 0.5 .. 8 Hz */
    if (v->fresh) {                             /* (sounding: organ_chunk glides them) */
        float bars[6];
        organ_want(c, bars, &amt);
        for (j = 0; j < 6; j++)
            c->ex[j] = bars[j];
        c->ex[19] = amt;
    }
    amt = c->ex[19];
    c->ex[18] = tr;
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
static void organ_chunk(voice_t *v)              /* the drawbars' glide, the tremolo's phase and the chunk's gain ramp */
{
    cfg_t *c = &v->c;
    float g1, bars[6], amt;
    int j;
    organ_want(c, bars, &amt);
    for (j = 0; j < 6; j++)
        c->ex[j] = glide_to(c->ex[j], bars[j]);
    c->ex[19] = glide_to(c->ex[19], amt);
    v->o[12] = wrap1(v->o[12] + c->ex[18] * (float)SY_CHUNK);
    g1 = 1.0f - c->ex[19] * 0.5f * (0.5f - 0.5f * fm_sin_turns(v->o[12]));
    v->o[14] = (g1 - v->o[13]) * (1.0f / (float)SY_CHUNK);
}

/* knit's unison: the voices' detune offsets by pairs (n / 2), the JP-8000's table at six or seven (after NuSaw,
 * the NuSaw plugin, MIT, and its FM-1 port fm1-nusaw: brocade's, the supersaw this replaces); NuSaw's detune curve
 * (fine at first, wide at the end); and its keytracked high-pass (12 dB at a third of the note, the JP-8000's), which
 * keeps a unison's beating out of the bass */
static const float KNIT_COEF[4][7] = {
    {0.0f},
    {0.0f, 1.0f, -1.0f},
    {0.0f, 1.0f / 3.0f, -1.0f / 3.0f, 1.0f, -1.0f},
    {0.0f, 128.0f / 720.0f, -128.0f / 720.0f, 408.0f / 720.0f, -412.0f / 720.0f, 704.0f / 720.0f, -1.0f}};
static inline float knit_detune(float x)
{
    if (x < 0.1f)
        return x * 0.2f;
    if (x < 0.5f)
        return 0.02f + (x - 0.1f) * 0.575f;
    return 0.25f + (x - 0.5f) * 1.5f;
}
SY_INLINE void knit_hp(float *x, float *lo, float *bp, float f, int m)
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
/* knit: the everyday subtractive voice. wave: saw (0) .. square (the middle) .. a 10 % pulse (saw_pulse:
 * the LFO on it is PWM); voices 1..7: a unison of that wave, detuned by the JP-8000's offsets (KNIT_COEF) as detune opens, each pair across the field as width opens, all at one power (1 / sqrt n); at one voice
 * detune is a drift instead, the pitch wandering slowly (up to +-12 cents, a fresh target every ~0.4 s). Its second
 * page (TP_X1..X4, KNIT_X_P): osc 2, one more of the wave at an interval (+-24 semitones about 64) and its level (0 off);
 * sub, a sine an octave down; noise. A unison's beating is kept out of the bass by brocade's keytracked high-pass.
 * o[0..6] the voices' phases (random at the start), o[7] osc 2's, o[8] the sub's, o[10] the drift (cents) and o[11]
 * its target, o[12..15] the high-pass (low and band, L then R). ex[0] the increment, ex[1] the detune's, ex[2] the
 * voices, ex[3] the pulse's mix, ex[4] its width, ex[5..10] the pairs' L and R gains, ex[11] the high-pass's, ex[12]
 * a voice's gain, ex[13] osc 2's increment, ex[14] its gain, ex[15] the sub's increment, ex[16] its gain, ex[17]
 * the noise's, ex[18..21] the second page's knobs (voice_chunk), ex[22] the drift's factor (knit_chunk). */
#define KNIT_MAX 7
static void knit_prep(voice_t *v)
{
    cfg_t *c = &v->c;
    int n = (int)fm_clampf(fm_floorf(c->ek[2] + 0.5f), 1.0f, (float)KNIT_MAX), M = n / 2, k;
    float f = c->freq * INV_SR, w = c->e[3];
    c->ex[0] = fm_minf(f, 0.45f);
    c->ex[1] = n > 1 ? f * 0.1102f * knit_detune(c->e[1]) : 0.0f;
    c->ex[2] = (float)n;
    if (c->e[0] < 0.5f) {                        /* saw .. square */
        c->ex[3] = c->e[0] * 2.0f;
        c->ex[4] = 0.5f;
    } else {                                     /* square .. a 10 % pulse */
        c->ex[3] = 1.0f;
        c->ex[4] = 0.5f - (c->e[0] - 0.5f) * 0.8f;
    }
    for (k = 1; k <= 3; k++) {                   /* pair k: its upper voice towards the right, its lower the left */
        float a = (1.0f + (M ? (float)(k < M ? k : M) / (float)M * w : 0.0f)) * 0.78539816f;
        c->ex[3 + 2 * k] = fm_cosf(a) * 1.41421356f;
        c->ex[4 + 2 * k] = fm_sinf(a) * 1.41421356f;
    }
    c->ex[11] = fm_minf(2.07345f * f, 0.9f);    /* the high-pass at a third of the note (brocade's) */
    c->ex[12] = fm_rsqrtf((float)n);
    c->ex[13] = fm_minf(f * fm_exp2f((c->ex[18] - 64.0f) * (1.0f / 12.0f)), 0.45f);
    c->ex[14] = c->ex[19] * (1.0f / 127.0f);
    c->ex[15] = f * 0.5f;
    c->ex[16] = c->ex[20] * (1.0f / 127.0f);
    c->ex[17] = c->ex[21] * (0.5f / 127.0f);
    if (c->ex[22] <= 0.0f)
        c->ex[22] = 1.0f;
    c->stereo = w > 0.001f && n > 1;
}
static void knit_chunk(voice_t *v)                 /* the drift at one voice: toward a fresh target, slowly */
{
    cfg_t *c = &v->c;
    float depth = c->ex[2] < 1.5f ? c->e[1] * 12.0f : 0.0f;
    if (depth <= 0.0f) {
        c->ex[22] = 1.0f;
        return;
    }
    if ((v->nchunk & 255) == 0)                  /* (~0.4 s) */
        v->o[11] = (float)(int32_t)xs(&v->rng) * (1.0f / 2147483648.0f) * depth;
    v->o[10] += (v->o[11] - v->o[10]) * 0.01f;
    c->ex[22] = fm_exp2f(v->o[10] * (1.0f / 1200.0f));
}
/* one oscillator of the wave added into the buffers at gains gl / gr */
SY_INLINE void knit_osc(float *ph, float d, float id, float pw, float mix, float gl, float gr, float *xl, float *xr,
                      int m, const int stereo)
{
    float p = *ph;
    int i;
    for (i = 0; i < m; i++) {
        float x;
        p = step1(p, d);
        x = mix <= 0.0f ? 2.0f * p - 1.0f - blep(p, d, id) : saw_pulse(p, d, id, pw, mix);
        xl[i] += x * gl;
        if (stereo)
            xr[i] += x * gr;
    }
    *ph = p;
}
SY_INLINE void knit_loop(voice_t *v, float *xl, float *xr, int m, const int stereo)
{
    const cfg_t *c = &v->c;
    const int n = (int)c->ex[2];
    const float *co = KNIT_COEF[n / 2], mix = c->ex[3], pw = c->ex[4], g = c->ex[12], dr = c->ex[22];
    int i, k;
    for (i = 0; i < m; i++) {
        xl[i] = 0.0f;
        if (stereo)
            xr[i] = 0.0f;
    }
    for (k = 0; k < n; k++) {                    /* the voices: the centre, then the pairs (upper right, lower left) */
        float d = fm_clampf((c->ex[0] + co[k] * c->ex[1]) * dr, 1e-6f, 0.45f), gl = g, gr = g;
        if (k && stereo) {
            const float *pn = &c->ex[3 + 2 * ((k + 1) >> 1)];
            gl = g * pn[~k & 1];
            gr = g * pn[k & 1];
        }
        knit_osc(&v->o[k], d, 1.0f / d, pw, mix, gl, gr, xl, xr, m, stereo);
    }
    if (n > 1) {
        knit_hp(xl, &v->o[12], &v->o[13], c->ex[11], m);
        if (stereo)
            knit_hp(xr, &v->o[14], &v->o[15], c->ex[11], m);
    }
    if (c->ex[14] > 0.0f) {                      /* osc 2: one more, in the middle */
        float d = fm_clampf(c->ex[13] * dr, 1e-6f, 0.45f);
        knit_osc(&v->o[7], d, 1.0f / d, pw, mix, c->ex[14], c->ex[14], xl, xr, m, stereo);
    }
    if (c->ex[16] > 0.0f || c->ex[17] > 0.0f) {  /* the sub (a sine an octave down) and the noise */
        float p = v->o[8], d = c->ex[15] * dr, gs = c->ex[16], gn = c->ex[17];
        for (i = 0; i < m; i++) {
            float x;
            p = step1(p, d);
            x = fm_sin_pi((p < 0.5f ? p : p - 1.0f) * FM_TWO_PI) * gs + frand(&v->rng) * gn;
            xl[i] += x;
            if (stereo)
                xr[i] += x;
        }
        v->o[8] = p;
    }
}
static void knit_run(voice_t *v, float *xl, float *xr, int m)
{
    if (v->c.stereo)
        knit_loop(v, xl, xr, m, 1);
    else
        knit_loop(v, xl, xr, m, 0);
}
static void knit_init(voice_t *v)                  /* the voices from random phases */
{
    int j;
    for (j = 0; j < KNIT_MAX; j++)
        v->o[j] = (float)(xs(&v->rng) >> 8) * (1.0f / 16777216.0f);
    v->c.ex[22] = 1.0f;                          /* (no drift yet: another engine's ex[22] is not a factor) */
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
#define SMP_SWITCH 64.0f                         /* a mono retrigger's fade of the sound playing (1.5 ms; slice's SL_SWITCH) */
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
    if (!done && v->o[5] > 0.5f) {               /* a mono retrigger: the sound playing fades out over SMP_SWITCH
                                                  * samples, then the note starts (smp_retrigger) */
        float sw = v->o[6];
        int k = (int)((1.0f - sw) * SMP_SWITCH + 0.999f);
        k = k < 1 ? 1 : k > m ? m : k;
        if (pos >= s0 && pos < s1) {
            int got = smp_play(&smp_vc[v->vi % NVOICE], sm, &pos, &fr, inc, dir, s0, s1, xl, k);
            for (int j = 0; j < got; j++)
                xl[j] *= fm_maxf(1.0f - (sw + (float)j * (1.0f / SMP_SWITCH)), 0.0f);
            for (int j = got; j < k; j++)
                xl[j] = 0.0f;
        } else
            for (int j = 0; j < k; j++)
                xl[j] = 0.0f;
        sw += (float)k * (1.0f / SMP_SWITCH);
        i = k;
        if (sw >= 1.0f) {
            v->o[5] = v->o[6] = 0.0f;
            v->o[4] = 0.0f;
        } else
            v->o[6] = sw;
    }
    if (!done && v->o[4] < 0.5f && v->o[5] < 0.5f) {   /* the note's first sample: the region's start (reverse: its end) */
        v->o[4] = 1.0f;
        pos = mode == SM_REVERSE ? s1 - 1 : s0;
        fr = 0.0f;
        dir = mode == SM_REVERSE ? -1 : 1;
    }
    while (!done && i < m && v->o[5] < 0.5f) {
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
static void smp_retrigger(voice_t *v)            /* a mono retrigger: what plays fades out first (smp_run), no click */
{
    if (v->o[4] > 0.5f && v->o[3] < 0.5f) {
        v->o[5] = 1.0f;
        v->o[6] = 0.0f;
    } else
        v->o[3] = v->o[4] = v->o[5] = 0.0f;
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
    case EG_RAYON: rayon_chunk(v); break;
    case EG_BELLOWS: organ_chunk(v); break;
    case EG_CANE: cn_chunk(v); break;
    case EG_KNIT: knit_chunk(v); break;
    case EG_VOILE: vo_chunk(v); break;
    default: break;
    }
}
static void eng_prep(voice_t *v, float bpm)
{
    switch (v->engine) {
    case EG_JACQUARD: wt_prep(v); break;
    case EG_RAYON: rayon_prep(v); break;
    case EG_BELLOWS: organ_prep(v); break;
    case EG_SAMPLE: smp_prep(v); break;
    case EG_SLICE: slc_prep(v, bpm); break;
    case EG_CANE: cn_prep(v); break;
    case EG_VOILE: vo_prep(v); break;
    default: knit_prep(v); break;
    }
}
static void eng_run(voice_t *v, float *xl, float *xr, int m)
{
    switch (v->engine) {
    case EG_JACQUARD: wt_run(v, xl, xr, m); break;
    case EG_RAYON: rayon_run(v, xl, xr, m); break;
    case EG_BELLOWS: organ_run(v, xl, xr, m); break;
    case EG_SAMPLE: smp_run(v, xl, xr, m); break;
    case EG_SLICE: slc_run(v, xl, xr, m); break;
    case EG_CANE: cn_run(v, xl, xr, m); break;
    case EG_VOILE: vo_run(v, xl, xr, m); break;
    default: knit_run(v, xl, xr, m); break;
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
        eng = EG_KNIT;
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
    if (v->engine == EG_RAYON)                   /* its fall and its tine from the top, every note */
        v->o[5] = v->o[7] = 1.0f;
    if (v->engine == EG_SAMPLE || v->engine == EG_SLICE) {   /* from the region's (the slice's) start again */
        v->o[3] = v->o[4] = 0.0f;
        v->o[6] = v->o[7] = 0.0f;
    }
    if (v->engine == EG_CANE)                    /* the Braids engines: a strike (Braids' trigger) */
        cn_strike(v, 1);
    else if (v->engine == EG_VOILE)
        vo_strike(v);
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

/* knit's cap (brocade's, whose unison it took): at most knit_cap() of its voices in the pool, by the unison of the
 * track that wants one (six at six or fewer, five at seven: such a voice costs about two and a half ordinary ones).
 * A voice counts as what it will play (a steal's pending note: its track's engine). */
static int knit_cap(const synth_t *s, int trk)
{
    return P(s, trk, TP_E3) >= 7 ? 5 : 6;
}
static int knit_voice(const synth_t *s, const voice_t *v)
{
    if (!v_sounding(v))
        return 0;
    return (v->steal_phase == 1 ? P(s, v->trk, TP_ENGINE) : v->engine) == EG_KNIT;
}
static int knit_find(const synth_t *s, int trk) /* the knit voice to take when the cap is reached; -1 */
{
    int i, n = 0, best = -1, own = 0;
    float quiet = 1e9f;
    uint32_t oldest = 0xFFFFFFFFu;
    for (i = 0; i < s->max_voices; i++)
        n += knit_voice(s, &s->v[i]);
    if (n < knit_cap(s, trk))
        return -1;
    for (i = 0; i < s->max_voices; i++) {        /* releasing first (the quietest), then the track's own oldest */
        const voice_t *v = &s->v[i];
        if (!knit_voice(s, v) || v->steal_phase == 1)
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
        if (!knit_voice(s, v) || v->steal_phase == 1 || (own && !mine))
            continue;
        if ((mine && !own) || v->age < oldest) {
            oldest = v->age;
            best = i;
            own = mine;
        }
    }
    if (best < 0)                                /* all of them fading for a note already: the newest of */
        for (i = 0; i < s->max_voices; i++)      /* those waits for this note instead */
            if (knit_voice(s, &s->v[i]) && (best < 0 || s->v[i].age > s->v[best].age))
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

/* the load check at a note's start (the FM-1's: there each track knows its voices' measured time): a free voice
 * adds the track's cost to this block's predicted load, scaled by LOAD_SHARE (the two cores share the voices); one
 * that would take it past LOAD_ADMIT % takes a sounding voice instead, as a full pool does. The overload guard
 * (engine.c) only sees a block once it has overrun: a chord change's new voices could. */
#define LOAD_ADMIT 92.0f
#define LOAD_SHARE 0.6f
void synth_load_hint(synth_t *s, uint32_t pct) { s->load_pred = (float)pct; }
static float load_add(const synth_t *s, int trk)
{
    return s->t[trk].cost_m * (100.0f / 544.0f) * LOAD_SHARE;   /* (ticks a frame; 544 a frame's budget) */
}
static int load_over(const synth_t *s, int trk)
{
    int i;
    float add = load_add(s, trk);
    if (add <= 0.0f || s->load_pred <= 0.0f || s->load_pred + add <= LOAD_ADMIT)
        return 0;
    for (i = 0; i < s->max_voices; i++)          /* (a sounding voice to take: else the free one after all) */
        if (v_sounding(&s->v[i]) && s->v[i].steal_phase == 0)
            return 1;
    return 0;
}

/* a voice for a new note: a free one (inside the load check); else the track's oldest if it holds NVOICE_TRK
 * already; else the quietest releasing; else the oldest of all. A stolen voice fades 15 ms first. knit and cane
 * first keep to their own caps (knit_find, cn_find). */
static int find_voice(synth_t *s, int trk)
{
    int i, best = -1, over;
    float quiet = 1e9f;
    uint32_t oldest = 0xFFFFFFFFu;
    if (P(s, trk, TP_ENGINE) == EG_KNIT && (best = knit_find(s, trk)) >= 0)
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
    over = load_over(s, trk);
    if (over)
        synth_load_steals++;
    else
        for (i = 0; i < s->max_voices; i++)
            if (!v_sounding(&s->v[i])) {
                s->load_pred += load_add(s, trk);
                return i;
            }
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
                if (v->engine == EG_RAYON)
                    v->o[5] = v->o[7] = 1.0f;
                if (v->engine == EG_SAMPLE)
                    smp_retrigger(v);
                if (v->engine == EG_SLICE)
                    slc_retrigger(v);
                if (v->engine == EG_CANE)        /* the lines as they are, the exciter again */
                    cn_strike(v, 0);
                else if (v->engine == EG_VOILE)
                    vo_strike(v);
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

/* a key let go: the release; a slice plays on to its gate's end (the GATE knob is how much of it plays, whatever the
 * note's length: slices on the steps play through as the break did) */
static void key_release(voice_t *v)
{
    if (v->engine == EG_SLICE && v->amp.stage != ENV_RELEASE)
        v->held = 0;
    else
        voice_release(v);
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
            key_release(v);
        }
        return;
    }
    for (i = 0; i < NVOICE; i++) {
        voice_t *v = &s->v[i];
        if (v->trk != trk)
            continue;
        if (v->held && v->tag == tag && v->steal_phase != 1)
            key_release(v);
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
int synth_lock_value(const synth_t *s, int trk, int param)
{
    if (trk < 0 || trk >= NTRK || param < 0 || param >= NTP_LOCK || s->t[trk].lock[param] == LOCK_NONE)
        return -1;
    return s->t[trk].lock[param];
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
    if (v->engine == EG_CANE || v->engine == EG_VOILE) {   /* by the model (braids.h) */
        int md = v->c.ex[19] > 0.0f ? (int)v->c.ex[19] : 0;
        return v->engine == EG_CANE ? CN_COST[md < NCANE ? md : 0] : VO_COST[md < NVOILE ? md : 0];
    }
    if (v->engine == EG_BELLOWS)
        return 5;                                /* (the FM-1, tools/measure_load.sh 2026-10-18: a quarter above the rest) */
    if (v->engine == EG_KNIT)                    /* (the FM-1, tools/preset_prof.py 2026-10-19: 3 and a unit and
                                                  * a half a unison voice, a unit an extra; clamped: a voice started
                                                  * this block holds its last engine's values until its first chunk) */
        return 3 + (int)(1.5f * fm_clampf(v->c.ex[2], 1.0f, (float)KNIT_MAX)) + (v->c.ex[14] > 0.0f) +
               (v->c.ex[16] > 0.0f || v->c.ex[17] > 0.0f);
    if (v->engine == EG_RAYON)
        return 5;                                /* (the FM-1, 2026-10-19: a quarter above an ordinary voice) */
    return 4;
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
        v->engine = EG_KNIT;
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
SY_INLINE void char_loop(const float *cx, float *cz, float *xl, float *xr, int k, const int type, const int stereo)
{
    const float c0 = cx[0], c1 = cx[1], c2 = cx[2], c3 = cx[3], c4 = cx[4], c5 = cx[5], wet = cx[7];
    float z0 = cz[0], z1 = cz[1], z2 = cz[2], z3 = cz[3], ph = cz[4], qi = cz[5];
    int j;
    if (type == CT_SAMPLER) {                    /* z0 / z1 the input filter, z2 / z3 the held sample, z6 / z7 the output's */
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
    cz[0] = fm_flush(z0);
    cz[1] = fm_flush(z1);
    cz[2] = fm_flush(z2);
    cz[3] = fm_flush(z3);
    cz[4] = ph;
    cz[5] = qi;
}
static void char_stage(const float *cx, float *cz, int type, int stereo, float *xl, float *xr, int k)
{
    switch (type * 2 + (stereo != 0)) {
    case CT_DRIVE * 2: char_loop(cx, cz, xl, xr, k, CT_DRIVE, 0); break;
    case CT_DRIVE * 2 + 1: char_loop(cx, cz, xl, xr, k, CT_DRIVE, 1); break;
    case CT_FOLD * 2: char_loop(cx, cz, xl, xr, k, CT_FOLD, 0); break;
    case CT_FOLD * 2 + 1: char_loop(cx, cz, xl, xr, k, CT_FOLD, 1); break;
    case CT_CRUSH * 2: char_loop(cx, cz, xl, xr, k, CT_CRUSH, 0); break;
    case CT_CRUSH * 2 + 1: char_loop(cx, cz, xl, xr, k, CT_CRUSH, 1); break;
    case CT_RING * 2: char_loop(cx, cz, xl, xr, k, CT_RING, 0); break;
    case CT_RING * 2 + 1: char_loop(cx, cz, xl, xr, k, CT_RING, 1); break;
    case CT_TILT * 2: char_loop(cx, cz, xl, xr, k, CT_TILT, 0); break;
    case CT_TILT * 2 + 1: char_loop(cx, cz, xl, xr, k, CT_TILT, 1); break;
    case CT_TAPE * 2: char_loop(cx, cz, xl, xr, k, CT_TAPE, 0); break;
    case CT_TAPE * 2 + 1: char_loop(cx, cz, xl, xr, k, CT_TAPE, 1); break;
    case CT_SAMPLER * 2: char_loop(cx, cz, xl, xr, k, CT_SAMPLER, 0); break;
    case CT_SAMPLER * 2 + 1: char_loop(cx, cz, xl, xr, k, CT_SAMPLER, 1); break;
    default: break;
    }
}

/* a kit's character (synth.h char_bus): the stage on the kit's summed pair */
void char_bus(chbus_t *c, const uint8_t *par, float *l, float *r, int n)
{
    int type = par[TP_CTYPE] < NCT ? par[TP_CTYPE] : CT_OFF, i;
    float a, t;
    if (par[TP_CMIX] == 0)
        type = CT_OFF;
    if (type != c->type) {                       /* another type: its states start clean */
        for (i = 0; i < 8; i++)
            c->cz[i] = 0.0f;
        c->type = (uint8_t)type;
        c->cin_type = 0xFFu;
    }
    if (type == CT_OFF)
        return;
    a = (float)(par[TP_CAMT] & 127) * (1.0f / 127.0f);
    t = (float)(par[TP_CTONE] & 127) * (1.0f / 127.0f);
    if (type != c->cin_type || a != c->cin[0] || t != c->cin[1]) {
        char_coefs(type, a, t, KIT_RING_HZ, c->cx);
        c->cin_type = (uint8_t)type;
        c->cin[0] = a;
        c->cin[1] = t;
    }
    c->cx[7] = (float)(par[TP_CMIX] & 127) * (1.0f / 127.0f);
    if (type == CT_RING) {                       /* the phasor back to unit length (char_prep) */
        float re = c->cz[4], im = c->cz[5], r2 = re * re + im * im, g = r2 > 0.5f ? fm_rsqrtf(r2) : 0.0f;
        c->cz[4] = g > 0.0f ? re * g : 1.0f;
        c->cz[5] = im * g;
    }
    char_stage(c->cx, c->cz, type, 1, l, r, n);
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
        int gm = v->fresh ? 0 : E_GLIDE[v->engine < NENGINE ? v->engine : 0];
        for (i = 0; i < 4; i++) {
            float e;
            c->ek[i] = fm_minf(pf(s, trk, TP_E1 + i, dest, off), 127.0f);   /* a foreign lock value stays in range */
            e = c->ek[i] * (1.0f / 127.0f);
            c->e[i] = gm >> i & 1 ? glide_to(c->e[i], e) : e;   /* (a fresh voice: at its values at once) */
        }
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
    {   /* the cutoff and the resonance glide too (~10 ms), as the engine knobs */
        float co = (pf(s, trk, TP_CUT, dest, off) + (float)t->perf.cut) * (8.64385619f / 127.0f)
                   + pf(s, trk, TP_KTRK, dest, off) * (1.0f / 127.0f) * (v->pitch - 60.0f) * (1.0f / 12.0f);
        c->cut_oct = v->fresh ? co : glide_to(c->cut_oct, co);
    }
    c->fenv_oct = (pf(s, trk, TP_FENV, dest, off) - 64.0f) * (6.0f / 64.0f) * (0.5f + 0.5f * v->vel);
    res = pf(s, trk, TP_RES, dest, off) * (1.0f / 127.0f);
    if (!v->fresh)
        res = glide_to(c->kres * 0.25f, res);
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
         * chunk: the organ's pitch wobble and phasor trim); knit's and rayon's second pages: when they moved */
        float pin = v->pitch_cur + c->pitch_off * 0.01f;
        int moved = pin != v->pitch_in || v->fresh || (v->nchunk & 3) == 0;
        if (v->engine == EG_RAYON) {             /* its second page (rayon_prep, rayon_chunk) */
            int fl = P(s, trk, TP_X1), fo = P(s, trk, TP_X2), ks = P(s, trk, TP_X3);
            float fall = fl ? fm_expf(-(float)SY_CHUNK / (0.02f * fm_exp2f((float)fl * (7.64f / 127.0f)) * SY_SR)) : 0.0f;
            float fv = (float)fo * (1.0f / 127.0f), kv = (float)ks * (1.0f / 127.0f);
            moved |= fall != c->ex[8] || kv != c->ex[10];
            c->ex[8] = fall;
            c->ex[9] = fv;
            c->ex[10] = kv;
        }
        if (v->engine == EG_KNIT) {            /* its second page (knit_prep) */
            for (i = 0; i < 4; i++) {
                float xv = (float)P(s, trk, TP_X1 + i);
                moved |= xv != c->ex[18 + i];
                c->ex[18 + i] = xv;
            }
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
 * xl / xr for the character stage (char_stage), then out_loop does the rest.
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
                char_stage(v->c.cx, v->cz, v->c.ctype, v->c.stereo, xl, xr, k);
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
            char_stage(v->c.cx, v->cz, v->c.ctype, v->c.stereo, xl, xr, k);
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

void synth_order(const synth_t *s, int8_t *ord)
{
    int vi, j;
    for (vi = 0; vi < NVOICE; vi++) {
        j = vi;
#ifndef PLAT_HOST
        while (j > 0 && s->v[ord[j - 1]].engine > s->v[vi].engine) {
            ord[j] = ord[j - 1];
            j--;
        }
#endif
        ord[j] = (int8_t)vi;
    }
    (void)s;
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
    /* TP_X1 .. TP_X4: the engine's second page: none but an engine's own (tp_info: knit's, rayon's) */
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
                                      {"type", 0, NDF - 1, DF_OFF, PK_NAMES, DFTYPE_N},
                                      {"fx I", 0, 127, 0, PK_PCT, 0}, {"fx II", 0, 127, 0, PK_PCT, 0}};
/* a drumline track's kit pages: Kit env (TP_A..TP_R) and Kit filter (the bus: TP_CUT..TP_KTRK) */
static const tp_info_t DRUMENV[4] = {{"lag", 0, 127, 0, PK_PCT, 0}, {"attack", 0, 127, 0, PK_PCT, 0},
                                     {"random", 0, 127, 0, PK_PCT, 0}, {"length", 0, 127, 64, PK_PCT, 0}};
static const tp_info_t DRUMFLT[4] = {{"lowcut", 0, 127, 0, PK_HZ, 0}, {"comp", 0, 127, 0, PK_PCT, 0},
                                     {"drive", 0, 127, 0, PK_PCT, 0}, {"hicut", 0, 127, 127, PK_HZ, 0}};
static const tp_info_t NONE_P = {"-", 0, 0, 0, PK_NUM, 0};
/* knit's second page: osc 2's interval (+-24 semitones about 64) and level, the sub, the noise (knit_prep) */
static const tp_info_t KNIT_X_P[4] = {{"osc 2", 40, 88, 64, PK_BIPST, 0}, {"level", 0, 127, 0, PK_PCT, 0},
                                    {"sub", 0, 127, 0, PK_PCT, 0}, {"noise", 0, 127, 0, PK_PCT, 0}};
/* orbit's second page: the index's fall (0 held; 20 ms .. 4 s), its floor, keyscale (rayon_prep) */
static const tp_info_t RAYON_X_P[4] = {{"fall", 0, 127, 0, PK_NUM, 0}, {"floor", 0, 127, 0, PK_PCT, 0},
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
            if (e == EG_JACQUARD && k == 0) {
                i->hi = WT_NTABLE - 1;
                i->kind = PK_NAMES;
                i->names = WTNAME_N;
            }
            if (e == EG_BELLOWS) {             /* the discrete 1..8 knobs */
                i->lo = 1;
                i->hi = 8;
                i->kind = PK_NUM;
            }
            if (e == EG_KNIT && k == 2) {      /* the voices, 1..7 */
                i->lo = 1;
                i->hi = KNIT_MAX;
                i->kind = PK_NUM;
            }
            if ((e == EG_CANE || e == EG_VOILE) && k == 0) {   /* the Braids models */
                i->hi = (uint8_t)(e == EG_CANE ? NCANE - 1 : NVOILE - 1);
                i->kind = PK_NAMES;
                i->names = e == EG_CANE ? CANE_N : VOILE_N;
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
            engine = EG_KNIT;
        return &eknob[engine][p - TP_E1];
    }
    if (p >= TP_X1 && p <= TP_X4)                /* the engine's second page: knit's, rayon's, else none */
        return engine == EG_RAYON ? &RAYON_X_P[p - TP_X1]
             : engine == EG_KNIT ? &KNIT_X_P[p - TP_X1] : &NONE_P;
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
            case LD_SYNTH: return &lparam[engine < NENGINE ? engine : EG_KNIT];
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
    if (p == TP_E2 && engine == EG_RAYON) {       /* the ratio: "1:1 +12c" in the detuned stretch, else "3.5:1" */
        float r = (float)(v > 127 ? 127 : v) * (100.0f / 127.0f);
        if (r <= 50.0f) {
            char *e = put_s(num, "1:1");
            if (r * 0.6f >= 0.5f)
                put_s(put_i(put_s(e, "+"), (int)(r * 0.6f + 0.5f)), "c");
        } else {
            int k = (int)((r - 51.0f) * (15.99f / 49.0f));
            float q = RAYON_RATIO[k < 0 ? 0 : k > 15 ? 15 : k];
            put_s((float)(int)q == q ? put_i(num, (int)q) : put_f1(num, q), ":1");
        }
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
        engine = EG_KNIT;
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
/* the factory presets: recipes in tools/presets/ (tools/presets/lib.py says how one is written), made into this
 * table by tools/gen_presets.py (app/fm1.toml gen; the host tests run it). Each engine's first preset is its sound in
 * a new project (and what choosing the engine loads) */
#include "presets.h"
const int NPRESETS = (int)(sizeof PRESETS / sizeof PRESETS[0]);

int preset_first(int engine)
{
    int i;
    for (i = 0; i < NPRESETS; i++)
        if (PRESETS[i].par[TP_ENGINE] == engine)
            return i;
    return -1;
}
