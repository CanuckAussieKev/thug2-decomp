// NOT ORIGINAL - extracts the script runtime's CFunction table (0x67F3A0: 1254 {name pointer, function pointer}
// pairs) from the reference build into a CSV of address,script_name (data/derived/cfuncs.csv), and prints how many functions
// several names share.
//
//   node tools/extract_cfuncs.ts <reference build exe> data/derived/cfuncs.csv

import * as fs from "node:fs";
import { decodeLatin1, isMain, print, pyTuple, runMain, writeTextPy } from "./pipeline/lib/py.ts";
import { unpackNumbers } from "./pipeline/lib/struct.ts";

const TABLE = 0x67f3a0;
const ENTRIES = 0x4e6;

interface Section {
    va: number;
    rsize: number;
    raw: number;
}

export function sections(d: Buffer): Section[] {
    const pe = d.readUInt32LE(0x3c);
    const o = pe + 24 + d.readUInt16LE(pe + 20);
    const out: Section[] = [];
    for (let i = 0; i < d.readUInt16LE(pe + 6); i++) {
        const [, va, rsize, raw] = unpackNumbers("<IIII", d, o + i * 40 + 8);
        out.push({ va: va!, rsize: rsize!, raw: raw! });
    }
    return out;
}

function main(): number {
    const [exe, csvPath] = process.argv.slice(2);
    if (exe === undefined || csvPath === undefined) throw new RangeError("usage: extract_cfuncs.ts <exe> <out.csv>");
    const d = fs.readFileSync(exe);
    const secs = sections(d);
    const off = (va: number): number => {
        const rva = va - 0x400000;
        for (const s of secs) if (s.va <= rva && rva < s.va + s.rsize) return s.raw + rva - s.va;
        throw new TypeError(`0x${va.toString(16)} is in no section`);
    };
    const cstr = (va: number): string => {
        const f = off(va);
        const end = d.indexOf(0, f);
        if (end < 0) throw new RangeError("subsection not found");
        return decodeLatin1(d.subarray(f, end));
    };
    const rows: [number, string][] = [];
    for (let i = 0; i < ENTRIES; i++) {
        const base = off(TABLE) + i * 8;
        const np = d.readUInt32LE(base);
        const fp = d.readUInt32LE(base + 4);
        rows.push(pyTuple([fp, cstr(np)]));
    }
    // (text mode, as the Python wrote it: "\r\n" line ends)
    writeTextPy(
        csvPath,
        `address,script_name\n${rows.map(([fp, n]) => `0x${fp.toString(16).padStart(8, "0")},${n}\n`).join("")}`,
    );
    // collections.Counter: counts in first-seen order; most_common keeps that order among equals
    const counts = new Map<number, number>();
    for (const [fp] of rows) counts.set(fp, (counts.get(fp) ?? 0) + 1);
    const common = [...counts.entries()].sort((a, b) => b[1] - a[1]).slice(0, 5);
    print("entries:", rows.length, "unique funcs:", counts.size);
    print(
        "shared by several names:",
        common
            .filter(([, k]) => k > 1)
            .map(([a]) => pyTuple([`0x${a.toString(16)}`, rows.filter(([f]) => f === a).map(([, n]) => n)])),
    );
    print("sample:", rows.slice(0, 8));
    return 0;
}

if (isMain(import.meta.url)) await runMain(() => main());
