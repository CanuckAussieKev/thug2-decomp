// NOT ORIGINAL - the script check's report rules (script_check.ts) and the runtime's answer it relies on.
import assert from "node:assert/strict";
import { test } from "node:test";
import { runInNewContext } from "node:vm";
import type { Definition, Expr, Statement } from "./ir.ts";
import { nativeRuntimeBundle } from "./runtime-fixtures.ts";
import { fixedArmCode, statementPlaces, unreachable } from "./script_check.ts";

type Script = Extract<Definition, { kind: "script" }>;
const zero: Expr = { kind: "literal", offset: 0, value: { type: 1, bits: 0 } } as Expr;
const set = (offset: number): Statement => ({ kind: "assign", offset, name: 1, value: zero });
// (offset -6: a statement's place is its offset)
const script = (body: Statement[]): Script => ({ kind: "script", offset: -6, name: 1, defaults: [], body });
const places = (d: Script, xs: Statement[]) => statementPlaces({ ...d, body: xs });

test("unreachable: a break after a return doesn't end its loop - the code after the loop is dead", () => {
    const d = script([
        {
            kind: "loop",
            offset: 0,
            repeatOffset: 40,
            repeatFields: [],
            body: [
                {
                    kind: "if",
                    offset: 10,
                    condition: zero,
                    yes: [
                        { kind: "return", offset: 20, fields: [] },
                        { kind: "break", offset: 30, target: 0 },
                    ],
                    no: [],
                },
            ],
        },
        set(50),
    ]);
    assert.deepEqual(places(d, unreachable(d)).sort(), [30, 50]);
});

test("unreachable: a break that can run ends its loop", () => {
    const d = script([
        {
            kind: "loop",
            offset: 0,
            repeatOffset: 40,
            repeatFields: [],
            body: [
                { kind: "if", offset: 10, condition: zero, yes: [{ kind: "break", offset: 30, target: 0 }], no: [] },
            ],
        },
        set(50),
    ]);
    assert.deepEqual(unreachable(d), []);
});

test("fixedArmCode: the statements of the arms named, nested ones included", () => {
    const d = script([
        {
            kind: "if",
            offset: 0,
            condition: zero,
            yes: [set(10), { kind: "if", offset: 20, condition: zero, yes: [set(30)], no: [set(40)] }],
            no: [set(50)],
        },
    ]);
    assert.deepEqual(places(d, fixedArmCode(d, ["if 0 1"])).sort(), [10, 20, 30, 40]);
    assert.deepEqual(places(d, fixedArmCode(d, ["if 20 0"])), [40]);
    assert.deepEqual(places(d, fixedArmCode(d, ["if 0 0"])), [50]);
});

test("(expression):Command - an expression that gives no name names no object (0)", () => {
    const context = runInNewContext(`${nativeRuntimeBundle()}globalThis`);
    assert.equal(context.qb.objectName({ type: 13, bits: 0x1234 }), 0x1234);
    assert.equal(context.qb.objectName(undefined), 0);
    assert.equal(context.qb.objectName({ type: 1, bits: 5 }), 0);
});
