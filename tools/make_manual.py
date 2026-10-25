#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Render docs/MANUAL.md as one HTML page (the site's manual, and a page to read beside the FM-1).

  tools/make_manual.py docs/MANUAL.md OUT.html [--fragment]

A small Markdown subset is enough for the manual: headings, paragraphs, bullet and numbered lists, tables,
bold, code, links. --fragment leaves out the document skeleton (an artifact viewer adds its own)."""
import html
import re
import sys
from pathlib import Path

STYLE = """
<style>
:root { --bg: #f5f4f2; --ink: #1c1c1e; --dim: #6a6a70; --line: #d8d7d4; --panel: #ebeae7; --key: #2a2a2e;
        --keytext: #f2f2f4; --red: #d9322b; --now: #e4e3df; }
@media (prefers-color-scheme: dark) { :root:not([data-theme="light"]) { color-scheme: dark; --bg: #000; --ink: #ebebee;
        --dim: #9a9aa0; --line: #3a3a40; --panel: #1e1e22; --key: #e6e6ea; --keytext: #111; --red: #e82c26; --now: #2a2a2f; } }
:root[data-theme="dark"] { color-scheme: dark; --bg: #000; --ink: #ebebee; --dim: #9a9aa0; --line: #3a3a40; --panel: #1e1e22;
        --key: #e6e6ea; --keytext: #111; --red: #e82c26; --now: #2a2a2f; }
body { background: var(--bg); color: var(--ink); margin: 0; padding-block: 24px 64px; padding-inline: 16px;
       font: 16px/1.55 -apple-system, "Helvetica Neue", Arial, sans-serif; }
main { max-width: 720px; margin: 0 auto; }
h1, h2, h3 { font-family: "Barlow Semi Condensed", "Helvetica Neue", Arial, sans-serif; text-wrap: balance; line-height: 1.15; }
h1 { font-size: 40px; font-weight: 700; margin: 0 0 8px; letter-spacing: -0.01em; }
h2 { font-size: 26px; font-weight: 600; margin: 48px 0 12px; padding-top: 16px; border-top: 1px solid var(--line); }
h3 { font-size: 19px; font-weight: 600; margin: 28px 0 8px; color: var(--ink); }
p, li { max-width: 68ch; }
a { color: inherit; text-decoration-color: var(--red); text-underline-offset: 3px; }
strong { font-weight: 600; }
kbd, strong.key { display: inline-block; font: 600 12px/1 "Barlow Semi Condensed", Arial, sans-serif; letter-spacing: 0.04em;
       padding: 3px 6px 2px; border-radius: 4px; background: var(--key); color: var(--keytext); vertical-align: 1px; }
code { font: 14px ui-monospace, Menlo, monospace; background: var(--panel); padding: 1px 5px; border-radius: 3px; }
table { border-collapse: collapse; width: 100%; font-size: 15px; margin: 12px 0; }
.wrap { overflow-x: auto; }
th, td { text-align: left; vertical-align: top; padding: 7px 10px; border-bottom: 1px solid var(--line); }
th { color: var(--dim); font-weight: 600; font-size: 12px; letter-spacing: 0.06em; text-transform: uppercase; }
ol.steps > li { margin: 0 0 12px; }
ul { padding-left: 20px; }
.toc { display: flex; flex-wrap: wrap; gap: 6px 14px; padding: 0; margin: 18px 0 28px; list-style: none; max-width: none; }
.toc li { max-width: none; }
.toc a { color: var(--dim); text-decoration: none; font-size: 14px; }
.toc a:hover { color: var(--red); }
.lede { color: var(--dim); font-size: 17px; }
.rec { color: var(--red); }
.shots { display: flex; flex-wrap: wrap; gap: 14px 12px; margin: 14px 0 22px; }
figure { margin: 0; width: 240px; max-width: calc(50% - 6px); }
figure img { display: block; width: 100%; height: auto; border-radius: 6px; background: #000;
             outline: 1px solid var(--line); }
figcaption { margin-top: 6px; font-size: 13px; line-height: 1.3; color: var(--dim); }
.video { aspect-ratio: 16 / 9; margin: 12px 0 20px; border-radius: 8px; overflow: hidden; background: #000; }
.video iframe { width: 100%; height: 100%; border: 0; display: block; }
.hint { display: block; width: 110px; height: auto; margin-top: 8px; }
.hints td { padding-block: 12px 14px; } .hints td + td { vertical-align: middle; }
#fm1 .hb { fill: #2c2d31; } #fm1 .hs { fill: #111; } #fm1 .off { fill: #4b4d53; } #fm1 .kw { fill: #a3a3a7; }
#fm1 .kb, .hint .kb { fill: #232427; } .hint .on, .legend .on { fill: var(--red); }
.hint .press, .legend .press { fill: #4b4d53; stroke: var(--red); stroke-width: 4; }
.hint .arc, .legend .arc { fill: none; stroke: var(--red); stroke-width: 3.5; stroke-linecap: round; }
.legend { display: flex; flex-wrap: wrap; align-items: center; gap: 4px 6px; color: var(--dim); font-size: 13px; }
.legend svg { width: 20px; height: 15px; margin-left: 8px; } .legend svg:first-child { margin-left: 0; }
.legend .press { stroke-width: 2; } .legend .arc { stroke-width: 2; }
@media (max-width: 340px) { figure { max-width: 100%; } }
@media (max-width: 480px) { h1 { font-size: 32px; } h2 { font-size: 22px; } }
</style>
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Barlow+Semi+Condensed:wght@600;700&display=swap">
"""


def inline(s):
    s = html.escape(s, quote=False)
    s = re.sub(r"`([^`]+)`", r"<code>\1</code>", s)
    s = re.sub(r"\*\*([A-Z0-9−+–\- ]{2,12})\*\*", r"<kbd>\1</kbd>", s)        # a button or knob name
    s = re.sub(r"\*\*(.+?)\*\*", r"<strong>\1</strong>", s)
    s = re.sub(r"\[([^\]]+)\]\(([^)]+)\)", r'<a href="\2">\1</a>', s)
    return s


# The cheat sheet's hints: a small drawn FM-1 beside a row, the controls it uses lit (held filled, pressed outlined,
# turned with an arc, keys played lit). The controls come from the row's bold names (the first sentence naming one),
# each lit as the verb before it says (hold / press / tap / turn; else a knob turns and a button is pressed). A row
# can say it itself in a comment, which GitHub doesn't show: <!-- hint: hold HOME; press REC; black 1; white 1-11 -->
HINT_KNOBS = {"MASTER": (22, 20), "SELECT": (50, 20), "PRESETS": (22, 46), "ALGORITHM": (50, 46),
              "KNOB 1": (160, 18), "KNOB 2": (184, 18), "KNOB 3": (208, 18), "KNOB 4": (232, 18)}
HINT_BTNS = {n: (150 + i * 22, 36) for i, n in enumerate(["FX", "SEL", "ENV", "LFO", "EDIT", "GLO"])}
HINT_BTNS.update({n: (150 + i * 22, 58) for i, n in enumerate(["HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC"])})
HINT_BTNS.update({"OCT−": (14, 66), "OCT+": (40, 66)})
HINT_ALIAS = {"OCT-": ["OCT−"], "OCT": ["OCT−", "OCT+"], "KNOB 1–4": ["KNOB 1", "KNOB 2", "KNOB 3", "KNOB 4"]}
HINT_BLACK_AFTER = [0, 1, 2, 4, 5, 7, 8, 9, 11, 12, 14]          # black key k sits after white key HINT_BLACK_AFTER[k]
HINT_NOTE = re.compile(r"<!--\s*hint:(.*?)-->")


def hint_controls(name):
    return HINT_ALIAS.get(name, [name] if name in HINT_KNOBS or name in HINT_BTNS else [])


def hint_parse(cell):
    """(hold, press, turn, white, black) for a row's cell, or None when it uses no control"""
    hold, press, turn, white, black = set(), set(), set(), set(), set()
    m = HINT_NOTE.search(cell)
    if m:
        for part in m.group(1).split(";"):
            verb, _, rest = part.strip().partition(" ")
            if verb in ("white", "black"):
                keys = white if verb == "white" else black
                for r in rest.split(","):
                    a, _, b = r.strip().partition("-")
                    keys.update(range(int(a), int(b or a) + 1))
            else:
                for n in rest.split(","):
                    {"hold": hold, "press": press, "turn": turn}[verb].update(hint_controls(n.strip()))
        return hold, press, turn, white, black
    for sentence in re.split(r"(?<=[.:])\s+(?=[A-Z])", HINT_NOTE.sub("", cell)):
        last = 0
        for b in re.finditer(r"\*\*([^*]+)\*\*", sentence):
            names = hint_controls(b.group(1))
            if not names:
                continue
            verbs = re.findall(r"\b(hold|press|tap|turn)", sentence[last:b.start()].lower())
            last = b.end()
            for n in names:
                verb = verbs[-1] if verbs else ("turn" if n in HINT_KNOBS else "press")
                if n in HINT_KNOBS and verb != "hold":
                    turn.add(n)
                else:
                    (hold if verb == "hold" else press).add(n)
        if hold or press or turn:
            return hold, press, turn, white, black
    return None


def hint_parts(lit=None):
    """the panel's shapes: every control unlit (lit None, the shared drawing), or only those lit"""
    hold, press, turn, white, black = lit or (set(),) * 5
    s = [] if lit else ['<rect x="1" y="1" width="288" height="126" rx="12" class="hb"/>',
                        '<rect x="78" y="8" width="56" height="56" rx="6" class="hs"/>']
    for n, (x, y) in HINT_KNOBS.items():
        if not lit or n in turn or n in hold:
            s.append(f'<circle cx="{x}" cy="{y}" r="8.5" class="{"on" if lit else "off"}"/>')
        if n in turn:
            s.append(f'<path d="M{x - 12},{y + 4} A12.5,12.5 0 1 1 {x + 12},{y + 4} l-5,-1 m5,1 l1,-5" class="arc"/>')
    for n, (x, y) in HINT_BTNS.items():
        cls = "on" if n in hold else ("press" if n in press else "off")
        if not lit or cls != "off":
            s.append(f'<rect x="{x}" y="{y}" width="{22 if n.startswith("OCT") else 18}" height="14" rx="3" '
                     f'class="{cls}"/>')
    kw = 270 / 16
    for i in range(16):
        if not lit or i + 1 in white:
            s.append(f'<rect x="{10 + i * kw:.1f}" y="80" width="{kw - 2:.1f}" height="40" rx="2" '
                     f'class="{"on" if lit else "kw"}"/>')
    for k, a in enumerate(HINT_BLACK_AFTER):
        if not lit or white or k + 1 in black:                 # over a lit white key, every black key again
            s.append(f'<rect x="{10 + (a + 1) * kw - kw * 0.34:.1f}" y="78" width="{kw * 0.68 - 1:.1f}" height="26" '
                     f'rx="2" class="{"on" if lit and k + 1 in black else "kb"}"/>')
    return "".join(s)


HINT_PANEL = ('<svg width="0" height="0" style="position:absolute" aria-hidden="true"><symbol id="fm1" viewBox="0 0 290 128">'
              + hint_parts() + "</symbol></svg>")


def hint_svg(cell, label):
    lit = hint_parse(cell)
    if not lit:
        return ""
    return (f'<svg class="hint" viewBox="0 0 290 128" role="img" aria-label="{html.escape(label)}">'
            f'<use href="#fm1"/>{hint_parts(lit)}</svg>')


HINT_LEGEND = ('<p class="legend"><svg viewBox="0 0 22 16"><rect x="2" y="2" width="18" height="12" rx="3" class="on"/></svg>'
               ' hold <svg viewBox="0 0 22 16"><rect x="2" y="2" width="18" height="12" rx="3" class="press"/></svg> press '
               '<svg viewBox="0 0 22 16"><circle cx="11" cy="10" r="4.5" class="on"/><path d="M3,11 A8,8 0 1 1 19,11" '
               'class="arc"/></svg> turn <svg viewBox="0 0 22 16"><rect x="6" y="1" width="10" height="14" rx="2" '
               'class="on"/></svg> keys</p>')
HINT_SECTIONS = ("How do I…", "The buttons")


IMG = re.compile(r'<img src="([^"]+)"[^>]*?alt="([^"]*)"[^>]*?title="([^"]*)"[^>]*>')   # the manual's images


def figures(md_text):
    """the text without its images, and the images as one row of captioned figures (or "")"""
    figs = [f'<figure><img src="{m.group(1)}" alt="{m.group(2)}" width="240" height="240" loading="lazy">'
            f'<figcaption>{m.group(3)}</figcaption></figure>' for m in IMG.finditer(md_text)]
    text = IMG.sub("", md_text).strip()
    return text, ('<div class="shots">' + "".join(figs) + "</div>") if figs else ""


YOUTUBE = re.compile(r"https://www\.youtube\.com/watch\?v=([\w-]+)")


def block(md_text, tag="p", cls=""):
    text, shots = figures(md_text)
    out = f"<{tag}{cls}>{inline(text)}</{tag}>" if text else ""
    m = YOUTUBE.search(text)                   # a paragraph linking a video: the video under it
    if m:
        out += (f'<div class="video"><iframe src="https://www.youtube-nocookie.com/embed/{m.group(1)}" '
                'title="LOOM-8 video" loading="lazy" allow="accelerometer; autoplay; clipboard-write; encrypted-media; '
                'gyroscope; picture-in-picture" allowfullscreen referrerpolicy="strict-origin-when-cross-origin"></iframe></div>')
    return out + shots


def slug(t):
    return re.sub(r"[^a-z0-9]+", "-", t.lower()).strip("-")


def render(md):
    out, i, lines = [], 0, md.split("\n")
    para = []

    def flush():
        if para:
            out.append(block(" ".join(para)))
            para.clear()

    first_p = True
    section = ""
    while i < len(lines):
        ln = lines[i]
        if ln.startswith("#"):
            flush()
            n = len(ln) - len(ln.lstrip("#"))
            t = ln[n:].strip()
            section = t
            out.append(f'<h{n} id="{slug(t)}">{inline(t)}</h{n}>')
            i += 1
            continue
        if ln.startswith("|"):
            flush()
            rows = []
            while i < len(lines) and lines[i].startswith("|"):
                rows.append([c.strip() for c in lines[i].strip().strip("|").split("|")])
                i += 1
            rows = [r for r in rows if not all(re.fullmatch(r"-*", c) for c in r)]
            hints = section in HINT_SECTIONS
            t = [HINT_PANEL + HINT_LEGEND if section == HINT_SECTIONS[0] else ""]
            t.append('<div class="wrap" tabindex="0" role="region" aria-label="table">')
            t[-1] += '<table class="hints">' if hints else "<table>"
            for k, r in enumerate(rows):
                tag = "th" if k == 0 else "td"
                cells = [inline(HINT_NOTE.sub("", c).strip()) for c in r]
                if hints and k:                                # the hint under the row's first cell
                    label = re.sub(r"\*\*", "", r[0]) + ": the controls it uses"
                    cells[0] += hint_svg(r[0] if section == HINT_SECTIONS[1] else r[1], label)
                t.append("<tr>" + "".join(f"<{tag}>{c}</{tag}>" for c in cells) + "</tr>")
            t.append("</table></div>")
            out.append("".join(t))
            continue
        m = re.match(r"^(\s*)([-*]|\d+\.)\s+(.*)", ln)
        if m:
            flush()
            ordered = m.group(2)[0].isdigit()
            items = []
            while i < len(lines):
                m = re.match(r"^(\s*)([-*]|\d+\.)\s+(.*)", lines[i])
                if m and len(m.group(1)) == 0:
                    items.append(m.group(3))
                    i += 1
                elif lines[i].startswith("  ") and items and lines[i].strip():
                    items[-1] += " " + lines[i].strip()
                    i += 1
                elif (not lines[i].strip() and items and i + 1 < len(lines) and lines[i + 1].startswith("   ")
                      and lines[i + 1].strip()):
                    i += 1                    # a blank line, then an indented paragraph: still this item
                else:
                    break
            toc = all(re.match(r"\[.*\]\(#.*\)", it) for it in items)
            cls = ' class="toc"' if toc else (' class="steps"' if ordered else "")
            tag = "ol" if ordered else "ul"
            lis = []
            for it in items:
                text, shots = figures(it)
                lis.append(f"<li>{inline(text)}{shots}</li>")
            out.append(f"<{tag}{cls}>" + "".join(lis) + f"</{tag}>")
            continue
        if not ln.strip():
            flush()
            i += 1
            continue
        if first_p and not out[-1:] == [] and out and out[-1].startswith("<h1"):
            para.append(ln.strip())
            i += 1
            # the first paragraph after the title is the lede
            while i < len(lines) and lines[i].strip() and not lines[i].startswith(("#", "|", "-", "*")):
                para.append(lines[i].strip())
                i += 1
            out.append('<p class="lede">' + inline(" ".join(para)) + "</p>")
            para.clear()
            first_p = False
            continue
        para.append(ln.strip())
        i += 1
    flush()
    return "\n".join(out)


def main():
    src, dst = Path(sys.argv[1]), Path(sys.argv[2])
    body = render(src.read_text())
    page = f"<title>LOOM-8 manual</title>{STYLE}<main>{body}</main>"
    if "--fragment" not in sys.argv:
        page = f'<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">{page}</html>'.replace("<main>", "</head><body><main>").replace("</main>", "</main></body>")
    dst.write_text(page)
    print(f"manual: {dst} ({len(page)} B)")


if __name__ == "__main__":
    main()
