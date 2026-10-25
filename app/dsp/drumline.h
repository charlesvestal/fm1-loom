/* SPDX-License-Identifier: MPL-2.0 */
/* LOOM-8 drumline: the kit engine, a drum synth with eleven keys on the black keys, each a model
 * instance with the model's own four knobs, random, pan, volume and a play mode, its own envelope
 * (attack, hold, decay, lag) and its own filter (low-pass, high-pass or band-pass, cutoff, resonance,
 * the envelope's reach, drive) (synth.h TP_DRUM(key, DP_*)). Eleven models (DP_MODEL, DM_*): kick, snare,
 * hat, clap, shaker, tambourine, string, conga, tom, fm, sample. Ten are analytic (sines with pitch
 * envelopes, filtered noise bursts, six-square metal, a Karplus-Strong string, 2-op FM), the eleventh plays
 * a sample of the bank (dsp/sample.h). Up to NDVOICE keys at once. The track's TP_A..TP_R are the kit's
 * envelope: lag (a delay before every hit, 0..60 ms), attack (a fade-in, 0..100 ms), random (every model's
 * knobs vary per hit, the kick's pitch excepted), length (0.25x .. 4x the model's length, 64 = 1x): neutral
 * is 0 / 0 / 0 / 64. Float, fastmath.h. */
#pragma once
#include <stdint.h>
#include "synth.h"
#include "sample.h"

#define DL_NSOUND 11                 /* keys: the black keys, a model instance each */
#define NDVOICE 8
/* the ten models and each one's four knobs (what each does: drumline.c start()):
 *   kick       pitch  length  punch   tone
 *   snare      pitch  length  wires   punch
 *   hat        pitch  length  metal   focus     (metal: noise left, the six squares and their ring-mod
 *                                              right; focus: the high-passed shimmer left, a bandpass right)
 *   clap       tone   length  spread  tail
 *   shaker     color  length  swell   return    (swell: the fade-in; return: how soon the shake comes back)
 *   tambourine tone   length  jingle  hiss      (jingle: the metal partials; hiss: the noise)
 *   string     pitch  length  bright  pluck     (bright: the loop's damping; pluck: the strike)
 *   conga      pitch  length  slide   drive
 *   tom        pitch  length  sweep   rate
 *   fm         pitch  length  mod     feedback
 *   sample     sample pitch   start   reverse   (a sample of the bank; pitch +-24 semitones about its own
 *                                              rate; start 0..99 % of it; reverse: from its end back to the
 *                                              start. Its length is the sample's, or the key's decay)
 * DM_HIHAT is the hat, DM_TAMB the tambourine, DM_RESO the string. DM_DEFAULT[m] = the model's knobs on
 * its key of the default kit (the low tom's for the tom; the tambourine and the sample, not in the kit,
 * their own). */
enum { DM_KICK, DM_SNARE, DM_HIHAT, DM_CLAP, DM_SHAKER, DM_TAMB, DM_RESO, DM_CONGA, DM_TOM, DM_FM, DM_SAMPLE,
       /* the x models, after Omodaka9375's MACHINEDRUM (MIT, a Web Audio drum machine): its eight engines, seven
        * knobs each (its PITCH, DECAY, TONE and SWEEP, then NOISE, FM and DRIVE on the second page); and clang,
        * digisophie's Sophie core (MIT: dsp/digisophie.c), eight knobs; sophie, Sophie's own voice (Matt Estela,
        * MIT; after SPH-16's port), its seven models on one knob */
       DM_XKICK, DM_XSNARE, DM_XHAT, DM_XCLAP, DM_XTOM, DM_XRIM, DM_XFM, DM_XNOISE, DM_CLANG, DM_SOPHIE,
       NDM };                                        /* (saved by number: a new model goes last; development builds
                                                      * had 21 more, 11..31: project.c keys_fix) */
#define DM_ISX(m) ((m) >= DM_XKICK && (m) <= DM_XNOISE)
extern const char *const DM_NAME[NDM];
extern const char *const DM_KNOB[NDM][4];
extern const uint8_t DM_DEFAULT[NDM][4];
/* a model with eight knobs has its second four on the key's second Engine page (synth.h TP_DRUMX); a model with
 * four (all of them, now) has 0 there */
extern const char *const DM_KNOB2[NDM][4];
extern const uint8_t DM_DEFAULT2[NDM][4];
extern const uint8_t DM_NKNOB[NDM];
extern const char *const SPH_MODEL_N[7];             /* sophie's MODEL (its knob 5): fuse .. boom, kick */
extern const char *const CLANG_MODEL_N[4];           /* clang's: fuse boom pipe shard (digisophie's order) */               /* each model's knobs (4; up to 8) */
static inline int dm_knobs(int m) { return m >= 0 && m < NDM ? DM_NKNOB[m] : 4; }
/* play modes: one-shot, gated (while the key / step is held), loop, choke groups 1..3 (one-shots that cut
 * each other within the group) */
enum { PM_ONESHOT, PM_GATED, PM_LOOP, PM_CHOKE1, PM_CHOKE2, PM_CHOKE3, NPM };
extern const char *const PM_NAME[NPM];
extern const char *const DL_SOUND_NAME[DL_NSOUND];     /* the default kit's key names: "kick", "snare", "hat", "open", ... */
extern const uint8_t DL_DEFAULT_KIT[DL_NSOUND][DP_N];  /* DP_* of each key of a new drum track */

typedef struct drumline drumline_t;
#define DRUMLINE_BYTES 4608                            /* 8 voices (each with its key's envelope and filter state),
                                                        * four sample windows (80 B each), the dynamic filter and
                                                        * two strings (int16, 512 each) */

void drumline_init(drumline_t *d, const uint8_t *par);  /* par: the track's par[] (reads TP_DRUM0.., TP_A..TP_R at a hit,
                                                         * TP_SEND* each chunk) */
/* a hit: the key's model with its knobs as they are now (plus the random variations), vel 1..127, pitch_semi
 * added to the model's pitch (the fm model: steps), short_gate caps the length at 60 ms (a short step),
 * long_tail x4 (the punch-in). A key retriggers its own voice; a choke-group key fades the group's others
 * (5 ms). Gated and loop modes hold the voice until drumline_release(sound). The key's envelope times and
 * lag are read at the hit; its filter and drive every chunk (a knob turned on a long sound is heard). */
void drumline_hit(drumline_t *d, int sound, int vel, int pitch_semi, int short_gate, int long_tail);
void drumline_release(drumline_t *d, int sound);       /* the key / step ends: gated and loop modes stop */
void drumline_choke(drumline_t *d);
/* the dynamic filter (synth.h FT_DYNAMIC on a drum track): the Filter page's knobs = low cut (12 dB, 20 Hz ..
 * 2 kHz, 0 = off), compression (one knob: threshold, ratio and make-up together), drive (tanh-shaped, 1..16x),
 * hi cut (12 dB, 300 Hz .. 20 kHz, 127 = off); any other type = off. It runs on the track's dry L/R after the
 * keys are summed; the sends are taken before it. Call it from the audio context when a knob changes (a few
 * exps; nothing to do per block). */
void drumline_filter(drumline_t *d, int type, int locut, int comp, int drive, int hicut);
/* render n frames: dry L/R (through the dynamic filter when on) and sends (the track's TP_SEND*), added into
 * the buffers. Stack: about 400 bytes (three 32-sample sub-block buffers) */
void drumline_render(drumline_t *d, float *l, float *r, float *s1, float *s2, uint32_t n);
int drumline_sounding(const drumline_t *d);
int drumline_voices(const drumline_t *d);              /* the voices sounding (the engine's core split) */
extern volatile uint8_t drumline_hitv[DL_NSOUND];      /* the last hit of each sound, 255 = full (UI lets it fall) */
#ifdef PLAT_HOST
extern uint32_t (*drumline_host_clock)(void);    /* the simulator's profiler: each key's time rendering [0] and starting */
extern uint32_t drumline_host_cost[DL_NSOUND][2];   /* [1] its hits, in the clock's units (app/host.c LOOM_WORK) */
#endif
