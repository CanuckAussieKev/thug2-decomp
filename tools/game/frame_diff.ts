// NOT ORIGINAL - how much a frame series flickers, pixel by pixel (docs/pending/shimmer-2.md). A series is the
// automation channel's `capture` (frame_0001.png ... from a locked camera - src/game/front/automation.h): with the
// camera and the skater still, a pixel that changes from one frame to the next is flicker (z-fighting, sorting,
// aliasing, animation).
//
//     node tools/game/frame_diff.ts <series folder> [--threshold N] [--region name:x,y,w,h]... [--heatmap out.png]
//                                   [--skip N]
//
// Per pixel: the frame-to-frame changes whose summed |dR|+|dG|+|dB| is over the threshold (default 24). Printed: the
// whole frame's and each region's share of pixels that ever flicker (>= 2% of the steps), the flicker rate (changes
// per pixel-step) and the mean |difference|. --heatmap writes a picture: the first frame dimmed, flickering pixels
// red by how often. --skip leaves out the first N frames. Only 8-bit RGB / RGBA PNGs without interlacing (what the
// game's screenshot writer makes).

import * as fs from "node:fs";
import * as path from "node:path";
import * as zlib from "node:zlib";

export interface Image {
    width: number;
    height: number;
    rgb: Uint8Array; // width * height * 3
}

const SIGNATURE = Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]);

function paeth(a: number, b: number, c: number): number {
    const p = a + b - c;
    const pa = Math.abs(p - a);
    const pb = Math.abs(p - b);
    const pc = Math.abs(p - c);
    return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
}

export function decodePng(data: Buffer): Image {
    if (!data.subarray(0, 8).equals(SIGNATURE)) throw new Error("not a PNG");
    let at = 8;
    let width = 0;
    let height = 0;
    let channels = 0;
    const idat: Buffer[] = [];
    while (at < data.length) {
        const length = data.readUInt32BE(at);
        const type = data.toString("latin1", at + 4, at + 8);
        const body = data.subarray(at + 8, at + 8 + length);
        if (type === "IHDR") {
            width = body.readUInt32BE(0);
            height = body.readUInt32BE(4);
            const depth = body[8];
            const colour = body[9];
            if (depth !== 8 || (colour !== 2 && colour !== 6) || body[12] !== 0)
                throw new Error("only 8-bit RGB / RGBA PNGs without interlacing");
            channels = colour === 2 ? 3 : 4;
        } else if (type === "IDAT") idat.push(body);
        else if (type === "IEND") break;
        at += 12 + length;
    }
    const raw = zlib.inflateSync(Buffer.concat(idat));
    const stride = width * channels;
    const pixels = new Uint8Array(stride * height);
    for (let y = 0; y < height; y++) {
        const filter = raw[y * (stride + 1)]!;
        const line = y * (stride + 1) + 1;
        const row = y * stride;
        for (let x = 0; x < stride; x++) {
            const value = raw[line + x]!;
            const a = x >= channels ? pixels[row + x - channels]! : 0;
            const b = y > 0 ? pixels[row - stride + x]! : 0;
            const c = x >= channels && y > 0 ? pixels[row - stride + x - channels]! : 0;
            const predicted =
                filter === 0 ? 0 : filter === 1 ? a : filter === 2 ? b : filter === 3 ? (a + b) >> 1 : paeth(a, b, c);
            pixels[row + x] = (value + predicted) & 0xff;
        }
    }
    if (channels === 3) return { width, height, rgb: pixels };
    const rgb = new Uint8Array(width * height * 3);
    for (let i = 0, j = 0; i < pixels.length; i += 4, j += 3) {
        rgb[j] = pixels[i]!;
        rgb[j + 1] = pixels[i + 1]!;
        rgb[j + 2] = pixels[i + 2]!;
    }
    return { width, height, rgb };
}

const CRC_TABLE = (() => {
    const table = new Uint32Array(256);
    for (let n = 0; n < 256; n++) {
        let c = n;
        for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
        table[n] = c >>> 0;
    }
    return table;
})();

function crc32(data: Buffer): number {
    let c = 0xffffffff;
    for (const byte of data) c = CRC_TABLE[(c ^ byte) & 0xff]! ^ (c >>> 8);
    return (c ^ 0xffffffff) >>> 0;
}

function chunk(type: string, body: Buffer): Buffer {
    const head = Buffer.alloc(8);
    head.writeUInt32BE(body.length, 0);
    head.write(type, 4, "latin1");
    const tail = Buffer.alloc(4);
    tail.writeUInt32BE(crc32(Buffer.concat([head.subarray(4), body])), 0);
    return Buffer.concat([head, body, tail]);
}

export function encodePng(image: Image): Buffer {
    const header = Buffer.alloc(13);
    header.writeUInt32BE(image.width, 0);
    header.writeUInt32BE(image.height, 4);
    header[8] = 8;
    header[9] = 2;
    const stride = image.width * 3;
    const raw = Buffer.alloc((stride + 1) * image.height);
    for (let y = 0; y < image.height; y++)
        raw.set(image.rgb.subarray(y * stride, (y + 1) * stride), y * (stride + 1) + 1);
    return Buffer.concat([
        SIGNATURE,
        chunk("IHDR", header),
        chunk("IDAT", zlib.deflateSync(raw)),
        chunk("IEND", Buffer.alloc(0)),
    ]);
}

export interface Region {
    name: string;
    x: number;
    y: number;
    w: number;
    h: number;
}

export interface RegionStats {
    name: string;
    pixels: number;
    flickering: number; // pixels changing in >= 2% of the steps
    rate: number; // changes per pixel-step
    meanDiff: number; // mean summed |dRGB| per pixel-step
}

// Per pixel: the steps over the threshold and the summed |differences|.
export function analyse(
    frames: Image[],
    threshold: number,
): { changes: Uint32Array; diff: Float64Array; steps: number } {
    const first = frames[0]!;
    const count = first.width * first.height;
    const changes = new Uint32Array(count);
    const diff = new Float64Array(count);
    for (let f = 1; f < frames.length; f++) {
        const a = frames[f - 1]!.rgb;
        const b = frames[f]!.rgb;
        for (let p = 0, i = 0; p < count; p++, i += 3) {
            const d = Math.abs(a[i]! - b[i]!) + Math.abs(a[i + 1]! - b[i + 1]!) + Math.abs(a[i + 2]! - b[i + 2]!);
            diff[p] = diff[p]! + d;
            if (d > threshold) changes[p] = changes[p]! + 1;
        }
    }
    return { changes, diff, steps: frames.length - 1 };
}

export function regionStats(
    width: number,
    result: { changes: Uint32Array; diff: Float64Array; steps: number },
    region: Region,
): RegionStats {
    let pixels = 0;
    let flickering = 0;
    let changes = 0;
    let diff = 0;
    const minimum = Math.max(1, Math.ceil(result.steps * 0.02));
    for (let y = region.y; y < region.y + region.h; y++) {
        for (let x = region.x; x < region.x + region.w; x++) {
            const p = y * width + x;
            pixels++;
            changes += result.changes[p]!;
            diff += result.diff[p]!;
            if (result.changes[p]! >= minimum) flickering++;
        }
    }
    const steps = Math.max(1, result.steps) * Math.max(1, pixels);
    return { name: region.name, pixels, flickering, rate: changes / steps, meanDiff: diff / steps };
}

function main(): void {
    const args = process.argv.slice(2);
    let folder = "";
    let threshold = 24;
    let heatmap = "";
    let skip = 0;
    const regions: Region[] = [];
    for (let i = 0; i < args.length; i++) {
        const a = args[i]!;
        if (a === "--threshold") threshold = Number(args[++i]);
        else if (a === "--heatmap") heatmap = args[++i]!;
        else if (a === "--skip") skip = Number(args[++i]);
        else if (a === "--region") {
            const [name, box] = args[++i]!.split(":");
            const [x, y, w, h] = box!.split(",").map(Number);
            regions.push({ name: name!, x: x!, y: y!, w: w!, h: h! });
        } else folder = a;
    }
    if (!folder) {
        console.log(
            "usage: node tools/game/frame_diff.ts <series folder> [--threshold N] [--region name:x,y,w,h]... [--heatmap out.png] [--skip N]",
        );
        process.exit(2);
    }
    const names = fs
        .readdirSync(folder)
        .filter((n) => /^frame_\d+\.png$/i.test(n))
        .sort()
        .slice(skip);
    if (names.length < 2) throw new Error(`fewer than 2 frames in ${folder}`);
    const frames = names.map((n) => decodePng(fs.readFileSync(path.join(folder, n))));
    const { width, height } = frames[0]!;
    const result = analyse(frames, threshold);
    console.log(`${names.length} frames ${width} x ${height}, threshold ${threshold}`);
    for (const region of [{ name: "frame", x: 0, y: 0, w: width, h: height }, ...regions]) {
        const s = regionStats(width, result, region);
        console.log(
            `${s.name.padEnd(12)} ${String(s.pixels).padStart(8)} px  flickering ${String(s.flickering).padStart(7)} (${((100 * s.flickering) / s.pixels).toFixed(2)}%)  rate ${(100 * s.rate).toFixed(3)}%  mean |d| ${s.meanDiff.toFixed(2)}`,
        );
    }
    if (heatmap) {
        const out = new Uint8Array(width * height * 3);
        const base = frames[0]!.rgb;
        for (let p = 0, i = 0; p < width * height; p++, i += 3) {
            const heat = Math.min(1, (result.changes[p]! / result.steps) * 4);
            const grey = ((base[i]! + base[i + 1]! + base[i + 2]!) / 3) * 0.35;
            out[i] = Math.round(grey + (255 - grey) * heat);
            out[i + 1] = Math.round(grey * (1 - heat));
            out[i + 2] = Math.round(grey * (1 - heat));
        }
        for (const r of regions) {
            for (let x = r.x; x < r.x + r.w; x++) {
                for (const y of [r.y, r.y + r.h - 1]) out.set([0, 255, 0], (y * width + x) * 3);
            }
            for (let y = r.y; y < r.y + r.h; y++) {
                for (const x of [r.x, r.x + r.w - 1]) out.set([0, 255, 0], (y * width + x) * 3);
            }
        }
        fs.writeFileSync(heatmap, encodePng({ width, height, rgb: out }));
        console.log(`heatmap: ${heatmap}`);
    }
}

if (process.argv[1] && path.resolve(process.argv[1]) === path.resolve(import.meta.filename)) main();
