# SPDX-License-Identifier: GPL-3.0-only
"""cane's factory presets (the bank as it was before the 2026-10-19 sound redesign; tools/presets/lib.py says how a
preset is written)."""
from lib import P

P("wicker harp", "pluck", "cane", knobs=dict(model=0, timbre=65, color=50, damp=10), volume=79,
  amp=(0, 100, 100, 70), fenv=(0, 56, 0, 60), filter=("lp12", 98, 20, 112, 64), sends=(25, 0),
  char=("tilt", 60, 110, 127))
P("rattan bass", "bass", "cane", knobs=dict(model=0, timbre=95, color=90, damp=40), mode="mono", volume=116,
  amp=(0, 60, 100, 70), fenv=(0, 60, 20, 40), filter=("ladder", 88, 10, 76, 30), char=("drive", 90, 100, 127))
P("thatch", "bass", "cane", knobs=dict(model=1, timbre=80, color=2, damp=54), mode="mono", volume=127,
  amp=(40, 90, 90, 30), fenv=(0, 60, 0, 40), filter=("lp24", 86, 50, 64, 30), char=("tilt", 45, 110, 127))
P("willow", "lead", "cane", knobs=dict(model=1, timbre=70, color=74, damp=24), mode="legato", glide=20, volume=127,
  amp=(50, 90, 90, 60), fenv=(0, 80, 0, 40), filter=("ladder", 108, 35, 112, 64), sends=(0, 8),
  lfo=("wobble", 92, 74, 64, 30, 0), char=("tilt", 60, 110, 127))
P("basket reed", "lead", "cane", knobs=dict(model=2, timbre=80, color=36, damp=13), mode="legato", glide=15,
  volume=120, amp=(40, 80, 60, 50), fenv=(0, 60, 0, 60), filter=("lp24", 100, 0, 64, 64), sends=(15, 0),
  lfo=("wobble", 92, 74, 64, 50, 0), char=("tilt", 60, 20, 127))
P("bamboo", "lead", "cane", knobs=dict(model=3, timbre=50, color=64, damp=0), mode="mono", volume=97,
  amp=(36, 80, 110, 78), fenv=(0, 64, 60, 60), filter=("lp12", 114, 10, 64, 64), sends=(25, 10),
  lfo=("wobble", 92, 74, 64, 60, 0), char=("tilt", 75, 50, 127))
P("sugar", "brass", "cane", knobs=dict(model=2, timbre=93, color=117, damp=24), volume=94, amp=(68, 76, 120, 56),
  fenv=(0, 56, 30, 40), filter=("lp24", 60, 0, 76, 64), lfo=("wobble", 92, 74, 64, 50, 0),
  char=("drive", 20, 50, 127))
P("osier", "pluck", "cane", knobs=dict(model=0, timbre=44, color=114, damp=0), volume=96, amp=(0, 80, 0, 55),
  fenv=(0, 56, 0, 40), filter=("bp12", 76, 0, 88, 64), sends=(0, 8))
P("lacquer", "pluck", "cane", knobs=dict(model=0, timbre=60, color=44, damp=60), volume=127, amp=(0, 60, 10, 70),
  fenv=(0, 60, 0, 40), filter=("lp12", 115, 0, 64, 64), char=("fold", 50, 90, 127))
P("cane cello", "strings", "cane", knobs=dict(model=1, timbre=46, color=33, damp=41), mode="legato", glide=20,
  volume=127, amp=(100, 90, 110, 100), fenv=(0, 60, 0, 60), filter=("lp24", 95, 20, 64, 64), sends=(0, 35),
  lfo=("wobble", 92, 74, 64, 40, 0), char=("tilt", 90, 110, 127))
P("cane field", "pad", "cane", knobs=dict(model=1, timbre=50, color=90, damp=26), volume=88, amp=(80, 100, 110, 110),
  fenv=(0, 60, 0, 60), filter=("lp12", 65, 20, 64, 64), sends=(0, 35), lfo=("wave", 25, 84, "synth free", 2, 1),
  char=("tilt", 60, 35, 127))
P("rush", "pad", "cane", knobs=dict(model=3, timbre=0, color=84, damp=19), volume=113, amp=(100, 100, 120, 110),
  fenv=(0, 60, 0, 40), filter=("lp12", 94, 28, 64, 64), sends=(0, 45), lfo=("wobble", 40, 72, 64, 30, 0),
  char=("tilt", 30, 35, 127))
P("temple bell", "bell", "cane", knobs=dict(model=4, timbre=100, color=30, damp=30), volume=98,
  amp=(0, 100, 127, 108), fenv=(0, 60, 0, 60), filter=("lp12", 126, 20, 64, 64), sends=(0, 50),
  char=("tilt", 75, 20, 127))
P("malacca", "bell", "cane", knobs=dict(model=4, timbre=127, color=86, damp=0), volume=127, amp=(0, 112, 0, 80),
  fenv=(0, 80, 0, 40), filter=("bp12", 90, 5, 100, 64), sends=(0, 40), char=("tilt", 45, 110, 127))
P("hand drum", "drum", "cane", knobs=dict(model=5, timbre=70, color=100, damp=20), volume=107, amp=(0, 95, 0, 60),
  fenv=(0, 50, 0, 60), filter=("lp24", 92, 0, 110, 32), sends=(0, 25), char=("fold", 95, 64, 127))
P("kooboo", "drum", "cane", knobs=dict(model=5, timbre=11, color=6, damp=60), volume=127, amp=(0, 90, 0, 50),
  fenv=(0, 56, 60, 40), filter=("lp24", 118, 28, 100, 64), sends=(0, 8), char=("drive", 20, 0, 127))
