// NOT ORIGINAL - for turning GameFunction calls to our own rewritten functions into direct C++ calls (standalone
// step 2): every GameFunction<...>(address) call site in src/game whose address has a REPLACE line, with the function
// registered there.
//   node tools/game_function_map.ts [--out <file>] [--folder <src/game/... prefix>]
// Each line: caller file:line | address | REPLACE kind | our function (as the REPLACE line names it) | its file.

import * as fs from "node:fs";
import { globRecursive } from "./pipeline/lib/files.ts";
import { isMain, print, runMain } from "./pipeline/lib/py.ts";

export interface Registered {
    kind: string;
    name: string;
    file: string;
}

export function registrations(files: readonly string[]): Map<number, Registered[]> {
    const found = new Map<number, Registered[]>();
    for (const file of files) {
        const lines = fs.readFileSync(file, "utf-8").split(/\r?\n/);
        for (const line of lines) {
            const m = /^\s*(REPLACE\w*)\(0x([0-9A-Fa-f]{8}),\s*(.*)\);/.exec(line);
            if (!m) continue;
            const a = parseInt(m[2]!, 16);
            if (!found.has(a)) found.set(a, []);
            found.get(a)!.push({ kind: m[1]!, name: m[3]!.replace(/\s*\/\/.*$/, "").replace(/\);?$/, ""), file });
        }
    }
    return found;
}

function main(): number {
    const args = process.argv.slice(2);
    const at = (flag: string): string | undefined => {
        const i = args.indexOf(flag);
        return i >= 0 ? args[i + 1] : undefined;
    };
    const out = at("--out") ?? "out/game_function_map.txt";
    const folder = (at("--folder") ?? "src/game").replaceAll("\\", "/");
    const files = [".cpp", ".h"].flatMap((s) => globRecursive("src/game", s)).map((f) => f.replaceAll("\\", "/"));
    const regs = registrations(files);
    const lines: string[] = [];
    let sites = 0;
    for (const file of files.filter((f) => f.startsWith(folder)).sort()) {
        const text = fs.readFileSync(file, "utf-8").split(/\r?\n/);
        text.forEach((line, i) => {
            for (const m of line.matchAll(/GameFunction<.*?>\(\s*0x([0-9A-Fa-f]{6,8})\s*\)/g)) {
                const a = parseInt(m[1]!, 16);
                const r = regs.get(a);
                if (!r) continue;
                sites++;
                const hex = `0x${a.toString(16).toUpperCase().padStart(8, "0")}`;
                for (const reg of r) lines.push(`${file}:${i + 1} | ${hex} | ${reg.kind} | ${reg.name} | ${reg.file}`);
            }
        });
    }
    fs.writeFileSync(out, `${lines.join("\n")}\n`);
    print(`${sites} call sites to rewritten functions under ${folder} -> ${out}`);
    return 0;
}

if (isMain(import.meta.url)) await runMain(() => main());
