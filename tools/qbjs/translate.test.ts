// NOT ORIGINAL - synthetic format and generator regression tests.
import assert from "node:assert/strict";
import { test } from "node:test";
import { command, conditional, field, fixture, floating, integer, name, script } from "./fixtures.ts";
import { checksum, parse, translate } from "./translate.ts";

test("preserves typed bits, checksum keys, script offsets and deterministic output", () => {
    const input = fixture(),
        ir = parse(input, "test.qb");
    assert.equal(ir.scripts.length, 16);
    assert.equal(ir.scripts[0]?.name, checksum("main"));
    assert.equal(ir.scripts[0]?.offset, 0);
    assert.deepEqual(ir.scripts[9]?.defaults, [
        { name: checksum("x"), value: { kind: "literal", value: { type: 2, bits: 0x80000000 } } },
    ]);
    assert.equal(translate(input, "test.qb"), translate(input, "test.qb"));
    assert.match(translate(input), /function\*/);
    assert.match(translate(input), /yield\* qb.call/);
    assert.equal(checksum("Index1"), 0x0ba453b9);
    assert.equal(checksum("A/B"), checksum("a\\b"));
});
test("truncations always fail with source offsets; EOF is required", () => {
    const b = Uint8Array.from([...script("a", command("b", field("n", floating(0x3f800000)))), 0]);
    for (let i = 0; i < b.length; ++i) assert.throws(() => parse(b.subarray(0, i), "cut.qb"), /cut.qb: QB offset/);
    assert.throws(() => parse(Uint8Array.from([...b, 0])), /trailing bytes/);
});
test("rejects unsupported instructions, cached pointers, duplicate definitions and dynamic defaults", () => {
    for (const body of [
        [0x20, 1],
        [0x43, 0, 0, 0, 0, 1],
        command("x", [0x1b, 1, 0, 0, 0, 0]),
        [...name("x"), 0x42, ...name("y"), 1],
    ])
        assert.throws(() => parse(Uint8Array.from([...script("a", body), 0])), /QB offset/);
    assert.throws(() => parse(Uint8Array.from([...script("a", []), ...script("a", []), 0])), /duplicate/);
    assert.throws(() => parse(Uint8Array.from([...script("a", [], [0x2c]), 0])), /defaults/);
});
test("validates compiled if targets instead of discarding jump metadata", () => {
    const b = conditional(name("check"), command("yes"), command("no"));
    assert.equal(parse(Uint8Array.from([...script("a", b), 0])).scripts[0]?.body[0]?.kind, "if");
    b[1] = 1;
    assert.throws(() => parse(Uint8Array.from([...script("a", b), 0])), /if jump/);
    assert.throws(() => parse(Uint8Array.from([...script("a", [], field("x", [0x2d, ...name("x")])), 0])), /defaults/);
});
test("accepts numbered EOL tokens and unsigned integer literals", () => {
    const b = [0x23, ...name("a"), 2, 7, 0, 0, 0, ...field("n", integer(-1)), 2, 8, 0, 0, 0, 0x24, 0];
    const ir = parse(Uint8Array.from(b));
    assert.equal(ir.scripts[0]?.body[0]?.kind, "assign");
});
