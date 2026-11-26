/* SPDX-License-Identifier: MPL-2.0 */
/* The sends as they were set before the one effects list (an FX track's pick, four knobs and the delay's PING WIDTH,
 * WOBBLE and SMEAR: dsp/fx.h fx_pick_slot), on a slot with its memory from the pool as the engine gives it (as much as
 * its effect needs), for the effects' tests (fx_test, fx_render): fxo_init starts it as fx_init did (the chorus at its
 * defaults, at rest), fxo_set, fxo_width, fxo_mod as fx_set, fx_width, fx_delay_mod did. */
#pragma once
#include <string.h>
#include "fx.h"

static const uint8_t RV_SPU_DEFAULT[4] = {127, 64, 64, 0};   /* the SPU modes' defaults */
static fxslot_t *fxo;
static uint8_t fxo_pick, fxo_k[4], fxo_w = 127, fxo_wob, fxo_smr;
static void fxo_apply(void)
{
    uint8_t o[9];
    fx_pick_slot(fxo_pick, fxo_k, fxo_w, fxo_wob, fxo_smr, o);
    fx_set(fxo, o[0], o + 1, o + 5);
    fx_memory(fxo, efx_pool[0], fx_need(fxo));
}
static void fxo_set(int pick, const uint8_t k[4])
{
    fxo_pick = (uint8_t)pick;
    memcpy(fxo_k, k, 4);
    fxo_apply();
}
static void fxo_width(int w)
{
    fxo_w = (uint8_t)w;
    fxo_apply();
}
static void fxo_mod(int wob, int smr)
{
    fxo_wob = (uint8_t)wob;
    fxo_smr = (uint8_t)smr;
    fxo_apply();
}
static void fxo_init(fxslot_t *f)
{
    static float z[1];
    fxo = f;
    fx_init(f);
    fxo_w = 127;
    fxo_wob = fxo_smr = 0;
    fxo_set(FXT_CHORUS, FXT_DEFAULT[FXT_CHORUS]);
    fx_process(f, z, z, z, 0);                  /* (at rest: its states as the chorus's) */
}
