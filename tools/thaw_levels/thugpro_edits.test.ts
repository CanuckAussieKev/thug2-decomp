// NOT ORIGINAL - tests of THUGPRO's hand edits applied from the compatibility profile (thugpro_edits.ts, TP-5...TP-9)
// and the extraction's rigid fit (thugpro_extract.ts): no game data.

import assert from "node:assert/strict";
import { test } from "node:test";
import { checksum, Name, type QbValue, Struct, Vector } from "../level_port/qb.ts";
import type * as thug2Col from "../level_port/thug2_col.ts";
import { Float } from "../lib/py.ts";
import { compactJson } from "./thugpro_compare.ts";
import {
    applyFaceEdits,
    applyNodeEdits,
    decodeValue,
    emptyEdits,
    encodeValue,
    moveColObject,
    movePoint,
    zoneEdits,
} from "./thugpro_edits.ts";
import { fitRigid } from "./thugpro_extract.ts";

const names = (): Map<number, string> => {
    const m = new Map<number, string>();
    for (const t of ["Name", "Class", "Pos", "Links", "RailNode", "CreatedAtStart", "AbsentInNetGames", "TerrainType"])
        m.set(checksum(t), t);
    return m;
};
const node = (name: string, extra: [string | null, QbValue][] = []): Struct =>
    new Struct([
        [new Name(checksum("Name")), new Name(checksum(name))],
        [new Name(checksum("Class")), new Name(checksum("RailNode"))],
        [new Name(checksum("Pos")), new Vector("vector", [1, 2, 3])],
        ...extra.map(([k, v]): [Name | null, QbValue] => [k === null ? null : new Name(checksum(k)), v]),
    ]);
const nameOf = (n: QbValue | undefined): number | undefined =>
    n instanceof Struct && n.get("Name") instanceof Name ? (n.get("Name") as Name).value : undefined;

test("qb values go through the profile's JSON unchanged", () => {
    const n = names();
    const value: QbValue = [
        3,
        new Float(1.5),
        new Name(checksum("RailNode")),
        "text",
        new Vector("vector", [1, 2, 3]),
        new Vector("pair", [4, 5]),
        new Struct([
            [null, new Name(checksum("CreatedAtStart"))],
            [new Name(checksum("Pos")), 7],
        ]),
    ];
    const json = encodeValue(value, n);
    assert.deepEqual(json, [
        3,
        { f: 1.5 },
        { n: "RailNode" },
        { s: "text" },
        { v: [1, 2, 3] },
        { p: [4, 5] },
        {
            struct: [
                [null, { n: "CreatedAtStart" }],
                ["Pos", 7],
            ],
        },
    ]);
    assert.deepEqual(encodeValue(decodeValue(json, n), n), json);
    // a name without a known text keeps its checksum
    assert.deepEqual(encodeValue(new Name(0x1234abcd), n), { n: "#1234ABCD" });
    assert.equal((decodeValue({ n: "#1234ABCD" }, n) as Name).value, 0x1234abcd);
});

test("applyNodeEdits: renames, moves, flags and keys, removals in place, additions linked by name", () => {
    const n = names();
    const array: QbValue[] = [
        node("A", [
            [null, new Name(checksum("AbsentInNetGames"))],
            ["Cluster", new Name(checksum("OldCluster"))],
        ]),
        node("B", [["Links", [0]]]),
        node("C"),
    ];
    const edits = emptyEdits();
    edits.renamed.push({ node: "C", to: "C2" });
    edits.moved.push({ node: "C2", pos: [9, 9, 9], links: ["A"] });
    edits.flags.push({
        node: "A",
        remove: ["AbsentInNetGames"],
        add: ["CreatedAtStart"],
        set: { TerrainType: { n: "TERRAIN_GRINDWOOD" } },
    });
    edits.flags.push({ node: "B", unset: ["Links"] });
    edits.removedNodes.push("B");
    edits.clusters.OldCluster = "z_xx_OldCluster";
    edits.added.push({
        entries: [
            ["Name", { n: "D" }],
            ["Class", { n: "RailNode" }],
        ],
        links: ["C2", "nowhere"],
    });
    const report: string[] = [];
    const out = applyNodeEdits(edits, array, n, report);
    assert.equal(out.length, 4);
    const [a, b, c, d] = out as Struct[];
    assert.ok(!a!.hasFlag("AbsentInNetGames") && a!.hasFlag("CreatedAtStart"));
    assert.equal((a!.get("TerrainType") as Name).value, checksum("TERRAIN_GRINDWOOD"));
    assert.equal(nameOf(b), undefined); // (a placeholder: its index kept)
    assert.equal(nameOf(c), checksum("C2"));
    assert.deepEqual((c!.get("Pos") as Vector).values, [9, 9, 9]);
    assert.deepEqual(c!.get("Links"), [0]);
    assert.equal(nameOf(d), checksum("D"));
    assert.deepEqual(d!.get("Links"), [2]); // (the link to a node that isn't there dropped)
    assert.equal((a!.get("Cluster") as Name).value, checksum("z_xx_OldCluster"));
    assert.equal(report.length, 1);
    assert.throws(() => applyNodeEdits({ ...emptyEdits(), removedNodes: ["X"] }, array, n, []));
});

test("applyFaceEdits sets / clears flags and terrain, refuses another face count", () => {
    const o: thug2Col.ColObject = {
        checksum: checksum("Wall"),
        flags: 0,
        smallFaces: 0,
        fixedVerts: 0,
        bbox: [0, 0, 0, 1, 1, 1, 1, 1],
        verts: [
            [0, 0, 0],
            [1, 0, 0],
            [0, 1, 0],
        ],
        intensities: Buffer.alloc(3),
        faces: [
            [0x0001, 0, 0, 1, 2],
            [0x0011, 3, 0, 2, 1],
        ],
        reach: Buffer.alloc(2),
        bsp: { kind: "leaf", faces: [0, 1], low: 3 },
    };
    const edits = emptyEdits();
    edits.faces.Wall = {
        faces: 2,
        edits: [
            [0, 0x0004, 0, -1],
            [1, 0, 0x0010, 7],
        ],
    };
    const col = { objects: [o] };
    applyFaceEdits(edits, col, []);
    assert.deepEqual(
        o.faces.map((f) => [f[0], f[1]]),
        [
            [0x0005, 0],
            [0x0001, 7],
        ],
    );
    edits.faces.Wall.faces = 3;
    assert.throws(() => applyFaceEdits(edits, col, []));
});

test("movePoint / moveColObject: turned about the pivot, then moved; a new box and tree", () => {
    const t = { translate: [10, 0, 0], rotate_y: Math.PI / 2, pivot: [1, 0, 0] };
    const p = movePoint([2, 5, 0], t);
    assert.ok(Math.abs(p[0]! - 11) < 1e-9 && p[1] === 5 && Math.abs(p[2]! + 1) < 1e-9);
    const o: thug2Col.ColObject = {
        checksum: 1,
        flags: 0,
        smallFaces: 0,
        fixedVerts: 1,
        bbox: [0, 0, 0, 1, 1, 1, 0, 1],
        verts: [
            [0, 0, 0],
            [1, 0, 0],
            [0, 1, 0],
        ],
        intensities: Buffer.alloc(3),
        faces: [[1, 0, 0, 1, 2]],
        reach: Buffer.alloc(1),
        bsp: { kind: "leaf", faces: [0], low: 3 },
    };
    const moved = moveColObject(o, { translate: [0.3, 0, -2] });
    assert.equal(moved.fixedVerts, 0);
    assert.deepEqual(moved.verts[1], [Math.fround(1.3), 0, -2]);
    assert.deepEqual(moved.bbox, [Math.fround(0.3), 0, -2, 1, Math.fround(1.3), 1, -2, 1]);
    assert.equal(moved.reach.length, 1);
});

test("fitRigid: the same shape, a rigid move (in any vertex order), or a changed mesh", () => {
    const a = [
        [0, 0, 0],
        [4, 0, 0],
        [4, 0, 2],
        [0, 3, 2],
    ];
    const shift = [100, 0, 0];
    const inFrame = a.map((p) => [p[0]! + 100, p[1]!, p[2]!]);
    assert.equal(fitRigid(a, inFrame, shift), "same");
    assert.equal(fitRigid(a, [...inFrame].reverse(), shift), "same");
    const moved = inFrame.map((p) => [p[0]! + 5, p[1]! - 1, p[2]!]);
    const t = fitRigid(a, [...moved].reverse(), shift);
    assert.ok(t !== null && t !== "same");
    assert.deepEqual(t.translate, [5, -1, 0]);
    const turned = a.map((p) => movePoint(p, { translate: [1, 0, 0], rotate_y: 0.3, pivot: [2, 0, 1] }));
    const fit = fitRigid(
        a,
        turned.map((p) => [p[0]! + 100, p[1]!, p[2]!]),
        shift,
    );
    assert.ok(fit !== null && fit !== "same" && Math.abs(fit.rotate_y! - 0.3) < 1e-4);
    assert.equal(fitRigid(a, [...inFrame.slice(0, 3), [50, 50, 50]], shift), null);
});

test("zoneEdits: the profile's sections for one zone, null without any", () => {
    assert.equal(zoneEdits({}, "Z_XX"), null);
    const z = zoneEdits({ removed: { Z_XX: { nodes: ["A"] } }, collision: {} }, "z_xx");
    assert.deepEqual(z?.removedNodes, ["A"]);
    assert.deepEqual(z?.added, []);
});

test("compactJson keeps lists of plain values on one line", () => {
    assert.equal(
        compactJson({ a: [1, 2], b: [[1, "x"]], c: {} }),
        '{\n  "a": [1, 2],\n  "b": [\n    [1, "x"]\n  ],\n  "c": {}\n}',
    );
});
