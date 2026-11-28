// SPDX-License-Identifier: MPL-2.0
// The export page's Worker (a module worker): render.js away from the page, so the page stays responsive. Messages in:
// {type: "setup", wasm, file} -> {type: "slots", slots} | {type: "error", message};
// {type: "open", slot} -> {type: "opened", song, scenes}; {type: "render", pick} -> {type: "progress", p} ...,
// then {type: "wav", bytes} (transferred).
// ./extras.js is written by tools/make_pages.py next to this file: a module whose default export returns the backup
// page's EXTRAS (app/web/samples/loom_samples.js and app/web/backup/extras.js in one function, as the backup page has
// them), for a backup's samples.
import { exportSetup, exportOpen, exportRender, useExtras } from "./render.js";
import extras from "./extras.js";

useExtras(extras);
self.onmessage = async (e) => {
  const m = e.data;
  try {
    if (m.type === "setup") {
      postMessage({ type: "slots", slots: await exportSetup(m.wasm, new Uint8Array(m.file)) });
    } else if (m.type === "open") {
      postMessage({ type: "opened", ...exportOpen(m.slot) });
    } else if (m.type === "render") {
      const bytes = await exportRender(m.pick, (p) => postMessage({ type: "progress", p }));
      postMessage({ type: "wav", bytes }, [bytes.buffer]);
    }
  } catch (err) {
    postMessage({ type: "error", message: err.message || String(err) });
  }
};
