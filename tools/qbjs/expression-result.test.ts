// NOT ORIGINAL - boundary/compatibility assertions, independent of game assets.
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { test } from "node:test";
import { runInNewContext } from "node:vm";
import { emitIR } from "./emitter.ts";
import { expressionResultBundle, expressionResultFixture } from "./expression-result-fixtures.ts";
import { field, integer, param, script } from "./fixtures.ts";
import { readIR } from "./reader.ts";

const completions = (value: unknown): number => {
    if (!value || typeof value !== "object") return 0;
    if (Array.isArray(value)) return value.reduce((sum: number, child: unknown) => sum + completions(child), 0);
    const record = value as Record<string, unknown>;
    return (
        (record.kind === "expressionResult" ? 1 : 0) +
        Object.values(record).reduce<number>((sum, child) => sum + completions(child), 0)
    );
};

test("expression results match x86 copy expectations and survive scheduler suspension", () => {
    const context = runInNewContext(expressionResultBundle() + "\nglobalThis", {}, { timeout: 5000 });
    assert.equal(context.qbExpressionResultHelperTest.completed, true);
    assert.ok(context.qbExpressionResultHelperTest.assertions >= 35);
    assert.equal(context.qbExpressionResultTest.completed, true);
    assert.ok(context.qbExpressionResultTest.assertions >= 10);
});

test("completion IR marks ParseExpression calls but leaves nested and random grouping inside the evaluator", () => {
    const fixture = expressionResultFixture();
    const grouping = fixture.ir.definitions[1]!;
    assert.equal(grouping.kind, "script");
    if (grouping.kind !== "script") throw Error("Expected script");
    assert.equal(completions(grouping), 4);
    const [grouped, random, saved] = grouping.body;
    if (
        grouped?.kind !== "assign" ||
        grouped.value.kind !== "expressionResult" ||
        grouped.value.value.kind !== "binary"
    )
        throw Error("Expected completed grouped comparison");
    assert.equal(grouped.value.value.left.kind, "parameter");
    if (
        random?.kind !== "assign" ||
        random.value.kind !== "expressionResult" ||
        random.value.value.kind !== "binary" ||
        random.value.value.left.kind !== "randomChoice"
    )
        throw Error("Expected random grouped comparison");
    assert.equal(random.value.value.left.choices[0]!.kind, "parameter");
    if (saved?.kind !== "assign" || saved.value.kind !== "randomChoice") throw Error("Expected separate random value");
    assert.equal(saved.value.choices[0]!.kind, "expressionResult");
    assert.equal(emitIR(fixture.ir), expressionResultFixture().javascript);
    assert.ok(fixture.javascript.includes("v.expressionResult("));
});

test("new completion helper is required before registration while old format-2 helpers remain compatible otherwise", () => {
    const values = readFileSync(new URL("../../scripting/values.js", import.meta.url), "utf8");
    const completion = emitIR(
        readIR(Uint8Array.from([...script("new", [...field("value", [14, ...integer(7), 15]), 1]), 0])),
    );
    const plain = emitIR(readIR(Uint8Array.from([...script("old", [...field("value", integer(7)), 1]), 0])));
    const context = runInNewContext(values + "\nglobalThis", {}, { timeout: 1000 });
    context.qbValues = Object.freeze({ ...context.qbValues, expressionResult: undefined });
    let registrations = 0;
    context.qb = {
        module() {
            ++registrations;
        },
        define() {},
    };
    assert.throws(() => runInNewContext(completion, context, { timeout: 1000 }), /expressionResult helper required/);
    assert.equal(registrations, 0);
    assert.doesNotThrow(() => runInNewContext(plain, context, { timeout: 1000 }));
    assert.equal(registrations, 1);
});

test("completion nodes retain bounds/truncation and cannot hide caller-dependent defaults or globals", () => {
    const fixture = expressionResultFixture();
    for (let end = 0; end < fixture.input.length; ++end)
        assert.throws(() => readIR(fixture.input.subarray(0, end), "completion.qb"), /completion\.qb: QB offset/);
    for (const operand of [param("caller"), [0x2c]]) {
        const defaults = Uint8Array.from([...script("bad", [], field("value", [14, ...operand, 15])), 0]);
        assert.throws(() => readIR(defaults), /defaults\/globals cannot read caller parameters/);
    }
    assert.throws(() => readIR(fixture.input, "limited.qb", { nodes: 2 }), /IR node limit/);
});
