/* SPDX-License-Identifier: MPL-2.0 */
/* LOOM-8's pages: the state the input layer (ui.c) and the screen share, and the drawing. Part of the unity
 * build, after project.c and before ui.c; the host simulator includes it too. Polly's framework: a header
 * (the page, the tempo, REC, unsaved; messages and questions in an alert box), the main band (the page's
 * picture), the tag (where you are: track > pattern > scene, the page dots), the four knobs' strip (with
 * the track's ink under the values), the encoders' line and the footer (what the keys do here).
 * Each band is drawn only when what it shows changed (a hash of it). Over the main band: a help card (a
 * button held alone), a list (SEL). The look: below. */

/* The look: a theme (THEMES[]), and in every theme each instrument track its own ink, so the steps, the arrange
 * rows and the mix say whose they are at a glance. On a track's pages its ink is K_NOW (solid: sounding now, the
 * playhead, the picture's line, the background's colour as text on it) and K_TINT (its ink thinned over the
 * background: the steps with notes). Red (K_REC) only for recording, locks and questions. */
typedef struct {
    const char *name;
    uint16_t bg, panel, panel2, line, faint, dim, text, now_m, rec, aux;
    uint16_t ink[NTRK];
    uint8_t tint, mid;                                /* the ink's share over the background, of 16 */
} theme_t;
/* the themes (Setup: THEME; the project keeps it in rsv[1]: theme_ix); b/w is the default */
static const theme_t THEMES[] = {
    {"tape deck", RGB(24, 18, 14), RGB(44, 34, 26), RGB(70, 56, 42), RGB(74, 60, 46), RGB(120, 100, 80),
     RGB(190, 170, 140), RGB(244, 230, 200), RGB(150, 130, 104), RGB(230, 70, 40), RGB(240, 220, 180),
     {RGB(230, 180, 50), RGB(214, 96, 48), RGB(160, 170, 70), RGB(70, 170, 160), RGB(110, 150, 200), RGB(180, 110, 150),
      RGB(210, 170, 120), RGB(150, 190, 150)}, 6, 9},
    {"blueprint", RGB(16, 48, 104), RGB(24, 62, 128), RGB(40, 84, 156), RGB(60, 100, 170), RGB(110, 140, 200),
     RGB(170, 195, 235), RGB(240, 246, 255), RGB(130, 160, 215), RGB(255, 110, 90), RGB(255, 255, 255),
     {RGB(255, 150, 190), RGB(120, 240, 230), RGB(255, 200, 80), RGB(180, 240, 120), RGB(255, 170, 110),
      RGB(210, 180, 255), RGB(240, 120, 120), RGB(200, 230, 200)}, 7, 11},
    {"b/w", RGB(0, 0, 0), RGB(30, 30, 34), RGB(58, 58, 64), RGB(60, 60, 66), RGB(96, 96, 102),
     RGB(150, 150, 156), RGB(235, 235, 238), RGB(130, 130, 138), RGB(232, 44, 38), RGB(215, 215, 220),
     {RGB(255, 92, 170), RGB(40, 204, 192), RGB(244, 200, 60), RGB(104, 152, 255), RGB(255, 136, 64),
      RGB(110, 216, 110), RGB(178, 136, 255), RGB(214, 176, 136)}, 6, 9},
};
#define NTHEMES (sizeof THEMES / sizeof THEMES[0])
#define THEME_BW 2
#define THEME_SET 0x80u                               /* rsv[1]: a theme picked since 0.11 (before: 0 the default, then the
                                                       * tape deck, now b/w; 1 and 2 as they were) */
#define LARGE_TYPE 0x40u                              /* rsv[1] with THEME_SET: large type (Setup > SEL > LARGE TYPE) */
static int theme_ix(void)
{
    uint32_t v = proj.rsv[1];
    return (int)(v & THEME_SET ? (v & 0x3Fu) % NTHEMES : v % NTHEMES ? v % NTHEMES : THEME_BW);
}
static void theme_step(int d)
{
    proj.rsv[1] = (uint8_t)(THEME_SET | (proj.rsv[1] & THEME_SET ? proj.rsv[1] & LARGE_TYPE : 0u) |
                            (uint32_t)(theme_ix() + (int)NTHEMES + d) % NTHEMES);
}
#if FM1B_MIRROR                                  /* the demo build (the live mirror): no large type, its ~10 KB of code
                                                      * for the mirror's; the setting is kept, not shown */
static int large_type(void) { return 0; }
#else
static int large_type(void) { return (proj.rsv[1] & (THEME_SET | LARGE_TYPE)) == (THEME_SET | LARGE_TYPE); }
#endif
static void large_type_set(int on)
{
    proj.rsv[1] = (uint8_t)(THEME_SET | (on ? LARGE_TYPE : 0u) | (uint32_t)theme_ix());
}
static const theme_t *theme = &THEMES[THEME_BW];
#define THEME_OF_PROJ() (&THEMES[theme_ix()])
#define AUTOSAVE_OFF 4u                              /* proj.rsv[2]: LOOM-8 saves only when SAVE is pressed */
#define LOOP_FREE 8u                                  /* proj.rsv[2]: a loop throw let go drifts (seq.loop_free) */
#define SCENES_AT_END 2u                              /* proj.rsv[2]: a scene's key waits for T1's pattern's end (bit 0: REC's mode) */
#define CC_OUT_OFF 16u                                /* proj.rsv[2]: no MIDI CCs out (midicc.c; on by default) */
#define SCENES_OUT 32u                                 /* proj.rsv[2]: a scene change's Program Change out (midicc.c;
                                                        * off by default, old and new projects alike) */
#define CC_IN_ENDLESS 1u                               /* proj.rsv[3]: CCs in from endless encoders, each a turn
                                                        * (midicc.c cc_delta; absolute by default) */
#define MOD_VIEW_OFF 2u                                /* proj.rsv[3]: no MOD VIEW (on by default, old FM-1s too) */
#define K_BG theme->bg
#define K_PANEL theme->panel
#define K_PANEL2 theme->panel2
#define K_LINE theme->line
#define K_DIM theme->dim
#define K_FAINT theme->faint
#define K_TEXT theme->text
#define K_NOW_M theme->now_m
#define K_REC theme->rec
#define K_SEL K_TEXT                                  /* the cursor, a held step, the knob last turned */
#define K_NOW k_now
#define K_TINT k_tint
#define K_NOW_T k_mid                                 /* the playhead on an empty step: the ink half thinned */
static uint16_t k_now, k_tint, k_mid;
static const theme_t *k_th;                           /* the theme k_now .. were made in */
static uint16_t trk_ink(int t) { return t >= 0 && t < NTRK ? theme->ink[t] : theme->aux; }
static uint16_t ink_mix(uint16_t ink, uint32_t a)    /* the ink at a/16 over the background */
{
    uint32_t pr = K_BG >> 11, pg = K_BG >> 5 & 63u, pb = K_BG & 31u;
    uint32_t r = ink >> 11, g = ink >> 5 & 63u, b = ink & 31u;
    return (uint16_t)(((r * a + pr * (16u - a)) / 16u) << 11 | ((g * a + pg * (16u - a)) / 16u) << 5 |
                      (b * a + pb * (16u - a)) / 16u);
}
static uint16_t trk_tint(int t) { return ink_mix(trk_ink(t), theme->tint); }
static uint16_t col_mix(uint16_t a, uint16_t b, uint32_t s)   /* a moved s/16 of the way to b */
{
    uint32_t ar = a >> 11, ag = a >> 5 & 63u, ab = a & 31u, br = b >> 11, bg = b >> 5 & 63u, bb = b & 31u;
    return (uint16_t)(((ar * (16u - s) + br * s) / 16u) << 11 | ((ag * (16u - s) + bg * s) / 16u) << 5 |
                      (ab * (16u - s) + bb * s) / 16u);
}

/* the pages (the spec's Buttons table): a button opens its first page; SELECT turns them. EDIT: the engine
 * (a drum track: the shown key's knobs) and Play (a drum track: the key's model, random, pan, volume); ENV: the
 * envelope, the filter, the filter envelope (a drum track: the shown key's envelope and filter, then the kit's
 * envelope and the kit's bus filter: four pages); FX: the track's own: its two inserts (a kit's on the whole
 * kit, after its bus filter), its sends and the throws (FX I and FX II are GLO's since 2026-11-26, after Mix:
 * page_group, grp_page); ARP three with the arp (its page 2, then hold). EDIT on a synth
 * track: the sound first, then how it plays: Engine, Sub where the engine has a second page of knobs (TP_X1..X4:
 * rayon's fall, acid's drive), Play (mode, glide, bend, octave), Play 2 (volume); rayon: Engine, Sub, Ops (P_KMOD),
 * Play, Play 2; knit: Engine, Osc 2 (osc 2's interval, fine, wave, shape), Mod (P_KMOD: sync, sweep, cross, the sub's
 * type), Levels (the Sub page: osc 2's level, sub, noise, analog), Play, Play 2; a kit or an FX track
 * Engine and Play; the conductor Engine, Play (routing), Sub (Follow) */
enum { P_ARRANGE, P_SONG, P_STEPS, P_BAR, P_ENGINE, P_PLAY, P_PLAY2, P_SUB, P_OSC2, P_KMOD, P_ENV, P_FILTER, P_ENV2, P_KITF, P_CHARACTER, P_INSERT, P_SENDS,
       P_FX1, P_FX2, P_THROWS, P_LFO, P_LFO2, P_PLAYERS, P_PLAYERS2, P_PLAYERS3, P_WEAVE, P_MIX, P_MASTER, P_MASTER2, P_MFILT, P_MASTER3, P_GLFO, P_SETUP,
       P_STATUS, P_MOD, NPAGES };   /* (P_MOD: the MOD track's one page, SEQ's when it is picked) */
static const char *const PAGE_NAME[NPAGES] = {"Arrange", "Song", "Steps", "Bar", "Engine", "Play", "Play 2", "Sub", "Osc 2", "Mod",
                                              "Envelope",
                                              "Filter", "Filter env", "Kit filter", "Insert 1", "Insert 2", "Sends", "FX I",
                                              "FX II", "Throws", "LFO", "LFO 2", "Players", "Players 2", "Hold", "Weave", "Mix",
                                              "Master EQ", "Master FX", "Master filter", "Master out", "LFO", "Setup",
                                              "Debug", "Lanes"};
static char *s_cat(char *b, const char *s);
static char *num_s(uint32_t v, char *b);
static const char *const GLFO_SHAPE[NGS + 1] = {"off", "sine", "triangle", "saw up", "saw down", "square", "random"};
static const char *const GLFO_DEST[NGD] = {"cutoff", "reso", "pitch", "pan", "volume", "FX I", "FX II", "insert 1"};
static void glfo_rate_text(int v, char *t)       /* synth.c glfo_hz: 16 bars .. 1/16 synced, then 0.02 .. 10 Hz */
{
    static const char *const SYNC[GL_SYNC_N] = {"16 bars", "8 bars", "4 bars", "2 bars", "1 bar", "1/2", "1/4", "1/8",
                                               "1/16"};
    if (v < GL_SYNC_N) {
        s_cat(t, SYNC[v]);
        return;
    }
    {
        uint32_t c = (uint32_t)(0.02f * fm_exp2f((float)(v - GL_SYNC_N) * (8.96578428f / 118.0f)) * 100.0f + 0.5f);
        char *e = num_s(c / 100u, t);
        *e++ = '.';
        *e++ = (char)('0' + c / 10u % 10u);
        *e++ = (char)('0' + c % 10u);
        s_cat(e, " Hz");
    }
}
static uint32_t glfo_route(void)                 /* the tracks it moves: bit t */
{
    const uint8_t *g = proj.trk[AUX_BRAIN].par;
    return (uint32_t)(g[TP_LX] & 15u) | (uint32_t)(g[TP_LX2] & 15u) << 4;
}
/* a master page's knob k: its value (fx.h MS_*); Master FX's are its slot's (slot_knobs) */
static int mst_m(int p, int k) { return p == P_MASTER ? MS_EQ_LO + k : p == P_MFILT ? MS_DFILT + k : MS_PERC + k; }
/* Master FX's slot as a track's slot 1 (synth.h TP_SLOT[0]): its knobs' names, ranges and texts as a track's are */
static const uint8_t MS_SLOT[SL_N] = {MS_CTYPE, MS_CAMT, MS_CTONE, MS_CK3, MS_CMIX, MS_CX1, MS_CX2, MS_CX3, MS_CX4};
static void mst_par(uint8_t *par)
{
    for (int i = 0; i < NTP; i++)
        par[i] = 0;
    for (int j = 0; j < SL_N; j++)
        par[TP_SLOT[0][j]] = proj.master[MS_SLOT[j]];
}
static const struct { uint8_t btn, first, n; } PAGE_BTN[] = {
    {B_HOME, P_ARRANGE, 2}, {B_SEQ, P_STEPS, 2}, {B_EDIT, P_ENGINE, 6}, {B_ENV, P_ENV, 4}, {B_FX, P_CHARACTER, 6},
    {B_LFO, P_LFO, 2}, {B_ARP, P_PLAYERS, 4}, {B_GLO, P_MIX, 8}, {B_SEQ, P_MOD, 1}};   /* (ranges; FX I and FX II
                                                     * are GLO's: page_group) */   /* (the last: the MOD track's,
                                                     * page_button) */
#define NPAGE_BTN (sizeof PAGE_BTN / sizeof PAGE_BTN[0])
#define G_GLO 7                                      /* (PAGE_BTN's GLO) */
static int page_group(int p)
{
    if (p == P_FX1 || p == P_FX2)                    /* (FX I and FX II: GLO's, after Mix) */
        return G_GLO;
    for (uint32_t i = 0; i < NPAGE_BTN; i++)
        if (p >= PAGE_BTN[i].first && p < PAGE_BTN[i].first + PAGE_BTN[i].n)
            return (int)i;
    return 0;
}
static int group_n(int g);                           /* the pages a button has now (the track decides) */
static const char *const BTN_LABEL[NB] = {"FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ",
                                          "PLAY", "REC", "OCT-", "OCT+"};

/* the tracks: 1-8, then the aux tracks (the tag's short names, the pages' long ones) */
static const char *const TRACK_NAME[NALLTRK] = {"T1", "T2", "T3", "T4", "T5", "T6", "T7", "T8", "COND", "THROW",
                                                "FX I", "FX II", "WEAVE"};
static const char *const NOTE_NAME[12] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};
/* the step components (SC_*: the OP-XY's 14, on the white keys with HOME and a step held), their letter on
 * the grid and their name */
static const char *const COMP_NAME[NSC] = {"none", "pulse", "hold", "multiply", "velocity", "ramp up", "ramp down",
                                           "random", "glide", "bend", "tonality", "jump", "skip lock",
                                           "skip trick", "skip trig", "flam"};
static const char *const COMP_GLYPH[NSC] = {"", "P", "H", "M", "V", "^", "v", "?", "~", "B", "T", "J", "L", "C", "S",
                                           "F"};
static const char *const VEL_VAL[10] = {"4", "8", "16", "32", "64", "100", "112", "127", "0", "random"};
static const char *const RAMP_VAL[10] = {"2 in 1", "3 in 1", "4 in 1", "5 in 1", "6 in 1", "2 in 3", "3 in 3", "4 in 3",
                                         "5 in 3", "random"};   /* steps over one / three octaves */
static const char *const PORTA_VAL[10] = {"10%", "20%", "30%", "40%", "50%", "60%", "70%", "80%", "off", "random"};
static const char *const BEND_VAL[10] = {"down-up", "up-down", "bump dn", "bump up", "spring out", "spring in",
                                         "fade dn", "fade up", "random 1", "random 2"};
static const char *const TON_VAL[10] = {"ignore", "transpose", "oct up", "5th up", "3rd up", "chrom up", "chrom dn",
                                        "q 33%", "q 66%", "q 100%"};
static const char *const JUMP_MOVE[6] = {"forward", "back", "fwd/back", "stay", "align", "random"};   /* 4..9 */
static const char *const TS_NAME[NTS] = {"1/2", "1", "2", "3", "4", "5", "6", "8", "16"};
static const uint8_t BAR_SCALE_KEY[9] = {TS_1, TS_2, TS_3, TS_4, TS_5, TS_6, TS_8, TS_16, TS_HALF};   /* Bar's black keys */
static const char *const LIGHTS_NAME[3] = {"off", "on", "keys"};
/* Setup's CLOCK: the knob's order (internal, receive, send) and the stored SEQ_CLK_* (0, 1 were off, on) */
static const char *const CLOCK_NAME[SEQ_NCLK] = {"internal", "receive", "send"};
static const uint8_t CLOCK_POS[SEQ_NCLK] = {0, 2, 1};                  /* SEQ_CLK_* -> the knob's position */
static const uint8_t CLOCK_AT[SEQ_NCLK] = {SEQ_CLK_INTERNAL, SEQ_CLK_RECEIVE, SEQ_CLK_SEND};
static const char *const CLOCK_ROW[SEQ_NCLK] = {"internal", "sends", "receives"};
static const char *const METRO_NAME[3] = {"off", "rec", "on"};
static const char *const ONOFF[2] = {"off", "on"};
static const char *const LINK_NAME[9] = {"off", "T1", "T2", "T3", "T4", "T5", "T6", "T7", "T8"};
static const char *const FOLLOW_NAME[NBM] = {"no", "diatonic", "transpose", "harmony", "ch. deg"};
static const char *const CORE2_NAME[4] = {"one core", "two cores", "lost", "barred"};
static const char *const USBA_NAME[3] = {"follows knob", "full", "stereo"};
static const char *const BRAIN_KNOB[3] = {"key", "scale", "link"};

/* the keys: 0 = F3; the white and the black ones in order */
static const uint8_t WHITE_K[16] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};
static const uint8_t BLACK_K[11] = {1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25};
#define KEY_BASE 53                                  /* F3 at octave 0 */
enum { KM_NONE, KM_PLAY, KM_STEP, KM_SCENE, KM_MUTE, KM_PUNCH, KM_BRAIN, KM_SOUND,
       KM_TRACK, KM_SAVE, KM_SOLO, KM_SCALE, KM_ROUTE, KM_STEPSOUND, KM_IDLE, KM_WEAVE, KM_BARK,
       KM_STEPCHORD };   /* KM_IDLE: does nothing */   /* what a held key does */

/* the input's and the screen's state */
static struct {
    uint32_t btn, keys, btn_used, btn_t[NB];
    uint32_t enc_t[NE];
    uint32_t act_t;
    char msg[2][32];                             /* (the second part up to 31: "set one on its ARP's Weave") */
    uint32_t msg_until;
    uint8_t msg_kind, msg_no;
    int8_t touched;
    uint32_t touch_until;
    uint32_t frame;
    uint8_t page;
    uint8_t any_btn;
    uint8_t card;                                    /* a help card on screen: its button + 1 */
    uint8_t ask;                                     /* a question on screen (SEL yes, HOME no) */
    char ask_q[48];
    void (*ask_yes)(void);
    void (*ask_no)(void);
    uint8_t ask_arg;
    uint8_t dirty;                                   /* the project changed since it was saved */
    uint8_t bar;                                     /* Steps: the bar shown 0..3 */
    uint8_t dsound;                                  /* a drum track: the key shown 0..10 */
    int8_t oct;                                      /* the keys' octave -4..4 */
    uint8_t rem[SN], nrem;                           /* the remembered notes (the last keys held together) */
    uint8_t rem_fx;                                  /* PUNCH: the remembered effect + 1 */
    uint8_t chord[8], nchord;                        /* the keys held together now (notes) */
    uint16_t held;                                   /* Steps: the white keys down as steps (bit = cell) */
    uint8_t held_first;                              /* the first held step (the one edited) */
    uint8_t held_used;                               /* a held step was edited: its release toggles nothing */
    uint8_t held_multi;                              /* more than one step has been held since the first: no release
                                                      * toggles a step */
    uint8_t len_key;                                 /* a later step held with the first alone, + 1: let go with
                                                      * nothing edited, the first's length to it (0 none) */
    uint8_t trickpg;                                 /* a step held + ARP: its tricks on the knobs */
    uint8_t chordmode;                               /* a step held, SEQ tapped: the keys write its chord, and stay
                                                      * doing so even if the step is let go (chord_held is who they
                                                      * write to). SEQ tapped again turns it off */
    uint16_t chord_held;                             /* chord mode: the steps it writes to (ui.held's snapshot when
                                                      * it turned on), in chord_bar of pattern chord_pat: a scene
                                                      * change while it is on moves neither */
    uint8_t chord_bar, chord_pat;
    uint8_t chord_pool;                              /* chord mode on a random track: the keys pick the pattern's
                                                      * pool (pattern.h LANE_POOL), not a step's chord */
    uint8_t chord_down;                              /* chord mode: the chord's keys still down (0: the next key
                                                      * starts a fresh chord, replacing the held steps' notes) */
    uint16_t wv_held, wv_latched;                    /* the WEAVE track's keys: the tracks woven now (seq.wv_live) */
    uint16_t wv_last;                                /* the tracks last woven by a key: what a tapped WEAVE step takes */
    uint8_t wv_step;                                 /* WEAVE's Steps: the step held (0xFF none) */
    uint8_t lockpg;                                  /* a step held + EDIT / ENV / FX / LFO: the lock editor's page + 1 */
    uint8_t cursor;                                  /* step record: the step the keys write */
    uint8_t rec_armed;                               /* REC tapped while stopped: step record */
    uint8_t srec_down;                               /* step record: the keys of the chord being written still down */
    uint8_t rec_held, rec_latched;                   /* REC down while playing (momentary); REC + PLAY latched it */
    uint8_t kmode[NKEYS], knote[NKEYS];              /* each held key: KM_*, its note / scene / fx */
    uint32_t key_t[NKEYS];
    uint32_t play_t;
    uint32_t punch_latched;
    uint8_t last_inst;                               /* the instrument track selected last (the FX pages audition it) */
    uint16_t solo;                                   /* the black keys held on Mix: the tracks soloed at the mixer (bit t) */
    uint16_t solo_lat;                               /* the tracks a double tap soloed: they stay soloed, let go */
    uint8_t solo_tap_trk, solo_tap_mut;              /* Mix: the last tap's track, the state it changed + 1 (0: none),
                                                      * and when it was let go (a double tap) */
    uint32_t solo_tap_t;
    uint16_t preset_i, pick_preset;               /* (more than 255 presets and user slots) */
    uint8_t pick_engine, pick_slot, pick_weave, pick_save;   /* (pick_save: SAVE TO's slot) */
    uint8_t pick_kit, pick_kslot;                    /* a kit track's KIT (0..3 factory, 4.. user) and SAVE KIT */
    uint8_t pick_bt;                                 /* Bluetooth audio's FORGET: the remembered device */
    uint8_t pick_clear, pick_paste;
    int8_t pick_shift, pick_rot;                     /* SHIFT NOTES' semitones (-24..24) and ROTATE's steps (-16 left ..
                                                      * 16 right), never 0 (0: the first's, 1) */        /* Steps' CLEAR (notes, locks, both), ROTATE (forward, back),
                                                      * PASTE (the copy, its locks only): kept from list to list */
    uint8_t name_slot;
    uint8_t playing;
    uint8_t scene_seen;
    uint8_t scene_pg;                                /* Arrange, Song: the white keys are scenes 1-16 (0) or 17-32 (1) */
    uint8_t scene_from, scene_made;                  /* a scene key's press: the scene playing before it (0xFF none),
                                                      * and whether the press made the scene (a copy) */
    uint32_t sig[6];
    uint8_t mod_pat;                                 /* the MOD page: the pattern it edits when the scene has none */
    uint8_t mod_lane;                                /* the lane picked (ALGORITHM), of the pattern's */
    uint8_t mod_tap, mod_tap_l;                      /* the step last tapped + 1 (KNOB 4 sets its value with no step
                                                      * held: mod_tap_step) and its lane (a pool index + 1) */
    uint8_t mod_learn;                               /* LEARN armed: the next knob turned by hand is a new lane's
                                                      * target (1), the lane picked's new one (2) */
    uint8_t mpick_t, mpick_p, mpick_k;               /* its SEL list's ADD LANE: the track (T1-T8, FX I, FX II, the
                                                      * master), its page (of mod_pages), its knob */
    uint8_t glo_pg;                                  /* the GLO page last visited (GLO opens it again; 0: Mix) */
    uint8_t cat_k, cat_n;                            /* the centre catch: the knob holding at the centre + 1, the
                                                      * detents it still holds for (centre_catch) */
} ui;

/* SEL: the screen as a list: the page's knobs, then its actions */
enum { LK_KNOB, LK_MORE, LK_ACTION, LK_PICK, LK_NOTE, LK_HEAD };   /* LK_NOTE: a line that says what to do; LK_HEAD:
                                                                    * a section's name (neither ever picked) */
enum { A_CLEAR_NOTES, A_CLEAR_LOCKS, A_CLEAR_BOTH, A_COPY_PAT, A_PASTE_PAT, A_ROT_FWD, A_ROT_BACK, A_DUP_BAR,
       A_ENGINE, A_PRESET, A_SCRAMBLE, A_SAVE_PRESET, A_INIT, A_FOLLOW, A_INSERT_SLOT, A_DELETE_SLOT, A_CLEAR_SONG,
       A_PLAY_SONG, A_NEW_PROJ, A_USB_AUDIO, A_CLEAR_SCENE, A_PANIC, A_EFFECT, A_REMOVE_NOTE,
       A_COUNT_IN, A_PLAYMODE, A_FTYPE, A_FX_HP, A_FX_LP, A_FX_SEND, A_WEAVE, A_THEME, A_PUMP_BY, A_PUMP_REL,
       A_PUMP_ACC, A_LEVEL, A_SCENE_AT, A_SAVE_TO, A_AUTOSAVE, A_RANDOM_SOUNDS, A_MIDI_OUT, A_LOOP_FREE, A_COPY_BAR,
       A_COPY_STEP, A_PASTE_BAR, A_PASTE_STEP, A_PASTE_LOCKS, A_DOUBLE, A_CLEAR_STEP_LOCKS, A_CC_OUT, A_SCENES_OUT,
       A_KIT, A_SAVE_KIT, A_MIDI_CC, A_USB_HOST, A_BT_MIDI, A_BT_AUDIO, A_BT_ADEV, A_BT_DEVICE, A_BT_DEV, A_BT_LOOK,
       A_BT_FORGET, A_BT_DISCONNECT, A_CLEAR, A_ROTATE, A_PASTE, A_COPY_EMPTY, A_CLEAR_BAR,
       A_COPY_SOUND, A_PASTE_SOUND, A_COPY_KEY, A_PASTE_KEY, A_COPY_SCENE, A_PASTE_SCENE, A_DUP_SLOT, A_KIT_FILTER,
       A_SHIFT, A_LARGE_TYPE, A_CC_IN, A_MOD_VIEW, A_CPU, A_PROJ1, A_PROJ2, A_PROJ3,
       A_MOD_LANE, A_MOD_REMOVE, A_MOD_CLEAR, A_MOD_TRK, A_MOD_PG, A_MOD_KNOB, A_MOD_ADD, A_MOD_LEARN,
       A_MOD_KIND, A_MOD_TTRK, A_MOD_TPG, A_MOD_TKNOB, A_MOD_TLEARN, A_GO_FX1, A_GO_FX2, A_K1LEVEL, A_KSWTO, A_SLOT_X1, A_SLOT_X2, A_SLOT_X3, A_SLOT_X4, NACTIONS };   /* (A_SLOT_X*: a slot page's
                                                     * extras; A_GO_FX*: Sends' way to GLO's FX I, FX II) */
static const char *const ACTION_NAME[NACTIONS] = {
    "CLEAR NOTES", "CLEAR LOCKS", "CLEAR NOTES AND LOCKS", "COPY PATTERN", "PASTE PATTERN", "ROTATE FORWARD",
    "ROTATE BACK", "DUPLICATE BAR", "ENGINE", "PRESET", "SCRAMBLE", "SAVE AS PRESET", "RESET SOUND", "FOLLOWS CONDUCTOR",
    "INSERT SLOT", "DELETE SLOT", "CLEAR SONG", "PLAY SONG", "NEW PROJECT", "USB AUDIO", "CLEAR SCENE",
    "ALL NOTES OFF", "EFFECT", "REMOVE", "COUNT-IN", "PLAY MODE", "FILTER TYPE", "HIGH-PASS", "LOW-PASS",
    "TO FX II", "KEEP AS PATTERN", "THEME", "PUMP BY", "RELEASE", "ACCENT", "MASTER LEVEL", "SCENES SWITCH", "SAVE TO", "AUTOSAVE",
    "RANDOMIZE PRESETS", "MIDI OUT", "LOOP THROWS", "COPY BAR", "COPY STEP", "PASTE BAR", "PASTE STEP",
    "PASTE LOCKS ONLY", "DOUBLE PATTERN", "CLEAR LOCKS", "CC OUT", "SCENES OUT", "KIT", "SAVE KIT", "KNOB CC",
    "USB PORT", "BT MIDI", "BT AUDIO", "AUDIO DEVICE", "MIDI DEVICE", "DEVICE", "LOOK AGAIN", "FORGET", "DISCONNECT",
    "CLEAR", "ROTATE", "PASTE", "COPY TO EMPTY", "CLEAR BAR", "COPY SOUND", "PASTE SOUND", "COPY DRUM", "PASTE DRUM",
    "COPY SCENE", "PASTE SCENE", "DUPLICATE SLOT", "KIT FILTER", "SHIFT NOTES", "LARGE TYPE", "CC IN",
    "MOD VIEW", "CPU", "PROJ 1", "PROJ 2", "PROJ 3", "LANE", "REMOVE LANE", "CLEAR LANE", "TRACK", "PAGE", "KNOB",
    "ADD LANE", "LEARN", "SHAPE", "TARGET TRACK", "TARGET PAGE", "TARGET KNOB", "LEARN TARGET", "FX I", "FX II", "OSC 1 LEVEL", "SWEEP TO", "EXTRA 1", "EXTRA 2", "EXTRA 3", "EXTRA 4"};
_Static_assert(NPROJ == 3, "GLO's list: an action a slot (A_PROJ1..A_PROJ3)");
static const char *const CLEAR_WHAT[3] = {"notes", "locks", "notes and locks"};
/* a pick ALGORITHM sets at once (a setting: SEL only leaves the list), as against a choice SEL then carries out */
static int pick_sets(int a)
{
    if (a >= A_SLOT_X1)                              /* (a slot's extras: the last actions, out of the switch's
                                                      * table) */
        return 1;
    switch (a) {
    case A_FOLLOW: case A_USB_AUDIO: case A_COUNT_IN: case A_THEME: case A_LARGE_TYPE: case A_PLAYMODE: case A_FTYPE:
    case A_FX_HP: case A_FX_LP: case A_FX_SEND: case A_PUMP_BY: case A_PUMP_REL: case A_PUMP_ACC: case A_LEVEL:
    case A_SCENE_AT: case A_AUTOSAVE: case A_MIDI_OUT: case A_LOOP_FREE: case A_CC_OUT: case A_SCENES_OUT: case A_MIDI_CC:
    case A_USB_HOST: case A_KIT_FILTER: case A_CC_IN: case A_MOD_VIEW: case A_CPU:
    case A_MOD_TRK: case A_MOD_PG: case A_MOD_KNOB: case A_MOD_KIND: case A_MOD_TTRK: case A_MOD_TPG: case A_MOD_TKNOB:
    case A_K1LEVEL: case A_KSWTO:
        return 1;
    default:
        return 0;
    }
}
/* a MIDI track's eight rows of A_MIDI_CC: each knob's CC (ui.c list_midi_knob reads the digit back) */
static const char *const MIDI_CC_ROW[8] = {"KNOB 1 CC", "KNOB 2 CC", "KNOB 3 CC", "KNOB 4 CC", "KNOB 5 CC",
                                           "KNOB 6 CC", "KNOB 7 CC", "KNOB 8 CC"};
/* the weave's ways (pattern.h WV_*): what this pattern keeps of itself */
static const char *const WEAVE_NAME[NWV] = {"bars", "steps", "rhythm", "pitch"};
#define LIST_ROWS 40                  /* (HOME's, EDIT's: their pages' knobs, their actions, the sections' names) */
#define LIST_SHOWN 10                 /* rows of LIST_RH on the screen (a section's name is LIST_HH) */
#define LIST_RH (LARGE ? 26 : 20)     /* (large type: taller rows, about seven on the screen) */
#define LIST_HH (LARGE ? 20 : 14)
static struct {
    uint8_t open, n, sel, top;
    uint8_t bt;                                      /* a Bluetooth list instead of the page's: 1 audio, 2 MIDI devices */
    struct { const char *name; char val[24]; uint8_t kind, id; } row[LIST_ROWS];
} lst LOOM_CR;

/* the undo: one stage, whatever was changed last (HOME + REC swaps it back, HOME + PLAY swaps again: redo): a
 * pattern with its locks, a track's sound (or T2-T8 at once), the master, or the arrangement (song, scenes, weave) */
enum { U_PAT, U_SOUND, U_MASTER, U_ARR };
#define UNDO_LANES 13                     /* (a pattern's 12 locks and its random pool) */
static struct {
    uint8_t valid, redo, kind, trk, n, pt, nl, rsv;
    uint32_t t;
    union {
        struct { tpat_t pat; lane_t lane[UNDO_LANES]; } p;
        trk_t s[NTRK - 1];
        uint8_t master[NMS];
        struct { song_t song; scene_t scene[NSCENE]; wvpat_t weave[NPATT]; } a;
    } u;
} undo LOOM_CR;
enum { CK_PAT, CK_BAR, CK_STEP };
static uint8_t clip_ok, clip_trk, clip_pat, clip_kind, clip_from;   /* COPY PATTERN / BAR / STEP: which (CK_*: the
                                                 * bar's or the step's first step), read at PASTE (no copy kept: the
                                                 * pool is short) */
static uint8_t snd_clip, key_clip, key_clip_k, scene_clip;   /* COPY SOUND (the track + 1), COPY DRUM (the kit track + 1,
                                                 * its key), COPY SCENE (the scene + 1): read at PASTE, as above */

/* ---------------------------------------------------------- helpers --- */
static char *num_s(uint32_t v, char *b)
{
    char t[12];
    int n = 0;
    do {
        t[n++] = (char)('0' + v % 10u);
        v /= 10u;
    } while (v);
    while (n)
        *b++ = t[--n];
    *b = 0;
    return b;
}
static char *num_i(int32_t v, char *b)
{
    if (v < 0) {
        *b++ = '-';
        v = -v;
    }
    return num_s((uint32_t)v, b);
}
static char *s_cat(char *b, const char *s)
{
    while (*s)
        *b++ = *s++;
    *b = 0;
    return b;
}
static uint32_t hash_u(uint32_t h, uint32_t v) { return (h ^ v) * 16777619u; }
static uint32_t hash_s(uint32_t h, const char *s)
{
    while (s && *s)
        h = hash_u(h, (uint8_t)*s++);
    return h;
}
static uint32_t s_len(const char *s)
{
    uint32_t n = 0;
    while (s[n])
        n++;
    return n;
}
static int s_eq(const char *a, const char *b)
{
    while (*a && *a == *b)
        a++, b++;
    return *a == *b;
}
static char *upcase(char *s)
{
    for (char *c = s; *c; c++)
        if (*c >= 'a' && *c <= 'z')
            *c = (char)(*c - 32);
    return s;
}
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
#include "bt.c"                                      /* Bluetooth audio and MIDI devices: their state, their words */
static char *note_name(int n, char *b)               /* "C4" (60), "Eb3" */
{
    char *e = s_cat(b, NOTE_NAME[(n % 12 + 12) % 12]);
    return num_i(n / 12 - 1, e);
}
static const int8_t SIN32[32] = {0, 20, 38, 56, 71, 83, 92, 98, 100, 98, 92, 83, 71, 56, 38, 20,
                                 0, -20, -38, -56, -71, -83, -92, -98, -100, -98, -92, -83, -71, -56, -38, -20};
static int32_t sin100(uint32_t k) { return SIN32[k & 31u]; }
static int32_t cos100(uint32_t k) { return SIN32[(k + 8u) & 31u]; }

/* a rounded box; the corners anti-aliased (each corner pixel by its distance from the corner's centre) */
static void cv_round(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint16_t c)
{
#ifdef GFX_AUDIT
    gfx_audit_box(x, y, w, h);
#endif
    if (r > h / 2)
        r = h / 2;
    if (r > w / 2)
        r = w / 2;
    if (r < 1) {
        cv_rect(x, y, w, h, c);
        return;
    }
    cv_rect(x, y + r, w, h - 2 * r, c);
    cv_rect(x + r, y, w - 2 * r, r, c);
    cv_rect(x + r, y + h - r, w - 2 * r, r, c);
    for (int32_t j = 0; j < r; j++)
        for (int32_t i = 0; i < r; i++) {
            float dx = (float)r - ((float)i + 0.5f), dy = (float)r - ((float)j + 0.5f), d2 = dx * dx + dy * dy;
            float cov = (float)r + 0.5f - (d2 > 1.0f ? f_sqrt1(d2) : 1.0f);
            uint32_t a = cov >= 1.0f ? 256u : cov <= 0.0f ? 0u : (uint32_t)(cov * 256.0f);
            cv_blend(x + i, y + j, c, a);                     /* the four corners, mirrored */
            cv_blend(x + w - 1 - i, y + j, c, a);
            cv_blend(x + i, y + h - 1 - j, c, a);
            cv_blend(x + w - 1 - i, y + h - 1 - j, c, a);
        }
}
static void cv_frame(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint16_t c, uint16_t in)
{
    cv_round(x, y, w, h, r, c);
    cv_round(x + 1, y + 1, w - 2, h - 2, r > 0 ? r - 1 : 0, in);
}
static void text_c(int32_t cx, int32_t y, const felucca_font_t *f, const char *s, uint16_t c)
{
    cv_text(cx - text_w(f, s) / 2, y, f, s, c);
}
static void text_r(int32_t xr, int32_t y, const felucca_font_t *f, const char *s, uint16_t c)
{
    cv_text(xr - text_w(f, s), y, f, s, c);
}
static int32_t cap_y(int32_t top, int32_t h, const felucca_font_t *f)
{
    int32_t c2 = f == &FONT_L ? 28 : f == &FONT_M ? 21 : 14;
    return (2 * top + h - 1 - c2) >> 1;
}
static int32_t text_fit(int32_t x, int32_t y, const felucca_font_t *f, const char *s, int32_t w, uint16_t c)
{
    char b[48];
    uint32_t n;
    for (n = 0; n < sizeof b - 1u && s[n]; n++)
        b[n] = s[n];
    b[n] = 0;
    while (n && text_w(f, b) > w)
        b[--n] = 0;
#ifdef GFX_AUDIT
    if (s[n])                                        /* (the layout audit: a text cut to fit is cut off on the screen) */
        ga_report("CUT TO FIT", s, b, x, y + cv_oy);
#endif
    return cv_text(x, y, f, b, c);
}
static uint32_t s_find_sp(const char *s)            /* the first space's place (the end: none) */
{
    uint32_t i = 0;
    while (s[i] && s[i] != ' ')
        i++;
    return i;
}
static int upper_only(const char *s)
{
    while (*s)
        if ((uint8_t)*s++ > 95u)
            return 0;
    return 1;
}
static int32_t pill_o(int32_t x, int32_t y, int32_t w, const char *s, uint16_t c, uint16_t fg)
{
    if (!w)
        w = text_w(&FONT_S, s) + 16;
    cv_round(x, y, w, 20, 6, c);
    cv_round(x + 1, y + 1, w - 2, 18, 5, K_BG);
    text_c(x + w / 2, y + 2, &FONT_S, s, fg);
    return x + w;
}
static int32_t wrap_do(int32_t x, int32_t y, int32_t w, const felucca_font_t *f, const char *s, uint16_t c, int draw)
{
    char line[48];
    while (*s) {
        uint32_t n = 0, cut = 0;
        while (s[n] && n < sizeof line - 1u) {
            line[n] = s[n];
            line[n + 1] = 0;
            if (text_w(f, line) > w)
                break;
            n++;
            if (!s[n] || s[n] == ' ')
                cut = n;
        }
        if (!cut)
            cut = n ? n : 1;
        line[cut] = 0;
        if (draw)
            cv_text(x, y, f, line, c);
        y += f->h > 17 ? f->h + 1 : 17;           /* (a line's pitch: 17, a larger face its height) */
        s += cut;
        while (*s == ' ')
            s++;
    }
    return y;
}
static int32_t wrap(int32_t x, int32_t y, int32_t w, const felucca_font_t *f, const char *s, uint16_t c)
{
    return wrap_do(x, y, w, f, s, c, 1);
}
#define SEP_W 13
static int32_t segs_w(const felucca_font_t *f, const char *s)
{
    char b[48];
    int32_t w = 0;
    uint32_t n = 0;
    for (;; s++) {
        if (*s == '|' || *s == '>' || !*s) {
            b[n] = 0;
            w += text_w(f, b);
            n = 0;
            if (!*s)
                return w;
            w += SEP_W;
        } else if (n < sizeof b - 1u)
            b[n++] = *s;
    }
}
static int32_t segs(int32_t x, int32_t y, const felucca_font_t *f, const char *s, uint16_t c)
{
    char b[48];
    uint32_t n = 0;
    for (;; s++) {
        if (*s == '|' || *s == '>' || !*s) {
            b[n] = 0;
            x = cv_text(x, y, f, b, c);
            n = 0;
            if (!*s)
                return x;
            if (*s == '>')
                for (int j = 0; j < 7; j++)
                    cv_rect(x + 4 + (j < 4 ? j : 6 - j), y + 6 + j, 2, 1, K_FAINT);
            else
                cv_round(x + 5, y + 8, 3, 3, 1, c);
            x += SEP_W;
        } else if (n < sizeof b - 1u)
            b[n++] = *s;
    }
}

/* --------------------------------------------------------- the model --- */
/* the bands, top to bottom: the header, the main band (a page's picture), the tag, the knobs, the hints (the
 * encoders' line, the footer). Large type (Setup > SEL > LARGE TYPE, gfx_loom.c loom_type) gives the header, the
 * tag and the knobs more room and the picture less; the hints stay where and as they are */
typedef struct { int16_t hdr_h, main_y, main_h, tag_y, tag_h, knb_y, knb_h, enc_y, foot_y; } bands_t;
static const bands_t BANDS[3] = {{24, 24, 120, 144, 14, 158, 50, 208, 224},
                                 {30, 30, 82, 112, 24, 136, 72, 208, 224},
                                 {30, 30, 98, 128, 24, 152, 72, 224, 224}};   /* (large, Steps: one hint row) */
static const bands_t *band = &BANDS[0];
#define HDR_H (band->hdr_h)
#define MAIN_Y (band->main_y)
#define MAIN_H (band->main_h)
#define TAG_Y (band->tag_y)
#define TAG_H (band->tag_h)                          /* the small font's descenders fit */
#define KNB_Y (band->knb_y)
#define KNB_H (band->knb_h)
#define ENC_Y (band->enc_y)
#define FOOT_Y (band->foot_y)
#if FM1B_MIRROR
#define LARGE 0                                      /* (the demo build: none, large_type) */
#else
#define LARGE (band != &BANDS[0])                    /* large type on (its faces there: gfx_loom.c lt_on) */
#endif
#define PIC_H (MAIN_H - 32)                          /* an engine's picture under its name (88 today) */
#define LY(a, b) (LARGE ? (b) : (a))                 /* a coordinate today, in large type */
#define MY(y) (LARGE ? (y) * MAIN_H / 120 : (y))     /* a y in the main band, scaled to large type's shorter band */
#define F_CAP (&LFONT_XS)                            /* a page's explanation ("tracks that follow"): small in large type
                                                      * too, as the hints */
enum { AL_INFO, AL_DONE, AL_UNDO, AL_REDO, AL_ASK, AL_NO };

static int sel_trk(void) { return proj.trk_sel < NALLTRK ? proj.trk_sel : 0; }
static int mod_sel(void) { return proj.trk_sel == AUX_MOD; }   /* the MOD track picked (sel_trk: T1's, unused there) */
static int is_aux(int t) { return t >= NTRK; }
static int is_seqt(int t) { return t < NSEQT; }
static int steps_page(int p) { return p == P_STEPS || p == P_BAR; }   /* SEQ's pages: both write steps */
static int is_drum(int t) { return t < NTRK && proj.trk[t].par[TP_ENGINE] == EG_DRUMLINE; }
static int is_midi(int t) { return t < NTRK && proj.trk[t].par[TP_ENGINE] == EG_MIDI; }   /* (no sound: notes and CCs
                                                                                         * out, synth.h EG_MIDI) */
static int is_knit(int t) { return t < NTRK && proj.trk[t].par[TP_ENGINE] == EG_KNIT; }
static int is_rayon(int t) { return t < NTRK && proj.trk[t].par[TP_ENGINE] == EG_RAYON; }
static int is_tulle(int t) { return t < NTRK && proj.trk[t].par[TP_ENGINE] == EG_TULLE; }
static int is_lace(int t) { return t < NTRK && proj.trk[t].par[TP_ENGINE] == EG_LACE; }
static int is_tartan(int t) { return t < NTRK && proj.trk[t].par[TP_ENGINE] == EG_TARTAN; }
static int is_acid(int t) { return t < NTRK && proj.trk[t].par[TP_ENGINE] == EG_ACID; }   /* (its 303's own envelope:
                                                                                         * no Filter env page) */
/* a page's name: a drum track's Play page is its Key page, its Filter env page the kit's envelope */
static const char *page_name_of(int p, int trk)
{
    if (is_drum(trk) && p == P_PLAY)
        return "Key";
    if (is_drum(trk) && p == P_ENV2)
        return "Kit env";
    if (is_drum(trk) && p == P_PLAY2)
        return "Sends";
    if (trk == AUX_WEAVE && p == P_ENGINE)
        return "Weave";
    if (trk == AUX_BRAIN && p == P_SUB)
        return "Follow";
    if (trk < NTRK && !is_drum(trk) && !is_aux(trk) && p == P_PLAYERS3 && proj.trk[trk].par[TP_PLAYER] % NPL == PL_RAND)
        return "Players 3";                          /* (the arp's page 3 is Hold) */
    if (trk < NTRK && p == P_SUB && proj.trk[trk].par[TP_ENGINE] == EG_SAMPLE)
        return "Loop";
    if (is_knit(trk) && p == P_SUB)                  /* (not "Mix": GLO's Mix is the track's sends, pan, level) */
        return "Levels";
    if (is_rayon(trk) && p == P_KMOD)                /* (knit's Mod page's place: rayon's ops 3 and 4) */
        return "Ops";
    if (is_tulle(trk) && p == P_KMOD)                /* (and tulle's grains' shape and direction) */
        return "Grain";
    if (is_lace(trk) && p == P_SUB)                  /* (lace's: what sets its modes ringing) */
        return "Exciter";
    if (is_tartan(trk) && p == P_SUB)                /* (tartan's: its voices; then their levels and places) */
        return "Voices";
    if (is_tartan(trk) && p == P_KMOD)
        return "Morph";
    if ((is_drum(trk) || is_midi(trk)) && p == P_SUB)
        return "Engine 2";
    return PAGE_NAME[p % NPAGES];
}
/* the filter type the Filter pages' PRESET and their list turn: a drum track's key's own (its Envelope and
 * Filter pages), the kit bus's (Kit env, Kit filter), else the voice filter's */
static int ftype_param(int trk, int page)
{
    if (is_drum(trk) && (page == P_ENV || page == P_FILTER))
        return TP_DRUM(ui.dsound % DL_NSOUND, DP_FTYPE);
    return TP_FTYPE;
}
static int is_fx(int t) { return t == AUX_FX1 || t == AUX_FX2; }
static int cur_scene(void) { return seq.scene < NSCENE ? seq.scene : 0; }
static int cur_pat(int t) { return is_seqt(t) ? seq.ppat[t] % NPATT : 0; }
static tpat_t *tpat(int t) { return &pats.pat[is_seqt(t) ? t : 0][cur_pat(t)]; }
static int playing(void) { return seq.playing; }
#define NCIN 4                                       /* the count-in's choices (proj.rsv[0]): off, 1 bar, 2 bars, on note */
#define CIN_NOTE 3
static int recording(void) { return seq.rec || seq.rec_wait || ui.rec_armed; }
static int page_btn_held(int b);
/* a kit's EDIT pages: the white keys put the key shown (black keys pick it) on the bar's steps, as Steps does; while
 * recording, or with EDIT held, they play the kit as before */
static int kit_edit_steps(void)
{
    int t = sel_trk();
    return is_drum(t) && PAGE_BTN[page_group(ui.page)].btn == B_EDIT && !recording() && !page_btn_held(B_EDIT);
}
static int step_shown(int i) { return ui.bar * 16 + i; }
static step_t *chord_step(int t, int i)              /* chord mode's held cell i: in the bar and pattern it began in */
{
    return &pats.pat[is_seqt(t) ? t : 0][ui.chord_pat % NPATT].st[(ui.chord_bar * 16 + i) % NSTEP];
}
static int home_held(void) { return ui.btn >> B_HOME & 1u; }
/* REC held: its knobs are the record settings while stopped and on Steps and Bar; on a sound page while playing,
 * the page's own, recorded as they turn (a punch-in of motion, as the keys punch in notes) */
static int rec_settings(void)
{
    return (ui.btn >> B_REC & 1u) && !home_held() && (!playing() || steps_page(ui.page));
}
static int kit_slot(int t)                           /* a kit track's place among the kits (scene_t kmute), -1 none */
{
    int k = 0;
    if (!is_drum(t))
        return -1;
    for (int i = 0; i < t; i++)
        k += is_drum(i);
    return k < NKITM ? k : -1;
}
static int pl_type(int t) { return is_aux(t) || is_drum(t) ? PL_OFF : proj.trk[t].par[TP_PLAYER] % NPL; }   /* (a kit has no player) */
static int inst_trk(void) { return is_aux(sel_trk()) ? ui.last_inst % NTRK : sel_trk(); }   /* the keys' instrument */
static int fx_page(int p) { return p == P_FX1 || p == P_FX2; }
/* the track a page's knobs edit: the FX pages their effect track, Character and Sends the instrument */
static int page_trk(int p)
{
    return p == P_FX1 ? AUX_FX1 : p == P_FX2 ? AUX_FX2 : p == P_SENDS || p == P_CHARACTER || p == P_INSERT ? inst_trk()
                                                                                                  : sel_trk();
}
/* the slot pages (one layout, one code: Insert 1 and 2, FX I and II, Master FX): a page's slot (0, 1) and track, Insert
 * 1 slot 1 and Insert 2 slot 2 of the instrument, FX I / II their effect track's slot 1, Master FX the master's (trk
 * -1); slot_vals its values (Master FX's into mp, mst_par) */
static int slot_page(int p, int *trk)
{
    *trk = p == P_FX1 ? AUX_FX1 : p == P_FX2 ? AUX_FX2 : p == P_MASTER2 ? -1 : inst_trk();
    return p == P_INSERT;
}
static int slot_pg(int p) { return p == P_CHARACTER || p == P_INSERT || fx_page(p) || p == P_MASTER2 ? p : -1; }
static int fx_pg(int t) { return t == AUX_FX1 ? P_FX1 : P_FX2; }   /* an effect track's own page */
static int list_slot_pg(void) { return slot_pg(ui.page) >= 0 ? ui.page : fx_pg(sel_trk()); }   /* (an FX track's EDIT) */
static void mst_par(uint8_t *par);
static const uint8_t *slot_vals(int pg, uint8_t *mp, int *trk, int *q)
{
    *q = slot_page(pg, trk);
    if (*trk >= 0)
        return proj.trk[*trk].par;
    mst_par(mp);
    return mp;
}
static const uint8_t *mod_shown(int trk, const uint8_t *par, uint8_t *buf, int n);
static int kit_x_page(int t)                         /* a kit whose key shown has eight knobs: its
                                                      * knobs 5..8 on Engine 2 (TP_DRUMX) */
{
    return t < NTRK && proj.trk[t].par[TP_ENGINE] == EG_DRUMLINE &&
           dm_knobs(proj.trk[t].par[TP_DRUM(ui.dsound % DL_NSOUND, DP_MODEL)] % NDM) > 4;
}
static int x_page(int t)                             /* the engine has a second page of knobs (TP_X1..X4) */
{
    int e = t < NTRK ? proj.trk[t].par[TP_ENGINE] : -1, m = t < NTRK ? proj.trk[t].par[TP_E4] % NSM : 0;
    return e == EG_RAYON || e == EG_KNIT || e == EG_MIDI || e == EG_ACID || e == EG_TULLE || e == EG_LACE ||
           e == EG_TARTAN || (e == EG_SAMPLE && (m == SM_LOOP || m == SM_PINGPONG)) ||   /* (the
                                                                                     * sample's loop page: looping) */
           kit_x_page(t);
}
static int group_n(int g)
{
    int trk = sel_trk(), b = PAGE_BTN[g].btn;
    if (b == B_SEQ && trk == AUX_WEAVE)
        return 1;                                    /* (WEAVE's steps: no Bar page) */
    if (b == B_EDIT)                                 /* (grp_page: the conductor's Sub third; a kit, an FX track: two) */
        return trk == AUX_PUNCH || trk == AUX_WEAVE ? 1 : trk == AUX_BRAIN ? 3 : is_aux(trk) ? 2
             : is_knit(trk) ? 6 : is_rayon(trk) || is_tulle(trk) || is_tartan(trk) ? 5 : x_page(trk) ? 4 : 3;   /* (a kit: Engine, Key, Sends: the
                                          * key's own; Engine 2 after Engine when the key's model has eight knobs;
                                          * knit: Osc 2, Mod, Levels, Play, Play 2; rayon: Sub, Ops, Play, Play 2;
                                          * tulle and tartan as rayon: tartan had 4, its Play 2 out of reach) */
    if (b == B_ARP)                                  /* (grp_page: Players, Players 2, Hold the arp's, Weave) */
        return is_aux(trk) ? 1
             : is_drum(trk) || pl_type(trk) == PL_OFF ? 2
             : pl_type(trk) == PL_ARP || pl_type(trk) == PL_RAND ? 4 : 3;
    if (b == B_ENV)
        return is_drum(trk) ? 4 : is_midi(trk) ? 1 : is_acid(trk) ? 2 : 3;   /* a kit: the key's envelope, filter; the
                                                           * kit's envelope, filter (a MIDI track: none, page_button
                                                           * stays; acid: its 303's envelope and filter) */
    if (b == B_LFO && is_midi(trk))
        return 1;
    if (b == B_FX)
        return is_midi(trk) ? 1 : 4;                 /* (grp_page: Insert 1, Insert 2, Sends, Throws; a MIDI track the
                                                      * throws alone) */
    if (b == B_GLO)
        return DEBUG_BUILD ? 10 : 9;                 /* Debug last, in a debug build only */
    return PAGE_BTN[g].n;
}
/* a button's i-th page now (ARP's depend on the player: the arp has Hold before Weave; the conductor and the
 * throws only Weave), and a page's place among them (-1: not one of them now) */
/* a track's weave (its settings' rsv[0..2]): the track woven in + 1 (0: none), its pattern, the rule */
static int wv_from(int t) { return t < NSEQT && proj.trk[t].rsv[0] <= NSEQT ? proj.trk[t].rsv[0] : 0; }
static int wv_pat(int t) { return proj.trk[t].rsv[1] % NPATT; }
static int wv_rule(int t) { return proj.trk[t].rsv[2] % NWV; }
static int wv_cued(int t) { return proj.trk[t].rsv[3] & 1u; }   /* PLAYS: 0 always, 1 only on the WEAVE track */
static int wv_kind(int t) { return t == AUX_PUNCH ? WK_FX : is_drum(t) ? WK_KIT : WK_MEL; }
static int grp_page(int g, int i)
{
    if ((PAGE_BTN[g].btn == B_ENV || PAGE_BTN[g].btn == B_LFO) && is_midi(sel_trk()))
        return P_ENGINE;                             /* (a MIDI track has no sound to shape: its knobs instead) */
    if (PAGE_BTN[g].btn == B_EDIT && sel_trk() == AUX_BRAIN)   /* Engine, Play (routing), Sub (Follow) */
        return i == 2 ? P_SUB : P_ENGINE + i;
    if (PAGE_BTN[g].btn == B_EDIT && is_knit(sel_trk())) {   /* knit: osc 2, its Mod page, the levels, then how it
                                                      * plays */
        static const uint8_t KNP[6] = {P_ENGINE, P_OSC2, P_KMOD, P_SUB, P_PLAY, P_PLAY2};
        return KNP[i >= 0 && i < 6 ? i : 0];
    }
    if (PAGE_BTN[g].btn == B_EDIT && (is_rayon(sel_trk()) || is_tulle(sel_trk()) || is_tartan(sel_trk()))) {   /* rayon: its fall (Sub), its
                                                      * ops 3 and 4; tulle: its grains (Sub), their shape (Grain); then
                                                      * how it plays */
        static const uint8_t RYP[5] = {P_ENGINE, P_SUB, P_KMOD, P_PLAY, P_PLAY2};
        return RYP[i >= 0 && i < 5 ? i : 0];
    }
    if (PAGE_BTN[g].btn == B_EDIT && x_page(sel_trk())) {   /* a second page of knobs second, after Engine (rayon's
                                                      * fall, acid's drive, a looping sample's Loop, MIDI's or a
                                                      * kit's key of eight knobs: Engine 2) */
        static const uint8_t SMPP[4] = {P_ENGINE, P_SUB, P_PLAY, P_PLAY2};
        return SMPP[i & 3];
    }
    if (PAGE_BTN[g].btn == B_FX) {                   /* the track's own (a MIDI track: no sound for the inserts or
                                                      * the sends) */
        static const uint8_t FXP[4] = {P_CHARACTER, P_INSERT, P_SENDS, P_THROWS};
        return is_midi(sel_trk()) ? P_THROWS : FXP[i & 3];
    }
    if (PAGE_BTN[g].btn == B_GLO) {                  /* Mix, the two effects, the master's, the LFO, Setup, Debug */
        static const uint8_t GLP[10] = {P_MIX, P_FX1, P_FX2, P_MASTER, P_MASTER2, P_MFILT, P_MASTER3, P_GLFO, P_SETUP,
                                        P_STATUS};
        return GLP[i >= 0 && i < 10 ? i : 0];
    }
    if (PAGE_BTN[g].btn == B_ARP) {
        static const uint8_t ARPP[4] = {P_PLAYERS, P_PLAYERS2, P_PLAYERS3, P_WEAVE};
        static const uint8_t OTHER[3] = {P_PLAYERS, P_PLAYERS2, P_WEAVE};
        int trk = sel_trk();
        if (is_aux(trk))
            return P_WEAVE;
        if (is_drum(trk) || pl_type(trk) == PL_OFF)
            return i & 1 ? P_WEAVE : P_PLAYERS;      /* (a kit, or no player: its page shows it off, then Weave) */
        return pl_type(trk) == PL_ARP || pl_type(trk) == PL_RAND ? ARPP[i & 3] : OTHER[(i % 3 + 3) % 3];
    }
    return PAGE_BTN[g].first + i;
}
static int grp_index(int p)
{
    int g = page_group(p), n = group_n(g);
    for (int i = 0; i < n; i++)
        if (grp_page(g, i) == p)
            return i;
    return -1;
}
/* the TP_* a page's knob k edits, -1 none */
static int tp_page_param(int p, int k, int trk)
{
    static const int8_t M[][4] = {                    /* (a row a page, from P_ENGINE to P_MIX) */
        {TP_E1, TP_E2, TP_E3, TP_E4}, {TP_VMODE, TP_PORTA, TP_BEND, TP_OCT}, {-1, -1, -1, TP_VOLUME},
        {TP_X1, TP_X2, TP_X3, TP_X4}, {-1, -1, -1, -1}, {-1, -1, -1, -1},   /* (Osc 2, Mod: knit's, below) */
        {TP_A, TP_D, TP_S, TP_R},
        {TP_CUT, TP_RES, TP_FENV, TP_KTRK}, {TP_FA, TP_FD, TP_FS, TP_FR}, {TP_CUT, TP_RES, TP_FENV, TP_KTRK},
        {-1, -1, -1, -1}, {-1, -1, -1, -1},        /* (Insert 1, 2: their slots', below) */
        {TP_SEND1, TP_SEND2, TP_PAN, TP_LEVEL},
        {TP_E1, TP_E2, TP_E3, TP_E4}, {TP_E1, TP_E2, TP_E3, TP_E4}, {-1, -1, -1, -1},
        {TP_LTYPE, TP_LSPEED, TP_LAMT, TP_LDEST},
        {TP_LX, TP_LX2, -1, -1}, {TP_PLAYER, TP_P1, TP_P2, TP_P3}, {TP_AP1, TP_AP2, TP_AP3, TP_AP4},
        {TP_P4, -1, -1, -1}, {-1, -1, -1, -1}, {TP_SEND1, TP_SEND2, TP_PAN, TP_LEVEL}};
    if (p < P_ENGINE || p > P_MIX || p == P_THROWS || p == P_WEAVE || k < 0 || k > 3)
        return -1;
    if (fx_page(p) || ((p == P_CHARACTER || p == P_INSERT) && !is_aux(trk)))   /* a slot's three knobs and its mix (the
                                                      * effect tracks' own whatever trk is) */
        return TP_SLOT[p == P_INSERT][k == 3 ? SL_MIX : SL_K1 + k];
    if (p == P_SENDS)                                /* (a kit's: the key's own, below) */
        return is_aux(trk) ? -1 : is_drum(trk) && k < 2 ? TP_DRUM(ui.dsound % DL_NSOUND, DP_SEND1 + k)
                                                        : M[p - P_ENGINE][k];
    if (p == P_CHARACTER || p == P_INSERT)
        return -1;
    if (is_aux(trk)) {
        if (p == P_MIX)                              /* (the conductor and the throws: no sound, no fader) */
            return k == 3 && is_fx(trk) ? TP_CMIX : -1;
        if (trk == AUX_BRAIN && (p == P_ENGINE || p == P_SUB))   /* key, scale, link; the Follow page: its track */
            return p == P_SUB ? (k ? -1 : TP_E1) : k < 3 ? TP_E2 + k : -1;
        if (p == P_ENGINE)
            return is_fx(trk) ? tp_page_param(P_FX1, k, trk) : (trk == AUX_PUNCH || trk == AUX_WEAVE) && k ? -1 : TP_E1 + k;
        if (p == P_PLAY && is_fx(trk)) {             /* an FX track's page 2: high-pass, low-pass, the feed into FX II */
            static const int8_t F2[4] = {TP_FENV, TP_CUT, TP_SEND2, -1};
            return k == 2 && trk != AUX_FX1 ? -1 : F2[k];
        }
        return -1;
    }
    if (is_drum(trk)) {
        int key = ui.dsound % DL_NSOUND;
        if (p == P_ENGINE)
            return TP_DRUM(key, DP_K1 + k);
        if (p == P_SUB)                              /* the key's knobs 5..8 (a model's with eight) */
            return kit_x_page(trk) ? TP_DRUMX(key, k) : -1;
        if (p == P_PLAY) {
            static const uint8_t K2[4] = {DP_MODEL, DP_RANDOM, DP_PAN, DP_VOLUME};
            return TP_DRUM(key, K2[k]);
        }
        if (p == P_ENV) {                            /* the key's own envelope */
            static const uint8_t KE[4] = {DP_ATTACK, DP_HOLD, DP_DECAY, DP_LAG};
            return TP_DRUM(key, KE[k]);
        }
        if (p == P_FILTER) {                         /* and its filter (the type: PRESET) */
            static const uint8_t KF[4] = {DP_CUT, DP_RES, DP_FENV, DP_DRIVE};
            return TP_DRUM(key, KF[k]);
        }
        if (p == P_MIX)                              /* (a kit has no track sends: FX, Sends has its keys') */
            return k < 2 ? -1 : M[p - P_ENGINE][k];
        if (p == P_ENV2)                             /* the kit's envelope: lag, attack, random, length */
            return M[P_ENV - P_ENGINE][k];
        if (p == P_PLAY2)                            /* (a kit has no octave page: the key's sends, as FX's Sends) */
            return k < 2 ? TP_DRUM(key, DP_SEND1 + k) : -1;
    }
    if (p == P_SUB && !x_page(trk))
        return -1;
    if (is_midi(trk) && (p == P_PLAY || p == P_PLAY2)) {   /* MIDI's as they were: volume on Play, the octave alone */
        static const int8_t MP[2][4] = {{TP_VMODE, TP_PORTA, TP_BEND, TP_VOLUME}, {TP_OCT, -1, -1, -1}};
        return MP[p == P_PLAY2][k];
    }
    if (p == P_PLAY2 && k == 2)                      /* a synth's low cut, beside its volume (a kit: none) */
        return is_drum(trk) || is_midi(trk) ? -1 : TP_HPF;
    if (is_knit(trk) && p == P_SUB)                  /* knit's Levels: osc 2, sub, noise, analog (its volume: Play 2) */
        return k < 3 ? TP_X2 + k : TP_KANALOG;
    if (p == P_KMOD)                                 /* knit's Mod page: sync, sweep, cross, the sub's type; rayon's
                                                      * Ops page: how ops 3 and 4 join, op 3's ratio, depth, feedback */
        return is_knit(trk) || is_tartan(trk) ? TP_KSYNC + k : is_rayon(trk) ? TP_ROPS + k
             : is_tulle(trk) && k < 2 ? TP_KSYNC + k : -1;
    if (p == P_OSC2) {                               /* knit's osc 2: its interval, fine tune, wave, shape (past the
                                                      * int8 table) */
        static const uint16_t O2[4] = {TP_X1, TP_O2FINE, TP_O2WAVE, TP_O2SHAPE};
        return is_knit(trk) ? O2[k] : -1;
    }
    if (is_drum(trk) && (p == P_PLAYERS || p == P_PLAYERS2 || p == P_PLAYERS3))
        return -1;                                   /* a kit: no player (its page shows it off) */
    if (pl_type(trk) == PL_RAND) {                  /* random: P1..P3, AP1..AP4, octave */
        if (p == P_PLAYERS)
            return TP_PLAYER + k;
        if (p == P_PLAYERS2)
            return TP_AP1 + k;
        if (p == P_PLAYERS3)
            return k == 0 ? TP_P4 : k == 1 ? TP_RUP : -1;   /* the octaves down and up */
    }
    if (p == P_PLAYERS2 && pl_type(trk) != PL_ARP)
        return k == 0 ? TP_P4 : -1;
    if (p == P_PLAYERS3 && pl_type(trk) != PL_ARP)
        return -1;
    return M[p - P_ENGINE][k];
}

/* a pen for smooth curves: pen_to draws an anti-aliased line from the last point (pen_up lifts it) */
/* bands taller than the canvas (app.h APP_CANVAS_PX: 60 rows) draw in strips: the band's rows r0 .. r0 + CV_ROWS - 1, the
 * drawing as it would be on the whole band (cv_oy: the offset; cv_y0, cv_y1: the strip's rows), sent to its own rows */
#define CV_ROWS ((int32_t)(APP_CANVAS_PX / 240u))
static void strip_begin(int32_t band_h, int32_t r0, uint16_t bg)
{
    int32_t n = band_h - r0 < CV_ROWS ? band_h - r0 : CV_ROWS;
    cv_begin(240, (uint32_t)n, bg);
    cv_oy = -r0, cv_y0 = 0, cv_y1 = n;
#ifdef GFX_AUDIT
    ga_lo = -r0, ga_hi = band_h - r0, ga_quiet = r0 > 0;   /* (the first strip audits the whole band) */
#endif
}
typedef struct { float x, y, px, py; int down, steep; } pen_t;   /* (px, py: the last segment's start; steep: its axis,
                                                                 * -1 none yet) */
static void pen_up(pen_t *p) { p->down = 0; }
static void pen_to(pen_t *p, float x, float y, uint16_t c)
{
    if (p->down) {
        int st = f_abs(y - p->y) > f_abs(x - p->x);
        if (p->steep >= 0 && st != p->steep)     /* the curve turns to the other axis: the last segment's end column too */
            cv_line_ex(p->px, p->py, p->x, p->y, c, 1.7f, 1);
        p->steep = cv_line_ex(p->x, p->y, x, y, c, 1.7f, 0);
        p->px = p->x, p->py = p->y;
    } else
        p->steep = -1;
    p->x = x, p->y = y, p->down = 1;
}
static float ease_exp(float t)                       /* 1 -> 0 along an exponential, reaching 0 at t = 1 */
{
    static const float E4 = 0.0183156f;              /* exp(-4) */
    return (fm_expf(-4.0f * t) - E4) / (1.0f - E4);
}

/* ---------------------------------------------------------- alerts --- */
#define AL_X 28
static void alert_box(int kind)
{
    uint16_t fill = kind == AL_ASK ? K_REC : kind == AL_NO ? K_PANEL2 : K_PANEL;
    uint16_t edge = kind == AL_ASK ? K_TEXT : kind == AL_NO ? K_TEXT : K_DIM;
    uint16_t badge = kind == AL_ASK ? K_TEXT : kind == AL_NO ? K_TEXT : K_DIM;
    uint16_t mark = kind == AL_ASK ? K_REC : K_BG;
    int32_t bx = 6, by = (HDR_H - 16) / 2;
    cv_round(1, 1, 238, HDR_H - 2, 7, edge);
    cv_round(2, 2, 236, HDR_H - 4, 6, fill);
    cv_round(bx, by, 16, 16, 8, badge);
    if (kind == AL_DONE) {
        for (int j = 0; j < 3; j++)
            cv_rect(bx + 4 + j, by + 7 + j, 2, 2, mark);
        for (int j = 0; j < 5; j++)
            cv_rect(bx + 7 + j, by + 9 - j, 2, 2, mark);
    } else if (kind == AL_UNDO || kind == AL_REDO) {
        int f = kind == AL_REDO, o = f ? -1 : 1;
        cv_rect(bx + 5 + o, by + 5, 6, 2, mark);
        cv_rect(bx + (f ? 4 : 10) + o, by + 5, 2, 6, mark);
        cv_rect(bx + 5 + o, by + 9, 6, 2, mark);
        for (int j = 0; j < 3; j++)
            cv_rect(bx + (f ? 13 - j : 2 + j) + o, by + 6 - j, 1, 2 * j + 1, mark);
    } else
        text_c(bx + 8, cap_y(by, 16, &LFONT_B), &LFONT_B, kind == AL_ASK ? "?" : kind == AL_NO ? "!" : "i", mark);
}
static void tempo_text(char *t)                      /* "120.0" */
{
    char *e = num_s(proj.tempo_x10 / 10u, t);
    *e++ = '.';
    num_s(proj.tempo_x10 % 10u, e);
}
/* the engine's meters (eng_peak, the loudest since the last look), taken once a frame for everything that reads them:
 * the header's meter, the Mix page, the engine pictures */
static uint16_t ui_peak[NALLTRK];
static uint8_t ui_ghost[NTRK];                   /* (eng_ghost, taken with the meters) */
static void meters_take(void)
{
    for (int t = 0; t < NALLTRK; t++) {
        ui_peak[t] = eng_peak[t];
        eng_peak[t] = 0;
    }
    for (int t = 0; t < NTRK; t++) {
        ui_ghost[t] = eng_ghost[t];
        eng_ghost[t] = 0;
    }
}
static float lin_db(float g);
/* the header's meter, before the track's pill: what the track plays now (its level after the fader, falling 24 dB
 * a second, -45 to -9 dB: activity more than level; the aux tracks make no sound: what they do now lights it). 0 .. HM_H pixels */
#define HM_H 17
static int hdr_meter(int trk)
{
    static float db = -60.0f;
    static uint32_t last;
    uint32_t now = plat_ms(), dt = now - last;
    float v;
    last = now;
    if (trk >= NTRK && !is_fx(trk)) {            /* COND, THROW: a step with something on it, playing; WEAVE: a
                                                  * track weaving */
        const step_t *st;
        if (trk == AUX_WEAVE)
            return (seq.wv_live | (playing() ? seq.wv_rec : 0)) ? HM_H : 0;
        if (!playing() || trk >= NSEQT)
            return 0;
        st = &tpat(trk)->st[eng_step[trk] % NSTEP];
        return step_empty(st) ? 0 : HM_H;
    }
    v = lin_db((float)ui_peak[trk] * (1.0f / 16383.5f));
    db = fm_maxf(v, db - (float)(dt > 1000u ? 1000u : dt) * 0.024f);
    if (db <= -45.0f)
        return 0;
    return 1 + (int)((fm_minf(db, -9.0f) + 45.0f) * (float)(HM_H - 1) / 36.0f);
}
/* the battery, small and upright at the header's right (plat_battery): three bars from the foot, red at one or none;
 * a USB host powering the FM-1, a bolt instead. 7 wide; -1 (not read yet): nothing */
#define BATT_W (LARGE ? 10 : 7)                 /* (large type: half as big again, rows 3..25) */
static void draw_batt(int32_t x, int b)
{
    uint16_t c = b >= 0 && b <= 1 ? K_REC : K_DIM;
    if (LARGE) {                                     /* the nub, the body 20 px, three bars 4 px; charging: the bolt */
        cv_round(x + 3, 3, 4, 3, 0, c);
        cv_round(x, 6, BATT_W, 20, 2, c);
        cv_round(x + 1, 7, BATT_W - 2, 18, 1, K_BG);
        if (b == 4) {
            cv_linef((float)x + 6.4f, 9.0f, (float)x + 2.6f, 16.5f, K_TEXT, 1.6f);
            cv_linef((float)x + 2.6f, 16.5f, (float)x + 6.4f, 13.5f, K_TEXT, 1.6f);
            cv_linef((float)x + 6.4f, 13.5f, (float)x + 2.3f, 23.0f, K_TEXT, 1.6f);
            return;
        }
        for (int i = 0; i < b && i < 3; i++)
            cv_rect(x + 2, 20 - i * 6, BATT_W - 4, 4, b <= 1 ? K_REC : K_TEXT);
        return;
    }
    cv_round(x + 2, 3, 3, 2, 0, c);              /* the nub; all of it rows 3..17, by the tempo's digits (6..16) */
    cv_round(x, 5, BATT_W, 13, 1, c);
    cv_round(x + 1, 6, BATT_W - 2, 11, 0, K_BG);
    if (b == 4) {
        cv_linef((float)x + 4.3f, 7.0f, (float)x + 1.7f, 12.0f, K_TEXT, 1.2f);   /* (about column x + 3, the middle) */
        cv_linef((float)x + 1.7f, 12.0f, (float)x + 4.3f, 10.0f, K_TEXT, 1.2f);
        cv_linef((float)x + 4.3f, 10.0f, (float)x + 1.5f, 16.0f, K_TEXT, 1.2f);
        return;
    }
    for (int i = 0; i < b && i < 3; i++)
        cv_rect(x + 2, 14 - i * 3, BATT_W - 4, 2, b <= 1 ? K_REC : K_TEXT);
}
#ifdef PLAT_HOST
static int msg_too_wide;                         /* hints and messages that did not fit (the host fails on them) */
#endif
#if defined(PLAT_HOST) && !defined(PLAT_WEB)
static void hdr_cut_(const felucca_font_t *f, const char *s, int32_t room)   /* a message's part cut in the header */
{
    static char last[64];
    if (text_w(f, s) <= room || !strncmp(last, s, sizeof last - 1u))
        return;
    strncpy(last, s, sizeof last - 1u);
    printf("  FAIL message cut in the header: %s\n", s);
    msg_too_wide++;
}
#endif
static void draw_header(void)
{
    char t[40];
    int mh = 0;
    int msg = plat_ms() < ui.msg_until, rec = recording(), ext = seq.ext, ask = ui.ask, cnt = seq.count_in;
    int pk = BT_PASSKEY();                     /* a pairing's code, waiting to be typed on the phone */
    uint32_t h = hash_u(2166136261u + (uint32_t)(pk + 1) + ((uint32_t)lst.bt << 24), ui.page | (uint32_t)msg << 8 | (uint32_t)ui.dirty << 9 | (uint32_t)rec << 11 |
                                         (uint32_t)ext << 12 | (uint32_t)ask << 14 | (uint32_t)proj.tempo_x10 << 16);
    h = hash_u(h, (uint32_t)(plat_battery() + 1));
    h = hash_u(h, (uint32_t)cnt | (uint32_t)playing() << 8 | (uint32_t)is_drum(sel_trk()) << 9 |
                      (uint32_t)page_trk(ui.page) << 10 | (uint32_t)trk_ink(page_trk(ui.page)) << 16);
    h = hash_u(h, (uint32_t)cur_scene() | (uint32_t)seq.rec_wait << 8);
    if (msg)
        h = hash_u(h, ui.msg_until ^ (uint32_t)ui.msg_no << 31);
    if (ask)
        h = hash_s(h, ui.ask_q);
    meters_take();
    if (((ui.page >= P_STEPS && ui.page <= P_PLAYERS3) || ui.page == P_WEAVE) && ui.page != P_THROWS)
        mh = hdr_meter(page_trk(ui.page));
    h = hash_u(h, (uint32_t)mh);
    if (h == ui.sig[0])
        return;
    ui.sig[0] = h;
    cv_begin(240, HDR_H, K_BG);
    if (pk >= 0) {                                   /* (first: a pairing waits only so long) */
        int32_t x;
        alert_box(AL_INFO);
        x = cv_text(AL_X, 4, &FONT_B, "PIN", K_TEXT);
        for (int i = 5, v = pk; i >= 0; i--, v /= 10)
            t[i] = (char)('0' + v % 10);
        t[6] = 0;
        cv_text(x + 8, 4, &FONT_B, t, K_TEXT);
    } else if (ask) {
        alert_box(AL_ASK);
        if (text_w(&FONT_B, ui.ask_q) <= 206)
            cv_text(AL_X, 4, &FONT_B, ui.ask_q, K_TEXT);
        else if (text_w(&FONT_S, ui.ask_q) <= 208)
            cv_text(AL_X, 5, &FONT_S, ui.ask_q, K_TEXT);
        else
            text_fit(AL_X, 5, &FONT_XS, ui.ask_q, 232 - AL_X, K_TEXT);
    } else if (cnt && playing()) {
        int32_t x = cv_text(8, 4, &FONT_B, "Count-in", K_TEXT);
        num_s((uint32_t)cnt, t);
        cv_text(x + 8, 4, &FONT_B, t, K_REC);
        cv_round(222, 8, 9, 9, 4, K_REC);
    } else if (msg) {
        int no = ui.msg_no;
#if defined(PLAT_HOST) && !defined(PLAT_WEB)
#define hdr_cut(f, s, room) hdr_cut_(f, s, room)
#else
#define hdr_cut(f, s, room) ((void)0)
#endif
        int32_t x;
        alert_box(no ? AL_NO : ui.msg_kind);
        {   /* (large type: a long first part a size down, the second after it as room allows) */
            const felucca_font_t *mf = LARGE && text_w(&FONT_B, ui.msg[0]) > 140 ? &LFONT_B : &FONT_B;
            x = cv_text(AL_X, LARGE ? cap_y(0, HDR_H, mf) : 4, mf, ui.msg[0], K_TEXT);
        }
        {   /* the second part smaller when it does not fit (cut only past the smallest) */
            int32_t room = 232 - (x + 8);
            const felucca_font_t *f = text_w(&FONT_B, ui.msg[1]) <= room ? &FONT_B
                                    : text_w(&FONT_S, ui.msg[1]) <= room ? &FONT_S
                                    : !LARGE || text_w(&FONT_XS, ui.msg[1]) <= room ? &FONT_XS
                                    : text_w(&LFONT_S, ui.msg[1]) <= room ? &LFONT_S : &LFONT_XS;   /* (large: 15, 12) */
            if (LARGE && f == &LFONT_XS && text_w(f, ui.msg[1]) > room) {   /* (two lines of 12 px in the 30) */
                char l1[48];
                uint32_t n = 0, cut = 0;
                for (; ui.msg[1][n] && n < sizeof l1 - 1u; n++) {
                    l1[n] = ui.msg[1][n];
                    l1[n + 1] = 0;
                    if (l1[n] == ' ' && text_w(f, l1) <= room)
                        cut = n;
                }
                if (cut) {
                    l1[cut] = 0;
                    cv_text(x + 8, 2, f, l1, K_DIM);
                    hdr_cut(f, ui.msg[1] + cut + 1, room);
                    text_fit(x + 8, 14, f, ui.msg[1] + cut + 1, room, K_DIM);
                } else {
                    hdr_cut(f, ui.msg[1], room);
                    text_fit(x + 8, cap_y(0, HDR_H, f), f, ui.msg[1], room, K_DIM);
                }
            } else {
                if (LARGE)                           /* (the normal header's check: ui.c say_as) */
                    hdr_cut(f, ui.msg[1], room);
                text_fit(x + 8, LARGE ? cap_y(0, HDR_H, f) : f == &FONT_XS ? 6 : 4, f, ui.msg[1], room, K_DIM);
            }
        }
    } else if (seq.rec_wait && playing()) {      /* recording on the first note: waiting for it */
        cv_text(8, 4, &FONT_B, "Play a note", K_TEXT);
        cv_round(222, 8, 9, 9, 4, K_REC);
    } else {
        int32_t r = 232, x = 8, py = LARGE ? 3 : 4, ph = LARGE ? 24 : 17;   /* (large type: taller pills, 30 px) */
        const char *bn = BTN_LABEL[PAGE_BTN[page_group(ui.page)].btn];
        const char *pn = lst.open && lst.bt ? (lst.bt == 1 ? "Audio devices" : "MIDI devices") : page_name_of(ui.page, sel_trk());
        int32_t w = text_w(&FONT_XS, bn) + 6;
        if (((ui.page >= P_STEPS && ui.page <= P_PLAYERS3) || ui.page == P_WEAVE || ui.page == P_MOD) &&
            ui.page != P_THROWS) {                   /* a track's page (the MOD track's: its own pill) */
            int pt = ui.page == P_MOD ? AUX_BRAIN : page_trk(ui.page);
            const char *tn = ui.page == P_MOD ? "MOD" : TRACK_NAME[pt];
            int32_t tw = text_w(&FONT_XS, tn) + 10;
            cv_round(x, py, 5, ph, 2, K_PANEL);      /* the meter's well, then its level from the foot */
            if (mh)
                cv_round(x, py + ph - mh * ph / HM_H, 5, mh * ph / HM_H, 2, trk_ink(pt));
            x += 9;
            cv_round(x, py, tw, ph, ph / 2, trk_ink(pt));
            cv_text(x + 5, cap_y(py, ph, &FONT_XS) - 1, &FONT_XS, tn, K_BG);
            x += tw + 5;
        }
        if (LARGE)
            w += 4;
        {   /* large type: the button's pill gives way when the page's name would not fit even a size down */
            char tt[12];
            int32_t room;
            if (ext)
                s_cat(tt, "ext");
            else
                tempo_text(tt);
            room = r - (plat_battery() >= 0 ? BATT_W + 5 : 0) - text_w(&FONT_S, tt) - 8 - x;
            if (!LARGE || w + 6 + text_w(&LFONT_B, pn) <= room) {
                cv_round(x, py, w, ph, 3, K_DIM);
                cv_round(x + 1, py + 1, w - 2, ph - 2, 2, K_BG);
                cv_text(x + (LARGE ? 5 : 3), cap_y(py, ph, &FONT_XS) - 1, &FONT_XS, bn, K_DIM);
                x += w + 6;
            }
        }
        {   /* the page's name (large type: a size down when it would meet the tempo, the battery) */
            const felucca_font_t *nf = &FONT_B;
            if (LARGE) {
                char tt[12];
                if (ext)
                    s_cat(tt, "ext");
                else
                    tempo_text(tt);
                int32_t room = r - (plat_battery() >= 0 ? BATT_W + 5 : 0) - text_w(&FONT_S, tt) - 8 - x - (rec ? 16 : 0);
                if (text_w(nf, pn) > room)
                    nf = &LFONT_B;
                if (text_w(nf, pn) > room) {         /* (still too long: as far as it goes) */
                    x = text_fit(x, cap_y(0, HDR_H, nf), nf, pn, room, K_TEXT);
                    nf = 0;
                }
            }
            if (nf)
                x = cv_text(x, LARGE ? cap_y(0, HDR_H, nf) : 4, nf, pn, K_TEXT);   /* (pn: page_name_of) */
        }
        if (plat_battery() >= 0) {               /* the battery at the right, by the tempo */
            r -= BATT_W;
            draw_batt(r, plat_battery());
            r -= 5;
        }
        if (ext)
            s_cat(t, "ext");
        else
            tempo_text(t);
        r -= text_w(&FONT_S, t);
        cv_text(r, LARGE ? cap_y(0, HDR_H, &FONT_S) : 4, &FONT_S, t, K_TEXT);
        r -= 6;
        if (rec) {                                   /* (before the scene: the scene gives way) */
            r -= 10;
            cv_round(r, HDR_H / 2 - 4, 9, 9, 4, K_REC);
            r -= 6;
        }
        num_s((uint32_t)cur_scene() + 1u, s_cat(t, "S"));   /* the scene playing, on every page */
        if (r - text_w(&FONT_S, t) >= x + 6) {
            r -= text_w(&FONT_S, t);
            cv_text(r, LARGE ? cap_y(0, HDR_H, &FONT_S) : 4, &FONT_S, t, K_DIM);
            r -= 6;
        }
        if (ui.dirty && LARGE && r - 8 >= x + 4)    /* large type: unsaved, a dot */
            cv_round(r - 7, HDR_H / 2 - 3, 7, 7, 3, K_DIM);
        else if (ui.dirty && r - text_w(&FONT_XS, "unsaved") >= x + 6)   /* (where it fits: a long name before it) */
            cv_text(r - text_w(&FONT_XS, "unsaved"), 5, &FONT_XS, "unsaved", K_DIM);
    }
    cv_blit(0, 0);
}

/* ------------------------------------------------------- MOD VIEW --- */
/* MOD VIEW (Setup; proj.rsv[3] MOD_VIEW_OFF): what the LFOs and the envelopes do inside the selected track's newest
 * voice, drawn while it sounds (silent: what its free-running LFO and the shared LFO do): a tick on a knob an LFO
 * moves, the Filter page's curve at the cutoff as it sounds, a dot on the Envelope pages' curves. The engine writes it once a block (synth.h synth_mod); mv is the screen's
 * copy, taken every other frame (so the pictures redraw at 30 fps at most, and only for a change they show). Off
 * while the audio's load is high: over MV_GUARD_ON % until under MV_GUARD_OFF % (the guard's average) */
#define MV_GUARD_ON 80u
#define MV_GUARD_OFF 70u
static synth_mod_t mv = {.of = NTRK};
static uint8_t mv_guard;
static uint32_t mv_sus;                             /* (when the Envelope page's note reached its sustain, ms | 1; 0 not in it) */
static int32_t mv_fc, mv_dot = -1;                  /* (the host's checks: the Filter curve's cutoff, x; the
                                                     * Envelope page's dot, x, -1 none) */
static int mod_view(void) { return !(proj.rsv[3] & MOD_VIEW_OFF) && !mv_guard; }
/* the MOD lanes' values as MOD VIEW shows them (the knobs' words and bars, the pictures): taken each frame, or four
 * times a second while the audio's load is over MV_SLOW % (a glide moves them every block: each frame the knob band
 * and a page's curves drawn again, the main loop's time the audio's busy blocks leave it) */
#define MV_SLOW 60u
static uint8_t mv_snap[NMODL];
static uint32_t mv_snap_ms;
static void mv_take(uint32_t ld)
{
    const uint32_t now = plat_ms();
    if (ld > MV_SLOW && now - mv_snap_ms < 250u)
        return;
    mv_snap_ms = now;
    for (int i = 0; i < NMODL; i++)
        mv_snap[i] = i < seq.mod_n ? seq.lrt[seq.mod_lane[i] % NLANE].applied : LOCK_NONE;
}
static int mv_value(int tgt, int p)                 /* the value MOD VIEW shows on (tgt, p), -1 none (seq_mod_value's) */
{
    const uint16_t key = (uint16_t)(tgt << 9 | p);
    for (int i = 0; i < seq.mod_n && i < NMODL; i++)
        if ((seq.mod_own >> i & 1u) && seq.mod_tgt[i] == key)
            return mv_snap[i] == LOCK_NONE ? -1 : mv_snap[i];
    return -1;
}
static void mod_take(void)
{
    int trk = sel_trk(), want;
    uint32_t ld = guard_avg_q8 >> 8;                 /* (engine.c: the load the overload guard averages) */
    if (ld > MV_GUARD_ON)
        mv_guard = 1;
    else if (ld < MV_GUARD_OFF)
        mv_guard = 0;
    mv_take(ld);
    want = mod_view() && trk < NTRK;                 /* (a kit or a MIDI track: no voice of its own sounds) */
    synth_mod.trk = (uint8_t)(want ? trk : 0xFF);
    if (ui.frame & 1u)
        return;
    mv = *(const synth_mod_t *)&synth_mod;           /* (torn by a block at worst: one odd frame) */
    if (!want || mv.of != trk)
        mv.of = NTRK;
}

/* ----------------------------------------------------------- knobs --- */
typedef struct {
    const char *name;
    char val[24];
    int32_t num, max;
    int32_t live;                                    /* (max > 0) a lock or a motion's value now, as num; -1 none: a dot */
    int32_t mod;                                     /* (max > 0) MOD VIEW: 1 + an LFO's value now, as num; 0 none: a tick */
    uint8_t ctr;                                     /* the bar from its centre (a master filter's: LP to the left, HP
                                                      * to the right, off a mark at the centre) */
} knob_t;
static void kv(knob_t *k, const char *name, const char *val, int32_t num, int32_t max)
{
    uint32_t i;
    k->name = name;
    for (i = 0; i < sizeof k->val - 1u && val[i]; i++)
        k->val[i] = val[i];
    k->val[i] = 0;
    k->num = num;
    k->max = max;
}
static void kn(knob_t *k, const char *name, int32_t v, int32_t max)
{
    k->name = name;
    num_i(v, k->val);
    k->num = v;
    k->max = max;
}
/* the master filter's three knobs (fx.h MS_DFILT, MS_MELF, MS_MFILT): which (0 drums, 1 melodic, 2 mix; -1 another) */
static int mfilt_k(int m) { return m == MS_DFILT ? 0 : m == MS_MELF ? 1 : m == MS_MFILT ? 2 : -1; }
/* a master filter's knob: "LP 5.8 kHz", "off", "HP 120 Hz" */
static void mfilt_text(int v, char *t)
{
    float hz = master_filter_hz(v);
    char *e;
    if (hz == 0.0f) {
        s_cat(t, "off");
        return;
    }
    e = s_cat(t, hz < 0.0f ? "HP " : "LP ");
    hz = fm_fabsf(hz);
    if (hz < 1000.0f) {
        s_cat(num_i((int32_t)(hz + 0.5f), e), " Hz");
    } else {
        int32_t d = (int32_t)(hz * 0.01f + 0.5f);   /* (tenths of a kHz) */
        e = num_i(d / 10, e);
        *e++ = '.';
        s_cat(num_i(d % 10, e), " kHz");
    }
}
/* its knob: "drums LP" "5.8 kHz", "melodic" "off", "mix HP" "120 Hz" */
static void mfilt_knob(knob_t *k, int m, int v)
{
    static const char *const NM[3][3] = {{"drums", "drums LP", "drums HP"}, {"melodic", "mel LP", "mel HP"},
                                         {"mix", "mix LP", "mix HP"}};
    const float hz = master_filter_hz(v);
    const int f = mfilt_k(m);
    char t[16];
    mfilt_text(v, t);
    kv(k, NM[f][hz > 0.0f ? 1 : hz < 0.0f ? 2 : 0], hz != 0.0f ? t + 3 : t, v, 127);
    k->ctr = 1;
}
/* the master's pump: its depth (0 .. 24 dB), its source, its release */
static void pump_text(int v, char *t)
{
    int d = (v * 240 + 63) / 127;
    char *e = num_i(d / 10, t);
    *e++ = '.';
    e = num_i(d % 10, e);
    s_cat(e, " dB");
}
static const char *pump_by_text(int v, char *t)
{
    if (v == PB_KICK)
        return "kick";
    if (v == PB_METRO)
        return "metronome";
    num_i(v - PB_T1 + 1, s_cat(t, "T"));
    return t;
}
static void kname(knob_t *k, const char *name, const char *const *names, int v, int n)
{
    kv(k, name, names[clampi(v, 0, n - 1)], v, 0);
}
static const char *short_name(const char *n)        /* a knob name short enough for the strip */
{
    return s_eq(n, "frequency") ? "freq" : s_eq(n, "modulation") ? "mod" : s_eq(n, "feedback") ? "fdbk"
           : s_eq(n, "envelope") ? "env" : s_eq(n, "keytrack") ? "keytrk" : n;
}
/* a track parameter's knob: its name and value as synth.c prints them (the value v, or the track's); kpar_val: of a
 * track's values par */
static void kpar_val(knob_t *k, const uint8_t *par, int p, int v);
static void ktp_val(knob_t *k, int trk, int p, int v)
{
    if (is_fx(trk) && p == TP_CMIX)                  /* (an FX track's mix: its return, a level as a track's fader) */
        p = TP_LEVEL;
    kpar_val(k, proj.trk[trk].par, p, v);
}
static void kpar_val(knob_t *k, const uint8_t *par, int p, int v)
{
    const tp_info_t *in;
    char num[16], unit[6], val[24], *e;
    in = tp_info_par(p, par);
    if (!in->name || !in->name[0] || s_eq(in->name, "-"))
        return;
    tp_text_par(p, v, par, num, unit);
    e = s_cat(val, num);
    if (unit[0]) {
        if (unit[0] != '%')
            e = s_cat(e, " ");
        s_cat(e, unit);
    }
    kv(k, short_name(in->name), val, v - in->lo,
       in->kind == PK_NAMES || in->kind == PK_SWITCH || in->kind == PK_SAMPLE ? 0 : in->hi - in->lo);
}
static void ktp(knob_t *k, int trk, int p)
{
    if (p >= 0 && p < NTP) {
        int lv = engine_live(trk, p);
        ktp_val(k, trk, p, proj.trk[trk].par[p]);
        if (lv >= 0 && k->max > 0) {             /* the base shown, the locked value now a dot on its bar */
            const tp_info_t *in = tp_info_par(p, proj.trk[trk].par);
            lv -= in->lo;
            k->live = lv < 0 ? 0 : lv > k->max ? k->max : lv;
        }
        if (mv.of == trk && k->max > 0) {            /* MOD VIEW: an LFO moves it, a tick */
            int m = f_floor((p == mv.lp ? mv.loff : 0.0f) + (p == mv.gp ? mv.goff : 0.0f) + 0.5f);
            if (p == mv.lp || p == mv.gp)
                k->mod = 1 + clampi(m + (k->live >= 0 ? k->live : k->num), 0, k->max);
        }
    }
}
/* ---------------------------------------------------------- the MOD track --- */
/* its page (P_MOD) edits the scene's MOD pattern (a scene with none, "-": the one edited last, ui.mod_pat); ALGORITHM
 * picks one of its lanes (ui.mod_lane). A lane's target: a parameter of T1-T8, FX I or FX II, or the master's
 * (MODT_MST: fx.h MS_*) */
static int mod_pat_now(void)
{
    int m = pats.scene_mod[cur_scene()];
    return m ? (m - 1) % NPATT : ui.mod_pat % NPATT;
}
static int mod_none(void) { return !pats.scene_mod[cur_scene()]; }   /* the scene's MOD pattern "-": no lanes shown */
static const char *const LANE_SHAPE_NAME[NLS] = {"step", "linear", "ease in", "ease out", "smooth"};   /* (LS_*) */
static int mod_lanes_now(uint8_t *ix) { return mod_none() ? 0 : mod_lanes(pats.lane, mod_pat_now(), ix); }
/* the step KNOB 4 sets with none held: the one last tapped on the lane picked (-1: none, or the lane or the length
 * changed since) */
static int mod_tap_step(int l)
{
    const int st = ui.mod_tap - 1;
    return l >= 0 && ui.mod_tap && ui.mod_tap_l == l + 1 && st < pats.mod[mod_pat_now()].len ? st : -1;
}
static int mod_lane_now(void)                        /* the lane picked: its pool index, -1 none */
{
    uint8_t ix[NMODL];
    int n = mod_lanes_now(ix);
    return n ? ix[ui.mod_lane < n ? ui.mod_lane : n - 1] : -1;
}
static int mod_val(int tgt, int p) { return tgt == MODT_MST ? proj.master[p % NMS] : proj.trk[tgt % NALLTRK].par[p % NTP]; }
static int mod_lo(int tgt, int p) { return tgt == MODT_MST ? 0 : tp_info_par(p, proj.trk[tgt % NALLTRK].par)->lo; }
static int mod_hi(int tgt, int p) { return tgt == MODT_MST ? master_max(p) : tp_info_par(p, proj.trk[tgt % NALLTRK].par)->hi; }
/* a target a lane can take: a knob of a sound, an effect or the master (not what picks an engine, an effect, a
 * player, a type, a model, a channel, a route) */
static int mod_target_ok(int tgt, int p)
{
    const tp_info_t *in;
    if (tgt == MODT_MST)
        return p >= 0 && p < NMS && p != MS_CTYPE && p != MS_PUMP_BY && p != MS_PUMP_ACC;
    if ((tgt < 0 || tgt >= NTRK) && tgt != AUX_FX1 && tgt != AUX_FX2)
        return 0;
    if (p < 0 || p >= NTP || p == TP_ENGINE || p == TP_PLAYER || p == TP_CHAN || p == TP_BRAIN || p == TP_LTYPE ||
        p == TP_FTYPE || p == TP_SLOT[0][SL_EF] || p == TP_SLOT[1][SL_EF] ||
        (tp_is_dkey(p) && ((p - TP_DRUM0) % DP_N == DP_MODEL || (p - TP_DRUM0) % DP_N == DP_FTYPE)))
        return 0;
    in = tp_info_par(p, proj.trk[tgt].par);
    return in && in->name && in->name[0] && in->name[0] != '-' && in->kind != PK_NAMES && in->kind != PK_SAMPLE;
}
/* a target's knob: its name and the value v as its page shows them */
static void mod_knob_v(knob_t *k, int tgt, int p, int v)
{
    if (tgt != MODT_MST) {
        ktp_val(k, tgt, p, v);
        return;
    }
    for (int j = 0; j < SL_N; j++)
        if (MS_SLOT[j] == p) {                       /* Master FX's knobs: as its page says them */
            uint8_t mp[NTP];
            mst_par(mp);
            kpar_val(k, mp, TP_SLOT[0][j], v);
            if (k->name)
                return;
        }
    if (mfilt_k(p) >= 0)
        mfilt_knob(k, p, v);
    else if (p == MS_PUMP) {
        char t[12];
        pump_text(v, t);
        kv(k, MS_NAME[p], t, v, 127);
    } else
        kn(k, MS_NAME[p % NMS], v, master_max(p));
}
/* its name: "T2 cutoff", "T1 hat decay", "FX I time", "master mix" (the knob's name: drums LP / HP, mix LP / HP
 * name the filter's side now; the lane's is the knob's: "master melodic") */
static void mod_tgt_name(int tgt, int p, char *t)
{
    knob_t k;
    char *e;
    memset(&k, 0, sizeof k);
    mod_knob_v(&k, tgt, p, tgt == MODT_MST && mfilt_k(p) >= 0 ? MS_FILT_OFF : mod_val(tgt, p));
    e = s_cat(s_cat(t, tgt == MODT_MST ? "master" : TRACK_NAME[tgt % NALLTRK]), " ");
    if (tgt < NTRK && (tp_is_dkey(p) || tp_is_dknob2(p)))   /* a kit key's: which */
        e = s_cat(s_cat(e, DL_SOUND_NAME[(tp_is_dkey(p) ? (p - TP_DRUM0) / DP_N : (p - TP_DRUMX0) / DPX_N) % DL_NSOUND]), " ");
    s_cat(e, k.name ? k.name : "?");
}
/* the pages a lane's target is picked from (the SEL list's ADD LANE): a track's sound pages (EDIT's, ENV's, Insert 1
 * and 2, Sends, LFO's), an FX track's two, the master's four; each with a knob a lane can take */
static int mod_page_param(int tgt, int pg, int k)
{
    uint8_t keep = proj.trk_sel;
    int p;
    if (tgt == MODT_MST)
        return pg == P_MASTER2 ? MS_SLOT[k == 3 ? SL_MIX : SL_K1 + k] : mst_m(pg, k);
    proj.trk_sel = (uint8_t)tgt;                     /* (the pages' knobs are the track picked's) */
    p = tp_page_param(pg, k, tgt);
    proj.trk_sel = keep;
    return p;
}
/* knob k of the page shown: its target (*tgt a track, MODT_MST the master) and parameter, -1 none (a list, a lock or
 * step editor, a page of no sound) */
static int knob_target(int k, int *tgt)
{
    int pg = ui.page, trk = sel_trk();
    if (lst.open || ui.lockpg || ui.held || ui.trickpg || rec_settings() || k < 0 || k > 3)
        return -1;
    if (pg == P_MASTER || pg == P_MASTER2 || pg == P_MFILT || pg == P_MASTER3) {
        *tgt = MODT_MST;
        return mod_page_param(MODT_MST, pg, k);
    }
    if (pg == P_FX1 || pg == P_FX2)
        trk = pg == P_FX1 ? AUX_FX1 : AUX_FX2;
    *tgt = trk;
    return tp_page_param(pg, k, trk);
}
/* the MOD VIEW of a knob a MOD lane holds (mv_value): the value playing (its words, its bar), the knob's own a
 * tick on the bar */
static void kmod(knob_t *k, int tgt, int p)
{
    knob_t m;
    const int v = mv_value(tgt, p), own = k->num;
    if (v < 0 || k->max <= 0 || !k->name)
        return;
    memset(&m, 0, sizeof m);
    if (tgt == MODT_MST)
        mod_knob_v(&m, tgt, p, v);
    else
        ktp_val(&m, tgt, p, v);
    if (!m.name)
        return;
    memcpy(k->val, m.val, sizeof k->val);
    k->num = m.num;
    k->live = -1;
    k->mod = 1 + clampi(own, 0, k->max);
}
/* a track's values (or the master's: trk MODT_MST, NMS of them) with MOD VIEW's: the values the MOD lanes play on it
 * (the pictures': a filter's curve, a slot's picture at the value it sounds at) */
static const uint8_t *mod_shown(int trk, const uint8_t *par, uint8_t *buf, int n)
{
    int any = 0;
    if (!seq.mod_own || !mod_view())
        return par;
    for (int i = 0; i < seq.mod_n; i++) {
        const int tg = seq.mod_tgt[i] >> 9, p = seq.mod_tgt[i] & 511, v = i < NMODL ? mv_snap[i] : LOCK_NONE;
        if (!(seq.mod_own >> i & 1u) || tg != trk || p >= n || v == LOCK_NONE)
            continue;
        if (!any)
            memcpy(buf, par, (size_t)n);
        any = 1;
        buf[p] = (uint8_t)v;
    }
    return any ? buf : par;
}
static int mod_pages(int tgt, uint8_t *out)
{
    static const uint8_t BT[4] = {B_EDIT, B_ENV, B_FX, B_LFO}, MP[4] = {P_MASTER, P_MASTER2, P_MFILT, P_MASTER3};
    uint8_t keep = proj.trk_sel, all[24];
    int n = 0, m = 0;
    if (tgt == MODT_MST)
        for (int i = 0; i < 4; i++)
            all[n++] = MP[i];
    else if (tgt == AUX_FX1 || tgt == AUX_FX2) {
        all[n++] = P_ENGINE;
        all[n++] = P_PLAY;
    } else {
        proj.trk_sel = (uint8_t)tgt;
        for (uint32_t b = 0; b < 4; b++)
            for (uint32_t g = 0; g < NPAGE_BTN; g++)
                if (PAGE_BTN[g].btn == BT[b] && PAGE_BTN[g].first != P_MOD)
                    for (int i = 0; i < group_n((int)g) && n < 24; i++) {
                        int pg = grp_page((int)g, i);
                        if (pg != P_FX1 && pg != P_FX2 && pg != P_THROWS)
                            all[n++] = (uint8_t)pg;
                    }
        proj.trk_sel = keep;
    }
    for (int i = 0; i < n; i++) {
        int any = 0;
        for (int k = 0; k < 4; k++)
            any |= mod_target_ok(tgt, mod_page_param(tgt, all[i], k));
        if (any)
            out[m++] = all[i];
    }
    return m;
}
static int mod_tgt_at(int i)                         /* a target's place in TRACK's choices: T1-T8, FX I, FX II, the
                                                      * master */
{
    i %= NTRK + 3;
    return i < NTRK ? i : i == NTRK ? AUX_FX1 : i == NTRK + 1 ? AUX_FX2 : MODT_MST;
}
static int mod_tgt_place(int tgt) { return tgt == MODT_MST ? NTRK + 2 : tgt == AUX_FX1 ? NTRK : tgt == AUX_FX2 ? NTRK + 1 : tgt; }
static int mod_pick_tgt(void) { return mod_tgt_at(ui.mpick_t); }   /* ADD LANE's track */
/* target (tgt, p)'s page (its place among mod_pages, *pg the page) and knob (0..3), -1 none of them shows it */
static int mod_locate(int tgt, int p, int *pi, int *pg)
{
    uint8_t pl[24];
    const int n = mod_pages(tgt, pl);
    for (int i = 0; i < n; i++)
        for (int k = 0; k < 4; k++)
            if (mod_page_param(tgt, pl[i], k) == p) {
                *pi = i;
                *pg = pl[i];
                return k;
            }
    return -1;
}
/* ADD LANE's target as picked: its parameter, -1 none (*pg: its page) */
static int mod_pick_param(int *pg)
{
    uint8_t pl[24];
    int tgt = mod_pick_tgt(), n = mod_pages(tgt, pl), p;
    *pg = n ? pl[ui.mpick_p % n] : -1;
    if (!n)
        return -1;
    p = mod_page_param(tgt, *pg, ui.mpick_k & 3);
    return mod_target_ok(tgt, p) ? p : -1;
}

/* a length in steps, from quarter steps: "1/2", "1", "1 1/2", "8"; 0: the pattern's note length, "1/2 auto" */
static void quarters_text(int q, char *t)
{
    static const char *const Q[4] = {"", "1/4", "1/2", "3/4"};
    char *e = t;
    *t = 0;
    if (q >= 4) {
        e = num_s((uint32_t)q / 4u, t);
        if (q & 3)
            e = s_cat(e, " ");
    }
    if (q & 3 || !q)
        s_cat(e, q ? Q[q & 3] : "0");
}
static void step_len_text(int len, char *t, int nlen)
{
    if (len) {
        quarters_text(step_len_q(len), t);
        return;
    }
    if (nlen == 25 || nlen == 50 || nlen == 75 || nlen == 100)
        quarters_text(nlen / 25, t);
    else
        s_cat(num_s((uint32_t)nlen, t), "%");
    s_cat(t + s_len(t), " auto");
}
static void cond_text(int c, char *t)
{
    if (!c)
        s_cat(t, "always");
    else if (c < COND_AB)
        s_cat(num_s((uint32_t)c, t), "%");
    else {
        int a, b;
        cond_ab(c - COND_AB, &a, &b);
        num_s((uint32_t)b, s_cat(num_s((uint32_t)a, t), ":"));
    }
}
static const char *comp_val_text(int comp, int v, char *t)
{
    switch (comp) {
    case SC_VELOCITY: return VEL_VAL[clampi(v, 0, 9)];
    case SC_RAMP_UP: case SC_RAMP_DOWN: case SC_RANDOM: return RAMP_VAL[clampi(v, 0, 9)];
    case SC_PORTAMENTO: return PORTA_VAL[clampi(v, 0, 9)];
    case SC_BEND: return BEND_VAL[clampi(v, 0, 9)];
    case SC_TONALITY: return TON_VAL[clampi(v, 0, 9)];
    case SC_JUMP:                                    /* "to 13", "to 49": a beat (0..3, then 10..21), or a move */
        if (v >= 4 && v <= 9)
            return JUMP_MOVE[v - 4];
        num_s((uint32_t)(v < 4 ? v * 4 : (clampi(v, 10, JUMP_LAST) - 6) * 4) + 1u, s_cat(t, "to "));
        return t;
    case SC_SKIP_LOCK: case SC_SKIP_COMP: case SC_SKIP: {   /* "on 4", "not 4", "1st/4" of the cycle */
        int n = (int)SK_N((uint32_t)v), m = (int)SK_MODE((uint32_t)v);
        char *e;
        if (n >= SC_RANDOM_VAL)
            return "random";
        e = s_cat(t, m == 0 ? "on " : m == 1 ? "not " : "1st of ");
        num_s((uint32_t)n + 1u, e);
        return t;
    }
    default:
        if (v >= SC_RANDOM_VAL)
            return "random";
        num_s((uint32_t)v + 1u, t);
        return t;
    }
}
/* ---- a kit's hits: each sound on a step its own chance and tricks (pattern.h LANE_HIT0 lanes into pats.hit) ---- */
static void hit_of_step(const step_t *st, hit_t *h)
{
    h->cond = st->cond;
    h->comp = st->comp;
    h->cval = st->cval;
    h->comp2 = st->comp2;
    h->cval2 = st->cval2;
}
/* sound snd (0..)'s settings on step s of pattern pt, as it plays: its own, or the step's */
static void hit_get(int trk, int pt, int s, int snd, hit_t *h)
{
    const hit_t *o = step_hit(&pats, trk, pt, s, snd);
    if (o)
        *h = *o;
    else
        hit_of_step(&pats.pat[trk][pt].st[s], h);
}
/* a held step's chance and tricks are the sound shown's, on a kit step it is on; else the step's own (a synth's, an
 * empty kit step's: its jump) */
static int hit_edits(int trk, int s) { return is_drum(trk) && kit_has(&tpat(trk)->st[s], ui.dsound % NDRUM + 1); }
static void hit_view(int trk, int s, hit_t *h)   /* what the knobs show for step s */
{
    if (hit_edits(trk, s))
        hit_get(trk, cur_pat(trk), s, ui.dsound % NDRUM, h);
    else
        hit_of_step(&tpat(trk)->st[s], h);
}
/* what step s's cell marks: a chance on any of its sounds; the tricks of the sound shown, or of the first with any */
static void hit_marks(int trk, int pt, int s, const step_t *st, hit_t *h)
{
    uint32_t m = is_drum(trk) ? kit_mask(st) : 0u;
    int cond = 0, shown = ui.dsound % NDRUM;
    hit_t o;
    hit_of_step(st, h);
    if (!m)
        return;
    h->comp = h->comp2 = 0;
    if (m >> shown & 1u) {
        hit_get(trk, pt, s, shown, &o);
        h->comp = o.comp, h->cval = o.cval, h->comp2 = o.comp2, h->cval2 = o.cval2;
    }
    for (; m; m &= m - 1u) {
        hit_get(trk, pt, s, __builtin_ctz(m), &o);
        cond |= o.cond;
        if (!h->comp && !h->comp2)
            h->comp = o.comp, h->cval = o.cval, h->comp2 = o.comp2, h->cval2 = o.cval2;
    }
    h->cond = (uint8_t)(cond ? 1 : 0);
}
static int hit_same(const hit_t *a, const hit_t *b)
{
    return a->cond == b->cond && a->comp == b->comp && a->cval == b->cval && a->comp2 == b->comp2 && a->cval2 == b->cval2;
}
/* the entries no lane points at (the undo's pattern's lanes count: undoing brings them back) are free again */
static void hits_gc(void)
{
    uint64_t used = 0;
    for (int k = 0; k < NLANE; k++)
        if (pats.lane[k].used && lane_is_hit(pats.lane[k].param))
            for (int i = 0; i < NSTEP; i++)
                if (pats.lane[k].val[i] < NHIT)
                    used |= 1ull << pats.lane[k].val[i];
    if (undo.valid && undo.kind == U_PAT && undo.nl != 0xFFu)
        for (int k = 0; k < undo.nl && k < UNDO_LANES; k++)
            if (lane_is_hit(undo.u.p.lane[k].param))
                for (int i = 0; i < NSTEP; i++)
                    if (undo.u.p.lane[k].val[i] < NHIT)
                        used |= 1ull << undo.u.p.lane[k].val[i];
    for (int k = 0; k < NHIT; k++)
        if (!(used >> k & 1u))
            memset(&pats.hit[k], 0, sizeof pats.hit[k]);
}
/* the entry of these settings (an entry once pointed at never changes: the interrupt may be reading it), -1 none left */
static int hit_intern(const hit_t *h)
{
    int k, f = -1;
    for (k = 0; k < NHIT; k++) {
        if (hit_same(&pats.hit[k], h))
            return k;
        if (f < 0 && hit_plain(&pats.hit[k]))
            f = k;
    }
    if (f < 0) {
        hits_gc();
        for (k = 0; k < NHIT && f < 0; k++)
            if (hit_plain(&pats.hit[k]))
                f = k;
    }
    if (f >= 0)
        pats.hit[f] = *h;
    return f;
}
static void lane_drop_empty(int ln)              /* a lane with no step left on it goes (the lanes are few) */
{
    for (int i = 0; i < NSTEP; i++)
        if (pats.lane[ln].val[i] != LOCK_NONE)
            return;
    lane_clear(pats.lane, ln);
}
enum { HIT_OK, HIT_NO_LANE, HIT_NO_ROOM };
/* sound snd (0..) on step s of pattern pt gets these settings (plain: none of its own). The step's own settings,
 * while it still has any, go to each of its sounds first (as their own), so the others keep playing as they did */
static int hit_put(int trk, int pt, int s, int snd, const hit_t *h)
{
    step_t *st = &pats.pat[trk][pt].st[s];
    hit_t sh;
    int ln, k;
    hit_of_step(st, &sh);
    if (!hit_plain(&sh)) {
        uint32_t m = kit_mask(st);
        if ((k = hit_intern(&sh)) < 0)
            return HIT_NO_ROOM;
        for (; m; m &= m - 1u)
            if (!step_hit(&pats, trk, pt, s, __builtin_ctz(m))) {
                if ((ln = lane_alloc(pats.lane, trk, pt, (int)(LANE_HIT0 + (unsigned)__builtin_ctz(m)))) < 0)
                    return HIT_NO_LANE;
                pats.lane[ln].val[s] = (uint8_t)k;
            }
        st->comp = st->comp2 = SC_NONE;           /* (every sound has it now: the step's own goes) */
        st->cval = st->cval2 = 0;
        st->cond = 0;
    }
    ln = lane_find(pats.lane, trk, pt, (int)(LANE_HIT0 + (unsigned)snd));
    if (hit_plain(h)) {
        if (ln >= 0) {
            pats.lane[ln].val[s] = LOCK_NONE;
            lane_drop_empty(ln);
        }
        return HIT_OK;
    }
    if ((k = hit_intern(h)) < 0)
        return HIT_NO_ROOM;
    if (ln < 0 && (ln = lane_alloc(pats.lane, trk, pt, (int)(LANE_HIT0 + (unsigned)snd))) < 0)
        return HIT_NO_LANE;
    pats.lane[ln].val[s] = (uint8_t)k;
    return HIT_OK;
}
static void hit_sound_off(int trk, int pt, int s, int snd)   /* a sound taken off a step: its settings and pitch there go */
{
    for (int i = 0; i < 2; i++) {
        int ln = lane_find(pats.lane, trk, pt, (int)((i ? LANE_TUNE0 : LANE_HIT0) + (unsigned)snd));
        if (ln >= 0) {
            pats.lane[ln].val[s] = LOCK_NONE;
            lane_drop_empty(ln);
        }
    }
}

static void step_knobs(knob_t *k)
{
    const tpat_t *tp = tpat(sel_trk());
    const step_t *st = &tp->st[step_shown(ui.held_first)];
    char t[16];
    if (is_drum(sel_trk())) {                         /* a kit: the velocity of the sound shown (its own, or the step's) */
        int own = kit_has(st, ui.dsound % NDRUM + 1) ? step_vel(pats.lane, sel_trk(), cur_pat(sel_trk()),
                                                              step_shown(ui.held_first), ui.dsound % NDRUM) : 0;
        kn(&k[0], "VEL", own ? own : st->vel, 127);
    } else
        kn(&k[0], "VEL", st->vel, 127);
    t[0] = 0;
    step_len_text(st->len, t, tp->nlen);
    kv(&k[1], "LENGTH", t, st->len, step_len_code(tp->len * 4));
    t[0] = 0;
    {
        hit_t h;
        hit_view(sel_trk(), step_shown(ui.held_first), &h);   /* (a kit: the sound shown's) */
        cond_text(h.cond, t);
    }
    kv(&k[2], "CHANCE", t, 0, 0);
    if (is_drum(sel_trk())) {                         /* the sound shown's pitch (its own, or the step's): semitones */
        int sh = step_shown(ui.held_first), own = kit_has(st, ui.dsound % NDRUM + 1) ?
            step_tune_own(pats.lane, sel_trk(), cur_pat(sel_trk()), sh, ui.dsound % NDRUM) : LOCK_NONE;
        int semi = own != LOCK_NONE ? own - TUNE_0 : step_tune(pats.lane, sel_trk(), cur_pat(sel_trk()), sh);
        char *e = t;
        if (semi > 0)
            e = s_cat(e, "+");
        s_cat(num_i(semi, e), " st");
        kv(&k[3], "PITCH", t, semi + TUNE_MAX, 2 * TUNE_MAX);
    } else {                                          /* the step's (first) note */
        int n = 0;
        for (int i = 0; i < SN && !n; i++)
            n = st->note[i];
        if (n && pl_type(sel_trk()) == PL_RAND)
            s_cat(t, "on");                           /* (a random slot: its pitch is drawn) */
        else if (n)
            note_name(n, t);
        else
            s_cat(t, "-");
        kv(&k[3], "PITCH", t, n, 127);
    }
}
/* HOME + a step held: the strip shows the step's components and their values */
static void comp_knobs(knob_t *k)
{
    hit_t h, *st = &h;                                /* (a kit: the sound shown's) */
    char t[16];
    hit_view(sel_trk(), step_shown(ui.held_first), &h);
    kv(&k[0], "TRICK 1", st->comp ? COMP_NAME[st->comp % NSC] : "-", 0, 0);
    kv(&k[1], "VALUE", st->comp ? comp_val_text(st->comp, st->cval, t) : "-", 0, 0);
    kv(&k[2], "TRICK 2", st->comp2 ? COMP_NAME[st->comp2 % NSC] : "-", 0, 0);
    kv(&k[3], "VALUE", st->comp2 ? comp_val_text(st->comp2, st->cval2, t) : "-", 0, 0);
}
/* the lock editor: a step held + EDIT / ENV / FX / LFO: that page's four, with the lock's value ("-": none) */
static void lock_knobs(knob_t *k)
{
    int trk = sel_trk(), pg = ui.lockpg - 1, step = step_shown(ui.held_first);   /* (lock pages are the track's own) */
    for (int i = 0; i < 4; i++) {
        int p = tp_page_param(pg, i, trk), ln;
        if (p < 0)
            continue;
        ktp(&k[i], trk, p);
        if (!k[i].name)
            continue;
        ln = lane_find(pats.lane, trk, cur_pat(trk), p);
        if (ln < 0 || pats.lane[ln].val[step] == LOCK_NONE)
            kv(&k[i], k[i].name, "-", 0, 0);
        else
            ktp_val(&k[i], trk, p, pats.lane[ln].val[step]);
    }
}
/* an FX track's effect (its slot 1's), and how the screen names it: "chorus", or "reverb hall" */
static int fx_ef(int trk) { return proj.trk[trk].par[TP_CTYPE] % NEF; }
static int fx_mode(int trk) { return proj.trk[trk].par[TP_S1X1] % NRV; }
static const char *fx_label(int trk)
{
    static char t[2][24];
    static uint8_t at;
    char *o = t[at++ & 1u];
    if (fx_ef(trk) != EF_REVERB)
        return EFX[fx_ef(trk)].name;
    o[0] = 0;
    s_cat(s_cat(s_cat(o, EFX[EF_REVERB].name), " "), RV_NAME[fx_mode(trk)]);
    return o;
}
/* an effect's four knobs, 00..99; the delay's time as a note ("1/8D", or what the line plays: "1/4 as 1/8") and its
 * fine in % */
static char *dl_name(char *o, int d, int h)
{
    o = num_s((uint32_t)FX_DL_NOTE[d] << h, s_cat(o, "1/"));
    if (FX_DL_MARK[d])
        *o++ = FX_DL_MARK[d], *o = 0;
    return o;
}
/* a slot page's knobs: its effect's three as the catalog names them and the mix (a send's: its return, a level as a
 * track's fader); the delay's time as what the line plays ("1/4 as 1/8": too long for it), its repeats at the top
 * "hold" */
static void slot_knobs(knob_t *k, int pg)
{
    uint8_t mp[NTP];
    int trk, q;
    const uint8_t *par = slot_vals(pg, mp, &trk, &q);
    const uint16_t *sl = TP_SLOT[q];
    for (int i = 0; i < 4; i++) {
        int p = sl[i == 3 ? SL_MIX : SL_K1 + i];
        if (trk < 0)
            kpar_val(&k[i], par, p, par[p]);
        else
            ktp(&k[i], trk, p);
    }
    if (trk >= 0 && is_fx(trk))
        k[3].name = "return";
    if (par[sl[SL_EF]] == EF_DELAY) {
        char t[24];
        int v = par[sl[SL_K1]], d = fx_delay_div(v);
        int h = fx_delay_halves(v, par[sl[SL_X1]], seq_tempo(&seq), engine_delay_len(trk, q));
        char *e = dl_name(t, d, h);                  /* (what it plays: the line too short, its time halved, "max") */
        if (h)
            s_cat(e, " max");
        kv(&k[0], k[0].name, t, d, 0);              /* (a list's knob, as the arp's speed) */
        if (par[sl[SL_K2]] >= 127)                   /* repeats at the top: held (fx_delay_fb 1) */
            kv(&k[1], k[1].name, "hold", 127, 127);
    }
}
static void aux_engine_knobs(knob_t *k, int trk)
{
    const uint8_t *par = proj.trk[trk].par;
    if (trk == AUX_BRAIN) {
        kname(&k[0], BRAIN_KNOB[0], NOTE_NAME, par[TP_E2], 12);
        kname(&k[1], BRAIN_KNOB[1], BS_NAME, par[TP_E3], NBS);
        kname(&k[2], BRAIN_KNOB[2], LINK_NAME, par[TP_E4], 9);
    } else if (trk == AUX_PUNCH || trk == AUX_WEAVE) {
        kname(&k[0], "latch", ONOFF, par[TP_E1], 2);
    } else
        slot_knobs(k, fx_pg(trk));
}
/* the MOD page's knobs: KNOB 1 the scene's MOD pattern ("-" none), 2 its length, 3 its scale, 4 the lane's target (its
 * own knob: the value a MOD lane holds now a dot); a step held: KNOB 4 the lane's value there ("-" none) */
static void mod_knobs(knob_t *k)
{
    const int pt = mod_pat_now(), sm = pats.scene_mod[cur_scene()], l = mod_lane_now();
    const modpat_t *mp = &pats.mod[pt];
    if (ui.held) {
        if (l >= 0) {
            const lane_t *L = &pats.lane[l];
            const int tg = lane_mod_tgt(L), v = L->val[step_shown(ui.held_first) % NSTEP];
            mod_knob_v(&k[3], tg, L->param, v == LOCK_NONE ? mod_val(tg, L->param) : v);
            if (v == LOCK_NONE)
                kv(&k[3], k[3].name, "-", 0, 0);
        }
        return;
    }
    {
        static char pn[6];
        pn[0] = 0;
        num_s((uint32_t)pt + 1u, s_cat(pn, "P"));
        kv(&k[0], "PATTERN", sm ? pn : "-", sm, NPATT);
    }
    if (!sm)                                         /* (none: KNOB 1 alone) */
        return;
    kn(&k[1], "LENGTH", mp->len, NSTEP);
    kname(&k[2], "SCALE", TS_NAME, mp->scale, NTS);
    if (l >= 0 && !(seq.rec && playing())) {         /* KNOB 4 sets the lane's value on the step last tapped ("-" none
                                                      * there; "STEP -": none tapped) */
        static char sn[10];
        const lane_t *L = &pats.lane[l];
        const int tg = lane_mod_tgt(L), st = mod_tap_step(l), v = st < 0 ? LOCK_NONE : L->val[st];
        mod_knob_v(&k[3], tg, L->param, v == LOCK_NONE ? mod_val(tg, L->param) : v);
        sn[0] = 0;
        if (st < 0)
            s_cat(sn, "STEP -");
        else
            num_s((uint32_t)st + 1u, s_cat(sn, "STEP "));
        k[3].name = sn;
        if (v == LOCK_NONE)
            kv(&k[3], sn, "-", 0, 0);
    } else if (l >= 0) {                             /* REC while playing: the target's own, written as it turns */
        const lane_t *L = &pats.lane[l];
        const int tg = lane_mod_tgt(L);
        mod_knob_v(&k[3], tg, L->param, mod_val(tg, L->param));
        if (mod_view())                              /* MOD VIEW: the value playing, the knob's own a tick */
            kmod(&k[3], tg, L->param);
        else if (tg != MODT_MST && k[3].max > 0) {   /* the MOD lane's value now: a dot on its bar */
            int lv = engine_live(tg, L->param);
            if (lv >= 0)
                k[3].live = clampi(lv - mod_lo(tg, L->param), 0, k[3].max);
        }
    }
}
static void page_knobs(knob_t *k)
{
    int trk = sel_trk(), p = ui.page;
    tpat_t *tp = tpat(trk);
    char t[16], *e;
    memset(k, 0, 4 * sizeof k[0]);
    for (int i = 0; i < 4; i++)
        k[i].live = -1;
    if (rec_settings()) {                            /* REC held: the record settings */
        static const char *const CIN[NCIN] = {"off", "1 bar", "2 bars", "on note"};
        static const char *const MODE[2] = {"overdub", "replace"};
        kname(&k[0], "CLICK", METRO_NAME, proj.metronome, 3);
        kname(&k[1], "COUNT", CIN, proj.rsv[0] < NCIN ? proj.rsv[0] : 0, NCIN);
        if (is_seqt(trk))
            kn(&k[2], "QUANT", tp->quant, 100);
        kname(&k[3], "MODE", MODE, proj.rsv[2] & 1u, 2);
        return;
    }
    if (ui.lockpg) {
        lock_knobs(k);
        return;
    }
    if (p == P_MOD) {
        mod_knobs(k);
        return;
    }
    if (steps_page(p) && ui.trickpg && is_seqt(trk)) {
        comp_knobs(k);
        return;
    }
    if (steps_page(p) && ui.held && is_seqt(trk)) {
        step_knobs(k);
        return;
    }
    if (trk == AUX_BRAIN && p == P_SUB) {            /* the Follow page: the track whose notes set the chords */
        kname(&k[0], "FOLLOW", LINK_NAME, proj.trk[AUX_BRAIN].par[TP_E1] % 9u, 9);
        return;
    }
    switch (p) {
    case P_ARRANGE:
        tempo_text(t);
        kv(&k[0], "TEMPO", t, (int)proj.tempo_x10 / 10 - 40, 180);
        kname(&k[1], "GROOVE", GR_NAME, proj.groove, NGROOVE);
        if (mod_sel()) {                             /* the MOD track: its pattern in this scene ("-" none) */
            static char mpn[6];
            const int m = pats.scene_mod[cur_scene()];
            mpn[0] = 0;
            num_s((uint32_t)m, s_cat(mpn, "P"));
            kv(&k[3], "MOD PAT", m ? mpn : "-", m, NPATT);
        } else if (is_seqt(trk)) {
            static char pn[12];                      /* the track's pattern in this scene ("T2 PAT"; cued: "1>2";
                                                      * THROW's name too long for a knob: "PATTERN") */
            char c[8], *ce;
            kn(&k[2], "AMOUNT", tp->groove, 0);
            pn[0] = 0;
            if (s_len(TRACK_NAME[trk]) > 4)
                s_cat(pn, "PATTERN");
            else
                s_cat(s_cat(pn, TRACK_NAME[trk]), " PAT");
            if (playing() && seq.pcue[trk] < NPATT && seq.pcue[trk] != cur_pat(trk)) {
                ce = num_s((uint32_t)cur_pat(trk) + 1u, c);
                *ce++ = '>';
                num_s((uint32_t)seq.pcue[trk] + 1u, ce);
                kv(&k[3], pn, c, seq.pcue[trk] + 1, NPATT);
            } else
                kn(&k[3], pn, cur_pat(trk) + 1, NPATT);
        }
        break;
    case P_SONG: {
        const song_t *sg = &pats.song;
        int cur = ui.name_slot < sg->len ? ui.name_slot : sg->len;
        kn(&k[0], "SLOT", cur + 1, NSONG);
        if (cur < sg->len) {
            kn(&k[1], "SCENE", sg->slot[cur].scene + 1, NSCENE);
            kn(&k[2], "BARS", sg->slot[cur].rep, SONG_BARS);
        } else {
            kv(&k[1], "SCENE", "-", 0, 0);
            kv(&k[2], "BARS", "-", 0, 0);
        }
        kv(&k[3], "AT END", sg->loop ? "loop" : "stop", 0, 0);
        break;
    }
    case P_STEPS:
        if (trk == AUX_WEAVE) {                      /* its pattern (the scene's) and length; a step held: the step's */
            const wvpat_t *wp = &pats.weave[pats.scene[cur_scene()].weave % NPATT];
            kn(&k[0], "PATTERN", pats.scene[cur_scene()].weave % NPATT + 1, NPATT);
            if (ui.wv_step < NSTEP)
                kn(&k[1], "STEP LEN", wp->st[ui.wv_step].len ? wp->st[ui.wv_step].len : 1, NSTEP);
            else
                kn(&k[1], "LENGTH", wp->len, NSTEP);
            break;
        }
        if (!is_seqt(trk))
            break;
        if (playing() && seq.pcue[trk] < NPATT && seq.pcue[trk] != cur_pat(trk)) {   /* cued: "1>2" until the bar */
            char c[8], *ce = num_s((uint32_t)cur_pat(trk) + 1u, c);
            *ce++ = '>';
            num_s((uint32_t)seq.pcue[trk] + 1u, ce);
            kv(&k[0], "NEXT", c, seq.pcue[trk] + 1, NPATT);
        } else
            kn(&k[0], "PATTERN", cur_pat(trk) + 1, NPATT);
        kn(&k[1], "LENGTH", tp->len, NSTEP);
        kname(&k[2], "SCALE", TS_NAME, tp->scale, NTS);
        kn(&k[3], "GROOVE", tp->groove, 0);
        break;
    case P_BAR:
        if (!is_seqt(trk))
            break;
        kn(&k[0], "QUANT", tp->quant, 100);
        e = num_s(tp->nlen, t);
        s_cat(e, "%");
        kv(&k[1], "NOTE", t, tp->nlen, 100);
        kn(&k[2], "GROOVE", tp->groove, 0);
        kn(&k[3], "SHAPE", tp->shape, 100);
        break;
    case P_ENGINE:
        if (is_aux(trk))
            aux_engine_knobs(k, trk);
        else
            for (int i = 0; i < 4; i++)
                ktp(&k[i], trk, tp_page_param(p, i, trk));
        break;
    case P_FX1: case P_FX2: case P_CHARACTER: case P_INSERT: case P_MASTER2:   /* a slot (its effect: PRESETS, SEL) */
        slot_knobs(k, p);
        break;
    case P_SENDS: {                                  /* the instrument's sends (a kit's: the key's), pan and level */
        int it = inst_trk();
        ktp(&k[0], it, is_drum(it) ? TP_DRUM(ui.dsound % DL_NSOUND, DP_SEND1) : TP_SEND1);
        ktp(&k[1], it, is_drum(it) ? TP_DRUM(ui.dsound % DL_NSOUND, DP_SEND2) : TP_SEND2);
        ktp(&k[2], it, TP_PAN);
        ktp(&k[3], it, TP_LEVEL);
        k[0].name = "fx I";
        k[1].name = "fx II";
        break;
    }
    case P_PLAY:
        if (is_fx(trk)) {
            const uint8_t *par = proj.trk[trk].par;
            kn(&k[0], "HP", par[TP_FENV], 127);
            kn(&k[1], "LP", par[TP_CUT], 127);
            if (trk == AUX_FX1)
                kn(&k[2], "TO FX II", par[TP_SEND2], 127);
        } else if (!is_aux(trk))
            for (int i = 0; i < 4; i++)
                ktp(&k[i], trk, tp_page_param(p, i, trk));
        break;
    case P_WEAVE: {                                  /* the track's weave: the track and pattern woven in, the rule */
        int f = wv_from(trk);
        static char wt[8];
        kv(&k[0], "FROM", f ? TRACK_NAME[f - 1] : "off", f, NSEQT);
        num_s((uint32_t)wv_pat(trk) + 1u, s_cat(wt, "P"));
        kv(&k[1], "PATTERN", f ? wt : "-", wv_pat(trk), NPATT - 1);
        kname(&k[2], "RULE", WEAVE_NAME, wv_rule(trk), NWV);
        if (f) {
            static const char *const PLAYS[2] = {"always", "on WEAVE"};
            kname(&k[3], "PLAYS", PLAYS, wv_cued(trk), 2);
        }
        break;
    }
    case P_THROWS: {
        static const char *const LATCH[2] = {"off", "on"};
        kname(&k[0], "LATCH", LATCH, proj.trk[AUX_PUNCH].par[TP_E1] & 1u, 2);
        break;
    }
    case P_MIX:
        if (is_fx(trk))
            ktp(&k[3], trk, TP_CMIX);
        else if (!is_aux(trk)) {                     /* (the conductor and the throws: no sound, no knobs; a kit's
                                                      * sends are its keys': FX, Sends) */
            if (!is_drum(trk)) {
                ktp(&k[0], trk, TP_SEND1);
                ktp(&k[1], trk, TP_SEND2);
            }
            ktp(&k[2], trk, TP_PAN);
            ktp(&k[3], trk, TP_LEVEL);
            k[0].name = "FX I";
            k[1].name = "FX II";
        }
        break;
    case P_MASTER: case P_MFILT: case P_MASTER3:
        for (int i = 0; i < 4; i++) {
            int m = mst_m(p, i);
            if (mfilt_k(m) >= 0)                     /* drums LP 5.8 kHz, melodic off, mix HP 120 Hz */
                mfilt_knob(&k[i], m, proj.master[m]);
            else if (m == MS_PUMP) {
                char t[12];
                pump_text(proj.master[m], t);
                kv(&k[i], MS_NAME[m], t, proj.master[m], 127);
            } else
                kn(&k[i], MS_NAME[m], proj.master[m], 127);
        }
        break;
    case P_GLFO: {                                   /* the shared LFO (synth.h GS_*, GD_*): the conductor's slots */
        const uint8_t *g = proj.trk[AUX_BRAIN].par;
        static char rt[12];
        glfo_rate_text(g[TP_LSPEED], rt);
        kname(&k[0], "SHAPE", GLFO_SHAPE, g[TP_LTYPE] % (NGS + 1), NGS + 1);
        kv(&k[1], "RATE", rt, g[TP_LSPEED], 127);
        kn(&k[2], "AMOUNT", (int)g[TP_LAMT] - 64, 0);
        kname(&k[3], "DEST", GLFO_DEST, g[TP_LDEST] % NGD, NGD);
        break;
    }
    case P_SETUP:
        kname(&k[0], "LIGHTS", LIGHTS_NAME, proj.lights, 3);
        kname(&k[1], "CLOCK", CLOCK_NAME, CLOCK_POS[proj.clock % SEQ_NCLK], SEQ_NCLK);
        kname(&k[2], "NOTES", ONOFF, proj.send_notes != 0, 2);
        kv(&k[3], "THEME", THEME_OF_PROJ()->name, 0, 0);
        break;
    case P_STATUS:
        kv(&k[0], "CORES", proj.one_core ? "one" : "two", 0, 0);
        break;
    default:                                         /* the sound pages: every knob through tp_info_par */
        if (!is_aux(trk))
            for (int i = 0; i < 4; i++)
                ktp(&k[i], trk, tp_page_param(p, i, trk));
        break;
    }
    if (seq.mod_own && mod_view())                   /* MOD VIEW: a knob a MOD lane holds shows the value playing */
        for (int i = 0, tg; i < 4; i++) {
            int q = knob_target(i, &tg);
            if (q >= 0)
                kmod(&k[i], tg, q);
        }
}
static int knobs_shown(void)
{
    knob_t k[4];
    page_knobs(k);
    return k[0].name || k[1].name || k[2].name || k[3].name;
}
/* a knob in large type (72 px): its name in lower case at 18 px (two words too wide: a line each at 15; one word too
 * wide: 15), its value at 31 (a number with a unit: the unit at 15 after it; a word too wide: 23, then two lines, then
 * 18), the bar at the foot */
#define KL_LW 55                                     /* large type: a knob's label (with 2 px to spare), its value */
#define KL_VW 50                                     /* (no separators: the knobs 58 px apart, the gaps between them) */
static void knob_tick(const knob_t *k, int32_t x, int32_t w, int32_t y)   /* MOD VIEW's tick across a knob's bar (x, w
                                                                         * wide): 2 px wide, 9 tall, where an LFO has it */
{
    if (!k->mod)
        return;
    x += w * (k->mod - 1) / k->max;
    cv_rect(x - 1, y, 4, 11, K_PANEL);
    cv_rect(x, y + 1, 2, 9, K_TEXT);
}
/* a knob's bar (x, w wide, at y): its value's share from the left, or (ctr) from the centre to it */
static void knob_bar(const knob_t *k, int32_t x, int32_t w, int32_t y, int hot)
{
    const uint16_t c = hot ? K_SEL : K_NOW;
    int32_t bw = w * k->num / k->max;
    cv_round(x, y, w, 6, 3, K_LINE);
    if (k->ctr) {                                    /* (the centre: half the knob's travel) */
        const int32_t m = x + w / 2, at = x + bw;
        if (at < m - 3)
            cv_round(at, y, m - at, 6, 3, c);
        else if (at > m + 3)
            cv_round(m, y, at - m, 6, 3, c);
        else
            cv_round(m - 3, y, 6, 6, 3, c);
    } else if (bw > 0)
        cv_round(x, y, bw < 6 ? 6 : bw, 6, 3, c);
}
static void knob_large(int32_t cx, const knob_t *k, int hot, uint16_t vc)
{
    char nm[24], a[24];
    const char *v = k->val;
    uint32_t i, sp;
    int32_t w;
    for (i = 0; k->name[i] && i < sizeof nm - 1u; i++)
        nm[i] = (char)(k->name[i] >= 'A' && k->name[i] <= 'Z' ? k->name[i] + 32 : k->name[i]);
    nm[i] = 0;
    if (i > 2 && nm[i - 1] == '%' && nm[i - 2] == ' ' && text_w(&FONT_XS, nm) > KL_LW - 2)
        nm[i - 2] = 0;                               /* ("rhythm %": the value says the %) */
    if (text_w(&FONT_XS, nm) > KL_LW - 2 && s_len(nm) > 4) {   /* a few pixels too wide: the letters 1 px closer */
        loom_track = 1;
        if (text_w(&FONT_XS, nm) > KL_LW - 2)
            loom_track = 0;
    }
    if (text_w(&FONT_XS, nm) <= KL_LW - 2) {
        text_c(cx, cap_y(1, 22, &FONT_XS), &FONT_XS, nm, hot ? K_TEXT : K_DIM);
        loom_track = 0;
    }
    else if (nm[sp = (uint32_t)s_find_sp(nm)]) {
        nm[sp] = 0;
        text_c(cx, 0, &LFONT_S, nm, hot ? K_TEXT : K_DIM);
        text_c(cx, 12, &LFONT_S, nm + sp + 1, hot ? K_TEXT : K_DIM);
    } else
        text_fit(cx - (text_w(&LFONT_S, nm) < KL_LW ? text_w(&LFONT_S, nm) : KL_LW) / 2, cap_y(1, 22, &LFONT_S), &LFONT_S,
                 nm, KL_LW, hot ? K_TEXT : K_DIM);
    sp = (uint32_t)s_find_sp(v);
    for (i = 0; i < sp && i < sizeof a - 1u; i++)
        a[i] = v[i];
    a[i] = 0;
    if (text_w(&FONT_M, v) <= KL_VW)
        text_c(cx, cap_y(26, 34, &FONT_M), &FONT_M, v, vc);
    else if (v[sp] && v[sp + 1] >= 'A' && s_len(v + sp + 1) <= 3 && (a[0] <= '9' || a[0] == '-' || a[0] == '+') &&
             text_w(&LFONT_M, a) + 2 + text_w(&LFONT_XS, v + sp + 1) - (int32_t)s_len(a) + 1 <= KL_VW) {
        /* a number and its unit (331 ms, 2.9 kHz: three letters at most): the number at 31 with the unit at 15, or
         * at 23 with the unit at 12 */
        int big = text_w(&FONT_M, a) + 3 + text_w(&LFONT_S, v + sp + 1) <= KL_VW;
        const felucca_font_t *f = big ? &FONT_M : &LFONT_M, *u = big ? &LFONT_S : &LFONT_XS;
        int32_t x, y = cap_y(26, 34, f), gap = big ? 3 : 2;
        if (!big && text_w(f, a) + gap + text_w(u, v + sp + 1) > KL_VW)
            loom_track = 1;                          /* (a pixel or two over: the digits 1 px closer) */
        w = text_w(f, a) + gap + text_w(u, v + sp + 1);
        x = cv_text(cx - w / 2, y, f, a, vc) + gap;
        loom_track = 0;
        cv_text(x, y + f->h - u->h - (big ? 3 : 2), u, v + sp + 1, K_DIM);
    } else if (text_w(&LFONT_M, v) <= KL_VW)
        text_c(cx, cap_y(26, 34, &LFONT_M), &LFONT_M, v, vc);
    else if (v[sp]) {                                /* two words: two lines */
        text_fit(cx - (text_w(&FONT_XS, a) < KL_VW ? text_w(&FONT_XS, a) : KL_VW) / 2, 25, &FONT_XS, a, KL_VW, vc);
        text_fit(cx - (text_w(&FONT_XS, v + sp + 1) < KL_VW ? text_w(&FONT_XS, v + sp + 1) : KL_VW) / 2, 42, &FONT_XS,
                 v + sp + 1, KL_VW, vc);
    } else if (text_w(&FONT_XS, v) <= KL_VW)
        text_c(cx, cap_y(26, 34, &FONT_XS), &FONT_XS, v, vc);
    else {                                           /* (15 px; a pixel or two over: the letters 1 px closer) */
        loom_track = text_w(&LFONT_S, v) > KL_VW;
        text_fit(cx - (text_w(&LFONT_S, v) < KL_VW ? text_w(&LFONT_S, v) : KL_VW) / 2, cap_y(26, 34, &LFONT_S), &LFONT_S,
                 v, KL_VW, vc);
        loom_track = 0;
    }
    if (k->max > 0) {
        knob_bar(k, cx - 24, 48, 62, hot);
        if (k->live >= 0) {
            int32_t lx = cx - 24 + 42 * k->live / k->max;
            cv_round(lx - 1, 60, 8, 10, 4, K_BG);
            cv_round(lx, 61, 6, 8, 3, K_REC);
        }
        knob_tick(k, cx - 24, 46, 59);
    }
}
static void knobs_paint(const knob_t *k, int now_touch)   /* the knob band (draw_knobs), one strip of it */
{
    uint32_t i;
    if (!(k[0].name || k[1].name || k[2].name || k[3].name))
        return;
    cv_round(4, 0, 232, KNB_H - 2, 6, K_PANEL);
    for (i = 0; i < 4; i++) {
        int32_t x = (int32_t)i * 60, cx = LARGE ? 33 + (int32_t)i * 58 : x + 30, nw;   /* (large: 58 apart in the panel) */
        int hot = now_touch && ui.touched == (int)i, lock = ui.lockpg && !s_eq(k[i].val, "-");
        uint16_t vc = hot ? K_SEL : lock ? K_REC : K_TEXT;
        if (i && !LARGE)                             /* (large type: the gaps alone) */
            cv_rect(x, 10, 1, KNB_H - 22, K_LINE);
        if (!k[i].name)
            continue;
        if (LARGE) {
            knob_large(cx, &k[i], hot, vc);
            continue;
        }
        nw = text_w(&FONT_XS, k[i].name);
        if (nw > 46)
            nw = 46;
        text_fit(cx - nw / 2, 0, &FONT_XS, k[i].name, 46, hot ? K_TEXT : K_DIM);
        if (upper_only(k[i].val) && text_w(&FONT_M, k[i].val) <= 48)   /* (48 px: clear of the panel's sides) */
            text_c(cx, cap_y(12, 28, &FONT_M), &FONT_M, k[i].val, vc);
        else if (text_w(&FONT_B, k[i].val) <= 48)
            text_c(cx, cap_y(12, 28, &FONT_B), &FONT_B, k[i].val, vc);
        else if (text_w(&FONT_XS, k[i].val) <= 48)
            text_c(cx, cap_y(12, 28, &FONT_XS), &FONT_XS, k[i].val, vc);
        else if (k[i].val[s_find_sp(k[i].val)]) {   /* two words: two lines ("ramp" / "down") */
            char a[24];
            const char *sp = k[i].val + s_find_sp(k[i].val);
            uint32_t n = (uint32_t)(sp - k[i].val) < sizeof a - 1u ? (uint32_t)(sp - k[i].val) : sizeof a - 1u;
            for (uint32_t j = 0; j < n; j++)
                a[j] = k[i].val[j];
            a[n] = 0;
            text_fit(cx - (text_w(&FONT_XS, a) < 48 ? text_w(&FONT_XS, a) : 48) / 2, 11, &FONT_XS, a, 48, vc);
            text_fit(cx - (text_w(&FONT_XS, sp + 1) < 48 ? text_w(&FONT_XS, sp + 1) : 48) / 2, 24, &FONT_XS, sp + 1, 48, vc);
        } else
            text_fit(cx - 24, cap_y(12, 28, &FONT_XS), &FONT_XS, k[i].val, 48, vc);
        if (k[i].max > 0) {
            knob_bar(&k[i], cx - 22, 44, 41, hot);
            if (k[i].live >= 0) {                    /* a lock or a motion holds it now: where, a dot on the bar */
                int32_t lx = cx - 22 + 38 * k[i].live / k[i].max;
                cv_round(lx - 1, 39, 8, 10, 4, K_BG);
                cv_round(lx, 40, 6, 8, 3, K_REC);
            }
            knob_tick(&k[i], cx - 22, 42, 38);       /* an LFO moves it: where it is now, a tick across the bar */
        }
    }
}
static void draw_knobs(void)
{
    knob_t k[4];
    uint32_t h = hash_u(2166136261u, ui.page | (uint32_t)ui.touched << 8), i;
    int now_touch = plat_ms() < ui.touch_until;
    page_knobs(k);
    for (i = 0; i < 4; i++) {
        h = hash_s(h, k[i].val);
        h = hash_s(h, k[i].name);
        h = hash_u(h, (uint32_t)k[i].num << 8 | (uint32_t)(k[i].name != 0) | (uint32_t)(k[i].live + 1) << 20);
        if (k[i].mod)
            h = hash_u(h, (uint32_t)k[i].mod);
    }
    h = hash_u(h, (uint32_t)now_touch | (uint32_t)ui.lockpg << 1);
    if (h == ui.sig[2])
        return;
    ui.sig[2] = h;
    for (int32_t r0 = 0; r0 < KNB_H; r0 += CV_ROWS) {   /* (large type's band is taller than the canvas: in strips) */
        strip_begin(KNB_H, r0, K_BG);
        knobs_paint(k, now_touch);
        cv_blit(0, (uint32_t)(KNB_Y + r0));
    }
}

/* ------------------------------------------------- tag, footer, cards --- */
static void track_crumb(char *t)
{
    int trk = sel_trk();
    char *e = s_cat(t, TRACK_NAME[trk]);
    if (!is_aux(trk)) {
        e = s_cat(e, " ");
        e = s_cat(e, EG_NAME[proj.trk[trk].par[TP_ENGINE] % NENGINE]);
    }
    if (is_seqt(trk)) {
        e = num_s((uint32_t)cur_pat(trk) + 1u, s_cat(e, ">P"));
        if (playing() && seq.pcue[trk] < NPATT && seq.pcue[trk] != cur_pat(trk))   /* cued: it comes at the bar */
            e = num_s((uint32_t)seq.pcue[trk] + 1u, s_cat(e, ">next P"));
    }
    num_s((uint32_t)cur_scene() + 1u, s_cat(e, ">S"));
}
static void page_tag(char *t)
{
    char *e;
    *t = 0;
    if (ui.page == P_MOD) {                          /* MOD>P2>S3>T2 cutoff (a step held: >step 5) */
        int l = mod_lane_now();
        e = mod_none() ? s_cat(t, "MOD>none") : num_s((uint32_t)mod_pat_now() + 1u, s_cat(t, "MOD>P"));
        e = num_s((uint32_t)cur_scene() + 1u, s_cat(e, ">S"));
        if (ui.held)
            e = num_s((uint32_t)step_shown(ui.held_first) + 1u, s_cat(e, ">step "));
        if (l >= 0)
            mod_tgt_name(lane_mod_tgt(&pats.lane[l]), pats.lane[l].param, s_cat(e, ">"));
        return;
    }
    switch (ui.page) {
    case P_ARRANGE:
        e = num_s((uint32_t)cur_scene() + 1u, s_cat(t, "S"));
        if (seq.cue != 0xFFu && seq.cue != seq.scene)
            num_s((uint32_t)seq.cue + 1u, s_cat(e, ">next S"));
        break;
    case P_SONG:
        num_s((uint32_t)pats.song.len, s_cat(t, "song>slots "));
        break;
    case P_STEPS: case P_BAR:
        track_crumb(t);
        if (ui.lockpg) {
            int lp = ui.lockpg - 1;
            e = num_s((uint32_t)step_shown(ui.held_first) + 1u, s_cat(t + s_len(t), ">step "));
            if (is_drum(sel_trk()) && (lp == P_ENGINE || lp == P_PLAY || lp == P_ENV || lp == P_FILTER))
                e = s_cat(s_cat(e, ">"), DL_SOUND_NAME[ui.dsound % DL_NSOUND]);   /* the key the locks reach */
            s_cat(s_cat(e, ">"), page_name_of(lp, sel_trk()));
        } else if (ui.held & (ui.held - 1u))     /* several held: how many (the knobs show the first's values) */
            s_cat(num_s((uint32_t)__builtin_popcount(ui.held), s_cat(t + s_len(t), ">")), " steps");
        else if (ui.held) {
            e = num_s((uint32_t)step_shown(ui.held_first) + 1u, s_cat(t + s_len(t), ">step "));
            if (hit_edits(sel_trk(), step_shown(ui.held_first)))   /* a kit: the sound its knobs reach */
                s_cat(s_cat(e, ">"), DL_SOUND_NAME[ui.dsound % DL_NSOUND]);
        }
        break;
    case P_ENGINE: case P_PLAY: case P_PLAY2: case P_SUB: case P_OSC2: case P_KMOD: case P_ENV: case P_FILTER: case P_ENV2:
    case P_KITF: case P_LFO: case P_LFO2:
        track_crumb(t);
        if (is_drum(sel_trk()))                      /* the key the knobs edit (the kit's own pages: the whole kit) */
            s_cat(s_cat(t + s_len(t), ">"), ui.page == P_ENV2 || ui.page == P_KITF ? "all keys"
                                             : DL_SOUND_NAME[ui.dsound % DL_NSOUND]);
        break;
    case P_SENDS:
        s_cat(s_cat(t, TRACK_NAME[inst_trk()]), ">sends");
        break;
    case P_CHARACTER: case P_INSERT: case P_FX1: case P_FX2: case P_MASTER2: {   /* T2>drive, FX II>reverb>hall */
        uint8_t mp[NTP];
        int st, q;
        const uint8_t *par = slot_vals(ui.page, mp, &st, &q);
        const int ef = par[TP_SLOT[q][SL_EF]] % NEF;
        e = s_cat(s_cat(s_cat(t, st < 0 ? "master" : TRACK_NAME[st]), ">"), EFX[ef].name);
        if (ef == EF_REVERB)
            s_cat(s_cat(e, ">"), RV_NAME[par[TP_SLOT[q][SL_X1]] % NRV]);
        break;
    }
    case P_MASTER: case P_MFILT: case P_MASTER3: case P_GLFO: case P_SETUP: case P_STATUS:
        s_cat(t, "all tracks");
        break;
    default:
        track_crumb(t);
        break;
    }
}
static void steps_head(int trk, char *nm, char *t);
static void draw_tag(void)
{
    char t[80], *v = t;
    int g = page_group(ui.page), n = group_n(g), d = grp_index(ui.page);
    int32_t r = 232, x, w;
    uint32_t h;
    int big = band != &BANDS[0], st = big && steps_page(ui.page) && is_seqt(sel_trk()) && sel_trk() != AUX_WEAVE;
    char nm[NAME_LEN + 2], hd[48];
    int nb = 0, pb = -1;
    page_tag(t);
    h = hash_s(hash_u(2166136261u, ui.page | (uint32_t)n << 8 | (uint32_t)big << 7), t);
    if (big && ui.page == P_ARRANGE) {               /* (the scenes there: which, cued, their use) */
        h = hash_u(h, (uint32_t)cur_scene() | (uint32_t)seq.cue << 8 | (uint32_t)(seq_cue_left(&seq) + 1) << 16 |
                          (uint32_t)seq.cue_end << 31 | (uint32_t)(ui.scene_pg & 1) << 30);
        for (int i = 0; i < NSCENE; i++)
            h = hash_u(h, pats.scene[i].used);
    }
    if (st) {                                        /* large type, Steps: the sound and the bars here */
        const tpat_t *tp = tpat(sel_trk());
        steps_head(sel_trk(), nm, hd);
        nb = (tp->len + 15) / 16;
        pb = playing() ? eng_step[sel_trk()] / 16 : -1;
        h = hash_s(hash_s(hash_u(h, (uint32_t)nb | (uint32_t)(pb + 1) << 8 | (uint32_t)ui.bar << 16), nm), hd);
    }
    if (h == ui.sig[3])
        return;
    ui.sig[3] = h;
    cv_begin(240, TAG_H, K_BG);
    if (n > 1) {
        r -= n * 9 - 3;
        for (int i = 0; i < n; i++)
            cv_round(r + i * 9, big ? 9 : 3, 6, 6, 3, i == d ? K_TEXT : K_LINE);
        r -= 8;
    }
    if (big && ui.page == P_ARRANGE) {
        int sc = cur_scene(), cue = seq.cue, left = seq_cue_left(&seq);
        char q[24], *e;
        for (int i = NSCENE - 1; i >= 0; i--) {      /* the 32 scenes, two rows of 16 (used: dim; this: bright) */
            int32_t dx = r - 96 + (i % 16) * 6, dy = 4 + (i / 16) * 8;
            cv_round(dx, dy, 5, 5, 1, pats.scene[i].used ? (i == sc ? K_NOW : K_DIM) : K_LINE);
            if (i == cue && cue != sc)
                cv_frame(dx, dy, 5, 5, 1, K_TEXT, K_BG);
        }
        cv_rect(r - 100, 4 + (ui.scene_pg & 1) * 8, 2, 5, K_TEXT);   /* (the white keys' 16, OCT: marked at the left) */
        e = num_s((uint32_t)sc + 1u, s_cat(q, "scene "));
        if (cue != 0xFF && cue != sc) {              /* a scene cued: to which, when (beats left; the pattern's end) */
            e = num_s((uint32_t)cue + 1u, s_cat(e, " > "));
            if (left >= 0)
                s_cat(num_s((uint32_t)(left + 4) / 4, s_cat(e, " in ")), "");
            else if (seq.cue_end)
                s_cat(e, " at end");
        }
        {   /* (no room for all of it, on another track's narrower tag or in a wider face: without "scene ") */
            const char *qs = text_w(&FONT_S, q) > r - 104 ? q + 6 : q;
            text_fit(8, cap_y(0, TAG_H, &FONT_S), &FONT_S, qs, r - 104, cue != 0xFF && cue != sc ? K_TEXT : K_DIM);
        }
        cv_blit(0, TAG_Y);
        return;
    }
    if (st) {
        for (int b = NBAR - 1; b >= 0; b--) {        /* the bars, right to left before the dots */
            uint16_t c = b < nb ? K_DIM : K_LINE, f = b == pb ? K_NOW : c;
            char q[4];
            r -= 20;
            cv_round(r, 2, 18, 20, 4, f);
            if (b == ui.bar)
                cv_frame(r, 2, 18, 20, 4, K_SEL, f);
            num_s((uint32_t)b + 1u, q);
            text_c(r + 9, cap_y(2, 20, &FONT_XS), &FONT_XS, q, b == ui.bar || b == pb ? K_BG : K_TEXT);
        }
        x = 8;
        if (nm[0])
            x = text_fit(x, cap_y(0, TAG_H, &FONT_S), &FONT_S, nm, 80, K_TEXT) + 8;
        if (hd[0])
            text_fit(x, cap_y(0, TAG_H, &FONT_S), &FONT_S, hd, r - 6 - x, K_DIM);
        cv_blit(0, TAG_Y);
        return;
    }
    while ((w = segs_w(&FONT_XS, v)) > r - 16 && *v) {
        char *c = v;
        while (*c && *c != '>')
            c++;
        if (*c)
            v = c + 1;
        else
            v[s_len(v) - 1] = 0;
    }
    x = r - w;
    segs(x, big ? 1 : -2, &FONT_XS, v, K_TEXT);
    if (x - 16 >= 24)
        cv_rect(8, big ? 12 : 6, x - 16, 1, K_LINE);
    cv_blit(0, TAG_Y);
}

/* the footer: what the keys do here; a list, a question, a card: what does what */
/* a slot page's hint: what SEL has past the catalog, its effect's extras (a send's: its filter) */
static void slot_hint(char *t, int pg)
{
    uint8_t mp[NTP];
    int st, q, n = 0;
    const uint8_t *par = slot_vals(pg, mp, &st, &q);
    const int ef = par[TP_SLOT[q][SL_EF]] % NEF;
    char *e = s_cat(t, "PRESET effect");
    for (int x = 3; x < 7; x++) {
        const char *w = efx_knob_word(ef, par[TP_SLOT[q][SL_X1]], x);
        if (w)
            e = s_cat(s_cat(e, n++ ? ", " : "|SEL "), w);
    }
    if (!n && st >= 0 && is_fx(st))
        e = s_cat(e, "|SEL filter");
    if (n < 3)                                       /* (the delay's four: no room) */
        s_cat(e, "|KEYS play");
}
static void page_footer(char *t)
{
    int trk = sel_trk();
    *t = 0;
    if (ui.ask) {
        if (plat_ms() < ui.msg_until) {
            s_cat(s_cat(s_cat(t, ui.msg[0]), " "), ui.msg[1]);
            upcase(t);
        } else
            s_cat(t, "SEL yes|HOME no");
        return;
    }
    if (rec_settings()) {                            /* REC held: its knobs, and step record */
        s_cat(t, playing() ? "KNOBS record settings|KEYS record" : "KNOBS record settings|PLAY record");
        return;
    }
    if ((ui.btn >> B_REC & 1u) && !home_held()) {   /* (playing, a sound page: the knobs record) */
        s_cat(t, "KNOBS record motion|KEYS record notes");
        return;
    }
    if (ui.card) {
        s_cat(t, "let go to close the help");
        return;
    }
    if (lst.open && lst.n) {
        int k = lst.row[lst.sel].kind;
        if (lst.bt && k == LK_ACTION && lst.row[lst.sel].id == A_BT_DEV) {
            s_cat(t, "SEL connect|HOME back");
            return;
        }
        if (k == LK_NOTE) {
            s_cat(t, "HOME back");
            return;
        }
        if (lst.row[lst.sel].id == A_MOD_LANE) {   /* (a MOD lane's row: SEL picks it) */
            s_cat(t, "SEL pick lane|HOME back");
            return;
        }
        s_cat(t, k == LK_ACTION ? "SEL confirm|HOME back"                       /* (the menu's rules: an action, a */
               : k == LK_PICK && !pick_sets(lst.row[lst.sel].id) ? "ALGO pick|SEL confirm|HOME back"   /* choice, a */
               : "ALGO change|SEL back");                /* setting or a knob's row) */
        return;
    }
    if (!ui.any_btn && ui.page == P_STEPS && !ui.held && !ui.chordmode) {
        s_cat(t, "WHITE steps|PLAY play|hold a button: help");
        return;
    }
    if (home_held()) {
        s_cat(t, ui.page == P_ARRANGE ? "REC undo|PLAY redo|KNOB 1 fine" : "REC undo|PLAY redo|KNOBS default");
        return;
    }
    switch (ui.page) {
    case P_ARRANGE:
        for (int i = 0; i < NKEYS; i++)             /* a kit's black key held: its sounds' mutes on the white keys */
            if (ui.kmode[i] == KM_MUTE && (ui.keys >> i & 1u) && is_drum(ui.knote[i] & 0x7F)) {
                s_cat(t, ui.knote[i] & 0x80 ? "WHITE mute a sound|let go: done" : "WHITE mute a sound|let go: mutes the kit");
                return;
            }
        s_cat(t, proj.rsv[2] & SCENES_AT_END ? "WHITE scene at the pattern's end|BLACK mute|OCT 1-16, 17-32"
                                             : "WHITE scene at the bar, again: now|BLACK mute|OCT 1-16, 17-32");
        break;
    case P_SONG:
        s_cat(t, "WHITE add scene|PLAY song|OCT 1-16, 17-32");
        break;
    case P_STEPS: case P_BAR:
        if (trk == AUX_WEAVE && ui.page == P_STEPS)
            s_cat(t, ui.wv_step < NSTEP ? "BLACK 1-10 its tracks|WHITE its end" : "WHITE steps: a tap adds, again clears");
        else if (!is_seqt(trk))
            s_cat(t, "ALGO track: no steps here");
        else if (ui.lockpg)
            s_cat(t, "KNOBS lock|SELECT page|SEL held + KNOB clear");
        else if (ui.trickpg)
            s_cat(t, "KNOB 1 3 trick|KNOB 2 4 value");
        else if (ui.chordmode && ui.page == P_STEPS)
            s_cat(t, ui.chord_pool ? "KEYS the pool|SEQ exit" : "KEYS the chord|SEQ exit");
        else if (ui.len_key && (ui.held >> (ui.len_key - 1) & 1u))
            s_cat(t, "let go: length|KNOBS both steps");
        else if (ui.held && is_drum(trk))
            s_cat(t, "BLACK sounds|ARP tricks|EDIT ENV FX LFO lock");
        else if (ui.held & (ui.held - 1u))
            s_cat(t, "WHITE more steps|ARP tricks|EDIT ENV FX LFO lock|SEQ notes");
        else if (ui.held)
            s_cat(t, "WHITE length|ARP tricks|EDIT ENV FX LFO lock|SEQ notes");
        else if (ui.rec_armed)
            s_cat(t, "KEYS write|OCT skip, back|REC+OCT clear|REC done");
        else if (seq.rec || seq.rec_wait)
            s_cat(t, "KEYS record|REC off");
        else if (ui.btn >> B_SEQ & 1u && ui.page == P_BAR)
            s_cat(t, "BLACK 1-4 jump to a bar|two: loop|twice: loop one");
        else if (ui.btn >> B_SEQ & 1u)
            s_cat(t, is_drum(trk) ? "KEYS pick a sound" : "KEYS the notes a step writes|OCT pattern octave");
        else if (is_drum(trk))
            s_cat(t, ui.page == P_BAR ? "WHITE steps|BLACK sound|SEQ held: bars" : "WHITE steps|BLACK sound|SEQ held: pick a sound");
        else
            s_cat(t, ui.page == P_BAR ? "WHITE steps|BLACK scale|SEQ held: bars" : "WHITE steps|SEQ held: play notes");
        break;
    case P_MIX:
        for (int i = 0; i < NKEYS; i++)             /* a kit's black key held: its keys' audio on the white keys */
            if (ui.kmode[i] == KM_SOLO && (ui.keys >> i & 1u) && (ui.knote[i] & 0x7F) < NTRK &&
                is_drum(ui.knote[i] & 0x7F)) {
                s_cat(t, "WHITE mute a sound|let go: done");
                return;
            }
        s_cat(t, "BLACK mute|SEL+BLACK wet|hold: solo");
        break;
    case P_MOD:
        s_cat(t, ui.mod_learn ? "turn the knob to learn, anywhere|SEL lanes"
                 : mod_none() ? "KNOB 1 a MOD pattern for this scene"
                 : mod_lane_now() < 0 ? "SEL add a lane|LEARN: SEL, then turn a knob"
                 : ui.held ? "KNOB 4 the step's value|SEL clear it"
                 : "WHITE a value|hold: set it|PRESET shape|OCT bar");
        break;
    case P_THROWS:
        s_cat(t, "KEYS throw: low drums, high melodic|REC held: record");
        break;
    case P_WEAVE:
        s_cat(t, "BLACK the track woven in|WHITE its pattern");
        break;
    case P_MASTER3:
        s_cat(t, "SEL pump by, level|KEYS play");
        break;
    case P_GLFO:
        s_cat(t, "BLACK the tracks it moves|WHITE a kit's sound, 12 all");
        break;
    case P_SETUP:
        s_cat(t, "SEL Bluetooth|KEYS play");
        break;
    case P_MASTER: case P_MFILT: case P_STATUS:
        s_cat(t, "KEYS play");
        break;
    case P_SENDS:
        s_cat(t, "KEYS play");
        break;
    case P_CHARACTER: case P_INSERT: case P_FX1: case P_FX2: case P_MASTER2:
        slot_hint(t, ui.page);
        break;
    default:
        if (ui.rec_armed)
            s_cat(t, "KEYS write|OCT skip, back|REC+OCT clear|REC done");
        else if (trk == AUX_BRAIN)
            s_cat(t, ui.page == P_PLAY ? "BLACK route track" : ui.page == P_SUB ? "BLACK the track" : "KEYS chord");
        else if (trk == AUX_PUNCH)
            s_cat(t, "KEYS fx|REC record");
        else if (trk == AUX_WEAVE)
            s_cat(t, "WHITE 1-10 weave a track|REC record");
        else if (is_aux(trk))
            s_cat(s_cat(t, "KEYS play "), TRACK_NAME[ui.last_inst % NTRK]);
        else if (ui.page >= P_PLAYERS && ui.page <= P_PLAYERS3 && pl_type(trk) == PL_STRUM)
            s_cat(t, "KEY strums the chord|KEYS together: a new chord");
        else if (ui.page >= P_PLAYERS && ui.page <= P_PLAYERS3 && pl_type(trk) == PL_ARP)
            s_cat(t, "KEYS hold notes: they arpeggiate");
        else if (ui.page >= P_PLAYERS && ui.page <= P_PLAYERS3 && pl_type(trk) == PL_RAND)
            s_cat(t, "POOL on Steps: hold a step, SEQ|steps on or off");
        else if (is_drum(trk) && ui.page == P_FILTER)
            s_cat(t, "PRESET type|BLACK pick a key");
        else if (is_drum(trk) && (ui.page == P_ENV2 || ui.page == P_KITF))
            s_cat(t, ui.page == P_KITF ? "PRESET on or off|KEYS play" : "KEYS play");
        else if (is_drum(trk) && ui.page <= P_LFO2)
            s_cat(t, !kit_edit_steps() ? "BLACK pick a key|KEYS play"
                     : ui.held ? "KNOBS lock the step|SEL held + KNOB clear" : "BLACK pick a key|WHITE its steps");
        else if (ui.page == P_ENGINE)
            s_cat(t, "PRESET next sound|SEL engine");
        else if (ui.page == P_FILTER && !is_acid(trk))
            s_cat(t, "PRESET filter type|KEYS play");
        else if (ui.page == P_ENV)
            s_cat(t, "KEYS play|REC held: record");
        else if (ui.page == P_LFO)
            s_cat(t, "KEYS play|REC held: record");
        else
            s_cat(t, "KEYS play|REC held: record");
        break;
    }
}
static void enc_line(char *t)
{
    int g = page_group(ui.page), n = group_n(g), p = ui.page, trk = sel_trk();
    char *e = t;
    *t = 0;
    if (ui.ask || (lst.open && lst.n) || ui.card)
        return;
    if (home_held()) {
        s_cat(t, ui.page == P_ARRANGE ? "SELECT tempo|PRESET scene|KNOB 1 fine" : "SELECT tempo|PRESET scene|KNOBS default");
        return;
    }
    if (n > 1)                        /* SELECT names what it turns to where that is the point of the page */
        e = s_cat(t, p == P_CHARACTER ? "SELECT insert 2" : p == P_INSERT ? "SELECT sends" : p == P_SENDS ? "SELECT throws" : p == P_ENV ? "SELECT filter"
                     : p == P_LFO ? "SELECT more" : "SELECT page");
    if (p != P_ARRANGE && p != P_SONG && p != P_THROWS && p < P_MASTER)
        e = s_cat(e, *t ? "|ALGO track" : "ALGO track");
    if (p == P_MOD)
        e = s_cat(e, *t ? "|ALGO lane, track" : "ALGO lane, track");
    {
        const char *a = p == P_ARRANGE ? "scene" : p == P_SONG ? "slot"
                        : steps_page(p) && is_drum(trk) ? "sound"
                        : steps_page(p) && is_seqt(trk) ? "pattern"
                        : (p == P_ENGINE || p == P_PLAY || p == P_ENV) && is_drum(trk) ? "key"
                        : p == P_ENGINE && is_fx(trk) ? "effect"
                        : p == P_MIX ? "track" : 0;   /* (Engine, Filter, the FX pages: the footer says it) */
        if (a)
            s_cat(s_cat(e, *t ? "|PRESET " : "PRESET "), a);
    }
}
#define BOT_LH 16
#define BOT_PH 15
#define BOT_PAD 3
#define BOT_PAD_E 5
#define BOT_JOB 3
#define BOT_GAP 10
#define BOT_X 6
#define BOT_W 228
#define BOT_PARTS 8
static const char *const CTL_WORD[] = {"SELECT", "ALGO", "PRESET", "SEL", "HOME", "REC", "PLAY", "OCT", "KEYS", "WHITE",
                                       "BLACK", "SAVE", "EDIT", "SEQ", "ENV", "FX", "LFO", "ARP", "GLO", "KNOBS"};
static int ctl_is(const char *s, uint32_t n)
{
    for (uint32_t i = 0; i < sizeof CTL_WORD / sizeof CTL_WORD[0]; i++) {
        const char *w = CTL_WORD[i];
        uint32_t j = 0;
        while (j < n && w[j] && w[j] == s[j])
            j++;
        if (j == n && !w[j])
            return 1;
    }
    return 0;
}
static uint32_t ctl_len(const char *s)
{
    uint32_t n = 0, a = 0;
    if (s[0] == 'O' && s[1] == 'C' && s[2] == 'T' && (s[3] == '+' || s[3] == '-') && (s[4] == ' ' || !s[4]))
        return 4;                                    /* "OCT+ rest": one control, its sign part of the name */
    for (;; n++)
        if (s[n] == '+' || s[n] == ' ' || !s[n]) {
            if (n == a || !ctl_is(s + a, n - a))
                return 0;
            if (s[n] != '+')
                return n;
            a = n + 1;
        }
}
static int s_has_q(const char *s)                   /* a label with a Q: its tail needs the pixel below */
{
    for (; *s; s++)
        if (*s == 'Q')
            return 1;
    return 0;
}
static int32_t bot_part(int32_t x, int32_t y, const char *s, uint32_t n, const felucca_font_t *job, int draw)
{
    char b[48];
    uint32_t c = ctl_len(s), a = 0, i;
    int32_t x0 = x;
    if (c > n)
        c = 0;
    for (i = 0; c && i <= c; i++)
        if (i == c || (s[i] == '+' && !(i == 3 && s[0] == 'O'))) {     /* ("OCT+": its sign is part of the name) */
            uint32_t k;
            int32_t w;
            for (k = 0; k < i - a && k < sizeof b - 1u; k++)
                b[k] = s[a + k];
            b[k] = 0;
            int enc = s_eq(b, "SELECT") || s_eq(b, "ALGO") || s_eq(b, "PRESET") || s_eq(b, "KNOBS");
            int32_t pad = enc ? BOT_PAD_E : BOT_PAD;
            w = text_w(&FONT_XS, b) + 2 * pad;
            if (draw) {
                cv_round(x, y, w, BOT_PH, enc ? 7 : 2, K_TEXT);
                cv_round(x + 1, y + 1, w - 2, BOT_PH - 2, enc ? 6 : 1, K_BG);
                cv_text(x + pad, cap_y(y, BOT_PH, &FONT_XS) - (s_has_q(b) ? 1 : 0), &FONT_XS, b, K_TEXT);   /* (Q's tail) */
            }
            x += w;
            if (i < c) {
                if (draw)
                    cv_text(x + 2, cap_y(y, BOT_PH, &FONT_XS), &FONT_XS, "+", K_TEXT);
                x += text_w(&FONT_XS, "+") + 4;
            }
            a = i + 1;
        }
    if (c) {
        c++;
        if (c >= n)
            return x - x0;
        x += BOT_JOB;
    }
    for (i = 0; c + i < n && i < sizeof b - 1u; i++)
        b[i] = s[c + i];
    b[i] = 0;
    if (draw)
        cv_text(x, cap_y(y, BOT_PH, job), job, b, c ? K_TEXT : K_DIM);
    return x + text_w(job, b) - x0;
}
static int ne0(const char *e)
{
    int n = *e != 0;
    for (; *e; e++)
        n += *e == '|';
    return n;
}
typedef struct { const char *s[BOT_PARTS]; uint32_t n[BOT_PARTS]; int32_t w[BOT_PARTS]; int cnt; } bot_t;
static void bot_split(bot_t *p, const char *t, const felucca_font_t *job)
{
    while (*t && p->cnt < BOT_PARTS) {
        uint32_t n = 0;
        while (t[n] && t[n] != '|')
            n++;
        p->s[p->cnt] = t;
        p->n[p->cnt] = n;
        p->w[p->cnt] = bot_part(0, 0, t, n, job, 0);
        p->cnt++;
        t += n;
        if (*t)
            t++;
    }
}
static int32_t bot_w(const bot_t *p, int a, int b)
{
    int32_t w = 0;
    for (int i = a; i < b; i++)
        w += p->w[i] + (i > a ? BOT_GAP : 0);
    return w;
}
static void bot_line(const bot_t *p, int a, int b, int32_t y, const felucca_font_t *job)
{
    int32_t tw = 0, gap = 0, x = BOT_X;
    for (int i = a; i < b; i++)
        tw += p->w[i];
    if (b - a > 1) {
        gap = (BOT_W - tw) / (b - a - 1);
        gap = gap > 28 ? 28 : gap;
    }
    for (int i = a; i < b; i++)
        x += bot_part(x, y, p->s[i], p->n[i], job, 1) + gap;
}
static void draw_footer_as(void);
static void draw_footer(void)                        /* the hints keep today's faces in large type: they are hints */
{
    int was = lt_on;
    loom_type(0);
    draw_footer_as();
    loom_type(was);
}
static void draw_footer_as(void)
{
    char t[80], e[64];
    int list = lst.open && !ui.card, ne = 0, split = -1, k;
    const felucca_font_t *job = &FONT_S;
    bot_t p;
    uint32_t h = hash_u(2166136261u, (uint32_t)list);
    page_footer(t);
    enc_line(e);
    if (list)
        *e = 0;
    h = hash_s(h, t);
    h = hash_s(h ^ 0x80u, e);
    if (h == ui.sig[4])
        return;
    ui.sig[4] = h;
    if (ENC_Y == FOOT_Y && !list) {                  /* one row (large type, Steps): the page's own hints, the
                                                      * encoders' and "hold a button" in the cards */
        char *c, *o;
        for (c = o = t; *c; ) {                      /* (the footer's parts without "hold a button: help") */
            char *e2 = c;
            while (*e2 && *e2 != '|')
                e2++;
            if (!(e2 - c == 19 && c[0] == 'h' && c[5] == 'a' && c[7] == 'b')) {
                if (o != t)
                    *o++ = '|';
                while (c < e2)
                    *o++ = *c++;
            }
            c = *e2 ? e2 + 1 : e2;
        }
        *o = 0;
        for (int f = 0; f < 2; f++) {
            job = f ? &FONT_XS : &FONT_S;
            memset(&p, 0, sizeof p);
            bot_split(&p, t, job);
            if (bot_w(&p, 0, p.cnt) <= BOT_W)
                break;
        }
        while (p.cnt > 1 && bot_w(&p, 0, p.cnt) > BOT_W)
            p.cnt--;                                 /* (the rest: hold a button) */
        cv_begin(240, BOT_LH, K_BG);
        bot_line(&p, 0, p.cnt, 0, job);
        cv_blit(0, FOOT_Y);
        return;
    }
    for (int f = 0; f < 2 && split < 0; f++)
        for (int drop = 0; drop <= ne0(e) && split < 0; drop++) {
            job = f ? &FONT_XS : &FONT_S;
            memset(&p, 0, sizeof p);
            bot_split(&p, e, job);
            p.cnt -= drop;
            ne = p.cnt;
            bot_split(&p, t, job);
            if (bot_w(&p, 0, p.cnt) <= BOT_W)
                split = 0;
            else if (!list)
                for (int d = 0; d < p.cnt && split < 0; d++)
                    for (int sg = -1; sg <= 1 && split < 0; sg += 2) {
                        k = ne + sg * d;
                        if (k > 0 && k < p.cnt && bot_w(&p, 0, k) <= BOT_W && bot_w(&p, k, p.cnt) <= BOT_W)
                            split = k;
                    }
        }
#if defined(PLAT_HOST) && !defined(PLAT_WEB)
    if (split < 0 || job != &FONT_S) {
        printf("  FAIL bottom lines too wide: %s / %s\n", e, t);
        msg_too_wide++;
    }
#endif
    if (split < 0)
        split = p.cnt > 1 ? ne > 0 && ne < p.cnt ? ne : 1 : 0;
    if (list) {
        cv_begin(240, BOT_LH, K_BG);
        bot_line(&p, 0, p.cnt, 0, job);
        cv_blit(0, FOOT_Y);
        return;
    }
    cv_begin(240, 2 * BOT_LH, K_BG);
    if (!split)
        bot_line(&p, 0, p.cnt, BOT_LH / 2, job);
    else {
        bot_line(&p, 0, split, 0, job);
        bot_line(&p, split, p.cnt, BOT_LH, job);
    }
    cv_blit(0, ENC_Y);
}

/* the help cards: a button held alone for a second says what it does on this page */
#define PG(p) (1ull << (p))                          /* (more than 32 pages: 64 bits) */
#define PG_ANY (~0ull)
#define PG_STEPS (PG(P_STEPS) | PG(P_BAR))
#define PG_SOUND (PG(P_ENGINE) | PG(P_PLAY) | PG(P_PLAY2) | PG(P_SUB) | PG(P_OSC2) | PG(P_KMOD) | PG(P_ENV) | PG(P_ENV2) | PG(P_KITF) | PG(P_FILTER) | \
                  PG(P_CHARACTER) | PG(P_INSERT) | PG(P_LFO) | PG(P_LFO2))
static const struct { uint8_t btn; uint64_t pages; const char *title, *l1, *l2; } CARD[] = {
    {B_SEQ, PG(P_BAR), "Bars", "Hold SEQ and press black key 1-4 to jump to that bar.",
     "Hold two to loop them. Tap one twice to loop it."},
    {B_SEQ, PG(P_MOD), "MOD lanes", "Each lane moves a knob over the pattern's steps. Tap a step for",
     "a value, hold it and turn KNOB 4. SEL adds lanes; ALGO picks one."},
    {B_SEQ, PG_ANY, "Steps", "The white keys are the bar's 16 steps. Hold SEQ and play notes,",
     "then press steps to write them. Hold several to edit them all."},
    {B_EDIT, PG_STEPS, "Lock", "Hold a step and press EDIT, ENV, FX or LFO for its knobs.",
     "Turn one to lock it. Hold SEL and turn it to clear."},
    {B_EDIT, PG_ANY, "Engine", "Press PRESET for the next sound.",
     "Hold EDIT to play the keys or turn ALGORITHM for the engine."},
    {B_ENV, PG_ANY, "Envelope", "The amp's envelope, the filter, its envelope.",
     "Press PRESET on Filter for its type. A kit's key pages come first."},
    {B_FX, PG_ANY, "Effects", "Insert 1 and 2, Sends, Throws: keys throw.",
     "Press PRESET for the next effect, SEL for them all."},
    {B_LFO, PG_ANY, "LFO", "Type, then its first three knobs.", "Turn SELECT for its fourth and fifth."},
    {B_ARP, PG_ANY, "Players", "Arp, strum, hold or random; last page, Weave.",
     "Random: tap steps for slots, hold one and press SEQ for the pool."},
    {B_ARP, PG_STEPS, "Tricks", "Hold a step and press ARP. Turn KNOB 1 and 3 for its tricks,",
     "KNOB 2 and 4 for their values. Let go of the step when done."},
    {B_GLO, PG(P_MFILT), "Master filter", "KNOB 1 drums, KNOB 2 melodic, KNOB 3 the whole mix.",
     "Left a low-pass, right a high-pass. KNOB 4: resonance."},
    {B_GLO, PG_ANY, "Mix", "Black keys mute the sound, SEL held: wet only. Hold: solo.",
     "A kit's key held: white keys its sounds. SEL: projects."},
    {B_HOME, PG_ANY, "Arrange", "Press white keys for scenes, black keys to mute notes.",
     "Hold HOME, press REC to undo, PLAY to redo."},
    {B_SEL, PG(P_CHARACTER) | PG(P_INSERT) | PG(P_FX1) | PG(P_FX2) | PG(P_MASTER2), "Effects",
     "Every effect, then its extras. Press SEL on one to use it.", "Dimmed: no memory left for it."},
    {B_SEL, PG_ANY, "Lists", "Actions, then knobs. Press SEL to run one.",
     "Turn SELECT to move, ALGORITHM to change it."},
    {B_SAVE, PG_STEPS, "Save", "Tap to save. Hold a step and press SEL to clear its locks.",
     "Hold SAVE and press white key 1 to 3 to save to that slot."},
    {B_SAVE, PG_SOUND, "Save", "Tap to save the project.",
     "Hold SAVE and press a white key to save the sound as a preset."},
    {B_SAVE, PG_ANY, "Save", "Tap to save now. LOOM-8 also saves when stopped and quiet.", "Hold SAVE and press white key 1 to 3 to save to that slot."},
    {B_REC, PG_ANY, "Recording", "When stopped, tap it and play keys to write steps.",
     "Hold it and play to record live, or turn knobs to set it up."},
    {B_PLAY, PG(P_SONG), "Play", "Press to play the song from the slot picked, again to stop.", "For the top, use PLAY SONG on SEL."},
    {B_PLAY, PG_ANY, "Play", "Press to start or stop, twice quickly to start from the top.", "Hold HOME and press PLAY to redo."},
    {B_OCTDN, PG_STEPS, "Bar", "Press for the bar before. Hold a step and press to nudge it earlier.",
     "In step record, press to step back."},
    {B_OCTUP, PG_STEPS, "Bar", "Press for the next bar. Hold a step and press to nudge it later.",
     "In step record, press to step on."},
    {B_OCTDN, PG_ANY, "Octave", "Press for the keys an octave lower.", 0},
    {B_OCTUP, PG_ANY, "Octave", "Press for the keys an octave higher.", 0},
};
#define NCARDS (sizeof CARD / sizeof CARD[0])
static int card_find(int btn)
{
    for (uint32_t i = 0; i < NCARDS; i++)
        if (CARD[i].btn == btn && (CARD[i].pages >> ui.page & 1u))
            return (int)i;
    return -1;
}
/* a card's band: the main band (large type: down to the hints, over the tag and the knobs), its lines' pitch */
#define CARD_H (LARGE ? ENC_Y - MAIN_Y : MAIN_H)
#define CARD_LH (LARGE ? FONT_S.h + 1 : 17)
/* where card i's text ends (the box's top in *top): both lines wrapped at 200 px */
static int32_t card_end(int i, int32_t *top)
{
    int32_t n = wrap_do(0, 0, 200, &FONT_S, CARD[i].l1, 0, 0) / CARD_LH +
                (CARD[i].l2 ? wrap_do(0, 0, 200, &FONT_S, CARD[i].l2, 0, 0) / CARD_LH : 0);
    int32_t bh = 42 + (n - 1) * CARD_LH + (CARD[i].l2 ? 4 : 0), t = 4 + (CARD_H - 8 - bh) / 2;
    if (t < 10)
        t = 10;
    *top = t;
    return t + 30 + n * CARD_LH + (CARD[i].l2 ? 4 : 0);
}
#if defined(PLAT_HOST) && !defined(PLAT_WEB)
static void cards_audit(void)                  /* every card on every page it is shown: its text fits the box */
{
    for (uint32_t i = 0; i < NCARDS; i++) {
        int32_t top;
        if (card_end((int)i, &top) - 1 > CARD_H - 4) {
            printf("  FAIL card too long: %s (%s)\n", CARD[i].title, CARD[i].l1);
            msg_too_wide++;
        }
    }
}
#endif
static void card_paint(int i);
static void draw_card(void)
{
    int i = card_find(ui.card - 1);
    uint32_t h = hash_u(hash_u(2166136261u, 0xCAu), ui.card + ((uint32_t)ui.page << 8));
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;
    if (LARGE)
        ui.sig[2] = ui.sig[3] = 0;                   /* (the tag and the knobs under it: drawn again after it) */
    for (int32_t off = 0; off < CARD_H; off += CV_ROWS) {   /* (taller than the canvas: in strips) */
        strip_begin(CARD_H, off, K_BG);
        card_paint(i);
        cv_blit(0, (uint32_t)(MAIN_Y + off));
    }
}
static void card_paint(int i)
{
    int32_t x, y;
    cv_round(8, 4, 224, CARD_H - 8, 8, K_PANEL);
    {
        int32_t top = 4 + (CARD_H - 8 - 20) / 2;
        if (i >= 0)
            card_end(i, &top);
        else if (top < 10)
            top = 10;
        x = pill_o(18, top, 0, BTN_LABEL[(ui.card - 1) % NB], K_TEXT, K_TEXT);
        y = top + 30;
    }
    if (i >= 0) {
        cv_text(x + 10, cap_y(y - 30, 20, &FONT_B), &FONT_B, CARD[i].title, K_TEXT);
        cv_rect(18, y - 6, 204, 1, K_DIM);
        y = wrap(20, y, 200, &FONT_S, CARD[i].l1, K_TEXT);
        if (CARD[i].l2)
            y = wrap(20, y + 4, 200, &FONT_S, CARD[i].l2, K_DIM);
    }
}

/* ------------------------------------------------------------- lists --- */
static void list_add(const char *name, const char *val, int kind, int id)
{
    if (lst.n >= LIST_ROWS)
        return;
    lst.row[lst.n].name = name;
    lst.row[lst.n].val[0] = 0;
    if (val)
        for (int i = 0; i < 23 && val[i]; i++)
            lst.row[lst.n].val[i] = val[i], lst.row[lst.n].val[i + 1] = 0;
    lst.row[lst.n].kind = (uint8_t)kind;
    lst.row[lst.n].id = (uint8_t)id;
    lst.n++;
}
static void proj_slot_text(int slot, char *t)     /* "Proj 2", "Proj 3 (open)", "Proj 1 (empty)": a project is its slot
                                                   * (no names) */
{
    char *e = num_s((uint32_t)slot + 1u, s_cat(t, "Proj "));
    if (slot == proj_slot)
        s_cat(e, " (open)");
    else if (!project_exists(slot))
        s_cat(e, " (empty)");
}
/* the kits (a kit track's SELECT list): the factory kits (Loom, then tools/gen_kits.py's), then the
 * user's (project.c user_kit_name) */
#include "kits.h"
#define NFKITS (1 + NGKITS)
#define NKITS (NFKITS + NUSER_KITS)
static const char *kit_name(int k) { return k == 0 ? "Loom" : GKIT_NAME[k - 1]; }
static void kit_label(int k, char *t)              /* "Loom", "K2 deep", "K3 (empty)" */
{
    if (k < NFKITS) {
        s_cat(t, kit_name(k));
        return;
    }
    k -= NFKITS;
    t = num_s((uint32_t)k + 1u, s_cat(t, "K"));
    if (user_kit_name[k][0])
        s_cat(s_cat(t, " "), user_kit_name[k]);
    else
        s_cat(t, " (empty)");
}
static void list_midi_out(int trk)                 /* the track's notes out, on its own channel (with Setup's NOTES) */
{
    list_add(ACTION_NAME[A_MIDI_OUT], proj.trk[trk].par[TP_CHAN] ? "on" : "off", LK_PICK, A_MIDI_OUT);
}
static void list_clip_step(void)                   /* a held step's list, after its notes: copy it, paste a step, its
                                                    * locks cleared */
{
    list_add(ACTION_NAME[A_COPY_STEP], 0, LK_ACTION, A_COPY_STEP);
    if (clip_ok && clip_kind == CK_STEP) {
        list_add(ACTION_NAME[A_PASTE_STEP], 0, LK_ACTION, A_PASTE_STEP);
        list_add(ACTION_NAME[A_PASTE_LOCKS], 0, LK_ACTION, A_PASTE_LOCKS);
    }
    list_add(ACTION_NAME[A_CLEAR_STEP_LOCKS], 0, LK_ACTION, A_CLEAR_STEP_LOCKS);
}
/* the Bluetooth lists (GLO's AUDIO DEVICE and MIDI DEVICE, SEL): a first line that says what to do, then the
 * devices (audio: the remembered ones first), each labelled */
static char bt_note[32];
static void list_note(const char *t)
{
    bt_note[0] = 0;
    s_cat(bt_note, t);
    list_add(bt_note, 0, LK_NOTE, 0);
}
static void list_bt_audio(void)
{
    int st = BTA(state)(), n = 0, i, k, look = st == PLAT_BTA_SCANNING;
    list_note("");
    for (i = 0; n < BT_ROWS && BTA(known)(i, bt_nm[n]); i++, n++) {
        bt_ix[n] = (uint8_t)(0x80 | i);
        list_add(bt_nm[n], i == bt.cur && st != PLAT_BTA_OFF && st != PLAT_BTA_SCANNING && st != PLAT_BTA_NONE &&
                           st != PLAT_BTA_FOUND ? bt_state_word(st) : "remembered", LK_ACTION, A_BT_DEV);
    }
    k = n;
    for (i = 0; n < BT_ROWS && BTA(found)(i, bt_nm[n]); i++) {
        int seen = 0;
        for (int j = 0; j < k; j++)                  /* (a remembered one found again: its row already) */
            seen |= s_eq(bt_nm[j], bt_nm[n]);
        if (seen)
            continue;
        bt_ix[n] = (uint8_t)i;
        list_add(bt_nm[n++], "new", LK_ACTION, A_BT_DEV);
    }
    s_cat(bt_note, look ? (n ? "pick one and press SEL" : "looking for devices...")
                        : n ? "pick one and press SEL" : "none found. In pairing mode?");
    list_add(ACTION_NAME[A_BT_LOOK], look ? "looking" : "", LK_ACTION, A_BT_LOOK);
    if (k) {
        char nm[17];
        if (ui.pick_bt >= k)
            ui.pick_bt = 0;
        BTA(known)(ui.pick_bt, nm);
        list_add(ACTION_NAME[A_BT_FORGET], nm, LK_PICK, A_BT_FORGET);
    }
}
static void list_bt_dev(void)
{
    int n;
    if (btdev_busy()) {
        list_note(btdev_ready() ? "Press SEL to disconnect" : "connecting...");
        list_add(ACTION_NAME[A_BT_DISCONNECT], btdev_name(), LK_ACTION, A_BT_DISCONNECT);
        return;
    }
    list_note("");
    for (n = 0; n < BT_ROWS && btdev_found(n, bt_nm[n], 0); n++)
        list_add(bt_nm[n], "found", LK_ACTION, A_BT_DEV);
    s_cat(bt_note, n ? "pick one and press SEL" : plat_ms() - bt.t0 < 8000u ? "looking for MIDI devices..."
                                                     : "none found yet. Is it on?");
}
/* the first empty pattern of a track (its steps all empty), not the one shown; -1 none */
static int pat_first_empty(int trk)
{
    for (int p = 0; p < NPATT; p++) {
        int empty = p != cur_pat(trk);
        for (int i = 0; i < NSTEP && empty; i++)
            empty = step_empty(&pats.pat[trk][p].st[i]);
        if (empty)
            return p;
    }
    return -1;
}
static void list_head(const char *t) { list_add(t, 0, LK_HEAD, 0); }
/* GLO's list, first: the slots (the open one marked) with their tempo and scenes as saved, then SAVE TO and NEW
 * PROJECT */
static const char *const PROJ_ROW[2][NPROJ] = {{"PROJ 1", "PROJ 2", "PROJ 3"},
                                                {"PROJ 1 (OPEN)", "PROJ 2 (OPEN)", "PROJ 3 (OPEN)"}};
static void proj_summary(int slot, char *t)        /* "122 bpm, 4 scenes", "98.5 bpm", "empty" */
{
    uint16_t tx;
    uint8_t ns;
    char *e;
    if (!project_slot_info(slot, &tx, &ns)) {
        s_cat(t, "empty");
        return;
    }
    e = num_s(tx / 10u, t);
    if (tx % 10u) {
        *e++ = '.';
        e = num_s(tx % 10u, e);
    }
    e = s_cat(e, " bpm");
    if (ns)                                        /* (0: saved before the count was kept) */
        s_cat(num_s(ns, s_cat(e, ", ")), ns == 1 ? " scene" : " scenes");
}
static void list_projects(void)
{
    static char val[NPROJ][24], save_t[24];
    list_head("projects");
    for (int s = 0; s < NPROJ; s++) {
        proj_summary(s, val[s]);
        list_add(PROJ_ROW[s == proj_slot][s], val[s], LK_ACTION, A_PROJ1 + s);
    }
    proj_slot_text(ui.pick_save % NPROJ, save_t);
    list_add(ACTION_NAME[A_SAVE_TO], save_t, LK_PICK, A_SAVE_TO);
    list_add(ACTION_NAME[A_NEW_PROJ], 0, LK_ACTION, A_NEW_PROJ);
}
static void list_page_knobs(int p, uint32_t mask)  /* page P's knobs (bit i: knob i) not listed yet */
{
    knob_t k[4];
    uint8_t keep = ui.page;
    ui.page = (uint8_t)p;
    page_knobs(k);
    ui.page = keep;
    for (int i = 0; i < 4; i++) {
        int seen = 0;
        if (!(mask >> i & 1u) || !k[i].name)
            continue;
        for (int j = 0; j < lst.n; j++)          /* (a knob twice on a page, or on Steps and Bar: GROOVE; on two
                                                  * pages of one name, two things: the filter envelope's attack) */
            seen |= lst.row[j].kind == LK_KNOB && ((lst.row[j].id >> 2) == p || (steps_page(lst.row[j].id >> 2) &&
                    steps_page(p))) && s_eq(lst.row[j].name, k[i].name) && s_eq(lst.row[j].val, k[i].val);
        if (!seen)
            list_add(k[i].name, k[i].val, LK_KNOB, p << 2 | i);
    }
}
static void list_page_section(int p)              /* a page's knobs under its name (none new: no name) */
{
    int n = lst.n;
    list_head(page_name_of(p, sel_trk()));
    list_page_knobs(p, 15u);
    if (lst.n == n + 1)
        lst.n = (uint8_t)n;
}
static int pick_amount(int v) { return v ? v : 1; }   /* SHIFT NOTES', ROTATE's amount (never 0) */
/* the MOD page's list, in sections: "lanes" (each its target, glide or step; SEL picks it), "this lane: N, target"
 * (GLIDE / STEP; its target: TRACK, PAGE and KNOB, ALGORITHM moves the lane there at once, its steps and values kept;
 * LEARN: the next knob turned by hand; REMOVE, CLEAR), "new lane" (TRACK, PAGE and KNOB: ALGORITHM; ADD LANE; LEARN:
 * the next knob turned by hand). The scene's MOD pattern "-": a note */
static const char *const MOD_ROW[NMODL] = {"LANE 1", "LANE 2", "LANE 3", "LANE 4", "LANE 5", "LANE 6", "LANE 7", "LANE 8"};
static void list_mod_target(int tg, int p, int ida)   /* TRACK, PAGE, KNOB of target (tg, p): ids ida, + 1, + 2 */
{
    char t[24];
    int pg = -1, pi;
    if (p >= 0 && ida == A_MOD_TTRK)
        mod_locate(tg, p, &pi, &pg);
    else if (ida == A_MOD_TRK)
        p = mod_pick_param(&pg);
    list_add(ACTION_NAME[ida], tg == MODT_MST ? "master" : TRACK_NAME[tg], LK_PICK, ida);
    list_add(ACTION_NAME[ida + 1], pg < 0 ? "-" : page_name_of(pg, tg == MODT_MST ? 0 : tg), LK_PICK, ida + 1);
    t[0] = 0;
    if (p >= 0)
        mod_tgt_name(tg, p, t);
    list_add(ACTION_NAME[ida + 2], p >= 0 ? t + s_len(tg == MODT_MST ? "master " : TRACK_NAME[tg]) + (tg != MODT_MST)
                                          : "-", LK_PICK, ida + 2);
}
static void list_mod(void)
{
    static char th[40];
    uint8_t ix[NMODL];
    char t[24], at[24];
    const int n = mod_lanes_now(ix), l = mod_lane_now();
    int pg, p;
    list_head("lanes");
    if (mod_none()) {
        list_add("NO MOD PATTERN", "KNOB 1 picks one", LK_NOTE, 0);
        return;
    }
    if (!n)
        list_add("NO LANES YET", "add one below", LK_NOTE, 0);
    for (int i = 0; i < n; i++) {                    /* "T2 cutoff, glide" */
        mod_tgt_name(lane_mod_tgt(&pats.lane[ix[i]]), pats.lane[ix[i]].param, t);
        s_cat(s_cat(t + s_len(t), ", "), LANE_SHAPE_NAME[lane_shape(&pats.lane[ix[i]])]);
        list_add(MOD_ROW[i], t, LK_ACTION, A_MOD_LANE);
    }
    if (l >= 0) {                                    /* "this lane: 2, T2 cutoff" (the heads' face has no middle dot) */
        const lane_t *L = &pats.lane[l];
        th[0] = 0;
        mod_tgt_name(lane_mod_tgt(L), L->param,
                     s_cat(num_s((uint32_t)ui.mod_lane % NMODL + 1u, s_cat(th, "this lane: ")), ", "));
        list_head(th);
        list_add(ACTION_NAME[A_MOD_KIND], LANE_SHAPE_NAME[lane_shape(L)], LK_PICK, A_MOD_KIND);
        list_mod_target(lane_mod_tgt(L), L->param, A_MOD_TTRK);
        list_add(ACTION_NAME[A_MOD_TLEARN], ui.mod_learn == 2 ? "turn a knob" : "by turning", LK_ACTION, A_MOD_TLEARN);
        list_add(ACTION_NAME[A_MOD_REMOVE], 0, LK_ACTION, A_MOD_REMOVE);
        list_add(ACTION_NAME[A_MOD_CLEAR], 0, LK_ACTION, A_MOD_CLEAR);
    }
    list_head("new lane");
    list_mod_target(mod_pick_tgt(), -1, A_MOD_TRK);
    p = mod_pick_param(&pg);
    at[0] = 0;
    if (p >= 0)
        mod_tgt_name(mod_pick_tgt(), p, at);
    list_add(ACTION_NAME[A_MOD_ADD], p < 0 ? "none on that knob" : n >= NMODL ? "eight already" : at, LK_ACTION, A_MOD_ADD);
    list_add(ACTION_NAME[A_MOD_LEARN], ui.mod_learn == 1 ? "turn a knob" : "by turning", LK_ACTION, A_MOD_LEARN);
}
/* Steps and Bar: the pattern's actions, then the bar's */
static void list_steps(int trk)
{
    static char empty_t[16], bar_t[8], rot_t[8], sh_t[8];
    int to = pat_first_empty(trk), paste = clip_ok && clip_kind != CK_STEP;   /* (a step pastes onto steps held) */
    list_head("pattern");
    if (to >= 0)
        num_s((uint32_t)to + 1u, s_cat(empty_t, "P"));
    else
        s_cat(empty_t, "none empty");
    list_add(ACTION_NAME[A_COPY_EMPTY], empty_t, LK_ACTION, A_COPY_EMPTY);
    list_add(ACTION_NAME[A_COPY_PAT], 0, LK_ACTION, A_COPY_PAT);
    if (paste && clip_kind == CK_PAT)
        list_add(ACTION_NAME[A_PASTE], ui.pick_paste & 1u ? "locks only" : "pattern", LK_PICK, A_PASTE);
    list_add(ACTION_NAME[A_CLEAR], CLEAR_WHAT[ui.pick_clear % 3u], LK_PICK, A_CLEAR);
    {
        int r = pick_amount(ui.pick_rot);
        num_s((uint32_t)(r < 0 ? -r : r), s_cat(rot_t, r < 0 ? "L" : "R"));
        list_add(ACTION_NAME[A_ROTATE], rot_t, LK_PICK, A_ROTATE);
    }
    if (!is_drum(trk) && trk != AUX_PUNCH) {         /* (a kit's notes are its sounds, THROW's its effects) */
        int v = pick_amount(ui.pick_shift);
        num_s((uint32_t)(v < 0 ? -v : v), s_cat(sh_t, v < 0 ? "-" : "+"));
        list_add(ACTION_NAME[A_SHIFT], sh_t, LK_PICK, A_SHIFT);
    }
    list_add(ACTION_NAME[A_DOUBLE], 0, LK_ACTION, A_DOUBLE);
    num_s((uint32_t)ui.bar + 1u, s_cat(bar_t, "bar "));
    list_head(bar_t);
    list_add(ACTION_NAME[A_COPY_BAR], 0, LK_ACTION, A_COPY_BAR);
    if (paste && clip_kind == CK_BAR)
        list_add(ACTION_NAME[A_PASTE], ui.pick_paste & 1u ? "locks only" : "bar", LK_PICK, A_PASTE);
    list_add(ACTION_NAME[A_CLEAR_BAR], 0, LK_ACTION, A_CLEAR_BAR);
    list_add(ACTION_NAME[A_DUP_BAR], 0, LK_ACTION, A_DUP_BAR);
}
/* HOME: the page's own first (Song: the song; Arrange: the scenes), then RANDOMIZE PRESETS and ALL NOTES OFF
 * (the projects moved to GLO's list) */
static void list_home(int keep)
{
    static char scl_t[12];
    for (int i = 0; i < 2; i++)
        if ((i == 0) == (keep == P_SONG)) {
            list_head("song");
            list_add(ACTION_NAME[A_PLAY_SONG], 0, LK_ACTION, A_PLAY_SONG);
            list_add(ACTION_NAME[A_INSERT_SLOT], 0, LK_ACTION, A_INSERT_SLOT);
            list_add(ACTION_NAME[A_DUP_SLOT], 0, LK_ACTION, A_DUP_SLOT);
            list_add(ACTION_NAME[A_DELETE_SLOT], 0, LK_ACTION, A_DELETE_SLOT);
            list_add(ACTION_NAME[A_CLEAR_SONG], 0, LK_ACTION, A_CLEAR_SONG);
        } else {
            list_head("scenes");
            list_add(ACTION_NAME[A_SCENE_AT], proj.rsv[2] & SCENES_AT_END ? "pattern end" : "next bar", LK_PICK,
                     A_SCENE_AT);
            list_add(ACTION_NAME[A_COPY_SCENE], 0, LK_ACTION, A_COPY_SCENE);
            if (scene_clip && scene_clip - 1 != cur_scene() && pats.scene[scene_clip - 1].used) {
                num_s((uint32_t)scene_clip, s_cat(scl_t, "from S"));
                list_add(ACTION_NAME[A_PASTE_SCENE], scl_t, LK_ACTION, A_PASTE_SCENE);
            }
            list_add(ACTION_NAME[A_CLEAR_SCENE], 0, LK_ACTION, A_CLEAR_SCENE);
        }
    list_head("more");
    list_add(ACTION_NAME[A_RANDOM_SOUNDS], 0, LK_ACTION, A_RANDOM_SOUNDS);
    list_add(ACTION_NAME[A_PANIC], 0, LK_ACTION, A_PANIC);
}
/* EDIT: the sound's actions (EDIT only: the other buttons' lists are their knobs) */
static void list_sound(int trk)
{
    static char slot_t[24], fol_t[24], kit_t[24], nm[NTRK][16], from_t[24], key_t[24];
    int sc = snd_clip ? snd_clip - 1 : -1, paste = sc >= 0 && sc != trk && sc < NTRK && is_drum(sc) == is_drum(trk);
    if (trk == AUX_BRAIN) {
        list_head("follow");
        for (int t = 0; t < NTRK; t++) {
            s_cat(num_s((uint32_t)t + 1u, s_cat(nm[t], "T")), " FOLLOWS");
            list_add(nm[t], FOLLOW_NAME[proj.trk[t].par[TP_BRAIN] % NBM], LK_PICK, A_FOLLOW);
        }
    } else if (is_drum(trk)) {                     /* the key's play mode, then the kit's */
        list_head("key");
        list_add(ACTION_NAME[A_PLAYMODE], PM_NAME[proj.trk[trk].par[TP_DRUM(ui.dsound % DL_NSOUND, DP_MODE)] % NPM],
                 LK_PICK, A_PLAYMODE);
        list_add(ACTION_NAME[A_COPY_KEY], DL_SOUND_NAME[ui.dsound % DL_NSOUND], LK_ACTION, A_COPY_KEY);
        if (key_clip && key_clip - 1 < NTRK && is_drum(key_clip - 1) &&
            (key_clip - 1 != trk || key_clip_k != ui.dsound % DL_NSOUND)) {
            char *e = s_cat(key_t, "from ");
            if (key_clip - 1 != trk)
                e = s_cat(num_s((uint32_t)key_clip, s_cat(e, "T")), " ");
            s_cat(e, DL_SOUND_NAME[key_clip_k % DL_NSOUND]);
            list_add(ACTION_NAME[A_PASTE_KEY], key_t, LK_ACTION, A_PASTE_KEY);
        }
        list_head("kit");
        kit_label(ui.pick_kit % NKITS, kit_t);
        list_add(ACTION_NAME[A_KIT], kit_t, LK_PICK, A_KIT);
        kit_label(NFKITS + ui.pick_kslot % NUSER_KITS, slot_t);
        list_add(ACTION_NAME[A_SAVE_KIT], slot_t, LK_PICK, A_SAVE_KIT);
        list_add(ACTION_NAME[A_SCRAMBLE], 0, LK_ACTION, A_SCRAMBLE);
        list_add(ACTION_NAME[A_COPY_SOUND], 0, LK_ACTION, A_COPY_SOUND);
        if (paste) {
            num_s((uint32_t)sc + 1u, s_cat(from_t, "from T"));
            list_add(ACTION_NAME[A_PASTE_SOUND], from_t, LK_ACTION, A_PASTE_SOUND);
        }
        list_add(ACTION_NAME[A_ENGINE], EG_NAME[ui.pick_engine % NENGINE], LK_PICK, A_ENGINE);
        list_midi_out(trk);
    } else if (!is_aux(trk)) {
        list_head("sound");
        if (NPRESETS > 0)
            list_add(ACTION_NAME[A_PRESET], PRESETS[ui.pick_preset % NPRESETS].name, LK_PICK, A_PRESET);
        list_add(ACTION_NAME[A_ENGINE], EG_NAME[ui.pick_engine % NENGINE], LK_PICK, A_ENGINE);
        list_add(ACTION_NAME[A_SCRAMBLE], 0, LK_ACTION, A_SCRAMBLE);
        num_s((uint32_t)ui.pick_slot % NUSER_PRESETS + 1u, s_cat(slot_t, "U"));
        list_add(ACTION_NAME[A_SAVE_PRESET], slot_t, LK_PICK, A_SAVE_PRESET);
        list_add(ACTION_NAME[A_INIT], 0, LK_ACTION, A_INIT);
        list_add(ACTION_NAME[A_COPY_SOUND], 0, LK_ACTION, A_COPY_SOUND);
        if (paste) {
            num_s((uint32_t)sc + 1u, s_cat(from_t, "from T"));
            list_add(ACTION_NAME[A_PASTE_SOUND], from_t, LK_ACTION, A_PASTE_SOUND);
        }
        s_cat(fol_t, FOLLOW_NAME[proj.trk[trk].par[TP_BRAIN] % NBM]);
        list_add(ACTION_NAME[A_FOLLOW], fol_t, LK_PICK, A_FOLLOW);
        if (is_midi(trk)) {                          /* its knobs' CCs (its notes always go out: no MIDI OUT) */
            list_head("MIDI");
            for (int k = 0; k < 8; k++) {
                static char v[8][4];
                num_s(proj.trk[trk].par[midi_cc_par(k)] % NMIDI_CC, v[k]);
                list_add(MIDI_CC_ROW[k], v[k], LK_PICK, A_MIDI_CC);
            }
        } else
            list_midi_out(trk);
    }
}
/* a slot's value pr past its page's knobs (a track's values par; none: "-"), its name in capitals: its list row */
static void list_slot_row(const uint8_t *par, int pr, char *nm, char *vt, int act)
{
    const tp_info_t *in = tp_info_par(pr, par);
    char num[16], unit[6];
    if (in->name[0] == '-')
        return;
    for (int c = 0; c < 11 && in->name[c]; c++)
        nm[c] = (char)(in->name[c] >= 'a' && in->name[c] <= 'z' ? in->name[c] - 32 : in->name[c]), nm[c + 1] = 0;
    tp_text_par(pr, par[pr], par, num, unit);
    vt[0] = 0;
    s_cat(unit[0] ? s_cat(s_cat(vt, num), unit[0] == '%' ? "" : " ") : s_cat(vt, num), unit);
    list_add(nm, vt, LK_PICK, act);
}
/* effect ef (the reverb in mode md) would find its memory in a slot page's slot, its own counted free (engine_fits:
 * PRESETS and the list offer only these) */
static int slot_fits(int pg, int ef, int md)
{
    int st, q = slot_page(pg, &st);
    const int send = st < 0 ? 2 : is_fx(st);
    if (ef == EF_DELAY)                          /* (a delay: what its default time needs at the tempo now) */
        return engine_fits(st, q, efx_delay_blocks(EFX[EF_DELAY].def[0], EFX[EF_DELAY].xdef[0], seq_tempo(&seq), 0, send));
    return engine_fits(st, q, efx_need(ef, md, send));
}
/* a slot page's list: the catalog in its groups (the one in use marked; "no memory", dimmed and refused, beside what
 * would find none where the slot is: slot_fits), the effect's extras, a send's filter and FX I's feed into FX II */
static void list_slot(int pg)
{
    static char nm[4][12], vt[4][16], hp[8], lp[8], fd[8];
    uint8_t mp[NTP];
    int st, q, g = EFG_NONE;
    const uint8_t *par = slot_vals(pg, mp, &st, &q);
    const int cur = par[TP_SLOT[q][SL_EF]] % NEF;
    for (int ef = 0; ef < NEF; ef++) {
        if (EFX[ef].group != g)
            list_head(EFG_NAME[g = EFX[ef].group]);
        list_add(EFX[ef].name, ef == cur ? "in use" : slot_fits(pg, ef, EFX[ef].xdef[0]) ? 0 : "no memory", LK_ACTION,
                 A_EFFECT);
    }
    if (efx_knob_name(cur, 3))
        list_head("extras");
    for (int r = 0; r < 4; r++)
        list_slot_row(par, TP_SLOT[q][SL_X1 + r], nm[r], vt[r], A_SLOT_X1 + r);
    if (st >= 0 && is_fx(st)) {
        par = proj.trk[st].par;
        list_head("send");
        num_s(par[TP_FENV], hp);
        num_s(par[TP_CUT], lp);
        list_add(ACTION_NAME[A_FX_HP], hp, LK_PICK, A_FX_HP);
        list_add(ACTION_NAME[A_FX_LP], lp, LK_PICK, A_FX_LP);
        if (st == AUX_FX1) {
            num_s(par[TP_SEND2], fd);
            list_add(ACTION_NAME[A_FX_SEND], fd, LK_PICK, A_FX_SEND);
        }
    }
}
/* GLO's pages but Setup: their knobs, page by page; Master out's settings with its knobs (Master FX's own list:
 * list_slot) */
static void list_mix(int g)
{
    static char by[8], rel[12], lev[8];
    for (int gi = 0; gi < group_n(g); gi++) {
        int p = grp_page(g, gi);
        if (p == P_SETUP || p == P_STATUS || fx_page(p))   /* (FX I and FX II: their own lists, the effects) */
            continue;
        list_page_section(p);
        if (p == P_MASTER3) {
            char *e = num_i((int32_t)(master_pump_ms(proj.master[MS_PUMP_REL]) + 0.5f), rel);
            s_cat(e, " ms");
            num_i(proj.master[MS_LEVEL], lev);
            list_add(ACTION_NAME[A_PUMP_BY], pump_by_text(proj.master[MS_PUMP_BY], by), LK_PICK, A_PUMP_BY);
            list_add(ACTION_NAME[A_PUMP_REL], rel, LK_PICK, A_PUMP_REL);
            list_add(ACTION_NAME[A_PUMP_ACC], proj.master[MS_PUMP_ACC] ? "on" : "off", LK_PICK, A_PUMP_ACC);
            list_add(ACTION_NAME[A_LEVEL], lev, LK_PICK, A_LEVEL);
        }
    }
}
/* GLO's Setup and Debug: the FM-1's own settings, Bluetooth first */
static void list_setup(int g)
{
    static char bmid_t[24], baud_t[24];
    list_add("VERSION " APP_VERSION, 0, LK_NOTE, 0);   /* (first, seen on opening: large type has no room for it on
                                                      * the page) */
    list_head("Bluetooth");
    list_add(ACTION_NAME[A_BT_MIDI], bt.midi ? "on" : "off", LK_PICK, A_BT_MIDI);   /* (two pairs: on / off, then
                                                                                  * where devices are chosen) */
    bt_dev_text(bmid_t, "choose", "BT MIDI off");
    list_add(ACTION_NAME[A_BT_DEVICE], bmid_t, LK_ACTION, A_BT_DEVICE);
    list_add(ACTION_NAME[A_BT_AUDIO], bt.want ? "on" : "off", LK_PICK, A_BT_AUDIO);
    bt_adev_text(baud_t);
    list_add(ACTION_NAME[A_BT_ADEV], baud_t, LK_ACTION, A_BT_ADEV);
    list_head("USB and MIDI");
    list_add(ACTION_NAME[A_USB_AUDIO], USBA_NAME[proj.usb_full < 3u ? proj.usb_full : 0u], LK_PICK, A_USB_AUDIO);
#if FM1B_USB_HOST
    list_add(ACTION_NAME[A_USB_HOST], plat_usb_host_state() ? "MIDI host" : "computer", LK_PICK, A_USB_HOST);
#endif
    list_page_knobs(P_SETUP, 6u);                    /* CLOCK, NOTES */
    list_add(ACTION_NAME[A_CC_IN], proj.rsv[3] & CC_IN_ENDLESS ? "endless" : "absolute", LK_PICK, A_CC_IN);
    list_add(ACTION_NAME[A_CC_OUT], proj.rsv[2] & CC_OUT_OFF ? "off" : "on", LK_PICK, A_CC_OUT);
    list_add(ACTION_NAME[A_SCENES_OUT], proj.rsv[2] & SCENES_OUT ? "on" : "off", LK_PICK, A_SCENES_OUT);
    list_head("LOOM-8");
    list_page_knobs(P_SETUP, 9u);                    /* LIGHTS, THEME */
    list_add(ACTION_NAME[A_LARGE_TYPE], !large_type() ? "off" : lt_ok ? "on" : "on: reinstall", LK_PICK, A_LARGE_TYPE);
    list_add(ACTION_NAME[A_AUTOSAVE], proj.rsv[2] & AUTOSAVE_OFF ? "off" : "on", LK_PICK, A_AUTOSAVE);
    list_add(ACTION_NAME[A_LOOP_FREE], proj.rsv[2] & LOOP_FREE ? "drift" : "in time", LK_PICK, A_LOOP_FREE);
    list_add(ACTION_NAME[A_MOD_VIEW], proj.rsv[3] & MOD_VIEW_OFF ? "off" : "on", LK_PICK, A_MOD_VIEW);
    {                                                /* the CPU's clock from the next start (project.c app_cpu_396): as
                                                      * chosen, "(restart)" while this start runs at the other */
        static const char *const CPU_TEXT[4] = {"360 MHz", "396 MHz", "360 MHz (restart)", "396 MHz (restart)"};
        const int want = (proj.rsv[3] & CPU_396) != 0;
        list_add(ACTION_NAME[A_CPU], CPU_TEXT[want | (want != plat_cpu_396()) << 1], LK_PICK, A_CPU);
    }
    if (group_n(g) > 6)
        list_page_knobs(P_STATUS, 15u);
    list_add(ACTION_NAME[A_PANIC], 0, LK_ACTION, A_PANIC);
}
static int list_is_setup(int p) { return p == P_SETUP || p == P_STATUS; }
static void list_build(void)
{
    int keep = ui.page, g = page_group(keep), trk = sel_trk(), b = PAGE_BTN[g].btn, knobs = 1, heads, i, j;
    static char rm[NDRUM][16], keep_t[12];
    lst.n = 0;
    if (lst.bt) {
        if (lst.bt == 1)
            list_bt_audio();
        else
            list_bt_dev();
        if (lst.sel >= lst.n)
            lst.sel = (uint8_t)(lst.n ? lst.n - 1 : 0);
        if (lst.row[lst.sel].kind == LK_NOTE && lst.n > 1)
            lst.sel = 1;                             /* (the note is never picked) */
        return;
    }
    if (steps_page(keep) && ui.held && is_seqt(trk)) {   /* a step held + SEL: its notes, to remove */
        const step_t *st = &tpat(trk)->st[step_shown(ui.held_first)];
        if (is_drum(trk)) {                           /* each sound on the step, by key */
            for (int i = 1, r = 0; i <= NDRUM; i++)
                if (kit_has(st, i)) {
                    s_cat(s_cat(rm[r], "REMOVE "), DL_SOUND_NAME[i - 1]);
                    list_add(rm[r++], 0, LK_ACTION, A_REMOVE_NOTE);
                }
            list_clip_step();
            return;
        }
        for (int i = 0; i < SN && st->note[i]; i++) {
            char *e = s_cat(rm[i], "REMOVE ");
            if (is_drum(trk))
                s_cat(e, DL_SOUND_NAME[(st->note[i] - 1) % DL_NSOUND]);
            else if (trk == AUX_PUNCH)
                s_cat(e, PX_VALID(st->note[i] - 1) ? PUNCH_NAME[(st->note[i] - 1) & 15] : "fx");
            else if (pl_type(trk) == PL_RAND)
                s_cat(e, "slot");
            else
                note_name(st->note[i], e);
            list_add(rm[i], 0, LK_ACTION, A_REMOVE_NOTE);
        }
        list_clip_step();
        return;
    }
    if (keep == P_MOD) {
        list_mod();
        knobs = 0;
    } else if (keep == P_STEPS && trk == AUX_WEAVE) {     /* WEAVE: its pattern cleared */
        list_add(ACTION_NAME[A_CLEAR_NOTES], 0, LK_ACTION, A_CLEAR_NOTES);
    } else if (steps_page(keep)) {
        if (is_seqt(trk))
            list_steps(trk);
    } else if (keep == P_WEAVE) {
        if (wv_from(trk)) {
            int to = pat_first_empty(trk);
            if (to >= 0)
                num_s((uint32_t)to + 1u, s_cat(keep_t, "P"));
            else
                s_cat(keep_t, "none empty");
            list_add(ACTION_NAME[A_WEAVE], keep_t, LK_ACTION, A_WEAVE);
        }
    } else if (keep == P_ARRANGE || keep == P_SONG) {
        list_home(keep);
    } else if (slot_pg(keep) >= 0 || (b == B_EDIT && is_fx(trk))) {
        list_slot(slot_pg(keep) >= 0 ? keep : fx_pg(trk));
        knobs = 0;
    } else if ((keep == P_ENV || keep == P_FILTER || keep == P_ENV2 || keep == P_KITF) && !is_aux(trk)) {
        int fq = ftype_param(trk, keep);
        const tp_info_t *fi = tp_info_par(fq, proj.trk[trk].par);
        if (is_drum(trk)) {                          /* the key's filter and the kit's bus (on: dynamic, or off), on
                                                      * all four pages */
            fq = TP_DRUM(ui.dsound % DL_NSOUND, DP_FTYPE);
            fi = tp_info_par(fq, proj.trk[trk].par);
            list_add("KEY FILTER", fi->names[proj.trk[trk].par[fq] % (fi->hi + 1u)], LK_PICK, A_FTYPE);
            list_add(ACTION_NAME[A_KIT_FILTER], proj.trk[trk].par[TP_FTYPE] == FT_DYNAMIC ? "on" : "off", LK_PICK,
                     A_KIT_FILTER);
        } else
            list_add(ACTION_NAME[A_FTYPE], fi->names[proj.trk[trk].par[fq] % (fi->hi + 1u)], LK_PICK, A_FTYPE);
    } else if (b == B_EDIT) {
        list_sound(trk);
    } else if (b == B_GLO) {
        list_projects();
        if (list_is_setup(keep))
            list_setup(g);
        else
            list_mix(g);
        knobs = 0;
    }
    if (knobs) {                                     /* the button's knobs, page by page (FX: the page's own) */
        if (b == B_FX) {
            if (keep == P_SENDS) {                   /* (the two effects the sends feed: their pages, GLO's) */
                list_add(ACTION_NAME[A_GO_FX1], fx_label(AUX_FX1), LK_ACTION, A_GO_FX1);
                list_add(ACTION_NAME[A_GO_FX2], fx_label(AUX_FX2), LK_ACTION, A_GO_FX2);
            }
            list_page_section(keep);
        }
        else {
            int kmod = 0;
            for (int gi = 0; gi < group_n(g); gi++) {
                list_page_section(grp_page(g, gi));
                kmod |= grp_page(g, gi) == P_KMOD;
            }
            if (kmod && is_knit(trk)) {          /* knit's extras (the Prophet's: a synced patch's master quiet, the
                                                  * sweep on the pulse's width): no knob of their own */
                static char lv[8];
                const uint8_t *par = proj.trk[trk].par;
                s_cat(num_s((uint32_t)((127 - par[TP_K1CUT] % 128) * 100 + 63) / 127u, lv), "%");
                list_head("extras");
                list_add(ACTION_NAME[A_K1LEVEL], lv, LK_PICK, A_K1LEVEL);
                list_add(ACTION_NAME[A_KSWTO], par[TP_KSWTO] ? "osc 1 width" : "osc 2 pitch", LK_PICK, A_KSWTO);
            }
        }
    }
    for (i = j = 0; i < lst.n; i++)
        j += lst.row[i].kind != LK_HEAD;
    heads = j > LIST_SHOWN || keep == P_MOD;         /* the sections' names only where the list is longer than the
                                                      * screen (and none with nothing under it); the MOD page's always
                                                      * (its "this lane" names the lane picked) */
    for (i = j = 0; i < lst.n; i++)
        if (lst.row[i].kind != LK_HEAD || (heads && i + 1 < lst.n && lst.row[i + 1].kind != LK_HEAD))
            lst.row[j++] = lst.row[i];
    lst.n = (uint8_t)j;
    if (lst.sel >= lst.n)
        lst.sel = (uint8_t)(lst.n ? lst.n - 1 : 0);
}
static void glyph_enter(int32_t x, int32_t y, uint16_t c)
{
    cv_rect(x + 8, y, 2, 7, c);
    cv_rect(x + 2, y + 5, 8, 2, c);
    cv_rect(x + 2, y + 3, 1, 6, c);
    cv_rect(x + 1, y + 4, 1, 4, c);
    cv_rect(x, y + 5, 1, 2, c);
}
static void glyph_pick(int32_t x, int32_t y, uint16_t c)
{
    for (int j = 0; j < 4; j++)
        cv_rect(x + j, y + j, 1, 9 - 2 * j, c);
}
#define LIST_Y HDR_H
#define LIST_H (FOOT_Y - LIST_Y)
#define LIST_VIS (LARGE ? LIST_H : LIST_SHOWN * LIST_RH)
static int list_rh(int i) { return lst.row[i].kind == LK_HEAD ? LIST_HH : LIST_RH; }
static int list_span(int a, int b)                /* rows a..b's height */
{
    int h = 0;
    for (int i = a; i <= b && i < lst.n; i++)
        h += list_rh(i);
    return h;
}
/* the row picked in view, with its section's name when it is the first of a section; open: from its section's
 * name, the list filling the screen (as far up as there is room) */
static void list_scroll(int open)
{
    int s = lst.sel, t = lst.top;
    if (open || t > s)
        t = s;
    if (t == s && s && lst.row[s - 1].kind == LK_HEAD)
        t--;
    if (open)
        while (t > 0 && list_span(t - 1, lst.n - 1) <= LIST_VIS)
            t--;
    while (t < s && list_span(t, s) > LIST_VIS)
        t++;
    lst.top = (uint8_t)t;
}
static void list_paint(int32_t oy)
{
    int32_t y = 1 - oy;
    for (int i = lst.top; i < lst.n && y + oy - 1 + list_rh(i) <= LIST_VIS; y += list_rh(i), i++) {
        int32_t ty = cap_y(y, LIST_RH - 1, &FONT_S) - 1;   /* (descenders clear the foot) */
        int on = i == lst.sel, k = lst.row[i].kind;
        const char *v = lst.row[i].val;
        const int no = k == LK_ACTION && lst.row[i].id == A_EFFECT && v[0] == 'n';   /* (an effect with no memory there:
                                                  * dimmed, SEL refuses it) */
        uint16_t fg = on ? K_BG : no ? K_DIM : K_TEXT;
        if (k == LK_HEAD) {                          /* a section's name: small, a rule after it */
            int32_t e = cv_text(10, cap_y(y, LIST_HH, &FONT_XS) - 1, &FONT_XS, lst.row[i].name, K_DIM);
            if (e + 6 < 226)
                cv_rect(e + 6, y + LIST_HH / 2, 226 - (e + 6), 1, K_FAINT);
            continue;
        }
        if (k == LK_NOTE) {
            cv_text(14, ty, &FONT_S, lst.row[i].name, K_DIM);
            continue;
        }
        cv_round(6, y, 222, LIST_RH - 1, 5, on ? K_TEXT : K_PANEL);
        {   /* every row alike: its name regular, in capitals; its value bold; at the right ↵ where SEL carries it out
             * (an action, a choice), ▸ where ALGORITHM changes it at once and SEL only leaves (a setting, a knob) */
            char nm[24];
            uint32_t j;
            int now = k == LK_KNOB || (k == LK_PICK && pick_sets(lst.row[i].id));
            int32_t ne, room, w;
            for (j = 0; lst.row[i].name[j] && j < sizeof nm - 1u; j++)
                nm[j] = (char)(lst.row[i].name[j] >= 'a' && lst.row[i].name[j] <= 'z' ? lst.row[i].name[j] - 32
                                                                                      : lst.row[i].name[j]);
            nm[j] = 0;
            ne = cv_text(14, ty, &FONT_S, nm, fg);
            room = 202 - (ne + 6);
            if (*v && room > 8) {                    /* (too long bold: regular, then 15 px, then cut) */
                const felucca_font_t *vf = text_w(&FONT_B, v) <= room ? &FONT_B : text_w(&FONT_S, v) <= room ? &FONT_S
                                         : text_w(&LFONT_S, v) <= room ? &LFONT_S : &LFONT_XS;
                w = text_w(vf, v);
                text_fit(202 - (w < room ? w : room), vf != &FONT_B && vf != &FONT_S ? cap_y(y, LIST_RH - 1, vf) - 1
                                                      : ty - (LARGE && vf == &FONT_B),   /* (large: its descenders clear) */
                         vf, v, room, fg);
            }
            if (now)
                glyph_pick(213, ty + 3, fg);
            else if (!no)
                glyph_enter(208, ty + 3, fg);
        }
    }
    {
        int32_t all = list_span(0, lst.n - 1);
        if (all > LIST_VIS) {
            int32_t th = LIST_VIS - 2, bh = th * LIST_VIS / all, by = lst.top ? th * list_span(0, lst.top - 1) / all : 0;
            cv_round(232, 1 - oy, 4, th, 2, K_PANEL);
            cv_round(232, 1 + by - oy, 4, bh, 2, K_FAINT);
        }
    }
}
static void draw_list(void)
{
    uint32_t h = hash_u(hash_u(2166136261u, 0x4Cu), lst.sel | (uint32_t)lst.top << 8), i;
    list_build();
    h = hash_u(h, lst.n);
    for (i = 0; i < lst.n; i++) {
        h = hash_u(h, lst.row[i].kind | (uint32_t)lst.row[i].id << 4);
        h = hash_s(h, lst.row[i].name);
        h = hash_s(h, lst.row[i].val);
    }
    ui.sig[2] = ui.sig[3] = ui.sig[5] = 0;
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;
    {                                            /* in bands of the canvas's rows at most, split between rows */
        int32_t y0 = 0, i = lst.top;
        while (y0 < LIST_H) {
            int32_t hb = y0 ? 0 : 1, hb0 = hb;
            while (i < lst.n && hb + list_rh(i) <= CV_ROWS && y0 + hb + list_rh(i) <= LIST_VIS + 1)
                hb += list_rh(i++);
            if (hb == hb0)                       /* (no more rows here: the rest, plain) */
                hb = LIST_H - y0 < CV_ROWS ? LIST_H - y0 : CV_ROWS;
            cv_begin(240, (uint32_t)hb, K_BG);
            list_paint(y0);
            cv_blit(0, (uint32_t)(LIST_Y + y0));
            y0 += hb;
        }
    }
}

/* ------------------------------------------------------- the pages --- */
/* the main band in strips (strip_begin): draw_page calls the page once a strip, main_r0 the strip's first row. The
 * first call decides (its hash), the later ones draw on: a page's own state moves on its first call only (main_r0 0) */
static int32_t main_r0;
static uint8_t main_drawn;
static int begin_main(uint32_t h)
{
    if (!main_r0) {
        if (h == ui.sig[1])
            return 0;
        ui.sig[1] = h;
        ui.sig[5] = 0;                               /* (the step record strip over it: drawn again) */
        main_drawn = 1;
    }
    strip_begin(MAIN_H, main_r0, K_BG);
    return 1;
}
static void main_blit(void) { cv_blit(0, (uint32_t)(MAIN_Y + main_r0)); }
static uint32_t hash_pat(uint32_t h, const tpat_t *p)
{
    const uint32_t *w = (const uint32_t *)(const void *)p;
    for (uint32_t i = 0; i < sizeof *p / 4u; i++)
        h = hash_u(h, w[i]);
    return h;
}

/* Arrange: eleven rows (the tracks), each its pattern and a playhead, mutes dimmed; the scene big at the right */
#define AR_MH 7                                      /* Arrange's track meters: pixels tall */
/* Arrange in large type: the ten sequenced tracks in two rows of five cells, each its number on its ink (faint:
 * muted), its pattern, "mute" or the sounds the scene mutes, its meter at the right, and at the foot its pattern's
 * bar (the loop bright, the place playing) */
static void arrange_large(const int *mh, const int *amh)
{
    for (int tr = 0; tr < NSEQT; tr++) {
        int32_t cw = 44, ch = 38, x = 2 + (tr % 5) * (cw + 4), y = 2 + (tr / 5) * (ch + 4);
        int muted = seq.mute >> tr & 1u, pt = seq.ppat[tr] % NPATT, len = tpat(tr)->len, pos = eng_step[tr], lf, le;
        int m = tr < NTRK ? mh[tr] : amh[tr - NTRK];
        uint16_t ink = muted ? K_FAINT : trk_ink(tr);
        char nm[8], t[12];
        const felucca_font_t *pf;
        cv_round(x, y, cw, ch, 4, K_PANEL);
        if (tr == sel_trk())
            cv_frame(x, y, cw, ch, 4, K_SEL, K_PANEL);
        nm[0] = 0;
        if (tr < NTRK)
            num_s((uint32_t)tr + 1u, nm);
        else
            s_cat(nm, tr == AUX_BRAIN ? "CO" : "TH");
        cv_round(x + 3, y + 3, 20, 17, 4, ink);
        text_c(x + 13, cap_y(y + 3, 17, &LFONT_S), &LFONT_S, nm, K_BG);
        num_s((uint32_t)pt + 1u, s_cat(t, "P"));
        pf = text_w(&FONT_XS, t) <= cw - 28 ? &FONT_XS : &LFONT_S;
        text_c(x + 33, cap_y(y + 3, 17, pf), pf, t, muted ? K_FAINT : K_TEXT);
        if (muted)
            cv_text(x + 4, y + 20, &LFONT_XS, "mute", K_DIM);
        else if (tr < NTRK && kit_slot(tr) >= 0 && seq.kmute[kit_slot(tr)]) {
            s_cat(num_s((uint32_t)__builtin_popcount(seq.kmute[kit_slot(tr)]), t), " off");
            cv_text(x + 4, y + 20, &LFONT_XS, t, K_DIM);
        }
        if (m > 0)                                   /* its meter: a bar up the cell's right, under the pattern */
            cv_rect(x + cw - 6, y + 32 - 10 * m / AR_MH, 3, 10 * m / AR_MH, ink);
        pat_loop(tpat(tr), &lf, &le);
        if (len > 0 && len <= NSTEP) {                /* the pattern's bar: its length of 64 steps, the loop bright */
            int32_t bw = (cw - 8) * len / NSTEP, bx = x + 4, by = y + ch - 5;
            cv_rect(bx, by, cw - 8, 2, K_PANEL2);
            cv_rect(bx, by, bw, 2, K_LINE);
            cv_rect(bx + bw * lf / len, by, bw * le / len - bw * lf / len, 2, muted ? K_FAINT : trk_tint(tr));
            if (playing() && !muted)
                cv_round(bx + (lf == 0 && le == len ? (cw - 10) * (pos % len) / (len > 1 ? len - 1 : 1)
                                                    : (bw - 3) * (pos % len) / (len > 1 ? len - 1 : 1)),
                         by - 2, 3, 6, 1, trk_ink(tr));
        }
    }
}
static void draw_arrange(void)
{
    static float a_db[NTRK];
    static uint32_t a_last;
    int sc = cur_scene(), cue = seq.cue, left = seq_cue_left(&seq), mh[NTRK];
    uint32_t now = plat_ms(), dt = now - a_last;
    uint32_t h = hash_u(hash_u(2166136261u, 0xA0u), (uint32_t)sc | (uint32_t)cue << 8 | (uint32_t)playing() << 16 |
                                          (uint32_t)sel_trk() << 20 | (uint32_t)seq.cue_end << 24 |
                                          (uint32_t)ui.scene_pg << 25);
    h = hash_u(h, (uint32_t)(left + 1));
    for (int k = 0; k < NKITM; k++)
        h = hash_u(h, seq.kmute[k]);
    a_last = now;
    for (int t = 0; t < NTRK; t++) {             /* each track's meter: what it plays now (Mix's fall; the header's
                                                  * range, -45 to -9 dB: activity more than level) */
        float db = lin_db((float)ui_peak[t] * (1.0f / 16383.5f));
        a_db[t] = dt > 250u ? db : fm_maxf(db, a_db[t] - (float)dt * 0.024f);
        mh[t] = (int)fm_clampf((a_db[t] + 45.0f) * (AR_MH / 36.0f) + 0.5f, 0.0f, (float)AR_MH);
        h = hash_u(h, (uint32_t)mh[t]);
    }
    int amh[2];                                   /* COND and THROW make no sound: a step of theirs playing lights it */
    for (int a = 0; a < 2; a++) {
        const tpat_t *tp = tpat(NTRK + a);
        amh[a] = playing() && !step_empty(&tp->st[eng_step[NTRK + a] % NSTEP]) ? AR_MH : 0;
        h = hash_u(h, (uint32_t)amh[a] << 4 | (uint32_t)a);
    }
    char t[16];
    for (int tr = 0; tr < NSEQT; tr++)
        h = hash_u(h, (uint32_t)seq.ppat[tr] | (uint32_t)eng_step[tr] << 8 | (uint32_t)(seq.mute >> tr & 1u) << 16 |
                          (uint32_t)tpat(tr)->len << 20 | (uint32_t)tpat(tr)->lstart << 24 | (uint32_t)tpat(tr)->lend << 16);
    for (int i = 0; i < NSCENE; i++)
        h = hash_u(h, pats.scene[i].used);
    h = hash_u(h, (uint32_t)pats.scene_mod[sc] | (uint32_t)seq.mod_pos << 8 | (uint32_t)seq.mod_pat << 16 |
                      (uint32_t)mod_sel() << 24 | (uint32_t)(seq.mute >> MOD_BIT & 1u) << 25);
    if (!begin_main(h))
        return;
    if (LARGE) {                                     /* large type: the tracks as cells (the scenes: the tag) */
        arrange_large(mh, amh);
        main_blit();
        return;
    }
    for (int tr = 0; tr < NSEQT; tr++) {
        int32_t y = 2 + tr * 10 + (tr >= NTRK ? 4 : 0);
        int muted = seq.mute >> tr & 1u, pt = seq.ppat[tr] % NPATT, len = tpat(tr)->len, pos = eng_step[tr];
        uint16_t ink = muted ? K_FAINT : K_DIM;
        char nm[8];
        if (tr < NTRK)
            num_s((uint32_t)tr + 1u, nm);
        else
            s_cat(nm, tr == AUX_BRAIN ? "CO" : tr == AUX_PUNCH ? "TH" : "TP");
        text_r(22, y - 3, &FONT_XS, nm, tr == sel_trk() && !mod_sel() ? K_TEXT : ink);
        if (tr < NTRK && mh[tr] > 0)                 /* (its meter, at the left of its number) */
            cv_rect(4, y + 5 - mh[tr], 3, mh[tr], muted ? K_FAINT : trk_ink(tr));
        else if (tr >= NTRK && amh[tr - NTRK] > 0)
            cv_rect(4, y + 5 - AR_MH, 3, AR_MH, muted ? K_FAINT : trk_ink(tr));
        num_s((uint32_t)pt + 1u, s_cat(t, "P"));
        text_c(37, y - 3, &FONT_XS, t, muted ? K_FAINT : trk_ink(tr));
        cv_rect(52, y + 3, 88, 2, K_PANEL2);         /* (88 px: "mute" fits before the rule at 168) */
        int lf, le, bw = 88 * len / NSTEP;
        pat_loop(tpat(tr), &lf, &le);
        if (len > 0 && len <= NSTEP) {
            if (lf == 0 && le == len)
                cv_rect(52, y + 3, bw, 2, muted ? K_LINE : trk_tint(tr));
            else {                                   /* a cropped loop: bright inside, the rest of the pattern dim */
                cv_rect(52, y + 3, bw, 2, K_LINE);
                cv_rect(52 + bw * lf / len, y + 2, bw * le / len - bw * lf / len, 4, muted ? K_FAINT : trk_tint(tr));
            }
        }
        if (playing() && !muted && len > 0) {
            if (lf == 0 && le == len)
                cv_round(52 + 86 * (pos % len) / (len > 1 ? len - 1 : 1), y + 1, 3, 6, 1, trk_ink(tr));
            else                                     /* (inside the loop, repeating) */
                cv_round(52 + (bw - 3) * (pos % len) / (len > 1 ? len - 1 : 1), y + 1, 3, 6, 1, trk_ink(tr));
        }
        if (muted)
            cv_text(144, y - 3, &FONT_XS, "mute", K_DIM);
        else if (tr < NTRK && kit_slot(tr) >= 0 && seq.kmute[kit_slot(tr)]) {   /* its sounds the scene mutes: how many */
            char ko[12];
            s_cat(num_s((uint32_t)__builtin_popcount(seq.kmute[kit_slot(tr)]), ko), " off");
            cv_text(144, y - 3, &FONT_XS, ko, K_DIM);
        }
    }
    {                                                /* the MOD track: the scene's MOD pattern ("-" none), its step */
        const int32_t y = 2 + NSEQT * 10 + 4;
        const int muted = seq.mute >> MOD_BIT & 1u, m = pats.scene_mod[sc] % (NPATT + 1);
        const int ml = m ? pats.mod[m - 1].len : 0;
        text_r(22, y - 3, &FONT_XS, "MD", mod_sel() ? K_TEXT : muted ? K_FAINT : K_DIM);
        if (m) {                                     /* (the whole pattern across the row, as a track's) */
            num_s((uint32_t)m, s_cat(t, "P"));
            text_c(37, y - 3, &FONT_XS, t, muted ? K_FAINT : theme->aux);
            cv_rect(52, y + 3, 88, 2, muted ? K_LINE : K_DIM);
            if (playing() && !muted && seq.mod_pat == m - 1 && ml > 0)
                cv_round(52 + 86 * (seq.mod_pos % ml) / (ml > 1 ? ml - 1 : 1), y + 1, 3, 6, 1, theme->aux);
        } else
            text_c(37, y - 3, &FONT_XS, "-", K_FAINT);
        if (muted)
            cv_text(144, y - 3, &FONT_XS, "mute", K_DIM);
    }
    cv_rect(168, 4, 1, 112, K_LINE);
    text_c(204, 8, &FONT_XS, cue == 0xFF || cue == sc ? "scene" : seq.cue_end ? "at the end" : "next bar", K_DIM);
    num_s((uint32_t)sc + 1u, t);
    text_c(204, 24, &FONT_L, t, playing() ? K_NOW : K_TEXT);
    if (cue != 0xFF && cue != sc) {
        num_s((uint32_t)cue + 1u, t);
        text_c(204, 62, &FONT_B, t, K_TEXT);
        if (left >= 0) {                         /* when it comes: the beats left, a bar counting down */
            int span = seq.cue_end ? (int)seq.bar_len : 16, beats = (left + 4) / 4;
            s_cat(num_s((uint32_t)beats, s_cat(t, "in ")), beats == 1 ? " beat" : " beats");
            text_c(204, 74, &FONT_XS, t, K_DIM);
            cv_rect(180, 87, 48, 2, K_PANEL2);
            cv_rect(180, 87, 48 * (left + 1) / (span > 0 ? span : 16), 2, K_NOW);
        }
    }
    for (int i = 0; i < NSCENE; i++) {               /* the 32 scenes, the white keys' page marked at its left */
        int32_t x = 176 + (i % 8) * 7, y = 92 + (i / 8) * 7;
        cv_round(x, y, 5, 5, 1, pats.scene[i].used ? (i == sc ? K_NOW : K_DIM) : K_LINE);
        if (i == cue && cue != sc)
            cv_frame(x, y, 5, 5, 1, K_TEXT, K_BG);
    }
    cv_rect(172, 92 + ui.scene_pg * 14, 2, 12, K_DIM);
    main_blit();
}

static void draw_song(void)
{
    const song_t *sg = &pats.song;
    int cur = ui.name_slot <= sg->len ? ui.name_slot : sg->len, top = (cur / 16) * 16, pp = seq.song_pos;
    uint32_t h = hash_u(hash_u(2166136261u, 0x50u), (uint32_t)cur | (uint32_t)sg->len << 8 | (uint32_t)sg->loop << 16 |
                                          (uint32_t)(seq.song_on && playing() ? pp + 1 : 0) << 20);
    char t[16];
    int32_t fill = 0;                                /* the playing slot's way through, 0..48 px: its bars and the bar's sixteenths */
    if (seq.song_on && playing() && pp < sg->len) {
        int reps = sg->slot[pp].rep ? sg->slot[pp].rep : 1, left = seq.song_rep ? seq.song_rep : 1;
        int32_t six = (int32_t)(reps - (left > reps ? reps : left)) * 16 + (int32_t)(seq.bar16 & 15u);
        fill = six * 48 / (reps * 16);
    }
    h = hash_u(h, (uint32_t)fill | (uint32_t)ui.scene_pg << 16);
    for (int i = top; i < top + 16 && i < sg->len; i++)
        h = hash_u(h, sg->slot[i].scene | (uint32_t)sg->slot[i].rep << 8);
    if (!begin_main(h))
        return;
    if (!sg->len) {
        text_c(120, LY(40, 18), &FONT_B, "No scenes in the song yet", K_TEXT);
        text_c(120, LY(62, 48), LY(&FONT_S, F_CAP), ui.scene_pg ? "Press a white key to add scene 17-32" : "Press a white key to add scene 1-16", K_DIM);
        main_blit();
        return;
    }
    for (int i = top; i < top + 16 && i <= sg->len; i++) {
        int32_t x = 8 + (i - top) % 4 * 57, y = 6 + (i - top) / 4 * 27;
        int on = seq.song_on && playing() && i == pp;
        if (i == sg->len) {
            cv_frame(x, y, 52, 22, 5, i == cur ? K_SEL : K_LINE, K_BG);
            text_c(x + 26, y + 3, &FONT_XS, sg->loop ? "loop" : "end", K_DIM);
            continue;
        }
        cv_round(x, y, 52, 22, 5, on || i == cur ? (i == cur ? K_SEL : K_NOW) : K_PANEL);
        if (on || i == cur)                          /* (the frame: the playing one's ink, the picked one's) */
            cv_round(x + 2, y + 2, 48, 18, 4, K_PANEL);
        if (on) {                                    /* playing: a bar along its foot fills as it plays, to the step */
            cv_rect(x + 6, y + 17, 40, 3, K_PANEL2);
            if (fill > 0)
                cv_rect(x + 6, y + 17, fill * 40 / 48, 3, K_NOW);
        }
        num_s((uint32_t)sg->slot[i].scene + 1u, s_cat(t, "S"));
        cv_text(x + 6, y + 3, &FONT_B, t, K_TEXT);
        num_s((uint32_t)sg->slot[i].rep, t);         /* its bars (the BARS knob names them) */
        text_r(x + 47, y + 4, &FONT_XS, t, on ? K_TEXT : K_DIM);
    }
    if (sg->len >= 16) {
        num_s((uint32_t)sg->len, s_cat(num_s((uint32_t)top + 1u, s_cat(t, "slots ")), " to "));
        text_r(232, 112, &FONT_XS, t, K_DIM);
    }
    main_blit();
}

/* Steps: what a press writes and the bars' squares, then the bar's 16 cells */
#define ST_X 4
#define ST_CW 29
/* large type: the sound and the bars on the tag (tag_steps), the cells shorter and their rows closer (today's faces) */
#define ST_Y (LARGE ? 2 : 22)       /* (large type: 2 px under the header) */
#define ST_CH 46
#define ST_TW (ST_CW - 8)   /* a cell's line: at most this wide, clear of a held cell's two-pixel frame */
#define ST_MID ((ST_CW - 2) / 2)   /* a line's centre */
#define ST_NUM 0            /* the rows' text y in a cell (ink 3-11, 13-21, 23-31, 33-41; large type: no number, the
                             * lines at 15 px from 4 and 17, the marks from 31) */
#define ST_L1 (LARGE ? 11 : 10)
#define ST_L2 (LARGE ? 23 : 20)
#define ST_MK 30
#define ST_TICK (LARGE ? 29 : 26)  /* a kit's ticks' row */
/* a cell's line: 15 px in large type when it fits the cell (25 px: no number now), else today's */
static const felucca_font_t *st_font(const char *s) { return LARGE && text_w(&LFONT_S, s) <= 25 ? &LFONT_S : &FONT_XS; }
/* a cell's text: its notes or chord, the shown drum sound, a throw track's effect (draw_steps) */
static int step_on(const step_t *st, int trk)       /* the cell is "on" for the sound shown (drums) or any note */
{
    if (is_drum(trk))
        return kit_has(st, ui.dsound + 1);
    return !step_empty(st);
}
/* a chord from its notes (3 or more): its shape (CHORD_SHAPE's, +1; 0 = none) and its root's pitch class. The root is
 * a note whose intervals to the others match a shape (the lowest tried first: C E G A is C6, A C E G Am7). A cell
 * names it in full when that fits ("C", "Am", "G7", "Dm7", "Bdim7"), else by the short form ("CM7", "C+", "Co",
 * "Ds2"): draw_steps; the chord editor and the strum player name it in full (chord_name) */
static const struct { uint16_t mask; const char *suf, *sh; } CHORD_SHAPE[] = {
    {0x091, "", ""}, {0x089, "m", "m"}, {0x491, "7", "7"}, {0x891, "maj7", "M7"}, {0x489, "m7", "m7"},
    {0x049, "dim", "o"}, {0x111, "aug", "+"}, {0x085, "sus2", "s2"}, {0x0A1, "sus4", "s4"},
    {0x249, "dim7", "o7"}, {0x449, "m7b5", "m7b5"}, {0x291, "6", "6"}, {0x289, "m6", "m6"}, {0x095, "add9", "add9"},
    {0x08D, "madd9", "madd9"}, {0x4A1, "7sus4", "7s4"}, {0x889, "mMaj7", "mM7"}};
static int chord_shape(const uint8_t *notes, int n, int *root)
{
    uint16_t pcs = 0;
    int lo = 0;
    if (n < 3)
        return 0;
    for (int i = 0; i < n; i++) {
        pcs |= (uint16_t)(1u << (notes[i] % 12));
        if (notes[i] < notes[lo])
            lo = i;
    }
    for (int pass = 0; pass < 2; pass++)              /* the bass note as the root first, then the others */
        for (int i = 0; i < n; i++) {
            int r = notes[pass ? i : lo] % 12;
            uint16_t rot = (uint16_t)(((pcs >> r) | (pcs << (12 - r))) & 0xFFFu);
            for (uint32_t k = 0; k < sizeof CHORD_SHAPE / sizeof CHORD_SHAPE[0]; k++)
                if (rot == CHORD_SHAPE[k].mask) {
                    *root = r;
                    return (int)k + 1;
                }
            if (!pass)
                break;
        }
    return 0;
}
/* a chord's name ("Cm7", "Bdim7") after "chord: " and before its notes, or nothing when its notes make no shape */
static char *chord_name(const uint8_t *notes, int n, char *e)
{
    int root = 0, sh = chord_shape(notes, n, &root);
    return sh ? s_cat(s_cat(s_cat(e, NOTE_NAME[root]), CHORD_SHAPE[sh - 1].suf), "  ") : e;
}
static void cell_note(int n, char *b)               /* a note's name in a cell; the octave -1's "C#-1" too wide: "C#" */
{
    note_name(n, b);
    if (text_w(&FONT_XS, b) > ST_TW)
        b[b[1] == '#' || b[1] == 'b' ? 2 : 1] = 0;
}
/* how many steps a step's note covers (its length, rounded up; the pattern's note length is under a step) */
static int step_cover(const step_t *st) { return st->len ? (step_len_q(st->len) + 3) / 4 : 1; }
/* WEAVE's Steps: the bar of its pattern, each step the tracks it weaves (names; more: "+n") and how long (the bar) */
static void draw_weave_steps(void)
{
    const wvpat_t *wp = &pats.weave[pats.scene[cur_scene()].weave % NPATT];
    int len = wp->len < 1 ? 16 : wp->len, pos = playing() ? (int)((seq.step_tick + (uint32_t)len - 1u) % (uint32_t)len) : -1;
    uint32_t h = hash_u(hash_u(2166136261u, 0x5Bu), (uint32_t)ui.bar | (uint32_t)(pos + 1) << 4 | (uint32_t)ui.wv_step << 12 |
                                          (uint32_t)len << 20);
    char t[24];
    for (int i = 0; i < NSTEP; i++)
        h = hash_u(h, wv_mask(&wp->st[i]) | (uint32_t)wp->st[i].len << 16);
    if (!begin_main(h))
        return;
    num_s((uint32_t)pats.scene[cur_scene()].weave % NPATT + 1u, s_cat(t, "WEAVE P"));
    if (band == &BANDS[0])                           /* (large type: the cells from the top; the knob says the pattern) */
        cv_text(ST_X + 2, 0, &FONT_XS, t, K_TEXT);
    for (int i = 0; i < 16; i++) {
        int s = ui.bar * 16 + i, in = s < len;
        const wv_step_t *w = &wp->st[s];
        uint32_t m = wv_mask(w);
        int32_t x = ST_X + (i % 8) * ST_CW, y = ST_Y + (i / 8) * (ST_CH + 4);
        uint16_t bg = !in ? K_BG : s == pos ? K_NOW : m ? K_PANEL2 : K_PANEL;
        char n[4];
        cv_round(x, y, ST_CW - 2, ST_CH, 4, bg);
        if (s == ui.wv_step)
            cv_frame(x, y, ST_CW - 2, ST_CH, 4, K_SEL, bg);
        num_s((uint32_t)s + 1u, n);
        cv_text(x + 4, y + ST_NUM, &FONT_XS, n, bg == K_NOW ? K_BG : K_DIM);
        if (m) {                                     /* the first track woven, and how many more */
            int first = __builtin_ctz(m), more = __builtin_popcount(m) - 1;
            text_c(x + ST_MID, y + ST_L1, &FONT_XS, first < NTRK ? TRACK_NAME[first] : first == AUX_BRAIN ? "CO" : "TH",
                   bg == K_NOW ? K_BG : K_TEXT);
            if (more > 0) {
                num_s((uint32_t)more, s_cat(t, "+"));
                text_c(x + ST_MID, y + ST_L2, &FONT_XS, t, bg == K_NOW ? K_BG : K_DIM);
            }
        }
    }
    for (int i = 0; i < 16; i++) {                   /* the lengths, over the cells: a bar from each step on */
        const wv_step_t *w = &wp->st[ui.bar * 16 + i];
        int32_t y = ST_Y + (i / 8) * (ST_CH + 4);
        if (!wv_mask(w) || ui.bar * 16 + i >= len)
            continue;
        for (int j = 0; j < (w->len ? w->len : 1) && i + j < 16 && (i + j) / 8 == i / 8; j++)
            cv_rect(ST_X + ((i + j) % 8) * ST_CW + (j ? 0 : 3), y + ST_CH - 4, ST_CW - (j ? 0 : 3) - (j + 1 == (w->len ? w->len : 1) ? 3 : 0), 2, K_NOW);
    }
    main_blit();
}
/* the Steps page's head: an instrument's name (nm) and what a step takes (t: its sound, or the notes held); an aux
 * track's in t alone ("fx: ...", "chord: ...") */
static void steps_head(int trk, char *nm, char *t)
{
    char *e = t;
    nm[0] = t[0] = 0;
    if (!is_aux(trk)) {
        for (int i = 0; i < NAME_LEN; i++)
            nm[i] = proj.trk[trk].name[i];
        nm[NAME_LEN] = 0;
        if (!nm[0])
            s_cat(nm, EG_NAME[proj.trk[trk].par[TP_ENGINE] % NENGINE]);
        if (is_drum(trk)) {
            s_cat(t, DL_SOUND_NAME[ui.dsound % DL_NSOUND]);
            return;
        }
    } else if (trk == AUX_PUNCH) {
        e = s_cat(t, "fx: ");
        if (ui.rem_fx && PX_VALID(ui.rem_fx - 1))
            s_cat(s_cat(e, PUNCH_NAME[(ui.rem_fx - 1) & 15]), ui.rem_fx - 1 >= PX_MEL ? " (melodic)" : " (drums)");
        else
            s_cat(e, "play a key on EDIT first");
        return;
    } else
        e = s_cat(t, "chord: ");
    for (int i = 0; i < ui.nrem && i < SN && (is_aux(trk) || pl_type(trk) != PL_RAND); i++) {   /* (the random
                                                     * player: no notes held for its steps) */
        if (i)
            e = s_cat(e, " ");
        e = note_name(ui.rem[i], e);
    }
}
static void draw_steps_as(void);
static void draw_steps(void)                         /* large type: the cells in today's faces (eight across) */
{
    int was = lt_on;
    loom_type(0);
    draw_steps_as();
    loom_type(was);
}
static void draw_steps_as(void)
{
    if (sel_trk() == AUX_WEAVE) {
        draw_weave_steps();
        return;
    }
    {
    int trk = sel_trk(), drum = is_drum(trk), pt = cur_pat(trk), ph = eng_step[trk];
    const tpat_t *tp = tpat(trk);
    uint32_t h = hash_u(hash_u(2166136261u, 0x57u), (uint32_t)trk | (uint32_t)pt << 4 | (uint32_t)ui.bar << 8 |
                                          (uint32_t)ui.held << 10 | (uint32_t)ui.dsound << 26);
    char t[40];
    h = hash_u(h, (uint32_t)ph | (uint32_t)playing() << 8 | (uint32_t)ui.rec_armed << 9 | (uint32_t)ui.cursor << 16 |
                      (uint32_t)ui.lockpg << 24 | (uint32_t)(seq.rec_wait != 0) << 30);
    h = hash_u(h, ui.trickpg | (uint32_t)ui.rem_fx << 16 | (uint32_t)(ui.frame / 8 & 1u) << 31);
    h = hash_pat(h, tp);
    h = hash_s(hash_u(h, proj.trk[trk].par[TP_ENGINE]), proj.trk[trk].name);   /* the sound's name, shown */
    for (int i = 0; i < 4; i++)
        h = hash_u(h, ui.rem[i]);
    h = hash_u(h, lane_count(pats.lane));
    if (!is_seqt(trk)) {
        h = hash_u(h, 0xFEu);
        if (!begin_main(h))
            return;
        text_c(120, 40, &FONT_B, TRACK_NAME[trk], K_TEXT);
        text_c(120, 62, &FONT_S, "has no steps: turn ALGORITHM", K_DIM);
        main_blit();
        return;
    }
    for (int i = 0; i < 16; i++)
        h = hash_u(h, step_has_lock(pats.lane, trk, pt, ui.bar * 16 + i));
    if (!begin_main(h))
        return;
    if (band == &BANDS[0]) {                         /* the sound and what a step takes (large type: on the tag) */
        char nm[NAME_LEN + 2];
        int32_t x = ST_X + 2;
        steps_head(trk, nm, t);
        if (nm[0])
            x = text_fit(x, 0, &FONT_XS, nm, 96, K_TEXT) + 8;
        if (t[0])
            text_fit(x, 0, &FONT_XS, t, (nm[0] ? 168 : 232) - x, K_DIM);
    }
    if (band == &BANDS[0]) {
        int nb = (tp->len + 15) / 16, pb = ph / 16;
        for (int b = 0; b < NBAR; b++) {
            int32_t x = 176 + b * 14;
            uint16_t c = b < nb ? K_DIM : K_LINE;
            cv_round(x - 1, 0, 12, 15, 4, b == pb && playing() ? K_NOW : c);
            if (b == ui.bar)
                cv_frame(x - 1, 0, 12, 15, 4, K_SEL, b == pb && playing() ? K_NOW : c);
            num_s((uint32_t)b + 1u, t);
            text_c(x + 5, cap_y(0, 15, &FONT_XS), &FONT_XS, t, b == ui.bar || (b == pb && playing()) ? K_BG : K_TEXT);
        }
    }
    for (int i = 0; i < 16; i++) {
        int s = ui.bar * 16 + i, in = s < tp->len, held = ui.held >> i & 1u, on, implicit;
        const step_t *st = &tp->st[s];
        int32_t x = ST_X + (i % 8) * ST_CW, y = ST_Y + (i / 8) * (ST_CH + 4);
        uint16_t bg, fg;
        hit_t hm;
        on = in && (pl_type(trk) == PL_RAND ? step_slot_on(tp, s) : step_on(st, trk));
        implicit = on && pl_type(trk) == PL_RAND && !step_slot_set(tp, s);   /* none set: all on, shown plain */
        bg = !in ? K_BG : on && !implicit ? K_TINT : K_PANEL;
        if (playing() && s == ph && in)
            bg = on ? K_NOW : K_NOW_T;
        fg = bg == K_NOW ? K_BG : K_TEXT;
        cv_round(x, y, ST_CW - 2, ST_CH, 4, bg);
        if (!in)
            cv_frame(x, y, ST_CW - 2, ST_CH, 4, K_LINE, K_BG);
        if (held)
            cv_frame(x, y, ST_CW - 2, ST_CH, 4, K_SEL, bg), cv_frame(x + 1, y + 1, ST_CW - 4, ST_CH - 2, 3, K_SEL, bg);
        if ((ui.rec_armed && s == ui.cursor) || (seq.rec_wait && s == 0 && (ui.frame / 8 & 1u)))
            cv_frame(x, y, ST_CW - 2, ST_CH, 4, K_REC, bg);
        num_s((uint32_t)s + 1u, t);
        cv_text(x + 4, y + ST_NUM, &FONT_XS, t, bg == K_NOW ? K_BG : K_DIM);
        /* the cell, top down, every row in the small font (ink rows from the cell's top): the number and the lock's
         * dot (3-11), line 1 (13-21), line 2 (23-31) or a kit's ticks (26-29), the marks (33-41: the chance's sign
         * at the left, the tricks' letters at the right), the length's bar (42-43). A line is at most ST_TW wide. */
        if (drum && !step_empty(st)) {               /* every sound on the step: a tick each, by key, the shown one bright */
            uint32_t have = kit_mask(st);
            int nt = __builtin_popcount(have), row1 = nt > 5 ? (nt + 1) / 2 : nt;
            for (int b = 0, j = 0; b < DL_NSOUND; b++)
                if (have >> b & 1u) {                /* up to five a row of 4 px ticks; more: two rows of 3 px */
                    uint16_t tc = b == ui.dsound ? (bg == K_NOW ? K_BG : K_TEXT) : (bg == K_NOW ? K_PANEL2 : K_NOW_M);
                    if (nt <= 5) {
                        int32_t tx = x + (ST_CW - 2) / 2 - (nt * 5 - 1) / 2 + j * 5;
                        cv_round(tx, y + ST_TICK, 4, 4, 1, tc);   /* their own row, between the name and the marks */
                    } else {
                        int r = j >= row1, n = r ? nt - row1 : row1, c = r ? j - row1 : j;
                        int32_t tx = x + (ST_CW - 2) / 2 - (n * 4 - 1) / 2 + c * 4;
                        cv_rect(tx, y + ST_TICK - 1 + r * 4, 3, 3, tc);
                    }
                    j++;
                }
            if (on) {
                s_cat(t, DL_SOUND_NAME[ui.dsound % DL_NSOUND]);
                t[3] = 0;
                text_c(x + ST_MID, y + ST_L1, st_font(t), t, fg);
            }
        } else if (on) {
            uint16_t dim = bg == K_NOW ? K_BG : K_DIM;
            if (pl_type(trk) == PL_RAND) {
                text_c(x + ST_MID, y + ST_L1, &FONT_XS, "on", implicit ? dim : fg);
                if (!implicit) {                     /* its register: the octave its note sets (4: C4, a plain tap) */
                    char o[4];
                    num_i((st->note[0] ? st->note[0] : 60) / 12 - 1, o);
                    text_c(x + ST_MID, y + ST_L2, st_font(o), o, dim);
                }
            } else if (trk == AUX_PUNCH) {
                int fx = st->note[0] - 1;
                /* the effect in capitals (no descender to meet the line under it), three letters, over its group */
                static const char *const PX_CELL[NPUNCH] = {"MUT", "STU", "REP", "ECH", "BLD", "FLR", "JMB", "WBL",
                                                            "PAN", "OCT", "CHP", "RNG"};
                const char *a = PX_VALID(fx) ? PX_CELL[(fx & 15) % NPUNCH] : "?", *b = PX_VALID(fx) && fx >= PX_MEL ? "mel" : "drm";
                text_c(x + ST_MID, y + ST_L1, st_font(a), a, fg);
                text_c(x + ST_MID, y + ST_L2, st_font(b), b, dim);
            } else {                                 /* notes: two lines, each one name (they never fit two a line) */
                char l1[12], l2[12];
                int nn = 0, lo = 0, root = 0, sh;
                uint16_t c2 = fg;
                for (int k = 0; k < SN && st->note[k]; k++) {
                    if (st->note[k] < st->note[lo])
                        lo = k;
                    nn++;
                }
                l2[0] = 0;
                if ((sh = chord_shape(st->note, nn, &root)) != 0) {
                    /* a chord: its name, the bass note under it (dim). The quality in full when the name fits and
                     * the quality has no descender (it would touch the line under it), else short ("CM7", "C+", "Co",
                     * "Ds2"); when even that is too wide ("C#M7", "Ebm7", "F#s4"), the root over its quality (in full
                     * when it fits: "sus4"), the bass left out */
                    const char *suf = CHORD_SHAPE[sh - 1].suf, *q;
                    for (q = suf; *q; q++)
                        if (*q == 'g' || *q == 'j' || *q == 'p' || *q == 'q' || *q == 'y')
                            suf = CHORD_SHAPE[sh - 1].sh;
                    s_cat(s_cat(l1, NOTE_NAME[root]), suf);
                    if (text_w(&FONT_XS, l1) > ST_TW)
                        s_cat(s_cat(l1, NOTE_NAME[root]), CHORD_SHAPE[sh - 1].sh);
                    if (text_w(&FONT_XS, l1) > ST_TW) {
                        s_cat(l1, NOTE_NAME[root]);
                        s_cat(l2, suf);
                        if (text_w(&FONT_XS, l2) > ST_TW)
                            s_cat(l2, CHORD_SHAPE[sh - 1].sh);
                    } else
                        cell_note(st->note[lo], l2), c2 = dim;
                } else {                             /* the first note; under it the second, or "+3": three more */
                    cell_note(st->note[0], l1);
                    if (nn == 2)
                        cell_note(st->note[1], l2);
                    else if (nn > 2)
                        num_s((uint32_t)nn - 1u, s_cat(l2, "+"));
                }
                text_c(x + ST_MID, y + ST_L1, st_font(l1), l1, fg);
                if (l2[0])
                    text_c(x + ST_MID, y + ST_L2, st_font(l2), l2, c2);
            }
        }
        hit_marks(trk, pt, s, st, &hm);                  /* (a kit: its sounds' own too) */
        if (hm.comp || hm.comp2) {                   /* the tricks' letters, bottom right */
            char g[4] = {0};
            char *e = g;
            if (hm.comp)
                e = s_cat(e, COMP_GLYPH[hm.comp % NSC]);
            if (hm.comp2)
                s_cat(e, COMP_GLYPH[hm.comp2 % NSC]);
            text_r(x + ST_CW - 6, y + ST_MK, &FONT_XS, g, fg);
        }
        if (hm.cond) {                              /* a small percent sign (5 x 7): the font's is too wide beside two letters */
            static const uint8_t PCT[7] = {0x19, 0x1A, 0x02, 0x04, 0x08, 0x0B, 0x13};
            for (int r = 0; r < 7; r++)
                for (int b = 0; b < 5; b++)
                    if (PCT[r] >> (4 - b) & 1u)
                        cv_rect(x + 3 + b, y + ST_MK + 4 + r, 1, 1, bg == K_NOW ? K_BG : K_DIM);
#ifdef GFX_AUDIT
            ga_text("%", x + 3, y + ST_MK + 4 + cv_oy, x + 7, y + ST_MK + 10 + cv_oy, 0);   /* audited as a label */
#endif
        }
        if (step_has_lock(pats.lane, trk, pt, s))
            cv_round(x + ST_CW - 9, y + 3, 4, 4, 2, K_REC);
    }
    {   /* lengths: a bar from a step across the steps its note covers, past the bar and round from the pattern's end
         * to its start; an arrow where it goes on onto another bar (or round), one in where it came from one */
        int plen = tp->len < 1 ? 1 : tp->len > NSTEP ? NSTEP : tp->len, b0 = ui.bar * 16;
        for (int s0 = 0; s0 < plen; s0++) {
            const step_t *st = &tp->st[s0];
            int c = step_cover(st);
            if (step_empty(st) || c < 2)
                continue;
            if (c > plen)
                c = plen;
            for (int k = 0; k < c; k++) {
                int sj = (s0 + k) % plen, j = sj - b0, last = k == c - 1;
                int32_t x, y, x0, x1;
                if (j < 0 || j > 15)
                    continue;
                x = ST_X + (j % 8) * ST_CW;
                y = ST_Y + (j / 8) * (ST_CH + 4) + ST_CH - 4;
                x0 = k == 0 ? x + 3 : x - 2;
                x1 = last || j == 15 || j % 8 == 7 || sj == plen - 1 ? x + ST_CW - 5 : x + ST_CW;
                cv_rect(x0, y, x1 - x0, 2, K_DIM);   /* rows 42-43, under the marks */
                if (!last && (j == 15 || sj == plen - 1))   /* it goes on: past the bar, or round to the start */
                    for (int q = 0; q < 3; q++)
                        cv_rect(x + ST_CW - 5 + q, y - 2 + q, 1, 6 - 2 * q, K_TEXT);
                if (k && (j == 0 || sj == 0))     /* it came from another bar (or round from the end) */
                    for (int q = 0; q < 3; q++)
                        cv_rect(x + q, y - 2 + q, 1, 6 - 2 * q, K_TEXT);
            }
        }
    }
    main_blit();
    }
}

static void draw_bar(void)
{
    int trk = sel_trk();
    const tpat_t *tp = tpat(trk);
    uint32_t h = hash_u(hash_u(2166136261u, 0xBAu), (uint32_t)trk | (uint32_t)cur_pat(trk) << 4 | (uint32_t)tp->len << 8 |
                                          (uint32_t)tp->scale << 16 | (uint32_t)eng_step[trk] << 20 | (uint32_t)playing() << 28);
    char t[48], *e;
    h = hash_u(h, (uint32_t)(tp->groove + 100) | (uint32_t)proj.groove << 8 | (uint32_t)tp->quant << 16 |
                      (uint32_t)ui.bar << 24 | (uint32_t)is_drum(trk) << 28);
    h = hash_u(h, (uint32_t)tp->lstart | (uint32_t)tp->lend << 8 | (uint32_t)seq.t[trk].bq_go << 16 | (uint32_t)seq.t[trk].bq_f << 24);
    h = hash_u(h, seq.t[trk].bq_e);
    if (!begin_main(h))
        return;
    if (!is_seqt(trk)) {
        text_c(120, 50, &FONT_S, "No steps on this track", K_DIM);
        main_blit();
        return;
    }
    static const char *const STEP_FRAC[NTS] = {"1/32", "1/16", "1/8", "3/16", "1/4", "5/16", "3/8", "1/2", "1 Bar"};
    e = num_s((uint32_t)tp->len, s_cat(t, "Length: "));
    e = s_cat(e, tp->len == 1 ? " step" : " steps");
    e = s_cat(e, "   Step: ");
    s_cat(e, STEP_FRAC[tp->scale % NTS]);
    cv_text(12, LY(6, 2), LY(&FONT_S, &FONT_XS), t, K_TEXT);
    {
        int nb = (tp->len + 15) / 16, ph = eng_step[trk], lf, le, go = seq.t[trk].bq_go, pf = seq.t[trk].bq_f, pe = seq.t[trk].bq_e;
        pat_loop(tp, &lf, &le);
        for (int b = 0; b < NBAR; b++) {
            int32_t x = 12 + b * 56, w = 50;
            int steps = tp->len - b * 16, in = b < nb && b * 16 < le && b * 16 + 16 > lf;
            steps = steps > 16 ? 16 : steps < 0 ? 0 : steps;
            int out = b < nb && (lf > 0 || le < tp->len) && !in;   /* (a cropped loop: the bars outside it sit back) */
            if (in && (lf > 0 || le < tp->len))              /* the loop: a bar under its bars */
                cv_rect(x, LY(79, 65), w, 3, K_NOW);
            if (go != 0xFF && (pf > 0 || pe > 0)) {          /* a loop waiting for the bar line: its bars outlined */
                if (b * 16 < (pe ? pe : tp->len) && b * 16 + 16 > pf && b < nb)
                    cv_frame(x, LY(78, 64), w, 5, 2, K_TEXT, K_BG);
            } else if (go != 0xFF && go / 16 == b)           /* a jump waiting for the bar line */
                cv_frame(x + w / 2 - 4, LY(78, 64), 8, 5, 2, K_TEXT, K_BG);
            if (b == ui.bar)                         /* the bar the white keys write (OCT- OCT+ pick it) */
                cv_round(x - 2, LY(32, 24), w + 4, LY(44, 38), 7, K_TEXT);
            cv_round(x, LY(34, 26), w, LY(40, 34), 5, b < nb && !out ? K_PANEL2 : K_PANEL);
            if (steps < 16 && steps > 0)
                cv_round(x + w * steps / 16, LY(34, 26), w - w * steps / 16, LY(40, 34), 5, K_PANEL);
            if (playing() && ph / 16 == b)
                cv_round(x + 2 + 46 * (ph % 16) / 16, LY(64, 51), 4, 8, 2, K_NOW);   /* (below the number) */
            num_s((uint32_t)b + 1u, t);
            text_c(x + w / 2, LY(46, 31), &FONT_B, t, b < nb && !out ? K_TEXT : b < nb ? K_DIM : K_FAINT);
        }
    }
    e = s_cat(s_cat(t, "Groove "), GR_NAME[proj.groove % NGROOVE]);
    e = s_cat(e, " ");
    if (tp->groove > 0)
        e = s_cat(e, "+");
    e = num_i(tp->groove, e);
    e = num_s((uint32_t)tp->quant, s_cat(e, ", quantise "));
    s_cat(e, "%");
    cv_text(12, LY(86, MAIN_H - 27), F_CAP, t, K_DIM);
    cv_text(12, LY(100, MAIN_H - 15), F_CAP, is_drum(trk) ? "white keys: the bar's steps, black keys: sounds"
                                            : "black keys: scale 1 2 3 4 5 6 8 16 1/2", K_DIM);
    main_blit();
}

/* the engines' pictures */
/* the engines' pictures: each follows its four knobs (p[TP_E1..E4], 0..127), redrawn as they turn */
static float weft_pic_v(float ph, float d, float kd, float kd2, float mo, float r, int reso)   /* synth.c weft_wave */
{
    float qs = ph < d ? ph * kd : 0.5f + (ph - d) * kd2;
    float qp = ph < 0.5f ? fm_minf(ph * kd, 0.5f) : 0.5f + fm_minf((ph - 0.5f) * kd, 0.5f);
    if (!reso)
        return fm_sin_turns(qs + (qp - qs) * mo + 0.25f);
    {
        float pl = fm_sin_turns(qp + 0.25f), rs = (1.0f - ph) * fm_sin_turns(r * qs);
        return pl + (rs - pl) * mo;
    }
}
/* the engine pictures move while the track sounds and rest when it is quiet: their clock (pic_f) runs only then, and
 * pic_lvl is the track's level (eng_peak, held and falling: a plucked string rings and settles with its note) */
static uint32_t pic_f;
static float pic_lvl;
static void pic_wave(int32_t x0, int32_t y0, int32_t w, int32_t h, const uint8_t *p, uint32_t f)
{
    /* the table's sixteen frames stacked front (scan 0) to back, faint; the frame scan reads drawn bright at its place
     * in the stack, bent as bend bends the phase (synth.c wt_run) and pushed by ripple's second cycle, which slides
     * while the track sounds */
    int t = p[TP_E1];
    enum { WT_PIC = 16 };
    float scan = (float)p[TP_E2] / 127.0f, wp = 0.5f + (float)p[TP_E3] / 127.0f * 0.45f, dr = (float)p[TP_E4] / 127.0f;
    float ka = 0.5f / wp, kb = 0.5f / (1.0f - wp), depth = dr * dr * 0.6f;
    float rate = dr > 0.001f ? fm_exp2f(-7.0f + 8.0f * dr) : 0.0f, mp0 = (float)f / 90.0f;
    int32_t dx = 40, dy = 22, ww = w - dx;
    float amp = (float)(h - dy - 6) / 2.0f, cy0 = (float)(y0 + h) - amp - 3.0f;
    for (int fr = WT_PIC - 1; fr >= 0; fr--) {    /* (16 waves in the stack: the table's 8 frames and the crossfades
                                                  * between them, as wt_value scans it) */
        pen_t pn = {0};
        float d = (float)fr / (float)(WT_PIC - 1), ox = (float)x0 + d * (float)dx, cy = cy0 - d * (float)dy;
        for (int32_t x = 0; x <= ww; x += 3)
            pen_to(&pn, ox + (float)x, cy - wt_value(t, d, (float)x / (float)ww) * amp, K_FAINT);
    }
    {
        pen_t pn = {0};
        float ox = (float)x0 + scan * (float)dx, cy = cy0 - scan * (float)dy;
        for (int32_t x = 0; x <= ww; x++) {
            float ph = (float)x / (float)ww, q = ph < wp ? ph * ka : 0.5f + (ph - wp) * kb;
            if (rate > 0.0f)
                q += depth * fm_sin_turns(mp0 + ph * rate);
            pen_to(&pn, ox + (float)x, cy - wt_value(t, scan, q) * amp, K_NOW);
        }
    }
}
static void pic_rayon(int32_t x0, int32_t y0, int32_t w, int32_t h, const uint8_t *p, uint32_t f)
{
    /* bright: the ring's size; ratio: the modulator's orbit; edge: its dots; wobble: its speed */
    float cx = (float)(x0 + w / 2), cy = (float)(y0 + h / 2), r1 = 14.0f + p[TP_E1] * (float)(h / 2 - 20) / 127.0f;
    float r2 = 4.0f + p[TP_E2] * 16.0f / 127.0f, k = (float)f * (1.0f + (float)(p[TP_E4] / 32)) / 64.0f;
    uint32_t dots = 4u + p[TP_E3] / 10u;
    pen_t ring = {0};
    for (int i = 0; i <= 64; i++) {
        float t = (float)i / 64.0f;
        pen_to(&ring, cx + fm_sin_turns(t + 0.25f) * r1, cy + fm_sin_turns(t) * r1, K_DIM);
    }
    {
        float mx = cx + fm_sin_turns(k + 0.25f) * r1, my = cy + fm_sin_turns(k) * r1;
        cv_linef(cx, cy, mx, my, K_DIM, 1.2f);
        for (uint32_t j = 0; j < dots; j++) {
            float t = (float)j / (float)dots;
            cv_dotf(mx + fm_sin_turns(t + 0.25f) * r2, my + fm_sin_turns(t) * r2, 1.3f, K_NOW);
        }
    }
}
static void pic_organ(int32_t x0, int32_t y0, int32_t w, int32_t h, const uint8_t *p, uint32_t f)
{
    /* the six drawbars stops and pedal pull out (synth.c organ_bars), each from the top as far as its level, its
     * footage under it; the rotor at the right: a horn turning at rate, as wide as its amount (still at 1) */
    static const char *const FT[6] = {"16", "8", "4", "2 2/3", "2", "1 1/3"};
    float bars[6];
    int32_t bw = 18, gap = 9, bh = h - 22, rx = x0 + w - 24, ry = y0 + bh / 2 + 4, rr = bh / 2 + 2 < 20 ? bh / 2 + 2 : 20;
    int amt = p[TP_E3] < 1 ? 1 : p[TP_E3] > 8 ? 8 : p[TP_E3], rate = p[TP_E4] < 1 ? 1 : p[TP_E4] > 8 ? 8 : p[TP_E4];
    organ_bars(p, bars);
    for (int k = 0; k < 6; k++) {
        int32_t x = x0 + k * (bw + gap), v = 6 + (int32_t)(bars[k] * (float)(bh - 6) + 0.5f);
        cv_round(x, y0, bw, bh, 3, K_PANEL2);
        cv_round(x, y0, bw, v, 3, k == 1 ? K_NOW : K_DIM);
        cv_rect(x + 3, y0 + v - 4, bw - 6, 2, K_BG);        /* the bar's handle */
        text_c(x + bw / 2, y0 + bh + 4, &LFONT_XS, FT[k], K_DIM);   /* (12 px always: 27 px apart) */
    }
    {
        float a = amt > 1 ? (float)f * 0.012f * (float)rate : 0.0f;   /* (rate 1..8: 0.5 .. 8 Hz, drawn slower) */
        float r = 6.0f + (float)(amt - 1) * 2.0f;
        pen_t ring = {0};
        for (int i = 0; i <= 32; i++) {
            float t = (float)i / 32.0f;
            pen_to(&ring, (float)rx + fm_sin_turns(t + 0.25f) * (float)rr, (float)ry + fm_sin_turns(t) * (float)rr, K_LINE);
        }
        cv_linef((float)rx - fm_sin_turns(a + 0.25f) * r, (float)ry - fm_sin_turns(a) * r * 0.4f,
                 (float)rx + fm_sin_turns(a + 0.25f) * r, (float)ry + fm_sin_turns(a) * r * 0.4f, K_NOW, 2.0f);
        text_c(rx, y0 + bh + 4, &LFONT_XS, "rotor", K_DIM);
    }
}
static float knit_y(float fr, const uint8_t *p, int osc2);
/* knit (Engine and Osc 2, one picture): osc 1's wave (saw .. square .. pulse) a line a voice, the voices drifting apart
 * as detune opens (one voice: its slow drift) and fanned up and down as width opens; osc 2 at its interval and fine
 * tune, its own wave (when heard: its level on Levels). The page's oscillator bright and on top (o2: Osc 2's), the
 * other faint */
static void pic_knit(int32_t x0, int32_t y0, int32_t w, int32_t h, const uint8_t *p, uint32_t f, int o2)
{
    static const float OFF[7] = {0.0f, 0.3f, -0.3f, 0.6f, -0.6f, 1.0f, -1.0f};
    int n = p[TP_E3] < 1 ? 1 : p[TP_E3] > 7 ? 7 : p[TP_E3], on = p[TP_X2] > 0;
    float det = (float)p[TP_E2] / 127.0f, amp = (float)h / 6.0f, fan = (float)p[TP_E4] / 127.0f * ((float)h / 2.0f - amp - 2.0f);
    float ratio = fm_exp2f(((float)clampi((int)p[TP_X1] - 64, -24, 24) + (float)((int)p[TP_O2FINE] - 64) * 0.01f) * (1.0f / 12.0f));
    cv_rect(x0, y0 + h / 2, w, 1, K_LINE);
    for (int k = 0; k < 2; k++) {                    /* the other oscillator first, the page's on top */
        if ((k == 0) == (o2 != 0)) {                 /* osc 1: its voices, the centre one last */
            for (int j = n - 1; j >= 0; j--) {
                pen_t pn = {0};
                float o = n > 1 ? OFF[j] : 0.0f, drift = n > 1 ? o * det * (float)f / 24.0f : det * 0.1f * fm_sin_turns((float)f / 97.0f);
                float cy = (float)(y0 + h / 2) - o * fan;
                uint16_t col = o2 ? K_FAINT : j == 0 ? K_NOW : K_DIM;
                for (int32_t x = 0; x <= w; x++) {
                    float ph = (float)x / (float)w * 3.0f + drift;
                    pen_to(&pn, (float)(x0 + x), cy - knit_y(ph - fm_floorf(ph), p, 0) * amp, col);
                }
            }
        } else if (on || o2) {                       /* osc 2 (off: on its own page only, dim) */
            pen_t pn = {0};
            for (int32_t x = 0; x <= w; x++) {
                float ph = (float)x / (float)w * 3.0f * ratio;
                pen_to(&pn, (float)(x0 + x), (float)(y0 + h / 2) - knit_y(ph - fm_floorf(ph), p, 1) * amp,
                       !o2 ? K_FAINT : on ? K_NOW : K_DIM);
            }
        }
    }
}
/* acid: three cycles of its wave (saw, or the 303's square) through a resonant low-pass at its cutoff, opened by its env
 * mod as a note's start does: the resonance rings at each edge; the decay's curve dim behind */
static void pic_acid(int32_t x0, int32_t y0, int32_t w, int32_t h, const uint8_t *p)
{
    const int sq = p[TP_E1] & 1;
    const float cut = (float)p[TP_CUT] / 127.0f, res = (float)p[TP_RES] / 127.0f, env = (float)p[TP_FENV] / 127.0f;
    const float dec = (float)p[TP_D] / 127.0f, amp = (float)h * 0.28f, cy = (float)(y0 + h / 2);
    const float g = 0.03f + 0.25f * cut * cut + 0.2f * env, q = 2.0f - 1.85f * res;
    float lo = 0.0f, bp = 0.0f;
    pen_t pe = {0}, pn = {0};
    cv_rect(x0, y0 + h / 2, w, 1, K_LINE);
    for (int32_t x = 0; x <= w; x++) {               /* the decay: from the top, falling over 0.2 .. 2 s */
        float t = (float)x / (float)w;
        pen_to(&pe, (float)(x0 + x), (float)(y0 + 4) + (float)(h - 8) * (1.0f - fm_expf(-t * (5.0f - 4.0f * dec))), K_LINE);
    }
    for (int pass = 0; pass < 2; pass++)             /* (the first pass settles the filter: the second is drawn) */
        for (int32_t x = 0; x <= w; x++) {
            float ph = (float)x / (float)w * 3.0f, fr = ph - fm_floorf(ph);
            float in = sq ? (fr < 0.5f ? 1.0f : -1.0f) : 1.0f - 2.0f * fr, hi;
            for (int k = 0; k < 2; k++) {            /* a state-variable low-pass, twice a pixel */
                hi = in - lo - q * bp;
                bp += g * hi;
                lo += g * bp;
            }
            if (pass)
                pen_to(&pn, (float)(x0 + x), cy - fm_clampf(lo, -1.6f, 1.6f) * amp, K_NOW);
        }
}
/* cane: the model's instrument. pluck: a string bent at color's place, swinging (longer at a higher timbre); bow:
 * the string and the bow across it at color's place, heavier with timbre; reed and flute: the tube and its
 * holes, the breath's specks at the mouth (fewer as timbre steadies it), the flute's embouchure at color; bell:
 * the bell and its rings (more as timbre lengthens them); drum: the head, its ripples; damp dims the ink */
static inline float pic_wrap(float x) { return x - fm_floorf(x); }
static void pic_cane(int32_t x0, int32_t y0, int32_t w, int32_t h, const uint8_t *p, uint32_t f)
{
    int md = p[TP_E1] < NCANE ? p[TP_E1] : 0;
    float tb = (float)p[TP_E2] / 127.0f, co = (float)p[TP_E3] / 127.0f, cy = (float)(y0 + h / 2);
    float xl = (float)(x0 + 8), xr = (float)(x0 + w - 8), xp = xl + (0.1f + 0.8f * co) * (xr - xl);
    uint16_t ink = p[TP_E4] > 64 ? K_DIM : K_NOW;
    uint32_t r = 77u ^ (f / 2);
    pen_t pn = {0};
    if (md <= 1) {                                   /* a string between its nut and its bridge */
        float a = (float)h * 0.3f * fm_sin_turns((float)f * 0.09f) * (md ? 0.25f : 0.3f + 0.7f * tb) * pic_lvl;
        cv_round(x0 + 2, (int32_t)cy - 8, 6, 16, 2, K_DIM);
        cv_round(x0 + w - 8, (int32_t)cy - 8, 6, 16, 2, K_DIM);
        pen_to(&pn, xl, cy, ink);
        if (md == 0) {
            pen_to(&pn, xp, cy - a, ink);
        } else {
            for (int k = 1; k <= 16; k++) {           /* the bowed string's swing, a lens */
                float t = (float)k / 16.0f;
                pen_to(&pn, xl + t * (xr - xl), cy - a * fm_sin_turns(t * 0.5f), ink);
            }
            cv_round((int32_t)xp - 2 - (int32_t)(tb * 3.0f), y0 + 4, 4 + (int32_t)(tb * 6.0f), h - 8, 3, K_DIM);
        }
        pen_to(&pn, xr, cy, ink);
    } else if (md <= 3) {                            /* a tube, its holes; the breath at its mouth */
        int32_t th = md == 2 ? 26 : 16, tx = x0 + 34, tw = w - 40;
        cv_frame(tx, (int32_t)cy - th / 2, tw, th, th / 2, ink, K_BG);
        for (int k = 0; k < 6; k++)
            cv_dotf((float)(tx + tw / 3 + k * (tw * 2 / 3 - 12) / 5), cy, md == 2 ? 3.0f : 2.2f, ink);
        if (md == 2) {                               /* the reed's mouthpiece */
            pen_to(&pn, (float)tx, cy - 9.0f, ink);
            pen_to(&pn, (float)(tx - 16), cy - 3.0f, ink);
            pen_to(&pn, (float)(tx - 16), cy + 3.0f, ink);
            pen_to(&pn, (float)tx, cy + 9.0f, ink);
        } else {                                     /* the embouchure, at color's place along the head */
            cv_round(tx + 6 + (int32_t)(co * 16.0f), (int32_t)cy - 4, 8, 8, 4, K_DIM);
        }
        for (int k = 0; k < 4 + (int)((1.0f - tb) * 20.0f); k++) {
            r = r * 1664525u + 1013904223u;
            cv_rect(x0 + 2 + (int32_t)((r >> 8) % 26u), (int32_t)cy - 10 + (int32_t)((r >> 20) % 20u), 2, 2, K_DIM);
        }
    } else if (md == 4) {                            /* the bell, its clapper swinging, its rings */
        float bx = (float)(x0 + w / 2), sw = 3.0f * fm_sin_turns((float)f * 0.05f);
        for (int k = 0; k <= 20; k++) {
            float t = (float)k / 20.0f, half = 8.0f + 26.0f * t * t * t + 6.0f * t;
            pen_to(&pn, bx - half, (float)(y0 + 10) + t * (float)(h - 26), ink);
        }
        pen_up(&pn);
        for (int k = 0; k <= 20; k++) {
            float t = (float)k / 20.0f, half = 8.0f + 26.0f * t * t * t + 6.0f * t;
            pen_to(&pn, bx + half, (float)(y0 + 10) + t * (float)(h - 26), ink);
        }
        cv_line(x0 + w / 2 - 40, y0 + h - 16, x0 + w / 2 + 40, y0 + h - 16, ink);
        cv_dotf(bx + sw, (float)(y0 + h - 10), 3.5f, K_DIM);
        for (int k = 0; k < 1 + (int)(tb * 4.0f); k++) {
            float rr = 46.0f + (float)((int)(f * 2u + (uint32_t)k * 9u) % 36);
            cv_rect((int32_t)(bx + rr), y0 + h / 2 - 4, 1, 8, K_FAINT);
            cv_rect((int32_t)(bx - rr), y0 + h / 2 - 4, 1, 8, K_FAINT);
        }
    } else {                                         /* the drum: its head, the ripples a strike sends */
        float dx = (float)(x0 + w / 2), dy = (float)(y0 + 26), ex = 60.0f, ey = 12.0f;
        for (int k = 0; k <= 32; k++)
            pen_to(&pn, dx + ex * fm_sin_turns((float)k / 32.0f + 0.25f), dy + ey * fm_sin_turns((float)k / 32.0f), ink);
        cv_line((int32_t)(dx - ex), (int32_t)dy, (int32_t)(dx - ex + 6), y0 + h - 6, ink);
        cv_line((int32_t)(dx + ex), (int32_t)dy, (int32_t)(dx + ex - 6), y0 + h - 6, ink);
        cv_line((int32_t)(dx - ex + 6), y0 + h - 6, (int32_t)(dx + ex - 6), y0 + h - 6, ink);
        {
            float g = (float)(f % 24u) / 24.0f;
            pen_up(&pn);
            for (int k = 0; k <= 24; k++)
                pen_to(&pn, dx + g * ex * fm_sin_turns((float)k / 24.0f + 0.25f), dy + g * ey * fm_sin_turns((float)k / 24.0f),
                       co > 0.5f ? K_DIM : K_FAINT);
        }
    }
}
/* voile: the voice's spectrum: its formants as peaks over the note's harmonics (vosim: timbre's and color's;
 * vowel and choir: the vowel's along timbre, color moving them; whisper: two, sharper with timbre, apart by
 * color), the breath as specks under them */
static void pic_voile(int32_t x0, int32_t y0, int32_t w, int32_t h, const uint8_t *p, uint32_t f)
{
    int md = p[TP_E1] < NVOILE ? p[TP_E1] : 0, np = md == 0 || md == 3 ? 2 : md == 1 ? 3 : 5;
    float tb = (float)p[TP_E2] / 127.0f, co = (float)p[TP_E3] / 127.0f, cx[5], wd, base = (float)(y0 + h - 4);
    uint32_t r = 31u ^ (f / 3);
    pen_t pn = {0};
    if (md == 0) {
        cx[0] = tb;
        cx[1] = co;
    } else if (md == 3) {
        cx[0] = 0.3f;
        cx[1] = fm_clampf(0.3f + (co - 0.5f) * 0.9f, 0.02f, 0.98f);
    } else {
        float sh = 0.8f + 0.4f * co;
        for (int k = 0; k < np; k++)
            cx[k] = fm_minf((0.08f + 0.19f * (float)k + 0.08f * fm_sin_turns(tb * 0.8f + (float)k * 0.31f)) * sh, 0.97f);
    }
    wd = md == 3 ? 0.012f + (1.0f - tb) * 0.05f : 0.035f;
    if (md != 3)
        for (int32_t x = 6; x < w; x += 12)          /* the harmonics */
            cv_rect(x0 + x, y0 + 30, 1, h - 34, K_PANEL2);
    for (int32_t x = 0; x <= w; x += 2) {
        float t = (float)x / (float)w, y = 0.0f;
        for (int k = 0; k < np; k++) {
            float d = (t - cx[k]) / wd;
            y += fm_expf(-d * d) / (1.0f + 0.5f * (float)k);
        }
        pen_to(&pn, (float)(x0 + x), base - fm_minf(y, 1.2f) * (float)(h - 14), K_NOW);
    }
    for (int k = 0; k < p[TP_E4] / 3; k++) {
        r = r * 1664525u + 1013904223u;
        cv_rect(x0 + (int32_t)((r >> 8) % (uint32_t)w), y0 + h - 4 - (int32_t)((r >> 20) % 24u), 2, 2, K_DIM);
    }
}
/* the sample engine: the sample's waveform (each column its lowest and highest), the region that plays bright,
 * the rest faint, the loop's ends marked; its name, root and the mode above */
static int8_t smp_col_lo[200], smp_col_hi[200];
static int smp_col_for = -1;
static uint32_t smp_col_bank;
static void smp_columns(int idx, const smp_t *sm, int w)
{
    smp_cache_t c;
    uint32_t bank = (uint32_t)smp_user_count() << 16 ^ smp_user_bytes();
    if (idx == smp_col_for && bank == smp_col_bank)
        return;
    smp_col_for = idx;
    smp_col_bank = bank;
    smp_cache_reset(&c);
    for (int x = 0; x < w; x++) {
        int32_t i0 = (int32_t)((uint32_t)x * sm->n / (uint32_t)w), i1 = (int32_t)((uint32_t)(x + 1) * sm->n / (uint32_t)w);
        float lo = 0.0f, hi = 0.0f;
        for (int32_t i = i0; i < i1 || i == i0; i++) {
            float v = smp_one(&c, sm, i);
            lo = v < lo ? v : lo;
            hi = v > hi ? v : hi;
        }
        smp_col_lo[x] = (int8_t)(lo * 127.0f);
        smp_col_hi[x] = (int8_t)(hi * 127.0f);
    }
}
/* what an empty sample's picture says: a moved factory sample (a break since 0.7, the rest on the FM-1 since 0.8:
 * the installer puts them in the bank) is installed from the samples page */
static const char *smp_missing(int idx)
{
    return idx >= smp_factory() ? "empty: fill it on the samples page"
         : idx >= SMP_BREAK && idx < SMP_BREAK + SMP_NBREAK ? "install the breaks: samples page"
         : "install the sounds: samples page";
}
static void pic_sample(int32_t x0, int32_t y0, int32_t w, int32_t h, const uint8_t *p)
{
    int idx = p[TP_E1] < smp_slots() ? p[TP_E1] : smp_slots() - 1, mode = p[TP_E4] % NSM;
    const smp_t *sm = smp_get(idx);
    int32_t wy = y0 + 20, wh = h - 22, cy = wy + wh / 2;
    char t[40], *e;
    static const char *const MODE_ARROW[NSM] = {"once", "loop", "backwards", "forth and back"};
    if (!sm) {
        text_c(x0 + w / 2, cy - 16, &FONT_S, smp_name(idx), K_DIM);
        text_c(x0 + w / 2, cy + 4, &FONT_XS, smp_missing(idx), K_DIM);
        return;
    }
    e = s_cat(t, sm->name);
    e = s_cat(e, ", root ");
    e = note_name(sm->root, e);
    text_fit(x0, y0 + 2, &FONT_XS, t, w - 70, K_TEXT);
    text_r(x0 + w, y0 + 2, &FONT_XS, MODE_ARROW[mode], K_DIM);
    smp_columns(idx, sm, w > 200 ? 200 : w);
    {
        float n = (float)sm->n, s0 = (float)p[TP_E2] * (1.0f / 127.0f), s1;
        int32_t xa, xb, la = -1, lb = -1;
        s0 = (s0 > 0.99f ? 0.99f : s0) * n;
        s1 = s0 + (n - s0) * (float)p[TP_E3] * (1.0f / 127.0f);
        if (s1 < s0 + 32.0f)
            s1 = s0 + 32.0f;
        xa = (int32_t)(s0 * (float)w / n);
        xb = (int32_t)(s1 * (float)w / n);
        if ((mode == SM_LOOP || mode == SM_PINGPONG) && sm->le > sm->ls && (float)sm->ls >= s0 && (float)sm->le <= s1) {
            la = (int32_t)((float)sm->ls * (float)w / n);
            lb = (int32_t)((float)sm->le * (float)w / n);
        }
        if ((mode == SM_LOOP || mode == SM_PINGPONG) && (p[TP_X1] || p[TP_X2])) {   /* the loop page's (synth.c smp_prep) */
            float ls = s0 + (s1 - s0 - 32.0f) * (float)p[TP_X1] * (1.0f / 127.0f), le;
            if (p[TP_X2])
                le = ls + (s1 - ls) * (float)p[TP_X2] * (1.0f / 127.0f);
            else if (sm->le > sm->ls && (float)sm->ls >= ls && (float)sm->le <= s1)
                le = (float)sm->le;
            else
                le = s1;
            if (le < ls + 32.0f)
                le = ls + 32.0f;
            la = (int32_t)(ls * (float)w / n);
            lb = (int32_t)(le * (float)w / n);
        }
        cv_rect(x0, cy, w, 1, K_LINE);
        if (xb > xa)
            cv_rect(x0 + xa, wy, xb - xa, wh, K_TINT);
        for (int32_t x = 0; x < w && x < 200; x++) {
            int32_t yl = cy - smp_col_hi[x] * (wh / 2 - 1) / 127, yh = cy - smp_col_lo[x] * (wh / 2 - 1) / 127;
            cv_rect(x0 + x, yl, 1, yh - yl + 1, x >= xa && x < xb ? K_NOW : K_FAINT);
        }
        if (la >= 0) {
            cv_rect(x0 + la, wy, 1, wh, K_TEXT);
            cv_rect(x0 + lb - 1, wy, 1, wh, K_TEXT);
        }
        if (mode == SM_REVERSE)                      /* where it starts: the region's end */
            cv_round(x0 + xb - 3, wy, 6, 6, 3, K_TEXT);
        else
            cv_round(x0 + xa - 3 < x0 ? x0 : x0 + xa - 3, wy, 6, 6, 3, K_TEXT);
    }
}

/* tulle: the sample's waveform (faint), where its grains come from tinted (the position, spray's width about it) and
 * bright, a grain's length marked under the position; above, the sample's name and the grains: how many a second, how
 * long, and the scan's arrow when it moves */
#define TL_SHOWN 16                                  /* grains drawn at most (four notes of four) */
static int32_t tl_gpos[TL_SHOWN];                    /* the grains sounding, as draw_engine read them */
static uint8_t tl_glvl[TL_SHOWN];
static int tl_ng;
static void pen_oval(float cx, float cy, float rx, float ry, uint16_t c)   /* an ellipse, 24 strokes */
{
    pen_t pn = {0};
    for (int k = 0; k <= 24; k++)
        pen_to(&pn, cx + rx * fm_sin_turns((float)k / 24.0f + 0.25f), cy + ry * fm_sin_turns((float)k / 24.0f), c);
}
/* lace: the thing that rings, by its material (a bar on its cords, the marimba's over its tube, a wood block, a church
 * bell on its headstock and wheel, a wine glass, a plate with sand on it, a string, a gong on its frame), shaking as it
 * sounds (pic_lvl), and what sets it ringing at the struck place (position): a mallet that falls as a note strikes (its
 * head soft and fuzzy or small and hard by hardness), a breath of noise, a bow sawing. bright: sparks off the struck
 * place as it sounds, more and longer; damp: a felt pad pressed on it, wider */
static void pic_lace(int32_t x0, int32_t y0, int32_t w, int32_t h, const uint8_t *p, uint32_t f)
{
    int m = p[TP_E1] < NLACE ? p[TP_E1] : NLACE - 1, ex = p[TP_X1] % 3;
    float lv = pic_lvl, pos = 0.03f + 0.47f * (float)p[TP_E4] / 127.0f, cx = (float)(x0 + w / 2);
    float cy = (float)(y0 + h / 2), sh = lv * fm_sin_turns((float)f * 0.37f), sx = cx, sy = cy, k8 = (float)h / 88.0f;
    float br = (float)p[TP_E2] / 127.0f, dp = (float)p[TP_E3] / 127.0f, hd = (float)p[TP_X4] / 127.0f, dx = cx, dy = cy;
    uint16_t ink = K_NOW;
    pen_t a = {0}, b = {0};
    if (m == 0 || m == 1 || m == 6) {            /* a bar (its first mode: the ends up, the middle down) */
        float len = m == 6 ? 64.0f : 150.0f, t = m == 6 ? 22.0f : 11.0f, l = cx - len * 0.5f, top = cy - (m == 1 ? 14.0f * k8 : 4.0f);
        if (m == 1)                              /* (the marimba's tube) */
            cv_frame((int32_t)cx - 9, (int32_t)(top + t + 4.0f), 18, y0 + h - 4 - (int32_t)(top + t + 4.0f), 6, K_DIM, K_BG);
        for (int k = 0; k <= 20; k++) {
            float u = (float)k / 20.0f, d = (m == 6 ? 1.5f : 5.0f) * sh * fm_sin_turns(u + 0.25f);
            pen_to(&a, l + u * len, top + d, ink);
            pen_to(&b, l + u * len, top + t + d, ink);
        }
        cv_line((int32_t)l, (int32_t)(top + 5.0f * sh), (int32_t)l, (int32_t)(top + t + 5.0f * sh), ink);
        cv_line((int32_t)(l + len), (int32_t)(top + 5.0f * sh), (int32_t)(l + len), (int32_t)(top + t + 5.0f * sh), ink);
        if (m == 6)                              /* (the block's slit, its base) */
            cv_rect((int32_t)l + 8, (int32_t)(top + t * 0.5f), (int32_t)len - 16, 2, K_DIM),
            cv_rect((int32_t)l - 10, (int32_t)(top + t + 3.0f), (int32_t)len + 20, 1, K_LINE);
        else
            for (int k = 0; k < 2; k++)          /* (the cords at its nodes) */
                cv_dotf(l + len * (k ? 0.776f : 0.224f), top + t + 3.0f, 2.0f, K_DIM);
        sx = l + pos * len;
        sy = top;
        dx = l + len * 0.88f;
        dy = top + 5.0f * sh;
    } else if (m == 2) {                         /* a church bell: squat, hung from its headstock on a beam, its wheel
                                                  * beside, swinging on the pivot */
        float tp = (float)y0 + 12.0f * k8, ht = (float)h - 20.0f * k8, sw = 5.0f * sh;
        cv_rect((int32_t)cx - 50, y0 + 3, 100, 4, K_DIM);                       /* the beam */
        cv_round((int32_t)cx - 12, (int32_t)tp - 6, 24, 6, 2, K_DIM);          /* the headstock */
        pen_oval(cx + 34.0f, (float)y0 + 5.0f + 9.0f * k8, 9.0f * k8, 9.0f * k8, K_FAINT);   /* the wheel */
        for (int k = 0; k <= 8; k++)             /* (its crown, rounded) */
            pen_to(&a, cx - 20.0f + 5.0f * (float)k, tp - 5.0f * k8 * fm_sin_turns(0.0625f * (float)k), ink);
        pen_up(&a);
        for (int k = 0; k <= 16; k++) {          /* (its waist, then the flare to the lip) */
            float t = (float)k / 16.0f, t2 = t * t, hw = 20.0f + 6.0f * t + 15.0f * t2 * t2 * t, x = cx + sw * t;
            pen_to(&a, x - hw, tp + t * ht, ink);
            pen_to(&b, x + hw, tp + t * ht, ink);
        }
        cv_line((int32_t)(cx + sw - 41.0f), (int32_t)(tp + ht), (int32_t)(cx + sw + 41.0f), (int32_t)(tp + ht), ink);
        cv_line((int32_t)(cx + sw - 38.0f), (int32_t)(tp + ht - 4.0f), (int32_t)(cx + sw + 38.0f), (int32_t)(tp + ht - 4.0f),
                K_DIM);                                                         /* (its sound bow) */
        sx = cx + sw + 39.0f - 22.0f * pos;
        sy = tp + ht - 8.0f;
        dx = cx + sw - 30.0f;
        dy = tp + ht - 8.0f;
    } else if (m == 3) {                         /* a wine glass: its rim an ellipse breathing in and out (its mode) */
        float rim = (float)y0 + 14.0f * k8, rx = 26.0f + 3.0f * sh, bh = 36.0f * k8;
        pen_oval(cx, rim, rx, 4.0f - 1.5f * sh, ink);
        for (int k = 0; k <= 14; k++) {
            float u = (float)k / 14.0f, hw = rx * fm_sqrtf(fm_maxf(1.0f - u * u, 0.0f)) * (1.0f - 0.1f * u);
            pen_to(&a, cx - hw, rim + u * bh, ink);
            pen_to(&b, cx + hw, rim + u * bh, ink);
        }
        cv_line((int32_t)cx, (int32_t)(rim + bh), (int32_t)cx, y0 + h - 8, ink);
        pen_oval(cx, (float)(y0 + h - 7), 16.0f, 2.5f, ink);
        sx = cx + rx * (1.0f - 2.0f * pos);
        sy = rim;
        dx = cx - rx * 0.6f;
        dy = rim + bh * 0.5f;
    } else if (m == 4) {                         /* a plate seen from above, sand gathering on its nodal lines */
        float pl = cx - 70.0f, pt = (float)y0 + 12.0f, pw = 140.0f, ph = (float)h - 26.0f;
        int na = 1 + (int)(pos * 6.0f), nb = na + 2;
        cv_frame((int32_t)pl, (int32_t)(pt + sh), (int32_t)pw, (int32_t)ph, 3, ink, K_BG);
        for (int gx = 1; gx < 36; gx++)
            for (int gy = 1; gy < 14; gy++) {
                float u = (float)gx / 36.0f, v = (float)gy / 14.0f;
                float z = fm_sin_turns(0.5f * (float)na * u) * fm_sin_turns(0.5f * (float)nb * v) -
                          fm_sin_turns(0.5f * (float)nb * u) * fm_sin_turns(0.5f * (float)na * v);
                if (fm_fabsf(z) < 0.12f + 0.1f * lv)
                    cv_rect((int32_t)(pl + u * pw), (int32_t)(pt + sh + v * ph), 1, 1, lv > 0.1f ? K_NOW : K_DIM);
            }
        sx = pl + pw * pos;
        sy = pt;
        dx = pl + pw * 0.85f;
        dy = pt + sh;
    } else if (m == 5) {                         /* a string between its pegs, its first two modes */
        float l = cx - 80.0f, len = 160.0f, sy0 = cy + 10.0f;
        cv_round((int32_t)l - 6, (int32_t)sy0 - 8, 6, 16, 2, K_DIM);
        cv_round((int32_t)(l + len), (int32_t)sy0 - 8, 6, 16, 2, K_DIM);
        for (int k = 0; k <= 24; k++) {
            float u = (float)k / 24.0f;
            pen_to(&a, l + u * len, sy0 + 9.0f * sh * fm_sin_turns(0.5f * u) +
                                    3.0f * lv * fm_sin_turns(u) * fm_sin_turns((float)f * 0.21f), ink);
        }
        sx = l + pos * len;
        sy = sy0;
        dx = l + len * 0.92f;
        dy = sy0;
    } else if (m == 8) {                         /* a singing bowl on its cushion, its rim breathing (its pairs beat) */
        float rim = cy - 10.0f * k8, rx = 40.0f + 3.0f * sh, bh = 26.0f * k8;
        pen_oval(cx, rim, rx, 5.0f - 2.0f * sh, ink);
        for (int k = 0; k <= 14; k++) {
            float u = (float)k / 14.0f, hw = rx * fm_sqrtf(fm_maxf(1.0f - u * u * 0.85f, 0.0f));
            pen_to(&a, cx - hw, rim + u * bh, ink);
            pen_to(&b, cx + hw, rim + u * bh, ink);
        }
        cv_line((int32_t)(cx - 15.0f), (int32_t)(rim + bh), (int32_t)(cx + 15.0f), (int32_t)(rim + bh), ink);
        cv_round((int32_t)cx - 30, (int32_t)(rim + bh + 1.0f), 60, (int32_t)(8.0f * k8) + 2, 4, K_DIM);   /* its cushion */
        sx = cx + rx * (1.0f - 2.0f * pos);
        sy = rim;
        dx = cx - rx * 0.5f;
        dy = rim + bh * 0.6f;
    } else {                                     /* a gong on its frame, its rim trembling */
        float gy = cy + 6.0f * k8, r = 28.0f * k8;
        cv_rect((int32_t)cx - 52, y0 + 4, 104, 2, K_DIM);
        cv_rect((int32_t)cx - 52, y0 + 4, 2, h - 6, K_DIM);
        cv_rect((int32_t)cx + 50, y0 + 4, 2, h - 6, K_DIM);
        cv_line((int32_t)cx - 16, y0 + 6, (int32_t)cx - 8, (int32_t)(gy - r), K_DIM);   /* (the cords) */
        cv_line((int32_t)cx + 16, y0 + 6, (int32_t)cx + 8, (int32_t)(gy - r), K_DIM);
        for (int k = 0; k <= 32; k++) {
            float t = (float)k / 32.0f, rr = r + 2.0f * sh * fm_sin_turns(3.0f * t + (float)f * 0.05f);
            pen_to(&a, cx + rr * fm_sin_turns(t + 0.25f), gy + rr * fm_sin_turns(t), ink);
        }
        pen_oval(cx, gy, 7.0f * k8, 7.0f * k8, ink);
        pen_oval(cx, gy, (18.0f + 2.0f * lv) * k8, (18.0f + 2.0f * lv) * k8, K_FAINT);
        sx = cx - r * (1.0f - 2.0f * pos) * 0.9f;
        sy = gy;
        dx = cx + r * 0.5f;
        dy = gy + r * 0.6f;
    }
    if (dp > 0.08f)                              /* damp: a felt pad on it */
        cv_round((int32_t)(dx - 2.0f - 7.0f * dp), (int32_t)dy - 3, (int32_t)(4.0f + 14.0f * dp), 6, 3, K_DIM);
    if (lv > 0.05f)                              /* bright: sparks off the struck place */
        for (int k = 0; k < 2 + (int)(br * 6.0f); k++) {
            float an = 0.55f + 0.4f * ((float)k / 7.0f) + 0.03f * fm_sin_turns((float)f * 0.13f + (float)k),
                  r0 = 6.0f, r1 = 6.0f + (4.0f + 12.0f * br) * lv;
            cv_line((int32_t)(sx + r0 * fm_sin_turns(an + 0.25f)), (int32_t)(sy - r0 * fm_sin_turns(an)),
                    (int32_t)(sx + r1 * fm_sin_turns(an + 0.25f)), (int32_t)(sy - r1 * fm_sin_turns(an)), K_FAINT);
        }
    if (ex == 0) {                               /* the mallet: down on the strike, lifting as it rings; its head soft
                                                  * and fuzzy, or small and hard */
        float lift = (1.0f - lv) * 14.0f * k8, hr = 3.0f + 3.0f * (1.0f - hd), hy = sy - 3.0f - hr - lift;
        cv_linef(sx + 26.0f, sy - 30.0f * k8 - lift * 0.5f, sx + 2.0f, hy, K_DIM, 2.0f);
        cv_dotf(sx, hy, hr, K_NOW);
        if (hd < 0.6f)                           /* (yarn) */
            for (int k = 0; k < 10; k++) {
                float t = (float)k / 10.0f, fz = hr + 1.0f + (0.6f - hd) * 3.0f * (0.5f + 0.5f * fm_sin_turns(t * 3.0f + (float)f * 0.05f));
                cv_rect((int32_t)(sx + fz * fm_sin_turns(t + 0.25f)), (int32_t)(hy + fz * fm_sin_turns(t)), 1, 1, K_DIM);
            }
    } else if (ex == 1) {                        /* a breath: specks drifting in */
        for (int k = 0; k < 10; k++) {
            float t = (float)k / 10.0f + (float)f * 0.02f;
            t -= fm_floorf(t);
            cv_dotf(sx - 34.0f + 30.0f * t, sy - 12.0f + 8.0f * t + 3.0f * fm_sin_turns(t * 2.0f + (float)k * 0.3f),
                    1.2f, lv > 0.1f ? K_NOW : K_DIM);
        }
    } else {                                     /* the bow, sawing across the struck place */
        float o = (8.0f + 18.0f * lv) * fm_sin_turns((float)f * 0.03f);
        cv_line((int32_t)(sx - 40.0f + o), (int32_t)sy - 3, (int32_t)(sx + 40.0f + o), (int32_t)sy - 3, K_NOW);
        cv_line((int32_t)(sx - 40.0f + o), (int32_t)sy - 8, (int32_t)(sx + 40.0f + o), (int32_t)sy - 6, K_DIM);
        cv_round((int32_t)(sx + 36.0f + o), (int32_t)sy - 9, 7, 7, 2, K_DIM);
    }
}
/* tartan: the chord as an aurora (after Chordism's): over a few twinkling stars on the background, each voice a curtain of
 * light in the track's ink, hanging at its place in the field, its foot higher for a higher note,
 * as tall and bright as its level (the morphs: the relative levels read first), drifting as the sound plays (pic_f: at
 * rest in silence), more by sway; the chord's name above */
static void tt_curtain(float cx, float half, float foot0, float level, uint16_t ink, float t, float seed, int32_t ytop)
{
    if (level <= 0.01f)
        return;
    for (int32_t x = ((int32_t)(cx - half) + 1) & ~1; x <= (int32_t)(cx + half); x += 2) {   /* (two pixels a column) */
        float u = ((float)x - cx) / half, env, sway;
        int32_t foot, hgt, a0;
        if (x < 0 || x >= 239 || u <= -1.0f || u >= 1.0f)
            continue;
        env = 1.0f - u * u;
        sway = fm_sin_turns((float)x * 0.011f + t * 0.21f + seed) * 0.5f + fm_sin_turns((float)x * 0.031f - t * 0.37f + seed * 2.0f) * 0.5f;
        foot = (int32_t)(foot0 + 5.0f * sway);
        hgt = (int32_t)(level * env * (42.0f + 30.0f * (0.5f + 0.5f * fm_sin_turns((float)x * 0.02f + t * 0.13f + seed * 3.0f))));
        if (foot - hgt < ytop)
            hgt = foot - ytop;
        a0 = (int32_t)(255.0f * fm_minf(1.2f * env, 1.0f) * (0.6f + 0.4f * level));
        for (int32_t y = 0; y <= hgt; y++) {     /* bright at the foot, fading upward (the square of the way up) */
            int32_t k = hgt - y, a = a0 * k / (hgt + 1) * k / (hgt + 1);
            cv_blend(x, foot - y, ink, (uint32_t)a);
            cv_blend(x + 1, foot - y, ink, (uint32_t)a);
        }
        cv_blend(x, foot, K_TEXT, (uint32_t)(80.0f * env * level));   /* (the bright hem) */
        cv_blend(x + 1, foot, K_TEXT, (uint32_t)(80.0f * env * level));
    }
}
static void pic_tartan(int32_t x0, int32_t y0, int32_t w, int32_t h, const uint8_t *p, uint32_t f)
{
    float semis[4], lvl[4], pan[4], top = 12.0f, t = (float)f * (0.03f + 0.05f * (float)p[TP_KSUB] / 127.0f);
    float life = 0.35f + 0.65f * pic_lvl;
    const uint16_t ink[4] = {K_NOW, K_NOW, K_NOW, K_NOW};   /* (the track's own ink, every voice: its place, height and
                                                      * brightness tell them apart) */
    tartan_voices(p, semis, lvl, pan);
    for (uint32_t i = 0; i < 22u; i++) {         /* the stars, a few twinkling */
        uint32_t r = (i + 7u) * 2654435761u, a = 60u + (r >> 25);
        if ((r >> 12 & 3u) == 0)
            a = (uint32_t)((float)a * (0.55f + 0.45f * fm_sin_turns((float)f * 0.011f + (float)(r & 255u) * 0.01f)));
        cv_blend((int32_t)(r % 240u), y0 + (int32_t)((r >> 8) % (uint32_t)(h / 2)), K_TEXT, a);
    }
    for (int k = 0; k < 4; k++)
        top = fm_maxf(top, semis[k]);
    for (int k = 0; k < 4; k++) {
        float cx = (float)(x0 + 18) + pan[k] * (float)(w - 36), foot = (float)(y0 + h - 8) - semis[k] / top * (float)(h / 3);
        tt_curtain(cx, 40.0f, foot, lvl[k] * life, ink[k], t, (float)k * 0.37f, y0 + 2);
    }
}
static void pic_tulle(int32_t x0, int32_t y0, int32_t w, int32_t h, const uint8_t *p)
{
    int idx = p[TP_E1] < smp_slots() ? p[TP_E1] : smp_slots() - 1;
    const smp_t *sm = smp_get(idx);
    int32_t wy = y0 + 20, wh = h - 30, cy = wy + wh / 2;
    char t[40], *e;
    if (w > 200)
        w = 200;
    if (!sm) {
        text_c(x0 + w / 2, cy - 16, &FONT_S, smp_name(idx), K_DIM);
        text_c(x0 + w / 2, cy + 4, &FONT_XS, smp_missing(idx), K_DIM);
        return;
    }
    {
        int ms = (int)(10.0f * fm_exp2f((float)p[TP_X1] * (5.643856f / 127.0f)) + 0.5f);
        int gs = (int)(2.0f * fm_exp2f((float)p[TP_X2] * (4.906891f / 127.0f)) + 0.5f);
        e = s_cat(t, sm->name);
        text_fit(x0, y0 + 2, &FONT_XS, t, w - 90, K_TEXT);
        e = s_cat(num_i(gs, t), " a second, ");
        s_cat(num_i(ms, e), " ms");
        text_r(x0 + w, y0 + 2, &FONT_XS, t, K_DIM);
    }
    smp_columns(idx, sm, w);
    {
        float n = (float)sm->n, pos = (float)p[TP_E2] * (1.0f / 127.0f), spr = (float)p[TP_E3] * (0.5f / 127.0f);
        float ms = 10.0f * fm_exp2f((float)p[TP_X1] * (5.643856f / 127.0f));
        int32_t xp = (int32_t)(pos * (float)w), xa = (int32_t)((pos - spr) * (float)w), xb = (int32_t)((pos + spr) * (float)w);
        int32_t gw = (int32_t)(ms * 0.001f * (float)sm->rate * (float)w / n);
        xa = xa < 0 ? 0 : xa;
        xb = xb > w - 1 ? w - 1 : xb;
        if (xb > xa)
            cv_rect(x0 + xa, wy, xb - xa + 1, wh, K_TINT);
        cv_rect(x0, cy, w, 1, K_LINE);
        for (int32_t x = 0; x < w; x++) {
            int32_t yl = cy - smp_col_hi[x] * (wh / 2 - 1) / 127, yh = cy - smp_col_lo[x] * (wh / 2 - 1) / 127;
            cv_rect(x0 + x, yl, 1, yh - yl + 1, x >= xa && x <= xb ? K_NOW : K_FAINT);
        }
        cv_round(x0 + xp - 3 < x0 ? x0 : x0 + xp - 3, wy, 6, 6, 3, K_TEXT);
        for (int k = 0; k < tl_ng; k++) {            /* each grain sounding: a line where it reads, as tall as it is
                                                      * loud (its window rising and falling) */
            int32_t gx = (int32_t)((float)tl_gpos[k] * (float)w / n), gh = 4 + tl_glvl[k] * (wh - 4) / 255;
            if (gx >= 0 && gx < w) {
                cv_rect(x0 + gx - 1, cy - gh / 2, 3, gh, K_TEXT);
                cv_round(x0 + gx - 3, cy - gh / 2 - 3, 7, 7, 3, K_TEXT);   /* (its head: easy to follow) */
            }
        }
        gw = gw < 2 ? 2 : gw > w - xp ? w - xp : gw;
        cv_rect(x0 + xp, wy + wh + 3, gw, 2, K_TEXT);  /* a grain's length */
        if (p[TP_E4])                                 /* the scan moves on: an arrow after the position */
            text_c(x0 + xp + 12 > x0 + w - 6 ? x0 + w - 6 : x0 + xp + 12, wy + wh + 1, &LFONT_XS, ">", K_DIM);
    }
}
/* the slice engine: the loop's waveform cut by its slice lines, each slice's gated part drawn and what the gate
 * leaves out faint; the slice playing tinted and bright, a line where it plays; a dot on slice 1 (the root's).
 * Above: the loop's name and bars (or its own speed), the count or the slice playing */
static void pic_slice(int32_t x0, int32_t y0, int32_t w, int32_t h, const uint8_t *p, int trk)
{
    int idx = p[TP_E1] < smp_slots() ? p[TP_E1] : smp_slots() - 1, ns = sl_count(p[TP_E2]), sl = -1, on;
    const smp_t *sm = smp_get(idx);
    int32_t wy = y0 + 20, wh = h - 22, cy = wy + wh / 2, pos = 0, px = -1;
    int32_t gate = (p[TP_E4] > 127 ? 127 : p[TP_E4]) * 1000 / 127;
    char t[40], *e;
    if (gate < 20)
        gate = 20;                                   /* (as the engine: 2 % at least) */
    if (w > 200)
        w = 200;
    if (!sm) {
        text_c(x0 + w / 2, cy - 16, &FONT_S, smp_name(idx), K_DIM);
        text_c(x0 + w / 2, cy + 4, &FONT_XS, smp_missing(idx), K_DIM);
        return;
    }
    e = s_cat(t, sm->name);
    if (sm->bars) {
        e = num_s(sm->bars, s_cat(e, ", "));
        e = s_cat(e, sm->bars > 1 ? " bars" : " bar");
    } else {
        e = s_cat(e, ", own speed");
    }
    text_fit(x0, y0 + 2, &FONT_XS, t, w - 76, K_TEXT);
    on = engine_slice_now(trk, &sl, &pos) && sl >= 0 && sl < ns;
    if (on) {
        e = num_s((uint32_t)sl + 1u, s_cat(t, "slice "));
        num_s((uint32_t)ns, s_cat(e, " of "));
        px = (int32_t)((float)pos * (float)w / (float)sm->n);
    } else {
        s_cat(num_s((uint32_t)ns, t), " slices");
    }
    text_r(x0 + w, y0 + 2, &FONT_XS, t, on ? K_TEXT : K_DIM);
    smp_columns(idx, sm, w);
    if (on)
        cv_rect(x0 + sl * w / ns, wy, (sl + 1) * w / ns - sl * w / ns, wh, K_TINT);
    cv_rect(x0, cy, w, 1, K_LINE);
    for (int32_t x = 0; x < w; x++) {
        int k = x * ns / w;
        int32_t a = k * w / ns, b = (k + 1) * w / ns, in = (x - a) * 1000 < gate * (b - a);
        int32_t yl = cy - smp_col_hi[x] * (wh / 2 - 1) / 127, yh = cy - smp_col_lo[x] * (wh / 2 - 1) / 127;
        cv_rect(x0 + x, yl, 1, yh - yl + 1, !in ? K_FAINT : on && k == sl ? K_NOW : K_DIM);
    }
    for (int k = 1; k < ns; k++) {                   /* the cuts: a gap through the wave, a tick above and below */
        cv_rect(x0 + k * w / ns, wy, 1, wh, K_BG);
        cv_rect(x0 + k * w / ns, wy, 1, 3, K_LINE);
        cv_rect(x0 + k * w / ns, wy + wh - 3, 1, 3, K_LINE);
    }
    if (px >= 0 && px < w)
        cv_rect(x0 + px, wy, 1, wh, K_TEXT);
    cv_round(x0, wy, 6, 6, 3, K_TEXT);              /* slice 1 */
}

/* the drumline: eleven pads, each key's model, the shown one ringed, hits flashing */
static uint8_t pad_lvl[NDRUM];                       /* each pad's flash as draw_engine last eased it */
static uint32_t pad_a(int k) { return pad_lvl[k] >= 200u ? 16u : pad_lvl[k] * 16u / 200u; }   /* its ink, of 16 */
#define PIC_VEILED 0xFFFFFFFFu                      /* pic_drumline's f: a silent kit's faint pads */
static void pic_drumline(int32_t x0, int32_t y0, int32_t w, int32_t h, const uint8_t *p, uint32_t f)
{
    int32_t pw = w / 6, ph = h / 2;
    (void)f;
    for (int k = 0; k < DL_NSOUND; k++) {
        int32_t x = x0 + (k % 6) * pw + 2, y = y0 + (k / 6) * ph + 2;
        if (f == PIC_VEILED) {                       /* a silent kit: the pads faint and bare, under kit_silent_veil */
            cv_rect(x, y, pw - 4, ph - 4, col_mix(K_PANEL, K_BG, 10));
            continue;
        }
        uint32_t a = pad_a(k);                       /* the flash fades (a retrigger brightens it: no dark frame
                                                      * between two hits); the text dark on a pad lit past half */
        uint16_t base = k == ui.dsound ? K_PANEL2 : K_PANEL;
        static const char *const PAD[] = {"kick", "snare", "hat", "clap", "shakr", "tamb", "strng", "cong", "tom", "fm",
                                             "smpl", "xkick", "xsnr", "xhat", "xclap", "xtom", "xrim", "xfm",
                                             "xnois", "clang", "sophi", "9kick", "9snr", "9tom", "9rim", "9clap",
                                             "9hat", "9open", "9ride", "9crsh"};
        static const char *const PAD4[] = {"kick", "snr", "hat", "clap", "shk", "tmb", "strg", "cga", "tom", "fm",
                                              "smp", "xkck", "xsnr", "xhat", "xclp", "xtom", "xrim", "xfm",
                                              "xnoi", "clng", "soph", "9kck", "9snr", "9tom", "9rim", "9clp",
                                              "9hat", "9opn", "9rid", "9crs"};   /* (large type: four letters at most) */
        _Static_assert(sizeof PAD / sizeof PAD[0] == NDM && sizeof PAD4 / sizeof PAD4[0] == NDM, "a pad name for each model");
        const char *nm = (LARGE ? PAD4 : PAD)[p[TP_DRUM(k, DP_MODEL)] % NDM];
        char t[4];
        cv_round(x, y, pw - 4, ph - 4, 4, col_mix(base, K_NOW, a));
        if (k == ui.dsound)
            cv_frame(x, y, pw - 4, ph - 4, 4, K_SEL, col_mix(K_PANEL2, K_NOW, a));
        if (LARGE) {                                 /* large type: the name alone, the pads in the black keys' order */
            const felucca_font_t *pf = text_w(&FONT_XS, nm) <= pw - 8 ? &FONT_XS : &LFONT_S;   /* (2 px each side) */
            text_c(x + (pw - 4) / 2, cap_y(y, ph - 4, pf), pf, nm, a >= 8u ? K_BG : K_TEXT);
            continue;
        }
        {
            int32_t tw = text_w(&FONT_XS, nm);
            text_fit(tw <= pw - 9 ? x + (pw - 4 - tw) / 2 : x + 3, y + (ph - 4) / 2 - 6, &FONT_XS, nm, pw - 9,
                     a >= 8u ? K_BG : K_TEXT);
        }
        num_s((uint32_t)k + 1u, t);
        cv_text(x + 3, y, &FONT_XS, t, a >= 8u ? K_BG : K_FAINT);
    }
}
static void pic_clock(int trk)                       /* (the clock eases out: its speed follows the sound up at once and
                                                      * down over ~half a second, so a picture slows to rest, not stops) */
{
    static uint32_t last;
    static float ft, spd;
    static int shown = -1;
    float q = (float)ui_peak[trk] * (1.0f / 16383.5f);   /* (meters_take's) */
    if (trk != shown) {                              /* another track: its own level and speed, not the last one's fade */
        shown = trk;
        pic_lvl = 0.0f;
        spd = q > 0.005f ? 1.0f : 0.0f;
    }
    pic_lvl = fm_maxf(fm_minf(q * 2.0f, 1.0f), pic_lvl * 0.86f);
    if (pic_lvl < 0.01f)
        pic_lvl = 0.0f;
    spd += ((pic_lvl > 0.0f ? 1.0f : 0.0f) - spd) * (pic_lvl > 0.0f ? 0.5f : 0.06f);
    if (spd < 0.02f && pic_lvl <= 0.0f)
        spd = 0.0f;
    ft += spd * (float)(ui.frame - last);
    if (ft >= 16777216.0f)                           /* (a float counts whole frames exactly to 2^24) */
        ft -= 16777216.0f;
    pic_f = (uint32_t)ft;
    last = ui.frame;
}
/* a track's parameters as they sound now: its own, a lock's or a motion's value over each (engine_live): what the
 * pages' pictures draw, so a recorded motion moves them as it plays back (one buffer: one track at a time) */
static const uint8_t *par_now(int trk)
{
    static uint8_t now[NTP];
    const uint8_t *par = proj.trk[trk].par;
    for (int i = 0; i < NTP; i++) {
        int lv = engine_live(trk, i);
        now[i] = lv >= 0 ? (uint8_t)lv : par[i];
    }
    return now;
}
/* the MIDI engine: a MIDI port in the middle, lit as a note goes out; its eight knobs' values as thin bars either
 * side, four and four, each with its CC under it, the page's four bright */
static void pic_midi(int32_t x0, int32_t y0, const uint8_t *p, int second, int hit)
{
    static const int8_t PIN[5][2] = {{-13, 0}, {-9, -9}, {0, -13}, {9, -9}, {13, 0}};   /* DIN-5, 180 degrees */
    int32_t cx = 120, cy = y0 + 30;
    char t[8];
    for (int k = 0; k < 8; k++) {
        int v = p[k < 4 ? TP_E1 + k : TP_X1 + k - 4], hb = 1 + v * 55 / 127, on = (k >= 4) == (second != 0);
        int32_t x = k < 4 ? x0 + k * 20 : 2 * cx - 4 - x0 - (7 - k) * 20;   /* (the right four: the left's mirror) */
        cv_round(x, y0, 4, 56, 2, K_LINE);
        cv_round(x, y0 + 56 - hb, 4, hb, 2, on ? K_NOW : K_FAINT);
        num_s(p[midi_cc_par(k)] % NMIDI_CC, t);
        text_c(x + 2, y0 + 62, &FONT_XS, t, on ? K_TEXT : K_DIM);
    }
    cv_round(cx - 25, cy - 25, 50, 50, 25, hit > 40 ? K_NOW : K_LINE);
    cv_round(cx - 21, cy - 21, 42, 42, 21, hit > 40 ? K_NOW : K_BG);
    cv_round(cx - 3, cy + 18, 6, 5, 1, hit > 40 ? K_BG : K_LINE);   /* the key */
    for (int i = 0; i < 5; i++)
        cv_round(cx + PIN[i][0] - 2, cy + PIN[i][1] - 2, 5, 5, 2, hit > 40 ? K_BG : K_TEXT);
}
/* a silent kit (three kits sound at once): over its faint pads (pic_drumline PIC_VEILED), a box saying why */
static void kit_silent_veil(int32_t x0, int32_t y0, int32_t w, int32_t h)
{
    int32_t bw = w - 40, bh = LARGE ? 64 : 46, bx = x0 + (w - bw) / 2, by = y0 + (h - bh) / 2;
    cv_frame(bx, by, bw, bh, 6, K_REC, K_PANEL2);
    text_c(bx + bw / 2, by + (LARGE ? 8 : 6), &FONT_B, "SILENT", K_REC);
    text_c(bx + bw / 2, by + (LARGE ? 36 : 26), &FONT_S, "3 kits at most", K_TEXT);
}
static void draw_engine(void)
{
    int trk = sel_trk(), eg = proj.trk[trk].par[TP_ENGINE] % NENGINE;
    const uint8_t *p = par_now(trk);
    uint32_t h;
    if (!main_r0)                                    /* (its clock moves once a frame, not once a strip) */
        pic_clock(trk);
    h = hash_u(hash_u(2166136261u, 0xE0u), (uint32_t)trk | (uint32_t)eg << 4 | (uint32_t)ui.dsound << 8 |
                                          (eg == EG_SAMPLE || eg == EG_SLICE || eg == EG_TULLE ? 0u : (pic_f / 2) << 12));
    h = hash_u(h, (uint32_t)(pic_lvl * 32.0f));
    for (int i = 0; i < 4; i++)
        h = hash_u(h, p[TP_E1 + i]);
    if (eg == EG_ACID)                               /* (its picture draws its filter page too) */
        h = hash_u(h, (uint32_t)p[TP_CUT] | (uint32_t)p[TP_RES] << 8 | (uint32_t)p[TP_FENV] << 16 | (uint32_t)p[TP_D] << 24);
    if (eg == EG_MIDI) {                             /* (the second page's values, the eight CCs, which page; a note
                                                      * out: the port lit, easing out) */
        if (!main_r0)
            eng_midi_hit[trk] = (uint8_t)(eng_midi_hit[trk] > 20 ? eng_midi_hit[trk] - 20 : 0);
        h = hash_u(h, eng_midi_hit[trk] > 40);
    }
    if (eg == EG_MIDI)
        for (int k = 0; k < 8; k++)
            h = hash_u(h, (uint32_t)p[k < 4 ? TP_E1 + k : TP_X1 + k - 4] | (uint32_t)p[midi_cc_par(k)] << 8 |
                              (uint32_t)ui.page << 16);
    if (eg == EG_SAMPLE || eg == EG_SLICE || eg == EG_TULLE)   /* (the bank: an upload changes what the picture shows) */
        h = hash_u(h, (uint32_t)smp_user_count() << 16 ^ smp_user_bytes());
    if (eg == EG_TULLE) {                            /* (its Sub page's spray and scan are drawn; its grains, live) */
        h = hash_u(h, (uint32_t)p[TP_X1] | (uint32_t)p[TP_X2] << 8);
        tl_ng = engine_tulle_grains(trk, tl_gpos, tl_glvl, TL_SHOWN);
        for (int k = 0; k < tl_ng; k++)
            h = hash_u(h, (uint32_t)tl_gpos[k] >> 6 | (uint32_t)(tl_glvl[k] >> 4) << 24);
    }
    if (eg == EG_SAMPLE)                             /* (its loop page's knobs mark the loop) */
        h = hash_u(h, (uint32_t)p[TP_X1] | (uint32_t)p[TP_X2] << 8 | (uint32_t)ui.page << 16);
    if (eg == EG_SLICE) {                            /* the slice playing and where (a column of the picture) */
        int sl;
        int32_t pos;
        const smp_t *sm = smp_get(p[TP_E1]);
        if (engine_slice_now(trk, &sl, &pos) && sm)
            h = hash_u(h, (uint32_t)(sl + 1) | (uint32_t)((float)pos * 200.0f / (float)sm->n) << 8);
    }
    if (eg == EG_DRUMLINE) {                         /* the flashes ease out by 0.6 a millisecond (10 a frame at 60
                                                      * frames a second), however often this runs */
        static uint32_t fade_ms, fade_acc;
        uint32_t now = plat_ms(), el = now - fade_ms, d;
        fade_ms = now;
        fade_acc += (el > 1000u ? 1000u : el) * 3u;
        d = fade_acc / 5u;
        fade_acc %= 5u;
        for (int k = 0; k < DL_NSOUND; k++) {
            uint32_t v = eng_drum_hit[k];
            eng_drum_hit[k] = (uint8_t)(v > d ? v - d : 0u);
            pad_lvl[k] = eng_drum_hit[k];            /* (what the pads draw: the engine may hit again meanwhile) */
            h = hash_u(h, p[TP_DRUM(k, DP_MODEL)] | pad_a(k) << 8);
        }
    }
    if (eg == EG_TARTAN)                             /* (its Morph page is drawn too: the voices' levels and places) */
        h = hash_u(h, (uint32_t)p[TP_KSYNC] | (uint32_t)p[TP_KSWEEP] << 8 | (uint32_t)p[TP_KCROSS] << 16 |
                          (uint32_t)p[TP_KSUB] << 24);
    if (eg == EG_LACE || eg == EG_TARTAN)   /* (their second pages are drawn: the exciter, stretch;
                                                      * shape, width, detune; the waves, width) */
        h = hash_u(h, (uint32_t)p[TP_X1] | (uint32_t)p[TP_X2] << 8 | (uint32_t)p[TP_X3] << 16 | (uint32_t)p[TP_X4] << 24);
    if (eg == EG_DRUMLINE)
        h = hash_u(h, eng_kit_silent >> trk & 1u);
    if (eg == EG_KNIT)                               /* (osc 2 and which page: Engine and Osc 2 share the picture) */
        h = hash_u(hash_u(h, (uint32_t)p[TP_X1] | (uint32_t)p[TP_X2] << 8 | (uint32_t)p[TP_O2FINE] << 16 |
                                 (uint32_t)p[TP_O2WAVE] << 24), (uint32_t)p[TP_O2SHAPE] | (uint32_t)(ui.page == P_OSC2) << 8);
    h = hash_s(h, proj.trk[trk].name);
    if (!begin_main(h))
        return;
    cv_text(10, 2, &FONT_B, EG_NAME[eg], K_TEXT);
    if (eg == EG_KNIT) {                             /* the page's oscillator: osc 1, or osc 2's tuning */
        char t[24], *e = t;
        int st = clampi((int)p[TP_X1] - 64, -24, 24), ct = (int)p[TP_O2FINE] - 64;
        if (ui.page != P_OSC2)
            s_cat(t, "osc 1");
        else if (!p[TP_X2])
            s_cat(t, "osc 2 is off");
        else {
            e = s_cat(t, "osc 2 ");
            if (st)
                e = s_cat(num_i(st, s_cat(e, st > 0 ? "+" : "")), " st");
            else
                e = s_cat(e, "in unison");
            if (ct)
                s_cat(num_i(ct, s_cat(e, ct > 0 ? ", +" : ", ")), " ct");
        }
        cv_text(50, 6, &FONT_S, t, K_DIM);
    }
    if (eg == EG_TARTAN)                             /* its chord */
        cv_text(56, 6, &FONT_S, tp_info(TP_E1, EG_TARTAN)->names[p[TP_E1] % TT_NCHORD], K_DIM);
    if (eg == EG_LACE) {                             /* what sets it ringing */
        static const char *const EX[3] = {"struck", "blown", "bowed"};
        cv_text(50, 6, &FONT_S, EX[p[TP_X1] % 3], K_DIM);
    }
    if (eg == EG_MIDI) {                             /* where its notes and CCs go */
        char ch[16];
        num_s((uint32_t)trk + 1u, s_cat(ch, "channel "));
        cv_text(60, 6, &FONT_S, ch, K_DIM);
    }
    {
        char nm[NAME_LEN + 1];
        for (int i = 0; i < NAME_LEN; i++)
            nm[i] = proj.trk[trk].name[i];
        nm[NAME_LEN] = 0;
        if (nm[0])
            text_r(230, 3, &FONT_S, nm, K_DIM);
    }
    switch (eg) {
    case EG_JACQUARD: pic_wave(20, 24, 200, PIC_H, p, pic_f); break;
    case EG_RAYON: pic_rayon(20, 24, 200, PIC_H, p, pic_f); break;
    case EG_BELLOWS: pic_organ(20, 24, 200, PIC_H, p, pic_f); break;
    case EG_SAMPLE: pic_sample(20, 24, 200, PIC_H, p); break;
    case EG_SLICE: pic_slice(20, 24, 200, PIC_H, p, trk); break;
    case EG_TULLE: pic_tulle(20, 24, 200, PIC_H, p); break;
    case EG_CANE: pic_cane(20, 24, 200, PIC_H, p, pic_f); break;
    case EG_LACE: pic_lace(20, 24, 200, PIC_H, p, pic_f); break;
    case EG_TARTAN: pic_tartan(20, 24, 200, PIC_H, p, pic_f); break;
    case EG_VOILE: pic_voile(20, 24, 200, PIC_H, p, pic_f); break;
    case EG_KNIT: pic_knit(20, 24, 200, PIC_H, p, pic_f, ui.page == P_OSC2); break;
    case EG_MIDI: pic_midi(24, 30, p, ui.page == P_SUB, eng_midi_hit[trk]); break;
    case EG_ACID: pic_acid(20, 24, 200, PIC_H, p); break;
    default:                                         /* the kit (large type: the pads fill the band, 4 px in) */
    {
        int silent = eng_kit_silent >> trk & 1u;     /* a fourth kit: no drum unit for it (engine.c NDL) */
        if (LARGE)
            pic_drumline(2, 26, 236, MAIN_H - 26, p, silent ? PIC_VEILED : pic_f);
        else
            pic_drumline(10, 26, 220, 90, p, silent ? PIC_VEILED : pic_f);
        if (silent)
            kit_silent_veil(LARGE ? 2 : 10, 26, LARGE ? 236 : 220, LARGE ? MAIN_H - 26 : 90);
    }
        break;
    }
    main_blit();
}

/* the aux tracks' pages */
/* the conductor in large type (82 px): the key at 20 px (its own knobs show it at 31 on Engine), with the degree on
 * Engine; then what the page says (12 px, as the hints), and the tracks that follow as today's 22 px chips */
static void brain_large(const uint8_t *p, int key, int sc, int deg, int src)
{
    char t[48], *e;
    int32_t cy;                                      /* the chips' row */
    e = s_cat(s_cat(t, NOTE_NAME[key]), " ");
    s_cat(e, BS_NAME[sc]);
    cv_text(12, cap_y(0, 26, &FONT_B), &FONT_B, t, K_TEXT);
    if (ui.page == P_PLAY) {
        cv_text(12, 28, F_CAP, "routing: black keys 1 to 8 toggle a track", K_DIM);
        cv_text(12, 42, F_CAP, "tracks that follow", K_DIM);
        cy = 58;
    } else if (ui.page == P_SUB) {                   /* (the follow track's name: on its knob, and in the line) */
        /* (what the chips are: Engine says it over the same chips; here the follow track's two lines) */
        if (!src)
            cv_text(12, 29, F_CAP, "off: the chords come from the", K_DIM), cv_text(12, 41, F_CAP, "conductor's own keys and steps", K_DIM);
        else if (is_drum(src - 1)) {
            s_cat(s_cat(t, TRACK_NAME[src - 1]), " is a kit: it has no notes");
            cv_text(12, 29, F_CAP, t, K_REC);
        } else {
            s_cat(s_cat(t, TRACK_NAME[src - 1]), "'s notes set the chord:");
            cv_text(12, 29, F_CAP, t, K_DIM);
            cv_text(12, 41, F_CAP, "chords, a bass line; it follows nothing", K_DIM);
        }
        cy = 58;
    } else {
        e = s_cat(t, "degree ");
        if (deg > 0)
            e = s_cat(e, "+");
        num_i(deg, e);
        text_r(230, cap_y(0, 26, &FONT_B), &FONT_B, t, deg ? K_NOW : K_DIM);
        {   /* under the degree: a link, and the active chord's tones (what harmony plays) left of it */
            int32_t r = 230;
            if (p[TP_E4]) {
                text_r(r, 30, &FONT_XS, LINK_NAME[p[TP_E4] % 9], K_DIM);
                r -= text_w(&FONT_XS, LINK_NAME[p[TP_E4] % 9]) + 4;
                text_r(r, 30, &FONT_XS, "link", K_FAINT);
                r -= text_w(&FONT_XS, "link") + 8;
            }
            if (seq.brain_chord) {
                char cb[24], *q = cb;
                *q = 0;
                for (int i = 0; i < SN; i++) {
                    int v = (int)(seq.brain_chord >> (8 * i) & 0xFFu);
                    if (!v)
                        continue;
                    if (q != cb)
                        q = s_cat(q, " ");
                    q = s_cat(q, NOTE_NAME[v % 12]);
                }
                text_r(r, 30, &FONT_XS, cb, K_FAINT);
            }
        }
        cv_text(12, 36, F_CAP, src ? "tracks that follow; the chords' in its colour" : "tracks that follow", K_DIM);
        cy = 52;
    }
    for (int i = 0; i < NTRK; i++) {                 /* (as today's: 22 px, 27 apart) */
        int32_t x = 12 + i * 27;
        int m = proj.trk[i].par[TP_BRAIN] % NBM;
        num_s((uint32_t)i + 1u, t);
        if (src == i + 1) {
            cv_round(x, cy, 22, 22, 5, trk_ink(i));
            text_c(x + 11, cap_y(cy, 22, &FONT_B), &FONT_B, t, K_BG);
            continue;
        }
        if (m == BM_DIATONIC || m == BM_HARMONY)
            cv_round(x, cy, 22, 22, 5, K_TEXT);
        else if (m == BM_TRANSPOSE || m == BM_DEGREE)
            cv_frame(x, cy, 22, 22, 5, K_TEXT, K_BG);
        else
            cv_frame(x, cy, 22, 22, 5, K_LINE, K_BG);
        if (m >= BM_HARMONY)                          /* the harmony modes: a bar under the number (as today's) */
            cv_rect(x + 6, cy + 18, 10, 2, m == BM_HARMONY ? K_BG : K_TEXT);
        text_c(x + 11, cap_y(cy, 22, &FONT_B), &FONT_B, t, m == BM_DIATONIC || m == BM_HARMONY ? K_BG : m ? K_TEXT : K_FAINT);
    }
}
/* a page's title (FONT_B) and the line that says what it does: today under it; large type, right-aligned on the title's
 * line when it fits there (else under it). Returns the y under them */
static int32_t title_sub(const char *title, const char *sub, uint16_t sc)
{
    int32_t tw = cv_text(12, LY(4, cap_y(0, 26, &FONT_B)), &FONT_B, title, K_TEXT);
    if (!LARGE) {
        cv_text(12, 24, F_CAP, sub, sc);
        return 40;
    }
    if (tw + 10 + text_w(F_CAP, sub) <= 230) {
        text_r(230, cap_y(0, 26, F_CAP), F_CAP, sub, sc);
        return 28;
    }
    cv_text(12, 25, F_CAP, sub, sc);
    return 40;
}
static void draw_brain(void)
{
    const uint8_t *p = proj.trk[AUX_BRAIN].par;
    int key = seq.brain_key % 12, sc = seq.brain_scale % NBS, deg = (int8_t)seq.brain_deg, src = p[TP_E1] % 9u;
    uint32_t chord = seq.brain_chord;
    uint32_t h = hash_u(hash_u(hash_u(2166136261u, 0xB0u), (uint32_t)key | (uint32_t)sc << 4 |
                                          (uint32_t)(deg + 64) << 8 | (uint32_t)src << 16 | (uint32_t)ui.page << 20 |
                                          (uint32_t)p[TP_E4] << 26), chord);
    char t[48], *e;
    for (int i = 0; i < NTRK; i++)
        h = hash_u(h, proj.trk[i].par[TP_BRAIN] | (uint32_t)is_drum(i) << 4);
    if (!begin_main(h))
        return;
    if (LARGE) {
        brain_large(p, key, sc, deg, src);
        main_blit();
        return;
    }
    e = s_cat(s_cat(t, NOTE_NAME[key]), " ");
    s_cat(e, BS_NAME[sc]);
    cv_text(12, 4, &FONT_L, t, K_TEXT);
    if (src) {
        s_cat(s_cat(t, "chords from "), TRACK_NAME[src - 1]);
        text_r(230, LY(6, 3), F_CAP, t, K_DIM);
    }
    if (ui.page == P_PLAY) {
        cv_text(12, LY(44, 34), F_CAP, "routing: black keys 1 to 8 toggle a track", K_DIM);
    } else if (ui.page == P_SUB) {
        if (!src)
            cv_text(12, LY(34, 35), F_CAP, "off: the chords come from the", K_DIM), cv_text(12, LY(48, 46), F_CAP, "conductor's own keys and steps", K_DIM);
        else if (is_drum(src - 1)) {
            s_cat(s_cat(t, TRACK_NAME[src - 1]), " is a kit: it has no notes");
            cv_text(12, LY(40, 38), F_CAP, t, K_REC);
        } else {
            s_cat(s_cat(t, TRACK_NAME[src - 1]), "'s notes set the chord:");
            cv_text(12, 34, &FONT_XS, t, K_DIM);
            cv_text(12, 48, &FONT_XS, "chords, a bass line; it follows nothing", K_DIM);
        }
    } else {
        e = s_cat(t, "degree ");
        if (deg > 0)
            e = s_cat(e, "+");
        num_i(deg, e);
        cv_text(12, 44, &FONT_B, t, deg ? K_NOW : K_DIM);
        if (chord) {                         /* the active chord: the tones harmony plays */
            char cb[24];
            char *q = cb;
            *q = 0;
            for (int i = 0; i < SN; i++) {
                int v = (int)(chord >> (8 * i) & 0xFFu);
                if (!v)
                    continue;
                if (q != cb)
                    q = s_cat(q, " ");
                q = s_cat(q, NOTE_NAME[v % 12]);
            }
            text_r(230, 44, &FONT_XS, cb, K_FAINT);
        }
        if (p[TP_E4])
            text_r(230, LY(48, 34), &FONT_XS, LINK_NAME[p[TP_E4] % 9], K_DIM), text_r(230 - text_w(&FONT_XS, LINK_NAME[p[TP_E4] % 9]) - 4, LY(48, 34), &FONT_XS, "link", K_FAINT);
    }
    {   /* large type, Follow: "tracks that / follow" at the chips' left (the lines above taken) */
        int side = LARGE && ui.page == P_SUB;
        if (side)
            cv_text(4, 60, F_CAP, "tracks that", K_DIM), cv_text(4, 69, F_CAP, src ? "follow:" : "follow", K_DIM);
        else
            cv_text(12, LY(70, 48), F_CAP, src ? "tracks that follow; the chords' in its colour" : "tracks that follow", K_DIM);
    }
    for (int i = 0; i < NTRK; i++) {
        int32_t x = 12 + i * 27;
        int m = proj.trk[i].par[TP_BRAIN] % NBM;
        num_s((uint32_t)i + 1u, t);
        if (src == i + 1) {                          /* the FOLLOW track: the chords come from it */
            cv_round(x, LY(86, 63), LY(22, 18), LY(22, 18), 5, trk_ink(i));
            text_c(x + LY(11, 9), LY(90, 64), LY(&FONT_B, &LFONT_B), t, K_BG);
            continue;
        }
        if (m == BM_DIATONIC || m == BM_HARMONY)
            cv_round(x, 86, 22, 22, 5, K_TEXT);
        else if (m == BM_TRANSPOSE || m == BM_DEGREE)
            cv_frame(x, 86, 22, 22, 5, K_TEXT, K_BG);
        else
            cv_frame(x, 86, 22, 22, 5, K_LINE, K_BG);
        if (m >= BM_HARMONY)                          /* the harmony modes: a bar under the number */
            cv_rect(x + 6, 104, 10, 2, m == BM_HARMONY ? K_BG : K_TEXT);
        text_c(x + 11, 90, &FONT_B, t, m == BM_DIATONIC || m == BM_HARMONY ? K_BG : m ? K_TEXT : K_FAINT);
    }
    main_blit();
}
static void draw_punch(void)
{
    uint32_t dm = eng_punch_drum, mm = eng_punch_mel;
    uint32_t h = hash_u(hash_u(2166136261u, 0xD0u), dm ^ (mm << 12) ^ (ui.keys << 3) ^ (ui.punch_latched << 7));
    char t[64], *e;
    int n = 0;
    if (!begin_main(h))
        return;
    e = t;
    *t = 0;
    for (int fx = 0; fx < NPUNCH_FX; fx++) {
        int on = fx < PX_MEL ? (dm >> fx & 1u) : (mm >> (fx - PX_MEL) & 1u);
        if (!on || !PX_VALID(fx))
            continue;
        if (n++)
            e = s_cat(e, ", ");
        if (s_len(t) < 40)
            e = s_cat(e, PUNCH_NAME[fx & 15]);
    }
    if (!n)
        s_cat(t, "hold a key for an effect");
    {   /* (the effects held: smaller as more are held, cut only past the smallest) */
        const felucca_font_t *f = !n ? &FONT_S : text_w(&FONT_B, t) <= 220 ? &FONT_B : text_w(&FONT_S, t) <= 220 ? &FONT_S : &FONT_XS;
        text_fit(10, f == &FONT_XS ? 6 : 4, f, t, 220, n ? K_NOW : K_DIM);
    }
    cv_text(10, 24, &FONT_XS, "drums", K_DIM);
    text_r(230, 24, &FONT_XS, "melodic", K_DIM);
    for (int k = 0; k < 16; k++) {                   /* the keys: two octaves, the ones in effect lit */
        int32_t x = 8 + k * 14;
        int fx = punch_of_key(WHITE_K[k]);
        int on = fx >= 0 && (fx < PX_MEL ? (dm >> fx & 1u) : (mm >> (fx - PX_MEL) & 1u));
        cv_round(x, 40, 13, 60, 2, on ? K_NOW : fx < 0 ? K_PANEL : K_PANEL2);
        if (fx >= 0 && k < 7) {                      /* the drum octave's initials: m s r e b f j */
            char ini[2] = {PUNCH_NAME[fx & 15][0], 0};
            text_c(x + 6, 102, &FONT_XS, ini, K_DIM);
        }
    }
    for (int k = 0; k < 11; k++) {
        int key = BLACK_K[k], fx = punch_of_key(key);
        int on = fx >= 0 && (fx < PX_MEL ? (dm >> fx & 1u) : (mm >> (fx - PX_MEL) & 1u));
        int32_t wi = 0;
        while (WHITE_K[wi] < key)
            wi++;
        cv_frame(8 + wi * 14 - 5, 40, 10, 34, 2, fx < 0 ? K_LINE : K_DIM, on ? K_NOW : K_BG);
    }
    cv_rect(8 + 7 * 14 - 1, 36, 2, 70, K_DIM);
    cv_text(120, 104, &FONT_XS, proj.trk[AUX_PUNCH].par[TP_E1] ? "latch on" : "while held", K_DIM);
    main_blit();
}
static void draw_play(void)
{
    int trk = sel_trk();
    const uint8_t *p = par_now(trk);
    uint32_t h = hash_u(hash_u(2166136261u, 0x9Au), (uint32_t)trk | (uint32_t)p[TP_VMODE] << 4 | (uint32_t)eng_voices << 24);
    char t[40];
    if (!begin_main(h))
        return;
    const char *sub = p[TP_ENGINE] == EG_ACID ? "one voice, held notes slide" : p[TP_VMODE] % 4 == VM_POLY ? (p[TP_ENGINE] == EG_TARTAN ? "a chord and the last one's tail" : "up to 6 voices on the track")
                    : p[TP_VMODE] % 4 == VM_MONO ? "one voice, every key restarts it"
                    : p[TP_VMODE] % 4 == VM_RETRIG ? "one voice, every key from zero" : "one voice, held keys glide";
    int32_t ty;
    if (LARGE)                                       /* (large type: the line on the title's, right) */
        ty = title_sub(tp_info_par(TP_VMODE, p)->names[p[TP_VMODE] % 4], sub, K_DIM);
    else {
        cv_text(12, 6, &FONT_B, tp_info_par(TP_VMODE, p)->names[p[TP_VMODE] % 4], K_TEXT);
        cv_text(12, 26, F_CAP, sub, K_DIM);
        ty = 0;
    }
    for (int v = 0; v < NVOICE; v++)                 /* (large type: the dots as big as the line's room allows) */
        cv_round(12 + v * LY(13, 16), LY(50, ty + 8), LY(10, 13), LY(10, 13), 3, v < eng_voices ? K_NOW : K_LINE);
    num_s((uint32_t)eng_voices, s_cat(t, "voices sounding "));
    cv_text(12, LY(66, ty + 28), LY(F_CAP, &FONT_XS), t, K_DIM);
    main_blit();
}
/* Play 2: the track's volume alone, a wedge of sixteen bars lit up to it (as the knob turns, or motion moves it) */
static void draw_volume(void)
{
    int trk = sel_trk(), v = par_now(trk)[TP_VOLUME], lit = (v * 16 + 63) / 127;
    uint32_t h = hash_u(hash_u(2166136261u, 0x0Du), (uint32_t)trk | (uint32_t)v << 4);
    int32_t top, base;
    if (!begin_main(h))
        return;
    top = title_sub("volume", "the sound's own level, before Mix's", K_DIM) + 8;
    base = MAIN_H - 10;
    for (int i = 0; i < 16; i++) {                   /* (each bar taller: louder to the right) */
        int32_t bh = 6 + (base - top - 6) * i / 15;
        cv_round(12 + i * 14, base - bh, 10, bh, 3, i < lit ? K_NOW : K_LINE);
    }
    main_blit();
}
/* a MIDI track's Play 2: the sound's octave, five places, the one now lit */
static void draw_play2(void)
{
    static const char *const WHAT[5] = {"two octaves down: a sub", "an octave down: a bass", "as played",
                                        "an octave up", "two octaves up"};
    int trk = sel_trk(), o = proj.trk[trk].par[TP_OCT] % 5;
    uint32_t h = hash_u(hash_u(2166136261u, 0x0Cu), (uint32_t)trk | (uint32_t)o << 4);
    if (!begin_main(h))
        return;
    cv_text(12, 6, &FONT_B, "octave", K_TEXT);
    cv_text(12, 26, &FONT_XS, WHAT[o], K_DIM);
    for (int i = 0; i < 5; i++) {
        static const char *const LAB[5] = {"-2", "-1", "0", "+1", "+2"};
        int x = 24 + i * 40;
        cv_round(x, 52, 32, 24, 5, i == o ? K_NOW : K_LINE);
        text_c(x + 16, 57, &FONT_XS, LAB[i], i == o ? K_BG : K_DIM);
    }
    main_blit();
}
/* orbit's Sub page: the FM index from the note's start over two seconds (fall, floor), and how keyscale lowers it
 * up the keyboard (a second, fainter curve: two octaves up) */
static void draw_rayon_fall(void)
{
    int trk = sel_trk();
    const uint8_t *p = par_now(trk);
    int fl = p[TP_X1], fo = p[TP_X2], ks = p[TP_X3];
    uint32_t h = hash_u(hash_u(2166136261u, 0x0Fu), (uint32_t)trk | (uint32_t)fl << 4 | (uint32_t)fo << 12 |
                                                    (uint32_t)ks << 20);
    float tau = 0.02f * fm_exp2f((float)fl * (7.64f / 127.0f)), floor_ = (float)fo / 127.0f;
    float hi = fm_exp2f(-2.0f * (float)ks / 127.0f);
    int32_t x0 = 12, y0 = 40, w = 216, hh = 56;
    pen_t pa = {0}, pb = {0};
    if (!begin_main(h))
        return;
    cv_text(12, 6, &FONT_B, fl ? "the brightness falls" : "the brightness holds", K_TEXT);
    cv_text(12, 24, F_CAP, fl ? "from the note's start, over two seconds" : "fall at 0: the index as the knob sets it",
            K_DIM);
    cv_rect(x0, y0 + hh, w, 1, K_LINE);
    for (int i = 0; i <= w; i++) {
        float t = 2.0f * (float)i / (float)w, e = fl ? floor_ + (1.0f - floor_) * fm_expf(-t / tau) : 1.0f;
        pen_to(&pb, (float)(x0 + i), (float)(y0 + hh) - e * hi * (float)hh, K_FAINT);
        pen_to(&pa, (float)(x0 + i), (float)(y0 + hh) - e * (float)hh, K_NOW);
    }
    main_blit();
}
/* knit's wave at phase fr (0..1), as pic_knit draws it: osc 1's (its wave knob: saw .. square .. pulse), or osc 2's
 * (osc2: its own wave, O2W_*) */
static float knit_y(float fr, const uint8_t *p, int osc2)
{
    int w = osc2 ? p[TP_O2WAVE] % NO2W : O2W_SAME;
    float wv = (float)p[w == O2W_SAME ? TP_E1 : TP_O2SHAPE] / 127.0f;
    float mix = wv < 0.5f ? wv * 2.0f : 1.0f, pw = wv < 0.5f ? 0.5f : 0.5f - (wv - 0.5f) * 0.8f;
    if (w == O2W_TRI)
        return fr < 0.5f ? 4.0f * fr - 1.0f : 3.0f - 4.0f * fr;
    if (w == O2W_SINE)
        return fm_sin_turns(fr);
    return (1.0f - 2.0f * fr) + ((fr < pw ? 1.0f : -1.0f) - (1.0f - 2.0f * fr)) * mix;
}
/* knit's Levels page (P_SUB): its layers stacked, each as tall as its level: the wave (faint: the first page's), osc 2
 * at its interval (its own wave), the sub an octave down, the noise */
static void draw_knit_x(void)
{
    int trk = sel_trk();
    const uint8_t *p = par_now(trk);
    int st = clampi((int)p[TP_X1] - 64, -24, 24);
    uint32_t h = hash_u(hash_u(2166136261u, 0xA4u), (uint32_t)trk | (uint32_t)p[TP_X1] << 4 | (uint32_t)p[TP_X2] << 12 |
                                                    (uint32_t)p[TP_X3] << 20);
    float ratio = fm_exp2f((float)st * (1.0f / 12.0f)), lv[4] = {1.0f, (float)p[TP_X2] / 127.0f, (float)p[TP_X3] / 127.0f,
                                                                   (float)p[TP_X4] / 127.0f};
    static const char *const NM[4] = {"wave", "osc 2", "sub", "noise"};
    uint32_t r = 12345u;
    char t[24];
    h = hash_u(h, p[TP_X4]);
    h = hash_u(h, (uint32_t)p[TP_E1] | (uint32_t)p[TP_O2WAVE] << 8 | (uint32_t)p[TP_O2SHAPE] << 16);
    if (!begin_main(h))
        return;
    for (int l = 0; l < 4; l++) {
        pen_t pn = {0};
        float cy = (float)MY(14 + l * 26), amp = (float)MY(10) * lv[l];   /* (large type: the rows closer) */
        cv_text(8, (int32_t)cy - LY(6, 7), LY(&FONT_XS, F_CAP), NM[l], lv[l] > 0.0f ? K_DIM : K_FAINT);
        for (int32_t x = 0; x <= 170; x++) {
            float ph = (float)x / 170.0f * 3.0f * (l == 1 ? ratio : l == 2 ? 0.5f : 1.0f), fr = ph - fm_floorf(ph), y;
            if (l == 2)
                y = fm_sin_turns(ph);
            else if (l == 3)
                r = r * 1664525u + 1013904223u, y = (float)(int32_t)r * (1.0f / 2147483648.0f);
            else
                y = knit_y(fr, p, l == 1);
            pen_to(&pn, (float)(56 + x), cy - y * amp, l == 0 ? K_FAINT : K_NOW);
        }
    }
    if (st)
        num_i(st, s_cat(t, st > 0 ? "osc 2 +" : "osc 2 ")), s_cat(t + s_len(t), " st");
    else
        s_cat(t, "osc 2 in unison");
    text_r(230, LY(100, 64), F_CAP, t, K_DIM);
    main_blit();
}
/* knit's Mod page (P_KMOD): osc 2 (bright) over osc 1 (faint), two of osc 1's cycles, synced (starting again with
 * each of osc 1's) or free; with a sweep, osc 2 at the filter envelope's top too (faint); under them two cycles of the
 * sub's wave (its type) */
static void draw_knit_mod(void)
{
    int trk = sel_trk();
    const uint8_t *p = par_now(trk);
    int st = clampi((int)p[TP_X1] - 64, -24, 24), sync = p[TP_KSYNC] != 0, sw = p[TP_KSWEEP], cr = p[TP_KCROSS];
    int sub = p[TP_KSUB] % NKS;
    uint32_t h = hash_u(hash_u(2166136261u, 0xA8u), (uint32_t)trk | (uint32_t)p[TP_X1] << 4 | (uint32_t)sync << 12 |
                                                    (uint32_t)sw << 13 | (uint32_t)cr << 20);
    float ratio = fm_exp2f((float)st * (1.0f / 12.0f)), top = ratio * fm_exp2f((float)sw * (1.0f / 12.0f));
    float cy = (float)MY(32), amp = (float)MY(20), sy = (float)MY(84), samp = (float)MY(10);
    pen_t a = {0}, b = {0}, c = {0}, d = {0};
    char t[48], *e;
    h = hash_u(hash_u(h, (uint32_t)sub | (uint32_t)p[TP_E1] << 4 | (uint32_t)p[TP_O2WAVE] << 12),
               (uint32_t)p[TP_O2SHAPE]);
    if (!begin_main(h))
        return;
    cv_text(8, (int32_t)cy - LY(6, 7), LY(&FONT_XS, F_CAP), "osc 2", K_DIM);
    cv_text(8, (int32_t)sy - LY(6, 7), LY(&FONT_XS, F_CAP), "sub", K_DIM);
    for (int32_t x = 0; x <= 170; x++) {
        float ph = (float)x / 85.0f, fr = ph - fm_floorf(ph);   /* (osc 1: two cycles) */
        float q = sync ? fr * ratio : ph * ratio, q2 = sync ? fr * top : ph * top;
        pen_to(&a, (float)(56 + x), cy - knit_y(fr, p, 0) * amp, K_FAINT);
        if (sw)
            pen_to(&c, (float)(56 + x), cy - knit_y(q2 - fm_floorf(q2), p, 1) * amp, K_FAINT);
        pen_to(&b, (float)(56 + x), cy - knit_y(q - fm_floorf(q), p, 1) * amp, K_NOW);
        {   /* the sub: two of its own cycles (its name says how far down) */
            float s = ph, sf = s - fm_floorf(s);
            float y = sub == KS_SINE ? fm_sin_turns(s) : sf < (sub == KS_LOW_PULSE ? 0.25f : 0.5f) ? 1.0f : -1.0f;
            pen_to(&d, (float)(56 + x), sy - y * samp, K_NOW);
        }
    }
    e = s_cat(t, sync ? "synced" : "free");
    if (sw)
        e = s_cat(num_i(sw, s_cat(e, ", sweep +")), " st");
    if (cr)
        num_i((cr * 100 + 64) / 128, s_cat(e, ", cross "));
    text_r(230, LY(56, 44), F_CAP, t, K_DIM);
    main_blit();
}
/* rayon's Ops page (P_KMOD): its four operators as boxes, each with its ratio, the modulators (dim) above what they
 * move, the carriers (bright) on the bottom row into the output line, op 4's feedback a loop when it is up; ops 3 and 4
 * faint when off */
static void draw_rayon_ops(void)
{
    /* per mode (RO_*): op 1..4's column and row (0 top .. 3 bottom), what modulates each (bit j: op j + 1), the
     * carriers */
    static const uint8_t POS[NRO][4][2] = {{{1, 3}, {1, 2}, {2, 3}, {2, 2}}, {{1, 3}, {1, 2}, {1, 1}, {1, 0}},
                                           {{0, 3}, {0, 2}, {2, 3}, {2, 2}}};
    static const uint8_t MOD[NRO][4] = {{0x2, 0, 0, 0}, {0x2, 0x4, 0x8, 0}, {0x2, 0, 0x8, 0}};
    static const uint8_t CAR[NRO] = {0x1, 0x1, 0x5};
    int trk = sel_trk();
    const uint8_t *p = par_now(trk);
    int ro = p[TP_ROPS] % NRO, fb = p[TP_RFB];
    uint32_t h = hash_u(hash_u(2166136261u, 0xA9u), (uint32_t)trk | (uint32_t)ro << 4 | (uint32_t)p[TP_RRATIO] << 8 |
                                                    (uint32_t)(fb > 0) << 16 | (uint32_t)p[TP_E2] << 20);
    int32_t bw = 40, bh = 16, rs = 20, cs = 60, ox = 50, oy = 8, bx[4], by[4];
    char rt[4][12], num[16], unit[6], cap[48];
    if (!begin_main(h))
        return;
    s_cat(rt[0], "1");
    tp_text(TP_E2, p[TP_E2], EG_RAYON, num, unit);
    s_cat(rt[1], num);
    tp_text(TP_RRATIO, p[TP_RRATIO], EG_RAYON, num, unit);
    s_cat(rt[2], num);
    s_cat(rt[3], num);
    for (int k = 0; k < 4; k++)
        bx[k] = ox + POS[ro][k][0] * cs, by[k] = oy + POS[ro][k][1] * rs;
    for (int d = 0; d < 4; d++)
        for (int s = 0; s < 4; s++)
            if (MOD[ro][d] >> s & 1u)
                cv_linef((float)(bx[s] + bw / 2), (float)(by[s] + bh), (float)(bx[d] + bw / 2), (float)by[d], K_DIM, 1.2f);
    {
        int32_t lo = 9999, hi = 0, yo = oy + 3 * rs + bh + 5;
        for (int k = 0; k < 4; k++)
            if (CAR[ro] >> k & 1u) {
                cv_rect(bx[k] + bw / 2, by[k] + bh, 1, yo - by[k] - bh, K_DIM);
                lo = bx[k] + bw / 2 < lo ? bx[k] + bw / 2 : lo;
                hi = bx[k] + bw / 2 > hi ? bx[k] + bw / 2 : hi;
            }
        cv_rect(lo, yo, hi - lo + 1, 1, K_DIM);
    }
    if (ro && fb) {                                  /* op 4's feedback: a loop on its right */
        int32_t fx = bx[3] + bw, fy = by[3] + bh / 2;
        cv_rect(fx, fy, 6, 1, K_DIM);
        cv_rect(fx + 6, by[3] - 4, 1, fy - by[3] + 5, K_DIM);
        cv_rect(bx[3] + bw / 2, by[3] - 4, fx + 6 - bx[3] - bw / 2, 1, K_DIM);
        cv_rect(bx[3] + bw / 2, by[3] - 4, 1, 4, K_DIM);
    }
    for (int k = 0; k < 4; k++) {
        int car = CAR[ro] >> k & 1u, off = !ro && k >= 2;
        if (off)
            continue;
        cv_round(bx[k], by[k], bw, bh, 3, car ? K_NOW : K_PANEL2);
        text_c(bx[k] + bw / 2, by[k] + (bh - 12) / 2, &LFONT_XS, rt[k], car ? K_BG : K_DIM);
    }
    s_cat(cap, ro == RO_OFF ? "the pair alone" : ro == RO_STACK ? "3 and 4 move the modulator" : "a second pair beside it");
    text_r(230, LY(100, 64), F_CAP, cap, K_DIM);
    main_blit();
}
/* tulle's Grain page (P_KMOD): a grain's window as its shape makes it (synth.c tl_win: its peak from the middle toward
 * its start), and the share of the grains played backwards: arrows, forward and back, as many of each */
static void draw_tulle_grain(void)
{
    int trk = sel_trk();
    const uint8_t *p = par_now(trk);
    int sh = p[TP_KSYNC], rv = p[TP_KSWEEP];
    uint32_t h = hash_u(hash_u(2166136261u, 0xAAu), (uint32_t)trk | (uint32_t)sh << 4 | (uint32_t)rv << 12);
    float att = 0.5f - 0.47f * (float)sh / 127.0f, x0 = 30.0f, w = 180.0f, base = (float)MY(70), hh = (float)MY(56);
    pen_t pn = {0};
    char t[32];
    if (!begin_main(h))
        return;
    cv_rect((int32_t)x0, (int32_t)base, (int32_t)w, 1, K_LINE);
    for (int i = 0; i <= 180; i++) {
        float ph = (float)i / 180.0f, u = ph < att ? 0.5f * ph / att : 0.5f + 0.5f * (ph - att) / (1.0f - att);
        float v = 4.0f * u * (1.0f - u);
        pen_to(&pn, x0 + (float)i, base - v * v * hh, K_NOW);
    }
    {   /* ten grains as arrows: the backward ones by the knob's share */
        int back = (rv * 10 + 63) / 127;
        for (int k = 0; k < 10; k++)
            text_c(36 + k * 18, LY(80, 74), &LFONT_XS, k < 10 - back ? ">" : "<", k < 10 - back ? K_DIM : K_NOW);
    }
    s_cat(num_i((rv * 100 + 63) / 127, s_cat(t, sh < 20 ? "smooth, " : sh < 90 ? "sharper, " : "a pluck, ")), "% backwards");
    text_r(230, LY(100, 64), F_CAP, t, K_DIM);
    main_blit();
}
/* acid's Sub page: its drive (TP_X1 amount, TP_X2 off / soft / rat; bass303.c drive_block) on two cycles of a sine,
 * the input faint and what comes out bright, each scaled to the picture's height (the shape, not the level) */
static void draw_acid_x(void)
{
    int trk = sel_trk();
    const uint8_t *p = par_now(trk);
    int ty = p[TP_X2] % 3;
    float d = (float)p[TP_X1] / 127.0f, g = ty == 1 ? fm_exp2f(d * 24.0f * 0.166096405f) : 1.0f + d * 40.0f;
    float cy = (float)MY(54), amp = (float)MY(34), y[171], top = 0.0f;
    uint32_t h = hash_u(hash_u(2166136261u, 0xA7u), (uint32_t)trk | (uint32_t)p[TP_X1] << 4 | (uint32_t)ty << 12 |
                                                    (uint32_t)p[TP_X3] << 16);
    pen_t a = {0}, b = {0};
    char t[32];
    if (!begin_main(h))
        return;
    for (int x = 0; x <= 170; x++) {                 /* the curve, then its height to scale it to the picture */
        float v = fm_sin_turns((float)x / 85.0f), u = v * g;
        if (ty == 1)
            u = fm_tanhf(u + 0.15f) - 0.14888503f;   /* soft: tanh, a little asymmetric */
        else if (ty == 2) {
            float x2 = u * u, sq = fm_rsqrtf(1.0f + x2 * x2);
            u *= sq * fm_rsqrtf(sq);                 /* rat: x / (1 + x^4)^(1/4) */
        } else
            u = v;
        y[x] = u;
        top = fm_maxf(top, fm_fabsf(u));
    }
    cv_rect(56, (int32_t)cy, 171, 1, K_LINE);
    for (int x = 0; x <= 170; x++) {
        pen_to(&a, (float)(56 + x), cy - fm_sin_turns((float)x / 85.0f) * amp, K_FAINT);
        pen_to(&b, (float)(56 + x), cy - y[x] / (top > 0.0f ? top : 1.0f) * amp, ty && d > 0.0f ? K_NOW : K_DIM);
    }
    cv_text(8, MY(20), F_CAP, "in", K_FAINT);
    cv_text(8, MY(34), F_CAP, "out", ty && d > 0.0f ? K_NOW : K_DIM);
    if (!ty || d <= 0.0f)
        s_cat(t, "drive off");
    else
        num_i((int32_t)(d * 100.0f + 0.5f), s_cat(s_cat(s_cat(t, "drive "), ty == 1 ? "soft" : "rat"), " ")), s_cat(t + s_len(t), " %");
    cv_text(8, LY(100, 64), F_CAP, t, K_DIM);
    text_r(230, LY(100, 64), F_CAP, "shaper: the square's edge", K_FAINT);
    main_blit();
}
/* the Sub page: the engine's second page of knobs (TP_X1..X4): knit's Levels (its layers), rayon's fall, acid's
 * drive */
static void draw_sub(void)
{
    int eg = proj.trk[sel_trk()].par[TP_ENGINE];
    if (eg == EG_RAYON)
        draw_rayon_fall();
    else if (eg == EG_ACID)
        draw_acid_x();
    else if (eg == EG_SAMPLE || eg == EG_MIDI || eg == EG_TULLE || eg == EG_LACE || eg == EG_TARTAN)
        draw_engine();                           /* (the sample's loop page: the waveform, its loop marked; MIDI's
                                                  * knobs 5..8: its eight bars) */
    else
        draw_knit_x();
}
/* a drum key's own envelope: its lag (flat), the attack's rise, the hold, the decay's fall (or, off, the level
 * held to the right edge: the sound's own length rules), the key's colour under it */
static void draw_key_env(void)
{
    int trk = sel_trk(), key = ui.dsound % DL_NSOUND;
    const uint8_t *p = par_now(trk) + TP_DRUM(key, 0);
    uint32_t h = hash_u(hash_u(2166136261u, 0xE8u), (uint32_t)trk | (uint32_t)key << 4 | (uint32_t)p[DP_ATTACK] << 8 |
                                          (uint32_t)p[DP_HOLD] << 16 | (uint32_t)p[DP_DECAY] << 24);
    float x0 = 16.0f, w = 208.0f, hh = 84.0f, base = 16.0f + hh, x = x0, px[48], py[48];
    int n = 0, open = p[DP_DECAY] >= 127;
    pen_t pn = {0};
    char t[48];
    h = hash_u(h, p[DP_LAG] | (uint32_t)p[DP_MODEL] << 8);
    if (!begin_main(h))
        return;
    cv_rect(16, 16 + 84, 208, 1, K_LINE);
    px[n] = x, py[n++] = base;
    x += (float)p[DP_LAG] * 20.0f / 127.0f;          /* the lag */
    px[n] = x, py[n++] = base;
    {
        float a = p[DP_ATTACK] ? 2.0f + (float)p[DP_ATTACK] * 42.0f / 127.0f : 0.0f;
        for (int i = 1; i <= 8; i++)
            px[n] = x + a * (float)i / 8.0f, py[n++] = base - hh * (float)i / 8.0f;
        x += a;
    }
    x += (float)p[DP_HOLD] * 36.0f / 127.0f;         /* the hold */
    px[n] = x, py[n++] = base - hh;
    if (open) {
        px[n] = x0 + w, py[n++] = base - hh;
    } else {
        float d = 6.0f + (float)p[DP_DECAY] * 98.0f / 127.0f;
        for (int i = 1; i <= 24; i++) {
            float u = (float)i / 24.0f;
            px[n] = x + d * u, py[n++] = base - hh * ease_exp(u);
        }
    }
    for (int i = 1; i < n; i++)                      /* under the curve, the track's colour thinned */
        for (int32_t xi = f_floor(px[i - 1] + 0.5f); xi < f_floor(px[i] + 0.5f); xi++) {
            float u = px[i] > px[i - 1] ? (xi - px[i - 1]) / (px[i] - px[i - 1]) : 0.0f;
            int32_t yt = f_floor(py[i - 1] + (py[i] - py[i - 1]) * u + 1.5f);
            if (yt < (int32_t)base)
                cv_rect(xi, yt, 1, (int32_t)base - yt, K_TINT);
        }
    for (int i = 0; i < n; i++)
        pen_to(&pn, px[i], py[i], K_NOW);
    s_cat(s_cat(t, DL_SOUND_NAME[key]), open ? "'s envelope, decay off: its own length" : "'s envelope");
    text_fit(16, LY(16 + 84 + 3, MAIN_H - 15), F_CAP, t, 208, K_DIM);
    main_blit();
}
/* a filter's response in dB at x px from its cutoff (an octave every 22 px): a resonant two-pole section,
 * squared for the 24 dB types; hp mirrors it, bp is its band-pass with the peak at unity */
static float filt_db(float dx, float q, int hp, int bp, int steep)
{
    float wn = fm_exp2f(dx / 22.0f), m2;
    if (hp)
        wn = 1.0f / wn;
    m2 = (1.0f - wn * wn) * (1.0f - wn * wn) + wn * wn / (q * q);   /* 1 / |H|^2 */
    if (bp)
        m2 = m2 * q * q / (wn * wn > 1e-6f ? wn * wn : 1e-6f);
    return -3.0103f * fm_log2f(m2 > 1e-6f ? m2 : 1e-6f) * (steep ? 2.0f : 1.0f);
}
/* the y of db on a filter picture: 0 dB sits 22 px under the top, -48 dB at the bottom; a peak eases into the top
 * rather than running into it (a resonant 24 dB filter reaches +30 dB: cut off flat, it would look square) */
static float filt_y(float db, int32_t y0, int32_t hh)
{
    float s = (float)(hh - 22) / 48.0f;
    if (db > 0.0f)
        return (float)y0 + 22.0f - 22.0f * (1.0f - fm_exp2f(-db * s * (1.0f / 22.0f) * 1.4427f));
    return (float)y0 + 22.0f - db * s;
}
/* a drum key's own filter: its response at the cutoff (bright), at where the envelope takes the cutoff
 * (faint), the cutoff's mark; off: a flat line */
static void draw_key_filter(void)
{
    int trk = sel_trk(), key = ui.dsound % DL_NSOUND;
    const uint8_t *p = par_now(trk) + TP_DRUM(key, 0);
    int ft = p[DP_FTYPE] % NDF;
    uint32_t h = hash_u(hash_u(2166136261u, 0xF2u), (uint32_t)trk | (uint32_t)key << 4 | (uint32_t)ft << 8 |
                                          (uint32_t)p[DP_CUT] << 12 | (uint32_t)p[DP_RES] << 20);
    int32_t x0 = 16, y0 = 12, w = 208, hh = 84, fc = x0 + 10 + p[DP_CUT] * (w - 20) / 127;
    char t[48];
    h = hash_u(h, p[DP_FENV] | (uint32_t)p[DP_DRIVE] << 8);
    if (!begin_main(h))
        return;
    cv_rect(x0, y0 + hh, w, 1, K_LINE);
    if (ft == DF_OFF) {
        cv_rect(x0, y0 + 22, w, 2, K_DIM);
        s_cat(s_cat(t, DL_SOUND_NAME[key]), ": no filter. Press PRESET for one");
        text_fit(x0, y0 + hh + 3, LY(&FONT_XS, F_CAP), t, w, K_DIM);
        main_blit();
        return;
    }
    {
        float q = 0.7f + (float)p[DP_RES] * 6.0f / 127.0f, ys[209], ye[209];
        float reach = ((float)p[DP_FENV] - 64.0f) * (6.0f / 64.0f) * 22.0f;   /* the envelope's reach, px */
        int hp = ft == DF_HP, bp = ft == DF_BP;
        pen_t pn = {0}, pe = {0};
        for (int32_t x = 0; x <= w; x++) {
            float y = filt_y(filt_db((float)(x0 + x - fc), q, hp, bp, 0), y0, hh);
            float z = filt_y(filt_db((float)(x0 + x - fc) - reach, q, hp, bp, 0), y0, hh);
            ys[x] = y < (float)y0 ? (float)y0 : y > (float)(y0 + hh) ? (float)(y0 + hh) : y;
            ye[x] = z < (float)y0 ? (float)y0 : z > (float)(y0 + hh) ? (float)(y0 + hh) : z;
        }
        for (int32_t x = 0; x < w; x++) {
            int32_t yt = f_floor(ys[x] + 1.5f);
            if (yt < y0 + hh)
                cv_rect(x0 + x, yt, 1, y0 + hh - yt, K_TINT);
        }
        if (p[DP_FENV] != 64)
            for (int32_t x = 0; x <= w; x++)
                pen_to(&pe, (float)(x0 + x), ye[x], K_DIM);
        for (int32_t x = 0; x <= w; x++)
            pen_to(&pn, (float)(x0 + x), ys[x], K_NOW);
    }
    cv_rect(fc, y0, 1, hh, K_FAINT);
    {
        char *e = s_cat(s_cat(s_cat(t, DL_SOUND_NAME[key]), ": "), tp_info_par(TP_DRUM(key, DP_FTYPE), proj.trk[trk].par)->names[ft]);
        if (p[DP_DRIVE])
            e = s_cat(e, ", driven");
        if (p[DP_FENV] != 64)
            s_cat(e, p[DP_FENV] > 64 ? ", the envelope opens it" : ", the envelope closes it");
    }
    text_fit(x0, y0 + hh + 3, &FONT_XS, t, w, K_DIM);
    main_blit();
}
static void draw_env(int filter)
{
    int trk = sel_trk(), b = filter ? TP_FA : TP_A, drum = is_drum(trk) && filter;
    const uint8_t *p = par_now(trk);
    uint32_t h = hash_u(hash_u(2166136261u, 0xE7u), (uint32_t)trk | (uint32_t)filter << 4 | (uint32_t)p[b] << 8 |
                                          (uint32_t)p[b + 1] << 16 | (uint32_t)p[b + 2] << 24);
    float x0 = 16.0f, y0 = 16.0f, w = 208.0f, hh = 84.0f, base = y0 + hh, x = x0, a, d, r, s;
    float px[64], py[64], dy;
    int n = 0, st;
    pen_t pn = {0};
    h = hash_u(h, p[b + 3] | (uint32_t)drum << 8);
    if (is_acid(trk) && !filter) {                   /* the 303's: over 3 s, the filter's opening (its attack's rise,
                                                      * its decay), an accented note's dimmer, the held note's level */
        const float ta = 0.3f * fm_exp2f(p[TP_A] * (13.28771238f / 127.0f)), td = 200.0f * fm_exp2f(p[TP_D] * (3.321928095f / 127.0f));
        const float tc = 30.0f + p[TP_S] * (2970.0f / 127.0f), tl = 30.0f + p[TP_R] * (2970.0f / 127.0f);
        pen_t pa = {0}, pc = {0}, pl = {0};
        if (!begin_main(hash_u(h, 0xAC1Du)))
            return;
        cv_rect(16, 16 + 84, 208, 1, K_LINE);
        for (int32_t xi = 0; xi <= 208; xi++) {
            float ms = (float)xi * (3000.0f / 208.0f);
            float rise = 1.0f - fm_expf(-ms / ta), fl = rise * fm_expf(-ms / td), ac = fm_expf(-ms / tc);
            pen_to(&pl, x0 + (float)xi, base - hh * 0.9f * fm_expf(-ms / tl), K_LINE);
            pen_to(&pc, x0 + (float)xi, base - hh * ac * (1.0f - fm_expf(-ms / 15.0f)), K_DIM);
            if (xi)
                cv_rect((int32_t)x0 + xi, (int32_t)(base - hh * fl + 1.5f), 1, (int32_t)(hh * fl), K_TINT);
            pen_to(&pa, x0 + (float)xi, base - hh * fl, K_NOW);
        }
        cv_text(16, 16 + 84 + 3, &FONT_XS, "filter; dim: accented; line: the note's level", K_DIM);
        main_blit();
        return;
    }
    st = mv.of == trk ? (filter ? mv.fst : mv.ast) : 0;   /* MOD VIEW: the newest note's stage and level, a dot */
    dy = base - hh * fm_clampf(filter ? mv.flv : mv.alv, 0.0f, 1.0f);
    /* the shape as points: each stage a curve (attack easing in, decay and release falling exponentially) */
    if (drum) {                                      /* lag (a delay), attack, then the hit's length */
        a = 2.0f + p[TP_A] * 40.0f / 127.0f;
        d = 4.0f + p[TP_D] * 40.0f / 127.0f;
        r = 20.0f + p[TP_R] * 100.0f / 127.0f;
        px[n] = x, py[n++] = base;
        x += a;
        px[n] = x, py[n++] = base;
        for (int i = 1; i <= 12; i++) {
            float t = i / 12.0f;
            px[n] = x + d * t, py[n++] = base - hh * (1.0f - (1.0f - t) * (1.0f - t));
        }
        x += d;
        for (int i = 1; i <= 24; i++) {
            float t = i / 24.0f;
            px[n] = x + r * t, py[n++] = base - hh * ease_exp(t);
        }
    } else {
        a = 4.0f + p[b] * 60.0f / 127.0f;
        d = 4.0f + p[b + 1] * 60.0f / 127.0f;
        r = 4.0f + p[b + 3] * 60.0f / 127.0f;
        s = p[b + 2] / 127.0f;
        px[n] = x, py[n++] = base;
        for (int i = 1; i <= 12; i++) {
            float t = i / 12.0f;
            px[n] = x + a * t, py[n++] = base - hh * (1.0f - (1.0f - t) * (1.0f - t));
        }
        x += a;
        for (int i = 1; i <= 16; i++) {
            float t = i / 16.0f;
            px[n] = x + d * t, py[n++] = base - hh * (s + (1.0f - s) * ease_exp(t));
        }
        x += d;
        x = x0 + w - r;                              /* the sustain, then the release */
        px[n] = x, py[n++] = base - hh * s;
        for (int i = 1; i <= 16; i++) {
            float t = i / 16.0f;
            px[n] = x + r * t, py[n++] = base - hh * s * ease_exp(t);
        }
    }
    mv_dot = -1;
    mv_sus = st == 3 ? (mv_sus ? mv_sus : plat_ms() | 1u) : 0;
    if (st) {                                        /* the dot where its stage's curve is at its level (sustain: the
                                                      * decay's end; a release from above the sustain: its start),
                                                      * between the curve's points, so it glides rather than steps */
        int i = st == 4 ? 30 : st == 1 ? 1 : 13, e = st == 4 ? 45 : st == 1 ? 12 : 28;
        float sg = st == 1 ? -1.0f : 1.0f, g;        /* (the attack rises: its y falls) */
        while (i < e && (py[i] - dy) * sg < 0.0f)
            i++;
        g = py[i] - py[i - 1];
        mv_dot = (int32_t)(px[i] - (px[i] - px[i - 1]) * (g != 0.0f ? fm_maxf((py[i] - dy) / g, 0.0f) : 0.0f) + 0.5f);
        if (st == 3)                                 /* sustain: along its line over 7 s, then it waits at the end */
            mv_dot = (int32_t)(px[28] + (px[29] - px[28]) * fm_minf((float)(int32_t)(plat_ms() - mv_sus) * (1.0f / 7000.0f), 1.0f) + 0.5f);
        dy = (float)(int32_t)(dy + 0.5f);
        h = hash_u(h, (uint32_t)st | (uint32_t)dy << 4 | (uint32_t)mv_dot << 16);
    }
    if (!begin_main(h))
        return;
    cv_rect(16, 16 + 84, 208, 1, K_LINE);
    for (int i = 1; i < n; i++)                      /* under the curve, the track's colour thinned */
        for (int32_t xi = f_floor(px[i - 1] + 0.5f); xi < f_floor(px[i] + 0.5f); xi++) {
            float t = px[i] > px[i - 1] ? (xi - px[i - 1]) / (px[i] - px[i - 1]) : 0.0f;
            int32_t yt = f_floor(py[i - 1] + (py[i] - py[i - 1]) * t + 1.5f);
            if (yt < (int32_t)base)
                cv_rect(xi, yt, 1, (int32_t)base - yt, K_TINT);
        }
    for (int i = 0; i < n; i++)
        pen_to(&pn, px[i], py[i], K_NOW);
    if (st)
        cv_round(mv_dot - 2, (int32_t)dy - 2, 5, 5, 2, K_NOW);   /* (a bead on the line, its colour) */
    cv_text(16, 16 + 84 + 3, &FONT_XS, drum ? "every key: lag, attack, random, length"
                                              : filter ? "filter envelope" : "amp envelope", K_DIM);
    main_blit();
}
static void draw_filter(void)
{
    int trk = sel_trk();
    const uint8_t *p = par_now(trk);
    int ft = p[TP_FTYPE] % NFT, cut = p[TP_CUT], res = p[TP_RES], dyn = ft == FT_DYNAMIC;
    uint32_t h = hash_u(hash_u(2166136261u, 0xF1u), (uint32_t)trk | (uint32_t)ft << 4 | (uint32_t)cut << 8 | (uint32_t)res << 16 |
                                          (uint32_t)p[TP_KTRK] << 24);
    int32_t x0 = 16, y0 = 12, w = 208, hh = 84, fc = x0 + 10 + cut * (w - 20) / 127, py = -1, fm = fc;
    if (is_drum(trk) && ui.page == P_FILTER) {       /* a kit's Filter page: the key's own */
        draw_key_filter();
        return;
    }
    if (mv.of == trk)                                /* MOD VIEW: the curve at the cutoff as it sounds (the line: the knob) */
        fm = x0 + 10 + f_floor(fm_clampf((float)cut + mv.cut, 0.0f, 127.0f) * (float)(w - 20) * (1.0f / 127.0f));
    mv_fc = fm;
    if (fm != fc)
        h = hash_u(h, (uint32_t)fm);
    if (!begin_main(h))
        return;
    cv_rect(x0, y0 + hh, w, 1, K_LINE);
    if (is_drum(trk) && !dyn) {                      /* the kit's bus filter, switched off */
        cv_rect(x0, y0 + 22, w, 2, K_DIM);
        cv_text(x0, y0 + hh + 3, &FONT_XS, "all keys: no bus filter. Press PRESET for one", K_DIM);
        main_blit();
        return;
    }
    if (dyn) {                                       /* low cut .. hi cut: the band that passes */
        int32_t lo = x0 + p[TP_CUT] * (w - 20) / 127, hi = x0 + 20 + p[TP_KTRK] * (w - 20) / 127;
        cv_round(lo, y0 + 10, hi > lo ? hi - lo : 2, hh - 10, 4, K_NOW_T);
        cv_rect(lo, y0 + 10, 2, hh - 10, K_NOW);
        cv_rect(hi, y0 + 10, 2, hh - 10, K_NOW);
        cv_text(x0, y0 + hh + 3, &FONT_XS, "all keys: low cut, compression, drive, hi cut", K_DIM);
        main_blit();
        return;
    }
    {
        /* the response: a resonant two-pole section (squared for the 24 dB types) on a log frequency axis, an
         * octave every 22 px, in dB, 0 dB 22 px under the top, -48 at the bottom */
        float q = 0.7f + (float)res * 6.0f / 127.0f, ys[209];
        int steep = ft == FT_LP24 || ft == FT_LADDER, hp = ft == FT_HP12, bp = ft == FT_BP12;
        pen_t pn = {0};
        (void)py;
        for (int32_t x = 0; x <= w; x++) {
            float y = filt_y(filt_db((float)(x0 + x - fm), q, hp, bp, steep), y0, hh);
            ys[x] = y < (float)y0 ? (float)y0 : y > (float)(y0 + hh) ? (float)(y0 + hh) : y;
        }
        for (int32_t x = 0; x < w; x++) {            /* under the curve, the track's colour thinned */
            int32_t yt = f_floor(ys[x] + 1.5f);
            if (yt < y0 + hh)
                cv_rect(x0 + x, yt, 1, y0 + hh - yt, K_TINT);
        }
        for (int32_t x = 0; x <= w; x++)
            pen_to(&pn, (float)(x0 + x), ys[x], K_NOW);
    }
    cv_rect(fc, y0, 1, hh, K_FAINT);
    cv_text(x0, y0 + hh + 3, &FONT_XS, tp_info_par(TP_FTYPE, p)->names[ft], K_DIM);
    text_r(x0 + w, y0 + hh + 3, &FONT_XS, is_acid(trk) ? "" : p[TP_KTRK] ? "key tracking on" : "", K_DIM);
    main_blit();
}
static float pic_atan(float x)                       /* atan x, x >= 0 (a picture's: about 0.001 rad) */
{
    int inv = x > 1.0f;
    float y, x2;
    if (inv)
        x = 1.0f / x;
    x2 = x * x;
    y = x * (0.99997726f + x2 * (-0.33262347f + x2 * (0.19354346f + x2 * (-0.11643287f + x2 * (0.05265332f - x2 * 0.01172120f)))));
    return inv ? 1.57079633f - y : y;
}
/* a slot page (Insert 1: a track's slot 1, Insert 2: its slot 2; FX I / II: the effect track's; Master FX the
 * master's): its effect (the reverb with its type), what it does, where it is, and a picture of it, from its knobs as
 * its effect names them: the dirt and the color their curves (synth.c slot_picture: the transfer, a crushed or
 * ring-modulated wave, the tilt's response, the dry dimmed behind); tremolo each side's level over two of its cycles
 * (the left bright, the right faint: apart by pan); the phaser, univibe and autowah their response; chorus and juno
 * their two taps' delays crossing; the delay its repeats; rumble its ducked tail; rat its clip; lofi its steps; the
 * reverb rings spreading. An effect without its memory says so (slot_memo) */
/* "not playing: memory full": a slot page's effect without its memory (set by a project, a preset, a lock or MIDI
 * where none was free; "": it has it) */
static void slot_memo(char *t, int pg)
{
    int st, q = slot_page(pg, &st);
    *t = 0;
    if (!(st < 0 ? engine_master_room() : is_fx(st) ? engine_fx_room(st - AUX_FX1) : engine_slot_room(st, q)))
        s_cat(t, "not playing: memory full");
}
static void draw_slot(int pg)
{
    static const char *const ABOUT[NEF] = {"the sound as it is", "a soft saturation", "a pedal's distortion",
                                           "a sine wavefolder", "fewer bits, a lower rate", "a ring modulator",
                                           "a tilt EQ", "worn tape: warmth and wow", "an old sampler's grain",
                                           "a lofi sampler's grit", "the level rising and falling",
                                           "notches that sweep", "a lamp-swept phase", "two voices swaying apart",
                                           "the Juno's chorus", "a filter your playing opens", "repeats in time",
                                           "a low, dark tail, ducked", ""};   /* (the reverb: its type) */
    uint8_t mp[NTP], ms[NTP];
    int trk, q;
    const uint8_t *p = slot_vals(pg, mp, &trk, &q);
    const int mst = trk < 0, drum = !mst && is_drum(trk), send = !mst && is_fx(trk);
    if (!mst)                                        /* (MOD VIEW: at the values the MOD lanes play) */
        p = mod_shown(trk, p, ms, NTP);
    const int pe = TP_SLOT[q][SL_EF], ef = p[pe] < NEF ? p[pe] : EF_OFF;
    const float k0 = (float)p[TP_SLOT[q][SL_K1]] / 127.0f, k1 = (float)p[TP_SLOT[q][SL_K2]] / 127.0f;
    const float k2 = (float)p[TP_SLOT[q][SL_K3]] / 127.0f, mix = (float)p[TP_SLOT[q][SL_MIX]] / 127.0f;
    uint32_t h = hash_u(hash_u(2166136261u, 0xC4u + (uint32_t)q),
                        (uint32_t)(trk + 1) | (uint32_t)ef << 5 | (uint32_t)drum << 10);
    int32_t x0 = 128, y0 = 8, w = 100, hh = 96, cy = y0 + hh / 2;
    char t[64];
    for (int j = SL_K1; j <= SL_X1; j++)
        h = hash_u(h, p[TP_SLOT[q][j]]);
    h = hash_u(h, (uint32_t)mst | (uint32_t)send << 1 | (uint32_t)(send || mst ? 0 : ui.last_inst) << 4);
    slot_memo(t, pg);
    h = hash_s(h, t);
    if (!begin_main(h))
        return;
    const int nomem = t[0] != 0;
    if (nomem && !LARGE)                             /* (no memory: not playing) */
        wrap(12, 74, 80, &FONT_XS, t, K_TEXT);
    s_cat(t, EFX[ef].name);
    t[0] = (char)(t[0] - 32);                        /* (its name a title: "Reverb") */
    cv_text(12, 4, &FONT_L, t, K_TEXT);
    if (nomem && LARGE)                              /* (large type: no room at the foot; the note in the line that
                                                      * says what the effect does) */
        cv_text(12, 40, &FONT_XS, "not playing: memory full", K_TEXT);
    else if (ef != EF_REVERB)
        cv_text(12, 40, &FONT_XS, ABOUT[ef], K_DIM);
    else {                                           /* "Reverb · hall": its type under its name */
        cv_round(12, 36 + FONT_B.h / 2, 4, 4, 2, K_DIM);
        cv_text(20, 36, &FONT_B, RV_NAME[p[TP_SLOT[q][SL_X1]] % NRV], K_TEXT);
    }
    if (mst)
        s_cat(t, "on the whole mix");
    else if (send)
        s_cat(s_cat(t, trk == AUX_FX1 ? "send I, keys play " : "send II, keys play "), TRACK_NAME[inst_trk()]);
    else
        s_cat(s_cat(s_cat(t, drum && !q ? "on all of " : "after "), TRACK_NAME[trk]),
              q ? "'s insert 1" : drum ? "'s keys" : "'s filter");
    cv_text(12, 56, &FONT_XS, t, K_DIM);
    if (efx_is_char(ef) || ef == EF_OFF) {           /* the curve, the dry one faint behind */
        const int pt = ef == EF_TILT || ef == EF_CRUSH || ef == EF_RING || ef == EF_SAMPLER;
        float ys[101], ds[101];
        pen_t pd = {0}, pn = {0};
        int32_t i;
        cv_rect(x0, cy, w, 1, K_LINE);
        if (!pt)
            cv_rect(x0 + w / 2, y0, 1, hh, K_LINE);
        for (i = 0; i <= w; i++) {
            float u = pt ? (float)i / (float)w : (float)i * 2.0f / (float)w - 1.0f;
            float y = fm_clampf(slot_picture(p, q, -1, u), -1.2f, 1.2f), d = fm_clampf(slot_picture(p, q, 0, u), -1.2f, 1.2f);
            ys[i] = (float)cy - y * (float)(hh / 2 - 6) / 1.2f;
            ds[i] = (float)cy - d * (float)(hh / 2 - 6) / 1.2f;
        }
        if (ef != EF_OFF)
            for (i = 0; i <= w; i++)
                pen_to(&pd, (float)(x0 + i), ds[i], K_FAINT);
        for (i = 0; i < w; i++) {                /* between the curve and the axis, the track's colour thinned */
            int32_t yt = f_floor(ys[i] + 0.5f);
            if (yt < cy)
                cv_rect(x0 + i, yt, 1, cy - yt, K_TINT);
            else if (yt > cy)
                cv_rect(x0 + i, cy + 1, 1, yt - cy, K_TINT);
        }
        for (i = 0; i <= w; i++)
            pen_to(&pn, (float)(x0 + i), ys[i], ef == EF_OFF ? K_DIM : K_NOW);
    } else if (ef == EF_TREMOLO) {
        float d = k1 * 0.5f, pn = k2 * 0.5f;
        pen_t pl = {0}, pr = {0};
        cv_rect(x0, y0 + hh - 4, w, 1, K_LINE);
        for (int32_t i = 0; i <= w; i++) {
            float ph = 2.0f * (float)i / (float)w;
            float gl = 1.0f - d - d * fm_sin_turns(ph), gr = 1.0f - d - d * fm_sin_turns(ph + pn);
            pen_to(&pr, (float)(x0 + i), (float)(y0 + hh - 4) - gr * (float)(hh - 12), K_FAINT);
            pen_to(&pl, (float)(x0 + i), (float)(y0 + hh - 4) - gl * (float)(hh - 12), K_NOW);
        }
    } else if (ef == EF_PHASER || ef == EF_UNIVIBE || ef == EF_AUTOWAH) {   /* the response, 20 Hz .. 20 kHz: the
                                                      * sweep's middle bright, its ends faint */
        static const float UV[4] = {1.0f, 0.068f, 0.032f, 3.19f};
        const float fq = ef == EF_PHASER ? 100.0f * fm_exp2f(k0 * 5.64f) : 0.0f;   /* (the send's: dsp/efx.c) */
        cv_rect(x0, y0 + hh - 4, w, 1, K_LINE);
        for (int c = 0; c < 3; c++) {
            pen_t pn = {0};
            float u = c == 0 ? 0.5f : c == 1 ? 0.0f : 1.0f;   /* (where in the sweep) */
            float fc = ef == EF_PHASER ? fq * fm_exp2f(k1 * 3.0f * (2.0f * u - 1.0f))
                     : ef == EF_UNIVIBE ? 220.0f * fm_exp2f(k1 * 3.2f * u * u) : 180.0f * fm_exp2f(4.3f * u);
            for (int32_t i = 0; i <= w; i++) {
                float f = 20.0f * fm_exp2f((float)i / (float)w * 9.97f), m;
                if (ef == EF_AUTOWAH) {
                    float r = f / fc, kq = 2.0f - 1.9f * k1, a = 1.0f - r * r;
                    m = fm_minf(fm_rsqrtf(a * a + kq * kq * r * r) * 0.35f, 1.0f);
                } else {
                    float ph = 0.0f, wet = ef == EF_PHASER ? 0.5f : 0.5f + 0.5f * k2;
                    for (int st = 0; st < 4; st++)
                        ph -= 2.0f * pic_atan(f / (fc * (ef == EF_PHASER ? 1.0f : UV[st])));
                    {
                        float re = 1.0f - wet + wet * fm_cosf(ph), im = wet * fm_sinf(ph);
                        m = fm_sqrtf(re * re + im * im);
                    }
                }
                pen_to(&pn, (float)(x0 + i), (float)(y0 + hh - 4) - m * (float)(hh - 12), c ? K_FAINT : K_NOW);
            }
        }
    } else if (ef == EF_CHORUS || ef == EF_JUNO) {   /* its two taps' delays over two cycles, crossing */
        float dm = ef == EF_JUNO ? (p[TP_SLOT[q][SL_K1]] % 3 == 2 ? 0.15f : 0.9f) : 0.1f + 0.9f * k1;
        pen_t pl = {0}, pr = {0};
        cv_rect(x0, cy, w, 1, K_LINE);
        for (int32_t i = 0; i <= w; i++) {
            float ph = 2.0f * (float)i / (float)w, tri = 4.0f * fm_fabsf(ph - fm_floorf(ph + 0.5f)) - 1.0f;
            pen_to(&pr, (float)(x0 + i), (float)cy + tri * dm * (float)(hh / 2 - 8), K_FAINT);
            pen_to(&pl, (float)(x0 + i), (float)cy - tri * dm * (float)(hh / 2 - 8), K_NOW);
        }
    } else if (ef == EF_DELAY) {                     /* the note, then its repeats at its time, each lower by repeats */
        const int dv = fx_delay_div(p[TP_SLOT[q][SL_K1]]);
        float fb = fx_delay_fb(p[TP_SLOT[q][SL_K2]]), lv = 1.0f;
        int32_t gap = (int32_t)(160.0f / (float)FX_DL_NOTE[dv] * (FX_DL_MARK[dv] == 'T' ? 0.667f : FX_DL_MARK[dv] ? 1.5f : 1.0f)) + 3;
        cv_rect(x0, y0 + hh - 4, w, 1, K_LINE);
        for (int32_t x = 0; x <= w && lv > 0.02f; x += gap) {
            int32_t bh = (int32_t)(lv * (float)(hh - 12));
            cv_rect(x0 + x, y0 + hh - 4 - bh, 3, bh, x ? K_NOW : K_DIM);
            lv = (x ? lv : mix) * (x ? fm_minf(fb, 0.97f) : 1.0f);
        }
    } else if (ef == EF_RUMBLE) {                    /* the hit, then the low tail rising as the duck lets go and fading
                                                      * by its decay (2 s across), its waves slower lower */
        float rt = 0.4f * fm_exp2f(k0 * 4.3f), cyc = 3.0f + 9.0f * k1;
        pen_t pt = {0}, pb = {0};
        cv_rect(x0, cy, w, 1, K_LINE);
        cv_rect(x0, y0 + 4, 3, hh - 8, K_DIM);
        for (int32_t i = 4; i <= w; i++) {
            float tt = 2.0f * (float)(i - 4) / (float)(w - 4), du = fm_minf(tt * 8.0f, 1.0f);
            float a = mix * du * du * fm_exp2f(-9.966f * tt / rt) * (float)(hh / 2 - 6), wv = fm_sin_turns(tt * cyc);
            pen_to(&pt, (float)(x0 + i), (float)cy - a * wv, K_NOW);
            pen_to(&pb, (float)(x0 + i), (float)cy + a * wv * 0.6f, K_FAINT);
        }
    } else if (ef == EF_LOFI) {                      /* a sine held in steps, the rate's */
        const int32_t st = 4 + (int32_t)((1.0f - k0) * 12.0f);
        for (int32_t x = 0; x < w; x += st)
            cv_rect(x0 + x, cy - (int32_t)(fm_sin_turns((float)x / (float)w) * (float)(hh / 2 - 8)), st, 3, K_NOW);
    } else if (ef == EF_REVERB) {                    /* rings spreading out, fainter as they go, wider by its size */
        for (int k = 0; k < 6; k++) {
            float rr = 8.0f + (float)k * (4.0f + 4.0f * k0);
            pen_t pn = {0};
            for (int i = 0; i <= 48; i++) {
                float u = (float)i / 48.0f;
                pen_to(&pn, (float)(x0 + w / 2) + rr * fm_sin_turns(u + 0.25f), (float)cy + rr * 0.5f * fm_sin_turns(u),
                       k & 1 ? K_DIM : K_NOW);
            }
        }
    } else if (ef == EF_RAT) {                       /* the clip's curve at its gain, the dry line faint */
        float g = 1.0f + 60.0f * k0, top;
        pen_t pd = {0}, pn = {0};
        {
            float a2 = g * g, sq = fm_rsqrtf(1.0f + a2 * a2);
            top = g * sq * fm_rsqrtf(sq);
        }
        cv_rect(x0, cy, w, 1, K_LINE);
        cv_rect(x0 + w / 2, y0, 1, hh, K_LINE);
        for (int32_t i = 0; i <= w; i++) {
            float x = (float)i * 2.0f / (float)w - 1.0f, u = g * x, a2 = u * u, sq = fm_rsqrtf(1.0f + a2 * a2);
            pen_to(&pd, (float)(x0 + i), (float)cy - x * (float)(hh / 2 - 6), K_FAINT);
            pen_to(&pn, (float)(x0 + i), (float)cy - u * sq * fm_rsqrtf(sq) / top * (float)(hh / 2 - 6), K_NOW);
        }
    }
    main_blit();
}
static void draw_sends(void)                         /* the instrument's two sends (a kit's: the key's), named with
                                                      * the effects on them */
{
    int trk = inst_trk(), key = ui.dsound % DL_NSOUND, drum = is_drum(trk);
    const uint8_t *p = par_now(trk), *sv = drum ? p + TP_DRUM(key, DP_SEND1) : p + TP_SEND1;
    uint32_t h = hash_u(hash_u(2166136261u, 0x5Eu), (uint32_t)trk | (uint32_t)sv[0] << 4 | (uint32_t)sv[1] << 12 |
                                          (uint32_t)p[TP_LEVEL] << 28);
    char t[32];
    h = hash_u(h, (uint32_t)fx_ef(AUX_FX1) | (uint32_t)fx_ef(AUX_FX2) << 5 | (uint32_t)fx_mode(AUX_FX1) << 10 |
                      (uint32_t)fx_mode(AUX_FX2) << 14 | (uint32_t)(drum ? key + 1 : 0) << 18);
    if (!begin_main(h))
        return;
    s_cat(s_cat(t, drum ? DL_SOUND_NAME[key] : TRACK_NAME[trk]), " sends to");
    cv_text(12, 4, &FONT_B, t, K_TEXT);
    for (int i = 0; i < 2; i++) {
        int32_t y = 40 + i * 32, v = sv[i] * 132 / 127;
        if (LARGE) {                                 /* large type: the name and the effect on a line, the send under */
            y = 28 + i * 27;
            v = sv[i] * 216 / 127;
            cv_text(12, y - 2, &FONT_S, i == 0 ? "FX I" : "FX II", K_TEXT);
            text_r(228, y, &FONT_XS, fx_label(AUX_FX1 + i), K_DIM);
            cv_round(12, y + 19, 216, 6, 3, K_LINE);
            if (v > 0)
                cv_round(12, y + 19, v < 6 ? 6 : v, 6, 3, K_NOW);
            continue;
        }
        cv_text(12, y - 2, &FONT_S, i == 0 ? "FX I" : "FX II", K_TEXT);
        cv_text(12, y + 11, &FONT_XS, fx_label(AUX_FX1 + i), K_DIM);
        cv_round(92, y + 3, 132, 8, 4, K_LINE);
        if (v > 0)
            cv_round(92, y + 3, v < 8 ? 8 : v, 8, 4, K_NOW);
    }
    main_blit();
}
/* an LFO shape at phase ph (0..1), -1..1: 0 sine, 1 saw (falling), 2 exponential, 3 square, 4 blip, 5 ramp (rising) */
static float lfo_shape_v(int shape, float ph)
{
    switch (shape) {
    case 1: return 1.0f - 2.0f * ph;
    case 2: return 2.0f * fm_exp2f(-8.0f * ph) - 1.0f;
    case 3: return ph < 0.5f ? 1.0f : -1.0f;
    case 4: return ph < 0.08f ? 1.0f : -1.0f;
    case 5: return 2.0f * ph - 1.0f;
    default: return fm_sin_turns(ph);
    }
}
static void draw_lfo(void)
{
    int trk = sel_trk();
    const uint8_t *p = par_now(trk);
    int lt = p[TP_LTYPE] % NLT, sh = p[TP_LX2];
    uint32_t h = hash_u(hash_u(2166136261u, 0x1Fu), (uint32_t)trk | (uint32_t)lt << 4 | (uint32_t)p[TP_LAMT] << 8 |
                                          (uint32_t)sh << 16 | (lt ? ui.frame / 2 : 0u) << 24);
    int32_t x0 = 16, y0 = 12, w = 208, hh = 84, cy = y0 + hh / 2, amp = 6 + p[TP_LAMT] * (hh / 2 - 8) / 127;
    uint32_t r = 12345u;
    if (!begin_main(h))
        return;
    cv_rect(x0, cy, w, 1, K_LINE);
    if (lt == LT_OFF)
        text_c(120, 40, &FONT_S, "LFO off", K_DIM);
    {
        pen_t pn = {0};
        float scroll = (float)(ui.frame / 2u) * 2.0f;     /* the curve slides left as it runs */
        (void)r;
        for (int32_t x = 0; lt && x <= w; x++) {
            float u = (float)x + scroll, v;              /* v: -1..1 */
            if (lt == LT_WAVE && sh == 4) {              /* random: a held value a cycle (128 px), stepped */
                uint32_t k = (uint32_t)(u / 128.0f), h0 = (k * 2654435761u) >> 8;
                v = (float)(h0 & 1023u) / 511.5f - 1.0f;
            } else if (lt == LT_WAVE || lt == LT_WOBBLE) /* their shapes (synth.c lfo_wave_value, lfo_wave_trem) */
                v = lfo_shape_v(lt == LT_WAVE ? (sh & 3) == 1 ? 0 : (sh & 3) == 0 ? 3 : (sh & 3) == 2 ? 5 : 1 : sh,
                                u / 128.0f - fm_floorf(u / 128.0f));
            else if (lt == LT_DRIFT) {                   /* a held random value every 24 px, slewed between */
                uint32_t k = (uint32_t)(u / 24.0f), h0 = (k * 2654435761u) >> 8, h1 = ((k + 1u) * 2654435761u) >> 8;
                float t = (u - (float)k * 24.0f) / 24.0f, s0 = (float)(h0 & 1023u) / 511.5f - 1.0f,
                      s1 = (float)(h1 & 1023u) / 511.5f - 1.0f;
                t = t * t * (3.0f - 2.0f * t);
                v = s0 + (s1 - s0) * t;
            } else {                                     /* pump: down at once, back up exponentially */
                float t = (u - fm_floorf(u / 48.0f) * 48.0f) / 48.0f;
                v = 1.0f - 2.0f * ease_exp(t);
            }
            pen_to(&pn, (float)(x0 + x), (float)cy - v * (float)amp, K_NOW);
        }
    }
    if (lt == LT_WAVE || lt == LT_DRIFT) {
        char t[40], num[16], unit[6], *e = s_cat(t, "to ");
        tp_text_par(TP_LDEST, p[TP_LDEST], p, num, unit);
        e = s_cat(s_cat(e, num), " ");
        tp_text_par(TP_LX, p[TP_LX], p, num, unit);
        s_cat(e, num);
        cv_text(x0, y0 + hh + 3, &FONT_XS, t, K_DIM);
    } else if (lt)
        cv_text(x0, y0 + hh + 3, &FONT_XS, tp_info_par(TP_LTYPE, p)->names[lt], K_DIM);
    main_blit();
}
#define BLACK_KEY(n) ((0x54Au >> ((n) % 12)) & 1)          /* C# D# F# G# A#: a black key's semitones */
static void draw_players(void)
{
    int trk = sel_trk(), pl = pl_type(trk);
    const player_t *pr = &seq.player[trk < NTRK ? trk : 0];
    uint32_t h = hash_u(hash_u(2166136261u, 0x9Cu), (uint32_t)trk | (uint32_t)pl << 4 | (uint32_t)pr->nheld << 8 |
                                          (uint32_t)pr->nchord << 12 | (uint32_t)pr->arp_note << 16 | (uint32_t)pr->chord[0] << 24);
    char t[64], *e;
    for (int i = 0; i < 16; i++)
        h = hash_u(h, pr->held[i] | (uint32_t)(i < pr->nsounding ? pr->sounding[i] : 0) << 8);
    for (int i = 0; i < SN * 2; i++)
        h = hash_u(h, pr->chord[i]);
    for (int i = 0; i < 4; i++)
        h = hash_u(h, pr->phys[i]);
    h = hash_u(h, (uint32_t)ui.oct + 8u);
    if (!begin_main(h))
        return;
    if (is_drum(trk)) {
        cv_text(12, 4, &FONT_B, "no player", K_DIM);
        cv_text(12, 24, F_CAP, "a kit's keys are its sounds: no arp, no strum", K_DIM);
        cv_text(12, LY(48, 40), LY(&FONT_S, F_CAP), "WEAVE is on the last page (SELECT)", K_DIM);
        main_blit();
        return;
    }
    int32_t ty = title_sub(tp_info_par(TP_PLAYER, proj.trk[trk].par)->names[pl],
                           pl == PL_OFF ? "the keys play as they are" : pl == PL_ARP ? "held notes play one after another"
                           : pl == PL_STRUM ? "one key strums the chord; keys together set it"
                           : pl == PL_RAND ? "active slots: notes from the pool" : "notes stay until the next", K_DIM);
    e = s_cat(t, pl == PL_STRUM ? "chord: " : "held: ");
    if (pl == PL_STRUM) {
        e = chord_name(pr->chord, pr->nchord < SN * 2 ? pr->nchord : SN * 2, e);
        for (int i = 0; i < pr->nchord && i < SN * 2; i++) {
            if (i)
                e = s_cat(e, " ");
            e = note_name(pr->chord[i], e);
        }
    } else {
        for (int i = 0; i < pr->nheld && i < 8; i++) {
            if (i)
                e = s_cat(e, " ");
            e = note_name(pr->held[i], e);
        }
        if (!pr->nheld)
            e = s_cat(e, "-");
    }
    text_fit(12, LY(48, ty), LY(&FONT_S, &FONT_XS), t, 216, K_TEXT);
    {   /* the keys as they are under the hands (F3 .. G5 at this octave): down (held, or latched by hold) in the track's
         * ink, the notes the player sounds now bright, the strum's chord marked under its keys (large type: shorter) */
        int base = KEY_BASE + ui.oct * 12, wi = 0;
        int32_t wx[27], y0 = LY(64, ty + 21), wh = LY(44, MAIN_H - ty - 29), bh = LY(27, (MAIN_H - ty - 29) * 3 / 5), ww = 13;
        uint16_t ink = trk_ink(trk);
        for (int k = 0; k < 27; k++)                 /* the white keys first, then the black on top */
            wx[k] = BLACK_KEY(base + k) ? 6 + wi * ww + 2 - 4 : 6 + (wi++) * ww + 2;
        for (int pass = 0; pass < 2; pass++)
            for (int k = 0; k < 27; k++) {
                int n = base + k, blk = BLACK_KEY(n), down = 0, snd = 0, ch = 0;
                if (blk != pass)
                    continue;
                for (int i = 0; i < pr->nheld && i < 16; i++)
                    down |= pr->held[i] == n;
                down |= n < 128 && (pr->phys[n >> 5] >> (n & 31) & 1u);
                for (int i = 0; i < pr->nsounding && i < 16; i++)
                    snd |= pr->sounding[i] == n;
                snd |= pr->arp_note == n;
                for (int i = 0; i < pr->nchord && i < SN * 2; i++)
                    ch |= pr->chord[i] == n;
                uint16_t c = snd ? K_TEXT : down ? ink : blk ? K_BG : K_PANEL2;
                if (blk) {
                    cv_round(wx[k], y0, 9, bh, 2, c);
                    if (!snd && !down)
                        cv_frame(wx[k], y0, 9, bh, 2, K_LINE, c);
                } else
                    cv_round(wx[k], y0, ww - 1, wh, 2, c);
                if (ch && pl == PL_STRUM)            /* the chord a key strums: a dot under each of its notes */
                    cv_round(wx[k] + (blk ? 2 : 3), blk ? y0 + bh + 3 : y0 + wh + 3, 5, 4, 2, ink);
            }
        for (int i = 0; i < pr->nheld && i < 16; i++)    /* a held note off these keys (another octave): an arrow */
            if (pr->held[i] < base || pr->held[i] >= base + 27)
                cv_text(pr->held[i] < base ? 0 : 232, y0 + 16, &FONT_XS, pr->held[i] < base ? "<" : ">", ink);
    }
    main_blit();
}
/* the pitch classes a random track may use at present (the pool picker's dots): its follow mode's notes */
static int rand_in_scale(int t, int pc)
{
    int mode = proj.trk[t].par[TP_BRAIN] % NBM;
    if (!((seq.brain_route >> t) & 1u) || mode == BM_NO)
        return 0;
    if (mode >= BM_HARMONY && seq.brain_chord) {
        for (int i = 0; i < SN; i++) {
            int n = (int)(seq.brain_chord >> (8 * i)) & 0xFF;
            if (n && n % 12 == pc)
                return 1;
        }
        return 0;
    }
    return (BRAIN_SCALE[seq.brain_scale % NBS] >> (((pc - seq.brain_key) % 12 + 12) % 12)) & 1u;
}
/* the random pool picker (chord mode on a PL_RAND track): the keys toggle pitch classes of the current
 * pattern's pool; a dot under a key marks the notes the track's follow mode allows */
static void draw_step_pool(void)
{
    int trk = sel_trk(), pat = ui.chord_pat % NPATT;
    uint16_t pool = lane_pool_get(pats.lane, trk, pat);
    uint32_t h = hash_u(hash_u(hash_u(2166136261u, 0xC4u),
                               (uint32_t)trk | (uint32_t)pat << 4 | (uint32_t)ui.oct << 12), pool);
    char t[48], *e;
    for (int i = 0; i < NKEYS; i++)
        h = hash_u(h, ui.kmode[i] == KM_STEPCHORD ? (uint32_t)ui.knote[i] + 1u : 0u);
    h = hash_u(hash_u(h, seq.brain_chord), (uint32_t)seq.brain_key | (uint32_t)seq.brain_scale << 8 |
                                                  (uint32_t)seq.brain_route << 16);
    if (!begin_main(h))
        return;
    num_s((uint32_t)pat + 1u, s_cat(t, "POOL P"));
    cv_text(12, 4, &FONT_B, t, K_REC);
    cv_text(12 + text_w(&FONT_B, t) + 8, 4, &FONT_XS, "[SEQ] exit", K_DIM);
    e = s_cat(t, "notes: ");
    if (!pool)
        e = s_cat(e, "all");
    else {
        int any = 0;
        for (int i = 0; i < 12; i++)
            if (pool >> i & 1u) {
                if (any)
                    e = s_cat(e, " ");
                e = note_name(60 + i, e);
                any = 1;
            }
    }
    text_fit(12, 24, &FONT_S, t, 216, K_TEXT);
    {
        int base = KEY_BASE + ui.oct * 12, wi = 0;
        int32_t wx[27], y0 = 48, wh = 44, bh = 27, ww = 13;
        uint16_t ink = trk_ink(trk);
        for (int k = 0; k < 27; k++)
            wx[k] = BLACK_KEY(base + k) ? 6 + wi * ww + 2 - 4 : 6 + (wi++) * ww + 2;
        for (int pass = 0; pass < 2; pass++)
            for (int k = 0; k < 27; k++) {
                int n = base + k, blk = BLACK_KEY(n), on = 0, down = 0, sc = rand_in_scale(trk, n % 12);
                if (blk != pass)
                    continue;
                on = (pool >> (n % 12)) & 1u;
                for (int i = 0; i < NKEYS; i++)
                    down |= ui.kmode[i] == KM_STEPCHORD && ui.knote[i] == n;
                {
                    uint16_t c = down ? K_TEXT : on ? ink : blk ? K_BG : K_PANEL2;
                    if (blk) {
                        cv_round(wx[k], y0, 9, bh, 2, c);
                        if (!down)
                            cv_frame(wx[k], y0, 9, bh, 2, K_LINE, c);
                    } else
                        cv_round(wx[k], y0, ww - 1, wh, 2, c);
                    if (sc)                           /* a note the follow mode allows: a dot under its key */
                        cv_rect(wx[k] + (blk ? 2 : 3), y0 + wh + 3, 5, 3, K_NOW);
                }
            }
    }
    main_blit();
}
/* chord mode (a step held, SEQ tapped), on Steps: the keys are a keyboard (Players') writing the held step's chord
 * instead of playing it. Shown in red (K_REC), as every lock is: the step's own notes lit in the track's ink, a key
 * still down bright, so the chord being built is always legible, not "in limbo" */
static void draw_step_chord(void)
{
    int trk = sel_trk(), first = 0;
    const step_t *st;
    uint32_t h;
    int s;
    for (int i = 0; i < 16; i++)
        if (ui.chord_held >> i & 1u) {
            first = i;
            break;
        }
    s = ui.chord_bar * 16 + first;
    st = chord_step(trk, first);
    h = hash_u(hash_u(2166136261u, 0xC2u), (uint32_t)trk | (uint32_t)s << 5 | (uint32_t)ui.oct << 13);
    char t[48], *e;
    uint8_t nt[SN];
    int any = 0;
    for (int i = 0; i < SN; i++)
        h = hash_u(h, st->note[i]);
    for (int i = 0; i < NKEYS; i++)
        h = hash_u(h, ui.kmode[i] == KM_STEPCHORD ? (uint32_t)ui.knote[i] + 1u : 0u);
    if (!begin_main(h))
        return;
    num_s((uint32_t)s + 1u, s_cat(t, "STEP "));
    cv_text(12, 4, &FONT_B, t, K_REC);
    cv_text(12 + text_w(&FONT_B, t) + 8, 4, &FONT_XS, "[SEQ] exit", K_DIM);
    for (int i = 0; i < SN; i++)
        if (st->note[i])
            nt[any++] = st->note[i];
    e = chord_name(nt, any, s_cat(t, "chord: "));
    any = 0;
    for (int i = 0; i < SN; i++)
        if (st->note[i]) {
            if (any)
                e = s_cat(e, " ");
            e = note_name(st->note[i], e);
            any = 1;
        }
    if (!any)
        e = s_cat(e, "-");
    text_fit(12, 24, &FONT_S, t, 216, K_TEXT);
    {
        int base = KEY_BASE + ui.oct * 12, wi = 0;
        int32_t wx[27], y0 = 48, wh = 44, bh = 27, ww = 13;
        uint16_t ink = trk_ink(trk);
        for (int k = 0; k < 27; k++)
            wx[k] = BLACK_KEY(base + k) ? 6 + wi * ww + 2 - 4 : 6 + (wi++) * ww + 2;
        for (int pass = 0; pass < 2; pass++)
            for (int k = 0; k < 27; k++) {
                int n = base + k, blk = BLACK_KEY(n), on = 0, down = 0;
                if (blk != pass)
                    continue;
                for (int i = 0; i < SN; i++)
                    on |= st->note[i] == n;
                for (int i = 0; i < NKEYS; i++)
                    down |= ui.kmode[i] == KM_STEPCHORD && ui.knote[i] == n;
                {
                    uint16_t c = down ? K_TEXT : on ? ink : blk ? K_BG : K_PANEL2;
                    if (blk) {
                        cv_round(wx[k], y0, 9, bh, 2, c);
                        if (!on && !down)
                            cv_frame(wx[k], y0, 9, bh, 2, K_LINE, c);
                    } else
                        cv_round(wx[k], y0, ww - 1, wh, 2, c);
                }
            }
    }
    main_blit();
}
/* the Mix page: a strip a track, its meter (after the fader, eng_peak) and its fader on one scale in dB, +6 at the
 * top, -48 at the foot, a line at 0 dB. The meter falls 24 dB a second; its peak holds a second, then falls too.
 * The conductor and the throws make no sound: their name alone */
#define MIX_TOP 6
#define MIX_H 80
#define MIX_DB_HI 6.0f
#define MIX_DB_LO (-48.0f)
static int32_t mix_y(float db)                   /* a level in dB: its height in the well, 0 .. MIX_H */
{
    db = db > MIX_DB_HI ? MIX_DB_HI : db < MIX_DB_LO ? MIX_DB_LO : db;
    return (int32_t)((db - MIX_DB_LO) * (float)MIX_H / (MIX_DB_HI - MIX_DB_LO) + 0.5f);
}
static float lin_db(float g) { return g > 1e-6f ? fm_log2f(g) * 6.0206f : -120.0f; }
static int lev_now(int t)                             /* a track's level now (a lock's, a motion's; an FX track's return) */
{
    const int p = is_fx(t) ? TP_CMIX : TP_LEVEL, lv = engine_live(t, p);
    return lv >= 0 ? lv : proj.trk[t].par[p];
}
static void draw_mix(void)
{
    static float m_db[NALLTRK], m_hold[NALLTRK];
    static uint32_t m_hold_ms[NALLTRK], m_last;
    static const char *const SHORT_NM[NALLTRK] = {"1", "2", "3", "4", "5", "6", "7", "8", "CO", "TH", "F1", "F2", "WV"};
    static const int8_t COL[NALLTRK] = {0, 1, 2, 3, 4, 5, 6, 7, -1, -1, 8, 9, -1};   /* the strips' places (the conductor and the
                                                                              * throws make no sound: none) */
    uint32_t now = plat_ms(), dt = now - m_last;
    int sel = sel_trk();
    uint32_t h = hash_u(hash_u(hash_u(2166136261u, 0x3Cu), (uint32_t)sel | (uint32_t)seq.mute << 4), ui.solo | (uint32_t)ui.solo_lat << 16);
    for (int t = 0; t < NTRK; t++)               /* (the mixer's mutes) */
        h = hash_u(h, proj.mixm[t]);
    float fall = (float)(dt > 1000u ? 1000u : dt) * 0.024f;
    m_last = now;
    for (int t = 0; t < NALLTRK; t++) {          /* the engine's loudest since the last look, then the fall */
        float db = lin_db((float)ui_peak[t] * (1.0f / 16383.5f));   /* (meters_take's) */
        if (t < NTRK && ui_ghost[t] && lev_now(t))   /* a muted track's steps: a meter in grey all the same (at its
                                                      * fader, less for a softer step), an empty track's none */
            db = fm_maxf(db, lin_db(tp_level_gain(lev_now(t)) * (float)ui_ghost[t] * (1.0f / 127.0f)) - 6.0f);
        if (dt > 250u)                           /* (back on the page: what piled up meanwhile is old) */
            m_db[t] = m_hold[t] = MIX_DB_LO - 1.0f;
        m_db[t] = fm_maxf(db, m_db[t] - fall);
        if (db >= m_hold[t]) {
            m_hold[t] = db;
            m_hold_ms[t] = now;
        } else if (now - m_hold_ms[t] > 1000u) {
            m_hold[t] -= fall;
        }
        h = hash_u(h, (uint32_t)mix_y(m_db[t]) | (uint32_t)mix_y(m_hold[t]) << 8 |
                      (uint32_t)lev_now(t) << 16);
    }
    if (!begin_main(h))
        return;
    int32_t y0 = MIX_TOP + MIX_H;                /* the wells' foot */
    static const int8_t MARK[3] = {0, -12, -24};
    static const char *const MARK_NM[3] = {"0", "-12", "-24"};
    for (int i = 0; i < 3; i++)                  /* the scale, at the right */
        text_r(236, y0 - mix_y((float)MARK[i]) - 4, &FONT_XS, MARK_NM[i], K_FAINT);
    for (int t = 0; t < NALLTRK; t++) {
        if (COL[t] < 0)
            continue;
        int32_t x = 4 + COL[t] * 18 + (COL[t] >= 8 ? 14 : 0);
        int muted = t < NSEQT && (seq.mute >> t & 1u), sound = t < NTRK || is_fx(t);
        uint16_t ink = muted ? K_FAINT : trk_ink(t);      /* (muted: its steps, grey) */
        /* the mixer's (audio) state in the number's place, centred on the column: a crossed speaker muted (grey out
         * of a solo), "W" wet only (in its ink); else the number */
        const int out = t < NTRK && ui.solo && !(ui.solo >> t & 1u), in = t < NTRK && (ui.solo >> t & 1u);
        const int amute = out || (t < NTRK && proj.mixm[t] == MIX_MUTE && !in);
        if (amute) {
            const uint16_t c = out ? K_FAINT : t == sel ? K_TEXT : K_DIM;
            const int32_t gx = x + 7 - 5, gy = 98;      /* (10 wide: the cone 4, a gap, the cross 5; 7 tall, on the
                                                         * digits' middle: their caps 97 .. 105) */
            cv_rect(gx, gy + 2, 2, 3, c);
            cv_rect(gx + 2, gy + 1, 1, 5, c);
            cv_rect(gx + 3, gy, 1, 7, c);
            for (int i = 0; i < 5; i++) {
                cv_rect(gx + 5 + i, gy + 1 + i, 1, 1, c);
                cv_rect(gx + 9 - i, gy + 1 + i, 1, 1, c);
            }
        } else if (t < NTRK && proj.mixm[t] == MIX_WET)
            text_c(x + 7, 94, &FONT_XS, "W", trk_ink(t));
        else
            text_c(x + 7, 94, &FONT_XS, SHORT_NM[t], t == sel ? K_TEXT : muted || !sound ? K_FAINT : K_DIM);
        if (muted)                               /* the scene's (note) mute: a dim row and a bar */
            cv_rect(x + 2, 104, 10, 2, K_FAINT);
        if (t == sel)
            cv_frame(x - 2, MIX_TOP - 2, 18, MIX_H + 4, 4, K_SEL, K_BG);
        if (!sound)
            continue;
        int32_t lv = mix_y(m_db[t]), pk = mix_y(m_hold[t]);
        int v = lev_now(t);
        int32_t fy = v ? mix_y(lin_db(tp_level_gain(v))) : 0;
        cv_round(x, MIX_TOP, 14, MIX_H, 3, K_PANEL);
        for (int i = 0; i < 3; i++)              /* 0, -12, -24 dB */
            cv_rect(x + (i ? 4 : 0), y0 - mix_y((float)MARK[i]), i ? 6 : 14, 1, K_PANEL2);
        if (lv > 0)
            cv_round(x + 3, y0 - lv, 8, lv, 2, ink);
        if (pk > 1)
            cv_rect(x + 3, y0 - pk, 8, 1, ink);
        cv_round(x - 1, y0 - fy - 2, 16, 4, 1, amute ? K_FAINT : t == sel ? K_SEL : K_DIM);   /* the fader (dim: muted) */
        cv_rect(x + 1, y0 - fy, 12, 1, K_BG);
    }
    if (ui.solo)
        text_r(236, 104, &FONT_XS, ui.solo_lat ? "SOLO" : "solo", ui.solo_lat ? K_SEL : K_TEXT);
    main_blit();
}
static void draw_master(void)
{
    int pg = ui.page == P_MASTER ? 0 : 2;
    uint8_t ms[NMS];
    const uint8_t *m = mod_shown(MODT_MST, proj.master, ms, NMS);   /* (MOD VIEW: as the MOD lanes play them) */
    uint32_t h = hash_u(hash_u(2166136261u, 0x3Au), (uint32_t)pg);
    int32_t x0 = 16, y0 = 12, w = 208, hh = 84, cy = y0 + hh / 2;
    int pv = pg == 2 ? (int)(eng_pump_db * (float)(hh - 10) / 24.0f)   /* the pump's duck, now (pixels) */
                     : (int)(eng_clip * (float)(hh - 10));   /* how deep CLIP is clipping, now (its ceiling: all) */
    for (int i = 0; i < NMS; i++)
        h = hash_u(h, m[i]);
    h = hash_u(h, (uint32_t)pv);
    if (!begin_main(h))
        return;
    if (pg == 0)
        w = 150;                                     /* (CLIP, KNOB 4, its column on the right) */
    cv_rect(x0, cy, w, 1, K_LINE);
    if (pg == 0) {
        pen_t pn = {0};
        const int32_t cx = 186, v = m[MS_CLIP] * (hh - 10) / 127;
        cv_round(cx, y0, 30, hh, 4, K_PANEL);        /* CLIP: its drive up from the bottom, as Master out's knobs; */
        cv_round(cx, y0 + hh - v, 30, v, 4, K_NOW);
        if (pv > 0)                                  /* how deep it clips now, hanging from the top, as the pump's
                                                      * duck */
            cv_round(cx + 4, y0 + 2, 22, pv < hh - 4 ? pv : hh - 4, 3, K_NOW_M);
        text_c(cx + 15, y0 + hh + 3, &FONT_XS, m[MS_CLIP] ? "push" : "push off", K_DIM);
        for (int32_t x = 0; x <= w; x++) {           /* the three bands, eased into each other */
            float u = (float)x / (float)w * 3.0f - 0.5f, g0, g1, t, b;
            int i = u < 0.0f ? 0 : u >= 2.0f ? 2 : (int)u;
            static const uint8_t BAND[3] = {MS_EQ_LO, MS_EQ_MID, MS_EQ_HI};
            t = u - (float)i;
            t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
            t = t * t * (3.0f - 2.0f * t);
            g0 = (float)m[BAND[i]];
            g1 = (float)m[BAND[i < 2 ? i + 1 : 2]];
            b = g0 + (g1 - g0) * t;
            pen_to(&pn, (float)(x0 + x), (float)cy - (b - 64.0f) * (float)(hh / 2 - 6) / 64.0f, K_NOW);
        }
        cv_text(x0, y0 + hh + 3, &FONT_XS, "low  mid  high: 64 is flat", K_DIM);
    } else {
        static const char *const NM[4] = {"drums", "melodic", "comp", "pump"};   /* (the knobs' names, MS_NAME) */
        for (int i = 0; i < 4; i++) {
            int32_t x = x0 + i * 52, v = m[MS_PERC + i] * (hh - 10) / 127;
            cv_round(x + 8, y0, 30, hh, 4, K_PANEL);
            cv_round(x + 8, y0 + hh - v, 30, v, 4, i == 2 ? K_NOW_M : K_NOW);
            if (i == 3 && pv > 0)                    /* the pump: its duck now, hanging from the top */
                cv_round(x + 12, y0 + 2, 22, pv < hh - 4 ? pv : hh - 4, 3, K_NOW_M);
            text_c(x + 23, y0 + hh + 3, &FONT_XS, NM[i], K_DIM);
        }
    }
    main_blit();
}
/* Master filter: each filter's response (fx.c master_filter_db), 20 Hz .. 20 kHz across, +12 .. -24 dB down; the
 * drums', the melodic group's and the mix's, one above the other, their knobs' values beside */
static void draw_mfilt(void)
{
    static const char *const NM[3] = {"drums", "melodic", "mix"};
    uint8_t ms[NMS];
    const uint8_t *m = mod_shown(MODT_MST, proj.master, ms, NMS);   /* (MOD VIEW: as the MOD lanes play them) */
    uint32_t h = hash_u(hash_u(2166136261u, 0x3Bu), (uint32_t)m[MS_DFILT] | (uint32_t)m[MS_MELF] << 8 |
                                                     (uint32_t)m[MS_MFILT] << 16 | (uint32_t)m[MS_FRES] << 24);
    const int32_t x0 = 16, w = 208, y0 = LARGE ? 2 : 10, gap = 4;
    const int32_t ph = (MAIN_H - y0 - 2 * gap - 6) / 3 < 30 ? (MAIN_H - y0 - 2 * gap - 6) / 3 : 30;
    if (!begin_main(h))
        return;
    for (int j = 0; j < 3; j++) {
        const int v = m[j == 0 ? MS_DFILT : j == 1 ? MS_MELF : MS_MFILT], r = m[MS_FRES];
        const int32_t top = y0 + j * (ph + gap), zero = top + ph / 3;   /* (0 dB a third down: +12 above, -24 below) */
        const int on = master_filter_hz(v) != 0.0f;
        pen_t pn = {0};
        char t[16];
        cv_round(x0, top, w, ph, 4, K_PANEL);
        cv_rect(x0 + 4, zero, w - 8, 1, K_LINE);
        for (int32_t x = 0; x <= w - 8; x += 2) {
            float hz = 20.0f * fm_exp2f((float)x * (9.9657843f / (float)(w - 8))), db = master_filter_db(v, r, hz);
            db = db > 12.0f ? 12.0f : db < -24.0f ? -24.0f : db;
            pen_to(&pn, (float)(x0 + 4 + x), (float)zero - db * ((float)ph / 36.0f), on ? K_NOW : K_DIM);
        }
        mfilt_text(v, t);
        {   /* (the names over the curve, on the panel's ground) */
            const int32_t ty = top + ph - 2 - FONT_XS.h, tw = text_w(&FONT_XS, t);
            cv_rect(x0 + 6, ty, text_w(&FONT_XS, NM[j]) + 4, FONT_XS.h, K_PANEL);
            cv_rect(x0 + w - 10 - tw, ty, tw + 4, FONT_XS.h, K_PANEL);
            cv_text(x0 + 8, ty, &FONT_XS, NM[j], K_DIM);
            text_r(x0 + w - 8, ty, &FONT_XS, t, on ? K_TEXT : K_DIM);
        }
    }
    main_blit();
}
static const felucca_font_t *setup_f;                /* setup_row's face (0: FONT_S) */
static void setup_row(int32_t y, const char *name, const char *val)
{
    const felucca_font_t *f = setup_f ? setup_f : &FONT_S;
    cv_text(12, y, f, name, K_DIM);
    text_r(228, y, f, val, K_TEXT);
}
/* the shared LFO: its shape across a cycle, a dot where it is now; the tracks it moves, lit */
static void draw_glfo(void)
{
    const uint8_t *g = proj.trk[AUX_BRAIN].par;
    int sh = g[TP_LTYPE] % (NGS + 1), now = (int)(engine_glfo_now() * 24.0f), route = (int)glfo_route();
    uint32_t h = hash_u(hash_u(2166136261u, 0x6F0u), (uint32_t)sh | (uint32_t)route << 4 | (uint32_t)g[TP_LDEST] << 12 |
                                           (uint32_t)(now + 32) << 16 | (uint32_t)g[TP_LAMT] << 24);
    h = hash_u(h, g[TP_P1]);
    int32_t x0 = 16, w = 208, cy = 40;
    pen_t pn = {0};
    char t[24];
    if (!begin_main(h))
        return;
    if (!sh) {
        text_c(120, 24, &FONT_B, "the shared LFO is off", K_TEXT);
        text_c(120, 50, &FONT_S, "turn KNOB 1 to pick a shape", K_DIM);
    } else {
        cv_rect(x0, cy, w, 1, K_LINE);
        for (int i = 0; i <= 52; i++) {           /* (the random shape: a few steps, as it holds a value a cycle) */
            float ph = (float)i / 52.0f, v;
            switch (sh - 1) {
            case GS_TRI: v = ph < 0.5f ? 4.0f * ph - 1.0f : 3.0f - 4.0f * ph; break;
            case GS_UP: v = 2.0f * ph - 1.0f; break;
            case GS_DOWN: v = 1.0f - 2.0f * ph; break;
            case GS_SQUARE: v = ph < 0.5f ? 1.0f : -1.0f; break;
            case GS_RANDOM: v = (float)((i / 13 * 5 + 3) % 7 - 3) / 3.0f; break;
            default: v = fm_sin_turns(ph); break;
            }
            pen_to(&pn, (float)x0 + ph * (float)w, (float)cy - v * 24.0f, K_NOW);
        }
        cv_round(x0 + w + 4, cy - now - 3, 6, 6, 3, K_TEXT);   /* (where it is now, at the right) */
        s_cat(s_cat(t, "moves "), GLFO_DEST[g[TP_LDEST] % NGD]);
        cv_text(16, 72, &FONT_XS, t, K_DIM);
        {                                         /* (on a kit: the sound it moves) */
            int ks = g[TP_P1];
            char kt[24];
            s_cat(s_cat(kt, "kits: "), ks >= 1 && ks <= NDRUM ? DL_SOUND_NAME[ks - 1] : "every sound");
            text_r(224, 72, &FONT_XS, kt, K_DIM);
        }
    }
    for (int i = 0; i < NTRK; i++) {             /* the tracks it moves: black keys 1-8 */
        int32_t x = 16 + i * 26, on = route >> i & 1;
        cv_round(x, 90, 22, 16, 4, on ? trk_ink(i) : K_PANEL);
        text_c(x + 11, 92, &FONT_XS, TRACK_NAME[i], on ? K_BG : K_DIM);
    }
    main_blit();
}
static void draw_setup(void)
{
    char ta[24], td[24];
    uint32_t h = hash_u(2166136261u + proj.clock, 0x5E700u | (uint32_t)proj_slot | (uint32_t)proj.usb_full << 4 | (uint32_t)seq.ext << 8 |
                                          (uint32_t)proj.rsv[0] << 12 | (uint32_t)proj.send_notes << 17 |
                                          (uint32_t)proj.rsv[1] << 20 | (uint32_t)(proj.rsv[2] & AUTOSAVE_OFF) << 26);
    bt_audio_text(ta);
    bt_dev_text(td, "on", "off");
    h = hash_s(hash_s(h, ta), td);
    if (!begin_main(h))
        return;
    if (LARGE) {                                     /* large type: the rows the knobs under them do not show (clock,
                                                      * notes, theme: their values right there), at 15 px */
        setup_f = &LFONT_S;
        setup_row(1, "tempo from", seq.ext ? "MIDI clock in" : "LOOM-8");
        setup_row(17, "USB audio", USBA_NAME[proj.usb_full < 3u ? proj.usb_full : 0u]);
        setup_row(33, "autosave", proj.rsv[2] & AUTOSAVE_OFF ? "off: press SAVE" : "on");
        setup_row(49, "BT audio", ta);
        setup_row(65, "BT MIDI", td);
        setup_f = 0;
        main_blit();
        return;
    }
    setup_row(2, "tempo from", seq.ext ? "MIDI clock in" : "LOOM-8");
    setup_row(16, "clock", CLOCK_ROW[proj.clock % SEQ_NCLK]);
    setup_row(30, "sends notes", proj.send_notes ? "yes" : "no");
    setup_row(44, "USB audio", USBA_NAME[proj.usb_full < 3u ? proj.usb_full : 0u]);
    setup_row(58, "autosave", proj.rsv[2] & AUTOSAVE_OFF ? "off: press SAVE" : "on");
    setup_row(72, "BT audio", ta);
    setup_row(86, "BT MIDI", td);
    setup_row(100, "version", APP_VERSION);   /* (2026-11-27: in the theme's place, the THEME knob under it says it; the
                                                * version was the Debug page's alone, a release without it) */
    main_blit();
}
static uint32_t status_peak;                          /* the highest peak shown since the screen reader said it (ui.c) */
static void draw_status(void)
{
    char t[24], *e;
    uint32_t cpu = plat_cpu_pct(), peak = plat_cpu_peak_pct(), xr = plat_xruns();
    if (peak > status_peak)
        status_peak = peak;
    uint32_t h = hash_u(2166136261u, 0x57A00u | cpu | peak << 8 | (uint32_t)eng_voices << 16 | (uint32_t)eng_voices_max << 24);
    h = hash_u(h, xr ^ eng_guard_count << 8 ^ (uint32_t)engine_core2_state() << 28 ^ (uint32_t)d909_res_ok() << 31);
    if (!begin_main(h))
        return;
    if (LARGE)                                       /* (large type: the debug build's seven rows at 12 px) */
        setup_f = &LFONT_XS;
    e = num_s(cpu, t);
    e = s_cat(e, "%, peak ");
    e = num_s(peak, e);
    s_cat(e, "%");
    setup_row(LY(2, 1), "audio load", t);
    e = num_s((uint32_t)eng_voices, t);
    e = s_cat(e, " of ");
    e = num_s((uint32_t)eng_voices_max, e);
    setup_row(LY(18, 14), "voices", t);
    setup_row(LY(34, 27), "second core", CORE2_NAME[engine_core2_state() & 3]);
    if (LARGE) {                                     /* (large type: the guard and the dropouts one row) */
        s_cat(num_s(xr, s_cat(num_s(eng_guard_count, t), " / ")), "");
        setup_row(40, "guard trips / dropouts", t);
    } else {
        num_s(eng_guard_count, t);
        setup_row(50, "guard trips", t);
        num_s(xr, t);
        setup_row(66, "dropouts", t);
    }
    setup_row(LY(82, 53), "version", APP_VERSION);
    setup_row(LY(98, 66), "909 cymbals", d909_res_ok() ? "there" : "missing: reinstall");
    setup_f = 0;   /* (the resource region: the
                                                                                     * installer writes them) */
    main_blit();
}

/* step record on a play page: the cursor's bar as a strip of 16 cells along the main band's foot, so the
 * keys' writes are not blind */
#define RS_H 16
static void draw_rec_strip(void)
{
    int trk = sel_trk(), b = ui.cursor / 16;
    const tpat_t *tp;
    uint32_t h;
    if (!ui.rec_armed || playing() || !is_seqt(trk) || ui.card || lst.open || ui.page < P_ENGINE || ui.page > P_PLAYERS3)
        return;
    tp = tpat(trk);
    h = hash_u(hash_u(2166136261u, 0x55u), (uint32_t)ui.cursor | (uint32_t)trk << 8);
    for (int i = 0; i < 16; i++)
        h = hash_u(h, !step_empty(&tp->st[b * 16 + i]));
    if (h == ui.sig[5])
        return;
    ui.sig[5] = h;
    cv_begin(240, RS_H, K_BG);
    for (int i = 0; i < 16; i++) {
        int st = b * 16 + i;
        int32_t x = 8 + i * 14;
        uint16_t c = st >= tp->len ? K_BG : !step_empty(&tp->st[st]) ? K_TINT : K_PANEL;   /* (as Steps draws them) */
        cv_round(x, 3, 12, 11, 2, c);
        if (st == ui.cursor)
            cv_frame(x - 1, 2, 14, 13, 3, K_REC, c);
    }
    cv_blit(0, MAIN_Y + MAIN_H - RS_H);
}
static void ink_follow(void)                         /* another track: its ink, every band drawn again */
{
    int trk = sel_trk();
    const bands_t *b = &BANDS[!loom_type(large_type()) ? 0     /* (large type: its faces there, gfx_loom.c) */
                              : steps_page(ui.page) && is_seqt(trk) && trk != AUX_WEAVE ? 2 : 1];
    theme = THEME_OF_PROJ();
    if (k_th == theme && k_now == trk_ink(trk) && band == b)
        return;
    if (k_th != theme || band != b)                  /* another theme or type: the whole screen, the gaps between the
                                                      * bands too */
        lcd_fill(0, 0, 240, 240, K_BG);
    band = b;
    k_th = theme;
    k_now = trk_ink(trk);
    k_tint = trk_tint(trk);
    k_mid = ink_mix(k_now, theme->mid);
    for (uint32_t i = 0; i < sizeof ui.sig / sizeof ui.sig[0]; i++)
        ui.sig[i] ^= 0x5A5A5A5Au;
}
/* ARP's Weave page: what is woven into the track and how, in words with their names, and its first bar three
 * ways: its own steps, the other pattern's, and what plays woven (each step in the ink of the pattern it came from) */
static void weave_row(int32_t y, const char *label, uint16_t lc, const tpat_t *p, const tpat_t *a,
                      const tpat_t *b, int ka, int kb, int rule, uint16_t ca, uint16_t cb, int bar)
{
    cv_text(8, y - LARGE, LY(&FONT_XS, F_CAP), label, lc);   /* (large type: the label small, as the cells) */
    for (int j = 0; j < 16; j++) {               /* the bar shown (OCT): steps bar * 16 .., a shorter pattern round again */
        int32_t x = 54 + j * 11;
        int i = bar * 16 + j;
        step_t w;
        const step_t *st = &w, *own = &a->st[i % (a->len ? a->len : 1)];
        uint16_t c;
        if (p)
            st = &p->st[i % (p->len ? p->len : 1)];
        else
            weave_step(a, ka, b, kb, rule, (uint32_t)i, &w);
        c = step_empty(st) ? K_PANEL : p ? (p == a ? ca : cb) : memcmp(&w, own, sizeof w) ? cb : ca;
        cv_round(x, y + 1, 9, 10, 2, c);
        if (j % 4 == 0 && step_empty(st))
            cv_rect(x + 4, y + 9, 1, 2, K_LINE);     /* (the beats, where nothing is) */
    }
}
static void draw_weave(void)
{
    int trk = sel_trk(), f = wv_from(trk), r = wv_rule(trk);
    uint32_t h = hash_u(hash_u(2166136261u, 0x57u), (uint32_t)trk | (uint32_t)f << 4 | (uint32_t)wv_pat(trk) << 8 |
                                          (uint32_t)r << 12 | (uint32_t)cur_pat(trk) << 16 |
                                          (uint32_t)((seq.wv_live | seq.wv_rec) >> trk & 1u) << 24 |
                                          (uint32_t)wv_cued(trk) << 25 | (uint32_t)ui.bar << 26);
    char t[56], an[12], bn[12], *e;
    if (f)
        h = hash_pat(hash_pat(h, tpat(trk)), &pats.pat[f - 1][wv_pat(trk)]);
    if (!begin_main(h))
        return;
    if (!f) {
        s_cat(s_cat(s_cat(t, "Nothing woven into "), TRACK_NAME[trk]), " yet");
        text_c(120, LY(40, 26), text_w(&FONT_B, t) <= 228 ? &FONT_B : &LFONT_B, t, K_TEXT);   /* ("COND ...": a size down;
                                                       * the keys' hints below, at the foot) */
        main_blit();
        return;
    }
    num_s((uint32_t)cur_pat(trk) + 1u, s_cat(s_cat(an, TRACK_NAME[trk]), " P"));   /* "T2 P1", "T1 P3" */
    num_s((uint32_t)wv_pat(trk) + 1u, s_cat(s_cat(bn, TRACK_NAME[f - 1]), " P"));
    s_cat(s_cat(s_cat(t, bn), " woven into "), an);   /* "T1 P3 woven into T2 P1": T2 changes, T1 plays as it was */
    if (!LARGE)                                      /* (large type: the header and the knobs say it) */
        cv_text(8, 2, &FONT_B, t, K_TEXT);
    e = s_cat(s_cat(t, "by "), WEAVE_NAME[r]);
    e = s_cat(e, ": ");
    if (r == WV_BARS || r == WV_STEPS) {             /* in words, with their names */
        e = s_cat(s_cat(e, r == WV_BARS ? "a bar of " : "a step of "), an);
        s_cat(s_cat(e, ", then "), bn);
    } else
        s_cat(s_cat(s_cat(s_cat(e, r == WV_RHYTHM ? an : bn), "'s rhythm, "), r == WV_RHYTHM ? bn : an), "'s pitch");
    text_fit(8, LY(26, 2), LY(&FONT_XS, F_CAP), t, 228, K_NOW);   /* (large type: an explanation's size) */
    {
        const tpat_t *a = tpat(trk), *b = &pats.pat[f - 1][wv_pat(trk)];
        uint16_t ca = trk_ink(trk), cb = f - 1 == trk || trk_ink(f - 1) == ca ? K_TEXT : trk_ink(f - 1);
        int ka = wv_kind(trk), kb = wv_kind(f - 1), la = a->len > b->len ? a->len : b->len;
        int nb = (la + 15) / 16, bar = ui.bar < nb ? ui.bar : nb - 1;
        /* (large type: the rows 12 apart in the shorter band, the status line under them) */
        weave_row(LY(44, 20), an, K_DIM, a, a, b, ka, kb, r, ca, cb, bar);
        weave_row(LY(58, 33), bn, K_DIM, b, a, b, ka, kb, r, ca, cb, bar);
        cv_rect(54, LY(73, 46), 175, 1, K_LINE);
        weave_row(LY(77, 49), "plays", K_TEXT, 0, a, b, ka, kb, r, ca, cb, bar);
        if (nb > 1) {                                /* more than a bar: which one, OCT for the others */
            char bt[16];
            num_s((uint32_t)nb, s_cat(num_s((uint32_t)bar + 1u, s_cat(bt, "bar ")), " of "));
            cv_text(232 - text_w(F_CAP, bt), LY(98, 64), F_CAP, bt, K_DIM);
        }
    }
    if (!wv_cued(trk))
        s_cat(s_cat(s_cat(t, "on: "), TRACK_NAME[trk]), " plays woven");
    else if ((seq.wv_live | seq.wv_rec) >> trk & 1u)
        s_cat(t, "weaving now");
    else
        num_s((uint32_t)trk + 1u, s_cat(t, "press WEAVE's white key "));   /* (short: "bar 2 of 4" beside it) */
    text_fit(8, LY(98, 64), F_CAP, t, LY(228, 160), !wv_cued(trk) || (seq.wv_live | seq.wv_rec) >> trk & 1u ? K_NOW : K_DIM);
    main_blit();
}
/* the WEAVE track's page: the ten tracks it weaves (white keys 1-10), each with its weave, lit while it weaves */
static void draw_weave_play(void)
{
    uint32_t on = (uint32_t)(seq.wv_live | seq.wv_rec | seq.wv_always);
    uint32_t h = hash_u(hash_u(2166136261u, 0x5Au), on | (uint32_t)proj.trk[AUX_WEAVE].par[TP_E1] << 16);
    int any = 0;
    for (int t = 0; t < NSEQT; t++)
        h = hash_u(h, (uint32_t)wv_from(t) | (uint32_t)wv_pat(t) << 8 | (uint32_t)wv_rule(t) << 12);
    if (!begin_main(h))
        return;
    for (int t = 0; t < NSEQT; t++) {
        /* (large type: the cells 32 high, the name at 15 px and what it weaves with under it; the rule on ARP's Weave) */
        int32_t x = 6 + (t % 5) * 46, y = LY(2 + (t / 5) * 48, (t / 5) * 34);
        int f = wv_from(t), lit = on >> t & 1u;
        char wt[16];
        cv_round(x, y, 42, LY(46, 32), 5, lit ? K_NOW : f ? K_PANEL2 : K_PANEL);
        text_c(x + 21, LY(y + 3, y + 1), LARGE ? (t < NTRK ? &LFONT_S : &LFONT_XS) : t < NTRK ? &FONT_S : &FONT_XS,
               TRACK_NAME[t], lit ? K_BG : f ? K_TEXT : K_FAINT);
        if (f) {                                 /* "+T1 P3" (its own: "+P3"), the rule */
            if (t == f - 1)
                num_s((uint32_t)wv_pat(t) + 1u, s_cat(wt, "+P"));
            else
                num_s((uint32_t)wv_pat(t) + 1u, s_cat(s_cat(s_cat(wt, "+"), TRACK_NAME[f - 1]), " P"));
            text_c(x + 21, LY(y + 18, y + 17), F_CAP, wt, lit ? K_BG : K_DIM);
            if (!LARGE)
                text_c(x + 21, y + 29, &FONT_XS, WEAVE_NAME[wv_rule(t)], lit ? K_BG : K_DIM);
            any = 1;
        } else
            text_c(x + 21, LY(y + 22, y + 16), F_CAP, "-", K_FAINT);
    }
    cv_text(8, LY(99, 66), F_CAP, !any ? "no weaves yet: set one on a track's ARP"
                             : proj.trk[AUX_WEAVE].par[TP_E1] ? "tap a key to weave, again to stop" : "hold a key to weave its track",
            K_DIM);
    main_blit();
}
/* the MOD page: the lane picked as it plays over the pattern's steps (a glide's line, a step's stairs; its values dots,
 * the held step outlined, the keys' 16 steps shaded, the playhead), its name and kind above, the pattern below */
static void mod_curve(const lane_t *L, int len, int v0, int16_t *out)   /* each step's value as it plays (-1 none) */
{
    int last = -1, lv = v0;
    for (int s = 0, q; s < len; s++) {
        if (L->val[s] != LOCK_NONE) {
            out[s] = (int16_t)(lv = L->val[s]);
            last = s;
            continue;
        }
        for (q = s + 1; q < len && L->val[q] == LOCK_NONE; q++)
            ;
        if (!lane_glide(L) || q >= len)              /* a step lane, or past the last value: held (none before) */
            out[s] = (int16_t)(last >= 0 ? lv : -1);
        else if (last < 0)                           /* moving to the first value, from where the target is */
            out[s] = (int16_t)(v0 + (int)((float)(L->val[q] - v0) * lane_curve(lane_shape(L), (float)s / (float)q)));
        else
            out[s] = (int16_t)(lv + (int)((float)(L->val[q] - lv) * lane_curve(lane_shape(L), (float)(s - last) /
                                                                                   (float)(q - last))));
    }
}
/* a moving lane's value at p (steps, a step's centre at its index: between them, in its shape; mod_curve's) */
static float mod_curve_at(const lane_t *L, int len, int v0, float p)
{
    int a, q, la;
    float va;
    if (p < 0.0f)
        p = 0.0f;
    a = (int)p;
    for (la = a; la >= 0 && L->val[la] == LOCK_NONE; la--)
        ;
    for (q = a + 1; q < len && L->val[q] == LOCK_NONE; q++)
        ;
    va = la >= 0 ? (float)L->val[la] : (float)v0;
    if (q >= len)                                    /* (past the last value: held) */
        return va;
    if (la < 0)
        la = 0;
    return va + ((float)L->val[q] - va) * lane_curve(lane_shape(L), (p - (float)la) / (float)(q - la));
}
/* the pattern's length in bars ("32 bars", "1 bar"), else beats ("3 beats"), else sixteenths */
static void mod_len_text(int six, char *t)
{
    const char *u = six % 16 ? six % 4 ? " sixteenth" : " beat" : " bar";
    const int n = six % 16 ? six % 4 ? six : six / 4 : six / 16;
    s_cat(s_cat(num_s((uint32_t)n, t), u), n == 1 ? "" : "s");
}
/* the grid under a lane: lines at the bars (a bar a step or longer: every 4 bars), ticks at the beats (at the bars),
 * each at the step nearest; ticks only where they are 2 steps apart or more (a bar a step: every step) */
static void mod_grid(int len, int six, int32_t gx, int32_t gw, int32_t gy, int32_t gh)
{
    const int big = six >= 8;                        /* (a step half a bar or more: ticks the bars, lines every 4) */
    const int lu = big ? 64 : 16, tu = big ? 16 : 4;  /* sixteenths a line, a tick */
    for (int u = 1; u * tu < len * six; u++) {
        const int st = (u * tu + six / 2) / six, line = !(u * tu % lu);
        const int32_t x = gx + gw * st / len;
        if (st <= 0 || st >= len)
            continue;
        if (line)
            cv_rect(x, gy - 3, 1, gh + 6, K_FAINT);
        else if (big || tu >= 2 * six)               /* (a beat's tick 2 steps apart at least; a bar's each) */
            cv_rect(x, gy + gh + 1, 1, 3, K_DIM);
    }
}
static void draw_mod(void)
{
    uint8_t ix[NMODL];
    const int pt = mod_pat_now(), n = mod_lanes_now(ix), l = mod_lane_now();
    const modpat_t *mp = &pats.mod[pt];
    const int len = mp->len < 1 ? 1 : mp->len > NSTEP ? NSTEP : mp->len, pos = playing() && seq.mod_pat == pt ? seq.mod_pos : -1;
    const int six = mod_scale_six(mp->scale);
    const lane_t *L = l >= 0 ? &pats.lane[l] : 0;
    const int tg = L ? lane_mod_tgt(L) : 0, v0 = L ? mod_val(tg, L->param) : 0;
    const int32_t gx = 8, gw = 224, gy = LY(20, 18), fy = MAIN_H - F_CAP->h - 1, gh = fy - gy - 7;
    uint32_t h = hash_u(hash_u(2166136261u, 0x4Du), (uint32_t)pt | (uint32_t)(l + 1) << 4 | (uint32_t)len << 12 |
                                          (uint32_t)(pos + 1) << 20 | (uint32_t)ui.bar << 28);
    int16_t cv[NSTEP];
    char t[40];
    h = hash_u(h, (uint32_t)ui.held | (uint32_t)n << 16 | (uint32_t)v0 << 20 | (uint32_t)mp->scale << 28);
    h = hash_u(h, (uint32_t)ui.mod_tap | (uint32_t)ui.mod_tap_l << 8);
    h = hash_u(h, (uint32_t)pats.scene_mod[cur_scene()] | (uint32_t)(L ? L->pat : 0) << 8 | (uint32_t)ui.mod_learn << 16 |
                      (uint32_t)ui.mod_lane << 24);
    for (int i = 0; L && i < NSTEP; i += 4)
        h = hash_u(h, (uint32_t)L->val[i] | (uint32_t)L->val[i + 1] << 8 | (uint32_t)L->val[i + 2] << 16 |
                          (uint32_t)L->val[i + 3] << 24);
    if (!begin_main(h))
        return;
    if (mod_none()) {                                /* the scene plays none: what to do */
        text_c(120, LY(34, 16), LY(&FONT_B, &LFONT_S), LARGE ? "No MOD pattern here" : "No MOD pattern in this scene", K_TEXT);
        text_c(120, LY(58, 44), LY(&FONT_S, F_CAP), "Turn KNOB 1 to pick one", K_DIM);
        text_c(120, LY(74, 60), LY(&FONT_S, F_CAP), "for this scene", K_DIM);
        main_blit();
        return;
    }
    if (!L) {
        num_s((uint32_t)pt + 1u, s_cat(t, "No lanes on P"));
        text_c(120, LY(34, 16), &FONT_B, t, K_TEXT);
        text_c(120, LY(58, 44), LY(&FONT_S, F_CAP), ui.mod_learn ? "Turn the knob it is to move" : "Press SEL to add one", K_DIM);
        if (!ui.mod_learn)
            text_c(120, LY(74, 60), LY(&FONT_S, F_CAP), "or LEARN it from a knob", K_DIM);
        main_blit();
        return;
    }
    const uint16_t ink = tg < NTRK ? trk_ink(tg) : K_TEXT;   /* (a track's colour; the master, FX I and II white) */
    {                                                /* its name, which lane of how many; glide or step, the length */
        char *e;
        mod_tgt_name(tg, L->param, t);
        cv_text(gx, 0, LY(&FONT_S, &LFONT_S), t, ink);
        e = num_s((uint32_t)ui.mod_lane % NMODL + 1u, t);
        e = s_cat(s_cat(s_cat(num_s((uint32_t)n, s_cat(e, "/")), " "), LANE_SHAPE_NAME[lane_shape(L)]), ", ");
        mod_len_text(len * six, e);
        text_r(gx + gw, LY(2, 0), F_CAP, t, K_DIM);
    }
    mod_grid(len, six, gx, gw, gy, gh);
    if (len > 16) {                                  /* the keys' 16 steps: a bar under the curve */
        int b0 = ui.bar * 16 < len ? ui.bar * 16 : 0, b1 = b0 + 16 < len ? b0 + 16 : len;
        cv_rect(gx + gw * b0 / len, gy + gh + 5, gw * b1 / len - gw * b0 / len, 2, K_DIM);
    }
    mod_curve(L, len, v0, cv);
    {
        const int lo = mod_lo(tg, L->param), hi = mod_hi(tg, L->param), span = hi > lo ? hi - lo : 1;
        float px = -1.0f, py = 0.0f;
        for (int s = 0; s < len; s++) {
            const float x = (float)gx + ((float)s + 0.5f) * (float)gw / (float)len;
            const float y = (float)(gy + gh) - (float)(clampi(cv[s], lo, hi) - lo) * (float)gh / (float)span;
            if (cv[s] < 0) {
                px = -1.0f;
                continue;
            }
            if (px >= 0.0f) {
                if (lane_glide(L)) {                 /* its shape: a point every 2 pixels */
                    float qx = px, qy = py;
                    for (float sx = px + 2.0f; qx < x; sx += 2.0f) {
                        const float ex = sx < x ? sx : x, p = (ex - (float)gx) * (float)len / (float)gw - 0.5f;
                        const float ey = ex < x ? (float)(gy + gh) - (fm_clampf(mod_curve_at(L, len, v0, p), (float)lo,
                                                                                (float)hi) - (float)lo) * (float)gh /
                                                                       (float)span
                                                : y;
                        cv_linef(qx, qy, ex, ey, ink, 1.6f);
                        qx = ex;
                        qy = ey;
                    }
                } else {                             /* stairs: along, then up or down at the step */
                    cv_linef(px, py, x - (float)gw / (float)len * 0.5f, py, ink, 1.6f);
                    cv_linef(x - (float)gw / (float)len * 0.5f, py, x - (float)gw / (float)len * 0.5f, y, ink, 1.6f);
                    cv_linef(x - (float)gw / (float)len * 0.5f, y, x, y, ink, 1.6f);
                }
            }
            px = x;
            py = y;
            if (s == len - 1)
                cv_linef(x, y, (float)(gx + gw), y, ink, 1.6f);
        }
        for (int s = 0; s < len; s++) {              /* the values: dots; the held step outlined */
            const int32_t x0 = gx + gw * s / len, x1 = gx + gw * (s + 1) / len;
            if (s / 16 == ui.bar && (ui.held ? ui.held >> (s % 16) & 1u : s == mod_tap_step(l)))
                cv_frame(x0, gy - 3, x1 - x0 > 3 ? x1 - x0 : 3, gh + 6, 1, K_SEL, K_BG);
            if (L->val[s] != LOCK_NONE)
                cv_dotf((float)gx + ((float)s + 0.5f) * (float)gw / (float)len,
                        (float)(gy + gh) - (float)(clampi(L->val[s], lo, hi) - lo) * (float)gh / (float)span,
                        len > 32 ? 1.6f : 2.4f, K_TEXT);
        }
    }
    if (pos >= 0 && pos < len)                       /* the playhead */
        cv_rect(gx + gw * pos / len + gw / len / 2, gy - 3, 2, gh + 6, K_TEXT);
    {                                                /* the pattern: its steps, a step's sixteenths; its bars */
        char *e = num_s((uint32_t)len, t);
        e = s_cat(e, len == 1 ? " step, " : " steps, ");
        if (mp->scale == TS_1 || mp->scale == TS_2 || mp->scale == TS_4 || mp->scale == TS_16)   /* "a beat each" */
            s_cat(e, mp->scale == TS_1 ? "a sixteenth each" : mp->scale == TS_2 ? "an eighth each"
                     : mp->scale == TS_4 ? "a beat each" : "a bar each");
        else
            s_cat(s_cat(e, TS_NAME[mp->scale % NTS]), " sixteenths each");
        cv_text(gx, fy, F_CAP, t, K_DIM);
        for (int b = 0; b < (len + 15) / 16; b++)
            cv_round(gx + gw - 4 * 12 + b * 12, fy + 3, 9, 7, 2,
                     b == ui.bar ? K_TEXT : pos >= 0 && pos / 16 == b ? K_NOW : K_LINE);
    }
    main_blit();
}
static void draw_main(int trk);
static void draw_page(void)
{
    if (ui.card) {
        draw_card();
        return;
    }
    if (lst.open) {
        draw_list();
        return;
    }
    main_r0 = 0;                                     /* the first strip decides; the rest, if it drew */
    main_drawn = 0;
    draw_main(sel_trk());
    for (main_r0 = CV_ROWS; main_drawn && main_r0 < MAIN_H; main_r0 += CV_ROWS)
        draw_main(sel_trk());
    main_r0 = 0;
}
static void draw_main(int trk)
{
    switch (ui.page) {
    case P_ARRANGE: draw_arrange(); break;
    case P_SONG: draw_song(); break;
    case P_STEPS:
        if (ui.chordmode && is_seqt(trk) && !is_drum(trk))
            if (ui.chord_pool)
                draw_step_pool();
            else
                draw_step_chord();
        else
            draw_steps();
        break;
    case P_BAR: draw_bar(); break;
    case P_CHARACTER: case P_INSERT: case P_FX1: case P_FX2: draw_slot(ui.page); break;
    case P_SENDS: draw_sends(); break;
    case P_ENGINE: case P_PLAY: case P_PLAY2: case P_SUB: case P_OSC2: case P_KMOD: case P_ENV: case P_ENV2: case P_KITF:
    case P_FILTER: case P_LFO: case P_LFO2: case P_PLAYERS: case P_PLAYERS2: case P_PLAYERS3:
        if (trk == AUX_BRAIN)
            draw_brain();
        else if (trk == AUX_PUNCH)
            draw_punch();
        else if (trk == AUX_WEAVE)
            draw_weave_play();
        else if (is_aux(trk))
            draw_slot(trk == AUX_FX1 ? P_FX1 : P_FX2);
        else if (ui.page == P_ENGINE || ui.page == P_OSC2 || (is_drum(trk) && (ui.page == P_PLAY || ui.page == P_SUB)))
            draw_engine();
        else if (ui.page == P_KMOD && is_rayon(trk))
            draw_rayon_ops();
        else if (ui.page == P_KMOD && is_tulle(trk))
            draw_tulle_grain();
        else if (ui.page == P_KMOD && is_tartan(trk))
            draw_engine();                       /* (its morphs: the chord's levels and places, the Engine page's) */
        else if (ui.page == P_KMOD)
            draw_knit_mod();
        else if (ui.page == P_PLAY)
            draw_play();
        else if (ui.page == P_PLAY2 && is_drum(trk))
            draw_sends();
        else if (ui.page == P_PLAY2 && is_midi(trk))
            draw_play2();
        else if (ui.page == P_PLAY2)
            draw_volume();
        else if (ui.page == P_SUB)
            draw_sub();
        else if (ui.page == P_ENV && is_drum(trk))
            draw_key_env();
        else if (ui.page == P_ENV || ui.page == P_ENV2)
            draw_env(ui.page == P_ENV2);
        else if (ui.page == P_FILTER || ui.page == P_KITF)
            draw_filter();
        else if (ui.page == P_LFO || ui.page == P_LFO2)
            draw_lfo();
        else
            draw_players();
        break;
    case P_MIX: draw_mix(); break;
    case P_THROWS: draw_punch(); break;
    case P_WEAVE: draw_weave(); break;
    case P_MOD: draw_mod(); break;
    case P_MASTER: case P_MASTER3: draw_master(); break;
    case P_MFILT: draw_mfilt(); break;
    case P_MASTER2: draw_slot(P_MASTER2); break;
    case P_GLFO: draw_glfo(); break;
    case P_SETUP: draw_setup(); break;
    default: draw_status(); break;
    }
}
