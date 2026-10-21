/* SPDX-License-Identifier: GPL-3.0-only */
/* LOOM-8 data model: tracks, patterns, steps, lanes (parameter locks), scenes and the song.
 *
 * Plain data, no pointers, so a pattern copies with an assignment and the project stores the bytes
 * (X0X's rule). The UI edits these in place from the main loop while the sequencer reads them in the
 * audio interrupt: every field the sequencer reads is a single byte or an aligned 32-bit word, so a
 * read never sees half a write. */
#pragma once
#include <stdint.h>

/* The code keeps its first names; the screen and the manual say: AUX_BRAIN the conductor (COND), AUX_PUNCH the
 * throws (THROW), components (SC_*) tricks, the drumline engine the kit, the maestro player strum. */
#define NTRK 8                        /* instrument tracks 1-8 */
enum { AUX_BRAIN = NTRK, AUX_PUNCH, AUX_FX1, AUX_FX2, AUX_WEAVE, NALLTRK };   /* (WEAVE: its own steps, wvpat_t) */
#define NSEQT (AUX_PUNCH + 1)         /* sequenced tracks: 1-8, brain, punch */
#define NPATT 8                       /* patterns per track */
#define NSTEP 64                      /* 4 bars of 16 */
#define NBAR 4
#define SN 4                          /* notes a step may hold */
#define NSCENE 32                     /* two pages of the 16 white keys (OCT on Arrange and Song) */
#define NSONG 64                      /* scene slots in the song */
#define SONG_BARS 64                  /* the most bars a song slot plays */
#define NLANE 96                      /* parameter lock lanes (the whole project) */
#define NDRUM 11                      /* drumline keys: the black keys (a model instance each) */
#define PPS 24                        /* ticks a step (a sixteenth at scale 1): nudge and lengths count in these */

/* step components: the OP-XY's 14, in the cheat sheet's order (the white keys 1..14 with HOME and a step held);
 * 0 = none. Values 0..8 = the black keys 1-9, 9 = black key 10 (random, where the OP-XY prints rnd). A step
 * carries up to two (comp / comp2). Defaults when a component is added (the notebook p. 139): SC_DEFAULT[]. */
enum { SC_NONE, SC_PULSE, SC_HOLD, SC_MULTIPLY, SC_VELOCITY, SC_RAMP_UP, SC_RAMP_DOWN, SC_RANDOM,
       SC_PORTAMENTO, SC_BEND, SC_TONALITY, SC_JUMP, SC_SKIP_LOCK, SC_SKIP_COMP, SC_SKIP, NSC };
#define SC_RANDOM_VAL 9
#define JUMP_LAST 21                       /* SC_JUMP's values run 0..21 (see below) */
extern const uint8_t SC_DEFAULT[NSC];      /* pulse 3 (= 4), hold 3, multiply 1, velocity 4, ramps 3, random 3,
                                            * portamento 3, bend 0, tonality 3, jump 3, skips 1 (0-based values) */
/* SC_PULSE / SC_HOLD: 1..9 repeats (the total count includes the original trigger), random
 * SC_MULTIPLY: 1..9 ratchets within the step, random
 * SC_VELOCITY: 4 8 16 32 64 100 112 127 0, random
 * SC_RAMP_* / SC_RANDOM: 2..6 pitch iterations across one octave (0..4), across three octaves (5..8), random:
 *   the notes are spread evenly over the span in the brain's scale and the ramp resets after each cycle
 * SC_PORTAMENTO: 10..80 % (0..7), off (8), random
 * SC_BEND: down-up, up-down, bump-down, bump-up, spring-out, spring-in, fade-down, fade-up, random 1, random 2
 * SC_TONALITY: ignore progression, transpose only, octave up, fifth up, third up (by the scale), chromatic up,
 *   chromatic down, quantize 33 %, 66 %, 100 %
 * SC_JUMP: to 1, 5, 9, 13 (0..3), forward (skips the next step), back, forward-or-back, stay, align, random (4..9),
 *   then to 17, 21 .. 61 (10..21: a longer pattern's beats; the knob turns through the beats the pattern has first)
 * SC_SKIP_LOCK / SC_SKIP_COMP / SC_SKIP: a cycle of 1..9 passes (random); which passes play is the cycle's
 *   mode in the value's upper bits: SK_MODE(v) 0 = play on the last pass of the cycle, 1 = skip the last pass,
 *   2 = play only on the first; SK_N(v) = the cycle length value 0..9 */
#define SK_N(v) ((v) & 15u)
#define SK_MODE(v) (((v) >> 4) & 3u)
#define SK_VAL(n, mode) ((uint8_t)(((mode) << 4) | (n)))

/* a step's condition: 0 always; 1..99 that chance in %; COND_AB + k = the A:B cycle k (X0X's order
 * 1:2 2:2 1:3 2:3 3:3 1:4 ... 8:8: on the A-th of every B passes of the pattern) */
#define COND_AB 100
#define COND_NAB 35

typedef struct {
    uint8_t note[SN];                 /* MIDI notes, 0 = none (a kit track: its sounds as bits, seq.h kit_*) */
    uint8_t vel;                      /* 1..127 */
    uint8_t len;                      /* 0 = the pattern's note length; 1..64 quarter steps; 65..112 = 17..64 whole
                                       * steps (a note as long as the pattern: step_len_q) */
    int8_t nudge;                     /* ticks, -12..12 */
    uint8_t comp;                     /* SC_* */
    uint8_t cval;                     /* the component's value 0..9 (skips: SK_VAL) */
    uint8_t cond;                     /* see COND_* */
    uint8_t comp2, cval2;             /* a second component on the same step (SC_NONE = none) */
} step_t;                             /* 12 bytes */
/* a kit track's step: any of its NDRUM sounds (1..11), a bit each (bit k: sound k + 1) in note[0] (sounds 1-8) and
 * note[1] (9-11); note[2] and note[3] stay 0, so step_empty holds */
#define KIT_ALL ((1u << NDRUM) - 1u)
#define LEN_MAX (NSTEP + 48)          /* a step's longest len: 64 steps */
static inline int step_len_q(int len) { return len <= 64 ? len : (len > LEN_MAX ? NSTEP : len - 48) * 4; }   /* quarter steps */
static inline int step_len_code(int q)   /* quarter steps (1..NSTEP * 4) to a len: past 16 steps, whole steps up */
{
    return q <= 64 ? (q < 1 ? 1 : q) : (q + 3) / 4 + 48 > LEN_MAX ? LEN_MAX : (q + 3) / 4 + 48;
}
static inline uint32_t kit_mask(const step_t *st) { return ((uint32_t)st->note[0] | (uint32_t)st->note[1] << 8) & KIT_ALL; }
static inline int kit_has(const step_t *st, int sound) { return sound >= 1 && sound <= NDRUM && (kit_mask(st) >> (sound - 1) & 1u); }
void kit_set(step_t *st, uint32_t mask);
void kit_toggle(step_t *st, int sound);           /* on or off; a fresh step's velocity 100 */
int kit_add(step_t *st, int sound);               /* 1: added (0: there already, or not a sound) */
int kit_first(const step_t *st);                  /* the lowest sound on the step, 0 none */

enum { TS_HALF, TS_1, TS_2, TS_3, TS_4, TS_5, TS_6, TS_8, TS_16, NTS };   /* track scale: sixteenths a step */
/* groove types (the project's): swing, lilt, weave, stretch, drunk, jitter, accent, lazy, push pull, sway, swell
 * (the index is saved with the project: append, never reorder); a track's amount -99..99 scales its timing and
 * velocity tables (negative = early / narrow), the random ones roll per hit */
enum { GR_SWING, GR_LILT, GR_WEAVE, GR_STRETCH, GR_DRUNK, GR_JITTER, GR_ACCENT, GR_LAZY, GR_PUSHPULL,
       GR_SWAY, GR_SWELL, NGROOVE };
extern const char *const GR_NAME[NGROOVE];   /* "swing", "lilt", .. */

typedef struct {
    step_t st[NSTEP];
    uint8_t len;                      /* steps 1..64 */
    uint8_t scale;                    /* TS_* */
    int8_t groove;                    /* this track's groove amount -99..99 (the type is the project's) */
    uint8_t quant;                    /* live record quantise 0..100 */
    uint8_t nlen;                     /* note length 0..100 % of a step (50 = half a step, the default) */
    uint8_t shape;                    /* lock smoothing 0..100 */
    uint8_t lstart;                   /* the loop: the first step it plays, 0-based (0 = the first) */
    uint8_t lend;                     /* and where it ends: the last step + 1 (0 = the pattern's end) */
} tpat_t;                             /* 776 bytes */
/* the loop the pattern plays, cropped to its length: steps from .. to - 1 (a pattern never plays outside it) */
static inline void pat_loop(const tpat_t *p, int *from, int *to)
{
    int len = p->len < 1 ? 1 : p->len > NSTEP ? NSTEP : p->len, f = p->lstart < len ? p->lstart : len - 1, e = p->lend;
    *from = f;
    *to = e > f && e < len ? e : len;
}

/* a parameter lock lane: a value for each step of one track pattern, LOCK_NONE = the knob's own */
#define LOCK_NONE 0xFFu
/* a lane that is no parameter: a drum step's note, the semitones its hits play away from the key's pitch (TUNE_0 =
 * none; held step + KNOB 4). It is not a lock: no knob gets it, no red dot shows it, SAVE's clear leaves it */
#define LANE_TUNE 0xFFFEu                     /* (past every parameter: synth.h NTP) */
#define LANE_VEL0 0xFF00u                     /* a kit step's own velocity for sound k: lane LANE_VEL0 + k (k 0..10; none:
                                               * the step's velocity), only where a sound of a step has its own */
static inline int lane_is_note(unsigned param)   /* a lane of notes (a kit's tunes, its sounds' velocities), not locks */
{
    return param == LANE_TUNE || (param >= LANE_VEL0 && param < LANE_VEL0 + 16u);
}
#define TUNE_0 64
#define TUNE_MAX 24
typedef struct {
    uint8_t used, gen;                /* gen: bumped when the slot is reused (the interrupt resets its runtime) */
    uint8_t trk, pat;                 /* the track and pattern */
    uint16_t param;                   /* the parameter (synth.h TP_*: past a byte, the kit's keys), or LANE_TUNE */
    uint8_t val[NSTEP];
} lane_t;                             /* 70 bytes */

typedef struct {
    uint8_t pat[NSEQT];               /* each sequenced track's pattern */
    uint8_t used;                     /* 1 = the scene exists */
    uint16_t mute;                    /* bit t = track t muted (NSEQT bits) */
    uint8_t brain_key, brain_scale, brain_deg;   /* the brain's state in this scene */
    uint8_t weave;                    /* the WEAVE track's pattern */
    uint16_t kmute;                   /* the kit sounds muted, on every kit track (bit k: sound k + 1) */
} scene_t;                            /* 20 bytes */
/* format 10's scene (0.5-beta's: 16 of them, no kit mutes), read by project.c's migration */
typedef struct {
    uint8_t pat[NSEQT];
    uint8_t used;
    uint16_t mute;
    uint8_t brain_key, brain_scale, brain_deg;
    uint8_t weave;
} scene10_t;                          /* 18 bytes */
#define NSCENE10 16
/* the WEAVE track's step: the tracks it weaves (a bit each: T1-T8, COND, THROW) and for how long, in sixteenths
 * (0 = one); its pattern: steps on the sequencer's sixteenths, len of them (16, 32, 48 or 64) */
typedef struct {
    uint8_t m0, m1, len;
} wv_step_t;
static inline uint32_t wv_mask(const wv_step_t *w) { return (uint32_t)w->m0 | (uint32_t)w->m1 << 8; }
typedef struct {
    uint8_t len, rsv[3];
    wv_step_t st[NSTEP];
} wvpat_t;                            /* 18 bytes */

typedef struct {
    uint8_t scene;                    /* 0..15 */
    uint8_t rep;                      /* 1..SONG_BARS bars (16 sixteenths each, whatever the patterns' lengths; a project
                                       * saved before counted track 1's spans: its value now reads as bars) */
} song_slot_t;

typedef struct {
    uint8_t len;                      /* slots used, 0 = empty */
    uint8_t loop;                     /* 1 = loop, 0 = stop at the end */
    uint8_t rsv[2];
    song_slot_t slot[NSONG];
} song_t;

/* the whole pattern store: the project's (in the pool) */
typedef struct {
    tpat_t pat[NSEQT][NPATT];
    wvpat_t weave[NPATT];             /* the WEAVE track's patterns */
    lane_t lane[NLANE];
    scene_t scene[NSCENE];
    song_t song;
} patterns_t;

void pattern_init(tpat_t *p);         /* empty, 16 steps, scale 1, no groove, note length half a step */
void patterns_init(patterns_t *P);    /* everything empty, scene 1 used with pattern 1 on every track */
/* lanes */
int lane_find(const lane_t *L, int trk, int pat, int param);     /* lane index, -1 */
int lane_alloc(lane_t *L, int trk, int pat, int param);          /* -1 = full */
void lane_clear(lane_t *L, int k);
int lane_count(const lane_t *L);
int step_has_lock(const lane_t *L, int trk, int pat, int step);
void step_clear_locks(lane_t *L, int trk, int pat, int step);
int step_tune(const lane_t *L, int trk, int pat, int step);       /* a drum step's note: semitones, 0 = none */
int step_vel(const lane_t *L, int trk, int pat, int step, int sound);   /* a kit step's sound's own velocity, 0 = none */
void pattern_clear_locks(lane_t *L, int trk, int pat);   /* (not the kit tune lane: that is notes) */
void cond_ab(int k, int *a, int *b);  /* COND_AB + k -> A:B */
/* a weave: pattern a (the track's) crossed with b (WITH). weave_step: the step the weave plays at pos, the track's
 * steps since it started (each pattern loops by its own length): WV_BARS the bars in turn (a's, b's, ...), WV_STEPS
 * the steps in turn, WV_RHYTHM a's steps with b's notes in their order, cycled, WV_NOTES b's steps with a's notes.
 * The sequencer plays it live; KEEP AS PATTERN writes it */
enum { WV_BARS, WV_STEPS, WV_RHYTHM, WV_NOTES, NWV };
enum { WK_MEL, WK_KIT, WK_FX };       /* a track's kind of steps: notes (synths, the conductor), a kit's sounds, throws */
/* ak / bk: a's and b's kinds; notes cross between them (pattern.c notes_cross: a kit's sounds as a pentatonic ladder
 * from C3, notes as sounds by note mod 11, throws only with throws) */
void weave_step(const tpat_t *a, int ak, const tpat_t *b, int bk, int rule, uint32_t pos, step_t *out);
/* the weave written whole into out (KEEP AS PATTERN): a's settings, the longer one's length (by bars: two bars at
 * least), each step weave_step's from the start */
void pattern_woven(tpat_t *out, const tpat_t *a, int ak, const tpat_t *b, int bk, int rule);
