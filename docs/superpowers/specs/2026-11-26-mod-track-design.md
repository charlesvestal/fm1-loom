# The MOD track: automation lanes on an aux track (2026-11-26)

Part D of the techno work (`docs/superpowers/2026-11-25-techno-handoff.md`): changes longer than a pattern — a filter
opening over 32 bars, a send pushed for a bar — living in the project, so a loop can evolve without hands on the
knobs. Designed with the user, 2026-11-26.

Prior art: KrisGelero's PR #5 "LOOM-8: Song Automation" (charlesvestal/fm1-loom#5, closed unmerged 2026-11-09:
four macros, a recorded curve per song slot). From it: the **hand-back rule** (where a lane has no value the
parameter belongs to its knob and the pattern's locks) and **learn by turning** the knob you mean. Credit him in the
commit and the manual's credits.

## Decisions (the user's)

- A **MOD aux track** beside COND and THROW: **automation lanes only**, targets chosen (no modes: a morph is two
  values gliding, a jump one value, a recording many values).
- **Steps, not bars**: a MOD pattern is a pattern like any track's — length 1-64 steps and **SCALE** (a step a
  sixteenth .. a bar: 64 steps at scale 16 = 64 bars).
- **Glide or step, per lane.**
- **"A"**: before a lane's first value the knob stays where it is; the glide to the first value starts there.

## The model

- **8 MOD patterns**; each scene picks one **or none** (`scenedef`'s aux columns gain MOD: "-", 1..8; a new scene's
  is "-", so modulation is only where it is put — no pattern spent as a blank one; the user, 2026-11-26). With "-"
  the knobs stay where the last modulation left them. The song follows the scenes; its black key mutes it, live (black
  key 11, after COND 9 and THROW 10: check the free keys).
- **Up to 8 lanes a MOD pattern**. A lane: a **target** (any track's parameter, a kit key's, FX I / FX II's, the
  master's values incl. Master FX), **glide or step**, and a value a step. Lanes come from the **shared lane pool**
  (`pats.lane`, NLANE 96, the locks' and motion's): a lane is what a lock lane is, with a target outside its own
  track. No new big storage.
- Values are **absolute** (a lane sets the knob's real value, as a lock).

## Playing

- At each step with a value: **step** lanes set it; **glide** lanes ramp from the value now to the next valued step's,
  over the steps between (a step with no value continues the glide; past the last value, it holds).
- Before the first value: nothing applies (the knob keeps its own).
- **Who wins** (the user, 2026-11-26: "mod"): the hand (until the lane's next valued step) > the MOD lane (from its
  first valued step, through the hold after its last) > the pattern's own locks and motion on that parameter > the
  knob's value. The pattern's locks are not changed, only overridden while a MOD lane owns the parameter: muting
  the MOD track or a scene with "-" lets them play again.
- **Turning the target by hand** takes over until the lane's next valued step (as a locked step).
- **A scene change**: the new scene's MOD pattern starts at its step 1; what it doesn't target stays where it was.
- **Mute**: the MOD track's knobs freeze where they are.
- Applied through the lock sink (`epar`), every step (ramps: per block, smooth — the slew work, 4833524, keeps the
  effects' knobs click-free).

## Editing (the MOD track's page)

- **SEL list**: the pattern's lanes (target, glide/step, remove), **add a lane**: pick the target from the parameter
  lists (track, page, knob: as the LFO's and locks'), or **learn** (turn the knob you mean, anywhere).
- **ALGO** picks the lane.
- Keys are steps (OCT for 17-64), lit where the selected lane has a value. **Hold a step and turn the lane's knob**
  to set its value (the lock gesture); the knob is the target's own, shown with its name and units.
- The page draws the selected lane's curve over the pattern's steps, with the playhead.
- **REC held while playing** writes the target's moves into the selected lane's steps.
- Pattern length and SCALE as on any track (the usual places).

## Storage

- Project format 31 (or the next): the MOD aux track's 8 patterns (length, scale, the lanes' indexes), its scene
  column, lanes with a target outside their own track (`lane_t` gains what it needs: the target's track and
  parameter are already in it — check), glide/step a lane bit. Older projects: an empty MOD track.
- RAM: patterns for one more aux track (the steps' own data are lane values only: a MOD pattern has no notes —
  keep its pattern record small, not a full `tpat_t`); report the cost. SRAM is ~3 KB free: find room first if
  needed.
- Flash: ~3 KB free now; a flash pass is running (target 10 KB).

## Testing

- Host: a glide lane ramps the filter cutoff between two valued steps at scale 16 (the value per bar as computed);
  step lanes jump; hand turn takes over until the next value; scene change restarts; mute freezes; learn; REC
  writes; save/load round trip; an older project loads with an empty MOD track.
- The techno demo: its hand-written knob moves (`knob 2 cut ...` every 4 bars) replaced by a MOD pattern, rendering
  the same shape.
- The FM-1: CPU with lanes gliding (asked first).

## Open

- How many lanes in all (NLANE 96 shared with locks and motion) — enough, or a separate small pool?
- The page's picture of a lane at 64 steps on a 240-px screen.

## Next (the user, 2026-11-26, after trying the fixes)

- **The SEL list's "this lane" section** holds the selected lane's own settings: **TARGET** (retarget: TRACK / PAGE
  / KNOB or LEARN; the lane keeps its steps and values, clamped to the new target's range), **GLIDE / STEP**,
  **REMOVE**, **CLEAR**. The "lanes" section only lists the lanes (SEL picks one; no glide/step there).
- **Reverting** (2eaa430): when MOD lets go ("-", mute, a scene without the target, a removed lane) the parameter
  returns to its knob's value — not frozen (this replaces "freezes" above).
- **CPU** (profiled on the FM-1 with a MOD glide on the drums filter: mix stage 6 % average, 17 % worst; master
  12 %): filter the kits' summed pair and sends once (not each kit), coefficients once a block and cheap while
  moving, the master's mix-filter path back in RAM.
- The filter knobs' label shows the side at a glance (LP to the left, HP to the right).
- **KNOB 4 on the Lanes page edits the lane, never the live parameter** (the user: "we're just setting a value, not
  controlling it"): held steps -> their values; nothing held -> the step last tapped (outlined; the header "step 9:
  5.8 kHz"); none tapped -> nothing ("hold a step"). REC held while playing still writes the moves into the lane. The
  hand takeover stays on the parameter's own page.
- **Takeover** (the user): on the parameter's own page, as it is (until the lane's next value); from the MOD page,
  never (KNOB 4 there edits the lane only).
- **Lane shapes** replace glide / step: step, linear (was glide), ease in, ease out, smooth (S); PRESETS on Lanes cycles
  them, the curve drawn in its shape, SEL's "this lane" shows SHAPE; a new lane starts smooth; older lanes keep theirs
  (glide = linear). Per block, as the glide now.
