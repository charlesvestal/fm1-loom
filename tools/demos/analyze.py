#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""A listening check by numbers for the demo mixdowns:
  tools/demos/analyze.py WAV ...                 the numbers
  tools/demos/analyze.py --master IN.wav OUT.wav  the simulator's output made a mixdown: +6 dB (the FM-1's codec
                                                  keeps 6 dB of headroom, Felucca's -6 dBFS ceiling: the host writes
                                                  what the codec gets, so LOOM-8's full scale is at -6 dBFS there)

Per file: length, peak (dBFS), samples at full scale (clipping), integrated loudness (LUFS, by ffmpeg's ebur128
when it is there; else an unweighted RMS), the share of the time the master limiter is at work (the output
within 0.5 dB of its ceiling, -1.9 dBFS: LOOM-8's limiter holds peaks at 0.8), and gaps: stretches of 250 ms or
more under -50 dBFS (the silence a song did not mean to have; the tail after the end is reported apart).
Needs numpy and scipy.
"""
import re
import shutil
import subprocess
import sys

import numpy as np
import scipy.io.wavfile as wf

CEIL = 0.8                                           # app/dsp/fx.c: the limiter's ceiling (in a mixdown, after +6 dB)
HEADROOM_DB = 6.0206                                 # engine.c: a mix of 1.0 is 2^22 of the 24-bit codec


def lufs(path):
    if not shutil.which("ffmpeg"):
        return None
    r = subprocess.run(["ffmpeg", "-nostats", "-i", path, "-af", "ebur128", "-f", "null", "-"],
                       capture_output=True, text=True)
    m = re.findall(r"I:\s+(-?[\d.]+) LUFS", r.stderr)
    return float(m[-1]) if m else None


def check(path):
    sr, x = wf.read(path)
    x = x.astype(np.float64) / 32768.0
    a = np.abs(x).max(axis=1)
    n = len(a)
    peak = a.max()
    clip = int((a >= 0.999).sum())
    hop = int(sr * 0.01)
    blocks = a[: n // hop * hop].reshape(-1, hop).max(axis=1)
    quiet = blocks < 10 ** (-50 / 20)
    # the end: from the last loud block on
    loud = np.flatnonzero(~quiet)
    end = loud[-1] + 1 if len(loud) else 0
    gaps, run = [], 0
    for i, q in enumerate(quiet[:max(0, end - 150)]):         # (not the last 1.5 s: the tails fading)
        run = run + 1 if q else 0
        if run == 25:
            gaps.append(i - 24)
    lim = float((blocks[:end] > CEIL * 10 ** (-0.5 / 20)).mean()) if end else 0.0
    rms = 20 * np.log10(np.sqrt((x[: end * hop] ** 2).mean()) + 1e-12)
    L = lufs(path)
    print(f"{path}: {n / sr:.1f} s (sound to {end / 100:.1f} s), peak {20 * np.log10(peak + 1e-12):.1f} dBFS, "
          f"{clip} clipped, {'%.1f LUFS' % L if L is not None else 'LUFS n/a'}, RMS {rms:.1f} dBFS, "
          f"limiter at work {lim * 100:.0f} % of the time, "
          + (f"gaps at {', '.join('%.2f s' % (g / 100) for g in gaps)}" if gaps else "no gaps"))
    return L


def master(src, dst):
    sr, x = wf.read(src)
    y = np.clip(x.astype(np.float64) * 10 ** (HEADROOM_DB / 20), -32767, 32767)
    wf.write(dst, sr, np.round(y).astype(np.int16))


if __name__ == "__main__":
    if sys.argv[1:2] == ["--master"]:
        master(sys.argv[2], sys.argv[3])
        sys.exit(0)
    for p in sys.argv[1:]:
        check(p)
