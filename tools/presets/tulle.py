# SPDX-License-Identifier: MPL-2.0
"""tulle's presets, the first set (2026-11-23): grains of the factory samples. knobs, the playhead: sample (smp(name)),
position (where in it), spray (each grain's position and start scattered: regular at 0, random up), scan (the
position moving on its own, 0 frozen). x = (size, density, pitch, width), the grains: a grain's length (10 .. 500 ms),
grains a second (2 .. 60), each grain detuned at random (up to an octave), the grains across the stereo field."""
from lib import P, smp

# its sound in a new project: a cloud of the singing bowl (its texture), long soft grains, slowly moving through it
P("tulle", "pad", "tulle", knobs=dict(sample=smp("bowl"), position=40, spray=30, scan=12), volume=100,
  amp=(60, 90, 118, 90), filter=("lp24", 110, 0, 64, 64), sends=(0, 40), x=(80, 70, 0, 90),
  genres=("lofi", "synthpop"))
# frozen piano: one moment of the piano held still, short dense grains, a little detune
P("frozen keys", "pad", "tulle", knobs=dict(sample=smp("piano"), position=20, spray=8, scan=0), volume=104,
  amp=(40, 90, 118, 96), filter=("lp24", 100, 0, 64, 64), sends=(0, 45), x=(60, 100, 10, 100), genres=("lofi",))
# shimmer: the flute's grains detuned up to an octave, wide, the reverb high
P("flute glow", "pad", "tulle", grain=(0, 45), knobs=dict(sample=smp("flute"), position=50, spray=40, scan=20), volume=96,
  amp=(70, 90, 118, 100), filter=("lp24", 116, 0, 64, 64), sends=(10, 60), x=(70, 90, 90, 127),
  genres=("lofi", "synthpop"))
# brass haze: the brass smeared, long grains scanning slowly, filtered dark
P("brass haze", "pad", "tulle", knobs=dict(sample=smp("brass"), position=30, spray=50, scan=20), volume=100,
  amp=(80, 90, 118, 96), filter=("lp24", 84, 5, 64, 64), sends=(0, 45), x=(90, 60, 15, 80), genres=("lofi",))
# vinyl dust: the crackle as a bed, sparse short grains all over it
P("vinyl dust", "fx", "tulle", knobs=dict(sample=smp("vinyl"), position=50, spray=127, scan=40), volume=127,
  amp=(20, 90, 118, 80), filter=("lp24", 100, 0, 64, 64), sends=(0, 30), x=(40, 60, 0, 127), genres=("lofi",))
# a stutter: the orchestra hit in short grains, fast, held in one place
P("hit stutter", "fx", "tulle", grain=(110, 0), knobs=dict(sample=smp("orch hit"), position=10, spray=5, scan=0), volume=96,
  amp=(0, 80, 110, 60), filter=("lp24", 110, 0, 64, 64), sends=(20, 25), x=(35, 110, 0, 60), genres=("house",))
# a thick cloud: long grains, dense (about a dozen at once from the shared pool), scattered in place and time, wide
P("thick cloud", "pad", "tulle", knobs=dict(sample=smp("strings"), position=45, spray=90, scan=16), volume=96,
  amp=(80, 90, 118, 100), filter=("lp24", 104, 0, 64, 64), sends=(0, 45), x=(110, 110, 8, 127), genres=("lofi",))
# backwards: the bowl's grains mostly reversed, swelling into each other, long and wide
P("backwards", "pad", "tulle", knobs=dict(sample=smp("bowl"), position=30, spray=60, scan=10), volume=100,
  amp=(60, 90, 118, 100), filter=("lp24", 110, 0, 64, 64), sends=(0, 50), x=(100, 70, 0, 110), grain=(0, 110),
  genres=("lofi",))
# bowl plucks: short sharp grains of the bowl, scattered in time, a glassy rain
P("bowl rain", "fx", "tulle", knobs=dict(sample=smp("bowl"), position=50, spray=110, scan=20), volume=100,
  amp=(0, 90, 118, 80), filter=("lp24", 116, 0, 64, 64), sends=(15, 45), x=(40, 80, 30, 127), grain=(115, 30),
  genres=("lofi", "synthpop"))
