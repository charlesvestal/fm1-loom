/* SPDX-License-Identifier: GPL-3.0-only */
/* LOOM-8's 909 kit models (drumline.h DM_909KICK .. DM_909CLAP): the circuit models of 9W9, a TR-909 model by
 * athousanddetails (github.com/athousanddetails/schwung-9W9, GPL-3.0-only; itself grown out of ER-99 by Matthew
 * Cieplak, GPL-3.0), through X0X's port to the FM-1 (charlesvestal/fm1-x0x, firmware/src/dsp/drum909.c and
 * drum909_dsp.h): the kick, the snare, the toms (low, mid, high on one model), the rim shot and the hand clap,
 * their fitted constants, pot curves and defaults as 9W9 has them. GPL-3.0-only: LOOM-8 built with it is GPL-3.0 as
 * a whole (LICENSING.md).
 *
 * What differs from X0X's port, for LOOM-8's kit: one voice a hit, in a slot of the drumline's 909 bank (drumline.c
 * d9_slot), not one instance a kit; the knobs read at the hit (each model's pots as LOOM-8 knobs, 0..127, below);
 * each voice its own noise (9W9's is one stream for the kit, whose filters it keeps warm between notes: here a hit's
 * filters start from rest); tanh from a table of half X0X's points. The level and the accent are 9W9's defaults (its
 * Level pot's, its kit Accent 2): the velocity, the key's volume and the kit's trim are the drumline's.
 *
 * The knobs (d909_start's k[], 0..1; a list knob as its index / 127), 9W9's pots in LOOM-8's order:
 *   kick   pitch (9W9's Pitch, 0.43 .. 4.7 x), decay (100 .. 4000 ms), attack (the beater's click), sweep (9W9's Tune:
 *          the pitch drop's time, 6 .. 32 ms); depth (the drop), drive (0.85 .. 12), shape (the 7 distortions)
 *   snare  pitch (130 .. 320 Hz), decay (120 .. 800 ms), snappy (the wires), tone (the wires' decay, 300 .. 4000 ms);
 *          drive, shape
 *   tom    pitch, decay (each by its size), attack (the stick), size (low, mid, high); drive, shape
 *   rim    pitch (150 .. 300 Hz), drive, shape
 *   clap   tone (650 .. 1400 Hz), tail (120 .. 1000 ms), drive, shape
 *   hat, open hat, ride, crash   9W9's sampler on ER-99's cymbals (the hat's sample for both hats, as 9W9): pitch
 *          (0.25 .. 4 x the rate), decay (the hat 15 .. 300 ms, the open hat 20 .. 1200, the cymbals 100 .. 3000),
 *          drive, shape. The samples are the resource region's (fm1-base device/res.c: tools/gen_909res.py, 5-bit
 *          block companded), found through d909_res_get; without it they are silent
 * Float, no libm (fastmath.h), no allocation. */
#pragma once
#include <stdint.h>

enum { D9_KICK, D9_SNARE, D9_TOM, D9_RIM, D9_CLAP, D9_CH, D9_OH, D9_RIDE, D9_CRASH, ND9 };
#define D9_IS_SMP(w) ((w) >= D9_CH)           /* a sampler model: its sample from the resource region */
#define D9_F_SHORT 1                  /* a short step: its length at most 60 ms (drumline.c HF_SHORT) */
#define D9_F_LONG 2                   /* long_tail: four times as long */

typedef struct { float v, cur, r, v0, v1, l2r, t1; int32_t k, left; } d9_env_t;   /* Web Audio's ramps */
typedef struct { float b0, b1, b2, a1, a2, x1, x2, y1, y2; } d9_biquad_t;           /* RBJ, direct form 1 */
typedef struct { int32_t type; float k, g, c1, c2; } d9_shape_t;                   /* the 7 distortions */

typedef struct {
    uint8_t which, rsv[3];
    int32_t mute;                     /* samples it still sounds (9W9's countdown) */
    uint32_t rng;                     /* its noise */
    d9_shape_t shape;
    float crush_st[2];
    float out_gain;
    union {
        struct {                      /* kick and snare: 9W9's er99_bt_t, its runtime */
            uint32_t ph, ph2;
            d9_env_t pitch, amp, click_env, noise_env;
            d9_biquad_t click_lp, noise_hpf, noise_lpf, dc_block;
            int32_t noise_hold, noise_gated, amp_hold_left, bd_phold, impulse;
            float decay, attack, snappy, noise_decay, ratio, mix2, inv_mix, bd_df, bd_mult, bd_base;
        } bt;
        struct {                      /* the toms: three VCOs, the stick */
            uint32_t ph[3];
            d9_env_t env[3], pitch, noise_env;
            d9_biquad_t noise_bp, dc_block;
            float noise_level;
        } tom;
        struct {
            d9_biquad_t bp1, bp2, hp;
            d9_env_t amp;
            float noise_mix, level;
            int32_t impulse;
        } rim;
        struct {
            d9_biquad_t bp, hp;
            d9_env_t burst, tail;
            float next_pulse, spread, burst_decay, tail_decay, tail_level;
            int32_t pulse_index;
        } clap;
        struct {                      /* the sampler (9W9's render_sampler): its sample's blocks, read in place */
            const uint8_t *blk;
            uint32_t len, pos, frac, inc, incf;
            d9_env_t out;
            float pre;                /* into the shape: 1 + drive / 2 */
        } smp;
    };
} d909_t;

/* the resource region's bytes (the app's: plat_res), 0 while the FM-1 does not hold them: the samplers are silent */
extern const uint8_t *(*d909_res_get)(uint32_t *len);
/* the sampler models' samples there: 1 */
int d909_res_ok(void);

/* a hit: model which (D9_*), its knobs k[0..6] (0..1), semitones up, the kit's length factor, D9_F_* */
void d909_start(d909_t *v, int which, const float *k, float semi, float lscale, int flags, uint32_t seed);
/* n samples into out[] (written, not added); 0 once it has finished (out zero from there) */
int d909_render(d909_t *v, float *out, int n);
