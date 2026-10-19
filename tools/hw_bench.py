#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""LOOM-8's hardware profiler: each kernel of app/dsp/bench.c timed on an attached FM-1 (a build with XY_BENCH=1:
  XY_BENCH=1 FM1B_POOL_SPARE=0 ./build.sh && python3 base/tools/fm1_install.py app/build/loom.fwsc --yes)
and on the host (tests/host/hw_bench.sh), side by side:
  dev t/s   the FM-1's fastest run, in 24 MHz ticks a sample
  % core    of one core's time at 44.1 kHz (544 ticks a sample): what one instance of the kernel costs, running
  host i/s  the host's instructions a sample (the portable measure of the work; host ns are latency, not work)
  t/i       ticks per host instruction: the factor that turns a host count into an FM-1 cost
The code kernels ("code N") do the same work over N copies of a ~0.6 KB function: their rise over "code 1" is the
XIP (flash) cache's cost of code size; "code ram" is one copy run from RAM. The FM-1's audio keeps running meanwhile
(its interrupt can land in a run: the fastest of up to 64 runs is taken). Writes build/hw_bench.json.
  tools/hw_bench.py [--reps N] [--label NAME]
Needs mido + python-rtmidi."""
import argparse
import json
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HDR = [0x7D, 0x46, 0x4C]
CMD = 27
TICKS_PER_SAMPLE = 24e6 / 44100.0


def ask(port, k, reps, timeout=2.0):
    import mido
    for _ in range(3):
        port.out.send(mido.Message("sysex", data=HDR + [CMD, k, reps]))
        t0 = time.time()
        while time.time() - t0 < timeout:
            for m in port.inp.iter_pending():
                if m.type == "sysex" and list(m.data[:4]) == HDR + [CMD]:
                    d = list(m.data[4:])
                    if d[0] != k:
                        continue
                    if k == 127 or len(d) < 3:
                        return {"count": d[1]}
                    z = d.index(0, 2)
                    name = bytes(d[2:z]).decode()
                    runs = d[z + 1]
                    v = [sum(d[z + 2 + 5 * i + j] << (7 * j) for j in range(5)) for i in range(4)]
                    return {"count": d[1], "name": name, "runs": runs, "min": v[0], "med": v[1], "max": v[2], "n": v[3]}
            time.sleep(0.002)
    raise TimeoutError("hw_bench: no BENCH reply (a build with XY_BENCH=1?)")


def host():
    out = subprocess.run(["sh", "tests/host/hw_bench.sh"], cwd=ROOT, check=True, capture_output=True, text=True).stdout
    h = {}
    for ln in out.splitlines():
        name, ns, ins = ln.split("\t")
        h[name] = {"ns": float(ns), "instr": float(ins)}
    return h


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--reps", type=int, default=64)
    ap.add_argument("--label", default="")
    a = ap.parse_args()
    sys.path.insert(0, str(ROOT / "tools"))
    from perf_read import Port
    port = Port()
    count = ask(port, 127, 1)["count"]
    dev = [ask(port, k, a.reps) for k in range(count)]
    h = host()
    rows = []
    print(f"{'kernel':12s} {'dev t/s':>8s} {'med':>7s} {'% core':>7s} {'host i/s':>9s} {'t/i':>6s}")
    for d in dev:
        tps, med = d["min"] / d["n"], d["med"] / d["n"]
        hi = h.get(d["name"], {}).get("instr", 0.0)
        r = {"name": d["name"], "ticks": tps, "ticks_med": med, "pct": 100.0 * tps / TICKS_PER_SAMPLE,
             "host_instr": hi, "host_ns": h.get(d["name"], {}).get("ns", 0.0), "runs": d["runs"]}
        rows.append(r)
        ti = f"{tps / hi:6.2f}" if hi else "     -"
        print(f"{d['name']:12s} {tps:8.2f} {med:7.2f} {r['pct']:6.2f}% {hi:9.1f} {ti}")
    base = next((r for r in rows if r["name"] == "code 1"), None)
    if base:
        print("\nthe code's size (0.6 KB a copy): the cost against code 1 (warm)")
        for r in rows:
            if r["name"].startswith("code "):
                print(f"  {r['name']:9s} {r['ticks'] / base['ticks']:5.2f}x")
    out = ROOT / "build" / "hw_bench.json"
    out.parent.mkdir(exist_ok=True)
    out.write_text(json.dumps({"label": a.label, "when": time.strftime("%Y-%m-%d %H:%M"), "rows": rows}, indent=1))
    print(f"\n{out}")


if __name__ == "__main__":
    main()
