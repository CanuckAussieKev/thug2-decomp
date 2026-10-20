// NOT ORIGINAL - the TypeScript half of tools/thps3_levels' parity wrapper (wrap.py: the same steps): runs
// thps3_levels.ts with the arguments given, prints its stdout with <out> written as "{out}", passes its exit code on,
// and replaces each .qb it wrote under <out> by a .qb.txt in a stable form (wrap.py's qb_norm).
//
//   node tools/parity/thps3_levels/wrap.ts <out> <thps3_levels.ts arguments...>

import { spawnSync } from "node:child_process";
import * as fs from "node:fs";
import * as path from "node:path";
import { fileURLToPath } from "node:url";
import * as qb from "../../level_port/qb.ts";

const TOOLS = path.dirname(path.dirname(path.dirname(fileURLToPath(import.meta.url))));
const CONVERTER = path.join(TOOLS, "thps3_levels", "thps3_levels.ts");

function qbNorm(data: Buffer): string {
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

function walk(dir: string, found: string[]): string[] {
    if (!fs.existsSync(dir)) return found;
    for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
        const p = path.join(dir, e.name);
        if (e.isDirectory()) walk(p, found);
        else found.push(p);
    }
    return found;
}

function main(): number {
    const out = process.argv[2]!;
    const result = spawnSync(process.execPath, [CONVERTER, ...process.argv.slice(3)], { maxBuffer: 1 << 30 });
    process.stdout.write(result.stdout.toString("utf-8").replaceAll(out, "{out}"));
    process.stderr.write(result.stderr);
    for (const file of walk(out, [])) {
        if (file.toLowerCase().endsWith(".qb")) {
            fs.writeFileSync(`${file}.txt`, qbNorm(fs.readFileSync(file)));
            fs.rmSync(file);
        }
    }
    return result.status ?? 1;
}

process.exitCode = main();
