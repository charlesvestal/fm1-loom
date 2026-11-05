#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""The emulator's page: fm1-base's (base/web/emu/index.html, which base/tools/build.py app --web writes to
app/build/emu/index.html) with the app's own bits put in from app/web/emu/page.html. That file is sections, each
after a line "<!-- emu-page: NAME -->":

  name         the app's name on the page (the title, the heading, the CPU line), for fm1.toml's file name
  description  the page's meta description
  head         put in at the end of <head> (the app's colours and styles)
  header       the page's header, in place of fm1-base's (the heading alone; empty: none there)
  top          put in at the start of <main> (a header inside the main part, under a site's own header)
  bar          put in at the end of the bar under the panel (status, mute, reset)
  reader       put in after the Screen Reader box
  end          put in at the end of <main> (the help)

What happens on the page while it runs (its keys, its MIDI, ...) is app/web/emu/app.js and app_worklet.js, which
fm1-base's build loads with the emulator's parts. Every place it puts something must be in the page exactly once, so
a change to fm1-base's page shows here as an error, not as a page without its help.

    tools/emu_page.py            (./build.sh --web runs it)
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PAGE = ROOT / "app" / "build" / "emu" / "index.html"
PARTS = ROOT / "app" / "web" / "emu" / "page.html"


def sections(text):
    out, name = {}, None
    for line in text.splitlines(keepends=True):
        m = re.fullmatch(r"\s*<!-- emu-page: ([a-z]+) -->\s*", line)
        if m:
            name = m[1]
            out[name] = ""
        elif name:
            out[name] += line
    return {k: v.strip("\n") for k, v in out.items()}


def once(page, old, new, what):
    n = page.count(old)
    if n != 1:
        raise SystemExit(f"emu_page: fm1-base's page has {n} of {what} ({old[:60]!r}), not one")
    return page.replace(old, new)


def main():
    m = re.search(r'^name\s*=\s*"([^"]*)"', (ROOT / "app" / "fm1.toml").read_text(), re.M)   # (as base/tools/build.py)
    name = m[1] if m else ""
    page, parts = PAGE.read_text(), sections(PARTS.read_text())
    if "emu-page" in page:
        raise SystemExit(f"emu_page: {PAGE} already has the app's bits (build it again: ./build.sh --web)")
    if "name" in parts:
        show = parts["name"].strip()
        page = once(page, f"<title>{name} in the browser</title>", f"<title>{show} in the browser</title>", "the title")
        page = once(page, f'content="{name} for the M-VAVE FM-1', f'content="{show} for the M-VAVE FM-1', "the description")
        page = once(page, f"`{name} uses ", f"`{show} uses ", "the CPU line")
    if "description" in parts:
        page = re.sub(r'(<meta name="description" content=")[^"]*(">)',
                      lambda m: m[1] + parts["description"].strip().replace('"', "&quot;") + m[2], page, count=1)
    if "head" in parts:
        page = once(page, "</head>", f"<!-- emu-page: head (app/web/emu/page.html) -->\n{parts['head']}\n</head>", "</head>")
    if "header" in parts:
        m = re.search(r"<header>\s*<h1>[^<]*</h1>\s*</header>", page)
        if not m:
            raise SystemExit("emu_page: fm1-base's page has no header with its heading alone")
        page = page[:m.start()] + parts["header"] + page[m.end():]
    if "top" in parts:
        page = once(page, "<main>\n", "<main>\n" + parts["top"] + "\n", "<main>")
    if "bar" in parts:
        page = once(page, '<span id="reset-why" class="sr">', parts["bar"] + '\n  <span id="reset-why" class="sr">', "the bar's end")
    if "reader" in parts:
        page = once(page, "</details>\n", "</details>\n" + parts["reader"] + "\n", "the Screen Reader box")
    if "end" in parts:
        page = once(page, "</main>", parts["end"] + "\n</main>", "</main>")
    PAGE.write_text(page)
    print(f"emu      page: fm1-base's, with {', '.join(parts)} from {PARTS.relative_to(ROOT)}")


if __name__ == "__main__":
    sys.exit(main())
