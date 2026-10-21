/* SPDX-License-Identifier: GPL-3.0-only */
/* LOOM-8 sequencer tests: a recording sink logs every event with its sample time; each test builds a
 * pattern, runs the sequencer the way the engine does (seq_until_event / seq_advance) and checks the
 * log. Prints "ok name" per test; exits 1 on the first failure. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "seq.h"

enum { E_ON, E_OFF, E_DRUM, E_LOCK, E_BEND, E_PUNCH, E_MIDI, E_STEP, E_SCENE, E_METRO, E_PAN, E_DOFF, E_GHOST };
typedef struct {
    int kind;
    uint32_t t;
    int trk, a, b, c;
    uint32_t tag;
} ev_t;
#define NEV 20000
static ev_t ev[NEV];
static int nev;
static uint32_t now;
static patterns_t P;
static uint8_t par[NALLTRK][NTP];
static seq_t S;
static const char *cur = "";

static void log_ev(int kind, int trk, int a, int b, int c, uint32_t tag)
{
    if (nev < NEV) {
        ev_t *e = &ev[nev++];
        e->kind = kind;
        e->t = now;
        e->trk = trk;
        e->a = a;
        e->b = b;
        e->c = c;
        e->tag = tag;
    }
}
static void s_on(void *x, int trk, int note, int vel, int glide, uint32_t tag) { (void)x; log_ev(E_ON, trk, note, vel, glide, tag); }
static void s_off(void *x, int trk, uint32_t tag) { (void)x; log_ev(E_OFF, trk, 0, 0, 0, tag); }
static void s_drum(void *x, int trk, int sound, int vel, int pitch, int sh, int lg)
{
    (void)x;
    log_ev(E_DRUM, trk, sound, vel, pitch, (uint32_t)(sh | lg << 1));
}
static void s_lock(void *x, int trk, int param, int val) { (void)x; log_ev(E_LOCK, trk, param, val, 0, 0); }
static void s_bend(void *x, int trk, int cents) { (void)x; log_ev(E_BEND, trk, cents, 0, 0, 0); }
static void s_punch(void *x, uint32_t d, uint32_t m) { (void)x; log_ev(E_PUNCH, -1, (int)d, (int)m, 0, 0); }
static void s_midi(void *x, uint8_t st, uint8_t d1, uint8_t d2) { (void)x; log_ev(E_MIDI, -1, st, d1, d2, 0); }
static void s_step(void *x, int trk, int step) { (void)x; log_ev(E_STEP, trk, step, 0, 0, 0); }
static void s_scene(void *x, int sc) { (void)x; log_ev(E_SCENE, -1, sc, 0, 0, 0); }
static void s_metro(void *x, int acc) { (void)x; log_ev(E_METRO, -1, acc, 0, 0, 0); }
static void s_pan(void *x, int trk, int pan) { (void)x; log_ev(E_PAN, trk, pan, 0, 0, 0); }
static void s_drum_off(void *x, int trk, int sound) { (void)x; log_ev(E_DOFF, trk, sound, 0, 0, 0); }
static void s_ghost(void *x, int trk, int sound, int vel) { (void)x; log_ev(E_GHOST, trk, sound, vel, 0, 0); }
static const seq_sink_t sink = {s_on, s_off, s_drum, s_lock, s_bend, s_punch, s_midi, s_step,
                                s_scene, s_metro, s_pan, s_drum_off, s_ghost, 0};

static void fail(const char *what)
{
    int i;
    printf("FAIL %s: %s\n", cur, what);
    for (i = 0; i < nev && i < 400; i++)
        if (ev[i].kind != E_STEP)
        printf("  %6u kind %d trk %d a %d b %d c %d tag %x\n", ev[i].t, ev[i].kind, ev[i].trk, ev[i].a, ev[i].b, ev[i].c,
               ev[i].tag);
    exit(1);
}
#define CHECK(c) do { if (!(c)) fail(#c); } while (0)

static void reset(void)
{
    int t;
    patterns_init(&P);
    memset(par, 0, sizeof par);
    for (t = 0; t < NTRK; t++)
        par[t][TP_ENGINE] = EG_KNIT;
    seq_init(&S, &P, par);
    S.send_clock = 0;
    nev = 0;
    now = 0;
}

static void run(uint32_t n)
{
    while (n) {
        uint32_t k = seq_until_event(&S, n);
        now += k;
        seq_advance(&S, k, &sink);
        n -= k;
    }
}
static void run_to(uint32_t t) { run(t > now ? t - now : 0); }
static void run_blocks(uint32_t n)    /* as engine_render: a block (256) at a time, each split at its events */
{
    while (n) {
        uint32_t b = n < 256u ? n : 256u;
        run(b);
        n -= b;
    }
}
static void start(void)
{
    now = 0;
    seq_start(&S, 1);
    seq_advance(&S, 0, &sink);
}
static void stop(void)
{
    seq_stop(&S);
    seq_advance(&S, 0, &sink);
}
#define L16 5512u                     /* a sixteenth at 120 BPM: 5512.5 samples */
static int near(uint32_t t, uint32_t want, uint32_t tol) { return t + tol >= want && t <= want + tol; }
static step_t *st(int trk, int step) { return &P.pat[trk][0].st[step]; }
static void note(int trk, int step, int n, int vel)   /* (a kit track's: sound n added to the step's) */
{
    if (trk < NTRK && par[trk][TP_ENGINE] == EG_DRUMLINE)
        kit_add(st(trk, step), n);
    else
        st(trk, step)->note[0] = (uint8_t)n;
    st(trk, step)->vel = (uint8_t)vel;
}
static int count(int kind, int trk)
{
    int i, n = 0;
    for (i = 0; i < nev; i++)
        n += ev[i].kind == kind && (trk < 0 || ev[i].trk == trk);
    return n;
}
static ev_t *nth(int kind, int trk, int k)
{
    int i;
    for (i = 0; i < nev; i++)
        if (ev[i].kind == kind && (trk < 0 || ev[i].trk == trk) && k-- == 0)
            return &ev[i];
    return 0;
}
static ev_t *on_n(int k) { return nth(E_ON, 0, k); }
static int midi_n(int status)         /* the MIDI messages with this status byte (0x90 | ch, 0x80 | ch) */
{
    int i, n = 0;
    for (i = 0; i < nev; i++)
        n += ev[i].kind == E_MIDI && ev[i].a == status;
    return n;
}
static ev_t *off_of(const ev_t *on)   /* the off of this note on (the first off with its tag after it) */
{
    int i;
    for (i = (int)(on - ev) + 1; i < nev; i++)
        if (ev[i].kind == E_OFF && ev[i].trk == on->trk && ev[i].tag == on->tag)
            return &ev[i];
    return 0;
}
static int in_major(int n) { return (0x0AB5 >> (n % 12)) & 1; }

/* ------------------------------------------------------------- the tests --- */
static void t_timing(void)
{
    cur = "timing";
    reset();
    note(0, 0, 60, 100);
    note(0, 1, 62, 100);
    start();
    run(3 * L16);
    CHECK(on_n(0) && on_n(0)->t == 0 && on_n(0)->a == 60);
    CHECK(on_n(1) && near(on_n(1)->t, L16, 1) && on_n(1)->a == 62);
    CHECK(nth(E_STEP, 0, 0)->t == 0 && nth(E_STEP, 0, 1)->a == 1);
    /* the next pass: step 1 again at 16 sixteenths, exact to the sample (Q8 carries) */
    run(14 * L16 + 40);
    CHECK(on_n(2) && near(on_n(2)->t, 16 * 5512 + 8, 1));
    printf("ok %s\n", cur);
}

static void t_scale(void)
{
    cur = "scale";
    reset();
    P.pat[0][0].scale = TS_2;
    note(0, 0, 60, 100);
    note(0, 1, 62, 100);
    start();
    run(3 * L16);
    CHECK(on_n(1) && near(on_n(1)->t, 2 * L16 + 1, 1));
    reset();
    P.pat[0][0].scale = TS_HALF;
    note(0, 0, 60, 100);
    note(0, 1, 62, 100);
    start();
    run(L16);
    CHECK(on_n(1) && near(on_n(1)->t, L16 / 2, 1));
    printf("ok %s\n", cur);
}

/* grooves: the project's type, the pattern's signed amount */
static void t_groove(void)
{
    int i, ons;
    cur = "groove";
    reset();
    S.groove_type = GR_SWING;
    P.pat[0][0].groove = 99;          /* the upbeats half a sixteenth late */
    note(0, 0, 60, 100);
    note(0, 1, 62, 100);
    note(0, 2, 64, 100);
    start();
    run(3 * L16);
    CHECK(near(on_n(1)->t, L16 + L16 / 2, 2));
    CHECK(near(on_n(2)->t, 2 * L16 + 1, 1));
    /* +50: a quarter sixteenth, the old swing 75 % */
    reset();
    S.groove_type = GR_SWING;
    P.pat[0][0].groove = 50;
    note(0, 0, 60, 100);
    note(0, 1, 62, 100);
    start();
    run(3 * L16);
    CHECK(near(on_n(1)->t, L16 + L16 / 4, 24) && on_n(1)->b == 100);   /* swing leaves the velocity */
    /* -50: the same, early */
    reset();
    S.groove_type = GR_SWING;
    P.pat[0][0].groove = -50;
    note(0, 0, 60, 100);
    note(0, 1, 62, 100);
    start();
    run(3 * L16);
    CHECK(near(on_n(1)->t, L16 - L16 / 4, 24));
    /* stretch at the full amount: beat 2 two ticks early, beat 4 four ticks late, both 10 louder; -99 mirrors
     * the timing, the velocity stays */
    reset();
    S.groove_type = GR_STRETCH;
    P.pat[0][0].groove = 99;
    note(0, 4, 60, 100);
    note(0, 8, 62, 100);
    note(0, 12, 64, 100);
    start();
    run(13 * L16);
    CHECK(near(on_n(0)->t, 4 * L16 + 2 - 2 * (L16 / 24), 3) && on_n(0)->b == 110);
    CHECK(near(on_n(1)->t, 8 * L16 + 4, 3) && on_n(1)->b == 100);
    CHECK(near(on_n(2)->t, 12 * L16 + 6 + 4 * (L16 / 24), 3) && on_n(2)->b == 110);
    reset();
    S.groove_type = GR_STRETCH;
    P.pat[0][0].groove = -99;
    note(0, 4, 60, 100);
    note(0, 12, 64, 100);
    start();
    run(13 * L16);
    CHECK(near(on_n(0)->t, 4 * L16 + 2 + 2 * (L16 / 24), 3) && on_n(0)->b == 110);
    CHECK(near(on_n(1)->t, 12 * L16 + 6 - 4 * (L16 / 24), 3) && on_n(1)->b == 110);
    /* weave, not random: the "and" of 1 and 3 eight ticks late and 6 louder, of 2 and 4 four ticks early and 6
     * softer, the rest on the grid; -99 swaps early and late, the velocity stays; two runs agree */
    for (i = 0; i < 2; i++) {
        int k, amt = i ? -99 : 99;
        static const int ST[6] = {0, 2, 4, 6, 10, 14}, TK[6] = {0, 8, 0, -4, 8, -4}, VE[6] = {100, 106, 100, 94, 106, 94};
        int pass;
        for (pass = 0; pass < 2; pass++) {
            reset();
            S.groove_type = GR_WEAVE;
            P.pat[0][0].groove = (int8_t)amt;
            for (k = 0; k < 6; k++)
                note(0, ST[k], 60 + k, 100);
            start();
            run(16 * L16);
            CHECK(count(E_ON, 0) == 6);
            for (k = 0; k < 6; k++) {
                int tk = amt > 0 ? TK[k] : -TK[k];
                CHECK(near(on_n(k)->t, (uint32_t)((int32_t)(ST[k] * L16) + ST[k] / 2 + tk * (int32_t)L16 / 24), 3));
                CHECK(on_n(k)->b == VE[k]);
            }
        }
    }
    /* accent: the velocity table scaled by the amount */
    reset();
    S.groove_type = GR_ACCENT;
    P.pat[0][0].groove = 50;
    note(0, 0, 60, 100);
    note(0, 1, 60, 100);
    start();
    run(2 * L16);
    CHECK(on_n(0)->b == 110 && on_n(1)->b == 97);
    /* a random type rolls per hit within its range: drunk at 99 lands within +-6 ticks and +-20 velocity */
    reset();
    S.groove_type = GR_DRUNK;
    P.pat[0][0].groove = 99;
    P.pat[0][0].len = 1;
    note(0, 0, 60, 100);
    start();
    run(40 * L16);
    ons = count(E_ON, 0);
    CHECK(ons >= 39 && ons <= 41);
    for (i = 1; i < ons; i++) {
        uint32_t want = (uint32_t)i * L16 + (uint32_t)i / 2u;
        CHECK(near(on_n(i)->t, want, 6 * (L16 / 24) + 3));
        CHECK(on_n(i)->b >= 80 && on_n(i)->b <= 120);
    }
    /* no amount: straight, whatever the type */
    reset();
    S.groove_type = GR_DRUNK;
    note(0, 0, 60, 100);
    note(0, 1, 62, 100);
    start();
    run(2 * L16);
    CHECK(near(on_n(1)->t, L16, 1) && on_n(1)->b == 100);
    /* a nudge: +12 ticks = half a step late */
    reset();
    note(0, 1, 60, 100);
    st(0, 1)->nudge = 12;
    start();
    run(2 * L16);
    CHECK(near(on_n(0)->t, L16 + L16 / 2, 2));
    printf("ok %s\n", cur);
}

static void t_length(void)
{
    cur = "length";
    reset();
    note(0, 0, 60, 100);
    note(0, 4, 62, 100);
    st(0, 4)->len = 2;
    note(0, 8, 64, 100);
    st(0, 8)->len = 8;
    start();
    run(12 * L16);
    CHECK(nth(E_OFF, 0, 0) && near(nth(E_OFF, 0, 0)->t, L16 / 2, 1));              /* the pattern's: 50 % of a step */
    CHECK(near(nth(E_OFF, 0, 1)->t, 4 * L16 + L16 / 2, 2));                        /* half a step */
    CHECK(near(nth(E_OFF, 0, 2)->t, 10 * L16 + 5, 2) && nth(E_OFF, 0, 2)->tag == on_n(2)->tag);   /* two steps */
    /* the pattern's note length as a percentage: 100 = a whole step, 25 = a quarter */
    P.pat[0][0].nlen = 100;
    stop();
    nev = 0;
    start();
    run(2 * L16);
    CHECK(near(nth(E_OFF, 0, 0)->t, L16, 1));
    P.pat[0][0].nlen = 25;
    stop();
    nev = 0;
    start();
    run(2 * L16);
    CHECK(near(nth(E_OFF, 0, 0)->t, L16 / 4, 1));
    printf("ok %s\n", cur);
}

static void t_long_page(void)          /* a note held past its page: step 16 to 20 of a 32-step pattern */
{
    ev_t *on;
    cur = "long_page";
    reset();
    P.pat[0][0].len = 32;
    note(0, 15, 60, 100);
    st(0, 15)->len = 20;              /* five steps */
    start();
    run(24 * L16);
    on = on_n(0);
    CHECK(on && near(on->t, 15 * L16 + 7, 4));
    CHECK(off_of(on) && near(off_of(on)->t, 20 * L16 + 10, 6));
    /* longer than sixteen steps (whole steps past them), and round from the pattern's end to its start */
    CHECK(step_len_q(step_len_code(20 * 4)) == 80 && step_len_code(64 * 4) == LEN_MAX && step_len_q(LEN_MAX) == 256);
    stop();
    reset();
    P.pat[0][0].len = 32;
    note(0, 0, 60, 100);
    st(0, 0)->len = (uint8_t)step_len_code(20 * 4);
    note(0, 28, 62, 100);
    st(0, 28)->len = (uint8_t)step_len_code(8 * 4);
    start();
    run(40 * L16);
    on = on_n(0);
    CHECK(on && on->a == 60 && off_of(on) && near(off_of(on)->t, 20 * L16 + 10, 8));
    on = on_n(1);
    CHECK(on && on->a == 62 && off_of(on) && near(off_of(on)->t, 36 * L16 + 18, 10));
    printf("ok %s\n", cur);
}

static void t_chord(void)
{
    cur = "chord";
    reset();
    st(0, 0)->note[0] = 60;
    st(0, 0)->note[1] = 64;
    st(0, 0)->note[2] = 67;
    st(0, 0)->note[3] = 71;
    st(0, 0)->vel = 90;
    start();
    run(2 * L16);
    CHECK(count(E_ON, 0) == 4 && count(E_OFF, 0) == 4);
    CHECK(on_n(3)->a == 71 && on_n(3)->b == 90 && on_n(0)->tag != on_n(3)->tag);
    printf("ok %s\n", cur);
}

static void t_pulse(void)
{
    cur = "pulse";
    reset();
    note(0, 0, 60, 100);
    st(0, 0)->comp = SC_PULSE;
    st(0, 0)->cval = 1;               /* two repeats: the step plays three times, over steps 2 and 3 */
    note(0, 1, 62, 100);              /* (covered: it gives way) */
    note(0, 3, 64, 100);
    start();
    run(4 * L16 + 10);
    CHECK(count(E_ON, 0) == 4);
    CHECK(on_n(1)->a == 60 && near(on_n(1)->t, L16, 1));
    CHECK(on_n(2)->a == 60 && near(on_n(2)->t, 2 * L16 + 1, 1));
    CHECK(on_n(3)->a == 64 && near(on_n(3)->t, 3 * L16 + 1, 2));   /* in time: step 4 where it was */
    printf("ok %s\n", cur);
}

static void t_pulse_kit(void)          /* a kit's pulse: only the trick's sounds repeat, the others play on */
{
    int kicks = 0, hats = 0, k2 = 0, i;
    cur = "pulse_kit";
    reset();
    par[0][TP_ENGINE] = EG_DRUMLINE;
    note(0, 0, 1, 100);               /* the kick on step 1, pulsed over steps 2-3 */
    st(0, 0)->comp = SC_PULSE;
    st(0, 0)->cval = 1;
    for (i = 0; i < 4; i++)
        note(0, i, 3, 80);            /* hats on every step (on step 1 too: they repeat with the kick) */
    note(0, 2, 1, 100);               /* a kick on step 3 too: covered, the pulse's stands for it */
    note(0, 4, 1, 100);               /* and on 5: after the pulse, where it was */
    start();
    run(5 * L16 + 10);
    for (i = 0; i < nev; i++)
        if (ev[i].kind == E_DRUM && ev[i].trk == 0) {
            kicks += ev[i].a == 0;
            hats += ev[i].a == 2;
            k2 += ev[i].a == 0 && near(ev[i].t, L16, 2);
        }
    CHECK(kicks == 4 && hats == 4 && k2 == 1);   /* kicks on 1, 2, 3, 5 (3's own not twice); a hat a step */
    printf("ok %s\n", cur);
}

static void t_hold(void)
{
    cur = "hold";
    reset();
    note(0, 0, 60, 100);
    st(0, 0)->comp = SC_HOLD;
    st(0, 0)->cval = 1;               /* sounds three steps */
    note(0, 1, 62, 100);              /* (covered) */
    note(0, 3, 64, 100);
    start();
    run(4 * L16 + 10);
    CHECK(count(E_ON, 0) == 2);
    CHECK(near(nth(E_OFF, 0, 0)->t, 3 * L16 + 1, 2));
    CHECK(on_n(1)->a == 64 && near(on_n(1)->t, 3 * L16 + 1, 2));   /* in time */
    printf("ok %s\n", cur);
}

static void t_multiply(void)
{
    cur = "multiply";
    reset();
    note(0, 0, 60, 100);
    st(0, 0)->comp = SC_MULTIPLY;
    st(0, 0)->cval = 3;               /* four hits */
    note(0, 1, 62, 100);
    start();
    run(2 * L16);
    CHECK(count(E_ON, 0) == 5 && count(E_OFF, 0) >= 4);
    CHECK(near(on_n(1)->t, L16 / 4, 2) && near(on_n(2)->t, L16 / 2, 2) && near(on_n(3)->t, 3 * L16 / 4, 2));
    CHECK(on_n(0)->tag != on_n(1)->tag && on_n(4)->a == 62);
    printf("ok %s\n", cur);
}

static void t_velocity(void)
{
    cur = "velocity";
    reset();
    note(0, 0, 60, 100);
    st(0, 0)->comp = SC_VELOCITY;
    st(0, 0)->cval = 5;               /* 100 */
    note(0, 1, 60, 50);
    st(0, 1)->comp = SC_VELOCITY;
    st(0, 1)->cval = 0;               /* 4 */
    note(0, 2, 60, 50);
    st(0, 2)->comp = SC_VELOCITY;
    st(0, 2)->cval = 8;               /* 0: silent */
    start();
    run(4 * L16);
    CHECK(count(E_ON, 0) == 2 && on_n(0)->b == 100 && on_n(1)->b == 4);
    printf("ok %s\n", cur);
}

static void t_ramp(void)
{
    cur = "ramp";
    reset();
    P.pat[0][0].len = 1;
    note(0, 0, 60, 100);
    st(0, 0)->comp = SC_RAMP_UP;
    st(0, 0)->cval = 3;               /* five iterations over an octave: degrees 0 1 2 4 5, then back */
    start();
    run(7 * L16);
    CHECK(on_n(0)->a == 60 && on_n(1)->a == 62 && on_n(2)->a == 64 && on_n(3)->a == 67 && on_n(4)->a == 69 &&
          on_n(5)->a == 60 && on_n(6)->a == 62);
    reset();
    P.pat[0][0].len = 1;
    note(0, 0, 60, 100);
    st(0, 0)->comp = SC_RAMP_UP;
    st(0, 0)->cval = 0;               /* two iterations: the tonic and the middle of the octave */
    start();
    run(3 * L16);
    CHECK(on_n(0)->a == 60 && on_n(1)->a == 65 && on_n(2)->a == 60);
    reset();
    P.pat[0][0].len = 1;
    note(0, 0, 60, 100);
    st(0, 0)->comp = SC_RAMP_DOWN;
    st(0, 0)->cval = 5;               /* two iterations over three octaves: 0 and -10 degrees */
    start();
    run(3 * L16);
    CHECK(on_n(0)->a == 60 && on_n(1)->a == 43 && on_n(2)->a == 60);
    printf("ok %s\n", cur);
}

static void t_random(void)
{
    int i, spread = 0;
    cur = "random";
    reset();
    P.pat[0][0].len = 1;
    note(0, 0, 60, 100);
    st(0, 0)->comp = SC_RANDOM;
    st(0, 0)->cval = 2;               /* one of four positions within the octave */
    start();
    run(60 * L16);
    CHECK(count(E_ON, 0) == 60);
    for (i = 0; i < 60; i++) {
        CHECK(in_major(on_n(i)->a) && on_n(i)->a >= 60 && on_n(i)->a <= 72);
        spread |= on_n(i)->a != 60;
    }
    CHECK(spread);
    printf("ok %s\n", cur);
}

static void t_portamento(void)
{
    cur = "portamento";
    reset();
    note(0, 0, 60, 100);
    st(0, 0)->comp = SC_PORTAMENTO;
    st(0, 0)->cval = 4;               /* 50 % of a 125 ms step */
    start();
    run(L16);
    CHECK(on_n(0)->c >= 61 && on_n(0)->c <= 63);
    reset();
    note(0, 0, 60, 100);
    st(0, 0)->comp = SC_PORTAMENTO;
    st(0, 0)->cval = 8;               /* off */
    start();
    run(L16);
    CHECK(on_n(0)->c == 0);
    printf("ok %s\n", cur);
}

static void t_bend(void)
{
    int i, max = 0, min = 0, last = 1;
    cur = "bend";
    reset();
    note(0, 0, 60, 100);
    st(0, 0)->comp = SC_BEND;
    st(0, 0)->cval = 1;               /* up-down */
    start();
    run_blocks(2 * L16);              /* (a bend moves once a stretch: the synth glides it across the chunks) */
    for (i = 0; i < nev; i++)
        if (ev[i].kind == E_BEND) {
            if (ev[i].a > max)
                max = ev[i].a;
            last = ev[i].a;
        }
    CHECK(max >= 190 && last == 0 && count(E_BEND, 0) > 20);
    reset();
    note(0, 0, 60, 100);
    st(0, 0)->comp = SC_BEND;
    st(0, 0)->cval = 0;               /* down-up */
    start();
    run(2 * L16);
    for (i = 0; i < nev; i++)
        if (ev[i].kind == E_BEND && ev[i].a < min)
            min = ev[i].a;
    CHECK(min <= -190);
    printf("ok %s\n", cur);
}

/* a pattern's saved loop: it starts at the loop's first step and wraps at its end, the rest of the pattern kept */
static void t_loop(void)
{
    int i;
    cur = "loop";
    reset();
    P.pat[0][0].len = 64;
    for (i = 0; i < 64; i++)
        note(0, i, 30 + i, 100);
    P.pat[0][0].lstart = 48;                         /* bar 4 */
    P.pat[0][0].lend = 0;
    start();
    run(20 * L16);
    CHECK(on_n(0)->a == 30 + 48 && on_n(1)->a == 30 + 49);
    CHECK(on_n(15)->a == 30 + 63 && on_n(16)->a == 30 + 48);
    P.pat[0][0].lstart = 8;                          /* steps 9 to 12 */
    P.pat[0][0].lend = 12;
    stop();
    nev = 0;
    start();
    run(10 * L16);
    CHECK(on_n(0)->a == 30 + 8 && on_n(3)->a == 30 + 11 && on_n(4)->a == 30 + 8 && on_n(9)->a == 30 + 9);
    /* a bar jump queued while playing: at the next bar line (every 16 steps) it jumps and sets the loop */
    reset();
    P.pat[0][0].len = 64;
    for (i = 0; i < 64; i++)
        note(0, i, 30 + i, 100);
    start();
    run(2 * L16);
    seq_bar_go(&S, 0, 48, 48, 0);
    run(16 * L16);
    CHECK(on_n(14)->a == 30 + 14 && on_n(15)->a == 30 + 15 && on_n(16)->a == 30 + 48);
    CHECK(P.pat[0][0].lstart == 48 && S.crop_dirty);
    /* one bar key: jump to that bar at the bar line and play on through the pattern, the loop cleared */
    reset();
    P.pat[0][0].len = 64;
    for (i = 0; i < 64; i++)
        note(0, i, 30 + i, 100);
    P.pat[0][0].lstart = 16;
    P.pat[0][0].lend = 32;
    start();
    run(2 * L16);
    seq_bar_go(&S, 0, 48, 0, 0);
    run(36 * L16);
    CHECK(on_n(14)->a == 30 + 30 && on_n(15)->a == 30 + 31 && on_n(16)->a == 30 + 48 && on_n(31)->a == 30 + 63);
    CHECK(on_n(32)->a == 30 && on_n(33)->a == 30 + 1);
    CHECK(P.pat[0][0].lstart == 0 && P.pat[0][0].lend == 0);
    printf("ok %s\n", cur);
}

static void t_jump(void)
{
    int i;
    cur = "jump";
    reset();
    for (i = 0; i < 16; i++)
        note(0, i, 60 + i, 100);
    st(0, 1)->comp = SC_JUMP;
    st(0, 1)->cval = 0;               /* to step 1 */
    start();
    run(4 * L16);
    CHECK(on_n(2)->a == 60 && on_n(3)->a == 61);
    st(0, 1)->cval = 3;               /* to step 13 */
    stop();
    nev = 0;
    start();
    run(4 * L16);
    CHECK(on_n(2)->a == 72 && on_n(3)->a == 73);
    st(0, 1)->cval = 5;               /* one back */
    stop();
    nev = 0;
    start();
    run(4 * L16);
    CHECK(on_n(2)->a == 60 && on_n(3)->a == 61);
    st(0, 1)->cval = 7;               /* stay */
    stop();
    nev = 0;
    start();
    run(4 * L16);
    CHECK(on_n(1)->a == 61 && on_n(2)->a == 61 && on_n(3)->a == 61);
    st(0, 1)->cval = 4;               /* one forward: skips step 3 */
    stop();
    nev = 0;
    start();
    run(4 * L16);
    CHECK(on_n(2)->a == 63 && on_n(3)->a == 64);
    /* a four-bar pattern: the beats past step 16 (values 10..21: to 17 .. 61) */
    reset();
    P.pat[0][0].len = 64;
    for (i = 0; i < 64; i++)
        note(0, i, 30 + i, 100);
    st(0, 1)->comp = SC_JUMP;
    st(0, 1)->cval = 10;              /* to step 17 */
    start();
    run(4 * L16);
    CHECK(on_n(2)->a == 30 + 16 && on_n(3)->a == 30 + 17);
    st(0, 1)->cval = 21;              /* to step 61 */
    stop();
    nev = 0;
    start();
    run(4 * L16);
    CHECK(on_n(2)->a == 30 + 60 && on_n(3)->a == 30 + 61);
    st(0, 1)->cval = 13;              /* to step 29 */
    stop();
    nev = 0;
    start();
    run(4 * L16);
    CHECK(on_n(2)->a == 30 + 28);
    printf("ok %s\n", cur);
}

/* a step's chance and its pass tricks decide whether the step happens, its jump included; a jump back (or stay)
 * starts a new pass, so the pass tricks keep counting inside a jump loop */
static void t_jump_chance(void)
{
    int i, visits = 0, jumps = 0, prev = -1;
    static const int want_ab[] = {60, 61, 60, 62, 63, 60, 61, 60, 62, 63};
    static const int want_sk[] = {60, 60, 61, 62, 63, 60, 60, 61, 62, 63};
    cur = "jump chance";
    reset();
    P.pat[0][0].len = 4;              /* empty steps (a jump works on one): the log keeps only the steps */
    st(0, 1)->comp = SC_JUMP;
    st(0, 1)->cval = 5;               /* one back */
    st(0, 1)->cond = 25;
    S.t[0].rng = 0x12345678u;         /* a fixed seed */
    start();
    run(1600 * L16);
    CHECK(nev < NEV);
    for (i = 0; i < nev; i++) {
        if (ev[i].kind != E_STEP || ev[i].trk != 0)
            continue;
        if (prev == 1) {
            visits++;
            jumps += ev[i].a == 0;
        }
        prev = ev[i].a;
    }
    CHECK(visits > 300 && jumps * 100 > visits * 18 && jumps * 100 < visits * 32);
    /* a condition inside the loop: step 2 plays (and jumps back) on the first of every two passes */
    stop();
    reset();
    P.pat[0][0].len = 4;
    for (i = 0; i < 4; i++)
        note(0, i, 60 + i, 100);
    st(0, 1)->comp = SC_JUMP;
    st(0, 1)->cval = 5;
    st(0, 1)->cond = COND_AB + 0;     /* 1:2 */
    start();
    run(16 * L16);   /* a step that does not happen still takes its time */
    CHECK(count(E_ON, 0) >= 10);
    for (i = 0; i < 10; i++)
        CHECK(on_n(i)->a == want_ab[i]);
    /* skip trigger inside a stay loop: step 1 stays twice in every three passes, then the pattern goes on */
    stop();
    reset();
    P.pat[0][0].len = 4;
    for (i = 0; i < 4; i++)
        note(0, i, 60 + i, 100);
    st(0, 0)->comp = SC_JUMP;
    st(0, 0)->cval = 7;               /* stay */
    st(0, 0)->comp2 = SC_SKIP;
    st(0, 0)->cval2 = SK_VAL(2, 1);   /* a cycle of three, skipping its last pass */
    start();
    run(16 * L16);   /* a step that does not happen still takes its time */
    CHECK(count(E_ON, 0) >= 10);
    for (i = 0; i < 10; i++)
        CHECK(on_n(i)->a == want_sk[i]);
    printf("ok %s\n", cur);
}

static void t_skip(void)
{
    int k;
    cur = "skip";
    reset();
    P.pat[0][0].len = 1;
    note(0, 0, 60, 100);
    st(0, 0)->comp = SC_SKIP;
    st(0, 0)->cval = SK_VAL(1, 0);    /* a cycle of two, playing on its last pass: every second pass */
    start();
    run(6 * L16);
    CHECK(count(E_ON, 0) == 3 && near(on_n(0)->t, L16, 1) && near(on_n(1)->t, 3 * L16 + 1, 2));
    st(0, 0)->cval = SK_VAL(2, 0);    /* every third */
    stop();
    nev = 0;
    start();
    run(9 * L16);
    CHECK(count(E_ON, 0) == 3 && near(on_n(0)->t, 2 * L16 + 1, 2));
    st(0, 0)->cval = SK_VAL(3, 1);    /* a cycle of four, skipping its last pass */
    stop();
    nev = 0;
    start();
    run(8 * L16 - L16 / 2);           /* (mid-step: a step due at the window's end may fire a few samples early) */
    CHECK(count(E_ON, 0) == 6 && on_n(0)->t == 0 && near(on_n(3)->t, 4 * L16 + 2, 2));
    st(0, 0)->cval = SK_VAL(3, 2);    /* only the first of four */
    stop();
    nev = 0;
    start();
    run(8 * L16 - L16 / 2);
    CHECK(count(E_ON, 0) == 2 && on_n(0)->t == 0 && near(on_n(1)->t, 4 * L16 + 2, 2));
    st(0, 0)->cval = SK_VAL(9, 0);    /* a random cycle length: some passes play */
    stop();
    nev = 0;
    start();
    run(60 * L16);
    CHECK(count(E_ON, 0) > 5 && count(E_ON, 0) < 55);
    /* skip the parameter lock: the lock is applied on the cycle's last pass only; the note plays every pass */
    reset();
    P.pat[0][0].len = 1;
    note(0, 0, 60, 100);
    st(0, 0)->comp = SC_SKIP_LOCK;
    st(0, 0)->cval = SK_VAL(1, 0);
    k = lane_alloc(P.lane, 0, 0, TP_CUT);
    P.lane[k].val[0] = 40;
    start();
    run(4 * L16 - L16 / 2);
    CHECK(count(E_ON, 0) == 4 && count(E_LOCK, 0) == 1 && near(nth(E_LOCK, 0, 0)->t, L16, 1));
    /* skip the step component: the velocity component in comp2 runs on the cycle's last pass only */
    reset();
    P.pat[0][0].len = 1;
    note(0, 0, 60, 100);
    st(0, 0)->comp = SC_SKIP_COMP;
    st(0, 0)->cval = SK_VAL(1, 0);
    st(0, 0)->comp2 = SC_VELOCITY;
    st(0, 0)->cval2 = 0;              /* 4 */
    start();
    run(4 * L16 - L16 / 2);
    CHECK(count(E_ON, 0) == 4 && on_n(0)->b == 100 && on_n(1)->b == 4 && on_n(2)->b == 100 && on_n(3)->b == 4);
    /* the skip in comp2 gates the component in comp the same way */
    st(0, 0)->comp = SC_MULTIPLY;
    st(0, 0)->cval = 1;               /* two hits */
    st(0, 0)->comp2 = SC_SKIP_COMP;
    st(0, 0)->cval2 = SK_VAL(1, 0);
    stop();
    nev = 0;
    start();
    run(4 * L16 - L16 / 2);
    CHECK(count(E_ON, 0) == 6);
    printf("ok %s\n", cur);
}

/* two components on one step, and the tonality component's ten options */
static void t_components(void)
{
    int i, lo = 0, hi = 0;
    cur = "components";
    reset();
    note(0, 0, 60, 100);
    st(0, 0)->comp = SC_MULTIPLY;
    st(0, 0)->cval = 1;               /* two hits */
    st(0, 0)->comp2 = SC_VELOCITY;
    st(0, 0)->cval2 = 7;              /* 127 */
    start();
    run(L16);
    CHECK(count(E_ON, 0) == 2 && on_n(0)->b == 127 && on_n(1)->b == 127 && near(on_n(1)->t, L16 / 2, 2));
    CHECK(SC_DEFAULT[SC_PULSE] == 3 && SC_DEFAULT[SC_PORTAMENTO] == 3 && SK_N(SC_DEFAULT[SC_SKIP]) == 1 &&
          SK_MODE(SC_DEFAULT[SC_SKIP]) == 0);
    /* tonality: the fixed intervals in C major */
    reset();
    for (i = 0; i < 7; i++) {
        note(0, i, 60, 100);
        st(0, i)->comp = SC_TONALITY;
        st(0, i)->cval = (uint8_t)i;
    }
    start();
    run(7 * L16);
    CHECK(on_n(0)->a == 60 && on_n(1)->a == 60);                       /* ignore / transpose only: no brain shift */
    CHECK(on_n(2)->a == 72 && on_n(3)->a == 67 && on_n(4)->a == 64);   /* octave, fifth, third (major) */
    CHECK(on_n(5)->a == 61 && on_n(6)->a == 59);                       /* chromatic up, down */
    /* the third by the scale: minor from D, a minor key's third from its tonic */
    reset();
    note(0, 0, 62, 100);
    st(0, 0)->comp = SC_TONALITY;
    st(0, 0)->cval = 4;
    start();
    run(L16);
    CHECK(on_n(0)->a == 65);
    reset();
    S.brain_key = 9;
    S.brain_scale = BS_MINOR;
    par[AUX_BRAIN][TP_E2] = 9;
    par[AUX_BRAIN][TP_E3] = BS_MINOR;
    note(0, 0, 57, 100);
    st(0, 0)->comp = SC_TONALITY;
    st(0, 0)->cval = 4;
    start();
    run(L16);
    CHECK(on_n(0)->a == 60);
    /* quantize: 100 % snaps C# to C (ties go down), 33 % some of the time */
    reset();
    P.pat[0][0].len = 1;
    note(0, 0, 61, 100);
    st(0, 0)->comp = SC_TONALITY;
    st(0, 0)->cval = 9;
    start();
    run(10 * L16);
    for (i = 0; i < 10; i++)
        CHECK(on_n(i)->a == 60);
    st(0, 0)->cval = 7;
    stop();
    nev = 0;
    start();
    run(100 * L16);
    for (i = 0; i < 100; i++) {
        lo += on_n(i)->a == 60;
        hi += on_n(i)->a == 61;
    }
    CHECK(lo > 10 && lo < 60 && lo + hi == 100);
    /* with the brain: ignore sits the step out, transpose only moves it chromatically */
    reset();
    par[0][TP_BRAIN] = 1;
    note(0, 0, 60, 100);
    st(0, 0)->comp = SC_TONALITY;
    st(0, 0)->cval = 0;
    note(0, 1, 62, 100);
    st(0, 1)->comp = SC_TONALITY;
    st(0, 1)->cval = 1;
    note(0, 2, 62, 100);
    start();
    seq_brain_key(&S, 64, 1);         /* +2 degrees */
    run(3 * L16);
    CHECK(on_n(0)->a == 60 && on_n(1)->a == 66 && on_n(2)->a == 65);
    printf("ok %s\n", cur);
}

static void t_cond(void)
{
    cur = "cond";
    reset();
    P.pat[0][0].len = 1;
    note(0, 0, 60, 100);
    st(0, 0)->cond = 0;
    start();
    run(100 * L16);
    CHECK(count(E_ON, 0) == 100);
    st(0, 0)->cond = 1;
    stop();
    nev = 0;
    start();
    run(200 * L16);
    CHECK(count(E_ON, 0) <= 12);
    st(0, 0)->cond = 99;
    stop();
    nev = 0;
    start();
    run(200 * L16);
    CHECK(count(E_ON, 0) >= 188);
    st(0, 0)->cond = 50;
    stop();
    nev = 0;
    start();
    run(400 * L16);
    CHECK(count(E_ON, 0) > 150 && count(E_ON, 0) < 250);
    st(0, 0)->cond = COND_AB + 0;     /* 1:2 */
    stop();
    nev = 0;
    start();
    run(6 * L16 - L16 / 2);
    CHECK(count(E_ON, 0) == 3 && on_n(1)->t > 2 * L16 - 2);
    st(0, 0)->cond = COND_AB + 1;     /* 2:2 */
    stop();
    nev = 0;
    start();
    run(6 * L16);
    CHECK(count(E_ON, 0) == 3 && near(on_n(0)->t, L16, 1));
    printf("ok %s\n", cur);
}

static void t_locks(void)
{
    int k, i, prev, seen = 0;
    cur = "locks";
    reset();
    note(0, 0, 60, 100);
    note(0, 2, 62, 100);
    k = lane_alloc(P.lane, 0, 0, TP_CUT);
    CHECK(k >= 0 && lane_find(P.lane, 0, 0, TP_CUT) == k && lane_count(P.lane) == 1);
    P.lane[k].val[0] = 10;
    P.lane[k].val[2] = 50;
    CHECK(step_has_lock(P.lane, 0, 0, 2) && !step_has_lock(P.lane, 0, 0, 1));
    start();
    run(4 * L16);
    CHECK(count(E_LOCK, 0) == 2);
    CHECK(nth(E_LOCK, 0, 0)->t == 0 && nth(E_LOCK, 0, 0)->a == TP_CUT && nth(E_LOCK, 0, 0)->b == 10);
    CHECK(nth(E_LOCK, 0, 0) < on_n(0));                                 /* the lock before the note */
    CHECK(near(nth(E_LOCK, 0, 1)->t, 2 * L16 + 1, 1) && nth(E_LOCK, 0, 1)->b == 50);
    /* the pattern wraps: 50 holds to the end (nothing restores the knob), then step 1's 10 again */
    run(14 * L16);
    CHECK(count(E_LOCK, 0) == 3 && nth(E_LOCK, 0, 2)->b == 10 && near(nth(E_LOCK, 0, 2)->t, 16 * L16 + 8, 1));
    stop();
    CHECK(nth(E_LOCK, 0, 3) && nth(E_LOCK, 0, 3)->b == LOCK_NONE);     /* stop: the knob again */
    /* a step that plays without a lock gets the knob back; the empty steps before it kept the lock */
    note(0, 5, 64, 100);
    nev = 0;
    start();
    run(6 * L16);
    CHECK(count(E_LOCK, 0) == 3 && nth(E_LOCK, 0, 1)->b == 50 && nth(E_LOCK, 0, 2)->b == LOCK_NONE &&
          near(nth(E_LOCK, 0, 2)->t, 5 * L16 + 1, 1));
    stop();
    memset(&P.pat[0][0].st[5], 0, sizeof P.pat[0][0].st[5]);
    /* shape: the value walks from 10 to 50 across the two steps */
    P.pat[0][0].shape = 100;
    nev = 0;
    start();
    run(2 * L16);
    prev = 10;
    for (i = 0; i < nev; i++)
        if (ev[i].kind == E_LOCK) {
            CHECK(ev[i].b >= prev && ev[i].b <= 50);
            prev = ev[i].b;
            seen++;
        }
    CHECK(seen > 10 && prev == 50);
    printf("ok %s\n", cur);
}

static void t_scene(void)
{
    cur = "scene";
    reset();
    note(0, 0, 60, 100);
    P.pat[0][1].st[0].note[0] = 72;
    P.pat[0][1].st[0].vel = 100;
    P.scene[1].used = 1;
    P.scene[1].pat[0] = 1;
    start();
    run(L16);
    seq_cue_scene(&S, 1, 0);
    run(20 * L16);
    CHECK(count(E_SCENE, -1) == 1 && nth(E_SCENE, -1, 0)->a == 1 && near(nth(E_SCENE, -1, 0)->t, 16 * L16 + 8, 1));
    CHECK(on_n(1)->a == 72 && near(on_n(1)->t, 16 * L16 + 8, 1) && S.scene == 1 && S.ppat[0] == 1);
    /* at once */
    seq_set_scene(&S, 0);
    run(L16);
    CHECK(S.scene == 0 && S.ppat[0] == 0);
    printf("ok %s\n", cur);
}

static void t_song(void)
{
    cur = "song";
    reset();
    note(0, 0, 60, 100);
    P.scene[1].used = 1;
    P.scene[1].pat[0] = 1;
    P.pat[0][1].st[0].note[0] = 72;
    P.pat[0][1].st[0].vel = 100;
    P.song.len = 2;
    P.song.loop = 1;
    P.song.slot[0].scene = 0;
    P.song.slot[0].rep = 2;
    P.song.slot[1].scene = 1;
    P.song.slot[1].rep = 1;
    S.song_on = 1;
    start();
    run(49 * L16);
    CHECK(on_n(0)->a == 60 && on_n(1)->a == 60 && on_n(2)->a == 72 && on_n(3)->a == 60);
    CHECK(near(on_n(2)->t, 32 * L16 + 16, 1));
    printf("ok %s\n", cur);
}

/* a song slot counts bars of 16 sixteenths, whatever track 1's length: a 64-step pattern on track 1 plays two
 * bars of it under repeats 2, then the next slot (its pattern from the top); a track whose pattern stays goes on */
static void t_song_bars(void)
{
    cur = "song-bars";
    reset();
    P.pat[0][0].len = 64;
    note(0, 0, 60, 100);
    note(0, 32, 61, 100);             /* bar 3 of track 1's pattern: never reached */
    P.pat[1][0].len = 64;
    note(1, 0, 40, 100);
    note(1, 16, 41, 100);
    note(1, 32, 42, 100);
    P.scene[1].used = 1;
    P.scene[1].pat[0] = 1;
    P.pat[0][1].st[0].note[0] = 72;
    P.pat[0][1].st[0].vel = 100;
    P.song.len = 2;
    P.song.loop = 1;
    P.song.slot[0].scene = 0;
    P.song.slot[0].rep = 2;
    P.song.slot[1].scene = 1;
    P.song.slot[1].rep = 1;
    S.song_on = 1;
    start();
    run(49 * L16);
    CHECK(count(E_ON, 0) == 3 && on_n(0)->a == 60 && on_n(1)->a == 72 && on_n(2)->a == 60);
    CHECK(near(on_n(1)->t, 32 * L16 + 16, 1) && near(on_n(2)->t, 48 * L16 + 24, 1));
    CHECK(count(E_ON, 1) == 3 && nth(E_ON, 1, 2)->a == 42 && near(nth(E_ON, 1, 2)->t, 32 * L16 + 16, 1));
    /* (track 2, the same pattern in both scenes, runs on through the slots: its bar 3 in slot 2) */
    printf("ok %s\n", cur);
}

static void t_brain(void)
{
    cur = "brain";
    CHECK(brain_shift(60, 0, BS_MAJOR, 2, 1) == 64 && brain_shift(62, 0, BS_MAJOR, 2, 1) == 65 &&
          brain_shift(64, 0, BS_MAJOR, 2, 1) == 67);
    CHECK(brain_shift(60, 0, BS_MAJOR, 2, 2) == 64 && brain_shift(62, 0, BS_MAJOR, 2, 2) == 66);
    CHECK(brain_shift(60, 0, BS_MAJOR, -1, 1) == 59 && brain_shift(60, 0, BS_MAJOR, 7, 1) == 72);
    CHECK(brain_shift(61, 0, BS_MAJOR, 1, 1) == 62);          /* C# snaps to C first */
    CHECK(brain_shift(60, 0, BS_MAJOR, 2, 0) == 60);
    CHECK(scale_step(60, 0, BS_CHROMATIC, 3) == 63 && scale_step(60, 9, BS_MINOR, 1) == 62);
    /* the brain in the sequence: the BRAIN track's key sets the degree, routed tracks follow */
    reset();
    note(0, 0, 60, 100);
    note(1, 0, 60, 100);
    par[0][TP_BRAIN] = 1;
    start();
    seq_brain_key(&S, 64, 1);         /* E over C major: +2 degrees */
    run(17 * L16);
    CHECK(on_n(0)->a == 60 && on_n(1)->a == 64);
    CHECK(nth(E_ON, 1, 1)->a == 60);  /* not routed */
    /* a brain step: a chord whose lowest note is A3 (57), two degrees under C: the C goes to A */
    P.pat[AUX_BRAIN][0].st[0].note[0] = 60;
    P.pat[AUX_BRAIN][0].st[0].note[1] = 57;
    P.pat[AUX_BRAIN][0].st[0].note[2] = 64;
    P.pat[AUX_BRAIN][0].st[0].vel = 100;
    stop();
    nev = 0;
    start();
    run(L16);
    CHECK(on_n(0)->a == 57);
    /* a chord on the brain's keys: the lowest key held is the root */
    P.pat[AUX_BRAIN][0].st[0].note[0] = P.pat[AUX_BRAIN][0].st[0].note[1] = P.pat[AUX_BRAIN][0].st[0].note[2] = 0;
    stop();
    nev = 0;
    seq_brain_key(&S, 64, 0);         /* the E from above goes */
    seq_brain_key(&S, 71, 1);         /* B, then G under it: a G chord, +4 degrees */
    seq_brain_key(&S, 67, 1);
    start();
    run(L16);
    CHECK(on_n(0)->a == 67);
    seq_brain_key(&S, 71, 0);
    seq_brain_key(&S, 67, 0);
    run(10);
    /* the link track plays the brain's keys as pressed, in parallel */
    par[AUX_BRAIN][TP_E4] = 2;        /* track 2 */
    par[1][TP_BRAIN] = 0;
    nev = 0;
    seq_brain_key(&S, 62, 1);
    run(10);
    CHECK(count(E_ON, 1) == 1 && nth(E_ON, 1, 0)->a == 62 && S.brain_link == 2);
    seq_brain_key(&S, 62, 0);
    run(10);
    CHECK(count(E_OFF, 1) == 1);
    par[AUX_BRAIN][TP_E4] = 0;
    stop();
    nev = 0;
    start();
    /* a scene carries the brain's state */
    P.scene[1].used = 1;
    P.scene[1].brain_deg = (uint8_t)(int8_t)4;
    P.scene[1].brain_key = 0;
    P.scene[1].brain_scale = BS_MAJOR;
    P.pat[AUX_BRAIN][0].st[0].note[0] = 0;
    seq_set_scene(&S, 1);
    run(17 * L16);
    CHECK(on_n(1)->a == 67);
    printf("ok %s\n", cur);
}

static void t_follow(void)             /* the conductor's FOLLOW track: its lowest note sets the chord */
{
    cur = "follow";
    reset();
    par[AUX_BRAIN][TP_E1] = 3;        /* T3's notes */
    st(2, 0)->note[0] = 67;           /* G under B, D: G is +4 degrees, the nearer way -3 */
    st(2, 0)->note[1] = 71;
    st(2, 0)->note[2] = 74;
    st(2, 0)->vel = 100;
    note(2, 4, 41, 100);              /* F2, a bass note: +3 whatever its octave */
    note(0, 0, 60, 100);              /* T1 follows: the same step hears the chord (T3 fires first) */
    note(0, 4, 60, 100);
    note(1, 0, 60, 100);              /* T2 does not */
    par[0][TP_BRAIN] = 1;
    par[2][TP_BRAIN] = 1;             /* (routed, but the chords' track follows nothing) */
    start();
    run(5 * L16 + 10);
    CHECK(on_n(0)->a == 55 && on_n(1)->a == 65);   /* C down to G (-3), then up to F (+3) */
    CHECK(nth(E_ON, 1, 0)->a == 60);
    CHECK(nth(E_ON, 2, 0)->a == 67 && nth(E_ON, 2, 3)->a == 41);   /* T3 plays as written */
    /* the conductor's own steps give way to the FOLLOW track */
    stop();
    nev = 0;
    P.pat[AUX_BRAIN][0].st[0].note[0] = 62;
    P.pat[AUX_BRAIN][0].st[0].vel = 100;
    st(2, 0)->note[0] = 60;           /* C: degree 0 */
    st(2, 0)->note[1] = st(2, 0)->note[2] = 0;
    start();
    run(L16);
    CHECK(on_n(0)->a == 60);
    /* its keys too: the lowest held */
    stop();
    nev = 0;
    memset(st(2, 0), 0, sizeof(step_t));
    memset(st(2, 4), 0, sizeof(step_t));
    seq_key(&S, 2, 69, 1, 100);       /* A: -2 */
    run(10);
    start();
    run(L16);
    CHECK(on_n(0)->a == 57);
    seq_key(&S, 2, 69, 0, 0);
    par[AUX_BRAIN][TP_E1] = 0;
    P.pat[AUX_BRAIN][0].st[0].note[0] = 0;
    printf("ok %s\n", cur);
}

static void t_arp(void)
{
    cur = "arp";
    reset();
    par[0][TP_PLAYER] = PL_ARP;
    par[0][TP_P1] = 4;                /* 1/16 */
    par[0][TP_P2] = 0;                /* up */
    par[0][TP_P3] = 1;
    seq_key(&S, 0, 60, 1, 100);
    seq_key(&S, 0, 64, 1, 100);
    seq_key(&S, 0, 67, 1, 100);
    run(4 * L16 - L16 / 2);
    CHECK(count(E_ON, 0) == 4 && on_n(0)->a == 60 && on_n(1)->a == 64 && on_n(2)->a == 67 && on_n(3)->a == 60);
    CHECK(near(on_n(1)->t, L16 + 1, 2) && near(on_n(2)->t, 2 * L16 + 1, 2));
    CHECK(nth(E_OFF, 0, 0) && near(nth(E_OFF, 0, 0)->t, L16 / 4 + 1, 2));   /* length short: a quarter period */
    seq_key(&S, 0, 60, 0, 0);
    seq_key(&S, 0, 64, 0, 0);
    seq_key(&S, 0, 67, 0, 0);
    run(2 * L16);
    CHECK(count(E_ON, 0) == 4 && count(E_OFF, 0) == 4);
    /* down, two octaves, 1/8 */
    nev = 0;
    par[0][TP_P1] = 2;
    par[0][TP_P2] = 1;
    par[0][TP_P3] = 2;
    seq_key(&S, 0, 60, 1, 100);
    seq_key(&S, 0, 64, 1, 100);
    run(8 * L16 - L16 / 2);
    CHECK(count(E_ON, 0) == 4 && on_n(0)->a == 76 && on_n(1)->a == 72 && on_n(2)->a == 64 && on_n(3)->a == 60);
    CHECK(near(on_n(1)->t - on_n(0)->t, 2 * L16 + 1, 2));
    /* hold: the keys may go */
    par[0][TP_P4] = 1;
    seq_key(&S, 0, 60, 0, 0);
    seq_key(&S, 0, 64, 0, 0);
    run(4 * L16);
    CHECK(count(E_ON, 0) == 6);
    /* up-down 1: the ends once */
    nev = 0;
    par[0][TP_P4] = 0;
    par[0][TP_P2] = 2;
    par[0][TP_P3] = 1;
    par[0][TP_P1] = 4;
    seq_key_all_off(&S, 0);
    seq_key(&S, 0, 60, 1, 100);
    seq_key(&S, 0, 64, 1, 100);
    seq_key(&S, 0, 67, 1, 100);
    run(6 * L16);
    CHECK(on_n(0)->a == 60 && on_n(1)->a == 64 && on_n(2)->a == 67 && on_n(3)->a == 64 && on_n(4)->a == 60 &&
          on_n(5)->a == 64);
    /* up-down 2: the ends repeated */
    nev = 0;
    par[0][TP_P2] = 3;
    seq_key_all_off(&S, 0);
    seq_key(&S, 0, 60, 1, 100);
    seq_key(&S, 0, 64, 1, 100);
    seq_key(&S, 0, 67, 1, 100);
    run(8 * L16);
    CHECK(on_n(0)->a == 60 && on_n(1)->a == 64 && on_n(2)->a == 67 && on_n(3)->a == 67 && on_n(4)->a == 64 &&
          on_n(5)->a == 60 && on_n(6)->a == 60 && on_n(7)->a == 64);
    /* 1/64: four notes a sixteenth, the key's velocity */
    nev = 0;
    par[0][TP_P1] = 8;
    par[0][TP_P2] = 0;
    seq_key_all_off(&S, 0);
    seq_key(&S, 0, 60, 1, 77);
    run(L16 + 10);
    CHECK(count(E_ON, 0) == 5 && near(on_n(1)->t - on_n(0)->t, L16 / 4, 2) && on_n(0)->b == 77);
    /* 1/4T: three notes a quarter */
    nev = 0;
    par[0][TP_P1] = 1;
    seq_key_all_off(&S, 0);
    seq_key(&S, 0, 60, 1, 100);
    run(8 * L16 + 10);
    CHECK(count(E_ON, 0) == 4 && near(on_n(1)->t - on_n(0)->t, 8 * L16 / 3, 3));
    /* the styles over an up arp of 60 64 67 71: A 1 2 1 3 1 4, B 1 3 2 4 3 1, C 1 1 2 2, D 1 3 2 4, E 2 1 4 3 */
    nev = 0;
    par[0][TP_P1] = 4;
    par[0][TP_AP2] = 1;
    seq_key_all_off(&S, 0);
    seq_key(&S, 0, 60, 1, 100);
    seq_key(&S, 0, 64, 1, 100);
    seq_key(&S, 0, 67, 1, 100);
    seq_key(&S, 0, 71, 1, 100);
    run(6 * L16 + 10);
    CHECK(on_n(0)->a == 60 && on_n(1)->a == 64 && on_n(2)->a == 60 && on_n(3)->a == 67 && on_n(4)->a == 60 &&
          on_n(5)->a == 71);
    nev = 0;
    par[0][TP_AP2] = 2;
    seq_key_all_off(&S, 0);
    seq_key(&S, 0, 60, 1, 100);
    seq_key(&S, 0, 64, 1, 100);
    seq_key(&S, 0, 67, 1, 100);
    seq_key(&S, 0, 71, 1, 100);
    run(6 * L16 + 10);
    CHECK(on_n(0)->a == 60 && on_n(1)->a == 67 && on_n(2)->a == 64 && on_n(3)->a == 71 && on_n(4)->a == 67 &&
          on_n(5)->a == 60);
    nev = 0;
    par[0][TP_AP2] = 3;
    seq_key_all_off(&S, 0);
    seq_key(&S, 0, 60, 1, 100);
    seq_key(&S, 0, 64, 1, 100);
    run(4 * L16 + 10);
    CHECK(on_n(0)->a == 60 && on_n(1)->a == 60 && on_n(2)->a == 64 && on_n(3)->a == 64);
    nev = 0;
    par[0][TP_AP2] = 4;
    seq_key_all_off(&S, 0);
    seq_key(&S, 0, 60, 1, 100);
    seq_key(&S, 0, 64, 1, 100);
    seq_key(&S, 0, 67, 1, 100);
    seq_key(&S, 0, 71, 1, 100);
    run(4 * L16 + 10);
    CHECK(on_n(0)->a == 60 && on_n(1)->a == 67 && on_n(2)->a == 64 && on_n(3)->a == 71);
    nev = 0;
    par[0][TP_AP2] = 5;
    seq_key_all_off(&S, 0);
    seq_key(&S, 0, 60, 1, 100);
    seq_key(&S, 0, 64, 1, 100);
    seq_key(&S, 0, 67, 1, 100);
    seq_key(&S, 0, 71, 1, 100);
    run(4 * L16 + 10);
    CHECK(on_n(0)->a == 64 && on_n(1)->a == 60 && on_n(2)->a == 71 && on_n(3)->a == 67);
    /* page 2: length long = 90 % of the period, glide high = 120 ms, stereo wide = +-60 alternating */
    nev = 0;
    par[0][TP_AP2] = 0;
    par[0][TP_AP1] = 2;
    par[0][TP_AP3] = 2;
    par[0][TP_AP4] = 2;
    seq_key_all_off(&S, 0);
    seq_key(&S, 0, 60, 1, 100);
    seq_key(&S, 0, 64, 1, 100);
    run(2 * L16 + 10);
    CHECK(count(E_ON, 0) == 3 && on_n(0)->c == 120 && off_of(on_n(0)) && near(off_of(on_n(0))->t - on_n(0)->t, L16 / 10 * 9, 3));
    CHECK(count(E_PAN, 0) >= 2 && nth(E_PAN, 0, 0)->a == 60 && nth(E_PAN, 0, 1)->a == -60);
    par[0][TP_AP1] = 0;
    nev = 0;
    run(2 * L16);
    CHECK(off_of(on_n(0)) && near(off_of(on_n(0))->t - on_n(0)->t, L16 / 4, 3));
    par[0][TP_AP1] = par[0][TP_AP3] = par[0][TP_AP4] = 0;
    /* while playing, the first note waits for the grid */
    reset();
    par[0][TP_PLAYER] = PL_ARP;
    par[0][TP_P1] = 4;
    par[0][TP_P3] = 1;
    start();
    run(L16 / 2);
    seq_key(&S, 0, 60, 1, 100);
    run(2 * L16);
    CHECK(on_n(0) && near(on_n(0)->t, L16, 2));
    printf("ok %s\n", cur);
}

static void t_maestro(void)
{
    static const uint8_t chord[3] = {60, 64, 67};
    cur = "maestro";
    reset();
    par[0][TP_PLAYER] = PL_STRUM;
    seq_key(&S, 0, 62, 1, 90);            /* a fresh strum: its triad (C E G) on the key, in the scale: D minor */
    run(10);
    CHECK(count(E_ON, 0) == 3 && on_n(1)->a == 65 && on_n(2)->a == 69);
    seq_key(&S, 0, 62, 0, 0);
    run(10);
    nev = 0;
    /* keys held together: the chord (as held: the first key's strum stops), then one key plays it from there */
    seq_key(&S, 0, 57, 1, 90);
    seq_key(&S, 0, 60, 1, 90);
    seq_key(&S, 0, 64, 1, 90);
    run(10);
    CHECK(S.player[0].nchord == 3 && S.player[0].chord[0] == 57 && S.player[0].chord[2] == 64);
    seq_key(&S, 0, 57, 0, 0);
    seq_key(&S, 0, 60, 0, 0);
    seq_key(&S, 0, 64, 0, 0);
    run(10);
    nev = 0;
    seq_key(&S, 0, 62, 1, 90);            /* a minor triad from D */
    run(10);
    CHECK(count(E_ON, 0) == 3 && on_n(0)->a == 62 && on_n(1)->a == 65 && on_n(2)->a == 69);
    seq_key(&S, 0, 62, 0, 0);
    run(10);
    nev = 0;
    seq_maestro_record(&S, 0, chord, 3);
    seq_key(&S, 0, 62, 1, 90);
    run(10);
    CHECK(count(E_ON, 0) == 3 && on_n(0)->a == 62 && on_n(1)->a == 65 && on_n(2)->a == 69 && on_n(0)->b == 90);
    seq_key(&S, 0, 62, 0, 0);
    run(10);
    CHECK(count(E_OFF, 0) == 3);
    {                                             /* the harmonic field of C major from C E G: F major, B diminished;
                                                   * a key outside the scale, the chord in parallel (C#: C# F G#) */
        static const struct { int key, n1, n2; } HF[3] = {{65, 69, 72}, {71, 74, 77}, {61, 65, 68}};
        for (int k = 0; k < 3; k++) {
            nev = 0;
            seq_key(&S, 0, HF[k].key, 1, 90);
            run(10);
            CHECK(count(E_ON, 0) == 3 && on_n(0)->a == HF[k].key && on_n(1)->a == HF[k].n1 && on_n(2)->a == HF[k].n2);
            seq_key(&S, 0, HF[k].key, 0, 0);
            run(10);
        }
        nev = 0;                                  /* a learned C7 (C E G Bb): G gives G7 (G B D F), the Bb's flat kept */
        {
            static const uint8_t C7[4] = {60, 64, 67, 70};
            seq_maestro_record(&S, 0, C7, 4);
        }
        seq_key(&S, 0, 67, 1, 90);
        run(10);
        CHECK(count(E_ON, 0) == 4 && on_n(1)->a == 71 && on_n(2)->a == 74 && on_n(3)->a == 77);
        seq_key(&S, 0, 67, 0, 0);
        run(10);
        seq_maestro_record(&S, 0, chord, 3);
    }
    /* roll 63: a note per sequencer step of the track (a sixteenth at scale 1), 21 = a third of that */
    nev = 0;
    par[0][TP_P1] = 63;
    seq_key(&S, 0, 60, 1, 90);
    run(3 * L16);
    CHECK(count(E_ON, 0) == 3 && near(on_n(1)->t - on_n(0)->t, L16, 2) && near(on_n(2)->t - on_n(1)->t, L16, 2));
    seq_key(&S, 0, 60, 0, 0);
    nev = 0;
    par[0][TP_P1] = 21;
    P.pat[0][0].scale = TS_2;
    seq_key(&S, 0, 60, 1, 90);
    run(3 * L16);
    CHECK(count(E_ON, 0) == 3 && near(on_n(1)->t - on_n(0)->t, 2 * L16 / 3, 3));
    P.pat[0][0].scale = TS_1;
    seq_key(&S, 0, 60, 0, 0);
    /* down, and up-down alternating per strum */
    nev = 0;
    par[0][TP_P2] = 1;
    seq_key(&S, 0, 60, 1, 90);
    run(3 * L16);
    CHECK(on_n(0)->a == 67 && on_n(1)->a == 64 && on_n(2)->a == 60);
    seq_key(&S, 0, 60, 0, 0);
    nev = 0;
    par[0][TP_P2] = 2;
    seq_key(&S, 0, 60, 1, 90);
    run(3 * L16);
    seq_key(&S, 0, 60, 0, 0);
    seq_key(&S, 0, 60, 1, 90);
    run(3 * L16);
    CHECK(on_n(0)->a == 60 && on_n(2)->a == 67 && on_n(3)->a == 67 && on_n(5)->a == 60);
    seq_key(&S, 0, 60, 0, 0);
    /* eight notes in the buffer */
    nev = 0;
    par[0][TP_P1] = 0;
    par[0][TP_P2] = 0;
    {
        static const uint8_t eight[8] = {48, 52, 55, 59, 60, 64, 67, 71};
        seq_maestro_record(&S, 0, eight, 8);
    }
    seq_key(&S, 0, 48, 1, 90);
    run(10);
    CHECK(count(E_ON, 0) == 8 && on_n(7)->a == 71);
    seq_key(&S, 0, 48, 0, 0);
    run(10);
    seq_maestro_record(&S, 0, chord, 3);
    nev = 0;
    par[0][TP_P1] = 63;
    seq_key(&S, 0, 60, 1, 90);
    run(3 * L16);
    /* hold: the chord stays after the key */
    par[0][TP_P4] = 1;
    seq_key(&S, 0, 60, 0, 0);
    run(10);
    CHECK(count(E_OFF, 0) == 0);
    seq_key(&S, 0, 65, 1, 90);
    run(10);
    CHECK(count(E_OFF, 0) == 3);
    printf("ok %s\n", cur);
}

static void t_player_switch(void)     /* the player changed while its notes sound: they go */
{
    cur = "player_switch";
    reset();
    par[0][TP_PLAYER] = PL_STRUM;
    seq_key(&S, 0, 60, 1, 90);
    run(10);
    CHECK(count(E_ON, 0) == 3 && count(E_OFF, 0) == 0);
    par[0][TP_PLAYER] = PL_OFF;
    run(10);
    CHECK(count(E_OFF, 0) == 3);
    seq_key(&S, 0, 60, 0, 0);
    run(10);
    /* direct keys, then an arp: the key's note goes */
    nev = 0;
    seq_key(&S, 0, 62, 1, 90);
    run(10);
    CHECK(count(E_ON, 0) == 1);
    par[0][TP_PLAYER] = PL_ARP;
    run(10);
    CHECK(count(E_OFF, 0) == 1);
    seq_key(&S, 0, 62, 0, 0);
    run(10);
    /* a latched arp, latch turned off with no key down: it stops */
    nev = 0;
    par[0][TP_P1] = 4;
    par[0][TP_P4] = 1;
    seq_key(&S, 0, 64, 1, 90);
    seq_key(&S, 0, 64, 0, 0);
    run(3 * L16);
    CHECK(count(E_ON, 0) >= 2);
    par[0][TP_P4] = 0;
    run(10);
    nev = 0;
    run(3 * L16);
    CHECK(count(E_ON, 0) == 0);
    printf("ok %s\n", cur);
}

static void t_step_players(void)      /* the arp and the strum play the steps' notes too */
{
    ev_t *a, *b, *c;
    cur = "step_players";
    reset();
    par[0][TP_PLAYER] = PL_ARP;
    par[0][TP_P1] = 4;                /* 1/16 */
    par[0][TP_P3] = 1;
    st(0, 0)->note[0] = 60;
    st(0, 0)->note[1] = 64;
    st(0, 0)->note[2] = 67;
    st(0, 0)->vel = 100;
    st(0, 0)->len = 16;               /* four steps */
    start();
    run(8 * L16);
    CHECK(count(E_ON, 0) == 4 && on_n(0)->a == 60 && on_n(1)->a == 64 && on_n(2)->a == 67 && on_n(3)->a == 60);
    CHECK(near(on_n(0)->t, 0, 2) && near(on_n(3)->t, 3 * L16 + 2, 4));
    CHECK(count(E_OFF, 0) == 4);
    stop();
    /* a key held joins the step's notes */
    nev = 0;
    start();
    seq_key(&S, 0, 72, 1, 100);
    run(4 * L16 - L16 / 2);
    CHECK(count(E_ON, 0) == 4 && on_n(3)->a == 72);
    seq_key(&S, 0, 72, 0, 0);
    stop();
    /* strum: a step's chord rolled, all ending together; one note: the strum's chord from it */
    reset();
    par[0][TP_PLAYER] = PL_STRUM;
    par[0][TP_P1] = 21;               /* a third of a step between the notes */
    st(0, 0)->note[0] = 67;
    st(0, 0)->note[1] = 60;
    st(0, 0)->note[2] = 64;
    st(0, 0)->vel = 100;
    st(0, 0)->len = 8;                /* two steps */
    st(0, 4)->note[0] = 62;
    st(0, 4)->vel = 100;
    start();
    run(3 * L16);
    CHECK(count(E_ON, 0) == 3);
    a = on_n(0), b = on_n(1), c = on_n(2);
    CHECK(a->a == 60 && b->a == 64 && c->a == 67);
    CHECK(near(b->t - a->t, L16 / 3u, 4) && near(c->t - b->t, L16 / 3u, 4));
    CHECK(off_of(a) && off_of(c) && near(off_of(a)->t, 2 * L16 + 2, 6) && near(off_of(c)->t, 2 * L16 + 2, 6));
    nev = 0;
    run(3 * L16);                                 /* (step 5's D: the chord on it, in the scale: D minor) */
    CHECK(count(E_ON, 0) == 3 && on_n(0)->a == 62 && on_n(1)->a == 65 && on_n(2)->a == 69);
    printf("ok %s\n", cur);
}

static void t_hold_player(void)
{
    cur = "hold-player";
    reset();
    par[0][TP_PLAYER] = PL_HOLD;
    seq_key(&S, 0, 60, 1, 100);
    seq_key(&S, 0, 60, 0, 0);
    run(10);
    CHECK(count(E_ON, 0) == 1 && count(E_OFF, 0) == 0);
    seq_key(&S, 0, 62, 1, 100);
    run(10);
    CHECK(count(E_ON, 0) == 2 && count(E_OFF, 0) == 1 && nth(E_OFF, 0, 0)->tag == on_n(0)->tag);
    stop();                           /* the transport stops: everything held goes */
    CHECK(count(E_OFF, 0) == 2);
    printf("ok %s\n", cur);
}

/* ------------------------------------------------------------ punch-in --- */
static void t_punch_keys(void)
{
    int k;
    cur = "punch-keys";
    CHECK(punch_of_key(0) == PX_MUTE && punch_of_key(1) == PX_WARBLE && punch_of_key(2) == PX_STUTTER &&
          punch_of_key(4) == PX_REPEAT && punch_of_key(6) == PX_ECHO && punch_of_key(7) == PX_BUILD &&
          punch_of_key(9) == PX_FLOOR && punch_of_key(11) == PX_JUMBLE);
    CHECK(punch_of_key(3) == PX_PAN && punch_of_key(5) == PX_OCTAVE && punch_of_key(8) == PX_CHOP &&
          punch_of_key(10) == PX_RING);
    CHECK(punch_of_key(12) == PX_MEL + PX_MUTE && punch_of_key(23) == PX_MEL + PX_JUMBLE);
    CHECK(punch_of_key(24) == -1 && punch_of_key(26) == -1 && punch_of_key(-1) == -1);
    for (k = 0; k < 24; k++)
        CHECK(punch_key_of(punch_of_key(k)) == k);
    CHECK(punch_key_of(12) == -1 && punch_key_of(15) == -1 && PX_VALID(27) && !PX_VALID(28) && !PX_VALID(12));
    CHECK(strcmp(PUNCH_NAME[PX_MUTE], "mute") == 0 && strcmp(PUNCH_NAME[PX_RING], "ring") == 0);
    reset();
    par[1][TP_ENGINE] = EG_DRUMLINE;
    CHECK(punch_group_of(&S, 0) == 1 && punch_group_of(&S, 1) == 0);
    printf("ok %s\n", cur);
}

static void t_duck(void)
{
    int i;
    cur = "duck";
    reset();
    par[1][TP_ENGINE] = EG_DRUMLINE;
    for (i = 0; i < 16; i++) {
        note(0, i, 60, 100);
        note(1, i, 1, 100);
        note(2, i, 60, 100);
    }
    start();
    run(10);
    seq_punch_key(&S, PX_MEL + PX_MUTE, 1, -1);     /* the melodic group: the drums go on */
    run(2 * L16);
    CHECK(count(E_ON, 0) == 1 && count(E_DRUM, 1) == 3);
    CHECK(count(E_PUNCH, -1) == 1 && nth(E_PUNCH, -1, 0)->a == 0 && nth(E_PUNCH, -1, 0)->b == (1 << PX_MUTE));
    CHECK(seq_punch_mask(&S, 0) == (1u << PX_MUTE) && seq_punch_mask(&S, 1) == 0);
    seq_punch_key(&S, PX_MEL + PX_MUTE, 0, -1);
    seq_punch_key(&S, PX_MUTE, 1, -1);              /* the percussive group */
    nev = 0;
    run(2 * L16);
    CHECK(count(E_ON, 0) == 2 && count(E_DRUM, 1) == 0 && nth(E_PUNCH, -1, 0)->a == 1 && nth(E_PUNCH, -1, 0)->b == 0);
    seq_punch_key(&S, PX_MUTE, 0, -1);
    /* aimed at one track: the lower octave's bit on a synth track reaches that track only */
    seq_punch_key(&S, PX_MUTE, 1, 0);
    nev = 0;
    run(2 * L16);
    CHECK(count(E_ON, 0) == 0 && count(E_ON, 2) == 2 && count(E_DRUM, 1) == 2);
    CHECK(seq_punch_mask(&S, 0) == (1u << PX_MUTE) && seq_punch_mask(&S, 2) == 0);
    seq_punch_key(&S, PX_MUTE, 0, 0);
    nev = 0;
    run(2 * L16);
    CHECK(count(E_ON, 0) == 2);
    /* the keys play through it: a held key on a ducked track is silent, the hold player too */
    seq_punch_key(&S, PX_MEL + PX_MUTE, 1, -1);
    run(10);
    stop();
    printf("ok %s\n", cur);
}

static void t_loops(void)
{
    int i;
    cur = "loops";
    reset();
    for (i = 0; i < 16; i++)
        note(0, i, 60 + i, 100);
    start();
    run(5 * L16 + 10);                /* on step 6 (index 5): loop 1 repeats steps 6 and 7 */
    seq_punch_key(&S, PX_MEL + PX_STUTTER, 1, -1);
    run(6 * L16);
    CHECK(on_n(6)->a == 66 && on_n(7)->a == 65 && on_n(8)->a == 66 && on_n(9)->a == 65 && on_n(10)->a == 66);
    CHECK(near(on_n(7)->t, 7 * L16 + 3, 2));
    seq_punch_key(&S, PX_MEL + PX_STUTTER, 0, -1);
    run(2 * L16);
    CHECK(on_n(12)->a == 72 && on_n(13)->a == 73);   /* released after the loop's step at 12: on at the grid's steps 13, 14 */
    /* loop 2: a beat */
    reset();
    for (i = 0; i < 16; i++)
        note(0, i, 60 + i, 100);
    start();
    run(5 * L16 + 10);
    seq_punch_key(&S, PX_MEL + PX_REPEAT, 1, -1);
    run(8 * L16);
    CHECK(on_n(6)->a == 66 && on_n(7)->a == 67 && on_n(8)->a == 68 && on_n(9)->a == 65 && on_n(10)->a == 66 &&
          on_n(13)->a == 65);
    seq_punch_key(&S, PX_MEL + PX_REPEAT, 0, -1);
    /* a drum track loops the same way, with the span in its own steps (scale 1/2: four steps) */
    reset();
    par[0][TP_ENGINE] = EG_DRUMLINE;
    P.pat[0][0].scale = TS_HALF;
    for (i = 0; i < 16; i++)
        note(0, i, 1 + i % 11, 100);
    start();
    run(4 * L16 + 10);                /* step 9 (index 8) */
    seq_punch_key(&S, PX_STUTTER, 1, -1);
    run(4 * L16);
    CHECK(nth(E_DRUM, 0, 9)->a == 9 && nth(E_DRUM, 0, 12)->a == 8 && nth(E_DRUM, 0, 16)->a == 8);
    printf("ok %s\n", cur);
}

static int last_step(int trk)
{
    int i;
    for (i = nev - 1; i >= 0; i--)
        if (ev[i].kind == E_STEP && ev[i].trk == trk)
            return ev[i].a;
    return -1;
}
/* a loop throw let go: the track goes on at the step it would be at had it never looped (a track beside it, with the
 * same pattern, shows where), and its passes count on from there (conditions, weaves) */
static void t_loop_release(void)
{
    static const int fxs[3] = {PX_STUTTER, PX_REPEAT, PX_ECHO};
    static const struct { int len, scale; } cfg[4] = {{16, TS_1}, {12, TS_1}, {16, TS_2}, {20, TS_HALF}};
    int c, f, h, r, i;
    cur = "loop release";
    for (c = 0; c < 4; c++)
        for (f = 0; f < 3; f++)
            for (h = 0; h < 5; h++) {
                uint32_t punch_at = (uint32_t)(3 * c + 5) * L16 + 10u, hold = (uint32_t)(5 + 2 * h) * L16 / 2u + 40u * (uint32_t)h;
                reset();
                for (i = 0; i < 2; i++) {
                    P.pat[i][0].len = (uint8_t)cfg[c].len;
                    P.pat[i][0].scale = (uint8_t)cfg[c].scale;
                }
                start();
                run_to(punch_at);
                seq_punch_key(&S, PX_MEL + fxs[f], 1, 0);
                run(hold);
                seq_punch_key(&S, PX_MEL + fxs[f], 0, 0);
                run(2u * L16 + 100u);          /* (it steps on its own grid from the next step: from there it agrees) */
                for (r = 0; r < 12; r++) {
                    run(L16 / 3u + 7u);
                    if (last_step(0) != last_step(1) || S.t[0].pass != S.t[1].pass) {
                        printf("cfg %d fx %d hold %d at %d: steps %d / %d, pass %d / %d\n", c, f, h, r, last_step(0),
                               last_step(1), S.t[0].pass, S.t[1].pass);
                        fail("a released loop is out of step");
                    }
                }
            }
    {                                             /* LOOP THROWS "drift": it goes on from where the loop left it */
        int apart = 0;
        reset();
        S.loop_free = 1;
        for (i = 0; i < 2; i++)
            P.pat[i][0].len = 16;
        start();
        run_to(5u * L16 + 10u);
        seq_punch_key(&S, PX_MEL + PX_STUTTER, 1, 0);
        run(5u * L16 + L16 / 2u);
        seq_punch_key(&S, PX_MEL + PX_STUTTER, 0, 0);
        run(2u * L16 + 100u);
        for (r = 0; r < 12; r++) {
            run(L16 / 3u + 7u);
            apart |= last_step(0) != last_step(1);
        }
        CHECK(apart);
    }
    printf("ok %s\n", cur);
}

static void t_echo(void)
{
    cur = "echo";
    reset();
    note(0, 0, 60, 100);
    note(0, 1, 62, 100);
    start();
    run(10);
    seq_punch_key(&S, PX_MEL + PX_ECHO, 1, -1);     /* steps 1 and 2 repeat, each pass 70 % as loud */
    run(6 * L16);
    CHECK(count(E_ON, 0) >= 6 && on_n(0)->b == 100 && on_n(1)->b == 100 && on_n(2)->b == 69 && on_n(3)->b == 69);
    CHECK(on_n(4)->b == 48 && on_n(5)->b == 48 && on_n(6)->b == 33);
    seq_punch_key(&S, PX_MEL + PX_ECHO, 0, -1);
    nev = 0;
    run(17 * L16);
    CHECK(count(E_ON, 0) >= 2 && on_n(0)->b == 100 && on_n(0)->a == 60 && on_n(1)->a == 62);   /* the grid's step 1 again, at full velocity */
    /* drums too */
    reset();
    par[0][TP_ENGINE] = EG_DRUMLINE;
    note(0, 0, 1, 100);
    seq_punch_key(&S, PX_ECHO, 1, -1);
    start();
    run(4 * L16 + 10);
    CHECK(count(E_DRUM, 0) == 3 && nth(E_DRUM, 0, 0)->b == 100 && nth(E_DRUM, 0, 1)->b == 69 &&
          nth(E_DRUM, 0, 2)->b == 48);
    printf("ok %s\n", cur);
}

static void t_fills(void)
{
    int i, kicks = 0, snares = 0, hats = 0;
    cur = "fills";
    reset();
    par[0][TP_ENGINE] = EG_DRUMLINE;
    note(0, 0, 5, 100);               /* the pattern's own hits are replaced while the fill runs */
    seq_punch_key(&S, PX_BUILD, 1, -1);
    start();
    run(16 * L16);
    for (i = 0; i < nev; i++)
        if (ev[i].kind == E_DRUM) {
            kicks += ev[i].a == 0;
            snares += ev[i].a == 1;
            hats += ev[i].a == 2;
        }
    CHECK(count(E_DRUM, 0) == 12 && snares == 12 && kicks == 0);
    CHECK(near(nth(E_DRUM, 0, 1)->t, 2 * L16 + 1, 2) && near(nth(E_DRUM, 0, 4)->t, 8 * L16 + 4, 2) &&
          near(nth(E_DRUM, 0, 5)->t, 9 * L16 + 4, 2) && nth(E_DRUM, 0, 5)->b == 110);
    seq_punch_key(&S, PX_BUILD, 0, -1);
    nev = 0;
    run(16 * L16);
    CHECK(count(E_DRUM, 0) == 1 && nth(E_DRUM, 0, 0)->a == 4);
    /* fill 2: a kick on the beats, hats in sixteenths */
    seq_punch_key(&S, PX_FLOOR, 1, -1);
    nev = 0;
    run(16 * L16);
    kicks = snares = hats = 0;
    for (i = 0; i < nev; i++)
        if (ev[i].kind == E_DRUM) {
            kicks += ev[i].a == 0;
            snares += ev[i].a == 1;
            hats += ev[i].a == 2;
        }
    CHECK(kicks == 4 && hats == 16 && snares == 0);
    seq_punch_key(&S, PX_FLOOR, 0, -1);
    /* the random fill: sounds 1..3 on most sixteenths */
    seq_punch_key(&S, PX_JUMBLE, 1, -1);
    nev = 0;
    run(32 * L16);
    CHECK(count(E_DRUM, 0) > 12 && count(E_DRUM, 0) < 32);
    for (i = 0; i < nev; i++)
        if (ev[i].kind == E_DRUM)
            CHECK(ev[i].a <= 2 && ev[i].b >= 70 && ev[i].b <= 110);
    seq_punch_key(&S, PX_JUMBLE, 0, -1);
    /* a synth track: its pattern's notes (60 67 64, distinct and sorted: 60 64 67) in a new rhythm */
    reset();
    note(0, 0, 60, 100);
    note(0, 2, 67, 100);
    note(0, 4, 64, 100);
    note(0, 6, 60, 100);
    seq_punch_key(&S, PX_MEL + PX_BUILD, 1, -1);
    start();
    run(8 * L16 - L16 / 2);
    CHECK(count(E_ON, 0) == 8 && on_n(0)->a == 60 && on_n(1)->a == 64 && on_n(2)->a == 67 && on_n(3)->a == 60 &&
          near(on_n(1)->t, L16, 1) && near(nth(E_OFF, 0, 0)->t, L16 / 2, 1));
    seq_punch_key(&S, PX_MEL + PX_BUILD, 0, -1);
    seq_punch_key(&S, PX_MEL + PX_FLOOR, 1, -1);
    nev = 0;
    run(8 * L16);
    CHECK(on_n(0)->a == 60 && on_n(1)->a == 67 && on_n(2)->a == 64 && on_n(3)->a == 60 && on_n(4)->a == 67);
    seq_punch_key(&S, PX_MEL + PX_FLOOR, 0, -1);
    seq_punch_key(&S, PX_MEL + PX_JUMBLE, 1, -1);
    nev = 0;
    run(32 * L16);
    CHECK(count(E_ON, 0) > 12 && count(E_ON, 0) < 32);
    for (i = 0; i < count(E_ON, 0); i++)
        CHECK(on_n(i)->a == 60 || on_n(i)->a == 64 || on_n(i)->a == 67);
    seq_punch_key(&S, PX_MEL + PX_JUMBLE, 0, -1);
    /* the fill's notes go through the brain of a routed track, and pitch takes them down an octave */
    par[0][TP_BRAIN] = 1;
    seq_brain_key(&S, 62, 1);         /* +1 degree */
    seq_punch_key(&S, PX_MEL + PX_BUILD, 1, -1);
    seq_punch_key(&S, PX_MEL + PX_OCTAVE, 1, -1);
    nev = 0;
    run(3 * L16);
    CHECK(on_n(0)->a == 62 - 12 && on_n(1)->a == 65 - 12 && on_n(2)->a == 69 - 12);
    printf("ok %s\n", cur);
}

/* a slice track is percussive for the throws: the drums' octave of keys, the drum fills on its own slices (the
 * downbeat's for the kick, the backbeat's for the snare, an off-beat's for the hat), its notes kept under the
 * octave throw (the engine raises its pitch); a sample track stays melodic */
static void t_slice_throws(void)
{
    int i, k = 0, sn = 0, h = 0;
    cur = "slice-throws";
    reset();
    par[0][TP_ENGINE] = EG_SLICE;
    par[1][TP_ENGINE] = EG_SAMPLE;
    for (i = 0; i < 8; i++)
        note(0, 2 * i, 60 + i, 100);  /* a break's eight slices in eighths */
    note(1, 0, 60, 100);
    CHECK(punch_group_of(&S, 0) == 0 && punch_group_of(&S, 1) == 1);
    seq_punch_key(&S, PX_OCTAVE, 1, -1);
    seq_punch_key(&S, PX_MEL + PX_OCTAVE, 1, -1);
    start();
    run(10);
    CHECK(on_n(0)->a == 60 && nth(E_ON, 1, 0)->a == 48);   /* the slice's note stays, the sample's goes down */
    CHECK(seq_punch_mask(&S, 0) == (1u << PX_OCTAVE) && seq_punch_mask(&S, 1) == (1u << PX_OCTAVE));
    seq_punch_key(&S, PX_OCTAVE, 0, -1);
    run(10);
    CHECK(seq_punch_mask(&S, 0) == 0);
    seq_punch_key(&S, PX_MEL + PX_OCTAVE, 0, -1);
    stop();
    /* build: the snare's slice (the backbeat's, 62) in eighths, then sixteenths */
    seq_punch_key(&S, PX_BUILD, 1, -1);
    nev = 0;
    start();
    run(16 * L16);
    CHECK(count(E_ON, 0) == 12 && count(E_ON, 1) == 1);
    for (i = 0; i < 12; i++)
        CHECK(on_n(i)->a == 62);
    CHECK(near(off_of(on_n(0))->t - on_n(0)->t, L16 / 2, 1));
    seq_punch_key(&S, PX_BUILD, 0, -1);
    /* floor: the kick's slice (60) on the beats, the hat's (61) on the other sixteenths */
    seq_punch_key(&S, PX_FLOOR, 1, -1);
    nev = 0;
    run(16 * L16);
    for (i = 0; i < count(E_ON, 0); i++) {
        k += on_n(i)->a == 60;
        h += on_n(i)->a == 61;
        sn += on_n(i)->a == 62;
    }
    CHECK(k == 4 && h == 12 && sn == 0 && count(E_ON, 0) == 16);
    seq_punch_key(&S, PX_FLOOR, 0, -1);
    /* jumble: the three slices */
    seq_punch_key(&S, PX_JUMBLE, 1, -1);
    nev = 0;
    run(32 * L16);
    CHECK(count(E_ON, 0) > 12 && count(E_ON, 0) < 32);
    for (i = 0; i < count(E_ON, 0); i++)
        CHECK(on_n(i)->a >= 60 && on_n(i)->a <= 62);
    seq_punch_key(&S, PX_JUMBLE, 0, -1);
    /* the melodic group's build leaves it alone; a live throw aimed at it (an upper key from its page) is its own */
    seq_punch_key(&S, PX_MEL + PX_BUILD, 1, -1);
    nev = 0;
    run(16 * L16);
    CHECK(count(E_ON, 0) == 8 && on_n(1)->a == 61);
    seq_punch_key(&S, PX_MEL + PX_BUILD, 0, -1);
    seq_punch_key(&S, PX_MEL + PX_BUILD, 1, 0);
    nev = 0;
    run(16 * L16);
    CHECK(count(E_ON, 0) == 12 && on_n(0)->a == 62);
    seq_punch_key(&S, PX_MEL + PX_BUILD, 0, 0);
    /* an empty slice pattern's fills are silent */
    stop();
    reset();
    par[0][TP_ENGINE] = EG_SLICE;
    seq_punch_key(&S, PX_FLOOR, 1, -1);
    start();
    run(16 * L16);
    CHECK(count(E_ON, 0) == 0);
    printf("ok %s\n", cur);
}

/* drum offs: the gated and loop play modes end with the step's length, the key's release, or stop */
static void t_drum_off(void)
{
    cur = "drum-off";
    reset();
    par[0][TP_ENGINE] = EG_DRUMLINE;
    note(0, 0, 3, 100);               /* the pattern's 50 %: half a step */
    note(0, 4, 1, 100);
    st(0, 4)->len = 8;                /* two steps */
    note(0, 8, 2, 100);
    st(0, 8)->comp = SC_MULTIPLY;
    st(0, 8)->cval = 1;               /* two hits: each a half step */
    start();
    run(10 * L16);
    CHECK(count(E_DOFF, 0) == 4);
    CHECK(nth(E_DOFF, 0, 0)->a == 2 && near(nth(E_DOFF, 0, 0)->t, L16 / 2, 1));
    CHECK(nth(E_DOFF, 0, 1)->a == 0 && near(nth(E_DOFF, 0, 1)->t, 6 * L16 + 3, 2));
    CHECK(nth(E_DOFF, 0, 2)->a == 1 && near(nth(E_DOFF, 0, 2)->t, 8 * L16 + L16 / 2 + 4, 2) &&
          nth(E_DOFF, 0, 3)->a == 1 && near(nth(E_DOFF, 0, 3)->t, 9 * L16 + 4, 2));
    /* a key holds its sound until released; stop lets go of what is down */
    nev = 0;
    seq_key(&S, 0, 5, 1, 100);
    run(3 * L16);
    CHECK(count(E_DRUM, 0) == 1 && count(E_DOFF, 0) == 0);
    seq_key(&S, 0, 5, 0, 0);
    run(10);
    CHECK(count(E_DOFF, 0) == 1 && nth(E_DOFF, 0, 0)->a == 4);
    seq_key(&S, 0, 7, 1, 100);
    seq_key(&S, 0, 8, 1, 100);
    run(10);
    nev = 0;
    stop();
    CHECK(count(E_DOFF, 0) == 2 && nth(E_DOFF, 0, 0)->a == 6 && nth(E_DOFF, 0, 1)->a == 7);
    /* fills get offs too: a hat every sixteenth, off half a sixteenth later */
    nev = 0;
    seq_punch_key(&S, PX_FLOOR, 1, -1);
    start();
    run(2 * L16);
    CHECK(count(E_DOFF, 0) >= 3 && nth(E_DOFF, 0, 0)->a == 0 && near(nth(E_DOFF, 0, 1)->t, L16 / 2, 1));
    printf("ok %s\n", cur);
}

static void t_pitch_short_long(void)
{
    cur = "pitch-short-long";
    reset();
    par[1][TP_ENGINE] = EG_DRUMLINE;
    note(0, 0, 60, 100);
    note(1, 0, 3, 100);
    seq_punch_key(&S, PX_OCTAVE, 1, -1);
    seq_punch_key(&S, PX_MEL + PX_OCTAVE, 1, -1);
    start();
    run(10);
    CHECK(on_n(0)->a == 48 && nth(E_DRUM, 1, 0)->c == 12);   /* synths down, drums up */
    seq_punch_key(&S, PX_OCTAVE, 0, -1);
    seq_punch_key(&S, PX_MEL + PX_OCTAVE, 0, -1);
    seq_punch_key(&S, PX_CHOP, 1, -1);
    seq_punch_key(&S, PX_MEL + PX_CHOP, 1, -1);
    nev = 0;
    run_to(16 * L16 + 20);
    run(L16);
    CHECK(on_n(0)->a == 60 && near(off_of(on_n(0))->t - on_n(0)->t, L16 / 4, 2));   /* a quarter step */
    CHECK((nth(E_DRUM, 1, 0)->tag & 1u) == 1u && nth(E_DRUM, 1, 0)->c == 0);
    seq_punch_key(&S, PX_CHOP, 0, -1);
    seq_punch_key(&S, PX_MEL + PX_CHOP, 0, -1);
    seq_punch_key(&S, PX_RING, 1, -1);
    seq_punch_key(&S, PX_MEL + PX_RING, 1, -1);
    nev = 0;
    run_to(32 * L16 + 20);
    run(3 * L16);
    CHECK(near(off_of(on_n(0))->t - on_n(0)->t, 2 * L16, 2));                        /* two steps */
    CHECK((nth(E_DRUM, 1, 0)->tag & 2u) == 2u);
    /* filter / vibrato and stereo only reach the engine's masks */
    seq_punch_key(&S, PX_WARBLE, 1, -1);
    seq_punch_key(&S, PX_MEL + PX_PAN, 1, -1);
    run(10);
    CHECK(nth(E_PUNCH, -1, count(E_PUNCH, -1) - 1)->a == ((1 << PX_RING) | (1 << PX_WARBLE)) &&
          nth(E_PUNCH, -1, count(E_PUNCH, -1) - 1)->b == ((1 << PX_RING) | (1 << PX_PAN)));
    printf("ok %s\n", cur);
}

/* sequenced punch-ins, and recording held punch keys into the PUNCH track */
/* a muted track's steps: nothing plays, each would-be hit goes to the sink's ghost (the master's pump) at the
 * step's velocity; a playing track sends none */
static void t_kit11(void)            /* a kit step holds all eleven sounds, and plays them in key order */
{
    cur = "kit11";
    reset();
    par[0][TP_ENGINE] = EG_DRUMLINE;
    for (int k = 1; k <= NDRUM; k++)
        note(0, 0, k, 100);
    CHECK(kit_mask(st(0, 0)) == KIT_ALL && !step_empty(st(0, 0)) && kit_first(st(0, 0)) == 1);
    start();
    run(10);
    CHECK(count(E_DRUM, 0) == NDRUM && nth(E_DRUM, 0, 0)->a == 0 && nth(E_DRUM, 0, NDRUM - 1)->a == NDRUM - 1);
    stop();
    kit_toggle(st(0, 0), 11);
    CHECK(!kit_has(st(0, 0), 11) && kit_has(st(0, 0), 10) && st(0, 0)->note[2] == 0 && st(0, 0)->note[3] == 0);
    {                                             /* a sound's own velocity (LANE_VEL0 + k): the snare at 50, the rest
                                                   * the step's 100; the scene's kit mutes: the hat silent */
        int ln = lane_alloc(P.lane, 0, 0, (int)(LANE_VEL0 + 1u));
        P.lane[ln].val[0] = 50;
        S.kmute[0] = 1u << 2;
        nev = 0;
        start();
        run(10);
        CHECK(count(E_DRUM, 0) == NDRUM - 2 && nth(E_DRUM, 0, 0)->a == 0 && nth(E_DRUM, 0, 0)->b == 100);
        CHECK(nth(E_DRUM, 0, 1)->a == 1 && nth(E_DRUM, 0, 1)->b == 50 && nth(E_DRUM, 0, 2)->a == 3);
        stop();
        S.kmute[0] = 0;
    }
    printf("ok %s\n", cur);
}

static void t_weave_live(void)        /* the sequencer weaves live: T1's P1 with T2's P1 by rhythm, and back */
{
    cur = "weave-live";
    reset();
    note(0, 0, 60, 100);                          /* T1: hits on 1 and 3 */
    note(0, 2, 60, 100);
    note(1, 1, 72, 100);                          /* T2 (the one woven in): 72 then 74 */
    note(1, 5, 74, 100);
    S.mute = 1u << 1;                             /* (T2 itself silent) */
    S.wv_from[0] = 2;                             /* T1 weaves with T2's P1, by rhythm */
    S.wv_pat[0] = 0;
    S.wv_rule[0] = WV_RHYTHM;
    S.wv_live = 1u;
    start();
    run(4 * L16);
    CHECK(count(E_ON, 0) == 2 && on_n(0)->a == 72 && nth(E_ON, 0, 1)->a == 74);
    stop();
    S.wv_live = 0;                                /* let go: its own notes */
    nev = 0;
    start();
    run(4 * L16);
    CHECK(count(E_ON, 0) == 2 && on_n(0)->a == 60);
    stop();
    printf("ok %s\n", cur);
}

static void t_ghost(void)
{
    cur = "ghost";
    reset();
    par[0][TP_ENGINE] = EG_DRUMLINE;
    note(0, 0, 1, 120);                           /* the kick (sound 0) */
    note(0, 4, 2, 90);                            /* snare and kick */
    note(0, 4, 1, 90);
    note(1, 0, 60, 70);                           /* a melodic chord on track 2 */
    st(1, 0)->note[1] = 64;
    S.mute = 3;
    start();
    run(6 * L16);
    CHECK(count(E_DRUM, 0) == 0 && count(E_ON, 1) == 0);
    CHECK(count(E_GHOST, 0) == 3 && nth(E_GHOST, 0, 0)->a == 0 && nth(E_GHOST, 0, 0)->b == 120);
    CHECK(nth(E_GHOST, 0, 1)->a == 0 && nth(E_GHOST, 0, 2)->a == 1 && nth(E_GHOST, 0, 2)->b == 90);   /* by key */
    CHECK(near(nth(E_GHOST, 0, 1)->t, 4 * L16 + 2, 3));
    CHECK(count(E_GHOST, 1) == 1 && nth(E_GHOST, 1, 0)->a == -1 && nth(E_GHOST, 1, 0)->b == 70);
    stop();
    S.mute = 0;
    nev = 0;
    start();
    run(6 * L16);
    CHECK(count(E_GHOST, -1) == 0 && count(E_DRUM, 0) == 3 && count(E_ON, 1) == 2);
    /* a mute throw (the punch track's) ghosts too */
    reset();
    note(0, 0, 60, 100);
    note(0, 1, 60, 100);
    P.pat[AUX_PUNCH][0].st[1].note[0] = 1 + PX_MEL + PX_MUTE;
    P.pat[AUX_PUNCH][0].st[1].vel = 100;
    P.pat[AUX_PUNCH][0].st[1].len = 4;
    start();
    run(2 * L16 + 100);
    CHECK(count(E_ON, 0) == 1 && count(E_GHOST, 0) == 1);
    printf("ok %s\n", cur);
}

static void t_punch_seq(void)
{
    cur = "punch-seq";
    reset();
    note(0, 0, 60, 100);
    note(0, 1, 60, 100);
    note(0, 2, 60, 100);
    P.pat[AUX_PUNCH][0].st[1].note[0] = 1 + PX_MEL + PX_MUTE;   /* the melodic duck on step 2, a step long */
    P.pat[AUX_PUNCH][0].st[1].vel = 100;
    P.pat[AUX_PUNCH][0].st[1].len = 4;
    start();
    run(3 * L16);
    CHECK(count(E_ON, 0) == 2 && near(on_n(1)->t, 2 * L16 + 1, 2));
    CHECK(nth(E_PUNCH, -1, 0) && nth(E_PUNCH, -1, 0)->b == (1 << PX_MUTE) && nth(E_PUNCH, -1, 1)->b == 0);
    /* an invalid value (an idle bit) does nothing */
    P.pat[AUX_PUNCH][0].st[1].note[0] = 1 + 13;
    stop();
    nev = 0;
    start();
    run(3 * L16);
    CHECK(count(E_ON, 0) == 3 && count(E_PUNCH, -1) == 0);
    /* recording: a punch key held two steps lands on the step it started with that length */
    reset();
    S.rec = 1;
    start();
    run(4 * L16 + 10);
    seq_punch_key(&S, PX_MEL + PX_STUTTER, 1, -1);
    run(2 * L16);
    seq_punch_key(&S, PX_MEL + PX_STUTTER, 0, -1);
    run(10);
    CHECK(P.pat[AUX_PUNCH][0].st[4].note[0] == 1 + PX_MEL + PX_STUTTER && P.pat[AUX_PUNCH][0].st[4].len == 8);
    /* a per-track one records the same way, in the track's octave */
    par[1][TP_ENGINE] = EG_DRUMLINE;
    run_to(8 * L16 + 10);
    seq_punch_key(&S, PX_MEL + PX_BUILD, 1, 1);     /* aimed at the drum track: the percussive bit */
    run(L16);
    seq_punch_key(&S, PX_MEL + PX_BUILD, 0, 1);
    run(10);
    CHECK(P.pat[AUX_PUNCH][0].st[8].note[0] == 1 + PX_BUILD && P.pat[AUX_PUNCH][0].st[8].len == 4);
    printf("ok %s\n", cur);
}

static void t_record(void)
{
    cur = "record";
    reset();
    S.rec = 1;
    start();
    run(L16 * 6 / 10);                /* 60 % into step 1: nearer step 2 */
    seq_key(&S, 0, 60, 1, 99);
    CHECK(seq_rec_pos(&S, 0) == 1);
    run(L16 * 5 / 10);                /* held for half a step: a quarter-step pair */
    seq_key(&S, 0, 60, 0, 0);
    run(10);
    CHECK(st(0, 1)->note[0] == 60 && st(0, 1)->vel == 99 && st(0, 1)->nudge == 0 && st(0, 1)->len == 2);
    /* overdub: a second note on the same step, the next pass (60 % into step 1 again) */
    run_to(16 * L16 + L16 * 6 / 10);
    seq_key(&S, 0, 64, 1, 80);
    run(10);
    seq_key(&S, 0, 64, 0, 0);
    run(10);
    CHECK(st(0, 1)->note[0] == 60 && st(0, 1)->note[1] == 64);
    /* quant 0 keeps the timing as a nudge */
    P.pat[0][0].quant = 0;
    run_to(32 * L16 + L16 + L16 * 3 / 10);   /* 30 % into step 2 (index 1) */
    seq_key(&S, 0, 67, 1, 80);
    run(10);
    CHECK(st(0, 1)->note[2] == 67);
    run_to(32 * L16 + 2 * L16 + L16 * 3 / 10);
    seq_key(&S, 0, 62, 1, 80);        /* 30 % into step 3: a fresh step, nudged +7 ticks */
    run(10);
    CHECK(st(0, 2)->note[0] == 62 && st(0, 2)->nudge == 7);
    /* a knob while recording: a lock on the playing step */
    seq_rec_knob(&S, 0, TP_RES, 77);
    CHECK(lane_find(P.lane, 0, 0, TP_RES) >= 0 && P.lane[lane_find(P.lane, 0, 0, TP_RES)].val[2] == 77);
    /* step record */
    stop();
    CHECK(seq_step_rec(&S, 0, 5, 72, 100) == 1 && st(0, 5)->note[0] == 72);
    CHECK(seq_step_rec(&S, 0, 5, 72, 100) == 0 && seq_step_rec(&S, 0, 5, 76, 100) == 1 && st(0, 5)->note[1] == 76);
    /* a step held + keys: the notes toggled */
    {
        static const uint8_t add[2] = {72, 79};
        CHECK(seq_step_add_notes(&S, 0, 5, add, 2, 90) == 2 && st(0, 5)->note[0] == 76 && st(0, 5)->note[1] == 79 &&
              st(0, 5)->vel == 90);
        CHECK(seq_step_add_notes(&S, 0, 9, add, 2, 0) == 2 && st(0, 9)->note[0] == 72 && st(0, 9)->vel == 100);
    }
    /* momentary: rec goes off while a key is held: its length is still written */
    reset();
    start();
    run(L16 * 2);
    seq_rec(&S, 1);
    seq_key(&S, 0, 60, 1, 100);
    run(L16 / 2);
    seq_rec(&S, 0);
    run(L16 / 2);
    seq_key(&S, 0, 60, 0, 0);
    run(10);
    seq_key(&S, 0, 64, 1, 100);       /* rec is off: not recorded */
    run(10);
    seq_key(&S, 0, 64, 0, 0);
    run(10);
    CHECK(st(0, 2)->note[0] == 60 && st(0, 2)->len == 4 && st(0, 2)->note[1] == 0 && st(0, 3)->note[0] == 0);
    printf("ok %s\n", cur);
}

/* live recording the OP-XY way: the transport waits for the first key, which lands on step 1 */
static void t_rec_wait(void)
{
    cur = "rec-wait";
    reset();
    note(1, 0, 67, 100);
    seq_rec(&S, 2);
    start();
    run(3 * L16);
    CHECK(S.playing && S.rec_wait && count(E_ON, -1) == 0 && count(E_STEP, -1) == 0);
    seq_key(&S, 0, 60, 1, 100);
    run(L16 / 2);
    CHECK(S.rec_wait == 0 && count(E_ON, 0) == 1 && on_n(0)->tag == (0x100u | 60u));   /* the key itself, once */
    CHECK(count(E_ON, 1) == 1 && near(nth(E_ON, 1, 0)->t, 3 * L16 + 1, 2));            /* the others start now */
    CHECK(st(0, 0)->note[0] == 60 && st(0, 0)->vel == 100 && st(0, 0)->nudge == 0);
    seq_key(&S, 0, 60, 0, 0);
    run(10);
    CHECK(st(0, 0)->len == 2);
    run_to(3 * L16 + 1 + 16 * L16);  /* the next pass plays the recorded note */
    run(10);
    CHECK(count(E_ON, 0) == 2 && on_n(1)->a == 60 && (on_n(1)->tag & 0x10000u));
    /* a drum key the same way, with the metronome silent while waiting */
    reset();
    par[0][TP_ENGINE] = EG_DRUMLINE;
    S.metronome = 2;
    seq_rec(&S, 2);
    start();
    run(2 * L16);
    CHECK(count(E_METRO, -1) == 0);
    seq_key(&S, 0, 3, 1, 100);
    run(10);
    CHECK(count(E_DRUM, 0) == 1 && count(E_METRO, -1) == 1 && kit_mask(st(0, 0)) == 1u << 2);   /* the hat (sound 3) */
    /* seq_rec(2) while playing is a plain rec */
    stop();
    seq_rec(&S, 0);
    start();
    seq_rec(&S, 2);
    run(L16);
    CHECK(S.rec == 1 && S.rec_wait == 0 && count(E_STEP, 0) >= 1);
    printf("ok %s\n", cur);
}

/* a note played just before a step lands on that step: heard once (the key), not again when the step comes */
static void t_rec_early(void)
{
    cur = "rec-early";
    reset();
    S.rec = 1;
    start();
    run(L16 * 6 / 10);                /* 60 % into step 1: nearer step 2 */
    seq_key(&S, 0, 60, 1, 99);
    run(L16 * 5 / 10);
    seq_key(&S, 0, 60, 0, 0);
    run(L16 * 2);
    CHECK(st(0, 1)->note[0] == 60);
    CHECK(count(E_ON, 0) == 1);       /* the key's own sound only */
    /* a chord of two early keys, and a kit's two sounds */
    reset();
    S.rec = 1;
    start();
    run(L16 * 6 / 10);
    seq_key(&S, 0, 60, 1, 99);
    seq_key(&S, 0, 64, 1, 99);
    run(L16);
    seq_key(&S, 0, 60, 0, 0);
    seq_key(&S, 0, 64, 0, 0);
    run(L16 * 2);
    CHECK(st(0, 1)->note[0] == 60 && st(0, 1)->note[1] == 64 && count(E_ON, 0) == 2);
    reset();
    par[0][TP_ENGINE] = EG_DRUMLINE;
    S.rec = 1;
    start();
    run(L16 * 6 / 10);
    seq_key(&S, 0, 1, 1, 99);
    seq_key(&S, 0, 3, 1, 99);
    run(L16 * 3);
    CHECK(kit_mask(st(0, 1)) == 5u && count(E_DRUM, 0) == 2);
    /* the recorded step does sound on the next pass, and a note on the current step still records late */
    reset();
    S.rec = 1;
    start();
    run(L16 * 6 / 10);
    seq_key(&S, 0, 60, 1, 99);
    seq_key(&S, 0, 60, 0, 0);
    run_to(16 * L16 + 2 * L16);
    CHECK(count(E_ON, 0) == 2);
    printf("ok %s\n", cur);
}

static void t_ext_clock(void)
{
    int i;
    cur = "ext-clock";
    reset();
    note(0, 0, 60, 100);
    note(0, 1, 62, 100);
    seq_ext_clock(&S);
    start();
    CHECK(S.ext == 1 && count(E_ON, 0) == 1 && on_n(0)->t == 0);
    for (i = 0; i < 12; i++) {        /* 24 ppqn at 100 BPM: a tick every 1102.5 samples */
        run(1102);
        seq_ext_clock(&S);
        seq_advance(&S, 0, &sink);
    }
    CHECK(count(E_ON, 0) == 2 && on_n(1)->a == 62 && near(on_n(1)->t, 6 * 1102, 1));
    CHECK(seq_tempo(&S) > 95.0f && seq_tempo(&S) < 105.0f);
    run(SEQ_FS);                      /* the clock stops: internal again after half a second */
    CHECK(S.ext == 0 && count(E_STEP, 0) >= 3 && nth(E_STEP, 0, 2)->a == 2);
    printf("ok %s\n", cur);
}

static void t_midi_out(void)
{
    int i, clocks = 0, starts = 0, ons = 0;
    cur = "midi-out";
    reset();
    S.send_clock = 1;
    S.send_notes = 1;
    par[2][TP_CHAN] = 1;              /* MIDI OUT on; the note goes out on the track's own channel (2 for T3) */
    note(2, 0, 60, 100);
    start();
    run_blocks(4 * L16 + 1);          /* (the clock waits for a stretch's end: at most a block late, never early) */
    for (i = 0; i < nev; i++)
        if (ev[i].kind == E_MIDI) {
            clocks += ev[i].a == 0xF8;
            starts += ev[i].a == 0xFA;
            ons += ev[i].a == 0x92 && ev[i].b == 60;
        }
    CHECK(clocks >= 24 && clocks <= 25 && starts == 1 && ons == 1);
    CHECK(nth(E_MIDI, -1, 3)->t >= 918 && nth(E_MIDI, -1, 3)->t < 918 + 256);   /* start, note on, clock, clock at
                                                                                * 1/24 of a quarter (by a block) */
    stop();
    CHECK(ev[nev - 1].kind == E_MIDI && ev[nev - 1].a == 0xFC);
    {                                             /* a kit's hit: its GM note, the off at its step's end (not at once) */
        uint32_t on_t = 0, off_t = 0;
        reset();
        S.send_notes = 1;
        par[3][TP_ENGINE] = EG_DRUMLINE;
        par[3][TP_CHAN] = 1;                      /* MIDI OUT on; the note goes out on T4's own channel (3) */
        note(3, 0, 1, 100);                       /* the kick */
        start();
        run(L16 + 10u);
        for (i = 0; i < nev; i++)
            if (ev[i].kind == E_MIDI && ev[i].b == 36) {
                if (ev[i].a == 0x93 && !on_t)
                    on_t = ev[i].t + 1u;
                if (ev[i].a == 0x83 && !off_t)
                    off_t = ev[i].t + 1u;
            }
        CHECK(on_t && off_t && off_t > on_t + L16 / 4u);
    }
    {                                             /* a note still on when the same note comes again: off, then on (never
                                                   * two ons, the first's off ending the second) */
        int last = 0, twice = 0, offs = 0;
        reset();
        S.send_notes = 1;
        par[0][TP_CHAN] = 1;
        note(0, 0, 60, 100);
        st(0, 0)->len = 8;                        /* two steps long */
        note(0, 1, 60, 100);
        start();
        run(4u * L16);
        for (i = 0; i < nev; i++)
            if (ev[i].kind == E_MIDI && ev[i].b == 60 && (ev[i].a & 0xF0) != 0xF0) {
                int on = (ev[i].a & 0xF0) == 0x90;
                twice |= on && last == 1;
                offs += !on;
                last = on ? 1 : 2;
            }
        CHECK(!twice && offs >= 2);
    }
    {                                             /* MIDI OUT off (TP_CHAN 0): no note out at all */
        int any = 0;
        reset();
        S.send_notes = 1;
        par[0][TP_CHAN] = 0;
        note(0, 0, 60, 100);
        start();
        run(2u * L16);
        for (i = 0; i < nev; i++)
            any |= ev[i].kind == E_MIDI && (ev[i].a & 0xF0) == 0x90;
        CHECK(!any);
    }
    {                                             /* a key from MIDI in plays (and records) but nothing of it goes back
                                                   * out: its note, or what its arp makes of it; the FM-1's own key
                                                   * does, and the arp it drives */
        int ons = 0, offs = 0;
        reset();
        S.send_notes = 1;
        par[0][TP_CHAN] = 1;
        seq_key_midi(&S, 0, 60, 1, 100);
        run(100);
        seq_key_midi(&S, 0, 60, 0, 0);
        run(100);
        CHECK(count(E_ON, 0) == 1 && count(E_OFF, 0) == 1 && count(E_MIDI, -1) == 0);
        seq_key(&S, 0, 62, 1, 100);
        run(100);
        seq_key(&S, 0, 62, 0, 0);
        run(100);
        for (i = 0; i < nev; i++)
            if (ev[i].kind == E_MIDI && ev[i].b == 62) {
                ons += ev[i].a == 0x90;
                offs += ev[i].a == 0x80;
            }
        CHECK(ons == 1 && offs == 1);
        reset();
        S.send_notes = 1;
        par[0][TP_CHAN] = 1;
        par[0][TP_PLAYER] = PL_ARP;
        par[0][TP_P1] = 4;
        par[0][TP_P3] = 1;
        seq_key_midi(&S, 0, 60, 1, 100);
        seq_key_midi(&S, 0, 64, 1, 100);
        run(4 * L16);
        CHECK(count(E_ON, 0) >= 3 && count(E_MIDI, -1) == 0);
        seq_key(&S, 0, 67, 1, 100);   /* the FM-1's own key joins: the arp is its playing now, out it goes */
        run(4 * L16);
        ons = 0;
        for (i = 0; i < nev; i++)
            ons += ev[i].kind == E_MIDI && ev[i].a == 0x90;
        CHECK(ons >= 3);
        seq_key(&S, 0, 60, 0, 0);
        seq_key(&S, 0, 64, 0, 0);
        seq_key(&S, 0, 67, 0, 0);
        run(2 * L16);
        CHECK(midi_n(0x90) == midi_n(0x80));
    }
    {                                             /* hold, and the strum rolled (roll > 0), from MIDI in: quiet; the
                                                   * FM-1's own key turns them back on, every on with its off */
        static const uint8_t chord[3] = {60, 64, 67};
        reset();
        S.send_notes = 1;
        par[0][TP_CHAN] = 1;
        par[0][TP_PLAYER] = PL_HOLD;
        seq_key_midi(&S, 0, 60, 1, 100);
        seq_key_midi(&S, 0, 60, 0, 0);
        run(100);
        CHECK(count(E_ON, 0) == 1 && count(E_MIDI, -1) == 0);
        seq_key(&S, 0, 62, 1, 100);
        seq_key(&S, 0, 62, 0, 0);
        run(100);
        CHECK(midi_n(0x90) == 1);
        seq_key_all_off(&S, 0);
        run(100);
        CHECK(midi_n(0x80) == 1);
        reset();
        S.send_notes = 1;
        par[0][TP_CHAN] = 1;
        par[0][TP_PLAYER] = PL_STRUM;
        par[0][TP_P1] = 20;                       /* rolled */
        seq_maestro_record(&S, 0, chord, 3);
        seq_key_midi(&S, 0, 62, 1, 100);
        run(2 * L16);
        seq_key_midi(&S, 0, 62, 0, 0);
        run(100);
        CHECK(count(E_ON, 0) == 3 && count(E_MIDI, -1) == 0);
        seq_key(&S, 0, 64, 1, 100);
        run(2 * L16);
        seq_key(&S, 0, 64, 0, 0);
        run(100);
        CHECK(midi_n(0x90) == 3 && midi_n(0x80) == 3);
    }
    for (int order = 0; order < 2; order++) {     /* MIDI in and the FM-1's key on one note, either way round: the one
                                                   * on out has its one off */
        reset();
        S.send_notes = 1;
        par[0][TP_CHAN] = 1;
        if (order)
            seq_key_midi(&S, 0, 60, 1, 100);
        seq_key(&S, 0, 60, 1, 100);
        if (!order)
            seq_key_midi(&S, 0, 60, 1, 100);
        run(100);
        if (order)
            seq_key(&S, 0, 60, 0, 0);
        seq_key_midi(&S, 0, 60, 0, 0);
        if (!order)
            seq_key(&S, 0, 60, 0, 0);
        run(100);
        CHECK(midi_n(0x90) == 1 && midi_n(0x80) == 1);
    }
    {                                             /* a key held while a step plays its note: two ons out, two offs (an
                                                   * off before the second on, so the receiver hears both) */
        reset();
        S.send_notes = 1;
        par[0][TP_CHAN] = 1;
        note(0, 0, 60, 100);
        seq_key(&S, 0, 60, 1, 100);
        run(100);
        start();
        run(L16 / 2);
        seq_key(&S, 0, 60, 0, 0);
        run(2u * L16);
        stop();
        CHECK(midi_n(0x90) == 2 && midi_n(0x80) == 2);
    }
    {                                             /* a kit key held when the keys let go (another track selected): its
                                                   * GM note's off goes out with it */
        reset();
        S.send_notes = 1;
        par[0][TP_ENGINE] = EG_DRUMLINE;
        par[0][TP_CHAN] = 1;
        seq_key(&S, 0, 1, 1, 100);                /* the kick: GM 36 */
        run(100);
        seq_key_all_off(&S, 0);
        run(100);
        seq_key(&S, 0, 1, 0, 0);
        run(100);
        CHECK(midi_n(0x90) == 1 && midi_n(0x80) == 1);
    }
    {                                             /* ALL NOTES OFF: every note still on out there gets its off, even one
                                                   * nothing else remembers (hold keeps sixteen: the seventeenth) */
        reset();
        S.send_notes = 1;
        par[0][TP_CHAN] = 1;
        par[0][TP_PLAYER] = PL_HOLD;
        for (i = 0; i < 17; i++)
            seq_key(&S, 0, 40 + i, 1, 100);
        run(100);
        for (i = 0; i < 17; i++)
            seq_key(&S, 0, 40 + i, 0, 0);
        run(100);
        CHECK(midi_n(0x90) == 17 && midi_n(0x80) == 0);
        seq_key_all_off(&S, 0);
        seq_midi_notes_off(&S);
        run(100);
        CHECK(midi_n(0x80) == 17);
        seq_midi_notes_off(&S);                   /* (once only) */
        run(100);
        CHECK(midi_n(0x80) == 17);
    }
    printf("ok %s\n", cur);
}

static void t_count_in(void)
{
    cur = "count-in";
    reset();
    note(0, 0, 60, 100);
    S.rec = 1;
    S.metronome = 1;
    S.count_in = 1;
    start();
    run(17 * L16);
    CHECK(count(E_METRO, -1) >= 5 && nth(E_METRO, -1, 0)->t == 0 && nth(E_METRO, -1, 0)->a == 1 &&
          near(nth(E_METRO, -1, 1)->t, 4 * L16 + 2, 2) && nth(E_METRO, -1, 1)->a == 0);
    CHECK(count(E_ON, 0) == 1 && near(on_n(0)->t, 16 * L16 + 8, 1) && S.count_in == 0 && S.rec == 1);
    /* keys in the count-in: the last half step before the downbeat lands on step 1, sounding once; earlier ones
     * are only heard */
    reset();
    S.rec = 1;
    S.count_in = 1;
    start();
    run_to(16 * L16 - L16 * 3 / 10);
    seq_key(&S, 0, 60, 1, 99);
    run(10);
    seq_key(&S, 0, 60, 0, 0);
    run_to(16 * L16 + L16);
    CHECK(S.count_in == 0 && st(0, 0)->note[0] == 60 && st(0, 0)->vel == 99);
    CHECK(count(E_ON, 0) == 1);
    reset();
    S.rec = 1;
    S.count_in = 1;
    start();
    run_to(16 * L16 - L16 * 7 / 10);
    seq_key(&S, 0, 60, 1, 99);
    seq_key(&S, 0, 60, 0, 0);
    run_to(16 * L16 + L16);
    CHECK(st(0, 0)->note[0] == 0 && count(E_ON, 0) == 1);
    reset();
    S.rec = 1;
    S.count_in = 2;
    start();
    run_to(16 * L16 - L16 * 3 / 10);
    seq_key(&S, 0, 60, 1, 99);
    seq_key(&S, 0, 60, 0, 0);
    run_to(32 * L16 + L16);
    CHECK(st(0, 0)->note[0] == 0);
    /* a kit key, and the second pass plays it */
    reset();
    par[0][TP_ENGINE] = EG_DRUMLINE;
    S.rec = 1;
    S.count_in = 1;
    start();
    run_to(16 * L16 - L16 * 2 / 10);
    seq_key(&S, 0, 3, 1, 99);
    run_to(16 * L16 + L16);
    CHECK(kit_mask(st(0, 0)) == 1u << 2 && count(E_DRUM, 0) == 1);
    run_to(32 * L16 + L16);
    CHECK(count(E_DRUM, 0) == 2);
    /* the metronome off while playing without rec */
    reset();
    S.metronome = 1;
    start();
    run(16 * L16);
    CHECK(count(E_METRO, -1) == 0);
    printf("ok %s\n", cur);
}

static void t_aux_tracks(void)
{
    cur = "aux-tracks";
    reset();
    note(0, 0, 60, 100);
    note(0, 1, 60, 100);
    P.pat[AUX_PUNCH][0].st[1].note[0] = 1 + PX_MEL + PX_MUTE;   /* the melodic duck on step 2, a step long */
    P.pat[AUX_PUNCH][0].st[1].vel = 100;
    P.pat[AUX_PUNCH][0].st[1].len = 4;
    start();
    run(3 * L16);
    CHECK(count(E_ON, 0) == 1);       /* step 2 muted by the sequenced FX */
    CHECK(nth(E_PUNCH, -1, 0) && nth(E_PUNCH, -1, 0)->b == 1);
    /* ten sequenced tracks (1-8, the conductor, the throws), then the two FX tracks: no tape */
    CHECK(NSEQT == 10 && AUX_PUNCH == NSEQT - 1 && AUX_FX1 == NSEQT && AUX_FX2 == NSEQT + 1 && AUX_WEAVE == NSEQT + 2 && NALLTRK == 13);
    printf("ok %s\n", cur);
}

static void t_pattern_helpers(void)
{
    step_t s;
    int a, b;
    cur = "pattern-helpers";
    reset();
    memset(&s, 0, sizeof s);
    CHECK(step_empty(&s));
    step_toggle_note(&s, 60);
    step_toggle_note(&s, 64);
    CHECK(!step_empty(&s) && s.note[0] == 60 && s.note[1] == 64 && s.vel == 100);
    step_toggle_note(&s, 60);
    CHECK(s.note[0] == 64 && s.note[1] == 0);
    cond_ab(0, &a, &b);
    CHECK(a == 1 && b == 2);
    cond_ab(2, &a, &b);
    CHECK(a == 1 && b == 3);
    cond_ab(COND_NAB - 1, &a, &b);
    CHECK(a == 8 && b == 8);
    CHECK(P.pat[AUX_BRAIN][0].scale == TS_4 && P.pat[0][0].len == 16 && P.scene[0].used);
    printf("ok %s\n", cur);
}

static void t_weave(void)
{
    static tpat_t a, b, o;
    cur = "weave";
    pattern_init(&a);
    pattern_init(&b);
    a.len = 32;                                    /* a: notes 60 on steps 1, 5, 9 ... over two bars */
    for (int i = 0; i < 32; i += 4)
        a.st[i].note[0] = 60, a.st[i].vel = 90;
    a.scale = TS_2;
    b.len = 16;                                    /* b: 72, 74, 76 on steps 3, 4, 8 of one bar */
    b.st[2].note[0] = 72, b.st[3].note[0] = 74, b.st[7].note[0] = 76;
    b.st[2].vel = b.st[3].vel = b.st[7].vel = 50;
    pattern_woven(&o, &a, WK_MEL, &b, WK_MEL, WV_BARS);   /* bar 1 a's, bar 2 b's */
    CHECK(o.len == 32 && o.scale == TS_2);
    CHECK(o.st[0].note[0] == 60 && o.st[4].note[0] == 60 && o.st[2].note[0] == 0);
    CHECK(o.st[16].note[0] == 0 && o.st[18].note[0] == 72 && o.st[23].note[0] == 76 && o.st[20].note[0] == 0);
    pattern_woven(&o, &b, WK_MEL, &a, WK_MEL, WV_BARS);   /* b (one bar) twice as long: bar 1 b's, bar 2 a's */
    CHECK(o.len == 32 && o.st[2].note[0] == 72 && o.st[16].note[0] == 60 && o.st[18].note[0] == 0);
    pattern_woven(&o, &a, WK_MEL, &b, WK_MEL, WV_STEPS);  /* odd steps a's, even b's */
    CHECK(o.st[0].note[0] == 60 && o.st[2].note[0] == 0 && o.st[3].note[0] == 74 && o.st[7].note[0] == 76);
    CHECK(o.st[19].note[0] == 74);                 /* b looped: step 20 is b's step 4 */
    pattern_woven(&o, &a, WK_MEL, &b, WK_MEL, WV_RHYTHM); /* a's hits, b's notes in turn, a's velocity */
    CHECK(o.len == 32 && o.st[0].note[0] == 72 && o.st[4].note[0] == 74 && o.st[8].note[0] == 76);
    CHECK(o.st[12].note[0] == 72 && o.st[0].vel == 90 && o.st[1].note[0] == 0);
    pattern_woven(&o, &a, WK_MEL, &b, WK_MEL, WV_NOTES);  /* b's hits (looped to 32), a's notes; a's settings */
    CHECK(o.len == 32 && o.scale == TS_2 && o.st[2].note[0] == 60 && o.st[3].note[0] == 60);
    CHECK(o.st[2].vel == 50 && o.st[0].note[0] == 0 && o.st[18].note[0] == 60);
    {   /* live: the weave carries on across passes: a one-bar a by bars plays b on its second pass */
        step_t w;
        weave_step(&b, WK_MEL, &a, WK_MEL, WV_BARS, 16 + 2, &w);
        CHECK(w.note[0] == 0);
        weave_step(&b, WK_MEL, &a, WK_MEL, WV_BARS, 2, &w);
        CHECK(w.note[0] == 72);
        weave_step(&b, WK_MEL, &a, WK_MEL, WV_RHYTHM, 16 + 2, &w);   /* b's 4th hit (pass 2's first) a's 4th note */
        CHECK(w.note[0] == 60);
    }
    {   /* across kinds: a kit's sounds on a melodic rhythm as the pentatonic ladder; notes onto a kit as sounds */
        static tpat_t k;
        step_t w;
        pattern_init(&k);
        kit_add(&k.st[0], 1);
        kit_add(&k.st[4], 3);
        k.st[0].vel = k.st[4].vel = 100;
        weave_step(&a, WK_MEL, &k, WK_KIT, WV_RHYTHM, 4, &w);   /* a's 2nd hit: the kit's 2nd (sound 3): F3 */
        CHECK(w.note[0] == 53 && w.vel == 90);
        weave_step(&k, WK_KIT, &a, WK_MEL, WV_RHYTHM, 0, &w);   /* the kit's 1st hit: a's 1st note (60) as a sound */
        CHECK(kit_mask(&w) == 1u << (60 % NDRUM) && w.note[2] == 0);
        weave_step(&a, WK_MEL, &k, WK_FX, WV_STEPS, 1, &w);     /* a throw's step onto notes: nothing taken */
        CHECK(step_empty(&w));
    }
    pattern_init(&b);                              /* no notes to take: the rhythm stays as it was */
    pattern_woven(&o, &a, WK_MEL, &b, WK_MEL, WV_RHYTHM);
    CHECK(o.st[0].note[0] == 60 && o.st[4].note[0] == 60);
    printf("ok %s\n", cur);
}

/* phase: a track's position comes from the time since its step 1, so a scale or length change while it plays
 * lands where it would be had it always run so; PLAY after STOP starts every track from step 1 */
static ev_t *step_after(int trk, uint32_t t0)
{
    for (int i = 0; i < nev; i++)
        if (ev[i].kind == E_STEP && ev[i].trk == trk && ev[i].t >= t0)
            return &ev[i];
    return 0;
}
#define T16(n) ((uint32_t)(n) * 11025u / 2u)   /* n sixteenths at 120 BPM, exactly */
static void t_phase(void)
{
    ev_t *e;
    uint32_t t0;
    cur = "phase";
    reset();
    for (int i = 0; i < 16; i++)
        note(0, i, 60, 100), note(1, i, 62, 100);
    start();
    run_to(5 * L16 + 100);                       /* step 6 (index 5) is playing */
    P.pat[0][0].scale = TS_2;                    /* two sixteenths a step from now */
    t0 = now;
    run(4 * L16);
    e = step_after(0, t0);                       /* on its new grid: at 6 sixteenths, index 3 */
    CHECK(e && near(e->t, T16(6), 3) && e->a == 3);
    P.pat[0][0].scale = TS_4;                    /* a beat a step, set between two of its steps (at ~9.x) */
    run_to(9 * L16 + 2000);
    t0 = now;
    run(6 * L16);
    e = step_after(0, t0);
    CHECK(e && near(e->t, T16(12), 3) && e->a == 3);
    P.pat[0][0].scale = TS_1;                    /* back to a sixteenth a step, at ~15.4: the next sixteenth */
    t0 = now;
    run(2 * L16);
    e = step_after(0, t0);
    CHECK(e && near(e->t, T16(16), 3) && e->a == 0);
    e = step_after(1, t0);                       /* the other track never moved: with it */
    CHECK(e && near(e->t, T16(16), 3) && e->a == 0);
    run_to(T16(17) + 2500);
    P.pat[0][0].len = 12;                        /* a shorter pattern at ~17.5: at 18, where 12 steps would be */
    t0 = now;
    run(2 * L16);
    e = step_after(0, t0);
    CHECK(e && near(e->t, T16(18), 3) && e->a == 18 % 12);
    /* stop, start: from step 1 */
    stop();
    nev = 0;
    start();
    run(L16 / 2);
    e = step_after(0, 0);
    CHECK(e && e->a == 0);
    e = step_after(1, 0);
    CHECK(e && e->a == 0);
    printf("ok %s\n", cur);
}

/* a pattern cued on a track switches at the next bar line (16 sixteenths), even when track 1's pattern is
 * longer (its span is the "bar" for scenes and the song) */
static void t_cue_bar(void)
{
    ev_t *e;
    cur = "cue-bar";
    reset();
    P.pat[0][0].len = 64;                        /* track 1: four bars */
    for (int i = 0; i < 16; i++)
        note(1, i, 60, 100);
    P.pat[1][1].st[0].note[0] = 72, P.pat[1][1].st[0].vel = 100;   /* track 2, pattern 2: one note on step 1 */
    start();
    run_to(T16(5) + 100);
    seq_cue_pattern(&S, 1, 1);
    run_to(T16(15) + 100);
    CHECK(S.ppat[1] == 0 && S.pcue[1] == 1);     /* still waiting */
    run_to(T16(16) + 100);
    CHECK(S.ppat[1] == 1 && S.pcue[1] == 0xFF);  /* switched at the bar line, from its step 1 */
    e = step_after(1, T16(16) - 10);
    CHECK(e && near(e->t, T16(16), 3) && e->a == 0);
    printf("ok %s\n", cur);
}

/* a scene cued while playing starts at the next bar line, or (cued to the end) when track 1's pattern ends; the
 * scene playing cued takes the cue off; seq_cue_left counts the sixteenths before it */
static void t_cue_scene(void)
{
    cur = "cue-scene";
    reset();
    P.pat[0][0].len = 32;                        /* track 1: two bars, in both scenes */
    P.pat[0][1].len = 32;
    note(0, 0, 60, 100);
    P.pat[0][1].st[0].note[0] = 72, P.pat[0][1].st[0].vel = 100;
    P.scene[1].used = 1;
    P.scene[1].pat[0] = 1;
    P.scene[2].used = 1;
    P.scene[2].pat[0] = 1;
    start();
    run_to(T16(5) + 100);
    CHECK(seq_cue_left(&S) == -1);
    seq_cue_scene(&S, 1, 0);                     /* at the bar */
    CHECK(seq_cue_left(&S) == 10);               /* sixteenths 6..15 before the bar line's */
    run_to(T16(15) + 100);
    CHECK(S.scene == 0 && S.cue == 1 && seq_cue_left(&S) == 0);
    run_to(T16(16) + 100);
    CHECK(S.scene == 1 && S.cue == 0xFF && S.ppat[0] == 1 && seq_cue_left(&S) == -1);
    CHECK(near(on_n(1)->t, T16(16), 3) && on_n(1)->a == 72);    /* track 1 from step 1, on the bar line */
    /* to the end: track 1's pattern, 32 steps from the bar line */
    seq_cue_scene(&S, 0, 1);
    run_to(T16(20) + 100);
    CHECK(S.cue == 0 && seq_cue_left(&S) == 27);
    run_to(T16(32) + 100);
    CHECK(S.scene == 1 && S.cue == 0);           /* the bar line passes: still waiting */
    run_to(T16(48) + 100);
    CHECK(S.scene == 0 && S.cue == 0xFF);        /* track 1's end */
    /* the scene playing: the cue is off */
    seq_cue_scene(&S, 2, 0);
    seq_cue_scene(&S, 0, 0);
    CHECK(S.cue == 0xFF);
    run_to(T16(64) + 100);
    CHECK(S.scene == 0);
    printf("ok %s\n", cur);
}

int main(void)
{
    t_pattern_helpers();
    t_phase();
    t_cue_bar();
    t_cue_scene();
    t_weave();
    t_timing();
    t_scale();
    t_groove();
    t_length();
    t_long_page();
    t_chord();
    t_pulse();
    t_pulse_kit();
    t_follow();
    t_hold();
    t_multiply();
    t_velocity();
    t_ramp();
    t_random();
    t_portamento();
    t_bend();
    t_jump();
    t_loop();
    t_jump_chance();
    t_skip();
    t_components();
    t_cond();
    t_locks();
    t_kit11();
    t_weave_live();
    t_scene();
    t_ghost();
    t_song();
    t_song_bars();
    t_brain();
    t_arp();
    t_maestro();
    t_player_switch();
    t_step_players();
    t_hold_player();
    t_punch_keys();
    t_duck();
    t_loops();
    t_echo();
    t_loop_release();
    t_fills();
    t_slice_throws();
    t_pitch_short_long();
    t_drum_off();
    t_punch_seq();
    t_record();
    t_rec_wait();
    t_rec_early();
    t_ext_clock();
    t_midi_out();
    t_count_in();
    t_aux_tracks();
    printf("all sequencer tests passed\n");
    return 0;
}
