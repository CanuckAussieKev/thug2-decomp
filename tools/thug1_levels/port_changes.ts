// NOT ORIGINAL - our own changes to THUG1's levels (the user's decision 2026-10-20): what this converter does to a
// THUG1 level that comes neither from THUG1 nor from THUGPRO, listed as data in the `port_changes` section of
// tools/compat/thugpro/thug1_levels.json (README.md there: "Our own changes (port_changes)", PC-THUG1-1 ...), read through
// tools/compat/compat.ts portChanges whatever --compat says. Each entry (compat.Entry): id (PC-THUG1-n), op ("add" /
// "set" / "remove"), rule (a short name), target ({levels, what}: what it applies to - a rule's description when it
// applies to every node / script of a kind), why (one line), required (true: needed for the level to load or to avoid
// a crash - applied even with --no-port-changes).
//
// The code that makes each change stays where it is (nodes.ts, thug1_port.ts, thug1_levels.ts); it asks `on(id)`
// whether to run. An entry taken out of the data switches its change off - unless it's required, which is an error (the
// register and the code would disagree). --no-port-changes switches off every entry that isn't required: THUG1's own data
// as far as THUG2 can load it.

import * as compat from "../compat/compat.ts";
import { FormatError } from "../level_port/binio.ts";

export type Op = compat.Op;

export interface PortChange {
    id: string;
    op: Op;
    target: string;
    why: string;
    required: boolean;
    data: compat.Entry; // (the whole entry: a change's own data besides the common fields - overrides, won_goals...)
}

// The changes this converter makes (id -> what the code does), each with its entry in the data.
export const IMPLEMENTED: ReadonlyMap<string, string> = new Map([
    ["PC-THUG1-1", "kill zones name a restart (nodes.addKillRestarts)"],
    ["PC-THUG1-2", "network spawns for levels without any (nodes.netRestarts)"],
    ["PC-THUG1-3", "the first Player1 restart moved to the array's start (nodes.convertNodes player1First)"],
    ["PC-THUG1-4", "DuplicateTrigger scripts resolved to the linked node's (thug1_port.resolveDuplicateTriggers)"],
    [
        "PC-THUG1-5",
        "restarts over nothing or over a kill zone: no network spawn, kills sent elsewhere (nodes.groundRestarts)",
    ],
    ["PC-THUG1-6", "animal peds left out (thug1_port.NodePass.pedestrian)"],
    ["PC-THUG1-7", "goal nodes left out: GenericNodes other than crowns (nodes.convertNodes)"],
    ["PC-THUG1-8", "objects whose model or skeleton THUG2 hasn't left out (thug1_port.NodePass)"],
    [
        "PC-THUG1-9",
        "particles whose texture THUG2 hasn't, moving objects without a sector left out (thug1_port.NodePass)",
    ],
    ["PC-THUG1-10", "peds given THUG2's skeleton, animations and a THUG2 profile (thug1_port.NodePass.pedestrian)"],
    [
        "PC-THUG1-11",
        "keys THUG2 hasn't the values of taken off: SoundType, AnimName / Extra_Anims (thug1_port.NodePass)",
    ],
    ["PC-THUG1-12", "TrickObject / Cluster off climbing nodes and geometry without a scene sector (thug1_port, nodes)"],
    ["PC-THUG1-13", "rail terrains THUG2 hasn't made THUG2's (nodes.fixTerrains)"],
    ["PC-THUG1-14", "path followers, peds and cars kept out of network games (nodes.absentInNetGames)"],
    ["PC-THUG1-15", "script statements THUG2 can't run left out, trigger scripts not brought taken off (script_port)"],
    ["PC-THUG1-16", "gap scripts given explicitly; the models' gap scripts defined empty (thug1_port)"],
    ["PC-THUG1-17", "the node array's shared parts made global structures (thug1_port.compressIncludes)"],
    ["PC-THUG1-18", "restart_types from a restart's Type; kill scripts as THUG2's SK3_KillSkater (nodes)"],
    ["PC-THUG1-19", "level names thug1_<code> where THUG2 has the name (thug1_levels.levelName)"],
    [
        "PC-THUG1-20",
        "collision face flags 0x2000-0x8000 dropped, skinned sectors left out of the scene (thug1.ts, geometry)",
    ],
    [
        "PC-THUG1-21",
        "VC <-> SJ as THUG1 has it: THUGPRO's transition arrows left out, VC's SJ kill zone kept (setCompat, overrides)",
    ],
    [
        "PC-THUG1-22",
        "THUG1's drivable cars: its driving scripts installed, their goals counted as won (drivingScripts, thug1_port)",
    ],
]);

export class PortChanges {
    readonly entries = new Map<string, PortChange>();
    readonly enabled: boolean;

    // `list`: compat.portChanges's entries (each {id, op, rule, target: {levels, what}, why, required}).
    constructor(list: readonly compat.Entry[], enabled = true) {
        this.enabled = enabled;
        for (const e of list) {
            if (!compat.OPS.includes(e.op)) throw new FormatError(`port_changes ${e.id}: op ${e.op}`);
            if (this.entries.has(e.id)) throw new FormatError(`port_changes: ${e.id} twice`);
            const what = e.target.what;
            this.entries.set(e.id, {
                id: e.id,
                op: e.op,
                target: typeof what === "string" ? what : "",
                why: e.why,
                required: e.required === true,
                data: e,
            });
        }
        for (const [id, what] of IMPLEMENTED) {
            const e = this.entries.get(id);
            if (e === undefined) continue;
            if (!e.target || !e.why) throw new FormatError(`port_changes ${id} (${what}): no target / why`);
        }
        for (const id of this.entries.keys())
            if (!IMPLEMENTED.has(id)) throw new FormatError(`port_changes: ${id} isn't one this converter makes`);
    }

    // Whether the change runs: in the data, and either required or port changes on. A required change taken out of
    // the data is an error.
    on(id: string): boolean {
        if (!IMPLEMENTED.has(id)) throw new FormatError(`port_changes: no change ${id} in the code`);
        const e = this.entries.get(id);
        if (e === undefined) return false;
        return e.required || this.enabled;
    }

    // A required change: always made (an error when the data hasn't it).
    required(id: string): void {
        const e = this.entries.get(id);
        if (e === undefined || !e.required) {
            throw new FormatError(
                `port_changes ${id} is required for THUG2 to load the level: missing or not required`,
            );
        }
    }
}

// The THUGPRO profile entries the changes that are on take back (an entry's `overrides`: {thugpro_new: [{level,
// name}], created_at_start: [{level, name}]} - a thugpro_new entry's target, a TP-34 on / off node), where our own
// change keeps THUG1's own data instead.
export interface Override {
    level: string;
    name: string;
}

export function overrides(changes: PortChanges, section: "thugpro_new" | "created_at_start"): Override[] {
    const out: Override[] = [];
    for (const [id, e] of changes.entries) {
        if (!IMPLEMENTED.has(id) || !changes.on(id)) continue;
        const list = (e.data.overrides as Record<string, unknown> | undefined)?.[section];
        if (list === undefined) continue;
        if (!Array.isArray(list)) throw new FormatError(`port_changes ${id}: overrides.${section} is not a list`);
        for (const o of list as Override[]) {
            if (typeof o.level !== "string" || typeof o.name !== "string") {
                throw new FormatError(
                    `port_changes ${id}: overrides.${section} ${JSON.stringify(o)}: needs level, name`,
                );
            }
            out.push({ level: o.level, name: o.name });
        }
    }
    return out;
}

// PC-THUG1-22's goals counted as won, by level code in lower case (names as THUG1's scripts give them); none when off.
export function wonGoals(changes: PortChanges): Map<string, string[]> {
    const out = new Map<string, string[]>();
    if (!changes.on("PC-THUG1-22")) return out;
    const won = (changes.entries.get("PC-THUG1-22")!.data.won_goals ?? {}) as Record<string, unknown>;
    for (const [code, list] of Object.entries(won)) {
        if (!Array.isArray(list) || !list.every((g) => typeof g === "string"))
            throw new FormatError(`port_changes PC-THUG1-22: won_goals.${code} is not a list of names`);
        out.set(code.toLowerCase(), list as string[]);
    }
    return out;
}

// PC-THUG1-22's THUG1 script files installed as THUG2's (their names in THUG1's qb.pre - THUG2's own are empty); none
// when off.
export function drivingFiles(changes: PortChanges): string[] {
    if (!changes.on("PC-THUG1-22")) return [];
    const files = changes.entries.get("PC-THUG1-22")!.data.scripts ?? [];
    if (!Array.isArray(files) || !files.every((f) => typeof f === "string"))
        throw new FormatError("port_changes PC-THUG1-22: scripts is not a list of file names");
    return files as string[];
}

export function load(enabled = true): PortChanges {
    return new PortChanges(compat.portChanges("thug1_levels"), enabled);
}
