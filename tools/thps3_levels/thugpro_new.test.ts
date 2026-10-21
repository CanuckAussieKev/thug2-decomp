// NOT ORIGINAL - unit tests of THUGPRO's new content (thugpro_new.ts) and the port_changes reader (no game data).

import assert from "node:assert/strict";
import { test } from "node:test";
import * as qb from "../level_port/qb.ts";
import { Name, Struct } from "../level_port/qb.ts";
import { pack } from "../lib/struct.ts";
import { PortChanges } from "./thps3_levels.ts";
import {
    adaptScript,
    applyNodes,
    emptyNew,
    flagChanges,
    globalsNs,
    type NewEntry,
    nodeBytes,
    resolveGlobals,
    rowId,
    scriptBytes,
    scriptNames,
} from "./thugpro_new.ts";

const name = (text: string): Buffer => Buffer.concat([Buffer.from([0x16]), pack("<I", qb.checksum(text))]);
const nl = Buffer.from([0x01]);

// script Foo  /  Bar a=obj:Baz  /  if Qux { Quux }  -  as THUG2 tokens
function sample(): Buffer {
    return Buffer.concat([
        Buffer.from([0x23]),
        name("Foo"),
        nl,
        name("THUGPRO_ForceCreate"),
        name("a"),
        Buffer.from([0x07]),
        name("obj"),
        Buffer.from([0x42]),
        name("Baz"),
        nl,
        Buffer.from([0x03]),
        name("Inside"),
        Buffer.from([0x04]),
        nl,
        Buffer.from([0x24]),
        nl,
        Buffer.from([0x00]),
    ]);
}

test("scriptBytes / scriptNames: calls start statements, not object: or struct contents", () => {
    const scripts = scriptBytes(sample());
    const code = scripts.get(qb.checksum("Foo"))!;
    assert.ok(code !== undefined);
    const { calls, refs } = scriptNames(code);
    assert.deepEqual([...calls], [qb.checksum("THUGPRO_ForceCreate")]);
    for (const r of ["a", "obj", "Baz", "Inside"]) assert.ok(refs.has(qb.checksum(r)), r);
    assert.ok(!refs.has(qb.checksum("Foo")));
});

test("adaptScript swaps a name, the bytes keep their length", () => {
    const code = scriptBytes(sample()).get(qb.checksum("Foo"))!;
    const out = adaptScript(code, { THUGPRO_ForceCreate: "Create" });
    assert.equal(out.length, code.length);
    assert.ok(scriptNames(out).calls.has(qb.checksum("Create")));
    assert.ok(!scriptNames(out).calls.has(qb.checksum("THUGPRO_ForceCreate")));
    assert.equal(adaptScript(code, undefined), code);
});

test("nodeBytes: names unknown to it as the register's #<hex digits> (its CRCs were made so)", () => {
    const node = new Struct([[new Name(qb.checksum("Class")), new Name(0x1ca1ff20)]]);
    const text = nodeBytes(node).toString("latin1");
    assert.ok(text.includes("#1CA1FF20"), text);
    assert.ok(!text.includes("#20FFA11C"), text);
});

test("adaptScript flags: Change <g>=0 / 1 of a level-flag number as UnSetFlag / SetFlag flag=<g>", () => {
    const int = (v: number): Buffer => Buffer.concat([Buffer.from([0x17]), pack("<i", v)]);
    const change = (v: number): Buffer =>
        Buffer.concat([name("Change"), name("BDJ_SHP_ANNOUNCE_ON"), Buffer.from([0x07]), int(v), nl]);
    const code = Buffer.concat([
        Buffer.from([0x23]),
        name("Foo"),
        nl,
        change(0),
        change(1),
        change(2),
        Buffer.concat([name("Change"), name("Other"), Buffer.from([0x07]), int(0), nl]),
        Buffer.from([0x24]),
        nl,
    ]);
    const flag = new Set([qb.checksum("BDJ_SHP_ANNOUNCE_ON")]);
    assert.equal(flagChanges(code, flag).length, 2);
    const out = adaptScript(code, undefined, flag);
    assert.equal(out.length, code.length);
    const expect = (cmd: string): Buffer =>
        Buffer.concat([name(cmd), name("flag"), Buffer.from([0x07]), name("BDJ_SHP_ANNOUNCE_ON"), nl]);
    assert.ok(out.includes(expect("UnSetFlag")));
    assert.ok(out.includes(expect("SetFlag")));
    assert.ok(out.includes(change(2)), "only 0 / 1 are flag changes");
    assert.ok(out.includes(Buffer.concat([name("Change"), name("Other")])), "other globals untouched");
});

test("resolveGlobals: THPS3's names kept (a Changed flag number marked), THUGPRO's own written", () => {
    const n = emptyNew();
    n.globals.set(qb.checksum("BDJ_SHP_ANNOUNCE_ON"), 0);
    n.globals.set(qb.checksum("shp_window_objects"), [new Name(qb.checksum("sl_GlassSide01"))]);
    const code = Buffer.concat([
        name("Change"),
        name("BDJ_SHP_ANNOUNCE_ON"),
        Buffer.from([0x07, 0x17]),
        pack("<i", 1),
        nl,
    ]);
    n.scripts.set(qb.checksum("Foo"), { code, entry: { op: "add" } as NewEntry });
    const thps3 = new qb.QbFile(
        Buffer.concat([name("BDJ_SHP_ANNOUNCE_ON"), Buffer.from([0x07, 0x17]), pack("<i", 14), nl, Buffer.from([0])]),
        "thps3",
    );
    const report = resolveGlobals(n, [thps3]);
    assert.equal(report.length, 1);
    assert.ok(n.flagGlobals.has(qb.checksum("BDJ_SHP_ANNOUNCE_ON")));
    assert.ok(n.thps3Globals.has(qb.checksum("BDJ_SHP_ANNOUNCE_ON")));
    const names = new Map([[qb.checksum("shp_window_objects"), "shp_window_objects"]]);
    names.set(qb.checksum("sl_GlassSide01"), "sl_GlassSide01");
    assert.equal(globalsNs(n, names, new Set()), "shp_window_objects = [sl_GlassSide01]\n");
    assert.equal(globalsNs(n, names, new Set([qb.checksum("shp_window_objects")])), "");
});

test("rowId: one register row per kind", () => {
    assert.equal(rowId("sector", "add"), "TPN-THPS3-1");
    assert.equal(rowId("collision", "add"), "TPN-THPS3-1");
    assert.equal(rowId("node", "add"), "TPN-THPS3-2");
    assert.equal(rowId("node", "set"), "TPN-THPS3-3");
    assert.equal(rowId("script", "add"), "TPN-THPS3-4");
    assert.equal(rowId("script", "set"), "TPN-THPS3-5");
    assert.equal(rowId("model", "add"), "TPN-THPS3-6");
});

test("applyNodes: added nodes' Links by name, set fields, a missing set target reported", () => {
    const names = new Map<number, string>();
    const node = (text: string, cls: string): Struct =>
        new Struct([
            [new Name(qb.checksum("Name")), new Name(qb.checksum(text))],
            [new Name(qb.checksum("Class")), new Name(qb.checksum(cls))],
        ]);
    const converted = [node("Ours", "LevelObject")];
    const entry = (target: string, op: "add" | "set"): NewEntry => ({
        op,
        id: rowId("node", op),
        kind: "node",
        target: { level: "foun", name: target },
        why: "test",
        archive: "x.prx",
    });
    const n = emptyNew();
    n.nodes.push({ node: node("New1", "Waypoint"), links: ["New2", "Ours", "Gone"], entry: entry("New1", "add") });
    n.nodes.push({ node: node("New2", "Waypoint"), links: [], entry: entry("New2", "add") });
    n.sets.push({
        name: qb.checksum("Ours"),
        fields: { Class: "Vehicle", model: "a\\a.mdl" },
        dropSector: true,
        entry: entry("Ours", "set"),
    });
    n.sets.push({
        name: qb.checksum("Missing"),
        fields: { Class: "Vehicle" },
        dropSector: false,
        entry: entry("Missing", "set"),
    });
    const report = applyNodes(converted, names, n);
    assert.equal(converted.length, 3);
    assert.deepEqual(converted[1]!.get("Links"), [2, 0]);
    assert.equal((converted[0]!.get("Class") as Name).value, qb.checksum("Vehicle"));
    assert.equal(converted[0]!.get("model"), "a\\a.mdl");
    assert.ok(report.some((l) => l.includes("set Missing: not in our node array")));
    assert.ok(report.some((l) => l.includes("1 links to nodes we haven't dropped")));
});

test("PortChanges: off without --no-port-changes only for entries not required, per level", () => {
    const entries = [
        { id: "PC-THPS3-1", op: "remove" as const, target: { what: "t", levels: ["ware"] }, why: "w" },
        { id: "PC-THPS3-2", op: "set" as const, target: { what: "t", levels: "*" }, why: "w", required: true },
    ];
    const on = new PortChanges(entries, true);
    assert.ok(on.on("PC-THPS3-1", "ware"));
    assert.ok(!on.on("PC-THPS3-1", "foun"));
    const off = new PortChanges(entries, false);
    assert.ok(!off.on("PC-THPS3-1", "ware"));
    assert.ok(off.on("PC-THPS3-2"));
    assert.throws(() => on.on("PC-THPS3-99"));
});
