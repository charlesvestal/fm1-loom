# Master CLIP Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers-extended-cc:subagent-driven-development (recommended) or superpowers-extended-cc:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A CLIP knob on the master (the empty EQ slot, `MS_RSV3`): drive into a soft clipper after comp / pump /
level, before the limiter, so a mix can be mastered loud.

**Architecture:** One more stage inside `master_process`'s last loop (`app/dsp/fx.c`), after `s *= lev`, before X0X's
limiter; skipped entirely at CLIP 0 (bit-exact with today). The UI, host scripts and MIDI CC map gain the slot.

**Tech Stack:** C99 single-precision (no double, `fastmath.h`), host tests in `tests/host/`.

**User decisions (already made):**
- "A" (a): a soft clip, transparent-ish, odd harmonics, not a drive/color.
- "yup": CLIP in the master EQ page's empty slot (low, mid, high, clip).
- "go ahead": spec `docs/superpowers/specs/2026-11-25-master-clip-design.md` approved.

---

### Task 1: The clip stage in the master (DSP)

**Goal:** `MS_CLIP` replaces `MS_RSV3`; master_set computes the drive; master_process clips before the limiter.

**Files:**
- Modify: `app/dsp/fx.h:106` (enum), `fx.h:118` (comment "3 ignored")
- Modify: `app/dsp/fx.c` (MS_NAME ~997, struct master ~1005, master_set ~1262, master_process ~1396-1430)
- Test: `tests/host/fx_test.c` (after the limiter test ~598), `tests/host/dsp_hash.c` (master_cases ~349)

**Acceptance Criteria:**
- [ ] `tests/host/dsp_hash.sh` (reference saved before the change): every existing case the same
- [ ] fx_test: CLIP 127 on a 4x signal: peak <= 1.0, no NaN; CLIP 127 raises a -12 dBFS sine's RMS by > 6 dB; CLIP 0 output equals a fresh master's
- [ ] the build still has no double (`./build.sh` passes)

**Verify:** `sh tests/host/fx_test.sh && sh tests/host/dsp_hash.sh`

**Steps:**

- [ ] **Step 1: Save the DSP fingerprints before touching anything**

Run: `sh tests/host/dsp_hash.sh save` → "dsp_hash: N cases saved"

- [ ] **Step 2: Write the failing tests** in `tests/host/fx_test.c`, after the limiter's block:

```c
        MS_INIT();
        {   /* CLIP: a soft clip before the limiter. At 127 a 4x signal still never passes 1.0; a -12 dBFS sine
             * (0.25 peak into the bus, x0.4 headroom) comes out louder by more than 6 dB */
            float pk = 0.0f, e0 = 0.0f, e1 = 0.0f;
            int pass;
            master_set(ms, MS_CLIP, 127);
            master_set(ms, MS_LEVEL, 127);
            for (b = 0; b < 400; b++) {
                for (i = 0; i < BLK; i++) {
                    pl[i] = pr[i] = 2.0f * noise();
                    mll[i] = mlr[i] = 2.0f * ((b & 8) ? 1.0f : -1.0f);
                }
                master_process(ms, pl, pr, mll, mlr, ol2, or2, BLK);
                for (i = 0; i < BLK; i++) {
                    ok &= ol2[i] == ol2[i];
                    pk = fm_maxf(pk, fm_maxf(fm_fabsf(ol2[i]), fm_fabsf(or2[i])));
                }
            }
            snprintf(name, sizeof name, "clip 127 on a 4x signal: peak %.3f, no NaN", (double)pk);
            check(name, ok && pk <= 1.0f);
            for (pass = 0; pass < 2; pass++) {
                float ph = 0.0f, *e = pass ? &e1 : &e0;
                MS_INIT();
                master_set(ms, MS_CLIP, pass ? 127 : 0);
                for (b = 0; b < 200; b++) {
                    for (i = 0; i < BLK; i++) {
                        ph += 220.0f / SR;
                        if (ph >= 1.0f) ph -= 1.0f;
                        pl[i] = pr[i] = 0.0f;
                        mll[i] = mlr[i] = 0.625f * fm_sin_turns(ph);   /* x0.4 headroom: 0.25 on the bus */
                    }
                    master_process(ms, pl, pr, mll, mlr, ol2, or2, BLK);
                    if (b >= 100)
                        for (i = 0; i < BLK; i++)
                            *e += ol2[i] * ol2[i];
                }
            }
            snprintf(name, sizeof name, "clip 127 on a -12 dB sine: %+.1f dB", (double)(10.0f * 0.30103f * fm_log2f(e1 / e0)));
            check(name, e1 > e0 * 4.0f);
        }
```

(Confirm `fm_sin_turns`, `fm_log2f` are in scope in fx_test.c — they are used in fx.c / the EQ test; if
`fm_sin_turns` is not visible, use `fm_sinf(FM_TWO_PI * ph)`.)

- [ ] **Step 3: Run, see it fail** — `sh tests/host/fx_test.sh` → compile error, `MS_CLIP` undeclared.

- [ ] **Step 4: Implement.** `fx.h`: `MS_RSV3` → `MS_CLIP`; the master_set comment "3 ignored" → "3 clip 0..127".
`fx.c`: MS_NAME[3] `"-"` → `"clip"`. In `struct master`, beside `gp, gm, glev`: `float cg;  /* CLIP's drive (1 .. 4x), 0: off */`.
master_set:

```c
    else if (p == MS_CLIP)
        m->cg = v ? fm_exp2f((float)v * (2.0f / 127.0f)) : 0.0f;   /* 0 .. +12 dB of drive; 0: the stage skipped */
```

A curve beside `soft_ceiling`:

```c
/* CLIP: a symmetric soft knee, flat to 0.5, then rounding towards the limiter's 0.8 (odd harmonics only) */
static inline float clip_knee(float s)
{
    if (s > 0.5f || s < -0.5f)
        s = s > 0.0f ? 0.5f + 0.3f * fm_tanhf((s - 0.5f) * (1.0f / 0.3f))
                     : -0.5f - 0.3f * fm_tanhf((-s - 0.5f) * (1.0f / 0.3f));
    return s;
}
```

In master_process's last block: `const float cg = m->cg;` with the other consts, and after `sr *= lev;`:

```c
            if (cg != 0.0f && STAGE(4)) {         /* CLIP: the drive into the knee, before the limiter */
                s = clip_knee(s * cg);
                sr = clip_knee(sr * cg);
            }
```

- [ ] **Step 5: dsp_hash case.** In `tests/host/dsp_hash.c` master_cases, a new case after the existing two
(appended, so the old lines keep their order): `static const int clip[] = {MS_CLIP, 90, MS_COMP, 60, -1};` (match
the terminator / format the existing `off` / `full` arrays use) and `master_case("master clip 90", clip);`.
Re-run `sh tests/host/dsp_hash.sh`: the only difference is the new line; then `sh tests/host/dsp_hash.sh save`.

- [ ] **Step 6: Verify** — `sh tests/host/fx_test.sh` all pass; `sh tests/host/dsp_hash.sh` "all the same".

- [ ] **Step 7: Commit** — `git add app/dsp/fx.h app/dsp/fx.c tests/host/fx_test.c tests/host/dsp_hash.c` and commit
"master CLIP: drive (0 .. +12 dB) into a soft knee after the comp, before the limiter; 0 skips it".

---

### Task 2: The knob on the screen, in scripts, on MIDI, in the manual

**Goal:** CLIP shown and turned on Master EQ, settable from demo scripts (`clip`) and MIDI CC 19 on the master channel.

**Files:**
- Modify: `app/pages.c:1883` (the skip), `app/ui.c:2238` (the skip), `app/host.c:417` (MN[3] "clip"),
  `app/midicc.c:28` (`{19, MS_CLIP}`), `app/words.tsv` (only if the audit asks for "clip"), `docs/MANUAL.md:1495`
  and the MIDI CC table (search "16" / "EQ low" in the MIDI section), `app/project.c:329` (`MS_RSV3` → `MS_CLIP`).
- Any other `MS_RSV3` users: `grep -rn MS_RSV3 app tests` must come back empty.

**Acceptance Criteria:**
- [ ] `grep -rn MS_RSV3 app tests` empty
- [ ] Master EQ shows four knobs: low, mid, high, clip (host screenshot or ui_sim)
- [ ] `sh tests/run_tests.sh` passes; `./build.sh` passes (size within the slot)

**Verify:** `sh tests/run_tests.sh && ./build.sh`

**Steps:**

- [ ] **Step 1:** pages.c: delete `if (m == MS_RSV3) continue;  /* (EQ: three knobs) */`; the default branch then
shows it as a plain 0..127 value. ui.c master_knob: delete the `MS_RSV3` return. host.c MN[3] `"-"` → `"clip"`.
midicc.c: add `{19, MS_CLIP}` to the master's table. project.c:329 `MS_RSV3` → `MS_CLIP` (stays 0: off by default).
- [ ] **Step 2:** MANUAL: "Master EQ has low, mid and high. 64 is flat." → "Master EQ has low, mid and high (64 is
flat) and **clip**: 0 is off; turning it up drives the mix into a soft clipper before the limiter, so it gets louder
and denser, the kick's and snare's peaks rounded off." Add CC 19 = clip to the MIDI table. Update the
screen-master.png alt text if regenerated (screens regenerate before a release, not now).
- [ ] **Step 3:** `sh tests/run_tests.sh` (includes the screen-reader audit); fix words if it flags "clip".
- [ ] **Step 4:** `./build.sh` → size printed, under the slot.
- [ ] **Step 5: Commit** — "master CLIP on Master EQ's fourth knob, scripts (clip), CC 19, the manual".

---

### Task 3: Measure on the techno demo

**Goal:** Loudness and crest factor of `tools/demos/techno.xy` at CLIP 0 / 40 / 80 / 127, renders for the user.

**Files:**
- Read: `tools/demos/analyze.py` (its `lufs()`), `tools/demos/techno.xy` (unchanged; variants in the scratchpad)

**Acceptance Criteria:**
- [ ] A table: CLIP value, integrated LUFS, peak dBFS, crest (peak - RMS dB)
- [ ] Four WAVs handed to the user (SendUserFile)

**Verify:** the table printed from analyze.py's numbers.

**Steps:**

- [ ] **Step 1:** Build the host: `make`/the tests' build of `app/build/host/loom_host` (run_tests.sh builds it).
- [ ] **Step 2:** For v in 0 40 80 127: copy techno.xy to the scratchpad with `master clip v` added after
`master 4096` (check host.c's script syntax for a master value: the `MN` names, line ~1394); render
`app/build/host/loom_host X.xy OUT_v`.
- [ ] **Step 3:** Measure each with analyze.py; tabulate.
- [ ] **Step 4:** Send the renders and the table; no commit unless the user wants the demo changed (that is part B).
