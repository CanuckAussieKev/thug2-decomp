// NOT ORIGINAL - audits two builds of the game's exe against this project's rewritten functions: every differing
// byte, which function or data it's in, whether we rewrote that function, and which functions read differing data.
//
//     node tools/game/exe_audit.ts [--a C:\Games\THUG2_PM_BAK\THUG2PM.exe] [--b C:\Games\THUG2\Game\THUG2.exe]
//
// Needs data/derived/functions.json (tools/ghidra/export_functions.py). Rewritten = a REPLACE* line under src/game.

import * as fs from "node:fs";
import * as path from "node:path";
import { fileURLToPath } from "node:url";
import { parseArgs } from "node:util";
import { globRecursive } from "../lib/files.ts";
import { isMain, print, pyExit, pyStrip, readTextPy, runMain } from "../lib/py.ts";

const ROOT = path.dirname(path.dirname(path.dirname(fileURLToPath(import.meta.url))));

interface Section {
    name: string;
    va: number; // (with the image base)
    vsize: number;
    raw: number;
    rsize: number;
}

interface Fn {
    addr: number;
    end: number;
    name: string;
    data_refs?: number[];
}

export function sections(data: Buffer): { base: number; secs: Section[] } {
    const pe = data.readUInt32LE(0x3c);
    const count = data.readUInt16LE(pe + 6);
    const base = data.readUInt32LE(pe + 24 + 28);
    const table = pe + 24 + data.readUInt16LE(pe + 20);
    const secs: Section[] = [];
    for (let i = 0; i < count; i++) {
        const at = table + 40 * i;
        secs.push({
            name: data
                .subarray(at, at + 8)
                .toString("latin1")
                .replace(/\0+$/, ""),
            va: base + data.readUInt32LE(at + 12),
            vsize: data.readUInt32LE(at + 8),
            raw: data.readUInt32LE(at + 20),
            rsize: data.readUInt32LE(at + 16),
        });
    }
    return { base, secs };
}

function toVa(secs: readonly Section[], offset: number): [number | null, string] {
    for (const s of secs) if (s.raw <= offset && offset < s.raw + s.rsize) return [s.va + offset - s.raw, s.name];
    return [null, "header"];
}

function bisectRight(sorted: readonly number[], v: number): number {
    let lo = 0;
    let hi = sorted.length;
    while (lo < hi) {
        const mid = (lo + hi) >> 1;
        if (v < sorted[mid]!) hi = mid;
        else lo = mid + 1;
    }
    return lo;
}

const X8 = (n: number): string => `0x${n.toString(16).toUpperCase().padStart(8, "0")}`;
const hexBytes = (b: Buffer): string => b.toString("hex");

function main(): number {
    const { values } = parseArgs({
        options: {
            a: { type: "string", default: "C:\\Games\\THUG2_PM_BAK\\THUG2PM.exe" },
            b: { type: "string", default: "C:\\Games\\THUG2\\Game\\THUG2.exe" },
        },
    });
    const a = fs.readFileSync(values.a);
    const b = fs.readFileSync(values.b);
    const { base: baseA, secs: secsA } = sections(a);
    const { base: baseB, secs: secsB } = sections(b);
    print(`A ${values.a}: ${a.length} bytes; B ${values.b}: ${b.length} bytes`);
    const sameLayout = a.length === b.length && JSON.stringify(secsA) === JSON.stringify(secsB) && baseA === baseB;
    print(`same size, image base and section table: ${sameLayout ? "True" : "False"}`);
    if (!sameLayout) pyExit("different layouts - this audit only compares same-layout builds");

    // the differing runs
    const runs: [number, number][] = [];
    let start: number | null = null;
    for (let i = 0; i < a.length; i++) {
        if (a[i] !== b[i]) {
            if (start === null) start = i;
        } else if (start !== null) {
            runs.push([start, i]);
            start = null;
        }
    }
    if (start !== null) runs.push([start, a.length]);
    print(`differing bytes: ${runs.reduce((n, [s, e]) => n + e - s, 0)} in ${runs.length} runs`);

    // functions (Ghidra's) and ours
    const funcs = (
        JSON.parse(fs.readFileSync(path.join(ROOT, "data", "derived", "functions.json"), "utf-8")) as Fn[]
    ).sort((x, y) => x.addr - y.addr);
    const starts = funcs.map((f) => f.addr);
    const replaced = new Map<number, [string, string, string]>();
    for (const p of globRecursive(path.join(ROOT, "src", "game"), ".cpp")) {
        const text = readTextPy(p, "replace");
        for (const m of text.matchAll(/\b(REPLACE\w*)\(\s*0x([0-9A-Fa-f]+)\s*,\s*([^;]*?)\);/g)) {
            replaced.set(Number.parseInt(m[2]!, 16), [
                m[1]!,
                [...pyStrip(m[3]!)].slice(0, 70).join(""),
                path.relative(ROOT, p),
            ]);
        }
    }
    print(`rewritten functions (REPLACE* under src/game): ${replaced.size}`);

    const functionAt = (va: number): Fn | null => {
        const i = bisectRight(starts, va) - 1;
        // (Ghidra sometimes makes one huge function over many)
        if (i >= 0 && funcs[i]!.addr <= va && va < funcs[i]!.end) return funcs[i]!;
        return null;
    };

    // 1. every rewritten function's original bytes, A against B
    const byAddr = new Map(funcs.map((f) => [f.addr, f]));
    const differingRewritten: [number, number][] = [];
    const unsized: number[] = [];
    const replacedSorted = [...replaced.keys()].sort((x, y) => x - y);
    for (const addr of replacedSorted) {
        const f = byAddr.get(addr);
        if (!f) {
            unsized.push(addr);
            continue;
        }
        let end = f.end;
        // (a function Ghidra over-extended: up to the next rewritten / known function start)
        const k = bisectRight(starts, addr);
        const nxt = k < starts.length ? starts[k]! : end;
        end = Math.min(end, nxt);
        const lo = addr - baseA; // .text is raw == va - base here (checked below)
        const hi = end - baseA;
        if (!a.subarray(lo, hi).equals(b.subarray(lo, hi))) differingRewritten.push([addr, end]);
    }
    const textSec = secsA.find((s) => s.name === ".text");
    if (!textSec) throw new Error("StopIteration: no .text section");
    if (textSec.va - baseA !== textSec.raw)
        throw new Error("AssertionError: text raw offset != rva: offsets need mapping");
    print(
        `\n1. rewritten functions whose original code differs between A and B: ${differingRewritten.length} of ${replaced.size - unsized.length}`,
    );
    for (const [addr, end] of differingRewritten) {
        const [kind, name, file] = replaced.get(addr)!;
        const diffs: number[] = [];
        for (let i = addr - baseA; i < end - baseA; i++) if (a[i] !== b[i]) diffs.push(i);
        print(
            `   ${X8(addr)}-${X8(end)} ${kind} ${name} (${file}): ${diffs.length} bytes differ at ${diffs
                .slice(0, 8)
                .map((i) => X8(baseA + i))
                .join(", ")}`,
        );
    }
    if (unsized.length) {
        print(
            `   (${unsized.length} rewritten addresses Ghidra has no function at - checked by the runs below: ${unsized
                .slice(0, 10)
                .map(X8)
                .join(", ")})`,
        );
    }

    // 2. each run: where, whose
    print("\n2. every differing run:");
    const dataRuns: [number, number][] = [];
    for (const [s, e] of runs) {
        const [va, sec] = toVa(secsA, s);
        const f = sec === ".text" ? functionAt(va!) : null;
        let owner: string;
        if (f) {
            let rewrittenStart: number | null = null;
            for (const x of replaced.keys())
                if (x <= va! && (rewrittenStart === null || x > rewrittenStart)) rewrittenStart = x;
            const r = replaced.get(f.addr);
            if (r) owner = `in ${f.name} (rewritten: ${r[0]} ${r[1]})`;
            else if (
                rewrittenStart !== null &&
                byAddr.get(rewrittenStart) === undefined &&
                va! - rewrittenStart < 0x40
            ) {
                owner = `near rewritten ${X8(rewrittenStart)}`;
            } else owner = `in ${f.name} ${X8(f.addr)}-${X8(f.end)} (NOT rewritten)`;
        } else if (sec !== ".text") {
            // (Python's None + e - s would raise for the header: the header never differs here)
            dataRuns.push([va!, va! + e - s]);
            owner = "data";
        } else owner = "in no function (padding / unreached)";
        const n = Math.min(e - s, 10);
        print(
            `   ${X8(va!)} ${sec.padEnd(6)} ${String(e - s).padStart(3)} bytes  A=${hexBytes(a.subarray(s, s + n))} B=${hexBytes(b.subarray(s, s + n))}  ${owner}`,
        );
    }

    // 3. which functions read the differing data (Ghidra's data references), and whether we rewrote them
    print("\n3. functions referring to the differing data:");
    for (const [lo, hi] of dataRuns) {
        const users = funcs.filter((f) => (f.data_refs ?? []).some((r) => lo - 0x40 <= r && r < hi));
        const text =
            users
                .slice(0, 6)
                .map((f) => `${f.name}${replaced.has(f.addr) ? ` (REWRITTEN ${replaced.get(f.addr)![1]})` : ""}`)
                .join(", ") || "none found";
        print(`   ${X8(lo)}-${X8(hi)}: ${text}`);
    }
    return 0;
}

if (isMain(import.meta.url)) await runMain(() => main());
