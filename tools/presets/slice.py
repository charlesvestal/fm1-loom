# SPDX-License-Identifier: GPL-3.0-only
"""slice's factory presets (the bank as it was before the 2026-10-19 sound redesign; tools/presets/lib.py says how a
preset is written)."""
from lib import P

P("loom break", "loop", "slice", knobs=dict(sample=7, slices=1, pitch=64, gate=120), volume=121,
  amp=(0, 80, 127, 120), fenv=(0, 60, 0, 40), filter=("lp12", 127, 0, 64, 0), sends=(0, 8),
  char=("tilt", 90, 95, 127))
P("chop", "loop", "slice", knobs=dict(sample=9, slices=2, pitch=64, gate=63), mode="mono", volume=127,
  amp=(0, 80, 127, 40), fenv=(0, 64, 0, 40), filter=("lp12", 92, 5, 64, 0), sends=(0, 8), char=("drive", 30, 0, 127))
P("shuttle", "loop", "slice", knobs=dict(sample=10, slices=3, pitch=64, gate=103), volume=127, amp=(0, 80, 127, 40),
  fenv=(0, 60, 0, 40), filter=("lp12", 106, 12, 64, 0), char=("drive", 127, 115, 127))
P("heddle", "loop", "slice", knobs=dict(sample=9, slices=0, pitch=52, gate=127), mode="mono", volume=127,
  amp=(0, 80, 127, 90), fenv=(0, 60, 0, 40), filter=("lp24", 102, 28, 64, 0), char=("drive", 30, 30, 127))
P("treadle", "loop", "slice", knobs=dict(sample=8, slices=1, pitch=71, gate=95), volume=127, amp=(0, 80, 127, 40),
  fenv=(0, 80, 0, 40), filter=("lp12", 100, 0, 64, 0), sends=(10, 0), char=("tilt", 50, 90, 127))
P("batten", "loop", "slice", knobs=dict(sample=9, slices=1, pitch=59, gate=96), volume=127, amp=(0, 80, 127, 40),
  fenv=(0, 80, 0, 40), filter=("lp24", 36, 5, 76, 0), sends=(0, 8), char=("drive", 65, 40, 127))
P("pirn", "loop", "slice", knobs=dict(sample=7, slices=2, pitch=69, gate=120), mode="mono", volume=127,
  amp=(0, 80, 127, 40), fenv=(0, 72, 0, 40), filter=("lp12", 107, 0, 76, 0), char=("tilt", 30, 20, 127))
P("sley", "loop", "slice", knobs=dict(sample=7, slices=1, pitch=61, gate=127), volume=127, amp=(0, 80, 127, 90),
  fenv=(0, 60, 0, 40), filter=("lp12", 107, 0, 64, 0), sends=(0, 8), char=("crush", 60, 64, 127))
P("bobbin", "drum", "slice", knobs=dict(sample=9, slices=2, pitch=64, gate=32), volume=127, amp=(0, 80, 127, 70),
  fenv=(0, 80, 30, 30), filter=("lp24", 50, 28, 64, 0), char=("ring", 70, 117, 60))
P("selvedge", "drum", "slice", knobs=dict(sample=10, slices=1, pitch=64, gate=69), volume=127, amp=(0, 80, 127, 40),
  fenv=(0, 88, 30, 40), filter=("hp12", 70, 0, 112, 0), char=("tilt", 30, 80, 127))
P("picker", "drum", "slice", knobs=dict(sample=7, slices=2, pitch=64, gate=34), volume=124, amp=(0, 80, 127, 50),
  fenv=(0, 88, 0, 40), filter=("lp12", 119, 0, 100, 0), sends=(20, 0), char=("tilt", 90, 95, 127))
P("shed", "drum", "slice", knobs=dict(sample=8, slices=1, pitch=52, gate=127), volume=127, amp=(0, 80, 127, 90),
  fenv=(0, 60, 0, 40), filter=("lp24", 30, 40, 64, 0), sends=(20, 0), char=("drive", 60, 50, 127))
P("warp beam", "fx", "slice", knobs=dict(sample=8, slices=1, pitch=59, gate=127), mode="legato", volume=127,
  amp=(80, 70, 127, 50), fenv=(0, 60, 0, 40), filter=("lp12", 73, 30, 64, 0), lfo=("wave", 2, 90, "filter", 0, 1),
  char=("crush", 127, 0, 127))
P("harness", "fx", "slice", knobs=dict(sample=9, slices=1, pitch=64, gate=120), volume=127, amp=(20, 80, 127, 90),
  fenv=(0, 80, 0, 40), filter=("bp12", 104, 50, 64, 0), sends=(20, 30), char=("ring", 70, 10, 127))
P("lease", "fx", "slice", knobs=dict(sample=9, slices=2, pitch=64, gate=73), volume=127, amp=(20, 80, 127, 100),
  fenv=(0, 72, 60, 40), filter=("bp12", 115, 50, 64, 0), sends=(60, 20), char=("tilt", 90, 110, 127))
P("ratchet", "fx", "slice", knobs=dict(sample=7, slices=1, pitch=52, gate=127), mode="legato", volume=127,
  amp=(50, 80, 127, 90), fenv=(0, 60, 0, 40), filter=("bp12", 78, 0, 64, 0), lfo=("wave", 1, 48, "synth", 2, 2),
  char=("tilt", 90, 110, 127))
