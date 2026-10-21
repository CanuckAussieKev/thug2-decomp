// NOT ORIGINAL - how many script components (THUG2's CComponent pool: 86000 for everything loaded) a level's .qb
// costs when it loads: every structure entry in its globals, nested structures included (script bodies not: they stay
// bytecode until run). The audit's method (docs/pending/shared-fields-audit.md; on the safe side by ~3k against the
// game's own count). A ported level's budget: about 34,750 (86,000 - the ~51,250 in use when a level's .qb loads).
//
//   node tools/level_port/components.ts <levels folder> [<levels folder, after>]
//
// One folder: each <folder>\<level>\<level>.qb's count, largest first. Two: before / after per level (levels in both).

import * as fs from "node:fs";
import * as path from "node:path";
import { isMain, print, runMain } from "../lib/py.ts";
import { QbFile, type QbValue, Struct } from "./qb.ts";

export function components(value: QbValue): number {
    if (value instanceof Struct) return value.entries.reduce((n, [, v]) => n + 1 + components(v), 0);
    if (Array.isArray(value)) return value.reduce((n: number, v) => n + components(v), 0);
    return 0;
}

export function qbComponents(data: Buffer, name = "qb"): number {
    return new QbFile(data, name, true).globals.reduce((n, [, v]) => n + components(v), 0);
}

// {level: count} of a levels folder (<folder>\<level>\<level>.qb).
export function folderComponents(folder: string): Map<string, number> {
    const out = new Map<string, number>();
    for (const level of fs.readdirSync(folder).sort()) {
        const file = path.join(folder, level, `${level}.qb`);
        if (fs.existsSync(file)) out.set(level, qbComponents(fs.readFileSync(file), file));
    }
    return out;
}

function main(argv: string[]): number {
    if (argv.length < 1 || argv.length > 2) {
        print("usage: node tools/level_port/components.ts <levels folder> [<levels folder, after>]");
        return 2;
    }
    const before = folderComponents(argv[0]!);
    if (argv.length === 1) {
        for (const [level, n] of [...before].sort((a, b) => b[1] - a[1])) print(`${level}\t${n}`);
        return 0;
    }
    const after = folderComponents(argv[1]!);
    const rows = [...after].filter(([level]) => before.has(level)).sort((a, b) => b[1] - a[1]);
    print("level\tbefore\tafter\tsaved");
    for (const [level, n] of rows) print(`${level}\t${before.get(level)}\t${n}\t${before.get(level)! - n}`);
    return 0;
}

if (isMain(import.meta.url)) await runMain(() => main(process.argv.slice(2)));
