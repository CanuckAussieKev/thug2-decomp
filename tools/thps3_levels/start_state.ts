// NOT ORIGINAL - what a THUG2-format level (ours or THUGPRO's) has at the start of offline free skate: its node
// array's CreatedAtStart nodes, then its start scripts followed statically (setup / startup scripts, the trigger
// scripts of the objects made at the start) - Create / Kill / Visible /
// Invisible / Shatter (and THUGPRO's THUGPRO_ForceCreate / THUGPRO_SafeKill) by name or prefix, called / spawned /
// GoTo'd scripts inlined. Conditions are decided as offline free skate decides them: InNetGame / InMultiplayerGame,
// IsCareerMode, GetGoal, InSplitScreenGame false; GetFlag from the flags set so far; IsAlive from the state so far;
// anything else unknown - both branches followed and what they do marked unsure. Waits are ignored (statements run in
// the order written), random and switch branches are all followed (unsure).
//
// Used by thugpro_compare.ts (--start-state) to compare our THPS3 ports' free-skate world with THUGPRO's
// (docs/pending/thps3-free-skate-thugpro.md).

import * as qb from "../level_port/qb.ts";
import { Name, type QbValue, Struct } from "../level_port/qb.ts";
import { decodeLatin1 } from "../lib/py.ts";
import { unpackNumbers } from "../lib/struct.ts";

export type Shown = "on" | "off" | "hidden";

export interface NodeStart {
    name: number;
    cls: string; // the node's class (lower case)
    initial: Shown; // from its flags: CreatedAtStart -> on
    state: Shown; // after the start scripts
    sure: boolean; // false: an unsure branch (an unknown condition, a random, a switch) touched it last
    by: string; // the script and command that set the state ("" when untouched)
}

const c = qb.checksum;
const CREATE = new Set(["Create", "THUGPRO_ForceCreate"].map(c));
const KILL = new Set(["Kill", "THUGPRO_SafeKill", "Shatter"].map(c));
const VISIBLE = c("Visible");
const INVISIBLE = c("Invisible");
const FALSE_CONDITIONS = new Set(
    ["InNetGame", "InMultiplayerGame", "IsCareerMode", "GetGoal", "InSplitScreenGame", "thps3_port_never"].map(c),
);
const CALLS = new Set(["SpawnScript", "GoTo", "GotoPreserveParams", "RunScriptOnObject"].map(c));
const ENDS = new Set(["Die", "return"].map(c));
const OBJECT_CLASSES = new Set(["gameobject", "levelobject", "vehicle", "pedestrian"]);
const MAX_DEPTH = 40;

const u32 = (b: Buffer): number => unpackNumbers("<I", b)[0]!;

export interface LevelScripts {
    nodes: Struct[];
    scripts: Map<number, qb.Statement[]>;
    names: Map<number, string>; // checksum -> text (node names for prefix= matching)
}

// The node array (the largest array of structures named *_NodeArray) and every script of THUG2-format .qb files.
export function readLevel(files: readonly Buffer[], names: ReadonlyMap<number, string> = new Map()): LevelScripts {
    const out: LevelScripts = { nodes: [], scripts: new Map(), names: new Map(names) };
    for (const data of files) {
        const q = new qb.QbFile(data, "qb", true);
        for (const [k, v] of q.names) out.names.set(k, v);
        for (const [k, v] of q.scripts) out.scripts.set(k, v);
        for (const [key, value] of q.globals) {
            if (!Array.isArray(value)) continue;
            const text = (q.names.get(key.value) ?? "").toLowerCase();
            const structs = value.filter((v): v is Struct => v instanceof Struct);
            if (text.endsWith("_nodearray") && structs.length > out.nodes.length) out.nodes = structs;
        }
    }
    return out;
}

function nameValue(v: QbValue | undefined): number | null {
    return v instanceof Name ? v.value : null;
}

// The scripts a level runs at its start besides its setup / startup scripts: the trigger scripts of the objects made
// at the start (an object runs its script when it is made). `player1`: also the first Player1 restart's trigger script -
// THUG2's free skate starts with the script command ResetSkaters (0x005AC3B0 -> 0x00566980: each local skater skipped
// to the restart 0x00566100 finds, its Player1 restart offline), and SkipToRestart (0x00583DA0) runs the restart
// node's TriggerScript: THUGPRO's levels keep it on the node (Foundry's FounTRG_Restart_1PScript); ours have it run
// from the setup script instead.
export function nodeRoots(nodes: readonly Struct[], player1 = true): number[] {
    const out: number[] = [];
    let p1Done = !player1;
    for (const n of nodes) {
        const script = nameValue(n.get("TriggerScript"));
        if (script === null) continue;
        const cls = nameValue(n.get("Class"));
        if (cls === c("Restart")) {
            if (p1Done) continue;
            const types = n.get("restart_types");
            const isP1 =
                nameValue(n.get("Type")) === c("Player1") ||
                (Array.isArray(types) && types.some((t) => nameValue(t) === c("Player1")));
            if (isP1) {
                p1Done = true;
                out.push(script);
            }
        } else if (n.hasFlag("CreatedAtStart") && cls !== null) {
            for (const k of OBJECT_CLASSES) if (cls === c(k)) out.push(script);
        }
    }
    return out;
}

interface Op {
    command: number;
    name: number | null;
    prefix: string | null;
}

// `<command> Name=X` / `prefix="X"` / `nodename=X` (the first of them).
function opOf(line: qb.Statement): Op | null {
    if (!line.length || line[0]![0] !== 0x16) return null;
    const command = u32(line[0]![1]);
    for (let i = 1; i + 2 < line.length; i++) {
        if (line[i]![0] !== 0x16 || line[i + 1]![0] !== 0x07) continue;
        const key = u32(line[i]![1]);
        const [vt, vp] = line[i + 2]!;
        if ((key === c("Name") || key === c("nodename")) && vt === 0x16)
            return { command, name: u32(vp), prefix: null };
        if (key === c("prefix") && (vt === 0x1b || vt === 0x1c))
            return { command, name: null, prefix: decodeLatin1(qb.rstripNul(vp)).toLowerCase() };
    }
    return { command, name: null, prefix: null };
}

export function startState(level: LevelScripts, roots: readonly number[]): Map<number, NodeStart> {
    const state = new Map<number, NodeStart>();
    const byPrefix: [string, number][] = [];
    for (const n of level.nodes) {
        const name = nameValue(n.get("Name"));
        if (name === null || state.has(name)) continue;
        const cls = level.names.get(nameValue(n.get("Class")) ?? 0) ?? "-";
        const initial: Shown = n.hasFlag("CreatedAtStart") ? "on" : "off";
        state.set(name, { name, cls: cls.toLowerCase(), initial, state: initial, sure: true, by: "" });
        const text = level.names.get(name);
        if (text !== undefined) byPrefix.push([text.toLowerCase(), name]);
    }
    const flags = new Set<number>();
    const text = (k: number): string => level.names.get(k) ?? `#${k.toString(16)}`;
    const apply = (op: Op, sure: boolean, where: string): void => {
        let to: Shown | null = null;
        if (CREATE.has(op.command)) to = "on";
        else if (KILL.has(op.command)) to = "off";
        else if (op.command === VISIBLE) to = "on";
        else if (op.command === INVISIBLE) to = "hidden";
        if (to === null) return;
        const targets =
            op.name !== null
                ? [op.name]
                : op.prefix !== null
                  ? byPrefix.filter(([t]) => t.startsWith(op.prefix!)).map(([, k]) => k)
                  : [];
        for (const k of targets) {
            const s = state.get(k);
            if (s === undefined) continue;
            // (Visible / Invisible act on a live object only; Create makes a dead one live and shown)
            if ((op.command === VISIBLE || op.command === INVISIBLE) && s.state === "off") continue;
            s.state = to;
            s.sure = sure;
            s.by = `${where}: ${text(op.command)}`;
        }
    };
    const condition = (line: qb.Statement): boolean | null => {
        let i = 1; // (after the if token - THUG2's 0x47 carries its jump in its payload)
        let negate = false;
        while (i < line.length && line[i]![0] === 0x39) {
            negate = !negate;
            i++;
        }
        if (i >= line.length || line[i]![0] !== 0x16) return null;
        const command = u32(line[i]![1]);
        let value: boolean | null = null;
        if (FALSE_CONDITIONS.has(command)) value = false;
        else if (command === c("GotParam")) value = false;
        else if (command === c("GetFlag")) {
            const op = line.findIndex(([t, p], k) => k > i && t === 0x16 && u32(p) === c("flag"));
            if (op > 0 && line[op + 2]?.[0] === 0x16) value = flags.has(u32(line[op + 2]![1]));
        } else if (command === c("IsAlive")) {
            const op = opOf([[0x16, line[i]![1]], ...line.slice(i + 1)]);
            const s = op?.name != null ? state.get(op.name) : undefined;
            if (s?.sure) value = s.state !== "off";
        }
        if (value === null) return null;
        // (anything after the command other than its arguments - and / or - leaves it unknown)
        if (line.slice(i + 1).some(([t]) => t === 0x32 || t === 0x33)) return null;
        return negate ? !value : value;
    };
    const run = (script: number, depth: number, stack: Set<number>, sureIn: boolean): void => {
        const lines = level.scripts.get(script);
        if (lines === undefined || depth > MAX_DEPTH || stack.has(script)) return;
        stack.add(script);
        const frames: { value: boolean | null; inElse: boolean; kind: "if" | "other" }[] = [];
        const where = text(script);
        for (const line of lines) {
            const head = line[0]![0];
            if (head === 0x47 || head === 0x25) {
                frames.push({ value: condition(line), inElse: false, kind: "if" });
                continue;
            }
            if (head === 0x48 || head === 0x26) {
                const f = frames[frames.length - 1];
                if (f) f.inElse = true;
                continue;
            }
            if (head === 0x28) {
                frames.pop();
                continue;
            }
            if (head === 0x3c) {
                frames.push({ value: null, inElse: false, kind: "other" }); // switch: every case, unsure
                continue;
            }
            if (head === 0x3d) {
                frames.pop();
                continue;
            }
            if (frames.some((f) => f.kind === "if" && f.value !== null && f.value === f.inElse)) continue;
            const sure = sureIn && frames.every((f) => f.value !== null);
            const randoms = line.some(([t]) => t === 0x2f || t === 0x37 || t === 0x40 || t === 0x41);
            const op = opOf(line.filter(([t]) => t !== 0x2f && t !== 0x37 && t !== 0x40 && t !== 0x41));
            if (op === null) continue;
            const lineSure = sure && !randoms;
            if (ENDS.has(op.command) && lineSure) break;
            if (op.command === c("SetFlag") || op.command === c("UnSetFlag")) {
                const f = line.findIndex(([t, p], k) => k > 0 && t === 0x16 && u32(p) === c("flag"));
                if (f > 0 && line[f + 2]?.[0] === 0x16 && lineSure) {
                    if (op.command === c("SetFlag")) flags.add(u32(line[f + 2]![1]));
                    else flags.delete(u32(line[f + 2]![1]));
                }
                continue;
            }
            if (CALLS.has(op.command) && line.length > 1 && line[1]![0] === 0x16) {
                run(u32(line[1]![1]), depth + 1, stack, lineSure);
                if (op.command !== c("SpawnScript") && op.command !== c("RunScriptOnObject") && lineSure) break;
                continue;
            }
            if (level.scripts.has(op.command)) {
                run(op.command, depth + 1, stack, lineSure);
                continue;
            }
            apply(op, lineSure, where);
        }
        stack.delete(script);
    };
    for (const root of roots) run(root, 0, new Set(), true);
    return state;
}

// Node classes that are the level's world (geometry, rails, their collision) - the rest (peds, vehicles, game
// objects, particles, sounds, proximity nodes...) is its life.
export const WORLD_CLASSES: ReadonlySet<string> = new Set(["levelgeometry", "environmentobject", "railnode"]);

export interface StartDiff {
    name: string;
    cls: string; // ours
    theirCls: string;
    ours: string;
    theirs: string;
    world: boolean;
}

const shown = (s: NodeStart): string => `${s.state}${s.sure ? "" : "?"}${s.by ? ` (${s.by})` : ""}`;

// The nodes both levels have whose start state differs. `sectors`: the scene sectors of both sides (checksums) - a
// geometry node neither side has a sector for (a light, a particle helper, an empty box) draws nothing and collides with
// nothing either way: left out.
export function startDiffs(
    ours: ReadonlyMap<number, NodeStart>,
    theirs: ReadonlyMap<number, NodeStart>,
    names: ReadonlyMap<number, string>,
    sectors: ReadonlySet<number> | null = null,
): StartDiff[] {
    const out: StartDiff[] = [];
    for (const [k, o] of ours) {
        const t = theirs.get(k);
        if (t === undefined || o.state === t.state) continue;
        const geometry = (cls: string): boolean => cls === "levelgeometry" || cls === "environmentobject";
        if (sectors !== null && geometry(o.cls) && !sectors.has(k) && t.cls !== "railnode") continue;
        out.push({
            name: names.get(k) ?? `#${k.toString(16)}`,
            cls: o.cls,
            theirCls: t.cls,
            ours: shown(o),
            theirs: shown(t),
            // (a node THUGPRO made a particle / generic node is its own emitter, not the layout)
            world: WORLD_CLASSES.has(o.cls) && WORLD_CLASSES.has(t.cls),
        });
    }
    return out.sort((a, b) => (a.name < b.name ? -1 : a.name > b.name ? 1 : 0));
}
