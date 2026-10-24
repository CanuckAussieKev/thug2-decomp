// Tony Hawk's Project 8 (Xbox 360) save files - the "skater-progress" file of a "Project 8 Saved Content" package,
// as Project8Recomp keeps it (Documents\thps_p8\<profile>\415607DD\00000001\Project 8 Saved Content\skater-progress).
//
// The file (decoded from the user's own save; every byte accounted for by read + write round-tripping it):
//
//   0x00  u32 BE  Neversoft's CRC-32 (no final inversion - crc32 below) of bytes [0, used size), this word taken as 0
//   0x04  u32 BE  the same CRC of the summary structure's bytes (0x14 .. 0x14 + summary size)
//   0x08  u32 BE  the summary structure's size in bytes (with its terminating 0)
//   0x0C  u32 BE  used size: the end of the main structure
//   0x10  u32 BE  0x47 - a format version (kept as read)
//   0x14  the summary structure (what the load screen shows: career missions complete / total, goals, file name)
//   ....  the main structure (the profile: options, career, stats, CAS...)
//   ....  up to the file's fixed size (98304): the byte 0x69
//
// The structures are THUG2's WriteToBuffer format (src/game/script/struct_buffer.h) as THP8 writes it on the 360:
// a type byte (| 0x80: the name is entry n of the qb global writetobuffer_compressionlookuptable_8 - one byte n; |
// 0x40: of ..._16, two bytes), the name as a little-endian u32, the value; a 0 byte ends a structure. The component
// headers and values are written byte by byte little-endian (integers, floats, names, array counts and elements); only
// a wide string is the 360's memory copied as it is: big-endian. THP8's types: 1 integer, 2 float,
// 3 string, 4 wide string (UTF-16 BE, up to a 0 character - THUG2's local string), 5 pair, 6 vector, 0x0A structure,
// 0x0C array {u8 element type, u16 count, elements}, 0x0D name (checksum), 0x0E / 0x0F signed byte / short, 0x10 /
// 0x11 unsigned byte / short, 0x12 integer 0, 0x13 float 0, 0x1A a reference (a checksum, unnamed: the qb item
// format's $global).

import * as zlib from "node:zlib";

export const T_INT = 0x01;
export const T_FLOAT = 0x02;
export const T_STRING = 0x03;
export const T_WIDE = 0x04;
export const T_PAIR = 0x05;
export const T_VECTOR = 0x06;
export const T_STRUCT = 0x0a;
export const T_ARRAY = 0x0c;
export const T_NAME = 0x0d;
export const T_S8 = 0x0e;
export const T_S16 = 0x0f;
export const T_U8 = 0x10;
export const T_U16 = 0x11;
export const T_ZERO_INT = 0x12;
export const T_ZERO_FLOAT = 0x13;
export const T_REF = 0x1a;

export const FILE_SIZE = 98304;
export const PAD_BYTE = 0x69;
const HEADER = 0x14;

export class FormatError extends Error {}

export type Value =
    | { kind: "int"; value: number }
    | { kind: "float"; value: number }
    | { kind: "string"; value: string }
    | { kind: "wide"; value: string }
    | { kind: "pair"; value: [number, number] }
    | { kind: "vector"; value: [number, number, number] }
    | { kind: "name"; value: number }
    | { kind: "ref"; value: number }
    | { kind: "struct"; value: SaveStruct }
    | { kind: "array"; elementType: number; value: Value[] };

export interface Component {
    // the type byte as read (with its 0x80 / 0x40 flags); the writer keeps it unless the value needs another form
    type: number;
    // the index into a compression table (0x80 / 0x40), else undefined
    index?: number;
    name: number;
    value: Value;
}

export interface SaveStruct {
    components: Component[];
}

export interface SaveFile {
    version: number; // the word at 0x10
    summary: SaveStruct;
    main: SaveStruct;
    // what the file stored, for checks
    stored: { crc: number; summaryCrc: number; summarySize: number; used: number; length: number };
}

// The two name tables (qb.pak's writetobuffer_compressionlookuptable_8 / _16).
export interface NameTables {
    short: number[];
    medium: number[];
}

// Neversoft's checksum: CRC-32 without the final inversion (src/game/core/crc.h; zlib's inverted back).
export function crc32(data: Uint8Array): number {
    return (zlib.crc32(data) ^ 0xffffffff) >>> 0;
}

// --- reading ----------------------------------------------------------------------------------------------------

class Reader {
    p: number;
    readonly d: Buffer;
    readonly tables: NameTables;
    constructor(d: Buffer, start: number, tables: NameTables) {
        this.d = d;
        this.p = start;
        this.tables = tables;
    }

    need(n: number): void {
        if (this.p + n > this.d.length) throw new FormatError(`past the end at 0x${this.p.toString(16)}`);
    }

    u8(): number {
        this.need(1);
        return this.d[this.p++]!;
    }

    wide(): string {
        let s = "";
        for (;;) {
            this.need(2);
            const c = this.d.readUInt16BE(this.p);
            this.p += 2;
            if (!c) return s;
            s += String.fromCharCode(c);
        }
    }

    ascii(): string {
        const e = this.d.indexOf(0, this.p);
        if (e < 0) throw new FormatError(`unterminated string at 0x${this.p.toString(16)}`);
        const s = this.d.subarray(this.p, e).toString("latin1");
        this.p = e + 1;
        return s;
    }

    value(t: number, at: number): Value {
        const d = this.d;
        const take = (n: number): number => {
            this.need(n);
            const q = this.p;
            this.p += n;
            return q;
        };
        switch (t) {
            case T_INT:
                return { kind: "int", value: d.readInt32LE(take(4)) };
            case T_FLOAT:
                return { kind: "float", value: d.readFloatLE(take(4)) };
            case T_STRING:
                return { kind: "string", value: this.ascii() };
            case T_WIDE:
                return { kind: "wide", value: this.wide() };
            case T_PAIR: {
                const q = take(8);
                return { kind: "pair", value: [d.readFloatLE(q), d.readFloatLE(q + 4)] };
            }
            case T_VECTOR: {
                const q = take(12);
                return { kind: "vector", value: [d.readFloatLE(q), d.readFloatLE(q + 4), d.readFloatLE(q + 8)] };
            }
            case T_STRUCT:
                return { kind: "struct", value: this.struct() };
            case T_ARRAY:
                return this.array();
            case T_NAME:
                return { kind: "name", value: d.readUInt32LE(take(4)) };
            case T_S8:
                return { kind: "int", value: d.readInt8(take(1)) };
            case T_S16:
                return { kind: "int", value: d.readInt16LE(take(2)) };
            case T_U8:
                return { kind: "int", value: d.readUInt8(take(1)) };
            case T_U16:
                return { kind: "int", value: d.readUInt16LE(take(2)) };
            case T_REF:
                return { kind: "ref", value: d.readUInt32LE(take(4)) };
            case T_ZERO_INT:
                return { kind: "int", value: 0 };
            case T_ZERO_FLOAT:
                return { kind: "float", value: 0 };
            default:
                throw new FormatError(`unknown type 0x${t.toString(16)} at 0x${at.toString(16)}`);
        }
    }

    array(): Value {
        const at = this.p;
        const t = this.u8();
        this.need(2);
        const n = this.d.readUInt16LE(this.p);
        this.p += 2;
        const value: Value[] = [];
        for (let i = 0; i < n; i++) value.push(this.value(t, at));
        return { kind: "array", elementType: t, value };
    }

    struct(): SaveStruct {
        const components: Component[] = [];
        for (;;) {
            const at = this.p;
            const type = this.u8();
            if (type === 0) return { components };
            let name: number;
            let index: number | undefined;
            if (type & 0x80) {
                index = this.u8();
                const v = this.tables.short[index];
                if (v === undefined)
                    throw new FormatError(`name index ${index} past the table at 0x${at.toString(16)}`);
                name = v;
            } else if (type & 0x40) {
                this.need(2);
                index = this.d.readUInt16LE(this.p);
                this.p += 2;
                const v = this.tables.medium[index];
                if (v === undefined)
                    throw new FormatError(`name index ${index} past the table at 0x${at.toString(16)}`);
                name = v;
            } else {
                this.need(4);
                name = this.d.readUInt32LE(this.p);
                this.p += 4;
            }
            const value = this.value(type & 0x3f, at);
            components.push(index === undefined ? { type, name, value } : { type, index, name, value });
        }
    }
}

export function readSave(data: Buffer, tables: NameTables): SaveFile {
    if (data.length < HEADER) throw new FormatError("too short");
    const stored = {
        crc: data.readUInt32BE(0),
        summaryCrc: data.readUInt32BE(4),
        summarySize: data.readUInt32BE(8),
        used: data.readUInt32BE(12),
        length: data.length,
    };
    if (stored.used > data.length || stored.used < HEADER) throw new FormatError(`used size ${stored.used}`);
    const r = new Reader(data.subarray(0, stored.used), HEADER, tables);
    const summary = r.struct();
    if (r.p - HEADER !== stored.summarySize)
        throw new FormatError(`summary ends at 0x${r.p.toString(16)}, header says ${stored.summarySize} bytes`);
    const main = r.struct();
    if (r.p !== stored.used)
        throw new FormatError(`main structure ends at 0x${r.p.toString(16)} of 0x${stored.used.toString(16)}`);
    return { version: data.readUInt32BE(0x10), summary, main, stored };
}

// The checks a reader of the file would make: [what, ok].
export function verifyChecksums(data: Buffer): [string, boolean][] {
    const used = data.readUInt32BE(12);
    const summarySize = data.readUInt32BE(8);
    const zeroed = Buffer.from(data.subarray(0, used));
    zeroed.writeUInt32BE(0, 0);
    let pad = true;
    for (let i = used; i < data.length; i++) if (data[i] !== PAD_BYTE) pad = false;
    return [
        ["file CRC", crc32(zeroed) === data.readUInt32BE(0)],
        ["summary CRC", crc32(data.subarray(HEADER, HEADER + summarySize)) === data.readUInt32BE(4)],
        ["padding", pad],
    ];
}

// --- writing ----------------------------------------------------------------------------------------------------

class Writer {
    chunks: Buffer[] = [];
    size = 0;
    push(b: Buffer): void {
        this.chunks.push(b);
        this.size += b.length;
    }
    u8(v: number): void {
        this.push(Buffer.of(v & 0xff));
    }
    u16(v: number): void {
        const b = Buffer.alloc(2);
        b.writeUInt16LE(v);
        this.push(b);
    }
    u32(v: number): void {
        const b = Buffer.alloc(4);
        b.writeUInt32LE(v >>> 0);
        this.push(b);
    }
    i32(v: number): void {
        const b = Buffer.alloc(4);
        b.writeInt32LE(v);
        this.push(b);
    }
    f32(v: number): void {
        const b = Buffer.alloc(4);
        b.writeFloatLE(v);
        this.push(b);
    }
    wide(s: string): void {
        const b = Buffer.alloc(2 * s.length + 2);
        for (let i = 0; i < s.length; i++) b.writeUInt16BE(s.charCodeAt(i), 2 * i);
        this.push(b);
    }
    ascii(s: string): void {
        this.push(Buffer.concat([Buffer.from(s, "latin1"), Buffer.of(0)]));
    }
}

// The base type an integer value is written as (THUG2's WriteInteger: the smallest form).
export function integerType(v: number): number {
    if (v === 0) return T_ZERO_INT;
    if (v > 0 && v <= 0xff) return T_U8;
    if (v > 0 && v <= 0xffff) return T_U16;
    if (v >= -0x80 && v <= 0x7f) return T_S8;
    if (v >= -0x8000 && v <= 0x7fff) return T_S16;
    return T_INT;
}

const INT_TYPES = new Set([T_INT, T_S8, T_S16, T_U8, T_U16, T_ZERO_INT]);

// The type byte a component is written with: as read when it still fits the value, else the writer's choice.
function componentType(c: Component): number {
    const flags = c.type & 0xc0;
    const base = c.type & 0x3f;
    const v = c.value;
    let want: number;
    switch (v.kind) {
        case "int":
            want = INT_TYPES.has(base) && fitsInt(base, v.value) ? base : integerType(v.value);
            break;
        case "float":
            want =
                base === T_ZERO_FLOAT && v.value !== 0
                    ? T_FLOAT
                    : base === T_FLOAT || base === T_ZERO_FLOAT
                      ? base
                      : T_FLOAT;
            break;
        case "string":
            want = T_STRING;
            break;
        case "wide":
            want = T_WIDE;
            break;
        case "pair":
            want = T_PAIR;
            break;
        case "vector":
            want = T_VECTOR;
            break;
        case "name":
            want = T_NAME;
            break;
        case "ref":
            want = T_REF;
            break;
        case "struct":
            want = T_STRUCT;
            break;
        case "array":
            want = T_ARRAY;
            break;
    }
    return flags | want;
}

function fitsInt(t: number, v: number): boolean {
    switch (t) {
        case T_ZERO_INT:
            return v === 0;
        case T_U8:
            return v >= 0 && v <= 0xff;
        case T_U16:
            return v >= 0 && v <= 0xffff;
        case T_S8:
            return v >= -0x80 && v <= 0x7f;
        case T_S16:
            return v >= -0x8000 && v <= 0x7fff;
        default:
            return v >= -0x80000000 && v <= 0x7fffffff;
    }
}

function writeValue(w: Writer, t: number, v: Value): void {
    switch (t) {
        case T_INT:
            w.i32((v as { value: number }).value);
            return;
        case T_S8:
        case T_U8:
            w.u8((v as { value: number }).value);
            return;
        case T_S16:
        case T_U16:
            w.u16((v as { value: number }).value & 0xffff);
            return;
        case T_ZERO_INT:
        case T_ZERO_FLOAT:
            return;
        case T_FLOAT:
            w.f32((v as { value: number }).value);
            return;
        case T_STRING:
            w.ascii((v as { value: string }).value);
            return;
        case T_WIDE:
            w.wide((v as { value: string }).value);
            return;
        case T_PAIR:
        case T_VECTOR:
            for (const x of (v as { value: number[] }).value) w.f32(x);
            return;
        case T_NAME:
        case T_REF:
            w.u32((v as { value: number }).value);
            return;
        case T_STRUCT:
            writeStruct(w, (v as { value: SaveStruct }).value);
            return;
        case T_ARRAY: {
            const a = v as { elementType: number; value: Value[] };
            w.u8(a.elementType);
            w.u16(a.value.length);
            for (const e of a.value) writeValue(w, a.elementType, e);
            return;
        }
    }
    throw new FormatError(`can't write type 0x${t.toString(16)}`);
}

function writeStruct(w: Writer, s: SaveStruct): void {
    for (const c of s.components) {
        const type = componentType(c);
        w.u8(type);
        if (type & 0x80) w.u8(c.index!);
        else if (type & 0x40) w.u16(c.index!);
        else w.u32(c.name);
        writeValue(w, type & 0x3f, c.value);
    }
    w.u8(0);
}

export function structBytes(s: SaveStruct): Buffer {
    const w = new Writer();
    writeStruct(w, s);
    return Buffer.concat(w.chunks);
}

// The file's bytes: header, structures, checksums, padding to the fixed size.
export function writeSave(save: SaveFile, length = FILE_SIZE): Buffer {
    const summary = structBytes(save.summary);
    const main = structBytes(save.main);
    const used = HEADER + summary.length + main.length;
    if (used > length) throw new FormatError(`${used} bytes don't fit in the file's ${length}`);
    const out = Buffer.alloc(length, PAD_BYTE);
    out.writeUInt32BE(0, 0);
    out.writeUInt32BE(crc32(summary), 4);
    out.writeUInt32BE(summary.length, 8);
    out.writeUInt32BE(used, 12);
    out.writeUInt32BE(save.version >>> 0, 0x10);
    summary.copy(out, HEADER);
    main.copy(out, HEADER + summary.length);
    out.writeUInt32BE(crc32(out.subarray(0, used)), 0);
    return out;
}

// --- access -----------------------------------------------------------------------------------------------------

export function find(s: SaveStruct, name: number): Component | undefined {
    return s.components.find((c) => c.name === name);
}
