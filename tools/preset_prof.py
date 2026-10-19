#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The presets' cost: every factory preset (or an engine's) held as a chord of 1, 4, 8 .. notes, its load measured,
on an attached FM-1 (the profiler: tools/perf_read.py) and in the host's load model (app/host.c meter), side by side.

  tools/preset_prof.py [--engines knit,rayon] [--names a,b] [--voices 1,4,8] [--hold S] [--host-only] [--device-only]
                       [--out build/preset_prof]
  tools/preset_prof.py --scenario NAME,NAME..   tests/scenarios/loadmodel.xy from OUT.csv's FM-1 rows (those presets,
                                                each chord within TOL points: the host's model held to the FM-1)

On the FM-1: a new project's silence first (the idle load); then for each preset SOUND (app/samples.c) puts it on T2
and selects it, the chord goes in over USB-MIDI (channel 16: the selected track), after 0.3 s the profiler's totals,
again after --hold s (1.5): the mean of the first core's load (all), the slowest 256-frame block, each core's voices,
the engine's time, the voices sounding, guard trips; the notes off, then a wait for the voices to end. On the host:
the same, the model's mean and peak (meter). The table to OUT.txt, the rows to OUT.csv. Needs mido + python-rtmidi
for the FM-1; the host build (sh build.sh --host) for the model. Leave the FM-1 on a new project, not playing.
"""
import argparse
import csv
import re
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import perf_read as pr  # noqa: E402

TRK = 1                                       # T2 (0-based): T1 is the kit
CHORD = [48, 55, 60, 64, 67, 72, 76, 79, 84, 88, 91, 96]   # spread like a player's hands, low to high
SOUND = 28


def presets():
    """[(index, name, engine)] in the firmware's order (tools/gen_presets.py's table)"""
    import gen_presets
    return [(i, p["name"], p["engine"]) for i, p in enumerate(gen_presets.load())]


# ------------------------------------------------------------------ the FM-1 ---
def sound(port, k):
    import mido
    port.out.send(mido.Message("sysex", data=pr.HDR + [SOUND, TRK, k & 127, k >> 7]))
    t0 = time.time()
    while time.time() - t0 < 1.0:
        for m in port.inp.iter_pending():
            if m.type == "sysex" and list(m.data[:4]) == pr.HDR + [SOUND]:
                d = list(m.data[4:])
                return d[3], bytes(d[4:d.index(0, 4)]).decode()
        time.sleep(0.002)
    raise TimeoutError("preset_prof: no SOUND reply (a firmware without it?)")


def notes(port, ns, on):
    import mido
    for n in ns:
        port.out.send(mido.Message("note_on" if on else "note_off", channel=15, note=n, velocity=100 if on else 0))


def measure(port, hold):
    a = pr.snap(port)
    time.sleep(hold)
    b = pr.snap(port)
    d = pr.diff(a, b)
    eng = sum(d["eng0"].values()) + sum(d["eng1"].values()) - d["eng0"].get("chunk", 0) - d["eng1"].get("chunk", 0)
    return {"all": d["stage"]["all"], "worst": d["worst"]["all"], "synth0": d["stage"]["synth0"],
            "synth1": d["stage"]["synth1"], "eng": eng, "voices": b["voices"], "guard": b["guard"] - a["guard"],
            "drops": b["drops"] - a["drops"], "vworst": d["vworst"][0]}


def settle(port, limit=4.0):
    t0 = time.time()
    while time.time() - t0 < limit:
        if pr.ask(port, 0)[2 + 2 * len(pr.STAGES) + 1] == 0:      # voices
            return
        time.sleep(0.1)


def device(rows, voices, hold):
    port = pr.Port()
    idle = measure(port, 1.0)
    print(f"FM-1 idle: all {idle['all']:.1f} %, worst block {idle['worst']:.0f} %")
    out = {}
    for k, name, eng in rows:
        e, nm = sound(port, k)
        if nm != name:
            sys.exit(f"preset_prof: the FM-1 has preset {k} as {nm!r}, the table {name!r} (an older firmware?)")
        time.sleep(0.15)
        for n in voices:
            ns = CHORD[:n]
            notes(port, ns, True)
            time.sleep(0.3)
            out[(k, n)] = measure(port, hold)
            notes(port, ns, False)
            settle(port)
        r = out[(k, voices[-1])]
        print(f"  {name:12s} {eng:9s} " + "  ".join(f"{n}v {out[(k, n)]['all']:5.1f}/{out[(k, n)]['worst']:3.0f}"
                                                   for n in voices) + (f"  GUARD {r['guard']}" if r["guard"] else ""))
    return idle, out


# ------------------------------------------------------------------ the host ---
def host(rows, voices, hold):
    lines = ["wait 300"]
    for k, name, eng in rows:
        lines.append(f"preset {TRK + 1} {name.replace(' ', '_')} {eng}")
        lines.append("wait 50")
        for n in voices:
            ns = CHORD[:n]
            lines += [f"play {TRK + 1} {x} on 100" for x in ns]
            lines += ["wait 300", f"meter {int(hold * 1000)}"]
            lines += [f"play {TRK + 1} {x} off" for x in ns]
            lines.append("wait 2500")
    scr = ROOT / "build" / "preset_prof.xy"
    scr.parent.mkdir(parents=True, exist_ok=True)
    scr.write_text("\n".join(lines) + "\n")
    exe = ROOT / "app" / "build" / "host" / "loom_host"
    res = subprocess.run([str(exe), str(scr), str(ROOT / "build" / "preset_prof_host")], capture_output=True,
                         text=True)
    m = re.findall(r"model load mean ([\d.]+) %, peak ([\d.]+) %, voices (\d+)", res.stdout)
    want = len(rows) * len(voices)
    if len(m) != want:
        sys.exit(f"preset_prof: the host gave {len(m)} meters of {want}\n{res.stdout[-2000:]}{res.stderr[-2000:]}")
    out, i = {}, 0
    for k, _, _ in rows:
        for n in voices:
            out[(k, n)] = {"mean": float(m[i][0]), "peak": float(m[i][1]), "voices": int(m[i][2])}
            i += 1
    return out


TOL = 6


def scenario(csv_path, names):
    rows = {(r["preset"], int(r["notes"])): r for r in csv.DictReader(open(csv_path)) if r["fm1_all"]}
    eng = {name: e for _, name, e in presets()}
    out = ["# the host's load model (host.c load_model) against the FM-1, as tools/preset_prof.py measured it (written by",
           f"# preset_prof.py --scenario from {Path(csv_path).name}): each preset on T2 of a new project, its chord held",
           f"# (preset_prof.py CHORD), the model's mean within {TOL} points of the FM-1's. The guard sees no load here (load",
           "# 0): the model alone.", "wait 300", "load 0"]
    for name in names:
        ns = sorted(n for (p, n) in rows if p == name)
        if not ns:
            sys.exit(f"preset_prof: no FM-1 rows for {name!r}")
        out += [f"# {name} ({eng[name]}): " + " / ".join(f"x{n} {float(rows[(name, n)]['fm1_all']):.0f}" for n in ns),
                f"preset {TRK + 1} {name.replace(' ', '_')} {eng[name]}", "wait 50"]
        for n in ns:
            r, ch = rows[(name, n)], CHORD[:n]
            want = float(r["fm1_all"])
            out += [f"play {TRK + 1} {x} on 100" for x in ch]
            out += ["wait 300", f"meter {int(1500)}", f"expect voices {r['voices']}",
                    f"expect model_avg >{int(want - TOL)}", f"expect model_avg <{int(want + TOL + 1)}"]
            out += [f"play {TRK + 1} {x} off" for x in ch]
            out += ["wait 2500"]
    Path(ROOT / "tests" / "scenarios" / "loadmodel.xy").write_text("\n".join(out) + "\n")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--engines")
    ap.add_argument("--names")
    ap.add_argument("--voices", default="1,4,8")
    ap.add_argument("--hold", type=float, default=1.5)
    ap.add_argument("--host-only", action="store_true")
    ap.add_argument("--device-only", action="store_true")
    ap.add_argument("--out", default=str(ROOT / "build" / "preset_prof"))
    ap.add_argument("--scenario")
    a = ap.parse_args()
    if a.scenario:
        scenario(a.out + ".csv", a.scenario.split(","))
        return
    rows = presets()
    if a.engines:
        rows = [r for r in rows if r[2] in a.engines.split(",")]
    if a.names:
        rows = [r for r in rows if r[1] in a.names.split(",")]
    voices = [int(v) for v in a.voices.split(",")]
    hm = {} if a.device_only else host(rows, voices, a.hold)
    idle, dv = (None, {}) if a.host_only else device(rows, voices, a.hold)
    hdr = ["preset", "engine", "notes", "voices", "fm1_all", "fm1_worst", "fm1_engine", "fm1_slowest_voice",
           "guard", "drops", "model_mean", "model_peak"]
    table = []
    for k, name, eng in rows:
        for n in voices:
            d, h = dv.get((k, n), {}), hm.get((k, n), {})
            table.append([name, eng, n, d.get("voices", h.get("voices", "")),
                          *(f"{d[x]:.1f}" if x in d else "" for x in ("all", "worst", "eng", "vworst")),
                          d.get("guard", ""), d.get("drops", ""),
                          *(f"{h[x]:.1f}" if x in h else "" for x in ("mean", "peak"))])
    with open(a.out + ".csv", "w", newline="") as f:
        csv.writer(f).writerows([hdr] + table)
    w = [max(len(str(r[i])) for r in [hdr] + table) for i in range(len(hdr))]
    text = []
    if idle:
        text.append(f"FM-1 idle: all {idle['all']:.1f} %, worst block {idle['worst']:.0f} %")
    text += ["  ".join(str(c).rjust(w[i]) for i, c in enumerate(r)) for r in [hdr] + table]
    Path(a.out + ".txt").write_text("\n".join(text) + "\n")
    print("\n".join(text))


if __name__ == "__main__":
    main()
