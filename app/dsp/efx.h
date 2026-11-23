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
