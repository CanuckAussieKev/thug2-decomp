// NOT ORIGINAL - the building-relief file (src/formats/relief_format.h): beside a level texture dictionary, its name +
// ".relief", the facade structure (facades.ts) of each of its textures that has one, for the game's relief geometry
// ([Graphics] building_relief).
//
// The file (little-endian): u32 magic "T2RF", u32 version (1), u32 count, u32 the detector's version, u64 the sources'
// hash (the dictionary's and the texture set's bytes, for `make` to skip a dictionary already made from them); then
// `count` entries of 24 bytes -
//   u32 the texture's checksum, u64 its content hash (pbr_file.ts contentHash of its first level as the game's own
//   dictionary stores it), u16 repeats across, u16 repeats down (the cell's), u32 its first rectangle, u32 how many
// - and the rectangles, 24 bytes each: u8 kind (1 band, 2 frame, 3 sill, 4 window), 3 bytes 0, f32 u0, v0, u1, v1
// (fractions of the cell; u1 / v1 may pass 1), f32 height (texture widths; + out of the wall).
import { type Facade, KIND_CODE, type RectKind, type ReliefRect } from "./facades.ts";

export const MAGIC = 0x46523254; // "T2RF"
export const VERSION = 1;
export const HEADER_SIZE = 24;
export const ENTRY_SIZE = 24;
export const RECT_SIZE = 24;
export const SUFFIX = ".relief";

export interface ReliefEntry {
    checksum: number;
    contentHash: bigint;
    facade: Pick<Facade, "repeatU" | "repeatV" | "rects">;
}

export interface ReliefFile {
    detector: number;
    sources: bigint;
    entries: ReliefEntry[];
}

const KIND_OF = new Map<number, RectKind>(Object.entries(KIND_CODE).map(([k, v]) => [v, k as RectKind]));

export function writeRelief(file: ReliefFile): Buffer {
    const rects = file.entries.reduce((sum, e) => sum + e.facade.rects.length, 0);
    const out = Buffer.alloc(HEADER_SIZE + file.entries.length * ENTRY_SIZE + rects * RECT_SIZE);
    out.writeUInt32LE(MAGIC, 0);
    out.writeUInt32LE(VERSION, 4);
    out.writeUInt32LE(file.entries.length, 8);
    out.writeUInt32LE(file.detector >>> 0, 12);
    out.writeBigUInt64LE(file.sources, 16);
    let rect = 0;
    const rectsAt = HEADER_SIZE + file.entries.length * ENTRY_SIZE;
    file.entries.forEach((e, index) => {
        const at = HEADER_SIZE + index * ENTRY_SIZE;
        out.writeUInt32LE(e.checksum >>> 0, at);
        out.writeBigUInt64LE(e.contentHash, at + 4);
        out.writeUInt16LE(e.facade.repeatU, at + 12);
        out.writeUInt16LE(e.facade.repeatV, at + 14);
        out.writeUInt32LE(rect, at + 16);
        out.writeUInt32LE(e.facade.rects.length, at + 20);
        for (const r of e.facade.rects) {
            const o = rectsAt + rect * RECT_SIZE;
            out.writeUInt8(KIND_CODE[r.kind], o);
            out.writeFloatLE(r.u0, o + 4);
            out.writeFloatLE(r.v0, o + 8);
            out.writeFloatLE(r.u1, o + 12);
            out.writeFloatLE(r.v1, o + 16);
            out.writeFloatLE(r.height, o + 20);
            rect++;
        }
    });
    return out;
}

export function readRelief(data: Uint8Array): ReliefFile {
    const view = Buffer.from(data.buffer, data.byteOffset, data.byteLength);
    if (view.length < HEADER_SIZE || view.readUInt32LE(0) !== MAGIC || view.readUInt32LE(4) !== VERSION) {
        throw new Error("not a building-relief file");
    }
    const count = view.readUInt32LE(8);
    if (count > (view.length - HEADER_SIZE) / ENTRY_SIZE) throw new Error("more entries than the file holds");
    const rectsAt = HEADER_SIZE + count * ENTRY_SIZE;
    const rectCount = (view.length - rectsAt) / RECT_SIZE;
    if (!Number.isInteger(rectCount)) throw new Error("damaged rectangles");
    const entries: ReliefEntry[] = [];
    for (let index = 0; index < count; index++) {
        const at = HEADER_SIZE + index * ENTRY_SIZE;
        const first = view.readUInt32LE(at + 16);
        const many = view.readUInt32LE(at + 20);
        if (first + many > rectCount) throw new Error(`entry ${index}: damaged`);
        const rects: ReliefRect[] = [];
        for (let r = first; r < first + many; r++) {
            const o = rectsAt + r * RECT_SIZE;
            const kind = KIND_OF.get(view.readUInt8(o));
            if (!kind) throw new Error(`entry ${index}: an unknown rectangle kind`);
            rects.push({
                kind,
                u0: view.readFloatLE(o + 4),
                v0: view.readFloatLE(o + 8),
                u1: view.readFloatLE(o + 12),
                v1: view.readFloatLE(o + 16),
                height: view.readFloatLE(o + 20),
            });
        }
        entries.push({
            checksum: view.readUInt32LE(at),
            contentHash: view.readBigUInt64LE(at + 4),
            facade: { repeatU: view.readUInt16LE(at + 12), repeatV: view.readUInt16LE(at + 14), rects },
        });
    }
    return { detector: view.readUInt32LE(12), sources: view.readBigUInt64LE(16), entries };
}
