/* SPDX-License-Identifier: MPL-2.0 */
/* LOOM-8's render for the WAV export page (app/web/export; the browser build only, app/web.c exports it): a slot
 * opened as GLO's list opens it, then its song played once from the top, or one scene a number of times (a song of
 * that scene alone, made for the render: the wasm is thrown away after), to its end. The audio is fm1-base's
 * web_render. */

int loom_render_open(int slot)                   /* slot 0..2 loaded: its song's slots, or -1 (empty) */
{
    if (slot < 0 || slot >= NPROJ || !project_exists(slot))
        return -1;
    ui.ask_arg = (uint8_t)slot;
    do_load_proj();
    return pats.song.len;
}
uint32_t loom_render_scenes(void)                /* the scenes that exist: bit s for scene s + 1 */
{
    uint32_t m = 0;
    for (int s = 0; s < NSCENE; s++)
        m |= (uint32_t)(pats.scene[s].used != 0) << s;
    return m;
}
/* a pass of the scene in sixteenths: track 1's loop at its scale (seq.c bar_len_update; its TS_HALVES, half
 * sixteenths a step) */
static uint32_t scene_six(int sc)
{
    static const uint8_t HALVES[NTS] = {1, 2, 4, 6, 8, 10, 12, 16, 32};
    const tpat_t *p = &pats.pat[0][pats.scene[sc].pat[0] % NPATT];
    int f, e;
    pat_loop(p, &f, &e);
    return (uint32_t)(e - f) * HALVES[p->scale < NTS ? p->scale : TS_1] / 2u;
}
int loom_render_start(int scene, int loops)      /* scene < 0: the song once; else scene (0..) loops times. Its bars */
{
    song_t *sg = &pats.song;
    int bars = 0;
    if (scene >= 0) {                            /* (a song slot counts bars of 16 sixteenths: the passes rounded up
                                                  * to whole bars) */
        int sc = scene % NSCENE;
        int left = (int)((scene_six(sc) * (uint32_t)(loops < 1 ? 1 : loops) + 15u) / 16u);
        memset(sg, 0, sizeof *sg);
        while (left > 0 && sg->len < NSONG) {    /* (a slot plays SONG_BARS at most) */
            int b = left > SONG_BARS ? SONG_BARS : left;
            sg->slot[sg->len].scene = (uint8_t)sc;
            sg->slot[sg->len++].rep = (uint8_t)b;
            left -= b;
        }
    }
    sg->loop = 0;                                /* (once: its end stops the transport) */
    for (int i = 0; i < sg->len; i++)
        bars += sg->slot[i].rep ? sg->slot[i].rep : 1;
    ui.rec_armed = 0;                            /* (no count-in, no recording) */
    ui.page = P_SONG;                            /* as PLAY SONG: from the top */
    ui.name_slot = 0;
    play_press();
    return bars;
}
/* the song still going (or about to start): its end stops the transport at its last bar line (seq.c six_tick);
 * the tail is rendered after */
int loom_render_playing(void) { return seq.playing || seq.req_start; }
