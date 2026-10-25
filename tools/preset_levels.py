#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""The presets' renders (tools/render_presets.sh) measured: per preset the voices' dry peak and RMS
(OUTDIR/dry.txt, from tools/preset_scripts.c: the levels to balance by) and its deviation from the mean,
then the WAV's (through the FX and the master's compressor and limiter) peak, the RMS of the phrase (the
first 2.5 s: three notes and the chord) and the loudest 100 ms, in dBFS, and whether it clips or is
silent. Writes OUTDIR/levels.txt (the contact list: one line a preset) and OUTDIR/index.html (the same
with a player for each). Exits 1 when a render clips, is silent, or is missing.
  preset_levels.py OUTDIR NAME..."""
import array
import html
import math
import sys
import wave
from pathlib import Path


def measure(path):
    with wave.open(str(path), "rb") as w:
        n, rate = w.getnframes(), w.getframerate()
        a = array.array("h", w.readframes(n))
    if sys.byteorder != "little":
        a.byteswap()
    peak = max((abs(x) for x in a), default=0)
    clips = sum(1 for x in a if abs(x) >= 32767)
    m = min(len(a) // 2, int(2.5 * rate))
    acc = sum(float(a[2 * i]) ** 2 + float(a[2 * i + 1]) ** 2 for i in range(m))
    rms = math.sqrt(acc / max(2 * m, 1)) / 32768.0
    win, loud = int(0.1 * rate), 0.0
    for s in range(0, len(a) // 2 - win, win // 2):
        e = sum(float(a[2 * i]) ** 2 + float(a[2 * i + 1]) ** 2 for i in range(s, s + win, 4))
        loud = max(loud, math.sqrt(e / (2 * (win // 4))) / 32768.0)
    db = lambda x: 20.0 * math.log10(x) if x > 0 else -200.0
    return db(peak / 32768.0), db(rms), db(loud), clips, len(a) // 2 / rate


def main():
    out, names = Path(sys.argv[1]), sys.argv[2:]
    drymap = {}
    for ln in (out / "dry.txt").read_text().splitlines():
        f = ln.split()
        drymap[f[0]] = (float(f[1]), float(f[2]))
    rows, bad = [], 0
    for nm in names:
        p = out / (nm + ".wav")
        if not p.exists():
            rows.append((nm, None))
            bad += 1
            continue
        r = measure(p) + drymap.get(nm, (-200.0, -200.0))
        rows.append((nm, r))
        if r[3] or r[1] < -60.0 or r[6] < -60.0 or r[5] > 0.0:
            bad += 1
    rmss = [r[6] for _, r in rows if r]
    mean = sum(rmss) / len(rmss) if rmss else 0.0
    lines = ["%-32s %18s   %21s" % ("", "dry (the voices)", "out (FX, master)"),
             "%-32s %6s %6s %5s   %6s %6s %6s  %s" % ("preset", "peak", "rms", "dev", "peak", "rms", "loud", "notes")]
    for nm, r in rows:
        if not r:
            lines.append("%-32s missing" % nm)
            continue
        flag = ("CLIPS %d " % r[3] if r[3] else "") + ("SILENT " if r[1] < -60.0 or r[6] < -60.0 else "") + \
               ("DRY OVER 0 dBFS" if r[5] > 0.0 else "")
        lines.append("%-32s %6.1f %6.1f %+5.1f   %6.1f %6.1f %6.1f  %s" % (nm, r[5], r[6], r[6] - mean, r[0], r[1], r[2], flag))
    lines.append("dry: mean rms %.1f dBFS, spread %+.1f .. %+.1f dB" % (mean, min(rmss) - mean, max(rmss) - mean))
    (out / "levels.txt").write_text("\n".join(lines) + "\n")
    print("\n".join(lines))
    with open(out / "index.html", "w") as f:
        f.write("<!doctype html><meta charset=utf-8><title>LOOM-8 presets</title>"
                "<style>body{font:14px system-ui;margin:16px}td{padding:2px 8px}"
                "audio{height:28px}</style><h1>LOOM-8 factory presets</h1>"
                "<p>Three notes and a chord on track 2 of a new project (FX I delay, FX II reverb). dBFS.</p>"
                "<table><tr><th>preset<th>dry rms<th>out peak<th>out rms<th>play</tr>\n")
        for nm, r in rows:
            if r:
                f.write("<tr><td>%s<td>%.1f<td>%.1f<td>%.1f<td><audio controls preload=none src=\"%s.wav\"></audio></tr>\n"
                        % (html.escape(nm), r[6], r[0], r[1], html.escape(nm)))
        f.write("</table>\n")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
