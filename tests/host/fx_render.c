/* SPDX-License-Identifier: MPL-2.0 */
/* Renders for listening (nothing here plays audio): every reverb mode (the plate, the PlayStation's SPU modes) on a short dry phrase, as
 * 16-bit stereo WAVs in build/fx_renders/, with an index (index.txt) of what each file is.
 *   tools/fx_renders.sh */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "fx.h"
#include "fastmath.h"

#define SR 44100
#define BLK 64
#define LEN (6 * SR)
#define DIR "build/fx_renders/"

static int16_t dly_l[FX_DLY_LEN], dly_r[FX_DLY_LEN], rv[FX_RV_LEN];
static uint8_t fxb[FXSLOT_BYTES] __attribute__((aligned(8)));
static fxslot_t *fx = (fxslot_t *)fxb;
static float src_l[LEN], src_r[LEN], out_l[LEN], out_r[LEN];
static FILE *idx;

static void wav(const char *name, const float *l, const float *r, int n, const char *what)
{
    static const uint8_t hdr[44] = {'R','I','F','F',0,0,0,0,'W','A','V','E','f','m','t',' ',16,0,0,0,1,0,2,0,
        0x44,0xAC,0,0,0x10,0xB1,2,0,4,0,16,0,'d','a','t','a',0,0,0,0};
    char path[160];
    FILE *f;
    uint32_t v;
    int i;
    snprintf(path, sizeof path, DIR "%s", name);
    f = fopen(path, "wb");
    if (!f)
        return;
    fwrite(hdr, 1, 44, f);
    for (i = 0; i < n; i++) {
        int16_t s[2];
        s[0] = (int16_t)fm_clampf(l[i] * 32767.0f, -32767.0f, 32767.0f);
        s[1] = (int16_t)fm_clampf(r[i] * 32767.0f, -32767.0f, 32767.0f);
        fwrite(s, 2, 2, f);
    }
    v = (uint32_t)n * 4u + 36u;
    fseek(f, 4, SEEK_SET);
    fwrite(&v, 4, 1, f);
    v = (uint32_t)n * 4u;
    fseek(f, 40, SEEK_SET);
    fwrite(&v, 4, 1, f);
    fclose(f);
    fprintf(idx, "%-34s %s\n", name, what);
}

/* a dry phrase: eight plucked notes (a saw, a decaying low-pass) in 1.5 s, then silence for the tail (mono) */
static void phrase(void)
{
    static const float F[8] = {261.63f, 329.63f, 392.0f, 523.25f, 493.88f, 392.0f, 329.63f, 293.66f};
    float ph = 0.0f, lp = 0.0f;
    int i;
    for (i = 0; i < LEN; i++) {
        int k = i / (SR * 3 / 16);
        float t = (float)(i % (SR * 3 / 16)) / SR, s = 0.0f;
        if (k < 8) {
            ph += F[k] / SR;
            ph -= fm_floorf(ph);
            lp += (0.05f + 0.4f * fm_expf(-t * 20.0f)) * ((2.0f * ph - 1.0f) - lp);
            s = 0.4f * lp * fm_expf(-t * 7.0f);
        }
        src_l[i] = src_r[i] = s;
    }
}


/* the phrase dry, the reverb's return under it (the send at full, the return at 0.7: an FX track at level 84) */
static void reverb_render(const char *name, int pick, const uint8_t kn[4], const char *what)
{
    static float in[BLK], ol[BLK], orr[BLK];
    int b, i;
    fx_init(fx, dly_l, dly_r, rv);
    fx_tempo(fx, 120.0f);
    fx_set(fx, pick, kn);
    fx_filter(fx, 20, 127);                                     /* the default project's FX II filter */
    for (b = 0; b < 40; b++) {                                  /* an SPU mode clears its area first */
        memset(in, 0, sizeof in);
        fx_process(fx, in, ol, orr, BLK);
    }
    for (b = 0; b < LEN / BLK; b++) {
        for (i = 0; i < BLK; i++)
            in[i] = src_l[b * BLK + i];
        memset(ol, 0, sizeof ol);
        memset(orr, 0, sizeof orr);
        fx_process(fx, in, ol, orr, BLK);
        for (i = 0; i < BLK; i++) {
            out_l[b * BLK + i] = in[i] + 0.7f * ol[i];
            out_r[b * BLK + i] = in[i] + 0.7f * orr[i];
        }
    }
    wav(name, out_l, out_r, LEN, what);
}

int main(void)
{
    char name[64], what[160];
    int i, m;
    idx = fopen(DIR "index.txt", "w");
    if (!idx) {
        fprintf(stderr, "fx_render: no " DIR "\n");
        return 1;
    }
    fprintf(idx, "LOOM-8 effect renders (tests/host/fx_render.c, tools/fx_renders.sh): 44.1 kHz, 16-bit stereo, 6 s each.\n");
    fprintf(idx, "\nThe reverb's modes on FX II, on a dry phrase (8 plucked notes in 1.5 s, then the tail): the dry, and the\n"
                 "return at 0.7 under it. The SPU modes at their defaults (size 127 = the console's delays, decay 64 = its own\n"
                 "feedback, tone flat, damp 0):\n");
    phrase();
    wav("reverb_dry.wav", src_l, src_r, LEN, "the phrase alone");
    for (m = 0; m < NRV; m++) {
        int pick = FXT_REVERB + m;
        snprintf(name, sizeof name, "reverb_%02d_%s.wav", m, fx_pick_name(pick));
        for (i = 0; name[i]; i++)
            if (name[i] == ' ')
                name[i] = '_';
        snprintf(what, sizeof what, "%s%s", fx_pick_name(pick), m ? "" : " (the plate, its defaults 100 40 44 72)");
        reverb_render(name, pick, m ? RV_SPU_DEFAULT : FXT_DEFAULT[FXT_REVERB], what);
    }
    {
        static const uint8_t k0[4] = {0, 64, 64, 0}, kd[4] = {127, 110, 64, 0}, kdark[4] = {127, 64, 30, 70};
        reverb_render("reverb_hall_size0.wav", FXT_REVERB + RV_HALL, k0, "hall at size 0: schwung-psxverb's scale (x1)");
        reverb_render("reverb_hall_decay110.wav", FXT_REVERB + RV_HALL, kd, "hall, decay 110: longer");
        reverb_render("reverb_space_echo_dark.wav", FXT_REVERB + RV_SPACE_ECHO, kdark, "space echo, tone 30, damp 70: darker");
    }
    fclose(idx);
    printf("renders in " DIR " (index.txt)\n");
    return 0;
}
