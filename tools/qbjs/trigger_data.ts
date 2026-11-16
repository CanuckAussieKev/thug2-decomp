// NOT ORIGINAL - a script file's trigger data: what each of its scripts does when a node's TriggerScript runs it,
// declared as data in the file's .json ("triggers" - qb_data.ts, docs/qb-ts.md "Trigger data"), so that the engine code
// that wants to know it reads data, never script code:
//   - the gap list (game/skate/gap_checklist.cpp: every EndGap a level's nodes' TriggerScripts reach - its text and
//     score),
//   - the level editor's trigger kinds (editor/level_editor_triggers*.cpp: kills, teleports, gaps, boosts...),
//   - the editor camera's keep-out flag (game/world/editor_camera.cpp: a trigger whose script kills, teleports or is a
//     boundary).
// A script's declaration: for each function of DECLARED_FUNCTIONS, the arguments of every call to it the script makes
// - directly or through the scripts it calls (their arguments passed on), SetEventHandler's scr= handlers and (EndGap's
// only) EndGap's gapscript= -, in the order reached, as values (Shatter's and ShatterAndDie's: the calls alone - their
// arguments, the pieces' scatter, often drawn at random, aren't the trigger's data); and whether it keeps the editor
// camera out. No declared value depends on a random draw (the tool reports one that would - randomDependent).
//
// The rules are the engine's own reading of a QB script (a QB script's declaration is made from its code in the game:
// engine/script/declared_calls.cpp - nodes.cpp for_each_script_call per function, editor_camera.cpp's flag walk), here
// over the .qb a module's TypeScript is made from, against the module's own scripts and the boot scripts:
//   - the code as the loader prepares it (qb.cpp prepare_script_tokens: a statement's first name that is a C function
//     or a member function made a direct reference);
//   - for_each_script_call (0x0045C950) from the script with no arguments, down to 5 levels, for each function: a call
//     to a script is followed (its arguments parsed with the caller's - its Randoms drawn: here a Random's choice is
//     taken by a fixed rule; a declaration whose values depend on it is reported), SetEventHandler's scr= too, and
//     EndGap's gapscript= when the function is EndGap;
//   - the camera's flag (0x004E4F50): the scripts called by name at a statement's start, any depth, one of
//     SK3_KillSkater, SK3_KillSkater_Finish, TeleportSkaterToNode, CAG_Boundary reached (EditorCamera never followed).
// The script check proves both against the game (docs/script-check.md "Trigger data": each module's declarations made
// from its QB in the game and read from its .json - the same).

import { runInThisContext } from "node:vm";
import { CFUNC_NAMES } from "thug2-level-converter-core/level_port/thug2_cfuncs.ts";
import { NAMED_MEMBERS, TABLE_MEMBERS } from "thug2-level-converter-core/level_port/thug2_members.ts";
import { gameRoot, type QbVersion, qbVersions } from "./game_scripts.ts";
import { helperSource } from "./helper-sources.ts";
import type { Expr, Field, ModuleIR } from "./ir.ts";
import { nameChecksum, type QbDataValue } from "./qb_data.ts";
import { argumentsReader, readIR } from "./reader.ts";

// The functions a declaration lists the calls of: what the engine asks for (engine/script/declared_calls.cpp
// kDeclaredFunctions - the same list, in the same order).
export const DECLARED_FUNCTIONS = [
    "EndGap",
    "StartGap",
    "SK3_KillSkater",
    "SK3_KillSkater_Water",
    "SK3_KillSkater_Pungee",
    "SK3_Killskater_Finish",
    "KillSkater",
    "TeleportSkaterToNode",
    "trigger_jump_boost",
    "Shatter",
    "ShatterAndDie",
    "RailAccelerate",
    "ed_RailSpeed",
    "LevelEditor_PlayCamera",
] as const;
// The flag's member in a declaration.
export const CAMERA_FLAG = "keeps_editor_camera_out";

// C functions this project adds to THUG2's (NOT ORIGINAL - registered in src: trigger_data.test.ts checks the list
// against the sources): a statement starting with one is prepared as a C function's.
export const ADDED_C_FUNCTIONS = [
    "Career100_Apply",
    "Career100_Available",
    "Demo_GetEntry",
    "Demo_Item",
    "Demo_Play",
    "Demo_Record",
    "Demo_Stop",
    "LevelEditor_Available",
    "LevelEditor_EditedLevels",
    "LevelEditor_EditorMode",
    "LevelEditor_PickLevel",
    "LevelEditor_PlayCamera",
    "LevelEditor_Start",
    "LevelEditor_Stop",
    "NewFrontend_Item",
    "NewFrontend_Open",
    "PaintTrailsClear",
    "TeleportSkaterStanding",
    "ThawBmx_Available",
    "ThawBmx_BikeReachable",
    "ThawBmx_HomeSpot",
    "ThawBmx_LoadTexture",
    "ThawBmx_WalkToBike",
];

const C_FUNCTIONS = new Set([...CFUNC_NAMES, ...ADDED_C_FUNCTIONS].map(nameChecksum));
const MEMBERS = new Set([...TABLE_MEMBERS.map(([n]) => n >>> 0), ...NAMED_MEMBERS.map(([n]) => nameChecksum(n))]);

const SET_EVENT_HANDLER = nameChecksum("SetEventHandler");
const END_GAP = nameChecksum("EndGap");
const GAP_SCRIPT = nameChecksum("gapscript");
const SCR = nameChecksum("scr");
const EDITOR_CAMERA = 0x43bfab74; // (editor_camera.cpp kEditorCameraName)
const KEEP_OUT = ["SK3_KillSkater_Finish", "SK3_KillSkater", "TeleportSkaterToNode", "CAG_Boundary"].map(nameChecksum);
const DEEPEST = 5;
const FUNCTION_NAMES = DECLARED_FUNCTIONS.map(nameChecksum);
const KEEP_OUT_MASK = 1 << DECLARED_FUNCTIONS.length;

// Tokens (engine/script/qb.h).
const NAME = 0x16,
    EQUALS = 0x07,
    NOT = 0x39,
    OPEN = 0x0e,
    COLON = 0x42,
    REPEAT = 0x21,
    SHORT_IF = 0x47,
    END = 0x24,
    NATIVE = 0x43,
    MEMBER_NAME = 0x44,
    MEMBER = 0x46;
const WORD = 5;
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
    if (t === 0x1e) return at + 13;
    if (t === 0x1f) return at + 9;
    if (t === 0x2b) {
        let p = at + WORD;
        while (b[p] !== 0) ++p;
        return p + 1;
    }
    if (t === 0x2f || t === 0x37 || t === 0x40 || t === 0x41) return at + WORD + word32(b, at + 1) * 6;
    if (t === 0x47 || t === 0x48 || t === 0x49) return at + 3;
    return at;
}
const step = (b: Uint8Array, at: number) => {
    const next = stepOverToken(b, at);
    if (next === at) throw Error(`QB offset 0x${at.toString(16)}: token 0x${b[at]!.toString(16)} has no size`);
    return next;
};

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
        p = step(b, p);
    }
}

// What a name is to the walks (resolve_symbol's kind).
type Kind = "script" | "function" | "member" | "other";

// A script's code as the walks read it: the file's bytes with the loader's preparation (prepare_script_tokens) - a
// statement's first name that is a C function or a member function retagged (its checksum kept: the walks only ask
// what it is) - and where its code starts (after its name).
// `names`: every name its code mentions (as a call, an argument, anything) - what it may reach.
type Code = {
    bytes: Uint8Array;
    start: number;
    read: (offset: number) => { fields: Field[]; end: number };
    names: number[];
};

// A script source: a module's bytes and IR (its scripts and globals), read once.
export type Source = { bytes: Uint8Array; ir: ModuleIR };

function prepared(source: Source): Map<number, Code> {
    const bytes = Uint8Array.from(source.bytes);
    const read = argumentsReader(source.bytes, source.ir.source);
    const out = new Map<number, Code>();
    for (const d of source.ir.definitions) {
        if (d.kind !== "script") continue;
        // (a script's code starts 6 bytes after its 0x23 - the token, the name's 0x16 and its checksum: load.cpp)
        const start = d.offset + 6;
        let t = skipToEndOfStatement(bytes, start);
        while (bytes[t] !== END) {
            if (bytes[t] === NAME) {
                const name = word32(bytes, t + 1);
                if (C_FUNCTIONS.has(name)) bytes[t] = NATIVE;
                else if (MEMBERS.has(name)) bytes[t] = MEMBER_NAME;
                t = skipToEndOfStatement(bytes, t + WORD);
            } else if (bytes[t] === SHORT_IF) {
                t = step(bytes, t);
                if (bytes[t] === NOT) ++t;
            } else t = skipToEndOfStatement(bytes, t);
        }
        const names: number[] = [];
        for (let at = start; bytes[at] !== END; at = step(bytes, at))
            if ([NAME, NATIVE, MEMBER_NAME, MEMBER].includes(bytes[at]!)) names.push(word32(bytes, at + 1));
        out.set(d.name >>> 0, { bytes, start, read, names });
    }
    return out;
}

// The value helpers (scripting/values.js) - the game's own value rules, as the translated scripts use them.
type Values = {
    fields(p: unknown[], entries: unknown[], ctx: unknown): unknown[];
    structure(fields: unknown[], ctx: unknown): unknown;
    array(type: number, items: unknown[], ctx: unknown): unknown;
    copy(v: unknown, mode: string, ctx: unknown): unknown;
    expressionResult(v: unknown, ctx: unknown): unknown;
    param(p: unknown[], n: number, mode: string, ctx: unknown): unknown;
    binary(op: number, a: unknown, b: unknown, ctx: unknown): unknown;
    global(n: number, ctx: unknown): unknown;
    readChecksum(p: unknown[], n: number, ctx: unknown): { bits: number } | undefined;
};
let valuesHelpers: Values | undefined;
function values(): Values {
    if (!valuesHelpers) {
        const holder = globalThis as unknown as { qbValues?: Values };
        if (!holder.qbValues) runInThisContext(helperSource("values"));
        valuesHelpers = holder.qbValues!;
    }
    return valuesHelpers;
}

// A value object (values.js) as qb-data's (qb_data.ts): references resolved to their values.
type Value = {
    type: number;
    bits?: number;
    bytes?: number[];
    coordinates?: number[];
    members?: [number, Value][];
    elements?: Value[];
    elementType?: number;
    target?: Value;
};
function dataValue(v: Value, token: (bytes: number[]) => string | undefined): QbDataValue {
    const x = v.type === 20 || v.type === 21 ? v.target! : v;
    switch (x.type) {
        case 1:
        case 2:
        case 13:
            return { type: x.type, bits: x.bits! >>> 0 };
        case 3:
        case 4: {
            const t = token(x.bytes!);
            return t === undefined
                ? { type: x.type, bytes: [...x.bytes!] }
                : { type: x.type, bytes: [...x.bytes!], token: t };
        }
        case 5:
        case 6:
            return { type: x.type, coordinates: [...x.coordinates!] };
        case 10:
            return { type: 10, members: x.members!.map(([n, m]) => [n, dataValue(m, token)]) };
        case 12:
            return { type: 12, elementType: x.elementType!, elements: x.elements!.map((e) => dataValue(e, token)) };
        default:
            throw Error(`a value of type ${x.type} in a declared call's arguments`);
    }
}

// A script's declaration: each declared function's calls' arguments (in order), and the camera's flag.
// The functions whose calls are declared without their arguments.
export const CALLS_ONLY = new Set<string>(["Shatter", "ShatterAndDie"]);
export type Declaration = { calls: Map<string, QbDataValue[]>; keepsCameraOut: boolean };

// The trigger data of modules against a set of boot scripts (the files the game loads at start-up: their scripts and
// globals are what a module's calls reach when they aren't its own).
export class TriggerData {
    private readonly bootScripts = new Map<number, Code>();
    private readonly bootGlobals = new Map<number, Expr>();
    // (the Randoms met while the declarations were made: a declaration whose values depend on one is reported)
    readonly randomDependent: string[] = [];
    // (scripts whose declaration couldn't be made - their walk met something the rules don't read: none declared)
    readonly failed: string[] = [];

    // `boot`: the boot files; `triggers`: the scripts the nodes' TriggerScripts name (triggerScripts: every level's) -
    // the ones declared (what the engine asks about).
    private readonly triggers: Set<number>;
    // (the names' text the boot files spell, and the data's own keys: the writer spells what it can)
    private readonly names = new Map<number, string>(
        [...DECLARED_FUNCTIONS, "calls", CAMERA_FLAG].map((n) => [nameChecksum(n), n]),
    );
    constructor(boot: Source[], triggers: Set<number>) {
        this.triggers = triggers;
        for (const source of boot)
            for (const [n, text] of Object.entries(source.ir.names ?? {})) this.names.set(Number(n) >>> 0, text);
        for (const source of boot) {
            for (const [name, code] of prepared(source)) this.bootScripts.set(name, code);
            for (const d of source.ir.definitions) if (d.kind === "global") this.bootGlobals.set(d.name >>> 0, d.value);
        }
    }

    // A name's text where the boot files or the data's keys spell it.
    nameText(n: number): string | undefined {
        return this.names.get(n >>> 0);
    }

    // Every script of `module` that declares anything, in definition order. `token`: a string's translation token
    // (localize.ts; none: its text).
    declarations(
        module: Source,
        token: (bytes: number[]) => string | undefined = () => undefined,
    ): [number, Declaration][] {
        const scripts = prepared(module);
        const globals = new Map(this.bootGlobals);
        for (const d of module.ir.definitions) if (d.kind === "global") globals.set(d.name >>> 0, d.value);
        const out: [number, Declaration][] = [];
        const seen = new Set<number>();
        const relevant = this.relevance(scripts);
        for (const d of module.ir.definitions) {
            if (d.kind !== "script" || !this.triggers.has(d.name >>> 0)) continue;
            const name = d.name >>> 0;
            // (a name a file defines twice: its last definition - as the loader leaves it)
            if (seen.has(name))
                out.splice(
                    out.findIndex(([n]) => n === name),
                    1,
                );
            seen.add(name);
            const mask = relevant(name);
            if (mask === 0) continue;
            let first: Declaration;
            try {
                const random = { met: false };
                first = this.declarationOf(name, { own: scripts, globals, mask, pick: "first", token, random });
                // (a Random met: its other choice taken everywhere - the same declaration, or it depends on the draw)
                if (random.met) {
                    const last = this.declarationOf(name, { own: scripts, globals, mask, pick: "last", token, random });
                    const differ = DECLARED_FUNCTIONS.filter(
                        (f) => JSON.stringify(first.calls.get(f)) !== JSON.stringify(last.calls.get(f)),
                    );
                    if (differ.length || first.keepsCameraOut !== last.keepsCameraOut)
                        this.randomDependent.push(
                            `${module.ir.source}: ${name.toString(16).padStart(8, "0")} (${differ.join(", ")})`,
                        );
                }
            } catch (e) {
                this.failed.push(
                    `${module.ir.source}: ${name.toString(16).padStart(8, "0")}: ${String((e as Error).stack ?? e)}`,
                );
                continue;
            }
            if (first.keepsCameraOut || [...first.calls.values()].some((c) => c.length)) out.push([name, first]);
        }
        return out;
    }

    // Which declared functions (bit i: DECLARED_FUNCTIONS[i]; KEEP_OUT_MASK: a keep-out name) a script of `own` or the
    // boot scripts may reach: the names mentioned anywhere in the scripts any of its names reach (Tarjan's strongly
    // connected components, one mask per component). A script whose mask is 0 declares nothing - not walked.
    private relevance(own: Map<number, Code>): (script: number) => number {
        const code = (n: number) => own.get(n) ?? this.bootScripts.get(n);
        const direct = (n: number) => {
            let mask = 0;
            for (const [i, f] of FUNCTION_NAMES.entries()) if (n === f) mask |= 1 << i;
            if (KEEP_OUT.includes(n)) mask |= KEEP_OUT_MASK;
            return mask;
        };
        const masks = new Map<number, number>();
        const index = new Map<number, number>();
        const low = new Map<number, number>();
        const stack: number[] = [];
        const onStack = new Set<number>();
        let counter = 0;
        const visit = (n: number) => {
            index.set(n, counter);
            low.set(n, counter++);
            stack.push(n);
            onStack.add(n);
            let mask = direct(n);
            for (const m of code(n)!.names) {
                mask |= direct(m);
                if (!code(m)) continue;
                if (!index.has(m)) {
                    visit(m);
                    low.set(n, Math.min(low.get(n)!, low.get(m)!));
                    mask |= masks.get(m) ?? 0;
                } else if (onStack.has(m)) low.set(n, Math.min(low.get(n)!, index.get(m)!));
                else mask |= masks.get(m) ?? 0;
            }
            masks.set(n, mask);
            if (low.get(n) === index.get(n)) {
                const component: number[] = [];
                let m: number;
                do {
                    m = stack.pop()!;
                    onStack.delete(m);
                    component.push(m);
                } while (m !== n);
                let all = 0;
                for (const c of component) all |= masks.get(c)!;
                for (const c of component) masks.set(c, all);
            }
        };
        return (script) => {
            if (!code(script)) return 0;
            if (!index.has(script)) visit(script);
            return masks.get(script)!;
        };
    }

    private declarationOf(
        script: number,
        how: {
            own: Map<number, Code>;
            globals: Map<number, Expr>;
            mask: number;
            pick: "first" | "last";
            token: (bytes: number[]) => string | undefined;
            random: { met: boolean };
        },
    ): Declaration {
        const { own, globals, mask, pick, token, random } = how;
        const code = (n: number) => own.get(n) ?? this.bootScripts.get(n);
        const kind = (n: number): Kind =>
            code(n) ? "script" : C_FUNCTIONS.has(n) ? "function" : MEMBERS.has(n) ? "member" : "other";
        const v = values();
        // (a global's value as a symbol's - the names standing for others followed by the value helpers)
        const globalValues = new Map<number, unknown>();
        const symbol = (n: number): unknown => {
            if (code(n)) return { kind: "callable", type: 7, name: n };
            const g = globals.get(n);
            if (!g) return undefined;
            if (!globalValues.has(n)) globalValues.set(n, value(g, []));
            return globalValues.get(n);
        };
        const ctx = (offset: number) => ({
            site: { source: "trigger data", script, offset },
            owner: 0,
            limits: {},
            charge: () => undefined,
            symbol,
        });
        // An expression's value with the caller's parameters `p` (emitter.ts's forms, evaluated here).
        const value = (x: Expr, p: unknown[]): unknown => {
            const c = ctx(x.offset);
            switch (x.kind) {
                case "expressionResult":
                    return v.expressionResult(value(x.value, p), c);
                case "literal":
                    return v.copy(x.value, "value", c);
                case "global":
                case "functionCall": {
                    if (C_FUNCTIONS.has(x.name >>> 0))
                        throw Error(`a C function in a declared call's arguments: ${x.name.toString(16)}`);
                    return v.global(x.name >>> 0, c);
                }
                case "parameter":
                    return v.param(p, x.name, x.mode, c);
                case "allParameters":
                    return x.copy ? v.structure(p, c) : { type: 20, target: { type: 10, members: p } };
                case "binary":
                    return v.binary(x.op, value(x.left, p), value(x.right, p), c);
                case "randomRange":
                    random.met = true;
                    return {
                        type: 2,
                        bits: new Uint32Array(new Float32Array([pick === "first" ? x.a : x.b]).buffer)[0],
                    };
                case "randomChoice":
                    random.met = true;
                    return value(pick === "first" ? x.choices[0]! : x.choices.at(-1)!, p);
                case "structure":
                    return v.structure(fields(x.fields, p, x.offset), c);
                case "array": {
                    const types = x.items.map((item) =>
                        item.kind === "literal"
                            ? item.value.type
                            : item.kind === "structure"
                              ? 10
                              : item.kind === "array"
                                ? 12
                                : 0,
                    );
                    const type = types.includes(2) && types.every((t) => t === 1 || t === 2) ? 2 : (types[0] ?? 0);
                    return v.array(
                        type,
                        x.items.map((item) => value(item, p)),
                        c,
                    );
                }
                case "inlineScript":
                    throw Error("a script value in a declared call's arguments");
            }
        };
        const entries = (xs: Field[], p: unknown[]): unknown[] =>
            xs.map((f) =>
                "spread" in f
                    ? null
                    : "kind" in f
                      ? {
                            group: () => {
                                random.met = true;
                                return entries(pick === "first" ? f.branches[0]! : f.branches.at(-1)!, p);
                            },
                        }
                      : () => [
                            f.name,
                            value(f.value, p),
                            ...(f.flattenStructure
                                ? [true]
                                : f.value.kind === "literal" && f.value.value.type === 6
                                  ? [false, true]
                                  : []),
                        ],
            );
        const fields = (xs: Field[], p: unknown[], offset: number) => v.fields(p, entries(xs, p), ctx(offset));
        // parse_arguments at `at` with the caller's parameters (null: none - <x> and <...> add nothing).
        const parse = (c: Code, at: number, parent: unknown[] | null) => {
            const { fields: xs, end } = c.read(at);
            return { params: fields(xs, parent ?? [], at) as [number, Value][], end };
        };
        const readName = (params: [number, Value][], n: number) => v.readChecksum(params, n, ctx(0))?.bits ?? 0;

        const calls = new Map<string, QbDataValue[]>();
        for (const [i, name] of DECLARED_FUNCTIONS.entries()) {
            if ((mask & (1 << i)) === 0) {
                calls.set(name, []);
                continue;
            }
            const target = FUNCTION_NAMES[i]!;
            const declared = (params: [number, Value][]) =>
                CALLS_ONLY.has(name)
                    ? { type: 10 as const, members: [] }
                    : dataValue({ type: 10, members: params }, token);
            const found: QbDataValue[] = [];
            const walk = (s: number, parent: unknown[] | null, depth: number) => {
                if (depth > DEEPEST) return;
                const c = code(s);
                if (!c) return;
                const b = c.bytes;
                let t = skipToEndOfStatement(b, c.start);
                for (;;) {
                    const at = b[t]!;
                    if (at === NAME || at === MEMBER_NAME || at === MEMBER) {
                        // (nodes.cpp follow_name_call)
                        const n = word32(b, t + 1);
                        t += WORD;
                        if (b[t] === EQUALS) continue;
                        const k = at === NAME ? kind(n) : "member";
                        if (k === "script") {
                            const { params, end } = parse(c, t, parent);
                            t = end;
                            if (n === target) found.push(declared(params));
                            else t = skipToEndOfStatement(b, t);
                            walk(n, params, depth + 1);
                        } else if (k === "function" || k === "member") {
                            if (n !== target) {
                                t = skipToEndOfStatement(b, t);
                                continue;
                            }
                            const { params, end } = parse(c, t, parent);
                            t = end;
                            found.push(declared(params));
                            if (k === "member" && n === END_GAP) {
                                const gap = readName(params, GAP_SCRIPT);
                                if (gap) walk(gap, null, depth + 1);
                            }
                        }
                    } else if (at === NATIVE) {
                        // (a C function: SetEventHandler's scr= followed)
                        const n = word32(b, t + 1);
                        t += WORD;
                        if (n !== SET_EVENT_HANDLER) {
                            t = skipToEndOfStatement(b, t);
                            continue;
                        }
                        const { params, end } = parse(c, t, parent);
                        t = end;
                        const handler = readName(params, SCR);
                        if (handler) walk(handler, null, depth + 1);
                    } else if (at === REPEAT) t = skipToEndOfStatement(b, step(b, t));
                    else if (at === SHORT_IF) {
                        t = step(b, t);
                        if (b[t] === NOT) t = step(b, t);
                        if (b[t] === OPEN) t = skipToEndOfStatement(b, t);
                    } else if (at === END) return;
                    else t = step(b, t);
                }
            };
            walk(script, null, 0);
            calls.set(name, found);
        }

        // The camera's flag (editor_camera.cpp script_flagged, the scripts seen kept for the whole walk).
        const seen = new Set<number>();
        const flagged = (n: number): boolean => {
            if (seen.has(n)) return false;
            seen.add(n);
            if (n === EDITOR_CAMERA) return false;
            if (KEEP_OUT.includes(n)) return true;
            const c = code(n);
            if (!c) return false;
            const b = c.bytes;
            let t = skipToEndOfStatement(b, c.start);
            while (b[t] !== END) {
                if (b[t] === NAME) {
                    let called = word32(b, t + 1);
                    let next = t + WORD;
                    if (b[next] === COLON) {
                        ++next;
                        if (b[next] !== NAME) {
                            t = skipToEndOfStatement(b, next);
                            continue;
                        }
                        called = word32(b, next + 1);
                        next += WORD;
                    }
                    if (flagged(called)) return true;
                    t = skipToEndOfStatement(b, next);
                } else if (b[t] === SHORT_IF) {
                    t = step(b, t);
                    if (b[t] === NOT) ++t;
                } else t = skipToEndOfStatement(b, t);
            }
            return false;
        };
        return { calls, keepsCameraOut: (mask & KEEP_OUT_MASK) !== 0 && flagged(script) };
    }
}

// The scripts the nodes of a module's node arrays name by TriggerScript= (a global's structures, at any depth).
const TRIGGER_SCRIPT = nameChecksum("TriggerScript");
export function triggerScripts(ir: ModuleIR, into = new Set<number>()): Set<number> {
    const visit = (x: Expr) => {
        if (x.kind === "array") x.items.forEach(visit);
        else if (x.kind === "structure")
            for (const f of x.fields) {
                if (!("value" in f) || "kind" in f) continue;
                if (f.name >>> 0 === TRIGGER_SCRIPT && f.value.kind === "literal" && f.value.value.type === 13)
                    into.add(f.value.value.bits >>> 0);
                visit(f.value);
            }
    };
    for (const d of ir.definitions) if (d.kind === "global") visit(d.value);
    return into;
}

// The module's declarations as the data file's "triggers" value: {"<script>": {"calls": {"EndGap": [{...}], ...},
// "keeps_editor_camera_out": true}} - only the functions with calls, the flag only when set.
export function triggersValue(declarations: [number, Declaration][]): [number, QbDataValue][] {
    return declarations.map(([name, d]) => {
        const calls: [number, QbDataValue][] = [];
        for (const [fn, list] of d.calls)
            if (list.length) calls.push([nameChecksum(fn), { type: 12, elementType: 10, elements: list }]);
        const members: [number, QbDataValue][] = [];
        if (calls.length) members.push([nameChecksum("calls"), { type: 10, members: calls }]);
        if (d.keepsCameraOut) members.push([0, { type: 13, bits: nameChecksum(CAMERA_FLAG) }]);
        return [name, { type: 10, members }];
    });
}

// The translation tokens of the scripts' strings by their text (localize.ts gave the literals tokens), file by file: a
// declared string takes its file's token for that text, else the boot files' (a helper the gap is declared through
// - its text one of theirs) when they have only one for it, else none (its text as it is).
export class TextTokens {
    private readonly boot = new Map<string, string | null>(); // (null: two tokens for one text)
    // A localised module's tokens; `boot`: whether it is a boot file's (its tokens offered to every file).
    of(ir: ModuleIR, boot: boolean): Map<string, string> {
        const own = new Map<string, string>();
        const visit = (node: unknown): void => {
            if (Array.isArray(node)) node.forEach(visit);
            else if (node && typeof node === "object") {
                const x = node as { type?: number; bytes?: number[]; token?: string };
                if ((x.type === 3 || x.type === 4) && Array.isArray(x.bytes) && typeof x.token === "string")
                    own.set(textKey(x.type, x.bytes), x.token);
                else Object.values(node).forEach(visit);
            }
        };
        visit(ir.definitions);
        if (boot)
            for (const [key, token] of own) {
                const had = this.boot.get(key);
                this.boot.set(key, had === undefined || had === token ? token : null);
            }
        return own;
    }
    // `values` with their strings given tokens - `own`'s, else the boot files'.
    tokenized(values: [number, QbDataValue][], own: Map<string, string>): [number, QbDataValue][] {
        const token = (type: number, bytes: number[]) => {
            const key = textKey(type, bytes);
            return own.get(key) ?? this.boot.get(key) ?? undefined;
        };
        const walk = (v: QbDataValue): QbDataValue => {
            if (v.type === 3 || v.type === 4) {
                const t = token(v.type, v.bytes);
                return t === undefined ? v : { ...v, token: t };
            }
            if (v.type === 10) return { type: 10, members: v.members.map(([n, m]) => [n, walk(m)]) };
            if (v.type === 12) return { ...v, elements: v.elements.map(walk) };
            return v;
        };
        return values.map(([n, v]) => [n, walk(v)]);
    }
}
const textKey = (type: number, bytes: number[]) => `${type}:${bytes.join(",")}`;

// The trigger data of a game folder's files (game_scripts.ts qbVersions - Data's, the archives', the overlays'): the boot
// files the game loads at start-up - every file outside levels\ (and Anteara Mod's, which replace them only with
// anteara_mod=1), the version it reads (the data overlay's, else an archive's, else Data's; PartyMod's patched one where
// there is one) - and the scripts every level's nodes name by TriggerScript=.
export function gameTriggerData(folder: string): TriggerData {
    const versions = qbVersions(gameRoot(folder));
    const rank = (v: QbVersion) =>
        v.origin.endsWith("+partymod")
            ? 0
            : v.origin === "thug2-decomp-data"
              ? 1
              : v.origin.toLowerCase().endsWith(".prx")
                ? 2
                : 3;
    const boot = new Map<string, QbVersion>();
    const triggers = new Set<number>();
    for (const v of versions) {
        let ir: ModuleIR;
        try {
            ir = readIR(v.bytes, v.name);
        } catch {
            continue; // (a file that doesn't translate: the game reads none of it)
        }
        triggerScripts(ir, triggers);
        const base = v.origin.replace(/\+partymod$/, "");
        if (v.name.startsWith("levels\\") || v.name.startsWith("anteara\\")) continue;
        if (base !== "Data" && base !== "thug2-decomp-data" && !base.toLowerCase().endsWith(".prx")) continue;
        const had = boot.get(v.name);
        if (!had || rank(v) < rank(had)) boot.set(v.name, v);
    }
    const sources: Source[] = [];
    for (const v of boot.values()) {
        try {
            sources.push({ bytes: v.bytes, ir: readIR(v.bytes, v.name) });
        } catch {
            // (as above)
        }
    }
    return new TriggerData(sources, triggers);
}
