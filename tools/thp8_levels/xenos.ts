// THP8's texture dictionaries (.tex in the zone archives, Xbox 360) read into tools/level_port/tex.ts's THUG2 form.
//
// File (big-endian): u32 0xFACECAA7, u16 0x011C, u16 count, u32 offset of the records, u32 offset of the data (the
// first texture's is the 4 KB after it), u32 -1, u32 10, u32 0x1C, 0xEF filler; then 0x28 bytes per texture
// {u8 10, u8 0x28, u8 2, u8 ?, u32 checksum, u16 width, height, depth, width, height, depth, u8 levels, u8 bits per
// texel, u8 kind, u8 0, u32 offset of the packed mip levels, u32 offset of its Direct3D header, u32 x, u32 offset of
// the base level}. The Direct3D header
// (0x34 bytes: the Xbox 360's D3DBaseTexture) ends with the GPU's texture fetch constant (6 words at +0x1C), which
// says it all: tiled (word 0 bit 31), pitch (word 0 bits 22-30, x 32 texels), format (word 1 bits 0-5), endian
// swap (word 1 bits 6-7: 1 = bytes swapped in 16-bit words, 2 = in 32-bit words), width - 1 / height - 1 (word 2
// bits 0-12 / 13-25). (The fetch constant's layout is the Xenos GPU's - as documented by the Xbox 360 emulator
// projects; the tiling below is the well-known XGAddress2DTiledOffset arithmetic.)
//
// Formats used by THP8's levels: 0x12 DXT1, 0x13 DXT2/3, 0x14 DXT4/5, 0x31 DXN (two-channel normal maps), 0x06
// 8:8:8:8. The base level is untiled into THUG2's linear DXT / 32-bit layout; the mip levels are made from it
// (dxt.ts), not read (the Xbox packs its small levels into one tile).

import { FormatError } from "../level_port/binio.ts";
import type * as tex from "../level_port/tex.ts";
import * as dxt from "./dxt.ts";

export const MAGIC = 0xfacecaa7;
export const FMT_8888 = 0x06;
export const FMT_DXT1 = 0x12;
export const FMT_DXT3 = 0x13;
export const FMT_DXT5 = 0x14;
export const FMT_DXN = 0x31;

export interface XenosTexture {
    checksum: number;
    width: number;
    height: number;
    format: number;
    endian: number;
    tiled: boolean;
    pitch: number; // texels
    data: Buffer; // from the texture's data offset to the next one's (or the end)
}

export function readXenosTex(data: Buffer, name = "tex"): XenosTexture[] {
    if (data.readUInt32BE(0) !== MAGIC) throw new FormatError(`${name}: not a THP8 texture file`);
    const count = data.readUInt16BE(6);
    const records = data.readUInt32BE(8);
    const raw: { t: XenosTexture; at: number }[] = [];
    for (let i = 0; i < count; i++) {
        const r = records + 0x28 * i;
        const header = data.readUInt32BE(r + 0x1c);
        const f = (k: number): number => data.readUInt32BE(header + 0x1c + 4 * k);
        const at = data.readUInt32BE(r + 0x24); // (the base level's; +0x18: the packed mip levels', +0x20: x)
        const t: XenosTexture = {
            checksum: data.readUInt32BE(r + 4),
            width: (f(2) & 0x1fff) + 1,
            height: ((f(2) >>> 13) & 0x1fff) + 1,
            format: f(1) & 0x3f,
            endian: (f(1) >>> 6) & 3,
            tiled: Boolean(f(0) >>> 31),
            pitch: ((f(0) >>> 22) & 0x1ff) * 32,
            data: Buffer.alloc(0),
        };
        if (t.width !== data.readUInt16BE(r + 8) || t.height !== data.readUInt16BE(r + 10)) {
            throw new FormatError(`${name}: texture ${t.checksum.toString(16)}: record and fetch constant disagree`);
        }
        raw.push({ t, at });
    }
    for (const x of raw) {
        const size = tiledSize(x.t);
        if (x.at >= data.length) throw new FormatError(`${name}: texture ${x.t.checksum.toString(16)} past the end`);
        // (the last texture of a file may stop before its tile's padding: the rest is zeros)
        x.t.data =
            x.at + size <= data.length
                ? data.subarray(x.at, x.at + size)
                : Buffer.concat([data.subarray(x.at), Buffer.alloc(x.at + size - data.length)]);
    }
    return raw.map((x) => x.t);
}

const blockOf = (format: number): [number, number] =>
    format === FMT_8888 ? [1, 4] : format === FMT_DXT1 ? [4, 8] : [4, 16]; // [texels per block side, bytes per block]

// The bytes of a tiled base level: its blocks in a surface aligned to 32 x 32 blocks.
export function tiledSize(t: XenosTexture): number {
    const [block, bpb] = blockOf(t.format);
    const w = Math.max(Math.ceil(t.width / block), t.pitch / block);
    const h = Math.ceil(t.height / block);
    return ((w + 31) & ~31) * ((h + 31) & ~31) * bpb;
}

// XGAddress2DTiledOffset: the offset (in blocks / texels) of (x, y) in a tiled surface `width` (aligned to 32)
// wide, with `bpb` bytes per block / texel.
export function tiledOffset(x: number, y: number, width: number, bpb: number): number {
    const alignedWidth = (width + 31) & ~31;
    const logBpp = (bpb >> 2) + ((bpb >> 1) >> (bpb >> 2));
    const macro = ((x >> 5) + (y >> 5) * (alignedWidth >> 5)) << (logBpp + 7);
    const micro = ((x & 7) + ((y & 0xe) << 2)) << logBpp;
    const offset = macro + ((micro & ~0xf) << 1) + (micro & 0xf) + ((y & 1) << 4);
    return (
        (((offset & ~0x1ff) << 3) +
            ((y & 16) << 7) +
            ((offset & 0x1c0) << 2) +
            (((((y & 8) >> 2) + (x >> 3)) & 3) << 6) +
            (offset & 0x3f)) >>
        logBpp
    );
}

function swap(src: Buffer, endian: number): Buffer {
    const out = Buffer.from(src);
    if (endian === 1) out.swap16();
    else if (endian === 2) out.swap32();
    else if (endian === 3) {
        out.swap32();
        out.swap16(); // (16-in-32: the halves of each word swapped)
    }
    return out;
}

// The base level, linear: DXT blocks (8 / 16 bytes, row by row) or 32-bit texels (B, G, R, A).
export function baseLevel(t: XenosTexture): Buffer {
    const [block, bpb] = blockOf(t.format);
    const w = Math.ceil(t.width / block);
    const h = Math.ceil(t.height / block);
    const pitch = Math.max(t.pitch / block, w);
    const src = swap(t.data, t.endian);
    const out = Buffer.alloc(w * h * bpb);
    for (let y = 0; y < h; y++) {
        for (let x = 0; x < w; x++) {
            const from = (t.tiled ? tiledOffset(x, y, pitch, bpb) : y * pitch + x) * bpb;
            if (from + bpb > src.length) throw new FormatError(`texture ${t.checksum.toString(16)}: data too short`);
            src.copy(out, (y * w + x) * bpb, from, from + bpb);
        }
    }
    // (8:8:8:8: swapped back - the 32-bit words' bytes reversed - it is B, G, R, A already, Direct3D's A8R8G8B8 in
    // memory. Until 2026-10-22 it was turned round once more, as if A, R, G, B: every 8:8:8:8 texture came out with
    // its alpha as blue - the factory's light map a solid blue.)
    return out;
}

export interface Converted {
    texture: tex.Texture | null;
    note?: string;
}

// A texture we make (B, G, R, A texels) as THUG2's DXT5, with its mip chain down to 4 x 4 when `mips`.
export function fromBgra(checksum: number, width: number, height: number, bgra: Buffer, mips = true): tex.Texture {
    const levels = [dxt.encode(bgra, width, height, 5)];
    let [w, h, rgba] = [width, height, bgra];
    while (mips && Math.min(w, h) > 4) {
        rgba = dxt.halve(rgba, w, h);
        w = Math.max(1, w >> 1);
        h = Math.max(1, h >> 1);
        levels.push(dxt.encode(rgba, w, h, 5));
    }
    return { checksum, width, height, texelDepth: 32, paletteDepth: 32, dxt: 5, palette: Buffer.alloc(0), levels };
}

// Why toThug2 doesn't make a texture (null: it does).
export function unconverted(t: XenosTexture): string | null {
    if (t.format === FMT_DXN) return "DXN normal map";
    if (![FMT_DXT1, FMT_DXT3, FMT_DXT5, FMT_8888].includes(t.format)) return `format 0x${t.format.toString(16)}`;
    return null;
}

// THUG2's texture: DXT1 / DXT5 kept compressed (mips made by dxt.ts), DXT3 and 8:8:8:8 as 32-bit; DXN (normal maps)
// left out. `mips`: make the mip chain down to 4 x 4.
export function toThug2(t: XenosTexture, mips = true): Converted {
    const note = unconverted(t);
    if (note !== null) return { texture: null, note };
    const base = baseLevel(t);
    if (t.format === FMT_DXT1 || t.format === FMT_DXT5) {
        const kind = t.format === FMT_DXT1 ? 1 : 5;
        const levels = [base];
        if (mips) {
            let rgba = dxt.decode(base, t.width, t.height, kind);
            let [w, h] = [t.width, t.height];
            while (Math.min(w, h) > 4) {
                rgba = dxt.halve(rgba, w, h);
                w = Math.max(1, w >> 1);
                h = Math.max(1, h >> 1);
                levels.push(dxt.encode(rgba, w, h, kind));
            }
        }
        return {
            texture: {
                checksum: t.checksum,
                width: t.width,
                height: t.height,
                texelDepth: 32, // (as every texture of THUG2's own files, DXT or not)
                paletteDepth: 32,
                dxt: kind === 1 ? (dxt.hasAlpha1(base) ? 2 : 1) : 5,
                palette: Buffer.alloc(0),
                levels,
            },
        };
    }
    let bgra = t.format === FMT_8888 ? base : dxt.decode(base, t.width, t.height, 3);
    const levels = [bgra];
    let [w, h] = [t.width, t.height];
    while (mips && Math.min(w, h) > 4) {
        bgra = dxt.halve(bgra, w, h);
        w = Math.max(1, w >> 1);
        h = Math.max(1, h >> 1);
        levels.push(bgra);
    }
    return {
        texture: {
            checksum: t.checksum,
            width: t.width,
            height: t.height,
            texelDepth: 32,
            paletteDepth: 32,
            dxt: 0,
            palette: Buffer.alloc(0),
            levels,
        },
    };
}
