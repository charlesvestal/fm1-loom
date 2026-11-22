/* SPDX-License-Identifier: MPL-2.0 */
/* LOOM-8's own exports for the browser build (app.h APP_WEB; base/web/emu/emu_web.c includes this at its end): the
 * WAV export page's worker (app/web/export) restores a backup, sends the sample bank as the backup page does (LOOM-8's
 * SysEx, F0 7D 46 4C: samples.c), opens a slot and plays its song or a scene to the end (render.c). */
#define WEB_EXPORT __attribute__((used, visibility("default")))

static uint8_t x_sx[1024];                       /* a SysEx frame from the page (F0 .. F7) */
WEB_EXPORT uint8_t *loom_x_buf(void) { return sim_img; }          /* a backup image goes straight where restore reads it */
WEB_EXPORT uint32_t loom_x_buf_max(void) { return sizeof sim_img; }
WEB_EXPORT int loom_x_restore(uint32_t n) { return n > sizeof sim_img ? 3 : sim_restore_image(n); }
WEB_EXPORT uint8_t *loom_x_sx_buf(void) { return x_sx; }
WEB_EXPORT int loom_x_sx(uint32_t n)             /* the frame in loom_x_sx_buf to the app, as USB hands it on */
{
    if (n < 2u || n > sizeof x_sx || x_sx[0] != 0xF0u || x_sx[n - 1u] != 0xF7u)
        return -1;
    return app_sysex(x_sx + 1, n - 2u);
}
WEB_EXPORT uint8_t *loom_x_out(void) { return hsx_out; }          /* the app's answers, F0 .. F7 frames */
WEB_EXPORT uint32_t loom_x_out_len(void) { return hsx_n; }
WEB_EXPORT void loom_x_out_clear(void) { hsx_n = hsx_lost = 0; }
WEB_EXPORT uint32_t loom_x_slot(int slot)        /* a slot's tempo_x10 | scenes << 16, 0 for an empty one */
{
    uint16_t tx;
    uint8_t ns;
    return project_slot_info(slot, &tx, &ns) ? (uint32_t)tx | (uint32_t)ns << 16 : 0u;
}
static void x_settle(void)                       /* a transport stopping or starting: let it land, then drop the
                                                  * audio made meanwhile (a render starts from its own first frame) */
{
    for (int k = 0; k < 1000 && (seq.playing || seq.req_stop || seq.req_start); k++)
        run_ms(1);
    ring_r = ring_w;
}
WEB_EXPORT int loom_x_open(int slot)             /* slot's song slots, -1 empty */
{
    int r = loom_render_open(slot);
    x_settle();
    return r;
}
WEB_EXPORT uint32_t loom_x_scenes(void) { return loom_render_scenes(); }
WEB_EXPORT int loom_x_start(int scene, int loops)   /* scene < 0: the song; its bars */
{
    x_settle();
    return loom_render_start(scene, loops);
}
WEB_EXPORT int loom_x_playing(void) { return loom_render_playing(); }
