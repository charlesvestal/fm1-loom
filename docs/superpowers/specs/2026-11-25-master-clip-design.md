# Master CLIP: a soft clipper before the limiter (2026-11-25)

Part A of the techno work (`docs/superpowers/2026-11-25-techno-handoff.md`, "A master that can go loud"). Club
techno is mastered around -8 to -6 LUFS; the techno demo renders at -17 LUFS with the limiter idle. The master has
EQ, color, comp, pump, level and X0X's limiter, but nothing that trades peaks for loudness.

## What

A **CLIP** knob on the master's first page, in the empty slot after low / mid / high (`MS_RSV3`, shown as "-"
today). 0 = off; up = louder and denser: the kick's and the snare's peaks rounded so the rest comes up.

## Where in the chain

EQ -> color -> comp -> pump -> level -> **clip** -> limiter -> soft ceiling. After the compressor (in front of it
the comp would pull the level back down), before the limiter, which then catches only what the clip leaves.

## The sound

- Drive 0 .. +12 dB (the knob 0..127, in dB: 0 at 0, 12 dB at 127), into a symmetric soft knee: below the knee
  the signal passes as is, above it rounded towards a ceiling at the limiter's threshold (0.8). Odd harmonics only;
  no filter, no wow, no level matching (on the master, louder is the point; the limiter keeps the output safe).
- CLIP 0 is the current master bit for bit (the stage skipped).
- Cost: a gain, a compare and a curve per sample a side; no memory beyond the knob.

## Changes

- `app/dsp/fx.h` / `fx.c`: `MS_RSV3` becomes `MS_CLIP`; `MS_NAME` "clip"; master_set computes the drive gain; the
  stage in master_process between `lev` and the limiter.
- `app/project.c`: the default stays 0 (old projects load with CLIP off: no format change; slot already saved).
- `app/pages.c`, `app/ui.c`: stop skipping the slot (pages.c ~1883, ui.c ~2238); the knob shown and turned as the
  others. Screen reader words (`words.tsv`) if the name is new to it.
- `app/host.c`: `MN[3]` "clip", so demo scripts can set it (`master clip N` / `knob ... clip`).
- MANUAL: one line under the master's page.

## Testing

- Host test: CLIP 0 renders the same bytes as before the change (a fixed song through master_process).
- Host test: CLIP 127 on a full-scale sine never exceeds the soft ceiling and has no DC.
- Measured: the techno demo rendered at CLIP 0 / 40 / 80 / 127: integrated loudness (LUFS) and crest factor
  (peak to RMS), aiming at about -8 LUFS without audible pumping; renders handed to the user for headphones.
- The FM-1: CPU of the master with CLIP on (the perf read), after the host passes.

## Not in this

The demo's rebuild (part B), the drum bus, delays, modulation scenes.
