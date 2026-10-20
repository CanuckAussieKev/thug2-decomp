// NOT ORIGINAL - tests of THUGPRO's hand edits as data (hand_edits.ts, TP-32 ... TP-39; no game data): the JSON form of
// qb values, the node edits on THUG1's array (moves, flags, values, added rails and their links, relinks), the collision
// edits (face flags and terrain, objects and faces taken out, vertices moved), the check of a converted level against
// the data, and the profile's JSON layout.

import assert from "node:assert/strict";
import { test } from "node:test";
import { checksum, Name, type QbValue, Struct, Vector } from "../level_port/qb.ts";
import type * as thug2Col from "../level_port/thug2_col.ts";
import { Float } from "../lib/py.ts";
import {
    applyColEdits,
    applyNodeEdits,
    checkEdits,
    decode,
    editCounts,
    encode,
    type LevelEdits,
} from "./hand_edits.ts";
import { formatJson } from "./thugpro_compare.ts";

const names = (): Map<number, string> => new Map();
const nm = (t: string, n: Map<number, string>): Name => {
    n.set(checksum(t), t);
    return new Name(checksum(t));
};

test("encode / decode: names, numbers, floats, strings, vectors, lists, structures", () => {
    const n = names();
    const values: QbValue[] = [
        nm("TERRAIN_GRINDMETAL", n),
        new Name(0x12345678),
        7,
        new Float(0.5),
        "veh\\a.mdl",
        new Vector("vector", [1, 2, 3]),
        new Vector("pair", [4, 5]),
        [nm("Player1", n), 3],
        new Struct([
            [nm("x", n), 1],
            [null, nm("Flag", n)],
        ]),
    ];
    const json = values.map((v) => encode(v, n));
    assert.deepEqual(json.slice(0, 7), [
        "TERRAIN_GRINDMETAL",
        "#12345678",
        7,
        { float: 0.5 },
        { string: "veh\\a.mdl" },
        { vector: [1, 2, 3] },
        { pair: [4, 5] },
    ]);
    const back = names();
    assert.deepEqual(
        json.map((j) => encode(decode(j, back), back)),
        json,
    );
    assert.equal(back.get(checksum("TERRAIN_GRINDMETAL")), "TERRAIN_GRINDMETAL");
});

function level(): [QbValue[], Map<number, string>] {
    const n = names();
    const node = (name: string, cls: string, x: number, extra: [Name | null, QbValue][] = []): Struct =>
        new Struct([
            [nm("Pos", n), new Vector("vector", [x, 0, 0])],
            [nm("Name", n), nm(name, n)],
            [nm("Class", n), nm(cls, n)],
            ...extra,
        ]);
    const array: QbValue[] = [
        node("Rail1", "RailNode", 0, [
            [nm("TerrainType", n), nm("TERRAIN_GRINDCONC", n)],
            [null, nm("CreatedAtStart", n)],
            [nm("Links", n), [1]],
        ]),
        node("Rail2", "RailNode", 10, [[null, nm("CreatedAtStart", n)]]),
        node("Geo", "LevelGeometry", 20, [
            [null, nm("CreatedAtStart", n)],
            [null, nm("AbsentInNetGames", n)],
        ]),
        node("Dog", "Pedestrian", 30, [[nm("SkeletonName", n), nm("dog", n)]]),
    ];
    return [array, n];
}

const EDITS: LevelEdits = {
    moved_nodes: { Rail2: { pos: [11, 1, 0], angles: [0, 1, 0] } },
    net_presence: { present: ["Geo"], absent: ["Dog"] },
    created_at_start: { off: ["Geo"] },
    node_values: {
        Rail1: { TerrainType: "TERRAIN_GRINDMETAL", "+TrickObject": true },
        Dog: { Class: "GameObject", SkeletonName: null, model: { string: "none" } },
    },
    rails: {
        added: [
            {
                Pos: { vector: [5, 5, 5] },
                Name: "New1",
                Class: "RailNode",
                TerrainType: "TERRAIN_DEFAULT",
                flags: ["CreatedAtStart"],
                Links: ["New2"],
            },
            { Pos: { vector: [6, 5, 5] }, Name: "New2", Class: "RailNode", flags: ["CreatedAtStart"] },
        ],
        links: { Rail2: ["New1"] },
    },
};

test("applyNodeEdits: moves, flags, values, added rails and links, as copies", () => {
    const [array, n] = level();
    const before = array[2];
    const report = applyNodeEdits(array, n, EDITS, "T");
    assert.match(report[0]!, /TP-32 moved 1; TP-33 .*1 present, 1 absent; TP-34 .*0 on, 1 off; TP-35 .* 2 nodes/);
    assert.match(report[0]!, /TP-36 rails: 2 added, 0 reused, 1 relinked/);
    assert.notEqual(array[2], before); // (edited nodes are fresh copies)
    const node = (i: number): Struct => array[i] as Struct;
    assert.deepEqual((node(1).get("Pos") as Vector).values, [11, 1, 0]);
    assert.deepEqual((node(1).get("Angles") as Vector).values, [0, 1, 0]);
    assert.equal(node(2).hasFlag("AbsentInNetGames"), false);
    assert.equal(node(2).hasFlag("CreatedAtStart"), false);
    assert.equal(node(3).hasFlag("AbsentInNetGames"), true);
    assert.equal((node(0).get("TerrainType") as Name).value, checksum("TERRAIN_GRINDMETAL"));
    assert.equal(node(0).hasFlag("TrickObject"), true);
    assert.equal((node(3).get("Class") as Name).value, checksum("GameObject"));
    assert.equal(node(3).get("SkeletonName"), undefined);
    assert.equal(node(3).get("model"), "none");
    assert.equal(array.length, 6);
    assert.deepEqual(node(4).get("Links"), [5]); // (New1 -> New2 by name)
    assert.deepEqual(node(1).get("Links"), [4]); // (Rail2 relinked to New1)
    assert.equal(n.get(checksum("New1")), "New1");
    assert.throws(() =>
        applyNodeEdits(level()[0], level()[1], { moved_nodes: { Nope: { pos: [], angles: [] } } }, "T"),
    );
    assert.throws(() => applyNodeEdits(array, n, { rails: { added: [{ Name: "Rail1" }] } }, "T")); // (a name it has)
});

test("checkEdits: the converted nodes against the data, misses explained", () => {
    const [array, n] = level();
    applyNodeEdits(array, n, EDITS, "T");
    const ours = array as Struct[];
    ours[0]!.removeFlag("TrickObject"); // (as a converter dropping the flag would)
    const checks = checkEdits(
        { ...EDITS, moved_nodes: { ...EDITS.moved_nodes, Gone: { pos: [], angles: [] } } },
        ours,
        n,
        null,
    );
    assert.deepEqual(
        [...checks].map(([tp, c]) => [tp, c.same, c.total]),
        [
            ["TP-32", 1, 2],
            ["TP-33", 2, 2],
            ["TP-34", 1, 1],
            ["TP-35", 1, 2],
            ["TP-36", 3, 3],
        ],
    );
    assert.deepEqual(
        [...checks.get("TP-32")!.differ],
        [["node not in our port (our converter leaves it out)", ["Gone"]]],
    );
    assert.deepEqual([...checks.get("TP-35")!.differ], [["not kept by our converter: +TrickObject", ["Rail1"]]]);
    assert.deepEqual(editCounts(EDITS), [
        "TP-32 1 moved",
        "TP-33 2 net presence",
        "TP-34 1 created at start",
        "TP-35 2 nodes' values",
        "TP-36 3 rail nodes / links",
    ]);
});

function col(): thug2Col.ColFile {
    const object = (name: string, faces: number): thug2Col.ColObject => ({
        checksum: checksum(name),
        flags: 0,
        smallFaces: 1,
        fixedVerts: 0,
        bbox: [0, 0, 0, 1, 10, 10, 10, 1],
        verts: [
            [0, 0, 0],
            [10, 0, 0],
            [0, 0, 10],
            [10, 0, 10],
        ],
        intensities: Buffer.alloc(4),
        faces: Array.from({ length: faces }, (_, i): thug2Col.Face => [0x1, 1, 0, i % 2 ? 3 : 1, 2]),
        reach: Buffer.alloc(faces),
        bsp: { kind: "leaf", faces: Array.from({ length: faces }, (_, i) => i), low: 3 },
    });
    return { objects: [object("Wall", 2), object("Prop", 2), object("Pipe", 2)] };
}

test("applyColEdits: face flags and terrain, objects and faces out, vertices moved; checked", () => {
    const edits: LevelEdits = {
        face_values: { Wall: { faces: 2, set: { "0x0004": [0, 1] }, clear: { "0x0001": [1] }, terrain: [[0, 9]] } },
        collision_removed: { Prop: { faces: 2, remove: "all" }, Pipe: { faces: 2, remove: [1] } },
        collision_reshaped: { Wall: { faces: 2, verts: [[3, 10, 4, 10]] } },
    };
    const [out, report] = applyColEdits(col(), edits, "T");
    assert.match(report[0]!, /TP-37 2 faces' .*TP-38 1 collision objects and 1 faces out; TP-39 1 collision vertices/);
    assert.deepEqual(
        out.objects.map((o) => o.checksum),
        [checksum("Wall"), checksum("Pipe")],
    );
    const wall = out.objects[0]!;
    assert.deepEqual(
        wall.faces.map((f) => [f[0], f[1]]),
        [
            [0x5, 9],
            [0x4, 1],
        ],
    );
    assert.deepEqual(wall.verts[3], [10, 4, 10]);
    assert.equal(wall.reach.length, 2);
    assert.equal(out.objects[1]!.faces.length, 1);
    assert.equal(out.objects[1]!.reach.length, 1);
    const checks = checkEdits(edits, [], new Map(), out);
    assert.deepEqual(
        [...checks].map(([tp, c]) => [tp, c.same, c.total]),
        [
            ["TP-37", 1, 1],
            ["TP-38", 2, 2],
            ["TP-39", 1, 1],
        ],
    );
    assert.throws(() => applyColEdits(col(), { collision_removed: { Wall: { faces: 3, remove: "all" } } }, "T"));
});

test("formatJson: numbers and short lists / objects on one line", () => {
    assert.equal(formatJson({ a: [1, 2], b: { c: "x" } }), '{"a": [1, 2], "b": {"c": "x"}}');
    const long = formatJson({ list: Array.from({ length: 30 }, (_, i) => `name_${i}`) });
    assert.match(long, /^\{\n {2}"list": \[\n {4}"name_0",\n/);
    assert.deepEqual(JSON.parse(long), { list: Array.from({ length: 30 }, (_, i) => `name_${i}`) });
});
