// NOT ORIGINAL - tests of the converted levels' script checks (script_check.ts), on hand-made THUG2 tokens.

import assert from "node:assert/strict";
import { test } from "node:test";
import { pack } from "../lib/struct.ts";
import { checksum } from "./qb.ts";
import { arraysRead, checkLevel, endlessLoops, nodeLookups, scriptTokens, yieldingIdleLoops } from "./script_check.ts";

const name = (text: string): Buffer => Buffer.concat([Buffer.from([0x16]), pack("<I", checksum(text))]);
const names = (...words: string[]): Buffer =>
    Buffer.concat(
        words.map((w) => Buffer.concat([Buffer.from([0x2b]), pack("<I", checksum(w)), Buffer.from(`${w}\0`)])),
    );
const nl = Buffer.from([0x01]);
const b = (...bytes: number[]): Buffer => Buffer.from(bytes);

// script <name> { <body> }
const script = (scriptName: string, ...body: Buffer[]): Buffer =>
    Buffer.concat([b(0x23), name(scriptName), nl, ...body, b(0x24), nl]);
const loop = (...body: Buffer[]): Buffer => Buffer.concat([b(0x20), nl, ...body, b(0x21), nl]);
const WORDS = [
    "Idle",
    "Obj_WaitAnimFinished",
    "Obj_PlayAnim",
    "Spin",
    "wait",
    "Wait",
    "IsAlive",
    "name",
    "Gone",
    "Here",
];

test("loops: the idle Obj_WaitAnimFinished loop spins and is made a frame's wait; a loop that plays one is fine", () => {
    const data = Buffer.concat([
        script("Idle", loop(name("Obj_WaitAnimFinished"), nl)),
        script("Plays", loop(name("Obj_PlayAnim"), nl, name("Obj_WaitAnimFinished"), nl)),
        script("Spin", loop(name("Here"), nl)),
        names(...WORDS),
        b(0),
    ]);
    const kinds = checkLevel(data, "t").map((f) => `${f.kind} ${f.where}`);
    assert.deepEqual(kinds.sort(), ["never yields Spin", "spins on Obj_WaitAnimFinished Idle"]);
    const [fixed, n] = yieldingIdleLoops(data);
    assert.equal(n, 1);
    assert.equal(fixed.length, data.length);
    assert.deepEqual(
        checkLevel(fixed, "t").map((f) => f.kind),
        ["never yields"],
    );
    // (a repeat with a count isn't endless)
    const counted = scriptTokens(script("C", b(0x20), nl, name("Here"), nl, b(0x21, 0x17, 3, 0, 0, 0), nl));
    assert.deepEqual(endlessLoops(counted.get(checksum("C"))!), []);
});

test("nodeLookups / arraysRead: name= constants of node lookups, (<array> [<i>]) and GetArraySize", () => {
    const toks = scriptTokens(
        script(
            "S",
            b(0x47, 0, 0),
            name("IsAlive"),
            name("name"),
            b(0x07),
            name("Gone"),
            nl,
            name("GetArraySize"),
            name("Arr"),
            nl,
            name("TriggerType"),
            b(0x05, 0x17, 1, 0, 0, 0, 0x06),
            nl,
            b(0x0e),
            name("Arr2"),
            b(0x05),
            b(0x2d),
            name("index"),
            b(0x06, 0x0f),
            nl,
        ),
    ).get(checksum("S"))!;
    assert.deepEqual(nodeLookups(toks), [[checksum("IsAlive"), checksum("Gone")]]);
    assert.deepEqual(arraysRead(toks), [checksum("Arr"), checksum("Arr2")]);
});
