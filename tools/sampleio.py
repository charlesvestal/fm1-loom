# SPDX-License-Identifier: GPL-3.0-only
# Portions adapted from X0X's tools/sampleio.py, Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton
# Instruments (Felucca's sample helpers).
"""LOOM-8's sample helpers, shared by tools/gen_samples.py (the factory bank compiled into the image) and
tools/loom_samples.py (the user bank, uploaded over USB): WAV reading, resampling, IMA ADPCM with
checkpoints, and the user bank's layout (app/dsp/sample.h).

A sample is mono IMA ADPCM, 4 bits a sample, the low nibble first, from predictor 0 / index 0: the factory's at
22050 Hz, a user sample at its own rate (RATES; the firmware plays 4000..48000 Hz). Every SMP_BLK (128; 64 in a
version 2 bank, before 0.8) samples a checkpoint: the decoder's state before that sample, (predictor & 0xFFFF) |
index << 16, one uint32 each, so a voice can start, loop and play backwards from any block (dsp/sample.c decodes from
the checkpoint before a window of 32 into the voice's cache). A second of sound takes 0.53125 bytes a sample: 11.4 KB
at 22050 Hz.

The user bank (216 KB): three flash windows (REGIONS), one virtual space through them end to end, offset 0 the
header (at 0xC1000). The header (2048 bytes): magic "LSMP", version 2, count, flags, data length (from byte 2048),
CRC-32 of the data, the bank's size it was made for, then from byte 64 up to 44 entries of 40 bytes (32 user
slots and, since 0.8, the factory set's 12): name (12, NUL-padded), ADPCM offset, checkpoint offset (virtual, from
the bank's start), samples, loop start, loop end (samples; end 0: none), root note, flags (E_FACTORY: one of the
factory set, which its factory row plays and which is no user slot), bars (a loop of that many bars: the slice engine follows the tempo with
it; 0 = not stated), a spare, rate (Hz, 16 bits), two spare. The data: each sample's checkpoints (4-aligned, in
one window) then its ADPCM (which may run on from one window into the next, once), one after another in the
order given; 0xFF between. In the two extra windows (fm1-base's PLAT_XDATA1 / 0) no sector may carry an update
record's mark, "AT" at bytes 0xF06..0xF07 (the update loader would erase that sector): the layout moves a sample
on by 4 bytes until none does.
"""
import math
import re
import struct
import zlib

RATE = 22050                    # the factory samples' rate, and a user sample's unless it asks for another
RATES = (11025, 16000, 22050, 32000)   # what the tool offers (dsp/sample.h: 4000..48000 played)
RATE_MIN, RATE_MAX = 4000, 48000
BLK = 128                       # samples a checkpoint: a bank's since version 3 (0.8); 64 in version 2
BLK_V2 = 64
# the bank's windows (dsp/sample.h SMP_REG*), in the order it fills them: flash offset, size
REGIONS = [(0xC1000, 0x1B000),  # the data area after the projects, up to the calibration (108 KB)
           (0xE5000, 0x17000),  # fm1-base's PLAT_XDATA1 (92 KB)
           (0x93000, 0x4000)]   # fm1-base's PLAT_XDATA0 (16 KB)
XDATA = (1, 2)                  # the windows where no sector may carry an update record's mark
REC_MARK = 0xF06                # "AT" there, in a sector: an update record (base/bsp/loader/ldr_core.c)
BANK_SIZE = sum(n for _, n in REGIONS)
BANK_HDR = 2048
BANK_MAX_DATA = BANK_SIZE - BANK_HDR
BANK_N = 32                     # user slots (dsp/sample.h SMP_NUSER)
NFENT = 12                      # the factory set's entries (SMP_NFENT)
NENT = BANK_N + NFENT           # the header's entries (SMP_NENT)
E_FACTORY = 0x01                # an entry's flags: the factory set's (SMP_E_FACTORY)
BANK_VER = 3                    # 3: a checkpoint every 128 samples; 2 (every 64) still read, and converted
BANK_VERS = (2, 3)
ENT0, ENT = 64, 40
BANK_MAGIC = 0x504D534C         # "LSMP"
NAME_LEN = 12
BARS_MAX = 16                   # (dsp/sample.h SMP_BARS_MAX)

IMA_STEP = [7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88,
            97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658,
            724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660,
            4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818,
            18500, 20350, 22385, 24623, 27086, 29794, 32767]
IMA_IDX = [-1, -1, -1, -1, 2, 4, 6, 8]


# ---- WAV in (X0X's sampleio.py)

def wav_chunks(d):
    """RIFF/WAVE chunks -> {id: (offset of the body, declared length)}; the first of each id wins"""
    if len(d) < 12 or d[:4] != b"RIFF" or d[8:12] != b"WAVE":
        raise ValueError("not a WAV file")
    out, i = {}, 12
    while i + 8 <= len(d):
        cid, n = d[i:i + 4], struct.unpack_from("<I", d, i + 4)[0]
        out.setdefault(cid, (i + 8, n))
        i += 8 + n + (n & 1)
    return out


def wav_format(d, ch):
    """-> (tag, channels, rate, bits); WAVE_FORMAT_EXTENSIBLE resolved to its subformat"""
    if b"fmt " not in ch:
        raise ValueError("WAV without a fmt chunk")
    o = ch[b"fmt "][0]
    tag, nch, sr = struct.unpack_from("<HHI", d, o)
    bits = struct.unpack_from("<H", d, o + 14)[0]
    if tag == 0xFFFE:
        tag = struct.unpack_from("<H", d, o + 24)[0]
    return tag, nch, sr, bits


def _channel_values(tag, bps, raw):
    n = len(raw) // bps
    if tag == 3:
        return list(struct.unpack("<%d%s" % (n, "f" if bps == 4 else "d"), raw))
    if bps == 2:
        return [v / 32768 for v in struct.unpack("<%dh" % n, raw)]
    if bps == 3:
        w = bytearray(4 * n)
        w[1::4], w[2::4], w[3::4] = raw[0::3], raw[1::3], raw[2::3]
        return [v / 2147483648 for v in struct.unpack("<%di" % n, w)]
    if bps == 4:
        return [v / 2147483648 for v in struct.unpack("<%di" % n, raw)]
    return [(b - 128) / 128 for b in raw]


def read_any_wav(path):
    """PCM 8/16/24/32 or float, any channel count, mixed to mono -> (rate, [float], loop or None).
    The loop: the first 'smpl' loop (start, end) in samples of the file's rate."""
    d = path.read_bytes()
    ch = wav_chunks(d)
    tag, nch, sr, bits = wav_format(d, ch)
    bps = bits // 8
    if tag not in (1, 3) or not bps or not nch:
        raise ValueError(f"{path}: unsupported WAV format (tag {tag}, {bits} bit)")
    o, n = ch.get(b"data", (0, 0))
    frame = bps * nch
    nfr = min(n, len(d) - o) // frame
    v = _channel_values(tag, bps, d[o:o + nfr * frame])
    if nch > 1:
        v = [sum(v[f:f + nch]) / nch for f in range(0, len(v), nch)]
    loop = None
    if b"smpl" in ch:
        so = ch[b"smpl"][0]
        if struct.unpack_from("<I", d, so + 28)[0]:
            loop = struct.unpack_from("<II", d, so + 36 + 8)
    return sr, v, loop


def _fir(x, h):
    """x through the symmetric FIR h (odd length), centred (no delay), zeros beyond the ends"""
    k = len(h) // 2
    try:
        import numpy as np
        return list(np.convolve(np.asarray(x, dtype=float), np.asarray(h, dtype=float), mode="same"))
    except ImportError:
        xp = [0.0] * k + list(x) + [0.0] * k
        return [sum(h[j] * xp[i + j] for j in range(len(h))) for i in range(len(x))]


def _lowpass(fc, sr, taps=63):
    """a windowed-sinc low-pass (Blackman) at fc Hz, unity at DC"""
    k = taps // 2
    h = []
    for n in range(-k, k + 1):
        s = 2.0 * fc / sr if n == 0 else math.sin(2.0 * math.pi * fc / sr * n) / (math.pi * n)
        w = 0.42 + 0.5 * math.cos(math.pi * n / k) + 0.08 * math.cos(2.0 * math.pi * n / k)
        h.append(s * w)
    g = sum(h)
    return [v / g for v in h]


# the playback's linear interpolation (dsp/sample.c smp_play) rolls the top off as sinc^2(f / 22050): -3.9 dB at
# 8 kHz, -6.3 at 10 kHz. This 5-tap pre-emphasis (least squares, DC 1) puts it back: flat within 0.3 dB to 9 kHz
EMPHASIS = [v / 1.0196 for v in (0.0517, -0.21, 1.3362, -0.21, 0.0517)]


def resample(x, sr, to):
    """down: a windowed-sinc low-pass under the new rate's half (with numpy; without, a 63-tap one), then linear
    interpolation (an earlier version averaged neighbours instead: -2.4 dB at 10 kHz and the band above folded back)"""
    if sr == to:
        return list(x)
    try:
        import numpy as np
    except ImportError:
        np = None
    if np is not None and len(x) > 1:
        a = np.asarray(x, dtype=np.float64)
        if to < sr:
            fc = 0.45 * to / sr                         # the cut, cycles a sample: 90 % of the new half rate
            half = int(math.ceil(8.0 / fc))
            t = np.arange(-half, half + 1)
            h = 2.0 * fc * np.sinc(2.0 * fc * t) * np.blackman(len(t))
            a = np.convolve(a, h / h.sum(), mode="same")
        n = int(math.ceil((len(a) - 1) * to / sr))
        return np.interp(np.arange(n) * (sr / to), np.arange(len(a)), a).tolist()
    if sr > to:
        x = _fir(x, _lowpass(0.47 * to, sr))
    step, out, p, last = sr / to, [], 0.0, len(x) - 1
    while p < last:
        i = int(p)
        f = p - i
        out.append(x[i] * (1 - f) + x[i + 1] * f)
        p += step
    return out


def emphasis(x):
    """at any rate: the pre-emphasis that the playback's linear interpolation takes back off (EMPHASIS)"""
    return _fir(x, EMPHASIS)


def to_int16(x, peak_to=30000):
    """mono float -> int16 list, the peak normalised to peak_to"""
    pk = max(1e-9, max(map(abs, x)))
    return [max(-32768, min(32767, int(round(v / pk * peak_to)))) for v in x]


def note_from_name(name, default=60):
    """"C4" / "F#3" / "Bb2" in a file name, C4 = 60, clamped to 0..127"""
    m = re.search(r"(?<![A-Za-z])([A-G])([#b]?)(-?\d)(?!\d)", name)
    if not m:
        return default
    pc = "C D EF G A B".index(m.group(1)) + {"#": 1, "b": -1, "": 0}[m.group(2)]
    return max(0, min(127, (int(m.group(3)) + 1) * 12 + pc))


# ---- IMA ADPCM with checkpoints

def ima_encode(samples):
    """int16 samples -> (ADPCM bytes, checkpoints): the decoder's state before every BLK-th sample,
    (pred & 0xFFFF) | idx << 16. The encoder runs the decoder's own state, so the two agree exactly."""
    pred, idx, nib, ck = 0, 0, bytearray(), []
    for n, x in enumerate(samples):
        if n % BLK == 0:
            ck.append((pred & 0xFFFF) | idx << 16)
        step = IMA_STEP[idx]
        diff = x - pred
        code = 0
        if diff < 0:
            code = 8
            diff = -diff
        vd = step >> 3
        if diff >= step:
            code |= 4
            diff -= step
            vd += step
        if diff >= step >> 1:
            code |= 2
            diff -= step >> 1
            vd += step >> 1
        if diff >= step >> 2:
            code |= 1
            vd += step >> 2
        pred = pred - vd if code & 8 else pred + vd
        pred = max(-32768, min(32767, pred))
        idx = max(0, min(88, idx + IMA_IDX[code & 7]))
        nib.append(code)
    if len(nib) & 1:
        nib.append(0)
    return bytes(a | (b << 4) for a, b in zip(nib[0::2], nib[1::2])), ck


def ima_decode(adp, n):
    """the firmware's decoder (dsp/sample.c), for the tests"""
    pred, idx, out = 0, 0, []
    for i in range(n):
        code = (adp[i >> 1] >> ((i & 1) * 4)) & 15
        step = IMA_STEP[idx]
        vd = step >> 3
        if code & 4:
            vd += step
        if code & 2:
            vd += step >> 1
        if code & 1:
            vd += step >> 2
        pred = pred - vd if code & 8 else pred + vd
        pred = max(-32768, min(32767, pred))
        idx = max(0, min(88, idx + IMA_IDX[code & 7]))
        out.append(pred)
    return out


def clean_name(name):
    """a name for the screen: lower case, printable, at most 11 characters"""
    s = re.sub(r"[^a-z0-9 _.-]", "", name.lower().replace("_", " ")).strip()
    return (s or "sample")[:NAME_LEN - 1]


def region_starts():
    """each window's first virtual offset"""
    out, v = [], 0
    for _, n in REGIONS:
        out.append(v)
        v += n
    return out


def region_of(v):
    """the window of virtual offset v (len(REGIONS): past the bank)"""
    for r, v0 in enumerate(region_starts()):
        if v0 <= v < v0 + REGIONS[r][1]:
            return r
    return len(REGIONS)


def flash_of(v):
    """the flash offset of virtual offset v"""
    r = region_of(v)
    return REGIONS[r][0] + v - region_starts()[r]


def sample_bytes(n):
    """a sample of n samples in the bank: its checkpoints and its ADPCM, 4-aligned"""
    return ((n + BLK - 1) // BLK) * 4 + ((n + 1) // 2 + 3) // 4 * 4


def bytes_per_second(rate):
    return rate * (0.5 + 4.0 / BLK)


def _place(blobs, pad):
    """lay the samples out one after another from BANK_HDR: [(ck offset, ADPCM offset)] and the end"""
    starts = region_starts() + [BANK_SIZE]
    v, out = BANK_HDR, []
    for k, (ckb, adpb) in enumerate(blobs):
        v = (v + pad[k] + 3) & ~3
        while True:
            r = region_of(v)
            if r >= len(REGIONS):
                raise ValueError("too long")
            if v + len(ckb) > starts[r + 1]:            # the checkpoints in one window
                v = starts[r + 1]
                continue
            a = v + len(ckb)
            ra = region_of(a) if a < BANK_SIZE else len(REGIONS)
            if ra >= len(REGIONS) or a + len(adpb) > starts[min(ra + 2, len(REGIONS))]:
                if ra + 1 >= len(REGIONS):              # past the last window
                    raise ValueError("too long")
                v = starts[ra + 1]                      # it would cross two seams: from the next window
                continue
            break
        out.append((v, a))
        v = a + len(adpb)
    if v > BANK_SIZE:
        raise ValueError("too long")
    return out, v


def _marks(img, end):
    """virtual offsets in the extra windows where an update record's mark lies"""
    starts = region_starts()
    out = []
    for r in XDATA:
        for sec in range(0, REGIONS[r][1], 0x1000):
            v = starts[r] + sec + REC_MARK
            if v + 2 <= end and img[v:v + 2] == b"AT":
                out.append(v)
    return out


class Stored:
    """a sample as a bank stores it (read back from one): its length, checkpoints' bytes and ADPCM, kept as they are"""
    def __init__(self, n, ckb, adp):
        self.n, self.ckb, self.adp = n, bytes(ckb), bytes(adp)

    def __len__(self):
        return self.n


def stored_of(img):
    """a bank image's samples as user_bank's samples, each kept as stored: [(name, Stored, root, loop, bars, rate,
    flags)]"""
    out = []
    blk = BLK_V2 if struct.unpack_from("<H", img, 4)[0] == 2 else BLK
    for k in range(img[6]):
        o = ENT0 + k * ENT
        nm, a, c, n, ls, le, root, flags, bars, _, rate, _ = struct.unpack_from("<12sIIIIIBBBBHH", img, o)
        ckb = img[c:c + (n + blk - 1) // blk * 4]
        if blk != BLK:                    # a version 2 bank's: every other checkpoint (the same states, none lost)
            step = BLK // blk
            ckb = b"".join(ckb[i:i + 4] for i in range(0, len(ckb), 4 * step))
        out.append((nm.split(b"\0")[0].decode(errors="replace"), Stored(n, ckb, img[a:a + (n + 1) // 2]),
                    root, (ls, le) if le > ls else None, bars, rate, flags))
    return out


def user_bank(samples):
    """samples: [(name, int16 list, root, (loop start, loop end) or None[, bars[, rate[, flags]]])] (rate: the
    samples' own, Hz; default RATE; flags: E_FACTORY for the factory set's) -> the bank image (header + data, from
    virtual offset 0, ready for the windows), without the padding to BANK_SIZE"""
    own = sum(1 for smp in samples if not (smp[6] if len(smp) > 6 else 0) & E_FACTORY)
    if not 1 <= len(samples) <= NENT or own > BANK_N or len(samples) - own > NFENT:
        raise ValueError(f"1..{BANK_N} samples in the bank (and the factory set's {NFENT})")
    blobs, meta = [], []
    for name, s, root, loop, *more in samples:
        bars = more[0] if more else 0
        rate = more[1] if len(more) > 1 else RATE
        flags = more[2] if len(more) > 2 else 0
        if not 0 <= bars <= BARS_MAX:
            raise ValueError(f"{name}: {bars} bars (0..{BARS_MAX})")
        if not RATE_MIN <= rate <= RATE_MAX:
            raise ValueError(f"{name}: {rate} Hz ({RATE_MIN}..{RATE_MAX})")
        if not s:
            raise ValueError(f"{name}: no audio")
        if isinstance(s, Stored):
            blobs.append((s.ckb, s.adp))
        else:
            adp, ck = ima_encode(s)
            blobs.append((struct.pack("<%dI" % len(ck), *ck), adp))
        ls, le = (loop if loop else (0, 0))
        le = min(le, len(s))
        if le <= ls:
            ls = le = 0
        meta.append((clean_name(name), len(s), ls, le, max(0, min(127, root)), flags, bars, rate))
    pad = [0] * len(blobs)
    for _ in range(4096):
        try:
            where, end = _place(blobs, pad)
        except ValueError:
            used = BANK_HDR + sum(len(a) + len(b) for a, b in blobs)
            raise ValueError(f"too long: {used - BANK_HDR} B, the bank holds {BANK_MAX_DATA} B (about "
                             f"{BANK_MAX_DATA / bytes_per_second(RATE):.1f} s at {RATE} Hz, "
                             f"{BANK_MAX_DATA / bytes_per_second(11025):.1f} s at 11025 Hz)") from None
        img = bytearray(b"\xFF" * end)
        for (ckb, adpb), (c, a) in zip(blobs, where):
            img[c:c + len(ckb)] = ckb
            img[a:a + len(adpb)] = adpb
        marks = _marks(img, end)
        if not marks:
            break
        for k, (c, a) in enumerate(where):              # move the sample holding the mark on by 4 bytes
            if c <= marks[0] < a + len(blobs[k][1]):
                pad[k] += 4
                break
        else:
            raise ValueError("an update record's mark outside the samples")  # (0xFF between them: never)
    else:
        raise ValueError("could not keep the update record's mark out of the bank")
    data = bytes(img[BANK_HDR:])
    ents = b"".join(struct.pack("<12sIIIIIBBBBHH", nm.encode().ljust(NAME_LEN, b"\0"), a, c, n, ls, le, root, flags,
                                bars, 0, rate, 0)
                    for (nm, n, ls, le, root, flags, bars, rate), (c, a) in zip(meta, where))
    hdr = struct.pack("<IHBBIII", BANK_MAGIC, BANK_VER, len(meta), 0, len(data), zlib.crc32(data) & 0xFFFFFFFF,
                      BANK_SIZE)
    hdr = hdr.ljust(ENT0, b"\0") + ents
    return hdr.ljust(BANK_HDR, b"\0") + data


def bank_used(img):
    """the data bytes a bank image uses"""
    return len(img) - BANK_HDR


def midi_hz(n):
    return 440.0 * math.pow(2.0, (n - 69) / 12.0)
