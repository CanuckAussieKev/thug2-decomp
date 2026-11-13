// NOT ORIGINAL - surface-map files (".pbr" beside a texture dictionary): the layout is src/formats/surface_map_format.h's
// (the game reads them; thug2_format_check checks the same sample as pbr_file.test.ts).
//   header: u32 "T2SM", u32 version 1, u32 count, u32 0
//   entries (32 bytes): u32 checksum, u64 content hash (FNV-1a 64 of the texture's first level as stored), u16 width,
//     u16 height, u8 class, u8 generator, u8 0, u8 0, f32 F0, u32 offset, u32 bytes
//   texels: RGBA rows top first - normal x (+u), normal y (+v) as 0 - 255 for -1 - 1, roughness, height.

export const MAGIC = 0x4d533254; // "T2SM"
export const VERSION = 1;
export const HEADER_SIZE = 16;
export const ENTRY_SIZE = 32;
export const SUFFIX = ".pbr";

export const GENERATOR_CLASSICAL = 0; // height from luminance, Sobel normals (classical.ts)
export const GENERATOR_CHORD = 1; // Ubisoft La Forge's CHORD model (chord.ts)

export interface MapEntry {
    checksum: number;
    contentHash: bigint;
    width: number;
    height: number;
    materialClass: number;
    generator: number;
    f0: number;
    texels: Uint8Array; // width * height * 4
}

const FNV_BASIS = 0xcbf29ce484222325n;
const FNV_PRIME = 0x100000001b3n;
const MASK64 = 0xffffffffffffffffn;

// 64-bit FNV-1a (the game's content hash: surface_map_format::content_hash).
export function contentHash(bytes: Uint8Array): bigint {
    // (two 32-bit halves: BigInt per byte is slow on whole levels)
    let hi = Number(FNV_BASIS >> 32n);
    let lo = Number(FNV_BASIS & 0xffffffffn);
    for (const byte of bytes) {
        lo = (lo ^ byte) >>> 0;
        // (hi:lo) * 0x100000001b3 = (hi:lo) * 0x1b3 + (lo << 40)
        const loTimes = lo * 0x1b3;
        const carry = Math.floor(loTimes / 0x100000000);
        const newLo = loTimes >>> 0;
        const newHi = (Math.imul(hi, 0x1b3) + carry + ((lo << 8) >>> 0)) >>> 0;
        hi = newHi;
        lo = newLo;
    }
    return ((BigInt(hi) << 32n) | BigInt(lo)) & MASK64;
}

// (the reference: BigInt, for the tests)
export function contentHashReference(bytes: Uint8Array): bigint {
    let hash = FNV_BASIS;
    for (const byte of bytes) hash = ((hash ^ BigInt(byte)) * FNV_PRIME) & MASK64;
    return hash;
}

export function writePbr(entries: readonly MapEntry[]): Buffer {
    let size = HEADER_SIZE + entries.length * ENTRY_SIZE;
    for (const e of entries) size += e.texels.length;
    const out = Buffer.alloc(size);
    out.writeUInt32LE(MAGIC, 0);
    out.writeUInt32LE(VERSION, 4);
    out.writeUInt32LE(entries.length, 8);
    out.writeUInt32LE(0, 12);
    let offset = HEADER_SIZE + entries.length * ENTRY_SIZE;
    entries.forEach((e, index) => {
        if (e.texels.length !== e.width * e.height * 4) throw new Error(`entry ${index}: texels don't match its size`);
        const at = HEADER_SIZE + index * ENTRY_SIZE;
        out.writeUInt32LE(e.checksum >>> 0, at);
        out.writeUInt32LE(Number(e.contentHash & 0xffffffffn), at + 4);
        out.writeUInt32LE(Number((e.contentHash >> 32n) & 0xffffffffn), at + 8);
        out.writeUInt16LE(e.width, at + 12);
        out.writeUInt16LE(e.height, at + 14);
        out.writeUInt8(e.materialClass, at + 16);
        out.writeUInt8(e.generator, at + 17);
        out.writeFloatLE(e.f0, at + 20);
        out.writeUInt32LE(offset, at + 24);
        out.writeUInt32LE(e.texels.length, at + 28);
        out.set(e.texels, offset);
        offset += e.texels.length;
    });
    return out;
}

export function readPbr(data: Uint8Array): MapEntry[] {
    const view = Buffer.from(data.buffer, data.byteOffset, data.byteLength);
    if (view.length < HEADER_SIZE || view.readUInt32LE(0) !== MAGIC || view.readUInt32LE(4) !== VERSION) {
        throw new Error("not a surface-map file");
    }
    const count = view.readUInt32LE(8);
    if (count > (view.length - HEADER_SIZE) / ENTRY_SIZE) throw new Error("more entries than the file holds");
    const entries: MapEntry[] = [];
    for (let index = 0; index < count; index++) {
        const at = HEADER_SIZE + index * ENTRY_SIZE;
        const width = view.readUInt16LE(at + 12);
        const height = view.readUInt16LE(at + 14);
        const offset = view.readUInt32LE(at + 24);
        const bytes = view.readUInt32LE(at + 28);
        if (bytes !== width * height * 4 || offset + bytes > view.length) throw new Error(`entry ${index}: damaged`);
        entries.push({
            checksum: view.readUInt32LE(at),
            contentHash: (BigInt(view.readUInt32LE(at + 8)) << 32n) | BigInt(view.readUInt32LE(at + 4)),
            width,
            height,
            materialClass: view.readUInt8(at + 16),
            generator: view.readUInt8(at + 17),
            f0: view.readFloatLE(at + 20),
            texels: new Uint8Array(view.subarray(offset, offset + bytes)),
        });
    }
    return entries;
}
