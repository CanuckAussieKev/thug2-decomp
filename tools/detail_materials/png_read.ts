// NOT ORIGINAL - PNG files as floating-point channels: 8 or 16 bits a channel, grey / grey + alpha / RGB / RGBA, not
// interlaced (what material scans ship - their normal and roughness maps are often 16-bit). tools/pbr_maps/png.ts reads
// 8-bit ones only.
import { inflateSync } from "node:zlib";

export interface FloatImage {
    width: number;
    height: number;
    channels: number; // 1 - 4, as the file has them
    data: Float32Array; // rows top first, 0 - 1
}

const SIGNATURE = [0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a];

function paeth(a: number, b: number, c: number): number {
    const p = a + b - c;
    const pa = Math.abs(p - a);
    const pb = Math.abs(p - b);
    const pc = Math.abs(p - c);
    return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
}

export function readPng(file: Uint8Array): FloatImage {
    if (file.length < 8 || SIGNATURE.some((byte, i) => file[i] !== byte)) throw new Error("not a PNG file");
    const view = Buffer.from(file.buffer, file.byteOffset, file.byteLength);
    let at = 8;
    let width = 0;
    let height = 0;
    let depth = 0;
    let colour = -1;
    const parts: Buffer[] = [];
    while (at + 8 <= view.length) {
        const length = view.readUInt32BE(at);
        const type = view.toString("ascii", at + 4, at + 8);
        const data = view.subarray(at + 8, at + 8 + length);
        if (type === "IHDR") {
            width = data.readUInt32BE(0);
            height = data.readUInt32BE(4);
            depth = data[8]!;
            colour = data[9]!;
            if (data[12] !== 0) throw new Error("interlaced PNGs are not read");
        } else if (type === "IDAT") parts.push(data);
        else if (type === "IEND") break;
        at += 12 + length;
    }
    const channels = colour === 6 ? 4 : colour === 2 ? 3 : colour === 0 ? 1 : colour === 4 ? 2 : 0;
    if (!channels) throw new Error(`PNG colour type ${colour} not read (palettes aren't)`);
    if (depth !== 8 && depth !== 16) throw new Error(`PNG depth ${depth} not read`);
    const bytesPer = (channels * depth) / 8; // (a pixel's bytes: the filters' left neighbour)
    const stride = width * bytesPer;
    const raw = inflateSync(Buffer.concat(parts));
    if (raw.length < height * (stride + 1)) throw new Error("PNG data cut short");
    const rows = new Uint8Array(height * stride);
    for (let y = 0; y < height; y++) {
        const filter = raw[y * (stride + 1)]!;
        const line = y * stride;
        const source = y * (stride + 1) + 1;
        for (let x = 0; x < stride; x++) {
            const value = raw[source + x]!;
            const a = x >= bytesPer ? rows[line + x - bytesPer]! : 0;
            const b = y > 0 ? rows[line - stride + x]! : 0;
            const c = x >= bytesPer && y > 0 ? rows[line - stride + x - bytesPer]! : 0;
            const predicted =
                filter === 1 ? a : filter === 2 ? b : filter === 3 ? (a + b) >> 1 : filter === 4 ? paeth(a, b, c) : 0;
            rows[line + x] = (value + predicted) & 0xff;
        }
    }
    const count = width * height * channels;
    const data = new Float32Array(count);
    if (depth === 8) {
        for (let i = 0; i < count; i++) data[i] = rows[i]! / 255;
    } else {
        for (let i = 0; i < count; i++) data[i] = ((rows[i * 2]! << 8) | rows[i * 2 + 1]!) / 65535;
    }
    return { width, height, channels, data };
}
