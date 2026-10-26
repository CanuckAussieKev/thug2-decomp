// NOT ORIGINAL - finds code that data points to (vtables, callback tables) but Ghidra never made into a function:
// 16-byte aligned, preceded by CC padding, inside game code, not inside any known function.
//   node tools/find_missed_functions.ts <reference build exe> data/derived/functions.json data/derived/missed_functions.txt

import * as fs from "node:fs";
import { isMain, print, runMain, writeTextPy } from "./pipeline/lib/py.ts";

interface Fn {
    addr: number;
    end: number;
}

// bisect.bisect_right(sorted, v)
export function bisectRight(sorted: readonly number[], v: number): number {
    let lo = 0;
    let hi = sorted.length;
    while (lo < hi) {
        const mid = (lo + hi) >> 1;
        if (v < sorted[mid]!) hi = mid;
        else lo = mid + 1;
    }
    return lo;
}

function main(): number {
    const [exePath, funcsPath, outPath] = process.argv.slice(2);
    if (exePath === undefined || funcsPath === undefined || outPath === undefined) {
        throw new RangeError("usage: find_missed_functions.ts <exe> <functions.json> <out.txt>");
    }
    const d = fs.readFileSync(exePath);
    const F = JSON.parse(fs.readFileSync(funcsPath, "utf-8")) as Fn[];
    const entries = new Set(F.map((f) => f.addr));
    const spans = F.map((f): [number, number] => [f.addr, f.end]).sort((a, b) => a[0] - b[0] || a[1] - b[1]);
    const starts = spans.map(([s]) => s);
    const insideFunction = (v: number): boolean => {
        const i = bisectRight(starts, v) - 1;
        return i >= 0 && spans[i]![0] < v && v <= spans[i]![1];
    };

    const pe = d.readUInt32LE(0x3c);
    const o = pe + 24 + d.readUInt16LE(pe + 20);
    const found = new Set<number>();
    for (let k = 0; k < d.readUInt16LE(pe + 6); k++) {
        const at = o + k * 40;
        const name = d
            .subarray(at, at + 8)
            .toString("latin1")
            .replace(/\0+$/, "");
        const vsz = d.readUInt32LE(at + 8);
        const rsz = d.readUInt32LE(at + 16);
        const rp = d.readUInt32LE(at + 20);
        if (name !== ".rdata" && name !== ".data") continue;
        for (let off = 0; off < Math.min(vsz, rsz) - 3; off += 4) {
            const v = d.readUInt32LE(rp + off);
            if (
                v >= 0x401000 &&
                v < 0x5f8130 &&
                !entries.has(v) &&
                v % 16 === 0 &&
                d[v - 0x400000 - 1] === 0xcc &&
                !insideFunction(v)
            ) {
                found.add(v);
            }
        }
    }
    const sorted = [...found].sort((a, b) => a - b);
    // (text mode, as the Python wrote it: "\r\n" line ends)
    writeTextPy(outPath, sorted.map((v) => `0x${v.toString(16)}\n`).join(""));
    print(`${found.size} missed functions -> ${outPath}`);
    return 0;
}

if (isMain(import.meta.url)) await runMain(() => main());
