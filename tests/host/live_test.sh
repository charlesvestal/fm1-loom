#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# The live mirror's round trip (app.c app_state_get / app_state_set; fm1-base web/emu/live.html): one simulator is the
# FM-1 (tests/live/fm1.xy: edits, a page, the SEL list; its backup, state, screen), a fresh one the page's copy
# (tests/live/page.xy: the backup, then the state). Their state signatures and screens must be the same.
# Needs the host simulator (./build.sh --host) and a Python with Pillow (PYTHON).
set -eu
cd "$(dirname "$0")/../.."
OUT=app/build/live_test
rm -rf "$OUT" && mkdir -p "$OUT/fm1" "$OUT/page"
for s in fm1 page; do
    sed "s|\$OUT|$PWD/$OUT|g" "tests/live/$s.xy" > "$OUT/$s.xy"
    LOOM_LOAD=0 app/build/host/loom_host "$OUT/$s.xy" "$OUT/$s" > "$OUT/$s.log" 2>&1 || { tail -5 "$OUT/$s.log"; exit 1; }
done
a=$(grep '^statesig' "$OUT/fm1.log"); b=$(grep '^statesig' "$OUT/page.log")
[ -n "$a" ] && [ "$a" = "$b" ] || { echo "live: the signatures differ: FM-1 $a, page $b"; exit 1; }
"${PYTHON:-python3}" -c "
import sys
from PIL import Image, ImageChops
a = Image.open('$OUT/fm1/fm1.png').convert('RGB'); b = Image.open('$OUT/page/page.png').convert('RGB')
box = ImageChops.difference(a, b).getbbox()
sys.exit(f'live: the screens differ in {box}' if box else 0)"
echo "live: the page's copy is the FM-1's ($a), the screens the same"
