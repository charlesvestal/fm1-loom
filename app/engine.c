/* SPDX-License-Identifier: GPL-3.0-only */
/* LOOM-8 audio engine; see engine.h. The shape is X0X's: a command queue from the main loop, the
 * sequencer stepped sample-accurately between renders, one stretch of frames rendered as the first
 * core's part plus the second core's job, summed in one fixed order so a device with one core and a
 * device with two produce the same samples, then the sends, the two FX tracks, the master.
 *
 * Who owns which bytes: the synth reads the project's par[] arrays directly (synth_init takes the
 * pointer; the main loop writes bytes). The drumlines and the FX tracks read the engine's
 * own copy (epar), so a parameter lock can override a knob without touching the project's value,
 * and the knob's value returns when the lock ends. The sequencer is only ever stepped here. */
#include "engine.h"
#include "plat.h"
#include "app.h"
#include "fastmath.h"

#define POOL PLAT_POOL
/* the FX and the master run from RAM (tools/size_fns.py RAM_DSP): out of a direct call's reach from the XIP
 * window, so they are called through a pointer the compiler cannot fold */
static inline void *dsp_far(void *p) { void *volatile q = p; return q; }
#define DSP_FAR(fn) ((__typeof__(&fn))dsp_far((void *)&fn))

/* time per stage of the render, in plat_cycles() units: running totals (wrapping; the Status page
 * takes differences) and the frames rendered */
volatile uint32_t eng_prof[ENG_PROF_N], eng_prof_frames, eng_prof_worst[ENG_PROF_N];
volatile uint32_t eng_vworst[NVOICE + 2];          /* (engine.h) */
volatile uint8_t eng_prof_reset;
volatile uint32_t eng_prof_drum[NTRK];
static uint32_t prof_block0[ENG_PROF_N];
uint16_t trace[TRACE_N][TRACE_K], trace_worst[TRACE_N][TRACE_K];   /* (engine.h) */
static uint32_t trace_w, trace_take;
#define PROF_T(v) uint32_t v = plat_cycles()
#define PROF_ADD(k, t0) (eng_prof[k] += plat_cycles() - (t0))

/* ---------------------------------------------------------------- state --- */
seq_t seq;
volatile uint8_t eng_step[NSEQT];
volatile uint16_t eng_peak[NALLTRK];
volatile uint8_t eng_drum_hit[NDRUM];
volatile uint8_t eng_voices, eng_voices_max = NVOICE;
volatile uint32_t eng_guard_count;
volatile uint32_t eng_punch_drum, eng_punch_mel;
volatile float eng_pump_db;
volatile uint8_t eng_scene_now, eng_scene_cue = 0xFF;
int16_t eng_scope[SCOPE_N];
volatile uint32_t eng_scope_w;
volatile uint16_t eng_quiet;

/* the units, in the pool (their structs are opaque: a byte array each, 8-aligned for their floats) */
static uint8_t synth_mem[SYNTH_BYTES] POOL __attribute__((aligned(8)));
/* drumline units: NDL of them for the pool's sake (three drum tracks at once; a fourth drum track stays silent
 * until one of the others changes engine). Three, not four, since step_t grew to twelve bytes. */
#define NDL 3
static uint8_t dl_mem[NDL][DRUMLINE_BYTES] POOL __attribute__((aligned(8)));
static uint8_t dl_slot[NTRK];                              /* the unit a drum track owns, 0xFF none */
static uint8_t dl_owner[NDL];                              /* the track owning each unit, 0xFF free */
static chbus_t dl_ch[NDL];                                 /* each unit's character stage (the kit's TP_CTYPE..CMIX) */
static uint8_t fx_mem[2][FXSLOT_BYTES] POOL __attribute__((aligned(8)));
static uint8_t mst_mem[MASTER_BYTES] __attribute__((aligned(8)));
#define SY ((synth_t *)(void *)synth_mem)
#define DL(t) ((drumline_t *)(void *)dl_mem[dl_slot[t]])
static int dl_take(int t)                                  /* a free unit for track t: 1 = got one */
{
    int k;
    if (dl_slot[t] != 0xFFu)
        return 1;
    for (k = 0; k < NDL; k++)
        if (dl_owner[k] == 0xFFu) {
            dl_owner[k] = (uint8_t)t;
            dl_slot[t] = (uint8_t)k;
            dl_ch[k] = (chbus_t){.type = CT_OFF};
            return 1;
        }
    return 0;
}
static void dl_drop(int t)
{
    if (dl_slot[t] != 0xFFu) {
        dl_owner[dl_slot[t]] = 0xFF;
        dl_slot[t] = 0xFF;
    }
}
#define FXS(k) ((fxslot_t *)(void *)fx_mem[k])
#define MST ((master_t *)(void *)mst_mem)
/* the lines (the memory plan, docs/superpowers/specs: one delay, one reverb). FX I has
 * the delay lines, FX II the reverb line: the notebook's default project (FX I delay, FX II reverb) and the
 * classic delay-into-reverb (FX I's return feeds FX II). A second delay pair (88 KB) or reverb line (64 KB:
 * the SPU modes at the console's size) does not fit beside the canvas and the patterns; fx_has() tells the
 * UI which effects a slot can do. */
static int16_t dly1_l[FX_DLY_LEN] POOL, dly1_r[FX_DLY_LEN] POOL;
static int16_t rv2_line[FX_RV_LEN] POOL;

/* the engine's parameter copy (drumlines, FX read it; locks override it) and the project's
 * contiguous array (app.h proj_par: the synth and the sequencer read that one) */
static uint8_t epar[NALLTRK][NTP];
static uint8_t (*ppar)[NTP];
static uint8_t lock_on[NALLTRK][NTP / 8 + 1];     /* bit: the parameter is under a lock */
static uint8_t eng_drum[NTRK];                     /* the track's engine is the drumline (ISR's view) */
static uint8_t fx_type[2];                        /* each slot's effect, as set */
static uint32_t metro_left;                       /* the metronome's click: samples left, phase, rate */
static float metro_ph, metro_inc;
static int16_t bend_c[NTRK];                      /* the bend component's cents, per track */
volatile uint8_t eng_cc_level[NTRK] = {[0 ... NTRK - 1] = 0xFF};
static uint8_t perf_tail[NTRK], perf_mute[NTRK];  /* the drum tracks' punch state (the synth's perf_t is the synth's) */
static int8_t perf_pan[NTRK];
static int8_t perf_pan_base[NTRK];                /* the sequencer's pan offset (the arp's stereo), under the LFO */
static uint8_t dl_punch_hp[NTRK];                 /* a drum track under the punch filter (its dynamic filter raised) */
static float pan_lfo_ph;                          /* the punch pan LFO, ~2 Hz */
static float vib_ph;                              /* the punch vibrato, 6 Hz */

/* the second core */
static int (*c2_run)(void (*)(uint32_t), uint32_t);
static int (*c2_wait)(void);
void engine_set_core2(int (*run)(void (*)(uint32_t), uint32_t), int (*wait)(void))
{
    c2_run = run;
    c2_wait = wait;
}
int engine_slice_now(int trk, int *slice, int32_t *pos)
{
    return trk >= 0 && trk < NTRK && synth_slice_now(SY, trk, slice, pos);
}
int engine_core2_state(void)
{
    if (!c2_run)
        return 0;
    if (plat_cpu2_barred())
        return 3;
    return plat_cpu2_on() ? 1 : 2;
}

/* ------------------------------------------------------- overload guard --- */
/* past the CPU (85 % on average, or three blocks in a row over 92 %: a load that keeps the audio late) the synth
 * plays eight voices instead of sixteen (the quietest end early). A single block's spike does not trip it (a chord's
 * note-ons, a hit: capping the voices afterwards would not have saved that block). It lets go a step at a time: once
 * the average has stayed under 70 % for a second with no such run, two voices more, and so on each further second
 * (8, 10 .. 16: the load the voices bring back is measured on the way); a trip on the way starts again from eight.
 * (Before, it waited for every block under 65 %, which a song averaging 60 % with its spikes never gave: one trip
 * kept eight voices for the rest of the song.) The platform reports each block's load after rendering it (same
 * context as engine_render); the change is made at the start of the next block. */
#define GUARD_ON_LAST 92u
#define GUARD_ON_RUN 3u
#define GUARD_ON_AVG 85u
#define GUARD_OFF 70u
#define GUARD_STEP_FRAMES 44100u
#define GUARD_VOICES 8
static uint8_t guard_on, guard_applied, guard_cap = NVOICE, guard_run;
static uint32_t guard_avg_q8, guard_calm, guard_frames;
void engine_load(uint32_t pct)
{
    uint32_t frames = guard_frames;
    guard_frames = 0;
    synth_load_hint(SY, pct);                        /* (the note-start check: synth.c load_over) */
    guard_avg_q8 = (guard_avg_q8 * 15u + pct * 256u) / 16u;
    guard_run = pct > GUARD_ON_LAST ? (uint8_t)(guard_run < 255u ? guard_run + 1u : 255u) : 0u;
    if (guard_run >= GUARD_ON_RUN || guard_avg_q8 > GUARD_ON_AVG * 256u) {
        if (!guard_on || guard_cap > GUARD_VOICES)
            eng_guard_count++;                       /* a trip (or one on the way back) */
        guard_on = 1;
        guard_cap = GUARD_VOICES;
        guard_calm = 0;
    } else if (!guard_on) {
        return;
    } else if (guard_avg_q8 >= GUARD_OFF * 256u || guard_run) {
        guard_calm = 0;
    } else if ((guard_calm += frames) >= GUARD_STEP_FRAMES) {
        guard_calm = 0;
        guard_cap = (uint8_t)(guard_cap + 2u);
        if (guard_cap >= NVOICE) {
            guard_cap = NVOICE;
            guard_on = 0;
        }
    }
}
static void guard_apply(void)
{
    {
        uint8_t cap = guard_on ? guard_cap : NVOICE;
        if (cap == guard_applied)
            return;
        guard_applied = cap;
        eng_voices_max = cap;
        synth_limit(SY, eng_voices_max);
    }
}

/* ------------------------------------------------------- command queue --- */
/* uint32 commands, single producer (main loop) / single consumer (audio ISR): type:4 | a:8 | b:8 | c:12 (C_PARAM: b
 * the parameter's low eight bits, c its value and, at bit 8, its ninth: synth.h NTP) */
enum { C_PARAM, C_ALL, C_KEY, C_KEYS_OFF, C_PUNCH, C_BRAIN_KEY, C_MASTER, C_FX, C_PANIC,
       C_METRO, C_TEMPO, C_GROOVE, C_SCENE, C_NOP };     /* at most sixteen: the type is four bits */
#define CQ 512u
static uint32_t cq[CQ];
static volatile uint32_t cq_w, cq_r;

static void cq_put(uint32_t type, uint32_t a, uint32_t b, uint32_t c)
{
    uint32_t w = cq_w;
    if (w - cq_r >= CQ)
        return;                        /* full: drop (a knob flood; the project still has the value) */
    cq[w % CQ] = (type << 28) | ((a & 0xFFu) << 20) | ((b & 0xFFu) << 12) | (c & 0xFFFu);
    __asm__ volatile("" ::: "memory");
    cq_w = w + 1;
}

/* the FX settings travel beside the queue (too many bytes for one word): the main loop writes them,
 * posts C_FX, and the interrupt reads them whole at the next block. A knob caught between two writes
 * lasts one block. */
static struct { uint8_t type, knob[4], hp, lp; } fx_req[2];

/* the knob's value into the contiguous array the synth and the sequencer read (one byte: safe beside
 * the interrupt), then the command, for what needs doing with it (a kind change, a unit told) */
void engine_set(int trk, int param, int val)
{
    if (trk < 0 || trk >= NALLTRK || param < 0 || param >= NTP)
        return;
    if (val < 0)
        val = 0;
    if (val > 255)
        val = 255;
    ppar[trk][param] = (uint8_t)val;
    cq_put(C_PARAM, (uint32_t)trk, (uint32_t)param & 0xFFu, (uint32_t)val | ((uint32_t)param >> 8) << 8);
}
void engine_set_all(int trk)
{
    int k;
    if (trk < 0 || trk >= NALLTRK)
        return;
    for (k = 0; k < NTP; k++)
        ppar[trk][k] = proj.trk[trk].par[k];
    cq_put(C_ALL, (uint32_t)trk, 0, 0);
}
void engine_key(int trk, int note, int on, int vel)
{
    cq_put(C_KEY, (uint32_t)trk, (uint32_t)note, (uint32_t)((on ? 0x80u : 0u) | ((uint32_t)vel & 0x7Fu)));
}
void engine_keys_off(int trk) { cq_put(C_KEYS_OFF, (uint32_t)trk, 0, 0); }
void engine_punch(int fx, int on, int target_trk)
{
    cq_put(C_PUNCH, (uint32_t)fx, (uint32_t)(on != 0), (uint32_t)(target_trk < 0 ? 0xFF : target_trk));
}
void engine_brain_key(int note, int on) { cq_put(C_BRAIN_KEY, (uint32_t)note, (uint32_t)(on != 0), 0); }
void engine_master(int p, int val)
{
    if (p >= 0 && p < NMS)
        cq_put(C_MASTER, (uint32_t)p, 0, (uint32_t)val);
}
void engine_fx(int slot, int type, const uint8_t knob[4], int hp, int lp)
{
    int k;
    if (slot < 0 || slot > 1)
        return;
    fx_req[slot].type = (uint8_t)type;
    for (k = 0; k < 4; k++)
        fx_req[slot].knob[k] = knob[k];
    fx_req[slot].hp = (uint8_t)hp;
    fx_req[slot].lp = (uint8_t)lp;
    cq_put(C_FX, (uint32_t)slot, 0, 0);
}
void engine_panic(void) { cq_put(C_PANIC, 0, 0, 0); }
void engine_metronome(int mode) { cq_put(C_METRO, (uint32_t)mode, 0, 0); }
void engine_tempo(int bpm_x10)                  /* 400..3000 fits the command's twelve-bit value */
{
    if (bpm_x10 < 400)
        bpm_x10 = 400;
    if (bpm_x10 > 3000)
        bpm_x10 = 3000;
    cq_put(C_TEMPO, 0, 0, (uint32_t)bpm_x10);
}
void engine_groove(int type)
{
    if (type >= 0 && type < NGROOVE)
        cq_put(C_GROOVE, (uint32_t)type, 0, 0);
}
void engine_scene(int scene, int when)
{
    if (scene >= 0 && scene < NSCENE)
        cq_put(C_SCENE, (uint32_t)scene, (uint32_t)(when == SCENE_END ? 2 : when == SCENE_BAR), 0);
}
void engine_transport(int what)                 /* the sequencer's flags: written from any context */
{
    switch (what) {
    case 0: seq_stop(&seq); break;
    case 1: seq_start(&seq, 0); break;
    case 2: seq_continue(&seq); break;
    default: seq_start(&seq, 1); break;
    }
}

/* --------------------------------------------------------------- mixing --- */
/* a gain that glides to its target (one pole, ~10 ms) instead of stepping each block, which zippered
 * on quick turns of a level or pan (X0X). cur < 0: never used, takes the target at once (no fade-in
 * at boot); settled, cur IS the target, so the product is the unsmoothed one exactly */
typedef struct { float cur, tgt; } glide_t;
#define GLIDE_A 0.0022651f                                /* 1 - exp(-1 / (10 ms * 44.1 kHz)) */
static void glide_set(glide_t *g, float t)
{
    g->tgt = t;
    if (g->cur < 0.0f)
        g->cur = t;
}
__attribute__((noinline)) static void add_glide(float *dst, const float *src, glide_t *g, uint32_t n)
{
    uint32_t i;
    float c = g->cur, t = g->tgt;
    if (c == t) {
        if (c != 0.0f)
            for (i = 0; i < n; i++)
                dst[i] += src[i] * c;
        return;
    }
    for (i = 0; i < n; i++) {
        c += (t - c) * GLIDE_A;
        dst[i] += src[i] * c;
    }
    if (fm_fabsf(t - c) < 1e-5f)
        c = t;
    g->cur = c;
}
/* a balance law: centre is full level on both sides, turning toward a side fades the other out along
 * a quarter cosine; the near side stays at 1 */
static void pan_gains(int pan, float *gl, float *gr)
{
    float x = ((float)(pan < 0 ? 0 : pan > 127 ? 127 : pan) - 64.0f) / 63.0f;   /* -1 .. 1 */
    if (x < 0.0f) {
        *gl = 1.0f;
        *gr = fm_cosf(-x * 1.5707963f);
    } else {
        *gr = 1.0f;
        *gl = fm_cosf(x * 1.5707963f);
    }
}
enum { GL_L, GL_R, GL_SEND, GL_N };
static glide_t trk_glide[NTRK][GL_N];             /* the drum tracks' level and pan (the synth has its own) */
static glide_t ret_glide[2][2];                   /* the FX returns */

static float trk_peak[NALLTRK];
/* a meter after its fader (as the synth's): the louder side's peak times its gain */
__attribute__((noinline)) static void meter(int p, const float *l, const float *r, float gl, float gr, uint32_t n)
{
    uint32_t i;
    float pl = 0.0f, pr = 0.0f;
    for (i = 0; i < n; i++) {
        float a = fm_fabsf(l[i]), b = fm_fabsf(r[i]);
        if (a > pl)
            pl = a;
        if (b > pr)
            pr = b;
    }
    pl = fm_maxf(pl * gl, pr * gr);
    if (pl > trk_peak[p])
        trk_peak[p] = pl;
}

/* ------------------------------------------------------------- params --- */
/* a drum track's Filter page into its drumline (engine.h: the dynamic filter's four values, forced on
 * with a raised low cut while the punch filter is on it) */
static void dl_filter_update(int t)
{
    const uint8_t *p = epar[t];
    int type = p[TP_FTYPE], locut = p[TP_CUT], comp = p[TP_RES], drive = p[TP_FENV], hicut = p[TP_KTRK];
    if (!eng_drum[t])
        return;
    if (dl_punch_hp[t]) {
        if (type != FT_DYNAMIC) {                           /* neutral: only the low cut acts */
            comp = 0;
            drive = 0;
            hicut = 127;
            locut = 0;
        }
        type = FT_DYNAMIC;
        if (locut < PUNCH_HP)
            locut = PUNCH_HP;
    }
    drumline_filter(DL(t), type, locut, comp, drive, hicut);
}
static int is_filter_param(int param)
{
    return param == TP_FTYPE || param == TP_CUT || param == TP_RES || param == TP_FENV || param == TP_KTRK;
}
/* a lock or a knob reaching a unit that reads epar (the drumline reads it live; the FX are told) */
static uint8_t fx_c2;                                      /* the effects' job runs on the second core (from the
                                                            * block's start): their knobs wait for it (fx_dirty) */
static uint8_t fx_dirty;
static void aux_changed(int trk, int param)
{
    const uint8_t *p = epar[trk];
    if ((trk == AUX_FX1 || trk == AUX_FX2) && param >= TP_E1 && param <= TP_E4) {
        if (fx_c2)
            fx_dirty |= (uint8_t)(1u << (trk - AUX_FX1));
        else
            fx_set(FXS(trk - AUX_FX1), fx_type[trk - AUX_FX1], p + TP_E1);
    }
}
static void apply_param(int trk, int param, int val)       /* ISR: C_PARAM */
{
    int locked = (lock_on[trk][param >> 3] >> (param & 7)) & 1;
    if (!locked)
        epar[trk][param] = (uint8_t)val;
    if (trk < NTRK && param == TP_ENGINE) {
        int drum = val == EG_DRUMLINE;
        if (drum != eng_drum[trk]) {                        /* the track changes kind: silence, then a fresh unit */
            synth_all_off(SY, trk);
            if (eng_drum[trk])
                drumline_choke(DL(trk));
            dl_drop(trk);
            eng_drum[trk] = (uint8_t)(drum && dl_take(trk));
            if (eng_drum[trk]) {
                drumline_init(DL(trk), epar[trk]);
                dl_filter_update(trk);
            }
        }
    } else if (trk < NTRK && eng_drum[trk] && is_filter_param(param) && !locked) {
        dl_filter_update(trk);
    } else if (trk == AUX_BRAIN && param == TP_E4) {
        seq.brain_link = (uint8_t)(val > NTRK ? NTRK : val);   /* the linked track 1..8, 0 none */
    } else if (trk >= NTRK && !locked) {
        aux_changed(trk, param);
    }
}
static void apply_all(int trk)                             /* ISR: C_ALL (a load, a preset) */
{
    int p;
    for (p = 0; p < NTP; p++)
        lock_on[trk][p >> 3] = 0;                          /* fresh values: no lock survives */
    for (p = 0; p < NTP; p++)
        epar[trk][p] = ppar[trk][p];
    if (trk < NTRK) {
        int drum = ppar[trk][TP_ENGINE] == EG_DRUMLINE;
        synth_all_off(SY, trk);
        if (eng_drum[trk])
            drumline_choke(DL(trk));
        dl_drop(trk);
        eng_drum[trk] = (uint8_t)(drum && dl_take(trk));
        if (eng_drum[trk]) {
            drumline_init(DL(trk), epar[trk]);
            dl_filter_update(trk);
        }
    } else {
        if (trk == AUX_BRAIN)
            seq.brain_link = (uint8_t)(epar[trk][TP_E4] > NTRK ? NTRK : epar[trk][TP_E4]);
        aux_changed(trk, TP_E1);
    }
}

/* ------------------------------------------------------- sequencer sink --- */
/* the master's PUMP keys: a kit's kick (its first key, or a key on the kick model) keys "kick" and its own track's
 * (T1.., a kit's: its kicks, not its hats), any note of a melodic track that track's; the master takes the one it
 * follows (fx.h PB_*). Also from a muted track's steps (s_ghost) */
static void pump_drum(int trk, int sound, int vel)
{
    if (sound == 0 || epar[trk][TP_DRUM(sound, DP_MODEL)] == DM_KICK) {
        master_key(MST, PB_KICK, vel, 1);
        master_key(MST, PB_T1 + trk, vel, 1);
    }
}
#ifdef PLAT_HOST
static uint32_t host_drum_hits, host_note_ons;
#endif
static void s_note_on(void *x, int trk, int note, int vel, int glide_ms, uint32_t tag)
{
    (void)x;
    PROF_T(pn);
    if (trk < 0 || trk >= NTRK || eng_drum[trk])
        return;
    DSP_FAR(synth_note_on)(SY, trk, note, vel, glide_ms, tag);   /* (in RAM: tools/size_fns.py RAM_SETS) */
#ifdef PLAT_HOST
    host_note_ons++;                                   /* (the host's load model: a note's set-up) */
#endif
    master_key(MST, PB_T1 + trk, vel, 0);
    PROF_ADD(PR_NOTE, pn);
}
static void s_ghost(void *x, int trk, int sound, int vel)   /* a muted track's step: it keys the pump, silently */
{
    (void)x;
    if (trk < 0 || trk >= NTRK)
        return;
    if (eng_drum[trk] && sound >= 0 && sound < NDRUM)
        pump_drum(trk, sound, vel);
    else if (!eng_drum[trk])
        master_key(MST, PB_T1 + trk, vel, 0);
}
static void s_note_off(void *x, int trk, uint32_t tag)
{
    (void)x;
    PROF_T(pn);
    if (trk >= 0 && trk < NTRK && !eng_drum[trk])
        synth_note_off(SY, trk, tag);
    PROF_ADD(PR_NOTE, pn);
}
static int glfo_kit_semis(int trk, int sound);
static void s_drum(void *x, int trk, int sound, int vel, int pitch_semi, int short_gate, int long_tail)
{
    (void)x;
    PROF_T(pn);
    if (trk < 0 || trk >= NTRK || !eng_drum[trk] || sound < 0 || sound >= NDRUM)
        return;
    drumline_hit(DL(trk), sound, vel, pitch_semi + glfo_kit_semis(trk, sound), short_gate, long_tail || perf_tail[trk]);
#ifdef PLAT_HOST
    host_drum_hits++;                                  /* (the host's load model: a hit's own cost) */
#endif
    synth_duck_trigger(SY, trk);                       /* duck LFOs listening to this track drop */
    pump_drum(trk, sound, vel);
    if (trk == proj.trk_sel)
        eng_drum_hit[sound] = (uint8_t)(vel >= 127 ? 255 : vel * 2);
    PROF_ADD(PR_NOTE, pn);
}
static void s_drum_off(void *x, int trk, int sound)    /* the key or the step ends: gated and loop modes stop */
{
    (void)x;
    if (trk < 0 || trk >= NTRK || !eng_drum[trk] || sound < 0 || sound >= NDRUM)
        return;
    drumline_release(DL(trk), sound);
}
static void s_pan(void *x, int trk, int pan)           /* the player's pan offset (the arp's stereo), -64..64 */
{
    (void)x;
    if (trk >= 0 && trk < NTRK)
        perf_pan_base[trk] = (int8_t)(pan < -64 ? -64 : pan > 64 ? 64 : pan);
}
static void s_lock(void *x, int trk, int param, int val)
{
    PROF_T(pn);
    (void)x;
    if (trk < 0 || trk >= NALLTRK || param < 0 || param >= NTP)
        return;
    if (trk < NTRK && !eng_drum[trk]) {
        synth_lock(SY, trk, param, val);               /* the synth reads the project's par: it overlays itself */
        PROF_ADD(PR_NOTE, pn);
        return;
    }
    if (val == LOCK_NONE) {
        lock_on[trk][param >> 3] &= (uint8_t)~(1u << (param & 7));
        epar[trk][param] = ppar[trk][param];
    } else {
        lock_on[trk][param >> 3] |= (uint8_t)(1u << (param & 7));
        epar[trk][param] = (uint8_t)val;
    }
    if (trk >= NTRK)
        aux_changed(trk, param);
    else if (is_filter_param(param))
        dl_filter_update(trk);
    PROF_ADD(PR_NOTE, pn);
}
/* a parameter's value as a lock (a p-lock, a motion recording) holds it now, -1 when none does: the knobs' dot
 * (main loop: a byte read beside the interrupt's write) */
int engine_live(int trk, int param)
{
    if (trk < 0 || trk >= NALLTRK || param < 0 || param >= NTP)
        return -1;
    if (trk < NTRK && !eng_drum[trk])
        return synth_lock_value(SY, trk, param);
    return lock_on[trk][param >> 3] >> (param & 7) & 1u ? epar[trk][param] : -1;
}
static void s_bend(void *x, int trk, int cents)
{
    (void)x;
    if (trk >= 0 && trk < NTRK)
        bend_c[trk] = (int16_t)cents;
}
/* the live FX state: per track, its group's mask (drum tracks the drum FX, the rest the melodic) into
 * the track's performance state (a slice track takes the drums': seq.c is_perc). Both masks arrive as bits PX_*
 * (the sequencer shifts the melodic octave down; eng_punch_mel is the same). Here: mute, wobble (drums: the high-pass and a wobble on what is long;
 * melodic: a short vibrato), pan (the pan LFO), ring (the drums' tails). Stutter, repeat, echo, the fills,
 * octave and the note lengths are the sequencer's own (but a slice track's octave up, here: its pitch). */
static uint32_t punch_d, punch_m;
static void s_punch(void *x, uint32_t drum_mask, uint32_t mel_mask)
{
    (void)x;
    punch_d = drum_mask;
    punch_m = mel_mask;
    eng_punch_drum = drum_mask;
    eng_punch_mel = mel_mask;
}
static void s_midi(void *x, uint8_t st, uint8_t d1, uint8_t d2)
{
    uint32_t cin = st >= 0xF0 ? 0x0Fu : (uint32_t)(st >> 4);
    (void)x;
    plat_midi_out(cin | ((uint32_t)st << 8) | ((uint32_t)d1 << 16) | ((uint32_t)d2 << 24));
}
static void s_step(void *x, int trk, int step)
{
    (void)x;
    if (trk >= 0 && trk < NSEQT)
        eng_step[trk] = (uint8_t)step;
}
static void s_scene(void *x, int scene)
{
    (void)x;
    eng_scene_now = (uint8_t)scene;
}
static void s_metro(void *x, int accent)
{
    (void)x;
    metro_left = 1800u;                                  /* 40 ms */
    metro_ph = 0.0f;
    metro_inc = (accent ? 1760.0f : 1320.0f) / 44100.0f;
    synth_duck_trigger(SY, DUCK_METRO);
    master_key(MST, PB_METRO, accent ? 127 : 100, 0);
}
/* named members: the sequencer may grow its sink at the end without this changing */
static const seq_sink_t SINK = {
    .note_on = s_note_on, .note_off = s_note_off, .drum = s_drum, .lock = s_lock, .bend = s_bend, .punch = s_punch,
    .midi = s_midi, .step = s_step, .scene = s_scene, .metro = s_metro, .pan = s_pan, .drum_off = s_drum_off,
    .ghost = s_ghost, .ctx = 0};

/* the performance state of every instrument track from the punch masks and the bends: once a stretch */
#define PUNCH_VIB_DRUM 30.0f                               /* cents, the wobble under the drums' filter */
#define PUNCH_VIB_MEL 20.0f                                /* the melodic tracks' short vibrato */
static void perf_tick(uint32_t n)
{
    int t;
    float lfo, vib;
    pan_lfo_ph += (float)n * (2.0f / 44100.0f);
    if (pan_lfo_ph >= 1.0f)
        pan_lfo_ph -= 1.0f;
    vib_ph += (float)n * (6.0f / 44100.0f);
    if (vib_ph >= 1.0f)
        vib_ph -= 1.0f;
    lfo = fm_sin_turns(pan_lfo_ph);
    vib = fm_sin_turns(vib_ph);
    for (t = 0; t < NTRK; t++) {
        perf_t *p = synth_perf(SY, t);
        int drum = eng_drum[t], slice = epar[t][TP_ENGINE] == EG_SLICE, perc = drum || slice;
        uint32_t m = perc ? punch_d : punch_m;
        int mute = (int)((m >> PX_MUTE) & 1u), pan_on = (int)((m >> PX_PAN) & 1u);
        int filt = (int)((m >> PX_WARBLE) & 1u), tail = (int)((m >> PX_RING) & 1u), pan;
        int up = slice && (seq_punch_mask(&seq, t) >> PX_OCTAVE & 1u);   /* a slice track's octave: up, as the
                                                                         * drums (its note picks the slice) */
        p->pitch_c = (int16_t)(bend_c[t] + (up ? 1200 : 0) +
                               (filt ? (int)(vib * (perc ? PUNCH_VIB_DRUM : PUNCH_VIB_MEL)) : 0));
        p->cut = 0;
        p->hp = (uint8_t)(drum && filt ? PUNCH_HP : 0);
        pan = perf_pan_base[t] + (pan_on ? (int)(lfo * 60.0f) : 0);
        p->pan = (int8_t)(pan < -127 ? -127 : pan > 127 ? 127 : pan);
        p->mute = (uint8_t)mute;
        p->send1 = 0;
        p->send2 = 0;
        p->long_tail = (uint8_t)tail;
        perf_tail[t] = (uint8_t)tail;
        perf_mute[t] = (uint8_t)mute;
        perf_pan[t] = p->pan;
        if (drum && dl_punch_hp[t] != (uint8_t)filt) {    /* the drumline's filter follows the punch state */
            dl_punch_hp[t] = (uint8_t)filt;
            dl_filter_update(t);
        }
    }
}

/* ---------------------------------------------------------------- MIDI in --- */
static int midi_track(int ch)                      /* the track a channel plays: 1-8 theirs, 12-16 the one selected
                                                    * (else T1); FX and the master none (-1) */
{
    if (ch < NTRK)
        return ch;
    if (ch <= CH_MASTER)
        return -1;
    return proj.trk_sel < NTRK ? proj.trk_sel : 0;
}
static void midi_in(uint32_t pkt)
{
    uint8_t st = (uint8_t)(pkt >> 8), d1 = (uint8_t)(pkt >> 16) & 0x7F, d2 = (uint8_t)(pkt >> 24) & 0x7F;
    uint8_t ch = st & 0x0F, ty = st & 0xF0;
    switch (st) {
    case 0xF8: seq_ext_clock(&seq); return;
    case 0xFA: seq_start(&seq, 1); return;
    case 0xFB: seq_continue(&seq); return;
    case 0xFC: seq_stop(&seq); return;
    default: break;
    }
    if (ty == 0x90 && d2 == 0)
        ty = 0x80;
    if (ty == 0x90 || ty == 0x80) {
        int t = midi_track(ch);
        if (t >= 0)
            seq_key(&seq, t, d1, ty == 0x90, d2);
    } else if (ty == 0xB0 && d1 == 7) {            /* CC 7, volume: the track's level (Task 2 replaces it) */
        int t = midi_track(ch);
        if (t >= 0)
            eng_cc_level[t] = d2;
    } else if (ty == 0xE0) {                       /* pitch bend: +-2 semitones onto the track's bend */
        int t = midi_track(ch);
        if (t >= 0)
            bend_c[t] = (int16_t)((((int)d2 << 7 | d1) - 8192) * 200 / 8192);
    }
}
void engine_midi_in(uint32_t pkt) { midi_in(pkt); }   /* (the render drains plat_midi_in itself) */

/* ---------------------------------------------------------------- render --- */
static void drain(void)
{
    uint32_t r = cq_r, pkt;
    int guard = 64;
    while (r != cq_w) {
        uint32_t c = cq[r % CQ], type = c >> 28, a = (c >> 20) & 0xFFu, b = (c >> 12) & 0xFFu, v = c & 0xFFFu;
        r++;
        switch (type) {
        case C_PARAM: apply_param((int)a, (int)(b | (v >> 8 & 1u) << 8), (int)(v & 0xFFu)); break;
        case C_ALL: apply_all((int)a); break;
        case C_KEY: seq_key(&seq, (int)a, (int)b, (int)((v >> 7) & 1u), (int)(v & 0x7Fu)); break;
        case C_KEYS_OFF: seq_key_all_off(&seq, (int)a); break;
        case C_PUNCH: seq_punch_key(&seq, (int)a, (int)b, v == 0xFFu ? -1 : (int)v); break;
        case C_BRAIN_KEY: seq_brain_key(&seq, (int)a, (int)b); break;
        case C_MASTER: master_set(MST, (int)a, (int)v); break;
        case C_FX:
            if (a < 2u) {
                int k, type = fx_req[a].type;
                if (!fx_has(FXS(a), type))
                    type = FXT_CHORUS;                      /* what the slot runs instead (fx_set's fallback) */
                /* (type: the pick, fx.h: the effect and the reverb's mode) */
                fx_type[a] = (uint8_t)type;
                for (k = 0; k < 4; k++)
                    epar[AUX_FX1 + a][TP_E1 + k] = fx_req[a].knob[k];
                epar[AUX_FX1 + a][TP_ENGINE] = (uint8_t)type;
                fx_set(FXS(a), type, fx_req[a].knob);
                fx_filter(FXS(a), fx_req[a].hp, fx_req[a].lp);
            }
            break;
        case C_PANIC: {
            int t;
            synth_all_off(SY, -1);
            for (t = 0; t < NTRK; t++) {
                seq_key_all_off(&seq, t);
                if (eng_drum[t])
                    drumline_choke(DL(t));
                bend_c[t] = 0;
            }
            break;
        }
        case C_METRO: seq.metronome = (uint8_t)a; break;
        case C_TEMPO: seq.bpm = (float)v * 0.1f; break;
        case C_GROOVE: seq.groove_type = (uint8_t)a; break;
        case C_SCENE:
            if (b)
                seq_cue_scene(&seq, (int)a, b == 2);
            else
                seq_set_scene(&seq, (int)a);
            break;
        default: break;
        }
    }
    cq_r = r;
    while (guard-- && plat_midi_in(&pkt, 0))         /* USB or the TRS jack */
        midi_in(pkt);
}

/* the mix of one stretch between two events: the percussion group, the melodic group, the two
 * sends; the second core's own; the drumlines' and the FX returns' scratch. In .bss, not the pool
 * (the pool is for the lines). 256 frames each: the most one render asks for. */
static float perc_l[256], perc_r[256], mel_l[256], mel_r[256], s1[256], s2[256];
static float c2_l[256], c2_r[256], c2_s1[256], c2_s2[256];
static float tl[256], tr[256], ts1[256], ts2[256];
static float mix_l[256], mix_r[256];

__attribute__((noinline)) static void zerof(float *b, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        b[i] = 0.0f;
}

/* the second core's job: the voices it takes (ws_take1, below) into its own buffers; arg: the frames, bit 8 not
 * timed (the cycle counter is the first core's). Nothing here is mixed: the first core does that afterwards, in one
 * core's order. The synth wants every voice rendered each stretch after its prepare (synth.h), sounding or not */
/* the effects a block behind: the sends of each block (both cores' voices and the drums, FX I's return into FX II's
 * send included) gathered into snd_acc, and the next block's first job runs the chorus, the delay and the
 * reverb (fx_lags) on them, whole, into fx_ret, which that block's stretches mix in, at their place, as they would
 * have their own returns. The effects take the lag back in their lines (fx.h fx_process_lag: their output where it
 * was, but for the lines' rounding; the SPU modes take it as 5.8 ms more pre-delay), and they leave the first core:
 * the second renders them beside its voices (on one core the same job runs there, in the same order: the same
 * samples). The lofi and the phaser have no line to take a lag back: they run in time, on the first core, as
 * before. */
static float snd_acc[2][256] POOL, fx_ret[2][2][256];     /* (one block of sends: the job has read the last
                                                            * block's before this block's first stretch writes) */
static uint8_t fx_lag_on[2];
static uint32_t snd_n;                                     /* the frames in snd_acc: the last block's */
static void fx_job(void)
{
    int k;
    for (k = 0; k < 2; k++)
        if (fx_lag_on[k]) {
            zerof(fx_ret[k][0], 256);
            zerof(fx_ret[k][1], 256);
            if (snd_n)
                DSP_FAR(fx_process_lag)(FXS(k), snd_acc[k], fx_ret[k][0], fx_ret[k][1], snd_n, (int)snd_n);
        }
}
static void fx_job_timed(uint32_t arg)                     /* the effects' job (on either core) */
{
    uint32_t pt = plat_cycles();
    (void)arg;
    fx_job();
    PROF_ADD(PR_FXJ, pt);
}
/* the effects' job, when it runs on the second core, from the block's start: beside the first core's sequencer.
 * fx_done waits for it (or runs it here, if the second core failed) and sets the knobs locked meanwhile */
static void fx_done(void)
{
    int k;
    if (!fx_c2)
        return;
    if (c2_wait())
        fx_job_timed(0);
    fx_c2 = 0;
    for (k = 0; k < 2; k++)
        if (fx_dirty >> k & 1u)
            fx_set(FXS(k), fx_type[k], epar[AUX_FX1 + k] + TP_E1);
    fx_dirty = 0;
}
static volatile uint32_t c2_took;                          /* the last job's time (plat_cycles) */
/* the voices shared out as the cores go (render_sub): the stretch's voices in ws_ord (grouped by engine: each core
 * fetches an engine's code once), the second core taking from the bottom, the first from the top, until they meet;
 * neither decides the split ahead, so neither waits on the other's misjudged half (a whole knit voice is ~12 % of a
 * block: a split fixed ahead could miss by that, on 2026-10-19's FM-1 by 25 points). No atomics between the cores:
 * each writes its own count, syncs, then reads the other's (Dekker's order: one of two that took the same voice sees
 * it, and gives it back); a voice both gave back the first core renders after the wait. Core 1 checks the index it
 * reads (on some FM-1s its reads come back wrong now and then: loom_cpu1.h) */
static int8_t ws_ord[NVOICE];
static volatile int32_t ws_a, ws_b;                        /* taken: by the second core (the bottom), the first (the top) */
static int ws_take1(void)                                  /* the second core's next voice, -1 none */
{
    int32_t a = ws_a + 1, vi;
    ws_a = a;
    plat_cpu2_sync();
    if (a + ws_b > NVOICE || a > NVOICE) {
        ws_a = a - 1;
        plat_cpu2_sync();
        return -1;
    }
    vi = ws_ord[a - 1];
    return vi >= 0 && vi < NVOICE ? vi : -1;
}
static int ws_take0(void)                                  /* the first core's next voice, -1 none */
{
    int32_t b = ws_b + 1;
    ws_b = b;
    plat_cpu2_sync();
    if (ws_a + b > NVOICE) {
        ws_b = b - 1;
        plat_cpu2_sync();
        return -1;
    }
    return ws_ord[NVOICE - b];
}
static void core2_job(uint32_t arg)
{
    uint32_t n = arg & 0xFFu ? arg & 0xFFu : 256u, pt = plat_cycles(), p0 = pt;   /* (the clock: a timer both cores read) */
    int vi;
    zerof(c2_l, n);
    zerof(c2_r, n);
    zerof(c2_s1, n);
    zerof(c2_s2, n);
    while ((vi = ws_take1()) >= 0)
        synth_render_on(SY, 1, vi, vi + 1, c2_l, c2_r, c2_s1, c2_s2, n);
    PROF_ADD(PR_SYNTH1, pt);
    c2_took = plat_cycles() - p0;
}

/* (the host's load model's estimate of the split, app/host.c load_model; the FM-1 shares the voices out as the cores
 * go: render_sub) where the pool splits between the cores: the second renders voices [0, k), the first [k, NVOICE) and
 * the drum tracks, so k evens the two out. A free voice is the lowest one (synth.c find_voice): with a
 * fixed half, eight voices or fewer all landed on the second core while the first waited (and the
 * overload guard's eight did too). The cost in rough units: a synth voice 4 (brocade's more, by its saws,
 * and cane's bell about one and a quarter: synth_voice_cost), a drum voice 2, a drum track's own filter and mix 1 (tests/host: ~180, ~95 and ~50
 * instructions a sample). Of the k that
 * do best, the one nearest the middle (the old fixed split). The same k with one core or two: the
 * samples stay the same either way (only the order of the halves' sum depends on k). */
static uint8_t split_units[2], split_blk[4], split_last[3];   /* (the profiler: the block's first stretch's voices taken
                                                              * by the second core and the first, late; its last's) */
static uint32_t split_at(int extra2, int top)          /* voices [0, top) shared out: the second core's [0, k) */
{
    int pre[NVOICE + 1], i, k, t, drum = 0, best = top / 2, cost = 1 << 30;
    for (t = 0; t < NTRK; t++)
        if (eng_drum[t]) {
            int nv = drumline_voices(DL(t));
            if (nv)
                drum += 1 + 2 * nv + (epar[t][TP_CTYPE] != CT_OFF && epar[t][TP_CMIX]);   /* (+ its character) */
        }
    pre[0] = 0;
    for (i = 0; i < top; i++)
        pre[i + 1] = pre[i] + synth_voice_cost(SY, i);
    for (k = 0; k <= top; k++) {
        int a = pre[k] + extra2, b = pre[top] - pre[k] + drum, c = a > b ? a : b;
        int dk = k > top / 2 ? k - top / 2 : top / 2 - k, db = best > top / 2 ? best - top / 2 : top / 2 - best;
        if (c < cost || (c == cost && dk < db)) {
            cost = c;
            best = k;
            split_units[0] = (uint8_t)(b > 255 ? 255 : b);   /* (the profiler: the first core's, the second's) */
            split_units[1] = (uint8_t)(a > 255 ? 255 : a < 0 ? 0 : a);
        }
    }
    return (uint32_t)best;
}

__attribute__((noinline)) static void add_in(float *dst, const float *src, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        dst[i] += src[i];
}

/* the metronome: a short sine tick on the beat (higher on the bar), into the percussion group; the
 * sequencer says when (the sink's metro) */
static void metro_tick(uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n && metro_left; i++, metro_left--) {
        float env = (float)metro_left * (1.0f / 1800.0f), v = fm_sin_turns(metro_ph) * env * env * 0.5f;
        metro_ph += metro_inc;
        if (metro_ph >= 1.0f)
            metro_ph -= 1.0f;
        perc_l[i] += v;
        perc_r[i] += v;
    }
}

#ifdef PLAT_HOST
#define HOST_ON(b) (synth_host_solo >> (b) & 1u)  /* the host's stems (host.c LOOM_SOLO) */
#else
#define HOST_ON(b) 1
#endif
/* one stretch: the upper half of the voices and the drumlines here while the second core renders the
 * lower half; then the halves are summed in the order one core always used, the FX tracks, the master */
static void render_drums(uint32_t n)                    /* the drum tracks (the first core's: render_sub) */
{
    int t;
    PROF_T(pd);
    for (t = 0; t < NTRK; t++) {
        float g, gl, gr;
        uint32_t pt1;
        if (!eng_drum[t] || !drumline_sounding(DL(t)))
            continue;
        pt1 = plat_cycles();
        zerof(tl, n);
        zerof(tr, n);
        zerof(ts1, n);
        zerof(ts2, n);
        drumline_render(DL(t), tl, tr, ts1, ts2, n);
        char_bus(&dl_ch[dl_slot[t]], epar[t], tl, tr, (int)n);   /* the kit's character, after its bus filter */
        g = perf_mute[t] || !HOST_ON(t) ? 0.0f : tp_level_gain(epar[t][TP_LEVEL]);
        pan_gains(epar[t][TP_PAN] + perf_pan[t], &gl, &gr);
        glide_set(&trk_glide[t][GL_L], g * gl);
        glide_set(&trk_glide[t][GL_R], g * gr);
        glide_set(&trk_glide[t][GL_SEND], g);
        add_glide(perc_l, tl, &trk_glide[t][GL_L], n);
        add_glide(perc_r, tr, &trk_glide[t][GL_R], n);
        add_glide(s1, ts1, &trk_glide[t][GL_SEND], n);
        add_glide(s2, ts2, &trk_glide[t][GL_SEND], n);
        meter(t, tl, tr, g * gl, g * gr, n);
        eng_prof_drum[t] += plat_cycles() - pt1;
    }
    metro_tick(n);
    PROF_ADD(PR_DRUM, pd);
}
static void render_sub(uint32_t n, uint32_t at)
{
    uint32_t arg = n & 0xFFu, pt, ps;
    int on2 = 0, t, k, vi, i, late, late_at0, drums_left = 0;
    PROF_T(pp);
    eng_prof[PR_SUBS]++;
    zerof(perc_l, n);
    zerof(perc_r, n);
    zerof(mel_l, n);
    zerof(mel_r, n);
    zerof(s1, n);
    zerof(s2, n);
    perf_tick(n);
    synth_prepare(SY, n);                                 /* the shared per-chunk values, before either core renders */
    synth_order(SY, ws_ord);
    ws_a = ws_b = 0;
    plat_cpu2_sync();
    late = late_at0 = fx_c2 && plat_cpu2_busy();          /* the second core still at the effects' job (begun with the
                                                           * block): it joins when it is free (below) */
    if (!late) {
        fx_done();                                        /* (done already, or one core) */
        on2 = c2_run ? c2_run(core2_job, arg | 0x100u) : 0;
    }
    PROF_ADD(PR_PREP, pp);
    if (on2)                                              /* the second core at the voices: the drums here first */
        render_drums(n);
    ps = plat_cycles();
    while ((vi = ws_take0()) >= 0) {
        synth_render_on(SY, 0, vi, vi + 1, mel_l, mel_r, s1, s2, n);
        if (late && !plat_cpu2_busy()) {                  /* free now: it takes from the bottom */
            late = 0;
            fx_done();
            on2 = c2_run ? c2_run(core2_job, arg | 0x100u) : 0;
            drums_left = 1;
        }
    }
    PROF_ADD(PR_SYNTH0, ps);
    if (!on2 || drums_left)
        render_drums(n);
    pt = plat_cycles();
    fx_done();                                            /* (the voices all done here first: the effects' job) */
    if (on2 && c2_wait()) {                               /* the second failed: what it took, here (it may not have) */
        zerof(c2_l, n);
        zerof(c2_r, n);
        zerof(c2_s1, n);
        zerof(c2_s2, n);
        for (i = 0; i < ws_a && i < NVOICE; i++)
            synth_render_on(SY, 0, ws_ord[i], ws_ord[i] + 1, c2_l, c2_r, c2_s1, c2_s2, n);
    } else if (on2) {
        PROF_ADD(PR_WAIT, pt);                            /* what the first core waited for the second */
    } else {
        zerof(c2_l, n);
        zerof(c2_r, n);
        zerof(c2_s1, n);
        zerof(c2_s2, n);
    }
    for (i = ws_a; i < NVOICE - ws_b; i++)                /* any both gave back */
        synth_render_on(SY, 0, ws_ord[i], ws_ord[i] + 1, mel_l, mel_r, s1, s2, n);
    split_last[0] = (uint8_t)ws_a;                        /* (the profiler: the voices each core took) */
    split_last[1] = (uint8_t)ws_b;
    split_last[2] = (uint8_t)(late_at0 ? 128u : 0u);
    if (at == 0) {
        split_blk[0] = split_last[0];
        split_blk[1] = split_last[1];
        split_blk[2] = split_last[2];
    }
    pt = plat_cycles();
    add_in(mel_l, c2_l, n);
    add_in(mel_r, c2_r, n);
    add_in(s1, c2_s1, n);
    add_in(s2, c2_s2, n);
    for (t = 0; t < NTRK; t++)                            /* the synth's meters, into the common ones */
        if (!eng_drum[t]) {
            float v = (float)synth_peak[t] * (1.0f / 16383.5f);
            if (v > trk_peak[t])
                trk_peak[t] = v;
        }
    PROF_ADD(PR_MIX, pt);
    {
        for (k = 0; k < 2; k++) {                         /* (timed apart: FX I, FX II) */
            PROF_T(pf);                         /* FX I, then FX II (which hears FX I's return) */
            float g = HOST_ON(8 + k) ? tp_level_gain(epar[AUX_FX1 + k][TP_LEVEL]) : 0.0f;
            float *rl = tl, *rr = tr;
            if (fx_lag_on[k]) {                           /* run a block behind (fx_job): its return for this stretch */
                rl = fx_ret[k][0] + at;
                rr = fx_ret[k][1] + at;
            } else {
                zerof(tl, n);
                zerof(tr, n);
                DSP_FAR(fx_process)(FXS(k), k ? s2 : s1, tl, tr, n);
            }
            glide_set(&ret_glide[k][0], g);
            glide_set(&ret_glide[k][1], g);
            add_glide(mel_l, rl, &ret_glide[k][0], n);
            add_glide(mel_r, rr, &ret_glide[k][1], n);
            meter(AUX_FX1 + k, rl, rr, g, g, n);
            if (k == 0 && epar[AUX_FX1][TP_SEND2]) {      /* FX I into FX II: the FX I track's second send */
                float s = (float)epar[AUX_FX1][TP_SEND2] * (1.0f / 127.0f) * 0.5f;
                uint32_t i;
                for (i = 0; i < n; i++)
                    s2[i] += (rl[i] + rr[i]) * s;
            }
            PROF_ADD(k ? PR_FX2 : PR_FX, pf);
        }
        for (k = 0; k < (int)n; k++) {                    /* the sends, gathered for the next block's effects */
            snd_acc[0][at + (uint32_t)k] = s1[k];
            snd_acc[1][at + (uint32_t)k] = s2[k];
        }
    }
}

#define VOL_SLEW 0.0045f                                 /* MASTER's glide: ~5 ms to most of a turn (1 - e^-1 in 220 samples) */
static float vol_s;                                      /* MASTER's gain as it glides (the render only) */
_Static_assert(SY_GLFO_TRK == AUX_BRAIN, "the shared LFO's knobs are the conductor's");
float engine_glfo_now(void) { return synth_glfo_now(SY); }
/* the shared LFO on the kits (synth.h GD_*; the synth moves the melodic tracks itself): once a block, over the engine's
 * own copy of the kit's knobs (epar, what drumline reads: at a hit, or each chunk for the filter), each value
 * modulated kept beside its base (the knob's, or a lock's: a write by anyone else since is the new base) and put
 * back when the LFO lets go of it. The conductor's TP_P1 picks the sound: 0 every key, 1..11 that key. Pitch is
 * not here: s_drum adds it to the hit's tuning */
static struct { int16_t p; uint8_t base, last; } gk[NTRK][NDRUM + 1];   /* (the kit's character: slot NDRUM) */
static void glfo_kits(void)
{
    const uint8_t *g = epar[AUX_BRAIN];
    static const uint8_t DPD[NGD] = {DP_CUT, DP_RES, 0xFF, DP_PAN, DP_VOLUME, DP_SEND1, DP_SEND2, 0xFF};
    int route = (g[TP_LX] & 15) | (g[TP_LX2] & 15) << 4, dest = g[TP_LDEST] % NGD, snd = g[TP_P1];
    int on = g[TP_LTYPE] != 0 && g[TP_LTYPE] <= NGS;
    float off = synth_glfo_now(SY) * fm_clampf(((float)g[TP_LAMT] - 64.0f) * (127.0f / 63.0f), -127.0f, 127.0f);
    for (int t = 0; t < NTRK; t++)
        for (int k = 0; k <= NDRUM; k++) {
            int want = -1;
            if (on && eng_drum[t] && (route >> t & 1)) {
                if (k == NDRUM)
                    want = dest == GD_CHAR ? TP_CAMT : -1;
                else if (DPD[dest] != 0xFF && (!snd || snd == k + 1))
                    want = TP_DRUM(k, DPD[dest]);
            }
            if (gk[t][k].p && gk[t][k].p - 1 != want) {    /* let go (or another destination): the base back */
                int q = gk[t][k].p - 1;
                if (epar[t][q] == gk[t][k].last)
                    epar[t][q] = gk[t][k].base;
                gk[t][k].p = 0;
            }
            if (want < 0)
                continue;
            if (!gk[t][k].p || epar[t][want] != gk[t][k].last) {   /* (fresh, or written since: its new base) */
                gk[t][k].p = (int16_t)(want + 1);
                gk[t][k].base = epar[t][want];
            }
            {
                int v = (int)((float)gk[t][k].base + off + (off >= 0.0f ? 0.5f : -0.5f));
                epar[t][want] = gk[t][k].last = (uint8_t)(v < 0 ? 0 : v > 127 ? 127 : v);
            }
        }
}
static int glfo_kit_semis(int trk, int sound)            /* the shared LFO on a kit's pitch: semitones for a hit */
{
    const uint8_t *g = epar[AUX_BRAIN];
    int route = (g[TP_LX] & 15) | (g[TP_LX2] & 15) << 4, snd = g[TP_P1];
    float off;
    if (!g[TP_LTYPE] || g[TP_LTYPE] > NGS || g[TP_LDEST] % NGD != GD_PITCH || !(route >> trk & 1) ||
        (snd && snd != sound + 1))
        return 0;
    off = synth_glfo_now(SY) * fm_clampf(((float)g[TP_LAMT] - 64.0f) * (127.0f / 63.0f), -127.0f, 127.0f);
    return (int)(off * (12.0f / 127.0f) + (off >= 0.0f ? 0.5f : -0.5f));   /* (+-12 semitones at 100 %) */
}
void engine_render(int32_t *out_lr, uint32_t n, uint32_t gain_q12)
{
    uint32_t done = 0, i;
    float vol = (float)gain_q12 * (1.0f / 4096.0f);
    int p;
    static uint8_t was_playing;
    PROF_T(pa);
    if (seq.playing && !was_playing)                     /* the transport started: the shared LFO from its start */
        synth_glfo_restart(SY);
    was_playing = seq.playing;
    glfo_kits();
    if (n > 256u)
        n = 256u;
    eng_prof_frames += n;
    guard_frames += n;
    for (i = 0; i < NVOICE; i++) {                       /* (the profiler: each voice's time this block) */
        synth_vblk[i] = 0;
        synth_vinfo[i] = 0;
    }
    guard_apply();
    {
        PROF_T(pt);
        float bpm;
        drain();
        PROF_ADD(PR_DRAIN, pt);
        bpm = seq_tempo(&seq);
        synth_tempo(SY, bpm);
        fx_tempo(FXS(0), bpm);
        fx_tempo(FXS(1), bpm);
        fx_lag_on[0] = (uint8_t)fx_lags(FXS(0));         /* (the type as set: fx_process takes it at its next call) */
        fx_lag_on[1] = (uint8_t)fx_lags(FXS(1));
        if (fx_lag_on[0] || fx_lag_on[1]) {               /* the effects' job (the last block's sends) now, on the
                                                           * second core beside the sequencer; else here */
            if (c2_run && c2_run(fx_job_timed, 0x100u))
                fx_c2 = 1;
            else
                fx_job_timed(0);
        }
        seq_advance(&seq, 0, &SINK);
        PROF_ADD(PR_SEQ, pt);
    }
    while (done < n) {
        uint32_t k = seq_until_event(&seq, n - done);
        if (k > n - done)
            k = n - done;
        if (!k)
            k = 1;
        render_sub(k, done);
        {
            PROF_T(pm);
            DSP_FAR(master_process)(MST, perc_l, perc_r, mel_l, mel_r, mix_l + done, mix_r + done, k);
            PROF_ADD(PR_MST, pm);
        }
        {
            PROF_T(pt);
            seq_advance(&seq, k, &SINK);
            PROF_ADD(PR_SEQ, pt);
        }
        done += k;
    }
    snd_n = n;                                           /* this block's sends: the next block's effects' */
    eng_scene_cue = seq.cue;
    eng_pump_db = master_pump_db(MST);
    PROF_T(po);
    {   /* quiet (the autosave waits for it: a flash write cuts the audio): every sample under -60 dB */
        float pk = 0.0f;
        for (i = 0; i < n; i++)
            pk = fm_maxf(pk, fm_maxf(fm_fabsf(mix_l[i]), fm_fabsf(mix_r[i])));
        eng_quiet = pk > 0.001f ? 0 : (uint16_t)(eng_quiet < 65535u ? eng_quiet + 1u : 65535u);
    }
    for (i = 0; i < n; i++) {                            /* Felucca's -6 dBFS ceiling of the 24-bit codec (2^22) */
        float l, r;
        vol_s += (vol - vol_s) * VOL_SLEW;               /* MASTER's gain glides (a block's step would zipper) */
        l = mix_l[i] * vol_s;
        r = mix_r[i] * vol_s;
        l = l > 2.0f ? 2.0f : l < -2.0f ? -2.0f : l;
        r = r > 2.0f ? 2.0f : r < -2.0f ? -2.0f : r;
        out_lr[2u * i] = (int32_t)(l * 4194303.0f);
        out_lr[2u * i + 1u] = (int32_t)(r * 4194303.0f);
        if (i & 1u)
            eng_scope[eng_scope_w++ & (SCOPE_N - 1u)] = (int16_t)((mix_l[i] + mix_r[i]) * 16383.5f);
    }
    for (p = 0; p < NALLTRK; p++) {                      /* meters: the loudest since the UI took them (Q14, to 2) */
        uint32_t q = (uint32_t)(fm_minf(trk_peak[p], 2.0f) * 16383.5f);
        if (q > eng_peak[p])
            eng_peak[p] = (uint16_t)q;
        trk_peak[p] = 0.0f;
    }
    eng_voices = (uint8_t)synth_voices_used(SY);
    PROF_ADD(PR_OUT, po);
    PROF_ADD(PR_ALL, pa);
    {   /* the slowest block's stages since the reader asked (its whole render the measure) */
        uint32_t d = eng_prof[PR_ALL] - prof_block0[PR_ALL];
        if (eng_prof_reset) {
            eng_prof_reset = 0;
            eng_prof_worst[PR_ALL] = 0;
        }
        {   /* the trace: each block's main stages in permille of its budget, the last TRACE_N */
            static const uint8_t K[TRACE_K - 2] = {PR_ALL, PR_SYNTH0, PR_SYNTH1, PR_SEQ, PR_DRUM, PR_NOTE};
            uint32_t bud = n * 544u, w = trace_w % TRACE_N, j;   /* (24 MHz / 44.1 kHz = 544.2 ticks a frame) */
            for (j = 0; j < TRACE_K - 2; j++)
                trace[w][j] = (uint16_t)((eng_prof[K[j]] - prof_block0[K[j]]) * 1000u / bud);
            trace[w][TRACE_K - 2] = (uint16_t)(eng_prof[PR_SUBS] - prof_block0[PR_SUBS]);
            trace[w][TRACE_K - 1] = (uint16_t)synth_voices_used(SY);
            trace_w++;
            if (d > eng_prof_worst[PR_ALL])          /* the slowest block: the trace around it kept, 4 blocks on */
                trace_take = trace_w + 4u;
            if (trace_w == trace_take)
                for (j = 0; j < TRACE_N; j++)
                    for (p = 0; p < TRACE_K; p++)
                        trace_worst[j][p] = trace[(trace_w + j) % TRACE_N][p];
        }
        if (d > eng_prof_worst[PR_ALL]) {
            uint32_t bud = n * 544u;
            for (p = 0; p < ENG_PROF_N; p++)
                eng_prof_worst[p] = eng_prof[p] - prof_block0[p];
            for (i = 0; i < NVOICE; i++)                 /* its voices: time (permille), engine and flags */
                eng_vworst[i] = (synth_vblk[i] * 1000u / bud) | (uint32_t)synth_vinfo[i] << 16;
            eng_vworst[NVOICE] = (uint32_t)split_blk[0] | (uint32_t)split_blk[1] << 8 | (uint32_t)split_blk[2] << 16 |
                                 (eng_prof[PR_SUBS] - prof_block0[PR_SUBS]) << 24;
            eng_vworst[NVOICE + 1] = (uint32_t)split_last[0] | (uint32_t)split_last[1] << 8 |
                                     (uint32_t)split_last[2] << 16;
        }
        for (p = 0; p < ENG_PROF_N; p++)
            prof_block0[p] = eng_prof[p];
    }
}

/* ----------------------------------------------------------------- init --- */
void engine_init(patterns_t *P, uint8_t (*par)[NTP])
{
    int t, k;
    ppar = par;
    for (t = 0; t < NALLTRK; t++) {
        for (k = 0; k < NTP; k++)
            epar[t][k] = par[t][k];
        for (k = 0; k < NTP / 8 + 1; k++)
            lock_on[t][k] = 0;
    }
    synth_clock = plat_cycles;
    {
        extern uint32_t (*seq_clock)(void);
        extern volatile uint32_t *seq_prof;
        seq_clock = plat_cycles;
        seq_prof = &eng_prof[PR_SIX];
    }
    synth_init(SY, par);
    for (k = 0; k < NDL; k++)
        dl_owner[k] = 0xFF;
    for (t = 0; t < NTRK; t++) {
        dl_slot[t] = 0xFF;
        dl_punch_hp[t] = 0;
        perf_pan_base[t] = 0;
        eng_drum[t] = (uint8_t)(par[t][TP_ENGINE] == EG_DRUMLINE && dl_take(t));
        if (eng_drum[t]) {
            drumline_init(DL(t), epar[t]);
            dl_filter_update(t);
        }
        for (k = 0; k < GL_N; k++) {
            trk_glide[t][k].cur = -1.0f;
            trk_glide[t][k].tgt = 0.0f;
        }
        bend_c[t] = 0;
    }
    for (k = 0; k < 2; k++)
        ret_glide[k][0].cur = ret_glide[k][1].cur = -1.0f;
    fx_init(FXS(0), dly1_l, dly1_r, 0);                   /* FX I: the delay lines, no reverb */
    fx_init(FXS(1), 0, 0, rv2_line);                      /* FX II: the reverb line, no delay */
    fx_type[0] = epar[AUX_FX1][TP_ENGINE];
    fx_type[1] = epar[AUX_FX2][TP_ENGINE];
    fx_set(FXS(0), fx_type[0], epar[AUX_FX1] + TP_E1);
    fx_set(FXS(1), fx_type[1], epar[AUX_FX2] + TP_E1);
    master_init(MST);
    seq_init(&seq, P, par);
    seq.groove_type = (uint8_t)(proj.groove < NGROOVE ? proj.groove : GR_SWING);
    seq.brain_link = (uint8_t)(par[AUX_BRAIN][TP_E4] > NTRK ? NTRK : par[AUX_BRAIN][TP_E4]);
    for (t = 0; t < NSEQT; t++)
        eng_step[t] = 0;
    eng_voices_max = NVOICE;
}

/* the UI asks before offering an effect: FX I has no reverb line, FX II no delay (the memory plan) */
int engine_fx_has(int slot, int pick) { return fx_has(FXS(slot & 1), pick); }
