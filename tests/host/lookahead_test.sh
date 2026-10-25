#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# The look-ahead (engine.c: the sequencer a block ahead of the sound) against the sequencer live, in the host
# simulator: tests/host/lookahead_rec.xy (keys recorded at every offset across the step boundaries, a knob recorded)
# with it off and on, the projects saved: the same bytes; and the MIDI each sends: the same messages at the same
# times, but the transport's stop (FC), which the look-ahead sends a block (5.8 ms) later: a stop reaches the
# sequencer after the block it has already made. And the same script without REC: a different project (so the
# recording is there to compare). Needs the host simulator (./build.sh --host).
set -eu
cd "$(dirname "$0")/../.."
H=app/build/host/loom_host
D=build/host/lookahead
rm -rf "$D" && mkdir -p "$D/off" "$D/on" "$D/norec"
for m in off on norec; do
    v=0; [ $m = on ] && v=1
    sed "s|^projsave 1 la_test|projsave 1 la_test\nstoresave $D/$m/store.bin|" tests/host/lookahead_rec.xy > "$D/$m/run.xy"
    [ $m = norec ] && sed -i.bak "s/^press REC$/wait 1/; s/^release REC$/wait 1/" "$D/$m/run.xy"
    LOOM_LOAD=0 LOOM_LOOKAHEAD=$v "$H" "$D/$m/run.xy" "$D/$m" > "$D/$m/log.txt" 2>&1 || { tail -5 "$D/$m/log.txt"; exit 1; }
done
bad=0
if cmp -s "$D/off/store.bin" "$D/on/store.bin"; then echo "  ok   what was recorded: the same project, byte for byte"
else echo "  FAIL what was recorded differs (the projects: $D/off, $D/on)"; bad=1; fi
if cmp -s "$D/off/store.bin" "$D/norec/store.bin"; then echo "  FAIL nothing was recorded (the same project without REC)"; bad=1
else echo "  ok   ... and it is a recording (without REC: another project)"; fi
grep -v ": FC " "$D/off/midi_out.txt" > "$D/off/midi.txt"
grep -v ": FC " "$D/on/midi_out.txt" > "$D/on/midi.txt"
if cmp -s "$D/off/midi.txt" "$D/on/midi.txt"; then echo "  ok   the MIDI out: the same messages at the same times ($(wc -l < "$D/on/midi.txt" | tr -d ' '))"
else echo "  FAIL the MIDI out differs:"; diff "$D/off/midi.txt" "$D/on/midi.txt" | head -8; bad=1; fi
echo "       the stop: $(grep ': FC ' "$D/off/midi_out.txt" | cut -d: -f1) live, $(grep ': FC ' "$D/on/midi_out.txt" | cut -d: -f1) ahead"
exit $bad
