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
//        (the compatibility profile's THPS3 data, TP-10 / TP-11: see extractThugpro)

import * as fs from "node:fs";
import { parseArgs } from "node:util";
import * as colCheck from "../level_port/col_check.ts";
import * as portedLevels from "../level_port/ported_levels.ts";
import * as prx from "../level_port/prx.ts";
import * as qb from "../level_port/qb.ts";
import { Name, type QbValue, Struct, Vector } from "../level_port/qb.ts";
import * as thug2Col from "../level_port/thug2_col.ts";
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
import { findFolder, flagTeams, LEVELS, levelFiles, STAGING, scriptNames } from "./thps3_levels.ts";

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

export function extractThugpro(thps3Dir: string, thugpro: string): Record<string, unknown> {
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
        if (Object.keys(changed).length || removed.length) spawnData[key] = { changed, removed };
    }
    return {
        _comment:
            "THUGPRO compatibility for the THPS3 level ports (tools/thps3_levels), approved by the user 2026-10-20. Each entry is registered in README.md here (TP-10, TP-11, TP-12). Capture the flag is decided on the server against its own flags; each side draws them from its own node array: these put our flags where THUGPRO's are, and our spawn points as THUGPRO's. Level keys are our (= THUGPRO's = THPS3's) level names. Read by tools/compat/compat.ts; `thps3_levels.ts convert --compat none` leaves all of it out (THPS3's own two bases). Regenerate: node tools/thps3_levels/thugpro_compare.ts --extract-thugpro tools/compat/thugpro/thps3_levels.json",
        ctf_flags: flags,
        team_nodes: extra,
        spawns: spawnData,
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
        const data = extractThugpro(v.thps3 ?? "C:\\Games\\THPS3", thugpro);
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
                `${r.mp_restarts}; ctf ${(r.ctf_ours as string[]).length}/${(r.ctf_theirs as string[]).length}` +
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
