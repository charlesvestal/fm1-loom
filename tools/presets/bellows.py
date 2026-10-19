# SPDX-License-Identifier: GPL-3.0-only
"""bellows' presets (2026-10-19): the organ, six drawbars as rotating sines. knobs, each 1..8: stops (1 transistor,
2 combo, 3 jazz, 4 gospel, 5 rock, 6 theatre, 7 pipe, 8 church), pedal (the 16' bar: 1 none), rotor (its depth:
1 none), rate (0.5 .. 8 Hz). The amp envelope makes the percussive ones (an organ bass, a stab)."""
from lib import P

TRANSISTOR, COMBO, JAZZ, GOSPEL, ROCK, THEATRE, PIPE, CHURCH = range(1, 9)

# its sound in a new project: jazz drawbars, a slow rotor
P("bellows", "keys", "bellows", knobs=dict(stops=JAZZ, pedal=1, rotor=4, rate=2), volume=100,
  amp=(0, 64, 127, 40), fenv=(0, 64, 0, 64), filter=("lp12", 110, 0, 64, 64), sends=(0, 15),
  genres=("house", "lofi"))
# the house organ bass (the M1's percussive organ): struck, decaying to a third, an octave down
P("organ bass", "bass", "bellows", knobs=dict(stops=ROCK, pedal=4, rotor=1, rate=1), mode="mono", volume=110,
  amp=(0, 78, 40, 30), fenv=(0, 64, 0, 64), filter=("lp24", 90, 0, 64, 40), genres=("house", "garage"))
# gospel: every bar out, the rotor fast, a little tape
P("gospel", "chord", "bellows", knobs=dict(stops=GOSPEL, pedal=3, rotor=6, rate=7), volume=92,
  amp=(0, 64, 127, 46), fenv=(0, 64, 0, 64), filter=("lp12", 100, 0, 64, 64), sends=(0, 20),
  char=("tape", 30, 80, 127), genres=("house", "garage"))
# the garage organ stab: combo bars, short, a delay throw
P("organ stab", "stab", "bellows", knobs=dict(stops=COMBO, pedal=1, rotor=1, rate=1), volume=104,
  amp=(0, 66, 0, 50), fenv=(0, 64, 0, 64), filter=("lp12", 96, 0, 64, 64), sends=(35, 20), genres=("garage", "house"))
# church: the full chorus and the pedal, swelling in, a big room
P("church", "pad", "bellows", knobs=dict(stops=CHURCH, pedal=6, rotor=1, rate=1), volume=92,
  amp=(70, 80, 127, 96), fenv=(0, 64, 0, 64), filter=("lp12", 96, 0, 64, 64), sends=(0, 55), genres=("synthpop",))
# a flute stop: the pipe's flutes, a breath of attack, in the room
P("flute stop", "lead", "bellows", knobs=dict(stops=PIPE, pedal=1, rotor=2, rate=3), mode="legato", glide=8,
  volume=104, amp=(40, 64, 120, 60), fenv=(0, 64, 0, 64), filter=("lp12", 100, 0, 64, 64), sends=(10, 30),
  genres=("lofi",))
# the transistor combo: reedy and hollow, a fast shimmer (the eighties' cheap organ)
P("combo", "keys", "bellows", knobs=dict(stops=TRANSISTOR, pedal=1, rotor=3, rate=6), volume=100,
  amp=(0, 64, 120, 40), fenv=(0, 64, 0, 64), filter=("lp12", 96, 0, 64, 64), sends=(10, 15), genres=("synthpop",))
# a dusty harmonium: theatre bars through worn tape, a slow wheeze
P("harmonium", "chord", "bellows", knobs=dict(stops=THEATRE, pedal=2, rotor=2, rate=2), volume=96,
  amp=(30, 70, 120, 64), fenv=(0, 64, 0, 64), filter=("lp12", 88, 0, 64, 64), sends=(0, 25),
  char=("tape", 70, 50, 127), genres=("lofi",))
# a slow rotor pad: jazz bars, the rotor deep and slow, swelling
P("rotor pad", "pad", "bellows", knobs=dict(stops=JAZZ, pedal=2, rotor=8, rate=1), volume=96,
  amp=(80, 80, 127, 96), fenv=(0, 64, 0, 64), filter=("lp12", 94, 0, 64, 64), sends=(0, 40), genres=("lofi", "house"))
