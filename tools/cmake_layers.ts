// NOT ORIGINAL - the CMake library check (docs/architecture.md "Layers"): CMake's link graph against the layer map
// (tools/layers.json) that tools/layers.ts holds the includes to. It reads the root CMakeLists.txt and every
// src/**/CMakeLists.txt (no configure needed) and checks that
//   - each library of the map but app is one static library target, thug2_<library>, in the CMakeLists.txt of a
//     folder of that library, and the app's folders hold only executables and one object library, thug2_app (what the
//     exes share: the start-up, the main loop, the tables);
//   - every source of a project target lies in its own library's folders, and every .cpp under src/ is compiled by
//     exactly one project target;
//   - a target links (target_link_libraries) only libraries its own may include, and a library reaches through its
//     links every library the map allows it - the links mirror the map;
//   - the one exception: sibling links between libraries the include baseline (tools/layers_baseline.txt) still has
//     lines between. Each such pair must be linked, and a link with no baseline line left fails, so the links shrink
//     with the baseline (none since the editor / front end API: the baseline is empty);
//   - an exe the map's 'executables' names exists and reaches none of the libraries it lists as 'without' (the game's
//     exe: not the level editor, which is a plugin);
//   - a library the map's 'standalone' names (the level editor's launcher) is one executable, thug2_<library>, that
//     links no project target;
//   - a library the map's 'plugins' names (the level editor) is a SHARED library that links its host exe (the exe's
//     import library: the engine API) and compiles against its layers' headers without linking their code
//     ($<COMPILE_ONLY:thug2_game>): a real link would put a second copy of the engine in the plugin.
//   node tools/cmake_layers.ts          check (prints the targets and their links)

import { readdirSync, readFileSync } from "node:fs";
import { join, posix } from "node:path";
import { type LayerMap, libraryOf, loadMap, readBaseline } from "./layers.ts";

const ROOT = join(import.meta.dirname, "..");

/** One command of a CMakeLists.txt: its name (lower case) and its arguments (quotes removed). */
export interface CMakeCommand {
    name: string;
    args: string[];
}

/** The commands of a CMake file's text, comments and bracket arguments aside (enough for this project's files). */
export function parseCMake(text: string): CMakeCommand[] {
    const commands: CMakeCommand[] = [];
    const command = /([A-Za-z_][A-Za-z0-9_]*)\s*\(/y;
    let i = 0;
    while (i < text.length) {
        const c = text[i] as string;
        if (c === "#") {
            const end = text.indexOf("\n", i);
            i = end < 0 ? text.length : end + 1;
            continue;
        }
        if (/\s/.test(c)) {
            i++;
            continue;
        }
        command.lastIndex = i;
        const match = command.exec(text);
        if (match === null) {
            i++;
            continue;
        }
        i = command.lastIndex;
        const args: string[] = [];
        let depth = 1;
        let token = "";
        const flush = () => {
            if (token !== "") args.push(token);
            token = "";
        };
        while (i < text.length && depth > 0) {
            const d = text[i] as string;
            if (d === '"') {
                const end = text.indexOf('"', i + 1);
                token += text.slice(i + 1, end < 0 ? text.length : end);
                i = end < 0 ? text.length : end + 1;
                continue;
            }
            if (d === "#") {
                const end = text.indexOf("\n", i);
                i = end < 0 ? text.length : end;
                continue;
            }
            if (d === "(") depth++;
            if (d === ")") depth--;
            if (depth === 0 || /\s/.test(d)) flush();
            else token += d;
            i++;
        }
        commands.push({ name: (match[1] as string).toLowerCase(), args });
    }
    return commands;
}

/** A project target: where it is defined, what it compiles and what it links. */
export interface Target {
    name: string;
    kind: "library" | "executable";
    object?: boolean; // an OBJECT library (its objects go into the targets that link it)
    shared?: boolean; // a SHARED library (a DLL)
    file: string; // its CMakeLists.txt, from the repository root
    sources: string[]; // from the repository root
    links: string[]; // every target_link_libraries item ($<COMPILE_ONLY:x> as x)
    compileOnly?: string[]; // the links that are $<COMPILE_ONLY:x>: headers only, nothing linked
}

const COMPILE_ONLY = /^\$<COMPILE_ONLY:([^>]+)>$/;

const KEYWORDS = new Set(["STATIC", "SHARED", "MODULE", "OBJECT", "INTERFACE", "WIN32", "EXCLUDE_FROM_ALL"]);
const LINK_KEYWORDS = new Set(["PUBLIC", "PRIVATE", "INTERFACE"]);

/** The targets made in one CMake file (sources resolved against its folder). */
export function targetsOf(text: string, file: string): Target[] {
    const folder = posix.dirname(file);
    const targets = new Map<string, Target>();
    for (const { name, args } of parseCMake(text)) {
        if ((name === "add_library" || name === "add_executable") && args.length > 0 && !args.includes("IMPORTED")) {
            const sources = args
                .slice(1)
                .filter((arg) => !KEYWORDS.has(arg) && !arg.startsWith("$"))
                .map((arg) => posix.normalize(posix.join(folder, arg)));
            targets.set(args[0] as string, {
                name: args[0] as string,
                kind: name === "add_library" ? "library" : "executable",
                ...(name === "add_library" && args.includes("OBJECT") ? { object: true } : {}),
                ...(name === "add_library" && args.includes("SHARED") ? { shared: true } : {}),
                file,
                sources,
                links: [],
            });
        } else if (name === "target_sources" && args.length > 0) {
            // (sources added to a target defined above it - e.g. in an if() on what the configure builds)
            const target = targets.get(args[0] as string);
            if (target !== undefined) {
                target.sources.push(
                    ...args
                        .slice(1)
                        .filter((arg) => !LINK_KEYWORDS.has(arg) && !arg.startsWith("$"))
                        .map((arg) => posix.normalize(posix.join(folder, arg))),
                );
            }
        } else if (name === "target_link_libraries" && args.length > 0) {
            const target = targets.get(args[0] as string);
            if (target === undefined) continue;
            for (const arg of args.slice(1).filter((item) => !LINK_KEYWORDS.has(item))) {
                const compileOnly = COMPILE_ONLY.exec(arg);
                if (compileOnly === null) {
                    target.links.push(arg);
                } else {
                    target.links.push(compileOnly[1] as string);
                    target.compileOnly = [...(target.compileOnly ?? []), compileOnly[1] as string];
                }
            }
        }
    }
    return [...targets.values()];
}

/** The project's CMake files: the root's and every src/**\/CMakeLists.txt, from the repository root. */
export function cmakeFiles(root: string = ROOT): string[] {
    const files = ["CMakeLists.txt"];
    const walk = (folder: string) => {
        for (const entry of readdirSync(join(root, folder), { withFileTypes: true })) {
            const path = posix.join(folder, entry.name);
            if (entry.isDirectory()) walk(path);
            else if (entry.name === "CMakeLists.txt") files.push(path);
        }
    };
    walk("src");
    return files;
}

/** Every target defined in src/ (the project's own: the layers' libraries, the exe and the checks). */
export function projectTargets(root: string = ROOT): Target[] {
    return cmakeFiles(root)
        .filter((file) => file.startsWith("src/"))
        .flatMap((file) => targetsOf(readFileSync(join(root, file), "utf8"), file));
}

/** The library pairs ("from -> to") that the include baseline still has lines between. */
export function siblingPairs(map: LayerMap, baseline: string[]): Set<string> {
    const pairs = new Set<string>();
    for (const line of baseline) {
        const [from, to] = line.split(" -> ");
        const a = libraryOf(map, from ?? "");
        const b = libraryOf(map, to ?? "");
        if (a !== undefined && b !== undefined && a !== b) pairs.add(`${a} -> ${b}`);
    }
    return pairs;
}

/** The libraries joined to `library` by sibling pairs (either way, through any number of pairs), itself aside. */
export function siblingGroup(pairs: Set<string>, library: string): Set<string> {
    const group = new Set<string>([library]);
    const stack = [library];
    while (stack.length > 0) {
        const at = stack.pop() as string;
        for (const pair of pairs) {
            const [a, b] = pair.split(" -> ") as [string, string];
            const other = a === at ? b : b === at ? a : undefined;
            if (other !== undefined && !group.has(other)) {
                group.add(other);
                stack.push(other);
            }
        }
    }
    group.delete(library);
    return group;
}

/** Every problem of the targets against the map (empty when CMake mirrors it). */
export function problems(map: LayerMap, targets: Target[], cppFiles: string[], baseline: string[]): string[] {
    const found: string[] = [];
    const libraryOfTarget = new Map<string, string>(); // target -> its library in the map
    for (const target of targets) {
        const library = libraryOf(map, target.file);
        if (library === undefined) {
            found.push(`${target.name}: ${target.file} is in no library of tools/layers.json`);
            continue;
        }
        libraryOfTarget.set(target.name, library);
        if (library === "app") {
            if (target.kind !== "executable" && !(target.object === true && target.name === "thug2_app"))
                found.push(`${target.name}: the app's folders hold executables and the object library thug2_app only`);
        } else if ((map.standalone ?? []).includes(library)) {
            if (target.kind !== "executable" || target.name !== `thug2_${library}`)
                found.push(`${target.name}: a target in ${library}'s folders must be the executable thug2_${library}`);
        } else if (target.kind !== "library" || target.name !== `thug2_${library}`) {
            found.push(`${target.name}: a target in ${library}'s folders must be the library thug2_${library}`);
        }
        for (const source of target.sources) {
            if (libraryOf(map, source) !== library)
                found.push(`${target.name}: ${source} is not in ${library}'s folders`);
        }
    }
    for (const library of Object.keys(map.libraries)) {
        if (library === "app") continue;
        const count = targets.filter((target) => target.name === `thug2_${library}`).length;
        if (count !== 1) found.push(`thug2_${library}: ${count} targets for the library ${library} (one expected)`);
    }
    // the sources: every .cpp of src/ in exactly one target
    const owners = new Map<string, string[]>();
    for (const target of targets)
        for (const source of target.sources) owners.set(source, [...(owners.get(source) ?? []), target.name]);
    for (const cpp of cppFiles) {
        const by = owners.get(cpp) ?? [];
        if (by.length !== 1) found.push(`${cpp}: compiled by ${by.length === 0 ? "no target" : by.join(", ")}`);
    }
    // the links
    const pairs = siblingPairs(map, baseline);
    // (a plugin's link to its host exe is the engine API's import library, not a layer: left out of the layers' rules)
    const hostOf = (target: Target) => map.plugins?.[libraryOfTarget.get(target.name) ?? ""]?.host;
    const projectLinks = (target: Target) =>
        target.links.filter((link) => libraryOfTarget.has(link) && link !== hostOf(target));
    const byName = new Map(targets.map((target) => [target.name, target]));
    for (const [library, plugin] of Object.entries(map.plugins ?? {})) {
        const target = byName.get(`thug2_${library}`);
        if (target === undefined) continue;
        if (target.shared !== true) found.push(`${target.name}: a plugin is a SHARED library`);
        if (byName.get(plugin.host)?.kind !== "executable" || !target.links.includes(plugin.host))
            found.push(`${target.name}: a plugin links its host exe ${plugin.host} (the engine API's import library)`);
        for (const link of projectLinks(target)) {
            if (!(target.compileOnly ?? []).includes(link))
                found.push(
                    `${target.name} links ${link}: a plugin compiles against the layers' headers only ($<COMPILE_ONLY:${link}>) - their code is the host's`,
                );
        }
    }
    for (const target of targets) {
        const library = libraryOfTarget.get(target.name);
        if (library === undefined) continue;
        if ((map.standalone ?? []).includes(library) && projectLinks(target).length > 0)
            found.push(
                `${target.name} links ${projectLinks(target).join(", ")}: a standalone exe links no library of the game`,
            );
        const allowed = new Set(map.libraries[library]?.allowed ?? []);
        for (const link of projectLinks(target)) {
            const to = libraryOfTarget.get(link) as string;
            if (byName.get(link)?.kind !== "library") found.push(`${target.name} links the executable ${link}`);
            else if (to !== library && !allowed.has(to) && !pairs.has(`${library} -> ${to}`))
                found.push(`${target.name} links ${link}: ${library} may not include ${to}`);
        }
        if (target.kind !== "library" || library === "app") continue;
        // what it reaches through its links (the sibling pairs aside) must be exactly what the map allows it; a sibling
        // link is not followed - what the sibling links is the sibling's own (the editor's backend, say)
        const siblings = siblingGroup(pairs, library);
        const reached = new Set<string>();
        const stack = projectLinks(target);
        while (stack.length > 0) {
            const name = stack.pop() as string;
            const to = libraryOfTarget.get(name) as string;
            if (reached.has(to) || to === library) continue;
            reached.add(to);
            if (!siblings.has(to)) stack.push(...projectLinks(byName.get(name) as Target));
        }
        for (const to of allowed) {
            if (!reached.has(to)) found.push(`${target.name} doesn't reach thug2_${to}, which ${library} may include`);
        }
        for (const to of reached) {
            if (!allowed.has(to) && !siblings.has(to))
                found.push(`${target.name} reaches thug2_${to}, which ${library} may not include`);
        }
    }
    // each sibling pair of the baseline is linked, and no other sibling link exists
    for (const pair of pairs) {
        const [from, to] = pair.split(" -> ") as [string, string];
        const target = byName.get(`thug2_${from}`);
        if (from === "app" || to === "app" || target === undefined) continue;
        if (!target.links.includes(`thug2_${to}`))
            found.push(`thug2_${from} must link thug2_${to}: tools/layers_baseline.txt has ${pair} lines`);
    }
    // the exes that must do without some libraries
    for (const [name, rule] of Object.entries(map.executables ?? {})) {
        const exe = byName.get(name);
        if (exe === undefined || exe.kind !== "executable") {
            found.push(`${name}: no such executable (tools/layers.json "executables")`);
            continue;
        }
        const reached = new Set<string>();
        const stack = [...exe.links.filter((link) => byName.has(link))];
        while (stack.length > 0) {
            const link = stack.pop() as string;
            if (reached.has(link)) continue;
            reached.add(link);
            stack.push(...(byName.get(link)?.links.filter((next) => byName.has(next)) ?? []));
        }
        for (const without of rule.without) {
            if (reached.has(`thug2_${without}`))
                found.push(`${name} reaches thug2_${without}, which it must do without`);
        }
    }
    return found;
}

/** Every .cpp under src/, from the repository root. */
export function cppFilesOf(root: string = ROOT): string[] {
    const out: string[] = [];
    const walk = (folder: string) => {
        for (const entry of readdirSync(join(root, folder), { withFileTypes: true })) {
            const path = posix.join(folder, entry.name);
            if (entry.isDirectory()) walk(path);
            else if (entry.name.endsWith(".cpp")) out.push(path);
        }
    };
    walk("src");
    return out.sort();
}

function main(): number {
    const map = loadMap();
    const targets = projectTargets();
    for (const target of targets) {
        const links = target.links.filter((link) => link.startsWith("thug2"));
        console.log(`${target.name} (${posix.dirname(target.file)}): ${links.join(" ")}`);
    }
    const found = problems(map, targets, cppFilesOf(), readBaseline());
    for (const problem of found) console.error(problem);
    console.log(`${targets.length} targets, ${found.length} problems`);
    return found.length > 0 ? 1 : 0;
}

if (import.meta.main) process.exitCode = main();
