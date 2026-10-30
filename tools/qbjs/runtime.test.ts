// NOT ORIGINAL - real runtime JS, real values and generated format-2 scripts.
import assert from "node:assert/strict";
import { test } from "node:test";
import { runInNewContext } from "node:vm";
import { nativeRuntimeBundle, runtimeAssertions } from "./runtime-fixtures.ts";

test("format-2 runs on the product JS scheduler with values, aliases, waits and faults", () => {
    runInNewContext(nativeRuntimeBundle() + runtimeAssertions(), {}, { timeout: 5000 });
});
test("format-2 manifests check exact helper requirements", () => {
    const context = runInNewContext(nativeRuntimeBundle() + "globalThis");
    assert.throws(() => context.qb.module({ format: 3 }), /Unsupported QB module format/);
    for (const requires of [undefined, {}, { values: 1 }, { values: 2, future: 1 }, { values: 2, random: 2 }])
        assert.throws(() => context.qb.module({ format: 2, requires }), /Unsupported QB module requirements/);
});
