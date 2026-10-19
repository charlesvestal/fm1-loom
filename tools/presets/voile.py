# SPDX-License-Identifier: GPL-3.0-only
"""voile's presets (2026-10-19): Braids' voices. knobs: model (0 vosim, 1 vowel, 2 choir, 3 whisper), timbre,
color, breath; per model (dsp/braids.h): vosim timbre and color the two formants; vowel (Braids' speech) timbre the
vowel, color the formants' shift, breath the consonant's length; choir timbre the vowel (a e i o u), color the voice
(bass, tenor, countertenor, alto, soprano); whisper timbre the peaks' sharpness, breath 127 all noise."""
from lib import P

VOSIM, VOWEL, CHOIR, WHISPER = range(4)

# its sound in a new project: an "aah" choir, altos, a breath of air, swelling in
P("voile", "voice", "voile", knobs=dict(model=CHOIR, timbre=0, color=90, breath=20), volume=104,
  amp=(80, 90, 120, 90), fenv=(0, 64, 0, 64), filter=("lp12", 78, 0, 64, 64), sends=(0, 30),
  genres=("synthpop", "garage"))
# an "ooh" choir: tenors, darker
P("ooh choir", "pad", "voile", knobs=dict(model=CHOIR, timbre=100, color=30, breath=30), volume=106,
  amp=(90, 90, 120, 96), fenv=(0, 64, 0, 64), filter=("lp12", 76, 0, 64, 64), sends=(0, 30), genres=("lofi",))
# a soprano line: "ee", a vibrato coming in
P("soprano", "voice", "voile", knobs=dict(model=CHOIR, timbre=32, color=120, breath=15), mode="legato", glide=10,
  volume=122, amp=(50, 80, 120, 80), fenv=(0, 64, 0, 64), filter=("lp12", 74, 0, 64, 64), sends=(10, 28),
  lfo=("wobble", 92, 74, 64, 20, 0), genres=("synthpop",))
# a vocal lead: Braids' speech, the vowel moving slowly
P("vocal lead", "lead", "voile", knobs=dict(model=VOWEL, timbre=50, color=64, breath=20), mode="legato",
  glide=10, volume=110, amp=(20, 70, 115, 60), fenv=(0, 64, 0, 64), filter=("lp12", 88, 0, 64, 64),
  sends=(20, 15), lfo=("wave", 26, 84, "synth free", 1, 1), genres=("garage", "house"))
# a vocal chop: a short sung stab, the garage way, with a delay throw
P("vocal chop", "stab", "voile", knobs=dict(model=VOWEL, timbre=30, color=70, breath=40), volume=124,
  amp=(0, 66, 0, 50), fenv=(0, 64, 0, 64), filter=("lp12", 80, 0, 64, 64), sends=(35, 15),
  genres=("garage", "house"))
# a VOSIM bass: two low formants, buzzing, an octave down
P("vosim bass", "bass", "voile", knobs=dict(model=VOSIM, timbre=64, color=96, breath=0), mode="mono", volume=104,
  amp=(0, 80, 90, 40), fenv=(0, 64, 0, 64), filter=("lp24", 84, 10, 64, 40), genres=("synthpop",))
# a whisper pad: breath with a peak in it, wide and dusty
P("whisper pad", "pad", "voile", knobs=dict(model=WHISPER, timbre=60, color=64, breath=100), volume=110,
  amp=(90, 90, 120, 100), fenv=(0, 64, 0, 64), filter=("lp12", 84, 0, 64, 64), sends=(10, 30),
  char=("tape", 40, 60, 127), genres=("lofi",))
