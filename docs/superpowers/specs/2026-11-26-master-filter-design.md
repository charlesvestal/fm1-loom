# Master filter: a DJ filter on the drums and on the whole mix (2026-11-26)

The user, 2026-11-26: a master DJ filter, and one on the drums — "we'll want both for automation" (the MOD track,
`2026-11-26-mod-track-design.md`, built after this so it can target them).

## What

A GLO page **Master filter**, between Master FX and Master out:

| Knob | |
|---|---|
| 1 drums | one knob on the drums group (every kit track): left a low-pass, the centre off, right a high-pass |
| 2 drums res | its resonance |
| 3 mix | the same on the whole mix |
| 4 mix res | its resonance |

## Where

- **drums**: on the percussion group (master_process's perc_l/r) before the groups meet.
- **mix**: late in the master, after the compressor and pump, before CLIP and the limiter (a sweep not fought by
  the compressor; CLIP and the limiter keep the output safe).
- A 12 dB state-variable filter each (stereo), cutoff on a log scale from the centre (left: 20 kHz down to ~60 Hz;
  right: ~20 Hz up to ~8 kHz; a small dead zone round 64 = off, bit-exact: the stage skipped), resonance 0..~0.9
  of self-oscillation, gain-compensated so a resonant sweep doesn't jump in level.
- Slewed like the effects (4833524): a hand, MIDI or a MOD lane sweeps without zipper; from off the filter fades in.

## Storage

Four master values (MS_*: NMS 21 -> 25), project format next (older: 64 / 0: off). MIDI: four free CCs on the
master's channel 11 (beside 20-23, 29), in docs/MANUAL.md's MIDI table. Host script names (`mst dfilt ...`).

## Cost

No memory; ~1-1.5 KB flash (after the flash pass); a few hundred instructions a block a filter when on.

## Testing

Host: centre = bit-exact (dsp_hash: all the same); a sweep's response (LP at 25 %: -12 dB an octave above the cutoff;
HP likewise); resonance bounded (a full-scale noise burst never past the limiter's ceiling, no NaN); the slew test's
check over the new knobs; a project from before loads with both off.

## Addendum (2026-11-26, the user): sends after the filters

Heard on the FM-1: the drums filter turned down, the drums' reverb and delay stayed full. A kit's sends are taken
per key (drumline, the keys' own send amounts) before its bus: the kit filter (dynamic), its Insert 1 / 2, and the
drums filter (on the master's drums group) never reach FX I / II. Synth tracks are right already (voice -> filter
-> Insert 1 -> Insert 2 -> fader -> sends). The decision:
- **Per-key send amounts stay.**
- **The filters apply to the sends too**: each kit's two send signals go through the kit filter and the drums
  filter as its pair does (a second, mono state each: linear, cheap). The drums filter moves from the master's
  drums group into each kit's render (its pair and its sends, one setting, up to four kits), so turning it down
  takes the drums' reverb and delay down with them (the tails already in FX I / II ring out).
- **Inserts stay before the sends on a kit** (an insert's memory and CPU can't be run twice): a kit with drive in
  Insert 1 sends its clean hits. Said in the manual.
- The mix filter is already after the returns (the whole mix): unchanged.

## Addendum 2 (2026-11-26, the user): three filters, one resonance

The page becomes: KNOB 1 **drums**, KNOB 2 **melodic**, KNOB 3 **mix**, KNOB 4 **resonance** (shared by the three).
- **drums**: each kit's pair and its two sends (addendum 1).
- **melodic**: the synth tracks' sum and their share of the two sends, before the kits add theirs and before the FX
  returns join the melodic group (a filter there would catch the drums' reverb too). Sends after the filter, as the
  drums'.
- **mix**: after the returns, on everything (as built in 7e79153).
- The four master values restructured in place (formats 31.. are unreleased: no migration from 31 needed; from
  older, all off): MS_DFILT drums, MS_MELF melodic (was MS_DRES), MS_MFILT mix, MS_FRES resonance (was MS_MRES).
  CCs: 30 drums, 31 melodic, 74 mix, 71 resonance.
