/* SPDX-License-Identifier: MPL-2.0 */
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
#include "efx.h"

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
/* EG_MIDI plays nothing here: the track's notes go out on its own channel (always: its MIDI OUT does not apply),
 * and its eight knobs (TP_E1..E4, TP_X1..X4) each send a CC on that channel when they move (by hand, a lock,
 * automation); the knobs' CC numbers in TP_A..TP_R and TP_FA..TP_FR (no envelope on such a track): MIDI_CC_P */
/* EG_ACID (2026-10-30): a TB-303, X0X's (dsp/bass303.c, GPL-3.0-only: Open303 by Robin Schmidt, MIT, with jc303's
 * Devilfish ranges and dm-Rat's RAT drive), whole: its own oscillator, filter, envelopes, accent and slide. One voice a
 * track (mono whatever the Play page says), at most NACID of them (each a bass303_t in RAM). Its track's Filter page is
 * the 303's cutoff, resonance, env mod and decay (TP_CUT, TP_RES, TP_FENV, TP_KTRK), the Engine page wave, accent,
 * slide time, accent decay, the second page drive and its type (off, soft, rat); the voice's own filter and
 * envelopes are left out (the character stage, the LFO, the sends and the mix are as on any track). A note of
 * velocity ACID_ACCENT or more is accented; a note that starts while the last one is still held slides to it, and so
 * does a note with a glide (a step's glide trick, the arp's glide) after a short one: on the same pitch, a tie. */
enum { EG_KNIT, EG_RAYON, EG_JACQUARD, EG_BELLOWS, EG_CANE, EG_VOILE, EG_SAMPLE, EG_SLICE, EG_DRUMLINE, EG_MIDI,
       EG_ACID, EG_TULLE, EG_LACE, EG_TARTAN, NENGINE };                           /* (saved by number: PROJ_FORMAT 10 renumbered them; a new one
                                                      * goes last) */
#define NACID 2
#define ACID_ACCENT 112
/* EG_TULLE (2026-11-23): grains of a sample of the bank. Its Engine page the playhead: E1 the sample, E2 position (where
 * in it), E3 spray (each grain's position jittered, and its start in time: regular at 0, random at the top), E4 scan
 * (the position moving on its own: 0 frozen); its Sub page (TP_X1..X4) the grains: size (a grain's length, 10 .. 500
 * ms), density (grains a second, 2 .. 60), pitch (each grain detuned at random, up to an octave), width (the grains
 * across the stereo field). At most CN_SLOTS of its voices (cane's slots: a cap), their grains from one pool of
 * TL_POOL, each with its own decode window */
#define TL_POOL 32                                   /* eight a note: its slot's own (TL_POOL / CN_SLOTS) */
#define TL_VOICE_MAX 8                               /* a note's grains at most (its voice on one core: the FM-1's budget) */
/* EG_LACE (2026-11-23): a modal resonator: LC_MODES tuned two-pole resonators, struck, blown or bowed. Its Engine page
 * E1 the material (the modes' ratios and how long each rings: NLACE of them), E2 bright (the higher modes' share), E3
 * damp (how fast they die; the higher sooner), E4 position (where it is struck: a comb on the modes); its Exciter page
 * (TP_X1..X4) the exciter (LX_*), stretch (the modes' spacing, about 64), spread (the modes across the stereo field),
 * hardness (the mallet's, the noise's color, the bow's pressure). At most CN_SLOTS of its voices (cane's slots: their
 * resonators' state in lc_mem) */
#define NLACE 9
#define LC_MODES 12
enum { LX_MALLET, LX_NOISE, LX_BOW, NLX };
/* EG_TARTAN (2026-11-23): Chordism (schwung-chordism, the user's, MIT) as an engine: each note a chord of four
 * oscillators, each its own wave, level and place in the stereo field. Its Engine page E1 the chord (TT_NCHORD types),
 * E2 spread (the intervals scaled, 64 as the type has them), E3 rotation (the inversions, 0..3), E4 detune (each voice
 * on, up to 50 cents); its Sub page (TP_X1..X4) the waves (TT_NWAVES sets of four), shape, width, FM (voice 1 on the
 * others' phase); its Morph page (TP_KSYNC..TP_KSUB, knit's Mod page's bytes) morph (the voices' levels, Chordism's
 * sixteen), its amount, pan morph (their places, its sixteen), sway (each voice's shape swept, a quarter cycle on: Chordism's
 * shape LFO) */
#define TT_NCHORD 18                         /* Chordism's 16, then "in key" (thirds stacked in the conductor's scale on
                                              * the note) and "conductor" (its chord now, voiced from the note up) */
#define TT_INKEY 16
#define TT_COND 17
#define TT_NWAVES 12
/* the conductor, for tartan's "in key" and "conductor" (the engine's, each block): its key (0..11), its scale (a
 * 12-bit mask from the key), its chord now (up to four MIDI notes in a word's bytes, 0 none: seq.h brain_chord) */
void synth_conductor(int key, uint16_t scale_mask, uint32_t chord);
void tartan_voices(const uint8_t *par, float *semis, float *lvl, float *pan);   /* its chord's voices (the picture) */
/* the Braids engines (dsp/braids.h, after Mutable Instruments Braids, MIT): E1 the model, E2 Braids' timbre, E3 its
 * color, E4 cane's damp, voile's breath. cane: pluck, bow, reed, flute, bell, drum, at most CN_SLOTS of its voices
 * (each a slot of CN_LEN int16 for its lines); voile: vosim, vowel, choir, whisper */
#define NCANE 6
#define NVOILE 4
#define CN_SLOTS 4
#define CN_LEN 1536
static inline int eg_synth(int e) { return e >= 0 && e < NENGINE && e != EG_DRUMLINE && e != EG_MIDI; }   /* a voice
                                                                                                         * engine */
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
                                                     * (0 off, 1..7 semitones, 8 = an octave: tp_bend_semi); volume
                                                     * (Play 2; knit's on Levels) */
    TP_A, TP_D, TP_S, TP_R,                         /* amp ADSR */
    TP_FA, TP_FD, TP_FS, TP_FR,                     /* filter ADSR */
    TP_CUT, TP_RES, TP_FENV, TP_KTRK, TP_FTYPE,     /* Filter page (cutoff 50 Hz .. 20 kHz) and the type (FT_*); on a
                                                     * drumline track low cut / compression / drive / hi cut */
    TP_SEND1, TP_SEND2,                             /* sends: fx I, fx II */
    TP_LTYPE, TP_LSPEED, TP_LAMT, TP_LDEST, TP_LX,  /* LFO: type (off, wave, wobble, drift, pump), then its four
                                                     * knobs as the comment below the enum says */
    TP_LX2,                                         /* a fifth LFO value: shape (wave, wobble) or envelope (drift) */
    TP_CTYPE, TP_CAMT, TP_CTONE, TP_CMIX,           /* the track's slot 1 (the slots: below): its effect (dsp/efx.h
                                                     * EF_*: the character was its first types), knobs 1 and 2, mix */
    TP_X1, TP_X2, TP_X3, TP_X4,                     /* the engine's second page (its names per engine: tp_info; "-"
                                                     * where unused): knit's interval (its Osc 2 page), osc 2's
                                                     * level, sub, noise (Levels); rayon's fall, floor, keyscale */
    TP_OCT,                                         /* Play's fourth (MIDI's Play 2): the sound's octave, 0..4 = -2..+2
                                                     * (2: as played); a preset's by its role (a bass -2). Moves the
                                                     * notes, sequenced too */
    TP_PLAYER, TP_P1, TP_P2, TP_P3, TP_P4,          /* Players: off / arp / strum / hold and its four knobs */
    TP_AP1, TP_AP2, TP_AP3, TP_AP4,                 /* the arp's second page: length, style, glide, stereo */
    TP_LEVEL, TP_PAN,                               /* Mix */
    TP_BRAIN,                                       /* follows the brain: BM_* (0 no, 1 diatonic, 2 transpose only,
                                                     * 3 harmony, 4 chord degree) */
    TP_CHAN,                                        /* MIDI OUT: 0 off, 1 on (on the track's own channel) */
    TP_DRUM0,                                       /* the drum kit: NDRUM keys * DP_N */
    TP_DRUMX0 = TP_DRUM0 + 11 * DP_N,               /* the keys' second page of model knobs (a model with eight:
                                                     * drumline.h DM_KNOB2): NDRUM keys * DPX_N; after the
                                                     * keys, so format 14's indices (locks, CCs) stay as they were */
    TP_O2FINE = TP_DRUMX0 + 11 * 4,                 /* knit's osc 2, fine: cents about 64 (14..114 = -50..+50; its Osc 2
                                                     * page). Last, so format 15's indices stay (format 16) */
    TP_RUP,                                         /* the random player's octaves up, 0..3 (TP_P4 its octaves down).
                                                     * Last, so format 18's indices stay (format 19) */
    TP_O2WAVE,                                      /* knit's osc 2, its wave (O2W_*: same, osc 1's, by default) and */
    TP_O2SHAPE,                                     /* its own saw .. square .. pulse (as osc 1's wave knob: saw, square,
                                                     * pulse only). Last, so format 21's indices stay (format 22) */
    TP_KSYNC, TP_KSWEEP, TP_KCROSS, TP_KSUB,        /* (tulle's Grain page: TP_KSYNC its grains' shape 0 smooth .. 127
                                                     * percussive, TP_KSWEEP the chance a grain plays backwards; the
                                                     * rest unused) knit's Mod page: osc 2 hard-synced to osc 1 (0 off, 1 on); the
                                                     * filter envelope on osc 2's pitch (0..48 semitones at its top);
                                                     * osc 2 on osc 1's frequency (FM, 0..127); the sub's type (KS_*).
                                                     * 0 each: as before. Last, so format 22's indices stay (format 23) */
    TP_ROPS, TP_RRATIO, TP_RDEPTH, TP_RFB,          /* rayon's Ops page: how ops 3 and 4 join its pair (RO_*), op 3's
                                                     * ratio (RO_NRATIO steps), their depth, op 4's feedback.
                                                     * 0 (off): rayon as before. Last, so format 23's indices stay
                                                     * (format 24) */
    TP_KANALOG,                                     /* knit's analog (its Levels page): drift, each note a little off,
                                                     * softer edges and some saturation (0: knit as before). Last, so
                                                     * format 24's indices stay (format 25) */
    TP_ITYPE, TP_I1, TP_I2, TP_I3,                  /* the track's slot 2 (was its insert): its effect (EF_*) and its
                                                     * knobs 1..3. Last, so format 25's indices stay (format 26) */
    TP_HPF,                                         /* its low cut (Play 2): 0 off, 1..127 a 12 dB high-pass at 20 Hz ..
                                                     * 2 kHz on each voice before its filter (the Juno-106's HPF) */
    TP_S1K3, TP_S1X1, TP_S1X2, TP_S1X3, TP_S1X4,    /* slot 1's knob 3 and extras, */
    TP_S2MIX, TP_S2X1, TP_S2X2, TP_S2X3, TP_S2X4,   /* slot 2's mix and extras (format 28: the one effects list). Last,
                                                     * so format 27's indices stay */
    NTP
};
enum { TP_S1TYPE = TP_CTYPE, TP_S1K1 = TP_CAMT, TP_S1K2 = TP_CTONE, TP_S1MIX = TP_CMIX,
       TP_S2TYPE = TP_ITYPE, TP_S2K1 = TP_I1, TP_S2K2 = TP_I2, TP_S2K3 = TP_I3 };
/* a track's two slots (the one effects list: docs/superpowers/specs/2026-11-25-one-effects-list-design.md), each any
 * effect of the catalog (dsp/efx.h EFX[]) with its three knobs, its mix (dry 0 .. wet 127; 0 skips it) and its extras
 * (the SEL list's): TP_SLOT[slot][SL_*]. On a synth track slot 1 then slot 2 run on the track's bus, before its fader
 * and its sends; ring and tape run on each voice first (ring's sine follows each note; tape's wow bends the voices'
 * pitch, its saturation on the bus), so a voice's effects come before the bus's whatever the slots' order. (A second
 * ring on the bus, at a ratio of C4.) On a kit both on the kit's pair after its bus filter (ring at a ratio of C4, tape
 * without its wow: no pitch to bend; rumble hears the kicks alone). Effects with memory (dsp/efx.h efx_need: chorus,
 * lofi, juno a 2 KB block, rumble two, a delay or a reverb many) take it from the one pool (efx_pool), after the sends;
 * a slot that finds none passes dry (synth_slot_room) */
enum { SL_EF, SL_K1, SL_K2, SL_K3, SL_MIX, SL_X1, SL_X2, SL_X3, SL_X4, SL_N };
extern const uint16_t TP_SLOT[2][SL_N];
int tp_slot_of(int p, int *j);                   /* the slot a parameter is in (0, 1; -1 none), and which (SL_*) */
_Static_assert(NTP <= 511, "a parameter's index: nine bits in the engine's queue (engine.c cq_put); a lane's is 16");
/* knit's osc 2 wave (TP_O2WAVE): osc 1's (its wave knob and the LFO on it), or its own: saw, square and pulse are
 * points of its shape's sweep (TP_O2SHAPE: turning the wave sets the shape there), triangle and sine */
enum { O2W_SAME, O2W_SAW, O2W_SQUARE, O2W_PULSE, O2W_TRI, O2W_SINE, NO2W };
/* knit's sub (TP_KSUB): a sine an octave down (as before), a square an octave down, a square two octaves down, a narrow
 * pulse two octaves down (the SH-101's three) */
enum { KS_SINE, KS_SQUARE, KS_LOW_SQUARE, KS_LOW_PULSE, NKS };
#define KSWEEP_MAX 48                                /* TP_KSWEEP: semitones */
/* rayon's ops 3 and 4 (TP_ROPS): off (its pair alone, as before); stack (4 -> 3 -> its modulator: growls, metal, digital
 * leads); layer (4 -> 3, a second pair added: a bell over a body). (No organ: bellows is the organ) */
enum { RO_OFF, RO_STACK, RO_LAYER, NRO };
#define RO_NRATIO 24
extern const float RO_RATIO[RO_NRATIO];            /* op 3's ratios (op 4 at op 3's frequency) */
static inline int o2w_shaped(int w) { return w >= O2W_SAW && w <= O2W_PULSE; }   /* the shape knob counts */
/* the random player's octave range: how many octaves below (TP_P4, "oct down") and above (TP_RUP, "oct up") the
 * step's own a note may land, each 0..3 (0 and 0: the step's own octave only) */
static inline int rand_oct_lo(int v) { return v < 0 ? 0 : v > 3 ? 3 : v; }
static inline int rand_oct_hi(int v) { return v < 0 ? 0 : v > 3 ? 3 : v; }
/* how a track follows the conductor (TP_BRAIN): 0 and 1..2 are saved before harmony and keep their meaning;
 * 3 and 4 are appended (append only: a project stores the byte). 3 "harmony" takes each note to the nearest tone of
 * the active chord; 4 "ch. deg" takes the note's place in the scale to pick a chord tone. See seq.c brain_note */
enum { BM_NO, BM_DIATONIC, BM_TRANSPOSE, BM_HARMONY, BM_DEGREE, NBM };
#define TP_DRUM(s, k) (TP_DRUM0 + (s) * DP_N + (k))
#define DPX_N 4                                      /* knobs 5..8 of a key's model */
#define TP_DRUMX(s, j) (TP_DRUMX0 + (s) * DPX_N + (j))
static inline int tp_is_dkey(int p) { return p >= TP_DRUM0 && p < TP_DRUMX0; }   /* a key's DP_* */
static inline int tp_is_dknob2(int p) { return p >= TP_DRUMX0 && p < TP_O2FINE; }   /* a key's knob 5..8 */
/* the MIDI engine (EG_MIDI): its knob k 0..7 (TP_E1..E4, TP_X1..X4) from a parameter, -1 none; the parameter
 * holding knob k's CC number (TP_A..TP_R, TP_FA..TP_FR) */
static inline int midi_knob(int p)
{
    return p >= TP_E1 && p <= TP_E4 ? p - TP_E1 : p >= TP_X1 && p <= TP_X4 ? 4 + p - TP_X1 : -1;
}
static inline int midi_cc_par(int k) { return k < 4 ? TP_A + k : TP_FA + k - 4; }
static inline int midi_cc_of(int p) { return p >= TP_A && p <= TP_R ? p - TP_A : p >= TP_FA && p <= TP_FR ? 4 + p - TP_FA : -1; }
#define NMIDI_CC 120                                 /* a knob's CC: 0..119 (120..127 are the channel modes) */
#define NTP_SOUND TP_PLAYER                          /* a preset keeps par[0..TP_PLAYER): the character, the engine's
                                                      * second page (TP_X1..X4) too */

enum { VM_POLY, VM_MONO, VM_LEGATO };
/* filter types: lp24 (two state-variable stages, 24 dB, aggressive), ladder (Moog-like, saturating feedback),
 * lp12 (one resonant stage, gentle 12 dB), hp12 (its high-pass), dynamic (drum tracks: low cut, compression,
 * drive, hi cut on the Filter page's four knobs instead of cutoff / res / env / keytrack; on a synth voice a
 * bypass), bp12 (the lp12 stage's band-pass, its peak at unity) */
enum { FT_LP24, FT_LADDER, FT_LP12, FT_HP12, FT_BP12, FT_DYNAMIC, NFT };   /* (format 14 moved FT_DYNAMIC
                                                                           * last, past bp12: project.c migrates
                                                                           * an older project's TP_FTYPE) */
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
 * each), TP_P1 a kit's sound it moves (0 every one, 1..11 that key: engine.c glfo_kits). It restarts when the
 * transport starts. */
enum { GS_SINE, GS_TRI, GS_UP, GS_DOWN, GS_SQUARE, GS_RANDOM, NGS };
enum { GD_CUT, GD_RES, GD_PITCH, GD_PAN, GD_VOL, GD_SEND1, GD_SEND2, GD_CHAR, NGD };
#define GL_SYNC_N 9
#define SY_GLFO_TRK NTRK                         /* the track holding its knobs: the conductor (pattern.h AUX_BRAIN) */
enum { LD_SYNTH, LD_ENV, LD_FILTER, LD_AMP, LD_FREE = 4, NLD = 8 };   /* TP_LDEST: module (LD_AMP: "voice"), + LD_FREE = free-running */
#define LS_SYNC_N 8                                /* TP_LSPEED values below this are synced divisions */
enum { PL_OFF, PL_ARP, PL_STRUM, PL_HOLD, PL_RAND, NPL };
#define NARPSPD 11
#define ARP_1_16 6                                 /* the arp's speed 1/16 (its default) */
/* arp: P1 speed (1/4 1/8D 1/4T 1/8 1/16D 1/8T 1/16 1/16T 1/32 1/32T 1/64; before project
 * format 21 no dotted ones: project.c migrates), P2 pattern (up, down, bounce (up and down, the
 * ends once), bounce+ (the ends repeated), as played, random), P3 range 1..4 octaves, P4 hold; its page 2 is
 * TP_AP1..AP4 = note length (short medium long), style (off, pedal, thirds, double, skip, swap: seq.c
 * arp_style_pos), glide (off low high), stereo (mono narrow wide). strum: P1 roll 0..63 (63 = a note per sequencer step of the
 * track), P2 pattern (up down up-down random), P3 -, P4 hold. random: P1 note %, P2 pitch %, P3 rhythm %,
 * P4 octave span; page 2 is TP_AP1..AP4 = time %, length, vel %, gate % (seq.c rand_notes) */

/* how a value is shown: a number, a percentage, bipolar (64 = 0), a name from the list, semitones, on / off,
 * milliseconds / seconds, the LFO speed (a division or Hz), 00..99 (the engine knobs),
 * Hz / kHz (cutoff), an LFO fade ("in 50 %" / "off" / "out 50 %"), dB */
enum { PK_NUM, PK_PCT, PK_BIP, PK_NAMES, PK_SEMI, PK_SWITCH, PK_MS, PK_HZ_SYNC, PK_NN, PK_HZ, PK_FADE, PK_DB,
       PK_SAMPLE,                                  /* a sample of the bank: its name (dsp/sample.h smp_name) */
       PK_BIPST,                                   /* semitones about 64: "+3 st" */
       PK_DMS,                                     /* a drum key's time: 0 = 0 ms, else as PK_MS */
       PK_DECAY,                                   /* the same, 127 = "off" */
       PK_LAGMS,                                   /* 0..127 = 0..60 ms */
       PK_FADER,                                   /* a track's level: dB by tp_level_gain, 0 = "off" */
       PK_CENT,                                    /* cents about 64: "+12 ct" */
       PK_LOWCUT };                                /* the low cut: 0 = "off", else 20 Hz .. 2 kHz */
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
/* the slot pages' picture of slot q's dirt or color (dsp/efx.h efx_char_picture: no state): drive, tape and fold the
 * transfer at u (-1..1); crush and sampler a cycle of a sine (u 0..1) held and quantised; ring two cycles of a sine
 * ring-modulated (its peak at 1); tilt the response in dB / 6 from 50 Hz (u 0) to 20 kHz (u 1). The mix is in it; mix:
 * -1 the slot's, else this one (0: the dry curve) */
float slot_picture(const uint8_t *par, int q, int mix, float u);
#define KIT_RING_HZ 261.63f                         /* a ring off the voices (a kit's, a second ring): a ratio of C4 */
/* the Engine page's picture of the organ: the six drawbars' levels (16' 8' 4' 2 2/3' 2' 1 1/3', 0..1, the loudest 1) that
 * stops and pedal set (organ_prep's registration) */
void organ_bars(const uint8_t *par, float bars[6]);
/* jacquard's table t (0..8) at scan 0..1 and phase 0..1, -1..1, as a voice reads it (the engine's picture) */
float wt_value(int t, float scan, float ph);
#define WT_FRAMES 16                               /* (wavetables.h's WT_NFRAME) */
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
/* a preset's sound is its fields (PS_N: par[0..NTP_SOUND), then knit's Osc 2 and Mod pages, rayon's Ops page, knit's
 * analog), kept as its engine's template (the value most of the engine's presets have) and its own differences from it
 * (tools/gen_presets.py: 4.5 KB for the 8 KB the whole fields took, 2026-11-23): at its place in the table, a mask of
 * PS_MASK bytes (bit i: field i differs), then the values of the fields that do */
#define PS_N (NTP_SOUND + 27)
#define PS_MASK ((PS_N + 7) / 8)
typedef struct {
    const char *name;
    uint8_t engine;                               /* EG_* */
    uint8_t role;                                 /* RL_* */
    uint16_t at;                                  /* its differences' place (synth.c PRESET_DATA) */
} preset_t;
extern const preset_t PRESETS[];
extern const int NPRESETS;
int preset_first(int engine);                       /* the first preset of an engine, -1 */
void preset_sound(uint8_t *par, const preset_t *pr);   /* a preset's sound into a track's par[NTP]: par[0..NTP_SOUND)
                                                     * and the sound's fields past it (PS_X) */
extern const uint16_t PS_X[PS_N - NTP_SOUND];       /* the sound's fields past par[0..NTP_SOUND): knit's Osc 2 and Mod
                                                     * pages, rayon's Ops page, knit's analog, slot 2, the low cut, the
                                                     * slots' third knob, mix and extras (TP_O2FINE .. TP_S2X4) */

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
#ifdef PLAT_HOST
#define SYNTH_BYTES 15600             /* (the host's pointers are wider) */
#else
#define SYNTH_BYTES 15528             /* room for the struct (checked at compile time in synth.c: 15,528 B, 2026-11-25) */
#endif

void synth_init(synth_t *s, uint8_t (*par)[NTP]);   /* par: the tracks' parameter arrays, NTRK + 1 of them (the conductor's, SY_GLFO_TRK: the shared LFO) */
void synth_tempo(synth_t *s, float bpm);
/* notes: tag identifies a note for its off (the sequencer uses the step, the keys the key) */
void synth_note_on(synth_t *s, int trk, int note, int vel, int glide_ms, uint32_t tag);
void synth_note_off(synth_t *s, int trk, uint32_t tag);
void synth_all_off(synth_t *s, int trk);            /* -1: every track (panic) */
void synth_cut(synth_t *s);                         /* every voice silent at once, its state cleared (the engine's
                                                     * recovery from a NaN in the mix) */
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
/* a track's character on its bus, split across the cores (synth.c bus_prepare): allowed by synth_split(s, 1) (the engine's;
 * off, a bus runs on one core and a block renders as it always did, in any ranges and frame splits); then each block,
 * after every range is rendered, synth_bus_join(n: the prepare's): the second core's share in and each such bus's
 * stage, gains and sends into the first's l, r, s1, s2. synth_bus_reset: a core's shares cleared (its render failed:
 * rendered again) */
void synth_split(synth_t *s, int on);
/* the first core's own buses' buffers, free from synth_prepare until it renders its voices: synth_kit_scratch(k), five
 * of BUS_N (256) floats each (k 0..4: its pair for a bus on one core, then its pairs for the split buses), for the
 * kits' scratch (engine.c render_drums). With synth_split(s, 2) synth_prepare leaves the first core's split pairs to
 * synth_split_ready(s, n), called once the kits are done, before the first core's voices */
void synth_split_ready(synth_t *s, uint32_t n);
float *synth_kit_scratch(int k);
/* a kit's slots on its pair (engine.c, after its bus filter; the keys' sends taken before): this block's effects, knobs
 * and mixes (synth_prepare); a rumble hears kick, its kicks alone (drumline_render_kick), when given */
void synth_kit_slots(synth_t *s, int trk, float *l, float *r, const float *kick, uint32_t n);
int synth_slots_ring(synth_t *s, int trk, int sounding, uint32_t n);   /* a kit's: 1 while it sounds or a slot rings on
                                                  * after it (rumble), counted in calls of n frames */
int synth_slot_room(const synth_t *s, int trk, int q);   /* 1: slot q has its memory (or needs none) */
void synth_slots_evict(synth_t *s, const efx_mask_t *mask);   /* the slots holding any of these blocks give them up (a send
                                                      * takes them: the engine, before synth_prepare) */
void synth_bus_join(synth_t *s, float *l, float *r, float *s1, float *s2, uint32_t n);
void synth_bus_reset(synth_t *s, int core);
/* the same, saying which core renders (0 the first, 1 the second: the profiler's halves) */
void synth_render_on(synth_t *s, int core, int v0, int v1, float *l, float *r, float *s1, float *s2, uint32_t n);
/* each core's track sink (the USB audio input's track channels, engine.c): while p is set, each voice that core renders
 * also adds its output, mono and before its pan (the bus's L + R it added, over its pan's two gains), times k, into
 * the 16-bit sample of its track: frame f of the render's n at p + f * stride (f < run), else p2 + (f - run) * stride,
 * plus the track. One sink a core, so one writer a sample */
typedef struct { int16_t *p, *p2; uint32_t run, stride; float k; } synth_tap_t;
extern synth_tap_t synth_tap[2];
int synth_order(const synth_t *s, int8_t *ord);   /* the cores' queue (engine.c): the sounding voices by cost, how many
                                                     * (the simulator: all NVOICE in order) */
void synth_idle(synth_t *s, const int8_t *ord, int nord, uint32_t n);   /* the voices not in it moved n frames on */
int synth_sounding(const synth_t *s, int v0, int v1);          /* a voice in the range is sounding */
int synth_voices_used(const synth_t *s);
/* the slice engine's newest playing voice on a track (the picture): its slice (0-based) and the position in
 * the sample; 0 when none plays. Called from the main loop: a racy read of the voices, for the screen only */
int synth_slice_now(const synth_t *s, int trk, int *slice, int32_t *pos);
/* tulle's grains sounding on a track (the picture): each one's place in the sample and its level now (its window times
 * the voice's envelope, 0..255), at most max; how many */
int synth_tulle_grains(const synth_t *s, int trk, int32_t *pos, uint8_t *lvl, int max);
void synth_limit(synth_t *s, int max_voices);                  /* the overload guard: fewer voices */
void synth_acid_lite(synth_t *s, int on);                      /* and before it: the 303s without oversampling */
int synth_lock_value(const synth_t *s, int trk, int param);   /* a lock's value now, -1 none (the UI's dot) */
/* MOD VIEW (pages.c): the modulation inside one track's newest sounding voice, for the screen. The UI names the
 * track (trk; 0xFF none: nothing is written), synth_prepare writes the rest once a block, of the track it was
 * written for (of; none of its voices sounding: the free-running LFO and the shared LFO alone, stages 0). No lock:
 * a torn read draws one odd frame. cut: what the
 * filter envelope and the LFOs add to the cutoff now, in the knob's units; lp / gp: the parameter the
 * track's LFO / the shared LFO moves (-1 none, or not now), by loff / goff now (the knob's units); ast / fst:
 * the amp / filter envelope's stage (0 off, 1 attack, 2 decay, 3 sustain, 4 release), alv / flv their levels 0..1 */
typedef struct {
    float cut, loff, goff, alv, flv;
    int16_t lp, gp;
    uint8_t trk, of, ast, fst;
} synth_mod_t;
extern volatile synth_mod_t synth_mod;
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
