// NOT ORIGINAL - the readable TypeScript (emit_ts.ts + compile_ts.ts + scripting/qb_ts.js) does what emitter.ts's
// JavaScript does, on every synthetic fixture module: same command calls, arguments, waits, endings and parameters.
import assert from "node:assert/strict";
import { test } from "node:test";
import { differential } from "./differential.ts";
import { emitterQB } from "./emitter-fixtures.ts";
import { expressionResultFixture } from "./expression-result-fixtures.ts";
import { fixture } from "./fixtures.ts";
import type { ModuleIR } from "./ir.ts";
import { randomFixture } from "./random-fixtures.ts";
import { readIR } from "./reader.ts";

const modules: [string, () => ModuleIR][] = [
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
