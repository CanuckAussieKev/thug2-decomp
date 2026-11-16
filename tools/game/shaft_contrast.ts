// NOT ORIGINAL - how distinct the light shafts in a game capture are: the same pose captured with the volumetric air on
// (rt_fog=1) and off (`console set rt_fog 0`), the air's own light the difference of the two (luma, 0 - 255 levels).
// Over a rectangle (default: the whole picture):
//   - veil: the median of the air's light - what it adds everywhere;
//   - beam / gap: its 90th and 10th percentiles (the lit air's rays, the shadowed air's - a gap under 0, where the air
//     only dims what is behind it, counts as 0);
//   - contrast: (beam - gap) / beam - 1 when the gaps are dark, 0 when the air is one even veil;
//   - structure: the mean difference between the air's light and its own blur over `radius` pixels, over its mean -
//     how much of it is shafts and gaps rather than a smooth glow.
//   node tools/game/shaft_contrast.ts <on.png> <off.png> [--rect x,y,w,h] [--radius 16] [--picture out.png]
// --picture writes the air's light (x 4, grey) for a contact sheet.
import * as fs from "node:fs";
import { isMain, parseCli, print, runMain } from "thug2-level-converter-core/lib/cli.ts";
import { decodePng, encodePng } from "thug2-level-converter-core/lib/frame_diff.ts";

const USAGE = "usage: shaft_contrast.ts on.png off.png [--rect x,y,w,h] [--radius N] [--picture out.png]";

export interface Rect {
    x: number;
    y: number;
    w: number;
    h: number;
}

export interface ShaftStats {
    pixels: number;
    veil: number;
    beam: number;
    gap: number;
    contrast: number;
    structure: number;
}

interface Picture {
    width: number;
    height: number;
    rgb: Uint8Array;
}

function luma(rgb: Uint8Array, i: number): number {
    return 0.2126 * (rgb[i * 3] ?? 0) + 0.7152 * (rgb[i * 3 + 1] ?? 0) + 0.0722 * (rgb[i * 3 + 2] ?? 0);
}

// The air's light: luma(on) - luma(off), a value a pixel.
export function airLight(on: Picture, off: Picture): Float32Array {
    if (on.width !== off.width || on.height !== off.height) throw new Error("the two captures differ in size");
    const out = new Float32Array(on.width * on.height);
    for (let i = 0; i < out.length; i++) out[i] = luma(on.rgb, i) - luma(off.rgb, i);
    return out;
}

function percentile(sorted: Float32Array, share: number): number {
    if (sorted.length === 0) return 0;
    const at = Math.min(sorted.length - 1, Math.max(0, Math.round(share * (sorted.length - 1))));
    return sorted[at] ?? 0;
}

// A box blur of `values` (width x height) over (2 radius + 1)^2 pixels, clamped at the edges (summed-area table).
function boxBlur(values: Float32Array, width: number, height: number, radius: number): Float32Array {
    const sums = new Float64Array((width + 1) * (height + 1));
    for (let y = 0; y < height; y++) {
        let row = 0;
        for (let x = 0; x < width; x++) {
            row += values[y * width + x] ?? 0;
            sums[(y + 1) * (width + 1) + x + 1] = (sums[y * (width + 1) + x + 1] ?? 0) + row;
        }
    }
    const out = new Float32Array(width * height);
    for (let y = 0; y < height; y++) {
        const y0 = Math.max(0, y - radius);
        const y1 = Math.min(height, y + radius + 1);
        for (let x = 0; x < width; x++) {
            const x0 = Math.max(0, x - radius);
            const x1 = Math.min(width, x + radius + 1);
            const sum =
                (sums[y1 * (width + 1) + x1] ?? 0) -
                (sums[y0 * (width + 1) + x1] ?? 0) -
                (sums[y1 * (width + 1) + x0] ?? 0) +
                (sums[y0 * (width + 1) + x0] ?? 0);
            out[y * width + x] = sum / ((x1 - x0) * (y1 - y0));
        }
    }
    return out;
}

export function shaftStats(air: Float32Array, width: number, height: number, rect: Rect, radius: number): ShaftStats {
    const x0 = Math.max(0, rect.x);
    const y0 = Math.max(0, rect.y);
    const x1 = Math.min(width, rect.x + rect.w);
    const y1 = Math.min(height, rect.y + rect.h);
    const w = Math.max(0, x1 - x0);
    const h = Math.max(0, y1 - y0);
    const inside = new Float32Array(w * h);
    for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) inside[y * w + x] = air[(y0 + y) * width + x0 + x] ?? 0;
    const blurred = boxBlur(inside, w, h, radius);
    let spread = 0;
    let magnitude = 0;
    for (let i = 0; i < inside.length; i++) {
        spread += Math.abs((inside[i] ?? 0) - (blurred[i] ?? 0));
        magnitude += Math.abs(inside[i] ?? 0);
    }
    const sorted = Float32Array.from(inside).sort();
    const beam = percentile(sorted, 0.9);
    const gap = Math.max(0, percentile(sorted, 0.1));
    return {
        pixels: inside.length,
        veil: percentile(sorted, 0.5),
        beam,
        gap,
        contrast: beam > 1 ? (beam - gap) / beam : 0,
        structure: magnitude > 0 ? spread / magnitude : 0,
    };
}

function parseRect(text: string | undefined, width: number, height: number): Rect {
    if (!text) return { x: 0, y: 0, w: width, h: height };
    const [x, y, w, h] = text.split(",").map(Number);
    if ([x, y, w, h].some((v) => v === undefined || !Number.isFinite(v))) throw new Error(`bad --rect ${text}`);
    return { x: x ?? 0, y: y ?? 0, w: w ?? width, h: h ?? height };
}

function main(): number {
    const { values, positionals } = parseCli(USAGE, {
        options: {
            rect: { type: "string" },
            radius: { type: "string", default: "16" },
            picture: { type: "string" },
        },
        allowPositionals: true,
    });
    if (positionals.length !== 2) {
        print(USAGE);
        return 2;
    }
    const [onFile, offFile] = positionals as [string, string];
    const on = decodePng(fs.readFileSync(onFile));
    const off = decodePng(fs.readFileSync(offFile));
    const air = airLight(on, off);
    const rect = parseRect(values.rect, on.width, on.height);
    const stats = shaftStats(air, on.width, on.height, rect, Number(values.radius));
    print(
        `veil ${stats.veil.toFixed(1)}  beam ${stats.beam.toFixed(1)}  gap ${stats.gap.toFixed(1)}  ` +
            `contrast ${stats.contrast.toFixed(2)}  structure ${stats.structure.toFixed(2)}  (${stats.pixels} pixels)`,
    );
    if (values.picture) {
        const rgb = new Uint8Array(on.width * on.height * 3);
        for (let i = 0; i < air.length; i++) {
            const v = Math.max(0, Math.min(255, Math.round(4 * (air[i] ?? 0))));
            rgb[i * 3] = v;
            rgb[i * 3 + 1] = v;
            rgb[i * 3 + 2] = v;
        }
        fs.writeFileSync(values.picture, encodePng({ width: on.width, height: on.height, rgb }));
    }
    return 0;
}

if (isMain(import.meta.url)) await runMain(() => main());
