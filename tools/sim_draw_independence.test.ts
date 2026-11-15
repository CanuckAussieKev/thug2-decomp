// NOT ORIGINAL - the simulation doesn't depend on whether a step's frame is drawn (docs/design/simulation-rate.md
// "Determinism under load"): whether the frame is drawn (engine_parts' suppress_draw) is read only by the main loop,
// the render's frame (engine/gfx/engine.cpp render_frame) and the drawing itself (render_pc/, gpu_vulkan/). The
// original's game code read it too - the animation blend skipped its pose, the vehicles their bones - and a late,
// undrawn step changed the game; a new reader on the game's side must not come back. (What draw_world did to the game's
// state is done on every step in render_frame; tools/game/sim_check.ts --load proves the runs equal.)

import assert from "node:assert/strict";
import { readdirSync, readFileSync } from "node:fs";
import { join, relative } from "node:path";
import { test } from "node:test";

const ROOT = join(import.meta.dirname, "..");

// Where the drawn state may be read: the loop that decides it, the frame that draws, the drawing.
const ALLOWED = [
    /^src\/engine\/sys\/mlp\.cpp$/,
    /^src\/engine\/gfx\/engine\.cpp$/,
    /^src\/engine\/gfx\/engine_parts\.h$/,
    /^src\/render_pc\//,
    /^src\/gpu_vulkan\//,
];

function sourceFiles(folder: string, out: string[] = []): string[] {
    for (const entry of readdirSync(folder, { withFileTypes: true })) {
        const path = join(folder, entry.name);
        if (entry.isDirectory()) sourceFiles(path, out);
        else if (/\.(cpp|h)$/.test(entry.name)) out.push(path);
    }
    return out;
}

test("whether a frame is drawn is read only by the loop and the drawing", () => {
    const found: string[] = [];
    for (const path of sourceFiles(join(ROOT, "src"))) {
        const name = relative(ROOT, path).replaceAll("\\", "/");
        if (ALLOWED.some((allowed) => allowed.test(name))) continue;
        const lines = readFileSync(path, "utf8").split(/\r?\n/);
        lines.forEach((line, index) => {
            const code = line.replace(/\/\/.*$/, "");
            if (/\bsuppress_draw\(\)/.test(code)) found.push(`${name}:${index + 1}`);
        });
    }
    assert.deepEqual(found, []);
});
