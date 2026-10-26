// NOT ORIGINAL - the TypeScript side of a THAW parity case (cases.ts): runs a tool and then normalises the compiled
// .qb files it wrote (qbnorm.ts); its stdout passed on with the side's own output folder written "{out}" (the two
// sides' folders differ), its exit code passed on. --copy <from> <to>: a folder copied first (the layouts
// migrate_layouts.ts changes, each side its own copy).
//
//   node tools/parity/thaw_levels/run.ts <out> [--copy <from> <to>] <tool.ts> [args...]

import { spawnSync } from "node:child_process";
import * as fs from "node:fs";
import { normaliseFolder } from "./qbnorm.ts";

const [out, ...rest] = process.argv.slice(2);
if (rest[0] === "--copy") {
    fs.cpSync(rest[1]!, rest[2]!, { recursive: true, preserveTimestamps: true });
    rest.splice(0, 3);
}
const result = spawnSync(process.execPath, rest, { stdio: ["inherit", "pipe", "inherit"], maxBuffer: 1 << 30 });
process.stdout.write(result.stdout.toString("utf-8").replaceAll(out!, "{out}"));
normaliseFolder(out!);
process.exitCode = result.status ?? 1;
