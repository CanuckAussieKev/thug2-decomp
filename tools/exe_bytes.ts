// NOT ORIGINAL - the reference build's bytes at an address, as the start value of a global (standalone step 3:
// GAME_GLOBAL's initialiser): hex, then as 32-bit words (hex / signed / float) and as text.
//   node tools/exe_bytes.ts <address> <size> [--exe <reference build>]

import * as fs from "node:fs";
import { Image } from "./code_coverage.ts";
import { REFERENCE_EXE } from "./pipeline/lib/decomp.ts";
import { isMain, print, runMain } from "./pipeline/lib/py.ts";

function main(): number {
    const args = process.argv.slice(2);
    const exeAt = args.indexOf("--exe");
    const exe = exeAt >= 0 ? args[exeAt + 1]! : REFERENCE_EXE;
    const address = parseInt(args[0] ?? "", 16);
    const size = parseInt(args[1] ?? "", 0);
    if (!Number.isFinite(address) || !Number.isFinite(size))
        throw new RangeError("usage: exe_bytes.ts <address> <size> [--exe <exe>]");
    const img = new Image(fs.readFileSync(exe));
    const bytes: number[] = [];
    for (let i = 0; i < size; i++) bytes.push(img.byte(address + i) ?? 0);
    for (let i = 0; i < size; i += 16)
        print(
            `${(address + i).toString(16).toUpperCase().padStart(8, "0")}  ${bytes
                .slice(i, i + 16)
                .map((b) => b.toString(16).padStart(2, "0"))
                .join(" ")}`,
        );
    const buf = Buffer.from(bytes);
    for (let i = 0; i + 4 <= size; i += 4) {
        const u = buf.readUInt32LE(i);
        print(
            `  +0x${i.toString(16).padStart(3, "0")}  0x${u.toString(16).toUpperCase().padStart(8, "0")}  ${buf.readInt32LE(i)}  ${buf.readFloatLE(i)}`,
        );
    }
    const text = bytes.map((b) => (b >= 0x20 && b < 0x7f ? String.fromCharCode(b) : ".")).join("");
    print(`  text: ${text}`);
    return 0;
}

if (isMain(import.meta.url)) await runMain(() => main());
