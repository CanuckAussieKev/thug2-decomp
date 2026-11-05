// NOT ORIGINAL - the layer checker (docs/architecture.md "Layers"). tools/layers.json says which library each folder
// belongs to and which libraries it may include; any other project include is an upward (or sideways) include. The
// includes that exist today are in tools/layers_baseline.txt, one "file -> header" line each. The baseline is a
// ratchet: a violation that is not in it fails, and so does a line in it that no longer occurs - it may only shrink.
//   node tools/layers.ts              check against the baseline
//   node tools/layers.ts --list       print every violation (the baseline's text)
//   node tools/layers.ts --write      rewrite the baseline (only ever shorter: refuses to add a line)
//   node tools/layers.ts --api <lib>  print the headers of <lib>'s api libraries that its files include now
// The map's 'strict' libraries (every library but the app's) are held at none: any upward include of theirs fails, in
// the sources or in the baseline.
// A library may also have an 'api': the exact list of the headers of some lower libraries ('of': the engine and the
// game, for the editor and the front end) that its files may include. An include of another header of those libraries
// fails, and so does a listed header that none of its files includes: the list is what the library uses, no more.

import { existsSync, readdirSync, readFileSync, writeFileSync } from "node:fs";
import { dirname, join, relative } from "node:path";

const ROOT = join(import.meta.dirname, "..");
const MAP_FILE = join(import.meta.dirname, "layers.json");
const BASELINE_FILE = join(import.meta.dirname, "layers_baseline.txt");

export interface Api {
    of: string[]; // the libraries whose headers the list covers
    headers: string[]; // from the repository root
}

export interface LayerMap {
    libraries: Record<string, { folders: string[]; allowed: string[]; api?: Api }>;
    strict?: string[];
    executables?: Record<string, { without: string[] }>; // (tools/cmake_layers.ts)
}

export function loadMap(): LayerMap {
    return JSON.parse(readFileSync(MAP_FILE, "utf8")) as LayerMap;
}

/** The library of a path under the repository root (the longest matching folder prefix), or undefined. */
export function libraryOf(map: LayerMap, path: string): string | undefined {
    let best: string | undefined;
    let bestLength = 0;
    for (const [name, library] of Object.entries(map.libraries)) {
        for (const folder of library.folders) {
            if (path.startsWith(folder) && folder.length > bestLength) {
                best = name;
                bestLength = folder.length;
            }
        }
    }
    return best;
}

/** True when a file of library `from` may include a file of library `to`. */
export function mayInclude(map: LayerMap, from: string, to: string): boolean {
    return from === to || (map.libraries[from]?.allowed.includes(to) ?? false);
}

function sourceFiles(folder: string, out: string[] = []): string[] {
    for (const entry of readdirSync(folder, { withFileTypes: true })) {
        const path = join(folder, entry.name);
        if (entry.isDirectory()) sourceFiles(path, out);
        else if (/\.(cpp|h|inc)$/.test(entry.name)) out.push(path);
    }
    return out;
}

const INCLUDE = /^\s*#\s*include\s*"([^"]+)"/gm;

/** Every project include under src/ as "file -> header" (paths from the repository root). */
export function includes(): string[] {
    const found: string[] = [];
    for (const path of sourceFiles(join(ROOT, "src"))) {
        const rel = relative(ROOT, path).replaceAll("\\", "/");
        const text = readFileSync(path, "utf8");
        for (const match of text.matchAll(INCLUDE)) {
            const name = match[1] as string;
            // project includes are written from src/; a file's own folder is tried second
            let target = `src/${name}`;
            if (!existsSync(join(ROOT, target))) {
                target = relative(ROOT, join(dirname(path), name)).replaceAll("\\", "/");
                if (!existsSync(join(ROOT, target))) continue;
            }
            found.push(`${rel} -> ${target}`);
        }
    }
    return found;
}

/** Every violation as "file -> header" (paths from the repository root), sorted. */
export function violations(map: LayerMap = loadMap(), lines: string[] = includes()): string[] {
    const found: string[] = [];
    for (const line of lines) {
        const [rel, target] = line.split(" -> ") as [string, string];
        const from = libraryOf(map, rel);
        const to = libraryOf(map, target);
        if (from !== undefined && to !== undefined && !mayInclude(map, from, to)) found.push(line);
    }
    return found.sort();
}

/** The headers of `library`'s api libraries that its files include now (sorted, each once). */
export function apiInUse(map: LayerMap, library: string, lines: string[] = includes()): string[] {
    const of = new Set(map.libraries[library]?.api?.of ?? []);
    const used = new Set<string>();
    for (const line of lines) {
        const [rel, target] = line.split(" -> ") as [string, string];
        const to = libraryOf(map, target);
        if (libraryOf(map, rel) === library && to !== undefined && of.has(to)) used.add(target);
    }
    return [...used].sort();
}

/** Includes outside a library's api list, and listed headers that none of its files includes. */
export function apiProblems(map: LayerMap, lines: string[] = includes()): string[] {
    const found: string[] = [];
    for (const [library, entry] of Object.entries(map.libraries)) {
        if (entry.api === undefined) continue;
        const listed = new Set(entry.api.headers);
        const used = new Set(apiInUse(map, library, lines));
        const of = new Set(entry.api.of);
        for (const line of lines) {
            const [rel, target] = line.split(" -> ") as [string, string];
            const to = libraryOf(map, target);
            if (libraryOf(map, rel) === library && to !== undefined && of.has(to) && !listed.has(target))
                found.push(`not in ${library}'s api: ${line}`);
        }
        for (const header of entry.api.headers) {
            if (!used.has(header)) found.push(`in ${library}'s api but included by none of its files: ${header}`);
        }
    }
    return found.sort();
}

/** The lines ("file -> header") whose including file belongs to one of the map's strict libraries. */
export function strictViolations(map: LayerMap, lines: string[]): string[] {
    const strict = new Set(map.strict ?? []);
    return lines.filter((line) => {
        const from = libraryOf(map, line.split(" -> ")[0] as string);
        return from !== undefined && strict.has(from);
    });
}

export function readBaseline(): string[] {
    if (!existsSync(BASELINE_FILE)) return [];
    return readFileSync(BASELINE_FILE, "utf8")
        .split(/\r?\n/) // (a CRLF checkout of the baseline too: core.autocrlf)
        .filter((line) => line !== "" && !line.startsWith("#"));
}

export interface Comparison {
    added: string[];
    stale: string[];
}

/** Violations the baseline doesn't list (new) and baseline lines that no longer occur (to be removed). */
export function compare(current: string[], baseline: string[]): Comparison {
    const now = new Set(current);
    const old = new Set(baseline);
    return {
        added: current.filter((line) => !old.has(line)),
        stale: baseline.filter((line) => !now.has(line)),
    };
}

function baselineText(lines: string[]): string {
    const header =
        "# NOT ORIGINAL - the upward includes that exist today (tools/layers.ts). May only shrink: delete a line when its\n" +
        "# include goes, never add one. Regenerate with: node tools/layers.ts --write\n";
    return `${header}${lines.join("\n")}\n`;
}

function main(): number {
    const map = loadMap();
    const lines = includes();
    const current = violations(map, lines);
    const flag = process.argv[2];
    if (flag === "--list") {
        console.log(current.join("\n"));
        return 0;
    }
    if (flag === "--api") {
        console.log(JSON.stringify(apiInUse(map, process.argv[3] ?? "", lines), null, 4));
        return 0;
    }
    const api = apiProblems(map, lines);
    for (const problem of api) console.error(problem);
    const strict = strictViolations(map, [...current, ...readBaseline()]);
    if (strict.length > 0) {
        for (const line of new Set(strict)) console.error(`upward include of a strict library: ${line}`);
        return 1;
    }
    const { added, stale } = compare(current, readBaseline());
    if (flag === "--write") {
        if (added.length > 0 && existsSync(BASELINE_FILE)) {
            console.error(`${added.length} violations are not in the baseline (it may only shrink):`);
            console.error(added.join("\n"));
            return 1;
        }
        writeFileSync(BASELINE_FILE, baselineText(current));
        console.log(`baseline: ${current.length} lines`);
        return 0;
    }
    for (const line of added) console.error(`upward include not in the baseline: ${line}`);
    for (const line of stale) console.error(`baseline line no longer needed (run --write): ${line}`);
    const apis = Object.entries(map.libraries).flatMap(([name, entry]) =>
        entry.api === undefined ? [] : [`${name} ${entry.api.headers.length}`],
    );
    console.log(`${current.length} upward includes, baseline ${readBaseline().length}; api lists: ${apis.join(", ")}`);
    return added.length + stale.length + api.length > 0 ? 1 : 0;
}

if (import.meta.main) process.exitCode = main();
