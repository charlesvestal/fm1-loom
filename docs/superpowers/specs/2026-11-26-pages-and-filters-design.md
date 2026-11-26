# Pages, filters and reset (2026-11-26)

The user's decisions, to build after the MOD track (they touch the same engine and page code):

1. **Three filters, one resonance** (master filter spec, addenda 1 and 2): drums (each kit, before its sends),
   melodic (the synth tracks' sum and their sends, before the kits' and the returns), mix (after the returns), KNOB 4
   the resonance for all three. A kit's sends also go through its kit filter. Inserts stay before a kit's sends.
2. **Reset**: hold **HOME** and turn a knob: that knob back to its default (the filters: off; pan: centre; an
   effect's knob: the effect's default; a track's parameter: tp_info's default), on any page; the screen and the
   screen reader say it ("melodic: off"). Arrange keeps HOME + KNOB 1 as the tempo's tenths. Not on held steps
   (SEL + knob clears locks there).
3. **Centre catch** on the filter knobs (the three DJ filters; pan and pitch where centred): a sweep through the
   middle holds at off for a couple of detents (a knob's own turning; MIDI absolute values land exactly).
4. **FX I and FX II move to GLO**, after Mix: GLO = Mix, FX I, FX II, Master EQ, Master FX, Master filter, Master
   out, LFO, Setup, Debug. FX = the track's own: Insert 1, Insert 2, Sends, Throws. On Sends, the SEL list jumps to
   FX I / FX II. The keys play the track you were on (as on every GLO page).
5. **GLO remembers its page**: pressing GLO opens the GLO page last visited (not the first), "since it's different
   from changing a track" (the user). FX keeps opening Insert 1.

Manual, screen reader words, help cards and the screenshots follow; scenarios that walk GLO or FX with SELECT counts
are updated.

## Performance ideas (to decide; not in this build)

The user, on the build with the drums filter before the sends: "the drums being down but the returns still having
the volume there … fun to play with, like turning down just some of the elements". Proceed with sends after the
filters (above), and keep the move as a performance element:
- **A "wet only" throw** (Throws page, drums and melodic octaves): the group's dry cut while its sends keep feeding
  FX I / II — the reverb and echoes alone while held. Gains only. (Preferred.)
- Or a setting on Master filter's SEL list: the filters before / after the sends.
Gather the other performance elements with it before deciding.
- **Wet only, throw or mixer** (the user asked): a throw is momentary and per group; a mixer state is latching and
  per track — "turning down just some of the elements" points to the mixer: a track's mute gets a third state, wet
  only (a post-send mute: the dry off, the sends feeding FX I / II).
- **Mixer mutes** (the user): a global mute layer on the Mix page, beside the scenes' (composition) mutes: per
  track and per kit sound (the first four kits, as scenes have), latching, untouched by scene changes, saved with
  the project not the scenes. Silent if the scene OR the mixer mutes it. The same gestures as Arrange's (black keys;
  a kit's black key held + white keys for its sounds; SEL held: wet only). Shown apart from a scene's mute (a
  crossed speaker, not a dim row). The mixer as the performance surface: these mutes, wet only, the three filters.
- **Note mutes vs audio** (the user asked): every mute today (Arrange, Mix, the scenes, the shared s->mute / kmute)
  is a note mute: the sequencer stops the notes (the pump still hears them), what sounds rings out. Mixer mutes would
  be note mutes too (a second layer). **Wet only cannot be one**: the notes must keep playing into the sends, so it
  is an audio gain of 0 on the track's dry output, the sends untouched (the voices keep running: the CPU as playing).

## Also in the next build: effects level-matched at their defaults

The user, on the FM-1: "RAT is so loud as you scroll through inserts". Every catalog effect at its default knobs
measured against the dry (noise and a synth note, host): defaults (or the effect's own make-up) brought within
about +-1.5 dB of dry, RAT first (its level knob then trims around unity); level-changing effects (tremolo) by their
average. A host test keeps it so.

## Also: the arp's 1/4D (the user)

ARP speeds (seq.c ARP_NUM / ARP_DEN, synth.c ARPSPD_N; slowest first) gain **1/4D** (six sixteenths) before 1/4. A
speed is stored as its index (TP_P1): the next project format moves every arp speed and its locks / motion up one
(as format 21's old_arp did), presets format too if presets store it; a migrate.xy expectation.

## Decided (the user, 2026-11-26): mixer mutes are audio; wet only — in this release

"It's a mixer, it should mute audio, not trim notes. That's what the pattern mutes are for." Arrange and the scenes
keep their note mutes (composition); the **Mix page** gets audio mutes (performance):
- Each track's channel: **on**, **mute** (its dry and its sends off; what is in FX I / II rings out), **wet only**
  (its dry off, its sends on: only its reverb and echoes heard).
- Mix's black keys: a tap mutes / unmutes the track's audio; **SEL held + tap**: wet only. A kit's black key held +
  white keys: its sounds' audio, one by one (the same gesture as Arrange's). Hold a black key: **audio solo** (the
  others muted at the mixer while held; the double-tap latch as now).
- Global: scene changes never touch it; saved with the project, not the scenes. A ~5 ms fade (no clicks).
- Shown on Mix's rows apart from a scene mute (a crossed speaker; "wet"); the screen reader says it.
- Changes from today: Mix's black keys toggled the scene's (note) mutes; they become the mixer's. Pattern / scene
  mutes on Arrange only. Manual: the mute rows of How do I, Mix, Arrange.

Built (branch room, 2026-11-26), with these calls made in the building:
- Stored after the tracks in the settings object (settings_t mixm, mixk; format 34, older projects all on). Every
  track's kit keys (not only the first four kits', as the scenes' are).
- A tap on a wet-only track turns it on (as on a muted one); SEL + tap on a muted one makes it wet only.
- A solo hears a soloed track even if it was muted at the mixer (as the old solo unmuted it); wet only stays wet.
- CC 9 (and 16-26) stay the note mutes (Arrange's, the scene's): they are what CC 9 out reports when a scene changes
  them. The mixer's mutes have no CC.
- FX I and FX II's black keys on Mix pick them only (no mute); COND, THROW and MOD have no key on Mix.
- The fades: a synth track's mute ~2 ms (its voices' gains), wet only ~5 ms (its bus's dry), a kit key's ~5 ms; a
  kit's mute and wet only ride its level's glide (~10 ms one-pole), as the punch's mute does.
- The pump still hears a muted kit's kicks (its notes play).

## Next: CPU (the user, 2026-11-26)

- **Work well at 360 MHz first**: the bit-exact optimisation pass (cached invariants, exact silence skips, hot loops in
  RAM) — no sound or behaviour change (dsp_hash, demos and the user's project byte-identical).
- **Then a Setup option "CPU 360 / 396 (restart)"**, off by default: fm1-base's FM1B_CPU_396 chosen at boot from
  LOOM-8's saved setting instead of the build (a small fm1-base change: the boot reads the app's choice; safe mode's
  fallback to 360 after two hung starts kept; the stock clock back before resets and updates). ~10 % more CPU.
  The page and the manual say: JieLi's top speed, not for mass production, a little more battery and heat.
