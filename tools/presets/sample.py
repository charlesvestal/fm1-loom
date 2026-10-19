# SPDX-License-Identifier: GPL-3.0-only
"""sample's presets, the first set (2026-10-19): the recorded instruments (VSCO 2 CE: upright piano, contrabass
pizzicato, a string section, a brass section (trumpet over trombone), a flute, an orchestra hit), the vinyl and the made drums.
knobs: sample (by name), start, length, mode (0 one-shot, 1 loop, 2 reverse, 3 ping-pong); a sustained recording
loops its own sustain in mode 1. Dust comes from tape, never the sampler (the rules: tools/presets/knit.py)."""
from lib import P, smp

# its sound in a new project: the upright, its sustain looped under a slow decay, a little tape, a little room
P("piano", "keys", "sample", knobs=dict(sample=smp("piano"), start=0, length=127, mode=1), volume=110,
  amp=(0, 100, 30, 70), filter=("lp12", 96, 0, 64, 64), sends=(0, 15), char=("tape", 25, 70, 127),
  genres=("lofi", "house"))
# the lo-fi piano: darker, the tape wobbling, the hammers softened
P("dusty keys", "keys", "sample", knobs=dict(sample=smp("piano"), start=2, length=127, mode=1), volume=120,
  amp=(2, 96, 20, 72), filter=("lp12", 76, 0, 70, 64), sends=(0, 22), char=("tape", 70, 40, 127),
  genres=("lofi",))
# the house piano: short and bright, the delay on the off-beats
P("piano stab", "stab", "sample", knobs=dict(sample=smp("piano"), start=0, length=127, mode=0), volume=112,
  amp=(0, 80, 0, 56), filter=("hp12", 40, 0, 64, 64), sends=(30, 12), genres=("house", "garage"))
# the piano backwards: a swell into the next bar
P("rev piano", "fx", "sample", knobs=dict(sample=smp("piano"), start=0, length=127, mode=2), volume=127,
  amp=(10, 80, 127, 60), filter=("lp12", 104, 0, 64, 64), sends=(20, 30), char=("tape", 40, 60, 127),
  genres=("lofi",))
# the upright bass, plucked: round, the finger in front
P("upright", "bass", "sample", knobs=dict(sample=smp("upright"), start=0, length=127, mode=0), mode="mono",
  volume=118, amp=(0, 90, 40, 50), filter=("lp24", 80, 0, 70, 64), char=("tape", 30, 50, 127),
  genres=("lofi",))
# the same, short, the filter plucked, deep in the tape: the jazz-hop bass under a dusty loop
P("dusty bass", "bass", "sample", knobs=dict(sample=smp("upright"), start=0, length=127, mode=0), mode="mono",
  volume=127, amp=(0, 74, 0, 50), fenv=(0, 70, 0, 50), filter=("lp24", 60, 20, 92, 40), char=("tape", 95, 30, 127),
  genres=("lofi", "garage"))
# the string section, bowed: a slow bow, the lush room
P("strings", "strings", "sample", knobs=dict(sample=smp("strings"), start=0, length=127, mode=1), volume=105,
  amp=(70, 80, 120, 90), filter=("lp12", 92, 0, 64, 64), sends=(0, 40), genres=("synthpop", "house"))
# the garage string stab: the section's attack, cut short
P("str stab", "stab", "sample", knobs=dict(sample=smp("strings"), start=4, length=127, mode=0), volume=112,
  amp=(0, 76, 0, 54), filter=("lp12", 100, 0, 64, 64), sends=(25, 15), genres=("garage", "house"))
# the strings backwards, swelling: a DnB intro
P("swell", "pad", "sample", knobs=dict(sample=smp("strings"), start=0, length=127, mode=2), volume=110,
  amp=(80, 80, 127, 90), filter=("lp24", 78, 5, 64, 64), sends=(0, 45), lfo=("drift", 25, 60, "filter free", 0, 64),
  genres=("dnb",))
# the brass section, held: chords that swell a little, the room behind
P("brass", "brass", "sample", knobs=dict(sample=smp("brass"), start=0, length=127, mode=1), volume=104,
  amp=(30, 80, 115, 64), filter=("lp12", 92, 0, 64, 64), sends=(10, 25), char=("tape", 25, 60, 127),
  genres=("lofi", "house", "synthpop"))
# the horn stab: the section's attack alone, for the chords
P("horn stab", "stab", "sample", knobs=dict(sample=smp("brass"), start=0, length=127, mode=0), volume=114,
  amp=(0, 74, 0, 50), filter=("lp12", 104, 0, 64, 64), sends=(20, 10), genres=("house", "garage"))
# the flute, breathy, its own vibrato
P("flute", "lead", "sample", knobs=dict(sample=smp("flute"), start=0, length=127, mode=1), mode="mono", glide=6,
  volume=108, amp=(20, 80, 120, 70), filter=("lp12", 100, 0, 64, 64), sends=(0, 30), char=("tape", 40, 60, 127),
  genres=("lofi", "synthpop"))
# the flute an octave down, slow: an airy pad for the lo-fi chords
P("air flute", "pad", "sample", knobs=dict(sample=smp("flute"), start=0, length=127, mode=1), volume=104,
  amp=(80, 80, 120, 90), filter=("lp12", 82, 0, 64, 64), sends=(0, 40), char=("tape", 50, 50, 127), octave=-1,
  genres=("lofi",))
# the orchestra hit: the eighties' stab, strings, horn and timpani at once
P("orch hit", "stab", "sample", knobs=dict(sample=smp("orch hit"), start=0, length=127, mode=0), volume=114,
  amp=(0, 80, 0, 60), filter=("lp12", 110, 0, 64, 64), sends=(10, 15), genres=("synthpop", "house"))
# the hit an octave down, darker: a DnB stab
P("dark hit", "stab", "sample", knobs=dict(sample=smp("orch hit"), start=0, length=127, mode=0), volume=127,
  amp=(0, 84, 0, 60), filter=("lp24", 86, 10, 70, 64), sends=(15, 20), octave=-1, genres=("dnb",))
# the run-out groove: hold a key under a track (its pitch, its colour)
P("vinyl", "fx", "sample", knobs=dict(sample=smp("vinyl"), start=0, length=127, mode=1), volume=127,
  amp=(0, 64, 127, 40), filter=("hp12", 30, 0, 64, 0), genres=("lofi",))
# the noise sweep: the riser
P("riser", "fx", "sample", knobs=dict(sample=smp("sweep"), start=0, length=127, mode=0), volume=127,
  amp=(40, 90, 100, 90), filter=("lp12", 127, 0, 64, 64), sends=(20, 30), genres=("house", "dnb"))
# the made kick, low and long: a boom under a break
P("boom", "drum", "sample", knobs=dict(sample=smp("kick"), start=0, length=127, mode=0), volume=124,
  amp=(0, 90, 0, 60), filter=("lp24", 90, 0, 64, 64), char=("tape", 40, 40, 127), octave=-1,
  genres=("dnb", "garage"))
