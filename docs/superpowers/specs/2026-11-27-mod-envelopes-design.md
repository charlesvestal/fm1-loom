# Two mod envelopes (2026-11-27)

The user: "two more envelopes for loom, each freely assignable to the parameters of a track (voice, synth, fx, etc).
Useful for things like wavetable scans, or other interesting design things." Agreed in the session: one set on
each voice (every note its own), one target each, on the ENV button, with LOOP.

## What it is

- A synth track has two more envelopes, **Mod env 1** and **Mod env 2**, after Filter env on the ENV button
  (ENV: Envelope, Filter, Filter env, Mod env 1, Mod env 2; acid: its two, then the mod envelopes). A kit and a
  MIDI track have none.
- Each runs on every voice from its note, as the amp and filter envelopes do: a new note starts it from zero; a
  mono retrigger from where it is (retrig: from zero); legato carries on; a key let go starts its release.
- Its knobs: **ATTACK, DECAY, SUSTAIN, AMOUNT** (amount 64 = 0; at the ends the target's whole range, pitch +-12
  semitones, as the LFO). Its SEL list rows: **TARGET**, **RELEASE**, **LOOP** (attack and decay again while the key
  is held, from the level it reached; the release as always). The page draws its shape and "to TARGET, AMOUNT, loop".
- **TARGET** (synth.c ME_TARGET): off, pitch, the engine's four knobs, its second page (TP_X1..X4), knit's osc 2
  fine and shape, the Mod page's bytes (knit's sweep and cross, tartan's morph, tulle's grain), rayon's Ops depth and
  feedback, cutoff, resonance, filter env, keytrack, pan, low cut. Only what the track's engine has and is
  continuous (menv_target_ok: not a switch or a list); ALGORITHM skips the rest. A target the engine lacks (after the
  engine changed) shows "-" and moves nothing. Not targets: the volume (on a bus it is the bus's), the slots, the
  sends, the fader (one for all the track's notes).
- The envelope adds to the knob (and the step's lock), as the LFO does, clamped to the knob's range; both envelopes
  can move one knob; the LFO's offset adds on top.
- Times: the amp envelope's curve (env_seconds), stepped once a 64-sample chunk (1.3 ms).

## How it runs

- synth_prepare (me_prepare, once a block a track, core 0): each envelope's target and its coefficients a chunk.
- voice_chunk: a voice whose track has an envelope on calls me_chunk, which steps them and writes the targets'
  values into a copy of the track's locks (as a lock would set them, rounded to the knob's steps); voice_chunk then
  reads through that copy (its P, P2, pf), so every read stays as it was. A voice without one reads as before:
  dsp_hash's 1524 cases unchanged with the envelopes off. Pitch adds in cents.

## Storage

- 14 parameters after TP_S2X4: TP_ME(e, f), f = target, attack, decay, sustain, release, amount, loop. Lockable
  (after TP_O2FINE: P2), so steps can lock them; the MOD track can move them.
- Defaults: target off, attack 0, decay 64, sustain 0, release 40, amount 64, loop off (menv_defaults).
- Project format 35 (28..34's tracks spread to the longer par[], the envelopes off).
- Presets format 18: the presets object (16 user presets and 8 kits, 3,836 of its 3,840 bytes before) is packed with
  lz.c, as the projects: [18][the unpacked length][packed user presets, then kits]. Unpacked in the canvas past
  lz_head on the FM-1 (ps_raw). Older formats load as before and save as 18. user_preset_t has me[14].
- Factory presets: lib.P(menv=((target, a, d, s, r, amount, loop), ...)); PS_X carries them.

## Room

- Flash (597,948 B slot; 724 B free before): a quarter-wave sine in digisophie.c (1.5 KB, the same values), more
  per-step sequencer code and engine.c's main-loop code built for size, the 909's tanh table as 16 bits (the user's
  choice: 0.7 KB net; the 909 cases' energy within 0.07 %), and the factory presets' masks packed (gen_presets.py:
  bytes 0..2, a head byte, the non-zero rest; ~720 B, the table decodes the same). With the seven new presets
  (jacquard scan pluck, morph pad, loop scan; knit zap bass, sync loop; rayon fm blip, loop bell): 597,608 B,
  340 B free.
- RAM: synth_t +864 B (the voices' envelopes, the tracks' coefficients and their inputs, the longer lock arrays);
  2,752 B free.

## On the FM-1 (2026-11-28, 360 MHz, two cores)

Chords held over MIDI into a scratch project: T2 sync loop (mono), T3 loop bell (4 notes), T4 morph pad (4 notes),
9-11 voices; sound checked in the same run (USB audio, MASTER 0: peaks 0.2-0.4). Steady readings (tools/perf_read.py):

| | load mean | worst block | chunk | prep |
| --- | --- | --- | --- | --- |
| envelopes on (first build) | 62.0 % | 68-70 % | 13.8 | 5.3 |
| envelopes off (targets off) | 58.5 % | 65-66 % | 8.3 | 4.3 |
| envelopes on (me_prepare cached) | 61.4 % | 68-69 % | 12.5 | 4.5 |

The cost is the engines' own: a moving scan, index or pitch makes the voice's engine prepare again every chunk (as
an LFO on it does). me_prepare now remakes a track's coefficients only when its values or engine change. No guard
trip, dropout or steal; no crash. The first ~9 s after a preset load read higher in every engine alike (the project's
autosave writing the flash, likely).
