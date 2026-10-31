/* SPDX-License-Identifier: GPL-3.0-only */
/* LOOM-8's 909 kit models: 9W9's TR-909 circuit models (athousanddetails, GPL-3.0-only) through X0X's port
 * (drum909.c, drum909_dsp.h): see d909.h. The circuits, constants and per-trigger pinning are 9W9's as X0X has them;
 * the building blocks below are drum909_dsp.h's (its envelopes re-anchored once a call, its biquads, the triangle on
 * a uint32 phase, the distortions), its tanh table at half X0X's resolution. */
#include "d909.h"
#include "fastmath.h"

#define D9_SR 44100.0f
#define D9_MS (0.001f * D9_SR)                 /* samples per ms, as 9W9's `ms` */
#define D9_MIN_EXP 1.0e-9f
#define D9_PHASE_PER_HZ 97391.548752834467f   /* 2^32 / 44100 */
#define D9_PH_QUARTER 0x40000000u              /* 9W9 starts every hit at phase 0.25 */
#define D9_VCA_VT 0.0002f
#define D9_VCA_NORM (1.0f / (1.0f - D9_VCA_VT))
#define D9_SNAPPY_MIX 1.95f
#define D9_BD_BASE_HZ 49.0f
#define D9_BD_DF_PER_MS 4.6f
#define D9_BD_PHOLD_MS 16.0f
#define D9_INV_TANH_1_6 1.0849887132644653f    /* 1 / tanh(1.6) */
#define D9_INV_TANH_2_0 1.037314772605896f     /* 1 / tanh(2.0) */
#define D9_ACCENT 2.0f                         /* 9W9's kit Accent at its default (pot 42) */

enum { D9_LP = 0, D9_HP, D9_BP };

/* tanh(u) at u = i / 64, i = 0..513 (one guard point): linear between, abs error < 2.5e-5 (X0X's: 1025 points) */
#define D9_TANH_N 512
#define D9_TANH_SCALE 64.0f
static const float d9_tanh_tab[D9_TANH_N + 2] = {
    0.0f, 0.0156237287f, 0.0312398318f, 0.0468406975f, 0.0624187477f, 0.0779664442f,
    0.0934763029f, 0.108940929f, 0.124352999f, 0.1397053f, 0.154990733f, 0.170202315f,
    0.185333207f, 0.200376719f, 0.215326339f, 0.230175704f, 0.244918659f, 0.259549201f,
    0.27406159f, 0.288450211f, 0.302709728f, 0.316835016f, 0.330821127f, 0.344663411f,
    0.3583574f, 0.371898919f, 0.385283977f, 0.398508847f, 0.411570042f, 0.424464375f,
    0.437188774f, 0.449740559f, 0.462117165f, 0.474316329f, 0.486336023f, 0.498174429f,
    0.509829998f, 0.521301329f, 0.53258729f, 0.543686986f, 0.554599702f, 0.565324962f,
    0.575862408f, 0.58621192f, 0.596373558f, 0.60634762f, 0.616134405f, 0.625734627f,
    0.635148942f, 0.644378245f, 0.653423607f, 0.662286103f, 0.670967102f, 0.679467916f,
    0.687790215f, 0.695935488f, 0.703905582f, 0.711702287f, 0.719327509f, 0.726783216f,
    0.734071493f, 0.741194546f, 0.748154461f, 0.754953563f, 0.761594176f, 0.768078566f,
    0.774409175f, 0.780588448f, 0.786618829f, 0.792502761f, 0.798242748f, 0.803841352f,
    0.809301078f, 0.814624429f, 0.819814026f, 0.824872315f, 0.829801917f, 0.834605336f,
    0.839285076f, 0.843843699f, 0.848283648f, 0.852607489f, 0.856817603f, 0.860916495f,
    0.864906609f, 0.868790329f, 0.872570038f, 0.876248002f, 0.879826725f, 0.883308291f,
    0.886695147f, 0.889989436f, 0.893193364f, 0.896309078f, 0.899338722f, 0.902284443f,
    0.905148268f, 0.907932222f, 0.910638273f, 0.913268387f, 0.915824533f, 0.918308556f,
    0.920722306f, 0.923067629f, 0.925346196f, 0.927559912f, 0.929710329f, 0.931799173f,
    0.933828056f, 0.935798585f, 0.937712312f, 0.939570844f, 0.941375554f, 0.94312793f,
    0.944829464f, 0.946481407f, 0.948085308f, 0.949642301f, 0.951153815f, 0.952621043f,
    0.954045236f, 0.955427647f, 0.956769347f, 0.95807153f, 0.959335268f, 0.960561752f,
    0.961751938f, 0.962906897f, 0.964027584f, 0.965115011f, 0.966170192f, 0.967193961f,
    0.968187213f, 0.969150901f, 0.9700858f, 0.970992863f, 0.971872747f, 0.972726345f,
    0.973554373f, 0.974357545f, 0.975136697f, 0.975892425f, 0.976625502f, 0.977336466f,
    0.978026092f, 0.978694975f, 0.979343712f, 0.979972839f, 0.980583072f, 0.981174827f,
    0.9817487f, 0.982305288f, 0.982845008f, 0.983368456f, 0.98387599f, 0.984368205f,
    0.984845519f, 0.985308349f, 0.985757172f, 0.986192346f, 0.986614287f, 0.987023473f,
    0.987420201f, 0.98780489f, 0.988177836f, 0.988539517f, 0.988890171f, 0.989230096f,
    0.98955977f, 0.98987931f, 0.990189195f, 0.990489602f, 0.99078083f, 0.991063237f,
    0.991337001f, 0.991602421f, 0.991859734f, 0.992109179f, 0.992351055f, 0.99258548f,
    0.992812812f, 0.993033171f, 0.993246794f, 0.99345386f, 0.993654609f, 0.993849277f,
    0.994037926f, 0.994220853f, 0.994398117f, 0.994570017f, 0.994736671f, 0.9948982f,
    0.995054781f, 0.995206535f, 0.995353699f, 0.995496333f, 0.995634556f, 0.995768607f,
    0.995898485f, 0.99602443f, 0.99614656f, 0.996264875f, 0.996379554f, 0.996490777f,
    0.996598542f, 0.996703029f, 0.996804297f, 0.996902466f, 0.996997654f, 0.997089863f,
    0.99717927f, 0.997265935f, 0.997349977f, 0.997431397f, 0.997510314f, 0.997586846f,
    0.997660995f, 0.997732878f, 0.997802556f, 0.997870088f, 0.997935534f, 0.997999012f,
    0.998060524f, 0.998120129f, 0.998177886f, 0.998233914f, 0.998288214f, 0.998340845f,
    0.998391807f, 0.998441279f, 0.998489201f, 0.998535633f, 0.998580635f, 0.998624325f,
    0.998666584f, 0.998707592f, 0.998747349f, 0.998785853f, 0.998823166f, 0.998859346f,
    0.998894453f, 0.998928428f, 0.998961389f, 0.998993337f, 0.999024272f, 0.999054313f,
    0.9990834f, 0.999111593f, 0.999138892f, 0.999165356f, 0.999191046f, 0.999215901f,
    0.999240041f, 0.999263406f, 0.999286056f, 0.99930799f, 0.999329329f, 0.999349952f,
    0.999369919f, 0.999389291f, 0.999408066f, 0.999426305f, 0.999443948f, 0.999461055f,
    0.999477625f, 0.999493659f, 0.999509275f, 0.999524355f, 0.999538958f, 0.999553144f,
    0.999566913f, 0.999580204f, 0.999593139f, 0.999605656f, 0.999617815f, 0.999629557f,
    0.999640942f, 0.999651968f, 0.999662697f, 0.999673069f, 0.999683142f, 0.999692857f,
    0.999702334f, 0.999711454f, 0.999720335f, 0.999728978f, 0.999737322f, 0.999745369f,
    0.999753237f, 0.999760807f, 0.999768138f, 0.99977529f, 0.999782205f, 0.99978888f,
    0.999795377f, 0.999801695f, 0.999807775f, 0.999813735f, 0.999819458f, 0.999825001f,
    0.999830365f, 0.99983561f, 0.999840677f, 0.999845564f, 0.999850333f, 0.999854922f,
    0.999859393f, 0.999863684f, 0.999867916f, 0.999871969f, 0.999875903f, 0.999879718f,
    0.999883413f, 0.99988699f, 0.999890506f, 0.999893844f, 0.999897122f, 0.999900281f,
    0.999903321f, 0.999906301f, 0.999909222f, 0.999912024f, 0.999914706f, 0.999917328f,
    0.999919891f, 0.999922335f, 0.999924719f, 0.999927044f, 0.999929309f, 0.999931455f,
    0.9999336f, 0.999935627f, 0.999937594f, 0.999939501f, 0.999941349f, 0.999943197f,
    0.999944925f, 0.999946594f, 0.999948263f, 0.999949872f, 0.999951422f, 0.999952912f,
    0.999954343f, 0.999955773f, 0.999957085f, 0.999958456f, 0.999959707f, 0.999960959f,
    0.999962151f, 0.999963343f, 0.999964416f, 0.999965549f, 0.999966621f, 0.999967635f,
    0.999968648f, 0.999969602f, 0.999970496f, 0.999971449f, 0.999972284f, 0.999973178f,
    0.999974012f, 0.999974787f, 0.999975562f, 0.999976337f, 0.999977052f, 0.999977767f,
    0.999978423f, 0.999979079f, 0.999979734f, 0.99998039f, 0.999980986f, 0.999981582f,
    0.999982119f, 0.999982655f, 0.999983191f, 0.999983728f, 0.999984205f, 0.999984682f,
    0.999985158f, 0.999985635f, 0.999986053f, 0.999986529f, 0.999986947f, 0.999987304f,
    0.999987721f, 0.999988079f, 0.999988437f, 0.999988794f, 0.999989152f, 0.99998951f,
    0.999989808f, 0.999990106f, 0.999990404f, 0.999990702f, 0.999991f, 0.999991298f,
    0.999991536f, 0.999991834f, 0.999992073f, 0.999992311f, 0.999992549f, 0.999992788f,
    0.999993026f, 0.999993205f, 0.999993443f, 0.999993622f, 0.999993801f, 0.99999404f,
    0.999994218f, 0.999994397f, 0.999994576f, 0.999994695f, 0.999994874f, 0.999995053f,
    0.999995172f, 0.999995351f, 0.99999547f, 0.999995589f, 0.999995768f, 0.999995887f,
    0.999996006f, 0.999996126f, 0.999996245f, 0.999996364f, 0.999996483f, 0.999996603f,
    0.999996722f, 0.999996781f, 0.999996901f, 0.99999696f, 0.999997079f, 0.999997199f,
    0.999997258f, 0.999997318f, 0.999997437f, 0.999997497f, 0.999997556f, 0.999997675f,
    0.999997735f, 0.999997795f, 0.999997854f, 0.999997914f, 0.999997973f, 0.999998033f,
    0.999998093f, 0.999998152f, 0.999998212f, 0.999998271f, 0.999998331f, 0.999998391f,
    0.99999845f, 0.99999851f, 0.99999851f, 0.999998569f, 0.999998629f, 0.999998689f,
    0.999998689f, 0.999998748f, 0.999998808f, 0.999998808f, 0.999998868f, 0.999998868f,
    0.999998927f, 0.999998987f, 0.999998987f, 0.999999046f, 0.999999046f, 0.999999106f,
    0.999999106f, 0.999999166f, 0.999999166f, 0.999999166f, 0.999999225f, 0.999999225f,
    0.999999285f, 0.999999285f, 0.999999285f, 0.999999344f, 0.999999344f, 0.999999344f,
    0.999999404f, 0.999999404f, 0.999999404f, 0.999999464f, 0.999999464f, 0.999999464f,
    0.999999464f, 0.999999523f, 0.999999523f, 0.999999523f, 0.999999523f, 0.999999583f,
    0.999999583f, 0.999999583f, 0.999999583f, 0.999999642f, 0.999999642f, 0.999999642f,
    0.999999642f, 0.999999642f, 0.999999702f, 0.999999702f, 0.999999702f, 0.999999702f,
    0.999999702f, 0.999999702f, 0.999999702f, 0.999999762f, 0.999999762f, 0.999999762f,
    0.999999762f, 0.999999762f, 0.999999762f, 0.999999762f,
};
static inline float d9_tanh(float u)              /* drum909_dsp.h's: from the table */
{
    const float a = fm_fabsf(u) * D9_TANH_SCALE;
    float t;
    if (a >= (float)D9_TANH_N) {
        t = 1.0f;
    } else {
        const int32_t i = (int32_t)a;
        const float fr = a - (float)i;
        t = d9_tanh_tab[i] + (d9_tanh_tab[i + 1] - d9_tanh_tab[i]) * fr;
    }
    return u < 0.0f ? -t : t;
}

/* ---- drum909_dsp.h's blocks ---- */
static inline float d9_exp_small(float x)       /* exp(x) to about an ulp for |x| < 0.35 */
{
    float p;
    if (x < -0.35f || x > 0.35f)
        return fm_expf(x);
    p = 1.0f + x * (1.0f / 8.0f);
    p = 1.0f + x * (1.0f / 7.0f) * p;
    p = 1.0f + x * (1.0f / 6.0f) * p;
    p = 1.0f + x * (1.0f / 5.0f) * p;
    p = 1.0f + x * (1.0f / 4.0f) * p;
    p = 1.0f + x * (1.0f / 3.0f) * p;
    p = 1.0f + x * 0.5f * p;
    return 1.0f + x * p;
}
static inline int32_t d9_ceil(float x)
{
    int32_t i = (int32_t)x;
    return (float)i < x ? i + 1 : i;
}
static void d9_env_set(d9_env_t *e, float v)
{
    e->v = v;
    e->cur = v;
    e->v1 = v;
    e->left = 0;
}
/* exponentialRampToValueAtTime(v1, now + t1 samples) */
static void d9_env_exp(d9_env_t *e, float v1, float t1)
{
    float v0 = e->cur < D9_MIN_EXP ? D9_MIN_EXP : e->cur;
    if (v1 < D9_MIN_EXP)
        v1 = D9_MIN_EXP;
    if (!(t1 > 0.0f))
        t1 = 1.0f;
    e->v0 = v0;
    e->v1 = v1;
    e->t1 = t1;
    e->k = d9_ceil(t1) - 1;
    e->left = e->k;
    if (e->k <= 0) {
        e->left = 0;
        e->v = v1;
        e->cur = v0;
        return;
    }
    e->l2r = fm_log2f(v1 / v0);
    e->r = d9_exp_small(e->l2r * FM_LN2 / t1);
    e->v = v0;
    e->cur = v0;
}
static inline float d9_env_tick(d9_env_t *e)
{
    const float o = e->v;
    e->cur = o;
    if (e->left) {
        if (--e->left)
            e->v *= e->r;
        else
            e->v = e->v1;
    }
    return o;
}
static inline void d9_env_anchor(d9_env_t *e)     /* back onto the closed form, once a call */
{
    if (e->left)
        e->v = e->v0 * fm_exp2f(e->l2r * ((float)(e->k - e->left) / e->t1));
}
static void d9_biquad_set(d9_biquad_t *f, int type, float freq, float q)
{
    float fc = freq / (D9_SR * 0.5f), w0, sh, cosw0, sinw0, alpha, b0, b1, b2, a0, a1, a2, inv;
    if (fc >= 1.0f) fc = 0.9999f;
    if (fc <= 0.0f) fc = 0.0001f;
    w0 = FM_PI * fc;
    sh = fm_sinf(0.5f * w0);
    cosw0 = 1.0f - 2.0f * sh * sh;
    sinw0 = fm_sinf(w0);
    if (q < 0.0001f) q = 0.0001f;
    alpha = sinw0 / (2.0f * q);
    a0 = 1.0f + alpha;
    a1 = -2.0f * cosw0;
    a2 = 1.0f - alpha;
    if (type == D9_LP) {
        b0 = (1.0f - cosw0) * 0.5f; b1 = 1.0f - cosw0; b2 = (1.0f - cosw0) * 0.5f;
    } else if (type == D9_HP) {
        b0 = (1.0f + cosw0) * 0.5f; b1 = -(1.0f + cosw0); b2 = (1.0f + cosw0) * 0.5f;
    } else {
        b0 = alpha; b1 = 0.0f; b2 = -alpha;
    }
    inv = 1.0f / a0;
    f->b0 = b0 * inv; f->b1 = b1 * inv; f->b2 = b2 * inv;
    f->a1 = a1 * inv; f->a2 = a2 * inv;
    f->x1 = f->x2 = f->y1 = f->y2 = 0.0f;
}
static inline float d9_biquad_tick(d9_biquad_t *f, float in)
{
    const float y = f->b0 * in + f->b1 * f->x1 + f->b2 * f->x2 - f->a1 * f->y1 - f->a2 * f->y2;
    f->x2 = f->x1; f->x1 = in;
    f->y2 = f->y1; f->y1 = y;
    return y;
}
static inline void d9_biquad_flush(d9_biquad_t *f)
{
    f->x1 = fm_flush(f->x1); f->x2 = fm_flush(f->x2); f->y1 = fm_flush(f->y1); f->y2 = fm_flush(f->y2);
}
static inline uint32_t d9_inc(float hz) { return (uint32_t)(hz * D9_PHASE_PER_HZ); }
static inline float d9_tri(uint32_t ph)           /* 4p - 1 rising over the first half, 3 - 4p falling after */
{
    const uint32_t u = ph ^ (uint32_t)((int32_t)ph >> 31);
    return (float)(int32_t)u * (1.0f / 1073741824.0f) - 1.0f;
}
static inline float d9_diode16(float x) { return d9_tanh(1.6f * x) * D9_INV_TANH_1_6; }
static inline float d9_diode20(float x) { return d9_tanh(2.0f * x) * D9_INV_TANH_2_0; }
static void d9_shape_prep(d9_shape_t *s, float drive, int type)
{
    const float k = drive > 0.01f ? drive : 0.01f;
    s->type = type;
    s->k = k;
    s->g = k < 1.0f ? 1.0f / k : 1.0f;
    s->c1 = s->c2 = 0.0f;
    switch (type) {
    case 0: s->g = 1.0f / d9_tanh(k); break;
    case 2: s->c1 = 0.08f * k; s->c2 = k < 1.0f ? k : 1.0f; break;   /* SAT: m */
    case 3: {                                                       /* BFZ: m */
        float m = (k - 0.85f) / 1.15f;
        s->c1 = m < 0.0f ? 0.0f : (m > 1.0f ? 1.0f : m);
        s->c2 = k * 2.5f;
        break;
    }
    case 6:                                                          /* crush */
        s->c1 = 1.5f + 9.0f / k;
        s->c2 = k < 1.0f ? 1.0f : 1.0f + (k - 1.0f) * 1.7f;
        s->g = 1.0f / s->c1;
        break;
    default: break;
    }
}
static float d9_shape_other(const d9_shape_t *s, float x, float *st)   /* types 1..6 */
{
    const float k = s->k;
    switch (s->type) {
    case 1:                                                          /* asymmetric soft clip */
        return (d9_tanh(x * k + 0.35f) - 0.33638f) * 0.958f * s->g;
    case 5: {                                                        /* wavefolder */
        float v = x * k, body = x * k;
        int i;
        for (i = 0; i < 3; ++i) {
            if (v > 1.0f) v = 2.0f - v;
            if (v < -1.0f) v = -2.0f - v;
        }
        body = body > 1.0f ? 1.0f : body < -1.0f ? -1.0f : body;
        return (0.62f * v + 0.38f * body) * s->g;
    }
    case 6: {                                                        /* bitcrush: quantise + decimate */
        float q = fm_floorf(x * s->c1 + 0.5f) * s->g;
        st[1] += 1.0f;
        if (st[1] >= s->c2) { st[1] -= s->c2; st[0] = q; }
        return st[0];
    }
    case 2: {                                                        /* SAT */
        const float u = x * k + s->c1 * x * x, wet = u / (1.0f + fm_fabsf(u)), m = s->c2;
        return ((1.0f - 0.65f * m) * x * m + 0.65f * m * wet * 1.35f) * s->g;
    }
    case 3: {                                                        /* BFZ */
        const float u = x * s->c2 + 0.22f, wet = (u / (1.0f + fm_fabsf(u)) - 0.18033f) * 1.05f, m = s->c1;
        return (1.0f - m) * x + m * wet;
    }
    case 4: {                                                        /* PDIST */
        float u = x * k + 0.12f;
        const float y0 = 0.12f - (0.12f * 0.12f * 0.12f) / 3.0f;
        u = u > 1.0f ? 1.0f : u < -1.0f ? -1.0f : u;
        return ((u - u * u * u * (1.0f / 3.0f)) - y0) * (1.5f / 1.479f);
    }
    default:
        return d9_tanh(k * x) * s->g;
    }
}
static inline float d9_shape(const d9_shape_t *s, float x, float *st)
{
    if (s->type == 0)                                 /* diode: every voice's default */
        return d9_tanh(s->k * x) * s->g;
    return d9_shape_other(s, x, st);
}
static inline float d9_noise(uint32_t *s)         /* xorshift32, uniform [-1, 1) (9W9's wa_noise) */
{
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x;
    return (float)x * (1.0f / 2147483648.0f) - 1.0f;
}

/* ---- 9W9's pots (er99_pots.h): EXP min * (max / min)^t, LIN min + (max - min) t ---- */
static inline float pot_exp(float lo, float hi, float t) { return lo * fm_powf(hi / lo, t); }
static inline float pot_lin(float lo, float hi, float t) { return lo + (hi - lo) * t; }
static inline int pot_list(float t, int n)
{
    int i = (int)(t * 127.0f + 0.5f);
    return i < 0 ? 0 : i >= n ? n - 1 : i;
}
static inline float drive_of(float t) { return pot_exp(0.85f, 12.0f, t); }
static float d9_len(float ms, float lscale, int flags)   /* a decay with the kit's length and the step's */
{
    ms *= lscale;
    if (flags & D9_F_SHORT)
        ms = fm_minf(ms, 60.0f);
    if (flags & D9_F_LONG)
        ms *= 4.0f;
    return ms;
}

/* ---- the samplers' samples: the resource region (tools/gen_909res.py), read in place ---- */
#define D9S_MAGIC 0x3153394Eu                  /* "N9S1" */
#define D9S_BLOCK 32u
#define D9S_BLOCK_B 21u                        /* an exponent byte, 32 codes of 5 bits */
enum { D9S_HH, D9S_RIDE, D9S_CRASH, ND9S };
const uint8_t *(*d909_res_get)(uint32_t *len);
static uint32_t d9s_rd32(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static const uint8_t *d9s_find(int s, uint32_t *n)   /* sample s's blocks and length, 0 when absent */
{
    uint32_t len = 0, off, cnt;
    const uint8_t *p = d909_res_get ? d909_res_get(&len) : 0;
    *n = 0;
    if (!p || len < 8u + 8u * ND9S || d9s_rd32(p) != D9S_MAGIC || (p[6] | p[7] << 8) != D9S_BLOCK)
        return 0;
    cnt = (uint32_t)(p[4] | p[5] << 8);
    if ((uint32_t)s >= cnt || len < 8u + 8u * cnt)
        return 0;
    off = d9s_rd32(p + 8 + 8 * s);
    *n = d9s_rd32(p + 12 + 8 * s);
    if (off > len || (*n + D9S_BLOCK - 1u) / D9S_BLOCK * D9S_BLOCK_B + 1u > len - off) {
        *n = 0;                                    /* (its blocks and the padding byte past them: inside) */
        return 0;
    }
    return p + off;
}
int d909_res_ok(void)
{
    uint32_t n;
    int s;
    for (s = 0; s < ND9S; s++)
        if (!d9s_find(s, &n))
            return 0;
    return 1;
}
/* the block's step 2^(e/2), over 32768 and the codes' bias of 16 taken out at the read */
static const float d9s_step[32] = {
    3.05175781e-05f, 4.31583721e-05f, 6.10351562e-05f, 8.63167443e-05f, 1.22070312e-04f, 1.72633489e-04f,
    2.44140625e-04f, 3.45266977e-04f, 4.88281250e-04f, 6.90533954e-04f, 9.76562500e-04f, 1.38106791e-03f,
    1.95312500e-03f, 2.76213582e-03f, 3.90625000e-03f, 5.52427163e-03f, 7.81250000e-03f, 1.10485433e-02f,
    1.56250000e-02f, 2.20970865e-02f, 3.12500000e-02f, 4.41941731e-02f, 6.25000000e-02f, 8.83883461e-02f,
    1.25000000e-01f, 1.76776692e-01f, 2.50000000e-01f, 3.53553385e-01f, 5.00000000e-01f, 7.07106769e-01f,
    1.00000000e+00f, 1.41421354e+00f};
static inline float d9s_at(const uint8_t *blk, uint32_t i)   /* sample i, -1 .. 1 */
{
    const uint8_t *b = blk + (i >> 5) * D9S_BLOCK_B;
    const uint32_t bit = (i & 31u) * 5u, c = ((uint32_t)b[1 + (bit >> 3)] | (uint32_t)b[2 + (bit >> 3)] << 8) >> (bit & 7u);
    return (float)((int32_t)(c & 31u) - 16) * d9s_step[b[0] & 31u];
}

/* ---- the hits (drum909.c drum909_trigger and the voices' *_trigger, with drum909_init's panels) ---- */
void d909_start(d909_t *v, int which, const float *k, float semi, float lscale, int flags, uint32_t seed)
{
    const float tr = fm_exp2f(semi * (1.0f / 12.0f));
    uint8_t *z = (uint8_t *)v;
    uint32_t n;
    for (n = 0; n < sizeof *v; n++)
        z[n] = 0;
    v->which = (uint8_t)which;
    v->rng = seed | 1u;
    switch (which) {
    case D9_KICK: {                                   /* er99_bt_trigger, bd: the stock sweep, one shell */
        const float tau = pot_lin(6.0f, 32.0f, k[3]), m = k[4], pm = pot_exp(0.43f, 4.7f, k[0]);
        v->bt.decay = d9_len(pot_exp(100.0f, 4000.0f, k[1]), lscale, flags);
        v->bt.attack = k[2];
        d9_shape_prep(&v->shape, drive_of(k[5]), pot_list(k[6], 7));
        d9_biquad_set(&v->bt.dc_block, D9_HP, 20.0f, 0.7071f);
        d9_biquad_set(&v->bt.click_lp, D9_LP, 2500.0f, 0.7071f);
        v->bt.ph = v->bt.ph2 = D9_PH_QUARTER;
        v->bt.bd_base = D9_BD_BASE_HZ * (1.0f + m * (pm - 1.0f)) * tr;
        v->bt.bd_df = D9_BD_DF_PER_MS * tau * (1.0f + 1.2f * m) * tr;
        v->bt.bd_mult = d9_exp_small(-1.0f / (tau * D9_MS));
        v->bt.bd_phold = (int32_t)(D9_BD_PHOLD_MS * D9_MS);
        d9_env_set(&v->bt.amp, 1.0f);
        v->bt.amp_hold_left = (int32_t)(40.0f * D9_MS);
        d9_env_set(&v->bt.click_env, 1.0f);
        d9_env_exp(&v->bt.click_env, 0.00001f, 3.0f * D9_MS);
        v->bt.impulse = 2;
        v->out_gain = 0.999f * D9_ACCENT;             /* (Level pot 94) */
        v->mute = d9_ceil((v->bt.decay + 20.0f) * D9_MS);   /* (9W9: the hold not counted) */
        break;
    }
    case D9_SNARE: {                                  /* two shells and the wires (ENV4 noise); the click off */
        const float tune = pot_exp(130.0f, 320.0f, k[0]) * tr;
        v->bt.decay = d9_len(pot_lin(120.0f, 800.0f, k[1]), lscale, flags);
        v->bt.snappy = k[2];
        v->bt.noise_decay = d9_len(pot_exp(300.0f, 4000.0f, k[3]), lscale, flags);
        d9_shape_prep(&v->shape, drive_of(k[4]), pot_list(k[5], 7));
        d9_biquad_set(&v->bt.dc_block, D9_HP, 20.0f, 0.7071f);
        d9_biquad_set(&v->bt.noise_hpf, D9_HP, 1000.0f, 0.7071f);
        d9_biquad_set(&v->bt.noise_lpf, D9_LP, 6500.0f, 0.7071f);
        v->bt.ph = v->bt.ph2 = D9_PH_QUARTER;
        v->bt.ratio = 1.585f;
        v->bt.mix2 = 0.40f;
        v->bt.inv_mix = 1.0f / 1.40f;
        d9_env_set(&v->bt.pitch, tune * 1.045f);
        d9_env_exp(&v->bt.pitch, tune, 30.0f * D9_MS);
        d9_env_set(&v->bt.amp, 1.0f);
        d9_env_exp(&v->bt.amp, 0.00001f, v->bt.decay * D9_MS);
        if (v->bt.snappy > 0.0f) {
            d9_env_set(&v->bt.noise_env, 1.0f);
            v->bt.noise_hold = (int32_t)(24.0f * D9_MS);
        }
        v->out_gain = (58.0f / 127.0f * 1.35f) * D9_ACCENT;   /* (Level pot 58) */
        v->mute = d9_ceil((fm_maxf(v->bt.decay, v->bt.noise_decay) + 20.0f) * D9_MS);
        break;
    }
    case D9_TOM: {                                    /* er99_tom909: the low, mid or high tom's panel */
        static const float TUNE[3][2] = {{50.0f, 90.0f}, {80.0f, 125.0f}, {110.0f, 170.0f}};
        static const float DECAY[3][2] = {{80.0f, 2600.0f}, {70.0f, 2200.0f}, {60.0f, 2000.0f}};
        static const float SWEEP[3] = {200.0f, 180.0f, 160.0f};
        const int s = pot_list(k[3], 3);
        const float tune = pot_exp(TUNE[s][0], TUNE[s][1], k[0]) * tr;
        const float d1 = d9_len(pot_exp(DECAY[s][0], DECAY[s][1], k[1]), lscale, flags);
        int j;
        d9_shape_prep(&v->shape, drive_of(k[4]), pot_list(k[5], 7));
        for (j = 0; j < 3; ++j)
            v->tom.ph[j] = D9_PH_QUARTER;
        d9_biquad_set(&v->tom.noise_bp, D9_BP, 1200.0f, 1.2f);
        d9_biquad_set(&v->tom.dc_block, D9_HP, 20.0f, 0.7071f);
        d9_env_set(&v->tom.pitch, tune * 1.12f);
        d9_env_exp(&v->tom.pitch, tune, SWEEP[s] * D9_MS);
        d9_env_set(&v->tom.env[0], 1.0f);
        d9_env_exp(&v->tom.env[0], 0.00001f, d1 * D9_MS);
        d9_env_set(&v->tom.env[1], 0.55f);
        d9_env_exp(&v->tom.env[1], 0.00001f, 110.0f * D9_MS);
        d9_env_set(&v->tom.env[2], 0.45f);
        d9_env_exp(&v->tom.env[2], 0.00001f, 70.0f * D9_MS);
        v->tom.noise_level = k[2];
        d9_env_set(&v->tom.noise_env, 1.0f);
        d9_env_exp(&v->tom.noise_env, 0.00001f, 8.0f * D9_MS);
        v->out_gain = (52.0f / 127.0f * 1.35f) * D9_ACCENT;   /* (Level pot 52) */
        v->mute = d9_ceil((d1 + 60.0f) * D9_MS);
        break;
    }
    case D9_RIM: {
        const float tune = pot_exp(150.0f, 300.0f, k[0]) * tr, dec = d9_len(200.0f, lscale, flags);
        d9_shape_prep(&v->shape, drive_of(k[1]), pot_list(k[2], 7));
        d9_biquad_set(&v->rim.bp1, D9_BP, tune, 10.0f);
        d9_biquad_set(&v->rim.bp2, D9_BP, tune * 2.286f, 11.0f);
        d9_biquad_set(&v->rim.hp, D9_HP, 120.0f, 0.7071f);
        v->rim.noise_mix = 0.35f;
        v->rim.level = 103.0f / 127.0f * 1.35f;       /* (Level pot 103) */
        d9_env_set(&v->rim.amp, 1.0f);
        d9_env_exp(&v->rim.amp, 0.00001f, dec * D9_MS);
        v->rim.impulse = 2;
        v->out_gain = D9_ACCENT;
        v->mute = d9_ceil((dec + 20.0f) * D9_MS);
        break;
    }
    case D9_CH: case D9_OH: case D9_RIDE: case D9_CRASH: {   /* the sampler (er99 render_sampler; X0X's panels) */
        static const float DECAY[4][2] = {{15.0f, 300.0f}, {20.0f, 1200.0f}, {100.0f, 3000.0f}, {100.0f, 3000.0f}};
        static const float LEVEL[4] = {89.0f, 80.0f, 42.0f, 47.0f};   /* (their Level pots) */
        static const uint8_t SAMPLE[4] = {D9S_HH, D9S_HH, D9S_RIDE, D9S_CRASH};
        const int c = which - D9_CH;
        const float rate = pot_exp(0.25f, 4.0f, k[0]) * tr, drive = drive_of(k[2]);
        const float dec = d9_len(pot_exp(DECAY[c][0], DECAY[c][1], k[1]), lscale, flags);
        d9_shape_prep(&v->shape, drive, pot_list(k[3], 7));
        v->smp.pre = 1.0f + drive * 0.5f;
        v->smp.blk = d9s_find(SAMPLE[c], &v->smp.len);
        v->smp.inc = (uint32_t)rate;
        v->smp.incf = (uint32_t)((rate - (float)v->smp.inc) * 4294967296.0f);
        d9_env_set(&v->smp.out, LEVEL[c] / 127.0f * 1.35f);
        d9_env_exp(&v->smp.out, 0.00001f, dec * D9_MS);
        v->out_gain = D9_ACCENT;
        v->mute = v->smp.blk ? d9_ceil(dec * D9_MS) : 0;   /* (no samples: silent) */
        break;
    }
    default: {                                        /* the clap: four bursts, then the tail */
        const float tune = pot_exp(650.0f, 1400.0f, k[0]) * tr;
        d9_shape_prep(&v->shape, drive_of(k[2]), pot_list(k[3], 7));
        d9_biquad_set(&v->clap.bp, D9_BP, tune, 2.0f);
        d9_biquad_set(&v->clap.hp, D9_HP, 400.0f, 0.7071f);
        d9_env_set(&v->clap.burst, 0.0f);
        d9_env_set(&v->clap.tail, 0.0f);
        v->clap.spread = 12.0f;
        v->clap.burst_decay = 50.0f;
        v->clap.tail_decay = d9_len(pot_exp(120.0f, 1000.0f, k[1]), lscale, flags);
        v->clap.tail_level = 0.58f;
        v->out_gain = (122.0f / 127.0f * 1.35f) * D9_ACCENT;   /* (Level pot 122) */
        v->mute = d9_ceil((v->clap.tail_decay + v->clap.spread * 4.0f + 40.0f) * D9_MS);
        break;
    }
    }
}

/* ---- the renders (drum909.c d9_render_*), into out[] ---- */
static void render_kick(d909_t *v, float *out, int m)
{
    int i;
    const float attack = v->bt.attack, g = v->out_gain;
    d9_env_anchor(&v->bt.amp);
    d9_env_anchor(&v->bt.click_env);
    for (i = 0; i < m; ++i) {
        const float f = v->bt.bd_base + v->bt.bd_df;
        float o, amp, body, click = 0.0f;
        if (v->bt.bd_phold > 0)
            --v->bt.bd_phold;
        else
            v->bt.bd_df *= v->bt.bd_mult;
        if (v->bt.amp_hold_left > 0 && --v->bt.amp_hold_left == 0)
            d9_env_exp(&v->bt.amp, 0.00001f, v->bt.decay * D9_MS);
        amp = d9_env_tick(&v->bt.amp);
        v->bt.ph += d9_inc(f);
        o = d9_diode16(d9_tri(v->bt.ph));
        o += 0.05f * o * o;
        o = d9_biquad_tick(&v->bt.dc_block, o);
        body = d9_shape(&v->shape, o, v->crush_st) * amp;
        if (v->bt.impulse > 0 || v->bt.click_env.left) {   /* the beater: past its 3 ms, skipped */
            if (v->bt.impulse > 0) {
                click += 1.0f;
                v->bt.impulse--;
            }
            click += d9_biquad_tick(&v->bt.click_lp, d9_noise(&v->rng));
            click *= d9_env_tick(&v->bt.click_env) * attack;
        }
        out[i] = (body + click) * g;
    }
    d9_biquad_flush(&v->bt.dc_block);
}
static void render_snare(d909_t *v, float *out, int m)
{
    int i;
    const float ratio = v->bt.ratio, mix2 = v->bt.mix2, inv_mix = v->bt.inv_mix, g = v->out_gain;
    d9_env_anchor(&v->bt.pitch);
    d9_env_anchor(&v->bt.amp);
    d9_env_anchor(&v->bt.noise_env);
    for (i = 0; i < m; ++i) {
        const float f = d9_env_tick(&v->bt.pitch);
        float amp = d9_env_tick(&v->bt.amp), o, y;
        amp = amp > D9_VCA_VT ? (amp - D9_VCA_VT) * D9_VCA_NORM : 0.0f;
        v->bt.ph += d9_inc(f);
        v->bt.ph2 += d9_inc(f * ratio);
        o = d9_diode20(d9_tri(v->bt.ph));
        o += d9_diode20(d9_tri(v->bt.ph2)) * mix2;
        o *= inv_mix;
        y = d9_shape(&v->shape, o, v->crush_st) * amp;
        if (v->bt.snappy > 0.0f && !v->bt.noise_gated) {
            float env;
            if (v->bt.noise_hold > 0 && --v->bt.noise_hold == 0)
                d9_env_exp(&v->bt.noise_env, 0.00001f, v->bt.noise_decay * 0.7f * D9_MS);
            env = d9_env_tick(&v->bt.noise_env);
            if (v->bt.noise_hold <= 0) {
                env = env > D9_VCA_VT ? (env - D9_VCA_VT) * D9_VCA_NORM : 0.0f;
                if (env <= 0.0f)
                    v->bt.noise_gated = 1;
            }
            y += d9_biquad_tick(&v->bt.noise_lpf, d9_biquad_tick(&v->bt.noise_hpf, d9_noise(&v->rng))) * env *
                 v->bt.snappy * D9_SNAPPY_MIX;
        }
        out[i] = y * g;
    }
    d9_biquad_flush(&v->bt.noise_hpf);
    d9_biquad_flush(&v->bt.noise_lpf);
}
static void render_tom(d909_t *v, float *out, int m)
{
    int i;
    const float stick_g = v->tom.noise_level * 1.5f, g = v->out_gain;
    d9_env_anchor(&v->tom.pitch);
    d9_env_anchor(&v->tom.env[0]);
    d9_env_anchor(&v->tom.env[1]);
    d9_env_anchor(&v->tom.env[2]);
    d9_env_anchor(&v->tom.noise_env);
    for (i = 0; i < m; ++i) {
        const float f = d9_env_tick(&v->tom.pitch);
        float o = 0.0f, stick = 0.0f, y;
        v->tom.ph[0] += d9_inc(f);
        o += d9_diode20(d9_tri(v->tom.ph[0])) * d9_env_tick(&v->tom.env[0]);
        if (v->tom.env[1].left) {
            v->tom.ph[1] += d9_inc(f * 1.50f);
            o += d9_diode20(d9_tri(v->tom.ph[1])) * d9_env_tick(&v->tom.env[1]);
        }
        if (v->tom.env[2].left) {
            v->tom.ph[2] += d9_inc(f * 2.75f);
            o += d9_diode20(d9_tri(v->tom.ph[2])) * d9_env_tick(&v->tom.env[2]);
        }
        o *= 0.55f;
        if (v->tom.noise_env.left)
            stick = d9_biquad_tick(&v->tom.noise_bp, d9_noise(&v->rng)) * d9_env_tick(&v->tom.noise_env) * stick_g;
        y = d9_shape(&v->shape, o + stick, v->crush_st);
        y = d9_biquad_tick(&v->tom.dc_block, y);
        out[i] = y * g;
    }
    d9_biquad_flush(&v->tom.dc_block);
}
static void render_rim(d909_t *v, float *out, int m)
{
    int i;
    const float g = v->rim.level * v->out_gain;
    d9_env_anchor(&v->rim.amp);
    for (i = 0; i < m; ++i) {
        float exc = 0.0f, a, y;
        if (v->rim.impulse > 0) {
            exc += 1.0f + d9_noise(&v->rng) * v->rim.noise_mix;
            v->rim.impulse--;
        }
        a = d9_env_tick(&v->rim.amp);
        y = d9_biquad_tick(&v->rim.bp1, exc) * 60.0f + d9_biquad_tick(&v->rim.bp2, exc) * 32.0f;
        y = d9_shape(&v->shape, y * a, v->crush_st);
        y = d9_biquad_tick(&v->rim.hp, y);
        out[i] = y * g;
    }
    d9_biquad_flush(&v->rim.bp1);
    d9_biquad_flush(&v->rim.bp2);
    d9_biquad_flush(&v->rim.hp);
}
static void render_clap(d909_t *v, float *out, int m)
{
    static const float AMP[4] = {1.22f, 1.35f, 1.68f, 1.0f};
    int i;
    const float g = v->out_gain;
    d9_env_anchor(&v->clap.burst);
    d9_env_anchor(&v->clap.tail);
    for (i = 0; i < m; ++i) {
        float env, y;
        if (v->clap.pulse_index < 4) {
            if (v->clap.next_pulse <= 0.0f) {
                d9_env_set(&v->clap.burst, AMP[v->clap.pulse_index]);
                d9_env_exp(&v->clap.burst, 0.00001f, v->clap.burst_decay * D9_MS);
                if (v->clap.pulse_index == 3) {
                    d9_env_set(&v->clap.tail, v->clap.tail_level);
                    d9_env_exp(&v->clap.tail, 0.00001f, v->clap.tail_decay * D9_MS);
                }
                v->clap.pulse_index++;
                v->clap.next_pulse = v->clap.spread * D9_MS;
            }
            v->clap.next_pulse -= 1.0f;
        }
        env = d9_env_tick(&v->clap.burst) + d9_env_tick(&v->clap.tail);
        y = d9_biquad_tick(&v->clap.bp, d9_noise(&v->rng)) * env;
        y = d9_shape(&v->shape, y, v->crush_st);
        y = d9_biquad_tick(&v->clap.hp, y);
        out[i] = y * g;
    }
    d9_biquad_flush(&v->clap.bp);
    d9_biquad_flush(&v->clap.hp);
}

static void render_smp(d909_t *v, float *out, int m)
{
    int i;
    const uint8_t *blk = v->smp.blk;
    const uint32_t len = v->smp.len;
    const float g = v->out_gain, pre = v->smp.pre;
    d9_env_anchor(&v->smp.out);
    for (i = 0; i < m; ++i) {
        float y = 0.0f;
        if (v->smp.pos < len) {                       /* linear, as 9W9's playbackRate */
            const float a = d9s_at(blk, v->smp.pos), b = v->smp.pos + 1u < len ? d9s_at(blk, v->smp.pos + 1u) : 0.0f;
            const uint32_t f2 = v->smp.frac + v->smp.incf;
            y = a + (b - a) * ((float)v->smp.frac * (1.0f / 4294967296.0f));
            v->smp.pos += v->smp.inc + (f2 < v->smp.frac);
            v->smp.frac = f2;
        }
        y *= d9_env_tick(&v->smp.out);
        out[i] = d9_shape(&v->shape, y * pre, v->crush_st) * g;   /* (9W9: drive always above 0.25 on its pot) */
    }
}

int d909_render(d909_t *v, float *out, int n)
{
    int m = n < v->mute ? n : v->mute, i;
    if (m > 0) {
        switch (v->which) {
        case D9_KICK: render_kick(v, out, m); break;
        case D9_SNARE: render_snare(v, out, m); break;
        case D9_TOM: render_tom(v, out, m); break;
        case D9_RIM: render_rim(v, out, m); break;
        case D9_CLAP: render_clap(v, out, m); break;
        default: render_smp(v, out, m); break;
        }
        v->mute -= m;
    }
    for (i = m < 0 ? 0 : m; i < n; i++)
        out[i] = 0.0f;
    return v->mute > 0;
}
