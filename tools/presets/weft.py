# SPDX-License-Identifier: GPL-3.0-only
"""weft's factory presets (the bank as it was before the 2026-10-19 sound redesign; tools/presets/lib.py says how a
preset is written)."""
from lib import P

P("cotton keys", "keys", "weft", knobs=dict(bend=70, wave=20, sweep=0, spread=30), volume=107, amp=(0, 84, 20, 84),
  fenv=(0, 72, 10, 60), filter=("lp12", 78, 10, 84, 64), char=("drive", 30, 127, 127))
P("denim bass", "bass", "weft", knobs=dict(bend=92, wave=40, sweep=0, spread=0), mode="mono", volume=109,
  amp=(0, 72, 95, 40), fenv=(0, 62, 0, 40), filter=("ladder", 58, 30, 100, 40), sends=(0, 5),
  char=("drive", 60, 60, 127))
P("acid cord", "bass", "weft", knobs=dict(bend=127, wave=13, sweep=0, spread=14), mode="legato", glide=18,
  volume=127, amp=(0, 80, 60, 85), fenv=(0, 72, 0, 40), filter=("ladder", 36, 0, 112, 40),
  char=("drive", 80, 110, 127))
P("poplin sub", "bass", "weft", knobs=dict(bend=0, wave=14, sweep=7, spread=17), mode="mono", volume=127,
  amp=(48, 100, 0, 80), fenv=(0, 60, 0, 40), filter=("lp24", 68, 0, 64, 64), char=("drive", 50, 60, 127))
P("thread lead", "lead", "weft", knobs=dict(bend=102, wave=71, sweep=78, spread=33), mode="mono", volume=104,
  amp=(0, 70, 90, 64), fenv=(0, 64, 0, 40), filter=("ladder", 53, 32, 112, 64), lfo=("wobble", 92, 74, 64, 30, 0),
  char=("drive", 68, 125, 127))
P("twill", "lead", "weft", knobs=dict(bend=14, wave=112, sweep=126, spread=24), mode="legato", glide=30, volume=93,
  amp=(56, 100, 127, 50), fenv=(0, 64, 30, 50), filter=("lp12", 100, 35, 100, 64),
  lfo=("wave", 60, 100, "synth", 2, 1), char=("drive", 70, 127, 127))
P("madras", "keys", "weft", knobs=dict(bend=127, wave=57, sweep=0, spread=24), volume=127, amp=(0, 102, 0, 70),
  fenv=(0, 56, 0, 40), filter=("bp12", 66, 45, 112, 64), char=("drive", 0, 117, 127))
P("button", "pluck", "weft", knobs=dict(bend=80, wave=100, sweep=16, spread=0), volume=127, amp=(0, 72, 0, 85),
  fenv=(0, 64, 0, 40), filter=("lp12", 105, 5, 76, 64), sends=(25, 0), char=("crush", 35, 10, 127))
P("chintz bell", "bell", "weft", knobs=dict(bend=36, wave=127, sweep=100, spread=40), volume=127,
  amp=(0, 108, 0, 104), fenv=(0, 72, 60, 80), filter=("bp12", 116, 0, 76, 64), sends=(0, 45))
P("pulse yarn", "chord", "weft", knobs=dict(bend=95, wave=64, sweep=7, spread=57), volume=99, amp=(50, 80, 40, 50),
  fenv=(0, 56, 30, 50), filter=("lp24", 50, 12, 64, 64), lfo=("wave", 60, 90, "synth free", 0, 1),
  char=("crush", 30, 90, 80))
P("burlap", "stab", "weft", knobs=dict(bend=84, wave=90, sweep=16, spread=46), volume=127, amp=(0, 82, 20, 55),
  fenv=(0, 88, 30, 50), filter=("lp24", 45, 12, 64, 64), sends=(18, 18), char=("fold", 70, 0, 127))
P("calico", "brass", "weft", knobs=dict(bend=100, wave=10, sweep=0, spread=70), volume=123, amp=(36, 84, 80, 64),
  fenv=(0, 64, 0, 66), filter=("ladder", 35, 0, 112, 64), sends=(0, 18), lfo=("wobble", 92, 74, 64, 20, 0),
  char=("drive", 20, 127, 127))
P("muslin pad", "pad", "weft", knobs=dict(bend=89, wave=0, sweep=0, spread=127), volume=70, amp=(79, 90, 110, 100),
  fenv=(0, 80, 64, 80), filter=("hp12", 57, 29, 64, 64), sends=(10, 70), lfo=("wave", 72, 88, "synth free", 3, 1),
  char=("tilt", 45, 126, 127))
P("chambray", "strings", "weft", knobs=dict(bend=92, wave=24, sweep=0, spread=89), volume=71, amp=(90, 90, 120, 90),
  fenv=(0, 80, 64, 80), filter=("lp12", 55, 0, 64, 64), sends=(0, 20), lfo=("wave", 45, 90, "synth free", 3, 1),
  char=("tilt", 60, 110, 127))
P("seersucker", "fx", "weft", knobs=dict(bend=90, wave=64, sweep=0, spread=7), mode="mono", volume=78,
  amp=(50, 110, 120, 100), fenv=(0, 64, 0, 40), filter=("lp12", 86, 10, 76, 64), sends=(80, 10),
  lfo=("wave", 82, 90, "voice", 1, 0), char=("tilt", 45, 20, 127))
P("khaki zap", "fx", "weft", knobs=dict(bend=110, wave=110, sweep=12, spread=0), mode="mono", volume=127,
  amp=(80, 90, 0, 60), fenv=(0, 88, 30, 40), filter=("ladder", 115, 35, 88, 64), sends=(30, 0),
  lfo=("wave", 5, 127, "voice", 1, 3), char=("tilt", 60, 20, 127))
