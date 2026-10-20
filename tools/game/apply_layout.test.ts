// NOT ORIGINAL - tests of tools/game/apply_layout.ts, layout_file.ts and tools/level_port/qb_write.ts on a small level
// made here (a scene of two sectors, their collision and an invisible wall, a node array with a shared structure) and
// small layouts covering every record kind. No game data.

import assert from "node:assert/strict";
import { test } from "node:test";
import { checksum, Name, QbFile, type QbValue, Struct, Vector } from "../level_port/qb.ts";
import { encodeValue, replaceGlobalArray } from "../level_port/qb_write.ts";
import { buildTree, type ColFile, type ColObject, readCol, setReaches, writeCol } from "../level_port/thug2_col.ts";
import { readScene, type Scene, type Sector, writeScene } from "../level_port/thug2_scene.ts";
import { Float } from "../lib/py.ts";
import { pack } from "../lib/struct.ts";
import {
    anglesOfRows,
    applyCrownOrder,
    applyPlacement,
    bake,
    installRefusal,
    type LevelFiles,
    quarterTurn,
} from "./apply_layout.ts";
import { decodeValue, LayoutError, parseName, readLayout } from "./layout_file.ts";

const LEVEL = "tst";
const NAME = (text: string): Name => new Name(checksum(text));

// A sector of one triangle: positions, normals (pointing +x), a colour and one uv per vertex (stride 36).
function sector(name: string, at: [number, number, number]): Sector {
    const parts: Buffer[] = [];
    const points = [
        [at[0], at[1], at[2]],
        [at[0] + 10, at[1], at[2]],
        [at[0], at[1] + 10, at[2] + 5],
    ];
    for (const p of points)
        parts.push(pack("<3f", ...p), pack("<3f", 1, 0, 0), pack("<4B", 128, 128, 128, 255), pack("<2f", 0, 0));
    const mesh = {
        sphere: [0, 0, 0, 1],
        bbox: [0, 0, 0, 1, 1, 1],
        flags: 0,
        material: 0,
        lods: [[0, 1, 2]],
        strips: [[0, 1, 2]],
        stride: 36,
        vertexCount: 3,
        buffers: [[0, Buffer.concat(parts)]] as [number, Buffer][],
        vertexShader: [0x152, 0],
        offsets: [12, 24, 28],
        hasWibble: 0,
        indexSets: 1,
        pixelShader: 2,
    };
    return {
        checksum: checksum(name),
        bone: -1,
        flags: 0,
        bbox: [at[0], at[1], at[2], at[0] + 10, at[1] + 10, at[2] + 5],
        sphere: [0, 0, 0, 1],
        meshes: [mesh],
    };
}

function collision(name: string, at: [number, number, number]): ColObject {
    const verts = [
        [at[0], at[1], at[2]],
        [at[0] + 10, at[1], at[2]],
        [at[0], at[1] + 10, at[2] + 5],
    ];
    const o: ColObject = {
        checksum: checksum(name),
        flags: 0,
        smallFaces: 1,
        fixedVerts: 0,
        bbox: [at[0], at[1], at[2], 1, at[0] + 10, at[1] + 10, at[2] + 5, 1],
        verts,
        intensities: Buffer.alloc(3, 0x80),
        faces: [[1, 0, 0, 1, 2]],
        reach: Buffer.alloc(0),
        bsp: { kind: "leaf", faces: [], low: 3 },
    };
    o.bsp = buildTree(o.verts, o.faces);
    setReaches(o);
    return o;
}

function struct(entries: [string | null, QbValue][]): Struct {
    return new Struct(entries.map(([k, v]) => [k === null ? null : NAME(k), v]));
}
const vec = (x: number, y: number, z: number): Vector => new Vector("vector", [x, y, z]);

// The level's .qb: a shared structure and its node array (the names in the name table).
function levelQb(): Buffer {
    const ncomp = struct([
        [null, NAME("CreatedAtStart")],
        ["RestartName", "P1"],
    ]);
    const nodes = [
        struct([
            ["Pos", vec(0, 0, 0)],
            ["Angles", vec(0, 0, 0)],
            ["Name", NAME("TRG_Restart01")],
            ["Class", NAME("Restart")],
            ["Type", NAME("Player1")],
            ["restart_types", [NAME("Player1")]],
            [null, NAME("ncomp_Restart_1")],
        ]),
        struct([
            ["Pos", vec(100, 0, 0)],
            ["Name", NAME("Rail01")],
            ["Class", NAME("RailNode")],
            ["TerrainType", NAME("TERRAIN_GRINDMETAL")],
            ["Links", [2]],
        ]),
        struct([
            ["Pos", vec(200, 0, 0)],
            ["Name", NAME("Rail02")],
            ["Class", NAME("RailNode")],
            ["TerrainType", NAME("TERRAIN_GRINDMETAL")],
            ["Links", [3]],
        ]),
        struct([
            ["Pos", vec(300, 0, 0)],
            ["Name", NAME("Rail03")],
            ["Class", NAME("RailNode")],
            ["TerrainType", NAME("TERRAIN_GRINDMETAL")],
        ]),
        struct([
            ["Pos", vec(0, 0, 50)],
            ["Name", NAME("Crown01")],
            ["Class", NAME("GenericNode")],
            ["Type", NAME("Crown")],
            [null, NAME("CreatedAtStart")],
            ["Radius", new Float(10)],
        ]),
        struct([
            ["Pos", vec(5, 0, 5)],
            ["Angles", vec(0, 0, 0)],
            ["Name", NAME("Obj01")],
            ["Class", NAME("LevelObject")],
        ]),
    ];
    const names = [
        "CreatedAtStart",
        "RestartName",
        "Pos",
        "Angles",
        "Name",
        "Class",
        "Type",
        "restart_types",
        "Restart",
        "Player1",
        "ncomp_Restart_1",
        "TRG_Restart01",
        "Rail01",
        "Rail02",
        "Rail03",
        "RailNode",
        "TerrainType",
        "TERRAIN_GRINDMETAL",
        "Links",
        "Crown01",
        "GenericNode",
        "Crown",
        "Radius",
        "Obj01",
        "LevelObject",
        `${LEVEL}_NodeArray`,
    ];
    const global = (name: string, value: QbValue): Buffer =>
        Buffer.concat([encodeValue(NAME(name)), Buffer.from([0x07]), encodeValue(value), Buffer.from([0x01])]);
    return Buffer.concat([
        global("ncomp_Restart_1", ncomp),
        global(`${LEVEL}_NodeArray`, nodes),
        ...names.map((n) =>
            Buffer.concat([Buffer.from([0x2b]), pack("<I", checksum(n)), Buffer.from(`${n}\0`, "latin1")]),
        ),
        Buffer.from([0x00]),
    ]);
}

function level(): LevelFiles {
    const scene: Scene = {
        versions: [1, 1, 1],
        materials: [],
        sectors: [sector("SecA", [0, 0, 0]), sector("SecB", [500, 0, 0])],
        hierarchyCount: 0,
        tail: Buffer.alloc(0),
    };
    const col: ColFile = {
        objects: [
            collision("SecA", [0, 0, 0]),
            collision("SecB", [500, 0, 0]),
            collision("Wall1", [0, 0, 900]),
            collision("Loose", [0, 0, 0]),
            collision("Far", [0, 0, 300]),
        ],
    };
    // (through the writers and readers: what the tool reads from disk)
    return {
        level: LEVEL,
        scene: readScene(writeScene(scene), "scn"),
        col: readCol(writeCol(col), "col"),
        qb: levelQb(),
    };
}

const hex = (name: string): string => checksum(name).toString(16).toUpperCase().padStart(8, "0");

function nodesOf(qb: Buffer): Struct[] {
    const file = new QbFile(qb, "tst.qb", true);
    const array = file.globals.find(([k]) => k.value === checksum(`${LEVEL}_NodeArray`))![1];
    return (array as QbValue[]).map((n) => n as Struct);
}
function named(nodes: Struct[], name: string): Struct {
    const n = nodes.find((s) => (s.get("Name") as Name | undefined)?.value === checksum(name));
    assert.ok(n, `node ${name}`);
    return n;
}
function vertex(s: Sector, v: number): number[] {
    const data = s.meshes[0]!.buffers[0]![1];
    return [0, 4, 8, 12, 16, 20].map((o) => data.readFloatLE(v * 36 + o));
}

// --- layout_file ----------------------------------------------------------------------------------------------------

test("layout_file reads every record kind the editor writes", () => {
    const text = [
        "thug2-decomp layout 2",
        `scene ${hex(LEVEL)}  # tst`,
        "level name=My%20Level start=00000001 tod=night time=0 ambient=1,2,3 fog=4,5,6,7,800 later=1",
        `sector ${hex("SecA")} 1 10 0 0 0 scale=2`,
        `rail ${hex("Rail01")} 0 0 5 0 0`,
        `clone ${hex("SecB")} 0 0 0 100`,
        `object ${hex("Obj01")} 1 2 3 1 0 0 0 1 0 0 0 1`,
        `copy 0000ABCD ${hex("Obj01")} 1 2 3 1 0 0 0 1 0 0 0 1`,
        "node ed_crown01 GenericNode type=Crown pos=1,2,3 angles=0,1,0",
        "order crown ed_crown01",
        `hide ${hex("Crown01")}`,
        `lock sector ${hex("SecA")}`,
        `set ${hex("TRG_Restart01")} pos=1,2,3 f.RestartName=s:Start%20here f.0x12345678=del f.CreatedAtStart=flag:0`,
        `point ed_rail01 RailNode pos=1,2,3 like=${hex("Rail01")} links=${hex("Rail02")}`,
        `links ${hex("Rail01")} -`,
        `drop ${hex("Rail03")}`,
        `terrain ${hex("Rail02")} TERRAIN_GRINDWOOD`,
        `wall ${hex("Wall1")} 0 0 0 16 1`,
        'trigger ed_gap01 GapStart pos=1,2,3 yaw=0 size=10,10,10 gap=7 text="A gap" replaces=0000ABCD',
        "mystery 1 2 3",
    ].join("\n");
    const l = readLayout(text);
    assert.equal(l.scene, checksum(LEVEL));
    assert.deepEqual(l.level, {
        name: "My Level",
        start: 1,
        tod: "night",
        time: 0,
        ambient: [1, 2, 3],
        fog: [4, 5, 6, 7, 800],
    });
    assert.equal(l.sectors[0]!.placement.scale, 2);
    assert.equal(l.sectors[0]!.placement.quarters, 1);
    assert.equal(l.rails.length, 1);
    assert.equal(l.clones[0]!.placement.offset.z, 100);
    assert.equal(l.copies[0]!.name, 0xabcd);
    assert.equal(l.nodes[0]!.type, "Crown");
    assert.deepEqual(l.crownOrder, ["ed_crown01"]);
    assert.equal(l.sets[0]!.fields.length, 3);
    assert.equal(l.sets[0]!.fields[0]!.value?.text, "Start here");
    assert.equal(l.sets[0]!.fields[1]!.removed, true);
    assert.equal(l.sets[0]!.fields[2]!.value?.on, false);
    assert.equal(l.points[0]!.like, checksum("Rail01"));
    assert.deepEqual(l.links[0]!.links, []);
    assert.deepEqual(l.dropped, [checksum("Rail03")]);
    assert.equal(l.walls[0]!.off, true);
    assert.equal(l.unknown[0]!.kind, "mystery");
    assert.equal(l.triggers[0]!.kind, "GapStart");
    assert.equal(l.triggers[0]!.fields.text, '"A gap"');
});

test("layout_file refuses what the DLL refuses, with the line", () => {
    const bad = (line: string): void => {
        assert.throws(
            () => readLayout(`thug2-decomp layout 2\nscene 00000001\n${line}\n`),
            (e: unknown) => {
                return e instanceof LayoutError && e.line === 3;
            },
        );
    };
    bad("sector 00000001 5 0 0 0 0");
    bad("set 00000001 f.Radius=f:1 f.Radius=f:2");
    bad("set 00000001 f.Radius=x:1");
    bad("node bad-name GenericNode pos=0,0,0");
    bad("node ed_x GenericNode type=Crown");
    bad("level tod=noon");
    bad("wall 00000001 0 0 0 0 2");
    assert.throws(() => readLayout("not a layout\n"), LayoutError);
});

test("field values decode as the DLL writes them (level_editor_fields.h)", () => {
    assert.deepEqual(decodeValue("i:-7"), { kind: "integer", integer: -7 });
    assert.deepEqual(decodeValue("v:1,2.5,-3"), { kind: "vector", numbers: [1, 2.5, -3] });
    assert.equal(decodeValue("n:0x0BADF00D")?.checksum, 0x0badf00d);
    assert.equal(decodeValue("n:TriggerScript")?.checksum, checksum("triggerscript"));
    assert.equal(decodeValue("s:a%20b%23c%3D%25")?.text, "a b#c=%");
    assert.deepEqual(decodeValue("an:")?.names, []);
    assert.deepEqual(decodeValue("ai:1,-5")?.names, [1, -5]);
    for (const bad of ["x:1", "i:1.5", "flag:2", "s:%4", "s:%00", "12"]) assert.equal(decodeValue(bad), null, bad);
    assert.equal(parseName("#1A2B3C4D"), 0x1a2b3c4d);
    assert.equal(parseName("2fast"), null);
});

// --- qb_write -------------------------------------------------------------------------------------------------------

test("qb_write: a node array written back keeps every untouched node's bytes", () => {
    const qb = levelQb();
    const name = checksum(`${LEVEL}_NodeArray`);
    const nodes = nodesOf(qb);
    const same = replaceGlobalArray(
        qb,
        name,
        nodes.map((_, i) => ({ keep: i })),
        new Map(),
        true,
    );
    assert.ok(same.equals(qb));
    const whole = replaceGlobalArray(qb, name, nodes);
    assert.deepEqual(JSON.stringify(nodesOf(whole)), JSON.stringify(nodes));
    const extra = replaceGlobalArray(
        qb,
        name,
        [{ keep: 0 }, { value: struct([["Name", NAME("New01")]]) }],
        new Map([[checksum("New01"), "New01"]]),
        true,
    );
    const back = new QbFile(extra, "x", true);
    assert.equal(nodesOf(extra).length, 2);
    assert.equal(back.names.get(checksum("New01")), "New01");
});

// --- the bake -------------------------------------------------------------------------------------------------------

test("placements as the editor's: QuarterTurn(x * scale) + offset", () => {
    assert.deepEqual(quarterTurn({ x: 1, y: 2, z: 3 }, 1), { x: 3, y: 2, z: -1 });
    assert.deepEqual(applyPlacement({ quarters: 2, scale: 2, offset: { x: 1, y: 0, z: 0 } }, { x: 1, y: 1, z: 1 }), {
        x: -1,
        y: 2,
        z: -2,
    });
    const { angles, tilted } = anglesOfRows([0, 0, -1, 0, 1, 0, 1, 0, 0]);
    assert.ok(Math.abs(angles.y - Math.PI / 2) < 1e-6 && !tilted);
    assert.ok(anglesOfRows([1, 0, 0, 0, 0, 1, 0, -1, 0]).tilted);
});

test("bake: scenery moved, copied, hidden; collision moved and switched off", () => {
    const layout = readLayout(
        [
            "thug2-decomp layout 2",
            `scene ${hex(LEVEL)}`,
            `sector ${hex("SecA")} 1 100 0 0 0`,
            `sector ${hex("SecB")} 0 0 0 0 1`,
            `clone ${hex("SecA")} 0 0 0 1000 scale=2`,
            `wall ${hex("Wall1")} 0 0 16 0 1`,
        ].join("\n"),
    );
    const out = bake(layout, level(), null);
    const a = out.scene.sectors.find((s) => s.checksum === checksum("SecA"))!;
    // (10, 0, 0) turned a quarter -> (0, 0, -10), + (100, 0, 0); its normal (1, 0, 0) -> (0, 0, -1)
    assert.deepEqual(vertex(a, 1), [100, 0, -10, 0, 0, -1]);
    assert.deepEqual(a.bbox, [100, 0, -10, 105, 10, 0]);
    assert.equal(
        out.scene.sectors.some((s) => s.checksum === checksum("SecB")),
        false,
        "hidden: left out",
    );
    const copy = out.scene.sectors.find((s) => s.checksum === checksum(`ed_clone_${hex("SecA")}_1`))!;
    assert.deepEqual(vertex(copy, 1).slice(0, 3), [20, 0, 1000], "the copy placed from its source as loaded");
    const colA = out.col!.objects.find((o) => o.checksum === checksum("SecA"))!;
    assert.deepEqual(colA.verts[1], [100, 0, -10]);
    assert.equal(
        out.col!.objects.some((o) => o.checksum === checksum("SecB")),
        false,
    );
    assert.equal(
        out.col!.objects.some((o) => o.checksum === checksum("Wall1")),
        false,
        "a wall switched off",
    );
    assert.ok(out.col!.objects.some((o) => o.checksum === checksum(`ed_clone_${hex("SecA")}_1`)));
    // The files written read back.
    readScene(writeScene(out.scene), "scn");
    readCol(writeCol(out.col!), "col");
    // Shared collision: the collision-only object's face on SecA's triangle went with it (a companion), the original off.
    const loose = out.col!.objects.find((o) => o.checksum === checksum("Loose"))!;
    assert.equal(loose.faces[0]![0] & 0x10, 0x10, "the original face switched off");
    const far = out.col!.objects.find((o) => o.checksum === checksum("Far"))!;
    assert.equal(far.faces[0]![0] & 0x10, 0, "a face away from the piece stays");
    const companion = out.col!.objects.find((o) => o.checksum === checksum(`ed_coll_${hex("SecA")}_1`))!;
    assert.ok(companion, "the piece's companion");
    assert.equal(companion.faces.length, 1);
    assert.deepEqual(companion.verts[1], [100, 0, -10], "placed with the piece");
    assert.ok(
        out.col!.objects.some((o) => o.checksum === checksum(`ed_coll_${hex("SecA")}_2`)),
        "and the copy's",
    );
    assert.deepEqual(out.parity, [], "not a shared level: no parity warnings");
});

test("bake: nodes set, added, hidden, relinked; fields over a shared structure", () => {
    const layout = readLayout(
        [
            "thug2-decomp layout 2",
            `scene ${hex(LEVEL)}`,
            "level tod=night",
            `rail ${hex("Rail01")} 0 0 10 0 0`,
            `object ${hex("Obj01")} 7 8 9 0 0 -1 0 1 0 1 0 0`,
            `set ${hex("TRG_Restart01")} pos=1,2,3 f.RestartName=s:Start%20here f.restart_types=an:Player1,Multiplayer f.CreatedAtStart=flag:0`,
            `set ${hex("Crown01")} f.Radius=f:25 f.CreatedAtStart=flag:0`,
            "node ed_crown01 GenericNode type=Crown pos=1,2,3 angles=0,0,0",
            "node ed_restart01 Restart type=MultiPlayer pos=4,5,6 angles=0,1,0 types=MultiPlayer,Horse",
            `point ed_rail01 RailNode pos=400,0,0 like=${hex("Rail03")} links=${hex("Rail01")}`,
            `links ${hex("Rail03")} ${hex("ed_rail01")}`,
            `drop ${hex("Rail02")}`,
            `links ${hex("Rail01")} ${hex("Rail03")}`,
            `terrain ${hex("Rail03")} TERRAIN_GRINDWOOD`,
            `hide ${hex("Crown01")}`,
            `trigger ed_kill01 Kill pos=0,0,0 yaw=0 size=50,50,50 target=TRG_Restart01 replaces=${hex("Wall1")}`,
            `wall ${hex("Wall1")} 0 0 0 0 1`,
            "mystery 1",
        ].join("\n"),
    );
    const out = bake(layout, level(), { level: LEVEL, why: "a test" });
    const nodes = nodesOf(out.qb);
    assert.equal(nodes.length, 9, "6 of the level's + 2 nodes + 1 point appended");
    const restart = named(nodes, "TRG_Restart01");
    assert.deepEqual((restart.get("Pos") as Vector).values, [1, 2, 3]);
    // RestartName comes from the shared structure: the node's own override at its end; the shared flag can't go.
    const last = restart.entries[restart.entries.length - 1]!;
    assert.equal(last[0]?.value, checksum("RestartName"));
    assert.equal(last[1], "Start here");
    assert.deepEqual(
        (restart.get("restart_types") as Name[]).map((n) => n.value),
        [checksum("Player1"), checksum("Multiplayer")],
    );
    assert.equal(
        restart.entries.findIndex(([k]) => k?.value === checksum("restart_types")),
        5,
        "own field: in place",
    );
    assert.ok(out.warnings.some((w) => w.includes("shared structure's flag")));
    const crown = named(nodes, "Crown01");
    assert.equal((crown.get("Radius") as Float).value, 25);
    assert.equal(crown.hasFlag("CreatedAtStart"), false, "an own flag taken out");
    assert.equal((crown.get("Class") as Name).value, checksum("ed_hidden"), "hidden: the editor's marker");
    assert.equal((crown.get("ed_hidden_type") as Name).value, checksum("Crown"));
    assert.deepEqual((named(nodes, "Rail01").get("Pos") as Vector).values, [100, 10, 0]);
    assert.deepEqual(named(nodes, "Rail01").get("Links"), [3], "relinked past the dropped point (by index)");
    assert.equal((named(nodes, "Rail02").get("Class") as Name).value, checksum("ed_hidden"));
    assert.equal((named(nodes, "Rail03").get("TerrainType") as Name).value, checksum("TERRAIN_GRINDWOOD"));
    assert.deepEqual(named(nodes, "Rail03").get("Links"), [8], "links to an added point: its new index");
    const obj = named(nodes, "Obj01");
    assert.deepEqual((obj.get("Pos") as Vector).values, [7, 8, 9]);
    assert.ok(Math.abs((obj.get("Angles") as Vector).values[1]! - Math.PI / 2) < 1e-6);
    const added = named(nodes, "ed_restart01");
    assert.ok(added.hasFlag("CreatedAtStart"));
    assert.equal(added.get("RestartName"), "Multi: Restart");
    assert.equal(nodes.indexOf(named(nodes, "ed_crown01")), 6);
    const point = named(nodes, "ed_rail01");
    assert.equal((point.get("TerrainType") as Name).value, checksum("TERRAIN_GRINDMETAL"), "copied from its like node");
    assert.deepEqual(point.get("Links"), [1]);
    assert.ok(point.hasFlag("CreatedAtStart"));
    // Untouched nodes keep their bytes; the names went into the name table.
    const file = new QbFile(out.qb, "tst.qb", true);
    assert.equal(file.names.get(checksum("ed_hidden")), "ed_hidden");
    assert.equal(file.names.get(checksum("ed_rail01")), "ed_rail01");
    // Reported: the level properties, the unknown record, the parity of spawns / crowns / rails.
    assert.ok(out.warnings.some((w) => w.startsWith("level properties not baked") && w.includes("tod_state=night")));
    assert.ok(out.unsupported.some((u) => u.includes('"trigger"') && u.includes("ed_kill01")));
    assert.ok(out.unsupported.some((u) => u.includes('"mystery"')));
    assert.ok(
        out.warnings.some((w) => w.includes("kept on")),
        "the replaced level trigger stays",
    );
    assert.ok(out.col!.objects.some((o) => o.checksum === checksum("Wall1")));
    assert.ok(out.parity.some((p) => p.startsWith("trigger ed_kill01")));
    for (const what of [
        "a spawn (restart) TRG_Restart01 moved",
        "a crown (King of the Hill) Crown01 hidden",
        "rail point Rail01 moved",
    ]) {
        assert.ok(
            out.parity.some((p) => p.startsWith(what) && p.includes("this breaks online parity")),
            what,
        );
    }
});

test("bake: added crowns keep the layout's order", () => {
    const list = [
        { name: "a", cls: "GenericNode", type: "Crown" },
        { name: "x", cls: "Restart", type: "" },
        { name: "b", cls: "GenericNode", type: "Crown" },
    ];
    assert.deepEqual(
        applyCrownOrder(list, ["b", "a"]).map((n) => n.name),
        ["b", "x", "a"],
    );
});

test("install refuses the backups and THUGPRO, and the main install without --allow-main", () => {
    assert.match(installRefusal("C:\\Games\\THUG2_PM_BAK", true) ?? "", /never/);
    assert.match(installRefusal("C:\\Games\\THUGPRO\\x", true) ?? "", /never/);
    assert.match(installRefusal("C:\\Games\\THUG2\\Game", false) ?? "", /main install/);
    assert.equal(installRefusal("C:\\Games\\THUG2\\Game", true), null);
    assert.equal(installRefusal("C:\\Games\\THUG2-1\\Game", false), null);
});

// (phase 4b - level_editor_chains.h) ladders drawn and turned, drawn rails as trick objects, rails grindable on flat ground.
test("bake: ladders, trick-object rails, flat-ground rails", () => {
    const layout = readLayout(
        [
            "thug2-decomp layout 2",
            `scene ${hex(LEVEL)}`,
            `point ed_ladder01 ClimbingNode pos=10,0,20 links=${hex("ed_ladder02")} facing=1.5`,
            "point ed_ladder02 ClimbingNode pos=10,200,20 facing=1.5",
            `point ed_rail01 RailNode pos=0,0,0 links=${hex("ed_rail02")} cluster=${hex("SecA")} grindflat=1`,
            "point ed_rail02 RailNode pos=50,0,0 cluster=00000ABC",
            `facing ${hex("Rail01")} -0.5`,
            `grindflat ${hex("Rail02")}`,
        ].join("\n"),
    );
    assert.equal(layout.points[0]!.facing, 1.5);
    assert.equal(layout.points[2]!.cluster, checksum("SecA"));
    assert.equal(layout.points[2]!.grindFlat, true);
    assert.deepEqual(layout.facings, [{ name: checksum("Rail01"), facing: -0.5 }]);
    assert.deepEqual(layout.grindFlat, [checksum("Rail02")]);
    const out = bake(layout, level(), { level: LEVEL, why: "a test" });
    const nodes = nodesOf(out.qb);
    const foot = named(nodes, "ed_ladder01");
    assert.equal((foot.get("Class") as Name).value, checksum("ClimbingNode"));
    assert.equal((foot.get("Type") as Name).value, checksum("Ladder"), "a ladder's point, as THUG2's");
    assert.deepEqual((foot.get("Angles") as Vector).values, [0, 1.5, 0]);
    assert.ok(foot.hasFlag("CreatedAtStart"));
    assert.equal(foot.get("TerrainType"), undefined, "no terrain on a ladder");
    assert.deepEqual(foot.get("Links"), [nodes.indexOf(named(nodes, "ed_ladder02"))]);
    const rail = named(nodes, "ed_rail01");
    assert.ok(rail.hasFlag("TrickObject"));
    assert.equal((rail.get("Cluster") as Name).value, checksum("SecA"));
    assert.ok(rail.hasFlag("ed_grind_flat"));
    const other = named(nodes, "ed_rail02");
    assert.equal((other.get("Cluster") as Name).value, 0xabc);
    assert.equal(other.hasFlag("ed_grind_flat"), false);
    assert.deepEqual((named(nodes, "Rail01").get("Angles") as Vector).values, [0, -0.5, 0], "a level ladder turned");
    assert.ok(named(nodes, "Rail02").hasFlag("ed_grind_flat"));
    assert.ok(out.warnings.some((w) => w.startsWith("2 rail point(s) grindable on flat ground")));
    for (const what of ["ladder point ed_ladder01 added", "rail Rail02 grindable on flat ground"])
        assert.ok(
            out.parity.some((p) => p.startsWith(what)),
            what,
        );
    // Refused as the DLL refuses.
    for (const line of [
        "point ed_rail09 RailNode pos=1,2,3 grindflat=2",
        "facing 00000001",
        "point x LevelLight pos=1,2,3",
    ])
        assert.throws(() => readLayout(`thug2-decomp layout 2\nscene 00000001\n${line}\n`), LayoutError, line);
});
