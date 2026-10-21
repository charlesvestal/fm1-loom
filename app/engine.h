/* SPDX-License-Identifier: GPL-3.0-only */
/* LOOM-8 audio engine: the sequencer, the voices (synth + drumline per track), the sends, the two FX
 * tracks, the master; the two cores and the overload guard. engine_render() runs in the
 * audio interrupt; everything else is called from the main loop and reaches the interrupt through
 * the command queue (one 32-bit word a command, single producer / consumer), so the engines' state
 * is only ever touched by one context.
 *
 * Parameters: track t's par[NTP] (dsp/synth.h) lives in the project (proj.trk[t].par) and is
 * mirrored into the engine with engine_set(); the interrupt reads the engine's copy. */
#pragma once
#include <stdint.h>
#include "seq/seq.h"
#include "dsp/synth.h"
#include "dsp/drumline.h"
#include "dsp/fx.h"

extern seq_t seq;                      /* the sequencer (its pattern pointer is the project's) */
/* the profiler's stages (samples.c SB_PROF, tools/perf_read.py), in plat_cycles() units: the sequencer and the queue,
 * the synth's voices on the first core and the second core's job (timed where it ran), the drum tracks, FX I, FX II
 * (with the returns), the master, the first core waiting for the second, a stretch's set-up (the buffers, the
 * performance state, the synth's prepare, the split), the halves' sum and meters, the output's conversion, the whole
 * render; PR_SUBS counts the stretches; PR_NOTE: the notes, hits and locks the sequencer sends (inside PR_SEQ). eng_prof_worst: the stages of the slowest block since eng_prof_reset was set */
enum { PR_SEQ, PR_SYNTH0, PR_SYNTH1, PR_DRUM, PR_FX, PR_MST, PR_WAIT, PR_FX2, PR_PREP, PR_MIX, PR_OUT, PR_ALL, PR_SUBS,
       PR_NOTE, PR_SIX, PR_STEP, PR_SUB, PR_COMP, PR_LOCKS, PR_NOTES, PR_FIRE, PR_DRAIN, PR_FXJ, ENG_PROF_N };
extern volatile uint32_t eng_prof[ENG_PROF_N], eng_prof_frames, eng_prof_worst[ENG_PROF_N];
extern volatile uint8_t eng_prof_reset;
/* the slowest block's voices: [i] its time in permille of the block | (engine | 32 started | 64 core 1 | 128 sounded) << 16;
 * [NVOICE] the first stretch's split k | the first core's units << 8 | the second's << 16 | stretches << 24 */
extern volatile uint32_t eng_vworst[NVOICE + 2];
/* the blocks around the slowest one since eng_prof_reset (the oldest first; it is the 8th, 4 follow): each its whole render, the synth on each
 * core, the sequencer, the drums, the notes (permille of the block's budget), its stretches, its voices */
#define TRACE_N 12
#define TRACE_K 8
extern uint16_t trace_worst[TRACE_N][TRACE_K];
extern volatile uint32_t eng_prof_drum[NTRK];          /* each drum track's time (in PR_DRUM) */

void engine_init(patterns_t *P, uint8_t (*par)[NTP]);    /* par: NALLTRK arrays (the project's) */
void engine_render(int32_t *out_lr, uint32_t n, uint32_t gain_q12);   /* ISR: n stereo frames, 24-bit in int32 */
int engine_live(int trk, int param);                     /* a lock's value now on a parameter, -1 none */
void engine_load(uint32_t pct);                          /* ISR, after a block: the overload guard's input */
void engine_set_core2(int (*run)(void (*)(uint32_t), uint32_t), int (*wait)(void));   /* 0, 0 = one core */

/* main loop -> interrupt (queued) */
void engine_set(int trk, int param, int val);            /* a knob: the engine's par copy + the voices */
void engine_set_all(int trk);                            /* every parameter of a track (load, preset) */
void engine_key(int trk, int note, int on, int vel);     /* a key through the sequencer's player and brain */
void engine_keys_off(int trk);
void engine_punch(int fx, int on, int target_trk);
void engine_brain_key(int note, int on);
void engine_master(int p, int val);
void engine_fx(int slot, int pick, const uint8_t knob[4], int hp, int lp);   /* pick: fx.h (effect, reverb mode) */
int engine_fx_has(int slot, int pick);                   /* the slot can do this effect (fx.h fx_has) */
void engine_panic(void);
void engine_midi_in(uint32_t pkt);                       /* a USB-MIDI packet (the main loop passes them) */
void engine_transport(int what);                         /* 0 stop, 1 start, 2 continue, 3 start from the top */
void engine_metronome(int mode);
void engine_tempo(int bpm_x10);                          /* the tempo in tenths of a BPM (400..3000) */
void engine_groove(int type);                            /* the project's groove type (pattern.h GR_*) */
enum { SCENE_NOW, SCENE_BAR, SCENE_END };
void engine_scene(int scene, int when);                  /* a scene: now (seq_set_scene), at the next bar line or at
                                                          * the end of track 1's pattern (seq_cue_scene) */

/* drum tracks (engine drumline): the Filter page's five values reach drumline_filter(). With TP_FTYPE
 * FT_DYNAMIC the knobs mean low cut = TP_CUT, compression = TP_RES, drive = TP_FENV, hi cut = TP_KTRK; any
 * other type turns the dynamic filter off. The warble throw (PX_WARBLE) on a drum track forces the dynamic
 * filter on with its low cut raised (PUNCH_HP), the other three as the knobs (or neutral when the track's
 * type is not dynamic), and sets perf_t.hp for the synth's sake. */
#define PUNCH_HP 90

/* state for the screen (written by the interrupt) */
extern volatile uint8_t eng_step[NSEQT];                 /* playheads */
extern volatile uint8_t eng_cc_level[NTRK];              /* MIDI CC 7 in, per track: the level to set (0xFF none; the
                                                          * main loop sets it, as the knob would) */
extern volatile uint16_t eng_peak[NALLTRK];              /* meters after the faders, Q14 (to +6 dB): the loudest
                                                          * since the UI took it (and set it to 0) */
extern volatile uint8_t eng_drum_hit[NDRUM];             /* the selected drum track's last hits (255 = full) */
extern volatile uint8_t eng_voices, eng_voices_max;      /* sounding, and the guard's cap */
extern volatile uint16_t eng_quiet;                      /* blocks in a row the output stayed under -60 dB (to 65535) */
extern volatile uint32_t eng_guard_count;
extern volatile uint32_t eng_punch_drum, eng_punch_mel;  /* the FX in effect */
extern volatile float eng_pump_db;                       /* the master's pump: its duck now, dB (the Master out page) */
extern volatile uint8_t eng_scene_now, eng_scene_cue;
#define SCOPE_N 256u
extern int16_t eng_scope[SCOPE_N];                       /* the last output samples, for the screen */
extern volatile uint32_t eng_scope_w;
int engine_core2_state(void);
int engine_slice_now(int trk, int *slice, int32_t *pos);  /* the slice engine's slice playing on a track (the picture) */                            /* 0 one core (never had), 1 two, 2 lost, 3 barred */
