// NOT ORIGINAL - the level select's signs for the levels we add (tools/menu_level/level_select.ts): THUG2's sign
// frame (the road-sign diamond of its sign_training) with a pictogram of our own drawn in it, as the game's
// images\LevelSelectSprites\<name>.img.xbx.
//
// The game's level select sprites (Levelselect.prx) are 8-bit paletted images:
//   u32 version (2), u32 bits per texel (8), u32 width, u32 height, u32 format (0x13 = paletted), u32 0,
//   u16 width, u16 height, u32 palette size in bytes (64: 16 BGRA colours), the palette, then width * height
//   indices stored swizzled (tools/level_port/tex.ts unswizzleOrder) - read by 0x004E0790.
// Nothing of the game is stored here: the frame is read from the user's Levelselect.prx each time.
//
// A preview:
//   node tools/menu_level/level_select_icons.ts <out.png> [--game C:\Games\THUG2] [pictogram...]
// draws the signs (3x, on grey) into a PNG to look at.

import * as fs from "node:fs";
import * as path from "node:path";
import { parseArgs } from "node:util";
import * as zlib from "node:zlib";
import { unswizzleOrder } from "../level_port/tex.ts";
import { isMain, print, pyHypot, pyJoin, pyMinBy, runMain } from "../lib/py.ts";
import { pack, unpackNumbers } from "../lib/struct.ts";

export const SIZE = 64;
export const FRAME_SOURCE = "images\\LevelSelectSprites\\sign_training.img.xbx";
// The diamond's inside: |x - 32| + |y - 32| <= INSIDE (pixel centres) is cleared to the fill and drawn on; the
// frame (white edge, grey bevel) is outside it.
export const INSIDE = 26.0;
export const FILL = 0xd7;

// A palette colour: [b, g, r, a].
export type Colour = [number, number, number, number];

export interface Img {
    w: number;
    h: number;
    palette: Colour[];
    indices: number[];
}

export function readImg(data: Buffer): Img {
    const [version, depth, w, h, fmt, , w2, h2, paletteSize] = unpackNumbers("<IIIIIIHHI", data, 0) as number[];
    if (version !== 2 || depth !== 8 || fmt !== 0x13 || w2 !== w || h2 !== h) {
        throw new Error(
            `not an 8-bit paletted img.xbx (version ${version}, ${depth} bits, format 0x${fmt!.toString(16).toUpperCase()})`,
        );
    }
    const palette: Colour[] = [];
    for (let i = 0; i < paletteSize!; i += 4) {
        palette.push([data[32 + i]!, data[33 + i]!, data[34 + i]!, data[35 + i]!]);
    }
    const stored = data.subarray(32 + paletteSize!, 32 + paletteSize! + w! * h!);
    const order = unswizzleOrder(w!, h!);
    const indices: number[] = [];
    for (let i = 0; i < w! * h!; i++) {
        const v = stored[order[i]!];
        if (v === undefined) throw new RangeError("index out of range");
        indices.push(v);
    }
    return { w: w!, h: h!, palette, indices };
}

export function writeImg(w: number, h: number, palette: readonly Colour[], indices: readonly number[]): Buffer {
    const stored = Buffer.alloc(w * h);
    unswizzleOrder(w, h).forEach((at, i) => {
        stored[at] = indices[i]!;
    });
    const head = pack("<IIIIIIHHI", 2, 8, w, h, 0x13, 0, w, h, palette.length * 4);
    return Buffer.concat([head, ...palette.map((c) => Buffer.from(c)), stored]);
}

// --- shapes: each a function (x, y) -> inside, on the 64 x 64 sign (y down) ----------------------------------------

export type Shape = (x: number, y: number) => boolean;
type Point = readonly [number, number];

const sq = (x: number): number => x * x;
// math.radians
const radians = (degrees: number): number => degrees * (Math.PI / 180.0);

function poly(...points: Point[]): Shape {
    return (x, y) => {
        let hit = false;
        const n = points.length;
        for (let i = 0; i < n; i++) {
            const [x1, y1] = points[i]!;
            const [x2, y2] = points[(i + 1) % n]!;
            if (y1 > y !== y2 > y && x < x1 + ((y - y1) * (x2 - x1)) / (y2 - y1)) hit = !hit;
        }
        return hit;
    };
}

function rect(x0: number, y0: number, x1: number, y1: number): Shape {
    return (x, y) => x0 <= x && x <= x1 && y0 <= y && y <= y1;
}

function circle(cx: number, cy: number, r: number): Shape {
    return (x, y) => sq(x - cx) + sq(y - cy) <= r * r;
}

function ellipse(cx: number, cy: number, rx: number, ry: number, angle = 0.0): Shape {
    const c = Math.cos(radians(angle));
    const s = Math.sin(radians(angle));
    return (x, y) => {
        const dx = x - cx;
        const dy = y - cy;
        const u = dx * c + dy * s;
        const v = -dx * s + dy * c;
        return sq(u / rx) + sq(v / ry) <= 1;
    };
}

function ring(cx: number, cy: number, r: number, width: number): Shape {
    return (x, y) => {
        const d = pyHypot(x - cx, y - cy);
        return r - width / 2 <= d && d <= r + width / 2;
    };
}

// A segment with round ends.
function line(x1: number, y1: number, x2: number, y2: number, width: number): Shape {
    const dx = x2 - x1;
    const dy = y2 - y1;
    const length2 = dx * dx + dy * dy || 1e-9;
    return (x, y) => {
        const t = Math.max(0.0, Math.min(1.0, ((x - x1) * dx + (y - y1) * dy) / length2));
        return sq(x - x1 - t * dx) + sq(y - y1 - t * dy) <= (width * width) / 4;
    };
}

function polyline(points: readonly Point[], width: number): Shape {
    const segments: Shape[] = [];
    for (let i = 0; i < points.length - 1; i++) {
        const [x1, y1] = points[i]!;
        const [x2, y2] = points[i + 1]!;
        segments.push(line(x1, y1, x2, y2, width));
    }
    return (x, y) => segments.some((s) => s(x, y));
}

// Degrees, 0 = +x, 90 = down (+y).
function arc(cx: number, cy: number, r: number, width: number, a0: number, a1: number): Shape {
    const points: Point[] = [];
    for (let i = 0; i < 25; i++) {
        const a = a0 + ((a1 - a0) * i) / 24;
        points.push([cx + r * Math.cos(radians(a)), cy + r * Math.sin(radians(a))]);
    }
    return polyline(points, width);
}

// A frond: from (x0, y0) to its tip (x1, y1), bowed sideways by `bend`, widest in the middle.
function leaf(x0: number, y0: number, x1: number, y1: number, bend: number, width: number): Shape {
    const dx = x1 - x0;
    const dy = y1 - y0;
    const length = pyHypot(dx, dy);
    const nx = -dy / length;
    const ny = dx / length;
    const upper: Point[] = [];
    const lower: Point[] = [];
    for (let i = 0; i < 13; i++) {
        const t = i / 12;
        const bow = bend * 4 * t * (1 - t);
        const cx = x0 + dx * t + nx * bow;
        const cy = y0 + dy * t + ny * bow;
        const half =
            t < 0.87
                ? (width / 2) * Math.sin(Math.PI * Math.min(1.0, t * 1.15))
                : (((width / 2) * (1 - t)) / 0.13) * 0.4;
        upper.push([cx + nx * half, cy + ny * half]);
        lower.push([cx - nx * half, cy - ny * half]);
    }
    return poly(...upper, ...lower.reverse());
}

const INK = 1;
const CUT = 0;
type Layer = [number, Shape];

function skateboard(): Layer[] {
    return [
        [
            INK,
            polyline(
                [
                    [15.5, 26.5],
                    [19, 30],
                    [45, 30],
                    [48.5, 26.5],
                ],
                3.6,
            ),
        ],
        [INK, rect(22.5, 31, 27.5, 33.5)],
        [INK, rect(36.5, 31, 41.5, 33.5)],
        [INK, circle(23, 37, 3.4)],
        [INK, circle(41, 37, 3.4)],
        [CUT, circle(23, 37, 1.1)],
        [CUT, circle(41, 37, 1.1)],
    ];
}

function cone(): Layer[] {
    return [
        [INK, poly([29.5, 14], [34.5, 14], [41.5, 41], [22.5, 41])],
        [CUT, rect(0, 21.5, 64, 25)],
        [CUT, rect(0, 30, 64, 33.5)],
        [INK, rect(19, 41, 45, 45)],
    ];
}

// A barred cell window (Alcatraz).
function bars(): Layer[] {
    const shapes: Layer[] = [
        [INK, rect(21, 17, 43, 47)],
        [CUT, rect(24.5, 20.5, 39.5, 43.5)],
    ];
    for (const x of [28.5, 32, 35.5]) shapes.push([INK, rect(x - 1.1, 20, x + 1.1, 44)]);
    shapes.push([INK, rect(24, 30.8, 40, 33)]);
    return shapes;
}

// The Golden Gate: two towers, the main cable, the deck and the hangers.
function bridge(): Layer[] {
    const cable = (x: number): number => {
        if (x < 24) return 30 - ((x - 13) / 11) * 11;
        if (x > 40) return 19 + ((x - 40) / 11) * 11;
        return 19 + 11 * (1 - sq((x - 32) / 8));
    };
    const xs: number[] = [];
    for (let i = 0; i < 26; i++) xs.push(13 + i * 1.5);
    const shapes: Layer[] = [
        [
            INK,
            polyline(
                xs.map((x): Point => [x, cable(x)]),
                1.8,
            ),
        ],
    ];
    for (const x of [27, 29.5, 34, 36.5]) shapes.push([INK, rect(x, cable(x), x + 0.9, 36)]);
    for (const tx of [24, 40]) {
        shapes.push(
            [INK, rect(tx - 2, 15, tx + 2, 44)],
            [CUT, rect(tx - 0.8, 21, tx + 0.8, 23.5)],
            [CUT, rect(tx - 0.8, 27, tx + 0.8, 29.5)],
        );
    }
    shapes.push([INK, rect(12, 35, 52, 38.5)]);
    return shapes;
}

function mortarboard(): Layer[] {
    const board: Point[] = [
        [32, 16],
        [49, 24.5],
        [32, 33],
        [15, 24.5],
    ];
    const grown: Point[] = [
        [32, 14.5],
        [51.5, 24.5],
        [32, 35],
        [12.5, 24.5],
    ];
    return [
        [INK, poly([22, 28], [42, 28], [41.5, 39], [32, 42.5], [22.5, 39])],
        [CUT, poly(...grown)],
        [INK, poly(...board)],
        [
            INK,
            polyline(
                [
                    [32, 24.5],
                    [45, 28],
                    [45, 38],
                ],
                1.4,
            ),
        ],
        [INK, poly([43.5, 37.5], [46.5, 37.5], [47, 43], [43, 43])],
    ];
}

function palm(): Layer[] {
    const trunk: Point[] = [
        [35, 47],
        [34, 41],
        [32.5, 35],
        [31.5, 29],
        [31.5, 24],
    ];
    const shapes: Layer[] = [];
    for (let i = 0; i < trunk.length - 1; i++) {
        const [x1, y1] = trunk[i]!;
        const [x2, y2] = trunk[i + 1]!;
        shapes.push([INK, line(x1, y1, x2, y2, 4.0 - i * 0.4)]);
    }
    const top: Point = [31.5, 23];
    const fronds: [Point, number][] = [
        [[17, 29], 4],
        [[20, 16.5], 3],
        [[33, 13], -2],
        [[44, 17], -3],
        [[47, 28], -4],
    ];
    for (const [tip, bend] of fronds) shapes.push([INK, leaf(top[0], top[1], tip[0], tip[1], bend, 5.2)]);
    shapes.push([INK, circle(30, 25.5, 1.8)], [INK, circle(33.5, 26, 1.8)], [INK, ellipse(33, 47.5, 9, 2.2)]);
    return shapes;
}

function anchor(): Layer[] {
    const shapes: Layer[] = [
        [INK, ring(32, 16.5, 3.2, 2.0)],
        [INK, rect(30.4, 19.5, 33.6, 45)],
        [INK, line(24.5, 24, 39.5, 24, 2.8)],
        [INK, arc(32, 33.5, 11.5, 3.0, 15, 165)],
    ];
    for (const side of [1, -1]) {
        const ex = 32 + side * 11.1;
        const ey = 36.5;
        shapes.push([INK, poly([ex - side * 2.6, ey + 1.5], [ex + side * 3.5, ey - 5.5], [ex + side * 2.2, ey + 2.4])]);
    }
    return shapes;
}

function bigben(): Layer[] {
    return [
        [INK, poly([27, 20], [37, 20], [32, 9])],
        [INK, rect(26, 19.5, 38, 31.5)],
        [CUT, circle(32, 25.5, 4.0)],
        [INK, line(32, 25.5, 32, 22.8, 1.0)],
        [INK, line(32, 25.5, 34.2, 26.5, 1.0)],
        [INK, rect(27.5, 31, 36.5, 46)],
        [CUT, rect(29.8, 33.5, 30.9, 43)],
        [CUT, rect(33.1, 33.5, 34.2, 43)],
        [INK, rect(25, 45, 39, 48)],
    ];
}

function paw(): Layer[] {
    return [
        [INK, poly([25, 40], [28.5, 33], [35.5, 33], [39, 40], [36, 44.5], [28, 44.5])],
        [INK, circle(32, 38.5, 5.6)],
        [INK, ellipse(21.5, 30, 3.1, 4.1, -25)],
        [INK, ellipse(27.5, 22.5, 3.2, 4.3, -8)],
        [INK, ellipse(36.5, 22.5, 3.2, 4.3, 8)],
        [INK, ellipse(42.5, 30, 3.1, 4.1, 25)],
    ];
}

function ferris(): Layer[] {
    const cx = 32;
    const cy = 27;
    const r = 12;
    const shapes: Layer[] = [
        [INK, line(cx, cy, 24, 46.5, 2.6)],
        [INK, line(cx, cy, 40, 46.5, 2.6)],
        [INK, line(22.5, 46.5, 41.5, 46.5, 2.2)],
        [INK, ring(cx, cy, r, 2.0)],
    ];
    for (let i = 0; i < 8; i++) {
        const a = radians(i * 45 + 22.5);
        shapes.push([INK, line(cx, cy, cx + r * Math.cos(a), cy + r * Math.sin(a), 1.1)]);
        shapes.push([INK, circle(cx + (r + 0.5) * Math.cos(a), cy + (r + 0.5) * Math.sin(a) + 1.6, 2.3)]);
    }
    shapes.push([INK, circle(cx, cy, 2.4)]);
    return shapes;
}

// Chicago: a stepped tower with two masts among smaller blocks.
function skyline(): Layer[] {
    const shapes: Layer[] = [
        [INK, rect(29, 13, 35, 43)],
        [INK, rect(26.5, 21, 37.5, 43)],
        [INK, rect(24, 29, 40, 43)],
        [INK, rect(30, 7.5, 31, 13.5)],
        [INK, rect(33, 7.5, 34, 13.5)],
        [INK, rect(17.5, 33, 22.5, 43)],
        [INK, rect(41.5, 30, 46.5, 43)],
        [INK, rect(18, 43, 46, 45.5)],
    ];
    for (const x of [19.4, 43.4]) shapes.push([CUT, rect(x, 35, x + 1.1, 41)]);
    return shapes;
}

function motorbike(): Layer[] {
    return [
        [INK, ring(20.5, 38, 5.6, 2.4)],
        [INK, ring(43.5, 38, 5.6, 2.4)],
        [INK, circle(20.5, 38, 1.4)],
        [INK, circle(43.5, 38, 1.4)],
        [INK, poly([23, 33], [28, 28.5], [38, 28.5], [41, 32], [35, 36], [26, 36])],
        [INK, poly([25, 29], [31, 26.5], [33, 28.5], [26, 31])],
        [INK, line(43.5, 38, 38.5, 25.5, 2.2)],
        [INK, line(36, 24.5, 41, 23.5, 1.6)],
        [INK, line(20.5, 38, 27, 33, 2.0)],
    ];
}

// The Statue of Liberty's torch.
function torch(): Layer[] {
    const flame = poly([32, 9.5], [37, 16], [36.5, 21], [32, 23.5], [27.5, 21], [27, 16], [29.5, 13.5], [30.5, 16.5]);
    return [
        [INK, flame],
        [CUT, poly([32, 15.5], [34, 19], [32, 21.5], [30, 19])],
        [INK, poly([25, 24], [39, 24], [37, 28], [27, 28])],
        [INK, rect(28.5, 28, 35.5, 31)],
        [INK, poly([29.5, 31], [34.5, 31], [33.5, 46], [30.5, 46])],
        [INK, rect(27.5, 45, 36.5, 48)],
    ];
}

function wave(): Layer[] {
    return [
        [
            INK,
            poly(
                [18, 40],
                [21, 33],
                [26, 26.5],
                [31.5, 22.5],
                [37.5, 21.5],
                [42.5, 23.5],
                [45.5, 28],
                [44, 30.5],
                [41, 28],
                [37.5, 28],
                [35.5, 30.5],
                [36, 34],
                [39, 36],
                [43, 37],
                [46.5, 40],
            ),
        ],
        [INK, rect(18.5, 42, 45.5, 44.5)],
    ];
}

function sun(): Layer[] {
    const shapes: Layer[] = [[INK, circle(32, 31, 7.5)]];
    for (let i = 0; i < 12; i++) {
        const a = radians(i * 30);
        shapes.push([
            INK,
            line(
                32 + 10.5 * Math.cos(a),
                31 + 10.5 * Math.sin(a),
                32 + 15.5 * Math.cos(a),
                31 + 15.5 * Math.sin(a),
                2.2,
            ),
        ]);
    }
    return shapes;
}

function house(): Layer[] {
    return [
        [INK, poly([32, 14], [47, 28], [43.5, 28], [43.5, 45], [20.5, 45], [20.5, 28], [17, 28])],
        [CUT, rect(28.5, 34, 35.5, 45.5)],
        [CUT, rect(23.5, 30.5, 27.5, 34.5)],
        [CUT, rect(36.5, 30.5, 40.5, 34.5)],
        [INK, rect(38.5, 16, 41.5, 23)],
    ];
}

// A half-pipe.
function ramp(): Layer[] {
    return [
        [
            INK,
            poly(
                [18, 26],
                [21.5, 26],
                [21.5, 28],
                [23.5, 33.5],
                [27, 37],
                [32, 38],
                [37, 37],
                [40.5, 33.5],
                [42.5, 28],
                [42.5, 26],
                [46, 26],
                [46, 43],
                [18, 43],
            ),
        ],
        [INK, rect(17.5, 24.5, 22.5, 26.5)],
        [INK, rect(41.5, 24.5, 46.5, 26.5)],
    ];
}

// An onion dome (Moscow).
function dome(): Layer[] {
    return [
        [
            INK,
            poly(
                [32, 10],
                [33.5, 15],
                [39, 19.5],
                [41, 25],
                [38.5, 29.5],
                [25.5, 29.5],
                [23, 25],
                [25, 19.5],
                [30.5, 15],
            ),
        ],
        [INK, rect(25, 29.5, 39, 31.5)],
        [INK, rect(26, 31.5, 38, 47)],
        [CUT, poly([30, 47.5], [30, 38], [32, 35.5], [34, 38], [34, 47.5])],
        [CUT, rect(28, 34, 29.2, 39)],
        [CUT, rect(34.8, 34, 36, 39)],
    ];
}

function flame(): Layer[] {
    return [
        [
            INK,
            poly(
                [32, 10],
                [37, 18],
                [41.5, 23],
                [43, 31],
                [41, 39],
                [35.5, 45],
                [28.5, 45],
                [23, 39],
                [21, 31],
                [23.5, 24],
                [27, 29],
                [28.5, 21],
            ),
        ],
        [CUT, poly([32, 27], [36, 33], [36.5, 38], [34, 42], [30, 42], [27.5, 38], [28.5, 32], [30.5, 35])],
    ];
}

// A cog (THPS3's Foundry).
function gear(): Layer[] {
    const shapes: Layer[] = [];
    for (let i = 0; i < 8; i++) {
        const a = radians(i * 45);
        shapes.push([
            INK,
            line(32 + 8 * Math.cos(a), 31 + 8 * Math.sin(a), 32 + 14 * Math.cos(a), 31 + 14 * Math.sin(a), 4.2),
        ]);
    }
    shapes.push([INK, circle(32, 31, 11)], [CUT, circle(32, 31, 4)]);
    return shapes;
}

// A fir tree (Canada).
function pine(): Layer[] {
    return [
        [
            INK,
            poly(
                [32, 11],
                [39, 20],
                [36, 20],
                [42, 28],
                [38.5, 28],
                [45, 37],
                [19, 37],
                [25.5, 28],
                [22, 28],
                [28, 20],
                [25, 20],
            ),
        ],
        [INK, rect(29.5, 37, 34.5, 45)],
    ];
}

// Christ the Redeemer (Rio): a figure with its arms out, on its pedestal.
function redeemer(): Layer[] {
    return [
        [INK, circle(32, 15, 2.6)],
        [INK, rect(17, 18.5, 47, 21.5)],
        [INK, poly([28.5, 18.5], [35.5, 18.5], [35, 40], [29, 40])],
        [INK, rect(27.5, 40, 36.5, 46)],
    ];
}

// A shrine gate (Tokyo).
function torii(): Layer[] {
    return [
        [INK, poly([15.5, 16], [48.5, 16], [46, 21], [18, 21])],
        [INK, rect(19.5, 25, 44.5, 27.5)],
        [INK, rect(22, 20, 25.5, 47)],
        [INK, rect(38.5, 20, 42, 47)],
        [INK, rect(30.5, 20, 33.5, 25.5)],
    ];
}

// A cruise liner.
function ship(): Layer[] {
    const shapes: Layer[] = [
        [INK, poly([15, 34], [49, 34], [44, 43], [20, 43])],
        [INK, rect(20, 28, 44, 34)],
        [INK, rect(24, 23, 40, 28)],
        [INK, poly([33, 16], [38, 16], [38.5, 23], [32.5, 23])],
    ];
    for (const x of [23.5, 28, 32.5, 37, 41.5]) shapes.push([CUT, circle(x, 31, 1.1)]);
    return shapes;
}

// A shipping crate (the Warehouse).
function crate(): Layer[] {
    return [
        [INK, rect(19, 19, 45, 45)],
        [CUT, rect(22, 22, 42, 42)],
        [INK, line(22, 22, 42, 42, 3.4)],
        [INK, line(42, 22, 22, 42, 3.4)],
        [INK, rect(19, 30.5, 45, 33.5)],
    ];
}

// Roswell.
function ufo(): Layer[] {
    return [
        [INK, ellipse(32, 30, 16, 5)],
        [INK, ellipse(32, 26, 7.5, 6)],
        [CUT, ellipse(32, 30, 13.5, 1.2)],
        [INK, line(25, 34, 21, 44, 2.0)],
        [INK, line(39, 34, 43, 44, 2.0)],
        [INK, line(32, 35, 32, 45, 2.0)],
    ];
}

export const PICTOGRAMS: ReadonlyMap<string, () => Layer[]> = new Map(
    [
        skateboard,
        cone,
        bars,
        bridge,
        mortarboard,
        palm,
        anchor,
        bigben,
        paw,
        ferris,
        skyline,
        motorbike,
        torch,
        wave,
        sun,
        house,
        ramp,
        dome,
        flame,
        gear,
        pine,
        redeemer,
        torii,
        ship,
        crate,
        ufo,
    ].map((f) => [f.name, f]),
);

const SAMPLES = 4;

// Each pixel's ink, 0..1, from SAMPLES x SAMPLES points (the last shape covering a point decides it).
export function coverage(shapes: readonly Layer[]): number[] {
    const out = new Array<number>(SIZE * SIZE).fill(0.0);
    for (let y = 0; y < SIZE; y++) {
        for (let x = 0; x < SIZE; x++) {
            if (Math.abs(x + 0.5 - 32) + Math.abs(y + 0.5 - 32) > INSIDE + 1) continue;
            let hits = 0;
            for (let j = 0; j < SAMPLES; j++) {
                for (let i = 0; i < SAMPLES; i++) {
                    const px = x + (i + 0.5) / SAMPLES;
                    const py = y + (j + 0.5) / SAMPLES;
                    let ink = 0;
                    for (const [mode, shape] of shapes) if (shape(px, py)) ink = mode;
                    hits += ink;
                }
            }
            out[y * SIZE + x] = hits / (SAMPLES * SAMPLES);
        }
    }
    return out;
}

// The frame's image with its inside cleared and the pictogram drawn in black over the fill (the nearest of the
// frame palette's opaque greys: the game tints the sign with the menu colour).
export function makeSign(frameData: Buffer, pictogram: string): Buffer {
    const { w, h, palette, indices } = readImg(frameData);
    if (w !== SIZE || h !== SIZE) throw new Error(`the sign frame is ${w}x${h}`);
    const greys: [number, number][] = [];
    palette.forEach((c, i) => {
        if (c[3] === 0xff && c[0] === c[1] && c[1] === c[2]) greys.push([i, c[0]]);
    });
    const draw = PICTOGRAMS.get(pictogram);
    if (draw === undefined) throw new Error(`KeyError: '${pictogram}'`);
    const ink = coverage(draw());
    const out = [...indices];
    for (let y = 0; y < SIZE; y++) {
        for (let x = 0; x < SIZE; x++) {
            if (Math.abs(x + 0.5 - 32) + Math.abs(y + 0.5 - 32) > INSIDE) continue;
            const value = FILL * (1 - ink[y * SIZE + x]!);
            out[y * SIZE + x] = pyMinBy(greys, (g) => Math.abs(g[1] - value))[0];
        }
    }
    return writeImg(w, h, palette, out);
}

// --- a preview -----------------------------------------------------------------------------------------------------

export function writePng(file: string, w: number, h: number, rgba: Uint8Array): void {
    const rows: Buffer[] = [];
    for (let y = 0; y < h; y++) rows.push(Buffer.from([0]), Buffer.from(rgba.subarray(y * w * 4, (y + 1) * w * 4)));
    const raw = Buffer.concat(rows);
    const chunk = (kind: string, data: Buffer): Buffer => {
        const body = Buffer.concat([Buffer.from(kind, "latin1"), data]);
        return Buffer.concat([pack(">I", data.length), body, pack(">I", zlib.crc32(body) >>> 0)]);
    };
    fs.writeFileSync(
        file,
        Buffer.concat([
            Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]),
            chunk("IHDR", pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)),
            chunk("IDAT", zlib.deflateSync(raw)),
            chunk("IEND", Buffer.alloc(0)),
        ]),
    );
}

async function main(): Promise<number> {
    const { prxFile } = await import("./make_menu_level.ts");
    const { values, positionals } = parseArgs({
        options: { game: { type: "string", default: "C:\\Games\\THUG2\\Game" } },
        allowPositionals: true,
    });
    const [out, ...given] = positionals;
    if (out === undefined) {
        process.stderr.write("usage: level_select_icons.ts out [names ...] [--game GAME]\n");
        return 2;
    }
    let game = values.game;
    // (a stock install keeps the game in a Game subfolder: C:\Games\THUG2 -> C:\Games\THUG2\Game)
    if (!isDir(path.join(game, "Data")) && isDir(path.join(game, "Game", "Data"))) game = pyJoin(game, "Game");
    const frame = prxFile(pyJoin(game, "Data", "pre", "Levelselect.prx"), FRAME_SOURCE);
    const names = given.length ? given : [...PICTOGRAMS.keys()];
    const scale = 3;
    const cols = 7;
    const grey = 90;
    const cell = SIZE * scale + 6;
    const rows = Math.floor((names.length + cols - 1) / cols);
    const width = cols * cell;
    const height = rows * cell;
    const image = new Uint8Array(width * height * 4);
    for (let i = 0; i < width * height; i++) image.set([grey, grey, grey, 255], i * 4);
    names.forEach((name, n) => {
        const { palette, indices } = readImg(makeSign(frame, name));
        const ox = (n % cols) * cell;
        const oy = Math.floor(n / cols) * cell;
        for (let y = 0; y < SIZE * scale; y++) {
            for (let x = 0; x < SIZE * scale; x++) {
                const [b, g, r, a] = palette[indices[Math.floor(y / scale) * SIZE + Math.floor(x / scale)]!]!;
                const o = ((oy + y) * width + ox + x) * 4;
                image.set([...[r, g, b].map((c) => Math.trunc((c * a) / 255 + grey * (1 - a / 255))), 255], o);
            }
        }
    });
    writePng(out, width, height, image);
    print(`wrote ${out}: ${names.join(", ")}`);
    return 0;
}

function isDir(p: string): boolean {
    try {
        return fs.statSync(p).isDirectory();
    } catch {
        return false;
    }
}

// (not awaited: main imports make_menu_level.ts, which imports this module through level_select.ts - a top-level
// await here would wait for itself)
if (isMain(import.meta.url)) void runMain(() => main());
