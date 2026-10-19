/* SPDX-License-Identifier: GPL-3.0-only */
/* The voices (dsp/synth.c) on their own: the parameter tables and their texts (the engines' plain
 * defaults, "2:1", "809Hz", the organ's eight models, the nine tables, the LFO's knobs per type, the
 * names of the filters, the LFO types and the arp); every engine and preset renders a note that stays
 * finite and at a sensible level and dies in its release; weft's knobs each move its spectrum; the six
 * filter types; the voice modes' retrigger rules; the pool's limit and stealing; locks; the LFO
 * (regular vs free, the wobble's envelope and shapes, the drift's unipolar amount, the pump's hold and
 * release); the character stage (every type bounded and finite, drive and fold keep the level, crush
 * quantises, ring makes sidebands, mix 0 is a bypass, locks and the LFO reach the amount); the sample
 * engine (a sample at its root note plays at its own rate, an octave up twice as fast; one-shot, loop,
 * reverse, pingpong; start and length; an empty slot is silent; bounded everywhere; a user bank built in
 * RAM is read, a bad one refused, a dropped one stops its voices); the slice engine (each note its slice,
 * wrapping; 4 to 32 slices; the gate; pitch; the tempo with the break's bar; no click at a slice's ends, a mono
 * retrigger or a legato move; poly; bounded everywhere); and that
 * splitting a block by voices or by frames changes no sample. Then a benchmark: instructions per sample
 * per voice, 16 voices of each engine and of each character type (macOS: the task's own instruction count,
 * as fm1-sph's sophie_bench.c; elsewhere clock()).
 *   build/host/synth_test            (tests/host/synth_test.sh builds it) */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#ifdef __APPLE__
#include <unistd.h>
#include <libproc.h>
#include <sys/resource.h>
#endif
#include "synth.h"
#include "drumline.h"
#include "sample.h"

static int bad;
static void check(const char *what, int ok)
{
    printf("%-64s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

#define N 256
static uint8_t par[NTRK][NTP];
static _Alignas(16) uint8_t mem[SYNTH_BYTES];
static synth_t *S = (synth_t *)mem;
static float L[N], R[N], S1[N], S2[N];
static float last_rms;                           /* the last block's RMS (left) */

static int finite_f(float x) { return x == x && x < 1e30f && x > -1e30f; }
static float fm_fabs_t(float x) { return x < 0.0f ? -x : x; }
static float fabs_f(float x) { return x < 0.0f ? -x : x; }

/* render n frames in one range, return the block's peak and whether all was finite */
static float block(uint32_t n, int *finite)
{
    uint32_t i;
    float pk = 0.0f;
    memset(L, 0, sizeof L);
    memset(R, 0, sizeof R);
    memset(S1, 0, sizeof S1);
    memset(S2, 0, sizeof S2);
    synth_prepare(S, n);
    synth_render(S, 0, NVOICE, L, R, S1, S2, n);
    last_rms = 0.0f;
    for (i = 0; i < n; i++) {
        if (!finite_f(L[i]) || !finite_f(R[i]) || !finite_f(S1[i]))
            *finite = 0;
        if (fabs_f(L[i]) > pk)
            pk = fabs_f(L[i]);
        if (fabs_f(R[i]) > pk)
            pk = fabs_f(R[i]);
        last_rms += L[i] * L[i];
    }
    last_rms = (float)sqrt((double)(last_rms / (float)n));
    return pk;
}

static void reset(void)
{
    int t;
    for (t = 0; t < NTRK; t++)
        sound_default(par[t], EG_KNIT);
    synth_init(S, par);
    synth_tempo(S, 120.0f);
}

static void load_preset(int t, int k)
{
    memcpy(par[t], PRESETS[k].par, NTP_SOUND);
}

static float db(float x) { return x > 0.0f ? 20.0f * (float)log10((double)x) : -200.0f; }

/* a one-second note, then its release: peak in range, finite, silent at the end */
static int note_test(const char *name, int t, float *peak_out, int level_check)
{
    int finite = 1, b, ok;
    float pk = 0.0f, tail = 0.0f;
    synth_all_off(S, -1);
    for (b = 0; b < 20; b++)
        block(N, &finite);                       /* let any old release die */
    synth_note_on(S, t, 60, 100, 0, 1);
    for (b = 0; b < 172; b++) {                  /* 1 s */
        float p = block(N, &finite);
        if (p > pk)
            pk = p;
    }
    synth_note_off(S, t, 1);
    for (b = 0; b < 172 * 12; b++) {             /* up to 12 s of release (10 s is the longest) */
        int i;
        block(N, &finite);
        if (!synth_sounding(S, 0, NVOICE)) {     /* the end of the last block: silence by then */
            tail = 0.0f;
            for (i = N - 16; i < N; i++)
                if (fabs_f(L[i]) > tail)
                    tail = fabs_f(L[i]);
            break;
        }
    }
    ok = finite && (pk > 0.0316f || !level_check) && pk <= 1.0f && !synth_sounding(S, 0, NVOICE) && tail < 1e-3f;
    if (!ok)
        printf("    %s: peak %.1f dBFS finite %d sounding %d tail %g\n", name, (double)db(pk), finite,
               synth_sounding(S, 0, NVOICE), (double)tail);
    *peak_out = pk;
    return ok;
}

/* the level swing (max / min block RMS) over nblocks of a held note, after skipping some */
static float swing(int skip, int nblocks, int *finite)
{
    int b;
    float mn = 1e9f, mx = 0.0f;
    for (b = 0; b < skip; b++)
        block(N, finite);
    for (b = 0; b < nblocks; b++) {
        float p;
        block(N, finite);
        p = last_rms;
        if (p < mn)
            mn = p;
        if (p > mx)
            mx = p;
    }
    return mn > 0.0f ? mx / mn : 1e9f;
}

/* the first block's samples of a one-note render into out[N] (a fingerprint of the sound) */
static void fingerprint(int t, float *out)
{
    int finite = 1, b;
    synth_all_off(S, -1);
    for (b = 0; b < 400 && synth_sounding(S, 0, NVOICE); b++)
        block(N, &finite);
    synth_note_on(S, t, 60, 100, 0, 1);
    for (b = 0; b < 20; b++)
        block(N, &finite);
    memcpy(out, L, sizeof L);
    synth_note_off(S, t, 1);
}
static int differ(const float *a, const float *b)
{
    int i;
    for (i = 0; i < N; i++)
        if (fabs_f(a[i] - b[i]) > 1e-4f)
            return 1;
    return 0;
}

#ifdef __APPLE__
static uint64_t instr_now(void)
{
    struct rusage_info_v4 ri;
    if (!proc_pid_rusage(getpid(), RUSAGE_INFO_V4, (rusage_info_t *)&ri))
        return ri.ri_instructions;
    return 0;
}
#else
static uint64_t instr_now(void) { return (uint64_t)clock(); }
#endif

/* 16 voices of weft, mono (spread 0): variant 1 saw .. pulse, 2 the resonant half; mean instructions per
 * sample per voice after 20 blocks, and the worst block's */
static double bench_weft(int variant, double *worst_out)
{
    int t, b, finite = 1;
    uint64_t total = 0, worst = 0;
    reset();
    for (t = 0; t < NTRK; t++) {
        sound_default(par[t], EG_KNIT);
        par[t][TP_FTYPE] = FT_LP24;
        par[t][TP_E1] = 90;
        par[t][TP_E2] = (uint8_t)(variant == 1 ? 40 : 110);
        par[t][TP_E3] = 60;
        par[t][TP_E4] = 0;
        par[t][TP_LTYPE] = LT_WAVE;
        par[t][TP_LDEST] = LD_FILTER;
        par[t][TP_LX] = 0;
    }
    for (t = 0; t < NVOICE; t++)
        synth_note_on(S, t % NTRK, 48 + t * 2, 100, 0, (uint32_t)t);
    for (b = 0; b < 400; b++) {
        uint64_t i0 = instr_now(), di;
        block(N, &finite);
        di = instr_now() - i0;
        if (b >= 20) {
            total += di;
            if (di > worst)
                worst = di;
        }
    }
    *worst_out = (double)worst / (double)(N * NVOICE);
    return (double)total / (380.0 * N * NVOICE);
}



/* ---- the sample engine ---- */
static float sbuf[44100 * 3];
/* track 0 plays note for up to secs, held: the mono output into sbuf; returns the frames until its voices stopped
 * (-1: still sounding at the end) */
static int sample_run(int note, float secs, int *n_out)
{
    int finite = 1, n = 0, stop = -1, b, i, nb = (int)(secs * 44100.0f) / 32;
    synth_all_off(S, -1);
    for (b = 0; b < 400 && synth_sounding(S, 0, NVOICE); b++)
        block(32, &finite);
    synth_note_on(S, 0, note, 100, 0, 77);
    for (b = 0; b < nb; b++) {
        block(32, &finite);
        for (i = 0; i < 32; i++)
            sbuf[n++] = (L[i] + R[i]) * 0.5f;
        if (stop < 0 && !synth_sounding(S, 0, NVOICE))
            stop = n;
    }
    synth_note_off(S, 0, 77);
    if (!finite)
        stop = -2;
    *n_out = n;
    return stop;
}
static float rms_d(const float *x, int n)
{
    double s = 0.0;
    int i;
    for (i = 0; i < n; i++)
        s += (double)x[i] * (double)x[i];
    return n ? (float)sqrt(s / n) : 0.0f;
}
static int period_of(const float *x, int n, int lo, int hi)   /* the autocorrelation's best lag in lo..hi */
{
    int lag, best = lo, i;
    double bv = -1e30;
    for (lag = lo; lag <= hi; lag++) {
        double s = 0.0;
        for (i = 0; i + lag < n; i++)
            s += (double)x[i] * (double)x[i + lag];
        if (s > bv) {
            bv = s;
            best = lag;
        }
    }
    return best;
}
#include "bank.h"                                /* a user bank built in RAM, its windows apart */
static float gen_hz;
static int gen_rate = SMP_RATE;
static float gen_sine(int i) { return (float)(20000.0 * sin(6.283185307179586 * (double)gen_hz * i / gen_rate)); }
/* one sample (gen(i): its values, int16 scale), as the bank's only entry, its bars stated, at SMP_RATE */
static void bank_gen(float (*gen)(int), int n, int root, int bars)
{
    bank_reset();
    bank_add("sine", gen, n, root, bars, SMP_RATE);
    bank_seal();
    bank_attach();
}
static void bank_sine(float hz, int n, int root)       /* one sample, a sine, as the bank's only entry */
{
    gen_hz = hz;
    gen_rate = SMP_RATE;
    bank_gen(gen_sine, n, root, 0);
}

static void sample_tests(void)
{
    int n, stop, stop2, p1, p2, ok, sm, md, nt;
    float e0, e1;
    char what[120];
    printf("-- the sample engine\n");
    reset();
    sound_default(par[0], EG_SAMPLE);
    par[0][TP_A] = 0;
    par[0][TP_S] = 127;                           /* held: the sample's own length decides */
    par[0][TP_R] = 20;
    par[0][TP_CUT] = 127;
    par[0][TP_RES] = 0;
    par[0][TP_E1] = 0;                            /* the kick, one-shot: 0.3 s at its root */
    par[0][TP_E4] = SM_ONESHOT;
    stop = sample_run(60, 1.0f, &n);
    stop2 = sample_run(72, 1.0f, &n);
    sprintf(what, "a sample at its root plays at its rate: the kick ends after %d frames (%d), an octave up %d",
            stop, (int)(smp_get(0)->n * 2u), stop2);
    check(what, abs(stop - (int)(smp_get(0)->n * 2u)) < 200 && abs(stop2 * 2 - stop) < 300);
    par[0][TP_E1] = 4;                            /* the ah: 220.5 Hz at its root A3, looped */
    par[0][TP_E4] = SM_LOOP;
    stop = sample_run(57, 1.5f, &n);
    p1 = period_of(sbuf + 22050, 8192, 150, 250);
    e0 = rms_d(sbuf + 4410, 4410);
    e1 = rms_d(sbuf + n - 4410, 4410);
    sample_run(69, 0.6f, &n);
    p2 = period_of(sbuf + 8820, 8192, 70, 130);
    sprintf(what, "the ah's period at its root %d frames (200), an octave up %d (100); looped held 1.5 s: still on (%d)",
            p1, p2, stop);
    check(what, abs(p1 - 200) <= 1 && abs(p2 - 100) <= 1 && stop == -1);
    sprintf(what, "loop: as loud at 1.5 s as at 0.1 s (RMS %.3f, %.3f)", (double)e0, (double)e1);
    check(what, e1 > e0 * 0.8f && e1 < e0 * 1.25f);
    par[0][TP_E4] = SM_ONESHOT;
    stop = sample_run(57, 1.5f, &n);
    sprintf(what, "one-shot: the same ah ends with its sample (%d frames, %d)", stop, (int)(smp_get(4)->n * 2u));
    check(what, abs(stop - (int)(smp_get(4)->n * 2u)) < 200);
    par[0][TP_E1] = 5;                            /* the piano reversed: it swells */
    par[0][TP_E4] = SM_REVERSE;
    stop = sample_run(60, 1.2f, &n);
    e0 = rms_d(sbuf + 2205, 8820);
    e1 = rms_d(sbuf + (int)(smp_get(5)->n * 2u) - 8820, 8820 - 441);
    sprintf(what, "reverse: the piano swells into its attack (RMS %.4f -> %.4f), ends with it (%d)", (double)e0, (double)e1, stop);
    check(what, e1 > e0 * 1.8f && abs(stop - (int)(smp_get(5)->n * 2u)) < 300);
    par[0][TP_E1] = 6;                            /* the sweep, pingpong: on past its 0.6 s, forth and back */
    par[0][TP_E4] = SM_PINGPONG;
    stop = sample_run(60, 2.0f, &n);
    e1 = rms_d(sbuf + 66150, 4410);
    sprintf(what, "pingpong: the sweep still sounds at 1.5 s, held (RMS %.4f)", (double)e1);
    check(what, stop == -1 && e1 > 0.01f);
    par[0][TP_E1] = 5;                            /* start and length: the piano from 50 %, a quarter of the rest */
    par[0][TP_E2] = 64;
    par[0][TP_E3] = 32;
    par[0][TP_E4] = SM_ONESHOT;
    stop = sample_run(60, 1.0f, &n);
    {
        int want = (int)((float)(smp_get(5)->n - smp_get(5)->n * 64u / 127u) * 32.0f / 127.0f) * 2;
        sprintf(what, "start 64, length 32: a region of %d frames plays (%d)", stop, want);
        check(what, abs(stop - want) < 300);
    }
    par[0][TP_E2] = 0;
    par[0][TP_E3] = 127;
    par[0][TP_E1] = (uint8_t)smp_factory();       /* the first user slot: empty */
    stop = sample_run(60, 0.2f, &n);
    check("an empty user slot: silent, the voice ends at once", stop >= 0 && stop < 200 && rms_d(sbuf, n) == 0.0f);
    {   /* every factory sample in every mode, notes from 0 to 127, the region's corners: finite, bounded */
        static const uint8_t ST[3][2] = {{0, 127}, {127, 127}, {64, 0}};
        float pk = 0.0f;
        int c, i;
        ok = 1;
        for (sm = 0; sm < smp_factory(); sm++)
            for (md = 0; md < NSM; md++)
                for (c = 0; c < 3; c++)
                    for (nt = 0; nt <= 127; nt += 127) {
                        par[0][TP_E1] = (uint8_t)sm;
                        par[0][TP_E4] = (uint8_t)md;
                        par[0][TP_E2] = ST[c][0];
                        par[0][TP_E3] = ST[c][1];
                        stop = sample_run(nt, 0.3f, &n);
                        for (i = 0; i < n; i++)
                            pk = fm_fabs_t(sbuf[i]) > pk ? fm_fabs_t(sbuf[i]) : pk;
                        if (stop == -2 || pk > 1.0f) {
                            printf("    %s mode %d corner %d note %d: peak %.2f\n", smp_name(sm), md, c, nt, (double)pk);
                            ok = 0;
                        }
                    }
        sprintf(what, "every sample x mode x region corner x note 0 / 127: finite, peak %.2f", (double)pk);
        check(what, ok);
    }
    {   /* a user bank in RAM: a 441 Hz sine, its root A4 */
        bank_sine(441.0f, 8000, 69);
        check("a user bank: read, one sample, named", smp_user_scan() == 1 && !strcmp(smp_name(smp_factory()), "sine") &&
                                                       smp_get(smp_factory()) != 0);
        par[0][TP_E1] = (uint8_t)smp_factory();
        par[0][TP_E2] = 0;
        par[0][TP_E3] = 127;
        par[0][TP_E4] = SM_LOOP;
        stop = sample_run(69, 0.5f, &n);
        p1 = period_of(sbuf + 4410, 4096, 80, 120);
        sprintf(what, "the user sine at its root: a period of %d frames (100), looped on (%d)", p1, stop);
        check(what, abs(p1 - 100) <= 1 && stop == -1);
        {   /* a voice playing it when the bank is dropped (an upload begins) stops */
            int finite = 1, b;
            synth_note_on(S, 0, 69, 100, 0, 78);
            for (b = 0; b < 20; b++)
                block(64, &finite);
            smp_user_drop();
            for (b = 0; b < 4; b++)
                block(64, &finite);
            check("a dropped bank: its voices stop at once", !synth_sounding(S, 0, NVOICE) && finite);
            synth_note_off(S, 0, 78);
        }
        bank[SMP_BANK_HDR + 100] ^= 0x55;          /* a flipped byte: the CRC refuses it */
        bank_attach();
        check("a bank whose data does not match its CRC is refused", smp_user_scan() == 0 && !smp_get(smp_factory()));
        bank[SMP_BANK_HDR + 100] ^= 0x55;
        bank[SMP_ENT0 + 20] = 0xFF;                /* a sample longer than the data */
        bank[SMP_ENT0 + 21] = 0xFF;
        bank[SMP_ENT0 + 22] = 0x01;
        bank_attach();
        check("an entry pointing past the data is refused", smp_user_scan() == 0);
        bank_detach();
    }
    {   /* each sample its own rate: a 441 Hz sine at its root A4 is a period of 100 frames, its length its own */
        static const int RT[4] = {11025, 16000, 22050, 32000};
        int k;
        ok = 1;
        for (k = 0; k < 4; k++) {
            int ns = RT[k] * 2 / 5, want = (int)((float)ns * 44100.0f / (float)RT[k]);
            gen_hz = 441.0f;
            gen_rate = RT[k];
            bank_reset();
            bank_add("sine", gen_sine, ns, 69, 0, RT[k]);
            bank_seal();
            bank_attach();
            if (smp_user_scan() != 1 || smp_get(smp_factory())->rate != RT[k]) {
                printf("    %d Hz: not read\n", RT[k]);
                ok = 0;
                continue;
            }
            par[0][TP_E1] = (uint8_t)smp_factory();
            par[0][TP_E4] = SM_LOOP;
            sample_run(69, 0.3f, &n);
            p1 = period_of(sbuf + 2205, 4096, 80, 120);
            sample_run(81, 0.2f, &n);
            p2 = period_of(sbuf + 2205, 4096, 40, 60);
            par[0][TP_E4] = SM_ONESHOT;
            stop = sample_run(69, 0.6f, &n);
            if (abs(p1 - 100) > 1 || abs(p2 - 50) > 1 || abs(stop - want) > 200) {
                printf("    %d Hz: period %d (100), an octave up %d (50), ends %d (%d)\n", RT[k], p1, p2, stop, want);
                ok = 0;
            }
        }
        check("11025 / 16000 / 22050 / 32000 Hz: the root's pitch and the sample's length at each", ok);
        gen_rate = SMP_RATE;
        bank_detach();
    }
    {   /* the windows: a sample running from the first into the second, one in the last; every sample decoded as
         * encoded, forwards and through the voice's reads backwards */
        static float fwd[40000];
        int i, ok2 = 1;
        uint32_t at = smp_reg_v(1) - 4096u;          /* its checkpoints 4 KB before the seam: its ADPCM across it */
        const smp_t *sm;
        smp_cache_t c;
        gen_hz = 300.0f;
        gen_rate = 16000;
        bank_reset();
        bank_add_at("seam", gen_sine, 30000, 60, 0, 16000, at);
        for (i = 0; i < 30000; i++)
            fwd[i] = (float)bank_pcm[i] * (1.0f / 32768.0f);
        gen_hz = 500.0f;
        bank_add_at("last", gen_sine, 8000, 60, 0, 11025, smp_reg_v(2) + 64u);
        bank_seal();
        bank_attach();
        check("a bank over the three windows: read, two samples", smp_user_scan() == 2);
        sm = smp_get(smp_factory());
        sprintf(what, "the seam's sample runs on into the next window (split at %u of %u)", sm ? sm->split : 0u,
                sm ? sm->n : 0u);
        check(what, sm && sm->adp2 && sm->split > 0u && sm->split < sm->n && !(sm->split & 1u));
        smp_cache_reset(&c);
        for (i = 0; sm && i < 30000; i++)
            if (smp_one(&c, sm, i) != fwd[i]) {
                printf("    sample %d: %f, encoded %f\n", i, (double)smp_one(&c, sm, i), (double)fwd[i]);
                ok2 = 0;
                break;
            }
        smp_cache_reset(&c);
        for (i = 29999; sm && ok2 && i >= 0; i--)    /* backwards: each window again from its checkpoint */
            if (smp_one(&c, sm, i) != fwd[i]) {
                printf("    backwards, sample %d differs\n", i);
                ok2 = 0;
            }
        check("across the seam: every sample as encoded, forwards and backwards", ok2);
        par[0][TP_E1] = (uint8_t)smp_factory();
        par[0][TP_E4] = SM_ONESHOT;
        stop = sample_run(60, 2.5f, &n);
        sprintf(what, "and the engine plays it whole at 16 kHz: %d frames (%d)", stop, 30000 * 44100 / 16000);
        check(what, abs(stop - 30000 * 44100 / 16000) < 200);
        sm = smp_get(smp_factory() + 1);
        check("a sample in the last window (PLAT_XDATA0) at 11025 Hz", sm && sm->rate == 11025 && sm->n == 8000 &&
                                                                   !sm->adp2);
        /* bounds: checkpoints across a seam, three windows for one sample, too long, the wrong size or version */
        bank_reset();
        bank_add_at("x", gen_sine, 8000, 60, 0, 16000, smp_reg_v(1) - 64u);   /* 500 B of checkpoints, 64 left */
        bank_seal();
        bank_attach();
        check("a sample whose checkpoints cross a window's end is refused", smp_user_scan() == 0);
        bank_reset();
        /* (its 12 KB of checkpoints before the first seam, its ADPCM on past the second window's end) */
        bank_add_at("x", gen_sine, 2 * (int)SMP_REG1_SIZE + 4096, 60, 0, 16000, smp_reg_v(1) - 14000u);
        bank_seal();
        bank_attach();
        check("a sample over three windows (two seams) is refused", smp_user_scan() == 0);
        bank_reset();
        bank_add("x", gen_sine, 8000, 60, 0, 16000);
        bank_seal();
        put32(bank + 8, SMP_BANK_SIZE - SMP_BANK_HDR + 4u);
        bank_attach();
        check("a bank longer than its windows is refused", smp_user_scan() == 0);
        bank_seal();
        put32(bank + 16, 0xD000u);
        bank_attach();
        check("a bank made for other windows is refused", smp_user_scan() == 0);
        bank_seal();
        bank[4] = 1;
        bank_attach();
        check("a version 1 bank is refused", smp_user_scan() == 0);
        bank_seal();
        bank[SMP_ENT0 + 36] = 0;
        bank[SMP_ENT0 + 37] = 0;
        bank_attach();
        check("a sample with no rate is refused", smp_user_scan() == 0);
        bank[SMP_ENT0 + 36] = 0x60;                 /* 60000 Hz */
        bank[SMP_ENT0 + 37] = 0xEA;
        bank_attach();
        check("a sample at 60 kHz is refused", smp_user_scan() == 0);
        bank[SMP_ENT0 + 36] = (uint8_t)16000;
        bank[SMP_ENT0 + 37] = (uint8_t)(16000 >> 8);
        bank[6] = SMP_NUSER + 1;
        bank_attach();
        check("33 samples are refused", smp_user_scan() == 0);
        bank[6] = 1;
        bank_attach();
        check("and the same bank, put right, is read", smp_user_scan() == 1);
        gen_rate = SMP_RATE;
        bank_detach();
    }
    reset();
}

/* ---- the slice engine ---- */
#define SL_SEC 2048                              /* the test loop: 8 sections of 2048 samples, section k a sine of */
static int sl_per(int k) { return 20 + 6 * k; } /* period 20 + 6k samples (at the native rate: twice that in frames) */
static float gen_sections(int i)
{
    int k = i / SL_SEC, p = sl_per(k);
    return (float)(20000.0 * sin(6.283185307179586 * (double)(i % p) / p));
}
static int sl_slice_seen;                        /* synth_slice_now's slice a few blocks in (-1 none) */
static int32_t sl_pos_seen;
/* as sample_run, a note held up to secs on track 0 (others may already play); also what synth_slice_now says
 * 10 blocks in */
static int slice_run(int note, float secs, int *n_out, int fresh)
{
    int finite = 1, n = 0, stop = -1, b, i, nb = (int)(secs * 44100.0f) / 32;
    if (fresh) {
        synth_all_off(S, -1);
        for (b = 0; b < 400 && synth_sounding(S, 0, NVOICE); b++)
            block(32, &finite);
    }
    synth_note_on(S, 0, note, 100, 0, 77);
    sl_slice_seen = -1;
    for (b = 0; b < nb; b++) {
        block(32, &finite);
        for (i = 0; i < 32; i++)
            sbuf[n++] = (L[i] + R[i]) * 0.5f;
        if (b == 10 && !synth_slice_now(S, 0, &sl_slice_seen, &sl_pos_seen))
            sl_slice_seen = -1;
        if (stop < 0 && !synth_sounding(S, 0, NVOICE))
            stop = n;
    }
    synth_note_off(S, 0, 77);
    if (!finite)
        stop = -2;
    *n_out = n;
    return stop;
}
static float max_step(const float *x, int n)    /* the largest jump from a sample to the next */
{
    float m = 0.0f;
    int i;
    for (i = 1; i < n; i++)
        m = fabs_f(x[i] - x[i - 1]) > m ? fabs_f(x[i] - x[i - 1]) : m;
    return m;
}
static float peak_of(const float *x, int n)
{
    float m = 0.0f;
    int i;
    for (i = 0; i < n; i++)
        m = fabs_f(x[i]) > m ? fabs_f(x[i]) : m;
    return m;
}
static void slice_tests(void)
{
    int n, stop, k, ok, root = 60;
    char what[160], num[16], unit[8];
    const smp_t *brk;
    float pk;
    printf("-- the slice engine\n");
    reset();
    sound_default(par[0], EG_SLICE);
    brk = smp_get(par[0][TP_E1]);
    sprintf(what, "its default: the factory break (%s, %d bar, root %d), 8 slices, pitch 0, gate full",
            brk ? brk->name : "-", brk ? brk->bars : -1, brk ? brk->root : -1);
    check(what, brk && !strcmp(brk->name, "classic") && brk->bars == 1 && brk->root == 53 &&
                    sl_count(par[0][TP_E2]) == 8 && par[0][TP_E3] == 64 && par[0][TP_E4] == 127);
    tp_text(TP_E2, 2, EG_SLICE, num, unit);
    check("slices 2 shows 16", !strcmp(num, "16"));
    tp_text(TP_E3, 76, EG_SLICE, num, unit);
    check("pitch 76 shows +12 st", !strcmp(num, "+12") && !strcmp(unit, "st"));
    check("pitch spans 40..88 (+-24 st)", tp_info(TP_E3, EG_SLICE)->lo == 40 && tp_info(TP_E3, EG_SLICE)->hi == 88);
    par[0][TP_A] = 0;
    par[0][TP_S] = 127;
    par[0][TP_R] = 127;                          /* the gate decides, not the note */
    par[0][TP_CUT] = 127;
    par[0][TP_RES] = 0;
    par[0][TP_KTRK] = 0;
    par[0][TP_FTYPE] = FT_LP12;
    /* a test loop in the user bank: 8 sections, each its own pitch, its own rate (no bars) */
    bank_gen(gen_sections, 8 * SL_SEC, root, 0);
    check("the test loop: read", smp_user_scan() == 1);
    par[0][TP_E1] = (uint8_t)smp_factory();
    par[0][TP_E2] = 1;                           /* 8 slices: a section each */
    ok = 1;
    for (k = -2; k < 11; k++) {                  /* below the root, the root, up, and past the eighth: wrapping */
        int want = ((k % 8) + 8) % 8, p;
        stop = slice_run(root + k, 0.2f, &n, 1);
        p = period_of(sbuf + 600, 2048, 30, 140);
        if (abs(p - 2 * sl_per(want)) > 1 || sl_slice_seen != want || abs(stop - 2 * SL_SEC) > 64) {
            printf("    note root%+d: period %d (%d), slice %d (%d), ends %d (%d)\n", k, p, 2 * sl_per(want),
                   sl_slice_seen, want, stop, 2 * SL_SEC);
            ok = 0;
        }
    }
    check("8 slices: note root+k plays slice k mod 8 (its section's pitch, synth_slice_now), wrapping both ways", ok);
    ok = 1;
    for (k = 0; k < SL_NCOUNT; k++) {            /* 4, 8, 16, 32: a slice of 16384 / count samples */
        int cnt = sl_count(k), len = 8 * SL_SEC / cnt, sl = 3 % cnt, sec = sl * len / SL_SEC, p;
        par[0][TP_E2] = (uint8_t)k;
        stop = slice_run(root + 3, 0.5f, &n, 1);
        p = period_of(sbuf + 200, 600, 30, 140);
        if (abs(stop - 2 * len) > 64 || abs(p - 2 * sl_per(sec)) > 1 || sl_slice_seen != sl) {
            printf("    %d slices: ends %d (%d), period %d (%d), slice %d\n", cnt, stop, 2 * len, p, 2 * sl_per(sec), sl_slice_seen);
            ok = 0;
        }
    }
    check("4 / 8 / 16 / 32 slices: the slice's length and place", ok);
    par[0][TP_E2] = 1;
    par[0][TP_E4] = 64;                          /* the gate: about half the slice */
    stop = slice_run(root + 1, 0.2f, &n, 1);
    sprintf(what, "gate 64: the slice plays %d frames (%d)", stop, (int)(64.0f / 127.0f * SL_SEC) * 2);
    check(what, abs(stop - (int)(64.0f / 127.0f * SL_SEC) * 2) < 64);
    par[0][TP_E4] = 0;
    stop = slice_run(root + 1, 0.2f, &n, 1);
    sprintf(what, "gate 0: still a short slice, %d frames (%d)", stop, (int)(0.02f * SL_SEC) * 2);
    check(what, stop > 0 && abs(stop - (int)(0.02f * SL_SEC) * 2) < 64);
    par[0][TP_E4] = 127;
    par[0][TP_E3] = 76;                          /* +12: twice as fast, an octave up */
    stop = slice_run(root + 2, 0.2f, &n, 1);
    k = period_of(sbuf + 300, 1024, 15, 80);
    sprintf(what, "pitch +12: the slice in %d frames (%d), its period %d (%d)", stop, SL_SEC, k, sl_per(2));
    check(what, abs(stop - SL_SEC) < 64 && abs(k - sl_per(2)) <= 1);
    par[0][TP_E3] = 64;
    {   /* no click: the fades at the gate's end, and the switch of a mono retrigger and of legato */
        float a, jump, tail, head;
        stop = slice_run(root + 0, 0.2f, &n, 1);
        a = peak_of(sbuf + 1000, 2000);
        jump = max_step(sbuf, n);
        tail = peak_of(sbuf + stop - 24, 24);
        head = peak_of(sbuf, 4);
        sprintf(what, "fades: the slice's first frames %.3f, last 24 %.3f of its peak %.3f; no jump over %.3f",
                (double)head, (double)tail, (double)a, (double)jump);
        check(what, a > 0.05f && head < 0.15f * a && tail < 0.3f * a && jump < 0.25f * a);
        par[0][TP_E4] = 50;                      /* cut mid-slice (the sine at full swing there) */
        stop = slice_run(root + 0, 0.2f, &n, 1);
        tail = peak_of(sbuf + stop - 24, 24);
        jump = max_step(sbuf, n);
        sprintf(what, "a gate cutting mid-slice fades out: last 24 frames %.3f, no jump over %.3f", (double)tail, (double)jump);
        check(what, tail < 0.3f * a && jump < 0.25f * a);
        par[0][TP_E4] = 127;
        {   /* the gate turned down behind the playing position: a fade, then the end */
            int finite = 1, b, i, gone = -1;
            synth_all_off(S, -1);
            for (b = 0; b < 400 && synth_sounding(S, 0, NVOICE); b++)
                block(32, &finite);
            n = 0;
            synth_note_on(S, 0, root + 2, 100, 0, 1);
            for (b = 0; b < 60; b++) {
                if (b == 40)
                    par[0][TP_E4] = 10;
                block(32, &finite);
                for (i = 0; i < 32; i++)
                    sbuf[n++] = L[i];
                if (gone < 0 && !synth_sounding(S, 0, NVOICE))
                    gone = n;
            }
            synth_note_off(S, 0, 1);
            par[0][TP_E4] = 127;
            jump = max_step(sbuf, n);
            sprintf(what, "the gate turned down mid-slice: it fades and ends (%d frames after), no jump over %.3f",
                    gone - 40 * 32, (double)jump);
            check(what, gone > 40 * 32 && gone < 40 * 32 + 200 && jump < 0.25f * a && finite);
        }
        par[0][TP_VMODE] = VM_MONO;
        {
            int finite = 1, b, i;
            synth_all_off(S, -1);
            for (b = 0; b < 400 && synth_sounding(S, 0, NVOICE); b++)
                block(32, &finite);
            n = 0;
            synth_note_on(S, 0, root + 1, 100, 0, 1);
            for (b = 0; b < 30; b++) {
                block(32, &finite);
                for (i = 0; i < 32; i++)
                    sbuf[n++] = L[i];
            }
            synth_note_on(S, 0, root + 5, 100, 0, 2);   /* mono: the new note's slice, faded across */
            for (b = 0; b < 30; b++) {
                block(32, &finite);
                for (i = 0; i < 32; i++)
                    sbuf[n++] = L[i];
            }
            synth_slice_now(S, 0, &k, &sl_pos_seen);
            jump = max_step(sbuf, n);
            pk = (float)period_of(sbuf + 30 * 32 + 200, 600, 30, 140);
            sprintf(what, "mono retrigger: slice %d (5) from its start (period %d, %d), no jump over %.3f", k, (int)pk,
                    2 * sl_per(5), (double)jump);
            check(what, k == 5 && abs((int)pk - 2 * sl_per(5)) <= 1 && jump < 0.25f * a && finite &&
                            sl_pos_seen >= 5 * SL_SEC && sl_pos_seen < 5 * SL_SEC + 500);
            synth_note_off(S, 0, 1);
            synth_note_off(S, 0, 2);
            par[0][TP_VMODE] = VM_LEGATO;
            synth_all_off(S, -1);
            for (b = 0; b < 400 && synth_sounding(S, 0, NVOICE); b++)
                block(32, &finite);
            n = 0;
            synth_note_on(S, 0, root + 1, 100, 0, 1);
            for (b = 0; b < 20; b++) {
                block(32, &finite);
                for (i = 0; i < 32; i++)
                    sbuf[n++] = L[i];
            }
            synth_note_on(S, 0, root + 4, 100, 0, 2);   /* legato: slice 4 at the same place, the time running on */
            for (b = 0; b < 10; b++) {
                block(32, &finite);
                for (i = 0; i < 32; i++)
                    sbuf[n++] = L[i];
            }
            synth_slice_now(S, 0, &k, &sl_pos_seen);
            jump = max_step(sbuf, n);
            sprintf(what, "legato: slice %d (4), at %d into it after %d frames (%d), no jump over %.3f", k,
                    (int)(sl_pos_seen - 4 * SL_SEC), n, n / 2, (double)jump);
            check(what, k == 4 && abs((int)(sl_pos_seen - 4 * SL_SEC) - n / 2) < 40 && jump < 0.25f * a && finite);
            synth_note_off(S, 0, 1);
            synth_note_off(S, 0, 2);
            par[0][TP_VMODE] = VM_POLY;
            /* poly: two notes, two slices, each its own */
            synth_all_off(S, -1);
            for (b = 0; b < 400 && synth_sounding(S, 0, NVOICE); b++)
                block(32, &finite);
            synth_note_on(S, 0, root + 2, 100, 0, 1);
            for (b = 0; b < 10; b++)
                block(32, &finite);
            synth_note_on(S, 0, root + 6, 100, 0, 2);
            for (b = 0; b < 10; b++)
                block(32, &finite);
            synth_slice_now(S, 0, &k, &sl_pos_seen);
            check("poly: two notes play two slices at once (the newest shown)", synth_voices_used(S) == 2 && k == 6);
            synth_note_off(S, 0, 1);
            synth_note_off(S, 0, 2);
        }
    }
    smp_user_attach(0);
    smp_user_scan();
    /* the tempo: the factory break states 1 bar */
    par[0][TP_E1] = SMP_BREAK;
    par[0][TP_E2] = 1;
    brk = smp_get(SMP_BREAK);
    {
        int want120 = 44100 * 2 / 8, /* an eighth of a bar at 120 BPM, whatever the break's rate */
            want90 = (int)((float)want120 * 120.0f / 90.0f), s120, s90, s12;
        synth_tempo(S, 120.0f);
        s120 = slice_run(53 + 2, 0.5f, &n, 1);
        synth_tempo(S, 90.0f);
        s90 = slice_run(53 + 2, 0.5f, &n, 1);
        par[0][TP_E3] = 52;                      /* -12 at 90: twice as long again */
        s12 = slice_run(53 + 2, 0.9f, &n, 1);
        par[0][TP_E3] = 64;
        synth_tempo(S, 120.0f);
        sprintf(what, "tempo: the break's eighth lasts %d frames at 120 BPM (%d), %d at 90 (%d), %d at 90 and -12",
                s120, want120, s90, want90, s12);
        check(what, abs(s120 - want120) < 64 && abs(s90 - want90) < 64 && abs(s12 - 2 * want90) < 128);
    }
    {   /* a user loop at its own rate: 16 kHz, no bars: a slice of n / 8 samples lasts n / 8 / 16000 s; stating
         * 1 bar, an eighth of the bar at the tempo, whatever its rate */
        int s16, sb, want16 = (int)((float)(8 * SL_SEC / 8) * 44100.0f / 16000.0f), wantb = 44100 * 2 / 8;
        bank_reset();
        bank_add("own", gen_sections, 8 * SL_SEC, 53, 0, 16000);
        bank_add("bar", gen_sections, 8 * SL_SEC, 53, 1, 16000);
        bank_seal();
        bank_attach();
        smp_user_scan();
        synth_tempo(S, 120.0f);
        par[0][TP_E1] = (uint8_t)smp_factory();
        s16 = slice_run(53 + 2, 0.5f, &n, 1);
        par[0][TP_E1] = (uint8_t)(smp_factory() + 1);
        sb = slice_run(53 + 2, 0.5f, &n, 1);
        sprintf(what, "a 16 kHz loop: a slice %d frames at its own speed (%d), %d as a bar at 120 (%d)", s16, want16,
                sb, wantb);
        check(what, abs(s16 - want16) < 64 && abs(sb - wantb) < 64);
        bank_detach();
        par[0][TP_E1] = SMP_BREAK;
    }
    {   /* bounded and finite: every factory sample and an empty slot, each count, pitch and gate at the ends,
         * notes 0 and 127 */
        int sm, c, pi, g, nt;
        float m = 0.0f;
        ok = 1;
        for (sm = 0; sm <= smp_factory(); sm++)
            for (c = 0; c < SL_NCOUNT; c++)
                for (pi = 40; pi <= 88; pi += 48)
                    for (g = 0; g <= 127; g += 127)
                        for (nt = 0; nt <= 127; nt += 127) {
                            par[0][TP_E1] = (uint8_t)sm;
                            par[0][TP_E2] = (uint8_t)c;
                            par[0][TP_E3] = (uint8_t)pi;
                            par[0][TP_E4] = (uint8_t)g;
                            stop = slice_run(nt, 0.1f, &n, 1);
                            pk = peak_of(sbuf, n);
                            m = pk > m ? pk : m;
                            if (stop == -2 || pk > 1.0f)
                                ok = 0;
                        }
        sprintf(what, "every sample x count x pitch / gate ends x note 0 / 127: finite, peak %.2f", (double)m);
        check(what, ok);
    }
    reset();
}

static void bench(void)
{
    int e;
    double m, w, orbit_mean = 0.0;
    printf("\nbenchmark: 16 voices, lp24, instructions per sample per voice (mean / worst block)\n");
    for (e = 0; e < NENGINE; e++) {
        if (!eg_synth(e))
            continue;
        int t, b, finite = 1;
        uint64_t total = 0, worst = 0;
        reset();
        for (t = 0; t < NTRK; t++) {
            sound_default(par[t], e);
            par[t][TP_FTYPE] = FT_LP24;
            par[t][TP_E4] = e == EG_BELLOWS ? 8 : 100;   /* stereo where the engine has it: the dear case */
            par[t][TP_E3] = e == EG_BELLOWS ? 8 : 60;
            if (e == EG_SAMPLE) {                /* the sung ah, looped: every voice sounds throughout */
                par[t][TP_E1] = 4;
                par[t][TP_E2] = 0;
                par[t][TP_E3] = 127;
                par[t][TP_E4] = SM_LOOP;
            }
            if (e == EG_SLICE) {                 /* a quarter of the break, two octaves down at 60 BPM: 4 s a voice */
                par[t][TP_E1] = SMP_BREAK;
                par[t][TP_E2] = 0;
                par[t][TP_E3] = 40;
                par[t][TP_E4] = 127;
                par[t][TP_S] = 127;
                synth_tempo(S, 60.0f);
            }
            par[t][TP_LTYPE] = LT_WAVE;
            par[t][TP_LDEST] = LD_FILTER;
            par[t][TP_LX] = 0;
        }
        for (t = 0; t < NVOICE; t++)
            synth_note_on(S, t % NTRK, 48 + t * 2, 100, 0, (uint32_t)t);
        for (b = 0; b < 400; b++) {
            uint64_t i0 = instr_now(), di;
            block(N, &finite);
            di = instr_now() - i0;
            if (b >= 20) {
                total += di;
                if (di > worst)
                    worst = di;
            }
        }
        if (e == EG_RAYON)
            orbit_mean = (double)total / (380.0 * N * NVOICE);
#ifdef __APPLE__
        printf("  %-10s %6.1f / %6.1f   (%d voices sounding)\n", EG_NAME[e], (double)total / (380.0 * N * NVOICE),
               (double)worst / (double)(N * NVOICE), synth_voices_used(S));
#else
        printf("  %-10s %6.3f us / sample / voice (clock)\n", EG_NAME[e],
               (double)total / (380.0 * N * NVOICE) / (double)CLOCKS_PER_SEC * 1e6);
#endif
    }
    m = bench_weft(1, &w);
    printf("  weft mono  %6.1f / %6.1f   (spread 0, saw .. pulse)\n", m, w);
    {
        double mr, wr, orbit;
        mr = bench_weft(2, &wr);
        printf("  weft reso  %6.1f / %6.1f   (spread 0, the resonant half)\n", mr, wr);
        orbit = orbit_mean;
#ifdef __APPLE__
        check("weft (mono, either half) costs about orbit's (within 1.35x)", m < orbit * 1.35 && mr < orbit * 1.35);
#else
        (void)orbit;
#endif
    }
}

/* ---- the character stage ---- */
static const char *const CT_N[NCT] = {"off", "drive", "fold", "crush", "ring", "tilt", "tape", "sampler"};
/* a held note's RMS (left, blocks 30..90) on track 0 as par[0] stands; its samples into buf when given */
static float held_rms(int note, float *buf, int *finite)
{
    int b, i;
    double e = 0.0;
    synth_init(S, par);
    synth_tempo(S, 120.0f);
    synth_note_on(S, 0, note, 100, 0, 1);
    for (b = 0; b < 90; b++) {
        block(N, finite);
        if (b < 30)
            continue;
        for (i = 0; i < N; i++)
            e += (double)L[i] * (double)L[i];
        if (buf)
            memcpy(buf + (b - 30) * N, L, sizeof L);
    }
    return (float)sqrt(e / (60.0 * N));
}
/* a plain held sound for the stage: an engine's default, the filter open, sustain full */
static void char_sound(int engine, int type, int amt, int tone, int mix)
{
    reset();
    sound_default(par[0], engine);
    par[0][TP_FTYPE] = FT_LP12;
    par[0][TP_CUT] = 127;
    par[0][TP_RES] = 0;
    par[0][TP_S] = 127;
    par[0][TP_VOLUME] = 100;
    par[0][TP_CTYPE] = (uint8_t)type;
    par[0][TP_CAMT] = (uint8_t)amt;
    par[0][TP_CTONE] = (uint8_t)tone;
    par[0][TP_CMIX] = (uint8_t)mix;
}
/* the energy at hz in n samples (Goertzel), relative to the whole */
static double tone_at(const float *x, int n, double hz)
{
    double w = 2.0 * 3.14159265358979 * hz / 44100.0, c = 2.0 * cos(w), s1 = 0.0, s2 = 0.0, e = 0.0, p;
    int i;
    for (i = 0; i < n; i++) {
        double s0 = (double)x[i] * (0.5 - 0.5 * cos(2.0 * 3.14159265358979 * i / n)) + c * s1 - s2;
        s2 = s1;
        s1 = s0;
        e += (double)x[i] * (double)x[i];
    }
    p = s1 * s1 + s2 * s2 - c * s1 * s2;
    return e > 0.0 ? p / (e * n) : 0.0;
}
static void char_tests(void)
{
    static float buf[60 * N], ref[60 * N];
    char num[16], unit[8], what[96];
    int ty, finite = 1, i, ok;
    float pk, r0, r1;
    uint8_t p[NTP];
    /* texts and tables */
    sound_default(p, EG_KNIT);
    check("character: off by default, mix 100 %", p[TP_CTYPE] == CT_OFF && p[TP_CMIX] == 127);
    check("character knobs: type amount tone mix", !strcmp(tp_info(TP_CTYPE, 0)->name, "type") &&
                                                     !strcmp(tp_info(TP_CAMT, 0)->name, "amount") &&
                                                     !strcmp(tp_info(TP_CTONE, 0)->name, "tone") &&
                                                     !strcmp(tp_info(TP_CMIX, 0)->name, "mix"));
    for (ty = 0; ty < NCT; ty++) {
        tp_text(TP_CTYPE, ty, EG_KNIT, num, unit);
        if (strcmp(num, CT_N[ty]))
            check("character type names", 0);
    }
    p[TP_CTYPE] = CT_RING;
    tp_text_par(TP_CTONE, 48, p, num, unit);
    check("ring: tone 48 is the ratio 1.5:1", !strcmp(num, "1.5:1"));
    p[TP_CTYPE] = CT_DRIVE;
    tp_text_par(TP_CTONE, 127, p, num, unit);
    check("drive: tone 127 is 20 kHz (open)", !strcmp(num, "20.0") && !strcmp(unit, "kHz"));
    tp_text_par(TP_CTONE, 0, p, num, unit);
    check("drive: tone 0 is a 400 Hz low-pass", !strcmp(num, "400") && !strcmp(unit, "Hz"));
    p[TP_CTYPE] = CT_TILT;
    tp_text_par(TP_CAMT, 127, p, num, unit);
    check("tilt: amount 127 is a 6 kHz pivot", !strcmp(num, "6.0") && !strcmp(unit, "kHz"));
    p[TP_LTYPE] = LT_WAVE;
    p[TP_LDEST] = LD_AMP;
    tp_text_par(TP_LX, 3, p, num, unit);
    check("the LFO's voice module, knob 4: the character's amount", !strcmp(num, "character"));

    /* every type, the knobs at their ends and the middle, a resonant filter and a stereo engine: finite,
     * bounded, silent after the release */
    ok = 1;
    for (ty = 1; ty < NCT; ty++) {
        int a, t, e;
        for (e = 0; e < 2; e++)
            for (a = 0; a <= 127; a += 127)
                for (t = 0; t <= 128; t += 64) {
                    reset();
                    sound_default(par[0], e ? EG_KNIT : EG_KNIT);
                    if (e)
                        par[0][TP_E4] = 100;   /* weft spread: stereo */
                    par[0][TP_RES] = 127;
                    par[0][TP_VOLUME] = 127;
                    par[0][TP_CTYPE] = (uint8_t)ty;
                    par[0][TP_CAMT] = (uint8_t)a;
                    par[0][TP_CTONE] = (uint8_t)(t > 127 ? 127 : t);
                    if (!note_test("character", 0, &pk, 0)) {
                        printf("    %s amount %d tone %d: peak %.1f dBFS\n", CT_N[ty], a, t, (double)db(pk));
                        ok = 0;
                    }
                }
    }
    check("character: every type stays finite, bounded, and dies", ok);

    /* drive and fold keep the level (+-3 dB) at mid amount; and they change the sound */
    for (ty = CT_DRIVE; ty <= CT_TILT; ty++) {
        int tone = ty == CT_DRIVE ? 127 : 64;
        char_sound(EG_KNIT, CT_OFF, 64, tone, 127);
        par[0][TP_E1] = 0;                       /* a saw */
        r0 = held_rms(57, ref, &finite);
        char_sound(EG_KNIT, ty, 64, tone, 127);
        par[0][TP_E1] = 0;
        if (ty == CT_TILT)
            par[0][TP_CTONE] = 100;
        r1 = held_rms(57, buf, &finite);
        printf("    %-8s at amount 64: %+.1f dB\n", CT_N[ty], (double)(db(r1) - db(r0)));
        if (ty == CT_DRIVE || ty == CT_FOLD) {
            sprintf(what, "%s at mid amount keeps the level (+-3 dB)", CT_N[ty]);
            check(what, fabs((double)(db(r1) - db(r0))) <= 3.0);
        }
        ok = 0;
        for (i = 0; i < 60 * N; i++)
            ok |= fabs_f(buf[i] - ref[i]) > 1e-3f;
        sprintf(what, "%s changes the sound", CT_N[ty]);
        check(what, ok);
    }
    for (ty = CT_DRIVE; ty <= CT_FOLD; ty++) {   /* and at full amount, a quiet and a loud input */
        float lo0, lo1;
        char_sound(EG_KNIT, CT_OFF, 127, 127, 127);
        par[0][TP_E1] = 0;
        r0 = held_rms(57, 0, &finite);
        par[0][TP_CTYPE] = (uint8_t)ty;
        r1 = held_rms(57, 0, &finite);
        char_sound(EG_KNIT, CT_OFF, 127, 127, 127);
        par[0][TP_E1] = 0;
        par[0][TP_CUT] = 40;                     /* (the filter low: a quieter input) */
        lo0 = held_rms(57, 0, &finite);
        par[0][TP_CTYPE] = (uint8_t)ty;
        lo1 = held_rms(57, 0, &finite);
        printf("    %-8s at amount 127: %+.1f dB (a quiet input %+.1f dB)\n", CT_N[ty], (double)(db(r1) - db(r0)),
               (double)(db(lo1) - db(lo0)));
        sprintf(what, "%s at full amount stays within 6 dB", CT_N[ty]);
        check(what, fabs((double)(db(r1) - db(r0))) <= 6.0);
    }

    /* crush: the bits (tone 127) leave few levels; the rate (tone 0) holds samples */
    {
        int nl = 0, held = 0;
        float lv[64];
        char_sound(EG_KNIT, CT_CRUSH, 127, 127, 127);
        par[0][TP_E4] = 0;
        held_rms(57, buf, &finite);
        r0 = 0.0f;
        for (i = 0; i < 60 * N; i++)
            if (fabs_f(buf[i]) > r0)
                r0 = fabs_f(buf[i]);
        for (i = 40 * N; i < 60 * N && nl < 64; i++) {   /* (the gain ramps have settled by here) */
            int j, seen = 0;
            for (j = 0; j < nl; j++)
                seen |= fabs_f(lv[j] - buf[i]) < r0 * 0.01f;
            if (!seen)
                lv[nl++] = buf[i];
        }
        printf("    crush bits at amount 127: %d levels\n", nl);
        check("crush (bits) quantises: at most 12 levels", nl <= 12);
        char_sound(EG_KNIT, CT_CRUSH, 127, 0, 127);
        par[0][TP_E4] = 0;
        held_rms(57, buf, &finite);
        for (i = 40 * N + 1; i < 60 * N; i++)
            held += fabs_f(buf[i] - buf[i - 1]) < r0 * 1e-3f;
        printf("    crush rate at amount 127: %.0f %% of samples held\n", 100.0 * held / (20.0 * N));
        check("crush (rate) holds samples: 90 % repeat", held > 18 * N);
    }

    /* ring: a sine (orbit, bright 0) at A3 times one at 1.5 x: sidebands at 0.5 and 2.5 x, the carrier gone */
    {
        double c, lo, hi, f = 220.0;
        char_sound(EG_RAYON, CT_RING, 127, 48, 127);
        par[0][TP_E1] = 0;
        par[0][TP_E4] = 0;
        par[0][TP_FTYPE] = FT_DYNAMIC;
        held_rms(57, buf, &finite);
        c = tone_at(buf + 20 * N, 32 * N, f);
        lo = tone_at(buf + 20 * N, 32 * N, f * 0.5);
        hi = tone_at(buf + 20 * N, 32 * N, f * 2.5);
        printf("    ring x1.5 on 220 Hz: 220 Hz %.4f, 110 Hz %.4f, 550 Hz %.4f\n", c, lo, hi);
        check("ring makes sidebands (0.5 and 2.5 x) and takes the carrier", lo > c * 20.0 && hi > c * 20.0);
    }

    /* a flat tilt (tone 64) passes the voice as it is: the stage's path (filter, stage, outputs) matches the plain
     * one, mono and stereo, every filter type */
    {
        int st, ft;
        float worst = 0.0f;
        for (st = 0; st < 2; st++)
            for (ft = 0; ft < NFT; ft++) {
                char_sound(EG_KNIT, CT_OFF, 64, 64, 127);
                par[0][TP_E4] = (uint8_t)(st ? 100 : 0);
                par[0][TP_FTYPE] = (uint8_t)ft;
                par[0][TP_CUT] = 90;
                held_rms(57, ref, &finite);
                par[0][TP_CTYPE] = CT_TILT;
                held_rms(57, buf, &finite);
                for (i = 0; i < 60 * N; i++)
                    if (fabs_f(buf[i] - ref[i]) > worst)
                        worst = fabs_f(buf[i] - ref[i]);
            }
        printf("    a flat tilt against off: %g at most\n", (double)worst);
        check("a flat tilt matches off (mono, stereo, every filter)", worst < 1e-4f);
    }
    /* mix 0: a bypass, sample for sample; mix 127 is not */
    char_sound(EG_KNIT, CT_OFF, 100, 64, 127);
    held_rms(60, ref, &finite);
    char_sound(EG_KNIT, CT_FOLD, 100, 64, 0);
    held_rms(60, buf, &finite);
    check("character mix 0 is bit-identical to off", !memcmp(buf, ref, sizeof buf));
    char_sound(EG_KNIT, CT_FOLD, 100, 64, 127);
    held_rms(60, buf, &finite);
    check("character mix 100 % differs", memcmp(buf, ref, sizeof buf) != 0);

    /* a lock on the amount and the LFO on it change the sound */
    char_sound(EG_KNIT, CT_DRIVE, 0, 127, 127);
    held_rms(60, ref, &finite);
    synth_init(S, par);
    synth_lock(S, 0, TP_CAMT, 127);
    synth_note_on(S, 0, 60, 100, 0, 1);
    for (i = 0; i < 60; i++)
        block(N, &finite);
    ok = 0;
    for (i = 0; i < N; i++)
        ok |= fabs_f(L[i] - ref[29 * N + i]) > 1e-3f;
    check("a lock on the character's amount changes the sound", ok);
    char_sound(EG_KNIT, CT_DRIVE, 0, 127, 127);
    par[0][TP_LTYPE] = LT_WAVE;
    par[0][TP_LSPEED] = 100;
    par[0][TP_LAMT] = 127;
    par[0][TP_LDEST] = LD_AMP + LD_FREE;
    par[0][TP_LX] = 3;
    held_rms(60, buf, &finite);
    check("the LFO on the character's amount changes the sound", memcmp(buf, ref, sizeof buf) != 0);
    check("character renders stay finite", finite);
}

/* 16 voices of stack, stereo (spread up) and mono, lp24, through each character type: instructions per sample
 * per voice */
static void bench_char(void)
{
    int ty, st;
    double off = 0.0;
    for (st = 1; st >= 0; st--)
    for (ty = 0; ty < NCT; ty++) {
        if (ty == 0)
            printf("\nbenchmark: the character stage, 16 voices of stack (%s), lp24 (mean / worst block, + over off)\n",
                   st ? "stereo" : "mono");
        int t, b, finite = 1;
        uint64_t total = 0, worst = 0;
        double m;
        reset();
        for (t = 0; t < NTRK; t++) {
            sound_default(par[t], EG_KNIT);
            par[t][TP_FTYPE] = FT_LP24;
            par[t][TP_E4] = (uint8_t)(st ? 100 : 0);
            par[t][TP_CTYPE] = (uint8_t)ty;
            par[t][TP_CAMT] = 90;
            par[t][TP_CTONE] = 100;
        }
        for (t = 0; t < NVOICE; t++)
            synth_note_on(S, t % NTRK, 48 + t * 2, 100, 0, (uint32_t)t);
        for (b = 0; b < 400; b++) {
            uint64_t i0 = instr_now(), di;
            block(N, &finite);
            di = instr_now() - i0;
            if (b >= 20) {
                total += di;
                if (di > worst)
                    worst = di;
            }
        }
        m = (double)total / (380.0 * N * NVOICE);
        if (ty == 0)
            off = m;
#ifdef __APPLE__
        printf("  %-10s %6.1f / %6.1f   %+6.1f\n", CT_N[ty], m, (double)worst / (double)(N * NVOICE), m - off);
#else
        printf("  %-10s %6.3f us / sample / voice (clock)\n", CT_N[ty], m / (double)CLOCKS_PER_SEC * 1e6);
#endif
    }
}

int main(void)
{
    int e, k, t, finite = 1, b;
    float pk;
    char num[16], unit[8];
    static float fp1[N], fp2[N];

    {
        int synth_sizeof(void);
        printf("synth_t: %d of %d bytes\n", synth_sizeof(), SYNTH_BYTES);
    }
    /* the parameter tables */
    check("every parameter has info and text", 1);
    for (k = 0; k < NTP; k++) {
        const tp_info_t *i = tp_info(k, EG_KNIT);
        int v;
        if (!i->name || i->lo > i->hi || i->def < i->lo || i->def > i->hi) {
            printf("    parameter %d\n", k);
            check("parameter info ranges", 0);
            break;
        }
        for (v = i->lo; v <= i->hi; v++)
            tp_text(k, v, EG_KNIT, num, unit);
    }
    for (e = 0; e < NENGINE; e++)
        for (k = 0; k < 4; k++) {
            const tp_info_t *i = tp_info(TP_E1 + k, e);
            int v;
            if (!i->name[0] && e != EG_DRUMLINE) {
                check("engine knob names", 0);
                break;
            }
            if (i->def < i->lo || i->def > i->hi)
                check("engine knob defaults in range", 0);
            for (v = i->lo; v <= i->hi; v++)
                tp_text(TP_E1 + k, v, e, num, unit);
        }
    tp_text(TP_LSPEED, 3, 0, num, unit);
    check("LFO speed 3 shows 1/4", !strcmp(num, "1/4"));
    tp_text(TP_LSPEED, 0, 0, num, unit);
    check("LFO speed 0 shows 2/1", !strcmp(num, "2/1"));
    tp_text(TP_LSPEED, 8, 0, num, unit);
    check("LFO speed 8 shows 0.1 Hz", !strcmp(num, "0.1") && !strcmp(unit, "Hz"));
    tp_text(TP_LSPEED, 127, 0, num, unit);
    check("LFO speed 127 shows 30.0 Hz", !strcmp(num, "30.0") && !strcmp(unit, "Hz"));
    tp_text(TP_A, 0, 0, num, unit);
    check("attack 0 shows 1 ms", !strcmp(num, "1") && !strcmp(unit, "ms"));
    tp_text(TP_LDEST, LD_FILTER, 0, num, unit);
    check("LFO destination shows the module", !strcmp(num, "filter"));
    tp_text(TP_LDEST, LD_FILTER + LD_FREE, 0, num, unit);
    check("LFO destination's free version", !strcmp(num, "filter free"));
    tp_text(TP_DRUM(2, DP_MODEL), 2, EG_DRUMLINE, num, unit);
    check("drum model text", !strcmp(num, "hat"));
    {
        uint8_t p[NTP];
        sound_default(p, EG_DRUMLINE);
        check("a drum key's knobs take the model's names", !strcmp(tp_info_par(TP_DRUM(4, DP_K3), p)->name, "spread"));
        check("a drum key's default kit", p[TP_DRUM(0, DP_MODEL)] == DM_KICK && p[TP_DRUM(0, DP_VOLUME)] == 76);
        tp_text_par(TP_DRUM(0, DP_VOLUME), 76, p, num, unit);
        check("drum volume 76 is 0 dB", !strcmp(num, "0") && !strcmp(unit, "dB"));
        check("a drum track's filter is dynamic", p[TP_FTYPE] == FT_DYNAMIC && !strcmp(tp_info(TP_CUT, EG_DRUMLINE)->name, "lowcut"));
        check("a drum track's envelope is lag / attack / random / length", !strcmp(tp_info(TP_A, EG_DRUMLINE)->name, "lag"));
    }
    {
        uint8_t p[NTP];
        sound_default(p, EG_KNIT);
        p[TP_PLAYER] = PL_STRUM;
        check("player knob names follow the player", !strcmp(tp_info_par(TP_P1, p)->name, "roll"));
        p[TP_PLAYER] = PL_ARP;
        check("the arp's second page", !strcmp(tp_info_par(TP_AP2, p)->name, "style"));
        tp_text_par(TP_AP2, 1, p, num, unit);
        check("arp style 1 is pedal", !strcmp(num, "pedal"));
        tp_text_par(TP_AP2, 5, p, num, unit);
        check("arp style 5 is swap", !strcmp(num, "swap"));
        tp_text_par(TP_P2, 2, p, num, unit);
        check("arp pattern 2 is bounce", !strcmp(num, "bounce"));
        tp_text_par(TP_P2, 3, p, num, unit);
        check("arp pattern 3 is bounce+", !strcmp(num, "bounce+"));
        tp_text_par(TP_LTYPE, LT_WAVE, p, num, unit);
        check("LFO types: wave", !strcmp(num, "wave"));
        tp_text_par(TP_LTYPE, LT_WOBBLE, p, num, unit);
        check("LFO types: wobble", !strcmp(num, "wobble"));
        tp_text_par(TP_LTYPE, LT_DRIFT, p, num, unit);
        check("LFO types: drift", !strcmp(num, "drift"));
        tp_text_par(TP_LTYPE, LT_PUMP, p, num, unit);
        check("LFO types: pump", !strcmp(num, "pump"));
        p[TP_LTYPE] = LT_WAVE;
        p[TP_LDEST] = LD_FILTER;
        tp_text_par(TP_LX, 0, p, num, unit);
        check("wave LFO: the filter module's knob 0 is cutoff", !strcmp(num, "cutoff"));
        p[TP_LDEST] = LD_SYNTH + LD_FREE;
        tp_text_par(TP_LX, 1, p, num, unit);
        check("wave LFO: the synth module's knobs are the engine's", !strcmp(num, "detune"));
        p[TP_LDEST] = LD_AMP;
        tp_text_par(TP_LX, 1, p, num, unit);
        check("wave LFO: the voice module's knob 1 is pitch", !strcmp(num, "pitch"));
        tp_text_par(TP_LX2, 2, p, num, unit);
        check("wave LFO: shape 2 is ramp", !strcmp(num, "ramp") && !strcmp(tp_info_par(TP_LX2, p)->name, "shape"));
        p[TP_LTYPE] = LT_PUMP;
        check("pump: source / amount / hold / release", !strcmp(tp_info_par(TP_LSPEED, p)->name, "source")
                                                             && !strcmp(tp_info_par(TP_LDEST, p)->name, "hold")
                                                             && !strcmp(tp_info_par(TP_LX, p)->name, "release"));
        tp_text_par(TP_LSPEED, DUCK_METRO, p, num, unit);
        check("pump source 10 is the metronome (no tape before it)", DUCK_METRO == 10 && !strcmp(num, "metro") &&
                                                                     tp_info_par(TP_LSPEED, p)->hi == DUCK_METRO);
        tp_text_par(TP_LSPEED, 8, p, num, unit);
        check("pump source 8 is the conductor", !strcmp(num, "conductor"));
        p[TP_LTYPE] = LT_WOBBLE;
        check("wobble: vibrato / volume / envelope / shape", !strcmp(tp_info_par(TP_LAMT, p)->name, "vibrato")
                                                               && !strcmp(tp_info_par(TP_LDEST, p)->name, "volume")
                                                               && !strcmp(tp_info_par(TP_LX, p)->name, "envelope")
                                                               && !strcmp(tp_info_par(TP_LX2, p)->name, "shape"));
        tp_text_par(TP_LX2, 2, p, num, unit);
        check("wobble shape 2 is exponential", !strcmp(num, "expo"));
        tp_text_par(TP_LX, 32, p, num, unit);
        check("envelope 32 shows in 50 %", !strcmp(num, "in 50") && !strcmp(unit, "%"));
        tp_text_par(TP_LX, 64, p, num, unit);
        check("envelope 64 shows off", !strcmp(num, "off"));
        p[TP_LTYPE] = LT_DRIFT;
        check("drift: amount is unipolar, envelope on x2", tp_info_par(TP_LAMT, p)->kind == PK_PCT
                                                               && !strcmp(tp_info_par(TP_LX2, p)->name, "envelope"));
    }
    /* the engines' plain defaults (INIT), names and displays */
    {
        static const uint8_t DEF[EG_DRUMLINE][4] = {{0, 20, 1, 0},  {40, 25, 0, 0}, {0, 40, 0, 0},  {3, 4, 2, 5},
                                                    {0, 64, 64, 0}, {2, 0, 64, 0},  {5, 0, 127, SM_ONESHOT},
                                                    {SMP_BREAK, 1, 64, 127}};
        static const char *const NAMES[EG_DRUMLINE] = {"knit", "rayon", "jacquard", "bellows", "cane", "voile", "sample",
                                                       "slice"};
        static const char *const KNOBS[EG_DRUMLINE][4] = {
            {"wave", "detune", "voices", "width"}, {"bright", "ratio", "edge", "tine"},    {"table", "scan", "bend", "ripple"},
            {"stops", "pedal", "rotor", "rate"},   {"model", "timbre", "color", "damp"},  {"model", "timbre", "color", "breath"},
            {"sample", "start", "length", "mode"}, {"sample", "slices", "pitch", "gate"}};
        int ok = 1, names = 1;
        for (e = 0; e < EG_DRUMLINE; e++) {
            uint8_t p[NTP];
            sound_default(p, e);
            names &= !strcmp(EG_NAME[e], NAMES[e]);
            for (k = 0; k < 4; k++) {
                if (p[TP_E1 + k] != DEF[e][k]) {
                    printf("    %s knob %d defaults to %d, not %d\n", EG_NAME[e], k + 1, p[TP_E1 + k], DEF[e][k]);
                    ok = 0;
                }
                names &= !strcmp(EG_KNOB[e][k], KNOBS[e][k]);
            }
        }
        check("the engines' defaults are their plain sounds", ok);
        check("the engines and their knobs have their names", names && !strcmp(EG_NAME[EG_DRUMLINE], "kit"));
        tp_text(TP_E2, 40, EG_KNIT, num, unit);
        check("knit's detune 40 shows 31", !strcmp(num, "31"));
        tp_text(TP_E1, 51, EG_RAYON, num, unit);
        check("engine knobs show two digits 00..99", !strcmp(num, "40") && !unit[0]);
        tp_text(TP_E1, 0, EG_RAYON, num, unit);
        check("engine knob 0 shows 00", !strcmp(num, "00"));
        tp_text(TP_E2, 110, EG_RAYON, num, unit);
        check("rayon's ratio shows N:1", !strcmp(num, "3.5:1"));
        check("knit's voices are 1..7", tp_info(TP_E3, EG_KNIT)->lo == 1 && tp_info(TP_E3, EG_KNIT)->hi == 7);
        check("organ knobs are discrete 1..8", tp_info(TP_E1, EG_BELLOWS)->lo == 1 && tp_info(TP_E1, EG_BELLOWS)->hi == 8
                                                   && tp_info(TP_E4, EG_BELLOWS)->hi == 8);
        check("nine wavetables", tp_info(TP_E1, EG_JACQUARD)->hi == 8);
        tp_text(TP_E1, 8, EG_JACQUARD, num, unit);
        check("the last table is chirp", !strcmp(num, "chirp"));
        tp_text(TP_E1, 3, EG_JACQUARD, num, unit);
        check("table 3 is vocal", !strcmp(num, "vocal"));
    }
    /* the filter and play page texts */
    tp_text(TP_CUT, 0, 0, num, unit);
    check("cutoff 0 shows 50 Hz", !strcmp(num, "50") && !strcmp(unit, "Hz"));
    tp_text(TP_CUT, 127, 0, num, unit);
    check("cutoff 127 shows 20.0 kHz", !strcmp(num, "20.0") && !strcmp(unit, "kHz"));
    tp_text(TP_CUT, 59, 0, num, unit);
    check("cutoff 59 shows about 800Hz", !strcmp(unit, "Hz") && atoi(num) > 780 && atoi(num) < 840);
    {
        static const char *const FN[6] = {"lp24", "ladder", "lp12", "hp12", "dynamic", "bp12"};
        int ok = NFT == 6;
        for (k = 0; k < NFT && k < 6; k++) {
            tp_text(TP_FTYPE, k, 0, num, unit);
            ok &= !strcmp(num, FN[k]);
        }
        check("filter types: lp24 ladder lp12 hp12 dynamic bp12", ok);
    }
    tp_text(TP_BEND, 0, 0, num, unit);
    check("bend 0 is off", !strcmp(num, "off"));
    tp_text(TP_BEND, 8, 0, num, unit);
    check("bend 8 is an octave", !strcmp(num, "octave") && tp_bend_semi(8) == 12 && tp_bend_semi(5) == 5);
    tp_text(TP_PORTA, 0, 0, num, unit);
    check("portamento 0 is off", !strcmp(num, "off"));
    tp_text(TP_PORTA, 50, 0, num, unit);
    check("portamento shows 0..127", !strcmp(num, "50") && !unit[0]);

    check("some presets, at most 255 (a byte picks one)", NPRESETS > 0 && NPRESETS <= 255);
    {   /* names: lower case words, at most 11 characters, each its own; stack's first is a bass */
        int ok = 1, j;
        for (k = 0; k < NPRESETS && ok; k++) {
            const char *n = PRESETS[k].name;
            int len = (int)strlen(n);
            if (len < 1 || len > 11 || n[0] == ' ' || n[len - 1] == ' ')
                ok = 0;
            for (j = 0; j < len; j++)
                if (!((n[j] >= 'a' && n[j] <= 'z') || n[j] == ' '))
                    ok = 0;
            for (j = 0; j < k; j++)
                if (!strcmp(PRESETS[j].name, n))
                    ok = 0;
            if (!ok)
                printf("    preset %d \"%s\"\n", k, n);
        }
        check("preset names: lower case, <= 11 characters, unique", ok);
        {
            int sub = 0;                         /* (project.c: a new project's track 2 is knit's sub) */
            for (k = 0; k < NPRESETS; k++)
                sub |= PRESETS[k].par[TP_ENGINE] == EG_KNIT && strstr(PRESETS[k].name, "sub") != 0;
            check("knit has a sub (a new project's track 2)", sub);
        }
    }
    for (e = 0; e < NENGINE; e++) {
        if (!eg_synth(e))
            continue;
        int n = 0;
        for (k = 0; k < NPRESETS; k++)
            n += PRESETS[k].par[TP_ENGINE] == e;
        if (n < 1) {                             /* (the bank by role and genre: as many as each engine earns) */
            printf("    engine %s has no presets\n", EG_NAME[e]);
            check("every engine has presets", 0);
        }
        if (preset_first(e) < 0 || PRESETS[preset_first(e)].par[TP_ENGINE] != e)
            check("preset_first", 0);
        for (k = preset_first(e); k >= 0 && k < preset_first(e) + n; k++)   /* together: PRESETS steps through them */
            if (k >= NPRESETS || PRESETS[k].par[TP_ENGINE] != e) {
                printf("    engine %s's presets are not together\n", EG_NAME[e]);
                check("an engine's presets together", 0);
                break;
            }
    }
    {
        int ok = 1;
        for (k = 0; k < NPRESETS; k++)
            if (PRESETS[k].role >= NRL) {
                printf("    preset %s: role %d\n", PRESETS[k].name, PRESETS[k].role);
                ok = 0;
            }
        check("every preset has a role", ok);
    }
    {
        int ok = 1;
        for (k = 0; k < NPRESETS; k++) {
            uint8_t p[NTP];
            int q;
            memcpy(p, PRESETS[k].par, NTP_SOUND);
            memset(p + NTP_SOUND, 0, NTP - NTP_SOUND);
            for (q = 0; q < NTP_SOUND; q++) {
                const tp_info_t *i = tp_info_par(q, p);
                if ((i->hi > i->lo || i->kind == PK_NAMES) && (p[q] < i->lo || p[q] > i->hi)) {
                    printf("    preset %s: parameter %d = %d outside %d..%d\n", PRESETS[k].name, q, p[q], i->lo, i->hi);
                    ok = 0;
                }
            }
        }
        check("every preset value is inside its knob's range", ok);
    }

    /* every engine, default sound */
    reset();
    for (e = 0; e < NENGINE; e++) {
        if (!eg_synth(e))
            continue;
        char what[64];
        sound_default(par[0], e);
        sprintf(what, "engine %s renders a note (default sound)", EG_NAME[e]);
        check(what, note_test(what, 0, &pk, 1));
        printf("    %-10s peak %6.1f dBFS\n", EG_NAME[e], (double)db(pk));
    }
    for (e = 0; e < NENGINE; e++) {
        if (!eg_synth(e))
            continue;          /* and with every filter type, resonance up, knobs at the ends */
        int ft, ok = 1;
        for (ft = 0; ft < NFT; ft++) {
            sound_default(par[0], e);
            par[0][TP_FTYPE] = (uint8_t)ft;
            par[0][TP_RES] = 127;
            par[0][TP_CUT] = (uint8_t)(ft == FT_HP12 ? 30 : 110);   /* a high-pass at 8 kHz would leave nothing */
            par[0][TP_E1] = par[0][TP_E2] = par[0][TP_E3] = par[0][TP_E4] = (uint8_t)(ft & 1 ? 127 : 0);
            ok &= note_test(EG_NAME[e], 0, &pk, 0);   /* some corners are quiet by design */
        }
        check(ok ? "engine with every filter type, knobs at the ends" : EG_NAME[e], ok);
    }
    {   /* the filter types behave as their names say, on a saw (stack's) at a low cutoff */
        float lv[NFT];
        int ft;
        for (ft = 0; ft < NFT; ft++) {
            reset();
            sound_default(par[0], EG_KNIT);
            par[0][TP_E4] = 0;
            par[0][TP_FTYPE] = (uint8_t)ft;
            par[0][TP_CUT] = 20;
            par[0][TP_RES] = 0;
            synth_note_on(S, 0, 60, 100, 0, 1);
            for (b = 0; b < 30; b++)
                block(N, &finite);
            lv[ft] = block(N, &finite);
        }
        printf("    saw at cutoff 20: lp24 %.1f ladder %.1f lp12 %.1f hp12 %.1f dynamic %.1f bp12 %.1f dBFS\n",
               (double)db(lv[0]), (double)db(lv[1]), (double)db(lv[2]), (double)db(lv[3]), (double)db(lv[4]),
               (double)db(lv[5]));
        check("lp24 is darker than lp12", lv[FT_LP24] < lv[FT_LP12] * 0.8f);
        check("ladder is darker than lp12", lv[FT_LADDER] < lv[FT_LP12] * 0.9f);
        check("dynamic on a synth track is a bypass (louder than any low-pass)", lv[FT_DYNAMIC] > lv[FT_LP12] * 1.5f);
        check("hp12 at a low cutoff keeps most of the saw", lv[FT_HP12] > lv[FT_LP12] * 1.5f);
        check("bp12 passes less than the bypass", lv[FT_BP12] > 0.0f && lv[FT_BP12] < lv[FT_DYNAMIC] * 0.7f);
        check("every filter type stays finite", finite);
        {   /* bp12 is a band: at a high cutoff it drops the saw's low partials (much less than lp12 there) */
            float lp, bp;
            reset();
            sound_default(par[0], EG_KNIT);
            par[0][TP_E4] = 0;
            par[0][TP_CUT] = 90;
            par[0][TP_RES] = 0;
            par[0][TP_FTYPE] = FT_LP12;
            synth_note_on(S, 0, 48, 100, 0, 1);
            for (b = 0; b < 30; b++)
                block(N, &finite);
            block(N, &finite);
            lp = last_rms;
            par[0][TP_FTYPE] = FT_BP12;
            for (b = 0; b < 30; b++)
                block(N, &finite);
            block(N, &finite);
            bp = last_rms;
            printf("    saw at cutoff 90 (3.5 kHz): lp12 rms %.1f, bp12 rms %.1f dBFS\n", (double)db(lp), (double)db(bp));
            check("bp12 at a high cutoff passes much less than lp12", bp < lp * 0.5f && bp > 0.0f && finite);
        }
        reset();                                 /* the ladder with its feedback at full self-limits */
        sound_default(par[0], EG_KNIT);
        par[0][TP_FTYPE] = FT_LADDER;
        par[0][TP_RES] = 127;
        par[0][TP_CUT] = 80;
        synth_note_on(S, 0, 48, 127, 0, 1);
        pk = 0.0f;
        for (b = 0; b < 100; b++) {
            float p = block(N, &finite);
            if (p > pk)
                pk = p;
        }
        check("the ladder at full resonance stays finite and inside full scale", finite && pk <= 1.0f);
    }

    /* every preset */
    {
        int ok = 1, klo = 0, khi = 0;
        float lo = 1.0f, hi = 0.0f;
        for (k = 0; k < NPRESETS; k++) {
            load_preset(0, k);
            if (!note_test(PRESETS[k].name, 0, &pk, 1))
                ok = 0;
            if (pk < lo)
                lo = pk, klo = k;
            if (pk > hi)
                hi = pk, khi = k;
        }
        check("every preset renders a 1 s note: finite, -30..0 dBFS, release ends", ok);
        check("the preset peaks sit within 18 dB of each other", db(hi) - db(lo) < 18.0f);
        printf("    preset peaks %.1f (%s) .. %.1f (%s) dBFS\n", (double)db(lo), PRESETS[klo].name, (double)db(hi),
               PRESETS[khi].name);
    }

    /* the organ's eight models and the nine tables each sound, and differently */
    {
        int ok = 1;
        reset();
        sound_default(par[0], EG_BELLOWS);
        for (k = 1; k <= 8; k++) {
            par[0][TP_E1] = (uint8_t)k;
            fingerprint(0, k & 1 ? fp1 : fp2);
            if (k > 1 && !differ(fp1, fp2))
                ok = 0;
        }
        check("organ: eight models, each its own sound", ok);
        ok = 1;
        sound_default(par[0], EG_BELLOWS);
        par[0][TP_E3] = 8;
        par[0][TP_E4] = 8;
        synth_note_on(S, 0, 60, 100, 0, 1);
        check("organ: rotor 8 at rate 8 moves the level", swing(20, 60, &finite) > 1.3f);
        synth_all_off(S, -1);
        sound_default(par[0], EG_JACQUARD);
        for (k = 0; k < 9; k++) {
            par[0][TP_E1] = (uint8_t)k;
            fingerprint(0, k & 1 ? fp1 : fp2);
            if (k > 0 && !differ(fp1, fp2))
                ok = 0;
        }
        check("wavetable: nine tables, each its own sound", ok);
    }

    /* a six-note chord of the loudest kind stays inside full scale */
    reset();
    sound_default(par[0], EG_BELLOWS);
    par[0][TP_E1] = 8;
    par[0][TP_VOLUME] = 127;
    synth_all_off(S, -1);
    for (k = 0; k < 6; k++)
        synth_note_on(S, 0, 48 + k * 4, 127, 0, (uint32_t)k);
    pk = 0.0f;
    for (b = 0; b < 100; b++) {
        float p = block(N, &finite);
        if (p > pk)
            pk = p;
    }
    printf("    six church organ notes at full velocity and volume: %.1f dBFS\n", (double)db(pk));
    check("a six-note chord at full volume stays finite", finite);

    /* mono / legato */
    reset();
    sound_default(par[0], EG_KNIT);
    par[0][TP_VMODE] = VM_MONO;
    par[0][TP_A] = 100;                          /* a slow attack: a retrigger shows as a dip */
    synth_note_on(S, 0, 60, 100, 0, 1);
    for (b = 0; b < 40; b++)
        block(N, &finite);
    check("mono: one voice", synth_voices_used(S) == 1);
    synth_note_on(S, 0, 64, 100, 0, 2);
    check("mono: a second note takes no new voice", synth_voices_used(S) == 1);
    {
        float a = block(N, &finite), c;
        for (b = 0; b < 40; b++)
            c = block(N, &finite);
        check("mono: the new note retriggers (level climbs again)", c > a);
        synth_note_off(S, 0, 2);
        for (b = 0; b < 4; b++)
            block(N, &finite);
        check("mono: releasing the top note returns to the held one", synth_sounding(S, 0, NVOICE));
        synth_note_off(S, 0, 1);
        for (b = 0; b < 400 && synth_sounding(S, 0, NVOICE); b++)
            block(N, &finite);
        check("mono: releasing the last note ends the voice", !synth_sounding(S, 0, NVOICE));
    }
    reset();
    sound_default(par[0], EG_KNIT);
    par[0][TP_VMODE] = VM_LEGATO;
    par[0][TP_A] = 100;
    synth_note_on(S, 0, 60, 100, 0, 1);
    for (b = 0; b < 100; b++)
        block(N, &finite);
    {
        float before = block(N, &finite), after, mn = 1.0f;
        synth_note_on(S, 0, 67, 100, 0, 2);
        for (b = 0; b < 10; b++) {
            after = block(N, &finite);
            if (after < mn)
                mn = after;
        }
        check("legato: a note on a held note does not retrigger", mn > before * 0.7f && synth_voices_used(S) == 1);
    }

    /* the pool: 16 voices, stealing (jacquard: no cap of its own) */
    reset();
    for (t = 0; t < NTRK; t++)
        sound_default(par[t], EG_JACQUARD);
    for (k = 0; k < 16; k++)
        synth_note_on(S, k % NTRK, 40 + k, 100, 0, (uint32_t)k);
    check("16 notes: 16 voices", synth_voices_used(S) == 16);
    synth_note_on(S, 0, 90, 100, 0, 100);
    check("a 17th note steals: still 16 voices", synth_voices_used(S) == 16);
    for (b = 0; b < 10; b++)
        block(N, &finite);
    {
        synth_note_off(S, 0, 100);                 /* the stolen voice plays the new note after its fade */
        for (b = 0; b < 2; b++)
            block(N, &finite);
        check("the stolen voice took the new note and stays finite", finite);
    }
    reset();
    for (k = 0; k < 8; k++)
        synth_note_on(S, 0, 40 + k, 100, 0, (uint32_t)k);
    check("one track takes at most NVOICE_TRK voices (the rest steal its own)", synth_voices_used(S) <= NVOICE_TRK + 1);
    synth_limit(S, 4);
    synth_all_off(S, -1);
    for (b = 0; b < 400 && synth_sounding(S, 0, NVOICE); b++)
        block(N, &finite);
    for (k = 0; k < 8; k++)
        synth_note_on(S, k, 40 + k, 100, 0, (uint32_t)k);
    check("synth_limit(4): four voices", synth_voices_used(S) == 4);
    synth_limit(S, NVOICE);

    /* locks override the knob and release */
    reset();
    sound_default(par[0], EG_KNIT);
    par[0][TP_CUT] = 127;
    par[0][TP_A] = 0;
    par[0][TP_FTYPE] = FT_LP24;
    {
        float open, locked, back;
        synth_note_on(S, 0, 48, 100, 0, 1);
        for (b = 0; b < 20; b++)
            block(N, &finite);
        open = block(N, &finite);
        synth_lock(S, 0, TP_CUT, 10);
        for (b = 0; b < 20; b++)
            block(N, &finite);
        locked = block(N, &finite);
        synth_lock(S, 0, TP_CUT, LOCK_NONE);
        for (b = 0; b < 20; b++)
            block(N, &finite);
        back = block(N, &finite);
        check("a cutoff lock closes the filter (quieter)", locked < open * 0.5f);
        check("LOCK_NONE returns to the knob", back > locked * 1.5f);
    }

    /* the wave LFO moves a parameter */
    reset();
    sound_default(par[0], EG_KNIT);
    par[0][TP_CUT] = 40;
    par[0][TP_LTYPE] = LT_WAVE;
    par[0][TP_LSPEED] = 60;                      /* a few Hz */
    par[0][TP_LAMT] = 127;
    par[0][TP_LDEST] = LD_FILTER;
    par[0][TP_LX] = 0;
    par[0][TP_LX2] = 1;
    synth_note_on(S, 0, 48, 100, 0, 1);
    check("a wave LFO on the filter's cutoff moves the level", swing(40, 172, &finite) > 1.3f);
    {   /* on an engine knob with a narrow range: the organ's type sweeps its 1..8 without leaving it */
        int ok = 1;
        reset();
        sound_default(par[0], EG_BELLOWS);
        par[0][TP_LTYPE] = LT_WAVE;
        par[0][TP_LSPEED] = 60;
        par[0][TP_LAMT] = 127;
        par[0][TP_LDEST] = LD_SYNTH;
        par[0][TP_LX] = 0;
        synth_note_on(S, 0, 48, 100, 0, 1);
        for (b = 0; b < 200; b++)
            block(N, &finite);
        ok = finite;
        check("a wave LFO on a discrete knob stays finite", ok);
    }
    {   /* on the pitch: the amp module's knob 1 */
        reset();
        sound_default(par[0], EG_KNIT);
        par[0][TP_LTYPE] = LT_WAVE;
        par[0][TP_LSPEED] = 40;
        par[0][TP_LAMT] = 127;
        par[0][TP_LDEST] = LD_AMP;
        par[0][TP_LX] = 1;
        par[0][TP_LX2] = 1;
        synth_note_on(S, 0, 48, 100, 0, 1);
        for (b = 0; b < 10; b++)
            block(N, &finite);
        fingerprint(0, fp1);
        par[0][TP_LAMT] = 64;
        fingerprint(0, fp2);
        check("a wave LFO on the pitch changes the sound", differ(fp1, fp2) && finite);
    }
    {   /* regular vs free: a square on the volume, +100 %, from a half-level knob */
        float reg, fre;
        int pass;
        for (pass = 0; pass < 2; pass++) {
            float a, c;
            reset();
            sound_default(par[0], EG_KNIT);
            par[0][TP_VOLUME] = 64;
            par[0][TP_A] = 0;
            par[0][TP_LTYPE] = LT_WAVE;
            par[0][TP_LSPEED] = 12;              /* slow: 0.13 Hz, a half cycle is about 4 s */
            par[0][TP_LAMT] = 127;
            par[0][TP_LDEST] = (uint8_t)(LD_AMP + (pass ? LD_FREE : 0));
            par[0][TP_LX] = 0;
            par[0][TP_LX2] = 0;                  /* square: +1 for the first half cycle */
            synth_note_on(S, 0, 48, 100, 0, 1);
            for (b = 0; b < 20; b++)
                block(N, &finite);
            a = block(N, &finite);
            synth_note_off(S, 0, 1);
            for (b = 0; b < 172 * 5; b++)        /* 5 s on: the LFO is in its low half now */
                block(N, &finite);
            synth_note_on(S, 0, 48, 100, 0, 2);
            for (b = 0; b < 20; b++)
                block(N, &finite);
            c = block(N, &finite);
            if (pass == 0)
                reg = c / a;
            else
                fre = c / a;
        }
        printf("    second note's level over the first's: regular %.2f, free %.2f\n", (double)reg, (double)fre);
        check("regular: the LFO restarts on a note (the same level)", reg > 0.9f && reg < 1.1f);
        check("free: the LFO runs on (a different level)", fre < 0.6f);
    }
    /* wobble: volume, vibrato, envelope, shapes */
    reset();
    sound_default(par[0], EG_BELLOWS);
    par[0][TP_LTYPE] = LT_WOBBLE;
    par[0][TP_LSPEED] = 70;
    par[0][TP_LAMT] = 64;
    par[0][TP_LDEST] = 127;
    par[0][TP_LX] = 64;
    synth_note_on(S, 0, 48, 100, 0, 1);
    check("a wobble's volume amount moves the level", swing(40, 172, &finite) > 1.5f);
    {
        int sh, ok = 1;
        for (sh = 0; sh < 5; sh++) {
            reset();
            sound_default(par[0], EG_BELLOWS);
            par[0][TP_LTYPE] = LT_WOBBLE;
            par[0][TP_LSPEED] = 70;
            par[0][TP_LAMT] = 64;
            par[0][TP_LDEST] = 127;
            par[0][TP_LX] = 64;
            par[0][TP_LX2] = (uint8_t)sh;
            synth_note_on(S, 0, 48, 100, 0, 1);
            if (swing(40, 172, &finite) < 1.3f || !finite)
                ok = 0;
        }
        check("every wobble shape moves the level", ok);
    }
    {
        reset();
        sound_default(par[0], EG_BELLOWS);
        par[0][TP_LTYPE] = LT_WOBBLE;
        par[0][TP_LSPEED] = 90;
        par[0][TP_LAMT] = 127;                   /* vibrato only */
        par[0][TP_LDEST] = 64;
        synth_note_on(S, 0, 48, 100, 0, 1);
        for (b = 0; b < 10; b++)
            block(N, &finite);
        fingerprint(0, fp1);
        par[0][TP_LAMT] = 64;
        fingerprint(0, fp2);
        check("a wobble's vibrato amount changes the sound", differ(fp1, fp2));
    }
    {
        float early, late;
        reset();
        sound_default(par[0], EG_BELLOWS);
        par[0][TP_LTYPE] = LT_WOBBLE;
        par[0][TP_LSPEED] = 90;
        par[0][TP_LAMT] = 64;
        par[0][TP_LDEST] = 127;
        par[0][TP_LX] = 0;                       /* fade in over 3 s */
        synth_note_on(S, 0, 72, 100, 0, 1);
        early = swing(8, 20, &finite);           /* 0.05 .. 0.16 s */
        late = swing(172 * 3, 40, &finite);      /* after 3 s */
        printf("    wobble fade in: swing %.2f early, %.2f late\n", (double)early, (double)late);
        check("wobble envelope: fade in (little early, full late)", early < 1.15f && late > 1.5f);
        reset();
        sound_default(par[0], EG_BELLOWS);
        par[0][TP_LTYPE] = LT_WOBBLE;
        par[0][TP_LSPEED] = 90;
        par[0][TP_LAMT] = 64;
        par[0][TP_LDEST] = 127;
        par[0][TP_LX] = 80;                      /* fade out over about 2.2 s */
        synth_note_on(S, 0, 72, 100, 0, 1);
        early = swing(8, 40, &finite);
        late = swing(172 * 3, 40, &finite);
        printf("    wobble fade out: swing %.2f early, %.2f late\n", (double)early, (double)late);
        check("wobble envelope: fade out (full early, none late)", early > 1.5f && late < 1.15f);
    }
    /* drift: unipolar */
    {
        float quiet, loud;
        reset();
        sound_default(par[0], EG_BELLOWS);
        par[0][TP_VOLUME] = 0;
        synth_note_on(S, 0, 48, 100, 0, 1);
        for (b = 0; b < 20; b++)
            block(N, &finite);
        quiet = block(N, &finite);
        par[0][TP_LTYPE] = LT_DRIFT;
        par[0][TP_LSPEED] = 90;
        par[0][TP_LAMT] = 127;
        par[0][TP_LDEST] = LD_AMP + LD_FREE;
        par[0][TP_LX] = 0;
        par[0][TP_LX2] = 64;
        loud = 0.0f;
        for (b = 0; b < 100; b++) {
            float p = block(N, &finite);
            if (p > loud)
                loud = p;
        }
        check("drift: a 100 % amount on a volume of 0 only adds (audible)", quiet < 1e-4f && loud > 0.01f && finite);
        par[0][TP_LAMT] = 0;
        for (b = 0; b < 40; b++)
            block(N, &finite);
        check("drift: amount 0 adds nothing", block(N, &finite) < 1e-4f);
    }
    /* pump: source, hold, release */
    {
        float a, c, held, rel;
        reset();
        sound_default(par[0], EG_BELLOWS);
        par[0][TP_LTYPE] = LT_PUMP;
        par[0][TP_LSPEED] = 3;                   /* source: track 4 */
        par[0][TP_LAMT] = 127;
        par[0][TP_LDEST] = 0;                    /* hold 1 ms */
        par[0][TP_LX] = 76;                      /* release about 250 ms */
        synth_note_on(S, 0, 48, 100, 0, 1);
        for (b = 0; b < 40; b++)
            block(N, &finite);
        a = block(N, &finite);
        synth_duck_trigger(S, 5);
        block(N, &finite);
        c = block(N, &finite);
        check("a pump trigger from another source does nothing", c > a * 0.9f);
        synth_duck_trigger(S, 3);
        block(N, &finite);                       /* the gain ramps down inside this block */
        c = block(N, &finite);
        check("a pump trigger from its source drops the level", c < a * 0.3f);
        for (b = 0; b < 172; b++)
            c = block(N, &finite);
        check("the pump recovers after its release", c > a * 0.8f);
        par[0][TP_LDEST] = 115;                  /* hold about 3 s */
        synth_duck_trigger(S, 3);
        for (b = 0; b < 172; b++)
            held = block(N, &finite);
        check("a long hold keeps the pump down a second later", held < a * 0.3f);
        par[0][TP_LDEST] = 0;
        par[0][TP_LX] = 0;                       /* release 1 ms */
        for (b = 0; b < 172 * 4; b++)
            block(N, &finite);
        synth_duck_trigger(S, 3);
        block(N, &finite);
        for (b = 0; b < 8; b++)
            rel = block(N, &finite);
        check("a short release lets go within 50 ms", rel > a * 0.8f);
        par[0][TP_LSPEED] = DUCK_METRO;
        par[0][TP_LX] = 76;
        synth_duck_trigger(S, DUCK_METRO);
        block(N, &finite);
        c = block(N, &finite);
        check("the metronome (source 10) pumps too", c < a * 0.3f);
    }

    /* perf: mute, pitch */
    reset();
    sound_default(par[0], EG_KNIT);
    synth_note_on(S, 0, 48, 100, 0, 1);
    for (b = 0; b < 20; b++)
        block(N, &finite);
    synth_perf(S, 0)->mute = 1;
    for (b = 0; b < 20; b++)
        block(N, &finite);
    check("perf mute silences the track (voices keep running)", block(N, &finite) < 1e-4f && synth_sounding(S, 0, NVOICE));
    synth_perf(S, 0)->mute = 0;

    /* stereo engines differ between the sides, mono ones do not */
    reset();
    sound_default(par[0], EG_KNIT);
    par[0][TP_E3] = 3;                               /* (a unison: a voice is stereo when it has one and a width) */
    par[0][TP_E4] = 100;
    par[0][TP_PAN] = 64;
    synth_note_on(S, 0, 48, 100, 0, 1);
    for (b = 0; b < 20; b++)
        block(N, &finite);
    {
        int i, d = 0;
        block(N, &finite);
        for (i = 0; i < N; i++)
            d |= L[i] != R[i];
        check("knit with width up: the sides differ", d);
        par[0][TP_E4] = 0;
        for (b = 0; b < 20; b++)
            block(N, &finite);
        block(N, &finite);
        d = 0;
        for (i = 0; i < N; i++)
            d |= L[i] != R[i];
        check("knit with width at 0, centred: the sides are the same", !d);
    }

    /* a drumline track takes no synth voice */
    reset();
    sound_default(par[0], EG_DRUMLINE);
    synth_note_on(S, 0, 60, 100, 0, 1);
    check("a drumline track takes no synth voice", synth_voices_used(S) == 0);

    /* splitting by voices and by frames changes nothing */
    {
        static float L2[N], R2[N], S12[N], S22[N];
        static float Lr[N], Rr[N];
        static uint8_t mem2[SYNTH_BYTES];
        int same = 1, i;
        reset();
        for (t = 0; t < NTRK; t++) {
            /* (cane keeps its models' state in slots outside the synth's, which this copies: knit on its track) */
            load_preset(t, preset_first(t == EG_CANE ? EG_KNIT : t) + 3);
            par[t][TP_LTYPE] = (uint8_t)(t & 1 ? LT_WOBBLE : LT_WAVE);
            par[t][TP_LDEST] = (uint8_t)(t & 1 ? 100 : LD_FILTER);
            par[t][TP_LAMT] = 100;
            par[t][TP_LSPEED] = 50;
            par[t][TP_LX] = (uint8_t)(t & 1 ? 20 : 0);
            par[t][TP_FTYPE] = (uint8_t)(t % NFT);
        }
        for (k = 0; k < 16; k++)
            synth_note_on(S, k % NTRK, 40 + k * 2, 90 + k, 0, (uint32_t)k);
        for (b = 0; b < 10; b++)
            block(N, &finite);
        synth_note_on(S, 0, 100, 100, 0, 77);   /* a steal in flight */
        memcpy(mem2, mem, SYNTH_BYTES);          /* the same state twice (par is shared) */
        /* A: one range, one block */
        memset(L, 0, sizeof L);
        memset(R, 0, sizeof R);
        memset(S1, 0, sizeof S1);
        memset(S2, 0, sizeof S2);
            synth_prepare(S, N);
        synth_render(S, 0, NVOICE, L, R, S1, S2, N);
        memcpy(Lr, L, sizeof L);
        memcpy(Rr, R, sizeof R);
        /* B: two voice ranges */
        memcpy(mem, mem2, SYNTH_BYTES);
        memset(L2, 0, sizeof L2);
        memset(R2, 0, sizeof R2);
        memset(S12, 0, sizeof S12);
        memset(S22, 0, sizeof S22);
        synth_prepare(S, N);
        synth_render(S, 0, 8, L2, R2, S12, S22, N);
        synth_render(S, 8, NVOICE, L2, R2, S12, S22, N);
        for (i = 0; i < N; i++)
            if (L2[i] != Lr[i] || R2[i] != Rr[i])
                same = 0;
        check("voices 0..8 + 8..16 sum bit-identically to 0..16", same);
        /* the other core's half into its own buffers, added after: the same within float rounding */
        memcpy(mem, mem2, SYNTH_BYTES);
        memset(L2, 0, sizeof L2);
        memset(R2, 0, sizeof R2);
        memset(L, 0, sizeof L);
        memset(R, 0, sizeof R);
        synth_prepare(S, N);
        synth_render(S, 8, NVOICE, L2, R2, S12, S22, N);
        synth_render(S, 0, 8, L, R, S12, S22, N);
        same = 1;
        for (i = 0; i < N; i++)
            if (fabs_f(L2[i] + L[i] - Lr[i]) > 1e-6f || fabs_f(R2[i] + R[i] - Rr[i]) > 1e-6f)
                same = 0;
        check("the halves in separate buffers, summed, match within 1e-6", same);
        /* C: 100 + 156 frames */
        memcpy(mem, mem2, SYNTH_BYTES);
        memset(L2, 0, sizeof L2);
        memset(R2, 0, sizeof R2);
        memset(S12, 0, sizeof S12);
        memset(S22, 0, sizeof S22);
        synth_prepare(S, 100);
        synth_render(S, 0, NVOICE, L2, R2, S12, S22, 100);
        synth_prepare(S, 156);
        synth_render(S, 0, NVOICE, L2 + 100, R2 + 100, S12 + 100, S22 + 100, 156);
        same = 1;
        for (i = 0; i < N; i++)
            if (L2[i] != Lr[i] || R2[i] != Rr[i])
                same = 0;
        check("100 + 156 frames render bit-identically to 256", same);
        /* and the two together, with one prepare for both renders */
        memcpy(mem, mem2, SYNTH_BYTES);
        memset(L2, 0, sizeof L2);
        memset(R2, 0, sizeof R2);
        memset(S12, 0, sizeof S12);
        memset(S22, 0, sizeof S22);
        synth_prepare(S, N);
        synth_render(S, 0, 8, L2, R2, S12, S22, 100);
        synth_render(S, 8, NVOICE, L2, R2, S12, S22, 100);
        synth_render(S, 0, 8, L2 + 100, R2 + 100, S12 + 100, S22 + 100, 156);
        synth_render(S, 8, NVOICE, L2 + 100, R2 + 100, S12 + 100, S22 + 100, 156);
        same = 1;
        for (i = 0; i < N; i++)
            if (L2[i] != Lr[i] || R2[i] != Rr[i])
                same = 0;
        check("ranges and frame splits combined, one prepare", same);
    }

    char_tests();
    sample_tests();
    slice_tests();
    bench();
    bench_char();
    printf("%s\n", bad ? "SYNTH TESTS FAILED" : "all synth tests passed");
    return bad != 0;
}
