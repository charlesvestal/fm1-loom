# SPDX-License-Identifier: MPL-2.0
"""lace's presets, the first set (2026-11-23): a modal resonator. knobs: material (BAR .. GONG: the modes' ratios and
how long they ring), bright (the higher modes' share), damp (how fast they die, the higher sooner), position (where it
is struck: a comb on the modes). x = (exciter, stretch, spread, hardness): MALLET / NOISE / BOW, the modes' spacing
(64 as the material has it), the modes across the stereo field, the mallet's hardness (the noise's color, the bow's
pressure). Its own sound rings in the amp's release: a sustain at the top, a long release. At most four lace voices
sound at once."""
from lib import P

BAR, MARIMBA, CHURCH, GLASS, PLATE, STRING, WOOD, GONG, BOWL = range(9)
MALLET, NOISE, BOW = range(3)

# its sound in a new project: a vibraphone-like bar, a soft mallet, ringing
P("lace", "bell", "lace", knobs=dict(material=BAR, bright=64, damp=50, position=30), volume=110,
  amp=(0, 64, 127, 90), sends=(0, 30), x=(MALLET, 64, 64, 80), genres=("lofi", "synthpop"))
# rosewood: the tuned bar of a marimba, wooden and short
P("rosewood", "keys", "lace", knobs=dict(material=MARIMBA, bright=56, damp=70, position=40), volume=116,
  amp=(0, 64, 127, 70), sends=(0, 20), x=(MALLET, 64, 50, 64), genres=("house", "lofi"))
# a kalimba: a bar, its modes drawn in (stretch down), a hard thumb, close
P("kalimba", "pluck", "lace", knobs=dict(material=BAR, bright=70, damp=66, position=20), volume=116,
  amp=(0, 64, 127, 64), sends=(0, 25), x=(MALLET, 30, 40, 96), genres=("lofi",))
# a church bell: the hum two octaves under, long, in the room
P("church bell", "bell", "lace", knobs=dict(material=CHURCH, bright=70, damp=30, position=25), volume=104,
  amp=(0, 64, 127, 110), sends=(0, 40), x=(MALLET, 64, 80, 90), genres=("lofi",))
# a glass harp: glass, bowed, slow in, wide
P("glass harp", "pad", "lace", knobs=dict(material=GLASS, bright=70, damp=20, position=30), volume=110,
  amp=(70, 64, 127, 90), sends=(0, 45), x=(BOW, 64, 90, 60), genres=("lofi", "synthpop"))
# air glass: glass breathed on (noise), a hollow pad
P("air glass", "pad", "lace", knobs=dict(material=GLASS, bright=60, damp=25, position=40), volume=104,
  amp=(80, 64, 127, 96), sends=(0, 50), x=(NOISE, 64, 100, 40), genres=("lofi",))
# a harp: a plucked string, soft, near the end
P("harp", "pluck", "lace", knobs=dict(material=STRING, bright=70, damp=56, position=16), volume=116,
  amp=(0, 64, 127, 80), sends=(0, 30), x=(MALLET, 66, 70, 70), genres=("lofi", "synthpop"))
# a bowed string: the string model bowed, a vibrato coming in
P("bowed wire", "strings", "lace", knobs=dict(material=STRING, bright=80, damp=40, position=24), mode="mono",
  volume=104, amp=(50, 64, 127, 70), sends=(0, 30), x=(BOW, 64, 40, 80), lfo=("wobble", 92, 74, 64, 20, 0),
  genres=("synthpop",))
# a steel plate: hard and bright, struck near the edge
P("steel plate", "fx", "lace", knobs=dict(material=PLATE, bright=96, damp=36, position=12), volume=100,
  amp=(0, 64, 127, 100), sends=(10, 40), x=(MALLET, 70, 110, 120), genres=("house",))
# a wood block: short, dry, high
P("wood block", "drum", "lace", knobs=dict(material=WOOD, bright=80, damp=60, position=30), volume=116,
  amp=(0, 64, 127, 40), x=(MALLET, 64, 30, 110), octave=1, genres=("house", "lofi"))
# a gong: soft beater, long, its modes crowded
P("gong", "fx", "lace", knobs=dict(material=GONG, bright=50, damp=24, position=40), volume=110,
  amp=(0, 64, 127, 112), sends=(0, 45), x=(MALLET, 64, 100, 30), octave=-1, genres=("lofi",))
# a storm bell: the church bell blown with noise, a dark wash
P("storm bell", "fx", "lace", knobs=dict(material=CHURCH, bright=40, damp=36, position=30), volume=104,
  amp=(60, 64, 127, 100), sends=(0, 50), x=(NOISE, 64, 110, 50), genres=("lofi",))
# a prayer bowl: the singing bowl struck soft, its pairs beating as it rings
P("prayer bowl", "bell", "lace", knobs=dict(material=BOWL, bright=50, damp=20, position=30), volume=110,
  amp=(0, 64, 127, 118), sends=(0, 40), x=(MALLET, 64, 90, 40), genres=("lofi",))
# a rubbed bowl: the singing bowl circled with its stick (the bow), swelling, wide
P("rubbed bowl", "pad", "lace", knobs=dict(material=BOWL, bright=56, damp=16, position=40), volume=106,
  amp=(80, 64, 127, 100), sends=(0, 45), x=(BOW, 64, 100, 50), genres=("lofi",))
