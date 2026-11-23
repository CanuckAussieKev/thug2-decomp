import assert from "node:assert/strict";
import { test } from "node:test";
import { autocorrelation, seriesResult, smoothed, wander } from "./still_series.ts";

test("a box mean over the window, none past the ends", () => {
    assert.deepEqual(smoothed([1, 2, 3, 4, 5], 3), [2, 3, 4]);
    assert.deepEqual(smoothed([1, 2, 3], 1), [1, 2, 3]);
});

test("a constant series doesn't wander; a slow wave does", () => {
    assert.deepEqual(wander([2, 2, 2]), { range: 0, std: 0 });
    const w = wander([0.9, 1.0, 1.1, 1.0]);
    assert.ok(Math.abs(w.range - 0.2) < 1e-9);
});

test("a slow wave keeps its error from sample to sample, alternating noise doesn't", () => {
    const slow = Array.from({ length: 64 }, (_, i) => 1 + 0.1 * Math.sin((2 * Math.PI * i) / 32));
    const fast = Array.from({ length: 64 }, (_, i) => (i % 2 === 0 ? 1.1 : 0.9));
    assert.ok(autocorrelation(slow, 1) > 0.9);
    assert.ok(autocorrelation(fast, 1) < -0.9);
    assert.equal(autocorrelation([1, 1, 1, 1], 1), 0);
});

test("the cells' result: a dark cell left out, the whole frame's mean kept", () => {
    // two cells and the whole: cell 0 a slow wave, cell 1 dark (under 2 % of the frame's mean)
    const frames = Array.from({ length: 32 }, (_, i) => {
        const a = 1 + 0.1 * Math.sin((2 * Math.PI * i) / 16);
        return [a, 0.001, a / 2];
    });
    const r = seriesResult(frames, 3);
    assert.equal(r.cells, 1);
    assert.equal(r.frames, 32);
    assert.ok(r.correlation[0]! > 0.8);
    assert.ok(Math.abs(r.wholeMean - 0.5) < 0.01);
});
