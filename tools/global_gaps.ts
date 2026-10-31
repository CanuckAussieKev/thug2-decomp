// NOT ORIGINAL - GAME_GLOBAL arrays that may be declared shorter than the original's: in the hooked build a GAME_GLOBAL
// is the exe's memory, so an array declared too short still has the exe's bytes behind it; in the standalone build it is
// a variable of the declared size, and the code that fills it past that end overwrites whatever the linker put next
// (bool[21] declared as one bool zeroed a function-pointer table; a 3072-entry strip buffer declared as 596 a hash
// table pointer).
//
// For every GAME_GLOBAL the original's code indexes from (`[address + register]`, tools/vtable_extents.ts' decode in
// data/derived/data_refs.txt): its declared size (the standalone build's PDB, tools/standalone/global_sizes.py) and
// the bytes after it up to the next address the original refers to or the next GAME_GLOBAL. Bytes nobody refers to
// right after an indexed array are most likely its own tail. Reviewed ones go in data/global_gaps_known.csv
// (address, reason).
//   node tools/global_gaps.ts [--exe <thug2_standalone.exe>] [--all]

import { spawnSync } from "node:child_process";
import * as fs from "node:fs";
import { globRecursive, readCsvDicts } from "./pipeline/lib/files.ts";
import { isMain, print, runMain } from "./pipeline/lib/py.ts";
import { tableExtents } from "./vtable_extents.ts";

const hex = (n: number): string => `0x${n.toString(16).toUpperCase().padStart(8, "0")}`;
const KNOWN = "data/global_gaps_known.csv";

function main(): number {
    const args = process.argv.slice(2);
    const exeAt = args.indexOf("--exe");
    const exe = exeAt >= 0 ? args[exeAt + 1]! : "out/build/verify/thug2_standalone.exe";
    tableExtents(); // (makes / refreshes the decode)
    const refs = new Map<number, Set<string>>();
    for (const l of fs.readFileSync("data/derived/data_refs.txt", "utf-8").split("\n")) {
        const p = l.trim().split(" ");
        if (p.length !== 3 || p[0] === "#") continue;
        const v = parseInt(p[2]!, 16);
        if (v < 0x67c000) continue; // (.data and .bss)
        if (!refs.has(v)) refs.set(v, new Set());
        refs.get(v)!.add(p[1]!);
    }
    const referenced = [...refs.keys()].sort((a, b) => a - b);
    const globals: { name: string; address: number; file: string }[] = [];
    for (const file of [".cpp", ".h"].flatMap((s) => globRecursive("src/game", s)))
        for (const m of fs
            .readFileSync(file, "utf-8")
            .matchAll(/^\s*(?:static\s+)?GAME_GLOBAL\w*\(\s*(?:[^;]*?),\s*(\w+),\s*0x([0-9A-Fa-f]{8})/gm))
            globals.push({ name: m[1]!, address: parseInt(m[2]!, 16), file: file.replaceAll("\\", "/") });
    globals.sort((a, b) => a.address - b.address);
    fs.mkdirSync("out", { recursive: true });
    fs.writeFileSync("out/globals_list.txt", globals.map((g) => `${hex(g.address)} ${g.name}`).join("\n"));
    const r = spawnSync("python", ["-I", "tools/standalone/global_sizes.py", exe, "out/globals_list.txt"], {
        encoding: "utf-8",
        maxBuffer: 1 << 28,
    });
    if (r.status !== 0) throw new Error(`global_sizes.py failed: ${r.stderr}`);
    const sizes = new Map<number, number>();
    for (const line of r.stdout.split("\n")) {
        const p = line.trim().split(" ");
        if (p.length === 3) sizes.set(parseInt(p[0]!, 16), parseInt(p[1]!, 10));
    }
    const known = new Map(
        fs.existsSync(KNOWN)
            ? readCsvDicts(fs.readFileSync(KNOWN, "utf-8")).map((k) => [parseInt(k["address"]!, 16), k["reason"]!])
            : [],
    );
    let flagged = 0;
    let unknownSize = 0;
    globals.forEach((g, i) => {
        const size = sizes.get(g.address) ?? -1;
        if (size <= 0) {
            unknownSize++;
            return;
        }
        const indexed = refs.get(g.address)?.has("indexed") ?? false;
        if (!indexed && !args.includes("--all")) return;
        const end = g.address + size;
        const nextGlobal = globals.slice(i + 1).find((x) => x.address >= end)?.address ?? 0x7dd000;
        const nextRef = referenced.find((a) => a >= end) ?? 0x7dd000;
        const next = Math.min(nextGlobal, nextRef);
        const gap = next - end;
        if (gap < 8) return; // (alignment)
        if (known.has(g.address)) return;
        flagged++;
        print(
            `${hex(g.address)} ${g.name} (${g.file}): ${size} bytes declared, then ${gap} bytes nobody refers to up to ${hex(next)}`,
        );
    });
    print(`${globals.length} GAME_GLOBALs (${unknownSize} not found in the PDB), ${flagged} to review`);
    return flagged ? 1 : 0;
}

if (isMain(import.meta.url)) await runMain(() => main());
