// NOT ORIGINAL - the detail-material library's file, thug2-decomp-materials.t2dm (src/formats/detail_material_format.h
// has the layout; its write_file makes the same bytes): the kinds with their real materials and detail textures, and
// which kind each texture is.

export const MAGIC = 0x4d443254; // "T2DM"
export const VERSION = 1;
export const HEADER_SIZE = 32;
export const KIND_SIZE = 48;
export const ASSIGNMENT_SIZE = 24;
export const LIBRARY_FILE = "thug2-decomp-materials.t2dm";

export interface KindEntry {
    kind: number; // kinds.ts KIND
    side: number;
    tileMetres: number;
    roughness: number;
    roughnessSwing: number;
    f0: number;
    metalness: number;
    roughnessWeight: number;
    texels: Uint8Array; // side x side x 4
}

export interface Assignment {
    checksum: number;
    contentHash: bigint;
    kind: number;
    evidence: number; // kinds.ts EVIDENCE_*
    repeats: number; // detail tiles per texture unit (0: the game's default)
    meanLuma: number; // < 0: unknown
}

export function writeLibrary(kinds: readonly KindEntry[], assignments: readonly Assignment[]): Buffer {
    const tables = HEADER_SIZE + kinds.length * KIND_SIZE + assignments.length * ASSIGNMENT_SIZE;
    const size = tables + kinds.reduce((sum, k) => sum + k.texels.length, 0);
    const out = Buffer.alloc(size);
    out.writeUInt32LE(MAGIC, 0);
    out.writeUInt32LE(VERSION, 4);
    out.writeUInt32LE(kinds.length, 8);
    out.writeUInt32LE(assignments.length, 12);
    let at = HEADER_SIZE;
    let offset = tables;
    for (const k of kinds) {
        if (k.texels.length !== k.side * k.side * 4) throw new Error(`kind ${k.kind}: ${k.texels.length} texel bytes`);
        out.writeUInt8(k.kind, at);
        out.writeUInt16LE(k.side, at + 2);
        out.writeFloatLE(k.tileMetres, at + 4);
        out.writeFloatLE(k.roughness, at + 8);
        out.writeFloatLE(k.roughnessSwing, at + 12);
        out.writeFloatLE(k.f0, at + 16);
        out.writeFloatLE(k.metalness, at + 20);
        out.writeFloatLE(k.roughnessWeight, at + 24);
        out.writeUInt32LE(offset, at + 28);
        out.writeUInt32LE(k.texels.length, at + 32);
        out.set(k.texels, offset);
        offset += k.texels.length;
        at += KIND_SIZE;
    }
    for (const a of assignments) {
        out.writeUInt32LE(a.checksum >>> 0, at);
        out.writeBigUInt64LE(a.contentHash, at + 4);
        out.writeUInt8(a.kind, at + 12);
        out.writeUInt8(a.evidence, at + 13);
        out.writeFloatLE(a.repeats, at + 16);
        out.writeFloatLE(a.meanLuma, at + 20);
        at += ASSIGNMENT_SIZE;
    }
    return out;
}

export interface Library {
    kinds: KindEntry[];
    assignments: Assignment[];
}

export function readLibrary(data: Uint8Array): Library {
    const view = Buffer.from(data.buffer, data.byteOffset, data.byteLength);
    if (view.length < HEADER_SIZE || view.readUInt32LE(0) !== MAGIC) throw new Error("not a detail-material library");
    if (view.readUInt32LE(4) !== VERSION) throw new Error(`version ${view.readUInt32LE(4)}, not ${VERSION}`);
    const kindCount = view.readUInt32LE(8);
    const assignmentCount = view.readUInt32LE(12);
    const kinds: KindEntry[] = [];
    let at = HEADER_SIZE;
    for (let i = 0; i < kindCount; i++) {
        const side = view.readUInt16LE(at + 2);
        const offset = view.readUInt32LE(at + 28);
        const bytes = view.readUInt32LE(at + 32);
        if (bytes !== side * side * 4 || offset + bytes > view.length)
            throw new Error(`kind ${i}: texels outside the file`);
        kinds.push({
            kind: view.readUInt8(at),
            side,
            tileMetres: view.readFloatLE(at + 4),
            roughness: view.readFloatLE(at + 8),
            roughnessSwing: view.readFloatLE(at + 12),
            f0: view.readFloatLE(at + 16),
            metalness: view.readFloatLE(at + 20),
            roughnessWeight: view.readFloatLE(at + 24),
            texels: new Uint8Array(view.subarray(offset, offset + bytes)),
        });
        at += KIND_SIZE;
    }
    const assignments: Assignment[] = [];
    for (let i = 0; i < assignmentCount; i++) {
        assignments.push({
            checksum: view.readUInt32LE(at),
            contentHash: view.readBigUInt64LE(at + 4),
            kind: view.readUInt8(at + 12),
            evidence: view.readUInt8(at + 13),
            repeats: view.readFloatLE(at + 16),
            meanLuma: view.readFloatLE(at + 20),
        });
        at += ASSIGNMENT_SIZE;
    }
    return { kinds, assignments };
}
