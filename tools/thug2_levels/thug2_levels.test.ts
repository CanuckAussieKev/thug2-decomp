// NOT ORIGINAL - tests of the THUG2-levels comparison with THUGPRO (TP-42; no game data): node differences (flags,
// values, links, added nodes, float rounding), collision differences (faces taken out - the backs of two-sided faces -,
// face flags, terrain), merging edits, choosing entries, the setup wrapper's bytes, the folders never written into, and
// the register file's layout.

import assert from "node:assert/strict";
import { test } from "node:test";
import * as qb from "../level_port/qb.ts";
import { checksum, Name, type QbValue, Struct, Vector } from "../level_port/qb.ts";
import { scriptSpans } from "../level_port/qb_write.ts";
import type * as thug2Col from "../level_port/thug2_col.ts";
import { Float } from "../lib/py.ts";
import { type Context, colChanges, KLASSES, mergeEdits, nodeChanges, sameValue } from "./diff.ts";
import { loadRegister } from "./register.ts";
import { callsBody, chooseEntries, refusal } from "./thugpro_apply.ts";

const names = new Map<number, string>();
const nm = (t: string): Name => {
    names.set(checksum(t), t);
    return new Name(checksum(t));
};
const node = (entries: [string | null, QbValue][]): Struct =>
    new Struct(entries.map(([k, v]) => [k === null ? null : nm(k), v]));
const ctx = (): Context => ({
    names,
    colFaces: new Map([[checksum("Wall01"), 12]]),
    scripts: new Map(),
    thug2Scripts: new Set(),
});

test("sameValue: floats within rounding, vectors, names", () => {
    assert.ok(sameValue(new Float(1.0), new Float(1.004)));
    assert.ok(!sameValue(new Float(1.0), new Float(1.02)));
    assert.ok(sameValue(new Vector("vector", [1, 2, 3]), new Vector("vector", [1.001, 2, 3])));
    assert.ok(!sameValue(nm("a"), nm("b")));
    assert.ok(sameValue([1, nm("x")], [1, nm("x")]));
});

test("nodeChanges: flags, values, links and added nodes, classified", () => {
    const ours: QbValue[] = [
        node([
            ["Name", nm("Rail01")],
            ["Class", nm("RailNode")],
            [null, nm("CreatedAtStart")],
            [null, nm("AbsentInNetGames")],
            ["Links", [1]],
        ]),
        node([
            ["Name", nm("Rail02")],
            ["Class", nm("RailNode")],
            ["Pos", new Vector("vector", [0, 0, 0])],
        ]),
        node([
            ["Name", nm("Wall01")],
            ["Class", nm("LevelGeometry")],
            [null, nm("CreatedAtStart")],
        ]),
        node([
            ["Name", nm("Lamp01")],
            ["Class", nm("LevelGeometry")],
            ["SuspendDistance", 0],
        ]),
    ];
    const theirs: QbValue[] = [
        node([
            ["Name", nm("Rail01")],
            ["Class", nm("RailNode")],
            [null, nm("CreatedAtStart")],
            ["Links", [4]],
        ]),
        node([
            ["Name", nm("Rail02")],
            ["Class", nm("RailNode")],
            ["Pos", new Vector("vector", [0, 0.001, 0])],
        ]),
        node([
            ["Name", nm("Wall01")],
            ["Class", nm("LevelGeometry")],
        ]),
        node([
            ["Name", nm("Lamp01")],
            ["Class", nm("LevelGeometry")],
            ["SuspendDistance", 10000],
        ]),
        node([
            ["Name", nm("NewRail")],
            ["Class", nm("RailNode")],
            [null, nm("CreatedAtStart")],
            ["Links", [0]],
        ]),
    ];
    const c = nodeChanges(ours, theirs, ctx());
    const by = (n: string, kind: string) => c.find((x) => x.node === n && x.kind === kind);
    assert.equal(by("Rail01", "net_present")?.klass, "parity");
    assert.deepEqual(by("Rail01", "net_present")?.edit, { net_presence: { present: ["Rail01"] } });
    assert.deepEqual(by("Rail01", "node_values")?.edit, { rails: { links: { Rail01: ["NewRail"] } } });
    assert.equal(c.filter((x) => x.node === "Rail02").length, 0, "a rounding difference isn't one");
    assert.equal(by("Wall01", "created_off")?.what, "collision");
    assert.equal(by("Wall01", "created_off")?.klass, "parity");
    assert.equal(by("Lamp01", "node_values")?.klass, "cosmetic");
    const added = by("NewRail", "nodes_added");
    assert.equal(added?.what, "rails");
    assert.deepEqual(added?.edit, {
        rails: { added: [{ Name: "NewRail", Class: "RailNode", Links: ["Rail01"], flags: ["CreatedAtStart"] }] },
    });
});

const obj = (name: string, faces: thug2Col.Face[], verts = 6): thug2Col.ColObject => ({
    checksum: checksum(name),
    flags: 0,
    smallFaces: 0,
    fixedVerts: 0,
    bbox: [0, 0, 0, 1, 1, 1, 1, 1],
    verts: Array.from({ length: verts }, (_, i) => [i, 0, 0]),
    intensities: Buffer.alloc(verts),
    faces,
    reach: Buffer.alloc(0),
    bsp: { kind: "leaf", faces: faces.map((_, i) => i), low: 3 },
});

test("colChanges: backs of two-sided faces taken out, flags and terrain by face", () => {
    names.set(checksum("Sign01"), "Sign01");
    const ours: thug2Col.ColFile = {
        objects: [
            obj("Sign01", [
                [1, 0, 0, 1, 2],
                [1, 0, 2, 1, 0],
                [1, 0, 3, 4, 5],
            ]),
        ],
    };
    const theirs: thug2Col.ColFile = {
        objects: [
            obj("Sign01", [
                [1 | 4, 0, 0, 1, 2],
                [1, 7, 3, 4, 5],
            ]),
        ],
    };
    const c = colChanges(ours, theirs, names);
    const removed = c.find((x) => x.kind === "faces_removed");
    assert.match(removed?.detail ?? "", /1 of 3 faces taken out, all backs of two-sided faces/);
    assert.deepEqual(removed?.edit, { collision_removed: { Sign01: { faces: 3, remove: [1] } } });
    assert.deepEqual(c.find((x) => x.kind === "face_flags")?.edit, {
        face_values: { Sign01: { faces: 3, set: { "0x0004": [0] } } },
    });
    assert.deepEqual(c.find((x) => x.kind === "terrain")?.edit, {
        face_values: { Sign01: { faces: 3, terrain: [[2, 7]] } },
    });
    // both on one object merge
    const merged = mergeEdits(c.flatMap((x) => (x.edit ? [x.edit] : [])));
    assert.deepEqual(merged.face_values?.Sign01, { faces: 3, set: { "0x0004": [0] }, terrain: [[2, 7]] });
});

test("mergeEdits: lists join, node values merge per node", () => {
    const m = mergeEdits([
        { net_presence: { present: ["a"] }, node_values: { n: { TriggerScript: "x" } } },
        { net_presence: { present: ["b"], absent: ["c"] }, node_values: { n: { "+TrickObject": true } } },
        { rails: { added: [{ Name: "r" }] } },
    ]);
    assert.deepEqual(m.net_presence, { present: ["a", "b"], absent: ["c"] });
    assert.deepEqual(m.node_values, { n: { TriggerScript: "x", "+TrickObject": true } });
    assert.deepEqual(m.rails, { added: [{ Name: "r" }] });
});

test("callsBody: one call per line, flags after the script", () => {
    const body = callsBody([["BO_setup"], ["bo_LEVEL_CHANGE_01", "off"]]);
    const data = Buffer.concat([
        Buffer.from([0x23, 0x16]),
        Buffer.alloc(4),
        Buffer.from([0x01]),
        body,
        Buffer.from([0x24, 0x00]),
    ]);
    assert.ok(scriptSpans(data).has(0));
    const words = [...qb.tokens(body, true)].filter(([, t]) => t === 0x16).map(([, , p]) => p.readUInt32LE(0));
    assert.deepEqual(words, ["BO_setup", "bo_LEVEL_CHANGE_01", "off"].map(checksum));
    assert.equal(body.filter((b) => b === 0x01).length, 2);
});

test("refusal: the main install, backups and THUGPRO's folders", () => {
    assert.ok(refusal("C:\\Games\\THUG2", false));
    assert.equal(refusal("C:\\Games\\THUG2", true), null);
    assert.ok(refusal("C:\\Games\\THUG2_PM_BAK", true));
    assert.ok(refusal("C:\\Games\\THUGPRO\\Game", true));
    assert.ok(refusal("C:\\Users\\x\\AppData\\Local\\THUG Pro", true));
    assert.equal(refusal("C:\\Games\\THUG2-3", false), null);
});

test("the register: ids, classes, every entry off, applicable ones carry their data", () => {
    const reg = loadRegister();
    assert.ok(reg, "tools/compat/thugpro/thug2_levels.json");
    assert.equal(reg._register, "TP-42");
    const ids = new Set<string>();
    for (const [L, lr] of Object.entries(reg.levels)) {
        for (const e of lr.entries) {
            assert.equal(e.level, L);
            assert.ok(!ids.has(e.id), e.id);
            ids.add(e.id);
            assert.ok(KLASSES.includes(e.class), `${e.id} ${e.class}`);
            assert.equal(e.enabled, false, `${e.id}: off until the user decides`);
            if (e.applicable) assert.ok(e.edits || e.scripts || e.replace_scripts || e.level_setup || e.geometry, e.id);
        }
    }
    assert.ok(ids.size > 0);
    assert.equal(chooseEntries(reg, undefined).length, 0, "nothing switched on by default");
    assert.ok(chooseEntries(reg, "parity").every((e) => e.class === "parity" && e.applicable));
    assert.equal(loadRegister("none"), null, "--compat none: no entries");
});
