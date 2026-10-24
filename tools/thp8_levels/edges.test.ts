// NOT ORIGINAL - tests of edges.ts (the other zones' collision a network barrier closes in) and edge_audit.ts, on
// synthetic data.

import assert from "node:assert/strict";
import { test } from "node:test";
import type * as thug2Col from "../level_port/thug2_col.ts";
import { audit } from "./edge_audit.ts";
import * as edges from "./edges.ts";
import type * as havok from "./havok.ts";

// A flat ground strip: `cells` squares of one metre along x from x0, z 0..1, at height y.
const strip = (x0: number, cells: number, y: number, material = 0): havok.HkBody => ({
    index: 0,
    triangles: Array.from({ length: cells }, (_, i): havok.HkTriangle[] => {
        const x = x0 + i;
        return [
            { a: [x, y, 0], b: [x, y, 1], c: [x + 1, y, 0], material, owner: 0 },
            { a: [x + 1, y, 0], b: [x, y, 1], c: [x + 1, y, 1], material, owner: 0 },
        ];
    }).flat(),
});

// A wall across z at x, from y0 to y1.
const wall = (x: number, y0: number, y1: number, material = 4): havok.HkBody => ({
    index: 1,
    triangles: [
        { a: [x, y0, -5], b: [x, y1, -5], c: [x, y1, 5], material, owner: 0 },
        { a: [x, y0, -5], b: [x, y1, 5], c: [x, y0, 5], material, owner: 0 },
    ],
});

const cellsOf = (g: edges.Ground): number[] => [...g.keys()];

test("groundCells: material 0 faces are ground (a flag-less face is collidable)", () => {
    assert.equal(cellsOf(edges.groundCells([[strip(0, 3, 5, 0)]])).length, 3);
});

test("groundCells: non-collidable and trigger faces are not ground", () => {
    assert.equal(cellsOf(edges.groundCells([[strip(0, 3, 5, 0x10)]])).length, 0);
    assert.equal(cellsOf(edges.groundCells([[strip(0, 3, 5, 0x40)]])).length, 0);
});

test("reached: along the ground from the seeds, stopped by a wall, a step too high and the distance", () => {
    const own = edges.groundCells([[strip(0, 3, 5)]]);
    const all = edges.groundCells([[strip(0, 12, 5)]]);
    const open = edges.reached(all, own, new Set(), 100);
    assert.equal(open.size, 12);
    // (a barrier wall across the strip at x = 6: nothing beyond)
    const blocked = edges.reached(all, own, edges.wallCells([wall(6, 0, 20)]), 100);
    assert.equal(blocked.size, 5);
    // (a step of 4 m: no rolling up it)
    const stepped = edges.groundCells([[strip(0, 6, 5), strip(6, 6, 9)]]);
    assert.equal(edges.reached(stepped, own, new Set(), 100).size, 6);
    // (no further than `within` cells from the seeds)
    assert.equal(edges.reached(all, own, new Set(), 4).size, 7);
});

test("barrierReach: the furthest wall cell from the ground, with the slack", () => {
    const own = edges.groundCells([[strip(0, 3, 5)]]);
    assert.equal(edges.barrierReach(own, edges.wallCells([wall(10, 0, 20)])), 9 + edges.REACH_SLACK);
});

test("inCells: a whole body when one triangle is over the cells, none when not", () => {
    const body = strip(0, 20, 5, 0);
    const cells = edges.withRoom(
        edges.reached(edges.groundCells([[body]]), edges.groundCells([[strip(0, 2, 5)]]), new Set(), 1),
    );
    const kept = edges.inCells([body], cells);
    assert.equal(kept.length, 1);
    assert.equal(kept[0]!.triangles.length, body.triangles.length);
    assert.equal(edges.inCells([strip(100, 3, 5)], cells).length, 0);
});

test("inCells: a body's non-collidable triangles are left out", () => {
    const body = { index: 0, triangles: [...strip(0, 2, 5, 0).triangles, ...strip(0, 2, 5, 0x10).triangles] };
    const cells = new Set(edges.groundCells([[strip(0, 2, 5)]]).keys());
    assert.equal(edges.inCells([body], cells)[0]!.triangles.length, 4);
});

// A collision file of faces: [flags, a, b, c].
const colOf = (faces: [number, number[], number[], number[]][]): thug2Col.ColFile =>
    ({
        objects: [
            {
                verts: faces.flatMap((f) => [f[1], f[2], f[3]]),
                faces: faces.map((f, i) => [f[0], 0, i * 3, i * 3 + 1, i * 3 + 2]),
            },
        ],
    }) as unknown as thug2Col.ColFile;

// A square floor of cells x cells audit cells (24 inches), up to x = 24 * size.
const floor = (size: number, y = 100): [number, number[], number[], number[]][] => {
    const out: [number, number[], number[], number[]][] = [];
    const s = 24 * size;
    out.push([0, [0, y, 0], [0, y, s], [s, y, 0]]);
    out.push([0, [s, y, 0], [0, y, s], [s, y, s]]);
    return out;
};

test("audit: a big floor's open edge is found, its drop to a lower floor told apart", () => {
    const alone = audit(colOf(floor(60)));
    assert.ok(alone.spots.length >= 1, "an edge with nothing beyond");
    assert.equal(alone.drops.length, 0);
    const lower: [number, number[], number[], number[]][] = [
        [0, [1500, -300, 0], [1500, -300, 1440], [3000, -300, 0]],
        [0, [3000, -300, 0], [1500, -300, 1440], [3000, -300, 1440]],
    ];
    const both = audit(colOf([...floor(60), ...lower]));
    assert.ok(both.drops.length >= 1, "the east edge: ground below within reach");
});

test("audit: a wall at the edge closes it, in the edge's own cell too", () => {
    const edge = 24 * 60;
    const fence: [number, number[], number[], number[]][] = [
        [4, [edge + 5, 90, 0], [edge + 5, 200, 0], [edge + 5, 200, 1440]],
        [4, [edge + 5, 90, 0], [edge + 5, 200, 1440], [edge + 5, 90, 1440]],
    ];
    const open = audit(colOf(floor(60)));
    const closed = audit(colOf([...floor(60), ...fence]));
    assert.ok(closed.edgeCells < open.edgeCells);
});

test("audit: a floor ending short of a network barrier is a gap", () => {
    const barrier: [number, number[], number[], number[]][] = [
        [0x1090, [2400, -100, 0], [2400, 400, 0], [2400, 400, 1440]],
        [0x1090, [2400, -100, 0], [2400, 400, 1440], [2400, -100, 1440]],
    ];
    const a = audit(colOf([...floor(60), ...barrier]));
    assert.ok(a.gaps.length >= 1);
});

test("triangleInCells: a corner, the middle or a stretch of a big triangle over the cells counts, others do not", () => {
    const cells = new Set<number>([(5 + 32768) * 65536 + (5 + 32768)]); // the cell x 5..6, z 5..6
    assert.equal(
        edges.triangleInCells(
            [
                [5.2, 0, 5.2],
                [9, 0, 9],
                [9, 0, 12],
            ],
            cells,
        ),
        true,
    );
    // (huge, none of its corners in)
    assert.equal(
        edges.triangleInCells(
            [
                [-50, 0, -50],
                [60, 0, -50],
                [-50, 0, 60],
            ],
            cells,
        ),
        true,
    );
    assert.equal(
        edges.triangleInCells(
            [
                [10, 0, 10],
                [12, 0, 10],
                [10, 0, 12],
            ],
            cells,
        ),
        false,
    );
});

test("withoutUnmade: bodies owned only by nodes that are not made go (the school's pre-unlock fence)", () => {
    const owned = (index: number, owner: number): havok.HkBody => ({
        index,
        triangles: [{ a: [0, 0, 0], b: [0, 1, 0], c: [1, 0, 0], material: 4, owner }],
    });
    const mixed: havok.HkBody = { index: 3, triangles: [...owned(3, 7).triangles, ...owned(3, 8).triangles] };
    const kept = edges.withoutUnmade(
        [owned(0, 7), owned(1, 9), owned(2, 0), mixed, { index: 4, triangles: [] }],
        new Set([7]),
    );
    assert.deepEqual(
        kept.map((b) => b.index),
        [1, 2, 3, 4],
    );
});
