import assert from "node:assert/strict";
import { test } from "node:test";
import { settleCurve } from "./settle_curve.ts";

// A 9 x 9 surface of 40 frames at 40: in the first 3 frames two isolated pixels at 200 (disocclusion sparkle), then
// settled.
const rect = { x: 0, y: 0, w: 9, h: 9 };
const frames = Array.from({ length: 40 }, (_, k) => {
    const f = new Float32Array(81).fill(40);
    if (k < 3) {
        f[2 * 9 + 2] = 200;
        f[6 * 9 + 6] = 200;
    }
    return f;
});

test("specks counted until the picture settles", () => {
    const rows = settleCurve(frames, rect, 30, 30, [1, 3, 4, 40, 41]);
    assert.deepEqual(
        rows.map((r) => [r.frame, r.specks]),
        [
            [1, 2],
            [3, 2],
            [4, 0],
            [40, 0],
        ],
    );
    assert.ok(rows[0]!.rms > 0);
    assert.equal(rows[2]!.rms, 0);
});
