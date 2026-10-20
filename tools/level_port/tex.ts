// Texture dictionaries (THUG2 levels\<x>\<x>.tex.xbx; THPS4 PC <lvl>tex.dat has the same layout and version):
//   u32 version (1), u32 count, then per texture:
//   u32 checksum, width, height, mip levels, texel depth, palette depth, DXT version (1, 2 = DXT1 with alpha, 5),
//   palette size, the palette, then per level {u32 size, data}.

import { hex8 } from "../lib/py.ts";
import { FormatError, Reader, Writer } from "./binio.ts";

export interface Texture {
    checksum: number;
    width: number;
    height: number;
    texelDepth: number;
    paletteDepth: number;
    dxt: number;
    palette: Buffer;
    levels: Buffer[];
}

export interface TexFile {
    version: number;
    textures: Texture[];
}

export function readTex(data: Uint8Array, name = "tex"): TexFile {
    const r = new Reader(data, 0, name);
    const [version, count] = r.read("2I") as [number, number];
    const textures: Texture[] = [];
    for (let k = 0; k < count; k++) {
        const [checksum, width, height, levels, texelDepth, paletteDepth, dxt, paletteSize] = r.read("8I") as [
            number,
            number,
            number,
            number,
            number,
            number,
            number,
            number,
        ];
        const palette = r.bytes(paletteSize);
        const levelData: Buffer[] = [];
        for (let n = 0; n < levels; n++) levelData.push(r.bytes(r.u32()));
        textures.push({ checksum, width, height, texelDepth, paletteDepth, dxt, palette, levels: levelData });
    }
    r.expectEnd();
    return { version, textures };
}

export function writeTex(tex: TexFile): Buffer {
    const w = new Writer();
    w.write("2I", tex.version, tex.textures.length);
    for (const t of tex.textures) {
        w.write(
            "8I",
            t.checksum,
            t.width,
            t.height,
            t.levels.length,
            t.texelDepth,
            t.paletteDepth,
            t.dxt,
            t.palette.length,
        );
        w.raw(t.palette);
        for (const level of t.levels) {
            w.u32(level.length);
            w.raw(level);
        }
    }
    return w.getvalue();
}

export function unswizzleOrder(w: number, h: number): number[] {
    // THPS4's 8-bit textures are stored swizzled, as on the Xbox: a texel's index has the bits of x and y
    // interleaved (x first) while both have bits left, then the rest of the longer one. Returns, for each texel in
    // row order, its index in the stored data.
    const order: number[] = [];
    for (let y = 0; y < h; y++) {
        for (let x = 0; x < w; x++) {
            let index = 0;
            let shift = 0;
            let bit = 0;
            while (2 ** bit < w || 2 ** bit < h) {
                if (2 ** bit < w) {
                    index += ((x >> bit) & 1) * 2 ** shift;
                    shift += 1;
                }
                if (2 ** bit < h) {
                    index += ((y >> bit) & 1) * 2 ** shift;
                    shift += 1;
                }
                bit += 1;
            }
            order.push(index);
        }
    }
    return order;
}

export function depalettise(tex: TexFile): number {
    // THPS4's 8-bit paletted textures (a few skies, one London texture: 256 x BGRA palette, indices row by row)
    // as 32-bit A8R8G8B8 - the texture loader (0x004CAE90) makes paletted ones 32-bit textures but its palette path
    // isn't the plain one the shipped THUG2 files use; 32-bit (texel depth 32, DXT 0) is.
    let changed = 0;
    for (const t of tex.textures) {
        if (!t.palette.length) continue;
        if (t.texelDepth !== 8 || t.palette.length !== 1024) {
            throw new FormatError(
                `texture ${hex8(t.checksum)}: ${t.texelDepth}-bit with a ${t.palette.length}-byte palette`,
            );
        }
        const levels: Buffer[] = [];
        t.levels.forEach((level, n) => {
            const w = Math.max(1, Math.floor(t.width / 2 ** n));
            const h = Math.max(1, Math.floor(t.height / 2 ** n));
            if (level.length !== w * h) {
                throw new FormatError(`texture ${hex8(t.checksum)} level ${n}: ${level.length} bytes for ${w}x${h}`);
            }
            const order = unswizzleOrder(w, h);
            const out = Buffer.alloc(order.length * 4);
            order.forEach((i, k) => {
                // (Python: colors[level[i]] - an index past the level raises)
                const index = level[i];
                if (index === undefined) throw new RangeError("index out of range");
                t.palette.copy(out, k * 4, index * 4, index * 4 + 4);
            });
            levels.push(out);
        });
        t.levels = levels;
        t.palette = Buffer.alloc(0);
        t.texelDepth = 32;
        t.paletteDepth = 32; // (as every texture of the shipped files)
        t.dxt = 0;
        changed += 1;
    }
    return changed;
}

export function checkLoadable(tex: TexFile, name: string): void {
    // What THUG2's loader takes plainly (0x004CAE90): DXT1/2/5, or uncompressed 32-bit; no palettes.
    for (const t of tex.textures) {
        if (t.palette.length || ![0, 1, 2, 5].includes(t.dxt) || (t.dxt === 0 && t.texelDepth !== 32)) {
            throw new FormatError(
                `${name}: texture ${hex8(t.checksum)} is ${t.texelDepth}-bit / palette ${t.palette.length} / DXT ${t.dxt}`,
            );
        }
    }
}
