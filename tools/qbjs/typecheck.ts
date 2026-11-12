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
import inspector from "node:inspector";
import { join, relative } from "node:path";
import { pathToFileURL } from "node:url";
import { runInNewContext } from "node:vm";
import { CFUNC_NAMES } from "thug2-level-converter-core/level_port/thug2_cfuncs.ts";
import { checksum, foldChecksum } from "../qb_checksum.ts";
import { gameRoot } from "./game_scripts.ts";
import { helperSource } from "./helper-sources.ts";
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
// The inbox and the union of all globals keep one level less of shape (joined as if one level deeper): every read of
// a script reached by dynamic calls goes through them, and their contents are the whole program's.
const SHALLOW = 1;
const NAME_LIMIT = 2048; // (a goal structure's script members name hundreds of scripts: exact sets keep their calls exact)

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
// Shared structures a value may stand for without a copy (the dynamic calls' inbox, the pools): a structure value's
// `lazy` names them, and its members are read through them (typecheck sets these up).
const LAZY = new Map<string, Ty>();
// The names `deep` keeps (typecheck sets them: scripts and globals - what a name held deep is used for: a call or a start
// of a script, a global it names). Others aren't kept (an event type held deep: handlers take every event's payload).
let deepKeeps: Set<number> | undefined;
// The checker's state version (typecheck bumps it whenever a script's parameters / returns, a pool or the inbox change):
// reads of shared values are cached while it stands.
const STATE = { version: 0 };
// Each shared value's version (bumped when it, or a shared value it reads through, grows): its cached member reads
// stand while it does.
const SHARED_VERSIONS = new Map<string, number>();
const sharedMembers = new Map<string, { version: number; byName: Map<number, Ty> }>();
// The shared values `t` reads through, transitively (each once).
function lazyClosure(t: Ty): Set<string> {
    const out = new Set<string>();
    if (!t.lazy) return out;
    const todo = [...t.lazy];
    while (todo.length) {
        const l = todo.pop()!;
        if (out.has(l)) continue;
        const shared = LAZY.get(l);
        if (!shared) continue;
        out.add(l);
        for (const m of shared.lazy ?? []) if (!out.has(m)) todo.push(m);
    }
    return out;
}
// A shared value's own member (cached while it stands; never changed by its users).
function sharedMember(l: string, shared: Ty, n: number): Ty {
    const version = SHARED_VERSIONS.get(l) ?? 0;
    let entry = sharedMembers.get(l);
    if (!entry || entry.version !== version) {
        entry = { version, byName: new Map() };
        sharedMembers.set(l, entry);
    }
    const byName = entry.byName;
    let t = byName.get(n);
    if (!t) {
        t = shared.ownMember(n);
        byName.set(n, t);
    }
    return t;
}

export class Ty {
    kinds = new KindSet();
    names: Set<number> | null = NO_NAMES; // (shared until a name is added: millions of values hold none)
    elem?: Ty;
    fields?: Map<number, Ty>;
    rest?: Ty;
    // The names held below DEPTH, where shapes aren't kept (null: any name) - so a script named deep in a structure is
    // still seen to reach what that structure reaches (namesAt).
    deep?: Set<number> | null;
    deepShared?: boolean; // (`deep` is another value's set: copied before it is added to)
    lazy?: Set<string>; // shared structures this structure includes (LAZY), read through, never copied
    // The kinds of the values held below DEPTH (shapes not kept): a member or element read there is one of them.
    below?: KindSet;
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
    // `root`: a script's own parameter structure, where a member joined in (an assignment, a command's output) doesn't
    // mean the others may be missing - its members' presence is the starts' (ScriptInfo.present).
    join(b: Ty | undefined, depth = 0, root = false): boolean {
        if (!b) return false;
        let changed = false;
        // Two structure values with different members: a member one lacks may be missing (none).
        const wasStruct = !root && this.kinds.has("struct") && this.fields !== undefined && depth < DEPTH;
        if (wasStruct && b.kinds.has("struct")) {
            if (!b.rest)
                for (const [n, f] of this.fields!)
                    if (!b.fields?.has(n) && !f.kinds.has("none")) {
                        f.kinds.add("none");
                        changed = true;
                    }
            if (!this.rest && b.fields)
                for (const [n, f] of b.fields)
                    if (!this.fields!.has(n)) {
                        const mine = new Ty();
                        mine.join(f, depth + 1);
                        mine.kinds.add("none");
                        this.fields!.set(n, mine);
                        changed = true;
                    }
        }
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
                // (past the limit: any name - sound, the calls it makes reach every script that can be named)
                if (this.names.size > NAME_LIMIT) this.names = null;
            }
        }
        if (b.deep !== undefined) changed = this.addDeep(b.deep, true) || changed;
        if (b.lazy)
            for (const l of b.lazy) {
                if (this.lazy?.has(l)) continue;
                if (!this.lazy) this.lazy = new Set();
                this.lazy.add(l);
                changed = true;
            }
        if (b.below) changed = this.addBelow(b.below) || changed;
        if (depth >= DEPTH) {
            if (b.elem || b.fields || b.rest) {
                changed = this.addDeep(nestedNames(b)) || changed;
                changed = this.addBelow(nestedKinds(b)) || changed;
            }
            return changed;
        }
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
    addBelow(kinds: KindSet): boolean {
        if (!kinds.bits) return false;
        const before = this.below?.bits ?? 0;
        if ((before | kinds.bits) === before) return false;
        this.below ??= new KindSet();
        this.below.bits |= kinds.bits;
        return true;
    }
    // Names joined into `deep`; true when it grew.
    // `shared`: `names` is another value's `deep` (already kept names only): taken by reference while this has none,
    // copied before this first adds to it.
    addDeep(names: Set<number> | null, shared = false): boolean {
        if (this.deep === null) return false;
        if (names === null) {
            this.deep = null;
            return true;
        }
        if (!names.size || this.deep === names) return false;
        if (shared && this.deep === undefined) {
            this.deep = names;
            this.deepShared = true;
            return true;
        }
        let changed = false;
        for (const n of names) {
            if (deepKeeps && !deepKeeps.has(n)) continue;
            if (this.deep?.has(n)) continue;
            if (!this.deep) this.deep = new Set();
            else if (this.deepShared) {
                this.deep = new Set(this.deep);
                this.deepShared = false;
            }
            this.deep.add(n);
            changed = true;
        }
        if (this.deep && this.deep.size > 1 << 16) this.deep = null;
        return changed;
    }
    member(n: number): Ty {
        const t = this.ownMember(n);
        // (the shared values it reads through, and those they read through: each once)
        for (const l of lazyClosure(this)) {
            const shared = LAZY.get(l)!;
            if (!shared.fields?.has(n) && !shared.rest && !(shared.below && !shared.fields)) continue;
            t.join(sharedMember(l, shared, n));
        }
        return t;
    }
    // Its own member n (not through the shared values it reads).
    ownMember(n: number): Ty {
        const t = new Ty();
        const f = this.fields?.get(n);
        if (f) t.join(f);
        if (this.rest) t.join(this.rest);
        if (!f) t.kinds.add("none");
        if (!f && !this.fields && !this.rest && this.below) t.join(belowTy(this));
        if (this.deep !== undefined) t.addDeep(this.deep, true);
        return t;
    }
    text(): string {
        const kinds = KINDS.filter((k) => this.kinds.has(k)).map((k) =>
            k === "array" && this.elem ? `array<${this.elem.text()}>` : k,
        );
        return kinds.join("|") || "never";
    }
}

// Every name held anywhere below `t` (its members, elements, the rest, `deep`); null: any name.
function nestedNames(t: Ty, out: Set<number> = new Set(), seen = new Set<Ty>()): Set<number> | null {
    if (seen.has(t)) return out;
    seen.add(t);
    const add = (x: Ty | undefined): boolean => {
        if (!x) return true;
        if (x.names === null && (x.kinds.has("name") || x.kinds.has("script"))) return false;
        for (const n of x.names ?? []) out.add(n);
        return nestedNames(x, out, seen) !== null;
    };
    if (t.deep === null) return null;
    for (const n of t.deep ?? []) out.add(n);
    for (const l of t.lazy ?? []) {
        const shared = LAZY.get(l);
        if (shared && nestedNames(shared, out, seen) === null) return null;
    }
    if (!add(t.elem) || !add(t.rest)) return null;
    for (const f of t.fields?.values() ?? []) if (!add(f)) return null;
    return out;
}

// The kinds of every value held anywhere below `t` (its members, elements, the rest, `below`).
function nestedKinds(t: Ty, out: KindSet = new KindSet(), seen = new Set<Ty>()): KindSet {
    if (seen.has(t)) return out;
    seen.add(t);
    if (t.below) out.bits |= t.below.bits;
    for (const x of [t.elem, t.rest, ...(t.fields?.values() ?? [])]) {
        if (!x) continue;
        out.bits |= x.kinds.bits;
        nestedKinds(x, out, seen);
    }
    return out;
}
// A member or element read where shapes weren't kept: one of the kinds held below (or none), with the names held there.
export function belowTy(t: Ty): Ty {
    const out = new Ty();
    out.kinds.bits = t.below!.bits;
    out.kinds.add("none");
    out.below = new KindSet();
    out.below.bits = t.below!.bits;
    if (t.deep !== undefined) out.addDeep(t.deep, true);
    out.names = t.deep === null ? null : new Set(t.deep ?? []);
    out.why = t.why;
    return out;
}

// The names a value may hold: its own and those kept below the depth limit (null: any name).
export function allNames(t: Ty): Set<number> | null {
    if (t.names === null && (t.kinds.has("name") || t.kinds.has("script"))) return null;
    if (t.deep === null) return null;
    if (!t.deep?.size) return t.names ?? NO_NAMES;
    return new Set([...(t.names ?? []), ...t.deep]);
}

// The value at `path` inside `t` (start_mechanisms.json's paths): member names (text or 0x + checksum), "[]" any
// element, "*member" any member, "@global" the global(s) the value names. Names kept below the depth limit come along
// (in `deep`); a step into something not tracked gives an unknown.
// The elements of the shared values `t` reads through (LAZY): an element read joins them.
export function lazyElems(t: Ty, into: Ty, seen = new Set<string>()) {
    // (each shared value once per read: they reference each other densely)
    for (const l of t.lazy ?? []) {
        if (seen.has(l)) continue;
        seen.add(l);
        const shared = LAZY.get(l);
        if (!shared) continue;
        if (shared.elem) into.join(shared.elem);
        if (shared.below && shared.kinds.has("array")) into.join(belowTy(shared));
        if (shared.lazy) lazyElems(shared, into, seen);
    }
}
// (set by typecheck: the value of any global, for a global named by a value whose names can't be told)
let anyGlobalOf: (() => Ty) | undefined;
// "@paramOrGlobal": the caller's parameter of that name when it has one, else the global (the array-data commands);
// "[]*": the elements at every depth (an array of arrays of any depth).
export function tyAt(
    t: Ty,
    path: readonly string[],
    globalOf: (n: number) => Ty | undefined,
    paramOf?: (n: number) => Ty,
): Ty {
    let at = t;
    for (const step of path) {
        const next = new Ty();
        if (at.deep !== undefined) next.addDeep(at.deep, true);
        if (step === "[]") {
            if (at.elem) next.join(at.elem);
            else if (at.kinds.has("array") && at.below) next.join(belowTy(at));
            else if (at.kinds.has("array") && !at.lazy)
                next.join(Ty.unknown("an array whose elements weren't tracked"));
            lazyElems(at, next);
        } else if (step === "*member") {
            for (const f of at.fields?.values() ?? []) next.join(f);
            if (at.rest) next.join(at.rest);
            for (const l of lazyClosure(at)) {
                const shared = LAZY.get(l)!;
                for (const f of shared.fields?.values() ?? []) next.join(f);
                if (shared.rest) next.join(shared.rest);
                if (shared.below && !shared.fields && !shared.rest) next.join(belowTy(shared));
            }
            if (at.kinds.has("struct") && !at.fields && !at.rest && !at.lazy)
                next.join(at.below ? belowTy(at) : Ty.unknown("a member of a structure whose members weren't tracked"));
        } else if (step === "@global" || step === "@paramOrGlobal") {
            const names = allNames(at);
            if (names === null)
                next.join(
                    anyGlobalOf ? anyGlobalOf() : Ty.unknown("a global named by a value whose names can't be told"),
                );
            else for (const n of names) next.join(globalOf(n));
            if (step === "@paramOrGlobal" && paramOf) {
                if (names === null) next.join(Ty.unknown("a parameter named by a value whose names can't be told"));
                else for (const n of names) next.join(paramOf(n));
            }
        } else if (step === "[]*") {
            for (let x: Ty | undefined = at; x; x = x.elem) {
                if (x !== at) next.join(x);
                if (x.below) next.join(belowTy(x));
            }
        } else {
            if (at.kinds.has("struct") && !at.fields && !at.rest && !at.lazy)
                next.join(at.below ? belowTy(at) : Ty.unknown("a member of a structure whose members weren't tracked"));
            next.join(at.member(pathKey(step)));
        }
        at = next;
    }
    return at;
}
export const pathKey = (k: string) => (/^0x[0-9a-f]{8}$/i.test(k) ? Number.parseInt(k, 16) >>> 0 : checksum(k));

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
    for (const op of [7, 8, 10, 11, 12, 13, 18, 20, 50, 51])
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
    // structures it merges into the caller's parameters, from elsewhere: a pool (start_mechanisms.json), an argument
    // path, or the return values of the scripts it starts (an event handler / on-exit script runs as an interrupt of
    // the caller and its returns are merged in); `under`: put under the members named by that argument instead
    // `member`: under that member; `value`: a value source (an output whose content comes from the arguments, a global...)
    outputsFrom?: ((
        | { pool: string }
        | { argumentPath: string[] }
        | { startedReturns: true }
        | { value: ValueSource }
    ) & {
        under?: string[];
        member?: string;
    })[];
    source?: string;
    uncertain?: string;
    notes?: string;
};
// A started script's parameters: an argument's name, "*" (the command's arguments), "callerParams", "none", "unknown",
// or members with kinds ("name?" may be missing).
type Start = { script: string; params?: string | { members: Record<string, string> }; how?: string };
// A value from elsewhere (command models' outputsFrom): a type spec, an argument path, a pool, a global's path, the
// globals matching a pattern, or an array / structure ("*member": any member) of such.
export type ValueSource =
    | TypeSpec
    | { argumentPath: string[] }
    | { pool: string; path?: string[] }
    | { global: string; path: string[] }
    | { globalsMatching: string; path: string[] }
    | { array: ValueSource }
    | { members: Record<string, ValueSource> };
// A member's type in the catalogues: kinds joined by | ("any"), or {"array": <element>}, or {"members": {...}} (a
// structure), or {"kinds": "...", "array"?: ..., "members"?: ...} for a union with a shaped array / structure.
export type TypeSpec = string | { kinds?: string; array?: TypeSpec; members?: Record<string, TypeSpec> };
function specTy(spec: TypeSpec): Ty {
    if (typeof spec === "string") return kindsTy(kindsOf(spec));
    const t = kindsTy(kindsOf(spec.kinds ?? ""));
    if (spec.array !== undefined) {
        t.kinds.add("array");
        t.elem = specTy(spec.array);
    }
    if (spec.members) {
        const m = membersTy(spec.members);
        t.kinds.add("struct");
        t.fields = m.fields ?? new Map();
    }
    return t;
}
function membersTy(members: Record<string, TypeSpec>): Ty {
    const t = Ty.of("struct");
    t.fields = new Map();
    for (const [k, spec] of Object.entries(members)) {
        if (k === "*" || k === "*member") {
            restOf(t).join(specTy(spec)); // (members of any name)
            continue;
        }
        const optional = k.endsWith("?");
        const m = specTy(spec);
        if (optional) m.kinds.add("none");
        t.fields.set(memberKey(optional ? k.slice(0, -1) : k), m);
    }
    return t;
}
const memberKey = (k: string) => (/^0x[0-9a-f]{8}$/i.test(k) ? Number.parseInt(k, 16) >>> 0 : checksum(k));
// A value of these kinds made outside the scripts (a command's output, a catalogue's member): a name or script it holds
// may be any (the C++ makes it) - unless a catalogue says which (FormatText's made names, pools of the scripts' own).
const kindsTy = (kinds: Kind[]): Ty => {
    const t = Ty.of(...kinds);
    if (t.kinds.has("name") || t.kinds.has("script")) t.names = null;
    return t;
};
const kindsOf = (text: string): Kind[] =>
    text === "any" ? [...KINDS] : (text.split("|").filter((k) => (KINDS as readonly string[]).includes(k)) as Kind[]);

// tools/qbjs/start_mechanisms.json: every way the C++ starts a script whose name it got from the scripts or the data
// (docs/qb-types.md "Start mechanisms"). An entry is where the name comes in - a command's argument path, a global, a
// node member, any structure member of that key, a name pattern - and the parameters the started script gets: "none",
// {members}, {argumentPath} (inside the entry's root: the command's arguments, the global, the node, the structure
// holding the member), "*" (all the command's arguments), {callerParams}, {pool} (a structure fed from several places:
// the goal structure, events' payloads), or "unknown: <what>" - plus members added on top.
export type StartParams =
    | string
    | { members: Record<string, string> }
    | { argumentPath: string[] }
    | { callerParams: true }
    | { pool: string };
export type StartMechanism = {
    entry:
        | { command: string }
        | { global: string }
        | { node: true }
        | { member: string }
        | { pattern: string }
        | { assets: string }
        | { pool: string }
        | { excluded: string };
    path: string[];
    script?: string; // a script the C++ names itself (entry_models.json's, when its parameters come from the entry)
    flowOnly?: boolean; // (a member mechanism for structures the scripts pass, not the data's - node data has its own)
    params: StartParams;
    plus?: Record<string, string> | { argumentPath: string[] } | { pool: string } | { eventType: string[] };
    sites?: string[];
    how?: string;
};
// A pool's sources: argument paths of commands, globals (paths inside them), members the C++ writes.
// A pool's sources: a command's argument path, or a member named by the names at an argument path (`namePath`, of type
// `spec`); a global's path (placed under member `as` when given); the globals whose names match `globalsMatching`;
// members the C++ writes; another pool (read through); an unknown.
export type PoolSource =
    | { command: string; path: string[] }
    | { command: string; namePath: string[]; spec: TypeSpec }
    | { global: string; path: string[]; as?: string }
    | { globalsMatching: string; path: string[] }
    | { members: Record<string, TypeSpec> }
    | { pool: string }
    | { unknown: string };
// Events: an event handler gets its row's params and the payload of the event that fires it - by event type, from the
// C++'s raisers, the commands that launch events (their type= names, their data=), event tables' rows, and the type
// EndGap folds from its GapID.
export type EventCatalogue = {
    raised: { type: string; payload: "none" | { members: Record<string, string> }; sites?: string[] }[];
    launches: { command: string; typePath: string[]; dataPath: string[] | null }[];
    rowTables: { typeMember: string; payloadMember: string }[];
    folded: { command: string; namePath: string[]; suffix: string; dataPath: string[] }[];
};
export type StartCatalogue = {
    pools?: Record<string, { sources: PoolSource[]; note?: string }>;
    events?: EventCatalogue;
    mechanisms: StartMechanism[];
};

type ScriptInfo = {
    module: string;
    def: Extract<Definition, { kind: "script" }>;
    params: Ty; // its parameter list as a structure
    returns: Ty;
    // The members every start gives it (null: no start seen yet) and its defaults' members: a member in neither may be
    // missing when it is read (none).
    present: Set<number> | null;
    defaults: Set<number>;
    pools: Set<string>; // the pools it is started with (read through, as the inbox: not copied in)
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
        // (a member way is always kept: node mechanisms take their exact names from it)
        if (set.size < 16 || how.startsWith("member ")) set.add(how);
    };
    let inData = true;
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
        if (r.kind === "structure" && triggers && inData && Array.isArray(r.fields)) {
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
    // (a node's TriggerScript params from the data only: a structure a script builds is followed in the flow -
    // start_mechanisms.json's TriggerScript member mechanism)
    for (const d of ir.definitions) {
        inData = d.kind === "global";
        collect(d, 0, 0);
    }
    for (const d of ir.definitions) {
        if (d.kind !== "global") continue;
        const t = globals.get(d.name) ?? new Ty();
        t.join(valueTy(d.value));
        globals.set(d.name, t);
    }
    return { ...ir, definitions: ir.definitions.filter((d) => d.kind === "script") };
}
const TRIGGER_SCRIPT = 0x2ca8a299; // TriggerScript
const FORMAT_TEXT = checksum("FormatText");
const CHECKSUM_NAME = checksum("ChecksumName");
const PARAMS = checksum("params");
const hex8 = (n: number) => `0x${(n >>> 0).toString(16).padStart(8, "0")}`;

// RandomRange's kind (random.js range, ParseRandomRange 0x0046FBD0): an integer when both endpoints are within 1e-5 of
// a whole number (truncated after a +/-0.1 nudge), else a float.
const RANGE_NUDGE = new Float32Array(new Uint32Array([0x3dcccccd]).buffer)[0]!;
const RANGE_TOLERANCE = new Float32Array(new Uint32Array([0x3727c5ac]).buffer)[0]!;
export function rangeKind(x: { a: number; b: number }): Kind {
    const whole = (bits: number) => {
        const f = new Float32Array(new Uint32Array([bits >>> 0]).buffer)[0]!;
        if (!Number.isFinite(f)) return false;
        const n = Math.trunc(f + (f >= 0 ? RANGE_NUDGE : -RANGE_NUDGE));
        return n >= -0x80000000 && n <= 0x7fffffff && Math.abs(f - n) <= RANGE_TOLERANCE;
    };
    return whole(x.a) && whole(x.b) ? "int" : "float";
}

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
            return Ty.of(rangeKind(x));
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
        mechanisms?: StartCatalogue | undefined;
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
                scripts.set(d.name, {
                    module: m.source,
                    def: d,
                    params: new Ty(),
                    returns: new Ty(),
                    present: null,
                    defaults: new Set(),
                    pools: new Set(),
                });
        }

    deepKeeps = new Set([...scripts.keys(), ...globals.keys()]);
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
                if (s) t.join(paramsValue(s));
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
                return Ty.of(rangeKind(x));
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
    // The value of any global: all of them joined once (registered as a shared value) - a value standing for it reads
    // its members and elements through it, by reference.
    const anyGlobalValue = (): Ty => {
        if (!anyGlobal) {
            anyGlobal = new Ty();
            for (const g of globals.values()) anyGlobal.join(g, SHALLOW);
            anyGlobal.names = null;
            LAZY.set("anyGlobal", anyGlobal);
        }
        const t = new Ty();
        t.kinds.bits = anyGlobal.kinds.bits;
        t.names = null;
        t.why = anyGlobal.why;
        t.lazy = new Set(["anyGlobal"]);
        return t;
    };
    anyGlobalOf = anyGlobalValue;
    const resolveNames = (t: Ty): Ty => {
        if (!t.kinds.has("name")) return t;
        const out = new Ty();
        out.join(t);
        if (t.names === null) {
            // (the value of any global: all of them joined, shapes and all - a name that may be any name)
            out.join(anyGlobalValue());
        } else for (const n of t.names) out.join(globalTy(n));
        return out;
    };
    const binaryTy = (op: number, a: Ty, b: Ty, x: Extract<Expr, { kind: "binary" }>): Ty => {
        const out = new Ty();
        out.why = whyUnion(a.why, b.why);
        if (op === 5) {
            // a[i]: the element, or none (not an array, index not an integer or out of range)
            if (a.kinds.has("array") && a.elem) out.join(a.elem);
            else if (a.kinds.has("array") && a.below) out.join(belowTy(a));
            else if (a.kinds.has("array") && !a.lazy) out.join(Ty.unknown("an array whose elements weren't tracked"));
            lazyElems(a, out);
            out.kinds.add("none");
            return out;
        }
        if (op === 8) {
            // a.b: a member, or none
            // (a structure's member is looked up by a name; any other pair of kinds - a pair's dot product - is the
            // operator's, from values.js)
            if (a.kinds.has("struct") && b.kinds.has("name")) {
                const untracked = () =>
                    a.below ? belowTy(a) : Ty.unknown("a member of a structure whose members weren't tracked");
                // (a member named by a value whose names can't be told: any of its members)
                if (b.names === null)
                    out.join(a.fields || a.rest || a.lazy ? tyAt(a, ["*member"], globalOf) : untracked());
                else for (const n of b.names) out.join(a.member(n));
                if (!a.fields && !a.rest && !a.lazy) out.join(untracked());
            }
            for (const ka of a.kinds)
                for (const kb of b.kinds) {
                    if (ka === "struct" && kb === "name") continue;
                    for (const r of ops.get(`8 ${ka} ${kb}`) ?? []) out.kinds.add(r);
                }
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
                    const whole = paramsValue(s);
                    for (const [n, v] of whole.fields ?? []) put(n, v);
                    if (whole.rest) restOf(t).join(whole.rest);
                    if (whole.lazy) t.lazy = new Set([...(t.lazy ?? []), ...whole.lazy]);
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
                // (a structure whose members weren't kept - below the depth limit: members of any name, of the kinds
                // held there; the shared structures it reads through, by reference)
                if (!v.fields && !v.rest && v.below) restOf(t).join(belowTy(v));
                if (v.lazy) t.lazy = new Set([...(t.lazy ?? []), ...v.lazy]);
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
    LAZY.clear();
    LAZY.set("inbox", inbox);
    const inboxPools = new Set<string>(); // (pools any nameable script may be started with)
    const reachedByInbox = (s: ScriptInfo) =>
        named.has(s.def.name) && (inbox.fields !== undefined || inboxPools.size > 0);
    // (member reads are cached until the state - scripts' parameters, returns, presence, pools, the inbox - changes:
    // a read's result is never changed by its users)
    const memberCache = new Map<ScriptInfo, Map<number, Ty>>();
    let cacheVersion = -1;
    const memberOf = (s: ScriptInfo, n: number): Ty => {
        if (cacheVersion !== STATE.version) {
            memberCache.clear();
            cacheVersion = STATE.version;
        }
        let byScript = memberCache.get(s);
        const hit = byScript?.get(n);
        if (hit) return hit;
        const t = readMember(s, n);
        if (!byScript) {
            byScript = new Map();
            memberCache.set(s, byScript);
        }
        byScript.set(n, t);
        return t;
    };
    const readMember = (s: ScriptInfo, n: number): Ty => {
        const t = s.params.member(n);
        const dynamic = reachedByInbox(s);
        if (dynamic) {
            t.join(inbox.member(n));
            for (const p of inboxPools) t.join(poolTy(p).member(n));
        }
        if (!s.defaults.has(n) && (dynamic || !s.present?.has(n))) t.kinds.add("none");
        return t;
    };
    // A start of `target` with `t` (null: one that guarantees no member - a pool, the C++'s untold structures).
    const started = (target: ScriptInfo, t: Ty | null) => {
        const keys = new Set<number>();
        if (t) for (const [n, f] of t.fields ?? []) if (!f.kinds.has("none")) keys.add(n);
        const before = target.present;
        ++STATE.version;
        if (before === null) target.present = keys;
        else {
            const kept = new Set([...before].filter((n) => keys.has(n)));
            if (kept.size === before.size) return;
            target.present = kept;
        }
        enqueue(target.def.name);
    };
    // The parameters as a structure value (<...>, a spread): each member as read (none where it may be missing).
    // The parameters as a structure value (<...>, a spread, callerParams): its own members as read (none where one may
    // be missing), and the shared structures it reads through (the inbox, pools) by reference.
    const paramsValue = (s: ScriptInfo): Ty => {
        const t = Ty.of("struct");
        t.fields = new Map();
        // (its own members only: what the shared structures add is read through them, by reference)
        for (const [n, f] of s.params.fields ?? []) {
            const m = new Ty();
            m.join(f);
            if (!s.defaults.has(n) && (reachedByInbox(s) || !s.present?.has(n))) m.kinds.add("none");
            t.fields.set(n, m);
        }
        if (s.params.rest) t.rest = s.params.rest;
        if (s.params.why) t.why = s.params.why;
        const lazy = new Set(s.params.lazy ?? []);
        if (reachedByInbox(s)) for (const l of ["inbox", ...inboxPools]) lazy.add(l);
        if (lazy.size) t.lazy = lazy;
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
        if (!target[into].join(t, 0, into === "params")) return;
        ++STATE.version;
        if (into === "returns" && named.has(target.def.name)) poolGrow("anyReturns", t);
        for (const l of target[into].lazy ?? [])
            if (!target.pools.has(l)) {
                target.pools.add(l);
                pool(l).subs.add(target.def.name);
                if (into === "returns") for (const c of callers.get(target.def.name) ?? []) pool(l).subs.add(c);
            }
        if (into === "params") enqueue(target.def.name);
        else for (const c of callers.get(target.def.name) ?? []) enqueue(c);
    };
    // --- start mechanisms (start_mechanisms.json) ---
    const catalogue = pre.mechanisms ?? { mechanisms: [] };
    const globalOf = (n: number) => globalTy(n);
    // pools: a structure fed from several places; every script started with it is grown when it grows
    const pools = new Map<string, { ty: Ty; subs: Set<number> }>();
    const pool = (name: string) => {
        let p = pools.get(name);
        if (!p && name !== "inbox" && LAZY.has(name)) {
            // (a shared value registered elsewhere: the union of commands' outputs, of globals)
            p = { ty: LAZY.get(name)!, subs: new Set() };
            pools.set(name, p);
        }
        if (!p && name === "inbox") {
            p = { ty: inbox, subs: new Set() };
            pools.set(name, p);
            LAZY.set(name, inbox);
        }
        if (!p) {
            p = { ty: Ty.of("struct"), subs: new Set() };
            p.ty.fields = new Map();
            LAZY.set(name, p.ty);
            // (an event type's payloads: event:<checksum>, event:* all of them, event:? launches of types not told)
            if (!catalogue.pools?.[name] && !name.startsWith("event:") && name !== "anyReturns")
                p.ty.join(Ty.unknown(`pool ${name} has no catalogued sources`));
            pools.set(name, p);
        }
        return p;
    };
    const poolTy = (name: string) => pool(name).ty;
    const poolReaders = new Map<string, Set<string>>(); // pool -> the pools that read it through
    // (a shared value that took references to others in a join reads them through: it is one of their readers)
    const noteReaders = (name: string, ty: Ty) => {
        for (const l of ty.lazy ?? []) {
            if (l === name) continue;
            let readers = poolReaders.get(l);
            if (!readers) {
                readers = new Set();
                poolReaders.set(l, readers);
            }
            readers.add(name);
        }
    };
    const poolStarts = new Map<string, StartMechanism[]>(); // pool -> the starts whose parameters are inside it
    const pendingPoolStarts = new Set<string>();
    const runPendingPoolStarts = () => {
        for (const key of pendingPoolStarts) {
            pendingPoolStarts.delete(key);
            const [name, index] = key.split("|");
            runPoolStart(name!, catalogue.mechanisms[Number(index)]!);
        }
    };
    let runPoolStart: (name: string, m: StartMechanism) => void = () => {};
    const poolGrow = (name: string, t: Ty) => {
        const p = pool(name);
        if (!p.ty.join(t, SHALLOW)) return;
        noteReaders(name, p.ty);
        sharedGrew(name);
        if (name.startsWith("event:") && name !== "event:*") poolGrow("event:*", t);
    };
    // A shared value grew: its readers, and the readers of the shared values that read it through, walk again.
    const sharedGrew = (name: string) => {
        ++STATE.version;
        const touched = new Set<string>([name]);
        for (const q of touched) for (const r of poolReaders.get(q) ?? []) touched.add(r);
        for (const q of touched) {
            SHARED_VERSIONS.set(q, (SHARED_VERSIONS.get(q) ?? 0) + 1);
            for (const n of pools.get(q)?.subs ?? [])
                if (n === -1) for (const d of dynamicTargets()) enqueue(d);
                else enqueue(n);
            // (run before the next walk, once however often the pool grows meanwhile)
            for (const m of poolStarts.get(q) ?? []) pendingPoolStarts.add(`${q}|${catalogue.mechanisms.indexOf(m)}`);
        }
    };
    // The payload pools of the event types `names` names (null: any type).
    const eventPools = (names: Set<number> | null): string[] =>
        names === null
            ? ["event:*"]
            : [...[...names].filter((n) => !scripts.has(n)).map((n) => `event:${n}`), "event:?"];
    const subscribe = (name: string, target: ScriptInfo) => {
        const p = pool(name);
        if (target.pools.has(name)) return;
        p.subs.add(target.def.name);
        target.pools.add(name);
        if (!target.params.lazy) target.params.lazy = new Set();
        target.params.lazy.add(name);
        ++STATE.version;
        started(target, null);
        enqueue(target.def.name);
    };
    const plusTy = (plus: Record<string, string>): Ty => {
        const t = Ty.of("struct");
        t.fields = new Map();
        for (const [k, v] of Object.entries(plus)) {
            if (k.startsWith("*")) {
                restOf(t).join(Ty.unknown(`members merged in by the C++: ${v}`));
                continue;
            }
            const optional = k.endsWith("?");
            const m = kindsTy(kindsOf(/^[a-z|]+/.exec(v)?.[0] ?? "any"));
            if (optional) m.kinds.add("none");
            t.fields.set(pathKey(optional ? k.slice(0, -1) : k), m);
        }
        return t;
    };
    // The scripts `names` names (null: any that can be named) started with `m`'s parameters; `root` is the entry's
    // root (argument paths are inside it), `caller` the calling script (callerParams).
    const nullStarts = new Map<string, number>(); // (diagnostic: TYPECHECK_NULLSTARTS)
    const startWith = (names: Set<number> | null, m: StartMechanism, root: Ty, caller?: ScriptInfo) => {
        if (names === null && process.env.TYPECHECK_NULLSTARTS) {
            const k = `${JSON.stringify(m.entry)} ${m.path.join(".")} <- ${caller ? nameOf(caller.def.name) : "setup"}`;
            nullStarts.set(k, (nullStarts.get(k) ?? 0) + 1);
        }
        const targets = (names === null ? [] : [...names]).flatMap((n) => {
            const sc = scripts.get(n);
            return sc ? [sc] : [];
        });
        if (!targets.length && names !== null) return;
        const parts: (Ty | string)[] = [];
        const add = (p: StartParams | NonNullable<StartMechanism["plus"]>) => {
            if (typeof p === "string") {
                if (p === "*") parts.push(root);
                else if (p.startsWith("unknown")) {
                    const t = Ty.of("struct");
                    t.rest = Ty.unknown(`parameters a script started by the C++ gets: ${p.slice(8).trim()}`);
                    parts.push(t);
                } else parts.push(Ty.of("struct")); // "none"
            } else if ("members" in p && typeof p.members === "object") parts.push(membersTy(p.members));
            else if ("argumentPath" in p && Array.isArray(p.argumentPath)) {
                const at = tyAt(root, p.argumentPath, globalOf);
                // ("as": under that member)
                const as = (p as { as?: string }).as;
                if (as === undefined) parts.push(at);
                else {
                    const t = Ty.of("struct");
                    t.fields = new Map([[pathKey(as), at]]);
                    parts.push(t);
                }
            } else if ("callerParams" in p)
                parts.push(caller ? paramsValue(caller) : Ty.unknown("the caller's parameters"));
            else if ("pool" in p) parts.push(`pool:${p.pool}`);
            else if ("eventType" in p && Array.isArray(p.eventType)) {
                const at = tyAt(root, p.eventType, globalOf);
                for (const e of eventPools(allNames(at))) parts.push(`pool:${e}`);
                // (types held below the depth limit aren't kept: any event's payload)
                if (at.deep !== undefined) parts.push("pool:event:*");
            } else parts.push(plusTy(p as Record<string, string>));
        };
        add(m.params);
        if (m.plus) add(m.plus);
        if (names === null) {
            // Any script that can be named: what it is started with goes to the inbox they all read (as a dynamic
            // call's arguments), not into each.
            let grew = false;
            for (const part of parts)
                if (typeof part === "string") {
                    if (!inboxPools.has(part.slice(5))) {
                        inboxPools.add(part.slice(5));
                        pool(part.slice(5)).subs.add(-1);
                        grew = true;
                    }
                } else grew = inbox.join(part, SHALLOW, true) || grew;
            if (grew) {
                pool("inbox");
                noteReaders("inbox", inbox);
                sharedGrew("inbox");
            }
            if (grew) {
                ++STATE.version;
                for (const n of dynamicTargets()) enqueue(n);
                for (const n of pools.get("inbox")?.subs ?? []) enqueue(n);
            }
            return;
        }
        for (const target of targets) {
            // (the parts are merged: a member any of them gives is present)
            const given = Ty.of("struct");
            given.fields = new Map();
            for (const part of parts)
                if (typeof part !== "string")
                    for (const [n, f] of part.fields ?? []) if (!f.kinds.has("none")) given.fields.set(n, f);
            if (parts.every((p) => typeof p !== "string")) started(target, given);
            for (const part of parts) {
                if (typeof part === "string") subscribe(part.slice(5), target);
                else grow(target, "params", part);
            }
        }
    };
    runPoolStart = (name: string, m: StartMechanism) => {
        const root = Ty.of("struct");
        root.lazy = new Set([name]);
        pool(name);
        startWith(new Set([pathKey(m.script!)]), m, root);
    };
    const byCommand = new Map<number, StartMechanism[]>();
    const byMember = new Map<number, StartMechanism[]>();
    const poolByCommand = new Map<number, { pool: string; path?: string[]; namePath?: string[]; spec?: TypeSpec }[]>();
    for (const m of catalogue.mechanisms) {
        if ("command" in m.entry) {
            const k = pathKey(m.entry.command);
            byCommand.set(k, [...(byCommand.get(k) ?? []), m]);
        } else if ("member" in m.entry) {
            const k = pathKey(m.entry.member);
            byMember.set(k, [...(byMember.get(k) ?? []), m]);
        }
    }
    for (const [name, p] of Object.entries(catalogue.pools ?? {}))
        for (const src of p.sources)
            if ("command" in src) {
                const k = pathKey(src.command);
                poolByCommand.set(k, [...(poolByCommand.get(k) ?? []), { pool: name, ...src }]);
            }
    // Structures holding a member a mechanism names (component structures, trick_mapping...): found anywhere in `t`.
    let globalValues: Set<Ty> | undefined; // (scanned once, at setup)
    const scanMembers = (t: Ty, caller?: ScriptInfo, seen = new Set<Ty>()) => {
        if (!byMember.size || seen.has(t) || (caller && globalValues?.has(t))) return;
        seen.add(t);
        for (const [k, f] of t.fields ?? []) {
            for (const m of byMember.get(k) ?? [])
                if (caller || !m.flowOnly) startWith(allNames(tyAt(f, m.path, globalOf)), m, t, caller);
            scanMembers(f, caller, seen);
        }
        if (t.elem) scanMembers(t.elem, caller, seen);
        if (t.rest) scanMembers(t.rest, caller, seen);
    };
    // A command called with `args`: the scripts its catalogued mechanisms start, the pools it feeds.
    const launches = new Map<number, EventCatalogue["launches"]>();
    for (const l of catalogue.events?.launches ?? [])
        launches.set(pathKey(l.command), [...(launches.get(pathKey(l.command)) ?? []), l]);
    const folds = new Map<number, EventCatalogue["folded"]>();
    for (const f of catalogue.events?.folded ?? [])
        folds.set(pathKey(f.command), [...(folds.get(pathKey(f.command)) ?? []), f]);
    const commandStarts = (n: number, args: Ty, caller: ScriptInfo, scan = true) => {
        for (const l of launches.get(n) ?? []) {
            // (type= may be a name, an array of names or a structure of them)
            const at = tyAt(args, l.typePath, globalOf);
            const own = allNames(at);
            const nested = nestedNames(at);
            const types = own === null || nested === null ? null : new Set([...own, ...nested]);
            const data = l.dataPath ? tyAt(args, l.dataPath, globalOf) : Ty.of("struct");
            for (const pname of types === null ? ["event:?"] : [...types].map((t) => `event:${t}`))
                poolGrow(pname, data);
        }
        for (const f of folds.get(n) ?? []) {
            const gaps = allNames(tyAt(args, f.namePath, globalOf));
            const data = tyAt(args, f.dataPath, globalOf);
            if (gaps === null) poolGrow("event:?", data);
            else for (const g of gaps) poolGrow(`event:${foldChecksum(g, f.suffix)}`, data);
        }
        for (const m of byCommand.get(n) ?? [])
            startWith(
                m.script ? new Set([pathKey(m.script)]) : allNames(tyAt(args, m.path, globalOf)),
                m,
                args,
                caller,
            );
        if (!scan) {
            poolSources(n, args);
            return;
        }
        poolSources(n, args);
        scanMembers(args, caller);
    };
    const poolSources = (n: number, args: Ty) => {
        for (const p of poolByCommand.get(n) ?? []) {
            if (p.path) poolGrow(p.pool, tyAt(args, p.path, globalOf));
            else if (p.namePath && p.spec !== undefined) {
                // a member named by an argument: each name it may hold (any member when they can't be told)
                const named = allNames(tyAt(args, p.namePath, globalOf));
                const t = Ty.of("struct");
                t.fields = new Map();
                if (named === null) t.rest = specTy(p.spec);
                else for (const m of named) t.fields.set(m, specTy(p.spec));
                poolGrow(p.pool, t);
            }
        }
    };

    // A value source (models' outputsFrom) for a call with `args` in script `s`.
    const sourceValue = (src: ValueSource, args: Ty, s: ScriptInfo): Ty => {
        if (typeof src === "string") return specTy(src);
        if ("argumentPath" in src) return tyAt(args, src.argumentPath, globalOf, (n) => memberOf(s, n));
        if ("pool" in src) {
            const t = Ty.of("struct");
            t.lazy = new Set([src.pool]);
            pool(src.pool).subs.add(s.def.name);
            s.pools.add(src.pool);
            return src.path ? tyAt(t, src.path, globalOf) : t;
        }
        if ("global" in src) {
            const g = globalTy(pathKey(src.global));
            return g ? tyAt(g, src.path, globalOf) : Ty.of("none");
        }
        if ("globalsMatching" in src) {
            const re = new RegExp(src.globalsMatching, "i");
            const t = new Ty();
            for (const [g, gt] of globals) if (re.test(names.get(g) ?? "")) t.join(tyAt(gt, src.path, globalOf));
            return t;
        }
        const t = kindsTy(kindsOf("kinds" in src && typeof src.kinds === "string" ? src.kinds : ""));
        if ("array" in src && src.array !== undefined) {
            t.kinds.add("array");
            t.elem = sourceValue(src.array as ValueSource, args, s);
        }
        if ("members" in src && src.members) {
            t.kinds.add("struct");
            t.fields = new Map();
            for (const [k, v] of Object.entries(src.members)) {
                const m = sourceValue(v as ValueSource, args, s);
                if (k === "*member") restOf(t).join(m);
                else {
                    if (k.endsWith("?")) m.kinds.add("none");
                    t.fields.set(memberKey(k.endsWith("?") ? k.slice(0, -1) : k), m);
                }
            }
        }
        return t;
    };
    // What a call of a command that can't be told may write into the caller: every model's outputs (members named by
    // an argument: any member of those kinds), its arguments when some command merges them, the structures models take
    // from the arguments or the pools, and the returns of the scripts commands start.
    let staticOutputs: Ty | undefined;
    let argumentModels: CommandModel[] | undefined;
    const dynamicOutputs = (args: Ty, s: ScriptInfo): Ty => {
        if (!staticOutputs) {
            staticOutputs = Ty.of("struct");
            staticOutputs.fields = new Map();
            const lazy = new Set<string>();
            for (const model of modelOf.values()) {
                const t = Ty.of("struct");
                t.fields = new Map();
                for (const [k, kinds] of Object.entries(model.outputs ?? {}))
                    t.fields.set(memberKey(k), kindsTy(kindsOf(kinds)));
                for (const kinds of Object.values(model.byArgument ?? {})) restOf(t).join(kindsTy(kindsOf(kinds)));
                if (model.anyMembers && model.anyMembers !== "any") restOf(t).join(kindsTy(kindsOf(model.anyMembers)));
                for (const o of model.outputsFrom ?? []) {
                    if ("pool" in o && o.under === undefined) lazy.add(o.pool);
                    else if ("pool" in o) {
                        const under = Ty.of("struct");
                        under.lazy = new Set([o.pool]);
                        pool(o.pool);
                        restOf(t).join(under);
                    }
                    if ("startedReturns" in o) lazy.add("anyReturns");
                }
                staticOutputs.join(t);
            }
            for (const l of lazy) pool(l);
            if (lazy.size) staticOutputs.lazy = lazy;
            // (every member may be missing: no one command writes them all)
            for (const f of staticOutputs.fields.values()) f.kinds.add("none");
            LAZY.set("anyOutputs", staticOutputs);
            pool("anyOutputs");
            noteReaders("anyOutputs", staticOutputs);
        }
        // (by reference: the union of every command's outputs is large and never changes)
        const out = Ty.of("struct");
        out.lazy = new Set(["anyOutputs"]);
        for (const l of staticOutputs.lazy ?? []) {
            pool(l).subs.add(s.def.name);
            s.pools.add(l);
        }
        argumentModels ??= [...modelOf.values()].filter(
            (m) => m.mergesArguments || m.outputsFrom?.some((o) => "argumentPath" in o || "value" in o),
        );
        for (const model of argumentModels) {
            if (model.mergesArguments) out.join(args);
            for (const o of model.outputsFrom ?? []) {
                if ("argumentPath" in o) out.join(tyAt(args, o.argumentPath, globalOf));
                if (!("value" in o)) continue;
                const v = sourceValue(o.value, args, s);
                const t = Ty.of("struct");
                t.fields = new Map();
                if (o.member !== undefined) {
                    v.kinds.add("none");
                    t.fields.set(memberKey(o.member), v);
                } else restOf(t).join(v);
                out.join(t);
            }
        }
        return out;
    };

    // FormatText ChecksumName=<x> "<format>": the name it makes is one whose text matches the format, each %-parameter
    // any text - among the names the game's name tables spell (a made name nothing spells is no script, global or
    // member the scripts name). The format as written in the call; one that isn't a literal: any name.
    const nameTexts = [...names].map(([n, t]) => [n, t.toLowerCase()] as const);
    const formatMatches = new Map<string, Set<number> | null>();
    const formatTextNames = (c: Call, args: Ty, s: ScriptInfo) => {
        const target = allNames(args.member(CHECKSUM_NAME));
        if (target === null || !target.size) return;
        let format: string | undefined;
        for (const f of c.fields)
            if ("value" in f && f.name === 0 && f.value.kind === "literal" && f.value.value.type === 3)
                format = String.fromCharCode(...(f.value.value as { bytes: number[] }).bytes);
        let made: Set<number> | null = null;
        if (format !== undefined) {
            if (!formatMatches.has(format)) {
                const quote = (part: string) => part.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
                const pattern = format
                    .toLowerCase()
                    .split(/%[a-z0-9]/)
                    .map(quote)
                    .join(".*");
                const re = new RegExp(`^${pattern}$`);
                const hits = new Set(nameTexts.filter(([, t]) => re.test(t)).map(([n]) => n));
                formatMatches.set(format, hits.size > NAME_LIMIT ? null : hits);
            }
            made = formatMatches.get(format)!;
        }
        const value = Ty.of("name");
        value.names = made;
        const t = Ty.of("struct");
        t.fields = new Map([...target].map((m) => [m, value] as const));
        grow(s, "params", t);
    };

    let rounds = 0;
    const seenDynamic = new Set<string>();
    const firstWalk = new Set<number>();
    const flowCall = (c: Call, s: ScriptInfo) => {
        const args = fieldsTy(c.fields, s);
        let targets: number[] = [];
        let dynamic = false;
        if (c.byExpr !== undefined || c.via !== undefined) {
            const t = c.byExpr !== undefined ? exprTy(c.byExpr, s, "expression") : memberOf(s, c.via!);
            const names = allNames(t);
            if (names === null) dynamic = true;
            else targets = [...names];
        } else if (c.name) targets = [c.name];
        if (dynamic) {
            // Its arguments go to every script that can be named - one inbox they all read (not a copy each) - and,
            // as it may name a command, what it writes into this script's parameters is an unknown.
            const site = `${s.module} ${nameOf(s.def.name)}`;
            if (!seenDynamic.has(site)) {
                seenDynamic.add(site);
                dynamicSites.push(site);
                if (process.env.TYPECHECK_DYNAMIC && dynamicSites.length <= 40) {
                    const t = c.byExpr !== undefined ? exprTy(c.byExpr, s, "expression") : memberOf(s, c.via!);
                    console.log(
                        `typecheck: dynamic #${dynamicSites.length} walk ${rounds} ${site} via ${c.via !== undefined ? nameOf(c.via) : "expression"}: ${t.text()} names ${t.names === null ? "any" : t.names.size} deep ${t.deep === null ? "any" : (t.deep?.size ?? 0)} inbox ${reachedByInbox(s)} why ${WHY_SETS[
                            t.why
                        ]!.map((r) => REASONS[r])
                            .join(" | ")
                            .slice(0, 300)}`,
                    );
                }
            }
            if (inbox.join(args, SHALLOW)) {
                pool("inbox");
                noteReaders("inbox", inbox);
                sharedGrew("inbox");
                ++STATE.version;
                for (const n of dynamicTargets()) enqueue(n);
                for (const n of pools.get("inbox")?.subs ?? []) enqueue(n);
            }
            // (it may name any command: its arguments go through every command's start mechanisms)
            for (const k of new Set([...byCommand.keys(), ...poolByCommand.keys()])) commandStarts(k, args, s, false);
            scanMembers(args, s);
            // ... and so write what any command writes (every command has a model): their outputs joined
            grow(s, "params", dynamicOutputs(args, s));
            // ... and what any script that can be named returns: the returns they all feed, by reference
            const returns = Ty.of("struct");
            returns.lazy = new Set(["anyReturns"]);
            pool("anyReturns").subs.add(s.def.name);
            s.pools.add("anyReturns");
            grow(s, "params", returns);
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
                started(callee, args);
                grow(callee, "params", args);
                grow(s, "params", callee.returns);
                continue;
            }
            // a command: the scripts it starts (start_mechanisms.json), then what it writes into the caller's parameters
            commandStarts(n, args, s);
            if (n === FORMAT_TEXT) formatTextNames(c, args, s);
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
                if (old) old.join(kindsTy(kindsOf(kinds)));
                else t.fields.set(memberKey(k), kindsTy(kindsOf(kinds)));
            }
            for (const [arg, kinds] of Object.entries(model.byArgument ?? {})) {
                // (named by a value whose names can't be told: a member of any name, of those kinds)
                const names = allNames(args.member(memberKey(arg)));
                if (names === null) restOf(t).join(kindsTy(kindsOf(kinds)));
                else for (const m of names) t.fields.set(m, kindsTy(kindsOf(kinds)));
            }
            for (const o of model.outputsFrom ?? []) {
                const parts: Ty[] = [];
                if ("value" in o) parts.push(sourceValue(o.value, args, s));
                else if ("pool" in o) {
                    const t = Ty.of("struct");
                    t.lazy = new Set([o.pool]);
                    pool(o.pool).subs.add(s.def.name);
                    s.pools.add(o.pool);
                    parts.push(t);
                } else if ("argumentPath" in o) parts.push(tyAt(args, o.argumentPath, globalOf));
                else
                    for (const m of byCommand.get(n) ?? []) {
                        const started = allNames(tyAt(args, m.path, globalOf));
                        if (started === null) {
                            // (any script that can be named: the returns they all feed)
                            const t = Ty.of("struct");
                            t.lazy = new Set(["anyReturns"]);
                            pool("anyReturns").subs.add(s.def.name);
                            s.pools.add("anyReturns");
                            parts.push(t);
                            continue;
                        }
                        for (const target of started) {
                            const callee = scripts.get(target);
                            if (!callee) continue;
                            let set = callers.get(target);
                            if (!set) {
                                set = new Set();
                                callers.set(target, set);
                            }
                            set.add(s.def.name);
                            parts.push(callee.returns);
                        }
                    }
                for (const part of parts) {
                    if (o.member !== undefined) {
                        const t = Ty.of("struct");
                        t.fields = new Map([[memberKey(o.member), part]]);
                        grow(s, "params", t);
                        continue;
                    }
                    if (o.under === undefined) {
                        grow(s, "params", part);
                        continue;
                    }
                    const under = allNames(tyAt(args, o.under, globalOf));
                    const t = Ty.of("struct");
                    t.fields = new Map();
                    const value = new Ty();
                    value.join(part);
                    if (under === null) t.rest = value;
                    else for (const m of under) t.fields.set(m, value);
                    grow(s, "params", t);
                }
            }
            if (model.anyMembers) {
                const rest =
                    model.anyMembers === "any"
                        ? Ty.unknown(`outputs of ${nameOf(n)}: a structure of members that can't be listed merged in`)
                        : kindsTy(kindsOf(model.anyMembers));
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
            // (the scripts commands start come from start_mechanisms.json, the complete catalogue of the C++'s starts -
            // not the models' older `starts`, kept in the models as notes)
            for (const start of model.starts === undefined || catalogue.mechanisms.length
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
                            ? paramsValue(s)
                            : start.params === "unknown"
                              ? Object.assign(Ty.of("struct"), {
                                    rest: Ty.unknown(`parameters ${nameOf(n)} gives the scripts it starts`),
                                })
                              : start.params && start.params !== "none"
                                ? args.member(memberKey(start.params))
                                : Ty.of("struct");
                const startedNames = nameField.names === null ? dynamicTargets() : [...nameField.names];
                for (const target of startedNames) {
                    const callee = scripts.get(target);
                    if (callee) {
                        started(callee, params);
                        grow(callee, "params", params);
                    }
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
        for (const body of scriptBodies.get(n) ?? []) {
            const d = fieldsTy(body.def.defaults, s);
            s.params.join(d, 0, true);
            // (a member every definition defaults is always there)
            const keys = new Set(d.fields?.keys() ?? []);
            s.defaults = body === scriptBodies.get(n)![0] ? keys : new Set([...s.defaults].filter((k) => keys.has(k)));
        }
        // Started by the C++ under its name: the parameter lists catalogued for it (an uncatalogued name in src/ is
        // not one it starts - the catalogue is the claim, docs/qb-types.md).
        for (const e of entriesOf.get(n) ?? []) {
            if (e.params === "none") started(s, Ty.of("struct"));
            else if (typeof e.params === "string" && e.params.startsWith("covered"))
                continue; // (start_mechanisms.json)
            else if (typeof e.params === "string" && /(^|\W)(a copy of )?the goal structure/.test(e.params))
                subscribe("goal", s); // (start_mechanisms.json's goal pool)
            else if (typeof e.params === "object" && e.params !== null) {
                const t = membersTy((e.params as { members: Record<string, string> }).members);
                started(s, t);
                s.params.join(t, 0, true);
            } else {
                const t = Ty.of("struct");
                t.rest = Ty.unknown(
                    "a script the C++ starts with parameters it makes that can't be listed (its catalogue entry)",
                );
                started(s, null);
                s.params.join(t, 0, true);
            }
        }
        // A node's TriggerScript gets its node's params (the nodes naming it: `triggers`).
        const trigger = pre.triggers?.get(n);
        if (trigger) {
            const t = Ty.of("struct");
            t.join(trigger.kinds.has("struct") ? trigger : Ty.of("struct"));
            started(s, t);
            s.params.join(t, 0, true);
        }
        enqueue(n);
    }
    // The data's own starts (start_mechanisms.json): globals the C++ reads, node members, name patterns, structures
    // holding a catalogued member anywhere in the globals; and the pools' sources in globals and the C++.
    // (A script named only in ways no mechanism starts isn't started by the C++ - the catalogue is the claim.)
    const nodeArrays = [...globals.keys()].filter((g) => /_nodearray$/i.test(names.get(g) ?? ""));
    // The names given as each member anywhere in the data (digest's `where`: exact, where the joined node type keeps
    // only 64 names a member).
    const byMemberKey = new Map<number, Set<number>>();
    for (const [n, ways] of pre.where ?? [])
        for (const w of ways) {
            const m = /^member 0x([0-9a-f]{8})$/.exec(w);
            if (!m) continue;
            const k = Number.parseInt(m[1]!, 16) >>> 0;
            let set = byMemberKey.get(k);
            if (!set) {
                set = new Set();
                byMemberKey.set(k, set);
            }
            set.add(n);
        }
    for (const m of catalogue.mechanisms) {
        const e = m.entry;
        if ("global" in e) {
            const g = globalTy(pathKey(e.global));
            if (g) startWith(m.script ? new Set([pathKey(m.script)]) : allNames(tyAt(g, m.path, globalOf)), m, g);
        } else if ("pool" in e && m.script) {
            // (a script the C++ starts with a structure built from a pool: the paths are inside the pool - run again
            // whenever it grows)
            poolStarts.set(e.pool, [...(poolStarts.get(e.pool) ?? []), m]);
            runPoolStart(e.pool, m);
        } else if ("node" in e) {
            const exact =
                m.path.length === 1 &&
                m.path[0] === "TriggerScript" &&
                JSON.stringify(m.params) === '{"argumentPath":["params"]}';
            if (exact) continue; // (`triggers` above: each script with its own nodes' params)
            const node = new Ty();
            for (const a of nodeArrays) node.join(tyAt(globalTy(a)!, ["[]"], globalOf));
            const exactNames = m.path.length === 1 ? byMemberKey.get(pathKey(m.path[0]!)) : undefined;
            startWith(exactNames ?? allNames(tyAt(node, m.path, globalOf)), m, node);
        } else if ("pattern" in e) {
            const pattern = e.pattern.toLowerCase();
            const hits = new Set(
                [...scripts.keys()].filter((n) => (names.get(n) ?? "").toLowerCase().includes(pattern)),
            );
            startWith(hits, m, Ty.of("struct"));
        }
    }
    for (const [name, p] of Object.entries(catalogue.pools ?? {}))
        for (const src of p.sources) {
            if ("global" in src) {
                const g = globalTy(pathKey(src.global));
                if (!g) continue;
                const at = tyAt(g, src.path, globalOf);
                if (src.as === undefined) poolGrow(name, at);
                else {
                    const t = Ty.of("struct");
                    t.fields = new Map([[pathKey(src.as), at]]);
                    poolGrow(name, t);
                }
            } else if ("globalsMatching" in src) {
                const re = new RegExp(src.globalsMatching, "i");
                for (const [g, gt] of globals)
                    if (re.test(names.get(g) ?? "")) poolGrow(name, tyAt(gt, src.path, globalOf));
            } else if ("pool" in src) {
                pool(src.pool);
                const t = Ty.of("struct");
                t.lazy = new Set([src.pool]);
                poolGrow(name, t);
                poolReaders.set(src.pool, new Set([...(poolReaders.get(src.pool) ?? []), name]));
            } else if ("members" in src) poolGrow(name, membersTy(src.members));
            else if ("unknown" in src) {
                const t = Ty.of("struct");
                t.rest = Ty.unknown(`pool ${name}: ${src.unknown}`);
                poolGrow(name, t);
            }
        }
    for (const g of globals.values()) scanMembers(g);
    globalValues = new Set(globals.values());
    for (const r of catalogue.events?.raised ?? [])
        poolGrow(`event:${pathKey(r.type)}`, r.payload === "none" ? Ty.of("struct") : membersTy(r.payload.members));
    // event tables' rows: structures with the type member and the payload member, anywhere in the globals
    const rowsIn = (t: Ty, seen = new Set<Ty>()) => {
        if (seen.has(t)) return;
        seen.add(t);
        for (const r of catalogue.events?.rowTables ?? []) {
            const type = t.fields?.get(pathKey(r.typeMember));
            if (!type) continue;
            const payload = t.fields?.get(pathKey(r.payloadMember)) ?? Ty.of("struct");
            const types = allNames(type);
            for (const pname of types === null ? ["event:?"] : [...types].map((n) => `event:${n}`))
                poolGrow(pname, payload);
        }
        for (const f of t.fields?.values() ?? []) rowsIn(f, seen);
        if (t.elem) rowsIn(t.elem, seen);
        if (t.rest) rowsIn(t.rest, seen);
    };
    for (const g of globals.values()) rowsIn(g);
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
            console.log("inbox why:", WHY_SETS[inbox.why]!.map((r) => REASONS[r]).join(" | "));
            console.log(
                "inbox rest:",
                inbox.rest
                    ? `${inbox.rest.text()} why ${WHY_SETS[inbox.rest.why]!.map((r) => REASONS[r]).join(" | ")}`
                    : "none",
            );
            console.log(
                "inbox pools:",
                [...inboxPools].join(" "),
                "| fields with unknown names:",
                [...(inbox.fields ?? [])]
                    .filter(([, f]) => allNames(f) === null)
                    .map(
                        ([n, f]) =>
                            `${nameOf(n)} (${WHY_SETS[f.why]!.map((r) => REASONS[r])
                                .join(" / ")
                                .slice(0, 200)})`,
                    )
                    .slice(0, 15)
                    .join("; "),
            );
            for (const [k, sc] of scripts)
                if (nameOf(k) === process.env.TYPECHECK_DUMP)
                    for (const [f, t] of sc.params.fields ?? [])
                        console.log(
                            "field",
                            nameOf(f),
                            count(t, 1),
                            t.text().slice(0, 80),
                            t.deep === undefined ? "" : `deep ${t.deep?.size ?? "any"}`,
                        );
            console.log(
                [...nullStarts]
                    .sort((x, y) => y[1] - x[1])
                    .slice(0, 40)
                    .map(([k, v]) => `${v} ${k}`)
                    .join("\n"),
            );
            process.exit(0);
        }
        runPendingPoolStarts();
        const s = scripts.get(n)!;
        const began = process.env.TYPECHECK_SLOW ? performance.now() : 0;
        // (diagnostic: TYPECHECK_PROFILE_WALK=<script> writes a CPU profile of that script's first walk)
        const profiled =
            process.env.TYPECHECK_PROFILE_WALK === nameOf(n) &&
            rounds >= Number(process.env.TYPECHECK_PROFILE_AFTER ?? 0)
                ? new inspector.Session()
                : undefined;
        if (profiled) {
            profiled.connect();
            profiled.post("Profiler.enable");
            profiled.post("Profiler.start");
        }
        for (const body of scriptBodies.get(n) ?? []) walk(body.def.body, s);
        if (profiled)
            profiled.post("Profiler.stop", (_e, r) => {
                writeFileSync(`out/scripts/walk-${nameOf(n)}.cpuprofile`, JSON.stringify(r.profile));
                process.exit(0);
            });
        if (began && performance.now() - began > 500)
            console.log(
                `typecheck: slow walk ${nameOf(n)} ${Math.round(performance.now() - began)} ms (walk ${rounds})`,
            );
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
    const mechanismsPath = join(import.meta.dirname, "start_mechanisms.json");
    const mechanisms = existsSync(mechanismsPath)
        ? (JSON.parse(readFileSync(mechanismsPath, "utf8")) as StartCatalogue)
        : undefined;
    const result = typecheck(modules, models, cxx, { named, globals, where, triggers, entries, mechanisms });
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
        // (each script's parameters as its reads see them: "<script>|<member>" -> kinds - what the checks test against)
        readKinds: Object.fromEntries(
            [
                ...result.sites.reduce((m, x) => {
                    const k = `${x.script}|${x.name}`;
                    m.set(k, new Set([...(m.get(k) ?? []), ...x.ty.kinds]));
                    return m;
                }, new Map<string, Set<string>>()),
            ].map(([k, v]) => [k, [...v].join("|")]),
        ),
        // (each read not proven, with its reasons: what is left to resolve)
        unproven: result.sites
            .filter((x) => x.ty.why !== 0)
            .map((x) => ({
                module: x.module,
                script: x.script,
                name: x.name,
                why: WHY_SETS[x.ty.why]!.map((r) => REASONS[r]),
            })),
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
