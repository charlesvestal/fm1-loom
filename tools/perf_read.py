#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""LOOM-8's profiler on an attached FM-1: where the audio render's time goes, stage by stage (app/samples.c SB_PROF,
app/engine.h PR_*, app/dsp/synth.h synth_prof), as shares of the render's budget (the time its frames last).

  tools/perf_read.py [--every S] [--for S]      a line every S seconds (1.0) for S seconds (forever): the stages'
                                                shares since the last line, the slowest block's, each engine's

As a module (tools/demos/device_load.py): snap(port) the running totals, diff(a, b) the shares between two snaps.
The clock is the 24 MHz timer; a stage's share = its time / (frames / 44.1 kHz). synth0 is the first core's voices,
synth1 the second core's job (in parallel: the two cores' sum is not the load), wait what the first core waited for
the second; the load (all) is the first core's: seq + synth0 + drum + prep + wait + mix + fx1 + fx2 + master + out.
Needs mido + python-rtmidi."""
import argparse
import sys
import time

HDR = [0x7D, 0x46, 0x4C]
CMD = 26
STAGES = ["seq", "synth0", "synth1", "drum", "fx1", "master", "wait", "fx2", "prep", "mix", "out", "all", "subs", "note", "six", "step", "sub", "comp", "locks", "notes", "fire", "drain", "fxj"]
ENGINES = ["knit", "rayon", "jacquard", "bellows", "cane", "voile", "sample", "slice", "kit", "chunk"]
TICK_HZ = 24e6
SR = 44100.0
TRAIL = ["load", "voices", "cap", "saws", "guard", "drops", "steals"]


class Port:
    """a pair of mido ports (the FM-1's): anything with .out and .inp will do"""

    def __init__(self):
        import mido
        outs = [n for n in mido.get_output_names() if "LOOM" in n or "FM-1" in n]
        ins = [n for n in mido.get_input_names() if "LOOM" in n or "FM-1" in n]
        if not outs or not ins:
            sys.exit("perf_read: no FM-1 on USB-MIDI")
        self.out = mido.open_output(outs[0])
        self.inp = mido.open_input(ins[0])


def ask(port, part, timeout=1.0):
    import mido
    for _ in range(3):
        port.out.send(mido.Message("sysex", data=HDR + [CMD, part]))
        t0 = time.time()
        while time.time() - t0 < timeout:
            for m in port.inp.iter_pending():
                if m.type == "sysex" and list(m.data[:4]) == HDR + [CMD]:
                    d = list(m.data[4:])
                    if d[0] != (part & 63):
                        continue
                    n = d[1]
                    return [sum(d[2 + 5 * i + j] << (7 * j) for j in range(5)) & 0xFFFFFFFF for i in range(n)]
            time.sleep(0.002)
    raise TimeoutError("perf_read: no PROF reply")


def snap(port, reset_worst=True):
    """the running totals: {'t', 'frames', 'stage': {..}, 'worst': {..}, 'load'.., 'eng': [2][16], 'drum': [8]}"""
    tr = ask(port, 3)                        # (first: the reset below lets the next block overwrite it)
    try:
        vb = ask(port, 4)                    # the slowest block's voices (a firmware without it: none)
    except TimeoutError:
        vb = None
    flat = [x for w in tr for x in (w & 0xFFFF, w >> 16)]
    a = ask(port, 0 | (64 if reset_worst else 0))
    n = len(STAGES)
    s = {"t": a[0], "frames": a[1], "stage": dict(zip(STAGES, a[2:2 + n])), "worst": dict(zip(STAGES, a[2 + n:2 + 2 * n]))}
    s.update(dict(zip(TRAIL, a[2 + 2 * n:])))
    e = ask(port, 1 | (64 if reset_worst else 0))
    k = len(ENGINES)
    s["eng"] = [e[:k], e[k:2 * k]]
    s["vworst"] = e[2 * k:2 * k + 3]
    s["drum"] = ask(port, 2)
    s["trace"] = [flat[i:i + 8] for i in range(0, len(flat), 8)]
    s["vblk"] = vb
    s["wall"] = time.time()
    return s


def diff(a, b):
    """the shares (%) between two snaps: stages, the slowest block's stages (b's, since a), engines (both halves) and
    drum tracks; 'subs' as stretches a block"""
    fr = (b["frames"] - a["frames"]) & 0xFFFFFFFF
    if fr == 0:
        return None
    budget = fr / SR * TICK_HZ
    pct = lambda d: 100.0 * (d & 0xFFFFFFFF) / budget
    out = {"frames": fr, "stage": {}, "worst": {}, "eng0": {}, "eng1": {}, "drum": []}
    for k in STAGES:
        d = (b["stage"][k] - a["stage"][k]) & 0xFFFFFFFF
        out["stage"][k] = d / (fr / 256.0) if k == "subs" else pct(d)
    wb = 256 / SR * TICK_HZ                  # the worst block: one 256-frame block's budget
    for k in STAGES:
        out["worst"][k] = b["worst"][k] if k == "subs" else 100.0 * b["worst"][k] / wb
    for h in (0, 1):
        for i, k in enumerate(ENGINES):
            v = pct(b["eng"][h][i] - a["eng"][h][i])
            if v >= 0.05:
                out["eng%d" % h][k] = v
    out["drum"] = [pct(b["drum"][i] - a["drum"][i]) for i in range(len(b["drum"]))]
    for k in TRAIL:
        out[k] = b[k]
    out["trace"] = b.get("trace")
    out["vblk"] = b.get("vblk")
    vw = b.get("vworst") or [0, 0, 0]
    out["vworst"] = (100.0 * vw[0] / wb, ENGINES[vw[1]] if vw[1] < len(ENGINES) else "?", vw[2])
    return out


def vblk_text(vb):
    """the slowest block's voices: each sounding one's time (% of the block), engine, * started there, the core
    (0 / 1); the voices each core took in the first stretch and the last (engine.c render_sub: the cores share
    them out as they go), whether core 1 joined late (from the effects' job), and the block's stretches"""
    if not vb or len(vb) < 17:
        return ""
    parts, sums = [], [0.0, 0.0]
    for i, w in enumerate(vb[:16]):
        info = w >> 16 & 0xFF
        if not info & 128:
            continue
        t = (w & 0xFFFF) / 10.0
        core = 1 if info & 64 else 0
        sums[core] += t
        eng = ENGINES[info & 31] if (info & 31) < len(ENGINES) else "?"
        parts.append(f"v{i} {eng}{'*' if info & 32 else ''} c{core} {t:.1f}")
    x = vb[16]
    took = f"first stretch: core 1 took {x & 255}, core 0 {x >> 8 & 255}{' (core 1 late)' if x >> 16 & 128 else ''}"
    if len(vb) > 17:                         # the last stretch's
        y = vb[17]
        took += f"; last: core 1 {y & 255}, core 0 {y >> 8 & 255}{' (late)' if y >> 16 & 128 else ''}"
    return (f"{took}, {x >> 24} stretches; voices c0 {sums[0]:.1f} % c1 {sums[1]:.1f} %: " + ", ".join(parts))


def trace_text(tr):
    """the blocks up to the slowest (all synth0 synth1 seq drum note in %, stretches, voices)"""
    return " ; ".join("%d=%d/%d/%d/%d/%d/%d s%d v%d" % (i, r[0] // 10, r[1] // 10, r[2] // 10, r[3] // 10, r[4] // 10,
                                                      r[5] // 10, r[6], r[7]) for i, r in enumerate(tr))


def line(d):
    st = d["stage"]
    w = d["worst"]
    eng = {}
    for h in ("eng0", "eng1"):
        for k, v in d[h].items():
            eng[k] = eng.get(k, 0.0) + v
    s = " ".join(f"{k} {st[k]:.1f}" for k in STAGES if k != "subs") + f" subs/blk {st['subs']:.1f}"
    s += f" | worst all {w['all']:.0f}: " + " ".join(f"{k} {w[k]:.0f}" for k in STAGES if k not in ("all", "subs") and w[k] >= 1)
    s += f" subs {w['subs']}"
    s += " | eng " + " ".join(f"{k} {v:.1f}" for k, v in sorted(eng.items(), key=lambda kv: -kv[1]))
    dr = [f"T{i + 1} {v:.1f}" for i, v in enumerate(d["drum"]) if v >= 0.05]
    if dr:
        s += " | drums " + " ".join(dr)
    s += " | slowest voice %.0f %% (%s, %d frames)" % d["vworst"]
    s += f" | load {d['load']} voices {d['voices']}/{d['cap']} saws {d['saws']} guard {d['guard']} drops {d['drops']} steals {d.get('steals', 0)}"
    return s


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--every", type=float, default=1.0)
    ap.add_argument("--for", dest="secs", type=float, default=0.0)
    a = ap.parse_args()
    p = Port()
    prev = snap(p)
    t0 = time.time()
    while not a.secs or time.time() - t0 < a.secs:
        time.sleep(a.every)
        cur = snap(p)
        d = diff(prev, cur)
        if d:
            print(line(d), flush=True)
        prev = cur


if __name__ == "__main__":
    main()
