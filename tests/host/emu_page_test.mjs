// SPDX-License-Identifier: MPL-2.0
// The browser build as the page runs it (./build.sh --web): fm1-base's page with LOOM-8's bits (tools/emu_page.py),
// its worklet with the emulator's parts, among them "resample": in a browser whose audio runs at 48 kHz (many Macs)
// the FM-1's clock keeps the page's time (without it LOOM-8 plays 48/44.1 fast and high). fm1-base's worklet under
// node (base/tests/emu_harness.mjs).
//   node tests/host/emu_page_test.mjs app/build/emu
import { existsSync, readFileSync } from "node:fs";
import { startEmu, check, done } from "../../base/tests/emu_harness.mjs";

const dir = process.argv[2] || "app/build/emu";
const page = readFileSync(`${dir}/index.html`, "utf8");
check(page.includes("<title>LOOM-8 in the browser</title>") && page.includes('<section class="help">'), "fm1-base's page with LOOM-8's name and help");
check(page.includes("{{") === false, "no placeholder left in the page");
const read = (f) => existsSync(`${dir}/${f}`) ? readFileSync(`${dir}/${f}`, "utf8") : "";
check(read("app_own.js").includes("LOOM-8") && read("app.js").includes("app_own.js"), "LOOM-8's app.js on the page");
check(read("app_worklet.js").includes("resample_worklet.js") && existsSync(`${dir}/parts/resample_worklet.js`), "the worklet has the part resample");
const emu = await startEmu(dir, { sampleRate: 48000 });
emu.run(500);
const t0 = emu.ex.web_now();
emu.run(1000);
const ms = emu.ex.web_now() - t0;
check(Math.abs(ms - 1000) <= 12, `at 48 kHz the FM-1 ran ${ms} ms in the page's 1000`);
done();
