// NOT ORIGINAL - unit tests of THPS3's doors / gates in free skate: PC-THPS3-16 (what a start script makes there) and
// TP-41 (Foundry's generator doors). No game data needed.

import assert from "node:assert/strict";
import { test } from "node:test";
import * as compat from "../compat/compat.ts";
import * as qb from "../level_port/qb.ts";
import { createdAtStart, levelCompat, startCondition, thps3PortChanges } from "./thps3_levels.ts";

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

test("TP-41: Foundry's generator doors and Skater Island's outer door are open in free skate", () => {
    const thugpro = compat.load("thugpro");
    assert.deepEqual(levelCompat(thugpro, "foun").freeSkate, { flags: ["CJR_FLAG_GEN_OPEN"], kill: [] });
    assert.deepEqual(levelCompat(thugpro, "si").freeSkate?.kill, ["SI_OuterDoor", "SI_Sponsors06", "si_lighting02"]);
    assert.equal(levelCompat(thugpro, "rio").freeSkate, undefined);
    assert.equal(levelCompat(compat.load("none"), "foun").freeSkate, undefined);
});
