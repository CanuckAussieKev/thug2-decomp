// NOT ORIGINAL - THPS2's gap names and scores from the user's own THPS2 executable, read at convert time (nothing of
// the executable is stored in the repo).
//
// THPS2 keeps its gap table in the executable, not in ALL.PKR: a level's .trg only numbers its gaps (GapPolyHit(other,
// gap) - thps2.ts). So the converter looks in <thps2>\THawk2.exe and THPS2.exe (or the --thps2-exe file) for it:
//
//   - probeExe: whether the executable's sections can be read at all. The retail PC release is SafeDisc-wrapped
//     (version 2.05.030 on the user's disc: the loader's "BoG_ *90.0&!!  Yy>" signature and its version words, the
//     stxt774 / stxt371 sections); its .text and .data are encrypted on disk - no string, no table, entropy ~7.95 bits
//     a byte - and are decrypted only by the loader at run time. PartyMod's THPS2.exe is that file run through
//     partypatcher's patch (35 bytes), so it is encrypted too. Nothing here decrypts anything: an encrypted executable
//     is reported and the gaps keep their stand-in names.
//   - findGapTable: in a readable (unwrapped) executable - its data sections - a table of fixed-size records each
//     holding a gap's number (u16 or u32: one of the numbers the levels' .trg files use), its name (a pointer to a
//     NUL-terminated string in the image, or the string in the record) and its score. The layout is not known yet (no
//     readable THPS2 executable has been seen): the finder searches every record size, field place and width for the
//     arrangement the levels' gap numbers fit best, and reports what it found so the layout can be fixed once seen.
//
// A gap the table doesn't name keeps "GAP <number>" and 250 points (thps2_levels.ts).

import * as fs from "node:fs";
import * as zlib from "node:zlib";

export interface GapInfo {
    name: string;
    score: number;
}

// --- the executable ------------------------------------------------------------------------------------------------

export interface PeSection {
    name: string;
    va: number; // relative to the image base
    virtualSize: number;
    raw: number; // file offset
    rawSize: number;
}

export interface PeImage {
    imageBase: number;
    sections: PeSection[];
}

export function readPe(buf: Buffer): PeImage | null {
    if (buf.length < 0x40 || buf.toString("latin1", 0, 2) !== "MZ") return null;
    const pe = buf.readUInt32LE(0x3c);
    if (pe + 24 > buf.length || buf.toString("latin1", pe, pe + 4) !== "PE\0\0") return null;
    const count = buf.readUInt16LE(pe + 6);
    const optional = buf.readUInt16LE(pe + 20);
    const imageBase = buf.readUInt32LE(pe + 24 + 28);
    const sections: PeSection[] = [];
    for (let i = 0; i < count; i++) {
        const o = pe + 24 + optional + i * 40;
        if (o + 40 > buf.length) break;
        sections.push({
            name: buf.toString("latin1", o, o + 8).replace(/\0.*$/s, ""),
            va: buf.readUInt32LE(o + 12),
            virtualSize: buf.readUInt32LE(o + 8),
            raw: buf.readUInt32LE(o + 20),
            rawSize: buf.readUInt32LE(o + 16),
        });
    }
    return { imageBase, sections };
}

// Bits of entropy a byte (0..8).
export function entropy(buf: Buffer, from = 0, to = buf.length): number {
    if (to <= from) return 0;
    const counts = new Array<number>(256).fill(0);
    for (let i = from; i < to; i++) counts[buf[i]!]!++;
    let h = 0;
    for (const c of counts) {
        if (!c) continue;
        const p = c / (to - from);
        h -= p * Math.log2(p);
    }
    return h;
}

const SAFEDISC_SIGNATURE = Buffer.from("BoG_ *90.0&!!  Yy>", "latin1");

export interface ExeProbe {
    file: string;
    size: number;
    crc32: number;
    pe: boolean;
    safeDisc: string | null; // the wrapper's version ("2.05.030"), null when there's no SafeDisc loader
    encrypted: boolean; // the data sections can't be read (no table can be found)
    dataEntropy: number; // of the largest data section
}

// What kind of executable `buf` is: PE, SafeDisc-wrapped (its version), and whether its data is readable. A data
// section above 7.5 bits a byte is taken as encrypted (a game's real .data - strings, tables, zeros - is far below).
export function probeExe(buf: Buffer, file = "?"): ExeProbe {
    const pe = readPe(buf);
    let safeDisc: string | null = null;
    const sig = buf.indexOf(SAFEDISC_SIGNATURE);
    if (sig >= 0 && sig + 44 <= buf.length) {
        // the version words follow it: major, minor, build at +32 (2, 5, 30 on THPS2's: 2.05.030)
        const v = [0, 4, 8].map((k) => buf.readUInt32LE(sig + 32 + k));
        safeDisc =
            v[0]! < 10 && v[1]! < 100 && v[2]! < 1000
                ? `${v[0]}.${String(v[1]).padStart(2, "0")}.${String(v[2]).padStart(3, "0")}`
                : "?";
    }
    let dataEntropy = 0;
    if (pe) {
        const data = pe.sections
            .filter((s) => /^\.(data|rdata)$/i.test(s.name) && s.rawSize > 0)
            .sort((a, b) => b.rawSize - a.rawSize)[0];
        if (data) dataEntropy = entropy(buf, data.raw, Math.min(buf.length, data.raw + data.rawSize));
    }
    return {
        file,
        size: buf.length,
        crc32: zlib.crc32(buf) >>> 0,
        pe: pe !== null,
        safeDisc,
        encrypted: pe !== null && dataEntropy > 7.5,
        dataEntropy,
    };
}

export function describeProbe(p: ExeProbe): string {
    const crc = p.crc32.toString(16).padStart(8, "0");
    if (!p.pe) return `${p.file}: not a PE executable`;
    const wrapper = p.safeDisc ? `SafeDisc ${p.safeDisc}-wrapped` : "no SafeDisc loader";
    return `${p.file}: ${p.size} bytes, crc32 ${crc}, ${wrapper}, data ${p.encrypted ? "encrypted" : "readable"} (${p.dataEntropy.toFixed(2)} bits a byte)`;
}

// --- the table -----------------------------------------------------------------------------------------------------

export interface TableLayout {
    at: number; // file offset of the first record found
    stride: number; // record size
    numberAt: number; // the gap number's place in the record
    numberWidth: 2 | 4;
    nameAt: number;
    nameInline: boolean; // the name in the record (else a pointer to it)
    scoreAt: number;
    scoreWidth: 2 | 4;
    records: number;
}

export interface GapTable {
    gaps: Map<number, GapInfo>;
    layouts: TableLayout[];
}

const printable = (c: number): boolean => c >= 0x20 && c < 0x7f;

// A gap name: 2-48 printable characters ending in NUL, with a letter in it.
export function readName(buf: Buffer, at: number): string | null {
    if (at < 0 || at >= buf.length) return null;
    let end = at;
    while (end < buf.length && end - at <= 48 && printable(buf[end]!)) end++;
    if (end >= buf.length || buf[end] !== 0 || end - at < 2 || end - at > 48) return null;
    const s = buf.toString("latin1", at, end);
    return /[A-Za-z]/.test(s) ? s : null;
}

// File offset of a virtual address (absolute) in the image, or -1.
export function fileOffset(pe: PeImage, address: number): number {
    const rva = address - pe.imageBase;
    for (const s of pe.sections) {
        if (rva >= s.va && rva < s.va + Math.min(s.rawSize, Math.max(s.virtualSize, s.rawSize)))
            return s.raw + (rva - s.va);
    }
    return -1;
}

const read = (buf: Buffer, at: number, width: 2 | 4): number =>
    at + width > buf.length ? -1 : width === 2 ? buf.readUInt16LE(at) : buf.readUInt32LE(at);

// Find the gap table in a readable image: `numbers` are the gap numbers the levels use. The search: every place a
// u16 / u32 holds one of them starts a candidate run - records `stride` bytes apart (4..64) whose number field keeps
// holding gap-like numbers (within 50 of a used one). Runs with 4+ used numbers, half their records, are kept; a run's
// name field is the place in the record holding a name (a pointer to one, or the text) in 9 of 10 records (mostly
// different names), its score field a u16 / u32 at another place holding 1..100000 in every record, half of them or
// more multiples of 25 (the most wins). The best runs (most used numbers) win; a number named twice keeps its first.
export function findGapTable(image: Buffer, numbers: Iterable<number>, pe: PeImage | null = readPe(image)): GapTable {
    const used = new Set(numbers);
    const out: GapTable = { gaps: new Map(), layouts: [] };
    if (used.size === 0) return out;
    const sorted = [...used].sort((a, b) => a - b);
    const gapLike = (v: number): boolean => {
        if (v < 0) return false;
        let lo = 0;
        let hi = sorted.length - 1;
        while (lo < hi) {
            const mid = (lo + hi) >> 1;
            if (sorted[mid]! < v) lo = mid + 1;
            else hi = mid;
        }
        return Math.abs(sorted[lo]! - v) <= 50 || (lo > 0 && Math.abs(sorted[lo - 1]! - v) <= 50);
    };
    // the regions searched: the data sections of a PE (all of a raw image)
    const regions: [number, number][] = pe
        ? pe.sections
              .filter((s) => !/^\.(text|rsrc|reloc)$/i.test(s.name) && s.rawSize > 0)
              .map((s) => [s.raw, Math.min(image.length, s.raw + s.rawSize)] as [number, number])
        : [[0, image.length]];
    const nameOf = (rec: number, nameAt: number, inline: boolean): string | null => {
        if (inline) return readName(image, rec + nameAt);
        const p = read(image, rec + nameAt, 4);
        if (p <= 0) return null;
        const at = pe ? fileOffset(pe, p) : p;
        return at < 0 ? null : readName(image, at);
    };
    interface Candidate {
        layout: TableLayout;
        gaps: Map<number, GapInfo>;
        hits: number;
    }
    const candidates: Candidate[] = [];
    const taken = new Set<string>();
    for (const [from, to] of regions) {
        for (const width of [4, 2] as const) {
            for (let at = from; at + width <= to; at += 2) {
                if (!used.has(read(image, at, width))) continue;
                for (let stride = 4; stride <= 64; stride += 2) {
                    if (stride < width + 2) continue;
                    // the run's start: walk back while gap-like
                    let first = at;
                    while (first - stride >= from && gapLike(read(image, first - stride, width))) first -= stride;
                    const key = `${first}:${stride}:${width}`;
                    if (taken.has(key)) continue;
                    taken.add(key);
                    const recs: number[] = [];
                    for (let p = first; p + width <= to && gapLike(read(image, p, width)); p += stride) recs.push(p);
                    if (recs.length < 4) continue;
                    const hits = recs.filter((p) => used.has(read(image, p, width))).length;
                    if (hits < 4 || hits * 2 < recs.length) continue;
                    const c = layoutOf(recs, stride, width);
                    if (c) candidates.push(c);
                }
            }
        }
    }
    function layoutOf(recs: number[], stride: number, numberWidth: 2 | 4): Candidate | null {
        // record starts relative to the number field: try every place of the number within the record
        let best: Candidate | null = null;
        for (let numberAt = 0; numberAt + numberWidth <= stride; numberAt += 2) {
            const starts = recs.map((p) => p - numberAt);
            if (starts[0]! < 0) continue;
            const overlaps = (a: number, aw: number, b: number, bw: number): boolean => a < b + bw && b < a + aw;
            // the name
            let name: { at: number; inline: boolean } | null = null;
            for (const inline of [false, true]) {
                for (let nameAt = 0; nameAt + (inline ? 3 : 4) <= stride && !name; nameAt += inline ? 1 : 4) {
                    if (overlaps(nameAt, inline ? 3 : 4, numberAt, numberWidth)) continue;
                    const found = starts.map((s) => nameOf(s, nameAt, inline)).filter((n) => n !== null);
                    // names in 9 of 10 records, mostly different ones
                    if (found.length * 10 >= starts.length * 9 && new Set(found).size * 5 >= found.length * 4)
                        name = { at: nameAt, inline };
                }
                if (name) break;
            }
            if (!name) continue;
            // (a name in the record takes up to its longest one's NUL)
            const longest = Math.max(...starts.map((s) => (nameOf(s, name.at, true) ?? "").length + 1));
            const nameEnd = name.inline ? Math.min(stride, name.at + longest) : name.at + 4;
            // the score
            let score: { at: number; width: 2 | 4; round: number } | null = null;
            for (const scoreWidth of [4, 2] as const) {
                for (let scoreAt = 0; scoreAt + scoreWidth <= stride; scoreAt += 2) {
                    if (overlaps(scoreAt, scoreWidth, numberAt, numberWidth)) continue;
                    if (overlaps(scoreAt, scoreWidth, name.at, nameEnd - name.at)) continue;
                    const values = starts.map((s) => read(image, s + scoreAt, scoreWidth));
                    if (!values.every((v) => v >= 1 && v <= 100000)) continue;
                    const round = values.filter((v) => v % 25 === 0).length;
                    if (round * 2 < values.length) continue; // scores are mostly round numbers
                    if (!score || round > score.round) score = { at: scoreAt, width: scoreWidth, round };
                }
            }
            if (!score) continue;
            const gaps = new Map<number, GapInfo>();
            let hits = 0;
            for (const s of starts) {
                const n = read(image, s + numberAt, numberWidth);
                const nm = nameOf(s, name.at, name.inline);
                if (nm === null) continue;
                if (!gaps.has(n)) gaps.set(n, { name: nm, score: read(image, s + score.at, score.width) });
                if (used.has(n)) hits++;
            }
            const c: Candidate = {
                layout: {
                    at: starts[0]!,
                    stride,
                    numberAt,
                    numberWidth,
                    nameAt: name.at,
                    nameInline: name.inline,
                    scoreAt: score.at,
                    scoreWidth: score.width,
                    records: starts.length,
                },
                gaps,
                hits: hits * 1000 + score.round,
            };
            if (!best || c.hits > best.hits) best = c;
        }
        return best;
    }
    candidates.sort((a, b) => b.hits - a.hits);
    for (const c of candidates) {
        let added = 0;
        for (const [n, g] of c.gaps) {
            if (!used.has(n) || out.gaps.has(n)) continue;
            out.gaps.set(n, g);
            added++;
        }
        if (added) out.layouts.push(c.layout);
    }
    return out;
}

// --- reading it ----------------------------------------------------------------------------------------------------

export interface GapSource {
    gaps: Map<number, GapInfo>;
    report: string[];
}

// The gap table of the first readable executable of `files` (those that exist), with a report of each file looked
// at. No readable one: an empty table (the converter's stand-ins).
export function loadGapTable(files: readonly string[], numbers: Iterable<number>): GapSource {
    const report: string[] = [];
    const wanted = [...numbers];
    for (const file of files) {
        if (!fs.existsSync(file)) continue;
        const buf = fs.readFileSync(file);
        const probe = probeExe(buf, file);
        report.push(describeProbe(probe));
        if (!probe.pe || probe.encrypted) continue;
        const table = findGapTable(buf, wanted);
        for (const l of table.layouts)
            report.push(
                `  gap table at 0x${l.at.toString(16)}: ${l.records} records of ${l.stride} bytes - number +${l.numberAt} ` +
                    `(u${l.numberWidth * 8}), name +${l.nameAt} (${l.nameInline ? "text" : "pointer"}), score +${l.scoreAt} (u${l.scoreWidth * 8})`,
            );
        report.push(`  ${table.gaps.size} of ${new Set(wanted).size} gaps named`);
        if (table.gaps.size) return { gaps: table.gaps, report };
    }
    if (report.length === 0) report.push(`no THPS2 executable found (${files.join(", ")})`);
    report.push("gaps keep their stand-in names (GAP <number>, 250 points)");
    return { gaps: new Map(), report };
}
