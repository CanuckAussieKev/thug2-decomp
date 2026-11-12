// NOT ORIGINAL - the script check's plan and its verdict (src/script_qb/script_check.h, docs/script-check.md): every
// script run headless on the game's own interpreter, each branch forced in turn, as QB and as TypeScript.
//
//   node tools/qbjs/script_check.ts plan --game <folder> [--only <path>[,...]] [--runs N] [--frames N]
//       writes <game>\thug2-decomp-check: plan.qb (the modules, the budgets) and one part per module (its scripts and
//       the argument sets to call each with: none, then up to three of the literal ones its callers pass)
//   then, in that game folder (a workspace, under its lock): thug2-decomp.exe -script-check <that folder>, once with
//       [features] ts_scripts=0 (qb.txt) and once with ts_scripts=1 in the game unpacked (ts.txt) - tools/game/script_check.ps1
//   node tools/qbjs/script_check.ts compare --game <folder>
//       compares the runs (every call, its arguments and result, the frame it came in, how each run ended and the
//       script's final parameters) and reports the branches the QB runs took against every branch the scripts have:
//       report.md and report.json beside.
import { createReadStream, existsSync, mkdirSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { dirname, join, relative } from "node:path";
import { createInterface } from "node:readline";
import { pathToFileURL } from "node:url";
import { checksum } from "../qb_checksum.ts";
import { fileHash, gameRoot, type QbVersion, qbVersions, unpatchedOrigin } from "./game_scripts.ts";
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

// A module: its game path, the version planned from (source: a file - an archive's copy is written out first) and
// every version of it the game may load (the run reports which: its bytes' hash).
export type PlanModule = {
    file: string;
    source: string;
    ir: ModuleIR;
    versions: PlanVersion[];
};
// A version: where it comes from, its bytes' hash, its bytes (written out) and where tools/qbjs/unpack_game.ts puts its
// TypeScript - the .ts the game reads for it with ts_scripts=1.
export type PlanVersion = { origin: string; hash: string; source: string; ts: string };

// The .ts unpack_game.ts makes for game file `file` read from `origin` (Data, a Data\pre archive, an overlay's folder).
export function versionTS(root: string, origin: string, file: string): string {
    const from = unpatchedOrigin(origin);
    const ts = file.replace(/\.qb$/i, ".ts").split("\\");
    if (/\.(prx|pre)$/i.test(from)) return join(root, "Data", "pre", from.slice(0, -4), ...ts);
    return join(root, ...from.split("\\"), ...ts);
}

// The modules the game would load by these names: every .qb of Data (its loose files and its archives' copies) and of
// the data overlay. A module is planned from the version the game prefers - the overlay's, else an archive's, else the
// loose file -; every version is written out (<game>\thug2-decomp-check-sources) for the verdict, which reads the one
// the game ran.
export function planModules(root: string, only: string[] = []): PlanModule[] {
    const sources = join(root, "thug2-decomp-check-sources");
    rmSync(sources, { recursive: true, force: true });
    // (a patched version - game_scripts.ts qbVersions - before the file it was made from: the game loads it patched)
    const rank = (patched: string) => {
        const origin = unpatchedOrigin(patched);
        const base = origin.startsWith("thug2-decomp-data\\")
            ? -1
            : origin === "thug2-decomp-data"
              ? 0
              : origin.toLowerCase().endsWith(".prx")
                ? 1
                : 2;
        return base * 2 - (origin === patched ? 0 : 1);
    };
    // (the overlay's own sets - the new front end's, Anteara Mod's - are read before the rest when the ini turns them
    // on: a version of the file they stand in for)
    const ini = existsSync(join(root, "thug2-decomp.ini")) ? readFileSync(join(root, "thug2-decomp.ini"), "utf8") : "";
    const on = (key: string) => new RegExp(`^${key}=[1-9]`, "m").test(ini);
    const sets = { "front_end\\": on("new_frontend"), "anteara\\": on("anteara_mod") };
    const byName = new Map<string, QbVersion[]>();
    for (let v of qbVersions(root)) {
        const origin = unpatchedOrigin(v.origin);
        if (origin !== "Data" && origin !== "thug2-decomp-data" && !origin.toLowerCase().endsWith(".prx")) continue;
        const set = Object.keys(sets).find((s) => v.name.startsWith(s)) as keyof typeof sets | undefined;
        if (set) {
            if (!sets[set] || v.origin !== "thug2-decomp-data") continue;
            v = { ...v, name: v.name.slice(set.length), origin: `thug2-decomp-data\\${set.slice(0, -1)}` };
        }
        byName.set(v.name, [...(byName.get(v.name) ?? []), v]);
    }
    const modules: PlanModule[] = [];
    for (const [file, all] of [...byName].sort((a, b) => a[0].localeCompare(b[0]))) {
        if (only.length && !only.some((o) => o.toLowerCase().replace(/\//g, "\\") === file)) continue;
        const versions: PlanVersion[] = [];
        const seen = new Set<string>();
        // (a file the game always patches - keyboard.qb - is never run unpatched where its patched version is)
        const patched = new Set(
            all.filter((v) => v.origin !== unpatchedOrigin(v.origin)).map((v) => unpatchedOrigin(v.origin)),
        );
        for (const v of [...all].sort((a, b) => rank(a.origin) - rank(b.origin))) {
            if (patched.has(v.origin)) continue;
            // (an empty version - qb_scripts.prx's car_scripts.qb: its end alone, 2 bytes - defines nothing: the game
            // reads none of it; skipped where the file has another)
            if (v.bytes.length < 4 && all.some((o) => o.bytes.length >= 4)) continue;
            const hash = fileHash(v.bytes);
            if (seen.has(hash)) continue;
            seen.add(hash);
            // (every version's bytes written out: an unpacked game - the TypeScript pass's - has no .qb left)
            const source = join(sources, v.origin, file);
            mkdirSync(dirname(source), { recursive: true });
            writeFileSync(source, v.bytes);
            versions.push({ origin: v.origin, hash, source, ts: versionTS(root, v.origin, file) });
        }
        const primary = versions[0]!;
        try {
            modules.push({
                file,
                source: primary.source,
                ir: readIR(readFileSync(primary.source), file),
                versions,
            });
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

// The shares of several workspaces running one plan at once: every module any of them has goes to one of them that has
// it (their data overlays differ), each share about as many scripts. Written to each workspace's
// thug2-decomp-check-share.txt (a module's game path a line), which plan --share reads.
export function writeShares(roots: string[]) {
    const lists = roots.map((r) => planModules(r));
    const weight = new Map<string, number>();
    for (const list of lists)
        for (const m of list) weight.set(m.file, m.ir.definitions.filter((d) => d.kind === "script").length + 1);
    const load = roots.map(() => 0);
    const shares = roots.map(() => [] as string[]);
    for (const [file, w] of [...weight].sort((a, b) => b[1] - a[1] || a[0].localeCompare(b[0]))) {
        let best = -1;
        lists.forEach((list, i) => {
            if (list.some((m) => m.file === file) && (best < 0 || (load[i] ?? 0) < (load[best] ?? 0))) best = i;
        });
        load[best] = (load[best] ?? 0) + w;
        shares[best]?.push(file);
    }
    roots.forEach((r, i) => {
        writeFileSync(join(r, "thug2-decomp-check-share.txt"), `${(shares[i] ?? []).sort().join("\n")}\n`);
    });
    return shares.map((s, i) => ({
        root: roots[i],
        modules: s.length,
        scripts: load[i],
    }));
}

export function writePlan(folder: string, all: PlanModule[], settings: Record<string, number>, share?: Set<string>) {
    rmSync(folder, { recursive: true, force: true });
    mkdirSync(folder, { recursive: true });
    // (a share's modules only - their argument sets still from every module's calls)
    const args = callerArguments(all);
    const shared = share ? all.filter((m) => share.has(m.file)) : all;
    // (a file with more than one version: each version run too, as it is - <file>@<hash>, its own part)
    type Entry = PlanModule & { version?: PlanVersion };
    const modules: Entry[] = shared.flatMap((m) => [
        m,
        ...(m.versions.length < 2
            ? []
            : m.versions.flatMap((v) => {
                  try {
                      return [{ ...m, ir: readIR(readFileSync(v.source), m.file), source: v.source, version: v }];
                  } catch {
                      return []; // (not translated: stays QB, nothing to compare)
                  }
              })),
    ]);
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
            ...(m.version
                ? [
                      ...nameBytes(checksum("version")),
                      7,
                      ...stringBytes(m.version.hash),
                      1,
                      ...nameBytes(checksum("qb")),
                      7,
                      ...stringBytes(m.version.source),
                      1,
                      ...nameBytes(checksum("ts")),
                      7,
                      ...stringBytes(m.version.ts),
                      1,
                  ]
                : []),
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
        JSON.stringify(
            modules.map((m) =>
                m.version
                    ? { file: `${m.file}@${m.version.hash}`, source: m.version.source, versions: [m.version] }
                    : { file: m.file, source: m.source, versions: m.versions },
            ),
        ),
    );
}

// --- the verdict ----------------------------------------------------------------------------------------------------

// ts.txt (the TypeScript pass compares each run with qb.txt itself): per module its run count, how many were the same,
// and each that differed with its first differing line on each side.
export type Verdict = {
    runs: number;
    same: number;
    exhausted: number; // QB's runs that filled a script pool (not replayed)
    withoutTypeScript: string[]; // modules the TypeScript pass ran as QB (no module for the version loaded): untested
    differences: { module: string; run: string; qb: string; ts: string }[];
};
export function readVerdict(text: string): Verdict {
    const out: Verdict = {
        runs: 0,
        same: 0,
        exhausted: 0,
        withoutTypeScript: [],
        differences: [],
    };
    let module = "";
    const lines = text.split("\n");
    for (let i = 0; i < lines.length; ++i) {
        const line = lines[i]!;
        if (line.startsWith("module ")) module = line.slice(7);
        else if (line.startsWith("loaded ") && line.endsWith(" no-typescript")) out.withoutTypeScript.push(module);
        else if (line.startsWith("runs ")) {
            const [, runs, , same, , exhausted] = line.split(" ");
            out.runs += Number(runs);
            out.same += Number(same);
            out.exhausted += Number(exhausted ?? 0);
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
    const lines = createInterface({
        input: createReadStream(path),
        crlfDelay: Number.POSITIVE_INFINITY,
    });
    for await (const line of lines) {
        if (line.startsWith("module ")) {
            current = new Set();
            out.set(line.slice(7), current);
        } else if (line.startsWith("cover ")) current.add(line.slice(6));
        else if (line.startsWith("loaded "))
            current.add(line); // (the version the game ran: "loaded <hash>")
        else if (line.startsWith("global ")) current.add(line); // (a global the file defined: "global <name> <len> <hash>")
    }
    return out;
}

// Every branch a script has: "if <place> 0|1", "switch <place> <arm>" (its case labels in order, then one more: the
// default or none), "random <place> <choice>" - places in the script's code (its offset in the file + 6, as the
// interpreter's cursor counts).
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
                const labels = x.cases.reduce((n, c) => n + c.labels.length, 0);
                for (let arm = 0; arm <= labels; ++arm) out.push(`switch ${x.offset - base} ${arm}`);
                for (const c of x.cases) statements(c.body);
                if (x.fallback) statements(x.fallback);
            } else if (x.kind === "randomBlock") {
                for (let i = 0; i < x.branches.length; ++i) out.push(`random ${x.offset - base} ${i}`);
                x.branches.forEach(statements);
            } else if (x.kind === "loop") {
                // (a loop with a count: each pass's end goes round again or out)
                if (x.repeatFields.length)
                    out.push(`repeat ${x.repeatOffset - base} 1`, `repeat ${x.repeatOffset - base} 0`);
                statements(x.body);
            } else if (x.kind === "call" || x.kind === "return") fields(x.fields);
            else if (x.kind === "assign") randomValues(x.value);
        }
    };
    statements(d.body);
    return out;
}

// Every statement a script has, by its place (as branches()).
export function statementPlaces(d: Extract<Definition, { kind: "script" }>): number[] {
    const base = d.offset + 6;
    const out: number[] = [];
    const statements = (xs: Statement[]): void => {
        for (const x of xs) {
            out.push(x.offset - base);
            if (x.kind === "if") {
                statements(x.yes);
                statements(x.no);
            } else if (x.kind === "switch") {
                for (const c of x.cases) statements(c.body);
                if (x.fallback) statements(x.fallback);
            } else if (x.kind === "randomBlock") x.branches.forEach(statements);
            else if (x.kind === "loop") statements(x.body);
        }
    };
    statements(d.body);
    return out;
}

// The statements no run can reach, the original's or ours: those after a return, or after a loop that never ends (no
// count, no break out of it that can run - one after a return isn't) - in the same block.
export function unreachable(d: Extract<Definition, { kind: "script" }>): Statement[] {
    const dead: Statement[] = [];
    const breaksOut = (xs: Statement[], loop: number): boolean =>
        xs.some((x) =>
            dead.includes(x)
                ? false
                : x.kind === "break"
                  ? x.target === loop
                  : x.kind === "if"
                    ? breaksOut(x.yes, loop) || breaksOut(x.no, loop)
                    : x.kind === "switch"
                      ? x.cases.some((c) => breaksOut(c.body, loop)) || breaksOut(x.fallback ?? [], loop)
                      : x.kind === "randomBlock"
                        ? x.branches.some((b) => breaksOut(b, loop))
                        : x.kind === "loop"
                          ? breaksOut(x.body, loop)
                          : false,
        );
    // A block's statements: whether it can run on past its end.
    const block = (xs: Statement[]): boolean => {
        for (let i = 0; i < xs.length; ++i) {
            if (!statement(xs[i]!)) {
                dead.push(...xs.slice(i + 1));
                return false;
            }
        }
        return true;
    };
    const statement = (x: Statement): boolean => {
        switch (x.kind) {
            case "return":
            case "break":
                return false;
            case "if": {
                const yes = block(x.yes);
                const no = block(x.no);
                return yes || no;
            }
            case "switch": {
                const arms = x.cases.map((c) => block(c.body));
                const fallback = x.fallback ? block(x.fallback) : true;
                return arms.some(Boolean) || fallback;
            }
            case "randomBlock":
                return x.branches.map(block).some(Boolean);
            case "loop":
                block(x.body);
                return x.repeatFields.length > 0 || breaksOut(x.body, x.offset);
            default:
                return true;
        }
    };
    block(d.body);
    return dead;
}

// The statements in the arms of ifs whose code no run can reach ("if <place> <1: true / 0: false>").
export function fixedArmCode(d: Extract<Definition, { kind: "script" }>, arms: string[]): Statement[] {
    const base = d.offset + 6;
    const out: Statement[] = [];
    const statements = (xs: Statement[]): void => {
        for (const x of xs) {
            if (x.kind === "if") {
                const place = x.offset - base;
                if (arms.includes(`if ${place} 1`)) out.push(...x.yes);
                else statements(x.yes);
                if (arms.includes(`if ${place} 0`)) out.push(...x.no);
                else statements(x.no);
            } else if (x.kind === "switch") {
                for (const c of x.cases) statements(c.body);
                if (x.fallback) statements(x.fallback);
            } else if (x.kind === "randomBlock") x.branches.forEach(statements);
            else if (x.kind === "loop") statements(x.body);
        }
    };
    statements(d.body);
    return out;
}

export function coverageOf(cover: Map<string, Set<string>>, modules: PlanModule[]) {
    // Coverage: every branch of every script against those the QB runs took.
    const coverage: {
        module: string;
        script: string;
        branches: number;
        taken: number;
        missing: string[];
        statements: number;
        run: number;
        notRun: number[];
        unreachable: number;
        fixed: string[];
    }[] = [];
    for (const m of modules) {
        const taken = cover.get(m.file) ?? new Set<string>();
        for (const d of m.ir.definitions) {
            if (d.kind !== "script") continue;
            const name = hex(d.name);
            // (an if whose condition stops before its line's end - an expression the evaluator gave up on - isn't
            // forced: its true part would start mid-line; the arm its condition can't give is reported, not counted.
            // Its true part's code is as unreachable as after a return - the original never takes it, or goes on from
            // the middle of the line and stays at the expression's ')' for ever -, and so is a false part never taken)
            const fixedPlaces = new Set(
                branches(d)
                    .map((b) => b.split(" "))
                    .filter(([kind, place]) => kind === "if" && taken.has(`fixed ${name} ${place}`))
                    .map(([, place]) => Number(place)),
            );
            const fixedArms = branches(d).filter((b) => {
                const [kind, place] = b.split(" ");
                return kind === "if" && fixedPlaces.has(Number(place)) && !taken.has(`${b.replace(" ", ` ${name} `)}`);
            });
            const dead = [
                ...unreachable(d),
                ...fixedArmCode(d, [...fixedArms, ...[...fixedPlaces].map((place) => `if ${place} 1`)]),
            ];
            const deadBranches = new Set(branches({ ...d, body: dead }));
            const deadPlaces = new Set(statementPlaces({ ...d, body: dead }));
            const all = branches(d).filter((b) => !deadBranches.has(b) && !fixedArms.includes(b));
            const missing = all.filter((b) => {
                const [kind, place, value] = b.split(" ");
                return !taken.has(`${kind} ${name} ${place} ${value}`);
            });
            const places = statementPlaces(d).filter((place) => !deadPlaces.has(place));
            const notRun = places.filter((place) => !taken.has(`stmt ${name} ${place}`));
            coverage.push({
                module: m.file,
                script: m.ir.names?.[d.name] ?? `_0x${name}`,
                branches: all.length,
                taken: all.length - missing.length,
                missing,
                statements: places.length,
                run: places.length - notRun.length,
                notRun,
                unreachable: deadPlaces.size,
                fixed: fixedArms,
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
    if (!game || !["plan", "compare", "shares"].includes(command ?? ""))
        throw Error(
            "Usage: node tools/qbjs/script_check.ts plan|compare|shares --game <folder>[,<folder>...] [--only a.qb,b.qb] [--share]",
        );
    // (compare: several game folders - the shares of one plan run at once - give one report, in the first's folder)
    const roots = game.split(",").map((g) => gameRoot(g));
    const root = roots[0]!;
    const folder = join(root, "thug2-decomp-check");
    const only = (value("--only") ?? "").split(",").filter(Boolean);
    if (command === "shares") {
        for (const s of writeShares(roots)) console.log(`share: ${s.root}: ${s.modules} modules, ${s.scripts} scripts`);
    } else if (command === "plan") {
        const all = planModules(root, only);
        const share = args.includes("--share")
            ? new Set(readFileSync(join(root, "thug2-decomp-check-share.txt"), "utf8").split("\n").filter(Boolean))
            : undefined;
        const modules = share ? all.filter((m) => share.has(m.file)) : all;
        writePlan(
            folder,
            all,
            {
                runs: Number(value("--runs") ?? 64),
                frames: Number(value("--frames") ?? 300),
                calls: Number(value("--calls") ?? 600),
                steps: Number(value("--steps") ?? 50000),
                log_runs: args.includes("--log-runs") ? 1 : 0,
            },
            share,
        );
        console.log(
            `plan: ${modules.length} modules, ${modules.reduce((n, m) => n + m.ir.definitions.filter((d) => d.kind === "script").length, 0)} scripts -> ${folder}`,
        );
    } else {
        const results = [];
        for (const one of roots) {
            const at = join(one, "thug2-decomp-check");
            const files = JSON.parse(readFileSync(join(at, "modules.json"), "utf8")) as {
                file: string;
                source: string;
                versions: PlanVersion[];
            }[];
            const cover = await readCoverage(join(at, "qb.txt"));
            const tsText = existsSync(join(at, "ts.txt")) ? readFileSync(join(at, "ts.txt"), "utf8") : "";
            // (the TypeScript pass's loaded hashes: the same files - a QB pass from before the QB side noted them
            // says "loaded none")
            const tsLoaded = new Map<string, string>();
            for (const [, file, hash] of tsText.matchAll(/^module (.+)\r?\nloaded (\w+)/gm)) tsLoaded.set(file!, hash!);
            // (each module read in the version the game ran - its loaded hash; one we have no copy of is reported)
            const unknown: string[] = [];
            const modules = files.map((f) => {
                const qbLoaded = [...(cover.get(f.file) ?? [])].find((x) => x.startsWith("loaded "))?.split(" ")[1];
                const loaded = qbLoaded && qbLoaded !== "none" ? qbLoaded : (tsLoaded.get(f.file) ?? qbLoaded);
                const version = f.versions.find((v) => v.hash === loaded);
                if (loaded && loaded !== "none" && !version) unknown.push(`${f.file} (${loaded})`);
                const source = version?.source ?? f.source;
                return {
                    ...f,
                    source,
                    ir: readIR(readFileSync(source), relative(one, source)),
                };
            });
            const verdict = readVerdict(tsText);
            // TypeScript's own coverage: what its runs reached, from the check's translation's reach() and decisions
            // (ts.txt), against the same branches and statements - the ifs QB found fixed are fixed in both
            const tsCover = existsSync(join(at, "ts.txt"))
                ? await readCoverage(join(at, "ts.txt"))
                : new Map<string, Set<string>>();
            // (an if QB found fixed: the original stays on its line - the run ends there, TypeScript's at the call its
            // condition makes past QB's last: reaching the line is taking what QB took)
            for (const [file, lines] of cover)
                for (const line of lines)
                    if (line.startsWith("fixed ")) {
                        const set = tsCover.get(file) ?? new Set<string>();
                        tsCover.set(file, set.add(line));
                        const [, name, place] = line.split(" ");
                        if (set.has(`stmt ${name} ${place}`))
                            for (const arm of ["0", "1"])
                                if (lines.has(`if ${name} ${place} ${arm}`)) set.add(`if ${name} ${place} ${arm}`);
                    }
            // The files' globals, QB's (read from the .qb) against the TypeScript pass's (a standalone module's with
            // ts_scripts=1): each by its value's text, hashed
            const globalsOf = (lines: Set<string> | undefined) =>
                new Map(
                    [...(lines ?? [])]
                        .filter((l) => l.startsWith("global "))
                        .map((l) => {
                            const [, name, ...value] = l.split(" ");
                            return [name!, value.join(" ")] as const;
                        }),
                );
            let globals = 0;
            const globalDifferences: string[] = [];
            for (const [file, lines] of cover) {
                if (!tsCover.has(file)) continue;
                const qbGlobals = globalsOf(lines);
                const tsGlobals = globalsOf(tsCover.get(file));
                globals += qbGlobals.size;
                for (const name of new Set([...qbGlobals.keys(), ...tsGlobals.keys()]))
                    if (qbGlobals.get(name) !== tsGlobals.get(name))
                        globalDifferences.push(
                            `${file} ${name}: QB ${qbGlobals.get(name) ?? "none"}, TS ${tsGlobals.get(name) ?? "none"}`,
                        );
            }
            results.push({
                ...verdict,
                globals,
                globalDifferences,
                unknownVersions: unknown,
                coverage: coverageOf(cover, modules),
                tsCoverage: coverageOf(tsCover, modules),
            });
        }
        const result = {
            runs: results.reduce((n, r) => n + r.runs, 0),
            same: results.reduce((n, r) => n + r.same, 0),
            exhausted: results.reduce((n, r) => n + r.exhausted, 0),
            withoutTypeScript: results.flatMap((r) => r.withoutTypeScript),
            differences: results.flatMap((r) => r.differences),
            unknownVersions: results.flatMap((r) => r.unknownVersions),
            coverage: results.flatMap((r) => r.coverage),
            tsCoverage: results.flatMap((r) => r.tsCoverage),
            globals: results.reduce((n, r) => n + r.globals, 0),
            globalDifferences: results.flatMap((r) => r.globalDifferences),
        };
        const unknownVersions = result.unknownVersions;
        const total = result.coverage.reduce((n, c) => n + c.branches, 0);
        const taken = result.coverage.reduce((n, c) => n + c.taken, 0);
        const statements = result.coverage.reduce((n, c) => n + c.statements, 0);
        const ran = result.coverage.reduce((n, c) => n + c.run, 0);
        const scripts = result.coverage.length;
        const unreachableCount = result.coverage.reduce((n, c) => n + c.unreachable, 0);
        const fixedCount = result.coverage.reduce((n, c) => n + c.fixed.length, 0);
        const started = result.coverage.filter((c) => c.run > 0 || c.statements === 0).length;
        const percent = (a: number, b: number) => (b ? ((100 * a) / b).toFixed(2) : "0");
        const ts = result.tsCoverage;
        const tsTaken = ts.reduce((n, c) => n + c.taken, 0);
        const tsRan = ts.reduce((n, c) => n + c.run, 0);
        const tsStarted = ts.filter((c) => c.run > 0 || c.statements === 0).length;
        const report = [
            "# Script check",
            "",
            `Runs: ${result.runs}; the same as QB and TypeScript: ${result.same}; different: ${result.differences.length}; not replayed: ${result.exhausted} (QB's run filled a script pool - the game runs out there).`,
            `Modules the TypeScript pass ran as QB (no module for the version loaded - untested): ${result.withoutTypeScript.length}${result.withoutTypeScript.length ? ` - ${result.withoutTypeScript.join(", ")}` : ""}.`,
            `Modules whose loaded version isn't among the planned ones: ${unknownVersions.length}${unknownVersions.length ? ` - ${unknownVersions.join(", ")}` : ""}.`,
            `Branches: ${taken} of ${total} taken by the QB runs (${percent(taken, total)}%; forced where no input took them).`,
            `Statements: ${ran} of ${statements} run (${percent(ran, statements)}%); scripts run: ${started} of ${scripts}; ${unreachableCount} statements no run can reach (after a return or a loop that never ends, or in an arm its if can't take) not counted.`,
            `TypeScript (measured in its own runs, the same counts): branches ${tsTaken} of ${total} (${percent(tsTaken, total)}%); statements ${tsRan} of ${statements} (${percent(tsRan, statements)}%); scripts run: ${tsStarted} of ${scripts}.`,
            `Globals: ${result.globals} compared (QB's from the .qb files, the TypeScript pass's - from the .ts modules with ts_scripts=1); different: ${result.globalDifferences.length}.`,
            "",
            "## Differences (first line that differs)",
            "",
            ...(result.differences.length
                ? result.differences
                      .slice(0, 500)
                      .map((d) => `- ${d.module} \`${d.run}\`:\n  - QB: \`${d.qb}\`\n  - TS: \`${d.ts}\``)
                : ["(none)"]),
            "",
            "## Globals that differ",
            "",
            ...(result.globalDifferences.length
                ? result.globalDifferences.slice(0, 500).map((d) => `- ${d}`)
                : ["(none)"]),
            "",
            "## Branches not taken",
            "",
            ...result.coverage
                .filter((c) => c.missing.length)
                .map((c) => `- ${c.module} ${c.script}: ${c.taken} / ${c.branches} - ${c.missing.join("; ")}`),
            "",
            `## If arms not forced (${fixedCount}: the condition stops before its line's end - an expression the evaluator gave up on - so it comes out the same every time)`,
            "",
            ...result.coverage
                .filter((c) => c.fixed.length)
                .map((c) => `- ${c.module} ${c.script}: ${c.fixed.join("; ")}`),
            "",
            "## Statements not run",
            "",
            ...result.coverage
                .filter((c) => c.notRun.length)
                .map((c) => `- ${c.module} ${c.script}: ${c.run} / ${c.statements} - at ${c.notRun.join(", ")}`),
            "",
            "## Not reached by TypeScript (branches; statements)",
            "",
            ...ts
                .filter((c) => c.missing.length || c.notRun.length)
                .slice(0, 2000)
                .map(
                    (c) =>
                        `- ${c.module} ${c.script}: ${c.taken} / ${c.branches} - ${c.missing.join("; ")}; ${c.run} / ${c.statements} - at ${c.notRun.join(", ")}`,
                ),
            "",
        ].join("\n");
        writeFileSync(join(folder, "report.md"), report);
        writeFileSync(join(folder, "report.json"), JSON.stringify(result, null, 1));
        console.log(report.split("\n").slice(0, 10).join("\n"));
        console.log(`written: ${join(folder, "report.md")}`);
    }
}
