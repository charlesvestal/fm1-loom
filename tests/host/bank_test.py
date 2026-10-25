#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The user bank's builder (tools/sampleio.py user_bank, tools/loom_samples.py build): the layout over the three
windows (each sample's checkpoints in one window, its ADPCM read back across a seam exactly as encoded), the rates
(each sample's own, auto choosing the highest that fits), the bounds (a bank larger than the windows refused, 33
samples refused), and an update record's mark kept out of the extra windows.
  tests/host/bank_test.py           (tests/run_tests.sh runs it)"""
import math
import struct
import sys
import tempfile
import wave
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import sampleio as sio  # noqa: E402
import loom_samples as ls  # noqa: E402

bad = 0


def check(what, ok):
    global bad
    print(f"{what:<88} {'ok' if ok else 'FAIL'}")
    bad += not ok


def tone(n, hz, rate, amp=20000):
    return [int(amp * math.sin(2 * math.pi * hz * i / rate)) for i in range(n)]


def entries(img):
    """the header as the firmware reads it (dsp/sample.c smp_user_scan)"""
    magic, ver, cnt, flags, ln, crc, size = struct.unpack_from("<IHBBIII", img, 0)
    out = []
    for i in range(cnt):
        e = struct.unpack_from("<12sIIIIIBBBBHH", img, sio.ENT0 + i * sio.ENT)
        out.append(dict(name=e[0].rstrip(b"\0").decode(), adp=e[1], ck=e[2], n=e[3], ls=e[4], le=e[5], root=e[6],
                        bars=e[8], rate=e[10]))
    return dict(magic=magic, ver=ver, cnt=cnt, len=ln, crc=crc, size=size), out


def window_end(v):
    r = sio.region_of(v)
    return sio.region_starts()[r] + sio.REGIONS[r][1]


# ---- a bank over the windows: the first sample long enough to run into the second window
rate = 22050
a = tone(210000, 440, rate)                         # 109 KB: past the first window's 106 KB of data
b = tone(30000, 220, 16000)
c = tone(5000, 880, 11025)
img = sio.user_bank([("long", a, 69, None, 0, rate), ("mid", b, 57, None, 0, 16000), ("short", c, 81, (100, 4000), 2,
                                                                                         11025)])
h, es = entries(img)
check("header: magic, version 3, 3 samples, made for 216 KB, CRC of the data",
      h["magic"] == sio.BANK_MAGIC and h["ver"] == 3 and h["cnt"] == 3 and h["size"] == sio.BANK_SIZE == 221184 and
      h["len"] == len(img) - sio.BANK_HDR and h["crc"] == (__import__("zlib").crc32(img[sio.BANK_HDR:]) & 0xFFFFFFFF))
check("each sample's rate, root, loop and bars kept",
      [e["rate"] for e in es] == [22050, 16000, 11025] and [e["root"] for e in es] == [69, 57, 81] and
      (es[2]["ls"], es[2]["le"], es[2]["bars"]) == (100, 4000, 2))
ok = all(e["ck"] % 4 == 0 and e["ck"] >= sio.BANK_HDR and
         e["ck"] + 4 * ((e["n"] + sio.BLK - 1) // sio.BLK) <= window_end(e["ck"]) for e in es)
check("every sample's checkpoints in one window, 4-aligned, after the header", ok)
e0 = es[0]
nb = (e0["n"] + 1) // 2
seam = e0["adp"] + nb > window_end(e0["adp"])
check(f"the long one's ADPCM runs on into the second window (from {e0['adp']:#x}, {nb} B)", seam)
adp, ck = sio.ima_encode(a)
check("read back across the seam: the bytes as encoded", bytes(img[e0["adp"]:e0["adp"] + nb]) == adp)
full = img + b"\xFF" * (sio.BANK_SIZE - len(img))
win = [full[v:v + n] for v, (_, n) in zip(sio.region_starts(), sio.REGIONS)]
r0 = sio.region_of(e0["adp"])
room = window_end(e0["adp"]) - e0["adp"]
part1 = win[r0][e0["adp"] - sio.region_starts()[r0]:]
part2 = win[r0 + 1][:nb - room]
check("and through the windows apart (as the FM-1 maps them): the same bytes", part1 + part2 == adp)
check("the flash offsets: virtual 0 at 0xC1000, the second window at 0xE5000, the third at 0x93000",
      sio.flash_of(0) == 0xC1000 and sio.flash_of(sio.region_starts()[1]) == 0xE5000 and
      sio.flash_of(sio.region_starts()[2]) == 0x93000 and sio.flash_of(sio.BANK_SIZE - 1) == 0x96FFF)
dec = sio.ima_decode(img[e0["adp"]:e0["adp"] + nb], 2000)
check("its decode is the encoder's own state", dec == sio.ima_decode(adp, 2000))

# ---- bounds
try:
    sio.user_bank([("big", tone(int(sio.BANK_MAX_DATA / sio.bytes_per_second(11025) * 11025) + 2000, 300, 11025), 60,
                    None, 0, 11025)])
    check("a bank larger than the windows is refused", False)
except ValueError as e:
    check("a bank larger than the windows is refused", str(e).startswith("too long"))
try:
    sio.user_bank([(f"s{i}", tone(100, 300, 22050), 60, None) for i in range(33)])
    check("33 samples are refused", False)
except ValueError:
    check("33 samples are refused", True)
check("32 samples are taken", entries(sio.user_bank([(f"s{i}", tone(100, 300, 22050), 60, None)
                                                     for i in range(32)]))[0]["cnt"] == 32)
try:
    sio.user_bank([("x", tone(100, 300, 22050), 60, None, 0, 60000)])
    check("a rate past 48 kHz is refused", False)
except ValueError:
    check("a rate past 48 kHz is refused", True)
# the biggest that fits, at 22050 Hz (the whole of the first two windows, about 16 s), and the seconds in all
one = int((sio.REGIONS[0][1] - sio.BANK_HDR + sio.REGIONS[1][1]) / sio.bytes_per_second(22050) * 22050) - 2000
img = sio.user_bank([("one", tone(one, 300, 22050), 60, None)])
check(f"one sample of {one / 22050:.1f} s at 22050 Hz fits (two windows)", entries(img)[0]["cnt"] == 1)
secs = {r: sio.BANK_MAX_DATA / sio.bytes_per_second(r) for r in sio.RATES}
check("in all: 37.4 s at 11025, 25.8 at 16000, 18.7 at 22050, 12.9 at 32000",
      [round(secs[r], 1) for r in sio.RATES] == [37.4, 25.8, 18.7, 12.9])

# ---- an update record's mark ("AT" at 0xF06 of a sector of an extra window) kept out: make the layout see one
long = tone(150000, 523, 22050)
img = sio.user_bank([("a", long, 60, None), ("b", long, 60, None)])
marks0 = sio._marks(img, len(img))
v = sio.region_starts()[1] + 0x3000 + sio.REC_MARK
was = bytes(img[v:v + 2])
saved = sio._marks


def fake(im, end):                                  # these bytes there count as a mark, as "AT" would
    return [v] if bytes(im[v:v + 2]) == was else []


sio._marks = fake
img2 = sio.user_bank([("a", long, 60, None), ("b", long, 60, None)])
sio._marks = saved
_, e2 = entries(img2)
_, e1 = entries(img)
check(f"no mark in the extra windows of a real bank ({len(marks0)})", not marks0)
check("a mark: the sample under it moved on by 4 bytes, nothing else",
      bytes(img2[v:v + 2]) != was and e2[0] == e1[0] and e2[1]["ck"] in (e1[1]["ck"] + 4, e1[1]["ck"] + 8))

# ---- the tool: rates by spec, auto, the frames of an upload
with tempfile.TemporaryDirectory() as d:
    d = Path(d)

    def wav(name, secs_, rate_=44100):
        p = d / name
        with wave.open(str(p), "wb") as w:
            w.setnchannels(1)
            w.setsampwidth(2)
            w.setframerate(rate_)
            w.writeframes(struct.pack("<%dh" % int(secs_ * rate_), *tone(int(secs_ * rate_), 220, rate_)))
        return str(p)

    small = [wav("a.wav", 1.0), wav("b.wav", 1.5)]
    img = ls.build([small[0] + "%32k", small[1]], quiet=True, factory=False)
    _, es = entries(img)
    check("per sample: %32k, the other auto at 22050 (it fits)", [e["rate"] for e in es] == [32000, 22050])
    check("lengths at those rates (1.0 s, 1.5 s)", abs(es[0]["n"] - 32000) < 40 and abs(es[1]["n"] - 33075) < 40)
    big = [wav(f"l{i}.wav", 6.0) for i in range(4)]    # 24 s: too long at 22050 (17.7 s), fits at 16000 (24.3)
    img = ls.build(big, quiet=True, factory=False)
    check("auto: 24 s of sound at 16000 Hz (not 22050)", {e["rate"] for e in entries(img)[1]} == {16000})
    img = ls.build(big[:2], rate=11025, quiet=True, factory=False)
    check("--rate 11025 for all", {e["rate"] for e in entries(img)[1]} == {11025})
    try:
        ls.build(big * 2, quiet=True, factory=False)
        check("48 s is refused, even at 11025 Hz", False)
    except ValueError as e:
        check("48 s is refused, even at 11025 Hz", str(e).startswith("too long"))
    img = ls.build(small, quiet=True, factory=False)
    fr = list(ls.frames(img))
    pages = [f for f in fr if f[0] == 12]
    check(f"an upload: begin, {len(pages)} pages from 2048, {ls.NPART} header parts, end",
          fr[0][0] == 11 and fr[-1][0] == 13 and [f[0] for f in fr].count(16) == ls.NPART == 6 and
          pages[0][3] == sio.BANK_HDR and pages[-1][3] + 256 >= len(img))
    # the header's parts, unpacked, are the header
    out = bytearray()
    for f in fr:
        if f[0] == 16:
            body, i = f[1][2:], 0
            while i < len(body):
                m, g = body[i], body[i + 1:i + 8]
                out += bytes(x | ((m >> j) & 1) << 7 for j, x in enumerate(g))
                i += 1 + len(g)
    check("the header's parts carry the header", bytes(out[:sio.ENT0 + 2 * sio.ENT]) == img[:sio.ENT0 + 2 * sio.ENT])

# a version 2 bank (a checkpoint every 64 samples, before 0.8) read back: kept as version 3 (every 128), its checkpoints
# every other one of the old (the same decoder states: none lost), the ADPCM as it was
smps = [("a", tone(5000, 220, 22050), 60, None), ("b", tone(777, 440, 16000), 64, (100, 700), 0, 16000)]
blk, ver = sio.BLK, sio.BANK_VER
sio.BLK, sio.BANK_VER = sio.BLK_V2, 2
try:
    v2 = sio.user_bank(smps)
finally:
    sio.BLK, sio.BANK_VER = blk, ver
kept = sio.stored_of(v2)
v3 = sio.user_bank(kept)
fresh = sio.user_bank(smps)
check("a version 2 bank, rebuilt: the version 3 bank of the same samples, byte for byte",
      struct.unpack_from("<H", v2, 4)[0] == 2 and v3 == fresh)
check("a checkpoint every 128 samples: 0.53 bytes a sample, not 0.56", abs(sio.bytes_per_second(1) - 0.53125) < 1e-9)

print("bank tool:", "all ok" if not bad else f"{bad} FAILED")
sys.exit(1 if bad else 0)
