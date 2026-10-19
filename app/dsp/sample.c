/* SPDX-License-Identifier: GPL-3.0-only */
/* LOOM-8 samples (sample.h): the factory bank, the user bank, the IMA ADPCM decoder. The decoder is the
 * standard IMA one (as X0X's breaks.c, adapted from Felucca's eng_sample.c), run a block at a time from
 * the block's checkpoint. */
#include "sample.h"
#include "samples.h"                      /* build/gen: tools/gen_samples.py */
_Static_assert(SMP_FACT_BREAK == SMP_BREAK, "the break's place in the factory bank (sample.h SMP_BREAK)");

static const int16_t IMA_STEP[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97,
    107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
    876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871,
    5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385,
    24623, 27086, 29794, 32767};
static const int8_t IMA_IDX[8] = {-1, -1, -1, -1, 2, 4, 6, 8};

static smp_t fact[SMP_NFACT];
static uint8_t fact_ready;
static smp_t user[SMP_NUSER];
static volatile uint8_t user_n;
static uint32_t user_bytes;
static const uint8_t *user_reg[SMP_NREG];
static char empty_name[SMP_NUSER][SMP_NAME];

static void fact_init(void)
{
    int i, k;
    for (i = 0; i < SMP_NFACT; i++) {
        smp_t *s = &fact[i];
        s->adp = SMP_FACT_ADP + SMP_FACT[i].adp;
        s->adp2 = 0;
        s->ck = SMP_FACT_CK + SMP_FACT[i].ck;
        s->n = SMP_FACT[i].n;
        s->split = s->n;
        s->rate = SMP_FACT[i].rate;
        s->ls = SMP_FACT[i].ls;
        s->le = SMP_FACT[i].le;
        s->root = SMP_FACT[i].root;
        s->bars = SMP_FACT[i].bars;
        for (k = 0; k < SMP_NAME - 1 && SMP_FACT[i].name[k]; k++)
            s->name[k] = SMP_FACT[i].name[k];
        s->name[k] = 0;
    }
    for (i = 0; i < SMP_NUSER; i++) {           /* "user 1" .. "user 32" */
        char *e = empty_name[i];
        const char *w = "user ";
        while (*w)
            *e++ = *w++;
        if (i + 1 >= 10)
            *e++ = (char)('0' + (i + 1) / 10);
        *e++ = (char)('0' + (i + 1) % 10);
        *e = 0;
    }
    fact_ready = 1;
}

int smp_slots(void) { return SMP_NFACT + SMP_NUSER; }
int smp_factory(void) { return SMP_NFACT; }
int smp_user_count(void) { return user_n; }
uint32_t smp_user_bytes(void) { return user_n ? user_bytes : 0u; }

const smp_t *smp_get(int i)
{
    if (!fact_ready)
        fact_init();
    if (i >= 0 && i < SMP_NFACT)
        return &fact[i];
    i -= SMP_NFACT;
    if (i >= 0 && i < (int)user_n)
        return &user[i];
    return 0;
}

const char *smp_name(int i)
{
    const smp_t *s = smp_get(i);
    if (s)
        return s->name;
    i -= SMP_NFACT;
    return i >= 0 && i < SMP_NUSER ? empty_name[i] : "-";
}

/* n IMA steps from the state (*pred, *idx) at sample i; the samples into out (0: only the state moves) */
static void ima_run(const uint8_t *a, uint32_t i, uint32_t n, int32_t *ppred, int32_t *pidx, int16_t *out)
{
    int32_t pred = *ppred, idx = *pidx;
    uint32_t k;
    for (k = 0; k < n; k++, i++) {
        uint32_t code = (uint32_t)(a[i >> 1] >> ((i & 1u) * 4u)) & 15u;
        int32_t step = IMA_STEP[idx], vd = step >> 3;
        if (code & 4u)
            vd += step;
        if (code & 2u)
            vd += step >> 1;
        if (code & 1u)
            vd += step >> 2;
        pred = code & 8u ? pred - vd : pred + vd;
        pred = pred > 32767 ? 32767 : pred < -32768 ? -32768 : pred;
        idx += IMA_IDX[code & 7u];
        idx = idx < 0 ? 0 : idx > 88 ? 88 : idx;
        if (out)
            out[k] = (int16_t)pred;
    }
    *ppred = pred;
    *pidx = idx;
}

/* as ima_run over the sample's ADPCM, which may go on in a second window from sample s->split (even) */
static void ima_span(const smp_t *s, uint32_t i, uint32_t n, int32_t *ppred, int32_t *pidx, int16_t *out)
{
    uint32_t k;
    if (i + n <= s->split) {
        ima_run(s->adp, i, n, ppred, pidx, out);
        return;
    }
    if (i >= s->split) {
        ima_run(s->adp2, i - s->split, n, ppred, pidx, out);
        return;
    }
    k = s->split - i;                                /* across the windows' seam */
    ima_run(s->adp, i, k, ppred, pidx, out);
    ima_run(s->adp2, 0, n - k, ppred, pidx, out ? out + k : 0);
}

void smp_fill(smp_cache_t *c, const smp_t *s, int32_t w)
{
    uint32_t n = s->n > (uint32_t)w ? s->n - (uint32_t)w : 0u, k;
    int32_t pred, idx;
    if (n > SMP_WIN + 1u)
        n = SMP_WIN + 1u;
    if (c->s == s && w == c->w + SMP_WIN && n) {     /* the next window: on from where the last one stopped (its
                                                       * last sample is this one's first) */
        pred = c->pred;
        idx = c->idx;
        c->b[0] = c->b[SMP_WIN];
        ima_span(s, (uint32_t)w + 1u, n - 1u, &pred, &idx, c->b + 1);
    } else {                                         /* from the checkpoint before it */
        uint32_t ck = s->ck[(uint32_t)w / SMP_BLK], skip = (uint32_t)w % SMP_BLK;
        pred = (int16_t)(ck & 0xFFFFu);
        idx = (int32_t)(ck >> 16);
        if (idx > 88)
            idx = 88;
        if (skip)
            ima_span(s, (uint32_t)w - skip, skip, &pred, &idx, 0);
        ima_span(s, (uint32_t)w, n, &pred, &idx, c->b);
    }
    for (k = n; k < SMP_WIN + 1u; k++)
        c->b[k] = 0;
    c->w = w;
    c->s = s;
    c->pred = (int16_t)pred;
    c->idx = (uint8_t)idx;
}

int smp_play(smp_cache_t *c, const smp_t *s, int32_t *ppos, float *pfr, float inc, int dir, int32_t lo, int32_t hi,
             float *out, int m)
{
    int32_t pos = *ppos, base = c->s == s && c->w >= 0 ? c->w : -0x40000000;
    const int16_t *b = c->b;
    float fr = *pfr;
    int i;
    if (hi > (int32_t)s->n)
        hi = (int32_t)s->n;
    if (lo < 0)
        lo = 0;
    if (dir > 0) {
        for (i = 0; i < m; i++) {
            uint32_t k;
            if (pos < lo || pos >= hi)
                break;
            k = (uint32_t)(pos - base);
            if (k >= SMP_WIN) {                      /* the next window (b[SMP_WIN] is its first: the interpolation's) */
                base = pos & ~(SMP_WIN - 1);
                smp_fill(c, s, base);
                k = (uint32_t)(pos - base);
            }
            out[i] = ((float)b[k] + (float)(b[k + 1] - b[k]) * fr) * (1.0f / 32768.0f);
            fr += inc;
            if (fr >= 1.0f) {
                int32_t w = (int32_t)fr;
                pos += w;
                fr -= (float)w;
            }
        }
    } else {
        for (i = 0; i < m; i++) {
            uint32_t k;
            if (pos < lo || pos >= hi)
                break;
            k = (uint32_t)(pos - base);
            if (k >= SMP_WIN) {
                base = pos & ~(SMP_WIN - 1);
                smp_fill(c, s, base);
                k = (uint32_t)(pos - base);
            }
            out[i] = ((float)b[k] + (float)(b[k + 1] - b[k]) * fr) * (1.0f / 32768.0f);
            fr -= inc;
            if (fr < 0.0f) {                         /* floor of a small negative number */
                int32_t w = (int32_t)fr;
                if ((float)w > fr)
                    w--;
                pos += w;
                fr -= (float)w;
            }
        }
    }
    *ppos = pos;
    *pfr = fr;
    return i;
}

/* ---- the user bank ---- */
uint32_t smp_crc32_more(uint32_t c, const uint8_t *p, uint32_t n)   /* zlib CRC-32, 4 bits a step */
{
    static const uint32_t T[16] = {
        0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu, 0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u, 0x5005713Cu,
        0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu, 0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu};
    c = ~c;
    while (n--) {
        c ^= *p++;
        c = (c >> 4) ^ T[c & 15u];
        c = (c >> 4) ^ T[c & 15u];
    }
    return ~c;
}
uint32_t smp_crc32(const uint8_t *p, uint32_t n) { return smp_crc32_more(0u, p, n); }

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

void smp_user_attach(const uint8_t *const *reg)
{
    for (int r = 0; r < SMP_NREG; r++)
        user_reg[r] = reg ? reg[r] : 0;
}
void smp_user_drop(void) { user_n = 0; }

/* bank bytes from virtual offset v: where they are and how many follow in v's window (0: past the bank, or the
 * window not attached) */
static const uint8_t *vptr(uint32_t v, uint32_t *room)
{
    uint32_t off;
    int r = smp_reg_of(v, &off);
    if (r < 0 || !user_reg[r]) {
        *room = 0;
        return 0;
    }
    *room = smp_reg_off(r) + smp_reg_size(r) - off;
    return user_reg[r] + (off - smp_reg_off(r));
}

int smp_bank_crc(uint32_t len, uint32_t *crc)
{
    uint32_t v = SMP_BANK_HDR, c = 0, room;
    if (len > SMP_BANK_SIZE - SMP_BANK_HDR)
        return -1;
    while (len) {
        const uint8_t *p = vptr(v, &room);
        if (!p)
            return -1;
        if (room > len)
            room = len;
        c = smp_crc32_more(c, p, room);
        v += room;
        len -= room;
    }
    *crc = c;
    return 0;
}

/* the header (tools/sampleio.py user_bank): magic, version 2, count, data length, CRC, the bank's size it was
 * made for, then up to 32 entries of 40 bytes from byte 64: name (12), ADPCM, checkpoints (virtual offsets in
 * the bank), samples, loop start, loop end, root, flags, bars, a spare, rate (Hz, 16 bits), two spare. Each entry
 * checked against the data (its checkpoints in one window, its ADPCM in one or running on into the next), the
 * data against its CRC */
int smp_user_scan(void)
{
    const uint8_t *h;
    uint32_t cnt, len, i, room, crc, end;
    user_n = 0;
    if (!fact_ready)
        fact_init();
    h = vptr(0, &room);
    if (!h || room < SMP_BANK_HDR || rd32(h) != SMP_BANK_MAGIC || h[4] != SMP_BANK_VER || h[5] != 0 ||
        rd32(h + 16) != SMP_BANK_SIZE)
        return 0;
    cnt = h[6];
    len = rd32(h + 8);
    if (!cnt || cnt > SMP_NUSER || len > SMP_BANK_SIZE - SMP_BANK_HDR)
        return 0;
    if (smp_bank_crc(len, &crc) || crc != rd32(h + 12))
        return 0;
    end = SMP_BANK_HDR + len;
    for (i = 0; i < cnt; i++) {
        const uint8_t *e = h + SMP_ENT0 + i * SMP_ENT;
        smp_t *s = &user[i];
        uint32_t adp = rd32(e + 12), ck = rd32(e + 16), n = rd32(e + 20), ls = rd32(e + 24), le = rd32(e + 28), k;
        uint32_t nb = (n + 1u) / 2u, nck = ((n + SMP_BLK - 1u) / SMP_BLK) * 4u;
        uint32_t rate = (uint32_t)e[36] | (uint32_t)e[37] << 8;
        if (!n || n > 2u * len || adp < SMP_BANK_HDR || adp > end || nb > end - adp || (ck & 3u) ||
            ck < SMP_BANK_HDR || ck > end || nck > end - ck || le > n || ls > le || e[32] > 127u ||
            rate < SMP_RATE_MIN || rate > SMP_RATE_MAX)
            return 0;
        s->ck = (const uint32_t *)(const void *)vptr(ck, &room);
        if (!s->ck || room < nck)                    /* its checkpoints in one window */
            return 0;
        s->adp = vptr(adp, &room);
        if (!s->adp)
            return 0;
        s->split = n;
        s->adp2 = 0;
        if (room < nb) {                             /* on into the next window, once */
            uint32_t r2;
            s->adp2 = vptr(adp + room, &r2);
            if (!s->adp2 || r2 < nb - room)
                return 0;
            s->split = room * 2u;
        }
        s->n = n;
        s->ls = ls;
        s->le = le;
        s->rate = (uint16_t)rate;
        s->root = e[32];
        s->bars = e[34] <= SMP_BARS_MAX ? e[34] : 0u;
        for (k = 0; k < SMP_NAME - 1 && e[k] >= 32u && e[k] < 127u; k++)
            s->name[k] = (char)e[k];
        s->name[k] = 0;
        if (!k) {
            s->name[0] = '?';
            s->name[1] = 0;
        }
    }
    user_bytes = len;
    user_n = (uint8_t)cnt;
    return (int)cnt;
}
