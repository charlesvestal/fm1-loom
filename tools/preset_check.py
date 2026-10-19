#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Checks the factory presets against the sound rules (docs/superpowers/specs/2026-10-19-sound-redesign.md 3.2), on
their role renders (tools/render_presets.sh --roles: each preset played as its role is used, through the default
chain: FX I delay, FX II lush, the master) and on their recipes (tools/gen_presets.py --json):
  rasp     the share of 2-6 kHz in the whole render: at most RASP dB (leads LEAD_RASP; fx, drum and loop free)
  boom     the share of 40-150 Hz: at most BOOM dB for anything but a bass or a drum
  level    the level of the frames that sound (within 20 dB of the loudest) within LEVEL dBFS (a stab, pluck, drum or
           bell 5 dB lower: short by nature); the peak under PEAK
  twin     the nearest other preset (tools/preset_distance.py's distance: spectrum and envelope, dB) at least TWIN
  sampler  no factory preset uses the sampler character (it is the user's)
Prints a line a preset that fails, then a count per rule; exits 1 when one fails (--report: always 0). The limits
come from the A/B renders Charles heard (E3: ramie, warp fifth, bowed linen) and are meant to be tuned by ear.
  tools/preset_check.py [DIR] [--report] [--all]       DIR: build/presets-roles; --all: every preset's numbers"""
import json
import subprocess
import sys
import wave
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
RASP, LEAD_RASP, BOOM = -9.0, -6.0, -12.0
LEVEL, PEAK, TWIN = (-37.0, -26.0), -1.0, 3.0     # (LEVEL: the old bank's median, -31.5, +-5 dB)
FREE_RASP = {"fx", "drum", "loop"}
SHORT = {"stab", "pluck", "drum", "bell"}           # (short by nature: their window 5 dB lower)


def load(p):
    with wave.open(str(p), "rb") as w:
        a = np.frombuffer(w.readframes(w.getnframes()), dtype="<i2").astype(np.float64) / 32768.0
        return a.reshape(-1, w.getnchannels())


def bands(x):
    m = x.mean(axis=1)
    nf, hop = 4096, 2048
    f = np.fft.rfftfreq(nf, 1.0 / 44100.0)
    p = np.zeros(len(f))
    for s in range(0, len(m) - nf, hop):
        p += np.abs(np.fft.rfft(m[s:s + nf] * np.hanning(nf))) ** 2
    tot = p[f > 40].sum() + 1e-20
    db = lambda lo, hi: 10.0 * np.log10(p[(f >= lo) & (f < hi)].sum() / tot + 1e-12)
    return db(40, 150), db(2000, 6000), db(6000, 16000)


def loudness(x):
    """the mean level of the frames that sound (within 20 dB of the loudest 2048-sample frame): dBFS"""
    m = x.mean(axis=1)
    e = np.array([np.mean(m[s:s + 2048] ** 2) for s in range(0, len(m) - 2048, 1024)]) + 1e-20
    on = e[e > e.max() * 0.01]
    return 10.0 * np.log10(np.mean(on))


def main(argv):
    report, every = "--report" in argv, "--all" in argv
    args = [a for a in argv if not a.startswith("--")]
    d = Path(args[0]) if args else ROOT / "build" / "presets-roles"
    recipes = json.loads(subprocess.run([sys.executable, str(ROOT / "tools" / "gen_presets.py"), "--json"],
                                        check=True, capture_output=True, text=True).stdout)
    dist = {}
    out = subprocess.run([sys.executable, str(ROOT / "tools" / "preset_distance.py"), str(d)], capture_output=True,
                         text=True).stdout
    for ln in out.splitlines():
        p = ln.split()
        if len(p) >= 3 and p[0][:2].isdigit():
            dist[p[0]] = (p[1], float(p[2]))
    fails, counts = [], {}
    for k, r in enumerate(recipes):
        tag = f"{k + 1:02d}-{r['engine']}-{r['name'].replace(' ', '_')}"
        wav = d / f"{tag}.wav"
        bad = []
        if r["char"][0] == "sampler":
            bad.append("sampler: on a factory preset")
        if not wav.exists():
            bad.append("no render")
        else:
            x = load(wav)
            boom, rasp, air = bands(x)
            rms = loudness(x)
            peak = 20.0 * np.log10(np.max(np.abs(x)) + 1e-12)
            lim = LEAD_RASP if r["role"] == "lead" else RASP
            if r["role"] not in FREE_RASP and rasp > lim:
                bad.append(f"rasp {rasp:.1f} dB > {lim:.0f}")
            if r["role"] not in ("bass", "drum") and boom > BOOM:
                bad.append(f"boom {boom:.1f} dB > {BOOM:.0f}")
            lo = LEVEL[0] - (5.0 if r["role"] in SHORT else 0.0)
            if not lo <= rms <= LEVEL[1]:
                bad.append(f"level {rms:.1f} dBFS")
            if peak > PEAK:
                bad.append(f"peak {peak:.1f} dBFS")
            if tag in dist and dist[tag][1] < TWIN:
                bad.append(f"twin of {dist[tag][0]} ({dist[tag][1]:.1f})")
            if every:
                print(f"  {tag:34s} {r['role']:8s} boom {boom:6.1f} rasp {rasp:6.1f} air {air:6.1f} rms {rms:6.1f} "
                      f"peak {peak:5.1f}")
        for b in bad:
            counts[b.split()[0]] = counts.get(b.split()[0], 0) + 1
        if bad:
            fails.append(f"{tag:34s} {r['role']:8s} " + "; ".join(bad))
    for f in fails:
        print(f)
    print(f"preset_check: {len(recipes) - len(fails)} of {len(recipes)} pass" +
          ("".join(f", {n} {k}" for k, n in sorted(counts.items())) if counts else ""))
    return 0 if report or not fails else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
