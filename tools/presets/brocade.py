# SPDX-License-Identifier: GPL-3.0-only
"""brocade's factory presets (the bank as it was before the 2026-10-19 sound redesign; tools/presets/lib.py says how a
preset is written)."""
from lib import P

P("gold thread", "lead", "brocade", knobs=dict(saws=7, detune=46, mix=100, spread=80), mode="legato", glide=22,
  volume=92, amp=(2, 70, 90, 78), fenv=(0, 60, 60, 50), filter=("lp24", 112, 15, 80, 64),
  lfo=("wobble", 92, 74, 64, 40, 0), char=("drive", 20, 127, 127))
P("tweed bass", "bass", "brocade", knobs=dict(saws=5, detune=30, mix=104, spread=68), mode="mono", volume=127,
  amp=(0, 70, 60, 85), fenv=(0, 80, 0, 30), filter=("ladder", 42, 0, 64, 40), char=("drive", 60, 60, 127))
P("tabby", "bass", "brocade", knobs=dict(saws=3, detune=33, mix=110, spread=60), mode="legato", glide=40, volume=115,
  amp=(48, 80, 60, 55), fenv=(0, 80, 0, 40), filter=("ladder", 36, 0, 88, 40),
  lfo=("wave", 30, 92, "filter free", 0, 1), char=("fold", 0, 60, 127))
P("chenille", "lead", "brocade", knobs=dict(saws=7, detune=110, mix=127, spread=100), mode="mono", glide=60,
  volume=112, amp=(0, 70, 127, 36), fenv=(0, 56, 0, 40), filter=("ladder", 86, 50, 76, 64), sends=(20, 10),
  lfo=("wobble", 88, 90, 64, 40, 0), char=("fold", 15, 90, 100))
P("zari", "lead", "brocade", knobs=dict(saws=1, detune=0, mix=0, spread=7), mode="legato", glide=25, volume=127,
  amp=(0, 80, 110, 50), fenv=(0, 88, 0, 50), filter=("ladder", 58, 50, 64, 64), sends=(25, 0),
  lfo=("wobble", 92, 74, 64, 30, 0), char=("drive", 50, 80, 127))
P("sequin", "pluck", "brocade", knobs=dict(saws=7, detune=50, mix=100, spread=90), volume=110, amp=(0, 84, 10, 40),
  fenv=(0, 66, 10, 44), filter=("lp24", 102, 22, 100, 64), sends=(30, 8), char=("drive", 70, 120, 127))
P("samite", "stab", "brocade", knobs=dict(saws=7, detune=60, mix=100, spread=90), volume=83, amp=(0, 80, 0, 40),
  fenv=(0, 64, 30, 50), filter=("lp24", 64, 28, 76, 64), char=("tilt", 45, 110, 127))
P("lampas", "brass", "brocade", knobs=dict(saws=5, detune=25, mix=80, spread=80), volume=127, amp=(68, 84, 80, 56),
  fenv=(0, 56, 0, 66), filter=("lp24", 35, 20, 88, 64), lfo=("wobble", 92, 74, 64, 20, 0),
  char=("tilt", 30, 50, 127))
P("jacquard", "strings", "brocade", knobs=dict(saws=5, detune=44, mix=90, spread=103), volume=82,
  amp=(100, 100, 100, 80), fenv=(0, 64, 0, 80), filter=("lp12", 74, 0, 64, 64), sends=(0, 45),
  char=("tilt", 30, 35, 127))
P("damask", "pad", "brocade", knobs=dict(saws=7, detune=40, mix=110, spread=127), volume=70, amp=(92, 90, 100, 110),
  fenv=(80, 90, 60, 90), filter=("lp12", 78, 0, 74, 50), sends=(0, 70), lfo=("wave", 25, 84, "filter free", 0, 1),
  char=("tilt", 45, 80, 127))
P("velvet", "pad", "brocade", knobs=dict(saws=7, detune=3, mix=62, spread=70), volume=127, amp=(108, 100, 110, 110),
  fenv=(30, 80, 60, 80), filter=("lp24", 50, 5, 70, 64), sends=(0, 55), char=("tilt", 30, 110, 127))
P("shot silk", "pad", "brocade", knobs=dict(saws=7, detune=70, mix=110, spread=100), volume=73,
  amp=(60, 100, 100, 110), fenv=(0, 80, 0, 80), filter=("lp12", 72, 0, 76, 64), sends=(10, 20),
  lfo=("wave", 22, 88, "synth free", 1, 1), char=("tilt", 75, 95, 127))
P("organza", "pad", "brocade", knobs=dict(saws=7, detune=70, mix=113, spread=110), volume=121,
  amp=(108, 90, 110, 80), fenv=(0, 80, 0, 60), filter=("hp12", 51, 12, 88, 64), sends=(15, 40),
  char=("tilt", 45, 80, 127))
P("kinkhab", "voice", "brocade", knobs=dict(saws=7, detune=26, mix=100, spread=100), volume=123,
  amp=(88, 100, 120, 84), fenv=(0, 60, 0, 40), filter=("bp12", 72, 60, 64, 64), sends=(0, 20),
  lfo=("wave", 30, 90, "filter free", 0, 1), char=("tilt", 45, 50, 127))
P("tapestry", "chord", "brocade", knobs=dict(saws=5, detune=16, mix=90, spread=90), volume=81, amp=(30, 90, 40, 64),
  fenv=(0, 72, 0, 60), filter=("lp24", 75, 0, 88, 64), sends=(0, 8), char=("tilt", 45, 80, 127))
P("baldachin", "fx", "brocade", knobs=dict(saws=7, detune=20, mix=76, spread=103), volume=127,
  amp=(90, 110, 120, 100), fenv=(0, 60, 0, 40), filter=("lp24", 30, 30, 64, 64), sends=(20, 50),
  lfo=("wave", 0, 120, "synth", 1, 2))
