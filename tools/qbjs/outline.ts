// NOT ORIGINAL - a translated script's outline: its QB statements, kept with its TypeScript (the .ts manifest's
// `outlines`) for the engine code that reads a script's code without running it - the gap list's walk over the nodes'
// TriggerScripts (engine/script/nodes.cpp for_each_script_call: every call's arguments parsed, its Randoms drawn), the
// level editor's trigger walks, the editor camera's script flags (game/world/editor_camera.cpp). The game puts the
// outline after the script's TypeScript token (src/script_qb/ts_bridge.cpp defineScript): the interpreter runs the
// TypeScript and never reaches it; those readers see exactly the statements the .qb's script has (docs/qb-ts.md
// "Outlines").
//
// The token walk below is the game's (engine/script/qb.cpp step_over_token, skip_to_end_of_statement,
// skip_to_end_of_script), so an outline is the bytes those readers would have read after the script's first line.
import type { ModuleIR } from "./ir.ts";

const WORD = 5; // a token and a 4-byte operand
const VECTOR = 13;
const PAIR = 9;
const TABLE_ENTRY = 6; // a Random's 16-bit weight and 32-bit offset

const word32 = (b: Uint8Array, at: number) =>
    (b[at]! | (b[at + 1]! << 8) | (b[at + 2]! << 16) | (b[at + 3]! << 24)) >>> 0;

// The token after the one at `at` (qb.cpp step_over_token: an unknown byte is not stepped over).
export function stepOverToken(b: Uint8Array, at: number): number {
    const t = b[at]!;
    if (t === 0x01 || (t >= 0x03 && t <= 0x0f) || (t >= 0x11 && t <= 0x15)) return at + 1;
    if ((t >= 0x20 && t <= 0x29) || t === 0x2c || t === 0x2d || t === 0x30) return at + 1;
    if ((t >= 0x32 && t <= 0x36) || t === 0x38 || t === 0x39 || t === 0x3a || t === 0x3b) return at + 1;
    if ((t >= 0x3c && t <= 0x3f) || t === 0x42 || t === 0x45) return at + 1;
    if (t === 0x02 || (t >= 0x16 && t <= 0x18) || t === 0x1a || t === 0x2e) return at + WORD;
    if (t === 0x43 || t === 0x44 || t === 0x46 || t === 0x4a) return at + WORD;
    if (t === 0x1b || t === 0x1c) return at + WORD + word32(b, at + 1);
    if (t === 0x1e) return at + VECTOR;
    if (t === 0x1f) return at + PAIR;
    if (t === 0x2b) {
        let p = at + WORD;
        while (b[p] !== 0) ++p;
        return p + 1;
    }
    if (t === 0x2f || t === 0x37 || t === 0x40 || t === 0x41) return at + WORD + word32(b, at + 1) * TABLE_ENTRY;
    if (t === 0x47 || t === 0x48 || t === 0x49) return at + 3;
    return at;
}

// Past the statement at `at` and its line's end (qb.cpp skip_to_end_of_statement: brackets of each kind counted).
export function skipToEndOfStatement(b: Uint8Array, at: number): number {
    let braces = 0,
        brackets = 0,
        parentheses = 0;
    for (let p = at; ; ) {
        if (p >= b.length) throw Error(`QB offset 0x${at.toString(16)}: a statement runs past the file`);
        const t = b[p]!;
        if (t === 0x03) ++braces;
        if (t === 0x04) --braces;
        if (t === 0x05) ++brackets;
        if (t === 0x06) --brackets;
        if (t === 0x0e) ++parentheses;
        if (t === 0x0f) --parentheses;
        if (braces === 0 && brackets === 0 && parentheses === 0 && (t === 0x01 || t === 0x02 || t === 0x00))
            return stepOverToken(b, p);
        const next = stepOverToken(b, p);
        if (next === p) throw Error(`QB offset 0x${p.toString(16)}: token 0x${t.toString(16)} has no size`);
        p = next;
    }
}

// The outline of the script whose code starts at `start` (after its name): from past its first line (its default
// parameters) to its endscript, that included.
export function outlineAt(b: Uint8Array, start: number): Uint8Array {
    const from = skipToEndOfStatement(b, start);
    let p = from;
    while (b[p] !== 0x24) {
        if (p >= b.length) throw Error(`QB offset 0x${start.toString(16)}: a script with no endscript`);
        const next = stepOverToken(b, p);
        if (next === p) throw Error(`QB offset 0x${p.toString(16)}: token 0x${b[p]!.toString(16)} has no size`);
        p = next;
    }
    return b.subarray(from, p + 1);
}

// Every script's outline, in the file's order: [name, base64] (a script's code starts 6 bytes after its 0x23 - the
// token, the name's 0x16 and its checksum - as the loader reads it: engine/script/load.cpp).
export function scriptOutlines(bytes: Uint8Array, ir: ModuleIR): [number, string][] {
    const out: [number, string][] = [];
    for (const d of ir.definitions)
        if (d.kind === "script") out.push([d.name, Buffer.from(outlineAt(bytes, d.offset + 6)).toString("base64")]);
    return out;
}

// The manifest's form: {"<name, 8 hex digits>": [base64, ...]} - a list per name, in definition order (a name a file
// defines twice gets each definition's).
export function outlineManifest(outlines: [number, string][]): Record<string, string[]> {
    const out: Record<string, string[]> = {};
    for (const [name, code] of outlines) {
        const key = name.toString(16).padStart(8, "0");
        out[key] = [...(out[key] ?? []), code];
    }
    return out;
}
