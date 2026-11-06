// NOT ORIGINAL - the memory manager's heaps as the game logs them once a level is up (src/engine/memory/memory_report.h:
// the "memory heap in <level>:" lines, one a heap - its region's size, the bytes and blocks in use and the most
// there have been), as a table; given two logs (a 32-bit and a 64-bit build's, or two builds of one), side by side:
// each heap's region and peak in both, how much the peak grew and how much room the second build has left.
//
//   node tools/game/heap_report.ts <thug2-decomp.log>              the heaps of each level the log loaded
//   node tools/game/heap_report.ts <first.log> <second.log>        compared, level by level (the first load of each)
//
// A heap whose peak came within 10% of its region (or past it) is marked: that region needs more room on that build.

import * as fs from "node:fs";
import * as path from "node:path";
import { helpFromHeader } from "thug2-level-converter-core/lib/cli.ts";

export interface HeapLine {
    when: string; // "in foun"
    load: number; // which report of the log it is in (0, 1...: a report a level load)
    name: string;
    region: number; // bytes
    used: number;
    peak: number;
    blocks: number;
    peakBlocks: number;
}

export interface HeapReport {
    bits: Map<string, number>; // when -> the build's pointer width, from the "memory heaps" line
    heaps: HeapLine[];
}

const HEADER = /^memory heaps (.+) \((\d+)-bit build\): \d+ heaps$/;
const LINE = /^memory heap (.+): region=(\d+) used=(-?\d+) peak=(\d+) blocks=(-?\d+) peak_blocks=(\d+) name=(.*)$/;

// The heap lines of a log (the log's own prefix before "memory" - a time stamp - is skipped).
export function readHeaps(text: string): HeapReport {
    const report: HeapReport = { bits: new Map(), heaps: [] };
    let load = -1;
    for (const raw of text.split(/\r?\n/)) {
        const at = raw.indexOf("memory heap");
        if (at < 0) continue;
        const line = raw.slice(at).trimEnd();
        const header = HEADER.exec(line);
        if (header) {
            report.bits.set(header[1]!, Number(header[2]));
            ++load;
            continue;
        }
        const m = LINE.exec(line);
        if (!m) continue;
        report.heaps.push({
            when: m[1]!,
            load: Math.max(load, 0),
            name: m[7]!,
            region: Number(m[2]),
            used: Number(m[3]),
            peak: Number(m[4]),
            blocks: Number(m[5]),
            peakBlocks: Number(m[6]),
        });
    }
    return report;
}

// The first load of each level: when -> its heaps (a level loaded again later is the same level).
export function firstLoads(report: HeapReport): Map<string, HeapLine[]> {
    const first = new Map<string, number>();
    for (const heap of report.heaps) {
        if (!first.has(heap.when)) first.set(heap.when, heap.load);
    }
    const out = new Map<string, HeapLine[]>();
    for (const heap of report.heaps) {
        if (first.get(heap.when) !== heap.load) continue;
        const list = out.get(heap.when) ?? [];
        list.push(heap);
        out.set(heap.when, list);
    }
    return out;
}

const kb = (bytes: number) => `${Math.round(bytes / 1024)}`;
// A peak within 10% of its region (or past it).
export const tight = (heap: HeapLine) => heap.peak > heap.region * 0.9;

export function table(heaps: readonly HeapLine[]): string[] {
    const rows = [["heap", "region KB", "peak KB", "peak %", "peak blocks", ""]];
    for (const h of heaps) {
        rows.push([
            h.name,
            kb(h.region),
            kb(h.peak),
            `${Math.round((100 * h.peak) / Math.max(1, h.region))}`,
            `${h.peakBlocks}`,
            tight(h) ? "TIGHT" : "",
        ]);
    }
    return align(rows);
}

// Two builds' heaps of one level, matched by name (a heap made more than once - skaterX - by its place among them).
export function compare(first: readonly HeapLine[], second: readonly HeapLine[]): string[] {
    const keyed = (heaps: readonly HeapLine[]) => {
        const seen = new Map<string, number>();
        return new Map(
            heaps.map((h) => {
                const n = seen.get(h.name) ?? 0;
                seen.set(h.name, n + 1);
                return [n === 0 ? h.name : `${h.name} #${n + 1}`, h] as const;
            }),
        );
    };
    const a = keyed(first);
    const b = keyed(second);
    const rows = [["heap", "region KB", "", "peak KB", "", "growth", "room left", ""]];
    for (const name of new Set([...a.keys(), ...b.keys()])) {
        const x = a.get(name);
        const y = b.get(name);
        const growth = x && y && x.peak > 0 ? `x${(y.peak / x.peak).toFixed(2)}` : "";
        const room = y ? `${kb(y.region - y.peak)} KB` : "";
        rows.push([
            name,
            x ? kb(x.region) : "-",
            y ? kb(y.region) : "-",
            x ? kb(x.peak) : "-",
            y ? kb(y.peak) : "-",
            growth,
            room,
            y && tight(y) ? "TIGHT" : "",
        ]);
    }
    return align(rows);
}

function align(rows: readonly string[][]): string[] {
    const widths = rows[0]!.map((_, i) => Math.max(...rows.map((r) => (r[i] ?? "").length)));
    return rows.map((r) =>
        r
            .map((cell, i) => (i === 0 ? cell.padEnd(widths[i]!) : cell.padStart(widths[i]!)))
            .join("  ")
            .trimEnd(),
    );
}

export function main(argv: readonly string[]): number {
    if (argv.length < 1 || argv.length > 2) {
        console.error("usage: heap_report.ts <thug2-decomp.log> [<second.log>]");
        return 2;
    }
    const reports = argv.map((file) => readHeaps(fs.readFileSync(file, "utf8")));
    if (reports.some((r) => r.heaps.length === 0)) {
        console.error("no 'memory heap' lines in the log (a build before the heap report, or no level loaded)");
        return 1;
    }
    const [first, second] = reports;
    const loadsA = firstLoads(first!);
    if (!second) {
        for (const [when, heaps] of loadsA) {
            console.log(`${when} (${first!.bits.get(when) ?? "?"}-bit build):`);
            for (const row of table(heaps)) console.log(`  ${row}`);
        }
        return 0;
    }
    const loadsB = firstLoads(second);
    for (const [when, heaps] of loadsA) {
        const other = loadsB.get(when);
        if (!other) continue;
        console.log(
            `${when} (${first!.bits.get(when) ?? "?"}-bit build vs ${second.bits.get(when) ?? "?"}-bit build):`,
        );
        for (const row of compare(heaps, other)) console.log(`  ${row}`);
    }
    return 0;
}

if (process.argv[1] && path.resolve(process.argv[1]) === path.resolve(import.meta.filename)) {
    helpFromHeader(import.meta.url);
    process.exit(main(process.argv.slice(2)));
}
