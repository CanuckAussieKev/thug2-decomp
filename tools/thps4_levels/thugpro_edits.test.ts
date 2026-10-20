// NOT ORIGINAL - tests of applying THUGPRO's hand edits (TP-22 ... TP-29: thugpro_edits.ts) to converted nodes, with
// made-up nodes (no game data), and of the profile switches.

import assert from "node:assert/strict";
import { test } from "node:test";
import * as qb from "../level_port/qb.ts";
import { Name, Struct, Vector } from "../level_port/qb.ts";
import type * as thps4 from "./thps4.ts";
import { COMPAT, editsOf, FACE_INVISIBLE, openWalls, setCompat } from "./thps4_levels.ts";
import { applyEdits, checkEdits, emptyEdits, type LevelEdits, withoutDuplicates } from "./thugpro_edits.ts";

const n = (t: string): Name => new Name(qb.checksum(t));
const node = (name: string, cls: string, extra: qb.Entry[] = []): Struct =>
    new Struct([
        [n("Position"), new Vector("vector", [1, 2, 3])],
        [n("Name"), n(name)],
        [n("Class"), n(cls)],
        [null, n("CreatedAtStart")],
        ...extra,
    ]);

function level(): [Struct[], Map<number, string>] {
    const converted = [
        node("Rail_A", "RailNode", [
            [n("Type"), n("Metal")],
            [n("TerrainType"), n("TERRAIN_METALSMOOTH")],
            [n("Links"), [2]],
        ]),
        node("Meerkat", "LevelObject"),
        node("Rail_B", "RailNode", [[null, n("AbsentInNetGames")]]),
        node("Old_Restart", "Restart"),
        node("Named_Restart", "Restart"),
        node("Box", "LevelGeometry"),
    ];
    const names = new Map<number, string>();
    for (const t of ["Rail_A", "Rail_B", "Old_Restart", "Named_Restart", "Box", "Meerkat"])
        names.set(qb.checksum(t), t);
    return [converted, names];
}

const edits = (): LevelEdits => ({
    ...emptyEdits(),
    moves: { Rail_A: { pos: [10, 20, 30] }, Box: { angles: [0, 1.5, 0] }, Not_Ours: { pos: [0, 0, 0] } },
    renames: { Old_Restart: "New_Restart", Named_Restart: "Other_Restart" },
    flags: { Rail_B: { remove: ["AbsentInNetGames"] }, Box: { remove: ["CreatedAtStart"] } },
    tricks: { Box: { trick_object: true, cluster: "Rail_A" } },
    terrains: { Rail_A: { type: "Metal", terrain: "TERRAIN_GRINDWIRE" } },
    removed: ["Meerkat"],
    added: [
        {
            name: "Add_1",
            class: "RailNode",
            pos: [5, 5, 5],
            angles: [0, 0, 0],
            terrain: "TERRAIN_WOOD",
            links: ["Add_2"],
            created_at_start: true,
        },
        {
            name: "Add_2",
            class: "RailNode",
            pos: [6, 5, 5],
            angles: [0, 0, 0],
            terrain: "TERRAIN_WOOD",
            created_at_start: true,
        },
        {
            name: "Add_Restart",
            class: "Restart",
            type: "Generic",
            pos: [7, 0, 0],
            angles: [0, 3, 0],
            created_at_start: true,
        },
    ],
});

test("TP-22 ... TP-27: applied to our nodes, and the check agrees", () => {
    const [converted, names] = level();
    // (something of ours still names Named_Restart: kept)
    const results = applyEdits(
        "Test",
        edits(),
        converted,
        names,
        { text: "script x {\n    Goto Named_Restart\n}", scripts: new Map() },
        [],
    );
    const by = (t: string): Struct | undefined =>
        converted.find((x) => (x.get("Name") as Name).value === qb.checksum(t));
    // moved: a legacy Position has z negated; Angles set
    assert.deepEqual((by("Rail_A")!.get("Position") as Vector).values, [10, 20, -30]);
    assert.deepEqual((by("Box")!.get("Angles") as Vector).values, [0, 1.5, 0]);
    // flags, trick object, terrain
    assert.ok(!by("Rail_B")!.hasFlag("AbsentInNetGames"));
    assert.ok(!by("Box")!.hasFlag("CreatedAtStart"));
    assert.ok(by("Box")!.hasFlag("TrickObject"));
    assert.equal((by("Box")!.get("Cluster") as Name).value, qb.checksum("Rail_A"));
    assert.equal((by("Rail_A")!.get("TerrainType") as Name).value, qb.checksum("TERRAIN_GRINDWIRE"));
    // renamed unless still named
    assert.ok(by("New_Restart") && !by("Old_Restart"));
    assert.ok(by("Named_Restart") && !by("Other_Restart"));
    assert.deepEqual(results.find((r) => r.register === "TP-23")!.kept, ["Named_Restart"]);
    // removed with the links renumbered (Rail_A -> Rail_B: 2 -> 1)
    assert.equal(by("Meerkat"), undefined);
    assert.deepEqual(by("Rail_A")!.get("Links"), [1]);
    // added at the end, linked by name; zero Angles left off a rail
    assert.equal(converted.length, 8);
    assert.deepEqual(converted[5]!.get("Links"), [6]);
    assert.equal(converted[5]!.get("Angles"), undefined);
    assert.deepEqual(
        (converted[7]!.get("restart_types") as Name[]).map((x) => x.value),
        [qb.checksum("Generic")],
    );
    // the check: everything there matches but the kept rename and the node we don't have
    for (const c of checkEdits(edits(), converted)) {
        if (c.register === "TP-23") assert.deepEqual([c.matching, c.absent], [1, ["Other_Restart"]]);
        else if (c.register === "TP-22") assert.deepEqual([c.matching, c.absent], [2, ["Not_Ours"]]);
        else assert.equal(c.matching, c.total, c.category);
    }
});

test("--compat none: no edits; TP-29 (the shop's door walls) off in the profile", () => {
    setCompat("none");
    const none = editsOf("Sch");
    assert.equal(Object.keys(none.moves).length + none.added.length + none.duplicates.length, 0);
    assert.equal(COMPAT.doorWalls, false);
    const [converted, names] = level();
    const before = converted.map((x) => qb.toNs(x, names)).join();
    applyEdits("Sch", none, converted, names, { text: "", scripts: new Map() }, []);
    assert.equal(converted.map((x) => qb.toNs(x, names)).join(), before);
    setCompat("thugpro");
    assert.equal(COMPAT.doorWalls, false);
});

test("TP-29 on: the skate shop's door walls kept; off: opened", () => {
    const col = (): thps4.ColFile =>
        ({
            objects: [
                {
                    checksum: qb.checksum("Object10"),
                    verts: [
                        [0, 0, 0],
                        [1, 0, 0],
                        [0, 1, 0],
                    ],
                    faces: [[FACE_INVISIBLE, 0, 0, 1, 2]],
                },
            ],
        }) as unknown as thps4.ColFile;
    const open = col();
    openWalls(open, "SkateShop", []);
    assert.equal(open.objects[0]!.faces.length, 0);
    COMPAT.doorWalls = true;
    try {
        const closed = col();
        openWalls(closed, "SkateShop", []);
        assert.equal(closed.objects[0]!.faces.length, 1);
    } finally {
        COMPAT.doorWalls = false;
    }
});

test("TP-28: duplicate faces (the same corners in any order) left out, the first kept", () => {
    const o = {
        verts: [
            [0, 0, 0],
            [1, 0, 0],
            [0, 1, 0],
            [0, 0, 1],
        ],
        faces: [
            [0, 1, 0, 1, 2],
            [0, 2, 2, 0, 1],
            [0, 1, 0, 1, 3],
        ],
    };
    assert.deepEqual(withoutDuplicates(o), [o.faces[0], o.faces[2]]);
});
