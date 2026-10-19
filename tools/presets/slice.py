# SPDX-License-Identifier: GPL-3.0-only
"""slice's presets, the first set (2026-10-19): the four recorded breaks, played at the song's tempo (classic 98 BPM,
heat 96 and ecstasy 121 a bar each; choke two bars at 138: labelled 69, half-time). knobs: sample (by name), slices
(0: 4, 1: 8, 2: 16, 3: 32), pitch (64: as recorded, +-24 st), gate. A loop keeps its kick (the boom check leaves loops and drums be);
dust comes from tape (the rules: tools/presets/knit.py)."""
from lib import P, smp

# its sound in a new project: the classic break in eight, as it was played
P("breaks", "loop", "slice", knobs=dict(sample=smp("classic"), slices=1, pitch=64, gate=127), volume=118,
  filter=("lp12", 127, 0, 64, 0), char=("tape", 20, 80, 127), genres=("lofi", "house", "dnb"))
# heat: tight and bright, for house and garage
P("heat", "loop", "slice", knobs=dict(sample=smp("heat"), slices=1, pitch=64, gate=127), volume=118,
  filter=("lp12", 127, 0, 64, 0), genres=("house", "garage"))
# choke: slow and heavy, the lo-fi loop, deep in the tape
P("choke", "loop", "slice", knobs=dict(sample=smp("choke"), slices=1, pitch=64, gate=127), volume=120,
  filter=("lp12", 96, 0, 64, 0), char=("tape", 60, 50, 127), genres=("lofi",))
# ecstasy: four to the floor's cousin, open hats
P("ecstasy", "loop", "slice", knobs=dict(sample=smp("ecstasy"), slices=1, pitch=64, gate=127), volume=118,
  filter=("lp12", 127, 0, 64, 0), genres=("house",))
# the jungle chop: classic in sixteenths, a little driven, each slice cutting the last
P("jungle", "loop", "slice", knobs=dict(sample=smp("classic"), slices=2, pitch=64, gate=116), mode="mono", volume=124,
  filter=("lp12", 127, 0, 64, 0), char=("drive", 40, 80, 127), genres=("dnb",))
# the roller: choke in sixteenths, tight gates, each cutting the last
P("roller", "loop", "slice", knobs=dict(sample=smp("choke"), slices=2, pitch=64, gate=96), mode="mono", volume=120,
  filter=("lp12", 116, 0, 64, 0), char=("drive", 25, 70, 127), genres=("dnb",))
# two-step: choke in sixteenths, short, a touch of delay
P("two step", "loop", "slice", knobs=dict(sample=smp("choke"), slices=2, pitch=64, gate=88), mode="mono", volume=120,
  filter=("lp12", 120, 0, 64, 0), sends=(15, 0), genres=("garage",))
# the dusty loop: ecstasy three semitones down, dull, wobbling tape
P("dusty loop", "loop", "slice", knobs=dict(sample=smp("ecstasy"), slices=1, pitch=61, gate=127), volume=127,
  filter=("lp12", 90, 0, 64, 0), char=("tape", 80, 40, 127), genres=("lofi",))
# the half-time break: classic an octave down, dark, for the slow half of a DnB tune
P("half time", "loop", "slice", knobs=dict(sample=smp("classic"), slices=1, pitch=52, gate=127), volume=124,
  filter=("lp24", 90, 5, 64, 0), char=("tape", 40, 60, 127), genres=("dnb", "lofi"))
# the hits: classic's eighths one at a time, short, to play as a kit
P("hits", "drum", "slice", knobs=dict(sample=smp("classic"), slices=1, pitch=64, gate=48), volume=124,
  filter=("lp12", 127, 0, 64, 0), genres=("lofi", "dnb"))
# ghost notes: classic's sixteenths, soft and short, under another beat
P("ghosts", "drum", "slice", knobs=dict(sample=smp("classic"), slices=2, pitch=64, gate=40), volume=124,
  filter=("lp12", 100, 0, 64, 0), genres=("garage", "house"))
