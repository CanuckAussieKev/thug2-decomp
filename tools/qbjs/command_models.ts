// NOT ORIGINAL - what each script command writes into the calling script's parameters, from our C++ (the models the
// type checker needs: tools/qbjs/typecheck.ts, docs/qb-types.md). The C functions of src/app/command_table.cpp are
// read mechanically:
//   - a function whose body never reaches the calling script (its `context`, the running script) has no outputs;
//   - one that only writes members under constant names (context.args()->put_int(checksums::Name{kX}, ...)) has
//     those, with their kinds;
//   - anything else (its context handed to a helper, a name taken from its arguments, a structure merged in) is left
//     for review: tools/qbjs/command_models_review.json lists each with why, and the reviewed models (written by hand
//     from the C++, each with its source) are kept in tools/qbjs/command_models_reviewed.json - they take precedence.
// Writes tools/qbjs/command_models.json (the merged models) and command_models_review.json (what's still open).
//
//   node tools/qbjs/command_models.ts
import { existsSync, readdirSync, readFileSync, statSync, writeFileSync } from "node:fs";
import { join, relative } from "node:path";
import { pathToFileURL } from "node:url";
import type { CommandModel } from "./typecheck.ts";

const SRC = join(import.meta.dirname, "..", "..", "src");
const OUT = import.meta.dirname;

// What each output call stores (the member's kind).
const WRITES: Record<string, string> = {
    put_int: "int",
    push_float: "float",
    put_float: "float",
    put_name_key: "name",
    append_word: "string",
    append_inline_text: "localstring",
    put_vec: "vector",
    put_vec3: "vector",
    insert_pair: "pair",
    append_struct_ref: "struct",
    put_sub_struct: "struct",
    append_array_ref: "array",
    append_array_value: "array",
    erase_module: "none",
    remove_flag: "none",
};

const walk = (p: string): string[] =>
    statSync(p).isDirectory() ? readdirSync(p).flatMap((n) => walk(join(p, n))) : /\.(cpp|h)$/.test(p) ? [p] : [];

// A function's body (the braces after its definition's name), or undefined.
function bodyOf(text: string, name: string): string | undefined {
    const at = new RegExp(String.raw`\bbool\s+${name}\s*\(\s*(script::)?Struct\s*\*`).exec(text);
    if (!at) return undefined;
    let i = text.indexOf("{", at.index);
    const start = i;
    let depth = 0;
    for (; i < text.length; ++i) {
        if (text[i] === "{") ++depth;
        else if (text[i] === "}" && --depth === 0) return text.slice(start, i + 1);
    }
    return undefined;
}
const stripComments = (s: string) => s.replace(/\/\*[\s\S]*?\*\//g, "").replace(/\/\/.*$/gm, "");

export type Extracted = {
    models: Record<string, CommandModel>;
    review: Record<string, { function: string; source?: string; why: string }>;
};

export function extract(): Extracted {
    // (the command table, and the commands our extensions add - the level editor, the new front end, THAW's BMX... -
    // registered the same way in their own files; a short function name is looked for in its own file first)
    const files = walk(SRC).map((f) => ({ file: f, text: readFileSync(f, "utf8") }));
    const entries = files.flatMap((home) =>
        [...home.text.matchAll(/\{\.name = "([^"]+)", \.function = &([\w:]+)\}/g)].map((m) => ({
            name: m[1]!,
            fn: m[2]!,
            home,
        })),
    );
    const models: Record<string, CommandModel> = {};
    const review: Extracted["review"] = {};
    // (a name registered twice - the editor plugin's and the stand-in without it: both models joined)
    const put = (name: string, model: CommandModel) => {
        models[name] = models[name] ? unionModel(models[name]!, model) : model;
    };
    for (const { name, fn, home } of entries) {
        const short = fn.split("::").at(-1)!;
        if (short === "command_true" || short === "command_false") {
            put(name, { outputs: {}, source: "src/app/command_table.cpp (a stub: true / false)" });
            continue;
        }
        const found =
            bodyOf(home.text, short) !== undefined ? home : files.find((f) => bodyOf(f.text, short) !== undefined);
        if (!found) {
            review[name] = { function: fn, why: "its definition wasn't found" };
            continue;
        }
        const body = stripComments(bodyOf(found.text, short)!);
        const source = `${relative(join(SRC, ".."), found.file).replace(/\\/g, "/")} ${short}`;
        // every use of the context (or the running script) must be an output under a constant name
        const contextUses = [...body.matchAll(/\bcontext\b[^;]*/g)].map((m) => m[0]);
        const running = /get_running_script|current_script_var|running_script\(/.test(body);
        if (!contextUses.length && !running) {
            put(name, { outputs: {}, source });
            continue;
        }
        const outputs: Record<string, string> = {};
        let open: string | undefined = running ? "reaches the running script another way" : undefined;
        for (const use of contextUses) {
            const write = /^context\.args\(\)->(\w+)\(\s*(?:checksums::Name\{\s*)?(k\w+|0x[0-9A-Fa-f]+)/.exec(use);
            if (write && WRITES[write[1]!]) {
                const constant = write[2]!;
                const value = constant.startsWith("0x")
                    ? constant
                    : (new RegExp(String.raw`\b${constant}\s*=\s*(0x[0-9A-Fa-f]+)`).exec(found.text)?.[1] ??
                      files
                          .map((f) => new RegExp(String.raw`\b${constant}\s*=\s*(0x[0-9A-Fa-f]+)`).exec(f.text)?.[1])
                          .find(Boolean));
                if (!value) {
                    open = `the constant ${constant} wasn't found`;
                    break;
                }
                const key = `0x${(Number.parseInt(value, 16) >>> 0).toString(16).padStart(8, "0")}`;
                outputs[key] = outputs[key] ? `${outputs[key]}|${WRITES[write[1]!]}` : WRITES[write[1]!]!;
                continue;
            }
            // reading the script's own title / object is no output
            if (/^context\.(script_title|object|script)\(\)\s*[^-]/.test(use) && !/->args\(\)/.test(use)) continue;
            open = `uses its context: ${use.trim().slice(0, 120)}`;
            break;
        }
        if (open) review[name] = { function: fn, source, why: open };
        else put(name, { outputs, source });
    }
    return { models, review };
}

// Reviewed models (written from the C++ by reading it, as JSON) put into the checker's shape: kinds known to it, the
// parameters a started script gets as "*" / "callerParams" / "none" / "unknown" / an argument's name / { members }.
// Whatever can't be read that way is widened (every kind; "unknown"), never narrowed.
const KIND_WORDS = [
    "int",
    "float",
    "string",
    "localstring",
    "pair",
    "vector",
    "script",
    "name",
    "struct",
    "array",
    "none",
];
function normalKinds(text: unknown): string {
    if (typeof text !== "string") return "any";
    if (text.trim() === "any") return "any";
    const words = text
        .toLowerCase()
        .split(/[|,/ ]+/)
        .map((w) => w.replace(/[^a-z]/g, ""))
        .filter(Boolean);
    const known = words.filter((w) => KIND_WORDS.includes(w));
    return known.length === words.length && known.length ? [...new Set(known)].join("|") : "any";
}
export function normalModel(raw: Record<string, unknown>): CommandModel {
    const out: CommandModel = {};
    const kindsMap = (m: unknown) =>
        Object.fromEntries(Object.entries((m ?? {}) as Record<string, unknown>).map(([k, v]) => [k, normalKinds(v)]));
    if (raw.outputs !== undefined) out.outputs = kindsMap(raw.outputs);
    if (raw.byArgument !== undefined) out.byArgument = kindsMap(raw.byArgument);
    if (raw.anyMembers !== undefined) out.anyMembers = normalKinds(raw.anyMembers);
    if (raw.changesGlobals) out.changesGlobals = String(raw.changesGlobals);
    if (raw.mergesArguments) out.mergesArguments = true;
    const starts = raw.starts === undefined ? [] : Array.isArray(raw.starts) ? raw.starts : [raw.starts];
    if (starts.length)
        out.starts = (starts as Record<string, unknown>[]).map((st) => {
            const script = String(st.script ?? "");
            const p = st.params;
            let params: string | { members: Record<string, string> } = "unknown";
            if (p === undefined || p === "none") params = "none";
            else if (p === "*" || p === "callerParams" || p === "unknown") params = p;
            else if (typeof p === "object" && p !== null)
                params = { members: kindsMap((p as { members?: unknown }).members ?? p) };
            else if (typeof p === "string" && /^[A-Za-z_]\w*$/.test(p)) params = p;
            else if (typeof p === "string") {
                const fixed = /^fixed\s*\{(.*)\}\s*$/.exec(p.trim());
                if (fixed) {
                    const members: Record<string, string> = {};
                    for (const part of fixed[1]!.split(",")) {
                        const m = /^\s*([\w?]+)\s*:\s*([\w|]+)\s*$/.exec(part);
                        if (!m) {
                            params = "unknown";
                            break;
                        }
                        members[m[1]!] = normalKinds(m[2]);
                    }
                    if (params !== "unknown" || !fixed[1]!.trim()) params = { members };
                }
            }
            // ("?": a script the review couldn't name - the checker takes any script that can be named)
            return { script: /^(fixed:)?\w+$/.test(script) ? script : "?", params, how: String(st.how ?? "") };
        });
    out.source = String(raw.source ?? "");
    if (raw.uncertain) out.uncertain = String(raw.uncertain);
    if (raw.notes) out.notes = String(raw.notes);
    return out;
}

// Two models of one command joined: every output, start and change of either.
export function unionModel(a: CommandModel, b: CommandModel): CommandModel {
    const kinds = (x?: string, y?: string) =>
        x === "any" || y === "any"
            ? "any"
            : [...new Set([...(x ?? "").split("|"), ...(y ?? "").split("|")])].filter(Boolean).join("|");
    const maps = (x?: Record<string, string>, y?: Record<string, string>) => {
        const out: Record<string, string> = { ...(x ?? {}) };
        for (const [k, v] of Object.entries(y ?? {})) out[k] = out[k] ? kinds(out[k], v) : v;
        return out;
    };
    const list = (s: CommandModel["starts"]) => (s === undefined ? [] : Array.isArray(s) ? s : [s]);
    const out: CommandModel = { outputs: maps(a.outputs, b.outputs) };
    if (a.byArgument || b.byArgument) out.byArgument = maps(a.byArgument, b.byArgument);
    if (a.anyMembers || b.anyMembers) out.anyMembers = kinds(a.anyMembers, b.anyMembers);
    if (a.starts || b.starts) out.starts = [...list(a.starts), ...list(b.starts)];
    if (a.changesGlobals || b.changesGlobals)
        out.changesGlobals = [a.changesGlobals, b.changesGlobals].filter(Boolean).join("; ");
    if (a.mergesArguments || b.mergesArguments) out.mergesArguments = true;
    out.source = [a.source, b.source].filter(Boolean).join("; ");
    const uncertain = [a.uncertain, b.uncertain].filter(Boolean).join("; ");
    if (uncertain) out.uncertain = uncertain;
    const notes = [a.notes, b.notes].filter(Boolean).join("; ");
    if (notes) out.notes = notes;
    return out;
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href && process.argv[2] === "--merge") {
    // node tools/qbjs/command_models.ts --merge <reviewed.json> ...: added to command_models_reviewed.json
    const reviewedPath = join(OUT, "command_models_reviewed.json");
    const reviewed = existsSync(reviewedPath)
        ? (JSON.parse(readFileSync(reviewedPath, "utf8")) as Record<string, CommandModel>)
        : {};
    const merged = new Set<string>();
    for (const file of process.argv.slice(3)) {
        const models = JSON.parse(readFileSync(file, "utf8")) as Record<string, Record<string, unknown>>;
        for (const [name, raw] of Object.entries(models)) {
            // (a name modelled in two files of one merge - an object's members handled by engine and game components:
            // the union of both)
            const model = normalModel(raw);
            reviewed[name] = merged.has(name) ? unionModel(reviewed[name]!, model) : model;
            merged.add(name);
        }
        console.log(`${file}: ${Object.keys(models).length} models`);
    }
    const sorted = Object.fromEntries(Object.entries(reviewed).sort(([a], [b]) => a.localeCompare(b)));
    writeFileSync(reviewedPath, `${JSON.stringify(sorted, null, 1)}\n`);
    process.exit(0);
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
    const { models, review } = extract();
    const reviewedPath = join(OUT, "command_models_reviewed.json");
    const reviewed = existsSync(reviewedPath)
        ? (JSON.parse(readFileSync(reviewedPath, "utf8")) as Record<string, CommandModel>)
        : {};
    // (hand corrections of reviewed models, each with its reason: command_models_overrides.json)
    const overridesPath = join(OUT, "command_models_overrides.json");
    const overrides = existsSync(overridesPath)
        ? (JSON.parse(readFileSync(overridesPath, "utf8")) as Record<string, Record<string, unknown>>)
        : {};
    const merged: Record<string, CommandModel> = {
        ...models,
        ...reviewed,
        ...Object.fromEntries(Object.entries(overrides).map(([k, v]) => [k, normalModel(v)])),
    };
    const open = Object.fromEntries(Object.entries(review).filter(([k]) => !reviewed[k]));
    writeFileSync(join(OUT, "command_models.json"), `${JSON.stringify(merged, null, 1)}\n`);
    writeFileSync(join(OUT, "command_models_review.json"), `${JSON.stringify(open, null, 1)}\n`);
    const none = Object.values(models).filter((m) => m.outputs && !Object.keys(m.outputs).length).length;
    console.log(
        `${Object.keys(models).length} C functions modelled mechanically (${none} with no outputs), ${Object.keys(reviewed).length} reviewed, ${Object.keys(open).length} left for review`,
    );
}

// The C++ starting scripts (catalogued from src/ into JSON: each site, how its scripts are chosen, what parameters
// they get) put into the checker's shape: tools/qbjs/entry_models.json - for each script named by a constant, the
// parameter lists it is started with ({ members } / "none" / "unknown: <why>"). Sites whose scripts come from data or
// arguments are left to the checker's own handling (data: a node's TriggerScript gets its node's params; the rest
// unknown); a parameter list described in words, or with structures merged in, is "unknown".
export function entryModels(records: Record<string, unknown>[]): Record<string, { params: unknown; site: string }[]> {
    const out: Record<string, { params: unknown; site: string }[]> = {};
    for (const r of records) {
        if ((r as { wrapper?: boolean }).wrapper) continue;
        const scripts = r.scripts as { how?: string; names?: { name?: string; checksum?: string }[] } | string;
        if (typeof scripts !== "object" || scripts.how !== "constant") continue;
        const p = r.params as unknown;
        let params: unknown = "unknown: " + (typeof p === "string" ? p : "a parameter list that couldn't be read");
        if (p === "none") params = "none";
        else if (p && typeof p === "object") {
            const o = p as Record<string, unknown>;
            const members = (o.members ??
                (o.merged_structures === undefined && o.unnamed_name_flags === undefined ? o : undefined)) as
                | Record<string, unknown>
                | undefined;
            const merged = Array.isArray(o.merged_structures) && o.merged_structures.length > 0;
            if (members && !merged && !("*" in members)) {
                const m: Record<string, string> = {};
                for (const [k, v] of Object.entries(members)) m[k] = normalKinds(v);
                if (Array.isArray(o.unnamed_name_flags) && o.unnamed_name_flags.length) m["0x00000000"] = "name";
                if (o.unnamed_values !== undefined) m["0x00000000"] = "any";
                params = { members: m };
            } else params = "unknown: structures merged in (members that can't be listed)";
        }
        for (const n of scripts.names ?? []) {
            const key = n.name && /^\w+$/.test(n.name) ? n.name : (n.checksum ?? "").toLowerCase();
            if (!key) continue;
            const list = out[key] ?? [];
            list.push({ params, site: String(r.site ?? "") });
            out[key] = list;
        }
    }
    return out;
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href && process.argv[2] === "--entries") {
    const records = process.argv
        .slice(3)
        .flatMap((f) => JSON.parse(readFileSync(f, "utf8")) as Record<string, unknown>[]);
    const models = entryModels(records);
    writeFileSync(join(OUT, "entry_models.json"), `${JSON.stringify(models, null, 1)}\n`);
    console.log(`${Object.keys(models).length} scripts the C++ starts by name, from ${records.length} records`);
}
