/* SPDX-License-Identifier: MPL-2.0 */
/* The Mix page's audio mutes in the DSP (engine.h engine_amix; synth.h perf_t mute bits, drumline_amute): a synth
 * track (a held, mellow knit note, its sends up) on its voices alone and on its bus (a slot on), set from on to muted
 * (bit 0: its dry and its sends silent after the fade), to wet only (bit 1: its dry silent, its sends as they were)
 * and back on (its dry back, as it was); a kit's key muted (drumline_amute: that key's output and sends silent, the
 * other key's as if it played alone). Each change without a click: the high-passed energy (above 8 kHz, 24 dB an
 * octave) in 2 ms windows from the change to 40 ms after it, over the largest of the same windows in the render that
 * never changed (+-150 ms), no more than SLEW_DB (6 dB), as tests/host/slew_test.c measures a knob's turn. A solo is
 * the same mute on the others (ui.c mix_send: its scenario, tests/scenarios/mixmute.xy).
 *   tests/host/mixmute_test.sh */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "synth.h"
#include "efx.h"
#include "drumline.h"
#include "fastmath.h"

#define N 256                                  /* the engine's block */
#define NB 120
#define TB 70                                  /* the change's block (0.41 s) */
#define LEN (NB * N)
#define T0 (TB * N)
#define W 88                                   /* 2 ms */
#define HOP 22
#define AFTER (int)(0.040f * 44100.0f)
#define AROUND (int)(0.150f * 44100.0f)
#define SETTLE (int)(0.030f * 44100.0f)        /* the fade's end, well past (~5 ms) */
#define SLEW_DB 6.0f

static int bad;
static void check(const char *what, int ok)
{
    printf("%-84s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

static uint8_t par[NTRK + 1][NTP];
static _Alignas(16) uint8_t smem[SYNTH_BYTES];
static synth_t *S = (synth_t *)smem;
static uint8_t dbuf[DRUMLINE_BYTES] __attribute__((aligned(8)));
static drumline_t *DL = (drumline_t *)dbuf;
enum { O_L, O_R, O_S1, O_S2, NO };
static float out[3][NO][LEN];                  /* the change's render, the never-changed one, the after's */

/* a synth track: before from the start, after from block TB (perf_t mute bits) */
static void render_synth(int slot, int before, int after, float (*o)[LEN])
{
    static float L[N], R[N], S1[N], S2[N];
    for (int t = 0; t <= NTRK; t++)
        memset(par[t], 0, NTP);
    for (int t = 0; t < NTRK; t++)
        sound_default(par[t], EG_KNIT);
    par[0][TP_A] = 0;
    par[0][TP_S] = 127;
    par[0][TP_CUT] = 50;                       /* (a mellow note: a click is not hidden in its edges) */
    par[0][TP_RES] = 0;
    par[0][TP_SEND1] = 100;
    par[0][TP_SEND2] = 60;
    if (slot) {                                /* (on its bus: a tilt in slot 1) */
        par[0][TP_SLOT[0][SL_EF]] = EF_TILT;
        par[0][TP_SLOT[0][SL_K1]] = 70;
        par[0][TP_SLOT[0][SL_MIX]] = 127;
    }
    memset(efx_pool, 0, sizeof efx_pool);
    efx_send_used = (efx_mask_t){{0}};
    synth_init(S, par);
    synth_tempo(S, 120.0f);
    synth_note_on(S, 0, 57, 100, 0, 1);
    for (int bk = 0; bk < NB; bk++) {
        synth_perf(S, 0)->mute = (uint8_t)(bk < TB ? before : after);
        memset(L, 0, sizeof L);
        memset(R, 0, sizeof R);
        memset(S1, 0, sizeof S1);
        memset(S2, 0, sizeof S2);
        synth_prepare(S, N);
        synth_render(S, 0, NVOICE, L, R, S1, S2, N);
        memcpy(o[O_L] + bk * N, L, sizeof L);
        memcpy(o[O_R] + bk * N, R, sizeof R);
        memcpy(o[O_S1] + bk * N, S1, sizeof S1);
        memcpy(o[O_S2] + bk * N, S2, sizeof S2);
    }
}

/* a kit: keys 1 and 2 (keys) hit just before the change, the keys muted at the mixer from block TB (after) */
static void render_kit(int keys, int after, float (*o)[LEN])
{
    static float L[N], R[N], S1[N], S2[N];
    memset(par[0], 0, NTP);
    for (int s = 0; s < DL_NSOUND; s++)
        for (int k = 0; k < DP_N; k++)
            par[0][TP_DRUM(s, k)] = DL_DEFAULT_KIT[s][k];
    par[0][TP_R] = 64;
    for (int s = 0; s < 2; s++) {              /* (two long, plain models: a kick and a tom) */
        const int m = s ? DM_TOM : DM_KICK;
        par[0][TP_DRUM(s, DP_MODEL)] = (uint8_t)m;
        for (int k = 0; k < 4; k++) {
            par[0][TP_DRUM(s, DP_K1 + k)] = DM_DEFAULT[m][k];
            par[0][TP_DRUMX(s, k)] = DM_DEFAULT2[m][k];
        }
        par[0][TP_DRUM(s, DP_SEND1)] = 64;
        par[0][TP_DRUM(s, DP_SEND2)] = 32;
    }
    drumline_init(DL, par[0]);
    for (int bk = 0; bk < NB; bk++) {
        if (bk == TB - 6)                      /* (35 ms before the change: both ringing) */
            for (int s = 0; s < 2; s++)
                if (keys >> s & 1)
                    drumline_hit(DL, s, 127, 0, 0, 0);
        drumline_amute(DL, (uint32_t)(bk < TB ? 0 : after));
        memset(L, 0, sizeof L);
        memset(R, 0, sizeof R);
        memset(S1, 0, sizeof S1);
        memset(S2, 0, sizeof S2);
        drumline_render(DL, L, R, S1, S2, N);
        memcpy(o[O_L] + bk * N, L, sizeof L);
        memcpy(o[O_R] + bk * N, R, sizeof R);
        memcpy(o[O_S1] + bk * N, S1, sizeof S1);
        memcpy(o[O_S2] + bk * N, S2, sizeof S2);
    }
}

static double rms(const float *x, int a, int b)
{
    double e = 0.0;
    for (int i = a; i < b; i++)
        e += (double)x[i] * (double)x[i];
    return sqrt(e / (double)(b - a));
}
static double db(double x) { return 20.0 * log10(x + 1e-30); }

/* the high-passed energy (8 kHz, fourth order) of the pair (c0, c0 + 1) in each 2 ms window */
static float hp[3][2][LEN];
static void high_pass(int k, int c0)
{
    static const double Q[2] = {0.54119610, 1.30656296};
    const double w = 2.0 * 3.14159265358979 * 8000.0 / 44100.0, cw = cos(w), sw = sin(w);
    for (int c = 0; c < 2; c++) {
        float *y = hp[k][c];
        memcpy(y, out[k][c0 + c], sizeof hp[k][c]);
        for (int q = 0; q < 2; q++) {
            const double al = sw / (2.0 * Q[q]), a0 = 1.0 + al;
            const double b0 = (1.0 + cw) / 2.0 / a0, b1 = -(1.0 + cw) / a0, a1 = -2.0 * cw / a0, a2 = (1.0 - al) / a0;
            double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;
            for (int i = 0; i < LEN; i++) {
                const double v = (double)y[i], o = b0 * v + b1 * x1 + b0 * x2 - a1 * y1 - a2 * y2;
                x2 = x1, x1 = v, y2 = y1, y1 = o;
                y[i] = (float)o;
            }
        }
    }
}
static double win_e(int k, int at)
{
    double e = 0.0;
    for (int c = 0; c < 2; c++)
        for (int i = at; i < at + W; i++)
            e += (double)hp[k][c][i] * (double)hp[k][c][i];
    return e;
}
/* the change's worst window over the never-changed render's largest (and the after's), dB; c0: the dry pair (O_L)
 * or the sends (O_S1) */
static float click_db(int c0)
{
    double worst = 0.0, ref = 0.0, sig = 0.0;
    for (int k = 0; k < 3; k++)
        high_pass(k, c0);
    for (int at = T0 - AROUND; at <= T0 + AROUND; at += HOP)
        for (int k = 1; k < 3; k++) {
            double e = win_e(k, at);
            ref = e > ref ? e : ref;
        }
    for (int c = 0; c < 2; c++)                  /* (a floor: 60 dB under the never-changed signal itself) */
        for (int i = T0 - AROUND; i < T0 + AROUND; i++)
            sig += (double)out[1][c0 + c][i] * (double)out[1][c0 + c][i];
    sig *= (double)W / (double)(2 * AROUND);
    ref = ref > sig * 1e-6 ? ref : sig * 1e-6;
    for (int at = T0 - W / 2; at <= T0 + AFTER; at += HOP) {
        double e = win_e(0, at);
        worst = e > worst ? e : worst;
    }
    return (float)(10.0 * log10(worst / ref + 1e-12));
}

static void synth_case(int slot)
{
    char t[120];
    const char *where = slot ? "on its bus (a slot)" : "on its voices";
    const int a = T0 + SETTLE, b = LEN - N;
    double dry_on, s1_on, s2_on;
    render_synth(slot, 0, 0, out[1]);           /* (never changed: on) */
    dry_on = rms(out[1][O_L], a, b) + rms(out[1][O_R], a, b);
    s1_on = rms(out[1][O_S1], a, b);
    s2_on = rms(out[1][O_S2], a, b);
    snprintf(t, sizeof t, "synth %s: on, its dry and its sends heard", where);
    check(t, dry_on > 1e-3 && s1_on > 1e-4 && s2_on > 1e-4);
    /* muted */
    render_synth(slot, 0, 1, out[0]);
    render_synth(slot, 1, 1, out[2]);
    snprintf(t, sizeof t, "synth %s: muted, the dry %.0f dB, the sends %.0f / %.0f dB", where,
             db((rms(out[0][O_L], a, b) + rms(out[0][O_R], a, b)) / dry_on), db(rms(out[0][O_S1], a, b) / s1_on),
             db(rms(out[0][O_S2], a, b) / s2_on));
    check(t, rms(out[0][O_L], a, b) + rms(out[0][O_R], a, b) < dry_on * 1e-5 && rms(out[0][O_S1], a, b) < s1_on * 1e-5 &&
             rms(out[0][O_S2], a, b) < s2_on * 1e-5);
    snprintf(t, sizeof t, "synth %s: muting, no click (dry %+.1f dB, sends %+.1f dB)", where, click_db(O_L),
             click_db(O_S1));
    check(t, click_db(O_L) <= SLEW_DB && click_db(O_S1) <= SLEW_DB);
    /* wet only */
    render_synth(slot, 0, 2, out[0]);
    render_synth(slot, 2, 2, out[2]);
    snprintf(t, sizeof t, "synth %s: wet only, the dry %.0f dB, the sends %+.2f / %+.2f dB", where,
             db((rms(out[0][O_L], a, b) + rms(out[0][O_R], a, b)) / dry_on), db(rms(out[0][O_S1], a, b) / s1_on),
             db(rms(out[0][O_S2], a, b) / s2_on));
    check(t, rms(out[0][O_L], a, b) + rms(out[0][O_R], a, b) < dry_on * 1e-5 &&
             fabs(db(rms(out[0][O_S1], a, b) / s1_on)) < 0.5 && fabs(db(rms(out[0][O_S2], a, b) / s2_on)) < 0.5);
    snprintf(t, sizeof t, "synth %s: to wet only, no click (dry %+.1f dB, sends %+.1f dB)", where, click_db(O_L),
             click_db(O_S1));
    check(t, click_db(O_L) <= SLEW_DB && click_db(O_S1) <= SLEW_DB);
    /* wet only, then on again */
    render_synth(slot, 2, 0, out[0]);
    render_synth(slot, 0, 0, out[2]);
    snprintf(t, sizeof t, "synth %s: wet only to on, the dry back %+.2f dB", where,
             db((rms(out[0][O_L], a, b) + rms(out[0][O_R], a, b)) / dry_on));
    check(t, fabs(db((rms(out[0][O_L], a, b) + rms(out[0][O_R], a, b)) / dry_on)) < 0.5);
    memcpy(out[1], out[2], sizeof out[1]);      /* (the references: wet only throughout, on throughout) */
    render_synth(slot, 2, 2, out[2]);
    snprintf(t, sizeof t, "synth %s: wet only to on, no click (dry %+.1f dB)", where, click_db(O_L));
    check(t, click_db(O_L) <= SLEW_DB);
    /* muted, then on again */
    render_synth(slot, 1, 0, out[0]);
    render_synth(slot, 0, 0, out[1]);
    render_synth(slot, 1, 1, out[2]);
    snprintf(t, sizeof t, "synth %s: muted to on, no click (dry %+.1f dB, sends %+.1f dB)", where, click_db(O_L),
             click_db(O_S1));
    check(t, click_db(O_L) <= SLEW_DB && click_db(O_S1) <= SLEW_DB);
}

static void kit_case(void)
{
    char t[120];
    const int a = T0 + SETTLE, b = T0 + SETTLE + 2048;
    double d = 0.0, on;
    render_kit(3, 1, out[0]);                    /* key 1 muted at the mixer from TB */
    render_kit(3, 0, out[1]);                    /* (never: both) */
    render_kit(2, 0, out[2]);                    /* (key 2 alone) */
    on = rms(out[1][O_L], a, b);
    for (int c = 0; c < NO; c++)
        for (int i = a; i < b; i++)
            d = fmax(d, fabs((double)out[0][c][i] - (double)out[2][c][i]));
    snprintf(t, sizeof t, "kit: key 1 muted at the mixer, the rest is key 2's alone (off by %.1g, its level %.2g)", d, on);
    check(t, on > 1e-3 && d < 1e-6);
    snprintf(t, sizeof t, "kit: key 1 muted, no click (dry %+.1f dB, sends %+.1f dB)", click_db(O_L), click_db(O_S1));
    check(t, click_db(O_L) <= SLEW_DB && click_db(O_S1) <= SLEW_DB);
    {                                            /* a key hit while muted: silent from its start */
        drumline_init(DL, par[0]);
        drumline_amute(DL, 1);
        drumline_hit(DL, 0, 127, 0, 0, 0);
        static float L[N], R[N], S1[N], S2[N];
        double e = 0.0;
        for (int bk = 0; bk < 20; bk++) {
            memset(L, 0, sizeof L);
            memset(R, 0, sizeof R);
            memset(S1, 0, sizeof S1);
            memset(S2, 0, sizeof S2);
            drumline_render(DL, L, R, S1, S2, N);
            for (int i = 0; i < N; i++)
                e = fmax(e, (double)(fabsf(L[i]) + fabsf(R[i]) + fabsf(S1[i]) + fabsf(S2[i])));
        }
        snprintf(t, sizeof t, "kit: a key hit while muted is silent from its start (peak %.1g)", e);
        check(t, e == 0.0);
    }
}

int main(void)
{
    synth_case(0);
    synth_case(1);
    kit_case();
    printf(bad ? "mixmute: %d FAILED\n" : "mixmute: all ok\n", bad);
    return bad != 0;
}
