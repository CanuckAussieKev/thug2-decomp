// Tony Hawk's American Wasteland (PC, 1.01) level files, read into tools/level_port's forms. Nearly every file of the
// game ends in .wpc; the levels are in Game\data\worlds\worldzones\<zone>\<zone>[_net].pak.wpc.
//
// - Archives (.pak.wpc, + .pab.wpc): the newer Neversoft pak (the GH3 family), uncompressed. A table of 32-byte
//   entries {u32 extension checksum, u32 data offset (from the entry), u32 size, u32 pak name checksum, u32 full
//   name checksum, u32 name checksum, u32 0, u32 flags}; flags & 0x20: a 160-byte path follows the entry. The table
//   ends with a ".last" entry. With a .pab the .pak is only the table and the offsets run on into the .pab.
// - Textures (.tex): u32 0xABADD00D, u16 0x0401, u16 count, then per texture u32 0xABADD00D, u16 2, u16 20 (the
//   header size), u32 checksum, u16 width, height, width, height, u8 levels, u8 texel depth, u8 DXT, u8 0, then per
//   level {u32 size, data} (32-bit ones: {u16 pitch, u16 rows}, data): THUG2's .tex.xbx field for field.
// - Collision (.col): THUG2's version 10 with 48 more bytes after the 32-byte header: {u32 0, u32 0, i32 grid x,
//   i32 grid z, 4f box min, 4f box max} (a supersector grid THUG2 makes at load time). Without them THUG2's reader
//   (tools/level_port/thug2_col.ts) reads it and writes it back byte-identical.
// - QB (.qb, .nqb node arrays, .sqb scripts): item-based (GH3 family), below (QbItems). Scripts are LZSS'd THUG2
//   token streams with one more token, 0x4A (an inline structure in the item format). The names of the checksums
//   are in data\pak\dbg.pak.wpc (one .dbg text file per qb: "0x1234abcd Name" lines).
// - Scene (.geom): readGeom below.

import * as fs from "node:fs";
import { FormatError, Reader } from "../level_port/binio.ts";
import * as prx from "../level_port/prx.ts";
import * as qb from "../level_port/qb.ts";
import { checksum, Name, type QbValue, type Statement, Struct, Vector } from "../level_port/qb.ts";
import type * as tex from "../level_port/tex.ts";
import * as thug2Col from "../level_port/thug2_col.ts";
import { resolveInstall } from "../lib/install.ts";
import { decodeLatin1, Float, pyJoin, pyStrip } from "../lib/py.ts";
import { unpackNumbers } from "../lib/struct.ts";

const hex = (n: number): string => n.toString(16).toUpperCase();
const u32At = (data: Uint8Array, at: number): number => unpackNumbers("<I", data, at)[0]!;

// --- archives ---------------------------------------------------------------------------------------------------

const EXTENSIONS = [
    ".qb",
    ".nqb",
    ".sqb",
    ".mqb",
    ".tex",
    ".geom",
    ".col",
    ".mdl",
    ".skin",
    ".stex",
    ".img",
    ".pimg",
    ".ska",
    ".ske",
    ".rnb",
    ".shd",
    ".gap",
    ".pfx",
    ".dbg",
    ".last",
    "last",
];
const EXTENSION_OF = new Map(EXTENSIONS.map((e) => [checksum(e), e]));
export const PAK_NAMED = 0x20; // a 160-byte path follows the entry
export const PAK_SFX = 0x02; // the zone's _sfx sub-zone (sounds, almost no geometry)

export interface PakEntry {
    extension: string;
    offset: number;
    size: number;
    pak: number;
    full: number;
    name: number;
    flags: number;
    path: string | null;
}

// The entries of <x>.pak.wpc and the data they point into (the .pak followed by its .pab when there is one).
export function pakEntries(path: string): [PakEntry[], Buffer] {
    let data = fs.readFileSync(path);
    if (path.toLowerCase().endsWith(".pak.wpc")) {
        const pab = `${path.slice(0, -".pak.wpc".length)}.pab.wpc`;
        if (fs.existsSync(pab)) data = Buffer.concat([data, fs.readFileSync(pab)]);
    }
    const entries: PakEntry[] = [];
    let offset = 0;
    for (;;) {
        if (offset + 32 > data.length) throw new FormatError(`${path}: no .last entry`);
        const [ext, rel, size, pak, full, name, , flags] = unpackNumbers("<8I", data, offset) as [
            number,
            number,
            number,
            number,
            number,
            number,
            number,
            number,
        ];
        const e: PakEntry = {
            extension: EXTENSION_OF.get(ext) ?? ext.toString(16).padStart(8, "0"),
            offset: offset + rel,
            size,
            pak,
            full,
            name,
            flags,
            path: null,
        };
        let header = 32;
        if (flags & PAK_NAMED) {
            const field = data.subarray(offset + 32, offset + 192);
            const nul = field.indexOf(0);
            e.path = decodeLatin1(nul >= 0 ? field.subarray(0, nul) : field);
            header += 160;
        }
        if (e.extension === ".last" || e.extension === "last") break;
        if (e.offset + size > data.length) throw new FormatError(`${path}: entry at 0x${hex(offset)} past the end`);
        entries.push(e);
        offset += header;
    }
    return [entries, data];
}

// [entry, bytes] of a pak.
export function pakFiles(path: string): [PakEntry, Buffer][] {
    const [entries, data] = pakEntries(path);
    return entries.map((e) => [e, Buffer.from(data.subarray(e.offset, e.offset + e.size))]);
}

// --- checksum names (dbg.pak) -----------------------------------------------------------------------------------

const NAME_LINE = /0x([0-9a-fA-F]{8}) ([^\r\n]+)/g;

// Every checksum name of every .dbg file in data\pak\dbg.pak.wpc: checksum -> text.
export function loadNames(thaw: string): Map<number, string> {
    const names = new Map<number, string>();
    for (const [, data] of pakFiles(pyJoin(resolveInstall(thaw, "thaw"), "data", "pak", "dbg.pak.wpc"))) {
        for (const match of decodeLatin1(data).matchAll(NAME_LINE)) {
            const text = pyStrip(match[2]!);
            const value = Number.parseInt(match[1]!, 16);
            // (the first name of a checksum wins; one that doesn't hash to it is kept only when it's the first - the
            // checksum is taken only for a new one: a non-ASCII name raises there, as in Python)
            if (!names.has(value) && checksum(text) === value) names.set(value, text);
            else if (!names.has(value)) names.set(value, text);
        }
    }
    return names;
}

// --- textures ---------------------------------------------------------------------------------------------------

const TEX_MAGIC = 0xabadd00d;

// A level's size word: its bytes (DXT), or {u16 row pitch, u16 rows} (32-bit, uncompressed).
export function levelSize(value: number, dxt: number): number {
    return dxt ? value : (value & 0xffff) * (value >>> 16);
}

// A .tex (or a single .img) as tools/level_port/tex.ts's THUG2 texture dictionary.
export function readTex(data: Uint8Array, name = "tex"): tex.TexFile {
    const r = new Reader(data, 0, name);
    let [magic, version, count] = r.read("IHH") as [number, number, number];
    if (magic !== TEX_MAGIC) throw new FormatError(`${name}: not a THAW texture file`);
    if (version === 2) {
        // a single .img
        r.offset = 0;
        count = 1;
    }
    const textures: tex.Texture[] = [];
    for (let k = 0; k < count; k++) {
        let header: number;
        [magic, version, header] = r.read("IHH") as [number, number, number];
        if (magic !== TEX_MAGIC || version !== 2 || header !== 20) {
            throw new FormatError(
                `${name}: texture header ${hex(magic).padStart(8, "0")} ${version} ${header} at 0x${hex(r.offset)}`,
            );
        }
        const [crc, w, h, w2, h2, levels, depth, dxt] = r.read("IHHHHBBBB") as [
            number,
            number,
            number,
            number,
            number,
            number,
            number,
            number,
        ];
        if (w !== w2 || h !== h2 || ![0, 1, 2, 5].includes(dxt) || depth !== 32) {
            throw new FormatError(
                `${name}: texture ${hex(crc).padStart(8, "0")} ${w}x${h} (${w2}x${h2}) depth ${depth} DXT ${dxt}`,
            );
        }
        const data: Buffer[] = [];
        for (let l = 0; l < levels; l++) data.push(r.bytes(levelSize(r.u32(), dxt)));
        textures.push({
            checksum: crc,
            width: w,
            height: h,
            texelDepth: depth,
            paletteDepth: 32,
            dxt,
            palette: Buffer.alloc(0),
            levels: data,
        });
    }
    r.expectEnd();
    return { version: 1, textures };
}

// --- collision --------------------------------------------------------------------------------------------------

const COL_GRID = 48; // the bytes after the 32-byte header that THUG2's files don't have

export function stripCol(data: Uint8Array): Buffer {
    const b = Buffer.from(data.buffer, data.byteOffset, data.byteLength);
    return Buffer.concat([b.subarray(0, 32), b.subarray(32 + COL_GRID)]);
}

export function readCol(data: Uint8Array, name = "col"): thug2Col.ColFile {
    return thug2Col.readCol(stripCol(data), name);
}

// --- qb ---------------------------------------------------------------------------------------------------------
//
// u32 0, u32 file size, 20 fixed bytes (1C 08 02 04 10 04 08 0C 0C 08 02 04 14 02 04 0C 10 10 0C 00), then the
// items one after another, each {u8 0, u8 flags (4 / 0x20), u8 type, u8 0, u32 name, u32 file name checksum,
// u32 value, u32 0} with the value's data right after it (offsets are from the start of the file):
//   1 int, 2 float, 0x0D checksum, 0x1A a reference to a global ($name): the value itself
//   3 string, 4 local string ('text'): offset of the text (zero-terminated, padded to 4)
//   5 / 6 pair / vector: offset of {u32 0x00010000, 2f / 3f}
//   7 script: offset of {u32 checksum, u32 size, u32 packed size, the LZSS'd tokens padded to 4} (packed size ==
//     size: stored)
//   0x0A structure: offset of {u32 0x00010000, u32 offset of the first entry}; entries are 16 bytes {u8 0,
//     u8 type << 1 | 1, u16 0, u32 key (0: a flag / unnamed value), u32 value (as above by type), u32 next entry}
//   0x0C array: offset of {u8 0, u8 1, u8 element type, u8 0, u32 count, u32 x}: x = the single element's value
//     (one element, an inline type) or its offset (one element of a type with data), else the offset of count
//     u32 values / offsets.
const T_INT = 1;
const T_FLOAT = 2;
const T_STRING = 3;
const T_LOCAL = 4;
const T_PAIR = 5;
const T_VECTOR = 6;
const T_SCRIPT = 7;
const T_STRUCT = 0x0a;
const T_ARRAY = 0x0c;
const T_NAME = 0x0d;
const T_REF = 0x1a;
const QB_HEADER = 0x1c;

// $name: a reference to a global. (Not one of qb.ts's QbValue forms: it sits in the values as Python's Ref did, an
// object none of level_port's instanceof checks take - `asValue`.)
export class Ref {
    readonly value: number;
    constructor(value: number) {
        this.value = value;
    }
}

// A Ref where qb.ts's values go (see Ref).
const asValue = (ref: Ref): QbValue => ref as unknown as QbValue;

// The item format's value reader (QbItems, and the scripts' inline structures: inlineStruct).
export class ItemReader {
    readonly name: string;
    readonly data: Buffer;
    readonly names: Map<number, string>;
    // the byte order of the item format's words: "<" (THAW's PC files), ">" (THP8's Xbox 360 files - tools/thp8_levels;
    // their script bodies stay little-endian THUG2 tokens)
    readonly order: "<" | ">";
    end = 0;

    constructor(data: Uint8Array, name: string, names: Map<number, string>, order: "<" | ">" = "<") {
        this.data = Buffer.isBuffer(data) ? data : Buffer.from(data.buffer, data.byteOffset, data.byteLength);
        this.name = name;
        this.names = names;
        this.order = order;
    }

    protected u32(at: number): number {
        return unpackNumbers(`${this.order}I`, this.data, at)[0]!;
    }

    protected reach(at: number): void {
        this.end = Math.max(this.end, at);
    }

    value(kind: number, v: number): QbValue {
        const d = this.data;
        if (kind === T_INT) return v | 0;
        if (kind === T_FLOAT) return new Float(unpackNumbers("<f", Buffer.from(Uint32Array.of(v).buffer))[0]!);
        if (kind === T_NAME) return new Name(v);
        if (kind === T_REF) return asValue(new Ref(v));
        if (kind === T_LOCAL && this.order === ">") {
            // (THP8: a wide string - UTF-16, big-endian, up to a 0 character)
            let end = v;
            while (end + 1 < d.length && (d[end] || d[end + 1])) end += 2;
            if (end + 1 >= d.length) throw new RangeError("subsection not found");
            this.reach(end + 2);
            return new qb.Local(Buffer.from(d.subarray(v, end)).swap16().toString("utf16le"));
        }
        if (kind === T_STRING || kind === T_LOCAL) {
            const end = d.indexOf(0, v);
            if (end < 0) throw new RangeError("subsection not found"); // (Python's bytes.index)
            this.reach(end + 1);
            const text = decodeLatin1(d.subarray(v, end));
            return kind === T_STRING ? text : new qb.Local(text);
        }
        if (kind === T_PAIR || kind === T_VECTOR) {
            const n = kind === T_PAIR ? 2 : 3;
            this.reach(v + 4 + 4 * n);
            return new Vector(n === 2 ? "pair" : "vector", unpackNumbers(`${this.order}${n}f`, d, v + 4));
        }
        if (kind === T_STRUCT) return this.struct(v);
        if (kind === T_ARRAY) return this.array(v);
        throw new FormatError(`${this.name}: value type 0x${hex(kind)}`);
    }

    struct(at: number): Struct {
        this.reach(at + 8);
        const entries: qb.Entry[] = [];
        let item = this.u32(at + 4);
        while (item) {
            const kindByte = this.data[item + 1];
            if (kindByte === undefined) throw new RangeError("index out of range");
            const [key, v, next] = unpackNumbers(`${this.order}3I`, this.data, item + 4) as [number, number, number];
            this.reach(item + 16);
            if (this.order === ">") {
                // (THP8: 0x80 | type; an unnamed type 0x1A - a reference to a global: the node arrays' includes - is
                // taken as THUG1's include, an unnamed checksum)
                if (!(kindByte & 0x80)) {
                    throw new FormatError(
                        `${this.name}: structure entry type byte ${hex(kindByte).padStart(2, "0")} at 0x${hex(item)}`,
                    );
                }
                const kind = kindByte & 0x7f;
                if (kind === T_REF && key === 0) entries.push([null, new Name(v)]);
                else entries.push([key ? new Name(key) : null, this.value(kind, v)]);
                item = next;
                continue;
            }
            if (!(kindByte & 1)) {
                throw new FormatError(
                    `${this.name}: structure entry type byte ${hex(kindByte).padStart(2, "0")} at 0x${hex(item)}`,
                );
            }
            entries.push([key ? new Name(key) : null, this.value(kindByte >> 1, v)]);
            item = next;
        }
        return new Struct(entries);
    }

    array(at: number): QbValue[] {
        const kind = this.data[at + 2];
        if (kind === undefined) throw new RangeError("index out of range");
        const count = this.u32(at + 4);
        const x = this.u32(at + 8);
        this.reach(at + 12);
        if (count === 0) return [];
        if (count === 1) return [this.value(kind, x)];
        const values = unpackNumbers(`${this.order}${count}I`, this.data, x);
        this.reach(x + 4 * count);
        return values.map((v) => this.value(kind, v));
    }
}

// A THAW qb file read into tools/level_port/qb.ts's values (globals, names, scripts as statements), so that
// level_port's node code (nodes.ts) takes it as a THUG1 QbFile.
export class QbItems extends ItemReader {
    readonly globals: [Name, QbValue][] = [];
    readonly scripts = new Map<number, Statement[]>();
    readonly scriptData = new Map<number, Buffer>();

    constructor(data: Uint8Array, name = "qb", names?: Map<number, string>, order: "<" | ">" = "<") {
        super(data, name, names ?? new Map(), order);
        const d = this.data;
        const size = this.u32(4);
        if (size !== d.length) throw new FormatError(`${name}: size ${size} in a file of ${d.length}`);
        let offset = QB_HEADER;
        while (offset < d.length) {
            if (offset + 20 > d.length) throw new FormatError(`${name}: item past the end at 0x${hex(offset)}`);
            const [zero, , kind, zero2] = d.subarray(offset, offset + 4) as unknown as [number, number, number, number];
            const [key, , value] = unpackNumbers(`${this.order}4I`, d, offset + 4) as [number, number, number, number];
            if (zero || zero2) {
                throw new FormatError(
                    `${name}: item header ${d.subarray(offset, offset + 4).toString("hex")} at 0x${hex(offset)}`,
                );
            }
            this.end = offset + 20;
            if (kind === T_SCRIPT) {
                const [, usize, csize] = unpackNumbers(`${this.order}3I`, d, value) as [number, number, number];
                const packed = d.subarray(value + 12, value + 12 + csize);
                const body = csize !== usize ? prx.lzssDecode(packed, usize) : Buffer.from(packed);
                if (body.length !== usize) {
                    throw new FormatError(
                        `${name}: script ${hex(key).padStart(8, "0")} unpacks to ${body.length} of ${usize} bytes`,
                    );
                }
                this.scriptData.set(key, body);
                this.scripts.set(key, statements(body, name));
                this.end = value + 12 + csize;
            } else {
                this.globals.push([new Name(key), this.value(kind, value)]);
            }
            offset = (this.end + 3) & ~3;
        }
    }

    get(name: string): QbValue | undefined {
        const k = checksum(name);
        for (const [key, value] of this.globals) if (key.value === k) return value;
        return undefined;
    }

    calls(script: number): [number, Statement][] {
        const out: [number, Statement][] = [];
        for (const line of this.scripts.get(script) ?? []) {
            if (line.length && line[0]![0] === 0x16) {
                if (line[0]![1].length !== 4) throw new RangeError("unpack requires a buffer of 4 bytes");
                out.push([u32At(line[0]![1], 0), line]);
            }
        }
        return out;
    }
}

// A 0x4A token's structure (THAW's item format, offsets from its own start).
export function inlineStruct(payload: Uint8Array, names: Map<number, string>, order: "<" | ">" = "<"): Struct {
    return new ItemReader(payload, "inline structure", names, order).struct(0);
}

// Script tokens: THUG2's (src/game/script/qb.h) with weighted random tokens, plus 0x4A {u16 size, padding to 4 from
// the script's start, a structure in the item format (offsets from its own start)}.
const FIXED = qb.FIXED;

// [offset, token, payload] of a script body's tokens.
export function* tokens(input: Uint8Array): Generator<qb.Token> {
    const data = Buffer.isBuffer(input) ? input : Buffer.from(input.buffer, input.byteOffset, input.byteLength);
    let i = 0;
    const n = data.length;
    while (i < n) {
        const t = data[i]!;
        const start = i;
        i += 1;
        let payload: Buffer;
        const fixed = FIXED.get(t);
        if (fixed !== undefined) {
            payload = data.subarray(i, i + fixed);
            i += fixed;
        } else if (t === 0x1b || t === 0x1c) {
            const length = u32At(data, i);
            payload = data.subarray(i + 4, i + 4 + length);
            i += 4 + length;
        } else if (t === 0x2f || t === 0x37 || t === 0x40 || t === 0x41) {
            const count = u32At(data, i);
            const size = 4 + 6 * count;
            payload = data.subarray(i, i + size);
            i += size;
        } else if (t === 0x4a) {
            const size = unpackNumbers("<H", data, i)[0]!;
            i = (i + 2 + 3) & ~3;
            payload = data.subarray(i, i + size);
            i += size;
        } else {
            payload = data.subarray(0, 0);
        }
        yield [start, t, payload];
        if (t === 0x24 && i >= n) break;
    }
}

// The script's statements (lines), each a list of [token, payload] - as level_port's QbFile.scripts.
export function statements(body: Uint8Array, _name = "script"): Statement[] {
    const out: Statement[] = [];
    let line: Statement = [];
    for (const [, t, payload] of tokens(body)) {
        if (t === 0x01 || t === 0x02) {
            if (line.length) out.push(line);
            line = [];
        } else if (t === 0x24) {
            break;
        } else {
            line.push([t, payload]);
        }
    }
    if (line.length) out.push(line);
    return out;
}

// --- scene (.geom) ----------------------------------------------------------------------------------------------
//
// A memory image the loader (THAW.exe 0x005083B0) fixes up in place:
//   0x00  u32 0, 7 x 0xCABAAAFA
//   0x20  materials (0x00530470): u16 0x1002, u16 count, u32 size of the block (from 0x20), u32 16 (offset of the
//         first), u32 -1; then 0x120 bytes per material (MAT_*), then their extra data (uv wibbles, colour
//         wibbles, texture animations - offsets in the materials are from 0x20, -1 = none)
//   then the scene; S = its start + 8 + its second word (+0x10), offsets below are from S:
//   +0x00 u32 0xBABEFACE, u32 8, ...
//   S+0x80 sector count, S+0x84 sectors, S+0x88 sector nodes, S+0x90 0x14 bytes per sector (runtime),
//   S+0x94 meshes (the loader's pointers: offsets in the file)
//   sectors     0x30 bytes: u32 0 (vtable), u32 checksum, u32 flags (SEC_*), rest runtime
//   nodes       0x68 bytes per sector: +0x18 box (4f min, 4f max), +0x44 mesh count (the sector's meshes are the
//               next ones in the mesh list)
//   meshes      0xE0 bytes (MESH_*): sphere, material checksum, vertex stride, vertex / index counts, the FVF
//   0xAA filler (0-31 bytes), then per mesh: (sector flag 0x800: a colour wibble byte per vertex), the vertex buffers {u32 size,
//   data} (1, or 2 in the colour wibble sectors), aligned to 4, {u32 size, u16 indices: one triangle strip with
//   degenerate joins}, aligned to 4. The file ends there.
//
// Vertices are Direct3D FVF vertices (position, normal, D3DCOLOR (128 = full), uv sets): what THUG2's meshes take.

const GEOM_SCENE = 0xbabeface;
const GEOM_MESH_TAG = 0x40; // set in every mesh record's first word (its flags: 0x20000 as THUG2's meshes, 0x100 ...)
const MAT_SIZE = 0x120;
const SEC_COLOR_WIBBLE = 0x800;

// Material record (0x120 bytes): offsets and per-pass arrays (4 passes at most).
const MAT_CHECKSUM = 0x00;
const MAT_NAME = 0x04;
const MAT_PASSES = 0x08;
const MAT_DOUBLE_SIDED = 0x0d; // u8 (set on most of the transparent ones: fences, foliage)
const MAT_ALPHA_CUTOFF = 0x10; // u8
const MAT_Z_BIAS = 0x11; // u8
const MAT_DRAW_ORDER = 0x2c; // f32
const MAT_PASS_FLAGS = 0x30; // u32 x4: THUG2's pass flags (textured 4, smooth 0x20, transparent 0x40, ...)
const MAT_TEXTURES = 0x40; // u32 x4
const MAT_COLORS = 0x50; // 4f x4: rgb, fixed alpha / 128
const MAT_BLEND = 0x90; // u32 x4: THUG2's blend mode | fixed alpha << 24
const MAT_ADDRESS = 0xa0; // u16 u, u16 v x4
const MAT_ENV_TILING = 0xb0; // 2f x4
const MAT_FILTERING = 0xd0; // u32 x4 (as THUG2's: 0x00010004)
const MAT_UV_WIBBLE = 0xe0; // u32 x4: offset of 12 floats (THUG2's 8 first), -1 none
// (0xF0: f32 x4, the mip bias - not used here)

export interface GeomPass {
    flags: number;
    texture: number;
    color: number[]; // 4f
    blend: number;
    address: number[]; // 2H
    envTiling: number[]; // 2f
    filtering: number;
    uvWibble?: number[]; // 12f
}

export interface GeomMaterial {
    checksum: number;
    nameChecksum: number;
    doubleSided: number;
    alphaCutoff: number;
    zBias: number;
    drawOrder: number;
    passes: GeomPass[];
}

export interface GeomMesh {
    sphere: number[]; // 4f
    material: number;
    stride: number;
    fvf: number;
    vertexCount: number;
    vertices: Buffer; // the first buffer
    buffers: number;
    strip: number[]; // u16
}

export interface GeomSector {
    checksum: number;
    flags: number;
    bbox: number[]; // 6f
    meshes: GeomMesh[];
}

export interface Geom {
    materials: GeomMaterial[];
    sectors: GeomSector[];
}

function byteAt(data: Buffer, at: number): number {
    const b = data[at];
    if (b === undefined) throw new RangeError("index out of range");
    return b;
}

export function readMaterial(data: Buffer, at: number, block: number): GeomMaterial {
    const u32 = (off: number): number => u32At(data, at + off);
    const m: GeomMaterial = {
        checksum: u32(MAT_CHECKSUM),
        nameChecksum: u32(MAT_NAME),
        doubleSided: byteAt(data, at + MAT_DOUBLE_SIDED),
        alphaCutoff: byteAt(data, at + MAT_ALPHA_CUTOFF),
        zBias: byteAt(data, at + MAT_Z_BIAS),
        drawOrder: unpackNumbers("<f", data, at + MAT_DRAW_ORDER)[0]!,
        passes: [],
    };
    const count = u32(MAT_PASSES);
    if (!(count >= 1 && count <= 4)) {
        throw new FormatError(`material ${hex(m.checksum).padStart(8, "0")}: ${count} passes`);
    }
    for (let i = 0; i < count; i++) {
        const p: GeomPass = {
            flags: u32(MAT_PASS_FLAGS + 4 * i),
            texture: u32(MAT_TEXTURES + 4 * i),
            color: unpackNumbers("<4f", data, at + MAT_COLORS + 16 * i),
            blend: u32(MAT_BLEND + 4 * i),
            address: unpackNumbers("<2H", data, at + MAT_ADDRESS + 4 * i),
            envTiling: unpackNumbers("<2f", data, at + MAT_ENV_TILING + 8 * i),
            filtering: u32(MAT_FILTERING + 4 * i),
        };
        const wibble = u32(MAT_UV_WIBBLE + 4 * i);
        if (wibble !== 0xffffffff) p.uvWibble = unpackNumbers("<12f", data, block + wibble);
        m.passes.push(p);
    }
    return m;
}

// Mesh record (0xE0 bytes).
const MESH_SPHERE = 0x04; // 4f
const MESH_MATERIAL = 0x14; // u32 material checksum
const MESH_STRIDE = 0x18; // u8
const MESH_VERTICES = 0x26; // u16
const MESH_INDICES = 0x28; // u32
const MESH_FVF = 0x40; // u32

// The scene: materials (readMaterial) and sectors {checksum, flags, bbox (6f), meshes: [{sphere, material, fvf,
// stride, vertexCount, vertices (the first buffer), buffers, strip (u16s)}]}.
export function readGeom(input: Uint8Array, name = "geom"): Geom {
    const data = Buffer.isBuffer(input) ? input : Buffer.from(input.buffer, input.byteOffset, input.byteLength);
    const u32 = (off: number): number => u32At(data, off);
    const [version, count, size] = unpackNumbers("<HHI", data, 0x20) as [number, number, number];
    if (version !== 0x1002) throw new FormatError(`${name}: material block version ${hex(version)}`);
    const first = 0x20 + u32(0x28);
    const materials: GeomMaterial[] = [];
    for (let i = 0; i < count; i++) materials.push(readMaterial(data, first + i * MAT_SIZE, 0x20));
    const base = 0x20 + size;
    if (u32(base) !== GEOM_SCENE) throw new FormatError(`${name}: no scene (0xBABEFACE) at 0x${hex(base)}`);
    const s = base + 8 + u32(base + 4);
    let [sectorCount, sectorsAt, nodesAt, , , meshesAt] = unpackNumbers("<i5I", data, s + 0x80) as [
        number,
        number,
        number,
        number,
        number,
        number,
    ];
    sectorsAt += s;
    nodesAt += s;
    meshesAt += s;
    const sectors: GeomSector[] = [];
    const meshCounts: number[] = [];
    for (let i = 0; i < sectorCount; i++) {
        const [, sectorChecksum, flags] = unpackNumbers("<3I", data, sectorsAt + 0x30 * i) as [number, number, number];
        const node = nodesAt + 0x68 * i;
        const lo = unpackNumbers("<3f", data, node + 0x18);
        const hi = unpackNumbers("<3f", data, node + 0x28);
        sectors.push({ checksum: sectorChecksum, flags, bbox: [...lo, ...hi], meshes: [] });
        meshCounts.push(u32(node + 0x44));
    }
    const total = meshCounts.reduce((a, b) => a + b, 0);
    let at = meshesAt + 0xe0 * total;
    while (at < data.length && data[at] === 0xaa) at += 1; // (filler up to the data's alignment in its pak)
    let index = 0;
    sectors.forEach((sec, k) => {
        for (let n = 0; n < meshCounts[k]!; n++) {
            const rec = meshesAt + 0xe0 * index;
            index += 1;
            if (!(u32(rec) & GEOM_MESH_TAG) || u32(rec) & 0xfff0fe00) {
                throw new FormatError(`${name}: mesh ${index - 1} tag ${hex(u32(rec)).padStart(8, "0")}`);
            }
            const sphere = unpackNumbers("<4f", data, rec + MESH_SPHERE);
            const material = u32(rec + MESH_MATERIAL);
            const stride = byteAt(data, rec + MESH_STRIDE);
            const fvf = u32(rec + MESH_FVF);
            const vertexCount = unpackNumbers("<H", data, rec + MESH_VERTICES)[0]!;
            const indices = u32(rec + MESH_INDICES);
            const vsize = vertexCount * stride;
            if (sec.flags & SEC_COLOR_WIBBLE) at += vertexCount;
            const buffers: Buffer[] = [];
            for (;;) {
                if (u32(at) !== vsize) {
                    throw new FormatError(
                        `${name}: mesh ${index - 1}: vertex buffer of ${u32(at)} bytes at 0x${hex(at)}, not ${vsize}`,
                    );
                }
                buffers.push(Buffer.from(data.subarray(at + 4, at + 4 + vsize)));
                at += 4 + vsize;
                // (the colour wibble sectors' meshes have a second buffer of the same size)
                if (!(sec.flags & SEC_COLOR_WIBBLE) || buffers.length === 2) break;
            }
            at = (at + 3) & ~3;
            if (u32(at) !== 2 * indices) {
                throw new FormatError(
                    `${name}: mesh ${index - 1}: ${u32(at)} index bytes at 0x${hex(at)}, not ${2 * indices}`,
                );
            }
            const strip = unpackNumbers(`<${indices}H`, data, at + 4);
            at = (at + 4 + 2 * indices + 3) & ~3;
            sec.meshes.push({
                sphere,
                material,
                stride,
                fvf,
                vertexCount,
                vertices: buffers[0]!,
                buffers: buffers.length,
                strip,
            });
        }
    });
    if (at !== data.length && (data.length - at >= 32 || data.subarray(at).some((b) => b !== 0xaa))) {
        throw new FormatError(`${name}: stopped at 0x${hex(at)} of 0x${hex(data.length)}`);
    }
    return { materials, sectors };
}
