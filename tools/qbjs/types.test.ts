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

test("RandomRange's kind: an integer for whole endpoints, else a float (random.js range)", async () => {
    const { rangeKind } = await import("./typecheck.ts");
    const bits = (f: number) => new Uint32Array(new Float32Array([f]).buffer)[0]!;
    assert.equal(rangeKind({ a: bits(0), b: bits(4) }), "int");
    assert.equal(rangeKind({ a: bits(-3), b: bits(2.000001) }), "int");
    assert.equal(rangeKind({ a: bits(0), b: bits(0.5) }), "float");
});

test("the script check's run ends read back for the type proof's test", async () => {
    const { traceKind, traceMembers } = await import("./check_types_against_runs.ts");
    const members = traceMembers(
        '{0c39eca4=0 ab377503=[#4faa6460] 3f6bcdba={00000000=128} 7f261953=(41a00000,41200000) a="x y" b=L"z" c=f3f800000 d=(1,2,3)}',
    );
    assert.deepEqual(
        members.map(([k, v]) => [k, traceKind(v)]),
        [
            ["0c39eca4", "int"],
            ["ab377503", "array"],
            ["3f6bcdba", "struct"],
            ["7f261953", "pair"],
            ["a", "string"],
            ["b", "localstring"],
            ["c", "float"],
            ["d", "vector"],
        ],
    );
});
