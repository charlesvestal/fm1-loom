#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Juno-106 patches on LOOM-8's knit: a bank of the instrument's patch messages (F0 41 30 0n pp, sixteen sliders, SW1,
SW2, F7; 24 bytes each, as Juno librarians save them) turned into knit sounds.

  tools/juno_import.py list BANK.syx [--names FILE]        each patch and the knit sound it becomes
  tools/juno_import.py script BANK.syx PATCH [--track T]    the host simulator's lines for one (PATCH: A11..B88 or 1..128)
  tools/juno_import.py pack BANK.syx PACK.syx --patches A11,A12,... [--slot N] [--names FILE]
                                                            up to 64 patches as a LOOM-8 preset pack (U(N) onward,
                                                            default U1), named as --names gives or J106 A11; send it
                                                            with tools/loom_presets.py send PACK.syx
  tools/juno_import.py render BANK.syx OUT [--patches A11,B88 ...] [--names FILE]
                                                            each patch played in the host simulator (a chord, then a
                                                            line), OUT/<label>.wav and .m4a, for listening

Bring your own bank: Roland's patches are never committed or published here (keep them in ~/private-stash). --names: a
text file, one name a line in program order (A11 first), for the listing and the files' names.

How a patch maps (knit's page: the 106's control):
  Engine    wave: the pulse when PULSE is on (its width: PWM, manual or the LFO on it), else the saw; one voice, no
            detune (the 106's DCOs are steady)
  Osc 2     the saw at unison when both SAW and PULSE are on (osc 2 level full), else off
  Levels    sub: SUB (a square an octave down: the Mod page's sub type square); noise: NOISE
  Envelope  the one ADSR into both the amp and the filter envelope (GATE mode: an organ's gate)
  Filter    ladder 24 dB; cutoff, env (its polarity: below 64 inverted), keytrack and resonance through the curves below
  LFO       one destination: the largest of DCO (vibrato: wobble with the LFO's delay as its fade-in, while it is
            within wobble's 100 cents; else a wave on the pitch), VCF (a wave on the cutoff) and PWM by the LFO (a
            wave on osc 1's width); the 106's LFO moves all three at once, knit's one
  Play 2    low cut: HPF 2 and 3 (about 240 and 720 Hz); HPF 0's bass lift is not made (flat)
  Insert 2  the juno chorus, I or II, when the patch has it on
  Play      octave: the range (16' 8' 4'); volume: the VCA level (about -6 .. +6 dB about 64)

The curves (Hz, octaves, seconds) are Saadet's (github.com/keremimo/saadet firmware/src/eng_j106.c, Kerem Kilic,
MPL-2.0), from the 106's service manual and published measurements: the cutoff 5.53 Hz x 2^(counts / 1143) with 128
counts a slider step, the envelope's full depth 14.2 octaves, the LFO's on the cutoff 3.6, vibrato up to half an
octave (a square law), the LFO 0.1 .. 30 Hz, the attack 1.5 ms .. 3 s and the decay and release 1.5 ms .. 12 s (to 1 %)
on a square law, the high-pass about 240 / 720 Hz. Knit's own: the cutoff 50 Hz x 2^(v x 8.64 / 127), the
envelope's depth +-6 octaves, an envelope time 1 ms x 2^(v x 13.29 / 127) (the attack to full, the decay and release
to -60 dB), the LFO's rate the same 0.1 .. 30 Hz above its eight synced values, the low cut 20 Hz x 2^(v x 6.64 / 127).
"""
import math
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FRAME = 24
(LFO_RATE, LFO_DELAY, DCO_LFO, DCO_PWM, NOISE, VCF_FREQ, VCF_RES, VCF_ENV, VCF_LFO, VCF_KYBD, VCA_LEVEL, ENV_A, ENV_D,
 ENV_S, ENV_R, SUB, SW1, SW2) = range(18)


def label(k):
    """the instrument's own label of program k: A11 .. A88, B11 .. B88"""
    return f"{'B' if k & 64 else 'A'}{1 + (k >> 3 & 7)}{1 + (k & 7)}"


def program_of(s):
    s = s.strip().upper()
    if s.isdigit():
        return int(s) - 1
    if len(s) == 3 and s[0] in "AB" and s[1] in "12345678" and s[2] in "12345678":
        return (64 if s[0] == "B" else 0) + (int(s[1]) - 1) * 8 + int(s[2]) - 1
    sys.exit(f"juno_import: no patch {s} (A11..B88 or 1..128)")


def read_bank(path):
    """{program: 18 bytes} from a file of patch messages (others in it are skipped)"""
    data = Path(path).read_bytes()
    out, i = {}, 0
    while True:
        i = data.find(b"\xf0\x41\x30", i)
        if i < 0 or i + FRAME > len(data):
            break
        m = data[i:i + FRAME]
        if m[-1] == 0xF7 and (m[3] & 0xF0) == 0 and all(b < 128 for b in m[4:-1]):
            out[m[4]] = bytes(m[5:23])
            i += FRAME
        else:
            i += 1
    if not out:
        sys.exit(f"juno_import: {path} holds no Juno-106 patch messages (F0 41 30 0n pp, 16 sliders, SW1, SW2, F7)")
    return out


def clamp(v, lo=0, hi=127):
    return max(lo, min(hi, int(round(v))))


# ---- the curves (the module's docstring says where each comes from)
J_F0, J_OCT = 5.528709, 1143.0


def j_cut_hz(v):
    return J_F0 * 2.0 ** (v * 128.0 / J_OCT)


def knit_cut(hz):
    return clamp(math.log2(max(hz, 1e-3) / 50.0) * 127.0 / 8.64385619)


def j_time(v, most):
    x = v / 127.0
    return 0.0015 + most * x * x


def knit_time(secs):
    return clamp(math.log2(max(secs, 0.001) / 0.001) * 127.0 / 13.2877124, 0, 146)


def convert(p):
    """a patch's 18 bytes -> [(knit parameter, value)] for track lines, in order; and notes on what did not map"""
    sw1, sw2 = p[SW1], p[SW2]
    rng = 0 if sw1 & 1 else 1 if sw1 & 2 else 2 if sw1 & 4 else 1
    pulse, saw = bool(sw1 & 8), bool(sw1 & 16)
    chorus = 0 if sw1 & 32 else (1 if sw1 & 64 else 2)          # 0 off, 1 I, 2 II
    pwm_manual, env_inv, gate = bool(sw2 & 1), bool(sw2 & 2), bool(sw2 & 4)
    hpf = 3 - ((sw2 >> 3) & 3)
    notes = []
    out: list = [("engine", "knit")]
    # the oscillators: osc 1 the pulse when it is on (its width the wave knob: 64 square .. 127 a 10 % pulse), else
    # the saw; osc 2 the saw beside a pulse
    pwm = p[DCO_PWM]
    if pulse:
        wave = 64 + pwm * 0.5 if pwm_manual else 64 + pwm * 0.25   # (by the LFO: about its middle, the LFO around it)
        out.append(("e1", clamp(wave)))
    else:
        out.append(("e1", 0))
    out += [("e2", 0), ("e3", 1), ("e4", 0), ("x1", 64), ("fine", 64), ("kanalog", 0)]
    if pulse and saw:
        out += [("o2wave", "saw"), ("x2", 127)]
    else:
        out += [("o2wave", "same"), ("x2", 0)]
    if not pulse and not saw:
        notes.append("no saw or pulse: knit's osc 1 cannot be turned off, its saw plays beside the sub and noise")
    out += [("x3", p[SUB]), ("ksub", 1), ("x4", clamp(p[NOISE] * 0.8))]
    # the envelope, one ADSR for both
    if gate:
        amp = (0, 0, 127, 0)
    else:
        amp = (knit_time(j_time(p[ENV_A], 3.0)), knit_time(j_time(p[ENV_D], 12.0) * 1.5),
               p[ENV_S], knit_time(j_time(p[ENV_R], 12.0) * 1.5))
    out += [("a", amp[0]), ("d", amp[1]), ("s", amp[2]), ("r", amp[3])]
    out += [("fa", knit_time(j_time(p[ENV_A], 3.0))), ("fd", knit_time(j_time(p[ENV_D], 12.0) * 1.5)),
            ("fs", p[ENV_S]), ("fr", knit_time(j_time(p[ENV_R], 12.0) * 1.5))]
    # the filter
    env_oct = p[VCF_ENV] * 16383.0 / 128.0 / J_OCT * (-1 if env_inv else 1)
    if abs(env_oct) > 6.0:
        notes.append(f"filter env {env_oct:+.1f} oct: knit's reaches 6")
    res = p[VCF_RES] / 127.0
    out += [("ftype", "ladder"), ("cut", knit_cut(j_cut_hz(p[VCF_FREQ]))), ("res", clamp(127 * min(1.0, 1.05 * res ** 1.5))),
            ("fenv", clamp(64 + env_oct * 64.0 / 6.0)), ("ktrk", p[VCF_KYBD])]
    # the LFO: one destination
    vib_semis = (p[DCO_LFO] / 127.0) ** 2 * 6.0
    vcf_oct = p[VCF_LFO] * 8191.0 / 256.0 / J_OCT
    pwm_lfo = pulse and not pwm_manual and pwm > 0
    cand = [(vib_semis / 6.0, "vib"), (vcf_oct / 3.6, "vcf"), ((pwm / 127.0) if pwm_lfo else 0.0, "pwm")]
    cand.sort(reverse=True)
    rate = clamp(8 + p[LFO_RATE] * 119.0 / 127.0, 8, 127)          # (knit: 0.1 .. 30 Hz past its eight synced)
    delay = (p[LFO_DELAY] / 127.0) ** 2 * 3.0                    # seconds (half waiting, half fading in)
    used = [c for c in cand if c[0] > 0.02]
    if used and len(used) > 1:
        notes.append("LFO on " + " and ".join(n for _, n in used[1:]) + " too: not made (knit's LFO has one target)")
    kind = used[0][1] if used else None
    if kind == "vib" and vib_semis <= 1.0:
        out += [("ltype", "wobble"), ("lspeed", rate), ("lamt", clamp(64 + vib_semis * 100.0 * 63.0 / 100.0)),
                ("ldest", 64), ("lx", clamp(64 - delay * 64.0 / 3.0) if delay > 0.01 else 64)]
    elif kind:
        dest, param, amt = {"vib": ("voice free", 1, 64 + vib_semis * 63.0 / 12.0),
                            "vcf": ("filter free", 0, 64 + (vcf_oct * 127.0 / 8.64385619) * 63.0 / 127.0),
                            "pwm": ("synth free", 0, 64 + (pwm * 0.25) * 63.0 / 127.0)}[kind]
        out += [("ltype", "wave"), ("lspeed", rate), ("lamt", clamp(amt)), ("ldest", dest), ("lx", param),
                ("lx2", "sine")]
        if delay > 0.05:
            notes.append(f"LFO delay {delay:.1f} s: not made (a wave LFO has no fade-in)")
    else:
        out += [("ltype", "off")]
    # the rest
    out += [("hpf", {0: 0, 1: 0, 2: 69, 3: 99}[hpf]), ("oct", 1 + rng), ("vmode", "poly"),
            ("volume", clamp(90 * 2.0 ** ((p[VCA_LEVEL] - 64) / 127.0)))]
    if hpf == 0:
        notes.append("HPF 0's bass lift: not made")
    if chorus:
        out += [("itype", "juno"), ("i1", chorus - 1), ("i2", 70), ("s2mix", 80)]
    else:
        out += [("itype", "off")]
    return out, notes


def names(path):
    if not path:
        return {}
    lines = [l.strip() for l in Path(path).read_text().splitlines() if l.strip()]
    return {k: n for k, n in enumerate(lines)}


def describe(p):
    sw1, sw2 = p[SW1], p[SW2]
    w = "+".join(x for x, b in (("saw", sw1 & 16), ("pulse", sw1 & 8)) if b) or "none"
    ch = "off" if sw1 & 32 else ("I" if sw1 & 64 else "II")
    return (f"{w:9s} sub {p[SUB]:3d} noise {p[NOISE]:3d}  VCF {p[VCF_FREQ]:3d} res {p[VCF_RES]:3d} env {p[VCF_ENV]:3d}"
            f"{'-' if sw2 & 2 else '+'}  ADSR {p[ENV_A]:3d} {p[ENV_D]:3d} {p[ENV_S]:3d} {p[ENV_R]:3d}"
            f"{' gate' if sw2 & 4 else ''}  chorus {ch}")


def lines(p, track=2):
    out, notes = convert(p)
    return [f"knob {track} {k} {v}" for k, v in out], notes


def main():
    a = sys.argv[1:]
    if len(a) < 2 or a[0] not in ("list", "script", "render", "pack"):
        sys.exit(__doc__)
    opt = {}
    for flag in ("--names", "--track", "--patches", "--slot"):
        if flag in a:
            i = a.index(flag)
            opt[flag] = a[i + 1]
            del a[i:i + 2]
    bank, nm = read_bank(a[1]), names(opt.get("--names"))
    if a[0] == "list":
        for k in sorted(bank):
            _, notes = convert(bank[k])
            print(f"{label(k)} {nm.get(k, ''):13s} {describe(bank[k])}")
            for n in notes:
                print(f"      {n}")
        return
    if a[0] == "script":
        k = program_of(a[2])
        if k not in bank:
            sys.exit(f"juno_import: {label(k)} is not in {a[1]}")
        ls, notes = lines(bank[k], int(opt.get("--track", 2)))
        for n in notes:
            print(f"# {n}")
        print("\n".join(ls))
        return
    if a[0] == "pack":
        import loom_presets
        progs = [program_of(s) for s in opt.get("--patches", "").split(",") if s]
        slot = int(opt.get("--slot", 1))
        if len(a) < 3 or not progs or slot < 1 or slot - 1 + len(progs) > 64:
            sys.exit("juno_import: pack BANK.syx PACK.syx --patches A11,... (1 to 64, in U1..U64)")
        out = bytearray()
        for i, k in enumerate(progs):
            if k not in bank:
                sys.exit(f"juno_import: {label(k)} is not in {a[1]}")
            ls, _ = lines(bank[k])
            ls, pk, vol = loom_presets.level_fit(ls)   # (as loud as the factory pads on a chord: the 106's
                                                          # levels, all summed, clipped LOOM-8's output)
            fmt, data = loom_presets.preset_from_lines(nm.get(k) or f"J106 {label(k)}", ls)
            out += loom_presets.frame(slot - 1 + i, fmt, data)
            print(f"U{slot + i} {loom_presets.name_of(data):12s} {label(k)}  chord {pk:+.1f} dB -> volume {vol}")
        Path(a[2]).write_bytes(bytes(out))
        print(f"{a[2]}: {len(progs)} presets; send: tools/loom_presets.py send {a[2]}")
        return
    out = Path(a[2]).resolve()
    out.mkdir(parents=True, exist_ok=True)
    progs = [program_of(s) for s in opt["--patches"].split(",")] if "--patches" in opt else sorted(bank)
    host = ROOT / "app/build/host/loom_host"
    if not host.exists():
        sys.exit("juno_import: build the host simulator first (./build.sh --host)")
    py = os.environ.get("PYTHON", "/opt/homebrew/bin/python3.14")
    for k in progs:
        ls, _ = lines(bank[k])
        name = label(k) + (("_" + "".join(c if c.isalnum() else "_" for c in nm[k].lower())) if k in nm else "")
        script = ["wait 2500", "master 4096", "knob 2 send1 0", "knob 2 send2 0", "knob 2 level 110", *ls, "wait 300",
                  "wav take.wav"]
        for n in ("C4", "E4", "G4", "B4"):                       # a chord held, let go
            script.append(f"play 2 {n} on 110")
        script += ["wait 1600"] + [f"play 2 {n} off" for n in ("C4", "E4", "G4", "B4")] + ["wait 900"]
        for n in ("C3", "G3", "C4", "D#4", "G4", "C5"):           # a line
            script += [f"play 2 {n} on 110", "wait 180", f"play 2 {n} off", "wait 70"]
        script += ["wait 1500", "wavstop"]
        d = out / ".run"
        d.mkdir(exist_ok=True)
        (d / "s.xy").write_text("\n".join(script) + "\n")
        r = subprocess.run([str(host), str(d / "s.xy"), str(d)], capture_output=True, text=True,
                           env={**os.environ, "LOOM_LOAD": "0"})
        if r.returncode or not (d / "take.wav").exists():
            print(f"{label(k)}: the simulator failed:\n{r.stdout[-400:]}{r.stderr[-400:]}")
            continue
        subprocess.run([py, str(ROOT / "tools/demos/analyze.py"), "--master", str(d / "take.wav"), str(out / f"{name}.wav")],
                       capture_output=True)
        subprocess.run(["afconvert", "-f", "m4af", "-d", "aac", "-b", "160000", str(out / f"{name}.wav"),
                        str(out / f"{name}.m4a")], capture_output=True)
        print(f"{label(k)} {nm.get(k, '')}: {out / (name + '.m4a')}")


if __name__ == "__main__":
    main()
