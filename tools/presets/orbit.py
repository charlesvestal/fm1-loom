# SPDX-License-Identifier: GPL-3.0-only
"""orbit's factory presets (the bank as it was before the 2026-10-19 sound redesign; tools/presets/lib.py says how a
preset is written)."""
from lib import P

P("bowed linen", "strings", "orbit", knobs=dict(bright=73, ratio=30, edge=53, wobble=90), volume=110,
  amp=(100, 90, 120, 80), fenv=(0, 80, 0, 80), filter=("lp12", 95, 28, 64, 64), sends=(0, 50),
  lfo=("wave", 34, 75, "filter free", 0, 1), char=("drive", 20, 90, 127))
P("cord bass", "bass", "orbit", knobs=dict(bright=77, ratio=66, edge=47, wobble=0), mode="mono", volume=127,
  amp=(48, 100, 0, 50), fenv=(0, 70, 10, 40), filter=("lp24", 100, 20, 100, 40), char=("drive", 50, 80, 127))
P("buckram", "bass", "orbit", knobs=dict(bright=114, ratio=104, edge=71, wobble=0), mode="mono", volume=127,
  amp=(0, 80, 60, 70), fenv=(0, 60, 0, 40), filter=("ladder", 72, 30, 64, 30), lfo=("wave", 4, 30, "synth", 0, 1),
  char=("drive", 70, 70, 127))
P("needle", "lead", "orbit", knobs=dict(bright=98, ratio=102, edge=64, wobble=7), mode="legato", glide=20, volume=90,
  amp=(2, 90, 90, 45), fenv=(0, 60, 30, 50), filter=("lp12", 120, 5, 70, 64), sends=(0, 8),
  lfo=("wobble", 92, 74, 64, 30, 0), char=("drive", 25, 127, 127))
P("lawn flute", "lead", "orbit", knobs=dict(bright=20, ratio=0, edge=0, wobble=30), mode="legato", glide=20,
  volume=74, amp=(56, 90, 127, 64), fenv=(0, 60, 0, 40), filter=("lp12", 58, 50, 64, 64), sends=(20, 10),
  lfo=("wobble", 92, 74, 64, 10, 0), char=("tilt", 30, 110, 127))
P("cambric", "brass", "orbit", knobs=dict(bright=64, ratio=0, edge=32, wobble=31), volume=121, amp=(52, 76, 80, 64),
  fenv=(56, 84, 70, 60), filter=("ladder", 47, 5, 104, 64), lfo=("wobble", 92, 74, 64, 20, 0),
  char=("tilt", 75, 80, 127))
P("hollow", "keys", "orbit", knobs=dict(bright=0, ratio=104, edge=38, wobble=0), volume=127, amp=(20, 84, 0, 56),
  fenv=(0, 88, 0, 40), filter=("hp12", 36, 0, 64, 64), char=("drive", 0, 70, 127))
P("holland", "keys", "orbit", knobs=dict(bright=55, ratio=125, edge=0, wobble=0), volume=82, amp=(20, 96, 0, 70),
  fenv=(0, 60, 0, 50), filter=("lp12", 115, 0, 64, 64), sends=(0, 12), char=("tilt", 30, 95, 127))
P("thimble", "bell", "orbit", knobs=dict(bright=67, ratio=114, edge=0, wobble=0), volume=117, amp=(0, 100, 0, 108),
  fenv=(0, 60, 0, 40), filter=("bp12", 90, 28, 64, 64), sends=(0, 40), char=("tilt", 90, 110, 127))
P("chime", "bell", "orbit", knobs=dict(bright=102, ratio=81, edge=0, wobble=0), volume=96, amp=(0, 100, 0, 108),
  fenv=(0, 96, 0, 90), filter=("lp12", 127, 0, 64, 64), sends=(0, 55), char=("ring", 0, 104, 127))
P("toile", "pluck", "orbit", knobs=dict(bright=38, ratio=119, edge=0, wobble=0), volume=116, amp=(0, 60, 30, 70),
  fenv=(0, 88, 0, 40), filter=("lp12", 96, 0, 88, 64), char=("tilt", 30, 80, 127))
P("ramie", "stab", "orbit", knobs=dict(bright=107, ratio=88, edge=2, wobble=0), volume=107, amp=(0, 100, 0, 60),
  fenv=(0, 64, 0, 50), filter=("bp12", 68, 15, 76, 64), sends=(0, 15), char=("drive", 30, 127, 127))
P("warp fifth", "chord", "orbit", knobs=dict(bright=70, ratio=92, edge=10, wobble=40), volume=107,
  amp=(50, 90, 127, 80), fenv=(0, 72, 30, 70), filter=("bp12", 90, 20, 88, 64), sends=(15, 0),
  lfo=("wave", 45, 100, "synth free", 0, 1), char=("fold", 5, 64, 90))
P("moire", "pad", "orbit", knobs=dict(bright=74, ratio=45, edge=13, wobble=60), volume=79, amp=(80, 90, 90, 90),
  fenv=(30, 80, 50, 80), filter=("lp12", 72, 5, 70, 64), sends=(0, 45), lfo=("drift", 40, 70, "filter free", 0, 64),
  char=("ring", 55, 80, 127))
P("tow", "pad", "orbit", knobs=dict(bright=40, ratio=66, edge=10, wobble=23), volume=103, amp=(96, 110, 120, 110),
  fenv=(0, 80, 64, 80), filter=("lp24", 40, 10, 64, 64), sends=(0, 50), lfo=("drift", 30, 60, "synth free", 0, 64),
  char=("tilt", 40, 30, 127))
P("scrim", "fx", "orbit", knobs=dict(bright=127, ratio=88, edge=127, wobble=0), volume=111, amp=(0, 90, 0, 80),
  fenv=(0, 72, 60, 40), filter=("hp12", 78, 20, 112, 64), sends=(10, 10), char=("tilt", 90, 110, 127))
