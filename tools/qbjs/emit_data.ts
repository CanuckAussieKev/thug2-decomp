// NOT ORIGINAL - a QB module's globals as its data file (qb_data.ts, docs/qb-ts.md "Data"): each global's value built
// as the game builds it from the TypeScript (scripting/qb_ts.js value() over values.js) - a structure's members merged
// as they are added (a later member of the same name and kind replaces the earlier and moves to the end, an unnamed
// structure's members spread in, an `angles` vector of all-but-zero literal coordinates left out), an array's integers
// among floats made floats - so the file holds the values themselves, read without running anything. A structure's
// script (`script ... endscript`, the CAS tables' part scripts) stays code: the data names it ({"$script": key}) and
// the module's .ts defines it (emit_ts.ts data mode: scriptValue(key, code(...))).
import type { Expr, Field, ModuleIR } from "./ir.ts";
import { nameChecksum as checksumOf, type QbDataValue, writeQbData } from "./qb_data.ts";

const ANGLES = 0x9d2d0915; // (qb_ts.js / values.js fields: near-zero literal angles dropped)
const NEAR_ZERO = 0x3c23d70a; // 0.01f
const floatOf = (bits: number) => new Float32Array(new Uint32Array([bits]).buffer)[0]!;
const bitsOf = (x: number) => new Uint32Array(new Float32Array([x]).buffer)[0]!;

// A global's scripts as values, in the order both the data and the .ts number them (depth first, in the source's
// order): the key `<global>#<n>`.
export function inlineScripts(value: Expr): Extract<Expr, { kind: "inlineScript" }>[] {
    const out: Extract<Expr, { kind: "inlineScript" }>[] = [];
    const walk = (x: Expr) => {
        if (x.kind === "inlineScript") out.push(x);
        else if (x.kind === "structure") {
            for (const f of x.fields) if ("value" in f) walk(f.value);
        } else if (x.kind === "array") x.items.forEach(walk);
    };
    walk(value);
    return out;
}

// The members as values.js merges them: keyed by name and kind (numbers one kind, an unnamed name by its checksum).
const mergeKey = (n: number, v: QbDataValue) =>
    `${n}:${v.type === 1 || v.type === 2 ? "numeric" : v.type === 13 && n === 0 ? `flag:${v.bits}` : v.type}`;
function merged(members: [number, QbDataValue][]): [number, QbDataValue][] {
    const index = new Map<string, [number, QbDataValue]>();
    for (const m of members) {
        const key = mergeKey(m[0], m[1]);
        index.delete(key);
        index.set(key, m);
    }
    return [...index.values()];
}

// A global's value; `scriptKey(n)` names its n-th script value.
export function globalValue(value: Expr, scriptKey: (n: number) => string, where = "a global"): QbDataValue {
    let scripts = 0;
    const fail = (what: string): never => {
        throw Error(`${where}: ${what} in a global's value`);
    };
    const build = (x: Expr): QbDataValue => {
        switch (x.kind) {
            case "literal": {
                const v = x.value;
                if (v.type === 1 || v.type === 2 || v.type === 13) return { type: v.type, bits: v.bits >>> 0 };
                if (v.type === 3 || v.type === 4)
                    return v.token !== undefined
                        ? { type: v.type, bytes: [...v.bytes], token: v.token }
                        : { type: v.type, bytes: [...v.bytes] };
                const { type, coordinates } = v as { type: 5 | 6; coordinates: number[] };
                return { type, coordinates: coordinates.map((c) => c >>> 0) };
            }
            case "structure":
                return { type: 10, members: merged(fields(x.fields)) };
            case "array": {
                const items = x.items.map(build);
                const types = items.map((i) => i.type);
                const elementType = types.includes(2) && types.every((t) => t === 1 || t === 2) ? 2 : (types[0] ?? 0);
                const elements = items.map((i) =>
                    elementType === 2 && i.type === 1 ? { type: 2 as const, bits: bitsOf(i.bits | 0) } : i,
                );
                if (elements.some((e) => e.type !== elementType)) fail("an array of mixed types");
                return { type: 12, elementType, elements };
            }
            case "inlineScript":
                return { type: 7, key: scriptKey(++scripts) };
            default:
                return fail(`a ${x.kind}`);
        }
    };
    const fields = (xs: Field[]): [number, QbDataValue][] => {
        const out: [number, QbDataValue][] = [];
        for (const f of xs) {
            if ("spread" in f) continue; // (<...>: a global is parsed without parameters - nothing)
            if ("kind" in f) fail("a Random among the fields");
            const field = f as Extract<Field, { name: number }>;
            // (<x>: no parameters - nothing)
            if (field.value.kind === "parameter") continue;
            const v = build(field.value);
            if (field.name === 0) {
                if (v.type === 10) out.push(...v.members);
                else out.push([0, v]);
                continue;
            }
            if (
                field.name === ANGLES &&
                field.value.kind === "literal" &&
                v.type === 6 &&
                v.coordinates.every((bits) => Math.abs(floatOf(bits)) < floatOf(NEAR_ZERO))
            )
                continue;
            out.push([field.name, v]);
        }
        return merged(out);
    };
    return build(value);
}

// The module's globals in order.
export function moduleGlobals(module: ModuleIR): [number, QbDataValue][] {
    const out: [number, QbDataValue][] = [];
    for (const d of module.definitions) {
        if (d.kind !== "global") continue;
        const key = globalKey(module, d.name);
        out.push([d.name, globalValue(d.value, (n) => `${key}#${n}`, `${module.source}: ${key}`)]);
    }
    return out;
}

// A global's name as the data file writes it (its text where the module's names spell it, else _0x...).
export function globalKey(module: ModuleIR, n: number): string {
    const text = module.names?.[n];
    return text !== undefined && /^[\x20-\x7e]+$/.test(text) && checksumOf(text) === n
        ? text
        : `_0x${(n >>> 0).toString(16).padStart(8, "0")}`;
}

// The module's data file (null: it defines no globals). `from`: the hash of the .qb it was made from.
export function emitData(module: ModuleIR, from: string): string | null {
    const globals = moduleGlobals(module);
    if (!globals.length) return null;
    return writeQbData({ source: module.source, from, globals }, (n) => module.names?.[n]);
}
