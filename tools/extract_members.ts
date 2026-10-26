// NOT ORIGINAL - extracts THUG2's script member functions and who runs them from the reference build (or THUG2.exe:
// the same layout) into tools/pipeline/level_port/thug2_members.ts, the table the level converters' script translator
// (tools/pipeline/level_port/script_port.ts) reads to know which member functions an object running a script has.
//
//   node tools/extract_members.ts [--exe <reference build exe>] [--thug2 C:\Games\THUG2\Game]
//                                 [--out tools/pipeline/level_port/thug2_members.ts]
//
// (--thug2: the game folder whose scripts' checksum names - Data\pre\*.prx - name the components and table members in
// the output's comments; the data itself comes from the exe alone.)
//
// A script calls a member function by name; the name is a ESYMBOLTYPE_MEMBERFUNCTION symbol registered in one of two
// ways (src/game/script/names.cpp):
//   - a table (RegisterMemberFunctionTable 0x0046F060, called with `push 0x18; push <table>; push <component type>`
//     by each component's register function and the composite object manager): 0x18-byte entries {name, component
//     type, member function pointer} up to a zero name. The script runs the entry on that component of the object
//     (Script::CallMemberFunction 0x00472980: GetComponent(type) - **a null component when the object hasn't it: a
//     crash**), or on the object itself for the types 0x57525413 (object) / 0x0ADA4E74 (compositeobject).
//   - by name (RegisterMemberFunctions 0x0046F020, called once at 0x005A5F62 with the names at 0x00681AD0, 0xFA of
//     them - the count is 0x005A5670's `mov eax, imm32`): the object's CallMemberFunction(name) (vtable slot 3) runs
//     it - a composite object asks each of its components in turn (CBaseComponent vtable slot 12: 0 false, 1 done, 2
//     not mine), then CObject's own (SetProps, Die; anything else: true). **No crash when no component takes it**: the
//     statement does nothing (and is true as a condition).
// Which component takes a by-name member: the member's checksum compared (cmp / sub with an imm32) in that component's
// slot-12 function. The components are the 81 types RegisterComponentTypes (0x004E4860) registers: `push <register>
// (or 0); push <create>; push <type>; mov ecx, esi; call 0x0045F270`. A component's vtable: the last one stored to
// [reg] (C7 0x imm32) by its create function or a constructor it calls (3 deep) that also has the type's checksum - the
// constructor stores the vtable and the type. Object classes (CObject's vtable slot 2 is 0x0045D850, the entry runner):
// their slot 3; CObject's (0x004608F0) is "object", CSkater's (vtable 0x0064EF48, the skater's constructor) "skater".
// A function's end for the scan: the next int3 padding (CC CC) or the next known function start (a call target or a
// pointer to code in .rdata), whichever is first.

import * as fs from "node:fs";
import * as path from "node:path";
import { parseArgs } from "node:util";
import * as prx from "./pipeline/level_port/prx.ts";
import { checksum } from "./pipeline/level_port/qb.ts";
import { REFERENCE_EXE } from "./pipeline/lib/decomp.ts";
import { resolveInstall } from "./pipeline/lib/install.ts";
import { decodeLatin1, hex8, isMain, print, pyExit, runMain } from "./pipeline/lib/py.ts";
import { unpackNumbers } from "./pipeline/lib/struct.ts";

const REGISTER_TABLE = 0x46f060;
const REGISTER_NAMES_CALL = 0x5a5f62; // call RegisterMemberFunctions
const REGISTER_COMPONENTS = 0x4e4860;
const REGISTER_COMPONENT = 0x45f270;
const ALLOC = 0x401cc0;
const BASE_CONSTRUCT = 0x45d730;
const BASE_VTABLE = 0x647618; // CBaseComponent's
const OBJECT_ENTRY_RUNNER = 0x45d850; // CObject vtable slot 2
const OBJECT_CMF = 0x4608f0; // CObject::CallMemberFunction(name)
const SKATER_VTABLE = 0x64ef48;
const COMPONENT_COUNT = 81;
// Component names THUG2's scripts don't use (their checksums match the types; used only when they do).
const OTHER_NAMES = ["object", "compositeobject", "tagging", "vehiclecamera", "vehiclesound", "staticvehicle"];

interface Section {
    va: number;
    size: number;
    raw: number;
    name: string;
}

export class Exe {
    readonly data: Buffer;
    readonly sections: Section[] = [];
    readonly text: Section;
    readonly rdata: Section;

    constructor(data: Buffer) {
        this.data = data;
        const pe = data.readUInt32LE(0x3c);
        const first = pe + 24 + data.readUInt16LE(pe + 20);
        for (let k = 0; k < data.readUInt16LE(pe + 6); k++) {
            const at = first + k * 40;
            const name = decodeLatin1(data.subarray(at, at + 8)).replace(/\0+$/, "");
            const [vsize, va, rsize, raw] = unpackNumbers("<IIII", data, at + 8) as [number, number, number, number];
            this.sections.push({ va: va + 0x400000, size: Math.min(vsize, rsize), raw, name });
        }
        const section = (name: string): Section => {
            const s = this.sections.find((x) => x.name === name);
            if (s === undefined) throw new RangeError(`no ${name} section`);
            return s;
        };
        this.text = section(".text");
        this.rdata = section(".rdata");
    }

    offset(va: number): number {
        for (const s of this.sections) if (s.va <= va && va < s.va + s.size) return s.raw + va - s.va;
        throw new RangeError(`0x${va.toString(16)} is in no section`);
    }

    u8(va: number): number {
        return this.data[this.offset(va)]!;
    }

    u32(va: number): number {
        return this.data.readUInt32LE(this.offset(va));
    }

    i32(va: number): number {
        return this.data.readInt32LE(this.offset(va));
    }

    cstr(va: number): string {
        const at = this.offset(va);
        return decodeLatin1(this.data.subarray(at, this.data.indexOf(0, at)));
    }

    isText(va: number): boolean {
        return va >= this.text.va && va < this.text.va + this.text.size;
    }

    isRdata(va: number): boolean {
        return va >= this.rdata.va && va < this.rdata.va + this.rdata.size;
    }

    // E8 rel32 call sites to `target`.
    callsTo(target: number): number[] {
        const out: number[] = [];
        const { va, size, raw } = this.text;
        for (let k = 0; k + 5 <= size; k++) {
            if (this.data[raw + k] === 0xe8 && va + k + 5 + this.data.readInt32LE(raw + k + 1) === target)
                out.push(va + k);
        }
        return out;
    }
}

// Known function starts: call targets and code pointers in .rdata (sorted).
function functionStarts(exe: Exe): number[] {
    const starts = new Set<number>();
    const { va, size, raw } = exe.text;
    for (let k = 0; k + 5 <= size; k++) {
        if (exe.data[raw + k] !== 0xe8) continue;
        const t = va + k + 5 + exe.data.readInt32LE(raw + k + 1);
        if (exe.isText(t)) starts.add(t);
    }
    const r = exe.rdata;
    for (let k = 0; k + 4 <= r.size; k += 4) {
        const v = exe.data.readUInt32LE(r.raw + k);
        if (exe.isText(v)) starts.add(v);
    }
    return [...starts].sort((a, b) => a - b);
}

function functionEnd(exe: Exe, starts: readonly number[], start: number): number {
    let next = exe.text.va + exe.text.size;
    let lo = 0;
    let hi = starts.length;
    while (lo < hi) {
        const mid = (lo + hi) >> 1;
        if (starts[mid]! <= start) lo = mid + 1;
        else hi = mid;
    }
    if (lo < starts.length) next = starts[lo]!;
    for (let a = start; a + 1 < next; a++) if (exe.u8(a) === 0xcc && exe.u8(a + 1) === 0xcc) return a;
    return next;
}

// The imm32s compared with (or subtracted from) a register or stack value in [start, end).
function comparedValues(exe: Exe, start: number, end: number): Set<number> {
    const out = new Set<number>();
    for (let a = start; a < end; a++) {
        const op = exe.u8(a);
        if ((op === 0x3d || op === 0x2d) && a + 5 <= end) out.add(exe.u32(a + 1)); // cmp / sub eax, imm32
        if (op === 0x81 && a + 2 <= end) {
            const modrm = exe.u8(a + 1);
            const reg = (modrm >> 3) & 7;
            if (reg !== 7 && reg !== 5) continue; // cmp / sub
            const mod = modrm >> 6;
            const rm = modrm & 7;
            let at = a + 2;
            if (mod !== 3 && rm === 4) at += 1; // SIB
            if (mod === 1) at += 1;
            else if (mod === 2 || (mod === 0 && rm === 5)) at += 4;
            if (at + 4 <= end) out.add(exe.u32(at));
        }
    }
    return out;
}

export interface Component {
    type: number;
    create: number;
    register: number;
    vtable: number | null;
    handler: number | null; // vtable slot 12: CallMemberFunction(name)
}

export function components(exe: Exe): Component[] {
    const out: Component[] = [];
    for (let a = REGISTER_COMPONENTS; out.length < COMPONENT_COUNT && a < REGISTER_COMPONENTS + 0x800; a++) {
        if (exe.u8(a) !== 0xe8 || a + 5 + exe.i32(a + 1) !== REGISTER_COMPONENT) continue;
        if (exe.u8(a - 7) !== 0x68 || exe.u8(a - 2) !== 0x8b) throw new RangeError(`0x${a.toString(16)}: not a push`);
        const type = exe.u32(a - 6);
        const create = exe.u32(a - 11);
        const register = exe.u8(a - 17) === 0x68 ? exe.u32(a - 16) : 0;
        const vtable = componentVtable(exe, create, type, 0);
        out.push({ type, create, register, vtable, handler: vtable === null ? null : exe.u32(vtable + 48) });
    }
    if (out.length !== COMPONENT_COUNT) throw new RangeError(`${out.length} component types, not ${COMPONENT_COUNT}`);
    return out;
}

function componentVtable(exe: Exe, fn: number, type: number, depth: number): number | null {
    const stored: number[] = [];
    const calls: number[] = [];
    let hasType = false;
    for (let a = fn; a < fn + 0x400; a++) {
        if (exe.u8(a) === 0xcc && exe.u8(a + 1) === 0xcc) break;
        if (exe.u32(a) === type) hasType = true;
        const op = exe.u8(a);
        const modrm = exe.u8(a + 1);
        if (op === 0xc7 && modrm < 0x08 && modrm !== 4 && modrm !== 5 && exe.isRdata(exe.u32(a + 2)))
            stored.push(exe.u32(a + 2));
        if (op === 0xe8) {
            const t = a + 5 + exe.i32(a + 1);
            if (t !== ALLOC && t !== BASE_CONSTRUCT && exe.isText(t)) calls.push(t);
        }
    }
    if (hasType && stored.length) {
        // (the one most like the base component's vtable - a constructor also stores its members' vtables)
        const score = (v: number): number => {
            let n = 0;
            for (let k = 0; k < 13; k++) if (exe.u32(v + 4 * k) === exe.u32(BASE_VTABLE + 4 * k)) n += 1;
            return n;
        };
        let best: number | null = null;
        for (const v of stored)
            if (v !== BASE_VTABLE && score(v) >= 4 && (best === null || score(v) >= score(best))) best = v;
        if (best !== null) return best;
    }
    if (depth < 3) {
        for (const t of calls) {
            const found = componentVtable(exe, t, type, depth + 1);
            if (found !== null) return found;
        }
    }
    return null;
}

// [member name checksum, component type] of every registered table.
export function tableMembers(exe: Exe): [number, number][] {
    const out: [number, number][] = [];
    for (const call of exe.callsTo(REGISTER_TABLE)) {
        if (exe.u8(call - 5) !== 0x68 || exe.u8(call - 10) !== 0x68 || exe.u8(call - 12) !== 0x6a) {
            throw new RangeError(
                `0x${call.toString(16)}: RegisterMemberFunctionTable's arguments not pushed as expected`,
            );
        }
        const type = exe.u32(call - 4);
        const table = exe.u32(call - 9);
        const stride = exe.u8(call - 11);
        for (let at = table; exe.u32(at) !== 0; at += stride) out.push([exe.u32(at), type]);
    }
    return out;
}

// The by-name members' names.
export function namedMembers(exe: Exe): string[] {
    // ... 68 <names> E8 <RegisterMemberFunctions>, after `call <count function>; push eax`
    const call = REGISTER_NAMES_CALL;
    if (exe.u8(call) !== 0xe8 || exe.u8(call - 5) !== 0x68 || exe.u8(call - 6) !== 0x50 || exe.u8(call - 11) !== 0xe8)
        throw new RangeError("RegisterMemberFunctions' call isn't where expected");
    const names = exe.u32(call - 4);
    const countFn = call - 6 + exe.i32(call - 10);
    if (exe.u8(countFn) !== 0xb8) throw new RangeError("the by-name member count isn't a mov eax, imm32");
    const count = exe.u32(countFn + 1);
    const out: string[] = [];
    for (let k = 0; k < count; k++) out.push(exe.cstr(exe.u32(names + 4 * k)));
    return out;
}

// [label, CallMemberFunction(name)] of the object classes (vtables whose slot 2 runs table entries).
export function objectHandlers(exe: Exe): [string, number][] {
    const out: [string, number][] = [];
    const seen = new Set<number>();
    const r = exe.rdata;
    for (let k = 8; k + 4 <= r.size; k += 4) {
        if (exe.data.readUInt32LE(r.raw + k) !== OBJECT_ENTRY_RUNNER) continue;
        const vtable = r.va + k - 8;
        const handler = exe.u32(vtable + 12);
        if (!exe.isText(handler) || seen.has(handler)) continue;
        seen.add(handler);
        const label =
            handler === OBJECT_CMF ? "object" : vtable === SKATER_VTABLE ? "skater" : `object class ${hex8(vtable)}`;
        out.push([label, handler]);
    }
    return out;
}

// Checksum names from the game's scripts (their 0x2B entries), lower case.
function scriptNames(thug2: string): Map<number, string> {
    const out = new Map<number, string>();
    const pre = path.join(thug2, "Data", "pre");
    if (!fs.existsSync(pre)) return out;
    for (const file of fs.readdirSync(pre).sort()) {
        if (!file.toLowerCase().endsWith(".prx")) continue;
        for (const [, data] of prx.entries(path.join(pre, file), (n) => n.toLowerCase().endsWith(".qb"))) {
            // (0x2B {u32 checksum, name\0} entries at the end of each file)
            for (let at = data.indexOf(0x2b); at >= 0 && at + 6 <= data.length; at = data.indexOf(0x2b, at + 1)) {
                const value = data.readUInt32LE(at + 1);
                const end = data.indexOf(0, at + 5);
                if (end < 0 || end - (at + 5) > 64) continue;
                const text = decodeLatin1(data.subarray(at + 5, end));
                if (/^[A-Za-z_][A-Za-z0-9_]*$/.test(text) && checksum(text) === value && !out.has(value))
                    out.set(value, text);
            }
        }
    }
    return out;
}

export interface Members {
    components: [number, string][]; // component type, name (hex8 when its name isn't known)
    table: [number, string][]; // member checksum, component
    named: [string, string[]][]; // by-name member, the components / objects that take it
    unknown: string[]; // by-name members no handler compares
}

export function extract(exe: Exe, names: ReadonlyMap<number, string>): Members {
    const starts = functionStarts(exe);
    // (names no THUG2 script uses, checked by their checksums: src/game/obj/component_types.cpp's)
    const known = new Map(OTHER_NAMES.map((n) => [checksum(n), n]));
    const label = (type: number): string => (known.get(type) ?? names.get(type) ?? hex8(type)).toLowerCase();
    const comps = components(exe);
    const handlers: [string, number][] = [];
    for (const c of comps) if (c.handler !== null) handlers.push([label(c.type), c.handler]);
    handlers.push(...objectHandlers(exe));
    const compared = handlers.map(([who, at]) => [who, comparedValues(exe, at, functionEnd(exe, starts, at))] as const);
    const named: [string, string[]][] = [];
    const unknown: string[] = [];
    for (const name of namedMembers(exe)) {
        const value = checksum(name);
        const who = [...new Set(compared.filter(([, values]) => values.has(value)).map(([w]) => w))];
        if (!who.length) unknown.push(name);
        named.push([name, who]);
    }
    return {
        components: comps.map((c) => [c.type, label(c.type)]),
        table: tableMembers(exe).map(([m, t]) => [m, label(t)]),
        named,
        unknown,
    };
}

export function render(m: Members, names: ReadonlyMap<number, string>): string {
    const hex = (v: number): string => `0x${hex8(v)}`;
    const lines: string[] = [
        "// GENERATED by tools/extract_members.ts - don't edit. Regenerate with",
        "//   node tools/extract_members.ts --exe <reference build exe> --thug2 C:\\Games\\THUG2\\Game",
        "// (npm run format afterwards). It is committed so that the level converters work without data/derived.",
        "//",
        `// THUG2's script member functions (from the reference build): see tools/extract_members.ts for how each is found.`,
        "//   COMPONENTS     the component types (RegisterComponentTypes 0x004E4860), named from THUG2's scripts",
        "//                  (hex where no script names one).",
        "//   TABLE_MEMBERS  the members registered in tables: [name checksum, component] - run on that component of the",
        "//                  object; an object without it crashes (Script::CallMemberFunction 0x00472980).",
        "//   NAMED_MEMBERS  the members registered by name: [name, the components / objects whose CallMemberFunction(name)",
        "//                  takes it] - an object none of whose components takes it does nothing (no crash). `object`:",
        "//                  every object (CObject); `skater`: the skater object itself (CSkater).",
        "",
        "export const COMPONENTS: readonly (readonly [number, string])[] = [",
        ...m.components.map(([t, n]) => `    [${hex(t)}, "${n}"],`),
        "];",
        "",
        "export const TABLE_MEMBERS: readonly (readonly [number, string])[] = [",
        ...m.table.map(([v, c]) => `    [${hex(v)}, "${c}"],${names.has(v) ? ` // ${names.get(v)}` : ""}`),
        "];",
        "",
        "export const NAMED_MEMBERS: readonly (readonly [string, readonly string[]])[] = [",
        ...m.named.map(([n, who]) => `    ["${n}", [${who.map((w) => `"${w}"`).join(", ")}]],`),
        "];",
        "",
    ];
    return lines.join("\n");
}

function main(): number {
    const { values } = parseArgs({
        options: {
            exe: { type: "string", default: REFERENCE_EXE },
            thug2: { type: "string", default: "C:\\Games\\THUG2\\Game" },
            out: { type: "string", default: path.join("tools", "pipeline", "level_port", "thug2_members.ts") },
        },
    });
    if (!fs.existsSync(values.exe)) pyExit(`${values.exe} not found`);
    const exe = new Exe(fs.readFileSync(values.exe));
    const names = scriptNames(resolveInstall(values.thug2, "thug2"));
    const m = extract(exe, names);
    fs.writeFileSync(values.out, render(m, names));
    print(
        `${m.components.length} component types, ${m.table.length} table members, ${m.named.length} by-name members ->`,
        values.out,
    );
    if (m.unknown.length) print("by-name members no handler compares:", m.unknown.join(" "));
    return 0;
}

if (isMain(import.meta.url)) await runMain(() => main());
