// NOT ORIGINAL - unit tests of THUGPRO's new content (thugpro_new.ts) and the port_changes reader (no game data).

import assert from "node:assert/strict";
import { test } from "node:test";
import { PortChanges } from "../compat/compat.ts";
import * as qb from "../level_port/qb.ts";
import { Name, Struct } from "../level_port/qb.ts";
import { pack } from "../lib/struct.ts";
import { adaptScript, applyNodes, emptyNew, type NewEntry, rowId, scriptBytes, scriptNames } from "./thugpro_new.ts";

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
        target,
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
        { id: "PC-THPS3-1", op: "remove" as const, target: "t", why: "w", levels: ["ware"] },
        { id: "PC-THPS3-2", op: "set" as const, target: "t", why: "w", required: true },
    ];
    const on = new PortChanges(entries, true);
    assert.ok(on.on("PC-THPS3-1", "ware"));
    assert.ok(!on.on("PC-THPS3-1", "foun"));
    const off = new PortChanges(entries, false);
    assert.ok(!off.on("PC-THPS3-1", "ware"));
    assert.ok(off.on("PC-THPS3-2"));
    assert.throws(() => on.on("PC-THPS3-99"));
});
