/* SPDX-License-Identifier: MPL-2.0 */
/* LOOM-8's effects library (the one effects list: docs/superpowers/specs/2026-11-25-one-effects-list-design.md): the
 * catalog every slot picks from (a track's two, FX I / II, the master: one list, the same names, knobs and code) and
 * each effect's DSP, once. Its users keep their own storage and pages for now (synth.c: a track's two slots, on its
 * voices or its bus; fx.c: the sends and the master's slot); they call in here.
 *
 * Everything per sample runs in the audio interrupt (or the second core's job); the coefficients' functions in the
 * main loop or once a block. Float, fastmath.h; no double, no libm. */
#pragma once
#include <stddef.h>
#include <stdint.h>

/* ---- the catalog: numbers fixed, a new effect only ever appended ---- */
enum { EF_OFF, EF_DRIVE, EF_RAT, EF_FOLD, EF_CRUSH, EF_RING, EF_TILT, EF_TAPE, EF_SAMPLER, EF_LOFI, EF_TREMOLO,
       EF_PHASER, EF_UNIVIBE, EF_CHORUS, EF_JUNO, EF_AUTOWAH, EF_DELAY, EF_RUMBLE, EF_REVERB, NEF };
enum { EFG_NONE, EFG_DIRT, EFG_COLOR, EFG_MOVE, EFG_SPACE, NEFG };   /* the groups (PRESETS steps through them) */
extern const char *const EFG_NAME[NEFG];
/* how a knob's value reads: 00..99, a percentage, bipolar (64 = 0), the LFO's rate (synced divisions, then Hz), one of
 * a list (efx_list_name), the delay's time (a division of the tempo), a ratio (ring's) */
enum { EK_NN, EK_PCT, EK_BIP, EK_SYNC, EK_LIST, EK_DIV, EK_RATIO };
#define EFX_BLOCK 2048                       /* the memory's unit, bytes */
#define EFX_GROW 4                             /* a delay growing (its time longer): its wet to dry over 4 blocks first */
#define EFX_GROWN 0x80u                        /* a delay's mmd: growing; its dry held at its mix's level (the fade lowers
                                                * the wet alone, its memory's clear passes the dry so, it starts again at
                                                * its mix: no swell), till it runs again (efx_slot_run) */
typedef struct {
    const char *name;
    uint8_t group;                           /* EFG_* */
    uint8_t voice;                           /* 1: on a synth track it runs on each voice (ring: the note's pitch; tape:
                                              * its wow bends the voices) */
    uint8_t word[7];                         /* knobs 1..3, then the SEL list's extras (efx_knob_name; extras 0: none);
                                              * knob 4 is always the mix */
    uint8_t def[3], kind[3];                 /* the knobs' defaults (0..127) and kinds (EK_*) */
    uint8_t xdef[4];                         /* the extras' */
} efx_info_t;
extern const efx_info_t EFX[NEF];
const char *efx_knob_name(int ef, int k);    /* k 0..2 a knob ("-": none), 3..6 an extra (0: none) */
const char *efx_knob_word(int ef, int mode, int k);   /* the same, in the reverb's mode (the plate's knob 2 its motion,
                                                       * shimmer's damp its shimmer) */
float efx_lfo_hz(int v, float bpm);          /* an EK_SYNC knob (the LFO's rate): synced divisions 0..7, then 0.1 .. 30 Hz */

/* ---- the one pool: every slot's memory (a track's two, FX I / II, the master's), in EFX_BLOCK blocks ----
 * A slot takes a contiguous run of blocks, first fit, when its effect needs memory (efx_need: by the effect, the
 * reverb's mode and the place: a send's chorus and lofi lines are longer, for its lag; a track's delay and reverb keep
 * their states at the start of theirs, efx_big_t), given back when the effect changes; a slot that finds none passes
 * dry ("not playing: memory full"). Whoever holds memory keeps it: a slot asking finds its run where no other holds
 * (the pages offer only what would: engine_fits). Those waiting: the sends first, then the master's slot (the
 * engine's, each block: efx_send_used), then the tracks in order, slot 1 then 2 (synth.c); a project loaded starts the
 * pool over in that order. 78 blocks (61 until 2026-11-25, the SRAM won back since): the
 * default sends (FX I's delay 24, FX II's lush 26) leave 28, a track's delay (24) beside them; FX I's delay and FX II's
 * largest reverb (32) leave 22. The demo build (FM1B_MIRROR: the live mirror, ./build.sh --demo) one fewer, for the
 * mirror's RAM: a slot that finds none passes dry, as always */
#if FM1B_MIRROR
#define EFX_NB 77
#else
#define EFX_NB 78
#endif
#define EFX_INS_N 1024                           /* int16 a block (2 KB) */
extern int16_t efx_pool[EFX_NB][EFX_INS_N];
/* a set of the pool's blocks, a bit each, in 32-bit words (block b: w[b / 32] bit b % 32) */
#define EFX_NW ((EFX_NB + 31) / 32)
typedef struct { uint32_t w[EFX_NW]; } efx_mask_t;
void efx_mark(efx_mask_t *m, int at, int nb);          /* blocks at .. at + nb - 1 into m */
int efx_meets(const efx_mask_t *m, int at, int nb);    /* any of them in m */
int efx_fit(const efx_mask_t *m, int need);            /* the first run of need blocks none of m, -1 */
extern efx_mask_t efx_send_used;                 /* the blocks the sends and the master hold */
extern uint8_t efx_send_wait;                    /* 1: a send or the master waits for memory (a track's given back
                                                  * goes to it first, the next block); 2: the pool starts over (a
                                                  * project loaded: every slot gives its memory back) */
/* memory newly given (or an effect changed on it) is cleared a slice at a time, not at once in the audio interrupt (64
 * KB at once: a block's overrun): efx_clear_step clears up to max of the nb blocks at mem still to clear (*left of
 * them, from the start), returns how many it cleared. A send and the master EFX_CLR blocks (8 KB) a block, the tracks'
 * slots a block of the pool for each 64 frames rendered (the same, a 256-frame block); the holder passes dry (a send:
 * silent) until its memory is clear: a 64 KB line in 8 blocks of 256 frames (46 ms) */
#define EFX_CLR 4
int efx_clear_step(int16_t *mem, int nb, uint8_t *left, int max);
int efx_need(int ef, int mode, int send);        /* the blocks effect ef (the reverb in mode) needs on a track (send 0),
                                                  * a send (1) or the master (2: as a track's, and tape's wow) */
int32_t efx_rv_ring(int mode, int nb, int send); /* the reverb's line in nb blocks: its length, int16 */
/* a delay's memory follows its time (the user's: a busy project's two delays held what lush needed): its "mode" is the
 * blocks it asks for (efx_need gives them back as they are), what its time needs at the tempo now and never fewer than
 * it holds (turned down, it keeps them: the warp as before; up past them, more when the pool has a run, else it keeps
 * its own and halves its time, efx_delay_secs). efx_delay_len: a line's length in nb blocks */
int efx_delay_blocks(int time, int fine, float bpm, int held, int send);
int32_t efx_delay_len(int nb, int send);
const char *efx_list_name(int ef, int k, int v);   /* an EK_LIST knob's value's name (juno's modes) */

/* ---- the dirt and the color: drive, fold, crush, ring, tilt, tape, sampler (the character's) ----
 * A stage's numbers x[8] (efx_char_coefs, from amount and tone 0..1 and, for ring, the note's frequency; x[7] the mix
 * 0..1, the caller's) and its states z[8] (zeroed: a fresh stage), on a block of one side (stereo 0) or both, in
 * place: the mix of the dry and the effect.
 *   drive:   a soft (tanh-like) saturation, the gain made up so the level holds; tone a low-pass after it (400 Hz ..
 *            20 kHz: open)
 *   fold:    a sine wavefolder, gain made up; tone its bias (64 = symmetric; off centre: even harmonics)
 *   crush:   fewer bits and a lower sample rate; tone which falls (0 the rate only, 127 the bits only)
 *   ring:    a sine at a ratio of freq (tone: 0.25:1 .. 7:1 in 16 steps); amount its depth. Its amount glides
 *            (efx_ring_glide, before the coefficients; efx_ring_from after them: its gains and its mix ramped across
 *            the chunk from where they were) and its phasor wants putting back to unit length once a chunk or a block
 *            (efx_ring_norm)
 *   tilt:    a tilt EQ: amount its pivot (150 Hz .. 6 kHz), tone the tilt (+-6 dB each side at the ends)
 *   tape:    a soft saturation (gain 0.5 .. 8, made up) into a two-pole roll-off (tone 1.5 .. 18 kHz); x[3] its wow's
 *            depth in cents, for whoever has a pitch to bend (the voices; the master and a send: a short line,
 *            efx_slot_run)
 *   sampler: low-passed, held at 26 .. 11 kHz and 14 .. 11 bits by the amount, its output filter (tone 2 .. 20 kHz) */
void efx_char_coefs(int ef, float a, float t, float freq, float *x);
void efx_char_stage(float *x, float *z, int ef, int stereo, float *xl, float *xr, int k);
float efx_ring_glide(float a, float was, float *run);   /* the ring's amount, glided from was (*run 0: it starts there) */
void efx_ring_from(float *x, float *z, const float *was, int len);   /* (after the coefficients) its gains and its mix
                                                 * (x[7]) ramped from was (the last x[0], x[1], x[7]) across the next len
                                                 * frames: x[4], x[6], x[5] where they are, z[7] the frames left */
void efx_ring_norm(float *z);
float efx_char_hz(int ef, int knob, float u);   /* a knob (0 amount, 1 tone; 0..1) that is a frequency: its Hz (the
                                                 * pages); 0: not one */
/* the page's picture of the stage (no state) from its knobs' bytes (amount, tone, mix): drive, tape and fold the
 * transfer at u (-1..1); crush and sampler a cycle of a sine (u 0..1) held and quantised; ring two cycles of a sine
 * ring-modulated (its peak at 1); tilt the response in dB / 6 from 50 Hz (u 0) to 20 kHz (u 1). The mix is in it */
float efx_char_picture(int ef, int amt, int tone, int mix, float u);

/* ---- the sends' effects: chorus, delay, lofi, phaser, reverb (FX I / II's, fx.c) ----
 * Each takes a mono block x (filtered: the slot's) and adds its stereo return, wet only. Their numbers (pt: targets,
 * pc: gliding toward them, the slot's ~10 ms) from the knobs: efx_send_nums. The reverb in one of its modes: the
 * plate (9W9's combs), the PlayStation's SPU reverbs (psxrev.h, from schwung-psxverb: MIT), lush (Dattorro's plate at
 * 22.05 kHz) and shimmer (lush with its output an octave up fed back: Chordism's, MIT). Saved by number: a new mode
 * at the end */
enum { RV_PLATE, RV_ROOM, RV_STUDIO_S, RV_STUDIO_M, RV_STUDIO_L, RV_HALL, RV_HALF_ECHO, RV_SPACE_ECHO,
       RV_CHAOS_ECHO, RV_ECHO_ONE, RV_LUSH, RV_SHIMMER, NRV };
static inline int rv_lush(int mode) { return mode == RV_LUSH || mode == RV_SHIMMER; }   /* (shimmer is lush's) */
#define NSPU RV_ECHO_ONE                           /* the SPU modes: RV_ROOM .. RV_ECHO_ONE */
extern const char *const RV_NAME[NRV];         /* "plate", "room", "studio S", ... */
#define FX_DLY_LEN 22400              /* the delay's longest lines: int16 samples a side, at half rate (22.05 kHz): 1.01 s
                                       * plus a 128-sample margin (the second core's lag, 256 frames at most): 1/4D to 89 BPM,
                                       * 1/4 to 60 (2026-11-27; was 0.5 s). A delay takes what its time needs of the pool
                                       * (efx_delay_blocks), so only a long time costs it */
#define FX_RV_LEN 32768               /* int16 samples: the reverb's longest line (an SPU mode at x4 up to 32766: the chaos
                                       * echo's and the delay's 1FFFh x 4); each mode's own: efx_rv_ring. (Its states'
                                       * clearing and sleeping count to it whatever the line) */
#define EFX_CHO_N 2048                /* a send's chorus line: 46 ms (int16; the lofi's spread reads it; its length the
                                       * send's lag's, a block late) */
#define EFX_SMEAR_N 1566              /* smear's allpasses and the send's wait: after a delay's lines */
#define EFX_CHO_REACH ((15 + 8) * 441 / 10 + 2)   /* how far back a chorus reads: its centre 15 ms and its depth up to
                                       * 8 ms (0.3 + 7.7), a sample more between: a slot's line (no lag) is EFX_INS_N */
/* the delay's time: one of FX_DL_N divisions of the tempo (1/32 .. 1/4D; fx_delay_div), its name's note and mark (T, D
 * or none). The knob: 1/32 .. 1/8D a tenth each as before (0..115), then 1/4 (116..121) and 1/4D (122..127, 2026-11-27,
 * project format 35: 1/4's top until then, project.c dl_old) */
#define FX_DL_N 11
#define FX_DL_14D 122                 /* the knob's first 1/4D */
static inline int fx_delay_div(int time)
{
    time = time < 0 ? 0 : time > 127 ? 127 : time;
    return time >= FX_DL_14D ? FX_DL_N - 1 : time * 10 >> 7;
}
static inline int fx_delay_at(int d)  /* the knob in the middle of division d */
{
    return d <= 0 ? 6 : d >= FX_DL_N - 1 ? 124 : d == FX_DL_N - 2 ? 118 : 13 * d + 6;
}
extern const uint8_t FX_DL_NOTE[FX_DL_N];
extern const char FX_DL_MARK[FX_DL_N];
float efx_delay_secs(int time, int fine, float bpm, int32_t len, int *halves);   /* the delay's time at a tempo, and the times the line
                                                                     * halves it to fit (0: as set) */
float fx_delay_fb(int v);                    /* the delay's REPEATS 0..127: its feedback (127: 1, held) */
void efx_delay_width(float *wa, float *wb, int w);   /* PING WIDTH 0..127: a side's echo into its own side, the other
                                                     * (into a delay's ta, tb: its wa, wb glide there) */

typedef struct { int32_t w; float ph, s1, s2, hl, hr, fz, rg, sd, fb; } efx_chorus_t;   /* (rg, sd, fb: the depth, the
                                              * width's side and the feedback as they ramp; rg 0: fresh) */
typedef struct { int32_t w; float ph, hold, in1, out1, wph, late; } efx_lofi_t;   /* (late: the width's, ramping) */
typedef struct { float xl, yl[4], xr, yr[4], ph, zl, zr, al, ar, fb; } efx_phaser_t;   /* (fb: the feedback, ramping) */
typedef struct {
    int16_t *l, *r;                           /* its lines (len a side) */
    int32_t len;                              /* a line's length: what its memory gives (efx_delay_len), FX_DLY_LEN at
                                               * most */
    int32_t w, quiet, ph;                     /* (the line at half rate: ph, the pair's second frame) */
    float cur, lpl, lpr, al, ar;              /* (the pair being summed into the slot at w) */
    float wa, wb, ta, tb;                     /* PING WIDTH: a side's echo into its own side, into the other, gliding
                                               * to ta, tb (efx_delay_width) */
    float wph, wdl, wdr, wsl, wsr, hl, hr;    /* wobble: the LFO's phase, the drifts, the offsets as they glide; the loop's
                                               * high-pass a side */
    uint32_t rnd;                             /* (the drift's noise) */
    int32_t sap[5];                           /* smear: its allpasses' places in its line (EFX_SMEAR_N), the send's
                                               * wait's */
} efx_delay_t;
#define EFX_SPU_NREG 30
typedef struct {                              /* an SPU mode's (psxrev.h) */
    int32_t a[EFX_SPU_NREG];                  /* the addresses at the size now (samples) and the dAPF displacements */
    float viir, vwall, vapf1, vapf2, vcomb[4];
    float in[128];                            /* the decimator's input, 44.1 kHz (mono): a ring of 64, each sample written
                                               * twice (at ip and ip + 64) so the taps read it straight, without a mask */
    float up[2][64];                          /* the interpolators' input, 22.05 kHz: rings of 32, written twice the same
                                               * way */
    float out[2][2];                          /* the two samples a tick gives, a side */
    float tl, th, tr, thr;                    /* the tone's states */
    uint32_t base;
    int32_t clear;                            /* samples of the work area still to clear */
    int32_t quiet;                            /* 44.1 kHz samples since anything but zero was written or came in */
    uint8_t ip, up_w, phase, mode;
} efx_spu_t;
#define EFX_LU_N 13
typedef struct {                              /* lush's */
    int32_t len[EFX_LU_N];                    /* the lines' lengths at the size now, 22.05 kHz samples */
    uint32_t base;
    int32_t clear, quiet;                     /* (as the SPU's) */
    float lp, hold, lfo, dmp[2], fb[2], prev[2], cur[2], tl, th, tr, thr, scale, pre;   /* (scale, pre: the size and
                                              * the pre-delay as they glide; 0: fresh) */
    float sh_d, sh_lp, sh_hp, sh_x1;          /* shimmer: its heads' distance, its low-pass, its high-pass */
    uint8_t phase;
} efx_lush_t;
typedef struct {
    int16_t *line;                            /* its line: n long (the plate uses 11.4 k of it; an SPU mode and lush wrap
                                               * their addresses at n) */
    uint32_t n;
    uint8_t mode;                             /* RV_*, running */
    float shm;                                /* shimmer's feedback (0: lush) */
    int32_t cpos[4], apos[4], quiet;          /* the plate */
    float cdmp[4], cd[4], ph, hp, tl[2], tr[2];   /* (tl / tr: the tone's low-pass and high-pass a side) */
    union {                                   /* the SPU reverb or lush (one mode at a time) */
        efx_spu_t spu;
        efx_lush_t lu;
    };
} efx_reverb_t;
typedef struct {                              /* a send's effects (one runs at a time: its states, its numbers) */
    int16_t *cho;                             /* the chorus's line (EFX_CHO_N), the lofi's; a delay's smear's */
    float pt[6], pc[6];                       /* the effect's numbers: targets, and gliding */
    efx_chorus_t chorus;
    efx_lofi_t lofi;
    efx_phaser_t phaser;
    efx_delay_t delay;
    efx_reverb_t reverb;
} efx_send_t;
/* the knobs (four, 0..127) of effect ef (EF_CHORUS, EF_DELAY, EF_LOFI, EF_PHASER, EF_REVERB in mode) to its numbers
 * (pt; the reverb's shimmer too); the delay's wobble and smear 0..127 */
void efx_send_nums(efx_send_t *e, int ef, int mode, const uint8_t *k, float bpm, int wobble, int smear);
void efx_nums(float *pt, float *shm, int ef, int mode, const uint8_t *k, float bpm, int wobble, int smear);   /* (into pt,
                                                 * the reverb's shimmer into shm: a slot's) */
void efx_send_reset(efx_send_t *e, int mode);   /* a fresh effect: its states at rest (its numbers as pt; the reverb's
                                                 * mode); not the lines (but the SPU's and lush's clear themselves) */
void efx_send_run(efx_send_t *e, int ef, const float *x, float *ol, float *orr, int m, int any, int lag);
int efx_send_awake(const efx_send_t *e, int ef);
/* the chorus and the phaser on their own (a track's slot): p the numbers as efx_send_t pc's (chorus: rate Hz, depth
 * in samples, feedback, width 0..1; phaser: centre Hz, depth octaves, rate Hz, feedback); the
 * phaser on two inputs (a send: the same twice) */
void efx_chorus(efx_chorus_t *c, int16_t *line, int32_t len, int hp, const float *p, const float *x, float *ol, float *orr,
                int m, int lag);   /* (len: its line's, a power of 2; hp: its wet high-passed, a send's) */
void efx_phaser(efx_phaser_t *s, const float *p, const float *xl, const float *xr, float *ol, float *orr, int m);

/* ---- a track's slot's effects but the dirt and the color (synth.c's: its bus): tremolo, rat, phaser, univibe, chorus,
 * lofi, juno, autowah, delay, rumble, reverb ----
 * On its pair (bl, br: n frames, in place), its states st (zeroed: fresh), its memory mem (efx_need's blocks from the
 * pool; 0: none, it passes dry), its three knobs kn (0..127, the LFO on them), its extras xt, its mix (0..127: dry ..
 * wet; rumble's its level, the dry whole), rate the LFO's speed in Hz (tremolo, univibe: the knob's, efx_lfo_hz), the
 * tempo; in what rumble hears (0: the pair's middle; a kit: its kicks alone). ph: its phase (turns; rumble's: beats
 * since its hit). The delay and the reverb are the sends' on the pair's middle, no lag, their states at the start of
 * their memory (efx_big_t), mixed */
typedef struct {                                 /* a track's delay or reverb: at the start of its memory */
    float pt[6], pc[6];                          /* its numbers (efx_nums), gliding as a send's */
    union {
        efx_delay_t d;
        efx_reverb_t r;
    } u;
} efx_big_t;
#define EFX_BIG_N ((int32_t)((sizeof(efx_big_t) + 7u) / 8u * 4u))   /* (int16 words: the lines after it, 8-aligned; */
#define EFX_BIG_DN ((int32_t)((offsetof(efx_big_t, u) + sizeof(efx_delay_t) + 7u) / 8u * 4u))   /* a delay's, smaller) */
int efx_big_awake(const int16_t *mem, int ef);   /* a track's delay or reverb still rings (its slot's tail) */
typedef union {
    float z[16];
    efx_chorus_t chorus;
    efx_phaser_t phaser;
    efx_lofi_t lofi;
} efx_ins_t;
void efx_insert(int ef, efx_ins_t *st, float *ph, int16_t *mem, int nb, const float *kn, const uint8_t *xt, int mix, float rate,
                float bpm, const float *in, float *bl, float *br, uint32_t n);
/* the dirt and the color (efx_char_stage: drive, fold, crush, ring, tilt, tape, sampler), against the rest (efx_insert) */
static inline int efx_is_char(int ef) { return ef == EF_DRIVE || (ef >= EF_FOLD && ef <= EF_SAMPLER); }
/* a track's slot (synth.c, its bus; a kit's too): this block's effect, knobs, mix and extras, its states */
typedef struct {
    union {
        struct { float x[8], z[8], in[2]; } ch;   /* the dirt and the color: the stage's numbers, its states, what
                                                  * the numbers are from (and cef) */
        efx_ins_t in;                             /* the others' */
    } u;
    float k[3], ph;                               /* its knobs this block (0..127: the LFO on them), its phase */
    float g[3], gm;                               /* the knobs and the mix as they glide there (efx_slot_run) */
    uint8_t ef, zef, cef;                         /* its effect this block (EF_OFF: none on the bus), its states', its
                                                   * numbers' (ch) */
    uint8_t mix, xt[4];                           /* its mix 0..127, its extras */
    int8_t at;                                    /* its memory: its first block of the pool (-1 none) */
    uint8_t nb, mef, mmd;                         /* how many, the effect (and the reverb's mode) it was given for */
    uint8_t clr;                                  /* its blocks still to clear (efx_clear_step: dry till then) */
    uint8_t live, fo, rise;                       /* live: it has run (or been off) while the sound played: a new
                                                   * effect starts from dry (rise: its input from silence, pieces
                                                   * left); fo: this block fades its effect out (efx_slot_want) */
} efx_slot_t;
/* a slot on a pair (bl, br: n frames), in place: this block's effect, knobs, mix and extras as sl has them (a new
 * effect starts its states clean; live, from dry, its mix gliding up). The knobs and the mix glide (~10 ms: EFX_GLIDE_N
 * frames) and nothing steps on a turn: the dirt and the color ramp their numbers a sample at a time, the others take
 * their knobs a 32-frame piece at a time and ramp their mix (a list's knob, juno's mode, at once: its reads glide); a
 * knob at rest runs as it always did. The dirt and the color on both sides (both: a kit's bus, a send, the master; else a
 * pair whose sides are the same, mono voices in the middle, runs once), ring at a ratio of C4 (EFX_RING_C4: off the
 * voices), tape with its wow when mem holds its line (efx_need: the master's, a send's; a synth track's bends its voices,
 * a kit's has none); the rest efx_insert's, their LFO's rate the first knob's at bpm, rumble hearing in (0: the pair's
 * middle). mem: its memory (0: none, an effect that needs it passes dry); sl->clr: not yet clear, dry */
#define EFX_RING_C4 261.63f
#define EFX_WOW_N 128                            /* tape's wow's line (int16 a side) and its centre: 1.5 ms, the pair */
#define EFX_WOW_AT 64.0f                         /* that late while tape is on (as the master color's was) */
#define EFX_GLIDE_N 441.0f                       /* the glides' time, frames (10 ms): a call of n frames goes
                                                  * n / (n + EFX_GLIDE_N) of the way */
void efx_slot_run(efx_slot_t *sl, int both, float bpm, const float *in, int16_t *mem, float *bl, float *br, uint32_t n);
/* the effect a slot runs this block (its caller's, before its memory): ef (the reverb in mode) wanted; one running
 * while the sound plays (live) that it changes from runs a block more, fading to dry (fo set: the caller leaves its
 * knobs, mix and extras as they were), then ef from dry. Returns the effect to run */
int efx_slot_want(efx_slot_t *sl, int ef, int mode);
float efx_rumble_secs(float k);                  /* rumble's decay knob (0..127): its RT60, seconds */
float efx_sin(float ph);                         /* (out of line: a sine of turns, a one-pole's coefficient at hz) */
float efx_op_coef(float hz);
