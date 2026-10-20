// NOT ORIGINAL - the TypeScript half of the level converters' parity runs (tools/parity/<converter>/cases.ts): runs a
// converter (node <script> <args...>, its exit code passed on, its stdout with the <out> folder's path written as
// "<out>" - the two sides write to different folders), then rewrites every .qb it wrote under
// <out> with its 0x2B name entries sorted - ns.exe (Go) writes them in map order, different on every run - so the two
// languages' outputs compare byte for byte. Its Python half, qb_norm.py (the same
// normalisation), went with the last Python twins (stage 3; it is in commit 57d7b5e).
//
//   node tools/parity/converters/qb_norm.ts <out> <script> [args...]

import { spawnSync } from "node:child_process";
import * as fs from "node:fs";
import * as path from "node:path";
import * as qb from "../../level_port/qb.ts";
import { isMain } from "../../lib/py.ts";

export function normalise(data: Buffer): Buffer {
    const toks = [...qb.tokens(data, true)];
    if (!toks.length) return data;
    // each token's raw bytes: up to the next token; the last runs to the reader's stop (the end token is one byte)
    const raw = toks.map(([off, t], k): [number, Buffer, number] => {
        const next = toks[k + 1];
        const end = next !== undefined ? next[0] : t === 0 ? off + 1 : data.length;
        return [t, data.subarray(off, end), off];
    });
    const names = raw
        .filter(([t]) => t === 0x2b)
        .map(([, r]) => r)
        .sort(Buffer.compare);
    const first = raw.find(([t]) => t === 0x2b)?.[2];
    if (first === undefined) return data;
    const rest = raw.filter(([t, , off]) => off >= first && t !== 0x2b).map(([, r]) => r);
    const last = raw[raw.length - 1]!;
    const end = last[2] + last[1].length;
    return Buffer.concat([data.subarray(0, first), ...names, ...rest, data.subarray(end)]);
}

function qbFiles(dir: string): string[] {
    const out: string[] = [];
    if (!fs.existsSync(dir)) return out;
    for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
        const p = path.join(dir, e.name);
        if (e.isDirectory()) out.push(...qbFiles(p));
        else if (e.name.toLowerCase().endsWith(".qb")) out.push(p);
    }
    return out;
}

function main(): number {
    const [out, script, ...args] = process.argv.slice(2);
    if (out === undefined || script === undefined) {
        process.stderr.write("usage: node tools/parity/converters/qb_norm.ts <out> <script> [args...]\n");
        return 2;
    }
    const result = spawnSync(process.execPath, [script, ...args], {
        stdio: ["inherit", "pipe", "inherit"],
        maxBuffer: 1 << 30,
    });
    process.stdout.write(result.stdout.toString("utf-8").replaceAll(out, "<out>"));
    for (const file of qbFiles(out)) fs.writeFileSync(file, normalise(fs.readFileSync(file)));
    return result.status ?? 1;
}

// (imported by tools/parity/menu_level/wrap.ts for normalise: main only when run)
if (isMain(import.meta.url)) process.exitCode = main();
