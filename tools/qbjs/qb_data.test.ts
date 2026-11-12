// NOT ORIGINAL - the data files (qb_data.ts, emit_data.ts): every kind of value a global holds written and read back
// exactly, and the same as the module's TypeScript (with its globals in it) defines them (qb_data_check.ts). The whole
// game's globals: node tools/qbjs/qb_data_check.ts --game <folder>.
import assert from "node:assert/strict";
import { test } from "node:test";
import { moduleGlobals } from "./emit_data.ts";
import { emitTS } from "./emit_ts.ts";
import type { Expr, Field, ModuleIR } from "./ir.ts";
import { hasFlag, member, nameChecksum, readQbData, writeQbData } from "./qb_data.ts";
import { checkModule, valueText } from "./qb_data_check.ts";

const bits = (x: number) => new Uint32Array(new Float32Array([x]).buffer)[0]!;
const at = { offset: 0 };
const int = (n: number): Expr => ({ kind: "literal", ...at, value: { type: 1, bits: n >>> 0 } });
const flt = (b: number): Expr => ({ kind: "literal", ...at, value: { type: 2, bits: b >>> 0 } });
const nm = (s: string): Expr => ({ kind: "literal", ...at, value: { type: 13, bits: nameChecksum(s) } });
const str = (bytes: number[], type: 3 | 4 = 3, token?: string): Expr => ({
    kind: "literal",
    ...at,
    value: token === undefined ? { type, bytes } : { type, bytes, token },
});
const text = (s: string) => Array.from(s, (c) => c.charCodeAt(0));
const vec = (...c: number[]): Expr => ({
    kind: "literal",
    ...at,
    value: { type: c.length === 2 ? 5 : 6, coordinates: c.map((x) => x >>> 0) },
});
const fld = (name: string | 0, value: Expr): Field => ({ name: name === 0 ? 0 : nameChecksum(name), value, ...at });
const struct = (...fields: Field[]): Expr => ({ kind: "structure", ...at, fields });
const arr = (...items: Expr[]): Expr => ({ kind: "array", ...at, items });
const inline = (): Expr => ({
    kind: "inlineScript",
    ...at,
    name: nameChecksum("disqualify_script"),
    defaults: [],
    body: [{ kind: "call", offset: 0, name: nameChecksum("Foo"), fields: [] }],
});
const names = [
    "Plain",
    "Floats",
    "Texts",
    "Shapes",
    "Nodes",
    "Odd",
    "Parts",
    "Pos",
    "Name",
    "Class",
    "CreatedAtStart",
    "Links",
    "angles",
    "a",
    "b",
    "$tag",
    "7",
    "Foo",
    "disqualify_script",
    "TRG_Rail01",
    "EnvironmentObject",
    "Flag2",
];
const module = (globals: [string, Expr][]): ModuleIR => ({
    format: 2,
    source: "test.qb",
    definitions: globals.map(([name, value]) => ({ kind: "global", offset: 0, name: nameChecksum(name), value })),
    names: Object.fromEntries(names.map((n) => [nameChecksum(n), n])),
});

const fixture = module([
    ["Plain", arr(int(0), int(-1), int(0x7fffffff), int(0x80000000))],
    [
        "Floats",
        arr(
            flt(bits(1.25)),
            flt(bits(2)),
            flt(0x80000000), // -0
            flt(0x7fc00000), // NaN
            flt(0xff800000), // -infinity
            flt(bits(1e21)),
            flt(bits(1e-7)),
            flt(bits(0.1)),
            flt(1), // the smallest subnormal
            int(3), // an integer among floats: a float
        ),
    ],
    [
        "Texts",
        struct(
            fld("a", str(text('quote " backslash \\ tab \t newline \n'))),
            fld("b", str([0xe9, 0x80, 0x01, 0x7f])),
            fld(0, str(text("unnamed local"), 4)),
            fld("Name", str(text("Boston"), 3, "bo.boston")),
            fld("Class", str(text("Boston"), 4, "bo.boston")),
        ),
    ],
    ["Shapes", struct(fld("Pos", vec(bits(1.5), 0x80000000, 0x7f800000)), fld("a", vec(bits(-2), bits(0.5))))],
    [
        "Nodes",
        arr(
            struct(
                fld("Pos", vec(bits(1), bits(2), bits(3))),
                fld("angles", vec(bits(0.001), 0, bits(-0.005))), // (all but zero: left out)
                fld("Name", nm("TRG_Rail01")),
                fld("Class", nm("EnvironmentObject")),
                fld(0, nm("CreatedAtStart")),
                fld("Links", arr(int(1), int(2))),
            ),
            struct(fld("angles", vec(bits(0.5), 0, 0)), fld(0, struct(fld("a", int(1)), fld(0, nm("Flag2"))))),
            struct(),
        ),
    ],
    ["a", arr()],
    ["b", nm("_unknown_name_")],
    [
        "Odd",
        arr(
            struct(fld("a", int(1)), fld("a", flt(bits(2.5)))), // (numbers: one member, the later)
            struct(fld("a", int(1)), fld("a", nm("x")), fld("b", int(2)), fld("a", int(3))), // (the replaced to the end)
            struct(fld(0, int(5)), fld("b", int(1))),
            struct(fld("$tag", int(1))),
            struct(fld("7", int(1))),
            struct(fld("CreatedAtStart", int(1)), fld(0, nm("CreatedAtStart"))),
        ),
    ],
    ["Parts", arr(struct(fld("Name", nm("a")), fld("disqualify_script", inline())), struct(fld("b", inline())))],
]);

test("every kind of value: written, read back, the same as the TypeScript's", () => {
    const checked = checkModule(fixture, true, { "bo.boston": "Boston" });
    assert.deepEqual(checked.different, []);
    assert.equal(checked.globals, 9);
});

test("the file reads as written: plain JSON where it can, tags elsewhere", () => {
    const json = writeQbData(
        { source: "test.qb", from: "0", globals: moduleGlobals(fixture) },
        (n) => fixture.names?.[n],
    );
    assert.match(json, /"Plain": \[0, -1, 2147483647, -2147483648\]/);
    assert.match(json, /1\.25,\s+\{"\$float": 2\},\s+\{"\$float": -0\},\s+\{"\$float": "0x7fc00000"\}/);
    assert.match(json, /"Name": "TRG_Rail01",\s+"Class": "EnvironmentObject",\s+"CreatedAtStart": true/);
    assert.match(json, /\["b", \{"\$string": "\\u00e9\\u0080\\u0001\\u007f"\}\]/);
    assert.match(json, /\["Name", \{"\$tr": "bo\.boston"\}\],\s+\["Class", \{"\$local": \{"\$tr": "bo\.boston"\}\}\]/);
    assert.match(json, /"Shapes": \{"Pos": \{"\$vec": \[1\.5, -0, "0x7f800000"\]\}, "a": \{"\$pair": \[-2, 0\.5\]\}\}/);
    assert.match(json, /\{"angles": \{"\$vec": \[0\.5, 0, 0\]\}, "a": 1, "Flag2": true\}/);
    assert.match(json, /\{"\$struct": \[\[null, 5\], \["b", 1\]\]\}/);
    assert.match(json, /"disqualify_script": \{"\$script": "Parts#1"\}/);
    assert.match(json, /"_0x[0-9a-f]{8}"/); // (a name the module doesn't spell)
    const data = readQbData(json, (token) => (token === "bo.boston" ? "Boston" : undefined));
    const nodes = data.globals.find(([n]) => n === nameChecksum("Nodes"))![1];
    assert.equal(nodes.type, 12);
    const node = nodes.type === 12 ? nodes.elements[0]! : nodes;
    assert.ok(hasFlag(node, "CreatedAtStart"));
    assert.equal(valueText(member(node, "Name")!), `13:${nameChecksum("TRG_Rail01")}`);
    assert.equal(member(node, "angles"), undefined);
    assert.equal(data.names.get(nameChecksum("TRG_Rail01")), "TRG_Rail01");
});

test("a change to a value is seen", () => {
    const json = writeQbData(
        { source: "test.qb", from: "0", globals: moduleGlobals(fixture) },
        (n) => fixture.names?.[n],
    );
    const original = readQbData(json).globals.map(([, v]) => valueText(v));
    for (const [from, to] of [
        ["1.25", "1.2500001"],
        ['{"$float": -0}', "0"],
        ['"CreatedAtStart": true', '"CreatedAtStart": 1'],
        ["0.1", "0.10000001"],
    ] as const) {
        assert.ok(json.includes(from), from);
        const changed = readQbData(json.replace(from, to)).globals.map(([, v]) => valueText(v));
        assert.notDeepEqual(changed, original, `${from} -> ${to}`);
    }
});

test("the .ts of a module with data: its scripts and its globals' script values only", () => {
    const ts = emitTS(fixture, {}, { data: "test.json" });
    assert.match(ts, /\/\/ Its globals \(data\): test\.json/);
    assert.doesNotMatch(ts, /global\(/);
    assert.match(ts, /scriptValue\("Parts#1", code\(function\* \(p\) \{/);
    assert.match(ts, /scriptValue\("Parts#2", code\(/);
});
