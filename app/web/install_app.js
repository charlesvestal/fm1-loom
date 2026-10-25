// SPDX-License-Identifier: GPL-3.0-only
// LOOM-8's steps around an install (fm1-base's web/installer.html APP): since 0.8 LOOM-8's own sounds (the kits'
// drums, the instruments, the sweep, the vinyl) live in the user sample bank, marked as the factory set's, after the
// user's samples. Before writing: the bank read from the LOOM-8 running (if one is), and whether the factory set fits
// beside it; if not, asked: the user's samples made smaller to fit (makeRoom), the install without the set, or
// nothing. After: the set added, the bank sent. tools/make_pages.py inlines app/web/samples/loom_samples.js before this
// and wraps both into one expression: ({ before, after }).

const FACTORY_URL = "../samples/factory.bin";
const esc = (t) => String(t).replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" })[c]);
const kb = (b) => `${Math.round(b / 1024)} KB`;
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

async function fetchFactory() {
  const r = await fetch(FACTORY_URL);
  if (!r.ok) throw new Error(`Couldn't fetch LOOM-8's sounds (HTTP ${r.status}).`);
  return factoryOf(new Uint8Array(await r.arrayBuffer()));
}

// the bank's items (kept as stored), or null if it cannot be read (no LOOM-8 running, or one too old to send it)
async function readItems(midi, ui, wait) {
  const link = await findLink(midi, wait);
  if (!link) return null;
  try {
    ui.say("Reading your samples…");
    const img = await readBank(link, (f) => ui.progress(f * 100));
    return img ? keptOf(img) : [];
  } catch (_) {
    return null;
  } finally {
    link.close();
    ui.progress(0);
  }
}

// what to do when the set does not fit as it is: "room" (r.items), "skip" or "cancel"
async function decide(ui, kept, factory, r) {
  const own = kept.filter((it) => !isFactory(it));
  const ownBytes = own.reduce((a, it) => a + sampleBytes(it.n), 0);
  const setBytes = factoryMissing(kept, factory).reduce((a, it) => a + sampleBytes(it.n), 0);
  const intro = `<p>From 0.8, LOOM-8's own sounds (the kits' drums, the instruments, the sweep and the vinyl:
    ${kb(setBytes)}) live in the sample bank, after your samples. Your samples take ${kb(ownBytes)}, so they
    don't fit beside them as they are.</p>`;
  if (r) {
    return ui.ask("Make room for LOOM-8's sounds?", intro +
      `<p>They fit if ${r.changed.map((n) => `<b>${esc(n)}</b>`).join(", ")} ${r.changed.length > 1 ? "are" : "is"}
      stored at ${r.cap / 1000 | 0} kHz: what is above ${r.cap / 2000 | 0} kHz goes. That can't be undone on the FM-1,
      so keep a backup (or the original files). The rest stays as it is.</p>
      <p>Without LOOM-8's sounds, the kits and presets that use them are silent until you add them from the samples
      page.</p>`,
      [["room", "Make room"], ["skip", "Install without them"], ["cancel", "Cancel"]]);
  }
  return ui.ask("Not enough room for LOOM-8's sounds", intro +
    `<p>Even at 11 kHz they don't fit. You can install now and, on the samples page, remove some samples and then add
    LOOM-8's sounds. Until then the kits and presets that use them are silent.</p>`,
    [["skip", "Install without them"], ["cancel", "Cancel"]]);
}

async function before(midi, ui) {
  let factory;
  try { factory = await fetchFactory(); } catch (e) { ui.log(String(e.message)); return { factory: null }; }
  const kept = await readItems(midi, ui, 800);
  ui.say("");
  if (kept === null) return { factory, later: true };       // (no LOOM-8 running, or too old: looked at after)
  if (!factoryMissing(kept, factory).length) return { factory, items: null };   // (nothing to add)
  const r = makeRoom(kept, factory);
  if (r && !r.cap) return { factory, items: r.items };
  const how = await decide(ui, kept, factory, r);
  if (how === "cancel") return false;
  if (how === "skip") return { factory, skipped: true };
  return { factory, items: r.items };
}

async function after(midi, ui, state) {
  if (!state || !state.factory) return;
  if (state.skipped) {
    ui.say("LOOM-8 is installed. Its own sounds aren't in the sample bank yet: add them from the samples page.");
    return;
  }
  let link = null;
  ui.say("Waiting for LOOM-8 to start…");
  for (let t = 0; t < 30 && !link; t++) {                   // (it restarts after the install)
    link = await findLink(midi, 800);
    if (!link) await sleep(1000);
  }
  if (!link) throw new Error("LOOM-8 is installed, but didn't answer for its sounds: add them from the samples page.");
  try {
    let items = state.items;
    if (state.later) {                                       // read now, from the LOOM-8 just installed
      link.close();
      link = null;
      const kept = (await readItems(midi, ui, 1500)) || [];
      if (!factoryMissing(kept, state.factory).length) items = null;
      else {
        const r = makeRoom(kept, state.factory);
        let how = r && !r.cap ? "room" : await decide(ui, kept, state.factory, r);
        if (how !== "room") {
          ui.say("LOOM-8 is installed. Its own sounds aren't in the sample bank yet: add them from the samples page.");
          return;
        }
        items = r.items;
      }
      link = await findLink(midi, 1500);
      if (!link) throw new Error("LOOM-8 stopped answering: add its sounds from the samples page.");
    }
    if (!items) { ui.say("LOOM-8 is installed, with its sounds."); return; }
    const img = build(items).img;
    ui.say("Adding LOOM-8's sounds to the sample bank…");
    await sendBank(link, img, (f) => ui.progress(f * 100));
    ui.progress(100);
    ui.say("LOOM-8 is installed, with its sounds.");
  } finally {
    if (link) link.close();
  }
}
