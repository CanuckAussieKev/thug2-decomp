// NOT ORIGINAL - the level select's signs for the levels we add (tools/menu_level/level_select.ts): THUG2's sign
// frame (the road-sign diamond of its sign_training) with a pictogram of our own drawn in it, as the game's
// images\LevelSelectSprites\<name>.img.xbx.
//
// The game's level select sprites (Levelselect.prx) are 8-bit paletted images:
//   u32 version (2), u32 bits per texel (8), u32 width, u32 height, u32 format (0x13 = paletted), u32 0,
//   u16 width, u16 height, u32 palette size in bytes (64: 16 BGRA colours), the palette, then width * height
//   indices stored swizzled (tools/level_port/tex.ts unswizzleOrder), the rows bottom-up - read by 0x004E0790.
// Nothing of the game is stored here: the frame is read from the user's Levelselect.prx each time.
//
// A preview:
//   node tools/menu_level/level_select_icons.ts <out.png> [--game C:\Games\THUG2] [--scale N] [pictogram...]
// draws the signs (3x, on grey) into a PNG to look at; with --sheet, a contact sheet instead: a row of THUG2's own
// signs, then ours section by section (THUG2's two added levels, THUG1, THPS4, THPS3, THAW), tinted yellow.

import * as fs from "node:fs";
import * as zlib from "node:zlib";
import { unswizzleOrder } from "../level_port/tex.ts";
import { parseCli } from "../lib/cli.ts";
import { resolveInstall } from "../lib/install.ts";
import { isMain, print, pyHypot, pyJoin, pyMinBy, runMain } from "../lib/py.ts";
import { pack, unpackNumbers } from "../lib/struct.ts";

const USAGE = "usage: level_select_icons.ts out [names ...] [--game GAME] [--scale N] [--sheet]";

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
    // (the game draws the rows bottom-up compared with unswizzleOrder's: THUG2's own signs come out upside down in
    // that order and upright in the game - so the rows of `indices` are flipped, top row first)
    const indices: number[] = [];
    for (let y = 0; y < h!; y++) {
        for (let x = 0; x < w!; x++) {
            const v = stored[order[(h! - 1 - y) * w! + x]!];
            if (v === undefined) throw new RangeError("index out of range");
            indices.push(v);
        }
    }
    return { w: w!, h: h!, palette, indices };
}

export function writeImg(w: number, h: number, palette: readonly Colour[], indices: readonly number[]): Buffer {
    const stored = Buffer.alloc(w * h);
    // (rows flipped as readImg reads them: the game draws the stored order bottom-up)
    unswizzleOrder(w, h).forEach((at, i) => {
        stored[at] = indices[(h - 1 - Math.floor(i / w)) * w + (i % w)]!;
    });
    const head = pack("<IIIIIIHHI", 2, 8, w, h, 0x13, 0, w, h, palette.length * 4);
    return Buffer.concat([head, ...palette.map((c) => Buffer.from(c)), stored]);
}

// --- shapes: each a function (x, y) -> inside, on the 64 x 64 sign (y down) ----------------------------------------

export type Shape = (x: number, y: number) => boolean;
export type Point = readonly [number, number];

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
export type Layer = [number, Shape];

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
        [INK, rect(19.5, 41, 44.5, 44.5)],
    ];
}

// The Golden Gate: two towers, the main cable, the deck and the hangers.
function bridge(): Layer[] {
    return scaleAll(bridgeDrawn(), 32, 30, 0.8);
}

function bridgeDrawn(): Layer[] {
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
    return scaleAll(mortarboardDrawn(), 32, 29, 0.95);
}

function mortarboardDrawn(): Layer[] {
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
    return scaleAll(palmDrawn(), 32, 31, 0.9);
}

function palmDrawn(): Layer[] {
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
    return scaleAll(skylineDrawn(), 32, 30, 0.87);
}

function skylineDrawn(): Layer[] {
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
    return scaleAll(motorbikeDrawn(), 32, 33, 0.82);
}

function motorbikeDrawn(): Layer[] {
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
        [INK, rect(19.5, 42, 44.5, 44.3)],
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

// Christ the Redeemer (Rio): the robed figure with its arms out, on its pedestal on Corcovado's peak.
function redeemer(): Layer[] {
    return [
        [INK, circle(32, 14.5, 2.5)],
        [INK, poly([20, 18.5], [44, 18.5], [44, 21], [35.5, 22.5], [28.5, 22.5], [20, 21])],
        [INK, poly([29, 17.5], [35, 17.5], [36.5, 36], [27.5, 36])],
        [CUT, line(32, 24, 32, 36, 0.7)],
        [INK, rect(28.5, 35.5, 35.5, 39)],
        [INK, poly([28, 39], [36, 39], [39, 41], [44, 45], [20, 45], [25, 41])],
    ];
}

// A shrine gate (Kyoto).
function torii(): Layer[] {
    return [
        [INK, poly([20, 19.5], [32, 21], [44, 19.5], [43, 24], [21, 24])],
        [INK, rect(21.5, 27.5, 42.5, 30)],
        [INK, poly([23.8, 23.5], [27, 23.5], [27.5, 46], [23.3, 46])],
        [INK, poly([37, 23.5], [40.2, 23.5], [40.7, 46], [36.5, 46])],
        [INK, rect(30.5, 23.5, 33.5, 28)],
    ];
}

// --- the signs added for every ported level (each its own; a landmark or symbol of the level) --------------------

// `shape` turned by `degrees` (clockwise on the y-down sign) about (cx, cy).
function turned(shape: Shape, cx: number, cy: number, degrees: number): Shape {
    const c = Math.cos(radians(degrees));
    const s = Math.sin(radians(degrees));
    return (x, y) => {
        const dx = x - cx;
        const dy = y - cy;
        return shape(cx + dx * c + dy * s, cy - dx * s + dy * c);
    };
}

function turnAll(layers: readonly Layer[], cx: number, cy: number, degrees: number): Layer[] {
    return layers.map(([mode, shape]): Layer => [mode, turned(shape, cx, cy, degrees)]);
}

// `layers` scaled by `k` about (cx, cy) and moved by (dx, dy).
function scaleAll(layers: readonly Layer[], cx: number, cy: number, k: number, dx = 0, dy = 0): Layer[] {
    return layers.map(
        ([mode, shape]): Layer => [mode, (x, y) => shape(cx + (x - dx - cx) / k, cy + (y - dy - cy) / k)],
    );
}

// A rectangle with rounded corners.
function rrect(x0: number, y0: number, x1: number, y1: number, r: number): Shape {
    const inner = rect(x0 + r, y0 + r, x1 - r, y1 - r);
    return (x, y) => {
        if (x < x0 || x > x1 || y < y0 || y > y1) return false;
        const cx = Math.max(x0 + r, Math.min(x1 - r, x));
        const cy = Math.max(y0 + r, Math.min(y1 - r, y));
        return inner(x, y) || sq(x - cx) + sq(y - cy) <= r * r;
    };
}

// A digit 0 / 1 in a w x h box at (x, y), strokes `t` wide (the freeway shield's number).
function digit(n: number, x: number, y: number, w: number, h: number, t: number): Shape[] {
    if (n === 1)
        return [rect(x + w / 2 - t / 2, y, x + w / 2 + t / 2, y + h), line(x + w / 2, y + 0.5, x + 0.6, y + 2.2, t)];
    const outer = rrect(x, y, x + w, y + h, w / 2.5);
    const inner = rrect(x + t, y + t, x + w - t, y + h - t, (w - 2 * t) / 2.5);
    return [(px, py) => outer(px, py) && !inner(px, py)];
}

// THPS3's Foundry: a ladle tipped to pour molten metal.
function ladle(): Layer[] {
    const cx = 32;
    const cy = 26.5;
    const r = 9.5;
    const tilt = radians(25);
    // the bowl: the circle below its rim (a line through the middle, tipped down to the right)
    const bowl: Shape = (x, y) =>
        sq(x - cx) + sq(y - cy) <= r * r && -(x - cx) * Math.sin(tilt) + (y - cy) * Math.cos(tilt) >= -0.5;
    return [
        [INK, bowl],
        [
            CUT,
            (x, y) =>
                sq(x - cx) + sq(y - cy) <= sq(r - 2.2) &&
                -(x - cx) * Math.sin(tilt) + (y - cy) * Math.cos(tilt) >= 0.6 &&
                -(x - cx) * Math.sin(tilt) + (y - cy) * Math.cos(tilt) <= 1.6,
        ],
        [INK, line(cx - r * Math.cos(tilt) + 1, cy - r * Math.sin(tilt) + 0.5, 20.5, 21.2, 2.4)],
        [
            INK,
            polyline(
                [
                    [cx + r * Math.cos(tilt) - 0.3, cy + r * Math.sin(tilt) + 0.3],
                    [41.6, 35],
                    [41.3, 40.5],
                ],
                2.6,
            ),
        ],
        [INK, ellipse(38.5, 42.5, 5.5, 2.2)],
        [INK, circle(33.6, 39.8, 1.1)],
        [INK, circle(45, 38.8, 0.9)],
    ];
}

// THPS3's Airport: the control tower.
function tower(): Layer[] {
    const shapes: Layer[] = [
        [INK, line(32, 10.5, 32, 16, 1.3)],
        [INK, poly([25.5, 16], [38.5, 16], [40, 19], [24, 19])],
        [INK, poly([21.5, 19], [42.5, 19], [39.5, 28], [24.5, 28])],
        [CUT, poly([23.4, 20.6], [40.6, 20.6], [39.6, 24.2], [24.4, 24.2])],
        [INK, rect(28.5, 27, 35.5, 44)],
        [INK, poly([26, 44], [38, 44], [40, 47], [24, 47])],
        [CUT, rect(31.3, 31, 32.7, 40)],
    ];
    for (const x of [27.8, 32, 36.2]) shapes.push([INK, rect(x - 0.6, 20, x + 0.6, 25)]);
    return shapes;
}

// THPS3's Skater Island (a competition park): the trophy.
function trophy(): Layer[] {
    return [
        [INK, poly([23, 17], [41, 17], [40.5, 23], [37.5, 29], [32, 32.5], [26.5, 29], [23.5, 23])],
        [INK, arc(23.5, 23, 4.4, 2.2, 90, 270)],
        [INK, arc(40.5, 23, 4.4, 2.2, -90, 90)],
        [CUT, poly([26.2, 19.5], [28.2, 19.5], [29.2, 27], [27.8, 27])],
        [INK, rect(30.5, 31, 33.5, 38)],
        [INK, poly([26.5, 38], [37.5, 38], [39.5, 42.5], [24.5, 42.5])],
        [INK, rect(23.5, 42.5, 40.5, 45.5)],
    ];
}

// THPS3's Los Angeles (its freeway): a US route shield, 101.
function freeway(): Layer[] {
    const shapes: Layer[] = [
        [
            INK,
            poly(
                [22, 19],
                [27, 18],
                [32, 19.5],
                [37, 18],
                [42, 19],
                [43.5, 24],
                [43, 32],
                [40, 39],
                [35, 44],
                [32, 46.5],
                [29, 44],
                [24, 39],
                [21, 32],
                [20.5, 24],
            ),
        ],
    ];
    const h = 11;
    const y = 24.5;
    for (const [n, x] of [
        [1, 23],
        [0, 28.8],
        [1, 35.4],
    ] as const) {
        for (const s of digit(n, x, y, 5.6, h, 2.0)) shapes.push([CUT, s]);
    }
    return shapes;
}

// THPS3's Tokyo: Tokyo Tower.
function tokyotower(): Layer[] {
    return [
        [INK, line(32, 8.6, 32, 17, 1.2)],
        [INK, poly([30.6, 16], [33.4, 16], [35.5, 30], [28.5, 30])],
        [INK, rect(28.2, 20, 35.8, 22)],
        [INK, rect(25.5, 29.5, 38.5, 33)],
        [INK, poly([27.5, 33], [36.5, 33], [42, 46], [22, 46])],
        [CUT, ellipse(32, 47, 5.5, 7.5)],
        [CUT, rect(20, 25.5, 44, 26.5)],
        [CUT, rect(20, 37, 44, 38)],
    ];
}

// THPS3's Burnside (under Portland's Burnside Bridge): the bridge, its two leaves raised between its towers.
function burnside(): Layer[] {
    const shapes: Layer[] = [
        [INK, rect(17.5, 33, 25, 35.5)],
        [INK, rect(39, 33, 46.5, 35.5)],
        [INK, line(27.5, 34.2, 30.6, 25.5, 2.6)],
        [INK, line(36.5, 34.2, 33.4, 25.5, 2.6)],
    ];
    for (const x of [25.25, 38.75]) {
        shapes.push(
            [INK, rect(x - 2.4, 24, x + 2.4, 40.5)],
            [INK, poly([x - 3, 24.5], [x + 3, 24.5], [x, 19.5])],
            [CUT, rect(x - 0.8, 27, x + 0.8, 30)],
            [INK, rect(x - 3, 40, x + 3, 44)],
        );
    }
    shapes.push([
        INK,
        polyline(
            [
                [21.5, 45],
                [25, 44],
                [28.5, 45],
                [32, 44],
                [35.5, 45],
                [39, 44],
                [42.5, 45],
            ],
            1.4,
        ),
    ]);
    return shapes;
}

// THPS3's Tutorials: the coach's whistle.
function whistle(): Layer[] {
    return [
        [INK, circle(28, 33.5, 8.5)],
        [INK, rrect(28, 24.5, 46, 31, 0.8)],
        [CUT, poly([30.5, 24], [35, 24], [35, 28], [32, 28])],
        [CUT, circle(28, 34, 3.2)],
        [INK, ring(19, 25.5, 3, 1.6)],
    ];
}

// THPS3's skate shop: a shop front with a striped awning.
function shop(): Layer[] {
    const shapes: Layer[] = [
        [INK, rect(24.5, 16.5, 39.5, 21)],
        [CUT, rect(26.5, 18.2, 37.5, 19.3)],
        [INK, poly([20, 22], [44, 22], [46.5, 28.5], [17.5, 28.5])],
    ];
    for (const x of [24, 32, 40])
        shapes.push([CUT, poly([x - 1.4, 22.8], [x + 1.4, 22.8], [x + 2.4, 28.6], [x - 2.4, 28.6])]);
    for (let x = 19.5; x <= 45; x += 4) shapes.push([INK, circle(x, 28.6, 2)]);
    shapes.push(
        [INK, rect(20, 29, 44, 44)],
        [CUT, rect(22.5, 32.5, 32, 40.5)],
        [CUT, rect(35, 32.5, 41.5, 44.5)],
        [INK, line(23.8, 39, 30.8, 39, 1.2)],
        [INK, circle(25.3, 40, 0.8)],
        [INK, circle(29.3, 40, 0.8)],
    );
    return shapes;
}

// THPS4's Alcatraz: its lighthouse.
function lighthouse(): Layer[] {
    return [
        [INK, line(32, 12.5, 32, 15.5, 1.1)],
        [INK, ellipse(32, 19, 4.5, 3.6)],
        [INK, rect(28.5, 19, 35.5, 24.5)],
        [CUT, rect(30, 20, 34, 23.5)],
        [INK, poly([29, 21.2], [21.5, 18.5], [21.5, 23.8])],
        [INK, poly([35, 21.2], [42.5, 18.5], [42.5, 23.8])],
        [INK, rect(26, 24.5, 38, 27)],
        [INK, poly([28, 27], [36, 27], [38.5, 45], [25.5, 45])],
        [CUT, rect(20, 31, 44, 33.3)],
        [CUT, rect(20, 37.5, 44, 39.8)],
        [INK, poly([22, 44.5], [42, 44.5], [40.5, 46.8], [23.5, 46.8])],
    ];
}

// THPS4's Carnival: the big top.
function tent(): Layer[] {
    const shapes: Layer[] = [
        [INK, line(32, 9.6, 32, 15, 1.2)],
        [INK, poly([32, 10], [35.5, 11.2], [32, 12.6])],
        [INK, poly([32, 14], [38, 19], [45, 27.5], [45, 30], [19, 30], [19, 27.5], [26, 19])],
        [INK, rect(21, 29, 43, 44.5)],
    ];
    for (const x of [23.5, 28, 36, 40.5]) shapes.push([CUT, line(32, 15.5, x, 29.6, 1.1)]);
    shapes.push([CUT, rect(19, 30, 45, 31.2)]);
    for (const x of [25.5, 38.5]) shapes.push([CUT, rect(x - 0.55, 31, x + 0.55, 45)]);
    shapes.push([CUT, poly([32, 33.5], [36.5, 44.6], [27.5, 44.6])]);
    return shapes;
}

// THPS4's Kona: the snake run - a snake.
function snake(): Layer[] {
    return scaleAll(snakeDrawn(), 32, 30, 0.92, -1.5, 1.5);
}

function snakeDrawn(): Layer[] {
    return [
        [
            INK,
            polyline(
                [
                    [35, 17.5],
                    [29, 18.2],
                    [24.5, 21],
                    [23.5, 25.5],
                    [27, 29],
                    [34, 30.5],
                    [39.5, 33.5],
                    [40.5, 38.5],
                    [36.5, 42.5],
                    [29, 43.8],
                    [22, 42.5],
                ],
                4.4,
            ),
        ],
        [INK, ellipse(37.5, 17, 4.6, 3.4, -8)],
        [CUT, circle(38.5, 15.8, 0.9)],
        [
            INK,
            polyline(
                [
                    [41.5, 17.2],
                    [44.5, 17.6],
                    [46, 16.2],
                ],
                0.9,
            ),
        ],
        [INK, line(44.5, 17.6, 46, 19.2, 0.9)],
        [INK, line(22, 42.5, 19.5, 41, 2.2)],
    ];
}

// THPS4's Zoo: an elephant.
function elephant(): Layer[] {
    return [
        [INK, ellipse(35, 29.5, 11, 8.5)],
        [INK, circle(23, 25.5, 6.5)],
        [
            INK,
            polyline(
                [
                    [19.5, 27],
                    [17.3, 33],
                    [17.6, 38.5],
                    [20.2, 41],
                ],
                3.4,
            ),
        ],
        [INK, line(45.5, 27, 47.5, 34, 1.2)],
        [CUT, ellipse(27.8, 27.8, 5.3, 7.2)],
        [INK, ellipse(27.8, 27.8, 4.2, 6.1)],
        [CUT, circle(21, 24, 1)],
        [INK, rect(25, 33, 29.5, 43)],
        [INK, rect(31, 33, 35, 43)],
        [INK, rect(37.5, 33, 41.5, 43)],
        [INK, rect(42.5, 31, 45.5, 42.5)],
    ];
}

// THPS4's skate shop: a T-shirt.
function tshirt(): Layer[] {
    return [
        [
            INK,
            poly(
                [26, 17.5],
                [29, 17],
                [32, 20],
                [35, 17],
                [38, 17.5],
                [46, 23],
                [43, 29],
                [39.5, 27],
                [39.5, 45],
                [24.5, 45],
                [24.5, 27],
                [21, 29],
                [18, 23],
            ),
        ],
        [CUT, ring(32, 32.5, 3.4, 1.4)],
        [CUT, line(29.5, 32.5, 34.5, 32.5, 1)],
    ];
}

// THUG1's New Jersey: a fire hydrant.
function hydrant(): Layer[] {
    return [
        [INK, rect(30.5, 15.5, 33.5, 19)],
        [INK, ellipse(32, 23, 6.5, 4.5)],
        [INK, rrect(24.5, 22.5, 39.5, 25.5, 0.8)],
        [INK, rect(26.5, 25, 37.5, 43)],
        [INK, rrect(20.5, 29, 27, 35, 0.8)],
        [INK, rrect(37, 29, 43.5, 35, 0.8)],
        [CUT, ring(32, 32, 3.1, 1)],
        [CUT, rect(28, 38, 36, 39)],
        [INK, rrect(23.5, 42.5, 40.5, 46, 0.8)],
    ];
}

// THUG1's Vancouver: a totem pole crowned by its thunderbird.
function totem(): Layer[] {
    const shapes: Layer[] = [
        [INK, poly([28, 15], [32, 11.5], [36, 15], [36.5, 20], [27.5, 20])],
        [INK, poly([26.5, 19.5], [17.5, 21], [18.5, 24.5], [22, 23.5], [26.5, 26])],
        [INK, poly([37.5, 19.5], [46.5, 21], [45.5, 24.5], [42, 23.5], [37.5, 26])],
        [INK, rect(26.5, 19.5, 37.5, 46)],
        [CUT, ellipse(29.8, 16.5, 1, 1)],
        [CUT, ellipse(34.2, 16.5, 1, 1)],
        [CUT, poly([29.5, 20.5], [34.5, 20.5], [32, 25.5])],
        [INK, poly([30.6, 20.5], [33.4, 20.5], [32, 24])],
        [CUT, rect(25, 26.3, 39, 27.1)],
        [CUT, rect(25, 36.4, 39, 37.2)],
    ];
    for (const y of [29.8, 39.8]) {
        shapes.push(
            [CUT, ellipse(29.5, y, 2, 1.4)],
            [CUT, ellipse(34.5, y, 2, 1.4)],
            [INK, circle(29.5, y, 0.7)],
            [INK, circle(34.5, y, 0.7)],
            [CUT, rrect(28.5, y + 2.8, 35.5, y + 4.8, 0.6)],
            [INK, rect(31.5, y + 2.8, 32.5, y + 4.8)],
        );
    }
    return shapes;
}

// THUG1's Downhill Jam (Phoenix): a saguaro.
function cactus(): Layer[] {
    return [
        [INK, line(32, 14.5, 32, 45, 6)],
        [
            INK,
            polyline(
                [
                    [29.5, 33],
                    [23.5, 33],
                    [23.5, 23.5],
                ],
                4.6,
            ),
        ],
        [
            INK,
            polyline(
                [
                    [34.5, 28.5],
                    [40.5, 28.5],
                    [40.5, 21],
                ],
                4.6,
            ),
        ],
        [INK, rect(23, 44.5, 41, 46.8)],
    ];
}

// THUG1's Philadelphia (THPS2's Philly): the LOVE sculpture.
function love(): Layer[] {
    const t = 3.2;
    const shapes: Layer[] = [
        // L
        [INK, rect(20.5, 20.5, 20.5 + t, 31)],
        [INK, rect(20.5, 31 - t, 30.5, 31)],
        // O, tilted
        ...turnAll(
            [
                [INK, ellipse(38.3, 25.7, 5.2, 5.7)],
                [CUT, ellipse(38.3, 25.7, 2.1, 2.5)],
            ],
            38.3,
            25.7,
            30,
        ),
        // V
        [INK, poly([20.5, 33.5], [23.9, 33.5], [25.5, 40], [27.1, 33.5], [30.5, 33.5], [27.2, 43.5], [23.8, 43.5])],
        // E
        [INK, rect(33.5, 33.5, 33.5 + t, 43.5)],
        [INK, rect(33.5, 33.5, 43.5, 33.5 + 2.6)],
        [INK, rect(33.5, 37.3, 42, 39.7)],
        [INK, rect(33.5, 43.5 - 2.6, 43.5, 43.5)],
    ];
    return shapes;
}

// THUG1's Venice (Muscle Beach): a barbell.
function dumbbell(): Layer[] {
    const shapes: Layer[] = [[INK, rect(17.5, 30.8, 46.5, 33.2)]];
    for (const side of [-1, 1]) {
        const x = 32 + side * 10;
        shapes.push(
            [INK, rrect(x - 2, 21.5, x + 2, 42.5, 1)],
            [INK, rrect(x + side * 2.2 - 1.6, 24.5, x + side * 2.2 + 1.6, 39.5, 0.8)],
        );
    }
    return shapes;
}

// THUG1's Hangar (Mullet Falls): a helicopter.
function helicopter(): Layer[] {
    return [
        [INK, line(18.5, 21.5, 45, 21.5, 1.6)],
        [INK, rect(29, 21.5, 31.5, 25)],
        [INK, ellipse(29.5, 31, 9.5, 6.5)],
        [INK, poly([36, 28], [47, 27], [47, 29.5], [37, 33])],
        [INK, line(46.5, 24.5, 46.5, 31, 1.6)],
        [CUT, poly([21, 30], [23, 26.5], [28, 26], [28, 30.5])],
        [INK, line(24, 37, 24, 41, 1.3)],
        [INK, line(34, 37, 34, 41, 1.3)],
        [
            INK,
            polyline(
                [
                    [19.5, 40],
                    [21, 41.3],
                    [38, 41.3],
                ],
                1.6,
            ),
        ],
    ];
}

// THUG1's Old School (THPS1's School, Miami): a school bus.
function bus(): Layer[] {
    const shapes: Layer[] = [
        [INK, rrect(17, 23, 42, 39, 1.5)],
        [INK, rrect(40, 29, 48, 39, 1.5)],
        [CUT, rect(19.5, 25.5, 39.5, 30)],
        [CUT, rect(43, 30.5, 46.5, 31.5)],
        [CUT, rect(17, 33, 42, 33.8)],
    ];
    for (const x of [24.5, 29.5, 34.5]) shapes.push([INK, rect(x - 0.6, 25, x + 0.6, 30.5)]);
    for (const x of [23, 41.5])
        shapes.push([CUT, circle(x, 39.5, 4.6)], [INK, circle(x, 39.5, 3.4)], [CUT, circle(x, 39.5, 1)]);
    return shapes;
}

// THUG1's School II (THPS2's, Southern California): an apple for the teacher.
function apple(): Layer[] {
    return [
        [INK, circle(28.3, 33, 8.6)],
        [INK, circle(35.7, 33, 8.6)],
        [INK, ellipse(32, 38, 9, 7)],
        [CUT, circle(32, 23.2, 2.4)],
        [
            INK,
            polyline(
                [
                    [31.6, 26],
                    [32, 21.5],
                    [33.5, 18],
                ],
                1.8,
            ),
        ],
        [INK, leaf(33.5, 21.5, 41.5, 17.5, -2.2, 4.2)],
        [CUT, ellipse(26, 30.5, 1.3, 2.6, 25)],
    ];
}

// THUG1's NJ skate shop: a price tag.
function tag(): Layer[] {
    const flat: Layer[] = [
        [INK, poly([18.5, 32], [25, 25], [45.5, 25], [45.5, 39], [25, 39])],
        [CUT, circle(25, 32, 2.2)],
        // a $
        [CUT, line(36, 27, 36, 37, 1.1)],
        [CUT, arc(36, 30, 2.4, 1.5, 0, 270)],
        [CUT, arc(36, 34, 2.4, 1.5, 180, 450)],
    ];
    return [
        ...turnAll(flat, 32, 32, -30),
        [
            INK,
            polyline(
                [
                    [22.4, 30.2],
                    [20, 26],
                    [21.5, 20],
                    [26, 17],
                ],
                1.2,
            ),
        ],
    ];
}

// THAW's Chicago: an L train on its elevated track.
function train(): Layer[] {
    const shapes: Layer[] = [
        [INK, rrect(18, 22, 46, 34, 1.5)],
        [CUT, rect(20.5, 24.5, 43.5, 29)],
        [CUT, rect(18, 31, 46, 31.8)],
        [INK, rect(21, 34, 26, 36)],
        [INK, rect(38, 34, 43, 36)],
        [INK, rect(15.5, 36, 48.5, 38.5)],
        [INK, rect(21, 38.5, 24, 46)],
        [INK, rect(40, 38.5, 43, 46)],
        [INK, line(24, 39, 40, 45.5, 1.2)],
        [INK, line(40, 39, 24, 45.5, 1.2)],
    ];
    for (const x of [26.2, 32, 37.8]) shapes.push([INK, rect(x - 0.6, 24, x + 0.6, 29.5)]);
    return shapes;
}

// THAW's The Mall: a shopping cart.
function cart(): Layer[] {
    const shapes: Layer[] = [
        [
            INK,
            polyline(
                [
                    [17.5, 23.5],
                    [21.5, 23.5],
                    [26, 39],
                    [43, 39],
                ],
                2,
            ),
        ],
        [INK, poly([22, 25], [47, 25], [43.5, 36], [25.2, 36])],
    ];
    for (const x of [28, 33, 38, 43]) shapes.push([CUT, line(x, 26.8, x - 0.8, 34.4, 1.2)]);
    shapes.push([CUT, rect(20, 30.3, 48, 31.3)]);
    shapes.push([INK, circle(27.5, 43, 2.4)], [INK, circle(41, 43, 2.4)]);
    return shapes;
}

// THAW's Minneapolis: the Spoonbridge and Cherry.
function spoonbridge(): Layer[] {
    return scaleAll(spoonbridgeDrawn(), 32, 29, 0.88);
}

function spoonbridgeDrawn(): Layer[] {
    return [
        [
            INK,
            polyline(
                [
                    [17, 41],
                    [24, 39.5],
                    [31, 36.5],
                    [36, 33],
                ],
                2.6,
            ),
        ],
        [INK, ellipse(40.5, 30.5, 7, 3.6, -28)],
        [CUT, ellipse(41.5, 29, 4.6, 1.2, -28)],
        [INK, circle(40.5, 22.5, 5)],
        [CUT, ellipse(38.4, 20.6, 1, 1.6, 30)],
        [
            INK,
            polyline(
                [
                    [40.5, 17.5],
                    [39, 14],
                    [35.5, 11.5],
                ],
                1.4,
            ),
        ],
        [
            INK,
            polyline(
                [
                    [18, 44.5],
                    [24, 43],
                    [30, 44.5],
                    [36, 43],
                    [42, 44.5],
                ],
                1.6,
            ),
        ],
    ];
}

// THAW's Santa Cruz: a surfboard.
function surfboard(): Layer[] {
    return scaleAll(surfboardDrawn(), 32, 30, 0.88);
}

function surfboardDrawn(): Layer[] {
    const board: Layer[] = [
        [
            INK,
            poly(
                [32, 10.5],
                [35.5, 16],
                [37.5, 26],
                [37.5, 38],
                [35.5, 46],
                [32, 48],
                [28.5, 46],
                [26.5, 38],
                [26.5, 26],
                [28.5, 16],
            ),
        ],
        [CUT, rect(31.4, 14, 32.6, 46)],
        [CUT, rect(26, 21.5, 38, 23.2)],
        [INK, poly([31.5, 40], [32.5, 40], [32.5, 46], [31.5, 46])],
    ];
    return turnAll(board, 32, 29.5, 28);
}

// THAW's The Ruins: broken fluted columns on their steps.
function column(): Layer[] {
    const shapes: Layer[] = [
        [INK, poly([21.5, 19], [24, 16.5], [26, 19.5], [29, 17.5], [29, 41], [21.5, 41])],
        [INK, poly([34.5, 29], [37, 31], [39.5, 27.5], [42, 30], [42, 41], [34.5, 41])],
        [INK, rect(20, 40.5, 44, 43.8)],
        [INK, rect(22, 43.8, 42, 46)],
    ];
    for (const x of [24, 26.5, 37, 39.5]) shapes.push([CUT, rect(x - 0.45, 21, x + 0.45, 39.5)]);
    shapes.push([CUT, rect(20, 42, 44, 42.6)]);
    return shapes;
}

// THAW's Vans Park: a skate shoe with its side stripe.
function shoe(): Layer[] {
    return [
        [
            INK,
            poly(
                [16.5, 36],
                [18.5, 31.5],
                [24, 29.5],
                [29.5, 26.5],
                [34, 22],
                [37, 19.5],
                [44, 19.5],
                [46, 23],
                [47.5, 36],
            ),
        ],
        [INK, rrect(16, 36, 48, 41.5, 2)],
        [CUT, rect(16, 37.4, 48, 38.4)],
        [
            CUT,
            polyline(
                [
                    [22, 35],
                    [30, 31.5],
                    [38, 31],
                    [44, 26],
                ],
                1.5,
            ),
        ],
        [CUT, line(29, 27.6, 31.8, 30.4, 1.0)],
        [CUT, line(32, 24.8, 34.8, 27.6, 1.0)],
        [CUT, ellipse(41, 21.7, 2.6, 1.0)],
    ];
}

// THAW's Hollywood: a clapperboard.
function clapper(): Layer[] {
    const stick: Layer[] = [[INK, rect(20.5, 22, 44, 26)]];
    for (const x of [24, 30, 36, 42])
        stick.push([CUT, poly([x, 22.7], [x + 2.4, 22.7], [x + 0.6, 25.3], [x - 1.8, 25.3])]);
    return [
        ...turnAll(stick, 21, 26.5, -13),
        [INK, rect(20.5, 27.5, 44, 44)],
        [CUT, rect(22.5, 29.5, 42, 30.4)],
        [CUT, rect(22.5, 32.5, 42, 32.9)],
        [CUT, rect(31.7, 29.5, 32.5, 42)],
        [CUT, rect(22.5, 36.5, 42, 37.2)],
    ];
}

// THAW's Beverly Hills: a mansion's columned front.
function mansion(): Layer[] {
    const shapes: Layer[] = [
        [INK, poly([19, 24], [32, 15.5], [45, 24])],
        [CUT, poly([25, 22.4], [32, 18.6], [39, 22.4])],
        [INK, rect(20, 24, 44, 27)],
        [INK, rect(19.5, 41, 44.5, 43.5)],
        [INK, rect(21.5, 43.5, 42.5, 46)],
    ];
    for (const x of [22.5, 27.5, 36.5, 41.5]) shapes.push([INK, rect(x - 1.4, 27, x + 1.4, 41)]);
    shapes.push([INK, rrect(29.6, 31, 34.4, 41, 1.6)]);
    return shapes;
}

// THAW's Downtown: Los Angeles City Hall.
function cityhall(): Layer[] {
    const shapes: Layer[] = [
        [INK, poly([28.8, 15.5], [35.2, 15.5], [32, 9.5])],
        [INK, rect(29, 15, 35, 19)],
        [INK, rect(27.5, 19, 36.5, 36)],
        [INK, rect(21.5, 34, 42.5, 45)],
        [INK, rect(25, 30, 39, 34)],
    ];
    for (const x of [30.2, 33.8]) shapes.push([CUT, rect(x - 0.45, 21, x + 0.45, 33)]);
    for (const x of [24.5, 28, 36, 39.5]) shapes.push([CUT, rect(x - 0.5, 36.5, x + 0.5, 43)]);
    return shapes;
}

// THAW's East L.A.: a lowrider, long and low, nose up on its hydraulics.
function lowrider(): Layer[] {
    const car: Layer[] = [
        [INK, poly([15.5, 36], [16, 31.5], [25, 30], [28.5, 26], [38, 26], [41.5, 30], [48.5, 31], [48.5, 36])],
        [CUT, poly([29.3, 27.5], [32.6, 27.5], [32.6, 30.2], [27.1, 30.2])],
        [CUT, poly([34.2, 27.5], [37.4, 27.5], [39.7, 30.2], [34.2, 30.2])],
        [CUT, rect(16, 33, 48.5, 33.6)],
    ];
    const shapes = turnAll(car, 32, 35, -6);
    for (const [x, y] of [
        [22.5, 37.4],
        [41.5, 35.4],
    ] as const) {
        shapes.push(
            [CUT, circle(x, y, 4.2)],
            [INK, circle(x, y, 3.3)],
            [CUT, circle(x, y, 1.7)],
            [INK, circle(x, y, 0.9)],
        );
    }
    return shapes;
}

// THAW's Skate Ranch: a horseshoe.
function horseshoe(): Layer[] {
    const shapes: Layer[] = [
        [INK, arc(32, 30, 9.5, 5.5, -10, 190)],
        [INK, line(22.6, 28.3, 23.8, 18.5, 5.2)],
        [INK, line(41.4, 28.3, 40.2, 18.5, 5.2)],
    ];
    for (let i = 0; i < 7; i++) {
        const a = radians(-5 + i * 31.7);
        shapes.push([CUT, ellipse(32 + 9.7 * Math.cos(a), 30 + 9.7 * Math.sin(a), 0.6, 1.0, i * 31.7 - 5 + 90)]);
    }
    return shapes;
}

// THAW's Casino: a pair of dice.
function dice(): Layer[] {
    const pip = (cx: number, cy: number, at: readonly (readonly [number, number])[]): Layer[] =>
        at.map(([u, v]): Layer => [CUT, circle(cx + u * 4.2, cy + v * 4.2, 1.45)]);
    const back: Layer[] = [
        [INK, rrect(30.5, 18.5, 44.5, 32.5, 2.1)],
        ...pip(37.5, 25.5, [
            [-1, -1],
            [0, 0],
            [1, 1],
        ]),
    ];
    const front: Layer[] = [
        [INK, rrect(18, 27, 34, 43, 2.2)],
        ...pip(26, 35, [
            [-1, -1],
            [1, -1],
            [0, 0],
            [-1, 1],
            [1, 1],
        ]),
    ];
    return [
        ...turnAll(back, 37.5, 25.5, 14),
        [CUT, turned(rrect(16.5, 25.5, 35.5, 44.5, 3), 26, 35, -10)],
        ...turnAll(front, 26, 35, -10),
    ];
}

// THAW's Oil Rig: the derrick on its platform.
function rig(): Layer[] {
    const shapes: Layer[] = [
        [INK, poly([30, 10.5], [34, 10.5], [38.5, 28], [25.5, 28])],
        [CUT, poly([31, 14], [33, 14], [35.6, 26], [28.4, 26])],
        [INK, line(30.5, 15, 36, 26.5, 1.1)],
        [INK, line(33.5, 15, 28, 26.5, 1.1)],
        [INK, rect(27, 20, 37, 21)],
        [INK, rect(18, 28, 46, 31.5)],
        [INK, rect(19.5, 23.5, 25, 28)],
        [INK, rect(40, 25, 44, 28)],
        [INK, rect(21.5, 31.5, 24.5, 44)],
        [INK, rect(39.5, 31.5, 42.5, 44)],
        [INK, line(24, 33, 40, 41, 1.1)],
        [INK, line(40, 33, 24, 41, 1.1)],
        [
            INK,
            polyline(
                [
                    [18.5, 43],
                    [22, 41.5],
                    [26, 43],
                    [30, 41.5],
                    [34, 43],
                    [38, 41.5],
                    [42, 43],
                    [45.5, 41.5],
                ],
                1.6,
            ),
        ],
    ];
    return shapes;
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
        [INK, rect(20, 20, 44, 44)],
        [CUT, rect(23, 23, 41, 41)],
        [INK, line(23, 23, 41, 41, 3.4)],
        [INK, line(41, 23, 23, 41, 3.4)],
        [INK, rect(20, 30.5, 44, 33.5)],
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

// --- THPS2's levels (tools/thps2_levels): ten more, none like the others' (THUG1's Hangar has the helicopter, its
// School II the apple, its Venice the dumbbell, its Philadelphia the LOVE sign; Alcatraz the lighthouse) -------------

// Hangar: the airfield's windsock on its pole.
function windsock(): Layer[] {
    return [
        [INK, rect(23.5, 16, 26.5, 47.5)],
        [INK, poly([26.5, 16], [44, 21], [44, 26.5], [26.5, 30])],
        [CUT, poly([31.5, 16.9], [35, 17.9], [35, 28.3], [31.5, 28.9])],
        [CUT, poly([39.5, 19.4], [42, 20.2], [42, 26.9], [39.5, 27.5])],
        [INK, rect(22, 45.5, 28, 47.3)],
    ];
}

// School II: a pencil, point down to the left.
function pencil(): Layer[] {
    const shapes: Layer[] = [
        [INK, poly([15, 32], [22, 28.5], [22, 35.5])],
        [CUT, poly([17.2, 32], [21, 30.1], [21, 33.9])],
        [INK, poly([15, 32], [17.6, 30.7], [17.6, 33.3])],
        [INK, rect(22, 28.5, 42.5, 35.5)],
        [CUT, rect(23.5, 31.4, 41, 32.6)],
        [INK, rect(43.5, 28.5, 45.5, 35.5)],
        [INK, rrect(46.5, 28.5, 49.5, 35.5, 1.2)],
    ];
    return turnAll(shapes, 32, 32, -45);
}

// Marseille: a sailboat of the Vieux-Port.
function sailboat(): Layer[] {
    return [
        [INK, line(32, 14, 32, 38, 1.8)],
        [INK, poly([33.6, 15], [33.6, 35.5], [46, 35.5])],
        [INK, poly([30.4, 19], [30.4, 35.5], [19.5, 35.5])],
        [INK, poly([17.5, 38], [46.5, 38], [42, 44.5], [22, 44.5])],
    ];
}

// NY City: the Empire State Building.
function empire(): Layer[] {
    const shapes: Layer[] = [
        [INK, rect(23.5, 37, 40.5, 48.5)],
        [INK, rect(25.5, 28, 38.5, 37)],
        [INK, rect(28, 21.5, 36, 28)],
        [INK, rect(30, 16.5, 34, 21.5)],
        [INK, line(32, 8.5, 32, 17, 1.6)],
    ];
    for (const x of [27, 30.3, 33.7, 37]) shapes.push([CUT, rect(x - 0.6, 39, x + 0.6, 47)]);
    for (const x of [29, 32, 35]) shapes.push([CUT, rect(x - 0.6, 30, x + 0.6, 35.5)]);
    return shapes;
}

// Venice Beach: a roller skate of the boardwalk.
function rollerskate(): Layer[] {
    const shapes: Layer[] = [
        [INK, poly([22.5, 17], [31, 17], [32, 29], [42.5, 31.5], [45.5, 35.5], [45.5, 39.5], [20.5, 39.5], [21.5, 28])],
        [INK, circle(25, 44.2, 3.4)],
        [INK, circle(40.5, 44.2, 3.4)],
        [CUT, circle(25, 44.2, 1.1)],
        [CUT, circle(40.5, 44.2, 1.1)],
        [INK, circle(45.8, 40, 1.8)],
    ];
    for (const y of [21, 24.5, 28]) shapes.push([CUT, line(25, y, 30, y, 1.2)]);
    return shapes;
}

// Skatestreet: a skate helmet, side on (facing right: the back comes down lower), its vents and strap.
function helmet(): Layer[] {
    return [
        [INK, ellipse(32, 34, 14.5, 13.5)],
        [CUT, rect(27, 36, 64, 64)],
        [CUT, rect(0, 42, 64, 64)],
        [INK, rrect(39, 34, 48, 37, 1.2)],
        [CUT, circle(26, 27.5, 1.7)],
        [CUT, circle(32, 25, 1.7)],
        [CUT, circle(38, 27.5, 1.7)],
        [CUT, circle(33.5, 31.5, 1.4)],
        [INK, line(29, 36, 33, 44, 1.6)],
    ];
}

// Philadelphia: a soft pretzel.
function pretzel(): Layer[] {
    return [
        [INK, ring(25.5, 29, 7, 3.4)],
        [INK, ring(38.5, 29, 7, 3.4)],
        [INK, arc(32, 31, 13.2, 3.4, 25, 155)],
        [CUT, circle(25.5, 22, 0.8)],
        [CUT, circle(38.5, 22, 0.8)],
        [CUT, circle(32, 44.2, 0.8)],
    ];
}

// The Bullring: a bull's head.
function bull(): Layer[] {
    return [
        [
            INK,
            polyline(
                [
                    [28, 27],
                    [23, 25.5],
                    [21.5, 22],
                    [22.5, 19.5],
                ],
                3.2,
            ),
        ],
        [
            INK,
            polyline(
                [
                    [36, 27],
                    [41, 25.5],
                    [42.5, 22],
                    [41.5, 19.5],
                ],
                3.2,
            ),
        ],
        [INK, ellipse(23, 30.5, 3.6, 1.8, 20)],
        [INK, ellipse(41, 30.5, 3.6, 1.8, -20)],
        [INK, poly([25.5, 25.5], [38.5, 25.5], [36.5, 42], [27.5, 42])],
        [INK, ellipse(32, 43, 5.5, 3.8)],
        [CUT, circle(28.8, 31.5, 1.3)],
        [CUT, circle(35.2, 31.5, 1.3)],
        [CUT, circle(30, 44, 0.9)],
        [CUT, circle(34, 44, 0.9)],
    ];
}

// Chopper Drop (Hawaii): a volcano, smoking.
function volcano(): Layer[] {
    return [
        [INK, poly([20, 44.5], [27.5, 25], [36.5, 25], [44, 44.5])],
        [CUT, ellipse(32, 25, 4.5, 1.4)],
        [
            CUT,
            polyline(
                [
                    [31, 26.5],
                    [29.5, 32],
                    [30.5, 38],
                ],
                1.5,
            ),
        ],
        [INK, circle(30.5, 19.5, 3)],
        [INK, circle(34.5, 15.5, 3.4)],
        [INK, circle(29.5, 12.5, 2.5)],
    ];
}

// Skate Heaven: a halo over wings.
function halo(): Layer[] {
    // (the left wing, its feathers fanning up and out)
    const wing: Point[] = [
        [31, 36],
        [28.5, 30],
        [24.5, 25.5],
        [19.5, 22],
        [17.5, 25],
        [20, 27],
        [15.5, 28.5],
        [19, 31],
        [15.5, 33.5],
        [20, 35],
        [18, 38],
        [24, 38.5],
        [31, 39],
    ];
    return [
        [INK, ellipse(32, 18.5, 8, 3)],
        [CUT, ellipse(32, 18.5, 5.6, 1.4)],
        [INK, poly(...wing)],
        [INK, poly(...wing.map(([x, y]): Point => [64 - x, y]))],
        [INK, ellipse(32, 33, 2.4, 5)],
    ];
}

// --- THP8's zones (tools/thp8_levels): THP8's own area names, a pictogram each ------------------------------------

// Suburbia: a mailbox on its post, the flag up.
function mailbox(): Layer[] {
    return [
        [INK, ellipse(30, 25, 10, 5)],
        [INK, rect(20, 25, 40, 34)],
        [CUT, rect(36, 27, 37.5, 32)],
        [INK, rect(28, 34, 32, 46)],
        [INK, rect(40.5, 20, 42.5, 30)],
        [INK, rect(40.5, 20, 45, 23.5)],
    ];
}

// The school: its bell.
function schoolbell(): Layer[] {
    return [
        [INK, ring(32, 16, 2, 1.6)],
        [INK, poly([32, 17.5], [36, 19], [38, 24], [38.5, 32], [43, 37.5], [21, 37.5], [25.5, 32], [26, 24], [28, 19])],
        [CUT, rect(21, 34, 43, 35)],
        [INK, circle(32, 41, 3)],
    ];
}

// The school inside: two lockers.
function locker(): Layer[] {
    const door = (x0: number): Layer[] => [
        [INK, rect(x0, 17, x0 + 9, 46)],
        [CUT, rect(x0 + 2, 20, x0 + 7, 21)],
        [CUT, rect(x0 + 2, 22.5, x0 + 7, 23.5)],
        [CUT, rect(x0 + 2, 25, x0 + 7, 26)],
        [CUT, rect(x0 + 6.5, 30, x0 + 7.5, 35)],
    ];
    return [...door(22), ...door(33)];
}

// Main Street: a traffic light.
function trafficlight(): Layer[] {
    return [
        [INK, rect(26, 15, 38, 41)],
        [CUT, circle(32, 20.5, 3.6)],
        [CUT, circle(32, 28, 3.6)],
        [CUT, circle(32, 35.5, 3.6)],
        [INK, circle(32, 20.5, 2.2)],
        [INK, rect(30.5, 41, 33.5, 48)],
    ];
}

// The slums: a brick wall.
function bricks(): Layer[] {
    const layers: Layer[] = [[INK, rect(19, 21, 45, 43)]];
    const rows = [21, 26.5, 32, 37.5, 43];
    for (let r = 0; r < 4; r++) {
        if (r) layers.push([CUT, rect(19, rows[r]! - 0.6, 45, rows[r]! + 0.6)]);
        for (let x = r % 2 ? 23 : 28; x < 45; x += 9)
            layers.push([CUT, rect(x - 0.6, rows[r]!, x + 0.6, rows[r + 1]!)]);
    }
    return layers;
}

// The factory: a gear.
function gear(): Layer[] {
    const layers: Layer[] = [[INK, circle(32, 32, 10)]];
    for (let k = 0; k < 8; k++) {
        const a = radians(k * 45 + 22.5);
        layers.push([
            INK,
            line(32 + 9 * Math.cos(a), 32 + 9 * Math.sin(a), 32 + 14 * Math.cos(a), 32 + 14 * Math.sin(a), 5),
        ]);
    }
    layers.push([CUT, circle(32, 32, 4.5)]);
    return layers;
}

// The Capitol: a flag on its pole.
function capitolflag(): Layer[] {
    return [
        [INK, rect(24, 16, 26.5, 46)],
        [INK, circle(25.25, 16, 1.6)],
        [INK, poly([26.5, 19], [31, 17.5], [36, 20], [42, 19.5], [42, 31], [37.5, 32.5], [32, 29.5], [26.5, 31])],
        [CUT, rect(26.5, 24, 42, 25)],
        [INK, rect(22.5, 44, 29.5, 46)],
    ];
}

// The city park: a park bench.
function bench(): Layer[] {
    return [
        [INK, rect(19, 23, 45, 27.5)],
        [CUT, rect(19, 25, 45, 25.6)],
        [INK, rect(21, 27.5, 23, 31)],
        [INK, rect(41, 27.5, 43, 31)],
        [INK, rect(17, 31, 47, 34.5)],
        [INK, rect(20, 34.5, 23, 42)],
        [INK, rect(41, 34.5, 44, 42)],
    ];
}

// The skate park: a bowl, its coping and deck.
function bowl(): Layer[] {
    return [
        [INK, arc(32, 25, 12.5, 3.4, 0, 180)],
        [INK, circle(19.5, 25, 2)],
        [INK, circle(44.5, 25, 2)],
        [INK, rect(16, 23.5, 19.5, 26)],
        [INK, rect(44.5, 23.5, 48, 26)],
        [INK, rect(26, 40.5, 38, 43)],
    ];
}

// The fun park: a roller coaster's loop.
function coaster(): Layer[] {
    return [
        [INK, ring(32, 27, 9, 3)],
        [
            INK,
            polyline(
                [
                    [18, 41],
                    [28, 41],
                    [32, 36],
                ],
                3,
            ),
        ],
        [
            INK,
            polyline(
                [
                    [32, 36],
                    [36, 41],
                    [46, 41],
                ],
                3,
            ),
        ],
        [INK, rect(21, 41, 23, 46)],
        [INK, rect(41, 41, 43, 46)],
        [INK, rect(30.8, 36, 33.2, 47)],
    ];
}

// Hill Top: a parking sign.
function parking(): Layer[] {
    return [
        [INK, rect(22, 17, 42, 37)],
        [CUT, rect(27, 21, 30, 33)],
        [CUT, arc(30.5, 24.75, 3.75, 3, -90, 90)],
        [INK, rect(30.5, 37, 33.5, 47)],
    ];
}

// Training: a target.
function target(): Layer[] {
    return [
        [INK, ring(32, 32, 12, 3)],
        [INK, ring(32, 32, 6.5, 3)],
        [INK, circle(32, 32, 2.5)],
    ];
}

// The cartoon world: a cloud.
function cloud(): Layer[] {
    return [
        [INK, circle(25, 33, 7)],
        [INK, circle(33, 28, 9)],
        [INK, circle(41, 34, 6)],
        [INK, rect(25, 33, 41, 40)],
    ];
}

// The skate shop: a skate tool (the T-shaped wrench).
function skatetool(): Layer[] {
    return [
        [INK, line(21.5, 23, 42.5, 23, 5.5)],
        [INK, line(32, 23, 32, 44, 5)],
        [CUT, circle(22, 23, 1.5)],
        [CUT, circle(42, 23, 1.5)],
        [CUT, circle(32, 42.5, 1.4)],
    ];
}

// The bedroom: a bed.
function bed(): Layer[] {
    return [
        [INK, rect(17, 22, 20, 42)],
        [INK, rect(20, 31, 47, 37)],
        [INK, rect(44, 28, 47, 42)],
        [INK, ellipse(24.5, 28.5, 3.5, 2)],
        [INK, rect(29, 28.5, 44, 31)],
    ];
}

export const PICTOGRAMS: ReadonlyMap<string, () => Layer[]> = new Map(
    [
        skateboard,
        cone,
        bridge,
        mortarboard,
        palm,
        anchor,
        bigben,
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
        pine,
        redeemer,
        torii,
        ship,
        crate,
        ufo,
        ladle,
        tower,
        trophy,
        freeway,
        tokyotower,
        burnside,
        whistle,
        shop,
        lighthouse,
        tent,
        snake,
        elephant,
        tshirt,
        hydrant,
        totem,
        cactus,
        love,
        dumbbell,
        helicopter,
        bus,
        apple,
        tag,
        train,
        cart,
        spoonbridge,
        surfboard,
        column,
        shoe,
        clapper,
        mansion,
        cityhall,
        lowrider,
        horseshoe,
        dice,
        rig,
        windsock,
        pencil,
        sailboat,
        empire,
        rollerskate,
        helmet,
        pretzel,
        bull,
        volcano,
        halo,
        mailbox,
        schoolbell,
        locker,
        trafficlight,
        bricks,
        gear,
        capitolflag,
        bench,
        bowl,
        coaster,
        parking,
        target,
        cloud,
        skatetool,
        bed,
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

// The pictograms keep clear of the frame: every ink sample point within this of the middle (|dx| + |dy|) - THUG2's
// own reach as far (their dark pixels' centres to 25).
export const MARGIN_LIMIT = INSIDE - 0.5;

// The sample points (as coverage() takes them, over the whole sign) where `shapes` leave ink beyond MARGIN_LIMIT.
export function strayInk(shapes: readonly Layer[]): Point[] {
    const out: Point[] = [];
    for (let y = 0; y < SIZE; y++) {
        for (let x = 0; x < SIZE; x++) {
            for (let j = 0; j < SAMPLES; j++) {
                for (let i = 0; i < SAMPLES; i++) {
                    const px = x + (i + 0.5) / SAMPLES;
                    const py = y + (j + 0.5) / SAMPLES;
                    if (Math.abs(px - 32) + Math.abs(py - 32) <= MARGIN_LIMIT) continue;
                    let ink = 0;
                    for (const [mode, shape] of shapes) if (shape(px, py)) ink = mode;
                    if (ink) out.push([px, py]);
                }
            }
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

// The sign's yellow (the level select tints the grey signs; roughly what it shows): a grey's share of the fill.
const YELLOW = [242, 200, 24] as const;

async function main(): Promise<number> {
    const { prxFile, prxFiles } = await import("./make_menu_level.ts");
    const levelSelect = await import("./level_select.ts");
    const { values, positionals } = parseCli(USAGE, {
        options: {
            game: { type: "string", default: "C:\\Games\\THUG2\\Game" },
            scale: { type: "string", default: "3" },
            sheet: { type: "boolean", default: false },
        },
        allowPositionals: true,
    });
    const [out, ...given] = positionals;
    if (out === undefined) {
        process.stderr.write(`${USAGE}\n`);
        return 2;
    }
    // (the install root or the game folder itself: C:\Games\THUG2 -> C:\Games\THUG2\Game - tools/lib/install.ts)
    const game = resolveInstall(values.game, "thug2");
    const prx = pyJoin(game, "Data", "pre", "Levelselect.prx");
    const frame = prxFile(prx, FRAME_SOURCE);
    const scale = Number(values.scale);
    // rows of signs: [a row's images (as read), its names]
    const groups: [Img[], string[]][] = [];
    const ours = (names: string[]): [Img[], string[]] => [names.map((name) => readImg(makeSign(frame, name))), names];
    if (values.sheet) {
        // THUG2's own signs, then ours section by section (THUG2's added two, THUG1, THPS4, THPS3, THAW), in yellow
        const own = [...prxFiles(prx, ".img.xbx")].filter(([name]) => /\\sign_/i.test(name));
        groups.push([
            own.map(([, data]) => readImg(data)),
            own.map(([name]) => name.replace(/^.*\\|\.img\.xbx$/g, "")),
        ]);
        groups.push(ours([...levelSelect.THUG2_EXTRA.values()].map((e) => e.icon)));
        for (const table of [
            levelSelect.THUG1_LEVELS,
            levelSelect.THPS4_LEVELS,
            levelSelect.THPS3_LEVELS,
            levelSelect.THAW_LEVELS,
        ]) {
            groups.push(ours([...table.values()].map((e) => e.icon!).filter((icon) => !icon.startsWith("texture:"))));
        }
    } else {
        groups.push(ours(given.length ? given : [...PICTOGRAMS.keys()]));
    }
    const cols = values.sheet ? 17 : 7;
    const grey = 90;
    const cell = SIZE * scale + 6;
    const gap = values.sheet ? Math.floor(cell / 4) : 0;
    const rowsOf = (n: number): number => Math.floor((n + cols - 1) / cols);
    const width = cols * cell;
    const height = groups.reduce((h, [images]) => h + rowsOf(images.length) * cell + gap, 0) - gap;
    const image = new Uint8Array(width * height * 4);
    for (let i = 0; i < width * height; i++) image.set([grey, grey, grey, 255], i * 4);
    let top = 0;
    for (const [images] of groups) {
        images.forEach(({ palette, indices }, n) => {
            const ox = (n % cols) * cell;
            const oy = top + Math.floor(n / cols) * cell;
            for (let y = 0; y < SIZE * scale; y++) {
                for (let x = 0; x < SIZE * scale; x++) {
                    const [b, g, r, a] = palette[indices[Math.floor(y / scale) * SIZE + Math.floor(x / scale)]!]!;
                    const rgb = values.sheet ? YELLOW.map((c) => Math.min(255, (c * r) / FILL)) : [r, g, b];
                    const o = ((oy + y) * width + ox + x) * 4;
                    image.set([...rgb.map((c) => Math.trunc((c * a) / 255 + grey * (1 - a / 255))), 255], o);
                }
            }
        });
        top += rowsOf(images.length) * cell + gap;
    }
    writePng(out, width, height, image);
    print(`wrote ${out}:\n${groups.map(([, names]) => `    ${names.join(", ")}`).join("\n")}`);
    return 0;
}

// (not awaited: main imports make_menu_level.ts, which imports this module through level_select.ts - a top-level
// await here would wait for itself)
if (isMain(import.meta.url)) void runMain(() => main());
