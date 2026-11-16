// NOT ORIGINAL - the scripts' outlines (outline.ts): the bytes after each script's first line, through its endscript.
import assert from "node:assert/strict";
import { test } from "node:test";
import { command, field, fixture, integer, name, script } from "./fixtures.ts";
import { outlineAt, outlineManifest, scriptOutlines, skipToEndOfStatement, stepOverToken } from "./outline.ts";
import { readIR } from "./reader.ts";
import { checksum } from "./translate.ts";

test("each script's outline is its statements after its first line, through its endscript", () => {
    const body = [...command("Echo", field("a", integer(1))), ...command("child")];
    const bytes = Uint8Array.from([
        ...script("main", body, [...field("x", integer(11))]),
        ...script("child", command("Wait", integer(1))),
        0,
    ]);
    const outlines = scriptOutlines(bytes, readIR(bytes, "outline.qb"));
    assert.deepEqual(
        outlines.map(([n]) => n),
        [checksum("main"), checksum("child")],
    );
    assert.deepEqual([...Buffer.from(outlines[0]![1], "base64")], [...body, 0x24]);
    assert.deepEqual([...Buffer.from(outlines[1]![1], "base64")], [...command("Wait", integer(1)), 0x24]);
});

test("the outlines of the synthetic module: every script's, its endscript last", () => {
    const bytes = fixture();
    const ir = readIR(bytes, "synthetic.qb");
    const outlines = scriptOutlines(bytes, ir);
    assert.equal(outlines.length, ir.definitions.filter((d) => d.kind === "script").length);
    for (const [, code] of outlines) assert.equal(Buffer.from(code, "base64").at(-1), 0x24);
});

test("the token walk is the game's: operands, strings, Random tables, line numbers and nesting", () => {
    // a string: its length and text; a Random of 2: 4 + 2 * 6 bytes after it; a numbered line end: 5 bytes
    const text = [0x1b, 3, 0, 0, 0, 0x61, 0x62, 0];
    assert.equal(stepOverToken(Uint8Array.from(text), 0), 8);
    assert.equal(stepOverToken(Uint8Array.from([0x2f, 2, 0, 0, 0, ...new Array(12).fill(0)]), 0), 17);
    assert.equal(stepOverToken(Uint8Array.from([0x02, 1, 0, 0, 0]), 0), 5);
    assert.equal(stepOverToken(Uint8Array.from([0x30, 0, 0]), 0), 1); // (RandomRange: one byte, as the original steps)
    assert.equal(stepOverToken(Uint8Array.from([0x10]), 0), 0); // (no size: not stepped over)
    // a line end inside a structure doesn't end the statement
    const nested = Uint8Array.from([...name("a"), 0x03, 0x01, ...field("b", integer(2)), 0x04, 0x01, 0x24]);
    assert.equal(skipToEndOfStatement(nested, 0), nested.length - 1);
    assert.deepEqual([...outlineAt(nested, 0)], [0x24]);
});

test("the manifest's form: a list per name, in definition order", () => {
    assert.deepEqual(
        outlineManifest([
            [0x12, "AQ=="],
            [0xabcdef01, "JA=="],
            [0x12, "Ag=="],
        ]),
        { "00000012": ["AQ==", "Ag=="], abcdef01: ["JA=="] },
    );
});
