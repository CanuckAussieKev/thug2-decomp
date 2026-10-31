// NOT ORIGINAL - an exe virtual table (or member / command table) slot by slot, with the function our source REPLACEs
// at each slot's address: what a GAME_VTABLE (src/hook/game_code.h) lists. The table runs while the words point into
// code up to the next table start (tools/vtable_extents.ts), or to the given count.
//   node tools/exe_vtable.ts <address> [count] [--exe <reference build>]

import * as fs from "node:fs";
import { Image } from "./code_coverage.ts";
import { registrations } from "./game_function_map.ts";
import { REFERENCE_EXE } from "./pipeline/lib/decomp.ts";
import { globRecursive, readCsvDicts } from "./pipeline/lib/files.ts";
import { isMain, print, runMain } from "./pipeline/lib/py.ts";
import { tableExtents } from "./vtable_extents.ts";

const hex = (n: number): string => `0x${n.toString(16).toUpperCase().padStart(8, "0")}`;

function main(): number {
    const args = process.argv.slice(2);
    const exeAt = args.indexOf("--exe");
    const exe = exeAt >= 0 ? args[exeAt + 1]! : REFERENCE_EXE;
    const address = parseInt(args[0] ?? "", 16);
    const count = args[1] && !args[1].startsWith("--") ? parseInt(args[1], 0) : 64;
    if (!Number.isFinite(address)) throw new RangeError("usage: exe_vtable.ts <address> [count] [--exe <exe>]");
    const img = new Image(fs.readFileSync(exe));
    const files = [".cpp", ".h"].flatMap((s) => globRecursive("src/game", s)).map((f) => f.replaceAll("\\", "/"));
    const regs = registrations(files);
    const library = new Map(
        readCsvDicts(fs.readFileSync("data/library_functions.csv", "utf-8")).map((r) => [
            parseInt(r["address"]!, 16),
            r["name"]!,
        ]),
    );
    // Where the table ends: at the next table start (tools/vtable_extents.ts: an address the exe's decoded code takes as
    // an immediate or indexes from, or a data word holds) or the first word that isn't code (or at the given count).
    const extent = tableExtents(exe).slots(address);
    for (let i = 0; i < count; i++) {
        if (
            args[1] === undefined &&
            i === extent &&
            img.dword(address + 4 * i) >= 0x401000 &&
            img.dword(address + 4 * i) < 0x645000
        ) {
            print(`  (${hex(address + 4 * i)}: another table starts here)`);
            break;
        }
        const code = img.dword(address + 4 * i);
        if (code < 0x401000 || code >= 0x645000) {
            if (args[1] === undefined) break;
            print(`  [${i}] ${hex(code)} (not code)`);
            continue;
        }
        const r = regs.get(code);
        const what = r
            ? r.map((x) => `${x.name}  (${x.kind}, ${x.file})`).join(" | ")
            : library.has(code)
              ? `library: ${library.get(code)} - GAME_LIBRARY_SLOT(${hex(code)})`
              : code >= 0x5beaae
                ? `library code - GAME_LIBRARY_SLOT(${hex(code)})`
                : "NOT REPLACEd";
        print(`  [${i}] ${hex(code)}  ${what}`);
    }
    return 0;
}

if (isMain(import.meta.url)) await runMain(() => main());
