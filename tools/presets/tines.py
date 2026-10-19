# SPDX-License-Identifier: GPL-3.0-only
"""tines's factory presets (the bank as it was before the 2026-10-19 sound redesign; tools/presets/lib.py says how a
preset is written)."""
from lib import P

P("felt piano", "keys", "tines", knobs=dict(bright=53, grit=0, bite=40, bell=55), volume=103, amp=(0, 92, 55, 66),
  fenv=(0, 70, 0, 50), filter=("lp12", 116, 5, 64, 64), lfo=("wobble", 84, 64, 82, 64, 0),
  char=("drive", 5, 100, 127))
P("soft felt", "keys", "tines", knobs=dict(bright=10, grit=0, bite=0, bell=10), volume=82, amp=(20, 95, 50, 72),
  fenv=(0, 88, 0, 50), filter=("lp12", 76, 0, 88, 64), char=("tilt", 45, 35, 127))
P("felt bass", "bass", "tines", knobs=dict(bright=62, grit=40, bite=90, bell=0), mode="mono", volume=112,
  amp=(10, 82, 90, 35), fenv=(0, 66, 0, 30), filter=("lp12", 80, 10, 64, 40), char=("drive", 35, 20, 127))
P("moleskin", "bass", "tines", knobs=dict(bright=22, grit=0, bite=34, bell=58), mode="mono", volume=127,
  amp=(48, 80, 0, 50), fenv=(0, 88, 30, 40), filter=("lp12", 79, 0, 88, 40), char=("drive", 0, 70, 127))
P("corduroy", "keys", "tines", knobs=dict(bright=127, grit=83, bite=39, bell=34), volume=127, amp=(0, 86, 0, 40),
  fenv=(0, 56, 60, 50), filter=("lp12", 120, 10, 100, 64), sends=(0, 10), char=("drive", 80, 110, 127))
P("loden", "keys", "tines", knobs=dict(bright=43, grit=63, bite=19, bell=0), volume=127, amp=(10, 96, 0, 40),
  fenv=(0, 64, 0, 40), filter=("lp12", 84, 12, 64, 64), sends=(12, 0), lfo=("wobble", 88, 64, 20, 64, 3),
  char=("drive", 0, 100, 127))
P("baize", "pluck", "tines", knobs=dict(bright=90, grit=110, bite=100, bell=0), volume=127, amp=(0, 90, 0, 40),
  fenv=(0, 60, 0, 40), filter=("bp12", 78, 40, 90, 64), char=("tilt", 90, 110, 127))
P("chamois", "stab", "tines", knobs=dict(bright=100, grit=2, bite=83, bell=78), volume=102, amp=(0, 90, 20, 70),
  fenv=(0, 60, 0, 40), filter=("bp12", 74, 12, 64, 64), sends=(18, 10), char=("tilt", 45, 80, 127))
P("hammer", "keys", "tines", knobs=dict(bright=127, grit=114, bite=127, bell=90), volume=101, amp=(20, 84, 20, 40),
  fenv=(0, 88, 30, 40), filter=("lp12", 127, 5, 112, 64), sends=(0, 10), char=("drive", 15, 127, 127))
P("velvet lead", "lead", "tines", knobs=dict(bright=42, grit=2, bite=115, bell=121), volume=127,
  amp=(40, 106, 37, 96), fenv=(0, 56, 60, 50), filter=("bp12", 96, 10, 76, 64), sends=(30, 10),
  char=("fold", 70, 70, 127))
P("lullaby", "bell", "tines", knobs=dict(bright=44, grit=0, bite=0, bell=127), volume=127, amp=(0, 112, 0, 86),
  fenv=(0, 70, 0, 50), filter=("bp12", 96, 28, 64, 64), sends=(0, 45), char=("ring", 55, 84, 127))
P("melton", "bell", "tines", knobs=dict(bright=0, grit=0, bite=0, bell=70), volume=106, amp=(0, 92, 20, 108),
  fenv=(0, 80, 30, 40), filter=("lp12", 112, 12, 100, 64), sends=(0, 10), lfo=("wobble", 84, 64, 30, 64, 0),
  char=("tilt", 30, 95, 127))
P("plush", "pluck", "tines", knobs=dict(bright=70, grit=0, bite=127, bell=90), volume=105, amp=(0, 84, 10, 85),
  fenv=(0, 60, 0, 40), filter=("hp12", 66, 20, 64, 64), sends=(15, 0))
P("flannel pad", "pad", "tines", knobs=dict(bright=15, grit=22, bite=80, bell=32), volume=81, amp=(0, 100, 127, 115),
  fenv=(0, 56, 60, 80), filter=("lp12", 63, 5, 100, 64), sends=(10, 20), lfo=("wobble", 80, 66, 127, 64, 0),
  char=("drive", 40, 90, 127))
P("flock", "chord", "tines", knobs=dict(bright=40, grit=0, bite=30, bell=20), volume=88, amp=(30, 90, 70, 80),
  fenv=(0, 88, 0, 40), filter=("lp12", 50, 28, 64, 64), sends=(15, 0), lfo=("wobble", 30, 76, 64, 64, 0),
  char=("crush", 60, 50, 127))
P("duffel", "fx", "tines", knobs=dict(bright=16, grit=0, bite=0, bell=127), volume=96, amp=(0, 110, 0, 50),
  fenv=(0, 56, 0, 40), filter=("ladder", 96, 0, 100, 64), sends=(20, 50), char=("ring", 127, 127, 127))
