# SPDX-License-Identifier: GPL-3.0-only
"""voile's factory presets (the bank as it was before the 2026-10-19 sound redesign; tools/presets/lib.py says how a
preset is written)."""
from lib import P

P("voile choir", "voice", "voile", knobs=dict(model=2, timbre=0, color=84, breath=27), volume=81,
  amp=(80, 100, 120, 104), fenv=(0, 60, 0, 60), filter=("lp12", 94, 20, 64, 64), sends=(0, 60),
  lfo=("drift", 30, 30, "synth free", 1, 64), char=("tilt", 90, 20, 127))
P("basso", "bass", "voile", knobs=dict(model=2, timbre=100, color=0, breath=41), mode="mono", volume=127,
  amp=(40, 70, 110, 85), fenv=(0, 60, 0, 60), filter=("lp24", 63, 50, 64, 64), lfo=("wobble", 92, 74, 64, 60, 0),
  char=("tilt", 30, 95, 127))
P("batiste", "bass", "voile", knobs=dict(model=0, timbre=64, color=84, breath=0), mode="mono",
  amp=(48, 100, 120, 85), fenv=(0, 88, 0, 40), filter=("lp24", 44, 50, 76, 40), char=("drive", 10, 50, 127))
P("sheer lead", "lead", "voile", knobs=dict(model=0, timbre=70, color=102, breath=0), mode="legato", glide=25,
  volume=92, amp=(20, 90, 127, 78), fenv=(0, 60, 0, 60), filter=("lp24", 100, 35, 64, 64),
  lfo=("wobble", 92, 74, 64, 30, 0), char=("drive", 0, 100, 127))
P("mousseline", "lead", "voile", knobs=dict(model=1, timbre=40, color=64, breath=24), mode="legato", glide=20,
  volume=127, amp=(0, 70, 120, 78), fenv=(0, 60, 0, 40), filter=("lp12", 79, 50, 64, 64),
  lfo=("wave", 60, 120, "synth", 1, 1), char=("drive", 20, 60, 127))
P("tulle", "keys", "voile", knobs=dict(model=0, timbre=127, color=127, breath=24), volume=93, amp=(20, 76, 20, 84),
  fenv=(0, 64, 30, 60), filter=("lp24", 120, 28, 88, 64), char=("tilt", 90, 110, 127))
P("chatter", "fx", "voile", knobs=dict(model=1, timbre=64, color=40, breath=57), volume=127, amp=(50, 80, 120, 100),
  fenv=(0, 56, 30, 60), filter=("bp12", 102, 0, 100, 64), sends=(20, 10), lfo=("wave", 3, 100, "synth", 1, 1),
  char=("tilt", 45, 80, 127))
P("doo wop", "voice", "voile", knobs=dict(model=1, timbre=100, color=30, breath=80), volume=127, amp=(0, 90, 40, 50),
  fenv=(0, 60, 0, 60), filter=("hp12", 14, 0, 64, 127), sends=(15, 35), char=("drive", 76, 100, 127))
P("illusion", "pluck", "voile", knobs=dict(model=1, timbre=56, color=119, breath=0), volume=127, amp=(0, 74, 10, 40),
  fenv=(0, 60, 0, 40), filter=("lp12", 88, 5, 64, 64), sends=(25, 8), char=("tilt", 75, 110, 127))
P("tissue", "bell", "voile", knobs=dict(model=0, timbre=117, color=120, breath=0), volume=127, amp=(0, 106, 20, 108),
  fenv=(0, 88, 30, 40), filter=("bp12", 114, 0, 76, 64), sends=(15, 40), char=("tilt", 45, 35, 127))
P("organdy", "stab", "voile", knobs=dict(model=2, timbre=44, color=74, breath=66), volume=127, amp=(0, 100, 0, 55),
  fenv=(0, 56, 0, 60), filter=("lp24", 45, 28, 64, 64), sends=(18, 0), char=("drive", 60, 127, 127))
P("tarlatan", "chord", "voile", knobs=dict(model=0, timbre=57, color=54, breath=0), volume=118, amp=(0, 90, 40, 64),
  fenv=(0, 72, 0, 40), filter=("lp24", 50, 0, 64, 64), sends=(0, 10), char=("crush", 80, 94, 127))
P("ninon", "voice", "voile", knobs=dict(model=2, timbre=60, color=127, breath=17), volume=122,
  amp=(100, 90, 120, 94), fenv=(0, 88, 0, 40), filter=("bp12", 110, 0, 112, 64), sends=(0, 50),
  lfo=("wobble", 84, 72, 64, 20, 0), char=("tilt", 30, 110, 127))
P("veil", "pad", "voile", knobs=dict(model=3, timbre=107, color=70, breath=127), volume=127, amp=(100, 100, 110, 88),
  fenv=(0, 56, 30, 60), filter=("lp24", 76, 20, 100, 64), sends=(0, 50), lfo=("wave", 20, 80, "synth free", 2, 1),
  char=("tilt", 90, 95, 127))
P("net curtain", "pad", "voile", knobs=dict(model=3, timbre=36, color=118, breath=94), volume=118,
  amp=(100, 90, 120, 100), fenv=(0, 60, 0, 60), filter=("lp12", 112, 0, 64, 64), sends=(0, 60),
  char=("tilt", 75, 35, 127))
P("lisse", "fx", "voile", knobs=dict(model=3, timbre=127, color=100, breath=127), volume=127,
  amp=(80, 100, 110, 100), fenv=(0, 60, 0, 40), filter=("hp12", 100, 20, 64, 64), sends=(20, 50),
  lfo=("drift", 20, 100, "synth free", 2, 64))
