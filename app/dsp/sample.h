/* SPDX-License-Identifier: GPL-3.0-only */
/* LOOM-8 samples: the factory bank compiled into the image (build/gen/samples.h, tools/gen_samples.py, mono IMA
 * ADPCM at 22050 Hz) and the user bank in flash (216 KB over three windows, uploaded over USB with
 * tools/loom_samples.py), mono IMA ADPCM at each sample's own rate (4000..48000 Hz: the tool offers 11025, 16000,
 * 22050 and 32000), read in place (the user bank through the XIP plaintext window). One index space:
 * 0..SMP_NFACT-1 the factory samples, then SMP_NUSER user slots (empty ones play nothing).
 *
 * Every SMP_BLK samples a checkpoint holds the decoder's state, so a voice reads any sample through a
 * small cache (smp_cache_t, 80 bytes: a window of SMP_WIN + 1 decoded samples and the decoder's state after
 * it). Forward, the next window carries on from that state (32 IMA steps per 32 samples); backward or after
 * a jump (a loop, a start anywhere) it starts again from the window's checkpoint (33 or 65 steps). The
 * format and the user bank's layout: tools/sampleio.py.
 *
 * The user bank's windows (SMP_REG*): the data area from the projects' end to the panel calibration, and
 * fm1-base's two extra windows (plat.h PLAT_XDATA1, PLAT_XDATA0), in that order. An offset in the bank is
 * virtual: it runs through the windows end to end (0 = the header, at 0xC1000). A sample's checkpoints lie
 * in one window; its ADPCM may run on from one window into the next (once: smp_t adp2 / split). In the two
 * extra windows no sector holds an update record at 0xF00 (plat.h; tools/sampleio.py keeps "AT" from bytes
 * 0xF06..0xF07, the commit refuses a bank that has it there).
 *
 * Threads: smp_get / smp_at run in the audio interrupt; smp_user_drop / smp_user_scan in the main loop
 * (the upload). A drop makes every user sample vanish at once (smp_get returns 0 from then on: a voice
 * reading one stops) before the flash under them is erased. */
#pragma once
#include <stdint.h>

#define SMP_RATE 22050                    /* the factory samples' rate (a user sample states its own) */
#define SMP_RATE_MIN 4000u                /* a user sample's rate, Hz */
#define SMP_RATE_MAX 48000u
#define SMP_BLK 64                        /* samples per checkpoint (a power of two) */
#define SMP_WIN 32                        /* samples a cache window holds (and the next one's first) */
#define SMP_NUSER 32                      /* the user bank's slots (user 1 is index SMP_NFACT, as before) */
#define SMP_NAME 12
#define SMP_NREG 3                        /* the bank's windows, in the order it fills them */
#define SMP_REG0 0xC1000u                 /* the data area after the projects, up to the calibration: 108 KB */
#define SMP_REG0_SIZE 0x1B000u
#define SMP_REG1 0xE5000u                 /* PLAT_XDATA1: 92 KB */
#define SMP_REG1_SIZE 0x17000u
#define SMP_REG2 0x93000u                 /* PLAT_XDATA0: 16 KB */
#define SMP_REG2_SIZE 0x4000u
#define SMP_BANK_SIZE (SMP_REG0_SIZE + SMP_REG1_SIZE + SMP_REG2_SIZE)   /* 216 KB */
#define SMP_BANK_BASE SMP_REG0            /* flash offset of the bank's header */
#define SMP_BANK_HDR 2048u                /* the header; the data follows */
#define SMP_BANK_MAGIC 0x504D534Cu        /* "LSMP" */
#define SMP_BANK_VER 2u                   /* 1: one window at 0xCF000, 16 slots, 22050 Hz (no longer read) */
#define SMP_ENT0 64u                      /* the header's first entry */
#define SMP_ENT 40u                       /* bytes an entry */
#define SMP_HDR_USED (SMP_ENT0 + SMP_NUSER * SMP_ENT)   /* 1344: what an upload stages */
#define SMP_BREAK 12                      /* the factory break (tools/gen_samples.py): the slice engine's default */
#define SMP_BARS_MAX 16                   /* a user entry's bars above this: not stated */

typedef struct {
    const uint8_t *adp;                   /* IMA ADPCM, the low nibble first */
    const uint8_t *adp2;                  /* from sample split on (a user sample running into the next window):
                                           * sample split + i is adp2's nibble i */
    const uint32_t *ck;                   /* a checkpoint a block: (predictor & 0xFFFF) | index << 16 */
    uint32_t n;                           /* samples */
    uint32_t split;                       /* where adp2 takes over (even; n: never) */
    uint32_t ls, le;                      /* its loop (le 0: none) */
    uint16_t rate;                        /* Hz: its root plays it at this rate */
    uint8_t root;                         /* the MIDI note it plays at its own rate (the slice engine: slice 1's) */
    uint8_t bars;                         /* a loop of this many bars (the slice engine follows the tempo with
                                           * it), 0: not stated */
    char name[SMP_NAME];
} smp_t;

typedef struct {
    int32_t w;                            /* the window's first sample (a multiple of SMP_WIN), -1 none */
    const smp_t *s;                       /* of this sample */
    int16_t pred;                         /* the decoder after the window's last sample (b[SMP_WIN]) */
    uint8_t idx, rsv;
    int16_t b[SMP_WIN + 1];               /* its samples and the next window's first */
} smp_cache_t;

extern const uint16_t smp_nslots;
static inline int smp_slots(void) { return smp_nslots; }   /* factory + user slots: a knob's range (inline: the
                                                            * note-start code in RAM reads it: size_fns.py) */
int smp_factory(void);                    /* the factory samples */
const smp_t *smp_get(int i);              /* 0: nothing there (an empty user slot, the bank being written) */
extern const smp_t *(*volatile smp_get_far)(int i);   /* the same through a pointer: for the note-start code in
                                                         * RAM, which cannot call the flash directly (size_fns.py) */
const char *smp_name(int i);              /* "kick", an upload's name, "user 3" for an empty slot */
void smp_fill(smp_cache_t *c, const smp_t *s, int32_t w);     /* decode the window from sample w (a multiple of
                                                                 * SMP_WIN) into the cache */
static inline void smp_cache_reset(smp_cache_t *c) { c->w = -1; c->s = 0; }
/* sample i (0 outside the sample), as a float -1..1 */
static inline float smp_one(smp_cache_t *c, const smp_t *s, int32_t i)
{
    if (i < 0 || (uint32_t)i >= s->n)
        return 0.0f;
    if (i - c->w >= SMP_WIN || i < c->w || c->s != s)
        smp_fill(c, s, i & ~(SMP_WIN - 1));
    return (float)c->b[i - c->w] * (1.0f / 32768.0f);
}
/* play: up to m samples into out[], linearly interpolated, from the position *pos + *fr stepping inc a
 * sample (dir +1 forward, -1 backward) while the position stays in [lo, hi) (clipped to the sample); returns
 * how many it wrote: fewer than m when the position left the range (the caller loops, bounces or ends).
 * *pos and *fr are left where it stopped */
int smp_play(smp_cache_t *c, const smp_t *s, int32_t *pos, float *fr, float inc, int dir, int32_t lo, int32_t hi,
             float *out, int m);

/* the user bank's windows: flash offset, size and first virtual offset of window r */
static inline uint32_t smp_reg_off(int r) { return r == 0 ? SMP_REG0 : r == 1 ? SMP_REG1 : SMP_REG2; }
static inline uint32_t smp_reg_size(int r) { return r == 0 ? SMP_REG0_SIZE : r == 1 ? SMP_REG1_SIZE : SMP_REG2_SIZE; }
static inline uint32_t smp_reg_v(int r) { return r == 0 ? 0u : r == 1 ? SMP_REG0_SIZE : SMP_REG0_SIZE + SMP_REG1_SIZE; }
/* the window of virtual offset v (-1: past the bank), *off its flash offset */
static inline int smp_reg_of(uint32_t v, uint32_t *off)
{
    for (int r = 0; r < SMP_NREG; r++)
        if (v - smp_reg_v(r) < smp_reg_size(r)) {
            *off = smp_reg_off(r) + (v - smp_reg_v(r));
            return r;
        }
    return -1;
}

/* the user bank */
void smp_user_attach(const uint8_t *const *reg);   /* where it reads: each window's first byte (the XIP window, RAM
                                                     * on the host; 0: none) */
int smp_user_scan(void);                  /* (re)read its header: the samples it holds, 0 = none / not valid */
void smp_user_drop(void);                 /* about to be rewritten: the user samples vanish now */
int smp_user_count(void);
uint32_t smp_user_bytes(void);            /* its data, bytes */
uint32_t smp_crc32(const uint8_t *p, uint32_t n);
uint32_t smp_crc32_more(uint32_t c, const uint8_t *p, uint32_t n);   /* on from c (start: 0) */
int smp_bank_crc(uint32_t len, uint32_t *crc);   /* the CRC of the bank's data, len bytes from SMP_BANK_HDR through
                                                  * the windows attached; 0 = done */
