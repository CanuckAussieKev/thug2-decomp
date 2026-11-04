// NOT ORIGINAL - the source's layering rule (docs/architecture.md "Layers"): the named views of in-memory layouts
// (raw::as / field_at / bytes / chars / owner_of, cell::of / pointer / address / at_address, memory::bits_as,
// function_at, library_function) belong to the boundary layer only - src/formats, src/wire, src/gpu, src/platform,
// and the QB bytecode readers until the script runtime is replaced. Anywhere else a use is an error: a layout to be
// given its type, or data from outside to decode at the boundary.
//   node tools/layering.ts            each use outside the boundary (none allowed)

import { readdirSync, readFileSync } from "node:fs";
import { join, relative } from "node:path";

const ROOT = join(import.meta.dirname, "..");

/** The boundary: folders and files where the views are the code's job. */
const BOUNDARY_FOLDERS = ["src/formats/", "src/wire/", "src/gpu/", "src/platform/"];
const BOUNDARY_FILES = new Set([
    // the helpers themselves
    "src/base/core/raw_view.h",
    "src/base/core/pointer_cell.h",
    "src/base/core/bits_as.h",
    // the all-ones pointer (INVALID_HANDLE_VALUE, the list heads' marker), from platform/ in stage 11 round 3
    "src/base/core/all_ones.h",
    // a script value's 32 bits, read as the kind its holder says (the QB value cell)
    "src/engine/script/cell.h",
    // the QB bytecode and its pre-compiled images, read in place
    "src/engine/script/qb.cpp",
    "src/engine/script/token_reader.cpp",
    "src/engine/script/precompiled.cpp",
    // the format check lays the readers' in-place images out itself (moved from formats/check in stage 11 round 11)
    "src/tests/format_check_files.cpp",
]);

const VIEW =
    /\braw::(?:as|field_at|bytes|chars|owner_of)\b|\bcell::(?:of|pointer|address|at_address)\b|\bbits_as\b|\bfunction_at\b|\blibrary_function\b/g;

export function isBoundary(path: string): boolean {
    return BOUNDARY_FILES.has(path) || BOUNDARY_FOLDERS.some((folder) => path.startsWith(folder));
}

export function countViews(text: string): number {
    return (text.match(VIEW) ?? []).length;
}

function sourceFiles(folder: string, out: string[] = []): string[] {
    for (const entry of readdirSync(folder, { withFileTypes: true })) {
        const path = join(folder, entry.name);
        if (entry.isDirectory()) sourceFiles(path, out);
        else if (/\.(cpp|h)$/.test(entry.name)) out.push(path);
    }
    return out;
}

/** Each file outside the boundary with views, and how many. */
export function viewCounts(): Map<string, number> {
    const counts = new Map<string, number>();
    for (const path of sourceFiles(join(ROOT, "src"))) {
        const rel = relative(ROOT, path).replaceAll("\\", "/");
        if (isBoundary(rel)) continue;
        const n = countViews(readFileSync(path, "utf8"));
        if (n > 0) counts.set(rel, n);
    }
    return counts;
}

/** Each use outside the boundary, as "file:line: text". */
export function viewsOutside(): string[] {
    const found: string[] = [];
    for (const file of [...viewCounts().keys()].sort()) {
        const lines = readFileSync(join(ROOT, file), "utf8").split("\n");
        lines.forEach((line, i) => {
            if (countViews(line) > 0) found.push(`${file}:${i + 1}: ${line.trim()}`);
        });
    }
    return found;
}

function main(): void {
    const found = viewsOutside();
    for (const line of found) console.log(line);
    console.log(`${found.length} lines with named views outside the boundary`);
    if (found.length) process.exitCode = 1;
}

if (import.meta.main) main();
