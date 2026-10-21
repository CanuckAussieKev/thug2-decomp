// NOT ORIGINAL - unit tests of THPS3's doors / gates in free skate: PC-THPS3-16 (what a start script makes there),
// TP-41 (the scripts THUGPRO rewrote for its free skate: editScripts) and start_state.ts (the comparison with
// THUGPRO). No game data needed.

import assert from "node:assert/strict";
import { test } from "node:test";
import * as compat from "../compat/compat.ts";
import * as qb from "../level_port/qb.ts";
import * as scriptPort from "../level_port/script_port.ts";
import * as startState from "./start_state.ts";
import {
    createdAtStart,
    editScripts,
    freeSkateEdits,
    levelCompat,
    startCondition,
    thps3PortChanges,
} from "./thps3_levels.ts";

const IF = 0x25;
const ELSE = 0x26;
const ENDIF = 0x28;
const EQ = 0x07;
const NOT = 0x39;

const u32 = (v: number): number[] => [v & 0xff, (v >>> 8) & 0xff, (v >>> 16) & 0xff, (v >>> 24) & 0xff];

// A THPS3 script as tokens: each line a list of names (strings) and token bytes (numbers), ended by 0x01.
function scriptBytes(name: string, lines: (string | number)[][]): Buffer {
    const out: number[] = [0x23, 0x16, ...u32(qb.checksum(name)), 0x01];
    for (const line of lines) {
        for (const t of line) out.push(...(typeof t === "number" ? [t] : [0x16, ...u32(qb.checksum(t))]));
        out.push(0x01);
    }
    out.push(0x24, 0x00);
    return Buffer.from(out);
}

const create = (x: string): (string | number)[] => ["Create", "Name", EQ, x];
const names = (list: string[]): Set<number> => new Set(list.map(qb.checksum));

test("PC-THPS3-16: a start script's career / goal / network branches make nothing at the start of free skate", () => {
    const q = new qb.QbFile(
        scriptBytes("Load_t", [
            [IF, "IsCareerMode"],
            create("CareerOnly"),
            [ENDIF],
            [IF, "InMultiPlayerGame"],
            create("NetOnly"),
            [ELSE],
            create("FreeOnly"),
            [ENDIF],
            [IF, NOT, "GetGoal", "Goal", EQ, "GOAL_SCRIPTED2"], // (not <goal>: true in free skate)
            create("NotGoal"),
            [ENDIF],
            [IF, "GetFlag", "Flag", EQ, "SomeFlag"], // (a flag: not decided - both branches)
            create("FlagTrue"),
            [ELSE],
            create("FlagFalse"),
            [ENDIF],
            [IF, "IsCareerMode"], // (nested in a branch that doesn't run: still nothing)
            [IF, "SomethingElse"],
            create("Nested"),
            [ENDIF],
            [ENDIF],
        ]),
        "test",
    );
    const [now] = createdAtStart(q, [], new Map(), "t", [], thps3PortChanges(true));
    assert.deepEqual(now, names(["FreeOnly", "NotGoal", "FlagTrue", "FlagFalse"]));
    // (--no-port-changes: both branches of every `if`, as before)
    const [before] = createdAtStart(q, [], new Map(), "t", [], thps3PortChanges(false));
    assert.deepEqual(
        before,
        names(["CareerOnly", "NetOnly", "FreeOnly", "NotGoal", "FlagTrue", "FlagFalse", "Nested"]),
    );
});

test("startCondition: only career / goal / network / split-screen commands are decided", () => {
    const tok = (name: string): [number, Buffer] => [0x16, Buffer.from(u32(qb.checksum(name)))];
    const t = (token: number): [number, Buffer] => [token, Buffer.alloc(0)];
    const c = qb.checksum;
    assert.equal(startCondition([tok("IsCareerMode")], c), false);
    assert.equal(startCondition([t(NOT), tok("InNetGame")], c), true);
    assert.equal(startCondition([tok("GetFlag"), tok("Flag"), t(EQ), tok("X")], c), null);
    assert.equal(startCondition([tok("IsCareerMode"), t(0x33), tok("InNetGame")], c), null);
    assert.equal(startCondition([], c), null);
});

test("TP-41: the scripts THUGPRO rewrote for its free skate (Foundry, Subway, Skater Island, Shipyard)", () => {
    const thugpro = compat.load("thugpro");
    const foun = levelCompat(thugpro, "foun").freeSkate?.scripts ?? {};
    assert.deepEqual(foun.CJR_Foun_Valve_Check?.conditions, { GetFlag: true });
    for (const v of ["A", "B", "C", "D", "E"])
        assert.deepEqual(foun[`CJR_Foun_Valve_${v}_Booth`]?.conditions, { IsCareerMode: true, GetGoal: true });
    const sub = levelCompat(thugpro, "sub").freeSkate?.scripts ?? {};
    assert.deepEqual(sub.ALF_SUB_Status, {
        conditions: { InMultiPlayerGame: false, GetGoal: true },
        drop: ["Kill Name=TRG_Ped_ThinMan"],
    });
    // (THUGPRO's offline free skate keeps Skater Island's outer door; its cannons' trigger isn't made at the start)
    const si = levelCompat(thugpro, "si").freeSkate?.scripts ?? {};
    assert.deepEqual(si.ALF_SI_Init, { conditions: {}, drop: ["Create Name=TRGP_FireCannons"] });
    const shp = levelCompat(thugpro, "shp").freeSkate?.scripts ?? {};
    assert.deepEqual(shp.BDJ_SHP_LoadingScript?.conditions, { IsCareerMode: true });
    for (const level of ["rio", "tok", "burn", "ros", "ware", "can", "ap", "la"])
        assert.equal(levelCompat(thugpro, level).freeSkate, undefined, level);
    assert.equal(levelCompat(compat.load("none"), "foun").freeSkate, undefined);
    assert.equal(freeSkateEdits(compat.load("none")).size, 0);
    assert.equal(freeSkateEdits(thugpro).size, 9);
});

test("editScripts: decided conditions take their branch, dropped statements go - in that script only", () => {
    const lines: (string | number)[][] = [
        [IF, "GetGoal", "Goal", EQ, "GOAL_SCRIPTED1"],
        ["Kill", "Name", EQ, "TRG_Ped_Kept"],
        ["Kill", "Name", EQ, "Boarded"],
        create("Unboarded"),
        [ENDIF],
        [IF, NOT, "IsCareerMode"],
        create("NotCareer"),
        [ELSE],
        create("Career"),
        [ENDIF],
    ];
    // (the first without its end-of-file 0)
    const data = Buffer.concat([scriptBytes("Edited", lines).subarray(0, -1), scriptBytes("Other", lines)]);
    const edits = new Map([
        [
            qb.checksum("Edited"),
            { conditions: { GetGoal: true, IsCareerMode: true }, drop: ["Kill Name=TRG_Ped_Kept"] },
        ],
    ]);
    const edited = editScripts(data, edits);
    assert.equal(edited.length, data.length);
    const q = new qb.QbFile(edited, "test");
    const [edit] = createdAtStart(q, [], new Map(), "t", [qb.checksum("Edited")], thps3PortChanges(true));
    assert.deepEqual(edit, names(["Unboarded", "Career"]));
    const [other] = createdAtStart(q, [], new Map(), "t", [qb.checksum("Other")], thps3PortChanges(true));
    assert.deepEqual(other, names(["NotCareer"]));
    // (the dropped Kill: its command is script_port's DROPPED marker, the other Kill kept)
    const commands = q.scripts
        .get(qb.checksum("Edited"))!
        .map((l) => (l[0]![0] === 0x16 ? l[0]![1].readUInt32LE(0) : 0));
    assert.equal(commands[1], qb.checksum(scriptPort.DROPPED));
    assert.equal(commands[2], qb.checksum("Kill"));
    assert.equal(editScripts(data, new Map()), data);
    assert.throws(() => editScripts(data, new Map([[qb.checksum("Edited"), { conditions: {}, drop: ["Kill"] }]])));
});

test("start_state: a level's free-skate start, both sides' scripts followed", () => {
    const node = (name: string, cls: string, flags: string[] = []): qb.Struct =>
        new qb.Struct([
            [new qb.Name(qb.checksum("Name")), new qb.Name(qb.checksum(name))],
            [new qb.Name(qb.checksum("Class")), new qb.Name(qb.checksum(cls))],
            ...flags.map((f): qb.Entry => [null, new qb.Name(qb.checksum(f))]),
        ]);
    const tok = (name: string): [number, Buffer] => [0x16, Buffer.from(u32(qb.checksum(name)))];
    const t = (token: number): [number, Buffer] => [token, Buffer.alloc(0)];
    const str = (s: string): [number, Buffer] => [0x1b, Buffer.from(`${s}\0`, "latin1")];
    const kill = (n: string): qb.Statement => [tok("Kill"), tok("Name"), t(EQ), tok(n)];
    const level: startState.LevelScripts = {
        nodes: [
            node("Door_Closed", "LevelGeometry", ["CreatedAtStart"]),
            node("Door_Open", "LevelGeometry"),
            node("Ped", "Pedestrian", ["CreatedAtStart"]),
            node("Rail_A", "RailNode", ["CreatedAtStart"]),
        ],
        scripts: new Map([
            [
                qb.checksum("Setup"),
                [
                    [t(IF), tok("InNetGame")],
                    kill("Ped"),
                    [t(ELSE)],
                    [tok("Create"), tok("prefix"), t(EQ), str("door_open")],
                    [t(ENDIF)],
                    [tok("Sub")],
                    [t(IF), tok("Unknown")],
                    kill("Rail_A"),
                    [t(ENDIF)],
                ],
            ],
            [qb.checksum("Sub"), [kill("Door_Closed"), [tok("Die")], kill("Ped")]],
        ]),
        names: new Map(
            ["Door_Closed", "Door_Open", "Ped", "Rail_A", "LevelGeometry", "Pedestrian", "RailNode"].map((n) => [
                qb.checksum(n),
                n,
            ]),
        ),
    };
    const s = startState.startState(level, [qb.checksum("Setup")]);
    const at = (n: string): string => {
        const x = s.get(qb.checksum(n))!;
        return `${x.state}${x.sure ? "" : "?"}`;
    };
    assert.equal(at("Door_Closed"), "off");
    assert.equal(at("Door_Open"), "on");
    assert.equal(at("Ped"), "on");
    assert.equal(at("Rail_A"), "off?");
    const other = startState.startState(level, []);
    const diffs = startState.startDiffs(s, other, level.names);
    assert.deepEqual(
        diffs.map((d) => [d.name, d.world]),
        [
            ["Door_Closed", true],
            ["Door_Open", true],
            ["Rail_A", true],
        ],
    );
    // (a geometry node neither side draws: not a difference)
    assert.equal(startState.startDiffs(s, other, level.names, new Set([qb.checksum("Door_Open")])).length, 2);
});
