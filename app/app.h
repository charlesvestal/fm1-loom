/* SPDX-License-Identifier: GPL-3.0-only */
/* LOOM-8: the project (everything that is saved), the app's entry points and its half of the contract with fm1-base
 * (base/device/unity.c, base/host/plat_host.c). Included first by fm1-base's unity build and by its simulator. */
#pragma once
#include <stdint.h>
#include "plat.h"
#include "seq/pattern.h"
#include "seq/seq.h"
#include "dsp/synth.h"
#include "dsp/fx.h"

#define APP_NAME "LOOM-8"
#define APP_BLOCK 256u                  /* frames a render (5.8 ms): fm1-base's USB audio input takes 256-frame halves
                                         * at most (felucca-bsp usb.c); LOOM-8's own platform had 512-frame halves,
                                         * each rendered as two 256-frame calls, which is what the simulator always did */
#define FM1B_UAC_DEEP 1                 /* the USB audio input a half deeper: a song's busy blocks take most of their
                                         * time, which ran fm1-base's ring dry (holds in a recording; base/device/unity.c) */
#define APP_CANVAS_PX (240u * 120u)     /* the tallest band, the main one, is 240 x 120 */
#define APP_SYS_TEXT                    /* fm1-base's own screens (crash, update, restore, calibration) in LOOM-8's faces:
                                         * gfx_loom.c app_sys_*; fm1-base's are then not linked (18 KB) */

/* the store, all in the data area: the panel calibration (the platform's, where every fm1-base firmware keeps it),
 * the user presets at 0xDE000, then NPROJ project slots of PROJ_NOBJ objects each (the settings, then the LZ chunks
 * of the patterns), two 4 KB sectors an object, from 0x97000 up to 0xC1000. The user sample bank is raw flash
 * beside them, 216 KB in three windows (dsp/sample.h SMP_REG*, plat_data): 0xC1000..0xDBFFF (108 KB, after the
 * projects, up to the calibration), fm1-base's PLAT_XDATA1 0xE5000..0xFBFFF (92 KB, above the update's staging)
 * and PLAT_XDATA0 0x93000..0x96FFF (16 KB, above the app area); app.c checks that no store object lies in them.
 * Nothing of LOOM-8's is left free in the data area; above it, 0xE0000..0xE4FFF is the update's staging and
 * 0xFC000.. Felucca's settings pair (neither LOOM-8's). LOOM-8 was never released, so this map did not have to
 * keep its own platform's (four slots); it does, but for the fourth slot: the same numbers at the same places, so
 * what a development build saved in slots 1 to 3 and the presets carry over (the sample bank moved: upload it
 * again) */
#define NPROJ 3                         /* project slots in flash */
#define PROJ_NOSLOT 0xFFu                /* proj_slot of a new project when every slot is taken: SAVE TO gives it one */
#define PROJ_NOBJ 7
enum { OBJ_PANEL, OBJ_PRESETS, OBJ_PROJ0, OBJ_NOBJ = OBJ_PROJ0 + NPROJ * PROJ_NOBJ };
#define OBJ_PROJ(slot, k) (OBJ_PROJ0 + (slot) * PROJ_NOBJ + (k))
#define APP_STORE_MAP {0xDC000u, 0xDE000u, \
    0x97000u, 0x99000u, 0x9B000u, 0x9D000u, 0x9F000u, 0xA1000u, 0xA3000u, \
    0xA5000u, 0xA7000u, 0xA9000u, 0xAB000u, 0xAD000u, 0xAF000u, 0xB1000u, \
    0xB3000u, 0xB5000u, 0xB7000u, 0xB9000u, 0xBB000u, 0xBD000u, 0xBF000u}

/* backup and restore over USB (fm1-base's backup.c; base/tools/fm1_backup.py, base/web/backup): the user presets
 * and the three project slots, all or nothing. Not the user sample bank (raw flash, not store objects: its source is
 * the player's own WAVs, tools/loom_samples.py); a restore writes only the objects listed here, never its windows */
#define APP_BACKUP_OBJS {OBJ_PRESETS, \
    OBJ_PROJ0 + 0, OBJ_PROJ0 + 1, OBJ_PROJ0 + 2, OBJ_PROJ0 + 3, OBJ_PROJ0 + 4, OBJ_PROJ0 + 5, OBJ_PROJ0 + 6, \
    OBJ_PROJ0 + 7, OBJ_PROJ0 + 8, OBJ_PROJ0 + 9, OBJ_PROJ0 + 10, OBJ_PROJ0 + 11, OBJ_PROJ0 + 12, OBJ_PROJ0 + 13, \
    OBJ_PROJ0 + 14, OBJ_PROJ0 + 15, OBJ_PROJ0 + 16, OBJ_PROJ0 + 17, OBJ_PROJ0 + 18, OBJ_PROJ0 + 19, OBJ_PROJ0 + 20}
#define APP_BACKUP_MAGIC 0x42384D4Cu     /* "LM8B" */
#define APP_BACKUP_VERSION 1u
#define APP_BACKUP_WHAT "replaces projects, presets"
int app_backup_playing(void);
void app_backup_flush(void);
void app_backup_hold(int on);
void app_backup_restored(void);

/* the contract: the platform calls these (app.c) */
void app_init(void);                    /* boot: the user bank, the project, the presets, the engine, the second core */
void app_render(int32_t *lr, uint32_t n, uint32_t gain_q12);   /* audio interrupt: n stereo frames, 24-bit */
void app_panic(void);                   /* silence now (before an update rewrites the flash) */
void app_ui_init(void);
void app_frame(void);
void app_input_only(void);
void app_say(const char *a, const char *b);
#define APP_SPLASH                      /* the name over the eight tracks' inks woven as threads */
void app_splash(void);
#define APP_REDRAW                      /* the screen mirror lost a part (or a viewer came): every band again */
void app_redraw(void);
#define APP_SYSEX                       /* the user sample bank over USB-MIDI (samples.c: F0 7D 46 4C) */
int app_sysex(const uint8_t *p, uint32_t n);
#ifndef PLAT_HOST
#define APP_CSTART                      /* the second core held first thing at a start (plat_loom.c) */
void app_cstart(void);
#define APP_FAULT                       /* a crash: the second core given up, or this crash is its own (plat_loom.c:
                                         * app_fault, after felucca-bsp's fm1_crash_t) */
#endif

/* the host simulator's part (host.c): LOOM-8's checks and commands, the screen reader's dump */
#ifdef PLAT_HOST
#define APP_HOST_EXPECT
int app_expect(const char *what, int *got);
#define APP_HOST_CMD
int app_host_cmd(const char *cmd, const char *a, const char *b, const char *c, const char *d);
#define APP_HOST_SPEAKDUMP
void app_speak_dump(void);
#define APP_HOST_REBOOT
void app_host_reboot(void);
#define APP_HOST_FAILS
int app_host_fails(void);
#define APP_HOST_MS                     /* the voices' and the estimated load's peaks (host.c "stats") */
void app_host_ms(void);
#endif

/* LOOM-8's own platform extras, beyond fm1-base's plat.h: on the FM-1 in plat_loom.c (felucca-bsp's helpers and the
 * second core of cpu1/), in the simulator in host.c */
uint32_t plat_cpu_peak_pct(void);       /* the render's peak load since the last call, % */
uint32_t plat_cycles(void);             /* a cycle counter for the profiler (ISR) */
int plat_store_load_prev(uint32_t obj, void *dst, uint32_t max);   /* the copy before the current one (a torn stream) */
int loom_store_save(uint32_t obj, const void *src, uint32_t len);  /* plat_store_save, the host keeping the copy before */
/* the second core (X0X's plat_cpu2_*) */
int plat_cpu2_set(int on);              /* main loop: 0 = done; -1 = it did not start (one core) */
int plat_cpu2_on(void);
int plat_cpu2_run(void (*fn)(uint32_t), uint32_t arg);   /* ISR: 1 = the second core has it */
int plat_cpu2_wait(void);               /* ISR: 0 = done; -1 = it failed (and is off): do the job here */
void plat_cpu2_sync(void);             /* ISR: a barrier between the cores' reads and writes (render_sub's sharing) */
int plat_cpu2_busy(void);               /* ISR: 1 = the job handed over is not done yet (no waiting) */
int plat_cpu2_gave_up(void);            /* why it is off: 0 no, 1 it read a wrong job, 2 one did not finish, 3 barred */
uint32_t plat_cpu2_bad(void);
int plat_cpu2_barred(void);             /* 1: it was given up, or core 0 crashed beside it (until power-off or a new build) */
int plat_vdc14_mv(void);                /* VDC14 as set at start, mV (the Status page) */
int plat_sysvdd_mv(void);
int plat_last_crash(uint32_t v[5]);     /* 1: a crash since power-on (vec, pc, emu, dbg, rets as its red screen) */
/* the lights: lit buttons and keys; glow: the unlit buttons dim (PLAY's green one); dim_keys dim; red: PLAY red */
void loom_lights(uint32_t buttons, uint32_t keys, int glow, uint32_t dim_keys, int red);

/* a debug build (not tools/build.py --release): GLO's last page, Debug, with the load, the voices and CORES */
#define DEBUG_BUILD (APP_VERSION[0] == 'D' && APP_VERSION[1] == 'E' && APP_VERSION[2] == 'V' && \
                     (!APP_VERSION[3] || APP_VERSION[3] == 'T'))   /* DEV, or the load test's DEVTEST */
#define PROJ_MAGIC 0x384D4F4Cu          /* "LOM8" (was "XXY1": XY-1's projects start fresh) */
#define PROJ_FORMAT 12u                 /* (10, 0.5-beta's, and 11 load too: project.c migrate_old; older ones do not)
                                         * 12: a scene's kit sound mutes per kit track (scene_t kmute[NKITM]);
                                         * 11: 32 scenes, a scene's kit sound mutes (one mask);
                                         * 10: a kit key's own sends (DP_SEND1/2, the kit's track sends gone), a
                                         *     parameter's index past a byte (a lane's 16 bits, LANE_TUNE moved);
                                         * 9: the sound's octave (TP_OCT) moved TP_PLAYER on;
                                         * 8: the WEAVE track (its patterns, its settings: a track more);
                                         * 7: a kit step's sounds as bits (pattern.h kit_*: any of the 11);
                                         * 6: the engine's second page (TP_X1..X4: brocade's sub) moved TP_PLAYER on
                                         * (and every index after it); 5: the master's pump (master[] 12 -> 16
                                         * bytes, out of rsv[]); 4: no tape (its track and send), the reverb's modes, the master's tape,
                                         * and each drum key its envelope and filter (DP_N 9 -> 18); 2: the character
                                         * stage moved TP_PLAYER on (and every index after it) */
#define NUSER_PRESETS 16
#define NAME_LEN 12

typedef struct {
    uint8_t par[NTP];                   /* dsp/synth.h: the sound, player, mix, brain, channel, the drum kit */
    char name[NAME_LEN];                /* the preset's name, as loaded or saved */
    uint8_t rsv[4];
} trk_t;

/* the settings object: small, stored raw. The patterns (seq/pattern.h patterns_t, in the pool) are
 * an LZ stream across the slot's other objects */
typedef struct {
    uint32_t magic, format;
    char name[NAME_LEN];                /* (unused since projects lost their names: a project is its slot, "Proj 2";
                                         * kept, so format 10's projects load) */
    uint16_t tempo_x10;                 /* 400..3000 */
    uint8_t swing;                      /* unused since the grooves (a reserved byte; the UI may still write it) */
    uint8_t groove;                     /* the project's groove type (pattern.h GR_*); each track has its amount */
    uint8_t metronome;                  /* 0 off, 1 while recording, 2 on */
    uint8_t lights;                     /* 0 off, 1 keys and glow, 2 keys only */
    uint8_t send_clock, send_notes, one_core, usb_full;
    uint8_t scene;                      /* the scene to start on */
    uint8_t trk_sel;                    /* the track selected */
    uint8_t rsv[10];                    /* (format 4 had 14: its last four are master[0..3] now); rsv[0] the count-in,
                                         * [1] the theme, [2] REC's mode and SCENES SWITCH, [4..7] the save count
                                         * (project.c: the slot saved or loaded last opens at power-on) */
    uint8_t master[NMS];                /* fx.h MS_*: format 4's twelve end where these sixteen end */
    trk_t trk[NALLTRK];
} settings_t;

typedef struct {
    char name[NAME_LEN];
    uint8_t engine;
    uint8_t par[NTP_SOUND];
} user_preset_t;

extern settings_t proj;                 /* the project's settings (RAM) */
extern uint8_t proj_par[NALLTRK][NTP];  /* the tracks' par[] as one contiguous array: what the synth and the
                                         * sequencer read (engine_init gets it); engine_set / engine_set_all
                                         * keep it in step with proj.trk[].par (the UI writes those) */
extern patterns_t pats;                 /* its patterns, lanes, scenes, song (pool) */
extern user_preset_t user_presets[NUSER_PRESETS];
extern uint8_t proj_slot;               /* the slot loaded */

void project_defaults(void);
void project_random_sounds(uint32_t seed);   /* T2-T8: a random preset each of a part (bass, poly, pad, lead, keys, fx, pluck) */
int project_mem_pct(void);              /* how full the patterns' room in flash is, % (the last save or load) */
int project_load(int slot);             /* 0 = loaded; else defaults are in place */
int project_save(int slot);             /* 0 = saved and verified; -1 = full / failed */
void project_apply(void);               /* every value to the engine (boot, load) */
int project_exists(int slot);
int project_last_slot(void);            /* the slot saved or loaded last (0 when none holds a project) */
int project_touch(int slot);            /* the loaded slot the last: its settings saved again, its count the next */
int presets_load(void);
int presets_save(void);

void ui_init(void);
void ui_frame(void);
void ui_input_only(void);
void ui_redraw(void);
int ui_dirty(void);
int ui_asking(void);                             /* a question is up (the host's expect ask) */
void ui_say(const char *a, const char *b);
