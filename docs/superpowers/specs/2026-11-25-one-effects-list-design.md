# One effects list (2026-11-25)

LOOM-8 has four effect systems, each with its own list, knobs, code and memory: a track's **character** (released),
a track's **insert** (unreleased, format 26), the sends **FX I / FX II** (released) and the **master color**
(released). Phaser, chorus, echo / delay, tape, sampler and lofi / crush exist two or three times, written twice
(`synth.c` and `fx.c` share no DSP code). The user's ask: one list everywhere.

Decisions (the user's, 2026-11-25):
- **One catalog everywhere**: every slot (a track's two, FX I, FX II, the master) picks from the same list, with the
  same names, knobs and code. Effects needing memory go where memory is free ("no memory" otherwise).
- **Two slots per track**, both from the full list.
- **Three knobs of the effect's own plus mix**, everywhere; on a send, mix is the return level. What no longer fits
  on the knobs goes to the SEL list (the delay's fine; the reverb's damp, and shimmer's amount).
- **The catalog** below; where two versions exist the released sound wins.

## The catalog

Numbers fixed, new effects only ever appended. Knobs 1-3, then the SEL list's extras. "-": no third knob.

| # | Group | Effect | Knobs | Extras | From (sound kept) |
|---|---|---|---|---|---|
| 0 | | off | | | |
| 1 | dirt | drive | amount, tone, - | | character drive |
| 2 | dirt | rat | distort, filter, level | | insert rat |
| 3 | dirt | fold | amount, bias, - | | character fold |
| 4 | dirt | crush | amount, which, - | | character crush |
| 5 | dirt | ring | depth, ratio, - | | character ring |
| 6 | dirt | tilt | pivot, tilt, - | | character tilt |
| 7 | color | tape | amount, tone, - | | character tape (A/B against the master's first) |
| 8 | color | sampler | amount, tone, - | | character sampler (A/B against the master's first) |
| 9 | color | lofi | rate, bits, smooth | width | FX lofi |
| 10 | movement | tremolo | rate, depth, pan | | insert tremolo |
| 11 | movement | phaser | freq, depth, rate | feedback | FX phaser |
| 12 | movement | univibe | rate, depth, vibrato | | insert univibe |
| 13 | movement | chorus | rate, depth, feedback | width | FX chorus |
| 14 | movement | juno | mode, tone, - | | insert juno |
| 15 | movement | autowah | sense, reso, decay | | insert autowah |
| 16 | space | delay | time, repeats, tone | fine, width, wobble, smear | FX delay (format 27's: held repeats, wobble, smear) |
| 17 | space | rumble | decay, tone, - | | insert rumble (with its gate) |
| 18 | space | reverb | size, decay (plate: motion), tone | mode, damp (shimmer: shimmer) | FX reverb, its twelve modes |

Mix (knob 4): dry 0 .. wet 127 on a track or the master (0 skips the stage); the return level on a send (an
effect's output there is wet only). The inserts' own mix knobs (chorus, echo, juno, rumble) become knob 4.

**Ring and tape on a synth track** stay per voice (ring follows each note's pitch; tape's wow bends the voices'
pitch), so released projects sound the same. On a kit, a send or the master: ring at a ratio of C4 (as kits now),
tape's wow through a short line (as the master's now).

## Architecture

- **`app/dsp/efx.c` / `efx.h`**: the catalog (names, knobs with defaults and kinds, extras, memory need in 2 KB
  blocks, whether it needs the voice) and per effect a coefficients function (knobs to numbers, the main loop) and a
  process function (a stereo block in place, with mix; `lag` for a send a block late, as `fx_process_lag`). One state
  struct per slot (type, knobs, numbers, filter states; its memory: a pointer into the pool).
- **One memory pool** in 2 KB blocks, replacing the FX I delay lines (44.8 KB), the FX II reverb line (64 KB), the
  inserts' pool (8 KB) and each FX slot's chorus line (2 x 4 KB): about 61 blocks. Allocated contiguously, first fit,
  each block (as `ins_alloc` now); a slot that gets none passes dry and its page says "no memory". The default
  project (FX I delay, FX II reverb) holds about 54 blocks; the rest is the tracks'.
- **Where slots run (as now)**: a synth track's slots on its bus (slot 1 then slot 2, before the fader and the
  sends); a kit's on its bus (rumble hears the kicks); FX I / II on the second core a block late; the master's after
  its EQ, before the compressor.
- **Deleted**: `synth.c`'s character and insert implementations (`char_*` but the per-voice ring and tape wow,
  `ins_run`, `ins_pool`), `fx.c`'s send effects (`do_*`), `color_block`. Their duplicates gone should free several
  KB of flash; phase 1 must free more than it adds.

## Storage and conversion

A slot is nine values: effect, knobs 1-3, mix, extras 1-4.

- **Tracks** (in the sound: presets and user kits keep both slots; user kits lose the insert today): slot 1 reuses
  `TP_CTYPE, TP_CAMT, TP_CTONE, TP_CMIX` (effect, knob 1, knob 2, mix); slot 2 reuses `TP_ITYPE, TP_I1..I3`
  (effect, knobs 1-3). Appended: slot 1's knob 3 and extras, slot 2's mix (default 127: as inserts sound now) and
  extras. Locks and LFO routes on the reused fields keep their meaning.
- **FX I / II**: the same layout as a track's slot 1 (one page, one code path). Converted: delay time / repeats /
  tone to knobs, fine / width / wobble / smear to extras; reverb size / decay or motion / tone to knobs, its mode and
  damp (shimmer's amount) to extras; chorus rate / depth / feedback, width an extra; phaser freq / depth / rate,
  feedback an extra; lofi rate / bits / smooth, width an extra. The FX track's level becomes the slot's mix. HP, LP
  and FX I's feed into FX II stay the send's own (the SEL list).
- **Master**: master color becomes a full slot (five more master values).
- **Project format 28**: character types to their catalog numbers; insert types to theirs (chorus and phaser take
  the FX sound: never released); FX knobs moved as above, with the locks and motion on them; master color to the
  slot. **Presets format 17**: user presets converted the same way; the factory presets rebuilt from
  `tools/presets` (`char=` and `insert=` become `fx1=` / `fx2=`).
- **MIDI**: the master's CC 20-23 become effect, knob 1, knob 2, mix, a new CC for knob 3; the FX tracks' CC 16-19
  become knobs 1-3 and mix.
- **RAM**: about 10 more bytes per track in each copy of the parameters (a few hundred bytes); covered by the FX
  slots' own chorus lines and duplicated states going (checked in phase 1: SRAM is ~3.5 KB from full).

## Pages

- A track's slots are **Insert 1** and **Insert 2**; the sends stay **FX I** / **FX II**; master color becomes
  **Master FX**. FX opens Insert 1; SELECT: Insert 1, Insert 2, Sends, FX I, FX II. GLO: Master EQ, Master FX, Master
  out.
- **One layout for every slot page**: the effect's name; its picture (one drawing per effect, from today's character,
  insert and FX pictures); knobs 1-3 and mix (return on a send). **PRESETS** steps through the list in its groups
  (a new effect starts at its defaults); the **SEL** list has the whole catalog ("no memory" beside what does not
  fit), then the effect's extras, and on FX I / II the send's high-pass, low-pass and FX I's feed.
- The manual's character, insert and FX sections become one **Effects** chapter. The screen reader reads the effect,
  its knobs and "no memory"; the audit covers every effect.

## Phases (each tested, each shippable)

1. **The library**: `efx.c` with every effect ported. The released sounds (character, FX) bit-exact in
   `tests/host/dsp_hash.sh`; the inserts' chorus and phaser take the FX sound; master color's tape and sampler take
   the version the user picks by ear (A/B renders first: the character's, unless the master's wins). Flash and SRAM measured: must not
   grow.
2. **Track slots** (synth and kits) on the library; project format 28 and presets format 17 for the tracks.
3. **The sends and the one pool**; the FX tracks' conversion.
4. **The master slot**.
5. **The pages**, the manual, the screen reader.

## Testing

- `dsp_hash` before and after each phase: the released effects unchanged.
- Host tests per effect (bounded, no NaN, mix 0 dry, its knobs move what they say); the pool (allocation, "no
  memory", freeing on change); each conversion in `tests/scenarios/migrate.xy` from the fixtures (format 26 / 27
  stores, presets 16).
- The FM-1: CPU with a busy song after phase 2 (bus effects are not counted by voice admission or the core split
  today), flash and SRAM each phase.

## Open, not in this

- The LFO reaching slot knobs (the insert path for it exists but is unreachable: `LD_TP` never names `TP_I*`).
- Merging lofi and crush.
- The insert echo's own sound (gone: the delay replaces it).
