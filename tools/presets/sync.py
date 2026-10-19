# SPDX-License-Identifier: GPL-3.0-only
"""sync's factory presets (the bank as it was before the 2026-10-19 sound redesign; tools/presets/lib.py says how a
preset is written)."""
from lib import P

P("wire stab", "stab", "sync", knobs=dict(reach=57, sub=20, hiss=0, thin=0), volume=127, amp=(0, 66, 20, 40),
  fenv=(0, 80, 0, 40), filter=("lp24", 88, 20, 64, 64), sends=(0, 10), char=("drive", 60, 110, 127))
P("wire bass", "bass", "sync", knobs=dict(reach=53, sub=80, hiss=0, thin=0), mode="mono", volume=109,
  amp=(10, 60, 120, 85), fenv=(0, 62, 0, 30), filter=("lp12", 76, 50, 90, 40), char=("drive", 65, 80, 127))
P("gauge sub", "bass", "sync", knobs=dict(reach=76, sub=127, hiss=0, thin=14), mode="mono", volume=127,
  amp=(30, 80, 120, 85), fenv=(0, 60, 0, 40), filter=("ladder", 36, 20, 64, 30), char=("drive", 20, 0, 127))
P("zipper", "lead", "sync", knobs=dict(reach=70, sub=0, hiss=0, thin=20), mode="legato", glide=15, volume=97,
  amp=(40, 80, 127, 36), fenv=(0, 80, 30, 40), filter=("lp12", 96, 10, 88, 64), lfo=("wave", 1, 92, "synth", 0, 3),
  char=("tilt", 30, 110, 127))
P("filament", "lead", "sync", knobs=dict(reach=69, sub=0, hiss=0, thin=90), mode="legato", glide=20, volume=119,
  amp=(56, 60, 100, 72), fenv=(0, 88, 0, 40), filter=("lp12", 120, 50, 88, 64), sends=(25, 0),
  lfo=("wobble", 92, 74, 64, 30, 0))
P("solder", "lead", "sync", knobs=dict(reach=90, sub=68, hiss=0, thin=0), mode="legato", glide=30, volume=108,
  amp=(56, 80, 127, 36), fenv=(0, 80, 60, 50), filter=("ladder", 72, 50, 90, 64), lfo=("wobble", 92, 74, 64, 40, 0),
  char=("drive", 90, 110, 127))
P("staple", "pluck", "sync", knobs=dict(reach=90, sub=62, hiss=60, thin=16), volume=127, amp=(0, 90, 10, 85),
  fenv=(0, 40, 0, 30), filter=("lp24", 78, 0, 64, 64), sends=(35, 8), char=("crush", 60, 50, 127))
P("coil", "keys", "sync", knobs=dict(reach=60, sub=0, hiss=0, thin=80), volume=107, amp=(20, 70, 0, 84),
  fenv=(0, 64, 0, 40), filter=("lp24", 74, 30, 76, 64), char=("tilt", 75, 20, 127))
P("tinsel", "bell", "sync", knobs=dict(reach=73, sub=0, hiss=20, thin=84), volume=115, amp=(0, 106, 0, 80),
  fenv=(0, 60, 0, 50), filter=("lp12", 95, 5, 64, 64), sends=(0, 25), char=("drive", 60, 120, 127))
P("rivet", "brass", "sync", knobs=dict(reach=54, sub=30, hiss=0, thin=0), volume=78, amp=(68, 92, 110, 64),
  fenv=(0, 80, 60, 64), filter=("lp24", 50, 28, 112, 64), sends=(0, 18), lfo=("wobble", 92, 74, 64, 20, 0),
  char=("tilt", 60, 95, 127))
P("mesh", "strings", "sync", knobs=dict(reach=28, sub=27, hiss=0, thin=61), volume=104, amp=(100, 90, 100, 92),
  fenv=(0, 80, 64, 80), filter=("lp12", 80, 0, 64, 64), sends=(0, 20), lfo=("wave", 40, 90, "synth free", 0, 1),
  char=("tilt", 45, 50, 127))
P("steel pad", "pad", "sync", knobs=dict(reach=95, sub=0, hiss=12, thin=12), volume=76, amp=(49, 80, 127, 112),
  fenv=(0, 80, 64, 80), filter=("lp12", 92, 35, 64, 64), sends=(0, 60), lfo=("wave", 28, 86, "synth free", 0, 1),
  char=("ring", 78, 98, 105))
P("foil", "chord", "sync", knobs=dict(reach=36, sub=0, hiss=7, thin=0), volume=78, amp=(30, 90, 100, 80),
  fenv=(0, 80, 0, 40), filter=("lp12", 89, 0, 100, 64), char=("tilt", 50, 80, 127))
P("frayed", "fx", "sync", knobs=dict(reach=127, sub=0, hiss=100, thin=64), volume=118, amp=(80, 72, 50, 100),
  fenv=(0, 56, 0, 50), filter=("lp12", 83, 35, 88, 64), sends=(20, 20), lfo=("drift", 70, 60, "synth", 0, 64),
  char=("fold", 70, 60, 127))
P("barbed", "fx", "sync", knobs=dict(reach=54, sub=0, hiss=106, thin=127), volume=119, amp=(80, 100, 110, 100),
  fenv=(0, 80, 64, 80), filter=("ladder", 102, 25, 64, 64), sends=(30, 20),
  lfo=("drift", 30, 100, "filter free", 0, 64))
P("spark", "fx", "sync", knobs=dict(reach=127, sub=62, hiss=30, thin=108), volume=127, amp=(20, 80, 0, 100),
  fenv=(0, 88, 0, 40), filter=("lp24", 90, 35, 64, 64), sends=(40, 0), lfo=("wave", 5, 127, "synth", 0, 3),
  char=("tilt", 90, 95, 127))
