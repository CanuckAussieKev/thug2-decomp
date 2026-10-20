// NOT ORIGINAL - splits game code into likely translation units (contiguous address ranges) and labels them.
// MSVC links each .cpp's functions contiguously, and also keeps each .cpp's constants/strings together,
// so we cut between neighbouring functions only when nothing ties them together:
//   - direct calls between nearby functions
//   - membership of the same small pointer table (vtables, callback tables), found by scanning the exe
//   - references to nearby data (each file's strings/constants sit together in .rdata/.data)
// The Rich header says ~483 C++ objects were linked, which is a sanity check for the unit count.
//   node tools/map_units.ts C:\Games\THUG2_PM_BAK\THUG2PM.exe data/derived/functions.json data/derived/units.csv

import * as fs from "node:fs";
import { csvRow } from "./lib/files.ts";
import { isMain, print, pyFloatRepr, runMain } from "./lib/py.ts";

const GAME_LO = 0x401000;
const GAME_HI = 0x5f8130;
const CFUNC_TABLE = [0x67f3a0, 0x67f3a0 + 0x4e6 * 8] as const;
const WINDOW = 6; // how far apart two functions can be and still tie a gap
const MAX_TABLE = 80; // pointer runs longer than this are global tables, not one class
const DATA_NEAR = 0x300; // data references this close count as "same file"
const COMMON_DATA = 12; // data used by more functions than this is shared globals, ignored
const LOCAL_SPAN = 60; // data counts as a file's own if all its users are within this many functions
const MIN_FUNCS = 3; // units smaller than this are merged into the previous one
const DATA_WINDOW = 8; // functions on each side of a gap whose data is compared
const SECTIONS = [
    [0x645000, 0x67c000],
    [0x67c000, 0x7de000],
] as const;

interface Fn {
    addr: number;
    end: number;
    size: number;
    name: string;
    callees: number[];
    data_refs: number[];
    strings: string[];
    imports: string[];
}

const STOP = new Set(
    (
        "the a an of to in is for and or not on with from at by be this that error out memory unknown " +
        "invalid failed can't couldn't get set"
    ).split(" "),
);

// collections.Counter(...).most_common(n): by count, first seen first among equals
function mostCommon<T>(items: Iterable<T>, n: number): [T, number][] {
    const counts = new Map<T, number>();
    for (const x of items) counts.set(x, (counts.get(x) ?? 0) + 1);
    return [...counts.entries()].sort((a, b) => b[1] - a[1]).slice(0, n);
}

const words = (text: string, re: RegExp): string[] => [...text.matchAll(re)].map((m) => m[0].toLowerCase());

function tokens(f: Fn): string[] {
    const out: string[] = [];
    let n = f.name;
    if (n.startsWith("CFunc_")) {
        n = n.slice(6);
        if (n.includes("_")) {
            const first = n.split("_")[0]!.toLowerCase();
            out.push(first, first, first);
        }
        out.push(...words(n, /[A-Z][a-z]+|[A-Z]+(?![a-z])/g));
    } else if (!n.startsWith("FUN_") && !n.startsWith("thunk_")) {
        out.push(...words(n, /[A-Z][a-z]+|[A-Z]+(?![a-z])|[a-z]+/g));
    }
    for (const s of f.strings) out.push(...words(s, /[A-Za-z]{3,}/g));
    for (const x of f.imports) {
        const part = x.split("!")[1];
        if (part === undefined) throw new RangeError("list index out of range");
        out.push(part.toLowerCase());
    }
    return out.filter((t) => !STOP.has(t) && t.length > 2);
}

// statistics.median of ints: the middle one, or the mean of the middle two (a float: "4.0", "3.5")
function median(values: readonly number[]): string {
    const s = [...values].sort((a, b) => a - b);
    const n = s.length;
    if (n === 0) throw new RangeError("no median for empty data");
    if (n % 2 === 1) return String(s[(n - 1) / 2]);
    return pyFloatRepr((s[n / 2 - 1]! + s[n / 2]!) / 2);
}

function main(): number {
    const [exePath, funcsPath, outPath] = process.argv.slice(2);
    if (exePath === undefined || funcsPath === undefined || outPath === undefined) {
        throw new RangeError("usage: map_units.ts <exe> <functions.json> <units.csv>");
    }
    const allf = JSON.parse(fs.readFileSync(funcsPath, "utf-8")) as Fn[];
    const funcs = allf.filter((f) => GAME_LO <= f.addr && f.addr < GAME_HI).sort((a, b) => a.addr - b.addr);
    const index = new Map<number, number>();
    funcs.forEach((f, i) => {
        index.set(f.addr, i);
    });

    // --- pointer tables: runs of consecutive dwords in .rdata/.data that are function entry points
    const d = fs.readFileSync(exePath);
    const pe = d.readUInt32LE(0x3c);
    const o = pe + 24 + d.readUInt16LE(pe + 20);
    const tables: number[][] = [];
    for (let k = 0; k < d.readUInt16LE(pe + 6); k++) {
        const at = o + k * 40;
        const name = d
            .subarray(at, at + 8)
            .toString("latin1")
            .replace(/\0+$/, "");
        const vsz = d.readUInt32LE(at + 8);
        const va = d.readUInt32LE(at + 12);
        const rsz = d.readUInt32LE(at + 16);
        const rp = d.readUInt32LE(at + 20);
        if (name !== ".rdata" && name !== ".data") continue;
        let run: number[] = [];
        let start = 0; // (0: none - Python's None; no table starts at address 0)
        for (let off = 0; off < Math.min(vsz, rsz) - 3; off += 4) {
            const v = d.readUInt32LE(rp + off);
            const addr = 0x400000 + va + off;
            const i = index.get(v);
            if (i !== undefined) {
                run.push(i);
                start = start || addr;
            } else {
                if (
                    2 <= run.length &&
                    run.length <= MAX_TABLE &&
                    !(CFUNC_TABLE[0] <= start && start < CFUNC_TABLE[1])
                ) {
                    tables.push(run);
                }
                run = [];
                start = 0;
            }
        }
    }

    // --- evidence that the gap after function i lies inside one unit
    const tie = new Array<number>(funcs.length).fill(0);
    const link = (x: number, y: number): void => {
        const [a, b] = x <= y ? [x, y] : [y, x];
        if (0 < b - a && b - a <= WINDOW) for (let g = a; g < b; g++) tie[g]!++;
    };
    funcs.forEach((f, i) => {
        for (const c of f.callees) {
            const j = index.get(c);
            if (j !== undefined) link(i, j);
        }
    });
    for (const t of tables) {
        const s = [...new Set(t)].sort((a, b) => a - b);
        for (let k = 0; k + 1 < s.length; k++) link(s[k]!, s[k + 1]!);
    }

    const dataUsers = new Map<number, number>();
    for (const f of funcs) for (const a of f.data_refs) dataUsers.set(a, (dataUsers.get(a) ?? 0) + 1);
    const anchors = funcs.map((f) => f.data_refs.filter((a) => dataUsers.get(a)! <= COMMON_DATA).sort((a, b) => a - b));
    for (let i = 0; i < funcs.length; i++) {
        for (let j = i + 1; j < Math.min(i + WINDOW + 1, funcs.length); j++) {
            if (anchors[i]!.some((a) => anchors[j]!.some((b) => Math.abs(a - b) <= DATA_NEAR))) link(i, j);
        }
    }

    // --- evidence that the gap after function i is a file boundary: both sides reference local data
    // (used only by nearby functions) and the data ranges don't interleave. Each .obj's data is contiguous and
    // linked in the same order as its code, so data addresses step upwards at a boundary.
    const users = new Map<number, { lo: number; hi: number }>();
    funcs.forEach((f, i) => {
        for (const a of f.data_refs) {
            const u = users.get(a);
            if (u) {
                u.lo = Math.min(u.lo, i);
                u.hi = Math.max(u.hi, i);
            } else users.set(a, { lo: i, hi: i });
        }
    });
    const local = funcs.map((f) =>
        SECTIONS.map(([lo, hi]) =>
            f.data_refs.filter((a) => {
                const u = users.get(a)!;
                return lo <= a && a < hi && u.hi - u.lo <= LOCAL_SPAN;
            }),
        ),
    );

    // True if some section shows left data strictly below right data around gap g, none shows overlap.
    const separable = (g: number): boolean => {
        let verdict: boolean | undefined;
        for (let s = 0; s < SECTIONS.length; s++) {
            const left: number[] = [];
            for (let i = Math.max(0, g - DATA_WINDOW + 1); i < g + 1; i++) left.push(...local[i]![s]!);
            const right: number[] = [];
            for (let i = g + 1; i < Math.min(funcs.length, g + 1 + DATA_WINDOW); i++) right.push(...local[i]![s]!);
            if (left.length && right.length) {
                if (Math.max(...left) < Math.min(...right)) verdict = verdict ?? true;
                else return false;
            }
        }
        return Boolean(verdict);
    };

    let cuts = [0];
    for (let g = 0; g < funcs.length - 1; g++) if (tie[g] === 0 && separable(g)) cuts.push(g + 1);
    cuts.push(funcs.length);
    // A unit of one or two functions is almost always a helper split off by its own string/constant;
    // fold it into the previous unit.
    const merged = [cuts[0]!];
    for (const c of cuts.slice(1))
        if (c - merged[merged.length - 1]! >= MIN_FUNCS || c === cuts[cuts.length - 1]) merged.push(c);
    cuts = merged;

    // --- labels
    let csv = csvRow(["unit", "start", "end", "funcs", "bytes", "named", "keywords", "examples"]);
    for (let u = 0; u + 1 < cuts.length; u++) {
        const seg = funcs.slice(cuts[u]!, cuts[u + 1]!);
        const kw = mostCommon(
            seg.flatMap((f) => tokens(f)),
            6,
        );
        const named = seg.filter((f) => !f.name.startsWith("FUN_") && !f.name.startsWith("thunk_")).map((f) => f.name);
        csv += csvRow([
            u,
            `0x${seg[0]!.addr.toString(16)}`,
            `0x${seg[seg.length - 1]!.end.toString(16)}`,
            seg.length,
            seg.reduce((n, f) => n + f.size, 0),
            named.length,
            kw.map(([k]) => k).join(" "),
            named.slice(0, 4).join("; "),
        ]);
    }
    fs.writeFileSync(outPath, csv, "utf-8");

    const sizes: number[] = [];
    for (let k = 0; k + 1 < cuts.length; k++) sizes.push(cuts[k + 1]! - cuts[k]!);
    print(
        `${tables.length} pointer tables; ${sizes.length} units from ${funcs.length} functions; ` +
            `median ${median(sizes)} funcs/unit, max ${Math.max(...sizes)}, singletons ${sizes.filter((s) => s === 1).length}`,
    );
    return 0;
}

if (isMain(import.meta.url)) await runMain(() => main());
