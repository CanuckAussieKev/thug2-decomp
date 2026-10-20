// NOT ORIGINAL - tests of tools/lib/converter_args.ts against what Python's argparse gives the converters' command lines.

import assert from "node:assert/strict";
import { test } from "node:test";
import { parseConverterArgs } from "./converter_args.ts";
import { SystemExit } from "./py.ts";

const OPTIONS = {
    thug2: { kind: "string", default: "C:\\Games\\THUG2\\Game" },
    out: { kind: "string" },
    "no-scripts": { kind: "flag" },
    extra: { kind: "list" },
} as const;

const parse = (...argv: string[]) => parseConverterArgs("t.ts", ["convert", "selfcheck"], OPTIONS, argv);

test("converter args: command, levels, options in any order, defaults", () => {
    const a = parse("convert", "Alc", "Zoo", "--out", "x", "--no-scripts");
    assert.equal(a.command, "convert");
    assert.deepEqual(a.positionals, ["Alc", "Zoo"]);
    assert.equal(a.strings.get("out"), "x");
    assert.equal(a.strings.get("thug2"), "C:\\Games\\THUG2\\Game");
    assert.equal(a.flags.get("no-scripts"), true);
    assert.deepEqual(a.lists.get("extra"), []);
    const b = parse("--thug2=C:\\G", "selfcheck", "--extra", "a.prx", "b.prx", "--no");
    assert.equal(b.command, "selfcheck");
    assert.equal(b.strings.get("thug2"), "C:\\G");
    assert.equal(b.strings.get("out"), undefined);
    assert.deepEqual(b.lists.get("extra"), ["a.prx", "b.prx"]);
    assert.equal(b.flags.get("no-scripts"), true); // (a unique prefix, as argparse)
});

test("converter args: errors exit 2", () => {
    const stderr = process.stderr.write;
    process.stderr.write = () => true;
    try {
        for (const argv of [["build"], [], ["convert", "--bogus"], ["convert", "--out"]]) {
            assert.throws(
                () => parse(...argv),
                (e: unknown) => e instanceof SystemExit && e.code === 2,
            );
        }
    } finally {
        process.stderr.write = stderr;
    }
});
