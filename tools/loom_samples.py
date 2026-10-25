#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Adapted from X0X's tools/fm1_sample_upload.py, Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton
# Instruments (Felucca's sample upload).
"""LOOM-8's user sample bank: WAV files into the FM-1's flash over USB-MIDI, beside the factory samples.

  loom_samples.py info                          what the bank holds now
  loom_samples.py load [--rate R] SPEC ...      replace the bank with these (up to 32), and the factory set
  loom_samples.py erase [--all]                 empty the bank: the factory set left in it (--all: that too)
  loom_samples.py factory [--make-room]         LOOM-8's sounds added after the samples there (the installer's step);
                                                --make-room: if they do not fit, your samples made smaller to fit
  loom_samples.py build [--rate R] OUT.bin SPEC ...   no device: the bank's image into OUT.bin
                                                (the host simulator loads it: "samples OUT.bin")
  loom_samples.py syx [--rate R] OUT.syx SPEC ...     no device: the SysEx frames "load" would send (and "info"),
                                                into OUT.syx (the host simulator plays them through: "upload OUT.syx")
  loom_samples.py backup OUT.bin                the bank as it is, read back from the FM-1, into OUT.bin
  loom_samples.py restore IN.bin                put a backup (or a "build" image, or the web backup page's .loom8
                                                file: its samples) back: the bank replaced
  loom_samples.py extract DUMP.bin OUT.bin      no device: the bank out of a full flash dump (1 MB: fm1t.py dump,
                                                fm1_rescue.py), into OUT.bin for "restore"

  SPEC: [NAME=]file.wav[:ROOT][@BARS][%RATE], or "breaks": the break pack (classic, heat, choke, ecstasy: the
        slice engine's breaks, in the firmware until 0.7, which finds them in the bank by name), e.g.
        loom_samples.py load breaks my_kick.wav, or "factory": the factory set (the kits' drums, the instruments,
        the sweep and the vinyl: in the firmware until 0.8, now in the bank, as the installer puts them there), or
        "bank:FILE.bin": the samples of a bank image (a backup, a build) as it stores them

load, build and syx add the factory set after the samples given (if no SPEC says where: "factory"), so a load
never leaves the kits silent; --no-factory leaves it out; --make-room puts samples kept as stored (bank:FILE) at a
lower rate when the bank would not fit otherwise. Its 12 entries are not user slots: user 1 is still the
first sample given. It takes 86 KB of the bank.

Each file becomes one sample: mixed to mono, resampled to its rate (a windowed-sinc anti-alias filter first),
pre-emphasised for the playback's linear interpolation (flat to 0.4 x its rate as it plays), its peak normalised, IMA
ADPCM with a checkpoint every 128 samples (tools/sampleio.py, as the factory bank). NAME is what the screen shows (lower
case, 11 characters; default: the file's name); ROOT the MIDI note it plays at its own pitch on a synth
track (default: a note in the file's name, "piano_C4.wav", else 60 = C4). A loop in the WAV's 'smpl' chunk
is kept (the sample engine's loop mode plays it). @BARS says the file is a loop of that many bars (1..16):
a break for the slice engine, which then plays it at the song's tempo (a 1-bar loop of 2 s fits 120 BPM
exactly; at 140 BPM it plays faster), whatever its rate; its ROOT is slice 1's note, F3 (53, the lowest key)
unless given.

The rate: %RATE for one sample, --rate for the rest: 11025, 16000, 22050 or 32000 (or 11k, 16k, 22k, 32k), or
auto (the default): the highest of 22050, 16000 and 11025 at which the whole bank fits, the same for every
sample that did not choose. Higher is brighter (a sample keeps what lies below half its rate) and costs more
room, and a little more of the FM-1's time while it plays. The bank holds 214 KB of sound (the factory set's 86 KB of it):
about 18.7 s in all at 22050 Hz, 25.8 s at 16000, 37.4 s at 11025, 12.9 s at 32000 (a bar at 120 BPM takes 2 s). A sample may be as
long as the bank's first two windows (about 16 s at 22050 Hz).
On a track, the sample engine's (and the kit's sample model's) sample knob counts the factory samples first,
then user 1, user 2 ...: a load replaces them all, in the order given.

Protocol (app/samples.c): F0 7D 46 4C cmd ... F7; BEGIN 11, WRITE 12, HDR 16, END 13 (the FM-1 checks the data's
CRC before it commits), ERASE 14, INFO 15. Needs mido and a backend (pip install mido python-rtmidi) for the
device commands.
"""
import struct
import sys
import time
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import sampleio as sio  # noqa: E402

HDR = [0x7D, 0x46, 0x4C]
RC = {1: "size or offset", 2: "erase", 3: "write", 4: "not begun", 5: "header", 6: "data CRC", 7: "no samples",
      8: "an update record's mark"}
PART = 320
NPART = (sio.ENT0 + sio.NENT * sio.ENT + PART - 1) // PART
AUTO = (22050, 16000, 11025)                        # what auto tries, best first
LOOP_ROOT = 53                                      # a loop's slice 1: F3, the lowest key at the keys' own octave


def pack7(b):
    """groups of up to 7 bytes, each preceded by their top bits"""
    out = []
    for i in range(0, len(b), 7):
        g = b[i:i + 7]
        out.append(sum(((x >> 7) & 1) << j for j, x in enumerate(g)))
        out += [x & 0x7F for x in g]
    return out


def rate_of(s):
    """"22050" / "22k" / "auto" -> Hz or None (auto)"""
    s = s.lower()
    if s == "auto":
        return None
    r = {"11k": 11025, "16k": 16000, "22k": 22050, "32k": 32000}.get(s) or (int(s) if s.isdigit() else 0)
    if r not in sio.RATES:
        raise ValueError(f"rate {s}: one of {', '.join(map(str, sio.RATES))} (or auto)")
    return r


def parse(spec):
    """"[NAME=]file.wav[:ROOT][@BARS][%RATE]" -> (name, float samples, file rate, root, loop, bars, rate or None)"""
    name = None
    if "=" in spec:
        name, spec = spec.split("=", 1)
    rate = None
    if "%" in spec:
        spec, r = spec.rsplit("%", 1)
        rate = rate_of(r)
    bars = 0
    if "@" in spec and spec.rsplit("@", 1)[1].isdigit():
        spec, b = spec.rsplit("@", 1)
        bars = int(b)
        if not 1 <= bars <= sio.BARS_MAX:
            raise ValueError(f"{spec}: @{bars}: a loop of 1..{sio.BARS_MAX} bars")
    root = None
    if ":" in spec and spec.rsplit(":", 1)[1].isdigit():
        spec, r = spec.rsplit(":", 1)
        root = int(r)
    path = Path(spec)
    sr, x, loop = sio.read_any_wav(path)
    if not x:
        raise ValueError(f"{path}: no audio")
    if root is None:
        root = LOOP_ROOT if bars else sio.note_from_name(path.stem)
    return (name or path.stem, x, sr, root, loop, bars, rate)


def at_rate(p, rate):
    """a parsed file at a rate -> sampleio's sample tuple"""
    name, x, sr, root, loop, bars, _ = p
    if loop:
        loop = (int(loop[0] * rate / sr), int((loop[1] + 1) * rate / sr))
    return (name, sio.to_int16(sio.emphasis(sio.resample(x, sr, rate))), root, loop, bars, rate)


def fits(parsed, default):
    """the bank at these rates, if it fits (None: it does not); an entry of parsed is a file's parse() or the
    factory set's sample (a tuple of 7, sampleio's, kept as it is)"""
    try:
        return sio.user_bank([tuple(p) if is_factory(p) else at_rate(p, p[6] or default) for p in parsed])
    except ValueError as e:
        if str(e).startswith("too long"):
            return None
        raise


def length_at(p, rate):
    return int(len(p[1]) * rate / p[2])


def expand(specs, factory=True):
    """a SPEC "breaks": the break pack (the four breaks, built into the firmware until 0.7: gen_samples.py RECORDINGS);
    "factory": the factory set (until 0.8), added at the end unless named or factory is False"""
    import gen_samples
    out = []
    for s in specs:
        if s == "factory":
            out += [Factory(f) for f in gen_samples.factory_set()]
        elif s.startswith("bank:"):
            img = checked(Path(s[5:]).read_bytes(), s[5:])
            out += [Factory(t) for t in sio.stored_of(img)
                    if not (t[6] & sio.E_FACTORY and factory and "factory" not in specs)]
        else:
            out += gen_samples.break_specs() if s == "breaks" else [s]
    if factory and "factory" not in specs:
        out += [Factory(f) for f in gen_samples.factory_set()]
    return out


class Factory(tuple):
    """a sample ready for the bank as it is (the factory set's, or one kept from a bank image): sampleio's sample
    tuple, not a file to resample"""


def is_factory(p):
    return isinstance(p, Factory)


def is_factory_set(p):
    return is_factory(p) and len(p) > 6 and bool(p[6] & sio.E_FACTORY)


def at_rate_stored(p, rate):
    """a sample kept as stored (bank:FILE, a backup) at a lower rate: decoded, resampled, its level kept (one
    generation of ADPCM more), as the installer's makeRoom (loom_samples.js atRateKept)"""
    name, st, root, loop, bars, old, flags = p
    y = sio.resample([float(v) for v in sio.ima_decode(st.adp, st.n)], old, rate)
    s16 = [max(-32768, min(32767, int(round(v)))) for v in y]
    if loop:
        loop = (int(loop[0] * rate / old), min(len(s16), int(loop[1] * rate / old)))
    return Factory((name, s16, root, loop, bars, rate, flags))


def make_room(parsed, rate):
    """the samples kept as stored above a rate put at it, the highest of AUTO at which the bank fits (the factory
    set's never): (image, the rate, the names made smaller), or None"""
    for cap in AUTO:
        changed, nxt = [], []
        for p in parsed:
            if is_factory(p) and not is_factory_set(p) and isinstance(p[1], sio.Stored) and p[5] > cap:
                changed.append(p[0])
                nxt.append(at_rate_stored(p, cap))
            else:
                nxt.append(p)
        if changed:
            img = fits(nxt, rate or AUTO[-1])
            if img:
                return img, cap, changed
    return None


def build(specs, rate=None, quiet=False, factory=True, room=False):
    """the bank's image; --rate R for every sample that did not choose (None: auto); room: if it does not fit, the
    samples kept as stored made smaller (make_room)"""
    parsed = [s if is_factory(s) else parse(s) for s in expand(specs, factory)]
    own = [p for p in parsed if not is_factory(p)]
    if not parsed or len([p for p in parsed if not is_factory_set(p)]) > sio.BANK_N:
        raise ValueError(f"1..{sio.BANK_N} samples in the bank")
    tries = [rate] if rate else AUTO
    img, chose = None, None
    fixed = sum(sio.sample_bytes(len(p[1])) for p in parsed if is_factory(p))
    fset = sum(sio.sample_bytes(len(p[1])) for p in parsed if is_factory_set(p))
    for r in tries:
        # (a quick estimate first: the layout is exact, but resampling every file at every rate is slow)
        est = fixed + sum(sio.sample_bytes(length_at(p, p[6] or r)) for p in own)
        if est > sio.BANK_MAX_DATA:
            continue
        img = fits(parsed, r)
        if img:
            chose = r
            break
    if img is None and room:
        r = make_room(parsed, rate)
        if r:
            img, cap, changed = r
            if not quiet:
                print(f"made room: {', '.join(changed)} at {cap} Hz (what is above {cap // 2} Hz went)")
            return img
    if img is None:
        need = fixed + sum(sio.sample_bytes(length_at(p, p[6] or tries[-1])) for p in own)
        raise ValueError(f"too long: {need / 1024:.1f} KB at {tries[-1]} Hz, the bank holds "
                         f"{sio.BANK_MAX_DATA / 1024:.1f} KB" + (f" ({fset / 1024:.1f} KB of it the factory set's)"
                                                                if fset else "") +
                         f" (about {sio.BANK_MAX_DATA / sio.bytes_per_second(11025):.1f} s "
                         f"at 11025 Hz, {sio.BANK_MAX_DATA / sio.bytes_per_second(22050):.1f} s at 22050 Hz)")
    if not quiet:
        report([p for p in parsed if not is_factory_set(p)], chose, img, auto=rate is None)
    return img


def report(parsed, chose, img, auto):
    for k, p in enumerate(parsed):
        if is_factory(p):                     # kept as stored: its own rate and length
            r, n, hz = p[5], len(p[1]), f"{p[5]} Hz (kept)"
        else:
            r = p[6] or chose
            n = length_at(p, r)
            hz = f"{r} Hz" + (" (auto)" if auto and not p[6] else "")
        line = f"  user {k + 1:2} {sio.clean_name(p[0]):11}  {hz:15} {n / r:6.2f} s  {sio.sample_bytes(n) / 1024:6.1f} KB"
        if not is_factory(p) and p[5]:
            line += f"  {p[5]} bar{'s' if p[5] > 1 else ''} = {p[5] * 240.0 * r / n:.1f} BPM at its own speed"
        print(line)
    used = sio.bank_used(img)
    free = sio.BANK_MAX_DATA - used
    fs = img[6] - len(parsed)
    print(f"bank: {len(parsed)} samples" + (f" and the factory set ({fs})" if fs else "") +
          f", {used / 1024:.1f} of {sio.BANK_MAX_DATA / 1024:.1f} KB used; free: " +
          ", ".join(f"{free / sio.bytes_per_second(r):.1f} s at {r} Hz" for r in sio.RATES))


def frames(image):
    """the upload's frames: (command, args, timeout s, what it is)"""
    hdr = image[:sio.BANK_HDR]
    yield 11, [0], 10, "begin"
    for off in range(sio.BANK_HDR, len(image), 256):
        yield 12, [0, off & 0x7F, (off >> 7) & 0x7F, (off >> 14) & 0x7F] + pack7(image[off:off + 256]), 5, off
    for part in range(NPART):
        yield 16, [0, part] + pack7(hdr[part * PART:(part + 1) * PART]), 3, f"header part {part}"
    yield 13, [0], 20, "end"


class Link:
    def __init__(self):
        try:
            import mido
        except ImportError:
            sys.exit("needs mido: pip install mido python-rtmidi")
        self.mido = mido
        outs = [n for n in mido.get_output_names() if "LOOM" in n]
        ins = [n for n in mido.get_input_names() if "LOOM" in n]
        if not outs or not ins:                     # (macOS may keep an earlier firmware's name for the port)
            outs = [n for n in mido.get_output_names() if "FM-1" in n]
            ins = [n for n in mido.get_input_names() if "FM-1" in n]
        if not outs or not ins:
            sys.exit("no MIDI port named LOOM: connect the FM-1 (running LOOM-8) by USB")
        self.o = mido.open_output(outs[0])
        self.i = mido.open_input(ins[0])
        for _ in range(3):                          # the first frame after opening the port can be lost
            try:
                self.req(25, [], 1.0)
                break
            except TimeoutError:
                pass

    def req(self, cmd, args, timeout=3.0):
        for _ in self.i.iter_pending():
            pass
        self.o.send(self.mido.Message("sysex", data=HDR + [cmd] + list(args)))
        t0 = time.time()
        while time.time() - t0 < timeout:
            for m in self.i.iter_pending():
                if m.type == "sysex" and list(m.data[:4]) == HDR + [cmd]:
                    return list(m.data[4:])
            time.sleep(0.002)
        raise TimeoutError(f"no reply to command {cmd}")


def upload(link, image):
    t0 = time.time()
    total = len(image) - sio.BANK_HDR
    for cmd, args, timeout, what in frames(image):
        for tries in range(4):                      # a WRITE again when its answer is lost (the same bytes: the
            try:                                    # sector's erase, if it starts one, and the page written again)
                r = link.req(cmd, args, timeout)
                break
            except TimeoutError:
                if cmd != 12 or tries == 3:
                    raise
                print(f"\n  (no answer to the write at {what:#x}: sent again)")
                time.sleep(0.5)
        rc = r[-1] if r else 99
        if rc:
            where = f"write at {what:#x}" if cmd == 12 else what
            print()
            sys.exit(f"{where} failed ({RC.get(rc, rc)})")
        if cmd == 12:
            done = min(what + 256, len(image)) - sio.BANK_HDR
            print(f"\r  {done / 1024:6.1f} / {total / 1024:.1f} KB  {100 * done // max(1, total):3d} %", end="",
                  flush=True)
    print(f"\n  {time.time() - t0:.1f} s: loaded, the data's CRC checked by the FM-1")
    n, _, used, names = info(link)
    if n != image[6]:
        sys.exit(f"  but the FM-1 shows {n} samples, not {image[6]}")
    return True


def info(link):
    """-> (count, size KB, used KB, [(name, rate, samples)])"""
    out, size, used, n = [], 0, 0, 0
    while True:
        r = link.req(15, [len(out)])
        if r[0] != 2:
            sys.exit("the FM-1's LOOM-8 is older than this tool: update it")
        size, used, n, k = r[1] | r[2] << 7, r[3] | r[4] << 7, r[5], r[7]
        i = 8
        for _ in range(k):
            j = r.index(0, i)
            nm = bytes(r[i:j]).decode(errors="replace")
            rate = r[j + 1] | r[j + 2] << 7 | r[j + 3] << 14
            ns = r[j + 4] | r[j + 5] << 7 | r[j + 6] << 14
            out.append((nm, rate, ns))
            i = j + 7
        if not k or len(out) >= n:
            return n, size, used, out


def unpack7(a):
    """pack7 undone"""
    out = []
    i = 0
    while i < len(a):
        m = a[i]
        i += 1
        for j in range(7):
            if i >= len(a):
                break
            out.append(a[i] | ((m >> j) & 1) << 7)
            i += 1
    return bytes(out)


def checked(img, what):
    """a bank image (backup, extract, build) cut to its used length; ValueError if it is not one or is damaged.
    A bank with no samples is a header alone, with a count of 0"""
    if len(img) < sio.BANK_HDR:
        raise ValueError(f"{what}: not a LOOM-8 sample bank (too short)")
    magic, ver, n, _, dlen, crc, _ = struct.unpack_from("<IHBBIII", img)
    if magic != sio.BANK_MAGIC or ver not in sio.BANK_VERS:
        raise ValueError(f"{what}: not a LOOM-8 sample bank (version {sio.BANK_VER})")
    if n > sio.NENT or dlen > sio.BANK_MAX_DATA or len(img) < sio.BANK_HDR + dlen:
        raise ValueError(f"{what}: cut short or damaged")
    if zlib.crc32(img[sio.BANK_HDR:sio.BANK_HDR + dlen]) & 0xFFFFFFFF != crc:
        raise ValueError(f"{what}: damaged (the data's CRC does not match)")
    return bytes(img[:sio.BANK_HDR + dlen])


def bank_in(data, what):
    """a bank image, or the samples section of a .loom8 file (the web backup page's: fm1_backup.py split) -> the
    bank's image, checked"""
    if data[:4] != struct.pack("<I", sio.BANK_MAGIC) and len(data) > 16:
        sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "base" / "tools"))
        import fm1_backup
        try:
            _, sections = fm1_backup.split(data)
        except fm1_backup.BackupError as e:
            raise ValueError(f"{what}: {e}")
        found = [body for tag, body in sections if tag == "LSMP"]
        if not found:
            raise ValueError(f"{what}: no samples in it (a backup made before the backup page kept them?)")
        return checked(found[0], f"{what}'s samples")
    return checked(data, what)


def empty_bank():
    return struct.pack("<IHBBIII", sio.BANK_MAGIC, sio.BANK_VER, 0, 0, 0, 0, sio.BANK_SIZE).ljust(sio.BANK_HDR, b"\0")


def listing(img):
    """a bank image's samples, a line each: the user slots, then LOOM-8's sounds (the factory set's entries)"""
    names = names_of(img)
    fset = [k for k in range(len(names)) if img[sio.ENT0 + k * sio.ENT + 33] & sio.E_FACTORY]
    own = [nm for k, nm in enumerate(names) if k not in fset]
    return ("".join(f"\n  user {k + 1:2}: {nm}" for k, nm in enumerate(own)) +
            (f"\n  LOOM-8's: {', '.join(names[k] for k in fset)}" if fset else ""))


def names_of(img):
    return [img[sio.ENT0 + k * sio.ENT:sio.ENT0 + k * sio.ENT + sio.NAME_LEN].split(b"\0")[0].decode(errors="replace")
            for k in range(img[6])]


def read_bank(link, off, n):
    """n bytes of the bank from virtual offset off (SMP_READ, 224 a request)"""
    out = bytearray()
    while len(out) < n:
        at, want = off + len(out), min(224, n - len(out))
        for tries in range(4):
            try:
                r = link.req(17, [0, at & 0x7F, (at >> 7) & 0x7F, (at >> 14) & 0x7F, want & 0x7F, want >> 7])
                break
            except TimeoutError:
                if tries == 3:
                    raise
        if r[4]:
            sys.exit("the FM-1 is writing its samples" if r[4] == 4 else f"read at {at:#x} failed ({RC.get(r[4], r[4])})")
        b = unpack7(r[5:])
        if len(b) < want:
            sys.exit(f"read at {at:#x}: short reply")
        out += b[:want]
        if n > 4096 and (len(out) == n or 100 * len(out) // n != 100 * (len(out) - want) // n):
            print(f"\r  {len(out) / 1024:6.1f} / {n / 1024:.1f} KB  {100 * len(out) // n:3d} %", end="", flush=True)
    if n > 4096:
        print()
    return bytes(out)


def backup(link):
    """the bank's image, read back (a header alone if it holds no samples)"""
    n = info(link)[0]
    if not n:
        return empty_bank()
    hdr = read_bank(link, 0, sio.BANK_HDR)
    dlen = struct.unpack_from("<I", hdr, 8)[0]
    return checked(hdr + read_bank(link, sio.BANK_HDR, dlen), "the FM-1's bank")


def extract(dump):
    """the bank's image out of a full flash dump (its windows end to end)"""
    if len(dump) < 0x100000:
        raise ValueError(f"a full 1 MB flash dump, not {len(dump)} bytes")
    v = b"".join(dump[o:o + n] for o, n in sio.REGIONS)
    if struct.unpack_from("<I", v)[0] != sio.BANK_MAGIC:
        return empty_bank()                         # (an erased bank: no samples)
    return checked(v, "the dump's bank")


ROOM = False                                        # --make-room (args_of)


def args_of(argv):
    """[--rate R] [--no-factory] [--all] [--make-room] rest -> (rate or None, factory, rest); --make-room sets ROOM"""
    global ROOM
    factory = True
    while argv[:1] and argv[0] in ("--no-factory", "--all", "--make-room"):
        if argv[0] == "--make-room":
            ROOM = True
        else:
            factory = False
        argv = argv[1:]
    rate = None
    if argv[:1] == ["--rate"] and len(argv) > 1:
        rate = rate_of(argv[1])
        argv = argv[2:]
    elif argv[:1] and argv[0].startswith("--rate="):
        rate = rate_of(argv[0].split("=", 1)[1])
        argv = argv[1:]
    while argv[:1] and argv[0] in ("--no-factory", "--all", "--make-room"):
        if argv[0] == "--make-room":
            ROOM = True
        else:
            factory = False
        argv = argv[1:]
    return rate, factory, argv


def main():
    if len(sys.argv) < 2 or sys.argv[1] not in ("info", "load", "erase", "build", "syx", "backup", "restore",
                                                 "extract", "factory"):
        sys.exit(__doc__)
    cmd = sys.argv[1]
    try:
        rate, factory, rest = args_of(sys.argv[2:])
        if cmd in ("build", "syx"):
            if len(rest) < 2:
                sys.exit(__doc__)
            img = build(rest[1:], rate, factory=factory, room=ROOM)
            if cmd == "build":
                Path(rest[0]).write_bytes(img)
            else:
                fr = list(frames(img)) + [(15, [0], 3, "info")]   # (and what "info" asks, after)
                Path(rest[0]).write_bytes(b"".join(bytes([0xF0] + HDR + [c] + a + [0xF7]) for c, a, _, _ in fr))
            print(f"{rest[0]}: {img[6]} samples, {sio.bank_used(img)} of {sio.BANK_MAX_DATA} B")
            return
        if cmd == "extract":
            if len(rest) != 2:
                sys.exit(__doc__)
            img = extract(Path(rest[0]).read_bytes())
            Path(rest[1]).write_bytes(img)
            print(f"{rest[1]}: {img[6]} samples, {sio.bank_used(img) / 1024:.1f} KB" +
                  listing(img))
            return
        if cmd == "restore":
            if len(rest) != 1:
                sys.exit(__doc__)
            img = bank_in(Path(rest[0]).read_bytes(), rest[0])
            link = Link()
            if not img[6]:
                r = link.req(14, [0], 60)
                print("an empty bank: erase", "ok" if r[1] == 0 else f"FAILED ({RC.get(r[1], r[1])})")
                sys.exit(1 if r[1] else 0)
            print(f"{rest[0]}: {img[6]} samples, {sio.bank_used(img) / 1024:.1f} KB")
            sys.exit(0 if upload(link, img) else 1)
        if cmd == "load":
            if not rest:
                sys.exit(__doc__)
            img = build(rest, rate, factory=factory, room=ROOM)
            sys.exit(0 if upload(Link(), img) else 1)
        if cmd == "erase" and factory:              # the factory set kept: the bank is it alone
            img = build([], factory=True, quiet=True)
            print("erase: the user samples (the factory set kept: --all for that too)")
            sys.exit(0 if upload(Link(), img) else 1)
    except (OSError, ValueError) as e:
        sys.exit(f"cannot build the bank: {e}")
    link = Link()
    if cmd == "factory":                            # the installer's step: the set after the samples there
        img = backup(link)
        own = [t for t in sio.stored_of(img) if not t[6] & sio.E_FACTORY] if img[6] else []
        have = {t[0] for t in sio.stored_of(img) if t[6] & sio.E_FACTORY} if img[6] else set()
        import gen_samples
        if {t[0] for t in gen_samples.factory_set()} <= have:
            print("LOOM-8's sounds are in the bank already")
            return
        path = Path("build") / "factory_before.bin"
        path.parent.mkdir(exist_ok=True)
        path.write_bytes(img)
        try:
            new = build([f"bank:{path}"] if own else [], factory=True, room=ROOM)
        except ValueError as e:
            sys.exit(f"cannot add LOOM-8's sounds: {e}" + ("" if ROOM else "\n  --make-room: your samples made smaller to fit "
                                                             "(their highs above the rate's half go; back up first)"))
        sys.exit(0 if upload(link, new) else 1)
    if cmd == "backup":
        if len(rest) != 1:
            sys.exit(__doc__)
        img = backup(link)
        Path(rest[0]).write_bytes(img)
        print(f"{rest[0]}: {img[6]} samples, {sio.bank_used(img) / 1024:.1f} KB" +
              listing(img))
        return
    if cmd == "erase":
        r = link.req(14, [0], 60)
        print("erase:", "ok" if r[1] == 0 else f"FAILED ({RC.get(r[1], r[1])})")
        return
    n, size, used, smp = info(link)
    hdr = read_bank(link, 0, sio.ENT0 + n * sio.ENT) if n else b""   # (each entry's flags: the factory set's)
    fset = [k for k in range(n) if hdr[sio.ENT0 + k * sio.ENT + 33] & sio.E_FACTORY]
    own = [s for k, s in enumerate(smp) if k not in fset]
    print(f"user bank: {len(own)} samples" + (f" and LOOM-8's sounds ({len(fset)})" if fset else "") +
          f", {used} of {size} KB" +
          "".join(f"\n  user {k + 1:2}: {nm:11}  {rt} Hz  {ns / max(1, rt):.2f} s" for k, (nm, rt, ns) in enumerate(own)) +
          (f"\n  LOOM-8's: {', '.join(smp[k][0] for k in fset)}" if fset else ""))


if __name__ == "__main__":
    try:
        main()
    except TimeoutError as e:
        sys.exit(f"{e}: is LOOM-8 running, and no other app using its MIDI port?")
