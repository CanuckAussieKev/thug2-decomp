// NOT ORIGINAL - THUG2 .qb values written as tokens (src/game/script/qb.h's token stream), and one global array of a
// .qb file replaced in place - everything else (scripts, other globals, the name table) kept byte for byte. Used by
// tools/game/apply_layout.ts to write a level's node array back without NeverScript (ns.exe can't be counted on to
// round-trip every file: 454 / 470 do) - and, the same way, a level structure's entries set (Levels.qb) and scripts
// added or replaced (a level's scripts .qb).
//
// The layout the game's own files use (and NeverScript writes): a structure is 0x03 0x01, each entry followed by 0x01,
// 0x04; an array of structures 0x05 0x01, each followed by 0x01, 0x06; other arrays 0x05, the elements with 0x09
// between, 0x06; an entry `name = value` is 0x16 <name> 0x07 <value>, a flag / an included structure 0x16 <name>.

import { Float } from "../lib/py.ts";
import { pack } from "../lib/struct.ts";
import { AllArgs, Arg, Expr, Local, Name, type QbValue, Struct, tokens, Vector } from "./qb.ts";

function nameToken(value: number): Buffer {
    return Buffer.concat([Buffer.from([0x16]), pack("<I", value >>> 0)]);
}

function stringToken(token: number, text: string): Buffer {
    const bytes = Buffer.from(`${text}\0`, "latin1");
    return Buffer.concat([Buffer.from([token]), pack("<I", bytes.length), bytes]);
}

// A value's tokens.
export function encodeValue(value: QbValue): Buffer {
    if (value instanceof Name) return nameToken(value.value);
    if (value instanceof Float) return Buffer.concat([Buffer.from([0x1a]), pack("<f", value.value)]);
    if (typeof value === "number") {
        if (!Number.isInteger(value)) return Buffer.concat([Buffer.from([0x1a]), pack("<f", value)]);
        return Buffer.concat([Buffer.from([0x17]), pack("<i", value | 0)]);
    }
    if (typeof value === "boolean") return Buffer.concat([Buffer.from([0x17]), pack("<i", value ? 1 : 0)]);
    if (typeof value === "string") return stringToken(0x1b, value);
    if (value instanceof Local) return stringToken(0x1c, value.text);
    if (value instanceof Vector) {
        return value.kind === "vector"
            ? Buffer.concat([Buffer.from([0x1e]), pack("<3f", ...value.values)])
            : Buffer.concat([Buffer.from([0x1f]), pack("<2f", ...value.values)]);
    }
    if (value instanceof Arg) return Buffer.concat([Buffer.from([0x2d]), nameToken(value.value)]);
    if (value instanceof AllArgs) return Buffer.from([0x2c]);
    if (value instanceof Expr) {
        return Buffer.concat([
            Buffer.from([0x0e]),
            encodeValue(value.left),
            Buffer.from([value.op === "-" ? 0x0a : 0x0b]),
            encodeValue(value.right),
            Buffer.from([0x0f]),
        ]);
    }
    if (value instanceof Struct) {
        const parts: Buffer[] = [Buffer.from([0x03, 0x01])];
        for (const [key, v] of value.entries) {
            if (key !== null) parts.push(nameToken(key.value), Buffer.from([0x07]));
            parts.push(encodeValue(v), Buffer.from([0x01]));
        }
        parts.push(Buffer.from([0x04]));
        return Buffer.concat(parts);
    }
    if (Array.isArray(value)) {
        const parts: Buffer[] = [Buffer.from([0x05])];
        if (value.some((v) => v instanceof Struct)) {
            parts.push(Buffer.from([0x01]));
            for (const v of value) parts.push(encodeValue(v), Buffer.from([0x01]));
        } else {
            value.forEach((v, i) => {
                if (i) parts.push(Buffer.from([0x09]));
                parts.push(encodeValue(v));
            });
        }
        parts.push(Buffer.from([0x06]));
        return Buffer.concat(parts);
    }
    throw new TypeError(`no token form for ${String(value)}`);
}

// Where the top-level global `name = [ ... ]` is in `data`: [start of its value, end after its closing 0x06], or null.
export function globalArraySpan(data: Buffer, name: number): [number, number] | null {
    // (line tokens - 0x01, 0x02 - skipped: the game's files have `name = <newline> [`)
    const toks = [...tokens(data, true)].filter(([, t]) => t !== 0x01 && t !== 0x02);
    let depth = 0;
    let inScript = false;
    for (let i = 0; i + 2 < toks.length; i++) {
        const [, t, payload] = toks[i]!;
        if (t === 0x23) inScript = true;
        if (t === 0x24) inScript = false;
        if (inScript) continue;
        if (t === 0x03 || t === 0x05) depth++;
        if (t === 0x04 || t === 0x06) depth--;
        if (depth !== 0 || t !== 0x16 || payload.readUInt32LE(0) !== name) continue;
        if (toks[i + 1]![1] !== 0x07 || toks[i + 2]![1] !== 0x05) continue;
        let level = 0;
        for (let j = i + 2; j < toks.length; j++) {
            const tj = toks[j]![1];
            if (tj === 0x05 || tj === 0x03) level++;
            if (tj === 0x06 || tj === 0x04) level--;
            if (level === 0) return [toks[i + 2]![0], toks[j]![0] + 1];
        }
        return null;
    }
    return null;
}

// The byte spans of the structures directly in the array at `span` (a node array's nodes), in order.
export function elementSpans(data: Buffer, span: [number, number]): [number, number][] {
    const out: [number, number][] = [];
    let depth = 0;
    let start = 0;
    for (const [offset, t] of tokens(data.subarray(span[0], span[1]), true)) {
        if (t === 0x03 || t === 0x05) {
            depth++;
            if (depth === 2 && t === 0x03) start = offset;
        }
        if (t === 0x04 || t === 0x06) {
            if (depth === 2 && t === 0x04) out.push([span[0] + start, span[0] + offset + 1]);
            depth--;
        }
    }
    return out;
}

// An element of a rewritten node array: the original's element `keep` as its bytes are, or a new value.
export type Element = { keep: number } | { value: QbValue };

// `data` with the global array `name`'s value replaced: by `value` (encoded whole), or by `elements` (an array of
// structures: kept elements keep their bytes and the separators after them, so an unchanged array comes out byte for
// byte); `newNames` (checksum -> text) added to the file's name table (0x2B entries, before the closing 0x00) when it
// doesn't have them. Throws when there's no such array.
export function replaceGlobalArray(
    data: Buffer,
    name: number,
    value: QbValue[] | Element[],
    newNames: ReadonlyMap<number, string> = new Map(),
    asElements = false,
): Buffer {
    const span = globalArraySpan(data, name);
    if (!span) throw new Error(`no global array ${name.toString(16)} at the top level`);
    let body: Buffer;
    if (asElements) {
        const spans = elementSpans(data, span);
        const parts: Buffer[] = [];
        const head = spans.length ? data.subarray(span[0], spans[0]![0]) : Buffer.from([0x05, 0x01]);
        const tail = spans.length ? data.subarray(spans[spans.length - 1]![1], span[1]) : Buffer.from([0x06]);
        const separator = (k: number): Buffer =>
            k + 1 < spans.length ? data.subarray(spans[k]![1], spans[k + 1]![0]) : Buffer.from([0x01]);
        parts.push(head);
        const elements = value as Element[];
        elements.forEach((e, i) => {
            const last = i === elements.length - 1;
            if ("keep" in e) {
                const s = spans[e.keep];
                if (!s) throw new RangeError(`no element ${e.keep}`);
                parts.push(data.subarray(s[0], s[1]));
                if (!last) parts.push(e.keep + 1 < spans.length ? separator(e.keep) : Buffer.from([0x01]));
            } else {
                parts.push(encodeValue(e.value));
                if (!last) parts.push(Buffer.from([0x01]));
            }
        });
        // (the original's last element is followed by the tail's own line token; a new last one gets the same)
        parts.push(tail);
        body = Buffer.concat(parts);
    } else {
        body = encodeValue(value as QbValue[]);
    }
    const out = Buffer.concat([data.subarray(0, span[0]), body, data.subarray(span[1])]);
    return addNames(out, newNames);
}

// `data` with `newNames` (checksum -> text) added to its name table (0x2B entries, before the closing 0x00) when it
// doesn't have them.
export function addNames(data: Buffer, newNames: ReadonlyMap<number, string>): Buffer {
    const have = new Set<number>();
    for (const [, t, payload] of tokens(data, true)) if (t === 0x2b) have.add(payload.readUInt32LE(0));
    const add: Buffer[] = [];
    for (const [checksum, text] of newNames) {
        if (have.has(checksum)) continue;
        have.add(checksum);
        add.push(Buffer.from([0x2b]), pack("<I", checksum >>> 0), Buffer.from(`${text}\0`, "latin1"));
    }
    if (!add.length) return data;
    const end = data.length > 0 && data[data.length - 1] === 0x00 ? data.length - 1 : data.length;
    return Buffer.concat([data.subarray(0, end), ...add, data.subarray(end)]);
}

// --- (tools/game/apply_layout.ts: a level structure's entries, scripts) ----------------------------------------------

// Where the top-level global `name = { ... }` is in `data`: [start of its 0x03, end after its closing 0x04], or null.
export function globalStructSpan(data: Buffer, name: number): [number, number] | null {
    const toks = [...tokens(data, true)].filter(([, t]) => t !== 0x01 && t !== 0x02);
    let depth = 0;
    let inScript = false;
    for (let i = 0; i + 2 < toks.length; i++) {
        const [, t, payload] = toks[i]!;
        if (t === 0x23) inScript = true;
        if (t === 0x24) inScript = false;
        if (inScript) continue;
        if (t === 0x03 || t === 0x05) depth++;
        if (t === 0x04 || t === 0x06) depth--;
        if (depth !== 0 || t !== 0x16 || payload.readUInt32LE(0) !== name) continue;
        if (toks[i + 1]![1] !== 0x07 || toks[i + 2]![1] !== 0x03) continue;
        let level = 0;
        for (let j = i + 2; j < toks.length; j++) {
            const tj = toks[j]![1];
            if (tj === 0x05 || tj === 0x03) level++;
            if (tj === 0x06 || tj === 0x04) level--;
            if (level === 0) return [toks[i + 2]![0], toks[j]![0] + 1];
        }
        return null;
    }
    return null;
}

// `data` with entries of the global structure `name` set: each `key = value` replaces the value of the structure's
// last own `key =` entry (the one the game's Search finds - a structure may repeat a key) in place, or is added at its
// end; every other byte kept. `newNames` go into the name table. Throws when there's no such structure.
export function setStructEntries(
    data: Buffer,
    name: number,
    entries: readonly [number, QbValue][],
    newNames: ReadonlyMap<number, string> = new Map(),
): Buffer {
    let out = data;
    for (const [key, value] of entries) {
        const span = globalStructSpan(out, name);
        if (!span) throw new Error(`no global structure ${name.toString(16)} at the top level`);
        // (the structure's own entries: key 0x07 at its depth 1; a value runs to the next line token / the end there)
        const toks = [...tokens(out.subarray(span[0], span[1]), true)];
        let depth = 0;
        let at: [number, number] | null = null;
        for (let i = 0; i < toks.length; i++) {
            const [, t, payload] = toks[i]!;
            if (t === 0x03 || t === 0x05) depth++;
            if (t === 0x04 || t === 0x06) depth--;
            if (depth !== 1 || t !== 0x16 || payload.readUInt32LE(0) !== key || toks[i + 1]?.[1] !== 0x07) continue;
            let d = 0;
            let j = i + 2;
            for (; j < toks.length; j++) {
                const tj = toks[j]![1];
                if (d === 0 && (tj === 0x01 || tj === 0x02 || tj === 0x04)) break;
                if (tj === 0x03 || tj === 0x05) d++;
                if (tj === 0x04 || tj === 0x06) d--;
            }
            at = [span[0] + toks[i + 2]![0], span[0] + (toks[j]?.[0] ?? span[1] - span[0])];
        }
        if (at) {
            out = Buffer.concat([out.subarray(0, at[0]), encodeValue(value), out.subarray(at[1])]);
        } else {
            // (before the closing 0x04: the structure's entries each end with a line token)
            const close = span[1] - 1;
            const entry = Buffer.concat([
                Buffer.from([0x16]),
                pack("<I", key >>> 0),
                Buffer.from([0x07]),
                encodeValue(value),
                Buffer.from([0x01]),
            ]);
            out = Buffer.concat([out.subarray(0, close), entry, out.subarray(close)]);
        }
    }
    return addNames(out, newNames);
}

// The byte spans of the top-level scripts in `data` (script name -> [start of its 0x23, end after its 0x24]).
export function scriptSpans(data: Buffer): Map<number, [number, number]> {
    const out = new Map<number, [number, number]>();
    const toks = [...tokens(data, true)];
    for (let i = 0; i < toks.length; i++) {
        if (toks[i]![1] !== 0x23 || toks[i + 1]?.[1] !== 0x16) continue;
        const name = toks[i + 1]![2].readUInt32LE(0);
        let j = i + 2;
        while (j < toks.length && toks[j]![1] !== 0x24) j++;
        if (j >= toks.length) break;
        if (!out.has(name)) out.set(name, [toks[i]![0], toks[j]![0] + 1]);
        i = j;
    }
    return out;
}

// `data` with scripts (name -> body: its statements, each ending with a line token) defined: a script of that name
// already there gets the new body in place (`script <name> <body> endscript`: 23 16 <name> 01 <body> 24); new ones are
// added before the file's name table (each 23 16 <name> 01 <body> 24 01), as a compiled file has them. Every other
// byte kept; `newNames` go into the name table.
export function putScripts(
    data: Buffer,
    scripts: readonly [number, Buffer][],
    newNames: ReadonlyMap<number, string> = new Map(),
): Buffer {
    const script = (name: number, body: Buffer): Buffer =>
        Buffer.concat([
            Buffer.from([0x23, 0x16]),
            pack("<I", name >>> 0),
            Buffer.from([0x01]),
            body,
            Buffer.from([0x24]),
        ]);
    let out = data;
    const added: Buffer[] = [];
    for (const [name, body] of scripts) {
        const span = scriptSpans(out).get(name);
        if (span) out = Buffer.concat([out.subarray(0, span[0]), script(name, body), out.subarray(span[1])]);
        else added.push(script(name, body), Buffer.from([0x01]));
    }
    if (added.length) {
        // (the name table: the 0x2B entries after the last other token, before the closing 0x00)
        const toks = [...tokens(out, true)];
        let at = toks.length;
        while (at > 0 && (toks[at - 1]![1] === 0x2b || toks[at - 1]![1] === 0x00)) at--;
        const offset = at < toks.length ? toks[at]![0] : out.length;
        const previous = at > 0 ? toks[at - 1]![1] : 0x01;
        const lead = previous === 0x01 || previous === 0x02 ? [] : [Buffer.from([0x01])];
        out = Buffer.concat([out.subarray(0, offset), ...lead, ...added, out.subarray(offset)]);
    }
    return addNames(out, newNames);
}
