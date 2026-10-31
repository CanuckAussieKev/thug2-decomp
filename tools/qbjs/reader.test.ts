// NOT ORIGINAL - synthetic binary cases for the phase-one format-2 reader.
import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { linkSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { test } from "node:test";
import { fileURLToPath } from "node:url";
import { command, conditional, field, fixture, integer, name, param, script, word } from "./fixtures.ts";
import type { Expr } from "./ir.ts";
import { readIR } from "./reader.ts";
import { checksum, translate } from "./translate.ts";

const module = (...data: number[][]) => Uint8Array.from([...data.flat(), 0]);
const global = (key: string, value: number[]) => [...field(key, value), 1];
const structure = (...data: number[][]) => [3, ...data.flat(), 4];
const array = (...data: number[][]) => [5, ...data.flat(), 6];
const string = (bytes: number[], local = false) => [local ? 0x1c : 0x1b, ...word(bytes.length), ...bytes];
const literal = (x: Expr) => {
    assert.equal(x.kind, "literal");
    if (x.kind !== "literal") throw Error("not literal");
    return x.value;
};
const parens = (...parts: number[][]) => [14, ...parts.flat(), 15];
const expressionIR = (tokens: number[]): Expr => {
    const def = readIR(module(script("expr", [...field("result", tokens), 1]))).definitions[0]!;
    assert.equal(def.kind, "script");
    if (def.kind !== "script" || def.body[0]?.kind !== "assign") throw Error("expected assignment");
    const completed = def.body[0].value;
    assert.equal(completed.kind, "expressionResult");
    if (completed.kind !== "expressionResult") throw Error("expected expression completion");
    return completed.value;
};
const shape = (v: Expr): unknown =>
    v.kind === "binary"
        ? [v.op, shape(v.left), shape(v.right)]
        : v.kind === "literal" && "bits" in v.value
          ? v.value.bits
          : v.kind;

test("uses original evaluator priorities, left associativity and explicit grouping", () => {
    assert.deepEqual(shape(expressionIR(parens(integer(8), [12], integer(4), [13], integer(2)))), [12, 8, [13, 4, 2]]);
    assert.deepEqual(shape(expressionIR(parens(integer(8), [10], integer(4), [11], integer(2)))), [11, [10, 8, 4], 2]);
    assert.deepEqual(shape(expressionIR(parens(integer(8), [20], integer(4), [18], integer(2)))), [20, 8, [18, 4, 2]]);
    assert.deepEqual(shape(expressionIR(parens(parens(integer(8), [12], integer(4)), [13], integer(2)))), [
        13,
        [12, 8, 4],
        2,
    ]);
    assert.deepEqual(shape(expressionIR(parens(integer(1), [50], integer(2), [51], integer(3)))), [50, 1, [51, 2, 3]]);
});
test("keeps index operators ungrouped and member names unresolved", () => {
    assert.deepEqual(shape(expressionIR(parens(name("items"), [5], integer(1), [11], integer(2), [6]))), [
        11,
        [5, "global", 1],
        2,
    ]);
    assert.deepEqual(shape(expressionIR(parens(name("items"), [5], parens(integer(1), [11], integer(2)), [6]))), [
        5,
        "global",
        [11, 1, 2],
    ]);
    const member = expressionIR(parens(name("owner"), [8], parens(name("field"))));
    assert.equal(member.kind, "binary");
    if (member.kind !== "binary") throw Error("expected binary");
    assert.equal(member.left.kind, "global");
    assert.deepEqual(literal(member.right), { type: 13, bits: checksum("field") });
    assert.equal(member.offset, member.left.offset + 5);
    const parameter = expressionIR(parens(param("p")));
    assert.equal(parameter.kind, "parameter");
    if (parameter.kind === "parameter") assert.equal(parameter.mode, "expression");
    assert.equal(expressionIR(parens([0x2c])).kind, "allParameters");
    assert.equal(expressionIR(parens(structure(field("x", integer(1))))).kind, "structure");
});
test("decodes implicit negative subtraction with raw float bits", () => {
    assert.deepEqual(shape(expressionIR(parens(integer(8), integer(-2), [13], integer(3)))), [10, 8, [13, 2, 3]]);
    assert.deepEqual(shape(expressionIR(parens(integer(8), [0x1a, ...word(0xc0200000)]))), [10, 8, 0x40200000]);
    assert.equal(literal(expressionIR(parens([0x1a, ...word(0x80000000)]))).type, 2);
    assert.throws(() => expressionIR(parens(integer(8), [0x1a, ...word(0x80000000)])), /adjacent/);
    assert.throws(() => expressionIR(parens(integer(8), integer(-2147483648))), /INT_MIN/);
});
test("bounds expression trees and rejects malformed or caller-dependent expressions", () => {
    for (const tokens of [
        parens(),
        parens(integer(1), [11]),
        parens(integer(1), integer(2)),
        parens(integer(1), [5], integer(0)),
        parens(integer(1), [6]),
        parens([0x42]),
        parens([0x18, ...word(1)]),
    ])
        assert.throws(() => expressionIR(tokens), /QB offset/);
    const flat = parens(integer(1), ...Array.from({ length: 129 }, () => [11, ...integer(1)]));
    assert.throws(() => expressionIR(flat), /depth limit/);
    for (const rhs of [parens(integer(1), [11], param("p")), parens([0x2c])])
        assert.throws(() => readIR(module(global("x", rhs))), /caller parameters/);
    const full = module(script("expr", [...field("x", parens(name("a"), [8], name("b"), [7], integer(1))), 1]));
    for (let end = 0; end < full.length; end++)
        assert.throws(() => readIR(full.subarray(0, end), "expr.qb"), /expr.qb: QB offset/);
});
const loop = (body: number[], fields: number[] = []) => [0x20, 1, ...body, 0x21, ...fields, 1];
const scriptBody = (body: number[]) => {
    const def = readIR(module(script("loops", body))).definitions[0]!;
    if (def.kind !== "script") throw Error("expected script");
    return def.body;
};
// Synthetic compiled switch, independent of game bytes. Empty adjacent case
// bodies encode the original grouped-label pattern (49 xx xx 3E 49 ...).
const switchTokens = (selector: number[], cases: [number[], number[]][], fallback?: number[]) => {
    const bytes = [0x3c, ...selector, 1],
        exits: number[] = [];
    let previous: number | undefined;
    const jump = (field: number, target: number) => {
        bytes[field] = (target - field) & 255;
        bytes[field + 1] = (target - field) >>> 8;
    };
    for (const [fields, body] of cases) {
        const start = bytes.length;
        if (previous !== undefined) jump(previous, start);
        bytes.push(0x3e, 0x49, 0, 0, ...fields, 1, ...body, 0x49, 0, 0);
        previous = start + 2;
        exits.push(bytes.length - 2);
    }
    if (previous !== undefined) jump(previous, bytes.length);
    if (fallback !== undefined) {
        bytes.push(0x3f, 0x49, 0, 0, ...fallback);
        exits.push(bytes.length - fallback.length - 2);
    }
    const end = bytes.length;
    bytes.push(0x3d, 1);
    for (const field of exits) jump(field, end);
    return bytes;
};
test("reads switch cases/default and preserves full selector/label arguments", () => {
    const selector = [...integer(1), ...field("named", integer(2)), 0x2c];
    const tokens = switchTokens(
        selector,
        [
            [param("x"), command("One")],
            [[], command("Two")],
        ],
        [],
    );
    const statement = scriptBody(tokens)[0]!;
    if (statement.kind !== "switch") throw Error("expected switch");
    assert.equal(statement.offset, 7);
    assert.equal(statement.fields.length, 3);
    assert.equal(statement.cases.length, 2);
    assert.equal(statement.cases[0]!.labels[0]!.offset, 7 + 2 + selector.length);
    assert.equal(statement.cases[1]!.labels[0]!.fields.length, 0);
    assert.deepEqual(statement.fallback, []);
    const empty = scriptBody(switchTokens([], []))[0]!;
    if (empty.kind !== "switch") throw Error("expected switch");
    assert.deepEqual(empty.fields, []);
    assert.deepEqual(empty.cases, []);
    assert.equal("fallback" in empty, false);
    const defaultOnly = scriptBody(switchTokens([], [], command("Fallback")))[0]!;
    if (defaultOnly.kind !== "switch") throw Error("expected default-only switch");
    assert.equal(defaultOnly.fallback?.[0]?.kind, "call");
});
test("groups adjacent labels without losing their argument evaluation or nested control flow", () => {
    const nested = switchTokens(integer(2), [[integer(2), command("Nested")]], command("Default"));
    const statement = scriptBody(
        loop(
            switchTokens(param("x"), [
                [integer(1), []],
                [param("y"), []],
                [integer(3), [...nested, 0x22, 1]],
                [integer(4), command("Other")],
            ]),
        ),
    )[0]!;
    if (statement.kind !== "loop" || statement.body[0]?.kind !== "switch") throw Error("expected switch in loop");
    const branch = statement.body[0].cases[0]!;
    assert.equal(branch.labels.length, 3);
    assert.equal(
        branch.labels[1]!.fields[0] && "value" in branch.labels[1]!.fields[0]!
            ? branch.labels[1]!.fields[0]!.value.kind
            : "missing",
        "parameter",
    );
    assert.equal(branch.body[0]?.kind, "switch");
    assert.deepEqual(branch.body[1], { kind: "break", offset: branch.body[1]!.offset, target: statement.offset });
    assert.equal(statement.body[0].cases.length, 2);
    // A nonempty body's exit jump must not turn the following label into part
    // of the group, even when that label immediately follows the jump bytes.
    assert.equal(statement.body[0].cases[1]!.labels.length, 1);
});
test("validates switch headers, destinations, nesting and every truncation", () => {
    const valid = switchTokens(integer(1), [[integer(1), command("One")]], command("Default"));
    const caseOffset = 7,
        exit = 7 + 4 + 5 + 1 + command("One").length;
    for (const position of [caseOffset + 1, caseOffset + 2, exit + 1, valid.indexOf(0x3f) + 1]) {
        const bad = [...valid];
        bad[position] = 0;
        assert.throws(() => scriptBody(bad), /QB offset/);
    }
    for (const body of [
        [0x3e, 1],
        [0x3f, 1],
        [0x3d, 1],
        [0x49, 0, 0],
        [0x3c, ...integer(1), 1],
        [0x3c, ...integer(1), 1, ...command("BeforeCase"), 0x3d, 1],
    ])
        assert.throws(() => scriptBody(body), /QB offset/);
    const full = module(script("switch", valid));
    for (let end = 0; end < full.length; end++)
        assert.throws(() => readIR(full.subarray(0, end), "switch.qb"), /switch.qb: QB offset/);
    let nested = command("Leaf");
    for (let i = 0; i < 65; i++) nested = switchTokens(integer(1), [[integer(1), nested]]);
    assert.throws(() => scriptBody(nested), /statement nesting limit/);
});
test("preserves loop bodies, repeat arguments and exact begin/repeat offsets", () => {
    for (const count of [
        [],
        integer(0),
        integer(-1),
        integer(3),
        param("count"),
        [...integer(1), ...field("named", integer(2)), ...integer(3), 0x2c],
    ]) {
        const body = command("Tick"),
            tokens = loop(body, count);
        const ir = scriptBody(tokens)[0]!;
        assert.equal(ir.kind, "loop");
        if (ir.kind !== "loop") throw Error("expected loop");
        assert.equal(ir.body[0]?.kind, "call");
        assert.equal(ir.offset, 7);
        assert.equal(ir.repeatOffset, 7 + 2 + body.length);
        assert.equal(ir.repeatFields.length, count.length ? (count.at(-1) === 0x2c ? 4 : 1) : 0);
        if (count[0] === 0x2d) {
            const f = ir.repeatFields[0]!;
            if ("spread" in f || f.value.kind !== "parameter") throw Error("expected parameter");
            assert.equal(f.value.mode, "value");
        }
    }
    // Begin itself is a token, not an argument line; same-line bodies are valid.
    assert.equal(scriptBody([0x20, ...command("Tick"), 0x21, 1])[0]?.kind, "loop");
});
test("binds breaks through if branches to the innermost lexical loop", () => {
    const body = scriptBody(
        loop([...loop(conditional(name("Check"), [0x22, 1], []), integer(2)), 0x22, 1], integer(3)),
    );
    const outer = body[0]!;
    if (outer.kind !== "loop") throw Error("expected outer loop");
    const inner = outer.body[0]!,
        outerBreak = outer.body[1]!;
    if (inner.kind !== "loop" || outerBreak.kind !== "break") throw Error("expected inner loop and break");
    const branch = inner.body[0]!;
    if (branch.kind !== "if" || branch.yes[0]?.kind !== "break") throw Error("expected conditional break");
    assert.equal(branch.yes[0].target, inner.offset);
    assert.equal(outerBreak.target, outer.offset);
    assert.equal(scriptBody([...loop([], integer(1)), ...loop([], integer(2))]).length, 2);
});
test("rejects unmatched/crossing loops, cross-call break and excessive loop nesting", () => {
    for (const body of [
        [0x21, 1],
        [0x20, 1],
        [0x22, 1],
        [0x20, 1, 0x28, 1],
        loop(conditional(name("Check"), [0x21, 1], [])),
        loop([0x22, ...integer(2), 1]),
    ])
        assert.throws(() => scriptBody(body), /QB offset/);
    let nested: number[] = [];
    for (let i = 0; i < 10; i++) nested = loop(nested);
    assert.equal(scriptBody(nested)[0]?.kind, "loop");
    assert.throws(() => scriptBody(loop(nested)), /loop nesting/);
    const full = module(script("loops", loop([0x22, 1], param("n"))));
    for (let end = 0; end < full.length; end++)
        assert.throws(() => readIR(full.subarray(0, end), "loop.qb"), /loop.qb: QB offset/);
});

test("preserves ordered globals, aliases and duplicate replacements without evaluating them", () => {
    const first = global("x", integer(7)),
        alias = global("alias", name("x"));
    const ir = readIR(module(first, alias, global("x", name("missing"))), "globals.qb");
    assert.equal(ir.format, 2);
    assert.equal(ir.source, "globals.qb");
    assert.equal(ir.definitions.length, 3);
    assert.deepEqual(
        ir.definitions.map((x) => [x.kind, x.name, x.offset]),
        [
            ["global", checksum("x"), 0],
            ["global", checksum("alias"), first.length],
            ["global", checksum("x"), first.length + alias.length],
        ],
    );
    const def = ir.definitions[1]!;
    assert.equal(def.kind, "global");
    if (def.kind === "global") assert.deepEqual(literal(def.value), { type: 13, bits: checksum("x") });
    assert.deepEqual(readIR(module(), "empty.qb").definitions, []);
    assert.equal(readIR(module(field("x", integer(1)), field("x", integer(2)))).definitions.length, 2);
});
test("reads raw string bytes, local strings and coordinate bits with exact token offsets", () => {
    const prefix = global("s", string([0xc0, 0xff, 0, 65]));
    const ir = readIR(
        module(
            prefix,
            global("local", string([0], true)),
            global("vector", [0x1e, ...word(0x80000000), ...word(0x7fc00123), ...word(0x3f800000)]),
            global("pair", [0x1f, ...word(0), ...word(0x40000000)]),
        ),
    );
    const values = ir.definitions.map((d) => {
        assert.equal(d.kind, "global");
        if (d.kind !== "global") throw Error("not global");
        return literal(d.value);
    });
    assert.deepEqual(values, [
        { type: 3, bytes: [0xc0, 0xff] },
        { type: 4, bytes: [] },
        { type: 6, coordinates: [0x80000000, 0x7fc00123, 0x3f800000] },
        { type: 5, coordinates: [0, 0x40000000] },
    ]);
    assert.equal(ir.definitions[1]!.offset, prefix.length);
});
test("reads structures, nested arrays, numeric promotion candidates and raw Name array elements", () => {
    const input = module(
        global("x", integer(4)),
        global(
            "tree",
            structure(
                field("a", array(integer(-1), [0x1a, ...word(0x3f800000)])),
                [1, 9],
                name("flag"),
                [2, ...word(9)],
                field("nested", array(array(name("x")), array(name("missing")))),
                field("empty", array()),
                field("structs", array(structure(field("x", integer(1))))),
            ),
        ),
    );
    const d = readIR(input).definitions[1]!;
    assert.equal(d.kind, "global");
    if (d.kind !== "global" || d.value.kind !== "structure") throw Error("expected structure");
    assert.equal(d.value.fields.length, 5);
    const a = d.value.fields[0]!;
    if ("spread" in a || a.value.kind !== "array") throw Error("expected array");
    assert.deepEqual(a.value.items.map(literal), [
        { type: 1, bits: 0xffffffff },
        { type: 2, bits: 0x3f800000 },
    ]);
    const nested = d.value.fields[2]!;
    if ("spread" in nested || nested.value.kind !== "array") throw Error("expected nested array");
    const inner = nested.value.items[0]!;
    if (inner.kind !== "array") throw Error("expected inner array");
    assert.deepEqual(literal(inner.items[0]!), { type: 13, bits: checksum("x") });
});
test("flattens anonymous brace inclusions while preserving named nested structures", () => {
    const fields = [
        ...name("x"),
        1,
        7,
        2,
        ...word(11),
        ...integer(1),
        1,
        ...structure(field("y", integer(2)), structure(field("z", integer(3)))),
    ];
    const first = readIR(module(global("g", structure(fields)))).definitions[0]!;
    if (first.kind !== "global" || first.value.kind !== "structure") throw Error("expected structure");
    assert.deepEqual(
        first.value.fields.map((f) => ("spread" in f ? null : f.name)),
        [checksum("x"), checksum("y"), checksum("z")],
    );
    const d = readIR(
        module(
            script(
                "main",
                command("Host", [
                    ...structure(field("a", integer(1))),
                    9,
                    ...field("nested", structure(field("b", integer(2)))),
                ]),
            ),
        ),
    ).definitions[0]!;
    if (d.kind !== "script" || d.body[0]?.kind !== "call") throw Error("expected call");
    assert.deepEqual(
        d.body[0].fields.map((f) => ("spread" in f ? null : f.name)),
        [checksum("a"), checksum("nested")],
    );
    const nested = d.body[0].fields[1]!;
    if ("spread" in nested) throw Error("expected nested value");
    assert.equal(nested.value.kind, "structure");
});
test("keeps copy/reference contexts explicit and accepts containers in script parameters", () => {
    const body = [
        ...field("payload", structure(field("x", param("x")), [1], field("list", array(integer(1))))),
        1,
        ...command("Host", [...field("p", param("payload")), 0x2c]),
        ...conditional([0x0e, ...param("x"), 0x0f], [0x29, ...field("result", param("payload")), 1], command("Other")),
    ];
    const d = readIR(module(script("main", body, field("x", integer(7))))).definitions[0]!;
    if (d.kind !== "script") throw Error("expected script");
    assert.equal(d.defaults.length, 1);
    assert.equal(d.body.length, 3);
    const assignment = d.body[0]!;
    if (assignment.kind !== "assign" || assignment.value.kind !== "structure") throw Error("expected assignment");
    const x = assignment.value.fields[0]!;
    if ("spread" in x) throw Error("not parameter");
    assert.equal(x.value.kind, "parameter");
    if (x.value.kind === "parameter") assert.equal(x.value.mode, "value");
    const call = d.body[1]!;
    if (call.kind !== "call") throw Error("expected call");
    assert.equal("spread" in call.fields[1]!, true);
    const branch = d.body[2]!;
    if (branch.kind !== "if" || branch.condition.kind !== "expressionResult") throw Error("expected condition");
    if (branch.condition.value.kind !== "parameter") throw Error("expected parameter operand");
    assert.equal(branch.condition.value.mode, "expression");
    const g = readIR(module(global("expr", [0x0e, ...name("x"), 0x0f]))).definitions[0]!;
    if (g.kind !== "global") throw Error("expected global");
    assert.equal(g.value.kind, "expressionResult");
    if (g.value.kind !== "expressionResult") throw Error("expected completed global expression");
    assert.equal(g.value.value.kind, "global");
});
test("accepts multiline global assignments, numbered EOL and validated debug-name records", () => {
    const data = [
        ...name("x"),
        2,
        ...word(100),
        7,
        1,
        ...integer(1),
        2,
        ...word(101),
        0x2b,
        ...word(checksum("x")),
        120,
        0,
        1,
    ];
    const ir = readIR(module(data));
    assert.equal(ir.definitions.length, 1);
    assert.equal(ir.definitions[0]!.name, checksum("x"));
    assert.throws(() => readIR(Uint8Array.from([0x2b, ...word(1), 65])), /unterminated checksum-name/);
});
test("all truncations of a compound module fail with source offsets", () => {
    const bytes = module(
        global("data", structure(field("s", string([65, 0])), field("v", [0x1e, ...word(0), ...word(1), ...word(2)]))),
        script("f", command("Host", field("a", array(integer(1))))),
    );
    readIR(bytes);
    for (let end = 0; end < bytes.length; ++end)
        assert.throws(() => readIR(bytes.subarray(0, end), "cut.qb"), /cut.qb: QB offset 0x/);
    assert.throws(() => readIR(Uint8Array.from([...bytes, 0])), /trailing bytes/);
    assert.throws(() => readIR(module(global("s", string([65, 66])))), /unterminated QB string/);
    assert.throws(() => readIR(module(global("s", string([])))), /unterminated QB string/);
});
test("enforces nodes, nesting, field counts and string/input limits", () => {
    const bytes = module(global("a", array(integer(1))));
    assert.throws(() => readIR(bytes, "limit.qb", { nodes: 1 }), /IR node limit/);
    assert.throws(() => readIR(bytes, "limit.qb", { depth: 0 }), /value depth limit/);
    assert.throws(() => readIR(bytes, "limit.qb", { nodes: -1 }), /invalid reader limit/);
    const fields = Array.from({ length: 4097 }, () => name("flag")).flat();
    assert.throws(() => readIR(module(global("a", structure(fields)))), /field count limit/);
    assert.throws(
        () => readIR(module(global("s", string([...new Array<number>(250001).fill(65), 0])))),
        /string byte limit/,
    );
    assert.throws(() => readIR(new Uint8Array(4 * 1024 * 1024 + 1)), /module exceeds/);
});
test("rejects unsafe arrays, caller-dependent defaults/globals and unimplemented control flow", () => {
    for (const value of [array(integer(1), string([65, 0])), array(param("x")), array([0x2f, 0, 0, 0, 0])])
        assert.throws(() => readIR(module(global("a", value))), /mixed literal-array|unsupported literal-array/);
    for (const value of [param("x"), structure(field("x", param("p"))), structure([0x2c])]) {
        assert.throws(() => readIR(module(global("a", value))), /caller parameters/);
        assert.throws(() => readIR(module(script("a", [], field("p", value)))), /caller parameters/);
    }
    for (const body of [
        [0x20, 1],
        [0x3c, ...integer(1), 1],
        [0x43, ...word(1), 1],
    ])
        assert.throws(() => readIR(module(script("a", body))), /QB offset/);
    assert.throws(() => readIR(module([0x45, ...name("section")])), /unsupported definition token/);
});
test("validates jump boundaries and keeps format-1 translation unchanged", () => {
    const branch = conditional(name("Check"), command("Yes"), command("No"));
    const valid = module(script("a", branch));
    assert.equal(readIR(valid).definitions.length, 1);
    branch[1] = 1;
    assert.throws(() => readIR(module(script("a", branch))), /if jump/);
    const input = fixture();
    assert.equal(readIR(input).definitions.length, 16);
    assert.equal(translate(input, "fixture.qb"), translate(input, "fixture.qb"));
    assert.match(translate(input), /format.*1/);
});
test("accepts jump targets at endif and following EOL boundaries, rejecting payload targets", () => {
    const condition = name("Check"),
        yes = command("Yes");
    const body = [0x47, 0, 0, ...condition, 1, ...yes, 0x28, 2, ...word(99)];
    const endif = 3 + condition.length + 1 + yes.length;
    for (const target of [endif, endif + 1, body.length]) {
        body[1] = (target - 1) & 255;
        body[2] = (target - 1) >>> 8;
        assert.equal(readIR(module(script("a", body))).definitions.length, 1);
    }
    body[1] = (endif + 2 - 1) & 255;
    body[2] = (endif + 2 - 1) >>> 8;
    assert.throws(() => readIR(module(script("a", body))), /if jump/);
    const branch = conditional(condition, yes, command("No"));
    const endField = 3 + condition.length + 1 + yes.length + 1;
    const endTarget = branch.length - 1;
    branch[endField] = (endTarget - endField) & 255;
    branch[endField + 1] = (endTarget - endField) >>> 8;
    assert.equal(readIR(module(script("a", branch))).definitions.length, 1);
    assert.throws(() => readIR(module(script("a", conditional(integer(1), yes, [])))), /unsupported condition/);
    assert.throws(() => readIR(Uint8Array.from([...word(0xb075ef6f), 0])), /precompiled QB images/);
});
test("CLI writes deterministic IR and preserves input, including same-path/hardlink rejection", () => {
    const dir = mkdtempSync(join(tmpdir(), "thug2-reader-"));
    try {
        const input = join(dir, "fixture.qb"),
            output = join(dir, "fixture.ir.json"),
            alias = join(dir, "alias.qb");
        const bytes = Buffer.from(module(global("g", array(integer(1), integer(2)))));
        writeFileSync(input, bytes);
        const cli = fileURLToPath(new URL("./reader.ts", import.meta.url));
        const run = (target: string) => execFileSync(process.execPath, [cli, input, target], { stdio: "pipe" });
        run(output);
        const first = readFileSync(output, "utf8");
        run(output);
        assert.equal(readFileSync(output, "utf8"), first);
        assert.deepEqual(JSON.parse(first), readIR(bytes, input));
        assert.deepEqual(readFileSync(input), bytes);
        assert.throws(() => run(input), /must differ/);
        linkSync(input, alias);
        assert.throws(() => run(alias), /must differ/);
        assert.deepEqual(readFileSync(input), bytes);
        writeFileSync(input, Buffer.from([0x16, 1]));
        assert.throws(() => run(output), /truncated/);
        assert.equal(readFileSync(output, "utf8"), first);
    } finally {
        rmSync(dir, { recursive: true, force: true });
    }
});
