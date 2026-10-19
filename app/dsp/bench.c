/* SPDX-License-Identifier: GPL-3.0-only */
/* LOOM-8's hardware profiler's kernels: see bench.h. Each kernel is the per-sample code of one building block, written
 * as synth.c and fx.c write theirs (the TPT state-variable filter, the PolyBLEP, soft3, int16 delay lines in the
 * pool), so its cost is what that block costs in a voice or an effect. The reverb is a Dattorro plate (Dattorro,
 * "Effect Design, Part 1", JAES 1997) with its delays scaled down to fit the bench's line: a delay's cost does not
 * depend on its length. */
#include "bench.h"
#if XY_BENCH
#include "fastmath.h"
#ifdef PLAT_HOST
#define BENCH_POOL
#define BENCH_RAM_CODE
#else
#include "plat.h"
#define BENCH_POOL PLAT_POOL
#define BENCH_RAM_CODE __attribute__((section(".c1_text.bench"), noinline))
#endif

#define DL_LEN 4096                     /* the delay lines' pool, int16 (8 KB), as the FX keep theirs */
#define DL_MASK (DL_LEN - 1)
#define BIG_TAB 4096                    /* a wavetable bigger than any cache: 16 KB of flash */
#define SMALL_TAB 256

static int16_t dl[DL_LEN] BENCH_POOL;
static float *b_in, *b_out;             /* the lent RAM: in, out (BENCH_N each) */
static float st[24];                    /* the kernel's state */
static uint32_t rng = 22222u, dl_base, code_at;
static int cur = -1;

/* the tables: SMALL_TAB and BIG_TAB points, const (in the flash), and a RAM copy of the small one */
static float tab_small_ram[SMALL_TAB];
extern const float bench_tab_small[SMALL_TAB], bench_tab_big[BIG_TAB];

static inline float blep(float t, float dt, float idt)
{
    if (t < dt) {
        t *= idt;
        return t + t - t * t - 1.0f;
    }
    if (t > 1.0f - dt) {
        t = (t - 1.0f) * idt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}
static inline float soft3(float x)
{
    x = fm_minf(fm_maxf(x, -1.5f), 1.5f);
    return x - x * x * x * (4.0f / 27.0f);
}
static inline uint32_t xs(uint32_t *r)
{
    uint32_t x = *r;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return *r = x;
}
/* the delay lines: one int16 ring whose base moves a sample a tick (as the SPU reverb's work area); a line at offset a
 * written now is read back len samples later at a + len */
static inline float rd(uint32_t a) { return (float)dl[(dl_base + a) & DL_MASK] * (1.0f / 32768.0f); }
static inline float rdf(float a)                  /* a fractional read, linearly interpolated */
{
    uint32_t i = (uint32_t)a;
    float f = a - (float)i, x0 = rd(i), x1 = rd(i + 1u);
    return x0 + f * (x1 - x0);
}
static inline void wr(uint32_t a, float x)
{
    float y = fm_clampf(x * 32768.0f, -32768.0f, 32767.0f);
    dl[(dl_base + a) & DL_MASK] = (int16_t)y;
}

/* ------------------------------------------------------------------ kernels --- */
static float k_copy(void)
{
    float g = st[0], s = 0.0f;
    for (int j = 0; j < BENCH_N; j++)
        b_out[j] = b_in[j] * g;
    s = b_out[BENCH_N - 1];
    return s;
}
static float k_saw(void)                          /* a PolyBLEP saw */
{
    float p = st[0], d = st[1], id = st[2];
    for (int j = 0; j < BENCH_N; j++) {
        p += d;
        if (p >= 1.0f)
            p -= 1.0f;
        b_out[j] = 2.0f * p - 1.0f - blep(p, d, id);
    }
    st[0] = p;
    return b_out[7];
}
static float k_pulse(void)                        /* a PolyBLEP pulse (two residuals) */
{
    float p = st[0], d = st[1], id = st[2], pw = 0.37f;
    for (int j = 0; j < BENCH_N; j++) {
        float q;
        p += d;
        if (p >= 1.0f)
            p -= 1.0f;
        q = p + (1.0f - pw);
        if (q >= 1.0f)
            q -= 1.0f;
        b_out[j] = (p < pw ? 1.0f : -1.0f) + blep(p, d, id) - blep(q, d, id);
    }
    st[0] = p;
    return b_out[7];
}
static float k_saw2(void)                         /* two detuned PolyBLEP saws (a stack voice's pair) */
{
    float p = st[0], q = st[3], d = st[1], e = st[1] * 1.003f, id = st[2], ie = 1.0f / e;
    for (int j = 0; j < BENCH_N; j++) {
        p += d;
        if (p >= 1.0f)
            p -= 1.0f;
        q += e;
        if (q >= 1.0f)
            q -= 1.0f;
        b_out[j] = 2.0f * (p + q) - 2.0f - blep(p, d, id) - blep(q, e, ie);
    }
    st[0] = p;
    st[3] = q;
    return b_out[7];
}
static float k_sine(void)
{
    float p = st[0], d = st[1];
    for (int j = 0; j < BENCH_N; j++) {
        p += d;
        if (p >= 1.0f)
            p -= 1.0f;
        b_out[j] = fm_sin_turns(p);
    }
    st[0] = p;
    return b_out[7];
}
static float k_noise(void)
{
    for (int j = 0; j < BENCH_N; j++)
        b_out[j] = (float)(int32_t)xs(&rng) * (1.0f / 2147483648.0f);
    return b_out[7];
}
static float wt_read(const float *t, uint32_t len)   /* a table read, linear, at a non-integer step */
{
    float p = st[0], d = st[1] * (float)len, fl = (float)len;
    for (int j = 0; j < BENCH_N; j++) {
        uint32_t i;
        float f;
        p += d;
        if (p >= fl)
            p -= fl;
        i = (uint32_t)p;
        f = p - (float)i;
        b_out[j] = t[i] + f * (t[(i + 1u) & (len - 1u)] - t[i]);
    }
    st[0] = p;
    return b_out[7];
}
static float k_wt_flash(void) { return wt_read(bench_tab_small, SMALL_TAB); }
static float k_wt_ram(void) { return wt_read(tab_small_ram, SMALL_TAB); }
static float k_wt_big(void) { return wt_read(bench_tab_big, BIG_TAB); }
static float k_onepole(void)
{
    float z = st[4], a = st[5];
    for (int j = 0; j < BENCH_N; j++) {
        z += a * (b_in[j] - z);
        b_out[j] = z;
    }
    st[4] = z;
    return z;
}
static float k_svf12(void)                        /* synth.c's TPT state-variable low-pass, 12 dB */
{
    float a1 = st[5], a2 = st[6], a3 = st[7], f0 = st[8], f1 = st[9];
    for (int j = 0; j < BENCH_N; j++) {
        float t3 = b_in[j] - f1, t1 = a1 * f0 + a2 * t3, t2 = f1 + a2 * f0 + a3 * t3;
        f0 = 2.0f * t1 - f0;
        f1 = 2.0f * t2 - f1;
        b_out[j] = t2;
    }
    st[8] = fm_flush(f0);
    st[9] = fm_flush(f1);
    return b_out[7];
}
static float k_svf24(void)                        /* two stages: lp24 */
{
    float a1 = st[5], a2 = st[6], a3 = st[7], b1 = st[12], b2 = st[13], b3 = st[14];
    float f0 = st[8], f1 = st[9], f2 = st[10], f3 = st[11];
    for (int j = 0; j < BENCH_N; j++) {
        float t3 = b_in[j] - f1, t1 = a1 * f0 + a2 * t3, t2 = f1 + a2 * f0 + a3 * t3, w3, w1, w2;
        f0 = 2.0f * t1 - f0;
        f1 = 2.0f * t2 - f1;
        w3 = t2 - f3;
        w1 = b1 * f2 + b2 * w3;
        w2 = f3 + b2 * f2 + b3 * w3;
        f2 = 2.0f * w1 - f2;
        f3 = 2.0f * w2 - f3;
        b_out[j] = w2;
    }
    st[8] = fm_flush(f0);
    st[9] = fm_flush(f1);
    st[10] = fm_flush(f2);
    st[11] = fm_flush(f3);
    return b_out[7];
}
static float k_ladder(void)                       /* four one-poles, the feedback through soft3 */
{
    float b1 = st[5], k = st[6], f0 = st[8], f1 = st[9], f2 = st[10], f3 = st[11];
    for (int j = 0; j < BENCH_N; j++) {
        float x = b_in[j] - k * soft3(f3);
        f0 += b1 * (x - f0);
        f1 += b1 * (f0 - f1);
        f2 += b1 * (f1 - f2);
        f3 += b1 * (f2 - f3);
        b_out[j] = f3;
    }
    st[8] = fm_flush(f0);
    st[9] = fm_flush(f1);
    st[10] = fm_flush(f2);
    st[11] = fm_flush(f3);
    return b_out[7];
}
static float k_soft3(void)
{
    float g = st[0];
    for (int j = 0; j < BENCH_N; j++)
        b_out[j] = soft3(b_in[j] * g);
    return b_out[7];
}
static float k_tanh(void)
{
    float g = st[0];
    for (int j = 0; j < BENCH_N; j++)
        b_out[j] = fm_tanhf(b_in[j] * g);
    return b_out[7];
}
static float k_exp2(void)                         /* an exp2 a sample (a pitch or cutoff moved per sample) */
{
    for (int j = 0; j < BENCH_N; j++)
        b_out[j] = fm_exp2f(b_in[j]);
    return b_out[7];
}
static float k_out(void)                          /* a voice's outputs: stereo gains ramped, two sends */
{
    float gl = st[4], gr = st[5], s = 0.0f, t = 0.0f, u = 0.0f, v = 0.0f, e = st[6], es = 1e-5f;
    for (int j = 0; j < BENCH_N; j++) {
        float y = b_in[j] * e, l = y * gl, r = y * gr, m = (l + r) * 0.5f;
        e += es;
        s += l;
        t += r;
        u += m * 0.3f;
        v += m * 0.2f;
        b_out[j] = l;
    }
    return s + t + u + v;
}
static float k_delay(void)                        /* an int16 line: a write and a fractional read */
{
    float t = 1500.5f;
    for (int j = 0; j < BENCH_N; j++) {
        float y = rdf(t);
        wr(0u, b_in[j] + 0.4f * y);
        b_out[j] = y;
        dl_base--;
    }
    return b_out[7];
}
static float k_chorus(void)                       /* two taps moved by a triangle LFO, a stereo pair */
{
    float ph = st[0], d = 0.33f / 44100.0f, l = 0.0f, r = 0.0f;
    for (int j = 0; j < BENCH_N; j++) {
        float tri, a, b;
        ph += d;
        if (ph >= 1.0f)
            ph -= 1.0f;
        tri = 4.0f * fm_fabsf(ph - 0.5f) - 1.0f;
        a = rdf(400.0f + 240.0f * tri);
        b = rdf(400.0f - 240.0f * tri);
        wr(0u, b_in[j] + 0.2f * a);
        l += a;
        r += b;
        b_out[j] = a;
        dl_base--;
    }
    st[0] = ph;
    return l + r;
}
static float k_allpass(void)                      /* one Schroeder allpass on the line */
{
    for (int j = 0; j < BENCH_N; j++) {
        float d = rd(557u), w = b_in[j] - 0.6f * d;
        wr(0u, w);
        b_out[j] = d + 0.6f * w;
        dl_base--;
    }
    return b_out[7];
}

/* the Dattorro plate, a sample in (mono) and two out; its lines at offsets in dl (lengths: Dattorro's at 29761 Hz
 * divided by 6, plus room for the modulation) */
enum { DA_A1 = 0, DA_A2 = 30, DA_A3 = 52, DA_A4 = 118, DA_M1 = 168, DA_D1 = 300, DA_P1 = 1050, DA_D2 = 1360,
       DA_M2 = 1990, DA_D3 = 2150, DA_P2 = 2860, DA_D4 = 3310, DA_END = 3850 };
#define DA_AP(off, len, g, x) do { float d_ = rd((off) + (len)), w_ = (x) - (g) * d_; wr((off), w_); (x) = d_ + (g) * w_; } while (0)
static float plate_tick(float x, float *l, float *r)
{
    float bw = st[4], dmp1 = st[5], dmp2 = st[6], lfo = st[7], fb, a, b, m;
    bw += 0.9995f * (x - bw);
    x = bw;
    DA_AP(DA_A1, 24u, 0.75f, x);
    DA_AP(DA_A2, 18u, 0.75f, x);
    DA_AP(DA_A3, 63u, 0.625f, x);
    DA_AP(DA_A4, 46u, 0.625f, x);
    lfo += 1.0f / 44100.0f;
    if (lfo >= 1.0f)
        lfo -= 1.0f;
    m = 4.0f * fm_fabsf(lfo - 0.5f) - 1.0f;           /* (a triangle: the excursion) */
    fb = rd(DA_D4 + 527u);                          /* the other half's end */
    a = x + 0.5f * fb;
    {   /* half 1: a modulated allpass, a delay, the damping, an allpass, a delay */
        float d = rdf((float)DA_M1 + 112.0f + 8.0f * m), w = a + 0.7f * d;
        wr(DA_M1, w);
        a = d - 0.7f * w;
    }
    wr(DA_D1, a);
    a = rd(DA_D1 + 742u);
    dmp1 += 0.3f * (a - dmp1);
    a = dmp1 * 0.5f;
    DA_AP(DA_P1, 300u, 0.5f, a);
    wr(DA_D2, a);
    b = x + 0.5f * rd(DA_D2 + 620u);
    {
        float d = rdf((float)DA_M2 + 151.0f - 8.0f * m), w = b + 0.7f * d;
        wr(DA_M2, w);
        b = d - 0.7f * w;
    }
    wr(DA_D3, b);
    b = rd(DA_D3 + 703u);
    dmp2 += 0.3f * (b - dmp2);
    b = dmp2 * 0.5f;
    DA_AP(DA_P2, 443u, 0.5f, b);
    wr(DA_D4, b);
    /* the output taps: seven a side */
    *l = rd(DA_D1 + 44u) + rd(DA_D1 + 495u) - rd(DA_P1 + 318u) + rd(DA_D2 + 332u) - rd(DA_D3 + 343u)
         - rd(DA_P2 + 31u) - rd(DA_D4 + 178u);
    *r = rd(DA_D3 + 58u) + rd(DA_D3 + 600u) - rd(DA_P2 + 205u) + rd(DA_D4 + 496u) - rd(DA_D1 + 371u)
         - rd(DA_P1 + 46u) - rd(DA_D2 + 25u);
    st[4] = bw;
    st[5] = dmp1;
    st[6] = dmp2;
    st[7] = lfo;
    dl_base--;
    return x;
}
static float k_plate(void)                        /* at the full rate: a tick a sample */
{
    float s = 0.0f;
    for (int j = 0; j < BENCH_N; j++) {
        float l, r;
        plate_tick(b_in[j], &l, &r);
        b_out[j] = l;
        s += r;
    }
    return s;
}
static float k_plate_half(void)                   /* at half the rate: a tick every other sample, averaged in, held
                                                   * and linearly interpolated out */
{
    float s = 0.0f, pl = st[15], pr = st[16];
    for (int j = 0; j < BENCH_N; j += 2) {
        float l, r;
        plate_tick(0.5f * (b_in[j] + b_in[j + 1]), &l, &r);
        b_out[j] = 0.5f * (pl + l);
        b_out[j + 1] = l;
        s += 0.5f * (pr + r) + r;
        pl = l;
        pr = r;
    }
    st[15] = pl;
    st[16] = pr;
    return s;
}
static float k_voice(void)                        /* a subtractive voice's sample path: two saws, lp24, amp, outputs */
{
    float p = st[0], q = st[3], d = st[1], e = st[1] * 1.003f, id = st[2], ie = 1.0f / e;
    float a1 = st[5], a2 = st[6], a3 = st[7], f0 = st[8], f1 = st[9], f2 = st[10], f3 = st[11];
    float env = st[17], es = 1e-5f, l = 0.0f, r = 0.0f, s1 = 0.0f;
    for (int j = 0; j < BENCH_N; j++) {
        float x, t3, t1, t2, w3, w1, w2, y;
        p += d;
        if (p >= 1.0f)
            p -= 1.0f;
        q += e;
        if (q >= 1.0f)
            q -= 1.0f;
        x = p + q - 1.0f - 0.5f * (blep(p, d, id) + blep(q, e, ie));
        t3 = x - f1, t1 = a1 * f0 + a2 * t3, t2 = f1 + a2 * f0 + a3 * t3;
        f0 = 2.0f * t1 - f0;
        f1 = 2.0f * t2 - f1;
        w3 = t2 - f3, w1 = a1 * f2 + a2 * w3, w2 = f3 + a2 * f2 + a3 * w3;
        f2 = 2.0f * w1 - f2;
        f3 = 2.0f * w2 - f3;
        env += es;
        y = w2 * env;
        b_out[j] += y * 0.7f;
        l += y * 0.7f;
        r += y * 0.6f;
        s1 += y * 0.2f;
    }
    st[0] = p;
    st[3] = q;
    st[8] = fm_flush(f0);
    st[9] = fm_flush(f1);
    st[10] = fm_flush(f2);
    st[11] = fm_flush(f3);
    return l + r + s1;
}

/* the code-size kernels: CK_N distinct copies of one function (each about a kilobyte: 64 multiply-adds over four
 * chains, their constants all different), called a block of 32 samples at a time, cycling through the first N of
 * them; "code ram" is the same function in RAM */
#define CK_N 64
#define CK_OP(y) y = y * (0.9990f - (float)(__COUNTER__ & 1023) * 1.0e-7f) + x[j] * (1.0e-3f + (float)(__COUNTER__ & 1023) * 1.0e-8f);
#define CK_G CK_OP(y0) CK_OP(y1) CK_OP(y2) CK_OP(y3)
#define CK_R4(x) x x x x
#define CK_BODY \
    float y0 = x[0], y1 = x[1], y2 = x[2], y3 = x[3]; \
    for (int j = 0; j < n; j++) { \
        CK_R4(CK_R4(CK_G)) \
        x[j] = y0 + y1 + y2 + y3; \
    }
#define CK_DEF(i) static void __attribute__((noinline)) ck_##i(float *x, int n) { CK_BODY }
CK_DEF(0) CK_DEF(1) CK_DEF(2) CK_DEF(3) CK_DEF(4) CK_DEF(5) CK_DEF(6) CK_DEF(7)
CK_DEF(8) CK_DEF(9) CK_DEF(10) CK_DEF(11) CK_DEF(12) CK_DEF(13) CK_DEF(14) CK_DEF(15)
CK_DEF(16) CK_DEF(17) CK_DEF(18) CK_DEF(19) CK_DEF(20) CK_DEF(21) CK_DEF(22) CK_DEF(23)
CK_DEF(24) CK_DEF(25) CK_DEF(26) CK_DEF(27) CK_DEF(28) CK_DEF(29) CK_DEF(30) CK_DEF(31)
CK_DEF(32) CK_DEF(33) CK_DEF(34) CK_DEF(35) CK_DEF(36) CK_DEF(37) CK_DEF(38) CK_DEF(39)
CK_DEF(40) CK_DEF(41) CK_DEF(42) CK_DEF(43) CK_DEF(44) CK_DEF(45) CK_DEF(46) CK_DEF(47)
CK_DEF(48) CK_DEF(49) CK_DEF(50) CK_DEF(51) CK_DEF(52) CK_DEF(53) CK_DEF(54) CK_DEF(55)
CK_DEF(56) CK_DEF(57) CK_DEF(58) CK_DEF(59) CK_DEF(60) CK_DEF(61) CK_DEF(62) CK_DEF(63)
typedef void (*ck_fn)(float *, int);
static ck_fn const ck_tab[CK_N] = {
    ck_0, ck_1, ck_2, ck_3, ck_4, ck_5, ck_6, ck_7, ck_8, ck_9, ck_10, ck_11, ck_12, ck_13, ck_14, ck_15,
    ck_16, ck_17, ck_18, ck_19, ck_20, ck_21, ck_22, ck_23, ck_24, ck_25, ck_26, ck_27, ck_28, ck_29, ck_30, ck_31,
    ck_32, ck_33, ck_34, ck_35, ck_36, ck_37, ck_38, ck_39, ck_40, ck_41, ck_42, ck_43, ck_44, ck_45, ck_46, ck_47,
    ck_48, ck_49, ck_50, ck_51, ck_52, ck_53, ck_54, ck_55, ck_56, ck_57, ck_58, ck_59, ck_60, ck_61, ck_62, ck_63};
static void BENCH_RAM_CODE ck_ram(float *x, int n) { CK_BODY }
static float k_code(int copies)
{
    for (int b = 0; b < BENCH_N; b += 32) {
        ck_fn volatile f = ck_tab[code_at];      /* (volatile: always an indirect call) */
        f(b_out + b, 32);
        if (++code_at >= (uint32_t)copies)
            code_at = 0;
    }
    return b_out[7];
}
static float k_code_ram(void)
{
    ck_fn volatile f = ck_ram;                    /* (RAM is out of a direct call's reach: a pointer) */
    for (int b = 0; b < BENCH_N; b += 32)
        f(b_out + b, 32);
    return b_out[7];
}

/* --------------------------------------------------------------- the list --- */
static const uint8_t CODE_N[] = {1, 2, 4, 8, 12, 16, 24, 32, 48, 64};
#define NCODE (int)(sizeof CODE_N / sizeof CODE_N[0])
typedef struct { const char *name; float (*fn)(void); } kern_t;
static const kern_t KERN[] = {
    {"copy", k_copy}, {"saw", k_saw}, {"pulse", k_pulse}, {"saw x2", k_saw2}, {"sine", k_sine},
    {"noise", k_noise}, {"wt flash", k_wt_flash}, {"wt ram", k_wt_ram}, {"wt big", k_wt_big},
    {"onepole", k_onepole}, {"svf12", k_svf12}, {"svf24", k_svf24}, {"ladder", k_ladder}, {"soft3", k_soft3},
    {"tanh", k_tanh}, {"exp2", k_exp2}, {"out", k_out}, {"delay", k_delay}, {"chorus", k_chorus},
    {"allpass", k_allpass}, {"plate", k_plate}, {"plate half", k_plate_half}, {"voice", k_voice},
    {"code ram", k_code_ram}};
#define NKERN (int)(sizeof KERN / sizeof KERN[0])
static const char *const CODE_NAME[NCODE] = {"code 1", "code 2", "code 4", "code 8", "code 12", "code 16",
                                             "code 24", "code 32", "code 48", "code 64"};

int bench_count(void) { return NKERN + NCODE; }
const char *bench_name(int k)
{
    return k < 0 || k >= NKERN + NCODE ? "" : k < NKERN ? KERN[k].name : CODE_NAME[k - NKERN];
}
void bench_prep(int k, void *ram)
{
    float f = 440.0f / 44100.0f, g, w;
    int i;
    b_in = (float *)ram;
    b_out = b_in + BENCH_N;
    for (i = 0; i < BENCH_N; i++) {               /* a quiet saw-ish input */
        b_in[i] = 0.5f * ((float)((i * 7) % 64) * (1.0f / 32.0f) - 1.0f);
        b_out[i] = 0.0f;
    }
    for (i = 0; i < SMALL_TAB; i++)
        tab_small_ram[i] = bench_tab_small[i];
    for (i = 0; i < 24; i++)
        st[i] = 0.0f;
    st[0] = 0.8f;                                 /* a phase, or a gain */
    st[1] = f;
    st[2] = 1.0f / f;
    g = fm_tanf(FM_PI * 1200.0f / 44100.0f);      /* the TPT coefficients at 1.2 kHz, Q 2 */
    w = 1.0f / (1.0f + g * (g + 0.5f));
    st[5] = w;
    st[6] = g * w;
    st[7] = g * st[6];
    st[12] = st[5];
    st[13] = st[6];
    st[14] = st[7];
    if (k < NKERN && KERN[k].fn == k_ladder) {    /* (the ladder: its one-pole and its feedback) */
        st[5] = 0.15f;
        st[6] = 2.5f;
    }
    if (k < NKERN && KERN[k].fn == k_onepole)
        st[5] = 0.05f;
    st[17] = 0.5f;
    for (i = 0; i < DL_LEN; i++)
        dl[i] = 0;
    dl_base = 0;
    code_at = 0;
    cur = k;
}
float bench_run(int k)
{
    if (k != cur || !b_in)
        return 0.0f;
    if (k < NKERN)
        return KERN[k].fn();
    if (k < NKERN + NCODE)
        return k_code(CODE_N[k - NKERN]);
    return 0.0f;
}

/* the flash tables, filled at build time (any values: only the reads are timed), const data in the flash */
#define TS(i) (0.5f - 0.5f * (float)(((i) * 37) % 101) * (1.0f / 101.0f))
#define TS4(i) TS(i), TS(i + 1), TS(i + 2), TS(i + 3)
#define TS16(i) TS4(i), TS4(i + 4), TS4(i + 8), TS4(i + 12)
#define TS64(i) TS16(i), TS16(i + 16), TS16(i + 32), TS16(i + 48)
#define TS256(i) TS64(i), TS64(i + 64), TS64(i + 128), TS64(i + 192)
#define TS1K(i) TS256(i), TS256(i + 256), TS256(i + 512), TS256(i + 768)
const float bench_tab_small[SMALL_TAB] = {TS256(0)};
const float bench_tab_big[BIG_TAB] = {TS1K(0), TS1K(1024), TS1K(2048), TS1K(3072)};
#endif
