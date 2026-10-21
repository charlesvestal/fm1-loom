# C15: the SELECT lists reviewed (2026-10-21)

From the testers' feedback (docs/feedback-2026-10-21.md, C15): a sweep of every SELECT list, like the help cards'.
Reviewed from the simulator's `speakdump` (every page's list, as the screen reader gets it) on a kit (T1), a synth
(T2), the conductor, the throws, FX I / II and the weave.

## Rules every list keeps

1. A button's list is the same on every page of that button. (FX keeps a list per page, as now: its pages are
   different things.)
2. Page by page: a page's knobs, then the settings and actions that belong to that page; the button's own actions
   after; ALL NOTES OFF, where a list has it, last.
3. One row per **parameter**: `list_build` drops a repeat by its parameter (the knob's TP_* / master index), not by
   name + value. Two rows of one list never share a name.

## Found, and the fix

| # | Where | Now | Fix |
|---|---|---|---|
| 1 | every list (pages.c `list_build`) | repeats dropped by name + value: a synth's filter-envelope attack and release vanish when they equal the amp's; its decay and sustain show as a second "decay", "sustain" | drop by parameter (rule 3) |
| 2 | ENV, synth | two "attack", "decay", ... | the filter envelope's rows: **f attack, f decay, f sustain, f release** (the list's names; the page keeps its own) |
| 3 | ENV, kit | "lag", "attack", "drive" twice (the key's, the kit's) | the kit's: **kit lag, kit attack, kit random, kit length**; the bus's drive **bus drive** |
| 4 | ENV, kit | FILTER TYPE is the key's on Envelope / Filter, the bus's on Kit env / Kit filter | both rows on all four pages: **KEY FILTER** (the key's type) and **KIT FILTER** (on / off) |
| 5 | EDIT, kit | Engine and Key list PLAY MODE, SCRAMBLE, SAVE AS PRESET, ENGINE, MIDI; the third page (Sends, `P_PLAY2`) lists PRESET, RESET SOUND, FOLLOWS instead and no PLAY MODE (`pages.c` the `P_ENGINE \|\| P_PLAY` test) | one list: the three pages' knobs, PLAY MODE, then the sound block (below) without FOLLOWS |
| 6 | the sound actions | on EDIT, LFO and Players, not on ENV | **EDIT only** (decided): ENGINE, PRESET, SCRAMBLE, SAVE AS PRESET, RESET SOUND, FOLLOWS CONDUCTOR, MIDI OUT (C3's name for MIDI CHANNEL) |
| 7 | Players | the Weave page's list: KEEP AS PATTERN, no sound actions; the other pages the reverse | one list: the players' knobs, the weave's knobs, KEEP AS PATTERN |
| 8 | FOLLOWS CONDUCTOR on a kit | listed; a kit's notes are its sounds (the conductor would move them onto other keys) | not on kits; and the sequencer's `brain_follows` skips kit tracks (a kit with TP_BRAIN set from before follows nothing) |
| 9 | GLO | 32 rows: the pump's knob far from PUMP BY / RELEASE / ACCENT (after Setup); "level" (the track's) and "LEVEL" (the master's); "amount" (color) and "AMOUNT" (the LFO) | grouped by page (below), the names made distinct |
| 10 | HOME | INSERT SCENE and DELETE SLOT (both a song slot) | **INSERT SLOT** |
| 11 | FX, Sends | ALL NOTES OFF | removed (HOME's and GLO's lists have it) |

## The lists after

- **EDIT, synth**: Engine, Play, Play 2, Sub knobs; ENGINE, PRESET, SCRAMBLE, SAVE AS PRESET, RESET SOUND,
  FOLLOWS CONDUCTOR, MIDI OUT.
- **EDIT, kit**: Engine, Key, Sends knobs (the key's); PLAY MODE; ENGINE, PRESET, SCRAMBLE, SAVE AS PRESET,
  RESET SOUND, MIDI OUT.
- **ENV, synth**: attack, decay, sustain, release; cutoff, reso, env, keytrk; f attack, f decay, f sustain, f release;
  FILTER TYPE.
- **ENV, kit**: attack, hold, decay, lag (the key's); cutoff, reso, env, drive (the key's filter); kit lag, kit attack,
  kit random, kit length; lowcut, comp, bus drive, hicut; KEY FILTER, KIT FILTER.
- **LFO**: its pages' knobs only.
- **Players**: the players' knobs, the weave's (WITH, PATTERN, RULE, PLAYS), KEEP AS PATTERN.
- **FX**: per page, as now (Character: its four; Sends: its four; FX I / II: the knobs, EFFECT, HIGH-PASS, LOW-PASS,
  TO FX II on FX I; Throws: LATCH).
- **GLO**: Mix (the selected track's: FX I, FX II, pan, **T2 level**); Mix 2 (C9: **T2 low cut**, **T2 high cut**); EQ (low, mid, high); color (color, **color
  amount**, tone, mix); out (drums, melodic, comp, pump, PUMP BY, RELEASE, ACCENT, **MASTER LEVEL**); LFO (SHAPE, RATE,
  **LFO AMOUNT**, DEST); Setup (LIGHTS, CLOCK, NOTES, THEME, USB AUDIO, LOOP THROWS, AUTOSAVE, CC OUT from C3);
  Debug (CORES, debug builds); ALL NOTES OFF. 35 rows: LIST_ROWS 36 -> 40 (+128 B RAM).
- **HOME**: as now (Arrange's and Song's knobs, the projects, the song, the scenes, ALL NOTES OFF), INSERT SLOT.
- **Steps / Bar, conductor, throws, weave**: as now.

Names in a list come from the knob (`page_knobs`) unless the list gives its own: a small table in pages.c of
(page, knob) -> list name for the rows above (f attack .., kit lag .., bus drive, color amount, LFO AMOUNT,
"T2 level" built from the track's name).

## Tests

- **Lists** (host scenario or a host-built check): every page's list for a synth, a kit, the conductor, the throws,
  FX I / II, the weave: each button's list the same on all its pages (FX excepted); no two rows of a list with the
  same name; no parameter twice; at most LIST_ROWS rows.
- **Presets**: every factory preset loaded on a synth track: every stored value that differs from its engine's
  default has a knob showing it on one of the track's pages, and every value lies in its knob's range (`tp_info_par`
  lo..hi). (Run once by hand for this review: all 85 presets pass; hiding cutoff on purpose flags 81, so the check
  sees.) The LFO's fields count only when its type is on.
- The words file (`words.tsv`) and the reader's audit for the new names; scripts that count SELECT turns through
  rows (pages.xy, autosave_off.xy, weavepage.xy, edits.xy) count from the end where they can.

## Docs

The manual's SELECT list descriptions (ENV, EDIT on a kit, Players, GLO); the help cards if they name rows.
