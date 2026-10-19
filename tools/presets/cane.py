# SPDX-License-Identifier: GPL-3.0-only
"""cane's presets (2026-10-19): Braids' physical models. knobs: model (0 pluck, 1 bow, 2 reed, 3 flute, 4 bell,
5 drum), timbre, color, damp; per model (dsp/braids.h): pluck timbre the decay, color the pluck's width, damp
softens it; bow timbre the pressure, color its place, damp the bridge's losses; reed timbre the breath's steadiness,
color the body; flute timbre clean breath, color how hard it is blown, damp darker; bell timbre how long it rings,
color the partials apart, damp shorter highs; drum timbre the decay, color tone to rattle. A plucked string is
bright: the filters keep it out of 2-5 kHz. At most four cane voices sound at once."""
from lib import P

PLUCK, BOW, REED, FLUTE, BELL, DRUM = range(6)

# its sound in a new project: a harp, the pluck softened, ringing into the room
P("cane", "pluck", "cane", knobs=dict(model=PLUCK, timbre=80, color=40, damp=80), volume=127,
  amp=(0, 100, 0, 80), fenv=(0, 64, 0, 64), filter=("lp12", 70, 0, 64, 64), sends=(0, 30),
  genres=("lofi", "synthpop"))
# a koto: short and bright, a narrow pluck
P("koto", "pluck", "cane", knobs=dict(model=PLUCK, timbre=70, color=30, damp=55), volume=127,
  amp=(0, 84, 0, 60), fenv=(0, 64, 0, 64), filter=("lp12", 70, 5, 64, 64), sends=(15, 20), genres=("lofi",))
# nylon: a soft, round string for chords
P("nylon", "keys", "cane", knobs=dict(model=PLUCK, timbre=70, color=64, damp=96), volume=124,
  amp=(0, 92, 0, 70), fenv=(0, 64, 0, 64), filter=("lp24", 78, 0, 64, 64), sends=(0, 18), genres=("lofi", "house"))
# a plucked bass: the string damped, the fingers' thump
P("finger bass", "bass", "cane", knobs=dict(model=PLUCK, timbre=90, color=50, damp=55), mode="mono", volume=127,
  amp=(0, 80, 0, 40), fenv=(0, 64, 0, 64), filter=("lp24", 72, 0, 64, 40), genres=("garage", "house"))
# a cello: the bow pressed, near the bridge's middle, a late vibrato
P("cello", "strings", "cane", knobs=dict(model=BOW, timbre=70, color=40, damp=60), mode="mono", volume=110,
  amp=(70, 80, 120, 80), fenv=(0, 64, 0, 64), filter=("lp12", 84, 0, 64, 64), sends=(0, 25),
  lfo=("wobble", 92, 74, 64, 20, 0), genres=("synthpop", "lofi"))
# a clarinet: a steady breath (under ~90 the reed barely speaks), a warm body
P("clarinet", "lead", "cane", knobs=dict(model=REED, timbre=100, color=80, damp=20), mode="legato", glide=8,
  volume=110, amp=(40, 70, 120, 60), fenv=(0, 64, 0, 64), filter=("lp12", 88, 0, 64, 64), sends=(10, 20),
  genres=("lofi",))
# a flute: clean, blown softly, a vibrato fading in
P("flute", "lead", "cane", knobs=dict(model=FLUTE, timbre=100, color=60, damp=40), mode="legato", glide=8,
  volume=110, amp=(50, 70, 120, 64), fenv=(0, 64, 0, 64), filter=("lp12", 94, 0, 64, 64), sends=(10, 25),
  lfo=("wobble", 92, 74, 64, 20, 0), genres=("lofi", "synthpop"))
# a temple bell: long, its partials a little apart, in the room
P("temple bell", "bell", "cane", knobs=dict(model=BELL, timbre=90, color=20, damp=60), volume=104,
  amp=(0, 104, 0, 96), fenv=(0, 64, 0, 64), filter=("lp12", 96, 0, 64, 64), sends=(0, 30), octave=1,
  genres=("lofi",))
# a hand drum: a short head, more tone than rattle
P("hand drum", "drum", "cane", knobs=dict(model=DRUM, timbre=50, color=30, damp=50), volume=116,
  amp=(0, 80, 0, 50), fenv=(0, 64, 0, 64), filter=("lp12", 100, 0, 64, 64), genres=("house", "lofi"))
