/* SPDX-License-Identifier: MPL-2.0 */
/* LOOM-8 effects: the two send-effect tracks (FX I, FX II: each a slot of the one effects list, dsp/efx.h) and the
 * master section (3-band EQ, its color, compressor, limiter, group levels).
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
 * the send is silent ("no memory": fx_room). Its filter page (high-pass, low-pass) on the send before the effect: one
 * filter instead of two, and the same result for the linear effects.
 * The delay's time: 1/32 1/16T 1/32D 1/16 1/8T 1/16D 1/8 1/4T 1/8D 1/4 of the tempo (one too long for the line, 0.5 s,
 * halved till it fits: 1/4 below 120 BPM, 1/8D below 90), fine +-25 %; its tone the repeats' low-pass, 800 Hz .. 19
 * kHz. The reverb's tone: a low-pass below the centre, a high-pass above it; the plate's damp its combs' damping, an SPU
 * mode's its reflections' low-pass (vIIR) closing; an SPU mode's size its delays x1 .. x4 of the registers, its decay
 * the wall's feedback (0.5x .. x1 at 64 .. as far as it stays stable). */
typedef struct fxslot fxslot_t;
#ifdef PLAT_HOST
#define FXSLOT_BYTES 1768             /* (the host's pointers are wider) */
#else
#define FXSLOT_BYTES 1744             /* (fx.c checks it: 1,740 B, 2026-11-25) */
#endif
void fx_init(fxslot_t *f);
void fx_set(fxslot_t *f, int ef, const uint8_t k[3], const uint8_t x[4]);   /* the effect (EF_*), its knobs and extras
                                                                           * (0..127; the reverb's mode its first) */
int fx_need(const fxslot_t *f);              /* the blocks of the pool its effect (as set) needs */
void fx_memory(fxslot_t *f, int16_t *mem, int nb);   /* its memory (0: none): another place is cleared, its effect fresh */
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
/* the master's knobs, three pages: EQ (low, mid, high; 64 flat), color (type, amount, tone, mix), out (drums, melodic,
 * comp, pump); then the out page's list (SEL): what the pump follows, its release, accent, and the level (the drive
 * into the limiter). Value 3 was the EQ's blend, gone: a project's byte there is ignored.
 * color (after the EQ, before the compressor; the character's warm types, dsp/efx.c's, on the whole mix):
 *   tape:    a wow (0.55 Hz, a 6 Hz flutter an eighth as deep) on the whole mix, ~1 .. 6 cents by the amount (times the
 *            mix, not mixed: no comb with the dry), then the character's tape: the saturation (gain 0.5 .. 8, made up)
 *            into a two-pole roll-off (tone 1.5 .. 18 kHz), mixed
 *   sampler: the mix low-passed, held at 26 .. 11 kHz and 14 .. 11 bits by the amount, its output filter (tone 2 ..
 *            20 kHz), mixed
 * PUMP (X0X's master PUMP): every hit of its source lowers the whole mix by up to 24 dB (rising in 12 ms) and
 * lets it swell back over the release; it is keyed by the sequencer, not by audio, so a muted track drives it
 * too. pump by: PB_KICK any kick of a kit (its first key, or a key on the kick model), PB_T1.. (a kit: its kicks) any note of
 * track 1..8, PB_METRO the metronome's click. release 0..127: 10 .. 1500 ms (60 = 107 ms, X0X's); accent
 * 0 / 1: a hit pumps by its velocity (0.5 .. 1 of the depth) or always fully. */
enum { MS_EQ_LO, MS_EQ_MID, MS_EQ_HI, MS_CLIP, MS_CTYPE, MS_CAMT, MS_CTONE, MS_CMIX,
       MS_PERC, MS_MEL, MS_COMP, MS_PUMP, MS_PUMP_BY, MS_PUMP_REL, MS_PUMP_ACC, MS_LEVEL, NMS };
#define MS_NKNOB 12                   /* the knobs' places (EQ 0..3, color 4..7, out 8..11); MS_PUMP_BY.. the out page's list */
enum { MC_OFF, MC_TAPE, MC_SAMPLER, NMC };   /* the master color's types */
extern const char *const MC_NAME[NMC];
enum { PB_KICK, PB_T1, PB_METRO = PB_T1 + 8, NPB };
#define MS_PUMP_REL_DEF 60
extern const char *const MS_NAME[NMS];
typedef struct master master_t;
#define MASTER_BYTES 1432             /* (fx.c checks it: 1,428 B, 2026-11-25) */
void master_init(master_t *m);
void master_clear(master_t *m);                             /* its states at rest, its settings kept */
void master_set(master_t *m, int p, int v);                 /* 0..127 (EQ 64 = flat; color type 0..NMC-1; pump by
                                                             * 0..NPB-1, accent 0..1; 3 clip 0..127) */
int master_max(int p);                                      /* a value's top: 127, NPB - 1 or 1 */
float master_pump_ms(int v);                                /* the pump's release, ms */
/* a hit on pump source src (PB_*), velocity 1..127: the pump drops if it follows src (the audio interrupt) */
void master_key(master_t *m, int src, int vel, int kit);   /* kit: a kit's hit (only the melodic group ducks) */
/* perc and mel: the two groups (stereo, in place), summed with the fx returns (added into mel) */
void master_process(master_t *m, float *perc_l, float *perc_r, float *mel_l, float *mel_r, float *out_l, float *out_r,
                    uint32_t n);
float master_gr_db(const master_t *m);                      /* the compressor's gain reduction (UI) */
float master_pump_db(const master_t *m);                    /* the pump's, now (UI: falls back slowly) */
#ifdef PLAT_HOST
extern int fx_host_stage;                                   /* the host's master taps (host.c LOOM_MST) */
#endif
