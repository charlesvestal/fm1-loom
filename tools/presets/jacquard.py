# SPDX-License-Identifier: GPL-3.0-only
"""jacquard's presets (2026-10-19): the digital voice, a frame of a table scanned. knobs: table (0 shapes, 1 nasal,
2 grit, 3 vocal, 4 spiral, 5 fold, 6 octaves, 7 glass, 8 chirp), scan (the frame), bend (the phase bent), ripple
(the phase modulated by a second oscillator, slow wobble up to the note's rate). The tables are bright (40
harmonics): the filters keep them out of 2-5 kHz. An LFO on the scan: lfo=("wave", speed, amount, "synth", 1, shape)
(speed 0..7 synced 2/1 .. 1/64, 3 a quarter; shape 0 square, 1 sine, 2 ramp, 3 saw)."""
from lib import P

SHAPES, NASAL, GRIT, VOCAL, SPIRAL, FOLD, OCTAVES, GLASS, CHIRP = range(9)

# its sound in a new project: between triangle and saw, a soft filter
P("jacquard", "keys", "jacquard", knobs=dict(table=SHAPES, scan=40, bend=0, ripple=0), volume=104,
  amp=(0, 80, 100, 60), fenv=(0, 72, 20, 60), filter=("lp24", 80, 5, 80, 64), genres=("synthpop",))
# a vowel pad: the vocal table's vowels morphing slowly, wide in the room
P("vowel pad", "pad", "jacquard", knobs=dict(table=VOCAL, scan=40, bend=0, ripple=10), volume=100,
  amp=(80, 90, 120, 96), fenv=(0, 64, 0, 64), filter=("lp12", 86, 0, 64, 64), sends=(0, 40),
  lfo=("wave", 22, 90, "synth free", 1, 1), genres=("synthpop", "garage"))
# glass keys: the glass table's first primes, plucked
P("glass keys", "keys", "jacquard", knobs=dict(table=GLASS, scan=50, bend=0, ripple=0), volume=106,
  amp=(0, 84, 30, 64), fenv=(0, 66, 10, 60), filter=("lp12", 92, 0, 84, 64), sends=(10, 25), genres=("lofi",))
# the growl: the fold table scanned in eighths, the ladder under it, a little drive (drum and bass)
P("growl", "bass", "jacquard", knobs=dict(table=FOLD, scan=30, bend=20, ripple=0), mode="mono", volume=100,
  amp=(0, 70, 120, 30), fenv=(0, 60, 40, 40), filter=("ladder", 74, 30, 84, 40),
  lfo=("wave", 4, 100, "synth", 1, 1), char=("drive", 50, 70, 127), genres=("dnb",))
# the wobble: the shapes table swept sine to square in quarters, the filter following
P("wobble", "bass", "jacquard", knobs=dict(table=SHAPES, scan=20, bend=0, ripple=0), mode="mono", volume=104,
  amp=(0, 70, 127, 30), fenv=(0, 64, 0, 40), filter=("lp24", 70, 20, 64, 40), lfo=("wave", 3, 110, "synth", 1, 1),
  genres=("dnb",))
# an octave pad: the octaves table near its sine end, slow, tape
P("octave pad", "pad", "jacquard", knobs=dict(table=OCTAVES, scan=45, bend=0, ripple=0), volume=100,
  amp=(90, 90, 120, 100), fenv=(0, 64, 0, 64), filter=("lp12", 82, 0, 64, 64), sends=(0, 45),
  lfo=("wave", 18, 76, "filter free", 0, 1), char=("tape", 40, 60, 127), genres=("lofi",))
# a spiral pad: the Fibonacci partials coming and going on a slow drift
P("spiral pad", "pad", "jacquard", knobs=dict(table=SPIRAL, scan=50, bend=10, ripple=20), volume=96,
  amp=(96, 96, 120, 104), fenv=(0, 64, 0, 64), filter=("lp12", 80, 0, 64, 64), sends=(15, 50),
  lfo=("drift", 30, 70, "synth free", 1, 64), genres=("lofi", "synthpop"))
# a chip lead: the grit table's steps, legato, a vibrato coming in
P("chip lead", "lead", "jacquard", knobs=dict(table=GRIT, scan=30, bend=0, ripple=0), mode="legato", glide=12,
  volume=92, amp=(0, 70, 110, 40), fenv=(0, 64, 0, 64), filter=("lp12", 92, 0, 64, 64), sends=(20, 10),
  lfo=("wobble", 92, 74, 64, 20, 0), genres=("lofi",))
# a shimmer bell: the glass table's high frames, rippling, ringing in the room
P("shimmer", "bell", "jacquard", knobs=dict(table=GLASS, scan=100, bend=0, ripple=20), volume=100,
  amp=(0, 100, 0, 90), fenv=(0, 64, 0, 64), filter=("lp12", 96, 0, 64, 64), sends=(0, 45), genres=("synthpop",))
# a nasal lead: the climbing resonance, mid-scan, a vibrato
P("nasal lead", "lead", "jacquard", knobs=dict(table=NASAL, scan=50, bend=0, ripple=0), mode="mono", volume=127,
  amp=(10, 70, 115, 50), fenv=(0, 64, 0, 64), filter=("lp24", 86, 10, 64, 64), sends=(20, 15),
  lfo=("wobble", 92, 74, 64, 20, 0), genres=("synthpop",))
# a digital pluck: the grit table's steps, rippled, the filter's snap, a delay throw
P("digi pluck", "pluck", "jacquard", knobs=dict(table=GRIT, scan=70, bend=30, ripple=40), volume=108,
  amp=(0, 66, 0, 52), fenv=(0, 54, 0, 50), filter=("lp12", 76, 10, 100, 64), sends=(35, 20),
  genres=("house", "garage"))
