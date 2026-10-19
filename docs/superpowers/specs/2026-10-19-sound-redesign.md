# LOOM-8's sound, again (2026-10-19)

The complaint (Charles): the engines and presets all sound somewhat the same, and harsh. "Less engines, more
presets, that are good and varied." Part 1 is what we measured and heard; part 2 what was decided; part 3 the plan;
part 4 what is open. The A/B pages this came from: `build/ab/index.html`, `build/ab2/index.html` (host renders).

## 1. What we found

**The rasp is in the sources and the filters, not mostly the character stage.**
- 222 of 224 presets use the character stage (68 drive, 15 fold, 13 crush, 12 ring, 98 tilt), and 53 put more than
  half their energy in 2-6 kHz. Turning the character stage off changed little on most (A/B 1, column B).
- The raspiest presets were resonant filter peaks parked in 2-5 kHz (band-pass at 3.5 kHz on warp fifth; a resonant
  low-pass swept by an LFO on bowed linen) on top of FM with a **fixed index**: orbit's brightness never moves, so
  notes buzz statically. Ramie's raw orbit source has more 2-6 kHz than a plain saw (-7.5 dB vs -12.6).
- Fixed (A/B 2): an index that falls after the attack (with velocity, to a floor, lower for higher notes) and the
  peak moved out of 2-5 kHz took 2-6 kHz down 9-27 dB. Darkening alone (E2) made them **very bass heavy**: a
  band-pass turned low-pass let the lows through (ramie 40-150 Hz -28 -> -8 dB). Balanced (E3: a broad band-pass
  around 1.1-1.3 kHz for a stab or a chord, a gentle tilt for air) is what sounded right.
- Many presets are mono and dry (rattan bass, solder, lacquer: no sends at all). A 30-second tweak that also sent
  everything to the reverb (A/B 1, D) was **too much reverb**: not everything wants it.

**The CPU is spent on code size, not arithmetic.** The hardware profiler (`tools/hw_bench.py`, a build with
`XY_BENCH=1`) on the FM-1, per instance, % of one core at 44.1 kHz, code warm:

| block | cost | block | cost |
|---|---|---|---|
| saw / pulse / 2 saws / sine | 0.4 / 0.9 / 1.0 / 0.7 % | svf12 / svf24 / ladder | 0.44 / 0.8 / 0.6 % |
| wavetable read (flash or RAM, any size) | 0.4 % | soft3 / tanh / exp2 a sample | 0.3 / 0.66 / 0.73 % |
| int16 delay (write + fractional read) | 0.66 % | chorus | 1.7 % |
| **a subtractive voice** (2 saws, lp24, amp, outs) | **2.4 %** | **Dattorro plate**, full / half rate | **9.8 / 5.0 %** |

- The XIP cache is ~32 KB: the same work spread over more code is flat to ~20 KB, x1.02 at 30 KB, x1.37 at 39 KB
  (and real voice code, with less work a byte, suffers more). RAM code is no faster than warm flash.
- The audio path's code is ~87 KB a block: `eng_run` 23.3 KB (all engines), `voice_chunk` 20.1, `seq_advance` 14.8,
  `drumline_render` 9.1, `synth_render_on` 7.8, `engine_render` 5.2, the timer IRQ 4.6, `char_run` 2.6. That is why
  a voice costs 2.5 % alone and 8-12 % in a song (the 2026-10-18 flash-cache note).
- Host to FM-1: ~0.11-0.20 ticks a host instruction for float code, ~0.34 for int16 lines (host vectorising skews a
  few). Good to about 30 %; a new block goes into `app/dsp/bench.c` and is measured on the FM-1 in a minute.
- The current reverbs (host): the comb plate 337 instructions a sample, the SPU modes 318, lush 315.

## 2. Decided

- **Fewer engines, about 200 better presets**, varied by role and genre, not sixteen an engine. A role uses the
  engines that do it best (basses from several engines, not "the bass engine").
- **Leaning**: dusty lo-fi, 80s synth pop, modern drum and bass, house, UK garage. **Digital stays** (FM, wavetable,
  crush and chip sounds where they belong). Brocade need not be its own engine.
- **A good slap / funk bass** is a target of its own (velocity: thumb vs slap; a pop; dry, tight).
- **Reverb where the role wants it**: basses, leads, stabs dry (or a little delay); pads, strings, bells, keys
  moderate sends. **Lush** (below) is the reverb to use; the SPU modes stay for the dusty side.
- **Basses an octave down from leads**: a preset carries its octave.
- **Work on the host, measure on the FM-1 at milestones**: the profiler gives the blocks' costs; a design is costed
  from its blocks and its code size before it is flashed.
- Melodee is a reference for approach only (nothing taken): engines that behave like real instruments so known
  recipes translate; tuning against a reference recording; presets written as readable recipes. Per-note analog
  variance is optional.

## 3. The plan

### 3.0 Done (2026-10-19, branch `sound`)
- The hardware profiler: `app/dsp/bench.{c,h}` (kernels: oscillators, filters, curves, delay lines, a Dattorro plate,
  a voice, the code-size curve), `SB_BENCH` (27) in `app/samples.c`, `tests/host/hw_bench.{c,sh}`, `tools/hw_bench.py`;
  `XY_BENCH=1 FM1B_POOL_SPARE=0 ./build.sh` (tools/gen_build.py; bench.c is empty otherwise: the normal image is
  unchanged).
- **lush**: Dattorro's plate at 22.05 kHz as reverb mode `RV_LUSH` (the last mode: projects keep their numbers),
  knobs size / decay / tone / damp (default 64 80 56 48), its state in a union with the SPU's, `do_lush` in RAM
  (size_fns.py RAM_DSP). fx_test passes; the device build fits (image 455,132 B).
- **orbit's second page** (TP_X1..X3): fall (the index falls from the note's start, 20 ms .. 4 s; velocity sets the
  start), floor, keyscale (to -6 dB an octave above C3). 0 0 0 = as before (dsp_hash: no synth case changed). Not yet
  shown in the UI (only brocade's Sub page is).
- Tools: `render_presets.sh` no longer writes the removed `mst color`; `measure_load.sh` reads the Debug page
  (its new-project step is still stale).

### 3.1 The effects and the character stage
- lush the default reverb on FX II; its gain checked against the plate's on the demo songs.
- The character stage's types: **warm** first (tape: soft saturation, HF roll-off, a little wow; sampler: 12-bit at
  26 kHz with its filter, the SP-1200 / S950 colour; drive as now), fold / ring / crush kept for FX and digital
  presets. Cost each in `bench.c` first.
- Maybe a third FX slot or a voice-level stereo spread (the chorus is a send: a pad cannot have chorus and reverb).
- Heard (2026-10-19, build/ab3, ab4): tape sounds good; sampler is very characterful: **no factory preset uses
  the sampler** (it stays for the user), and both are on the master too (GLO, Master color: done).

### 3.2 The preset format and its checks
- **Octave** in the sound part (default by role: a bass -2, else 0; done. At -1 most basses had nothing under 100 Hz
  on the FM-1's keys, F3 .. G5: "thin", Charles). PRESETS_FORMAT / PROJ_FORMAT
  bump; old saves do not load (no migrations before release).
- **Presets as recipes**: a readable file (tools/presets/*.py, one entry a preset: role, genres, engine, the knobs by
  name) generated into C, so they are reviewed, diffed and measured on the host.
- Done (2026-10-19): the recipes (tools/presets/, tools/gen_presets.py; the old bank converted byte for byte) and
  tools/preset_check.py (rasp, boom, level of the sounding frames, peak, twins, no sampler) on the role renders. The
  old bank: 149 of 224 pass (39 rasp, 21 boom, 25 level). It joins run_tests strictly once the new bank is in.
- **Checks in the tests** (on the role's phrase, dry and through the chain), per role: 2-5 kHz under a limit unless
  the role is FX; low end under a ceiling for anything not a bass; a minimum of air for pads, bells, keys; a
  resonant peak in 2-5 kHz only if it moves; distance to every other preset above a floor; peak and loudness in
  range. (These would have caught both the rasp and E2's boom.)

### 3.3 The engines
Charles defers the engine choice. Settled: the **digital voice is wavetable** (gingham is too chiptune: cut), and
**bring-your-own wavetables** are wanted (a table uploaded into the user sample bank, as samples are: 216 KB of flash,
and a flash table reads as fast as RAM, `bench.c` "wt big"). Proposal for the rest: **analog** (new: two oscillators, sub, noise, sync, unison = brocade's saws, PWM, the filters,
drift), **fm** (orbit + tines: 2-op with index envelopes and keyscale, the tine; e-pianos, bells, DX basses, the
slap), **wavetable** (tables smoothed), **organ**, **cane** (plucks, strings, reeds, mallets), **sample**, **slice**,
the kit. Cut: weft, stack, sync, swarm, gingham, brocade (into analog); voile by ear.
- Done (2026-10-19): **knit** (new: wave, detune, voices 1-7, width; osc 2, sub, noise), **rayon** (orbit + tines:
  the tine, velocity, 16 ratios, fall / floor / keyscale), wavetable renamed **jacquard**, organ **bellows**; weft,
  stack, sync, swarm, brocade, gingham and tines cut (EG_* renumbered: knit 0 .. slice 7, the kit 8). Image 470 ->
  440 KB; eng_run 23.3 -> 15.1 KB (cane and voile's Braids code most of it), voice_chunk 20.1 -> 18.7 KB, the hot
  path ~87 -> ~79 KB. Left: voile by ear; voice_chunk; the demos and the manual on the new engines.
- **Code budget**: `eng_run` (all engines) <= ~10 KB, `voice_chunk` slimmed; the hot path toward the ~32 KB cache.
  Measured with llvm-nm on `app/build/loom.elf` and the profiler's code curve.
- **Every FM path** gets an index envelope and keyscale (orbit has them now; tines has a fixed decay).

### 3.4 The presets
- ~200, by role x genre: e.g. ~50 lo-fi, ~55 synth pop, ~45 drum and bass, ~50 house and garage; roles bass (sub,
  reese, acid, slap / funk, organ bass, FM bass), lead, keys (e-piano, M1 piano, organ, clav), pad, strings, brass,
  pluck, bell, stab, chord, voice, FX, drums, loops.
- Rules: no resonant peak parked in 2-5 kHz; FM brightness follows an envelope and falls up the keyboard; movement
  from envelopes and velocity, not only LFOs; reverb by role; balanced lows (a broad band-pass, a tilt) rather than
  darker filters.
- Listened in batches by role, in context (a beat, chords), against reference records; the checks first, the ears
  last.
- The slap bass: prototyped on fm (1:1, the index falling in ~70 ms: `build/ab2/slap_fm-E3.wav`) and on cane; pick
  by ear.

### 3.5 The FM-1 checkpoints
After 3.1 (lush and the character types), after 3.3 (analog in, the cuts: the code budget measured), before the
presets are final: `tools/hw_bench.py` and the demo songs' load each time; the host's factors refitted if the FM-1
disagrees.

## 4. Open

1. Which engines stay: voile (choirs, vocal pads: by ear), cane as is or reworked; wavetable's tables (smoothed, new
   ones) and the BYO format (a table's size and frame count, the upload tool's).
2. analog's shape: one engine for everything subtractive (supersaw included) or the supersaw apart.
3. Per-note analog variance: optional; cost it if wanted.
4. The air above 6 kHz on FM with a falling index (the E3s have little): a higher floor, or noise / breath.
5. A third FX slot (RAM: ~6 KB plus its lines) or a voice-level ensemble.
