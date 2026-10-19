#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Make the LOOM-8 site for GitHub Pages (https://charlesvestal.github.io/fm1-loom/):

  index.html                    what LOOM-8 is, and the ways in: try, install, download, manual, source
  manual/index.html             docs/MANUAL.md as a page (tools/make_manual.py), its screenshots in img/
  img/                          screenshots (docs/img)
  emu/                          LOOM-8 in the browser (app/build/emu, from ./build.sh --web)
  words/                        the screen reader's words files (app/web/words, from app/words.tsv)
  install/index.html            the web installer (fm1-base's web/installer.html, with fm1pkg.js, fm1ota.js and
                                the package's metadata inlined; Chrome or Edge, Web MIDI)
  backup/index.html             backup and restore in the browser (fm1-base's web/backup/index.html: the projects and
                                the user presets)
  samples/index.html            the user sample bank in the browser (app/web/samples/: add, replace, rename, reorder,
                                remove; loom_samples.js inlined)
  firmware/loom-VERSION.fwsc    the package the installer writes; also the download

  tools/make_pages.py app/build/loom-0.1.fwsc 0.1 OUT_DIR

The package must be a release build (./build.sh --release X.Y): its identity, FM-1_10XXYYZZ, is what
the installer checks the download against and what the FM-1 reports after the install."""
import html
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

SRC = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SRC / "base" / "tools"))
import make_site as base_site  # noqa: E402  (fm1-base's: the installer, the backup page, the package format)
from make_site import product_of, strip_module  # noqa: E402

REPO = "https://github.com/charlesvestal/fm1-loom"
# the app's look (app/pages.c THEMES[0], "tape deck"): its eight track inks, and the site's palette from it: dark is the
# FM-1's screen, light the same browns on cream paper. Every page of the site wears it (dress()), whatever names its own
# stylesheet gives the colours, with a header to go between them.
TRACKS = ["#e6b432", "#d66030", "#a0aa46", "#46aaa0", "#6e96c8", "#b46e96", "#d2aa78", "#96be96"]
PALETTE = {
    "light": {"bg": "#f4ecdc", "card": "#fffaf0", "now": "#e9dcc4", "rule": "#d8c6a8", "muted": "#6a5440", "ink": "#24180f",
              "accent": "#8a5a00", "on": "#ffffff", "teal": "#2a7a72", "bad": "#b3261e", "focus": "#2f5f9a", "wave": "#a08a6e"},
    "dark": {"bg": "#18120e", "card": "#2c221a", "now": "#46382a", "rule": "#4a3c2e", "muted": "#beaa8c", "ink": "#f4e6c8",
             "accent": "#e6b432", "on": "#18120e", "teal": "#46aaa0", "bad": "#ff7a5c", "focus": "#8fb4e0", "wave": "#786450"},
}


def tokens(p):
    """the palette under every name the site's pages use for it (the landing's, the manual's, the installer's, the
    backup's, the emulator's, the samples page's)"""
    names = {"bg": ("ground", "bg"), "card": ("card", "panel"), "now": ("now",), "rule": ("rule", "line"),
             "muted": ("muted", "dim"), "ink": ("ink", "fg"), "accent": ("accent",), "on": ("on-accent", "keytext"),
             "teal": ("teal",), "bad": ("bad", "red"), "focus": ("focus",), "wave": ("wave",)}
    out = [f"--{n}: {p[k]};" for k, ns in names.items() for n in ns]
    out.append(f"--key: {p['ink']};")
    return " ".join(out)


SITE_CSS = """<style>
:root { %s color-scheme: light; }
@media (prefers-color-scheme: dark) { :root:not([data-theme="light"]) { %s color-scheme: dark; } }
:root[data-theme="dark"] { %s color-scheme: dark; }
.lm-top { max-width: 64rem; margin: 0 auto 28px; display: flex; flex-wrap: wrap; align-items: center; gap: .3rem 1.4rem;
          padding: 14px 0 12px; border-bottom: 1px solid var(--rule); font: 600 1rem/1.4 "Barlow", "Helvetica Neue", Arial, sans-serif; }
.lm-top .lm-brand { display: inline-flex; align-items: center; gap: .55rem; color: var(--ink); text-decoration: none;
                    font: 700 1.35rem/1 "Barlow Semi Condensed", "Barlow", sans-serif; letter-spacing: .02em; }
.lm-top .lm-threads { display: inline-flex; gap: 3px; }
.lm-top .lm-threads i { width: 4px; height: 18px; border-radius: 2px; }
.lm-top nav { display: flex; flex-wrap: wrap; gap: .2rem 1.1rem; margin-left: auto; }
.lm-top nav a { color: var(--muted); text-decoration: none; padding: 2px 0; border-bottom: 2px solid transparent; }
.lm-top nav a:hover, .lm-top nav a:focus-visible { color: var(--ink); }
.lm-top nav a[aria-current="page"] { color: var(--ink); border-bottom-color: var(--accent); }
@media (max-width: 520px) { .lm-top nav { margin-left: 0; width: 100%%; } }
</style>""" % (tokens(PALETTE["light"]), tokens(PALETTE["dark"]), tokens(PALETTE["dark"]))

SECTIONS = [("emu/", "Try it"), ("manual/", "Manual"), ("install/", "Install"), ("samples/", "Samples"),
            ("backup/", "Backup")]


def dress(page, here):
    """a page of the site in the app's colours, with the site's header; here: its section ("" the front page)"""
    up = "../" if here else ""
    threads = "".join(f'<i style="background:{c}"></i>' for c in TRACKS)
    links = "".join(f'<a href="{up}{d}"' + (' aria-current="page"' if d == here else "") + f">{t}</a>"
                    for d, t in SECTIONS)
    links += f'<a href="{REPO}">Source</a>'
    top = (f'<header class="lm-top"><a class="lm-brand" href="{up or "./"}"' + (' aria-current="page"' if not here else "") +
           f'><span class="lm-threads" aria-hidden="true">{threads}</span>LOOM-8</a>'
           f'<nav aria-label="LOOM-8">{links}</nav></header>')
    if page.count("</head>") != 1 or not re.search(r"<body[^>]*>", page):
        raise SystemExit(f"dress: {here or 'index'}: no head or body")
    page = page.replace("</head>", SITE_CSS + "</head>")
    return re.sub(r"(<body[^>]*>)", lambda m: m.group(1) + top, page, count=1)

LANDING = """<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>LOOM-8</title>
<meta name="description" content="A pocket groovebox for the M-VAVE FM-1: eight tracks, each in its own colour, a 64-step sequencer with tricks and locks, weave, throws, a conductor and a song.">
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Barlow:wght@400;600&family=Barlow+Semi+Condensed:wght@600;700&display=swap">
<style>
:root { --ground: #f4f3f1; --ink: #1c1c20; --muted: #66666e; --rule: #dcdbd8; --card: #ffffff; --accent: #c8306f; color-scheme: light; }
@media (prefers-color-scheme: dark) {
  :root:not([data-theme="light"]) { --ground: #000; --ink: #ebebee; --muted: #9a9aa2; --rule: #2e2e34; --card: #141418; --accent: #ff5caa; color-scheme: dark; }
}
:root[data-theme="dark"] { --ground: #000; --ink: #ebebee; --muted: #9a9aa2; --rule: #2e2e34; --card: #141418; --accent: #ff5caa; color-scheme: dark; }
body { background: var(--ground); color: var(--ink); margin: 0; padding-inline: 16px; padding-block: 48px 64px;
       font: 400 17px/1.6 "Barlow", "Helvetica Neue", Arial, sans-serif; }
main { max-width: 42rem; margin: 0 auto; }
h1 { font: 700 3rem/1 "Barlow Semi Condensed", "Barlow", sans-serif; margin: 0 0 .7rem; }
h2 { font: 700 1.4rem "Barlow Semi Condensed", "Barlow", sans-serif; margin: 2.2rem 0 .6rem; }
.threads { display: flex; gap: 6px; margin: 0 0 1.4rem; }
.threads span { flex: 1; height: 6px; border-radius: 3px; }
.lede { font-size: 1.15rem; margin: 0 0 1.6rem; max-width: 36rem; }
.shots { display: grid; grid-template-columns: repeat(auto-fit, minmax(150px, 1fr)); gap: 12px; margin: 0 0 1.8rem; }
.shots img { width: 100%; height: auto; border-radius: 14px; image-rendering: pixelated; border: 1px solid var(--rule); }
.status { border: 1px solid var(--rule); background: var(--card); border-radius: 14px; padding: 12px 16px; margin: 0 0 1.6rem; }
.ways { display: grid; gap: 12px; margin: 0 0 1rem; }
.ways a { display: block; text-decoration: none; color: var(--ink); background: var(--card); border: 1px solid var(--rule);
          border-radius: 14px; padding: 14px 18px; }
.ways a:hover, .ways a:focus-visible { border-color: var(--accent); outline: none; }
.ways strong { font: 600 1.25rem "Barlow Semi Condensed", "Barlow", sans-serif; color: var(--accent); display: block; }
.ways span { color: var(--muted); font-size: .95rem; }
ul.feat { padding-left: 1.2rem; }
ul.feat li { margin: 0 0 .5rem; }
p.small { color: var(--muted); font-size: .9rem; }
a { color: var(--accent); }
code { font-size: .9em; }
</style>
</head>
<body>
<main>
<h1>LOOM-8</h1>
<div class="threads" aria-hidden="true">__THREADS__</div>
<p class="lede">A pocket groovebox for the M-VAVE FM-1. Eight tracks, each in its own colour, on six synth
engines, a sample player, a break slicer and a drum kit; a 64-step sequencer with tricks and parameter locks; and weave, which crosses two
patterns into a third.</p>
<div class="shots">
  <img src="img/screen-beat-playing.png" width="240" height="240" alt="The Steps page on T1, the kit, playing: kick and snare in pink, the playhead lit">
  <img src="img/screen-arrange.png" width="240" height="240" alt="Arrange: every track's pattern in its own colour, the scene number large at the right">
  <img src="img/screen-weave.png" width="240" height="240" alt="The Steps list with WEAVE WITH COPIED picked, its way set to rhythm">
</div>
<div class="status"><strong>Version __VERSION__.</strong> It installs and
uninstalls the way every fm1-base firmware does, and the installer can put M-VAVE's own firmware back. Installing is at your own risk.</div>
<nav class="ways" aria-label="Get LOOM-8">
  <a href="emu/"><strong>Try it in the browser</strong><span>The same code the FM-1 runs, with sound. No FM-1 needed.</span></a>
  <a href="manual/"><strong>Read the manual</strong><span>A ten-minute quick start, every page and gesture, with screenshots.</span></a>
  <a href="install/"><strong>Install</strong><span>From Chrome or Edge, with the FM-1 connected by USB. Nothing to install on the computer.</span></a>
  <a href="samples/"><strong>Samples</strong><span>Put your own samples on the FM-1: add, replace, rename, reorder and remove up to 32.</span></a>
  <a href="backup/"><strong>Backup</strong><span>Save your projects and presets to a file, and load them back later.</span></a>
  <a href="firmware/__PKG__"><strong>Download __PKG__</strong><span>For the command-line installer: <code>python3 base/tools/fm1_install.py __PKG__</code></span></a>
  <a href="__REPO__"><strong>Source</strong><span>GitHub, GPL-3.0. Built on fm1-base and felucca-bsp, from Felucca by Leo Kuroshita (Hügelton Instruments).</span></a>
</nav>

<h2>What's in it</h2>
<ul class="feat">
  <li><b>Eight tracks</b>, each with eight patterns of up to 64 steps: knit (the everyday subtractive voice, with a supersaw unison), rayon (two-operator FM), jacquard (wavetables), bellows (an organ), and cane and voile (physical models and voices from Mutable Instruments Braids) synths; sample and slice, which play the sample bank and chop breaks; or the kit, eleven drum voices on the black keys. 85 factory presets, and 16 of your own.</li>
  <li><b>Tricks</b> on any step: pulse, hold, multiply, ramps, bends, jumps, skips and more; <b>locks</b> for any knob on any step.</li>
  <li><b>Weave</b> a pattern with a copied one: bars or steps in turn, or one's rhythm playing the other's notes.</li>
  <li><b>Players</b>: arp, strum (one key plays a recorded chord) and hold.</li>
  <li><b>The conductor</b> moves the tracks that follow it through a key, a scale and a progression.</li>
  <li><b>Throws</b>: twelve momentary effects a hand, held from the keys, and recorded if you like.</li>
  <li>Two <b>send effects</b> (the reverb with the PlayStation's own reverbs among its modes), a mixer with EQ and a compressor on the master, sixteen <b>scenes</b> and a <b>song</b>.</li>
  <li>A <b>screen reader</b>: the FM-1 says its screen through <a href="https://charlesvestal.github.io/fm1-reader/">FM-1 Screen Reader</a>.</li>
</ul>
<p class="small">To go back to the official firmware, use the installer's "Back to the stock firmware", or
M-VAVE's updater, M-UPGRADE, from <a href="https://www.m-vave.com/download">m-vave.com/download</a>. LOOM-8's
way of working owes a great deal to teenage engineering's OP-XY. M-VAVE, FM-1 and OP-XY are trademarks of their
owners; LOOM-8 is not affiliated with any of them.</p>
</main>
</body>
</html>
"""


def main(pkg, version, out):
    pkg, out = Path(pkg), Path(out)
    raw = pkg.read_bytes()
    product = product_of(raw)
    if not re.fullmatch(r"FM-1_10\d{6}", product):
        raise SystemExit(f"{pkg}: identity {product!r}: make a release build (./build.sh --release X.Y)")
    if b"FELUCCA-LOADER-1" not in raw:
        raise SystemExit(f"{pkg}: no update loader in it")
    name = f"loom-{re.sub(r'[^A-Za-z0-9.-]', '-', version)}.fwsc"
    if out.exists():
        shutil.rmtree(out)
    for d in ("install", "firmware", "backup"):
        (out / d).mkdir(parents=True)
    shutil.copy(pkg, out / "firmware" / name)
    page = (SRC / "base" / "web" / "installer.html").read_text(encoding="utf-8")
    lib = strip_module((SRC / "base" / "web" / "fm1pkg.js").read_text(encoding="utf-8")) + "\n" + \
        strip_module((SRC / "base" / "web" / "fm1ota.js").read_text(encoding="utf-8"))
    meta = json.dumps({"version": version, "product": product, "pkg": "../firmware/" + name})
    for mark in ("/*LIB*/", "/*META*/"):
        if page.count(mark) != 1:
            raise SystemExit(f"base/web/installer.html must contain {mark} once")
    conf = base_site.read_conf(SRC / "app" / "fm1.toml")
    credits = conf.get("credits", "").replace("Built on fm1-base and felucca-bsp, from Felucca by Leo Kuroshita (Hügelton "
                                              "Instruments). ", "")        # (the page says that part itself)
    for key, val in (("NAME", "LOOM-8"), ("REPO", REPO), ("REPO_SHORT", REPO.replace("https://", "")),
                     ("LICENSE", conf.get("licence", "GPL-3.0-only")), ("CREDITS", " " + credits if credits else "")):
        page = page.replace("{{" + key + "}}", val)
    (out / "install" / "index.html").write_text(page.replace("/*LIB*/", lib).replace("/*META*/", meta), encoding="utf-8")
    tools = base_site.tool_pages(SRC / "app", conf, {"NAME": "LOOM-8"})
    (out / "backup" / "index.html").write_text(tools["backup/index.html"], encoding="utf-8")
    # the samples page, its bank code (app/web/samples/loom_samples.js) inlined
    spage = (SRC / "app" / "web" / "samples" / "index.html").read_text(encoding="utf-8")
    if spage.count("/*LIB*/") != 1:
        raise SystemExit("app/web/samples/index.html must contain /*LIB*/ once")
    slib = strip_module((SRC / "app" / "web" / "samples" / "loom_samples.js").read_text(encoding="utf-8"))
    (out / "samples").mkdir()
    (out / "samples" / "index.html").write_text(spage.replace("/*LIB*/", slib), encoding="utf-8")
    shutil.copytree(SRC / "docs" / "img", out / "img")
    threads = "".join(f'<span style="background:{c}"></span>' for c in TRACKS)
    (out / "index.html").write_text(LANDING.replace("__VERSION__", html.escape(version)).replace("__PKG__", name)
                                    .replace("__REPO__", REPO).replace("__THREADS__", threads), encoding="utf-8")
    (out / "manual").mkdir()
    subprocess.run([sys.executable, str(SRC / "tools" / "make_manual.py"), str(SRC / "docs" / "MANUAL.md"),
                    str(out / "manual" / "index.html")], check=True)
    for f in (out / "manual" / "index.html",):    # the manual's pictures are the site's img/
        f.write_text(f.read_text(encoding="utf-8").replace('src="img/', 'src="../img/'), encoding="utf-8")
    emu = SRC / "app" / "build" / "emu"
    if not (emu / "app.wasm").exists():
        raise SystemExit("no app/build/emu/app.wasm: run ./build.sh --web (needs Emscripten)")
    shutil.copytree(emu, out / "emu")
    # the screen reader's words files (fm1-reader): every one ever made, for the devices that point at them
    shutil.copytree(SRC / "app" / "web" / "words", out / "words")
    for path, here in (("index.html", ""), ("manual/index.html", "manual/"), ("install/index.html", "install/"),
                       ("backup/index.html", "backup/"), ("samples/index.html", "samples/"), ("emu/index.html", "emu/")):
        f = out / path
        f.write_text(dress(f.read_text(encoding="utf-8"), here), encoding="utf-8")
    (out / ".nojekyll").write_text("")
    print(f"site: {out}: index.html, manual/, install/ ({product}), backup/, samples/, emu/, words/, firmware/{name} ({len(raw)} B)")


if __name__ == "__main__":
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    main(*sys.argv[1:4])
