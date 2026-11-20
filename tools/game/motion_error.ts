// The picture's error while the camera moves (round MV): a repeatable camera move at a spot, the realtime frames
// (Ray Reconstruction's output, linear) against the path-traced reference at the same poses, per term of the light and
// per screen region (newly revealed against kept history).
//
//   node tools/game/motion_error.ts plan <spot> [--motions pan,turn,dolly] [--terms off,lamps,...] [--ref-samples N]
//        [--seeds N] [--out commands.txt]
//     the automation commands of a measurement session at a spot (p1, p2, p3 - BO at night, the user's three spots):
//     for each motion the hold at its start, the move, the frames after it stops, each term's run, the converged picture
//     and two references at its mid and stop poses; with --seeds the same move replayed N times (the noise's mean).
//   node tools/game/motion_error.ts analyse <Screenshots folder> <spot> [--json out.json] [--crops <folder>]
//     the tables: per motion and term the error of each frame against the reference / the converged picture, in display
//     space after one exposure (ACES, sRGB; the session's median pre-exposure), split into newly revealed pixels and
//     those with history; the references' own noise taken out.
//   node tools/game/motion_error.ts compare <a.exr> <b.exr> [--exposure E] [--grid CxR]
//     two linear pictures: their means and the display-space error, whole and per cell.
//   node tools/game/motion_error.ts terms <spot> [--ref-samples N] [--tag t] [--out file]
//     (round BIAS) the light term by term, the camera held: each realtime view converged (rt_debug_view, 400 frames)
//     and the reference's matching light (`pt ref` with only / direct / bounces 0, 1, 2) at the spot's pose.
//   node tools/game/motion_error.ts terms-analyse <Screenshots folder> <spot> [--tag t] [--grid CxR] [--json out.json]
//     realtime / reference per term (each view less the emission it holds), whole and per cell.
//   node tools/game/motion_error.ts pair <a.exr> [b.exr ...] <out.png> [--exposure E]
//     linear pictures side by side through one exposure (`pt frames` ones taken to the global unit first).
//   node tools/game/motion_error.ts motion <folder>
//     Ray Reconstruction's motion vectors (`pt frames ... inputs`) against the depth and the two frames' cameras (a
//     frozen world: the camera's motion is all there is), and the albedo guide of the frame before fetched through them.
//
// The game side: `camera ... <turn> <frames>` (a move tied to the frame count - game/rider/test_view.h), `console measure
// on` (the world frozen, every frame drawn once), `console pt frames <count> <folder> [every]` (the frames' linear light
// after the upscaler: frame_NNNN.exr + frames.txt) and `console pt ref <samples> <EV100> as <name>` / `waitfor pt`.
import * as fs from "node:fs";
import * as path from "node:path";
import { isMain, parseCli, print, runMain } from "thug2-level-converter-core/lib/cli.ts";
import { encodePng } from "thug2-level-converter-core/lib/frame_diff.ts";

const USAGE =
    "usage: motion_error.ts plan <spot> [--motions pan,turn,dolly] [--terms off,...] [--ref-samples N] [--seeds N] [--out file]\n" +
    "       motion_error.ts analyse <Screenshots folder> <spot> [--json out.json] [--crops folder]\n" +
    "       motion_error.ts compare <a.exr> <b.exr> [--exposure E] [--grid CxR]\n" +
    "       motion_error.ts motion <pt frames ... inputs folder>\n" +
    "       motion_error.ts terms <spot> [--ref-samples N] [--tag t] [--out file]\n" +
    "       motion_error.ts terms-analyse <Screenshots folder> <spot> [--tag t] [--grid CxR] [--json out.json]\n" +
    "       motion_error.ts pair <a.exr> [b.exr ...] <out.png> [--exposure E]";

// --- linear pictures ---------------------------------------------------------------------------------------------------

export interface Picture {
    width: number;
    height: number;
    rgb: Float32Array; // width * height * 3, linear
}

export function halfToFloat(h: number): number {
    const sign = h & 0x8000 ? -1 : 1;
    const exponent = (h >> 10) & 0x1f;
    const fraction = h & 0x3ff;
    if (exponent === 0) return sign * fraction * 2 ** -24;
    if (exponent === 31) return fraction ? Number.NaN : sign * Number.POSITIVE_INFINITY;
    return sign * (1 + fraction / 1024) * 2 ** (exponent - 15);
}

// An OpenEXR file as the game writes them: one part, scanlines, no compression, HALF or FLOAT channels R, G, B.
export function readExr(data: Buffer): Picture {
    if (data.readInt32LE(0) !== 20000630) throw new Error("not an OpenEXR file");
    let at = 8;
    const text = (): string => {
        const end = data.indexOf(0, at);
        const value = data.toString("latin1", at, end);
        at = end + 1;
        return value;
    };
    const channels: { name: string; type: number }[] = [];
    let box = [0, 0, 0, 0];
    for (;;) {
        const name = text();
        if (name === "") break;
        const type = text();
        const size = data.readInt32LE(at);
        at += 4;
        const value = data.subarray(at, at + size);
        at += size;
        if (type === "chlist") {
            let c = 0;
            while (value[c] !== 0) {
                const end = value.indexOf(0, c);
                const channel = value.toString("latin1", c, end);
                channels.push({ name: channel, type: value.readInt32LE(end + 1) });
                c = end + 1 + 16;
            }
        } else if (name === "compression" && value[0] !== 0) {
            throw new Error("compressed OpenEXR files aren't read");
        } else if (name === "dataWindow") {
            box = [value.readInt32LE(0), value.readInt32LE(4), value.readInt32LE(8), value.readInt32LE(12)];
        }
    }
    const width = box[2]! - box[0]! + 1;
    const height = box[3]! - box[1]! + 1;
    const rgb = new Float32Array(width * height * 3);
    const offsets = at;
    for (let y = 0; y < height; y++) {
        let p = Number(data.readBigUInt64LE(offsets + y * 8)) + 8;
        for (const channel of channels) {
            const slot = channel.name === "R" ? 0 : channel.name === "G" ? 1 : channel.name === "B" ? 2 : -1;
            for (let x = 0; x < width; x++) {
                let v: number;
                if (channel.type === 1) {
                    v = halfToFloat(data.readUInt16LE(p));
                    p += 2;
                } else if (channel.type === 2) {
                    v = data.readFloatLE(p);
                    p += 4;
                } else {
                    v = data.readUInt32LE(p);
                    p += 4;
                }
                if (slot >= 0) rgb[(y * width + x) * 3 + slot] = Number.isFinite(v) ? v : 0;
            }
        }
    }
    return { width, height, rgb };
}

export function scaled(picture: Picture, factor: number): Picture {
    const rgb = new Float32Array(picture.rgb.length);
    for (let i = 0; i < rgb.length; i++) rgb[i] = picture.rgb[i]! * factor;
    return { width: picture.width, height: picture.height, rgb };
}

export function luma(r: number, g: number, b: number): number {
    return 0.2126 * r + 0.7152 * g + 0.0722 * b;
}

// ACES (Stephen Hill's fit of the RRT + sRGB ODT - the game's rt_tonemap=2) and the sRGB encoding: 0 - 1.
function rrtOdt(v: number): number {
    return (v * (v + 0.0245786) - 0.000090537) / (v * (0.983729 * v + 0.432951) + 0.238081);
}
function srgb(v: number): number {
    const c = Math.min(Math.max(v, 0), 1);
    return c <= 0.0031308 ? 12.92 * c : 1.055 * c ** (1 / 2.4) - 0.055;
}
export function displayRgb(r: number, g: number, b: number): [number, number, number] {
    const a = rrtOdt(0.59719 * r + 0.35458 * g + 0.04823 * b);
    const bb = rrtOdt(0.076 * r + 0.90834 * g + 0.01566 * b);
    const c = rrtOdt(0.0284 * r + 0.13383 * g + 0.83777 * b);
    return [
        srgb(1.60475 * a - 0.53108 * bb - 0.07367 * c),
        srgb(-0.10208 * a + 1.10813 * bb - 0.00605 * c),
        srgb(-0.00327 * a - 0.07276 * bb + 1.07602 * c),
    ];
}

// The picture as shown at `exposure` (the light x it into the curve): RGB 0 - 1.
export function display(picture: Picture, exposure: number): Float32Array {
    const out = new Float32Array(picture.rgb.length);
    for (let i = 0; i < out.length; i += 3) {
        const [r, g, b] = displayRgb(
            picture.rgb[i]! * exposure,
            picture.rgb[i + 1]! * exposure,
            picture.rgb[i + 2]! * exposure,
        );
        out[i] = r;
        out[i + 1] = g;
        out[i + 2] = b;
    }
    return out;
}

export interface Error {
    pixels: number;
    meanA: number; // linear luma
    meanB: number;
    ratio: number; // meanA / meanB
    rmse: number; // display space, 0 - 255
    floor: number; // the reference's own noise in the same units (0 without a second reference)
    excess: number; // sqrt(max(0, rmse^2 - floor^2)): the error that isn't the reference's
    // the error's fine part - the display luma's difference less its own 15 x 15 mean (noise, specks: what flickers),
    // the reference's noise taken out as above - and its broad part (the 15 x 15 mean: a region too bright or dark)
    fine: number;
    broad: number;
}

const kBroadRadius = 7; // (a 15 x 15 box: the error's broad part)

// A luma map's 15 x 15 box mean (an integral image; the box cut at the edges).
function boxMean(w: number, h: number, v: Float32Array): Float32Array {
    const integral = new Float64Array((w + 1) * (h + 1));
    for (let y = 0; y < h; y++) {
        let row = 0;
        for (let x = 0; x < w; x++) {
            row += v[y * w + x]!;
            integral[(y + 1) * (w + 1) + x + 1] = integral[y * (w + 1) + x + 1]! + row;
        }
    }
    const out = new Float32Array(w * h);
    for (let y = 0; y < h; y++) {
        const y0 = Math.max(0, y - kBroadRadius);
        const y1 = Math.min(h, y + kBroadRadius + 1);
        for (let x = 0; x < w; x++) {
            const x0 = Math.max(0, x - kBroadRadius);
            const x1 = Math.min(w, x + kBroadRadius + 1);
            const sum =
                integral[y1 * (w + 1) + x1]! -
                integral[y0 * (w + 1) + x1]! -
                integral[y1 * (w + 1) + x0]! +
                integral[y0 * (w + 1) + x0]!;
            out[y * w + x] = sum / ((x1 - x0) * (y1 - y0));
        }
    }
    return out;
}

// The display luma's difference split into its fine part (less its box mean) and its broad part (the box mean), RMS over
// the mask, 0 - 255; with a second reference the fine part of the references' own half-difference too.
function splitError(
    w: number,
    h: number,
    da: Float32Array,
    db: Float32Array,
    mask?: Uint8Array,
    dc?: Float32Array,
): { fine: number; broad: number; fineFloor: number } {
    const y = (d: Float32Array, i: number): number => luma(d[i * 3]!, d[i * 3 + 1]!, d[i * 3 + 2]!);
    const diff = new Float32Array(w * h);
    const noise = new Float32Array(w * h);
    for (let i = 0; i < w * h; i++) {
        const ref = dc ? (y(db, i) + y(dc, i)) / 2 : y(db, i);
        diff[i] = y(da, i) - ref;
        if (dc) noise[i] = (y(db, i) - y(dc, i)) / 2;
    }
    const mean = boxMean(w, h, diff);
    const noiseMean = dc ? boxMean(w, h, noise) : noise;
    let n = 0;
    let sf = 0;
    let sb = 0;
    let sn = 0;
    for (let i = 0; i < w * h; i++) {
        if (mask && !mask[i]) continue;
        n++;
        sf += (diff[i]! - mean[i]!) ** 2;
        sb += mean[i]! ** 2;
        if (dc) sn += (noise[i]! - noiseMean[i]!) ** 2;
    }
    return n
        ? { fine: 255 * Math.sqrt(sf / n), broad: 255 * Math.sqrt(sb / n), fineFloor: 255 * Math.sqrt(sn / n) }
        : { fine: 0, broad: 0, fineFloor: 0 };
}
// a against b (b the reference; c a second, independent reference: b and c's mean is the reference then, and half their
// difference its noise), over the pixels `mask` keeps (all without one). Display values are 0 - 1 RGB (display()).
export function compare(
    a: Picture,
    da: Float32Array,
    b: Picture,
    db: Float32Array,
    mask?: Uint8Array,
    c?: Picture,
    dc?: Float32Array,
): Error {
    let n = 0;
    let sa = 0;
    let sb = 0;
    let se = 0;
    let sf = 0;
    const count = a.width * a.height;
    for (let i = 0; i < count; i++) {
        if (mask && !mask[i]) continue;
        n++;
        const j = i * 3;
        let ref = luma(b.rgb[j]!, b.rgb[j + 1]!, b.rgb[j + 2]!);
        if (c) ref = (ref + luma(c.rgb[j]!, c.rgb[j + 1]!, c.rgb[j + 2]!)) / 2;
        sa += luma(a.rgb[j]!, a.rgb[j + 1]!, a.rgb[j + 2]!);
        sb += ref;
        for (let k = 0; k < 3; k++) {
            const r = dc ? (db[j + k]! + dc[j + k]!) / 2 : db[j + k]!;
            const d = da[j + k]! - r;
            se += d * d;
            if (dc) {
                // (the mean of two has half the variance of one: (b - c)^2 / 4 is the mean's noise)
                const h = db[j + k]! - dc[j + k]!;
                sf += (h * h) / 4;
            }
        }
    }
    const { fine, broad, fineFloor } = splitError(a.width, a.height, da, db, mask, dc);
    const rmse = n ? 255 * Math.sqrt(se / (3 * n)) : 0;
    const floor = n ? 255 * Math.sqrt(sf / (3 * n)) : 0;
    return {
        pixels: n,
        meanA: n ? sa / n : 0,
        meanB: n ? sb / n : 0,
        ratio: sb > 0 ? sa / sb : 0,
        rmse,
        floor,
        excess: Math.sqrt(Math.max(0, rmse * rmse - floor * floor)),
        fine: Math.sqrt(Math.max(0, fine * fine - fineFloor * fineFloor)),
        broad,
    };
}

// --- the camera ------------------------------------------------------------------------------------------------------

export interface Vec3 {
    x: number;
    y: number;
    z: number;
}
export interface Hold {
    position: Vec3;
    direction: Vec3;
    drift: Vec3;
    turn: number; // degrees a frame
    frames: number; // the move's length (then held)
}
// game/rider/test_view.cpp camera_pose_at's twin: where `hold` has the camera `frames` frames after its lock.
export function cameraPoseAt(hold: Hold, frames: number): { position: Vec3; direction: Vec3 } {
    const k = hold.frames > 0 ? Math.min(frames, hold.frames) : frames;
    const turn = (hold.turn * k * Math.PI) / 180;
    const s = Math.sin(turn);
    const c = Math.cos(turn);
    return {
        position: {
            x: hold.position.x + hold.drift.x * k,
            y: hold.position.y + hold.drift.y * k,
            z: hold.position.z + hold.drift.z * k,
        },
        direction: {
            x: hold.direction.x * c + hold.direction.z * s,
            y: hold.direction.y,
            z: hold.direction.z * c - hold.direction.x * s,
        },
    };
}

// A tap frame (frames.txt): the camera it was drawn from.
export interface TapFrame {
    index: number;
    width: number;
    height: number;
    preExposure: number;
    position: Vec3;
    forward: Vec3;
    right: Vec3;
    up: Vec3;
    p00: number;
    p11: number;
    p22: number;
    p32: number;
    jitter: [number, number]; // pixels of the drawn size: x right, y down
}
export function parseFrames(text: string): TapFrame[] {
    const out: TapFrame[] = [];
    for (const line of text.split(/\r?\n/)) {
        if (!line.trim() || line.startsWith("#")) continue;
        const v = line.trim().split(/\s+/).map(Number);
        const at = (i: number): number => v[i] ?? 0;
        out.push({
            index: at(0),
            width: at(2),
            height: at(3),
            preExposure: at(4),
            position: { x: at(5), y: at(6), z: at(7) },
            // (the view matrix's third column is the camera's back: its forward is the other way)
            forward: { x: -at(8), y: -at(9), z: -at(10) },
            p00: at(11),
            p11: at(12),
            right: { x: at(15), y: at(16), z: at(17) },
            up: { x: at(18), y: at(19), z: at(20) },
            p22: at(21),
            p32: at(22),
            jitter: [at(13), at(14)],
        });
    }
    return out;
}

function dot(a: Vec3, b: Vec3): number {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
function normalised(a: Vec3): Vec3 {
    const l = Math.hypot(a.x, a.y, a.z) || 1;
    return { x: a.x / l, y: a.y / l, z: a.z / l };
}

// The move's frame k a tap frame was drawn at: the k whose pose is nearest (position and direction).
export function moveFrameOf(frame: TapFrame, hold: Hold): number {
    let best = 0;
    let bestCost = Number.POSITIVE_INFINITY;
    for (let k = 0; k <= hold.frames; k++) {
        const pose = cameraPoseAt(hold, k);
        const d = normalised(pose.direction);
        const cost =
            Math.hypot(
                pose.position.x - frame.position.x,
                pose.position.y - frame.position.y,
                pose.position.z - frame.position.z,
            ) +
            100 * (1 - dot(d, normalised(frame.forward)));
        if (cost < bestCost - 1e-9) {
            bestCost = cost;
            best = k;
        }
    }
    return best;
}

// How many of the frames before `k` in a row saw each pixel's direction (0 - most): a direction leaving the view and
// coming back counts from its return. Directions only - exact for a turn; a move's parallax (what a nearer surface
// uncovers) isn't counted. The frames before the first are the first's (the hold before the move).
export function historyAge(frames: TapFrame[], k: number, most: number): Uint8Array {
    const now = frames[k]!;
    const { width, height } = now;
    const age = new Uint8Array(width * height);
    for (let y = 0; y < height; y++) {
        for (let x = 0; x < width; x++) {
            const nx = ((x + 0.5) / width) * 2 - 1;
            const ny = 1 - ((y + 0.5) / height) * 2;
            const d = normalised({
                x: now.forward.x + (nx / now.p00) * now.right.x + (ny / now.p11) * now.up.x,
                y: now.forward.y + (nx / now.p00) * now.right.y + (ny / now.p11) * now.up.y,
                z: now.forward.z + (nx / now.p00) * now.right.z + (ny / now.p11) * now.up.z,
            });
            let a = 0;
            for (let j = 1; j <= most; j++) {
                const before = frames[Math.max(0, k - j)]!;
                const z = dot(d, before.forward);
                if (z <= 0) break;
                const bx = (dot(d, before.right) / z) * before.p00;
                const by = (dot(d, before.up) / z) * before.p11;
                if (Math.abs(bx) > 1 || Math.abs(by) > 1) break;
                a = j;
            }
            age[y * width + x] = a;
        }
    }
    return age;
}

// --- the session's plan -----------------------------------------------------------------------------------------------

export interface Spot {
    name: string;
    skater: Vec3;
    heading: number;
    camera: Vec3;
    direction: Vec3;
}
// BO at night: the user's three spots (their screenshots' sidecars, 2026-11-20 12-56-36 / 47 / 57).
export const kSpots: Spot[] = [
    {
        name: "p1",
        skater: { x: 2460.1, y: 130.7, z: -1516.1 },
        heading: -123.3,
        camera: { x: 2578.6, y: 208.1, z: -1438.3 },
        direction: { x: -0.822, y: -0.179, z: -0.54 },
    },
    {
        name: "p2",
        skater: { x: 2928.7, y: 130.8, z: -2350.3 },
        heading: 147.6,
        camera: { x: 2843.5, y: 208.1, z: -2237.1 },
        direction: { x: 0.592, y: -0.179, z: -0.786 },
    },
    {
        name: "p3",
        skater: { x: 1980.5, y: 52.5, z: -354.9 },
        heading: -59.0,
        camera: { x: 2101.8, y: 131.1, z: -427.7 },
        direction: { x: -0.842, y: -0.187, z: 0.506 },
    },
];

export interface Motion {
    name: string;
    turn: number; // degrees a frame
    dolly: number; // units a frame along the view
    frames: number;
}
// A slow pan (15 degrees in a second), a fast turn (90 degrees in half a second), a dolly forward (90 units in a second).
export const kMotions: Motion[] = [
    { name: "pan", turn: 0.25, dolly: 0, frames: 60 },
    { name: "turn", turn: 3, dolly: 0, frames: 30 },
    { name: "dolly", turn: 0, dolly: 1.5, frames: 60 },
];
export const kTerms = ["off", "lamps", "lamps_specular", "gi", "gi_specular", "sky", "reflections", "fog"];
export const kSettleFrames = 240; // the hold before a move (Ray Reconstruction and the reservoirs settled)
export const kAfterFrames = 30; // the frames after the stop
export const kConvergeFrames = 300; // a held pose's converged picture
export const kEv100 = 0.89; // the references' camera (BO night's metered one at the spots)

export function holdOf(spot: Spot, motion: Motion): Hold {
    const d = normalised(spot.direction);
    return {
        position: spot.camera,
        direction: spot.direction,
        drift: { x: d.x * motion.dolly, y: d.y * motion.dolly, z: d.z * motion.dolly },
        turn: motion.turn,
        frames: motion.frames,
    };
}
function fixed(v: Vec3, digits = 5): string {
    return `${v.x.toFixed(digits)} ${v.y.toFixed(digits)} ${v.z.toFixed(digits)}`;
}
function holdCommand(hold: Hold): string {
    return `camera ${fixed(hold.position)} ${fixed(hold.direction, 6)} ${fixed(hold.drift, 6)} ${hold.turn} ${hold.frames}`;
}
function poseCommand(hold: Hold, k: number): string {
    const pose = cameraPoseAt(hold, k);
    return `camera ${fixed(pose.position)} ${fixed(pose.direction, 6)}`;
}
export function poseNames(motion: Motion): { name: string; k: number }[] {
    return [
        { name: "mid", k: Math.round(motion.frames / 2) },
        { name: "stop", k: motion.frames },
    ];
}

export function planCommands(
    spot: Spot,
    motions: Motion[],
    terms: string[],
    refSamples: number,
    seeds: number,
    tag = "",
): string[] {
    const folder = `mv/${spot.name}${tag}`;
    const out: string[] = [
        "waitfor level bo 600",
        `teleport ${fixed(spot.skater, 1)} ${spot.heading}`,
        "wait 90",
        "console measure on",
    ];
    for (const motion of motions) {
        const hold = holdOf(spot, motion);
        const start = `camera ${fixed(spot.camera)} ${fixed(spot.direction, 6)}`;
        for (const term of terms) {
            // the term's view (its exposure the whole frame's), the hold, then the move and the frames after it
            out.push(`console set rt_debug_view ${term}`, start, `wait ${kSettleFrames}`);
            const every = term === "off" ? 1 : 3;
            const count = Math.ceil((motion.frames + kAfterFrames + 2) / every);
            out.push(`console pt frames ${count} ${folder}/${motion.name}_${term} ${every}`, holdCommand(hold));
            out.push(`wait ${motion.frames + kAfterFrames + 6}`);
        }
        for (const pose of poseNames(motion)) {
            // the converged picture of each term at the pose, then two independent references of the whole
            for (const term of terms) {
                out.push(`console set rt_debug_view ${term}`, poseCommand(hold, pose.k), `wait ${kConvergeFrames}`);
                out.push(`console pt frames 1 ${folder}/conv_${motion.name}_${pose.name}_${term} 1`, "wait 3");
            }
            if (refSamples <= 0) continue; // (--ref-samples 0: a session that reuses another's references)
            out.push("console set rt_debug_view off", poseCommand(hold, pose.k), "wait 30");
            for (const half of ["a", "b"]) {
                const name = `mv_${spot.name}_${motion.name}_${pose.name}_${half}`;
                out.push(
                    `console pt ref ${refSamples} ${kEv100} as ${name}`,
                    "waitfor pt 3000",
                    "console pt off",
                    "wait 5",
                );
            }
        }
    }
    if (seeds > 0) {
        // the first motion replayed: its frames around the middle, each replay with its own random sequences
        const motion = motions[0]!;
        const hold = holdOf(spot, motion);
        const start = `camera ${fixed(spot.camera)} ${fixed(spot.direction, 6)}`;
        out.push("console set rt_debug_view off");
        for (let s = 0; s < seeds; s++) {
            out.push(start, `wait ${kSettleFrames}`, holdCommand(hold));
            out.push(`wait ${Math.round(motion.frames / 2) - 4}`);
            out.push(`console pt frames 8 ${folder}/seed_${motion.name}_${s} 1`, "wait 20");
        }
    }
    out.push("console set rt_debug_view off", "console measure off", "camera off", "wait 5");
    return out;
}

// --- the analysis ------------------------------------------------------------------------------------------------------

interface Series {
    frames: TapFrame[];
    files: string[];
}
function readSeries(folder: string): Series | undefined {
    const info = path.join(folder, "frames.txt");
    if (!fs.existsSync(info)) return undefined;
    const frames = parseFrames(fs.readFileSync(info, "utf8"));
    const files = frames.map((_, i) => path.join(folder, `frame_${String(i + 1).padStart(4, "0")}.exr`));
    return { frames, files };
}
function loadLinear(file: string, preExposure: number): Picture {
    return scaled(readExr(fs.readFileSync(file)), 1 / preExposure);
}

export interface Row {
    motion: string;
    term: string;
    phase: string; // "moving", "after 1", "after 5"...
    frame: number; // the move's frame
    against: string; // "reference" or "converged"
    whole: Error;
    revealed?: Error;
    history?: Error;
}

function crop(d: Float32Array, width: number, x0: number, y0: number, w: number, h: number): Uint8Array {
    const out = new Uint8Array(w * h * 3);
    for (let y = 0; y < h; y++) {
        for (let x = 0; x < w; x++) {
            for (let k = 0; k < 3; k++) {
                out[(y * w + x) * 3 + k] = Math.round(255 * d[((y0 + y) * width + x0 + x) * 3 + k]!);
            }
        }
    }
    return out;
}

function analyse(
    root: string,
    spotName: string,
    jsonOut?: string,
    cropsOut?: string,
    tag = "",
    fixedExposure = 0,
): number {
    const spot = kSpots.find((s) => s.name === spotName);
    if (!spot) {
        print(`no spot ${spotName}`);
        return 2;
    }
    const base = path.join(root, "mv", `${spot.name}${tag}`);
    const rows: Row[] = [];
    // one exposure for every picture of the spot: the full view's median pre-exposure (what the curve got in play)
    const exposures: number[] = [];
    for (const motion of kMotions) {
        const s = readSeries(path.join(base, `${motion.name}_off`));
        if (s) exposures.push(...s.frames.map((f) => f.preExposure));
    }
    exposures.sort((a, b) => a - b);
    const exposure = fixedExposure > 0 ? fixedExposure : (exposures[exposures.length >> 1] ?? 1);
    print(`${spot.name}: display exposure ${exposure.toPrecision(5)} (the full view's median pre-exposure)`);
    for (const motion of kMotions) {
        const hold = holdOf(spot, motion);
        const poses = poseNames(motion);
        const refs = new Map<number, { a: Picture; da: Float32Array; b: Picture; db: Float32Array }>();
        for (const pose of poses) {
            const fa = path.join(root, `mv_${spot.name}_${motion.name}_${pose.name}_a_reference.exr`);
            const fb = path.join(root, `mv_${spot.name}_${motion.name}_${pose.name}_b_reference.exr`);
            if (!fs.existsSync(fa) || !fs.existsSync(fb)) continue;
            const a = readExr(fs.readFileSync(fa));
            const b = readExr(fs.readFileSync(fb));
            refs.set(pose.k, { a, da: display(a, exposure), b, db: display(b, exposure) });
        }
        // (the full view's frames - every one: what each frame's pixels had seen before, for every term's series)
        const full = readSeries(path.join(base, `${motion.name}_off`));
        for (const term of kTerms) {
            const series = readSeries(path.join(base, `${motion.name}_${term}`));
            if (!series) continue;
            const converged = new Map<number, { p: Picture; d: Float32Array }>();
            for (const pose of poses) {
                const c = readSeries(path.join(base, `conv_${motion.name}_${pose.name}_${term}`));
                if (!c?.frames[0]) continue;
                const p = loadLinear(c.files[0]!, c.frames[0].preExposure);
                converged.set(pose.k, { p, d: display(p, exposure) });
            }
            const ks = series.frames.map((f) => moveFrameOf(f, hold));
            let stopSeen = 0;
            for (let i = 0; i < series.frames.length; i++) {
                const k = ks[i]!;
                const atStop = k === motion.frames;
                if (atStop) stopSeen++;
                const phase = atStop ? (stopSeen === 1 ? "stop" : `after ${stopSeen - 1}`) : "moving";
                const wanted =
                    poses.some((p) => p.k === k && !atStop) || (atStop && [1, 2, 6, 16, 31].includes(stopSeen));
                if (!wanted) continue;
                const frame = series.frames[i]!;
                const picture = loadLinear(series.files[i]!, frame.preExposure);
                const shown = display(picture, exposure);
                const stride = full ? Math.max(1, Math.round(full.frames.length / series.frames.length)) : 1;
                const age = full
                    ? historyAge(full.frames, Math.min(i * stride, full.frames.length - 1), 16)
                    : historyAge(series.frames, i, 16);
                const revealedMask = new Uint8Array(age.length);
                const historyMask = new Uint8Array(age.length);
                let revealed = 0;
                for (let p = 0; p < age.length; p++) {
                    if (age[p]! < 8) {
                        revealedMask[p] = 1;
                        revealed++;
                    } else historyMask[p] = 1;
                }
                const conv = converged.get(k);
                if (conv) {
                    rows.push({
                        motion: motion.name,
                        term,
                        phase,
                        frame: k,
                        against: "converged",
                        whole: compare(picture, shown, conv.p, conv.d),
                        ...(revealed > 0
                            ? {
                                  revealed: compare(picture, shown, conv.p, conv.d, revealedMask),
                                  history: compare(picture, shown, conv.p, conv.d, historyMask),
                              }
                            : {}),
                    });
                }
                const ref = refs.get(k);
                if (ref && term === "off") {
                    rows.push({
                        motion: motion.name,
                        term,
                        phase,
                        frame: k,
                        against: "reference",
                        whole: compare(picture, shown, ref.a, ref.da, undefined, ref.b, ref.db),
                        ...(revealed > 0
                            ? {
                                  revealed: compare(picture, shown, ref.a, ref.da, revealedMask, ref.b, ref.db),
                                  history: compare(picture, shown, ref.a, ref.da, historyMask, ref.b, ref.db),
                              }
                            : {}),
                    });
                    if (conv && phase !== "moving" && stopSeen === 31) {
                        rows.push({
                            motion: motion.name,
                            term: "converged",
                            phase: "held",
                            frame: k,
                            against: "reference",
                            whole: compare(conv.p, conv.d, ref.a, ref.da, undefined, ref.b, ref.db),
                        });
                    }
                }
                if (cropsOut && term === "off" && (phase === "moving" || stopSeen === 1)) {
                    fs.mkdirSync(cropsOut, { recursive: true });
                    const png = encodePng({
                        width: frame.width,
                        height: frame.height,
                        rgb: crop(shown, frame.width, 0, 0, frame.width, frame.height),
                    });
                    fs.writeFileSync(
                        path.join(cropsOut, `${spot.name}_${motion.name}_${phase.replace(" ", "")}_k${k}.png`),
                        png,
                    );
                }
            }
        }
        // the replays (--seeds): their mean at each move frame against the reference there, and their spread
        const replays: Series[] = [];
        for (let s = 0; ; s++) {
            const r = readSeries(path.join(base, `seed_${motion.name}_${s}`));
            if (!r) break;
            replays.push(r);
        }
        if (replays.length > 1) {
            const mid = poses[0]!.k;
            const ref = refs.get(mid);
            const pictures: Picture[] = [];
            for (const r of replays) {
                const i = r.frames.findIndex((f) => moveFrameOf(f, hold) === mid);
                if (i >= 0) pictures.push(loadLinear(r.files[i]!, r.frames[i]!.preExposure));
            }
            if (ref && pictures.length > 1) {
                const mean = new Float32Array(pictures[0]!.rgb.length);
                for (const p of pictures) for (let i = 0; i < mean.length; i++) mean[i]! += p.rgb[i]! / pictures.length;
                const meanPicture = { width: pictures[0]!.width, height: pictures[0]!.height, rgb: mean };
                const one = compare(
                    pictures[0]!,
                    display(pictures[0]!, exposure),
                    ref.a,
                    ref.da,
                    undefined,
                    ref.b,
                    ref.db,
                );
                const avg = compare(
                    meanPicture,
                    display(meanPicture, exposure),
                    ref.a,
                    ref.da,
                    undefined,
                    ref.b,
                    ref.db,
                );
                rows.push({
                    motion: motion.name,
                    term: `seed x1`,
                    phase: "moving",
                    frame: mid,
                    against: "reference",
                    whole: one,
                });
                rows.push({
                    motion: motion.name,
                    term: `seeds x${pictures.length}`,
                    phase: "moving",
                    frame: mid,
                    against: "reference",
                    whole: avg,
                });
            }
        }
    }
    const f = (e?: Error): string =>
        e ? `${e.excess.toFixed(2)} f${e.fine.toFixed(2)} b${e.broad.toFixed(2)} (${e.ratio.toFixed(3)})` : "-";
    print(
        "motion term phase frame against | whole: RMSE f<fine> b<broad> (mean ratio) | revealed | history | ref floor",
    );
    for (const r of rows) {
        print(
            `${r.motion} ${r.term} ${r.phase} k${r.frame} vs ${r.against} | ${f(r.whole)} | ${f(r.revealed)} ${
                r.revealed ? `[${r.revealed.pixels}]` : ""
            } | ${f(r.history)} | ${r.whole.floor.toFixed(2)}`,
        );
    }
    if (jsonOut) fs.writeFileSync(jsonOut, JSON.stringify({ spot: spot.name, exposure, rows }, null, 1));
    return 0;
}

// --- Ray Reconstruction's motion vectors, checked -----------------------------------------------------------------------

export interface MotionCheck {
    pixels: number;
    meanError: number; // |the motion given - the motion the depth and the two cameras give|, pixels of the drawn size
    over1: number; // share over a pixel
    warpError: number; // the albedo guide of the frame before, fetched through the motion, against this frame's: mean |d|
    warpOver: number; // share of pixels off by more than 0.05 (of those the warp keeps: not disoccluded)
}

// Where the frame before saw the point a pixel of `now` shows (its depth): 0 - 1 of the drawn picture, minus where it
// is now - the motion vector a static world has. Direct3D's pixel centres (integers), the jitter taken out, as the
// upscaler's preparation pass makes it for the pixels no draw wrote.
export function cameraMotion(now: TapFrame, before: TapFrame, x: number, y: number, depth: number): [number, number] {
    const w = now.width;
    const h = now.height;
    const z = now.p32 / Math.max(depth - now.p22, 1e-7);
    // the pixel's place without the jitter (the projection was moved by it): ndc of (pixel - jitter)
    const nx = ((x - now.jitter[0]) / w) * 2 - 1;
    const ny = 1 - ((y - now.jitter[1]) / h) * 2;
    const vx = (nx * z) / now.p00;
    const vy = (ny * z) / now.p11;
    const p = {
        x: now.position.x + now.right.x * vx + now.up.x * vy + now.forward.x * z,
        y: now.position.y + now.right.y * vx + now.up.y * vy + now.forward.y * z,
        z: now.position.z + now.right.z * vx + now.up.z * vy + now.forward.z * z,
    };
    const d = { x: p.x - before.position.x, y: p.y - before.position.y, z: p.z - before.position.z };
    const bz = dot(d, before.forward);
    const bx = (dot(d, before.right) / bz) * before.p00;
    const by = (dot(d, before.up) / bz) * before.p11;
    const beforeU = bx * 0.5 + 0.5;
    const beforeV = 0.5 - by * 0.5;
    return [beforeU - (x - now.jitter[0]) / w, beforeV - (y - now.jitter[1]) / h];
}

export function checkMotion(
    now: TapFrame,
    before: TapFrame,
    depth: Picture,
    motion: Picture,
    albedoNow: Picture,
    albedoBefore: Picture,
): MotionCheck {
    const w = depth.width;
    const h = depth.height;
    const frameNow = { ...now, width: w, height: h };
    const frameBefore = { ...before, width: w, height: h };
    let n = 0;
    let sum = 0;
    let over = 0;
    let warped = 0;
    let warpSum = 0;
    let warpOver = 0;
    for (let y = 0; y < h; y++) {
        for (let x = 0; x < w; x++) {
            const i = (y * w + x) * 3;
            const d = depth.rgb[i]!;
            const [ex, ey] = cameraMotion(frameNow, frameBefore, x, y, d);
            const mx = motion.rgb[i]!;
            const my = motion.rgb[i + 1]!;
            const err = Math.hypot((mx - ex) * w, (my - ey) * h);
            if (Number.isFinite(err)) {
                n++;
                sum += err;
                if (err > 1) over++;
            }
            // the albedo of the frame before where the motion says the pixel was
            const bx = Math.round(x + mx * w);
            const by = Math.round(y + my * h);
            if (bx < 0 || by < 0 || bx >= w || by >= h || d >= 1) continue;
            const j = (by * w + bx) * 3;
            const diff =
                (Math.abs(albedoNow.rgb[i]! - albedoBefore.rgb[j]!) +
                    Math.abs(albedoNow.rgb[i + 1]! - albedoBefore.rgb[j + 1]!) +
                    Math.abs(albedoNow.rgb[i + 2]! - albedoBefore.rgb[j + 2]!)) /
                3;
            warped++;
            warpSum += diff;
            if (diff > 0.05) warpOver++;
        }
    }
    return {
        pixels: n,
        meanError: n ? sum / n : 0,
        over1: n ? over / n : 0,
        warpError: warped ? warpSum / warped : 0,
        warpOver: warped ? warpOver / warped : 0,
    };
}

function motionCommand(folder: string): number {
    const frames = parseFrames(fs.readFileSync(path.join(folder, "frames.txt"), "utf8"));
    const input = (k: number, name: string): string =>
        path.join(folder, `inputs_${String(k).padStart(4, "0")}_${name}.exr`);
    for (let k = 2; k <= frames.length; k++) {
        if (!fs.existsSync(input(k, "motion")) || !fs.existsSync(input(k - 1, "albedo"))) continue;
        const r = checkMotion(
            frames[k - 1]!,
            frames[k - 2]!,
            readExr(fs.readFileSync(input(k, "depth"))),
            readExr(fs.readFileSync(input(k, "motion"))),
            readExr(fs.readFileSync(input(k, "albedo"))),
            readExr(fs.readFileSync(input(k - 1, "albedo"))),
        );
        // the light Ray Reconstruction was given and the light it gave back, each over the frame's pre-exposure: equal
        // means unless its history is held at another exposure
        const meanLuma = (p: Picture): number => {
            let s = 0;
            for (let i = 0; i < p.rgb.length; i += 3) s += luma(p.rgb[i]!, p.rgb[i + 1]!, p.rgb[i + 2]!);
            return s / (p.width * p.height);
        };
        const frame = frames[k - 1]!;
        const given = meanLuma(readExr(fs.readFileSync(input(k, "colour")))) / frame.preExposure;
        const output = path.join(folder, `frame_${String(k).padStart(4, "0")}.exr`);
        const shown = fs.existsSync(output) ? meanLuma(readExr(fs.readFileSync(output))) / frame.preExposure : 0;
        print(
            `frame ${k}: motion vs the cameras' ${r.meanError.toFixed(3)} px (${(100 * r.over1).toFixed(2)} % over 1 px); ` +
                `the albedo warped ${r.warpError.toFixed(4)} (${(100 * r.warpOver).toFixed(2)} % over 0.05); ` +
                `pre-exposure ${frame.preExposure.toPrecision(5)}, light given ${given.toPrecision(4)} -> out ${shown.toPrecision(4)} ` +
                `(x ${(given > 0 ? shown / given : 0).toFixed(3)})`,
        );
    }
    return 0;
}

function compareCommand(a: string, b: string, exposure: number, grid: string): number {
    const pa = readExr(fs.readFileSync(a));
    const pb = readExr(fs.readFileSync(b));
    const da = display(pa, exposure);
    const db = display(pb, exposure);
    const whole = compare(pa, da, pb, db);
    print(
        `whole: mean ${whole.meanA.toPrecision(4)} / ${whole.meanB.toPrecision(4)} = x ${whole.ratio.toFixed(3)}, display RMSE ${whole.rmse.toFixed(2)}`,
    );
    const [cols, rowsCount] = grid.split("x").map(Number) as [number, number];
    for (let r = 0; r < rowsCount; r++) {
        const cells: string[] = [];
        for (let c = 0; c < cols; c++) {
            const mask = new Uint8Array(pa.width * pa.height);
            const x0 = Math.floor((c * pa.width) / cols);
            const x1 = Math.floor(((c + 1) * pa.width) / cols);
            const y0 = Math.floor((r * pa.height) / rowsCount);
            const y1 = Math.floor(((r + 1) * pa.height) / rowsCount);
            for (let y = y0; y < y1; y++) for (let x = x0; x < x1; x++) mask[y * pa.width + x] = 1;
            cells.push(compare(pa, da, pb, db, mask).ratio.toFixed(2).padStart(5));
        }
        print(cells.join(" "));
    }
    return 0;
}

// --- (round BIAS) the light term by term against the reference, the camera held ---------------------------------------

// The realtime views captured converged (each holds the surfaces' own emission: the `emission` view is that alone).
export const kTermViews = [
    "off",
    "lamps",
    "lamps_specular",
    "gi",
    "gi_specular",
    "sky",
    "reflections",
    "fog",
    "front",
    "emission",
];
// The references: a name and its `pt ref` words. `b0_*` what the camera sees emit (no bounce), `*_direct` one bounce.
export const kTermReferences: { name: string; words: string }[] = [
    { name: "full", words: "" },
    { name: "full2", words: "" },
    { name: "direct", words: "direct" },
    { name: "b2", words: "bounces 2" },
    { name: "b0_emit", words: "only emit bounces 0" },
    { name: "b0_distant", words: "only disc,sky bounces 0" },
    { name: "local_direct", words: "only emit,lamps direct" },
    { name: "emit_direct", words: "only emit direct" },
    { name: "lamps_direct", words: "only lamps direct" },
    { name: "distant_direct", words: "only disc,sky direct" },
];
export const kTermConvergeFrames = 400;

export function planTermCommands(spot: Spot, refSamples: number, tag = ""): string[] {
    const folder = `bias/${spot.name}${tag}`;
    const pose = `camera ${fixed(spot.camera)} ${fixed(spot.direction, 6)}`;
    const out: string[] = [
        "waitfor level bo 600",
        `teleport ${fixed(spot.skater, 1)} ${spot.heading}`,
        "wait 90",
        "console measure on",
        pose,
    ];
    for (const view of kTermViews) {
        out.push(`console set rt_debug_view ${view}`, `wait ${kTermConvergeFrames}`);
        out.push(`console pt frames 1 ${folder}/rt_${view} 1`, "wait 3");
    }
    out.push("console set rt_debug_view off", "wait 60");
    for (const ref of kTermReferences) {
        const words = ref.words ? ` ${ref.words}` : "";
        out.push(
            `console pt ref ${refSamples} ${kEv100}${words} as bias_${spot.name}${tag}_${ref.name}`,
            "waitfor pt 3000",
            "console pt off",
            "wait 5",
        );
    }
    out.push("console measure off", "camera off", "wait 5");
    return out;
}

// A picture's mean luma over the cells of a grid (the last: the whole picture).
export function cellMeans(p: Picture, cols: number, rows: number): number[] {
    const sums = new Array<number>(cols * rows + 1).fill(0);
    const counts = new Array<number>(cols * rows + 1).fill(0);
    for (let y = 0; y < p.height; y++) {
        const r = Math.min(rows - 1, Math.floor((y * rows) / p.height));
        for (let x = 0; x < p.width; x++) {
            const c = Math.min(cols - 1, Math.floor((x * cols) / p.width));
            const i = (y * p.width + x) * 3;
            const l = luma(p.rgb[i]!, p.rgb[i + 1]!, p.rgb[i + 2]!);
            if (!Number.isFinite(l)) continue;
            sums[r * cols + c]! += l;
            counts[r * cols + c]!++;
            sums[cols * rows]! += l;
            counts[cols * rows]!++;
        }
    }
    return sums.map((s, i) => (counts[i]! > 0 ? s / counts[i]! : 0));
}

// The comparisons: each realtime term (its view less the emission every view holds) against the reference's same light.
export interface TermRow {
    term: string;
    realtime: number[];
    reference: number[];
}
export function termRows(rt: Map<string, number[]>, ref: Map<string, number[]>): TermRow[] {
    const cells = (rt.get("off") ?? ref.get("full") ?? []).length;
    const pick = (m: Map<string, number[]>, name: string): number[] =>
        m.get(name) ?? new Array<number>(cells).fill(Number.NaN);
    const combine = (parts: [Map<string, number[]>, string, number][]): number[] =>
        Array.from({ length: cells }, (_, i) =>
            parts.reduce((s, [m, name, k]) => (k === 0 ? s : s + k * pick(m, name)[i]!), 0),
        );
    const fullMean = combine([
        [ref, "full", 0.5],
        [ref, "full2", 0.5],
    ]);
    return [
        { term: "whole", realtime: pick(rt, "off"), reference: ref.has("full2") ? fullMean : pick(ref, "full") },
        // (what the camera sees shine itself: the surfaces' emission and the sky - the realtime view holds both)
        {
            term: "emission + sky seen",
            realtime: pick(rt, "emission"),
            reference: combine([
                [ref, "b0_emit", 1],
                [ref, "b0_distant", 1],
            ]),
        },
        {
            term: "  emitters seen",
            realtime: new Array<number>(cells).fill(Number.NaN),
            reference: pick(ref, "b0_emit"),
        },
        {
            term: "local direct (DI)",
            realtime: combine([
                [rt, "lamps", 1],
                [rt, "lamps_specular", 1],
                [rt, "emission", -2],
            ]),
            reference: combine([
                [ref, "local_direct", 1],
                [ref, "b0_emit", -1],
            ]),
        },
        {
            term: "  emitters direct",
            realtime: new Array<number>(cells).fill(Number.NaN),
            reference: combine([
                [ref, "emit_direct", 1],
                [ref, "b0_emit", -1],
            ]),
        },
        {
            term: "  lamps direct",
            realtime: new Array<number>(cells).fill(Number.NaN),
            reference: pick(ref, "lamps_direct"),
        },
        {
            term: "disc + sky direct",
            realtime: combine([
                [rt, "sky", 1],
                [rt, "emission", -1],
            ]),
            reference: combine([
                [ref, "distant_direct", 1],
                [ref, "b0_distant", -1],
            ]),
        },
        {
            term: "indirect (GI)",
            realtime: combine([
                [rt, "gi", 1],
                [rt, "gi_specular", 1],
                [rt, "emission", -2],
            ]),
            reference: combine([
                [ref, "full", ref.has("full2") ? 0.5 : 1],
                [ref, "full2", ref.has("full2") ? 0.5 : 0],
                [ref, "direct", -1],
            ]),
        },
        {
            // (the reflections pass is the frame's second specular estimator - the game's shine, glass and water: its
            // light is indirect too, the reference's paths hold it)
            term: "  + reflections",
            realtime: combine([
                [rt, "gi", 1],
                [rt, "gi_specular", 1],
                [rt, "reflections", 1],
                [rt, "emission", -3],
            ]),
            reference: combine([
                [ref, "full", ref.has("full2") ? 0.5 : 1],
                [ref, "full2", ref.has("full2") ? 0.5 : 0],
                [ref, "direct", -1],
            ]),
        },
        {
            term: "  one bounce",
            realtime: new Array<number>(cells).fill(Number.NaN),
            reference: combine([
                [ref, "b2", 1],
                [ref, "direct", -1],
            ]),
        },
        {
            term: "reflections",
            realtime: combine([
                [rt, "reflections", 1],
                [rt, "emission", -1],
            ]),
            reference: new Array<number>(cells).fill(Number.NaN),
        },
        {
            term: "fog",
            realtime: combine([
                [rt, "fog", 1],
                [rt, "emission", -1],
            ]),
            reference: new Array<number>(cells).fill(Number.NaN),
        },
        {
            term: "front",
            realtime: combine([
                [rt, "front", 1],
                [rt, "emission", -1],
            ]),
            reference: new Array<number>(cells).fill(Number.NaN),
        },
        {
            term: "reference noise",
            realtime: pick(ref, "full"),
            reference: pick(ref, "full2"),
        },
    ];
}

// A linear picture in the global unit: a `pt frames` frame (frames.txt beside it) over its pre-exposure, a reference as
// it is.
function globalUnit(file: string): Picture {
    const list = path.join(path.dirname(file), "frames.txt");
    if (!fs.existsSync(list)) return readExr(fs.readFileSync(file));
    const k = Number(/frame_(\d+)\.exr$/.exec(file)?.[1] ?? "1");
    const frame = parseFrames(fs.readFileSync(list, "utf8"))[k - 1];
    return frame ? loadLinear(file, frame.preExposure) : readExr(fs.readFileSync(file));
}

// Pictures side by side (each through the same exposure, ACES + sRGB) as one PNG - e.g. realtime | reference.
function pairCommand(files: string[], out: string, exposure: number): number {
    const pictures = files.map(globalUnit);
    const width = pictures.reduce((s, p) => s + p.width, 0);
    const height = Math.max(...pictures.map((p) => p.height));
    const rgb = new Uint8Array(width * height * 3);
    let x0 = 0;
    for (const p of pictures) {
        const shown = crop(display(p, exposure), p.width, 0, 0, p.width, p.height);
        for (let y = 0; y < p.height; y++)
            rgb.set(shown.subarray(y * p.width * 3, (y + 1) * p.width * 3), (y * width + x0) * 3);
        x0 += p.width;
    }
    fs.writeFileSync(out, encodePng({ width, height, rgb }));
    print(`wrote ${out} (${width} x ${height})`);
    return 0;
}

function termsAnalyse(root: string, spotName: string, tag: string, grid: string, json?: string): number {
    const [cols, rows] = grid.split("x").map(Number) as [number, number];
    const rt = new Map<string, number[]>();
    const ref = new Map<string, number[]>();
    for (const view of kTermViews) {
        const file = path.join(root, "bias", `${spotName}${tag}`, `rt_${view}`, "frame_0001.exr");
        if (fs.existsSync(file)) rt.set(view, cellMeans(globalUnit(file), cols, rows));
    }
    for (const r of kTermReferences) {
        const file = path.join(root, `bias_${spotName}${tag}_${r.name}_reference.exr`);
        if (fs.existsSync(file)) ref.set(r.name, cellMeans(readExr(fs.readFileSync(file)), cols, rows));
    }
    const rowsOut = termRows(rt, ref);
    const n = cols * rows;
    const f = (v: number): string => (Number.isFinite(v) ? v.toPrecision(3) : "-");
    print(
        `${spotName}${tag}: realtime / reference (mean luma, global unit) - whole picture, then the ${grid} cells' ratios`,
    );
    for (const row of rowsOut) {
        const a = row.realtime[n]!;
        const b = row.reference[n]!;
        const ratio = Number.isFinite(a) && Number.isFinite(b) && b !== 0 ? (a / b).toFixed(3) : "-";
        print(`${row.term.padEnd(20)} ${f(a).padStart(9)} ${f(b).padStart(9)}  x ${ratio}`);
        if (Number.isFinite(a) && Number.isFinite(b)) {
            for (let r = 0; r < rows; r++) {
                const cells: string[] = [];
                for (let c = 0; c < cols; c++) {
                    const i = r * cols + c;
                    const x = row.reference[i]! !== 0 ? row.realtime[i]! / row.reference[i]! : Number.NaN;
                    cells.push((Number.isFinite(x) ? x.toFixed(2) : "-").padStart(6));
                }
                print(`${"".padEnd(24)}${cells.join("")}`);
            }
        }
    }
    if (json) fs.writeFileSync(json, JSON.stringify({ spot: spotName + tag, grid, rows: rowsOut }, null, 1));
    return 0;
}

function main(): number {
    const { values, positionals } = parseCli(USAGE, {
        options: {
            motions: { type: "string", default: "pan,turn,dolly" },
            terms: { type: "string", default: kTerms.join(",") },
            "ref-samples": { type: "string", default: "2048" },
            seeds: { type: "string", default: "0" },
            out: { type: "string" },
            tag: { type: "string", default: "" },
            display: { type: "string", default: "0" },
            json: { type: "string" },
            crops: { type: "string" },
            exposure: { type: "string", default: "20000" },
            grid: { type: "string", default: "6x4" },
        },
        allowPositionals: true,
    });
    const [verb, ...rest] = positionals;
    if (verb === "plan" && rest.length === 1) {
        const spot = kSpots.find((s) => s.name === rest[0]);
        if (!spot) {
            print(`no spot ${rest[0]} (p1, p2, p3)`);
            return 2;
        }
        const wanted = (values.motions as string).split(",");
        const motions = kMotions.filter((m) => wanted.includes(m.name));
        const commands = planCommands(
            spot,
            motions,
            (values.terms as string).split(","),
            Number(values["ref-samples"]),
            Number(values.seeds),
            values.tag as string,
        );
        const text = commands.join("\n") + "\n";
        if (values.out) fs.writeFileSync(values.out as string, text);
        else print(text);
        return 0;
    }
    if (verb === "analyse" && rest.length === 2)
        return analyse(
            rest[0]!,
            rest[1]!,
            values.json as string | undefined,
            values.crops as string | undefined,
            values.tag as string,
            Number(values.display),
        );
    if (verb === "terms" && rest.length === 1) {
        const spot = kSpots.find((s) => s.name === rest[0]);
        if (!spot) {
            print(`no spot ${rest[0]} (p1, p2, p3)`);
            return 2;
        }
        const samples = values["ref-samples"] === "2048" ? 1024 : Number(values["ref-samples"]);
        const text = planTermCommands(spot, samples, values.tag as string).join("\n") + "\n";
        if (values.out) fs.writeFileSync(values.out as string, text);
        else print(text);
        return 0;
    }
    if (verb === "terms-analyse" && rest.length === 2)
        return termsAnalyse(
            rest[0]!,
            rest[1]!,
            values.tag as string,
            values.grid as string,
            values.json as string | undefined,
        );
    if (verb === "pair" && rest.length >= 2)
        return pairCommand(rest.slice(0, -1), rest[rest.length - 1]!, Number(values.exposure));
    if (verb === "motion" && rest.length === 1) return motionCommand(rest[0]!);
    if (verb === "compare" && rest.length === 2)
        return compareCommand(rest[0]!, rest[1]!, Number(values.exposure), values.grid as string);
    print(USAGE);
    return 2;
}

if (isMain(import.meta.url)) await runMain(() => main());
