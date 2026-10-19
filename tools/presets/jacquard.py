# SPDX-License-Identifier: GPL-3.0-only
"""jacquard's (was wavetable's) factory presets (the bank as it was before the 2026-10-19 sound redesign; tools/presets/lib.py says how a
preset is written)."""
from lib import P

P("silk pad", "pad", "jacquard", knobs=dict(table=5, scan=65, bend=35, ripple=44), volume=89, amp=(90, 90, 110, 96),
  fenv=(0, 80, 64, 80), filter=("lp24", 88, 80, 64, 64), sends=(0, 20), lfo=("wave", 34, 120, "synth free", 1, 1),
  char=("tilt", 60, 110, 127))
P("indigo bass", "bass", "jacquard", knobs=dict(table=5, scan=36, bend=6, ripple=24), mode="mono", volume=116,
  amp=(48, 70, 95, 85), fenv=(0, 88, 30, 30), filter=("ladder", 72, 10, 64, 40), char=("fold", 75, 74, 127))
P("taffeta", "bass", "jacquard", knobs=dict(table=2, scan=47, bend=0, ripple=84), mode="mono", volume=94,
  amp=(10, 60, 120, 85), fenv=(0, 88, 60, 40), filter=("lp12", 56, 50, 88, 40), lfo=("wave", 4, 100, "synth", 1, 1),
  char=("drive", 0, 30, 127))
P("satin lead", "lead", "jacquard", knobs=dict(table=0, scan=10, bend=1, ripple=0), mode="legato", glide=70,
  volume=92, amp=(80, 80, 115, 64), fenv=(0, 64, 30, 60), filter=("lp12", 99, 20, 112, 64),
  lfo=("wobble", 81, 70, 64, 14, 0), char=("tilt", 45, 35, 127))
P("shantung", "lead", "jacquard", knobs=dict(table=8, scan=46, bend=0, ripple=0), mode="legato", glide=20,
  volume=111, amp=(56, 100, 60, 78), fenv=(0, 60, 0, 40), filter=("lp12", 115, 35, 64, 64), sends=(25, 0),
  lfo=("wave", 1, 30, "synth", 1, 3), char=("drive", 0, 127, 127))
P("lace keys", "keys", "jacquard", knobs=dict(table=0, scan=70, bend=17, ripple=0), volume=95, amp=(0, 82, 40, 84),
  fenv=(0, 66, 0, 50), filter=("lp12", 95, 5, 80, 64), sends=(0, 12), char=("tilt", 30, 20, 127))
P("crepe", "stab", "jacquard", knobs=dict(table=1, scan=36, bend=0, ripple=0), volume=107, amp=(0, 80, 0, 60),
  fenv=(0, 76, 0, 50), filter=("lp24", 66, 25, 96, 64), sends=(0, 15), char=("tilt", 30, 110, 127))
P("pongee", "pluck", "jacquard", knobs=dict(table=6, scan=90, bend=14, ripple=0), volume=127, amp=(0, 80, 0, 70),
  fenv=(0, 64, 60, 50), filter=("lp12", 108, 28, 112, 64), sends=(30, 0), lfo=("wave", 3, 120, "synth", 1, 3),
  char=("tilt", 30, 95, 127))
P("spool bell", "bell", "jacquard", knobs=dict(table=7, scan=90, bend=0, ripple=30), volume=122,
  amp=(0, 100, 20, 108), fenv=(0, 90, 0, 80), filter=("lp12", 116, 5, 64, 64), sends=(0, 50),
  char=("ring", 45, 58, 127))
P("gauze choir", "voice", "jacquard", knobs=dict(table=3, scan=23, bend=14, ripple=32), volume=127,
  amp=(70, 90, 100, 85), fenv=(0, 88, 0, 80), filter=("bp12", 70, 41, 100, 50), sends=(0, 55),
  lfo=("wave", 21, 113, "synth free", 1, 1), char=("tilt", 60, 80, 127))
P("georgette", "strings", "jacquard", knobs=dict(table=0, scan=45, bend=37, ripple=0), volume=71,
  amp=(84, 100, 100, 96), fenv=(0, 80, 64, 80), filter=("lp12", 66, 28, 64, 64), sends=(0, 40),
  lfo=("wave", 40, 84, "synth free", 2, 1), char=("tilt", 60, 35, 127))
P("faille", "brass", "jacquard", knobs=dict(table=5, scan=39, bend=0, ripple=0), volume=91, amp=(50, 76, 120, 66),
  fenv=(0, 56, 60, 66), filter=("lp24", 50, 28, 88, 64), sends=(0, 18), lfo=("wobble", 92, 74, 64, 20, 0),
  char=("drive", 0, 100, 127))
P("chiffon", "pad", "jacquard", knobs=dict(table=7, scan=65, bend=0, ripple=0), volume=127, amp=(92, 90, 100, 101),
  fenv=(0, 80, 64, 80), filter=("hp12", 78, 38, 64, 64), sends=(0, 65), lfo=("drift", 45, 50, "synth free", 1, 64),
  char=("tilt", 65, 117, 127))
P("tussah", "chord", "jacquard", knobs=dict(table=6, scan=0, bend=0, ripple=0), volume=52, amp=(50, 60, 127, 80),
  fenv=(0, 60, 0, 40), filter=("lp12", 97, 20, 64, 64), sends=(15, 0), char=("tilt", 90, 20, 127))
P("spiral", "fx", "jacquard", knobs=dict(table=4, scan=24, bend=58, ripple=20), volume=127, amp=(0, 110, 60, 100),
  fenv=(0, 60, 0, 40), filter=("lp24", 30, 20, 64, 64), sends=(20, 0), lfo=("wave", 50, 127, "synth free", 1, 1),
  char=("fold", 30, 64, 127))
P("noil", "fx", "jacquard", knobs=dict(table=2, scan=114, bend=103, ripple=127), volume=127, amp=(50, 100, 0, 50),
  fenv=(0, 60, 0, 40), filter=("bp12", 80, 50, 64, 64), sends=(30, 30), lfo=("wave", 80, 120, "synth free", 3, 1),
  char=("crush", 50, 74, 127))
