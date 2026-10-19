#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""LOOM-8's factory samples: drums and a noise sweep made here, procedurally (mono at 22050 Hz); the
recorded instruments (app/samples/, tools/prep_recordings.py: VSCO 2 CE, CC0); a vinyl loop made here; the four
breaks (app/samples/: the slice engine's). Each at its own rate, IMA ADPCM with a checkpoint every 64
samples (tools/sampleio.py), into a header the firmware compiles in (dsp/sample.c). Deterministic: the same bytes every run.

  gen_samples.py OUT.h [WAVDIR]      WAVDIR: also each sample as a 16-bit WAV (to listen to)
"""
import math
import struct
import sys
import wave
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import sampleio as sio  # noqa: E402

SR = sio.RATE
TWO_PI = 2.0 * math.pi


class Rng:
    """a small LCG: the same noise everywhere"""
    def __init__(self, seed):
        self.s = seed & 0xFFFFFFFF

    def __call__(self):
        self.s = (self.s * 1664525 + 1013904223) & 0xFFFFFFFF
        return (self.s / 2147483648.0) - 1.0


def secs(t):
    return int(round(t * SR))


def onepole(x, hz, hp=False):
    a = 1.0 - math.exp(-TWO_PI * hz / SR)
    y, out = 0.0, []
    for v in x:
        y += a * (v - y)
        out.append(v - y if hp else y)
    return out


def bandpass(x, hz_of, q):
    """a state-variable band-pass whose centre may move (hz_of: index -> Hz)"""
    lo = bp = 0.0
    out = []
    for i, v in enumerate(x):
        f = 2.0 * math.sin(math.pi * min(hz_of(i), SR * 0.24) / SR)
        hp = v - lo - bp / q
        bp += f * hp
        lo += f * bp
        out.append(bp)
    return out


def kick():
    n, ph, out, r = secs(0.30), 0.0, [], Rng(1)
    for i in range(n):
        t = i / SR
        hz = 48.0 + 110.0 * math.exp(-t / 0.035)
        ph += hz / SR
        body = math.sin(TWO_PI * ph) * math.exp(-t / 0.11)
        click = r() * math.exp(-t / 0.002) * 0.5
        out.append(math.tanh(1.6 * body) + click)
    return out


def snare():
    n, r, out = secs(0.25), Rng(2), []
    noise = bandpass([r() for _ in range(n)], lambda i: 3000.0, 0.8)
    p1 = p2 = 0.0
    for i in range(n):
        t = i / SR
        p1 += 185.0 * (1.0 + 0.3 * math.exp(-t / 0.01)) / SR
        p2 += 330.0 * (1.0 + 0.3 * math.exp(-t / 0.01)) / SR
        body = (0.6 * math.sin(TWO_PI * p1) + 0.4 * math.sin(TWO_PI * p2)) * math.exp(-t / 0.045)
        out.append(body + 1.6 * noise[i] * math.exp(-t / 0.07))
    return out


def hat(length=0.10, tau=0.025, seed=3):
    n, r, out = secs(length), Rng(seed), []
    hz = [205.3, 304.4, 369.6, 522.7, 540.0, 800.0]
    ph = [0.0] * 6
    for i in range(n):
        m = 0.0
        for k in range(6):
            ph[k] = (ph[k] + hz[k] * 1.9 / SR) % 1.0
            m += 1.0 if ph[k] < 0.5 else -1.0
        out.append((m / 6.0) * 0.6 + r() * 0.5)
    out = onepole(onepole(out, 6000.0, hp=True), 6000.0, hp=True)
    return [v * math.exp(-(i / SR) / tau) for i, v in enumerate(out)]


def clap():
    n, r, out = secs(0.25), Rng(4), []
    noise = bandpass([r() for _ in range(n)], lambda i: 1100.0, 1.6)
    starts = [0.0, 0.011, 0.022, 0.033]
    for i in range(n):
        t = i / SR
        e = sum(math.exp(-(t - s) / 0.006) for s in starts if t >= s)
        tail = math.exp(-(t - 0.033) / 0.07) if t >= 0.033 else 0.0
        out.append(noise[i] * (e + 0.8 * tail))
    return out


def vinyl():
    """a record's run-out: crackle (sparse clicks, each a few samples, bright and dull) over a low hiss and a little
    rumble, a second that loops (each click whole inside it)"""
    n, r = secs(1.0), Rng(8)
    hiss = onepole(onepole([r() for _ in range(n)], 3500.0), 3500.0)
    rumble = onepole(onepole([r() for _ in range(n)], 60.0), 60.0)
    out = [0.25 * h + 1.5 * u for h, u in zip(hiss, rumble)]
    i = secs(0.004)
    while i < n - secs(0.004):
        a, tau = r() * (1.0 if r() > -0.6 else 3.0), (0.00015 if r() > -0.3 else 0.0006)
        for k in range(secs(0.004)):
            out[i + k] += a * math.exp(-(k / SR) / tau)
        i += int(secs(0.012) + (r() + 1.0) * secs(0.045))
    return out, (0, n), 60


def sweep():
    """noise through a resonant band-pass rising from 200 Hz to 8 kHz, swelling in and falling away"""
    n, r = secs(0.60), Rng(7)
    x = [r() for _ in range(n)]
    y = bandpass(x, lambda i: 200.0 * math.pow(40.0, i / n), 6.0)
    return [v * math.sin(math.pi * i / n) ** 0.7 for i, v in enumerate(y)]


BREAK_ROOT = 53                                      # F3, the lowest key: slice 1



SAMPLES = [("kick", kick), ("snare", snare), ("hat", hat), ("clap", clap), ("sweep", sweep)]
# recorded instruments (app/samples/, tools/prep_recordings.py: from VSCO 2 CE, CC0), after the made drums: name, file,
# rate the bank keeps it at, root; a sustained one's loop comes from its file ('smpl')
INSTRUMENTS = [("piano", "piano.wav", 16000, 60), ("upright", "upright.wav", 11025, 28),
               ("strings", "strings.wav", 16000, 60), ("brass", "brass.wav", 16000, 62),
               ("flute", "flute.wav", 16000, 72), ("orch hit", "orch_hit.wav", 16000, 60)]
MADE_AFTER = [("vinyl", vinyl, 11025)]               # made here, after the instruments, at their own rate
# recordings (app/samples/: one bar each, mono, cut from the source loops), after the made ones: name, file, rate the
# bank keeps it at, bars (a break: the slice engine plays it at the song's tempo, slice 1 on BREAK_ROOT). The breaks:
# free loops posted on Reddit, their author's words "You have my full permission to use these loops in commercial
# or personal projects" (LICENSING.md has the source)
RECORDINGS = [("classic", "classic.wav", 16000, 1), ("heat", "heat.wav", 16000, 1), ("choke", "choke.wav", 16000, 1),
              ("ecstasy", "ecstasy.wav", 16000, 1)]
BUDGET = 184 * 1024                                  # B: the image's room for the factory bank
SAMPLE_DIR = Path(__file__).resolve().parents[1] / "app" / "samples"


def names():
    """the factory bank's names, in its order"""
    return ([n for n, _ in SAMPLES] + [i[0] for i in INSTRUMENTS] + [m[0] for m in MADE_AFTER] +
            [r[0] for r in RECORDINGS])


def made(name, fn, rate):
    v = fn()
    loop, root, bars = None, 60, 0
    if isinstance(v, tuple):
        v, loop, root, *more = v
        bars = more[0] if more else 0
    if rate != SR:
        v = sio.resample(v, SR, rate)
        loop = loop and (loop[0] * rate // SR, min(len(v), loop[1] * rate // SR))
    return (name, sio.to_int16(v), root, loop, bars, rate)


def build():
    out = [made(name, fn, SR) for name, fn in SAMPLES]
    for name, f, rate, root in INSTRUMENTS:
        sr, x, loop = sio.read_any_wav(SAMPLE_DIR / f)
        if sr != rate:
            x = sio.resample(x, sr, rate)
            loop = loop and (loop[0] * rate // sr, loop[1] * rate // sr)
        out.append((name, sio.to_int16(sio.emphasis(x)), root, loop and (loop[0], loop[1] + 1), 0, rate))
    out += [made(name, fn, rate) for name, fn, rate in MADE_AFTER]
    for name, f, rate, bars in RECORDINGS:
        sr, x, _ = sio.read_any_wav(SAMPLE_DIR / f)
        out.append((name, sio.to_int16(sio.emphasis(sio.resample(x, sr, rate))), BREAK_ROOT if bars else 60, None,
                    bars, rate))
    return out


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    smp = build()
    adp_all, ck_all, rows = bytearray(), [], []
    for name, s, root, loop, bars, rate in smp:
        adp, ck = sio.ima_encode(s)
        ls, le = loop if loop else (0, 0)
        rows.append((name, len(adp_all), len(ck_all), len(s), ls, le, root, bars, rate))
        adp_all += adp
        ck_all += ck
    total = len(adp_all) + 4 * len(ck_all)
    if total > BUDGET:
        sys.exit(f"gen_samples: {total} B, over the {BUDGET // 1024} KB the factory bank may take (the image's room)")
    lines = ["/* generated by tools/gen_samples.py: LOOM-8's factory samples (made procedurally, then recordings: the",
             f" * breaks), IMA ADPCM each at its rate, a checkpoint every {sio.BLK} samples; {len(adp_all)} B of ADPCM + "
             f"{4 * len(ck_all)} B of checkpoints */",
             "#pragma once",
             f"#define SMP_NFACT {len(rows)}",
             f"#define SMP_FACT_BREAK {[r[0] for r in rows].index(RECORDINGS[0][0])}   /* the first break: the slice "
             "engine's sample */",
             f"static const uint8_t SMP_FACT_ADP[{len(adp_all)}] = {{"]
    for i in range(0, len(adp_all), 24):
        lines.append("    " + ", ".join(str(b) for b in adp_all[i:i + 24]) + ",")
    lines.append("};")
    lines.append(f"static const uint32_t SMP_FACT_CK[{len(ck_all)}] = {{")
    for i in range(0, len(ck_all), 8):
        lines.append("    " + ", ".join(f"0x{c:08X}u" for c in ck_all[i:i + 8]) + ",")
    lines.append("};")
    lines.append("/* name, ADPCM offset, checkpoint index, samples, loop start, loop end (0: none), root, bars (0: "
                 "not a loop of bars), rate */")
    lines.append("static const struct { const char *name; uint32_t adp, ck, n, ls, le; uint8_t root, bars; "
                 "uint16_t rate; } SMP_FACT[SMP_NFACT] = {")
    for name, a, c, n, ls, le, root, bars, rate in rows:
        lines.append(f'    {{"{name}", {a}u, {c}u, {n}u, {ls}u, {le}u, {root}, {bars}, {rate}}},')
    lines.append("};")
    Path(sys.argv[1]).parent.mkdir(parents=True, exist_ok=True)
    Path(sys.argv[1]).write_text("\n".join(lines) + "\n")
    if len(sys.argv) > 2:
        d = Path(sys.argv[2])
        d.mkdir(parents=True, exist_ok=True)
        for name, s, root, loop, bars, rate in smp:
            with wave.open(str(d / f"{name}.wav"), "wb") as w:
                w.setnchannels(1)
                w.setsampwidth(2)
                w.setframerate(rate)
                w.writeframes(struct.pack("<%dh" % len(s), *s))
    print(f"samples: {len(rows)}, {total} B")


if __name__ == "__main__":
    main()
