// NOT ORIGINAL - same assertions also run inside embedded quickjs-ng.
import { readFileSync } from "node:fs";
import { test } from "node:test";
import { runInNewContext } from "node:vm";
import { helperSource } from "./helper-sources.ts";
import { reviewBundle } from "./review-fixtures.ts";

test("review regressions: optional values, script symbols, angles, linear fields, precision and fuel", () => {
    runInNewContext(
        helperSource("random") +
            helperSource("values") +
            readFileSync(new URL("../../scripting/runtime.js", import.meta.url), "utf8") +
            reviewBundle(),
        {},
        { timeout: 5000 },
    );
});
