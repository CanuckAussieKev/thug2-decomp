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
import { CallGraph, sourceFiles as cppSources } from "./sim_draw_calls.ts";

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

// The drawing calls nothing that runs scripts - through any chain of calls (tools/sim_draw_calls.ts): the time of day's
// clock once ran the level's time-of-day scripts from draw_world (render_pc -> engine/gfx/day_clock -> tod_manager's
// set_tod_instant -> refresh_all -> spawn_script); it is engine::render_frame's step now (day_clock::step). The drawing
// is every function in render_pc and gpu_vulkan but Device::step_frame (render_pc's part of the step: render_frame runs
// it on every step, drawn or not).
const STEP_PARTS = new Set(["src/render_pc/draw_frame.cpp step_frame"]);

test("the drawing calls nothing that runs scripts", () => {
    const graph = new CallGraph(cppSources(ROOT));
    const drawing = graph.functions.filter(
        (def) => /^src\/(render_pc|gpu_vulkan)\//.test(def.file) && !STEP_PARTS.has(`${def.file} ${def.name}`),
    );
    assert.ok(
        drawing.some((def) => def.name === "draw_world"),
        "draw_world found",
    );
    // (the chain the check exists for: the clock's step reaches a script)
    const clock = graph.functions.find((def) => def.file === "src/engine/gfx/day_clock.cpp" && def.name === "step");
    assert.ok(clock !== undefined && graph.runsScripts(clock), "day_clock::step runs scripts (set_tod_instant)");
    assert.deepEqual(graph.callsIntoScripts(drawing), []);
});

test("a chain of calls into a script is found", () => {
    const graph = new CallGraph([
        {
            name: "src/render_pc/draw.cpp",
            text: "namespace render {\nvoid draw_world()\n{\n    day_clock::frame();\n}\n} // namespace render\n",
        },
        {
            name: "src/engine/gfx/day_clock.cpp",
            text:
                "namespace render::day_clock {\nnamespace {\nvoid apply(gfx::Manager& manager)\n{\n" +
                "    manager.set_state(1);\n}\n} // namespace\nvoid frame()\n{\n    apply(*manager());\n}\n" +
                "} // namespace render::day_clock\n",
        },
        {
            name: "src/engine/gfx/manager.cpp",
            text:
                "namespace gfx {\nvoid Manager::set_state(int state)\n{\n    refresh(state);\n}\n" +
                "void Manager::refresh(int state)\n{\n    script::spawn_script(kTransition, nullptr);\n}\n" +
                "} // namespace gfx\n",
        },
    ]);
    const drawing = graph.functions.filter((def) => def.file.startsWith("src/render_pc/"));
    assert.deepEqual(graph.callsIntoScripts(drawing), ["src/render_pc/draw.cpp:4 draw_world -> day_clock::frame"]);
});
