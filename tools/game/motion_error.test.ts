import assert from "node:assert/strict";
import { test } from "node:test";
import {
    cameraMotion,
    cameraPoseAt,
    cellMeans,
    compare,
    display,
    halfToFloat,
    historyAge,
    holdOf,
    kMotions,
    kSpots,
    moveFrameOf,
    type Picture,
    parseFrames,
    planCommands,
    planTermCommands,
    readExr,
    type TapFrame,
    termRows,
} from "./motion_error.ts";

// An uncompressed scanline EXR as the game writes it (vulkan_path_reference.cpp exr_half_bytes): B, G, R halves.
function exr(width: number, height: number, halves: number[][]): Buffer {
    const parts: Buffer[] = [];
    const int = (v: number): Buffer => {
        const b = Buffer.alloc(4);
        b.writeInt32LE(v);
        return b;
    };
    const str = (s: string): Buffer => Buffer.from(`${s}\0`, "latin1");
    const attribute = (name: string, type: string, value: Buffer): void => {
        parts.push(str(name), str(type), int(value.length), value);
    };
    parts.push(int(20000630), int(2));
    const channels = Buffer.concat([
        ...["B", "G", "R"].map((c) => Buffer.concat([str(c), int(1), int(0), int(1), int(1)])),
        Buffer.from([0]),
    ]);
    attribute("channels", "chlist", channels);
    attribute("compression", "compression", Buffer.from([0]));
    attribute("dataWindow", "box2i", Buffer.concat([int(0), int(0), int(width - 1), int(height - 1)]));
    parts.push(Buffer.from([0]));
    const header = Buffer.concat(parts);
    const lineBytes = width * 3 * 2;
    const offsets = Buffer.alloc(height * 8);
    const first = header.length + offsets.length;
    for (let y = 0; y < height; y++) offsets.writeBigUInt64LE(BigInt(first + y * (8 + lineBytes)), y * 8);
    const lines: Buffer[] = [];
    for (let y = 0; y < height; y++) {
        const line = Buffer.alloc(8 + lineBytes);
        line.writeInt32LE(y, 0);
        line.writeInt32LE(lineBytes, 4);
        let p = 8;
        for (const channel of [2, 1, 0]) {
            for (let x = 0; x < width; x++) {
                line.writeUInt16LE(halves[y * width + x]![channel]!, p);
                p += 2;
            }
        }
        lines.push(line);
    }
    return Buffer.concat([header, offsets, ...lines]);
}

test("halves decoded", () => {
    assert.equal(halfToFloat(0x3c00), 1);
    assert.equal(halfToFloat(0xc000), -2);
    assert.equal(halfToFloat(0x0001), 2 ** -24);
    assert.equal(halfToFloat(0x7bff), 65504);
});

test("an EXR of halves read back as R, G, B", () => {
    // pixel 0: R 1, G 2, B 0.5; pixel 1: R 0, G 0, B 1
    const picture = readExr(
        exr(2, 1, [
            [0x3c00, 0x4000, 0x3800],
            [0, 0, 0x3c00],
        ]),
    );
    assert.deepEqual([picture.width, picture.height], [2, 1]);
    assert.deepEqual(Array.from(picture.rgb), [1, 2, 0.5, 0, 0, 1]);
});

test("the camera move is the game's: a turn about the up, a dolly along the view, then held", () => {
    const hold = {
        position: { x: 0, y: 0, z: 0 },
        direction: { x: 0, y: 0, z: 1 },
        drift: { x: 0, y: 0, z: 0 },
        turn: 3,
        frames: 30,
    };
    const quarter = cameraPoseAt(hold, 30).direction;
    assert.ok(Math.abs(quarter.x - 1) < 1e-9 && Math.abs(quarter.z) < 1e-9); // heading 90: +x
    assert.deepEqual(cameraPoseAt(hold, 45), cameraPoseAt(hold, 30));
    const dolly = holdOf(kSpots[0]!, kMotions[2]!);
    const end = cameraPoseAt(dolly, 60).position;
    const moved = Math.hypot(end.x - kSpots[0]!.camera.x, end.y - kSpots[0]!.camera.y, end.z - kSpots[0]!.camera.z);
    assert.ok(Math.abs(moved - 90) < 1e-6);
});

function frameAt(heading: number, width = 64, height = 36): TapFrame {
    const r = (heading * Math.PI) / 180;
    return {
        index: 0,
        width,
        height,
        preExposure: 1,
        position: { x: 0, y: 0, z: 0 },
        forward: { x: Math.sin(r), y: 0, z: Math.cos(r) },
        right: { x: Math.cos(r), y: 0, z: -Math.sin(r) },
        up: { x: 0, y: 1, z: 0 },
        p00: 1,
        p11: 16 / 9,
        p22: 1.001,
        p32: -1.001,
        jitter: [0, 0],
    };
}

test("a turn reveals the side it turns to", () => {
    // turning right (heading growing) 10 degrees a frame with a 90-degree view: the right edge is new
    const frames = [0, 10, 20].map((h) => frameAt(h));
    const age = historyAge(frames, 2, 4);
    const w = frames[0]!.width;
    const row = 18 * w;
    assert.equal(age[row + w - 1], 0); // the right edge: never seen before
    assert.ok(age[row]! >= 2); // the left edge: seen by the frames before
});

test("a tap frame's move frame found by its pose", () => {
    const hold = holdOf(kSpots[1]!, kMotions[0]!);
    const pose = cameraPoseAt(hold, 17);
    const frame = { ...frameAt(0), position: pose.position, forward: pose.direction };
    assert.equal(moveFrameOf(frame, hold), 17);
});

test("frames.txt parsed", () => {
    const [f] = parseFrames("# header\n3 99 1280 720 19000 1 2 3 0 0 -1 1.2 2.1 0.25 -0.5 1 0 0 0 1 0 1.001 -1.001\n");
    assert.equal(f!.index, 3);
    assert.equal(f!.preExposure, 19000);
    assert.deepEqual(f!.jitter, [0.25, -0.5]);
    assert.equal(f!.p32, -1.001);
    assert.deepEqual(f!.right, { x: 1, y: 0, z: 0 });
});

test("a still camera's motion is zero; a turn's matches the image shift", () => {
    const a = frameAt(0, 100, 100);
    const [sx, sy] = cameraMotion(a, a, 50, 50, 0.5);
    assert.ok(Math.abs(sx) < 1e-9 && Math.abs(sy) < 1e-9);
    // turned right: the frame before saw the centre's point further right
    const b = frameAt(-5, 100, 100);
    const [tx] = cameraMotion(a, b, 50, 50, 0.5);
    assert.ok(tx > 0);
});

test("the error: none for the same picture, the mean ratio, the reference's noise taken out", () => {
    const flat = (v: number): Picture => ({ width: 2, height: 2, rgb: new Float32Array(12).fill(v) });
    const a = flat(0.5);
    const same = compare(a, display(a, 1), a, display(a, 1));
    assert.equal(same.rmse, 0);
    assert.equal(same.ratio, 1);
    const half = compare(flat(0.25), display(flat(0.25), 1), a, display(a, 1));
    assert.ok(Math.abs(half.ratio - 0.5) < 1e-6);
    // two references either side of a: their noise is the whole of the error
    const lo = flat(0.4);
    const hi = flat(0.6);
    const e = compare(a, display(a, 1), lo, display(lo, 1), undefined, hi, display(hi, 1));
    assert.ok(e.floor > 0 && e.excess <= e.rmse);
});

test("the plan holds, moves, captures and references every motion", () => {
    const lines = planCommands(kSpots[0]!, kMotions, ["off", "gi"], 64, 2);
    assert.equal(lines[0], "waitfor level bo 600");
    assert.ok(lines.includes("console measure on"));
    assert.equal(lines.filter((l) => l.startsWith("console pt ref 64")).length, kMotions.length * 2 * 2);
    assert.ok(lines.some((l) => /^camera .* 3 30$/.test(l)));
    assert.equal(lines.filter((l) => l.startsWith("console pt frames 8 mv/p1/seed_pan_")).length, 2);
    assert.equal(lines.at(-3), "console measure off");
});

test("terms: a picture's cell means and the whole", () => {
    const p: Picture = { width: 4, height: 2, rgb: new Float32Array(4 * 2 * 3) };
    for (let i = 0; i < 8; i++) p.rgb.fill(i < 4 ? 0 : 1, i * 3, i * 3 + 3); // the top row 0, the bottom 1
    const m = cellMeans(p, 2, 2);
    assert.equal(m.length, 5);
    assert.ok(Math.abs(m[0]!) < 1e-9 && Math.abs(m[2]! - 1) < 1e-6 && Math.abs(m[4]! - 0.5) < 1e-6);
});

test("terms: each realtime view less the emission it holds, against the reference's same light", () => {
    const rt = new Map<string, number[]>([
        ["off", [10]],
        ["lamps", [5]],
        ["lamps_specular", [3]],
        ["emission", [1]],
        ["gi", [2]],
        ["gi_specular", [1.5]],
        ["sky", [1.2]],
        ["reflections", [1.5]],
    ]);
    const ref = new Map<string, number[]>([
        ["full", [12]],
        ["direct", [9]],
        ["b0_emit", [0.5]],
        ["b0_distant", [0.1]],
        ["local_direct", [6.5]],
        ["distant_direct", [0.3]],
    ]);
    const rows = new Map(termRows(rt, ref).map((r) => [r.term.trim(), r]));
    assert.deepEqual(rows.get("local direct (DI)")!.realtime, [6]); // 5 + 3 - 2 x 1
    assert.deepEqual(rows.get("local direct (DI)")!.reference, [6]); // 6.5 - 0.5
    assert.deepEqual(rows.get("indirect (GI)")!.realtime, [1.5]);
    assert.deepEqual(rows.get("indirect (GI)")!.reference, [3]);
    assert.ok(Math.abs(rows.get("emission + sky seen")!.reference[0]! - 0.6) < 1e-9);
    assert.ok(Math.abs(rows.get("disc + sky direct")!.realtime[0]! - 0.2) < 1e-9);
});

test("terms: the session holds each view, then the references", () => {
    const lines = planTermCommands(kSpots[0]!, 256, "_t");
    assert.ok(lines.includes("console set rt_debug_view emission"));
    assert.ok(lines.some((l) => l.startsWith("console pt frames 1 bias/p1_t/rt_lamps ")));
    assert.ok(lines.includes("console pt ref 256 0.89 only emit bounces 0 as bias_p1_t_b0_emit"));
    assert.equal(lines.filter((l) => l === "waitfor pt 3000").length, 10);
});
