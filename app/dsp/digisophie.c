/* SPDX-License-Identifier: MIT */
/* digisophie's Sophie core, vendored unchanged from github.com/soejrd/digisophie (sophie.c, commit fee5f51),
 * MIT, Copyright (c) 2026 Matt Estela: a fixed-point adaptation of Sophie (github.com/mestela/schwung-sophie, MIT)
 * for the Digitakt. LOOM-8's clang model (drumline.c) runs it with the kit key's envelope and filter.
 * (Only the include names changed: digisophie.h, digisophie_sin.inc.) */
/* SPDX-License-Identifier: MIT
 *
 * Fixed-point adaptation of Sophie, an original metallic percussion engine
 * by the Schwung Sophie contributors.  The Digitakt port deliberately omits
 * Sophie's drive and filter: its stock AMP and filter stages own those jobs.
 */
#include "digisophie.h"

#define Q15 32767

static int32_t ds_clamp(int32_t x, int32_t lo, int32_t hi)
{ return x < lo ? lo : (x > hi ? hi : x); }
static int32_t ds_mul(int32_t a, int32_t b) { return (a * b) >> 15; }

/* Roughly 1.3 ms slow-control smoothing. At 24 kHz the synth updates these
 * controls every four samples, equivalent to eight 48 kHz output frames. */
static int32_t ds_slew8(int32_t value, int32_t target)
{
    int32_t delta = target - value;
    return value + (delta > 0 ? (delta + 7) / 8 : -((-delta + 7) / 8));
}

static int32_t ds_slew32(int32_t value, int32_t target)
{
    int32_t delta = target - value;
    return value + (delta > 0 ? (delta + 31) / 32 : -((-delta + 31) / 32));
}

/* The fixed-point oscillator uses one cached table read per sample. The
 * previous polynomial required several multiplies for every FM oscillator. */
static const int16_t ds_sine_full[1024] = {
#include "digisophie_sin.inc"
};

static int32_t ds_sin(uint32_t phase)
{
    return ds_sine_full[phase >> 22];
}

static uint32_t ds_phase_from_radians_q15(int32_t radians)
{
    /* radians(Q15) * 65536 / (2*pi), returned in the phase accumulator's
     * high-word units.  The halved constant avoids overflowing int32_t at
     * maximum oscillator feedback. */
    return (uint32_t)((radians * 5215) >> 14) << 16;
}

static uint32_t ds_index_phase(int32_t index_q12, int32_t mod_q15,
                               int32_t factor_q15)
{
    int32_t scaled = ds_mul(mod_q15, factor_q15);
    int32_t n = (scaled >> 4) * index_q12;
    uint32_t a = n < 0 ? 0u - (uint32_t)n : (uint32_t)n;
    /* 163/131072 approximates 1/804 within 0.016%. Splitting n before
     * multiplication keeps the entire conversion in 32-bit registers and
     * avoids a long divide for every oscillator. */
    uint32_t q = (((a >> 9) * 163u) + (((a & 511u) * 163u) >> 9)) >> 8;
    return (uint32_t)(n < 0 ? -(int32_t)q : (int32_t)q) << 16;
}

/* Exact soft-clip points at 1024-unit intervals. Linear interpolation stays
 * within nine 16-bit units of the original curve and avoids two long
 * divisions per sample on ColdFire. */
static const uint16_t ds_clip_table[65] = {
    0, 992, 1927, 2808, 3640, 4428, 5173, 5881, 6553,
    7192, 7801, 8382, 8936, 9466, 9972, 10457, 10922, 11368,
    11796, 12207, 12602, 12983, 13349, 13702, 14043, 14371, 14688,
    14995, 15291, 15578, 15855, 16123, 16383, 16635, 16880, 17117,
    17347, 17570, 17788, 17999, 18204, 18403, 18597, 18786, 18970,
    19149, 19324, 19494, 19660, 19822, 19980, 20134, 20284, 20431,
    20574, 20715, 20851, 20985, 21116, 21244, 21370, 21492, 21612,
    21729, 21844
};

static int32_t ds_soft_clip(int32_t x)
{
    uint32_t a = x < 0 ? 0u - (uint32_t)x : (uint32_t)x;
    int32_t y;
    if (a >= 65536u) {
        /* Defensive fallback outside the oscillator's bounded range. */
        if (a > 131072u) a = 131072u;
        y = (int32_t)(((a >> 1) * Q15) / ((Q15 + a) >> 1));
    } else {
        uint32_t pos = a >> 10;
        int32_t lo = ds_clip_table[pos];
        y = lo + ((ds_clip_table[pos + 1] - lo) * (int32_t)(a & 1023u) >> 10);
    }
    return x < 0 ? -y : y;
}

/* Bipolar exponential pitch contour: roughly +/-4.5 octaves at the
 * transient, converging on the played note.  Linear interpolation inside
 * each octave is monotonic, cheap on ColdFire and close to exp2(). */
static const uint16_t ds_inverse_octave[65] = {
    32768, 32264, 31775, 31301, 30840, 30394, 29959, 29537,
    29127, 28728, 28340, 27962, 27594, 27236, 26887, 26546,
    26214, 25891, 25575, 25267, 24966, 24672, 24385, 24105,
    23831, 23564, 23302, 23046, 22795, 22550, 22310, 22075,
    21845, 21620, 21400, 21183, 20972, 20764, 20560, 20361,
    20165, 19973, 19784, 19600, 19418, 19240, 19065, 18893,
    18725, 18559, 18396, 18236, 18079, 17924, 17772, 17623,
    17476, 17332, 17190, 17050, 16913, 16777, 16644, 16513,
    16384
};

static int32_t ds_swept_inc(uint16_t base, int8_t amount, int32_t env)
{
    int32_t oct_q12 = ((int32_t)amount * env * 9) >> 10;
    int32_t octave, fraction, inc;
    if (oct_q12 >= 0) {
        octave = oct_q12 >> 12;
        fraction = oct_q12 & 4095;
        inc = ((int32_t)base * (4096 + fraction)) >> 12;
        inc <<= octave;
    } else {
        int32_t recip;
        oct_q12 = -oct_q12;
        octave = oct_q12 >> 12;
        fraction = oct_q12 & 4095;
        recip = ds_inverse_octave[fraction >> 6];
        recip += ((ds_inverse_octave[(fraction >> 6) + 1] - recip)
                  * (fraction & 63)) >> 6;
        inc = ((int32_t)base * recip) >> 15;
        inc >>= octave;
    }
    return ds_clamp(inc, 8, 24576);
}

static uint32_t ds_rand(struct ds_voice *v)
{
    uint32_t x = v->rng ? v->rng : 0x534f5048u;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    v->rng = x;
    return x;
}

static int32_t ds_noise(struct ds_voice *v)
{ return (int32_t)((ds_rand(v) >> 16) & 0xffffu) - 32768; }

void ds_voice_init(struct ds_voice *v)
{
    v->carrier = v->mod1 = v->mod2 = v->mod3 = 0;
    v->sub = v->upper = v->split = 0;
    v->amp = v->pitch = v->attack = v->feedback_z = v->noise_z = 0;
    v->rng = 0x534f5048u;
    v->color_s = v->metal_s = v->feedback_s = v->velocity_s = 0;
    v->last = v->tail = 0;
    v->transition = v->model = 0;
    v->sleeping = v->quiet_blocks = v->fade_left = 0;
    v->active = 0;
}

/* Stock AMP owns note-off and duration. Its level passes through zero at the
 * start of an active envelope, so level alone must never put a held note to
 * sleep. Phase zero is the released/idle state. Once that phase is quiet,
 * fade our still-oscillating source before sleeping. */
void ds_voice_gate(struct ds_voice *v, int32_t amp_level, int32_t amp_phase)
{
    uint32_t mag;
    if (!v->active) return;
    mag = amp_level < 0 ? 0u - (uint32_t)amp_level : (uint32_t)amp_level;
    if (amp_phase != 0 || mag > (1u << 19)) {
        v->quiet_blocks = 0;
        if (v->fade_left) {
            v->tail = v->last;
            v->transition = 64;
            v->fade_left = 0;
        }
        if (v->sleeping) {
            v->sleeping = 0;
            v->tail = 0;
            v->transition = 64;
        }
    } else if (v->quiet_blocks < 32) {
        if (++v->quiet_blocks == 32) {
            v->fade_left = 128; /* 256 output frames, about 5.3 ms */
        }
    }
}

void ds_voice_render(struct ds_voice *v, const struct ds_params *p,
                     int trigger, int32_t *out, uint32_t n)
{
    uint32_t i;
    uint8_t model = p->model & 3u;
    int32_t color = 0, metal = 0, feedback = 0;
    int32_t ratio = 0, index = 0, inc = 0;
    int32_t color_target = ds_u7_q15(p->color);
    int32_t metal_target = ds_u7_q15(p->metal);
    int32_t feedback_target = ds_u7_q15(p->feedback);
    int32_t velocity_target = ds_u7_q15(p->velocity);
    if (trigger || (v->active && v->model != model)) {
        if (!v->active) {
            v->color_s = color_target; v->metal_s = metal_target;
            v->feedback_s = feedback_target; v->velocity_s = velocity_target;
        }
        v->tail = v->last;
        v->transition = 64;
        v->model = model;
        v->carrier = v->mod1 = v->mod2 = v->mod3 = 0;
        v->sub = v->upper = v->split = 0;
        v->amp = Q15;
        v->pitch = Q15;
        v->attack = 0;
        v->feedback_z = v->noise_z = 0;
        v->rng ^= 0x9e3779b9u + ((uint32_t)p->velocity << 16);
        v->sleeping = v->quiet_blocks = v->fade_left = 0;
        v->active = 1;
    }
    /* A two-operator core at 24 kHz. The models share one inharmonic modulator
     * and one pitched carrier; only BOOM reads the carrier twice so its clean
     * low-frequency body survives an aggressively FM'd attack. */
    for (i = 0; i < n; i += 2) {
        int32_t mod, raw, fb, phase_mod;
        int32_t y;
        uint32_t control_tick = (i & 7u) == 0;
        if (!v->active) {
            out[i] = 0;
            if (i + 1 < n) out[i + 1] = 0;
            continue;
        }
        if (control_tick) {
            color = v->color_s = ds_slew8(v->color_s, color_target);
            metal = v->metal_s = ds_slew8(v->metal_s, metal_target);
            v->feedback_s = ds_slew8(v->feedback_s, feedback_target);
            v->velocity_s = ds_slew8(v->velocity_s, velocity_target);
            feedback = ds_mul(v->feedback_s, v->feedback_s);
            /* COLOR visits different inharmonic bands for each topology. */
            switch (model) {
            default: ratio = 1925 + ((ds_mul(color, color) * 31130) >> 15); break;
            case 1: ratio = 2457 + ((color * 17203) >> 15); break;
            case 2: ratio = 5366 + ((color * 24576) >> 15); break;
            case 3: ratio = 1925 + ((color * 32767) >> 15); break;
            }
        }
        /* About 28 ms at the equivalent 48 kHz output rate.
         * The former coefficient collapsed in under 1 ms, making SWEEP
         * audible mostly as a click. */
        v->pitch -= (v->pitch * 48 + 32767) >> 15;
        /* A separate ~135 ms brightness contour lets the high end open,
         * scrape and settle after the faster pitch snap. AMP still owns the
         * actual note duration. */
        v->amp -= (v->amp * 10 + 32767) >> 15;
        if (v->attack < Q15) v->attack = ds_slew32(v->attack, Q15);
        /* Ease into feedback and damp its one-sample loop below. This keeps
         * the useful metallic range from abruptly becoming Nyquist chatter. */
        /* Four synth samples share one pitch step. Phase stays continuous,
         * while a moving SWEEP no longer repeats its lookup every sample. */
        if (control_tick)
            inc = p->sweep ? ds_swept_inc(p->phase_inc, p->sweep, v->pitch)
                           : p->phase_inc;
        v->carrier += (uint32_t)inc << 17;
        v->mod1 += (uint32_t)((inc * ratio) >> 11) << 16;
        /* Diet: with no feedback the loop contributes exactly 0
         * (ds_mul(z, 0) == 0), so skip the modulator read, the radian
         * conversion and the smoother update. Bit-exact while FBK stays
         * 0; raising FBK mid-note reconverges from the frozen smoother
         * within samples (inaudible). */
        if (feedback) {
            fb = ds_mul(v->feedback_z, feedback);
            fb = (fb * (8192 + ((metal * 40960) >> 15))) >> 12;
            mod = ds_sin(v->mod1 + ds_phase_from_radians_q15(fb));
        } else {
            mod = ds_sin(v->mod1);
        }
        /* The pitch contour moves slowly relative to four synth samples.
         * Hold FM depth/ceiling across the same eight output frames as the
         * other controls; this avoids repeated heavy multiply/divide work. */
        if (control_tick) {
            int32_t shape = 8192 + 3 * (8192 + ds_mul(v->amp, 24576));
            int32_t metal2 = ds_mul(metal, metal);
            int32_t rate = (inc * ratio) >> 12;
            int32_t bandwidth, ceiling;
            index = ((metal2 >> 2) * shape) >> 13; /* Q12 radians */
            rate = ((rate >> 4) * (4096 + ((metal * 10240) >> 15))) >> 8;
            if (rate < 1) rate = 1;
            bandwidth = 16384 - inc;
            if (bandwidth < 256) bandwidth = 256;
            if (index <= 4096 && rate <= 4096 &&
                index * rate <= bandwidth * 4096) {
                /* The common low/mid-pitch case needs no costly divide. */
            } else {
                ceiling = (bandwidth * 4096) / rate;
                if (index > ceiling) index = ceiling;
            }
        }
        phase_mod = ds_index_phase(index, mod, Q15);

        switch (model) {
        default: /* FUSE: a bright clang that settles as FM depth recedes. */
            raw = ds_sin(v->carrier + phase_mod);
            break;
        case 1: { /* BOOM: round sine body, with a decaying FM impact skin. */
            int32_t body = ds_sin(v->carrier);
            int32_t edge = ds_sin(v->carrier + phase_mod);
            int32_t edge_mix = 2048 + ds_mul(metal, v->amp) / 2;
            raw = ds_mul(body, Q15 - edge_mix) + ds_mul(edge, edge_mix);
            break;
        }
        case 2: { /* PIPE: cross-modulated sine through an inharmonic ring. */
            int32_t body = ds_sin(v->carrier + phase_mod);
            int32_t ring = ds_mul(body, mod);
            int32_t ring_mix = 10240 + (metal >> 1) + (v->amp >> 3);
            /* A little ring remains at METAL 0: COLOR tunes the partial
             * and FBK bends it, rather than both controls going dead. */
            raw = ds_mul(body, Q15 - ring_mix) + ds_mul(ring, ring_mix);
            raw += raw >> 2; /* compensate the ring's lower average level */
            break;
        }
        case 3: /* SHARD: quantized tone and a short, scratchy noise bloom. */
            raw = ds_sin((v->carrier & 0xf8000000u) + phase_mod);
            /* Diet: at METAL 0 the bloom term is exactly 0, so skip the
             * LPF and the LFSR advance. Bit-exact while METAL stays 0;
             * raising it restarts from frozen state (white restarts
             * white — statistically identical, inaudible). */
            if (metal) {
                v->noise_z += ds_mul(ds_noise(v) - v->noise_z, 655 + ds_mul(color, 11469));
                raw = ds_mul(raw, 27852 - (metal >> 2))
                    + ds_mul(v->noise_z, ds_mul(ds_mul(metal, v->amp), 13762));
            } else {
                raw = ds_mul(raw, 27852);
            }
            break;
        }
        /* Frozen while feedback is 0 (see above); resumes on raise. */
        if (feedback)
            v->feedback_z += (ds_soft_clip(raw + ((raw * 13) >> 5)) - v->feedback_z) / 8;
        if (v->attack < Q15) raw = ds_mul(raw, v->attack);

        y = ds_mul(raw, v->velocity_s);
        /* Fixed output conditioning, not an extra drive control: a full-scale
         * sine reaches 0.25 FS; hotter model sums saturate smoothly. */
        y = ds_soft_clip(y) / 2;
        if (v->transition) {
            y = (y * (64 - v->transition) + v->tail * v->transition) / 64;
            v->transition = v->transition > 2 ? v->transition - 2 : 0;
        }
        if (v->fade_left) {
            y = (y * v->fade_left) / 128;
            if (--v->fade_left == 0) {
                v->sleeping = 1;
                v->tail = 0;
                v->transition = 0;
            }
        }
        out[i] = ((v->last + y) / 2) * 65536;
        if (i + 1 < n) out[i + 1] = y * 65536;
        v->last = v->sleeping ? 0 : y;
    }
}

/* Fold the 16.16 source after synthesis, before the stock AMP/filter path.
 * The triangular wrap needs no division or lookup.  Zero is a bit-exact
 * bypass, including for old sounds whose BR slot was left at zero. */
void ds_fold_block(int32_t *out, uint32_t n, uint8_t amount)
{
    uint32_t i;
    uint32_t index, fraction;
    int32_t drive, level;
    /* Measured against sustained FUSE, BOOM, PIPE and SHARD output.  The
     * inverse gain eases back up above 64 because repeated folding then
     * reduces RMS.  Interpolate once per block, not once per sample. */
    static const uint16_t level_q8[17] = {
        256, 171, 128, 102, 85, 73, 64, 61, 59,
        63, 67, 72, 77, 82, 87, 91, 95
    };
    if (!amount) return;
    drive = 256 + (int32_t)amount * 16; /* 1.0625x .. 8.9375x */
    index = amount >> 3;
    fraction = amount & 7u;
    level = ((int32_t)level_q8[index] * (8 - (int32_t)fraction)
           + (int32_t)level_q8[index + 1] * (int32_t)fraction + 4) >> 3;
    for (i = 0; i < n; ++i) {
        /* Shifts, not divides: |x| <= 32768 and drive <= 2288, so
         * |x * drive| <= 75M < 2^31; |folded * level| <= 8.4M. Exact on
         * multiples, +-1 LSB elsewhere (inherent, inaudible). */
        int32_t x = out[i] >> 16;
        int32_t driven = (x * drive) >> 8;
        uint32_t phase = ((uint32_t)(driven + 32768)) & 0x1ffffu;
        int32_t folded = (int32_t)(phase < 65536u ? phase : 131071u - phase) - 32768;
        out[i] = ((folded * level) >> 8) * 65536;
    }
}
