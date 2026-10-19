# SPDX-License-Identifier: GPL-3.0-only
"""rayon's presets (2026-10-19): two-operator FM the DX's way. knobs: bright (the index; velocity sets how much of it a
note gets), ratio (ratio(): the modulator's), edge (feedback), tine (a 14:1 partial dying in ~0.25 s, by velocity).
x = (fall, floor, keyscale, -): the index falling from the note's start (0 held; 20 ms .. 4 s), what remains, and
how much it falls up the keyboard. The rules as knit's (docs/superpowers/specs/2026-10-19-sound-redesign.md)."""
from lib import P

RATIOS = [0.5, 0.6666667, 0.75, 1.25, 1.3333334, 1.4142135, 1.5, 1.75, 2.0, 2.5, 3.0, 3.5, 4.0, 5.0, 7.0, 8.0]


def ratio(q):
    """the ratio knob's value for the modulator at q:1 (1 = an exact 1:1; else one of RATIOS)"""
    if q == 1:
        return 0
    k = RATIOS.index(q)
    return round((51.0 + (k + 0.5) * 49.0 / 15.99) * 1.27)


# its sound in a new project: the DX e-piano, 1:1 with the tine's bark, its brightness falling as a tine's does
P("rayon", "keys", "rayon", knobs=dict(bright=58, ratio=ratio(1), edge=0, tine=70), volume=87,
  amp=(0, 94, 0, 70), fenv=(0, 64, 0, 64), filter=("lp12", 110, 0, 64, 64), sends=(0, 18), x=(62, 30, 50, 0),
  genres=("synthpop", "lofi", "garage"))
# a mellow electric piano: less bark, a slower fall, a tremolo, tape
P("mellow ep", "keys", "rayon", knobs=dict(bright=46, ratio=ratio(1), edge=0, tine=36), volume=92,
  amp=(0, 96, 0, 76), fenv=(0, 64, 0, 64), filter=("lp12", 96, 0, 64, 64), sends=(0, 22), x=(80, 35, 50, 0),
  lfo=("wobble", 70, 64, 82, 64, 0), char=("tape", 35, 70, 127), genres=("lofi", "house"))
# the DX bass: a half-ratio modulator, plucked
P("dx bass", "bass", "rayon", knobs=dict(bright=80, ratio=ratio(0.5), edge=30, tine=0), mode="mono", volume=104,
  amp=(0, 80, 70, 40), fenv=(0, 64, 0, 40), filter=("lp24", 90, 5, 64, 40), x=(35, 20, 30, 0),
  genres=("synthpop", "house"))
# the slap: a bright 1:1 snapping shut in ~70 ms, the tine for the pop, a tilt up for the click
P("slap", "bass", "rayon", knobs=dict(bright=110, ratio=ratio(1), edge=30, tine=40), mode="mono", volume=127,
  amp=(0, 70, 55, 25), fenv=(0, 30, 0, 30), filter=("lp12", 110, 0, 100, 64), x=(28, 55, 40, 0),
  char=("tilt", 50, 106, 127), genres=("synthpop", "garage", "house"))
# a glass bell: 3.5:1, ringing long, in the room
P("glass bell", "bell", "rayon", knobs=dict(bright=60, ratio=ratio(3.5), edge=0, tine=30), volume=100,
  amp=(0, 100, 0, 90), fenv=(0, 64, 0, 64), filter=("lp12", 112, 0, 64, 64), sends=(0, 40), x=(90, 20, 60, 0),
  genres=("synthpop", "lofi"))
# a marimba: 4:1, the modulator gone almost at once, a short wooden note
P("marimba", "pluck", "rayon", knobs=dict(bright=50, ratio=ratio(4.0), edge=0, tine=0), volume=112,
  amp=(0, 76, 0, 60), fenv=(0, 64, 0, 64), filter=("lp12", 104, 0, 64, 64), sends=(0, 20), x=(30, 0, 40, 0),
  genres=("lofi", "house"))
# a tubular bell: root 2, long, its brightness slow to go
P("tubular", "bell", "rayon", knobs=dict(bright=55, ratio=ratio(1.4142135), edge=0, tine=0), volume=100,
  amp=(0, 104, 0, 96), fenv=(0, 64, 0, 64), filter=("lp12", 108, 0, 64, 64), sends=(0, 45), x=(100, 30, 50, 0), octave=1,
  genres=("synthpop",))
# FM brass: 1:1 with feedback for the saw, the filter's swell, a late vibrato
P("fm brass", "brass", "rayon", knobs=dict(bright=70, ratio=ratio(1), edge=60, tine=0), volume=90,
  amp=(50, 80, 110, 60), fenv=(60, 80, 70, 60), filter=("lp24", 80, 5, 90, 64), sends=(0, 18), x=(70, 60, 40, 0),
  lfo=("wobble", 92, 74, 64, 20, 0), genres=("synthpop",))
# a glass pad: 2:1 still, the filter breathing, wide in the room, tape
P("glass pad", "pad", "rayon", knobs=dict(bright=35, ratio=ratio(2.0), edge=0, tine=0), volume=90,
  amp=(90, 90, 120, 100), fenv=(0, 64, 0, 64), filter=("lp12", 90, 0, 64, 64), sends=(0, 50), x=(0, 0, 70, 0),
  lfo=("wave", 20, 74, "filter free", 0, 1), char=("tape", 40, 60, 127), genres=("lofi",))
# toy keys: 3:1 with the tine, short and bright, a music box gone a little wrong
P("toy keys", "keys", "rayon", knobs=dict(bright=45, ratio=ratio(3.0), edge=0, tine=50), volume=91,
  amp=(0, 82, 0, 60), fenv=(0, 64, 0, 64), filter=("lp12", 108, 0, 64, 64), sends=(10, 25), x=(40, 10, 50, 0),
  genres=("lofi",))
