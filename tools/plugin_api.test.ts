// NOT ORIGINAL - where the engine API's marks may be (base/core/api.h, tools/plugin_api.ts): THUG2_API only on
// declarations in the headers of the layers a plugin may include (the level editor's: the game and below), never in a
// source file, the plugin's own folder or a layer above the game - what the exe exports is what the plugin sees. The
// build checks the other half (the exe exports exactly what the plugin imports: plugin_api.ts, after the plugin's link).

import assert from "node:assert/strict";
import { readdirSync, readFileSync } from "node:fs";
import { join, relative } from "node:path";
import { test } from "node:test";

const ROOT = join(import.meta.dirname, "..");
const SRC = join(ROOT, "src");
const API_LAYERS = ["base/", "platform/", "formats/", "wire/", "engine/", "game/"];

function files(folder: string, out: string[] = []): string[] {
    for (const entry of readdirSync(folder, { withFileTypes: true })) {
        const path = join(folder, entry.name);
        if (entry.isDirectory()) files(path, out);
        else if (/\.(cpp|h|inc)$/.test(entry.name)) out.push(relative(SRC, path).replaceAll("\\", "/"));
    }
    return out;
}

test("THUG2_API marks only the headers of the game and the layers below it", () => {
    const misplaced = files(SRC)
        .filter((name) => name !== "base/core/api.h" && /\bTHUG2_API\b/.test(readFileSync(join(SRC, name), "utf8")))
        .filter((name) => !name.endsWith(".h") || !API_LAYERS.some((layer) => name.startsWith(layer)))
        .sort();
    assert.deepEqual(misplaced, []);
});
