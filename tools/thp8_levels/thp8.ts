// Tony Hawk's Project 8 (Xbox 360) data files, read into tools/level_port's forms. Everything is big-endian except
// the script bodies (THUG2's little-endian token streams). The files are under game\DATA:
//
// - COMPRESSED\...\<x>.xen: each file one raw deflate stream (no zlib header). compress.toc.xen lists them:
//   "TOC1", u32 0x8FF, u32 0x20 (header size), u32 0x00E90020, 16 bytes of 0, then 16-byte entries {u32 checksum
//   of the file's path, u32 unpacked size, u32 packed size, u32 0}; compress.toc.lst.xen is the same list as text
//   ("Name: <path> <packed size> <checksum>"). The archives are read by unpacking the file itself (unpack below).
// - Archives (.pak.xen, + .pab.xen): THAW's pak (tools/thaw_levels/thaw.ts) big-endian: 32-byte entries {u32
//   extension checksum, u32 data offset (from the entry), u32 size, u32 pak name checksum, u32 full name checksum,
//   u32 name checksum, u32 (a checksum: the scene's / collision's own), u32 flags}; flags & 0x20: a 160-byte path
//   follows. The table ends with a "last" entry. A zone's archive holds the zone and its sub-zones (flags 2, 4...).
// - QB (.qb, .nqb): THAW's item format with big-endian words (thaw.ts QbItems, order ">"); scripts LZSS'd THUG2
//   tokens. The checksum names: PAK\dbg.pak.xen's .dbg files ("0x1234abcd Name" lines).
// - Textures (.tex), scenes (.geom), collision (.hkc, Havok 4.0 packfiles): xenos.ts, geom.ts, havok.ts.

import * as fs from "node:fs";
import * as zlib from "node:zlib";
import { FormatError } from "../level_port/binio.ts";
import { checksum } from "../level_port/qb.ts";
import { decodeLatin1, pyStrip } from "../lib/py.ts";

const hex = (n: number): string => n.toString(16).toUpperCase().padStart(8, "0");

// --- compression ------------------------------------------------------------------------------------------------

// A .xen file of COMPRESSED: its bytes (one raw deflate stream).
export function unpack(path: string): Buffer {
    const packed = fs.readFileSync(path);
    try {
        return zlib.inflateRawSync(packed);
    } catch (e) {
        throw new FormatError(`${path}: not a deflate stream (${(e as Error).message})`);
    }
}

export interface TocEntry {
    checksum: number; // of the path (lower case, back slashes: "zones\\z_houses.pak.xen")
    size: number; // unpacked
    packed: number;
}

export const TOC_MAGIC = "TOC1";

// compress.toc.xen's entries.
export function readToc(data: Buffer, name = "compress.toc.xen"): TocEntry[] {
    if (data.subarray(0, 4).toString("latin1") !== TOC_MAGIC) throw new FormatError(`${name}: not a TOC1 file`);
    const header = data.readUInt32BE(8);
    const out: TocEntry[] = [];
    for (let at = header; at + 16 <= data.length; at += 16) {
        const key = data.readUInt32BE(at);
        if (!key) continue; // (the padding at the end)
        out.push({ checksum: key, size: data.readUInt32BE(at + 4), packed: data.readUInt32BE(at + 8) });
    }
    return out;
}

// compress.toc.lst.xen: path -> [packed size, checksum].
export function readTocList(text: string): Map<string, [number, number]> {
    const out = new Map<string, [number, number]>();
    for (const m of text.matchAll(/^Name: (\S+) (\d+) ([0-9a-f]+)\r?$/gm)) {
        out.set(m[1]!.toLowerCase(), [Number(m[2]), Number.parseInt(m[3]!, 16)]);
    }
    return out;
}

// --- archives ---------------------------------------------------------------------------------------------------

const EXTENSIONS = [
    ".qb",
    ".nqb",
    ".sqb",
    ".mqb",
    ".tex",
    ".geom",
    ".col",
    ".hkc",
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
export const PAK_NAMED = 0x20;

export interface PakEntry {
    extension: string; // ".geom", or the checksum in hex when it isn't a known one
    offset: number;
    size: number;
    pak: number; // the archive's name checksum
    full: number; // the file's full name checksum
    name: number; // the (sub-)zone's name checksum
    own: number; // the file's own checksum (the scene's / collision's / node array's)
    flags: number;
    path: string | null;
}

export interface Pak {
    entries: PakEntry[];
    data: Buffer;
}

// A pak's entries and data (unpacked: the .pak.xen followed by its .pab.xen when there is one).
export function readPak(data: Buffer, name = "pak"): Pak {
    const entries: PakEntry[] = [];
    let offset = 0;
    for (;;) {
        if (offset + 32 > data.length) throw new FormatError(`${name}: no last entry`);
        const word = (k: number): number => data.readUInt32BE(offset + 4 * k);
        const e: PakEntry = {
            extension: EXTENSION_OF.get(word(0)) ?? word(0).toString(16).padStart(8, "0"),
            offset: offset + word(1),
            size: word(2),
            pak: word(3),
            full: word(4),
            name: word(5),
            own: word(6),
            flags: word(7),
            path: null,
        };
        let header = 32;
        if (e.flags & PAK_NAMED) {
            const field = data.subarray(offset + 32, offset + 192);
            const nul = field.indexOf(0);
            e.path = decodeLatin1(nul >= 0 ? field.subarray(0, nul) : field);
            header += 160;
        }
        if (e.extension === ".last" || e.extension === "last") break;
        if (e.offset + e.size > data.length) throw new FormatError(`${name}: entry at 0x${offset.toString(16)} past the end`);
        entries.push(e);
        offset += header;
    }
    return { entries, data };
}

export function openPak(path: string): Pak {
    let data = unpack(path);
    const pab = path.replace(/\.pak\.xen$/i, ".pab.xen");
    if (pab !== path && fs.existsSync(pab)) data = Buffer.concat([data, unpack(pab)]);
    return readPak(data, path);
}

export function entryData(pak: Pak, e: PakEntry): Buffer {
    return pak.data.subarray(e.offset, e.offset + e.size);
}

// --- checksum names ---------------------------------------------------------------------------------------------

const NAME_LINE = /0x([0-9a-fA-F]{8}) ([^\r\n]+)/g;

// Every name of every .dbg file of PAK\dbg.pak.xen (checksum -> text; the first one that hashes to it wins, else the
// first one).
export function loadNames(dbgPak: string): Map<number, string> {
    const names = new Map<number, string>();
    const unhashed = new Map<number, string>();
    const pak = openPak(dbgPak);
    for (const e of pak.entries) {
        for (const match of decodeLatin1(entryData(pak, e)).matchAll(NAME_LINE)) {
            const text = pyStrip(match[2]!);
            const value = Number.parseInt(match[1]!, 16);
            if (names.has(value)) continue;
            let ok = false;
            try {
                ok = checksum(text) === value;
            } catch {
                ok = false;
            }
            if (ok) names.set(value, text);
            else if (!unhashed.has(value)) unhashed.set(value, text);
        }
    }
    for (const [k, v] of unhashed) if (!names.has(k)) names.set(k, v);
    return names;
}

export { hex };
