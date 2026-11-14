// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Henri David
// The page's side of the emulator's optional parts (fm1.toml emu = [...], tools/build.py --web). The build makes the
// page's app.js from this: compose(parts, own, info) gives the attach / message pair the page calls (index.html: its
// app part), for every part and then the app's own app.js (built as app_own.js). Each part is web/emu/parts/NAME.js,
// a module with any of
//   attach(ctx)   when the emulator starts, before the wasm goes to the worklet. ctx: what the page gives an app part
//                 (node, ac, status), and id, name (fm1.toml), bar (the page's row of buttons), post(m, transfer) (a
//                 message to the part's worklet side: { type: "app", part: NAME, ...m }), beforeLoad(fn) (fn(msg) may
//                 change the load message, or a promise of it: msg.parts.NAME goes to the worklet side's load)
//   message(m)    a message from the part's worklet side (fm1.part(NAME, m) there)
// The worklet's side is parts_worklet.js.

export function compose(parts, own, info) {
  const list = Object.entries(parts).map(([name, m]) => ({ name, m }));
  const hooks = [];
  function attach(ctx) {
    const port = ctx.node.port, post = port.postMessage.bind(port);
    port.postMessage = (m, transfer) => {
      if (!m || m.type !== "load" || !hooks.length) return post(m, transfer);
      (async () => {
        let msg = { ...m, parts: {} };
        for (const fn of hooks) msg = (await fn(msg)) || msg;
        post(msg, msg.wasm instanceof ArrayBuffer ? [msg.wasm] : []);
      })().catch((e) => {
        if (ctx.status) ctx.status("An emulator part could not start: " + (e && e.message || e));
        post(m, transfer);
      });
    };
    const bar = document.querySelector(".bar");
    for (const p of list) {
      if (!p.m.attach) continue;
      p.m.attach({ ...ctx, ...info, bar, beforeLoad: (fn) => hooks.push(fn),
        post: (m, transfer) => post({ ...m, type: "app", part: p.name }, transfer) });
    }
    if (own.attach) own.attach(ctx);
  }
  function message(m) {
    if (m && m.part) {
      const p = list.find((x) => x.name === m.part);
      if (p && p.m.message) p.m.message(m);
    } else if (own.message) {
      own.message(m);
    }
  }
  return { attach, message };
}
