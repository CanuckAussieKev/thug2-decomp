// NOT ORIGINAL - tests of tools/level_port/movers.ts (moving props made solid LevelObjects: PC-THPS3-15).

import assert from "node:assert/strict";
import { test } from "node:test";
import { makeSolid, moversFor } from "./movers.ts";
import { checksum, Name, type QbValue, Struct, Vector } from "./qb.ts";

const names = (): Map<number, string> => new Map(["LevelObject", "Vehicle", "Restart"].map((t) => [checksum(t), t]));
const n = (text: string): Name => new Name(checksum(text));
const node = (entries: [string | null, QbValue][]): Struct =>
    new Struct(entries.map(([k, v]) => [k === null ? null : n(k), v]));

function level(): Struct[] {
    return [
        node([
            ["Position", new Vector("vector", [1, 2, 3])],
            ["Name", n("Obj_Bucket")],
            ["Class", n("LevelObject")],
            [null, n("AbsentInNetGames")],
            ["TriggerScript", n("BucketScript")],
        ]),
        node([
            ["Name", n("TRG_Plane")],
            ["Class", n("Vehicle")],
        ]),
        node([
            ["Name", n("TRG_Undrawn")],
            ["Class", n("LevelObject")],
        ]),
    ];
}

test("moversFor: a level's objects only", () => {
    const objects = [
        { level: "foun", name: "A" },
        { level: "Tok", name: "B" },
    ];
    assert.deepEqual(
        moversFor(objects, "tok").map((m) => m.name),
        ["B"],
    );
    assert.deepEqual(moversFor(undefined, "tok"), []);
});

test("makeSolid: a drawn LevelObject gets CollisionMode=Geometry; others are left as they are", () => {
    const converted = level();
    const drawn = new Set([checksum("Obj_Bucket"), checksum("TRG_Plane")]);
    const movers = ["Obj_Bucket", "TRG_Plane", "TRG_Undrawn", "TRG_Missing"].map((name) => ({ level: "foun", name }));
    const [solid, report] = makeSolid(converted, names(), movers, drawn, "T");
    assert.deepEqual([...solid], [checksum("Obj_Bucket")]);
    const bucket = converted[0]!;
    assert.equal((bucket.get("CollisionMode") as Name).value, checksum("Geometry"));
    assert.equal((bucket.get("Class") as Name).value, checksum("LevelObject"));
    assert.ok(bucket.hasFlag("AbsentInNetGames"));
    assert.equal((bucket.get("TriggerScript") as Name).value, checksum("BucketScript"));
    assert.equal(converted[1]!.get("CollisionMode"), undefined); // (a Vehicle: not ours to change)
    assert.equal(converted[2]!.get("CollisionMode"), undefined); // (no drawn sector: no collision to take)
    assert.equal(report.length, 4);
    assert.ok(report[1]!.includes("not a LevelObject"));
    assert.ok(report[2]!.includes("no drawn sector"));
    assert.ok(report[3]!.includes("isn't in the node array"));
    // again: one CollisionMode only
    makeSolid(converted, names(), movers, drawn, "T");
    assert.equal(bucket.entries.filter(([k]) => k !== null && k.value === checksum("CollisionMode")).length, 1);
});
