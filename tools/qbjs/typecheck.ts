// NOT ORIGINAL - the scripts' types, proven for the whole program (docs/qb-types.md): an abstract interpretation of
// every module's IR to a fixed point. Each site's type is a set of QB kinds that holds on every path - run or not -
// or, where something outside the scripts decides it, carries the reasons it can't be proven: an unknown, each listed
// with where it comes from (docs/qb-types-unknowns.md and its artifact).
//
//   node tools/qbjs/typecheck.ts --game <folder> [--models tools/qbjs/command_models.json] [--doc docs] [--json out]
//
// Sound by construction: types only grow (joins), flows are flow-insensitive (a parameter's type is every type it may
// hold anywhere in its script), and what can't be seen is never assumed - it is an unknown:
//   - a command (C function / member function) without a model of what it writes into the caller's parameters;
//   - a script that something outside QB may start (named in data or by the C++), with parameters the C++ makes;
//   - a command named by a parameter whose names can't be told (a dynamic call): its arguments go to every script that
//     can be named;
//   - a global something may change at run time (a command's writes to globals without a model).
// Operators' results come from scripting/values.js itself (each operator run on samples of every pair of kinds), so the
// checker can't disagree with the runtime the harness proves against the game's interpreter.
import { existsSync, mkdirSync, readdirSync, readFileSync, statSync, writeFileSync } from "node:fs";
import { join, relative } from "node:path";
import { pathToFileURL } from "node:url";
import { runInNewContext } from "node:vm";
import { CFUNC_NAMES } from "thug2-level-converter-core/level_port/thug2_cfuncs.ts";
import { checksum } from "../qb_checksum.ts";
import { helperSource } from "./helper-sources.ts";
import { gameRoot } from "./install_ts.ts";
import type { Call, Definition, Expr, Field, ModuleIR, Statement } from "./ir.ts";
import { readIR } from "./reader.ts";

export const KINDS = [
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
] as const;
export type Kind = (typeof KINDS)[number];
const DEPTH = 2; // structure / array shapes kept this deep (deeper: kinds only)
const NAME_LIMIT = 64;

// A type: the kinds a value may have; a name's possible values (null: any); an array's element type; a structure's
// members (open: other members may be there, of `rest`'s type); `why`: the unknowns that went into it (empty: proven).
// The unknowns' texts, by id (a type keeps ids: thousands of values carry the same few hundred reasons).
export const REASONS: string[] = [];
const reasonIds = new Map<string, number>();
export function reasonId(text: string): number {
    let id = reasonIds.get(text);
    if (id === undefined) {
        id = REASONS.length;
        REASONS.push(text);
        reasonIds.set(text, id);
    }
    return id;
}

// Sets of reasons, each kept once (id 0: none); a union of two is worked out once.
export const WHY_SETS: number[][] = [[]];
const whyIds = new Map<string, number>([["", 0]]);
const unions = new Map<string, number>();
export function whyOf(reasons: number[]): number {
    const sorted = [...new Set(reasons)].sort((a, b) => a - b);
    const key = sorted.join(",");
    let id = whyIds.get(key);
    if (id === undefined) {
        id = WHY_SETS.length;
        WHY_SETS.push(sorted);
        whyIds.set(key, id);
    }
    return id;
}
export function whyUnion(a: number, b: number): number {
    if (a === b || b === 0) return a;
    if (a === 0) return b;
    const key = a < b ? `${a},${b}` : `${b},${a}`;
    let id = unions.get(key);
    if (id === undefined) {
        id = whyOf([...WHY_SETS[a]!, ...WHY_SETS[b]!]);
        unions.set(key, id);
    }
    return id;
}

// A set of kinds as bits (a value's type holds one; millions are made).
export class KindSet {
    bits = 0;
    constructor(kinds: Iterable<Kind> = []) {
        for (const k of kinds) this.add(k);
    }
    has(k: Kind) {
        return (this.bits & (1 << KINDS.indexOf(k))) !== 0;
    }
    add(k: Kind) {
        this.bits |= 1 << KINDS.indexOf(k);
        return this;
    }
    get size() {
        let n = 0;
        for (let b = this.bits; b; b &= b - 1) ++n;
        return n;
    }
    *[Symbol.iterator]() {
        for (let i = 0; i < KINDS.length; ++i) if (this.bits & (1 << i)) yield KINDS[i]!;
    }
}
const NO_NAMES: Set<number> = new Set();

export class Ty {
    kinds = new KindSet();
    names: Set<number> | null = NO_NAMES; // (shared until a name is added: millions of values hold none)
    elem?: Ty;
    fields?: Map<number, Ty>;
    rest?: Ty;
    why = 0; // the set of reasons (WHY_SETS: shared, most values carry the same few)
    static of(...kinds: Kind[]) {
        const t = new Ty();
        for (const k of kinds) t.kinds.add(k);
        return t;
    }
    static unknown(reason: string) {
        const t = Ty.of(...KINDS);
        t.names = null;
        t.why = whyOf([reasonId(reason)]);
        return t;
    }
    // Join `b` in; true when anything changed. (A name set past NAME_LIMIT names becomes "any name": precision, not an
    // unknown source - the reason is kept.)
    join(b: Ty | undefined, depth = 0): boolean {
        if (!b) return false;
        let changed = false;
        for (const k of b.kinds) {
            if (!this.kinds.has(k)) {
                this.kinds.add(k);
                changed = true;
            }
        }
        const why = whyUnion(this.why, b.why);
        if (why !== this.why) {
            this.why = why;
            changed = true;
        }
        if (this.names !== null) {
            if (b.names === null) {
                if (b.kinds.has("name") || b.kinds.has("script")) {
                    this.names = null;
                    changed = true;
                }
            } else {
                for (const n of b.names) {
                    if (!this.names.has(n)) {
                        if (this.names === NO_NAMES) this.names = new Set();
                        this.names.add(n);
                        changed = true;
                    }
                }
                if (this.names.size > NAME_LIMIT) {
                    this.names = null;
                    this.why = whyUnion(
                        this.why,
                        whyOf([reasonId(`more than ${NAME_LIMIT} names flow into one value (not told apart)`)]),
                    );
                }
            }
        }
        if (depth >= DEPTH) return changed;
        if (b.elem) {
            if (!this.elem) {
                this.elem = new Ty();
                changed = true;
            }
            changed = this.elem.join(b.elem, depth + 1) || changed;
        }
        if (b.fields || b.rest) {
            if (!this.fields) {
                this.fields = new Map();
                changed = true;
            }
            for (const [n, t] of b.fields ?? []) {
                let mine = this.fields.get(n);
                if (!mine) {
                    mine = new Ty();
                    this.fields.set(n, mine);
                    changed = true;
                }
                changed = mine.join(t, depth + 1) || changed;
            }
            if (b.rest) {
                if (!this.rest) {
                    this.rest = new Ty();
                    changed = true;
                }
                changed = this.rest.join(b.rest, depth + 1) || changed;
            }
        }
        return changed;
    }
    member(n: number): Ty {
        const t = new Ty();
        const f = this.fields?.get(n);
        if (f) t.join(f);
        if (this.rest) t.join(this.rest);
        if (!f) t.kinds.add("none");
        return t;
    }
    text(): string {
        const kinds = KINDS.filter((k) => this.kinds.has(k)).map((k) =>
            k === "array" && this.elem ? `array<${this.elem.text()}>` : k,
        );
        return kinds.join("|") || "never";
    }
}

// A structure type's type for members not listed (made when first needed).
function restOf(t: Ty): Ty {
    if (!t.rest) t.rest = new Ty();
    return t.rest;
}

// --- operators, from values.js -------------------------------------------------------------------------------------

type Values = {
    operate: (op: number, a: unknown, b: unknown, ctx: unknown) => { type: number } | undefined;
};
const TYPE_KIND: Record<number, Kind> = {
    1: "int",
    2: "float",
    3: "string",
    4: "localstring",
    5: "pair",
    6: "vector",
    7: "script",
    10: "struct",
    12: "array",
    13: "name",
    20: "struct",
    21: "array",
};
// For each operator and pair of kinds, the result kinds values.js gives on samples (none: a fault for some sample).
export function operatorTable(): Map<string, Set<Kind>> {
    const v = runInNewContext(`${helperSource("values")}\nglobalThis.qbValues`, {}) as Values;
    const ctx = {
        site: { source: "typecheck", script: 0, offset: 0 },
        owner: 0,
        symbol: () => undefined,
        charge: () => {},
    };
    const f = (x: number) => ({ type: 2, bits: new Uint32Array(new Float32Array([x]).buffer)[0]! });
    const samples: Record<Kind, unknown[]> = {
        int: [
            { type: 1, bits: 0 },
            { type: 1, bits: 3 },
        ],
        float: [f(0), f(2.5)],
        string: [{ type: 3, bytes: [65] }],
        localstring: [{ type: 4, bytes: [65] }],
        pair: [{ type: 5, coordinates: [f(1).bits, f(2).bits] }],
        vector: [{ type: 6, coordinates: [f(1).bits, f(2).bits, f(3).bits] }],
        script: [{ type: 7, bytes: [1, 0x24] }],
        name: [{ type: 13, bits: 0x1234 }],
        struct: [{ type: 10, members: [[0x1234, { type: 1, bits: 1 }]] }],
        array: [
            { type: 12, elementType: 1, elements: [{ type: 1, bits: 1 }] },
            { type: 12, elementType: 0, elements: [] },
        ],
        none: [undefined],
    };
    const table = new Map<string, Set<Kind>>();
    for (const op of [7, 10, 11, 12, 13, 18, 20, 50, 51])
        for (const a of KINDS)
            for (const b of KINDS) {
                const out = new Set<Kind>();
                for (const x of samples[a])
                    for (const y of samples[b]) {
                        try {
                            const r = v.operate(op, x, y, ctx);
                            out.add(r === undefined ? "none" : (TYPE_KIND[r.type] ?? "none"));
                        } catch {
                            out.add("none");
                        }
                    }
                table.set(`${op} ${a} ${b}`, out);
            }
    return table;
}

// --- the program -----------------------------------------------------------------------------------------------------

// What a command does to the scripts' types (tools/qbjs/command_models.ts; reviewed ones: command_models_reviewed.json).
export type CommandModel = {
    // the members it writes into the caller's parameters: name (or 0x + checksum) -> kinds joined by | ("none": removed)
    outputs?: Record<string, string>;
    // members named by an argument's value: argument -> kinds
    byArgument?: Record<string, string>;
    // members of names that can't be listed (a structure merged in): their kinds, or "any"
    anyMembers?: string;
    // the scripts it starts: the argument naming the script, its parameters (an argument, "*" all of the command's
    // arguments, "callerParams" the caller's parameters, "none")
    starts?: Start | Start[];
    // globals it changes (how their names are chosen)
    changesGlobals?: boolean | string;
    // its own arguments merged into the caller's parameters (AddParams)
    mergesArguments?: boolean;
    source?: string;
    uncertain?: string;
    notes?: string;
};
// A started script's parameters: an argument's name, "*" (the command's arguments), "callerParams", "none", "unknown",
// or members with kinds ("name?" may be missing).
type Start = { script: string; params?: string | { members: Record<string, string> }; how?: string };
function membersTy(members: Record<string, string>): Ty {
    const t = Ty.of("struct");
    t.fields = new Map();
    for (const [k, kinds] of Object.entries(members)) {
        const optional = k.endsWith("?");
        const m = Ty.of(...kindsOf(kinds));
        if (optional) m.kinds.add("none");
        t.fields.set(memberKey(optional ? k.slice(0, -1) : k), m);
    }
    return t;
}
const memberKey = (k: string) => (/^0x[0-9a-f]{8}$/i.test(k) ? Number.parseInt(k, 16) >>> 0 : checksum(k));
const kindsOf = (text: string): Kind[] =>
    text === "any" ? [...KINDS] : (text.split("|").filter((k) => (KINDS as readonly string[]).includes(k)) as Kind[]);

type ScriptInfo = {
    module: string;
    def: Extract<Definition, { kind: "script" }>;
    params: Ty; // its parameter list as a structure
    returns: Ty;
};

export type Site = { kind: string; module: string; script: string; name: string; ty: Ty };

// What a module gives before its IR is let go: the names it uses as values (into `named`), its globals' types (into
// `globals`; a ported level's node arrays are millions of nodes - only their types are kept). Returns its scripts.
// `where` (optional): for each name used as a value, where - "member <field>" of a structure, "argument <field> of
// <command>", "global <name>", "in an expression" - the way a script so named may be started.
export function digest(
    ir: ModuleIR,
    named: Set<number>,
    globals: Map<number, Ty>,
    where?: Map<number, Set<string>>,
    triggers?: Map<number, Ty>,
): ModuleIR {
    const note = (n: number, how: string) => {
        named.add(n);
        if (!where) return;
        let set = where.get(n);
        if (!set) {
            set = new Set();
            where.set(n, set);
        }
        if (set.size < 16) set.add(how);
    };
    const collect = (x: unknown, field: number, call: number): void => {
        if (!x || typeof x !== "object") return;
        if (Array.isArray(x)) {
            for (const item of x) collect(item, field, call);
            return;
        }
        const r = x as { kind?: string; value?: unknown; name?: number; fields?: unknown };
        const v = r.value as { type?: number; bits?: number } | undefined;
        if (r.kind === "literal" && v?.type === 13 && v.bits !== undefined)
            note(v.bits, call ? `argument ${hex8(field)} of ${hex8(call)}` : `member ${hex8(field)}`);
        if ((r.kind === "global" || r.kind === "functionCall") && r.name !== undefined)
            note(r.name, "in an expression");
        // A node's TriggerScript is started with the node's params (the engine's and the game's catalogues):
        // `triggers` gets that structure's type for the script it names.
        if (r.kind === "structure" && triggers && Array.isArray(r.fields)) {
            const fs = r.fields as { name?: number; value?: Expr }[];
            const trigger = fs.find((f) => f.name === TRIGGER_SCRIPT && f.value?.kind === "literal");
            const v = trigger?.value as Extract<Expr, { kind: "literal" }> | undefined;
            if (v && v.value.type === 13) {
                const params = fs.find((f) => f.name === PARAMS && f.value);
                const t = params?.value ? valueTy(params.value) : Ty.of("none");
                const old = triggers.get(v.value.bits);
                if (old) old.join(t);
                else triggers.set(v.value.bits, t);
            }
        }
        if (r.kind === "call") {
            collect(r.fields, 0, r.name ?? 0);
            for (const [k, item] of Object.entries(x)) if (k !== "fields") collect(item, field, call);
            return;
        }
        if (r.kind === undefined && typeof r.name === "number" && r.value && typeof r.value === "object") {
            collect(r.value, r.name, call);
            return;
        }
        if (r.kind === "global" && typeof r.name === "number" && r.value) {
            collect(r.value, r.name, 0);
            return;
        }
        for (const item of Object.values(x)) collect(item, field, call);
    };
    collect(ir.definitions, 0, 0);
    for (const d of ir.definitions) {
        if (d.kind !== "global") continue;
        const t = globals.get(d.name) ?? new Ty();
        t.join(valueTy(d.value));
        globals.set(d.name, t);
    }
    return { ...ir, definitions: ir.definitions.filter((d) => d.kind === "script") };
}
const TRIGGER_SCRIPT = 0x2ca8a299; // TriggerScript
const PARAMS = checksum("params");
const hex8 = (n: number) => `0x${(n >>> 0).toString(16).padStart(8, "0")}`;

// A global's value's type (a global is parsed without parameters: literals, structures, arrays, scripts).
function valueTy(x: Expr): Ty {
    switch (x.kind) {
        case "literal": {
            const t = Ty.of(TYPE_KIND[x.value.type]!);
            if (x.value.type === 13) t.names = new Set([x.value.bits]);
            return t;
        }
        case "structure": {
            const t = Ty.of("struct");
            t.fields = new Map();
            for (const f of x.fields) {
                if (!("value" in f)) continue;
                const v = valueTy(f.value);
                if (f.name === 0 && f.value.kind === "structure") {
                    for (const [n, m] of v.fields ?? []) t.fields.set(n, m);
                    continue;
                }
                const old = t.fields.get(f.name);
                if (old) old.join(v);
                else t.fields.set(f.name, v);
            }
            return t;
        }
        case "array": {
            const t = Ty.of("array");
            t.elem = new Ty();
            for (const i of x.items) t.elem.join(valueTy(i));
            return t;
        }
        case "randomChoice": {
            const t = new Ty();
            for (const c of x.choices) t.join(valueTy(c));
            return t;
        }
        case "randomRange":
            return Ty.of("float");
        case "inlineScript":
            return Ty.of("script");
        default:
            return Ty.unknown("a global made by an expression (its value not worked out)");
    }
}

export function typecheck(
    modules: ModuleIR[],
    models: Record<string, CommandModel>,
    cxxNames: Set<number>,
    pre: {
        named: Set<number>;
        globals: Map<number, Ty>;
        where?: Map<number, Set<string>>;
        triggers?: Map<number, Ty>;
        entries?: Record<string, { params: unknown; site: string }[]>;
    },
) {
    const ops = operatorTable();
    const names = new Map<number, string>();
    for (const m of modules) for (const [n, s] of Object.entries(m.names ?? {})) names.set(Number(n), s);
    const cfuncs = new Set(CFUNC_NAMES.map((s) => checksum(s)));
    for (const s of CFUNC_NAMES) names.set(checksum(s), s);
    const nameOf = (n: number) => names.get(n) ?? `_0x${n.toString(16).padStart(8, "0")}`;
    const modelOf = new Map<number, CommandModel>();
    for (const [k, model] of Object.entries(models)) modelOf.set(memberKey(k), model);

    const scripts = new Map<number, ScriptInfo>();
    const { globals, named } = pre; // (digest: the globals' types, the names used as values)
    const entriesOf = new Map<number, { params: unknown; site: string }[]>();
    for (const [k, list] of Object.entries(pre.entries ?? {})) entriesOf.set(memberKey(k), list);
    for (const m of modules)
        for (const d of m.definitions) {
            // (a name defined twice - a level redefining a boot script: one type for it, every body counts - either
            // may be the one that runs)
            if (d.kind === "script" && !scripts.has(d.name))
                scripts.set(d.name, { module: m.source, def: d, params: new Ty(), returns: new Ty() });
        }

    const unknownGlobal = Ty.unknown("global changed at run time by a command without a model");
    const changedGlobals = new Set<number>();
    let anyGlobal: Ty | undefined;
    const globalTy = (n: number): Ty | undefined => {
        const t = globals.get(n);
        if (!t) return undefined;
        if (!changedGlobals.has(n)) return t;
        const out = new Ty();
        out.join(t);
        out.join(unknownGlobal);
        return out;
    };

    // The type of an expression in script `s` (mode: as a value, or read inside an expression - names standing for
    // globals then resolved).
    const exprTy = (x: Expr, s: ScriptInfo | undefined, mode: "value" | "expression"): Ty => {
        switch (x.kind) {
            case "literal": {
                const v = x.value;
                const t = Ty.of(TYPE_KIND[v.type]!);
                if (v.type === 13) {
                    t.names = new Set([v.bits]);
                    named.add(v.bits);
                }
                return mode === "expression" && v.type === 13 ? resolveNames(t) : t;
            }
            case "parameter": {
                const t = s ? memberOf(s, x.name) : Ty.of("none");
                return mode === "expression" || x.mode === "expression" ? resolveNames(t) : t;
            }
            case "global": {
                const g = globalTy(x.name);
                if (g) return g;
                if (cfuncs.has(x.name)) return Ty.of("int");
                const t = Ty.of("name");
                t.names = new Set([x.name]);
                named.add(x.name);
                return t;
            }
            case "functionCall":
                return cfuncs.has(x.name) ? Ty.of("int") : (globalTy(x.name) ?? Ty.of("name"));
            case "allParameters": {
                const t = Ty.of("struct");
                if (s) t.join(wholeOf(s));
                t.kinds.add("struct");
                return t;
            }
            case "expressionResult":
                return exprTy(x.value, s, "expression");
            case "binary":
                return binaryTy(
                    x.op,
                    exprTy(x.left, s, "expression"),
                    exprTy(x.right, s, x.op === 8 ? "value" : "expression"),
                    x,
                );
            case "randomRange":
                return Ty.of("float");
            case "randomChoice": {
                const t = new Ty();
                for (const c of x.choices) t.join(exprTy(c, s, mode));
                return t;
            }
            case "structure": {
                const t = fieldsTy(x.fields, s);
                t.kinds = new KindSet(["struct"]);
                return t;
            }
            case "array": {
                const t = Ty.of("array");
                t.elem = new Ty();
                for (const i of x.items) t.elem.join(exprTy(i, s, "value"));
                return t;
            }
            case "inlineScript":
                return Ty.of("script");
        }
    };
    // A name's value inside an expression: the global it names (when there is one), else the name.
    const resolveNames = (t: Ty): Ty => {
        if (!t.kinds.has("name")) return t;
        const out = new Ty();
        out.join(t);
        if (t.names === null) {
            if (!anyGlobal) {
                // (the kinds any global may have - not their shapes: a member of it is then an unknown)
                anyGlobal = new Ty();
                anyGlobal.names = null;
                for (const g of globals.values()) for (const k of g.kinds) anyGlobal.kinds.add(k);
                for (const g of globals.values()) anyGlobal.why = whyUnion(anyGlobal.why, g.why);
            }
            out.join(anyGlobal);
            out.why = whyUnion(
                out.why,
                whyOf([reasonId("a name of unknown value read in an expression may stand for any global")]),
            );
        } else for (const n of t.names) out.join(globalTy(n));
        return out;
    };
    const binaryTy = (op: number, a: Ty, b: Ty, x: Extract<Expr, { kind: "binary" }>): Ty => {
        const out = new Ty();
        out.why = whyUnion(a.why, b.why);
        if (op === 5) {
            // a[i]: the element, or none (not an array, index not an integer or out of range)
            if (a.kinds.has("array") && a.elem) out.join(a.elem);
            else if (a.kinds.has("array")) out.join(Ty.unknown("an array whose elements weren't tracked"));
            out.kinds.add("none");
            return out;
        }
        if (op === 8) {
            // a.b: a member, or none
            if (a.kinds.has("struct")) {
                if (b.names === null || !b.kinds.has("name"))
                    out.join(a.rest ?? Ty.unknown("a member of a structure whose members weren't tracked"));
                else for (const n of b.names) out.join(a.member(n));
                if (!a.fields && !a.rest) out.join(Ty.unknown("a member of a structure whose members weren't tracked"));
            }
            for (const k of a.kinds)
                if (k !== "struct")
                    for (const r of ops.get(`8 ${k} ${[...b.kinds][0] ?? "none"}`) ?? []) out.kinds.add(r);
            out.kinds.add("none");
            return out;
        }
        for (const ka of a.kinds)
            for (const kb of b.kinds)
                for (const r of ops.get(`${op} ${ka} ${kb}`) ?? new Set<Kind>(["none"])) out.kinds.add(r);
        if (out.kinds.has("array")) {
            out.elem = new Ty();
            out.elem.join(a.elem);
            out.elem.join(b.elem);
        }
        if (out.kinds.has("struct")) {
            out.fields = new Map();
            out.join(a);
            out.join(b);
            out.kinds = new KindSet(
                [...out.kinds].filter((k) => k !== "array" || a.kinds.has("array") || b.kinds.has("array")),
            );
        }
        if (out.kinds.has("name") && op === 11) out.names = null; // (a name plus an integer: another name)
        void x;
        return out;
    };
    // A field list as the structure it makes (arguments, a structure literal, a return).
    const fieldsTy = (fs: Field[], s: ScriptInfo | undefined): Ty => {
        const t = Ty.of("struct");
        t.fields = new Map();
        const put = (n: number, v: Ty) => {
            const old = t.fields!.get(n);
            if (old) old.join(v);
            else {
                const copy = new Ty();
                copy.join(v);
                t.fields!.set(n, copy);
            }
        };
        for (const f of fs) {
            if ("spread" in f) {
                if (s) {
                    const whole = wholeOf(s);
                    for (const [n, v] of whole.fields ?? []) put(n, v);
                    if (whole.rest) restOf(t).join(whole.rest);
                }
                continue;
            }
            if ("kind" in f) {
                // a Random among the fields: one branch's - each member may be missing
                for (const branch of f.branches) {
                    const b = fieldsTy(branch, s);
                    for (const [n, v] of b.fields ?? []) {
                        const m = new Ty();
                        m.join(v);
                        m.kinds.add("none");
                        put(n, m);
                    }
                    if (b.rest) restOf(t).join(b.rest);
                }
                continue;
            }
            const v = exprTy(f.value, s, "value");
            if (f.name === 0 && v.kinds.has("struct") && (f.flattenStructure || f.value.kind === "structure")) {
                for (const [n, m] of v.fields ?? []) put(n, m);
                if (v.rest) restOf(t).join(v.rest);
                if (v.why) restOf(t).join(v);
            }
            if (f.value.kind === "inlineScript") put(f.name, Ty.of("script"));
            else if (f.name === 0 && f.value.kind !== "structure") put(0, v);
            else if (f.name !== 0) put(f.name, v);
        }
        return t;
    };

    // --- flows ---
    const scriptBodies = new Map<number, { module: string; def: Extract<Definition, { kind: "script" }> }[]>();
    for (const m of modules)
        for (const d of m.definitions)
            if (d.kind === "script") {
                const list = scriptBodies.get(d.name) ?? [];
                list.push({ module: m.source, def: d });
                scriptBodies.set(d.name, list);
            }
    let targetList: number[] | undefined;
    const dynamicTargets = () => (targetList ??= [...scripts.keys()].filter((n) => named.has(n)));
    // A script's parameters as it sees them: its own, and - when it can be named - the inbox of dynamic calls' arguments.
    const inbox = new Ty();
    const memberOf = (s: ScriptInfo, n: number): Ty => {
        const t = s.params.member(n);
        if (named.has(s.def.name) && inbox.fields) t.join(inbox.member(n));
        return t;
    };
    const wholeOf = (s: ScriptInfo): Ty => {
        if (!named.has(s.def.name) || !inbox.fields) return s.params;
        const t = new Ty();
        t.join(s.params);
        t.join(inbox);
        return t;
    };
    const unknownCommands = new Map<string, { calls: number; scripts: Set<string> }>();
    const undefinedCalls = new Set<string>();
    const dynamicSites: string[] = [];

    // The worklist: a script is walked again when its parameters change; its callers when its returns do.
    const queued = new Set<number>();
    const queue: number[] = [];
    const enqueue = (n: number) => {
        if (!queued.has(n)) {
            queued.add(n);
            queue.push(n);
        }
    };
    const callers = new Map<number, Set<number>>();
    const grow = (target: ScriptInfo, into: "params" | "returns", t: Ty) => {
        if (!target[into].join(t)) return;
        if (into === "params") enqueue(target.def.name);
        else for (const c of callers.get(target.def.name) ?? []) enqueue(c);
    };
    let rounds = 0;
    const seenDynamic = new Set<string>();
    const firstWalk = new Set<number>();
    const flowCall = (c: Call, s: ScriptInfo) => {
        const args = fieldsTy(c.fields, s);
        let targets: number[] = [];
        let dynamic = false;
        if (c.byExpr !== undefined) {
            const t = exprTy(c.byExpr, s, "expression");
            if (t.names === null) dynamic = true;
            else targets = [...t.names];
        } else if (c.via !== undefined) {
            const t = memberOf(s, c.via);
            if (t.names === null) dynamic = true;
            else targets = [...t.names];
        } else if (c.name) targets = [c.name];
        if (dynamic) {
            // Its arguments go to every script that can be named - one inbox they all read (not a copy each) - and,
            // as it may name a command, what it writes into this script's parameters is an unknown.
            const site = `${s.module} ${nameOf(s.def.name)}`;
            if (!seenDynamic.has(site)) {
                seenDynamic.add(site);
                dynamicSites.push(site);
            }
            if (inbox.join(args)) for (const n of dynamicTargets()) enqueue(n);
            const t = Ty.of("struct");
            t.rest = Ty.unknown("outputs of a command named by a parameter whose names can't be told");
            grow(s, "params", t);
            for (const n of dynamicTargets()) {
                let set = callers.get(n);
                if (!set) {
                    set = new Set();
                    callers.set(n, set);
                }
                if (!set.has(s.def.name)) {
                    set.add(s.def.name);
                    grow(s, "params", scripts.get(n)!.returns);
                }
            }
        }
        for (const n of targets) {
            const callee = scripts.get(n);
            if (callee) {
                let set = callers.get(n);
                if (!set) {
                    set = new Set();
                    callers.set(n, set);
                }
                set.add(s.def.name);
                grow(callee, "params", args);
                grow(s, "params", callee.returns);
                continue;
            }
            // a command: what it writes into the caller's parameters
            const model = modelOf.get(n);
            // A name that is no script, no C function and no member function anywhere: the interpreter finds nothing
            // (run_command: false), so it writes nothing - listed in the report (a misspelling, a removed command).
            if (!model && !cfuncs.has(n)) {
                undefinedCalls.add(nameOf(n));
                continue;
            }
            if (!model) {
                const key = nameOf(n);
                const u = unknownCommands.get(key) ?? { calls: 0, scripts: new Set() };
                u.calls += firstWalk.has(s.def.name) ? 0 : 1;
                u.scripts.add(`${s.module} ${nameOf(s.def.name)}`);
                unknownCommands.set(key, u);
                const t = Ty.of("struct");
                t.rest = Ty.unknown(`outputs of ${key} (no model)`);
                grow(s, "params", t);
                continue;
            }
            const t = Ty.of("struct");
            t.fields = new Map();
            for (const [k, kinds] of Object.entries(model.outputs ?? {})) {
                const old = t.fields.get(memberKey(k));
                if (old) old.join(Ty.of(...kindsOf(kinds)));
                else t.fields.set(memberKey(k), Ty.of(...kindsOf(kinds)));
            }
            for (const [arg, kinds] of Object.entries(model.byArgument ?? {})) {
                const named = args.member(memberKey(arg));
                if (named.names === null)
                    t.rest = Ty.unknown(
                        `outputs of ${nameOf(n)}: members named by its argument ${arg}, whose value can't be told`,
                    );
                else for (const m of named.names) t.fields.set(m, Ty.of(...kindsOf(kinds)));
            }
            if (model.anyMembers) {
                const rest =
                    model.anyMembers === "any"
                        ? Ty.unknown(`outputs of ${nameOf(n)}: a structure of members that can't be listed merged in`)
                        : Ty.of(...kindsOf(model.anyMembers));
                restOf(t).join(rest);
            }
            grow(s, "params", t);
            if (model.mergesArguments) grow(s, "params", args);
            if (model.changesGlobals) {
                for (const [g, v] of args.fields ?? []) {
                    changedGlobals.add(g);
                    if (g === 0 && v.names) for (const m of v.names) changedGlobals.add(m);
                }
            }
            for (const start of model.starts === undefined
                ? []
                : Array.isArray(model.starts)
                  ? model.starts
                  : [model.starts]) {
                // (a script fixed in the C++: "fixed:<name>"; else the argument naming it)
                // ("?": the review couldn't tell which - any script that can be named)
                const nameField =
                    start.script === "?"
                        ? Object.assign(Ty.of("name"), { names: null })
                        : start.script.startsWith("fixed:")
                          ? Object.assign(Ty.of("name"), { names: new Set([checksum(start.script.slice(6))]) })
                          : args.member(memberKey(start.script));
                const params =
                    typeof start.params === "object"
                        ? membersTy(start.params.members)
                        : start.params === "*"
                          ? args
                          : start.params === "callerParams"
                            ? wholeOf(s)
                            : start.params === "unknown"
                              ? Object.assign(Ty.of("struct"), {
                                    rest: Ty.unknown(`parameters ${nameOf(n)} gives the scripts it starts`),
                                })
                              : start.params && start.params !== "none"
                                ? args.member(memberKey(start.params))
                                : Ty.of("struct");
                const started = nameField.names === null ? dynamicTargets() : [...nameField.names];
                for (const target of started) {
                    const callee = scripts.get(target);
                    if (callee) grow(callee, "params", params);
                }
            }
        }
    };
    const walk = (xs: Statement[], s: ScriptInfo): void => {
        for (const x of xs) {
            switch (x.kind) {
                case "call":
                    flowCall(x, s);
                    break;
                case "assign": {
                    const t = Ty.of("struct");
                    t.fields = new Map([[x.name, exprTy(x.value, s, "value")]]);
                    grow(s, "params", t);
                    break;
                }
                case "return":
                    grow(s, "returns", fieldsTy(x.fields, s));
                    break;
                case "if":
                    if (x.condition.kind === "call") flowCall(x.condition, s);
                    else if (x.condition.kind === "assign") walk([x.condition], s);
                    walk(x.yes, s);
                    walk(x.no, s);
                    break;
                case "loop":
                    walk(x.body, s);
                    break;
                case "randomBlock":
                    for (const b of x.branches) walk(b, s);
                    break;
                case "switch":
                    for (const c of x.cases) walk(c.body, s);
                    if (x.fallback) walk(x.fallback, s);
                    break;
                case "break":
                    break;
            }
        }
    };
    // Scripts something outside QB may start: named in C++ (cxxNames) or as a value anywhere (data: node arrays, goal
    // tables, menus...) - their parameters then include what the C++ gives them.
    const entry = (n: number) => cxxNames.has(n) || named.has(n);
    for (const [n, s] of scripts) {
        // its defaults (merged under whatever it is started with)
        for (const body of scriptBodies.get(n) ?? []) s.params.join(fieldsTy(body.def.defaults, s));
        // Started by the C++ under its name: the parameter lists catalogued for it (an uncatalogued name in src/ is
        // not one it starts - the catalogue is the claim, docs/qb-types.md).
        for (const e of entriesOf.get(n) ?? []) {
            if (e.params === "none") s.params.join(Ty.of("struct"));
            else if (typeof e.params === "object" && e.params !== null)
                s.params.join(membersTy((e.params as { members: Record<string, string> }).members));
            else {
                const t = Ty.of("struct");
                t.rest = Ty.unknown(
                    "a script the C++ starts with parameters it makes that can't be listed (its catalogue entry)",
                );
                s.params.join(t);
            }
        }
        // Named in data: a node's TriggerScript gets its node's params; any other way of naming it is an unknown.
        if (named.has(n) && !entriesOf.has(n)) {
            const ways = pre.where?.get(n) ?? new Set(["?"]);
            const onlyTrigger = [...ways].every(
                (w) => w === `member 0x${TRIGGER_SCRIPT.toString(16).padStart(8, "0")}`,
            );
            const trigger = pre.triggers?.get(n);
            if (onlyTrigger && trigger) {
                const t = Ty.of("struct");
                t.join(trigger.kinds.has("struct") ? trigger : Ty.of("struct"));
                s.params.join(t);
            } else {
                const t = Ty.of("struct");
                t.rest = Ty.unknown(
                    "a script named as a value (data: goal tables, menus, event handlers, command arguments...): may be started from C++ with parameters it makes",
                );
                s.params.join(t);
            }
        }
        enqueue(n);
    }
    if (process.env.TYPECHECK_PROGRESS)
        console.log(
            `typecheck: ${scripts.size} scripts set up, ${Math.round(process.memoryUsage().heapUsed / 1e6)} MB`,
        );
    while (queue.length) {
        const n = queue.shift()!;
        queued.delete(n);
        ++rounds;
        if (process.env.TYPECHECK_PROGRESS && rounds % 200 === 0)
            console.log(
                `typecheck: ${rounds} walks, ${queue.length} queued, ${Math.round(process.memoryUsage().heapUsed / 1e6)} MB`,
            );
        if (process.env.TYPECHECK_STATS && rounds === Number(process.env.TYPECHECK_STATS)) {
            const stats = { tys: 0, fields: 0, names: 0, why: 0 };
            const big: [string, number][] = [];
            const count = (t: Ty | undefined, depth: number): number => {
                if (!t || depth > 8) return 0;
                stats.tys++;
                stats.names += t.names?.size ?? 0;
                stats.why += WHY_SETS[t.why]!.length;
                let n = 1;
                for (const f of t.fields?.values() ?? []) {
                    stats.fields++;
                    n += count(f, depth + 1);
                }
                n += count(t.elem, depth + 1) + count(t.rest, depth + 1);
                return n;
            };
            for (const [k, sc] of scripts) big.push([nameOf(k), count(sc.params, 0) + count(sc.returns, 0)]);
            big.sort((a, b) => b[1] - a[1]);
            console.log(JSON.stringify(stats), JSON.stringify(big.slice(0, 10)), "inbox", count(inbox, 0));
            process.exit(0);
        }
        const s = scripts.get(n)!;
        for (const body of scriptBodies.get(n) ?? []) walk(body.def.body, s);
        firstWalk.add(n);
    }

    // --- the sites: each parameter read, with its proven type or its unknowns ---
    const sites: Site[] = [];
    const reads = (xs: Statement[], s: ScriptInfo, module: string) => {
        const exprs = (x: Expr | undefined): void => {
            if (!x) return;
            if (x.kind === "parameter")
                sites.push({
                    kind: "param",
                    module,
                    script: nameOf(s.def.name),
                    name: nameOf(x.name),
                    ty: memberOf(s, x.name),
                });
            if (x.kind === "binary") [x.left, x.right].forEach(exprs);
            if (x.kind === "expressionResult") exprs(x.value);
            if (x.kind === "randomChoice") x.choices.forEach(exprs);
            if (x.kind === "array") x.items.forEach(exprs);
            if (x.kind === "structure" || x.kind === "functionCall") fields(x.fields);
        };
        const fields = (fs: Field[]) => {
            for (const f of fs) {
                if ("value" in f) exprs(f.value);
                else if ("kind" in f) f.branches.forEach(fields);
            }
        };
        for (const x of xs) {
            if (x.kind === "call") {
                fields(x.fields);
                exprs(x.byExpr);
                exprs(x.onExpr);
            } else if (x.kind === "assign") exprs(x.value);
            else if (x.kind === "return") fields(x.fields);
            else if (x.kind === "if") {
                if (x.condition.kind === "call") reads([x.condition], s, module);
                else if (x.condition.kind === "assign") exprs(x.condition.value);
                else exprs(x.condition);
                reads(x.yes, s, module);
                reads(x.no, s, module);
            } else if (x.kind === "loop") {
                reads(x.body, s, module);
                fields(x.repeatFields);
            } else if (x.kind === "randomBlock") {
                for (const b of x.branches) reads(b, s, module);
            } else if (x.kind === "switch") {
                fields(x.fields);
                for (const c of x.cases) {
                    for (const l of c.labels) fields(l.fields);
                    reads(c.body, s, module);
                }
                if (x.fallback) reads(x.fallback, s, module);
            }
        }
    };
    for (const [n, s] of scripts) for (const body of scriptBodies.get(n) ?? []) reads(body.def.body, s, body.module);
    return {
        sites,
        unknownCommands,
        undefinedCalls,
        dynamicSites,
        changedGlobals: [...changedGlobals].map(nameOf),
        rounds,
        scripts,
        nameOf,
        entry,
        cxxNames,
        named,
    };
}

// Script names the C++ itself uses: hash_from_text("...") texts and checksum constants in src/ that are scripts' names.
export function cxxScriptNames(src: string, scriptNames: Set<number>): Set<number> {
    const out = new Set<number>();
    const walkSrc = (p: string): string[] =>
        statSync(p).isDirectory()
            ? readdirSync(p).flatMap((n) => walkSrc(join(p, n)))
            : /\.(cpp|h)$/.test(p)
              ? [p]
              : [];
    for (const file of walkSrc(src)) {
        const text = readFileSync(file, "utf8");
        for (const m of text.matchAll(/hash_from_text\("([^"]+)"\)/g)) {
            const n = checksum(m[1]!);
            if (scriptNames.has(n)) out.add(n);
        }
        for (const m of text.matchAll(/0x([0-9A-Fa-f]{8})\b/g)) {
            const n = Number.parseInt(m[1]!, 16) >>> 0;
            if (scriptNames.has(n)) out.add(n);
        }
    }
    return out;
}

const walkQb = (path: string): string[] =>
    !existsSync(path)
        ? []
        : statSync(path).isDirectory()
          ? readdirSync(path).flatMap((n) => walkQb(join(path, n)))
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
    if (!game) throw Error("Usage: node tools/qbjs/typecheck.ts --game <folder> [--models file] [--json out.json]");
    const root = gameRoot(game);
    const modules: ModuleIR[] = [];
    const named = new Set<number>();
    const globals = new Map<number, Ty>();
    const where = new Map<number, Set<string>>();
    const triggers = new Map<number, Ty>();
    for (const base of [join(root, "Data"), join(root, "thug2-decomp-data")])
        for (const sub of ["scripts", "levels"])
            for (const file of walkQb(join(base, sub))) {
                try {
                    modules.push(
                        digest(readIR(readFileSync(file), relative(root, file)), named, globals, where, triggers),
                    );
                } catch {
                    // (stays QB)
                }
            }
    const modelsPath = value("--models") ?? join(import.meta.dirname, "command_models.json");
    const models = existsSync(modelsPath)
        ? (JSON.parse(readFileSync(modelsPath, "utf8")) as Record<string, CommandModel>)
        : {};
    const scriptNames = new Set<number>();
    for (const m of modules) for (const d of m.definitions) if (d.kind === "script") scriptNames.add(d.name);
    const cxx = cxxScriptNames(join(import.meta.dirname, "..", "..", "src"), scriptNames);
    const entriesPath = join(import.meta.dirname, "entry_models.json");
    const entries = existsSync(entriesPath)
        ? (JSON.parse(readFileSync(entriesPath, "utf8")) as Record<string, { params: unknown; site: string }[]>)
        : {};
    const result = typecheck(modules, models, cxx, { named, globals, where, triggers, entries });
    const proven = result.sites.filter((s) => s.ty.why === 0);
    const single = proven.filter((s) => [...s.ty.kinds].filter((k) => k !== "none").length === 1);
    console.log(
        `${modules.length} modules, ${result.scripts.size} scripts, ${result.rounds} rounds; ${result.sites.length} parameter reads: ${proven.length} proven (${single.length} one kind), ${result.sites.length - proven.length} with unknowns`,
    );
    console.log(
        `unknowns: ${result.unknownCommands.size} commands without a model, ${result.cxxNames.size} scripts the C++ names, ${[...result.scripts.keys()].filter((n) => result.named.has(n)).length} scripts named as values, ${result.dynamicSites.length} dynamic calls, ${result.changedGlobals.length} globals changed at run time`,
    );
    // The report: every unknown, by kind, with the sites it reaches (--json for the artifact, --doc for the .md).
    const why = new Map<string, number>();
    for (const s of result.sites)
        for (const r of WHY_SETS[s.ty.why]!) why.set(REASONS[r]!, (why.get(REASONS[r]!) ?? 0) + 1);
    const namedScripts = [...result.scripts.entries()].filter(([n]) => result.named.has(n) && !result.cxxNames.has(n));
    const byModule = new Map<string, number>();
    for (const [, sc] of namedScripts) byModule.set(sc.module, (byModule.get(sc.module) ?? 0) + 1);
    const commands = [...result.unknownCommands]
        .map(([name, u]) => ({ name, scripts: u.scripts.size, sites: why.get(`outputs of ${name} (no model)`) ?? 0 }))
        .sort((x, y) => y.sites - x.sites || x.name.localeCompare(y.name));
    const kinds: Record<string, number> = {};
    for (const s of result.sites) {
        const seen = new Set(WHY_SETS[s.ty.why]!.map((r) => reasonKind(REASONS[r]!)));
        for (const k of seen) kinds[k] = (kinds[k] ?? 0) + 1;
    }
    // How the scripts named in data are named (the mechanism that may start them: a structure member, a command's
    // argument...), names spelled out.
    const ways = new Map<string, number>();
    for (const [n] of namedScripts)
        for (const how of where.get(n) ?? ["(not recorded)"]) {
            const text = how.replace(/0x([0-9a-f]{8})/g, (_m, h: string) => result.nameOf(Number.parseInt(h, 16)));
            ways.set(text, (ways.get(text) ?? 0) + 1);
        }
    const report = {
        undefinedCalls: [...result.undefinedCalls].sort(),
        ways: [...ways].sort((x, y) => y[1] - x[1]),
        kinds,
        date: new Date().toISOString().slice(0, 10),
        modules: modules.length,
        scripts: result.scripts.size,
        sites: result.sites.length,
        proven: proven.length,
        single: single.length,
        models: Object.keys(models).length,
        commands,
        cxx: [...result.cxxNames].map((n) => ({ name: result.nameOf(n), module: result.scripts.get(n)?.module ?? "" })),
        named: namedScripts.map(([n, sc]) => ({ name: result.nameOf(n), module: sc.module })),
        namedByModule: [...byModule].sort((x, y) => y[1] - x[1]),
        dynamic: result.dynamicSites,
        globals: result.changedGlobals,
        reasons: [...why].sort((x, y) => y[1] - x[1]),
    };
    const out = value("--json");
    if (out) {
        mkdirSync(join(out, ".."), { recursive: true });
        writeFileSync(out, JSON.stringify(report));
    }
    const doc = value("--doc");
    if (doc) writeFileSync(doc, unknownsDoc(report));
}

// docs/qb-types-unknowns.md: what keeps types from being proven, each kind with what it reaches and what resolves it.
// The kind of an unknown, by its reason's text.
export function reasonKind(reason: string): string {
    return reason.startsWith("outputs of a command named by a parameter")
        ? "dynamic call"
        : reason.startsWith("outputs of")
          ? "command without a model"
          : reason.startsWith("a script the C++")
            ? "started by the C++"
            : reason.startsWith("a script named as a value")
              ? "named in data"
              : reason.startsWith("global changed")
                ? "global changed at run time"
                : "precision";
}

export function unknownsDoc(r: {
    undefinedCalls: string[];
    ways: [string, number][];
    kinds: Record<string, number>;
    date: string;
    modules: number;
    scripts: number;
    sites: number;
    proven: number;
    single: number;
    models: number;
    commands: { name: string; scripts: number; sites: number }[];
    cxx: { name: string; module: string }[];
    named: { name: string; module: string }[];
    namedByModule: [string, number][];
    dynamic: string[];
    globals: string[];
    reasons: [string, number][];
}): string {
    const pct = (n: number) => (r.sites ? `${((100 * n) / r.sites).toFixed(1)}%` : "0%");
    const byKind = new Map<string, number>(Object.entries(r.kinds));
    const lines = [
        "# QB types: the unknowns",
        "",
        `Generated by \`node tools/qbjs/typecheck.ts --game <folder> --doc docs/qb-types-unknowns.md\` (${r.date}) - do not`,
        "edit by hand. The type checker (docs/qb-types.md) proves, for every parameter read in every script, the set of kinds it",
        "can hold on every path - except where something outside the scripts decides it. Each such thing is an **unknown**,",
        "listed here with the reads it reaches. A read with no unknown is proven; one with any is not (it keeps the exact",
        "form in the natural TypeScript).",
        "",
        `${r.modules} modules, ${r.scripts} scripts, ${r.sites} parameter reads: **${r.proven} proven** (${pct(r.proven)}; ${r.single} of a single kind), ${r.sites - r.proven} reached by an unknown. ${r.models} command models.`,
        "",
        "| Kind of unknown | How many | Parameter reads it reaches (a read may be reached by several kinds) |",
        "| --- | --- | --- |",
        `| Commands without a model of what they write into the caller's parameters | ${r.commands.length} | ${byKind.get("command without a model") ?? 0} |`,
        `| Scripts the C++ starts (named in its code) with parameters it makes | ${r.cxx.length} | ${byKind.get("started by the C++") ?? 0} |`,
        `| Scripts named in data (node arrays, goal tables, menus...) - the C++ may start them | ${r.named.length} | ${byKind.get("named in data") ?? 0} |`,
        `| Dynamic calls (a command named by a parameter whose names can't be told) | ${r.dynamic.length} | ${byKind.get("dynamic call") ?? 0} |`,
        `| Globals a command changes at run time | ${r.globals.length} | ${byKind.get("global changed at run time") ?? 0} |`,
        `| Precision (more names than tracked, members of untracked structures, names standing for any global) | - | ${byKind.get("precision") ?? 0} |`,
        "",
        "How each is resolved: a command gets a model from its C++ (tools/qbjs/command_models.ts mechanically, the rest reviewed",
        "into tools/qbjs/command_models_reviewed.json, each with its source); a script the C++ or the data starts gets a model of",
        "each mechanism that starts it (what parameters it passes); a dynamic call is resolved by the names that can reach it.",
        "",
        "## Commands without a model",
        "",
        "| Command | Scripts calling it | Reads reached |",
        "| --- | --- | --- |",
        ...r.commands.map((c) => `| ${c.name} | ${c.scripts} | ${c.sites} |`),
        "",
        "## Scripts the C++ starts",
        "",
        "Named in src/ (a checksum constant or hash_from_text): the C++ runs them with parameters it makes.",
        "",
        ...r.cxx.map((c) => `- ${c.name} (${c.module})`),
        "",
        "## Scripts named in data",
        "",
        "Named as a value somewhere (not only called): whatever reads that value - a node's TriggerScript, a goal's script, a",
        "menu item's handler, a command's argument - may start it, with parameters the C++ makes. By how they are named (a",
        "script may be named several ways; modelling the mechanism behind one way resolves all the scripts named so):",
        "",
        "| Named as | Scripts |",
        "| --- | --- |",
        ...r.ways.slice(0, 200).map(([w, n]) => `| ${w.replace(/\|/g, "|")} | ${n} |`),
        "",
        "By module (the full list is in the artifact):",
        "",
        "| Module | Scripts |",
        "| --- | --- |",
        ...r.namedByModule.map(([m, n]) => `| ${m} | ${n} |`),
        "",
        "## Dynamic calls",
        "",
        ...r.dynamic.map((d) => `- ${d}`),
        "",
        "## Globals changed at run time",
        "",
        ...(r.globals.length
            ? r.globals.map((g) => `- ${g}`)
            : [
                  "(none modelled yet: until the commands that change globals have models, they are counted under commands)",
              ]),
        "",
        "## Names called but defined nowhere",
        "",
        "No script, C function or member function has these names: the interpreter finds nothing (the call is false) and",
        "nothing is written - not an unknown, listed so they can be looked at (misspellings, commands removed before release).",
        "",
        r.undefinedCalls.join(", ") || "(none)",
        "",
        "## Every reason, with the reads it reaches",
        "",
        "| Reason | Reads |",
        "| --- | --- |",
        ...r.reasons.map(([reason, n]) => `| ${reason.replace(/\|/g, "|")} | ${n} |`),
        "",
    ];
    return lines.join("\n");
}
