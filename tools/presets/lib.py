# SPDX-License-Identifier: GPL-3.0-only
"""LOOM-8's factory presets as recipes: each file in tools/presets/ calls P() once a preset; tools/gen_presets.py
turns them into app/build/gen/presets.h (the PRESETS table synth.c includes). A preset is a track's sound
(synth.h par[0..TP_PLAYER)): every value 0..127 as the FM-1's knob holds it, the switches by name.

  P(name, role, engine, knobs={...},          the engine's four knobs by their names (synth.c EG_KNOB), all four
    mode="poly", glide=0, bend=2, volume=100, the Play page (mode: poly / mono / legato)
    amp=(a, d, s, r), fenv=(a, d, s, r),       the amp and filter envelopes
    filter=(type, cut, res, env, key),         type: lp24 ladder lp12 hp12 bp12
    sends=(fx1, fx2),
    lfo=None | (type, speed, amount, dest, x, x2)   type: wave wobble drift pump; dest a name for wave and drift
                                                ("synth", "envelope", "filter", "voice", and "... free"), a number
                                                for wobble (its volume amount) and pump (its hold)
    char=None | (type, amount, tone, mix),     type: drive fold crush ring tilt tape sampler
    x=(x1, x2, x3, x4),                         the engine's second page (brocade's sub, orbit's fall ...)
    octave=None,                                -2 .. +2; None: by the role (a bass -1, else 0)
    genres=())                                  what it is for (lofi, synthpop, dnb, house, garage ...): the
                                                checks and the docs read it, the firmware does not
The scales (0..127): LFO speeds 0..7 synced (3 = 1/4), above free: 20 is about 0.16 Hz, 40 0.45 Hz, 60 1.2 Hz, 80
3 Hz, 92 5.6 Hz. Times: 32 is about 10 ms, 64 100 ms, 80 330 ms, 96 1.1 s, 110 3 s. Cutoff: 59 is 800 Hz, 80
2.2 kHz, 100 5.6 kHz (50 Hz .. 20 kHz). A vibrato: lfo=("wobble", 92, 74, 64, fade, 0), about 16 cents at 5.6 Hz.
Omitted, a value is the engine's new sound's (as sound_default leaves it) where that is fixed, else the default
above. Names are what the FM-1 shows. The table goes in the engines' order, each engine's presets in the order the
files list them (gen_presets.py FILES); an engine's first preset is its sound in a new project."""

ROLES = ["bass", "lead", "pad", "keys", "pluck", "bell", "stab", "strings", "brass", "voice", "chord", "fx", "drum",
         "loop"]                                      # synth.h RL_*, in order
ENGINES = ["weft", "stack", "jacquard", "rayon", "tines", "bellows", "sync", "swarm", "kit", "sample", "slice",
           "brocade", "cane", "voile", "gingham", "knit"]   # synth.h EG_*, in order
KNOBS = {"weft": ["bend", "wave", "sweep", "spread"], "stack": ["wave", "ratio", "detune", "spread"],
         "jacquard": ["table", "scan", "bend", "ripple"], "rayon": ["bright", "ratio", "edge", "tine"],
         "tines": ["bright", "grit", "bite", "bell"], "bellows": ["stops", "pedal", "rotor", "rate"],
         "sync": ["reach", "sub", "hiss", "thin"], "swarm": ["swarm", "ring", "tone", "spread"],
         "sample": ["sample", "start", "length", "mode"], "slice": ["sample", "slices", "pitch", "gate"],
         "brocade": ["saws", "detune", "mix", "spread"], "cane": ["model", "timbre", "color", "damp"],
         "voile": ["model", "timbre", "color", "breath"], "gingham": ["model", "timbre", "color", "arp"],
         "knit": ["wave", "detune", "voices", "width"]}
MODES = ["poly", "mono", "legato"]                    # VM_*
FILTERS = ["lp24", "ladder", "lp12", "hp12", "dynamic", "bp12"]   # FT_*
LFOS = ["off", "wave", "wobble", "drift", "pump"]    # LT_*
DESTS = ["synth", "envelope", "filter", "voice", "synth free", "envelope free", "filter free", "voice free"]   # LD_*
CHARS = ["off", "drive", "fold", "crush", "ring", "tilt", "tape", "sampler"]   # CT_*
BR_SUB_DEF = 52                                      # synth.h: brocade's sub offset by default
X_DEF = {"brocade": (0, BR_SUB_DEF, 0, 0), "knit": (64, 0, 0, 0)}   # the engines' second pages as sound_default leaves them
NOLFO = ("off", 3, 64, "filter", 0, 1)
NOCHAR = ("off", 64, 64, 127)

PRESETS = []


def P(name, role, engine, knobs, mode="poly", glide=0, bend=2, volume=100, amp=(0, 64, 127, 40),
      fenv=(0, 64, 0, 40), filter=("lp24", 127, 0, 64, 64), sends=(0, 0), lfo=None, char=None, x=None, octave=None,
      genres=()):
    assert role in ROLES, (name, role)
    assert engine in KNOBS, (name, engine)
    assert len(name) <= 12 and name == name.lower(), name
    assert sorted(knobs) == sorted(KNOBS[engine]), (name, knobs)
    lf = lfo or NOLFO
    assert lf[0] in LFOS and len(lf) == 6, (name, lf)
    if lf[0] in ("wave", "drift", "off"):
        assert lf[3] in DESTS, (name, lf)
    ch = char or NOCHAR
    assert ch[0] in CHARS and len(ch) == 4, (name, ch)
    oc = octave if octave is not None else (-1 if role == "bass" else 0)
    assert -2 <= oc <= 2, (name, oc)
    xs = x if x is not None else X_DEF.get(engine, (0, 0, 0, 0))
    vals = [*[knobs[k] for k in KNOBS[engine]], glide, bend, volume, *amp, *fenv, filter[1], filter[2], filter[3],
            filter[4], *sends, lf[1], lf[2], lf[4], lf[5], ch[1], ch[2], ch[3], *xs]
    assert all(isinstance(v, int) and 0 <= v <= 127 for v in vals), (name, vals)
    assert mode in MODES and filter[0] in FILTERS, name
    PRESETS.append(dict(name=name, role=role, engine=engine, knobs=[knobs[k] for k in KNOBS[engine]], mode=mode,
                        glide=glide, bend=bend, volume=volume, amp=tuple(amp), fenv=tuple(fenv), filter=tuple(filter),
                        sends=tuple(sends), lfo=tuple(lf), char=tuple(ch), x=tuple(xs), octave=oc,
                        genres=tuple(genres)))
