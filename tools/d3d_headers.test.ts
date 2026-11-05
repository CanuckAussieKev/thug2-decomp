// NOT ORIGINAL - where Direct3D's headers may be seen (docs/architecture.md "The graphics backend"): a header under src/ includes
// <d3d9.h> / <d3dx9*.h> - itself or through another of the project's headers - only if it is one of Direct3D's own
// headers below. Every other header holds the graphics handles (engine/gfx/gpu_handles.h), so a file that doesn't talk to
// Direct3D never sees its API.

import assert from "node:assert/strict";
import { readdirSync, readFileSync } from "node:fs";
import { join, relative } from "node:path";
import { test } from "node:test";

const ROOT = join(import.meta.dirname, "..");
const SRC = join(ROOT, "src");

// Direct3D's own headers: the files that talk to Direct3D include them.
const DIRECT3D_HEADERS = new Set([
    "render_d3d9/d3d9_state.h",
    "render_d3d9/device_d3d.h",
    "render_d3d9/pc_d3d_resources.h",
    "render_d3d9/d3d9_handles.h",
    "render_d3d9/d3d_matrix.h",
    "render_d3d9/d3dx_jpg.h",
    "render_d3d9/imgui_texture_id.h",
]);

const SDK_HEADER = /^\s*#\s*include\s*<(d3d9|d3d9types|d3d9caps|d3dx9\w*)\.h>/m;
const PROJECT_INCLUDE = /^\s*#\s*include\s*"([^"]+)"/gm;

function headers(folder: string, out: string[] = []): string[] {
    for (const entry of readdirSync(folder, { withFileTypes: true })) {
        const path = join(folder, entry.name);
        if (entry.isDirectory()) headers(path, out);
        else if (entry.name.endsWith(".h")) out.push(relative(SRC, path).replaceAll("\\", "/"));
    }
    return out;
}

test("only Direct3D's own headers see <d3d9.h>", () => {
    const all = headers(SRC);
    const known = new Set(all);
    const text = new Map(all.map((name) => [name, readFileSync(join(SRC, name), "utf8")]));
    const sees = new Map<string, boolean>();
    const visit = (name: string, path: Set<string>): boolean => {
        const done = sees.get(name);
        if (done !== undefined) return done;
        if (path.has(name)) return false;
        path.add(name);
        const source = text.get(name) ?? "";
        let result = SDK_HEADER.test(source);
        for (const match of source.matchAll(PROJECT_INCLUDE)) {
            const included = match[1] ?? "";
            if (known.has(included) && visit(included, path)) result = true;
        }
        path.delete(name);
        sees.set(name, result);
        return result;
    };
    const outside = all.filter((name) => visit(name, new Set()) && !DIRECT3D_HEADERS.has(name)).sort();
    assert.deepEqual(outside, []);
    for (const name of DIRECT3D_HEADERS) assert.ok(known.has(name), `${name} is listed but missing`);
});

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

// The graphics backend is chosen in one place: app/ installs Direct3D 9's render::Device (docs/architecture.md, "The
// graphics backend"); the engine side reaches it only through render::device().
test("only app/ picks the Direct3D 9 device", () => {
    const pickers = including(/^\s*#\s*include\s*"render_d3d9\/d3d9_device\.h"/m);
    const outside = pickers.filter((name) => !name.startsWith("app/") && name !== "render_d3d9/d3d9_device.cpp");
    assert.deepEqual(outside, []);
    assert.ok(pickers.includes("app/renderer_choice.cpp"), "app/renderer_choice.cpp installs the device");
});

// The files of the PC layer that reach the graphics API only through render::Gpu: none includes a Direct3D header
// (the API-neutral PC layer: V5a the render-state cache, V5b the meshes' buffers, V5c the effects).
const THROUGH_GPU = [
    "render_d3d9/render_state.cpp",
    "render_d3d9/primitive_states.cpp",
    "render_d3d9/vertex_buffers.cpp",
    "render_d3d9/pc_buffers.cpp",
    "render_d3d9/mesh_list.cpp",
    "render_d3d9/sub_mesh.cpp",
    "render_d3d9/pc_mesh_life.cpp",
    "render_d3d9/pc_mesh_render.cpp",
    "render_d3d9/pc_material.cpp",
    "render_d3d9/pc_instance.cpp",
    "render_d3d9/pixel_shaders.cpp",
    "render_d3d9/pc_mesh_load.cpp",
    "render_d3d9/pc_flexible_particles.cpp",
    "render_d3d9/pc_particles.cpp",
    "render_d3d9/pc_billboards.cpp",
    "render_d3d9/billboard_quad.cpp",
    "render_d3d9/pc_weather.cpp",
    "render_d3d9/pc_grass.cpp",
    "render_d3d9/glow.cpp",
    "render_d3d9/texture_splats.cpp",
    "render_d3d9/shatter.cpp",
    "render_d3d9/pc_fog.cpp",
    "render_d3d9/pc_gamma.cpp",
    "render_d3d9/wireframe.cpp",
    "render_d3d9/flash.cpp",
    "render_d3d9/pc_viewport.cpp",
    "render_d3d9/pc_texture_projection.cpp",
    "render_d3d9/shadow_base.cpp",
    "render_d3d9/screen_fx.cpp",
    "render_d3d9/draw_frame.cpp",
];

// render::Gpu (engine/gfx/gpu.h) is the low interface the PC layer calls in place of the device: app/ installs
// Direct3D 9's with the device, and the files above reach Direct3D only through it.
test("only app/ picks the Direct3D 9 Gpu; the files through the Gpu see no Direct3D header", () => {
    const pickers = including(/^\s*#\s*include\s*"render_d3d9\/gpu_d3d9\.h"/m);
    const outside = pickers.filter((name) => !name.startsWith("app/") && name !== "render_d3d9/gpu_d3d9.cpp");
    assert.deepEqual(outside, []);
    assert.ok(pickers.includes("app/renderer_choice.cpp"), "app/renderer_choice.cpp installs the Gpu");
    for (const name of THROUGH_GPU) {
        const source = readFileSync(join(SRC, name), "utf8");
        assert.ok(!SDK_HEADER.test(source), `${name} includes a Direct3D SDK header`);
        for (const match of source.matchAll(PROJECT_INCLUDE)) {
            assert.ok(!DIRECT3D_HEADERS.has(match[1] ?? ""), `${name} includes ${match[1] ?? ""}`);
        }
    }
});

// Dear ImGui's platform and renderer backends are the graphics backend's (render::UiBackend, engine/gfx/ui_backend.h):
// only render_d3d9/ includes their headers, and only app/ picks Direct3D 9's UI backend.
test("only render_d3d9/ includes ImGui's backends; only app/ picks the UI backend", () => {
    const backends = including(/^\s*#\s*include\s*"backends\/imgui_impl_\w+\.h"/m);
    assert.deepEqual(
        backends.filter((name) => !name.startsWith("render_d3d9/")),
        [],
    );
    const pickers = including(/^\s*#\s*include\s*"render_d3d9\/d3d9_ui_backend\.h"/m);
    const outside = pickers.filter((name) => !name.startsWith("app/") && name !== "render_d3d9/d3d9_ui_backend.cpp");
    assert.deepEqual(outside, []);
    assert.ok(pickers.includes("app/renderer_choice.cpp"), "app/renderer_choice.cpp installs the UI backend");
});

// D3DX9 is the Direct3D 9 backend's alone (its shader assembly and JPEG writer; its maths is plain C++,
// base/maths/view_transforms.h) - and the maths check's reference.
test("only render_d3d9/ (and the maths check) includes D3DX", () => {
    const users = including(/^\s*#\s*include\s*<d3dx9\w*\.h>/m);
    assert.deepEqual(
        users.filter((name) => !name.startsWith("render_d3d9/") && name !== "tests/maths_check_d3dx.cpp"),
        [],
    );
    assert.ok(users.includes("render_d3d9/d3dx.cpp"), "render_d3d9/d3dx.cpp calls D3DX");
});
