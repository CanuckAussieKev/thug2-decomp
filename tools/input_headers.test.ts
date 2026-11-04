// NOT ORIGINAL - where the input SDKs may be seen (docs/architecture.md "The input seam"): only the DirectInput
// backend (src/input_dinput) includes <dinput.h>, and only app/ picks its input::Source. The engine, the game and the
// rest reach DirectInput through the input sources (engine/pad_input/input_source.h).

import assert from "node:assert/strict";
import { readdirSync, readFileSync } from "node:fs";
import { join, relative } from "node:path";
import { test } from "node:test";

const SRC = join(import.meta.dirname, "..", "src");

// The source files (relative to src/, / separators) whose text matches `pattern`.
function including(pattern: RegExp): string[] {
    const found: string[] = [];
    const walk = (folder: string): void => {
        for (const entry of readdirSync(folder, { withFileTypes: true })) {
            const path = join(folder, entry.name);
            if (entry.isDirectory()) walk(path);
            else if (/\.(cpp|h)$/.test(entry.name) && pattern.test(readFileSync(path, "utf8")))
                found.push(relative(SRC, path).replaceAll("\\", "/"));
        }
    };
    walk(SRC);
    return found.sort();
}

test("only input_dinput/ includes <dinput.h>", () => {
    const users = including(/^\s*#\s*include\s*<dinput\.h>/m);
    assert.deepEqual(
        users.filter((name) => !name.startsWith("input_dinput/")),
        [],
    );
    assert.ok(users.includes("input_dinput/direct_input.h"), "input_dinput/direct_input.h declares DirectInput's side");
});

test("only app/ picks the DirectInput source", () => {
    const pickers = including(/^\s*#\s*include\s*"input_dinput\/dinput_source\.h"/m);
    const outside = pickers.filter((name) => !name.startsWith("app/") && name !== "input_dinput/dinput_source.cpp");
    assert.deepEqual(outside, []);
    assert.ok(pickers.includes("app/main.cpp"), "app/main.cpp adds the source");
});
