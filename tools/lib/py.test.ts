// NOT ORIGINAL - tests of tools/lib's Python-compatibility helpers against values Python 3.14 gave (no game data).

import assert from "node:assert/strict";
import { test } from "node:test";
import { getCloseMatches, unifiedDiff } from "./difflib.ts";
import {
    decodeAsciiReplace,
    encodeLatin1,
    Float,
    floorDiv,
    formatFixed,
    intFromBytesLE,
    pyDist,
    pyFloatRepr,
    pyIntHex,
    pyJoin,
    pyJsonDumps,
    pyJsonLoads,
    pyMax,
    pyMod,
    pyParseFloat,
    pyRepr,
    pyRound,
    pySplitlines,
    pyStrRepr,
    pySum,
    pyTuple,
    roundHalfEven,
} from "./py.ts";
import { search, sub } from "./re.ts";
import { f32, pack, StructError, unpackFrom } from "./struct.ts";

test("roundHalfEven is Python's round(): halves to even", () => {
    assert.deepEqual([0.5, 1.5, 2.5, -0.5, -1.5, 2.4999999999999996, 3.5].map(roundHalfEven), [0, 2, 2, 0, -2, 2, 4]);
    assert.equal(Object.is(roundHalfEven(-0.4), 0), true); // (no -0)
    assert.throws(() => roundHalfEven(Number.NaN));
});

test("pyRound(x, n) rounds the exact binary value", () => {
    assert.equal(pyRound(2.675, 2), 2.67);
    assert.equal(pyRound(0.125, 2), 0.12);
    assert.equal(pyRound(0.375, 2), 0.38);
    assert.equal(pyRound(1.00005, 4), 1.0001); // (1.00005 is 1.0000500000000001 in binary)
});

test("formatFixed is Python's %.Nf", () => {
    assert.equal(formatFixed(0.0009765625, 9), "0.000976562"); // (toFixed: ...563)
    assert.equal(formatFixed(-0.0001, 3), "-0.000");
    assert.equal(formatFixed(1e21, 2), "1000000000000000000000.00"); // (toFixed: "1e+21")
    assert.equal(formatFixed(Number.POSITIVE_INFINITY, 9), "inf");
    assert.equal(formatFixed(2.5, 0), "2");
});

test("pyFloatRepr is Python's repr(float)", () => {
    const cases: [number, string][] = [
        [1e16, "1e+16"],
        [1e15, "1000000000000000.0"],
        [1e-5, "1e-05"],
        [1e-4, "0.0001"],
        [123.0, "123.0"],
        [-0, "-0.0"],
        [0.1, "0.1"],
        [Math.fround(0.1), "0.10000000149011612"],
        [1.5e-7, "1.5e-07"],
        [5e-324, "5e-324"],
        [Number.NaN, "nan"],
        [Number.NEGATIVE_INFINITY, "-inf"],
    ];
    for (const [x, text] of cases) assert.equal(pyFloatRepr(x), text);
});

test("pySum is Python 3.12+'s compensated sum()", () => {
    assert.equal(pySum(Array(10).fill(0.1)), 1.0);
    assert.equal(
        Array(10)
            .fill(0.1)
            .reduce((a, b) => a + b, 0),
        0.9999999999999999,
    );
    assert.equal(pySum([]), 0);
});

test("pyDist is math.dist", () => {
    assert.equal(pyDist([0, 0, 0], [1, 1, 1]), 1.7320508075688772);
    assert.equal(pyDist([1e-300, 2e-300], [0, 0]), 2.2360679774997897e-300);
});

test("floor division and modulo take the divisor's sign", () => {
    assert.equal(floorDiv(-7, 2), -4);
    assert.equal(pyMod(-7, 2), 1);
    assert.equal(pyMod(7, -2), -1);
});

test("max / min: Python's NaN and empty behaviour", () => {
    assert.equal(pyMax([1, Number.NaN, 3]), 3);
    assert.throws(() => pyMax([]));
});

test("strings: splitlines, repr, hex ints, floats, latin-1, ascii", () => {
    assert.deepEqual(pySplitlines("a\r\nb\rc\x0bd\n"), ["a", "b", "c", "d"]);
    assert.equal(pyStrRepr("it's"), `"it's"`);
    assert.equal(pyStrRepr("a\nb\x00"), "'a\\nb\\x00'");
    assert.equal(pyIntHex("0x1f"), 31);
    assert.throws(() => pyIntHex("1g"));
    assert.equal(pyParseFloat(" 1_000.5 "), 1000.5);
    assert.throws(() => pyParseFloat(""));
    assert.throws(() => encodeLatin1("\u0100"));
    assert.equal(decodeAsciiReplace(Buffer.from([0x41, 0xe9])), "A\ufffd");
    assert.equal(intFromBytesLE(Buffer.from([0xff, 0xff, 0xff, 0xff]), true), -1);
});

test("pyRepr prints dicts, lists and tuples as Python", () => {
    assert.equal(
        pyRepr(
            new Map<string, unknown>([
                ["same", 0],
                ["x", [1, 2.5, "a"]],
            ]),
        ),
        "{'same': 0, 'x': [1, 2.5, 'a']}",
    );
    assert.equal(pyRepr(pyTuple([1, "b"])), "(1, 'b')");
    assert.equal(pyRepr(pyTuple([1])), "(1,)");
});

test("pyJoin keeps the parts' own slashes (ntpath.join)", () => {
    assert.equal(pyJoin("C:/x", "levels", "a.qb"), "C:/x\\levels\\a.qb");
    assert.equal(pyJoin("C:\\x\\", "y"), "C:\\x\\y");
    assert.equal(pyJoin("C:\\x", "D:\\y"), "D:\\y");
});

test("json: Python's dumps text and loads' floats", () => {
    const text = pyJsonDumps(
        new Map<string, unknown>([
            ["a", [1, new Float(2.0), "é"]],
            ["b", {}],
            ["c", []],
        ]),
        1,
    );
    assert.equal(text, '{\n "a": [\n  1,\n  2.0,\n  "\\u00e9"\n ],\n "b": {},\n "c": []\n}');
    const back = pyJsonLoads('{"a": [1, 2.0, NaN, "NaN"]}') as { a: unknown[] };
    assert.equal(back.a[0], 1);
    assert.ok(back.a[1] instanceof Float);
    assert.ok(back.a[2] instanceof Float && Number.isNaN(back.a[2].value));
    assert.equal(back.a[3], "NaN");
});

test("struct: Python's errors where DataView would wrap", () => {
    assert.throws(() => pack("<H", 70000), StructError);
    assert.throws(() => pack("<I", -1), StructError);
    assert.throws(() => pack("<i", 1.5), StructError);
    assert.throws(() => pack("<f", 1e39), StructError);
    assert.throws(() => f32(1e39), StructError);
    assert.deepEqual(unpackFrom("<8sI", Buffer.from("abc\0\0\0\0\0\x01\0\0\0", "latin1")), [
        Buffer.from("abc\0\0\0\0\0", "latin1"),
        1,
    ]);
    assert.equal(pack(">I", 1).toString("hex"), "00000001");
});

test("difflib: get_close_matches and unified_diff as Python's", () => {
    assert.deepEqual(getCloseMatches("appel", ["ape", "apple", "peach", "puppy"]), ["apple", "ape"]);
    assert.deepEqual(getCloseMatches("TERRAIN_MEATALSMOOTH", ["TERRAIN_METALSMOOTH", "TERRAIN_METALROUGH"], 1, 0.8), [
        "TERRAIN_METALSMOOTH",
    ]);
    assert.deepEqual(
        [...unifiedDiff("a b c d e f g".split(" "), "a b x d e f g h".split(" "), "ref", "new", "", "", 1, "")],
        ["--- ref", "+++ new", "@@ -2,3 +2,3 @@", " b", "-c", "+x", " d", "@@ -7 +7,2 @@", " g", "+h"],
    );
});

test("re: match offsets and literal replacements", () => {
    const m = search(/b(c+)/, "abccd");
    assert.equal(m?.start(1), 2);
    assert.equal(m?.end(1), 4);
    assert.equal(sub(/c/, "$&", "acc", 1), "a$&c"); // (a string replacement is literal)
});
