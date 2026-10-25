/* SPDX-License-Identifier: MPL-2.0 */
/* A small LZ77 for the project's patterns, song and motion (project.c), which are mostly empty
 * steps and repeats: 64 steps x 88 patterns plus the lanes are ~75 KB raw and a few KB packed.
 *
 * Stream: a control byte c, then
 *   c < 0x80   c + 1 literal bytes follow (1..128);
 *   c >= 0x80  a match of (c & 0x7F) + LZ_MIN bytes (3..130), then its distance - 1 as a u16
 *              (little endian), copied a byte at a time: a distance shorter than the length
 *              repeats, so a run of one byte is a literal and a match at distance 1.
 *
 * The compressor writes through a sink (a run of bytes at a time), so its output can go to the
 * flash objects one chunk at a time, and reads its input in place. It remembers the last position
 * of each 3-byte hash (lz_head, 4 KB: its low 16 bits, the rest from where it is now) and tries the byte before too (runs). The decompressor reads
 * a byte at a time from a source and writes into place. No allocation; inputs up to 16 MB (a match
 * reaches at most 64 KB back: the distance is a u16; a farther candidate is passed over). */
#include <stdint.h>
#include "plat.h"

#define LZ_MIN 3u
#define LZ_MAXLEN (0x7Fu + LZ_MIN)
#define LZ_MAXLIT 128u
#define LZ_HBITS 11
#define LZ_NONE 0xFFFFFFFFu
#define LZ_MAXDIST 65536u

typedef struct {
    int (*put)(void *ctx, const uint8_t *p, uint32_t n);   /* 0 = taken; else the compression stops */
    void *ctx;
} lz_sink_t;

typedef struct {
    int (*get)(void *ctx);                                  /* the next byte, -1 = none left */
    void *ctx;
} lz_src_t;

/* a position's low 16 bits: a match reaches 64 KB back at most. On the FM-1 in the screen's canvas (gfx.c cv_px,
 * 57 KB), which nothing reads between frames: every page fills it (cv_begin) before it sends it, and the
 * compressor runs in the main loop, never in a page's drawing, after the last transfer (lcd_sync). 4 KB of the
 * pool for Bluetooth's (ble-spike) */
#ifdef PLAT_HOST
static uint16_t lz_head[1u << LZ_HBITS];
#define lz_head_ready()
#else
_Static_assert(CV_MAX * sizeof cv_px[0] >= (1u << LZ_HBITS) * sizeof(uint16_t), "lz_head fits the canvas");
#define lz_head ((uint16_t *)(void *)cv_px)
#define lz_head_ready() lcd_sync()
#endif

static uint32_t lz_cand(uint32_t i, uint32_t h)   /* the last position of hash h before i, LZ_NONE none (a stale one
                                                   * is only a poorer candidate: lz_match_len compares the bytes) */
{
    uint32_t c = (i & ~(LZ_MAXDIST - 1u)) | lz_head[h];
    if (c < i)
        return c;
    return i >= LZ_MAXDIST ? c - LZ_MAXDIST : LZ_NONE;
}

static uint32_t lz_hash(const uint8_t *p)
{
    return (((uint32_t)p[0] << 16 | (uint32_t)p[1] << 8 | p[2]) * 2654435761u) >> (32 - LZ_HBITS);
}

static uint32_t lz_match_len(const uint8_t *in, uint32_t n, uint32_t cand, uint32_t i)
{
    uint32_t len = 0;
    while (len < LZ_MAXLEN && i + len < n && in[cand + len] == in[i + len])
        len++;
    return len;
}

static int lz_literals(lz_sink_t *o, const uint8_t *p, uint32_t n)
{
    while (n) {
        uint32_t k = n > LZ_MAXLIT ? LZ_MAXLIT : n;
        uint8_t c = (uint8_t)(k - 1u);
        if (o->put(o->ctx, &c, 1) || o->put(o->ctx, p, k))
            return -1;
        p += k;
        n -= k;
    }
    return 0;
}

/* 0 = all of in[0..n) went to the sink; -1 = the sink refused (or n is too big) */
static int lz_compress(const uint8_t *in, uint32_t n, lz_sink_t *o)
{
    uint32_t i = 0, lit = 0, k;
    if (n >= 0x1000000u)
        return -1;
    lz_head_ready();                          /* (the canvas: the screen's last transfer done) */
    for (k = 0; k < (1u << LZ_HBITS); k++)
        lz_head[k] = 0xFFFFu;                 /* (before 64 KB: none; after, a candidate the bytes check) */
    while (i < n) {
        uint32_t best = 0, dist = 0;
        if (i + LZ_MIN <= n) {
            uint32_t h = lz_hash(in + i), c = lz_cand(i, h), len;
            lz_head[h] = (uint16_t)i;
            if (c != LZ_NONE && i - c <= LZ_MAXDIST && (len = lz_match_len(in, n, c, i)) > best) {
                best = len;
                dist = i - c;
            }
            if (i && (len = lz_match_len(in, n, i - 1u, i)) > best) {   /* a run */
                best = len;
                dist = 1;
            }
        }
        if (best < LZ_MIN) {
            i++;
            continue;
        }
        if (lz_literals(o, in + lit, i - lit))
            return -1;
        {
            uint8_t m[3] = {(uint8_t)(0x80u | (best - LZ_MIN)), (uint8_t)(dist - 1u), (uint8_t)((dist - 1u) >> 8)};
            if (o->put(o->ctx, m, 3))
                return -1;
        }
        for (k = 1; k < best && i + k + LZ_MIN <= n; k++)       /* the positions inside the match */
            lz_head[lz_hash(in + i + k)] = (uint16_t)(i + k);
        i += best;
        lit = i;
    }
    return lz_literals(o, in + lit, i - lit);
}

/* 0 = exactly n bytes came out; -1 = the stream ended early or does not fit */
static int lz_decompress(lz_src_t *s, uint8_t *out, uint32_t n)
{
    uint32_t w = 0;
    while (w < n) {
        int c = s->get(s->ctx), b;
        uint32_t len, dist;
        if (c < 0)
            return -1;
        if (c < 0x80) {
            for (len = (uint32_t)c + 1u; len; len--) {
                if (w >= n || (b = s->get(s->ctx)) < 0)
                    return -1;
                out[w++] = (uint8_t)b;
            }
            continue;
        }
        len = ((uint32_t)c & 0x7Fu) + LZ_MIN;
        if ((b = s->get(s->ctx)) < 0)
            return -1;
        dist = (uint32_t)b;
        if ((b = s->get(s->ctx)) < 0)
            return -1;
        dist |= (uint32_t)b << 8;
        dist++;
        if (dist > w || len > n - w)
            return -1;
        for (; len; len--, w++)
            out[w] = out[w - dist];
    }
    return 0;
}
