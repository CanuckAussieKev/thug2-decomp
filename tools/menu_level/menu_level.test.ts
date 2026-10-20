// NOT ORIGINAL - unit tests of tools/menu_level's and tools/anteara's pure functions (no game data): the values are
// what their Python twins gave (the parity cases, tools/parity/menu_level and tools/parity/anteara, compare them on
// the game's files).

import * as assert from "node:assert/strict";
import { test } from "node:test";
import { keyOf, norm } from "../anteara/anteara.ts";
import { nsString } from "./career_complete.ts";
import { mapPosition } from "./level_select.ts";
import { parseArguments, skipStatement } from "./qb_walk.ts";

test("mapPosition puts places where THUG2's stars are", () => {
    assert.deepEqual(mapPosition(-118.24, 34.05), [77.8, 101.8]);
    assert.deepEqual(mapPosition(-0.128, 51.507), [223.0, 69.3]);
    assert.deepEqual(mapPosition(144.96, -37.8), [426.0, 194.0]);
});

test("qb_walk reads a call's arguments to the end of the line", () => {
    // name=5 then an end of line
    const d = Buffer.from([0x16, 1, 0, 0, 0, 0x07, 0x17, 5, 0, 0, 0, 0x01]);
    const [params, end] = parseArguments(d, 0, null);
    assert.equal(end, 11);
    assert.deepEqual([...params], [[1, 5]]);
    assert.equal(skipStatement(d, 0), 12);
});

test("anteara's unit keys and normalised code", () => {
    assert.equal(keyOf("Load_TestLevel"), "#260db59e");
    assert.equal(keyOf("`Load_TestLevel`"), "#260db59e");
    assert.equal(keyOf("#ABCDEF01"), "#abcdef01");
    assert.equal(norm('foo = { // x\n  bar  "a  b" }'), '#de9a8c73 = { // #7ce92373 #55730089 "a b" }');
});

test("gap names pass only as plain ASCII", () => {
    assert.equal(nsString("a\\b"), "a\\\\b");
    assert.throws(() => nsString('say "hi"'));
    assert.throws(() => nsString("café"));
});
