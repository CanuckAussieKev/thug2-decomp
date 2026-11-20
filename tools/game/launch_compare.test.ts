import assert from "node:assert/strict";
import { test } from "node:test";
import { cellMeans, seriesCells } from "./launch_compare.ts";

test("a frame's cells' means", () => {
    // 4 x 2 pixels, grey 10 on the left half, 30 on the right
    const rgb = new Uint8Array(4 * 2 * 3);
    for (let y = 0; y < 2; y++) {
        for (let x = 0; x < 4; x++) {
            rgb.fill(x < 2 ? 10 : 30, (y * 4 + x) * 3, (y * 4 + x) * 3 + 3);
        }
    }
    const cells = cellMeans(rgb, 4, { x: 0, y: 0, w: 4, h: 2 }, 2, 1);
    assert.ok(Math.abs(cells[0]! - 10) < 1e-9);
    assert.ok(Math.abs(cells[1]! - 30) < 1e-9);
});

test("a series' cell means and their standard error", () => {
    const s = seriesCells([[10], [12], [14]]);
    assert.equal(s.mean[0], 12);
    assert.ok(Math.abs(s.error[0]! - 2 / Math.sqrt(3)) < 1e-9);
});
