# Projects on GLO Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers-extended-cc:subagent-driven-development (recommended) or superpowers-extended-cc:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The three project slots listed first on GLO's SEL list, each with its tempo and scene count; SEL loads a
slot or starts a new project in an empty one; HOME's list loses its project rows.

**Architecture:** project.c's slot cache (`project_exists`, `slot_known`) reads a little more of each slot's raw
settings header (tempo, and a scene count every save now writes into `proj.rsv[8]`). pages.c builds a "projects"
section at the top of GLO's list; ui.c runs its rows. The host simulator gets three test hooks (`expect slot`,
`expect projslot`, `rowval` / `norow`). Spec: `docs/superpowers/specs/2026-11-22-glo-projects-design.md`.

**Tech Stack:** C (gnu99, single-precision only), fm1-base's host simulator scenarios (`tests/scenarios/*.xy`).

**User decisions (already made):**
- "1": move the projects to GLO's SEL list (not both places, not kept on HOME).
- "1": each row shows the slot and a summary (tempo, scene count); no names.
- Three slots stay; no format change.

---

Build and test (from `fm1-xy/`): `./build.sh --host` builds `app/build/host/loom_host`; one scenario:
`app/build/host/loom_host tests/scenarios/NAME.xy app/build/scenarios/NAME` (exit status non-zero and `FAIL` lines on
a failure); everything: `sh tests/run_tests.sh`. Python for the build: `PYTHON=/opt/homebrew/bin/python3.14`.

Style: match the files' own comments (short, lower-case, what and why) and names. No `double` in app code (host.c
may use it). Commit messages in the repo's style: `LOOM-8: ...`, no Co-Authored-By or session trailers.

### Task 1: Slot summaries (tempo, scene count)

**Goal:** `project_slot_info(slot, &tempo_x10, &scenes)` reads a slot's tempo and scene count through the existing
slot cache; every save writes the scene count into `proj.rsv[8]`.

**Files:**
- Modify: `app/app.h` (settings_t rsv comment; `RSV_SCENES`; prototype next to `project_exists`, line ~221)
- Modify: `app/project.c` (`count_next` ~line 917, `project_exists` ~line 962)
- Modify: `app/host.c` (`app_expect` ~line 420: `projslot`; `expect_more` ~line 564: `slot`)
- Create: `tests/scenarios/glo_projects.xy`

**Acceptance Criteria:**
- [ ] `expect slot 1 1220 1` passes after `bpm 122` + `projsave 1` on a fresh project (one scene)
- [ ] a second scene made on Arrange, `bpm 98.5`, `projsave 2`: `expect slot 2 985 2` passes
- [ ] `tests/scenarios/slot_reads.xy` still passes (the summary costs no extra reads of the flash)

**Verify:** `./build.sh --host && app/build/host/loom_host tests/scenarios/glo_projects.xy app/build/scenarios/glo_projects && app/build/host/loom_host tests/scenarios/slot_reads.xy app/build/scenarios/slot_reads` → no `FAIL` lines, exit 0

**Steps:**

- [ ] **Step 1: Write the failing scenario** `tests/scenarios/glo_projects.xy`:

```
# GLO's projects (docs/superpowers/specs/2026-11-22-glo-projects-design.md): each slot's tempo and scene count, read
# from its saved settings (the scene count written by every save into rsv[8])
wait 300
bpm 122
expect used 1
projsave 1
expect slot 1 1220 1
# a second scene (white key 2 on Arrange copies the one playing), another tempo, saved into slot 2
tap HOME
tapkey w1
wait 50
expect used 3
bpm 98.5
projsave 2
expect slot 2 985 2
expect projslot 2
```

- [ ] **Step 2: Run it, see it fail** — `./build.sh --host && app/build/host/loom_host tests/scenarios/glo_projects.xy app/build/scenarios/glo_projects`
  Expected: `FAIL` on `expect slot` (unknown) and `expect projslot`. If `expect used 1` fails, a fresh project has
  another scene count: set the scenario's numbers to what `expect used` reports and carry them through.

- [ ] **Step 3: app.h.** In settings_t's `rsv[10]` comment, after "[4..7] the save count (...)", add
  `[8] the scenes used, written by every save (GLO's list shows it without unpacking the patterns)`. After the
  struct:

```c
#define RSV_SCENES 8                    /* proj.rsv[RSV_SCENES]: the scenes used when saved (0: saved before 0.15) */
```

  Next to `int project_exists(int slot);`:

```c
int project_slot_info(int slot, uint16_t *tempo_x10, uint8_t *scenes);   /* a slot's tempo and scenes (as saved):
                                                                           * 1, or 0 for an empty slot */
```

- [ ] **Step 4: project.c, the count on every save.** `count_next()` runs on every save and every touch; add the
  scene count there:

```c
static void count_next(void)
{
    uint32_t c = ++count_max;
    uint8_t ns = 0;
    for (uint32_t i = 0; i < 4u; i++)
        proj.rsv[4 + i] = (uint8_t)(c >> (8u * i));
    for (int s = 0; s < NSCENE; s++)              /* (GLO's list reads it from the header: RSV_SCENES) */
        ns = (uint8_t)(ns + (pats.scene[s].used != 0));
    proj.rsv[RSV_SCENES] = ns;
}
```

- [ ] **Step 5: project.c, the summary in the slot cache.** Read the header up to `master` (raw, before the packed
  tracks) instead of 8 bytes, and keep tempo and scenes beside `slot_have`:

```c
#define SLOT_HEAD ((uint32_t)offsetof(settings_t, master))   /* the raw header: magic .. rsv */
static uint16_t slot_tempo[NPROJ];
static uint8_t slot_scenes[NPROJ];
int project_exists(int slot)
{
    int n;
    if (slot < 0 || slot >= NPROJ)
        return 0;
    if (slot_known >> slot & 1u)
        return slot_have >> slot & 1u;
#ifdef PLAT_HOST
    slot_reads++;
#endif
    n = plat_store_load(OBJ_PROJ(slot, 0), io_buf, SLOT_HEAD);
    n = n >= (int)SLOT_HEAD && get32(io_buf) == PROJ_MAGIC && FORMAT_OK(get32(io_buf + 4));
    if (n) {                                      /* (the tempo and scenes for GLO's list, read with it) */
        const uint8_t *t = io_buf + offsetof(settings_t, tempo_x10);
        slot_tempo[slot] = (uint16_t)(t[0] | t[1] << 8);
        slot_scenes[slot] = io_buf[offsetof(settings_t, rsv) + RSV_SCENES];
    }
    slot_known |= (uint8_t)(1u << slot);
    slot_have = (uint8_t)(n ? slot_have | 1u << slot : slot_have & ~(1u << slot));
    return n;
}
int project_slot_info(int slot, uint16_t *tempo_x10, uint8_t *scenes)
{
    if (!project_exists(slot))
        return 0;
    *tempo_x10 = slot_tempo[slot];
    *scenes = slot_scenes[slot];
    return 1;
}
```

  (`save_buf` and `app_backup_restored` already clear `slot_known`, so a save, a load's touch and a restore refresh
  it.)

- [ ] **Step 6: host.c.** In `app_expect`, beside `slot_reads`:

```c
    else if (!strcmp(what, "projslot"))             /* the open project's slot, from 1 (0: none yet) */
        *got = proj_slot < NPROJ ? proj_slot + 1 : 0;
```

  In `expect_more`, beside `step`:

```c
    } else if (!strcmp(what, "slot")) {             /* expect slot S TEMPO_X10 SCENES: slot S's summary (GLO's list) */
        uint16_t tx = 0;
        uint8_t ns = 0;
        int have = project_slot_info(atoi(a) - 1, &tx, &ns), bad = !have || tx != atoi(b) || ns != atoi(c);
        printf("%s slot %s: %s, tempo_x10 %u, scenes %u\n", bad ? "FAIL" : "  ok", a, have ? "saved" : "empty",
               (unsigned)tx, (unsigned)ns);
        host_fails += bad;
```

- [ ] **Step 7: Run both scenarios** (Verify line). Expected: exit 0, no `FAIL`.

- [ ] **Step 8: Commit**

```bash
git add app/app.h app/project.c app/host.c tests/scenarios/glo_projects.xy
git commit -m "LOOM-8: each slot's tempo and scene count, read with the slot (a save writes the count into rsv[8])"
```

### Task 2: The projects section on GLO's list

**Goal:** GLO's SEL list starts with "projects": PROJ 1-3 (the open one "PROJ n (OPEN)") with their summaries, SAVE
TO and NEW PROJECT; SEL on a saved slot loads it (asked first when there are unsaved changes), on an empty slot asks
"NEW PROJECT IN PROJ n?" and starts one there, on the open one does nothing. HOME's list loses LOAD PROJECT, SAVE TO
and NEW PROJECT.

**Files:**
- Modify: `app/pages.c` (action enum + ACTION_NAME ~line 280-296; `list_home` ~line 2852; list build `b == B_GLO`
  ~line 3090; new `list_projects` after `proj_slot_text` ~line 2682)
- Modify: `app/ui.c` (`do_new_proj` ~line 3414; `action_run` ~line 3784; LK_PICK `A_LOAD_PROJ` line ~2614)
- Modify: `app/host.c` (`app_host_cmd` beside `row` ~line 1414: `rowval`, `norow`)
- Modify: `tests/scenarios/glo_projects.xy`, and every scenario that used HOME's LOAD PROJECT / SAVE TO / NEW
  PROJECT rows: `grep -ln "LOAD PROJECT\|SAVE TO\|NEW PROJECT" tests/scenarios/*.xy`

**Acceptance Criteria:**
- [ ] `rowval PROJ 1 = 122 bpm, 1 scene`, `rowval PROJ 2 (OPEN) = 98.5 bpm, 2 scenes`, `rowval PROJ 3 = empty` pass on GLO's list
- [ ] SEL on PROJ 1 with nothing unsaved loads it at once (`expect projslot 1`, `expect ask 0`)
- [ ] with unsaved changes, SEL on a saved slot asks first (`expect ask 1`), SEL again loads it
- [ ] SEL on the empty PROJ 3 asks, then `expect projslot 3` and `expect dirty 1`
- [ ] `norow LOAD PROJECT` and `norow NEW PROJECT` pass on HOME's list
- [ ] every scenario in `tests/scenarios/` passes (`sh tests/run_tests.sh`: no `FAIL scenario-*`)

**Verify:** `./build.sh --host && sh tests/run_tests.sh` → every `scenario-*` line `ok`

**Steps:**

- [ ] **Step 1: Extend the scenario** (append to `tests/scenarios/glo_projects.xy`):

```
# GLO's list: the projects first, the open one marked
tap GLO
tap SEL
expect listopen 1
rowval PROJ 1 = 122 bpm, 1 scene
rowval PROJ 2 (OPEN) = 98.5 bpm, 2 scenes
rowval PROJ 3 = empty
shot glo_projects.png
# a saved slot, nothing unsaved: loaded at once
row PROJ 1
tap SEL
wait 200
expect ask 0
expect projslot 1
expect tempo 122
# unsaved changes: asked first
tap HOME
tapkey w0
expect dirty 1
tap GLO
tap SEL
row PROJ 2
tap SEL
wait 50
expect ask 1
shot glo_projects_ask.png
tap SEL
wait 200
expect projslot 2
# an empty slot: a new project there, asked first
tap GLO
tap SEL
row PROJ 3
tap SEL
wait 50
expect ask 1
tap SEL
wait 200
expect projslot 3
expect dirty 1
# HOME's list: no project rows now
tap HOME
tap SEL
norow LOAD PROJECT
norow NEW PROJECT
norow SAVE TO
row RANDOMIZE PRESETS
```

- [ ] **Step 2: host.c, `rowval` and `norow`.** In `app_host_cmd`, before `if (!strcmp(cmd, "row"))`:

```c
    if (!strcmp(cmd, "rowval") || !strcmp(cmd, "norow")) {   /* rowval NAME = VALUE: the open list's row NAME shows
                                                               * VALUE; norow NAME: the list has no row NAME */
        int val = cmd[0] == 'r';
        const char *nm = plat_host_line + (val ? 6 : 5), *eq;
        size_t l, vl = 0;
        while (*nm == ' ')
            nm++;
        eq = val ? strstr(nm, " = ") : 0;
        l = eq ? (size_t)(eq - nm) : strcspn(nm, "\r\n");
        while (l && nm[l - 1] == ' ')
            l--;
        if (eq)
            vl = strcspn(eq + 3, "\r\n");
        list_build();
        for (int r = 0; r < lst.n; r++)
            if (strlen(lst.row[r].name) == l && !strncmp(lst.row[r].name, nm, l)) {
                int bad = !val || strlen(lst.row[r].val) != vl || strncmp(lst.row[r].val, eq + 3, vl);
                printf("%s %s %.*s: \"%s\"\n", bad ? "FAIL" : "  ok", cmd, (int)l, nm, lst.row[r].val);
                host_fails += bad;
                return 1;
            }
        printf("%s %s %.*s: not in the list\n", val ? "FAIL" : "  ok", cmd, (int)l, nm);
        host_fails += val;
        return 1;
    }
```

- [ ] **Step 3: Run the scenario, see it fail** at the first `rowval` (no such row).

- [ ] **Step 4: pages.c, the actions.** Append `A_PROJ1, A_PROJ2, A_PROJ3` before `NACTIONS` in the enum, and
  `"PROJ 1", "PROJ 2", "PROJ 3"` at the end of `ACTION_NAME` (after `"PING WIDTH"`). Add after the enum:

```c
_Static_assert(NPROJ == 3, "GLO's list: an action a slot (A_PROJ1..A_PROJ3)");
```

- [ ] **Step 5: pages.c, `list_projects`** (after `proj_slot_text`):

```c
/* GLO's list, first: the slots (the open one marked) with their tempo and scenes as saved, then SAVE TO and NEW
 * PROJECT */
static const char *const PROJ_ROW[2][NPROJ] = {{"PROJ 1", "PROJ 2", "PROJ 3"},
                                               {"PROJ 1 (OPEN)", "PROJ 2 (OPEN)", "PROJ 3 (OPEN)"}};
static void proj_summary(int slot, char *t)        /* "122 bpm, 4 scenes", "98.5 bpm", "empty" */
{
    uint16_t tx;
    uint8_t ns;
    char *e;
    if (!project_slot_info(slot, &tx, &ns)) {
        s_cat(t, "empty");
        return;
    }
    e = num_s(tx / 10u, t);
    if (tx % 10u) {
        *e++ = '.';
        e = num_s(tx % 10u, e);
    }
    e = s_cat(e, " bpm");
    if (ns)                                        /* (0: saved before the count was kept) */
        s_cat(num_s(ns, s_cat(e, ", ")), ns == 1 ? " scene" : " scenes");
}
static void list_projects(void)
{
    static char val[NPROJ][24], save_t[24];
    list_head("projects");
    for (int s = 0; s < NPROJ; s++) {
        proj_summary(s, val[s]);
        list_add(PROJ_ROW[s == proj_slot][s], val[s], LK_ACTION, A_PROJ1 + s);
    }
    proj_slot_text(ui.pick_save % NPROJ, save_t);
    list_add(ACTION_NAME[A_SAVE_TO], save_t, LK_PICK, A_SAVE_TO);
    list_add(ACTION_NAME[A_NEW_PROJ], 0, LK_ACTION, A_NEW_PROJ);
}
```

  Check `s_cat` / `num_s` (pages.c) write from the pointer given and return the end; `val[s]` is rebuilt every
  build, so start each with `val[s][0] = 0` if `s_cat` appends rather than writes.

- [ ] **Step 6: pages.c, GLO and HOME.** In the list build, `} else if (b == B_GLO) {`: call `list_projects();`
  first, before `if (list_is_setup(keep))`. In `list_home`, replace the project section's head and its three rows:

```c
    list_head("more");
    list_add(ACTION_NAME[A_RANDOM_SOUNDS], 0, LK_ACTION, A_RANDOM_SOUNDS);
    list_add(ACTION_NAME[A_PANIC], 0, LK_ACTION, A_PANIC);
```

  and drop `slot_t`, `save_t` from its statics.

- [ ] **Step 7: ui.c, the rows.** Turn `do_new_proj` into `new_proj(int slot)` (slot < 0: the first free one, as
  now) and keep `do_new_proj` as its caller:

```c
static void new_proj(int slot)                   /* slot < 0: the first free one (none free: SAVE TO picks one) */
{
    engine_transport(0);
    engine_panic();
    project_defaults();
    project_random_sounds(plat_ms() * 2654435761u ^ proj_seed++);   /* a band of its own each time */
    after_load();
    ui.dirty = 1;
    proj_slot = PROJ_NOSLOT;
    if (slot >= 0 && slot < NPROJ)
        proj_slot = (uint8_t)slot;
    else
        for (int s = NPROJ - 1; s >= 0; s--)
            if (!project_exists(s))
                proj_slot = (uint8_t)s;
    if (proj_slot < NPROJ) {
        char t[16];
        num_s((uint32_t)proj_slot + 1u, s_cat(t, "slot "));
        say("NEW PROJECT", t);
    } else
        say("NEW PROJECT", "no free slot");
}
static void do_new_proj(void) { new_proj(-1); }
static void do_new_proj_here(void) { new_proj(ui.ask_arg % NPROJ); }
/* GLO's list: a slot's row. The open one: nothing; an empty one: a new project there, asked; a saved one: loaded,
 * asked first when the open project has unsaved changes */
static void act_project(int slot)
{
    char q[32];
    if (slot == proj_slot)
        return;
    ui.ask_arg = (uint8_t)slot;
    if (!project_exists(slot)) {
        s_cat(num_s((uint32_t)slot + 1u, s_cat(q, "NEW PROJECT IN PROJ ")), "?");
        ask(q, do_new_proj_here);
    } else if (ui.dirty)
        ask("LOSE UNSAVED CHANGES?", do_load_proj);
    else {
        do_load_proj();
        lst.open = 0;                            /* (the other song: back to its page, as after the question) */
    }
}
```

  (Check `ask` copies its question; if it keeps the pointer, make `q` static.) In `action_run`, replace the
  `case A_LOAD_PROJ:` block with:

```c
    case A_PROJ1: case A_PROJ2: case A_PROJ3: act_project(a - A_PROJ1); return;
```

  and in the LK_PICK switch remove `case A_LOAD_PROJ: ...` (no row uses it now). `A_LOAD_PROJ` stays in the enum
  (unused) only if removing it is more churn than it's worth; otherwise remove it with its ACTION_NAME entry.

- [ ] **Step 8: Run `glo_projects.xy`.** Expected: exit 0. Look at `app/build/scenarios/glo_projects/glo_projects.png`
  and `glo_projects_ask.png` (Read tool): the rows and the question fit the screen.

- [ ] **Step 9: Move the other scenarios to GLO.** For each file from
  `grep -ln "LOAD PROJECT\|SAVE TO\|NEW PROJECT" tests/scenarios/*.xy`: where it opens HOME's list for these rows
  (`tap HOME` / `tap SEL` / `row LOAD PROJECT`), open GLO's (`tap GLO` / `tap SEL`); `row LOAD PROJECT` + `turn ALGO
  n` + `tap SEL` becomes `row PROJ k` + `tap SEL` for the slot it picked (`PROJ k (OPEN)` if that one is open);
  `row NEW PROJECT` and `row SAVE TO` stay, on GLO's list. Keep each scenario's intent and its comments true
  (`slot_reads.xy` and `projects.xy` describe HOME's list: say GLO's). Run each until it passes.

- [ ] **Step 10: Run everything** — `sh tests/run_tests.sh`. Expected: no `FAIL` (the layout audit included).

- [ ] **Step 11: Commit**

```bash
git add app/pages.c app/ui.c app/host.c tests/scenarios/
git commit -m "LOOM-8: the projects on GLO's list, each slot with its tempo and scenes; SEL loads it, or starts a new project in an empty one; HOME's list without them"
```

### Task 3: Help, screen reader, manual

**Goal:** the words around the change: GLO's and HOME's help lines, the screen reader's words for the new rows, the
manual's "load a project".

**Files:**
- Modify: `app/pages.c` help table ~line 2572-2576 (`B_GLO` "Mix", `B_HOME` "Arrange")
- Modify: `app/words.tsv` (and the generated `app/web/words/*.json` the build makes: commit the new one, never
  delete old ones)
- Modify: `docs/MANUAL.md` (~line 190, "load a project"; any other place that sends people to HOME's list for
  projects: `grep -n "LOAD PROJECT\|HOME.*SEL.*project" docs/MANUAL.md`)

**Acceptance Criteria:**
- [ ] HOME's help no longer says "Press SEL for projects"; GLO's help says it
- [ ] the screen reader audit passes (fm1-reader `tools/audit.mjs`, as `tests/run_tests.sh` or the `fm1-screen-reader` skill runs it) with "bpm" said as "beats per minute"
- [ ] the manual's "load a project" row describes GLO's list

**Verify:** `sh tests/run_tests.sh` → no `FAIL`; `grep -n "Press SEL for projects" app/pages.c` → only GLO's help

**Steps:**

- [ ] **Step 1: Help lines** (pages.c). GLO: `"Tap a black key to mute, hold to solo, tap twice to keep it.",
  "Press SEL for projects. Turn SELECT for the master, Setup."` HOME: `"Press white keys for scenes, black keys to
  mute.", "Hold HOME, press REC to undo, PLAY to redo."` Keep each line within the width the layout audit allows
  (run the `pages` / layout scenarios; shorten if it flags them).

- [ ] **Step 2: words.tsv.** Add `bpm\tbeats per minute` beside `BPM`. Use the `fm1-screen-reader` skill's audit
  on a dump of GLO's list (`listdump` in a scenario, or its documented way) and add lines for anything it flags
  ("OPEN" if it reads oddly).

- [ ] **Step 3: Manual.** Replace the "load a project" row's text:

```
| load a project | Press **GLO** and press **SEL**. The list starts with your three projects, each with its tempo and scenes. Pick one with **SELECT** and press **SEL** to load it. An empty slot starts a new project there, with its sounds picked at random. <!-- hint: press GLO, SEL; turn SELECT --> |
```

  and fix any other sentence that sends readers to HOME's list for projects.

- [ ] **Step 4: Run everything** — `sh tests/run_tests.sh`. Expected: no `FAIL`.

- [ ] **Step 5: Commit**

```bash
git add app/pages.c app/words.tsv app/web/words docs/MANUAL.md
git commit -m "LOOM-8: GLO's help points at the projects; the manual and the screen reader's words for GLO's list"
```
