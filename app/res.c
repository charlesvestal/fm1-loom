/* SPDX-License-Identifier: MPL-2.0 */
/* LOOM-8's resource region (fm1-base device/res.c, plat_res; tools/gen_res.py): its sections by tag, read in place.
 * The region is the installer's to write after an update; until it has, every section is absent (0) and its readers
 * do without: the 909 cymbals are silent, large type falls back to the faces built in. */
#define RES_MAGIC_LRS 0x3153524Cu                    /* "LRS1" */
#define RES_TAG(a, b, c, d) ((uint32_t)(a) | (uint32_t)(b) << 8 | (uint32_t)(c) << 16 | (uint32_t)(d) << 24)

static uint32_t res_rd32(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

/* the section tagged t and its length, 0 when absent (no region, or not in it) */
static const uint8_t *res_section(uint32_t t, uint32_t *len)
{
    uint32_t n = 0, i, cnt;
    const uint8_t *p = plat_res(&n);
    *len = 0;
    if (!p || n < 8u || res_rd32(p) != RES_MAGIC_LRS)
        return 0;
    cnt = (uint32_t)(p[4] | p[5] << 8);
    if (n < 8u + 12u * cnt)
        return 0;
    for (i = 0; i < cnt; i++) {
        const uint8_t *e = p + 8 + 12 * i;
        uint32_t off = res_rd32(e + 4), l = res_rd32(e + 8);
        if (res_rd32(e) == t && off <= n && l <= n - off) {
            *len = l;
            return p + off;
        }
    }
    return 0;
}
/* the 909 cymbals' samples (dsp/d909.c d909_res_get) */
static const uint8_t *res_samples(uint32_t *len) { return res_section(RES_TAG('N', '9', 'S', '1'), len); }
