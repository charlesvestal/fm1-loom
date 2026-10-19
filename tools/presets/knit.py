# SPDX-License-Identifier: GPL-3.0-only
"""knit's presets, the first set (2026-10-19): the everyday subtractive voice. wave 0 saw, 64 square, 127 a narrow
pulse; detune (one voice: a drift); voices 1..7; width. x = (osc 2 interval, 64 unison; its level; sub; noise).
The rules (docs/superpowers/specs/2026-10-19-sound-redesign.md): no resonant peak parked in 2-5 kHz unless it moves,
the lows balanced, reverb only where the role wants the room, a bass an octave down."""
from lib import P

# its sound in a new project: one warm saw, a little drift, a soft filter that opens on the note
P("knit", "keys", "knit", knobs=dict(wave=0, detune=20, voices=1, width=0), volume=100,
  amp=(0, 80, 100, 60), fenv=(0, 70, 30, 60), filter=("lp24", 76, 10, 84, 64), genres=("synthpop",))
# the PWM string machine: a square whose width the LFO moves, two voices wide, tape on top
P("ribbon", "strings", "knit", knobs=dict(wave=64, detune=25, voices=2, width=80), volume=84,
  amp=(75, 80, 120, 90), fenv=(0, 64, 0, 64), filter=("lp12", 84, 5, 64, 64), sends=(0, 35),
  lfo=("wave", 40, 76, "synth free", 0, 1), char=("tape", 40, 60, 127), genres=("synthpop", "lofi"))
# the poly brass: the filter's swell, a vibrato coming in late
P("brass swell", "brass", "knit", knobs=dict(wave=0, detune=30, voices=2, width=40), volume=76,
  amp=(55, 80, 110, 64), fenv=(70, 85, 60, 70), filter=("lp24", 64, 20, 96, 64), sends=(0, 20),
  lfo=("wobble", 92, 74, 64, 20, 0), genres=("synthpop",))
# the Reese: three saws beating, the detune itself wandering, a sub under it
P("reese", "bass", "knit", knobs=dict(wave=0, detune=40, voices=3, width=30), mode="mono", volume=84,
  amp=(0, 64, 127, 40), fenv=(0, 64, 0, 40), filter=("lp24", 70, 10, 64, 40), lfo=("wave", 30, 80, "synth free", 1, 1),
  x=(64, 0, 50, 0), genres=("dnb",))
# a round sub: the filter nearly closed on a saw, the sine sub under it
P("round sub", "bass", "knit", knobs=dict(wave=0, detune=10, voices=1, width=0), mode="mono", volume=110,
  amp=(2, 80, 110, 40), fenv=(0, 60, 0, 40), filter=("lp24", 48, 5, 70, 30), x=(64, 0, 70, 0),
  genres=("dnb", "garage", "house"))
# the house chord stab: three saws, a quick filter blip, a throw of delay
P("house stab", "stab", "knit", knobs=dict(wave=0, detune=30, voices=3, width=70), volume=92,
  amp=(0, 80, 30, 55), fenv=(0, 60, 0, 50), filter=("lp12", 78, 10, 84, 64), sends=(25, 20),
  genres=("house", "garage"))
# acid: the ladder high in resonance, swept by the envelope (it moves: the rule's exception), glide, drive after
P("acid", "bass", "knit", knobs=dict(wave=0, detune=0, voices=1, width=0), mode="legato", glide=30, volume=120,
  amp=(0, 90, 100, 30), fenv=(0, 70, 0, 40), filter=("ladder", 52, 95, 110, 50), char=("drive", 40, 90, 127),
  genres=("house",))
# a pluck: half saw, half square, the filter's snap
P("knit pluck", "pluck", "knit", knobs=dict(wave=30, detune=15, voices=1, width=0), volume=112,
  amp=(0, 72, 0, 60), fenv=(0, 60, 0, 50), filter=("lp24", 60, 15, 105, 64), sends=(30, 25),
  genres=("synthpop", "garage"))
# a soft lead: a square with its octave above, legato, a vibrato that fades in
P("soft lead", "lead", "knit", knobs=dict(wave=64, detune=15, voices=1, width=0), mode="legato", glide=20,
  volume=96, amp=(30, 70, 115, 60), fenv=(0, 64, 0, 64), filter=("lp12", 90, 5, 64, 64), sends=(20, 15),
  lfo=("wobble", 92, 74, 64, 20, 0), x=(76, 40, 0, 0), genres=("lofi", "synthpop"))
# a dusty pad: four saws wide, the filter breathing, tape
P("dusty pad", "pad", "knit", knobs=dict(wave=0, detune=35, voices=4, width=100), volume=96,
  amp=(85, 90, 120, 96), fenv=(0, 64, 0, 64), filter=("lp24", 70, 0, 64, 64), sends=(0, 45),
  lfo=("wave", 20, 72, "filter free", 0, 1), char=("tape", 60, 50, 127), genres=("lofi",))
