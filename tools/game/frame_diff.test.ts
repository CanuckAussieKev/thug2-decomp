// NOT ORIGINAL - unit tests of tools/game/frame_diff.ts (no game data needed).

import assert from "node:assert/strict";
import { test } from "node:test";
import * as zlib from "node:zlib";
import { analyse, decodePng, encodePng, type Image, regionStats } from "./frame_diff.ts";

const image = (width: number, height: number, fill: (x: number, y: number) => number[]): Image => {
    const rgb = new Uint8Array(width * height * 3);
    for (let y = 0; y < height; y++) for (let x = 0; x < width; x++) rgb.set(fill(x, y), (y * width + x) * 3);
    return { width, height, rgb };
};

test("PNG: our RGB encoding decodes back the same", () => {
    const a = image(5, 3, (x, y) => [x * 40, y * 80, (x + y) * 20]);
    const b = decodePng(encodePng(a));
    assert.equal(b.width, 5);
    assert.equal(b.height, 3);
    assert.deepEqual([...b.rgb], [...a.rgb]);
});

test("PNG: every row filter (sub, up, average, Paeth) and RGBA undone", () => {
    // Two RGBA pixels a row, three rows: row 0 filter 1 (sub), row 1 filter 2 (up), row 2 filter 3 (average) - then a
    // fourth with filter 4 (Paeth).
    const want = [
        [10, 20, 30, 255, 15, 27, 39, 255],
        [11, 22, 33, 255, 20, 30, 40, 255],
        [12, 24, 36, 255, 25, 33, 41, 255],
        [13, 26, 39, 255, 30, 36, 42, 255],
    ];
    const paeth = (a: number, b: number, c: number): number => {
        const p = a + b - c;
        const pa = Math.abs(p - a);
        const pb = Math.abs(p - b);
        const pc = Math.abs(p - c);
        return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
    };
    const rows: number[] = [];
    want.forEach((row, y) => {
        const filter = y + 1;
        rows.push(filter);
        row.forEach((v, x) => {
            const a = x >= 4 ? row[x - 4]! : 0;
            const b = y > 0 ? want[y - 1]![x]! : 0;
            const c = x >= 4 && y > 0 ? want[y - 1]![x - 4]! : 0;
            const predicted = filter === 1 ? a : filter === 2 ? b : filter === 3 ? (a + b) >> 1 : paeth(a, b, c);
            rows.push((v - predicted) & 0xff);
        });
    });
    const header = Buffer.alloc(13);
    header.writeUInt32BE(2, 0);
    header.writeUInt32BE(4, 4);
    header[8] = 8;
    header[9] = 6;
    const chunk = (type: string, body: Buffer): Buffer => {
        const head = Buffer.alloc(8);
        head.writeUInt32BE(body.length, 0);
        head.write(type, 4, "latin1");
        return Buffer.concat([head, body, Buffer.alloc(4)]); // (the CRC isn't checked)
    };
    const png = Buffer.concat([
        Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]),
        chunk("IHDR", header),
        chunk("IDAT", zlib.deflateSync(Buffer.from(rows))),
        chunk("IEND", Buffer.alloc(0)),
    ]);
    const got = decodePng(png);
    assert.deepEqual(
        [...got.rgb],
        want.flatMap((row) => [row[0], row[1], row[2], row[4], row[5], row[6]]),
    );
});

test("flicker: a pixel flipping every other frame counts, a still one and a small change don't", () => {
    // 3 x 1: pixel 0 flips black / white, pixel 1 still, pixel 2 creeps by 2 a step (under the threshold).
    const frames = [0, 1, 2, 3, 4].map((f) =>
        image(3, 1, (x) => (x === 0 ? (f % 2 ? [255, 255, 255] : [0, 0, 0]) : x === 1 ? [9, 9, 9] : [f * 2, 0, 0])),
    );
    const result = analyse(frames, 24);
    assert.deepEqual([...result.changes], [4, 0, 0]);
    assert.equal(result.steps, 4);
    const all = regionStats(3, result, { name: "all", x: 0, y: 0, w: 3, h: 1 });
    assert.equal(all.flickering, 1);
    assert.equal(all.rate, 4 / 12);
    assert.equal(all.meanDiff, (4 * 765 + 4 * 2) / 12);
    const still = regionStats(3, result, { name: "still", x: 1, y: 0, w: 1, h: 1 });
    assert.equal(still.flickering, 0);
    assert.equal(still.meanDiff, 0);
});
