// NOT ORIGINAL - the readable TypeScript (emit_ts.ts + compile_ts.ts + scripting/qb_ts.js) does what emitter.ts's
// JavaScript does, on every synthetic fixture module: same command calls, arguments, waits, endings and parameters.
import assert from "node:assert/strict";
import { test } from "node:test";
import { checksum } from "../qb_checksum.ts";
import { differential } from "./differential.ts";
import { emitTS } from "./emit_ts.ts";
import { emitterQB } from "./emitter-fixtures.ts";
import { expressionResultFixture } from "./expression-result-fixtures.ts";
import { command, conditional, field, fixture, integer, name, param, script } from "./fixtures.ts";
import type { ModuleIR } from "./ir.ts";
import { randomChoice, randomFixture } from "./random-fixtures.ts";
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
// C functions in expressions: (F args) and bare names, which the harness's host makes functions or not by seed;
// a global's and a script's names in the same places.
const functions = () => {
    const call = (fn: string, ...args: number[][]) => [0x0e, ...name(fn), ...args.flat(), 0x0f];
    return readIR(
        Uint8Array.from([
            ...[...name("Limit"), 7, ...integer(4), 1],
            ...script("inExpressions", [
                ...conditional(
                    [0x0e, ...call("ModeIs", name("career")), 0x32, ...call("ModeIs", name("net")), 0x0f],
                    command("Yes"),
                    command("No"),
                ),
                ...field("a", [0x0e, ...call("CountOf", field("kind", name("gaps"))), 0x0b, ...integer(1), 0x0f]),
                1,
                ...field("b", [0x0e, ...name("Alpha"), 0x0b, ...name("Beta"), 0x0b, ...name("Gamma"), 0x0f]),
                1,
                ...field("c", [0x0e, ...name("Limit"), 0x0b, ...name("Delta"), 0x0f]),
                1,
                ...field("d", [0x0e, ...name("inExpressions"), 0x0f]),
                1,
                ...command("Show", [0x2c]),
            ]),
            0,
        ]),
        "functions.qb",
    );
};
// Random among statements (one branch runs, a break inside one leaves the loop) and among a call's fields (one
// branch's fields used, the draws in their places among the other random values).
const randomBlocks = () =>
    readIR(
        Uint8Array.from([
            ...script("pickStatements", [
                0x20,
                1,
                ...randomChoice(
                    0x2f,
                    [
                        [...command("First"), ...command("Again")],
                        [...command("Second"), 0x22, 1],
                        [1, ...command("Third", field("n", integer(3)))],
                    ],
                    [2, 1, 1],
                ),
                1,
                ...command("AfterPick"),
                0x21,
                ...integer(4),
                1,
                ...randomChoice(0x40, [command("NoRepeatA"), command("NoRepeatB"), command("NoRepeatC")]),
                1,
            ]),
            ...script(
                "pickFields",
                [
                    ...command("Anim", [
                        ...field("before", randomChoice(0x2f, [integer(1), integer(2)])),
                        ...randomChoice(0x2f, [
                            field("anim", [0x2d, ...name("a")]),
                            [...field("anim", [0x2d, ...name("b")]), ...name("Cycle")],
                            field("anim", randomChoice(0x37, [name("x"), name("y")])),
                        ]),
                        ...field("blend", integer(3)),
                    ]),
                ],
                [...field("a", name("idle")), ...field("b", name("run"))],
            ),
            0,
        ]),
        "random-blocks.qb",
    );
// (expression) as a statement or condition (a name is called, else its truth), a statement naming nothing (its
// arguments still read), defaults reading parameters (none: they add nothing), Random branches ending at the jump.
const dispatched = () =>
    readIR(
        Uint8Array.from([
            ...[...name("Handler"), 7, ...name("Target"), 1],
            ...[...name("Flag"), 7, ...integer(1), 1],
            ...script(
                "dispatch",
                [
                    ...[0x0e, ...param("cmd"), 0x0f, ...field("x", integer(1)), 1],
                    ...[0x0e, ...name("Handler"), 0x0f, 1],
                    ...[0x0e, ...param("number"), 0x0f, 1],
                    ...conditional([0x0e, ...name("Handler"), 0x0f], command("Yes"), command("No")),
                    ...conditional([0x0e, ...name("Flag"), 0x0f], command("Yes"), command("No")),
                    ...[0x1b, 5, 0, 0, 0, 0x68, 0x65, 0x79, 0x21, 0, ...field("y", integer(2)), 1],
                    ...randomChoice(0x2f, [command("Left").slice(0, -1), command("Right").slice(0, -1)]),
                    1,
                    ...command("Show", [0x2c]),
                ],
                [
                    ...field("cmd", name("Called")),
                    ...field("number", integer(0)),
                    ...field("fromCaller", param("anything")),
                    0x2c,
                ],
            ),
            0,
        ]),
        "dispatch.qb",
    );
// Statements before a switch's first case (never run), `if x = 1` (assigns, true), (expression):Command.
const oddForms = () =>
    readIR(
        Uint8Array.from([
            ...[...name("Who"), 7, 5, ...name("Ann"), ...name("Bob"), 6, 1],
            ...script(
                "odd",
                [
                    ...[0x3c, 0x2d, ...name("k"), 1, ...command("NeverRuns"), 0x3e, 0x49, 0x11, 0x00, ...integer(1), 1],
                    ...command("One"),
                    ...[0x49, 0x02, 0x00, 0x3d, 1],
                    ...conditional([...name("set"), 7, ...integer(3)], command("Yes"), command("No")),
                    ...conditional([0x39, ...name("set2"), 7, ...integer(4)], command("Yes"), command("No")),
                    ...[0x0e, ...name("Who"), 0x05, ...integer(1), 0x06, 0x0f, 0x42, ...name("Wave"), 1],
                    ...command("Show", [0x2c]),
                ],
                field("k", integer(1)),
            ),
            0,
        ]),
        "odd.qb",
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
// Jumps into a script's tail (only closers before its endscript - reader.ts tailEnd): an if inside a loop whose false
// jump leaves the loop for the end (z_el), a switch inside an if with its endswitch after the endif (CA_Scripts), a
// Random's branch jumping past the repeat (SK6Ped_UniqueStateLogic).
const le16 = (n: number) => [n & 255, n >>> 8];
const le32 = (n: number) => [n & 255, (n >>> 8) & 255, (n >>> 16) & 255, n >>> 24];
const tails = () =>
    readIR(
        Uint8Array.from([
            // begin / X / if Cond / Y / repeat / endif / $ - the if's false jump goes to the line after the endif
            ...script("ifIntoTail", [
                0x20,
                1,
                ...command("X"),
                0x47,
                ...le16(26 - 11),
                ...name("Cond"),
                1,
                ...command("Y"),
                0x21,
                1,
                0x28,
                1,
            ]),
            // if A / switch 1 / case 1: One (exit to the tail) / case 2: Two / endif / endswitch / $
            ...script("switchIntoTail", [
                0x47,
                ...le16(53),
                ...name("A"),
                1,
                0x3c,
                ...integer(1),
                1,
                0x3e,
                0x49,
                ...le16(17),
                ...integer(1),
                1,
                ...command("One"),
                0x49,
                ...le16(21),
                0x3e,
                0x49,
                ...le16(18),
                ...integer(2),
                1,
                ...command("Two"),
                0x28,
                1,
                0x3d,
                1,
            ]),
            // begin / Random(@ A [jump past the repeat] @ B) / repeat / $
            ...script("randomIntoTail", [
                0x20,
                1,
                0x2f,
                ...le32(2),
                ...le16(1),
                ...le16(1),
                ...le32(4),
                ...le32(11),
                ...command("A"),
                0x2e,
                ...le32(7),
                ...command("B"),
                0x21,
                1,
            ]),
            0,
        ]),
        "tails.qb",
    );
// A structure holding a script (the CAS tables' `script name ... endscript` values), passed to a command.
const scriptValues = () =>
    readIR(
        Uint8Array.from([
            ...name("parts"),
            7,
            3,
            ...field("desc", name("a")),
            1,
            0x23,
            ...name("part_script"),
            1,
            ...command("SetPart", field("x", integer(1))),
            0x24,
            1,
            4,
            1,
            ...script("useParts", command("Use", field("t", [0x0e, ...name("parts"), 0x0f]))),
            0,
        ]),
        "script-values.qb",
    );
const modules: [string, () => ModuleIR][] = [
    ["jumps into a script's tail", tails],
    ["scripts as values", scriptValues],
    ["if NOT", negated],
    ["<...> values and commands named by parameters", dynamic],
    ["object:Command", objects],
    ["C functions in expressions", functions],
    ["Random among statements and fields", randomBlocks],
    ["(expression) statements, nameless statements, defaults reading parameters", dispatched],
    ["statements before a first case, if x = 1, (expression):Command", oddForms],
    ["format-1 fixture", () => readIR(fixture(), "fixture.qb")],
    ["emitter fixture", () => readIR(emitterQB(), "emitter.qb")],
    ["expression results", () => expressionResultFixture().ir],
    ["random values", () => randomFixture().ir],
];
test("a jump into the script's tail is a return; a script value is named by its own name", () => {
    const [ifInto, switchInto, randomInto] = tails().definitions;
    assert.ok(ifInto?.kind === "script" && switchInto?.kind === "script" && randomInto?.kind === "script");
    const loop = ifInto.body[0];
    assert.ok(loop?.kind === "loop" && loop.body[1]?.kind === "if");
    assert.deepEqual(
        loop.body[1].no.map((x) => x.kind),
        ["return"],
    );
    const branch = switchInto.body[0];
    assert.ok(branch?.kind === "if" && branch.yes[0]?.kind === "switch" && branch.yes[0].cases.length === 2);
    const random = randomInto.body[0];
    assert.ok(random?.kind === "loop" && random.body[0]?.kind === "randomBlock");
    assert.deepEqual(
        random.body[0].branches.map((b) => b.map((x) => x.kind)),
        [["call", "return"], ["call"]],
    );
    const parts = scriptValues().definitions[0];
    assert.ok(parts?.kind === "global" && parts.value.kind === "structure");
    const member = parts.value.fields[1];
    assert.ok(member && "value" in member && member.value.kind === "inlineScript");
    assert.equal(member.name, checksum("part_script"));
    const names = Object.fromEntries(["part_script", "SetPart", "x"].map((n) => [checksum(n), n]));
    assert.match(
        emitTS(scriptValues(), names),
        /part_script: code\(function\* \(p\) \{\n {8}yield\* SetPart\(\{ x: 1 \}\);\n {4}\}\)/,
    );
});
for (const [label, make] of modules)
    test(`TypeScript and JavaScript agree: ${label}`, () => {
        const outcome = differential(make());
        const i = outcome.js.findIndex((e, n) => e !== outcome.ts[n]);
        assert.ok(outcome.same, `event ${i}:\n  js ${outcome.js[i]}\n  ts ${outcome.ts[i]}`);
        assert.ok(outcome.scripts > 0);
    });
