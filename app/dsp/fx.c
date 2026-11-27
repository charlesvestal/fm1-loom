/* SPDX-License-Identifier: MPL-2.0 */
/* LOOM-8 effects (fx.h): the send slots and the master section.
 *
 * Send slots: each takes a mono send and adds a stereo return; the effects themselves are dsp/efx.c's, the catalog's
 * (the sends' own chorus, delay, lofi, phaser and reverb; the rest as a kit's slot runs them). Here: a slot's effect
 * and its memory (the pool's, the engine's), its knobs, its filter page, the glide of its numbers.
 *
 * Knob values are turned into the effect's numbers in fx_set (the interrupt, between blocks) and glide toward them a
 * block at a time (~10 ms): no zipper noise on a turn. A change of effect starts it at rest at the next block, its
 * memory cleared (another reverb mode: its states only; the SPU's and lush's lines clear themselves, a slice a block);
 * memory given at another place is cleared as it comes. The slot's filter page (high-pass, low-pass) runs on the mono
 * send before the effect: one filter instead of two, and the same result for the linear effects.
 *
 * Master: the two groups' levels, a 3-band EQ (RBJ shelves and a peak; all three at 64 a true bypass), X0X's bus compressor behind its one-knob macro with X0X's PUMP in its gain
 * (a hit of the pump's source ducks the whole mix: the sequencer keys it, so it needs no detector and a
 * muted track keys it too; at 0 the stage is as before, sample for sample), the master level and X0X's
 * lookahead limiter; and the master filter (fx.h MS_DFILT ..: DJ filters on the drums and on the melodic group, their
 * pairs and sends before they meet: the engine calls master_group_filter; one on the mix between the level and CLIP;
 * at the centre the stage skipped).
 *
 * Everything here runs in the audio interrupt; master_set writes plain words from the main loop. Float, fastmath.h;
 * stack locals stay small (a 32-sample block). */
#include "fx.h"
#include "fastmath.h"

#define SR 44100.0f
#define SUB 32                                /* the block: parameter glides step here */
#define GLIDE 0.07f                           /* a block's share of the distance: ~10 ms to settle */

struct fxslot {
    efx_send_t e;                             /* the sends' own effects: their numbers and states, their lines (efx.h) */
    efx_slot_t s;                             /* the others' (as a kit's slot) */
    int16_t *mem;                             /* its memory (the pool's) and its blocks */
    uint8_t mnb, fresh;                       /* (fresh: its memory just given: the effect starts at rest) */
    uint8_t clr;                              /* its blocks still to clear (fx_memory, a slice a block: silent till then) */
    uint8_t type, type_t, mode, mode_t;       /* the effect (EF_*) and the reverb's mode: running, and as set */
    uint8_t live, fo;                         /* live: it has run; fo: the running one has faded out (fx_fading) */
    float lv;                                 /* its return's level as a new effect comes in (0 .. 1 in a block) */
    uint8_t k[3], x[4];                       /* its knobs and extras */
    uint8_t knob[4];                          /* a send effect's four, as efx_send_nums takes them */
    uint8_t hpv, lpv;
    float bpm;
    float hpc, lpc, hps, lps;                 /* the filter page */
};
_Static_assert(sizeof(struct fxslot) <= FXSLOT_BYTES, "fxslot_t does not fit FXSLOT_BYTES");

static inline float onepole_k(float hz) { return hz >= 20000.0f ? 1.0f : 1.0f - fm_expf(-FM_TWO_PI * hz / SR); }
static inline float k127(int v) { return (float)(v < 0 ? 0 : v > 127 ? 127 : v) * (1.0f / 127.0f); }
static inline int send_own(int ef)            /* one of the sends' own (efx_send_run) */
{
    return ef == EF_CHORUS || ef == EF_DELAY || ef == EF_LOFI || ef == EF_PHASER || ef == EF_REVERB;
}

static void fx_point(fxslot_t *f)               /* the running effect's lines in its memory */
{
    int16_t *m = f->mem;
    f->e.cho = f->type == EF_DELAY ? (m ? m + 2 * FX_DLY_LEN : 0) : m;
    f->e.delay.l = m;
    f->e.delay.r = m ? m + FX_DLY_LEN : 0;
    f->e.reverb.line = m;
    f->e.reverb.n = (uint32_t)efx_rv_ring(f->mode, f->mnb, 1);
}

static void fx_states(fxslot_t *f, int wipe)    /* a fresh effect: its states at rest (wipe: its memory cleared too, a
                                                 * slice a block: fx_memory) */
{
    uint8_t *z = (uint8_t *)&f->s;
    uint32_t i;
    if (wipe && f->mem)
        f->clr = f->mnb;
    fx_point(f);
    efx_send_reset(&f->e, f->mode);
    for (i = 0; i < sizeof f->s; i++)
        z[i] = 0;
    f->s.cef = 0xFFu;
}

int fx_delay_halves(int time, int fine, float bpm)
{
    int h;
    efx_delay_secs(time, fine, bpm, &h);
    return h;
}

/* the knobs to the effect's numbers (a send's own; the others' per block, fx_other) */
static void fx_derive(fxslot_t *f)
{
    if (send_own(f->type_t))
        efx_send_nums(&f->e, f->type_t, f->mode_t, f->knob, f->bpm, f->x[2], f->x[3]);
}

void fx_init(fxslot_t *f)
{
    static const uint8_t K[3] = {0, 0, 0}, X[4] = {0, 0, 0, 0};
    uint32_t i;
    uint8_t *z = (uint8_t *)f;
    for (i = 0; i < sizeof *f; i++)
        z[i] = 0;
    f->bpm = 120.0f;
    f->lv = 1.0f;
    f->lpv = 127;
    f->hpc = onepole_k(20.0f);
    f->lpc = onepole_k(20000.0f);
    fx_set(f, EF_OFF, K, X);
    fx_states(f, 0);
}

void fx_clear(fxslot_t *f)
{
    f->hps = f->lps = 0.0f;
    fx_states(f, 1);
}

/* another effect (or reverb mode) set while one plays: the old one runs a block more, its return fading out, and
 * keeps its memory till then (fx_need, fx_lags: the running one's); then the new one from rest, its return fading in */
static int fx_fading(const fxslot_t *f)
{
    return f->live && f->type != EF_OFF && (f->type_t != f->type || f->mode_t != f->mode) && !f->fo && !f->clr &&
           efx_need(f->type, f->mode, 1) <= (f->mem ? f->mnb : 0);
}
int fx_need(const fxslot_t *f) { return fx_fading(f) ? efx_need(f->type, f->mode, 1) : efx_need(f->type_t, f->mode_t, 1); }
int fx_effect(const fxslot_t *f, int *mode)
{
    *mode = f->mode_t;
    return f->type_t;
}
int fx_room(const fxslot_t *f) { return efx_need(f->type_t, f->mode_t, 1) <= (f->mem ? f->mnb : 0); }

int fx_memory(fxslot_t *f, int16_t *mem, int nb)
{
    if (!mem)
        nb = 0;
    if (mem != f->mem || nb != f->mnb) {
        f->mem = mem;
        f->mnb = (uint8_t)nb;
        f->clr = (uint8_t)nb;
        f->fresh = 1;
    }
    efx_clear_step(f->mem, f->mnb, &f->clr, EFX_CLR);
    return f->clr;
}

void fx_set(fxslot_t *f, int ef, const uint8_t k[3], const uint8_t x[4])
{
    int i;
    ef = ef > EF_OFF && ef < NEF ? ef : EF_OFF;
    for (i = 0; i < 3; i++)
        f->k[i] = k[i] > 127 ? 127 : k[i];
    for (i = 0; i < 4; i++)
        f->x[i] = x[i] > 127 ? 127 : x[i];
    f->knob[0] = f->k[0];                     /* (the sends' four: the delay's time, fine, repeats, tone; the reverb's
                                               * size, decay or motion, tone, damp; the others' three and their extra) */
    f->knob[1] = ef == EF_DELAY ? f->x[0] : f->k[1];
    f->knob[2] = ef == EF_DELAY ? f->k[1] : f->k[2];
    f->knob[3] = ef == EF_DELAY ? f->k[2] : ef == EF_REVERB ? f->x[1] : f->x[0];
    if (ef == EF_DELAY)                       /* (its width glides there) */
        efx_delay_width(&f->e.delay.ta, &f->e.delay.tb, f->x[1]);
    /* another reverb mode while the reverb runs: the interrupt takes it at its next block (fx_states: spu_reset, its
     * line cleared a slice a block) */
    f->mode_t = (uint8_t)(ef == EF_REVERB && f->x[0] < NRV ? f->x[0] : RV_PLATE);
    f->type_t = (uint8_t)ef;
    fx_derive(f);
}

void fx_tempo(fxslot_t *f, float bpm)
{
    if (bpm > 20.0f && bpm != f->bpm) {
        f->bpm = bpm;
        if (f->type_t == EF_DELAY) {
            int h;
            f->e.pt[0] = efx_delay_secs(f->knob[0], f->knob[1], f->bpm, &h) * SR;
        }
    }
}

void fx_filter(fxslot_t *f, int hp, int lp)
{
    f->hpv = (uint8_t)(hp < 0 ? 0 : hp > 127 ? 127 : hp);
    f->lpv = (uint8_t)(lp < 0 ? 0 : lp > 127 ? 127 : lp);
    f->hpc = onepole_k(20.0f * fm_exp2f(k127(hp) * 8.64f));            /* 20 Hz .. 8 kHz */
    f->lpc = onepole_k(20000.0f * fm_exp2f(-(1.0f - k127(lp)) * 8.0f)); /* 78 Hz .. 20 kHz */
}

int fx_lags(const fxslot_t *f)
{
    const int t = fx_fading(f) ? f->type : f->type_t;
    return t == EF_CHORUS || t == EF_DELAY || t == EF_REVERB;
}

/* the catalog's others on the send (both sides), wet, as a kit's slot runs them (dsp/efx.c efx_slot_run: ring at a
 * ratio of C4, tape with its wow, the inserts' effects at the LFO's rate of their first knob) */
static void fx_other(fxslot_t *f, float *l, float *r, int m)
{
    efx_slot_t *sl = &f->s;
    sl->ef = f->type;
    for (int i = 0; i < 3; i++)
        sl->k[i] = (float)f->k[i];
    for (int i = 0; i < 4; i++)
        sl->xt[i] = f->x[i];
    sl->mix = 127;
    efx_slot_run(sl, 1, f->bpm, 0, efx_need(f->type, 0, 1) ? f->mem : 0, l, r, (uint32_t)m);
}

void fx_process(fxslot_t *f, const float *in, float *out_l, float *out_r, uint32_t n)
{
    fx_process_lag(f, in, out_l, out_r, n, 0);
}
void fx_process_lag(fxslot_t *f, const float *in, float *out_l, float *out_r, uint32_t n, int lag)
{
    uint32_t o;
    int out = 0;                               /* (fading out: the old one's last block) */
    if (f->type_t != f->type || f->mode_t != f->mode || f->fresh) {   /* a new effect or mode, new memory: at rest */
        const int wipe = f->type_t != f->type && !f->fresh;
        if (fx_fading(f) && !f->fresh && n) {   /* (a call of no frames: at once) */
            out = 1;
            f->fo = 1;
        } else {
            f->type = f->type_t;
            f->mode = f->mode_t;
            f->fresh = 0;
            f->fo = 0;
            f->lv = f->live ? 0.0f : 1.0f;     /* (live: its return from silence) */
            fx_states(f, wipe);
        }
    }
    for (o = 0; !f->live && o < n; o++)        /* (live once sound comes in) */
        f->live = in[o] != 0.0f;
    if (f->type == EF_OFF || efx_need(f->type, f->mode, 1) > (f->mem ? f->mnb : 0) || f->clr)
        return;                                /* (no memory, or its memory not yet clear: silent) */
    for (o = 0; o < n; o += SUB) {
        float x[2 * SUB];
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
        if (send_own(f->type) && !out && f->lv >= 1.0f) {
            efx_send_run(&f->e, f->type, x, out_l + o, out_r + o, m, any, lag);
        } else {
            float r[SUB], g = f->lv, dg = out ? -1.0f / (float)n : 1.0f / 256.0f;   /* (out: to silence across the
                                                 * call; in: up in 256 frames) */
            if (out)
                g = 1.0f - (float)o / (float)n;
            else if (g < 1.0f) {                 /* (coming in: its input from silence too, so its lines start
                                                  * without a step) */
                float gi = g;
                for (i = 0; i < m; i++) {
                    gi = fm_minf(gi + dg, 1.0f);
                    x[i] *= gi;
                }
            }
            if (send_own(f->type)) {
                for (i = 0; i < m; i++)
                    r[i] = 0.0f;
                for (i = 0; i < m; i++)
                    x[i + SUB] = 0.0f;
                efx_send_run(&f->e, f->type, x, x + SUB, r, m, any, lag);
                for (i = 0; i < m; i++)
                    x[i] = x[i + SUB];
            } else {
                for (i = 0; i < m; i++)
                    r[i] = x[i];
                fx_other(f, x, r, m);
            }
            for (i = 0; i < m; i++) {
                if (g < 1.0f || out)
                    g = fm_clampf(g + dg, 0.0f, 1.0f);
                out_l[o + i] += g * x[i];
                out_r[o + i] += g * r[i];
            }
            if (!out)
                f->lv = g;
        }
    }
}

int fx_awake(const fxslot_t *f)
{
    if (f->type == EF_OFF || efx_need(f->type, f->mode, 1) > (f->mem ? f->mnb : 0))
        return 0;
    return send_own(f->type) ? efx_send_awake(&f->e, f->type) : 1;
}

/* an old FX track's pick, knobs and the delay's three (fx.h) as the slot's values */
void fx_pick_slot(int pick, const uint8_t e[4], int width, int wobble, int smear, uint8_t *o)
{
    static const uint8_t EF[NFXT] = {EF_CHORUS, EF_DELAY, EF_LOFI, EF_PHASER, EF_REVERB};
    const int t = fx_pick_type(pick < 0 || pick >= NFXPICK ? 0 : pick);
    o[0] = EF[t];
    o[1] = e[0], o[2] = e[1], o[3] = e[2];
    o[5] = e[3], o[6] = o[7] = o[8] = 0;
    if (t == FXT_DELAY) {                        /* time, repeats, tone; fine, width, wobble, smear */
        o[2] = e[2], o[3] = e[3];
        o[5] = e[1], o[6] = (uint8_t)width, o[7] = (uint8_t)wobble, o[8] = (uint8_t)smear;
    } else if (t == FXT_REVERB) {                /* size, decay (motion), tone; the mode, damp (shimmer) */
        o[5] = (uint8_t)fx_pick_mode(pick);
        o[6] = e[3];
    }
}

/* ================================================================ master === */
/* the master's knob names, short enough for the knob strip: percussion, melodic, compressor
 * and pump are perc, mel, comp, pump; then the out page's list */
const char *const MS_NAME[NMS] = {"low", "mid", "high", "clip", "effect", "knob 1", "knob 2", "mix",
                                  "drums", "melodic", "comp", "pump", "pump by", "release", "accent", "level",
                                  "knob 3", "extra 1", "extra 2", "extra 3", "extra 4", "drums", "melodic", "mix",
                                  "res"};

typedef struct { float b0, b1, b2, a1, a2; } biq_t;
typedef struct { float x1, x2, y1, y2; } biqs_t;
/* the master filter (fx.h MS_DFILT ..): a state-variable filter's states, both sides; its cutoff (log2 Hz), damping
 * (1/Q) and wet, gliding once a block (mf_plan); a group's (the drums', the melodic one's) its sends' too (sc: s1's
 * two, s2's); the coefficients now (c: a1 0, none yet; made again only while they glide, never at rest), their step a
 * frame across the plan (dc, while mv), the frames the plan has left */
typedef struct { float a1, a2, a3, cx, c1, c2; } mfc_t;
typedef struct { float ic[4], lc, kc, w; mfc_t c, dc; uint16_t left; uint8_t mv; } mfilt_t;
static const uint8_t MF_KNOB[NMF] = {MS_DFILT, MS_MELF, MS_MFILT};
enum { MFM_OFF, MFM_LP, MFM_HP };

struct master {
    uint8_t pot[NMS];
    float gp, gm, glev;                       /* the group and master levels (targets) */
    float gp_c, gm_c, lev_c;                  /* gliding */
    float cg;                                 /* CLIP's drive (1 .. 2.5x); 0: off */
    float cg_c;                               /* its drive gliding there (~10 ms, as the level: a turn stepped it a block
                                               * at a time); 1 at rest when off; 0 until first set (taken at once) */
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
    float clip_pk, clip_view;         /* CLIP: the block's peak out of its knee; how deep into it, held and falling
                                       * (the Master EQ page) */
    float pg, pg_step;                /* its gain on the groups, ramped as a linear gain over MST_CR samples */
    uint8_t pcr, duck_mel;            /* (the countdown); duck_mel: a kit's hit keyed it, the melodic group ducks alone */
#define MST_CR 8                              /* (the gain computed every 8 samples, ramped between: 5.5 kHz) */
    /* limiter (X0X's) */
#define MST_LA 64
    float env, gain, inv_env;
    float la[MST_LA], la_r[MST_LA];
    int32_t la_pos, hold;
    /* the slot (fx.h): its effect, knobs and states (dsp/efx.c efx_slot_run; sl.clr its memory still to clear, sl.mef
     * and sl.mmd the effect it was given for); its memory (the pool's: the engine's) and what the interrupt runs on */
    efx_slot_t sl;
    int16_t *mem, *mrun;                      /* (mrun: mem while it holds the effect, else 0) */
    uint8_t mnb;
    uint8_t sw;                               /* the effect set (slot_set): the slot takes it at a block (master_memory),
                                               * the old one fading out a block first while the sound plays */
    uint8_t mfm[NMF];                         /* the filters' modes running (MFM_*): the drums', the melodic, the mix's */
    float bpm;
    mfilt_t mf[NMF];                          /* the drums' filter, the melodic group's, the mix's */
    float sc[2][4];                           /* the groups' sends' states (s1 two, s2 two) */
};
_Static_assert(sizeof(struct master) <= MASTER_BYTES, "master_t does not fit MASTER_BYTES");

/* the bus's headroom: the groups enter it 8 dB down, so a project's chords and its kit peak a few dB under full scale
 * before the EQ, the slot, the compressor and the limiter (at unity they peaked at +2 .. +9 dBFS: the limiter always
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
int master_max(int p) { return p == MS_PUMP_BY ? NPB - 1 : p == MS_PUMP_ACC ? 1 : p == MS_CTYPE ? NEF - 1 : 127; }
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

/* the slot's values from the knobs (master_set): its effect (off at mix 0: the stage skipped), knobs, mix, extras; the
 * interrupt runs it as they are (efx_slot_run: a new effect from rest; its knobs and mix glide). Another effect (or
 * reverb mode) set while the sound plays: master_memory takes it at a block, the old one fading out a block first
 * (efx_slot_want); then it runs on no memory until master_memory gives it its own */
static int slot_mode(const efx_slot_t *sl) { return sl->ef == EF_REVERB && sl->xt[0] < NRV ? sl->xt[0] : 0; }
static void slot_set(master_t *m)
{
    efx_slot_t *sl = &m->sl;
    sl->k[0] = (float)m->pot[MS_CAMT];
    sl->k[1] = (float)m->pot[MS_CTONE];
    sl->k[2] = (float)m->pot[MS_CK3];
    sl->mix = m->pot[MS_CMIX];
    for (int i = 1; i < 4; i++)
        sl->xt[i] = m->pot[MS_CX1 + i];
    m->sw = (uint8_t)(m->pot[MS_CMIX] && m->pot[MS_CTYPE] < NEF ? m->pot[MS_CTYPE] : EF_OFF);
    if (!sl->live) {                             /* (before the sound: at once) */
        sl->ef = m->sw;
        sl->xt[0] = m->pot[MS_CX1];
    }
    if (sl->ef != sl->mef || slot_mode(sl) != sl->mmd)
        m->mrun = 0;
}
int master_need(const master_t *m) { return m->sl.ef != EF_OFF ? efx_need(m->sl.ef, slot_mode(&m->sl), 2) : 0; }
int master_room(const master_t *m) { return master_need(m) <= (m->mem ? m->mnb : 0); }
int master_memory(master_t *m, int16_t *mem, int nb)
{
    efx_slot_t *sl = &m->sl;
    const int x0 = m->pot[MS_CX1], ef = efx_slot_want(sl, m->sw, m->sw == EF_REVERB && x0 < NRV ? x0 : 0);
    if (!sl->fo) {                               /* (the effect set, unless the old one fades out this block) */
        sl->ef = (uint8_t)ef;
        sl->xt[0] = (uint8_t)x0;
    }
    if (!mem)
        nb = 0;
    if (mem != m->mem || nb != m->mnb || sl->ef != sl->mef || slot_mode(sl) != sl->mmd) {   /* (new memory, or the
                                                  * effect it held changed: cleared, the effect from rest) */
        m->mem = mem;
        m->mnb = (uint8_t)nb;
        sl->clr = (uint8_t)nb;
        sl->zef = EF_OFF;
        sl->mef = sl->ef;
        sl->mmd = (uint8_t)slot_mode(sl);
    }
    efx_clear_step(m->mem, m->mnb, &sl->clr, EFX_CLR);
    m->mrun = master_room(m) ? m->mem : 0;
    return sl->clr;
}
void master_tempo(master_t *m, float bpm)
{
    if (bpm > 20.0f)
        m->bpm = bpm;
}
/* the master runs from RAM (tools/size_fns.py RAM_DSP), the slot from the flash: out of a direct call's reach, called
 * through a pointer the compiler cannot fold (engine.c DSP_FAR) */
static inline void *dsp_far(void *p) { void *volatile q = p; return q; }

void master_init(master_t *m)
{
    static const uint8_t DEF[NMS] = {64, 64, 64, 0, EF_OFF, 40, 64, 127, 100, 100, 0, 0, PB_KICK, MS_PUMP_REL_DEF, 1, 100,
                                     0, 0, 0, 0, 0, MS_FILT_OFF, MS_FILT_OFF, MS_FILT_OFF, 0};
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
    m->bpm = 120.0f;
    eq_coefs(m);
    comp_coefs(m);
    slot_set(m);
}

void master_clear(master_t *m)                  /* (its memory given again: the engine's next block) */
{
    uint8_t pot[NMS];
    float bpm = m->bpm;
    uint32_t i;
    for (i = 0; i < NMS; i++)
        pot[i] = m->pot[i];
    master_init(m);
    m->bpm = bpm;
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
    else if (p == MS_CLIP) {
        m->cg = v ? fm_exp2f((float)v * (1.3288f / 127.0f)) : 0.0f;   /* 0 .. +8 dB of drive (at +12 the top half
                                                                     * flattened a mix); 0: the stage skipped */
        if (m->cg_c == 0.0f)                                      /* (a new master: no glide in) */
            m->cg_c = m->cg != 0.0f ? m->cg : 1.0f;
    } else if (p <= MS_CMIX || (p >= MS_CK3 && p <= MS_CX4))
        slot_set(m);
    else if (p >= MS_DFILT)
        return;                                           /* (the filters: master_process reads them, mf_run) */
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
/* CLIP's meter, once a block (engine.c, in the flash: the RAM's master_process only keeps the block's peak): how deep
 * into the knee that peak went, 0 (under its 0.5: untouched) .. 1 (at its 0.8 ceiling), held and falling (~130 ms to
 * half: a hit stays long enough to read) */
float master_clip_depth(master_t *m)
{
    float d = m->cg != 0.0f ? fm_clampf((m->clip_pk - 0.5f) * (1.0f / 0.3f), 0.0f, 1.0f) : 0.0f;
    m->clip_view = d > m->clip_view ? d : m->clip_view * 0.97f;
    return m->clip_view;
}

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
int fx_host_stage = 9;                        /* the host's taps (host.c LOOM_MST): 0 the groups only, 1 + EQ, 2 + the slot,
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
/* the master filter (fx.h): a knob's cutoff in log2 Hz on its side of the centre (62..66 off) */
#define MF_LP_TOP 14.2877123f                 /* log2 20000: the low-pass's open end (61) */
#define MF_LP_OCT (8.38082178f / 61.0f)       /* down to 60 Hz at 0 */
#define MFM_HP_BOT 4.32192809f                 /* log2 20: the high-pass's open end (67) */
#define MFM_HP_OCT (8.64385619f / 60.0f)       /* up to 8 kHz at 127 */
static int mf_mode(int v) { return v < MS_FILT_OFF - 2 ? MFM_LP : v > MS_FILT_OFF + 2 ? MFM_HP : MFM_OFF; }
static float mf_log2hz(int v)
{
    return v < MS_FILT_OFF ? MF_LP_TOP - (float)(MS_FILT_OFF - 3 - v) * MF_LP_OCT
                           : MFM_HP_BOT + (float)(v - (MS_FILT_OFF + 3)) * MFM_HP_OCT;
}
static float mf_k(int r) { return 1.41421356f * (1.0f - 0.9f * (1.0f / 127.0f) * (float)r); }   /* 1/Q: 0.71 .. 7.1 */
float master_filter_hz(int v)
{
    const int md = mf_mode(v);
    return md == MFM_OFF ? 0.0f : (md == MFM_HP ? -1.0f : 1.0f) * fm_exp2f(mf_log2hz(v));
}
float master_filter_db(int v, int r, float hz)
{
    const int md = mf_mode(v);
    float x, x2, k, g2;
    if (md == MFM_OFF)
        return 0.0f;
    x = hz * fm_exp2f(-mf_log2hz(v));
    x2 = x * x;
    k = mf_k(r);
    g2 = (md == MFM_HP ? x2 * x2 : 1.0f) * k * 0.70710678f / ((1.0f - x2) * (1.0f - x2) + k * k * x2);
    return 3.0103f * fm_log2f(g2 + 1e-12f);
}
/* tan(x) on [0, 1.43] (the cutoffs up to 20 kHz): Pade's [5/4], within 0.01 % up to 8 kHz, 0.1 % at 20 kHz (the
 * low-pass's open end); one division (fm_tanf's two sines and a division, at each plan of a moving filter) */
static inline float mf_tan(float x)
{
    const float x2 = x * x;
    return x * (945.0f + x2 * (-105.0f + x2)) / (945.0f + x2 * (-420.0f + x2 * 15.0f));
}
/* the coefficients (Cytomic's SVF: a1..a3) and the output's mix of the input and the two states, from the cutoff, the
 * damping and the wet: low-pass v2, high-pass x - k v1 - v2; the wet's level made up for the resonance (sqrt(k /
 * sqrt 2): unity at none, -10 dB at the most, its peak then +7 dB) */
static void mf_coefs(mfc_t *c, const mfilt_t *f, int mode)
{
    const float g = mf_tan(FM_PI * (1.0f / SR) * fm_exp2f(f->lc)), wc = f->w * fm_sqrtf(f->kc * 0.70710678f);
    c->a1 = 1.0f / (1.0f + g * (g + f->kc));
    c->a2 = g * c->a1;
    c->a3 = g * c->a2;
    if (mode == MFM_HP) {
        c->cx = 1.0f - f->w + wc;
        c->c1 = -wc * f->kc;
        c->c2 = -wc;
    } else {
        c->cx = 1.0f - f->w;
        c->c1 = 0.0f;
        c->c2 = wc;
    }
}
/* a filter's plan over the next n frames (a block: master_filters_block; or a stretch the plan did not cover): its
 * cutoff and damping glide as the master's levels do (~10 ms), its wet in 10 ms straight, the coefficients from where
 * they are to there, a step a frame (mf_pair). Another mode (or off) while it sounds: it fades out first, then the new
 * one fades in from its open end, its states at rest */
static void mf_plan(mfilt_t *f, uint8_t *mode, int v, int r, float *sc, uint32_t n)
{
    const int want = mf_mode(v);
    float lt = mf_log2hz(v), kt = mf_k(r), wt = 1.0f;
    const float a = fm_minf((float)n * 0.0022651f, 1.0f), wa = (float)n * (1.0f / 441.0f);
    if (want != *mode && f->w == 0.0f) {         /* (faded out, or never in: the one wanted, from its open end) */
        *mode = (uint8_t)want;
        f->ic[0] = f->ic[1] = f->ic[2] = f->ic[3] = 0.0f;
        if (sc)
            sc[0] = sc[1] = sc[2] = sc[3] = 0.0f;
        f->lc = want == MFM_HP ? MFM_HP_BOT : MF_LP_TOP;
        f->kc = kt;
        f->c.a1 = 0.0f;
    }
    f->left = (uint16_t)n;
    f->mv = 0;
    if (*mode == MFM_OFF)
        return;
    if (want != *mode) {                         /* (fading out where it is) */
        lt = f->lc;
        kt = f->kc;
        wt = 0.0f;
    }
    if (f->c.a1 == 0.0f)                         /* (fresh: from here) */
        mf_coefs(&f->c, f, *mode);
    if (f->lc != lt || f->kc != kt || f->w != wt) {
        const float dl = lt - f->lc, dk = kt - f->kc, inv = 1.0f / (float)n;
        mfc_t e;
        f->lc = fm_fabsf(dl) < 0.002f ? lt : f->lc + dl * a;
        f->kc = fm_fabsf(dk) < 0.0005f ? kt : f->kc + dk * a;
        f->w = f->w < wt ? fm_minf(f->w + wa, wt) : fm_maxf(f->w - wa, wt);
        mf_coefs(&e, f, *mode);
        f->dc = (mfc_t){(e.a1 - f->c.a1) * inv, (e.a2 - f->c.a2) * inv, (e.a3 - f->c.a3) * inv,
                        (e.cx - f->c.cx) * inv, (e.c1 - f->c.c1) * inv, (e.c2 - f->c.c2) * inv};
        f->mv = 1;
    }
}
/* one filter's coefficients over n frames on two signals in place (the pair, or a group's two sends, mono each: their
 * states s[4]): moving, they step every 16 frames (each piece at its middle's values; 0.36 ms), every 4 while the
 * output's mix moves too (the resonance's make-up, a fade: in 16-frame steps a resonance turned clicked, slew_test
 * +4.6 dB; in 4, -0.4, as a ramp a frame), else the one set. lp: a low-pass's (c1 0: the band-pass state not in the
 * output; a register fewer, the loop's coefficients stay in them) */
static inline __attribute__((always_inline)) void mf_pair_k(const mfilt_t *f, float *X, float *Y, float *s,
                                                             uint32_t n, const int lp)
{
    float l1 = s[0], l2 = s[1], r1 = s[2], r2 = s[3];
    const uint32_t pc = f->dc.cx != 0.0f || f->dc.c1 != 0.0f || f->dc.c2 != 0.0f ? 4u : 16u;
    uint32_t i = 0;
    while (i < n) {
        const uint32_t e = f->mv && n - i > pc ? i + pc : n;
        const float t = f->mv ? (float)i + 0.5f * (float)(e - i) : 0.0f;
        const float a1 = f->c.a1 + f->dc.a1 * t, a2 = f->c.a2 + f->dc.a2 * t, a3 = f->c.a3 + f->dc.a3 * t;
        const float cx = f->c.cx + f->dc.cx * t, c1 = f->c.c1 + f->dc.c1 * t, c2 = f->c.c2 + f->dc.c2 * t;
        for (; i < e; i++) {
            const float xl = X[i], xr = Y[i];
            const float v3 = xl - l2, w3 = xr - r2;
            const float v1 = a1 * l1 + a2 * v3, w1 = a1 * r1 + a2 * w3;
            const float v2 = l2 + a2 * l1 + a3 * v3, w2 = r2 + a2 * r1 + a3 * w3;
            l1 = 2.0f * v1 - l1;
            l2 = 2.0f * v2 - l2;
            r1 = 2.0f * w1 - r1;
            r2 = 2.0f * w2 - r2;
            X[i] = lp ? cx * xl + c2 * v2 : cx * xl + c1 * v1 + c2 * v2;
            Y[i] = lp ? cx * xr + c2 * w2 : cx * xr + c1 * w1 + c2 * w2;
        }
    }
    s[0] = fm_flush(l1);
    s[1] = fm_flush(l2);
    s[2] = fm_flush(r1);
    s[3] = fm_flush(r2);
}
static void mf_pair(const mfilt_t *f, float *X, float *Y, float *s, uint32_t n)
{
    if (f->c.c1 == 0.0f && (!f->mv || f->dc.c1 == 0.0f))
        mf_pair_k(f, X, Y, s, n, 1);
    else
        mf_pair_k(f, X, Y, s, n, 0);
}
/* two signals silent over n frames and their states at rest (a group's sends: nothing to filter) */
static int mf_quiet(const float *a, const float *b, const float *s, uint32_t n)
{
    if (s[0] != 0.0f || s[1] != 0.0f || s[2] != 0.0f || s[3] != 0.0f)
        return 0;
    for (uint32_t i = 0; i < n; i++)
        if (a[i] != 0.0f || b[i] != 0.0f)
            return 0;
    return 1;
}
/* one filter over a stretch (in place, both sides; L may be R; S1, S2 the sends, mono, sc their states: 0 none): v its
 * knob, r its resonance (MS_*). Runs from the flash (only while it is on or fading), on the block's plan (else one of
 * its own: a filter turned on within a block, a stretch past the plan); the sends on the pair's coefficients */
static void mf_run(mfilt_t *f, uint8_t *mode, int v, int r, float *L, float *R, float *S1, float *S2, float *sc,
                   uint32_t n)
{
    if (*mode == MFM_OFF || f->left < n)
        mf_plan(f, mode, v, r, sc, n);
    if (*mode == MFM_OFF)
        return;
    if (S1 && !mf_quiet(S1, S2, sc, n))
        mf_pair(f, S1, S2, sc, n);
    mf_pair(f, L, R, f->ic, n);
    f->left = (uint16_t)(f->left - n);
    if (f->mv) {
        const float k = (float)n;
        f->c.a1 += f->dc.a1 * k;
        f->c.a2 += f->dc.a2 * k;
        f->c.a3 += f->dc.a3 * k;
        f->c.cx += f->dc.cx * k;
        f->c.c1 += f->dc.c1 * k;
        f->c.c2 += f->dc.c2 * k;
    }
}
/* filter k (MF_*) wanted on (its knob off the centre) or still running (fading out) */
static inline int mf_on(const master_t *m, int k)
{
    return m->mfm[k] != MFM_OFF || (unsigned)(m->pot[MF_KNOB[k]] - (MS_FILT_OFF - 2)) > 4u;
}
int master_group_on(const master_t *m, int k) { return k >= 0 && k < MF_MIX && mf_on(m, k); }
void master_filters_block(master_t *m, uint32_t n)
{
    for (int k = 0; k < NMF; k++)
        if (mf_on(m, k))
            mf_plan(&m->mf[k], &m->mfm[k], m->pot[MF_KNOB[k]], m->pot[MS_FRES], k < MF_MIX ? m->sc[k] : 0, n);
}
void master_group_filter(master_t *m, int k, float *l, float *r, float *s1, float *s2, uint32_t n)
{
    if (master_group_on(m, k))
        mf_run(&m->mf[k], &m->mfm[k], m->pot[MF_KNOB[k]], m->pot[MS_FRES], l, r, s1, s2, s1 ? m->sc[k] : 0, n);
}
/* the profiler's detail (fx.h MP_*): engine.c's clock and its totals; until it sets them (the host's tests), a clock
 * that stands still and totals of its own (the master's RAM code reads it without a test) */
static uint32_t mp_still(void) { return 0u; }
static volatile uint32_t mp_none[MP_N];
uint32_t (*master_clock)(void) = mp_still;
volatile uint32_t *master_prof = mp_none;
/* the mix's filter over a stretch when it is on (from the flash: master_tail_f calls it through dsp_far, its RAM code
 * kept small) */
static void mf_stage(master_t *m, float *L, float *R, uint32_t n)
{
    if (mf_on(m, MF_MIX)) {
        const uint32_t t = master_clock();
        mf_run(&m->mf[MF_MIX], &m->mfm[MF_MIX], m->pot[MS_MFILT], m->pot[MS_FRES], L, R, 0, 0, 0, n);
        master_prof[MP_MFILT] += master_clock() - t;
    }
}
#define MF_STAGE(m, L, R, n) ((__typeof__(&mf_stage))dsp_far((void *)&mf_stage))(m, L, R, n)

/* the compressor, the level, CLIP and the limiter (master_process's last loop), over mel in place into out; the
 * compressor's largest reduction. filt: with the mix's filter between the level and CLIP (the loop twice: pass 1 up
 * to the filter, pass 2 from it, the compressor and the level then at rest, x 1.0: exact); without, the one loop. One
 * copy, in RAM (master_process's) */
static inline float master_tail(master_t *m, float *mel_l, float *mel_r, float *out_l, float *out_r, uint32_t n,
                                int filt)
{
    uint32_t i;
    const float glev = m->glev, a_det = m->a_det, a_att = m->a_att, a_rel = m->a_rel, slope = m->slope;
    const float makeup = m->makeup, cgt = m->cg != 0.0f ? m->cg : 1.0f;   /* (CLIP off: down to unity, then skipped) */
    float cgc = m->cg_c != 0.0f ? m->cg_c : 1.0f;
    const int clip_on = m->cg != 0.0f || cgc != 1.0f;
    float ms = m->ms, want = m->want, gr = m->gr;
    float g_lin = m->g_lin, g_step = m->g_step, env = m->env, inv_env = m->inv_env, gain = m->gain;
    float *la = m->la, *la_r = m->la_r;
    int32_t la_pos = m->la_pos, hold = m->hold;
    int cr = m->cr;
    float glv = glev, lev = m->lev_c, lev1 = lev, grmax = 0.0f, cpk = 0.0f;
    int comp_go = m->comp_on, pass = filt;
    for (;;) {                                   /* (pass 2: the compressor and the level at rest, x 1.0: exact) */
        for (i = 0; i < n; i++) {
            float s = mel_l[i], sr = mel_r[i];
            lev = glide1(lev, glv);
            if (comp_go && STAGE(3)) {
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
            if (pass == 1) {                      /* (the first of two: the mix's filter between) */
                mel_l[i] = s;
                mel_r[i] = sr;
                continue;
            }
            if (clip_on && STAGE(4)) {            /* CLIP: the drive into the knee, before the limiter */
                cgc = glide1(cgc, cgt);
                s = clip_knee(s * cgc);
                sr = clip_knee(sr * cgc);
            }
            {       /* X0X's limiter: a peak is seen MST_LA samples before it comes out of the line; the gain
                 * eases down over those samples and holds for them */
                float a = fm_maxf(fm_fabsf(s), fm_fabsf(sr)), d, dr, w;
                cpk = fm_maxf(cpk, a);            /* (CLIP's meter: the block's peak out of its knee) */
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
        if (pass != 1)
            break;
        MF_STAGE(m, mel_l, mel_r, n);             /* the mix's filter: after the compressor and the pump, before CLIP
                                                   * and the limiter (it times itself: MP_MFILT) */
        pass = 2;
        lev1 = lev;
        lev = glv = 1.0f;
        comp_go = 0;
    }
    m->lev_c = pass == 2 ? lev1 : lev;
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
    m->clip_pk = cpk;
    m->cg_c = cgc;
    return grmax;
}

void master_process(master_t *m, float *perc_l, float *perc_r, float *mel_l, float *mel_r, float *out_l,
                    float *out_r, uint32_t n)
{
    uint32_t i;
    float gp = m->gp_c, gm = m->gm_c, grmax, pmax = 0.0f;
    const int eq_on = m->eq_on;
    uint32_t tk[MP_TAIL + 2], f0;                /* (the profiler's detail: the clock between the parts) */
    tk[0] = master_clock();
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
    tk[1 + MP_GROUPS] = master_clock();
    if (eq_on && STAGE(1)) {
        int b;
        for (b = 0; b < 3; b++)
            if (eq_on >> b & 1) {
                biq_block(&m->eq[b], &m->eqs[0][b], mel_l, n);
                biq_block(&m->eq[b], &m->eqs[1][b], mel_r, n);
            }
    }
    tk[1 + MP_EQ] = master_clock();
    if (m->sl.ef != EF_OFF && STAGE(2))          /* the slot (dsp/efx.c, from the flash) */
        ((__typeof__(&efx_slot_run))dsp_far((void *)&efx_slot_run))(&m->sl, 1, m->bpm, 0, m->mrun, mel_l, mel_r, n);
    for (i = 0; !m->sl.live && i < n; i++)       /* (off while the sound plays: an effect set starts from dry) */
        m->sl.live = mel_l[i] != 0.0f || mel_r[i] != 0.0f;
    tk[1 + MP_SLOT] = master_clock();
    f0 = master_prof[MP_MFILT];
    grmax = master_tail(m, mel_l, mel_r, out_l, out_r, n,      /* (the mix's filter on: two passes; its knob read
                                                                * here, not through MF_KNOB, a table in the flash) */
                        m->mfm[MF_MIX] != MFM_OFF || (unsigned)(m->pot[MS_MFILT] - (MS_FILT_OFF - 2)) > 4u);
    tk[1 + MP_TAIL] = master_clock();
    tk[1 + MP_TAIL] -= master_prof[MP_MFILT] - f0;   /* (the tail less the mix's filter inside it: MP_MFILT's) */
    for (i = 0; i <= MP_TAIL; i++)
        master_prof[i] += tk[i + 1] - tk[i];
    m->gp_c = gp;
    m->gm_c = gm;
    m->gr_view = grmax > m->gr_view ? grmax : m->gr_view * 0.93f;
    m->pump_view = pmax > m->pump_view ? pmax : m->pump_view * 0.93f;
}
