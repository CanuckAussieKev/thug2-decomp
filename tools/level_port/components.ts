// NOT ORIGINAL - how many script components (THUG2's CComponent pool) a level's .qb costs when it loads: every
// structure entry in its globals, nested structures included (script bodies not: they stay bytecode until run). The
// audit's method (docs/pending/shared-fields-audit.md; on the safe side by ~3k against the game's own count). A ported
// level's budget with THUG2's 86,000: about 34,750 (minus the ~51,250 in use when a level's .qb loads).
//
//   node tools/level_port/components.ts <levels folder> [<levels folder, before>]
//
// Each <folder>\<level>\<level>.qb: its count, and its count unshared - every node with its included structures'
// entries in place of the include, those structures not counted (nodes.shareNodeFields undone) - largest first. With a
// second folder, that folder's counts too (e.g. an older install).

import * as fs from "node:fs";
import * as path from "node:path";
import { isMain, print, runMain } from "../lib/py.ts";
import { expandIncludes } from "./nodes.ts";
import { Name, QbFile, type QbValue, Struct } from "./qb.ts";

export function components(value: QbValue): number {
    if (value instanceof Struct) return value.entries.reduce((n, [, v]) => n + 1 + components(v), 0);
    if (Array.isArray(value)) return value.reduce((n: number, v) => n + components(v), 0);
    return 0;
}

// [the count, the count with the node array's shared fields put back in its nodes].
export function qbComponents(data: Buffer, name = "qb"): [number, number] {
    const q = new QbFile(data, name, true);
    const globals = new Map(q.globals.map(([k, v]): [number, QbValue] => [k.value, v]));
    let shared = 0;
    let unshared = 0;
    const included = new Set<number>();
    for (const [k, v] of q.globals) {
        shared += components(v);
        if (!Array.isArray(v) || !(q.names.get(k.value) ?? "").toLowerCase().endsWith("_nodearray")) continue;
        for (const node of v) {
            if (!(node instanceof Struct)) continue;
            for (const [key, value] of node.entries) {
                if (key === null && value instanceof Name && globals.get(value.value) instanceof Struct)
                    included.add(value.value);
            }
        }
    }
    for (const [k, v] of q.globals) {
        if (included.has(k.value)) continue;
        unshared += components(
            Array.isArray(v) && (q.names.get(k.value) ?? "").toLowerCase().endsWith("_nodearray")
                ? expandIncludes(v, globals)
                : v,
        );
    }
    return [shared, unshared];
}

// {level: [count, unshared]} of a levels folder (<folder>\<level>\<level>.qb).
export function folderComponents(folder: string): Map<string, [number, number]> {
    const out = new Map<string, [number, number]>();
    for (const level of fs.readdirSync(folder).sort()) {
        const file = path.join(folder, level, `${level}.qb`);
        if (fs.existsSync(file)) out.set(level, qbComponents(fs.readFileSync(file), file));
    }
    return out;
}

function main(argv: string[]): number {
    if (argv.length < 1 || argv.length > 2) {
        print("usage: node tools/level_port/components.ts <levels folder> [<levels folder, before>]");
        return 2;
    }
    const now = folderComponents(argv[0]!);
    const before = argv.length === 2 ? folderComponents(argv[1]!) : null;
    print(`level\tcount\tunshared\tsaved${before ? "\tbefore" : ""}`);
    for (const [level, [n, u]] of [...now].sort((a, b) => b[1][0] - a[1][0])) {
        const old = before ? `\t${before.get(level)?.[0] ?? "-"}` : "";
        print(`${level}\t${n}\t${u}\t${u - n}${old}`);
    }
    return 0;
}

if (isMain(import.meta.url)) await runMain(() => main(process.argv.slice(2)));
