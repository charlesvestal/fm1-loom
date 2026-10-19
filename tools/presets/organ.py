# SPDX-License-Identifier: GPL-3.0-only
"""organ's factory presets (the bank as it was before the 2026-10-19 sound redesign; tools/presets/lib.py says how a
preset is written)."""
from lib import P

P("reed combo", "keys", "organ", knobs=dict(stops=2, pedal=3, rotor=4, rate=7), volume=84, amp=(0, 60, 100, 22),
  fenv=(0, 60, 0, 40), filter=("lp12", 120, 0, 64, 64), sends=(12, 12), char=("drive", 60, 117, 127))
P("pedal bass", "bass", "organ", knobs=dict(stops=1, pedal=8, rotor=3, rate=1), mode="mono", volume=127,
  amp=(0, 60, 100, 30), fenv=(0, 72, 60, 40), filter=("lp12", 58, 0, 112, 40), char=("tilt", 75, 110, 127))
P("bellows", "bass", "organ", knobs=dict(stops=3, pedal=8, rotor=1, rate=1), mode="mono", glide=30, volume=127,
  amp=(48, 70, 56, 85), fenv=(0, 60, 0, 40), filter=("lp12", 78, 0, 64, 40), char=("drive", 74, 100, 127))
P("parlour", "keys", "organ", knobs=dict(stops=3, pedal=4, rotor=5, rate=6), volume=104, amp=(20, 96, 60, 40),
  fenv=(0, 56, 30, 40), filter=("lp12", 90, 28, 76, 64), char=("tilt", 90, 35, 127))
P("drawbar", "keys", "organ", knobs=dict(stops=3, pedal=2, rotor=2, rate=3), volume=88, amp=(0, 102, 127, 84),
  fenv=(0, 56, 0, 40), filter=("lp24", 76, 20, 100, 64), sends=(0, 12), char=("tilt", 75, 110, 127))
P("sunday", "chord", "organ", knobs=dict(stops=4, pedal=6, rotor=7, rate=8), volume=90, amp=(50, 60, 100, 50),
  fenv=(0, 80, 60, 40), filter=("lp12", 120, 0, 100, 64), sends=(0, 12), char=("drive", 70, 95, 127))
P("musette", "keys", "organ", knobs=dict(stops=1, pedal=1, rotor=1, rate=1), volume=66, amp=(32, 102, 127, 56),
  fenv=(0, 60, 0, 40), filter=("lp12", 107, 0, 64, 64), sends=(12, 0), lfo=("wobble", 100, 76, 64, 64, 0),
  char=("tilt", 45, 110, 127))
P("harmonium", "chord", "organ", knobs=dict(stops=1, pedal=2, rotor=3, rate=3), volume=94, amp=(50, 60, 100, 56),
  fenv=(0, 60, 0, 40), filter=("lp12", 95, 12, 64, 64), char=("fold", 10, 84, 127))
P("carousel", "keys", "organ", knobs=dict(stops=6, pedal=4, rotor=8, rate=8), volume=88, amp=(0, 60, 100, 30),
  fenv=(0, 60, 0, 40), filter=("lp12", 93, 0, 64, 64), sends=(10, 12), char=("fold", 120, 90, 127))
P("calliope", "lead", "organ", knobs=dict(stops=7, pedal=1, rotor=2, rate=1), volume=80, amp=(56, 80, 127, 50),
  fenv=(0, 60, 0, 40), filter=("lp12", 65, 35, 64, 64), sends=(20, 10), lfo=("wobble", 96, 90, 64, 30, 0),
  char=("drive", 60, 110, 127))
P("loft flute", "lead", "organ", knobs=dict(stops=7, pedal=1, rotor=1, rate=1), mode="legato", glide=15, volume=105,
  amp=(64, 80, 127, 72), fenv=(0, 60, 0, 40), filter=("lp24", 62, 50, 64, 64), lfo=("wobble", 40, 72, 64, 64, 0),
  char=("crush", 10, 20, 127))
P("crypt", "stab", "organ", knobs=dict(stops=4, pedal=5, rotor=1, rate=2), volume=127, amp=(0, 72, 0, 55),
  fenv=(0, 64, 0, 40), filter=("lp24", 58, 5, 64, 64), char=("drive", 70, 100, 127))
P("chapel", "chord", "organ", knobs=dict(stops=7, pedal=8, rotor=1, rate=3), volume=70, amp=(50, 60, 127, 80),
  fenv=(0, 64, 0, 40), filter=("lp12", 92, 0, 88, 64), sends=(15, 0), char=("tilt", 30, 20, 127))
P("cathedral", "pad", "organ", knobs=dict(stops=8, pedal=5, rotor=1, rate=1), volume=107, amp=(100, 110, 100, 76),
  fenv=(0, 80, 30, 40), filter=("lp12", 112, 20, 112, 64), sends=(0, 80), char=("tilt", 60, 110, 127))
P("swell", "pad", "organ", knobs=dict(stops=8, pedal=3, rotor=1, rate=1), volume=89, amp=(70, 100, 127, 110),
  fenv=(0, 80, 64, 80), filter=("lp12", 58, 0, 64, 64), sends=(0, 45), char=("tilt", 90, 20, 127))
P("steam", "fx", "organ", knobs=dict(stops=6, pedal=1, rotor=7, rate=5), volume=127, amp=(80, 90, 0, 100),
  fenv=(0, 80, 60, 40), filter=("lp12", 126, 0, 100, 64), sends=(10, 0), char=("ring", 80, 30, 127))
