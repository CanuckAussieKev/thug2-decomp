// NOT ORIGINAL - the readable TypeScript (emit_ts.ts + compile_ts.ts + scripting/qb_ts.js) does what emitter.ts's
// JavaScript does, on every synthetic fixture module: same command calls, arguments, waits, endings and parameters.
import assert from "node:assert/strict";
import { test } from "node:test";
import { differential } from "./differential.ts";
import { emitterQB } from "./emitter-fixtures.ts";
import { expressionResultFixture } from "./expression-result-fixtures.ts";
import { command, conditional, field, fixture, integer, name, param, script } from "./fixtures.ts";
import type { ModuleIR } from "./ir.ts";
import { randomFixture } from "./random-fixtures.ts";
import { readIR } from "./reader.ts";

// if NOT: a command's result and an expression's truth inverted, with and without else.
const negated = () =>
    readIR(
        Uint8Array.from([
            ...script(
                "notCommand",
                conditional([0x39, ...name("Probe"), ...field("a", integer(1))], command("Yes"), command("No")),
            ),
            ...script("notExpression", [
                ...field("x", integer(0)),
                1,
                ...conditional([0x39, 0x0e, ...param("x"), 0x0f], command("Yes"), command("No")),
                ...conditional([0x39, 0x0e, ...param("missing"), 0x0f], command("Yes"), command("No")),
            ]),
            ...script("notScript", conditional([0x39, ...name("notCommand")], command("Yes"), [])),
            0,
        ]),
        "negated.qb",
    );
const modules: [string, () => ModuleIR][] = [
    ["if NOT", negated],
    ["format-1 fixture", () => readIR(fixture(), "fixture.qb")],
    ["emitter fixture", () => readIR(emitterQB(), "emitter.qb")],
    ["expression results", () => expressionResultFixture().ir],
    ["random values", () => randomFixture().ir],
];
for (const [label, make] of modules)
    test(`TypeScript and JavaScript agree: ${label}`, () => {
        const outcome = differential(make());
        const i = outcome.js.findIndex((e, n) => e !== outcome.ts[n]);
        assert.ok(outcome.same, `event ${i}:\n  js ${outcome.js[i]}\n  ts ${outcome.ts[i]}`);
        assert.ok(outcome.scripts > 0);
    });
