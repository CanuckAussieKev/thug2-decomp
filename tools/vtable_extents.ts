// NOT ORIGINAL - the true extent of every table of code pointers in the reference build's data (virtual tables,
// member / command tables): where each table starts and how many slots it has.
//
// A table starts at an address the exe refers to as a whole: an immediate or memory displacement of an instruction
// decoded from a function's start (capstone, tools/capstone_refs.py - `mov dword ptr [esi], 0x00657EC8` in a
// constructor, `push 0x...`, `mov eax, [0x... + ecx*4]` - not a lone `call [0x...]`, which reads one slot), or an aligned data word holding that address. It runs while
// its words point at code (a function's start, or code outside every function Ghidra knows) and ends at the next table
// start. (Scanning every byte offset of .text for a matching
// dword instead, as exe_vtable.ts once did, finds random byte matches inside instructions and cuts tables short.)
//
//   node tools/vtable_extents.ts                  the GAME_VTABLEs listing fewer slots than their table has (exit 1)
//   node tools/vtable_extents.ts --write          also regenerate src/hook/vtable_extents.cpp (what thug2_check's
//                                                 CheckGameCode compares every GAME_VTABLE's slot count with)
//   node tools/vtable_extents.ts --complete       append the missing slots to those GAME_VTABLEs (REPLACE names)
//   node tools/vtable_extents.ts --table <addr>   one table: its extent and where it is referenced from
//   [--exe <reference build>] [--refresh]         (the decode is cached in data/derived/data_refs.txt)

import { spawnSync } from "node:child_process";
import * as crypto from "node:crypto";
import * as fs from "node:fs";
import { Image } from "./code_coverage.ts";
import { registrations } from "./game_function_map.ts";
import { REFERENCE_EXE } from "./pipeline/lib/decomp.ts";
import { globRecursive } from "./pipeline/lib/files.ts";
import { isMain, print, runMain } from "./pipeline/lib/py.ts";
import { codeOnly } from "./raw_addresses.ts";

const hex = (n: number): string => `0x${n.toString(16).toUpperCase().padStart(8, "0")}`;
const TEXT_LO = 0x401000;
const TEXT_HI = 0x645000;
const DATA_LO = 0x645000; // .rdata, .data
const DATA_HI = 0x7dd000;
const isCode = (v: number): boolean => v >= TEXT_LO && v < TEXT_HI;
const CACHE = "data/derived/data_refs.txt";
const OUTPUT = "src/hook/vtable_extents.cpp";

// Every data address an instruction takes (an immediate) or indexes from: value -> instructions (the decode cached
// per exe and function list).
interface FunctionList {
    ends: Map<number, number>; // start -> end (exclusive; 0: unknown - up to the next start)
    starts: number[]; // sorted
}

// Ghidra's functions, and the functions our source REPLACEs that it never made (0x0054D8B0...).
function functionList(): FunctionList {
    const functions = JSON.parse(fs.readFileSync("data/derived/functions.json", "utf-8")) as {
        addr: number;
        end: number;
    }[];
    const ends = new Map(functions.map((f) => [f.addr, f.end + 1]));
    for (const file of [".cpp", ".h"].flatMap((s) => globRecursive("src/game", s)))
        for (const m of fs.readFileSync(file, "utf-8").matchAll(/^[ \t]*REPLACE\w*\(0x([0-9A-Fa-f]{8})/gm)) {
            const a = parseInt(m[1]!, 16);
            if (isCode(a) && !ends.has(a)) ends.set(a, 0);
        }
    return { ends, starts: [...ends.keys()].sort((a, b) => a - b) };
}

function instructionRefs(exe: string, bytes: Buffer, fl: FunctionList, refresh: boolean): Map<number, number[]> {
    // (a function Ghidra never made is decoded up to the next function start)
    const { ends, starts } = fl;
    const ranges = starts
        .map((a, i) => `${a.toString(16)} ${(ends.get(a) || (starts[i + 1] ?? a + 16)).toString(16)}`)
        .join("\n");
    const hash = crypto.createHash("sha1").update(bytes).update(ranges).digest("hex");
    let text: string | undefined;
    if (!refresh && fs.existsSync(CACHE)) {
        const cached = fs.readFileSync(CACHE, "utf-8");
        if (cached.startsWith(`# ${hash}\n`)) text = cached;
    }
    if (text === undefined) {
        const r = spawnSync(
            "uv",
            [
                "run",
                "--no-project",
                "--python",
                "3.13",
                "--with",
                "capstone",
                "python",
                "-I",
                "tools/capstone_refs.py",
                "sweep",
                DATA_LO.toString(16),
                DATA_HI.toString(16),
                exe,
            ],
            { encoding: "utf-8", maxBuffer: 1 << 30, input: ranges },
        );
        if (r.status !== 0) throw new Error(`capstone_refs.py failed: ${r.stderr}`);
        text = `# ${hash}\n${r.stdout}`;
        fs.mkdirSync("data/derived", { recursive: true });
        fs.writeFileSync(CACHE, text);
    }
    const refs = new Map<number, number[]>();
    for (const line of text.split("\n")) {
        const p = line.trim().split(" ");
        // (an immediate - the address taken - or an indexed displacement - table[i]; a lone [address] reads one word:
        // `call dword ptr [0x00657ED0]` calls a slot through the table, which doesn't start a table there)
        if (p.length !== 3 || (p[1] !== "operand" && p[1] !== "indexed")) continue;
        const v = parseInt(p[2]!, 16);
        let list = refs.get(v);
        if (!list) {
            list = [];
            refs.set(v, list);
        }
        list.push(parseInt(p[0]!, 16));
    }
    return refs;
}

export interface Extents {
    starts: Set<number>; // every table start found (referenced address whose first word is code)
    slots: (address: number) => number; // a table's extent from that address
    references: Map<number, number[]>; // address -> instructions / data words referring to it
}

export function tableExtents(exe = REFERENCE_EXE, refresh = false): Extents {
    const bytes = fs.readFileSync(exe);
    const img = new Image(bytes);
    const fl = functionList();
    const references = instructionRefs(exe, bytes, fl, refresh);
    // A slot points at a function's start, or at least not into the middle of a function Ghidra knows (".qb\0" =
    // 0x0062712E after the node array component's table is text).
    const known = [...fl.ends].filter(([, e]) => e > 0).sort((a, b) => a[0] - b[0]);
    const insideFunction = (v: number): boolean => {
        let lo = 0;
        let hi = known.length - 1;
        while (lo <= hi) {
            const mid = (lo + hi) >> 1;
            const [a, e] = known[mid]!;
            if (v < a) hi = mid - 1;
            else if (v >= e) lo = mid + 1;
            else return v !== a;
        }
        return false;
    };
    const isSlot = (v: number): boolean => isCode(v) && (fl.ends.has(v) || !insideFunction(v));
    // aligned data words pointing into data (a table of tables, a class description...) - next to another word that
    // points into the image (text that happens to read as an address, "ded\0" = 0x00646564, doesn't count)
    const inImage = (v: number): boolean => v >= TEXT_LO && v < DATA_HI;
    for (const s of img.sectionBytes()) {
        if (s.name === ".text") continue;
        for (let o = 0; o + 4 <= s.data.length; o += 4) {
            const v = s.data.readUInt32LE(o);
            if (v < DATA_LO || v >= DATA_HI) continue;
            const before = o >= 4 ? s.data.readUInt32LE(o - 4) : 0;
            const after = o + 8 <= s.data.length ? s.data.readUInt32LE(o + 4) : 0;
            if (!inImage(before) && !inImage(after)) continue;
            let list = references.get(v);
            if (!list) {
                list = [];
                references.set(v, list);
            }
            list.push(s.va + o);
        }
    }
    const starts = new Set<number>();
    for (const a of references.keys()) if (isSlot(img.dword(a))) starts.add(a);
    const slots = (address: number): number => {
        let n = 0;
        while (n < 4096 && isSlot(img.dword(address + 4 * n)) && (n === 0 || !starts.has(address + 4 * n))) n++;
        return n;
    };
    return { starts, slots, references };
}

interface Declared {
    file: string; // path:line
    path: string;
    close: number; // the index of the macro's closing parenthesis
    last: number; // the index just past the last slot (or the address)
    name: string;
    address: number;
    slots: number;
}

// Every GAME_VTABLE in the source and how many slots it lists.
export function declaredVtables(): Declared[] {
    const out: Declared[] = [];
    for (const file of [".cpp", ".h"].flatMap((s) => globRecursive("src", s))) {
        const text = codeOnly(fs.readFileSync(file, "utf-8"));
        for (const m of text.matchAll(/\bGAME_VTABLE\(/g)) {
            const line = text.slice(0, m.index).split("\n").length;
            if (/#define\s+$/.test(text.slice(text.lastIndexOf("\n", m.index) + 1, m.index))) continue;
            let depth = 1;
            let angle = 0;
            let cur = "";
            const parts: string[] = [];
            let i = m.index + m[0].length;
            let last = i;
            for (; i < text.length && depth > 0; i++) {
                const c = text[i]!;
                if (c === "(" || c === "{" || c === "[") depth++;
                else if (c === ")" || c === "}" || c === "]") {
                    depth--;
                    if (depth === 0) break;
                } else if (c === "<") angle++;
                else if (c === ">" && angle > 0 && text[i - 1] !== "-") angle--;
                if (c === "," && depth === 1 && angle === 0) {
                    parts.push(cur.trim());
                    cur = "";
                } else cur += c;
                if (!/\s/.test(c)) last = i + 1;
            }
            parts.push(cur.trim());
            const address = parseInt(parts[1] ?? "", 16);
            if (!Number.isFinite(address)) continue; // (the macro's own definition)
            out.push({
                file: `${file.replaceAll("\\", "/")}:${line}`,
                path: file,
                close: i,
                last,
                name: parts[0]!,
                address,
                slots: parts.slice(2).filter((p) => p !== "").length,
            });
        }
    }
    return out;
}

// Appends the missing slots to every GAME_VTABLE that lists fewer than its table has: each slot's function as its
// REPLACE line names it, a library function as GAME_LIBRARY_SLOT. (The file may then need an include or a friend.)
function completeTables(ext: Extents, exe: string): void {
    const img = new Image(fs.readFileSync(exe));
    const files = [".cpp", ".h"].flatMap((s) => globRecursive("src/game", s)).map((f) => f.replaceAll("\\", "/"));
    const regs = registrations(files);
    const byFile = new Map<string, Declared[]>();
    for (const d of declaredVtables())
        if (d.slots < ext.slots(d.address)) byFile.set(d.path, [...(byFile.get(d.path) ?? []), d]);
    for (const [path, tables] of byFile) {
        let text = fs.readFileSync(path, "utf-8");
        for (const d of tables.sort((a, b) => b.close - a.close)) {
            const added: string[] = [];
            for (let i = d.slots; i < ext.slots(d.address); i++) {
                const code = img.dword(d.address + 4 * i);
                const r = regs.get(code);
                if (r) added.push(r[0]!.name);
                else if (code >= 0x5beaae) added.push(`GAME_LIBRARY_SLOT(${hex(code)})`);
                else throw new Error(`${d.file} ${d.name} slot ${i}: 0x${code.toString(16)} isn't REPLACEd`);
            }
            const head = text.slice(0, d.last);
            const multiline = head.slice(head.lastIndexOf("GAME_VTABLE(")).includes("\n");
            const indent = multiline ? (/\n([ \t]*)[^\n]*$/.exec(head)?.[1] ?? "    ") : "";
            const sep = multiline ? `,\r\n${indent}` : ", ";
            text = head + added.map((a) => sep + a).join("") + text.slice(d.last);
            print(`${d.file} ${d.name}: +${added.length} slots`);
        }
        fs.writeFileSync(path, text);
    }
}

function writeTable(ext: Extents): void {
    const rows = [...ext.starts].sort((a, b) => a - b).map((a) => `    {${hex(a)}, ${ext.slots(a)}},`);
    const text = [
        "// GENERATED by tools/vtable_extents.ts - do not edit. Every table of code pointers in the original's data:",
        "// its start (an address the original's code or data refers to) and its slots (the words pointing into code, up",
        "// to the next table start). CheckGameCode (game_code.cpp) fails a GAME_VTABLE listing fewer slots than this.",
        "",
        '#include "hook/game_code.h"',
        "",
        "#ifndef THUG2_STANDALONE",
        "",
        "const GameTableExtent kGameTableExtents[] = {",
        ...rows,
        "};",
        "const int kGameTableExtentCount = static_cast<int>(sizeof(kGameTableExtents) / sizeof(kGameTableExtents[0]));",
        "",
        "#endif",
        "",
    ].join("\r\n");
    fs.writeFileSync(OUTPUT, text);
    print(`${OUTPUT}: ${rows.length} tables`);
}

function main(): number {
    const args = process.argv.slice(2);
    const at = (flag: string): string | undefined => {
        const i = args.indexOf(flag);
        return i >= 0 ? args[i + 1] : undefined;
    };
    const ext = tableExtents(at("--exe") ?? REFERENCE_EXE, args.includes("--refresh"));
    const one = at("--table");
    if (one !== undefined) {
        const a = parseInt(one, 16);
        print(`${hex(a)}: ${ext.starts.has(a) ? "a table start" : "NOT a table start"}, ${ext.slots(a)} slots`);
        for (const r of ext.references.get(a) ?? []) print(`  referenced at ${hex(r)}`);
        return 0;
    }
    if (args.includes("--write")) writeTable(ext);
    if (args.includes("--complete")) completeTables(ext, at("--exe") ?? REFERENCE_EXE);
    let short = 0;
    let notStart = 0;
    const declared = declaredVtables();
    for (const d of declared) {
        const n = ext.slots(d.address);
        if (!ext.starts.has(d.address)) {
            notStart++;
            print(`  (not a table start) ${d.file} ${d.name} ${hex(d.address)}: lists ${d.slots}, run ${n}`);
        }
        if (d.slots < n) {
            short++;
            print(`${d.file} ${d.name} ${hex(d.address)}: lists ${d.slots} slots, the table has ${n}`);
        }
    }
    print(
        `${declared.length} GAME_VTABLEs, ${short} list fewer slots than their table has, ${notStart} not at a table start`,
    );
    return short ? 1 : 0;
}

if (isMain(import.meta.url)) await runMain(() => main());
