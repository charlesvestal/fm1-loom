/* SPDX-License-Identifier: MPL-2.0 */
/* LOOM-8 effects (fx.h): the send slots and the master section.
 *
 * Send slots: each takes a mono send and adds a stereo return; the effects themselves (chorus, delay, lofi, phaser,
 * the reverb in its modes) are dsp/efx.c's, the catalog's. Here: a slot's effect and lines, its knobs, its filter
 * page, the glide of its numbers.
 *
 * Knob values are turned into the effect's numbers in fx_set (main loop) and glide toward them in
 * the interrupt a block at a time (~10 ms): no zipper noise on a turn. A change of effect clears
 * the states at the next block; fx_set zeroes the new effect's lines itself, which is safe because
 * the running effect never uses another's line (the lofi borrows the chorus's, which the chorus
 * cannot be using at the same time). The slot's filter page (high-pass, low-pass) runs on the mono
 * send before the effect: one filter instead of two, and the same result for the linear effects.
 *
 * Master: the two groups' levels, a 3-band EQ (RBJ shelves and a peak; all three at 64 a true bypass), X0X's bus compressor behind its one-knob macro with X0X's PUMP in its gain
 * (a hit of the pump's source ducks the whole mix: the sequencer keys it, so it needs no detector and a
 * muted track keys it too; at 0 the stage is as before, sample for sample), the master level and X0X's
 * lookahead limiter.
 *
 * Everything here runs in the audio interrupt; fx_set / master_set write plain words from the main
 * loop. Float, fastmath.h; stack locals stay small (a 32-sample block). */
#include "fx.h"
#include "fastmath.h"

#define SR 44100.0f
#define SUB 32                                /* the block: parameter glides step here */
#define GLIDE 0.07f                           /* a block's share of the distance: ~10 ms to settle */
#define CHO_N EFX_CHO_N                       /* the chorus line: 46 ms (int16; the lofi's spread reads it too) */

const char *const FXT_NAME[NFXT] = {"chorus", "delay", "lofi", "phaser", "reverb"};
static const uint8_t FXT_EF[NFXT] = {EF_CHORUS, EF_DELAY, EF_LOFI, EF_PHASER, EF_REVERB};   /* (the catalog's numbers) */
static const char *const FXT_KNOB[NFXT][4] = {
    {"rate", "depth", "feedback", "width"}, {"time", "fine", "repeats", "tone"},
    {"rate", "bits", "smooth", "width"},    {"freq", "depth", "rate", "feedback"},
    {"size", "motion", "tone", "damp"},
};
static const char *const SPU_KNOB[4] = {"size", "decay", "tone", "damp"};
/* the defaults, 0..127 (what each gives: fx_derive's mappings) */
const uint8_t FXT_DEFAULT[NFXT][4] = {
    {50, 86, 20, 127},                        /* chorus      0.33 Hz, +-5.5 ms, feedback 0.11, fully wide */
    {83, 64, 56, 64},                         /* delay       1/8, on the beat, feedback 0.42 (~4 repeats), 4 kHz */
    {80, 80, 64, 40},                         /* lofi        8.4 kHz, 10.8 bits, half smoothed, 3.4 ms spread */
    {50, 90, 50, 80},                         /* phaser      466 Hz, +-2.1 octaves, 0.27 Hz, feedback 0.57 */
    {100, 40, 44, 72},                        /* plate       combs x1.43, 2.4 samples of wander, 7.4 kHz, damp 0.45 */
};
const uint8_t RV_SPU_DEFAULT[4] = {127, 64, 64, 0};   /* the console's sizes, its decay, flat, its own filter */
const uint8_t RV_LUSH_DEFAULT[4] = {64, 64, 56, 48};  /* (fx.h) */
const uint8_t RV_SHIMMER_DEFAULT[4] = {64, 84, 72, 72};

const char *fx_pick_name(int pick)
{
    int t = fx_pick_type(pick);
    return t == FXT_REVERB ? RV_NAME[fx_pick_mode(pick)] : FXT_NAME[t];
}
const char *fx_knob_name(int pick, int k)
{
    int t = fx_pick_type(pick);
    k &= 3;
    if (t == FXT_REVERB && fx_pick_mode(pick) == RV_SHIMMER && k == 3)
        return "shimmer";                     /* (its fourth knob: the octaves fed back; its damping fixed) */
    return t == FXT_REVERB && fx_pick_mode(pick) != RV_PLATE ? SPU_KNOB[k] : FXT_KNOB[t][k];
}

struct fxslot {
    int16_t cho[CHO_N];
    efx_send_t e;                             /* the effect's numbers and states, its lines (efx.h) */
    uint8_t type, type_t, hpv, lpv;
    uint8_t mode, mode_t;                     /* the reverb's mode: running, and as set */
    uint8_t knob[4];
    uint8_t dwob, dsmr;                       /* the delay's WOBBLE and SMEAR, 0..127 (fx_delay_mod) */
    float bpm;
    float hpc, lpc, hps, lps;                 /* the filter page */
};
_Static_assert(sizeof(struct fxslot) <= FXSLOT_BYTES, "fxslot_t does not fit FXSLOT_BYTES");

static inline float onepole_k(float hz) { return hz >= 20000.0f ? 1.0f : 1.0f - fm_expf(-FM_TWO_PI * hz / SR); }
static inline float k127(int v) { return (float)(v < 0 ? 0 : v > 127 ? 127 : v) * (1.0f / 127.0f); }

static void zero16(int16_t *b, int32_t n)
{
    int32_t i;
    for (i = 0; i < n; i++)
        b[i] = 0;
}

static void fx_states(fxslot_t *f)              /* a fresh effect: its states at rest */
{
    zero16(f->cho, CHO_N);
    efx_send_reset(&f->e, f->mode);
}

int fx_delay_halves(int time, int fine, float bpm)
{
    int h;
    efx_delay_secs(time, fine, bpm, &h);
    return h;
}

/* the knobs to the effect's numbers */
static void fx_derive(fxslot_t *f)
{
    efx_send_nums(&f->e, FXT_EF[f->type_t], f->mode_t, f->knob, f->bpm, f->dwob, f->dsmr);
}

void fx_init(fxslot_t *f, int16_t *dly_l, int16_t *dly_r, int16_t *rv)
{
    uint32_t i;
    uint8_t *z = (uint8_t *)f;
    for (i = 0; i < sizeof *f; i++)
        z[i] = 0;
    f->e.cho = f->cho;
    f->e.delay.l = dly_l;
    f->e.delay.r = dly_r;
    f->e.reverb.line = rv;
    f->bpm = 120.0f;
    f->lpv = 127;
    f->hpc = onepole_k(20.0f);
    f->lpc = onepole_k(20000.0f);
    f->e.delay.wa = 1.0f;                     /* (full width: the bounce hard left and right) */
    if (dly_l) zero16(dly_l, FX_DLY_LEN);
    if (dly_r) zero16(dly_r, FX_DLY_LEN);
    if (rv) zero16(rv, FX_RV_LEN);
    for (i = 0; i < 4; i++)
        f->knob[i] = FXT_DEFAULT[FXT_CHORUS][i];
    f->type = f->type_t = FXT_CHORUS;
    f->mode = f->mode_t = RV_PLATE;
    fx_derive(f);
    fx_states(f);
}

void fx_clear(fxslot_t *f)
{
    if (f->e.delay.l) zero16(f->e.delay.l, FX_DLY_LEN);
    if (f->e.delay.r) zero16(f->e.delay.r, FX_DLY_LEN);
    if (f->e.reverb.line) zero16(f->e.reverb.line, FX_RV_LEN);
    f->hps = f->lps = 0.0f;
    fx_states(f);
}

int fx_has(const fxslot_t *f, int pick)
{
    int type = fx_pick_type(pick);
    if (pick < 0 || pick >= NFXPICK)
        return 0;
    if (type == FXT_REVERB)
        return f->e.reverb.line != 0;
    if (type == FXT_DELAY)
        return f->e.delay.l != 0 && f->e.delay.r != 0;
    return 1;
}

void fx_set(fxslot_t *f, int pick, const uint8_t knob[4])
{
    int i, type, mode;
    if (!fx_has(f, pick))
        pick = FXT_CHORUS;
    type = fx_pick_type(pick);
    mode = fx_pick_mode(pick);
    for (i = 0; i < 4; i++)
        f->knob[i] = knob[i] > 127 ? 127 : knob[i];
    if (type != f->type_t) {                  /* the lines the new effect reads are idle now: clear them */
        if (type == FXT_DELAY) {
            zero16(f->e.delay.l, FX_DLY_LEN);
            zero16(f->e.delay.r, FX_DLY_LEN);
        } else if (type == FXT_REVERB) {
            zero16(f->e.reverb.line, FX_RV_LEN);
        }
    }
    /* another reverb mode while the reverb runs: its line is in use, so the interrupt clears it
     * (fx_states: spu_reset, a slice a block) when it takes the new mode at its next block */
    f->mode_t = (uint8_t)mode;
    f->type_t = (uint8_t)type;
    fx_derive(f);
}

void fx_tempo(fxslot_t *f, float bpm)
{
    if (bpm > 20.0f && bpm != f->bpm) {
        f->bpm = bpm;
        if (f->type_t == FXT_DELAY) {
            int h;
            f->e.pt[0] = efx_delay_secs(f->knob[0], f->knob[1], f->bpm, &h) * SR;
        }
    }
}

/* PING WIDTH: the echoes' mid kept, their sides times w (0 centred .. 127 hard left and right), at the same power: an
 * echo on one side (e) gives e (1 + w) / 2 and e (1 - w) / 2, power e^2 (1 + w^2) / 2, so both times sqrt(2 / (1 + w^2)) */
void fx_width(fxslot_t *f, int w)
{
    float x = k127(w), g = fm_sqrtf(2.0f / (1.0f + x * x));
    f->e.delay.wa = w >= 127 ? 1.0f : 0.5f * g * (1.0f + x);
    f->e.delay.wb = w >= 127 ? 0.0f : 0.5f * g * (1.0f - x);
}

/* the delay's WOBBLE (a slow drift of its time, a tape's wow: each repeat drifts again, so they detune as they go)
 * and SMEAR (two allpasses a side in the loop: each repeat blurs a little more, the echoes become a wash), 0..127 */
void fx_delay_mod(fxslot_t *f, int wobble, int smear)
{
    f->dwob = (uint8_t)(wobble < 0 ? 0 : wobble > 127 ? 127 : wobble);
    f->dsmr = (uint8_t)(smear < 0 ? 0 : smear > 127 ? 127 : smear);
    if (f->type_t == FXT_DELAY)
        fx_derive(f);
}

void fx_filter(fxslot_t *f, int hp, int lp)
{
    f->hpv = (uint8_t)(hp < 0 ? 0 : hp > 127 ? 127 : hp);
    f->lpv = (uint8_t)(lp < 0 ? 0 : lp > 127 ? 127 : lp);
    f->hpc = onepole_k(20.0f * fm_exp2f(k127(hp) * 8.64f));            /* 20 Hz .. 8 kHz */
    f->lpc = onepole_k(20000.0f * fm_exp2f(-(1.0f - k127(lp)) * 8.0f)); /* 78 Hz .. 20 kHz */
}

int fx_lags(const fxslot_t *f) { return f->type_t == FXT_CHORUS || f->type_t == FXT_DELAY || f->type_t == FXT_REVERB; }

void fx_process(fxslot_t *f, const float *in, float *out_l, float *out_r, uint32_t n)
{
    fx_process_lag(f, in, out_l, out_r, n, 0);
}
void fx_process_lag(fxslot_t *f, const float *in, float *out_l, float *out_r, uint32_t n, int lag)
{
    uint32_t o;
    if (f->type_t != f->type || f->mode_t != f->mode) {   /* a new effect or mode: start it at rest */
        f->type = f->type_t;
        f->mode = f->mode_t;
        fx_states(f);
    }
    for (o = 0; o < n; o += SUB) {
        float x[SUB];
        int m = (int)(n - o < SUB ? n - o : SUB), i, any = 0;
        const int hp_on = f->hpv > 0, lp_on = f->lpv < 127;
        float hps = f->hps, lps = f->lps;
        const float hpc = f->hpc, lpc = f->lpc;
        for (i = 0; i < 6; i++)
            f->e.pc[i] += (f->e.pt[i] - f->e.pc[i]) * GLIDE;
        for (i = 0; i < m; i++) {
            float v = in[o + i];
            any |= v != 0.0f;
            if (hp_on) {
                hps += hpc * (v - hps);
                v -= hps;
            }
            if (lp_on) {
                lps += lpc * (v - lps);
                v = lps;
            }
            x[i] = v;
        }
        f->hps = fm_flush(hps);
        f->lps = fm_flush(lps);
        efx_send_run(&f->e, FXT_EF[f->type], x, out_l + o, out_r + o, m, any, lag);
    }
}

int fx_awake(const fxslot_t *f) { return efx_send_awake(&f->e, FXT_EF[f->type]); }

/* ================================================================ master === */
/* the master's knob names, short enough for the knob strip: percussion, melodic, compressor
 * and pump are perc, mel, comp, pump; then the out page's list */
const char *const MC_NAME[NMC] = {"off", "tape", "sampler"};
const char *const MS_NAME[NMS] = {"low", "mid", "high", "clip", "color", "amount", "tone", "mix",
                                  "drums", "melodic", "comp", "pump", "pump by", "release", "accent", "level"};

typedef struct { float b0, b1, b2, a1, a2; } biq_t;
typedef struct { float x1, x2, y1, y2; } biqs_t;

struct master {
    uint8_t pot[NMS];
    float gp, gm, glev;                       /* the group and master levels (targets) */
    float gp_c, gm_c, lev_c;                  /* gliding */
    float cg;                                 /* CLIP's drive (1 .. 2.5x); 0: off */
    /* EQ */
    biq_t eq[3];
    biqs_t eqs[2][3];
    uint8_t eq_on, comp_on;                   /* eq_on: bit b, band b moved off flat (a flat band is not run) */
    /* compressor (X0X's master.c) */
    float thr_db, slope, knee, a_att, a_rel, makeup, gr, ms, a_det, want, g_lin, g_step, gr_view;
    uint8_t cr;
    /* PUMP (X0X's): its depth (dB), its envelope (dB) and the target it rises to; the rise and the release
     * as coefficients of MST_CR samples (it moves where the gain is computed) */
    float pump_db, pump, pump_tgt, a_up, a_down, pump_view;
    float pg, pg_step;                /* its gain on the groups, ramped as a linear gain over MST_CR samples */
    uint8_t pcr, duck_mel;            /* (the countdown); duck_mel: a kit's hit keyed it, the melodic group ducks alone */
#define MST_CR 8                              /* (the gain computed every 8 samples, ramped between: 5.5 kHz) */
    /* limiter (X0X's) */
#define MST_LA 64
    float env, gain, inv_env;
    float la[MST_LA], la_r[MST_LA];
    int32_t la_pos, hold;
    /* color (fx.h): the type running, its numbers (master_set), its states; the tape's wow line, a side */
#define MC_LINE 128
#define MC_DELAY 64.0f                        /* the wow's centre, samples (1.5 ms: the mix that late while tape is on) */
    uint8_t col;
    float c_g, c_mk, c_lp, c_dep, c_mix, c_step, c_pre, c_q, c_iq;
    float c_z[2][2], c_ph, c_fph, c_sh[2], c_sp;
    int16_t c_line[2][MC_LINE];
    int32_t c_pos;
};
_Static_assert(sizeof(struct master) <= MASTER_BYTES, "master_t does not fit MASTER_BYTES");

/* the bus's headroom: the groups enter it 8 dB down, so a project's chords and its kit peak a few dB under full scale
 * before the EQ, the color, the compressor and the limiter (at unity they peaked at +2 .. +9 dBFS: the limiter always
 * working, the organs audibly squashed; 2026-10-19). The drums and the synths keep their balance; the FM-1's volume
 * makes up the level */
#ifndef MST_HEADROOM                          /* (tests/host/fx_test.sh: 1, the master's mechanics at unity) */
#define MST_HEADROOM 0.4f
#endif
static float level_lin(int v)                 /* 0 .. 100 .. 127: silence, unity (exactly), 2x */
{
    float x = (float)(v < 0 ? 0 : v > 127 ? 127 : v);
    return x <= 100.0f ? x * x * 0.0001f : 1.0f + (x - 100.0f) * (1.0f / 27.0f);
}
static float coef_ms(float t) { return fm_expf(-1000.0f / (t * SR)); }
float master_pump_ms(int v) { return 10.0f * fm_exp2f((float)v * (7.229f / 127.0f)); }   /* X0X's release: 10 .. 1500 */
int master_max(int p) { return p == MS_PUMP_BY ? NPB - 1 : p == MS_PUMP_ACC ? 1 : p == MS_CTYPE ? NMC - 1 : 127; }
/* PUMP's duck rises over this (X0X's MASTER_PUMP_RISE_MS): 12 ms lets the kick's attack through and then ducks;
 * at 3 ms X0X heard a click on every kick */
#define MST_PUMP_RISE_MS 12.0f

/* RBJ: a low shelf, a peak, a high shelf, gain in dB */
static void biq_shelf(biq_t *q, float hz, float db, int high)
{
    float A = fm_exp2f(db * (3.3219281f / 40.0f)), w = FM_TWO_PI * hz / SR, cs = fm_cosf(w), sn = fm_sinf(w);
    float al = sn * 0.5f * 1.4142136f, sa = 2.0f * fm_sqrtf(A) * al, ia, c = high ? -cs : cs;
    float a0 = (A + 1.0f) + (A - 1.0f) * c + sa;
    ia = 1.0f / a0;
    q->b0 = A * ((A + 1.0f) - (A - 1.0f) * c + sa) * ia;
    q->b1 = (high ? -2.0f : 2.0f) * A * ((A - 1.0f) - (A + 1.0f) * c) * ia;
    q->b2 = A * ((A + 1.0f) - (A - 1.0f) * c - sa) * ia;
    q->a1 = (high ? 2.0f : -2.0f) * ((A - 1.0f) + (A + 1.0f) * c) * ia;
    q->a2 = ((A + 1.0f) + (A - 1.0f) * c - sa) * ia;
}
static void biq_peak(biq_t *q, float hz, float db, float Q)
{
    float A = fm_exp2f(db * (3.3219281f / 40.0f)), w = FM_TWO_PI * hz / SR, cs = fm_cosf(w), sn = fm_sinf(w);
    float al = sn / (2.0f * Q), ia = 1.0f / (1.0f + al / A);
    q->b0 = (1.0f + al * A) * ia;
    q->b1 = -2.0f * cs * ia;
    q->b2 = (1.0f - al * A) * ia;
    q->a1 = -2.0f * cs * ia;
    q->a2 = (1.0f - al / A) * ia;
}

static void eq_coefs(master_t *m)
{
    float lo = ((float)m->pot[MS_EQ_LO] - 64.0f) * (12.0f / 64.0f);
    float mid = ((float)m->pot[MS_EQ_MID] - 64.0f) * (12.0f / 64.0f);
    float hi = ((float)m->pot[MS_EQ_HI] - 64.0f) * (12.0f / 64.0f);
    biq_shelf(&m->eq[0], 120.0f, lo, 0);
    biq_peak(&m->eq[1], 1000.0f, mid, 0.8f);
    biq_shelf(&m->eq[2], 6000.0f, hi, 1);
    {   /* the bands moved off flat (a flat one is 0 dB: it would only round); one turned on starts from rest */
        uint8_t on = (uint8_t)((m->pot[MS_EQ_LO] != 64) | (m->pot[MS_EQ_MID] != 64) << 1 | (m->pot[MS_EQ_HI] != 64) << 2);
        int b;
        for (b = 0; b < 3; b++)
            if ((on & ~m->eq_on) >> b & 1u) {
                static const biqs_t rest;
                m->eqs[0][b] = rest;
                m->eqs[1][b] = rest;
            }
        m->eq_on = on;
    }
}

/* X0X's one-knob COMP: the threshold falls (-4 .. -32 dB), the ratio climbs (1.5:1 .. 6:1), the
 * makeup follows (0.85 of the reduction at the threshold); attack 2.6 ms, release 106 ms */
static void comp_coefs(master_t *m)
{
    static const float R[6] = {1.0f, 1.5f, 2.0f, 3.0f, 4.0f, 6.0f};
    int v = m->pot[MS_COMP], ri = v ? 1 + (int)((float)v * (4.0f / 127.0f) + 0.5f) : 0;
    float x = (float)v * (1.0f / 127.0f), thr = -4.0f - 28.0f * x, mk;
    uint8_t was = m->comp_on;
    mk = ri ? fm_minf(-thr * (1.0f - 1.0f / R[ri]) * 0.85f, 24.0f) : 0.0f;
    m->thr_db = thr;
    m->slope = 1.0f - 1.0f / R[ri];
    m->knee = 6.0f;
    m->a_att = coef_ms(2.6f);
    m->a_rel = coef_ms(106.0f);
    m->a_det = coef_ms(8.0f);
    m->makeup = ri ? fm_db2lin(mk) : 1.0f;              /* (the pump alone: unity under it) */
    m->pump_db = 24.0f * (float)m->pot[MS_PUMP] * (1.0f / 127.0f);
    m->a_up = coef_ms(MST_PUMP_RISE_MS / (float)MST_CR);
    m->a_down = coef_ms(master_pump_ms(m->pot[MS_PUMP_REL]) / (float)MST_CR);
    if (!m->pot[MS_PUMP])
        m->pump = m->pump_tgt = 0.0f;
    m->comp_on = ri != 0;                        /* (the pump is on the groups: process's first loop) */
    if (m->comp_on && !was) {
        m->g_lin = m->makeup;
        m->g_step = 0.0f;
        m->cr = 0;
    }
}

/* the color's numbers from its knobs (the main loop); a new type starts at rest */
static void color_coefs(master_t *m)
{
    float a = (float)m->pot[MS_CAMT] * (1.0f / 127.0f), t = (float)m->pot[MS_CTONE] * (1.0f / 127.0f);
    int type = m->pot[MS_CTYPE] < NMC ? m->pot[MS_CTYPE] : MC_OFF;
    m->c_mix = (float)m->pot[MS_CMIX] * (1.0f / 127.0f);
    if (type == MC_TAPE) {
        float g = 0.5f * fm_exp2f(a * 3.585f), x = fm_minf(g * 0.7f, 1.5f);
        m->c_g = g;
        m->c_mk = 0.7f / (x - x * x * x * (4.0f / 27.0f)) * (1.0f - 0.15f * a);   /* (sat_c's at 0.7: made up) */
        m->c_lp = onepole_k(1500.0f * fm_exp2f(t * 3.585f));
        m->c_dep = (4.0f + 28.0f * a) * m->c_mix;  /* samples of wow: ~1 .. 6 cents at 0.55 Hz */
    } else if (type == MC_SAMPLER) {
        float r = 26040.0f * fm_exp2f(-a * 1.243f);
        m->c_step = r / SR;
        m->c_pre = onepole_k(0.45f * r);
        m->c_q = fm_exp2f(13.0f - 3.0f * a);
        m->c_iq = 1.0f / m->c_q;
        m->c_lp = onepole_k(2000.0f * fm_exp2f(t * 3.322f));
    }
    if (type != m->col) {                         /* (the interrupt may be mid-block: the states zeroed first) */
        m->c_z[0][0] = m->c_z[0][1] = m->c_z[1][0] = m->c_z[1][1] = 0.0f;
        m->c_sh[0] = m->c_sh[1] = m->c_sp = 0.0f;
        m->col = (uint8_t)type;
    }
}
/* the color over a block of the bus, in place (master_process; from RAM: every call inlined) */
static inline float mc_sat(float x)
{
    x = fm_minf(fm_maxf(x, -1.5f), 1.5f);
    return x - x * x * x * (4.0f / 27.0f);
}
static inline void color_block(master_t *m, float *l, float *r, uint32_t n)
{
    const float mix = m->c_mix, lp = m->c_lp;
    float z00 = m->c_z[0][0], z01 = m->c_z[0][1], z10 = m->c_z[1][0], z11 = m->c_z[1][1];
    uint32_t i;
    if (m->col == MC_TAPE) {
        const float g = m->c_g, mk = m->c_mk, dep = m->c_dep, dw = 0.55f / SR, df = 6.1f / SR;
        float ph = m->c_ph, fph = m->c_fph;
        int32_t pos = m->c_pos;
        for (i = 0; i < n; i++) {
            float d, fr, yl, yr, a0, a1;
            int32_t k, k0, k1;
            m->c_line[0][pos] = (int16_t)(fm_clampf(l[i], -1.99f, 1.99f) * 16384.0f);
            m->c_line[1][pos] = (int16_t)(fm_clampf(r[i], -1.99f, 1.99f) * 16384.0f);
            ph += dw;
            fph += df;
            if (ph >= 1.0f) ph -= 1.0f;
            if (fph >= 1.0f) fph -= 1.0f;
            d = MC_DELAY + dep * (fm_sin_turns(ph) + 0.125f * fm_sin_turns(fph));
            k = (int32_t)d;
            fr = d - (float)k;
            k0 = (pos - k) & (MC_LINE - 1);
            k1 = (k0 - 1) & (MC_LINE - 1);
            a0 = (float)m->c_line[0][k0], a1 = (float)m->c_line[0][k1];
            yl = (a0 + fr * (a1 - a0)) * (1.0f / 16384.0f);
            a0 = (float)m->c_line[1][k0], a1 = (float)m->c_line[1][k1];
            yr = (a0 + fr * (a1 - a0)) * (1.0f / 16384.0f);
            pos = (pos + 1) & (MC_LINE - 1);
            z00 += lp * (mc_sat(yl * g) * mk - z00);
            z01 += lp * (z00 - z01);
            z10 += lp * (mc_sat(yr * g) * mk - z10);
            z11 += lp * (z10 - z11);
            l[i] = yl + mix * (z01 - yl);
            r[i] = yr + mix * (z11 - yr);
        }
        m->c_ph = ph;
        m->c_fph = fph;
        m->c_pos = pos;
    } else if (m->col == MC_SAMPLER) {
        const float step = m->c_step, pre = m->c_pre, q = m->c_q, iq = m->c_iq;
        float sp = m->c_sp, h0 = m->c_sh[0], h1 = m->c_sh[1];
        for (i = 0; i < n; i++) {
            z00 += pre * (l[i] - z00);
            z10 += pre * (r[i] - z10);
            sp += step;
            if (sp >= 1.0f) {
                sp -= 1.0f;
                h0 = fm_floorf(z00 * q + 0.5f) * iq;
                h1 = fm_floorf(z10 * q + 0.5f) * iq;
            }
            z01 += lp * (h0 - z01);
            z11 += lp * (h1 - z11);
            l[i] += mix * (z01 - l[i]);
            r[i] += mix * (z11 - r[i]);
        }
        m->c_sp = sp;
        m->c_sh[0] = h0;
        m->c_sh[1] = h1;
    }
    m->c_z[0][0] = fm_flush(z00);
    m->c_z[0][1] = fm_flush(z01);
    m->c_z[1][0] = fm_flush(z10);
    m->c_z[1][1] = fm_flush(z11);
}

void master_init(master_t *m)
{
    static const uint8_t DEF[NMS] = {64, 64, 64, 0, MC_OFF, 40, 64, 127, 100, 100, 0, 0, PB_KICK, MS_PUMP_REL_DEF, 1, 100};
    uint32_t i;
    uint8_t *z = (uint8_t *)m;
    for (i = 0; i < sizeof *m; i++)
        z[i] = 0;
    for (i = 0; i < NMS; i++)
        m->pot[i] = DEF[i];
    m->gain = 1.0f;
    m->pg = 1.0f;                                /* (the pump's gain: none ducked) */
    m->gp = m->gp_c = m->gm = m->gm_c = MST_HEADROOM;
    m->glev = m->lev_c = 1.0f;
    eq_coefs(m);
    comp_coefs(m);
    color_coefs(m);
}

void master_clear(master_t *m)
{
    uint8_t pot[NMS];
    uint32_t i;
    for (i = 0; i < NMS; i++)
        pot[i] = m->pot[i];
    master_init(m);
    for (i = 0; i < NMS; i++)
        master_set(m, (int)i, pot[i]);
}

void master_set(master_t *m, int p, int v)
{
    if (p < 0 || p >= NMS)
        return;
    m->pot[p] = (uint8_t)(v < 0 ? 0 : v > master_max(p) ? master_max(p) : v);
    if (p <= MS_EQ_HI)
        eq_coefs(m);
    else if (p == MS_CLIP)
        m->cg = v ? fm_exp2f((float)v * (1.3288f / 127.0f)) : 0.0f;   /* 0 .. +8 dB of drive (at +12 the top half
                                                                     * flattened a mix); 0: the stage skipped */
    else if (p <= MS_CMIX)
        color_coefs(m);
    else if (p == MS_PERC)
        m->gp = level_lin(v) * MST_HEADROOM;
    else if (p == MS_MEL)
        m->gm = level_lin(v) * MST_HEADROOM;
    else if (p == MS_LEVEL)
        m->glev = level_lin(v);
    else
        comp_coefs(m);                                    /* comp, the pump's depth, source, release, accent */
}

float master_gr_db(const master_t *m) { return m->gr_view; }
float master_pump_db(const master_t *m) { return m->pump_view; }

void master_key(master_t *m, int src, int vel, int kit)
{
    float t;
    if (!m->pot[MS_PUMP] || src != m->pot[MS_PUMP_BY])
        return;
    t = m->pot[MS_PUMP_ACC] ? m->pump_db * (0.5f + 0.5f * (float)(vel < 0 ? 0 : vel > 127 ? 127 : vel) * (1.0f / 127.0f))
                            : m->pump_db;                   /* an accented hit pumps deeper (X0X: 0.5 .. 1) */
    if (t > m->pump_tgt)
        m->pump_tgt = t;
    m->duck_mel = (uint8_t)(kit != 0);           /* a kick (a kit's hit) ducks the rest, never itself */
}

static inline float gr_of(const master_t *m, float lv)       /* the static curve, soft knee (dB) */
{
    float over = lv - m->thr_db, h = 0.5f * m->knee;
    if (over <= -h)
        return 0.0f;
    if (over >= h)
        return m->slope * over;
    over += h;
    return m->slope * over * over / (2.0f * m->knee);
}
static inline float soft_ceiling(float s)
{
    if (s > 0.89f || s < -0.89f)
        s = s > 0.0f ? 0.89f + 0.11f * fm_tanhf((s - 0.89f) * 9.0f) : -0.89f - 0.11f * fm_tanhf((-s - 0.89f) * 9.0f);
    return s;
}
/* CLIP: a symmetric soft knee, flat to 0.5, then rounding towards the limiter's 0.8 (odd harmonics only) */
static inline float clip_knee(float s)
{
    if (s > 0.5f || s < -0.5f)
        s = s > 0.0f ? 0.5f + 0.3f * fm_tanhf((s - 0.5f) * (1.0f / 0.3f))
                     : -0.5f - 0.3f * fm_tanhf((-s - 0.5f) * (1.0f / 0.3f));
    return s;
}
static inline float glide1(float cur, float tgt)             /* a level knob: ~10 ms, then exact */
{
    if (cur == tgt)
        return cur;
    cur += (tgt - cur) * 0.0022651f;
    return fm_fabsf(tgt - cur) < 1e-5f ? tgt : cur;
}

#ifdef PLAT_HOST
int fx_host_stage = 9;                        /* the host's taps (host.c LOOM_MST): 0 the groups only, 1 + EQ, 2 + color,
                                               * 3 + comp and level, else all (the limiter) */
#define STAGE(k) (fx_host_stage >= (k))
#else
#define STAGE(k) 1
#endif
/* one biquad over a block, its state in registers (the band-major order of master_process: each band's samples
 * are the same as the sample-major loop's, operation for operation) */
static inline void biq_block(const biq_t *q, biqs_t *st, float *x, uint32_t n)
{
    const float b0 = q->b0, b1 = q->b1, b2 = q->b2, a1 = q->a1, a2 = q->a2;
    float x1 = st->x1, x2 = st->x2, y1 = st->y1, y2 = st->y2;
    uint32_t i;
    for (i = 0; i < n; i++) {
        float in = x[i], y = b0 * in + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1;
        x1 = in;
        y2 = y1;
        y1 = fm_flush(y);
        x[i] = y;
    }
    st->x1 = x1;
    st->x2 = x2;
    st->y1 = y1;
    st->y2 = y2;
}
void master_process(master_t *m, float *perc_l, float *perc_r, float *mel_l, float *mel_r, float *out_l,
                    float *out_r, uint32_t n)
{
    uint32_t i;
    float gp = m->gp_c, gm = m->gm_c, lev = m->lev_c, grmax = 0.0f, pmax = 0.0f;
    const int eq_on = m->eq_on, comp_on = m->comp_on;
    /* the groups into the bus, then the EQ a band at a time, in place in mel. (Every state is a local in its loop: a
     * load from RAM costs this core more than a multiply.) */
    {   /* (with the PUMP, X0X's, on the groups as they meet: a kit's hit ducks the melodic group alone, so the kick
         * that keys it is never ducked; a melodic track's note or the click, both) */
        const float tgp = m->gp, tgm = m->gm, a_up = m->a_up, a_down = m->a_down;
        float pump = m->pump, pump_tgt = m->pump_tgt, pg = m->pg, pg_step = m->pg_step;
        int pcr = m->pcr;
        const int mel_only = m->duck_mel;
        for (i = 0; i < n; i++) {
            float pp, ml = mel_l[i], mr = mel_r[i];   /* (both read first: a caller may pass one buffer for both) */
            gp = glide1(gp, tgp);
            gm = glide1(gm, tgm);
            if (pcr == 0) {
                if (pump_tgt > pump) {            /* rise to the hit's depth ... */
                    pump = a_up * pump + (1.0f - a_up) * pump_tgt;
                    if (pump_tgt - pump < 0.05f)
                        pump_tgt = 0.0f;          /* ... then let go: the release swells back */
                } else {
                    pump = fm_flush(a_down * pump);
                }
                if (pump > pmax)
                    pmax = pump;
                pg_step = (fm_db2lin(-pump) - pg) * (1.0f / MST_CR);
                pcr = MST_CR;
            }
            pcr--;
            pg += pg_step;
            pp = mel_only ? gp : gp * pg;
            mel_l[i] = perc_l[i] * pp + ml * gm * pg;
            mel_r[i] = perc_r[i] * pp + mr * gm * pg;
        }
        m->pump = pump;
        m->pump_tgt = pump_tgt;
        m->pg = pg;
        m->pg_step = pg_step;
        m->pcr = (uint8_t)pcr;
    }
    if (eq_on && STAGE(1)) {
        int b;
        for (b = 0; b < 3; b++)
            if (eq_on >> b & 1) {
                biq_block(&m->eq[b], &m->eqs[0][b], mel_l, n);
                biq_block(&m->eq[b], &m->eqs[1][b], mel_r, n);
            }
    }
    if (m->col != MC_OFF && STAGE(2))
        color_block(m, mel_l, mel_r, n);
    {
        const float glev = m->glev, a_det = m->a_det, a_att = m->a_att, a_rel = m->a_rel, slope = m->slope;
        const float makeup = m->makeup, cg = m->cg;
        float ms = m->ms, want = m->want, gr = m->gr;
        float g_lin = m->g_lin, g_step = m->g_step, env = m->env, inv_env = m->inv_env, gain = m->gain;
        float *la = m->la, *la_r = m->la_r;
        int32_t la_pos = m->la_pos, hold = m->hold;
        int cr = m->cr;
        for (i = 0; i < n; i++) {
            float s = mel_l[i], sr = mel_r[i];
            lev = glide1(lev, glev);
            if (comp_on && STAGE(3)) {
                /* the level: mean square over ~8 ms of both sides, x2 so a sine reads its peak; the static
                 * curve every MST_CR samples; the reduction smoothed (attack / release) and ramped as a
                 * linear gain (X0X's master.c: read every sample, the gain chased each cycle of a bass) */
                ms = fm_flush(a_det * ms + 0.5f * ((1.0f - a_det) * s * s + (1.0f - a_det) * sr * sr));
                if (cr == 0) {
                    float lv = 2.0f * ms;
                    want = (lv > 1e-12f && slope > 0.0f) ? gr_of(m, 3.0103f * fm_log2f(lv)) : 0.0f;
                }
                gr = want > gr ? a_att * gr + (1.0f - a_att) * want : a_rel * gr + (1.0f - a_rel) * want;
                if (gr > grmax)
                    grmax = gr;
                if (cr == 0) {
                    g_step = (makeup * fm_db2lin(-gr) - g_lin) * (1.0f / MST_CR);
                    cr = MST_CR;
                }
                cr--;
                g_lin += g_step;
                s *= g_lin;
                sr *= g_lin;
            }
            s *= lev;
            sr *= lev;
            if (cg != 0.0f && STAGE(4)) {         /* CLIP: the drive into the knee, before the limiter */
                s = clip_knee(s * cg);
                sr = clip_knee(sr * cg);
            }
            {   /* X0X's limiter: a peak is seen MST_LA samples before it comes out of the line; the gain
                 * eases down over those samples and holds for them */
                float a = fm_maxf(fm_fabsf(s), fm_fabsf(sr)), d, dr, w;
                if (a >= env) {
                    float r = 1.0f / fm_maxf(a, 0.8f);                   /* safe on its own: a clamp inside
                                                                          * the branch is folded away */
                    env = a;
                    hold = MST_LA;
                    inv_env = a > 0.8f ? r : 0.0f;
                } else if (hold) {
                    hold--;
                } else {
                    env *= 0.99985f;
                    inv_env *= 1.0001500225f;
                }
                w = env > 0.8f ? 0.8f * inv_env : 1.0f;
                gain += (w - gain) * (w < gain ? (5.0f / MST_LA) : 0.002f);
                d = la[la_pos];
                dr = la_r[la_pos];
                la[la_pos] = s;
                la_r[la_pos] = sr;
                la_pos = (la_pos + 1) & (MST_LA - 1);
                out_l[i] = soft_ceiling(d * gain);
                out_r[i] = soft_ceiling(dr * gain);
#ifdef PLAT_HOST
                if (!STAGE(4)) {                  /* (a tap before the limiter: the bus as it comes, unlimited) */
                    out_l[i] = d;
                    out_r[i] = dr;
                }
#endif
            }
        }
        m->ms = ms;
        m->want = want;
        m->gr = gr;
        m->g_lin = g_lin;
        m->g_step = g_step;
        m->env = env;
        m->inv_env = inv_env;
        m->gain = gain;
        m->la_pos = la_pos;
        m->hold = hold;
        m->cr = (uint8_t)cr;
    }
    m->gp_c = gp;
    m->gm_c = gm;
    m->lev_c = lev;
    m->gr_view = grmax > m->gr_view ? grmax : m->gr_view * 0.93f;
    m->pump_view = pmax > m->pump_view ? pmax : m->pump_view * 0.93f;
}
