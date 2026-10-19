#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Listening renders of the kit's per-key envelopes and filters, the sample model and the sample engine
# (tools/render_drums.c: the units dry) and of the sample engine's eight presets through the FX and the master
# (tools/render_presets.sh's), into build/drum_renders/ with index.html (a player for each) and index.txt.
# The factory samples themselves (22.05 kHz) are in build/drum_renders/factory/. Silent: nothing is played.
#   tools/render_drums.sh
set -e
cd "$(dirname "$0")/.."
CC="${CC:-cc}"
PY="${PYTHON:-python3}"
OUT=build/drum_renders
mkdir -p "$OUT" build/gen build/host
"$PY" tools/gen_samples.py build/gen/samples.h "$OUT/factory" >/dev/null
"$PY" tools/gen_presets.py build/gen/presets.h
[ -f build/gen/wavetables.h ] || "$PY" tools/gen_wavetables.py build/gen/wavetables.h >/dev/null
$CC -O2 -std=c99 -DPLAT_HOST -Iapp/dsp -Ibase/include -Ibuild/gen -o build/host/render_drums tools/render_drums.c \
    app/dsp/drumline.c app/dsp/synth.c app/dsp/sample.c -lm
rm -f "$OUT"/*.wav
build/host/render_drums "$OUT"
sh tools/render_presets.sh >/dev/null
for f in build/presets/*-sample-*.wav; do
    n=$(basename "$f" .wav)
    cp "$f" "$OUT/30-preset-${n#*-sample-}.wav"
    printf '30-preset-%s\t-\tthe sample engine preset "%s": three notes and a chord, through the FX and the master\n' \
        "${n#*-sample-}" "$(echo "${n#*-sample-}" | tr _ ' ')" >> "$OUT/index.txt"
done
"$PY" - "$OUT" <<'EOF'
import html, sys
from pathlib import Path
out = Path(sys.argv[1])
rows = [l.split("\t") for l in (out / "index.txt").read_text().splitlines() if l.strip()]
facts = sorted((out / "factory").glob("*.wav"))
page = ["<!doctype html><meta charset=utf-8><title>LOOM-8 drum renders</title>",
        "<style>body{font:15px system-ui;margin:24px;max-width:860px}td{padding:4px 8px;vertical-align:middle}"
        "audio{height:30px}</style><h1>LOOM-8: per-key envelopes and filters, samples</h1>",
        "<p>Dry from the units (no FX, no master) unless it says so. The factory samples are placeholders, made by "
        "tools/gen_samples.py.</p><table>"]
for name, peak, what in rows:
    page.append(f"<tr><td><b>{html.escape(name)}</b><br>{html.escape(what)}</td><td>{html.escape(peak)} dBFS</td>"
                f"<td><audio controls preload=none src='{html.escape(name)}.wav'></audio></td></tr>")
page.append("</table><h2>The factory samples</h2><table>")
for f in facts:
    page.append(f"<tr><td>{html.escape(f.stem)}</td><td><audio controls preload=none src='factory/{f.name}'></audio></td></tr>")
page.append("</table>")
(out / "index.html").write_text("\n".join(page))
print(f"renders: {out}/index.html ({len(rows)} renders, {len(facts)} factory samples)")
EOF
