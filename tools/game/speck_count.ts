// Specks in a captured series (the automation's `capture <frames> <folder>` with the camera held): isolated bright
// pixels over a surface, counted in a rectangle - what Ray Reconstruction keeps of a noisy estimator (the white specks
// on BO's kickers / ledge at night).
//   node tools/game/speck_count.ts <series folder> <x,y,w,h> [--skip N] [--threshold 30]
// A speck: a pixel whose luma (0-255, Rec. 709 weights on the PNG's bytes) is over its 5x5 neighbourhood's median by the
// threshold. Two kinds: flickering (also over the pixel's own median over the series by the threshold - new each frame)
// and steady (over its neighbourhood but not over its own median: the same glint frame after frame). Also each pixel's
// standard deviation over the series (temporal noise), averaged over the rectangle.
import * as fs from "node:fs";
import * as path from "node:path";
import { isMain, parseCli, print, runMain } from "thug2-level-converter-core/lib/cli.ts";
import { decodePng } from "thug2-level-converter-core/lib/frame_diff.ts";

const USAGE = "usage: speck_count.ts <series folder> x,y,w,h [--skip N] [--threshold T]";

export interface Rect {
    x: number;
    y: number;
    w: number;
    h: number;
}

export interface SpeckStats {
    frames: number;
    flickering: number; // a frame, on average
    steady: number; // a frame, on average
    std: number; // the pixels' temporal standard deviation, mean over the rectangle
}

function median(values: Float32Array): number {
    values.sort();
    return values[values.length >> 1]!;
}

// The frames' luma over the rectangle, frame-major.
export function speckStats(frames: Float32Array[], rect: Rect, threshold: number): SpeckStats {
    const { w, h } = rect;
    const n = frames.length;
    const own = new Float32Array(w * h);
    let stdSum = 0;
    const series = new Float32Array(n);
    for (let i = 0; i < w * h; i++) {
        let sum = 0;
        for (let k = 0; k < n; k++) {
            series[k] = frames[k]![i]!;
            sum += series[k]!;
        }
        const mean = sum / n;
        let squares = 0;
        for (let k = 0; k < n; k++) {
            squares += (series[k]! - mean) * (series[k]! - mean);
        }
        stdSum += Math.sqrt(squares / n);
        own[i] = median(series);
    }
    let flickering = 0;
    let steady = 0;
    const around = new Float32Array(25);
    for (const f of frames) {
        for (let y = 2; y < h - 2; y++) {
            for (let x = 2; x < w - 2; x++) {
                const v = f[y * w + x]!;
                if (v <= threshold) {
                    continue; // (quick out: no neighbourhood median is under 0)
                }
                let k = 0;
                for (let j = -2; j <= 2; j++) {
                    for (let i = -2; i <= 2; i++) {
                        around[k++] = f[(y + j) * w + x + i]!;
                    }
                }
                if (v - median(around) <= threshold) {
                    continue;
                }
                if (v - own[y * w + x]! > threshold) {
                    ++flickering;
                } else {
                    ++steady;
                }
            }
        }
    }
    return { frames: n, flickering: flickering / n, steady: steady / n, std: stdSum / (w * h) };
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
        options: { skip: { type: "string", default: "0" }, threshold: { type: "string", default: "30" } },
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
        .sort()
        .slice(Number(values.skip));
    const frames = files.map((f) => lumaOf(path.join(folder, f), rect));
    const s = speckStats(frames, rect, Number(values.threshold));
    print(
        `${folder}: ${s.frames} frames, specks a frame ${s.flickering.toFixed(1)} flickering + ${s.steady.toFixed(1)} steady, ` +
            `per-pixel std ${s.std.toFixed(2)}`,
    );
    return 0;
}

if (isMain(import.meta.url)) await runMain(() => main());
