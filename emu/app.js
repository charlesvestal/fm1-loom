// made by tools/build.py: the emulator's parts (fm1.toml emu) and the app's own part of the page
import { compose } from "./parts/parts.js";
import * as own from "./app_own.js";
export const { attach, message } = compose({  }, own, { id: "loom", name: "loom", backupExt: "loom8" });
