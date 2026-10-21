#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Build LOOM-8 with fm1-base (base/, a git submodule) (see BUILDING.md).
#   ./build.sh [--demo | --release X.Y] [--host | --web]   (--demo: with screen mirroring, for demos)
#   JIELI_TOOLCHAIN  JieLi Linux toolchain (default: ~/.jieli/toolchain)
#   AC79_SDK         JieLi AC79 SDK checkout (default: ~/fw-AC79_AIoT_SDK)
#   PYTHON           a Python with Pillow (and libraqm, for the fonts; on macOS Homebrew's)
set -e
cd "$(dirname "$0")"
export JIELI_TOOLCHAIN="${JIELI_TOOLCHAIN:-$HOME/.jieli/toolchain}"
export AC79_SDK="${AC79_SDK:-$HOME/fw-AC79_AIoT_SDK}"
PY="${PYTHON:-python3}"
[ -f base/tools/build.py ] || { echo "build.sh: no fm1-base in base/ (git submodule update --init --recursive)"; exit 1; }
"$PY" -c 'import PIL' 2>/dev/null || { echo "build.sh: $PY has no Pillow (pip3 install Pillow)"; exit 1; }
if [ "${1:-}" = "--demo" ]; then             # a demo build: screen mirroring for the viewer (base/web/mirror,
    shift                                    # base/tools/fm1_mirror.py), spending the pool's spare; never a release
    case " $* " in *" --release "*) echo "build.sh: --demo is never a release"; exit 1;; esac
    export FM1B_MIRROR=1 FM1B_POOL_SPARE=0 FM1B_BACKUP=0  # (no backup and restore: its buffer is 3.8 KB of the pool, which the mirror needs; USB audio stays, to record a demo)
    "$PY" base/tools/build.py app "$@"
    case " $* " in *" --host "*|*" --web "*) exit 0;; esac
    mkdir -p build/mirror                    # the viewer, fm1-base's, with LOOM-8's name (Chrome or Edge: Web MIDI)
    sed 's/{{NAME}}/LOOM-8/g' base/web/mirror/index.html > build/mirror/index.html
    echo "mirror   $(pwd)/build/mirror/index.html"
    exit 0
fi
exec "$PY" base/tools/build.py app "$@"
