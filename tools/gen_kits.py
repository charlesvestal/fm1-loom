#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""LOOM-8's factory kits after Loom: each kit eleven keys, written here in each model's own terms
(MACHINEDRUM's 0..100, Sophie's and digisophie's controls), into the GKIT table app/pages.c and app/ui.c use (kit_factory).
  tools/gen_kits.py OUT.h           (run by the build: app/fm1.toml gen; the host tests run it too)
  tools/gen_kits.py --xy NAME       a kit as the host simulator's knob lines (track 1), for renders and checks
  tools/gen_kits.py --names         the kits' names, in order

The keys go as the default kit's: 1 kick, 2 snare, 3 closed hat, 4 open hat (3 and 4 in choke group 1), 5 clap,
6 low tom, 7 high tom, 8 percussion, 9 shaker or noise, 10 cymbal, 11 bell or FM. Pitched keys sit in C (the
kit's kick on C or G, the toms on G and C, the bells on C or G). Volumes balance the keys by role: each key alone,
its first 50 ms against Loom's kick (the kick level with it, the snare and clap 2-3 dB under, the toms 4, the
percussion 8, the bells 9, the hats 10-12, the noise 14), measured in the host simulator."""
import math
import sys

# drumline.h's model numbers (saved by number: never renumber)
M = dict(kick=0, snare=1, hat=2, clap=3, shaker=4, tamb=5, string=6, conga=7, tom=8, fm=9, sample=10,
         xkick=11, xsnare=12, xhat=13, xclap=14, xtom=15, xrim=16, xfm=17, xnoise=18, clang=19, sophie=20,
         kick909=21, snare909=22, tom909=23, rim909=24, clap909=25,   # (9W9's circuits: dsp/d909.c)
         ch909=26, oh909=27, ride909=28, crash909=29)                  # (9W9's sampler: the resource region's)
PM_ONESHOT, PM_CHOKE1 = 0, 3
k127 = lambda v: max(0, min(127, round(v * 1.27)))                     # MACHINEDRUM's 0..100 on a knob


def length(sec):                                                       # sophie's and clang's LENGTH: 30 ms .. 4 s
    return max(0, min(127, round(127 * math.log(sec / 0.03) / math.log(4 / 0.03))))


def x(model, pitch=None, decay=None, tone=None, sweep=None, noise=None, fm=None, drive=18, **key):
    """an x model in MACHINEDRUM's terms (pitch a note; the rest 0..100); the knobs the model shows (drumline.c XMAP)"""
    if model in ("xclap", "xnoise"):
        knobs = [k127(tone), k127(decay), k127(drive)]
    elif model == "xrim":
        knobs = [pitch, k127(decay), k127(tone), k127(sweep), k127(noise), k127(drive)]
    else:
        knobs = [pitch, k127(decay), k127(tone), k127(sweep), k127(noise), k127(fm), k127(drive)]
    return dict(model=M[model], knobs=knobs, **key)


SOPHIE_M = ["fuse", "stack", "split", "shard", "pipe", "boom", "kick"]


def sophie(model, note, decay, color, metal, feedback, sweep, crush, **key):
    """sophie in Sophie's terms: decay in seconds, sweep -100..100, the rest 0..100"""
    return dict(model=M["sophie"], knobs=[note, length(decay), k127(color), k127(metal), SOPHIE_M.index(model),
                                          k127(feedback), max(0, min(127, round(64 + sweep * 0.63))), k127(crush)], **key)


CLANG_M = ["fuse", "boom", "pipe", "shard"]


def clang(model, note, decay, color, metal, sweep=0, feedback=0, fold=0, **key):
    """clang in digisophie's terms: decay in seconds, color / metal / feedback / fold 0..127, sweep -64..63"""
    return dict(model=M["clang"], knobs=[note, length(decay), color, metal, CLANG_M.index(model), 64 + sweep, feedback,
                                         fold], **key)


def n9(model, *knobs, **key):
    """a 909 model (9W9's circuits, dsp/d909.c), its knobs as the model shows them: kick pitch decay attack sweep depth
    drive shape; snare pitch decay snappy tone drive shape; tom pitch decay attack size drive shape; rim pitch drive shape;
    clap tone tail drive shape; the hat, open hat, ride and crash pitch decay drive shape"""
    return dict(model=M[model], knobs=list(knobs), **key)


def loom(model, k1, k2, k3, k4, **key):
    """one of LOOM-8's own models (kick, snare, hat, clap, shaker, conga, tom, fm ...), its four knobs as they are"""
    return dict(model=M[model], knobs=[k1, k2, k3, k4], **key)


SMP = dict(kick909=0, snare909=1, hat909=2, clap909=3)                # the factory bank's first four


def smp(sample, pitch=64, **key):
    """a sample of the factory bank on the sample model (pitch: 64 its own, 40..88 = +-24 semitones)"""
    return dict(model=M["sample"], knobs=[SMP[sample], pitch, 0, 0], **key)


def hat(k, **key):                                                     # the closed and open hats choke each other
    return dict(k, mode=PM_CHOKE1, **key)


S_STRING = sophie("pipe", 52, 0.5, 30, 30, 10, 0, 0)               # (a warm, struck string)
S_CONGA = sophie("boom", 55, 0.25, 25, 20, 5, 30, 0)
X_BELL = x("xfm", 72, 30, 76, 30, 0, 35)                      # (C5; ratio 3.5, its brightness gone in ~0.1 s)
X_PLUCK = x("xfm", 55, 22, 40, 15, 0, 45)                     # (G3; ratio 1: a plucked FM string)
X_CONGA = x("xtom", 60, 20, 70, 25, 0, 0)
X_SHAKER = x("xnoise", tone=100, decay=5)
# the fm key: a short, woody FM blip, low and little metal (a bell was too tonal and metallic for a kit)
X_BLIP = x("xfm", 50, 12, 60, 10, 0, 25)                      # (D3; ratio 2, ~0.1 s)
SO_BLIP = sophie("boom", 50, 0.12, 40, 30, 5, 20, 0)
CL_BLIP = clang("boom", 50, 0.12, 40, 40, 20, 5)
# the kits: name (11 characters at most), then the eleven keys; db = the key's volume, pan 0..127 (64 the middle)
KITS = [
    ("Nylon", [                                   # MACHINEDRUM's, as you hearted them
        x("xkick", 36, 50, 40, 70, 10, 12, drive=45, db=-2),
        x("xsnare", 53, 30, 85, 100, 60, 12, db=8),
        hat(x("xhat", 82, 3, 76, 22, 92, 12), db=13, pan=56),
        hat(x("xhat", 74, 30, 40, 22, 92, 12), db=6, pan=72),
        x("xclap", tone=76, decay=27, db=11),
        x("xtom", 43, 34, 76, 22, 10, 12, db=1, pan=50),
        x("xtom", 55, 30, 76, 22, 10, 12, db=2, pan=78),
        x("xrim", 63, 14, 76, 22, 10, db=4, pan=80),
        x("xnoise", tone=90, decay=12, db=3, pan=44),
        x("xhat", 91, 65, 76, 22, 92, 12, db=-1, pan=40),
        x("xfm", 79, 18, 76, 22, 10, 30, db=0, pan=84),
    ]),
    ("Elastane", [                              # Sophie's, rubbery: a bouncing kick, crushed ticks, a latex snap, toms that boing
        sophie("kick", 31, 0.5, 15, 8, 6, 90, 0, db=-2),
        sophie("stack", 43, 0.25, 40, 45, 40, 40, 55, db=0),
        hat(sophie("shard", 44, 0.05, 70, 45, 30, 0, 50), db=-5, pan=56),
        hat(sophie("shard", 46, 0.35, 70, 55, 50, -4, 30), db=-10, pan=72),
        sophie("split", 42, 0.18, 50, 35, 40, 30, 30, db=-1),           # (a latex snap)
        sophie("boom", 43, 0.4, 15, 15, 10, 85, 0, db=-5, pan=50),
        sophie("boom", 50, 0.35, 15, 15, 10, 85, 0, db=-5, pan=78),
        CL_BLIP, CL_BLIP, CL_BLIP, CL_BLIP,                          # (keys 8..11: SLOTS)
]),
    ("Wire", [                               # digisophie's, hard and metallic: short, bright, folded
        clang("fuse", 33, 0.18, 50, 30, 30, 10, 40, db=7),
        clang("shard", 38, 0.16, 90, 120, 18, 40, 40, db=8),
        hat(clang("shard", 44, 0.03, 127, 127, 0, 90), db=7, pan=56),
        hat(clang("shard", 46, 0.3, 124, 127, -3, 90), db=-3, pan=72),
        clang("shard", 39, 0.12, 127, 127, -5, 70, 30, db=8),
        clang("fuse", 45, 0.25, 40, 60, 30, 20, 30, db=2, pan=50),
        clang("fuse", 52, 0.2, 40, 60, 30, 20, 30, db=3, pan=78),
        CL_BLIP, CL_BLIP, CL_BLIP, CL_BLIP,                          # (keys 8..11: SLOTS)
]),
    ("Burlap", [                                    # a tight kick, a cracking snare, a ghost snare, ride and crash
        x("xkick", 36, 28, 45, 20, 30, 15, drive=30, db=2),
        sophie("shard", 38, 0.24, 52, 76, 18, 28, 0, db=-1),
        hat(x("xhat", 84, 6, 90, 22, 92, 20), db=12, pan=58),
        hat(x("xhat", 80, 30, 60, 22, 92, 20), db=6, pan=72),
        sophie("fuse", 40, 0.16, 52, 76, 18, 28, 20, db=-10),        # (the ghost snare, on the clap's key)
        x("xtom", 45, 30, 76, 40, 10, 12, db=2, pan=50),
        x("xtom", 52, 26, 76, 40, 10, 12, db=3, pan=78),
        x("xrim", 74, 10, 76, 22, 10, db=5, pan=80),
        x("xnoise", tone=86, decay=10, db=3, pan=44),
        x("xhat", 96, 45, 90, 22, 92, 30, db=1, pan=40),              # (a ride: MACHINEDRUM's cymbal, shorter)
        x("xhat", 91, 65, 76, 22, 92, 12, db=2, pan=84),                 # (the crash: MACHINEDRUM's cymbal)
    ]),
    ("Sequin", [                                   # a long boom, a bodied snare and clap, FM toms, cowbell, a zap
        x("xkick", 31, 70, 30, 40, 10, 10, drive=30, db=-2),
        x("xsnare", 50, 30, 60, 30, 50, 80, db=9),
        hat(x("xhat", 82, 4, 90, 22, 92, 12), db=13, pan=56),
        hat(x("xhat", 82, 22, 90, 22, 92, 12), db=9, pan=72),
        x("xclap", tone=76, decay=27, db=11),
        x("xtom", 43, 40, 76, 60, 5, 60, db=0, pan=48),
        x("xtom", 50, 36, 76, 60, 5, 60, db=1, pan=80),
        x("xrim", 72, 12, 76, 60, 10, db=4, pan=82),
        clang("fuse", 50, 0.3, 40, 70, -40, 40, 100, db=-3, pan=44),   # (the zap)
        x("xhat", 91, 70, 50, 22, 92, 40, db=-2, pan=40),
        x("xfm", 79, 18, 76, 22, 10, 30, db=0, pan=86),
    ]),
    ("Frayed", [                                     # crushed and folded: Sophie's crusher, digisophie's fold
        sophie("kick", 31, 0.3, 24, 8, 6, 55, 30, db=-1),
        sophie("shard", 38, 0.2, 52, 76, 18, 28, 70, db=-1),
        hat(sophie("shard", 42, 0.06, 94, 98, 58, 0, 75, pan=58), db=-5),
        hat(sophie("shard", 46, 0.3, 91, 96, 62, -4, 70, pan=72), db=-9),
        clang("pipe", 39, 0.22, 107, 112, -5, 53, 60, db=4),
        clang("boom", 43, 0.4, 30, 56, 37, 20, 50, db=2, pan=50),
        clang("boom", 48, 0.3, 30, 56, 37, 20, 50, db=3, pan=78),
        sophie("shard", 37, 0.09, 76, 72, 34, 16, 80, db=-3, pan=80),
        clang("shard", 45, 0.12, 127, 127, 0, 127, 90, db=-7, pan=44),
        sophie("stack", 49, 1.2, 78, 92, 72, -12, 60, db=-13, pan=40),
        clang("pipe", 60, 0.6, 60, 64, 0, 20, 40, db=-3, pan=84),
    ]),
    # the mixed kits: the factory bank's 909 samples or the x models as the core, the new models as color (LOOM-8's own
# models stay in the Loom kit)
    ("Denim", [                                  # the 909 samples, sophie's toms, the x rim, cymbal and cowbell
        smp("kick909", db=4),
        smp("snare909", db=10),
        hat(smp("hat909", decay=60), db=7, pan=56),                   # (cut short: about 80 ms)
        hat(smp("hat909"), db=9, pan=72),
        smp("clap909", db=13),
        sophie("fuse", 45, 0.4, 34, 48, 18, 52, 8, db=-5, pan=50),
        sophie("fuse", 52, 0.28, 52, 60, 26, 40, 12, db=-4, pan=78),
        x("xrim", 67, 14, 80, 22, 8, db=4, pan=82),
        x("xnoise", tone=100, decay=8, db=3, pan=46),
        x("xhat", 91, 65, 76, 22, 92, 12, db=-1, pan=40),
        x("xfm", 79, 15, 76, 22, 10, 30, db=1, pan=86),
    ]),
    ("Linen", [                                   # short and dry: a tight kick, ticking hats, clicks, a small clap
        x("xkick", 38, 22, 40, 60, 10, 0, drive=20, db=4),
        x("xsnare", 58, 14, 90, 60, 60, 0, db=10),
        hat(clang("shard", 46, 0.03, 120, 124, 0, 70), db=7, pan=56),
        hat(clang("shard", 46, 0.15, 118, 122, 0, 75), db=0, pan=72),
        x("xclap", tone=95, decay=10, db=13),
        clang("boom", 45, 0.15, 30, 50, 30, 10, db=6, pan=48),
        clang("boom", 52, 0.12, 30, 50, 30, 10, db=7, pan=80),
        x("xrim", 76, 8, 80, 20, 5, db=0, pan=82),
        x("xnoise", tone=100, decay=3, db=0, pan=44),
        x("xhat", 96, 30, 90, 22, 92, 30, db=0, pan=40),              # (a short ride)
        CL_BLIP,
    ]),
    ("Velvet", [                                  # a round, long kick, the 909 clap, dusty hats, a round conga
        x("xkick", 34, 55, 25, 50, 10, 5, drive=25, db=-1),
        x("xsnare", 48, 34, 55, 35, 40, 15, db=7),
        hat(x("xhat", 76, 8, 40, 22, 92, 12), db=11, pan=58),
        hat(x("xhat", 72, 34, 25, 22, 92, 10), db=5, pan=70),
        smp("clap909", db=13),
        x("xtom", 43, 40, 50, 30, 5, 10, db=0, pan=48),
        x("xtom", 52, 40, 50, 30, 5, 10, db=0, pan=80),
        x("xrim", 67, 14, 80, 22, 8, db=0, pan=82),
        x("xnoise", tone=70, decay=12, db=0, pan=44),
        x("xhat", 91, 65, 76, 22, 92, 12, db=0, pan=40),
        SO_BLIP,
    ]),
    ("Twill", [                                   # the 909's own circuits and its cymbals (9W9's)
        n9("kick909", 45, 80, 20, 34, 0, 10, 0, db=0),
        n9("snare909", 64, 41, 70, 68, 36, 0, db=7),
        hat(n9("ch909", 64, 84, 0, 0), db=6, pan=56),                 # (levels as Denim's hats)
        hat(n9("oh909", 64, 97, 0, 0), db=10, pan=72),
        n9("clap909", 63, 63, 30, 0, db=12),
        n9("tom909", 66, 105, 23, 0, 41, 0, db=-1, pan=50),            # (the low tom)
        n9("tom909", 53, 100, 23, 2, 41, 0, db=-1, pan=78),            # (the high tom)
    ]),
]
# keys 8..11 as the default kit names them (conga, shaker, string, fm): a pattern written on Loom plays the same roles on
# every kit. the kit's own models; the fm key a short FM blip
SLOTS = {                                                              # each kit's own: no two kits share a key here
    "Nylon": [X_CONGA, X_SHAKER, X_PLUCK, X_BLIP],
    "Elastane": [sophie("boom", 55, 0.2, 30, 30, 10, 60, 0), sophie("shard", 45, 0.07, 100, 100, 50, 0, 0),
               sophie("pipe", 48, 0.3, 40, 40, 10, 0, 0), sophie("boom", 48, 0.12, 50, 40, 10, 15, 0)],
    "Wire": [clang("boom", 55, 0.2, 30, 40, 20, 10), clang("shard", 45, 0.08, 127, 127, 0, 60),
              clang("pipe", 48, 0.3, 50, 50, 0, 10), CL_BLIP],
    "Burlap": [x("xtom", 57, 18, 80, 35, 5, 0), x("xnoise", tone=88, decay=6), x("xfm", 55, 28, 50, 20, 0, 55),
               clang("boom", 47, 0.1, 50, 50, 30, 10)],
    "Sequin": [x("xtom", 60, 22, 70, 40, 0, 40), x("xnoise", tone=100, decay=4, drive=60), x("xfm", 48, 26, 50, 25, 0, 60),
                x("xfm", 55, 10, 80, 30, 0, 40)],
    "Frayed": [clang("boom", 55, 0.2, 30, 40, 20, 10, 50), sophie("shard", 45, 0.06, 100, 100, 60, 0, 70),
              clang("pipe", 48, 0.3, 50, 50, 0, 10, 40), dict(SO_BLIP, knobs=SO_BLIP["knobs"][:7] + [k127(40)])],
    "Denim": [x("xtom", 64, 18, 70, 20, 0, 0), x("xnoise", tone=90, decay=7), sophie("pipe", 60, 0.3, 40, 30, 10, 0, 0),
                 x("xfm", 52, 14, 55, 12, 0, 20)],
    "Linen": [x("xrim", 64, 10, 60, 30, 0), x("xnoise", tone=100, decay=3), clang("pipe", 55, 0.2, 50, 60, 0, 10),
                clang("boom", 50, 0.08, 45, 45, 20, 5)],
    "Velvet": [S_CONGA, x("xnoise", tone=70, decay=12), S_STRING, sophie("boom", 45, 0.18, 30, 25, 5, 20, 0)],
    "Twill": [n9("tom909", 69, 95, 23, 1, 41, 0), x("xnoise", tone=90, decay=7), S_STRING,   # (the conga a mid tom; the
              n9("rim909", 62, 24, 0)],                                                    # fm key the 909 rim)
}
# their volumes, balanced by role as the other keys (conga 6 dB under the kick, the shaker 14, the string and the fm 8)
SLOT_DB = {"Nylon": [3, 4, 0, 1], "Elastane": [-5, -6, -6, -7], "Wire": [1, 0, 0, 0], "Burlap": [3, 4, -1, 2],
           "Sequin": [2, 7, -1, 1], "Frayed": [1, -7, 0, -8], "Denim": [3, 4, -7, 0], "Linen": [7, 4, 1, 3],
           "Velvet": [-7, 4, -8, -9], "Twill": [3, 4, -8, 0]}
KITS = [(name, keys[:7] + [dict(k, db=d) for k, d in zip(SLOTS[name], SLOT_DB[name])]) for name, keys in KITS]
assert all(len(keys) == 11 and len(name) <= 11 for name, keys in KITS)


def vol(db):                                                           # drumline.c volume_lin: 76 = 0 dB
    return max(1, min(127, round(76 + db * (75 / 30) if db < 0 else 76 + db * (51 / 20))))


def key_bytes(k):
    knobs = (k["knobs"] + [0] * 8)[:8]
    return [k["model"], *knobs, vol(k.get("db", 0)), k.get("pan", 64), k.get("mode", PM_ONESHOT), k.get("decay", 127)]


def header():
    out = ["/* SPDX-License-Identifier: MPL-2.0 */",
           "/* generated by tools/gen_kits.py: the factory kits after Loom (app/ui.c kit_factory) */",
           f"#define NGKITS {len(KITS)}",
           "static const char *const GKIT_NAME[NGKITS] = {" + ", ".join(f'"{n}"' for n, _ in KITS) + "};",
           "/* each key: the model, its knobs 1..8, volume, pan, play mode, the key's decay (127: off, the model's own) */",
           "static const uint8_t GKIT[NGKITS][11][13] = {"]
    for name, keys in KITS:
        out.append(f"    {{   /* {name} */")
        out += ["        {" + ", ".join(map(str, key_bytes(k))) + "}," for k in keys]
        out.append("    },")
    out.append("};")
    return "\n".join(out) + "\n"


def xy(name):
    keys = dict(KITS)[name]
    lines = []
    for i, k in enumerate(keys, 1):
        b = key_bytes(k)
        lines.append(f"knob 1 d{i}.model {b[0]}")
        lines += [f"knob 1 d{i}.k{j + 1} {v}" for j, v in enumerate(b[1:9])]
        lines += [f"knob 1 d{i}.volume {b[9]}", f"knob 1 d{i}.pan {b[10]}", f"knob 1 d{i}.mode {b[11]}",
                  f"knob 1 d{i}.decay {b[12]}"]
    return "\n".join(lines) + "\n"


if __name__ == "__main__":
    if sys.argv[1:2] == ["--xy"]:
        sys.stdout.write(xy(sys.argv[2]))
    elif sys.argv[1:2] == ["--names"]:
        print("\n".join(n for n, _ in KITS))
    else:
        open(sys.argv[1], "w").write(header())
