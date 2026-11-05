#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Build LOOM-8 with fm1-base (base/, a git submodule) (see BUILDING.md).
#   ./build.sh [--release X.Y] [--host | --web]   (screen mirroring in every build: base/web/mirror, the site's mirror/;
#                                                  --web: fm1-base's emulator page, app/web/emu/ its LOOM-8 bits)
#   JIELI_TOOLCHAIN  JieLi Linux toolchain (default: ~/.jieli/toolchain)
#   AC79_SDK         JieLi AC79 SDK checkout (default: ~/fw-AC79_AIoT_SDK)
#   PYTHON           a Python with Pillow (and libraqm, for the fonts; on macOS Homebrew's)
set -e
case $0 in */*) cd -- "${0%/*}";; esac      # (the repository: this script's folder, wherever it is called from)
: "${JIELI_TOOLCHAIN:=$HOME/.jieli/toolchain}" "${AC79_SDK:=$HOME/fw-AC79_AIoT_SDK}"
export JIELI_TOOLCHAIN AC79_SDK
PY="${PYTHON:-python3}"
[ -f base/tools/build.py ] || { echo "build.sh: no fm1-base in base/ (git submodule update --init --recursive)"; exit 1; }
"$PY" -c 'import PIL' 2>/dev/null || { echo "build.sh: $PY has no Pillow (pip3 install Pillow)"; exit 1; }
case " $* " in
    *" --web "*)                            # the emulator: fm1-base's page with LOOM-8's bits (tools/emu_page.py)
        "$PY" base/tools/build.py app "$@"
        exec "$PY" tools/emu_page.py;;
esac
exec "$PY" base/tools/build.py app "$@"
