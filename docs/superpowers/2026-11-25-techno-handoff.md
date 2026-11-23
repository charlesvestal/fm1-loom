# Handoff, 2026-11-25: making LOOM-8 sound like real techno

Branch `room` (worktree `fm1-xy-room`), unreleased. Target: **Berlin and Detroit techno** (Berghain / Ostgut Ton,
Rødhåd, Ben Klock; Jeff Mills, Robert Hood). The user's verdict on the techno demo (`tools/demos/techno.xy`): it
"still isn't sounding like real techno". Discuss each feature with the user before building it (memory:
discuss-features-first); no release until the user says so.

## Where things stand

- Rumble insert done (synth.c `IT_RUMBLE`): the kick's reverb as a low instrument, after the producers' chain (sub
  kept out of the reverb at 120 Hz, a dense network at 1.38 kHz with no modulation, 24 dB low-pass at tone, light
  saturation, ducked on each hit). On a kit it hears only the kicks (`drumline_render_kick`). The user's reactions
  on the way: hard-clipped = "gritty", modulated lines + an offbeat delay = "out of tune, stretched", too loud =
  "too rumbly", the kick's sub in the reverb = "too harmonic". Sources: Myloops' full chain
  (myloops.net/creating-techno-kick-rumbles), Underdog's "techno rumble mastery" (youtube oUbACkekJZ8), Studio
  Brootle (cXASNE4pzgk), Attack Magazine's Beat Dissected (dark techno rumble, dark Berlin techno, Phase Fatale).
- Low cut on every synth (Play 2's KNOB 3, `TP_HPF`, the Juno-106's HPF). Dance presets (juno pad/brass/bass/str,
  hoover, deep stab, dub chord, rumble, rat stab, hypnotic, noise burst, neuro, dirty reese, liquid pad, rave stab,
  deep bass, sub glide, house organ): 169 presets.
- Project format 26 and presets format 16 are unreleased (released 0.18.2 is format 22): new parameters can go into
  them without a migration while this branch stays unreleased.
- Room: the Bluetooth build 592,820 of 597,948 B (~5 KB free); SRAM ~5 KB free. The FX delay holds 0.5 s (fx.h
  `FX_DLY_LEN`, 22.05 kHz; longer times are halved).
- Demo render: `app/build/host/loom_host tools/demos/techno.xy build/demos/techno` (no bank needed). On the FM-1:
  the setup up to `tap HOME` + `projsave 1 techno` + `backup out:techno.loom8`, then `fm1_backup.py restore` (SEL
  on the FM-1; it replaces the projects). The user doesn't want backups made first.

## Why the demo doesn't sound like techno (the diagnosis the user agreed with)

1. **Loudness and density.** Club techno is mastered around -8 to -6 LUFS, the drums glued by bus saturation and
   compression, then limited or clipped. The demo: -17 LUFS, the limiter never working. The master has EQ, comp,
   pump and a limiter, but no drive or clip stage.
2. **The kick and drums too quiet and clean.** The kick should be the loudest, most processed thing. No ride (the
   909 ride is close to essential); hats at flat velocities. Techno programming lives in velocity and decay
   variation, ghost notes, and 3- or 5-step percussion cycles against the four on the floor.
3. **No evolution.** Techno is one loop changing slowly over minutes (filter sweeps over 16 to 64 bars, a send
   pushed for a bar, a hat opening). Motion in LOOM-8 is recorded per pattern, so a long sweep has nowhere to live.
4. **Pop sound choices** (a liquid pad, a noise burst). More typical: an acid line (the 303), short metallic FM
   hits, a dub chord with lots of delay, filtered noise; fewer pitched parts, more texture.

## The new work (the user's asks)

1. **Longer, evolving delay repeats** (dub techno, Basic Channel-style chords; Berlin's long echoes). Today: 0.5 s at
   half rate. Ideas to weigh with the user: more memory (or a lower rate / 8-bit companded line, as the insert echo);
   the repeats changing as they go (darker, saturated, drifting, filtered by a moving band); a long feedback that
   holds without running away; delay times past a beat (dotted quarter, half).
2. **Modulation scenes**: sweeps longer than a pattern. A filter (or any knob) moving over 8 to 64 bars, tied to a
   scene or the song, so a loop can evolve without the player turning knobs. Look first at what exists: per-pattern
   motion (REC held while turning), locks, the THROW aux track, scenes and the song (MANUAL "Arrange, scenes and the
   song", "The aux tracks"). Questions: per scene or per song section? one lane or several? drawn shapes (rise,
   fall, rise-fall) or recorded? what a scene change does to a sweep in progress?
   Prior art: PR #5 "LOOM-8: Song Automation" (charlesvestal/fm1-loom#5, by KrisGelero; closed unmerged
   2026-11-09; local branch `pr5-song-automation`, worktree `fm1-xy-pr5`, one commit 2043e20 on an old main, project
   format 15). It records a macro's curve per song slot (`song_auto_t slot_map[NSONG]`): each slot's curve tagged
   with the macro it was recorded with, REC arms and PLAY records from the picked slot after the count-in, writing
   from the first knob move, an "Auto" view beside "Song" (ALGO toggles), black key 11 deletes a slot's curve,
   format-14 stores migrated. Read it before designing (memory: check-open-prs-first; build on a contributor's work
   where it fits, and credit them); no other PR touches automation or delays.
3. **A master that can go loud**: a drive or clip stage before the limiter.
4. **Drum-bus glue**: check whether the kit's character (drive) and its dynamic filter's compression already give
   it; the demo may simply not use them.
5. **A 909 ride** in the kit (we have the 909 hats via 9W9's sampler).
6. **Rebuild the demo** as a 3 to 4 minute Berlin/Detroit loop: loud kit bus, ride, velocity-shaped hats, a 3-step
   percussion cycle, an acid line, the dub chord's echo as the main texture, slow filter and send changes. Check it
   against measured targets (loudness, kick-to-rumble balance, energy under 100 Hz against the rest), not by ear
   alone, and render it for the user to hear on headphones (the FM-1's speaker can't play the low end).

## Also open

- Juno-106 importer: the user brings 106 SysEx banks (Roland's factory banks are online); converted on their side,
  no Roland data committed or published (as Polly's BYO-ROM bank). Needs calibrated curves (cutoff, envelope times,
  LFO rate, PWM depth) from published measurements, not GPL emulator code. The low cut maps the 106's HPF slider.
- Shared effects code: the insert phaser and chorus to use the FX tracks' code (the user agreed it makes sense; the
  FX versions are released, so the inserts take their sound).
- One list for character and inserts: discuss first (pages and songs change).
- Before any release: CPU of the inserts on the FM-1 with a busy song (none measured yet); site and manual
  screenshots; the Insert page's rumble picture still shows the first design.
