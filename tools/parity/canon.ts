// NOT ORIGINAL - the TypeScript half of the parity drivers' canonical dumps (tools/parity/canon.py: the format).
// TypeScript's structures use camelCase keys; they are written as Python's snake_case ones here.

import { createHash } from "node:crypto";
import { AllArgs, Arg, Expr, Local, Name, type QbValue, Struct, Vector } from "../level_port/qb.ts";
import { Float, hex8, pyFloatRepr, pyJsonDumps } from "../lib/py.ts";

export function blob(data: Uint8Array): string {
    return `b:${data.length}:${createHash("sha256").update(data).digest("hex").slice(0, 24)}`;
}

export function qcanon(v: QbValue | null | undefined): unknown {
    if (v === null || v === undefined) return null;
    if (v instanceof Name) return { N: hex8(v.value) };
    if (v instanceof Local) return { L: v.text };
    if (v instanceof Arg) return { A: hex8(v.value) };
    if (v instanceof AllArgs) return { AA: 1 };
    if (v instanceof Expr) return { E: [qcanon(v.left), v.op, qcanon(v.right)] };
    if (v instanceof Struct) return { S: v.entries.map(([k, x]) => [k === null ? null : hex8(k.value), qcanon(x)]) };
    if (typeof v === "boolean") return { B: v };
    if (typeof v === "number") return Number.isInteger(v) ? { i: v } : { f: pyFloatRepr(v) };
    if (v instanceof Float) return { f: pyFloatRepr(v.value) };
    if (typeof v === "string") return { s: v };
    if (v instanceof Vector) return { V: v.kind, v: v.values.map(pyFloatRepr) };
    if (Array.isArray(v)) return v.map(qcanon);
    return `?${(v as object).constructor.name}`;
}

const SKIPPED = new Set(["bspOff", "nodeSize", "indexCount"]); // (Python's "_bsp_off", "_node_size", "_index_count")

export const snake = (key: string): string => key.replace(/[A-Z]/g, (c) => `_${c.toLowerCase()}`);

export function canon(x: unknown): unknown {
    if (x === null || x === undefined || typeof x === "boolean") return x ?? null;
    if (
        x instanceof Name ||
        x instanceof Local ||
        x instanceof Arg ||
        x instanceof AllArgs ||
        x instanceof Expr ||
        x instanceof Struct
    ) {
        return qcanon(x);
    }
    if (typeof x === "number") return `n:${pyFloatRepr(x)}`;
    if (x instanceof Float) return `n:${pyFloatRepr(x.value)}`;
    if (typeof x === "string") return `s:${x}`;
    if (x instanceof Uint8Array) return blob(x);
    if (Array.isArray(x)) return x.map(canon);
    if (x instanceof Set) {
        return [...x].map(canon).sort((a, b) => {
            const p = dump(a);
            const q = dump(b);
            return p < q ? -1 : p > q ? 1 : 0;
        });
    }
    if (x instanceof Map) return Object.fromEntries([...x].map(([k, v]) => [String(k), canon(v)]));
    const o = x as Record<string, unknown>;
    if (o.kind === "leaf") return ["s:leaf", canon(o.faces), canon(o.low)];
    if (o.kind === "split") return ["s:split", canon(o.axis), canon(o.point), canon(o.left), canon(o.right)];
    const out: Record<string, unknown> = {};
    for (const [k, v] of Object.entries(o)) if (!SKIPPED.has(k) && v !== undefined) out[snake(k)] = canon(v);
    return out;
}

function sortKeys(x: unknown): unknown {
    if (Array.isArray(x)) return x.map(sortKeys);
    if (x !== null && typeof x === "object") {
        const raw =
            x instanceof Map
                ? [...x].map(([k, v]) => [String(k), v] as const)
                : Object.entries(x as Record<string, unknown>);
        const entries = [...raw].sort(([a], [b]) => (a < b ? -1 : a > b ? 1 : 0));
        return new Map(entries.map(([k, v]) => [k, sortKeys(v)]));
    }
    return x;
}

// json.dumps(x, sort_keys=True)
export function dump(x: unknown): string {
    return pyJsonDumps(sortKeys(x));
}

export function digest(x: unknown): string {
    return createHash("sha256").update(dump(x), "utf-8").digest("hex").slice(0, 24);
}
