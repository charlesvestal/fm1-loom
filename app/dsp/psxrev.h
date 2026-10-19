/* SPDX-License-Identifier: MIT */
/* The PlayStation's SPU reverb, for LOOM-8's reverb modes. Included by fx.c only (static code, no unit of
 * its own).
 *
 * Ported from schwung-psxverb (src/dsp/psxverb.c), Copyright (c) 2025 Charles Vestal, MIT License (the
 * licence text: LICENSING.md). The register sets are psx-spx's "SPU Reverb Examples" (the nine of them;
 * psxverb carries six), the formula psx-spx's "SPU Reverb Formula", as psxverb runs it:
 *
 *   [mLSAME] = (Lin + [dLSAME]*vWALL - [mLSAME-1])*vIIR + [mLSAME-1]       and the same for R, and the
 *   [mLDIFF] = (Lin + [dRDIFF]*vWALL - [mLDIFF-1])*vIIR + [mLDIFF-1]       crossed pair (R-to-L, L-to-R)
 *   Lout = vCOMB1*[mLCOMB1] + vCOMB2*[mLCOMB2] + vCOMB3*[mLCOMB3] + vCOMB4*[mLCOMB4]
 *   Lout -= vAPF1*[mLAPF1-dAPF1]; [mLAPF1] = Lout; Lout = Lout*vAPF1 + [mLAPF1-dAPF1]     (then APF2)
 *
 * at 22.05 kHz on an int16 work area written with saturation, every address relative to a base that moves
 * one sample a tick. The changes for LOOM-8:
 *   - the send is mono: one decimator (Lin = Rin), two interpolators;
 *   - the resampling is the SPU's own 39-tap half-band FIR (psx-spx, "Reverb Buffer Resampling"):
 *     psxverb's table has every side tap negative and its interpolator's second phase all zeros, which
 *     leaves every other output sample silent;
 *   - the addresses scale by "size": x1 is psxverb's (a register = one sample), x4 the console's (the
 *     registers count 8-byte units: 4 samples; psx-spx, DuckStation); the IIR's "-1" stays one sample;
 *   - "decay" is psxverb's: the wall feedback 0.5x .. x1 (the knob's middle) .. 0.99 / |vWALL| (at most
 *     x10), held within +-0.995; "damp" closes vIIR (the reflections' low-pass); "tone" is the plate's
 *     tilt on the return;
 *   - the work area is the FX slot's reverb line (FX_RV_LEN, a power of two: the addresses wrap by mask;
 *     every mode at x4 fits, its largest address 1FFFh x 4 = 32764 < 32768, so no two addresses alias);
 *   - the input and output volumes (vLIN, vRIN 8000h = -1, the output -1) are folded into one gain a
 *     mode (SPU_GAIN), which evens the modes' levels out.
 * A mode change (fx_set) clears the work area from the interrupt, a slice a block, the return silent
 * meanwhile. */

/* psx-spx's register order: dAPF1 dAPF2 vIIR vCOMB1 vCOMB2 vCOMB3 vCOMB4 vWALL | vAPF1 vAPF2 mLSAME mRSAME
 * mLCOMB1 mRCOMB1 mLCOMB2 mRCOMB2 | dLSAME dRSAME mLDIFF mRDIFF mLCOMB3 mRCOMB3 mLCOMB4 mRCOMB4 |
 * dLDIFF dRDIFF mLAPF1 mRAPF1 mLAPF2 mRAPF2 (vLIN vRIN: 8000h in all of them, left out) */
enum { R_DAPF1, R_DAPF2, R_VIIR, R_VCOMB1, R_VCOMB2, R_VCOMB3, R_VCOMB4, R_VWALL, R_VAPF1, R_VAPF2,
       R_MLSAME, R_MRSAME, R_MLCOMB1, R_MRCOMB1, R_MLCOMB2, R_MRCOMB2, R_DLSAME, R_DRSAME, R_MLDIFF, R_MRDIFF,
       R_MLCOMB3, R_MRCOMB3, R_MLCOMB4, R_MRCOMB4, R_DLDIFF, R_DRDIFF, R_MLAPF1, R_MRAPF1, R_MLAPF2, R_MRAPF2, NREG };
static const uint16_t SPU_REG[NSPU][NREG] = {
    {0x007D, 0x005B, 0x6D80, 0x54B8, 0xBED0, 0x0000, 0x0000, 0xBA80, 0x5800, 0x5300, 0x04D6, 0x0333, 0x03F0, 0x0227,
     0x0374, 0x01EF, 0x0334, 0x01B5, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x01B4, 0x0136,
     0x00B8, 0x005C},                                                               /* room        26C0h bytes */
    {0x0033, 0x0025, 0x70F0, 0x4FA8, 0xBCE0, 0x4410, 0xC0F0, 0x9C00, 0x5280, 0x4EC0, 0x03E4, 0x031B, 0x03A4, 0x02AF,
     0x0372, 0x0266, 0x031C, 0x025D, 0x025C, 0x018E, 0x022F, 0x0135, 0x01D2, 0x00B7, 0x018F, 0x00B5, 0x00B4, 0x0080,
     0x004C, 0x0026},                                                               /* studio S    1F40h */
    {0x00B1, 0x007F, 0x70F0, 0x4FA8, 0xBCE0, 0x4510, 0xBEF0, 0xB4C0, 0x5280, 0x4EC0, 0x0904, 0x076B, 0x0824, 0x065F,
     0x07A2, 0x0616, 0x076C, 0x05ED, 0x05EC, 0x042E, 0x050F, 0x0305, 0x0462, 0x02B7, 0x042F, 0x0265, 0x0264, 0x01B2,
     0x0100, 0x0080},                                                               /* studio M    4840h */
    {0x00E3, 0x00A9, 0x6F60, 0x4FA8, 0xBCE0, 0x4510, 0xBEF0, 0xA680, 0x5680, 0x52C0, 0x0DFB, 0x0B58, 0x0D09, 0x0A3C,
     0x0BD9, 0x0973, 0x0B59, 0x08DA, 0x08D9, 0x05E9, 0x07EC, 0x04B0, 0x06EF, 0x03D2, 0x05EA, 0x031D, 0x031C, 0x0238,
     0x0154, 0x00AA},                                                               /* studio L    6FE0h */
    {0x01A5, 0x0139, 0x6000, 0x5000, 0x4C00, 0xB800, 0xBC00, 0xC000, 0x6000, 0x5C00, 0x15BA, 0x11BB, 0x14C2, 0x10BD,
     0x11BC, 0x0DC1, 0x11C0, 0x0DC3, 0x0DC0, 0x09C1, 0x0BC4, 0x07C1, 0x0A00, 0x06CD, 0x09C2, 0x05C1, 0x05C0, 0x041A,
     0x0274, 0x013A},                                                               /* hall        ADE0h */
    {0x0017, 0x0013, 0x70F0, 0x4FA8, 0xBCE0, 0x4510, 0xBEF0, 0x8500, 0x5F80, 0x54C0, 0x0371, 0x02AF, 0x02E5, 0x01DF,
     0x02B0, 0x01D7, 0x0358, 0x026A, 0x01D6, 0x011E, 0x012D, 0x00B1, 0x011F, 0x0059, 0x01A0, 0x00E3, 0x0058, 0x0040,
     0x0028, 0x0014},                                                               /* half echo   3C00h */
    {0x033D, 0x0231, 0x7E00, 0x5000, 0xB400, 0xB000, 0x4C00, 0xB000, 0x6000, 0x5400, 0x1ED6, 0x1A31, 0x1D14, 0x183B,
     0x1BC2, 0x16B2, 0x1A32, 0x15EF, 0x15EE, 0x1055, 0x1334, 0x0F2D, 0x11F6, 0x0C5D, 0x1056, 0x0AE1, 0x0AE0, 0x07A2,
     0x0464, 0x0232},                                                               /* space echo  F6C0h */
    {0x0001, 0x0001, 0x7FFF, 0x7FFF, 0x0000, 0x0000, 0x0000, 0x8100, 0x0000, 0x0000, 0x1FFF, 0x0FFF, 0x1005, 0x0005,
     0x0000, 0x0000, 0x1005, 0x0005, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x1004, 0x1002,
     0x0004, 0x0002},                                                               /* chaos echo  18040h */
    {0x0001, 0x0001, 0x7FFF, 0x7FFF, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x1FFF, 0x0FFF, 0x1005, 0x0005,
     0x0000, 0x0000, 0x1005, 0x0005, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x1004, 0x1002,
     0x0004, 0x0002},                                                               /* one echo    18040h */
};
#define SPU_MASK (FX_RV_LEN - 1)
_Static_assert((FX_RV_LEN & SPU_MASK) == 0, "FX_RV_LEN must be a power of two");
_Static_assert(0x1FFF * 4 + 1 < FX_RV_LEN, "the SPU modes at x4 do not fit FX_RV_LEN");
/* the return's level a mode (the output volume, EVOL, a game's choice on the console): each mode's tail near
 * the plate's on a phrase (tests/host/fx_render.c), so a change of mode does not jump; the half echo and
 * the chaos echo feed back hardest */
static const float SPU_GAIN[NSPU] = {1.6f, 0.65f, 1.0f, 1.03f, 1.0f, 0.39f, 0.93f, 0.54f, 1.36f};
#define SPU_CLEAR 2048                /* samples of the work area cleared a block after a mode change */

/* the SPU's 39-tap half-band FIR (psx-spx), Q15: the centre 4000h and the odd taps either side of it */
static const float SPU_HB[10] = {0x2806 / 32768.0f, -0x0B90 / 32768.0f, 0x0534 / 32768.0f, -0x0268 / 32768.0f,
                                 0x010A / 32768.0f, -0x0067 / 32768.0f, 0x0023 / 32768.0f, -0x000A / 32768.0f,
                                 0x0002 / 32768.0f, -0x0001 / 32768.0f};

typedef struct {
    int32_t a[NREG];                  /* the addresses at the size now (samples) and the dAPF displacements */
    float viir, vwall, vapf1, vapf2, vcomb[4];
    float in[128];                    /* the decimator's input, 44.1 kHz (mono): a ring of 64, each sample written
                                       * twice (at ip and ip + 64) so the taps read it straight, without a mask */
    float up[2][64];                  /* the interpolators' input, 22.05 kHz: rings of 32, written twice the same way */
    float out[2][2];                  /* the two samples a tick gives, a side */
    float tl, th, tr, thr;            /* the tone's states */
    uint32_t base;
    int32_t clear;                    /* samples of the work area still to clear */
    int32_t quiet;                    /* 44.1 kHz samples since anything but zero was written or came in */
    uint8_t ip, up_w, phase, mode;
} spu_t;

/* the registers at a size (x1 .. x4) and the volumes at decay / damp */
static void spu_derive(spu_t *s, int mode, float size, float decay, float damp)
{
    const uint16_t *R = SPU_REG[mode - 1];
    float w = (float)(int16_t)R[R_VWALL] * (1.0f / 32768.0f), aw = fm_fabsf(w), mx, sc;
    int i;
    for (i = 0; i < NREG; i++)
        if (i >= R_MLSAME || i == R_DAPF1 || i == R_DAPF2)
            s->a[i] = (int32_t)((float)R[i] * size + 0.5f);
    mx = aw > 1e-5f ? fm_clampf(0.99f / aw, 0.5f, 10.0f) : 1.0f;        /* psxverb's v2_update_decay */
    sc = decay <= 0.5f ? 0.5f + decay : 1.0f + (decay - 0.5f) * 2.0f * (mx - 1.0f);
    s->vwall = fm_clampf(w * sc, -0.995f, 0.995f);
    s->viir = (float)(int16_t)R[R_VIIR] * (1.0f / 32768.0f) * (1.0f - 0.85f * damp);
    s->vapf1 = (float)(int16_t)R[R_VAPF1] * (1.0f / 32768.0f);
    s->vapf2 = (float)(int16_t)R[R_VAPF2] * (1.0f / 32768.0f);
    for (i = 0; i < 4; i++)
        s->vcomb[i] = (float)(int16_t)R[R_VCOMB1 + i] * (1.0f / 32768.0f);
}

static void spu_reset(spu_t *s, int mode)
{
    int i;
    for (i = 0; i < 128; i++)
        s->in[i] = 0.0f;
    for (i = 0; i < 64; i++)
        s->up[0][i] = s->up[1][i] = 0.0f;
    s->out[0][0] = s->out[0][1] = s->out[1][0] = s->out[1][1] = 0.0f;
    s->tl = s->th = s->tr = s->thr = 0.0f;
    s->base = 0;
    s->ip = s->up_w = s->phase = 0;
    s->mode = (uint8_t)mode;
    s->clear = FX_RV_LEN;
    s->quiet = FX_RV_LEN * 2;
}

static inline float spu_rd(const int16_t *w, uint32_t base, int32_t off)
{
    return (float)w[(base + (uint32_t)off) & SPU_MASK] * (1.0f / 32768.0f);
}
static inline int32_t spu_wr(int16_t *w, uint32_t base, int32_t off, float v)
{
    int32_t k = (int32_t)(v * 32768.0f);
    k = k > 32767 ? 32767 : k < -32768 ? -32768 : k;
    w[(base + (uint32_t)off) & SPU_MASK] = (int16_t)k;
    return k;
}

/* one 22.05 kHz tick: x the decimated send (vLIN applied); the two outputs, before the interpolators.
 * Returns the bits of everything written (0: the tick wrote only zeros) */
static inline int32_t spu_tick(spu_t *s, int16_t *w, float x, float *lo, float *ro)
{
    const int32_t *a = s->a;
    const uint32_t b = s->base;
    const float vw = s->vwall, vi = s->viir;
    float iir, l, r, d;
    int32_t any = 0;
    iir = spu_rd(w, b, a[R_MLSAME] - 1);
    any |= spu_wr(w, b, a[R_MLSAME], (x + spu_rd(w, b, a[R_DLSAME]) * vw - iir) * vi + iir);
    iir = spu_rd(w, b, a[R_MRSAME] - 1);
    any |= spu_wr(w, b, a[R_MRSAME], (x + spu_rd(w, b, a[R_DRSAME]) * vw - iir) * vi + iir);
    iir = spu_rd(w, b, a[R_MLDIFF] - 1);
    any |= spu_wr(w, b, a[R_MLDIFF], (x + spu_rd(w, b, a[R_DRDIFF]) * vw - iir) * vi + iir);
    iir = spu_rd(w, b, a[R_MRDIFF] - 1);
    any |= spu_wr(w, b, a[R_MRDIFF], (x + spu_rd(w, b, a[R_DLDIFF]) * vw - iir) * vi + iir);
    l = s->vcomb[0] * spu_rd(w, b, a[R_MLCOMB1]) + s->vcomb[1] * spu_rd(w, b, a[R_MLCOMB2]) +
        s->vcomb[2] * spu_rd(w, b, a[R_MLCOMB3]) + s->vcomb[3] * spu_rd(w, b, a[R_MLCOMB4]);
    r = s->vcomb[0] * spu_rd(w, b, a[R_MRCOMB1]) + s->vcomb[1] * spu_rd(w, b, a[R_MRCOMB2]) +
        s->vcomb[2] * spu_rd(w, b, a[R_MRCOMB3]) + s->vcomb[3] * spu_rd(w, b, a[R_MRCOMB4]);
    d = spu_rd(w, b, a[R_MLAPF1] - a[R_DAPF1]);
    l -= s->vapf1 * d;
    any |= spu_wr(w, b, a[R_MLAPF1], l);
    l = l * s->vapf1 + d;
    d = spu_rd(w, b, a[R_MRAPF1] - a[R_DAPF1]);
    r -= s->vapf1 * d;
    any |= spu_wr(w, b, a[R_MRAPF1], r);
    r = r * s->vapf1 + d;
    d = spu_rd(w, b, a[R_MLAPF2] - a[R_DAPF2]);
    l -= s->vapf2 * d;
    any |= spu_wr(w, b, a[R_MLAPF2], l);
    l = l * s->vapf2 + d;
    d = spu_rd(w, b, a[R_MRAPF2] - a[R_DAPF2]);
    r -= s->vapf2 * d;
    any |= spu_wr(w, b, a[R_MRAPF2], r);
    r = r * s->vapf2 + d;
    s->base = (b + 1u) & SPU_MASK;
    *lo = l;
    *ro = r;
    return any;
}

/* the decimator: the 39 taps over the last 39 inputs (the newest at ip - 1) */
static inline float spu_decimate(const spu_t *s)
{
    const float *z = s->in + s->ip + 64u - 20u;                      /* the centre: 19 samples back (z[-19..19] lie
                                                                      * in the doubled ring) */
    float y = 0.5f * z[0];
    int k;
    for (k = 0; k < 10; k++)
        y += SPU_HB[k] * (z[-1 - 2 * k] + z[1 + 2 * k]);
    return y;
}
/* an interpolator: the zero-stuffed 22.05 kHz side through the same FIR, x2: one output is the side taps'
 * sum (between two inputs), the next the input 10 ticks back (the centre tap, 0.5 x 2) */
static inline void spu_interp(const float *u, uint32_t w, float *o0, float *o1)
{
    const float *z = u + w + 31u - 9u;                               /* (z[-9..9]: in the doubled ring) */
    float y = 0.0f;
    int k;
    for (k = 0; k < 10; k++)
        y += SPU_HB[k] * (z[k] + z[-1 - k]);
    *o0 = 2.0f * y;
    *o1 = z[0];
}
