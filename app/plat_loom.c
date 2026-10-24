/* SPDX-License-Identifier: GPL-3.0-only */
/* LOOM-8 on the FM-1: its platform extras beyond fm1-base's plat.h (app.h), on felucca-bsp's helpers (the second
 * core is fm1-base's: fm1.toml cpu2 = true, plat_cpu2_*). Part
 * of the app's unity build (app.c), compiled inside fm1-base's (device/unity.c: after the platform, before its
 * main_fm1.c); the simulator's are in host.c. */
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

/* boot (app_init): the profiler's clock, the supplies */
static void plat_loom_init(void)
{
    vdc14_level = (uint8_t)fm1_vdc14();
    sysvdd_level = (uint8_t)fm1_sysvdd();
}
/* the second core at power-on, before the audio and the timers, as X0X starts it; one core after a failed start */
static int plat_loom_two_cores_ok(void) { return !bootguard.failed; }
