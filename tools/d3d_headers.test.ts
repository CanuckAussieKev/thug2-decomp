// NOT ORIGINAL - where Direct3D's headers may be seen (docs/architecture.md "gpu/"): a header under src/ includes
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
    "platform/d3dx_jpg.h",
    "platform/imgui_texture_id.h",
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

// The graphics backend is chosen in one place: app/ installs Direct3D 9's render::Device (docs/architecture.md, "The
// graphics backend"); the engine side reaches it only through render::device().
test("only app/ picks the Direct3D 9 device", () => {
    const pickers: string[] = [];
    const walk = (folder: string): void => {
        for (const entry of readdirSync(folder, { withFileTypes: true })) {
            const path = join(folder, entry.name);
            if (entry.isDirectory()) walk(path);
            else if (/\.(cpp|h)$/.test(entry.name)) {
                const name = relative(SRC, path).replaceAll("\\", "/");
                if (/^\s*#\s*include\s*"render_d3d9\/d3d9_device\.h"/m.test(readFileSync(path, "utf8")))
                    pickers.push(name);
            }
        }
    };
    walk(SRC);
    const outside = pickers.filter((name) => !name.startsWith("app/") && name !== "render_d3d9/d3d9_device.cpp").sort();
    assert.deepEqual(outside, []);
    assert.ok(pickers.includes("app/main.cpp"), "app/main.cpp installs the device");
});
