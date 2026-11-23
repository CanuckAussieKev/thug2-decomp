import assert from "node:assert/strict";
import { test } from "node:test";
import { heatColour, kHeatFull, speckStats } from "./speck_count.ts";

// A 9 x 9 surface of 10 frames at 40: one pixel lit to 200 in every frame (steady), another in every third frame
// (flickering: its own median the surface's).
const rect = { x: 0, y: 0, w: 9, h: 9 };
const frames = Array.from({ length: 10 }, (_, k) => {
    const f = new Float32Array(81).fill(40);
    f[2 * 9 + 2] = 200;
    if (k % 3 === 0) f[6 * 9 + 6] = 200;
    return f;
});

test("a steady glint and a flickering speck told apart", () => {
    const s = speckStats(frames, rect, 30);
    assert.equal(s.frames, 10);
    assert.equal(s.steady, 1);
    assert.equal(s.flickering, 0.4);
    assert.ok(s.std > 0);
});

test("an even surface has none", () => {
    const even = frames.map(() => new Float32Array(81).fill(40));
    const s = speckStats(even, rect, 30);
    assert.equal(s.flickering + s.steady, 0);
    assert.equal(s.std, 0);
});

test("the mean and the per-pixel map", () => {
    const s = speckStats(frames, rect, 30);
    assert.ok(Math.abs(s.mean - (40 + 160 / 81 + (160 * 0.4) / 81)) < 1e-3);
    assert.equal(s.stdMap[2 * 9 + 2], 0);
    assert.ok(s.stdMap[6 * 9 + 6]! > 70);
});

test("the heat map's ramp", () => {
    assert.deepEqual(heatColour(0), [0, 0, 0]);
    assert.deepEqual(heatColour(kHeatFull), [255, 255, 255]);
    assert.deepEqual(heatColour(kHeatFull / 3), [255, 0, 0]);
});

test("(round NOISE) a region breathing together: its mean's deviation and the blocks' equal the pixels'", () => {
    const r = { x: 0, y: 0, w: 32, h: 32 };
    const breathing = Array.from({ length: 8 }, (_, k) => new Float32Array(32 * 32).fill(k % 2 === 0 ? 40 : 50));
    const s = speckStats(breathing, r, 30);
    assert.ok(Math.abs(s.regionStd - 5) < 1e-6);
    assert.ok(Math.abs(s.regionStep - 10) < 1e-6);
    assert.ok(Math.abs(s.blockStd - s.std) < 1e-6);
});
