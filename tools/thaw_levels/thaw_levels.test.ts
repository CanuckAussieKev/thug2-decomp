// NOT ORIGINAL - tests of the THAW converter's pure parts and the Python helpers it brought into tools/lib (formatG,
// pySplit, pyTitle, pySplitlines' keepends), against values Python 3.14 gave (no game data).

import assert from "node:assert/strict";
import { test } from "node:test";
import { checksum, Name, Struct, Vector } from "../level_port/qb.ts";
import { formatG, pySplit, pySplitlines, pyTitle } from "../lib/py.ts";
import { pack } from "../lib/struct.ts";
import { migrateText, quarterTurn } from "./migrate_layouts.ts";
import { inlineStruct, statements, tokens } from "./thaw.ts";
import {
    applyNodeFlags,
    applySpawns,
    COMPAT,
    levelName,
    moveTree,
    netOnlyRails,
    nodeFlagScript,
    setCompat,
    sixteenths,
    zoneNodeFlags,
} from "./thaw_levels.ts";
import { RAIL_SCRIPTS, scriptToks } from "./thaw_scripts.ts";

test("formatG is Python's %g / %.9g: exact value, halves to even", () => {
    const values = [100.0625, -0.0, 0.0, 12660.4375, -61514.9375, 1e-5, 0.0001, 123456789.0, 1234567.0, 999999.5];
    assert.deepEqual(
        values.map((v) => formatG(v)),
        ["100.062", "-0", "0", "12660.4", "-61514.9", "1e-05", "0.0001", "1.23457e+08", "1.23457e+06", "1e+06"],
    );
    assert.deepEqual(
        values.map((v) => formatG(v, 9)),
        ["100.0625", "-0", "0", "12660.4375", "-61514.9375", "1e-05", "0.0001", "123456789", "1234567", "999999.5"],
    );
    assert.equal(formatG(Number.POSITIVE_INFINITY), "inf");
});

test("pySplit, pyTitle, pySplitlines(keepends) are Python's", () => {
    assert.deepEqual(pySplit(" a\tb\x1c c\xa0d  "), ["a", "b", "c", "d"]);
    assert.equal(pyTitle("EAST LOS ANGELES"), "East Los Angeles");
    assert.equal(pyTitle("it's x2b"), "It'S X2B");
    assert.deepEqual(pySplitlines("a\r\nb\x0bc\n\nd", true), ["a\r\n", "b\x0b", "c\n", "\n", "d"]);
});

test("migrateText moves every record by the frames' difference", () => {
    const text =
        "scene 41AFB2FF  # thaw_ma\nsector 1 1 10 20 30 x\nobject 3 1.5 2 3#c\r\n\n  # only\ncopy A B -1 -2 -3";
    assert.deepEqual(migrateText(text, 0x41afb2ff, 0x12345678, [0.5, -1.0, 2.25], "z_ma"), [
        "scene 12345678  # z_ma\nsector 1 1 8.25 20 32.75 x\nobject 3 2 1 5.25  #c\n\n  # only\ncopy A B -0.5 -3 -0.75",
        3,
    ]);
    assert.throws(() => migrateText("scene 00000001\n", 2, 3, [0, 0, 0]));
    assert.deepEqual(quarterTurn([1, 2, 3], 1), [3, 2, -1]);
    assert.deepEqual(quarterTurn([1, 2, 3], 6), [-1, 2, -3]);
});

test("frames: whole sixteenths, BSP splits moved, level names", () => {
    assert.deepEqual(sixteenths([0.03, -0.03125, 100.09375]), [0, 0, 100.125]);
    const tree = moveTree(
        {
            kind: "split",
            axis: 2,
            point: 160,
            left: { kind: "leaf", faces: [0], low: 3 },
            right: { kind: "leaf", faces: [1], low: 3 },
        },
        [0, 0, -2.5],
    );
    assert.equal(tree.kind === "split" && tree.point, 120);
    assert.equal(levelName("Z_MA"), "z_ma");
    assert.equal(levelName("Z_MA", true), "thaw_ma");
    assert.equal(levelName("Z_CH"), "skatepark"); // (THUGPRO's name: tools/compat/thugpro, TP-1)
});

test("THAW script tokens: 0x4A inline structures, statements", () => {
    // an inline structure {a=5}: {u32 0x00010000, u32 first entry} + one 16-byte entry (int 1 << 1 | 1)
    const struct = Buffer.concat([pack("<II", 0x10000, 8), Buffer.from([0, 3, 0, 0]), pack("<III", 0xabc, 5, 0)]);
    const body = Buffer.concat([
        Buffer.from([0x16]),
        pack("<I", 0x1234),
        Buffer.from([0x4a]),
        pack("<H", struct.length),
        // (no padding: the structure starts at 8, already aligned to 4 from the script's start)
        struct,
        Buffer.from([0x01, 0x24]),
    ]);
    const toks = [...tokens(body)];
    assert.deepEqual(
        toks.map(([off, t]) => [off, t]),
        [
            [0, 0x16],
            [5, 0x4a],
            [32, 0x01],
            [33, 0x24],
        ],
    );
    const s = inlineStruct(toks[1]![2], new Map());
    assert.ok(s instanceof Struct);
    assert.ok(s.entries[0]![0] instanceof Name && s.entries[0]![0].value === 0xabc && s.entries[0]![1] === 5);
    assert.equal(statements(body).length, 1);
});

test("THAW's `if not skater:InManual` becomes `skater:GetSingleTag state` + `if not (<state> = Skater_InManual)`", () => {
    const name = (text: string): Buffer => Buffer.concat([Buffer.from([0x16]), pack("<I", checksum(text))]);
    const body = Buffer.concat([
        Buffer.from([0x47]),
        pack("<H", 0),
        Buffer.from([0x39]),
        name("Skater"),
        Buffer.from([0x42]),
        name("InManual"),
        Buffer.from([0x01]),
        name("StartGap"),
        Buffer.from([0x01, 0x28, 0x01, 0x24]),
    ]);
    const names = new Map(["Skater", "StartGap"].map((n) => [checksum(n), n]));
    const shown = scriptToks(body, names).map((t) => (t.t === 0x16 ? names.get(t.name()!) : t.t));
    assert.deepEqual(shown, [
        "Skater",
        0x42,
        "GetSingleTag",
        "state",
        0x01,
        0x25,
        0x39,
        0x0e,
        0x2d,
        "state",
        0x07,
        "Skater_InManual",
        0x0f,
        0x01,
        "StartGap",
        0x01,
        0x28,
        0x01,
    ]);
});

test("applySpawns (TP-4):changes a named restart, adds one with the drop-in, nothing without the profile", () => {
    const names = new Map<number, string>([[checksum("Restart"), "Restart"]]);
    const restart = (): Struct =>
        new Struct([
            [new Name(checksum("Pos")), new Vector("vector", [1, 2, 3])],
            [new Name(checksum("Name")), new Name(checksum("R1"))],
            [new Name(checksum("Class")), new Name(checksum("Restart"))],
            [new Name(checksum("restart_types")), [new Name(checksum("Multiplayer"))]],
        ]);
    const converted = [restart()];
    COMPAT.spawns = new Map([
        [
            "Z_XX",
            {
                edits: [
                    { node: "R1", pos: [4, 5, 6], restart_types: [] },
                    {
                        pos: [7, 8, 9],
                        angles: [0, 1, 0],
                        type: "Generic",
                        restart_types: ["Multiplayer"],
                        dropin: true,
                    },
                ],
                order: ["TRG_Net_Spawn01"],
            },
        ],
    ]);
    const report: string[] = [];
    const script = applySpawns("z_xx", "z_xx", converted, names, report);
    assert.equal(script, "script z_xx_net_dropin {\n    MakeSkaterGoto DropIn \n}\n");
    assert.deepEqual((converted[0]!.get("Pos") as Vector).values, [4, 5, 6]);
    assert.equal(converted[0]!.get("restart_types"), undefined);
    assert.equal(converted.length, 2);
    assert.deepEqual((converted[1]!.get("Pos") as Vector).values, [7, 8, 9]);
    assert.ok(converted[1]!.get("TriggerScript") instanceof Name);
    assert.deepEqual(report, ["  TP-4: 1 spawn(s) as THUGPRO's, 1 added, 0 moved to THUGPRO's order"]);
    // the order: two spawns swapped into THUGPRO's order, a Links to one of them follows it
    const spawn = (name: string): Struct =>
        new Struct([
            [new Name(checksum("Name")), new Name(checksum(name))],
            [new Name(checksum("Class")), new Name(checksum("Restart"))],
            [new Name(checksum("restart_types")), [new Name(checksum("Multiplayer"))]],
        ]);
    names.set(checksum("Multiplayer"), "Multiplayer");
    const other = new Struct([[new Name(checksum("Links")), [2]]]);
    const ordered = [spawn("A"), other, spawn("B")];
    COMPAT.spawns = new Map([["Z_XX", { edits: [], order: ["B", "A"] }]]);
    applySpawns("z_xx", "z_xx", ordered, names, []);
    assert.equal(ordered[0]!.get("Name")?.toString(), new Name(checksum("B")).toString());
    assert.deepEqual(
        [ordered[0], ordered[2]].map((n) => (n!.get("Name") as Name).value),
        [checksum("B"), checksum("A")],
    );
    assert.deepEqual(other.get("Links"), [0]);
    assert.throws(() => {
        COMPAT.spawns = new Map([["Z_XX", { edits: [], order: ["B"] }]]);
        applySpawns("z_xx", "z_xx", [spawn("A"), spawn("B")], names, []);
    });
    setCompat("none");
    const untouched = [restart()];
    assert.equal(applySpawns("z_xx", "z_xx", untouched, names, []), "");
    assert.deepEqual((untouched[0]!.get("Pos") as Vector).values, [1, 2, 3]);
    setCompat("thugpro");
});

test("RailAccelerate passes its parameters (a rail's rate=) on to RailAccelerate2, as THAW's skutils.qb does", () => {
    assert.match(RAIL_SCRIPTS, /Obj_SpawnScript RailAccelerate2 params=<\.\.\.>\n/);
    assert.match(RAIL_SCRIPTS, /script RailAccelerate2 \{rate=0\.05\n/);
});

test("RailAccelerate starts the loop on THUG2's rail event (grind onto), not land onto - never true on a rail", () => {
    assert.match(RAIL_SCRIPTS, /if TriggerType \[3, 13, -1\] \{\n/);
});

test("node flags: the non-story script runs the initial one first, then its own list; flagged nodes made at the start", () => {
    const name = (text: string): Buffer => Buffer.concat([Buffer.from([0x16]), pack("<I", checksum(text))]);
    const flag = (text: string, on: number): Buffer =>
        Buffer.concat([Buffer.from([0x03]), name(text), Buffer.from([0x17]), pack("<i", on), Buffer.from([0x04])]);
    const flags = (...entries: Buffer[]): Buffer =>
        Buffer.concat([
            name("Flags"),
            Buffer.from([0x07, 0x05]),
            ...entries,
            Buffer.from([0x06, 0x01]),
            name("ChangeNodeFlag"),
            Buffer.from([0x01, 0x24]),
        ]);
    const scripts = new Map([
        [checksum("initial"), flags(flag("NODEFLAG_A", 1), flag("NODEFLAG_B_DEF", 1))],
        [
            checksum("non_story"),
            Buffer.concat([
                Buffer.from([0x01]),
                name("initial"),
                Buffer.from([0x01]),
                flags(flag("NODEFLAG_B_DEF", 0), flag("NODEFLAG_B", 1)),
            ]),
        ],
    ]);
    const values = nodeFlagScript(scripts, checksum("non_story"));
    assert.deepEqual(
        [...values].map(([k, v]) => [k, v]),
        [
            [checksum("NODEFLAG_A"), true],
            [checksum("NODEFLAG_B_DEF"), false],
            [checksum("NODEFLAG_B"), true],
        ],
    );
    // (a list applied some other way than ChangeNodeFlag: not guessed at)
    assert.throws(() => nodeFlagScript(new Map([[1, Buffer.concat([flag("NODEFLAG_A", 1), Buffer.from([0x24])])]]), 1));
    const node = (flagName: string | null, created = false): Struct => {
        const n = new Struct([[new Name(checksum("Class")), new Name(checksum("LevelGeometry"))]]);
        if (flagName) n.entries.push([new Name(checksum("CreatedFromVariable")), new Name(checksum(flagName))]);
        if (created) n.entries.push([null, new Name(checksum("CreatedAtStart"))]);
        return n;
    };
    const array = [
        node("NODEFLAG_B"),
        node("NODEFLAG_B_DEF"),
        node("NODEFLAG_UNSET"),
        node(null),
        node("NODEFLAG_A", true),
    ];
    assert.equal(applyNodeFlags(array, values), 1);
    assert.deepEqual(
        array.map((n) => n.hasFlag("CreatedAtStart")),
        [true, false, false, false, true],
    );
    assert.equal(array[4]!.entries.filter(([k]) => k === null).length, 1);
});

test("TP-40: THUGPRO's node flag values on top of THAW's non-story ones, per zone; THAW's own with --compat none", () => {
    setCompat("thugpro");
    const k = checksum;
    // THAW's non-story state as recorded in the profile (Skate Ranch: the dino's stand-in, no dino head)
    const thaw = new Map([
        [k("NODEFLAG_Z_SR_HO_DINO"), false],
        [k("NODEFLAG_Z_SR_HO_DINO_DEF"), true],
        [k("NODEFLAG_Z_SR_HO_ROPES"), true],
        [k("NODEFLAG_OI_Derrick_After"), true],
    ]);
    const report: string[] = [];
    const sr = zoneNodeFlags("z_sr", thaw, report);
    assert.equal(sr.get(k("NODEFLAG_Z_SR_HO_DINO")), true);
    assert.equal(sr.get(k("NODEFLAG_Z_SR_HO_DINO_DEF")), false);
    assert.equal(sr.get(k("NODEFLAG_Z_SR_EL_MATTRESS")), true); // (unset in THAW: off; THUGPRO's on)
    assert.equal(sr.get(k("NODEFLAG_Z_SR_HO_ROPES")), true); // (the same in both: THAW's)
    assert.equal(sr.get(k("NODEFLAG_OI_Derrick_After")), true); // (another zone's flag: not touched here)
    assert.equal(thaw.get(k("NODEFLAG_Z_SR_HO_DINO")), false); // (THAW's map itself unchanged)
    // every recorded THAW value matched but the ones this test left out of its map (all off there)
    assert.ok(report.every((line) => !line.includes("HO_DINO") || line.includes("node flags as")));
    assert.equal(zoneNodeFlags("z_oi", thaw).get(k("NODEFLAG_OI_Derrick_After")), false);
    assert.equal(zoneNodeFlags("z_ch", thaw), thaw); // (no entries: THAW's)
    // a THAW value other than the recorded one is reported
    const changed: string[] = [];
    zoneNodeFlags("z_sr", new Map([[k("NODEFLAG_Z_SR_HO_DINO"), true]]), changed);
    assert.ok(changed.some((line) => line.includes("NODEFLAG_Z_SR_HO_DINO is on in THAW")));
    // the flagged nodes made as THUGPRO's
    const node = (flag: string): Struct =>
        new Struct([[new Name(checksum("CreatedFromVariable")), new Name(checksum(flag))]]);
    const array = [node("NODEFLAG_Z_SR_HO_DINO"), node("NODEFLAG_Z_SR_HO_DINO_DEF")];
    assert.equal(applyNodeFlags(array, sr), 1);
    assert.deepEqual(
        array.map((n) => n.hasFlag("CreatedAtStart")),
        [true, false],
    );
    setCompat("none");
    assert.equal(zoneNodeFlags("z_sr", thaw), thaw);
    setCompat("thugpro");
});

test("netOnlyRails (PC-THAW-15): Santa Monica's static ferris rails made only in network games", () => {
    const names = new Map<number, string>();
    const node = (name: string, cls: string): Struct => {
        names.set(checksum(name), name);
        names.set(checksum(cls), cls);
        return new Struct([
            [new Name(checksum("Pos")), new Vector("vector", [1.0, 2.0, 3.0])],
            [new Name(checksum("Name")), new Name(checksum(name))],
            [new Name(checksum("Class")), new Name(checksum(cls))],
            [null, new Name(checksum("CreatedAtStart"))],
        ]);
    };
    const converted = [
        node("Z_SM_TRG_Veh_FerrisCar01_StaticRail01", "RailNode"),
        node("Z_SM_TRG_Veh_FerrisCar01_StaticRail02", "RailNode"),
        node("Z_SM_TRG_Veh_FerrisCar01", "Vehicle"), // (not a rail: kept)
        node("Z_SM_TRG_Rail_Other", "RailNode"),
    ];
    const [script, text] = netOnlyRails("z_sm", converted, names, []);
    assert.deepEqual(
        converted.slice(0, 4).map((n) => n.hasFlag("CreatedAtStart")),
        [false, false, true, true],
    );
    assert.equal(converted.length, 5);
    const ghost = converted[4]!;
    assert.equal((ghost.get("Name") as Name).value, checksum("Z_SM_TRG_G_FerrisNetRails"));
    assert.equal(script, checksum("Z_SM_TRG_G_FerrisNetRailsScript"));
    assert.equal((ghost.get("TriggerScript") as Name).value, script);
    assert.ok(ghost.hasFlag("CreatedAtStart"));
    assert.equal(
        text,
        "script Z_SM_TRG_G_FerrisNetRailsScript {\n    if InNetGame {\n" +
            "        create name=Z_SM_TRG_Veh_FerrisCar01_StaticRail01\n" +
            "        create name=Z_SM_TRG_Veh_FerrisCar01_StaticRail02\n    }\n}\n",
    );
    // another area: nothing
    assert.deepEqual(netOnlyRails("z_ch", converted, names, []), [0, ""]);
});
