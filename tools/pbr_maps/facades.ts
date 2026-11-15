// NOT ORIGINAL - building fronts' structure for the relief geometry ([Graphics] building_relief; docs/design/vulkan.md
// "Building relief"): a facade texture - a wall with windows in it, in a grid or a row, floor bands, sills - read as
// rectangles in the texture's coordinates, each at a height off the wall (windows recessed, frames raised, sills and
// bands standing out), from which the game builds real geometry (src/engine/gfx/facade_relief.h). Not brightness as
// depth: the windows are found as a regular set of rectangles unlike the wall, the rest from where they are. Textures
// without a clear structure are left flat (no entry).
//
//   the wall     the median colour of the lightly blurred texture (3 x 3, wrapping: the textures tile); each texel's
//                distance from it (RGB, 0 - 1)
//   the grid     the texture's repeats across and down (1 - 16): the most whose shift of the distance map correlates
//                with it at 0.45 or more (and at least 85% of the best), a cell at least 6 texels; a grid's cells are
//                averaged into one (noise and dirt cancel, the windows stay), and the structure is found in that cell
//   windows      the texels at least Otsu's threshold of the distances (0.12 at least) away from the wall - 4 - 55% of
//                them; their connected pieces (wrapping) filling 60% of their box, at least 2 texels (1/48 of the
//                side) a side, not lines (sides within 6 : 1, or a ribbon all across at least 4% thick), at most
//                90% of a side. In a cell: the pieces at least 30% of the largest. In a whole texture (one cell): the
//                largest family of alike pieces (sides within 35%) - at least 2, or 1 away from the edges - snapped
//                into columns and rows (each one's median extent). Their mean distance from the wall 0.15 at least.
//   the wall's   the texels outside the windows and their margins (1/4 of a window's smaller side): their luma's
//   evenness     deviation at most 0.16 (bookshelves, signs, pictures of streets are busier); the windows at most 60%
//                of the region; ribbons no lighter than the wall (glass - not mortar, light stripes)
//   frames       a ring round a window (1/8 of its smaller side, at least a texel) brighter than the wall by 0.06:
//                raised, the window inside it
//   sills        the rows under a window (its ring + 1) brighter than the wall by 0.08 across 70% of its width:
//                a ledge as wide as the frame
//   bands        runs of rows without windows, even across (deviation under 0.08), their luma 0.1 off the wall's, at
//                most 20% of the cell's height: floor bands and cornices, standing out
//
// Heights are in texture widths (u = 1 the whole texture across), so the game turns them into units by how wide the
// texture is drawn: a window's recess 0.35 x its smaller side, a frame 0.06 x, a sill 0.15 x; a band 0.5 x its own
// height. The game caps them in units and scales them by [Graphics] building_relief_strength.
import { luminance, type Rgba } from "./classical.ts";

export type RectKind = "band" | "frame" | "sill" | "window";
export const KIND_CODE: Record<RectKind, number> = { band: 1, frame: 2, sill: 3, window: 4 };

// A rectangle of a cell (fractions of the cell, u0 < u1, v0 < v1; u1 / v1 may pass 1: it wraps into the next cell) at
// a height off the wall in texture widths (+ out of the wall, - into it). Later rectangles are drawn over earlier ones.
export interface ReliefRect {
    kind: RectKind;
    u0: number;
    v0: number;
    u1: number;
    v1: number;
    height: number;
}

// The structure: the cell repeated `repeatU` x `repeatV` times across the texture, its rectangles.
export interface Facade {
    repeatU: number;
    repeatV: number;
    rects: ReliefRect[];
    windows: number; // in the whole texture
    why: string;
}

export type Detection = { facade: Facade; skip?: undefined } | { facade?: undefined; skip: string };

export const RECESS = 0.35;
export const FRAME = 0.06;
export const SILL = 0.15;
export const BAND = 0.5;

const MIN_WINDOW_SHARE = 0.04;
const MAX_UNLIKE_SHARE = 0.55;
const MIN_DISTANCE = 0.12;
const MIN_CONTRAST = 0.15;
const RIBBON_SHARE = 0.04;
const MAX_ASPECT = 6;
const MIN_FILL = 0.6;
const MAX_SIDE = 0.9;
const ALIKE = 0.35;
const CELL_PIECE_SHARE = 0.3;
const MAX_WALL_DEVIATION = 0.16;
const MAX_WINDOW_SHARE = 0.6;
const FRAME_CONTRAST = 0.06;
const SILL_CONTRAST = 0.08;
const SILL_SHARE = 0.7;
const MOST_SILL = 0.3;
const BAND_CONTRAST = 0.1;
const BAND_EVEN = 0.08;
const BAND_MAX_SHARE = 0.2;
const MAX_REPEATS = 16;
const MIN_CELL = 6;
const MIN_CORRELATION = 0.45;
const CELL_RIBBON_SHARE = 0.2;
const HALF_DROP = 0.2;
const SAME_SCORE = 0.05;
const DARKER_GLASS = 0;
const MIN_WINDOWS = 2;
const MIN_GLASS_SHARE = 0.05;
const SNAPPED_FILL = 0.45;
const MIN_WINDOW_FRACTION = 32;
const GRID_OCCUPANCY = 0.5;
const BAND_MIN_SHARE = 0.02;
const MAX_BANDS = 4;

// Otsu's threshold of values in 0 - 1 (64 bins).
export function otsu(values: Float32Array): number {
    const bins = 64;
    const hist = new Float64Array(bins);
    for (const v of values) hist[Math.min(bins - 1, Math.max(0, Math.floor(v * bins)))]!++;
    const total = values.length;
    let sum = 0;
    for (let i = 0; i < bins; i++) sum += i * hist[i]!;
    let below = 0;
    let belowSum = 0;
    let best = 0;
    let bestAt = bins / 2;
    for (let i = 0; i < bins - 1; i++) {
        below += hist[i]!;
        belowSum += i * hist[i]!;
        const above = total - below;
        if (below === 0 || above === 0) continue;
        const m0 = belowSum / below;
        const m1 = (sum - belowSum) / above;
        const between = below * above * (m0 - m1) ** 2;
        if (between > best) {
            best = between;
            bestAt = i + 1;
        }
    }
    return bestAt / bins;
}

const median = (values: ArrayLike<number>): number => {
    if (!values.length) return 0;
    const sorted = Array.from(values).sort((a, b) => a - b);
    const mid = sorted.length >> 1;
    return sorted.length % 2 ? sorted[mid]! : (sorted[mid - 1]! + sorted[mid]!) / 2;
};

const wrapIndex = (x: number, size: number): number => ((Math.floor(x) % size) + size) % size;

// A plane of values (rows top first), tiling.
interface Plane {
    width: number;
    height: number;
    data: Float32Array;
}
const at = (p: Plane, x: number, y: number): number =>
    p.data[wrapIndex(y, p.height) * p.width + wrapIndex(x, p.width)]!;

// A 3 x 3 box blur, wrapping.
function box3(src: Plane): Plane {
    const out = new Float32Array(src.data.length);
    for (let y = 0; y < src.height; y++) {
        for (let x = 0; x < src.width; x++) {
            let sum = 0;
            for (let dy = -1; dy <= 1; dy++) for (let dx = -1; dx <= 1; dx++) sum += at(src, x + dx, y + dy);
            out[y * src.width + x] = sum / 9;
        }
    }
    return { width: src.width, height: src.height, data: out };
}

// A connected piece of a mask (4-connected, wrapping): its box in texels (x1, y1 exclusive; past the size when it
// wraps) and its texel count.
export interface Piece {
    x0: number;
    y0: number;
    x1: number;
    y1: number;
    count: number;
}

export function pieces(mask: Uint8Array, width: number, height: number): Piece[] {
    const seen = new Uint8Array(mask.length);
    const out: Piece[] = [];
    const stack: number[] = [];
    for (let start = 0; start < mask.length; start++) {
        if (!mask[start] || seen[start]) continue;
        // (unwrapped coordinates: a piece crossing an edge keeps going past it)
        let x0 = Infinity;
        let y0 = Infinity;
        let x1 = -Infinity;
        let y1 = -Infinity;
        let count = 0;
        seen[start] = 1;
        stack.push(start % width, Math.floor(start / width));
        while (stack.length) {
            const y = stack.pop()!;
            const x = stack.pop()!;
            x0 = Math.min(x0, x);
            y0 = Math.min(y0, y);
            x1 = Math.max(x1, x + 1);
            y1 = Math.max(y1, y + 1);
            count++;
            for (const [dx, dy] of [
                [1, 0],
                [-1, 0],
                [0, 1],
                [0, -1],
            ] as const) {
                const nx = x + dx;
                const ny = y + dy;
                const index = wrapIndex(ny, height) * width + wrapIndex(nx, width);
                if (mask[index] && !seen[index]) {
                    seen[index] = 1;
                    stack.push(nx, ny);
                }
            }
        }
        // (a piece that runs all round wraps onto itself: its box is the whole side)
        const wide = x1 - x0 >= width;
        const tall = y1 - y0 >= height;
        out.push({
            x0: wide ? 0 : x0,
            y0: tall ? 0 : y0,
            x1: wide ? width : x1,
            y1: tall ? height : y1,
            count,
        });
    }
    return out;
}

// Intervals that overlap grouped (as columns or rows): each group's median start and end.
function snap(intervals: [number, number][]): { groups: [number, number][]; of: number[] } {
    const order = intervals.map((_, i) => i).sort((a, b) => intervals[a]![0] - intervals[b]![0]);
    const groupOf = new Array<number>(intervals.length).fill(-1);
    const members: number[][] = [];
    let end = -Infinity;
    for (const i of order) {
        const [a, b] = intervals[i]!;
        if (a >= end || !members.length) {
            members.push([]);
            end = b;
        } else end = Math.max(end, b);
        groupOf[i] = members.length - 1;
        members[members.length - 1]!.push(i);
    }
    const groups = members.map(
        (list) =>
            [median(list.map((i) => intervals[i]![0])), median(list.map((i) => intervals[i]![1]))] as [number, number],
    );
    return { groups, of: groupOf };
}

// The repeats of a plane along x (`across`) or y (1 - 16, a cell at least MIN_CELL texels): the best correlated k (the
// most among about equal ones) whose shift by
// size / k correlates with the plane at MIN_CORRELATION or more while the shift by half of that correlates at least
// HALF_DROP less (a period: half of it lands between the windows - a smooth texture correlates more at a smaller
// shift, not less), the shift by size / d for every divisor d of k correlating too (whole periods).
export function repeats(plane: Plane, across: boolean): number {
    const size = across ? plane.width : plane.height;
    const mean = plane.data.reduce((s, v) => s + v, 0) / plane.data.length;
    let variance = 0;
    for (const v of plane.data) variance += (v - mean) ** 2;
    if (variance <= 1e-9) return 1;
    const correlation = (shift: number): number => {
        let sum = 0;
        for (let y = 0; y < plane.height; y++) {
            for (let x = 0; x < plane.width; x++) {
                // (a fractional shift read between its two texels)
                const s = across ? x + shift : y + shift;
                const s0 = Math.floor(s);
                const f = s - s0;
                const a = across ? at(plane, s0, y) : at(plane, x, s0);
                const b = across ? at(plane, s0 + 1, y) : at(plane, x, s0 + 1);
                sum += (plane.data[y * plane.width + x]! - mean) * (a * (1 - f) + b * f - mean);
            }
        }
        return sum / variance;
    };
    const scores = new Map<number, number>();
    const periodic = new Map<number, boolean>();
    for (let k = 2; k <= MAX_REPEATS && size / k >= MIN_CELL; k++) {
        const r = correlation(size / k);
        scores.set(k, r);
        periodic.set(k, r >= MIN_CORRELATION && correlation(size / (2 * k)) <= r - HALF_DROP);
    }
    let chosen = 1;
    let best = -Infinity;
    for (const [k, yes] of periodic) {
        let divisors = yes;
        for (let d = 2; d < k && divisors; d++)
            if (k % d === 0 && (scores.get(d) ?? 0) < MIN_CORRELATION) divisors = false;
        // (the best correlated - a shift past a whole period correlates less; among about equal ones the most)
        const r = scores.get(k)!;
        if (divisors && r >= best - SAME_SCORE) {
            if (r > best) best = r;
            chosen = k;
        }
    }
    return chosen;
}

// The mean of a plane's cells (repeatU x repeatV of them), sampled at round(cell) texels a side.
function meanCell(plane: Plane, repeatU: number, repeatV: number): Plane {
    const cellW = plane.width / repeatU;
    const cellH = plane.height / repeatV;
    const width = Math.max(1, Math.round(cellW));
    const height = Math.max(1, Math.round(cellH));
    const data = new Float32Array(width * height);
    for (let y = 0; y < height; y++) {
        for (let x = 0; x < width; x++) {
            let sum = 0;
            for (let j = 0; j < repeatV; j++) {
                for (let i = 0; i < repeatU; i++) {
                    sum += at(plane, i * cellW + ((x + 0.5) * cellW) / width, j * cellH + ((y + 0.5) * cellH) / height);
                }
            }
            data[y * width + x] = sum / (repeatU * repeatV);
        }
    }
    return { width, height, data };
}

interface Box {
    x0: number;
    y0: number;
    x1: number;
    y1: number;
}

type Windows = { boxes: Box[]; skip?: undefined } | { boxes?: undefined; skip: string };

// The windows of a region (a grid's mean cell - `cell` - or a whole texture), in its texels.
function findWindows(distance: Plane, cell: boolean, smallest: number): Windows {
    const { width, height } = distance;
    const n = width * height;
    const threshold = Math.max(MIN_DISTANCE, otsu(distance.data));
    const mask = new Uint8Array(n);
    let unlike = 0;
    for (let i = 0; i < n; i++) {
        if (distance.data[i]! >= threshold) {
            mask[i] = 1;
            unlike++;
        }
    }
    if (unlike / n < MIN_WINDOW_SHARE || unlike / n > MAX_UNLIKE_SHARE) return { skip: "no share like windows" };
    const candidates = pieces(mask, width, height).filter((p) => {
        const w = p.x1 - p.x0;
        const h = p.y1 - p.y0;
        // (a ribbon - glass all across - thick enough; anything else not a line)
        const ribbon = Math.max(smallest, (cell ? CELL_RIBBON_SHARE : RIBBON_SHARE) * Math.min(width, height));
        const shape =
            w >= MAX_SIDE * width
                ? h >= ribbon
                : h >= MAX_SIDE * height
                  ? w >= ribbon
                  : Math.max(w / h, h / w) <= MAX_ASPECT;
        return (
            w >= smallest &&
            h >= smallest &&
            shape &&
            p.count / (w * h) >= MIN_FILL &&
            (w <= MAX_SIDE * width || h <= MAX_SIDE * height)
        );
    });
    if (!candidates.length) return { skip: "no window-shaped pieces" };
    let chosen: Piece[];
    if (cell) {
        const largest = Math.max(...candidates.map((p) => p.count));
        chosen = candidates.filter((p) => p.count >= CELL_PIECE_SHARE * largest);
    } else {
        chosen = [];
        const area = (list: Piece[]) => list.reduce((s, q) => s + q.count, 0);
        for (const p of candidates) {
            const alike = candidates.filter(
                (q) =>
                    Math.abs(q.x1 - q.x0 - (p.x1 - p.x0)) <= ALIKE * Math.max(q.x1 - q.x0, p.x1 - p.x0) &&
                    Math.abs(q.y1 - q.y0 - (p.y1 - p.y0)) <= ALIKE * Math.max(q.y1 - q.y0, p.y1 - p.y0),
            );
            if (alike.length > chosen.length || (alike.length === chosen.length && area(alike) > area(chosen))) {
                chosen = alike;
            }
        }
        const inside = (p: Piece) => p.x0 > 0 && p.y0 > 0 && p.x1 < width && p.y1 < height;
        if (chosen.length < 2 && !(chosen.length === 1 && inside(chosen[0]!))) return { skip: "no regular windows" };
    }
    let contrast = 0;
    let count = 0;
    for (const p of chosen) {
        for (let y = p.y0; y < p.y1; y++) {
            for (let x = p.x0; x < p.x1; x++) {
                const index = wrapIndex(y, height) * width + wrapIndex(x, width);
                if (mask[index]) {
                    contrast += distance.data[index]!;
                    count++;
                }
            }
        }
    }
    if (contrast / count < MIN_CONTRAST) return { skip: "windows too close to the wall" };
    // (snapped into columns and rows: a grid comes out as one)
    const columns = snap(chosen.map((p) => [p.x0, p.x1]));
    const rows = snap(chosen.map((p) => [p.y0, p.y1]));
    const unique = new Map<string, Box>();
    chosen.forEach((_, i) => {
        const [x0, x1] = columns.groups[columns.of[i]!]!;
        const [y0, y1] = rows.groups[rows.of[i]!]!;
        unique.set(`${x0},${x1},${y0},${y1}`, { x0, x1, y0, y1 });
    });
    const boxes = [...unique.values()];
    // (snapped, each still mostly unlike the wall; a whole texture's windows filling half their grid's places at least)
    for (const b of boxes) {
        let inMask = 0;
        for (let y = b.y0; y < b.y1; y++)
            for (let x = b.x0; x < b.x1; x++) inMask += mask[wrapIndex(y, height) * width + wrapIndex(x, width)]!;
        if (inMask < SNAPPED_FILL * (b.x1 - b.x0) * (b.y1 - b.y0)) return { skip: "windows not rectangles" };
    }
    if (!cell && boxes.length < GRID_OCCUPANCY * columns.groups.length * rows.groups.length)
        return { skip: "windows not in a grid" };
    return { boxes };
}

// The structure of a region round its windows (frames, sills, bands) and the windows themselves, in the region's
// fractions, heights in region texels (turned into texture widths by the caller); or why it isn't a facade.
function structure(
    luma: Plane,
    found: Box[],
): { rects: ReliefRect[]; why: string; skip?: undefined } | { rects?: undefined; skip: string } {
    const { width, height } = luma;
    const n = width * height;
    let windows = found;
    const covered = new Uint8Array(n);
    let windowArea = 0;
    const marginOf = (r: Box) => Math.max(1, Math.round(Math.min(r.x1 - r.x0, r.y1 - r.y0) / 4));
    for (const r of windows) {
        const margin = marginOf(r);
        for (let y = r.y0 - margin; y < r.y1 + margin; y++) {
            for (let x = r.x0 - margin; x < r.x1 + margin; x++) {
                covered[wrapIndex(y, height) * width + wrapIndex(x, width)] = 1;
            }
        }
        windowArea += (r.x1 - r.x0) * (r.y1 - r.y0);
    }
    if (windowArea > MAX_WINDOW_SHARE * n) return { skip: "mostly windows" };
    let wallSum = 0;
    let wallSquares = 0;
    let wallCount = 0;
    for (let i = 0; i < n; i++) {
        if (covered[i]) continue;
        wallSum += luma.data[i]!;
        wallSquares += luma.data[i]! ** 2;
        wallCount++;
    }
    if (wallCount < 0.1 * n) return { skip: "no wall round the windows" };
    const wall = wallSum / wallCount;
    const deviation = Math.sqrt(Math.max(0, wallSquares / wallCount - wall ** 2));
    if (deviation > MAX_WALL_DEVIATION) return { skip: "the wall too busy" };
    // (ribbons - glass all across - read no lighter than the wall: mortar and light stripes do)
    let glass = 0;
    let glassCount = 0;
    const ribbons = windows.filter((r) => r.x1 - r.x0 >= MAX_SIDE * width || r.y1 - r.y0 >= MAX_SIDE * height);
    for (const r of ribbons) {
        for (let y = r.y0; y < r.y1; y++) {
            for (let x = r.x0; x < r.x1; x++) {
                glass += at(luma, x, y);
                glassCount++;
            }
        }
    }
    if (glassCount && glass / glassCount > wall - DARKER_GLASS) return { skip: "ribbons not darker than the wall" };
    const meanOf = (x0: number, y0: number, x1: number, y1: number): number => {
        let sum = 0;
        let count = 0;
        for (let y = y0; y < y1; y++) {
            for (let x = x0; x < x1; x++) {
                sum += at(luma, x, y);
                count++;
            }
        }
        return count ? sum / count : wall;
    };
    const out: ReliefRect[] = [];
    const u = (x: number) => x / width;
    const v = (y: number) => y / height;
    // (bands: rows without windows, even across, unlike the wall)
    const windowRows = new Uint8Array(height);
    for (const r of windows) {
        const margin = marginOf(r);
        for (let y = r.y0 - margin; y < r.y1 + margin; y++) windowRows[wrapIndex(y, height)] = 1;
    }
    const banded = (y: number): boolean => {
        if (windowRows[y]) return false;
        let sum = 0;
        let squares = 0;
        for (let x = 0; x < width; x++) {
            sum += luma.data[y * width + x]!;
            squares += luma.data[y * width + x]! ** 2;
        }
        const mean = sum / width;
        return (
            Math.sqrt(Math.max(0, squares / width - mean ** 2)) < BAND_EVEN && Math.abs(mean - wall) >= BAND_CONTRAST
        );
    };
    let bands = 0;
    for (let y = 0; y < height; ) {
        if (!banded(y)) {
            y++;
            continue;
        }
        let end = y;
        while (end < height && banded(end)) end++;
        if (end - y <= BAND_MAX_SHARE * height && end - y >= BAND_MIN_SHARE * height && bands < MAX_BANDS) {
            out.push({ kind: "band", u0: 0, v0: v(y), u1: 1, v1: v(end), height: BAND * (end - y) });
            bands++;
        }
        y = end;
    }
    // (a sill painted against the glass is in the window's piece: its bottom rows brighter than the wall - taken off
    // the window and made the sill, at most 30% of it)
    const trimmed: Box[] = [];
    const sillRows: Box[] = [];
    for (const r of windows) {
        let bottom = r.y1;
        while (
            bottom - 1 > r.y0 + (r.y1 - r.y0) * (1 - MOST_SILL) &&
            meanOf(r.x0, bottom - 1, r.x1, bottom) - wall >= SILL_CONTRAST
        ) {
            bottom--;
        }
        trimmed.push({ ...r, y1: bottom });
        if (bottom < r.y1) sillRows.push({ x0: r.x0, y0: bottom, x1: r.x1, y1: r.y1 });
    }
    windows = trimmed;
    // (frames and sills round each window)
    let framed = 0;
    let sills = 0;
    for (const r of windows) {
        const side = Math.min(r.x1 - r.x0, r.y1 - r.y0);
        const ring = Math.max(1, Math.round(side / 8));
        const outer =
            (meanOf(r.x0 - ring, r.y0 - ring, r.x1 + ring, r.y0) +
                meanOf(r.x0 - ring, r.y1, r.x1 + ring, r.y1 + ring) +
                meanOf(r.x0 - ring, r.y0, r.x0, r.y1) +
                meanOf(r.x1, r.y0, r.x1 + ring, r.y1)) /
            4;
        if (outer - wall >= FRAME_CONTRAST) {
            out.push({
                kind: "frame",
                u0: u(r.x0 - ring),
                v0: v(r.y0 - ring),
                u1: u(r.x1 + ring),
                v1: v(r.y1 + ring),
                height: FRAME * side,
            });
            framed++;
        }
        // (a sill: rows just under the window - within its ring + 2 - bright across most of its width)
        const brightRow = (y: number): boolean => {
            let bright = 0;
            for (let x = r.x0; x < r.x1; x++) if (at(luma, x, y) - wall >= SILL_CONTRAST) bright++;
            return bright >= SILL_SHARE * (r.x1 - r.x0);
        };
        let first = -1;
        let last = -1;
        for (let y = r.y1; y < r.y1 + ring + 2; y++) {
            if (brightRow(y)) {
                if (first < 0) first = y;
                last = y + 1;
            } else if (first >= 0) break;
        }
        const cut = sillRows.find((b) => b.x0 === r.x0 && b.x1 === r.x1 && b.y0 === r.y1);
        if (cut) {
            first = cut.y0;
            last = Math.max(cut.y1, last);
        }
        if (first >= 0) {
            out.push({
                kind: "sill",
                u0: u(r.x0 - ring),
                v0: v(first),
                u1: u(r.x1 + ring),
                v1: v(last),
                height: SILL * side,
            });
            sills++;
        }
    }
    for (const r of windows) {
        const side = Math.min(r.x1 - r.x0, r.y1 - r.y0);
        out.push({ kind: "window", u0: u(r.x0), v0: v(r.y0), u1: u(r.x1), v1: v(r.y1), height: -RECESS * side });
    }
    // (rectangles starting past the region moved back into it; those starting before it wrapped round)
    for (const r of out) {
        const du = Math.floor(r.u0);
        const dv = Math.floor(r.v0);
        r.u0 -= du;
        r.u1 -= du;
        r.v0 -= dv;
        r.v1 -= dv;
        // (at most a cell wide: the file's rule)
        r.u1 = Math.min(r.u1, r.u0 + 1);
        r.v1 = Math.min(r.v1, r.v0 + 1);
    }
    return {
        rects: out,
        why: `${framed} framed, ${sills} sills, ${bands} bands; wall ${wall.toFixed(2)} +- ${deviation.toFixed(2)}`,
    };
}

// The facade structure of a texture, or why it has none.
export function detectFacade(image: Rgba): Detection {
    const { width, height } = image;
    if (width < 16 || height < 16) return { skip: "too small" };
    const n = width * height;
    const luma: Plane = { width, height, data: luminance(image) };
    // (the wall's colour: the median of each channel of the lightly blurred texture; a texel's distance from it)
    const channels = [0, 1, 2].map((c) => {
        const plane = new Float32Array(n);
        for (let i = 0; i < n; i++) plane[i] = image.data[i * 4 + c]! / 255;
        return box3({ width, height, data: plane });
    });
    const wallColour = channels.map((plane) => median(plane.data));
    const distanceData = new Float32Array(n);
    for (let i = 0; i < n; i++) {
        distanceData[i] = Math.hypot(
            channels[0]!.data[i]! - wallColour[0]!,
            channels[1]!.data[i]! - wallColour[1]!,
            channels[2]!.data[i]! - wallColour[2]!,
        );
    }
    const distance: Plane = { width, height, data: distanceData };
    // (a grid: its cells averaged into one; else the whole texture)
    const repeatU = repeats(distance, true);
    const repeatV = repeats(distance, false);
    const grid = repeatU * repeatV > 1;
    const regionDistance = grid ? meanCell(distance, repeatU, repeatV) : distance;
    const regionLuma = grid ? meanCell(luma, repeatU, repeatV) : luma;
    // (a window at least 1/32 of the texture's smaller side a side, in the region's texels)
    const regionScale = width / repeatU / regionDistance.width;
    const smallest = Math.max(2, Math.round(Math.min(width, height) / MIN_WINDOW_FRACTION / regionScale));
    const found = findWindows(regionDistance, grid, smallest);
    if (!found.boxes) return { skip: found.skip };
    if (found.boxes.length * repeatU * repeatV < MIN_WINDOWS) return { skip: "one window (a door, a sign?)" };
    const glassArea = found.boxes.reduce((sum, b) => sum + (b.x1 - b.x0) * (b.y1 - b.y0), 0);
    if (glassArea < MIN_GLASS_SHARE * regionDistance.width * regionDistance.height)
        return { skip: "specks, not windows" };
    const made = structure(regionLuma, found.boxes);
    if (!made.rects) return { skip: made.skip };
    // (heights in region texels to texture widths: a region texel is (cell width / its texels) texture texels)
    const texelsPerRegionTexel = width / repeatU / regionLuma.width;
    for (const r of made.rects) r.height = (r.height * texelsPerRegionTexel) / width;
    const windows = found.boxes.length * repeatU * repeatV;
    return {
        facade: {
            repeatU,
            repeatV,
            rects: made.rects,
            windows,
            why: `${windows} windows (cell ${repeatU} x ${repeatV}, ${found.boxes.length} a cell), ${made.why}`,
        },
    };
}

// The height at texture point (u, v) (any repeat of the texture): the last rectangle of its cell holding it, else 0
// (the wall). In texture widths.
export function heightAt(facade: Pick<Facade, "repeatU" | "repeatV" | "rects">, u: number, v: number): number {
    const cu = u * facade.repeatU;
    const cv = v * facade.repeatV;
    const fu = cu - Math.floor(cu);
    const fv = cv - Math.floor(cv);
    let h = 0;
    for (const r of facade.rects) {
        const inU = (fu >= r.u0 && fu < r.u1) || (fu + 1 >= r.u0 && fu + 1 < r.u1);
        const inV = (fv >= r.v0 && fv < r.v1) || (fv + 1 >= r.v0 && fv + 1 < r.v1);
        if (inU && inV) h = r.height;
    }
    return h;
}

// The structure drawn as a height map (0.5 the wall; `range` texture widths to 0 / 1): the pictures, and the height
// a surface map can carry.
export function heightMap(
    facade: Pick<Facade, "repeatU" | "repeatV" | "rects">,
    width: number,
    height: number,
    range = 0.05,
): Uint8Array {
    const out = new Uint8Array(width * height);
    for (let y = 0; y < height; y++) {
        for (let x = 0; x < width; x++) {
            const h = heightAt(facade, (x + 0.5) / width, (y + 0.5) / height);
            out[y * width + x] = Math.max(0, Math.min(255, Math.round(127.5 + (127.5 * h) / range)));
        }
    }
    return out;
}
