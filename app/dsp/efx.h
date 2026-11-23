/* SPDX-License-Identifier: MPL-2.0 */
/* LOOM-8's effects library (the one effects list: docs/superpowers/specs/2026-11-25-one-effects-list-design.md): the
 * catalog every slot picks from (a track's two, FX I / II, the master: one list, the same names, knobs and code) and
 * each effect's DSP, once. Its users keep their own storage and pages for now (synth.c: a voice's or a bus's
 * character, a track's insert; fx.c: the sends and the master color); they call in here.
 *
 * Everything per sample runs in the audio interrupt (or the second core's job); the coefficients' functions in the
 * main loop or once a block. Float, fastmath.h; no double, no libm. */
#pragma once
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
typedef struct {
    const char *name;
    uint8_t group;                           /* EFG_* */
    uint8_t blocks;                          /* the memory it needs, EFX_BLOCK each (0: none) */
    uint8_t voice;                           /* 1: on a synth track it runs on each voice (ring: the note's pitch; tape:
                                              * its wow bends the voices) */
    uint8_t word[7];                         /* knobs 1..3, then the SEL list's extras (efx_knob_name; extras 0: none);
                                              * knob 4 is always the mix */
    uint8_t def[3], kind[3];                 /* the knobs' defaults (0..127) and kinds (EK_*) */
    uint8_t xdef[4];                         /* the extras' */
} efx_info_t;
extern const efx_info_t EFX[NEF];
const char *efx_knob_name(int ef, int k);    /* k 0..2 a knob ("-": none), 3..6 an extra (0: none) */
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
 *            (efx_ring_glide, before the coefficients) and its phasor wants putting back to unit length once a chunk
 *            or a block (efx_ring_norm)
 *   tilt:    a tilt EQ: amount its pivot (150 Hz .. 6 kHz), tone the tilt (+-6 dB each side at the ends)
 *   tape:    a soft saturation (gain 0.5 .. 8, made up) into a two-pole roll-off (tone 1.5 .. 18 kHz); x[3] its wow's
 *            depth in cents, for whoever has a pitch to bend (the voices; the master: a short line, fx.c)
 *   sampler: low-passed, held at 26 .. 11 kHz and 14 .. 11 bits by the amount, its output filter (tone 2 .. 20 kHz) */
void efx_char_coefs(int ef, float a, float t, float freq, float *x);
void efx_char_stage(const float *x, float *z, int ef, int stereo, float *xl, float *xr, int k);
float efx_ring_glide(float a, float was, float *run, float *x);   /* the ring's amount, glided from was (*run 0: it
                                                                 * starts there) */
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
#define FX_DLY_LEN 11200              /* the delay's lines: int16 samples a side, at half rate (22.05 kHz): 0.5 s plus a
                                       * 128-sample margin (the second core's lag, 256 frames at most; the pool's budget) */
#define FX_RV_LEN 32768               /* int16 samples: the reverb's line (the plate uses 11.4 k of it; an SPU mode at x4
                                       * up to 32766: the chaos echo's and the delay's 1FFFh x 4) */
#define EFX_CHO_N 2048                /* the chorus's line: 46 ms (int16; the lofi's spread and smear's allpasses read it) */
/* the delay's time: one of FX_DL_N divisions of the tempo (1/32 .. 1/4; fx_delay_div), its name's note and mark (T, D
 * or none) */
#define FX_DL_N 10
static inline int fx_delay_div(int time) { return (time < 0 ? 0 : time > 127 ? 127 : time) * FX_DL_N >> 7; }
extern const uint8_t FX_DL_NOTE[FX_DL_N];
extern const char FX_DL_MARK[FX_DL_N];
float efx_delay_secs(int time, int fine, float bpm, int *halves);   /* the delay's time at a tempo, and the times the line
                                                                     * halves it to fit (0: as set) */
float fx_delay_fb(int v);                    /* the delay's REPEATS 0..127: its feedback (127: 1, held) */

typedef struct { int32_t w; float ph, s1, s2, hl, hr, fz; } efx_chorus_t;
typedef struct { int32_t w; float ph, hold, in1, out1, wph; } efx_lofi_t;
typedef struct { float xl, yl[4], xr, yr[4], ph, zl, zr, al, ar; } efx_phaser_t;
typedef struct {
    int16_t *l, *r;                           /* its lines (FX_DLY_LEN a side) */
    int32_t w, quiet, ph;                     /* (the line at half rate: ph, the pair's second frame) */
    float cur, lpl, lpr, al, ar;              /* (the pair being summed into the slot at w) */
    float wa, wb;                             /* PING WIDTH: a side's echo into its own side, into the other (fx_width) */
    float wph, wdl, wdr, wsl, wsr, hl, hr;    /* wobble: the LFO's phase, the drifts, the offsets as they glide; the loop's
                                               * high-pass a side */
    uint32_t rnd;                             /* (the drift's noise) */
    int32_t sap[5];                           /* smear: its allpasses' places in the chorus line (idle while a delay), the
                                               * send's wait's */
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
    float lp, hold, lfo, dmp[2], fb[2], prev[2], cur[2], tl, th, tr, thr, scale;
    float sh_d, sh_lp, sh_hp, sh_x1;          /* shimmer: its heads' distance, its low-pass, its high-pass */
    uint8_t phase;
} efx_lush_t;
typedef struct {
    int16_t *line;                            /* FX_RV_LEN */
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
    int16_t *cho;                             /* the chorus's line (EFX_CHO_N), the lofi's and smear's */
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
void efx_send_reset(efx_send_t *e, int mode);   /* a fresh effect: its states at rest (its numbers as pt; the reverb's
                                                 * mode); not the lines (but the SPU's and lush's clear themselves) */
void efx_send_run(efx_send_t *e, int ef, const float *x, float *ol, float *orr, int m, int any, int lag);
int efx_send_awake(const efx_send_t *e, int ef);
/* the chorus and the phaser on their own (a track's insert): p the numbers as efx_send_t pc's (chorus: rate Hz, depth
 * in samples, feedback, width 0..1; phaser: centre Hz, depth octaves, rate Hz, feedback); the
 * phaser on two inputs (a send: the same twice) */
void efx_chorus(efx_chorus_t *c, int16_t *line, const float *p, const float *x, float *ol, float *orr, int m, int lag);
void efx_phaser(efx_phaser_t *s, const float *p, const float *xl, const float *xr, float *ol, float *orr, int m);
