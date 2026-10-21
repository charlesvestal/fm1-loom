/* SPDX-License-Identifier: GPL-3.0-only */
/* LOOM-8 pattern store: the plain-data helpers of pattern.h (main loop side; the sequencer only reads). */
#include "pattern.h"

/* the value a component starts with (the notebook p. 139, 0-based): pulse 4, hold 4, multiply 2, velocity 64,
 * ramps 4 iterations, random 4, portamento 40 %, bend down-up, tonality 4 (fifth up), jump to 13, skips: a
 * cycle of 2 (mode 0: the step plays on the cycle's last pass) */
const uint8_t SC_DEFAULT[NSC] = {0, 3, 3, 1, 4, 3, 3, 3, 3, 0, 3, 3, SK_VAL(1, 0), SK_VAL(1, 0), SK_VAL(1, 0)};
const char *const GR_NAME[NGROOVE] = {"swing", "lilt", "weave", "stretch", "drunk", "jitter", "accent", "lazy",
                                      "push pull", "sway", "swell"};

static void zero(void *p, uint32_t n)
{
    uint8_t *b = (uint8_t *)p;
    while (n--)
        *b++ = 0;
}

void pattern_init(tpat_t *p)
{
    zero(p, sizeof *p);
    p->len = 16;
    p->scale = TS_1;
    p->groove = 0;                    /* no groove amount */
    p->quant = 100;
    p->nlen = 50;                     /* half a step */
    p->shape = 0;
}

void patterns_init(patterns_t *P)
{
    int t, k;
    zero(P, sizeof *P);
    for (t = 0; t < NSEQT; t++)
        for (k = 0; k < NPATT; k++) {
            pattern_init(&P->pat[t][k]);
            if (t == AUX_BRAIN)       /* a brain step is a beat: a chord progression at four to the bar */
                P->pat[t][k].scale = TS_4;
        }
    for (k = 0; k < NPATT; k++)
        P->weave[k].len = 16;
    for (k = 0; k < NLANE; k++)
        P->lane[k].used = 0;
    P->scene[0].used = 1;             /* scene 1: pattern 1 on every track (pat[] is already 0) */
    P->song.len = 0;
    P->song.loop = 1;
}

/* ----------------------------------------------------------------- lanes --- */
int lane_find(const lane_t *L, int trk, int pat, int param)
{
    int k;
    for (k = 0; k < NLANE; k++)
        if (L[k].used && L[k].trk == trk && L[k].pat == pat && L[k].param == param)
            return k;
    return -1;
}

int lane_alloc(lane_t *L, int trk, int pat, int param)
{
    int k, i;
    k = lane_find(L, trk, pat, param);
    if (k >= 0)
        return k;
    for (k = 0; k < NLANE; k++)
        if (!L[k].used)
            break;
    if (k == NLANE)
        return -1;
    L[k].trk = (uint8_t)trk;
    L[k].pat = (uint8_t)pat;
    L[k].param = (uint16_t)param;
    for (i = 0; i < NSTEP; i++)
        L[k].val[i] = LOCK_NONE;
    L[k].gen++;                       /* the interrupt sees a new generation: a fresh runtime */
    __asm__ volatile("" ::: "memory");
    L[k].used = 1;
    return k;
}

void lane_clear(lane_t *L, int k)
{
    if (k >= 0 && k < NLANE)
        L[k].used = 0;
}

int lane_count(const lane_t *L)
{
    int k, n = 0;
    for (k = 0; k < NLANE; k++)
        n += L[k].used != 0;
    return n;
}

int step_has_lock(const lane_t *L, int trk, int pat, int step)
{
    int k;
    if (step < 0 || step >= NSTEP)
        return 0;
    for (k = 0; k < NLANE; k++)
        if (L[k].used && L[k].trk == trk && L[k].pat == pat && !lane_is_note(L[k].param) && L[k].val[step] != LOCK_NONE)
            return 1;
    return 0;
}

int step_tune(const lane_t *L, int trk, int pat, int step)
{
    int k;
    if (step < 0 || step >= NSTEP)
        return 0;
    for (k = 0; k < NLANE; k++)
        if (L[k].used && L[k].param == LANE_TUNE && L[k].trk == trk && L[k].pat == pat)
            return L[k].val[step] == LOCK_NONE ? 0 : (int)L[k].val[step] - TUNE_0;
    return 0;
}

int step_vel(const lane_t *L, int trk, int pat, int step, int sound)
{
    int k;
    if (step < 0 || step >= NSTEP)
        return 0;
    for (k = 0; k < NLANE; k++)
        if (L[k].used && L[k].param == LANE_VEL0 + (unsigned)sound && L[k].trk == trk && L[k].pat == pat)
            return L[k].val[step] == LOCK_NONE ? 0 : L[k].val[step];
    return 0;
}

void step_clear_locks(lane_t *L, int trk, int pat, int step)
{
    int k;
    if (step < 0 || step >= NSTEP)
        return;
    for (k = 0; k < NLANE; k++)
        if (L[k].used && L[k].trk == trk && L[k].pat == pat && !lane_is_note(L[k].param))
            L[k].val[step] = LOCK_NONE;
}

void pattern_clear_locks(lane_t *L, int trk, int pat)
{
    int k;
    for (k = 0; k < NLANE; k++)
        if (L[k].used && L[k].trk == trk && L[k].pat == pat && !lane_is_note(L[k].param))
            L[k].used = 0;
}

/* COND_AB + k -> A:B in the order 1:2 2:2 1:3 2:3 3:3 1:4 ... 8:8 (X0X's) */
void cond_ab(int k, int *a, int *b)
{
    int n = 2;
    if (k < 0)
        k = 0;
    if (k >= COND_NAB)
        k = COND_NAB - 1;
    while (k >= n) {
        k -= n;
        n++;
    }
    *a = k + 1;
    *b = n;
}

/* ----------------------------------------------------------------- steps --- */
void step_set_notes(step_t *st, const uint8_t *notes, int n, int vel)
{
    int i;
    for (i = 0; i < SN; i++)
        st->note[i] = i < n ? notes[i] : 0;
    st->vel = (uint8_t)(vel < 1 ? 1 : vel > 127 ? 127 : vel);
}

void step_toggle_note(step_t *st, int note)
{
    int i;
    if (note <= 0 || note > 127)
        return;
    for (i = 0; i < SN; i++)
        if (st->note[i] == note) {    /* there: remove it, close the gap */
            for (; i < SN - 1; i++)
                st->note[i] = st->note[i + 1];
            st->note[SN - 1] = 0;
            return;
        }
    for (i = 0; i < SN; i++)
        if (!st->note[i]) {
            st->note[i] = (uint8_t)note;
            if (!st->vel)
                st->vel = 100;
            return;
        }
}

int step_empty(const step_t *st)
{
    int i;
    for (i = 0; i < SN; i++)
        if (st->note[i])
            return 0;
    return 1;
}

/* the kit's steps (seq.h): the interrupt reads note[0] and note[1] apart, so a change between them plays at
 * most one step half old */
void kit_set(step_t *st, uint32_t mask)
{
    mask &= KIT_ALL;
    st->note[2] = st->note[3] = 0;
    st->note[1] = (uint8_t)(mask >> 8);
    st->note[0] = (uint8_t)(mask & 255u);
}
void kit_toggle(step_t *st, int sound)
{
    if (sound < 1 || sound > NDRUM)
        return;
    kit_set(st, kit_mask(st) ^ 1u << (sound - 1));
    if (!st->vel)
        st->vel = 100;
}
int kit_add(step_t *st, int sound)
{
    if (sound < 1 || sound > NDRUM || kit_has(st, sound))
        return 0;
    kit_set(st, kit_mask(st) | 1u << (sound - 1));
    return 1;
}
int kit_first(const step_t *st)
{
    uint32_t m = kit_mask(st);
    return m ? __builtin_ctz(m) + 1 : 0;
}

/* ----------------------------------------------------------------- weave --- */
static int pat_n(const tpat_t *p) { return p->len < 1 ? 1 : p->len > NSTEP ? NSTEP : p->len; }
static int onsets(const tpat_t *p, int upto)     /* the steps with notes before step upto */
{
    int n = 0;
    for (int i = 0; i < upto && i < NSTEP; i++)
        n += !step_empty(&p->st[i]);
    return n;
}
static const step_t *onset_at(const tpat_t *p, int k)   /* the k-th step with notes, cycled (0: none) */
{
    int len = pat_n(p), n = onsets(p, len);
    if (!n)
        return 0;
    k %= n;
    for (int i = 0; i < len; i++)
        if (!step_empty(&p->st[i]) && k-- == 0)
            return &p->st[i];
    return 0;
}
/* notes from a step of kind fk onto a step of kind tk (WK_*): a kit's sounds as notes on a pentatonic ladder from C3
 * (sound 1 C3, 2 Eb3, 3 F3 ...), notes as a kit's sounds (note mod 11); a throw's effects only onto a throw's step.
 * 0: nothing to take (the step keeps its own) */
static int notes_cross(const step_t *from, int fk, int tk, step_t *out)
{
    static const uint8_t LADDER[NDRUM] = {0, 3, 5, 7, 10, 12, 15, 17, 19, 22, 24};
    if (fk == tk) {
        for (int j = 0; j < SN; j++)
            out->note[j] = from->note[j];
        return 1;
    }
    if (fk == WK_FX || tk == WK_FX)
        return 0;
    if (fk == WK_KIT) {                          /* sounds to notes: the first four */
        int n = 0;
        for (int j = 0; j < SN; j++)
            out->note[j] = 0;
        for (int k = 1; k <= NDRUM && n < SN; k++)
            if (kit_has(from, k))
                out->note[n++] = (uint8_t)(48 + LADDER[k - 1]);
        return n > 0;
    }
    {                                            /* notes to sounds */
        uint32_t m = 0;
        for (int j = 0; j < SN; j++)
            if (from->note[j])
                m |= 1u << (from->note[j] % NDRUM);
        kit_set(out, m);
        return m != 0;
    }
}
void weave_step(const tpat_t *a, int ak, const tpat_t *b, int bk, int rule, uint32_t pos, step_t *out)
{
    int la = pat_n(a), lb = pat_n(b), ia = (int)(pos % (uint32_t)la), ib = (int)(pos % (uint32_t)lb);
    const step_t *n;
    switch (rule) {
    case WV_BARS:                     /* the bars in turn, counted from the start: A's, B's, A's ... */
    case WV_STEPS:                    /* the steps in turn */
        *out = a->st[ia];
        if (rule == WV_BARS ? (pos / 16u) & 1u : pos & 1u) {
            step_t keep = *out;
            *out = b->st[ib];         /* B's step, its notes as A's kind (none to take: A's step stays) */
            if (!step_empty(&b->st[ib]) && !notes_cross(&b->st[ib], bk, ak, out))
                *out = keep;
        }
        break;
    case WV_RHYTHM:                   /* A's step (its rhythm, velocity, tricks) with B's next notes */
        *out = a->st[ia];
        if (!step_empty(out) && (n = onset_at(b, (int)(pos / (uint32_t)la) * onsets(a, la) + onsets(a, ia))))
            notes_cross(n, bk, ak, out);
        break;
    default:                          /* WV_NOTES: B's step (its rhythm) with A's next notes */
        *out = b->st[ib];
        if (step_empty(out))
            break;
        if (!(n = onset_at(a, (int)(pos / (uint32_t)lb) * onsets(b, lb) + onsets(b, ib))) || !notes_cross(n, ak, ak, out))
            if (!notes_cross(&b->st[ib], bk, ak, out))
                *out = a->st[ia];     /* (nothing either way: A's step) */
        break;
    }
}

void pattern_woven(tpat_t *out, const tpat_t *a, int ak, const tpat_t *b, int bk, int rule)
{
    int la = pat_n(a), lb = pat_n(b), len = la > lb ? la : lb;
    if (rule == WV_BARS && len < 32)             /* (the bars in turn need two of them) */
        len = len * 2 > NSTEP ? NSTEP : len * 2;
    *out = *a;
    out->len = (uint8_t)len;
    for (int i = 0; i < NSTEP; i++) {
        if (i < len)
            weave_step(a, ak, b, bk, rule, (uint32_t)i, &out->st[i]);
        else
            zero(&out->st[i], sizeof out->st[i]);
    }
}
