/* SPDX-License-Identifier: GPL-3.0-only */
/* LOOM-8 effects: the two send-effect tracks (fx I, fx II: one of chorus, delay, lofi, phaser, reverb each,
 * four knobs of its own; the reverb in one of eleven modes: the plate, or one of the PlayStation's SPU
 * reverbs) and the master section (3-band EQ with blend, compressor, limiter, group
 * levels).
 *
 * All run in the audio interrupt. Buffers for the delay and the reverb live in the pool;
 * the caller passes them in (their sizes are the memory plan's). Float, fastmath.h. */
#pragma once
#include <stdint.h>

enum { FXT_CHORUS, FXT_DELAY, FXT_LOFI, FXT_PHASER, FXT_REVERB, NFXT };
extern const char *const FXT_NAME[NFXT];
/* the reverb's modes: the plate (9W9's combs), then the SPU reverbs of the PlayStation (psx-spx's register
 * sets, as in schwung-psxverb): room, studio small / medium / large, hall, half echo, space echo, chaos echo
 * (almost endless), delay (one echo); then lush, Dattorro's plate at 22.05 kHz (saved by number: a new mode at the end) */
enum { RV_PLATE, RV_ROOM, RV_STUDIO_S, RV_STUDIO_M, RV_STUDIO_L, RV_HALL, RV_HALF_ECHO, RV_SPACE_ECHO,
       RV_CHAOS_ECHO, RV_ECHO_ONE, RV_LUSH, NRV };
#define NSPU RV_ECHO_ONE                           /* the SPU modes: RV_ROOM .. RV_ECHO_ONE */
extern const char *const RV_NAME[NRV];         /* "plate", "room", "studio S", ... */
/* a track's TP_ENGINE byte names the effect and, for the reverb, its mode: the "pick", 0..NFXPICK-1:
 * chorus, delay, lofi, phaser, then the reverb's modes in order (FXT_REVERB + mode) */
#define NFXPICK (FXT_REVERB + NRV)
static inline int fx_pick_type(int pick) { return pick < FXT_REVERB ? (pick < 0 ? 0 : pick) : FXT_REVERB; }
static inline int fx_pick_mode(int pick) { return pick < FXT_REVERB ? 0 : pick - FXT_REVERB < NRV ? pick - FXT_REVERB : 0; }
const char *fx_pick_name(int pick);            /* "chorus" .. "phaser", "plate" .. "one echo" (the mode's name) */
const char *fx_knob_name(int pick, int k);     /* the four knobs' names (the plate's and the SPU modes' differ) */
/* the knobs and their defaults (stored 0..127), and what the defaults sound like:
 *   chorus      rate depth feedback width        50 86 20 127   0.33 Hz, +-5.5 ms, light feedback, the sides opposite
 *   delay       time fine repeats tone           72 64 56 64    1/8 ping-pong, ~4 repeats, repeats low-passed at 4 kHz
 *   lofi        rate bits smooth width           80 80 64 40    8.4 kHz, 11 bits, half smoothed, the right 3.4 ms late
 *   phaser      freq depth rate feedback         50 90 50 80    466 Hz, +-2.1 octaves, 0.27 Hz, feedback 0.57
 *   plate       size motion tone damp            100 40 44 72   combs x1.43 (feedback 0.88), a little wander, 7.4 kHz, damp 0.45
 *   SPU modes   size decay tone damp             127 64 64 0    the console's own: its delays (x4 of the registers), its
 *                                                               wall feedback, flat, its own reflection filter
 *   lush        size decay tone damp             64 80 56 48    Dattorro's lengths x1 (pre-delay 19 ms), decay 0.69,
 *                                                               a little darker, the tank's damping at 0.39
 * delay time: 1/32 1/32D 1/16 1/16D 1/8 1/8D 1/4 1/2 of the tempo, fine +-25 %; delay tone: the repeats' low-pass,
 * 800 Hz .. 19 kHz; reverb tone: a low-pass below the centre, a high-pass above it; plate damp: the combs' damping.
 * SPU size: the delays from x1 of the registers (schwung-psxverb's) to x4 (the console's: the registers count
 * 8-byte units); SPU decay: the wall reflection's feedback, 0.5x .. x1 (64) .. as far as it stays stable;
 * SPU damp: the reflections' low-pass (vIIR) closes. The FX tracks are plain send / return: an effect's output
 * is the wet sound only. */
extern const uint8_t FXT_DEFAULT[NFXT][4];      /* the reverb's row: the plate's */
extern const uint8_t RV_SPU_DEFAULT[4];         /* the SPU modes' */
extern const uint8_t RV_LUSH_DEFAULT[4];        /* lush's */

#define FX_DLY_LEN 22400              /* int16 samples a side: 0.5 s plus the line's 256-sample margin (the pool's budget) */
#define FX_RV_LEN 32768               /* int16 samples: the reverb's lines (the plate uses 11.4 k of them; an SPU mode at
                                       * x4 up to 32766: the chaos echo's and the delay's 1FFFh x 4) */
typedef struct fxslot fxslot_t;
#define FXSLOT_BYTES 5760             /* the chorus line (2048 int16: 4 KB, the lofi's spread reads it too) and the states */

void fx_init(fxslot_t *f, int16_t *dly_l, int16_t *dly_r, int16_t *rv);   /* a slot gets the lines it may use;
                                                                            * a slot without a reverb line (0) has no
                                                                            * reverb (fx I: the memory plan) */
void fx_set(fxslot_t *f, int pick, const uint8_t knob[4]);  /* the slot's effect (a pick: the type and the reverb's
                                                             * mode) and its four knobs (0..127) */
void fx_tempo(fxslot_t *f, float bpm);
void fx_filter(fxslot_t *f, int hp, int lp);                /* the slot's filter page: high-pass, low-pass 0..127 */
/* process n frames of the send (mono in) into out L/R (added) */
void fx_process(fxslot_t *f, const float *in, float *out_l, float *out_r, uint32_t n);
/* the same for a send that arrives lag samples late (0..256: the engine runs the effects a block behind, on the
 * second core): the chorus, the delay and the plate put it back in time in their lines (their output as without the
 * lag, but for the lines' rounding); the SPU modes take it as that much more pre-delay. fx_lags: the effect is one of
 * those (the lofi and the phaser have no line to hide it in: the engine runs them in time) */
void fx_process_lag(fxslot_t *f, const float *in, float *out_l, float *out_r, uint32_t n, int lag);
int fx_lags(const fxslot_t *f);
int fx_has(const fxslot_t *f, int pick);                    /* the slot can do this effect (reverb needs its line) */
int fx_awake(const fxslot_t *f);                            /* the slot works this block: its input heard, or its line
                                                            * still ringing (a delay or reverb sleeps on silence; the
                                                            * others always work) */

/* ---- master ---- */
/* the master's knobs, two pages: EQ (low, mid, high; 64 flat), out (drums, melodic, comp, pump); then the out page's
 * list (SEL): what the pump follows, its release, accent, and the level (the drive into the limiter). Value 3 was the
 * EQ's blend, values 4..7 the master tape's (TapeScam-lite: drive, color, wobble, noise), gone: a project's bytes there are ignored.
 * PUMP (X0X's master PUMP): every hit of its source lowers the whole mix by up to 24 dB (rising in 12 ms) and
 * lets it swell back over the release; it is keyed by the sequencer, not by audio, so a muted track drives it
 * too. pump by: PB_KICK any kick of a kit (its first key, or a key on the kick model), PB_T1.. any note of
 * track 1..8, PB_METRO the metronome's click. release 0..127: 10 .. 1500 ms (60 = 107 ms, X0X's); accent
 * 0 / 1: a hit pumps by its velocity (0.5 .. 1 of the depth) or always fully. */
enum { MS_EQ_LO, MS_EQ_MID, MS_EQ_HI, MS_RSV3, MS_RSV4, MS_RSV5, MS_RSV6, MS_RSV7,
       MS_PERC, MS_MEL, MS_COMP, MS_PUMP, MS_PUMP_BY, MS_PUMP_REL, MS_PUMP_ACC, MS_LEVEL, NMS };
#define MS_NKNOB 12                   /* the knobs' places (EQ 0..3, out 8..11); MS_PUMP_BY.. are the out page's list */
enum { PB_KICK, PB_T1, PB_METRO = PB_T1 + 8, NPB };
#define MS_PUMP_REL_DEF 60
extern const char *const MS_NAME[NMS];
typedef struct master master_t;
#define MASTER_BYTES 1280
void master_init(master_t *m);
void master_set(master_t *m, int p, int v);                 /* 0..127 (EQ 64 = flat; pump by 0..NPB-1, accent
                                                             * 0..1; 3..7 ignored) */
int master_max(int p);                                      /* a value's top: 127, NPB - 1 or 1 */
float master_pump_ms(int v);                                /* the pump's release, ms */
/* a hit on pump source src (PB_*), velocity 1..127: the pump drops if it follows src (the audio interrupt) */
void master_key(master_t *m, int src, int vel);
/* perc and mel: the two groups (stereo, in place), summed with the fx returns (added into mel) */
void master_process(master_t *m, float *perc_l, float *perc_r, float *mel_l, float *mel_r, float *out_l, float *out_r,
                    uint32_t n);
float master_gr_db(const master_t *m);                      /* the compressor's gain reduction (UI) */
float master_pump_db(const master_t *m);                    /* the pump's, now (UI: falls back slowly) */
#ifdef PLAT_HOST
extern int fx_host_stage;                                   /* the host's master taps (host.c LOOM_MST) */
#endif
