/* SPDX-License-Identifier: GPL-3.0-only */
/* LOOM-8 synth: a pool of voices playing eight engines (knit, rayon, jacquard, bellows, cane, voile, sample and slice),
 * each voice with two ADSRs, a state-variable filter, a character stage, an LFO and sends. (The kit, EG_DRUMLINE, is
 * dsp/drumline.c's.)
 *
 * A track's sound is a flat byte array par[NTP] (four modules, four knobs each: engine, envelope, filter,
 * LFO; plus the play page, sends, the character stage, player and mix). Continuous values are 0..127, switches small integers (ranges in
 * tp_info()). The same index space addresses parameter locks (seq/pattern.h lane_t.param), LFO
 * destinations and MIDI CCs. The drum engine's 11 sounds take TP_DRUM0 + sound * DP_N + DP_*.
 *
 * Threads: synth_render() runs in the audio interrupt; the main loop only writes par[] bytes and
 * calls synth_note_* through the engine's command queue (app/engine.c). Float, no libm: fastmath.h. */
#pragma once
#include <stdint.h>

#define SY_SR 44100.0f
#define NTRK 8                        /* instrument tracks (seq/pattern.h agrees) */
#define SY_CHUNK 64                   /* control rate: envelopes, LFO, glides, locks */
#define NVOICE 16                     /* synth voices, all tracks */
#define NVOICE_TRK 6                  /* the most one track takes */

/* the engines (2026-10-19: knit the subtractive voice, rayon FM, jacquard the wavetables, bellows the organ; weft,
 * stack, sync, swarm, brocade, gingham and tines gone); the kit (EG_DRUMLINE) is the drum engine, EG_SAMPLE plays a
 * sample of the bank (dsp/sample.h) pitched by the note: sample, start, length, mode (one-shot,
 * loop, reverse, pingpong). EG_SLICE plays a loop (a break) cut into even slices, a note picking one:
 * sample, slices (4, 8, 16, 32: TP_E2 0..3), pitch (+-24 semitones about 64, TP_E3 40..88), gate (how much of
 * the slice plays). The note at the sample's root plays slice 1, the next note slice 2 ..., wrapping:
 * slice = (note - root) mod slices. A sample that states its bars (sample.h smp_t.bars) plays at the
 * tempo (its bars last as many bars of the song), the pitch knob on top; one that does not, at its own rate */
enum { EG_KNIT, EG_RAYON, EG_JACQUARD, EG_BELLOWS, EG_CANE, EG_VOILE, EG_SAMPLE, EG_SLICE, EG_DRUMLINE,
       NENGINE };                                    /* (saved by number: PROJ_FORMAT 10 renumbered them) */
/* the Braids engines (dsp/braids.h, after Mutable Instruments Braids, MIT): E1 the model, E2 Braids' timbre, E3 its
 * color, E4 cane's damp, voile's breath. cane: pluck, bow, reed, flute, bell, drum, at most CN_SLOTS of its voices
 * (each a slot of CN_LEN int16 for its lines); voile: vosim, vowel, choir, whisper */
#define NCANE 6
#define NVOILE 4
#define CN_SLOTS 4
#define CN_LEN 1536
static inline int eg_synth(int e) { return e >= 0 && e < NENGINE && e != EG_DRUMLINE; }   /* a voice engine */
enum { SM_ONESHOT, SM_LOOP, SM_REVERSE, SM_PINGPONG, NSM };   /* the sample engine's modes (TP_E4) */
#define SL_NCOUNT 4                                  /* the slice engine's counts: 4 << TP_E2 */
static inline int sl_count(int v) { return 4 << (v < 0 ? 0 : v >= SL_NCOUNT ? SL_NCOUNT - 1 : v); }
extern const char *const EG_NAME[NENGINE];         /* "knit", "rayon", ... "slice", "kit" */
extern const char *const EG_KNOB[NENGINE][4];      /* each engine's four knob names */

/* a drumline key: its model (drumline.h DM_*), the model's four knobs (names per model: DM_KNOB), then
 * random / pan / volume (-30..+20 dB, 0..127, 76 = 0 dB) and the play mode (drumline.h PM_*); then its own
 * envelope (the kit's Envelope page): attack, hold, decay (127 = off: the model's own length), lag (a delay
 * of 0..60 ms); and its own filter (the Filter page): cutoff (50 Hz .. 20 kHz), resonance, the envelope's
 * reach on the cutoff (64 = none), drive, and the type (DF_*, off by default). Neutral, the key sounds as
 * the model alone: attack 0, hold 0, decay off, lag 0, the filter off, drive 0.
 * On a drumline track TP_A..TP_R are the kit's envelope (the Kit env page): lag (a delay before every hit),
 * attack, random (randomise every model's knobs per hit, the kick's pitch excepted), length (a scale on
 * every model's length); TP_FTYPE / TP_CUT..TP_KTRK the kit's bus filter (Kit filter: FT_DYNAMIC). */
enum { DP_MODEL, DP_K1, DP_K2, DP_K3, DP_K4, DP_RANDOM, DP_PAN, DP_VOLUME, DP_MODE,
       DP_ATTACK, DP_HOLD, DP_DECAY, DP_LAG, DP_CUT, DP_RES, DP_FENV, DP_DRIVE, DP_FTYPE,
       DP_SEND1, DP_SEND2, DP_N };                  /* (the key's own sends: a kit has no track sends) */
enum { DF_OFF, DF_LP, DF_HP, DF_BP, NDF };        /* a key's filter: off, 12 dB low-pass, high-pass, band-pass */
enum {
    TP_ENGINE, TP_E1, TP_E2, TP_E3, TP_E4,         /* Engine page: the engine and its four knobs */
    TP_VMODE, TP_PORTA, TP_BEND, TP_VOLUME,        /* Play page: poly / mono / legato, glide (0 = off), bend range
                                                     * (0 off, 1..7 semitones, 8 = an octave: tp_bend_semi), volume */
    TP_A, TP_D, TP_S, TP_R,                         /* amp ADSR */
    TP_FA, TP_FD, TP_FS, TP_FR,                     /* filter ADSR */
    TP_CUT, TP_RES, TP_FENV, TP_KTRK, TP_FTYPE,     /* Filter page (cutoff 50 Hz .. 20 kHz) and the type (FT_*); on a
                                                     * drumline track low cut / compression / drive / hi cut */
    TP_SEND1, TP_SEND2,                             /* sends: fx I, fx II */
    TP_LTYPE, TP_LSPEED, TP_LAMT, TP_LDEST, TP_LX,  /* LFO: type (off, wave, wobble, drift, pump), then its four
                                                     * knobs as the comment below the enum says */
    TP_LX2,                                         /* a fifth LFO value: shape (wave, wobble) or envelope (drift) */
    TP_CTYPE, TP_CAMT, TP_CTONE, TP_CMIX,           /* Character: the voice's own effect after the filter (CT_*),
                                                     * its amount, tone (per type, below) and mix (dry .. wet) */
    TP_X1, TP_X2, TP_X3, TP_X4,                     /* the engine's second page (its names per engine: tp_info; "-"
                                                     * where unused): knit's osc 2, level, sub, noise; rayon's fall,
                                                     * floor, keyscale */
    TP_OCT,                                         /* Play 2: the sound's octave, 0..4 = -2..+2 (2: as played); a
                                                     * preset's by its role (a bass -2). Moves the notes, sequenced too */
    TP_PLAYER, TP_P1, TP_P2, TP_P3, TP_P4,          /* Players: off / arp / strum / hold and its four knobs */
    TP_AP1, TP_AP2, TP_AP3, TP_AP4,                 /* the arp's second page: length, style, glide, stereo */
    TP_LEVEL, TP_PAN,                               /* Mix */
    TP_BRAIN,                                       /* follows the brain: 0 no, 1 diatonic, 2 transpose only */
    TP_CHAN,                                        /* MIDI channel 0 = off, 1..16 */
    TP_DRUM0,                                       /* the drum kit: NDRUM keys * DP_N */
    NTP = TP_DRUM0 + 11 * DP_N
};
_Static_assert(NTP <= 511, "a parameter's index: nine bits in the engine's queue (engine.c cq_put); a lane's is 16");
#define TP_DRUM(s, k) (TP_DRUM0 + (s) * DP_N + (k))
#define NTP_SOUND TP_PLAYER                          /* a preset keeps par[0..TP_PLAYER): the character, the engine's
                                                      * second page (TP_X1..X4) too */

enum { VM_POLY, VM_MONO, VM_LEGATO };
/* filter types: lp24 (two state-variable stages, 24 dB, aggressive), ladder (Moog-like, saturating feedback),
 * lp12 (one resonant stage, gentle 12 dB), hp12 (its high-pass), dynamic (drum tracks: low cut, compression,
 * drive, hi cut on the Filter page's four knobs instead of cutoff / res / env / keytrack; on a synth voice a
 * bypass), bp12 (the lp12 stage's band-pass, its peak at unity) */
enum { FT_LP24, FT_LADDER, FT_LP12, FT_HP12, FT_DYNAMIC, FT_BP12, NFT };
/* the character stage, per voice after the filter and before the amp and the sends (no memory beyond a few
 * filter states: cheap); a kit's on the kit's summed pair after its bus filter (char_bus: the keys' sends are
 * taken before it, as before the bus filter). amount / tone per type:
 *   drive:   a soft (tanh-like) saturation, the gain made up so the level holds; tone = a low-pass after it
 *            (400 Hz .. 20 kHz: open)
 *   fold:    a sine wavefolder, gain made up; tone = its bias (64 = symmetric; off centre: even harmonics)
 *   crush:   fewer bits and a lower sample rate; tone = which falls (0 the rate only, 127 the bits only)
 *   ring:    a ring modulator, a sine at a ratio of the note (tone: 0.25:1 .. 7:1 in 16 steps); amount = depth (a kit: a
 *            ratio of C4)
 *   tilt:    a tilt EQ: amount = its pivot (150 Hz .. 6 kHz), tone = the tilt (below 64 darker, above brighter,
 *            +-6 dB each side at the ends)
 *   tape:    worn tape: a soft saturation (gain 0.5 .. 8, made up) into a two-pole roll-off (tone: 1.5 .. 18 kHz), and
 *            a wow on the pitch (0.55 Hz, with a 6 Hz flutter a quarter as deep): 2 .. 10 cents by the amount (a kit:
 *            no wow, it has no pitch to bend)
 *   sampler: an old sampler: the voice low-passed, held at a lower rate and fewer bits (amount: 26 kHz and 14 bits ..
 *            11 kHz and 11 bits: a voice sits well under full scale), then its output filter (tone: 2 .. 20 kHz): the
 *            dusty grain, not the crush's rasp
 * mix: dry (0) .. wet (127); at 0 the stage is skipped. The LFO reaches the amount (the voice module's knob 4).
 * (Saved by number: a new type goes at the end.) */
enum { CT_OFF, CT_DRIVE, CT_FOLD, CT_CRUSH, CT_RING, CT_TILT, CT_TAPE, CT_SAMPLER, NCT };
/* the LFO: TP_LSPEED is one knob: 0..7 synced 2/1, 1, 1/2, 1/4, 1/8, 1/16, 1/32, 1/64
 * (counter-clockwise), 8..127 free (clockwise, 0.1..30 Hz). Per type the five values TP_LSPEED / TP_LAMT /
 * TP_LDEST / TP_LX / TP_LX2 mean:
 *   wave:    speed, amount -100..100 % (64 = 0), destination (LD_*: the module synth / envelope / filter / voice,
 *            + LD_FREE for the free-running version), parameter (TP_LX: the module's knob
 *            0..3: the engine's four, attack / decay / sustain / release, cutoff / reso / env / keytrack, volume /
 *            pitch / pan / character (its amount)), shape (TP_LX2: square, sine, ramp, saw).
 *            100 % = the destination knob's whole range; pitch = +-12 semitones.
 *   drift:   speed, amount 0..100 % (unipolar: the offset only adds), destination (LD_*), parameter (TP_LX),
 *            envelope (TP_LX2: fade in / fade out, below)
 *   wobble:  rate, vibrato amount -100..100 % (64 = 0, 100 % = +-1 semitone), volume amount -100..100 % (TP_LDEST,
 *            64 = 0), envelope (TP_LX), shape (TP_LX2: sine, saw, exponential, square, blip)
 *   pump:    source (TP_LSPEED: 0..7 tracks 1-8, 8..9 brain / punch, 10 metronome: DUCK_METRO), amount 0..100 %,
 *            hold (TP_LDEST, ms), release (TP_LX, ms)
 * Regular (not free): the LFO's phase restarts on every note of the track (drift draws a fresh value too).
 * The envelope knob: 64 = none; below, the LFO fades in from the note over up to 3 s (0 = 3 s, 63 = at once);
 * above, it fades out over up to 3 s (65 = 3 s, 127 = 50 ms). */
enum { LT_OFF, LT_WAVE, LT_WOBBLE, LT_DRIFT, LT_PUMP, NLT };
/* the shared LFO (GLO's LFO page): one for the project, moving the tracks it is routed to. Its knobs live in the
 * conductor track's (AUX_BRAIN's) own LFO slots, unused by it: TP_LTYPE the shape (0 off, 1 + GS_*), TP_LSPEED the
 * rate (0..GL_SYNC_N-1 synced 16 bars .. 1/16, then free 0.02 .. 10 Hz), TP_LAMT the amount (64 = 0, -100..100 % of
 * the destination's range), TP_LDEST the destination (GD_*), TP_LX / TP_LX2 the tracks it moves (T1-T4, T5-T8: a bit
 * each). It restarts when the transport starts. */
enum { GS_SINE, GS_TRI, GS_UP, GS_DOWN, GS_SQUARE, GS_RANDOM, NGS };
enum { GD_CUT, GD_RES, GD_PITCH, GD_PAN, GD_VOL, GD_SEND1, GD_SEND2, GD_CHAR, NGD };
#define GL_SYNC_N 9
#define SY_GLFO_TRK NTRK                         /* the track holding its knobs: the conductor (pattern.h AUX_BRAIN) */
enum { LD_SYNTH, LD_ENV, LD_FILTER, LD_AMP, LD_FREE = 4, NLD = 8 };   /* TP_LDEST: module (LD_AMP: "voice"), + LD_FREE = free-running */
#define LS_SYNC_N 8                                /* TP_LSPEED values below this are synced divisions */
enum { PL_OFF, PL_ARP, PL_STRUM, PL_HOLD, NPL };
/* arp: P1 speed (1/4 1/4T 1/8 1/8T 1/16 1/16T 1/32 1/32T 1/64), P2 pattern (up, down, bounce (up and down, the
 * ends once), bounce+ (the ends repeated), as played, random), P3 range 1..4 octaves, P4 hold; its page 2 is
 * TP_AP1..AP4 = note length (short medium long), style (off, pedal, thirds, double, skip, swap: seq.c
 * arp_style_pos), glide (off low high), stereo (mono narrow wide). strum: P1 roll 0..63 (63 = a note per sequencer step of the
 * track), P2 pattern (up down up-down random), P3 -, P4 hold */

/* how a value is shown: a number, a percentage, bipolar (64 = 0), a name from the list, semitones, on / off,
 * milliseconds / seconds, the LFO speed (a division or Hz), 00..99 (the engine knobs),
 * Hz / kHz (cutoff), an LFO fade ("in 50 %" / "off" / "out 50 %"), dB */
enum { PK_NUM, PK_PCT, PK_BIP, PK_NAMES, PK_SEMI, PK_SWITCH, PK_MS, PK_HZ_SYNC, PK_NN, PK_HZ, PK_FADE, PK_DB,
       PK_SAMPLE,                                  /* a sample of the bank: its name (dsp/sample.h smp_name) */
       PK_BIPST,                                   /* semitones about 64: "+3 st" */
       PK_DMS,                                     /* a drum key's time: 0 = 0 ms, else as PK_MS */
       PK_DECAY,                                   /* the same, 127 = "off" */
       PK_LAGMS,                                   /* 0..127 = 0..60 ms */
       PK_FADER };                                 /* a track's level: dB by tp_level_gain, 0 = "off" */
typedef struct {
    const char *name;                 /* short, as on the knob row (<= 8 chars) */
    uint8_t lo, hi, def;
    uint8_t kind;                     /* PK_* */
    const char *const *names;         /* PK_NAMES */
} tp_info_t;
const tp_info_t *tp_info(int p, int engine);        /* engine: the track's (TP_E1..E4 names depend on it) */
void tp_text(int p, int v, int engine, char *num, char *unit);   /* the value as the screen shows it (<= 11, <= 4) */
/* the same, knowing the whole track: the player's knobs (TP_P1..P4) and the LFO's fields (TP_LAMT, TP_LDEST,
 * TP_LX) take their names, ranges and kinds from TP_PLAYER / TP_LTYPE */
const tp_info_t *tp_info_par(int p, const uint8_t *par);
void tp_text_par(int p, int v, const uint8_t *par, char *num, char *unit);
void sound_default(uint8_t *par, int engine);       /* par[NTP] for a fresh track of this engine */
/* the Character page's picture of the stage (no state): per type the curve at u. drive, fold and off:
 * the transfer, the output for an input u (-1..1); crush: a cycle of a sine (u 0..1) as it comes out, held and
 * quantised; ring: two cycles of a sine (u 0..1) ring-modulated (its peak at 1); tilt: the response in dB / 6 from 50 Hz
 * (u = 0) to 20 kHz (u = 1). The mix is in it. */
float char_picture(const uint8_t *par, float u);
/* a kit's character: the stage (par's TP_CTYPE..TP_CMIX) on the kit's dry pair, n samples in place. The ring's
 * sine is at KIT_RING_HZ times the tone's ratio (C4: the kit is tuned to C). A zeroed chbus_t is a fresh one */
#define KIT_RING_HZ 261.63f
typedef struct {
    float cx[8], cz[8], cin[2];
    uint8_t type, cin_type;
} chbus_t;
void char_bus(chbus_t *c, const uint8_t *par, float *l, float *r, int n);
/* the Engine page's picture of the organ: the six drawbars' levels (16' 8' 4' 2 2/3' 2' 1 1/3', 0..1, the loudest 1) that
 * stops and pedal set (organ_prep's registration) */
void organ_bars(const uint8_t *par, float bars[6]);
/* a track's level (TP_LEVEL) as a gain: a fader's law, cubic (60 dB over 0..100: half way -18 dB), 100 = 0 dB,
 * 127 = +6.2 dB, 0 = off. The drums and the FX returns take it as is; the synth's voices with 100/127 under it
 * (their headroom: synth.c) */
static inline float tp_level_gain(int v)
{
    float x = (float)v * 0.01f;
    return x * x * x;
}
static inline int tp_bend_semi(int v) { return v >= 8 ? 12 : v; }   /* TP_BEND as semitones */

/* factory presets: the sound part of par[], and its role (what it is for: the PRESETS message says it, "PAD 3/16",
 * and tools/render_presets.sh --roles plays each as it is used). Sixteen an engine, in their engine's order */
enum { RL_BASS, RL_LEAD, RL_PAD, RL_KEYS, RL_PLUCK, RL_BELL, RL_STAB, RL_STRINGS, RL_BRASS, RL_VOICE, RL_CHORD, RL_FX,
       RL_DRUM, RL_LOOP, NRL };
extern const char *const RL_NAME[NRL];            /* "bass", "lead", ... */
typedef struct {
    const char *name;
    uint8_t par[NTP_SOUND];
    uint8_t role;                                 /* RL_* */
} preset_t;
extern const preset_t PRESETS[];
extern const int NPRESETS;
int preset_first(int engine);                       /* the first preset of an engine, -1 */

/* --------------------------------------------------------------- voices --- */
/* performance state per track, written by the sequencer / punch-in FX, read by the voices each chunk */
typedef struct {
    int16_t pitch_c;                  /* cents, from bends, ramps, punch octave */
    int16_t cut;                      /* filter cutoff offset, -127..127 */
    int8_t pan;                       /* pan offset */
    uint8_t mute;                     /* 1 = silent (punch mute: voices keep running) */
    uint8_t send1, send2;             /* send offsets 0..127 (punch delay / reverb up) */
    uint8_t long_tail;                /* drums: decay x4 */
    uint8_t hp;                       /* high-pass 0..127 (the punch filter on drums; the engine also tells the
                                       * drumline's dynamic filter); the synth may ignore it */
} perf_t;

typedef struct synth synth_t;         /* opaque: dsp/synth.c */
void synth_glfo_restart(synth_t *s);              /* the shared LFO from its start (the transport started) */
float synth_glfo_now(const synth_t *s);            /* its value now, -1..1 (the page's picture) */
#define SYNTH_BYTES 12288             /* room for the struct (checked at compile time in synth.c) */

void synth_init(synth_t *s, uint8_t (*par)[NTP]);   /* par: the tracks' parameter arrays, NTRK + 1 of them (the conductor's, SY_GLFO_TRK: the shared LFO) */
void synth_tempo(synth_t *s, float bpm);
/* notes: tag identifies a note for its off (the sequencer uses the step, the keys the key) */
void synth_note_on(synth_t *s, int trk, int note, int vel, int glide_ms, uint32_t tag);
void synth_note_off(synth_t *s, int trk, uint32_t tag);
void synth_all_off(synth_t *s, int trk);            /* -1: every track (panic) */
void synth_bend(synth_t *s, int trk, int cents);    /* performance bend (the bend component, MIDI) */
perf_t *synth_perf(synth_t *s, int trk);            /* the track's performance state (write from the interrupt) */
#ifndef LOCK_NONE
#define LOCK_NONE 0xFFu               /* seq/pattern.h's: no lock */
#endif
void synth_lock(synth_t *s, int trk, int param, int val);   /* a parameter lock in effect (LOCK_NONE: the knob's) */
#define DUCK_METRO 10                 /* the pump's source numbers: 0..7 tracks, 8 brain, 9 punch, 10 the metronome */
void synth_duck_trigger(synth_t *s, int src);      /* a hit on a duck source (0..7 tracks, 8..9 brain / punch,
                                                    * DUCK_METRO the metronome): duck LFOs listening to it drop */
/* a block: synth_prepare(n) once (the tracks' LFO, tremolo and duck values for every chunk boundary inside
 * the next n frames, n <= 512, and the meters), then synth_render(n) for each voice range, on any core: a
 * render touches only its voices' state, so 0..8 on one core and 8..16 on the other may run at once.
 * Notes, locks, bends and perf writes happen between blocks (the command queue), never during a render.
 * Every voice range must be rendered each block, with the n given to prepare. */
void synth_prepare(synth_t *s, uint32_t n);
/* render n frames (any n; chunks inside): dry L/R and the two sends (mono), added into the buffers.
 * v0..v1 is the range of voices to render (the two cores split the pool) */
void synth_render(synth_t *s, int v0, int v1, float *l, float *r, float *s1, float *s2, uint32_t n);
/* the same, saying which core renders (0 the first, 1 the second: the profiler's halves) */
void synth_render_on(synth_t *s, int core, int v0, int v1, float *l, float *r, float *s1, float *s2, uint32_t n);
void synth_order(const synth_t *s, int8_t *ord);   /* the NVOICE voices, grouped by engine on the FM-1 (the order the
                                                      * cores take them in: engine.c render_sub); the simulator's in order */
int synth_sounding(const synth_t *s, int v0, int v1);          /* a voice in the range is sounding */
int synth_voices_used(const synth_t *s);
/* the slice engine's newest playing voice on a track (the picture): its slice (0-based) and the position in
 * the sample; 0 when none plays. Called from the main loop: a racy read of the voices, for the screen only */
int synth_slice_now(const synth_t *s, int trk, int *slice, int32_t *pos);
void synth_limit(synth_t *s, int max_voices);                  /* the overload guard: fewer voices */
int synth_lock_value(const synth_t *s, int trk, int param);   /* a lock's value now, -1 none (the UI's dot) */
void synth_load_hint(synth_t *s, uint32_t pct);              /* the last block's load, %: the note-start check */
extern uint32_t synth_load_steals;                            /* notes that took a sounding voice by the load check */
/* what voice vi costs to render, in rough units for the cores' split (engine.c split_at): 0 silent, 4 an
 * ordinary voice (bellows 5), knit 3 + its unison (+ 1 osc 2, + 1 sub or noise); the Braids engines by their model
 * (braids.h: 3 .. 8, cane's bell the most) */
int synth_voice_cost(const synth_t *s, int vi);
extern volatile uint16_t synth_peak[NTRK];                     /* per track meters, Q14, decaying (UI) */
/* the profiler (samples.c SB_PROF, tools/perf_read.py): the time each voice took, by its engine, in synth_clock's
 * units (engine.c sets it: plat_cycles; none, no timing), running totals (wrapping) for each half of the pool: [0] the
 * first core's voices, [1] the second core's; [.][NENGINE] every voice's control-rate work (voice_chunk) */
extern uint32_t (*synth_clock)(void);
extern volatile uint32_t synth_prof[2][NENGINE + 1];
extern volatile uint32_t synth_vworst[3];
extern volatile uint32_t synth_vblk[NVOICE];
extern volatile uint8_t synth_vinfo[NVOICE];
#ifdef PLAT_HOST
extern uint16_t synth_host_solo;              /* the host's stems (host.c LOOM_SOLO): bits 0-7 tracks, 8 / 9 FX I / II */
#endif
