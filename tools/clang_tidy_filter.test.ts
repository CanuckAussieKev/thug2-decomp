// NOT ORIGINAL - .clang-tidy's header filter covers every folder of src/: a header the filter leaves out is never
// reported, so a folder added under src/ and missed by the filter would pass the checks unchecked. The fetched
// libraries and the build's own headers stay out.

import assert from "node:assert/strict";
import { readdirSync, readFileSync } from "node:fs";
import { join } from "node:path";
import { test } from "node:test";

const ROOT = join(import.meta.dirname, "..");

// A top-level `Key: 'value'` of the .clang-tidy file (single-quoted YAML: '' is a quote).
function option(text: string, key: string): RegExp {
    const match = new RegExp(`^${key}: '((?:[^']|'')*)'\\s*$`, "m").exec(text);
    const value = match?.[1];
    assert.ok(value !== undefined, `.clang-tidy has no ${key}`);
    return new RegExp(value.replaceAll("''", "'"));
}

const config = readFileSync(join(ROOT, ".clang-tidy"), "utf8");
const include = option(config, "HeaderFilterRegex");
const exclude = option(config, "ExcludeHeaderFilterRegex");

function reported(path: string): boolean {
    return include.test(path) && !exclude.test(path);
}

function folders(folder: string, out: string[] = []): string[] {
    for (const entry of readdirSync(folder, { withFileTypes: true })) {
        if (!entry.isDirectory()) continue;
        const path = join(folder, entry.name);
        out.push(path);
        folders(path, out);
    }
    return out;
}

test("every folder of src/ is in the header filter", () => {
    const missed: string[] = [];
    for (const folder of folders(join(ROOT, "src"))) {
        // Both spellings: clang-tidy is given Windows paths on Windows.
        for (const path of [join(folder, "x.h"), join(folder, "x.h").replaceAll("\\", "/")]) {
            if (!reported(path)) missed.push(path);
        }
    }
    assert.deepEqual(missed, []);
});

test("the fetched libraries and the build's headers stay out", () => {
    const build = join(ROOT, "out", "build", "verify");
    for (const path of [
        join(build, "_deps", "sdl3-src", "include", "SDL3", "SDL.h"),
        join(build, "_deps", "gamespy_sdk-src", "src", "x.h"),
        join(build, "generated", "build_info.h"),
        join(build, "editor_fonts", "inter.inc"),
    ]) {
        assert.equal(reported(path), false, path);
        assert.equal(reported(path.replaceAll("\\", "/")), false, path);
    }
});
