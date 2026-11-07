// NOT ORIGINAL - the level editor's icon (src/editor/launcher/thug2-editor.ico): a plain, generic editor icon of our own
// - a dark rounded square holding an application window (a title bar, a side panel, a viewport) - drawn here and
// written as a Windows icon at 16, 24, 32, 48, 64 and 256 pixels (the small ones as 32-bit bitmaps, 256 as a PNG). The
// launcher (thug2-editor.exe) has it as its "EDITOR" icon resource; the editor mode's window takes it from there
// (src/platform/app_window.h).
//   node tools/editor_icon.ts              write the icon
//   node tools/editor_icon.ts --check      fail if the committed icon isn't what this draws

import { readFileSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { crc32, deflateSync } from "node:zlib";

const ROOT = join(import.meta.dirname, "..");
export const ICON_FILE = join(ROOT, "src", "editor", "launcher", "thug2-editor.ico");
const SIZES = [16, 24, 32, 48, 64, 256];
const SUPERSAMPLE = 4;

type Rgba = readonly [number, number, number, number];
const BACKGROUND: Rgba = [38, 42, 49, 255];
const EDGE: Rgba = [70, 76, 88, 255];
const FRAME: Rgba = [24, 26, 30, 255];
const TITLE_BAR: Rgba = [232, 131, 58, 255];
const SIDE_PANEL: Rgba = [62, 68, 80, 255];
const VIEWPORT: Rgba = [30, 34, 40, 255];
const GRID: Rgba = [232, 131, 58, 150];

/** Whether (x, y) - in 0..1 of the icon - is inside a rounded rectangle. */
function inRounded(x: number, y: number, left: number, top: number, right: number, bottom: number, radius: number) {
    if (x < left || x > right || y < top || y > bottom) return false;
    const cx = Math.min(Math.max(x, left + radius), right - radius);
    const cy = Math.min(Math.max(y, top + radius), bottom - radius);
    return (x - cx) ** 2 + (y - cy) ** 2 <= radius ** 2;
}

/** The icon's colour at a point (0..1 each way, y down). */
function colourAt(x: number, y: number): Rgba | undefined {
    if (!inRounded(x, y, 0.02, 0.02, 0.98, 0.98, 0.2)) return undefined;
    if (!inRounded(x, y, 0.055, 0.055, 0.945, 0.945, 0.17)) return EDGE;
    if (!inRounded(x, y, 0.16, 0.2, 0.84, 0.8, 0.05)) return BACKGROUND;
    if (!inRounded(x, y, 0.19, 0.23, 0.81, 0.77, 0.03)) return FRAME;
    if (y < 0.33) return TITLE_BAR;
    if (y < 0.36) return FRAME;
    if (x < 0.36) return y > 0.39 && y < 0.74 && x > 0.22 && x < 0.33 ? SIDE_PANEL : FRAME;
    if (x < 0.39) return FRAME;
    const gx = (x - 0.39) / (0.78 - 0.39);
    const gy = (y - 0.39) / (0.74 - 0.39);
    if (gx < 0 || gx > 1 || gy < 0 || gy > 1) return FRAME;
    const line = 0.035;
    if (Math.abs(gx - 0.5) < line || Math.abs(gy - 0.5) < line) return GRID;
    return VIEWPORT;
}

/** The icon at `size` pixels: rows top first, RGBA, each pixel the average of SUPERSAMPLE x SUPERSAMPLE samples. */
export function drawIcon(size: number): Uint8Array {
    const pixels = new Uint8Array(size * size * 4);
    for (let py = 0; py < size; py++) {
        for (let px = 0; px < size; px++) {
            let r = 0;
            let g = 0;
            let b = 0;
            let a = 0;
            for (let sy = 0; sy < SUPERSAMPLE; sy++) {
                for (let sx = 0; sx < SUPERSAMPLE; sx++) {
                    const colour = colourAt(
                        (px + (sx + 0.5) / SUPERSAMPLE) / size,
                        (py + (sy + 0.5) / SUPERSAMPLE) / size,
                    );
                    if (colour === undefined) continue;
                    const alpha = colour[3];
                    r += colour[0] * alpha;
                    g += colour[1] * alpha;
                    b += colour[2] * alpha;
                    a += alpha;
                }
            }
            const at = (py * size + px) * 4;
            const samples = SUPERSAMPLE * SUPERSAMPLE;
            pixels[at] = a > 0 ? Math.round(r / a) : 0;
            pixels[at + 1] = a > 0 ? Math.round(g / a) : 0;
            pixels[at + 2] = a > 0 ? Math.round(b / a) : 0;
            pixels[at + 3] = Math.round(a / samples);
        }
    }
    return pixels;
}

function chunk(type: string, data: Uint8Array): Buffer {
    const out = Buffer.alloc(12 + data.length);
    out.writeUInt32BE(data.length, 0);
    out.write(type, 4, "ascii");
    Buffer.from(data).copy(out, 8);
    out.writeUInt32BE(crc32(out.subarray(4, 8 + data.length)), 8 + data.length);
    return out;
}

/** RGBA rows (top first) as a PNG file. */
export function png(pixels: Uint8Array, size: number): Buffer {
    const header = Buffer.alloc(13);
    header.writeUInt32BE(size, 0);
    header.writeUInt32BE(size, 4);
    header[8] = 8; // bits per channel
    header[9] = 6; // RGBA
    const rows = Buffer.alloc(size * (size * 4 + 1));
    for (let y = 0; y < size; y++) {
        rows[y * (size * 4 + 1)] = 0; // (no filter)
        Buffer.from(pixels.subarray(y * size * 4, (y + 1) * size * 4)).copy(rows, y * (size * 4 + 1) + 1);
    }
    return Buffer.concat([
        Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]),
        chunk("IHDR", header),
        chunk("IDAT", deflateSync(rows, { level: 9 })),
        chunk("IEND", new Uint8Array(0)),
    ]);
}

/** RGBA rows (top first) as an icon's bitmap: its header, BGRA rows bottom first, an empty AND mask. */
export function bitmap(pixels: Uint8Array, size: number): Buffer {
    const maskRow = Math.ceil(size / 32) * 4;
    const out = Buffer.alloc(40 + size * size * 4 + maskRow * size);
    out.writeUInt32LE(40, 0);
    out.writeInt32LE(size, 4);
    out.writeInt32LE(size * 2, 8); // (the colours and the mask)
    out.writeUInt16LE(1, 12);
    out.writeUInt16LE(32, 14);
    out.writeUInt32LE(size * size * 4 + maskRow * size, 20);
    for (let y = 0; y < size; y++) {
        for (let x = 0; x < size; x++) {
            const from = ((size - 1 - y) * size + x) * 4;
            const to = 40 + (y * size + x) * 4;
            out[to] = pixels[from + 2] ?? 0;
            out[to + 1] = pixels[from + 1] ?? 0;
            out[to + 2] = pixels[from] ?? 0;
            out[to + 3] = pixels[from + 3] ?? 0;
        }
    }
    return out;
}

/** The icon file: every size. */
export function iconFile(): Buffer {
    const images = SIZES.map((size) => {
        const pixels = drawIcon(size);
        return size >= 256 ? png(pixels, size) : bitmap(pixels, size);
    });
    const header = Buffer.alloc(6 + 16 * SIZES.length);
    header.writeUInt16LE(0, 0);
    header.writeUInt16LE(1, 2);
    header.writeUInt16LE(SIZES.length, 4);
    let offset = header.length;
    SIZES.forEach((size, i) => {
        const entry = 6 + i * 16;
        const image = images[i] as Buffer;
        header[entry] = size >= 256 ? 0 : size;
        header[entry + 1] = size >= 256 ? 0 : size;
        header.writeUInt16LE(1, entry + 4);
        header.writeUInt16LE(32, entry + 6);
        header.writeUInt32LE(image.length, entry + 8);
        header.writeUInt32LE(offset, entry + 12);
        offset += image.length;
    });
    return Buffer.concat([header, ...images]);
}

function main(): number {
    const made = iconFile();
    if (process.argv.includes("--check")) {
        const same = Buffer.compare(readFileSync(ICON_FILE), made) === 0;
        console.log(same ? "the editor's icon is up to date" : "the editor's icon differs: node tools/editor_icon.ts");
        return same ? 0 : 1;
    }
    writeFileSync(ICON_FILE, made);
    console.log(`${ICON_FILE}: ${SIZES.join(", ")} pixels, ${made.length} bytes`);
    return 0;
}

if (import.meta.main) process.exitCode = main();
