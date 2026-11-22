# knit: an Osc 2 page

Testers asked for more from knit's second oscillator (Bloomy, a LOOM-8 fork, rebuilt knit with a filter and envelopes
per oscillator: measured at 1.44x to 1.88x a knit voice's cost and +2 KB RAM, so not that). This gives osc 2 its own
wave instead, at about no cost.

## The page

Knit's second page stays as it is (osc 2 interval, level, sub, noise). A third page, knit only, after it: **Osc 2**.

| knob | values |
|---|---|
| wave | same (the default: osc 1's wave, as now), saw, square, pulse, triangle, sine |
| shape | osc 2's own sweep, saw .. square .. pulse (as osc 1's wave knob): used when wave is saw, square or pulse, and set to where that wave sits when wave is turned to it; "-" for same, triangle, sine |
| fine | osc 2 fine (TP_O2FINE, +-50 cents), moved here |
| - | unused |

- The HOME shortcut for osc 2 fine (HOME held on the Sub page's osc 2 knob) goes: one place for each control.
- Wave "same" keeps every existing sound and preset exactly as it is (osc 2 follows osc 1's wave and its LFO PWM).
- Osc 2 with its own wave: the morph waves as osc 1 makes them (the same code, its own shape value); triangle and
  sine cheap (no band-limiting needed for sine; triangle as knit's other waves handle aliasing).
- The page shows up only on knit tracks, as engine pages do; it has a picture like the other engine pages (osc 2's
  wave drawn), or the Sub page's picture if that's simpler and reads well.

## Saved

Two new parameters after the last (TP_RUP), so earlier formats' indices stay: a format bump, old projects load with
wave = same. Locks and motion work on them as on any parameter. Presets: user presets keep them; factory presets
unchanged (same).

## Around it

Manual (knit's pages), the screen reader's words for the new names, help if the Sub page's help mentions HOME, host
tests (the page, the values, old projects load as same, the sound changes with wave and stays bit-identical with
same: dsp_hash or synth_test), the layout audit.

## Not in this

Per-oscillator filters or envelopes, osc 2 level on this page (it stays on Sub).
