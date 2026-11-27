/* SPDX-License-Identifier: MPL-2.0 */
/* LOOM-8 on fm1-base: the app's unity build (compiled into fm1-base's device/unity.c after the platform, and into its
 * simulator host/plat_host.c) and the glue between fm1-base's contract (app.h) and LOOM-8's parts: its drawing
 * (gfx_loom.c), the engine (engine.c; the DSP and the sequencer in dsp/ and seq/, their own -O2 units), the project
 * (project.c), the pages (pages.c), the input (ui.c), the user sample bank over USB (samples.c), and on the FM-1 its
 * platform extras (plat_loom.c: the second core, the profiler's clock, the supplies). */
#include "res.c"                                    /* the resource region's sections (fm1-base plat_res) */
#include "gfx_loom.c"
#include "engine.c"
#include "project.c"
#include "pages.c"
#include "ui.c"
#include "midicc.c"
#include "samples.c"
#ifdef PLAT_WEB
#include "render.c"                                   /* the WAV export page's render (the browser build only) */
#endif
#ifndef PLAT_HOST
#include "plat_loom.c"
#else
static uint32_t render_t0(void);
static uint32_t render_pct(uint32_t t0, uint32_t n);
#if defined(PLAT_HOST) && !defined(PLAT_WEB)
static void work_begin(void);
static int work_end(uint32_t n);
#endif
static int load_force = -1;             /* the script's "load N": the guard sees N %, -1 measured (host.c) */
#endif

_Static_assert(OBJ_NOBJ == 24 && OBJ_PROJ0 == 2, "the store map lists every object (APP_STORE_MAP)");
_Static_assert(SMP_REG0 >= 0xC1000u && SMP_REG0 + SMP_REG0_SIZE <= 0xDC000u && SMP_REG0 >= PLAT_DATA_LO,
               "the user sample bank's first window between the project slots and the panel calibration");
_Static_assert(SMP_REG1 == PLAT_XDATA1_LO && SMP_REG1 + SMP_REG1_SIZE == PLAT_XDATA1_HI,
               "the user sample bank's other window: fm1-base's extra window XDATA1, whole (plat.h)");
_Static_assert(PLAT_XDATA0_LO == PLAT_XDATA0_HI, "no XDATA0: the app area reaches 0x97000 (fm1.toml app_end)");
_Static_assert(SMP_HDR_USED <= SMP_BANK_HDR, "the bank's entries fit its header");

/* no store object (the calibration, the presets, the projects: APP_STORE_MAP, two sectors each) in a window of the
 * sample bank: nothing else writes there (checked at every start; the store's own checks are fm1-base's) */
static int store_clear_of_bank(void)
{
    static const uint32_t map[OBJ_NOBJ] = APP_STORE_MAP;
    for (int o = 0; o < OBJ_NOBJ; o++)
        for (int r = 0; r < SMP_NREG; r++)
            if (map[o] + 0x2000u > smp_reg_off(r) && map[o] < smp_reg_off(r) + smp_reg_size(r))
                return 0;
    return 1;
}

/* the lights: LOOM-8's (the unlit buttons glowing, keys dim, PLAY green or red) on fm1-base's plat_lights */
void loom_lights(uint32_t buttons, uint32_t keys, int glow, uint32_t dim_keys, int red)
{
    uint32_t play = 1u << B_PLAY, b = buttons, dim = glow ? ((1u << NB) - 1u) & ~buttons : 0u;
    if (!red) {                                     /* PLAY's green LED, unless recording */
        b = (b & ~play) | (buttons & play ? LED_PLAY_GREEN : 0u);
        dim = (dim & ~play) | (dim & play ? LED_PLAY_GREEN : 0u);
    }
    plat_lights(b, keys, dim, dim_keys & ~keys, glow || dim_keys ? 2 : 0);
}

/* ---- the contract */
void app_init(void)
{
#ifndef PLAT_HOST
    plat_loom_init();
    if (plat_cache_ram()->status != PLAT_CR_OK)    /* its test failed: the objects there (app.h LOOM_CR) are not RAM.
                                                    * A crash (its record says where): two of them start safe mode */
        __builtin_trap();
#endif
    {   /* the user bank, read in place through its windows (none without the flash, or over a store object) */
        const uint8_t *reg[SMP_NREG];
        int ok = store_clear_of_bank();
        for (int r = 0; r < SMP_NREG; r++)
            reg[r] = ok ? plat_data(smp_reg_off(r)) : 0;
        smp_user_attach(reg);
#ifdef PLAT_HOST
        if (!ok) {
            printf("the sample bank's windows overlap a store object (APP_STORE_MAP)\n");
            exit(2);
        }
#endif
    }
    smp_user_scan();                                /* (an empty or torn bank: no user samples) */
    d909_res_get = res_samples;                     /* the 909 cymbals' samples: the resource region (fm1.toml) */
    loom_large_load();                              /* the large type faces, from there too */
    project_load(project_last_slot());              /* the slot used last; defaults when nothing (or another format) is saved */
    presets_load();
    engine_init(&pats, proj_par);
    project_apply();
#ifndef PLAT_HOST
    if (!DEBUG_BUILD)                               /* a release: two cores, unless the second was given up */
        proj.one_core = (uint8_t)(plat_cpu2_barred() != 0);
    else if (plat_cpu2_barred() && !proj.one_core) {     /* it crashed (or was given up) with two cores: one from now on */
        proj.one_core = 1;
        project_save(proj_slot);                    /* written now, on one core, the project as it loaded;
                                                     * Setup > 2nd core turns it back on */
    }
    if (!proj.one_core && plat_cpu2_set(1) == 0)  /* the second core: at power-on, before the audio and the timers */
        engine_set_core2(plat_cpu2_run, plat_cpu2_wait);   /* (fm1-base's: a failure of it restarts the FM-1) */
#else
    engine_set_core2(plat_cpu2_run, plat_cpu2_wait);
#endif
}

void app_render(int32_t *lr, uint32_t n, uint32_t gain_q12)
{
    uint32_t t0 = render_t0(), pct;
#if defined(PLAT_HOST) && !defined(PLAT_WEB)
    int wl;
    work_begin();                                   /* (the simulator's count of the block's work: host.c) */
#endif
    engine_render(lr, n, gain_q12);
#if defined(PLAT_HOST) && !defined(PLAT_WEB)
    wl = work_end(n);
#endif
    pct = render_pct(t0, n);
#if defined(PLAT_HOST) && !defined(PLAT_WEB)
    if (wl >= 0)
        pct = (uint32_t)wl;
#endif
#ifdef PLAT_HOST
#ifdef PLAT_WEB
    pct = load_force >= 0 ? (uint32_t)load_force : 0u;   /* a browser's timing is not the FM-1's */
#else
    pct = load_force >= 0 ? (uint32_t)load_force : pct;
#endif
#endif
    engine_load(pct);                               /* the overload guard's input */
}

void app_panic(void)
{
    engine_transport(0);
    engine_panic();
}
void app_ui_init(void) { ui_init(); }               /* boot; again after an update that did not happen, a restore */
#if FM1B_USB_HOST
/* the USB MIDI host (Setup's USB PORT): what changed, said; a session's end releases the notes its keys held (off
 * since 0.19, fm1.toml usb_host: its 3.8 KB of flash for the engines) */
static void usb_host_poll(void)
{
    static const char *const MSG[][2] = {{0, 0}, {"USB MIDI HOST", "plug in a powered hub"}, {"USB", "to a computer"},
                                         {"USB MIDI", "connected"}, {"NO USB MIDI", "on that device"},
                                         {"NOT SUPPORTED", "a low-speed device"}, {"USB MIDI", "lost"}};
    int m = plat_usb_host_msg();
    if (plat_usb_lost())
        for (int t = 0; t < NTRK; t++)
            engine_keys_off(t);
    if (m > USBH_MSG_NONE && m <= USBH_MSG_LOST) {
        if (m == USBH_MSG_NOMIDI || m == USBH_MSG_LOW || m == USBH_MSG_LOST)
            say_no(MSG[m][0], MSG[m][1]);
        else
            say(MSG[m][0], MSG[m][1]);
    }
}
#endif
void app_frame(void)
{
    plat_usb_audio_full(proj.usb_full != 0);        /* Setup's USB AUDIO: follows the knob, full, or stereo (full,
                                                     * the master alone) */
    eng_usb_stereo = proj.usb_full == 2;
#if FM1B_USB_HOST
    usb_host_poll();
#endif
    ui_frame();
}
void app_input_only(void) { ui_input_only(); }
void app_say(const char *a, const char *b) { ui_say(a, b); }
void app_redraw(void) { ui_redraw(); }
int app_sysex(const uint8_t *p, uint32_t n) { return smp_frame(p, n); }

/* the name over the eight tracks' inks woven as threads, in the default theme (pages.c THEME_BW: the splash comes
 * before the project, so before the theme picked) */
void app_splash(void)
{
    const theme_t *t = &THEMES[THEME_BW];
    lcd_fill(0, 0, 240, 240, t->bg);
    cv_begin(240, 96, t->bg);
    {
        int32_t w = text_w(&FONT_L, "LOOM-8");
        cv_text((240 - w) / 2, 0, &FONT_L, "LOOM-8", t->text);
        for (int i = 0; i < 8; i++)                  /* the warp: eight threads, each over and under the weft */
            for (int j = 0; j < 12; j++)
                cv_rect(56 + i * 16, 44 + j * 3, 8, (i + j) & 1 ? 3 : 2, (i + j) & 1 ? t->ink[i] : t->panel2);
        w = text_w(&FONT_S, "a pocket groovebox");
        cv_text((240 - w) / 2, 76, &FONT_S, "a pocket groovebox", t->dim);
    }
    cv_blit(0, 76);
}

/* backup and restore over USB (fm1-base's backup.c): the slots and the presets as saved (LOOM-8 saves when asked:
 * what is not saved stays so); no saving while a backup streams; a restore: everything from the flash again */
int app_backup_playing(void) { return seq.playing != 0; }
#if FM1B_MIRROR
/* the demo build (./build.sh --demo, the live mirror): a backup takes the working project too, so the live page's copy
 * of LOOM-8 (web/emu/live.html reads the project as a backup does) starts from what is on the screen. fm1-base calls this
 * only while stopped; a recording is never written half done */
void app_backup_flush(void)
{
    if (ui.dirty && proj_slot < NPROJ && !seq.rec && !seq.rec_wait && project_save(proj_slot) == 0)
        ui.dirty = 0;
}
#else
void app_backup_flush(void) {}
#endif
void app_backup_hold(int on) { store_held = (uint8_t)(on != 0); }
void app_backup_restored(void)
{
    memset(stored_known, 0, sizeof stored_known);  /* (what project.c knew of the flash is stale) */
    slot_known = 0;
    if (project_load(proj_slot) != 0)
        proj_slot = 0;
    presets_load();
    after_load();
    ui.dirty = 0;
}

/* the live mirror (fm1-base device/mirror.c's events mode, web/emu/live.html; the demo build only on the FM-1): the
 * page reads the project as a backup does, then this state, then the FM-1's inputs, and its own copy of LOOM-8 plays
 * along and draws the screen. The state is what the screen shows beyond the project (the page, the track, the list,
 * the scene and mutes) and a hash of the patterns, so a copy that drifted (a key landing on another step) is seen:
 * the page reads the project again. Times, questions and the step under the playhead are not in it: the copy plays
 * from its own PLAY (connect while stopped). */
#define MS_MAGIC 0x4C4D5331u                                /* "1SML" */
typedef struct {
    uint32_t magic, proj_hash;
    uint16_t mute, solo, solo_lat, chord_held, preset_i;
    uint8_t page, bar, dsound, last_inst, scene_pg, mod_pat, mod_lane, glo_pg, lockpg, trickpg, cursor, rec_armed;
    uint8_t chordmode, chord_bar, chord_pat, chord_pool, rem_fx, nrem, rem[SN];
    uint8_t pick_engine, pick_slot, pick_weave, pick_save, pick_kit, pick_kslot, pick_clear, pick_paste, name_slot;
    int8_t oct, pick_shift, pick_rot;
    uint8_t trk_sel, dirty, playing, scene, cue, song_on, list_open, list_sel, list_top, slot;
    uint8_t ppat[NSEQT];
} mirror_state_t;
static uint32_t hash_words(uint32_t h, const void *p, uint32_t n)   /* FNV-1a a word at a time (n: a multiple of 4) */
{
    const uint32_t *w = (const uint32_t *)p;
    for (n /= 4u; n--; w++)
        h = (h ^ *w) * 16777619u;
    return h;
}
/* the patterns, what can drift (a key landing on another step, a recording): the song's slots in use only (a load sets
 * the rest's repeats to 1). Not the tracks' settings: they change only by the inputs the copy takes too, and a save
 * and load is not exact for the values a sound does not use (an LFO that is off: its speed) */
static uint32_t pats_hash(void)
{
    const uint8_t *p = (const uint8_t *)&pats;
    const uint32_t song = (uint32_t)offsetof(patterns_t, song), hit = (uint32_t)offsetof(patterns_t, hit);
    uint32_t h = hash_words(2166136261u, p, song), used = pats.song.len < NSONG ? pats.song.len : NSONG;
    h = hash_words(h, &pats.song, (uint32_t)offsetof(song_t, slot));
    for (uint32_t k = 0; k < used; k++)
        h = (h ^ ((uint32_t)pats.song.slot[k].scene << 8 | pats.song.slot[k].rep)) * 16777619u;
    return hash_words(h, p + hit, (uint32_t)sizeof pats - hit);
}
_Static_assert(offsetof(patterns_t, song) % 4u == 0 && offsetof(patterns_t, hit) % 4u == 0 && sizeof pats % 4u == 0 &&
               offsetof(song_t, slot) % 4u == 0, "the patterns hashed by words");
uint32_t app_state_get(uint8_t *buf, uint32_t max)
{
    mirror_state_t *m = (mirror_state_t *)(void *)buf;      /* (buf: 4-byte aligned) */
    if (max < sizeof *m)
        return 0;
    memset(m, 0, sizeof *m);
    m->magic = MS_MAGIC;
    m->proj_hash = pats_hash();
    m->page = ui.page;
    m->bar = ui.bar;
    m->dsound = ui.dsound;
    m->oct = ui.oct;
    m->last_inst = ui.last_inst;
    m->scene_pg = ui.scene_pg;
    m->mod_pat = ui.mod_pat;
    m->mod_lane = ui.mod_lane;
    m->glo_pg = ui.glo_pg;
    m->lockpg = ui.lockpg;
    m->trickpg = ui.trickpg;
    m->cursor = ui.cursor;
    m->rec_armed = ui.rec_armed;
    m->chordmode = ui.chordmode;
    m->chord_held = ui.chord_held;
    m->chord_bar = ui.chord_bar;
    m->chord_pat = ui.chord_pat;
    m->chord_pool = ui.chord_pool;
    m->rem_fx = ui.rem_fx;
    m->nrem = ui.nrem;
    memcpy(m->rem, ui.rem, sizeof m->rem);
    m->solo = ui.solo;
    m->solo_lat = ui.solo_lat;
    m->preset_i = ui.preset_i;
    m->pick_engine = ui.pick_engine;
    m->pick_slot = ui.pick_slot;
    m->pick_weave = ui.pick_weave;
    m->pick_save = ui.pick_save;
    m->pick_kit = ui.pick_kit;
    m->pick_kslot = ui.pick_kslot;
    m->pick_clear = ui.pick_clear;
    m->pick_paste = ui.pick_paste;
    m->pick_shift = ui.pick_shift;
    m->pick_rot = ui.pick_rot;
    m->name_slot = ui.name_slot;
    m->slot = proj_slot;
    m->trk_sel = proj.trk_sel;
    m->dirty = ui.dirty;
    m->playing = seq.playing != 0;
    m->scene = seq.scene;
    m->cue = seq.cue;
    m->song_on = seq.song_on;
    m->mute = seq.mute;
    memcpy(m->ppat, (const void *)seq.ppat, sizeof m->ppat);
    m->list_open = lst.open && !lst.bt;                      /* (a Bluetooth list: not the copy's to show) */
    m->list_sel = m->list_open ? lst.sel : 0;
    m->list_top = m->list_open ? lst.top : 0;
    return sizeof *m;
}
void app_state_set(const uint8_t *buf, uint32_t n)
{
    const mirror_state_t *m = (const mirror_state_t *)(const void *)buf;
    if (n != sizeof *m || m->magic != MS_MAGIC)
        return;
    if (m->slot != proj_slot && m->slot < NPROJ) {          /* the project the FM-1 has open (the page read them all) */
        if (seq.playing)
            engine_transport(0);
        if (project_load(m->slot) == 0) {
            proj_slot = m->slot;
            after_load();
        }
    }
    proj.trk_sel = m->trk_sel;
    ui.page = m->page;
    ui.bar = m->bar;
    ui.dsound = m->dsound;
    ui.oct = m->oct;
    ui.last_inst = m->last_inst;
    ui.scene_pg = m->scene_pg;
    ui.mod_pat = m->mod_pat;
    ui.mod_lane = m->mod_lane;
    ui.glo_pg = m->glo_pg;
    ui.lockpg = m->lockpg;
    ui.trickpg = m->trickpg;
    ui.cursor = m->cursor;
    ui.rec_armed = m->rec_armed;
    ui.chordmode = m->chordmode;
    ui.chord_held = m->chord_held;
    ui.chord_bar = m->chord_bar;
    ui.chord_pat = m->chord_pat;
    ui.chord_pool = m->chord_pool;
    ui.rem_fx = m->rem_fx;
    ui.nrem = m->nrem;
    memcpy(ui.rem, m->rem, sizeof ui.rem);
    ui.solo = m->solo;
    ui.solo_lat = m->solo_lat;
    ui.preset_i = m->preset_i;
    ui.pick_engine = m->pick_engine;
    ui.pick_slot = m->pick_slot;
    ui.pick_weave = m->pick_weave;
    ui.pick_save = m->pick_save;
    ui.pick_kit = m->pick_kit;
    ui.pick_kslot = m->pick_kslot;
    ui.pick_clear = m->pick_clear;
    ui.pick_paste = m->pick_paste;
    ui.pick_shift = m->pick_shift;
    ui.pick_rot = m->pick_rot;
    ui.name_slot = m->name_slot;
    ui.dirty = m->dirty;
    ui.ask = 0;                                             /* (a question's answers are code: not carried) */
    ui.card = 0;
    if (!m->playing && seq.playing)
        engine_transport(0);
    else if (m->playing && !seq.playing)
        engine_transport(3);                                /* (from the top: the FM-1 was playing at connect) */
    seq.scene = m->scene;
    seq.cue = m->cue;
    seq.song_on = m->song_on;
    seq.mute = m->mute;
    memcpy((void *)seq.ppat, m->ppat, sizeof m->ppat);
    lst.open = 0;
    lst.bt = 0;
    if (m->list_open) {                                     /* the rows are the page's: built here, as SEL builds them */
        lst.open = 1;
        list_build();
        lst.sel = m->list_sel < lst.n ? m->list_sel : 0;
        lst.top = m->list_top < lst.n ? m->list_top : 0;
    }
    memset(ui.sig, 0, sizeof ui.sig);                       /* (the screen drawn again, whole) */
}

#ifdef PLAT_HOST
#include "host.c"                                   /* the host simulator's part: LOOM-8's checks and commands */
#endif

/* the platform's own code after this (fm1-base's main_fm1.c) draws with its own fonts again */
#undef felucca_font_t
#undef FONT_XS
#undef FONT_S
#undef FONT_B
#undef FONT_M
#undef FONT_L
#undef cv_begin
#undef cv_line
#undef cv_text
#undef text_w
#undef draw_text_box
