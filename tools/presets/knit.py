# SPDX-License-Identifier: MPL-2.0
"""knit's presets, the first set (2026-10-19): the everyday subtractive voice. wave 0 saw, 64 square, 127 a narrow
pulse; detune (one voice: a drift); voices 1..7; width. x = (osc 2 interval, 64 unison; its level; sub; noise).
The rules (docs/superpowers/specs/2026-10-19-sound-redesign.md): no resonant peak parked in 2-5 kHz unless it moves,
the lows balanced, reverb only where the role wants the room, a bass an octave down."""
from lib import P

# its sound in a new project: one plain saw, a little drift, darker, the filter opening a little on the note, dry
P("knit", "keys", "knit", knobs=dict(wave=0, detune=20, voices=1, width=0), volume=87,
  amp=(2, 84, 90, 64), fenv=(0, 76, 20, 64), filter=("lp24", 66, 5, 76, 64), genres=("synthpop",))
# the PWM string machine: a square whose width the LFO moves, two voices wide, tape on top
P("ribbon", "strings", "knit", knobs=dict(wave=64, detune=25, voices=2, width=80), volume=71,
  amp=(75, 80, 120, 90), fenv=(0, 64, 0, 64), filter=("lp12", 84, 5, 64, 64), sends=(0, 35),
  lfo=("wave", 40, 76, "synth free", 0, 1), fx1=("tape", 40, 60, 127), genres=("synthpop", "lofi"))
# the poly brass: the filter's swell, a vibrato coming in late
P("brass swell", "brass", "knit", knobs=dict(wave=0, detune=30, voices=2, width=40), volume=76,
  amp=(55, 80, 110, 64), fenv=(70, 85, 60, 70), filter=("lp24", 64, 20, 96, 64), sends=(0, 20),
  lfo=("wobble", 92, 74, 64, 20, 0), genres=("synthpop",))
# the Reese: three saws beating, the detune itself wandering, a sub under it
P("reese", "bass", "knit", knobs=dict(wave=0, detune=40, voices=3, width=30), mode="mono", volume=84,
  amp=(0, 64, 127, 40), fenv=(0, 64, 0, 40), filter=("lp24", 70, 10, 64, 40), lfo=("wave", 30, 80, "synth free", 1, 1),
  x=(64, 0, 50, 0), genres=("dnb",))
# a round sub: the filter nearly closed on a saw, the sine sub under it
P("round sub", "bass", "knit", knobs=dict(wave=0, detune=10, voices=1, width=0), mode="mono", volume=110,
  amp=(2, 80, 110, 40), fenv=(0, 60, 0, 40), filter=("lp24", 48, 5, 70, 30), x=(64, 0, 70, 0),
  genres=("dnb", "garage", "house"))
# the house chord stab: hollow (saw and square), five voices wide, an octave on top, a snappy filter, a delay throw
P("house stab", "stab", "knit", knobs=dict(wave=40, detune=22, voices=5, width=100), volume=69,
  amp=(0, 72, 0, 54), fenv=(0, 60, 0, 50), filter=("lp24", 84, 15, 100, 64), sends=(40, 25), x=(76, 50, 0, 0),
  genres=("house", "garage"))
# acid: the ladder near self-oscillation, swept hard and fast by the envelope (it moves: the rule's exception), a
# acid: the ladder near self-oscillation, swept hard and fast by the envelope (it moves: the rule's exception), a
# short slide on held notes only, driven hard after; the sub under it (a ladder high in resonance loses its lows)
P("acid", "bass", "knit", knobs=dict(wave=0, detune=0, voices=1, width=0), mode="legato", glide=16, volume=105,
  amp=(0, 90, 100, 30), fenv=(0, 64, 0, 40), filter=("ladder", 48, 100, 112, 50), fx1=("drive", 90, 100, 127),
  x=(64, 0, 40, 0), genres=("house",))
# a finger bass: a round saw, plucked by a short filter envelope, the sub under it
P("finger bass", "bass", "knit", knobs=dict(wave=20, detune=10, voices=1, width=0), mode="mono", volume=110,
  amp=(0, 80, 50, 30), fenv=(0, 50, 20, 40), filter=("lp24", 58, 5, 84, 40), x=(64, 0, 40, 0),
  genres=("garage", "house", "lofi"))
# a pluck: half saw, half square, the filter's snap
P("knit pluck", "pluck", "knit", knobs=dict(wave=30, detune=15, voices=1, width=0), volume=112,
  amp=(0, 72, 0, 60), fenv=(0, 60, 0, 50), filter=("lp24", 60, 15, 105, 64), sends=(30, 25),
  genres=("synthpop", "garage"))
# a soft lead: a square with its octave above, legato, a vibrato that fades in
P("soft lead", "lead", "knit", knobs=dict(wave=64, detune=15, voices=1, width=0), mode="legato", glide=20,
  volume=96, amp=(30, 70, 115, 60), fenv=(0, 64, 0, 64), filter=("lp12", 90, 5, 64, 64), sends=(20, 15),
  lfo=("wobble", 92, 74, 64, 20, 0), x=(76, 40, 0, 0), genres=("lofi", "synthpop"))
# a dusty pad: four saws wide, the filter breathing, tape
P("dusty pad", "pad", "knit", knobs=dict(wave=0, detune=35, voices=4, width=100), volume=81,
  amp=(85, 90, 120, 96), fenv=(0, 64, 0, 64), filter=("lp24", 70, 0, 64, 64), sends=(0, 45),
  lfo=("wave", 20, 72, "filter free", 0, 1), fx1=("tape", 60, 50, 127), genres=("lofi",))
# --- the Osc 2 page (2026-11-22): osc 2 on a wave of its own (lib.py osc2) ---
# a fifth lead: a saw with a triangle a fifth up, a few cents sharp: softer than a saw stack, legato, a late vibrato
P("fifth lead", "lead", "knit", knobs=dict(wave=0, detune=15, voices=1, width=0), mode="legato", glide=18,
  volume=96, amp=(4, 70, 115, 60), fenv=(0, 70, 30, 60), filter=("lp24", 76, 10, 80, 64), sends=(20, 15),
  lfo=("wobble", 92, 74, 64, 20, 0), x=(71, 100, 0, 0), osc2=("triangle", 0, 70), genres=("synthpop", "lofi"))
# a pulse bass: a saw, a thin pulse an octave under it for the weight (no sub sine), a short filter envelope
P("pulse bass", "bass", "knit", knobs=dict(wave=0, detune=10, voices=1, width=0), mode="mono", volume=100,
  amp=(0, 80, 100, 36), fenv=(0, 56, 10, 40), filter=("lp24", 56, 8, 80, 40), x=(52, 90, 0, 0),
  osc2=("pulse", 127, 64), genres=("house", "synthpop"))
# chime keys: a square-ish saw, a quiet sine two octaves up, the filter's short envelope
P("chime keys", "keys", "knit", knobs=dict(wave=40, detune=12, voices=1, width=0), volume=86,
  amp=(0, 84, 70, 64), fenv=(0, 54, 0, 50), filter=("lp24", 64, 5, 92, 90), sends=(20, 35), x=(88, 70, 0, 0),
  osc2=("sine", 0, 64), genres=("synthpop", "lofi"))
# a reed pad: two saws, osc 2 an octave up between square and pulse (its shape), nasal through a gentle filter
P("reed pad", "pad", "knit", knobs=dict(wave=20, detune=28, voices=2, width=90), volume=60,
  amp=(60, 90, 118, 92), fenv=(0, 64, 0, 64), filter=("lp12", 80, 10, 64, 64), sends=(0, 45),
  lfo=("wave", 22, 60, "filter free", 0, 1), x=(76, 90, 0, 0), osc2=("pulse", 96, 64), genres=("lofi", "synthpop"))
# --- the Mod page (2026-11-23): sync, the filter envelope on osc 2 (sweep), osc 2 on osc 1 (cross), the sub's type ---
# a sync lead: osc 2 a fifth up and synced, the filter envelope sweeping it an octave and a fifth on every note, legato
P("sync lead", "lead", "knit", knobs=dict(wave=0, detune=10, voices=1, width=0), mode="legato", glide=14,
  volume=88, amp=(2, 70, 110, 50), fenv=(0, 76, 30, 60), filter=("lp24", 92, 10, 70, 64), sends=(22, 18),
  x=(71, 110, 0, 0), osc2=("saw", 0, 64), mod=(True, 19, 0, "sine"), genres=("synthpop",))
# the 101's bass: a saw and a pulse locked to it (osc 2 synced in unison: one oscillator), the square sub an octave
# down (the low one sat under hearing, a bass already two octaves down), the ladder plucked by a short envelope, mono
P("rubber bass", "bass", "knit", knobs=dict(wave=0, detune=0, voices=1, width=0), mode="mono", volume=112,
  amp=(0, 76, 90, 30), fenv=(0, 54, 10, 40), filter=("ladder", 58, 26, 96, 50), x=(64, 90, 45, 0),
  osc2=("pulse", 100, 64), mod=(True, 0, 0, "square"), genres=("house", "synthpop"))
# poly sync keys: a synced saw an octave up, swept a little by the filter envelope on each note, two voices wide
P("poly sync", "keys", "knit", knobs=dict(wave=0, detune=18, voices=2, width=70), volume=74,
  amp=(0, 84, 80, 64), fenv=(0, 70, 20, 64), filter=("lp24", 80, 8, 80, 64), sends=(20, 30), x=(76, 90, 0, 0),
  osc2=("saw", 0, 64), mod=(True, 7, 0, "sine"), genres=("synthpop", "house"))
# a cross growl: a silent sine a fifth up moving osc 1's frequency (FM), a short filter envelope, the low pulse under it
P("cross growl", "bass", "knit", knobs=dict(wave=0, detune=0, voices=1, width=0), mode="mono", volume=96,
  amp=(0, 80, 100, 36), fenv=(0, 60, 20, 40), filter=("lp24", 60, 15, 84, 40), x=(71, 0, 50, 0),
  osc2=("sine", 0, 64), mod=(False, 0, 56, "low pulse"), genres=("dnb",))
# random steps on the filter, a sixteenth each (the LFO's random shape: the 101's sample and hold), a resonant square
P("step bleeps", "fx", "knit", knobs=dict(wave=64, detune=0, voices=1, width=0), volume=80,
  amp=(0, 70, 110, 50), fenv=(0, 64, 0, 40), filter=("lp24", 70, 60, 64, 64), sends=(30, 25),
  lfo=("wave", 5, 110, "filter", 0, 4), genres=("synthpop", "house"))
# --- analog (2026-11-23, the Levels page): each oscillator drifting, each note a little off, softer edges, saturation ---
# a warm poly: two saws a little apart, analog well up (every voice its own), a slow filter swell, the room
P("warm poly", "keys", "knit", knobs=dict(wave=10, detune=22, voices=2, width=60), volume=78,
  amp=(10, 84, 100, 70), fenv=(20, 76, 40, 64), filter=("lp24", 70, 10, 70, 64), sends=(10, 35), analog=90,
  genres=("synthpop", "lofi"))
# old strings: a PWM square, three voices, analog at the top, a gentle ensemble's width, slow in and out
P("old strings", "strings", "knit", knobs=dict(wave=64, detune=20, voices=3, width=100), volume=66,
  amp=(80, 90, 118, 96), fenv=(0, 64, 0, 64), filter=("lp12", 80, 0, 64, 64), sends=(0, 45),
  lfo=("wave", 34, 74, "synth free", 0, 1), analog=120, genres=("lofi", "synthpop"))
# an old lead: one saw and a sub, analog for the wander and the grit, legato glide, late vibrato
P("old lead", "lead", "knit", knobs=dict(wave=0, detune=0, voices=1, width=0), mode="legato", glide=20,
  volume=88, amp=(2, 70, 110, 50), fenv=(0, 72, 40, 60), filter=("ladder", 70, 50, 80, 64), sends=(20, 20),
  x=(64, 0, 40, 0), lfo=("wobble", 92, 74, 64, 30, 0), analog=100, genres=("synthpop",))
# --- dance music (2026-11-25): the Juno's chorus (insert juno: mode 0 I, 1 II, 2 I+II; mix; tone), rumble, grit ---
# the Juno pad: one square, the LFO on its width, analog, chorus II over it
P("juno pad", "pad", "knit", knobs=dict(wave=64, detune=10, voices=1, width=0), volume=82,
  amp=(70, 90, 120, 90), fenv=(0, 64, 0, 64), filter=("lp24", 74, 10, 64, 64), sends=(0, 35),
  lfo=("wave", 34, 70, "synth free", 0, 1), analog=60, fx2=("juno", 1, 70, 80), genres=("house", "synthpop"))
# the Juno's brass: one saw, the filter's swell, chorus I
P("juno brass", "brass", "knit", knobs=dict(wave=0, detune=10, voices=1, width=0), volume=80,
  amp=(40, 80, 115, 64), fenv=(45, 80, 60, 64), filter=("lp24", 60, 15, 90, 64), sends=(0, 20), analog=60,
  fx2=("juno", 0, 80, 70), genres=("synthpop", "house"))
# the Juno's bass: a pulse, the square sub, a short resonant pluck, a little chorus I
P("juno bass", "bass", "knit", knobs=dict(wave=90, detune=0, voices=1, width=0), mode="mono", volume=104,
  amp=(0, 74, 90, 30), fenv=(0, 52, 10, 40), filter=("lp24", 52, 30, 90, 40), x=(64, 0, 60, 0),
  mod=(False, 0, 0, "square"), analog=40, fx2=("juno", 0, 60, 40), genres=("house", "synthpop"))
# the Juno's strings: one saw, slow in and out, chorus II at the top
P("juno str", "strings", "knit", knobs=dict(wave=0, detune=15, voices=1, width=0), volume=74,
  amp=(70, 90, 120, 96), fenv=(0, 64, 0, 64), filter=("lp24", 82, 0, 64, 64), sends=(0, 45), analog=70,
  fx2=("juno", 1, 60, 100), genres=("synthpop", "lofi", "house"))
# the hoover: three wide squares, the LFO on their width, gliding, chorus II full and bright, driven
P("hoover", "lead", "knit", knobs=dict(wave=64, detune=45, voices=3, width=100), mode="legato", glide=34,
  volume=70, amp=(0, 70, 118, 60), fenv=(0, 64, 0, 64), filter=("lp24", 96, 10, 64, 64), sends=(0, 25),
  lfo=("wave", 74, 80, "synth free", 0, 1), fx1=("drive", 40, 80, 127), fx2=("juno", 1, 110, 127),
  genres=("techno", "dnb"))
# the deep house chord: hollow, two voices, the filter's soft pluck, analog, chorus II, a delay throw
P("deep stab", "stab", "knit", knobs=dict(wave=40, detune=20, voices=2, width=80), volume=70,
  amp=(0, 80, 40, 70), fenv=(0, 62, 10, 60), filter=("lp24", 56, 20, 96, 64), sends=(35, 30), analog=70,
  fx2=("juno", 1, 50, 70), genres=("house", "garage"))
# the dub techno chord: a short resonant saw into a long tape echo (1/8D), then the room
P("dub chord", "stab", "knit", knobs=dict(wave=20, detune=15, voices=1, width=0), volume=86,
  amp=(0, 66, 0, 50), fenv=(0, 56, 0, 45), filter=("lp24", 60, 40, 80, 64), sends=(0, 40), analog=50,
  fx2=("delay", 108, 75, 64, 70), genres=("techno", "house"))
# the techno rumble: a short low thump under the kick (play it with the kick), the rumble insert's dark tail after it
P("rumble", "bass", "knit", knobs=dict(wave=0, detune=0, voices=1, width=0), mode="mono", volume=86,
  amp=(0, 58, 0, 40), fenv=(0, 64, 0, 40), filter=("lp24", 34, 0, 64, 40), x=(64, 0, 60, 0),
  fx2=("rumble", 90, 30, 100), genres=("techno",))
# a techno stab: two saws, a short pluck through the RAT
P("rat stab", "stab", "knit", knobs=dict(wave=0, detune=20, voices=2, width=60), volume=96,
  amp=(0, 64, 0, 50), fenv=(0, 56, 0, 40), filter=("lp24", 66, 30, 90, 64), sends=(30, 25),
  fx2=("rat", 60, 60, 95, 127), genres=("techno",))
# a hypnotic line: a square on the ladder, half open, a slow phaser turning it
P("hypnotic", "pluck", "knit", knobs=dict(wave=64, detune=0, voices=1, width=0), mode="mono", volume=120,
  amp=(0, 72, 40, 40), fenv=(0, 60, 0, 40), filter=("ladder", 66, 70, 80, 64), sends=(40, 20), analog=40,
  fx2=("phaser", 50, 90, 40, 127, 60), genres=("techno",))
# a noise burst: noise through a resonant high-pass and the RAT, into the room
P("noise burst", "fx", "knit", knobs=dict(wave=0, detune=0, voices=1, width=0), volume=90,
  amp=(0, 60, 0, 50), fenv=(0, 64, 0, 64), filter=("hp12", 80, 40, 64, 64), sends=(0, 40), x=(64, 0, 0, 127),
  fx2=("rat", 90, 40, 70, 127), genres=("techno", "dnb"))
# a neuro bass: osc 2 on osc 1's frequency, folded, an autowah your playing opens
P("neuro", "bass", "knit", knobs=dict(wave=0, detune=0, voices=1, width=0), mode="mono", volume=92,
  amp=(0, 80, 110, 36), fenv=(0, 64, 0, 40), filter=("lp24", 90, 10, 64, 40), x=(71, 0, 60, 0),
  osc2=("sine", 0, 64), mod=(False, 0, 70, "sine"), fx1=("fold", 60, 64, 127), fx2=("autowah", 100, 90, 40, 127),
  genres=("dnb",))
# a dirty Reese: three saws beating, analog, driven, a slow phaser sweeping through them, the sub under it
P("dirty reese", "bass", "knit", knobs=dict(wave=0, detune=55, voices=3, width=20), mode="mono", volume=84,
  amp=(0, 64, 127, 40), fenv=(0, 64, 0, 40), filter=("lp24", 66, 10, 64, 40), x=(64, 0, 50, 0), analog=60,
  fx1=("drive", 50, 80, 127), fx2=("phaser", 50, 80, 35, 127, 50), genres=("dnb", "techno"))
# a liquid pad: three soft saws wide, analog, chorus I, the room
P("liquid pad", "pad", "knit", knobs=dict(wave=0, detune=30, voices=3, width=100), volume=74,
  amp=(80, 90, 120, 100), fenv=(0, 64, 0, 64), filter=("lp24", 72, 5, 64, 64), sends=(0, 55), analog=70,
  fx2=("juno", 0, 60, 80), genres=("dnb", "house"))
# the rave stab: three wide squares and their octave, a snappy filter, chorus II bright
P("rave stab", "stab", "knit", knobs=dict(wave=64, detune=30, voices=3, width=100), volume=68,
  amp=(0, 70, 0, 60), fenv=(0, 64, 0, 50), filter=("lp24", 80, 20, 90, 64), sends=(25, 30), x=(76, 60, 0, 0),
  fx2=("juno", 1, 100, 100), genres=("dnb", "techno"))
# a deep house bass: one saw on the ladder, analog well up, the sub, a little drive
P("deep bass", "bass", "knit", knobs=dict(wave=0, detune=0, voices=1, width=0), mode="mono", volume=104,
  amp=(2, 80, 100, 40), fenv=(0, 60, 10, 40), filter=("ladder", 50, 30, 70, 40), x=(64, 0, 50, 0), analog=80,
  fx1=("drive", 40, 64, 127), genres=("house",))
# a gliding sub: the sine sub alone (the saw filtered away), driven for its harmonics, legato
P("sub glide", "bass", "knit", knobs=dict(wave=0, detune=0, voices=1, width=0), mode="legato", glide=25,
  volume=110, amp=(2, 80, 120, 40), fenv=(0, 64, 0, 40), filter=("lp24", 24, 0, 64, 40), x=(64, 0, 127, 0),
  fx1=("drive", 50, 64, 127), genres=("dnb",))

# --- the mod envelopes (2026-11-28) ---
# a zap bass: an octave of pitch falling in a blink on every note, the 80s drum machine's zap under a saw
P("zap bass", "bass", "knit", knobs=dict(wave=0, detune=10, voices=1, width=0), mode="mono", volume=96,
  amp=(0, 78, 90, 36), fenv=(0, 60, 20, 40), filter=("lp24", 72, 10, 80, 40),
  menv=(("pitch", 0, 56, 0, 40, 127, False),), genres=("synthpop", "house"))
# a sync loop: osc 2 synced and its interval swept again and again while the key is held
P("sync loop", "lead", "knit", knobs=dict(wave=0, detune=10, voices=1, width=0), mode="legato", glide=10,
  volume=84, amp=(0, 70, 120, 40), fenv=(0, 64, 0, 64), filter=("lp24", 92, 10, 64, 64), sends=(25, 10),
  x=(64, 110, 0, 0), osc2=("saw", 0, 64), mod=(True, 0, 0, "sine"),
  menv=(("X1", 0, 72, 0, 50, 110, True),), genres=("synthpop",))
