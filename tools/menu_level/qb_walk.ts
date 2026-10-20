// NOT ORIGINAL - reading compiled .qb scripts the way the game walks them, for CREATE 100% CAS (career_complete.ts):
//   - gapList(): a level's gap checklist as the game builds it when the level loads (CGapChecklist::Build,
//     src/game/skate/gap_checklist.cpp, through SkateScript::ForEachNodeScriptCall / ForEachScriptCall,
//     src/game/script/nodes.cpp): every node's TriggerScript followed into the scripts it calls, every EndGap call
//     with a score= and a non-empty text= is a gap (name and score; the same name with the same score once);
//   - classicGoalCalls(): the AddGoal_* calls of a level's <prefix>_Goals script inside its
//     `if ((GameModeEquals is_classic) or (InNetGame))` block (NeverScript can't decompile every level's scripts -
//     CA_scripts.qb stops it - so this reads the tokens).
// The token format is src/game/script/qb.h's; SkipToken / SkipToEndOfStatement / the argument parsing follow
// src/game/script/qb.cpp and parse.cpp (ParseArguments, ParseStructure).

import * as zlib from "node:zlib";
import { encodeLatin1, Float } from "../lib/py.ts";

export function checksum(name: string): number {
    return (zlib.crc32(encodeLatin1(name.toLowerCase())) ^ 0xffffffff) >>> 0;
}

export const END_GAP = checksum("EndGap");
export const SET_EVENT_HANDLER = checksum("SetEventHandler");
export const TRIGGER_SCRIPT = checksum("TriggerScript");
export const GAP_SCRIPT = checksum("gapscript");
export const SCR = checksum("scr");
export const SCORE = checksum("score");
export const TEXT = checksum("text");
export const VERSION = checksum("version");
export const POS = checksum("pos");
export const POSITION = checksum("position");
export const MAX_DEPTH = 5; // ForEachScriptCall's kMaxDepth

const ONE: ReadonlySet<number> = new Set([
    0x01, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x11, 0x12, 0x13, 0x14, 0x15,
    0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2c, 0x2d, 0x30, 0x32, 0x33, 0x34, 0x35, 0x36, 0x38,
    0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f, 0x42, 0x45,
]);
const FIVE: ReadonlySet<number> = new Set([0x02, 0x16, 0x17, 0x18, 0x1a, 0x2e, 0x43, 0x44, 0x46]);

// A byte of the data (raises past the end, as Python's d[i]).
function at(d: Buffer, i: number): number {
    const b = d[i];
    if (b === undefined) throw new RangeError(`index ${i} out of range`);
    return b;
}

export function u32(d: Buffer, i: number): number {
    return d.readUInt32LE(i);
}

function tokenError(t: number, i: number): Error {
    return new Error(`token 0x${t.toString(16).toUpperCase().padStart(2, "0")} at 0x${i.toString(16).toUpperCase()}`);
}

// Script::SkipToken (0 for tokens it doesn't step over: the caller stops).
export function skipToken(d: Buffer, i: number): number {
    const t = at(d, i);
    if (ONE.has(t)) return i + 1;
    if (FIVE.has(t)) return i + 5;
    if (t === 0x1b || t === 0x1c) return i + 5 + u32(d, i + 1);
    if (t === 0x1e) return i + 13;
    if (t === 0x1f) return i + 9;
    if (t === 0x2b) {
        const nul = d.indexOf(0, i + 5);
        if (nul < 0) throw new Error("subsection not found");
        return nul + 1;
    }
    if (t === 0x2f || t === 0x37 || t === 0x40 || t === 0x41) return i + 5 + u32(d, i + 1) * 6;
    if (t === 0x47 || t === 0x48 || t === 0x49) return i + 3;
    return i;
}

// Script::SkipToEndOfStatement: past the end of line outside any brackets.
export function skipStatement(d: Buffer, i: number): number {
    let depth = 0;
    for (;;) {
        const t = at(d, i);
        if (t === 0x03 || t === 0x05 || t === 0x0e) depth += 1;
        else if (t === 0x04 || t === 0x06 || t === 0x0f) depth -= 1;
        if (depth === 0 && (t === 0x00 || t === 0x01 || t === 0x02)) return skipToken(d, i);
        const nxt = skipToken(d, i);
        if (nxt === i) throw tokenError(t, i);
        i = nxt;
    }
}

// The scripts of a .qb file by name checksum: [data, offset of the script's code: after `script <name>`].
export type Symbols = Map<number, [Buffer, number]>;

// {name checksum: (data, offset of the script's code)} of a .qb file.
export function scriptsIn(d: Buffer): Symbols {
    const out: Symbols = new Map();
    let i = 0;
    while (i < d.length && d[i] !== 0x00) {
        if (d[i] === 0x23 && at(d, i + 1) === 0x16) {
            out.set(u32(d, i + 2), [d, i + 6]);
            i += 6;
            while (at(d, i) !== 0x24) {
                const nxt = skipToken(d, i);
                if (nxt === i) throw tokenError(at(d, i), i);
                i = nxt;
            }
            continue;
        }
        const nxt = skipToken(d, i);
        if (nxt === i) throw tokenError(at(d, i), i);
        i = nxt;
    }
    return out;
}

// A name (checksum) value.
export interface NameValue {
    kind: "name";
    value: number;
}

export interface VectorValue {
    kind: "vector";
    xyz: [number, number, number];
}

// A value this reader doesn't work out (an expression, a random choice).
export class Unknown {}

// The values read: an int (a number), a float (Float), a string, a name, a vector, a structure (Params), an array,
// Unknown; null: a missing <param>.
export type WalkValue = number | Float | string | NameValue | VectorValue | Unknown | Params | WalkValue[] | null;
export type Params = Map<number, WalkValue>;

export function isName(value: unknown): value is NameValue {
    return typeof value === "object" && value !== null && (value as { kind?: unknown }).kind === "name";
}

function isVector(value: unknown): value is VectorValue {
    return typeof value === "object" && value !== null && (value as { kind?: unknown }).kind === "vector";
}

function skipEndOfLines(d: Buffer, i: number): number {
    while (at(d, i) === 0x01 || at(d, i) === 0x02) i = skipToken(d, i);
    return i;
}

// One value: [value, next offset]; a missing <param> gives [null, next].
export function parseValue(d: Buffer, i: number, args: Params | null): [WalkValue, number] {
    const t = at(d, i);
    if (t === 0x17 || t === 0x18) return [d.readInt32LE(i + 1), i + 5];
    if (t === 0x1a) return [new Float(d.readFloatLE(i + 1)), i + 5];
    if (t === 0x1b || t === 0x1c) {
        const n = u32(d, i + 1);
        let s = d.subarray(i + 5, i + 5 + n);
        let end = s.length;
        while (end > 0 && s[end - 1] === 0) end--;
        s = s.subarray(0, end);
        return [s.toString("latin1"), i + 5 + n];
    }
    if (t === 0x16) return [{ kind: "name", value: u32(d, i + 1) }, i + 5];
    if (t === 0x1e)
        return [{ kind: "vector", xyz: [d.readFloatLE(i + 1), d.readFloatLE(i + 5), d.readFloatLE(i + 9)] }, i + 13];
    if (t === 0x1f) return [new Unknown(), i + 9];
    if (t === 0x2d && at(d, i + 1) === 0x16) {
        const name = u32(d, i + 2);
        return [args?.size ? (args.get(name) ?? null) : null, i + 6];
    }
    if (t === 0x03) {
        const dest: Params = new Map();
        return [dest, parseStructure(d, i, dest, args)];
    }
    if (t === 0x05) {
        const items: WalkValue[] = [];
        i += 1;
        for (;;) {
            i = skipEndOfLines(d, i);
            if (d[i] === 0x06) return [items, i + 1];
            if (d[i] === 0x09) {
                i += 1;
                continue;
            }
            const [value, next] = parseValue(d, i, args);
            items.push(value);
            i = next;
        }
    }
    if (t === 0x0a && (at(d, i + 1) === 0x17 || at(d, i + 1) === 0x1a)) {
        const [value, j] = parseValue(d, i + 1, args);
        if (value instanceof Float) return [new Float(-value.value), j];
        const n = value as number;
        return [n === 0 ? 0 : -n, j];
    }
    if (t === 0x0e) {
        let depth = 0;
        let j = i;
        for (;;) {
            if (at(d, j) === 0x0e) depth += 1;
            else if (d[j] === 0x0f) {
                depth -= 1;
                if (depth === 0) return [new Unknown(), j + 1];
            }
            j = skipToken(d, j);
        }
    }
    return [new Unknown(), skipToken(d, i)];
}

function isParams(value: WalkValue): value is Params {
    return value instanceof Map;
}

// AddParameter: a named value; an unnamed structure (or <param> naming one) merges in; flags are left out.
function add(dest: Params, name: number, value: WalkValue): void {
    if (name) {
        if (value !== null) dest.set(name, value);
    } else if (value !== null && isParams(value)) {
        for (const [k, v] of value) dest.set(k, v);
    }
}

function parseEntry(d: Buffer, i: number, dest: Params, args: Params | null, inStructure: boolean): number {
    const t = at(d, i);
    if (t === 0x03) return parseStructure(d, i, dest, args);
    if (t === 0x09) return i + 1;
    if (t === 0x2c) {
        if (args?.size) for (const [k, v] of args) dest.set(k, v);
        return i + 1;
    }
    if (t === 0x16) {
        const name = u32(d, i + 1);
        let j = inStructure ? skipEndOfLines(d, i + 5) : i + 5;
        if (at(d, j) !== 0x07) return i + 5; // a flag
        j = inStructure ? skipEndOfLines(d, j + 1) : j + 1;
        const [value, next] = parseValue(d, j, args);
        add(dest, name, value);
        return next;
    }
    const [value, j] = parseValue(d, i, args);
    add(dest, 0, value);
    return j;
}

// ParseStructure: the { ... } at `i` added into `dest`; the offset past its '}'.
export function parseStructure(d: Buffer, i: number, dest: Params, args: Params | null): number {
    i += 1;
    for (;;) {
        i = skipEndOfLines(d, i);
        if (d[i] === 0x04) return i + 1;
        i = parseEntry(d, i, dest, args, true);
    }
}

// ParseArguments: the call's parameters up to the end of the line; [parameters, offset of the end of line].
export function parseArguments(d: Buffer, i: number, args: Params | null): [Params, number] {
    const dest: Params = new Map();
    for (;;) {
        const t = at(d, i);
        if (t === 0x00 || t === 0x01 || t === 0x02 || t === 0x0f) break;
        i = parseEntry(d, i, dest, args, false);
    }
    return [dest, i];
}

// SkateScript::ForEachScriptCall: every call to `target` in `script` and the scripts it calls (statements are
// followed in order, both ways of every `if`). symbols: {checksum: [data, offset]} of the scripts.
export function walkScript(
    symbols: Symbols,
    script: number,
    target: number,
    callback: (params: Params) => void,
    parent: Params | null,
    depth: number,
): void {
    const found = symbols.get(script);
    if (depth > MAX_DEPTH || found === undefined) return;
    // The game knows each name's symbol type: a C function or member function that isn't `target` (or
    // SetEventHandler) has its statement skipped, any other name is stepped over. This reader doesn't have the
    // function tables, so a name that isn't a script counts as a function where a statement starts (after an end
    // of line, `if`, `not` or an object's ':' - where the game's scripts call them) and is stepped over elsewhere.
    const [d, start] = found;
    let i = skipStatement(d, start); // (past the first line: the script's default parameters)
    let atStart = true;
    for (;;) {
        const t = at(d, i);
        const starts = atStart;
        atStart = t === 0x01 || t === 0x02 || t === 0x39 || t === 0x42 || t === 0x47 || t === 0x48 || t === 0x49;
        if (t === 0x16) {
            const name = u32(d, i + 1);
            i += 5;
            if (at(d, i) === 0x07) continue;
            if (symbols.has(name)) {
                const [params, next] = parseArguments(d, i, parent);
                i = next;
                if (name === target) callback(params);
                walkScript(symbols, name, target, callback, params, depth + 1);
            } else if (name === target) {
                const [params, next] = parseArguments(d, i, parent);
                i = next;
                callback(params);
                const gapScript = params.get(GAP_SCRIPT);
                if (name === END_GAP && isName(gapScript)) {
                    walkScript(symbols, gapScript.value, target, callback, null, depth + 1);
                }
            } else if (name === SET_EVENT_HANDLER) {
                const [params, next] = parseArguments(d, i, parent);
                i = next;
                const handler = params.get(SCR);
                if (isName(handler)) walkScript(symbols, handler.value, target, callback, null, depth + 1);
            } else if (starts && d[i] !== 0x42) {
                i = skipStatement(d, i); // a C function or member function
                atStart = true;
            }
            // (else: an object's name before ':', or a name inside a statement: stepped over)
        } else if (t === 0x21) {
            i = skipStatement(d, skipToken(d, i));
            atStart = true;
        } else if (t === 0x47) {
            i = skipToken(d, i);
            if (at(d, i) === 0x39) i = skipToken(d, i);
            if (at(d, i) === 0x0e) {
                i = skipStatement(d, i);
                atStart = true;
            }
        } else if (t === 0x24) {
            return;
        } else {
            const nxt = skipToken(d, i);
            if (nxt === i) throw tokenError(t, i);
            i = nxt;
        }
    }
}

// The top-level `name = value` of a .qb file (null: not there).
export function globalValue(d: Buffer, name: number): WalkValue {
    let i = 0;
    while (i < d.length && d[i] !== 0x00) {
        if (d[i] === 0x23) {
            while (at(d, i) !== 0x24) i = skipToken(d, i);
            i += 1;
            continue;
        }
        if (d[i] === 0x16 && u32(d, i + 1) === name) {
            const j = skipEndOfLines(d, i + 5);
            if (at(d, j) === 0x07) return parseValue(d, skipEndOfLines(d, j + 1), null)[0];
        }
        const nxt = skipToken(d, i);
        if (nxt === i) throw tokenError(at(d, i), i);
        i = nxt;
    }
    return null;
}

// A gap: [name, score, position (x, y, z) or null].
export type Gap = [string, number, [number, number, number] | null];

// The gaps CGapChecklist::Build finds: [name, score, position] in the order found, each (name, score) once;
// position: (x, y, z) of the first node whose TriggerScript leads to the gap's EndGap (where the gap ends - the
// game keeps where the skater started it, once landed, for the gap menu's camera), or null.
export function gapList(symbols: Symbols, nodeArray: readonly WalkValue[]): Gap[] {
    const gaps: Gap[] = [];
    const seen = new Set<string>();
    let position: [number, number, number] | null = null;
    const found = (params: Params): void => {
        const score = params.get(SCORE);
        const text = params.get(TEXT);
        if (typeof score === "number" && score && typeof text === "string" && text && !seen.has(`${score}:${text}`)) {
            seen.add(`${score}:${text}`);
            gaps.push([text, score, position]);
        }
    };
    for (const node of nodeArray) {
        if (node !== null && isParams(node) && isName(node.get(TRIGGER_SCRIPT))) {
            const pos = node.has(POS) ? node.get(POS) : node.get(POSITION);
            position = isVector(pos) ? pos.xyz : null;
            walkScript(symbols, (node.get(TRIGGER_SCRIPT) as NameValue).value, END_GAP, found, null, 0);
        }
    }
    return gaps;
}

// The AddGoal_* calls in the classic block of `goalsScript` (the first `if` testing is_classic): [AddGoal checksum,
// version or null]. addGoalNames: the AddGoal_* scripts' checksums.
export function classicGoalCalls(
    symbols: Symbols,
    goalsScript: number,
    addGoalNames: ReadonlySet<number>,
): [number, number | null][] {
    const isClassic = Buffer.alloc(4);
    isClassic.writeUInt32LE(checksum("is_classic"));
    const found = symbols.get(goalsScript);
    if (found === undefined) throw new Error(`KeyError: ${goalsScript}`);
    const [d, start] = found;
    let i = skipStatement(d, start);
    const calls: [number, number | null][] = [];
    let depth = 0;
    let inside = false;
    while (at(d, i) !== 0x24) {
        const t = d[i]!;
        const end = skipStatement(d, i);
        if (t === 0x47) {
            if (inside) depth += 1;
            else if (d.subarray(i, end).indexOf(isClassic) >= 0) {
                inside = true;
                depth = 0;
            }
        } else if (t === 0x48 && inside && depth === 0) {
            break;
        } else if (t === 0x49 && inside) {
            if (depth === 0) break;
            depth -= 1;
        } else if (t === 0x16 && inside && addGoalNames.has(u32(d, i + 1))) {
            const [params] = parseArguments(d, i + 5, null);
            const version = params.get(VERSION);
            calls.push([u32(d, i + 1), typeof version === "number" ? version : null]);
        }
        i = end; // (an `if` line is only its condition: its body follows as statements)
    }
    return calls;
}
