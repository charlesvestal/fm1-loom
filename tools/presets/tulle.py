# SPDX-License-Identifier: MPL-2.0
"""tulle's presets, the first set (2026-11-23): grains of the factory samples. knobs: sample (smp(name)), position
(where in it), size (a grain, 10 .. 500 ms), density (grains a second, 2 .. 60). x = (spray, scan, pitch, width): each
grain's position jittered, the position moving on its own (0 frozen), each grain detuned at random (up to an octave),
the grains across the stereo field."""
from lib import P, smp

# its sound in a new project: a cloud of the strings, long soft grains, slowly moving through them
P("tulle", "pad", "tulle", knobs=dict(sample=smp("strings"), position=40, size=80, density=70), volume=100,
  amp=(60, 90, 118, 90), filter=("lp24", 110, 0, 64, 64), sends=(0, 40), x=(30, 12, 0, 90),
  genres=("lofi", "synthpop"))
# frozen piano: one moment of the piano held still, short dense grains, a little detune
P("frozen keys", "pad", "tulle", knobs=dict(sample=smp("piano"), position=20, size=60, density=100), volume=104,
  amp=(40, 90, 118, 96), filter=("lp24", 100, 0, 64, 64), sends=(0, 45), x=(8, 0, 10, 100), genres=("lofi",))
# shimmer: the flute's grains detuned up to an octave, wide, the reverb high
P("flute glow", "pad", "tulle", knobs=dict(sample=smp("flute"), position=50, size=70, density=90), volume=96,
  amp=(70, 90, 118, 100), filter=("lp24", 116, 0, 64, 64), sends=(10, 60), x=(40, 20, 90, 127),
  genres=("lofi", "synthpop"))
# brass haze: the brass smeared, long grains scanning slowly, filtered dark
P("brass haze", "pad", "tulle", knobs=dict(sample=smp("brass"), position=30, size=90, density=60), volume=100,
  amp=(80, 90, 118, 96), filter=("lp24", 84, 5, 64, 64), sends=(0, 45), x=(50, 20, 15, 80), genres=("lofi",))
# vinyl dust: the crackle as a bed, sparse short grains all over it
P("vinyl dust", "fx", "tulle", knobs=dict(sample=smp("vinyl"), position=50, size=40, density=60), volume=127,
  amp=(20, 90, 118, 80), filter=("lp24", 100, 0, 64, 64), sends=(0, 30), x=(127, 40, 0, 127), genres=("lofi",))
# a stutter: the orchestra hit in short grains, fast, held in one place
P("hit stutter", "fx", "tulle", knobs=dict(sample=smp("orch hit"), position=10, size=35, density=110), volume=96,
  amp=(0, 80, 110, 60), filter=("lp24", 110, 0, 64, 64), sends=(20, 25), x=(5, 0, 0, 60), genres=("house",))
# a thick cloud: long grains, dense (about a dozen at once from the shared pool), scattered in place and time, wide
P("thick cloud", "pad", "tulle", knobs=dict(sample=smp("strings"), position=45, size=110, density=110), volume=96,
  amp=(80, 90, 118, 100), filter=("lp24", 104, 0, 64, 64), sends=(0, 45), x=(90, 16, 8, 127), genres=("lofi",))
