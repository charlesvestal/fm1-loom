#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""LOOM-8's factory kits after Loom and Loom Samples: each kit eleven keys, written here in each model's own terms
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
         xkick=11, xsnare=12, xhat=13, xclap=14, xtom=15, xrim=16, xfm=17, xnoise=18, clang=19, sophie=20)
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


def loom(model, k1, k2, k3, k4, **key):
    """one of LOOM-8's own models (kick, snare, hat, clap, shaker, conga, tom, fm ...), its four knobs as they are"""
    return dict(model=M[model], knobs=[k1, k2, k3, k4], **key)


SMP = dict(kick909=0, snare909=1, hat909=2, clap909=3)                # the factory bank's first four (Loom Samples)


def smp(sample, pitch=64, **key):
    """a sample of the factory bank on the sample model (pitch: 64 its own, 40..88 = +-24 semitones)"""
    return dict(model=M["sample"], knobs=[SMP[sample], pitch, 0, 0], **key)


def hat(k, **key):                                                     # the closed and open hats choke each other
    return dict(k, mode=PM_CHOKE1, **key)


# the kits: name (11 characters at most), then the eleven keys; db = the key's volume, pan 0..127 (64 the middle)
KITS = [
    ("Machine", [                                   # MACHINEDRUM's, as you hearted them
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
    ("Sophie", [                                    # Sophie's kick, snares and crash, digisophie's closed hat
        sophie("boom", 31, 0.35, 24, 30, 6, 55, 0, db=-1),
        sophie("stack", 40, 0.18, 63, 82, 28, 24, 38, db=0),
        hat(clang("shard", 42, 0.055, 119, 124, 0, 74), db=3, pan=56),
        hat(sophie("shard", 46, 0.48, 91, 96, 62, -4, 0), db=-10, pan=72),
        clang("shard", 39, 0.19, 107, 112, -5, 53, db=6),
        sophie("fuse", 41, 0.42, 24, 44, 16, 58, 8, db=-5, pan=50),
        sophie("boom", 48, 0.3, 30, 50, 20, 40, 8, db=-4, pan=78),
        sophie("shard", 37, 0.09, 76, 72, 34, 16, 48, db=-4, pan=80),
        clang("shard", 45, 0.12, 127, 127, 0, 127, 60, db=-4, pan=44),
        sophie("stack", 49, 1.65, 78, 92, 72, -12, 28, db=-13, pan=40),  # (Sophie's factory crash)
        sophie("pipe", 60, 0.6, 50, 50, 20, 0, 0, db=-8, pan=84),
    ]),
    ("Clang", [                                     # digisophie's, all of it
        clang("fuse", 31, 0.28, 30, 10, 35, 8, db=5),
        clang("shard", 38, 0.24, 66, 97, 18, 23, db=6),
        hat(clang("shard", 42, 0.055, 119, 124, 0, 74), db=3, pan=56),
        hat(clang("shard", 46, 0.48, 116, 122, -3, 79), db=-5, pan=72),
        clang("shard", 39, 0.19, 107, 112, -5, 53, db=6),
        clang("fuse", 43, 0.42, 30, 56, 37, 20, db=0, pan=50),
        clang("boom", 48, 0.3, 30, 56, 37, 20, db=3, pan=78),
        clang("shard", 37, 0.09, 97, 91, 10, 43, db=4, pan=80),
        clang("shard", 45, 0.5, 127, 127, 0, 127, db=-9, pan=44),
        x("xhat", 91, 65, 76, 22, 92, 12, db=-1, pan=40),               # (MACHINEDRUM's cymbal)
        clang("pipe", 60, 0.6, 60, 64, 0, 20, db=-3, pan=84),
    ]),
    ("House", [                                     # round kick, long clap, crisp digital hats, a cowbell, a rim
        x("xkick", 36, 45, 30, 70, 20, 10, drive=35, db=-1),
        x("xsnare", 50, 28, 70, 40, 45, 20, db=8),
        hat(clang("shard", 44, 0.06, 112, 118, 0, 60), db=3, pan=58),
        hat(x("xhat", 74, 26, 30, 22, 92, 12), db=7, pan=70),
        x("xclap", tone=80, decay=40, db=8),
        x("xtom", 43, 40, 60, 30, 5, 20, db=0, pan=48),
        x("xtom", 50, 36, 60, 30, 5, 20, db=1, pan=80),
        x("xrim", 67, 14, 80, 22, 8, db=4, pan=82),
        x("xnoise", tone=100, decay=8, db=3, pan=46),
        x("xhat", 91, 65, 76, 22, 92, 12, db=-1, pan=40),               # (MACHINEDRUM's cymbal)
        x("xfm", 79, 15, 76, 22, 10, 30, db=1, pan=86),
    ]),
    ("Garage", [                                    # a short punchy kick, a snappy snare, shuffling hats, a rim
        x("xkick", 38, 26, 50, 85, 40, 20, drive=40, db=1),
        x("xsnare", 55, 24, 100, 100, 70, 10, db=8),
        hat(clang("shard", 42, 0.045, 119, 124, 0, 74), db=4, pan=58),
        hat(clang("shard", 46, 0.25, 116, 122, -3, 79), db=-2, pan=72),
        x("xclap", tone=90, decay=14, db=13),
        sophie("fuse", 43, 0.36, 38, 52, 22, 48, 12, db=-5, pan=50),
        sophie("fuse", 50, 0.23, 58, 64, 30, 36, 18, db=-4, pan=78),
        x("xrim", 79, 12, 80, 30, 8, db=4, pan=82),
        x("xnoise", tone=96, decay=6, db=4, pan=44),
        x("xhat", 91, 65, 76, 22, 92, 12, db=-1, pan=40),              # (MACHINEDRUM's cymbal)
        sophie("stack", 67, 0.25, 50, 60, 20, 0, 10, db=-7, pan=84),
    ]),
    ("Jungle", [                                    # a tight kick, a cracking snare, a ghost snare, ride and crash
        x("xkick", 36, 28, 45, 20, 30, 15, drive=30, db=2),
        sophie("shard", 38, 0.24, 52, 76, 18, 28, 0, db=-1),
        hat(x("xhat", 84, 6, 90, 22, 92, 20), db=12, pan=58),
        hat(x("xhat", 80, 30, 60, 22, 92, 20), db=6, pan=72),
        sophie("fuse", 40, 0.16, 52, 76, 18, 28, 20, db=-11),        # (the ghost snare, on the clap's key)
        x("xtom", 45, 30, 76, 40, 10, 12, db=2, pan=50),
        x("xtom", 52, 26, 76, 40, 10, 12, db=3, pan=78),
        x("xrim", 74, 10, 76, 22, 10, db=5, pan=80),
        x("xnoise", tone=86, decay=10, db=3, pan=44),
        x("xhat", 96, 45, 90, 22, 92, 30, db=1, pan=40),              # (a ride: MACHINEDRUM's cymbal, shorter)
        x("xhat", 91, 65, 76, 22, 92, 12, db=2, pan=84),                 # (the crash: MACHINEDRUM's cymbal)
    ]),
    ("Electro", [                                   # a long boom, a bodied snare and clap, FM toms, cowbell, a zap
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
    ("Lo-fi", [                                     # crushed and folded: Sophie's crusher, digisophie's fold
        sophie("kick", 31, 0.3, 24, 8, 6, 55, 30, db=-1),
        sophie("shard", 38, 0.2, 52, 76, 18, 28, 70, db=-1),
        hat(sophie("shard", 42, 0.06, 94, 98, 58, 0, 75, pan=58), db=-5),
        hat(sophie("shard", 46, 0.3, 91, 96, 62, -4, 70, pan=72), db=-10),
        clang("pipe", 39, 0.22, 107, 112, -5, 53, 60, db=4),
        clang("boom", 43, 0.4, 30, 56, 37, 20, 50, db=2, pan=50),
        clang("boom", 48, 0.3, 30, 56, 37, 20, 50, db=3, pan=78),
        sophie("shard", 37, 0.09, 76, 72, 34, 16, 80, db=-3, pan=80),
        clang("shard", 45, 0.12, 127, 127, 0, 127, 90, db=-7, pan=44),
        sophie("stack", 49, 1.2, 78, 92, 72, -12, 60, db=-13, pan=40),
        clang("pipe", 60, 0.6, 60, 64, 0, 20, 40, db=-3, pan=84),
    ]),
    # the mixed kits: a standard core (the factory bank's 909 samples, LOOM-8's own models), the new models as color
    ("909 Plus", [                                  # the 909 samples, sophie's toms, the x rim, cymbal and cowbell
        smp("kick909", db=4),
        smp("snare909", db=10),
        hat(smp("hat909", decay=60), db=7, pan=56),                   # (cut short: about 80 ms)
        hat(smp("hat909"), db=9, pan=72),
        smp("clap909", db=13),
        sophie("fuse", 43, 0.36, 38, 52, 22, 48, 12, db=-5, pan=50),
        sophie("fuse", 50, 0.23, 58, 64, 30, 36, 18, db=-4, pan=78),
        x("xrim", 67, 14, 80, 22, 8, db=4, pan=82),
        x("xnoise", tone=100, decay=8, db=3, pan=46),
        x("xhat", 91, 65, 76, 22, 92, 12, db=-1, pan=40),
        x("xfm", 79, 15, 76, 22, 10, 30, db=1, pan=86),
    ]),
    ("Analog Plus", [                               # LOOM-8's own kick, snare, hats and clap; x toms, rim, cymbal; a bell
        loom("kick", 82, 42, 64, 64, db=0),
        loom("snare", 79, 40, 84, 64, db=8),
        hat(loom("hat", 93, 23, 64, 88), db=7, pan=56),
        hat(loom("hat", 93, 88, 64, 88), db=0, pan=72),
        loom("clap", 100, 64, 64, 38, db=11),
        x("xtom", 43, 40, 60, 30, 5, 20, db=0, pan=48),
        x("xtom", 50, 36, 60, 30, 5, 20, db=1, pan=80),
        x("xrim", 72, 12, 76, 22, 10, db=5, pan=82),
        loom("shaker", 31, 64, 64, 101, db=-1, pan=44),
        x("xhat", 91, 65, 76, 22, 92, 12, db=-1, pan=40),
        sophie("pipe", 72, 0.6, 50, 50, 20, 0, 0, db=-8, pan=84),
    ]),
    ("Deep Mix", [                                  # the house demo's round kick, the 909 clap, LOOM's hat and conga
        loom("kick", 76, 64, 48, 60, db=-1),
        x("xsnare", 50, 28, 70, 40, 45, 20, db=8),
        hat(loom("hat", 96, 23, 64, 88), db=7, pan=58),
        hat(x("xhat", 74, 26, 30, 22, 92, 12), db=7, pan=70),
        smp("clap909", db=13),
        loom("conga", 109, 46, 64, 0, db=2, pan=48),
        x("xtom", 50, 36, 60, 30, 5, 20, db=1, pan=80),
        x("xrim", 67, 14, 80, 22, 8, db=4, pan=82),
        loom("shaker", 50, 64, 64, 101, db=1, pan=44),
        x("xhat", 91, 65, 76, 22, 92, 12, db=-1, pan=40),
        sophie("pipe", 60, 0.6, 50, 50, 20, 0, 0, db=-8, pan=84),
    ]),
    ("Breaks Mix", [                                # a tight LOOM kick, the 909 snare, x hats, a ghost snare, ride, crash
        loom("kick", 82, 30, 80, 64, db=1),
        smp("snare909", db=10),
        hat(x("xhat", 84, 6, 90, 22, 92, 20), db=12, pan=58),
        hat(x("xhat", 80, 30, 60, 22, 92, 20), db=6, pan=72),
        sophie("fuse", 40, 0.16, 52, 76, 18, 28, 20, db=-11),        # (the ghost snare, on the clap's key)
        loom("tom", 70, 46, 38, 64, db=4, pan=50),
        loom("tom", 100, 46, 38, 64, db=4, pan=78),
        x("xrim", 74, 10, 76, 22, 10, db=5, pan=80),
        x("xnoise", tone=86, decay=10, db=3, pan=44),
        x("xhat", 96, 45, 90, 22, 92, 30, db=1, pan=40),           # (a ride)
        x("xhat", 91, 65, 76, 22, 92, 12, db=2, pan=84),           # (a crash)
    ]),
]
assert all(len(keys) == 11 and len(name) <= 11 for name, keys in KITS)


def vol(db):                                                           # drumline.c volume_lin: 76 = 0 dB
    return max(1, min(127, round(76 + db * (75 / 30) if db < 0 else 76 + db * (51 / 20))))


def key_bytes(k):
    knobs = (k["knobs"] + [0] * 8)[:8]
    return [k["model"], *knobs, vol(k.get("db", 0)), k.get("pan", 64), k.get("mode", PM_ONESHOT), k.get("decay", 127)]


def header():
    out = ["/* SPDX-License-Identifier: GPL-3.0-only */",
           "/* generated by tools/gen_kits.py: the factory kits after Loom and Loom Samples (app/ui.c kit_factory) */",
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
