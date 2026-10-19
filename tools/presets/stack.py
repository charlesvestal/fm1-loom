# SPDX-License-Identifier: GPL-3.0-only
"""stack's factory presets (the bank as it was before the 2026-10-19 sound redesign; tools/presets/lib.py says how a
preset is written)."""
from lib import P

P("ply bass", "bass", "stack", knobs=dict(wave=13, ratio=2, detune=4, spread=7), mode="mono", volume=106,
  amp=(0, 70, 95, 35), fenv=(0, 62, 0, 30), filter=("lp24", 60, 35, 100, 20), char=("drive", 30, 100, 127))
P("rope sub", "bass", "stack", knobs=dict(wave=64, ratio=1, detune=14, spread=7), mode="mono", volume=87,
  amp=(0, 70, 120, 35), fenv=(0, 80, 30, 30), filter=("lp24", 40, 0, 64, 30), char=("drive", 45, 70, 127))
P("hawser", "bass", "stack", knobs=dict(wave=7, ratio=1, detune=54, spread=14), mode="mono", volume=127,
  amp=(30, 100, 0, 70), fenv=(0, 60, 0, 40), filter=("lp24", 70, 10, 64, 40),
  lfo=("wave", 30, 84, "synth free", 2, 1), char=("drive", 0, 120, 127))
P("elastic", "bass", "stack", knobs=dict(wave=18, ratio=1, detune=2, spread=20), mode="legato", glide=80, volume=103,
  amp=(0, 60, 120, 35), fenv=(0, 72, 30, 40), filter=("ladder", 36, 0, 73, 64), lfo=("wave", 3, 127, "filter", 0, 2),
  char=("drive", 67, 90, 127))
P("lanyard", "lead", "stack", knobs=dict(wave=16, ratio=2, detune=12, spread=7), mode="legato", glide=28, volume=108,
  amp=(0, 70, 110, 64), fenv=(0, 56, 30, 50), filter=("ladder", 66, 35, 76, 64), lfo=("wobble", 92, 74, 64, 30, 0),
  char=("drive", 60, 80, 127))
P("twine fifth", "lead", "stack", knobs=dict(wave=63, ratio=3, detune=30, spread=75), mode="mono", volume=127,
  amp=(56, 80, 127, 30), fenv=(0, 64, 0, 40), filter=("lp12", 55, 53, 76, 64), sends=(30, 0),
  char=("fold", 34, 127, 127))
P("twister", "lead", "stack", knobs=dict(wave=40, ratio=1, detune=127, spread=127), mode="legato", glide=70,
  volume=76, amp=(56, 90, 127, 70), fenv=(0, 60, 0, 50), filter=("lp24", 50, 0, 64, 64), sends=(25, 0),
  lfo=("wave", 92, 127, "synth free", 0, 1), char=("drive", 80, 127, 127))
P("skein", "keys", "stack", knobs=dict(wave=117, ratio=1, detune=22, spread=64), volume=69, amp=(0, 84, 0, 56),
  fenv=(0, 56, 0, 60), filter=("lp12", 107, 10, 88, 64), sends=(0, 12), char=("tilt", 45, 35, 127))
P("sinew", "pluck", "stack", knobs=dict(wave=109, ratio=2, detune=0, spread=0), volume=96, amp=(0, 60, 0, 60),
  fenv=(0, 56, 30, 50), filter=("lp24", 45, 0, 64, 64), sends=(25, 0), char=("tilt", 75, 20, 127))
P("glass bead", "pluck", "stack", knobs=dict(wave=127, ratio=7, detune=10, spread=60), volume=82,
  amp=(0, 60, 10, 85), fenv=(0, 56, 30, 70), filter=("lp24", 61, 0, 64, 64), sends=(35, 8),
  char=("ring", 50, 110, 127))
P("braid stab", "stab", "stack", knobs=dict(wave=7, ratio=1, detune=22, spread=70), volume=106, amp=(0, 100, 0, 70),
  fenv=(0, 80, 0, 60), filter=("lp24", 68, 25, 112, 64), sends=(0, 15), char=("drive", 80, 90, 127))
P("plait brass", "brass", "stack", knobs=dict(wave=0, ratio=1, detune=15, spread=40), volume=113,
  amp=(52, 80, 100, 80), fenv=(0, 88, 0, 64), filter=("lp24", 35, 20, 100, 64), lfo=("wobble", 92, 74, 64, 20, 0),
  char=("tilt", 75, 80, 127))
P("cordage", "strings", "stack", knobs=dict(wave=56, ratio=1, detune=27, spread=127), volume=43,
  amp=(70, 100, 127, 80), fenv=(0, 80, 64, 80), filter=("lp12", 55, 0, 64, 64), sends=(0, 35),
  lfo=("wave", 50, 100, "synth free", 0, 1), char=("tilt", 30, 35, 127))
P("wool pad", "pad", "stack", knobs=dict(wave=0, ratio=2, detune=0, spread=46), volume=121, amp=(100, 90, 127, 88),
  fenv=(107, 116, 90, 96), filter=("lp24", 30, 0, 121, 50), sends=(0, 65), char=("drive", 29, 27, 127))
P("tartan", "chord", "stack", knobs=dict(wave=9, ratio=2, detune=0, spread=66), volume=92, amp=(50, 100, 40, 64),
  fenv=(0, 56, 0, 40), filter=("bp12", 86, 28, 88, 64), char=("tilt", 75, 110, 127))
P("tangle", "fx", "stack", knobs=dict(wave=127, ratio=5, detune=54, spread=60), mode="mono", volume=73,
  amp=(0, 90, 100, 100), fenv=(0, 60, 0, 50), filter=("lp24", 55, 35, 64, 64), sends=(30, 10),
  lfo=("wave", 5, 127, "synth", 1, 3))
