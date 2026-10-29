// NOT ORIGINAL - synthetic tooling tests, never the game's helper body.
import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { createHash } from "node:crypto";
import { linkSync, mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { dirname, join } from "node:path";
import { test } from "node:test";
import { fileURLToPath } from "node:url";
import { command, field, integer, name, script } from "./fixtures.ts";
import { helperModule, helperName, prepareHelper } from "./real-helper.ts";
import { checksum } from "./translate.ts";

// Intentionally a single call, not a reconstruction of ResetLevelFlags.
const synthetic = (body = command("UnSetFlag", field("flag", integer(7)))) =>
    Uint8Array.from([...script(helperName, body), 0]);
test("local helper preparation inventories dependencies and fingerprints raw bytes", () => {
    const bytes = synthetic(),
        result = prepareHelper(bytes);
    assert.equal(result.metadata.crc, checksum(helperName));
    assert.equal(result.metadata.sha256, createHash("sha256").update(bytes).digest("hex"));
    assert.deepEqual(result.metadata.hosts, ["UnSetFlag"]);
    assert.equal(result.metadata.requiresRandom, false);
    assert.match(result.javascript, /runtimeVersion/);
    assert.match(result.javascript, /"format":2/);
    assert.equal(result.javascript, prepareHelper(bytes).javascript);
});
test("unsupported helper dependencies and missing scripts fail explicitly", () => {
    assert.throws(() => prepareHelper(Uint8Array.of(0)), /Missing nonempty/);
    assert.throws(() => prepareHelper(synthetic([])), /Missing nonempty/);
    assert.throws(() => prepareHelper(synthetic(command("Unknown"))), /dependencies differ/);
    assert.throws(
        () => prepareHelper(synthetic(command("UnSetFlag", field("flag", [14, ...name("external"), 15])))),
        /dependencies differ/,
    );
    assert.throws(() => prepareHelper(Uint8Array.of(0x43, 0)), /QB offset/);
});
test("real-helper CLI preserves QB and previous output on unsupported input", () => {
    const root = mkdtempSync(join(tmpdir(), "thug2-real-helper-"));
    try {
        const input = join(root, helperModule),
            output = join(root, "helper.js"),
            alias = join(root, "alias.qb");
        mkdirSync(dirname(input), { recursive: true });
        const bytes = synthetic();
        writeFileSync(input, bytes);
        const cli = fileURLToPath(new URL("./real-helper.ts", import.meta.url));
        const run = (target: string) => execFileSync(process.execPath, [cli, root, target], { stdio: "pipe" });
        const report = JSON.parse(run(output).toString());
        assert.equal(report.module, helperModule);
        const first = readFileSync(output, "utf8");
        assert.throws(() => run(input), /must differ/);
        linkSync(input, alias);
        assert.throws(() => run(alias), /must differ/);
        assert.deepEqual(readFileSync(input), Buffer.from(bytes));
        writeFileSync(input, Buffer.from([0x23]));
        assert.throws(() => run(output), /truncated/);
        assert.equal(readFileSync(output, "utf8"), first);
    } finally {
        rmSync(root, { recursive: true, force: true });
    }
});
