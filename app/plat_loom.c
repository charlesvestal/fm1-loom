/* SPDX-License-Identifier: GPL-3.0-only */
/* LOOM-8 on the FM-1: its platform extras beyond fm1-base's plat.h (app.h), on felucca-bsp's helpers and the
 * second core of cpu1/ (X0X's, kept by the app: it is GPL, felucca-bsp's own second core is not tried yet). Part
 * of the app's unity build (app.c), compiled inside fm1-base's (device/unity.c: after the platform, before its
 * main_fm1.c); the simulator's are in host.c. */
#include "cpu1/loom_cpu1.h"
#include "loom_build.h"                 /* LOOM_BUILD: this build's sources, hashed (tools/gen_build.py) */

/* the second core (X0X's plat_cpu2_*) */
int plat_cpu2_set(int on)
{
    int rc;
    if (!on) {
        fm1_cpu1_hold();
        return 0;
    }
    {
        int locked = fm1_guard_top_locked();            /* at boot the top is not locked yet: leave it so */
        if (locked)
            fm1_guard_unlock_top();                     /* its entry word is in the protected top of RAM */
        fm1_guard_pc_open();                            /* it starts in the chip's ROM */
        rc = fm1_cpu1_start();
        fm1_guard_enable(FM1_GUARD_PC);
        if (locked)
            fm1_guard_lock_top();
    }
    return rc;
}
int plat_cpu2_on(void) { return fm1_c1_on; }
int plat_cpu2_run(void (*fn)(uint32_t), uint32_t arg) { return fm1_cpu1_run(fn, arg); }
int plat_cpu2_wait(void) { return fm1_cpu1_wait(); }
void plat_cpu2_sync(void) { FM1_C1_SYNC(); }
int plat_cpu2_busy(void)                           /* (core 0 reads core 1's word: core 0's reads are sound) */
{
    FM1_C1_SYNC();
    return fm1_c1_mb.done != fm1_c1_mb.job && !fm1_c1_mb.bad;
}
int plat_cpu2_gave_up(void) { return fm1_c1_gave_up; }
uint32_t plat_cpu2_bad(void) { return fm1_c1_mb.bad; }
int plat_cpu2_barred(void) { return fm1_c1_bar.magic == FM1_C1_BAR_MAGIC && fm1_c1_bar.barred == 1u; }

/* first thing at a start (fm1-base's APP_CSTART): a second core that a reset left running would read the RAM the
 * start clears (Jangada, fm1-x0x#10): held. Registers only */
void app_cstart(void) { fm1_cpu1_halt(); }

/* a crash (fm1-base's APP_FAULT), before its red screen. On the second core (its sp on its own stacks): the record
 * only (felucca-bsp's, written), then it stops here: the screen and the reset would be core 1's, and fm1_reboot holds
 * core 1 first, so it would hold itself and the FM-1 would freeze. Core 0 gives it up when its job does not come
 * back (fm1_cpu1_wait) and goes on with one core. On core 0 beside a running second core: one core until power-off
 * or a new build (fm1_c1_bar) */
static void app_fault(const fm1_crash_t *c)
{
    if (c->sp >= (uint32_t)(uintptr_t)_c1_ustack && c->sp <= (uint32_t)(uintptr_t)_c1_sstack_top) {
        fm1_c1_crashed = FM1_C1_BAR_MAGIC;
        FM1_C1_SYNC();
        for (;;)
            ;
    }
    if (fm1_c1_on)
        fm1_cpu1_bar();
}

/* the bar is for this build's sake: a new build (another LOOM_BUILD) starts with two cores again */
static void cpu2_bar_build(void)
{
    if (fm1_c1_bar.magic == FM1_C1_BAR_MAGIC && fm1_c1_bar.build != LOOM_BUILD)
        fm1_c1_bar.barred = 0;
    fm1_c1_bar.build = LOOM_BUILD;
}

/* the render's load: measured around each block (app_render), its peak for the Status page */
static volatile uint32_t audio_peak_pct;
uint32_t plat_cpu_peak_pct(void)
{
    uint32_t p = audio_peak_pct;
    audio_peak_pct = 0;
    return p;
}
static uint32_t render_t0(void) { return fm1_ticks(); }
static uint32_t render_pct(uint32_t t0, uint32_t n)   /* the block's share of its time (n frames at 44.1 kHz), % */
{
    uint32_t us = (fm1_ticks() - t0) / FM1_TICKS_PER_US, budget = n * 1000000u / 44100u, pct = us * 100u / budget;
    if (pct > audio_peak_pct)
        audio_peak_pct = pct;
    return pct;
}

/* the profiler's clock: the 24 MHz timer, which both cores read alike (the CPU's cycle counter is the first core's:
 * the second core's job is timed too, engine.c core2_job) */
uint32_t plat_cycles(void) { return fm1_ticks(); }

/* the supplies as fm1-base set them at start (the Status page), the last crash */
static uint8_t vdc14_level, sysvdd_level;
int plat_vdc14_mv(void) { return 1250 + 50 * vdc14_level; }
int plat_sysvdd_mv(void) { return 930 + 30 * sysvdd_level; }
int plat_last_crash(uint32_t v[5])                   /* fm1_crash (.noinit): a warm reset keeps it, power-off clears it */
{
    if (fm1_crash.magic != FM1_CRASH_MAGIC)
        return 0;
    v[0] = fm1_crash.vec, v[1] = fm1_crash.pc, v[2] = fm1_crash.emu, v[3] = fm1_crash.dbg, v[4] = fm1_crash.rets;
    return 1;
}

/* the copy before the current one (storage.c keeps two): a stream torn across objects falls back to the save before
 * (project.c load_stream). On fm1-base's storage.c, compiled before the app in the same unit */
int plat_store_load_prev(uint32_t obj, void *dst, uint32_t max)
{
    uint32_t i;
    st_hdr_t h;
    int cur;
    if (!flash_ok)
        return -1;
    cur = st_current(obj, &h);
    if (cur < 0 || st_head(obj, (uint32_t)!cur, &h) || st_body(obj, (uint32_t)!cur, &h))
        return -1;
    if (h.len > max)
        h.len = max;
    for (i = 0; i < h.len; i++)
        ((uint8_t *)dst)[i] = st_buf[i];
    return (int)h.len;
}
int loom_store_save(uint32_t obj, const void *src, uint32_t len) { return plat_store_save(obj, src, len); }

/* boot (app_init): the profiler's clock, the supplies, the bar */
static void plat_loom_init(void)
{
    vdc14_level = (uint8_t)fm1_vdc14();
    sysvdd_level = (uint8_t)fm1_sysvdd();
    cpu2_bar_build();
}
/* the second core at power-on, before the audio and the timers, as X0X starts it; one core after a failed start */
static int plat_loom_two_cores_ok(void) { return !bootguard.failed; }
