/* SPDX-License-Identifier: GPL-3.0-only */
/* LOOM-8's MIDI CC map: a CC on channel 1-8 (a track), 9 / 10 (FX I / II) or 11 (the master) sets a parameter as its
 * knob would, scaled onto the parameter's range; a track's CC is also recorded into its own lane with REC on and
 * playing. Part of app.c's unity build, after ui.c: is_aux, is_drum, inst_trk, set_tp. */

#define CC_NONE 0xFFu                         /* (the map's "nothing here") */
#define CC_MUTE 0xFEu                         /* the track's mute, not a parameter */
#define CC_KSEND1 0xFDu                       /* a kit: every sound's send I / II by an offset */
#define CC_KSEND2 0xFCu
/* tracks 1-8: the CC, what it sets on a synth, on a kit */
static const struct { uint8_t cc, syn, kit; } CC_TRK[] = {
    {7, TP_LEVEL, TP_LEVEL}, {10, TP_PAN, TP_PAN},  {91, TP_SEND1, CC_KSEND1}, {93, TP_SEND2, CC_KSEND2},
    {9, CC_MUTE, CC_MUTE},   {74, TP_CUT, TP_KTRK}, {71, TP_RES, TP_CUT},      {73, TP_A, TP_D},
    {72, TP_R, TP_R},        {75, TP_D, CC_NONE},   {5, TP_PORTA, CC_NONE},     {1, TP_LAMT, CC_NONE}};
/* a kit's sounds 1-11: CC first + sound */
#define CC_KMUTE 16
#define CC_KPAN 80
#define CC_KLEVEL 102
/* FX I / II: the return level, the four knobs, low-pass / high-pass; fx1: FX I only (93, its feed into FX II) */
static const struct { uint8_t cc, par, fx1; } CC_FX[] = {
    {7, TP_LEVEL, 0}, {16, TP_E1, 0}, {17, TP_E2, 0}, {18, TP_E3, 0}, {19, TP_E4, 0}, {74, TP_CUT, 0},
    {75, TP_FENV, 0}, {93, TP_SEND2, 1}};
/* the master */
static const struct { uint8_t cc, m; } CC_MST[] = {
    {7, MS_LEVEL},  {16, MS_EQ_LO}, {17, MS_EQ_MID}, {18, MS_EQ_HI}, {20, MS_CTYPE}, {21, MS_CAMT},     {22, MS_CTONE},
    {23, MS_CMIX},  {24, MS_PERC},  {25, MS_MEL},    {26, MS_COMP},  {27, MS_PUMP},  {28, MS_PUMP_REL}};

static int cc_to(int v, int lo, int hi) { return lo + (v * (hi - lo) + 63) / 127; }   /* 0..127 onto lo..hi */

static void cc_par(int t, int p, int v)       /* a track's parameter from a CC (recorded as a knob turn would be) */
{
    if (is_aux(t))
        set_tp(t, p, v);                       /* an FX track's knobs: 0..127 as they come */
    else {
        const tp_info_t *in = tp_info_par(p, proj.trk[t].par);
        set_tp(t, p, cc_to(v, in->lo, in->hi));
    }
    if (seq.rec && playing() && !is_aux(t))
        seq_rec_knob(&seq, t, p, proj.trk[t].par[p]);
}
static void cc_master(int m, int v)
{
    int nv = cc_to(v, 0, master_max(m));
    if (nv == proj.master[m])                  /* (as the master list's knob: no undo step for a value that holds) */
        return;
    undo_master();
    proj.master[m] = (uint8_t)nv;
    engine_master(m, proj.master[m]);
    mark_dirty();
}
static void cc_track(int t, int cc, int v)
{
    int kit = is_drum(t);
    for (uint32_t i = 0; i < sizeof CC_TRK / sizeof CC_TRK[0]; i++)
        if (CC_TRK[i].cc == cc) {
            int p = kit ? CC_TRK[i].kit : CC_TRK[i].syn;
            if (p < TP_DRUM0)
                cc_par(t, p, v);
            return;
        }
}
static void cc_in(uint32_t pkt)
{
    int st = (int)(pkt >> 8 & 0xFFu), ch = st & 15, ty = st & 0xF0, d1 = (int)(pkt >> 16 & 0x7Fu),
        v = (int)(pkt >> 24 & 0x7Fu);
    if (ty != 0xB0)
        return;
    if (ch == CH_MASTER) {
        for (uint32_t i = 0; i < sizeof CC_MST / sizeof CC_MST[0]; i++)
            if (CC_MST[i].cc == d1)
                cc_master(CC_MST[i].m, v);
    } else if (ch == CH_FX1 || ch == CH_FX2) {
        int ft = ch == CH_FX1 ? AUX_FX1 : AUX_FX2;
        for (uint32_t i = 0; i < sizeof CC_FX / sizeof CC_FX[0]; i++)
            if (CC_FX[i].cc == d1 && !(CC_FX[i].fx1 && ft != AUX_FX1))
                cc_par(ft, CC_FX[i].par, v);
    } else
        cc_track(ch < NTRK ? ch : inst_trk(), d1, v);
}
static void cc_in_drain(void)                 /* the main loop: what MIDI in brought since the last frame */
{
    uint32_t pkt;
    while (engine_cc_take(&pkt))
        cc_in(pkt);
}
