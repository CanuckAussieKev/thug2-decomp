// NOT ORIGINAL - tests of the THAW converter's pure parts and the Python helpers it brought into tools/lib (formatG,
// pySplit, pyTitle, pySplitlines' keepends), against values Python 3.14 gave (no game data).

import assert from "node:assert/strict";
import { test } from "node:test";
import { checksum, Name, Struct, Vector } from "../level_port/qb.ts";
import { formatG, pySplit, pySplitlines, pyTitle } from "../lib/py.ts";
import { pack } from "../lib/struct.ts";
import { migrateText, quarterTurn } from "./migrate_layouts.ts";
import { inlineStruct, statements, tokens } from "./thaw.ts";
import { applySpawns, COMPAT, levelName, moveTree, setCompat, sixteenths } from "./thaw_levels.ts";

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

test("applySpawns (TP-4): changes a named restart, adds one with the drop-in, nothing without the profile", () => {
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
