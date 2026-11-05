// NOT ORIGINAL - the project's rule for clang-tidy (docs/style-guide.md "Exceptions"): no NOLINT anywhere in src/. A
// check that can't hold is changed in the code, or configured / switched off in a .clang-tidy with its reason.

import assert from "node:assert/strict";
import { readdirSync, readFileSync } from "node:fs";
import { join, relative } from "node:path";
import { test } from "node:test";

const ROOT = join(import.meta.dirname, "..");

function sourceFiles(folder: string, out: string[] = []): string[] {
    for (const entry of readdirSync(folder, { withFileTypes: true })) {
        const path = join(folder, entry.name);
        if (entry.isDirectory()) sourceFiles(path, out);
        else out.push(path);
    }
    return out;
}

test("no NOLINT under src/", () => {
    const found: string[] = [];
    for (const path of sourceFiles(join(ROOT, "src"))) {
        const lines = readFileSync(path, "utf8").split("\n");
        lines.forEach((line, index) => {
            if (line.includes("NOLINT")) found.push(`${relative(ROOT, path).replaceAll("\\", "/")}:${index + 1}`);
        });
    }
    assert.deepEqual(found, []);
});
