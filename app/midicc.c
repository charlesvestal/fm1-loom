/* SPDX-License-Identifier: GPL-3.0-only */
/* LOOM-8's MIDI CC map: a CC on channel 1-8 (a track), 9 / 10 (FX I / II) or 11 (the master) sets a parameter as its
 * knob would, scaled onto the parameter's range; a track's CC is also recorded into its own lane with REC on and
 * playing. The other way: a mapped value changed by hand (a knob, a SEL list row) goes out as its CC, with CC OUT on.
 * Part of app.c's unity build, after ui.c: is_aux, is_drum, inst_trk, set_tp. */

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
static uint8_t cc_ksend[NTRK][2] = {[0 ... NTRK - 1] = {64, 64}};   /* a kit's last CC 91 / 93 (the offsets' base) */
static void cc_mute(int t, int on)            /* as the Mix page's tap: the mute now, in the scene playing */
{
    if ((int)(seq.mute >> t & 1u) != on) {
        set_mute(t, on);
        say(on ? "MUTED" : "UNMUTED", TRACK_NAME[t]);
    }
}
static void cc_kmute(int t, int snd, int on)  /* a kit's sound, as Arrange's (the first four kits) */
{
    int k = kit_slot(t);
    if (k >= 0 && (int)(seq.kmute[k] >> snd & 1u) != on)
        kit_sound_mute(t, snd);
}
static void cc_kit_send(int t, int which, int v)   /* every sound's send moved by the CC's change: eleven lanes for
                                                    * one knob, so this move is not recorded (a kit sound's own
                                                    * send is, from set_tp) */
{
    int d = v - cc_ksend[t][which];
    cc_ksend[t][which] = (uint8_t)v;
    for (int k = 0; k < DL_NSOUND; k++) {
        int p = TP_DRUM(k, DP_SEND1 + which);
        set_tp(t, p, proj.trk[t].par[p] + d);    /* (set_tp clamps to the send's range) */
    }
}
static void cc_track(int t, int cc, int v)
{
    int kit = is_drum(t);
    if (kit && cc >= CC_KMUTE && cc < CC_KMUTE + DL_NSOUND) {
        cc_kmute(t, cc - CC_KMUTE, v >= 64);
        return;
    }
    if (kit && cc >= CC_KPAN && cc < CC_KPAN + DL_NSOUND) {
        cc_par(t, TP_DRUM(cc - CC_KPAN, DP_PAN), v);
        return;
    }
    if (kit && cc >= CC_KLEVEL && cc < CC_KLEVEL + DL_NSOUND) {
        cc_par(t, TP_DRUM(cc - CC_KLEVEL, DP_VOLUME), v);
        return;
    }
    for (uint32_t i = 0; i < sizeof CC_TRK / sizeof CC_TRK[0]; i++)
        if (CC_TRK[i].cc == cc) {
            int p = kit ? CC_TRK[i].kit : CC_TRK[i].syn;
            if (p == CC_MUTE)
                cc_mute(t, v >= 64);
            else if (p == CC_KSEND1 || p == CC_KSEND2)
                cc_kit_send(t, p == CC_KSEND2, v);
            else if (p < TP_DRUM0) {
                cc_par(t, p, v);
                if (p == TP_SEND1 || p == TP_SEND2)   /* a synth's send: keep the kit's base current too (91 / 93's
                                                        * last value), so a track turned back into a kit doesn't jump */
                    cc_ksend[t][p == TP_SEND2] = (uint8_t)v;
            }
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

/* ---- out: a mapped value changed by hand (a knob, a SEL list row), with CC OUT on ---- */
static uint32_t cc_out_last, cc_out_n;        /* (the host's expect ccouts, ccst, ccd1, ccd2) */
static int cc_from(int x, int lo, int hi) { return hi > lo ? ((x - lo) * 127 + (hi - lo) / 2) / (hi - lo) : 0; }
static void cc_send(int st, int d1, int d2)  /* a channel message out over USB (st: 0xB0 / 0xC0 | channel) */
{
    if (proj.rsv[2] & CC_OUT_OFF)
        return;
    cc_out_last = (uint32_t)st << 16 | (uint32_t)d1 << 8 | (uint32_t)d2;
    cc_out_n++;
    plat_midi_out((uint32_t)st >> 4 | (uint32_t)st << 8 | (uint32_t)d1 << 16 | (uint32_t)d2 << 24);
}
static int cc_of(int t, int p)                /* the CC a track's parameter has, -1 none */
{
    if (t == AUX_FX1 || t == AUX_FX2) {
        for (uint32_t i = 0; i < sizeof CC_FX / sizeof CC_FX[0]; i++)
            if (CC_FX[i].par == p && !(t == AUX_FX2 && p == TP_SEND2))
                return CC_FX[i].cc;
        return -1;
    }
    if (t >= NTRK)
        return -1;
    if (is_drum(t) && p >= TP_DRUM0) {
        int k = (p - TP_DRUM0) / DP_N, q = (p - TP_DRUM0) % DP_N;
        return q == DP_PAN ? CC_KPAN + k : q == DP_VOLUME ? CC_KLEVEL + k : -1;
    }
    if (p >= TP_DRUM0)
        return -1;                       /* a kit key's on a synth: none (the map's 0xFC..0xFF marks sit past
                                          * TP_DRUM0 too, at real kit-key indices: never a synth's own parameter) */
    for (uint32_t i = 0; i < sizeof CC_TRK / sizeof CC_TRK[0]; i++)
        if ((is_drum(t) ? CC_TRK[i].kit : CC_TRK[i].syn) == p)
            return CC_TRK[i].cc;
    return -1;
}
static void cc_out_param(int t, int p)        /* set_tp: a track's parameter changed */
{
    int cc = cc_hand ? cc_of(t, p) : -1, ch;
    const tp_info_t *in;
    if (cc < 0 || (t < NTRK && !proj.trk[t].par[TP_CHAN]))
        return;
    ch = t < NTRK ? t : t == AUX_FX1 ? CH_FX1 : CH_FX2;
    in = tp_info_par(p, proj.trk[t].par);
    cc_send(0xB0 | ch, cc, is_aux(t) ? proj.trk[t].par[p] : cc_from(proj.trk[t].par[p], in->lo, in->hi));
}
static void cc_out_master(int m)              /* master_knob, the master's list rows: a master value changed */
{
    if (!cc_hand)
        return;
    for (uint32_t i = 0; i < sizeof CC_MST / sizeof CC_MST[0]; i++)
        if (CC_MST[i].m == m)
            cc_send(0xB0 | CH_MASTER, CC_MST[i].cc, cc_from(proj.master[m], 0, master_max(m)));
}
