// NOT ORIGINAL - tools/qbjs/types.ts: the .qb files' argument types and the game's recording merged into typings.
import assert from "node:assert/strict";
import { test } from "node:test";
import { checksum } from "../qb_checksum.ts";
import { command, field, floating, integer, name, script } from "./fixtures.ts";
import { readIR } from "./reader.ts";
import { staticTypes, typesReport } from "./types.ts";

const hex = (n: number) => n.toString(16).padStart(8, "0");
const module = () =>
    readIR(
        Uint8Array.from([
            ...script("Main", [
                ...command("Wait", [...integer(1), ...name("gameframe")]),
                ...command("Helper", field("speed", floating(0x3f800000))),
                ...command("Helper", field("speed", integer(2))),
            ]),
            0,
        ]),
        "types.qb",
    );

test("the .qb files' argument types, by command and argument", () => {
    const { counts, scripts } = staticTypes([module()]);
    assert.ok(scripts.has(checksum("Main")));
    assert.deepEqual(Object.fromEntries(counts.get(`arg ${hex(checksum("Wait"))} #0`)!), { int: 1 });
    assert.deepEqual(Object.fromEntries(counts.get(`arg ${hex(checksum("Wait"))} #1`)!), { name: 1 });
    assert.deepEqual(Object.fromEntries(counts.get(`arg ${hex(checksum("Helper"))} ${hex(checksum("speed"))}`)!), {
        float: 1,
        int: 1,
    });
});

test("typings and the report: the recording's types added, waits counted", () => {
    const recorded = new Map([
        [`wait ${hex(checksum("Wait"))}`, new Map([["waited", 3]])],
        [`wait ${hex(checksum("Helper"))}`, new Map([["same frame", 2]])],
        [
            `param types.qb ${hex(checksum("Main"))} ${hex(checksum("speed"))}`,
            new Map([
                ["int", 4],
                ["float", 1],
            ]),
        ],
    ]);
    const result = typesReport([module()], recorded);
    assert.match(
        result.dts,
        /\/\*\* C function\. Seen run 3 times: waited 3\. \*\/\nexport declare function Wait\(\.\.\.unnamed: \(Int \| Name\)\[\]\): Command;/,
    );
    assert.match(result.dts, /function _0x[0-9a-f]{8}\(args\?: \{ _0x[0-9a-f]{8}\?: Float \| Int \}\): Command;/);
    assert.equal(result.seenRunning, 2);
    assert.equal(result.neverWaited, 1);
    assert.match(result.report, /\| parameter reads \(p\.x as a value\) \| 1 \| 0 \| - \|/);
    assert.match(result.report, /- `param types\.qb _0x[0-9a-f]{8} _0x[0-9a-f]{8}`: int 4, float 1/);
});
