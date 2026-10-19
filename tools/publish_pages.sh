#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Build the GitHub Pages site for release VERSION (tools/make_pages.py) and push it to the gh-pages
# branch of origin, replacing what was there (the branch holds only the generated site).
#   tools/publish_pages.sh 0.1-beta          (after ./build.sh --release 0.1-beta; PYTHON: one with Pillow)
#   SITE_PASSWORD=...  every page encrypted behind it (staticrypt, by npx; one salt, so it is asked once and
#                      remembered): a gate for the pages, not for the files they link (the package, the emulator)
set -e
cd "$(dirname "$0")/.."
V="${1:?usage: tools/publish_pages.sh VERSION}"
PY="${PYTHON:-python3}"
PKG="app/build/loom-$V.fwsc"
[ -f "$PKG" ] || { echo "no $PKG: run ./build.sh --release $V first"; exit 1; }
PYTHON="$PY" sh build.sh --web --release "$V"
node tests/host/emu_test.mjs app/build/emu/app.wasm
"$PY" tools/make_pages.py "$PKG" "$V" build/pages
if [ -n "${SITE_PASSWORD:-}" ]; then
    SALT="$(openssl rand -hex 16)"
    (cd build/pages && find . -name '*.html' | while read -r f; do
        STATICRYPT_PASSWORD="$SITE_PASSWORD" npx --yes staticrypt@3 "$f" -c false -s "$SALT" --short --remember 0 \
            --template-title "LOOM-8" --template-instructions "LOOM-8 is not out yet: the password, please." \
            --template-button "OPEN" --template-color-primary "#c8306f" --template-color-secondary "#f4f3f1" \
            -d "$(dirname "$f")" >/dev/null
    done)
    echo "the pages are behind the password"
fi
REMOTE="$(git remote get-url origin)"
SHA="$(git rev-parse --short HEAD)"
cd build/pages
rm -rf .git
git init -q -b gh-pages
git add -A
git commit -q -m "Site for LOOM-8 $V (from $SHA)"
git push -q -f "$REMOTE" gh-pages
rm -rf .git
# ask GitHub Pages to build it now: a push alone has left the first deploy stuck on "building"
REPO="$(printf %s "$REMOTE" | sed -E 's#(git@github.com:|https://github.com/)##; s#\.git$##')"
gh api -X POST "repos/$REPO/pages/builds" >/dev/null 2>&1 || echo "(could not ask GitHub Pages to build: check the repository's Pages settings)"
echo "pushed the site for $V to gh-pages"
