// NOT ORIGINAL - unit tests of tools/menu_level's and tools/anteara's pure functions (no game data): the values are
// what their Python twins gave (the parity cases, tools/parity/menu_level and tools/parity/anteara, compare them on
// the game's files).

import * as assert from "node:assert/strict";
import { test } from "node:test";
import { keyOf, norm } from "../anteara/anteara.ts";
import * as compat from "../compat/compat.ts";
import type { Manifest } from "../level_port/ported_levels.ts";
import { unswizzleOrder } from "../level_port/tex.ts";
import { nsString } from "./career_complete.ts";
import {
    type Level,
    mapPosition,
    signEntry,
    THAW_LEVELS,
    THP8_LEVELS,
    THPS3_LEVELS,
    THPS4_LEVELS,
    THUG1_LEVELS,
    THUG2_EXTRA,
    thawPositionScaleFlags,
} from "./level_select.ts";
import { coverage, PICTOGRAMS, readImg, strayInk, writeImg } from "./level_select_icons.ts";
import { parseArguments, skipStatement } from "./qb_walk.ts";

test("mapPosition puts places where THUG2's stars are", () => {
    assert.deepEqual(mapPosition(-118.24, 34.05), [77.8, 101.8]);
    assert.deepEqual(mapPosition(-0.128, 51.507), [223.0, 69.3]);
    assert.deepEqual(mapPosition(144.96, -37.8), [426.0, 194.0]);
});

const compatNone = compat.load("none");

test("THUGPRO's big level (TP-R6) is flagged in its level select entry", () => {
    const manifest = {
        levels: [
            { folder: "Z_MA", level: "z_ma", display: "THAW The Mall", name: "z_ma", level_num: 1 },
            { folder: "Z_CH", level: "skatepark", display: "THAW Chicago", name: "skatepark", level_num: 2 },
        ],
    } as unknown as Manifest;
    const level = (load: string): Level => ({
        text: load,
        levelNum: 1,
        load,
        icon: "cart",
        sign: "sign_x",
        place: "",
        location: null,
    });
    const flagged = thawPositionScaleFlags(manifest, [level("Load_z_ma"), level("Load_skatepark")]);
    assert.deepEqual(flagged[0]!.flags, ["IsBigLevel"]);
    assert.equal(flagged[1]!.flags, undefined);
    assert.match(signEntry(flagged[0]!), / IsBigLevel\} \n$/);
    assert.equal(thawPositionScaleFlags(manifest, [level("Load_z_ma")], compatNone)[0]!.flags, undefined);
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

test("every level we add has a pictogram of its own, and every pictogram stays inside the sign's diamond", () => {
    const icons = [
        ...[...THUG2_EXTRA.values()].map((e) => e.icon),
        ...[THUG1_LEVELS, THPS4_LEVELS, THPS3_LEVELS, THAW_LEVELS, THP8_LEVELS].flatMap((table) =>
            [...table.values()].map((e) => e.icon!),
        ),
    ];
    for (const icon of icons) assert.ok(PICTOGRAMS.has(icon), `${icon}: no such pictogram`);
    const seen = new Set<string>();
    for (const icon of icons) {
        assert.ok(!seen.has(icon), `${icon}: on two levels' signs`);
        seen.add(icon);
    }
    for (const [name, draw] of PICTOGRAMS) {
        const layers = draw();
        assert.deepEqual(strayInk(layers).length, 0, `${name}: ink outside the diamond's inside`);
        const ink = coverage(layers).reduce((a, b) => a + b, 0);
        assert.ok(ink > 120, `${name}: hardly any ink (${ink})`);
    }
});

test("a sign written upright is stored the way the game draws THUG2's own: bottom-up in unswizzleOrder", () => {
    // (found in the game: the level select showed our signs upside down while THUG2's own were upright)
    const w = 8;
    const h = 8;
    const palette: [number, number, number, number][] = [[0, 0, 0, 255]];
    const indices = new Array<number>(w * h).fill(0);
    indices[1 * w + 2] = 1; // a mark near the top left (x 2, y 1)
    indices[6 * w + 5] = 2; // another near the bottom right
    const data = writeImg(w, h, palette, indices);
    const stored = data.subarray(data.length - w * h);
    const order = unswizzleOrder(w, h);
    // the game's read order: row 0 of unswizzleOrder is the BOTTOM row of the picture
    assert.equal(stored[order[(h - 1 - 1) * w + 2]!], 1);
    assert.equal(stored[order[(h - 1 - 6) * w + 5]!], 2);
    assert.notEqual(stored[order[1 * w + 2]!], 1);
    // and reading gives the upright picture back
    assert.deepEqual(readImg(data).indices, indices);
});
