/* SPDX-License-Identifier: GPL-3.0-only */
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
/* the themes (Setup: THEME; the project keeps it in rsv[1]); the first is the default */
static const theme_t THEMES[] = {
    {"tape deck", RGB(24, 18, 14), RGB(44, 34, 26), RGB(70, 56, 42), RGB(74, 60, 46), RGB(120, 100, 80),
     RGB(190, 170, 140), RGB(244, 230, 200), RGB(150, 130, 104), RGB(230, 70, 40), RGB(240, 220, 180),
     {RGB(230, 180, 50), RGB(214, 96, 48), RGB(160, 170, 70), RGB(70, 170, 160), RGB(110, 150, 200), RGB(180, 110, 150),
      RGB(210, 170, 120), RGB(150, 190, 150)}, 6, 9},
    {"blueprint", RGB(16, 48, 104), RGB(24, 62, 128), RGB(40, 84, 156), RGB(60, 100, 170), RGB(110, 140, 200),
     RGB(170, 195, 235), RGB(240, 246, 255), RGB(130, 160, 215), RGB(255, 110, 90), RGB(255, 255, 255),
     {RGB(255, 150, 190), RGB(120, 240, 230), RGB(255, 200, 80), RGB(180, 240, 120), RGB(255, 170, 110),
      RGB(210, 180, 255), RGB(240, 120, 120), RGB(200, 230, 200)}, 7, 11},
};
#define NTHEMES (sizeof THEMES / sizeof THEMES[0])
static const theme_t *theme = &THEMES[0];
#define THEME_OF_PROJ() (&THEMES[proj.rsv[1] % NTHEMES])
#define AUTOSAVE_OFF 4u                              /* proj.rsv[2]: LOOM-8 saves only when SAVE is pressed */
#define LOOP_FREE 8u                                  /* proj.rsv[2]: a loop throw let go drifts (seq.loop_free) */
#define SCENES_AT_END 2u                              /* proj.rsv[2]: a scene's key waits for T1's pattern's end (bit 0: REC's mode) */
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

/* the pages (the spec's Buttons table): a button opens its first page; SELECT turns them. EDIT: the engine
 * (a drum track: the shown key's knobs) and Play (a drum track: the key's model, random, pan, volume); ENV: the
 * envelope, the filter, the filter envelope (a drum track: the shown key's envelope and filter, then the kit's
 * envelope and the kit's bus filter: four pages); FX: the instrument's character stage (a kit's on the whole
 * kit, after its bus filter), its sends and the two effects; ARP three with the arp (its page 2, then hold). EDIT on a synth
 * track: Engine, Play, Play 2 (the octave), then Sub where the engine has a second page of knobs (TP_X1..X4: knit's
 * oscillator 2, level, sub, noise; rayon's fall); a kit or an FX track Engine and Play; the conductor Engine, Play (routing), Sub (Follow) */
enum { P_ARRANGE, P_SONG, P_STEPS, P_BAR, P_ENGINE, P_PLAY, P_PLAY2, P_SUB, P_ENV, P_FILTER, P_ENV2, P_KITF, P_CHARACTER, P_SENDS,
       P_FX1, P_FX2, P_THROWS, P_LFO, P_LFO2, P_PLAYERS, P_PLAYERS2, P_PLAYERS3, P_WEAVE, P_MIX, P_MASTER, P_MASTER2, P_MASTER3, P_GLFO, P_SETUP,
       P_STATUS, NPAGES };
static const char *const PAGE_NAME[NPAGES] = {"Arrange", "Song", "Steps", "Bar", "Engine", "Play", "Play 2", "Sub", "Envelope",
                                              "Filter", "Filter env", "Kit filter", "Character", "Sends", "FX I",
                                              "FX II", "Throws", "LFO", "LFO 2", "Players", "Players 2", "Hold", "Weave", "Mix",
                                              "Master EQ", "Master color", "Master out", "LFO", "Setup", "Debug"};
static char *s_cat(char *b, const char *s);
static char *num_s(uint32_t v, char *b);
static const char *const GLFO_SHAPE[NGS + 1] = {"off", "sine", "triangle", "saw up", "saw down", "square", "random"};
static const char *const GLFO_DEST[NGD] = {"cutoff", "reso", "pitch", "pan", "volume", "FX I", "FX II", "character"};
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
static int mst_base(int p) { return p == P_MASTER ? MS_EQ_LO : p == P_MASTER2 ? MS_CTYPE : MS_PERC; }   /* its first knob */
static const struct { uint8_t btn, first, n; } PAGE_BTN[] = {
    {B_HOME, P_ARRANGE, 2}, {B_SEQ, P_STEPS, 2}, {B_EDIT, P_ENGINE, 4}, {B_ENV, P_ENV, 4}, {B_FX, P_CHARACTER, 5},
    {B_LFO, P_LFO, 2}, {B_ARP, P_PLAYERS, 4}, {B_GLO, P_MIX, 7}};
#define NPAGE_BTN (sizeof PAGE_BTN / sizeof PAGE_BTN[0])
static int page_group(int p)
{
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
                                           "skip trick", "skip trig"};
static const char *const COMP_GLYPH[NSC] = {"", "P", "H", "M", "V", "^", "v", "?", "~", "B", "T", "J", "L", "C", "S"};
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
static const char *const METRO_NAME[3] = {"off", "rec", "on"};
static const char *const ONOFF[2] = {"off", "on"};
static const char *const LINK_NAME[9] = {"off", "T1", "T2", "T3", "T4", "T5", "T6", "T7", "T8"};
static const char *const FOLLOW_NAME[3] = {"no", "diatonic", "transpose"};
static const char *const CORE2_NAME[4] = {"one core", "two cores", "lost", "barred"};
static const char *const USBA_NAME[2] = {"follows knob", "full"};
static const char *const BRAIN_KNOB[3] = {"key", "scale", "link"};

/* the keys: 0 = F3; the white and the black ones in order */
static const uint8_t WHITE_K[16] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};
static const uint8_t BLACK_K[11] = {1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25};
#define KEY_BASE 53                                  /* F3 at octave 0 */
enum { KM_NONE, KM_PLAY, KM_STEP, KM_SCENE, KM_MUTE, KM_PUNCH, KM_BRAIN, KM_SOUND,
       KM_TRACK, KM_SAVE, KM_LENGTH, KM_SOLO, KM_SCALE, KM_ROUTE, KM_STEPSOUND, KM_IDLE, KM_WEAVE, KM_BARK };   /* KM_IDLE: does nothing */   /* what a held key does */

/* the input's and the screen's state */
static struct {
    uint32_t btn, keys, btn_used, btn_t[NB];
    uint32_t enc_t[NE];
    uint32_t act_t;
    char msg[2][24];
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
    uint8_t trickpg;                                 /* a step held + ARP: its tricks on the knobs */
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
    uint16_t solo;                                   /* the black keys held on Mix: the tracks soloed (bit t) */
    uint16_t solo_mute;                              /* the mutes before the solo */
    uint16_t preset_i, pick_preset;               /* (more than 255 presets and user slots) */
    uint8_t pick_engine, pick_slot, pick_fx, pick_weave, pick_save;   /* (pick_save: SAVE TO's slot) */
    uint8_t name_slot;
    uint8_t playing;
    uint8_t scene_seen;
    uint8_t scene_pg;                                /* Arrange, Song: the white keys are scenes 1-16 (0) or 17-32 (1) */
    uint32_t sig[6];
} ui;

/* SEL: the screen as a list: the page's knobs, then its actions */
enum { LK_KNOB, LK_MORE, LK_ACTION, LK_PICK };
enum { A_CLEAR_NOTES, A_CLEAR_LOCKS, A_CLEAR_BOTH, A_COPY_PAT, A_PASTE_PAT, A_ROT_FWD, A_ROT_BACK, A_DUP_BAR,
       A_ENGINE, A_PRESET, A_SCRAMBLE, A_SAVE_PRESET, A_INIT, A_FOLLOW, A_INSERT_SLOT, A_DELETE_SLOT, A_CLEAR_SONG,
       A_PLAY_SONG, A_LOAD_PROJ, A_NEW_PROJ, A_USB_AUDIO, A_CLEAR_SCENE, A_PANIC, A_EFFECT, A_REMOVE_NOTE,
       A_COUNT_IN, A_PLAYMODE, A_FTYPE, A_FX_HP, A_FX_LP, A_FX_SEND, A_WEAVE, A_THEME, A_PUMP_BY, A_PUMP_REL,
       A_PUMP_ACC, A_LEVEL, A_SCENE_AT, A_SAVE_TO, A_AUTOSAVE, A_RANDOM_SOUNDS, A_MIDI_CH, A_LOOP_FREE, A_COPY_BAR, A_COPY_STEP, A_PASTE_BAR,
       A_PASTE_STEP, A_PASTE_LOCKS, A_DOUBLE, A_CLEAR_STEP_LOCKS, NACTIONS };
static const char *const ACTION_NAME[NACTIONS] = {
    "CLEAR NOTES", "CLEAR LOCKS", "CLEAR NOTES AND LOCKS", "COPY PATTERN", "PASTE PATTERN", "ROTATE FORWARD",
    "ROTATE BACK", "DUPLICATE BAR", "ENGINE", "PRESET", "SCRAMBLE", "SAVE AS PRESET", "RESET SOUND", "FOLLOWS CONDUCTOR",
    "INSERT SCENE", "DELETE SLOT", "CLEAR SONG", "PLAY SONG", "LOAD PROJECT", "NEW PROJECT", "USB AUDIO", "CLEAR SCENE",
    "ALL NOTES OFF", "EFFECT", "REMOVE", "COUNT-IN", "PLAY MODE", "FILTER TYPE", "HIGH-PASS", "LOW-PASS",
    "TO FX II", "KEEP AS PATTERN", "THEME", "PUMP BY", "RELEASE", "ACCENT", "LEVEL", "SCENES SWITCH", "SAVE TO", "AUTOSAVE",
    "RANDOMIZE PRESETS", "MIDI CHANNEL", "LOOP THROWS", "COPY BAR", "COPY STEP", "PASTE BAR", "PASTE STEP",
    "PASTE LOCKS ONLY", "DOUBLE PATTERN", "CLEAR LOCKS"};
/* the weave's ways (pattern.h WV_*): what this pattern keeps of itself */
static const char *const WEAVE_NAME[NWV] = {"bars", "steps", "rhythm", "pitch"};
#define LIST_ROWS 36                  /* (GLO's: its seven pages' knobs and its actions) */
#define LIST_SHOWN 10
static struct {
    uint8_t open, n, sel, top;
    struct { const char *name; char val[24]; uint8_t kind, id; } row[LIST_ROWS];
} lst;

/* the undo: one stage, whatever was changed last (HOME + REC swaps it back, HOME + PLAY swaps again: redo): a
 * pattern with its locks, a track's sound (or T2-T8 at once), the master, or the arrangement (song, scenes, weave) */
enum { U_PAT, U_SOUND, U_MASTER, U_ARR };
#define UNDO_LANES 12
static struct {
    uint8_t valid, redo, kind, trk, n, pt, nl, rsv;
    uint32_t t;
    union {
        struct { tpat_t pat; lane_t lane[UNDO_LANES]; } p;
        trk_t s[NTRK - 1];
        uint8_t master[NMS];
        struct { song_t song; scene_t scene[NSCENE]; wvpat_t weave[NPATT]; } a;
    } u;
} undo PLAT_POOL;
enum { CK_PAT, CK_BAR, CK_STEP };
static uint8_t clip_ok, clip_trk, clip_pat, clip_kind, clip_from;   /* COPY PATTERN / BAR / STEP: which (CK_*: the
                                                 * bar's or the step's first step), read at PASTE (no copy kept: the
                                                 * pool is short) */

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
        y += 17;
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
#define HDR_H 24
#define MAIN_Y 24
#define MAIN_H 120
#define TAG_Y 144
#define TAG_H 14                                     /* the small font's descenders fit */
#define KNB_Y 158
#define KNB_H 50
#define ENC_Y 208
#define FOOT_Y 224
enum { AL_INFO, AL_DONE, AL_UNDO, AL_REDO, AL_ASK, AL_NO };

static int sel_trk(void) { return proj.trk_sel < NALLTRK ? proj.trk_sel : 0; }
static int is_aux(int t) { return t >= NTRK; }
static int is_seqt(int t) { return t < NSEQT; }
static int steps_page(int p) { return p == P_STEPS || p == P_BAR; }   /* SEQ's pages: both write steps */
static int is_drum(int t) { return t < NTRK && proj.trk[t].par[TP_ENGINE] == EG_DRUMLINE; }
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
static int recording(void) { return seq.rec || seq.rec_wait || ui.rec_armed; }
static int step_shown(int i) { return ui.bar * 16 + i; }
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
#define CTYPE_NAME(par) (tp_info_par(TP_CTYPE, par)->names[(par)[TP_CTYPE] % NCT])   /* the character's type */
static int inst_trk(void) { return is_aux(sel_trk()) ? ui.last_inst % NTRK : sel_trk(); }   /* the keys' instrument */
static int fx_page(int p) { return p == P_FX1 || p == P_FX2; }
/* the track a page's knobs edit: the FX pages their effect track, Character and Sends the instrument */
static int page_trk(int p)
{
    return p == P_FX1 ? AUX_FX1 : p == P_FX2 ? AUX_FX2 : p == P_SENDS || p == P_CHARACTER ? inst_trk() : sel_trk();
}
static int x_page(int t)                             /* the engine has a second page of knobs (TP_X1..X4) */
{
    int e = t < NTRK ? proj.trk[t].par[TP_ENGINE] : -1;
    return e == EG_RAYON || e == EG_KNIT;
}
static int group_n(int g)
{
    int trk = sel_trk(), b = PAGE_BTN[g].btn;
    if (b == B_SEQ && trk == AUX_WEAVE)
        return 1;                                    /* (WEAVE's steps: no Bar page) */
    if (b == B_EDIT)                                 /* (grp_page: the conductor's Sub third; a kit, an FX track: two) */
        return trk == AUX_PUNCH || trk == AUX_WEAVE ? 1 : trk == AUX_BRAIN ? 3 : is_aux(trk) ? 2
             : is_drum(trk) ? 3 : x_page(trk) ? 4 : 3;      /* (a kit: Engine, Key, Sends: the key's own) */
    if (b == B_ARP)                                  /* (grp_page: Players, Players 2, Hold the arp's, Weave) */
        return is_aux(trk) ? 1 : is_drum(trk) || pl_type(trk) == PL_OFF ? 2 : pl_type(trk) == PL_ARP ? 4 : 3;
    if (b == B_ENV)
        return is_drum(trk) ? 4 : 3;                 /* a kit: the key's envelope, filter; the kit's envelope, filter */
    if (b == B_GLO)
        return DEBUG_BUILD ? 7 : 6;                  /* Debug last, in a debug build only */
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
    if (PAGE_BTN[g].btn == B_EDIT && sel_trk() == AUX_BRAIN)   /* Engine, Play (routing), Sub (Follow) */
        return i == 2 ? P_SUB : P_ENGINE + i;
    if (PAGE_BTN[g].btn == B_ARP) {
        static const uint8_t ARPP[4] = {P_PLAYERS, P_PLAYERS2, P_PLAYERS3, P_WEAVE};
        static const uint8_t OTHER[3] = {P_PLAYERS, P_PLAYERS2, P_WEAVE};
        int trk = sel_trk();
        if (is_aux(trk))
            return P_WEAVE;
        if (is_drum(trk) || pl_type(trk) == PL_OFF)
            return i & 1 ? P_WEAVE : P_PLAYERS;      /* (a kit, or no player: its page shows it off, then Weave) */
        return pl_type(trk) == PL_ARP ? ARPP[i & 3] : OTHER[(i % 3 + 3) % 3];
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
    static const int8_t M[][4] = {
        {TP_E1, TP_E2, TP_E3, TP_E4}, {TP_VMODE, TP_PORTA, TP_BEND, TP_VOLUME}, {TP_OCT, -1, -1, -1},
        {TP_X1, TP_X2, TP_X3, TP_X4},
        {TP_A, TP_D, TP_S, TP_R},
        {TP_CUT, TP_RES, TP_FENV, TP_KTRK}, {TP_FA, TP_FD, TP_FS, TP_FR}, {TP_CUT, TP_RES, TP_FENV, TP_KTRK},
        {TP_CTYPE, TP_CAMT, TP_CTONE, TP_CMIX},
        {TP_SEND1, TP_SEND2, TP_PAN, TP_LEVEL},
        {TP_E1, TP_E2, TP_E3, TP_E4}, {TP_E1, TP_E2, TP_E3, TP_E4}, {-1, -1, -1, -1},
        {TP_LTYPE, TP_LSPEED, TP_LAMT, TP_LDEST},
        {TP_LX, TP_LX2, -1, -1}, {TP_PLAYER, TP_P1, TP_P2, TP_P3}, {TP_AP1, TP_AP2, TP_AP3, TP_AP4},
        {TP_P4, -1, -1, -1}, {-1, -1, -1, -1}, {TP_SEND1, TP_SEND2, TP_PAN, TP_LEVEL}};
    if (p < P_ENGINE || p > P_MIX || p == P_THROWS || p == P_WEAVE || k < 0 || k > 3)
        return -1;
    if (fx_page(p))                                  /* (the effect tracks' own knobs, whatever trk is) */
        return M[p - P_ENGINE][k];
    if (p == P_SENDS)                                /* (a kit's: the key's own, below) */
        return is_aux(trk) ? -1 : is_drum(trk) && k < 2 ? TP_DRUM(ui.dsound % DL_NSOUND, DP_SEND1 + k)
                                                        : M[p - P_ENGINE][k];
    if (p == P_CHARACTER)                            /* (a kit's: the whole kit's, after its bus filter) */
        return is_aux(trk) ? -1 : M[p - P_ENGINE][k];
    if (is_aux(trk)) {
        if (p == P_MIX)                              /* (the conductor and the throws: no sound, no fader) */
            return k == 3 && is_fx(trk) ? TP_LEVEL : -1;
        if (trk == AUX_BRAIN && (p == P_ENGINE || p == P_SUB))   /* key, scale, link; the Follow page: its track */
            return p == P_SUB ? (k ? -1 : TP_E1) : k < 3 ? TP_E2 + k : -1;
        if (p == P_ENGINE)
            return (trk == AUX_PUNCH || trk == AUX_WEAVE) && k ? -1 : TP_E1 + k;
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
    if (is_drum(trk) && (p == P_PLAYERS || p == P_PLAYERS2 || p == P_PLAYERS3))
        return -1;                                   /* a kit: no player (its page shows it off) */
    if (p == P_PLAYERS2 && pl_type(trk) != PL_ARP)
        return k == 0 ? TP_P4 : -1;
    if (p == P_PLAYERS3 && pl_type(trk) != PL_ARP)
        return -1;
    return M[p - P_ENGINE][k];
}

/* a pen for smooth curves: pen_to draws an anti-aliased line from the last point (pen_up lifts it) */
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
    int32_t bx = 6, by = 4;
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
        text_c(bx + 8, cap_y(by, 16, &FONT_B), &FONT_B, kind == AL_ASK ? "?" : kind == AL_NO ? "!" : "i", mark);
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
static void meters_take(void)
{
    for (int t = 0; t < NALLTRK; t++) {
        ui_peak[t] = eng_peak[t];
        eng_peak[t] = 0;
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
static void draw_header(void)
{
    char t[40];
    int mh = 0;
    int msg = plat_ms() < ui.msg_until, rec = recording(), ext = seq.ext, ask = ui.ask, cnt = seq.count_in;
    uint32_t h = hash_u(2166136261u, ui.page | (uint32_t)msg << 8 | (uint32_t)ui.dirty << 9 | (uint32_t)rec << 11 |
                                         (uint32_t)ext << 12 | (uint32_t)ask << 14 | (uint32_t)proj.tempo_x10 << 16);
    h = hash_u(h, (uint32_t)cnt | (uint32_t)playing() << 8 | (uint32_t)is_drum(sel_trk()) << 9 |
                      (uint32_t)page_trk(ui.page) << 10 | (uint32_t)trk_ink(page_trk(ui.page)) << 16);
    h = hash_u(h, (uint32_t)cur_scene());
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
    if (ask) {
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
        int32_t x;
        alert_box(no ? AL_NO : ui.msg_kind);
        x = cv_text(AL_X, 4, &FONT_B, ui.msg[0], K_TEXT);
        {   /* the second part smaller when it does not fit (cut only past the smallest) */
            int32_t room = 232 - (x + 8);
            const felucca_font_t *f = text_w(&FONT_B, ui.msg[1]) <= room ? &FONT_B
                                    : text_w(&FONT_S, ui.msg[1]) <= room ? &FONT_S : &FONT_XS;
            text_fit(x + 8, f == &FONT_XS ? 6 : 4, f, ui.msg[1], room, K_DIM);
        }
    } else {
        int32_t r = 232, x = 8;
        const char *bn = BTN_LABEL[PAGE_BTN[page_group(ui.page)].btn];
        const char *pn = page_name_of(ui.page, sel_trk());
        int32_t w = text_w(&FONT_XS, bn) + 6;
        if (((ui.page >= P_STEPS && ui.page <= P_PLAYERS3) || ui.page == P_WEAVE) && ui.page != P_THROWS) {   /* a track's page */
            int pt = page_trk(ui.page);
            const char *tn = TRACK_NAME[pt];
            int32_t tw = text_w(&FONT_XS, tn) + 10;
            cv_round(x, 4, 5, HM_H, 2, K_PANEL);     /* the meter's well, then its level from the foot */
            if (mh)
                cv_round(x, 4 + HM_H - mh, 5, mh, 2, trk_ink(pt));
            x += 9;
            cv_round(x, 4, tw, 17, 8, trk_ink(pt));
            cv_text(x + 5, cap_y(4, 17, &FONT_XS) - 1, &FONT_XS, tn, K_BG);
            x += tw + 5;
        }
        cv_round(x, 4, w, 17, 3, K_DIM);
        cv_round(x + 1, 5, w - 2, 15, 2, K_BG);
        cv_text(x + 3, cap_y(4, 17, &FONT_XS) - 1, &FONT_XS, bn, K_DIM);
        x += w + 6;
        x = cv_text(x, 4, &FONT_B, pn, K_TEXT);   /* (pn: page_name_of) */
        if (ext)
            s_cat(t, "ext");
        else
            tempo_text(t);
        r -= text_w(&FONT_S, t);
        cv_text(r, 4, &FONT_S, t, K_TEXT);
        r -= 6;
        num_s((uint32_t)cur_scene() + 1u, s_cat(t, "S"));   /* the scene playing, on every page */
        if (r - text_w(&FONT_S, t) >= x + 6) {
            r -= text_w(&FONT_S, t);
            cv_text(r, 4, &FONT_S, t, K_DIM);
            r -= 6;
        }
        if (rec) {
            r -= 10;
            cv_round(r, 8, 9, 9, 4, K_REC);
            r -= 6;
        }
        if (ui.dirty && r - text_w(&FONT_XS, "unsaved") >= x + 6)   /* (where it fits: a long name before it) */
            cv_text(r - text_w(&FONT_XS, "unsaved"), 5, &FONT_XS, "unsaved", K_DIM);
    }
    cv_blit(0, 0);
}

/* ----------------------------------------------------------- knobs --- */
typedef struct {
    const char *name;
    char val[24];
    int32_t num, max;
    int32_t live;                                    /* (max > 0) a lock or a motion's value now, as num; -1 none: a dot */
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
/* the master color's knob m (amount, tone, mix): printed as a voice's character stage of the same type prints it
 * (tp_text_par: the tape's tone and the sampler's rate and filter in Hz, the rest in %) */
static void kmc(knob_t *k, int m)
{
    static const uint8_t CT_OF[NMC] = {CT_OFF, CT_TAPE, CT_SAMPLER};
    uint8_t par[NTP_SOUND] = {0};
    char num[12], unit[6], val[24], *e;
    int p = TP_CAMT + (m - MS_CAMT), v = proj.master[m];
    par[TP_CTYPE] = CT_OF[proj.master[MS_CTYPE] % NMC];
    tp_text_par(p, v, par, num, unit);
    if (p == TP_CTONE && par[TP_CTYPE] == CT_OFF)      /* (off: the tone a plain number) */
        num_i(v, num), unit[0] = 0;
    e = s_cat(val, num);
    if (unit[0])
        s_cat(unit[0] != '%' ? s_cat(e, " ") : e, unit);
    kv(k, MS_NAME[m], val, v, 127);
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
/* a track parameter's knob: its name and value as synth.c prints them (the value v, or the track's) */
static void ktp_val(knob_t *k, int trk, int p, int v)
{
    const uint8_t *par = proj.trk[trk].par;
    const tp_info_t *in = tp_info_par(p, par);
    char num[12], unit[6], val[24], *e;
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
    }
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
    cond_text(st->cond, t);
    kv(&k[2], "CHANCE", t, 0, 0);
    if (is_drum(sel_trk())) {                         /* the step's note: semitones from the keys' pitch */
        int semi = step_tune(pats.lane, sel_trk(), cur_pat(sel_trk()), step_shown(ui.held_first));
        char *e = t;
        if (semi > 0)
            e = s_cat(e, "+");
        s_cat(num_i(semi, e), " st");
        kv(&k[3], "PITCH", t, semi + TUNE_MAX, 2 * TUNE_MAX);
    } else {                                          /* the step's (first) note */
        int n = 0;
        for (int i = 0; i < SN && !n; i++)
            n = st->note[i];
        if (n)
            note_name(n, t);
        else
            s_cat(t, "-");
        kv(&k[3], "PITCH", t, n, 127);
    }
}
/* HOME + a step held: the strip shows the step's components and their values */
static void comp_knobs(knob_t *k)
{
    const step_t *st = &tpat(sel_trk())->st[step_shown(ui.held_first)];
    char t[16];
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
/* an FX track's TP_ENGINE byte: the pick (fx.h), and how the screen names it: "chorus", or "reverb hall" */
static int fx_pick_of(int trk) { return proj.trk[trk].par[TP_ENGINE] % NFXPICK; }
static const char *fx_label(int pick)
{
    static char t[24];
    if (fx_pick_type(pick) != FXT_REVERB)
        return fx_pick_name(pick);
    s_cat(s_cat(s_cat(t, FXT_NAME[FXT_REVERB]), " "), fx_pick_name(pick));
    return t;
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
    } else {
        int pick = par[TP_ENGINE] % NFXPICK;
        for (int i = 0; i < 4; i++)
            kn(&k[i], short_name(fx_knob_name(pick, i)), par[TP_E1 + i] * 100 / 128, 99);
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
        static const char *const CIN[3] = {"off", "1 bar", "2 bars"};
        static const char *const MODE[2] = {"overdub", "replace"};
        kname(&k[0], "CLICK", METRO_NAME, proj.metronome, 3);
        kname(&k[1], "COUNT", CIN, proj.rsv[0] % 3u, 3);
        if (is_seqt(trk))
            kn(&k[2], "QUANT", tp->quant, 100);
        kname(&k[3], "MODE", MODE, proj.rsv[2] & 1u, 2);
        return;
    }
    if (ui.lockpg) {
        lock_knobs(k);
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
        if (is_seqt(trk)) {
            static char pn[12];                      /* the track's pattern in this scene ("T2 PAT"; cued: "1>2") */
            char c[8], *ce;
            kn(&k[2], "AMOUNT", tp->groove, 0);
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
    case P_FX1: case P_FX2: {                        /* an effect: its four knobs (the type: PRESETS, SEL) */
        int ft = P_FX1 == p ? AUX_FX1 : AUX_FX2, pick = fx_pick_of(ft);
        for (int i = 0; i < 4; i++)
            kn(&k[i], short_name(fx_knob_name(pick, i)), proj.trk[ft].par[TP_E1 + i] * 100 / 128, 99);
        break;
    }
    case P_CHARACTER: {                              /* the instrument's character stage */
        int it = inst_trk();
        for (int i = 0; i < 4; i++)
            ktp(&k[i], it, tp_page_param(p, i, it));
        break;
    }
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
        kv(&k[0], "WITH", f ? TRACK_NAME[f - 1] : "off", f, NSEQT);
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
            ktp(&k[3], trk, TP_LEVEL);
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
    case P_MASTER: case P_MASTER2: case P_MASTER3:
        for (int i = 0; i < 4; i++) {
            int m = mst_base(p) + i;
            if (m == MS_RSV3)
                continue;                            /* (EQ: three knobs) */
            if (m == MS_CTYPE)
                kname(&k[i], MS_NAME[m], MC_NAME, proj.master[m] % NMC, NMC);
            else if (m >= MS_CAMT && m <= MS_CMIX)
                kmc(&k[i], m);
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
        kname(&k[1], "CLOCK", ONOFF, proj.send_clock != 0, 2);
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
}
static int knobs_shown(void)
{
    knob_t k[4];
    page_knobs(k);
    return k[0].name || k[1].name || k[2].name || k[3].name;
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
    }
    h = hash_u(h, (uint32_t)now_touch | (uint32_t)ui.lockpg << 1);
    if (h == ui.sig[2])
        return;
    ui.sig[2] = h;
    cv_begin(240, KNB_H, K_BG);
    if (!(k[0].name || k[1].name || k[2].name || k[3].name)) {
        cv_blit(0, KNB_Y);
        return;
    }
    cv_round(4, 0, 232, KNB_H - 2, 6, K_PANEL);
    for (i = 0; i < 4; i++) {
        int32_t x = (int32_t)i * 60, cx = x + 30, nw;
        int hot = now_touch && ui.touched == (int)i, lock = ui.lockpg && !s_eq(k[i].val, "-");
        uint16_t vc = hot ? K_SEL : lock ? K_REC : K_TEXT;
        if (i)
            cv_rect(x, 10, 1, KNB_H - 22, K_LINE);
        if (!k[i].name)
            continue;
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
            int32_t bw = 44 * k[i].num / k[i].max;
            cv_round(cx - 22, 41, 44, 6, 3, K_LINE);
            if (bw > 0)
                cv_round(cx - 22, 41, bw < 6 ? 6 : bw, 6, 3, hot ? K_SEL : K_NOW);
            if (k[i].live >= 0) {                    /* a lock or a motion holds it now: where, a dot on the bar */
                int32_t lx = cx - 22 + 38 * k[i].live / k[i].max;
                cv_round(lx - 1, 39, 8, 10, 4, K_BG);
                cv_round(lx, 40, 6, 8, 3, K_REC);
            }
        }
    }
    cv_blit(0, KNB_Y);
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
        } else if (ui.held)
            num_s((uint32_t)step_shown(ui.held_first) + 1u, s_cat(t + s_len(t), ">step "));
        break;
    case P_ENGINE: case P_PLAY: case P_PLAY2: case P_SUB: case P_ENV: case P_FILTER: case P_ENV2: case P_KITF: case P_LFO:
    case P_LFO2:
        track_crumb(t);
        if (is_drum(sel_trk()))                      /* the key the knobs edit (the kit's own pages: the whole kit) */
            s_cat(s_cat(t + s_len(t), ">"), ui.page == P_ENV2 || ui.page == P_KITF ? "all keys"
                                             : DL_SOUND_NAME[ui.dsound % DL_NSOUND]);
        break;
    case P_SENDS:
        s_cat(s_cat(t, TRACK_NAME[inst_trk()]), ">sends");
        break;
    case P_CHARACTER:
        e = s_cat(s_cat(t, TRACK_NAME[inst_trk()]), ">character");
        s_cat(s_cat(e, ">"), CTYPE_NAME(proj.trk[inst_trk()].par));
        break;
    case P_FX1: case P_FX2: {                        /* FX II>delay, FX II>reverb>hall */
        int pick = fx_pick_of(ui.page == P_FX1 ? AUX_FX1 : AUX_FX2);
        e = s_cat(s_cat(s_cat(t, TRACK_NAME[ui.page == P_FX1 ? AUX_FX1 : AUX_FX2]), ">"), FXT_NAME[fx_pick_type(pick)]);
        if (fx_pick_type(pick) == FXT_REVERB)
            s_cat(s_cat(e, ">"), fx_pick_name(pick));
        break;
    }
    case P_MASTER: case P_MASTER2: case P_MASTER3: case P_GLFO: case P_SETUP: case P_STATUS:
        s_cat(t, "all tracks");
        break;
    default:
        track_crumb(t);
        break;
    }
}
static void draw_tag(void)
{
    char t[80], *v = t;
    int g = page_group(ui.page), n = group_n(g), d = grp_index(ui.page);
    int32_t r = 232, x, w;
    uint32_t h;
    page_tag(t);
    h = hash_s(hash_u(2166136261u, ui.page | (uint32_t)n << 8), t);
    if (h == ui.sig[3])
        return;
    ui.sig[3] = h;
    cv_begin(240, TAG_H, K_BG);
    if (n > 1) {
        r -= n * 9 - 3;
        for (int i = 0; i < n; i++)
            cv_round(r + i * 9, 3, 6, 6, 3, i == d ? K_TEXT : K_LINE);
        r -= 8;
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
    segs(x, -2, &FONT_XS, v, K_TEXT);
    if (x - 16 >= 24)
        cv_rect(8, 6, x - 16, 1, K_LINE);
    cv_blit(0, TAG_Y);
}

/* the footer: what the keys do here; a list, a question, a card: what does what */
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
        s_cat(t, "let go: nothing changes");
        return;
    }
    if (lst.open && lst.n) {
        int k = lst.row[lst.sel].kind;
        s_cat(t, k == LK_ACTION ? "SELECT move|SEL do it|HOME back"
               : k == LK_PICK ? "ALGO pick|SEL do it|HOME back" : "SELECT move|ALGO set|HOME back");
        return;
    }
    if (!ui.any_btn && ui.page == P_STEPS) {
        s_cat(t, "WHITE steps|PLAY play|hold a button: help");
        return;
    }
    if (home_held()) {
        s_cat(t, "REC undo|PLAY redo|KNOBS fine");
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
        else if (ui.held && is_drum(trk))
            s_cat(t, "BLACK sounds|ARP tricks|EDIT ENV FX LFO lock");
        else if (ui.held)
            s_cat(t, "WHITE length|ARP tricks|EDIT ENV FX LFO lock");
        else if (ui.rec_armed)
            s_cat(t, "KEYS write|OCT+ rest|OCT- back|REC done");
        else if (seq.rec || seq.rec_wait)
            s_cat(t, "KEYS record|REC off");
        else if (ui.btn >> B_SEQ & 1u && ui.page == P_BAR)
            s_cat(t, "BLACK 1-4 jump to a bar|two: loop|twice: loop one");
        else if (ui.btn >> B_SEQ & 1u)
            s_cat(t, is_drum(trk) ? "KEYS pick a sound" : "KEYS the notes a step writes");
        else if (is_drum(trk))
            s_cat(t, ui.page == P_BAR ? "WHITE steps|BLACK sound|SEQ held: bars" : "WHITE steps|BLACK sound|SEQ held: pick a sound");
        else
            s_cat(t, ui.page == P_BAR ? "WHITE steps|BLACK scale|SEQ held: bars" : "WHITE steps|SEQ held: play notes");
        break;
    case P_MIX:
        s_cat(t, "BLACK mute|hold: solo|WHITE play");
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
    case P_MASTER: case P_MASTER2: case P_SETUP: case P_STATUS:
        s_cat(t, "KEYS play");
        break;
    case P_SENDS:
        s_cat(t, "KEYS play");
        break;
    case P_CHARACTER:
        s_cat(t, "PRESET type|KEYS play");
        break;
    case P_FX1: case P_FX2:
        s_cat(t, "PRESET effect|SEL filter|KEYS play");
        break;
    default:
        if (ui.rec_armed)
            s_cat(t, "KEYS write|OCT+ rest|OCT- back|REC done");
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
        else if (is_drum(trk) && ui.page == P_FILTER)
            s_cat(t, "PRESET type|BLACK pick a key");
        else if (is_drum(trk) && (ui.page == P_ENV2 || ui.page == P_KITF))
            s_cat(t, ui.page == P_KITF ? "PRESET on or off|KEYS play" : "KEYS play");
        else if (is_drum(trk) && ui.page <= P_LFO2)
            s_cat(t, "BLACK pick a key|KEYS play");
        else if (ui.page == P_ENGINE)
            s_cat(t, "PRESET next sound|SEL engine");
        else if (ui.page == P_FILTER)
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
        s_cat(t, "SELECT tempo|PRESET scene|KNOBS fine");
        return;
    }
    if (n > 1)                        /* SELECT names what it turns to where that is the point of the page */
        e = s_cat(t, p == P_CHARACTER ? "SELECT sends" : p == P_SENDS ? "SELECT effects" : p == P_ENV ? "SELECT filter"
                     : p == P_LFO ? "SELECT more" : "SELECT page");
    if (p != P_ARRANGE && p != P_SONG && p != P_THROWS && p < P_MASTER)
        e = s_cat(e, *t ? "|ALGO track" : "ALGO track");
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
#ifdef PLAT_HOST
static int msg_too_wide;
#endif
static void draw_footer(void)
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
#define PG(p) (1u << (p))
#define PG_ANY 0xFFFFFFFFu
#define PG_STEPS (PG(P_STEPS) | PG(P_BAR))
#define PG_SOUND (PG(P_ENGINE) | PG(P_PLAY) | PG(P_PLAY2) | PG(P_SUB) | PG(P_ENV) | PG(P_ENV2) | PG(P_KITF) | PG(P_FILTER) | \
                  PG(P_CHARACTER) | PG(P_LFO) | PG(P_LFO2))
static const struct { uint8_t btn; uint32_t pages; const char *title, *l1, *l2; } CARD[] = {
    {B_SEQ, PG(P_BAR), "Bars", "Hold SEQ, black keys 1-4: jump to a bar at the next bar line.",
     "Two held: loop them. One twice: loop it."},
    {B_SEQ, PG_ANY, "Steps", "The bar's 16 steps on the white keys. Hold SEQ and play the notes,",
     "then press steps to write them. A step held + ARP: its tricks."},
    {B_EDIT, PG_STEPS, "Lock", "With a step held, EDIT, ENV, FX or LFO show their knobs.",
     "Turn one to lock it. SELECT: pages. SEL held + knob: clear."},
    {B_EDIT, PG_ANY, "Engine", "The engine's four knobs. PRESETS: its next sound.",
     "Held: the keys play, ALGORITHM picks the engine. SELECT: pages."},
    {B_ENV, PG_ANY, "Envelope", "The amp's envelope, the filter, its envelope.",
     "PRESETS on Filter: its type. A kit: the key's, then the kit's."},
    {B_FX, PG_ANY, "Effects", "Character, sends, FX I, FX II, then Throws: keys fire effects.",
     "PRESETS: the type or effect. SEL: an FX's filter."},
    {B_LFO, PG_ANY, "LFO", "Type, then its first three knobs.", "Page 2 has the type's fourth and fifth."},
    {B_ARP, PG_ANY, "Players", "Arp, strum or hold on the track's keys; last page, Weave.",
     "Strum: a key strums its chord; keys held together set it."},
    {B_ARP, PG_STEPS, "Tricks", "With a step held, ARP: KNOB 1 and 3 pick its two tricks,",
     "KNOB 2 and 4 their values. Let go of the step when done."},
    {B_GLO, PG_ANY, "Mix", "Sends, pan, level. Black: tap mutes, hold solos (more: more).",
     "SELECT: Master EQ, out, Setup. HOME's SEL: projects."},
    {B_HOME, PG_ANY, "Arrange", "Each track's pattern in the scene. White: scenes, black: mutes.",
     "SEL: projects, scene timing. + REC undoes, + PLAY redoes."},
    {B_SEL, PG_ANY, "Lists", "The page's knobs, then its actions. SEL runs the one picked.",
     "SELECT moves, ALGORITHM changes, HOME goes back."},
    {B_SAVE, PG_STEPS, "Save", "Tap to save the project. With a step held, clears its locks.",
     "Hold SAVE and press white key 1 to 3 to save to that slot."},
    {B_SAVE, PG_SOUND, "Save", "Tap to save the project.",
     "Hold SAVE and press a white key: the sound as that user preset."},
    {B_SAVE, PG_ANY, "Save", "Saves now (LOOM-8 saves by itself when stopped and quiet).", "Hold SAVE and press white key 1 to 3 to save to that slot."},
    {B_REC, PG_ANY, "Recording", "Stopped: a tap, then each key writes a step. + PLAY: live.",
     "Playing: hold it and play. Held, the knobs set click, count-in, mode."},
    {B_PLAY, PG(P_SONG), "Play", "Plays the song from the slot picked. PLAY again stops.", "SEL's PLAY SONG: from the top."},
    {B_PLAY, PG_ANY, "Play", "Starts and stops. Twice quickly: from the top.", "HOME + PLAY redoes."},
    {B_OCTDN, PG_STEPS, "Bar", "Shows the bar before. A step held: nudges it earlier.",
     "Step record: the cursor back. Both held 5 s: update mode."},
    {B_OCTUP, PG_STEPS, "Bar", "Shows the next bar. A step held: nudges it later.",
     "Step record: the cursor on. Both held 5 s: update mode."},
    {B_OCTDN, PG_ANY, "Octave", "The keys an octave down.", "Hold both for 5 seconds for update mode."},
    {B_OCTUP, PG_ANY, "Octave", "The keys an octave up.", "Hold both for 5 seconds for update mode."},
};
#define NCARDS (sizeof CARD / sizeof CARD[0])
static int card_find(int btn)
{
    for (uint32_t i = 0; i < NCARDS; i++)
        if (CARD[i].btn == btn && (CARD[i].pages >> ui.page & 1u))
            return (int)i;
    return -1;
}
/* where card i's text ends (the box's top in *top): both lines wrapped at 200 px */
static int32_t card_end(int i, int32_t *top)
{
    int32_t n = wrap_do(0, 0, 200, &FONT_S, CARD[i].l1, 0, 0) / 17 +
                (CARD[i].l2 ? wrap_do(0, 0, 200, &FONT_S, CARD[i].l2, 0, 0) / 17 : 0);
    int32_t bh = 42 + (n - 1) * 17 + (CARD[i].l2 ? 4 : 0), t = 4 + (MAIN_H - 8 - bh) / 2;
    if (t < 10)
        t = 10;
    *top = t;
    return t + 30 + n * 17 + (CARD[i].l2 ? 4 : 0);
}
#if defined(PLAT_HOST) && !defined(PLAT_WEB)
static void cards_audit(void)                  /* every card on every page it is shown: its text fits the box */
{
    for (uint32_t i = 0; i < NCARDS; i++) {
        int32_t top;
        if (card_end((int)i, &top) - 1 > MAIN_H - 4) {
            printf("  FAIL card too long: %s (%s)\n", CARD[i].title, CARD[i].l1);
            msg_too_wide++;
        }
    }
}
#endif
static void draw_card(void)
{
    int i = card_find(ui.card - 1);
    uint32_t h = hash_u(hash_u(2166136261u, 0xCAu), ui.card + ((uint32_t)ui.page << 8));
    int32_t x, y;
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;
    cv_begin(240, MAIN_H, K_BG);
    cv_round(8, 4, 224, MAIN_H - 8, 8, K_PANEL);
    {
        int32_t top = 4 + (MAIN_H - 8 - 20) / 2;
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
    cv_blit(0, MAIN_Y);
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
static void list_midi_ch(int trk)                  /* the track's notes out over USB (Setup's NOTES on) and in */
{
    static char ch_t[4];
    if (proj.trk[trk].par[TP_CHAN])
        num_s(proj.trk[trk].par[TP_CHAN], ch_t);
    else
        s_cat(ch_t, "off");
    list_add(ACTION_NAME[A_MIDI_CH], ch_t, LK_PICK, A_MIDI_CH);
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
static void list_build(void)
{
    int keep = ui.page, g = page_group(keep), f = PAGE_BTN[g].first, n = group_n(g), trk = sel_trk();
    knob_t k[4];
    static char slot_t[24], fol_t[24], nm[NTRK][16], rm[NDRUM][16];
    lst.n = 0;
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
            else
                note_name(st->note[i], e);
            list_add(rm[i], 0, LK_ACTION, A_REMOVE_NOTE);
        }
        list_clip_step();
        return;
    }
    if (PAGE_BTN[g].btn == B_FX)                     /* (FX I and FX II each their own effect: the page's list) */
        f = keep, n = 1;
    for (int gi = 0; gi < n; gi++) {
        int p = PAGE_BTN[g].btn == B_FX ? f : grp_page(g, gi);
        ui.page = (uint8_t)p;
        page_knobs(k);
        for (int i = 0; i < 4; i++) {
            int seen = 0;
            for (int j = 0; j < lst.n; j++)
                seen |= k[i].name && s_eq(lst.row[j].name, k[i].name) && s_eq(lst.row[j].val, k[i].val);
            if (k[i].name && !seen)
                list_add(k[i].name, k[i].val, LK_KNOB, p << 2 | i);
        }
    }
    ui.page = (uint8_t)keep;
    if (keep == P_STEPS && trk == AUX_WEAVE) {     /* WEAVE: its pattern cleared */
        list_add(ACTION_NAME[A_CLEAR_NOTES], 0, LK_ACTION, A_CLEAR_NOTES);
    } else if (steps_page(keep)) {
        if (is_seqt(trk)) {
            list_add(ACTION_NAME[A_CLEAR_NOTES], 0, LK_ACTION, A_CLEAR_NOTES);
            list_add(ACTION_NAME[A_CLEAR_LOCKS], 0, LK_ACTION, A_CLEAR_LOCKS);
            list_add(ACTION_NAME[A_CLEAR_BOTH], 0, LK_ACTION, A_CLEAR_BOTH);
            list_add(ACTION_NAME[A_COPY_PAT], 0, LK_ACTION, A_COPY_PAT);
            list_add(ACTION_NAME[A_COPY_BAR], 0, LK_ACTION, A_COPY_BAR);
            if (clip_ok && clip_kind != CK_STEP) {    /* (a step pastes onto steps held: their list) */
                list_add(ACTION_NAME[clip_kind == CK_BAR ? A_PASTE_BAR : A_PASTE_PAT], 0, LK_ACTION,
                         clip_kind == CK_BAR ? A_PASTE_BAR : A_PASTE_PAT);
                list_add(ACTION_NAME[A_PASTE_LOCKS], 0, LK_ACTION, A_PASTE_LOCKS);
            }
            list_add(ACTION_NAME[A_ROT_FWD], 0, LK_ACTION, A_ROT_FWD);
            list_add(ACTION_NAME[A_ROT_BACK], 0, LK_ACTION, A_ROT_BACK);
            list_add(ACTION_NAME[A_DUP_BAR], 0, LK_ACTION, A_DUP_BAR);
            list_add(ACTION_NAME[A_DOUBLE], 0, LK_ACTION, A_DOUBLE);
        }
    } else if (keep == P_WEAVE) {
        if (wv_from(trk))
            list_add(ACTION_NAME[A_WEAVE], 0, LK_ACTION, A_WEAVE);
    } else if (keep == P_ARRANGE || keep == P_SONG) {   /* HOME's one list: the projects, the song, the scenes */
        static char save_t[24];
        proj_slot_text(ui.pick_slot % NPROJ, slot_t);
        list_add(ACTION_NAME[A_LOAD_PROJ], slot_t, LK_PICK, A_LOAD_PROJ);
        proj_slot_text(ui.pick_save % NPROJ, save_t);
        list_add(ACTION_NAME[A_SAVE_TO], save_t, LK_PICK, A_SAVE_TO);
        list_add(ACTION_NAME[A_NEW_PROJ], 0, LK_ACTION, A_NEW_PROJ);
        list_add(ACTION_NAME[A_RANDOM_SOUNDS], 0, LK_ACTION, A_RANDOM_SOUNDS);
        list_add(ACTION_NAME[A_PLAY_SONG], 0, LK_ACTION, A_PLAY_SONG);
        list_add(ACTION_NAME[A_INSERT_SLOT], 0, LK_ACTION, A_INSERT_SLOT);
        list_add(ACTION_NAME[A_DELETE_SLOT], 0, LK_ACTION, A_DELETE_SLOT);
        list_add(ACTION_NAME[A_CLEAR_SONG], 0, LK_ACTION, A_CLEAR_SONG);
        list_add(ACTION_NAME[A_SCENE_AT], proj.rsv[2] & SCENES_AT_END ? "pattern end" : "next bar", LK_PICK, A_SCENE_AT);
        list_add(ACTION_NAME[A_CLEAR_SCENE], 0, LK_ACTION, A_CLEAR_SCENE);
        list_add(ACTION_NAME[A_PANIC], 0, LK_ACTION, A_PANIC);
    } else if (fx_page(keep)) {                     /* an effect: the type, its filter, the feed into FX II */
        int ft = keep == P_FX1 ? AUX_FX1 : AUX_FX2;
        static char hp[8], lp[8], fd[8];
        num_s(proj.trk[ft].par[TP_FENV], hp);
        num_s(proj.trk[ft].par[TP_CUT], lp);
        list_add(ACTION_NAME[A_EFFECT], fx_label(ui.pick_fx % NFXPICK), LK_PICK, A_EFFECT);
        list_add(ACTION_NAME[A_FX_HP], hp, LK_PICK, A_FX_HP);
        list_add(ACTION_NAME[A_FX_LP], lp, LK_PICK, A_FX_LP);
        if (ft == AUX_FX1) {
            num_s(proj.trk[ft].par[TP_SEND2], fd);
            list_add(ACTION_NAME[A_FX_SEND], fd, LK_PICK, A_FX_SEND);
        }
    } else if (keep == P_SENDS) {
        list_add(ACTION_NAME[A_PANIC], 0, LK_ACTION, A_PANIC);
    } else if (keep == P_CHARACTER) {
        ;                                            /* (its four knobs) */
    } else if ((keep == P_ENV || keep == P_FILTER || keep == P_ENV2 || keep == P_KITF) && !is_aux(trk)) {
        int fq = ftype_param(trk, keep);
        const tp_info_t *fi = tp_info_par(fq, proj.trk[trk].par);
        if (is_drum(trk) && fq == TP_FTYPE)          /* the kit's bus filter: on (dynamic) or off */
            list_add(ACTION_NAME[A_FTYPE], proj.trk[trk].par[TP_FTYPE] == FT_DYNAMIC ? "on" : "off", LK_PICK, A_FTYPE);
        else
            list_add(ACTION_NAME[A_FTYPE], fi->names[proj.trk[trk].par[fq] % (fi->hi + 1u)], LK_PICK, A_FTYPE);
    } else if ((keep == P_ENGINE || keep == P_PLAY) && is_drum(trk)) {   /* the key's play mode, then the sound's */
        list_add(ACTION_NAME[A_PLAYMODE], PM_NAME[proj.trk[trk].par[TP_DRUM(ui.dsound % DL_NSOUND, DP_MODE)] % NPM],
                 LK_PICK, A_PLAYMODE);
        list_add(ACTION_NAME[A_SCRAMBLE], 0, LK_ACTION, A_SCRAMBLE);
        num_s((uint32_t)ui.pick_slot % NUSER_PRESETS + 1u, s_cat(slot_t, "U"));
        list_add(ACTION_NAME[A_SAVE_PRESET], slot_t, LK_PICK, A_SAVE_PRESET);
        list_add(ACTION_NAME[A_ENGINE], EG_NAME[ui.pick_engine % NENGINE], LK_PICK, A_ENGINE);
        list_midi_ch(trk);
    } else if (keep >= P_ENGINE && keep <= P_PLAYERS3 && keep != P_THROWS) {
        if (trk == AUX_BRAIN) {
            for (int t = 0; t < NTRK; t++) {
                s_cat(num_s((uint32_t)t + 1u, s_cat(nm[t], "T")), " FOLLOWS");
                list_add(nm[t], FOLLOW_NAME[proj.trk[t].par[TP_BRAIN] % 3], LK_PICK, A_FOLLOW);
            }
        } else if (is_fx(trk)) {
            list_add(ACTION_NAME[A_EFFECT], fx_label(ui.pick_fx % NFXPICK), LK_PICK, A_EFFECT);
        } else if (!is_aux(trk)) {
            list_add(ACTION_NAME[A_ENGINE], EG_NAME[ui.pick_engine % NENGINE], LK_PICK, A_ENGINE);
            if (NPRESETS > 0)
                list_add(ACTION_NAME[A_PRESET], PRESETS[ui.pick_preset % NPRESETS].name, LK_PICK, A_PRESET);
            list_add(ACTION_NAME[A_SCRAMBLE], 0, LK_ACTION, A_SCRAMBLE);
            num_s((uint32_t)ui.pick_slot % NUSER_PRESETS + 1u, s_cat(slot_t, "U"));
            list_add(ACTION_NAME[A_SAVE_PRESET], slot_t, LK_PICK, A_SAVE_PRESET);
            list_add(ACTION_NAME[A_INIT], 0, LK_ACTION, A_INIT);
            s_cat(fol_t, FOLLOW_NAME[proj.trk[trk].par[TP_BRAIN] % 3]);
            list_add(ACTION_NAME[A_FOLLOW], fol_t, LK_PICK, A_FOLLOW);
            list_midi_ch(trk);
        }
    } else if (keep >= P_MIX) {                     /* GLO's one list: the pump's settings, the level, USB audio */
        static char by[8], rel[12], lev[8];
        char *e = num_i((int32_t)(master_pump_ms(proj.master[MS_PUMP_REL]) + 0.5f), rel);
        s_cat(e, " ms");
        num_i(proj.master[MS_LEVEL], lev);
        list_add(ACTION_NAME[A_PUMP_BY], pump_by_text(proj.master[MS_PUMP_BY], by), LK_PICK, A_PUMP_BY);
        list_add(ACTION_NAME[A_PUMP_REL], rel, LK_PICK, A_PUMP_REL);
        list_add(ACTION_NAME[A_PUMP_ACC], proj.master[MS_PUMP_ACC] ? "on" : "off", LK_PICK, A_PUMP_ACC);
        list_add(ACTION_NAME[A_LEVEL], lev, LK_PICK, A_LEVEL);
        list_add(ACTION_NAME[A_USB_AUDIO], USBA_NAME[proj.usb_full & 1u], LK_PICK, A_USB_AUDIO);
        list_add(ACTION_NAME[A_LOOP_FREE], proj.rsv[2] & LOOP_FREE ? "drift" : "in time", LK_PICK, A_LOOP_FREE);
        list_add(ACTION_NAME[A_AUTOSAVE], proj.rsv[2] & AUTOSAVE_OFF ? "off" : "on", LK_PICK, A_AUTOSAVE);
        list_add(ACTION_NAME[A_PANIC], 0, LK_ACTION, A_PANIC);
    }
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
#define LIST_RH 20
#define LIST_Y 24
#define LIST_H (FOOT_Y - LIST_Y)
static void list_paint(int32_t oy)
{
    uint32_t i;
    for (i = lst.top; i < lst.n && i < lst.top + LIST_SHOWN; i++) {
        int32_t y = 1 + (int32_t)(i - lst.top) * LIST_RH - oy, ty = cap_y(y, LIST_RH - 1, &FONT_S) - 1;   /* (descenders clear the foot) */
        int on = i == lst.sel, k = lst.row[i].kind;
        uint16_t fg = on ? K_BG : K_TEXT, dim = on ? K_PANEL2 : K_DIM;
        const char *v = lst.row[i].val;
        cv_round(6, y, 222, LIST_RH - 1, 5, on ? K_TEXT : K_PANEL);
        if (k == LK_KNOB) {
            cv_text(14, ty, &FONT_S, lst.row[i].name, dim);
            text_fit(218 - text_w(&FONT_B, v), ty, &FONT_B, v, 120, fg);
        } else {
            int32_t ne = cv_text(14, ty, &FONT_B, lst.row[i].name, fg);
            if (k == LK_PICK) {
                int32_t room = 206 - (ne + 6), w = text_w(&FONT_S, v);
                text_fit(206 - (w < room ? w : room), ty, &FONT_S, v, room, fg);
                glyph_pick(213, ty + 3, fg);
            } else {
                if (*v && 202 - text_w(&FONT_S, v) >= ne + 6)
                    cv_text(202 - text_w(&FONT_S, v), ty, &FONT_S, v, dim);
                glyph_enter(208, ty + 3, fg);
            }
        }
    }
    if (lst.n > LIST_SHOWN) {
        int32_t th = LIST_SHOWN * LIST_RH - 2, bh = th * LIST_SHOWN / lst.n, by = th * lst.top / lst.n;
        cv_round(232, 1 - oy, 4, th, 2, K_PANEL);
        cv_round(232, 1 + by - oy, 4, bh, 2, K_FAINT);
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
    cv_begin(240, MAIN_H, K_BG);
    list_paint(0);
    cv_blit(0, LIST_Y);
    cv_begin(240, LIST_H - MAIN_H, K_BG);
    list_paint(MAIN_H);
    cv_blit(0, LIST_Y + MAIN_H);
}

/* ------------------------------------------------------- the pages --- */
static int begin_main(uint32_t h)
{
    if (h == ui.sig[1])
        return 0;
    ui.sig[1] = h;
    ui.sig[5] = 0;                                   /* (the step record strip over it: drawn again) */
    cv_begin(240, MAIN_H, K_BG);
    return 1;
}
static uint32_t hash_pat(uint32_t h, const tpat_t *p)
{
    const uint32_t *w = (const uint32_t *)(const void *)p;
    for (uint32_t i = 0; i < sizeof *p / 4u; i++)
        h = hash_u(h, w[i]);
    return h;
}

/* Arrange: eleven rows (the tracks), each its pattern and a playhead, mutes dimmed; the scene big at the right */
#define AR_MH 7                                      /* Arrange's track meters: pixels tall */
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
    if (!begin_main(h))
        return;
    for (int tr = 0; tr < NSEQT; tr++) {
        int32_t y = 2 + tr * 10 + (tr >= NTRK ? 4 : 0);
        int muted = seq.mute >> tr & 1u, pt = seq.ppat[tr] % NPATT, len = tpat(tr)->len, pos = eng_step[tr];
        uint16_t ink = muted ? K_FAINT : K_DIM;
        char nm[8];
        if (tr < NTRK)
            num_s((uint32_t)tr + 1u, nm);
        else
            s_cat(nm, tr == AUX_BRAIN ? "CO" : tr == AUX_PUNCH ? "TH" : "TP");
        text_r(22, y - 3, &FONT_XS, nm, tr == sel_trk() ? K_TEXT : ink);
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
    cv_blit(0, MAIN_Y);
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
        text_c(120, 40, &FONT_B, "No scenes in the song yet", K_TEXT);
        text_c(120, 62, &FONT_S, ui.scene_pg ? "White keys add scenes 17 to 32" : "White keys add scenes 1 to 16", K_DIM);
        cv_blit(0, MAIN_Y);
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
    cv_blit(0, MAIN_Y);
}

/* Steps: what a press writes and the bars' squares, then the bar's 16 cells */
#define ST_X 4
#define ST_CW 29
#define ST_Y 22
#define ST_CH 46
#define ST_TW (ST_CW - 8)   /* a cell's line: at most this wide, clear of a held cell's two-pixel frame */
#define ST_MID ((ST_CW - 2) / 2)   /* a line's centre */
#define ST_NUM 0            /* the rows' text y in a cell (ink 3-11, 13-21, 23-31, 33-41) */
#define ST_L1 10
#define ST_L2 20
#define ST_MK 30
/* a cell's text: its notes or chord, the shown drum sound, a throw track's effect (draw_steps) */
static int step_on(const step_t *st, int trk)       /* the cell is "on" for the sound shown (drums) or any note */
{
    if (is_drum(trk))
        return kit_has(st, ui.dsound + 1);
    return !step_empty(st);
}
/* a chord from its notes (3 or 4): its shape (CHORD_SHAPE's, +1; 0 = none) and its root's pitch class. The root is a
 * note whose intervals to the others match a shape (the lowest tried first). A cell names it in full when that fits
 * ("C", "Am", "G7", "Dm7"), else by the short form ("CM7", "C+", "Co", "Ds2"): draw_steps */
static const struct { uint16_t mask; const char *suf, *sh; } CHORD_SHAPE[] = {
    {0x091, "", ""}, {0x089, "m", "m"}, {0x491, "7", "7"}, {0x891, "maj7", "M7"}, {0x489, "m7", "m7"},
    {0x049, "dim", "o"}, {0x111, "aug", "+"}, {0x085, "sus2", "s2"}, {0x0A1, "sus4", "s4"}};
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
    cv_blit(0, MAIN_Y);
}
static void draw_steps(void)
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
        cv_blit(0, MAIN_Y);
        return;
    }
    for (int i = 0; i < 16; i++)
        h = hash_u(h, step_has_lock(pats.lane, trk, pt, ui.bar * 16 + i));
    if (!begin_main(h))
        return;
    if (!is_aux(trk)) {                              /* an instrument: its sound first, then what a step takes */
        char nm[NAME_LEN + 1];
        int32_t x;
        for (int i = 0; i < NAME_LEN; i++)
            nm[i] = proj.trk[trk].name[i];
        nm[NAME_LEN] = 0;
        x = text_fit(ST_X + 2, 0, &FONT_XS, nm[0] ? nm : EG_NAME[proj.trk[trk].par[TP_ENGINE] % NENGINE], 96, K_TEXT);
        if (drum)
            s_cat(t, DL_SOUND_NAME[ui.dsound % DL_NSOUND]);
        else {
            char *e = t;
            *e = 0;
            for (int i = 0; i < ui.nrem && i < SN; i++) {
                if (i)
                    e = s_cat(e, " ");
                e = note_name(ui.rem[i], e);
            }
        }
        text_fit(x + 8, 0, &FONT_XS, t, 168 - (x + 8), K_DIM);
        t[0] = 0;
    } else if (trk == AUX_PUNCH) {
        char *e = s_cat(t, "fx: ");
        if (ui.rem_fx && PX_VALID(ui.rem_fx - 1))
            s_cat(s_cat(e, PUNCH_NAME[(ui.rem_fx - 1) & 15]), ui.rem_fx - 1 >= PX_MEL ? " (melodic)" : " (drums)");
        else
            s_cat(e, "play a key on EDIT first");
    } else {
        char *e = s_cat(t, "chord: ");
        for (int i = 0; i < ui.nrem && i < SN; i++) {
            if (i)
                e = s_cat(e, " ");
            e = note_name(ui.rem[i], e);
        }
    }
    if (t[0])
        cv_text(ST_X + 2, 0, &FONT_XS, t, K_DIM);
    {
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
        int s = ui.bar * 16 + i, in = s < tp->len, held = ui.held >> i & 1u, on;
        const step_t *st = &tp->st[s];
        int32_t x = ST_X + (i % 8) * ST_CW, y = ST_Y + (i / 8) * (ST_CH + 4);
        uint16_t bg, fg;
        on = step_on(st, trk);
        bg = !in ? K_BG : on ? K_TINT : K_PANEL;
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
                        cv_round(tx, y + 26, 4, 4, 1, tc);   /* their own row, between the name and the marks */
                    } else {
                        int r = j >= row1, n = r ? nt - row1 : row1, c = r ? j - row1 : j;
                        int32_t tx = x + (ST_CW - 2) / 2 - (n * 4 - 1) / 2 + c * 4;
                        cv_rect(tx, y + 25 + r * 4, 3, 3, tc);
                    }
                    j++;
                }
            if (on) {
                s_cat(t, DL_SOUND_NAME[ui.dsound % DL_NSOUND]);
                t[3] = 0;
                text_c(x + ST_MID, y + ST_L1, &FONT_XS, t, fg);
            }
        } else if (on) {
            uint16_t dim = bg == K_NOW ? K_BG : K_DIM;
            if (trk == AUX_PUNCH) {
                int fx = st->note[0] - 1;
                /* the effect in capitals (no descender to meet the line under it), three letters, over its group */
                static const char *const PX_CELL[NPUNCH] = {"MUT", "STU", "REP", "ECH", "BLD", "FLR", "JMB", "WBL",
                                                            "PAN", "OCT", "CHP", "RNG"};
                text_c(x + ST_MID, y + ST_L1, &FONT_XS, PX_VALID(fx) ? PX_CELL[(fx & 15) % NPUNCH] : "?", fg);
                text_c(x + ST_MID, y + ST_L2, &FONT_XS, PX_VALID(fx) && fx >= PX_MEL ? "mel" : "drm", dim);
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
                text_c(x + ST_MID, y + ST_L1, &FONT_XS, l1, fg);
                if (l2[0])
                    text_c(x + ST_MID, y + ST_L2, &FONT_XS, l2, c2);
            }
        }
        if (st->comp || st->comp2) {                 /* the tricks' letters, bottom right */
            char g[4] = {0};
            char *e = g;
            if (st->comp)
                e = s_cat(e, COMP_GLYPH[st->comp % NSC]);
            if (st->comp2)
                s_cat(e, COMP_GLYPH[st->comp2 % NSC]);
            text_r(x + ST_CW - 6, y + ST_MK, &FONT_XS, g, fg);
        }
        if (st->cond) {                              /* a small percent sign (5 x 7): the font's is too wide beside two letters */
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
    cv_blit(0, MAIN_Y);
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
        cv_blit(0, MAIN_Y);
        return;
    }
    static const char *const STEP_FRAC[NTS] = {"1/32", "1/16", "1/8", "3/16", "1/4", "5/16", "3/8", "1/2", "1 Bar"};
    e = num_s((uint32_t)tp->len, s_cat(t, "Length: "));
    e = s_cat(e, tp->len == 1 ? " step" : " steps");
    e = s_cat(e, "   Step: ");
    s_cat(e, STEP_FRAC[tp->scale % NTS]);
    cv_text(12, 6, &FONT_S, t, K_TEXT);
    {
        int nb = (tp->len + 15) / 16, ph = eng_step[trk], lf, le, go = seq.t[trk].bq_go, pf = seq.t[trk].bq_f, pe = seq.t[trk].bq_e;
        pat_loop(tp, &lf, &le);
        for (int b = 0; b < NBAR; b++) {
            int32_t x = 12 + b * 56, w = 50;
            int steps = tp->len - b * 16, in = b < nb && b * 16 < le && b * 16 + 16 > lf;
            steps = steps > 16 ? 16 : steps < 0 ? 0 : steps;
            int out = b < nb && (lf > 0 || le < tp->len) && !in;   /* (a cropped loop: the bars outside it sit back) */
            if (in && (lf > 0 || le < tp->len))              /* the loop: a bar under its bars */
                cv_rect(x, 79, w, 3, K_NOW);
            if (go != 0xFF && (pf > 0 || pe > 0)) {          /* a loop waiting for the bar line: its bars outlined */
                if (b * 16 < (pe ? pe : tp->len) && b * 16 + 16 > pf && b < nb)
                    cv_frame(x, 78, w, 5, 2, K_TEXT, K_BG);
            } else if (go != 0xFF && go / 16 == b)           /* a jump waiting for the bar line */
                cv_frame(x + w / 2 - 4, 78, 8, 5, 2, K_TEXT, K_BG);
            if (b == ui.bar)                         /* the bar the white keys write (OCT- OCT+ pick it) */
                cv_round(x - 2, 32, w + 4, 44, 7, K_TEXT);
            cv_round(x, 34, w, 40, 5, b < nb && !out ? K_PANEL2 : K_PANEL);
            if (steps < 16 && steps > 0)
                cv_round(x + w * steps / 16, 34, w - w * steps / 16, 40, 5, K_PANEL);
            if (playing() && ph / 16 == b)
                cv_round(x + 2 + 46 * (ph % 16) / 16, 64, 4, 8, 2, K_NOW);   /* (below the number) */
            num_s((uint32_t)b + 1u, t);
            text_c(x + w / 2, 46, &FONT_B, t, b < nb && !out ? K_TEXT : b < nb ? K_DIM : K_FAINT);
        }
    }
    e = s_cat(s_cat(t, "Groove "), GR_NAME[proj.groove % NGROOVE]);
    e = s_cat(e, " ");
    if (tp->groove > 0)
        e = s_cat(e, "+");
    e = num_i(tp->groove, e);
    e = num_s((uint32_t)tp->quant, s_cat(e, ", quantise "));
    s_cat(e, "%");
    cv_text(12, 86, &FONT_XS, t, K_DIM);
    cv_text(12, 100, &FONT_XS, is_drum(trk) ? "white keys: the bar's steps, black keys: sounds"
                                            : "black keys: scale 1 2 3 4 5 6 8 16 1/2", K_DIM);
    cv_blit(0, MAIN_Y);
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
    /* table: the harmonics; scan: their mix; bend: the phase bent; ripple: a second cycle sliding */
    float cy = (float)(y0 + h / 2), amp = (float)(h / 2 - 8), harm = (float)(1 + p[TP_E1] * 7 / 127);
    pen_t pn = {0};
    (void)f;
    cv_rect(x0, y0 + h / 2, w, 1, K_LINE);
    for (int32_t x = 0; x <= w; x++) {
        float ph = (float)x / (float)w * 2.0f, bend = ph + fm_sin_turns(ph) * (float)p[TP_E3] / 127.0f / 6.0f;
        float a = fm_sin_turns(bend), b = fm_sin_turns(bend * harm + (float)p[TP_E4] / 256.0f);
        pen_to(&pn, (float)(x0 + x), cy - (a * (127 - p[TP_E2]) + b * p[TP_E2]) / 127.0f * amp, K_NOW);
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
    int32_t bw = 18, gap = 9, bh = h - 22, rx = x0 + w - 24, ry = y0 + bh / 2 + 4;
    int amt = p[TP_E3] < 1 ? 1 : p[TP_E3] > 8 ? 8 : p[TP_E3], rate = p[TP_E4] < 1 ? 1 : p[TP_E4] > 8 ? 8 : p[TP_E4];
    organ_bars(p, bars);
    for (int k = 0; k < 6; k++) {
        int32_t x = x0 + k * (bw + gap), v = 6 + (int32_t)(bars[k] * (float)(bh - 6) + 0.5f);
        cv_round(x, y0, bw, bh, 3, K_PANEL2);
        cv_round(x, y0, bw, v, 3, k == 1 ? K_NOW : K_DIM);
        cv_rect(x + 3, y0 + v - 4, bw - 6, 2, K_BG);        /* the bar's handle */
        text_c(x + bw / 2, y0 + bh + 4, &FONT_XS, FT[k], K_DIM);
    }
    {
        float a = amt > 1 ? (float)f * 0.012f * (float)rate : 0.0f;   /* (rate 1..8: 0.5 .. 8 Hz, drawn slower) */
        float r = 6.0f + (float)(amt - 1) * 2.0f;
        pen_t ring = {0};
        for (int i = 0; i <= 32; i++) {
            float t = (float)i / 32.0f;
            pen_to(&ring, (float)rx + fm_sin_turns(t + 0.25f) * 20.0f, (float)ry + fm_sin_turns(t) * 20.0f, K_LINE);
        }
        cv_linef((float)rx - fm_sin_turns(a + 0.25f) * r, (float)ry - fm_sin_turns(a) * r * 0.4f,
                 (float)rx + fm_sin_turns(a + 0.25f) * r, (float)ry + fm_sin_turns(a) * r * 0.4f, K_NOW, 2.0f);
        text_c(rx, y0 + bh + 4, &FONT_XS, "rotor", K_DIM);
    }
}
/* knit: the wave (saw .. square .. pulse) a line a voice, the voices drifting apart as detune opens (one voice:
 * its slow drift) and fanned up and down as width opens */
static void pic_knit(int32_t x0, int32_t y0, int32_t w, int32_t h, const uint8_t *p, uint32_t f)
{
    static const float OFF[7] = {0.0f, 0.3f, -0.3f, 0.6f, -0.6f, 1.0f, -1.0f};
    int n = p[TP_E3] < 1 ? 1 : p[TP_E3] > 7 ? 7 : p[TP_E3];
    float wv = (float)p[TP_E1] / 127.0f, mix = wv < 0.5f ? wv * 2.0f : 1.0f, pw = wv < 0.5f ? 0.5f : 0.5f - (wv - 0.5f) * 0.8f;
    float det = (float)p[TP_E2] / 127.0f, amp = (float)h / 6.0f, fan = (float)p[TP_E4] / 127.0f * ((float)h / 2.0f - amp - 2.0f);
    cv_rect(x0, y0 + h / 2, w, 1, K_LINE);
    for (int j = n - 1; j >= 0; j--) {          /* the centre one last, on top */
        pen_t pn = {0};
        float o = n > 1 ? OFF[j] : 0.0f, drift = n > 1 ? o * det * (float)f / 24.0f : det * 0.1f * fm_sin_turns((float)f / 97.0f);
        float cy = (float)(y0 + h / 2) - o * fan;
        uint16_t col = j == 0 ? K_NOW : K_DIM;
        for (int32_t x = 0; x <= w; x++) {
            float ph = (float)x / (float)w * 3.0f + drift, fr = ph - fm_floorf(ph);
            float saw = 1.0f - 2.0f * fr, pulse = fr < pw ? 1.0f : -1.0f;
            pen_to(&pn, (float)(x0 + x), cy - (saw + (pulse - saw) * mix) * amp, col);
        }
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
static void pic_sample(int32_t x0, int32_t y0, int32_t w, int32_t h, const uint8_t *p)
{
    int idx = p[TP_E1] < smp_slots() ? p[TP_E1] : smp_slots() - 1, mode = p[TP_E4] % NSM;
    const smp_t *sm = smp_get(idx);
    int32_t wy = y0 + 20, wh = h - 22, cy = wy + wh / 2;
    char t[40], *e;
    static const char *const MODE_ARROW[NSM] = {"once", "loop", "backwards", "forth and back"};
    if (!sm) {
        text_c(x0 + w / 2, cy - 16, &FONT_S, smp_name(idx), K_DIM);
        text_c(x0 + w / 2, cy + 4, &FONT_XS, "empty: tools/loom_samples.py fills it", K_DIM);
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
        text_c(x0 + w / 2, cy + 4, &FONT_XS, "empty: tools/loom_samples.py fills it", K_DIM);
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
static void pic_drumline(int32_t x0, int32_t y0, int32_t w, int32_t h, const uint8_t *p, uint32_t f)
{
    int32_t pw = w / 6, ph = h / 2;
    (void)f;
    for (int k = 0; k < DL_NSOUND; k++) {
        int32_t x = x0 + (k % 6) * pw + 2, y = y0 + (k / 6) * ph + 2, hit = eng_drum_hit[k];
        static const char *const PAD[NDM] = {"kick", "snare", "hat", "clap", "shakr", "tamb", "strng", "cong", "tom", "fm",
                                             "smpl"};
        const char *nm = PAD[p[TP_DRUM(k, DP_MODEL)] % NDM];
        char t[4];
        cv_round(x, y, pw - 4, ph - 4, 4, hit > 40 ? K_NOW : k == ui.dsound ? K_PANEL2 : K_PANEL);
        if (k == ui.dsound)
            cv_frame(x, y, pw - 4, ph - 4, 4, K_SEL, hit > 40 ? K_NOW : K_PANEL2);
        {
            int32_t tw = text_w(&FONT_XS, nm);
            text_fit(tw <= pw - 9 ? x + (pw - 4 - tw) / 2 : x + 3, y + (ph - 4) / 2 - 6, &FONT_XS, nm, pw - 9,
                     hit > 40 ? K_BG : K_TEXT);
        }
        num_s((uint32_t)k + 1u, t);
        cv_text(x + 3, y, &FONT_XS, t, hit > 40 ? K_BG : K_FAINT);
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
static void draw_engine(void)
{
    int trk = sel_trk(), eg = proj.trk[trk].par[TP_ENGINE] % NENGINE;
    const uint8_t *p = par_now(trk);
    uint32_t h;
    pic_clock(trk);
    h = hash_u(hash_u(2166136261u, 0xE0u), (uint32_t)trk | (uint32_t)eg << 4 | (uint32_t)ui.dsound << 8 |
                                          (eg == EG_SAMPLE || eg == EG_SLICE ? 0u : (pic_f / 2) << 12));
    h = hash_u(h, (uint32_t)(pic_lvl * 32.0f));
    for (int i = 0; i < 4; i++)
        h = hash_u(h, p[TP_E1 + i]);
    if (eg == EG_SAMPLE || eg == EG_SLICE)           /* (the bank: an upload changes what the picture shows) */
        h = hash_u(h, (uint32_t)smp_user_count() << 16 ^ smp_user_bytes());
    if (eg == EG_SLICE) {                            /* the slice playing and where (a column of the picture) */
        int sl;
        int32_t pos;
        const smp_t *sm = smp_get(p[TP_E1]);
        if (engine_slice_now(trk, &sl, &pos) && sm)
            h = hash_u(h, (uint32_t)(sl + 1) | (uint32_t)((float)pos * 200.0f / (float)sm->n) << 8);
    }
    if (eg == EG_DRUMLINE)
        for (int k = 0; k < DL_NSOUND; k++)
            h = hash_u(h, p[TP_DRUM(k, DP_MODEL)] | (uint32_t)eng_drum_hit[k] << 8);
    h = hash_s(h, proj.trk[trk].name);
    if (!begin_main(h))
        return;
    cv_text(10, 2, &FONT_B, EG_NAME[eg], K_TEXT);
    {
        char nm[NAME_LEN + 1];
        for (int i = 0; i < NAME_LEN; i++)
            nm[i] = proj.trk[trk].name[i];
        nm[NAME_LEN] = 0;
        if (nm[0])
            text_r(230, 3, &FONT_S, nm, K_DIM);
    }
    switch (eg) {
    case EG_JACQUARD: pic_wave(20, 24, 200, 88, p, pic_f); break;
    case EG_RAYON: pic_rayon(20, 24, 200, 88, p, pic_f); break;
    case EG_BELLOWS: pic_organ(20, 24, 200, 88, p, pic_f); break;
    case EG_SAMPLE: pic_sample(20, 24, 200, 88, p); break;
    case EG_SLICE: pic_slice(20, 24, 200, 88, p, trk); break;
    case EG_CANE: pic_cane(20, 24, 200, 88, p, pic_f); break;
    case EG_VOILE: pic_voile(20, 24, 200, 88, p, pic_f); break;
    case EG_KNIT: pic_knit(20, 24, 200, 88, p, pic_f); break;
    default: pic_drumline(10, 26, 220, 90, p, pic_f); break;
    }
    cv_blit(0, MAIN_Y);
}

/* the aux tracks' pages */
static void draw_brain(void)
{
    const uint8_t *p = proj.trk[AUX_BRAIN].par;
    int key = seq.brain_key % 12, sc = seq.brain_scale % NBS, deg = (int8_t)seq.brain_deg, src = p[TP_E1] % 9u;
    uint32_t h = hash_u(hash_u(2166136261u, 0xB0u), (uint32_t)key | (uint32_t)sc << 4 | (uint32_t)(deg + 64) << 8 |
                                          (uint32_t)src << 16 | (uint32_t)ui.page << 20 | (uint32_t)p[TP_E4] << 26);
    char t[48], *e;
    for (int i = 0; i < NTRK; i++)
        h = hash_u(h, proj.trk[i].par[TP_BRAIN] | (uint32_t)is_drum(i) << 4);
    if (!begin_main(h))
        return;
    e = s_cat(s_cat(t, NOTE_NAME[key]), " ");
    s_cat(e, BS_NAME[sc]);
    cv_text(12, 4, &FONT_L, t, K_TEXT);
    if (src) {
        s_cat(s_cat(t, "chords from "), TRACK_NAME[src - 1]);
        text_r(230, 6, &FONT_XS, t, K_DIM);
    }
    if (ui.page == P_PLAY) {
        cv_text(12, 44, &FONT_XS, "routing: black keys 1 to 8 toggle a track", K_DIM);
    } else if (ui.page == P_SUB) {
        if (!src)
            cv_text(12, 34, &FONT_XS, "off: the chords come from the", K_DIM), cv_text(12, 48, &FONT_XS, "conductor's own keys and steps", K_DIM);
        else if (is_drum(src - 1)) {
            s_cat(s_cat(t, TRACK_NAME[src - 1]), " is a kit: it has no notes");
            cv_text(12, 40, &FONT_XS, t, K_REC);
        } else {
            s_cat(s_cat(t, TRACK_NAME[src - 1]), "'s lowest note sets the chord:");
            cv_text(12, 34, &FONT_XS, t, K_DIM);
            cv_text(12, 48, &FONT_XS, "chords, a bass line; it follows nothing", K_DIM);
        }
    } else {
        e = s_cat(t, "degree ");
        if (deg > 0)
            e = s_cat(e, "+");
        num_i(deg, e);
        cv_text(12, 44, &FONT_B, t, deg ? K_NOW : K_DIM);
        if (p[TP_E4])
            text_r(230, 48, &FONT_XS, LINK_NAME[p[TP_E4] % 9], K_DIM), text_r(230 - text_w(&FONT_XS, LINK_NAME[p[TP_E4] % 9]) - 4, 48, &FONT_XS, "link", K_FAINT);
    }
    cv_text(12, 70, &FONT_XS, src ? "tracks that follow; the chords' in its colour" : "tracks that follow", K_DIM);
    for (int i = 0; i < NTRK; i++) {
        int32_t x = 12 + i * 27;
        int m = proj.trk[i].par[TP_BRAIN] % 3;
        num_s((uint32_t)i + 1u, t);
        if (src == i + 1) {                          /* the FOLLOW track: the chords come from it */
            cv_round(x, 86, 22, 22, 5, trk_ink(i));
            text_c(x + 11, 90, &FONT_B, t, K_BG);
            continue;
        }
        if (m == 1)
            cv_round(x, 86, 22, 22, 5, K_TEXT);
        else if (m == 2)
            cv_frame(x, 86, 22, 22, 5, K_TEXT, K_BG);
        else
            cv_frame(x, 86, 22, 22, 5, K_LINE, K_BG);
        text_c(x + 11, 90, &FONT_B, t, m == 1 ? K_BG : m == 2 ? K_TEXT : K_FAINT);
    }
    cv_blit(0, MAIN_Y);
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
    cv_blit(0, MAIN_Y);
}
static void draw_fxtrack(int trk)                    /* AUX_FX1 / AUX_FX2: the effect and a picture of it */
{
    int pick = fx_pick_of(trk), type = fx_pick_type(pick);
    uint32_t h = hash_u(hash_u(2166136261u, 0xF0u), (uint32_t)trk | (uint32_t)pick << 4 | (ui.frame / 3) << 8 |
                                          (uint32_t)ui.last_inst << 24);
    char t[32];
    if (!begin_main(h))
        return;
    cv_text(12, 4, &FONT_L, FXT_NAME[type], K_TEXT);
    if (type == FXT_REVERB)                          /* the reverb's mode under the description */
        cv_text(12, 58, &FONT_S, fx_pick_name(pick), K_NOW);
    s_cat(s_cat(t, trk == AUX_FX1 ? "send effect I, keys play " : "send effect II, keys play "), TRACK_NAME[inst_trk()]);
    cv_text(12, 40, &FONT_XS, t, K_DIM);
    {
        int32_t x0 = 130, y0 = 60, w = 100;
        uint32_t f = ui.frame;
        switch (type) {
        case FXT_CHORUS: {
            pen_t p0 = {0}, p1 = {0};
            for (int32_t x = 0; x <= w; x++) {
                float t = (float)x / 192.0f + (float)f / 96.0f;
                pen_to(&p1, (float)(x0 + x), (float)(y0 + 20) + 10.0f * fm_sin_turns(t + 0.16f), K_DIM);
                pen_to(&p0, (float)(x0 + x), (float)(y0 + 20) + 10.0f * fm_sin_turns(t), K_NOW);
            }
            break;
        }
        case FXT_DELAY:
            for (int k = 0; k < 5; k++)
                cv_round(x0 + k * 20, y0 + 30 - (30 >> k), 10, 30 >> k, 2, k ? K_DIM : K_NOW);
            break;
        case FXT_LOFI:
            for (int32_t x = 0; x < w; x += 8)
                cv_rect(x0 + x, y0 + 20 - sin100((uint32_t)(x / 8)) * 16 / 100, 8, 3, K_NOW);
            break;
        case FXT_PHASER:
            for (int k = 0; k < 4; k++) {
                int32_t x = x0 + 10 + k * 24 + sin100(f / 3 + (uint32_t)k * 8) * 6 / 100;
                cv_round(x, y0, 6, 40, 3, k & 1 ? K_DIM : K_NOW);
            }
            break;
        default:
            for (int k = 0; k < 6; k++) {                /* rings spreading out, fainter as they go */
                float rr = 8.0f + (float)k * 7.0f;
                pen_t pn = {0};
                for (int i = 0; i <= 48; i++) {
                    float t = (float)i / 48.0f;
                    pen_to(&pn, (float)(x0 + 50) + rr * fm_sin_turns(t + 0.25f), (float)(y0 + 20) + rr * 0.5f * fm_sin_turns(t),
                           k & 1 ? K_DIM : K_NOW);
                }
            }
            break;
        }
    }
    cv_blit(0, MAIN_Y);
}

static void draw_play(void)
{
    int trk = sel_trk();
    const uint8_t *p = par_now(trk);
    uint32_t h = hash_u(hash_u(2166136261u, 0x9Au), (uint32_t)trk | (uint32_t)p[TP_VMODE] << 4 | (uint32_t)eng_voices << 24);
    char t[40];
    if (!begin_main(h))
        return;
    cv_text(12, 6, &FONT_B, tp_info_par(TP_VMODE, p)->names[p[TP_VMODE] % 3], K_TEXT);
    cv_text(12, 26, &FONT_XS, p[TP_VMODE] % 3 == VM_POLY ? "up to 6 voices on the track"
                              : p[TP_VMODE] % 3 == VM_MONO ? "one voice, every key restarts it"
                                                            : "one voice, held keys glide", K_DIM);
    for (int v = 0; v < NVOICE; v++)
        cv_round(12 + v * 13, 50, 10, 10, 3, v < eng_voices ? K_NOW : K_LINE);
    num_s((uint32_t)eng_voices, s_cat(t, "voices sounding "));
    cv_text(12, 66, &FONT_XS, t, K_DIM);
    cv_blit(0, MAIN_Y);
}
/* Play 2: the sound's octave, five places, the one now lit */
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
    cv_blit(0, MAIN_Y);
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
    cv_text(12, 24, &FONT_XS, fl ? "from the note's start, over two seconds" : "fall at 0: the index as the knob sets it",
            K_DIM);
    cv_rect(x0, y0 + hh, w, 1, K_LINE);
    for (int i = 0; i <= w; i++) {
        float t = 2.0f * (float)i / (float)w, e = fl ? floor_ + (1.0f - floor_) * fm_expf(-t / tau) : 1.0f;
        pen_to(&pb, (float)(x0 + i), (float)(y0 + hh) - e * hi * (float)hh, K_FAINT);
        pen_to(&pa, (float)(x0 + i), (float)(y0 + hh) - e * (float)hh, K_NOW);
    }
    cv_blit(0, MAIN_Y);
}
/* knit's Sub page: its layers stacked, each as tall as its level: the wave (faint: the first page's), osc 2 at its
 * interval, the sub an octave down, the noise */
static void draw_knit_x(void)
{
    int trk = sel_trk();
    const uint8_t *p = par_now(trk);
    int st = clampi((int)p[TP_X1] - 64, -24, 24);
    uint32_t h = hash_u(hash_u(2166136261u, 0xA4u), (uint32_t)trk | (uint32_t)p[TP_X1] << 4 | (uint32_t)p[TP_X2] << 12 |
                                                    (uint32_t)p[TP_X3] << 20);
    float wv = (float)p[TP_E1] / 127.0f, mix = wv < 0.5f ? wv * 2.0f : 1.0f, pw = wv < 0.5f ? 0.5f : 0.5f - (wv - 0.5f) * 0.8f;
    float ratio = fm_exp2f((float)st * (1.0f / 12.0f)), lv[4] = {1.0f, (float)p[TP_X2] / 127.0f, (float)p[TP_X3] / 127.0f,
                                                                   (float)p[TP_X4] / 127.0f};
    static const char *const NM[4] = {"wave", "osc 2", "sub", "noise"};
    uint32_t r = 12345u;
    char t[24];
    h = hash_u(h, p[TP_X4]);
    h = hash_u(h, p[TP_E1]);
    if (!begin_main(h))
        return;
    for (int l = 0; l < 4; l++) {
        pen_t pn = {0};
        float cy = 14.0f + (float)l * 26.0f, amp = 10.0f * lv[l];
        cv_text(8, (int32_t)cy - 6, &FONT_XS, NM[l], lv[l] > 0.0f ? K_DIM : K_FAINT);
        for (int32_t x = 0; x <= 170; x++) {
            float ph = (float)x / 170.0f * 3.0f * (l == 1 ? ratio : l == 2 ? 0.5f : 1.0f), fr = ph - fm_floorf(ph), y;
            if (l == 2)
                y = fm_sin_turns(ph);
            else if (l == 3)
                r = r * 1664525u + 1013904223u, y = (float)(int32_t)r * (1.0f / 2147483648.0f);
            else
                y = (1.0f - 2.0f * fr) + ((fr < pw ? 1.0f : -1.0f) - (1.0f - 2.0f * fr)) * mix;
            pen_to(&pn, (float)(56 + x), cy - y * amp, l == 0 ? K_FAINT : K_NOW);
        }
    }
    if (st)
        num_i(st, s_cat(t, st > 0 ? "osc 2 +" : "osc 2 ")), s_cat(t + s_len(t), " st");
    else
        s_cat(t, "osc 2 in unison");
    text_r(230, 100, &FONT_XS, t, K_DIM);
    cv_blit(0, MAIN_Y);
}
/* the Sub page: the engine's second page of knobs (TP_X1..X4), knit's layers or rayon's fall */
static void draw_sub(void)
{
    if (proj.trk[sel_trk()].par[TP_ENGINE] == EG_RAYON)
        draw_rayon_fall();
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
    text_fit(16, 16 + 84 + 3, &FONT_XS, t, 208, K_DIM);
    cv_blit(0, MAIN_Y);
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
        s_cat(s_cat(t, DL_SOUND_NAME[key]), ": no filter, PRESET picks one");
        text_fit(x0, y0 + hh + 3, &FONT_XS, t, w, K_DIM);
        cv_blit(0, MAIN_Y);
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
    cv_blit(0, MAIN_Y);
}
static void draw_env(int filter)
{
    int trk = sel_trk(), b = filter ? TP_FA : TP_A, drum = is_drum(trk) && filter;
    const uint8_t *p = par_now(trk);
    uint32_t h = hash_u(hash_u(2166136261u, 0xE7u), (uint32_t)trk | (uint32_t)filter << 4 | (uint32_t)p[b] << 8 |
                                          (uint32_t)p[b + 1] << 16 | (uint32_t)p[b + 2] << 24);
    float x0 = 16.0f, y0 = 16.0f, w = 208.0f, hh = 84.0f, base = y0 + hh, x = x0, a, d, r, s;
    float px[64], py[64];
    int n = 0;
    pen_t pn = {0};
    h = hash_u(h, p[b + 3] | (uint32_t)drum << 8);
    if (!begin_main(h))
        return;
    cv_rect(16, 16 + 84, 208, 1, K_LINE);
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
    for (int i = 1; i < n; i++)                      /* under the curve, the track's colour thinned */
        for (int32_t xi = f_floor(px[i - 1] + 0.5f); xi < f_floor(px[i] + 0.5f); xi++) {
            float t = px[i] > px[i - 1] ? (xi - px[i - 1]) / (px[i] - px[i - 1]) : 0.0f;
            int32_t yt = f_floor(py[i - 1] + (py[i] - py[i - 1]) * t + 1.5f);
            if (yt < (int32_t)base)
                cv_rect(xi, yt, 1, (int32_t)base - yt, K_TINT);
        }
    for (int i = 0; i < n; i++)
        pen_to(&pn, px[i], py[i], K_NOW);
    cv_text(16, 16 + 84 + 3, &FONT_XS, drum ? "every key: lag, attack, random, length"
                                              : filter ? "filter envelope" : "amp envelope", K_DIM);
    cv_blit(0, MAIN_Y);
}
static void draw_filter(void)
{
    int trk = sel_trk();
    const uint8_t *p = par_now(trk);
    int ft = p[TP_FTYPE] % NFT, cut = p[TP_CUT], res = p[TP_RES], dyn = ft == FT_DYNAMIC;
    uint32_t h = hash_u(hash_u(2166136261u, 0xF1u), (uint32_t)trk | (uint32_t)ft << 4 | (uint32_t)cut << 8 | (uint32_t)res << 16 |
                                          (uint32_t)p[TP_KTRK] << 24);
    int32_t x0 = 16, y0 = 12, w = 208, hh = 84, fc = x0 + 10 + cut * (w - 20) / 127, py = -1;
    if (is_drum(trk) && ui.page == P_FILTER) {       /* a kit's Filter page: the key's own */
        draw_key_filter();
        return;
    }
    if (!begin_main(h))
        return;
    cv_rect(x0, y0 + hh, w, 1, K_LINE);
    if (is_drum(trk) && !dyn) {                      /* the kit's bus filter, switched off */
        cv_rect(x0, y0 + 22, w, 2, K_DIM);
        cv_text(x0, y0 + hh + 3, &FONT_XS, "all keys: no bus filter, PRESET turns it on", K_DIM);
        cv_blit(0, MAIN_Y);
        return;
    }
    if (dyn) {                                       /* low cut .. hi cut: the band that passes */
        int32_t lo = x0 + p[TP_CUT] * (w - 20) / 127, hi = x0 + 20 + p[TP_KTRK] * (w - 20) / 127;
        cv_round(lo, y0 + 10, hi > lo ? hi - lo : 2, hh - 10, 4, K_NOW_T);
        cv_rect(lo, y0 + 10, 2, hh - 10, K_NOW);
        cv_rect(hi, y0 + 10, 2, hh - 10, K_NOW);
        cv_text(x0, y0 + hh + 3, &FONT_XS, "all keys: low cut, compression, drive, hi cut", K_DIM);
        cv_blit(0, MAIN_Y);
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
            float y = filt_y(filt_db((float)(x0 + x - fc), q, hp, bp, steep), y0, hh);
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
    text_r(x0 + w, y0 + hh + 3, &FONT_XS, p[TP_KTRK] ? "key tracking on" : "", K_DIM);
    cv_blit(0, MAIN_Y);
}
/* the instrument's character stage: its type, what it does, and a picture of it (synth.c char_picture: the
 * transfer curve, a crushed or ring-modulated wave, the tilt's response), dry dimmed behind */
static void draw_character(void)
{
    static const char *const ABOUT[NCT] = {"the voice as it is", "a soft saturation", "a sine wavefolder",
                                           "fewer bits, a lower rate", "a ring modulator", "a tilt EQ",
                                           "worn tape: warmth and wow", "an old sampler's grain"};
    int trk = inst_trk(), drum = is_drum(trk);
    const uint8_t *p = par_now(trk);
    int ct = p[TP_CTYPE] % NCT;
    uint32_t h = hash_u(hash_u(2166136261u, 0xC4u), (uint32_t)trk | (uint32_t)ct << 4 | (uint32_t)p[TP_CAMT] << 8 |
                                          (uint32_t)p[TP_CTONE] << 16 | (uint32_t)p[TP_CMIX] << 24);
    int32_t x0 = 128, y0 = 8, w = 100, hh = 96, cy = y0 + hh / 2;
    char t[32];
    h = hash_u(h, (uint32_t)drum);
    if (!begin_main(h))
        return;
    cv_text(12, 4, &FONT_L, CTYPE_NAME(p), K_TEXT);
    cv_text(12, 40, &FONT_XS, ABOUT[ct], K_DIM);
    s_cat(s_cat(s_cat(t, drum ? "on all of " : "after "), TRACK_NAME[trk]), drum ? "'s keys" : "'s filter");
    cv_text(12, 56, &FONT_XS, t, K_DIM);
    cv_rect(x0, cy, w, 1, K_LINE);
    if (ct != CT_TILT && ct != CT_CRUSH && ct != CT_RING && ct != CT_SAMPLER)
        cv_rect(x0 + w / 2, y0, 1, hh, K_LINE);
    {
        float ys[101], ds[101];
        pen_t pd = {0}, pn = {0};
        uint8_t dry[NTP_SOUND];
        int32_t i;
        for (i = 0; i < NTP_SOUND; i++)
            dry[i] = p[i];
        dry[TP_CMIX] = 0;                        /* the dry curve: the same with the mix at 0 */
        for (i = 0; i <= w; i++) {
            float u = ct == CT_TILT || ct == CT_CRUSH || ct == CT_RING || ct == CT_SAMPLER ? (float)i / (float)w
                                                                                     : (float)i * 2.0f / (float)w - 1.0f;
            float y = fm_clampf(char_picture(p, u), -1.2f, 1.2f), d = fm_clampf(char_picture(dry, u), -1.2f, 1.2f);
            ys[i] = (float)cy - y * (float)(hh / 2 - 6) / 1.2f;
            ds[i] = (float)cy - d * (float)(hh / 2 - 6) / 1.2f;
        }
        if (ct != CT_OFF)
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
            pen_to(&pn, (float)(x0 + i), ys[i], ct == CT_OFF ? K_DIM : K_NOW);
    }
    cv_blit(0, MAIN_Y);
}
static void draw_sends(void)                         /* the instrument's two sends (a kit's: the key's), named with
                                                      * the effects on them */
{
    int trk = inst_trk(), key = ui.dsound % DL_NSOUND, drum = is_drum(trk);
    const uint8_t *p = par_now(trk), *sv = drum ? p + TP_DRUM(key, DP_SEND1) : p + TP_SEND1;
    uint32_t h = hash_u(hash_u(2166136261u, 0x5Eu), (uint32_t)trk | (uint32_t)sv[0] << 4 | (uint32_t)sv[1] << 12 |
                                          (uint32_t)p[TP_LEVEL] << 28);
    char t[32];
    h = hash_u(h, proj.trk[AUX_FX1].par[TP_ENGINE] | (uint32_t)proj.trk[AUX_FX2].par[TP_ENGINE] << 8 |
                      (uint32_t)(drum ? key + 1 : 0) << 16);
    if (!begin_main(h))
        return;
    s_cat(s_cat(t, drum ? DL_SOUND_NAME[key] : TRACK_NAME[trk]), " sends to");
    cv_text(12, 4, &FONT_B, t, K_TEXT);
    for (int i = 0; i < 2; i++) {
        int32_t y = 40 + i * 32, v = sv[i] * 132 / 127;
        cv_text(12, y - 2, &FONT_S, i == 0 ? "FX I" : "FX II", K_TEXT);
        cv_text(12, y + 11, &FONT_XS, fx_label(fx_pick_of(AUX_FX1 + i)), K_DIM);
        cv_round(92, y + 3, 132, 8, 4, K_LINE);
        if (v > 0)
            cv_round(92, y + 3, v < 8 ? 8 : v, 8, 4, K_NOW);
    }
    cv_blit(0, MAIN_Y);
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
            if (lt == LT_WAVE || lt == LT_WOBBLE)        /* their shapes (synth.c lfo_wave_value, lfo_wave_trem) */
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
        char t[40], num[12], unit[6], *e = s_cat(t, "to ");
        tp_text_par(TP_LDEST, p[TP_LDEST], p, num, unit);
        e = s_cat(s_cat(e, num), " ");
        tp_text_par(TP_LX, p[TP_LX], p, num, unit);
        s_cat(e, num);
        cv_text(x0, y0 + hh + 3, &FONT_XS, t, K_DIM);
    } else if (lt)
        cv_text(x0, y0 + hh + 3, &FONT_XS, tp_info_par(TP_LTYPE, p)->names[lt], K_DIM);
    cv_blit(0, MAIN_Y);
}
#define BLACK_KEY(n) ((0x54Au >> ((n) % 12)) & 1)          /* C# D# F# G# A#: a black key's semitones */
static void draw_players(void)
{
    int trk = sel_trk(), pl = pl_type(trk);
    const player_t *pr = &seq.player[trk < NTRK ? trk : 0];
    uint32_t h = hash_u(hash_u(2166136261u, 0x9Cu), (uint32_t)trk | (uint32_t)pl << 4 | (uint32_t)pr->nheld << 8 |
                                          (uint32_t)pr->nchord << 12 | (uint32_t)pr->arp_note << 16 | (uint32_t)pr->chord[0] << 24);
    char t[48], *e;
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
        cv_text(12, 24, &FONT_XS, "a kit's keys are its sounds: no arp, no strum", K_DIM);
        cv_text(12, 48, &FONT_S, "WEAVE is on the last page (SELECT)", K_DIM);
        cv_blit(0, MAIN_Y);
        return;
    }
    cv_text(12, 4, &FONT_B, tp_info_par(TP_PLAYER, proj.trk[trk].par)->names[pl], K_TEXT);
    if (pl == PL_OFF)
        cv_text(12, 24, &FONT_XS, "the keys play as they are", K_DIM);
    else if (pl == PL_ARP)
        cv_text(12, 24, &FONT_XS, "held notes play one after another", K_DIM);
    else if (pl == PL_STRUM)
        cv_text(12, 24, &FONT_XS, "one key strums the chord; keys together set it", K_DIM);
    else
        cv_text(12, 24, &FONT_XS, "notes stay until the next", K_DIM);
    e = s_cat(t, pl == PL_STRUM ? "chord: " : "held: ");
    if (pl == PL_STRUM) {
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
    text_fit(12, 48, &FONT_S, t, 216, K_TEXT);
    {   /* the keys as they are under the hands (F3 .. G5 at this octave): down (held, or latched by hold) in the track's
         * ink, the notes the player sounds now bright, the strum's chord marked under its keys */
        int base = KEY_BASE + ui.oct * 12, wi = 0;
        int32_t wx[27], y0 = 64, wh = 44, bh = 27, ww = 13;
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
    cv_blit(0, MAIN_Y);
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
static int lev_now(int t)                             /* a track's level now (a lock's, a motion's) */
{
    int lv = engine_live(t, TP_LEVEL);
    return lv >= 0 ? lv : proj.trk[t].par[TP_LEVEL];
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
    uint32_t h = hash_u(hash_u(hash_u(2166136261u, 0x3Cu), (uint32_t)sel | (uint32_t)seq.mute << 4), ui.solo);
    float fall = (float)(dt > 1000u ? 1000u : dt) * 0.024f;
    m_last = now;
    for (int t = 0; t < NALLTRK; t++) {          /* the engine's loudest since the last look, then the fall */
        float db = lin_db((float)ui_peak[t] * (1.0f / 16383.5f));   /* (meters_take's) */
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
        uint16_t ink = muted ? K_LINE : trk_ink(t);
        text_c(x + 7, 94, &FONT_XS, SHORT_NM[t], t == sel ? K_TEXT : muted || !sound ? K_FAINT : K_DIM);
        if (muted)
            cv_rect(x + 2, 108, 10, 2, K_FAINT);
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
        cv_round(x - 1, y0 - fy - 2, 16, 4, 1, t == sel ? K_SEL : K_DIM);   /* the fader */
        cv_rect(x + 1, y0 - fy, 12, 1, K_BG);
    }
    if (ui.solo)
        text_r(236, 108, &FONT_XS, "solo", K_TEXT);
    cv_blit(0, MAIN_Y);
}
/* Master color: the type and what it does, and the Character page's picture of it (synth.c char_picture, through a
 * voice's bytes made from the master's knobs: the same curves) */
static void draw_master_color(void)
{
    static const char *const ABOUT[NMC] = {"the mix as it is", "warmth and wow", "an old sampler's grain"};
    const uint8_t *m = proj.master;
    int ty = m[MS_CTYPE] % NMC;
    uint32_t h = hash_u(hash_u(2166136261u, 0xC0u), (uint32_t)ty | (uint32_t)m[MS_CAMT] << 8 | (uint32_t)m[MS_CTONE] << 16 |
                                                    (uint32_t)m[MS_CMIX] << 24);
    int32_t x0 = 128, y0 = 8, w = 100, hh = 72, cy = y0 + hh / 2;
    uint8_t par[NTP_SOUND];
    pen_t pn = {0};
    if (!begin_main(h))
        return;
    cv_text(12, 4, &FONT_L, MC_NAME[ty], K_TEXT);
    cv_text(12, 40, &FONT_XS, ABOUT[ty], K_DIM);
    cv_text(12, 56, &FONT_XS, "on the whole mix", K_DIM);
    for (int i = 0; i < NTP_SOUND; i++)
        par[i] = 0;
    par[TP_CTYPE] = ty == MC_TAPE ? CT_TAPE : ty == MC_SAMPLER ? CT_SAMPLER : CT_OFF;
    par[TP_CAMT] = m[MS_CAMT];
    par[TP_CTONE] = m[MS_CTONE];
    par[TP_CMIX] = m[MS_CMIX];
    cv_rect(x0, cy, w, 1, K_LINE);
    for (int32_t i = 0; i <= w; i++) {
        float u = ty == MC_SAMPLER ? (float)i / (float)w : (float)i * 2.0f / (float)w - 1.0f;
        float y = fm_clampf(char_picture(par, u), -1.2f, 1.2f);
        pen_to(&pn, (float)(x0 + i), (float)cy - y * (float)(hh / 2 - 6) / 1.2f, ty == MC_OFF ? K_FAINT : K_NOW);
    }
    cv_blit(0, MAIN_Y);
}
static void draw_master(void)
{
    int pg = ui.page == P_MASTER ? 0 : 2;
    const uint8_t *m = proj.master;
    uint32_t h = hash_u(hash_u(2166136261u, 0x3Au), (uint32_t)pg);
    int32_t x0 = 16, y0 = 12, w = 208, hh = 84, cy = y0 + hh / 2;
    int pv = pg == 2 ? (int)(eng_pump_db * (float)(hh - 10) / 24.0f) : 0;   /* the pump's duck, now (pixels) */
    for (int i = 0; i < NMS; i++)
        h = hash_u(h, m[i]);
    h = hash_u(h, (uint32_t)pv);
    if (!begin_main(h))
        return;
    cv_rect(x0, cy, w, 1, K_LINE);
    if (pg == 0) {
        pen_t pn = {0};
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
    cv_blit(0, MAIN_Y);
}
static void setup_row(int32_t y, const char *name, const char *val)
{
    cv_text(12, y, &FONT_S, name, K_DIM);
    text_r(228, y, &FONT_S, val, K_TEXT);
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
        text_c(120, 50, &FONT_S, "KNOB 1: a shape to turn it on", K_DIM);
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
    cv_blit(0, MAIN_Y);
}
static void draw_setup(void)
{
    char t[24];
    uint32_t h = hash_u(2166136261u, 0x5E700u | (uint32_t)proj_slot | (uint32_t)proj.usb_full << 4 | (uint32_t)seq.ext << 8 |
                                          (uint32_t)proj.rsv[0] << 12 | (uint32_t)proj.send_clock << 16 | (uint32_t)proj.send_notes << 17 |
                                          (uint32_t)proj.rsv[1] << 20 | (uint32_t)(proj.rsv[2] & AUTOSAVE_OFF) << 26);
    if (!begin_main(h))
        return;
    (void)t;
    setup_row(2, "tempo from", seq.ext ? "MIDI clock in" : "LOOM-8");
    setup_row(18, "sends clock", proj.send_clock ? "yes" : "no");
    setup_row(34, "sends notes", proj.send_notes ? "yes" : "no");
    setup_row(50, "USB audio", USBA_NAME[proj.usb_full & 1u]);
    setup_row(66, "autosave", proj.rsv[2] & AUTOSAVE_OFF ? "off: SAVE saves" : "on");
    setup_row(82, "theme", THEME_OF_PROJ()->name);
    setup_row(98, "version", APP_VERSION);
    cv_blit(0, MAIN_Y);
}
static uint32_t status_peak;                          /* the highest peak shown since the screen reader said it (ui.c) */
static void draw_status(void)
{
    char t[24], *e;
    uint32_t cpu = plat_cpu_pct(), peak = plat_cpu_peak_pct(), xr = plat_xruns();
    if (peak > status_peak)
        status_peak = peak;
    uint32_t h = hash_u(2166136261u, 0x57A00u | cpu | peak << 8 | (uint32_t)eng_voices << 16 | (uint32_t)eng_voices_max << 24);
    h = hash_u(h, xr ^ eng_guard_count << 8 ^ (uint32_t)engine_core2_state() << 28);
    if (!begin_main(h))
        return;
    e = num_s(cpu, t);
    s_cat(e, "%");
    setup_row(4, "audio load", t);
    e = num_s(peak, t);
    s_cat(e, "%");
    setup_row(22, "peak", t);
    e = num_s((uint32_t)eng_voices, t);
    e = s_cat(e, " of ");
    e = num_s((uint32_t)eng_voices_max, e);
    setup_row(40, "voices", t);
    setup_row(58, "second core", CORE2_NAME[engine_core2_state() & 3]);
    num_s(eng_guard_count, t);
    setup_row(76, "guard trips", t);
    num_s(xr, t);
    setup_row(94, "dropouts", t);
    cv_blit(0, MAIN_Y);
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
    theme = THEME_OF_PROJ();
    if (k_th == theme && k_now == trk_ink(trk))
        return;
    if (k_th != theme)                               /* another theme: the whole screen, the gaps between the bands too */
        lcd_fill(0, 0, 240, 240, K_BG);
    k_th = theme;
    k_now = trk_ink(trk);
    k_tint = trk_tint(trk);
    k_mid = ink_mix(k_now, theme->mid);
    for (uint32_t i = 0; i < sizeof ui.sig / sizeof ui.sig[0]; i++)
        ui.sig[i] ^= 0x5A5A5A5Au;
}
/* ARP's Weave page: the track, what it weaves with and how, in words with their names, and its first bar three
 * ways: its own steps, the other pattern's, and what plays woven (each step in the ink of the pattern it came from) */
static void weave_row(int32_t y, const char *label, uint16_t lc, const tpat_t *p, const tpat_t *a,
                      const tpat_t *b, int ka, int kb, int rule, uint16_t ca, uint16_t cb, int bar)
{
    cv_text(8, y, &FONT_XS, label, lc);
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
        s_cat(s_cat(t, TRACK_NAME[trk]), " weaves with nothing yet");
        text_c(120, 26, &FONT_B, t, K_TEXT);
        text_c(120, 54, &FONT_S, "black key: the track to weave in", K_DIM);
        text_c(120, 72, &FONT_S, "white key: its pattern", K_DIM);
        cv_blit(0, MAIN_Y);
        return;
    }
    num_s((uint32_t)cur_pat(trk) + 1u, s_cat(s_cat(an, TRACK_NAME[trk]), " P"));   /* "T2 P1", "T1 P3" */
    num_s((uint32_t)wv_pat(trk) + 1u, s_cat(s_cat(bn, TRACK_NAME[f - 1]), " P"));
    if (f - 1 == trk)                                /* its own patterns: "T2 weaves P1 with P3" */
        e = s_cat(s_cat(s_cat(s_cat(t, TRACK_NAME[trk]), " weaves "), an + s_len(TRACK_NAME[trk]) + 1), " with ");
    else
        e = s_cat(s_cat(t, TRACK_NAME[trk]), " weaves with ");
    s_cat(e, f - 1 == trk ? bn + s_len(TRACK_NAME[trk]) + 1 : TRACK_NAME[f - 1]);
    cv_text(8, 2, &FONT_B, t, K_TEXT);
    e = s_cat(s_cat(t, "by "), WEAVE_NAME[r]);
    e = s_cat(e, ": ");
    if (r == WV_BARS || r == WV_STEPS) {             /* in words, with their names */
        e = s_cat(s_cat(e, r == WV_BARS ? "a bar of " : "a step of "), an);
        s_cat(s_cat(e, ", then "), bn);
    } else
        s_cat(s_cat(s_cat(s_cat(e, r == WV_RHYTHM ? an : bn), "'s rhythm, "), r == WV_RHYTHM ? bn : an), "'s pitch");
    text_fit(8, 26, &FONT_XS, t, 228, K_NOW);
    {
        const tpat_t *a = tpat(trk), *b = &pats.pat[f - 1][wv_pat(trk)];
        uint16_t ca = trk_ink(trk), cb = f - 1 == trk || trk_ink(f - 1) == ca ? K_TEXT : trk_ink(f - 1);
        int ka = wv_kind(trk), kb = wv_kind(f - 1), la = a->len > b->len ? a->len : b->len;
        int nb = (la + 15) / 16, bar = ui.bar < nb ? ui.bar : nb - 1;
        weave_row(44, an, K_DIM, a, a, b, ka, kb, r, ca, cb, bar);
        weave_row(58, bn, K_DIM, b, a, b, ka, kb, r, ca, cb, bar);
        cv_rect(54, 73, 175, 1, K_LINE);
        weave_row(77, "plays", K_TEXT, 0, a, b, ka, kb, r, ca, cb, bar);
        if (nb > 1) {                                /* more than a bar: which one, OCT for the others */
            char bt[16];
            num_s((uint32_t)nb, s_cat(num_s((uint32_t)bar + 1u, s_cat(bt, "bar ")), " of "));
            cv_text(232 - text_w(&FONT_XS, bt), 98, &FONT_XS, bt, K_DIM);
        }
    }
    if (!wv_cued(trk))
        s_cat(t, "on: it plays woven");
    else if ((seq.wv_live | seq.wv_rec) >> trk & 1u)
        s_cat(t, "weaving now");
    else
        num_s((uint32_t)trk + 1u, s_cat(t, "plays while the WEAVE track's white key ")), s_cat(t + s_len(t), " is on");
    cv_text(8, 98, &FONT_XS, t, !wv_cued(trk) || (seq.wv_live | seq.wv_rec) >> trk & 1u ? K_NOW : K_DIM);
    cv_blit(0, MAIN_Y);
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
        int32_t x = 6 + (t % 5) * 46, y = 2 + (t / 5) * 48;
        int f = wv_from(t), lit = on >> t & 1u;
        char wt[16];
        cv_round(x, y, 42, 46, 5, lit ? K_NOW : f ? K_PANEL2 : K_PANEL);
        text_c(x + 21, y + 3, t < NTRK ? &FONT_S : &FONT_XS, TRACK_NAME[t], lit ? K_BG : f ? K_TEXT : K_FAINT);
        if (f) {                                 /* "+T1 P3" (its own: "+P3"), the rule */
            if (t == f - 1)
                num_s((uint32_t)wv_pat(t) + 1u, s_cat(wt, "+P"));
            else
                num_s((uint32_t)wv_pat(t) + 1u, s_cat(s_cat(s_cat(wt, "+"), TRACK_NAME[f - 1]), " P"));
            text_c(x + 21, y + 18, &FONT_XS, wt, lit ? K_BG : K_DIM);
            text_c(x + 21, y + 29, &FONT_XS, WEAVE_NAME[wv_rule(t)], lit ? K_BG : K_DIM);
            any = 1;
        } else
            text_c(x + 21, y + 22, &FONT_XS, "-", K_FAINT);
    }
    cv_text(8, 99, &FONT_XS, !any ? "no weaves yet: set one on a track's ARP"
                             : proj.trk[AUX_WEAVE].par[TP_E1] ? "latch: a tap on, a tap off" : "a key held: that track weaves",
            K_DIM);
    cv_blit(0, MAIN_Y);
}
static void draw_page(void)
{
    int trk = sel_trk();
    if (ui.card) {
        draw_card();
        return;
    }
    if (lst.open) {
        draw_list();
        return;
    }
    switch (ui.page) {
    case P_ARRANGE: draw_arrange(); break;
    case P_SONG: draw_song(); break;
    case P_STEPS: draw_steps(); break;
    case P_BAR: draw_bar(); break;
    case P_CHARACTER: draw_character(); break;
    case P_SENDS: draw_sends(); break;
    case P_FX1: draw_fxtrack(AUX_FX1); break;
    case P_FX2: draw_fxtrack(AUX_FX2); break;
    case P_ENGINE: case P_PLAY: case P_PLAY2: case P_SUB: case P_ENV: case P_ENV2: case P_KITF: case P_FILTER: case P_LFO:
    case P_LFO2: case P_PLAYERS: case P_PLAYERS2: case P_PLAYERS3:
        if (trk == AUX_BRAIN)
            draw_brain();
        else if (trk == AUX_PUNCH)
            draw_punch();
        else if (trk == AUX_WEAVE)
            draw_weave_play();
        else if (is_aux(trk))
            draw_fxtrack(trk);
        else if (ui.page == P_ENGINE || (is_drum(trk) && ui.page == P_PLAY))
            draw_engine();
        else if (ui.page == P_PLAY)
            draw_play();
        else if (ui.page == P_PLAY2 && is_drum(trk))
            draw_sends();
        else if (ui.page == P_PLAY2)
            draw_play2();
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
    case P_MASTER: case P_MASTER3: draw_master(); break;
    case P_MASTER2: draw_master_color(); break;
    case P_GLFO: draw_glfo(); break;
    case P_SETUP: draw_setup(); break;
    default: draw_status(); break;
    }
}
