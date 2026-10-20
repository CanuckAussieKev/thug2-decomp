// NOT ORIGINAL - compares our converted THPS3 levels with THUGPRO's own ports of them, read from the user's THUGPRO
// install (read only: <thugpro>\data\pre\<level>_scripts.prx and <level>col.prx), for online play between the two
// (docs/pending/thps3-thugpro.md). The THAW twin is tools/thaw_levels/thugpro_compare.ts; its pieces are reused here.
//
// What has to agree for a THUGPRO player and one of ours to share a level (see that tool's header): the load script's
// name (0x46 / 0x33 carry its checksum only), the world frame (0x23 skater updates carry world positions), the King of
// the Hill crowns in the same order and places, the game-mode nodes under the names THUG2's scripts look for. Also
// reported: the multiplayer restarts (which only one side has), the node classes, the nodes with trigger scripts only
// one side has (what THUGPRO brings that we don't, and the other way round), the scripts, the collision faces that
// can't be found through their BSP trees.
//
// THUGPRO doesn't port THPS3's Canada, Airport and Los Angeles: it plays THUG2's own classic versions (CA, AP, LA -
// THUG2's Levels.qb has game=THPS3 on them). Those three are compared with THUG2's own (its Data\pre) for information.
//
//   node tools/thps3_levels/thugpro_compare.ts [levels...] [--thug2 C:\Games\THUG2-1] [--thugpro <dir>]
//        [--staging <folder>] [--json out] [--details]
//   node tools/thps3_levels/thugpro_compare.ts --extract-thugpro tools/compat/thugpro/thps3_levels.json [--thps3 <dir>]
//        (the compatibility profile's THPS3 data, TP-10 .. TP-19: see extractThugpro, nodeEdits, collisionEdits)

import * as fs from "node:fs";
import { parseArgs } from "node:util";
import * as colCheck from "../level_port/col_check.ts";
import * as portedLevels from "../level_port/ported_levels.ts";
import * as prx from "../level_port/prx.ts";
import * as qb from "../level_port/qb.ts";
import { Name, type QbValue, Struct, Vector } from "../level_port/qb.ts";
import * as scriptPort from "../level_port/script_port.ts";
import * as thug2Col from "../level_port/thug2_col.ts";
import * as thug2Scene from "../level_port/thug2_scene.ts";
import {
    Float,
    formatFixed,
    hex8,
    isMain,
    print,
    pyExit,
    pyJoin,
    pyJsonDumps,
    pyRound,
    runMain,
    writeTextPy,
} from "../lib/py.ts";
import { findPath } from "../thaw_levels/thaw_levels.ts";
import {
    CTF_NAMES,
    isCrown,
    isMpRestart,
    isName,
    maxDiff,
    nameOf,
    nodeArray,
    position,
    sameSequence,
    sequence,
    TOLERANCE,
} from "../thaw_levels/thugpro_compare.ts";
import * as thps3 from "./thps3.ts";
import { findFolder, flagTeams, LEVELS, levelFiles, STAGING, scriptNames } from "./thps3_levels.ts";
import { REGISTER_ID } from "./thugpro_new.ts";
import { extractNew } from "./thugpro_new_extract.ts";

const compatChecksum = (text: string): number =>
    text.startsWith("#") ? Number.parseInt(text.slice(1), 16) >>> 0 : qb.checksum(text);

const DEFAULT_THUGPRO = pyJoin(process.env.LOCALAPPDATA ?? "", "THUG Pro");
// THPS3 levels THUGPRO leaves to THUG2's own classic remakes (compared with THUG2's files instead).
const THUG2_OWN: ReadonlyMap<string, string> = new Map([
    ["can", "CA"],
    ["ap", "AP"],
    ["la", "LA"],
]);

// THPS3 levels THUGPRO doesn't port (its Levels.qb, 2026-10-20): the tutorials, the skate shop, the park editor's sets.
const NO_COUNTERPART: ReadonlySet<string> = new Set(["tut", "skateshop", "sk3ed_bch", "sk3ed_indr", "sk3ed_schl"]);

export type Result = Record<string, unknown> & { folder: string; level: string };

interface Side {
    array: QbValue[];
    names: Map<number, string>;
    scripts: Set<number>;
}

function readSide(files: [Buffer, string][]): Side {
    let array: QbValue[] = [];
    const names = new Map<number, string>();
    const scripts = new Set<number>();
    for (const [data, label] of files) {
        const [nodes, n] = nodeArray(data, label);
        if (nodes.length > array.length) array = nodes;
        for (const [k, v] of n) names.set(k, v);
        for (const k of new qb.QbFile(data, label, true).scripts.keys()) scripts.add(k);
    }
    return { array, names, scripts };
}

const structs = (array: readonly QbValue[]): Struct[] => array.filter((n): n is Struct => n instanceof Struct);

function text(value: QbValue | undefined, names: ReadonlyMap<number, string>): string {
    return value instanceof Name ? (names.get(value.value) ?? hex8(value.value)) : "-";
}

function classCounts(array: readonly QbValue[], names: ReadonlyMap<number, string>): Map<string, number> {
    const out = new Map<string, number>();
    for (const n of structs(array)) {
        const key = text(n.get("Class"), names).toLowerCase();
        out.set(key, (out.get(key) ?? 0) + 1);
    }
    return out;
}

// Node name -> trigger script name, for the nodes with one.
function triggers(array: readonly QbValue[], names: ReadonlyMap<number, string>): Map<number, string> {
    const out = new Map<number, string>();
    for (const n of structs(array)) {
        const nm = nameOf(n);
        const t = n.get("TriggerScript");
        if (nm !== null && t instanceof Name) out.set(nm, text(t, names));
    }
    return out;
}

function mpRestartNames(array: readonly QbValue[]): Set<number> {
    return new Set(
        structs(array)
            .filter(isMpRestart)
            .map(nameOf)
            .filter((n): n is number => n !== null),
    );
}

const nameList = (keys: Iterable<number>, names: ReadonlyMap<number, string>): string[] =>
    [...keys].map((k) => names.get(k) ?? hex8(k)).sort();

export function compare(entry: portedLevels.LevelEntry, oursDir: string, thugpro: string, thug2: string): Result {
    const folder = String(entry.folder ?? entry.level);
    const mine = entry.level;
    const own = THUG2_OWN.get(folder.toLowerCase());
    const key = own ?? folder.toLowerCase();
    const out: Result = {
        folder,
        level: mine,
        reference: own ? `THUG2's own ${own}` : `THUGPRO ${key}`,
        // (a checksum is case-blind: Load_Foun = load_foun)
        same_load_script: qb.checksum(`Load_${mine}`) === qb.checksum(`Load_${key}`),
    };
    if (!own && NO_COUNTERPART.has(folder.toLowerCase())) {
        // (THUGPRO's "skateshop" is THUG1's, its front end)
        out.error = "THUGPRO has no port of it";
        return out;
    }
    const oursQb = pyJoin(oursDir, mine, `${mine}.qb`);
    if (!fs.existsSync(oursQb)) {
        out.error = `not converted (${oursQb})`;
        return out;
    }
    const scriptsPrx = own
        ? findPath(thug2, "Data", "pre", `${key}_scripts.prx`)
        : findPath(thugpro, "data", "pre", `${key}_scripts.prx`);
    if (!scriptsPrx) {
        out.error = `no ${key}_scripts.prx (${own ? "THUG2" : "THUGPRO"})`;
        return out;
    }
    const theirFiles: [Buffer, string][] = [];
    for (const [name, data] of prx.entries(scriptsPrx, (n) => n.toLowerCase().endsWith(".qb"))) {
        const low = name.toLowerCase();
        if (low === `levels\\${key}\\${key}.qb`.toLowerCase() || low.endsWith("_scripts.qb"))
            theirFiles.push([data, name]);
    }
    const ours = readSide([[fs.readFileSync(oursQb), oursQb]]);
    const theirs = readSide(theirFiles);
    const names = new Map([...theirs.names, ...ours.names]);

    // positions by name
    const byName = new Map<number, readonly number[]>();
    for (const n of structs(theirs.array)) {
        const nm = nameOf(n);
        const p = position(n);
        if (nm !== null && p !== null && !byName.has(nm)) byName.set(nm, p);
    }
    let shared = 0;
    let same = 0;
    let worst: [number, string | null] = [0.0, null];
    for (const n of structs(ours.array)) {
        const p = position(n);
        const nm = nameOf(n);
        if (p === null || nm === null || !byName.has(nm)) continue;
        shared += 1;
        const d = maxDiff(p, byName.get(nm)!);
        if (d <= TOLERANCE) same += 1;
        else if (d > worst[0]) worst = [d, names.get(nm) ?? hex8(nm)];
    }
    out.nodes_ours = ours.array.length;
    out.nodes_theirs = theirs.array.length;
    out.shared = shared;
    out.same_place = same;
    out.worst = [new Float(pyRound(worst[0], 3)), worst[1]];
    out.crowns = sameSequence(sequence(ours.array, isCrown), sequence(theirs.array, isCrown))[0];
    out.mp_restarts = sameSequence(sequence(ours.array, isMpRestart), sequence(theirs.array, isMpRestart))[0];
    // every network spawn by name, in array order (THUG2 starts player n at the n-th Multiplayer restart)
    const spawnNames = (array: readonly QbValue[]): string =>
        structs(array)
            .filter(isSpawn)
            .map((n) => nameOf(n))
            .join();
    out.spawn_order = spawnNames(ours.array) === spawnNames(theirs.array) ? "same" : "differs";
    const ourMp = mpRestartNames(ours.array);
    const theirMp = mpRestartNames(theirs.array);
    out.mp_restarts_only_ours = nameList(
        [...ourMp].filter((k) => !theirMp.has(k)),
        names,
    );
    out.mp_restarts_only_theirs = nameList(
        [...theirMp].filter((k) => !ourMp.has(k)),
        names,
    );
    const ourNames = new Set(structs(ours.array).map(nameOf));
    const theirNameSet = new Set(structs(theirs.array).map(nameOf));
    const ctf = [
        ...CTF_NAMES,
        ...["Red", "Blue", "Green", "Yellow"].map((c) => `TRG_CTF_Restart_${c}`),
        "TRG_Team_Restart",
    ];
    out.ctf_ours = ctf.filter((c) => ourNames.has(qb.checksum(c)));
    out.ctf_theirs = ctf.filter((c) => theirNameSet.has(qb.checksum(c)));
    // the game-mode nodes both have: at the same place?
    out.ctf_moved = ctf
        .filter((c) => ourNames.has(qb.checksum(c)) && byName.has(qb.checksum(c)))
        .filter((c) => {
            const n = structs(ours.array).find((x) => nameOf(x) === qb.checksum(c));
            const p = n ? position(n) : null;
            return p === null || maxDiff(p, byName.get(qb.checksum(c))!) > TOLERANCE;
        });
    // classes
    const oc = classCounts(ours.array, names);
    const tc = classCounts(theirs.array, names);
    const classes: Record<string, string> = {};
    for (const k of [...new Set([...oc.keys(), ...tc.keys()])].sort()) {
        if ((oc.get(k) ?? 0) !== (tc.get(k) ?? 0)) classes[k] = `${oc.get(k) ?? 0}/${tc.get(k) ?? 0}`;
    }
    out.classes_differ = classes;
    // trigger scripts
    const ot = triggers(ours.array, names);
    const tt = triggers(theirs.array, names);
    out.triggers = `${ot.size}/${tt.size}`;
    const onlyTheirs = new Map<string, number>();
    for (const [node, script] of tt) {
        if (!ot.has(node) && ourNames.has(node)) onlyTheirs.set(script, (onlyTheirs.get(script) ?? 0) + 1);
    }
    out.trigger_scripts_only_theirs = [...onlyTheirs].sort((a, b) => b[1] - a[1]).map(([s, c]) => `${s} x${c}`);
    const onlyOurs = new Map<string, number>();
    for (const [node, script] of ot) {
        if (!tt.has(node) && theirNameSet.has(node)) onlyOurs.set(script, (onlyOurs.get(script) ?? 0) + 1);
    }
    out.trigger_scripts_only_ours = [...onlyOurs].sort((a, b) => b[1] - a[1]).map(([s, c]) => `${s} x${c}`);
    out.nodes_only_theirs = nameList(
        [...theirNameSet].filter((k): k is number => k !== null && !ourNames.has(k)),
        names,
    );
    out.scripts = `${ours.scripts.size}/${theirs.scripts.size}`;
    // collision
    const ourCol = pyJoin(oursDir, mine, `${mine}.col.xbx`);
    if (fs.existsSync(ourCol)) {
        const [faces, missing] = colCheck.check(thug2Col.readCol(fs.readFileSync(ourCol), ourCol));
        out.col_ours = [faces, missing];
    }
    const theirCol = own
        ? findPath(thug2, "Data", "pre", `${key}col.prx`)
        : findPath(thugpro, "data", "pre", `${key}col.prx`);
    if (theirCol) {
        let faces = 0;
        let missing = 0;
        for (const [name, cdata] of colCheck.colFiles(theirCol)) {
            const [f, m] = colCheck.check(thug2Col.readCol(cdata, name));
            faces += f;
            missing += m;
        }
        out.col_theirs = [faces, missing];
    }
    return out;
}

// --- the compatibility profile's THPS3 data (--extract-thugpro) ----------------------------------------------------

// Read once from THUGPRO's install into tools/compat/thugpro/thps3_levels.json (the user approved TP-10 / TP-11,
// 2026-10-20), so the values are re-derived rather than typed in. For each THPS3 level THUGPRO ports under THPS3's own
// name (levels\<level>\<level>.qb in <level>_scripts.prx):
// - TP-10 ctf_flags: TRG_CTF_Red / _Blue and their _Base (place in THUG2's world - Pos, or Position with z negated - and
//   Angles) where THUGPRO's Red or Blue flag isn't at THPS3's own base of that colour (CTF_1 blue / CTF_2 red);
// - TP-11 team_nodes: the nodes THPS3 has no counterpart for - TRG_CTF_Green / _Yellow (+ _Base), the lobby's team flags
//   TRG_Flag_<colour> (+ _Base) and TRG_Team_Restart;
// - TP-12 spawns: every network spawn THUGPRO changed against THPS3's node array (approved later the same day: both
//   sides should spawn players alike) - each restart with a MultiPlayer / Horse / Team / CTF* type on either side whose
//   place, angles, type, restart_types, RestartName or CreatedAtStart THUGPRO changed, or that THUGPRO added
//   (TRG_CTF_Restart_<colour>, Tokyo's TRG_Restart_1P...), as THUGPRO has it (`changed`); and `removed`: THPS3's
//   network spawns THUGPRO doesn't have (Warehouse's TRG_CTF1_Restart_*). TRG_Team_Restart is TP-11's.
export const TP10_NODES = ["Red", "Blue"].flatMap((c) => [`TRG_CTF_${c}`, `TRG_CTF_${c}_Base`]);
export const TP11_NODES = [
    ...["Green", "Yellow"].flatMap((c) => [`TRG_CTF_${c}`, `TRG_CTF_${c}_Base`]),
    ...["Red", "Blue", "Green", "Yellow"].flatMap((c) => [`TRG_Flag_${c}`, `TRG_Flag_${c}_Base`]),
    "TRG_Team_Restart",
];
interface Placement {
    pos: number[];
    angles: number[];
}

function placement(node: Struct): Placement | null {
    const p = position(node);
    if (p === null) return null;
    const a = node.get("Angles");
    return { pos: [...p], angles: a instanceof Vector && a.kind === "vector" ? [...a.values] : [0, 0, 0] };
}

export const SPAWN_TYPES = ["MultiPlayer", "Horse", "Team", "CTF", "CTF_1", "CTF_2", "CTF_3", "CTF_4"];
const SPAWN_SET: ReadonlySet<number> = new Set(SPAWN_TYPES.map(qb.checksum));

// A restart's spawn record (TP-12): place and angles in THUG2's world, Type, restart_types, RestartName,
// CreatedAtStart.
export interface SpawnRecord extends Placement {
    type: string | null;
    restart_types: string[];
    restart_name: string | null;
    created: boolean;
}

export const isRestart = (node: Struct): boolean => isName(node.get("Class"), "Restart");

export function restartTypes(node: Struct): number[] {
    const v = node.get("restart_types");
    return Array.isArray(v) ? v.filter((x): x is Name => x instanceof Name).map((x) => x.value) : [];
}

export const isSpawn = (node: Struct): boolean => isRestart(node) && restartTypes(node).some((t) => SPAWN_SET.has(t));

function spawnRecord(node: Struct, names: ReadonlyMap<number, string>): SpawnRecord {
    const text = (v: number): string => names.get(v) ?? `#${hex8(v)}`;
    const type = node.get("Type");
    const label = node.get("RestartName");
    return {
        ...placement(node)!,
        type: type instanceof Name ? text(type.value) : null,
        restart_types: restartTypes(node).map(text),
        restart_name: typeof label === "string" ? label : null,
        created: node.hasFlag("CreatedAtStart"),
    };
}

// THUGPRO's spawns that differ from THPS3's ({name: record}) and THPS3's spawns it hasn't ([names]).
export function spawnChanges(
    own: readonly QbValue[],
    ownNames: ReadonlyMap<number, string>,
    theirs: readonly QbValue[],
    theirNames: ReadonlyMap<number, string>,
    skip: ReadonlySet<number>,
): [Record<string, SpawnRecord>, string[]] {
    const names = new Map([...ownNames, ...theirNames]);
    const text = (v: number): string => names.get(v) ?? `#${hex8(v)}`;
    const byName = (array: readonly QbValue[]): Map<number, Struct> => {
        const out = new Map<number, Struct>();
        for (const n of array) if (n instanceof Struct && nameOf(n) !== null) out.set(nameOf(n)!, n);
        return out;
    };
    const ownBy = byName(own);
    const theirBy = byName(theirs);
    const changed: Record<string, SpawnRecord> = {};
    for (const [nm, node] of theirBy) {
        if (skip.has(nm) || !isRestart(node)) continue;
        const mine = ownBy.get(nm);
        if (!isSpawn(node) && !(mine !== undefined && isSpawn(mine))) continue;
        const rec = spawnRecord(node, names);
        if (mine !== undefined && isRestart(mine)) {
            const old = spawnRecord(mine, names);
            const same =
                maxDiff(old.pos, rec.pos) <= TOLERANCE &&
                maxDiff(old.angles, rec.angles) <= 0.001 &&
                (old.type ?? "").toLowerCase() === (rec.type ?? "").toLowerCase() &&
                [...restartTypes(mine)].sort().join() === [...restartTypes(node)].sort().join() &&
                old.restart_name === rec.restart_name &&
                old.created === rec.created;
            if (same) continue;
        }
        changed[text(nm)] = rec;
    }
    const removed = [...ownBy]
        .filter(([nm, node]) => !skip.has(nm) && isSpawn(node) && !theirBy.has(nm))
        .map(([nm]) => text(nm));
    return [changed, removed];
}

// --- THUGPRO's hand edits (TP-13 .. TP-19, the user's decision 2026-10-20: "everything THUGPRO changed" as data) ---

// THPS3 class -> the THUG2 classes a port makes of it (a translation, not counted as an edit; thugpro_inventory too).
export const USUAL_CLASSES: ReadonlyMap<string, readonly string[]> = new Map([
    ["environmentobject", ["levelgeometry"]],
    ["gameobject", ["levelobject", "gameobject"]],
    ["bouncyobject", ["gameobject", "levelobject"]],
    ["vehicle", ["vehicle", "levelobject"]],
    ["animatingobject", ["animatingobject", "levelobject", "gameobject"]],
]);
// The node flags whose changes are applied (TP-14). NeverSuspend / Permanent: THUGPRO's toolchain's form (Permanent
// joins every NetEnabled, NeverSuspend goes from game objects) - translation, left out.
export const EDIT_FLAGS = ["CreatedAtStart", "AbsentInNetGames", "NetEnabled", "TrickObject"] as const;

export interface FlagEdit {
    add: string[];
    remove: string[];
}

// An added rail (TP-18): THUGPRO's node data - place (THUG2's world), angles, terrain, flags, cluster, links by name.
export interface RailRecord extends Placement {
    name: string;
    terrain: string | null;
    flags: string[];
    cluster: string | null;
    links: string[];
}

export interface NodeEdits {
    moved: Record<string, Placement>;
    flags: Record<string, FlagEdit>;
    terrain: Record<string, string>;
    removed: string[];
    renamed: Record<string, string>;
    rails: RailRecord[];
    // what was found but not taken, with the reason (for the report and the register)
    notes: string[];
}

// The names TP-10 / TP-11 / TP-12 make or place: not edited again here.
const COMPAT_NAMES: ReadonlySet<number> = new Set(
    [...TP10_NODES, ...TP11_NODES, ...["Red", "Blue", "Green", "Yellow"].map((c) => `TRG_CTF_Restart_${c}`)].map(
        qb.checksum,
    ),
);

// How often each name (0x16 token) occurs in the given qb files: a node still named by a script (or another node)
// isn't removed or renamed.
export function nameUses(files: readonly Buffer[]): Map<number, number> {
    const out = new Map<number, number>();
    for (const data of files) {
        for (const [, t, p] of qb.tokens(data)) {
            if (t !== 0x16) continue;
            const v = p.readUInt32LE(0);
            out.set(v, (out.get(v) ?? 0) + 1);
        }
    }
    return out;
}

// THUGPRO's hand edits of a level's node array against THPS3's own (both in full: Links are indices into them).
// `uses`: nameUses of THPS3's level .qb and global scripts (a node's own Name counts once).
// - moved (TP-13): a node both have that THUGPRO placed elsewhere (> TOLERANCE) or turned (> 0.001 rad) - not the
//   network spawns (TP-12) or the CTF / team nodes (TP-10 / TP-11);
// - flags (TP-14): EDIT_FLAGS added / removed on a node of the same kind (USUAL_CLASSES) - a node THUGPRO made another
//   class (THPS3's lights, particle boxes, static peds as game objects...) is its toolchain's form: left out;
// - terrain (TP-15): every TerrainType THUGPRO changed (THPS3's TERRAIN_MEATALSMOOTH typo, wires, ropes...);
// - renamed (TP-17): a THUGPRO-only node at the place of a THPS3-only node of the same class, the old name used by no
//   script, the new one not TP-10 / TP-11's;
// - removed (TP-16): the other THPS3-only nodes no script names;
// - rails (TP-18): THUGPRO-only RailNodes, as node data.
export function nodeEdits(
    own: readonly QbValue[],
    ownNames: ReadonlyMap<number, string>,
    theirs: readonly QbValue[],
    theirNames: ReadonlyMap<number, string>,
    uses: ReadonlyMap<number, number>,
): NodeEdits {
    const names = new Map([...ownNames, ...theirNames]);
    const text = (v: number): string => names.get(v) ?? `#${hex8(v)}`;
    const classText = (n: Struct): string => {
        const c = n.get("Class");
        return c instanceof Name ? text(c.value).toLowerCase() : "-";
    };
    const sameKind = (o: Struct, t: Struct): boolean =>
        classText(o) === classText(t) || (USUAL_CLASSES.get(classText(o)) ?? []).includes(classText(t));
    const byName = (array: readonly QbValue[]): Map<number, Struct> => {
        const out = new Map<number, Struct>();
        for (const n of array)
            if (n instanceof Struct && nameOf(n) !== null && !out.has(nameOf(n)!)) out.set(nameOf(n)!, n);
        return out;
    };
    const ownBy = byName(own);
    const theirBy = byName(theirs);
    const spawn = (nm: number): boolean =>
        [ownBy.get(nm), theirBy.get(nm)].some((n) => n !== undefined && isRestart(n) && isSpawn(n));
    const edits: NodeEdits = { moved: {}, flags: {}, terrain: {}, removed: [], renamed: {}, rails: [], notes: [] };
    const terrainOf = (n: Struct): number | null => {
        const v = n.get("TerrainType");
        return v instanceof Name ? v.value : null;
    };
    for (const [nm, t] of theirBy) {
        const o = ownBy.get(nm);
        if (o === undefined) continue;
        const skip = COMPAT_NAMES.has(nm) || spawn(nm);
        const a = placement(o);
        const b = placement(t);
        if (!skip && a !== null && b !== null) {
            if (maxDiff(a.pos, b.pos) > TOLERANCE || maxDiff(a.angles, b.angles) > 0.001) edits.moved[text(nm)] = b;
        }
        if (!skip) {
            const add = EDIT_FLAGS.filter((f) => t.hasFlag(f) && !o.hasFlag(f));
            const remove = EDIT_FLAGS.filter((f) => o.hasFlag(f) && !t.hasFlag(f));
            if (add.length || remove.length) {
                if (sameKind(o, t)) edits.flags[text(nm)] = { add, remove };
                else edits.notes.push(`flags of ${text(nm)}: class ${classText(o)} -> ${classText(t)} (translation)`);
            }
        }
        const to = terrainOf(o);
        const tt = terrainOf(t);
        if (tt !== null && tt !== to) edits.terrain[text(nm)] = text(tt);
        else if (tt === null && to !== null) edits.notes.push(`terrain of ${text(nm)}: THUGPRO has none`);
    }
    // renamed / removed / added
    const onlyTheirs = [...theirBy].filter(([nm]) => !ownBy.has(nm));
    const onlyOwn = [...ownBy].filter(([nm]) => !theirBy.has(nm));
    const used = (nm: number): boolean => (uses.get(nm) ?? 0) > 1;
    const renamedFrom = new Set<number>();
    const renamedTo = new Set<number>();
    for (const [nm, t] of onlyTheirs) {
        const pt = position(t);
        if (pt === null || COMPAT_NAMES.has(nm)) continue;
        const match = onlyOwn.find(([om, o]) => {
            const po = position(o);
            return !renamedFrom.has(om) && po !== null && maxDiff(po, pt) <= TOLERANCE && classText(o) === classText(t);
        });
        if (match === undefined) continue;
        if (used(match[0])) {
            edits.notes.push(`renamed ${text(match[0])} -> ${text(nm)}: the old name is used by a script - kept`);
            renamedFrom.add(match[0]); // (neither renamed nor removed)
            continue;
        }
        renamedFrom.add(match[0]);
        renamedTo.add(nm);
        edits.renamed[text(match[0])] = text(nm);
    }
    for (const [om] of onlyOwn) {
        if (renamedFrom.has(om)) continue;
        if (used(om)) edits.notes.push(`removed ${text(om)}: used by a script - kept`);
        else edits.removed.push(text(om));
    }
    for (const [nm, t] of onlyTheirs) {
        if (classText(t) !== "railnode" || renamedTo.has(nm)) continue;
        const p = placement(t)!;
        const links = t.get("Links");
        const terrain = terrainOf(t);
        const cluster = t.get("Cluster");
        edits.rails.push({
            name: text(nm),
            ...p,
            terrain: terrain === null ? null : text(terrain),
            flags: EDIT_FLAGS.filter((f) => t.hasFlag(f)),
            cluster: cluster instanceof Name ? text(cluster.value) : null,
            links: (Array.isArray(links) ? links : []).map((i) => {
                const target = typeof i === "number" ? theirs[i] : undefined;
                if (!(target instanceof Struct) || nameOf(target) === null)
                    throw new RangeError(`${text(nm)}: a link to no named node (${String(i)})`);
                return text(nameOf(target)!);
            }),
        });
    }
    return edits;
}

// A collision edit (TP-19): the object's collision left out (`drop`), or some of its faces (`remove`: each face as its
// three corners in THPS3's order - THPS3's own vertices, the geometry's frame).
export interface CollisionEdit {
    drop?: boolean;
    remove?: number[][][];
}

const near = (a: readonly number[], b: readonly number[], tolerance: number): boolean =>
    a.every((x, i) => Math.abs(x - b[i]!) <= tolerance);

// Whether two faces have the same corners in the same turning order (any first corner).
export function sameFace(
    a: readonly (readonly number[])[],
    b: readonly (readonly number[])[],
    tolerance: number,
): boolean {
    return [0, 1, 2].some((r) => [0, 1, 2].every((i) => near(a[i]!, b[(i + r) % 3]!, tolerance)));
}

// THUGPRO's collision edits against THPS3's own geometry: an object THUGPRO draws but has no collision for (`drop`),
// and the faces THPS3's object has and THUGPRO's hasn't (`remove`) - found only when THUGPRO's faces are THPS3's
// (same corners, either turning; 0.15 units: its collision vertices are fixed point) less some. Face flags and terrain
// numbers aren't compared (THUGPRO's toolchain writes its own flag bits on every face: translation); an object whose
// faces moved or changed otherwise is noted, not edited.
export function collisionEdits(
    world: thps3.World,
    theirCol: readonly thug2Col.ColObject[],
    theirScene: ReadonlySet<number>,
    names: ReadonlyMap<number, string>,
): [Record<string, CollisionEdit>, string[]] {
    const text = (v: number): string => names.get(v) ?? `#${hex8(v)}`;
    const theirs = new Map(theirCol.map((o) => [o.checksum, o] as const));
    const out: Record<string, CollisionEdit> = {};
    const notes: string[] = [];
    const tolerance = 0.15;
    for (const sec of world.sectors) {
        const count = Math.min(sec.triangles.length, (sec.faceFlags ?? []).length);
        if (!count) continue;
        const t = theirs.get(sec.checksum);
        if (t === undefined) {
            if (theirScene.has(sec.checksum)) out[text(sec.checksum)] = { drop: true };
            continue;
        }
        if (t.faces.length >= count) continue;
        const mine = sec.triangles.slice(0, count).map(([, a, b, c]) => [a, b, c].map((i) => sec.positions[i]!));
        const left = new Set(mine.keys());
        const theirFaces = t.faces.map((f) => [f[2], f[3], f[4]].map((i) => t.verts[i]!));
        const unmatched: (readonly number[])[][] = [];
        for (const f of theirFaces) {
            const k = [...left].find((i) => sameFace(mine[i]!, f, tolerance));
            if (k === undefined) unmatched.push(f);
            else left.delete(k);
        }
        // (a face THUGPRO's toolchain turned the other way round)
        const reversed = (f: readonly (readonly number[])[]): (readonly number[])[] => [f[0]!, f[2]!, f[1]!];
        for (const f of unmatched) {
            const k = [...left].find((i) => sameFace(mine[i]!, reversed(f), tolerance));
            if (k === undefined) {
                notes.push(`collision ${text(sec.checksum)}: THUGPRO's faces aren't THPS3's - not edited`);
                left.clear();
                break;
            }
            left.delete(k);
        }
        if (left.size && left.size === count - t.faces.length)
            out[text(sec.checksum)] = {
                remove: [...left].sort((a, b) => a - b).map((i) => mine[i]!.map((p) => [...p])),
            };
    }
    return [out, notes];
}

export function extractThugpro(
    thps3Dir: string,
    thugpro: string,
    thug2: string | null = null,
): Record<string, unknown> {
    // (thugpro_new needs THUG2's own scripts and commands: which of THUGPRO's scripts can run - --thug2)
    const thug2Data = thug2 !== null ? new scriptPort.Thug2(thug2) : null;
    const thugproNew: Record<string, unknown> = {
        _register: REGISTER_ID,
        _source:
            "References only (the user's decision 2026-10-20, with Morten1337's permission): what is new in THUGPRO's THPS3 ports, in no source game - archive and file in THUGPRO's datapre, the item's name and a CRC-32 of it. tools/thps3_levels/thugpro_new.ts reads the bytes from the user's THUGPRO install (--thugpro) at convert time; a missing or different item is warned about and skipped. Each entry: op (add / set / remove), id, kind (sector, collision, node, script, model), target, why. Found by tools/thps3_levels/thugpro_new_extract.ts (--extract-thugpro); never typed in.",
    };
    const newLeftOut: Record<string, unknown> = {};
    const teams = flagTeams(scriptNames(thps3Dir));
    const flags: Record<string, unknown> = {
        _register: "TP-10",
        _source:
            "THUGPRO's installed data\\pre\\<level>_scripts.prx, levels\\<level>\\<level>.qb: TRG_CTF_Red / _Blue (+ _Base) - pos in THUG2's world (Pos; Position with z negated), angles as they are. Extracted by tools/thps3_levels/thugpro_compare.ts --extract-thugpro.",
    };
    const extra: Record<string, unknown> = {
        _register: "TP-11",
        _source:
            "THUGPRO's installed data\\pre\\<level>_scripts.prx, levels\\<level>\\<level>.qb: TRG_CTF_Green / _Yellow (+ _Base), TRG_Flag_<colour> (+ _Base), TRG_Team_Restart. Extracted by tools/thps3_levels/thugpro_compare.ts --extract-thugpro.",
    };
    const spawnData: Record<string, unknown> = {
        _register: "TP-12",
        _source:
            "THUGPRO's installed data\\pre\\<level>_scripts.prx, levels\\<level>\\<level>.qb against THPS3's Data\\Levels\\<Lvl>\\<lvl>.qb: every restart with a network type (MultiPlayer, Horse, Team, CTF*) THUGPRO moved, retyped, renamed or added (changed: pos in THUG2's world, angles, type, restart_types, restart_name, created), and THPS3's network restarts THUGPRO hasn't (removed). Extracted by tools/thps3_levels/thugpro_compare.ts --extract-thugpro.",
    };
    const source = (what: string): string =>
        `THUGPRO's installed data\\pre\\<level>_scripts.prx (levels\\<level>\\<level>.qb) against THPS3's Data\\Levels\\<Lvl>\\<lvl>.qb: ${what}. Extracted by tools/thps3_levels/thugpro_compare.ts --extract-thugpro (nodeEdits).`;
    const moved13: Record<string, unknown> = {
        _register: "TP-13",
        _source: source(
            "the nodes THUGPRO moved or turned (not the network spawns - TP-12 - or the CTF / team nodes - TP-10 / TP-11): {name: pos in THUG2's world, angles}",
        ),
    };
    const flags14: Record<string, unknown> = {
        _register: "TP-14",
        _source: source(
            "CreatedAtStart / AbsentInNetGames / NetEnabled / TrickObject THUGPRO added or removed on the nodes it kept of the same kind (one it made another class is its toolchain's form: not here)",
        ),
    };
    const terrain15: Record<string, unknown> = {
        _register: "TP-15",
        _source: source("every TerrainType THUGPRO changed: {name: THUGPRO's terrain}"),
    };
    const removed16: Record<string, unknown> = {
        _register: "TP-16",
        _source: source(
            "THPS3's nodes THUGPRO hasn't (not renamed) that no script or other node of THPS3's names (the level's .qb, Data\\Scripts)",
        ),
    };
    const renamed17: Record<string, unknown> = {
        _register: "TP-17",
        _source: source(
            "{THPS3's name: THUGPRO's}: a THUGPRO-only node at the place of a THPS3-only node of the same class whose name no script uses",
        ),
    };
    const rails18: Record<string, unknown> = {
        _register: "TP-18",
        _source: source(
            "the RailNodes THUGPRO added, as node data: pos in THUG2's world, angles, TerrainType, flags, Cluster, Links by name",
        ),
    };
    const collision19: Record<string, unknown> = {
        _register: "TP-19",
        _source:
            "THUGPRO's installed data\\pre\\<level>col.prx / <level>scn.prx against THPS3's Data\\Levels\\<Lvl>\\<lvl>.bsp: an object THUGPRO draws without collision (drop), the faces THUGPRO's object lacks (remove: THPS3's own corners, in THPS3's order). Extracted by tools/thps3_levels/thugpro_compare.ts --extract-thugpro (collisionEdits).",
    };
    const notes: Record<string, string[]> = {};
    const globalNames = new Map<number, string>();
    for (const q of scriptNames(thps3Dir)) for (const [k, v] of q.names) globalNames.set(k, v);
    const scriptsDir = pyJoin(thps3Dir, "Data", "Scripts");
    const globalUses = nameUses(
        fs
            .readdirSync(scriptsDir)
            .filter((f) => f.toLowerCase().endsWith(".qb"))
            .map((f) => fs.readFileSync(pyJoin(scriptsDir, f))),
    );
    for (const [base] of LEVELS) {
        const key = base.toLowerCase();
        if (THUG2_OWN.has(key) || NO_COUNTERPART.has(key)) continue;
        const scripts = findPath(thugpro, "data", "pre", `${key}_scripts.prx`);
        const folder = findFolder(pyJoin(thps3Dir, "Data", "Levels"), base);
        if (!scripts || !folder) continue;
        const data = prx.find(scripts, `levels\\${key}\\${key}.qb`);
        if (data === null) continue;
        const [theirs, theirNames] = nodeArray(data, `${key}.qb`);
        const byName = new Map<number, Struct>();
        for (const n of theirs) if (n instanceof Struct && nameOf(n) !== null) byName.set(nameOf(n)!, n);
        const pick = (names: readonly string[]): Record<string, Placement> => {
            const out: Record<string, Placement> = {};
            for (const nm of names) {
                const node = byName.get(qb.checksum(nm));
                const p = node ? placement(node) : null;
                if (p !== null) out[nm] = p;
            }
            return out;
        };
        // THPS3's own bases, by colour
        const ownQ = new qb.QbFile(fs.readFileSync(levelFiles(folder, base).get("qb")!), key);
        const own = ownQ.get("NodeArray");
        let moved = false;
        for (const team of teams.filter((t) => t.colour === "Red" || t.colour === "Blue")) {
            const baseNode = (Array.isArray(own) ? own : []).find((n): n is Struct => {
                const t = n instanceof Struct ? n.get("Type") : undefined;
                return t instanceof Name && t.value === team.type;
            });
            const theirFlag = byName.get(qb.checksum(`TRG_CTF_${team.colour}`));
            const a = baseNode ? position(baseNode) : null;
            const b = theirFlag ? position(theirFlag) : null;
            if (a !== null && b !== null && maxDiff(a, b) > TOLERANCE) moved = true;
        }
        if (moved) flags[key] = pick(TP10_NODES);
        const more = pick(TP11_NODES);
        if (Object.keys(more).length) extra[key] = more;
        const [changed, removed] = spawnChanges(
            Array.isArray(own) ? own : [],
            ownQ.names,
            theirs,
            theirNames,
            new Set([qb.checksum("TRG_Team_Restart")]), // (TP-11's)
        );
        // (every network spawn, in THUGPRO's array order: the order decides who starts where)
        const order = theirs
            .filter((n): n is Struct => n instanceof Struct && isSpawn(n))
            .map((n) => theirNames.get(nameOf(n)!) ?? `#${hex8(nameOf(n)!)}`);
        spawnData[key] = { changed, removed, order };
        // TP-13 .. TP-18: the node edits (the level's own .qb and THPS3's global scripts tell which names are used)
        const ownNames = new Map(globalNames);
        for (const [k, v] of ownQ.names) ownNames.set(k, v);
        const uses = nameUses([fs.readFileSync(levelFiles(folder, base).get("qb")!)]);
        for (const [k, v] of globalUses) uses.set(k, (uses.get(k) ?? 0) + v);
        const edits = nodeEdits(Array.isArray(own) ? own : [], ownNames, theirs, theirNames, uses);
        if (Object.keys(edits.moved).length) moved13[key] = edits.moved;
        if (Object.keys(edits.flags).length) flags14[key] = edits.flags;
        if (Object.keys(edits.terrain).length) terrain15[key] = edits.terrain;
        if (edits.removed.length) removed16[key] = edits.removed;
        if (Object.keys(edits.renamed).length) renamed17[key] = edits.renamed;
        if (edits.rails.length) rails18[key] = edits.rails;
        // TP-19: collision, against THPS3's own geometry
        const colPrx = findPath(thugpro, "data", "pre", `${key}col.prx`);
        const scnPrx = findPath(thugpro, "data", "pre", `${key}scn.prx`);
        if (colPrx && scnPrx) {
            const theirCol: thug2Col.ColObject[] = [];
            for (const [name, cdata] of colCheck.colFiles(colPrx))
                theirCol.push(...thug2Col.readCol(cdata, name).objects);
            const theirScene = new Set<number>();
            for (const [name, sdata] of prx.entries(scnPrx, (n) => /\.scn\./i.test(n)))
                for (const s of thug2Scene.readScene(sdata, name).sectors) theirScene.add(s.checksum);
            const bsp = levelFiles(folder, base).get("bsp")!;
            const [col, colNotes] = collisionEdits(
                thps3.readBsp(fs.readFileSync(bsp), bsp),
                theirCol,
                theirScene,
                ownNames,
            );
            if (Object.keys(col).length) collision19[key] = col;
            edits.notes.push(...colNotes);
        }
        if (edits.notes.length) notes[key] = edits.notes;
        if (thug2Data !== null && thug2 !== null) {
            // TPN-THPS3: THUGPRO's own content (the nodes TP-10 .. TP-18 make or place aren't new content)
            const handled = new Set<number>([...COMPAT_NAMES, ...Object.keys(changed).map(compatChecksum)]);
            for (const r of edits.rails) handled.add(compatChecksum(r.name));
            for (const now of Object.values(edits.renamed)) handled.add(compatChecksum(now));
            const globalScripts = new Set<number>();
            const globalQbs = scriptNames(thps3Dir);
            for (const q of globalQbs) for (const k of q.scripts.keys()) globalScripts.add(k);
            const found = extractNew({
                level: key,
                base,
                thps3Dir,
                thugpro,
                thug2: thug2Data,
                thug2Dir: thug2,
                globalScripts,
                globalQbs,
                globalNames,
                handled,
            });
            if (found.entries.length) thugproNew[key] = found.entries;
            if (found.leftOut.length) newLeftOut[key] = found.leftOut;
        }
    }
    thugproNew._left_out = newLeftOut;
    return {
        _comment:
            "THUGPRO compatibility for the THPS3 level ports (tools/thps3_levels), approved by the user 2026-10-20. Each entry is registered in README.md here (TP-10 .. TP-19). Capture the flag is decided on the server against its own flags; each side draws them from its own node array: these put our flags where THUGPRO's are, and our spawn points as THUGPRO's. TP-13 .. TP-19 (the user's decision 2026-10-20: everything THUGPRO changed, as data under the flag): its other hand edits of THPS3's nodes and collision - moved, flags, terrain, removed, renamed, rails, collision. Level keys are our (= THUGPRO's = THPS3's) level names. Read by tools/compat/compat.ts; `thps3_levels.ts convert --compat none` leaves all of it out (THPS3's own two bases). Regenerate: node tools/thps3_levels/thugpro_compare.ts --extract-thugpro tools/compat/thugpro/thps3_levels.json",
        ctf_flags: flags,
        team_nodes: extra,
        spawns: spawnData,
        moved: moved13,
        flags: flags14,
        terrain: terrain15,
        removed: removed16,
        renamed: renamed17,
        rails: rails18,
        collision: collision19,
        // TPN-THPS3: what is new in THUGPRO (by reference; --no-thugpro-new leaves it out)
        thugpro_new: thugproNew,
        // (what nodeEdits / collisionEdits found and left out, and why - not read by the converter)
        _notes: notes,
    };
}

const USAGE =
    "usage: thugpro_compare.ts [-h] [--thug2 THUG2] [--thugpro THUGPRO] [--thps3 THPS3] [--staging STAGING] [--json JSON] [--details] [--extract-thugpro FILE] [levels ...]";

const OPTIONS = {
    thug2: { type: "string" },
    thugpro: { type: "string" },
    staging: { type: "string" }, // our converted levels (default <thug2>\thug2-decomp-thps3)
    json: { type: "string" },
    details: { type: "boolean" },
    thps3: { type: "string" },
    "extract-thugpro": { type: "string" },
    help: { type: "boolean", short: "h" },
} as const;

export function main(argv: readonly string[] = process.argv.slice(2)): number {
    let parsed: ReturnType<typeof parseArgs<{ options: typeof OPTIONS; allowPositionals: true }>>;
    try {
        parsed = parseArgs({ args: [...argv], options: OPTIONS, allowPositionals: true, strict: true });
    } catch (e) {
        process.stderr.write(`${USAGE}\nthugpro_compare.ts: error: ${(e as Error).message}\n`);
        pyExit(2);
    }
    const v = parsed.values;
    if (v.help) {
        print(USAGE);
        return 0;
    }
    let thug2 = v.thug2 ?? "C:\\Games\\THUG2\\Game";
    if (!fs.existsSync(pyJoin(thug2, "Data")) && fs.existsSync(pyJoin(thug2, "Game", "Data")))
        thug2 = pyJoin(thug2, "Game");
    const thugpro = v.thugpro ?? DEFAULT_THUGPRO;
    if (v["extract-thugpro"]) {
        const data = extractThugpro(v.thps3 ?? "C:\\Games\\THPS3", thugpro, thug2);
        // (port_changes - PC-n, this project's own changes, not THUGPRO's - are kept as the file has them: not extracted)
        if (fs.existsSync(v["extract-thugpro"])) {
            const before = JSON.parse(fs.readFileSync(v["extract-thugpro"], "utf-8")) as Record<string, unknown>;
            if (before.port_changes !== undefined) data.port_changes = before.port_changes;
        }
        fs.writeFileSync(
            v["extract-thugpro"],
            `${JSON.stringify(data, null, 2)}
`,
            "utf-8",
        );
        print(`wrote ${v["extract-thugpro"]}`);
        return 0;
    }
    const staging = v.staging || pyJoin(thug2, STAGING);
    const manifest = portedLevels.readManifest(pyJoin(staging, portedLevels.MANIFEST));
    if (!manifest) pyExit(`${staging}: no ${portedLevels.MANIFEST} (convert first)`);
    let entries = manifest.levels;
    if (parsed.positionals.length) {
        const wanted = new Set(parsed.positionals.map((x) => x.toLowerCase()));
        entries = entries.filter(
            (e) => wanted.has(String(e.folder).toLowerCase()) || wanted.has(e.level.toLowerCase()),
        );
    }
    const results: Result[] = [];
    let bad = 0;
    for (const entry of entries) {
        const r = compare(entry, pyJoin(staging, "levels"), thugpro, thug2);
        results.push(r);
        if ("error" in r) {
            print(`${r.folder.padEnd(10)} ${r.level.padEnd(16)} ${String(r.error)}`);
            continue;
        }
        const worst = r.worst as [Float, string | null];
        const colOurs = (r.col_ours as number[] | undefined) ?? ["-", "-"];
        const colTheirs = (r.col_theirs as number[] | undefined) ?? ["-", "-"];
        print(
            `${r.folder.padEnd(10)} ${r.level.padEnd(16)} vs ${String(r.reference)}: load script ${r.same_load_script ? "same" : "DIFFERS"}; ` +
                `nodes ${r.nodes_ours}/${r.nodes_theirs}, ${r.shared} shared, ${r.same_place} at the same place ` +
                `(worst ${formatFixed(worst[0].value, 3)} ${worst[1] || ""}); crowns ${r.crowns}; mp restarts ` +
                `${r.mp_restarts}; spawns ${r.spawn_order}; ctf ${(r.ctf_ours as string[]).length}/${(r.ctf_theirs as string[]).length}` +
                `${(r.ctf_moved as string[]).length ? ` (moved: ${(r.ctf_moved as string[]).join(" ")})` : ""}; triggers ${r.triggers}; ` +
                `scripts ${r.scripts}; collision unreachable ours ${colOurs[1]}, theirs ${colTheirs[1]}`,
        );
        if (v.details) {
            for (const k of [
                "mp_restarts_only_ours",
                "mp_restarts_only_theirs",
                "ctf_ours",
                "ctf_theirs",
                "classes_differ",
                "trigger_scripts_only_theirs",
                "trigger_scripts_only_ours",
            ]) {
                const value = r[k];
                const shown = Array.isArray(value) ? value.join(", ") : JSON.stringify(value);
                if (shown && shown !== "{}") print(`    ${k}: ${shown}`);
            }
            const only = r.nodes_only_theirs as string[];
            if (only.length)
                print(
                    `    nodes_only_theirs (${only.length}): ${only.slice(0, 40).join(", ")}${only.length > 40 ? " ..." : ""}`,
                );
        }
        if (!r.same_load_script && !THUG2_OWN.has(r.folder.toLowerCase())) bad += 1;
        if (r.crowns !== "same" && !THUG2_OWN.has(r.folder.toLowerCase())) bad += 1;
    }
    if (v.json) writeTextPy(v.json, pyJsonDumps(results, 1));
    return bad ? 1 : 0;
}

if (isMain(import.meta.url)) await runMain(() => main());
