# MOD VIEW: modulation on the screen

A tester asked to see modulation: the LFO and the envelope moving the filter's curve, and a parameter that is being
modulated animating. Today the screen shows a lock's or a motion's value (engine_live: a dot on the knob's bar, the
pictures redrawn with par_now), but nothing of what the LFO and the envelopes do inside the voices.

## What it shows

On the selected track, while it sounds:

- **Knobs**: a knob the track's LFO (or the shared LFO, GLO's) is moving gets a moving tick on its bar at the value it
  has now, drawn unlike the lock dot (a short vertical tick in the accent colour, say), so a lock and modulation can
  show together.
- **Filter page**: the curve's cutoff follows the cutoff as it sounds: the knob (or lock/motion), plus the filter
  envelope on the newest note, plus the LFO when it moves the cutoff. The resonance's peak moves with it.
- **Envelope pages** (amp and filter): a dot travels along the drawn envelope at the newest voice's stage and level
  (attack, decay, sustain held, release).
- Nothing else changes: the engine pictures keep following par_now as now.

When nothing sounds on the track, or nothing modulates, the screen is exactly as today.

## Where the values come from

The engine publishes, for the selected track only (the UI tells it which: one track at a time), from its newest
sounding voice, once per audio block: the effective cutoff (0..127 knob units, float or Q8), the amp and filter
envelopes' stage and level, and the LFO's current offset with its destination (module + parameter, TP_LDEST /
TP_LX) mapped to the parameter index it moves; the shared LFO's likewise where it routes to the track. A few bytes in
the engine's shared state, written by the audio side, read by the UI; no locks needed (a torn read draws one odd
frame). No new per-voice state if the values already exist in the voice; the cost in the audio path is a handful of
stores per block.

## The switch and the guard

- **MOD VIEW** in Setup: on (the default) / off. An FM-1 setting, like the theme (kept with the device's settings,
  not per project; a free bit in settings rsv[3], or where Setup's other device settings live).
- **The guard**: when the audio load (the engine's measured load, as the Debug page shows it) is over 80 %, MOD VIEW
  stops updating (the plain screen as with it off) until the load is under 70 % (hysteresis).
- **Frame rate**: the main band redraws for modulation at most every other UI frame (~30 fps), and only when a drawn
  value changed (begin_main's hash includes the modulated values, quantised to what the screen can show).

## Cost

Flash: under 1 KB (the app has ~18 KB left of its slot). RAM: tens of bytes. Audio: a few stores per block. The main
loop: redraws of the main band while modulation moves (SPI time), bounded by the frame rate above and off under
load.

## Around it

Screen reader: nothing new is said (the modulated value is not announced). Manual: a short paragraph on MOD VIEW
(where it is, what it shows, that it pauses when LOOM-8 is busy). Setup's words in words.tsv. Host tests: with an
LFO on the cutoff and a note held, the Filter page's drawn cutoff changes over time with MOD VIEW on and not with it
off; the Setup row; the guard (the host can feed the load: `load N`). Screenshots for the manual if the manual shows
Setup.

## Not in this

Modulation shown on other tracks than the selected one, on the engine pictures, or announced by the screen reader;
the MIDI or kit tracks' modulation beyond what their pages already draw.
