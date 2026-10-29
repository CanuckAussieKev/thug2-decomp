// NOT ORIGINAL - generated format-2 execution with real value helpers.
import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { linkSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { test } from "node:test";
import { fileURLToPath } from "node:url";
import { runInNewContext } from "node:vm";
import { emitIR, translateIR } from "./emitter.ts";
import { emittedFixture, executionBundle } from "./emitter-fixtures.ts";
import { field, integer, script } from "./fixtures.ts";

const values = readFileSync(new URL("../../scripting/values.js", import.meta.url), "utf8");
test("format-2 generators execute values, loops, grouped switches, calls and yield/resume", () => {
    runInNewContext(executionBundle(values), {}, { timeout: 5000 });
});
test("emission is deterministic and scalar runtime rejects format 2 before registration", () => {
    const output = emittedFixture();
    assert.equal(output, emittedFixture());
    const runtime = readFileSync(new URL("../../scripting/runtime.js", import.meta.url), "utf8");
    assert.throws(() => runInNewContext(runtime + output, {}, { timeout: 1000 }), /Unsupported QB module format/);
    assert.throws(() => runInNewContext(output, { qb: { module() {} } }), /QB values API 2 required/);
});
test("invalid lexical break targets fail emission with source offset", () => {
    assert.throws(
        () =>
            emitIR({
                format: 2,
                source: "bad.qb",
                definitions: [
                    {
                        kind: "script",
                        name: 1,
                        offset: 0,
                        defaults: [],
                        body: [{ kind: "break", offset: 7, target: 3 }],
                    },
                ],
            }),
        /bad.qb: QB offset 0x7/,
    );
});
test("repeat argument code is shared across break sites instead of multiplying output", () => {
    const breaks = Array.from({ length: 128 }, () => [0x22, 1]).flat();
    const fields = Array.from({ length: 256 }, (_, n) => field(`p${n}`, integer(n))).flat();
    const bytes = Uint8Array.from([...script("size", [0x20, 1, ...breaks, 0x21, ...fields, 1]), 0]);
    const output = translateIR(bytes, "size.qb");
    assert.equal(output.match(/const loop0Args/g)?.length, 1);
    assert.equal(output.match(/loop0Args\(\[\]\)/g)?.length, 128);
    assert.ok(output.length < 200000);
});
test("format-2 CLI is deterministic and protects input and previous output on failure", () => {
    const dir = mkdtempSync(join(tmpdir(), "thug2-emitter-"));
    try {
        const input = join(dir, "input.qb"),
            output = join(dir, "output.js"),
            alias = join(dir, "alias.qb");
        const bytes = Buffer.from([...script("test", [...field("x", integer(1)), 1]), 0]);
        writeFileSync(input, bytes);
        const cli = fileURLToPath(new URL("./emitter.ts", import.meta.url));
        const run = (target: string) => execFileSync(process.execPath, [cli, input, target], { stdio: "pipe" });
        run(output);
        const first = readFileSync(output, "utf8");
        run(output);
        assert.equal(readFileSync(output, "utf8"), first);
        assert.match(first, /"format":2/);
        assert.throws(() => run(input), /must differ/);
        linkSync(input, alias);
        assert.throws(() => run(alias), /must differ/);
        assert.deepEqual(readFileSync(input), bytes);
        writeFileSync(input, Buffer.from([0x23]));
        assert.throws(() => run(output), /truncated/);
        assert.equal(readFileSync(output, "utf8"), first);
    } finally {
        rmSync(dir, { recursive: true, force: true });
    }
});
