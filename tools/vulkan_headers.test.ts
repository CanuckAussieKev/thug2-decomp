// NOT ORIGINAL - where Vulkan may be seen (docs/design/vulkan.md, docs/architecture.md "The graphics backend"): the
// Vulkan headers (<vulkan/...>, volk, the memory allocator, SDL's Vulkan header) are included - by a file itself or
// through another of the project's headers - only in the Vulkan backend's folder, src/render_vulkan, and by the
// context check that tries it (src/tests/vulkan_context_check.cpp). Vulkan's functions are called there alone, and only
// app/ (and that check) include the backend's headers: like Direct3D 9's (tools/d3d_headers.test.ts).

import assert from "node:assert/strict";
import { readdirSync, readFileSync } from "node:fs";
import { join, relative } from "node:path";
import { test } from "node:test";

const SRC = join(import.meta.dirname, "..", "src");

// The one program outside the backend that sees Vulkan: it makes a context with the backend's code and prints it.
const CONTEXT_CHECK = "tests/vulkan_context_check.cpp";

const SDK_HEADER = /^\s*#\s*include\s*<(vulkan\/[\w./]+|volk\.h|vk_mem_alloc\.h|SDL2\/SDL_vulkan\.h)>/m;
const PROJECT_INCLUDE = /^\s*#\s*include\s*"([^"]+)"/gm;
// A call of a Vulkan function (vkCreateInstance...), of volk's (volkInitialize...) or of SDL's Vulkan side.
const VULKAN_CALL = /\b(vk[A-Z]\w*|volk[A-Z]\w*|SDL_Vulkan_\w+)\s*\(/;

// Every source file under src/ (relative to src/, / separators) and its text.
function sources(): Map<string, string> {
    const found = new Map<string, string>();
    const walk = (folder: string): void => {
        for (const entry of readdirSync(folder, { withFileTypes: true })) {
            const path = join(folder, entry.name);
            if (entry.isDirectory()) walk(path);
            else if (/\.(cpp|h)$/.test(entry.name))
                found.set(relative(SRC, path).replaceAll("\\", "/"), readFileSync(path, "utf8"));
        }
    };
    walk(SRC);
    return found;
}

// The files that see a Vulkan header, themselves or through the project's headers they include.
function seeingVulkan(files: Map<string, string>): string[] {
    const sees = new Map<string, boolean>();
    const visit = (name: string, path: Set<string>): boolean => {
        const done = sees.get(name);
        if (done !== undefined) return done;
        if (path.has(name)) return false;
        path.add(name);
        const source = files.get(name) ?? "";
        let result = SDK_HEADER.test(source);
        for (const match of source.matchAll(PROJECT_INCLUDE)) {
            const included = match[1] ?? "";
            if (files.has(included) && visit(included, path)) result = true;
        }
        path.delete(name);
        sees.set(name, result);
        return result;
    };
    return [...files.keys()].filter((name) => visit(name, new Set())).sort();
}

const inBackend = (name: string): boolean => name.startsWith("render_vulkan/");

test("only render_vulkan/ (and the context check) see the Vulkan headers", () => {
    const files = sources();
    const seeing = seeingVulkan(files);
    assert.deepEqual(
        seeing.filter((name) => !inBackend(name) && name !== CONTEXT_CHECK),
        [],
    );
    assert.ok(seeing.includes("render_vulkan/vulkan_loader.cpp"), "the loader includes volk");
    // no header outside the backend, the context check's included
    assert.deepEqual(
        seeing.filter((name) => name.endsWith(".h") && !inBackend(name)),
        [],
    );
});

test("only render_vulkan/ (and the context check) call Vulkan", () => {
    const callers = [...sources()]
        .filter(([, text]) => VULKAN_CALL.test(text))
        .map(([name]) => name)
        .sort();
    assert.deepEqual(
        callers.filter((name) => !inBackend(name) && name !== CONTEXT_CHECK),
        [],
    );
});

test("only app/ (and the context check) include the Vulkan backend", () => {
    const includers = [...sources()]
        .filter(([, text]) => /^\s*#\s*include\s*"render_vulkan\//m.test(text))
        .map(([name]) => name)
        .filter((name) => !inBackend(name) && !name.startsWith("app/") && name !== CONTEXT_CHECK);
    assert.deepEqual(includers, []);
});
