/* SPDX-License-Identifier: GPL-3.0-only */
/* The drumline (dsp/drumline.c) on its own: every key of the default kit hits, stays finite, peaks in
 * -20..0 dBFS at volume 0 dB and velocity 127, falls to silence within its length; every model with
 * its four knobs at their ends stays finite and in range; the play modes (gated stops on release,
 * loop repeats while held, choke groups cut); the key's random and the track's random vary hits;
 * the track envelope's lag delays the onset, attack fades in, length scales; the key's own envelope
 * (attack, hold, decay, lag) and filter (low-pass darkens, high-pass thins, resonance peaks, the envelope
 * sweeps the cutoff, drive squares it, a turned knob is heard at once), bounded and finite at every corner;
 * the sample model (plays its sample at its rate, pitch, start, reverse, an empty slot is silent); the
 * dynamic filter bounds its output and bypasses on the other types; volume in dB, pan, sends; eight voices
 * at once stay bounded; and the cost in instructions a sample (macOS: proc_pid_rusage), the key stages and
 * the sample model too. Each key's hit is written to build/host/drum_<name>.wav for inspection (nothing
 * here plays audio).
 *   tests/host/drumline_test.sh */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include "drumline.h"
#include "synth.h"
#include "fastmath.h"
#include "sample.h"
#include "bank.h"                                 /* a user bank built in RAM */
static float gen_tone(int i) { return (float)(16000 * ((i / 20) & 1 ? 1 : -1)); }   /* a square, 20 samples a half */
#ifdef __APPLE__
#include <unistd.h>
#include <libproc.h>
#include <sys/resource.h>
static uint64_t instr_now(void)
{
    struct rusage_info_v4 ri;
    if (!proc_pid_rusage(getpid(), RUSAGE_INFO_V4, (rusage_info_t *)&ri))
        return ri.ri_instructions;
    return 0;
}
#else
static uint64_t instr_now(void) { return 0; }
#endif

#define SR 44100
#define BLK 64
#define VOL0 76                                       /* DP_VOLUME = 0 dB */
static int bad;
static void check(const char *what, int ok)
{
    printf("%-72s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

static uint8_t par[NTP];
static uint8_t dbuf[DRUMLINE_BYTES] __attribute__((aligned(8)));
static drumline_t *dl = (drumline_t *)dbuf;
static float L[BLK], R[BLK], S1[BLK], S2[BLK];

/* the project's neutral drum envelope: lag 0, attack 0, random 0, length 64 (1x) */
static void kit_default(void)
{
    int s, k;
    memset(par, 0, sizeof par);
    for (s = 0; s < DL_NSOUND; s++)
        for (k = 0; k < DP_N; k++)
            par[TP_DRUM(s, k)] = DL_DEFAULT_KIT[s][k];
    par[TP_A] = 0;
    par[TP_D] = 0;
    par[TP_S] = 0;
    par[TP_R] = 64;
    par[TP_SEND1] = 64;
    par[TP_SEND2] = 32;
}
static void key_model(int s, int model)               /* key s plays a model with its defaults */
{
    int k;
    par[TP_DRUM(s, DP_MODEL)] = (uint8_t)model;
    for (k = 0; k < 4; k++)
        par[TP_DRUM(s, DP_K1 + k)] = DM_DEFAULT[model][k];
}

static int nan_seen;
static void render(void)
{
    int i;
    memset(L, 0, sizeof L); memset(R, 0, sizeof R);
    memset(S1, 0, sizeof S1); memset(S2, 0, sizeof S2);
    drumline_render(dl, L, R, S1, S2, BLK);
    for (i = 0; i < BLK; i++)
        if (L[i] != L[i] || R[i] != R[i] || S1[i] != S1[i] || S2[i] != S2[i])
            nan_seen = 1;
}
static float blk_peak(void)
{
    int i;
    float pk = 0.0f;
    for (i = 0; i < BLK; i++)
        pk = fm_maxf(pk, fm_maxf(fm_fabsf(L[i]), fm_fabsf(R[i])));
    return pk;
}

static void wav_open(FILE **f, const char *name)
{
    static const uint8_t hdr[44] = {'R','I','F','F',0,0,0,0,'W','A','V','E','f','m','t',' ',16,0,0,0,1,0,1,0,
        0x44,0xAC,0,0,0x88,0x58,1,0,2,0,16,0,'d','a','t','a',0,0,0,0};
    *f = fopen(name, "wb");
    if (*f)
        fwrite(hdr, 1, 44, *f);
}
static void wav_put(FILE *f, const float *l, const float *r, int n)
{
    int i;
    for (i = 0; f && i < n; i++) {
        float v = (l[i] + r[i]) * 0.7071f * 32767.0f;
        int16_t s = (int16_t)fm_clampf(v, -32767.0f, 32767.0f);
        fwrite(&s, 2, 1, f);
    }
}
static void wav_close(FILE *f)
{
    uint32_t len;
    if (!f)
        return;
    len = (uint32_t)ftell(f);
    fseek(f, 4, SEEK_SET);
    { uint32_t v = len - 8; fwrite(&v, 4, 1, f); }
    fseek(f, 40, SEEK_SET);
    { uint32_t v = len - 44; fwrite(&v, 4, 1, f); }
    fclose(f);
}

/* one hit of a key: its peak, the time it falls under -60 dB of the peak, the time it stops, the first
 * sample above -40 dBFS, the RMS of the mono mix (as the WAV) from the start to the -60 dB point */
typedef struct { float peak, t60, tend, t_on, rms; } hit_t;
static hit_t one_hit(int sound, int vel, int pitch, int sg, int lt, FILE *wav)
{
    int b, i, nb = SR * 16 / BLK, last_loud = 0, first = -1;
    float pk = 0.0f;
    double sq = 0.0, sq_loud = 0.0;
    hit_t h;
    drumline_init(dl, par);
    drumline_hit(dl, sound, vel, pitch, sg, lt);
    h.tend = -1.0f;
    for (b = 0; b < nb; b++) {
        render();
        wav_put(wav, L, R, BLK);
        for (i = 0; i < BLK; i++) {
            float a = fm_maxf(fm_fabsf(L[i]), fm_fabsf(R[i])), mono = (L[i] + R[i]) * 0.7071f;
            sq += (double)(mono * mono);
            if (a > pk)
                pk = a;
            if (a > pk * 0.001f) {
                last_loud = b * BLK + i;
                sq_loud = sq;
            }
            if (first < 0 && a > 0.01f)
                first = b * BLK + i;
        }
        if (!drumline_sounding(dl)) {
            h.tend = (float)((b + 1) * BLK) / (float)SR;
            break;
        }
    }
    h.peak = pk;
    h.t60 = (float)last_loud / (float)SR;
    h.t_on = first < 0 ? -1.0f : (float)first / (float)SR;
    h.rms = (float)sqrt(sq_loud / (double)(last_loud + 1));
    return h;
}
static float dbfs(float x) { return 20.0f * fm_log2f(x + 1e-9f) * 0.30103f; }
static float rms_of(const float *x, int n)
{
    double s = 0.0;
    int i;
    for (i = 0; i < n; i++)
        s += (double)x[i] * (double)x[i];
    return n ? (float)sqrt(s / n) : 0.0f;
}
static float peak_of(const float *x, int n)
{
    float p = 0.0f;
    int i;
    for (i = 0; i < n; i++)
        p = fm_maxf(p, fm_fabsf(x[i]));
    return p;
}
static float diff_rms(const float *x, int n)          /* the first difference's RMS: brightness */
{
    double s = 0.0;
    int i;
    for (i = 1; i < n; i++)
        s += (double)(x[i] - x[i - 1]) * (double)(x[i] - x[i - 1]);
    return n > 1 ? (float)sqrt(s / (n - 1)) : 0.0f;
}
static float mag_at(const float *x, int n, float hz)  /* |DFT| at hz over a Hann window, per sample */
{
    double re = 0.0, im = 0.0, w = 6.283185307179586 * (double)hz / SR;
    int i;
    for (i = 0; i < n; i++) {
        double hw = 0.5 - 0.5 * cos(6.283185307179586 * i / (n - 1));
        re += hw * (double)x[i] * cos(w * i);
        im -= hw * (double)x[i] * sin(w * i);
    }
    return (float)(sqrt(re * re + im * im) / n);
}
static float cut_hz(int v) { return 50.0f * fm_exp2f((float)v * (8.64385619f / 127.0f)); }   /* DP_CUT, as synth.c */
static float band_at(const float *x, int n, float hz) /* the mean |DFT| over hz +-4 % (noise: one bin varies) */
{
    float s = 0.0f;
    int k;
    for (k = -20; k <= 20; k++)
        s += mag_at(x, n, hz * (1.0f + 0.002f * (float)k));
    return s / 41.0f;
}

/* a key's pitch: its hit alone (the mono mix) from t0 to t1 seconds, through a Hann window, and the
 * strongest frequency within -4 .. +4 semitones of the target, found in steps of a cent (a DFT at each:
 * double, this is the host) */
#define PMAX (SR / 2)
static float pbuf[PMAX];
static float pitch_of(int sound, float t0, float t1, float target)
{
    int b, i, n = 0, i0 = (int)(t0 * SR), i1 = (int)(t1 * SR), c, best = 0;
    double bestm = -1.0;
    drumline_init(dl, par);
    drumline_hit(dl, sound, 127, 0, 0, 0);
    for (b = 0; b * BLK < i1 && b * BLK < PMAX; b++) {
        render();
        for (i = 0; i < BLK; i++, n++)
            if (n >= i0 && n < i1)
                pbuf[n - i0] = (L[i] + R[i]) * 0.7071f;
    }
    n = i1 - i0;
    for (c = -400; c <= 400; c++) {
        double f = (double)target * pow(2.0, (double)c / 1200.0), re = 0.0, im = 0.0, w = 6.283185307179586 * f / SR;
        for (i = 0; i < n; i++) {
            double h = 0.5 - 0.5 * cos(6.283185307179586 * (double)i / (double)(n - 1)), x = (double)pbuf[i] * h;
            re += x * cos(w * (double)i);
            im -= x * sin(w * (double)i);
        }
        if (re * re + im * im > bestm) {
            bestm = re * re + im * im;
            best = c;
        }
    }
    return target * (float)pow(2.0, (double)best / 1200.0);
}

int main(void)
{
    int s, i, k, m;
    hit_t h, h2;
    char name[96];
    kit_default();

    printf("-- the default kit, volume 0 dB, velocity 127 (peak dBFS, RMS dBFS to -60 dB, -60 dB at, ends at)\n");
    {
        float kpk[DL_NSOUND];
        for (s = 0; s < DL_NSOUND; s++) {
            FILE *w;
            float db;
            snprintf(name, sizeof name, "build/host/drum_%s.wav", DL_SOUND_NAME[s]);
            wav_open(&w, name);
            h = one_hit(s, 127, 0, 0, 0, w);
            wav_close(w);
            db = dbfs(h.peak);
            kpk[s] = db;
            snprintf(name, sizeof name, "  %-7s %-7s %6.1f dBFS %6.1f rms  %5.2f s  %5.2f s", DL_SOUND_NAME[s],
                     DM_NAME[DL_DEFAULT_KIT[s][0]], (double)db, (double)dbfs(h.rms), (double)h.t60, (double)h.tend);
            check(name, !nan_seen && db >= -20.0f && db <= 0.0f && h.tend > 0.0f && h.t60 < 4.0f);
        }
        {
            float top = fm_minf(kpk[0], kpk[1]), rest = -99.0f;
            for (s = 2; s < DL_NSOUND; s++)
                rest = fm_maxf(rest, kpk[s]);
            snprintf(name, sizeof name, "  balanced: kick %.1f, snare %.1f, the rest up to %.1f, hats %.1f / %.1f dBFS",
                     (double)kpk[0], (double)kpk[1], (double)rest, (double)kpk[2], (double)kpk[3]);
            check(name, top >= rest - 0.5f && kpk[2] <= top - 2.0f && kpk[3] <= top - 2.0f &&
                        fm_fabsf(kpk[0] - kpk[1]) < 2.0f);
        }
        snprintf(name, sizeof name, "  the kick's length: -60 dB at %.2f s (0.15 .. 0.22)", (double)one_hit(0, 127, 0, 0, 0, 0).t60);
        check(name, one_hit(0, 127, 0, 0, 0, 0).t60 > 0.15f && one_hit(0, 127, 0, 0, 0, 0).t60 < 0.22f);
        check("the open hat rings longer than the closed (x3)", one_hit(3, 127, 0, 0, 0, 0).t60 > 3.0f * one_hit(2, 127, 0, 0, 0, 0).t60);
    }
    check("no NaN anywhere", !nan_seen);
    check("the default kit's hats share choke group 1",
          DL_DEFAULT_KIT[2][DP_MODE] == PM_CHOKE1 && DL_DEFAULT_KIT[3][DP_MODE] == PM_CHOKE1 &&
          DL_DEFAULT_KIT[2][DP_MODEL] == DM_HIHAT && DL_DEFAULT_KIT[3][DP_MODEL] == DM_HIHAT);
    check("the kit's key 10 is a string, the tambourine has no key",
          DL_DEFAULT_KIT[9][DP_MODEL] == DM_RESO && !strcmp(DL_SOUND_NAME[9], "string"));

    printf("-- the kit is tuned to C: each pitched key's strongest frequency near its note (within 25 cents)\n");
    {
        /* the snare's body is gone in 70 ms, too short to measure under its wires: it is measured with its
         * pitch as in the kit, the length at 127 and the wires at 0 (the pitch does not depend on them) */
        static const struct { int key; float t0, t1, hz; const char *note; } PT[] = {
            {0, 0.040f, 0.160f, 49.00f, "G1"},  {1, 0.030f, 0.300f, 196.00f, "G3"}, {5, 0.060f, 0.200f, 130.81f, "C3"},
            {6, 0.060f, 0.200f, 196.00f, "G3"}, {7, 0.030f, 0.120f, 261.63f, "C4"}, {9, 0.050f, 0.300f, 130.81f, "C3"},
            {10, 0.250f, 0.450f, 523.25f, "C5"}};
        for (i = 0; i < (int)(sizeof PT / sizeof PT[0]); i++) {
            float hz;
            if (PT[i].key == 1) {
                par[TP_DRUM(1, DP_K2)] = 127;
                par[TP_DRUM(1, DP_K3)] = 0;
            }
            hz = pitch_of(PT[i].key, PT[i].t0, PT[i].t1, PT[i].hz);
            par[TP_DRUM(1, DP_K2)] = DL_DEFAULT_KIT[1][DP_K2];
            par[TP_DRUM(1, DP_K3)] = DL_DEFAULT_KIT[1][DP_K3];
            float cents = 1200.0f * fm_log2f(hz / PT[i].hz);
            snprintf(name, sizeof name, "  %-7s %-3s %7.2f Hz measured (%7.2f Hz, %+5.1f cents)", DL_SOUND_NAME[PT[i].key],
                     PT[i].note, (double)hz, (double)PT[i].hz, (double)cents);
            check(name, fm_fabsf(cents) <= 25.0f);
        }
        /* the hats: the 808's six metal oscillators times 2^((k1 - 0.73) x 2), so k1 = 93 is the 808's own set */
        {
            float ratio = fm_exp2f(((float)DL_DEFAULT_KIT[2][DP_K1] / 127.0f - 0.73f) * 2.0f);
            snprintf(name, sizeof name, "  the hats' metal at x %.4f the 808's (%+.1f cents; 540 Hz -> %.1f Hz)", (double)ratio,
                     (double)(1200.0f * fm_log2f(ratio)), (double)(540.0f * ratio));
            check(name, fm_fabsf(1200.0f * fm_log2f(ratio)) < 10.0f && DL_DEFAULT_KIT[2][DP_K1] == DL_DEFAULT_KIT[3][DP_K1]);
        }
    }

    printf("-- a short beat of the default kit (build/host/drum_beat.wav: two bars of 16ths at 120 BPM)\n");
    {
        /* per step, the keys that hit: bit k = key k */
        static const uint16_t BEAT[32] = {
            0x001 | 0x004 | 0x200, 0x100, 0x004 | 0x100, 0x100, 0x002 | 0x004 | 0x100, 0x100, 0x004 | 0x100, 0x080,
            0x001 | 0x004, 0x100, 0x001 | 0x004 | 0x100, 0x400, 0x002 | 0x010 | 0x004, 0x100, 0x008, 0x080,
            0x001 | 0x004, 0x100, 0x004 | 0x100, 0x100, 0x002 | 0x004, 0x100, 0x004 | 0x100, 0x200,
            0x001 | 0x004, 0x100, 0x001 | 0x040, 0x040, 0x002 | 0x010 | 0x020, 0x020, 0x008 | 0x020, 0x080};
        int step, nstep = SR * 60 / 120 / 4 / BLK, b, j;
        FILE *w;
        float pk = 0.0f;
        double sq = 0.0;
        long nsamp = 0;
        wav_open(&w, "build/host/drum_beat.wav");
        drumline_init(dl, par);
        for (step = 0; step < 32 + 8; step++) {
            if (step < 32)
                for (j = 0; j < DL_NSOUND; j++)
                    if (BEAT[step] >> j & 1)
                        drumline_hit(dl, j, j == 8 ? 70 : (j == 2 && (step & 3)) ? 90 : 127, 0, 0, 0);
            for (b = 0; b < nstep; b++) {
                render();
                wav_put(w, L, R, BLK);
                for (i = 0; i < BLK; i++) {
                    float mono = (L[i] + R[i]) * 0.7071f;
                    pk = fm_maxf(pk, fm_maxf(fm_fabsf(L[i]), fm_fabsf(R[i])));
                    sq += (double)(mono * mono);
                    nsamp++;
                }
            }
        }
        wav_close(w);
        snprintf(name, sizeof name, "  the beat: peak %.1f dBFS, RMS %.1f dBFS, under 0 dBFS", (double)dbfs(pk),
                 (double)dbfs((float)sqrt(sq / (double)nsamp)));
        check(name, pk < 1.0f && !nan_seen);
    }

    printf("-- every model: its defaults, then each knob at 0 and 127 (finite, -20..0 dBFS at the defaults, in range\n");
    printf("   at the ends; length 0 ends within 1.5 s, 127 within 12 s)\n");
    for (m = 0; m < NDM; m++) {
        int ok = 1;
        float db, worst_lo = 0.0f, worst_hi = 0.0f;
        key_model(0, m);
        h = one_hit(0, 127, 0, 0, 0, 0);
        db = dbfs(h.peak);
        ok = !nan_seen && db >= -20.0f && db <= 0.0f;
        for (k = 0; k < 4 && ok; k++) {
            int e;
            for (e = 0; e <= 127; e += 127) {
                float d2;
                par[TP_DRUM(0, DP_K1 + k)] = (uint8_t)e;
                h2 = one_hit(0, 127, 0, 0, 0, 0);
                d2 = dbfs(h2.peak);
                if (d2 < worst_lo || k == 0)
                    worst_lo = fm_minf(worst_lo, d2);
                worst_hi = fm_maxf(worst_hi, d2);
                if (m == DM_SAMPLE && k == 0 && e > smp_factory() - 1) {   /* an empty user slot: silence */
                    if (nan_seen || h2.peak > 0.0f || h2.tend > 0.01f) {
                        printf("  sample: an empty slot plays %.1f dBFS, ends at %.3f s\n", (double)d2, (double)h2.tend);
                        ok = 0;
                    }
                    par[TP_DRUM(0, DP_K1 + k)] = DM_DEFAULT[m][k];
                    continue;
                }
                if (nan_seen || d2 < (m == DM_SAMPLE && k == 2 ? -40.0f : -26.0f) || d2 > 1.0f) {   /* (the
                                                       * sample's start at 99 %: its quiet tail) */
                    printf("  %s %s=%d: %.1f dBFS, nan %d\n", DM_NAME[m], DM_KNOB[m][k], e, (double)d2, nan_seen);
                    ok = 0;
                }
                if (k == 1 && ((e == 0 && (h2.tend < 0.0f || h2.tend > 1.5f)) || (e == 127 && (h2.tend < 0.0f || h2.tend > 12.0f)))) {
                    printf("  %s length=%d ends at %.2f s\n", DM_NAME[m], e, (double)h2.tend);
                    ok = 0;
                }
                par[TP_DRUM(0, DP_K1 + k)] = DM_DEFAULT[m][k];
            }
        }
        snprintf(name, sizeof name, "  %-10s %s/%s/%s/%s  default %5.1f dBFS, ends %5.1f .. %5.1f", DM_NAME[m],
                 DM_KNOB[m][0], DM_KNOB[m][1], DM_KNOB[m][2], DM_KNOB[m][3], (double)db, (double)worst_lo,
                 (double)worst_hi);
        check(name, ok);
    }
    kit_default();
    {
        par[TP_DRUM(0, DP_K2)] = 127;
        h = one_hit(0, 127, 0, 1, 0, 0);
        check("short gate: a long kick is under -60 dB by 150 ms", h.t60 < 0.15f);
        par[TP_DRUM(0, DP_K2)] = 0;
        h = one_hit(0, 127, 0, 0, 0, 0);
        h2 = one_hit(0, 127, 0, 0, 1, 0);
        check("long tail: the shortest kick rings at least twice as long", h2.t60 >= 2.0f * h.t60);
        par[TP_DRUM(0, DP_K2)] = DL_DEFAULT_KIT[0][DP_K2];
        h = one_hit(0, 127, 0, 0, 0, 0);
        h2 = one_hit(0, 127, 12, 0, 0, 0);
        check("pitch +12: still in range", h2.peak > 0.1f && h2.peak <= 1.0f);
        h2 = one_hit(0, 40, 0, 0, 0, 0);
        check("velocity 40 is quieter than 127", h2.peak < h.peak);
        h2 = one_hit(10, 127, 7, 0, 0, 0);
        check("fm: pitch +7 steps still in range", h2.peak > 0.1f && h2.peak <= 1.0f && !nan_seen);
    }

    printf("-- the shift page: volume in dB, pan\n");
    {
        float p0, p6, p20, p30;
        h = one_hit(0, 127, 0, 0, 0, 0);
        p0 = h.peak;
        par[TP_DRUM(0, DP_VOLUME)] = 127;
        p20 = one_hit(0, 127, 0, 0, 0, 0).peak;
        par[TP_DRUM(0, DP_VOLUME)] = 1;
        p30 = one_hit(0, 127, 0, 0, 0, 0).peak;
        par[TP_DRUM(0, DP_VOLUME)] = 61;                /* -6 dB */
        p6 = one_hit(0, 127, 0, 0, 0, 0).peak;
        par[TP_DRUM(0, DP_VOLUME)] = 0;
        h2 = one_hit(0, 127, 0, 0, 0, 0);
        par[TP_DRUM(0, DP_VOLUME)] = VOL0;
        snprintf(name, sizeof name, "  volume 127 = +20 dB (%.1f), 61 = -6 dB (%.1f), 1 = -30 dB (%.1f), 0 silent",
                 (double)dbfs(p20 / p0), (double)dbfs(p6 / p0), (double)dbfs(p30 / p0));
        check(name, fm_fabsf(dbfs(p20 / p0) - 20.0f) < 0.5f && fm_fabsf(dbfs(p6 / p0) + 6.0f) < 0.5f &&
              fm_fabsf(dbfs(p30 / p0) + 30.0f) < 0.5f && h2.peak == 0.0f);
    }
    {
        float sl = 0.0f, sr = 0.0f, s1 = 0.0f, s2 = 0.0f;
        int b;
        par[TP_DRUM(0, DP_PAN)] = 127;
        drumline_init(dl, par);
        drumline_hit(dl, 0, 127, 0, 0, 0);
        for (b = 0; b < 40; b++) {
            render();
            for (i = 0; i < BLK; i++) {
                sl += L[i] * L[i]; sr += R[i] * R[i]; s1 += S1[i] * S1[i]; s2 += S2[i] * S2[i];
            }
        }
        check("pan 127: the right side only", sr > 0.0f && sl < sr * 1e-6f);
        check("sends follow TP_SEND1 (64) / TP_SEND2 (32)", s1 > s2 * 3.5f && s1 < s2 * 4.5f);
        par[TP_DRUM(0, DP_PAN)] = 64;
    }

    printf("-- play modes\n");
    {
        int b;
        float pk;
        /* gated: the open hat (long) stops 5 ms after its release */
        par[TP_DRUM(3, DP_MODE)] = PM_GATED;
        drumline_init(dl, par);
        drumline_hit(dl, 3, 127, 0, 0, 0);
        for (b = 0; b < 35; b++)                          /* 50 ms */
            render();
        pk = blk_peak();
        drumline_release(dl, 3);
        for (b = 0; b < 8; b++)                           /* 11 ms */
            render();
        check("gated: the open hat is -40 dB 11 ms after drumline_release", pk > 0.01f && blk_peak() < pk * 0.01f);
        for (b = 0; b < 40; b++)
            render();
        check("gated: and off within 70 ms", !drumline_sounding(dl));
        /* one-shot: a release changes nothing */
        par[TP_DRUM(3, DP_MODE)] = PM_ONESHOT;
        drumline_init(dl, par);
        drumline_hit(dl, 3, 127, 0, 0, 0);
        for (b = 0; b < 35; b++)
            render();
        pk = blk_peak();
        drumline_release(dl, 3);
        for (b = 0; b < 8; b++)
            render();
        check("one-shot: a release does not cut it", blk_peak() > pk * 0.3f);
        /* loop: the shortest kick restarts while held, so it is still sounding after a second; released, it ends */
        par[TP_DRUM(0, DP_MODE)] = PM_LOOP;
        par[TP_DRUM(0, DP_K2)] = 0;
        drumline_init(dl, par);
        drumline_hit(dl, 0, 127, 0, 0, 0);
        {
            int hits = 0, was_quiet = 0;
            for (b = 0; b < SR / BLK; b++) {
                float p;
                render();
                p = blk_peak();
                if (p < 0.02f)
                    was_quiet = 1;
                else if (was_quiet) {
                    hits++;
                    was_quiet = 0;
                }
            }
            snprintf(name, sizeof name, "  loop: a 50 ms kick restarted %d times in a second while held", hits);
            check(name, hits >= 5 && drumline_sounding(dl));
        }
        drumline_release(dl, 0);
        for (b = 0; b < SR / BLK / 2; b++)
            render();
        check("loop: released, it finishes and stops", !drumline_sounding(dl));
        par[TP_DRUM(0, DP_MODE)] = PM_ONESHOT;
        par[TP_DRUM(0, DP_K2)] = DL_DEFAULT_KIT[0][DP_K2];
        /* choke: the open hat hard left, the closed hat hard right: the left shows the open hat alone */
        par[TP_DRUM(3, DP_MODE)] = DL_DEFAULT_KIT[3][DP_MODE];
        par[TP_DRUM(3, DP_PAN)] = 0;
        par[TP_DRUM(2, DP_PAN)] = 127;
        drumline_init(dl, par);
        drumline_hit(dl, 3, 127, 0, 0, 0);
        pk = 0.0f;
        for (b = 0; b < 70; b++) {                        /* 100 ms */
            render();
            for (i = 0; i < BLK; i++)
                pk = fm_maxf(pk, fm_fabsf(L[i]));
        }
        drumline_hit(dl, 2, 127, 0, 0, 0);
        for (b = 0; b < 8; b++)                           /* 11 ms later */
            render();
        {
            float lpk2 = 0.0f;
            for (b = 0; b < 8; b++) {
                render();
                for (i = 0; i < BLK; i++)
                    lpk2 = fm_maxf(lpk2, fm_fabsf(L[i]));
            }
            check("choke 1: the closed hat chokes the open hat (-40 dB in 11 ms)", lpk2 < pk * 0.01f && pk > 0.01f);
        }
        /* a different group does not */
        par[TP_DRUM(2, DP_MODE)] = PM_CHOKE2;
        drumline_init(dl, par);
        drumline_hit(dl, 3, 127, 0, 0, 0);
        for (b = 0; b < 70; b++)
            render();
        drumline_hit(dl, 2, 127, 0, 0, 0);
        for (b = 0; b < 16; b++)
            render();
        {
            float lpk2 = 0.0f;
            for (i = 0; i < BLK; i++)
                lpk2 = fm_maxf(lpk2, fm_fabsf(L[i]));
            check("choke 2 against choke 1: the open hat keeps ringing", lpk2 > pk * 0.05f);
        }
        par[TP_DRUM(2, DP_MODE)] = PM_CHOKE1;
        par[TP_DRUM(3, DP_PAN)] = DL_DEFAULT_KIT[3][DP_PAN];
        par[TP_DRUM(2, DP_PAN)] = DL_DEFAULT_KIT[2][DP_PAN];
    }

    printf("-- random: the key's and the track's vary the hits; the kick's pitch keeps out of the track's\n");
    {
        float t1, t2, t3, p1, p2;
        h = one_hit(1, 127, 0, 0, 0, 0);
        h2 = one_hit(1, 127, 0, 0, 0, 0);
        check("random 0: two snare hits are the same", h.peak == h2.peak && h.t60 == h2.t60);
        par[TP_DRUM(1, DP_RANDOM)] = 127;
        drumline_init(dl, par);
        drumline_hit(dl, 1, 127, 0, 0, 0);
        {
            float a = 0.0f, b2 = 0.0f, c = 0.0f;
            int b;
            for (b = 0; b < 300; b++) { render(); a = fm_maxf(a, blk_peak()); }
            drumline_hit(dl, 1, 127, 0, 0, 0);
            for (b = 0; b < 300; b++) { render(); b2 = fm_maxf(b2, blk_peak()); }
            drumline_hit(dl, 1, 127, 0, 0, 0);
            for (b = 0; b < 300; b++) { render(); c = fm_maxf(c, blk_peak()); }
            t1 = a; t2 = b2; t3 = c;
        }
        check("key random 127: three snare hits differ", !(t1 == t2 && t2 == t3) && !nan_seen);
        par[TP_DRUM(1, DP_RANDOM)] = 0;
        par[TP_S] = 127;
        drumline_init(dl, par);
        drumline_hit(dl, 1, 127, 0, 0, 0);
        {
            float a = 0.0f, b2 = 0.0f;
            int b;
            for (b = 0; b < 300; b++) { render(); a = fm_maxf(a, blk_peak()); }
            drumline_hit(dl, 1, 127, 0, 0, 0);
            for (b = 0; b < 300; b++) { render(); b2 = fm_maxf(b2, blk_peak()); }
            check("track random 127: two snare hits differ", a != b2 && !nan_seen);
        }
        /* the kick's pitch: with the track's random its zero crossings in the body are those of random 0 */
        {
            int zc0 = 0, zc1 = 0, b;
            par[TP_S] = 0;
            par[TP_DRUM(0, DP_K3)] = 0;                   /* no click, no drive: a plain sine */
            par[TP_DRUM(0, DP_K2)] = 127;
            drumline_init(dl, par);
            drumline_hit(dl, 0, 127, 0, 0, 0);
            for (b = 0; b < 100; b++) render();
            for (b = 0; b < 300; b++) {
                render();
                for (i = 1; i < BLK; i++)
                    zc0 += (L[i] > 0.0f) != (L[i - 1] > 0.0f);
            }
            par[TP_S] = 127;
            p1 = p2 = 0.0f;
            for (k = 0; k < 4; k++) {
                drumline_init(dl, par);
                drumline_hit(dl, 0, 127, 0, 0, 0);
                for (b = 0; b < 100; b++) render();
                zc1 = 0;
                for (b = 0; b < 300; b++) {
                    render();
                    for (i = 1; i < BLK; i++)
                        zc1 += (L[i] > 0.0f) != (L[i - 1] > 0.0f);
                }
                p1 = fm_maxf(p1, fm_fabsf((float)(zc1 - zc0)));
            }
            snprintf(name, sizeof name, "  track random: the kick's pitch holds (zero crossings %d, off by at most %.0f)", zc0, (double)p1);
            check(name, zc0 > 35 && p1 <= 2.0f);
            par[TP_S] = 0;
            par[TP_DRUM(0, DP_K3)] = DL_DEFAULT_KIT[0][DP_K3];
            par[TP_DRUM(0, DP_K2)] = DL_DEFAULT_KIT[0][DP_K2];
        }
        (void)t3;
    }

    printf("-- the track envelope: lag, attack, length\n");
    {
        float on0, on1, t0;
        h = one_hit(1, 127, 0, 0, 0, 0);
        on0 = h.t_on;
        t0 = h.t60;
        par[TP_A] = 127;
        h2 = one_hit(1, 127, 0, 0, 0, 0);
        on1 = h2.t_on;
        snprintf(name, sizeof name, "  lag 127: the snare starts %.1f ms later (55..65)", (double)((on1 - on0) * 1000.0f));
        check(name, on1 - on0 > 0.055f && on1 - on0 < 0.065f && h2.peak > 0.1f);
        par[TP_A] = 0;
        {
            int b, a;
            float early[2], late[2];
            for (a = 0; a < 2; a++) {                     /* the open hat without, then with the attack */
                par[TP_D] = (uint8_t)(a ? 127 : 0);
                drumline_init(dl, par);
                drumline_hit(dl, 3, 127, 0, 0, 0);
                early[a] = late[a] = 0.0f;
                for (b = 0; b < 7; b++) { render(); early[a] = fm_maxf(early[a], blk_peak()); }   /* the first 10 ms */
                for (b = 0; b < 62; b++) render();
                for (b = 0; b < 7; b++) { render(); late[a] = fm_maxf(late[a], blk_peak()); }    /* at 100 ms */
            }
            snprintf(name, sizeof name, "  attack 127: the open hat's first 10 ms %.3f -> %.3f, at 100 ms %.3f -> %.3f",
                     (double)early[0], (double)early[1], (double)late[0], (double)late[1]);
            check(name, early[1] < early[0] * 0.25f && late[1] > late[0] * 0.7f);
        }
        par[TP_D] = 0;
        par[TP_R] = 0;
        h = one_hit(1, 127, 0, 0, 0, 0);
        par[TP_R] = 127;
        h2 = one_hit(1, 127, 0, 0, 0, 0);
        par[TP_R] = 64;
        snprintf(name, sizeof name, "  length 0 / 64 / 127: the snare's -60 dB at %.2f / %.2f / %.2f s", (double)h.t60,
                 (double)t0, (double)h2.t60);
        check(name, h.t60 < t0 * 0.5f && h2.t60 > t0 * 2.0f);
    }

    printf("-- the key's own envelope: attack, hold, decay, lag\n");
    {
        kit_default();
        par[TP_DRUM(0, DP_K2)] = 127;                     /* a long kick: 1.5 s of model */
        h = one_hit(0, 127, 0, 0, 0, 0);
        par[TP_DRUM(0, DP_DECAY)] = 64;                   /* the key's decay: about 100 ms to -60 dB */
        h2 = one_hit(0, 127, 0, 0, 0, 0);
        snprintf(name, sizeof name, "  decay 64: the long kick's -60 dB at %.2f -> %.2f s, it ends at %.2f s", (double)h.t60,
                 (double)h2.t60, (double)h2.tend);
        check(name, h2.t60 < 0.15f && h2.t60 > 0.05f && h2.tend > 0.0f && h2.tend < 0.2f && h.t60 > 0.8f);
        par[TP_DRUM(0, DP_DECAY)] = 90;
        h = one_hit(0, 127, 0, 0, 0, 0);
        snprintf(name, sizeof name, "  decay 90 rings longer than 64: %.2f s", (double)h.t60);
        check(name, h.t60 > h2.t60 * 2.0f);
        par[TP_DRUM(0, DP_HOLD)] = 70;                    /* hold ~140 ms, then the decay */
        par[TP_DRUM(0, DP_DECAY)] = 40;
        h2 = one_hit(0, 127, 0, 0, 0, 0);
        snprintf(name, sizeof name, "  hold 70 + decay 40: -60 dB at %.3f s (the hold's 0.16 s and a short fall)", (double)h2.t60);
        check(name, h2.t60 > 0.14f && h2.t60 < 0.2f);
        par[TP_DRUM(0, DP_HOLD)] = 0;
        par[TP_DRUM(0, DP_DECAY)] = 127;
        {   /* attack: the first 10 ms quiet, by 400 ms full */
            int b, a;
            float early[2], late[2];
            for (a = 0; a < 2; a++) {
                par[TP_DRUM(3, DP_ATTACK)] = (uint8_t)(a ? 80 : 0);   /* ~330 ms on the open hat */
                drumline_init(dl, par);
                drumline_hit(dl, 3, 127, 0, 0, 0);
                early[a] = late[a] = 0.0f;
                for (b = 0; b < 7; b++) { render(); early[a] = fm_maxf(early[a], blk_peak()); }
                for (b = 0; b < 260; b++) render();
                for (b = 0; b < 7; b++) { render(); late[a] = fm_maxf(late[a], blk_peak()); }   /* at ~400 ms */
            }
            snprintf(name, sizeof name, "  attack 80: the open hat's first 10 ms %.3f -> %.3f, at 400 ms %.4f -> %.4f",
                     (double)early[0], (double)early[1], (double)late[0], (double)late[1]);
            check(name, early[1] < early[0] * 0.1f && late[1] > late[0] * 0.5f);
            par[TP_DRUM(3, DP_ATTACK)] = 0;
        }
        h = one_hit(1, 127, 0, 0, 0, 0);
        par[TP_DRUM(1, DP_LAG)] = 127;
        h2 = one_hit(1, 127, 0, 0, 0, 0);
        snprintf(name, sizeof name, "  the key's lag 127: the snare starts %.1f ms later (55..65)", (double)((h2.t_on - h.t_on) * 1000.0f));
        check(name, h2.t_on - h.t_on > 0.055f && h2.t_on - h.t_on < 0.065f);
        par[TP_A] = 127;
        h2 = one_hit(1, 127, 0, 0, 0, 0);
        snprintf(name, sizeof name, "  and the kit's lag on top: %.1f ms later (115..125)", (double)((h2.t_on - h.t_on) * 1000.0f));
        check(name, h2.t_on - h.t_on > 0.115f && h2.t_on - h.t_on < 0.125f);
        kit_default();
    }

    printf("-- the key's own filter: type, cutoff, resonance, envelope, drive\n");
    {
        static float buf[SR];
        int n;
        float r_off, r_lp, d_off, d_lp, m0, m1, a_early, a_late;
        /* a hit's mono mix into buf (up to a second): its length */
#define GRAB(snd) do { int b_, i_; n = 0; drumline_init(dl, par); drumline_hit(dl, (snd), 127, 0, 0, 0); \
        for (b_ = 0; b_ < SR / BLK; b_++) { render(); for (i_ = 0; i_ < BLK; i_++) buf[n++] = (L[i_] + R[i_]) * 0.7071f; } } while (0)
        kit_default();
        GRAB(3);                                          /* the open hat: bright */
        r_off = rms_of(buf, n);
        d_off = diff_rms(buf, n) / r_off;
        par[TP_DRUM(3, DP_FTYPE)] = DF_LP;
        par[TP_DRUM(3, DP_CUT)] = 127;
        GRAB(3);
        snprintf(name, sizeof name, "  lp open (cutoff 127) is close to off: %.2f dB", (double)(dbfs(rms_of(buf, n)) - dbfs(r_off)));
        check(name, fm_fabsf(dbfs(rms_of(buf, n)) - dbfs(r_off)) < 1.5f && !nan_seen);
        par[TP_DRUM(3, DP_CUT)] = 60;
        GRAB(3);
        r_lp = rms_of(buf, n);
        d_lp = diff_rms(buf, n) / r_lp;
        snprintf(name, sizeof name, "  lp at %.0f Hz darkens the open hat: RMS %.1f dB, brightness %.3f -> %.3f",
                 (double)cut_hz(60), (double)(dbfs(r_lp) - dbfs(r_off)), (double)d_off, (double)d_lp);
        check(name, dbfs(r_lp) < dbfs(r_off) - 10.0f && d_lp < d_off * 0.5f);
        kit_default();
        GRAB(0);                                          /* the kick */
        r_off = rms_of(buf, n);
        par[TP_DRUM(0, DP_FTYPE)] = DF_HP;
        par[TP_DRUM(0, DP_CUT)] = 75;
        GRAB(0);
        snprintf(name, sizeof name, "  hp at %.0f Hz thins the kick: %.1f dB", (double)cut_hz(75), (double)(dbfs(rms_of(buf, n)) - dbfs(r_off)));
        check(name, dbfs(rms_of(buf, n)) < dbfs(r_off) - 15.0f);
        kit_default();
        key_model(0, DM_SHAKER);                          /* noise: the resonance's peak at the cutoff */
        par[TP_DRUM(0, DP_K2)] = 127;
        par[TP_DRUM(0, DP_FTYPE)] = DF_LP;
        par[TP_DRUM(0, DP_CUT)] = 75;
        par[TP_DRUM(0, DP_RES)] = 0;
        GRAB(0);
        m0 = band_at(buf, n, cut_hz(75)) / rms_of(buf, n);
        par[TP_DRUM(0, DP_RES)] = 127;
        GRAB(0);
        m1 = band_at(buf, n, cut_hz(75)) / rms_of(buf, n);
        snprintf(name, sizeof name, "  resonance 127 peaks at the cutoff (%.0f Hz): %.1f dB more of it in the sound",
                 (double)cut_hz(75), (double)(dbfs(m1) - dbfs(m0)));
        check(name, dbfs(m1) > dbfs(m0) + 6.0f && !nan_seen);
        par[TP_DRUM(0, DP_RES)] = 127;
        par[TP_DRUM(0, DP_FTYPE)] = DF_BP;
        GRAB(0);
        check("  band-pass at full resonance: finite, bounded", !nan_seen && peak_of(buf, n) < 1.0f && rms_of(buf, n) > 1e-4f);
        /* the envelope on the cutoff: a low cutoff opened by the envelope, the decay closing it */
        par[TP_DRUM(0, DP_FTYPE)] = DF_LP;
        par[TP_DRUM(0, DP_RES)] = 30;
        par[TP_DRUM(0, DP_CUT)] = 30;
        par[TP_DRUM(0, DP_FENV)] = 127;
        par[TP_DRUM(0, DP_DECAY)] = 85;                   /* ~0.4 s */
        GRAB(0);
        a_early = diff_rms(buf + 441, 4410) / fm_maxf(rms_of(buf + 441, 4410), 1e-9f);
        a_late = diff_rms(buf + 13230, 4410) / fm_maxf(rms_of(buf + 13230, 4410), 1e-9f);
        snprintf(name, sizeof name, "  env 127: bright at the hit, darker as the decay closes it (%.3f -> %.3f)",
                 (double)a_early, (double)a_late);
        check(name, a_early > a_late * 1.5f);
        /* drive: a quiet hit squared off, its level kept near a drum's */
        kit_default();
        {
            float c0, c1;
            GRAB(1);
            c0 = peak_of(buf, n) / rms_of(buf, n);
            par[TP_DRUM(1, DP_DRIVE)] = 127;
            GRAB(1);
            c1 = peak_of(buf, n) / rms_of(buf, n);
            snprintf(name, sizeof name, "  drive 127 squares the snare: crest %.1f -> %.1f, peak %.2f", (double)c0, (double)c1,
                     (double)peak_of(buf, n));
            check(name, c1 < c0 * 0.6f && peak_of(buf, n) < 1.0f && peak_of(buf, n) > 0.2f);
        }
        /* a knob turned on a sounding key is heard at once (read every chunk), and locks reach it (epar) */
        kit_default();
        {
            int b;
            float before, after;
            par[TP_DRUM(9, DP_K2)] = 127;                 /* the string, long */
            drumline_init(dl, par);
            drumline_hit(dl, 9, 127, 0, 0, 0);
            for (b = 0; b < 100; b++)
                render();
            for (before = 0.0f, b = 0; b < 20; b++) { render(); before += diff_rms(L, BLK); }
            par[TP_DRUM(9, DP_FTYPE)] = DF_LP;
            par[TP_DRUM(9, DP_CUT)] = 20;
            for (after = 0.0f, b = 0; b < 20; b++) { render(); after += diff_rms(L, BLK); }
            snprintf(name, sizeof name, "  the cutoff turned down on a ringing string darkens it at once (%.4f -> %.4f)",
                     (double)before, (double)after);
            check(name, after < before * 0.3f);
        }
        /* every model, every type, the corners: finite and bounded */
        {
            int ok = 1, ft, c, mdl;
            static const uint8_t CORNER[4][6] = {{0, 0, 0, 0, 0, 0}, {127, 127, 127, 127, 127, 127},
                                                 {0, 127, 0, 127, 0, 127}, {127, 0, 127, 0, 127, 0}};
            float pk = 0.0f;
            for (mdl = 0; mdl < NDM && ok; mdl++)
                for (ft = 0; ft < NDF && ok; ft++)
                    for (c = 0; c < 4 && ok; c++) {
                        kit_default();
                        key_model(0, mdl);
                        par[TP_DRUM(0, DP_FTYPE)] = (uint8_t)ft;
                        par[TP_DRUM(0, DP_CUT)] = CORNER[c][0];
                        par[TP_DRUM(0, DP_RES)] = CORNER[c][1];
                        par[TP_DRUM(0, DP_FENV)] = CORNER[c][2];
                        par[TP_DRUM(0, DP_DRIVE)] = CORNER[c][3];
                        par[TP_DRUM(0, DP_ATTACK)] = CORNER[c][4] / 2;
                        par[TP_DRUM(0, DP_DECAY)] = CORNER[c][5] ? 100 : 30;
                        nan_seen = 0;
                        h = one_hit(0, 127, 0, 0, 0, 0);
                        pk = fm_maxf(pk, h.peak);
                        if (nan_seen || h.peak > 1.5f || h.tend < 0.0f) {
                            printf("  %s, filter %d, corner %d: peak %.2f, nan %d, ends %.2f\n", DM_NAME[mdl], ft, c,
                                   (double)h.peak, nan_seen, (double)h.tend);
                            ok = 0;
                        }
                    }
            snprintf(name, sizeof name, "  every model x type x corner: finite, ends, peak %.2f (under 1.5)", (double)pk);
            check(name, ok);
        }
        kit_default();
#undef GRAB
    }

    printf("-- the sample model\n");
    {
        const smp_t *kick = smp_get(0);
        float t_root, t_up;
        kit_default();
        key_model(0, DM_SAMPLE);                          /* sample 0: the factory kick */
        h = one_hit(0, 127, 0, 0, 0, 0);
        t_root = h.tend;
        snprintf(name, sizeof name, "  the kick sample: peak %.1f dBFS, ends at %.3f s (its %.3f s)", (double)dbfs(h.peak),
                 (double)h.tend, (double)((float)kick->n / (float)SMP_RATE));
        check(name, dbfs(h.peak) > -14.0f && dbfs(h.peak) < -3.0f && fm_fabsf(h.tend - (float)kick->n / (float)SMP_RATE) < 0.01f);
        par[TP_DRUM(0, DP_K2)] = 76;                      /* +12 */
        h = one_hit(0, 127, 0, 0, 0, 0);
        t_up = h.tend;
        snprintf(name, sizeof name, "  pitch +12 plays it twice as fast: %.3f s", (double)t_up);
        check(name, fm_fabsf(t_up - t_root * 0.5f) < 0.01f);
        h = one_hit(0, 127, -12, 0, 0, 0);
        snprintf(name, sizeof name, "  and the hit's transpose -12 back at its rate: %.3f s", (double)h.tend);
        check(name, fm_fabsf(h.tend - t_root) < 0.01f);
        par[TP_DRUM(0, DP_K2)] = 64;
        par[TP_DRUM(0, DP_K3)] = 64;                      /* from the middle */
        h = one_hit(0, 127, 0, 0, 0, 0);
        snprintf(name, sizeof name, "  start 50 %%: half of it, %.3f s", (double)h.tend);
        check(name, fm_fabsf(h.tend - t_root * 0.5f) < 0.02f);
        par[TP_DRUM(0, DP_K3)] = 0;
        par[TP_DRUM(0, DP_DECAY)] = 40;
        h = one_hit(0, 127, 0, 0, 0, 0);
        snprintf(name, sizeof name, "  the key's decay 40 shortens it: ends at %.3f s", (double)h.tend);
        check(name, h.tend < t_root * 0.5f);
        par[TP_DRUM(0, DP_DECAY)] = 127;
        {   /* reverse: the kick backwards swells into its attack */
            static float buf[SR];
            int b, i, n = 0;
            float e0, e1;
            par[TP_DRUM(0, DP_K4)] = 1;
            drumline_init(dl, par);
            drumline_hit(dl, 0, 127, 0, 0, 0);
            for (b = 0; b < (int)(t_root * SR) / BLK; b++) {
                render();
                for (i = 0; i < BLK; i++)
                    buf[n++] = L[i];
            }
            e0 = rms_of(buf, n / 4);
            e1 = rms_of(buf + 3 * n / 4, n / 4);
            snprintf(name, sizeof name, "  reverse: quiet first, loud last (RMS %.4f -> %.4f)", (double)e0, (double)e1);
            check(name, e1 > e0 * 4.0f && !nan_seen);
            par[TP_DRUM(0, DP_K4)] = 0;
        }
        par[TP_DRUM(0, DP_K1)] = (uint8_t)smp_factory();  /* the first user slot: empty here */
        h = one_hit(0, 127, 0, 0, 0, 0);
        check("  an empty user slot is silent, the voice ends at once", h.peak == 0.0f && h.tend >= 0.0f && h.tend < 0.01f);
        {   /* a user sample at its own rate: 0.4 s of noise-free tone at 11025 and at 32000 Hz lasts 0.4 s */
            static const int RT[2] = {11025, 32000};
            int k, ok = 1;
            for (k = 0; k < 2; k++) {
                bank_reset();
                bank_add("tone", gen_tone, RT[k] * 2 / 5, 60, 0, RT[k]);
                bank_seal();
                bank_attach();
                smp_user_scan();
                par[TP_DRUM(0, DP_K1)] = (uint8_t)smp_factory();
                h = one_hit(0, 127, 0, 0, 0, 0);
                if (fm_fabsf(h.tend - 0.4f) > 0.01f || h.peak < 0.05f) {
                    printf("  %d Hz: ends at %.3f s (0.400), peak %.2f\n", RT[k], (double)h.tend, (double)h.peak);
                    ok = 0;
                }
            }
            check("  a user sample at 11025 / 32000 Hz plays its own length", ok);
            bank_detach();
            par[TP_DRUM(0, DP_K1)] = (uint8_t)smp_factory();
        }
        {   /* every factory sample, forward and back, pitched to the ends: finite, bounded */
            int ok = 1, sm, rv, pt;
            for (sm = 0; sm < smp_factory(); sm++)
                for (rv = 0; rv < 2; rv++)
                    for (pt = 40; pt <= 88; pt += 48) {
                        par[TP_DRUM(0, DP_K1)] = (uint8_t)sm;
                        par[TP_DRUM(0, DP_K4)] = (uint8_t)rv;
                        par[TP_DRUM(0, DP_K2)] = (uint8_t)pt;
                        nan_seen = 0;
                        h = one_hit(0, 127, 0, 0, 0, 0);
                        if (nan_seen || h.peak > 1.0f || h.peak < 0.01f || h.tend < 0.0f) {
                            printf("  sample %s reverse %d pitch %d: peak %.2f ends %.2f nan %d\n", smp_name(sm), rv, pt,
                                   (double)h.peak, (double)h.tend, nan_seen);
                            ok = 0;
                        }
                    }
            check("  every factory sample forward and reversed at -24 / +24: finite, in range, ends", ok);
        }
        kit_default();
    }

    printf("-- the dynamic filter\n");
    {
        int b;
        float pk_dry, pk_f, pk_lo, pk_hi, e_lo, e_hi;
        /* bypass: any other type leaves the sound as it was */
        h = one_hit(0, 127, 0, 0, 0, 0);
        pk_dry = h.peak;
        drumline_init(dl, par);
        drumline_filter(dl, FT_LP24, 127, 127, 127, 0);
        drumline_hit(dl, 0, 127, 0, 0, 0);
        pk_f = 0.0f;
        for (b = 0; b < 300; b++) { render(); pk_f = fm_maxf(pk_f, blk_peak()); }
        check("type lp24 with every knob up: bypass, the kick unchanged", pk_f == pk_dry);
        /* neutral settings: close to dry */
        drumline_init(dl, par);
        drumline_filter(dl, FT_DYNAMIC, 0, 0, 0, 127);
        drumline_hit(dl, 0, 127, 0, 0, 0);
        pk_f = 0.0f;
        for (b = 0; b < 300; b++) { render(); pk_f = fm_maxf(pk_f, blk_peak()); }
        check("dynamic at low cut 0, comp 0, drive 0, hi cut 127: within 1 dB of dry", fm_fabsf(dbfs(pk_f / pk_dry)) < 1.0f);
        /* the low cut at 2 kHz takes the kick down; the hi cut at 300 Hz takes the closed hat down */
        drumline_init(dl, par);
        drumline_filter(dl, FT_DYNAMIC, 127, 0, 0, 127);
        drumline_hit(dl, 0, 127, 0, 0, 0);
        pk_lo = 0.0f;
        for (b = 0; b < 300; b++) { render(); pk_lo = fm_maxf(pk_lo, blk_peak()); }
        e_lo = 0.0f;
        drumline_init(dl, par);
        drumline_filter(dl, FT_DYNAMIC, 0, 0, 0, 0);
        drumline_hit(dl, 2, 127, 0, 0, 0);
        pk_hi = 0.0f;
        for (b = 0; b < 100; b++) { render(); pk_hi = fm_maxf(pk_hi, blk_peak()); }
        h2 = one_hit(2, 127, 0, 0, 0, 0);
        snprintf(name, sizeof name, "  low cut 127 on the kick %.1f dB, hi cut 0 on the hat %.1f dB", (double)dbfs(pk_lo / pk_dry),
                 (double)dbfs(pk_hi / h2.peak));
        check(name, dbfs(pk_lo / pk_dry) < -12.0f && dbfs(pk_hi / h2.peak) < -12.0f);
        (void)e_lo; (void)e_hi;
        /* compression evens out: a velocity 40 kick and a 127 kick end up closer than 3:1 */
        drumline_init(dl, par);
        drumline_filter(dl, FT_DYNAMIC, 0, 127, 0, 127);
        drumline_hit(dl, 0, 127, 0, 0, 0);
        pk_f = 0.0f;
        for (b = 0; b < 300; b++) { render(); pk_f = fm_maxf(pk_f, blk_peak()); }
        drumline_hit(dl, 0, 40, 0, 0, 0);
        pk_lo = 0.0f;
        for (b = 0; b < 300; b++) { render(); pk_lo = fm_maxf(pk_lo, blk_peak()); }
        h = one_hit(0, 40, 0, 0, 0, 0);
        snprintf(name, sizeof name, "  comp 127: kick 127 / 40 = %.1f dB apart (dry %.1f dB), output under 0 dBFS (%.2f)",
                 (double)dbfs(pk_f / pk_lo), (double)dbfs(pk_dry / h.peak), (double)pk_f);
        check(name, dbfs(pk_f / pk_lo) < dbfs(pk_dry / h.peak) - 3.0f && pk_f <= 1.0f && !nan_seen);
        /* drive: everything at once, eight voices, drive 127 and +20 dB on every key, stays under 1.0 */
        for (s = 0; s < DL_NSOUND; s++)
            par[TP_DRUM(s, DP_VOLUME)] = 127;
        drumline_init(dl, par);
        drumline_filter(dl, FT_DYNAMIC, 0, 127, 127, 127);
        pk_f = 0.0f;
        for (b = 0; b < 400; b++) {
            if (b % 20 == 0)
                for (s = 0; s < DL_NSOUND; s++)
                    drumline_hit(dl, s, 127, 0, 0, 0);
            render();
            pk_f = fm_maxf(pk_f, blk_peak());
        }
        snprintf(name, sizeof name, "  drive 127 + comp 127, eleven keys at +20 dB: peak %.2f, bounded by 1", (double)pk_f);
        check(name, pk_f <= 1.0f && pk_f > 0.3f && !nan_seen);
        for (s = 0; s < DL_NSOUND; s++)
            par[TP_DRUM(s, DP_VOLUME)] = VOL0;
    }

    printf("-- voices\n");
    {
        float lpk;
        int b;
        /* a retrigger uses the sound's own voice: the kick twice is one kick's level, not two */
        drumline_init(dl, par);
        drumline_hit(dl, 0, 127, 0, 0, 0);
        for (b = 0; b < 4; b++)
            render();
        drumline_hit(dl, 0, 127, 0, 0, 0);
        lpk = 0.0f;
        for (b = 0; b < 40; b++) {
            render();
            lpk = fm_maxf(lpk, blk_peak());
        }
        h = one_hit(0, 127, 0, 0, 0, 0);
        check("a sound retriggers its own voice (one kick's level)", lpk <= h.peak * 1.05f);

        /* all eleven at once, repeatedly: finite and bounded; the eighth takes the oldest voice */
        drumline_init(dl, par);
        lpk = 0.0f;
        nan_seen = 0;
        for (b = 0; b < 400; b++) {
            if (b % 20 == 0)
                for (s = 0; s < DL_NSOUND; s++)
                    drumline_hit(dl, s, 127, 0, 0, 0);
            render();
            for (i = 0; i < BLK; i++)
                lpk = fm_maxf(lpk, fm_fabsf(L[i]) + fm_fabsf(S1[i]));
        }
        printf("  eleven at once: peak %.2f\n", (double)lpk);
        check("eleven sounds hit together, 20 times: finite, under eight voices' worth", !nan_seen && lpk < 8.0f);
        drumline_choke(dl);
        for (b = 0; b < 40; b++)
            render();
        check("drumline_choke silences everything within 60 ms", !drumline_sounding(dl));
        check("drumline_hitv shows the last hit", drumline_hitv[0] == 255);
        /* two string keys share the two delay lines; a third takes the older's */
        for (s = 0; s < 3; s++)
            key_model(s, DM_RESO);
        drumline_init(dl, par);
        for (s = 0; s < 3; s++) {
            drumline_hit(dl, s, 127, s * 5, 0, 0);
            for (b = 0; b < 10; b++)
                render();
        }
        lpk = (float)drumline_sounding(dl);
        for (b = 0; b < 200; b++)
            render();
        check("three strings: finite, the third steals a delay line", !nan_seen && lpk > 0.0f);
        kit_default();
    }

    printf("-- cost, instructions a sample (each model alone, then eight voices, then with the filter)\n");
    {
        static float l[256], r[256], a[256], b2[256];
        uint64_t i0, di;
        {
            int drumline_sizeof(void);
            printf("  drumline_t: %d of %d bytes\n", drumline_sizeof(), DRUMLINE_BYTES);
        }
        for (m = 0; m < NDM; m++) {
            key_model(0, m);
            if (m != DM_SAMPLE)
                par[TP_DRUM(0, DP_K2)] = 127;             /* (the sample's pitch stays at its own rate) */
            drumline_init(dl, par);
            drumline_hit(dl, 0, 127, 0, 0, 0);
            memset(l, 0, sizeof l);
            drumline_render(dl, l, r, a, b2, 256);
            i0 = instr_now();
            drumline_render(dl, l, r, a, b2, 256);
            drumline_render(dl, l, r, a, b2, 256);
            di = instr_now() - i0;
            printf("  %-10s %5.0f\n", DM_NAME[m], (double)di / 512.0);
        }
        kit_default();
        for (s = 0; s < DL_NSOUND; s++)
            par[TP_DRUM(s, DP_K2)] = 127;
        for (k = 0; k < 2; k++) {
            uint64_t total = 0, worst = 0;
            int blk;
            drumline_init(dl, par);
            if (k)
                drumline_filter(dl, FT_DYNAMIC, 64, 64, 64, 64);
            for (s = 0; s < 8; s++)
                drumline_hit(dl, s + 3, 127, 0, 0, 0);      /* clap .. fm, the open hat too */
            for (blk = 0; blk < 60; blk++) {
                memset(l, 0, sizeof l);
                i0 = instr_now();
                drumline_render(dl, l, r, a, b2, 256);
                di = instr_now() - i0;
                if (blk > 4) {
                    total += di;
                    if (di > worst)
                        worst = di;
                }
            }
            printf("  8 voices%s:  %5.0f mean, %5.0f worst block\n", k ? " + dynamic filter" : "",
                   (double)total / (55.0 * 256.0), (double)worst / 256.0);
            check(k ? "eight voices with the dynamic filter under 780 instructions a sample (the filter ~120)"
                    : "eight voices under 660 instructions a sample (80 a voice, the key stages neutral)",
                  total / (55 * 256) < (k ? 780u : 660u));
        }
        /* the key stages: every key through its filter (resonant, on the envelope), its drive, its decay; then
         * eight sample voices (the kick at its rate, and two octaves up) */
        for (k = 0; k < 4; k++) {
            uint64_t total = 0, worst = 0;
            int blk;
            static const char *const WHAT[4] = {"8 voices, filter + env", "8 voices, filter + env + drive",
                                                "8 sample hits, 4 sound (at their rate)", "8 sample hits, 4 sound (+24 semitones)"};
            kit_default();
            for (s = 0; s < DL_NSOUND; s++) {
                par[TP_DRUM(s, DP_K2)] = 127;
                if (k < 2) {
                    par[TP_DRUM(s, DP_FTYPE)] = DF_LP;
                    par[TP_DRUM(s, DP_CUT)] = 70;
                    par[TP_DRUM(s, DP_RES)] = 80;
                    par[TP_DRUM(s, DP_FENV)] = 100;
                    par[TP_DRUM(s, DP_DECAY)] = 110;
                    par[TP_DRUM(s, DP_DRIVE)] = (uint8_t)(k == 1 ? 90 : 0);
                } else {
                    key_model(s, DM_SAMPLE);
                    par[TP_DRUM(s, DP_K1)] = 5;           /* the piano: the longest */
                    par[TP_DRUM(s, DP_K2)] = (uint8_t)(k == 3 ? 88 : 64);
                }
            }
            drumline_init(dl, par);
            for (s = 0; s < 8; s++)
                drumline_hit(dl, s + 3, 127, 0, 0, 0);
            for (blk = 0; blk < 60; blk++) {
                memset(l, 0, sizeof l);
                i0 = instr_now();
                drumline_render(dl, l, r, a, b2, 256);
                di = instr_now() - i0;
                if (blk > 4) {
                    total += di;
                    if (di > worst)
                        worst = di;
                }
            }
            printf("  %-32s %5.0f mean, %5.0f worst block%s\n", WHAT[k], (double)total / (55.0 * 256.0),
                   (double)worst / 256.0, drumline_sounding(dl) ? "" : " (all ended)");
            if (k == 1)
                check("eight voices through filter, envelope and drive under 1000 instructions a sample",
                      total / (55 * 256) < 1000u);
            if (k == 2)
                check("sample voices (four at once: the caches) under 760 instructions a sample", total / (55 * 256) < 760u);
        }
        kit_default();
    }
    printf("%s\n", bad ? "DRUMLINE TESTS FAILED" : "drumline tests passed");
    return bad != 0;
}
