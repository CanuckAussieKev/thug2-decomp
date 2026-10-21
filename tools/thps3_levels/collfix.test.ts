// NOT ORIGINAL - tests of tools/thps3_levels/collfix.ts (no game data).

import assert from "node:assert/strict";
import { test } from "node:test";
import type * as compat from "../compat/compat.ts";
import { cover, exposure, isCollfix, replacedNodes, tri, withoutCollfix } from "./collfix.ts";

const entry = (name: string, kind = "collision"): compat.Entry => ({
    id: "TPN-THPS3-1",
    op: "add",
    kind,
    target: { level: "sub", name },
    why: "",
});

test("isCollfix: THUGPRO's overlay names, not other new content", () => {
    for (const n of [
        "SUB_TRamp06_collfix",
        "Inviso_PicnicCollide01_collfix",
        "SouthDeck_QP01_coll",
        "Trench_HP04_Back_coll",
    ])
        assert.ok(isCollfix(n), n);
    for (const n of ["Obj_Bucket", "foun_wire", "ES_Building_collision", "TRG_BBQ_BurningNoise", "SUB_collfix_thing"])
        assert.ok(!isCollfix(n), n);
});

test("withoutCollfix leaves out the overlays' collision objects and nodes, keeps the rest", () => {
    const entries = [
        entry("SUB_TRamp06_collfix"),
        entry("SUB_TRamp06_collfix", "node"),
        entry("Obj_Bucket"),
        entry("TRG_BBQ_BurningNoise", "node"),
    ];
    assert.deepEqual(
        withoutCollfix(entries).map((e) => e.target.name),
        ["Obj_Bucket", "TRG_BBQ_BurningNoise"],
    );
});

test("replacedNodes: the flagged THPS3 nodes an overlay takes the place of", () => {
    const entries = [entry("Inviso_PicnicCollide01_collfix"), entry("SUB_TRamp06_collfix"), entry("Obj_Bucket")];
    assert.deepEqual(replacedNodes(entries, ["Inviso_PicnicCollide01", "SkeletonHand02", "Obj_Bucket"]), [
        "Inviso_PicnicCollide01",
    ]);
    assert.deepEqual(replacedNodes(entries, ["inviso_picniccollide01"]), ["inviso_picniccollide01"]);
    assert.deepEqual(replacedNodes([], ["Inviso_PicnicCollide01"]), []);
});

// A unit square floor at y = 0 (two faces, normal up) and the same square at `y`.
const floor = (object: number, y: number) => [
    tri(object, 0, [0, y, 0], [0, y, 100], [100, y, 0]),
    tri(object, 0, [100, y, 100], [100, y, 0], [0, y, 100]),
];

test("cover: a duplicate, a face just below, one facing the other way, none", () => {
    const level = floor(1, 0);
    assert.ok(level[0]!.n[1] > 0.99, "the test floor faces up");
    assert.equal(cover(floor(2, 0.2)[0]!, level).kind, "same");
    const below = cover(floor(2, -3)[0]!, level);
    assert.equal(below.kind, "offset");
    assert.equal(below.offset, 3);
    const flipped = tri(2, 0, [0, -1, 0], [100, -1, 0], [0, -1, 100]);
    assert.ok(flipped.n[1] < -0.99);
    assert.equal(cover(flipped, level).kind, "back only");
    assert.equal(cover(floor(2, -20)[0]!, level).kind, "none");
});

test("exposure: an overlay under the floor is met after it; one in the open first", () => {
    const level = floor(1, 0);
    const under = exposure(floor(2, -5), level);
    assert.equal(under.exposed + under.enclosed + under.same, 0);
    assert.ok(under.inFront > 0);
    assert.ok(under.depth.every((d) => d === 5));
    const open = exposure(floor(2, 10), level);
    assert.equal(open.inFront, 0);
    assert.ok(open.exposed > 0);
    assert.ok(open.behind.every((d) => d === 10));
});
