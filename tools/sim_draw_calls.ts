// NOT ORIGINAL - which functions run scripts, through any chain of calls the source shows, and where the drawing calls
// one (tools/sim_draw_independence.test.ts "the drawing calls nothing that runs scripts"; docs/design/simulation-rate.md
// "What moved"). A script changes the game's state, and the drawing runs only on drawn steps: the time of day's clock
// once ran the level's time-of-day scripts from draw_world through two modules (render_pc -> engine/gfx/day_clock ->
// tod_manager's set_tod_instant -> refresh_all -> spawn_script), which a test of the drawing's own lines can't see.
//
// A light reading of C++ written in this project's style (definitions begin at column 0, a multi-line body ends with
// "}" at column 0, namespaces aren't indented): each function's calls by name, `Qualifier::name(` or `name(` (member
// calls `.name(` / `->name(` too). A function runs scripts when its body starts one (SCRIPT_START), or it calls a
// function that does: `q::name(` resolves to the definitions of name in class or namespace q; `name(` to the caller's
// own class or namespace when it defines one, else to the caller's file's, else to the one definition of that name in
// the source (a name defined in several places - `step`, `place` - can't be told apart by name: the chain is missed
// rather than guessed; a local variable made with parentheses reads as a call, which this rule keeps from counting). A
// call counts when every definition it resolves to runs scripts. Calls through function pointers and virtual dispatch
// aren't followed.

import { readdirSync, readFileSync } from "node:fs";
import { join, relative } from "node:path";

export const SCRIPT_START = /\b(launch_script|spawn_script|run_script_once|run_script)\s*\(/;

export interface SourceFile {
    name: string; // repo-relative, "/" separators
    text: string;
}

export interface FunctionDef {
    file: string;
    line: number; // 1-based, the definition's first line
    qualifier: string; // its class's or innermost named namespace's last part ("" at the top)
    name: string;
    calls: { qualifier: string; name: string; line: number }[];
    startsScript: boolean;
}

const NOT_CALLS = new Set([
    "if",
    "for",
    "while",
    "switch",
    "return",
    "sizeof",
    "alignof",
    "decltype",
    "static_cast",
    "reinterpret_cast",
    "const_cast",
    "dynamic_cast",
    "catch",
    "noexcept",
    "static_assert",
    "defined",
    "assert",
]);
const NOT_DEFINITIONS =
    /^(if|for|while|switch|return|namespace|struct|class|enum|union|using|template|typedef|static_assert|extern|#|}|\/\/|\/\*|\*)/;

export function sourceFiles(root: string, folder = "src", out: SourceFile[] = []): SourceFile[] {
    for (const entry of readdirSync(join(root, folder), { withFileTypes: true })) {
        const path = join(root, folder, entry.name);
        const name = relative(root, path).replaceAll("\\", "/");
        if (entry.isDirectory()) {
            if (name !== "src/tests") sourceFiles(root, name, out);
        } else if (entry.name.endsWith(".cpp")) out.push({ name, text: readFileSync(path, "utf8") });
    }
    return out;
}

// The functions defined in a file, with their calls.
export function functionsOf(file: SourceFile): FunctionDef[] {
    const out: FunctionDef[] = [];
    const namespaces: string[] = [];
    let current: FunctionDef | null = null;
    file.text.split(/\r?\n/).forEach((raw, index) => {
        const code = raw.replace(/\/\/.*$/, "").replace(/"(?:[^"\\]|\\.)*"/g, '""');
        const opened = /^namespace\s*([\w:]*)\s*\{/.exec(code);
        if (opened) {
            namespaces.push((opened[1] ?? "").split("::").pop() ?? "");
            current = null;
            return;
        }
        if (/^}\s*\/\/\s*namespace/.test(raw)) {
            namespaces.pop();
            current = null;
            return;
        }
        if (/^};?\s*$/.test(code)) {
            current = null;
            return;
        }
        if (/^(struct|class|enum|union)\b/.test(code)) {
            current = null;
            return;
        }
        let rest = code;
        if (/^\S/.test(code) && !NOT_DEFINITIONS.test(code) && !/;\s*$/.test(code)) {
            const found = /^[^;=(]*?\b((?:\w+::)*)(~?\w+)\s*\(/.exec(code);
            if (found?.[2] !== undefined && !NOT_CALLS.has(found[2])) {
                const qualifiers = (found[1] ?? "").split("::").filter((part) => part !== "");
                const named = namespaces.filter((part) => part !== "");
                current = {
                    file: file.name,
                    line: index + 1,
                    qualifier: qualifiers.at(-1) ?? named.at(-1) ?? "",
                    name: found[2],
                    calls: [],
                    startsScript: false,
                };
                out.push(current);
                rest = code.slice(found[0].length);
            }
        }
        const into = current as FunctionDef | null;
        if (into === null) return;
        if (SCRIPT_START.test(rest)) into.startsScript = true;
        for (const call of rest.matchAll(/(?:(\w+)::)?(~?\w+)\s*\(/g)) {
            const name = call[2]!;
            if (!NOT_CALLS.has(name)) into.calls.push({ qualifier: call[1] ?? "", name, line: index + 1 });
        }
    });
    return out;
}

export class CallGraph {
    readonly functions: FunctionDef[];
    private readonly byKey = new Map<string, FunctionDef[]>();
    private readonly byName = new Map<string, FunctionDef[]>();
    private readonly runners = new Set<FunctionDef>();

    constructor(files: SourceFile[]) {
        this.functions = files.flatMap(functionsOf);
        for (const def of this.functions) {
            const key = `${def.qualifier}::${def.name}`;
            this.byKey.set(key, [...(this.byKey.get(key) ?? []), def]);
            this.byName.set(def.name, [...(this.byName.get(def.name) ?? []), def]);
            if (def.startsScript || SCRIPT_START.test(`${def.name}(`)) this.runners.add(def);
        }
        for (let changed = true; changed; ) {
            changed = false;
            for (const def of this.functions) {
                if (this.runners.has(def)) continue;
                if (def.calls.some((call) => this.callRunsScripts(def, call))) {
                    this.runners.add(def);
                    changed = true;
                }
            }
        }
    }

    // The definitions a call names (see the file's header).
    resolve(caller: FunctionDef, call: { qualifier: string; name: string }): FunctionDef[] {
        if (call.qualifier !== "") return this.byKey.get(`${call.qualifier}::${call.name}`) ?? [];
        const own = this.byKey.get(`${caller.qualifier}::${call.name}`);
        if (own !== undefined) return own;
        const named = this.byName.get(call.name) ?? [];
        const here = named.filter((def) => def.file === caller.file);
        if (here.length > 0) return here;
        return named.length === 1 ? named : [];
    }

    callRunsScripts(caller: FunctionDef, call: { qualifier: string; name: string }): boolean {
        const targets = this.resolve(caller, call).filter((target) => target !== caller);
        return targets.length > 0 && targets.every((target) => this.runners.has(target));
    }

    runsScripts(def: FunctionDef): boolean {
        return this.runners.has(def);
    }

    // Each call from `defs` into a function that runs scripts: "file:line caller -> qualifier::callee".
    callsIntoScripts(defs: FunctionDef[]): string[] {
        const found: string[] = [];
        for (const def of defs) {
            for (const call of def.calls) {
                if (this.callRunsScripts(def, call)) {
                    const shown = call.qualifier !== "" ? `${call.qualifier}::${call.name}` : call.name;
                    found.push(`${def.file}:${call.line} ${def.name} -> ${shown}`);
                }
            }
        }
        return found;
    }
}
