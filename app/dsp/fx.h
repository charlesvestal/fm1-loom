/* SPDX-License-Identifier: MPL-2.0 */
/* LOOM-8 effects: the two send-effect tracks (FX I, FX II: each a slot of the one effects list, dsp/efx.h) and the
 * master section (3-band EQ, its effect slot, compressor, limiter, group levels).
 *
 * All run in the audio interrupt (the sends' delay, reverb and chorus on the second core, a block late). Float,
 * fastmath.h. */
#pragma once
#include <stdint.h>
#include "efx.h"                             /* the effects themselves (dsp/efx.c), the reverb's modes, the lines' sizes */

/* ---- the sends ----
 * A send is a slot of the one effects list, the same layout as a track's slot 1 (synth.h TP_SLOT[0]): any effect of
 * the catalog, its three knobs and its extras; its mix is the return level (the engine's). Its input is mono (the
 * tracks' sends summed), its return stereo, wet only. The chorus, the delay, the lofi, the phaser and the reverb are
 * the sends' own (efx_send_*: the knobs as the catalog has them, efx.h; the delay's fine, width, wobble and smear its
 * extras, the reverb's mode and damp, shimmer's amount, theirs); the others run as on a kit's slot (ring at a ratio of
 * C4, tape without its wow), wet. Memory from the pool (fx_need, fx_memory: the engine's, each block); without enough
 * the send is silent ("not playing: memory full": fx_room). Its filter page (high-pass, low-pass) on the send before the effect: one
 * filter instead of two, and the same result for the linear effects.
 * The delay's time: 1/32 1/16T 1/32D 1/16 1/8T 1/16D 1/8 1/4T 1/8D 1/4 of the tempo (one too long for the line, 0.5 s,
 * halved till it fits: 1/4 below 120 BPM, 1/8D below 90), fine +-25 %; its tone the repeats' low-pass, 800 Hz .. 19
 * kHz. The reverb's tone: a low-pass below the centre, a high-pass above it; the plate's damp its combs' damping, an SPU
 * mode's its reflections' low-pass (vIIR) closing; an SPU mode's size its delays x1 .. x4 of the registers, its decay
 * the wall's feedback (0.5x .. x1 at 64 .. as far as it stays stable). */
typedef struct fxslot fxslot_t;
#ifdef PLAT_HOST
#define FXSLOT_BYTES 1832             /* (the host's pointers are wider) */
#else
#define FXSLOT_BYTES 1808             /* (fx.c checks it: 1,808 B, 2026-11-25) */
#endif
void fx_init(fxslot_t *f);
void fx_set(fxslot_t *f, int ef, const uint8_t k[3], const uint8_t x[4]);   /* the effect (EF_*), its knobs and extras
                                                                           * (0..127; the reverb's mode its first) */
int fx_need(const fxslot_t *f);              /* the blocks of the pool its effect (as set) needs */
int fx_memory(fxslot_t *f, int16_t *mem, int nb);    /* its memory (0: none), each block: another place is cleared a
                                                     * slice a block (efx.h EFX_CLR), its effect fresh; the blocks
                                                     * still to clear (the send silent till 0) */
int fx_room(const fxslot_t *f);              /* its memory holds its effect (as set) */
int fx_effect(const fxslot_t *f, int *mode); /* its effect (EF_*) and the reverb's mode, as set */
void fx_tempo(fxslot_t *f, float bpm);
int fx_delay_halves(int time, int fine, float bpm);          /* the times the line halves it (0: as set; the delay's
                                                             * divisions FX_DL_N, its REPEATS' feedback: efx.h) */
void fx_filter(fxslot_t *f, int hp, int lp);                /* the slot's filter page: high-pass, low-pass 0..127 */
/* process n frames of the send (mono in) into out L/R (added) */
void fx_process(fxslot_t *f, const float *in, float *out_l, float *out_r, uint32_t n);
/* the same for a send that arrives lag samples late (0..256: the engine runs the effects a block behind, on the
 * second core): the chorus, the delay and the plate put it back in time in their lines (their output as without the
 * lag, but for the lines' rounding); the SPU modes and lush take it as that much more pre-delay. fx_lags: the effect
 * is one of those (the rest have no line to hide it in: the engine runs them in time) */
void fx_process_lag(fxslot_t *f, const float *in, float *out_l, float *out_r, uint32_t n, int lag);
int fx_lags(const fxslot_t *f);
void fx_clear(fxslot_t *f);                                 /* its memory and states at rest, its settings kept */
int fx_awake(const fxslot_t *f);                            /* the slot works this block: its input heard, or its line
                                                            * still ringing (a delay or reverb sleeps on silence; the
                                                            * others always work; no memory: none) */
/* before the one effects list (projects before format 29): an FX track's TP_ENGINE was a "pick" (chorus, delay,
 * lofi, phaser, then the reverb's modes in order: FXT_REVERB + mode), its four knobs TP_E1..E4, the delay's PING
 * WIDTH, WOBBLE and SMEAR TP_X1..X3. fx_pick_slot: those as the slot's values, in synth.h SL_* order (the effect,
 * knobs 1..3, the mix left as it is, extras 1..4) */
enum { FXT_CHORUS, FXT_DELAY, FXT_LOFI, FXT_PHASER, FXT_REVERB, NFXT };
#define NFXPICK (FXT_REVERB + NRV)
static inline int fx_pick_type(int pick) { return pick < FXT_REVERB ? (pick < 0 ? 0 : pick) : FXT_REVERB; }
static inline int fx_pick_mode(int pick) { return pick < FXT_REVERB ? 0 : pick - FXT_REVERB < NRV ? pick - FXT_REVERB : 0; }
void fx_pick_slot(int pick, const uint8_t e[4], int width, int wobble, int smear, uint8_t *o);


/* ---- master ---- */
/* the master's values: three pages, EQ (low, mid, high; 64 flat, then CLIP), its slot (the effect, knobs 1, 2, the mix;
 * its knob 3 and extras after the out page's), out (drums, melodic, comp, pump); then the out page's list (SEL): what
 * the pump follows, its release, accent, and the level (the drive into the limiter). Value 3 was the EQ's blend, gone.
 * The slot (Master FX: after the EQ, before the compressor) is a slot of the one effects list (dsp/efx.h, as a kit's
 * bus runs one: ring at a ratio of C4, the dirt and the color on both sides): any effect of the catalog, its three
 * knobs, its mix (0: the stage skipped), its extras. Memory from the pool after the sends', before the tracks'
 * (master_need, master_memory: the engine's, each block); without enough it passes dry ("not playing": master_room).
 * Tape's wow (0.55 Hz, a 6 Hz flutter an eighth as deep; ~1 .. 6 cents by the amount, times the mix, not mixed: no
 * comb with the dry) through a short line in its memory (a block), then the saturation into the roll-off, mixed.
 * Before format 30 the slot was the master color: off, tape or sampler (0, 1, 2: the catalog's 0, 7, 8 now), amount,
 * tone, mix: the same places
 * PUMP (X0X's master PUMP): every hit of its source lowers the whole mix by up to 24 dB (rising in 12 ms) and
 * lets it swell back over the release; it is keyed by the sequencer, not by audio, so a muted track drives it
 * too. pump by: PB_KICK any kick of a kit (its first key, or a key on the kick model), PB_T1.. (a kit: its kicks) any note of
 * track 1..8, PB_METRO the metronome's click. release 0..127: 10 .. 1500 ms (60 = 107 ms, X0X's); accent
 * 0 / 1: a hit pumps by its velocity (0.5 .. 1 of the depth) or always fully. */
enum { MS_EQ_LO, MS_EQ_MID, MS_EQ_HI, MS_CLIP, MS_CTYPE, MS_CAMT, MS_CTONE, MS_CMIX,
       MS_PERC, MS_MEL, MS_COMP, MS_PUMP, MS_PUMP_BY, MS_PUMP_REL, MS_PUMP_ACC, MS_LEVEL,
       MS_CK3, MS_CX1, MS_CX2, MS_CX3, MS_CX4, MS_DFILT, MS_MELF, MS_MFILT, MS_FRES, NMS };
                                                        /* (the slot: MS_CTYPE effect, MS_CAMT, MS_CTONE, MS_CK3 its
                                                         * knobs, MS_CMIX its mix, MS_CX1..4 its extras; the filter
                                                         * page: MS_DFILT the drums', MS_MELF the melodic group's,
                                                         * MS_MFILT the mix's, MS_FRES their resonance) */
#define MS_NMS29 16                   /* (format 29's values: up to MS_LEVEL) */
#define MS_NMS30 21                   /* (format 30's: up to the slot's extras) */
typedef struct master master_t;
/* the master filter (the page Master filter, format 31; three since 33): DJ filters on the drums (MS_DFILT: every
 * kit's pair and its two sends, before they meet the rest: master_group_filter, the engine's), on the melodic group
 * (MS_MELF: the synth tracks' sum and their share of the sends, before the kits add theirs and before the FX returns
 * join it: master_group_filter) and on the whole mix (MS_MFILT: after the returns, the compressor and the pump, before
 * CLIP and the limiter: master_process). One knob each: below the centre a low-pass (20 kHz at 61 down to 60 Hz at
 * 0), above it a high-pass (20 Hz at 67 up to 8 kHz at 127), 62..66 off (the stage skipped: the sound as without it,
 * sample for sample); one resonance for the three (MS_FRES) up to 0.9 of the way to self-oscillation (Q ~7), the level
 * made up for it. A 12 dB state-variable filter (stereo, the sends mono; Cytomic's, trapezoidal: it sweeps without
 * zipper); the cutoff and the resonance glide (~10 ms), a filter turned on fades in from its open end, one turned off
 * or across the centre fades out first. master_filter_hz: a knob's cutoff (0: off; negative: a high-pass's, as -Hz) */
#define MS_FILT_OFF 64
enum { MF_DRUMS, MF_MEL, MF_MIX, NMF };
float master_filter_hz(int v);
float master_filter_db(int v, int r, float hz);   /* its response at hz (the page's picture): knob v, resonance r */
/* the drums' (MF_DRUMS) or the melodic group's (MF_MEL) filter over a stretch, in place: the group's pair and its two
 * sends (mono), when it is on or fading (master_group_on); the engine's, before they meet the rest */
int master_group_on(const master_t *m, int k);
void master_group_filter(master_t *m, int k, float *l, float *r, float *s1, float *s2, uint32_t n);
/* the three filters' plan for the next block of n frames (the engine's, at the block's start, before any stretch): their
 * glides moved on once a block, the coefficients stepped across it */
void master_filters_block(master_t *m, uint32_t n);
enum { PB_KICK, PB_T1, PB_METRO = PB_T1 + 8, NPB };
#define MS_PUMP_REL_DEF 60
extern const char *const MS_NAME[NMS];
#ifdef PLAT_HOST
#define MASTER_BYTES 1280             /* (the host's pointers are wider) */
#else
#define MASTER_BYTES 1264             /* (fx.c checks it: 1,264 B, 2026-11-27, CLIP's meter: 1,256 B, 2026-11-26 (976 before the master filter, 1,036 with
                                       * two, 1,100 with three, + their coefficients kept, + their steps: a plan a
                                       * block); 1,428 with the master
                                       * color's line) */
#endif
void master_init(master_t *m);
void master_clear(master_t *m);                             /* its states at rest, its settings kept */
void master_set(master_t *m, int p, int v);                 /* 0..127 (EQ 64 = flat; the effect 0..NEF-1; pump by
                                                             * 0..NPB-1, accent 0..1; 3 clip 0..127) */
int master_max(int p);                                      /* a value's top: 127, NEF - 1, NPB - 1 or 1 */
void master_tempo(master_t *m, float bpm);
int master_need(const master_t *m);                         /* the blocks of the pool its effect (as set) needs */
int master_memory(master_t *m, int16_t *mem, int nb);       /* its memory (0: none), each block: as a send's
                                                             * (fx_memory: cleared a slice a block); the blocks still
                                                             * to clear */
int master_room(const master_t *m);                         /* its memory holds its effect (as set) */
float master_pump_ms(int v);                                /* the pump's release, ms */
/* a hit on pump source src (PB_*), velocity 1..127: the pump drops if it follows src (the audio interrupt) */
void master_key(master_t *m, int src, int vel, int kit);   /* kit: a kit's hit (only the melodic group ducks) */
/* perc and mel: the two groups (stereo, in place), summed with the fx returns (added into mel) */
void master_process(master_t *m, float *perc_l, float *perc_r, float *mel_l, float *mel_r, float *out_l, float *out_r,
                    uint32_t n);
/* the profiler's detail (engine.c sets both; 0: not timed): master_process's parts, running totals in master_clock's
 * units at master_prof[MP_*]: the groups and the pump, the EQ, the slot (Master FX), the tail (the compressor, the
 * level, CLIP, the limiter and the ceiling: one loop a sample, not apart) and the mix's filter (inside the tail, not
 * in MP_TAIL) */
enum { MP_GROUPS, MP_EQ, MP_SLOT, MP_TAIL, MP_MFILT, MP_N };
extern uint32_t (*master_clock)(void);
extern volatile uint32_t *master_prof;
float master_gr_db(const master_t *m);                      /* the compressor's gain reduction (UI) */
float master_pump_db(const master_t *m);                    /* the pump's, now (UI: falls back slowly) */
float master_clip_depth(master_t *m);                       /* CLIP's, now: how deep into its knee, 0..1 (once a block) */
#ifdef PLAT_HOST
extern int fx_host_stage;                                   /* the host's master taps (host.c LOOM_MST) */
#endif
