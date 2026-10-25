/* SPDX-License-Identifier: MPL-2.0 */
/* A user sample bank built in RAM for the host tests, in tools/sampleio.py's format (version 3, app/dsp/sample.h; or 2,
 * bank_blk = SMP_BLK_V2: a checkpoint every 64 samples, as before 0.8):
 * the image in one virtual space (bank[]), each window then copied into a buffer of its own (bank_attach), so a
 * reader that took the windows for one block of memory would read the wrong bytes.
 *   bank_reset();  bank_add(...) / bank_add_at(...);  bank_seal();  bank_attach();  smp_user_scan()
 * A tiny IMA encoder (tools/sampleio.py's): the decoder's own state, so the two agree exactly. */
#include <string.h>
#include "breaks.h"                              /* the break pack: what a test is left with */

static const int BK_IMA_S[89] = {7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60,
    66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598,
    658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026,
    4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350,
    22385, 24623, 27086, 29794, 32767};
static const int BK_IMA_I[8] = {-1, -1, -1, -1, 2, 4, 6, 8};
static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static uint8_t bank[SMP_BANK_SIZE];
static uint8_t bank_w0[SMP_REG0_SIZE], bank_w1[SMP_REG1_SIZE], bank_w2[SMP_REG2_SIZE];
static uint32_t bank_end;
static int bank_cnt;
static int bank_blk = SMP_BLK;                   /* samples a checkpoint: SMP_BLK (version 3) or SMP_BLK_V2 (2) */
static int16_t bank_pcm[1 << 19];                /* the last sample added, as the decoder will give it back */

static void bank_reset(void)
{
    memset(bank, 0xFF, sizeof bank);
    memset(bank, 0, SMP_BANK_HDR);
    bank_end = SMP_BANK_HDR;
    bank_cnt = 0;
}
/* a sample (gen(i): its values, int16 scale) of n samples at rate Hz: its checkpoints at virtual offset at (0: after
 * the last), its ADPCM right after them; returns its slot */
static int bank_add_at(const char *name, float (*gen)(int), int n, int root, int bars, int rate, uint32_t at)
{
    int pred = 0, idx = 0, i;
    uint32_t ck_off = at ? at : (bank_end + 3u) & ~3u, adp_off = ck_off + (uint32_t)((n + bank_blk - 1) / bank_blk) * 4u;
    uint8_t *e = bank + SMP_ENT0 + (uint32_t)bank_cnt * SMP_ENT;
    memset(bank + adp_off, 0, (size_t)(n + 1) / 2u);
    for (i = 0; i < n; i++) {
        int x = (int)gen(i), step, left, code, vd, b;
        if (i % bank_blk == 0)
            put32(bank + ck_off + (uint32_t)(i / bank_blk) * 4u, (uint32_t)(pred & 0xFFFF) | (uint32_t)idx << 16);
        step = BK_IMA_S[idx];
        left = x < pred ? pred - x : x - pred;
        code = x < pred ? 8 : 0;
        vd = step >> 3;
        for (b = 0; b < 3; b++) {                /* the magnitude bits 4, 2, 1: step, step / 2, step / 4 */
            int part = step >> b;
            if (left >= part) {
                code |= 4 >> b;
                left -= part;
                vd += part;
            }
        }
        pred = code & 8 ? pred - vd : pred + vd;
        pred = pred > 32767 ? 32767 : pred < -32768 ? -32768 : pred;
        idx += BK_IMA_I[code & 7];
        idx = idx < 0 ? 0 : idx > 88 ? 88 : idx;
        bank[adp_off + (uint32_t)(i >> 1)] |= (uint8_t)(code << ((i & 1) * 4));
        if (i < (int)(sizeof bank_pcm / sizeof bank_pcm[0]))
            bank_pcm[i] = (int16_t)pred;
    }
    memset(e, 0, SMP_ENT);
    memcpy(e, name, strlen(name) < SMP_NAME - 1 ? strlen(name) : SMP_NAME - 1);
    put32(e + 12, adp_off);
    put32(e + 16, ck_off);
    put32(e + 20, (uint32_t)n);
    e[32] = (uint8_t)root;
    e[34] = (uint8_t)bars;
    e[36] = (uint8_t)rate;
    e[37] = (uint8_t)(rate >> 8);
    if (adp_off + (uint32_t)(n + 1) / 2u > bank_end)
        bank_end = adp_off + (uint32_t)(n + 1) / 2u;
    return bank_cnt++;
}
static int bank_add(const char *name, float (*gen)(int), int n, int root, int bars, int rate)
{
    return bank_add_at(name, gen, n, root, bars, rate, 0);
}
static void bank_seal(void)                      /* the header: magic, version, count, length, CRC, size */
{
    put32(bank, SMP_BANK_MAGIC);
    bank[4] = bank_blk == SMP_BLK_V2 ? 2u : SMP_BANK_VER;
    bank[5] = 0;
    bank[6] = (uint8_t)bank_cnt;
    put32(bank + 8, bank_end - SMP_BANK_HDR);
    put32(bank + 12, smp_crc32(bank + SMP_BANK_HDR, bank_end - SMP_BANK_HDR));
    put32(bank + 16, SMP_BANK_SIZE);
}
static void bank_attach(void)                    /* the image into its three windows, apart; the reader on them */
{
    static const uint8_t *w[SMP_NREG] = {bank_w0, bank_w1, bank_w2};
    memcpy(bank_w0, bank + smp_reg_v(0), SMP_REG0_SIZE);
    memcpy(bank_w1, bank + smp_reg_v(1), SMP_REG1_SIZE);
    memcpy(bank_w2, bank + smp_reg_v(2), SMP_REG2_SIZE);
    smp_user_attach(w);
}
static void bank_detach(void)                    /* back to the break pack (breaks.h) */
{
    breaks_attach();
}
