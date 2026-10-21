/* SPDX-License-Identifier: GPL-3.0-only */
/* Listening renders for the kit's per-key envelopes and filters, the sample model and the sample engine
 * (tools/render_drums.sh): each a short phrase straight from the units (dsp/drumline.c, dsp/synth.c, dry: no
 * FX, no master), 16-bit stereo WAVs into OUTDIR, and OUTDIR/index.txt (name, what it is). Silent: nothing
 * is played.
 *   render_drums OUTDIR */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "drumline.h"
#include "synth.h"
#include "sample.h"

#define SR 44100
#define BLK 64
static uint8_t par[NTP];
static _Alignas(16) uint8_t dmem[DRUMLINE_BYTES];
static drumline_t *dl = (drumline_t *)dmem;
static uint8_t spar[NTRK + 1][NTP];         /* (+ the conductor's row: the shared LFO, off) */
static _Alignas(16) uint8_t smem[SYNTH_BYTES];
static synth_t *sy = (synth_t *)smem;
static FILE *idx;
static float w_gain = 0.7f;                       /* the kit a little under full scale: dry, no master */
static const char *outdir;

typedef struct { FILE *f; long n; float peak; } wav_t;
static void wav_open(wav_t *w, const char *name)
{
    static const uint8_t hdr[44] = {'R','I','F','F',0,0,0,0,'W','A','V','E','f','m','t',' ',16,0,0,0,1,0,2,0,
        0x44,0xAC,0,0,0x10,0xB1,2,0,4,0,16,0,'d','a','t','a',0,0,0,0};
    char path[512];
    snprintf(path, sizeof path, "%s/%s.wav", outdir, name);
    w->f = fopen(path, "wb");
    w->n = 0;
    w->peak = 0.0f;
    if (w->f)
        fwrite(hdr, 1, 44, w->f);
}
static void wav_put(wav_t *w, const float *l, const float *r, int n)
{
    int i;
    for (i = 0; w->f && i < n; i++) {
        int16_t s[2];
        float a = l[i] * w_gain, b = r[i] * w_gain;
        w->peak = fmaxf(w->peak, fmaxf(fabsf(a), fabsf(b)));
        s[0] = (int16_t)fmaxf(-32767.0f, fminf(32767.0f, a * 32767.0f));
        s[1] = (int16_t)fmaxf(-32767.0f, fminf(32767.0f, b * 32767.0f));
        fwrite(s, 2, 2, w->f);
    }
    w->n += n;
}
static void wav_close(wav_t *w, const char *name, const char *what)
{
    uint32_t len;
    if (!w->f)
        return;
    len = (uint32_t)ftell(w->f);
    fseek(w->f, 4, SEEK_SET);
    { uint32_t v = len - 8; fwrite(&v, 4, 1, w->f); }
    fseek(w->f, 40, SEEK_SET);
    { uint32_t v = len - 44; fwrite(&v, 4, 1, w->f); }
    fclose(w->f);
    fprintf(idx, "%s\t%.1f\t%s\n", name, 20.0 * log10((double)w->peak + 1e-9), what);
    printf("%-28s peak %5.1f dBFS  %s\n", name, 20.0 * log10((double)w->peak + 1e-9), what);
}

static void kit_default(void)
{
    int s, k;
    memset(par, 0, sizeof par);
    for (s = 0; s < DL_NSOUND; s++)
        for (k = 0; k < DP_N; k++)
            par[TP_DRUM(s, k)] = DL_DEFAULT_KIT[s][k];
    par[TP_R] = 64;
}
static void key_model(int s, int model)
{
    int k;
    par[TP_DRUM(s, DP_MODEL)] = (uint8_t)model;
    for (k = 0; k < 4; k++)
        par[TP_DRUM(s, DP_K1 + k)] = DM_DEFAULT[model][k];
}
#define K(s, dp) par[TP_DRUM((s), (dp))]

/* a pattern: per 16th step, the keys hit (bit k = key k), at bpm, nbars bars; then a tail */
static void beat(const char *name, const char *what, const uint16_t *steps, int nsteps, int bars, float bpm)
{
    static float l[BLK], r[BLK], a[BLK], b[BLK], c[BLK];
    wav_t w;
    long t = 0, step_len = (long)(SR * 60.0f / bpm / 4.0f), total = step_len * nsteps * bars + SR;
    int st = -1;
    drumline_init(dl, par);
    wav_open(&w, name);
    for (t = 0; t < total; t += BLK) {
        int s = (int)(t / step_len), k;
        if (s != st && s < nsteps * bars) {
            st = s;
            for (k = 0; k < DL_NSOUND; k++)
                if (steps[s % nsteps] >> k & 1u)
                    drumline_hit(dl, k, (s % 4) ? 100 : 127, 0, 0, 0);
        }
        memset(l, 0, sizeof l);
        memset(r, 0, sizeof r);
        drumline_render(dl, l, r, a, b, c, BLK);
        wav_put(&w, l, r, BLK);
    }
    wav_close(&w, name, what);
}
/* one key hit n times, every gap seconds, each time with a change applied by the caller's callback */
static void hits(const char *name, const char *what, int key, int n, float gap, void (*vary)(int i))
{
    static float l[BLK], r[BLK], a[BLK], b[BLK], c[BLK];
    wav_t w;
    long t, total = (long)(SR * gap * (float)n) + SR / 2, every = (long)(SR * gap);
    drumline_init(dl, par);
    wav_open(&w, name);
    for (t = 0; t < total; t += BLK) {
        if (t % every < BLK && t / every < n) {
            if (vary)
                vary((int)(t / every));
            drumline_hit(dl, key, 127, 0, 0, 0);
        }
        memset(l, 0, sizeof l);
        memset(r, 0, sizeof r);
        drumline_render(dl, l, r, a, b, c, BLK);
        wav_put(&w, l, r, BLK);
    }
    wav_close(&w, name, what);
}

static void v_kick_decay(int i) { static const uint8_t D[4] = {127, 90, 70, 50}; K(0, DP_DECAY) = D[i & 3]; }
static void v_snare_cut(int i) { K(1, DP_CUT) = (uint8_t)(127 - i * 18); }
static void v_hat_res(int i) { K(3, DP_RES) = (uint8_t)(i * 30); }
static void v_tom_bp(int i) { K(5, DP_CUT) = (uint8_t)(20 + i * 6); }
static void v_drive(int i) { K(1, DP_DRIVE) = (uint8_t)(i * 42); }
static void v_attack(int i) { K(4, DP_ATTACK) = (uint8_t)(i * 28); }
static void v_sample_pitch(int i) { K(10, DP_K2) = (uint8_t)(52 + i * 4); }

/* the sample engine: track 0 plays notes (each held for len s, gap s apart) */
static void synth_notes(const char *name, const char *what, const int *notes, int n, float len, float gap)
{
    static float l[256], r[256], s1[256], s2[256], st[256];
    wav_t w;
    long t, total = (long)(SR * (gap * (float)n + 2.0f));
    int on = -1;
    synth_init(sy, spar);
    synth_tempo(sy, 110.0f);
    wav_open(&w, name);
    for (t = 0; t < total; t += 256) {
        float sec = (float)t / SR;
        int k = (int)(sec / gap);
        if (k < n && k != on) {
            synth_note_on(sy, 0, notes[k], 100, 0, (uint32_t)(k + 1));
            on = k;
        }
        if (on >= 0 && sec - (float)on * gap > len)
            synth_note_off(sy, 0, (uint32_t)(on + 1));
        memset(l, 0, sizeof l);
        memset(r, 0, sizeof r);
        memset(s1, 0, sizeof s1);
        memset(s2, 0, sizeof s2);
        memset(st, 0, sizeof st);
        synth_prepare(sy, 256);
        synth_render(sy, 0, NVOICE, l, r, s1, s2, st, 256);
        wav_put(&w, l, r, 256);
    }
    synth_all_off(sy, -1);
    wav_close(&w, name, what);
}

int main(int argc, char **argv)
{
    char path[512];
    /* 16th steps: kick 1, snare 2, hat 4, open 8, clap 16, ltom 32, htom 64, conga 128, shaker 256, string 512, fm 1024 */
    static const uint16_t B1[16] = {1 | 4, 4, 4, 4, 2 | 4, 4, 4, 1 | 8, 4, 1 | 4, 4, 4, 2 | 4, 4, 4 | 32, 64 | 4};
    static const uint16_t B2[16] = {1 | 1024, 4, 4, 1024, 2 | 4, 4, 1 | 4, 8, 4, 1 | 4, 1024, 4, 2 | 16, 4, 4, 128 | 4};
    if (argc < 2) {
        fprintf(stderr, "render_drums OUTDIR\n");
        return 2;
    }
    outdir = argv[1];
    snprintf(path, sizeof path, "%s/index.txt", outdir);
    idx = fopen(path, "w");
    if (!idx)
        return 1;

    kit_default();
    beat("01-kit-default", "the default kit, every key neutral (as before: the reference)", B1, 16, 2, 110.0f);
    kit_default();
    K(0, DP_K2) = 127;                                         /* a long kick, then its decay shortening it */
    hits("02-kick-decay", "a long kick (length 127): the key's decay off, 90, 70, 50", 0, 4, 0.8f, v_kick_decay);
    kit_default();
    K(0, DP_K2) = 127;
    K(0, DP_HOLD) = 70;
    K(0, DP_DECAY) = 30;
    hits("03-kick-gated", "the same long kick held 140 ms then cut (hold 70, decay 30): a gated boom", 0, 4, 0.5f, 0);
    kit_default();
    K(1, DP_FTYPE) = DF_LP;
    K(1, DP_RES) = 70;
    hits("04-snare-lowpass", "the snare through its low-pass, cutoff 127 down to 19 (resonance 70)", 1, 7, 0.45f, v_snare_cut);
    kit_default();
    K(1, DP_FTYPE) = DF_LP;
    K(1, DP_CUT) = 35;
    K(1, DP_RES) = 90;
    K(1, DP_FENV) = 120;
    K(1, DP_DECAY) = 85;
    hits("05-snare-envelope-sweep", "the snare: a low cutoff the envelope opens at the hit, its decay (85) closing it", 1, 4, 0.5f, 0);
    kit_default();
    K(3, DP_FTYPE) = DF_BP;
    K(3, DP_CUT) = 95;
    hits("06-openhat-bandpass", "the open hat through a band-pass at 3.3 kHz, resonance 0 .. 120", 3, 5, 0.6f, v_hat_res);
    kit_default();
    K(2, DP_FTYPE) = DF_HP;
    K(2, DP_CUT) = 110;
    beat("07-hats-highpass", "the default beat, the closed hat thinned by a high-pass at 9 kHz", B1, 16, 2, 110.0f);
    kit_default();
    K(5, DP_FTYPE) = DF_BP;
    K(5, DP_RES) = 115;
    K(5, DP_K2) = 100;
    hits("08-tom-ringing", "the low tom through a ringing band-pass (resonance 115), its centre rising from 130 Hz", 5, 6, 0.45f, v_tom_bp);
    kit_default();
    hits("09-snare-drive", "the snare, drive 0, 42, 84, 126", 1, 4, 0.5f, v_drive);
    kit_default();
    K(4, DP_K2) = 110;
    hits("10-clap-attack", "the clap, a long one, its attack 0 .. 112 (a swell into it)", 4, 5, 0.7f, v_attack);
    kit_default();
    K(1, DP_LAG) = 50;                                         /* a lazy snare: 24 ms behind */
    K(1, DP_DECAY) = 75;
    K(2, DP_FTYPE) = DF_HP;
    K(2, DP_CUT) = 100;
    K(0, DP_DRIVE) = 60;
    K(0, DP_FTYPE) = DF_LP;
    K(0, DP_CUT) = 70;
    K(0, DP_RES) = 60;
    K(0, DP_FENV) = 100;
    K(0, DP_DECAY) = 80;
    K(10, DP_FTYPE) = DF_BP;
    K(10, DP_CUT) = 85;
    K(10, DP_RES) = 90;
    K(10, DP_DECAY) = 70;
    beat("11-kit-shaped", "a shaped kit: kick driven through a swept low-pass, snare late (lag 50) and shorter, "
         "hats high-passed, fm through a band-pass", B2, 16, 2, 110.0f);
    /* the sample model */
    kit_default();
    key_model(0, DM_SAMPLE);
    K(0, DP_K1) = 0;                                           /* the factory kick, snare, hat, clap */
    key_model(1, DM_SAMPLE);
    K(1, DP_K1) = 1;
    key_model(2, DM_SAMPLE);
    K(2, DP_K1) = 2;
    key_model(3, DM_SAMPLE);
    K(3, DP_K1) = 2;
    K(3, DP_K2) = 60;
    key_model(4, DM_SAMPLE);
    K(4, DP_K1) = 3;
    beat("12-sample-kit", "the sample model: the factory kick, snare, hat (and a lower one), clap (placeholders)", B1, 16, 2, 110.0f);
    K(0, DP_FTYPE) = DF_LP;
    K(0, DP_CUT) = 60;
    K(0, DP_RES) = 80;
    K(1, DP_K4) = 1;
    K(2, DP_DECAY) = 40;
    beat("13-sample-kit-shaped", "the same with the kick sample low-passed (resonant), the snare reversed, the hat cut short",
         B1, 16, 2, 110.0f);
    kit_default();
    key_model(10, DM_SAMPLE);
    K(10, DP_K1) = 5;                                          /* the piano on a key, pitched by the knob */
    K(10, DP_DECAY) = 95;
    hits("14-sample-piano-key", "the piano sample on a key, its pitch -12, -8 .. +12 semitones", 10, 7, 0.5f, v_sample_pitch);
    kit_default();
    key_model(10, DM_SAMPLE);
    K(10, DP_K1) = 4;                                          /* the ah, reversed, through a band-pass */
    K(10, DP_K4) = 1;
    K(10, DP_FTYPE) = DF_BP;
    K(10, DP_CUT) = 80;
    K(10, DP_RES) = 60;
    K(10, DP_VOLUME) = 110;
    hits("15-sample-ah-reversed", "the sung ah reversed through a band-pass", 10, 3, 0.7f, 0);

    /* the sample engine: its modes on the piano, then the bank's samples played up the keys */
    w_gain = 1.0f;
    {
        static const int MEL[6] = {48, 55, 60, 64, 67, 72};
        static const int ONE[3] = {60, 60, 60};
        static const char *const MN[NSM] = {"one-shot", "loop", "reverse", "pingpong"};
        int t, m;
        char nm[64], what[160];
        for (t = 0; t < NTRK; t++)
            sound_default(spar[t], EG_SAMPLE);
        spar[0][TP_S] = 127;
        spar[0][TP_R] = 60;
        for (m = 0; m < NSM; m++) {
            spar[0][TP_E1] = (uint8_t)(m == SM_LOOP || m == SM_PINGPONG ? 4 : 5);
            spar[0][TP_E4] = (uint8_t)m;
            snprintf(nm, sizeof nm, "%02d-engine-%s", 16 + m, MN[m]);
            snprintf(what, sizeof what, "the sample engine, %s, on the %s: three notes of 1.2 s", MN[m], smp_name(spar[0][TP_E1]));
            synth_notes(nm, what, ONE, 3, 1.2f, 1.4f);
        }
        spar[0][TP_E1] = 5;
        spar[0][TP_E4] = SM_ONESHOT;
        synth_notes("20-engine-pitch", "the piano sample up the keys: C3 G3 C4 E4 G4 C5 (its root C4 at its own rate)", MEL, 6,
                    0.5f, 0.55f);
    }
    fclose(idx);
    return 0;
}
