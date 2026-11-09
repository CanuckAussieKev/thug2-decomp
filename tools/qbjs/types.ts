// NOT ORIGINAL - the types the scripts use, from what the .qb files say and what the game saw ([debug] ts_types=1:
// thug2-decomp-ts\types-recorded.json, scripting/runtime.js). They are the facts the natural-TypeScript steps rest on
// (docs/qb-ts.md "Types"): a parameter only ever an integer can be a plain number, an operator only ever given numbers
// can be +, a command never seen waiting needs no yield*.
//
//   node tools/qbjs/types.ts --game <folder> [--out <folder>]
//
// Writes <out> (default <game>\thug2-decomp-ts\types): commands.d.ts - every command the scripts call, its arguments
// and their types; report.md - how many sites have one type, and the ones with more. The summary is printed.
import { existsSync, mkdirSync, readdirSync, readFileSync, statSync, writeFileSync } from "node:fs";
import { join, relative } from "node:path";
import { pathToFileURL } from "node:url";
import { CFUNC_NAMES } from "thug2-level-converter-core/level_port/thug2_cfuncs.ts";
import { checksum } from "../qb_checksum.ts";
import { gameRoot } from "./install_ts.ts";
import type { Expr, Field, ModuleIR, Statement } from "./ir.ts";
import { readIR } from "./reader.ts";

// site -> type -> count (the recorded file's shape; static sites count their occurrences in the .qb files)
type Counts = Map<string, Map<string, number>>;

const LITERAL_TYPES: Record<number, string> = {
    1: "int",
    2: "float",
    3: "string",
    4: "localstring",
    5: "pair",
    6: "vector",
    13: "name",
};
const hex = (n: number) => (n >>> 0).toString(16).padStart(8, "0");

// A value's type where the .qb file shows it (undefined: only known when it runs - a parameter, an expression...).
export function staticType(x: Expr): string | undefined {
    switch (x.kind) {
        case "literal":
            return LITERAL_TYPES[x.value.type];
        case "structure":
            return "struct";
        case "array": {
            const first = x.items[0];
            const inner = first ? staticType(first) : "empty";
            return `array<${inner ?? "unknown"}>`;
        }
        case "inlineScript":
            return "script";
        case "randomRange":
            return "float";
        default:
            return undefined;
    }
}

function add(counts: Counts, site: string, type: string, n = 1) {
    let m = counts.get(site);
    if (!m) {
        m = new Map();
        counts.set(site, m);
    }
    m.set(type, (m.get(type) ?? 0) + n);
}

// What the .qb files show: each call's arguments (by command and argument), each global's type, each script's
// defaults and the parameters it reads.
export function staticTypes(modules: ModuleIR[]): {
    counts: Counts;
    scripts: Map<number, string>;
    reads: Map<number, Set<number>>;
} {
    const counts: Counts = new Map();
    const scripts = new Map<number, string>(); // script -> its module
    const reads = new Map<number, Set<number>>(); // script -> the parameters it reads
    const fieldsOf = (cmd: number, fields: Field[]) => {
        let unnamed = 0;
        for (const f of fields) {
            if ("spread" in f || "kind" in f) continue;
            const t = staticType(f.value);
            const site = `arg ${hex(cmd)} ${f.name ? hex(f.name) : `#${unnamed++}`}`;
            if (t) add(counts, site, t);
            values(f.value);
        }
    };
    let script = 0;
    const values = (x: Expr | undefined): void => {
        if (!x) return;
        if (x.kind === "parameter") reads.get(script)?.add(x.name);
        if (x.kind === "binary") [x.left, x.right].forEach(values);
        if (x.kind === "expressionResult") values(x.value);
        if (x.kind === "randomChoice") x.choices.forEach(values);
        if (x.kind === "array") x.items.forEach(values);
        if (x.kind === "structure" || x.kind === "functionCall")
            for (const f of x.fields) if ("value" in f) values(f.value);
        if (x.kind === "inlineScript") statements(x.body);
    };
    const statements = (xs: Statement[]): void => {
        for (const x of xs) {
            if (x.kind === "call") {
                if (x.name && x.via === undefined) fieldsOf(x.name, x.fields);
                else for (const f of x.fields) if ("value" in f) values(f.value);
                values(x.byExpr);
                values(x.onExpr);
            } else if (x.kind === "assign") values(x.value);
            else if (x.kind === "if") {
                if (x.condition.kind === "call") statements([x.condition]);
                else if (x.condition.kind === "assign") values(x.condition.value);
                else values(x.condition);
                statements(x.yes);
                statements(x.no);
            } else if (x.kind === "loop") statements(x.body);
            else if (x.kind === "randomBlock") x.branches.forEach(statements);
            else if (x.kind === "switch") {
                for (const c of x.cases) statements(c.body);
                if (x.fallback) statements(x.fallback);
            } else if (x.kind === "return") for (const f of x.fields) if ("value" in f) values(f.value);
        }
    };
    for (const module of modules)
        for (const d of module.definitions) {
            if (d.kind === "global") {
                add(counts, `global ${hex(d.name)}`, staticType(d.value) ?? "expression");
                continue;
            }
            scripts.set(d.name, module.source);
            reads.set(d.name, new Set());
            script = d.name;
            for (const f of d.defaults)
                if ("value" in f && f.name)
                    add(counts, `default ${hex(d.name)} ${hex(f.name)}`, staticType(f.value) ?? "expression");
            statements(d.body);
        }
    return { counts, scripts, reads };
}

export function readRecorded(path: string): Counts {
    const counts: Counts = new Map();
    if (!existsSync(path)) return counts;
    const sites = (JSON.parse(readFileSync(path, "utf8")) as { sites?: Record<string, Record<string, number>> }).sites;
    for (const [site, types] of Object.entries(sites ?? {}))
        for (const [type, n] of Object.entries(types)) add(counts, site, type, Number(n));
    return counts;
}

// The TypeScript spelling of a QB type (the aliases commands.d.ts declares).
const TS_TYPES: Record<string, string> = {
    int: "Int",
    float: "Float",
    string: "string",
    localstring: "LocalString",
    pair: "Pair",
    vector: "Vector",
    name: "Name",
    struct: "Struct",
    script: "Script",
    callable: "Name",
    none: "undefined",
};
const tsType = (t: string): string => {
    const array = /^array<(.*)>$/.exec(t);
    if (array) return array[1] === "empty" ? "QbArray<never>" : `QbArray<${tsType(array[1]!)}>`;
    return TS_TYPES[t] ?? "unknown";
};
const union = (types: Iterable<string>) => [...new Set([...types].map(tsType))].sort().join(" | ") || "unknown";
const arrayOf = (t: string) => (t.includes(" ") ? `(${t})[]` : `${t}[]`);

export function typesReport(modules: ModuleIR[], recorded: Counts) {
    const { counts: fixed, scripts, reads } = staticTypes(modules);
    // Names: every module's checksum-name records, the scripts and the C functions.
    const names = new Map<number, string>();
    for (const m of modules) for (const [n, s] of Object.entries(m.names ?? {})) names.set(Number(n), s);
    const cfuncs = new Set<number>();
    for (const s of CFUNC_NAMES) {
        names.set(checksum(s), s);
        cfuncs.add(checksum(s));
    }
    const nameOf = (h: string) => {
        const n = Number.parseInt(h, 16);
        return names.get(n) ?? `_0x${h}`;
    };
    const identifier = (s: string) => (/^[A-Za-z_$][\w$]*$/.test(s) ? s : JSON.stringify(s));

    // The commands: every name called with arguments (the .qb files' calls) or seen called (recorded).
    const commands = new Map<string, Map<string, Map<string, number>>>(); // command -> argument -> types
    const note = (counts: Counts) => {
        for (const [site, types] of counts) {
            const [kind, cmd, arg] = site.split(" ");
            if (kind !== "arg" || !cmd || !arg) continue;
            let args = commands.get(cmd);
            if (!args) {
                args = new Map();
                commands.set(cmd, args);
            }
            let seen = args.get(arg);
            if (!seen) {
                seen = new Map();
                args.set(arg, seen);
            }
            for (const [t, n] of types) seen.set(t, (seen.get(t) ?? 0) + n);
        }
    };
    note(fixed);
    note(recorded);
    for (const site of recorded.keys()) {
        const [kind, cmd] = site.split(" ");
        if (kind === "wait" && cmd && !commands.has(cmd)) commands.set(cmd, new Map());
    }

    const lines = [
        "// Generated by tools/qbjs/types.ts: the commands the scripts call - their arguments, with every type the",
        "// .qb files give them and the game was seen passing ([debug] ts_types=1). Not edited by hand.",
        'export type Int = number & { readonly qb?: "int" };',
        'export type Float = number & { readonly qb?: "float" };',
        "export type Name = { readonly type: 13 };",
        "export type LocalString = { readonly type: 4 };",
        "export type Pair = { readonly type: 5 };",
        "export type Vector = { readonly type: 6 };",
        "export type Struct = object;",
        "export type Script = { readonly type: 7 };",
        "export type QbArray<T> = readonly T[];",
        "export type Command = Generator<unknown, boolean, unknown>;",
        "",
    ];
    const waits = (cmd: string) => recorded.get(`wait ${cmd}`);
    const sorted = [...commands.keys()].sort((a, b) =>
        nameOf(a).localeCompare(nameOf(b), "en", { sensitivity: "base" }),
    );
    let neverWaited = 0,
        seenRunning = 0;
    for (const cmd of sorted) {
        const n = Number.parseInt(cmd, 16);
        const args = commands.get(cmd)!;
        const kind = scripts.has(n)
            ? `script (${scripts.get(n)})`
            : cfuncs.has(n)
              ? "C function"
              : "member function, or not defined";
        const w = waits(cmd);
        const waited = w?.get("waited") ?? 0,
            same = w?.get("same frame") ?? 0;
        if (w) {
            ++seenRunning;
            if (!waited) ++neverWaited;
        }
        const named = [...args].filter(([a]) => !a.startsWith("#"));
        const unnamed = [...args].filter(([a]) => a.startsWith("#"));
        const fields = named.map(([a, types]) => `${identifier(nameOf(a))}?: ${union(types.keys())}`);
        const params = [
            ...(fields.length ? [`args?: { ${fields.join("; ")} }`] : []),
            ...(unnamed.length ? [`...unnamed: ${arrayOf(union(unnamed.flatMap(([, t]) => [...t.keys()])))}`] : []),
        ];
        lines.push(
            `/** ${kind}. ${w ? `Seen run ${waited + same} times: ${waited ? `waited ${waited}` : "never waited"}.` : "Not seen run."} */`,
        );
        lines.push(`export declare function ${identifier(nameOf(cmd))}(${params.join(", ")}): Command;`);
    }

    // The report: one type or several, by kind of site.
    const all: Counts = new Map(recorded);
    const kinds = new Map<string, { sites: number; single: number; numbers: number; ints: number }>();
    const several: string[] = [];
    for (const [site, types] of all) {
        const kind = site.split(" ")[0]!;
        if (kind === "wait") continue;
        const k = kinds.get(kind) ?? { sites: 0, single: 0, numbers: 0, ints: 0 };
        ++k.sites;
        if (types.size === 1) ++k.single;
        const pairs = [...types.keys()].map((t) => t.split(","));
        if (pairs.every((p) => p.every((t) => t === "int" || t === "float"))) ++k.numbers;
        if (pairs.every((p) => p.every((t) => t === "int"))) ++k.ints;
        kinds.set(kind, k);
        if (types.size > 1) {
            const parts = site.split(" ");
            const named = parts.map((p, i) =>
                i && /^[0-9a-f]{8}$/.test(p)
                    ? nameOf(p)
                    : p.replace(/^p\.([0-9a-f]{8})$/, (_m, h: string) => `p.${nameOf(h)}`),
            );
            several.push(`- \`${named.join(" ")}\`: ${[...types].map(([t, n]) => `${t} ${n}`).join(", ")}`);
        }
    }
    const kindNames: Record<string, string> = {
        param: "parameter reads (p.x as a value)",
        name: "parameters read as a command's / object's name",
        arg: "command arguments",
        global: "global reads (G.x)",
        test: "if conditions on an expression",
        is: "switch comparisons",
        op5: "index ([])",
        op7: "==",
        op8: "member (.)",
        op10: "-",
        op11: "+",
        op12: "/",
        op13: "*",
        op18: "<",
        op20: ">",
        op50: "OR",
        op51: "AND",
    };
    const table = [...kinds]
        .sort((a, b) => b[1].sites - a[1].sites)
        .map(
            ([kind, k]) =>
                `| ${kindNames[kind] ?? kind} | ${k.sites} | ${k.single} | ${kind.startsWith("op") || kind === "is" ? `${k.numbers} / ${k.ints}` : "-"} |`,
        );
    let scriptReads = 0;
    for (const set of reads.values()) scriptReads += set.size;
    const report = [
        "# Types the scripts see",
        "",
        "Generated by `tools/qbjs/types.ts` from the .qb files and the game's recording (`[debug] ts_types=1`).",
        "",
        `- Modules: ${modules.length}; scripts: ${scripts.size}; parameters they read: ${scriptReads}.`,
        `- Commands called: ${commands.size}; seen run: ${seenRunning}; of those never seen waiting: ${neverWaited}.`,
        "",
        "| Sites (recorded) | Seen | One type | Only numbers / only integers |",
        "| --- | --- | --- | --- |",
        ...table,
        "",
        "## Sites seen with more than one type",
        "",
        ...(several.length ? several.sort() : ["(none)"]),
        "",
    ];
    return {
        dts: `${lines.join("\n")}\n`,
        report: report.join("\n"),
        commands: commands.size,
        neverWaited,
        seenRunning,
        kinds,
    };
}

const walk = (path: string): string[] =>
    !existsSync(path)
        ? []
        : statSync(path).isDirectory()
          ? readdirSync(path).flatMap((name) => walk(join(path, name)))
          : path.toLowerCase().endsWith(".qb")
            ? [path]
            : [];

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
    const args = process.argv.slice(2);
    const value = (flag: string) => {
        const i = args.indexOf(flag);
        return i >= 0 ? args[i + 1] : undefined;
    };
    const game = value("--game");
    if (!game) throw Error("Usage: node tools/qbjs/types.ts --game <folder> [--out <folder>]");
    const root = gameRoot(game);
    const out = value("--out") ?? join(root, "thug2-decomp-ts", "types");
    const overlays = readdirSync(root).filter(
        (name) => name.toLowerCase().startsWith("thug2-decomp-") && name.toLowerCase() !== "thug2-decomp-ts",
    );
    const modules: ModuleIR[] = [];
    for (const file of [
        ...walk(join(root, "Data", "scripts")),
        ...walk(join(root, "Data", "levels")),
        ...overlays.flatMap((name) => walk(join(root, name))),
    ]) {
        try {
            modules.push(readIR(readFileSync(file), relative(root, file)));
        } catch {
            // (not translated: stays QB - install_ts.ts lists them)
        }
    }
    const recorded = readRecorded(join(root, "thug2-decomp-ts", "types-recorded.json"));
    const result = typesReport(modules, recorded);
    mkdirSync(out, { recursive: true });
    writeFileSync(join(out, "commands.d.ts"), result.dts);
    writeFileSync(join(out, "report.md"), result.report);
    console.log(`${result.commands} commands (${result.seenRunning} seen run, ${result.neverWaited} never waiting)`);
    for (const [kind, k] of result.kinds) console.log(`${kind}: ${k.sites} sites seen, ${k.single} with one type`);
    console.log(`written: ${join(out, "commands.d.ts")}, ${join(out, "report.md")}`);
}
