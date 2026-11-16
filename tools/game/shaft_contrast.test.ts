// NOT ORIGINAL - tests of tools/game/shaft_contrast.ts's measures (no game data).

import assert from "node:assert/strict";
import { test } from "node:test";
import { airLight, shaftStats } from "./shaft_contrast.ts";

const width = 64;
const height = 32;
const whole = { x: 0, y: 0, w: width, h: height };

test("an even veil has no contrast and no structure", () => {
    const air = new Float32Array(width * height).fill(40);
    const stats = shaftStats(air, width, height, whole, 4);
    assert.equal(stats.veil, 40);
    assert.equal(stats.contrast, 0);
    assert.ok(stats.structure < 1e-6);
});

test("beams with dark gaps between them have contrast 1 and structure", () => {
    const air = new Float32Array(width * height);
    for (let y = 0; y < height; y++)
        for (let x = 0; x < width; x++) air[y * width + x] = Math.floor(x / 8) % 2 ? 60 : 0;
    const stats = shaftStats(air, width, height, whole, 8);
    assert.equal(stats.beam, 60);
    assert.equal(stats.gap, 0);
    assert.equal(stats.contrast, 1);
    assert.ok(stats.structure > 0.3);
});

test("the air's light is the luma of on less off; where it only dims, the gap counts as 0", () => {
    const on = { width: 2, height: 1, rgb: new Uint8Array([100, 100, 100, 10, 10, 10]) };
    const off = { width: 2, height: 1, rgb: new Uint8Array([50, 50, 50, 20, 20, 20]) };
    const air = airLight(on, off);
    assert.ok(Math.abs((air[0] ?? 0) - 50) < 1e-3);
    assert.ok(Math.abs((air[1] ?? 0) + 10) < 1e-3);
    assert.equal(shaftStats(air, 2, 1, { x: 0, y: 0, w: 2, h: 1 }, 1).gap, 0);
});
