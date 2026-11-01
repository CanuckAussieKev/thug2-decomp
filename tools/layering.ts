// NOT ORIGINAL - the source's layering rule (docs/architecture.md "Layers"): the named views of in-memory layouts
// (raw::as / field_at / bytes / chars / owner_of, cell::of / pointer / address / at_address, memory::bits_as,
// function_at, library_function) belong to the boundary layer only - src/formats, src/wire, src/gpu, src/platform,
// and the QB bytecode readers until the script runtime is replaced. Anywhere else each use is a layout still to be
// given its type; tools/layering_budget.json lists how many each file may still have, and the count may only fall.
//   node tools/layering.ts            the files' counts against the budget
//   node tools/layering.ts --update   the budget lowered to today's counts (never raised)

import { readdirSync, readFileSync, writeFileSync } from "node:fs";
import { join, relative } from "node:path";

const ROOT = join(import.meta.dirname, "..");
const BUDGET = join(import.meta.dirname, "layering_budget.json");

/** The boundary: folders and files where the views are the code's job. */
const BOUNDARY_FOLDERS = ["src/formats/", "src/wire/", "src/gpu/", "src/platform/"];
const BOUNDARY_FILES = new Set([
    // the helpers themselves
    "src/engine/core/raw_view.h",
    "src/engine/core/pointer_cell.h",
    "src/engine/core/bits_as.h",
    // the QB bytecode and its pre-compiled images, read in place
    "src/engine/script/qb.cpp",
    "src/engine/script/token_reader.cpp",
    "src/engine/script/precompiled.cpp",
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

export function readBudget(): Record<string, number> {
    return JSON.parse(readFileSync(BUDGET, "utf8")) as Record<string, number>;
}

/** The files over their budget (a file not in it has none). */
export function overBudget(counts: Map<string, number>, budget: Record<string, number>): string[] {
    const over: string[] = [];
    for (const [file, n] of counts) {
        const allowed = budget[file] ?? 0;
        if (n > allowed) over.push(`${file}: ${n} named views (budget ${allowed})`);
    }
    return over.sort();
}

function main(): void {
    const counts = viewCounts();
    const budget = readBudget();
    const total = [...counts.values()].reduce((a, b) => a + b, 0);
    if (process.argv.includes("--update")) {
        const lowered: Record<string, number> = {};
        for (const file of Object.keys(budget).sort()) {
            const n = Math.min(budget[file] ?? 0, counts.get(file) ?? 0);
            if (n > 0) lowered[file] = n;
        }
        writeFileSync(BUDGET, `${JSON.stringify(lowered, null, 4)}\n`);
    }
    const over = overBudget(counts, budget);
    for (const line of over) console.log(line);
    console.log(`${total} named views outside the boundary in ${counts.size} files`);
    if (over.length) process.exitCode = 1;
}

if (import.meta.main) main();
