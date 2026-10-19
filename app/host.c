/* SPDX-License-Identifier: GPL-3.0-only */
/* LOOM-8 in fm1-base's host simulator (base/host/plat_host.c, which has the common commands): LOOM-8's platform
 * extras as the simulator has them, its checks and its commands. Included by app.c, inside plat_host.c (its statics:
 * now_ms, enc_acc, store, run_ms ...). Tracks, patterns, steps and scenes count from 1 in a script, as the screen
 * shows them; "expect step" reports the playhead 0-based (eng_step).
 *   param T P V                  parameter P (synth.h TP_*: a number or a knob's name, as the knob command's) of track
 *                                T set to V, as a knob would
 *   load N                       every block costs N % (the overload guard's input); -1 = measured
 *   meter MS                     run MS ms; print the modelled load's mean and peak (the model: load_model below)
 *   steps T PAT                  print track T's pattern PAT: each step's notes, component, condition
 *   step T PAT S NOTE VEL        write one note on step S (NOTE 0 clears the step)
 *   chord T PAT S LEN N1 N2 [N3 [N4]]  notes together on step S, LEN quarter steps long
 *   scene N                      switch to scene N now
 *   punch KEY on|off             a punch-in key (0..26 = F3..G5, or w0../b0..) held or released, on the group
 *   brainkey N on|off            a key on the BRAIN track (MIDI note N)
 *   bpm N                        the project's tempo (a decimal allowed: 0.1 BPM steps)
 *   projnew                      a new project (Setup's NEW PROJECT)
 *   projsave SLOT [NAME]         the project saved into slot SLOT (1..3), named NAME ('_' for a space) if given
 *   cores N                      the second core on (1) or off (0): its job then runs in place
 *   samples FILE                 the user sample bank from FILE (an image from tools/loom_samples.py build), as an
 *                                upload would leave it (empty at boot)
 *   upload FILE                  an upload's SysEx frames (tools/loom_samples.py syx), each through samples.c as from
 *                                USB; a frame not answered, or answered with an error, fails the run
 *   expect WHAT VALUE            besides fm1-base's: playing, scene, mute (the tracks' bits), voices, drumhits (the kit's hits),
 *                                guard, cores, dirty, ask, mem (the last save's
 *                                share of a project slot, %), model_avg /
 *                                model_peak (the last meter's, %), tempo, trk, user / userbytes / rateN (the user bank's
 *                                samples, its data's bytes, user N's rate), punch_drum / punch_mel (the masks in effect,
 *                                PX_* bits), parT.P (P a number or a name: par2.x1, par1.d3.decay), mst.NAME (a
 *                                master value, the mst command's names), pump (the
 *                                pump's duck now, tenths of a dB); and with more words: step T VALUE, page NAME, comp T S VALUE (prints
 *                                both components; checks the first), note T PAT S VALUE
 * The demos' stems and taps (tools/demos/render.sh --stems), from the environment:
 *   LOOM_SOLO=1,3,fx1,fx2        only these tracks (dry and their sends) and FX returns sound
 *   LOOM_MST=raw|eq|comp         the master's output taken after that stage (raw: the groups summed, the level;
 *                                comp: the compressor and the level, no limiter); unset: all of it
 *   LOOM_LOAD=N                  the overload guard sees N % (the host's own timing, which varies with what else
 *                                runs, out of the result: render.sh sets 0)
 *   LOOM_MSTSET=comp=0,pump=0    master knobs held at these values whatever the script sets (at each "mst" and at
 *                                "stats reset", which every song has before its "wav")
 * LOOM_THEME=N: the screenshots in theme N (ui.c). The layout audit (gfx_loom.c): its findings fail the run. */
#include <time.h>

static float meter_avg, meter_peak, host_load;      /* the last "meter", the model's load now (below) */

/* ---- LOOM-8's platform extras (app.h), simulated */
static uint32_t cpu_peak;
uint32_t plat_cpu_peak_pct(void)
{
    uint32_t p = cpu_peak;
    cpu_peak = 0;
    return p;
}
static double host_ns(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec * 1e9 + (double)t.tv_nsec;
}
static uint32_t render_t0(void) { return (uint32_t)(uint64_t)(host_ns() / 10.0); }
static uint32_t render_pct(uint32_t t0, uint32_t n)   /* the block's share of its time on this machine, % */
{
    uint32_t pct = (uint32_t)(100.0 * (double)(render_t0() - t0) * 10.0 / ((double)n * 1e9 / 44100.0));
    if (pct > cpu_peak)
        cpu_peak = pct;
    return pct;
}
uint32_t plat_cycles(void) { return render_t0(); }   /* the profiler's clock: nanoseconds / 10 here */

/* the store's copy before the current one (storage.c keeps two on the FM-1): kept here beside fm1-base's store */
static uint8_t store_prev[OBJ_NOBJ][PLAT_STORE_MAX];
static int store_prev_len[OBJ_NOBJ] = {[0 ... OBJ_NOBJ - 1] = -1};
int loom_store_save(uint32_t obj, const void *src, uint32_t len)
{
    int n = obj < OBJ_NOBJ ? plat_store_size(obj) : -1, rc;
    if (n > 0) {
        memcpy(store_prev[obj], store[obj], (size_t)n);
        store_prev_len[obj] = n;
    }
    rc = plat_store_save(obj, src, len);
    if (rc && n > 0)
        store_prev_len[obj] = -1;                   /* (a save refused: nothing moved) */
    return rc;
}
int plat_store_load_prev(uint32_t obj, void *dst, uint32_t max)
{
    int n;
    if (obj >= OBJ_NOBJ || (n = store_prev_len[obj]) < 0)
        return -1;
    if ((uint32_t)n > max)
        n = (int)max;
    memcpy(dst, store_prev[obj], (size_t)n);
    return n;
}

/* the second core, simulated: a job runs at once, in order (the split path, the same sums) */
static int cpu2_on = 1;
int plat_cpu2_set(int on) { cpu2_on = on; return 0; }
int plat_cpu2_on(void) { return cpu2_on; }
int plat_cpu2_run(void (*fn)(uint32_t), uint32_t arg)
{
    if (!cpu2_on)
        return 0;
    fn(arg);
    return 1;
}
int plat_cpu2_wait(void) { return 0; }
void plat_cpu2_sync(void) {}
int plat_cpu2_busy(void) { return 0; }   /* (a job ran at once) */
int plat_cpu2_gave_up(void) { return 0; }
uint32_t plat_cpu2_bad(void) { return 0; }
int plat_cpu2_barred(void) { return 0; }
int plat_vdc14_mv(void) { return 1600; }
int plat_sysvdd_mv(void) { return 1380; }
int plat_last_crash(uint32_t v[5]) { (void)v; return 0; }

/* ---- the script */
static void run_ms(uint32_t ms);                        /* (plat_host.c's, after this) */
static int host_fails;
static int key_of(const char *s);

static int host_kit(int t) { return t >= 0 && t < NTRK && proj.trk[t].par[TP_ENGINE] == EG_DRUMLINE; }   /* 0-based */
static tpat_t *pat_of(int t, int p)             /* track T, pattern P (1-based; P 0 = the one playing) */
{
    if (t < 1 || t > NSEQT)
        return 0;
    if (p == 0)
        p = seq.ppat[t - 1] + 1;
    if (p < 1 || p > NPATT)
        return 0;
    return &pats.pat[t - 1][p - 1];
}

static void steps_print(int t, int p)
{
    tpat_t *tp = pat_of(t, p);
    int s, k;
    if (!tp) {
        printf("steps: no track %d pattern %d\n", t, p);
        return;
    }
    printf("  track %d pattern %d: %d steps, scale %d, nlen %d\n", t, p, tp->len, tp->scale, tp->nlen);
    for (s = 0; s < tp->len; s++) {
        const step_t *st = &tp->st[s];
        if (step_empty(st) && !st->comp && !st->comp2 && !st->cond)
            continue;
        printf("    %2d:", s + 1);
        for (k = 0; k < SN && !host_kit(t - 1); k++)
            if (st->note[k])
                printf(" %d", st->note[k]);
        for (k = 1; k <= NDRUM && host_kit(t - 1); k++)   /* a kit's step: its sounds */
            if (kit_has(st, k))
                printf(" %d", k);
        printf("  vel %d len %d nudge %d comp %d/%d comp2 %d/%d cond %d\n", st->vel, st->len, st->nudge, st->comp,
               st->cval, st->comp2, st->cval2, st->cond);
    }
    for (k = 0; k < NLANE; k++) {                   /* the pattern's lock lanes: each locked step and its value */
        const lane_t *l = &pats.lane[k];
        if (!l->used || l->trk != t - 1 || l->pat != p - 1)
            continue;
        printf("    lane %d param %d:", k, l->param);
        for (s = 0; s < NSTEP; s++)
            if (l->val[s] != LOCK_NONE)
                printf(" %d=%d", s + 1, l->val[s]);
        printf("\n");
    }
}

static int isnum(const char *s);
static int ieq(const char *a, const char *b);
static int param_of(const char *s);
static const char *const MN[NMS] = {"lo", "mid", "hi", "-", "color", "camount", "ctone", "cmix", "perc", "mel",
                                    "comp", "pump", "pumpby", "release", "accent", "level"};
/* fm1-base's expect, for LOOM-8's own state with one value */
int app_expect(const char *what, int *got)
{
    if (!strcmp(what, "playing"))
        *got = seq.playing;
    else if (!strcmp(what, "scene"))
        *got = seq.scene + 1;
    else if (!strcmp(what, "mute"))
        *got = seq.mute;
    else if (!strcmp(what, "voices"))
        *got = eng_voices;
    else if (!strcmp(what, "drumhits"))             /* the kit's hits so far (sequenced and played) */
        *got = (int)host_drum_hits;
    else if (!strcmp(what, "guard"))
        *got = eng_voices_max;
    else if (!strcmp(what, "model_avg"))            /* the last "meter": the model's mean and peak load, % */
        *got = (int)(meter_avg + 0.5f);
    else if (!strcmp(what, "model_peak"))
        *got = (int)(meter_peak + 0.5f);
    else if (!strcmp(what, "cores"))
        *got = engine_core2_state();
    else if (!strcmp(what, "dirty"))
        *got = ui_dirty();
    else if (!strcmp(what, "ask"))
        *got = ui_asking();
    else if (!strcmp(what, "mem"))                  /* the last save's share of a project slot, % */
        *got = project_mem_pct();
    else if (!strcmp(what, "tempo"))
        *got = (int)(seq_tempo(&seq) + 0.5f);
    else if (!strcmp(what, "trk"))
        *got = proj.trk_sel + 1;
    else if (!strcmp(what, "user"))                 /* the user bank: its samples, its data's bytes, user N's rate */
        *got = smp_user_count();
    else if (!strcmp(what, "userbytes"))
        *got = (int)smp_user_bytes();
    else if (!strncmp(what, "rate", 4) && isnum(what + 4)) {
        const smp_t *sm = smp_get(smp_factory() + atoi(what + 4) - 1);
        *got = sm ? sm->rate : 0;
    }
    else if (!strcmp(what, "punch_drum"))           /* the punch masks in effect (seq.h PX_* bits) */
        *got = (int)eng_punch_drum;
    else if (!strcmp(what, "punch_mel"))
        *got = (int)eng_punch_mel;
    else if (!strcmp(what, "pump"))                 /* the pump's duck now, tenths of a dB */
        *got = (int)(eng_pump_db * 10.0f + 0.5f);
    else if (!strncmp(what, "mst.", 4)) {           /* mst.NAME: a master value */
        int k;
        for (k = 0; k < NMS && !ieq(what + 4, MN[k]); k++)
            ;
        if (k == NMS)
            return 0;
        *got = proj.master[k];
    }
    else if (!strncmp(what, "par", 3) && strchr(what, '.')) {   /* parT.P: parameter P of track T */
        int t = atoi(what + 3) - 1, p = param_of(strchr(what, '.') + 1);
        if (t < 0 || t >= NALLTRK || p < 0 || p >= NTP)
            return 0;
        *got = proj.trk[t].par[p];
    } else
        return 0;
    return 1;
}

static int expect_val(const char *what, int got, const char *val)   /* (fm1-base's expect's way with a value) */
{
    if (val[0] == '<' || val[0] == '>') {
        int lim = (int)strtol(val + 1, 0, 0);
        if (val[0] == '<' ? got >= lim : got <= lim) {
            printf("FAIL expect %s %s, got %d\n", what, val, got);
            return 1;
        }
        printf("  ok %s == %d (%s)\n", what, got, val);
        return 0;
    }
    if (got != (int)strtol(val, 0, 0)) {
        printf("FAIL expect %s == %s, got %d\n", what, val, got);
        return 1;
    }
    printf("  ok %s == %d\n", what, got);
    return 0;
}

/* the expects with more words than fm1-base's: 1 = handled (a failure counted in host_fails) */
static int expect_more(const char *what, const char *a, const char *b, const char *c)
{
    if (!strcmp(what, "step")) {                    /* expect step T VALUE: the playhead of track T (0-based) */
        host_fails += expect_val(what, eng_step[(atoi(a) - 1) % NSEQT], b);
    } else if (!strcmp(what, "page")) {             /* expect page NAME ('_' for a space: Play_2) */
        const char *p = ui_page_name();
        int bad = 0, i;
        for (i = 0; !bad && (p[i] || a[i]); i++)    /* (stops at the first difference: neither read past its end) */
            bad = p[i] != (a[i] == '_' ? ' ' : a[i]);
        printf("%s page \"%s\"%s%s\n", bad ? "FAIL" : "  ok", p, bad ? ", expected " : "", bad ? a : "");
        host_fails += bad;
    } else if (!strcmp(what, "comp")) {             /* expect comp T S VALUE: step S's component on the playing pattern */
        tpat_t *tp = pat_of(atoi(a), 0);
        int s = atoi(b) - 1;
        if (!tp || s < 0 || s >= NSTEP) {
            printf("FAIL expect comp: no track %s step %s\n", a, b);
            host_fails++;
            return 1;
        }
        printf("  comp %s step %s: %d/%d and %d/%d\n", a, b, tp->st[s].comp, tp->st[s].cval, tp->st[s].comp2,
               tp->st[s].cval2);
        host_fails += expect_val(what, tp->st[s].comp, c);
    } else if (!strcmp(what, "note")) {             /* expect note T PAT S VALUE: five words */
        char t[16], pt[16], st[16], v[16];
        tpat_t *tp;
        int s, got;
        if (sscanf(plat_host_line, "%*s %*s %15s %15s %15s %15s", t, pt, st, v) != 4) {
            printf("FAIL expect note: T PAT S VALUE\n");
            host_fails++;
            return 1;
        }
        tp = pat_of(atoi(t), atoi(pt));
        s = atoi(st) - 1;
        got = !tp || s < 0 || s >= NSTEP ? -1 : host_kit(atoi(t) - 1) ? kit_first(&tp->st[s]) : tp->st[s].note[0];
        if (got != atoi(v)) {
            printf("FAIL expect note %s %s %s == %s, got %d\n", t, pt, st, v, got);
            host_fails++;
        } else
            printf("  ok note %s %s %s == %d\n", t, pt, st, got);
    } else if (!strcmp(what, "lane")) {            /* expect lane T PAT PARAM N: the steps a lock / motion lane holds */
        char t[16] = {0}, pt[16] = {0}, pn[24] = {0}, nv[16] = {0};
        int ln, got = 0;
        sscanf(plat_host_line, "%*s %*s %15s %15s %23s %15s", t, pt, pn, nv);
        ln = lane_find(pats.lane, atoi(t) - 1, atoi(pt) - 1, param_of(pn));
        for (int i = 0; ln >= 0 && i < NSTEP; i++)
            got += pats.lane[ln].val[i] != LOCK_NONE;
        if ((atoi(nv) == 0) != (got == 0) || (atoi(nv) > 0 && got < atoi(nv))) {
            printf("FAIL expect lane %s %s %s >= %s, got %d\n", t, pt, pn, nv, got);
            host_fails++;
        } else
            printf("  ok lane %s %s %s: %d steps\n", t, pt, pn, got);
    } else if (!strcmp(what, "weave")) {           /* expect weave PAT S MASK LEN: the WEAVE track's step */
        char pt[16] = {0}, st[16] = {0}, m[16] = {0}, l[16] = {0};
        const wv_step_t *w;
        sscanf(plat_host_line, "%*s %*s %15s %15s %15s %15s", pt, st, m, l);
        w = &pats.weave[(atoi(pt) - 1) & (NPATT - 1)].st[(atoi(st) - 1) & (NSTEP - 1)];
        if (wv_mask(w) != (uint32_t)strtol(m, 0, 0) || w->len != atoi(l)) {
            printf("FAIL expect weave %s %s == %s %s, got 0x%03x %d\n", pt, st, m, l, (unsigned)wv_mask(w), w->len);
            host_fails++;
        } else
            printf("  ok weave %s %s == %s %s\n", pt, st, m, l);
    } else if (!strncmp(what, "par", 3) && !strchr(what, '.')) {
        printf("FAIL unknown expect %s\n", what);
        host_fails++;
    } else
        return 0;
    return 1;
}

/* ---- the song commands (tools/demos): what a player writes on the device, in fewer words. Names where the
 * screen has them, so a script reads as the pages do and survives a parameter's index moving:
 *   knob T PARAM VALUE           a knob turned to VALUE as the UI turns it (set_tp: an engine, LFO type or drum
 *                                model starts from its own defaults; FX tracks re-applied). PARAM: a TP_* name
 *                                (below) or its number; VALUE: a number, a name of the value's list (spaces as _),
 *                                a sample's name, or uN = user sample N; a kit's send1 / send2: every key's (the
 *                                kit has no track sends: each key its own, dK.send1)
 *   preset T NAME [ENGINE]       a factory preset by name (exact, else the first holding the word; else, with
 *                                ENGINE, that engine's first, with a warning)
 *   pat T PAT LEN [SCALE [NLEN [GROOVE [SHAPE]]]]   a pattern's length, scale (sixteenths a step: h 1 2 3 4 5 6
 *                                8 16), note length %, groove amount, lock shape
 *   hits T PAT SOUND STRING      a drum sound on the steps: one character a step from step 1 (| and spaces skip):
 *                                . or - none, x 100, X 127, o 80, g 45, 1..9 = 14..126; a step's other sounds stay
 *                                (a drum step has one velocity: the loudest written)
 *   notes T PAT S LEN VEL N1 [N2 [N3 [N4]]]   notes on step S, LEN steps long (decimals: 0.25 steps; 0 = the
 *                                pattern's note length); N a number or a name (C4 = 60, F#3, Bb2)
 *   trick T PAT S NAME VAL[:MODE]  a trick (pulse hold multiply velocity rampup rampdown random porta bend tonality
 *                                jump skiplock skiptrick skip) at its value 1..9 (10 random), the black keys';
 *                                skips' MODE 0 last pass, 1 not the last, 2 only the first. A third replaces the second
 *   weaveset T FROM PAT RULE     track T's weave (ARP's Weave page): with track FROM's pattern PAT by RULE
 *   weave T DST A B RULE         pattern DST of track T = A woven with B (bars steps rhythm notes: SEL's WEAVE WITH
 *                                COPIED, A the pattern, B the copy)
 *   chance T PAT S PCT|A:B       the step's condition
 *   nudge T PAT S TICKS          -12..12 (24 a step)
 *   lock T PAT S PARAM VALUE     a parameter lock on the step (PARAM / VALUE as knob)
 *   scenedef N P1 .. P10         scene N's pattern for tracks 1-8, COND, THROW (1-based; 0 = pattern 1); used
 *   scenemute N MASK             scene N's mutes: a character a track 1-8, COND, THROW: x muted, . not
 *   song S:R [S:R ...]           the song: scene S (1-based) for R bars (16 sixteenths each)
 *   songloop 0|1
 *   mst NAME VALUE               a master knob (lo mid hi color camount ctone cmix perc mel comp pump) or the
 *                                out page's list (pumpby kick|t1..t8|metro, release, accent on|off, level)
 *   groove NAME                  the project's groove type (swing lilt weave ...)
 *   play T NOTE on|off [VEL]     a key on track T (through its player and the conductor), at any octave
 *   throw NAME on|off perc|mel [T]   a throw (mute stutter repeat echo build floor jumble warble pan octave chop
 *                                ring) held or let go, on the percussive or melodic group, or on track T only
 *   bars N                       run N bars at the tempo (in sixteenths: 0.25 a beat), counted from the start on the
 *                                sequencer's own sixteenths while it plays (the waits never drift)
 *   stats [reset]                the peaks since the last reset: synth and drum voices, guard trips, and the load
 *                                the FM-1 would show (a model: below) */
static const char *const TPN[TP_DRUM0] = {
    "engine", "e1", "e2", "e3", "e4", "vmode", "porta", "bend", "volume", "a", "d", "s", "r", "fa", "fd", "fs", "fr",
    "cut", "res", "fenv", "ktrk", "ftype", "send1", "send2", "ltype", "lspeed", "lamt", "ldest", "lx", "lx2",
    "ctype", "camt", "ctone", "cmix", "x1", "x2", "x3", "x4", "oct", "player", "p1", "p2", "p3", "p4", "ap1", "ap2",
    "ap3", "ap4", "level", "pan", "brain", "chan"};
_Static_assert(TP_X1 == 34 && TP_OCT == 38 && TP_PLAYER == 39 && TP_CHAN == TP_DRUM0 - 1,
               "TPN: a name for every TP_* in its place");
static const char *const DPN[DP_N] = {"model", "k1", "k2", "k3", "k4", "random", "pan", "volume", "mode", "attack",
                                      "hold", "decay", "lag", "cut", "res", "fenv", "drive", "ftype", "send1",
                                      "send2"};
static int ieq(const char *a, const char *b)       /* names alike: case and '_' for ' ' */
{
    for (; *a && *b; a++, b++) {
        char x = *a == '_' ? ' ' : *a, y = *b == '_' ? ' ' : *b;
        if (x >= 'A' && x <= 'Z')
            x = (char)(x + 32);
        if (y >= 'A' && y <= 'Z')
            y = (char)(y + 32);
        if (x != y)
            return 0;
    }
    return !*a && !*b;
}
static int isnum(const char *s) { return *s && strspn(s + (*s == '-'), "0123456789") == strlen(s + (*s == '-')); }
static void host_die(const char *what, const char *s)
{
    printf("demo: %s \"%s\" (line: %s)\n", what, s, plat_host_line);
    exit(2);
}
static int param_of(const char *s)                 /* a TP_* name, dK.NAME (drum key K 1..11), or a number */
{
    int k;
    if (isnum(s))
        return atoi(s);
    if (s[0] == 'd' && s[1] >= '1' && s[1] <= '9') {
        int key = atoi(s + 1);
        const char *dot = strchr(s, '.');
        if (dot && key >= 1 && key <= NDRUM)
            for (k = 0; k < DP_N; k++)
                if (ieq(dot + 1, DPN[k]))
                    return TP_DRUM(key - 1, k);
    }
    for (k = 0; k < TP_DRUM0; k++)
        if (ieq(s, TPN[k]))
            return k;
    host_die("no parameter", s);
    return 0;
}
static int value_of(int t, int p, const char *s)   /* a number, a name of the value's list, uN */
{
    const tp_info_t *in = t < NTRK ? tp_info_par(p, proj.trk[t].par) : 0;
    if (in && in->kind == PK_NAMES && in->names)
        for (int k = in->lo; k <= in->hi; k++)
            if (in->names[k] && ieq(s, in->names[k]))
                return k;
    if (s[0] == 'u' && isnum(s + 1))
        return smp_factory() + atoi(s + 1) - 1;
    if (in && in->kind == PK_SAMPLE)                /* a sample by its name: "ah", "break" */
        for (int k = 0; k < smp_slots(); k++)
            if (ieq(s, smp_name(k)))
                return k;
    if (t == AUX_FX1 || t == AUX_FX2)
        for (int k = 0; k < NFXPICK; k++)
            if (p == TP_ENGINE && ieq(s, fx_pick_name(k)))
                return k;
    if (!isnum(s))
        host_die("no value", s);
    return atoi(s);
}
static int note_of(const char *s)                  /* 60, C4, F#3, Bb2 */
{
    static const int8_t L[7] = {9, 11, 0, 2, 4, 5, 7};   /* a .. g */
    int n, i = 1;
    char c = s[0];
    if (isnum(s))
        return atoi(s);
    if (c >= 'a' && c <= 'g')
        c = (char)(c - 32);
    if (c < 'A' || c > 'G')
        host_die("no note", s);
    n = L[c - 'A'];
    if (s[1] == '#')
        n++, i++;
    else if (s[1] == 'b')
        n--, i++;
    return n + 12 * (atoi(s + i) + 1);
}
static int words(char *buf, char **w, int max)     /* the script's line in words (w[0] the command) */
{
    int n = 0;
    char *p;
    snprintf(buf, 512, "%s", plat_host_line);
    for (p = strtok(buf, " \t\r\n"); p && n < max; p = strtok(0, " \t\r\n")) {
        if (p[0] == '#')
            break;
        w[n++] = p;
    }
    return n;
}
static tpat_t *pat_or_die(const char *t, const char *p)
{
    tpat_t *tp = pat_of(atoi(t), atoi(p));
    if (!tp || atoi(p) < 1)
        host_die("no track / pattern", plat_host_line);
    return tp;
}
static step_t *step_or_die(tpat_t *tp, const char *s)
{
    int k = atoi(s) - 1;
    if (k < 0 || k >= NSTEP)
        host_die("no step", s);
    return &tp->st[k];
}

/* the peaks, every simulated millisecond: the voices, and the load the FM-1's Status page would show, by a model
 * of its stages (tools/perf_read.py: what each took on the FM-1, 2026-10-18; tests/scenarios/loadmodel.xy checks the
 * fit to tools/measure_load.sh, tools/demos/device_load.py the songs'). A block is a serial part on the first core
 * (the sequencer, a stretch's set-up, the mix, the master: idle LOAD_IDLE, a little more playing; the lofi and
 * the phaser, which run in time there) and a parallel part: the second core renders the effects a block behind
 * (the delay, the reverbs, the chorus: each its cost awake, a little asleep: fx_awake) and voices [0, k) (engine.c
 * split_at), the first the rest and the drum tracks, and waits for the second. The voices cost by split_at's units
 * (synth_voice_cost: an ordinary voice 4, LOAD_UNIT % of a block a unit; a drum voice 2, a drum track sounding 1).
 * A block where notes start or drums hit costs more on the first core (their set-up, in the sequencer's stage:
 * LOAD_HIT a hit, LOAD_NOTE a note). With one core both halves are on the first. */
static uint32_t st_ms, st_syn, st_drum, st_guard0, st_hi_ms;
static float st_load, st_load_sum;
#define LOAD_IDLE 9.5f
#define LOAD_PLAY 1.5f
#define LOAD_UNIT 1.5f                                      /* a cost unit, % of a block: an ordinary voice is 4 (the
                                                             * FM-1, tools/preset_prof.py 2026-10-19: 1.36 x the 1.1 of
                                                             * 2026-10-18's engines) */
#define LOAD_SYNCFX 3.0f                                   /* the lofi or the phaser, in time on the first core */
#define LOAD_DRUM_VOICE 2
#define LOAD_DRUM_TRACK 1
#define LOAD_HIT 4.5f
#define LOAD_NOTE 1.5f
#define LOAD_HIT_MS 6                                      /* a block, 5.8 ms */
static float fx_cost(int k)                                /* an effect a block behind, on the second core (%) */
{
    int pick = fx_type[k], type = fx_pick_type(pick);
    if (!fx_lags(FXS(k)))
        return 0.0f;
    if (!fx_awake(FXS(k)))
        return 0.7f;
    return type == FXT_DELAY ? 3.9f : type == FXT_CHORUS ? 3.0f : pick == FXT_REVERB ? 10.4f : 7.5f;
}
static float load_model(int *nsyn, int *ndrum)
{
    static uint32_t hits0, notes0;
    static float hit_ring[LOAD_HIT_MS], hit_pct;   /* each millisecond's hits and notes, the last LOAD_HIT_MS: a hit
                                                     * costs its share for LOAD_HIT_MS ms, then leaves the sum */
    static int hit_at;
    int k, v0 = 0, v1 = 0, n = 0, t;
    float serial = LOAD_IDLE + (seq.playing ? LOAD_PLAY : 0.0f), c0, c1, fx_left;
    *ndrum = 0;
    for (int i = 0; i < NVOICE; i++)
        n += synth_sounding(SY, i, i + 1);
    for (t = 0; t < NTRK; t++) {
        int nv = eng_drum[t] && dl_slot[t] != 0xFFu ? drumline_voices(DL(t)) : 0;
        if (nv) {
            v0 += LOAD_DRUM_TRACK + LOAD_DRUM_VOICE * nv;
            *ndrum += nv;
        }
    }
    {   /* hits and notes since the last millisecond */
        float now = LOAD_HIT * (float)(host_drum_hits - hits0) + LOAD_NOTE * (float)(host_note_ons - notes0);
        hit_pct += now - hit_ring[hit_at];
        hit_ring[hit_at] = now;
        hit_at = (hit_at + 1) % LOAD_HIT_MS;
        if (hit_pct < 0.0f)
            hit_pct = 0.0f;
        hits0 = host_drum_hits;
        notes0 = host_note_ons;
    }
    {   /* the effects' job runs on the second core from the block's start, beside the sequencer: what is left of it
         * when the voices start shares the second core with them (engine.c render_sub) */
        float seq_part = (seq.playing ? LOAD_PLAY : 0.0f) + hit_pct;
        fx_left = fx_cost(0) + fx_cost(1) - seq_part;
        if (fx_left < 0.0f)
            fx_left = 0.0f;
    }
    k = (int)split_at((int)(fx_left / LOAD_UNIT + 0.5f), NVOICE);
    for (int i = 0; i < NVOICE; i++)
        if (synth_sounding(SY, i, i + 1))
            *(i < k ? &v1 : &v0) += synth_voice_cost(SY, i);
    serial += hit_pct;
    *nsyn = n;
    for (t = 0; t < 2; t++)
        if (!fx_lags(FXS(t)) && fx_awake(FXS(t)))
            serial += LOAD_SYNCFX;
    c0 = LOAD_UNIT * (float)v0;
    c1 = LOAD_UNIT * (float)v1 + fx_left;
    return serial + (engine_core2_state() == 1 ? (c0 > c1 ? c0 : c1) : c0 + c1);
}
/* the model's load over MS milliseconds of the device, its mean and peak (the "meter" command; expect model_avg,
 * model_peak) */
static void meter_run(uint32_t ms)
{
    double sum = 0.0;
    meter_peak = 0.0f;
    for (uint32_t i = 0; i < ms; i++) {
        float l;
        run_ms(1);
        l = host_load;
        sum += l;
        if (l > meter_peak)
            meter_peak = l;
    }
    meter_avg = ms ? (float)(sum / ms) : 0.0f;
    printf("meter: %u ms, model load mean %.1f %%, peak %.1f %%, voices %d\n", ms, meter_avg, meter_peak, eng_voices);
}
void app_host_ms(void)
{
    int ns, nd;
    float l = host_load = load_model(&ns, &nd);      /* (every millisecond: the model keeps its hits' time) */
    if (!seq.playing)
        return;
    st_ms++;
    st_load_sum += l;
    if (l > st_load)
        st_load = l;
    if (l > 85.0f)
        st_hi_ms++;
    if ((uint32_t)ns > st_syn)
        st_syn = (uint32_t)ns;
    if ((uint32_t)nd > st_drum)
        st_drum = (uint32_t)nd;
}
static void stats_print(void)
{
    printf("stats: %.1f s playing, peak synth voices %u (of %d), peak drum voices %u, guard trips %u; "
           "model load peak %.0f %%, mean %.0f %%, over 85 %% for %u ms\n", st_ms / 1000.0, st_syn, NVOICE, st_drum,
           (unsigned)(eng_guard_count - st_guard0), st_load, st_ms ? st_load_sum / (float)st_ms : 0.0f, st_hi_ms);
}

/* a master value as a script writes it: a number, or for pumpby kick / t1..t8 / metro, for accent on / off */
static int mst_value(int k, const char *v)
{
    if (k == MS_PUMP_BY) {
        if (ieq(v, "kick"))
            return PB_KICK;
        if (ieq(v, "metro") || ieq(v, "metronome"))
            return PB_METRO;
        if ((v[0] == 't' || v[0] == 'T') && v[1] >= '1' && v[1] <= '8' && !v[2])
            return PB_T1 + v[1] - '1';
    }
    if (k == MS_PUMP_ACC && (ieq(v, "on") || ieq(v, "off")))
        return ieq(v, "on");
    if (!(v[0] >= '0' && v[0] <= '9'))
        host_die("not a master value", v);
    return clampi(atoi(v), 0, master_max(k));
}
static void host_env(void)                          /* LOOM_SOLO, LOOM_MST (once) */
{
    static int done;
    const char *e;
    if (done)
        return;
    done = 1;
    if ((e = getenv("LOOM_SOLO")) && *e) {
        char b[128], *p;
        uint16_t m = 0;
        snprintf(b, sizeof b, "%s", e);
        for (p = strtok(b, ", "); p; p = strtok(0, ", "))
            m |= (uint16_t)(!strcmp(p, "fx1") ? 1u << 8 : !strcmp(p, "fx2") ? 1u << 9
                            : atoi(p) >= 1 && atoi(p) <= 8 ? 1u << (atoi(p) - 1) : 0u);
        synth_host_solo = m;
    }
    if ((e = getenv("LOOM_LOAD")) && *e)            /* the guard's input fixed (a script's "load N" still wins) */
        load_force = atoi(e);
    if ((e = getenv("LOOM_MST")) && *e)
        fx_host_stage = !strcmp(e, "raw") ? 0 : !strcmp(e, "eq") ? 1 : !strcmp(e, "comp") ? 3 : 9;
}
static void mst_override(void)                      /* LOOM_MSTSET: knobs held where it says */
{
    const char *e = getenv("LOOM_MSTSET");
    char b[256], *p;
    if (!e || !*e)
        return;
    snprintf(b, sizeof b, "%s", e);
    for (p = strtok(b, ","); p; p = strtok(0, ",")) {
        char *eq = strchr(p, '=');
        int k;
        if (!eq)
            continue;
        *eq = 0;
        for (k = 0; k < NMS && !ieq(p, MN[k]); k++)
            ;
        if (k < NMS) {
            proj.master[k] = (uint8_t)mst_value(k, eq + 1);
            engine_master(k, proj.master[k]);
        }
    }
}

static int song_cmd(const char *cmd)
{
    char buf[512], *w[80];
    int n = words(buf, w, 80), t, k;
    host_env();
    if (!strcmp(cmd, "knob")) {                     /* knob T PARAM VALUE */
        if (n < 4)
            host_die("knob T PARAM VALUE", "");
        t = atoi(w[1]) - 1;
        if (t < 0 || t >= NALLTRK)
            host_die("no track", w[1]);
        k = param_of(w[2]);
        if (t < NTRK && proj.trk[t].par[TP_ENGINE] == EG_DRUMLINE && (k == TP_SEND1 || k == TP_SEND2)) {
            int v = value_of(t, k, w[3]);         /* a kit's send1 / send2: every key's (it has no track sends) */
            for (int d = 0; d < DL_NSOUND; d++)
                set_tp(t, TP_DRUM(d, k == TP_SEND1 ? DP_SEND1 : DP_SEND2), v);
        } else
            set_tp(t, k, value_of(t, k, w[3]));
        run_ms(1);
    } else if (!strcmp(cmd, "preset")) {            /* preset T NAME [ENGINE] (NAME: _ for spaces) */
        int pick = -1;
        t = atoi(w[1]) - 1;
        if (n < 3 || t < 0 || t >= NTRK)
            host_die("preset T NAME", "");
        for (k = 0; k < NPRESETS && pick < 0; k++)
            if (ieq(w[2], PRESETS[k].name))
                pick = k;
        for (k = 0; k < NPRESETS && pick < 0; k++) {
            char nm[32];
            snprintf(nm, sizeof nm, "%s", w[2]);
            for (char *q = nm; *q; q++)
                if (*q == '_')
                    *q = ' ';
            if (strstr(PRESETS[k].name, nm))
                pick = k;
        }
        if (pick < 0 && n > 3) {
            for (k = 0; k < NENGINE; k++)
                if (ieq(w[3], EG_NAME[k]))
                    pick = preset_first(k);
            if (pick >= 0)
                printf("demo: no preset \"%s\": %s's first, \"%s\"\n", w[2], w[3], PRESETS[pick].name);
        }
        if (pick < 0)
            host_die("no preset", w[2]);
        load_preset(t, &PRESETS[pick]);
        run_ms(1);
    } else if (!strcmp(cmd, "pat")) {               /* pat T PAT LEN [SCALE [NLEN [GROOVE [SHAPE]]]] */
        static const char *const SC[NTS] = {"h", "1", "2", "3", "4", "5", "6", "8", "16"};
        tpat_t *tp = pat_or_die(w[1], w[2]);
        if (n < 4)
            host_die("pat T PAT LEN ...", "");
        tp->len = (uint8_t)(atoi(w[3]) < 1 ? 1 : atoi(w[3]) > NSTEP ? NSTEP : atoi(w[3]));
        if (n > 4) {
            for (k = 0; k < NTS && strcmp(w[4], SC[k]); k++)
                ;
            if (k == NTS)
                host_die("no scale", w[4]);
            tp->scale = (uint8_t)k;
        }
        if (n > 5)
            tp->nlen = (uint8_t)atoi(w[5]);
        if (n > 6)
            tp->groove = (int8_t)atoi(w[6]);
        if (n > 7)
            tp->shape = (uint8_t)atoi(w[7]);
    } else if (!strcmp(cmd, "hits")) {              /* hits T PAT SOUND STRING... */
        tpat_t *tp = pat_or_die(w[1], w[2]);
        int snd = atoi(w[3]), s = 0;
        if (n < 5 || snd < 1 || snd > NDRUM)
            host_die("hits T PAT SOUND STRING", "");
        for (int i = 4; i < n; i++)
            for (const char *q = w[i]; *q; q++) {
                int vel = *q == 'x' ? 100 : *q == 'X' ? 127 : *q == 'o' ? 80 : *q == 'g' ? 45
                        : (*q >= '1' && *q <= '9') ? (*q - '0') * 14 : 0;
                step_t *st;
                if (*q == '|')
                    continue;
                if (s >= NSTEP)
                    host_die("more than 64 steps", w[i]);
                st = &tp->st[s++];
                if (!vel)
                    continue;
                kit_add(st, snd);
                if (vel > st->vel)
                    st->vel = (uint8_t)vel;
            }
    } else if (!strcmp(cmd, "notes")) {             /* notes T PAT S LEN VEL N1 [N2 [N3 [N4]]] */
        tpat_t *tp = pat_or_die(w[1], w[2]);
        step_t *st = step_or_die(tp, w[3]);
        uint8_t nn[SN];
        int m = 0;
        if (n < 7)
            host_die("notes T PAT S LEN VEL N1 ..", "");
        for (int i = 6; i < n && m < SN; i++)
            nn[m++] = (uint8_t)note_of(w[i]);
        step_set_notes(st, nn, m, atoi(w[5]));
        k = (int)(atof(w[4]) * 4.0 + 0.5);
        st->len = (uint8_t)(k > 0 ? step_len_code(k) : 0);
    } else if (!strcmp(cmd, "trick")) {             /* trick T PAT S NAME VAL[:MODE] */
        static const char *const TN[NSC] = {"none", "pulse", "hold", "multiply", "velocity", "rampup", "rampdown",
                                            "random", "porta", "bend", "tonality", "jump", "skiplock", "skiptrick",
                                            "skip"};
        step_t *st = step_or_die(pat_or_die(w[1], w[2]), w[3]);
        int v;
        if (n < 6)
            host_die("trick T PAT S NAME VAL", "");
        for (k = 1; k < NSC && !ieq(w[4], TN[k]); k++)
            ;
        if (k == NSC)
            host_die("no trick", w[4]);
        v = atoi(w[5]) - 1;
        if (v < 0 || v > SC_RANDOM_VAL)
            host_die("a trick's value is 1..10", w[5]);
        if (k >= SC_SKIP_LOCK)
            v = SK_VAL(v, strchr(w[5], ':') ? atoi(strchr(w[5], ':') + 1) : 0);
        if (!st->comp || st->comp == k) {
            st->comp = (uint8_t)k;
            st->cval = (uint8_t)v;
        } else {
            st->comp2 = (uint8_t)k;
            st->cval2 = (uint8_t)v;
        }
    } else if (!strcmp(cmd, "weaveset")) {          /* weaveset T FROM PAT RULE: track T weaves with track FROM's PAT */
        static const char *const RN2[NWV] = {"bars", "steps", "rhythm", "notes"};
        int t = atoi(w[1]) - 1, r = -1;
        for (k = 0; k < NWV; k++)
            if (n > 4 && ieq(w[4], RN2[k]))
                r = k;
        if (n < 5 || t < 0 || t >= NSEQT || r < 0)
            host_die("weaveset T FROM PAT bars|steps|rhythm|notes", "");
        weave_set(t, atoi(w[2]), atoi(w[3]) - 1, r);
    } else if (!strcmp(cmd, "wvpat")) {             /* wvpat PAT LEN: the WEAVE track's pattern, emptied, LEN sixteenths */
        int p = atoi(w[1]) - 1, l = n > 2 ? atoi(w[2]) : 16;
        if (p < 0 || p >= NPATT || (l != 16 && l != 32 && l != 48 && l != 64))
            host_die("wvpat PAT 16|32|48|64", "");
        memset(&pats.weave[p], 0, sizeof pats.weave[p]);
        pats.weave[p].len = (uint8_t)l;
    } else if (!strcmp(cmd, "wvstep")) {            /* wvstep PAT S TRACKS LEN: a WEAVE step, as REC writes it (TRACKS
                                                     * 1-8, c = COND, t = THROW, by commas; LEN sixteenths) */
        int p = atoi(w[1]) - 1, s = atoi(w[2]) - 1;
        uint32_t m = 0;
        char b[32], *q;
        if (n < 5 || p < 0 || p >= NPATT || s < 0 || s >= NSTEP || atoi(w[4]) < 1 || atoi(w[4]) > NSTEP)
            host_die("wvstep PAT S TRACKS LEN", "");
        snprintf(b, sizeof b, "%s", w[3]);
        for (q = strtok(b, ","); q; q = strtok(0, ","))
            m |= *q == 'c' ? 1u << AUX_BRAIN : *q == 't' ? 1u << AUX_PUNCH
                 : atoi(q) >= 1 && atoi(q) <= NTRK ? 1u << (atoi(q) - 1) : 0u;
        if (!m)
            host_die("no track to weave", w[3]);
        pats.weave[p].st[s].m0 = (uint8_t)m;
        pats.weave[p].st[s].m1 = (uint8_t)(m >> 8);
        pats.weave[p].st[s].len = (uint8_t)atoi(w[4]);
    } else if (!strcmp(cmd, "sceneweave")) {        /* sceneweave N PAT: scene N plays the WEAVE track's PAT */
        int sc = atoi(w[1]) - 1;
        if (sc < 0 || sc >= NSCENE || n < 3 || atoi(w[2]) < 1 || atoi(w[2]) > NPATT)
            host_die("sceneweave N PAT", "");
        pats.scene[sc].weave = (uint8_t)(atoi(w[2]) - 1);
    } else if (!strcmp(cmd, "weave")) {             /* weave T DST A B RULE: pattern DST = A woven with B */
        static const char *const RN[NWV] = {"bars", "steps", "rhythm", "notes"};
        static tpat_t out;
        tpat_t *d = pat_or_die(w[1], w[2]), *a = pat_or_die(w[1], w[3]), *b = pat_or_die(w[1], w[4]);
        int r = -1;
        for (k = 0; k < NWV; k++)
            if (n > 5 && ieq(w[5], RN[k]))
                r = k;
        if (r < 0)
            host_die("weave T DST A B bars|steps|rhythm|notes", "");
        {
            int tk = atoi(w[1]) - 1, kd = tk == AUX_PUNCH ? WK_FX : host_kit(tk) ? WK_KIT : WK_MEL;
            pattern_woven(&out, a, kd, b, kd, r);
        }
        *d = out;
    } else if (!strcmp(cmd, "chance")) {            /* chance T PAT S PCT|A:B */
        step_t *st = step_or_die(pat_or_die(w[1], w[2]), w[3]);
        if (n < 5)
            host_die("chance T PAT S PCT|A:B", "");
        if (strchr(w[4], ':')) {
            int A = atoi(w[4]), B = atoi(strchr(w[4], ':') + 1), a2, b2;
            for (k = 0; k < COND_NAB; k++) {
                cond_ab(k, &a2, &b2);
                if (a2 == A && b2 == B)
                    break;
            }
            if (k == COND_NAB)
                host_die("no cycle", w[4]);
            st->cond = (uint8_t)(COND_AB + k);
        } else
            st->cond = (uint8_t)atoi(w[4]);
    } else if (!strcmp(cmd, "nudge")) {
        step_t *st = step_or_die(pat_or_die(w[1], w[2]), w[3]);
        st->nudge = (int8_t)atoi(w[4]);
    } else if (!strcmp(cmd, "lock")) {              /* lock T PAT S PARAM VALUE */
        int p, l, s = atoi(w[3]) - 1;
        if (n < 6 || s < 0 || s >= NSTEP)
            host_die("lock T PAT S PARAM VALUE", "");
        pat_or_die(w[1], w[2]);
        t = atoi(w[1]) - 1;
        p = param_of(w[4]);
        if (p < 0 || p >= NTP)
            host_die("no parameter", w[4]);
        l = lane_find(pats.lane, t, atoi(w[2]) - 1, p);
        if (l < 0)
            l = lane_alloc(pats.lane, t, atoi(w[2]) - 1, p);
        if (l < 0)
            host_die("the lanes are full", "");
        pats.lane[l].val[s] = (uint8_t)value_of(t, p, w[5]);
    } else if (!strcmp(cmd, "scenedef")) {          /* scenedef N P1 .. P10 */
        int sc = atoi(w[1]) - 1;
        if (sc < 0 || sc >= NSCENE)
            host_die("no scene", w[1]);
        for (t = 0; t < NSEQT; t++)
            pats.scene[sc].pat[t] = (uint8_t)(n > 2 + t && atoi(w[2 + t]) > 0 ? (atoi(w[2 + t]) - 1) % NPATT : 0);
        pats.scene[sc].used = 1;
        pats.scene[sc].brain_key = seq.brain_key;
        pats.scene[sc].brain_scale = seq.brain_scale;
    } else if (!strcmp(cmd, "scenemute")) {         /* scenemute N MASK */
        int sc = atoi(w[1]) - 1;
        uint16_t m = 0;
        if (sc < 0 || sc >= NSCENE || n < 3)
            host_die("scenemute N MASK", "");
        for (t = 0; t < NSEQT && w[2][t]; t++)
            if (w[2][t] == 'x')
                m |= (uint16_t)(1u << t);
        pats.scene[sc].mute = m;
    } else if (!strcmp(cmd, "song")) {              /* song S:R ... */
        pats.song.len = 0;
        for (int i = 1; i < n && pats.song.len < NSONG; i++) {
            int sc = atoi(w[i]) - 1, r = strchr(w[i], ':') ? atoi(strchr(w[i], ':') + 1) : 1;
            if (sc < 0 || sc >= NSCENE || r < 1 || r > SONG_BARS)
                host_die("a song slot is S:R (scene 1..16, 1..64 bars)", w[i]);
            pats.song.slot[pats.song.len].scene = (uint8_t)sc;
            pats.song.slot[pats.song.len++].rep = (uint8_t)r;
        }
    } else if (!strcmp(cmd, "songloop"))
        pats.song.loop = (uint8_t)(atoi(w[1]) != 0);
    else if (!strcmp(cmd, "mst")) {                 /* mst NAME VALUE (MN: the knobs, then pumpby, release, accent,
                                                     * level: the out page's list) */
        for (k = 0; k < NMS && !ieq(w[1], MN[k]); k++)
            ;
        if (k == NMS || n < 3)
            host_die("no master knob", w[1]);
        proj.master[k] = (uint8_t)mst_value(k, w[2]);
        engine_master(k, proj.master[k]);
        mst_override();
        run_ms(1);
    } else if (!strcmp(cmd, "groove")) {
        for (k = 0; k < NGROOVE && !ieq(w[1], GR_NAME[k]); k++)
            ;
        if (k == NGROOVE)
            host_die("no groove", w[1]);
        set_groove_type(k);
    } else if (!strcmp(cmd, "play")) {              /* play T NOTE on|off [VEL] */
        if (n < 4)
            host_die("play T NOTE on|off [VEL]", "");
        engine_key(atoi(w[1]) - 1, note_of(w[2]), !strcmp(w[3], "on"), n > 4 ? atoi(w[4]) : 100);
        run_ms(1);
    } else if (!strcmp(cmd, "throw")) {             /* throw NAME on|off perc|mel [T]: HOME + its key */
        int fx;
        for (k = 0; k < NPUNCH && !ieq(w[1], PUNCH_NAME[k]); k++)
            ;
        if (k == NPUNCH || n < 4)
            host_die("throw NAME on|off perc|mel [T]", w[1]);
        fx = k + (!strcmp(w[3], "mel") ? PX_MEL : 0);
        engine_punch(fx, !strcmp(w[2], "on"), n > 4 ? atoi(w[4]) - 1 : -1);
        run_ms(1);
    } else if (!strcmp(cmd, "bars")) {              /* on the sequencer's sixteenths while it plays: no drift */
        static uint32_t mark;
        uint32_t six = (uint32_t)(atof(w[1]) * 16.0 + 0.5), guard = 0;
        if (!seq.playing) {
            run_ms((uint32_t)(atof(w[1]) * 240000.0 / seq_tempo(&seq) + 0.5));
            mark = 0;
        } else {
            if (seq.step_tick + 1u < mark)          /* (started again) */
                mark = 0;
            mark += six;
            while (seq.playing && seq.step_tick < mark && guard++ < 600000u)
                run_ms(1);
        }
    }
    else if (!strcmp(cmd, "stats")) {
        if (n > 1 && !strcmp(w[1], "reset")) {     /* (quietly) */
            st_ms = st_syn = st_drum = st_hi_ms = 0;
            st_load = st_load_sum = 0.0f;
            st_guard0 = eng_guard_count;
            mst_override();
        } else
            stats_print();
    } else
        return 0;
    return 1;
}

int app_host_cmd(const char *cmd, const char *a, const char *b, const char *c, const char *d)
{
    if (song_cmd(cmd))
        return 1;
    if (!strcmp(cmd, "expect"))
        return expect_more(a, b, c, d);
    if (!strcmp(cmd, "param")) {                    /* param T P V */
        int t = atoi(a) - 1, p = param_of(b), v = atoi(c);
        if (t < 0 || t >= NALLTRK || p < 0 || p >= NTP) {
            printf("no param %s %s\n", a, b);
            exit(2);
        }
        proj.trk[t].par[p] = (uint8_t)v;
        engine_set(t, p, v);
        run_ms(1);
    } else if (!strcmp(cmd, "samples")) {           /* samples FILE: the user bank as uploaded */
        static uint8_t img[SMP_BANK_SIZE + 1];
        FILE *bf = fopen(a, "rb");
        size_t nb;
        uint32_t v;
        if (!bf) {
            printf("no bank %s\n", a);
            exit(2);
        }
        memset(img, 0xFF, sizeof img);
        nb = fread(img, 1, sizeof img, bf);
        fclose(bf);
        if (nb > SMP_BANK_SIZE) {
            printf("bank %s: larger than the bank's windows (%u B)\n", a, (unsigned)SMP_BANK_SIZE);
            exit(2);
        }
        smp_user_drop();
        for (v = 0; v < SMP_BANK_SIZE; v += 0x1000u) {   /* as written: each sector erased, then the image's */
            uint32_t off;
            smp_reg_of(v, &off);
            plat_data_erase(off);
            plat_data_write(off, img + v, 0x1000u);
        }
        printf("samples: %s, %d B, %d user samples\n", a, (int)nb, smp_user_scan());
        run_ms(1);
    } else if (!strcmp(cmd, "readback")) {          /* readback IMG: the bank read back through SMP_READ (as the samples
                                                     * page reads it), the header's used part and the data, against IMG */
        static uint8_t img[SMP_BANK_SIZE], got[SMP_BANK_SIZE];
        FILE *rf = fopen(a, "rb");
        uint32_t nb, off, bad = 0, i;
        if (!rf) {
            printf("no image %s\n", a);
            exit(2);
        }
        nb = (uint32_t)fread(img, 1, sizeof img, rf);
        fclose(rf);
        for (off = 0; off < nb; off += SB_READ_MAX) {
            uint32_t want = nb - off < SB_READ_MAX ? nb - off : SB_READ_MAX;
            uint8_t fr[10] = {SB_HDR0, SB_HDR1, SB_HDR2, SB_READ, 0, (uint8_t)(off & 127u), (uint8_t)(off >> 7 & 127u),
                              (uint8_t)(off >> 14 & 127u), (uint8_t)(want & 127u), (uint8_t)(want >> 7)};
            if (!smp_frame(fr, sizeof fr) || sb_out[4] != SB_READ || sb_out[9]) {
                printf("readback: read at %u not answered or failed\n", (unsigned)off);
                exit(2);
            }
            if (sb_unpack7(sb_out + 10, sb_n - 11u, got + off, want) != want) {
                printf("readback: read at %u short\n", (unsigned)off);
                exit(2);
            }
        }
        for (i = 0; i < nb; i++)                     /* (the header past its entries: zeros in the image, 0xFF or old
                                                      * bytes in flash, never read by the page) */
            if (got[i] != img[i] && !(i >= SMP_ENT0 + (uint32_t)img[6] * SMP_ENT && i < SMP_BANK_HDR))
                bad++;
        if (bad) {
            printf("FAIL readback %s: %u of %u bytes differ\n", a, (unsigned)bad, (unsigned)nb);
            host_fails++;
        } else
            printf("  ok readback %s: %u bytes\n", a, (unsigned)nb);
    } else if (!strcmp(cmd, "upload")) {            /* upload FILE: the SysEx frames of an upload (tools/loom_samples.py
                                                     * syx), each through samples.c as from USB, its answer checked */
        static uint8_t fr[2048];
        FILE *uf = fopen(a, "rb");
        int c, nf = 0;
        uint32_t n = 0;
        if (!uf) {
            printf("no frames %s\n", a);
            exit(2);
        }
        while ((c = fgetc(uf)) != EOF) {
            if (c == 0xF0) {
                n = 0;
                continue;
            }
            if (c != 0xF7) {
                if (n < sizeof fr)
                    fr[n++] = (uint8_t)c;
                continue;
            }
            if (!smp_frame(fr, n) || sb_n < 7u || sb_out[4] != fr[3]) {
                printf("upload: frame %d (command %d) not answered\n", nf, n > 3 ? fr[3] : -1);
                exit(2);
            }
            if (fr[3] >= SB_BEGIN && fr[3] <= SB_HDR && fr[3] != SB_SINFO && sb_out[sb_n - 2u]) {
                printf("upload: frame %d (command %d) failed, rc %d\n", nf, fr[3], sb_out[sb_n - 2u]);
                exit(2);
            }
            if (fr[3] == SB_SINFO) {                /* the info's answer: size, used, count, names, rates, lengths */
                uint32_t i = 13, k;
                printf("info: version %d, %d KB, %d KB used, %d samples from %d:", sb_out[5], sb_out[6] | sb_out[7] << 7,
                       sb_out[8] | sb_out[9] << 7, sb_out[10], sb_out[11]);
                for (k = 0; k < sb_out[12] && i < sb_n; k++) {
                    printf(" %s", (const char *)sb_out + i);
                    i += (uint32_t)strlen((const char *)sb_out + i) + 1u;
                    printf(" %u Hz %u", (unsigned)(sb_out[i] | sb_out[i + 1] << 7 | sb_out[i + 2] << 14),
                           (unsigned)(sb_out[i + 3] | sb_out[i + 4] << 7 | sb_out[i + 5] << 14));
                    i += 6;
                }
                printf("\n");
            }
            nf++;
        }
        fclose(uf);
        printf("upload: %s, %d frames, %d user samples, %u B\n", a, nf, smp_user_count(), (unsigned)smp_user_bytes());
        run_ms(1);
    } else if (!strcmp(cmd, "meter"))               /* meter MS: the model's load over MS ms */
        meter_run((uint32_t)atoi(a));
    else if (!strcmp(cmd, "load"))
        load_force = atoi(a);
    else if (!strcmp(cmd, "steps"))
        steps_print(atoi(a), atoi(b));
    else if (!strcmp(cmd, "weavesteps")) {          /* weavesteps PAT: the WEAVE track's pattern: each step's tracks, length */
        const wvpat_t *wp = &pats.weave[(atoi(a) - 1) & (NPATT - 1)];
        printf("  weave pattern %d: %d sixteenths\n", atoi(a), wp->len);
        for (int s2 = 0; s2 < NSTEP; s2++)
            if (wv_mask(&wp->st[s2]))
                printf("    %2d: tracks 0x%03x len %d\n", s2 + 1, (unsigned)wv_mask(&wp->st[s2]), wp->st[s2].len);
    }
    else if (!strcmp(cmd, "step")) {                /* step T PAT S NOTE [VEL] */
        char e[16] = {0};
        tpat_t *tp = pat_of(atoi(a), atoi(b));
        int s = atoi(c) - 1, note = atoi(d), vel;
        sscanf(plat_host_line, "%*s %*s %*s %*s %*s %15s", e);
        vel = e[0] ? atoi(e) : 100;
        if (!tp || s < 0 || s >= NSTEP) {
            printf("no step %s %s %s\n", a, b, c);
            exit(2);
        }
        if (note <= 0) {
            memset(&tp->st[s], 0, sizeof tp->st[s]);
        } else {
            uint8_t n1 = (uint8_t)note;
            if (host_kit(atoi(a) - 1)) {             /* a kit track: the sound note on the step, alone */
                kit_set(&tp->st[s], 0);
                kit_add(&tp->st[s], note);
                tp->st[s].vel = (uint8_t)vel;
            } else
                step_set_notes(&tp->st[s], &n1, 1, vel);
        }
    } else if (!strcmp(cmd, "chord")) {             /* chord T PAT S LEN N1 N2 [N3 [N4]]: notes on one step */
        char w[4][16] = {{0}};
        tpat_t *tp = pat_of(atoi(a), atoi(b));
        int s = atoi(c) - 1, n = 0;
        uint8_t notes[SN];
        sscanf(plat_host_line, "%*s %*s %*s %*s %*s %15s %15s %15s %15s", w[0], w[1], w[2], w[3]);
        for (int i = 0; i < SN; i++)
            if (w[i][0] && atoi(w[i]) > 0)
                notes[n++] = (uint8_t)atoi(w[i]);
        if (!tp || s < 0 || s >= NSTEP || !n) {
            printf("no step %s %s %s\n", a, b, c);
            exit(2);
        }
        step_set_notes(&tp->st[s], notes, n, 90);
        tp->st[s].len = (uint8_t)atoi(d);
    } else if (!strcmp(cmd, "scene"))
        engine_scene((atoi(a) - 1) & (NSCENE - 1), 0);
    else if (!strcmp(cmd, "punch")) {               /* punch KEY on|off: through the key's punch bit, on the group */
        int fx = punch_of_key(key_of(a));
        if (fx < 0) {
            printf("key %s has no punch-in fx\n", a);
            exit(2);
        }
        engine_punch(fx, !strcmp(b, "on"), -1);
        run_ms(1);
    } else if (!strcmp(cmd, "brainkey")) {          /* brainkey NOTE on|off */
        engine_brain_key(atoi(a), !strcmp(b, "on"));
        run_ms(1);
    } else if (!strcmp(cmd, "bpm"))
        set_tempo_x10((int)(atof(a) * 10.0 + 0.5));     /* the project's tempo (saved with it), as SELECT turns it */
    else if (!strcmp(cmd, "projnew"))              /* as Setup's NEW PROJECT */
        do_new_proj();
    else if (!strcmp(cmd, "upgrade9")) {           /* upgrade9 SLOT: a format-9 project (before the kit's keys had their
                                                    * own sends) in slot 1..3 made format 10 and saved there: each key's
                                                    * values in their new places, the kit's track sends on every key,
                                                    * the lanes' drum parameters and tune renumbered (a one-off rescue,
                                                    * build/rescue: restore a backup, upgrade9, backup) */
        enum { O_TRK = offsetof(settings_t, trk), ODP = 18, ONTP = TP_DRUM0 + 11 * ODP, OTRK = ONTP + NAME_LEN + 4 };
        static uint8_t old[4096];
        int slot = a ? atoi(a) - 1 : -1, len, k, p, t, nl = 0;
        if (slot < 0 || slot >= NPROJ)
            host_die("upgrade9 SLOT", "");
        len = plat_store_load(OBJ_PROJ(slot, 0), old, sizeof old);
        if (len != O_TRK + NALLTRK * OTRK || get32(old) != PROJ_MAGIC || get32(old + 4) != 9u)
            host_die("upgrade9: no format-9 project in slot", a);
        proj_slot = (uint8_t)slot;
        slot_objects(slot);
        project_defaults();
        len = plat_store_load(stream_obj[0], io_buf, sizeof io_buf);
        if (len < 4 || get32(io_buf) != PROJ_STREAM || load_stream(len, 0))
            host_die("upgrade9: its patterns do not load", a);
        for (k = 0; k < NLANE; k++) {               /* (before the clamp: an old index's range is another's now) */
            lane_t *l = &pats.lane[k];
            if (!l->used)
                continue;
            if (l->param == 0xFEu)                  /* format 9's LANE_TUNE */
                l->param = LANE_TUNE;
            else if (l->param >= TP_DRUM0)
                l->param = (uint16_t)TP_DRUM((l->param - TP_DRUM0) / ODP, (l->param - TP_DRUM0) % ODP);
            nl++;
        }
        patterns_clamp();
        memcpy(&proj, old, O_TRK);
        proj.format = PROJ_FORMAT;
        for (t = 0; t < NALLTRK; t++) {
            const uint8_t *o = old + O_TRK + t * OTRK;
            trk_t *nt = &proj.trk[t];
            int drum = t < NTRK && o[TP_ENGINE] == EG_DRUMLINE;
            memcpy(nt->par, o, TP_DRUM0);
            for (k = 0; k < DL_NSOUND; k++) {
                for (p = 0; p < ODP; p++)
                    nt->par[TP_DRUM(k, p)] = o[TP_DRUM0 + k * ODP + p];
                nt->par[TP_DRUM(k, DP_SEND1)] = drum ? o[TP_SEND1] : DL_DEFAULT_KIT[k][DP_SEND1];
                nt->par[TP_DRUM(k, DP_SEND2)] = drum ? o[TP_SEND2] : DL_DEFAULT_KIT[k][DP_SEND2];
            }
            memcpy(nt->name, o + ONTP, NAME_LEN);
            memcpy(nt->rsv, o + ONTP + NAME_LEN, 4);
        }
        if (!settings_valid(&proj))
            host_die("upgrade9: the settings do not check out", a);
        project_apply();
        if (project_save(slot))
            host_die("upgrade9: not saved", a);
        printf("upgrade9: slot %d made format %u (%d lanes)\n", slot + 1, PROJ_FORMAT, nl);
    }
    else if (!strcmp(cmd, "projsave")) {           /* projsave SLOT [NAME]: saved into slot 1..3 (as SAVE there) */
        int slot = atoi(a) - 1;
        if (slot < 0 || slot >= NPROJ) {
            printf("projsave: no slot %s\n", a);
            exit(2);
        }
        if (*b) {
            memset(proj.name, 0, sizeof proj.name);
            for (int i = 0; b[i] && i < NAME_LEN; i++)
                proj.name[i] = b[i] == '_' ? ' ' : b[i];
        }
        if (save_project(slot) != 0) {
            printf("projsave: slot %s not saved\n", a);
            exit(2);
        }
        printf("projsave: slot %d \"%.*s\", %d %% of the slot\n", slot + 1, NAME_LEN, proj.name, project_mem_pct());
    }
    else if (!strcmp(cmd, "cores"))
        plat_cpu2_set(atoi(a));
    else
        return 0;
    return 1;
}

void app_speak_dump(void) { speak_dump(); }
void app_host_reboot(void) { memset(&proj, 0, sizeof proj); }   /* what RAM forgets (the flash keeps the rest) */
int app_host_fails(void)
{
#ifdef GFX_AUDIT
    if (ga_findings)                                /* the layout audit (gfx_loom.c): labels must not clip, touch or overlap */
        printf("LAYOUT: %u findings\n", (unsigned)ga_findings);
    return host_fails + (ga_findings != 0);
#else
    return host_fails;
#endif
}
#ifdef GFX_AUDIT
static const char *gfx_audit_where(void)          /* the layout audit's "where": the page and the track */
{
    static char w[64];
    snprintf(w, sizeof w, "%s, track %d", ui_page_name(), (int)proj.trk_sel + 1);
    return w;
}
#endif
