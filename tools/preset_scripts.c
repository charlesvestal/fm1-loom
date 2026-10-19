/* SPDX-License-Identifier: GPL-3.0-only */
/* The factory presets for tools/render_presets.sh: for each preset a script for app/build/host/loom_host that
 * loads it onto track 2 of a new project (the default FX I delay and FX II reverb) and plays a short phrase: three notes, then a four-note chord held, then its
 * tail; basses an octave down. And the same phrase through the voices alone (dsp/synth.c, dry, the
 * track's level at 100), its peak, RMS and loudest 100 ms in dBFS into OUTDIR/dry.txt: the levels to balance the presets
 * by, before the master's compressor and limiter even them out; the dry phrase itself (mono, 16 bit) into
 * OUTDIR/dry/NN-engine-name.wav for tools/preset_spectra.py and tools/preset_distance.py. Also checks every sample is
 * finite. With --roles the scripts play each preset as its role is used instead (synth.h RL_*: a bass a riff an
 * octave down, a lead a line with its slides, a pad, strings and voices held chords, keys, stabs and chords a
 * rhythm of chords, plucks and bells an arpeggio, brass a swell and stabs, fx long notes, drums a beat, a loop its
 * slices in order and then cut up): to listen to. The levels and the dry phrases stay the common phrase's, the
 * same for every preset (the comparisons need one phrase).
 *   preset_scripts OUTDIR [--roles]   writes OUTDIR/NN-engine-name.xy, OUTDIR/dry.txt and OUTDIR/dry/, prints the names */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <sys/stat.h>
#include "synth.h"

#define N 256
static uint8_t par[NTRK][NTP];
static _Alignas(16) uint8_t mem[SYNTH_BYTES];
static float L[N], R[N], S1[N], S2[N];
static double acc, peak, win[17], loud;
static long frames, bad, nblk;
#define DRY_MAX (44100 * 4)
static float dry_buf[DRY_MAX];                   /* the dry phrase, mono */
static long dry_n;

static int is_bass(int k) { return PRESETS[k].role == RL_BASS; }

/* the role phrases (--roles): notes as key, start and length in ms (keys 0..26 = F3..G5; a bass sounds an octave
 * down by its own octave, TP_OCT) */
typedef struct { uint8_t key; uint16_t at, len; } ev_t;
#define C1(t, l) {7, t, l}, {11, t, l}, {14, t, l}, {18, t, l}      /* C E G B */
#define C2(t, l) {5, t, l}, {9, t, l}, {12, t, l}, {16, t, l}       /* Bb D F A */
#define C3(t, l) {0, t, l}, {4, t, l}, {7, t, l}, {11, t, l}        /* F A C E */
static const ev_t PH_BASS[] = {{7, 0, 210}, {7, 250, 90}, {19, 375, 100}, {7, 500, 210}, {10, 750, 100}, {12, 875, 100},
                               {14, 1000, 280}, {12, 1250, 280}, {7, 1500, 210}, {5, 1750, 220}, {7, 2000, 700}};
static const ev_t PH_LEAD[] = {{14, 0, 430}, {17, 400, 430}, {19, 800, 640}, {21, 1400, 310}, {19, 1680, 310},
                               {17, 1960, 700}, {14, 2640, 1000}};
static const ev_t PH_PAD[] = {C1(0, 2500), C3(2500, 1800)};
static const ev_t PH_KEYS[] = {C1(0, 220), C1(375, 160), C1(750, 220), C2(1125, 160), C2(1500, 620), C1(2250, 1000)};
static const ev_t PH_ARP[] = {{7, 0, 140}, {11, 150, 140}, {14, 300, 140}, {18, 450, 140}, {19, 600, 140},
                              {18, 750, 140}, {14, 900, 140}, {11, 1050, 140}, {5, 1200, 140}, {9, 1350, 140},
                              {12, 1500, 140}, {16, 1650, 140}, {19, 1800, 140}, {16, 1950, 140}, {12, 2100, 140},
                              {7, 2250, 900}};
static const ev_t PH_BRASS[] = {{7, 0, 1100}, {11, 0, 1100}, {14, 0, 1100}, {7, 1250, 180}, {11, 1250, 180},
                                {14, 1250, 180}, {5, 1500, 1300}, {9, 1500, 1300}, {12, 1500, 1300}};
static const ev_t PH_FX[] = {{7, 0, 1700}, {19, 1900, 1200}};
static const ev_t PH_DRUM[] = {{7, 0, 100}, {14, 250, 100}, {7, 375, 100}, {10, 500, 100}, {7, 750, 100}, {19, 875, 100},
                               {7, 1000, 100}, {14, 1250, 100}, {12, 1375, 100}, {7, 1500, 100}, {7, 1750, 100},
                               {19, 1875, 100}, {7, 2000, 400}};
static const ev_t PH_LOOP[] = {{0, 0, 240}, {1, 250, 240}, {2, 500, 240}, {3, 750, 240}, {4, 1000, 240}, {5, 1250, 240},
                               {6, 1500, 240}, {7, 1750, 240}, {0, 2000, 240}, {2, 2250, 120}, {2, 2375, 120},
                               {0, 2500, 240}, {5, 2750, 240}, {4, 3000, 120}, {4, 3125, 120}, {6, 3250, 240},
                               {7, 3500, 240}};
static void phrase(FILE *f, int role)
{
    const ev_t *e;
    int n, t = 0, i;
    struct { int t, key, down; } x[80];
    int nx = 0;
#define PH(a) (e = a, n = (int)(sizeof a / sizeof a[0]))
    switch (role) {
    case RL_BASS: PH(PH_BASS); break;
    case RL_LEAD: PH(PH_LEAD); break;
    case RL_PAD: case RL_STRINGS: case RL_VOICE: PH(PH_PAD); break;
    case RL_KEYS: case RL_STAB: case RL_CHORD: PH(PH_KEYS); break;
    case RL_PLUCK: case RL_BELL: PH(PH_ARP); break;
    case RL_BRASS: PH(PH_BRASS); break;
    case RL_DRUM: PH(PH_DRUM); break;
    case RL_LOOP: PH(PH_LOOP); break;
    default: PH(PH_FX); break;
    }
#undef PH
    for (i = 0; i < n; i++) {                    /* the events in time order: a note's off before another's on */
        x[nx].t = e[i].at, x[nx].key = e[i].key, x[nx++].down = 1;
        x[nx].t = e[i].at + e[i].len, x[nx].key = e[i].key, x[nx++].down = 0;
    }
    for (i = 1; i < nx; i++) {
        int j = i;
        while (j > 0 && (x[j - 1].t > x[j].t || (x[j - 1].t == x[j].t && x[j - 1].down > x[j].down))) {
            int tt = x[j].t, kk = x[j].key, dd = x[j].down;
            x[j] = x[j - 1];
            x[j - 1].t = tt, x[j - 1].key = kk, x[j - 1].down = dd;
            j--;
        }
    }
    for (i = 0; i < nx; i++) {
        if (x[i].t > t)
            fprintf(f, "wait %d\n", x[i].t - t);
        t = x[i].t;
        fprintf(f, "key %d %s\n", x[i].key, x[i].down ? "down" : "up");
    }
    fprintf(f, "wait 1500\nwavstop\n");
}

static void run(synth_t *s, int ms, int measure)
{
    long n = (long)ms * 441 / 10, i;
    for (; n > 0; n -= N) {
        memset(L, 0, sizeof L);
        memset(R, 0, sizeof R);
        memset(S1, 0, sizeof S1);
        memset(S2, 0, sizeof S2);
        synth_prepare(s, N);
        synth_render(s, 0, NVOICE, L, R, S1, S2, N);
        win[nblk++ % 17] = 0.0;
        for (i = 0; i < N; i++) {
            if (dry_n < DRY_MAX)
                dry_buf[dry_n++] = (L[i] + R[i]) * 0.5f;
            win[(nblk - 1) % 17] += (double)L[i] * L[i] + (double)R[i] * R[i];
            if (!(L[i] == L[i]) || !(R[i] == R[i]) || fabs((double)L[i]) > 1e6 || fabs((double)R[i]) > 1e6)
                bad++;
            if (fabs((double)L[i]) > peak)
                peak = fabs((double)L[i]);
            if (fabs((double)R[i]) > peak)
                peak = fabs((double)R[i]);
            if (measure) {
                acc += (double)L[i] * L[i] + (double)R[i] * R[i];
                frames++;
            }
        }
        if (nblk >= 17) {                        /* the loudest 100 ms (17 blocks) */
            double e = 0.0;
            for (i = 0; i < 17; i++)
                e += win[i];
            if (e > loud)
                loud = e;
        }
    }
}

/* the phrase: C E G, then C E G B held (the scripts' keys 7 11 14 18 from F3) */
static void dry(int k, double *pk, double *rms, double *ld)
{
    static const int NOTE[4] = {60, 64, 67, 71};
    synth_t *s = (synth_t *)mem;
    int t, j, o = is_bass(k) ? -12 : 0;
    for (t = 0; t < NTRK; t++)
        sound_default(par[t], EG_KNIT);
    memcpy(par[1], PRESETS[k].par, NTP_SOUND);
    synth_init(s, par);
    synth_tempo(s, 120.0f);
    acc = peak = loud = 0.0;
    frames = nblk = dry_n = 0;
    for (j = 0; j < 3; j++) {
        synth_note_on(s, 1, NOTE[j] + o, 100, 0, (uint32_t)j);
        run(s, 260, 1);
        synth_note_off(s, 1, (uint32_t)j);
        run(s, 40, 1);
    }
    for (j = 0; j < 4; j++)
        synth_note_on(s, 1, NOTE[j] + o, 100, 0, (uint32_t)(10 + j));
    run(s, 1300, 1);
    for (j = 0; j < 4; j++)
        synth_note_off(s, 1, (uint32_t)(10 + j));
    run(s, 1000, 0);
    *pk = peak > 0.0 ? 20.0 * log10(peak) : -200.0;
    *rms = acc > 0.0 ? 10.0 * log10(acc / (2.0 * (double)frames)) : -200.0;
    *ld = loud > 0.0 ? 10.0 * log10(loud / (2.0 * 17.0 * N)) : -200.0;
}

static void put_le(FILE *f, uint32_t v, int n)
{
    for (; n > 0; n--, v >>= 8)
        fputc((int)(v & 255u), f);
}
static int dry_wav(const char *path)             /* dry_buf as a 44.1 kHz mono 16-bit WAV */
{
    FILE *f = fopen(path, "wb");
    long i;
    if (!f)
        return 1;
    fwrite("RIFF", 1, 4, f);
    put_le(f, (uint32_t)(36 + dry_n * 2), 4);
    fwrite("WAVEfmt ", 1, 8, f);
    put_le(f, 16, 4);
    put_le(f, 1, 2);
    put_le(f, 1, 2);
    put_le(f, 44100, 4);
    put_le(f, 88200, 4);
    put_le(f, 2, 2);
    put_le(f, 16, 2);
    fwrite("data", 1, 4, f);
    put_le(f, (uint32_t)(dry_n * 2), 4);
    for (i = 0; i < dry_n; i++) {
        float x = dry_buf[i] * 32767.0f;
        int v = x > 32767.0f ? 32767 : x < -32768.0f ? -32768 : (int)x;
        put_le(f, (uint32_t)(uint16_t)(int16_t)v, 2);
    }
    fclose(f);
    return 0;
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "build/presets";
    int roles = argc > 2 && !strcmp(argv[2], "--roles");
    char path[256];
    FILE *lv;
    int k, p;
    snprintf(path, sizeof path, "%s/dry.txt", dir);
    lv = fopen(path, "w");
    if (!lv)
        return 1;
    snprintf(path, sizeof path, "%s/dry", dir);
    mkdir(path, 0755);
    for (k = 0; k < NPRESETS; k++) {
        const preset_t *pr = &PRESETS[k];
        char base[96], *c;
        double pk, rms, ld;
        FILE *f;
        snprintf(base, sizeof base, "%02d-%s-%s", k + 1, EG_NAME[pr->par[TP_ENGINE]], pr->name);
        for (c = base; *c; c++)
            if (*c == ' ')
                *c = '_';
        snprintf(path, sizeof path, "%s/%s.xy", dir, base);
        f = fopen(path, "w");
        if (!f)
            return 1;
        fprintf(f, "# preset %d: %s\nwait 200\nspin ALGO 1\nexpect trk 2\ntap EDIT\n", k + 1, pr->name);
        fprintf(f, "param 2 %d %d\n", TP_ENGINE, pr->par[TP_ENGINE]);
        fprintf(f, "param 2 %d %d\n", TP_LTYPE, pr->par[TP_LTYPE]);
        for (p = 0; p < NTP_SOUND; p++)
            if (p != TP_ENGINE && p != TP_LTYPE)
                fprintf(f, "param 2 %d %d\n", p, pr->par[p]);
        fprintf(f, "wait 100\nwav %s.wav\n", base);
        if (roles)
            phrase(f, pr->role);
        else
            fprintf(f, "key 7 down\nwait 260\nkey 7 up\nwait 40\nkey 11 down\nwait 260\nkey 11 up\nwait 40\n"
                   "key 14 down\nwait 260\nkey 14 up\nwait 40\n"
                   "key 7 down\nkey 11 down\nkey 14 down\nkey 18 down\nwait 1300\n"
                   "key 7 up\nkey 11 up\nkey 14 up\nkey 18 up\nwait 1000\nwavstop\n");
        fclose(f);
        dry(k, &pk, &rms, &ld);
        snprintf(path, sizeof path, "%s/dry/%s.wav", dir, base);
        if (dry_wav(path))
            return 1;
        fprintf(lv, "%s %.2f %.2f %.2f\n", base, pk, rms, ld);
        printf("%s\n", base);
    }
    fclose(lv);
    if (bad) {
        fprintf(stderr, "preset_scripts: %ld samples not finite\n", bad);
        return 1;
    }
    return 0;
}
