# SPDX-License-Identifier: MPL-2.0
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
    fx1=None | (effect, k1, k2, mix) | (effect, k1, k2, k3, mix, x1 ..),   the track's slot 1 (FX's first page: the
                                                one effects list, EFFECTS): its knobs 0..127 (a two-knob effect's
                                                three-value form), its mix (dry 0 .. wet 127), its extras (the SEL
                                                list's; omitted: the effect's defaults, XDEF). The character's sounds
                                                (drive fold crush ring tilt tape sampler) as they were
    x=(x1, x2, x3, x4),                         the engine's second page (knit's osc 2 .., rayon's fall ..)
    osc2=None | (wave, shape, fine),            knit's Osc 2 page: wave same (osc 1's) / saw / square / pulse /
                                                triangle / sine, its shape 0..127 (saw .. square .. pulse; counts for
                                                saw, square and pulse only), fine 14..114 (64: none, +-50 cents)
    morph=None | (morph, amount, pan, sway),      tartan's Morph page (knit's Mod page's bytes): the voices' levels (Chordism's
                                                level morph), its amount, their places (its pan morph), the sway 0..127
    lowcut=0,                                   the low cut (Play 2): 0 off, 1..127 a 12 dB high-pass at 20 Hz .. 2 kHz
                                                before the filter (the Juno-106's HPF)
    fx2=None | (as fx1),                        the track's slot 2 (FX's second page), after slot 1: with it the
                                                sound's chorus, grit or movement comes with the preset (the delay
                                                and the reverb have no memory on a track yet: they pass dry)
    grain=None | (shape, reverse),              tulle's Grain page: its grains' shape 0 smooth .. 127 a pluck, the chance a
                                                grain plays backwards 0..127 (stored in knit's Mod page's bytes)
    analog=0,                                   knit's analog 0..127 (its Levels page): drift, each note a little off,
                                                softer edges, some saturation
    ops=None | (mode, ratio, depth, feedback),  rayon's Ops page: mode off / stack / layer, op 3's ratio (one of
                                                RO_RATIOS, as a number), depth 0..127, op 4's feedback 0..127
    mod=None | (sync, sweep, cross, sub),       knit's Mod page: sync True / False (osc 2 hard-synced to osc 1),
                                                sweep 0..48 (semitones the filter envelope lifts osc 2 at its top),
                                                cross 0..127 (osc 2 on osc 1's frequency), sub sine / square /
                                                low square / low pulse (the last two two octaves down)
    octave=None,                                -2 .. +2; None: by the role (a bass -2: on the FM-1's middle keys,
                                                F3 .. G5, it plays around C2; at -1 most had nothing under 100 Hz)
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
ENGINES = ["knit", "rayon", "jacquard", "bellows", "cane", "voile", "sample", "slice", "kit", "midi", "acid", "tulle", "lace",
           "tartan"]   # synth.h EG_*, in order
KNOBS = {"knit": ["wave", "detune", "voices", "width"], "rayon": ["bright", "ratio", "edge", "tine"],
         "jacquard": ["table", "scan", "bend", "ripple"], "bellows": ["stops", "pedal", "rotor", "rate"],
         "cane": ["model", "timbre", "color", "damp"], "voile": ["model", "timbre", "color", "breath"],
         "sample": ["sample", "start", "length", "mode"], "slice": ["sample", "slices", "pitch", "gate"],
         "acid": ["wave", "accent", "slide", "tune"], "tulle": ["sample", "position", "spray", "scan"],
         "lace": ["material", "bright", "damp", "position"],
         "tartan": ["chord", "spread", "rotate", "detune"]}
MODES = ["poly", "mono", "legato"]                    # VM_*
FILTERS = ["lp24", "ladder", "lp12", "hp12", "dynamic", "bp12"]   # FT_*
LFOS = ["off", "wave", "wobble", "drift", "pump"]    # LT_*
DESTS = ["synth", "envelope", "filter", "voice", "synth free", "envelope free", "filter free", "voice free"]   # LD_*
EFFECTS = ["off", "drive", "rat", "fold", "crush", "ring", "tilt", "tape", "sampler", "lofi", "tremolo", "phaser",
           "univibe", "chorus", "juno", "autowah", "delay", "rumble", "reverb"]   # dsp/efx.h EF_* (the catalog)
XDEF = {"lofi": (40, 0, 0, 0), "phaser": (80, 0, 0, 0), "chorus": (127, 0, 0, 0), "delay": (64, 127, 0, 0),
        "reverb": (12, 48, 0, 0)}   # the extras' defaults (dsp/efx.c EFX[].xdef; the rest 0): lush S
O2WAVES = ["same", "saw", "square", "pulse", "triangle", "sine"]   # O2W_*
NOOSC2 = ("same", 0, 64)
SUBS = ["sine", "square", "low square", "low pulse"]   # KS_*
NOMOD = (False, 0, 0, "sine")
OPS = ["off", "stack", "layer"]
# RO_*
RO_RATIOS = [0.5, 1, 1.5, 2, 2.5, 3, 3.5, 4, 5, 6, 7, 8, 9, 10, 11, 12, 14, 16, 1.41, 1.73, 2.24, 2.76, 3.46, 5.19]   # synth.c RO_RATIO
NOOPS = ("off", 1, 0, 0)
X_DEF = {"knit": (64, 0, 0, 0), "acid": (0, 1, 27, 0), "tulle": (80, 70, 0, 90), "lace": (0, 64, 64, 80),
         "tartan": (0, 0, 114, 0)}   # the engines' second pages as sound_default leaves them
NOLFO = ("off", 3, 64, "filter", 0, 1)
NOFX1 = ("off", 64, 64, 0, 127, 0, 0, 0, 0)   # (a slot as sound_default leaves it: effect, k1 k2 k3, mix, x1 .. x4)
NOFX2 = ("off", 0, 0, 0, 127, 0, 0, 0, 0)


def fx_slot(name, fx, none):
    """a slot's recipe as its nine values (effect, k1 k2 k3, mix, x1 .. x4)"""
    if fx is None:
        return none
    assert fx[0] in EFFECTS and len(fx) >= 4, (name, fx)
    v = (fx[1], fx[2], 0, fx[3]) if len(fx) == 4 else tuple(fx[1:5])
    x = tuple(fx[5:]) + XDEF.get(fx[0], (0, 0, 0, 0))[len(fx[5:]):]
    assert len(x) == 4 and all(isinstance(k, int) and 0 <= k <= 127 for k in v + x), (name, fx)
    return (fx[0],) + v + x

PRESETS = []


def P(name, role, engine, knobs, mode="poly", glide=0, bend=2, volume=100, amp=(0, 64, 127, 40),
      fenv=(0, 64, 0, 40), filter=("lp24", 127, 0, 64, 64), sends=(0, 0), lfo=None, fx1=None, x=None, osc2=None,
      mod=None, ops=None, analog=0, grain=None, morph=None, fx2=None, lowcut=0, octave=None, genres=()):
    assert role in ROLES, (name, role)
    assert engine in KNOBS, (name, engine)
    assert len(name) <= 11 and name == name.lower(), name
    assert sorted(knobs) == sorted(KNOBS[engine]), (name, knobs)
    lf = lfo or NOLFO
    assert lf[0] in LFOS and len(lf) == 6, (name, lf)
    if lf[0] in ("wave", "drift", "off"):
        assert lf[3] in DESTS, (name, lf)
    ch = fx_slot(name, fx1, NOFX1)
    ins = fx_slot(name, fx2, NOFX2)
    oc = octave if octave is not None else (-2 if role == "bass" else 0)
    assert -2 <= oc <= 2, (name, oc)
    xs = x if x is not None else X_DEF.get(engine, (0, 0, 0, 0))
    o2 = tuple(osc2 or NOOSC2)
    assert len(o2) == 3 and o2[0] in O2WAVES and 0 <= o2[1] <= 127 and 14 <= o2[2] <= 114, (name, o2)
    assert engine == "knit" or o2 == NOOSC2, (name, "the Osc 2 page is knit's")
    md = tuple(mod or NOMOD)
    assert len(md) == 4 and md[0] in (True, False) and 0 <= md[1] <= 48 and 0 <= md[2] <= 127 and md[3] in SUBS, (name, md)
    assert engine == "knit" or md == NOMOD, (name, "the Mod page is knit's")
    op = tuple(ops or NOOPS)
    assert len(op) == 4 and op[0] in OPS and op[1] in RO_RATIOS and 0 <= op[2] <= 127 and 0 <= op[3] <= 127, (name, op)
    assert engine == "rayon" or op == NOOPS, (name, "the Ops page is rayon's")
    assert 0 <= analog <= 127 and (engine == "knit" or analog == 0), (name, "analog is knit's")
    mo = tuple(morph or (0, 0, 0, 0))
    assert len(mo) == 4 and all(0 <= m <= 127 for m in mo) and (engine == "tartan" or mo == (0, 0, 0, 0)), (name, mo)
    assert isinstance(lowcut, int) and 0 <= lowcut <= 127, (name, lowcut)
    gr = tuple(grain or (0, 0))
    assert len(gr) == 2 and all(0 <= g <= 127 for g in gr) and (engine == "tulle" or gr == (0, 0)), (name, gr)
    vals = [*[knobs[k] for k in KNOBS[engine]], glide, bend, volume, *amp, *fenv, filter[1], filter[2], filter[3],
            filter[4], *sends, lf[1], lf[2], lf[4], lf[5], *xs]
    assert all(isinstance(v, int) and 0 <= v <= 127 for v in vals), (name, vals)
    assert mode in MODES and filter[0] in FILTERS, name
    PRESETS.append(dict(name=name, role=role, engine=engine, knobs=[knobs[k] for k in KNOBS[engine]], mode=mode,
                        glide=glide, bend=bend, volume=volume, amp=tuple(amp), fenv=tuple(fenv), filter=tuple(filter),
                        sends=tuple(sends), lfo=tuple(lf), fx1=ch, x=tuple(xs), osc2=o2, mod=md, ops=op, analog=analog, grain=gr, morph=mo, fx2=ins, lowcut=lowcut, octave=oc,
                        genres=tuple(genres)))


def smp(name):
    """a factory sample's index by its name (tools/gen_samples.py's order)"""
    import gen_samples
    return gen_samples.names().index(name)
