# SPDX-License-Identifier: GPL-3.0-only
"""swarm's factory presets (the bank as it was before the 2026-10-19 sound redesign; tools/presets/lib.py says how a
preset is written)."""
from lib import P

P("mohair", "keys", "swarm", knobs=dict(swarm=30, ring=10, tone=55, spread=32), volume=121, amp=(0, 90, 40, 70),
  fenv=(0, 60, 0, 40), filter=("lp12", 110, 5, 64, 64), sends=(0, 6))
P("fuzz bass", "bass", "swarm", knobs=dict(swarm=24, ring=25, tone=70, spread=0), mode="mono", volume=124,
  amp=(0, 70, 120, 85), fenv=(0, 56, 30, 30), filter=("ladder", 90, 35, 76, 40), char=("fold", 95, 50, 127))
P("vicuna", "bass", "swarm", knobs=dict(swarm=0, ring=0, tone=0, spread=0), mode="mono", volume=127,
  amp=(48, 80, 90, 85), fenv=(0, 60, 0, 40), filter=("ladder", 36, 20, 64, 30), char=("drive", 0, 10, 127))
P("glint lead", "lead", "swarm", knobs=dict(swarm=18, ring=35, tone=127, spread=99), mode="legato", glide=31,
  volume=109, amp=(0, 80, 127, 78), fenv=(0, 60, 64, 40), filter=("hp12", 62, 58, 64, 64),
  lfo=("wobble", 90, 97, 64, 64, 0), char=("drive", 117, 117, 127))
P("angora", "lead", "swarm", knobs=dict(swarm=4, ring=0, tone=0, spread=0), mode="legato", glide=70, volume=81,
  amp=(40, 80, 120, 78), fenv=(0, 80, 30, 40), filter=("lp12", 120, 0, 100, 64), sends=(25, 0),
  lfo=("wobble", 90, 110, 64, 30, 0))
P("alpaca", "lead", "swarm", knobs=dict(swarm=74, ring=0, tone=8, spread=10), mode="mono", volume=112,
  amp=(56, 80, 110, 78), fenv=(0, 60, 0, 40), filter=("lp12", 85, 50, 64, 64), sends=(15, 10),
  lfo=("wobble", 92, 74, 64, 40, 0), char=("tilt", 30, 35, 127))
P("carded", "pluck", "swarm", knobs=dict(swarm=20, ring=6, tone=77, spread=20), volume=127, amp=(0, 72, 0, 60),
  fenv=(0, 64, 0, 40), filter=("lp24", 120, 12, 64, 64), sends=(25, 0))
P("shearling", "bell", "swarm", knobs=dict(swarm=10, ring=0, tone=83, spread=50), volume=109, amp=(0, 104, 0, 100),
  fenv=(0, 60, 0, 40), filter=("lp12", 118, 0, 64, 64), sends=(0, 10), char=("tilt", 45, 95, 127))
P("burr", "stab", "swarm", knobs=dict(swarm=87, ring=96, tone=46, spread=60), volume=127, amp=(0, 76, 20, 40),
  fenv=(0, 60, 0, 40), filter=("lp24", 88, 20, 64, 64), char=("fold", 110, 80, 127))
P("cashmere", "chord", "swarm", knobs=dict(swarm=8, ring=0, tone=0, spread=16), volume=116, amp=(30, 60, 70, 90),
  fenv=(0, 64, 0, 40), filter=("lp12", 50, 5, 112, 64), char=("tilt", 60, 110, 127))
P("merino", "voice", "swarm", knobs=dict(swarm=20, ring=0, tone=32, spread=67), volume=127, amp=(100, 100, 127, 84),
  fenv=(0, 60, 0, 40), filter=("lp24", 50, 20, 64, 64), sends=(10, 20), lfo=("wobble", 70, 72, 64, 30, 0),
  char=("tilt", 75, 110, 127))
P("fleece pad", "pad", "swarm", knobs=dict(swarm=50, ring=40, tone=103, spread=103), volume=103,
  amp=(100, 100, 120, 110), fenv=(0, 80, 64, 80), filter=("lp12", 95, 5, 64, 64), sends=(0, 70),
  lfo=("drift", 30, 70, "synth free", 2, 64), char=("drive", 127, 127, 127))
P("gossamer", "pad", "swarm", knobs=dict(swarm=78, ring=0, tone=111, spread=65), volume=127, amp=(108, 85, 100, 115),
  fenv=(0, 60, 0, 40), filter=("lp12", 112, 0, 64, 64), sends=(10, 70), lfo=("wave", 30, 90, "synth free", 2, 1),
  char=("ring", 86, 80, 127))
P("worsted", "strings", "swarm", knobs=dict(swarm=0, ring=60, tone=40, spread=40), volume=77,
  amp=(60, 100, 127, 100), fenv=(0, 64, 60, 40), filter=("lp24", 58, 10, 88, 64), sends=(0, 50),
  char=("tilt", 60, 35, 127))
P("roving", "fx", "swarm", knobs=dict(swarm=40, ring=44, tone=60, spread=50), volume=127, amp=(80, 90, 0, 50),
  fenv=(0, 64, 30, 40), filter=("lp12", 64, 20, 76, 64), sends=(30, 20), lfo=("wave", 4, 110, "synth", 2, 0),
  char=("tilt", 75, 80, 127))
P("breeze", "fx", "swarm", knobs=dict(swarm=103, ring=23, tone=16, spread=70), volume=127, amp=(100, 90, 120, 100),
  fenv=(0, 80, 64, 80), filter=("lp12", 66, 67, 64, 30), sends=(0, 70), lfo=("wave", 15, 107, "filter free", 0, 1))
