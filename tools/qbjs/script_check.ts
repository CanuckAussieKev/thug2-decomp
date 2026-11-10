// NOT ORIGINAL - the script check's plan and its verdict (src/script_qb/script_check.h, docs/script-check.md): every
// script run headless on the game's own interpreter, each branch forced in turn, as QB and as TypeScript.
//
//   node tools/qbjs/script_check.ts plan --game <folder> [--only <path>[,...]] [--runs N] [--frames N]
//       writes <game>\thug2-decomp-check: plan.qb (the modules, the budgets) and one part per module (its scripts and
//       the argument sets to call each with: none, then up to three of the literal ones its callers pass)
//   then, in that game folder (a workspace, under its lock): thug2-decomp.exe -script-check <that folder>, once with
//       [features] ts_scripts=0 (qb.txt) and once with ts_scripts=1 (ts.txt) - tools/game/script_check.ps1
//   node tools/qbjs/script_check.ts compare --game <folder>
//       compares the runs (every call, its arguments and result, the frame it came in, how each run ended and the
//       script's final parameters) and reports the branches the QB runs took against every branch the scripts have:
//       report.md and report.json beside.
import {
    createReadStream,
    existsSync,
    mkdirSync,
    readdirSync,
    readFileSync,
    rmSync,
    statSync,
    writeFileSync,
} from "node:fs";
import { join, relative } from "node:path";
import { createInterface } from "node:readline";
import { pathToFileURL } from "node:url";
import { checksum } from "../qb_checksum.ts";
import { gameRoot } from "./install_ts.ts";
import type { Definition, Expr, Field, ModuleIR, Statement } from "./ir.ts";
import { readIR } from "./reader.ts";

const hex = (n: number) => (n >>> 0).toString(16).padStart(8, "0");

// --- QB bytes for the plan ------------------------------------------------------------------------------------------

const u32 = (n: number) => [n & 255, (n >>> 8) & 255, (n >>> 16) & 255, (n >>> 24) & 255];
const nameBytes = (n: number) => [0x16, ...u32(n)];
const stringBytes = (s: string) => {
    const bytes = [...Buffer.from(s, "latin1"), 0];
    return [0x1b, ...u32(bytes.length), ...bytes];
};
const intBytes = (n: number) => [0x17, ...u32(n >>> 0)];

// A literal value's tokens (undefined: not a literal - a parameter, an expression...).
export function literalBytes(x: Expr): number[] | undefined {
    switch (x.kind) {
        case "literal": {
            const v = x.value;
            if (v.type === 1 || v.type === 2 || v.type === 13)
                return [v.type === 1 ? 0x17 : v.type === 2 ? 0x1a : 0x16, ...u32(v.bits)];
            if (v.type === 3 || v.type === 4)
                return [v.type === 3 ? 0x1b : 0x1c, ...u32(v.bytes.length + 1), ...v.bytes, 0];
            return "coordinates" in v ? [v.type === 6 ? 0x1e : 0x1f, ...v.coordinates.flatMap(u32)] : undefined;
        }
        case "structure": {
            const inner = fieldsBytes(x.fields);
            return inner && [3, ...inner, 4];
        }
        case "array": {
            const items = x.items.map(literalBytes);
            return items.every((i) => i) ? [5, ...items.flatMap((i) => [...i!, 1]), 6] : undefined;
        }
        default:
            return undefined;
    }
}
export function fieldsBytes(fields: Field[]): number[] | undefined {
    const out: number[] = [];
    for (const f of fields) {
        if (!("value" in f)) return undefined;
        const value = literalBytes(f.value);
        if (!value) return undefined;
        out.push(...(f.name ? [...nameBytes(f.name), 7] : []), ...value, 1);
    }
    return out;
}

// --- the plan -------------------------------------------------------------------------------------------------------

const walk = (path: string): string[] =>
    !existsSync(path)
        ? []
        : statSync(path).isDirectory()
          ? readdirSync(path).flatMap((n) => walk(join(path, n)))
          : path.toLowerCase().endsWith(".qb")
            ? [path]
            : [];

export type PlanModule = { file: string; source: string; ir: ModuleIR };

// The modules the game would load by these names: Data\scripts and Data\levels, the data overlay's own version where
// it has one (the game's file system looks there first), and the overlay's levels the Data folder doesn't have.
export function planModules(root: string, only: string[] = []): PlanModule[] {
    const overlay = join(root, "thug2-decomp-data");
    const names = new Map<string, string>(); // game path (lower case) -> source file
    for (const base of [join(root, "Data"), overlay])
        for (const sub of ["scripts", "levels"])
            for (const file of walk(join(base, sub))) names.set(relative(base, file).toLowerCase(), file);
    const modules: PlanModule[] = [];
    for (const [key, source] of [...names].sort((a, b) => a[0].localeCompare(b[0]))) {
        const file = key.replace(/\//g, "\\");
        if (only.length && !only.some((o) => o.toLowerCase().replace(/\//g, "\\") === file)) continue;
        try {
            modules.push({ file, source, ir: readIR(readFileSync(source), relative(root, source)) });
        } catch {
            // (not translated: stays QB, nothing to compare)
        }
    }
    return modules;
}

// Every call's literal argument sets, by the script called (all modules).
export function callerArguments(modules: PlanModule[]): Map<number, number[][]> {
    const sets = new Map<number, Map<string, number[]>>();
    const statements = (xs: Statement[]): void => {
        for (const x of xs) {
            if (x.kind === "call" && x.name && x.via === undefined && x.byExpr === undefined) {
                const bytes = fieldsBytes(x.fields);
                if (bytes?.length) {
                    let m = sets.get(x.name);
                    if (!m) {
                        m = new Map();
                        sets.set(x.name, m);
                    }
                    m.set(bytes.join(","), bytes);
                }
            }
            if (x.kind === "if") {
                if (x.condition.kind === "call") statements([x.condition]);
                statements(x.yes);
                statements(x.no);
            } else if (x.kind === "loop") statements(x.body);
            else if (x.kind === "randomBlock") x.branches.forEach(statements);
            else if (x.kind === "switch") {
                for (const c of x.cases) statements(c.body);
                if (x.fallback) statements(x.fallback);
            }
        }
    };
    for (const m of modules) for (const d of m.ir.definitions) if (d.kind === "script") statements(d.body);
    const out = new Map<number, number[][]>();
    for (const [name, m] of sets) out.set(name, [...m.values()].sort((a, b) => a.length - b.length).slice(0, 3));
    return out;
}

export function writePlan(folder: string, modules: PlanModule[], settings: Record<string, number>) {
    rmSync(folder, { recursive: true, force: true });
    mkdirSync(folder, { recursive: true });
    const args = callerArguments(modules);
    const entries: number[] = [];
    modules.forEach((m, i) => {
        const part = `m${i}.qb`;
        const scripts: number[] = [];
        for (const d of m.ir.definitions) {
            if (d.kind !== "script") continue;
            const sets = [[] as number[], ...(args.get(d.name) ?? [])];
            scripts.push(
                3,
                ...nameBytes(checksum("name")),
                7,
                ...nameBytes(d.name),
                1,
                ...nameBytes(checksum("args")),
                7,
                5,
                ...sets.flatMap((s) => [3, ...s, 4, 1]),
                6,
                1,
                4,
                1,
            );
        }
        writeFileSync(
            join(folder, part),
            Uint8Array.from([...nameBytes(checksum("check_scripts")), 7, 5, ...scripts, 6, 1, 0]),
        );
        entries.push(
            3,
            ...nameBytes(checksum("file")),
            7,
            ...stringBytes(m.file),
            1,
            ...nameBytes(checksum("load")),
            7,
            ...intBytes(1),
            1,
            ...nameBytes(checksum("part")),
            7,
            ...stringBytes(part),
            1,
            4,
            1,
        );
    });
    const settingBytes = Object.entries(settings).flatMap(([k, v]) => [
        ...nameBytes(checksum(k)),
        7,
        ...intBytes(v),
        1,
    ]);
    writeFileSync(
        join(folder, "plan.qb"),
        Uint8Array.from([
            ...nameBytes(checksum("check_plan")),
            7,
            5,
            ...entries,
            6,
            1,
            ...nameBytes(checksum("check_settings")),
            7,
            3,
            ...settingBytes,
            4,
            1,
            0,
        ]),
    );
    writeFileSync(
        join(folder, "modules.json"),
        JSON.stringify(modules.map((m) => ({ file: m.file, source: m.source }))),
    );
}

// --- the verdict ----------------------------------------------------------------------------------------------------

// ts.txt (the TypeScript pass compares each run with qb.txt itself): per module its run count, how many were the same,
// and each that differed with its first differing line on each side.
export type Verdict = {
    runs: number;
    same: number;
    differences: { module: string; run: string; qb: string; ts: string }[];
};
export function readVerdict(text: string): Verdict {
    const out: Verdict = { runs: 0, same: 0, differences: [] };
    let module = "";
    const lines = text.split("\n");
    for (let i = 0; i < lines.length; ++i) {
        const line = lines[i]!;
        if (line.startsWith("module ")) module = line.slice(7);
        else if (line.startsWith("runs ")) {
            const [, runs, , same] = line.split(" ");
            out.runs += Number(runs);
            out.same += Number(same);
        } else if (line.startsWith("differ ")) {
            out.differences.push({
                module,
                run: line.slice(7),
                qb: (lines[i + 1] ?? "").replace(/^ {2}qb /, ""),
                ts: (lines[i + 2] ?? "").replace(/^ {2}ts /, ""),
            });
            i += 2;
        }
    }
    return out;
}

// The branches qb.txt's runs took, by module (qb.txt runs to gigabytes: read a line at a time).
export async function readCoverage(path: string): Promise<Map<string, Set<string>>> {
    const out = new Map<string, Set<string>>();
    let current = new Set<string>();
    const lines = createInterface({ input: createReadStream(path), crlfDelay: Number.POSITIVE_INFINITY });
    for await (const line of lines) {
        if (line.startsWith("module ")) {
            current = new Set();
            out.set(line.slice(7), current);
        } else if (line.startsWith("cover ")) current.add(line.slice(6));
    }
    return out;
}

// Every branch a script has: "if <place> 0|1", "switch <place> <case place|-1>", "random <place> <choice>" - places
// in the script's code (its offset in the file + 6, as the interpreter's cursor counts).
export function branches(d: Extract<Definition, { kind: "script" }>): string[] {
    const base = d.offset + 6;
    const out: string[] = [];
    const randomValues = (x: Expr | undefined): void => {
        if (!x) return;
        if (x.kind === "randomChoice")
            for (let i = 0; i < x.choices.length; ++i) out.push(`random ${x.offset - base} ${i}`);
        if (x.kind === "randomChoice") x.choices.forEach(randomValues);
        if (x.kind === "binary") [x.left, x.right].forEach(randomValues);
        if (x.kind === "expressionResult") randomValues(x.value);
        if (x.kind === "structure" || x.kind === "functionCall") fields(x.fields);
        if (x.kind === "array") x.items.forEach(randomValues);
    };
    const fields = (fs: Field[]) => {
        for (const f of fs) {
            if ("kind" in f) {
                for (let i = 0; i < f.branches.length; ++i) out.push(`random ${f.offset - base} ${i}`);
                f.branches.forEach(fields);
            } else if ("value" in f) randomValues(f.value);
        }
    };
    const statements = (xs: Statement[]): void => {
        for (const x of xs) {
            if (x.kind === "if") {
                out.push(`if ${x.offset - base} 1`, `if ${x.offset - base} 0`);
                if (x.condition.kind === "call") fields(x.condition.fields);
                statements(x.yes);
                statements(x.no);
            } else if (x.kind === "switch") {
                for (const c of x.cases)
                    for (const l of c.labels) out.push(`switch ${x.offset - base} ${l.offset - base}`);
                if (x.fallback === undefined) out.push(`switch ${x.offset - base} -1`);
                for (const c of x.cases) statements(c.body);
                if (x.fallback) statements(x.fallback);
            } else if (x.kind === "randomBlock") {
                for (let i = 0; i < x.branches.length; ++i) out.push(`random ${x.offset - base} ${i}`);
                x.branches.forEach(statements);
            } else if (x.kind === "loop") statements(x.body);
            else if (x.kind === "call" || x.kind === "return") fields(x.fields);
            else if (x.kind === "assign") randomValues(x.value);
        }
    };
    statements(d.body);
    return out;
}

export function coverageOf(cover: Map<string, Set<string>>, modules: PlanModule[]) {
    // Coverage: every branch of every script against those the QB runs took.
    const coverage: { module: string; script: string; branches: number; taken: number; missing: string[] }[] = [];
    for (const m of modules) {
        const taken = cover.get(m.file) ?? new Set<string>();
        for (const d of m.ir.definitions) {
            if (d.kind !== "script") continue;
            const all = branches(d);
            const name = hex(d.name);
            const missing = all.filter((b) => {
                const [kind, place, value] = b.split(" ");
                return !taken.has(`${kind} ${name} ${place} ${value}`);
            });
            coverage.push({
                module: m.file,
                script: m.ir.names?.[d.name] ?? `_0x${name}`,
                branches: all.length,
                taken: all.length - missing.length,
                missing,
            });
        }
    }
    return coverage;
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
    const [command, ...args] = process.argv.slice(2);
    const value = (flag: string) => {
        const i = args.indexOf(flag);
        return i >= 0 ? args[i + 1] : undefined;
    };
    const game = value("--game");
    if (!game || (command !== "plan" && command !== "compare"))
        throw Error("Usage: node tools/qbjs/script_check.ts plan|compare --game <folder> [--only a.qb,b.qb]");
    const root = gameRoot(game);
    const folder = join(root, "thug2-decomp-check");
    const only = (value("--only") ?? "").split(",").filter(Boolean);
    if (command === "plan") {
        const modules = planModules(root, only);
        writePlan(folder, modules, {
            runs: Number(value("--runs") ?? 64),
            frames: Number(value("--frames") ?? 120),
            calls: Number(value("--calls") ?? 300),
            steps: Number(value("--steps") ?? 200000),
        });
        console.log(
            `plan: ${modules.length} modules, ${modules.reduce((n, m) => n + m.ir.definitions.filter((d) => d.kind === "script").length, 0)} scripts -> ${folder}`,
        );
    } else {
        const files = JSON.parse(readFileSync(join(folder, "modules.json"), "utf8")) as {
            file: string;
            source: string;
        }[];
        const modules = files.map((f) => ({ ...f, ir: readIR(readFileSync(f.source), relative(root, f.source)) }));
        const verdict = readVerdict(
            existsSync(join(folder, "ts.txt")) ? readFileSync(join(folder, "ts.txt"), "utf8") : "",
        );
        const result = { ...verdict, coverage: coverageOf(await readCoverage(join(folder, "qb.txt")), modules) };
        const total = result.coverage.reduce((n, c) => n + c.branches, 0);
        const taken = result.coverage.reduce((n, c) => n + c.taken, 0);
        const report = [
            "# Script check",
            "",
            `Runs: ${result.runs}; the same as QB and TypeScript: ${result.same}; different: ${result.differences.length}.`,
            `Branches: ${taken} of ${total} taken by the QB runs (${total ? ((100 * taken) / total).toFixed(1) : "0"}%).`,
            "",
            "## Differences (first line that differs)",
            "",
            ...(result.differences.length
                ? result.differences
                      .slice(0, 500)
                      .map((d) => `- ${d.module} \`${d.run}\`:\n  - QB: \`${d.qb}\`\n  - TS: \`${d.ts}\``)
                : ["(none)"]),
            "",
            "## Branches not taken",
            "",
            ...result.coverage
                .filter((c) => c.missing.length)
                .map((c) => `- ${c.module} ${c.script}: ${c.taken} / ${c.branches} - ${c.missing.join("; ")}`),
            "",
        ].join("\n");
        writeFileSync(join(folder, "report.md"), report);
        writeFileSync(join(folder, "report.json"), JSON.stringify(result, null, 1));
        console.log(report.split("\n").slice(0, 5).join("\n"));
        console.log(`written: ${join(folder, "report.md")}`);
    }
}
