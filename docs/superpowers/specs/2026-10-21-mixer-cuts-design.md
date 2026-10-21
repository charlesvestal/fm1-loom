# C9: the mixer's low and high cut (2026-10-21)

From the testers' feedback (docs/feedback-2026-10-21.md, C9): per-track EQ / low and high cut, and per drum sound.
Agreed with the user: a low cut and a high cut on each of the eight tracks, **the mixer's own** (new parameters,
shown nowhere else; they don't stand in for the kit bus's cuts or the FX's HP / LP). No per-key cuts (a key keeps
its one filter); the FX tracks keep their HP / LP and get none.

## What there is now

- A synth voice: one filter, its type selectable (`TP_FTYPE`: lp24, ladder, lp12, hp12, bp12), then the character.
  The voices add straight into the melodic bus: no buffer per track (one would be 8 x 2 x 256 floats = 16 KB; RAM
  has 4.4 KB).
- A kit: each key its own 12 dB filter (LP / HP / BP); the kit renders into its own pair (engine.c `render_drums`:
  `tl` / `tr`), then its bus filter (`FT_DYNAMIC`: low cut, comp, drive, hi cut) and character.
- The master: a 3-band EQ.

## The cuts

- Two parameters a track: **low cut** and **high cut**, 12 dB/oct state-variable filters (high-pass, low-pass).
  Off by default: low cut at its bottom (20 Hz, "off"), high cut at its top (20 kHz, "off"). Ranges: low cut off ..
  2 kHz, high cut 500 Hz .. off, on the kit bus cuts' curve (exponential in the knob).
- Mix settings, like level and pan: not in a preset (`NTP_SOUND` unchanged), kept when a preset loads, lockable and
  automatable like any knob.
- Synth track: per voice, after the voice's filter and character, with its track's values (the voice reads them
  each chunk, as it reads its other parameters). A cut that is off is skipped: a track with both off costs nothing.
- Kit track: on the kit's pair, after the bus filter and character, before level and pan (render_drums).
- FX I / II, the conductor, the throws, the weave: none.

## Where

GLO gets **Mix 2** after Mix: the selected track's low cut (knob 1) and high cut (knob 4); knobs 2 and 3 empty. On a
track without cuts (FX, conductor, throws, weave) the page's knobs are empty. GLO's list (C15) adds the two rows
after Mix's (**T2 low cut**, **T2 high cut**, named with the track as Mix's level is).

## Storage: format 14

- `TP_LOCUT`, `TP_HICUT` at the **end** of `par[]`, after the drum block: `NTP = TP_DRUM0 + 11 * DP_N + 2`. No
  parameter's index moves: locks and lanes (`lane_t.param`), LFO destinations and the MIDI map keep theirs.
- Format 13 (and 10 .. 12, through `migrate_old`) load with both cuts off: each track's par is two bytes shorter in
  the old stream; the stream's length decides, as now; saved whole at the first touch.
- New fixture `tests/fixtures/store_v13.bin` (made on a format-13 build with the host's storesave, as v10 / v11),
  checked in tests/scenarios/migrate.xy.
- The filters' state outside the pool (.bss in synth.c / engine.c): 16 voices x 32 B (two filters, two states, two
  channels) + 3 kit units (`NDL`) x 32 B = ~0.6 KB of RAM (4.4 KB free now, ~3.8 KB after). The pool is untouched.
  The project's own size: 2 bytes x `NALLTRK` in the project, the engine's copy and the undo's.

## With C3 and C15

- C3 (MIDI CC map): CC **14** low cut, **15** high cut on each track's channel (synth and kit alike); sent out with
  CC OUT. (The kit's 74 / 71 stay the bus's hi / low cut.)
- C15 (SELECT lists): GLO's list grows to 35 rows; `LIST_ROWS` 36 -> 40 (+128 B RAM) for room.

## Tests

- Host (tests/host, synth_test / drumline-side): a cut's response (-3 dB within a few percent of its frequency, 12
  dB/oct beyond), a track with both cuts off renders bit-for-bit as before (the dsp hash), a kit's cuts on its pair.
- Migration: store_v13.bin loads, its tracks' cuts off, everything else as before (as the v10 / v11 checks).
- A lock and recorded automation on a cut play back (a scenario).
- On the FM-1 (`tools/perf_read.py`): a heavy project (`tools/demos/device_load.py`) with cuts on all eight
  tracks against none, the peaks at chord changes included; the guard (92 % a block) not reached.

## Docs

The manual (GLO, Mix 2; the MIDI map's 14 / 15), the words file (Mix 2, low cut, high cut), the release notes.
