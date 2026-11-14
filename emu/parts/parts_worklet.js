// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Henri David
// The worklet's side of the emulator's optional parts (fm1.toml emu = [...], tools/build.py --web): the build's
// app_worklet.js loads the app's own app_worklet.js (as app_own_worklet.js), this, then each part's NAME_worklet.js,
// which calls
//   fm1Part({ name, load(fm1, data), message(fm1, m), process(fm1) })
//   load      after the wasm is instantiated, before the device boots; data: what the page's side put in the load
//             message (msg.parts[name], parts.js beforeLoad)
//   message   the page side's messages ({ type: "app", part: name, ... })
//   process   before each render quantum (after the app's own)
// fm1 is the worklet's processor (worklet.js: ex, the wasm's exports; mem; port); fm1.part(name, m, transfer) sends
// a message to the part's page side. The app's own fm1App (load, message, process, env) keeps working beside them.

const own = globalThis.fm1App || {};
const parts = [];
globalThis.fm1Part = (p) => { parts.push(p); };

// the load message's part data, which fm1App.load is not given: kept by the processor as it comes
const register = globalThis.registerProcessor;
globalThis.registerProcessor = (name, Proc) => register(name, name !== "fm1" ? Proc : class extends Proc {
  onMessage(m) {
    if (m && m.type === "load") this.partData = m.parts || {};
    return super.onMessage(m);
  }
});

globalThis.fm1App = {
  env: own.env,                          // (the app's own imports for app.wasm, worklet.js: kept as the app gave them)
  load(fm1) {
    fm1.part = (name, m, transfer) => fm1.port.postMessage({ ...m, type: "app", part: name }, transfer || []);
    for (const p of parts) if (p.load) p.load(fm1, (fm1.partData || {})[p.name]);
    if (own.load) own.load(fm1);
  },
  message(fm1, m) {
    if (m.part) {
      for (const p of parts) if (p.name === m.part && p.message) p.message(fm1, m);
    } else if (own.message) {
      own.message(fm1, m);
    }
  },
  process(fm1) {
    if (own.process) own.process(fm1);
    for (const p of parts) if (p.process) p.process(fm1);
  },
};
