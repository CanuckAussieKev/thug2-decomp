// NOT ORIGINAL - tests of what is new in THUGPRO (thugpro_new.ts, TPN-THUG1-1 ... 4; no game data): scripts cut out of
// and spliced into a .qb, the commands and nodes a script uses, the safety check, THUGPRO's nodes added / set with their
// checksums; and our own changes (port_changes.ts, PC-THUG1-n): the switches and the data agreeing with the code.

import assert from "node:assert/strict";
import { test } from "node:test";
import * as compat from "../compat/compat.ts";
import { checksum, Name, type QbValue, Struct, Vector } from "../level_port/qb.ts";
import type * as scriptPort from "../level_port/script_port.ts";
import { pack } from "../lib/struct.ts";
import * as portChanges from "./port_changes.ts";
import type { Side } from "./thugpro_edits.ts";
import {
    applyNodes,
    emptyScript,
    fieldsCrc,
    fromSection,
    type LevelRef,
    type NewLevel,
    nodeCrc,
    rawScripts,
    scriptCheck,
    scriptUses,
    spliceScripts,
    toSection,
} from "./thugpro_new.ts";

const name = (t: string): Buffer => Buffer.concat([Buffer.from([0x16]), pack("<I", checksum(t))]);
// script <s> { <lines: each a list of names, the first a command> }
const script = (s: string, lines: string[][]): Buffer =>
    Buffer.concat([
        Buffer.from([0x23]),
        name(s),
        Buffer.from([0x01]),
        ...lines.map((l) => Buffer.concat([...l.map(name), Buffer.from([0x01])])),
        Buffer.from([0x24, 0x01]),
    ]);
// a statement `cmd key=value`
const keyed = (cmd: string, key: string, value: string): Buffer =>
    Buffer.concat([name(cmd), name(key), Buffer.from([0x07]), name(value), Buffer.from([0x01])]);

test("rawScripts / emptyScript: a .qb's scripts as they are", () => {
    const a = script("A", [["Wait"]]);
    const b = script("B", [["Hide"], ["UnHide"]]);
    const raw = rawScripts(Buffer.concat([a, b, Buffer.from([0x00])]));
    assert.deepEqual([...raw.keys()], [checksum("A"), checksum("B")]);
    assert.ok(raw.get(checksum("A"))!.equals(a));
    assert.ok(raw.get(checksum("B"))!.equals(b));
    assert.ok(
        rawScripts(emptyScript(checksum("C")))
            .get(checksum("C"))!
            .equals(emptyScript(checksum("C"))),
    );
});

test("spliceScripts: replaced scripts cut out and appended, new ones added, the rest kept", () => {
    const a = script("A", [["Wait"]]);
    const b = script("B", [["Hide"]]);
    const newA = script("A", [["UnHide"]]);
    const c = script("C", [["Wait"]]);
    const compiled = Buffer.concat([a, b, Buffer.from([0x00])]);
    // (a stand-in for script_port.appendScripts: the scripts before the end)
    const append = (d: Buffer, m: ReadonlyMap<number, Uint8Array>): Buffer =>
        Buffer.concat([d.subarray(0, d.length - 1), ...[...m.values()].map((x) => Buffer.from(x)), Buffer.from([0])]);
    const out = spliceScripts(
        compiled,
        new Map([
            [checksum("A"), newA],
            [checksum("C"), c],
        ]),
        append,
    );
    const raw = rawScripts(out);
    assert.equal(raw.size, 3);
    assert.ok(raw.get(checksum("A"))!.equals(newA));
    assert.ok(raw.get(checksum("B"))!.equals(b));
    assert.ok(raw.get(checksum("C"))!.equals(c));
    assert.equal(out.length, newA.length + b.length + c.length + 1);
});

test("scriptUses: commands at a line's start / after if / after `:`, nodes by name= and as objects", () => {
    const code = Buffer.concat([
        Buffer.from([0x23]),
        name("S"),
        Buffer.from([0x01]),
        keyed("Obj_MoveToNode", "name", "TRG_Way"),
        Buffer.from([0x47, 0x00, 0x00]),
        name("GotParam"),
        name("on"),
        Buffer.from([0x01]),
        name("TRG_Car"),
        Buffer.from([0x42]),
        name("Obj_Hover"),
        Buffer.from([0x01, 0x24, 0x01]),
    ]);
    const { calls, nodeNames } = scriptUses(code);
    assert.deepEqual(
        [...calls].sort(),
        ["Obj_MoveToNode", "GotParam", "Obj_Hover"].map(checksum).sort((x, y) => x - y),
    );
    assert.deepEqual(
        [...nodeNames].sort(),
        [checksum("TRG_Way"), checksum("TRG_Car")].sort((x, y) => x - y),
    );
});

test("scriptCheck: THUG2's commands and the level's nodes only; THUG1's flags refused", () => {
    const thug2 = {
        root: "C:\\nowhere",
        cfuncs: new Set([checksum("Wait"), checksum("GetFlag")]),
        members: new Map([[checksum("Obj_MoveToNode"), 1]]),
        scripts: new Set<number>(),
        bodies: new Map<number, Set<number>>(),
    } as unknown as scriptPort.Thug2;
    const names = new Map(["Wait", "GetFlag", "LoadZone", "TRG_Way"].map((t) => [checksum(t), t]));
    const c = {
        thug2,
        defined: new Set<number>(),
        nodeNames: new Set([checksum("TRG_Way")]),
        levelNodes: new Set([checksum("TRG_Way"), checksum("TRG_Gone")]),
        names,
    };
    assert.equal(scriptCheck(script("S", [["Wait"]]), c), null);
    assert.match(scriptCheck(script("S", [["LoadZone"]]), c)!, /LoadZone, which THUG2 hasn't/);
    assert.match(scriptCheck(script("S", [["GetFlag"]]), c)!, /goals \/ flags/);
    assert.equal(scriptCheck(script("S", [["LoadZone"]]), { ...c, defined: new Set([checksum("LoadZone")]) }), null);
    const moveTo = (n: string): Buffer =>
        Buffer.concat([
            Buffer.from([0x23]),
            name("S"),
            Buffer.from([0x01]),
            keyed("Obj_MoveToNode", "name", n),
            Buffer.from([0x24, 0x01]),
        ]);
    assert.equal(scriptCheck(moveTo("TRG_Way"), c), null);
    assert.match(scriptCheck(moveTo("TRG_Gone"), c)!, /names node/);
    assert.equal(scriptCheck(moveTo("SomeScreenId"), c), null); // (not a node of the level: not checked)
    // (geometry kept without a scene sector: Create / Kill / IsAlive of it allowed, anything else refused)
    const lines = (...parts: [string, string][]): Buffer =>
        Buffer.concat([
            Buffer.from([0x23]),
            name("S"),
            Buffer.from([0x01]),
            ...parts.flatMap(([cmd, n]) => [keyed(cmd, "name", n), Buffer.from([0x01])]),
            Buffer.from([0x24, 0x01]),
        ]);
    const cmds = new Set(["Kill", "Create", "IsAlive", "Visible"].map(checksum));
    const u = { ...c, defined: cmds, undrawn: new Set([checksum("TRG_Gone")]) };
    assert.deepEqual(
        [...scriptUses(lines(["Kill", "TRG_Gone"], ["Visible", "TRG_Way"])).createKillOnly],
        [checksum("TRG_Gone")],
    );
    assert.equal(scriptCheck(lines(["Kill", "TRG_Gone"], ["IsAlive", "TRG_Gone"], ["Create", "TRG_Gone"]), u), null);
    assert.match(scriptCheck(lines(["Kill", "TRG_Gone"], ["Visible", "TRG_Gone"]), u)!, /names node/);
    assert.match(scriptCheck(lines(["Kill", "TRG_Gone"]), { ...c, defined: cmds })!, /names node/); // (not known to be undrawn geometry)
});

const nm = (t: string, n: Map<number, string>): Name => {
    n.set(checksum(t), t);
    return new Name(checksum(t));
};

test("applyNodes: THUGPRO's nodes added (Links by name) and values set; checksums and missing targets", () => {
    const n = new Map<number, string>();
    const node = (entries: [string | null, QbValue][]): Struct =>
        new Struct(entries.map(([k, v]) => [k === null ? null : nm(k, n), v]));
    const array: QbValue[] = [
        node([
            ["Name", nm("TRG_Way", n)],
            ["Class", nm("Waypoint", n)],
        ]),
        node([
            ["Name", nm("TRG_546", n)],
            ["Class", nm("Pedestrian", n)],
            ["profile", nm("Ped_Bum_01", n)],
        ]),
    ];
    const theirs = [
        node([
            ["Name", nm("TRG_Way", n)],
            ["Class", nm("Waypoint", n)],
        ]),
        node([
            ["Name", nm("TRG_BUM_01", n)],
            ["Class", nm("Pedestrian", n)],
            ["model", "skater_secret\\bum\\bum.skin"],
        ]),
        node([
            ["Pos", new Vector("vector", [1, 2, 3])],
            ["Name", nm("TRG_Train", n)],
            ["Class", nm("Vehicle", n)],
            ["model", "veh\\train\\train.mdl"],
            ["Links", [0]],
        ]),
    ];
    const tp: Side = { nodes: theirs, names: n, qbs: new Map(), scn: null, col: null };
    const fields = ["Name", "profile", "model"];
    const level: NewLevel = {
        key: "vn",
        archive: "data\\pre\\VN_scripts.prx",
        entries: [
            {
                id: "TPN-THUG1-2",
                kind: "node",
                op: "add",
                target: "TRG_Train",
                why: "",
                crc32: nodeCrc(theirs[2]!, tp),
            },
            {
                id: "TPN-THUG1-2",
                kind: "node",
                op: "set",
                target: "TRG_546",
                node: "TRG_BUM_01",
                fields,
                why: "",
                crc32: fieldsCrc(theirs[1]!, fields),
            },
            {
                id: "TPN-THUG1-2",
                kind: "node",
                op: "set",
                target: "TRG_Way",
                node: "TRG_Way",
                fields: [],
                why: "",
                crc32: "00000000",
            },
        ],
    };
    const names = new Map(n);
    const r = applyNodes(array, names, level, tp, () => true);
    assert.equal(array.length, 3);
    const train = array[2] as Struct;
    assert.deepEqual(train.get("Links"), [0]);
    assert.ok(r.forced.has(train));
    const bum = array[1] as Struct;
    assert.equal((bum.get("Name") as Name).value, checksum("TRG_BUM_01"));
    assert.equal(bum.get("profile"), undefined); // (THUGPRO's node hasn't it: taken off)
    assert.equal(bum.get("model"), "skater_secret\\bum\\bum.skin");
    assert.equal(r.forced.size, 2);
    assert.match(r.skipped.join("\n"), /TRG_Way: THUGPRO's values differ/);
    // a model that couldn't be had: the node left out; "set" on a node the level hasn't: an error reported
    const again: QbValue[] = [array[0]!];
    const r2 = applyNodes(
        again,
        new Map(n),
        { ...level, entries: [level.entries[0]!, level.entries[1]!] },
        tp,
        (m) => !m.includes("train"),
    );
    assert.equal(again.length, 1);
    assert.match(r2.skipped.join("\n"), /model veh\\train\\train.mdl couldn't be had/);
    assert.match(r2.skipped.join("\n"), /set TRG_546: ERROR - THUG1's level has no such node/);
});

test("port_changes: required ones always on, the others switchable; ids must be the code's", () => {
    const entry = (id: string, op: compat.Op, required: boolean): compat.Entry => ({
        id,
        op,
        target: { levels: "all", what: "t" },
        why: "w",
        required,
    });
    const entries = [entry("PC-THUG1-1", "add", false), entry("PC-THUG1-6", "remove", true)];
    const on = new portChanges.PortChanges(entries, true);
    const off = new portChanges.PortChanges(entries, false);
    assert.equal(on.on("PC-THUG1-1"), true);
    assert.equal(off.on("PC-THUG1-1"), false);
    assert.equal(off.on("PC-THUG1-6"), true);
    assert.equal(on.on("PC-THUG1-2"), false); // (not in the data: off)
    off.required("PC-THUG1-6");
    assert.throws(() => off.required("PC-THUG1-1"));
    assert.throws(() => new portChanges.PortChanges([entry("PC-THUG1-99", "add", false)]));
    assert.throws(() => new portChanges.PortChanges([{ ...entries[0]!, op: "upsert" as compat.Op }]));
});

test("port_changes: the register's data has every change the code makes, in compat.Entry's shape", () => {
    const list = compat.portChanges("thug1_levels"); // (compat checks id, op, target, why)
    const data = new portChanges.PortChanges(list);
    assert.deepEqual([...data.entries.keys()].sort(), [...portChanges.IMPLEMENTED.keys()].sort());
    for (const e of list) assert.ok(typeof e.rule === "string" && typeof e.required === "boolean" && e.target.what);
    // (--compat none keeps them: they're ours, not THUGPRO's - read from the thugpro folder whatever the profile)
    assert.equal(compat.load("none").section("thug1_levels", "port_changes").entries, undefined);
});

test("thugpro_new: the section's form (one list, target {level, name}) and back", () => {
    const level: NewLevel = {
        key: "nj",
        archive: "data\\pre\\NJ_scripts.prx",
        entries: [
            {
                id: "TPN-THUG1-3",
                kind: "script",
                op: "remove",
                target: "TRGP_X",
                why: "w",
                crc32: "12345678",
                file: "f",
            },
        ],
    };
    const section = toSection(new Map([["NJ", level]]));
    assert.deepEqual(section.levels, { NJ: { key: "nj", archive: "data\\pre\\NJ_scripts.prx" } });
    assert.deepEqual(section.entries[0]!.target, { level: "NJ", name: "TRGP_X" });
    const back = fromSection(section.entries as unknown as compat.Entry[], section.levels);
    assert.deepEqual(back.get("nj"), level);
    // (the data in the register: in compat.Entry's shape, every entry's level listed)
    const profile = compat.load("thugpro");
    const refs = (profile.section("thug1_levels", "thugpro_new").levels ?? {}) as Record<string, LevelRef>;
    const levels = fromSection(compat.thugproNew(profile, "thug1_levels"), refs);
    assert.ok(levels.size >= 10);
    assert.deepEqual(compat.thugproNew(compat.load("none"), "thug1_levels"), []);
});
