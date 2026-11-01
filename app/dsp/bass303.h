/* SPDX-License-Identifier: GPL-3.0-only */
/* LOOM-8's acid engine (synth.h EG_ACID), X0X's TB-303 bass engine (charlesvestal/fm1-x0x
 * firmware/src/dsp/bass303.c) as it is there, but for the parameter table (LOOM-8's knobs are synth.c's
 * tp_info) and the pitch multiplier (bass303_pitch: LOOM-8's bend, octave and LFO on the pitch). Copyright (C)
 * 2026 Charles Vestal. GPL-3.0-only, for jc303's Devilfish ranges and dm-Rat's RAT model in it: LOOM-8 built with
 * it is GPL-3.0 as a whole (the MPL-2.0 files stay MPL-2.0 each, MPL-2.0 section 3.3).
 *
 * X0X's notes: a C99 / float / no-libm port of Open303 (Robin Schmidt, MIT) with
 * the Devilfish ranges and the Soft / RAT drive of schwung-303 (GPL-3.0; the RAT model is
 * from davemollen/dm-Rat, GPL-3.0).
 *
 * What is the same as schwung-303's engine: the signal path and every constant of Open303's
 * getSample() - pitch slew, the decay envelope with its two RC followers, the measured
 * env-mod mapping, the TB_303-mode TeeBeeFilter (mystran & kunn coefficients) with its
 * feedback highpass, the amp envelope, the 200 Hz de-clicker, and the allpass / highpass /
 * notch after the filter - and the pot-to-physical ranges of plugin.cpp.
 *
 * What is different, and why:
 *  - Oscillator: Open303's MipMappedWaveTable (2 x 12 x 2052 doubles, FFT-built) does not fit.
 *    Saw and the "303 square" are computed directly: the saw is polyBLEP; the square is the
 *    same transistor-shaper function Open303 tabulates, -tanh(F*(2p-1) + O), evaluated per
 *    sample (its falling edge is a smooth tanh ramp), with a polyBLEP on its one hard edge
 *    and its DC removed (the mip-map drops DC above 43 Hz).
 *  - Oversampling: BASS303_OS (1, 2 or 4; Open303 uses 4). The decimator is a polyphase IIR
 *    halfband (one stage per factor of 2) instead of Open303's 12th-order elliptic run at
 *    every oversample: 6 (OS 2) or 12 (OS 4) multiplies per output sample instead of ~100.
 *  - Control rate: envelopes, slew, cutoff and the filter's coefficients every BASS303_CTRL
 *    samples (the recursions advanced exactly, the values interpolated linearly between);
 *    Open303 computes them every sample. A trigger still jumps pitch and amp.
 *  - Idle: when the gate is off and the amp envelope has died (< -120 dB) for
 *    BASS303_IDLE_HOLD samples, render writes zeros and only advances the oscillator phase
 *    and the envelopes (closed form, once per block). Filter states are cleared on the way
 *    in, as Open303's own triggerNote() does after idle (schwung-303 never idles).
 *  - Only the four Devilfish controls that fit the panel are exposed (slide time, accent
 *    decay); the others stay at the stock values plugin.cpp uses with Devilfish off.
 *
 * Instances run side by side: their one shared state is bass303_fixed_t, the sample rate's coefficients, written
 * once by the first bass303_init (Optimist's; a voice's own coefficients stay in bass303_t). */
#pragma once
#include <stdint.h>

#ifndef BASS303_OS
#define BASS303_OS 2                    /* oversampling factor: 1, 2 or 4 */
#endif
#if BASS303_OS != 1 && BASS303_OS != 2 && BASS303_OS != 4
#error "BASS303_OS must be 1, 2 or 4"
#endif

/* Idle hold-off, in samples of silence before the engine stops computing. Open303 never
 * stops: its oscillator and filters keep running under the closed amp, so a note after a rest
 * starts into warm filter states. Starting into cleared ones makes the first 5 ms of the note
 * 4-5 dB quieter than the reference (measured; every filter in the chain contributes, so no
 * cheap state initialisation or short pre-roll fixes it). Holding off keeps notes within a phrase exactly as
 * Open303 plays them; only a note after a longer silence starts cold. It costs nothing in
 * the worst case, which is a note sounding continuously. 0 = idle as soon as it is silent. */
#ifndef BASS303_IDLE_HOLD
#define BASS303_IDLE_HOLD 22050         /* 500 ms */
#endif
/* Control rate: the envelopes, slew, cutoff and filter coefficients are computed every
 * BASS303_CTRL samples (the envelope recursions advanced in exact closed form) and the
 * coefficients, increment and amp interpolated linearly in between. 1 = every sample, as
 * Open303. tests/host/run_bass303.sh measures 4 against 1. */
#ifndef BASS303_CTRL
#define BASS303_CTRL 4
#endif
#if BASS303_CTRL < 1 || BASS303_CTRL > 4
#error "BASS303_CTRL must be 1..4"
#endif
/* bass303_set_lite (the overload guard). 0: compiled out, the render always oversamples (Optimist's ACID engine never
 * sets it); LOOM-8 uses it */
#ifndef BASS303_LITE
#define BASS303_LITE 1
#endif
#define BASS303_SR 44100.0f
#define BASS303_MAX_BLOCK 256

/* parameter indices (pages of four: the UI shows KNOB 1-4) */
enum {
    BASS303_CUTOFF, BASS303_RESO, BASS303_ENVMOD, BASS303_DECAY,     /* page 1 */
    BASS303_ACCENT, BASS303_WAVE, BASS303_TUNE, BASS303_VOLUME,      /* page 2 */
    BASS303_DRIVE, BASS303_DRVTYPE, BASS303_SLIDE, BASS303_ACCDEC,   /* page 3 */
    /* LOOM-8: the other Devilfish controls jc303 has (its ranges), each at the stock 303's value by default */
    BASS303_SOFTATK,         /* soft attack: the filter envelope's rise on a plain note, 0.3 ms .. 3 s (3 ms) */
    BASS303_AMPDEC,          /* the amp's decay while the note is held, 30 .. 3000 ms (1230 ms) */
    BASS303_FBHPF,           /* the filter's feedback high-pass, 350 .. 100 Hz (150 Hz): the resonance's low end */
    BASS303_SHAPER,          /* the square's tanh shaper, 25 .. 80 dB (36.9 dB): how square the square is */
    BASS303_NPARAMS
};
enum { BASS303_DRV_OFF, BASS303_DRV_SOFT, BASS303_DRV_RAT };

typedef struct { float b0, b1, b2, a1, a2; } bass303_bq_t;             /* TDF-II biquad coefficients */
typedef struct { float z1, z2; } bass303_bqz_t;                         /* its state */
typedef struct { float x1, x2, y1, y2; } bass303_df1_t;                /* DF-I state */
typedef struct { float x1, y1; } bass303_ap_t;                         /* halfband allpass */

typedef struct bass303 {
    uint8_t pot[BASS303_NPARAMS];

    /* derived from the pots (bass303_set) */
    float cutoff;            /* nominal cutoff, Hz */
    float reso;              /* resonanceSkewed */
    float env_scaler, env_offset;
    float accent;            /* 0..1 */
    float tuning;            /* A4, Hz */
    float amp_scaler;        /* volume, linear */
    float decay_c, accdec_c; /* main-envelope multipliers for normal / accented notes */
    float slew_c;            /* pitch slew-limiter coefficient */
    int wave;                /* 0 saw, 1 square */
    float pmul;              /* LOOM-8: the oscillator's frequency times this (bass303_pitch), 1 = as the note */
    int drv_type;
    float drv_amt;

    /* the coefficients LOOM-8's Devilfish knobs set (the others every voice shares: bass303_fixed_t) */
    float rc1_c;                        /* the filter envelope's first RC follower (3 ms stock; BASS303_SOFTATK) */
    float fbhp_b0, fbhp_a1;             /* feedback highpass at the oversampled rate (BASS303_FBHPF) */
    float fbhpl_b0, fbhpl_a1;           /* the same at the base rate (lite) */
    int lite, lite_cur;      /* X0X overload guard: render without oversampling (bass303_set_lite) */
    float sq_dc, sq_h;                  /* 303 square: mean, hard-edge height */
    float sq_f;                         /* and its shaper's factor (dB2amp(36.9) stock; BASS303_SHAPER) */

    /* voice */
    int gate;                /* a note is held */
    int idle;                /* nothing sounding: render writes zeros */
    int started;             /* a note has been played since init (oscillator runs) */
    int quiet;               /* samples of silence so far (idle hold-off) */
    int snap;                /* next control step starts without interpolation */
    int trig;                /* next control step follows a trigger: pitch and amp jump */

    /* control rate: c^m for m = 0..BASS303_CTRL of each recursion, the RC followers'
     * closed-form gains, and the interpolated values the samples use */
    float rc1_pw[BASS303_CTRL + 1], slew_pw[BASS303_CTRL + 1], ampd_pw[BASS303_CTRL + 1];
    float env_pw[BASS303_CTRL + 1], g1[BASS303_CTRL + 1], g2[BASS303_CTRL + 1];
    float c_inc, c_b0, c_k, c_g2, c_a;
    float osc_freq, slew_y;
    float phase;             /* oscillator phase in turns, 0..1 */
    float env_c, env_y;      /* main (filter) decay envelope */
    float rc1_y, rc2_y;
    float accent_gain;
    float amp_y;             /* amp envelope */
    int amp_acc;             /* the release is the accented one */
    int amp_trig;            /* next sample is the attack */
    bass303_df1_t dc;
    float hp1_x1, hp1_y1;
    float f_y1, f_y2, f_y3, f_y4, fb_x1, fb_y1;   /* TeeBeeFilter */
#if BASS303_OS >= 2
    bass303_ap_t hb1[6];                 /* 2x -> 1x halfband */
#endif
#if BASS303_OS == 4
    bass303_ap_t hb2[3];                 /* 4x -> 2x halfband */
#endif
    float ap_x1, ap_y1, hp2_x1, hp2_y1;
    bass303_df1_t nt;

    /* drive (schwung-303 drive.h) */
    bass303_bqz_t s_pre, s_post, up_lp, down_lp;   /* (their coefficients: bass303_fixed_t) */
    float s_gain, s_inv;
    float r_b[4], r_a[4], r_z[3];        /* RAT op-amp, 3rd-order TDF-II */
    float r_corr_b1, r_corr_z, r_tone_z;
    float dcb_x1, dcb_y1;
} bass303_t;

/* The coefficients that depend on the sample rate only, the same for every voice: one table (bass303.c bass303_k,
 * after Optimist a64ca03b), filled by the first bass303_init (later ones would write the same values) */
typedef struct {
    float rc2_c;                        /* the filter envelope's second RC follower (15 ms) */
    float rc2_pw[BASS303_CTRL + 1], ampn_pw[BASS303_CTRL + 1], ampa_pw[BASS303_CTRL + 1];
    float dc_b0, dc_b1, dc_a1, dc_a2;   /* de-clicker: 200 Hz Butterworth lowpass, b2 = b0 */
    float hp1_b0, hp1_a1;               /* pre-filter highpass at the oversampled rate */
    float hp1l_b0, hp1l_a1;             /* the same at the base rate (lite) */
    float ap_b0;                        /* 14 Hz allpass (b1 = 1, a1 = -b0) */
    float hp2_b0, hp2_a1;               /* 24 Hz highpass */
    float nt_b0, nt_b1, nt_a1, nt_a2;   /* 7.5 Hz notch (b2 = b0) */
    bass303_bq_t s_pre, s_post, up_lp, down_lp;   /* drive: Soft shelves, the 2x resampler's lowpasses */
    float r_tone_b1;                    /* RAT tone one-pole */
    int ready;
} bass303_fixed_t;

void bass303_init(bass303_t *b);
void bass303_note_on(bass303_t *b, int note, int accent, int slide);
void bass303_note_off(bass303_t *b);
void bass303_all_off(bass303_t *b);
void bass303_render(bass303_t *b, float *out, int n);
/* the engine's overload guard: 1 = no oversampling from the next block (~12 % of the worst case
 * on the FM-1; some aliasing on bright, resonant notes), 0 = back to BASS303_OS */
void bass303_set_lite(bass303_t *b, int on);
/* LOOM-8: the frequency, slid or not, times mul from the next sample (cents of bend, octave, an LFO: 2^(c / 1200)) */
void bass303_pitch(bass303_t *b, float mul);
int bass303_max(int i);                          /* a parameter's top value: 127, or a switch's last */
void bass303_set(bass303_t *b, int i, int value);
int bass303_get(const bass303_t *b, int i);
/* LOOM-8: the cutoff pot (0..127) in Hz: its top half the stock 303's (874 Hz at 64 .. 2394 Hz), its bottom half
 * stretched down to 60 Hz (the stock 303 stops at 314 Hz): the dark end the other engines' filters have */
float bass303_cutoff_hz(int v);
