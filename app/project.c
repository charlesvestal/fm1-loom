/* SPDX-License-Identifier: MPL-2.0 */
/* LOOM-8 project: the settings (tempo, tracks' sounds and mix, master), the patterns, lanes, scenes
 * and song, and the user presets; three slots in flash.
 *
 * The settings are one storage object, stored raw (small, validated on load). The patterns, lanes,
 * scenes and song (~64 KB, mostly empty steps) are one stream packed with lz.c across the slot's
 * other six objects (X0X's project.c). The first carries the raw length and a CRC of the raw bytes,
 * so a stream torn by a power cut between two objects, or one that does not unpack, is never loaded
 * as garbage: the objects' previous copies (storage.c keeps two) are tried instead, which give the
 * save before; only when no mix of copies checks out, the defaults. A save packs once to measure
 * (nothing is written if it would not fit: "MEMORY FULL") and once to write; an object whose bytes
 * did not change is not written again.
 *
 * Aux tracks keep their settings in the same par[] as the instruments: FX I / II: TP_ENGINE the
 * effect (a pick, fx.h: the effect and the reverb's mode), TP_E1..E4 its four knobs, TP_FENV its
 * high-pass, TP_CUT its low-pass, TP_LEVEL the return, TP_SEND2 of FX I its feed into FX II; BRAIN:
 * TP_E1..E4 mode, key, scale, degree; PUNCH: TP_E1 latch.
 * Part of the unity build, after app.h and engine.h. */
#include <stddef.h>
#include "app.h"
#include "engine.h"
#include "dsp/drumline.h"
#include "lz.c"

settings_t proj;
uint8_t proj_par[NALLTRK][NTP] PLAT_POOL;
patterns_t pats PLAT_POOL;
user_preset_t user_presets[NUSER_PRESETS];
char user_kit_name[NUSER_KITS][NAME_LEN];
uint8_t proj_slot;

#define PROJ_STREAM 2u                  /* the format word of the packed objects */
#define NSTREAM (PROJ_NOBJ - 1)
#define STREAM_HDR 16u                  /* in the first object: format, raw length, raw CRC, packed length */
#define STREAM_CAP ((uint32_t)NSTREAM * (PLAT_STORE_MAX - 4u) - (STREAM_HDR - 4u))
#define RAW_LEN sizeof(patterns_t)
/* formats 12..17: the store without the kits' hit settings (pats.hit, last: none then) */
#define RAW_LEN17 (sizeof(patterns_t) - sizeof pats.hit)
/* format 10 (0.5-beta): the same store but 16 scenes of scene10_t, the song after them */
#define RAW_LEN10 (RAW_LEN17 - sizeof pats.scene + NSCENE10 * sizeof(scene10_t))
/* format 11: 32 scenes of scene11_t (one kit mute mask for every kit) */
#define RAW_LEN11 (RAW_LEN17 - sizeof pats.scene + NSCENE * sizeof(scene11_t))
typedef char scene_sizes[(sizeof(scene10_t) == 18 && sizeof(scene11_t) == 20 && sizeof(scene_t) == 26) ? 1 : -1];
typedef char songs_apart[(NSCENE10 * sizeof(scene10_t) + sizeof(song_t) <= NSCENE * sizeof(scene_t) &&
                          NSCENE * sizeof(scene11_t) + sizeof(song_t) <= NSCENE * sizeof(scene_t)) ? 1 : -1];   /* (an
                                                   * old song's bytes end before the new song's start) */
static uint8_t load_format;                       /* the format of the project being loaded (10, 11: migrated) */
static uint8_t migrated = 0xFF;                   /* the slot loaded from an older format and not saved since (0xFF
                                                   * none): its next touch saves it whole, never its settings alone
                                                   * beside the old format's patterns */
#define FORMAT_OK(f) ((f) >= 10u && (f) <= PROJ_FORMAT)   /* the formats a slot loads (migrate_old, before 13 glfo_defaults) */
typedef char raw_fits[(RAW_LEN < 0x100000u) ? 1 : -1];   /* (lz.c packs up to 16 MB; the header's length is a u32) */
/* the settings object (format 15 on): the settings before the tracks as they are (SET_HDR), the packed tracks' length,
 * then the tracks packed (lz.c: a synth track's kit keys and an aux track's sound are mostly defaults). Formats 10..14
 * stored them raw, each track's par[] TP_DRUMX0 long (the keys' knobs 5..8 came in 15): SET_OLD bytes in all */
#define SET_HDR ((uint32_t)offsetof(settings_t, trk))
#define NTP14 ((uint32_t)TP_DRUMX0)
#define TRK14 (NTP14 + NAME_LEN + 4u)
#define SET_OLD ((SET_HDR + NALLTRK * TRK14 + 3u) & ~3u)   /* (the struct padded to its 4-byte alignment) */
#define NTP15 ((uint32_t)TP_O2FINE)                       /* format 15's par[]: up to the keys' knobs 5..8 */
#define TRK15 (NTP15 + NAME_LEN + 4u)
#define NTP18 ((uint32_t)TP_RUP)                          /* formats 16..18's: up to knit's osc 2 fine */
/* the random player's one octave knob before 19 (TP_P4, 0..24: PR #13's path about its centre) as down * 4 + up */
static const uint8_t OLD_OCT[25] = {15, 11, 7, 14, 10, 6, 13, 9, 5, 12, 8, 4, 0, 1, 2, 3, 5, 6, 7, 9, 10, 11, 13, 14, 15};
typedef char settings_head_fits[(SET_HDR + 4u + 64u <= PLAT_STORE_MAX && SET_OLD == 3804u) ? 1 : -1];   /* (format 14's size) */
typedef char trk_layout[(offsetof(trk_t, name) == NTP && sizeof(trk_t) == NTP + NAME_LEN + 4u) ? 1 : -1];
typedef char preset_packed[(sizeof(user_preset_t) == NAME_LEN + 2u + NTP_SOUND) ? 1 : -1];   /* (no padding) */
#define PRESET_OLD (NAME_LEN + 1u + NTP_SOUND)                    /* formats 6..8: a user preset without its fine */
#define PRESETS_V7 (4u + NUSER_PRESETS * PRESET_OLD)              /* 6, 7: the user presets alone */
#define OLD_KITS 4u                                               /* formats 8..10: four user kits */
#define PRESETS_V8 (PRESETS_V7 + OLD_KITS * KIT_LEN)              /* 8: the user kits after them */
#define PRESETS_KITS (4u + (uint32_t)sizeof user_presets)         /* 9: where the kits start */
#define PRESETS_LEN (PRESETS_KITS + NUSER_KITS * KIT_LEN)
#define PRESETS_V10 (PRESETS_KITS + OLD_KITS * KIT_LEN)          /* 9, 10: four kits */
typedef char presets_fit[(PRESETS_LEN <= PLAT_STORE_MAX && PRESETS_V7 == 836u) ? 1 : -1];
#define PRESETS_FORMAT 11u               /* 11: eight user kits (10 and 9 held four, loaded as K1..K4);
                                          * 10: as 9, its kits' keys on the new models (11..20: keys_fix leaves them);
                                          * 9: knit's osc 2 fine (user_preset_t o2fine), the kits after; 8: the user kits;
                                          * 7: the filter type off (FT_DYNAMIC) moved past bp12 (TP_FTYPE 4/5 swap, below);
                                          * 6: the octave (TP_OCT); 5: the engine's second page (TP_X1..X4); 4: no tape send (TP_SENDT);
                                          * 3: the character stage; 2: engine 0 is weft */

static uint32_t packed_len;             /* the last save's (or load's) packed stream, bytes */
int project_mem_pct(void) { return (int)((packed_len * 100u + STREAM_CAP - 1u) / STREAM_CAP); }

/* ---- defaults ------------------------------------------------------------------------------- */

static void apply_preset(trk_t *t, int k)
{
    int i;
    if (k < 0 || k >= NPRESETS)
        return;
    for (i = 0; i < NTP_SOUND; i++)
        t->par[i] = PRESETS[k].par[i];
    t->par[TP_O2FINE] = 64;
    for (i = 0; i < NAME_LEN - 1 && PRESETS[k].name[i]; i++)
        t->name[i] = PRESETS[k].name[i];
    t->name[i] = 0;
}

static int preset_named(int engine, const char *word)   /* the engine's first preset whose name has the word */
{
    int k, first = preset_first(engine);
    for (k = 0; k < NPRESETS; k++) {
        const char *n = PRESETS[k].name, *w;
        int j;
        if (PRESETS[k].par[TP_ENGINE] != engine)
            continue;
        for (j = 0; n[j]; j++) {
            int m = 0;
            for (w = word; w[m] && n[j + m]; m++) {
                char a = n[j + m], b = w[m];
                if (a >= 'A' && a <= 'Z')
                    a = (char)(a + 32);
                if (a != b)
                    break;
            }
            if (!w[m])
                return k;
        }
    }
    return first;
}

static void set_name(trk_t *t, const char *s)
{
    int i;
    for (i = 0; i < NAME_LEN - 1 && s[i]; i++)
        t->name[i] = s[i];
    t->name[i] = 0;
}

/* the shared LFO's knobs (synth.h: the conductor's LFO slots) at their start: off, 1 bar, no amount, the cutoff,
 * no track, on a kit every sound */
static void glfo_defaults(uint8_t *p)
{
    p[TP_LTYPE] = 0;
    p[TP_LSPEED] = 4;
    p[TP_LAMT] = 64;
    p[TP_LDEST] = GD_CUT;
    p[TP_LX] = p[TP_LX2] = 0;
    p[TP_P1] = 0;                                         /* (on a kit: every sound) */
}
static void aux_defaults(void)
{
    int t, k;
    for (t = NTRK; t < NALLTRK; t++) {
        uint8_t *p = proj.trk[t].par;
        sound_default(p, EG_KNIT);                       /* mix and sends at their defaults */
        for (k = TP_ENGINE; k <= TP_E4; k++)
            p[k] = 0;
        p[TP_LEVEL] = 100;
        p[TP_PAN] = 64;
        p[TP_SEND1] = p[TP_SEND2] = 0;
        p[TP_CHAN] = 0;
        p[TP_BRAIN] = 0;
    }
    glfo_defaults(proj.trk[AUX_BRAIN].par);
    set_name(&proj.trk[AUX_BRAIN], "conductor");
    set_name(&proj.trk[AUX_PUNCH], "throws");
    set_name(&proj.trk[AUX_FX1], "delay");
    set_name(&proj.trk[AUX_FX2], "reverb");
    /* the notebook's default project: FX I a delay, FX II the lush reverb, each with its printed defaults
     * (fx.h FXT_DEFAULT, RV_LUSH_DEFAULT); the FX I track's second send feeds its return into FX II */
    proj.trk[AUX_FX1].par[TP_ENGINE] = FXT_DELAY;
    proj.trk[AUX_FX2].par[TP_ENGINE] = FXT_REVERB + RV_LUSH;
    for (k = 0; k < 4; k++) {
        proj.trk[AUX_FX1].par[TP_E1 + k] = FXT_DEFAULT[FXT_DELAY][k];
        proj.trk[AUX_FX2].par[TP_E1 + k] = RV_LUSH_DEFAULT[k];
    }
    proj.trk[AUX_FX1].par[TP_CUT] = 127;                 /* low-pass open */
    proj.trk[AUX_FX1].par[TP_FENV] = 40;                 /* the high-pass: the echoes from 130 Hz (no mud) */
    proj.trk[AUX_FX2].par[TP_CUT] = 127;
    proj.trk[AUX_FX2].par[TP_FENV] = 52;                 /* the reverb from 230 Hz: the kick and the bass stay dry */
}

/* a new band: T2-T8 each a random factory preset of a part, so a project starts as one: a bass, a polyphonic synth,
 * a pad, a mono lead, keys, an effect, a pluck or bell. Sample and slice presets are left out (they need the bank's
 * sounds); the kit (T1) stays. The tracks keep their mix, player and following (a preset is the sound part) */
void project_random_sounds(uint32_t seed)
{
    static const uint8_t ROLES[NTRK - 1][3] = {
        {RL_BASS, RL_BASS, RL_BASS}, {RL_CHORD, RL_STAB, RL_BRASS}, {RL_PAD, RL_PAD, RL_STRINGS},
        {RL_LEAD, RL_LEAD, RL_LEAD}, {RL_KEYS, RL_KEYS, RL_KEYS}, {RL_FX, RL_FX, RL_FX}, {RL_PLUCK, RL_BELL, RL_VOICE}};
    static const uint8_t VMODE[NTRK - 1] = {VM_MONO, VM_POLY, VM_POLY, VM_MONO, VM_POLY, 0xFF, 0xFF};   /* 0xFF: its own */
    uint32_t r = seed ? seed : 0x9E3779B9u;
    for (int t = 1; t < NTRK; t++) {
        int n = 0, k, pick = -1;
        const uint8_t *rl;
        r ^= r << 13, r ^= r >> 17, r ^= r << 5;
        rl = ROLES[t - 1];
        for (k = 0; k < NPRESETS; k++) {                 /* reservoir: one of the matching presets, evenly */
            int eg = PRESETS[k].par[TP_ENGINE], role = PRESETS[k].role;
            if (eg == EG_SAMPLE || eg == EG_SLICE || eg == EG_DRUMLINE ||
                (role != rl[0] && role != rl[1] && role != rl[2]))
                continue;
            n++;
            r ^= r << 13, r ^= r >> 17, r ^= r << 5;
            if (r % (uint32_t)n == 0)
                pick = k;
        }
        if (pick < 0)
            continue;
        apply_preset(&proj.trk[t], pick);
        if (VMODE[t - 1] != 0xFF && PRESETS[pick].par[TP_ENGINE] != EG_ACID)   /* (acid: its one mode) */
            proj.trk[t].par[TP_VMODE] = VMODE[t - 1];
    }
}

/* the FM-1's own settings, not a project's: Setup's list (the lights, the clock, MIDI notes, USB audio, the autosave,
 * loop throws, CC OUT, SCENES OUT, Bluetooth) and the theme. A new project starts with them and a project loaded
 * takes them (app.h project_device_get / put around the load) */
#define DEV_BITS (4u | 8u | 16u | 32u | 64u | 128u)   /* rsv[2]: pages.c AUTOSAVE_OFF, LOOP_FREE, CC_OUT_OFF, SCENES_OUT,
                                                        * bt.c BT_AUDIO_ON, BT_MIDI_OFF */
#define DEV_BITS3 1u                                   /* rsv[3]: pages.c CC_IN_ENDLESS */
static struct { uint8_t theme, bits, bits3, lights, clock, notes, usb; } dev;
void project_device_get(void)
{
    dev.theme = proj.rsv[1];
    dev.bits = (uint8_t)(proj.rsv[2] & DEV_BITS);
    dev.bits3 = (uint8_t)(proj.rsv[3] & DEV_BITS3);
    dev.lights = proj.lights;
    dev.clock = proj.clock;                               /* (Setup sets them and seq's together) */
    dev.notes = proj.send_notes;
    dev.usb = proj.usb_full;
}
void project_device_put(void)
{
    proj.rsv[1] = dev.theme;
    proj.rsv[2] = (uint8_t)((proj.rsv[2] & ~DEV_BITS) | dev.bits);
    proj.rsv[3] = (uint8_t)((proj.rsv[3] & ~DEV_BITS3) | dev.bits3);
    proj.lights = dev.lights;
    proj.clock = dev.clock;
    proj.send_notes = dev.notes;
    proj.usb_full = dev.usb;
}

void project_defaults(void)
{
    static const uint8_t ORDER[NTRK] = {EG_DRUMLINE, EG_KNIT, EG_RAYON, EG_JACQUARD, EG_KNIT, EG_BELLOWS, EG_CANE,
                                        EG_VOILE};
    int t, k;
    uint8_t *z = (uint8_t *)&proj, keep = proj.magic == PROJ_MAGIC;   /* (a project before it: the FM-1's settings
                                                                     * carried over; at the first start, none) */
    if (keep)
        project_device_get();
    for (k = 0; k < (int)sizeof proj; k++)
        z[k] = 0;
    proj.magic = PROJ_MAGIC;
    proj.format = PROJ_FORMAT;
    proj.name[0] = 'N';
    proj.name[1] = 'E';
    proj.name[2] = 'W';
    proj.name[3] = 0;
    proj.tempo_x10 = 1200;
    proj.swing = 0;                                       /* unused (a reserved byte) */
    proj.groove = GR_SWING;                               /* the project's groove type; each track's amount is 0 */
    proj.metronome = 1;                                   /* while recording */
    proj.lights = 1;
    proj.clock = SEQ_CLK_RECEIVE;                         /* follows a MIDI clock in (0.7: sent clock, followed one) */
    proj.send_notes = 1;                                  /* MIDI notes out (each track on its channel: below) */
    proj.one_core = 0;
    proj.usb_full = 0;
    if (keep)
        project_device_put();
    proj.scene = 0;
    proj.trk_sel = 0;
    for (t = 0; t < NTRK; t++) {
        trk_t *tr = &proj.trk[t];
        sound_default(tr->par, ORDER[t]);
        if (ORDER[t] == EG_DRUMLINE) {
            for (k = 0; k < DL_NSOUND; k++) {
                int j;
                for (j = 0; j < DP_N; j++)
                    tr->par[TP_DRUM(k, j)] = DL_DEFAULT_KIT[k][j];
            }
            tr->par[TP_A] = 0;                            /* the track-wide drum envelope, neutral (drumline.c): */
            tr->par[TP_D] = 0;                            /* funk 0, attack 0, random 0, length 64 */
            tr->par[TP_S] = 0;
            tr->par[TP_R] = 64;
            set_name(tr, "kit");
        } else if (t == 1) {
            apply_preset(tr, preset_named(EG_KNIT, "sub"));     /* knit's round sub under everything */
        } else {
            apply_preset(tr, preset_first(ORDER[t]));
        }
        if (!tr->name[0])
            set_name(tr, EG_NAME[ORDER[t]]);
        tr->par[TP_ENGINE] = ORDER[t];                    /* (a preset carries its engine; the kit keeps its own) */
        tr->par[TP_BRAIN] = t ? 1 : 0;                    /* the melodic tracks follow the brain from the start
                                                           * (the notebook: "several tracks are routed by default") */
        tr->par[TP_CHAN] = 1;                             /* MIDI OUT on: track N's notes on channel N
                                                           * (with Setup's NOTES) */
    }
    aux_defaults();
    /* the master: the EQ flat, the color off (tape when on: amount 40, tone 64, all wet), the groups at unity, a touch
     * of compression */
    for (k = 0; k < NMS; k++)
        proj.master[k] = 64;
    proj.master[MS_RSV3] = 0;
    proj.master[MS_CTYPE] = MC_OFF;
    proj.master[MS_CAMT] = 40;
    proj.master[MS_CMIX] = 127;
    proj.master[MS_PERC] = 100;
    proj.master[MS_MEL] = 100;
    proj.master[MS_COMP] = 30;
    proj.master[MS_PUMP] = 0;                             /* the pump off; when on: by the kick, X0X's release, */
    proj.master[MS_PUMP_BY] = PB_KICK;                    /* accented hits deeper */
    proj.master[MS_PUMP_REL] = MS_PUMP_REL_DEF;
    proj.master[MS_PUMP_ACC] = 1;
    proj.master[MS_LEVEL] = 100;
    patterns_init(&pats);
}

/* ---- validation ----------------------------------------------------------------------------- */

/* the settings came whole (CRC): what the engine indexes with is checked, a knob out of its range
 * (a build whose ranges moved) is clamped, not a reason to lose the project */
/* development builds had 21 more kit models (11..31, removed 2026-10-23): a key saved with one, in a project before format
 * 17 or a kit before presets format 10 (dev: those numbers are now the x models, sophie and clang), gets the built-in model
 * for its part, from its defaults (an open hat's long). Any other number past the models: a kick */
static void keys_fix(uint8_t *p, int dev)
{
    static const uint8_t PART[21] = {DM_KICK, DM_TOM, DM_CONGA, DM_FM, DM_SNARE, DM_CLAP, DM_KICK, DM_HIHAT,
                                     DM_HIHAT, DM_HIHAT, DM_CLAP, DM_SHAKER, DM_RESO, DM_KICK, DM_SNARE, DM_TOM,
                                     DM_CLAP, DM_CLAP, DM_FM, DM_HIHAT, DM_HIHAT};
    for (int key = 0; key < DL_NSOUND; key++) {
        int m = p[TP_DRUM(key, DP_MODEL)];
        if (m < (dev ? DM_SAMPLE + 1 : NDM))
            continue;
        p[TP_DRUM(key, DP_MODEL)] = dev && m - (DM_SAMPLE + 1) < 21 ? PART[m - (DM_SAMPLE + 1)] : DM_KICK;
        for (int k = 0; k < 4; k++) {
            p[TP_DRUM(key, DP_K1 + k)] = DM_DEFAULT[p[TP_DRUM(key, DP_MODEL)]][k];
            p[TP_DRUMX(key, k)] = 0;
        }
        if (dev && (m == 19 || m == 20 || m == 31))   /* (the open hat and the cymbals) */
            p[TP_DRUM(key, DP_K2)] = 88;
    }
}

static int settings_valid(settings_t *s)
{
    int t, k;
    if (s->magic != PROJ_MAGIC || !FORMAT_OK(s->format))
        return 0;
    if (s->tempo_x10 < 400u || s->tempo_x10 > 3000u || s->scene >= NSCENE || s->trk_sel >= NALLTRK)
        return 0;
    if (s->groove >= NGROOVE)
        s->groove = GR_SWING;
    s->name[NAME_LEN - 1] = 0;
    for (t = 0; t < NALLTRK; t++) {
        uint8_t *p = s->trk[t].par;
        s->trk[t].name[NAME_LEN - 1] = 0;
        if (t < NTRK && p[TP_ENGINE] >= NENGINE)
            return 0;
        if (t >= AUX_FX1 && p[TP_ENGINE] >= NFXPICK)
            return 0;
        if (t < NTRK)
            keys_fix(p, s->format < 17u);
        if (t < NTRK)
            for (k = 0; k < NTP; k++) {
                const tp_info_t *i = tp_info_par(k, p);   /* the ranges depend on the player and LFO type */
                if (i && (p[k] < i->lo || p[k] > i->hi))
                    p[k] = i->def;
            }
    }
    for (k = 0; k < NMS; k++)
        if (s->master[k] > master_max(k))
            s->master[k] = (uint8_t)master_max(k);
    return 1;
}

/* the patterns came from the flash whole (CRC): only the fields the sequencer indexes with are
 * clamped, so a stray value never reaches past an array */
static void patterns_clamp(void)
{
    int t, p, k;
    for (t = 0; t < NSEQT; t++)
        for (p = 0; p < NPATT; p++) {
            tpat_t *tp = &pats.pat[t][p];
            if (tp->len < 1 || tp->len > NSTEP)
                tp->len = 16;
            if (tp->scale >= NTS)
                tp->scale = TS_1;
            if (tp->groove < -99 || tp->groove > 99)      /* the track's groove amount */
                tp->groove = 0;
            if (tp->nlen > 100)                           /* the note length, % of a step */
                tp->nlen = 50;
            if (tp->quant > 100)
                tp->quant = 100;
            for (k = 0; k < NSTEP; k++) {
                step_t *s = &tp->st[k];
                if (t < NTRK && proj.trk[t].par[TP_ENGINE] == EG_DRUMLINE)
                    kit_set(s, kit_mask(s));              /* a kit step: its sounds' bits only */
                if (s->comp >= NSC)
                    s->comp = SC_NONE;
                if (s->comp2 >= NSC)
                    s->comp2 = SC_NONE;
                if (s->cond >= COND_AB + COND_NAB)
                    s->cond = 0;
            }
        }
    for (k = 0; k < NLANE; k++) {
        lane_t *l = &pats.lane[k];
        if (l->used && (l->trk >= NALLTRK || l->pat >= NPATT || (l->param >= NTP && !lane_is_note(l->param))))
            l->used = 0;                  /* (a drum step's tune lane is no parameter: kept; it was dropped before 10) */
        if (l->used && lane_is_hit(l->param))                   /* a kit hit's settings: an index into pats.hit */
            for (int st = 0; st < NSTEP; st++)
                if (l->val[st] >= NHIT)
                    l->val[st] = LOCK_NONE;
        if (l->used && !lane_is_note(l->param)) {                /* a value past the parameter's range would reach the voices unclamped */
            const tp_info_t *i = tp_info_par(l->param, proj.trk[l->trk].par);
            int st;
            if (i)
                for (st = 0; st < NSTEP; st++)
                    if (l->val[st] != LOCK_NONE && l->val[st] > i->hi)
                        l->val[st] = i->hi;
        }
    }
    for (k = 0; k < NHIT; k++) {
        hit_t *h = &pats.hit[k];
        if (h->comp >= NSC)
            h->comp = SC_NONE;
        if (h->comp2 >= NSC)
            h->comp2 = SC_NONE;
        if (h->cond >= COND_AB + COND_NAB)
            h->cond = 0;
    }
    for (k = 0; k < NSCENE; k++) {
        scene_t *sc = &pats.scene[k];
        for (t = 0; t < NSEQT; t++)
            if (sc->pat[t] >= NPATT)
                sc->pat[t] = 0;
        if (sc->brain_scale >= NBS)
            sc->brain_scale = 0;
        if (sc->brain_key >= 12)
            sc->brain_key = 0;
    }
    if (pats.song.len > NSONG)
        pats.song.len = NSONG;
    for (k = 0; k < NSONG; k++) {
        if (pats.song.slot[k].scene >= NSCENE)
            pats.song.slot[k].scene = 0;
        if (pats.song.slot[k].rep < 1)
            pats.song.slot[k].rep = 1;
        if (pats.song.slot[k].rep > SONG_BARS)
            pats.song.slot[k].rep = SONG_BARS;
    }
}

/* ---- objects -------------------------------------------------------------------------------- */

static uint8_t io_buf[PLAT_STORE_MAX] __attribute__((aligned(4)));

/* what each object last held on the flash (a hash of its payload), so a save writes only the
 * objects that changed: a save of a tweaked pattern usually erases one or two sectors, not seven */
static uint32_t stored_hash[OBJ_NOBJ];
static uint8_t stored_known[OBJ_NOBJ];
static uint32_t hash_of(const uint8_t *p, uint32_t n)        /* FNV-1a */
{
    uint32_t h = 2166136261u;
    while (n--)
        h = (h ^ *p++) * 16777619u;
    return h;
}
static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}
static uint32_t crc32_of(const uint8_t *p, uint32_t n)       /* zlib CRC-32, 4 bits per step */
{
    static const uint32_t T[16] = {
        0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu, 0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u, 0x5005713Cu,
        0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu, 0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu};
    uint32_t c = 0xFFFFFFFFu;
    while (n--) {
        c ^= *p++;
        c = (c >> 4) ^ T[c & 15u];
        c = (c >> 4) ^ T[c & 15u];
    }
    return ~c;
}

/* the object is the n bytes of io_buf; written only if they changed. Not while a backup streams (app_backup_hold:
 * it reads the objects one by one) */
static uint8_t store_held;
static int save_buf(uint32_t obj, uint32_t n)
{
    uint32_t h = hash_of(io_buf, n);
    int rc;
    if (stored_known[obj] && stored_hash[obj] == h)
        return 0;                                    /* the flash already holds exactly this */
    if (store_held)
        return -2;
    rc = loom_store_save(obj, io_buf, n);
    stored_known[obj] = rc == 0;
    stored_hash[obj] = h;
    return rc;
}

/* ---- the packed stream ---------------------------------------------------------------------- */

static uint8_t stream_obj[NSTREAM];                   /* the slot's stream objects, OBJ_PROJ(slot, 1..) */

/* the writer: fills io_buf after the object's header and writes it when full (measure: counts only) */
typedef struct {
    int measure, k, err;
    uint32_t fill, total;
} st_out_t;

static int st_flush(st_out_t *o)
{
    int rc = save_buf(stream_obj[o->k], o->fill);
    o->k++;
    o->fill = 4u;
    put32(io_buf, PROJ_STREAM);
    return rc;
}

static int st_put(void *ctx, const uint8_t *p, uint32_t n)
{
    st_out_t *o = ctx;
    o->total += n;
    if (o->measure)
        return o->total > STREAM_CAP ? -1 : 0;
    while (n) {
        if (o->fill == PLAT_STORE_MAX) {
            if (o->k + 1 >= NSTREAM || st_flush(o)) {
                o->err = 1;
                return -1;
            }
        }
        io_buf[o->fill++] = *p++;
        n--;
    }
    return 0;
}

/* the packed stream's length (packed_len); -7: over STREAM_CAP */
static int stream_measure(void)
{
    st_out_t o = {1, 0, 0, 0, 0};
    lz_sink_t sink = {st_put, &o};
    if (lz_compress((const uint8_t *)&pats, RAW_LEN, &sink))
        return -7;
    packed_len = o.total;
    return 0;
}
/* the objects a save of the settings and a stream of packed_len writes: the pool's room it needs */
static int save_objects(void)
{
    uint32_t first = PLAT_STORE_MAX - STREAM_HDR, rest = PLAT_STORE_MAX - 4u;
    return 2 + (packed_len > first ? (int)((packed_len - first + rest - 1u) / rest) : 0);
}
/* the stream written (stream_measure first: packed_len) */
static int save_stream(void)
{
    const uint8_t *raw = (const uint8_t *)&pats;
    uint32_t crc = crc32_of(raw, RAW_LEN);
    st_out_t o = {0, 0, 0, 0, 0};
    lz_sink_t sink = {st_put, &o};
    o.total = 0;
    o.fill = STREAM_HDR;
    put32(io_buf, PROJ_STREAM);
    put32(io_buf + 4, RAW_LEN);
    put32(io_buf + 8, crc);
    put32(io_buf + 12, packed_len);
    if (lz_compress(raw, RAW_LEN, &sink) || o.err || st_flush(&o))
        return -3;
    while (o.k < NSTREAM)                             /* the objects past the stream: empty (none: in a pooled store
                                                       * they take no sector; nothing reads past the packed length) */
        if (save_buf(stream_obj[o.k++], 0))
            return -3;
    return 0;
}

/* the reader: the objects in turn, from io_buf */
typedef struct {
    int k, n;
    uint32_t pos, left, prev;                         /* prev: bit k, object k from its previous copy */
} st_in_t;

static int stream_obj_load(int k, uint32_t prev)      /* object k of the stream into io_buf, its copy per prev */
{
    return (prev >> k) & 1u ? plat_store_load_prev(stream_obj[k], io_buf, sizeof io_buf)
                            : plat_store_load(stream_obj[k], io_buf, sizeof io_buf);
}

static int st_get(void *ctx)
{
    st_in_t *s = ctx;
    if (!s->left)
        return -1;
    while (s->pos >= (uint32_t)s->n) {
        if (++s->k >= NSTREAM)
            return -1;
        s->n = stream_obj_load(s->k, s->prev);
        if (s->n < 4 || get32(io_buf) != PROJ_STREAM)
            return -1;
        stored_known[stream_obj[s->k]] = !((s->prev >> s->k) & 1u);   /* a previous copy: written again next save */
        stored_hash[stream_obj[s->k]] = hash_of(io_buf, (uint32_t)s->n);
        s->pos = 4u;
    }
    s->left--;
    return io_buf[s->pos++];
}

/* an older format's store (10: 16 scenes of scene10_t; 11: 32 of scene11_t), unpacked at the start of pats, made
 * this format's: its scenes spread to their new size (format 10's other 16 unused; its kits' sounds unmuted;
 * format 11's one mask for every kit, each kit's), the song moved after them */
static void migrate_old(int fmt)
{
    uint8_t *sc = (uint8_t *)pats.scene;
    const uint32_t osz = fmt == 10 ? sizeof(scene10_t) : sizeof(scene11_t), on = fmt == 10 ? NSCENE10 : NSCENE;
    memcpy(&pats.song, sc + on * osz, sizeof pats.song);   /* (apart: songs_apart) */
    for (int i = (int)on - 1; i >= 0; i--) {             /* (from the last: each moves up, over none not yet read) */
        scene11_t o;
        memset(&o, 0, sizeof o);
        memcpy(&o, sc + (uint32_t)i * osz, osz);          /* (format 10's: the same fields, no kmute) */
        memset(&pats.scene[i], 0, sizeof pats.scene[i]);
        memcpy(pats.scene[i].pat, o.pat, NSEQT);
        pats.scene[i].used = o.used;
        pats.scene[i].mute = o.mute;
        pats.scene[i].brain_key = o.brain_key;
        pats.scene[i].brain_scale = o.brain_scale;
        pats.scene[i].brain_deg = o.brain_deg;
        pats.scene[i].weave = o.weave;
        for (int k = 0; k < NKITM; k++)
            pats.scene[i].kmute[k] = o.kmute;
    }
    if (on < NSCENE)
        memset(&pats.scene[on], 0, (NSCENE - on) * sizeof pats.scene[0]);
}

/* 0 = loaded; -1 = not there or not good (pats then holds garbage). An older format's (load_format) as it was
 * saved, then migrated */
static int load_stream(int n, uint32_t prev)
{
    uint8_t *raw = (uint8_t *)&pats;
    uint32_t crc = get32(io_buf + 8), plen = get32(io_buf + 12), len = get32(io_buf + 4);
    st_in_t s = {0, n, STREAM_HDR, plen, prev};
    lz_src_t src = {st_get, &s};
    if (n < (int)STREAM_HDR || (len != RAW_LEN && len != RAW_LEN17 && len != RAW_LEN10 && len != RAW_LEN11) ||
        plen > STREAM_CAP)
        return -1;                                /* (its own length says its format: settings saved alone, as
                                                   * this format's, before a power cut beside format 10's patterns) */
    stored_known[stream_obj[0]] = !(prev & 1u);
    stored_hash[stream_obj[0]] = hash_of(io_buf, (uint32_t)n);
    if (lz_decompress(&src, raw, len) || crc32_of(raw, len) != crc)
        return -1;
    if (len != RAW_LEN)                           /* (an older store: no hit settings) */
        memset(pats.hit, 0, sizeof pats.hit);
    if (len == RAW_LEN17 && load_format > 17u)
        load_format = 17u;
    else if (len == RAW_LEN10 || len == RAW_LEN11) {
        load_format = len == RAW_LEN10 ? 10u : 11u;
        migrate_old(load_format);
    }
    packed_len = plen;
    return 0;
}

/* ---- the settings object ------------------------------------------------------------------- */

typedef struct { uint32_t fill; } set_out_t;
static int set_put(void *ctx, const uint8_t *p, uint32_t n)
{
    set_out_t *o = ctx;
    if (n > PLAT_STORE_MAX - o->fill)
        return -1;                                    /* (it would not fit: nothing written) */
    while (n--)
        io_buf[o->fill++] = *p++;
    return 0;
}
/* proj into io_buf as the settings object: its size, or -1 (the tracks do not pack into one object: never seen;
 * a project of eight kits with every knob of every key random packs into about two thirds of it) */
static int settings_pack(void)
{
    set_out_t o = {SET_HDR + 4u};
    lz_sink_t sink = {set_put, &o};
    for (uint32_t i = 0; i < SET_HDR; i++)
        io_buf[i] = ((const uint8_t *)&proj)[i];
    if (lz_compress((const uint8_t *)proj.trk, (uint32_t)sizeof proj.trk, &sink))
        return -1;
    put32(io_buf + SET_HDR, o.fill - SET_HDR - 4u);
    return (int)o.fill;
}
typedef struct { const uint8_t *p; uint32_t left; } set_in_t;
static int set_get(void *ctx)
{
    set_in_t *s = ctx;
    if (!s->left)
        return -1;
    s->left--;
    return *s->p++;
}
/* the settings object in io_buf (n bytes) into proj: this format's packed, or format 10..14's raw (each key's knobs
 * 5..8 then its model's defaults); 0 = done (proj then needs settings_valid), -1 = not one */
static int settings_unpack(int n)
{
    uint32_t fmt, i;
    if (n < (int)(SET_HDR + 4u) || get32(io_buf) != PROJ_MAGIC)
        return -1;
    fmt = get32(io_buf + 4);
    if (fmt >= 15u && fmt <= PROJ_FORMAT) {
        set_in_t in = {io_buf + SET_HDR + 4u, get32(io_buf + SET_HDR)};
        lz_src_t src = {set_get, &in};
        if (in.left > (uint32_t)n - SET_HDR - 4u)
            return -1;
        for (i = 0; i < SET_HDR; i++)
            ((uint8_t *)&proj)[i] = io_buf[i];
        uint32_t ntp = fmt >= 19u ? (uint32_t)NTP : fmt >= 16u ? NTP18 : NTP15, tl = ntp + NAME_LEN + 4u;
        if (fmt >= 19u)
            return lz_decompress(&src, (uint8_t *)proj.trk, (uint32_t)sizeof proj.trk) ? -1 : 0;
        if (lz_decompress(&src, (uint8_t *)proj.trk, NALLTRK * tl))   /* formats 15..18: their tracks shorter, */
            return -1;
        for (int t = NALLTRK - 1; t >= 0; t--) {  /* spread from the last (each moves up: copied from its end) */
            const uint8_t *o = (const uint8_t *)proj.trk + (uint32_t)t * tl;
            uint8_t *d = (uint8_t *)&proj.trk[t];
            uint8_t *par = proj.trk[t].par;
            for (i = tl; i-- > ntp;)            /* the name and rsv after the new parameters */
                d[i + (NTP - ntp)] = o[i];
            for (i = ntp; i-- > 0;)
                d[i] = o[i];
            if (ntp <= (uint32_t)TP_O2FINE)
                par[TP_O2FINE] = 64;
            par[TP_RUP] = 0;
            if (t < NTRK && par[TP_PLAYER] % NPL == PL_RAND) {   /* (the octave knob: down and up) */
                uint8_t v = OLD_OCT[par[TP_P4] < 25u ? par[TP_P4] : 12u];
                par[TP_P4] = (uint8_t)(v >> 2);
                par[TP_RUP] = (uint8_t)(v & 3u);
            }
        }
        return 0;
    }
    if (n != (int)SET_OLD || !FORMAT_OK(fmt))
        return -1;
    for (i = 0; i < SET_HDR; i++)
        ((uint8_t *)&proj)[i] = io_buf[i];
    for (int t = 0; t < NALLTRK; t++) {
        const uint8_t *o = io_buf + SET_HDR + (uint32_t)t * TRK14;
        trk_t *tr = &proj.trk[t];
        memcpy(tr->par, o, NTP14);
        memcpy(tr->name, o + NTP14, NAME_LEN);
        memcpy(tr->rsv, o + NTP14 + NAME_LEN, 4);
        for (int k = 0; k < DL_NSOUND; k++)
            for (int j = 0; j < DPX_N; j++)
                tr->par[TP_DRUMX(k, j)] = DM_DEFAULT2[tr->par[TP_DRUM(k, DP_MODEL)] % NDM][j];
        tr->par[TP_O2FINE] = 64;
        tr->par[TP_RUP] = 0;
    }
    return 0;
}

/* ---- load, save ----------------------------------------------------------------------------- */

static void slot_objects(int slot)
{
    int k;
    for (k = 0; k < NSTREAM; k++)
        stream_obj[k] = (uint8_t)OBJ_PROJ(slot, k + 1);
}

/* the defaults, then each object into its place: a load copies only what checks out */
int project_load(int slot)
{
    int bad = 0, n;
    if (slot < 0 || slot >= NPROJ)
        slot = 0;
    proj_slot = (uint8_t)slot;
    slot_objects(slot);
    project_defaults();
    packed_len = 0;
    load_format = PROJ_FORMAT;
    n = plat_store_load(OBJ_PROJ(slot, 0), io_buf, sizeof io_buf);
    if (n > 0)
        stored_hash[OBJ_PROJ(slot, 0)] = hash_of(io_buf, (uint32_t)n);
    if (settings_unpack(n) == 0 && settings_valid(&proj)) {
        load_format = (uint8_t)proj.format;
        proj.format = PROJ_FORMAT;                    /* (an older one migrated: saved as this format next) */
        if (load_format < 13u)                        /* (before 13 the conductor's LFO slots held a knit's defaults:
                                                       * the shared LFO from its start) */
            glfo_defaults(proj.trk[AUX_BRAIN].par);
        if (load_format < 14u)                        /* (before 14, off (FT_DYNAMIC) sat at 4 and bp12 at 5: swap) */
            for (int t = 0; t < NTRK; t++) {
                uint8_t ft = proj.trk[t].par[TP_FTYPE];
                if (ft == 4u || ft == 5u)
                    proj.trk[t].par[TP_FTYPE] = (uint8_t)(9u - ft);
            }
        if (load_format == 10u) {                     /* 0.5-beta had no way to set a track's MIDI channel: they are
                                                       * all off there. As a new project's: notes out, MIDI OUT on */
            proj.send_notes = 1;
            for (int t = 0; t < NTRK; t++)
                if (!proj.trk[t].par[TP_CHAN])
                    proj.trk[t].par[TP_CHAN] = 1;
        }
        for (int t = 0; t < NTRK; t++)                /* (before C3 a track held its channel, 1-16: now MIDI OUT on) */
            if (proj.trk[t].par[TP_CHAN])
                proj.trk[t].par[TP_CHAN] = 1;
        stored_known[OBJ_PROJ(slot, 0)] = 1;
    } else {
        project_defaults();                           /* (an unpack may have left part of it) */
        bad = 1;
        if (n >= 8 && get32(io_buf) == PROJ_MAGIC && !FORMAT_OK(get32(io_buf + 4)))
            proj_slot = NPROJ;            /* another format's project there: no slot, so nothing (autosave, SAVE)
                                           * writes over it unasked (it took a song once, 2026-10-19); SAVE TO
                                           * chooses a slot */
    }
    n = plat_store_load(stream_obj[0], io_buf, sizeof io_buf);
    if (n >= 4 && get32(io_buf) == PROJ_STREAM) {
        if (load_stream(n, 0)) {                      /* torn (a power cut mid-save) or broken: the previous
                                                       * copies, fewest first (the newest save that checks out) */
            uint32_t prev;
            int ok = 0, b;
            for (b = 1; b <= NSTREAM && !ok; b++)
                for (prev = 1; prev < (1u << NSTREAM) && !ok; prev++) {
                    if (__builtin_popcount(prev) != b)
                        continue;
                    n = stream_obj_load(0, prev);
                    ok = n >= 4 && get32(io_buf) == PROJ_STREAM && load_stream(n, prev) == 0;
                }
            if (!ok) {                                /* nothing checks out: the defaults, not garbage */
                patterns_init(&pats);
                bad = 1;
            }
        }
        patterns_clamp();
    } else {
        bad = 1;
    }
    migrated = !bad && load_format != PROJ_FORMAT ? (uint8_t)slot : 0xFF;
    return bad ? -1 : 0;
}

static void count_next(void);                         /* (below: the save count) */
/* 0 = saved and verified; -1 = the settings failed; -7 = the patterns do not fit (nothing written) */
static int project_write(int slot);
int project_save(int slot)
{
    uint32_t i;
    if (slot < 0 || slot >= NPROJ)
        return -1;
    if (slot != proj_slot) {                          /* a new slot: nothing known about its flash */
        for (i = 0; i < PROJ_NOBJ; i++)
            stored_known[OBJ_PROJ(slot, i)] = 0;
    }
    proj_slot = (uint8_t)slot;
    slot_objects(slot);
    proj.tempo_x10 = (uint16_t)(seq_tempo(&seq) * 10.0f + 0.5f);
    proj.clock = seq.clock;
    proj.send_notes = seq.send_notes;
    proj.metronome = seq.metronome;
    proj.scene = seq.scene;
    return project_write(slot);
}
/* the project as it is in proj and pats, into its slot (project_save takes the sequencer's live values first; a
 * migrated project's first touch does not: just loaded, they are not the sequencer's yet) */
static int project_write(int slot)
{
    int rc, n;
    count_next();
    if ((n = settings_pack()) < 0)
        return -1;
    if (stream_measure())
        return -7;                                    /* would not fit: nothing written (MEMORY FULL) */
    {   /* the store's pool (app.h APP_STORE_POOL): room for every object it writes, else nothing written (MEMORY
         * FULL): the three slots' projects share it */
        int room = plat_store_room();
        if (room >= 0 && room < save_objects())
            return -7;
    }
    if ((rc = save_buf(OBJ_PROJ(slot, 0), (uint32_t)n)) != 0)
        return rc < 0 ? -1 : rc;
    rc = save_stream();
    if (rc == 0 && migrated == slot)
        migrated = 0xFF;                          /* (saved whole: this format now) */
    return rc;
}

/* the save count (settings rsv[4..7]): every save and every load takes the next, so the highest is the slot used
 * last; app_init opens it */
#define COUNT_AT offsetof(settings_t, rsv) + 4u
static uint32_t count_max;
static uint32_t slot_count(int slot)
{
    int n = plat_store_load(OBJ_PROJ(slot, 0), io_buf, (uint32_t)(COUNT_AT + 4u));
    if (n < (int)(COUNT_AT + 4u) || get32(io_buf) != PROJ_MAGIC || !FORMAT_OK(get32(io_buf + 4)))
        return 0;
    return get32(io_buf + COUNT_AT);
}
static void count_next(void)
{
    uint32_t c = ++count_max;
    for (uint32_t i = 0; i < 4u; i++)
        proj.rsv[4 + i] = (uint8_t)(c >> (8u * i));
}
int project_last_slot(void)
{
    int best = 0;
    count_max = 0;
    for (int s = 0; s < NPROJ; s++) {
        uint32_t c = slot_count(s);
        if (c > count_max) {
            count_max = c;
            best = s;
        }
    }
    return best;
}
int project_touch(int slot)                       /* (only the settings change: the patterns' objects stay) */
{
    int n;
    if (slot < 0 || slot >= NPROJ)
        return -1;
    if (migrated == slot)                         /* (an older format's patterns there: the whole project, now) */
        return project_write(slot) == 0 ? 0 : -1;
    count_next();
    n = settings_pack();
    return n < 0 || save_buf(OBJ_PROJ(slot, 0), (uint32_t)n) < 0 ? -1 : 0;
}

int project_exists(int slot)
{
    int n;
    if (slot < 0 || slot >= NPROJ)
        return 0;
    n = plat_store_load(OBJ_PROJ(slot, 0), io_buf, 8);
    return n >= 8 && get32(io_buf) == PROJ_MAGIC && FORMAT_OK(get32(io_buf + 4));
}

int presets_load(void)
{
    int n = plat_store_load(OBJ_PRESETS, io_buf, sizeof io_buf), k;
    uint32_t i, fmt = get32(io_buf);
    for (k = 0; k < NUSER_PRESETS; k++)
        user_presets[k].name[0] = 0;
    for (k = 0; k < NUSER_KITS; k++)
        user_kit_name[k][0] = 0;
    if (!(fmt == PRESETS_FORMAT && n == (int)PRESETS_LEN) && !((fmt == 10u || fmt == 9u) && n == (int)PRESETS_V10) &&
        !(fmt == 8u && n == (int)PRESETS_V8) &&
        !((fmt == 6u || fmt == 7u) && n == (int)PRESETS_V7))
        return -1;
    if (fmt >= 9u)
        for (i = 0; i < sizeof user_presets; i++)
            ((uint8_t *)user_presets)[i] = io_buf[4u + i];
    else
        for (k = 0; k < NUSER_PRESETS; k++) {     /* (before 9: a preset without its fine) */
            for (i = 0; i < PRESET_OLD; i++)
                ((uint8_t *)&user_presets[k])[i] = io_buf[4u + (uint32_t)k * PRESET_OLD + i];
            user_presets[k].o2fine = 64;
        }
    for (k = 0; k < NUSER_PRESETS; k++)
        if (user_presets[k].o2fine < 14u || user_presets[k].o2fine > 114u)
            user_presets[k].o2fine = 64;
    if (fmt < 7u)                                /* (before 7, off (FT_DYNAMIC) sat at 4 and bp12 at 5: swap) */
        for (k = 0; k < NUSER_PRESETS; k++) {
            uint8_t ft = user_presets[k].par[TP_FTYPE];
            if (ft == 4u || ft == 5u)
                user_presets[k].par[TP_FTYPE] = (uint8_t)(9u - ft);
        }
    for (k = 0; k < NUSER_PRESETS; k++) {
        user_presets[k].name[NAME_LEN - 1] = 0;
        if (user_presets[k].engine >= NENGINE)
            user_presets[k].name[0] = 0;
    }
    if (fmt >= 8u)
        for (k = 0; k < (fmt == PRESETS_FORMAT ? NUSER_KITS : (int)OLD_KITS); k++) {
            const uint8_t *img = io_buf + (fmt == 8u ? PRESETS_V7 : PRESETS_KITS) + (uint32_t)k * KIT_LEN;
            for (i = 0; i < NAME_LEN; i++)
                user_kit_name[k][i] = (char)img[i];
            user_kit_name[k][NAME_LEN - 1] = 0;
            if (img[NAME_LEN + TP_ENGINE] != EG_DRUMLINE)
                user_kit_name[k][0] = 0;
        }
    stored_known[OBJ_PRESETS] = 1;
    stored_hash[OBJ_PRESETS] = hash_of(io_buf, (uint32_t)n);
    return 0;
}

/* the presets object in io_buf as it will be saved: the user presets as they are in RAM, the kits as the flash
 * holds them (moved up from format 8's place; none, from an older format) */
static void presets_image(void)
{
    int n = plat_store_load(OBJ_PRESETS, io_buf, sizeof io_buf);
    uint32_t i, fmt = get32(io_buf);
    uint32_t keep = 0;                          /* the kits' bytes kept: the rest of the slots empty */
    if (fmt == 8u && n == (int)PRESETS_V8) {
        for (i = OLD_KITS * KIT_LEN; i-- > 0;)       /* (up: copied from the end) */
            io_buf[PRESETS_KITS + i] = io_buf[PRESETS_V7 + i];
        keep = OLD_KITS * KIT_LEN;
    } else if ((fmt == 10u || fmt == 9u) && n == (int)PRESETS_V10)
        keep = OLD_KITS * KIT_LEN;
    else if (fmt == PRESETS_FORMAT && n == (int)PRESETS_LEN)
        keep = NUSER_KITS * KIT_LEN;
    for (i = PRESETS_KITS + keep; i < PRESETS_LEN; i++)
        io_buf[i] = 0;
    put32(io_buf, PRESETS_FORMAT);
    for (i = 0; i < sizeof user_presets; i++)
        io_buf[4u + i] = ((const uint8_t *)user_presets)[i];
}

int presets_save(void)
{
    presets_image();
    return save_buf(OBJ_PRESETS, PRESETS_LEN);
}

int kit_load(int slot, uint8_t *img)
{
    int n;
    uint32_t i, at, fmt;
    if (slot < 0 || slot >= NUSER_KITS || !user_kit_name[slot][0])
        return -1;
    n = plat_store_load(OBJ_PRESETS, io_buf, sizeof io_buf);
    fmt = get32(io_buf);
    if (fmt == PRESETS_FORMAT && n == (int)PRESETS_LEN)
        at = PRESETS_KITS;
    else if ((fmt == 10u || fmt == 9u) && n == (int)PRESETS_V10 && slot < (int)OLD_KITS)
        at = PRESETS_KITS;
    else if (fmt == 8u && n == (int)PRESETS_V8 && slot < (int)OLD_KITS)
        at = PRESETS_V7;
    else
        return -1;
    for (i = 0; i < KIT_LEN; i++)
        img[i] = io_buf[at + (uint32_t)slot * KIT_LEN + i];
    img[NAME_LEN - 1] = 0;
    return img[NAME_LEN + TP_ENGINE] != EG_DRUMLINE ? -1 : fmt >= 10u ? 0 : 1;
}

int kit_save(int slot, const uint8_t *img)
{
    uint32_t i;
    int rc;
    if (slot < 0 || slot >= NUSER_KITS)
        return -1;
    presets_image();
    for (i = 0; i < KIT_LEN; i++)
        io_buf[PRESETS_KITS + (uint32_t)slot * KIT_LEN + i] = img[i];
    rc = save_buf(OBJ_PRESETS, PRESETS_LEN);
    if (rc == 0) {
        for (i = 0; i < NAME_LEN; i++)
            user_kit_name[slot][i] = (char)img[i];
        user_kit_name[slot][NAME_LEN - 1] = 0;
    }
    return rc;
}

/* every value to the engine (boot, load): the tracks, the FX, the master, the transport */
void project_apply(void)
{
    int t, k;
    for (t = 0; t < NALLTRK; t++)
        engine_set_all(t);
    for (k = 0; k < 2; k++) {
        const uint8_t *p = proj.trk[AUX_FX1 + k].par;
        engine_fx(k, p[TP_ENGINE], p + TP_E1, p[TP_FENV], p[TP_CUT]);
    }
    for (k = 0; k < NMS; k++)
        engine_master(k, proj.master[k]);
    seq.bpm = (float)proj.tempo_x10 * 0.1f;               /* at once (a save right after reads it back) */
    engine_tempo(proj.tempo_x10);
    engine_groove(proj.groove);
    seq.clock = proj.clock < SEQ_NCLK ? proj.clock : SEQ_CLK_RECEIVE;
    seq.send_notes = proj.send_notes;
    seq.loop_free = (uint8_t)((proj.rsv[2] & 8u) != 0);   /* (pages.c LOOP_FREE: a loop throw let go drifts) */
    seq.metronome = proj.metronome;
    engine_scene(proj.scene < NSCENE ? proj.scene : 0, 0);
    engine_metronome(proj.metronome);
}
