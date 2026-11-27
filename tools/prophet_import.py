#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Prophet-5 (Rev 4) programs on LOOM-8's knit: a bank of the instrument's program messages (F0 01 32 02 group
program, 133 bytes packed 7 to 8, F7; 159 bytes each, as Sequential's factory files) turned into knit sounds.

  tools/prophet_import.py list BANK.syx                     each program and the knit sound it becomes, with what
                                                            did not map
  tools/prophet_import.py script BANK.syx PROGRAM           the host simulator's lines for one (PROGRAM: 111..588,
                                                            group bank program, or 1..200)
  tools/prophet_import.py render BANK.syx OUT [--programs 111,112 ...]
                                                            each played in the host simulator (a chord, then a line),
                                                            OUT/<label>.wav, for listening
  tools/prophet_import.py pack BANK.syx PACK.syx --programs 111,112,... [--slot N]
                                                            up to 64 as a LOOM-8 preset pack (tools/loom_presets.py
                                                            send PACK.syx), each at the factory sounds' level

Bring your own bank: Sequential's programs are never committed or published here (keep them in ~/private-stash).

How a program maps (knit's page: the Prophet's section)
  Engine    osc 1's wave: saw, pulse (its width) or the two (a blend); unison: mono, its voices (5 for 0) and detune
  Osc 2     the other oscillator at its interval (+-24 semitones) and fine tune, its own wave (saw, pulse, triangle)
  Levels    the louder oscillator full, the other against it (osc 1's level: knit's extra); noise
  Mod       SYNC: A synced to B -> osc 2 synced to osc 1 (so A is osc 2, B osc 1); Poly-Mod's filter envelope on A's
            frequency -> sweep (osc 2: A is osc 2 then); Poly-Mod's osc B on A's frequency -> cross (osc 2 into osc 1:
            A osc 1, B osc 2, without sync); Poly-Mod's envelope on A's pulse width -> the sweep on osc 1's width
            (knit's extra), where A is osc 1 and its frequency is not swept
  Envelope  the amp's and the filter's ADSRs, each its own
  Filter    ladder; cutoff, resonance, keyboard (off, half, full), the envelope amount (Poly-Mod's on the filter
            added)
  LFO       one destination: the largest of pitch (FREQ A or B on the wheel), the cutoff and pulse width; INITIAL
            its depth; triangle -> sine, saw -> saw, square -> square, mostly noise -> random
  Play      glide, bend range, unison's mode, volume (level-fitted); VINTAGE -> knit's analog

The program layout is Saadet's (github.com/keremimo/saadet, Kerem Kilic, MPL-2.0: firmware prophet_patch.c), and so
are the curves, from his measurements of Sequential's demo recordings: the cutoff (Curtis) about 870 Hz x
2^((v - 67) / 15.2) at C2 with full keyboard tracking doubling per octave from there; the filter envelope 0.046
octave a step; decay and release a time constant of 10000^(v / 127) ms; the LFO e^(-3.10 + 0.0539 v) Hz; its pitch
depth 0.052 v^1.74 cents; Poly-Mod's envelope on A 2.65 octaves at full, its osc B +-1.1 octaves. Estimated there
(not measured), and here too: the LFO's filter depth (4 octaves at full), Poly-Mod on the filter (8), the attacks.
"""
import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from juno_import import clamp, knit_cut, knit_time  # noqa: E402

FRAME, RAW = 159, 133
(FREQ_A, FREQ_B, FINE_B, SAW_A, PULSE_A, SAW_B, TRI_B, PULSE_B, PW_A, PW_B, SYNC, LOW_B, KEY_B, GLIDE, LEVEL_A,
 LEVEL_B, NOISE, CUTOFF, RES, KEY_FILTER, FILTER_REV, LFO_RATE, LFO_INITIAL, LFO_SAW, LFO_TRI, LFO_PULSE, WHEEL_MIX,
 WHEEL_FREQ_A, WHEEL_FREQ_B, WHEEL_PW_A, WHEEL_PW_B, WHEEL_FILTER, POLY_ENV, POLY_B, POLY_FREQ, POLY_PW, POLY_FILTER,
 VINTAGE, PRESS_FILTER, PRESS_LFO, ENV_FILTER, VEL_FILTER, VEL_AMP, ATT_F, ATT_A, DEC_F, DEC_A, SUS_F, SUS_A, REL_F,
 REL_A, RELEASE_ON, UNISON, UNI_COUNT, UNI_DETUNE) = range(55)
NAME, NAME_LEN, BEND = 65, 20, 86


def label(k):
    """the instrument's label of program k: group, bank, program (111 .. 588)"""
    return f"{1 + k // 40}{1 + k // 8 % 5}{1 + k % 8}"


def program_of(s):
    s = s.strip()
    if len(s) == 3 and s[0] in "12345" and s[1] in "12345" and s[2] in "12345678":
        return (int(s[0]) - 1) * 40 + (int(s[1]) - 1) * 8 + int(s[2]) - 1
    if s.isdigit() and 1 <= int(s) <= 200:
        return int(s) - 1
    sys.exit(f"prophet_import: no program {s} (111..588 or 1..200)")


def read_bank(path):
    """{program: 133 bytes} from a file of program messages (others in it are skipped)"""
    data = Path(path).read_bytes()
    out, i = {}, 0
    while (i := data.find(b"\xf0\x01\x32\x02", i)) >= 0:
        f = data[i:i + FRAME]
        if len(f) == FRAME and f[-1] == 0xF7 and all(b < 128 for b in f[1:-1]) and f[4] <= 4 and f[5] < 40:
            pk = f[6:-1]
            out[f[4] * 40 + f[5]] = bytes(pk[g + j + 1] | (pk[g] >> j & 1) << 7 for g in range(0, 152, 8) for j in range(7))
            i += FRAME
        else:
            i += 1
    if not out:
        sys.exit(f"prophet_import: {path} holds no Prophet-5 program messages (F0 01 32 02 group program .. F7)")
    return out


def name_of(p):
    return p[NAME:NAME + NAME_LEN].decode("latin-1").strip()


def semis(v):
    """coarse tuning: semitone detents, two raw values each, root at 24 (-12 .. +36)"""
    return max(-12, min(36, (v >> 1) - 12))


def pulse_wave(pw):
    """a pulse width (64 square, either end narrow) as knit's wave: 64 square .. 127 a 10 % pulse"""
    return clamp(64 + abs(pw - 64) * 63.0 / 64.0)


def osc_wave(saw, pulse, pw):
    return 32 if saw and pulse else pulse_wave(pw) if pulse else 0


def p_time(v):
    """decay / release: a time constant 10000^(v / 127) ms; knit's the time to -60 dB (6.9 of them)"""
    return 6.9 * 10.0 ** (4.0 * v / 127.0) / 1000.0


def p_attack(v):
    return 10.0 ** (4.0 * v / 127.0) / 1000.0


def lfo_hz(v):
    return min(500.0, max(0.022, math.exp(-3.10 + 0.0539 * v)))


def knit_rate(hz):
    """knit's LFO rate: 0.1 .. 30 Hz past its eight synced values"""
    return clamp(8 + 119.0 * math.log2(max(hz, 0.1) / 0.1) / math.log2(300.0), 8, 127)


def convert(p):
    """a program's 133 bytes -> [(knit parameter, value)], and notes on what did not map"""
    notes, out = [], [("engine", "knit")]
    a_sounds = (p[SAW_A] or p[PULSE_A]) and p[LEVEL_A] > 0
    b_sounds = (p[SAW_B] or p[TRI_B] or p[PULSE_B]) and p[LEVEL_B] > 0
    sweep_a = p[POLY_FREQ] and p[POLY_ENV] > 0
    fm_a = p[POLY_FREQ] and p[POLY_B] > 0
    a_is_2 = bool(p[SYNC] or sweep_a)                    # (knit syncs and sweeps osc 2)
    if not a_is_2 and not a_sounds and b_sounds and not fm_a:
        a_is_2 = True                                    # (B alone: B as osc 1)
    if p[LOW_B] or not p[KEY_B]:
        notes.append("osc B " + ("in its low (LFO) range" if p[LOW_B] else "off the keyboard") +
                     ": played as a keyed oscillator")
    o1, o2 = ("B", "A") if a_is_2 else ("A", "B")
    lev = {"A": p[LEVEL_A] if a_sounds else 0, "B": p[LEVEL_B] if b_sounds else 0}
    st = {"A": semis(p[FREQ_A]), "B": semis(p[FREQ_B])}
    # osc 1: its wave; knit's osc 1 always sounds
    if o1 == "A":
        out.append(("e1", osc_wave(p[SAW_A], p[PULSE_A], p[PW_A])))
    else:
        if p[TRI_B] and not (p[SAW_B] or p[PULSE_B]):
            notes.append("osc B a triangle as osc 1: knit's osc 1 plays a saw")
        out.append(("e1", osc_wave(p[SAW_B], p[PULSE_B], p[PW_B])))
    # unison
    if p[UNISON]:
        n = p[UNI_COUNT] if 0 < p[UNI_COUNT] <= 7 else 5 if p[UNI_COUNT] == 0 else 7
        out += [("e2", clamp(p[UNI_DETUNE] * 20)), ("e3", n), ("e4", 64), ("vmode", "mono")]
    else:
        out += [("e2", 0), ("e3", 1), ("e4", 0), ("vmode", "poly")]
    # osc 2: the other, at its interval; its wave
    # (the interval and Fine B together, then split into semitones and the cents left: knit's fine reaches +-50,
    # Fine B 84 cents at 112 (Iconic Brass: B a semitone down and 84 up, 16 cents flat; clamped, a quarter-tone off)
    cents = (st[o2] - st[o1]) * 100.0 + p[FINE_B] * 0.75 * (1 if o2 == "B" else -1)   # (Fine B 24 = +18 cents)
    iv = round(cents / 100.0)
    if abs(iv) > 24:
        notes.append(f"osc {o2} {iv:+d} semitones from osc {o1}: knit's osc 2 reaches 24")
    out += [("x1", clamp(64 + iv, 40, 88)), ("fine", clamp(64 + cents - 100.0 * iv, 14, 114))]
    if o2 == "A":
        out += ([("o2wave", "saw")] if p[SAW_A] and not p[PULSE_A] else
                [("o2wave", "pulse"), ("o2shape", pulse_wave(p[PW_A]))] if p[PULSE_A] and not p[SAW_A] else
                [("o2wave", "saw")])
    else:
        out += ([("o2wave", "triangle")] if p[TRI_B] and not (p[SAW_B] or p[PULSE_B]) else
                [("o2wave", "pulse"), ("o2shape", pulse_wave(p[PW_B]))] if p[PULSE_B] and not p[SAW_B] else
                [("o2wave", "saw")])
    # the levels: the louder oscillator full, the other against it (osc 1 turned down: knit's extra, TP_K1CUT; a synced
    # patch's master silent)
    top = max(lev[o1], lev[o2], 1)
    out += [("k1cut", clamp(127 * (1.0 - lev[o1] / top))), ("x2", clamp(127 * lev[o2] / top)), ("x3", 0),
            ("x4", clamp(p[NOISE] * 0.8 * 127 / top)), ("kanalog", p[VINTAGE])]
    # the octave: osc 1's coarse tuning (8' at its root); a remainder off the octaves is lost
    octs, rem = divmod(st[o1], 12)
    if rem:
        notes.append(f"osc {o1} {rem} semitones above an octave: knit plays it on the octave")
    out.append(("oct", clamp(2 + octs, 0, 4)))
    # the Mod page
    sweep = round(p[POLY_ENV] * 2.65 * 12 / 127) if sweep_a else 0
    cross = round(127 * math.sqrt(min(1.0, p[POLY_B] / 127 * 1.1 / 6.0))) if fm_a and not a_is_2 else 0
    if fm_a and a_is_2:
        notes.append("Poly-Mod osc B on A's frequency beside sync or the sweep: not made (knit's cross runs osc 2 "
                     "into osc 1)")
    # Poly-Mod's envelope on A's pulse width: knit's sweep on osc 1's width (its extra, TP_KSWTO), where A is osc 1,
    # pulsed, and the sweep is free (A's frequency not swept)
    swto = 0
    if p[POLY_PW] and p[POLY_ENV] and o1 == "A" and p[PULSE_A] and not sweep:
        swto, sweep = 1, round(p[POLY_ENV] * 48 / 127)
    elif p[POLY_PW] and (p[POLY_ENV] or p[POLY_B]):
        notes.append("Poly-Mod on A's pulse width: not made" + (" (by osc B)" if not p[POLY_ENV] else ""))
    if p[POLY_FILTER] and p[POLY_B]:
        notes.append("Poly-Mod osc B on the filter: not made")
    out += [("ksync", 1 if p[SYNC] else 0), ("ksweep", min(sweep, 48)), ("kcross", cross), ("ksub", "sine"),
            ("kswto", swto)]
    # the envelopes
    rel_a = p[REL_A] if p[RELEASE_ON] else 0
    rel_f = p[REL_F] if p[RELEASE_ON] else 0
    out += [("a", knit_time(p_attack(p[ATT_A]))), ("d", knit_time(p_time(p[DEC_A]))), ("s", p[SUS_A]),
            ("r", knit_time(p_time(rel_a))), ("fa", knit_time(p_attack(p[ATT_F]))), ("fd", knit_time(p_time(p[DEC_F]))),
            ("fs", p[SUS_F]), ("fr", knit_time(p_time(rel_f)))]
    # the filter
    kf = {0: 0.0, 1: 0.5, 2: 1.0}.get(p[KEY_FILTER], 1.0)
    hz = 870.0 * 2.0 ** ((p[CUTOFF] - 67) / 15.2) * 2.0 ** (2.0 * kf)   # (at C4: knit's pivot)
    env_oct = p[ENV_FILTER] * 0.046 + (p[POLY_ENV] / 127.0 * 8.0 if p[POLY_FILTER] else 0.0)
    if env_oct > 6.0:
        notes.append(f"filter env +{env_oct:.1f} oct: knit's reaches 6")
    out += [("ftype", "ladder"), ("cut", knit_cut(hz)), ("res", clamp(127 * min(1.0, 1.05 * (p[RES] / 127) ** 1.5))),
            ("fenv", clamp(64 + env_oct * 64.0 / 6.0)), ("ktrk", clamp(127 * kf))]
    if not p[FILTER_REV]:
        notes.append("the Rev 1/2 (SSI) filter: played as the Rev 3 (Curtis)")
    # the LFO: one destination
    depth = p[LFO_INITIAL]
    cents = 0.052 * depth ** 1.74 if (p[WHEEL_FREQ_A] or p[WHEEL_FREQ_B]) else 0.0
    f_oct = depth / 127.0 * 4.0 if p[WHEEL_FILTER] else 0.0
    pw = depth / 127.0 if (p[WHEEL_PW_A] or p[WHEEL_PW_B]) else 0.0
    cand = sorted([(cents / 1200.0, "vib"), (f_oct / 4.0, "vcf"), (pw, "pwm")], reverse=True)
    used = [c for c in cand if c[0] > 0.01]
    if len(used) > 1:
        notes.append("LFO on " + " and ".join(n for _, n in used[1:]) + " too: not made (knit's LFO has one target)")
    rate = knit_rate(lfo_hz(p[LFO_RATE]))
    if lfo_hz(p[LFO_RATE]) > 30.0 and used:
        notes.append(f"LFO {lfo_hz(p[LFO_RATE]):.0f} Hz: knit's reaches 30")
    shape = ("random" if p[WHEEL_MIX] >= 64 else "saw" if p[LFO_SAW] and not p[LFO_TRI] else
             "square" if p[LFO_PULSE] and not p[LFO_TRI] else "sine")
    kind = used[0][1] if used else None
    if kind == "vib" and cents <= 100.0 and shape == "sine":
        out += [("ltype", "wobble"), ("lspeed", rate), ("lamt", clamp(64 + cents * 63.0 / 100.0)), ("ldest", 64),
                ("lx", 64)]
    elif kind:
        dest, param, amt = {"vib": ("voice free", 1, 64 + cents / 100.0 * 63.0 / 12.0),
                            "vcf": ("filter free", 0, 64 + (f_oct * 127.0 / 8.64385619) * 63.0 / 127.0),
                            "pwm": ("synth free", 0, 64 + pw * 32.0)}[kind]
        out += [("ltype", "wave"), ("lspeed", rate), ("lamt", clamp(amt)), ("ldest", dest), ("lx", param),
                ("lx2", shape)]
    else:
        out.append(("ltype", "off"))
    if p[PRESS_FILTER] or p[PRESS_LFO]:
        notes.append("aftertouch routes: not made")
    # the rest
    bend = p[BEND]
    out += [("porta", p[GLIDE]), ("bend", "off" if bend == 0 else str(bend) if bend <= 7 else "octave"),
            ("hpf", 0), ("volume", 100), ("itype", "off"), ("ctype", "off")]
    return out, notes


def lines(p, track=2):
    out, notes = convert(p)
    return [f"knob {track} {k} {v}" for k, v in out], notes


def describe(p):
    w = lambda pairs: "+".join(n for n, b in pairs if b) or "-"
    a = w((("saw", p[SAW_A]), ("pulse", p[PULSE_A])))
    b = w((("saw", p[SAW_B]), ("tri", p[TRI_B]), ("pulse", p[PULSE_B])))
    mods = " ".join(x for x, c in (("sync", p[SYNC]), ("pm-env", p[POLY_FREQ] and p[POLY_ENV]),
                                   ("pm-B", p[POLY_FREQ] and p[POLY_B]), ("unison", p[UNISON])) if c)
    return (f"A {a:9s} {p[LEVEL_A]:3d}  B {b:13s} {p[LEVEL_B]:3d}  cut {p[CUTOFF]:3d} res {p[RES]:3d} env "
            f"{p[ENV_FILTER]:3d}  {mods}")


def main():
    a = sys.argv[1:]
    if len(a) < 2 or a[0] not in ("list", "script", "render", "pack"):
        sys.exit(__doc__)
    opt = {}
    for flag in ("--programs", "--slot", "--track"):
        if flag in a:
            i = a.index(flag)
            opt[flag] = a[i + 1]
            del a[i:i + 2]
    bank = read_bank(a[1])
    if a[0] == "list":
        for k in sorted(bank):
            _, notes = convert(bank[k])
            print(f"{label(k)} {name_of(bank[k]):20s} {describe(bank[k])}")
            for n in notes:
                print(f"      {n}")
        return
    if a[0] == "script":
        k = program_of(a[2])
        ls, notes = lines(bank[k], int(opt.get("--track", 2)))
        print("\n".join([f"# {n}" for n in notes] + ls))
        return
    import loom_presets
    progs = [program_of(s) for s in opt["--programs"].split(",")] if "--programs" in opt else sorted(bank)
    if a[0] == "pack":
        slot = int(opt.get("--slot", 1))
        if len(a) < 3 or not progs or slot < 1:
            sys.exit("prophet_import: pack BANK.syx PACK.syx --programs 111,... (any number: a library)")
        out = bytearray()
        for i, k in enumerate(progs):
            ls, _ = lines(bank[k])
            ls, pk, vol = loom_presets.level_fit(ls)
            fmt, data = loom_presets.preset_from_lines(name_of(bank[k]), ls)
            out += loom_presets.frame((slot - 1 + i) & 127, fmt, data)
            print(f"{i + 1:3d} {loom_presets.name_of(data):12s} {label(k)} {name_of(bank[k])}  chord {pk:+.1f} dB -> "
                  f"volume {vol}")
        Path(a[2]).write_bytes(bytes(out))
        print(f"{a[2]}: {len(progs)} presets; send: tools/loom_presets.py send {a[2]}")
        return
    import os
    import subprocess
    out = Path(a[2]).resolve()
    out.mkdir(parents=True, exist_ok=True)
    host = loom_presets.host()
    for k in progs:
        ls, _ = lines(bank[k])
        ls, _, _ = loom_presets.level_fit(ls)
        name = label(k) + "_" + "".join(c if c.isalnum() else "_" for c in name_of(bank[k]).lower())
        sc = ["wait 2500", "master 4096", *ls, "wait 300", "wav take.wav"]
        sc += [f"play 2 {n} on 110" for n in ("C4", "E4", "G4", "B4")] + ["wait 1600"]
        sc += [f"play 2 {n} off" for n in ("C4", "E4", "G4", "B4")] + ["wait 900"]
        for n in ("C3", "G3", "C4", "D#4", "G4", "C5"):
            sc += [f"play 2 {n} on 110", "wait 180", f"play 2 {n} off", "wait 70"]
        sc += ["wait 1500", "wavstop"]
        d = out / ".run"
        d.mkdir(exist_ok=True)
        (d / "s.xy").write_text("\n".join(sc) + "\n")
        r = subprocess.run([str(host), str(d / "s.xy"), str(d)], capture_output=True, text=True,
                           env={**os.environ, "LOOM_LOAD": "0"})
        if r.returncode or not (d / "take.wav").exists():
            print(f"{label(k)}: the simulator failed:\n{r.stdout[-400:]}{r.stderr[-400:]}")
            continue
        (d / "take.wav").replace(out / f"{name}.wav")
        print(out / f"{name}.wav")


if __name__ == "__main__":
    main()
