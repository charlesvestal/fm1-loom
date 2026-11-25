#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""LOOM-8's factory presets: the recipes in tools/presets/*.py (tools/presets/lib.py says how one is written) into
the PRESETS table app/dsp/synth.c includes, each value by its parameter's name (synth.h TP_*), so a change of the
layout cannot shift them. The presets go in the engines' order (synth.h EG_*: an engine's first is its sound in a
new project), each engine's in the order its files list them (FILES, then any other file, by name).
  tools/gen_presets.py OUT.h        (run by the build: app/fm1.toml gen; the host tests run it too)
  tools/gen_presets.py --json       the presets as JSON (the checks, the docs)
  tools/gen_presets.py --full OUT.h each preset's fields whole (the host test checks the table decodes to them)"""
import collections
import json
import re
import runpy
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE / "presets"))
import lib  # noqa: E402

FILES = ["knit", "rayon", "jacquard", "bellows", "cane", "voile", "sample", "slice", "acid", "tulle", "lace",
         "tartan"]


def load():
    names = [f for f in FILES if (HERE / "presets" / f"{f}.py").exists()]
    names += sorted(p.stem for p in (HERE / "presets").glob("*.py") if p.stem not in FILES and p.stem != "lib")
    for n in names:
        runpy.run_path(str(HERE / "presets" / f"{n}.py"), run_name="presets." + n)
    ps = sorted(lib.PRESETS, key=lambda p: lib.ENGINES.index(p["engine"]))   # (stable: the files' order within)
    seen = set()
    for p in ps:
        assert p["name"] not in seen, f"two presets named {p['name']}"
        seen.add(p["name"])
    return ps


def fields(p):
    """a preset's fields as C values, by name: par[0..TP_PLAYER) (TP_* less its prefix), then the pages past it"""
    lf, ch, fl = p["lfo"], p["fx1"], p["filter"]
    dest = lib.DESTS.index(lf[3]) if lf[0] in ("off", "wave", "drift") else lf[3]
    f = [("ENGINE", "EG_" + p["engine"].upper())]
    f += [(f"E{i + 1}", v) for i, v in enumerate(p["knobs"])]
    f += [("VMODE", "VM_" + p["mode"].upper()), ("PORTA", p["glide"]), ("BEND", p["bend"]), ("VOLUME", p["volume"])]
    f += list(zip(("A", "D", "S", "R"), p["amp"])) + list(zip(("FA", "FD", "FS", "FR"), p["fenv"]))
    f += [("CUT", fl[1]), ("RES", fl[2]), ("FENV", fl[3]), ("KTRK", fl[4]), ("FTYPE", "FT_" + fl[0].upper())]
    f += [("SEND1", p["sends"][0]), ("SEND2", p["sends"][1])]
    f += [("LTYPE", "LT_" + lf[0].upper()), ("LSPEED", lf[1]), ("LAMT", lf[2]), ("LDEST", dest), ("LX", lf[4]),
          ("LX2", lf[5])]
    f += [("CTYPE", "EF_" + ch[0].upper()), ("CAMT", ch[1]), ("CTONE", ch[2]), ("CMIX", ch[4])]
    f += [(f"X{i + 1}", v) for i, v in enumerate(p["x"])] + [("OCT", p["octave"] + 2)]
    o2, md, op = p["osc2"], p["mod"], p["ops"]
    f += [("O2FINE", o2[2]), ("O2WAVE", "O2W_" + (o2[0].upper()[:3] if o2[0] == "triangle" else o2[0].upper())),
          ("O2SHAPE", o2[1])]
    if p["engine"] == "tartan":                  # (tartan's Morph page in knit's Mod page's bytes)
        f += list(zip(("KSYNC", "KSWEEP", "KCROSS", "KSUB"), p["morph"]))
    elif p["engine"] != "tulle":                 # (tulle's Grain page in knit's Mod page's bytes)
        f += [("KSYNC", int(md[0])), ("KSWEEP", md[1]), ("KCROSS", md[2]), ("KSUB", "KS_" + md[3].upper().replace(" ", "_"))]
    else:
        f += [("KSYNC", p["grain"][0]), ("KSWEEP", p["grain"][1]), ("KCROSS", 0), ("KSUB", 0)]
    f += [("ROPS", "RO_" + op[0].upper()), ("RRATIO", lib.RO_RATIOS.index(op[1])), ("RDEPTH", op[2]), ("RFB", op[3]),
          ("KANALOG", p["analog"])]
    ins = p["fx2"]
    f += [("ITYPE", "EF_" + ins[0].upper()), ("I1", ins[1]), ("I2", ins[2]), ("I3", ins[3]), ("HPF", p["lowcut"])]
    f += [("S1K3", ch[3])] + [(f"S1X{i + 1}", v) for i, v in enumerate(ch[5:])] + [("S2MIX", ins[4])]
    f += [(f"S2X{i + 1}", v) for i, v in enumerate(ins[5:])]
    return {k: str(v) for k, v in f}


EXTRA = ["O2FINE", "O2WAVE", "O2SHAPE", "KSYNC", "KSWEEP", "KCROSS", "KSUB", "ROPS", "RRATIO", "RDEPTH", "RFB", "KANALOG",
         "ITYPE", "I1", "I2", "I3", "HPF", "S1K3", "S1X1", "S1X2", "S1X3", "S1X4", "S2MIX", "S2X1", "S2X2", "S2X3",
         "S2X4"]


def sound_order():
    """par[0..TP_PLAYER)'s names in synth.h's order (TP_ less its prefix), then EXTRA: the preset fields' order (synth.c
    PS_X)"""
    h = (HERE.parent / "app" / "dsp" / "synth.h").read_text()
    i = h.index("enum {\n    TP_ENGINE")
    names = re.findall(r"\bTP_([A-Z0-9]+)\b", h[i:h.index("TP_PLAYER", i)])
    return names + EXTRA


def table(ps):
    """the presets as each engine's template (the value most of its presets have, field by field) and each preset's
    differences from it: a mask of the fields that differ (PS_MASK bytes, bit i field i), then their values"""
    order = sound_order()
    fs = [fields(p) for p in ps]
    tpl = {}
    for e in dict.fromkeys(p["engine"] for p in ps):
        mine = [f for f, p in zip(fs, ps) if p["engine"] == e]
        tpl[e] = [collections.Counter(f[k] for f in mine).most_common(1)[0][0] for k in order]
    data, at = [], []
    for f, p in zip(fs, ps):
        t = tpl[p["engine"]]
        mask = [0] * ((len(order) + 7) // 8)
        vals = []
        for i, k in enumerate(order):
            if f[k] != t[i]:
                mask[i >> 3] |= 1 << (i & 7)
                vals.append(f[k])
        at.append(len(data))
        data += [str(m) for m in mask] + vals
    return order, tpl, data, at


def main(argv):
    ps = load()
    if argv and argv[0] == "--json":
        print(json.dumps(ps, indent=1))
        return
    if argv and argv[0] == "--full":             # (the host test's check: every preset's fields whole, as decoded)
        order = sound_order()
        rows = [fields(p) for p in ps]
        Path(argv[1]).write_text("/* generated by tools/gen_presets.py --full: each preset's fields whole (tests) */\n"
                                 f"static const uint8_t PRESET_FULL[{len(ps)}][{len(order)}] = {{\n" +
                                 "\n".join("    {" + ", ".join(r[k] for k in order) + "}," for r in rows) + "\n};\n")
        return
    order, tpl, data, at = table(ps)
    assert len(data) < 65536
    text = ("/* generated by tools/gen_presets.py from the recipes in tools/presets/: edit those, not this. Each engine's\n"
            " * template, then each preset's differences from it (synth.h preset_t, synth.c preset_sound) */\n"
            "_Static_assert(TP_OCT + 1 == NTP_SOUND, \"gen_presets.py: a preset names every sound parameter\");\n"
            f"_Static_assert(PS_N == {len(order)}, \"gen_presets.py: the preset fields as synth.c's\");\n"
            "static const uint8_t PRESET_TPL[NENGINE][PS_N] = {\n" +
            "\n".join(f"    [EG_{e.upper()}] = {{{', '.join(v)}}}," for e, v in tpl.items()) + "\n};\n"
            f"static const uint8_t PRESET_DATA[{len(data)}] = {{\n" +
            "\n".join("    " + ", ".join(data[i:i + 24]) + "," for i in range(0, len(data), 24)) + "\n};\n"
            "const preset_t PRESETS[] = {\n" +
            "\n".join(f'    {{"{p["name"]}", EG_{p["engine"].upper()}, RL_{p["role"].upper()}, {a}}},' for p, a in zip(ps, at)) +
            "\n};\n")
    out = Path(argv[0])
    if not out.exists() or out.read_text() != text:   # (unchanged: left alone, so nothing rebuilds for it)
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(text)


if __name__ == "__main__":
    main(sys.argv[1:])
