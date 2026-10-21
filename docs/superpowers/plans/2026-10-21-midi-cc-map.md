# C3: the MIDI CC map, Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers-extended-cc:subagent-driven-development (recommended) or superpowers-extended-cc:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** LOOM-8 is played and mixed from MIDI: fixed channels, a CC map for the tracks / kits / FX / master (in, recorded, and out), and scenes by Program Change or notes.

**Architecture:** The audio interrupt's `midi_in` (app/engine.c) keeps notes and bend, and puts every CC, Program Change and channel-11 note into a 16-entry ring. The main loop (`ui_frame`) drains it through a new unity-build part, `app/midicc.c` (included after ui.c), which holds the map and applies each message the way the knob or key would (`set_tp`, `set_mute`, `kit_sound_mute`, `scene_go`, the master's path). Out: `set_tp` and `master_knob` call midicc.c's senders, which send only while a flag says a person turned something (`knob()` and `list_change()` set it).

**Tech Stack:** C (single-precision, no libm, no memmove), LOOM-8's unity build (app/app.c), fm1-base's host simulator (`loom_host` + tests/scenarios/*.xy).

**Spec:** docs/superpowers/specs/2026-10-21-midi-cc-map-design.md

**User decisions (already made):**
- Map reach: "Also standard sound CCs" (mixer + FX + master + 74/71/73/72/75/5/1).
- Channels: "dont let tracks assign different channels ... just have them always be the track channels": T1-T8 = 1-8, FX I 9, FX II 10, master 11, 12-16 the selected track; MIDI CHANNEL becomes MIDI OUT on / off.
- Direction: "In and out"; only by hand; never echo what came in.
- Recording: "Yes, on its own track".
- Kit sounds: "Level, pan, mute only" (16-26 mute, 80-90 pan, 102-112 level; 91/93 move all sends by an offset).
- Mutes do what the Mix page does (they save into the scene playing).
- "cc out should be on by default ... just like note out".
- Scenes: Program Change on 11 picks the scene; notes 36-67 on 11 too ("should it be notes too? fine if not"); Program Change out.

---

## Build and test, for every task

```sh
cd /Volumes/ExtFS/charlesvestal/github/fm1-parent/fm1-xy
export PATH=~/github/fm1-parent/bin:$PATH
PYTHON=/opt/homebrew/bin/python3.14 sh build.sh --host                 # the simulator: app/build/host/loom_host
mkdir -p app/build/scenarios/midicc && app/build/host/loom_host tests/scenarios/midicc.xy app/build/scenarios/midicc
PYTHON=/opt/homebrew/bin/python3.14 sh tests/run_tests.sh              # everything (ends "all tests passed")
```

A scenario prints `FAIL ...` lines and exits non-zero on a failed `expect`. Parameter numbers used below (synth.h):
TP_A 9, TP_D 10, TP_R 12, TP_CUT 17, TP_RES 18, TP_KTRK 20, TP_SEND1 22, TP_SEND2 23, TP_LAMT 26, TP_PORTA 6,
TP_E1 1, TP_FENV 19, TP_LEVEL 48, TP_PAN 49, TP_CHAN 51, TP_DRUM0 52, DP_N 20 (a kit key's DP_PAN 6, DP_VOLUME 7,
DP_SEND1 18, DP_SEND2 19: key k's at 52 + 20k + that). A new project: T1 a kit, T2-T8 synths, every track's MIDI OUT
on. `midi B1 07 40` sends CC 7 = 64 on channel 2 (the status byte's low nibble is the channel, from 0).

## Files

- Create `app/midicc.c`: the map, CC / Program Change / scene-note in, CC and Program Change out.
- Modify `app/app.c`: include midicc.c after ui.c.
- Modify `app/engine.c` / `app/engine.h`: fixed channels in `midi_track`, the ring (replaces `eng_cc_level`), `engine_cc_take`, the channel numbers.
- Modify `app/seq/seq.c`: notes out on the track's own channel.
- Modify `app/dsp/synth.c`: TP_CHAN's info (a switch, "midi out").
- Modify `app/project.c`: new tracks MIDI OUT 1; old channel numbers read as on.
- Modify `app/ui.c`: the drain, the hand flag around `knob()` and `list_change()`, the out hooks in `set_tp` and `master_knob`, scene out in `ui_frame`, the MIDI OUT row's pick, the CC OUT row's pick.
- Modify `app/pages.c`: ACTION names (MIDI OUT, CC OUT), the rows, `CC_OUT_OFF`.
- Modify `app/host.c`: `expect ccouts` / `ccst` / `ccd1` / `ccd2`, the `ccreset` command.
- Create `tests/scenarios/midicc.xy`. Modify `tests/scenarios/edits.xy` (its MIDI CHANNEL check).
- Modify `docs/MANUAL.md`, `docs/feedback-2026-10-21.md` (release notes).

---

### Task 1: Fixed channels, MIDI OUT on / off

**Goal:** Notes in and out use the fixed channels (T1-T8 = 1-8, 12-16 the selected track, 9-11 no notes); the per-track MIDI CHANNEL row becomes MIDI OUT on / off; old projects' channel numbers read as on.

**Files:**
- Modify: `app/engine.c` (`midi_track`, `midi_in`), `app/engine.h`
- Modify: `app/seq/seq.c:403,409,553,557,563` (the five `TP_CHAN - 1` channels)
- Modify: `app/dsp/synth.c` (FIXED's last row; `tp_text_i`'s TP_CHAN case)
- Modify: `app/project.c:126,233,588` and the load path after the format-10 block
- Modify: `app/pages.c` (`ACTION_NAME[A_MIDI_CH]`, `list_midi_ch`), `app/ui.c:2230` (A_MIDI_CH pick), `app/ui.c:159` (sound_reset's comment)
- Create: `tests/scenarios/midicc.xy`; Modify: `tests/scenarios/edits.xy:128-135`

**Acceptance Criteria:**
- [ ] A note on channel 3 plays T3 whatever track is selected; on channel 12 the selected track (its note out on its own channel); on 9, 10 nothing (`expect voices 0`).
- [ ] T3's notes go out on channel 3 (`midi_out.txt`: `92 ..` / `82 ..`), with T3's MIDI OUT on; nothing with it off.
- [ ] The SEL row reads MIDI OUT on / off; ALGORITHM sets 1 / 0.
- [ ] A project whose track holds channel 5 loads with that track's TP_CHAN = 1.

**Verify:** `app/build/host/loom_host tests/scenarios/midicc.xy app/build/scenarios/midicc` → no FAIL lines, exit 0; `grep -c "^.*: 92 3C" app/build/scenarios/midicc/midi_out.txt` → at least 1.

**Steps:**

- [ ] **Step 1: Write the failing scenario** `tests/scenarios/midicc.xy`:

```
# C3 (docs/superpowers/specs/2026-10-21-midi-cc-map-design.md): fixed channels, the CC map in and out, scenes by MIDI
wait 300
# fixed channels: channel 3 is T3 whichever track is selected; MIDI OUT on by default
projnew
expect par3.51 1
expect par2.51 1
midi 92 3C 64
wait 20
midi 82 3C 00
wait 20
```

(The `92 3C` out check is in `midi_out.txt`, the Verify's grep; Setup's NOTES is on in a new project.)

- [ ] **Step 2: Run it**: `expect par3.51 1` fails (a new project's T3 has 3). Run: `PYTHON=/opt/homebrew/bin/python3.14 sh build.sh --host && app/build/host/loom_host tests/scenarios/midicc.xy app/build/scenarios/midicc` → `FAIL expect par3.51 1, got 3`.

- [ ] **Step 3: The channels.** In `app/engine.h`, after `#define PUNCH_HP 90`:

```c
/* MIDI's fixed channels (from 0): tracks 1-8 on 0-7, FX I and FX II on 8 and 9, the master and the scenes on 10;
 * 11-15 the track selected (docs/superpowers/specs/2026-10-21-midi-cc-map-design.md) */
enum { CH_FX1 = NTRK, CH_FX2, CH_MASTER };
```

In `app/engine.c`, replace `midi_track` and the note / bend part of `midi_in`:

```c
static int midi_track(int ch)                      /* the track a channel plays: 1-8 theirs, 12-16 the one selected
                                                    * (else T1); FX and the master none (-1) */
{
    if (ch < NTRK)
        return ch;
    if (ch <= CH_MASTER)
        return -1;
    return proj.trk_sel < NTRK ? proj.trk_sel : 0;
}
```

```c
    if (ty == 0x90 && d2 == 0)
        ty = 0x80;
    if (ty == 0x90 || ty == 0x80) {
        int t = midi_track(ch);
        if (t >= 0)
            seq_key(&seq, t, d1, ty == 0x90, d2);
    } else if (ty == 0xB0 && d1 == 7) {            /* CC 7, volume: the track's level (Task 2 replaces it) */
        int t = midi_track(ch);
        if (t >= 0)
            eng_cc_level[t] = d2;
    } else if (ty == 0xE0) {                       /* pitch bend: +-2 semitones onto the track's bend */
        int t = midi_track(ch);
        if (t >= 0)
            bend_c[t] = (int16_t)((((int)d2 << 7 | d1) - 8192) * 200 / 8192);
    }
```

- [ ] **Step 4: Notes out on the track's channel.** In `app/seq/seq.c`, each of the five `((s->par[t][TP_CHAN] - 1) & 15)` becomes `(t & 15)` (lines 403, 409, 553, 557, 563). The `s->par[t][TP_CHAN]` tests stay: they are now MIDI OUT's on / off. Check none is left:

Run: `grep -n "TP_CHAN\] - 1" app/seq/seq.c` → no output.

- [ ] **Step 5: TP_CHAN is a switch.** In `app/dsp/synth.c` FIXED's last row, `{"channel", 0, 16, 0, PK_NUM, 0}}` becomes:

```c
    {"midi out", 0, 1, 1, PK_SWITCH, 0}};
```

and in `tp_text_i` delete the block `if (p == TP_CHAN && v == 0) { put_s(num, "off"); return; }` (PK_SWITCH prints on / off).

- [ ] **Step 6: New and old projects.** `app/project.c:233`: `tr->par[TP_CHAN] = (uint8_t)(t + 1);` becomes `tr->par[TP_CHAN] = 1;` with the comment `/* MIDI OUT on: track N's notes on channel N (with Setup's NOTES) */`. `app/project.c:588-589` (format 10): `proj.trk[t].par[TP_CHAN] = (uint8_t)(t + 1);` becomes `= 1;`. After that format-10 block's closing brace, add:

```c
        for (int t = 0; t < NTRK; t++)                /* (before C3 a track held its channel, 1-16: now MIDI OUT on) */
            if (proj.trk[t].par[TP_CHAN])
                proj.trk[t].par[TP_CHAN] = 1;
```

(`app/project.c:126`, the aux tracks' `p[TP_CHAN] = 0;`, stays.) In `app/ui.c:159` the comment becomes `/* the engine's defaults; MIDI OUT stays (routing, not sound) */`.

- [ ] **Step 7: The row.** `app/pages.c` ACTION_NAME: `"MIDI CHANNEL"` becomes `"MIDI OUT"`. `list_midi_ch`:

```c
static void list_midi_ch(int trk)                  /* the track's notes out over USB, on its own channel (Setup's NOTES on) */
{
    list_add(ACTION_NAME[A_MIDI_CH], proj.trk[trk].par[TP_CHAN] ? "on" : "off", LK_PICK, A_MIDI_CH);
}
```

`app/ui.c` `case A_MIDI_CH:` becomes `set_tp(sel_trk(), TP_CHAN, d > 0);`.

- [ ] **Step 8: edits.xy's checks.** `tests/scenarios/edits.xy:109-117` checks "track N on MIDI channel N": its comment becomes `# a new project: every track's MIDI OUT on; ...` (keep the rest of the line) and `expect par2.51 2` becomes `expect par2.51 1` (`expect par1.51 1` stays). `tests/scenarios/edits.xy:128-135` becomes:

```
# a track's MIDI OUT on its sound pages' SEL list (MIDI OUT, the last row): on in a new project, ALGORITHM sets it
turn ALGO 1
tap EDIT
tap SEL
turn SELECT 20
turn ALGO -1
wait 40
expect par2.51 0
turn ALGO 1
wait 40
expect par2.51 1
tap HOME
```

- [ ] **Step 9: Extend midicc.xy** with the other channels, MIDI OUT off, and an old channel number (append):

```
# a track holding a channel number (as before C3) loads with MIDI OUT on
projnew
param 3 51 5
projsave 1
reboot
expect par3.51 1

# channel 12: the track selected (T2 after ALGO 1: its note goes out on channel 2, "91 3E" in the log);
# channels 9 and 10: no notes
turn ALGO 1
expect trk 2
midi 9B 3E 64
wait 20
midi 8B 3E 00
wait 300
midi 98 3C 64
midi 99 3C 64
wait 20
expect voices 0
# MIDI OUT off on T3: no note out (the out log's count stays)
param 3 51 0
midi 92 40 64
wait 20
midi 82 40 00
```

and in the Verify, also: `grep -c ": 92 40" app/build/scenarios/midicc/midi_out.txt` → `0`, and
`grep -c ": 91 3E" app/build/scenarios/midicc/midi_out.txt` → at least 1.

- [ ] **Step 10: Build, run, full tests.** Run the Build and test block. Expected: midicc.xy and edits.xy pass, `all tests passed`. If `voices` stays above 0 after a release, raise the `wait 300` (a release tail).

- [ ] **Step 11: Commit**

```bash
git add app/engine.c app/engine.h app/seq/seq.c app/dsp/synth.c app/project.c app/pages.c app/ui.c tests/scenarios/midicc.xy tests/scenarios/edits.xy
git commit -m "LOOM-8: MIDI's fixed channels (T1-T8 on 1-8, 12-16 the track selected, none on 9-11); MIDI CHANNEL becomes MIDI OUT on / off (an old channel number reads as on)"
```

---

### Task 2: The CC ring and the parameter map in (tracks, FX, master), recorded

**Goal:** Every CC, Program Change and channel-11 note-on reaches the main loop through a ring; midicc.c applies the mapped CCs to the synth / kit tracks' parameters, FX I / II and the master, scaled to each range, and records a track parameter into its own track's automation when REC is on and playing.

**Files:**
- Create: `app/midicc.c`
- Modify: `app/app.c:11` (include), `app/engine.c` (ring; `eng_cc_level` removed), `app/engine.h` (`engine_cc_take`; `eng_cc_level` removed), `app/ui.c:3755-3760` (the drain), `app/ui.c` top (prototypes)
- Test: `tests/scenarios/midicc.xy`

**Acceptance Criteria:**
- [ ] CC 7 / 10 / 91 / 93 / 74 / 71 / 73 / 72 / 75 / 5 / 1 on channel 2 set T2's level, pan, sends, cutoff, reso, attack, release, decay, glide, LFO amount.
- [ ] On channel 1 (the kit T1): 74 the bus hi cut (TP_KTRK), 71 the bus low cut (TP_CUT), 73 kit attack (TP_D), 72 kit length (TP_R); 75 / 5 / 1 do nothing.
- [ ] Channel 9: 7 FX I's level, 16-19 its knobs, 74 / 75 its low-pass / high-pass, 93 its feed into FX II; channel 10 the same for FX II without 93.
- [ ] Channel 11: 7 the master level, 16-18 EQ, 20-23 color, 24-27 out, 28 pump release; CC 20 = 127 gives color type 2 (scaled), 64 gives 1.
- [ ] REC held, playing: CC 7 on channel 3 while T2 is selected writes a lane on T3's parameter 48.
- [ ] The existing CC 7 check in edits.xy still passes.

**Verify:** `app/build/host/loom_host tests/scenarios/midicc.xy app/build/scenarios/midicc` → exit 0, no FAIL.

**Steps:**

- [ ] **Step 1: Failing scenario** (append to midicc.xy):

```
# the map in: a synth (T2 on channel 2)
projnew
midi B1 07 40
midi B1 0A 00
midi B1 5B 7F
midi B1 5D 20
midi B1 4A 10
midi B1 47 7F
midi B1 49 05
midi B1 48 06
midi B1 4B 07
midi B1 05 08
midi B1 01 00
wait 40
expect par2.48 64
expect par2.49 0
expect par2.22 127
expect par2.23 32
expect par2.17 16
expect par2.18 127
expect par2.9 5
expect par2.12 6
expect par2.10 7
expect par2.6 8
expect par2.26 0
# a kit (T1 on channel 1): the bus's cuts, the kit's attack and length; 75 does nothing there
midi B0 4A 11
midi B0 47 12
midi B0 49 13
midi B0 48 14
midi B0 4B 15
wait 40
expect par1.20 17
expect par1.17 18
expect par1.10 19
expect par1.12 20
expect par1.9 0
# FX I on 9, FX II on 10
midi B8 07 21
midi B8 10 22
midi B8 13 23
midi B8 4A 24
midi B8 4B 25
midi B8 5D 26
midi B9 11 27
midi B9 5D 28
wait 40
expect par11.48 33
expect par11.1 34
expect par11.4 35
expect par11.17 36
expect par11.19 37
expect par11.23 38
expect par12.2 39
# the master on 11: level, EQ, a switch scaled (color type 0..2), release
midi BA 07 29
midi BA 10 2A
midi BA 14 7F
midi BA 1C 2B
wait 40
expect mst.level 41
expect mst.lo 42
expect mst.color 2
expect mst.release 43
midi BA 14 40
wait 40
expect mst.color 1
# recorded: REC held while playing, a CC on channel 3 writes T3's lane (T2 selected)
turn ALGO 1
tap PLAY
wait 300
press REC
wait 20
midi B2 07 10
wait 200
midi B2 07 70
wait 200
release REC
tap PLAY
expect lane 3 1 48 1
```

(par11 / par12: `expect parT.P` counts tracks from 1, so AUX_FX1 = NTRK + 2 = 11 and AUX_FX2 = 12 — check `pattern.h`: `AUX_BRAIN = NTRK (8, track 9), AUX_PUNCH 10, AUX_FX1 11, AUX_FX2 12`.)

- [ ] **Step 2: Run it**: FAIL on `expect par2.49 0` (only CC 7 is handled).

- [ ] **Step 3: The ring.** In `app/engine.c`, delete `volatile uint8_t eng_cc_level[NTRK] = ...;` (line 102) and add in its place:

```c
/* MIDI for the main loop (midicc.c): every CC, Program Change and the master channel's note-ons, raw USB-MIDI
 * packets; the interrupt writes, the main loop reads (engine_cc_take); full, a message is dropped */
#define CCQ 16u
static volatile uint32_t ccq[CCQ];
static volatile uint8_t ccq_w, ccq_r;
static void ccq_put(uint32_t pkt)
{
    uint8_t w = ccq_w;
    if ((uint8_t)(w - ccq_r) >= CCQ)
        return;
    ccq[w % CCQ] = pkt;
    ccq_w = (uint8_t)(w + 1u);
}
int engine_cc_take(uint32_t *pkt)
{
    uint8_t r = ccq_r;
    if (r == ccq_w)
        return 0;
    *pkt = ccq[r % CCQ];
    ccq_r = (uint8_t)(r + 1u);
    return 1;
}
```

In `midi_in`, the note and CC branches become (the bend branch as Task 1):

```c
    if (ty == 0x90 || ty == 0x80) {
        int t = midi_track(ch);
        if (ch == CH_MASTER) {                     /* the master's channel: notes pick scenes (midicc.c) */
            if (ty == 0x90)
                ccq_put(pkt);
        } else if (t >= 0)
            seq_key(&seq, t, d1, ty == 0x90, d2);
    } else if (ty == 0xB0 || ty == 0xC0)            /* CCs and Program Change: the main loop's (midicc.c) */
        ccq_put(pkt);
    else if (ty == 0xE0) {
```

In `app/engine.h`, replace the `eng_cc_level` declaration (two lines) with:

```c
int engine_cc_take(uint32_t *pkt);                       /* the main loop: the next CC / Program Change / master-channel
                                                          * note-on from MIDI in (a USB-MIDI packet), 0 none */
```

- [ ] **Step 4: midicc.c, the map in.** Create `app/midicc.c`:

```c
/* SPDX-License-Identifier: GPL-3.0-only */
/* LOOM-8's MIDI map (docs/superpowers/specs/2026-10-21-midi-cc-map-design.md): CCs in, as the knob or key would set
 * them (recorded with REC on, playing); CCs out for a knob turned by hand; the scenes by Program Change or notes on the
 * master's channel. Part of app.c's unity build, after ui.c: set_tp, set_mute, kit_sound_mute, scene_go. */

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
/* FX I / II (FX II has no feed: 93 on channel 10 does nothing) */
static const struct { uint8_t cc, par; } CC_FX[] = {
    {7, TP_LEVEL}, {16, TP_E1}, {17, TP_E2}, {18, TP_E3}, {19, TP_E4}, {74, TP_CUT}, {75, TP_FENV}, {93, TP_SEND2}};
/* the master */
static const struct { uint8_t cc, m; } CC_MST[] = {
    {7, MS_LEVEL},  {16, MS_EQ_LO}, {17, MS_EQ_MID}, {18, MS_EQ_HI}, {20, MS_CTYPE}, {21, MS_CAMT},     {22, MS_CTONE},
    {23, MS_CMIX},  {24, MS_PERC},  {25, MS_MEL},    {26, MS_COMP},  {27, MS_PUMP},  {28, MS_PUMP_REL}};

static int cc_to(int v, int lo, int hi) { return lo + (v * (hi - lo) + 63) / 127; }   /* 0..127 onto lo..hi */

static void cc_par(int t, int p, int v)       /* a track's parameter from a CC (recorded as a knob turn would be) */
{
    const tp_info_t *in = tp_info_par(p, proj.trk[t].par);
    set_tp(t, p, is_aux(t) ? v : cc_to(v, in->lo, in->hi));
    if (seq.rec && playing() && !is_aux(t))
        seq_rec_knob(&seq, t, p, proj.trk[t].par[p]);
}
static void cc_master(int m, int v)
{
    undo_master();
    proj.master[m] = (uint8_t)cc_to(v, 0, master_max(m));
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
            if (CC_FX[i].cc == d1 && !(ft == AUX_FX2 && CC_FX[i].par == TP_SEND2))
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
```

(`master_max` is fx.h's, `int master_max(int p)`.)

- [ ] **Step 5: Include and drain.** `app/app.c`: after `#include "ui.c"` add `#include "midicc.c"`. At the top of `app/ui.c` after the `undo_arr` prototype (line 158) add:

```c
static void cc_in_drain(void);                    /* midicc.c: MIDI in's CCs, Program Changes, scene notes */
```

Replace `ui.c:3755-3760` (the `eng_cc_level` loop) with:

```c
    cc_in_drain();                               /* MIDI in: the CC map, the scenes (midicc.c) */
```

- [ ] **Step 6: Build, run** the Build and test block. Expected: midicc.xy passes; edits.xy's CC 7 check (`midi B2 07 40` → `par3.48 64`) still passes.

- [ ] **Step 7: Commit**

```bash
git add app/midicc.c app/app.c app/engine.c app/engine.h app/ui.c tests/scenarios/midicc.xy
git commit -m "LOOM-8: the MIDI CC map in (midicc.c): a track's level, pan, sends, cutoff, reso, attack, release, decay, glide, LFO amount; a kit's bus cuts, attack, length; FX I / II; the master; recorded on its own track"
```

---

### Task 3: Mutes, a kit's sounds, a kit's sends

**Goal:** CC 9 mutes a track and 16-26 a kit's sounds (as the Mix page / Arrange do, saved into the scene playing); 80-90 and 102-112 set a kit sound's pan and level; 91 / 93 on a kit move every sound's send by the change since the last value (64 before the first).

**Files:**
- Modify: `app/midicc.c`
- Test: `tests/scenarios/midicc.xy`

**Acceptance Criteria:**
- [ ] CC 9 = 127 on channel 3 sets mute bit 2; 127 again changes nothing; 0 clears it.
- [ ] CC 17 = 127 on channel 1 mutes the kit's sound 2 (`expect kmute1 2`); 0 unmutes.
- [ ] CC 81 = 0 on channel 1 sets sound 2's pan (par1.78 = 0); CC 103 = 90 its level (par1.79 = 90).
- [ ] With sound 1's send I at 10 and sound 2's at 40, CC 91 = 74 then 84 on channel 1 leaves them 30 and 60 (offsets +10, +10 from 64).

**Verify:** `app/build/host/loom_host tests/scenarios/midicc.xy app/build/scenarios/midicc` → exit 0, no FAIL.

**Steps:**

- [ ] **Step 1: Failing scenario** (append):

```
# mutes as the Mix page's: T3 by CC 9, the kit's sound 2 by CC 17
projnew
midi B2 09 7F
wait 20
expect mute 4
midi B2 09 7F
wait 20
expect mute 4
midi B2 09 00
wait 20
expect mute 0
midi B0 11 7F
wait 20
expect kmute1 2
midi B0 11 00
wait 20
expect kmute1 0
# a kit sound's pan and level (sound 2: par 52 + 20 + 6 = 78, + 7 = 79)
midi B0 51 00
midi B0 67 5A
wait 20
expect par1.78 0
expect par1.79 90
# a kit's send I by an offset: sound 1's (par 70) and sound 2's (par 90) keep their gap
param 1 70 10
param 1 90 40
midi B0 5B 4A
wait 20
midi B0 5B 54
wait 20
expect par1.70 30
expect par1.90 60
```

- [ ] **Step 2: Run it**: FAIL on `expect mute 4`.

- [ ] **Step 3: Implement.** In `app/midicc.c`, before `cc_track`:

```c
static uint8_t cc_ksend[NTRK][2] = {[0 ... NTRK - 1] = {64, 64}};   /* a kit's last CC 91 / 93 (the offsets' base) */
static void cc_mute(int t, int on)            /* as the Mix page's tap: the mute now, in the scene playing */
{
    if ((int)(seq.mute >> t & 1u) != on)
        set_mute(t, on);
}
static void cc_kmute(int t, int snd, int on)  /* a kit's sound, as Arrange's (the first four kits) */
{
    int k = kit_slot(t);
    if (k >= 0 && (int)(seq.kmute[k] >> snd & 1u) != on)
        kit_sound_mute(t, snd);
}
static void cc_kit_send(int t, int which, int v)   /* every sound's send moved by the CC's change */
{
    int d = v - cc_ksend[t][which];
    cc_ksend[t][which] = (uint8_t)v;
    for (int k = 0; k < DL_NSOUND; k++) {
        int p = TP_DRUM(k, DP_SEND1 + which);
        set_tp(t, p, proj.trk[t].par[p] + d);    /* (set_tp clamps to the send's range) */
    }
}
```

and replace `cc_track` with:

```c
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
            else if (p < TP_DRUM0)
                cc_par(t, p, v);
            return;
        }
}
```

(Check `set_tp`'s clamp for drum indices: it clamps with `tp_info_par(p, par)` → DRUMP's send range 0..127. A negative `par + d` is clamped by `clampi` to `lo`.)

- [ ] **Step 4: Build, run.** Expected: pass. If `expect mute 4` fails because `seq.mute` holds more bits after `projnew`, compare against the value before: use `expect mute 4` only after `projnew` (a new project has no mutes).

- [ ] **Step 5: Commit**

```bash
git add app/midicc.c tests/scenarios/midicc.xy
git commit -m "LOOM-8: MIDI CC mutes (9 a track, 16-26 a kit's sounds, as the Mix page and Arrange), a kit sound's pan and level (80-90, 102-112), a kit's sends by an offset (91 / 93)"
```

---

### Task 4: CC OUT (on by default), sent by hand only

**Goal:** A new GLO list row CC OUT (on by default, stored as `CC_OUT_OFF` in `proj.rsv[2]`); with it on, a mapped parameter changed by a knob or a SEL list row sends its CC on its channel (track: when its MIDI OUT is on); CCs that came in, locks, automation and presets send nothing.

**Files:**
- Modify: `app/pages.c` (`CC_OUT_OFF`, `A_CC_OUT` + its name, GLO's row)
- Modify: `app/ui.c` (A_CC_OUT pick, the hand flag around `knob()` and `list_change()`, `set_tp`'s and `master_knob`'s hooks, the list's non-actions list at `ui.c:2829-2830`)
- Modify: `app/midicc.c` (senders), `app/host.c` (`expect ccout`, `ccouts`)
- Test: `tests/scenarios/midicc.xy`

**Acceptance Criteria:**
- [ ] A new project: CC OUT on; turning T2's level knob on Mix sends status 0xB1, CC 7 (`expect ccst 177`, `ccd1 7`).
- [ ] Turning a master knob mapped in CC_MST on Master out sends status 0xBA with its CC.
- [ ] A CC that came in sends nothing (`expect ccouts 0` after `ccreset`).
- [ ] CC OUT off (GLO's SEL list) or T2's MIDI OUT off: the knob sends nothing (`ccouts 0`).

**Verify:** `app/build/host/loom_host tests/scenarios/midicc.xy app/build/scenarios/midicc` → exit 0, no FAIL.

**Steps:**

- [ ] **Step 1: Host expects and a reset.** In `app/host.c` `app_expect`, before the final `else if (!strncmp(what, "par", 3) ...`:

```c
    else if (!strcmp(what, "ccouts"))               /* CCs / Program Changes sent (midicc.c) since "ccreset" */
        *got = (int)cc_out_n;
    else if (!strcmp(what, "ccst"))                 /* the last one's status, first and second data byte */
        *got = (int)(cc_out_last >> 16);
    else if (!strcmp(what, "ccd1"))
        *got = (int)(cc_out_last >> 8 & 0xFFu);
    else if (!strcmp(what, "ccd2"))
        *got = (int)(cc_out_last & 0xFFu);
```

and among the commands, after `else if (!strcmp(cmd, "projnew")) do_new_proj();`:

```c
    else if (!strcmp(cmd, "ccreset"))              /* the CCs-out count from 0 (expect ccouts) */
        cc_out_n = 0;
```

- [ ] **Step 2: Failing scenario** (append). Mix is GLO's first page; K4 is the level. Statuses are decimal: 0xB1 = 177, 0xBA = 186:

```
# CC OUT: on in a new project; a knob turned by hand sends its CC; a CC in sends nothing
projnew
turn ALGO 1
expect trk 2
tap GLO
expect page Mix
ccreset
turn K4 -1
wait 20
expect ccst 177
expect ccd1 7
ccreset
midi B1 07 40
wait 20
expect par2.48 64
expect ccouts 0
# MIDI OUT off on T2: its knob sends nothing
param 2 51 0
ccreset
turn K4 1
wait 20
expect ccouts 0
param 2 51 1
# the master's level (Master out: GLO's fourth page) on channel 11
turn SELECT 3
expect page Master out
ccreset
turn K4 -1
wait 20
expect ccst 186
expect ccd1 7
# CC OUT off (GLO's SEL list: the row before ALL NOTES OFF, the last): nothing sent
tap SEL
turn SELECT 40
turn SELECT -1
turn ALGO -1
tap SEL
expect page Master out
ccreset
turn K4 1
wait 20
expect ccouts 0
```

(If `turn SELECT 3` from Mix lands elsewhere, use the page names in tests/scenarios/pages.xy; `expect page` says where it is. If `MS_LEVEL` is not Master out's K4, use the knob that `expect mst.level` shows moving.)

- [ ] **Step 3: Run it**: FAIL `expect ccouts 1` (got 0).

- [ ] **Step 4: The row.** `app/pages.c`, after `#define SCENES_AT_END 2u`:

```c
#define CC_OUT_OFF 16u                                /* proj.rsv[2]: no MIDI CCs out (midicc.c; on by default) */
```

Add `A_CC_OUT` at the end of the action enum (before `NACTIONS`) and `"CC OUT"` at the end of `ACTION_NAME`. In `list_build`'s GLO branch, after the AUTOSAVE row:

```c
        list_add(ACTION_NAME[A_CC_OUT], proj.rsv[2] & CC_OUT_OFF ? "off" : "on", LK_PICK, A_CC_OUT);
```

In `app/ui.c` `list_change`'s `LK_PICK` switch, after `case A_LOOP_FREE: ... break;`:

```c
        case A_CC_OUT:
            proj.rsv[2] = (uint8_t)(d > 0 ? proj.rsv[2] & ~CC_OUT_OFF : proj.rsv[2] | CC_OUT_OFF);
            mark_dirty();
            break;
```

and add `case A_CC_OUT:` to the list at `ui.c:2830` (`case A_MIDI_CH: case A_LOOP_FREE: return;` → `case A_MIDI_CH: case A_LOOP_FREE: case A_CC_OUT: return;`).

- [ ] **Step 5: The senders.** Append to `app/midicc.c`:

```c
/* ---- out: a mapped value changed by hand (a knob, a SEL list row), with CC OUT on ---- */
static uint32_t cc_out_last, cc_out_n;        /* (the host's expect ccout, ccouts) */
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
```

- [ ] **Step 6: The hooks.** In `app/ui.c`, beside the `cc_in_drain` prototype:

```c
static void cc_out_param(int trk, int p);         /* midicc.c: a parameter changed (sent when by hand, CC OUT on) */
static void cc_out_master(int m);
static uint8_t cc_hand;                           /* (midicc.c's: knob() and list_change() running) */
```

(`cc_hand` lives in ui.c only: midicc.c, included after it, uses it.) In `set_tp`, just before its final `mark_dirty();` (after `apply_brain`):

```c
    cc_out_param(trk, p);
```

In `master_knob`, after `engine_master(m, proj.master[m]);`: `cc_out_master(m);`. In `list_change`'s `case A_PUMP_BY: case A_PUMP_REL: case A_PUMP_ACC: case A_LEVEL:` block, after its `engine_master(...)` call: `cc_out_master(m);`.

Rename `static void knob(int k, int32_t e)` (ui.c:1929) to `static void knob_do(int k, int32_t e)` and add right after it:

```c
static void knob(int k, int32_t e)               /* a knob turned by hand: what it changes goes out as its CC */
{
    cc_hand = 1;
    knob_do(k, e);
    cc_hand = 0;
}
```

If ui.c has a prototype `static void knob(int k, int32_t e);` earlier, it stays (it names the wrapper). `list_change` the same way: rename it `list_change_do`, wrapper `list_change` setting `cc_hand` around it. Check that no path inside them returns to the caller with `cc_hand` left set: the wrappers clear it after.

- [ ] **Step 7: Build, run.** Expected: midicc.xy passes. Then the whole suite: other scenarios turn knobs too and now send CCs over the log; none counts `midi_out` exactly except where it checks clocks or notes: if one fails on a `midi_out` count, make that scenario turn CC OUT off first is wrong (it hides a change) — instead update its expected count, noting why in a comment.

- [ ] **Step 8: Commit**

```bash
git add app/midicc.c app/ui.c app/pages.c app/host.c tests/scenarios/midicc.xy
git commit -m "LOOM-8: CC OUT (GLO's list, on by default): a mapped knob turned by hand sends its CC on its track's channel (FX 9 / 10, the master 11); CCs in, locks and automation never"
```

---

### Task 5: Scenes over MIDI

**Goal:** Program Change 0-31 and notes 36-67 on channel 11 cue scenes 1-32 the way a white key on Arrange does (SCENES SWITCH decides when); a scene change on the FM-1 sends its Program Change on channel 11 (CC OUT on), not one that came in over MIDI.

**Files:**
- Modify: `app/midicc.c` (scene in, `cc_out_scene`), `app/ui.c` (`ui_frame`'s scene change: the out; prototype)
- Test: `tests/scenarios/midicc.xy`

**Acceptance Criteria:**
- [ ] Stopped: `midi CA 02` → `expect scene 3`; `midi 9A 25 64` (note 37) → `expect scene 2`.
- [ ] Playing: Program Change 4 cues scene 5 for the next bar (scene still the old one 20 ms later; 5 after a bar).
- [ ] A scene picked on Arrange's white key sends `CA nn` (`expect ccst 202`, `ccd1` = scene - 1); one that came in over MIDI does not (`expect ccouts 0` after `ccreset`).

**Verify:** `app/build/host/loom_host tests/scenarios/midicc.xy app/build/scenarios/midicc` → exit 0, no FAIL.

**Steps:**

- [ ] **Step 1: Failing scenario** (append; on Arrange the white keys are scenes 1-16: `tapkey w0` = scene 1, as tests/scenarios/scenes.xy):

```
# scenes: Program Change and notes 36-67 on channel 11, as Arrange's white keys
projnew
ccreset
midi CA 02
wait 40
expect scene 3
midi 9A 25 64
wait 40
expect scene 2
expect ccouts 0
# playing: cued for the next bar (SCENES SWITCH next bar)
tap PLAY
wait 100
midi CA 04
wait 20
expect scene 2
wait 2200
expect scene 5
tap PLAY
# a scene picked on the FM-1 goes out as its Program Change (scene 1: CA 00; 0xCA = 202)
tap HOME
expect page Arrange
tapkey w0
wait 40
expect scene 1
expect ccst 202
expect ccd1 0
```

(2200 ms: at 120 BPM a bar is 2000 ms.)

- [ ] **Step 2: Run it**: FAIL `expect scene 3`.

- [ ] **Step 3: Scene in.** In `app/midicc.c`, before `cc_in`:

```c
static uint8_t cc_scene_quiet = 0xFF;         /* a scene asked for over MIDI: its change is not sent back out */
static void cc_scene(int n)                   /* as a white key on Arrange: made if unused, cued by SCENES SWITCH */
{
    if (n < 0 || n >= NSCENE)
        return;
    if (n != cur_scene())
        cc_scene_quiet = (uint8_t)n;
    scene_go(n, proj.rsv[2] & SCENES_AT_END ? SCENE_END : SCENE_BAR);
}
```

At the start of `cc_in`, replace `if (ty != 0xB0) return;` with:

```c
    if (ty == 0xC0) {                         /* Program Change: on the master's channel, the scene */
        if (ch == CH_MASTER)
            cc_scene(d1);
        return;
    }
    if (ty == 0x90) {                         /* the master's channel's notes 36-67: scenes 1-32 */
        if (ch == CH_MASTER && v && d1 >= 36)
            cc_scene(d1 - 36);
        return;
    }
    if (ty != 0xB0)
        return;
```

- [ ] **Step 4: Scene out.** Append to `app/midicc.c`:

```c
static void cc_out_scene(int n)               /* ui_frame: the scene playing changed */
{
    if (cc_scene_quiet == n) {
        cc_scene_quiet = 0xFF;
        return;
    }
    cc_send(0xC0 | CH_MASTER, n, 0);
}
```

In `app/ui.c`, add the prototype `static void cc_out_scene(int n);` beside the others, and in `ui_frame`'s `if (cur_scene() != ui.scene_seen) {` block, first line inside:

```c
        cc_out_scene(cur_scene());               /* (MIDI: its Program Change, midicc.c) */
```

`cc_send` packs a Program Change as a three-byte packet with d2 0; USB-MIDI's code index for 0xC is a two-byte message (CIN 0xC), which `st >> 4` gives: the receiver ignores the third byte.

- [ ] **Step 5: Build, run.** Expected: pass. (`ccreset` and the `ccouts` expects come from Task 4.)

- [ ] **Step 6: Commit**

```bash
git add app/midicc.c app/ui.c tests/scenarios/midicc.xy
git commit -m "LOOM-8: scenes over MIDI: Program Change 0-31 and notes 36-67 on channel 11 pick scenes 1-32 as Arrange's keys do; a scene change sends its Program Change (not one that came in)"
```

---

### Task 6: Docs, the whole suite, the FM-1 build

**Goal:** The manual describes the fixed channels, the map, the scenes and CC OUT; the release notes say it; the full suite passes; the device build fits.

**Files:**
- Modify: `docs/MANUAL.md` (the MIDI part, ~line 896-902)
- Modify: `docs/feedback-2026-10-21.md` (Draft release notes: MIDI)

**Acceptance Criteria:**
- [ ] MANUAL.md has the channel table, the track / kit / FX / master CC tables (as the spec's), scenes over MIDI, CC OUT and MIDI OUT; no "MIDI CHANNEL" left.
- [ ] `sh tests/run_tests.sh` ends `all tests passed` (the reader's audit included).
- [ ] `./build.sh` builds `build/loom.fwsc`; flash and RAM printed under the slot (581 KB) and RAM (98.3 KB) limits.

**Verify:** `PYTHON=/opt/homebrew/bin/python3.14 sh tests/run_tests.sh | tail -1` → `all tests passed`; `PYTHON=/opt/homebrew/bin/python3.14 ./build.sh 2>&1 | tail -5` → the size lines, no error.

**Steps:**

- [ ] **Step 1: Manual.** In `docs/MANUAL.md`, replace the sentence about MIDI CHANNEL (line ~899: "**SEL** list on its sound pages has MIDI CHANNEL, off or 1 to 16, which also picks the track for notes coming in on ...") with a MIDI section: "Over USB MIDI each track has its own channel: T1 to T8 on channels 1 to 8, FX I on 9, FX II on 10, the master and the scenes on 11; notes on 12 to 16 play the track selected. A track's **SEL** list on its sound pages has MIDI OUT (on in a new project): its notes go out, with Setup's NOTES on." followed by the spec's two map tables (copied from docs/superpowers/specs/2026-10-21-midi-cc-map-design.md, "The map"), a paragraph "Scenes: Program Change 0 to 31 on channel 11, or notes C1 to G3 (36 to 67) there, pick scenes 1 to 32 as Arrange's white keys do (SCENES SWITCH decides when). A scene changed on LOOM-8 sends its Program Change on 11.", and "CC OUT, in **GLO**'s **SEL** list (on in a new project): a mapped knob you turn sends its CC on its track's channel; what comes in, locks and automation are not sent back." Check: `grep -n "MIDI CHANNEL" docs/MANUAL.md` → nothing.

- [ ] **Step 2: Release notes.** In `docs/feedback-2026-10-21.md` "Draft release notes", replace "MIDI: notes out by default, MIDI CHANNEL per track, CC 7 level, kit notes with lengths" with "MIDI: fixed channels (T1-T8 on 1-8, FX 9 / 10, the master 11), a CC map for the mixer, sounds, kits, FX and master, in (recorded with REC) and out (CC OUT), scenes by Program Change or notes on 11, MIDI OUT per track, kit notes with lengths".

- [ ] **Step 3: The whole suite.** Run `PYTHON=/opt/homebrew/bin/python3.14 sh tests/run_tests.sh`. Expected `all tests passed`. The reader's audit (words) may flag the new row names "MIDI OUT", "CC OUT": if it does, add them to `app/words.tsv` / `app/words.allow` the way existing rows are (look at how "USB AUDIO" is listed: `grep -n "USB" app/words.tsv app/words.allow`).

- [ ] **Step 4: The FM-1 build.** Run `PYTHON=/opt/homebrew/bin/python3.14 ./build.sh`. Expected: builds; note the flash and RAM lines against 556 KB / 93.9 KB before (expected growth ~1 KB flash, ~0.1 KB RAM).

- [ ] **Step 5: Commit**

```bash
git add docs/MANUAL.md docs/feedback-2026-10-21.md app/words.tsv app/words.allow
git commit -m "LOOM-8 docs: MIDI's fixed channels, the CC map, scenes over MIDI, CC OUT and MIDI OUT in the manual and the release notes"
```

- [ ] **Step 6: Hand to the user for the FM-1** (not automated): flash (`python3 tools/fm1_install.py build/loom.fwsc --yes`, after a backup to `build/backups/`), then from a DAW or a MIDI monitor: CC 7 on channel 3 moves T3's level; turning T2's level sends CC 7 on channel 2; Program Change 1 on channel 11 cues scene 2.
