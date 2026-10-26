// NOT ORIGINAL - the THAW converter's parity cases (cases.ts): every compiled .qb under a folder replaced by
// <name>.qb.norm, its text without NeverScript's own nondeterminism (ns.exe writes its 0x2B checksum names in Go map
// order, different every run): the bytes before the first name, the names sorted, the tokens after them. Run on both
// sides' outputs alike (run.ts and its Python twin run.py, deleted with the Python converter), so that the harness
// compares them byte for byte.
//
//   node tools/parity/thaw_levels/qbnorm.ts <folder>

import * as fs from "node:fs";
import * as path from "node:path";
import * as qb from "../../pipeline/level_port/qb.ts";

// (as tools/parity/level_port/driver.ts qbNorm)
export function qbNorm(data: Buffer): string {
    const toks = [...qb.tokens(data, true)];
    const first = toks.find(([, t]) => t === 0x2b)?.[0] ?? data.length;
    const names = toks
        .filter(([, t]) => t === 0x2b)
        .map(([, , p]) => p.toString("hex"))
        .sort();
    const rest = toks
        .filter(([off, t]) => off >= first && t !== 0x2b)
        .map(([, t, p]) => Buffer.from([t]).toString("hex") + p.toString("hex"));
    return `${data.subarray(0, first).toString("hex")}\n${names.join("\n")}\n${rest.join("")}\n`;
}

export function normaliseFolder(folder: string): number {
    let count = 0;
    if (!fs.existsSync(folder)) return count;
    for (const entry of fs.readdirSync(folder, { withFileTypes: true })) {
        const file = path.join(folder, entry.name);
        if (entry.isDirectory()) count += normaliseFolder(file);
        else if (entry.name.toLowerCase().endsWith(".qb")) {
            fs.writeFileSync(`${file}.norm`, qbNorm(fs.readFileSync(file)));
            fs.rmSync(file);
            count += 1;
        }
    }
    return count;
}

if (process.argv[1] && path.resolve(process.argv[1]) === path.resolve(import.meta.filename)) {
    for (const folder of process.argv.slice(2)) normaliseFolder(folder);
}
