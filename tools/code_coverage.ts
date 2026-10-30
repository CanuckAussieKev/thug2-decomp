// NOT ORIGINAL - completeness by bytes: every byte of the reference build's game code is accounted for, and every
// reference into game code lands on something we have.
//   node tools/code_coverage.ts [--exe <reference build>] [--verbose]
// A byte is accounted for when it lies inside a claimed function's extent (a REPLACE line under src/game, an entry
// of data/library_functions.csv or data/not_functions.csv), or is padding (CC / 90), a switch's jump table (code
// addresses, then its byte index table), the 5-byte filler some functions are followed by, or a range reviewed in
// data/code_coverage_known.csv. Extents come from data/derived/functions.json, clipped at the next known start (a
// Ghidra "function" with a chunk far away would otherwise hide everything in between); a claimed start with no
// Ghidra function runs to its first RET followed by padding.
// References checked: CALL / PUSH imm / MOV reg, imm in game code and in the static initialisers (0x640000-0x645000),
// and aligned words in the data sections - each target must be a claimed start, inside covered code, padding or a
// reviewed range. Exits 1 when anything is left over (the function Ghidra never made, a lost REPLACE line, a
// boundary mistake).

import * as fs from "node:fs";
import { REFERENCE_EXE } from "./pipeline/lib/decomp.ts";
import { globRecursive, readCsvDicts } from "./pipeline/lib/files.ts";
import { isMain, print, runMain } from "./pipeline/lib/py.ts";

// The game's code (docs/library_ranges.md); the rest of .text is libraries, import thunks and compiler code.
export const GAME_RANGES: readonly (readonly [number, number])[] = [
    [0x401000, 0x5beaae],
    [0x5f2e00, 0x5f6640],
];
const INITIALISERS: readonly [number, number] = [0x640000, 0x645000];
// The 5 bytes that follow many functions (linker filler, not code).
const FILLER = [0x52, 0x4c, 0x44, 0x21, 0x00];

export const inGame = (a: number): boolean => GAME_RANGES.some(([lo, hi]) => a >= lo && a < hi);
const isPad = (b: number | undefined): boolean => b === 0xcc || b === 0x90;
const hex = (n: number): string => `0x${n.toString(16).toUpperCase().padStart(8, "0")}`;

export class Image {
    private readonly bytes: Buffer;
    private readonly sections: { name: string; va: number; raw: number; size: number }[] = [];
    constructor(bytes: Buffer) {
        this.bytes = bytes;
        const pe = bytes.readUInt32LE(0x3c);
        const base = bytes.readUInt32LE(pe + 24 + 28);
        const first = pe + 24 + bytes.readUInt16LE(pe + 20);
        for (let i = 0; i < bytes.readUInt16LE(pe + 6); i++) {
            const at = first + i * 40;
            this.sections.push({
                name: bytes
                    .subarray(at, at + 8)
                    .toString("latin1")
                    .replace(/\0+$/, ""),
                va: base + bytes.readUInt32LE(at + 12),
                size: Math.min(bytes.readUInt32LE(at + 8), bytes.readUInt32LE(at + 16)),
                raw: bytes.readUInt32LE(at + 20),
            });
        }
    }
    byte(va: number): number | undefined {
        for (const s of this.sections) if (va >= s.va && va < s.va + s.size) return this.bytes[s.raw + va - s.va];
        return undefined;
    }
    dword(va: number): number {
        let v = 0;
        for (let i = 3; i >= 0; i--) v = v * 256 + (this.byte(va + i) ?? 0);
        return v;
    }
    dataSections(): { va: number; size: number }[] {
        return this.sections.filter((s) => s.name !== ".text").map((s) => ({ va: s.va, size: s.size }));
    }
}

interface Fn {
    addr: number;
    end: number;
}
export interface Run {
    from: number;
    to: number;
}

function replacedAddresses(): Set<number> {
    const found = new Set<number>();
    for (const file of [".cpp", ".h"].flatMap((suffix) => globRecursive("src/game", suffix))) {
        const text = fs.readFileSync(file, "utf-8");
        for (const m of text.matchAll(/REPLACE(?:_SHADOW|_UNCHECKED|_SOURCE_ONLY)?\(0x([0-9A-Fa-f]{8})/g))
            found.add(parseInt(m[1]!, 16));
    }
    return found;
}
const readCsv = (file: string): Record<string, string>[] => readCsvDicts(fs.readFileSync(file, "utf-8"));
const csvAddresses = (file: string): number[] => readCsv(file).map((r) => parseInt(r["address"]!, 16));

// Reviewed leftovers: an address (one run starting there) or a from-to range, and why it isn't a function.
function knownRanges(file: string): Run[] {
    return readCsv(file).map((r) => {
        const [from, to] = r["range"]!.split("-").map((x) => parseInt(x, 16));
        return { from: from!, to: to ?? from! + 1 };
    });
}

// A jump table (code addresses) then its byte index table, after any alignment instruction.
export function isJumpTable(img: Image, from: number, to: number): boolean {
    let a = from;
    while (a < to && isPad(img.byte(a))) a++;
    const align: number[][] = [
        [0x8d, 0x49, 0x00],
        [0x8b, 0xff],
        [0x8d, 0xa4, 0x24],
        [0x8d, 0x64, 0x24],
    ];
    for (const seq of align) {
        if (seq.every((b, i) => img.byte(a + i) === b)) {
            a += seq[0] === 0x8d && seq[1] === 0xa4 ? 7 : seq[0] === 0x8d && seq[1] === 0x64 ? 4 : seq.length;
            break;
        }
    }
    // Pointer blocks and their index bytes (each below the block's entry count), as many as there are; the run may
    // also start in the middle of a pointer block whose start is covered.
    let entries = 0;
    let any = false;
    for (let shift = 0; shift < 4 && !any; shift++) {
        let p = a + shift;
        let n = 0;
        while (p + 4 <= to && inGame(img.dword(p))) {
            p += 4;
            n++;
        }
        if (n >= (shift ? 2 : 1)) {
            a = p;
            entries = n;
            any = true;
        }
    }
    if (!any) return false;
    while (a < to) {
        if (a + 4 <= to && inGame(img.dword(a))) {
            let n = 0;
            while (a + 4 <= to && inGame(img.dword(a))) {
                a += 4;
                n++;
            }
            entries = n;
        } else if (img.byte(a) === 0x8b && img.byte(a + 1) === 0xff && inGame(img.dword(a + 2))) {
            a += 2; // (alignment before the next block)
        } else if (
            img.byte(a) === 0x8d &&
            img.byte(a + 1) === 0x49 &&
            img.byte(a + 2) === 0 &&
            inGame(img.dword(a + 3))
        ) {
            a += 3;
        } else if (isPad(img.byte(a)) || (img.byte(a) ?? 0xff) < Math.max(entries, 1)) {
            a++;
        } else {
            return false;
        }
    }
    return true;
}

function main(): number {
    const args = process.argv.slice(2);
    const exeAt = args.indexOf("--exe");
    const exe = exeAt >= 0 ? args[exeAt + 1]! : REFERENCE_EXE;
    const verbose = args.includes("--verbose");
    if (!exe) throw new RangeError("no reference build: pass --exe or set referenceExe in local.json");
    const img = new Image(fs.readFileSync(exe));

    const replaced = replacedAddresses();
    const claimed = new Set<number>([
        ...replaced,
        ...csvAddresses("data/library_functions.csv"),
        ...csvAddresses("data/not_functions.csv"),
    ]);
    const funcs = JSON.parse(fs.readFileSync("data/derived/functions.json", "utf-8")) as Fn[];
    const byAddr = new Map(funcs.map((f) => [f.addr, f]));
    const starts = [...new Set([...funcs.map((f) => f.addr), ...claimed])].sort((a, b) => a - b);
    const nextStart = (a: number): number => {
        let lo = 0;
        let hi = starts.length;
        while (lo < hi) {
            const mid = (lo + hi) >> 1;
            if (starts[mid]! <= a) lo = mid + 1;
            else hi = mid;
        }
        return starts[lo] ?? a + 1;
    };

    const LO = GAME_RANGES[0]![0];
    const HI = GAME_RANGES[GAME_RANGES.length - 1]![1];
    const covered = new Uint8Array(HI - LO);
    const mark = (from: number, to: number): void => {
        for (let a = Math.max(from, LO); a < Math.min(to, HI); a++) covered[a - LO] = 1;
    };
    const isCovered = (a: number): boolean => a >= LO && a < HI && covered[a - LO] === 1;
    let guessed = 0;
    for (const a of claimed) {
        if (!inGame(a)) continue;
        const f = byAddr.get(a);
        const next = nextStart(a);
        if (f) {
            mark(a, Math.min(f.end + 1, next));
            continue;
        }
        guessed++;
        let end = next;
        for (let x = a; x < next; x++) {
            const b = img.byte(x);
            const after = b === 0xc3 ? x + 1 : b === 0xc2 ? x + 3 : -1;
            if (after > 0 && isPad(img.byte(after))) {
                end = after;
                break;
            }
        }
        mark(a, end);
    }

    // The uncovered runs, classified.
    const known = knownRanges("data/code_coverage_known.csv");
    const inKnown = (a: number): boolean => known.some((k) => a >= k.from && a < k.to);
    const explained: Run[] = [];
    const unexplained: Run[] = [];
    for (const [lo, hi] of GAME_RANGES) {
        let start = -1;
        for (let a = lo; a <= hi; a++) {
            const c = a === hi || covered[a - LO] === 1;
            if (!c && start < 0) start = a;
            if (c && start >= 0) {
                const run = { from: start, to: a };
                let real = 0;
                for (let x = start; x < a; x++) if (!isPad(img.byte(x))) real++;
                let filler = 0;
                for (let x = start; x + 5 <= a; x++) if (FILLER.every((b, i) => img.byte(x + i) === b)) filler = 5;
                const ok =
                    real === 0 ||
                    (real === filler && real === 5) ||
                    isJumpTable(img, start, a) ||
                    inKnown(start) ||
                    known.some((k) => start >= k.from && a <= k.to);
                (ok ? explained : unexplained).push(run);
                start = -1;
            }
        }
    }
    const inRun = (runs: Run[], a: number): boolean => runs.some((r) => a >= r.from && a < r.to);
    // A reference is a problem only when it lands in an unexplained run (or on real code nothing covers).
    const suspicious = (t: number): boolean =>
        inGame(t) && !claimed.has(t) && !isCovered(t) && !isPad(img.byte(t)) && !inRun(explained, t);

    const refs = new Map<number, string[]>();
    const add = (target: number, what: string): void => {
        if (!refs.has(target)) refs.set(target, []);
        refs.get(target)!.push(what);
    };
    const scanCode = (lo: number, hi: number, requireCovered: boolean): void => {
        for (let a = lo; a < hi - 5; a++) {
            const op = img.byte(a)!;
            if (!(op === 0x68 || (op >= 0xb8 && op <= 0xbf) || op === 0xe8 || op === 0xe9)) continue;
            if (requireCovered && !isCovered(a)) continue;
            if (op === 0xe9 && requireCovered) continue; // (jumps inside game code land mid-function all the time)
            let t = img.dword(a + 1);
            if (op >= 0xe8) t = (a + 5 + (t | 0)) >>> 0;
            if (suspicious(t)) add(t, `${op === 0xe8 ? "call" : op === 0xe9 ? "jmp" : "imm"} at ${hex(a)}`);
        }
    };
    for (const [lo, hi] of GAME_RANGES) scanCode(lo, hi, true);
    scanCode(INITIALISERS[0], INITIALISERS[1], false);
    for (const s of img.dataSections()) {
        for (let off = 0; off + 4 <= s.size; off += 4) {
            const t = img.dword(s.va + off);
            if (suspicious(t)) add(t, `data at ${hex(s.va + off)}`);
        }
    }

    const total = GAME_RANGES.reduce((n, [lo, hi]) => n + hi - lo, 0);
    let coveredBytes = 0;
    for (const v of covered) coveredBytes += v;
    const explainedBytes = explained.reduce((n, r) => n + r.to - r.from, 0);
    print(
        `game code: ${total} bytes; inside claimed functions ${coveredBytes}, padding / tables / filler / reviewed ` +
            `${explainedBytes}, unexplained ${unexplained.reduce((n, r) => n + r.to - r.from, 0)}`,
    );
    print(
        `claimed starts: ${claimed.size} (${replaced.size} REPLACE lines; ${guessed} with no Ghidra function, ` +
            "extent guessed)",
    );
    for (const r of unexplained) {
        const bytes: string[] = [];
        for (let x = r.from; x < Math.min(r.to, r.from + 16); x++)
            bytes.push(img.byte(x)!.toString(16).padStart(2, "0"));
        print(`  unexplained ${hex(r.from)}-${hex(r.to)} (${r.to - r.from} bytes): ${bytes.join(" ")}`);
    }
    for (const [t, from] of [...refs].sort((a, b) => a[0] - b[0]))
        print(`  referenced, not ours: ${hex(t)} (${from.slice(0, 3).join(", ")}${from.length > 3 ? ", ..." : ""})`);
    if (verbose) for (const r of explained) print(`  explained ${hex(r.from)}-${hex(r.to)}`);
    const clean = unexplained.length === 0 && refs.size === 0;
    print(clean ? "Every byte and reference accounted for." : "Left over: see above.");
    return clean ? 0 : 1;
}

if (isMain(import.meta.url)) await runMain(() => main());
