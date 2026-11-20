// Is the same spot lit the same from launch to launch? Series of the same held camera captured in different launches
// (automation `capture <frames> <folder>`), each reduced to a grid of cells' mean luma (0-255, Rec. 709 weights on the
// PNG's bytes) over its frames; per cell the launches' spread against the frame-to-frame noise of that cell's mean.
//   node tools/game/launch_compare.ts <series folder> <series folder> [...] [--grid 8x6] [--rect x,y,w,h] [--skip N]
// Prints each launch's whole-rectangle mean, then the cells whose launches differ most: the spread (largest - smallest
// launch mean), the ratio and the noise (the standard error of a cell's mean over the series' frames, the largest of
// the launches') - a spread many times the noise is a lighting state that differs, not sampling noise.
import * as fs from "node:fs";
import * as path from "node:path";
import { isMain, parseCli, print, runMain } from "thug2-level-converter-core/lib/cli.ts";
import { decodePng } from "thug2-level-converter-core/lib/frame_diff.ts";

const USAGE =
    "usage: launch_compare.ts <series folder> <series folder> [...] [--grid CxR] [--rect x,y,w,h] [--skip N] [--top N]";

export interface CellSeries {
    means: number[]; // each frame's cell mean
}

// A frame's cell means over a grid of `columns` x `rows` cells of the rectangle.
export function cellMeans(
    rgb: Uint8Array,
    width: number,
    rect: { x: number; y: number; w: number; h: number },
    columns: number,
    rows: number,
): number[] {
    const sums = new Float64Array(columns * rows);
    const counts = new Float64Array(columns * rows);
    for (let y = 0; y < rect.h; y++) {
        const row = Math.min(Math.floor((y * rows) / rect.h), rows - 1);
        for (let x = 0; x < rect.w; x++) {
            const column = Math.min(Math.floor((x * columns) / rect.w), columns - 1);
            const s = ((rect.y + y) * width + rect.x + x) * 3;
            const cell = row * columns + column;
            sums[cell]! += 0.2126 * rgb[s]! + 0.7152 * rgb[s + 1]! + 0.0722 * rgb[s + 2]!;
            counts[cell]! += 1;
        }
    }
    return Array.from(sums, (sum, i) => (counts[i]! > 0 ? sum / counts[i]! : 0));
}

// A series' per-cell mean over its frames and the standard error of that mean.
export function seriesCells(frames: number[][]): { mean: number[]; error: number[] } {
    const n = frames.length;
    const cells = frames[0]?.length ?? 0;
    const mean: number[] = [];
    const error: number[] = [];
    for (let c = 0; c < cells; c++) {
        let sum = 0;
        for (const f of frames) sum += f[c]!;
        const m = sum / n;
        let squares = 0;
        for (const f of frames) squares += (f[c]! - m) * (f[c]! - m);
        mean.push(m);
        error.push(n > 1 ? Math.sqrt(squares / (n - 1) / n) : 0);
    }
    return { mean, error };
}

function main(): number {
    const { values, positionals } = parseCli(USAGE, {
        options: {
            grid: { type: "string", default: "8x6" },
            rect: { type: "string", default: "" },
            skip: { type: "string", default: "0" },
            top: { type: "string", default: "8" },
        },
        allowPositionals: true,
    });
    if (positionals.length < 2) {
        print(USAGE);
        return 2;
    }
    const [columns, rows] = values.grid.split("x").map(Number) as [number, number];
    const launches = positionals.map((folder) => {
        const files = fs
            .readdirSync(folder)
            .filter((f) => /^frame_\d+\.png$/.test(f))
            .sort()
            .slice(Number(values.skip));
        let rect = { x: 0, y: 0, w: 0, h: 0 };
        const frames = files.map((f) => {
            const img = decodePng(fs.readFileSync(path.join(folder, f)));
            if (rect.w === 0) {
                rect =
                    values.rect !== ""
                        ? (([x, y, w, h]) => ({ x: x!, y: y!, w: w!, h: h! }))(values.rect.split(",").map(Number))
                        : { x: 0, y: 0, w: img.width, h: img.height };
            }
            return cellMeans(img.rgb, img.width, rect, columns, rows);
        });
        return { folder, ...seriesCells(frames) };
    });
    for (const l of launches) {
        const whole = l.mean.reduce((a, b) => a + b, 0) / l.mean.length;
        print(`${l.folder}: mean ${whole.toFixed(2)}`);
    }
    const cells = launches[0]!.mean.map((_, c) => {
        const means = launches.map((l) => l.mean[c]!);
        const low = Math.min(...means);
        const high = Math.max(...means);
        const noise = Math.max(...launches.map((l) => l.error[c]!), 1e-3);
        return { c, low, high, spread: high - low, ratio: low > 0 ? high / low : Infinity, noise };
    });
    cells.sort((a, b) => b.spread / b.noise - a.spread / a.noise);
    const worst = cells.reduce((a, b) => (b.spread > a.spread ? b : a));
    print(
        `largest spread: cell ${worst.c % columns},${Math.floor(worst.c / columns)} ${worst.low.toFixed(2)} - ${worst.high.toFixed(2)}`,
    );
    for (const cell of cells.slice(0, Number(values.top))) {
        print(
            `cell ${cell.c % columns},${Math.floor(cell.c / columns)}: ${cell.low.toFixed(2)} - ${cell.high.toFixed(2)} ` +
                `(x${cell.ratio.toFixed(3)}), spread ${(cell.spread / cell.noise).toFixed(1)} x the noise`,
        );
    }
    return 0;
}

if (isMain(import.meta.url)) await runMain(() => main());
