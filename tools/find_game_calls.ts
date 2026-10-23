// NOT ORIGINAL - lists the GameFunction calls under src/game whose address we have rewritten: they reach our code
// through the hook's jump (or the original bytes, when the replacement is source only or disabled), and become
// direct C++ calls once cleaned up.
//   node tools/find_game_calls.ts                  counts per calling file
//   node tools/find_game_calls.ts --list [filter]  every call: file:line, the address, our function, where it lives
//                                                  (filter: a substring of the calling file or the target's file)
// GameFunction calls through a named constant (kCreate...) are counted separately, not resolved.

import * as fs from "node:fs";
import { globRecursive } from "./lib/files.ts";
import { isMain, print, runMain } from "./lib/py.ts";

interface Target {
    name: string;
    file: string;
    sourceOnly: boolean;
}

interface Call {
    file: string;
    line: number;
    addr: number;
    target: Target;
}

const hex = (n: number): string => `0x${n.toString(16).toUpperCase().padStart(8, "0")}`;
const slash = (p: string): string => p.replaceAll("\\", "/");

export function readReplacements(root = "src/game"): Map<number, Target> {
    const targets = new Map<number, Target>();
    for (const p of globRecursive(root, ".cpp")) {
        const text = fs.readFileSync(p, "utf-8");
        for (const m of text.matchAll(
            /\bREPLACE(_SHADOW|_UNCHECKED|_SOURCE_ONLY)?\(\s*0x([0-9A-Fa-f]{8})\s*,\s*([^;]*?)\)\s*;/g,
        ))
            targets.set(Number.parseInt(m[2]!, 16), {
                name: m[3]!.trim(),
                file: slash(p),
                sourceOnly: m[1] === "_SOURCE_ONLY",
            });
    }
    return targets;
}

function main(argv: string[]): number {
    const list = argv.includes("--list");
    const filter = argv.filter((a) => !a.startsWith("--"))[0];
    const targets = readReplacements();
    const calls: Call[] = [];
    let named = 0;
    for (const p of globRecursive("src/game", ".cpp").concat(globRecursive("src/game", ".h"))) {
        const lines = fs.readFileSync(p, "utf-8").split(/\r?\n/);
        // The template argument can span lines: match on the whole text, then find the line.
        const text = lines.join("\n");
        for (const m of text.matchAll(/GameFunction<(?:[^<>]|<[^<>]*>)*>\s*\(\s*([A-Za-z_0-9:]+)\s*\)/g)) {
            const arg = m[1]!;
            if (!/^0x[0-9A-Fa-f]+$/.test(arg)) {
                named++;
                continue;
            }
            const addr = Number.parseInt(arg, 16);
            const target = targets.get(addr);
            if (target === undefined) continue;
            const line = text.slice(0, m.index).split("\n").length;
            calls.push({ file: slash(p), line, addr, target });
        }
    }
    const shown =
        filter === undefined ? calls : calls.filter((c) => c.file.includes(filter) || c.target.file.includes(filter));
    if (list) {
        for (const c of shown)
            print(
                `${c.file}:${c.line}  ${hex(c.addr)}  ${c.target.name}  (${c.target.file}${c.target.sourceOnly ? ", source only" : ""})`,
            );
    } else {
        const perFile = new Map<string, number>();
        for (const c of shown) perFile.set(c.file, (perFile.get(c.file) ?? 0) + 1);
        for (const [f, n] of [...perFile].sort((a, b) => b[1] - a[1])) print(`${String(n).padStart(5)}  ${f}`);
    }
    const sourceOnly = shown.filter((c) => c.target.sourceOnly).length;
    print(
        `${shown.length} calls to rewritten functions (${sourceOnly} to source-only ones) in ${new Set(shown.map((c) => c.file)).size} files; ${named} GameFunction calls through a named constant not resolved`,
    );
    return 0;
}

if (isMain(import.meta.url)) await runMain(() => main(process.argv.slice(2)));
