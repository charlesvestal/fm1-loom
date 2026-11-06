#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Charles Vestal. The rendering of the faces (FONTSETS, upscale, render_bdf, render, the header) is
# fm1-base's tools/gen_font.py, Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments, under MPL-2.0
# as there (felucca-bsp#4)
"""LOOM-8's fonts as a C header (its own compact format: fm1-base's faces, in base/assets/fonts), the app's drawing's
(app/gfx_loom.c; fm1-base's own screens keep fm1-base's fonts):
  tools/gen_font.py OUT.h        LFONT_XS .. LFONT_L, of type loom_font_t

  XS / S / B / M / L: Terminus 12, 16, 16 bold, 24 bold, 32 bold (BDF, SIL OFL 1.1); see SIZES. TTF fonts also work through render()
(anti-aliased, tabular figures via the OpenType `tnum` feature).

Glyph format: per glyph an advance width, an ink box {x, y, w, h} and a byte
offset. The glyph's cell starts FONT_PAD pixels left of the pen position (room
for side bearings) and is the face's height tall; only the ink box is stored:
its pixels (alpha 0..15) row after row as one stream of nibbles, high nibble
first. A face with `rle` set codes runs: nibble 0 or 15 is followed by a count
(n + 1 pixels of it), 1..14 are single pixels (where that is smaller: the big
faces). gfx.c cv_text decodes it as it draws (the main loop only).
"""
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

FONTS = Path(__file__).resolve().parents[1] / "base" / "assets" / "fonts"   # fm1-base's faces (SIL OFL 1.1)
# (name, file, px, scale, pixel, first, last): pixel fonts are rendered without
# anti-aliasing at their design size and enlarged by an integer factor
# X0X: five roles, XS (grid labels, units, hints), S (body), B (headers, names, the selected
# row), M (knob values) and L (the big readout). Each set gives them a face and a pixel size,
# chosen so the line height stays near the layout's 12 / 16 / 24 / 32. X0X_FONTSET picks one.
# A TTF entry "file.ttf@wght=600" sets a variable font's axis.
FONTSETS = {
    "terminus": [("XS", "ter-u12n.bdf", 12, 1, True, 32, 126),
                 ("S", "ter-u16n.bdf", 16, 1, True, 32, 126),
                 ("B", "ter-u16b.bdf", 16, 1, True, 32, 126),
                 ("M", "ter-u24b.bdf", 24, 1, True, 32, 95),
                 ("L", "ter-u32b.bdf", 32, 1, True, 32, 95)],
    "barlow": [("XS", "BarlowSemiCondensed-SemiBold.ttf", 12, 1, False, 32, 126),
               ("S", "BarlowSemiCondensed-Medium.ttf", 15, 1, False, 32, 126),
               ("B", "BarlowSemiCondensed-Bold.ttf", 15, 1, False, 32, 126),
               ("M", "BarlowSemiCondensed-SemiBold.ttf", 23, 1, False, 32, 95),
               ("L", "BarlowSemiCondensed-Bold.ttf", 31, 1, False, 32, 126)],
    "inter": [("XS", "Inter[opsz,wght].ttf@wght=600", 11, 1, False, 32, 126),
              ("S", "Inter[opsz,wght].ttf@wght=500", 14, 1, False, 32, 126),
              ("B", "Inter[opsz,wght].ttf@wght=700", 14, 1, False, 32, 126),
              ("M", "Inter[opsz,wght].ttf@wght=650", 21, 1, False, 32, 95),
              ("L", "Inter[opsz,wght].ttf@wght=700", 29, 1, False, 32, 95)],
    "rajdhani": [("XS", "Rajdhani-Bold.ttf", 14, 1, False, 32, 126),
                 ("S", "Rajdhani-SemiBold.ttf", 17, 1, False, 32, 126),
                 ("B", "Rajdhani-Bold.ttf", 17, 1, False, 32, 126),
                 ("M", "Rajdhani-Bold.ttf", 26, 1, False, 32, 95),
                 ("L", "Rajdhani-Bold.ttf", 35, 1, False, 32, 95)],
    "chakra": [("XS", "ChakraPetch-SemiBold.ttf", 11, 1, False, 32, 126),
               ("S", "ChakraPetch-Medium.ttf", 14, 1, False, 32, 126),
               ("B", "ChakraPetch-Bold.ttf", 14, 1, False, 32, 126),
               ("M", "ChakraPetch-SemiBold.ttf", 21, 1, False, 32, 95),
               ("L", "ChakraPetch-Bold.ttf", 29, 1, False, 32, 95)],
}
import os  # noqa: E402
FONTSET = os.environ.get("X0X_FONTSET", "barlow")    # the others need X0X_FONT_DIRS (not in assets/)
FONTDIRS = [Path(p) for p in os.environ.get("X0X_FONT_DIRS", "").split(":") if p]
SIZES = FONTSETS[FONTSET]
PAD = 2


def upscale(px_, w, h, scale):
    """integer nearest-neighbour enlargement of a w x h glyph"""
    if scale == 1:
        return px_
    big = []
    for y in range(h):
        row = []
        for x in range(w):
            row += [px_[y * w + x]] * scale
        big += row * scale
    return big


def render_bdf(path, scale, first, last):
    """BDF bitmap font (Terminus 8x16): exact pixels, cell = the font bounding box."""
    lines = path.read_text(errors="replace").splitlines()
    fbb = next(l for l in lines if l.startswith("FONTBOUNDINGBOX")).split()
    cw, ch, fx, fy = int(fbb[1]), int(fbb[2]), int(fbb[3]), int(fbb[4])
    base = ch + fy                                     # rows above the baseline
    glyphs, i = {}, 0
    while i < len(lines):
        if lines[i].startswith("ENCODING"):
            code = int(lines[i].split()[1])
            j = i
            while not lines[j].startswith("BBX"):
                j += 1
            bw, bh, bx, by = map(int, lines[j].split()[1:5])
            dw = cw
            k = i
            while not lines[k].startswith("BITMAP"):
                if lines[k].startswith("DWIDTH"):
                    dw = int(lines[k].split()[1])
                k += 1
            rows = [int(r, 16) for r in lines[k + 1:k + 1 + bh]]
            nbits = ((bw + 7) // 8) * 8
            cell = [0] * (cw * ch)
            top = base - (by + bh)
            for r, bits in enumerate(rows):
                for c in range(bw):
                    if bits >> (nbits - 1 - c) & 1:
                        x, y = bx + c, top + r
                        if 0 <= x < cw and 0 <= y < ch:
                            cell[y * cw + x] = 15
            glyphs[code] = (dw, cell)
            i = k + bh
        i += 1
    if last >= 0xDC and 0xDC not in glyphs and ord("U") in glyphs:      # Ü for "HÜGELTON": U with dots on its top row
        dw, cell = glyphs[ord("U")]
        cell = cell[:]
        lit = [x for y in range(ch) for x in range(cw) if cell[y * cw + x]]
        if lit:
            top = min(y for y in range(ch) for x in range(cw) if cell[y * cw + x])
            for x in range(cw):
                cell[top * cw + x] = 15 if x in (min(lit), max(lit)) else 0
            for x in range(cw):
                cell[(top + 1) * cw + x] = 0
            glyphs[0xDC] = (dw, cell)
    missing = [chr(c) for c in range(first, min(last, 126) + 1) if c not in glyphs]
    if missing:
        print(f"font {path.name}: no glyph for {''.join(missing)!r} (drawn as '?')")
    out = []
    for c in range(first, last + 1):
        dw, cell = glyphs.get(c, glyphs[ord("?")])
        bw = cw + 2 * PAD                              # keep the PAD convention of the TTF path
        px_ = []
        for y in range(ch):
            px_ += [0] * PAD + cell[y * cw:(y + 1) * cw] + [0] * PAD
        out.append((dw * scale, bw * scale, upscale(px_, bw, ch, scale)))
    return ch * scale, out


def font_path(name):
    for d in [FONTS, *FONTDIRS]:
        if (d / name).exists():
            return d / name
    raise SystemExit(f"gen_font: {name} not in {FONTS} or X0X_FONT_DIRS")


def render(ttf, px, scale, pixel, first, last):
    if ttf.endswith(".bdf"):
        return render_bdf(font_path(ttf), scale, first, last)
    name, _, var = ttf.partition("@")
    font = ImageFont.truetype(str(font_path(name)), px)
    if var:                                            # a variable font: "wght=600"
        axes = dict(kv.split("=") for kv in var.split(","))
        names = [a["name"] if isinstance(a["name"], str) else a["name"].decode() for a in font.get_variation_axes()]
        font.set_variation_by_axes([float(axes.get(n.lower()[:4], axes.get(n, 0)) or a["default"])
                                    for n, a in zip(names, font.get_variation_axes())])
    feats = None if pixel else ["tnum"]
    asc, desc = font.getmetrics()
    top = max(0, font.getbbox("A8|(", features=feats)[1] - 1)
    h = asc + desc - top
    glyphs = []
    for c in range(first, last + 1):
        ch = chr(c)
        adv = int(round(font.getlength(ch, features=feats)))
        bw = adv + 2 * PAD
        img = Image.new("L", (bw, asc + desc), 0)
        ImageDraw.Draw(img).text((PAD, 0), ch, font=font, fill=255, features=feats)
        img = img.crop((0, top, bw, top + h))
        px_ = [(15 if v >= 128 else 0) if pixel else min(15, (v + 8) // 17) for v in img.tobytes()]
        glyphs.append((adv * scale, bw * scale, upscale(px_, bw, h, scale)))
    return h * scale, glyphs


def ink_box(g, bw, h):
    """the glyph's ink box (bx, by, w, h) inside its bw x h cell; (0, 0, 0, 0) for a blank one"""
    ys = [y for y in range(h) if any(g[y * bw:(y + 1) * bw])]
    xs = [x for x in range(bw) if any(g[y * bw + x] for y in range(h))]
    if not ys:
        return 0, 0, 0, 0
    return xs[0], ys[0], xs[-1] - xs[0] + 1, ys[-1] - ys[0] + 1


def rle(px):
    """gfx.c cv_text's run-length nibbles: 0 n = n + 1 zeros, 15 n = n + 1 fifteens (n 0..15), 1..14 as they are"""
    out, i = [], 0
    while i < len(px):
        v = px[i]
        if v in (0, 15):
            r = 1
            while i + r < len(px) and px[i + r] == v and r < 16:
                r += 1
            out += [v, r - 1]
            i += r
        else:
            out.append(v)
            i += 1
    return out


def unpack(nib, n, runs):
    """cv_text's decoder, for the check: n pixels from the nibbles"""
    out, i = [], 0
    while len(out) < n:
        v = nib[i]
        i += 1
        r = 1
        if runs and v in (0, 15):
            r = nib[i] + 1
            i += 1
        out += [v] * r
    assert len(out) == n
    return out


# large type (Setup > SEL > LARGE TYPE): the faces the large roles add, kept in the resource region (tools/gen_res.py,
# app/gfx_loom.c loom_large_load), not in the image; the values use L, the units and the two-line labels S
LARGE = [("L18", "BarlowSemiCondensed-SemiBold.ttf", 18, 1, False, 32, 126),    # XS's role: labels, pads
         ("L20", "BarlowSemiCondensed-Medium.ttf", 20, 1, False, 32, 126),      # S's: the path line, captions
         ("L20B", "BarlowSemiCondensed-Bold.ttf", 20, 1, False, 32, 126)]      # B's: the header, step numbers


# a face's third coding (runs 2, the faces in the image: main), when it is smaller: each pixel's alpha by one of three
# canonical Huffman codes, the one for the alpha after a 0, after a 15, after anything else (a glyph starts after a 0),
# bits from the high one down, each glyph padded to a byte. The data starts with the three codes (HUF_SZ bytes each: the
# count of codes of each length 1..HUF_LEN, then the alphas in code order, padded to 16); the offsets count from the
# data's start. fm1-base's font_pack (base/tools/gen_font.py) for its own faces; app/gfx_loom.c loom_alpha decodes it
HUF_LEN = 12
HUF_SZ = HUF_LEN + 16


def huff_lengths(freq):
    """{alpha: code length} of a Huffman code for the counts freq (none longer than HUF_LEN)"""
    import heapq
    freq = {k: v for k, v in freq.items() if v}
    while True:
        if len(freq) == 1:
            return {k: 1 for k in freq}
        heap = [(v, i, [k]) for i, (k, v) in enumerate(sorted(freq.items()))]
        heapq.heapify(heap)
        depth = dict.fromkeys(freq, 0)
        n = len(heap)
        while len(heap) > 1:
            x, y = heapq.heappop(heap), heapq.heappop(heap)
            for k in x[2] + y[2]:
                depth[k] += 1
            heapq.heappush(heap, (x[0] + y[0], n, x[2] + y[2]))
            n += 1
        if max(depth.values()) <= HUF_LEN:
            return depth
        freq = {k: (v + 1) // 2 for k, v in freq.items()}


def huff_code(lengths):
    """-> (the table's HUF_SZ bytes, {alpha: (code, length)}): the canonical code (by length, then alpha)"""
    order = sorted(lengths, key=lambda k: (lengths[k], k))
    counts = [sum(1 for k in order if lengths[k] == n) for n in range(1, HUF_LEN + 1)]
    codes, code, at = {}, 0, 0
    for n in range(1, HUF_LEN + 1):
        for _ in range(counts[n - 1]):
            codes[order[at]] = (code, n)
            code += 1
            at += 1
        code <<= 1
    return counts + order + [0] * (16 - len(order)), codes


def ctx_of(prev):
    return 0 if prev == 0 else 1 if prev == 15 else 2


def huff_face(cells):
    """the glyphs' pixels -> (data, offsets): the three codes, then each glyph's bits"""
    freq = [dict.fromkeys(range(16), 0) for _ in range(3)]
    for c in cells:
        prev = 0
        for a in c:
            freq[ctx_of(prev)][a] += 1
            prev = a
    data, codes = [], []
    for f in freq:
        t, k = huff_code(huff_lengths(f) if any(f.values()) else {})
        data += t
        codes.append(k)
    offs = []
    for c in cells:
        offs.append(len(data))
        acc, nb, prev = 0, 0, 0
        for a in c:
            code, n = codes[ctx_of(prev)][a]
            acc, nb, prev = (acc << n) | code, nb + n, a
            while nb >= 8:
                nb -= 8
                data.append((acc >> nb) & 0xFF)
        if nb:
            data.append((acc << (8 - nb)) & 0xFF)
    return data, offs


def huff_unpack(data, off, n):
    """loom_alpha's decoder, for the check: n pixels of the glyph at off"""
    out, bit, prev = [], off * 8, 0
    while len(out) < n:
        t = data[HUF_SZ * ctx_of(prev):]
        code = first = at = 0
        for ln in range(HUF_LEN):
            code |= (data[bit >> 3] >> (7 - (bit & 7))) & 1
            bit += 1
            if code - first < t[ln]:
                prev = t[HUF_LEN + at + code - first]
                break
            at += t[ln]
            first = (first + t[ln]) << 1
            code <<= 1
        else:
            raise AssertionError("no code")
        out.append(prev)
    return out


def face(name, ttf, px, scale, pixel, first, last, huff=False):
    """one face encoded: (h, pad, first, last, runs, advances, boxes, offsets, data); huff: the third coding too, when
    it is smaller (runs 2)"""
    h, glyphs = render(ttf, px, scale, pixel, first, last)
    # each glyph trimmed to its ink box, its pixels row after row as one nibble stream (high nibble first),
    # run-length coded when that is smaller for the face (FONT_<N>.rle); each glyph starts on a byte
    boxes = [ink_box(g, bw, h) for _, bw, g in glyphs]
    cells = [[g[(by + y) * bw + bx + x] for y in range(bh) for x in range(w)]
             for (_, bw, g), (bx, by, w, bh) in zip(glyphs, boxes)]
    plain = sum((len(c) + 1) // 2 for c in cells)
    runs = int(sum((len(rle(c)) + 1) // 2 for c in cells) < plain)
    data, offs = [], []
    for c in cells:
        nib = rle(c) if runs else c
        assert unpack(nib, len(c), runs) == c          # decodes to the glyph's pixels exactly
        offs.append(len(data))
        nib = nib + [0] * (len(nib) & 1)
        data += [(nib[i] << 4) | nib[i + 1] for i in range(0, len(nib), 2)]
    if huff:
        hd, ho = huff_face(cells)
        if len(hd) < len(data):
            for c, o in zip(cells, ho):
                assert huff_unpack(hd, o, len(c)) == c   # decodes to the glyph's pixels exactly
            data, offs, runs = hd, ho, 2
    assert len(data) < 65536 and all(max(b) < 256 for b in boxes)
    return h, PAD * scale, first, last, runs, [a for a, _, _ in glyphs], boxes, offs, data or [0]


def blob(entries=LARGE):
    """the faces as the resource region holds them (little-endian, read in place by app/gfx_loom.c loom_large_load):
    u16 count, u16 0, u32 offset of each face (from the blob's start); each face 4-aligned: u8 h, pad, first, last,
    rle, 3 x 0, u32 offsets (from the face's start) of its advances (u8 a glyph), ink boxes (4 x u8), glyph offsets
    (u16, 2-aligned) and nibbles"""
    import struct
    faces = []
    for e in entries:
        h, pad, first, last, runs, adv, boxes, offs, data = face(*e)
        n = len(adv)
        o_adv = 24
        o_box = o_adv + n
        o_off = (o_box + 4 * n + 1) & ~1
        o_dat = o_off + 2 * n
        b = struct.pack("<8B4I", h, pad, first, last, runs, 0, 0, 0, o_adv, o_box, o_off, o_dat)
        b += bytes(adv) + bytes(v for bx in boxes for v in bx)
        b += bytes(o_off - len(b)) + struct.pack(f"<{n}H", *offs) + bytes(data)
        faces.append(b + bytes(-len(b) % 4))
    head = struct.pack("<HH", len(faces), 0)
    at = len(head) + 4 * len(faces)
    for f in faces:
        head += struct.pack("<I", at)
        at += len(f)
    return head + b"".join(faces)


def main(out):
    lines = [f"/* generated by tools/gen_font.py: font set {FONTSET} */",
             "#pragma once", "#include <stdint.h>", ""]
    total = 0
    for name, ttf, px, scale, pixel, first, last in SIZES:
        h, pad, first, last, runs, adv, boxes, offs, data = face(name, ttf, px, scale, pixel, first, last, huff=True)
        lines.append(f"static const uint8_t LFONT_{name}_DATA[{len(data)}] = {{")
        for i in range(0, len(data), 24):
            lines.append("    " + ", ".join(f"0x{b:02x}" for b in data[i:i + 24]) + ",")
        lines.append("};")
        lines.append(f"static const uint16_t LFONT_{name}_OFF[{len(offs)}] = {{" + ", ".join(map(str, offs)) + "};")
        lines.append(f"static const uint8_t LFONT_{name}_ADV[{len(adv)}] = {{" + ", ".join(map(str, adv)) + "};")
        lines.append(f"static const uint8_t LFONT_{name}_BOX[{len(adv)}][4] = {{" +
                     ", ".join("{%d,%d,%d,%d}" % b for b in boxes) + "};")
        lines.append(f"static const loom_font_t LFONT_{name} = {{ {h}, {pad}, {first}, {last}, {runs}, "
                     f"LFONT_{name}_ADV, LFONT_{name}_BOX, LFONT_{name}_OFF, LFONT_{name}_DATA }};")
        lines.append("")
        size = len(data) + 2 * len(offs) + 5 * len(adv)
        total += size
        print(f"font {name}: {ttf} {px}px h {h}, digit adv {adv[ord('0') - first]}, {size} B"
              f"{('', ' (runs)', ' (huffman)')[runs]}")
    print(f"font set {FONTSET}: {total} B")
    Path(out).write_text("\n".join(lines))


if __name__ == "__main__":
    main(sys.argv[1])
