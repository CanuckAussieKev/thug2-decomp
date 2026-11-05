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
const DIRECT3D_HEADERS = new Set(["gpu_d3d9/d3d9_state.h", "gpu_d3d9/d3d9_handles.h", "gpu_d3d9/imgui_texture_id.h"]);

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
    const pickers = including(/^\s*#\s*include\s*"render_pc\/d3d9_device\.h"/m);
    const outside = pickers.filter((name) => !name.startsWith("app/") && name !== "render_pc/d3d9_device.cpp");
    assert.deepEqual(outside, []);
    assert.ok(pickers.includes("app/renderer_choice.cpp"), "app/renderer_choice.cpp installs the device");
});

// Every file of the PC layer (src/render_pc) reaches the graphics API only through render::Gpu / render::GpuContext: none
// includes a Direct3D SDK header or one of the Direct3D library's headers (src/gpu_d3d9), however indirectly.
function sources(folder: string, out: string[] = []): string[] {
    for (const entry of readdirSync(folder, { withFileTypes: true })) {
        const path = join(folder, entry.name);
        if (entry.isDirectory()) sources(path, out);
        else if (/\.(cpp|h)$/.test(entry.name)) out.push(relative(SRC, path).replaceAll("\\", "/"));
    }
    return out.sort();
}
const RENDER_PC = sources(join(SRC, "render_pc"));

// render::Gpu (engine/gfx/gpu.h) is the low interface the PC layer calls in place of the device: app/ installs
// Direct3D 9's with the device, and the files above reach Direct3D only through it.
test("only app/ picks the Direct3D 9 Gpu; render_pc sees no Direct3D header", () => {
    const pickers = including(/^\s*#\s*include\s*"gpu_d3d9\/gpu_d3d9\.h"/m);
    const outside = pickers.filter((name) => !name.startsWith("app/") && name !== "gpu_d3d9/gpu_d3d9.cpp");
    assert.deepEqual(outside, []);
    assert.ok(pickers.includes("app/renderer_choice.cpp"), "app/renderer_choice.cpp installs the Gpu");
    assert.ok(RENDER_PC.length > 100, "render_pc's files are found");
    for (const name of RENDER_PC) {
        const source = readFileSync(join(SRC, name), "utf8");
        assert.ok(!SDK_HEADER.test(source), `${name} includes a Direct3D SDK header`);
        for (const match of source.matchAll(PROJECT_INCLUDE)) {
            const included = match[1] ?? "";
            assert.ok(!DIRECT3D_HEADERS.has(included), `${name} includes ${included}`);
            assert.ok(!included.startsWith("gpu_d3d9/"), `${name} includes ${included}`);
        }
    }
});

// render::GpuContext (engine/gfx/gpu_context.h) makes and resets the device (the instance, the device, the present
// parameters as API-neutral settings, the caps): app/ installs Direct3D 9's with the Gpu.
test("only app/ picks the Direct3D 9 GpuContext", () => {
    const pickers = including(/^\s*#\s*include\s*"gpu_d3d9\/d3d9_context\.h"/m);
    const outside = pickers.filter((name) => !name.startsWith("app/") && name !== "gpu_d3d9/d3d9_context.cpp");
    assert.deepEqual(outside, []);
    assert.ok(pickers.includes("app/renderer_choice.cpp"), "app/renderer_choice.cpp installs the GpuContext");
});

// Dear ImGui's platform and renderer backends are the graphics backend's (render::UiBackend, engine/gfx/ui_backend.h):
// only gpu_d3d9/ includes their headers, and only app/ picks Direct3D 9's UI backend.
test("only gpu_d3d9/ includes ImGui's backends; only app/ picks the UI backend", () => {
    const backends = including(/^\s*#\s*include\s*"backends\/imgui_impl_\w+\.h"/m);
    assert.deepEqual(
        backends.filter((name) => !name.startsWith("gpu_d3d9/")),
        [],
    );
    const pickers = including(/^\s*#\s*include\s*"gpu_d3d9\/d3d9_ui_backend\.h"/m);
    const outside = pickers.filter((name) => !name.startsWith("app/") && name !== "gpu_d3d9/d3d9_ui_backend.cpp");
    assert.deepEqual(outside, []);
    assert.ok(pickers.includes("app/renderer_choice.cpp"), "app/renderer_choice.cpp installs the UI backend");
});

// D3DX9 is the Direct3D 9 backend's alone (its shader assembly and JPEG writer; its maths is plain C++,
// base/maths/view_transforms.h) - and the maths check's reference.
test("only gpu_d3d9/ (and the maths check) includes D3DX", () => {
    const users = including(/^\s*#\s*include\s*<d3dx9\w*\.h>/m);
    assert.deepEqual(
        users.filter((name) => !name.startsWith("gpu_d3d9/") && name !== "tests/maths_check_d3dx.cpp"),
        [],
    );
    assert.ok(users.includes("gpu_d3d9/d3dx.cpp"), "gpu_d3d9/d3dx.cpp calls D3DX");
});

// Only the Direct3D 9 Gpu library (src/gpu_d3d9) includes a Direct3D SDK header (the maths check's reference excepted);
// render_pc has none and includes none of gpu_d3d9's own headers (the previous test).
test("only gpu_d3d9/ includes <d3d9.h> or <d3dx9*.h>", () => {
    const users = including(SDK_HEADER);
    assert.deepEqual(
        users.filter((name) => !name.startsWith("gpu_d3d9/") && name !== "tests/maths_check_d3dx.cpp"),
        [],
    );
    assert.ok(users.includes("gpu_d3d9/gpu_d3d9.cpp"), "gpu_d3d9/gpu_d3d9.cpp calls Direct3D");
});
