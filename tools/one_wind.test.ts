// NOT ORIGINAL - round W's rule (docs/design/vulkan.md "Round W: the air and the wind"): one wind. Everything that moves
// with the air (water, grass, clutter, falling snow, rain, clouds) reads src/engine/gfx/wind.h and the level's air transport
// (engine/gfx/air_volume.h); no file declares a wind speed, direction or gust of its own. A constant whose name speaks of
// the wind or its gusts outside wind.h fails unless it is listed below with what it is (a clock's wrap, the volume's own
// storage, a physical threshold of the waves' growth law), and no shader writes a downwind direction of its own.

import assert from "node:assert/strict";
import { readdirSync, readFileSync } from "node:fs";
import { join, relative } from "node:path";
import { test } from "node:test";

const ROOT = join(import.meta.dirname, "..");

// name -> why it is not a wind of its own
const ALLOWED = new Map<string, string>([
    ["kWindClockWraps", "a clock kept small (seconds)"],
    ["kGustWraps", "a clock kept small (seconds)"],
    ["kWindScale", "the air volume's storage (a byte's scale)"],
    ["kLeastWind", "the air volume's search: a draught too weak to follow"],
    ["kOnsetWind", "the waves' growth law: no waves below it (Kahma and Donelan)"],
    ["kWind", "the water check's test winds (m/s)"],
    ["kOptWaterWind", "the Options screen's text"],
    ["kOptWindHelp", "the Options screen's text"],
]);

function sourceFiles(folder: string, out: string[] = []): string[] {
    for (const entry of readdirSync(folder, { withFileTypes: true })) {
        const path = join(folder, entry.name);
        if (entry.isDirectory()) sourceFiles(path, out);
        else if (/\.(h|cpp)$/.test(entry.name)) out.push(path);
    }
    return out;
}

test("one wind: no wind constant outside engine/gfx/wind.h", () => {
    const found: string[] = [];
    const constant = /constexpr[^=;(]*\b(k\w*(?:Wind|Gust)\w*)\b\s*(?:=|\{)/g;
    for (const path of sourceFiles(join(ROOT, "src"))) {
        const name = relative(ROOT, path).replaceAll("\\", "/");
        if (name === "src/engine/gfx/wind.h") continue;
        const text = readFileSync(path, "utf8");
        for (const match of text.matchAll(constant)) {
            const id = match[1] ?? "";
            if (/Window/.test(id) || ALLOWED.has(id)) continue;
            found.push(`${name}: ${id}`);
        }
        if (/vec2\(0\.8, 0\.6\)/.test(text)) found.push(`${name}: a downwind direction of its own in a shader`);
    }
    assert.deepEqual(found, []);
});
