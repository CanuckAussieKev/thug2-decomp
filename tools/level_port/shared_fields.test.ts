// NOT ORIGINAL - nodes.shareNodeFields (the converters' last step on a level's .qb: every node kind's common fields
// shared as <level>_inc_<n> structures) and the guard that keeps the text passes before it.

import assert from "node:assert/strict";
import { test } from "node:test";
import { pack } from "../lib/struct.ts";
import {
    absentInNetGames,
    expandIncludes,
    groundRestarts,
    netRestarts,
    sharedDifferences,
    shareNodeFields,
    stripMissingTrickObjects,
    unshareNodeFields,
} from "./nodes.ts";
import { checksum, Name, QbFile, type QbValue, Struct, toNs, Vector } from "./qb.ts";
import { encodeValue } from "./qb_write.ts";

const NAME = (text: string): Name => new Name(checksum(text));
const vec = (x: number, y: number, z: number): Vector => new Vector("vector", [x, y, z]);
const struct = (entries: [string | null, QbValue][]): Struct =>
    new Struct(entries.map(([k, v]) => [k === null ? null : NAME(k), v]));

function rail(n: number, links: number[] | null): Struct {
    return struct([
        ["Pos", vec(n * 100, 0, 0)],
        ["Angles", vec(0, n, 0)],
        ["Name", NAME(`Rail0${n}`)],
        ["Class", NAME("RailNode")],
        ["Type", NAME("Metal")],
        [null, NAME("CreatedAtStart")],
        [null, NAME("TrickObject")],
        ["TerrainType", NAME("TERRAIN_GRINDMETAL")],
        ...(links ? [["Links", links] as [string, QbValue]] : []),
    ]);
}

const NODES = (): Struct[] => [
    rail(1, [1]),
    rail(2, [2]),
    rail(3, null),
    struct([
        ["Pos", vec(0, 0, 0)],
        ["Name", NAME("TRG_Restart01")],
        ["Class", NAME("Restart")],
        ["Type", NAME("Player1")],
        [null, NAME("CreatedAtStart")],
        ["restart_types", [NAME("Player1")]],
        ["TriggerScript", NAME("TRG_Restart01Script")],
    ]),
];

// A compiled level .qb as NeverScript writes one: the node array, a script, the closing 0x00.
function levelQb(nodes: Struct[]): Buffer {
    return Buffer.concat([
        Buffer.from([0x01, 0x16]),
        pack("<I", checksum("lv_NodeArray") >>> 0),
        Buffer.from([0x07]),
        encodeValue(nodes),
        Buffer.from([0x01, 0x23, 0x16]),
        pack("<I", checksum("TRG_Restart01Script") >>> 0),
        Buffer.from([0x01, 0x16]),
        pack("<I", checksum("Die") >>> 0),
        Buffer.from([0x01, 0x24, 0x01, 0x00]),
    ]);
}

const nodesOf = (data: Buffer): [QbValue[], Map<number, QbValue>] => {
    const q = new QbFile(data, "lv.qb", true);
    const array = q.get("lv_NodeArray");
    assert.ok(Array.isArray(array));
    return [array, new Map(q.globals.map(([k, v]) => [k.value, v]))];
};
const text = (nodes: readonly QbValue[]): string => nodes.map((n) => toNs(n, new Map())).join("\n");

test("shareNodeFields: every kind's common fields as <level>_inc_<n>, the nodes' own kept, the rest byte for byte", () => {
    const data = levelQb(NODES());
    const shared = shareNodeFields(data, "lv_NodeArray", "lv", "lv.qb");
    assert.equal(shared.includes, 1);
    assert.equal(shared.nodes, 3);
    const [array, globals] = nodesOf(shared.data);
    const inc = globals.get(checksum("lv_inc_0"));
    assert.ok(inc instanceof Struct);
    assert.equal(inc.entries.length, 5); // Class Type CreatedAtStart TrickObject TerrainType
    const first = array[0] as Struct;
    assert.deepEqual(
        first.entries.map(([k, v]) => (k === null ? (v as Name).value : k.value)),
        [checksum("Pos"), checksum("Angles"), checksum("Name"), checksum("lv_inc_0"), checksum("Links")],
    );
    // (the game sees the same fields: the include's entries where it is named)
    assert.equal(text(expandIncludes(array, globals)), text(NODES()));
    // (the restart, alone in its kind: its bytes and the script untouched)
    const restart = encodeValue(NODES()[3]!);
    assert.ok(shared.data.includes(restart));
    assert.equal(new QbFile(shared.data, "lv.qb", true).scripts.size, 1);
    assert.ok(new QbFile(shared.data, "lv.qb", true).names.get(checksum("lv_inc_0")) === "lv_inc_0");
});

test("shareNodeFields runs once, last: nodes naming shared structures already are an error", () => {
    const once = shareNodeFields(levelQb(NODES()), "lv_NodeArray", "lv", "lv.qb").data;
    assert.throws(() => shareNodeFields(once, "lv_NodeArray", "lv", "lv.qb"), /sharing runs once/);
});

test("unshareNodeFields puts the shared fields back in the nodes (make_menu_level's reading of the shop)", () => {
    const shared = shareNodeFields(levelQb(NODES()), "lv_NodeArray", "lv", "lv.qb").data;
    const [array] = nodesOf(unshareNodeFields(shared, "lv_NodeArray", "lv.qb"));
    assert.equal(text(array), text(NODES()));
});

test("the text passes refuse shared nodes: a field in an include can't be missed silently", () => {
    const sharedText =
        "lv_inc_0 = {\n    Class=LevelGeometry\n    CreatedAtStart\n    TrickObject\n    Cluster=c\n}\n" +
        "lv_NodeArray = [\n    {\n        Pos=(0.0, 0.0, 0.0)\n        Name=Sector01\n        lv_inc_0\n    }\n]\n";
    assert.throws(() => stripMissingTrickObjects(sharedText, new Set()), /before sharing/);
    assert.throws(() => absentInNetGames(sharedText), /before sharing/);
    assert.throws(() => netRestarts(sharedText), /before sharing/);
    assert.throws(() => groundRestarts(sharedText, { objects: [], pad: 0 }), /before sharing/);
    // (unshared: the strip sees the geometry's own TrickObject / Cluster)
    const own =
        "lv_NodeArray = [\n    {\n        Pos=(0.0, 0.0, 0.0)\n        Name=Sector01\n        Class=LevelGeometry\n" +
        "        CreatedAtStart\n        TrickObject\n        Cluster=c\n    }\n]\n";
    assert.deepEqual(stripMissingTrickObjects(own, new Set())[1], 1);
});

test("sharedDifferences: the shared level resolves to the same fields; a changed include or node is found", () => {
    const before = levelQb(NODES());
    const shared = shareNodeFields(before, "lv_NodeArray", "lv", "lv.qb").data;
    assert.deepEqual(sharedDifferences(before, shared, "lv_NodeArray", "lv.qb"), []);
    // (a field the rails don't have: the three rails that name the include differ)
    const nodes = NODES();
    for (const n of nodes.slice(0, 3)) n.entries = n.entries.filter(([k]) => k?.value !== checksum("TerrainType"));
    assert.deepEqual(sharedDifferences(levelQb(nodes), shared, "lv_NodeArray", "lv.qb"), [
        "node 0",
        "node 1",
        "node 2",
    ]);
    // (an own field changed)
    const moved = NODES();
    moved[3]!.entries[0] = [NAME("Pos"), vec(1, 0, 0)];
    assert.deepEqual(sharedDifferences(levelQb(moved), shared, "lv_NodeArray", "lv.qb"), ["node 3"]);
});
