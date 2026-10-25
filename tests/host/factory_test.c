/* SPDX-License-Identifier: MPL-2.0 */
/* The factory set in the user bank (0.8), as the FM-1 has it: sample.c built with SMP_FACT_MOVED (no factory samples
 * in the image), a bank of two user samples and the factory set after them (build/gen/factory_bank.bin:
 * tools/loom_samples.py build, its default). Each factory row plays its entry, the same bytes as the image's until
 * 0.8 (samples.h, built in here for the comparison); user 1 and 2 are the user's two, the factory set's entries no
 * user slots; a bank without the factory set leaves its rows silent (and named). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sample.h"
#define SMP_FACT_BUILTIN 1
#include "samples.h"

static int bad;
static void check(const char *what, int ok)
{
    printf("  %s %s\n", ok ? "ok  " : "FAIL", what);
    bad |= !ok;
}

static uint8_t w0[SMP_REG0_SIZE], w1[SMP_REG1_SIZE], w2[SMP_REG2_SIZE];
static int attach(const char *path)
{
    static const uint8_t *w[SMP_NREG] = {w0, w1, w2};
    uint8_t *const dst[SMP_NREG] = {w0, w1, w2};
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "no %s\n", path);
        exit(1);
    }
    for (int r = 0; r < SMP_NREG; r++)
        memset(dst[r], 0xFF, smp_reg_size(r));
    for (int r = 0; r < SMP_NREG; r++)
        if (fread(dst[r], 1, smp_reg_size(r), f) < smp_reg_size(r))
            break;
    fclose(f);
    smp_user_attach(w);
    return smp_user_scan();
}

static int same_bytes(const smp_t *s, int i)
{
    uint32_t nb = (s->n + 1u) / 2u, nck = (s->n + SMP_BLK - 1u) / SMP_BLK;
    for (uint32_t j = 0; j < nb; j++) {
        uint8_t b = j * 2u < s->split ? s->adp[j] : s->adp2[j - s->split / 2u];
        if (b != SMP_FACT_ADP[SMP_FACT[i].adp + j])
            return 0;
    }
    return memcmp(s->ck, SMP_FACT_CK + SMP_FACT[i].ck, nck * 4u) == 0;
}

int main(void)
{
    int n = attach("build/gen/factory_bank.bin"), all = 1, named = 1;
    check("the bank: two samples and the factory set's twelve", n == 2 + SMP_NFENT && smp_user_slots() == 2);
    for (int i = 0; i < SMP_FACT_NSET; i++) {
        const smp_t *s = smp_get(i);
        all &= s && s->n == SMP_FACT[i].n && s->rate == SMP_FACT[i].rate && s->root == SMP_FACT[i].root &&
               s->ls == SMP_FACT[i].ls && s->le == SMP_FACT[i].le && !strcmp(s->name, SMP_FACT[i].name) &&
               same_bytes(s, i);
    }
    check("each factory row plays its entry: the image's bytes until 0.8", all);
    check("user 1 and 2: the user's (piano two, sweep two), not the factory set",
          smp_get(SMP_NFACT) && !strcmp(smp_get(SMP_NFACT)->name, "piano two") && smp_get(SMP_NFACT + 1) &&
          !strcmp(smp_get(SMP_NFACT + 1)->name, "sweep two") && !smp_get(SMP_NFACT + 2));
    check("the breaks' rows: still empty (their pack not installed)", !smp_get(SMP_BREAK));
    n = attach("build/gen/user_bank_nofactory.bin");
    for (int i = 0; i < SMP_FACT_NSET; i++)
        named &= !smp_get(i) && !strcmp(smp_name(i), SMP_FACT[i].name);
    check("a bank without the factory set: its rows silent, still named", n == 2 && named);
    puts(bad ? "FACTORY FAILED" : "factory: the bank's entries play the factory rows");
    return bad;
}
