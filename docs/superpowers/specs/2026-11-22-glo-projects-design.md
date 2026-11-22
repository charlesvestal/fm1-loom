# Projects on GLO

A tester's suggestion (after gfcalvi's Bloomy, which lists its projects on GLO): the projects in one list, on GLO,
instead of HOME's LOAD PROJECT pick. Three slots, as now; no names.

## GLO's SEL list

A "projects" section first, above GLO's knob pages (on every GLO page, Setup included):

| row | value | SEL |
|---|---|---|
| Proj 1 | 122 BPM · 4 scenes | loads it; "LOSE UNSAVED CHANGES?" first when the open project has unsaved changes |
| Proj 2 (open) | 98 BPM | nothing |
| Proj 3 | empty | "NEW PROJECT IN PROJ 3?", then a new project (as NEW PROJECT: defaults and a band of its own) whose slot is 3 |
| SAVE TO | Proj 3 (knob picks) | as now |
| NEW PROJECT | | as now (its slot the first free one, none free: SAVE TO picks) |

- The value: the slot's tempo, then its scene count when the slot has one ("4 scenes", "1 scene"); "empty" for a
  slot with nothing saved; a slot of an older format that won't load says "old format".
- A load or a new project stops the transport, as LOAD PROJECT does now, and closes the list.

## HOME's SEL list

Loses its "project" section (LOAD PROJECT, SAVE TO, NEW PROJECT). Keeps its song or scenes actions, RANDOMIZE PRESETS
and PANIC.

## The summary

- Tempo: `settings_t.tempo_x10`, in the settings object's raw header (`SET_HDR`): read with the slot's first bytes,
  no unpacking.
- Scenes: the patterns are an LZ stream, too big to unpack for a list. A save writes the count of used scenes into
  `proj.rsv[8]` (unused today); 0 shows no count. Projects saved before this show their tempo only, until they are
  saved again. No format change.
- Read once per slot when the list is built, cached as `project_exists` caches (slot_known); a save, a load, a new
  project or a restore forget the cache.

## Around it

- The SAVE button's help ("Press SEL for projects...") points at GLO.
- Screen reader: the rows' words in `words.tsv` (the audit clean).
- Manual: the projects section moves from HOME to GLO.
- Host UI tests: the list's rows (open, empty, a summary with and without a scene count), loading with and without
  unsaved changes, a new project in an empty slot, SAVE TO, HOME's list without the section.

## Not in this

Names, more slots, per-project backup.
