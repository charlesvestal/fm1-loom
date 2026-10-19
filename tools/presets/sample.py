# SPDX-License-Identifier: GPL-3.0-only
"""sample's factory presets (the bank as it was before the 2026-10-19 sound redesign; tools/presets/lib.py says how a
preset is written)."""
from lib import P

P("jute keys", "keys", "sample", knobs=dict(sample=5, start=7, length=110, mode=0), volume=120, amp=(0, 76, 127, 56),
  fenv=(0, 80, 0, 50), filter=("lp12", 98, 5, 88, 64), char=("drive", 45, 70, 127))
P("hemp bass", "bass", "sample", knobs=dict(sample=0, start=0, length=127, mode=0), mode="mono", volume=127,
  amp=(0, 70, 120, 40), fenv=(0, 60, 0, 30), filter=("lp24", 80, 10, 84, 40), char=("drive", 55, 60, 127))
P("kenaf", "bass", "sample", knobs=dict(sample=0, start=24, length=103, mode=0), mode="mono", volume=127,
  amp=(48, 100, 0, 80), fenv=(0, 60, 0, 40), filter=("lp12", 64, 0, 64, 30), char=("drive", 60, 30, 127))
P("hessian", "lead", "sample", knobs=dict(sample=4, start=49, length=0, mode=3), mode="mono", glide=10, volume=127,
  amp=(72, 70, 100, 50), fenv=(0, 66, 64, 50), filter=("lp24", 52, 31, 64, 64), sends=(0, 5),
  char=("crush", 89, 118, 127))
P("abaca", "drum", "sample", knobs=dict(sample=2, start=0, length=127, mode=0), volume=127, amp=(0, 70, 0, 50),
  fenv=(0, 60, 0, 40), filter=("lp12", 127, 0, 64, 64), sends=(30, 0), char=("tilt", 90, 95, 127))
P("coir", "drum", "sample", knobs=dict(sample=1, start=0, length=127, mode=3), volume=127, amp=(0, 80, 70, 70),
  fenv=(0, 88, 0, 40), filter=("lp12", 100, 40, 76, 64), sends=(15, 0), char=("crush", 15, 60, 127))
P("nettle", "drum", "sample", knobs=dict(sample=3, start=7, length=127, mode=0), volume=127, amp=(0, 90, 0, 80),
  fenv=(0, 60, 0, 40), filter=("lp24", 76, 28, 64, 64), sends=(0, 8), char=("crush", 10, 44, 127))
P("flax bell", "bell", "sample", knobs=dict(sample=5, start=0, length=127, mode=0), volume=127, amp=(0, 92, 0, 85),
  fenv=(0, 80, 0, 70), filter=("lp24", 80, 5, 64, 64), sends=(0, 25), char=("ring", 70, 114, 127))
P("roselle", "stab", "sample", knobs=dict(sample=5, start=0, length=90, mode=0), volume=127, amp=(0, 60, 0, 50),
  fenv=(0, 60, 0, 40), filter=("hp12", 45, 20, 64, 64), sends=(18, 10), char=("drive", 100, 100, 127))
P("raffia", "fx", "sample", knobs=dict(sample=5, start=7, length=103, mode=2), volume=127, amp=(80, 70, 0, 60),
  fenv=(0, 64, 0, 50), filter=("lp12", 112, 20, 64, 64), char=("tilt", 60, 110, 127))
P("urena", "chord", "sample", knobs=dict(sample=5, start=30, length=56, mode=1), volume=127, amp=(0, 80, 100, 80),
  fenv=(0, 60, 0, 40), filter=("lp12", 68, 0, 64, 64), sends=(15, 0), lfo=("wobble", 84, 70, 64, 64, 0),
  char=("tilt", 60, 80, 127))
P("jute choir", "voice", "sample", knobs=dict(sample=4, start=30, length=117, mode=1), volume=127,
  amp=(76, 110, 100, 104), fenv=(0, 80, 64, 80), filter=("lp12", 50, 13, 64, 50), sends=(0, 55),
  lfo=("wobble", 71, 77, 64, 20, 0), char=("crush", 64, 81, 127))
P("jute pad", "pad", "sample", knobs=dict(sample=4, start=40, length=127, mode=1), volume=91,
  amp=(100, 100, 90, 110), fenv=(0, 64, 0, 40), filter=("lp24", 55, 20, 88, 64), sends=(10, 50),
  lfo=("drift", 25, 60, "filter free", 0, 64), char=("tilt", 30, 95, 127))
P("henequen", "loop", "sample", knobs=dict(sample=7, start=0, length=96, mode=0), volume=109, amp=(0, 80, 127, 40),
  fenv=(0, 72, 0, 40), filter=("lp12", 116, 5, 112, 0), char=("drive", 30, 120, 127))
P("sisal sweep", "fx", "sample", knobs=dict(sample=6, start=24, length=127, mode=3), volume=127,
  amp=(80, 90, 100, 90), fenv=(50, 80, 50, 80), filter=("ladder", 105, 20, 64, 64), sends=(0, 20),
  lfo=("drift", 40, 70, "filter free", 0, 64), char=("tilt", 30, 35, 127))
P("istle", "fx", "sample", knobs=dict(sample=6, start=0, length=110, mode=2), volume=127, amp=(0, 70, 127, 80),
  fenv=(0, 60, 0, 40), filter=("hp12", 76, 30, 64, 64))
