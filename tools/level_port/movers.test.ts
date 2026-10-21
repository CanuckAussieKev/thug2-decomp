// NOT ORIGINAL - tests of tools/level_port/movers.ts (non-car Vehicles made game objects: PC-THPS3-15).

import assert from "node:assert/strict";
import { test } from "node:test";
import { makeMovers, modelFile, modelTriggers, moversFor } from "./movers.ts";
import { checksum, Name, type QbValue, Struct, Vector } from "./qb.ts";
import * as thug2Col from "./thug2_col.ts";

const names = (): Map<number, string> =>
    new Map(
        [
            "Vehicle",
            "GameObject",
            "Restart",
            "LevelGeometry",
            "TRG_Bucket",
            "TRG_Restart_Near",
            "TRG_Restart_Far",
            "TRG_Car",
            "car",
        ].map((t) => [checksum(t), t]),
    );
const n = (text: string): Name => new Name(checksum(text));
const node = (entries: [string | null, QbValue][]): Struct =>
    new Struct(entries.map(([k, v]) => [k === null ? null : n(k), v]));
const at = (x: number, y: number, z: number): Vector => new Vector("vector", [x, y, z]);

function level(): Struct[] {
    return [
        node([
            ["Position", at(0, 0, 0)],
            ["Name", n("TRG_Restart_Far")],
            ["Class", n("Restart")],
        ]),
        node([
            ["Position", at(100, 0, 0)],
            ["Name", n("TRG_Restart_Near")],
            ["Class", n("Restart")],
        ]),
        node([
            ["Position", at(110, 5, 0)],
            ["Name", n("TRG_Bucket")],
            ["Class", n("Vehicle")],
            ["Type", n("normal")],
            [null, n("AbsentInNetGames")],
            ["model", "Obj_Bucket\\Obj_Bucket.mdl"],
            ["skeletonName", n("car")],
            ["TriggerScript", n("BucketScript")],
        ]),
        node([
            ["Position", at(500, 0, 0)],
            ["Name", n("TRG_Car")],
            ["Class", n("Vehicle")],
            ["model", "veh\\veh_van\\veh_van.mdl"],
        ]),
    ];
}

// A model collision: one solid object and one trigger object (non-collidable trigger faces, 0x50).
function modelCol(): Buffer {
    const object = (name: string, flags: number): thug2Col.ColObject => {
        const verts = [
            [0, 0, 0],
            [10, 0, 0],
            [0, 0, 10],
        ];
        const faces: thug2Col.Face[] = [[flags, 0, 0, 1, 2]];
        const o: thug2Col.ColObject = {
            checksum: checksum(name),
            flags: 0,
            smallFaces: 0,
            fixedVerts: 0,
            bbox: [0, 0, 0, 1, 10, 0, 10, 1],
            verts,
            intensities: Buffer.alloc(verts.length, 0x80),
            faces,
            reach: Buffer.alloc(0),
            bsp: thug2Col.buildTree(verts, faces),
        };
        thug2Col.setReaches(o);
        return o;
    };
    return thug2Col.writeCol({ objects: [object("TRG_g_Bucket", 0x880), object("TRGP_KillSkaterBucket", 0x50)] });
}

test("modelFile: the model's other files, lower case, backslashes", () => {
    assert.equal(modelFile("Obj_Bucket/Obj_Bucket.MDL", ".qb"), "obj_bucket\\obj_bucket.qb");
    assert.equal(modelFile("bullet_train\\bullet_train.mdl", ".col.xbx"), "bullet_train\\bullet_train.col.xbx");
});

test("moversFor: a level's objects only", () => {
    const objects = [
        { level: "foun", name: "A", model: "a.mdl" },
        { level: "Tok", name: "B", model: "b.mdl" },
    ];
    assert.deepEqual(
        moversFor(objects, "tok").map((m) => m.name),
        ["B"],
    );
    assert.deepEqual(moversFor(undefined, "tok"), []);
});

test("modelTriggers: trigger collision objects by checksum", () => {
    assert.deepEqual(modelTriggers(undefined, modelCol()), [
        `#${checksum("TRGP_KillSkaterBucket").toString(16).padStart(8, "0")}`,
    ]);
    assert.deepEqual(modelTriggers(undefined, undefined), []);
});

test("makeMovers: a non-car Vehicle becomes a solid game object; its kill trigger a stand-in", () => {
    const converted = level();
    const nm = names();
    const kills = new Map<number, string>();
    const models = new Map([[modelFile("Obj_Bucket\\Obj_Bucket.mdl", ".col.xbx"), modelCol()]]);
    const report = makeMovers(
        converted,
        nm,
        [
            {
                level: "foun",
                name: "TRG_Bucket",
                model: "Obj_Bucket\\Obj_Bucket.mdl",
                kill_triggers: [`#${checksum("TRGP_KillSkaterBucket").toString(16)}`],
            },
            { level: "foun", name: "TRG_Missing", model: "x.mdl" },
        ],
        models,
        kills,
        "PC-TEST",
    );
    const bucket = converted[2]!;
    assert.equal((bucket.get("Class") as Name).value, checksum("GameObject"));
    assert.equal((bucket.get("CollisionMode") as Name).value, checksum("Geometry"));
    assert.equal(bucket.get("skeletonName"), undefined);
    assert.equal(bucket.get("model"), "Obj_Bucket\\Obj_Bucket.mdl");
    assert.ok(bucket.hasFlag("AbsentInNetGames"));
    assert.equal((bucket.get("TriggerScript") as Name).value, checksum("BucketScript"));
    // the car untouched
    assert.equal((converted[3]!.get("Class") as Name).value, checksum("Vehicle"));
    // the stand-in: the trigger's name, LevelGeometry, a kill to the nearest restart
    assert.equal(converted.length, 5);
    const stand = converted[4]!;
    assert.equal((stand.get("Name") as Name).value, checksum("TRGP_KillSkaterBucket"));
    assert.equal((stand.get("Class") as Name).value, checksum("LevelGeometry"));
    const script = (stand.get("TriggerScript") as Name).value;
    assert.equal(kills.get(script), "SK3_KillSkater nodename=TRG_Restart_Near");
    assert.ok(report.some((l) => l.includes("TRG_Missing isn't in the node array")));
    assert.ok(report[0]!.includes("1 non-car Vehicles made game objects"));
});

test("makeMovers: a trigger without a kill gets a stand-in without a script; not a Vehicle / other model: unchanged", () => {
    const converted = level();
    const kills = new Map<number, string>();
    const models = new Map([[modelFile("Obj_Bucket\\Obj_Bucket.mdl", ".col.xbx"), modelCol()]]);
    makeMovers(
        converted,
        names(),
        [{ level: "x", name: "TRG_Bucket", model: "Obj_Bucket\\Obj_Bucket.mdl" }],
        models,
        kills,
        "T",
    );
    assert.equal(converted[4]!.get("TriggerScript"), undefined);
    assert.equal(kills.size, 0);
    // run again: no longer a Vehicle - nothing more
    const before = converted.length;
    const report = makeMovers(
        converted,
        names(),
        [{ level: "x", name: "TRG_Bucket", model: "Obj_Bucket\\Obj_Bucket.mdl" }],
        models,
        kills,
        "T",
    );
    assert.equal(converted.length, before);
    assert.ok(report[0]!.includes("isn't a Vehicle"));
    const other = level();
    const r2 = makeMovers(other, names(), [{ level: "x", name: "TRG_Car", model: "plane.mdl" }], new Map(), kills, "T");
    assert.equal((other[3]!.get("Class") as Name).value, checksum("Vehicle"));
    assert.ok(r2[0]!.includes("not plane.mdl"));
});
