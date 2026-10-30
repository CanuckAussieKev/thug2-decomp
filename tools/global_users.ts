// NOT ORIGINAL - standalone step 3: which GameVariable globals can become real variables now. A global can move out of
// the exe once nothing that still runs the exe's own code touches it and no data in the exe points into it:
//   - code: the libraries (D3DX9, GameSpy, the C runtime and its start-up, the C++ library functions inside the game's
//     ranges - data/library_functions.csv), the static initialisers / unwind funclets, the data table inside .text, and
//     the original bodies of REPLACE_SOURCE_ONLY functions (not hooked: the exe's own callers still run them);
//   - data: an aligned word in .rdata / .data / .tls holding an address inside the global (a table of pointers).
// A global's extent runs from its address to the next address anything refers to (an absolute operand anywhere in
// .text, a data pointer, another GameVariable) - the exe's layout gives no sizes, so this is an upper bound.
//   node tools/global_users.ts [--exe <reference build>] [--out <csv>]
// The csv (default out/globals.csv): address, section, extent, uses in src/game, files, verdict, blockers.

import * as fs from "node:fs";
import { Image, inGame } from "./code_coverage.ts";
import { REFERENCE_EXE } from "./pipeline/lib/decomp.ts";
import { globRecursive, readCsvDicts } from "./pipeline/lib/files.ts";
import { isMain, print, runMain } from "./pipeline/lib/py.ts";

const DATA_LO = 0x645000;
const DATA_HI = 0x7e0000;
// .text that is not game code and still runs (or is data): docs/library_ranges.md.
const RUNNING_RANGES: readonly (readonly [number, number, string])[] = [
    [0x5beaae, 0x5beb1a, "import thunks"],
    [0x5beb1a, 0x5f2e00, "D3DX9"],
    [0x5f6640, 0x5f8130, "data table in .text"],
    [0x5f8130, 0x5f8160, "import thunks"],
    [0x5f8160, 0x622e80, "GameSpy"],
    [0x622e80, 0x622f80, "STL string helpers"],
    [0x622f80, 0x640000, "C runtime"],
    [0x640000, 0x645000, "static initialisers / unwind"],
];

export function section(a: number): string {
    if (a < 0x67c000) return ".rdata";
    if (a < 0x6f0000) return ".data";
    return ".bss";
}

interface Fn {
    addr: number;
    end: number;
}

const hex = (n: number): string => `0x${n.toString(16).toUpperCase().padStart(8, "0")}`;

function main(): number {
    const args = process.argv.slice(2);
    const at = (flag: string): string | undefined => {
        const i = args.indexOf(flag);
        return i >= 0 ? args[i + 1] : undefined;
    };
    const exe = at("--exe") ?? REFERENCE_EXE;
    const out = at("--out") ?? "out/globals.csv";
    const img = new Image(fs.readFileSync(exe));
    const funcs = JSON.parse(fs.readFileSync("data/derived/functions.json", "utf-8")) as Fn[];
    const byAddr = new Map(funcs.map((f) => [f.addr, f]));

    // The globals our source names, with their uses and files.
    const uses = new Map<number, { n: number; files: Set<string> }>();
    const sourceOnly = new Set<number>();
    for (const file of [".cpp", ".h"].flatMap((s) => globRecursive("src/game", s))) {
        const text = fs.readFileSync(file, "utf-8");
        for (const m of text.matchAll(/GameVariable<[^;]*?>\(\s*0x([0-9A-Fa-f]{6,8})/g)) {
            const a = parseInt(m[1]!, 16);
            if (!uses.has(a)) uses.set(a, { n: 0, files: new Set() });
            const u = uses.get(a)!;
            u.n++;
            u.files.add(file.replaceAll("\\", "/").replace(/^src\/game\//, ""));
        }
        for (const m of text.matchAll(/REPLACE_SOURCE_ONLY\(0x([0-9A-Fa-f]{8})/g)) sourceOnly.add(parseInt(m[1]!, 16));
    }

    // Code that still runs the exe's own instructions: [from, to, what].
    const running: [number, number, string][] = RUNNING_RANGES.map(([a, b, w]) => [a, b, w]);
    const extentOf = (a: number): number => {
        const f = byAddr.get(a);
        if (f && f.end > a) return f.end + 1;
        for (let x = a; x < a + 0x400; x++) {
            const b = img.byte(x);
            if (b === 0xc3) return x + 1;
            if (b === 0xc2) return x + 3;
        }
        return a + 5;
    };
    for (const r of readCsvDicts(fs.readFileSync("data/library_functions.csv", "utf-8"))) {
        const a = parseInt(r["address"]!, 16);
        if (inGame(a)) running.push([a, extentOf(a), `library ${r["name"]}`]);
    }
    // A source-only function's original body only runs when something still running reaches it: a data word holding its
    // address (a vtable, a table), or a call / jump / pushed address in the code above (our callers call ours).
    const reached = new Set<number>();
    for (const s of img.dataSections())
        for (let off = 0; off + 4 <= s.size; off += 4) reached.add(img.dword(s.va + off));
    for (const [lo, hi] of running) {
        for (let x = lo; x + 5 <= hi; x++) {
            const op = img.byte(x);
            if (op === 0xe8 || op === 0xe9) reached.add((x + 5 + (img.dword(x + 1) | 0)) >>> 0);
            reached.add(img.dword(x));
        }
    }
    for (const a of sourceOnly) if (reached.has(a)) running.push([a, extentOf(a), `source-only ${hex(a)}`]);

    // Every absolute address the exe refers to: operands anywhere in .text (a byte scan - over-counting only makes
    // extents shorter and blockers more), and aligned words in the data sections.
    const boundaries = new Set<number>(uses.keys());
    const codeRefs = new Map<number, string[]>(); // target -> what refers to it (running code only)
    for (const [lo, hi, what] of running) {
        for (let x = lo; x + 4 <= hi; x++) {
            const v = img.dword(x);
            if (v >= DATA_LO && v < DATA_HI) {
                if (!codeRefs.has(v)) codeRefs.set(v, []);
                codeRefs.get(v)!.push(`${what} @${hex(x)}`);
            }
        }
    }
    for (let x = 0x401000; x + 4 <= 0x645000; x++) {
        const v = img.dword(x);
        if (v >= DATA_LO && v < DATA_HI) boundaries.add(v);
    }
    const dataRefs = new Map<number, number[]>(); // target -> data word addresses pointing at it
    for (const s of img.dataSections()) {
        for (let off = 0; off + 4 <= s.size; off += 4) {
            const v = img.dword(s.va + off);
            if (v >= DATA_LO && v < DATA_HI) {
                boundaries.add(v);
                if (!dataRefs.has(v)) dataRefs.set(v, []);
                dataRefs.get(v)!.push(s.va + off);
            }
        }
    }
    const sorted = [...boundaries].sort((a, b) => a - b);
    const nextAfter = (a: number): number => {
        let lo = 0;
        let hi = sorted.length;
        while (lo < hi) {
            const mid = (lo + hi) >> 1;
            if (sorted[mid]! <= a) lo = mid + 1;
            else hi = mid;
        }
        return sorted[lo] ?? a + 4;
    };
    const globalsSorted = [...uses.keys()].sort((a, b) => a - b);
    const ownerOf = (word: number): number | undefined => {
        // the GameVariable whose extent holds this data word, if any
        let best: number | undefined;
        for (const g of globalsSorted) {
            if (g > word) break;
            best = g;
        }
        return best !== undefined && word < nextAfter(best) ? best : undefined;
    };

    const rows: string[] = ["address,section,extent,uses,files,verdict,blockers"];
    const counts = new Map<string, number>();
    const blockerKinds = new Map<string, number>();
    for (const a of globalsSorted) {
        const end = Math.min(nextAfter(a), a + 0x100000);
        const blockers: string[] = [];
        // (.rdata is read-only: a copy of the constant in our code is the same value whoever else reads the exe's;
        // only a data table holding its address ties it to the exe)
        if (section(a) !== ".rdata")
            for (const [t, from] of codeRefs)
                if (t >= a && t < end) blockers.push(...from.map((f) => `${f} -> ${hex(t)}`));
        for (const [t, words] of dataRefs) {
            if (t < a || t >= end) continue;
            for (const w of words) {
                const owner = ownerOf(w);
                blockers.push(`data @${hex(w)}${owner !== undefined ? ` (in global ${hex(owner)})` : ""} -> ${hex(t)}`);
            }
        }
        const verdict = blockers.length ? "blocked" : "movable";
        counts.set(`${verdict} ${section(a)}`, (counts.get(`${verdict} ${section(a)}`) ?? 0) + 1);
        for (const b of blockers) {
            const kind = b.startsWith("data")
                ? b.includes("in global")
                    ? "data inside another global"
                    : "data table"
                : b.replace(/ .*/, "").replace(/^source-only$/, "source-only function");
            const k = b.startsWith("library")
                ? "library function"
                : b.startsWith("source-only")
                  ? "source-only function"
                  : kind;
            const label = RUNNING_RANGES.find(([, , w]) => b.startsWith(w))?.[2] ?? k;
            blockerKinds.set(label, (blockerKinds.get(label) ?? 0) + 1);
        }
        const u = uses.get(a)!;
        rows.push(
            [
                hex(a),
                section(a),
                end - a,
                u.n,
                [...u.files].join(" "),
                verdict,
                `"${blockers.slice(0, 6).join("; ")}${blockers.length > 6 ? `; +${blockers.length - 6}` : ""}"`,
            ].join(","),
        );
    }
    fs.mkdirSync("out", { recursive: true });
    fs.writeFileSync(out, `${rows.join("\n")}\n`);
    print(`${globalsSorted.length} GameVariable addresses in src/game`);
    for (const [k, n] of [...counts].sort()) print(`  ${k.padEnd(16)} ${n}`);
    print("blocker references by kind:");
    for (const [k, n] of [...blockerKinds].sort((x, y) => y[1] - x[1])) print(`  ${k.padEnd(34)} ${n}`);
    print(`-> ${out}`);
    return 0;
}

if (isMain(import.meta.url)) await runMain(() => main());
