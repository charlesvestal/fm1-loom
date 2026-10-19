#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build the unity build's main-loop code for size (fm1-base's tools/build.py runs it: app/fm1.toml ir_pass): the
functions defined in SIZE_FILES (the UI, the pages, the drawing, the stores, the update, the backup, the screen
reader) get LLVM's `minsize` (about -Oz) while the rest of fm1-base's device/unity.c (the audio and timer
interrupts, the engine glue, USB, MIDI, libc) keeps -Os. The idea is Melodee's (github.com/keremimo/melodee,
tools/size_fns.py): the JieLi clang has no `#pragma clang attribute`, so the build compiles unity.c to LLVM IR
without the optimiser, this script adds `minsize` to those definitions, and the IR is then compiled at -Os
(without the edit the round trip is byte-identical to compiling unity.c directly).

Safety net: nothing an interrupt can reach is marked. The roots are the C bodies of the ISRs
(hal/fm1_isr.S: *_irq), the crash handlers and the second core's code; from them every function and
global they name is followed (a function pointer in a table counts as a call), and whatever is reached
keeps -Os, whichever file it is in. Functions placed in a section (.ram_text, .c1_text) are left alone.
SIZE_FILES are relative to the repository (the app's, and fm1-base's in base/).

The DSP units (build.py O2_UNITS, built at -O2) name theirs in COLD: what runs once a note, a hit, a step
or a setting change (and the UI's text), never once a sample or a block. The same IR round trip.

Code built for size computes the same samples: the FPU is single precision only and no fused multiply-add
or reassociation is allowed at any level (-ffp-contract=off); only its speed differs.

The effects' and the master's per-sample code (RAM_DSP, of dsp/fx.c) runs from RAM: in section .c1_text.dsp, which
fm1-base's app.ld keeps in .ram_text's RAM (RAMTEXT, 24 KB; .c1_text*: the second core's RAM code, copied at boot,
inside the PC guard's window, and left out of the check that RAM code calls nothing in the flash: it runs only from the
audio interrupt and the second core's job, never while the flash is off). Out of the XIP cache's way, it leaves the
cache to the synth, the drums and the sequencer (on the FM-1, 2026-10-18: the master as fast either way, the rest
~10 % faster). A direct call does not reach from the XIP window to that RAM (R_PI32V2_LONG_JUMP_23M2: 4 MB) or back,
so the set is closed (RAM_INLINE: what both sides call is inlined into each) and engine.c calls in through a pointer
(DSP_FAR); the linker refuses any call that was missed.

    size_fns.py IN.ll OUT.ll [UNIT]        (XY_SIZE=0: copy unchanged; UNIT: an o2 unit, relative to app/)"""
import os
import re
import sys
from pathlib import Path

SRC = Path(__file__).resolve().parents[1]
SIZE_FILES = ["app/ui.c", "app/pages.c", "app/project.c", "app/lz.c", "app/samples.c", "app/gfx_loom.c", "app/app.c",
              "app/plat_loom.c", "base/device/panel.c", "base/device/main_fm1.c", "base/device/gfx.c",
              "base/device/storage.c", "base/bsp/chip/ota.c", "base/device/announce.c", "base/device/backup.c"]
COLD = {
    "dsp/drumline.c": ["start", "knob", "drumline_filter", "drumline_hit", "reso_set", "drumline_init"],
    "dsp/fx.c": ["fx_derive", "eq_coefs", "fx_filter", "master_set", "master_init", "comp_coefs",
                 "fx_set", "fx_init"],
    "dsp/sample.c": ["smp_user_scan", "fact_init"],
    "dsp/bench.c": [],
    "dsp/synth.c": ["tp_text_i", "tp_text_par", "tp_info_par", "tp_text", "char_picture", "voice_start",
                    "voice_take", "synth_note_on", "synth_note_off", "synth_duck_trigger", "synth_all_off",
                    "sound_default", "synth_init", "eknob_init"],
    "seq/pattern.c": [],
    "seq/seq.c": ["fire_notes", "key_in", "seq_init", "bar_end", "do_start", "emit_drum", "rec_key_off",
                  "rec_key_on", "restart", "track_reset", "seq_rec_pos", "deg_to_note", "seq_cue_pattern",
                  "arp_stop", "keys_all_off"],
}
# the two FX slots' and the master's per-sample code, closed under calls (~18 KB; the 24 KB RAMTEXT region holds
# .ram_text's ~3 KB and the second core's loop besides)
RAM_DSP = ["fx_process", "fx_process_lag", "do_spu", "spu_interp", "do_lush", "master_process", "color_block"]
RAM_INLINE = ["fx_states", "zero16"]
RAM_UNITS = ["dsp/fx.c"]
RAM_SECTION = ".c1_text.dsp"
ROOTS = re.compile(r"^(?:\w+_irq|fm1_fault_c|fm1_fatal_common|core2_job)$")
DEF = re.compile(r"^(?:static\s+|inline\s+|const\s+|unsigned\s+|signed\s+|struct\s+\w+\s+|__attribute__\s*\(\(.*?\)\)\s*)*"
                 r"(?:void|int|char|float|uint\d+_t|int\d+_t|size_t|\w+_t)\s*\**\s*([A-Za-z_]\w*)\s*\(", re.M)
IR_DEF = re.compile(r"^define [^\n]*?@\"?([\w.]+)\"?\(", re.M)


def definitions(text):
    """the names of the functions a C file defines (a signature at column 0 whose ')' is followed by '{')"""
    text = re.sub(r"/\*.*?\*/|//[^\n]*", " ", text, flags=re.S)
    names = []
    for m in DEF.finditer(text):
        i, depth = m.end() - 1, 0
        while i < len(text):
            depth += {"(": 1, ")": -1}.get(text[i], 0)
            i += 1
            if depth == 0:
                break
        rest = re.sub(r"^(__attribute__\s*\(\(.*?\)\)\s*)+", "", text[i:i + 200].lstrip())
        if rest.startswith("{"):
            names.append(m.group(1))
    return names


def ir_bodies(ir):
    """name -> (header line, body text) of every function defined in the IR; name -> initialiser of every global"""
    funcs, glob = {}, {}
    for m in re.finditer(r"^(define [^\n]*\{)\n(.*?)^\}", ir, re.M | re.S):
        n = IR_DEF.match(m.group(1))
        funcs[n.group(1)] = (m.group(1), m.group(2))
    for m in re.finditer(r"^@\"?([\w.]+)\"? = ([^\n]*)$", ir, re.M):
        glob[m.group(1)] = m.group(2)
    return funcs, glob


def reachable(funcs, glob):
    """everything the interrupt roots name, transitively (functions and globals)"""
    refs = {n: set(re.findall(r"@\"?([\w.]+)", b)) for n, (_, b) in funcs.items()}
    refs.update({n: set(re.findall(r"@\"?([\w.]+)", v)) for n, v in glob.items()})
    todo = [n for n in funcs if ROOTS.match(n)]
    seen = set(todo)
    while todo:
        for r in refs.get(todo.pop(), ()):
            if r not in seen:
                seen.add(r)
                todo.append(r)
    return seen


def ram_place(ir, names, inl, groups, made):
    """the named definitions into section .ram_dsp; the inl ones, and whatever else this file defines that
    the placed ones call, always inlined (a group of their own), so the placed code calls only itself"""
    extra = {}
    funcs = ir_bodies(ir)[0]
    todo = [n for n in names if n in funcs]
    inl = set(inl)
    while todo:
        for c in re.findall(r"call [^@\n]*@\"?([\w.]+)\"?\(", funcs[todo.pop()][1]):
            if c in funcs and c not in names and c not in inl and not c.startswith("llvm."):
                inl.add(c)
                todo.append(c)

    def place(m):
        line = m.group(0)
        n = IR_DEF.match(line).group(1)
        if n in inl:
            g = re.search(r" #(\d+) ", line).group(1)
            if g not in extra:
                extra[g] = 1 + max([int(x) for x in groups] + list(made.values()) + list(extra.values()))
            line = line.replace(f" #{g} ", f" #{extra[g]} ", 1)
        if n in names and " section " not in line:
            line = line[:-1].rstrip() + ' section "' + RAM_SECTION + '" {'
        return line
    ir = re.sub(r"^define [^\n]* #\d+ [^\n]*\{$", place, ir, flags=re.M)
    for g, new in extra.items():
        src = re.search(r"^attributes #%s = \{ (.*) \}$" % g, ir, re.M).group(1)
        src = " ".join(a for a in src.split() if a not in ("noinline", "minsize", "optnone", "inlinehint"))
        ir += f"\nattributes #{new} = {{ alwaysinline {src} }}\n"
    return ir


def main(src, dst, unit=None):
    ir = Path(src).read_text()
    if os.environ.get("XY_SIZE") == "0":
        Path(dst).write_text(ir)
        return 0
    funcs, glob = ir_bodies(ir)
    if unit:
        names, hot = set(COLD[unit]), set()
        missing = names - set(funcs)
        if missing:                   # renamed or gone: the list is out of date
            raise SystemExit(f"size_fns: {unit} defines no {sorted(missing)}")
    else:
        hot = reachable(funcs, glob)
        names = set()
        for f in SIZE_FILES:
            names.update(definitions((SRC / f).read_text()))
    pick = {n for n in names & set(funcs) if n not in hot and " section " not in funcs[n][0]}

    # each picked definition gets its own attribute group: the old one plus minsize
    groups = dict(re.findall(r"^attributes #(\d+) = \{ (.*) \}$", ir, re.M))
    nxt = max(map(int, groups)) + 1 if groups else 0
    made = {}

    def mark(m):
        nonlocal nxt
        n = IR_DEF.match(m.group(0)).group(1)
        if n not in pick:
            return m.group(0)
        g = m.group(1)
        if g not in made:
            made[g] = nxt
            nxt += 1
        return m.group(0)[:m.start(1) - m.start(0)] + str(made[g]) + m.group(0)[m.end(1) - m.start(0):]
    ir = re.sub(r"^define [^\n]* #(\d+) [^\n]*\{$", mark, ir, flags=re.M)
    if unit in RAM_UNITS:
        ir = ram_place(ir, RAM_DSP, RAM_INLINE, groups, made)
    for g, new in made.items():
        a = groups[g] if "optsize" in groups[g].split() else "optsize " + groups[g]
        if unit and "noinline" not in a.split():
            a = "noinline " + a           # (inlined, a cold function would take its hot caller's code with it)
        ir += f"\nattributes #{new} = {{ minsize {a} }}\n"
    Path(dst).write_text(ir)
    if unit:
        print(f"size: {unit}: {len(pick)} functions built for size")
    else:
        print(f"size: {len(pick)} main-loop functions built for size; {len(names & set(funcs) & hot)} in those "
              f"files reachable from an interrupt keep -Os")
    return 0


if __name__ == "__main__":
    sys.exit(main(*sys.argv[1:4]))
