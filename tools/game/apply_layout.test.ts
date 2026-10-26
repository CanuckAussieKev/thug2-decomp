// NOT ORIGINAL - tests of tools/pipeline/game/apply_layout.ts, layout_file.ts and tools/pipeline/level_port/qb_write.ts on a small level
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
    normalOffset,
    objectScaleValue,
    placeSector,
    quarterTurn,
    scaledNormal,
} from "./apply_layout.ts";
import { decodeValue, LayoutError, parseName, readLayout, readScaleToken } from "./layout_file.ts";

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
test("bake: (UX9) a raised ground piece and a pit - new sectors and collision, the pit's tiles left out", () => {
    const layout = readLayout(
        [
            "thug2-decomp layout 2",
            `scene ${hex(LEVEL)}`,
            `ground ed_ground01 raise rect=0,0,400,300 base=0 height=100 ramps=q,-,-,- length=0 material=0x${hex("SecA")} uv=128 color=FF606060 terrain=3`,
            `ground ed_ground02 lower rect=400,-100,700,200 base=0 height=80 ramps=-,-,-,- length=0 material=0x${hex("SecA")} uv=128 color=FF606060 terrain=3 hides=0x${hex("SecB")}`,
            "ground ed_ground03 raise rect=0,0,10,10 base=0 height=10 material=0x00000001",
        ].join("\n"),
    );
    assert.equal(layout.grounds.length, 3);
    const out = bake(layout, level(), null);
    const scene = readScene(writeScene(out.scene), "scn");
    const col = readCol(writeCol(out.col!), "col");
    const piece = scene.sectors.find((s) => s.checksum === checksum("ed_ground01"))!;
    const pieceCol = col.objects.find((o) => o.checksum === checksum("ed_ground01"))!;
    // (deck 2 + 3 walls x 2 + a quarter pipe: 16 curve + 16 cheek triangles)
    assert.equal(pieceCol.faces.length, 40);
    assert.equal(piece.meshes[0]!.vertexCount, 120);
    assert.ok(piece.meshes[0]!.strips[0]!.length > 0);
    assert.equal(pieceCol.faces.filter((f) => f[0] === 0x0009).length, 8, "the quarter pipe's vert faces");
    assert.ok(
        pieceCol.faces.every((f) => f[1] === 3),
        "the terrain",
    );
    assert.ok(scene.sectors.some((s) => s.checksum === checksum("ed_ground02")));
    assert.equal(
        scene.sectors.some((s) => s.checksum === checksum("SecB")),
        false,
        "the pit's tile left out",
    );
    assert.equal(
        col.objects.some((o) => o.checksum === checksum("SecB")),
        false,
    );
    assert.ok(
        out.warnings.some((w) => w.includes("ed_ground03")),
        "a missing material: warned, not baked",
    );
    assert.ok(
        out.report.some((r) => r.startsWith("ground ed_ground01: a platform 400 x 300, 100 high, ramps q,-,-,-")),
    );
});

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
        'trigger ed_gap01 gapstart pos=1,2,3 yaw=0 size=10,10,10 gap=7 replaces=0000ABCD text="A gap"',
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
    assert.deepEqual(l.sectors[0]!.placement.scale, { x: 2, y: 2, z: 2 });
    assert.deepEqual(l.clones[0]!.placement.scale, { x: 1, y: 1, z: 1 });
    assert.deepEqual(l.objects[0]!.scale, { x: 1, y: 1, z: 1 }, "older files: 1x");
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
    assert.equal(l.triggers[0]!.kind, "gapstart");
    assert.equal(l.triggers[0]!.fields.text, '"A gap"');
    assert.equal(l.triggers[0]!.spec.text, "A gap");
    assert.equal(l.triggers[0]!.spec.replaces, 0xabcd);
    assert.deepEqual(l.triggers[0]!.spec.size, { x: 10, y: 10, z: 10 });
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
    // (triggers: Triggers::ReadRecord's refusals - the kind's case, `other`, a repeated key, a bad value, no pos)
    bad("trigger ed_t01 Kill pos=0,0,0");
    bad("trigger ed_t01 other pos=0,0,0");
    bad("trigger ed_t01 kill pos=0,0,0 pos=1,1,1");
    bad("trigger ed_t01 kill pos=0,0,0 water=2");
    bad("trigger ed_t01 kill pos=0,0,0 score=-1");
    bad('trigger ed_t01 gapend pos=0,0,0 text="a" b');
    bad("trigger ed_t01 kill yaw=0");
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
    const two = { x: 2, y: 2, z: 2 };
    assert.deepEqual(applyPlacement({ quarters: 2, scale: two, offset: { x: 1, y: 0, z: 0 } }, { x: 1, y: 1, z: 1 }), {
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
            `trigger ed_kill01 kill pos=0,0,0 yaw=0 size=50,50,50 target=TRG_Restart01 replaces=${hex("Wall1")}`,
            `wall ${hex("Wall1")} 0 0 0 0 1`,
            "mystery 1",
        ].join("\n"),
    );
    const out = bake(layout, level(), { level: LEVEL, why: "a test" });
    const nodes = nodesOf(out.qb);
    assert.equal(nodes.length, 10, "6 of the level's + 2 nodes + 1 point + 1 trigger appended");
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
    assert.ok(!out.unsupported.some((u) => u.includes('"trigger"')), "triggers are baked now");
    assert.ok(out.unsupported.some((u) => u.includes('"mystery"')));
    assert.ok(!out.col!.objects.some((o) => o.checksum === checksum("Wall1")), "the replaced level trigger: off");
    assert.ok(
        out.col!.objects.some((o) => o.checksum === checksum("ed_kill01")),
        "the trigger's box",
    );
    assert.equal((named(nodes, "ed_kill01").get("TriggerScript") as Name).value, checksum("ed_kill01Script"));
    assert.ok(
        new QbFile(out.qb, "tst.qb", true).scripts.has(checksum("ed_kill01Script")),
        "no scripts .qb: in the node .qb",
    );
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

// A scripts .qb (one script) and a Levels.qb (the level's structure between two others).
function scriptsQb(): Buffer {
    return Buffer.concat([
        Buffer.from([0x01, 0x23]),
        encodeValue(NAME("tst_Setup")),
        Buffer.from([0x01]),
        encodeValue(NAME("DoThings")),
        Buffer.from([0x01, 0x24, 0x01, 0x2b]),
        pack("<I", checksum("tst_Setup")),
        Buffer.from("tst_Setup\0", "latin1"),
        Buffer.from([0x00]),
    ]);
}
function levelsQb(): Buffer {
    const global = (name: string, value: QbValue): Buffer =>
        Buffer.concat([encodeValue(NAME(name)), Buffer.from([0x07]), encodeValue(value), Buffer.from([0x01])]);
    return Buffer.concat([
        global("Level_aa", struct([["level", "aa"]])),
        global(
            `Level_${LEVEL}`,
            struct([
                ["name", "Test"],
                ["level", LEVEL],
                ["tod_manager_active", 0],
                ["ambient_red", 10],
                ["ambient_green", 11],
                ["ambient_blue", 12],
            ]),
        ),
        global("Level_zz", struct([["level", "zz"]])),
        Buffer.from([0x00]),
    ]);
}

test("bake: triggers - node, trigger box, script in the scripts .qb; level properties in Levels.qb", () => {
    const layout = readLayout(
        [
            "thug2-decomp layout 2",
            `scene ${hex(LEVEL)}`,
            `level name=My%20Test start=${hex("TRG_Restart01")} tod=night time=1 ambient=300,2,3 fog=4,5,6,7,200000`,
            `trigger ed_trigger01 gapstart pos=0,100,0 yaw=0 size=400,200,400 gap=ed_gap01 flags=CANCEL_GROUND score=100 text="Editor Gap 1"`,
            `trigger ed_trigger02 gapend pos=600,100,0 yaw=0 size=400,200,400 gap=ed_gap01 flags=CANCEL_GROUND score=100 text="Editor Gap 1"`,
            "trigger ed_trigger03 teleport pos=0,0,900 yaw=1.5 size=100,100,100 target=TRG_Restart01",
            `trigger ed_trigger04 script pos=0,0,0 yaw=0 size=10,10,10 script=tst_Setup replaces=${hex("Wall1")}`,
            "trigger ed_trigger05 kill pos=0,0,0 yaw=0 size=10,10,10",
            "trigger ed_trigger06 gapstart pos=0,0,0 yaw=0 size=10,10,10 gap=ed_lonely score=100",
            "trigger Rail01 kill pos=0,0,0 target=TRG_Restart01",
        ].join("\n"),
    );
    const files = { ...level(), scripts: scriptsQb(), levels: levelsQb() };
    const out = bake(layout, files, null);
    const nodes = nodesOf(out.qb);
    assert.equal(nodes.length, 6 + 6, "six triggers appended (not the one named like a level node)");
    const t1 = named(nodes, "ed_trigger01");
    assert.deepEqual(
        t1.entries.map(([k]) => k!.value),
        ["Pos", "Angles", "Name", "Class", "Type", "TriggerScript"].map(checksum),
    );
    assert.equal((t1.get("TriggerScript") as Name).value, checksum("ed_trigger01Script"));
    assert.deepEqual((named(nodes, "ed_trigger03").get("Angles") as Vector).values, [0, Math.fround(1.5), 0]);
    assert.ok(out.warnings.some((w) => w.includes("trigger Rail01") && w.includes("already")));
    // The boxes: one collision object a trigger, named after its node; the replaced level trigger left out.
    for (let i = 1; i <= 6; i++) {
        const o = out.col!.objects.find((x) => x.checksum === checksum(`ed_trigger0${i}`));
        assert.ok(o && o.faces.length === 12 && o.faces.every((f) => (f[0] & 0x50) === 0x50), `box ${i}`);
    }
    assert.ok(!out.col!.objects.some((o) => o.checksum === checksum("Wall1")), "replaced: off");
    assert.ok(out.col!.objects.some((o) => o.checksum === checksum("SecA")));
    // The scripts: in the scripts .qb (its own script's bytes first, as they were), none in the node .qb.
    assert.ok(out.scripts);
    const original = scriptsQb();
    const ownEnd = original.indexOf(0x2b);
    assert.deepEqual(out.scripts.subarray(0, ownEnd), original.subarray(0, ownEnd));
    const sq = new QbFile(out.scripts, "tst_scripts.qb", true);
    assert.equal(sq.scripts.size, 1 + 6);
    assert.deepEqual(sq.calls(checksum("ed_trigger01Script"))[0]![0], checksum("StartGap"));
    assert.deepEqual(sq.calls(checksum("ed_trigger02Script"))[0]![0], checksum("EndGap"));
    assert.deepEqual(sq.calls(checksum("ed_trigger04Script"))[0]![0], checksum("tst_Setup"));
    assert.equal(sq.scripts.get(checksum("ed_trigger05Script"))!.length, 0, "no restart: an empty script, as the DLL");
    assert.equal(sq.names.get(checksum("ed_gap01")), "ed_gap01");
    assert.equal(new QbFile(out.qb, "tst.qb", true).scripts.size, 0);
    assert.ok(out.warnings.some((w) => w.includes("ed_trigger05") && w.includes("does nothing")));
    assert.ok(out.warnings.some((w) => w.includes("ed_lonely has no end")));
    assert.ok(!out.warnings.some((w) => w.includes("ed_gap01 has no")));
    // Level properties: set in Level_tst (clamped as the DLL), the other structures' bytes kept; start reported.
    assert.ok(out.levels);
    const lv = new QbFile(out.levels, "Levels.qb", true);
    const s = lv.globals.find(([k]) => k.value === checksum(`Level_${LEVEL}`))![1] as Struct;
    assert.equal(s.get("name"), "My Test");
    assert.equal((s.get("tod_state") as Name).value, checksum("night"));
    assert.equal(s.get("tod_manager_active"), 1);
    // (map4: our own ed_ambient / ed_fog keys - the structure's own ambient_* left as they were)
    assert.deepEqual(s.get("ed_ambient"), [255, 2, 3]);
    assert.deepEqual(s.get("ed_fog"), [4, 5, 6, 7, 100000]);
    assert.equal(s.get("ambient_red"), 10);
    assert.equal(s.get("fog_red"), undefined);
    const before = levelsQb();
    const first = before.indexOf(Buffer.concat([encodeValue(NAME(`Level_${LEVEL}`))]));
    assert.deepEqual(out.levels.subarray(0, first), before.subarray(0, first), "Level_aa as it was");
    assert.equal((lv.globals.find(([k]) => k.value === checksum("Level_zz"))![1] as Struct).get("level"), "zz");
    assert.ok(out.warnings.some((w) => w.startsWith("level start") && w.includes("not baked")));
    assert.ok(out.report.some((r) => r.startsWith(`Levels.qb: Level_${LEVEL}`)));
    // Baked again over its own output: the triggers are there already - nothing doubled.
    const twice = bake(layout, { ...files, qb: out.qb, col: out.col, scripts: out.scripts, levels: out.levels }, null);
    assert.equal(nodesOf(twice.qb).length, 12);
    assert.equal(twice.scripts, null, "no new scripts");
    assert.deepEqual(twice.levels, out.levels, "the same values: the same bytes");
});

// --- (UX5) note pins: read, never baked ---------------------------------------------------------------------------------

test("layout_file: (UX5) note records read as the DLL writes them; malformed refused; the bake says nothing of them", () => {
    const l = readLayout(
        [
            "thug2-decomp layout 2",
            `scene ${hex(LEVEL)}`,
            'note ed_note01 pos=1.5,-2,300.25 color=5AC8FA text="raise this ledge%0A100%25 %22now%22 %231 é"',
            "note ed_note02 pos=4,5,6 later=7",
            "# not found in the level when loaded - kept until the Problems window drops them",
        ].join("\n"),
    );
    assert.deepEqual(l.notes, [
        {
            name: "ed_note01",
            pos: { x: 1.5, y: -2, z: 300.25 },
            colour: 0x5ac8fa,
            text: 'raise this ledge\n100% "now" #1 é',
        },
        { name: "ed_note02", pos: { x: 4, y: 5, z: 6 }, colour: 0xffd84a, text: "" },
    ]);
    assert.deepEqual(l.unknown, []);
    const refused = [
        "note ed_note01",
        "note ed_note01 pos=1,2",
        "note ed_note01 pos=1,2,3 pos=1,2,3",
        "note ed_note01 pos=1,2,3 color=FFF",
        "note bad-name pos=1,2,3",
        'note ed_note01 pos=1,2,3 text="x" color=FFFFFF',
        'note ed_note01 pos=1,2,3 text="open',
        "note ed_note01 pos=1,2,3 text=bare",
        'note ed_note01 pos=1,2,3 text="bad%zz"',
    ];
    for (const line of refused) {
        assert.throws(
            () => readLayout(`thug2-decomp layout 2\nscene ${hex(LEVEL)}\n${line}\n`),
            (e: unknown) => e instanceof LayoutError && e.line === 3,
            line,
        );
    }
    const out = bake(l, level(), null);
    assert.deepEqual(
        [out.warnings, out.unsupported, out.parity],
        [[], [], []],
        "notes: no warning, nothing unsupported",
    );
    assert.ok(!out.report.some((r) => /note/i.test(r)), "nothing reported of the notes");
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
    assert.match(installRefusal("C:\\Games\\THUG2-BACKUP", true) ?? "", /never/);
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

// --- the editor's later records: per-axis scale, object scale, exit triggers (editor-scale2 / editor-polish) --------

// Every record kind and key the DLL's writers produce (level_editor_layout.cpp Write, LevelProps::WriteRecord,
// Triggers::Record, Fields::Token, Chains::WriteRecords), in their own forms - comments too.
test("layout_file reads every record and key the DLL writes", () => {
    const h = hex;
    const text = [
        "thug2-decomp layout 2",
        `scene ${h(LEVEL)}  # tst`,
        "level name=My%20Level start=00000001 tod=evening time=1 ambient=1,2,3 fog=4,5,6,7,800",
        `sector ${h("SecA")} 1 10 0 0 0 scale=2,1,0.5  # SecA`,
        `sector ${h("SecB")} 0 0 0 0 1 scale=1.5`,
        `rail ${h("Rail01")} 2 0 5 0 0 scale=1,2,3  # Rail01`,
        `clone ${h("SecB")} 3 0 0 100 scale=0.25,4,1  # SecB`,
        `object ${h("Obj01")} 1 2 3 1 0 0 0 1 0 0 0 1 scale=2  # Obj01`,
        `copy 0000ABCD ${h("Obj01")} 1 2 3 1 0 0 0 1 0 0 0 1 scale=1,2,3  # Obj01`,
        `copy 0000ABCE ${h("Obj01")} 4 5 6 1 0 0 0 1 0 0 0 1`,
        "node ed_light01 GenericNode type=Light pos=1,2,3 angles=0,0.5,0 color=255,128,0 brightness=1.5 inner=100 outer=300 radius=50",
        "node ed_restart01 Restart type=MultiPlayer pos=4,5,6 angles=0,1,0 types=MultiPlayer,Horse",
        "order crown ed_crown01 ed_crown02",
        `hide ${h("Crown01")}  # Crown01`,
        `lock object ${h("Obj01")}  # Obj01`,
        `lock sector ${h("SecA")}`,
        'trigger ed_t01 gapstart pos=1,2,3 yaw=0.5 size=10,20,30 gap=G1 flags=CANCEL_GROUND,CANCEL_WALL score=100 replaces=0000ABCD exit=1 text="A gap"',
        "trigger ed_t02 kill pos=1,2,3 yaw=0 size=400,200,400 target=TRG_Restart01 water=1",
        "trigger ed_t03 script pos=1,2,3 yaw=0 size=400,200,400 script=tst_Setup exit=0",
        `set ${h("TRG_Restart01")} pos=1,2,3 angles=0,1,0 f.a=i:1 f.b=f:1.5 f.c=p:1,2 f.d=v:1,2,3 f.e=n:Name f.f=s:x%20y f.g=l:y f.h=flag:1 f.i=an:A,0x0BADF00D f.j=ai:1,2 f.k=af:1.5,2 f.0x0000AAAA=del  # TRG_Restart01`,
        `point ed_rail01 RailNode pos=1,2,3 like=${h("Rail01")} terrain=TERRAIN_GRINDWOOD links=${h("Rail01")},${h("Rail02")} facing=1.5 cluster=${h("Rail01")} grindflat=1`,
        "point ed_ladder01 ClimbingNode pos=1,2,3 facing=0",
        `links ${h("Rail01")} -`,
        `links ${h("Rail02")} ${h("Rail01")},${h("Rail03")}`,
        `drop ${h("Rail03")}`,
        `terrain ${h("Rail02")} TERRAIN_GRINDWOOD`,
        `facing ${h("Ladder01")} 1.5`,
        `grindflat ${h("Rail02")}`,
        `wall ${h("Wall1")} 0 0 16 0 1  # Wall1`,
    ].join("\n");
    const l = readLayout(text);
    assert.deepEqual(l.unknown, [], "every record known");
    assert.deepEqual(l.sectors[0]!.placement.scale, { x: 2, y: 1, z: 0.5 });
    assert.deepEqual(l.sectors[1]!.placement.scale, { x: 1.5, y: 1.5, z: 1.5 });
    assert.equal(l.sectors[1]!.hidden, true);
    assert.deepEqual(l.rails[0]!.placement.scale, { x: 1, y: 2, z: 3 });
    assert.deepEqual(l.clones[0]!.placement, {
        quarters: 3,
        offset: { x: 0, y: 0, z: 100 },
        scale: { x: 0.25, y: 4, z: 1 },
    });
    assert.deepEqual(l.objects[0]!.scale, { x: 2, y: 2, z: 2 });
    assert.deepEqual(l.objects[0]!.rows, [1, 0, 0, 0, 1, 0, 0, 0, 1]);
    assert.deepEqual(l.copies[0]!.scale, { x: 1, y: 2, z: 3 });
    assert.deepEqual(l.copies[1]!.scale, { x: 1, y: 1, z: 1 });
    assert.deepEqual(l.nodes[0]!.colour, [255, 128, 0]);
    assert.equal(l.nodes[0]!.outer, 300);
    assert.equal(l.nodes[0]!.radius, 50);
    assert.deepEqual(l.nodes[1]!.types, ["MultiPlayer", "Horse"]);
    assert.deepEqual(l.crownOrder, ["ed_crown01", "ed_crown02"]);
    assert.deepEqual(
        l.locks.map((k) => k.kind),
        ["object", "sector"],
    );
    assert.equal(l.triggers.length, 3);
    assert.equal(l.triggers[0]!.spec.exit, true);
    assert.deepEqual(l.triggers[0]!.spec.flags, ["CANCEL_GROUND", "CANCEL_WALL"]);
    assert.equal(l.triggers[1]!.spec.water, true);
    assert.equal(l.triggers[2]!.spec.exit, false);
    assert.equal(l.sets[0]!.fields.length, 12);
    assert.equal(l.sets[0]!.fields[11]!.removed, true);
    assert.equal(l.points[0]!.cluster, checksum("Rail01"));
    assert.equal(l.points[0]!.grindFlat, true);
    assert.equal(l.points[1]!.cls, "ClimbingNode");
    assert.equal(l.links[1]!.links.length, 2);
    assert.deepEqual(l.facings, [{ name: checksum("Ladder01"), facing: 1.5 }]);
    assert.deepEqual(l.grindFlat, [checksum("Rail02")]);
    assert.equal(l.walls[0]!.placement.offset.y, 16);
    assert.equal(l.level.tod, "evening");
});

test("layout_file: scale= as Scale::ReadToken - one number or three, each above 0", () => {
    assert.deepEqual(readScaleToken("scale=2"), { x: 2, y: 2, z: 2 });
    assert.deepEqual(readScaleToken("scale=1.5,1,0.25"), { x: 1.5, y: 1, z: 0.25 });
    const f = Math.fround(1.1);
    assert.deepEqual(readScaleToken("scale=1.1"), { x: f, y: f, z: f });
    for (const bad of [
        "scale=",
        "scale=0",
        "scale=-1",
        "scale=1,2",
        "scale=1,2,3,4",
        "scale=a",
        "scale=1,,2",
        "scale=1,0,1",
        "scale=nan",
        "scale=inf",
    ])
        assert.equal(readScaleToken(bad), null, bad);
    const refused = (line: string): void => {
        assert.throws(
            () => readLayout(`thug2-decomp layout 2\nscene 00000001\n${line}\n`),
            (e: unknown) => e instanceof LayoutError && e.line === 3 && /scale=/.test(e.message),
            line,
        );
    };
    refused("sector 00000001 0 0 0 0 0 scale=1,2");
    refused("rail 00000001 0 0 0 0 0 scale=0");
    refused("clone 00000001 0 0 0 0 scale=1,2,3,4");
    refused("object 00000001 1 2 3 1 0 0 0 1 0 0 0 1 scale=-2");
    refused("copy 00000002 00000001 1 2 3 1 0 0 0 1 0 0 0 1 scale=x");
    // (other key=value fields after the numbers: a later version's - skipped)
    const l = readLayout("thug2-decomp layout 2\nscene 00000001\nsector 00000001 0 0 0 0 0 later=1 scale=3\n");
    assert.deepEqual(l.sectors[0]!.placement.scale, { x: 3, y: 3, z: 3 });
    // exit= as Triggers::ReadRecord: 0 / 1, once
    assert.throws(() => readLayout("thug2-decomp layout 2\ntrigger ed_t kill pos=0,0,0 exit=2\n"), LayoutError);
    assert.throws(() => readLayout("thug2-decomp layout 2\ntrigger ed_t kill pos=0,0,0 exit=1 exit=1\n"), LayoutError);
});

test("normals as the editor's Rebuild: n / scale made unit length, then turned", () => {
    assert.deepEqual(scaledNormal({ x: 1, y: 0, z: 0 }, { x: 1, y: 1, z: 1 }, 1), { x: 0, y: 0, z: -1 });
    // unscaled: turned only, not normalised again
    assert.deepEqual(scaledNormal({ x: 0.5, y: 0, z: 0 }, { x: 1, y: 1, z: 1 }, 0), { x: 0.5, y: 0, z: 0 });
    const n = scaledNormal({ x: 0.6, y: 0.8, z: 0 }, { x: 2, y: 1, z: 1 }, 0);
    assert.ok(Math.abs(n.x - 0.3 / Math.sqrt(0.73)) < 1e-6 && Math.abs(n.y - 0.8 / Math.sqrt(0.73)) < 1e-6);
    const zero = scaledNormal({ x: 0, y: 0, z: 0 }, { x: 2, y: 1, z: 1 }, 0);
    assert.ok(zero.x === 0 && zero.y === 0 && zero.z === 0, "a zero normal stays zero");
    // NormalOffset: an FVF with XYZ and NORMAL and a stride of 24 or more; a shader handle, XYZRHW, XYZB1: none
    assert.equal(normalOffset(0x152, 36), 12);
    assert.equal(normalOffset(0x142, 36), -1);
    assert.equal(normalOffset(0x152, 20), -1);
    assert.equal(normalOffset(0x10152, 36), -1);
    assert.equal(normalOffset(0x014, 36), -1);
    assert.equal(normalOffset(0x016, 36), -1);
    assert.equal(normalOffset(0x4012, 36), -1);
});

test("bake: a stretched sector - vertices per axis then turned, normals still the faces', collision, rails, copies", () => {
    // The triangle's own normal in its vertices: stretched and turned, it must be the placed triangle's normal.
    const s = sector("SecA", [0, 0, 0]);
    const data = s.meshes[0]!.buffers[0]![1];
    const face = { x: 0, y: -50 / Math.hypot(50, 100), z: 100 / Math.hypot(50, 100) };
    for (let v = 0; v < 3; v++) {
        data.writeFloatLE(face.x, v * 36 + 12);
        data.writeFloatLE(face.y, v * 36 + 16);
        data.writeFloatLE(face.z, v * 36 + 20);
    }
    placeSector(s, { quarters: 1, offset: { x: 100, y: 0, z: 0 }, scale: { x: 2, y: 3, z: 4 } });
    const placed = s.meshes[0]!.buffers[0]![1];
    const at = (v: number): number[] => [0, 4, 8, 12, 16, 20].map((o) => placed.readFloatLE(v * 36 + o));
    const [a, b, c] = [at(0), at(1), at(2)];
    // (10, 0, 0) -> (20, 0, 0) -> turned (0, 0, -20) -> (100, 0, -20); (0, 10, 5) -> (0, 30, 20) -> (120, 30, 0)
    assert.deepEqual(b.slice(0, 3), [100, 0, -20]);
    assert.deepEqual(c.slice(0, 3), [120, 30, 0]);
    const e1 = [b[0]! - a[0]!, b[1]! - a[1]!, b[2]! - a[2]!];
    const e2 = [c[0]! - a[0]!, c[1]! - a[1]!, c[2]! - a[2]!];
    const cross = [
        e1[1]! * e2[2]! - e1[2]! * e2[1]!,
        e1[2]! * e2[0]! - e1[0]! * e2[2]!,
        e1[0]! * e2[1]! - e1[1]! * e2[0]!,
    ];
    const length = Math.hypot(...cross);
    for (const v of [a, b, c]) {
        for (let k = 0; k < 3; k++) assert.ok(Math.abs(v[3 + k]! - cross[k]! / length) < 1e-6, `normal ${k}`);
    }
    assert.deepEqual(s.bbox, [100, 0, -20, 120, 30, 0]);

    // Through the bake: the sector, its collision, the shared collision's companion, a rail point, a stretched copy.
    const layout = readLayout(
        [
            "thug2-decomp layout 2",
            `scene ${hex(LEVEL)}`,
            `sector ${hex("SecA")} 1 100 0 0 0 scale=2,3,4`,
            `clone ${hex("SecB")} 0 0 0 1000 scale=1,1,2`,
            `rail ${hex("Rail01")} 1 0 10 0 0 scale=2,1,1`,
        ].join("\n"),
    );
    const out = bake(layout, level(), null);
    const sa = out.scene.sectors.find((x) => x.checksum === checksum("SecA"))!;
    assert.deepEqual(vertex(sa, 2).slice(0, 3), [120, 30, 0]);
    const colA = out.col!.objects.find((o) => o.checksum === checksum("SecA"))!;
    assert.deepEqual(colA.verts[1], [100, 0, -20]);
    assert.deepEqual(colA.verts[2], [120, 30, 0]);
    assert.deepEqual(colA.bbox, [100, 0, -20, 1, 120, 30, 0, 1]);
    const companion = out.col!.objects.find((o) => o.checksum === checksum(`ed_coll_${hex("SecA")}_1`))!;
    assert.deepEqual(companion.verts[1], [100, 0, -20], "the shared collision stretched with the piece");
    const copy = out.scene.sectors.find((x) => x.checksum === checksum(`ed_clone_${hex("SecB")}_1`))!;
    // SecB's (500, 10, 5) -> (500, 10, 10) + (0, 0, 1000)
    assert.deepEqual(vertex(copy, 2).slice(0, 3), [500, 10, 1010]);
    // Rail01 (100, 0, 0) -> (200, 0, 0) -> turned (0, 0, -200) -> (0, 10, -200)
    assert.deepEqual((named(nodesOf(out.qb), "Rail01").get("Pos") as Vector).values, [0, 10, -200]);
});

test("bake: an object's / copy's scale= becomes its node's scale (the model's); its collision warned", () => {
    assert.equal(objectScaleValue({ x: 1, y: 1, z: 1 }), null, "1x: nothing written");
    assert.equal((objectScaleValue({ x: 2, y: 2, z: 2 }) as Float).value, 2);
    assert.deepEqual((objectScaleValue({ x: 1, y: 2, z: 9 }) as Vector).values, [1, 2, 4], "clamped to 0.25 .. 4");
    assert.equal((objectScaleValue({ x: 0.1, y: 0.1, z: 0.1 }) as Float).value, 0.25);
    const layout = readLayout(
        [
            "thug2-decomp layout 2",
            `scene ${hex(LEVEL)}`,
            `object ${hex("Obj01")} 7 8 9 1 0 0 0 1 0 0 0 1 scale=1.5`,
            `copy ${hex("ed_copy01")} ${hex("Obj01")} 1 2 3 1 0 0 0 1 0 0 0 1 scale=1,2,0.5`,
            `copy ${hex("ed_copy02")} ${hex("Obj01")} 1 2 3 1 0 0 0 1 0 0 0 1`,
        ].join("\n"),
    );
    const out = bake(layout, level(), null);
    const nodes = nodesOf(out.qb);
    assert.equal((named(nodes, "Obj01").get("scale") as Float).value, 1.5);
    assert.deepEqual((named(nodes, "ed_copy01").get("scale") as Vector).values, [1, 2, 0.5]);
    // a copy without scale=: its source's fields as loaded - no scale
    assert.equal(named(nodes, "ed_copy02").get("scale"), undefined);
    assert.equal(new QbFile(out.qb, "tst.qb", true).names.get(checksum("scale")), "scale");
    assert.equal(out.warnings.filter((w) => w.includes("its collision isn't")).length, 2);
    assert.ok(out.report.some((r) => r.startsWith("object Obj01: scale=")));
    // Baked twice over its own output: the scale replaced, not doubled.
    const again = bake(layout, { ...level(), qb: out.qb }, null);
    const obj = named(nodesOf(again.qb), "Obj01");
    assert.equal(obj.entries.filter(([k]) => k?.value === checksum("scale")).length, 1);
});

test("bake: an exit trigger's box gets its faces again facing in; a plain one doesn't", () => {
    const layout = readLayout(
        [
            "thug2-decomp layout 2",
            `scene ${hex(LEVEL)}`,
            "trigger ed_in01 script pos=0,0,0 yaw=0 size=100,100,100 script=tst_Setup",
            "trigger ed_out01 script pos=0,0,500 yaw=0 size=100,100,100 script=tst_Setup exit=1",
        ].join("\n"),
    );
    const out = bake(layout, level(), null);
    const box = (name: string): ColObject => out.col!.objects.find((o) => o.checksum === checksum(name))!;
    assert.equal(box("ed_in01").faces.length, 12);
    assert.equal(box("ed_out01").faces.length, 24);
    assert.ok(out.report.some((r) => r.startsWith("trigger ed_out01") && r.includes("also on leaving")));
    readCol(writeCol(out.col!), "col");
});

// --- (map3) faces, clusters, cameras, the sky --------------------------------------------------------------------------

test("layout_file: (map3) faces, cluster and camera records, the level's sky - read as the DLL, malformed refused", () => {
    const l = readLayout(
        [
            "thug2-decomp layout 2",
            `scene ${hex(LEVEL)}`,
            "level tod=night sky=BO_Sky",
            `faces ${hex("SecA")} 9:1,0:0091,65535:FFFF`,
            `cluster ${hex("Obj01")} ${hex("MyCluster")}  # Obj01`,
            `cluster ${hex("SecA")} -`,
            "camera ed_camera01 pos=1,2,3 angles=-0.25,1.5,0 dist=10 later=7",
            "camera ed_camera02 pos=4,5,6",
        ].join("\n"),
    );
    assert.equal(l.level.sky, "BO_Sky");
    assert.deepEqual(l.faces, [
        {
            checksum: checksum("SecA"),
            faces: [
                [0, 0x91],
                [9, 1],
                [65535, 0xffff],
            ],
        },
    ]);
    assert.deepEqual(l.clusters, [
        { sector: checksum("Obj01"), trick: true, cluster: checksum("MyCluster") },
        { sector: checksum("SecA"), trick: false, cluster: 0 },
    ]);
    assert.deepEqual(l.cameras, [
        { name: "ed_camera01", pos: { x: 1, y: 2, z: 3 }, angles: { x: -0.25, y: 1.5, z: 0 }, distance: 50 },
        { name: "ed_camera02", pos: { x: 4, y: 5, z: 6 }, angles: { x: 0, y: 0, z: 0 }, distance: 1000 },
    ]);
    assert.deepEqual(l.unknown, []);
    const refused = [
        "level sky=TR",
        "level sky=TR%20Sky",
        `faces ${hex("SecA")} 1:0011,1:0012`,
        `faces ${hex("SecA")} 1:00110`,
        `faces ${hex("SecA")}`,
        `faces ${hex("SecA")} 65536:1`,
        `faces ${hex("SecA")} 1:1 junk`,
        `cluster ${hex("SecA")}`,
        `cluster ${hex("SecA")} 00000000`,
        `cluster 00000000 ${hex("SecA")}`,
        "camera ed_camera01",
        "camera ed_camera01 pos=1,2,3 pos=1,2,3",
        "camera bad-name pos=1,2,3",
        "camera ed_camera01 pos=1,2,3 dist=x",
        "camera ed_camera01 pos=1,2,3 nokey",
    ];
    for (const line of refused) {
        assert.throws(
            () => readLayout(`thug2-decomp layout 2\nscene ${hex(LEVEL)}\n${line}\n`),
            (e: unknown) => e instanceof LayoutError && e.line === 3,
            line,
        );
    }
});

test("bake: (map3) face flags - the collision object's, a taken face's in its companion (the original keeps 0x10)", () => {
    const layout = readLayout(
        [
            "thug2-decomp layout 2",
            `scene ${hex(LEVEL)}`,
            `sector ${hex("SecA")} 1 100 0 0 0`,
            `faces ${hex("SecB")} 0:0004`,
            `faces ${hex("Loose")} 0:0041`,
            `faces ${hex("Far")} 0:2081,3:0001`,
            `faces ${hex("Nothing")} 0:0001`,
        ].join("\n"),
    );
    const out = bake(layout, level(), null);
    const object = (name: string): ColObject => out.col!.objects.find((o) => o.checksum === checksum(name))!;
    assert.equal(object("SecB").faces[0]![0], 0x0004, "a plain face's flags");
    assert.equal(object("Far").faces[0]![0], 0x2081);
    // (Loose's face went with the moved SecA: the companion has the flags, the original keeps 0x10)
    assert.equal(object("Loose").faces[0]![0], 0x0051);
    assert.equal(object(`ed_coll_${hex("SecA")}_1`).faces[0]![0], 0x0041);
    assert.ok(out.warnings.some((w) => w.startsWith(`face 3 of ${hex("Far")}`) && w.includes("has 1 faces")));
    assert.ok(out.warnings.some((w) => w.includes(`faces of ${hex("Nothing")}`) && w.includes("no collision object")));
    assert.ok(out.report.some((r) => r.startsWith("collision faces: 3 faces")));
    readCol(writeCol(out.col!), "col");
    // A copy's companion keeps the flags it was made with (the DLL's copies don't take face edits).
    const copied = bake(
        readLayout(
            [
                "thug2-decomp layout 2",
                `scene ${hex(LEVEL)}`,
                `sector ${hex("SecA")} 1 100 0 0 0`,
                `clone ${hex("SecA")} 0 0 0 1000`,
                `faces ${hex("Loose")} 0:0041`,
            ].join("\n"),
        ),
        level(),
        null,
    );
    const flags = (name: string): number =>
        copied.col!.objects.find((o) => o.checksum === checksum(name))!.faces[0]![0];
    assert.equal(flags(`ed_coll_${hex("SecA")}_1`), 0x0041);
    assert.equal(flags(`ed_coll_${hex("SecA")}_2`), 0x0001, "the copy's companion as loaded");
});

// A node array with a TrickObject of its own and one from a shared structure.
function trickQb(): Buffer {
    const ncomp = struct([
        [null, NAME("TrickObject")],
        ["Cluster", NAME("SharedCluster")],
    ]);
    const nodes = [
        struct([
            ["Pos", vec(0, 0, 0)],
            ["Name", NAME("Own01")],
            ["Class", NAME("LevelGeometry")],
            [null, NAME("TrickObject")],
            ["Cluster", NAME("OwnCluster")],
        ]),
        struct([
            ["Pos", vec(0, 0, 0)],
            ["Name", NAME("Shared01")],
            ["Class", NAME("LevelGeometry")],
            [null, NAME("ncomp_LevelGeometry_1")],
        ]),
        struct([
            ["Pos", vec(0, 0, 0)],
            ["Name", NAME("Plain01")],
            ["Class", NAME("LevelGeometry")],
        ]),
    ];
    const global = (name: string, value: QbValue): Buffer =>
        Buffer.concat([encodeValue(NAME(name)), Buffer.from([0x07]), encodeValue(value), Buffer.from([0x01])]);
    return Buffer.concat([
        global("ncomp_LevelGeometry_1", ncomp),
        global(`${LEVEL}_NodeArray`, nodes),
        ...[
            "TrickObject",
            "Cluster",
            "Own01",
            "Shared01",
            "Plain01",
            "ncomp_LevelGeometry_1",
            `${LEVEL}_NodeArray`,
        ].map((n) => Buffer.concat([Buffer.from([0x2b]), pack("<I", checksum(n)), Buffer.from(`${n}\0`, "latin1")])),
        Buffer.from([0x00]),
    ]);
}

test("bake: (map3) scenery clusters - TrickObject + Cluster= on the node, taken out, a shared one warned", () => {
    const layout = readLayout(
        [
            "thug2-decomp layout 2",
            `scene ${hex(LEVEL)}`,
            `cluster ${hex("Plain01")} ${hex("NewCluster")}`,
            `cluster ${hex("Own01")} -`,
            `cluster ${hex("Shared01")} -`,
            `cluster ${hex("Gone01")} -`,
        ].join("\n"),
    );
    const out = bake(layout, { ...level(), qb: trickQb() }, null);
    const nodes = nodesOf(out.qb);
    const flag = (s: Struct): boolean =>
        s.entries.some(([k, v]) => k === null && v instanceof Name && v.value === checksum("TrickObject"));
    const plain = named(nodes, "Plain01");
    assert.ok(flag(plain));
    assert.equal((plain.get("Cluster") as Name).value, checksum("NewCluster"));
    const own = named(nodes, "Own01");
    assert.ok(!flag(own) && own.get("Cluster") === undefined, "its own TrickObject and Cluster= taken out");
    assert.ok(out.warnings.some((w) => w.startsWith("scenery Shared01") && w.includes("shared structure")));
    assert.ok(out.warnings.some((w) => w.startsWith("cluster: no node")));
});

test("bake: (map3) camera nodes appended (virtual_cam offsets); the level's sky in Levels.qb", () => {
    const layout = readLayout(
        [
            "thug2-decomp layout 2",
            `scene ${hex(LEVEL)}`,
            "level sky=BO_Sky",
            "camera ed_camera01 pos=100,50,-20 angles=-0.25,0.5,0 dist=800",
            "camera Rail01 pos=0,0,0",
        ].join("\n"),
    );
    const out = bake(layout, { ...level(), levels: levelsQb() }, null);
    const nodes = nodesOf(out.qb);
    const cam = named(nodes, "ed_camera01");
    assert.deepEqual(
        cam.entries.map(([k]) => k!.value),
        ["Pos", "Angles", "Name", "Class", "Type", "targetOffset", "positionOffset"].map(checksum),
    );
    assert.equal((cam.get("Type") as Name).value, checksum("ed_camera"));
    const target = (cam.get("targetOffset") as Vector).values;
    const offset = (cam.get("positionOffset") as Vector).values;
    // (the game's at row for (-0.25, 0.5, 0): (sin 0.5 cos 0.25, sin 0.25, cos 0.5 cos 0.25), 800 ahead)
    const f = [Math.sin(0.5) * Math.cos(0.25), Math.sin(0.25), Math.cos(0.5) * Math.cos(0.25)];
    [100, 50, -20].forEach((p, i) => {
        assert.ok(Math.abs(target[i]! - (p + f[i]! * 800)) < 1e-3, `target ${i}`);
        assert.ok(Math.abs(offset[i]! + f[i]! * 800) < 1e-3, `offset ${i}`);
    });
    assert.ok(out.warnings.some((w) => w.startsWith("camera Rail01") && w.includes("already")));
    const lv = new QbFile(out.levels!, "Levels.qb", true);
    const s = lv.globals.find(([k]) => k.value === checksum(`Level_${LEVEL}`))![1] as Struct;
    assert.equal(s.get("sky"), "BO_Sky");
});

// --- level_editor_entities.h: speeds, emitters' sounds, particle systems moved / copied, team lobby flags -----------

function entityLevel(): LevelFiles {
    const emitterShared = struct([
        ["Class", NAME("EmitterObject")],
        ["Type", NAME("BoundingBox")],
        [null, NAME("CreatedAtStart")],
    ]);
    const nodes = [
        struct([
            ["Pos", vec(100, 0, 0)],
            ["Name", NAME("Smoke01")],
            ["Class", NAME("ParticleObject")],
            ["MidPosition", vec(110, 20, 0)],
            ["EndPosition", vec(120, 40, 0)],
            ["BoxDimsStart", vec(30, 5, 10)],
        ]),
        struct([
            ["Pos", vec(0, 0, 0)],
            ["Name", NAME("TRG_SFX_SOB_Fire01")],
            [null, NAME("ncomp_EmitterObject_1")],
        ]),
        struct([
            ["Pos", vec(5, 0, 5)],
            ["Name", NAME("TRG_Car01")],
            ["Class", NAME("Vehicle")],
        ]),
    ];
    const names = [
        "Pos",
        "Name",
        "Class",
        "Type",
        "Smoke01",
        "ParticleObject",
        "MidPosition",
        "EndPosition",
        "BoxDimsStart",
    ];
    names.push("TRG_SFX_SOB_Fire01", "EmitterObject", "BoundingBox", "CreatedAtStart", "ncomp_EmitterObject_1");
    names.push("TRG_Car01", "Vehicle", `${LEVEL}_NodeArray`);
    const global = (name: string, value: QbValue): Buffer =>
        Buffer.concat([encodeValue(NAME(name)), Buffer.from([0x07]), encodeValue(value), Buffer.from([0x01])]);
    const qb = Buffer.concat([
        global("ncomp_EmitterObject_1", emitterShared),
        global(`${LEVEL}_NodeArray`, nodes),
        ...names.map((n) =>
            Buffer.concat([Buffer.from([0x2b]), pack("<I", checksum(n)), Buffer.from(`${n}\0`, "latin1")]),
        ),
        Buffer.from([0x00]),
    ]);
    return { ...level(), qb };
}

test("layout_file: speed and emitter records as the DLL writes and refuses them", () => {
    const layout = readLayout(
        [
            "thug2-decomp layout 2",
            `scene ${hex(LEVEL)}`,
            `speed ${hex("TRG_Car01")} 27.125`,
            `emitter ${hex("TRG_SFX_SOB_Fire01")} sound=DEADBEEF vol=75.5 radius=320.25 later=1`,
            `emitter ${hex("ed_sound01")} sound=00000000 vol=100 radius=0`,
        ].join("\n"),
    );
    assert.deepEqual(layout.speeds, [{ name: checksum("TRG_Car01"), mph: 27.125 }]);
    assert.deepEqual(layout.emitters[0], {
        name: checksum("TRG_SFX_SOB_Fire01"),
        sound: 0xdeadbeef,
        volume: 75.5,
        radius: 320.25,
    });
    assert.equal(layout.emitters.length, 2);
    assert.equal(layout.unknown.length, 0);
    const refused = (line: string): void =>
        assert.throws(
            () => readLayout(["thug2-decomp layout 2", `scene ${hex(LEVEL)}`, line].join("\n")),
            (e: unknown) => e instanceof LayoutError && e.line === 3,
            line,
        );
    refused("speed 0000ABCD 0");
    refused("speed 0000ABCD 201");
    refused("speed ABCD 5");
    refused("emitter 00000001 sound=1");
    refused("emitter 00000001 vol=401");
    refused("emitter 00000001 radius=0.5");
    refused("emitter 00000001 vol=5 vol=6");
    assert.throws(() =>
        readLayout(["thug2-decomp layout 2", `scene ${hex(LEVEL)}`, "speed 0000ABCD 5", "speed 0000ABCD 6"].join("\n")),
    );
});

// (map4) A particle node turned: its Mid / End turned about its place and moved with it, its boxes turned (a quarter
// turn swaps x and z) - as the DLL's Entities::TurnParticle.
test("bake: a particle system turned - its emitter places and boxes with it", () => {
    const layout = readLayout(
        [
            "thug2-decomp layout 2",
            `scene ${hex(LEVEL)}`,
            `set ${hex("Smoke01")} pos=200,0,0 angles=0,1.57079637,0`,
        ].join("\n"),
    );
    const nodes = nodesOf(bake(layout, entityLevel(), null).qb);
    const smoke = named(nodes, "Smoke01");
    const near = (v: QbValue | undefined, want: number[]): void => {
        const got = (v as Vector).values;
        assert.ok(
            got.every((x, i) => Math.abs(x - want[i]!) < 1e-3),
            `${got.join(",")} is not ${want.join(",")}`,
        );
    };
    near(smoke.get("MidPosition"), [200, 20, -10]);
    near(smoke.get("EndPosition"), [200, 40, -20]);
    near(smoke.get("BoxDimsStart"), [10, 5, 30]);
    assert.deepEqual(
        (smoke.get("Angles") as Vector).values.map((x) => Math.round(x * 1000) / 1000),
        [0, 1.571, 0],
    );
    // Half a quarter: the box that holds the turned box.
    const eighth = nodesOf(
        bake(
            readLayout(
                ["thug2-decomp layout 2", `scene ${hex(LEVEL)}`, `set ${hex("Smoke01")} angles=0,0.78539819,0`].join(
                    "\n",
                ),
            ),
            entityLevel(),
            null,
        ).qb,
    );
    const r = Math.SQRT1_2;
    near(named(eighth, "Smoke01").get("BoxDimsStart"), [r * 40, 5, r * 40]);
    near(named(eighth, "Smoke01").get("MidPosition"), [100 + 10 * r, 20, -10 * r]);
});

test("bake: particle systems moved and copied with their emitter places; speeds and emitters' sounds as node fields", () => {
    const layout = readLayout(
        [
            "thug2-decomp layout 2",
            `scene ${hex(LEVEL)}`,
            `set ${hex("Smoke01")} pos=150,10,0`,
            `copy 0000ABCD ${hex("Smoke01")} 100 0 500 1 0 0 0 1 0 0 0 1`,
            `speed ${hex("TRG_Car01")} 30`,
            `emitter ${hex("TRG_SFX_SOB_Fire01")} sound=DEADBEEF vol=75.5 radius=320`,
            "node ed_sound01 EmitterObject pos=1,2,3 angles=0,0,0 radius=300",
            `emitter ${hex("ed_sound01")} sound=0000BEEF vol=100 radius=0`,
            "node TRG_Flag_Green GameObject type=Flag_Green pos=7,8,9 angles=0,0,0",
        ].join("\n"),
    );
    const out = bake(layout, entityLevel(), null);
    const nodes = nodesOf(out.qb);
    const smoke = named(nodes, "Smoke01");
    assert.deepEqual((smoke.get("Pos") as Vector).values, [150, 10, 0]);
    assert.deepEqual((smoke.get("MidPosition") as Vector).values, [160, 30, 0], "moved with its pos=");
    assert.deepEqual((smoke.get("EndPosition") as Vector).values, [170, 50, 0]);
    const copy = nodes.find((s) => (s.get("Name") as Name | undefined)?.value === 0xabcd)!;
    assert.ok(copy, "the copy appended");
    assert.deepEqual((copy.get("MidPosition") as Vector).values, [110, 20, 500], "relative to its source as loaded");
    assert.deepEqual((copy.get("EndPosition") as Vector).values, [120, 40, 500]);
    const car = named(nodes, "TRG_Car01");
    assert.equal((car.get("ed_path_speed") as Float).value, 30);
    const fire = named(nodes, "TRG_SFX_SOB_Fire01");
    assert.equal((fire.get("ed_sound") as Name).value, 0xdeadbeef);
    assert.equal((fire.get("ed_volume") as Float).value, 75.5);
    assert.equal((fire.get("Radius") as Float).value, 320);
    const added = named(nodes, "ed_sound01");
    assert.equal((added.get("ed_sound") as Name).value, 0xbeef);
    assert.equal((added.get("Radius") as Float).value, 300, "radius=0: the node record's own");
    assert.ok(!out.warnings.some((w) => w.includes("ed_sound01: silent")), "it has a sound now");
    assert.ok(out.warnings.some((w) => w.includes("ed_path_speed")));
    assert.ok(out.warnings.some((w) => w.includes("ed_sound / ed_volume")));
    const flag = named(nodes, "TRG_Flag_Green");
    assert.equal((flag.get("TriggerScript") as Name).value, checksum("TRG_Flag_GreenScript"), "a lobby flag's script");
    assert.equal(out.unsupported.length, 0);
});

// --- (UX7) linked copies -------------------------------------------------------------------------------------------

test("layout_file: (UX7) link records - read as Links::ReadRecord, malformed refused", () => {
    const l = readLayout(
        [
            "thug2-decomp layout 2",
            `scene ${hex(LEVEL)}`,
            "link sector 1A2B3C4D c0@1,DEADBEEF,c3@2",
            "link object 11111111 22222222 later=1",
        ].join("\n"),
    );
    assert.deepEqual(l.linkGroups, [
        {
            scenery: true,
            source: { id: 0x1a2b3c4d, clone: -1, quarters: 0 },
            members: [
                { id: 0, clone: 0, quarters: 1 },
                { id: 0xdeadbeef, clone: -1, quarters: 0 },
                { id: 0, clone: 3, quarters: 2 },
            ],
        },
        {
            scenery: false,
            source: { id: 0x11111111, clone: -1, quarters: 0 },
            members: [{ id: 0x22222222, clone: -1, quarters: 0 }],
        },
    ]);
    assert.deepEqual(l.unknown, []);
    for (const line of [
        "link object c1 22222222",
        "link object 11111111 22222222@1",
        "link sector 1A2B3C4D",
        "link thing 11111111 22222222",
        "link sector 1A2B3C4D c0@7",
        "link sector 1A2B3C4D 123",
    ]) {
        assert.throws(
            () => readLayout(`thug2-decomp layout 2\nscene ${hex(LEVEL)}\n${line}\n`),
            (e: unknown) => e instanceof LayoutError && e.line === 3,
            line,
        );
    }
});
