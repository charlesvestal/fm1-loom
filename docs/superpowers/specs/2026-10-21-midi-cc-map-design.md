# C3: a MIDI CC map for the mixer, the FX and the sounds (2026-10-21)

From the testers' feedback (docs/feedback-2026-10-21.md, C3): levels, sends and the master FX over MIDI CC. Agreed
with the user: the mixer, the FX and the master, plus the standard sound CCs; fixed channels; in and out; recorded
into automation.

## Channels: fixed

| Channel | What |
|---|---|
| 1-8 | tracks T1-T8 |
| 9 / 10 | FX I / FX II |
| 11 | the master, and the scenes (Program Change, notes 36-67) |
| 12-16 | the selected instrument track (`inst_trk()`) |

Notes, pitch bend and CCs all follow this table (notes on 9 and 10 are ignored; on 11 they pick scenes) (`engine.c midi_track` becomes a fixed lookup). A track's notes go
out on its own channel; a kit's GM notes too.

The per-track MIDI CHANNEL row (`TP_CHAN`) becomes **MIDI OUT: on / off**. The byte keeps its place: 0 off, any
other value on (new tracks 1). Old projects need no migration and no format bump: 1..16 all read as on. The SEL list
row, the words file and the manual change with it.

## The map

A CC's 0..127 is scaled onto the parameter's range (`tp_info_par` lo..hi; switches step); out, the reverse.

### Tracks 1-8 (channel = track)

| CC | Synth track | Kit track |
|---|---|---|
| 7 | level (`TP_LEVEL`) | level |
| 10 | pan (`TP_PAN`) | pan |
| 91 / 93 | send I / II (`TP_SEND1` / `TP_SEND2`) | every sound's send I / II moved by the same offset (below) |
| 9 | mute, >= 64 muted | mute |
| 14 / 15 | the mixer's low cut / high cut (C9: `TP_LOCUT` / `TP_HICUT`) | same |
| 74 | cutoff (`TP_CUT`) | the bus's hi cut (`TP_KTRK`) |
| 71 | resonance (`TP_RES`) | the bus's low cut (`TP_CUT`) |
| 73 | attack (`TP_A`) | kit attack (`TP_D`) |
| 72 | release (`TP_R`) | kit length (`TP_R`) |
| 75 | decay (`TP_D`) | - |
| 5 | glide (`TP_PORTA`) | - |
| 1 | LFO amount (`TP_LAMT`) | - |
| 16-26 | - | mute sound 1-11 (>= 64 muted; the first four kit tracks, `NKITM`) |
| 80-90 | - | pan of sound 1-11 (`DP_PAN`) |
| 102-112 | - | level of sound 1-11 (`DP_VOLUME`) |

A kit's sends: a kit has no track sends, so 91 / 93 on a kit move all its sounds' `DP_SEND1` / `DP_SEND2` by the
change since the last value received (64 assumed before the first), clamped; the per-sound balance holds away from
the ends. The last values: two bytes a track. Never sent out (no single knob holds them).

Mutes (9, 16-26) do exactly what the Mix page does (`set_mute`, `kit_sound_mute`): the mute now, saved into the
scene playing. A CC only acts when it changes the state (>= 64 on a muted track does nothing).

Numbers left alone on purpose: 32-63 (a DAW's 14-bit CC 7 also sends 39), 64-69 (pedals), 120-127 (channel mode).

### FX I / II (channels 9 / 10) and the master (channel 11)

| CC | FX I / II | Master |
|---|---|---|
| 7 | return level (`TP_LEVEL`) | level (`MS_LEVEL`) |
| 16-19 | the effect's four knobs (`TP_E1..E4`) | EQ low / mid / high (19 unused) |
| 20-23 | - | color type / amount / tone / mix |
| 24-27 | - | drums / melodic / comp / pump |
| 28 | - | pump release (`MS_PUMP_REL`) |
| 74 / 75 | low-pass (`TP_CUT`) / high-pass (`TP_FENV`) | - |
| 93 | feed into FX II (`TP_SEND2`; channel 9 only) | - |

The effect's choice (`TP_ENGINE`) stays on the FM-1.

## In

`midi_in` runs in the audio interrupt. It looks the CC up in the map (a const table: channel kind, CC, parameter or
action) and puts (track, parameter or action, value) into a ring of 16 four-byte entries (64 B RAM), replacing
`eng_cc_level`. A full ring drops the CC. The main loop drains the ring where it drains `eng_cc_level` now
(ui.c), applying each through `set_tp`, `master_set`'s path (`proj.master` + `engine_master`), `set_mute` or
`kit_sound_mute`, as the knob or key would (undo, dirty, the screen).

Recording: with REC on and the sequencer playing, a CC on a track parameter records into **its own track's**
automation (`seq_rec_knob(&seq, trk, p, v)`), whichever track is selected. Mutes and the master are not recorded
(the knobs aren't either).

## Out

A new row in GLO's SEL list (Setup's group), **CC OUT**, **on by default** (as notes out is; the user's call). With
it on, a mapped parameter **turned by hand** on the FM-1 sends its CC on its channel, when the track's MIDI OUT is on
(FX and master: always). Not sent: locks, automation playing, the shared LFO, a preset loaded, a CC that came in (no
loops with a DAW). Stored as an off bit, `proj.rsv[2]` bit 16 (`CC_OUT_OFF` beside `AUTOSAVE_OFF` in pages.c): new
and old projects alike start with it on; no format change.

## Scenes

- In, on channel 11 (the master's): **Program Change** 0..31 picks scene 1..32, and **notes 36..67** (C1 up, where
  pads start) pick scenes 1..32 too, for pad controllers without Program Change (note off ignored). Either goes the
  white key's way (ui.c `scene_go`): an unused scene is made from the one playing; stopped, at once; playing, cued by
  SCENES SWITCH (next bar / pattern end). Other programs and notes on 11 are ignored.
- Out, with **SCENES OUT** on (its own row in GLO's list, after CC OUT; **off by default**, the user's call: a
  Program Change another device follows can switch its sounds, more intrusive than a CC; stored as an on bit,
  `proj.rsv[2]` bit 32, `SCENES_OUT`, so old projects have it off too): when the scene playing changes (ui.c's frame
  sees `cur_scene()` change), Program Change 0..31 on channel 11. Not for a change that came in over MIDI (the scene asked for is remembered and skipped once):
  no loop with a DAW that echoes.

## Cost

Flash: the table (~60 entries, 3 bytes each) and the code, ~1 KB. RAM: the ring 64 B + kits' last sends 16 B, less
`eng_cc_level` 8 B. No pool, no audio CPU beyond the lookup.

## Tests

- Host test (tests/host): feed packets into `engine_midi_in`, drain, check each mapped parameter on a synth track,
  a kit track, FX I / II and the master; scaling of a switch; channels 12-16 reach the selected track; a mute CC
  matches the Mix page's (the scene too); the kit send offset holds the balance.
- Recording: REC on, playing, a CC on track 3 while track 1 is selected writes track 3's lane.
- Scenes: Program Change 2 and note 37 on channel 11 cue scene 3 / scene 2 by SCENES SWITCH; with SCENES OUT on a
  scene changed on the FM-1 sends its Program Change (off: nothing); one asked for over MIDI does not echo.
- Out: CC OUT on (a new project's default), a knob turned sends the CC; an incoming CC, a lock or automation sends nothing; MIDI OUT off on
  a track sends nothing for it.
- Old projects: a track with `TP_CHAN` 5 reads as MIDI OUT on and sends on its own channel.

## Docs

The manual: the channel table and the map (both tables), scenes over MIDI, CC OUT in GLO's list, MIDI OUT in the SEL list. words.tsv for
the new row names; the release notes (MIDI section).
