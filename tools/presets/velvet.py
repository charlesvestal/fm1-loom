# SPDX-License-Identifier: MPL-2.0
"""velvet's presets, the first set (2026-11-23): a string machine. knobs: register (VIOLIN 8' 4', VIOLA 8', CELLO 16'
8', BASS 16', FULL all three), bright, chorus (the ensemble's depth), rate (its two LFOs, the fast one about eight times the
slow). x = (shape, width, detune, -): saw .. square, the three copies across the stereo field, the copies apart."""
from lib import P

VIOLIN, VIOLA, CELLO, BASS, FULL = range(5)

# its sound in a new project: the ensemble's strings, slow in, slow out, in the room
P("velvet", "strings", "velvet", knobs=dict(register=VIOLIN, bright=70, chorus=70, rate=40), volume=104,
  amp=(70, 64, 127, 90), sends=(0, 40), x=(0, 90, 30, 0), genres=("synthpop", "lofi"))
# an ensemble: the 8' alone, a deeper ensemble
P("ensemble", "strings", "velvet", knobs=dict(register=VIOLA, bright=64, chorus=90, rate=36), volume=104,
  amp=(64, 64, 127, 86), sends=(0, 40), x=(0, 100, 24, 0), genres=("synthpop",))
# low strings: 16' and 8', darker
P("low strings", "strings", "velvet", knobs=dict(register=CELLO, bright=50, chorus=70, rate=40), volume=104,
  amp=(66, 64, 127, 86), sends=(0, 35), x=(0, 80, 30, 0), genres=("synthpop", "lofi"))
# big strings: every register, wide
P("big strings", "pad", "velvet", knobs=dict(register=FULL, bright=72, chorus=80, rate=40), volume=100,
  amp=(76, 64, 127, 96), sends=(0, 45), x=(0, 120, 36, 0), genres=("synthpop",))
# a string bass: the 16' alone, a quick start, under the chords
P("string bass", "bass", "velvet", knobs=dict(register=BASS, bright=56, chorus=40, rate=40), mode="mono",
  volume=110, amp=(20, 64, 127, 50), sends=(0, 10), x=(0, 40, 20, 0), octave=-1, genres=("synthpop",))
# a hollow pad: square registers, dark
P("hollow pad", "pad", "velvet", knobs=dict(register=VIOLA, bright=44, chorus=80, rate=30), volume=104,
  amp=(80, 64, 127, 96), sends=(0, 45), x=(127, 100, 30, 0), genres=("lofi",))
# dark velvet: the violins behind a closed lid
P("dark velvet", "strings", "velvet", knobs=dict(register=VIOLIN, bright=28, chorus=76, rate=34), volume=110,
  amp=(80, 64, 127, 96), sends=(0, 40), x=(0, 90, 30, 0), genres=("lofi",))
# eighties: half square, a filter that opens, chords
P("eighties", "chord", "velvet", knobs=dict(register=FULL, bright=90, chorus=60, rate=50), volume=100,
  amp=(30, 64, 127, 70), fenv=(40, 80, 60, 70), filter=("lp24", 76, 20, 90, 64), sends=(10, 35),
  x=(60, 100, 36, 0), genres=("synthpop",))
# a warble: the ensemble deep and fast, seasick
P("warble", "pad", "velvet", knobs=dict(register=VIOLIN, bright=64, chorus=127, rate=100), volume=100,
  amp=(60, 64, 127, 86), sends=(0, 45), x=(0, 110, 50, 0), genres=("lofi",))
# pizzicato: short, the ensemble on
P("pizzicato", "pluck", "velvet", knobs=dict(register=VIOLA, bright=80, chorus=50, rate=40), volume=116,
  amp=(0, 50, 0, 40), sends=(0, 30), x=(0, 80, 20, 0), genres=("synthpop",))
