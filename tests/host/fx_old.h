/* SPDX-License-Identifier: MPL-2.0 */
/* The sends as they were set before the one effects list (an FX track's pick, four knobs and the delay's PING WIDTH,
 * WOBBLE and SMEAR: dsp/fx.h fx_pick_slot), on a slot with its memory from the pool as the engine gives it (as much as
 * its effect needs), for the effects' tests (fx_test, fx_render): fxo_init starts it as fx_init did (the chorus at its
 * defaults, at rest), fxo_set, fxo_width, fxo_mod as fx_set, fx_width, fx_delay_mod did. */
#pragma once
#include <string.h>
#include "fx.h"

/* the picks' names and their four knobs' defaults (the plate's for the reverb; the SPU modes', lush's) */
static const char *const FXT_NAME[NFXT] = {"chorus", "delay", "lofi", "phaser", "reverb"};
static const uint8_t FXT_DEFAULT[NFXT][4] = {{50, 86, 20, 127}, {83, 64, 56, 64}, {80, 80, 64, 40}, {50, 90, 50, 80},
                                             {100, 40, 44, 72}};
static const uint8_t RV_SPU_DEFAULT[4] = {127, 64, 64, 0};
static inline const char *fx_pick_name(int pick)
{
    int t = fx_pick_type(pick);
    return t == FXT_REVERB ? RV_NAME[fx_pick_mode(pick)] : FXT_NAME[t];
}
static fxslot_t *fxo;
static uint8_t fxo_pick, fxo_k[4], fxo_w = 127, fxo_wob, fxo_smr;
static void fxo_apply(void)
{
    static float z[1];
    uint8_t o[9];
    fx_pick_slot(fxo_pick, fxo_k, fxo_w, fxo_wob, fxo_smr, o);
    fx_set(fxo, o[0], o + 1, o + 5);
    fx_memory(fxo, efx_pool[0], fx_need(fxo));
    fx_process(fxo, z, z, z, 0);                /* (the effect taken; its memory cleared, as the engine's blocks would) */
    while (fx_memory(fxo, efx_pool[0], fx_need(fxo)))
        ;
}
static void fxo_set(int pick, const uint8_t k[4])
{
    fxo_pick = (uint8_t)pick;
    memcpy(fxo_k, k, 4);
    fxo_apply();
}
static inline void fxo_width(int w)
{
    fxo_w = (uint8_t)w;
    fxo_apply();
}
static inline void fxo_mod(int wob, int smr)
{
    fxo_wob = (uint8_t)wob;
    fxo_smr = (uint8_t)smr;
    fxo_apply();
}
static void fxo_init(fxslot_t *f)
{
    fxo = f;
    fx_init(f);
    fxo_w = 127;
    fxo_wob = fxo_smr = 0;
    fxo_set(FXT_CHORUS, FXT_DEFAULT[FXT_CHORUS]);   /* (at rest: its states as the chorus's) */
}
