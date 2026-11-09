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

// <...> as a value (a copy of the parameters) and commands named by a parameter: <x> args, <...> args, in a
// condition, a parameter holding no name, one holding a structure, and one naming a script of the module.
const dynamic = () =>
    readIR(
        Uint8Array.from([
            ...script(
                "allCopy",
                [
                    ...command("Show", [...field("params", [0x2c]), ...field("b", integer(2))]),
                    ...field("copied", [0x2c]),
                    1,
                    ...field("late", integer(3)),
                    1,
                    ...command("Show", field("copy", param("copied"))),
                ],
                field("a", integer(1)),
            ),
            ...script(
                "named",
                [
                    ...command("Probe", [0x2d, ...name("target")]),
                    ...[0x2d, ...name("target"), ...field("x", integer(5)), 1],
                    ...[0x2d, ...name("missing"), ...field("x", integer(6)), 1],
                    ...[0x2d, ...name("structured"), 1],
                    ...[0x2d, ...name("local"), ...field("x", integer(7)), 1],
                    ...[0x2c, ...field("y", integer(8)), 1],
                    ...conditional([0x2d, ...name("target")], command("Yes"), command("No")),
                    ...conditional([0x39, 0x2d, ...name("missing")], command("Yes"), command("No")),
                ],
                [
                    ...field("target", name("SomeCommand")),
                    ...field("structured", [3, ...field("m", integer(1)), 4]),
                    ...field("local", name("allCopy")),
                    ...name("Unnamed"),
                ],
            ),
            0,
        ]),
        "dynamic.qb",
    );
// object:Command - written and parameter-named objects and commands, a script of the module run on an object (its
// own commands then run on it), in a condition, and an object that doesn't exist.
const objects = () =>
    readIR(
        Uint8Array.from([
            ...script(
                "onObjects",
                [
                    ...[...name("Board"), 0x42, ...name("Flip"), ...field("speed", integer(2)), 1],
                    ...[0x2d, ...name("who"), 0x42, ...name("Flip"), 1],
                    ...[...name("Board"), 0x42, ...name("onInner"), ...field("x", integer(1)), 1],
                    ...[...name("Board"), 0x42, 0x2d, ...name("what"), 1],
                    ...conditional([...name("Board"), 0x42, ...name("Ready")], command("Yes"), command("No")),
                    ...command("Plain"),
                    ...[0x2d, ...name("nobody"), 0x42, ...name("Flip"), 1],
                    ...command("Unreached"),
                ],
                [...field("who", name("Skater")), ...field("what", name("Spin")), ...field("nobody", integer(3))],
            ),
            ...script("onInner", [...command("Inner", [0x2c]), ...command("onDeeper")]),
            ...script("onDeeper", command("Deeper")),
            0,
        ]),
        "objects.qb",
    );
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
    ["<...> values and commands named by parameters", dynamic],
    ["object:Command", objects],
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
