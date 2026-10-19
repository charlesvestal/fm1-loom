# SPDX-License-Identifier: GPL-3.0-only
"""gingham's factory presets (the bank as it was before the 2026-10-19 sound redesign; tools/presets/lib.py says how a
preset is written)."""
from lib import P

P("picnic", "lead", "gingham", knobs=dict(model=0, timbre=110, color=37, arp=0), mode="mono", volume=89,
  amp=(0, 70, 90, 50), fenv=(0, 60, 0, 60), filter=("lp12", 92, 0, 64, 64), lfo=("wobble", 92, 74, 64, 30, 0),
  char=("tilt", 90, 20, 127))
P("check bass", "bass", "gingham", knobs=dict(model=0, timbre=114, color=0, arp=0), mode="mono", volume=127,
  amp=(48, 70, 60, 85), fenv=(0, 80, 60, 40), filter=("ladder", 100, 20, 88, 30), char=("drive", 100, 110, 127))
P("hamper", "bass", "gingham", knobs=dict(model=1, timbre=26, color=78, arp=0), mode="mono", volume=103,
  amp=(0, 80, 120, 70), fenv=(0, 72, 0, 40), filter=("lp24", 83, 0, 112, 40))
P("bunting", "lead", "gingham", knobs=dict(model=1, timbre=40, color=66, arp=0), mode="mono", volume=127,
  amp=(56, 90, 60, 50), fenv=(0, 60, 0, 40), filter=("lp12", 108, 50, 64, 64), lfo=("wobble", 92, 74, 64, 40, 0),
  char=("tilt", 75, 35, 127))
P("arcade", "chord", "gingham", knobs=dict(model=1, timbre=127, color=127, arp=4), volume=127, amp=(0, 100, 70, 80),
  fenv=(0, 60, 0, 60), filter=("bp12", 102, 28, 64, 64), sends=(20, 10))
P("pinwheel", "chord", "gingham", knobs=dict(model=0, timbre=60, color=60, arp=6), volume=94, amp=(50, 80, 70, 80),
  fenv=(0, 60, 0, 40), filter=("lp12", 120, 0, 64, 64), sends=(20, 10), char=("tilt", 30, 35, 127))
P("kite", "pluck", "gingham", knobs=dict(model=1, timbre=78, color=78, arp=8), volume=127, amp=(0, 72, 0, 85),
  fenv=(0, 60, 0, 40), filter=("lp12", 68, 0, 64, 64), sends=(15, 0), char=("tilt", 45, 95, 127))
P("pinafore", "bell", "gingham", knobs=dict(model=2, timbre=66, color=124, arp=0), volume=108, amp=(0, 106, 20, 80),
  fenv=(0, 56, 30, 60), filter=("lp12", 103, 0, 76, 64), sends=(0, 40), char=("tilt", 45, 95, 127))
P("parasol", "keys", "gingham", knobs=dict(model=3, timbre=80, color=107, arp=0), volume=97, amp=(20, 90, 0, 40),
  fenv=(0, 80, 60, 40), filter=("lp12", 100, 0, 100, 64), sends=(12, 6))
P("pixel", "fx", "gingham", knobs=dict(model=3, timbre=50, color=51, arp=1), mode="mono", volume=127,
  amp=(80, 80, 90, 80), fenv=(0, 80, 30, 60), filter=("bp12", 90, 10, 112, 64), sends=(20, 10),
  lfo=("wave", 4, 90, "synth", 1, 2))
P("plaid pad", "pad", "gingham", knobs=dict(model=1, timbre=78, color=84, arp=0), volume=118, amp=(108, 100, 90, 95),
  fenv=(0, 64, 60, 60), filter=("lp12", 75, 12, 76, 64), sends=(0, 60), lfo=("wave", 25, 84, "filter free", 0, 1),
  char=("tilt", 60, 20, 127))
P("apron", "strings", "gingham", knobs=dict(model=4, timbre=30, color=60, arp=0), volume=122,
  amp=(100, 110, 120, 100), fenv=(0, 80, 0, 60), filter=("lp24", 76, 12, 100, 64), sends=(0, 40))
P("tea towel", "drum", "gingham", knobs=dict(model=5, timbre=127, color=0, arp=0), volume=127, amp=(0, 90, 30, 50),
  fenv=(0, 72, 0, 60), filter=("hp12", 57, 0, 88, 64), sends=(10, 8), char=("tilt", 90, 80, 127))
P("jam jar", "drum", "gingham", knobs=dict(model=5, timbre=54, color=120, arp=0), volume=127, amp=(0, 70, 0, 50),
  fenv=(0, 60, 0, 40), filter=("lp24", 98, 5, 64, 64), sends=(20, 0), char=("tilt", 30, 20, 127))
P("bandana", "fx", "gingham", knobs=dict(model=4, timbre=78, color=16, arp=0), mode="mono", volume=127,
  amp=(0, 70, 110, 100), fenv=(0, 60, 0, 40), filter=("lp24", 46, 20, 64, 64), lfo=("wave", 4, 120, "synth", 1, 2),
  char=("tilt", 60, 35, 127))
P("deckchair", "keys", "gingham", knobs=dict(model=0, timbre=42, color=50, arp=0), volume=109, amp=(0, 90, 0, 56),
  fenv=(0, 72, 60, 40), filter=("lp12", 62, 5, 76, 64), lfo=("wobble", 30, 74, 64, 64, 0),
  char=("tilt", 90, 35, 127))
