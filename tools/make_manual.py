#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
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


IMG = re.compile(r'<img src="([^"]+)"[^>]*?alt="([^"]*)"[^>]*?title="([^"]*)"[^>]*>')   # the manual's images


def figures(md_text):
    """the text without its images, and the images as one row of captioned figures (or "")"""
    figs = [f'<figure><img src="{m.group(1)}" alt="{m.group(2)}" width="240" height="240" loading="lazy">'
            f'<figcaption>{m.group(3)}</figcaption></figure>' for m in IMG.finditer(md_text)]
    text = IMG.sub("", md_text).strip()
    return text, ('<div class="shots">' + "".join(figs) + "</div>") if figs else ""


def block(md_text, tag="p", cls=""):
    text, shots = figures(md_text)
    out = f"<{tag}{cls}>{inline(text)}</{tag}>" if text else ""
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
    while i < len(lines):
        ln = lines[i]
        if ln.startswith("#"):
            flush()
            n = len(ln) - len(ln.lstrip("#"))
            t = ln[n:].strip()
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
            t = ['<div class="wrap" tabindex="0" role="region" aria-label="table">']
            t[0] += "<table>"
            for k, r in enumerate(rows):
                tag = "th" if k == 0 else "td"
                t.append("<tr>" + "".join(f"<{tag}>{inline(c)}</{tag}>" for c in r) + "</tr>")
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
