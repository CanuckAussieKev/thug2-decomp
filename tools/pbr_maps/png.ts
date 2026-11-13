// NOT ORIGINAL - a minimal PNG writer and reader (8-bit RGBA, no interlace) for the surface maps' pictures and the
// model generator's exchange files.
import { deflateSync, inflateSync } from "node:zlib";

const CRC_TABLE = (() => {
    const table = new Uint32Array(256);
    for (let n = 0; n < 256; n++) {
        let c = n;
        for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
        table[n] = c >>> 0;
    }
    return table;
})();

function crc32(bytes: Uint8Array): number {
    let c = 0xffffffff;
    for (const b of bytes) c = CRC_TABLE[(c ^ b) & 0xff]! ^ (c >>> 8);
    return (c ^ 0xffffffff) >>> 0;
}

function chunk(type: string, data: Uint8Array): Buffer {
    const out = Buffer.alloc(12 + data.length);
    out.writeUInt32BE(data.length, 0);
    out.write(type, 4, "ascii");
    out.set(data, 8);
    out.writeUInt32BE(crc32(out.subarray(4, 8 + data.length)), 8 + data.length);
    return out;
}

export function encodePng(width: number, height: number, rgba: Uint8Array): Buffer {
    const header = Buffer.alloc(13);
    header.writeUInt32BE(width, 0);
    header.writeUInt32BE(height, 4);
    header[8] = 8; // bits
    header[9] = 6; // RGBA
    const raw = Buffer.alloc(height * (width * 4 + 1));
    for (let y = 0; y < height; y++) {
        raw[y * (width * 4 + 1)] = 0;
        raw.set(rgba.subarray(y * width * 4, (y + 1) * width * 4), y * (width * 4 + 1) + 1);
    }
    return Buffer.concat([
        Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]),
        chunk("IHDR", header),
        chunk("IDAT", deflateSync(raw)),
        chunk("IEND", new Uint8Array(0)),
    ]);
}

function paeth(a: number, b: number, c: number): number {
    const p = a + b - c;
    const pa = Math.abs(p - a);
    const pb = Math.abs(p - b);
    const pc = Math.abs(p - c);
    return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
}

// 8-bit RGB / RGBA / grey PNGs (what image tools write) as RGBA.
export function decodePng(file: Uint8Array): { width: number; height: number; data: Uint8Array } {
    const view = Buffer.from(file.buffer, file.byteOffset, file.byteLength);
    let at = 8;
    let width = 0;
    let height = 0;
    let colour = 0;
    const parts: Buffer[] = [];
    while (at < view.length) {
        const length = view.readUInt32BE(at);
        const type = view.toString("ascii", at + 4, at + 8);
        const data = view.subarray(at + 8, at + 8 + length);
        if (type === "IHDR") {
            width = data.readUInt32BE(0);
            height = data.readUInt32BE(4);
            if (data[8] !== 8 || data[12] !== 0) throw new Error("only 8-bit, non-interlaced PNGs");
            colour = data[9]!;
        } else if (type === "IDAT") parts.push(data);
        at += 12 + length;
    }
    const channels = colour === 6 ? 4 : colour === 2 ? 3 : colour === 0 ? 1 : colour === 4 ? 2 : 0;
    if (!channels) throw new Error(`PNG colour type ${colour} not read`);
    const raw = inflateSync(Buffer.concat(parts));
    const stride = width * channels;
    const pixels = new Uint8Array(height * stride);
    for (let y = 0; y < height; y++) {
        const filter = raw[y * (stride + 1)]!;
        for (let x = 0; x < stride; x++) {
            const value = raw[y * (stride + 1) + 1 + x]!;
            const a = x >= channels ? pixels[y * stride + x - channels]! : 0;
            const b = y > 0 ? pixels[(y - 1) * stride + x]! : 0;
            const c = x >= channels && y > 0 ? pixels[(y - 1) * stride + x - channels]! : 0;
            const predicted = [0, a, b, (a + b) >> 1, paeth(a, b, c)][filter] ?? 0;
            pixels[y * stride + x] = (value + predicted) & 0xff;
        }
    }
    const data = new Uint8Array(width * height * 4);
    for (let i = 0; i < width * height; i++) {
        const p = pixels.subarray(i * channels, (i + 1) * channels);
        const grey = channels <= 2;
        data[i * 4] = p[0]!;
        data[i * 4 + 1] = grey ? p[0]! : p[1]!;
        data[i * 4 + 2] = grey ? p[0]! : p[2]!;
        data[i * 4 + 3] = channels === 4 ? p[3]! : channels === 2 ? p[1]! : 255;
    }
    return { width, height, data };
}
