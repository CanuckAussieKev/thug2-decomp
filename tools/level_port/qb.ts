// THPS4 / THUG1 .qb files: read the global data (`name = value` at the top level: node arrays, level structures) and
// the checksum names, and write values as NeverScript text (tools/neverscript compiles that to THUG2 .qb).
//
// The token stream is THUG2's (src/game/script/qb.h) in both. THUG1's is THUG2's exactly; THPS4's differs in the
// random tokens: its 0x2F / 0x37 / 0x40 / 0x41 are {u32 count, count x u32 jump offset} - THUG1 / THUG2 have a u16
// weight per entry before the offsets - which is why NeverScript's THUG2 decompiler stops in THPS4 files (QbFile's
// `weights`). Scripts (0x23 ... 0x24) are split into statements, not decoded further.
//
// Values: Name, Local, Arg, AllArgs, Expr, Struct, Vector ("vector" / "pair"), a number (an int), a Float (a float:
// JavaScript can't tell 1.0 from 1, NeverScript can), a string, a boolean, an array.

import * as zlib from "node:zlib";
import {
    decodeLatin1,
    encodeAscii,
    Float,
    formatFixed,
    hex8,
    isAlnumChar,
    isDigitChar,
    pyFloatRepr,
} from "../lib/py.ts";
import { f32, unpackNumbers } from "../lib/struct.ts";
import { FormatError } from "./binio.ts";

// The script checksum of a name: CRC-32 of its lower case (ASCII only - raises otherwise, as Python's encode) without
// the final inversion. Always unsigned.
export function checksum(name: string): number {
    return (zlib.crc32(encodeAscii(name.toLowerCase())) ^ 0xffffffff) >>> 0;
}

// A checksum value (a name in the script).
export class Name {
    readonly value: number;
    constructor(value: number) {
        this.value = value;
    }

    equals(other: unknown): boolean {
        return other instanceof Name && other.value === this.value;
    }

    toString(): string {
        return `Name(${hex8(this.value)})`;
    }
}

// A local string ('text').
export class Local {
    readonly text: string;
    constructor(text: string) {
        this.text = text;
    }
}

// A parameter reference (<name>).
export class Arg {
    readonly value: number;
    constructor(value: number) {
        this.value = value;
    }
}

// <...>: all of the script's parameters (THPS3 has one in a node array: Cruise Ship's).
export class AllArgs {
    readonly allArgs = true; // (a member: an empty class would match any object in TypeScript's structural types)
}

export class Expr {
    readonly left: QbValue;
    readonly op: string;
    readonly right: QbValue;
    constructor(left: QbValue, op: string, right: QbValue) {
        this.left = left;
        this.op = op;
        this.right = right;
    }
}

// A vector (3 floats) or a pair (2): Python's ("vector", (x, y, z)) / ("pair", (x, y)).
export class Vector {
    readonly kind: "vector" | "pair";
    readonly values: readonly number[];
    constructor(kind: "vector" | "pair", values: readonly number[]) {
        this.kind = kind;
        this.values = values;
    }
}

export type QbValue =
    | Name
    | Local
    | Arg
    | AllArgs
    | Expr
    | Struct
    | Vector
    | Float
    | number
    | boolean
    | string
    | QbValue[];

// A structure entry: key Name, or null for a flag / unnamed value.
export type Entry = [Name | null, QbValue];

// A structure: entries (key Name or null for a flag / unnamed value, value).
export class Struct {
    entries: Entry[];
    constructor(entries?: Entry[]) {
        this.entries = entries?.length ? entries : [];
    }

    get(key: string | number): QbValue | undefined {
        const k = typeof key === "string" ? checksum(key) : key;
        for (const [name, value] of this.entries) {
            if (name !== null && name.value === k) return value;
        }
        return undefined;
    }

    hasFlag(flag: string): boolean {
        const k = checksum(flag);
        return this.entries.some(([name, value]) => name === null && value instanceof Name && value.value === k);
    }

    remove(key: string): void {
        const k = checksum(key);
        this.entries = this.entries.filter(([n]) => !(n !== null && n.value === k));
    }

    removeFlag(flag: string): void {
        const k = checksum(flag);
        this.entries = this.entries.filter(([n, v]) => !(n === null && v instanceof Name && v.value === k));
    }

    set(key: string, value: QbValue): void {
        const k = checksum(key);
        for (let i = 0; i < this.entries.length; i++) {
            const name = this.entries[i]![0];
            if (name !== null && name.value === k) {
                this.entries[i] = [name, value];
                return;
            }
        }
        this.entries.push([new Name(k), value]);
    }
}

// Token sizes after the token byte, for skipping (scripts).
export const FIXED: ReadonlyMap<number, number> = new Map([
    [0x02, 4],
    [0x16, 4],
    [0x17, 4],
    [0x18, 4],
    [0x19, 4],
    [0x1a, 4],
    [0x1e, 12],
    [0x1f, 8],
    [0x2e, 4],
    [0x43, 4],
    [0x44, 4],
    [0x45, 4],
    [0x46, 4],
    [0x47, 2],
    [0x48, 2],
    [0x49, 2],
]);

// A token: [offset, token, payload].
export type Token = [number, number, Buffer];

function asBuffer(data: Uint8Array): Buffer {
    return Buffer.isBuffer(data) ? data : Buffer.from(data.buffer, data.byteOffset, data.byteLength);
}

const u32At = (data: Uint8Array, at: number): number => unpackNumbers("<I", data, at)[0]!;

// [offset, token, payload] for every token; `weights`: the random tokens have the u16 weights (THUG1 / THUG2).
export function* tokens(input: Uint8Array, weights = false): Generator<Token> {
    const data = asBuffer(input);
    let i = 0;
    const n = data.length;
    while (i < n) {
        const t = data[i]!;
        const start = i;
        i += 1;
        let payload: Buffer;
        const fixed = FIXED.get(t);
        if (fixed !== undefined) {
            payload = data.subarray(i, i + fixed);
            i += fixed;
        } else if (t === 0x1b || t === 0x1c) {
            const length = u32At(data, i);
            payload = data.subarray(i + 4, i + 4 + length);
            i += 4 + length;
        } else if (t === 0x2b) {
            const end = data.indexOf(0, i + 4);
            if (end < 0) throw new RangeError("subsection not found"); // (Python's bytes.index)
            payload = data.subarray(i, end + 1);
            i = end + 1;
        } else if (t === 0x2f || t === 0x37 || t === 0x40 || t === 0x41) {
            const count = u32At(data, i);
            const size = 4 + (weights ? 6 : 4) * count;
            payload = data.subarray(i, i + size);
            i += size;
        } else {
            payload = data.subarray(0, 0);
        }
        yield [start, t, payload];
        if (t === 0x00) break;
    }
}

// A statement: its (token, payload) pairs.
export type Statement = [number, Buffer][];

function at<T>(list: readonly T[], i: number): T {
    // (Python's list[i]: IndexError past the end)
    if (i < 0 || i >= list.length) throw new RangeError("list index out of range");
    return list[i]!;
}

export class QbFile {
    readonly name: string;
    readonly names = new Map<number, string>(); // checksum -> text, from the file's 0x2B entries
    readonly globals: [Name, QbValue][] = []; // in file order
    // Scripts: name -> statements, each the list of its (token, payload) - not decoded further.
    readonly scripts = new Map<number, Statement[]>();
    private readonly toks: Token[];

    constructor(data: Uint8Array, name = "qb", weights = false) {
        this.name = name;
        const toks = [...tokens(data, weights)];
        for (const [, t, payload] of toks) {
            if (t === 0x2b) {
                const value = u32At(payload, 0);
                this.names.set(value, decodeLatin1(payload.subarray(4, payload.length - 1)));
            }
        }
        let i = 0;
        while (i < toks.length) {
            if (toks[i]![1] === 0x23 && i + 1 < toks.length && toks[i + 1]![1] === 0x16) {
                const scriptName = u32At(toks[i + 1]![2], 0);
                const statements: Statement[] = [];
                let line: Statement = [];
                i += 2;
                while (i < toks.length && toks[i]![1] !== 0x24) {
                    const [, t, p] = toks[i]!;
                    if (t === 0x01 || t === 0x02) {
                        if (line.length) statements.push(line);
                        line = [];
                    } else {
                        line.push([t, p]);
                    }
                    i++;
                }
                if (line.length) statements.push(line);
                this.scripts.set(scriptName, statements);
            }
            i++;
        }
        this.toks = toks.filter((x) => x[1] !== 0x01 && x[1] !== 0x02 && x[1] !== 0x2b);
        this.parse();
    }

    // The names each statement of `script` starts with (the commands / scripts it calls), and the whole statement.
    calls(script: number): [number, Statement][] {
        const out: [number, Statement][] = [];
        for (const line of this.scripts.get(script) ?? []) {
            if (line.length && line[0]![0] === 0x16) {
                if (line[0]![1].length !== 4) throw new RangeError("unpack requires a buffer of 4 bytes");
                out.push([u32At(line[0]![1], 0), line]);
            }
        }
        return out;
    }

    private parse(): void {
        const toks = this.toks;
        let i = 0;
        while (i < toks.length) {
            const [, t, payload] = toks[i]!;
            if (t === 0x23) {
                // script ... endscript
                while (at(toks, i)[1] !== 0x24) i++;
                i++;
            } else if (t === 0x16 && i + 1 < toks.length && toks[i + 1]![1] === 0x07) {
                const key = new Name(u32At(payload, 0));
                const [value, next] = this.value(i + 2);
                i = next;
                this.globals.push([key, value]);
            } else if (t === 0x00) {
                break;
            } else {
                throw new FormatError(
                    `${this.name}: token 0x${t.toString(16).toUpperCase().padStart(2, "0")} at 0x${toks[i]![0].toString(16).toUpperCase()} at the top level`,
                );
            }
        }
    }

    private value(i: number): [QbValue, number] {
        let [value, next] = this.single(i);
        i = next;
        // a + b / a - b (seen in a park editor shell's node array): kept as an expression
        while (
            (at(this.toks, i)[1] === 0x0a || at(this.toks, i)[1] === 0x0b) &&
            ![0x04, 0x06].includes(at(this.toks, i + 1)[1])
        ) {
            const op = this.toks[i]![1] === 0x0a ? "-" : "+";
            let right: QbValue;
            [right, next] = this.single(i + 1);
            i = next;
            value = new Expr(value, op, right);
        }
        return [value, i];
    }

    private single(i: number): [QbValue, number] {
        const toks = this.toks;
        const [off, t, payload] = at(toks, i);
        if (t === 0x16) return [new Name(unpackNumbers("<I", payload)[0]!), i + 1];
        if (t === 0x17 || t === 0x18) return [unpackNumbers("<i", payload)[0]!, i + 1];
        if (t === 0x1a) return [new Float(unpackNumbers("<f", payload)[0]!), i + 1];
        if (t === 0x1e) return [new Vector("vector", unpackNumbers("<3f", payload)), i + 1];
        if (t === 0x1f) return [new Vector("pair", unpackNumbers("<2f", payload)), i + 1];
        if (t === 0x1b) return [decodeLatin1(rstripNul(payload)), i + 1];
        if (t === 0x1c) return [new Local(decodeLatin1(rstripNul(payload))), i + 1];
        if (t === 0x0a && [0x17, 0x18, 0x1a].includes(at(toks, i + 1)[1])) {
            // a negative number
            const [value, next] = this.single(i + 1);
            return [value instanceof Float ? new Float(-value.value) : 0 - (value as number), next];
        }
        if (t === 0x2c) return [new AllArgs(), i + 1]; // <...>
        if (t === 0x2d && at(toks, i + 1)[1] === 0x16) {
            // <name>: a parameter
            return [new Arg(unpackNumbers("<I", toks[i + 1]![2])[0]!), i + 2];
        }
        if (t === 0x03) {
            const entries: Entry[] = [];
            i += 1;
            while (at(toks, i)[1] !== 0x04) {
                if (toks[i]![1] === 0x09) {
                    i += 1;
                    continue;
                }
                if (toks[i]![1] === 0x16 && at(toks, i + 1)[1] === 0x07) {
                    const key = new Name(unpackNumbers("<I", toks[i]![2])[0]!);
                    const [value, next] = this.value(i + 2);
                    i = next;
                    entries.push([key, value]);
                } else {
                    const [value, next] = this.value(i);
                    i = next;
                    entries.push([null, value]);
                }
            }
            return [new Struct(entries), i + 1];
        }
        if (t === 0x05) {
            const items: QbValue[] = [];
            i += 1;
            while (at(toks, i)[1] !== 0x06) {
                if (toks[i]![1] === 0x09) {
                    i += 1;
                    continue;
                }
                const [value, next] = this.value(i);
                i = next;
                items.push(value);
            }
            return [items, i + 1];
        }
        if (t === 0x0e) {
            // a parenthesised expression: kept only when it is a single value
            const [value, j] = this.value(i + 1);
            if (at(toks, j)[1] === 0x0f) return [value, j + 1];
        }
        throw new FormatError(
            `${this.name}: value token 0x${t.toString(16).toUpperCase().padStart(2, "0")} at 0x${off.toString(16).toUpperCase()}`,
        );
    }

    get(name: string): QbValue | undefined {
        const k = checksum(name);
        for (const [key, value] of this.globals) {
            if (key.value === k) return value;
        }
        return undefined;
    }

    nameOf(value: number): string | undefined {
        return this.names.get(value);
    }
}

// bytes.rstrip(b"\0")
export function rstripNul(data: Buffer): Buffer {
    let end = data.length;
    while (end > 0 && data[end - 1] === 0) end--;
    return data.subarray(0, end);
}

// --- NeverScript text -------------------------------------------------------------------------------------------

// A float as NeverScript reads it: the float32's shortest repr ("0.1", not 0.10000000149011612), fixed-point when
// repr would use an exponent.
export function fmtFloat(f: number): string {
    let text = pyFloatRepr(f32(f));
    if (text.includes("e") || text.includes("inf") || text.includes("nan")) text = formatFixed(f, 9);
    if (!text.includes(".")) text += ".0";
    return text;
}

export function toNs(value: QbValue, names: ReadonlyMap<number, string>, indent = 0): string {
    const pad = "    ".repeat(indent);
    if (value instanceof Name) return nameText(value.value, names);
    if (value instanceof AllArgs) return "<...>";
    if (value instanceof Arg) return `<${nameText(value.value, names)}>`;
    if (value instanceof Expr) return `(${toNs(value.left, names)} ${value.op} ${toNs(value.right, names)})`;
    if (typeof value === "boolean") return value ? "1" : "0";
    if (typeof value === "number") return Number.isInteger(value) ? String(value) : fmtFloat(value);
    if (value instanceof Float) return fmtFloat(value.value);
    if (typeof value === "string") return `"${value.replaceAll("\\", "\\\\").replaceAll('"', '\\"')}"`;
    if (value instanceof Local) return `'${value.text.replaceAll("\\", "\\\\").replaceAll("'", "\\'")}'`;
    if (value instanceof Vector) return `(${value.values.map(fmtFloat).join(", ")})`;
    if (Array.isArray(value)) {
        if (!value.length) return "[]";
        if (value.every((v) => !(v instanceof Struct || Array.isArray(v)))) {
            return `[${value.map((v) => toNs(v, names)).join(", ")}]`;
        }
        const inner = value.map((v) => `${pad}    ${toNs(v, names, indent + 1)}`).join("\n");
        return `[\n${inner}\n${pad}]`;
    }
    if (value instanceof Struct) {
        const lines: string[] = [];
        for (const [key, v] of value.entries) {
            const text = toNs(v, names, indent + 1);
            lines.push(`${pad}    ${key === null ? text : `${nameText(key.value, names)}=${text}`}`);
        }
        return `{\n${lines.join("\n")}\n${pad}}`;
    }
    throw new TypeError(`no NeverScript form for ${String(value)}`);
}

export function nameText(value: number, names: ReadonlyMap<number, string>): string {
    const text = names.get(value);
    if (
        text &&
        [...text].every((c) => isAlnumChar(c) || c === "_") &&
        !isDigitChar([...text][0]!) &&
        checksum(text) === value &&
        !RESERVED.has(text.toLowerCase())
    ) {
        return text;
    }
    // (NeverScript's #xxxxxxxx is the checksum's bytes in file order: little-endian - #20FFA11C is 0x1CA1FF20,
    // `default`. Written as the number's hex digits it compiled to the byte-swapped checksum.)
    const bytes = Buffer.alloc(4);
    bytes.writeUInt32LE(value >>> 0);
    return `#${bytes.toString("hex").toUpperCase()}`;
}

// Words NeverScript reads as keywords, not names.
export const RESERVED: ReadonlySet<string> = new Set([
    "script",
    "endscript",
    "if",
    "else",
    "elseif",
    "endif",
    "while",
    "loop",
    "break",
    "return",
    "switch",
    "case",
    "default",
    "begin",
    "repeat",
    "random",
    "not",
    "and",
    "or",
    "true",
    "false",
    "endswitch",
    "useheap",
    "random2",
    "randomrange",
    "randomrange2",
    "randompermute",
    "randomnorepeat",
]);
