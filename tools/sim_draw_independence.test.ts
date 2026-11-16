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

// The drawing starts no script: a script changes the game's state (its records, the screen elements the game's scripts
// ask about, the heap), and the drawing runs only on drawn steps - and the lens flare's on the sun's occlusion query, a
// GPU answer a frame or more late. The original's sun launched MaybeHideLensFlare / CreateLensFlare from the drawing;
// they are sun::step's now (engine/gfx/lens_flare.h), run by render_frame on every step.
const SCRIPT_START = /\b(launch_script|spawn_script|run_script_once|run_script)\s*\(/;

test("the drawing starts no script", () => {
    const found: string[] = [];
    for (const folder of ["src/render_pc", "src/gpu_vulkan"]) {
        for (const path of sourceFiles(join(ROOT, folder))) {
            const name = relative(ROOT, path).replaceAll("\\", "/");
            readFileSync(path, "utf8")
                .split(/\r?\n/)
                .forEach((line, index) => {
                    if (SCRIPT_START.test(line.replace(/\/\/.*$/, ""))) found.push(`${name}:${index + 1}`);
                });
        }
    }
    // lens_flare.cpp: only sun::step starts scripts (the function a line is in: the last definition begun at column 0)
    const flare = "src/engine/gfx/lens_flare.cpp";
    let current = "";
    readFileSync(join(ROOT, flare), "utf8")
        .split(/\r?\n/)
        .forEach((line, index) => {
            const code = line.replace(/\/\/.*$/, "");
            const definition = /^[A-Za-z].*?\b(\w+)\s*\([^;]*$/.exec(code);
            if (definition?.[1] !== undefined) current = definition[1];
            if (SCRIPT_START.test(code) && current !== "step") found.push(`${flare}:${index + 1} (${current})`);
        });
    assert.deepEqual(found, []);
});
