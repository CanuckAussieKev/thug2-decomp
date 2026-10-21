// NOT ORIGINAL - checks a converted level's .qb for the script faults that crashed or hung ported levels in THUG2
// (docs/pending/level-crashes-2.md), and the one rewrite that cures the hang:
//
//   - loops that never yield: an endless `while { ... }` whose body has no wait at all (the game hangs in that frame),
//     or whose only wait is Obj_WaitAnimFinished with nothing in the loop starting an animation - that wait ends at
//     once when no animation plays (the anim component's check, called straight after the command by CScript::Update),
//     so the loop spins for ever in one frame: THUG1 Hotter Than Hell's ATV riders (THUGPRO's SE_ATVDriver02 / 04:
//     `while { Obj_WaitAnimFinished }`), the hang in the blender's wait check;
//   - names given to IsAlive / Create / Visible / Invisible (`name=<constant>`) that aren't nodes of the level:
//     CFunc_IsAlive (0x005A5380 -> 0x005A4EF0) and the others look the name up with GetNode, which asserts and returns
//     null - THUG2 crashes on it (THAW Hollywood: IsAlive name=<ObjID> with the skater's id 0 - a parameter, which
//     this can't see: the translator's guards are ObjectExists now);
//   - arrays read by GetArraySize / `<array>[i]` that the file doesn't define (a global of another file: reported to
//     look at - THUG2's own globals are fine; THPS3 Shipyard's shp_window_objects was missing);
//   - a node's animation set (AnimName / Extra_Anims) that the level's load_level_anims doesn't load: the ped plays an
//     animation of a set never loaded - FrameIndex on null, 0x00482BA2 (THUG1 Venice's THUGPRO seagulls).
//
//   node tools/level_port/script_check.ts [--thug2 C:\Games\THUG2] <levels folder | level .qb>...
//
// Prints each finding (level: kind: script / node: detail) and a count per kind; exit code 1 when a level has one.

import * as fs from "node:fs";
import * as path from "node:path";
import { findCaseless, resolveInstall } from "../lib/install.ts";
import { isMain, print, runMain } from "../lib/py.ts";
import * as prx from "./prx.ts";
import { checksum, Name, nameText, QbFile, type QbValue, Struct, tokens } from "./qb.ts";

const EOL = new Set([0x01, 0x02]);
const NAME = 0x16;
const BEGIN = 0x20;
const REPEAT = 0x21;

// Commands that look the name= up as a node and use it without a check (0x005A4EF0 -> 0x005A4DA0, GetNode asserting):
// IsAlive (0x005A3D10), Visible / Invisible (0x005A3B80: the node's Class read), Create (0x005A32A0 -> 0x005A2E50).
// Kill's 0x005A32C0 returns when there's no node; Shatter (0x005A5090) finds the sector, not the node.
export const NODE_LOOKUPS = new Set(["IsAlive", "Create", "Visible", "Invisible"].map(checksum));
// (THUG2's skater sets, loaded by the game itself)
const ALWAYS_LOADED = new Set(["animload_THPS6_human"].map(checksum));

export interface Finding {
    kind:
        | "never yields"
        | "spins on Obj_WaitAnimFinished"
        | "node name missing"
        | "array missing"
        | "anim set not loaded";
    where: string;
    detail: string;
}

type Tok = [number, number, Buffer]; // [offset, token, payload]

// Each script's tokens (from its name to its endscript), by name.
export function scriptTokens(data: Buffer): Map<number, Tok[]> {
    const toks = [...tokens(data, true)];
    const out = new Map<number, Tok[]>();
    for (let i = 0; i + 1 < toks.length; i++) {
        if (toks[i]![1] !== 0x23 || toks[i + 1]![1] !== NAME) continue;
        let k = i + 2;
        while (k < toks.length && toks[k]![1] !== 0x24) k++;
        out.set(toks[i + 1]![2].readUInt32LE(0), toks.slice(i + 2, k));
        i = k;
    }
    return out;
}

const nameOf = (t: Tok): number | null => (t[1] === NAME ? t[2].readUInt32LE(0) : null);

// The endless loops of a script: [index of the begin, index of the repeat] (a repeat with a count isn't endless).
export function endlessLoops(toks: readonly Tok[]): [number, number][] {
    const out: [number, number][] = [];
    const open: number[] = [];
    toks.forEach((t, i) => {
        if (t[1] === BEGIN) open.push(i);
        else if (t[1] === REPEAT && open.length) {
            const begin = open.pop()!;
            const next = toks[i + 1];
            if (next === undefined || EOL.has(next[1])) out.push([begin, i]);
        }
    });
    return out;
}

// How a loop body waits: "none" (no wait-like name and no call to a script of `local` - the level's or THUG2's, which
// may wait), "anim" (its
// only waits are Obj_WaitAnimFinished and nothing in it starts an animation), or "ok".
export function loopWaits(
    body: readonly Tok[],
    local: ReadonlySet<number>,
    names: ReadonlyMap<number, string>,
): string {
    let waits = 0;
    let animWaits = 0;
    let starts = 0;
    let calls = 0;
    for (const t of body) {
        const v = nameOf(t);
        if (v === null) continue;
        const text = (names.get(v) ?? "").toLowerCase();
        if (v === checksum("Obj_WaitAnimFinished")) animWaits += 1;
        else if (text.includes("wait") || v === checksum("Block")) waits += 1;
        if (["obj_playanim", "obj_cycleanim", "playanim", "obj_setanimcyclemode"].includes(text)) starts += 1;
        if (local.has(v)) calls += 1;
        // (an unnamed checksum may be anything: counted as a call that may wait)
        if (!text) calls += 1;
    }
    if (waits || calls) return "ok";
    if (animWaits) return starts ? "ok" : "anim";
    // (THUG2's own scripts called by name may wait: only the loops with no name of a script or command that waits)
    return "none";
}

// The `<command> name=<constant>` statements of the lookups in NODE_LOOKUPS: [command, name].
export function nodeLookups(toks: readonly Tok[]): [number, number][] {
    const out: [number, number][] = [];
    let start = true;
    let command: number | null = null;
    toks.forEach((t, i) => {
        if (EOL.has(t[1])) {
            start = true;
            command = null;
            return;
        }
        const v = nameOf(t);
        if (start) {
            start = false;
            if (v !== null && NODE_LOOKUPS.has(v)) command = v;
            else if (t[1] !== 0x47 && t[1] !== 0x39)
                command = null; // (if / not: the command follows)
            else start = true;
            return;
        }
        if (command === null || v !== checksum("name")) return;
        const value = toks[i + 2];
        if (toks[i + 1]?.[1] === 0x07 && value !== undefined && value[1] === NAME && toks[i - 1]?.[1] !== 0x2d) {
            out.push([command, value[2].readUInt32LE(0)]);
        }
    });
    return out;
}

// The arrays a script reads by name: GetArraySize <name> and `(<name> [<i>])`.
export function arraysRead(toks: readonly Tok[]): number[] {
    const out: number[] = [];
    toks.forEach((t, i) => {
        const v = nameOf(t);
        if (v === null) return;
        if (v === checksum("GetArraySize")) {
            const next = toks[i + 1];
            if (next?.[1] === NAME && toks[i]![1] !== 0x2d) out.push(next[2].readUInt32LE(0));
        } else if (toks[i + 1]?.[1] === 0x05 && toks[i - 1]?.[1] === 0x0e) {
            // (`(<name> [<i>])`: an element of a global array - not a command's [ ] argument)
            out.push(v);
        }
    });
    return out;
}

function nodeArrayOf(q: QbFile): Struct[] {
    for (const [k, v] of q.globals) {
        if ((q.names.get(k.value) ?? "").toLowerCase().endsWith("_nodearray") && Array.isArray(v))
            return v.filter((n): n is Struct => n instanceof Struct);
    }
    return [];
}

// A node's own fields with its includes (bare names of global structures) put in place.
function resolved(node: Struct, globals: ReadonlyMap<number, QbValue>): Struct {
    const entries: [Name | null, QbValue][] = [];
    for (const [k, v] of node.entries) {
        const g = k === null && v instanceof Name ? globals.get(v.value) : undefined;
        if (g instanceof Struct) entries.push(...g.entries);
        else entries.push([k, v]);
    }
    return new Struct(entries);
}

// THUG2's own scripts and globals (Data\pre\qb_scripts.prx): its scripts may wait (Obj_CycleAnim...), its globals
// are there for every level.
export interface Thug2Names {
    scripts: Set<number>;
    globals: Set<number>;
}
export function thug2Names(game: string): Thug2Names {
    const out: Thug2Names = { scripts: new Set(), globals: new Set() };
    const prxFile = findCaseless(resolveInstall(game, "thug2"), "Data", "pre", "qb_scripts.prx");
    if (prxFile === null) throw new Error(`no Data\\pre\\qb_scripts.prx in ${game}`);
    for (const [file, data] of prx.entries(prxFile)) {
        if (!file.toLowerCase().endsWith(".qb")) continue;
        for (const k of scriptTokens(data).keys()) out.scripts.add(k);
        try {
            for (const [k] of new QbFile(data, file, true).globals) out.globals.add(k.value);
        } catch {
            // (a file whose globals the reader can't take)
        }
    }
    return out;
}

// Every finding of one level's .qb (`thug2`: THUG2's own scripts and globals).
export function checkLevel(
    data: Buffer,
    level: string,
    thug2: Thug2Names = { scripts: new Set(), globals: new Set() },
): Finding[] {
    const known = thug2.globals;
    const q = new QbFile(data, level, true);
    const names = q.names;
    const text = (v: number): string => nameText(v, names);
    const globals = new Map(q.globals.map(([k, v]) => [k.value, v] as const));
    const nodes = nodeArrayOf(q).map((n) => resolved(n, globals));
    const nodeNames = new Set<number>();
    for (const n of nodes) {
        const v = n.get("Name") ?? n.get("name");
        if (v instanceof Name) nodeNames.add(v.value);
    }
    const scripts = scriptTokens(data);
    const local = new Set([...scripts.keys(), ...thug2.scripts]);
    const out: Finding[] = [];
    for (const [s, toks] of scripts) {
        for (const [b, r] of endlessLoops(toks)) {
            const how = loopWaits(toks.slice(b + 1, r), local, names);
            if (how === "none")
                out.push({ kind: "never yields", where: text(s), detail: "an endless loop with no wait" });
            if (how === "anim")
                out.push({
                    kind: "spins on Obj_WaitAnimFinished",
                    where: text(s),
                    detail: "its only wait is Obj_WaitAnimFinished and it starts no animation",
                });
        }
        for (const [command, name] of nodeLookups(toks)) {
            if (!nodeNames.has(name))
                out.push({ kind: "node name missing", where: text(s), detail: `${text(command)} name=${text(name)}` });
        }
        for (const a of new Set(arraysRead(toks))) {
            if (!globals.has(a) && !known.has(a)) out.push({ kind: "array missing", where: text(s), detail: text(a) });
        }
    }
    // the animation sets the nodes name, against load_level_anims' calls
    const loads = new Set<number>(ALWAYS_LOADED);
    for (const t of scripts.get(checksum("load_level_anims")) ?? []) {
        const v = nameOf(t);
        if (v !== null) loads.add(v);
    }
    for (const n of nodes) {
        for (const key of ["AnimName", "Extra_Anims"]) {
            const set = n.get(key);
            if (set instanceof Name && !loads.has(set.value)) {
                const nodeName = n.get("Name") ?? n.get("name");
                out.push({
                    kind: "anim set not loaded",
                    where: nodeName instanceof Name ? text(nodeName.value) : "?",
                    detail: `${key}=${text(set.value)}`,
                });
            }
        }
    }
    return out;
}

// The rewrite that cures "spins on Obj_WaitAnimFinished" for the plainest form - a loop whose whole body is
// Obj_WaitAnimFinished (`while { Obj_WaitAnimFinished }`, an idle that waits for ever): the wait made a bare `Wait`
// (CFunc_Wait without a count: a 0 ms timer, which yields the frame - WAIT_TYPE_TIMER's first update only zeroes it).
// The same 5 bytes, so no jump moves. Returns the file and how many were changed.
export function yieldingIdleLoops(data: Buffer): [Buffer, number] {
    const out = Buffer.from(data);
    let changed = 0;
    for (const toks of scriptTokens(data).values()) {
        for (const [b, r] of endlessLoops(toks)) {
            const body = toks.slice(b + 1, r).filter((t) => !EOL.has(t[1]));
            if (body.length === 1 && nameOf(body[0]!) === checksum("Obj_WaitAnimFinished")) {
                out.writeUInt32LE(checksum("Wait"), body[0]![0] + 1);
                changed += 1;
            }
        }
    }
    return [out, changed];
}

function levelFiles(arg: string): [string, string][] {
    if (fs.statSync(arg).isFile()) return [[path.basename(arg, path.extname(arg)), arg]];
    const out: [string, string][] = [];
    for (const level of fs.readdirSync(arg).sort()) {
        const file = path.join(arg, level, `${level}.qb`);
        if (fs.existsSync(file)) out.push([level, file]);
    }
    return out;
}

function main(argv: string[]): number {
    let game = "C:\\Games\\THUG2";
    const at = argv.indexOf("--thug2");
    if (at >= 0) {
        game = argv[at + 1] ?? game;
        argv = argv.filter((_, i) => i !== at && i !== at + 1);
    }
    if (!argv.length) {
        print("usage: node tools/level_port/script_check.ts [--thug2 <THUG2>] <levels folder | level .qb>...");
        return 2;
    }
    const thug2 = thug2Names(game);
    const counts = new Map<string, number>();
    let bad = 0;
    for (const arg of argv) {
        for (const [level, file] of levelFiles(arg)) {
            let found: Finding[];
            try {
                found = checkLevel(fs.readFileSync(file), level, thug2);
            } catch (e) {
                print(`${level}: unreadable (${(e as Error).message})`);
                bad += 1;
                continue;
            }
            for (const f of found) {
                print(`${level}: ${f.kind}: ${f.where}: ${f.detail}`);
                counts.set(f.kind, (counts.get(f.kind) ?? 0) + 1);
            }
            if (found.length) bad += 1;
        }
    }
    print(counts.size ? [...counts].map(([k, v]) => `${v} ${k}`).join(", ") : "no findings");
    return bad ? 1 : 0;
}

if (isMain(import.meta.url)) await runMain(() => main(process.argv.slice(2)));
