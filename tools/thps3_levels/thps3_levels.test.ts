// NOT ORIGINAL - unit tests of the THPS3 converter's THUGPRO compatibility pieces (no game data needed).

import assert from "node:assert/strict";
import { test } from "node:test";
import * as qb from "../level_port/qb.ts";
import { Name, type QbValue, Struct, Vector } from "../level_port/qb.ts";
import { renameScene } from "./migrate_layouts.ts";
import type * as thps3 from "./thps3.ts";
import {
    appendRails,
    applyNodeEdits,
    applySpawns,
    collisionObjects,
    ctfNodes,
    legacyName,
    levelName,
    solidCollision,
} from "./thps3_levels.ts";
import { nodeEdits } from "./thugpro_compare.ts";

const n = (text: string): Name => new Name(qb.checksum(text));
const vec = (...values: number[]): Vector => new Vector("vector", values);

test("level names: THPS3's own for its network levels, thps3_ for the rest and for THUG2's own names", () => {
    const regular = new Struct([
        [n("load_script"), n("Load_Foun")],
        [null, n("regular_level")],
    ]);
    const debug = new Struct([
        [n("load_script"), n("Load_SkateShop")],
        [null, n("debug_level")],
    ]);
    const thug2 = new Set([qb.checksum("Load_AP"), qb.checksum("Level_AP")]);
    assert.equal(levelName("Foun", regular, thug2), "foun");
    assert.equal(levelName("Ap", regular, thug2), "thps3_ap"); // (THUG2's own Load_AP: checksums are case-blind)
    assert.equal(levelName("SkateShop", debug, thug2), "thps3_skateshop");
    assert.equal(levelName("Sk3Ed_Bch", null, thug2), "thps3_sk3ed_bch");
    assert.equal(levelName("Foun", regular, thug2, true), "thps3_foun");
    assert.equal(legacyName("Rio_Sky"), "thps3_rio_sky");
});

test("capture the flag: THPS3's flag bases and team restarts as THUG2's game-mode nodes", () => {
    const names = new Map<number, string>();
    const blue = qb.checksum("CTF_1");
    const array: QbValue[] = [
        new Struct([
            [n("Position"), vec(10, 0, 20)],
            [n("Angles"), vec(0, 1, 0)],
            [n("Name"), n("TRG_CTF_TEAM1")],
            [n("Class"), n("GenericNode")],
            [n("Type"), n("CTF_1")],
        ]),
        new Struct([
            [n("Position"), vec(500, 0, 20)],
            [n("Name"), n("TRG_CTF1_Restart_1P")],
            [n("Class"), n("Restart")],
            [n("restart_types"), [n("MultiPlayer"), n("CTF_1")]],
        ]),
        new Struct([
            [n("Position"), vec(40, 0, 20)],
            [n("Name"), n("TRG_CTF1_Restart_2P")],
            [n("Class"), n("Restart")],
            [n("restart_types"), [n("MultiPlayer"), n("CTF_1")]],
        ]),
        new Struct([
            [n("Position"), vec(11, 0, 20)],
            [n("Name"), n("TRG_Restart_Other")],
            [n("Class"), n("Restart")],
            [n("restart_types"), [n("MultiPlayer")]],
        ]),
    ];
    for (const t of ["GenericNode", "Restart", "TRG_CTF_TEAM1", "TRG_CTF1_Restart_2P"]) names.set(qb.checksum(t), t);
    const [made, scripts, report] = ctfNodes(array, names, [
        { colour: "Blue", type: blue },
        { colour: "Red", type: qb.checksum("CTF_2") }, // (no base in this array: nothing made)
    ]);
    const byName = new Map(made.map((s) => [(s.get("Name") as Name).value, s]));
    assert.deepEqual(
        [...byName.keys()],
        ["TRG_CTF_Blue", "TRG_CTF_Blue_Base", "TRG_CTF_Restart_Blue"].map(qb.checksum),
    );
    const flag = byName.get(qb.checksum("TRG_CTF_Blue"))!;
    assert.deepEqual((flag.get("Position") as Vector).values, [10, 0, 20]);
    assert.equal(flag.get("Model"), "gameobjects\\flags\\flag_blue\\flag_blue.mdl");
    assert.equal((flag.get("TriggerScript") as Name).value, qb.checksum("TRG_CTF_BlueScript"));
    assert.ok(flag.hasFlag("NeverSuspend") && !flag.hasFlag("CreatedAtStart"));
    // the team's restart: its own type's nearest to the base (not the closer restart of another type)
    const restart = byName.get(qb.checksum("TRG_CTF_Restart_Blue"))!;
    assert.deepEqual((restart.get("Position") as Vector).values, [40, 0, 20]);
    assert.deepEqual(
        (restart.get("restart_types") as Name[]).map((t) => t.value),
        ["CTF", "MultiPlayer", "Horse"].map(qb.checksum),
    );
    assert.equal(scripts, "script TRG_CTF_BlueScript {\n    Team_Flag blue \n}\n");
    assert.match(report, /Blue \(base TRG_CTF_TEAM1, restart at TRG_CTF1_Restart_2P\)/);
});

test("capture the flag with the compatibility data (TP-10 / TP-11): places from it, the form and restarts ours", () => {
    const names = new Map<number, string>();
    const array: QbValue[] = [
        new Struct([
            [n("Position"), vec(10, 0, 20)],
            [n("Name"), n("TRG_CTF_TEAM1")],
            [n("Class"), n("GenericNode")],
            [n("Type"), n("CTF_1")],
        ]),
        new Struct([
            [n("Position"), vec(100, 0, -200)], // (THUG2's world: z 200)
            [n("Name"), n("TRG_MP_1")],
            [n("Class"), n("Restart")],
            [n("restart_types"), [n("MultiPlayer"), n("CTF_1")]],
        ]),
        new Struct([
            [n("Position"), vec(900, 0, 0)],
            [n("Name"), n("TRG_MP_2")],
            [n("Class"), n("Restart")],
            [n("restart_types"), [n("Multiplayer")]],
        ]),
    ];
    for (const t of ["GenericNode", "Restart"]) names.set(qb.checksum(t), t);
    const p = (pos: number[]) => ({ pos, angles: [0, 0, 0] });
    const [made, scripts] = ctfNodes(array, names, [{ colour: "Blue", type: qb.checksum("CTF_1") }], {
        flags: { TRG_CTF_Blue: p([90, 0, 210]), TRG_CTF_Blue_Base: p([91, 0, 210]) },
        extra: { TRG_CTF_Green: p([880, 0, 0]), TRG_Flag_Red: p([1, 2, 3]), TRG_Team_Restart: p([4, 5, 6]) },
    });
    const byName = new Map(made.map((s) => [(s.get("Name") as Name).value, s]));
    const posOf = (nm: string): readonly number[] => (byName.get(qb.checksum(nm))!.get("Pos") as Vector).values;
    assert.deepEqual(posOf("TRG_CTF_Blue"), [90, 0, 210]);
    assert.deepEqual(posOf("TRG_CTF_Blue_Base"), [91, 0, 210]);
    assert.deepEqual(posOf("TRG_CTF_Green"), [880, 0, 0]);
    assert.deepEqual(posOf("TRG_Team_Restart"), [4, 5, 6]);
    // restarts: THPS3's (Position), nearest to the flag in THUG2's world
    const restartOf = (c: string) =>
        (byName.get(qb.checksum(`TRG_CTF_Restart_${c}`))!.get("Position") as Vector).values;
    assert.deepEqual(restartOf("Blue"), [100, 0, -200]);
    assert.deepEqual(restartOf("Green"), [900, 0, 0]);
    assert.equal(byName.get(qb.checksum("TRG_Flag_Red"))!.get("Model"), "gameobjects\\flags\\flag_red\\flag_red.mdl");
    assert.match(scripts, /script TRG_Flag_RedScript \{\n {4}Team_Flag red \n\}/);
    assert.match(scripts, /script TRG_CTF_GreenScript \{\n {4}Team_Flag green \n\}/);
});

test("layouts: the scene line renamed, the rest kept byte for byte", () => {
    const old = qb.checksum("thps3_foun");
    const now = qb.checksum("foun");
    const hex = (v: number): string => v.toString(16).toUpperCase().padStart(8, "0");
    const text = `# layout\r\nscene ${hex(old)}  # thps3_foun\r\nsector 12345678 1 0.5 0 0  # x\r\n`;
    const [out, lines] = renameScene(text, old, now, "foun");
    assert.equal(lines, 1);
    assert.equal(out, `# layout\r\nscene ${hex(now)}  # foun\r\nsector 12345678 1 0.5 0 0  # x\r\n`);
    assert.throws(() => renameScene(text, now, old, "x"));
});

test("TP-12: spawns made as THUGPRO's - changed in place, added, removed ones no longer network spawns", () => {
    const names = new Map<number, string>();
    for (const t of ["Restart", "MultiPlayer"]) names.set(qb.checksum(t), t);
    const restart = (name: string, extra: qb.Entry[] = []): Struct =>
        new Struct([
            [n("Position"), vec(1, 2, 3)],
            [n("Name"), n(name)],
            [n("Class"), n("Restart")],
            [n("Type"), n("MultiPlayer")],
            [null, n("CreatedAtStart")],
            [n("restart_types"), [n("MultiPlayer"), n("CTF_1")]],
            ...extra,
        ]);
    const converted = [restart("TRG_A", [[n("TriggerScript"), n("SomeScript")]]), restart("TRG_B")];
    const spawn = { angles: [0, 1, 0], type: "MultiPlayer", restart_name: "Multi: Restart", created: true };
    const report = applySpawns(converted, names, {
        changed: {
            TRG_A: { ...spawn, pos: [7, 8, 9], restart_types: ["MultiPlayer"] },
            TRG_New: { ...spawn, pos: [4, 5, 6], restart_types: ["MultiPlayer", "Horse"] },
        },
        removed: ["TRG_B"],
    });
    assert.equal(report, "TP-12 spawns as THUGPRO's: 1 changed, 1 added, 1 no longer network spawns");
    const [a, b, added] = converted as [Struct, Struct, Struct];
    assert.deepEqual((a.get("Pos") as Vector).values, [7, 8, 9]);
    assert.equal(a.get("Position"), undefined);
    assert.deepEqual(
        (a.get("restart_types") as Name[]).map((x) => x.value),
        [qb.checksum("MultiPlayer")],
    );
    assert.equal((a.get("TriggerScript") as Name).value, qb.checksum("SomeScript")); // (kept)
    assert.equal(a.get("RestartName"), "Multi: Restart");
    assert.ok(a.hasFlag("CreatedAtStart"));
    assert.equal((b.get("Type") as Name).value, qb.checksum("Generic"));
    assert.deepEqual(
        (b.get("restart_types") as Name[]).map((x) => x.value),
        [qb.checksum("Generic")],
    );
    assert.equal((added.get("Name") as Name).value, qb.checksum("TRG_New"));
    assert.deepEqual((added.get("Pos") as Vector).values, [4, 5, 6]);
    assert.equal(applySpawns(converted, names, undefined), "");
});

test("TP-12: the spawns refilled in THUGPRO's order, Links following", () => {
    const names = new Map<number, string>();
    for (const t of ["Restart", "RailNode", "MultiPlayer", "Player1"]) names.set(qb.checksum(t), t);
    const node = (name: string, cls: string, types: string[], links: number[] = []): Struct =>
        new Struct([
            [n("Name"), n(name)],
            [n("Class"), n(cls)],
            ...(types.length ? ([[n("restart_types"), types.map(n)]] as qb.Entry[]) : []),
            ...(links.length ? ([[n("Links"), links]] as qb.Entry[]) : []),
        ]);
    // [A (spawn), rail -> 2, B (spawn), P1 (not a spawn), C (spawn)]
    const converted = [
        node("A", "Restart", ["MultiPlayer"]),
        node("R", "RailNode", [], [2]),
        node("B", "Restart", ["MultiPlayer"]),
        node("P1", "Restart", ["Player1"]),
        node("C", "Restart", ["MultiPlayer"]),
    ];
    const report = applySpawns(converted, names, { changed: {}, removed: [], order: ["C", "A", "B"] });
    assert.match(report, /3 spawns in THUGPRO's order/);
    const order = converted.map((s) => (s.get("Name") as Name).value);
    assert.deepEqual(order, ["C", "R", "A", "P1", "B"].map(qb.checksum));
    assert.deepEqual(converted[1]!.get("Links"), [4]); // (B moved from 2 to 4)
    assert.throws(() => applySpawns(converted, names, { changed: {}, removed: [], order: ["C", "A"] }));
});

test("TP-13 .. TP-17: THUGPRO's node edits applied to THPS3's array in place", () => {
    const names = new Map<number, string>();
    const array: QbValue[] = [
        new Struct([
            [n("Position"), vec(1, 2, 3)],
            [n("Angles"), vec(0, 0, 0)],
            [n("Name"), n("Ped")],
            [n("Class"), n("Pedestrian")],
        ]),
        new Struct([
            [n("Name"), n("Rail")],
            [n("Class"), n("RailNode")],
            [n("TerrainType"), n("TERRAIN_MEATALSMOOTH")],
            [null, n("CreatedAtStart")],
            [null, n("TrickObject")],
        ]),
        new Struct([
            [n("Name"), n("Old")],
            [n("Class"), n("Restart")],
        ]),
        new Struct([
            [n("Name"), n("Box")],
            [n("Class"), n("EnvironmentObject")],
        ]),
    ];
    const before = array[0];
    const [report, removed] = applyNodeEdits(array, names, {
        moved: { Ped: { pos: [10, 20, 30], angles: [0, 1, 0] } },
        flags: { Rail: { add: ["AbsentInNetGames"], remove: ["CreatedAtStart", "TrickObject"] } },
        terrain: { Rail: "TERRAIN_METALSMOOTH" },
        removed: ["Box", "NotThere"],
        renamed: { Old: "New" },
        rails: [],
    });
    assert.match(
        report,
        /1 moved \(TP-13\), 1 flags changed \(TP-14\), 1 terrains \(TP-15\), 2 removed \(TP-16\), 1 renamed/,
    );
    assert.notEqual(array[0], before); // (a copy: q's other users see the edited node)
    assert.deepEqual((array[0] as Struct).get("Position"), vec(10, 20, -30)); // (THPS3's Position: z negated)
    assert.deepEqual((array[0] as Struct).get("Angles"), vec(0, 1, 0));
    const rail = array[1] as Struct;
    assert.ok(rail.hasFlag("AbsentInNetGames") && !rail.hasFlag("CreatedAtStart") && !rail.hasFlag("TrickObject"));
    assert.deepEqual(rail.get("TerrainType"), n("TERRAIN_METALSMOOTH"));
    assert.deepEqual((array[2] as Struct).get("Name"), n("New"));
    assert.ok(removed.has(qb.checksum("Box")));
    assert.deepEqual(applyNodeEdits(array, names, undefined), ["", new Set()]);
});

test("TP-18: added rails as our rail nodes, Links by name, a missing cluster left out with TrickObject", () => {
    const names = new Map<number, string>();
    const converted = [
        new Struct([
            [n("Name"), n("Banner")],
            [n("Class"), n("LevelGeometry")],
        ]),
    ];
    const rail = (name: string, cluster: string | null, links: string[]) => ({
        name,
        pos: [1, 2, 3],
        angles: [0, 0, 0],
        terrain: "TERRAIN_GRINDMETAL",
        flags: ["CreatedAtStart", "TrickObject"],
        cluster,
        links,
    });
    const report = appendRails(converted, names, [rail("A", "Banner", ["B"]), rail("B", "Wire", [])]);
    assert.match(report, /2 rails THUGPRO added \(1 without their cluster/);
    const [a, b] = [converted[1]!, converted[2]!];
    assert.deepEqual(a.get("Links"), [2]);
    assert.deepEqual(a.get("Cluster"), n("Banner"));
    assert.ok(a.hasFlag("TrickObject"));
    assert.equal(b.get("Cluster"), undefined);
    assert.ok(!b.hasFlag("TrickObject") && b.hasFlag("CreatedAtStart"));
    assert.deepEqual(b.get("Pos"), vec(1, 2, 3));
    assert.throws(() => appendRails(converted, names, [rail("C", null, ["Nowhere"])]));
    assert.equal(appendRails(converted, names, []), "");
});

test("TP-19: collision edits - an object's collision dropped, faces taken out by their corners and turning", () => {
    const sector = (checksum: number): thps3.Sector => ({
        checksum,
        materialBase: 0,
        positions: [
            [0, 0, 0],
            [1, 0, 0],
            [0, 1, 0],
        ],
        uvs: [],
        uvSets: 0,
        // (the same triangle both ways round, then the first again from another corner)
        triangles: [
            [0, 0, 1, 2],
            [0, 0, 2, 1],
            [0, 1, 2, 0],
        ],
        binmesh: null,
        faceFlags: [0, 0, 0],
    });
    const materials = [{ ns: null } as unknown as thps3.RwMaterial];
    const counts = { dropped: 0, faces: 0, missing: 0 };
    const edits = new Map([
        [1, { drop: true }],
        [
            2,
            {
                remove: [
                    [
                        [1, 0, 0],
                        [0, 1, 0],
                        [0, 0, 0],
                    ],
                    [
                        [5, 5, 5],
                        [0, 1, 0],
                        [0, 0, 0],
                    ],
                ],
            },
        ],
    ]);
    const out = collisionObjects([sector(1), sector(2), sector(3)], materials, new Set(), edits, counts);
    assert.deepEqual(
        out.objects.map((o) => [o.checksum, o.faces.length]),
        [
            [2, 2],
            [3, 3],
        ],
    );
    // (the first face with those corners in that turning went - (0 1 2); the other way round stayed)
    assert.deepEqual(
        out.objects[0]!.faces.map((f) => f.slice(2)),
        [
            [0, 2, 1],
            [1, 2, 0],
        ],
    );
    assert.deepEqual(counts, { dropped: 1, faces: 1, missing: 1 });
});

test("THUGPRO's node edits found against THPS3's array (nodeEdits)", () => {
    const at = (name: string, cls: string, pos: number[], extra: qb.Entry[] = []): Struct =>
        new Struct([[n("Position"), vec(...pos)], [n("Name"), n(name)], [n("Class"), n(cls)], ...extra]);
    const own: QbValue[] = [
        at("Ped", "Pedestrian", [0, 0, 0]),
        at(
            "Rail",
            "RailNode",
            [5, 0, 0],
            [
                [null, n("CreatedAtStart")],
                [n("TerrainType"), n("TERRAIN_WOOD")],
            ],
        ),
        at("Light", "EnvironmentObject", [7, 0, 0], [[null, n("CreatedAtStart")]]),
        at("Gone", "EnvironmentObject", [9, 0, 0]),
        at("Named", "EnvironmentObject", [11, 0, 0]),
        at("OldName", "Restart", [13, 0, 0]),
    ];
    const theirs: QbValue[] = [
        at("Ped", "Pedestrian", [0, 0, 10]),
        at(
            "Rail",
            "RailNode",
            [5, 0, 0],
            [
                [null, n("AbsentInNetGames")],
                [n("TerrainType"), n("TERRAIN_METALSMOOTH")],
            ],
        ),
        at("Light", "GenericNode", [7, 0, 0]),
        at("NewName", "Restart", [13, 0, 0]),
        at(
            "Added",
            "RailNode",
            [1, 1, 1],
            [
                [n("Links"), [5]],
                [n("Cluster"), n("Wire")],
                [null, n("TrickObject")],
            ],
        ),
        at("Added2", "RailNode", [2, 2, 2]),
    ];
    const names = new Map<number, string>();
    for (const t of [
        ...["Ped", "Rail", "Light", "Gone", "Named", "OldName", "NewName", "Added", "Added2", "Wire"],
        ...["Pedestrian", "RailNode", "EnvironmentObject", "GenericNode", "Restart"],
        ...["TERRAIN_WOOD", "TERRAIN_METALSMOOTH"],
    ])
        names.set(qb.checksum(t), t);
    const uses = new Map([[qb.checksum("Named"), 2]]); // (its own Name and a script)
    const edits = nodeEdits(own, names, theirs, names, uses);
    assert.deepEqual(Object.keys(edits.moved), ["Ped"]);
    assert.deepEqual(edits.moved.Ped!.pos, [0, 0, -10]); // (THUGPRO's Position, z negated: THUG2's world)
    assert.deepEqual(edits.flags, { Rail: { add: ["AbsentInNetGames"], remove: ["CreatedAtStart"] } });
    assert.deepEqual(edits.terrain, { Rail: "TERRAIN_METALSMOOTH" });
    assert.deepEqual(edits.renamed, { OldName: "NewName" });
    assert.deepEqual(edits.removed, ["Gone"]); // (Named: a script uses it)
    assert.deepEqual(
        edits.rails.map((r) => [r.name, r.links, r.cluster, r.flags]),
        [
            ["Added", ["Added2"], "Wire", ["TrickObject"]],
            ["Added2", [], null, []],
        ],
    );
    assert.ok(edits.notes.some((x) => /Light: class environmentobject -> genericnode/.test(x)));
    assert.ok(edits.notes.some((x) => /removed Named: used by a script/.test(x)));
});

test("PC-THPS3-15: a solid moving sector's collision - every face, flags 0 (clumps have none), its material's terrain", () => {
    const sector = (checksum: number): thps3.Sector => ({
        checksum,
        materialBase: 0,
        positions: [
            [-5, 0, -5],
            [5, 0, -5],
            [0, 10, 5],
        ],
        uvs: [],
        uvSets: 0,
        triangles: [
            [1, 0, 1, 2],
            [1, 0, 2, 1],
        ],
        binmesh: null,
    });
    const materials = [{ ns: null }, { ns: { terrain: 7 } }] as unknown as thps3.RwMaterial[];
    const out = solidCollision([sector(1), sector(2), sector(3)], materials, new Set([2, 3]), new Set([3]));
    assert.deepEqual(
        out.map((o) => o.checksum),
        [2],
    );
    assert.deepEqual(out[0]!.faces, [
        [0, 7, 0, 1, 2],
        [0, 7, 0, 2, 1],
    ]);
    assert.deepEqual(out[0]!.bbox, [-5, 0, -5, 1, 5, 10, 5, 1]);
    assert.deepEqual(solidCollision([sector(1)], materials, new Set(), new Set()), []);
});
