/* SPDX-License-Identifier: GPL-3.0-only */
/* LOOM-8 sequencer: runs in the audio interrupt, sample accurate (X0X's shape):
 *
 *     while (n) {
 *         k = seq_until_event(&seq, n);        // samples until the next due event (>= 1)
 *         render k samples;
 *         seq_advance(&seq, k, &sink);         // moves time, fires what is due
 *         n -= k;
 *     }
 *
 * Ten sequenced tracks (1-8, brain, punch), each on its own pattern (the scene's), with its
 * own step counter, length and scale. A cued scene starts at the next bar line (16 sixteenths of the
 * transport) or, cued to the end, when track 1's pattern wraps (or every 16 steps if it is shorter): with the
 * tracks phasing there is no common end, so track 1's pattern is the scene's length. The song counts bars. Time is counted in
 * ticks (PPS a sixteenth) at the tempo, swung and grooved per track.
 *
 * Step components run here: pulse and hold stall the counter, multiply ratchets, velocity fixes,
 * ramps and random move the note along the brain's scale pass by pass, portamento glides, bend writes
 * the track's perf pitch over the step, jump moves the counter, skip (and the condition) decide
 * whether the step fires. Parameter locks: before a step fires, each lane of the pattern sets its
 * value (ramped toward the next lock when the pattern's shape says so) through the sink.
 *
 * Players: the keys reach the engine through seq_key(): arp (clocked here), maestro (the recorded
 * chord, transposed, rolled), hold; the brain shifts routed tracks' notes diatonically (its degree
 * from its keys or its steps; its link track plays its keys in parallel). Punch-in FX are a bit mask
 * per group, from held keys (seq_punch_key, group-wide or aimed at one track) or the punch track's
 * steps; the sequencer runs the ones that change the sequence (mute, stutter, repeat, echo, the fills,
 * octave, chop, ring) and hands the engine the masks for the rest (warble, pan).
 *
 * Clock: internal (BPM) or MIDI clock in (24 ppqn, seq_ext_clock); clock and transport out through
 * the sink. Everything the main loop changes is a flag or a byte, picked up at the next block. */
#pragma once
#include <stdint.h>
#include "pattern.h"
#include "../dsp/synth.h"
#define NTP_ALL NTP                   /* aux tracks use the same parameter array as the instrument tracks */

#define SEQ_FS 44100u
/* throws (punch-in FX): twelve, the same in both octaves. White keys 1..7 of an octave: mute, stutter (a
 * two-sixteenth loop), repeat (a four-sixteenth loop), echo (a fading loop), build, floor, jumble (fills); its
 * black keys 1..5: warble (a vibrato; on drums a high-pass too), pan (an auto-pan), octave (drums up, melodic
 * down), chop (short notes), ring (long notes and tails). The bit order is stored in throw steps: keep it. Bits 0..11 = the lower octave (percussive tracks: the kit and
 * slice tracks), 16..27 = the upper
 * (melodic tracks). On the FM-1 the lower octave is keys F3..E4, the upper F4..E5 (F5, F#5, G5 idle). */
#define NPUNCH_FX 32
#define NPUNCH 12
enum { PX_MUTE, PX_STUTTER, PX_REPEAT, PX_ECHO, PX_BUILD, PX_FLOOR, PX_JUMBLE, PX_WARBLE, PX_PAN, PX_OCTAVE,
       PX_CHOP, PX_RING };
#define PX_MEL 16                     /* + PX_* = the melodic octave's bit */
#define PX_VALID(fx) ((fx) >= 0 && (fx) < NPUNCH_FX && ((fx) & 15) < NPUNCH)   /* a real bit: 0..11, 16..27 */
extern const char *const PUNCH_NAME[NPUNCH];          /* "mute", "stutter", ... */
/* a key number 0..26 (F3 = 0) to its punch bit, -1 for the idle keys; and back */
int punch_of_key(int key);
int punch_key_of(int fx);
/* What each effect does here (the engine does warble and pan from the masks):
 *   mute      the group's (or the track's) steps and keys do not sound (mute wins over everything)
 *   stutter   the step playing at the punch-in and the next ones over two sixteenths repeat while held
 *   repeat    the same over a beat (four sixteenths)
 *   echo      stutter's span, its velocity falling to 70 % every pass
 *   build     drums: snares in eighths over the first half of the bar, sixteenths over the second;
 *             synths: the pattern's own notes (every distinct note it holds, sorted) climbing in sixteenths
 *   floor     drums: a kick on every beat and hats in sixteenths; synths: the notes alternating low / high
 *   jumble    drums: a random one of sounds 1..3 on three sixteenths in four; synths: a random pattern note
 *             on three sixteenths in four. The fills replace the track's own steps while held, and
 *             play on the bar's sixteenths whatever the track's scale (an empty synth pattern is silent).
 *             A slice track plays the drums' fills on its own slices: seq.c fill_slices
 *   octave    drums an octave up (pitch_semi 12 on the drum call; a slice track: the engine's pitch), synths an
 *             octave down (the note)
 *   chop      drums: short_gate on the drum call; synths: the gate is 25 % of the step
 *   ring      drums: long_tail on the drum call; synths: the gate is 200 % of the step
 * The PUNCH track's steps hold an effect as note[0] = 1 + its bit: 1..12 the percussive octave, 17..28 the
 * melodic one (the UI writes the same); a step's length is how long it holds. Live recording writes held
 * punch keys there (seq_punch_key with rec on while playing), per-track ones included. */

/* brain scales (the key's), 12-bit masks in BRAIN_SCALE[] */
enum { BS_MAJOR, BS_MINOR, BS_DORIAN, BS_MIXO, BS_LYDIAN, BS_PHRYGIAN, BS_HARM_MINOR, BS_PENTA, BS_PENTA_MIN,
       BS_CHROMATIC, NBS };
extern const char *const BS_NAME[NBS];
extern const uint16_t BRAIN_SCALE[NBS];
int brain_shift(int note, int key, int scale, int degree, int mode);   /* the note moved `degree` scale steps
                                                                        * (mode: TP_BRAIN 1 diatonic, 2 transpose) */
int scale_step(int note, int key, int scale, int steps);               /* a note `steps` degrees along the scale */
int scale_snap(int note, int key, int scale);                          /* the nearest scale note (ties go down) */

typedef struct {
    void (*note_on)(void *ctx, int trk, int note, int vel, int glide_ms, uint32_t tag);
    void (*note_off)(void *ctx, int trk, uint32_t tag);
    void (*drum)(void *ctx, int trk, int sound, int vel, int pitch_semi, int short_gate, int long_tail);
    void (*lock)(void *ctx, int trk, int param, int val);          /* LOCK_NONE = back to the knob */
    void (*bend)(void *ctx, int trk, int cents);                   /* the bend component, each chunk */
    void (*punch)(void *ctx, uint32_t drum_mask, uint32_t mel_mask);   /* the live FX state changed */
    void (*midi)(void *ctx, uint8_t status, uint8_t d1, uint8_t d2);
    void (*step)(void *ctx, int trk, int step);                    /* UI playheads */
    void (*scene)(void *ctx, int scene);                           /* a scene started */
    void (*metro)(void *ctx, int accent);                          /* a metronome click (accent: the bar) */
    void (*pan)(void *ctx, int trk, int pan);                      /* the arp's stereo: the track's pan offset
                                                                    * -64..63 for its next notes (0 = centre) */
    void (*drum_off)(void *ctx, int trk, int sound);               /* a drum's key / step ends (gated and loop play
                                                                    * modes stop; the engine's drumline_release) */
    void (*ghost)(void *ctx, int trk, int sound, int vel);         /* a muted track's step that would have played (the
                                                                    * scene's mute or a mute throw): each drum sound of it
                                                                    * (sound 0..), or once for a melodic track (sound -1),
                                                                    * at the step's velocity. Nothing sounds: it keys the
                                                                    * master's pump (a silent kick can drive it) */
    void *ctx;
} seq_sink_t;

#define SEQ_PENDING 16                /* note-offs a track may have waiting */
typedef struct {
    uint32_t off_at;                  /* samples left, Q8 (counted down; due under SEQ_DUE: seq.c) */
    uint32_t tag;
    uint8_t used;
    uint8_t kind;                     /* OFF_NOTE: sink note_off; OFF_PUNCH: clear a punch_seq bit;
                                       * OFF_DRUM: sink drum_off (note = the sound + 1) */
    uint8_t note;                     /* the MIDI note echoed, 0 none */
} seq_off_t;
enum { OFF_NOTE, OFF_PUNCH, OFF_DRUM };

typedef struct {                      /* one track's runtime (interrupt only) */
    uint8_t pos;                      /* the step playing */
    uint8_t started;
    uint8_t grid_h, grid_len;         /* the scale (half-sixteenths a step) and length it last stepped with */
    uint32_t origin_h8;               /* when its step 1 began: half-sixteenths since start, Q8 (the phase) */
    uint8_t stall;                    /* pulse / hold: steps it still covers (the playhead goes on, in time) */
    uint8_t ratchet, ratchets;        /* multiply: hits done, hits wanted */
    uint16_t pass;                    /* passes of the pattern since start (conditions, skips, ramps) */
    uint32_t to_next;                 /* samples to the next step, Q8 */
    uint32_t step_q8;                 /* the current step's length (ratchets, bends) */
    uint32_t rng;
    int16_t ramp_deg;                 /* ramp / random: the degree offset this pass */
    uint8_t bend_shape, bend_on;
    uint32_t bend_t, bend_len;        /* samples into the bend, its length */
    seq_off_t off[SEQ_PENDING];
    uint16_t off_mask;                /* bit i: off[i].used (the scans skip a track with none) */
    uint8_t last_note[SN];            /* the notes sounding (step mode: one step at a time) */
    uint8_t act[NSC];                 /* the step's components in force: act[SC_x] = value + 1, 0 = absent */
    uint8_t loop_on;                  /* a punch loop runs: steps loop_from .. loop_from + loop_span - 1 */
    uint8_t loop_from, loop_span;
    uint16_t echo_gain;               /* the loop's velocity, Q8 (echo: x 0.7 a pass) */
    uint8_t stall_kind;               /* SC_PULSE or SC_HOLD while stall > 0 */
    uint8_t stall_p;                  /* the trick's step (a pulse plays it again) */
    uint8_t stall_vel;                /* a kit: the hit's velocity, for the pulse's repeats */
    uint16_t stall_mask;              /* a kit: the sounds covered (the trick's step's; the others play on) */
    uint8_t jump_to;                  /* the jump component's next step, 0xFF none */
    uint8_t nlast;                    /* notes in last_note[] */
    uint8_t last_step;                /* the step last_note[] came from */
    uint8_t last_vel, last_glide;     /* for retriggers (ratchets) */
    uint8_t ext_ticks;                /* external clock: ticks into this step */
    uint8_t fill_on;                  /* a punch fill runs: the pattern's steps are silent */
    uint8_t fill_i;                   /* the fill's note counter */
    uint8_t fill_kind;                /* 1 build, 2 floor, 3 jumble: the counter restarts when it changes */
    uint8_t nfill;                    /* synth fills: the pattern's distinct notes, sorted */
    uint8_t fill_notes[16];
    uint8_t skip_note;                /* the note live recording just put on step 1: the step does not sound it again */
    uint8_t bend_last_set;
    int16_t bend_last;                /* cents last sent */
    int16_t bend_amp;                 /* random 2: the amplitude rolled */
    uint32_t sub_to_next;             /* ratchets: samples to the next hit, Q8 (NEVER none) */
    uint32_t age;                     /* samples since the step fired, Q8 (live record quantise) */
    int32_t off_q8;                   /* the current step's timing offset (groove, nudge), Q8 */
    step_t wv_st;                     /* the woven step playing (weave_step), while the track weaves */
    uint8_t wv_on;                    /* 1: the step playing is wv_st (its ratchets repeat it) */
} seq_trk_t;

typedef struct {
    uint8_t gen, applied;             /* lane runtime: the value last applied (LOCK_NONE = none) */
    uint8_t from, to;                 /* the ramp across this step (shape) */
    uint8_t ramp;                     /* ramping toward `to` */
    uint8_t trk;                      /* whose value is applied: a cleared lane is released through them */
    uint16_t param;
    uint32_t age, len, hold;          /* samples into the gap, its length, the part held flat before the ramp */
} lane_rt_t;

typedef struct {                      /* a player (per track; the arp's clock runs here) */
    uint8_t held[16], nheld;          /* the keys down, in order */
    uint8_t latched;                  /* hold: notes stay */
    uint8_t arp_oct, arp_dir;
    uint32_t arp_i;                   /* the arp's position in its cycle */
    uint8_t arp_note;                 /* sounding arp note, 0 none */
    uint8_t arp_vel;                  /* the last key's velocity: the arp's */
    uint8_t arp_side;                 /* stereo: the side of the next note */
    int8_t pan_sent;                  /* the pan last sent through the sink */
    uint32_t arp_to_next, arp_to_off; /* Q8 */
    uint8_t chord[SN * 2], nchord;    /* maestro: the recorded chord (up to 8 notes) */
    uint8_t maestro_root;             /* the key playing it, 0 none */
    uint8_t roll_i; uint32_t roll_to_next;
    uint8_t sounding[16], nsounding;  /* notes the player holds on the engine (for their offs) */
    uint32_t phys[4];                 /* the keys physically down (bit note) */
    uint8_t nphys;
    uint8_t roll[SN * 2], nroll;      /* maestro: the strum in its order */
    uint8_t roll_vel;
    uint8_t arp_wait;                 /* the first arp note waits for the grid */
    uint16_t drums_down;              /* a drum track: bit sound = its key is down (released through drum_off) */
    uint8_t type_seen, latch_seen;    /* TP_PLAYER and TP_P4 last seen: a change lets the old player's notes go */
    uint8_t sq[SN], nsq;              /* arp: the step's notes, held with the keys for the step's length */
    uint32_t sq_to_off;               /* Q8, NEVER none */
    uint8_t sroll[SN * 2], nsroll;    /* strum: a step's chord in its order, rolled */
    uint8_t sroll_i, sroll_vel;
    uint32_t sroll_to_next, sroll_left;   /* Q8: to the next note; to the chord's end (each note's off) */
    uint32_t sroll_tag;               /* TAG_STEP | the step's: a note's tag adds its index */
} player_t;

typedef struct {
    patterns_t *P;                    /* the project's patterns, lanes, scenes, song */
    uint8_t (*par)[NTP_ALL];          /* each track's parameters (NALLTRK of them): TP_PLAYER.., TP_BRAIN, mutes */
    volatile uint8_t playing;
    volatile uint8_t scene;           /* the scene playing */
    volatile uint8_t cue;             /* the scene cued, 0xFF none */
    volatile uint8_t cue_end;         /* it waits for track 1's pattern to end, not the next bar line */
    volatile uint8_t song_on;         /* the song decides the scenes */
    volatile uint8_t song_pos, song_rep;
    volatile uint8_t ppat[NSEQT];     /* each track's pattern (the scene's, or cued per track) */
    volatile uint16_t mute;           /* bit t */
    volatile uint8_t ext, send_clock, send_notes;
    volatile uint8_t metronome;       /* 0 off, 1 while recording, 2 on */
    volatile uint8_t rec;             /* live recording: the keys write steps */
    volatile uint8_t rec_wait;        /* seq_rec(2): the transport waits for the first key, which lands on step 1 */
    volatile uint8_t count_in;        /* bars of count-in left before rec */
    volatile uint8_t groove_type;     /* the project's groove GR_* (the UI / engine writes); each pattern's
                                       * signed `groove` is its amount */
    float bpm, ext_bpm;
    /* the brain */
    volatile uint8_t brain_key, brain_scale, brain_deg;
    volatile uint8_t brain_src;       /* 0 off, 1..8 = track t + 1 FOLLOW: its notes set the chords (it follows nothing) */
    volatile uint8_t brain_link;      /* 0 off, 1..8 = instrument track t + 1 played from the brain's keys too */
    volatile uint16_t brain_route;    /* bit t = track t follows */
    uint32_t brain_phys[4];           /* the brain's keys down (bit note): the lowest is the chord's root */
    /* punch-in */
    uint32_t punch_keys;              /* bits NPUNCH_FX: held keys (set from the key ring, in the interrupt) */
    uint8_t punch_tgt[NPUNCH_FX];     /* each held key's target: 0xFF = its octave's group, else one track */
    uint32_t punch_seq;               /* from the punch track's steps */
    /* runtime */
    seq_trk_t t[NSEQT];
    lane_rt_t lrt[NLANE];
    player_t player[NTRK];
    uint32_t bar_to_next;             /* Q8 */
    int32_t clk_to_next;              /* Q8; the MIDI clock's goes below 0 when it waits for a stretch to end */
    uint32_t ext_since, ext_period;
    volatile uint32_t ext_pending;
    volatile uint8_t req_start, req_stop, req_cont, req_from_start;
    uint32_t rng;
    uint32_t step_tick;               /* sixteenths since start (the metronome, the arp grid) */
    uint8_t metro_beat;
    /* added runtime */
    volatile uint8_t pcue[NSEQT];     /* a pattern cued per track, 0xFF none */
    volatile uint8_t req_scene;       /* seq_set_scene: apply now, 0xFF none */
    volatile uint32_t kq[32];         /* the keys from the main loop, played in the interrupt */
    volatile uint8_t kq_w, kq_r;
    uint32_t six_to_next;             /* samples to the next sixteenth, Q8 */
    uint32_t bar_six;                 /* sixteenths since the bar count started */
    uint32_t bar_len;                 /* the bar in sixteenths (track 1's span, at least 16) */
    uint8_t bar16;                    /* the sixteenth of the bar, 0..15 */
    uint8_t countin_six;
    uint8_t bar_fresh;                /* the bar count just restarted: the next sixteenth is not a bar's end */
    uint8_t ext_six;                  /* external clock: ticks into the sixteenth */
    uint8_t chunk_cap;                /* a lock ramp runs: advance in chunks */
    uint8_t brain_seen[4];            /* the BRAIN track's E1..E4 last read */
    uint32_t punch_sent_d, punch_sent_m;
    uint8_t rec_note[8], rec_trk[8], rec_step[8];   /* live record: keys down and the steps they took */
    volatile uint8_t rec_replace;     /* live record replaces: the steps the playhead passes on rec_target are
                                       * cleared, unless written in this pass (the UI's MODE) */
    volatile uint8_t rec_target;      /* the track the keys record into (the UI's) */
    uint32_t rec_written[NSEQT][2];   /* steps written since the playhead last passed them */
    uint32_t rec_age[8];
    /* the weave (pattern.h weave_step): a track weaving plays its pattern crossed with wv_from's pattern wv_pat by
     * wv_rule; it weaves while its bit is in wv_live (the WEAVE page's keys, held or latched) or wv_rec (the WEAVE
     * track's steps playing). wv_from 0 = no weave set up, else the track + 1. Set by the UI, read here */
    volatile uint8_t wv_from[NSEQT], wv_pat[NSEQT], wv_rule[NSEQT];
    volatile uint16_t wv_live, wv_rec;
    volatile uint16_t wv_always;      /* the tracks whose weave plays always (PLAYS always: set up = on) */
    uint8_t wv_ppat;                  /* the WEAVE track's pattern playing (the scene's) */
    uint8_t wv_left[NSEQT];           /* sixteenths each track's recorded weave still lasts */
    uint8_t wv_rec_at[NSEQT];         /* recording: the step a held weave started on (0xFF none) */
} seq_t;

void seq_init(seq_t *s, patterns_t *P, uint8_t (*par)[NTP_ALL]);
uint32_t seq_until_event(seq_t *s, uint32_t n);
void seq_advance(seq_t *s, uint32_t k, const seq_sink_t *o);

/* main loop / MIDI side (flag writes, picked up at the next block) */
void seq_start(seq_t *s, int from_start);
void seq_stop(seq_t *s);
void seq_continue(seq_t *s);
void seq_ext_clock(seq_t *s);                  /* one 24-ppqn tick */
void seq_cue_scene(seq_t *s, int scene, int at_end);   /* at the next bar line, or at the end of track 1's
                                                          * pattern (now, if stopped) */
int seq_cue_left(const seq_t *s);              /* sixteenths before the cued scene's (0: the next), -1 none */
void seq_set_scene(seq_t *s, int scene);       /* now: the scene's patterns, mutes, brain */
void seq_cue_pattern(seq_t *s, int trk, int pat);   /* one track's pattern at its wrap */
float seq_tempo(const seq_t *s);
uint32_t seq_step_q8(const seq_t *s, int trk);     /* a step of track trk in samples Q8 (its scale, no groove) */
int seq_bar_step(const seq_t *s);                   /* 0..15: the sixteenth of the bar (UI, metronome) */

/* the keys (main loop): through the track's player and the brain, to the engine; tag = the key */
void seq_key(seq_t *s, int trk, int note, int on, int vel);
void seq_key_all_off(seq_t *s, int trk);
void seq_maestro_record(seq_t *s, int trk, const uint8_t *notes, int n);   /* the maestro's chord (up to 8) */
/* a key on the BRAIN track: the lowest key held is the chord's root, its pitch class against the key's tonic
 * (in the octave of middle C) sets the degree; the link track plays the key as pressed, in parallel */
void seq_brain_key(seq_t *s, int note, int on);
/* a punch-in key: fx = the bit (0..11 the percussive octave, 16..27 the melodic one); target_trk -1 = every
 * track of that octave's group, t = that track only (the bit is moved to the track's own octave). With rec on
 * while playing the key is recorded into the PUNCH track's pattern. The latch (PUNCH track E1) is the UI's:
 * it sends on / off as the latched state changes */
void seq_punch_key(seq_t *s, int fx, int on, int target_trk);
int punch_group_of(const seq_t *s, int trk);        /* 0 percussive (a drumline or slice track), 1 melodic */
uint32_t seq_punch_mask(const seq_t *s, int trk);   /* the 12 PX_* bits in force on track trk now (held keys aimed at
                                                     * it or its group, and the sequenced ones); the engine's per-track
                                                     * view, finer than the two masks the punch sink gets */

/* live / step recording (main loop) */
/* on: 0 off; 1 the keys write steps (quantised) while playing (momentary: on while REC is held); 2 the same,
 * and if the transport is started afterwards it waits for the first key, which lands on step 1 (the OP-XY's
 * live recording); count_in (bars) stays as an option on top */
void seq_rec(seq_t *s, int on);
void seq_rec_knob(seq_t *s, int trk, int param, int val);   /* REC held + a knob: a lock on the step playing */
int seq_rec_pos(const seq_t *s, int trk);           /* the step live keys land on now */
int seq_step_rec(seq_t *s, int trk, int step, int note, int vel);   /* step record: the note into the step of the
                                                     * track's pattern (overdubs; the UI moves its cursor); 1 = written */
int seq_step_add_notes(seq_t *s, int trk, int step, const uint8_t *notes, int n, int vel);   /* a step held + keys:
                                                     * each note toggled on the step; returns the notes it holds after */
/* step helpers (main loop) */
void step_set_notes(step_t *st, const uint8_t *notes, int n, int vel);
void step_toggle_note(step_t *st, int note);
int step_empty(const step_t *st);
