// NOT ORIGINAL - the slow waves of a held view's light (round LW): a series of linear frames (`console pt frames <count>
// <folder> [every]` with the world frozen - `console measure on` - and the camera held) split into a grid of cells, each
// cell's mean light per frame (over the frame's pre-exposure: the global unit), smoothed over a window that takes the
// per-frame noise out, and how far each cell's smoothed light wanders: the lights "fading in and out" over seconds
// that per-pixel noise measures (speck_count.ts) don't see.
//
//   node tools/game/still_series.ts <Screenshots folder> [--grid CxR] [--smooth N] [--skip N] [--json out.json]
//     per cell: the smoothed light's range (max - min) and standard deviation over its mean; their median and largest
//     over the cells (cells darker than 2 % of the frame's mean left out - noise over nothing), and the whole frame's
//     mean's. --smooth N: the window in frames of the series (default 15 / every); --skip N: frames left out at the start.
import * as fs from "node:fs";
import * as path from "node:path";
import { isMain, parseCli, print, runMain } from "thug2-level-converter-core/lib/cli.ts";
import { cellMeans, parseFrames, readExr } from "./motion_error.ts";

const USAGE = "usage: still_series.ts <Screenshots folder> [--grid CxR] [--smooth N] [--skip N] [--json out.json]";

export interface Wander {
    range: number; // (max - min) / mean of the smoothed series
    std: number; // standard deviation / mean of the smoothed series
}

// A box mean over `window` samples (shorter at the ends: none past them).
export function smoothed(series: number[], window: number): number[] {
    const half = Math.max(0, Math.floor(window / 2));
    const out: number[] = [];
    for (let i = half; i < series.length - half; i++) {
        let sum = 0;
        for (let k = i - half; k <= i + half; k++) sum += series[k]!;
        out.push(sum / (2 * half + 1));
    }
    return out;
}

export function wander(series: number[]): Wander {
    if (series.length === 0) return { range: 0, std: 0 };
    const mean = series.reduce((a, b) => a + b, 0) / series.length;
    if (!(mean > 0)) return { range: 0, std: 0 };
    const variance = series.reduce((a, b) => a + (b - mean) ** 2, 0) / series.length;
    return { range: (Math.max(...series) - Math.min(...series)) / mean, std: Math.sqrt(variance) / mean };
}

function median(values: number[]): number {
    if (values.length === 0) return 0;
    const sorted = [...values].sort((a, b) => a - b);
    return sorted[Math.floor(sorted.length / 2)]!;
}

export interface SeriesResult {
    frames: number;
    cells: number;
    rangeMedian: number;
    rangeMost: number;
    stdMedian: number;
    stdMost: number;
    whole: Wander;
    wholeMean: number;
    // each cell's series' autocorrelation at these lags (in samples of the series), averaged over the cells: how long its
    // light keeps an error - 0 at a lag of one sample is noise new every sample, near 1 a slow wave
    lags: number[];
    correlation: number[];
}

// A series' autocorrelation at `lag` (its mean taken out; 0 for a constant series).
export function autocorrelation(series: number[], lag: number): number {
    const n = series.length;
    if (n <= lag + 1) return 0;
    const mean = series.reduce((a, b) => a + b, 0) / n;
    let v = 0;
    for (const x of series) v += (x - mean) ** 2;
    if (!(v > 0)) return 0;
    let c = 0;
    for (let i = 0; i + lag < n; i++) c += (series[i]! - mean) * (series[i + lag]! - mean);
    return c / (n - lag) / (v / n);
}
export const kLags = [1, 2, 4, 8, 16];

// The cells' series (each frame's cell means, the last entry the whole frame's) -> the result.
export function seriesResult(perFrame: number[][], window: number): SeriesResult {
    const count = perFrame[0]?.length ?? 0;
    const cells = count - 1;
    const whole = perFrame.map((f) => f[cells]!);
    const wholeMean = whole.reduce((a, b) => a + b, 0) / Math.max(1, whole.length);
    const ranges: number[] = [];
    const stds: number[] = [];
    const correlation = kLags.map(() => 0);
    for (let c = 0; c < cells; c++) {
        const s = perFrame.map((f) => f[c]!);
        const mean = s.reduce((a, b) => a + b, 0) / Math.max(1, s.length);
        if (!(mean > 0.02 * wholeMean)) continue;
        const w = wander(smoothed(s, window));
        kLags.forEach((lag, k) => {
            correlation[k]! += autocorrelation(s, lag);
        });
        ranges.push(w.range);
        stds.push(w.std);
    }
    return {
        frames: perFrame.length,
        cells: ranges.length,
        rangeMedian: median(ranges),
        rangeMost: ranges.length > 0 ? Math.max(...ranges) : 0,
        stdMedian: median(stds),
        stdMost: stds.length > 0 ? Math.max(...stds) : 0,
        whole: wander(smoothed(whole, window)),
        wholeMean,
        lags: kLags,
        correlation: correlation.map((c) => (ranges.length > 0 ? c / ranges.length : 0)),
    };
}

function main(): number {
    const { values, positionals } = parseCli(USAGE, {
        allowPositionals: true,
        options: {
            grid: { type: "string", default: "8x6" },
            smooth: { type: "string" },
            skip: { type: "string", default: "0" },
            json: { type: "string" },
        },
    });
    if (positionals.length !== 1) {
        print(USAGE);
        return 2;
    }
    const folder = positionals[0]!;
    const info = path.join(folder, "frames.txt");
    if (!fs.existsSync(info)) {
        print(`still_series: no frames.txt in ${folder}`);
        return 1;
    }
    const [cols, rows] = String(values.grid).split("x").map(Number) as [number, number];
    const frames = parseFrames(fs.readFileSync(info, "utf8"));
    const skip = Number(values.skip);
    // (the series' spacing: frames.txt's frame numbers)
    const step = frames.length > 1 ? Math.max(1, frames[1]!.index - frames[0]!.index) : 1;
    const window = values.smooth !== undefined ? Number(values.smooth) : Math.max(1, Math.round(15 / step));
    const perFrame: number[][] = [];
    for (let i = skip; i < frames.length; i++) {
        const file = path.join(folder, `frame_${String(i + 1).padStart(4, "0")}.exr`);
        if (!fs.existsSync(file)) break;
        const means = cellMeans(readExr(fs.readFileSync(file)), cols, rows);
        const exposure = frames[i]!.preExposure > 0 ? frames[i]!.preExposure : 1;
        perFrame.push(means.map((m) => m / exposure));
    }
    const result = seriesResult(perFrame, window);
    const pct = (v: number): string => `${(v * 100).toFixed(1)} %`;
    print(
        `${folder}: ${result.frames} frames (every ${step}), ${result.cells} cells of ${cols}x${rows}, window ${window}\n` +
            `  cells' slow range median ${pct(result.rangeMedian)}, most ${pct(result.rangeMost)}; ` +
            `slow std median ${pct(result.stdMedian)}, most ${pct(result.stdMost)}\n` +
            `  whole frame: mean ${result.wholeMean.toExponential(3)}, slow range ${pct(result.whole.range)}, ` +
            `std ${pct(result.whole.std)}
` +
            `  cells' autocorrelation at lags ${result.lags.join(", ")}: ${result.correlation.map((c) => c.toFixed(2)).join(", ")}`,
    );
    if (values.json) fs.writeFileSync(String(values.json), JSON.stringify(result, null, 2));
    return 0;
}

if (isMain(import.meta.url)) runMain(main);
