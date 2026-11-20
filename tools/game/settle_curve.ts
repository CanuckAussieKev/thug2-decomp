// How fast a picture settles after the camera moves (the automation's `capture <frames> <folder>` started at a camera
// cut or a stop, the camera then held): each frame's error against the settled picture - the per-pixel median of the
// series' last frames - in a rectangle. What a denoiser and a resampled estimator leave on newly revealed pixels
// (disocclusion noise: sparkle that fades while the camera stands still).
//   node tools/game/settle_curve.ts <series folder> <x,y,w,h> [--settled N] [--threshold 30] [--frames 1,2,3,...]
// Per listed frame (1 = the first captured): specks - pixels whose luma (0-255, Rec. 709 weights on the PNG's bytes) is
// over the settled picture's by the threshold, and over their own 5x5 neighbourhood's median by it (isolated bright
// pixels, not a shifted edge) - and the RMS luma error against the settled picture. The settled picture: the median of
// the last N frames (default 30).
import * as fs from "node:fs";
import * as path from "node:path";
import { isMain, parseCli, print, runMain } from "thug2-level-converter-core/lib/cli.ts";
import { decodePng } from "thug2-level-converter-core/lib/frame_diff.ts";

const USAGE = "usage: settle_curve.ts <series folder> x,y,w,h [--settled N] [--threshold T] [--frames 1,2,...]";

export interface Rect {
    x: number;
    y: number;
    w: number;
    h: number;
}

export interface SettleRow {
    frame: number; // 1-based
    specks: number;
    rms: number;
}

function median(values: Float32Array): number {
    values.sort();
    return values[values.length >> 1]!;
}

// The settled picture: each pixel's median over `frames`.
export function settledPicture(frames: Float32Array[], size: number): Float32Array {
    const out = new Float32Array(size);
    const series = new Float32Array(frames.length);
    for (let i = 0; i < size; i++) {
        for (let k = 0; k < frames.length; k++) {
            series[k] = frames[k]![i]!;
        }
        out[i] = median(series);
    }
    return out;
}

// One frame's specks and RMS error against the settled picture (luma over the rectangle, row-major w x h).
export function settleRow(
    frame: Float32Array,
    settled: Float32Array,
    w: number,
    h: number,
    threshold: number,
): { specks: number; rms: number } {
    let squares = 0;
    for (let i = 0; i < w * h; i++) {
        const d = frame[i]! - settled[i]!;
        squares += d * d;
    }
    let specks = 0;
    const around = new Float32Array(25);
    for (let y = 2; y < h - 2; y++) {
        for (let x = 2; x < w - 2; x++) {
            const v = frame[y * w + x]!;
            if (v - settled[y * w + x]! <= threshold) {
                continue;
            }
            let k = 0;
            for (let j = -2; j <= 2; j++) {
                for (let i = -2; i <= 2; i++) {
                    around[k++] = frame[(y + j) * w + x + i]!;
                }
            }
            if (v - median(around) > threshold) {
                ++specks;
            }
        }
    }
    return { specks, rms: Math.sqrt(squares / (w * h)) };
}

export function settleCurve(
    frames: Float32Array[],
    rect: Rect,
    settledCount: number,
    threshold: number,
    rows: number[],
): SettleRow[] {
    const size = rect.w * rect.h;
    const settled = settledPicture(frames.slice(Math.max(0, frames.length - settledCount)), size);
    return rows
        .filter((f) => f >= 1 && f <= frames.length)
        .map((f) => ({ frame: f, ...settleRow(frames[f - 1]!, settled, rect.w, rect.h, threshold) }));
}

function lumaOf(file: string, rect: Rect): Float32Array {
    const img = decodePng(fs.readFileSync(file));
    const out = new Float32Array(rect.w * rect.h);
    for (let y = 0; y < rect.h; y++) {
        for (let x = 0; x < rect.w; x++) {
            const s = ((rect.y + y) * img.width + rect.x + x) * 3;
            out[y * rect.w + x] = 0.2126 * img.rgb[s]! + 0.7152 * img.rgb[s + 1]! + 0.0722 * img.rgb[s + 2]!;
        }
    }
    return out;
}

function main(): number {
    const { values, positionals } = parseCli(USAGE, {
        options: {
            settled: { type: "string", default: "30" },
            threshold: { type: "string", default: "30" },
            frames: { type: "string", default: "1,2,3,4,5,6,8,10,12,15,20,25,30,40,50,60" },
        },
        allowPositionals: true,
    });
    if (positionals.length !== 2) {
        print(USAGE);
        return 2;
    }
    const [folder, rectText] = positionals as [string, string];
    const [x, y, w, h] = rectText.split(",").map(Number) as [number, number, number, number];
    const rect = { x, y, w, h };
    const files = fs
        .readdirSync(folder)
        .filter((f) => /^frame_\d+\.png$/.test(f))
        .sort();
    const frames = files.map((f) => lumaOf(path.join(folder, f), rect));
    const rows = settleCurve(
        frames,
        rect,
        Number(values.settled),
        Number(values.threshold),
        String(values.frames).split(",").map(Number),
    );
    print(`${folder}: ${frames.length} frames, settled = median of the last ${values.settled}`);
    print("frame  specks    rms");
    for (const r of rows) {
        print(`${String(r.frame).padStart(5)} ${String(r.specks).padStart(7)} ${r.rms.toFixed(2).padStart(6)}`);
    }
    return 0;
}

if (isMain(import.meta.url)) await runMain(() => main());
