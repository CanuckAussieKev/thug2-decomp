// NOT ORIGINAL - converts Tony Hawk's Underground (PC) levels to THUG2's formats so that THUG2 can load and play them
// (see README.md). Nothing of either game is stored in the repo: the files are made from the user's own copies.
//
//   node tools/thug1_levels/thug1_levels.ts selfcheck [--thug2 C:\Games\THUG2] [--thug1 C:\Games\THUG1]
//   node tools/thug1_levels/thug1_levels.ts convert [levels...] [--thug2 ...] [--thug1 ...] [--out <folder>]
//        [--compat thugpro|none] [--legacy-names] [--thugpro <THUGPRO's install>] [--no-thugpro-new] [--no-port-changes]
//   then: node tools/menu_level/make_menu_level.ts   (installs them into the data overlay, with level select signs)
//
// convert reads <thug1>\Game\Data\pre (the level archives <LV>scn.pre, <LV>col.pre, <LV>.pre and qb.pre's Levels.qb)
// and writes <thug2>\thug2-decomp-thug1\ (not read by the game): levels\<name>\<name>.scn.xbx / .tex.xbx / .col.xbx /
// .qb (the node array; <name>: levelName - nj, hh, thug1_sc...), the skies (scene and textures) the same way, and
// levels.json (what
// make_menu_level.ts adds to Levels.qb and the level select: tools/level_port/ported_levels.ts).
//
// The THUG2 formats, the geometry conversion, the node arrays and the installing are shared with tools/thps4_levels
// (tools/level_port); this folder has THUG1's own formats (thug1.ts) and its levels.

import * as fs from "node:fs";
import * as path from "node:path";
import * as compat from "../compat/compat.ts";
import { FormatError } from "../level_port/binio.ts";
import * as common from "../level_port/common.ts";
import * as geometry from "../level_port/geometry.ts";
import * as nodes from "../level_port/nodes.ts";
import * as portedLevels from "../level_port/ported_levels.ts";
import * as prx from "../level_port/prx.ts";
import * as qb from "../level_port/qb.ts";
import { Name, Struct } from "../level_port/qb.ts";
import * as scriptPort from "../level_port/script_port.ts";
import * as tex from "../level_port/tex.ts";
import * as thug2Scene from "../level_port/thug2_scene.ts";
import { parseConverterArgs } from "../lib/converter_args.ts";
import { resolveInstall } from "../lib/install.ts";
import { hex8, isMain, print, pyExit, pyJoin, pyRepr, runMain } from "../lib/py.ts";
import * as handEdits from "./hand_edits.ts";
import * as portChanges from "./port_changes.ts";
import * as thug1 from "./thug1.ts";
import * as thug1Port from "./thug1_port.ts";
import { type Side, type SpawnEdits, thugproSide } from "./thugpro_edits.ts";
import * as thugproNew from "./thugpro_new.ts";

export const STAGING = "thug2-decomp-thug1";

// THUG1's levels: code (the archives' and Levels.qb's name), the level select's sign text, where it is, a THUG2
// level select sign that suits it, the sign's level number (unique: THPS4's levels use 19-31; none has career
// records), and whether `convert` makes it by default. Read by the level select (levels.json carries the same).
export type LevelInfo = readonly [string, string, string, string, number, boolean];
// biome-ignore format: a table
export const LEVELS: readonly LevelInfo[] = [
    // code            sign text                 location                         icon               num  default
    ["NJ",            "NEW JERSEY",             "New Jersey, USA",               "sign_philly",      40, true],
    ["NY",            "MANHATTAN",              "Manhattan, New York, USA",      "sign_boston",      41, true],
    ["FL",            "TAMPA",                  "Tampa, Florida, USA",           "sign_neworleans",  42, true],
    ["SD",            "SAN DIEGO",              "San Diego, California, USA",    "sign_la",          43, true],
    ["HI",            "HAWAII",                 "Hawaii, USA",                   "sign_australia",   44, true],
    ["VC",            "VANCOUVER",              "Vancouver, Canada",             "sign_canada",      45, true],
    ["SJ",            "SLAM CITY JAM",          "Vancouver, Canada",             "sign_canada",      46, true],
    ["RU",            "MOSCOW",                 "Moscow, Russia",                "sign_berlin",      47, true],
    ["SE",            "HOTTER THAN HELL",       "Hell (secret level)",           "sign_secret1",     48, true],
    ["SC",            "OLD SCHOOL",             "Miami, Florida, USA (THPS1)",   "sign_school",      49, true],
    ["DJ",            "OLD DOWNHILL JAM",       "Phoenix, Arizona, USA (THPS1)", "sign_jam",         50, true],
    ["PH",            "OLD PHILLY",             "Philadelphia, USA (THPS2)",     "sign_philly",      51, true],
    ["VN",            "OLD VENICE",             "Venice Beach, California, USA (THPS2)", "sign_la",  52, true],
    ["HN",            "OLD HANGAR",             "Mullet Falls, Montana, USA (THPS2)", "sign_airport", 53, true],
    ["SC2",           "SCHOOL II",              "Southern California, USA (THPS2)", "sign_school",  54, true],
    ["NJ_Skateshop",  "NJ SKATE SHOP",          "New Jersey, USA",               "sign_training",    55, true],
    ["skateshop",     "SKATE SHOP",             "THUG1's front end skate shop (THPS4's)", "sign_training", 56, false],
    ["CAS_BEDROOM",   "BEDROOM",                "Create-a-skater bedroom",       "sign_training",    57, false],
    ["TestLevel",     "TEST LEVEL",             "The developers' test level",    "sign_training",    58, false],
];
export const BY_CODE: ReadonlyMap<string, LevelInfo> = new Map(LEVELS.map((info) => [info[0].toLowerCase(), info]));

// THUG1's collision face flags in THUG2: the same meanings up to 0x1000 (0x80: the camera passes through, as THUG2's -
// THUG1's own levels set it on under 7% of their faces: lamps, trees, fences, triggers). Not in OLD_CAMERA_FLAG's
// levels, though: THUG1 shipped those with the THPS3 / THPS4 meaning (0x80 where the camera collides - the solid world:
// DJ 6,596 of 7,120 faces, PH 8,596 of 10,120, SC 10,692 of 13,114, the skate shop THPS4's own 6,141 of 19,724); THUG2
// re-flagged its own copies of DJ / PH / SC (0 / 316 / 328 faces). Theirs is inverted, as the THPS3 and THPS4
// converters do (docs/pending/face-flag-0x80.md). Above: THUG1 sets 0x2000 on every face (476,641 of 476,641) - in
// THUG2 it means "not taggable" (THUG2's own levels: 6,555 faces, mostly walls), which would make all of a THUG1 level
// untaggable: dropped; 0x4000 (8,397) and 0x8000 (69,189) do nothing in THUG2 (below): dropped.
// THUG2's own bits above 0x1000 (docs/level-port-compromises.md): 0x2000 is "not taggable" - the graffiti feelers
// ignore 0x1000 | 0x2000 (SetIgnore(0x3000, 0) at 0x0051DF71 / 0x0051E74B); 0x4000 and 0x8000 (on 4,535 / 40,513 of
// THUG2's own faces - the park editor's shells, most levels) have no reader in THUG2's code: no feeler's ignore masks
// (SetIgnore's callers: 0x10, 0x810, 0x90, 0x3000, require 0x20 / 0x50) and no test of those bits in the face flags
// anywhere in the exe (every test / and with 0x4000 / 0x8000 is of other fields: CCollObj +0x08, pads, piece flags).
export const FACE_KEEP = 0x1fff;

export function faceFlags(flags: number): number {
    return flags & FACE_KEEP;
}

// The levels whose face flag 0x80 has THPS3 / THPS4's meaning (the camera collides): inverted for THUG2.
export const OLD_CAMERA_FLAG: ReadonlySet<string> = new Set(["dj", "ph", "sc", "skateshop"]);
export const FACE_CAMERA = 0x0080;

// The face flags function for level `code`.
export function faceFlagsOf(code: string): (flags: number) => number {
    return OLD_CAMERA_FLAG.has(code.toLowerCase()) ? (flags) => faceFlags(flags ^ FACE_CAMERA) : faceFlags;
}

interface Args {
    command: string;
    levels: string[];
    thug2: string;
    thug1: string;
    out: string | undefined;
    ns: string;
    static: boolean;
    extra: string[];
    compat: string;
    legacyNames: boolean;
    thugpro: string; // THUGPRO's install (TPN-THUG1-1 ... TPN-THUG1-4 read from it)
    noThugproNew: boolean;
    noPortChanges: boolean;
}

// --- level names (online: the load script's checksum is the level) ---------------------------------------------------
//
// A network game's change-level message (0x46) and start info (0x33) carry only the load script's checksum; the other
// side runs the script of that checksum. So a level is named as THUG1 itself names it - its Levels.qb structure's
// level= when its load script is Load_<level> (Load_NJ, Load_hn...) - which is also what THUGPRO's own ports of them
// load (thugpro_compare.ts checks it). A THUG1 name THUG2 already uses (SE, SC, DJ, PH, TestLevel: THUG2's own load
// scripts) and a level THUG1's Levels.qb doesn't load by its own name (NJ_Skateshop, the front end's skateshop,
// CAS_BEDROOM) keep the thug1_ prefix: Levels.qb would otherwise replace THUG2's own level. What THUGPRO chose beyond
// that (Hotter Than Hell as hh: THUG1's SE is THUG2's SE) is in the compatibility profile
// tools/compat/thugpro/thug1_levels.json (TP-30...; --compat none leaves it out). --legacy-names: thug1_<code> for
// every level, as this converter named them before.
export const COMPAT = {
    levelNames: new Map<string, string>(),
    spawns: new Map<string, SpawnEdits>(),
    edits: new Map<string, handEdits.LevelEdits>(), // (TP-32 ... TP-39: hand_edits.ts, by level code in lower case)
    // (TPN-THUG1-1 ... TPN-THUG1-4: what is new in THUGPRO - thugpro_new.ts; by level code in lower case)
    new: new Map<string, thugproNew.NewLevel>(),
    // (our own changes - port_changes.ts: read whatever the profile)
    portChanges: portChanges.load(),
};

// The compatibility profile's THUG1 data (tools/compat/compat.ts): "thugpro" (the default) or "none". thugproNew: false
// leaves the profile's thugpro_new out (--no-thugpro-new); ownChanges: false switches our own changes that aren't
// required off (--no-port-changes).
export function setCompat(name: string, thugproNew_ = true, ownChanges = true): void {
    const profile = compat.load(name);
    COMPAT.new = new Map();
    if (thugproNew_) {
        const refs = (profile.section("thug1_levels", "thugpro_new").levels ?? {}) as Record<
            string,
            thugproNew.LevelRef
        >;
        COMPAT.new = thugproNew.fromSection(compat.thugproNew(profile, "thug1_levels"), refs);
    }
    COMPAT.portChanges = portChanges.load(ownChanges);
    COMPAT.levelNames = new Map(
        Object.entries(profile.section("thug1_levels", "level_names")).map(([k, v]) => [k.toLowerCase(), String(v)]),
    );
    COMPAT.spawns = new Map(
        Object.entries(profile.section("thug1_levels", "spawns")).map(([k, v]) => [k.toLowerCase(), v as SpawnEdits]),
    );
    COMPAT.edits = new Map();
    for (const [section] of handEdits.SECTIONS) {
        for (const [code, data] of Object.entries(profile.section("thug1_levels", section))) {
            const level = COMPAT.edits.get(code.toLowerCase()) ?? {};
            (level as Record<string, unknown>)[section] = data;
            COMPAT.edits.set(code.toLowerCase(), level);
        }
    }
}

// --- TP-31: THUGPRO's network spawns (tools/compat/thugpro/thug1_levels.json "spawns") ------------------------------
//
// THUG2 starts player n at the n-th Multiplayer restart of the node array and respawns at one of the first 8 (Skate
// 0x00566100 -> GetRestartNode 0x0046B790; CSkater::MoveToRandomRestart 0x00580830: Rnd(8)), so for both sides to
// spawn alike the spawn restarts (Multiplayer / Horse / CTF / Team) need the same places, types and order. THUGPRO
// moved, re-typed and renamed a few of THUG1's (and reordered VC's); TP-31 (approved by the user 2026-10-20 - the
// fairness of a competitive start) makes ours the same. The data is extracted from THUGPRO's install by
// `thugpro_compare.ts --extract-thugpro` (thugpro_edits.spawnEdits); the nodes keep our form. A level with an entry
// keeps THUG1's own node order (no Player1-first swap: 0x00566100 looks restarts up by type - Multiplayer, else Player1), and
// its spawns are then put in THUGPRO's order (`order`). Returns a report line.

const NET_SPAWN_TYPES = nodes.NET_RESTART_TYPES;

function nodeText(v: qb.QbValue | undefined, names: ReadonlyMap<number, string>): string {
    return v instanceof Name ? (names.get(v.value) ?? hex8(v.value)) : "";
}

function spawnTypes(n: Struct, names: ReadonlyMap<number, string>): string[] {
    const types = n.get("restart_types");
    return Array.isArray(types) ? types.map((t) => nodeText(t, names)) : [];
}

function isSpawnNode(n: Struct, names: ReadonlyMap<number, string>): boolean {
    return (
        nodes.classOf(n, names) === "restart" && spawnTypes(n, names).some((t) => NET_SPAWN_TYPES.has(t.toLowerCase()))
    );
}

const nameEntry = (names: Map<number, string>, text: string): Name => {
    const value = qb.checksum(text);
    if (!names.has(value)) names.set(value, text);
    return new Name(value);
};

export function applySpawns(converted: Struct[], names: Map<number, string>, edits: SpawnEdits, code: string): string {
    const byName = new Map<string, Struct>();
    for (const n of converted) byName.set(nodeText(n.get("Name"), names).toLowerCase(), n);
    const node = (name: string): Struct => {
        const n = byName.get(name.toLowerCase());
        if (n === undefined) throw new FormatError(`${code}: TP-31 names ${name}, not a node of the level`);
        return n;
    };
    const done: string[] = [];
    for (const [name, place] of Object.entries(edits.moved ?? {})) {
        const n = node(name);
        n.set("Pos", new qb.Vector("vector", place.pos));
        n.set("Angles", new qb.Vector("vector", place.angles));
        done.push(`${name} moved`);
    }
    for (const [name, types] of Object.entries(edits.types ?? {})) {
        node(name).set(
            "restart_types",
            types.map((t) => nameEntry(names, t)),
        );
        done.push(`${name} [${types.join(" ")}]`);
    }
    for (const name of edits.removed ?? []) {
        const n = node(name);
        n.set(
            "restart_types",
            spawnTypes(n, names)
                .filter((t) => !NET_SPAWN_TYPES.has(t.toLowerCase()))
                .map((t) => nameEntry(names, t)),
        );
        done.push(`${name} no spawn`);
    }
    for (const [old, now] of Object.entries(edits.renamed ?? {})) {
        node(old).set("Name", nameEntry(names, now));
        done.push(`${old} -> ${now}`);
    }
    if (edits.added?.length)
        throw new FormatError(`${code}: TP-31 adds spawns - not handled (none in THUGPRO's today)`);
    if (edits.order !== undefined) {
        // (the spawns' slots in the array, refilled in THUGPRO's order; every node's Links follow)
        const slots = converted.flatMap((n, i) => (isSpawnNode(n, names) ? [i] : []));
        const wanted = edits.order.map((name) => converted.indexOf(node(name)));
        const sorted = [...wanted].sort((a, b) => a - b);
        if (sorted.join(",") !== slots.join(",")) {
            throw new FormatError(`${code}: TP-31's spawn order names other restarts than the level's spawns`);
        }
        const moveTo = new Map(wanted.map((from, k) => [from, slots[k]!] as const)); // old index -> new
        const before = [...converted];
        for (const [from, to] of moveTo) converted[to] = before[from]!;
        const links = qb.checksum("Links");
        for (const n of converted) {
            n.entries = n.entries.map(([k, v]) => [
                k,
                k !== null && k.value === links && Array.isArray(v)
                    ? v.map((x) => (typeof x === "number" && moveTo.has(x) ? moveTo.get(x)! : x))
                    : v,
            ]);
        }
        if (wanted.some((from, k) => from !== slots[k])) done.push(`spawn order as THUGPRO's (${slots.length})`);
    }
    return `  TP-31 spawns: ${done.length ? done.join(", ") : "THUG1's order kept"}`;
}

// A renamed spawn still named by a script would lose its target: refused.
export function checkRenamed(
    edits: SpawnEdits | undefined,
    scripts: ReadonlyMap<number, Uint8Array>,
    code: string,
): void {
    for (const old of Object.keys(edits?.renamed ?? {})) {
        const bytes = Buffer.alloc(4);
        bytes.writeUInt32LE(qb.checksum(old));
        for (const body of scripts.values()) {
            if (Buffer.from(body).includes(bytes))
                throw new FormatError(`${code}: TP-31 renames ${old}, a script names it`);
        }
    }
}

setCompat("thugpro"); // (the default, also for the tools that import this one: thugpro_compare, migrate_layouts)

// The checksums of THUG2's own load scripts (its Levels.qb, Data\pre\qb_scripts.prx).
export function thug2LoadScripts(thug2: string): Set<number> {
    const data = prx.find(pyJoin(thug2, "Data", "pre", "qb_scripts.prx"), "scripts\\game\\levels.qb");
    if (data === null) throw new TypeError(`${thug2}: no scripts\\game\\levels.qb in qb_scripts.prx`);
    return new Set(new qb.QbFile(data, "Levels.qb", true).scripts.keys());
}

// THUG1's own name for the level: its level structure's level= when the structure's load script is Load_<level>.
export function ownName(struct: Struct | undefined): string | null {
    const level = struct?.get("level");
    const load = struct?.get("load_script");
    if (typeof level !== "string" || !(load instanceof Name) || load.value !== qb.checksum(`Load_${level}`))
        return null;
    return level.toLowerCase();
}

// The level's THUG2 name (its folder and files, Load_<name> / Level_<name>, <name>_NodeArray).
export function levelName(
    code: string,
    struct: Struct | undefined,
    thug2Loads: ReadonlySet<number>,
    legacy = false,
): string {
    const prefixed = `thug1_${code.toLowerCase()}`;
    if (legacy) return prefixed;
    const chosen = COMPAT.levelNames.get(code.toLowerCase());
    if (chosen !== undefined) return chosen;
    const own = ownName(struct);
    if (own === null || thug2Loads.has(qb.checksum(`Load_${own}`))) return prefixed;
    return own;
}

// The sky's THUG2 name: the level's name + what follows the level's code in THUG1's (SE_Sky -> hh_sky), else
// thug1_<sky>.
export function skyName(code: string, name: string, skyCode: string): string {
    if (skyCode.toLowerCase().startsWith(code.toLowerCase())) return name + skyCode.slice(code.length).toLowerCase();
    return `thug1_${skyCode.toLowerCase()}`;
}

// Every level's THUG2 name ({code in lower case: name}) for the tools that look at converted levels.
export function levelNames(thug1: string, thug2: string, legacy = false): Map<string, string> {
    const structs = levelStructs({ thug1 });
    const loads = thug2LoadScripts(thug2);
    return new Map(
        LEVELS.map(([code]) => [code.toLowerCase(), levelName(code, structs.get(code.toLowerCase()), loads, legacy)]),
    );
}

export function prePath(args: { thug1: string }, name: string): string | null {
    const file = pyJoin(resolveInstall(args.thug1, "thug1"), "Data", "pre", name);
    if (fs.existsSync(file)) return file;
    const folder = path.win32.dirname(file);
    for (const entry of fs.readdirSync(folder)) {
        if (entry.toLowerCase() === name.toLowerCase()) return pyJoin(folder, entry);
    }
    return null;
}

export type Files = Map<string, Buffer>;

export function levelFiles(args: { thug1: string }, code: string): Files {
    // The level's files from its archives: {name in lower case: data} for levels\<code>\* and levels\<code>_sky\*
    // (scene, textures, collision, qb).
    const files: Files = new Map();
    const prefix = `levels\\${code}\\`.toLowerCase();
    const skyPrefix = `levels\\${code}_sky\\`.toLowerCase();
    const wanted = (name: string): boolean => {
        const low = name.toLowerCase().replaceAll("/", "\\");
        return low.startsWith(prefix) || low.startsWith(skyPrefix);
    };
    for (const archive of [`${code}scn.pre`, `${code}col.pre`, `${code}.pre`]) {
        const file = prePath(args, archive);
        if (!file) continue;
        for (const [name, data] of prx.entries(file, wanted)) files.set(name.toLowerCase().replaceAll("/", "\\"), data);
    }
    return files;
}

export function levelStructs(args: { thug1: string }): Map<string, Struct> {
    // THUG1's Levels.qb (in qb.pre): its level structures by their level= name.
    const data = prx.find(prePath(args, "qb.pre")!, "scripts\\game\\levels.qb");
    if (data === null) throw new TypeError("qb.pre: scripts\\game\\levels.qb not found");
    const q = new qb.QbFile(data, "Levels.qb", true);
    const found = new Map<string, Struct>();
    for (const [, value] of q.globals) {
        if (value instanceof Struct) {
            const level = value.get("level");
            if (typeof level === "string" && !found.has(level.toLowerCase())) found.set(level.toLowerCase(), value);
        }
    }
    return found;
}

function need(files: Files, key: string): Buffer {
    const data = files.get(key);
    if (data === undefined) throw new RangeError(`KeyError: '${key}'`);
    return data;
}

const baseOf = (code: string): string => `levels\\${code.toLowerCase()}\\${code.toLowerCase()}`;

export function sceneSectors(files: Files, code: string): Set<number> {
    // The checksums of the sectors the converted scene has (geometry.convertScene leaves the skinned ones out).
    const base = baseOf(code);
    const scene1 = thug1.readScene(need(files, `${base}.scn.xbx`), `${base}.scn.xbx`);
    return new Set(
        scene1.sectors.filter((s) => !(s.flags & thug2Scene.SEC_WEIGHTS || s.bone !== -1)).map((s) => s.checksum),
    );
}

export function convertGeometry(
    files: Files,
    code: string,
    outDir: string,
    outName: string,
    report: string[],
    keepTriggers: ((checksum: number) => boolean) | null = null,
): void {
    // The scene, textures and (for a level, not a sky) collision of levels\<code>\<code>.*.
    const base = baseOf(code);
    const warnings: string[] = [];
    if (!files.has(`${base}.scn.xbx`) || !files.has(`${base}.tex.xbx`)) {
        throw new FormatError(`${code}: no scene / textures in the archives`);
    }
    const scene1 = thug1.readScene(need(files, `${base}.scn.xbx`), `${base}.scn.xbx`);
    const scene = geometry.convertScene(scene1, warnings, scene1.materials);
    const textures = tex.readTex(need(files, `${base}.tex.xbx`), `${base}.tex.xbx`);
    const converted = tex.depalettise(textures);
    let col = null;
    let editReport: string[] = [];
    if (keepTriggers !== null) {
        if (!files.has(`${base}.col.xbx`)) throw new FormatError(`${code}: no collision in the archives`);
        const col1 = thug1.readCol(need(files, `${base}.col.xbx`), `${base}.col.xbx`);
        col = thug1.toThug2Col(col1, keepTriggers, faceFlagsOf(code), base);
        // (TP-37 ... TP-39: THUGPRO's collision edits - hand_edits.ts)
        const edits = COMPAT.edits.get(code.toLowerCase());
        if (edits !== undefined) [col, editReport] = handEdits.applyColEdits(col, edits, code);
    }
    common.writeLevel(outDir, outName, scene, textures, col);
    common.reportLevel(report, outName, scene, textures, col, warnings, converted);
    report.push(...editReport);
}

export interface LevelNodes {
    triggers: Set<number>; // the checksums of the nodes whose collision keeps its trigger faces
    starts: Map<string, string>; // the level structure's startup_script / setup_script kept
    recheck: (levelDir: string) => string[];
}

// What is new in THUGPRO for one level (TPN-THUG1-2 ... TPN-THUG1-4, thugpro_new.ts): its entries, THUGPRO's side of the level (its
// node array and level scripts, read from the install) and whether a model could be had.
export interface NewContext {
    level: thugproNew.NewLevel;
    side: Side;
    install: thugproNew.Install;
    modelOk: (model: string) => boolean;
}

// Every node name of the level, THUGPRO's and ours (a script naming one we haven't is refused).
function levelNodesOf(side: Side, converted: readonly Struct[]): Set<number> {
    const out = new Set<number>();
    for (const n of [...side.nodes, ...converted]) {
        const v = n.get("Name");
        if (v instanceof Name) out.add(v.value);
    }
    return out;
}

// TPN-THUG1-3's scripts for one level: the added ones that can run (decided before the node array is written: a node whose
// added trigger script can't loses it), the set / emptied ones (applied to the compiled .qb), the report.
interface NewScripts {
    adds: Map<number, Buffer>;
    later: { e: thugproNew.NewEntry; name: number; code: Buffer }[];
    levelNodes: Set<number>;
    report: string[];
}

// (scripts the converter writes itself: thug1_port.levelScriptsNs, nodes.loadTerrainNs)
const GENERATED = ["LoadTerrain", "LoadAllParticleTextures", "load_level_anims"].map(qb.checksum);

// The nodes a THUGPRO script may name: our converted level's, but not geometry the scene has no sector for (Create /
// Kill / Visible of one reads through a null - as script_port's undrawn).
function nodeNamesOf(
    converted: readonly Struct[],
    sectors: ReadonlySet<number>,
    names: ReadonlyMap<number, string>,
): Set<number> {
    const out = new Set<number>();
    for (const n of converted) {
        const v = n.get("Name");
        if (v instanceof Name && (nodes.classOf(n, names) !== "levelgeometry" || sectors.has(v.value)))
            out.add(v.value);
    }
    return out;
}

function prepareNewScripts(
    tpNew: NewContext,
    port: thug1Port.Thug1Port,
    converted: Struct[],
    scripts: ReadonlyMap<number, Buffer>,
    kills: ReadonlyMap<number, string>,
    names: Map<number, string>,
    sectors: ReadonlySet<number>,
): NewScripts {
    const out: NewScripts = { adds: new Map(), later: [], report: [], levelNodes: levelNodesOf(tpNew.side, converted) };
    const files = new Map<string, Map<number, Buffer>>();
    const addEntries = new Map<number, thugproNew.NewEntry>();
    for (const e of tpNew.level.entries) {
        if (e.kind !== "script") continue;
        const name = handEdits.checksumOf(e.target);
        const key = `${e.archive ?? tpNew.level.archive}|${e.file}`.toLowerCase();
        let raw = files.get(key);
        if (raw === undefined) {
            const data = tpNew.install.read(tpNew.level, e);
            if (typeof data === "string") {
                out.report.push(`  THUGPRO new, left out: ${e.id} ${e.op} ${e.target}: ${data}`);
                continue;
            }
            raw = thugproNew.rawScripts(data);
            for (const [k, v] of new qb.QbFile(data, e.file ?? "", true).names) if (!names.has(k)) names.set(k, v);
            files.set(key, raw);
        }
        const code = raw.get(name);
        if (code === undefined || thugproNew.crc(code) !== e.crc32) {
            const why = code === undefined ? `not in THUGPRO's ${e.file}` : "THUGPRO's script differs (checksum)";
            out.report.push(`  THUGPRO new, left out: ${e.id} ${e.op} ${e.target}: ${why}`);
            continue;
        }
        if (e.op === "add") {
            out.adds.set(name, code);
            addEntries.set(name, e);
        } else out.later.push({ e, name, code });
    }
    for (const [k, v] of tpNew.side.names) if (!names.has(k)) names.set(k, v);
    // (the added scripts that can run in THUG2 here: until none is refused, as one may call another)
    const base = [...scripts.keys(), ...kills.keys(), ...GENERATED];
    const nodeNames = nodeNamesOf(converted, sectors, names);
    let refused = true;
    while (refused) {
        refused = false;
        for (const [s, code] of out.adds) {
            const why = thugproNew.scriptCheck(code, {
                thug2: port.thug2,
                defined: new Set([...base, ...out.adds.keys()]),
                nodeNames,
                levelNodes: levelNodesOf(tpNew.side, converted),
                names,
            });
            if (why === null) continue;
            out.adds.delete(s);
            out.report.push(`  THUGPRO new, left out: TPN-THUG1-3 add ${addEntries.get(s)!.target}: ${why}`);
            refused = true;
        }
    }
    // (a node of the level running an added script that couldn't be added: its TriggerScript taken off)
    for (const n of converted) {
        const t = n.get("TriggerScript");
        if (t instanceof Name && addEntries.has(t.value) && !out.adds.has(t.value)) n.remove("TriggerScript");
    }
    return out;
}

// TPN-THUG1-3 on the compiled .qb: THUGPRO's set scripts replace ours, emptied ones are emptied, added ones are added.
function applyNewScripts(
    s: NewScripts,
    data: Buffer,
    port: thug1Port.Thug1Port,
    converted: readonly Struct[],
    names: Map<number, string>,
    sectors: ReadonlySet<number>,
): [Buffer, string[]] {
    const defined = new Set(new qb.QbFile(data, "qb", true).scripts.keys());
    const replaced = new Map<number, Buffer>();
    const lines: string[] = [];
    const counts = { add: 0, set: 0, remove: 0 };
    const nodeNames = nodeNamesOf(converted, sectors, names);
    for (const { e, name, code } of s.later) {
        if (!defined.has(name)) {
            lines.push(
                `  THUGPRO new: ${e.id} ${e.op} ${e.target}: ERROR - our level has no such script (not applied)`,
            );
            continue;
        }
        if (e.op === "set") {
            const why = thugproNew.scriptCheck(code, {
                thug2: port.thug2,
                defined: new Set([...defined, ...s.adds.keys()]),
                nodeNames,
                levelNodes: s.levelNodes,
                names,
            });
            if (why !== null) {
                lines.push(`  THUGPRO new, left out: ${e.id} set ${e.target}: ${why}`);
                continue;
            }
        }
        replaced.set(name, e.op === "remove" ? thugproNew.emptyScript(name) : code);
        counts[e.op] += 1;
    }
    for (const [name, code] of s.adds) {
        if (defined.has(name)) {
            lines.push(`  THUGPRO new: TPN-THUG1-3 add ${qb.nameText(name, names)}: ERROR - our level has it already`);
            continue;
        }
        replaced.set(name, code);
        counts.add += 1;
    }
    const out = thugproNew.spliceScripts(data, replaced, (d, m) => scriptPort.appendScripts(d, m, names));
    lines.push(`  TPN-THUG1-3 THUGPRO's scripts: ${counts.add} added, ${counts.set} set, ${counts.remove} emptied`);
    return [out, lines];
}

export function convertLevelNodes(
    files: Files,
    code: string,
    name: string,
    ns: string,
    outDir: string,
    report: string[],
    port: thug1Port.Thug1Port | null = null,
    struct: Struct | undefined = undefined,
    sectorsIn: Set<number> | null = null,
    tpNew: NewContext | null = null,
): LevelNodes {
    const pc = COMPAT.portChanges;
    // sectorsIn: the converted scene's sector checksums (sceneSectors).
    const sectors = sectorsIn ?? sceneSectors(files, code);
    // The node array: kept nodes and kill zones (THUG2's SK3_KillSkater) as <name>.qb; with a port
    // (thug1_port.Thug1Port) also the level's peds, vehicles, objects, particles and its scripts (thug1_port.ts).
    // Returns the checksums of the nodes whose collision keeps its trigger faces, and the level structure's
    // startup_script / setup_script kept ({key: name}).
    const base = baseOf(code);
    if (!files.has(`${base}.qb`)) throw new FormatError(`${code}: no ${base}.qb`);
    const q = new qb.QbFile(need(files, `${base}.qb`), `${base}.qb`, true);
    const scriptQbs = [q];
    if (files.has(`${base}_scripts.qb`)) {
        scriptQbs.push(new qb.QbFile(need(files, `${base}_scripts.qb`), `${base}_scripts.qb`, true));
    }
    const names = new Map<number, string>();
    for (const f of [...scriptQbs].reverse()) for (const [k, v] of f.names) names.set(k, v);
    const nodeArray = q.get("NodeArray");
    if (!Array.isArray(nodeArray)) throw new FormatError(`${base}: no NodeArray`);
    const array = nodes.expandIncludes(nodeArray, new Map(q.globals.map(([k, v]) => [k.value, v])));
    // (TP-32 ... TP-36: THUGPRO's node edits, made to THUG1's array - converted from there as THUG1's own)
    const edits = COMPAT.edits.get(code.toLowerCase());
    if (edits !== undefined) report.push(...handEdits.applyNodeEdits(array, names, edits, code));
    // (TPN-THUG1-2: THUGPRO's own nodes - thugpro_new.ts; kept whole by the node pass)
    let forced = new Set<Struct>();
    if (tpNew !== null) {
        const r = thugproNew.applyNodes(array, names, tpNew.level, tpNew.side, tpNew.modelOk);
        forced = r.forced;
        report.push(...r.report.map((l) => `  ${l}`));
        for (const s of r.skipped) report.push(`  THUGPRO new, left out: ${s}`);
    }
    const restarts = new Set<number>();
    for (const n of array) {
        const nodeName = n instanceof Struct ? n.get("Name") : undefined;
        if (n instanceof Struct && nodeName instanceof Name && nodes.classOf(n, names) === "restart") {
            restarts.add(nodeName.value);
        }
    }
    let kills = nodes.killCommands(scriptQbs, restarts, names);
    let made: thug1Port.NodePass | null = null;
    let scripts = new Map<number, Buffer>();
    let extra = "";
    let starts = new Map<string, string>();
    let kept: qb.QbValue[] = array;
    const wanted = new Set<number>();
    let levelSources: scriptPort.Source[] = [];
    if (port !== null) {
        levelSources = [".qb", "_scripts.qb", "_sfx.qb"]
            .filter((suffix) => files.has(base + suffix))
            .map((suffix) => new scriptPort.Source(need(files, base + suffix), thug1Port.LEVEL, true));
        for (const s of [...port.shared, ...levelSources]) {
            for (const [value, text] of s.names) if (!names.has(value)) names.set(value, text);
        }
        // (PC-THUG1-4: our own change - port_changes.ts)
        const duplicates = pc.on("PC-THUG1-4") ? thug1Port.resolveDuplicateTriggers(array, scriptQbs, names) : 0;
        made = new thug1Port.NodePass(port, array, names, sectors, forced);
        if (duplicates) made.counts.set("DuplicateTrigger nodes given their link's script", duplicates);
        kept = made.kept;
        for (const n of kept) {
            const script = n instanceof Struct ? n.get("TriggerScript") : undefined;
            if (script instanceof Name && !kills.has(script.value)) wanted.add(script.value);
        }
    }
    const spawnEdits = COMPAT.spawns.get(code.toLowerCase()); // (TP-31)
    const convertedStats = nodes.convertNodes(
        kept,
        names,
        kills,
        ["pos"],
        made ? made.asIs : [],
        wanted,
        spawnEdits === undefined && pc.on("PC-THUG1-3"),
    );
    let converted = convertedStats[0];
    const stats = convertedStats[1];
    if (spawnEdits !== undefined) {
        report.push(applySpawns(converted, names, spawnEdits, code));
        for (const [script, call] of kills) {
            for (const old of Object.keys(spawnEdits.renamed ?? {})) {
                if (call.toLowerCase().includes(old.toLowerCase())) {
                    throw new FormatError(`${code}: TP-31 renames ${old}, the kill ${hex8(script)} sends to it`);
                }
            }
        }
    }
    if (pc.on("PC-THUG1-1")) kills = nodes.addKillRestarts(converted, kills, names);
    const triggers = nodes.killTriggerNames(converted, kills);
    let scriptReport = "";
    let newScripts: NewScripts | null = null;
    if (port !== null && made !== null) {
        const adds =
            tpNew === null
                ? []
                : tpNew.level.entries
                      .filter((e) => e.kind === "script" && e.op === "add")
                      .map((e) => handEdits.checksumOf(e.target));
        const ported = thug1Port.portScripts(port, levelSources, converted, array, kills, names, struct, sectors, adds);
        scripts = ported.scripts;
        if (tpNew !== null) {
            newScripts = prepareNewScripts(tpNew, port, converted, scripts, kills, names, sectors);
            report.push(...newScripts.report);
        }
        checkRenamed(spawnEdits, scripts, code);
        starts = ported.starts;
        scriptReport = ported.report;
        // (the sectors whose trigger script runs keep their trigger faces)
        for (const n of converted) {
            const nodeName = n.get("Name");
            if (
                n.get("TriggerScript") instanceof Name &&
                nodeName instanceof Name &&
                nodes.classOf(n, names) === "levelgeometry"
            ) {
                triggers.add(nodeName.value);
            }
        }
        const anims = q
            .calls(qb.checksum("load_level_anims"))
            .filter(([c]) => port.thug2.scripts.has(c))
            .map(([c]) => qb.nameText(c, names));
        extra =
            ported.globalsText +
            thug1Port.levelScriptsNs(made, anims, new Set([...scripts.keys(), ...port.thug2.scripts]));
    }
    let includes = "";
    if (port !== null) [converted, includes] = thug1Port.compressIncludes(converted, names, name, sectors);
    let text = includes + nodes.nodeArrayNs(`${name}_NodeArray`, converted, names, kills) + extra;
    // (a LevelGeometry TrickObject / Cluster whose sector the scene hasn't crashes ParseNodeArray)
    const [stripped, cleared] = nodes.stripMissingTrickObjects(text, sectors);
    text = stripped;
    fs.mkdirSync(outDir, { recursive: true });
    const qbPath = pyJoin(outDir, `${name}.qb`);
    // (network games: path followers, peds and cars kept out - no path manager or ped animations there -, restarts for the players)
    // (nodes.netGameNodes, its two parts: PC-THUG1-14 required, PC-THUG1-2 our own change - port_changes.ts)
    pc.required("PC-THUG1-14");
    const [absentText, absent] = nodes.absentInNetGames(text, scripts);
    const [netText, spawns] = pc.on("PC-THUG1-2") ? nodes.netRestarts(absentText) : [absentText, 0];
    const marked = absent + spawns;
    text = netText;
    if (marked) report.push(`  ${marked} node(s) changed for network games`);

    let newReported = false;
    const writeQb = (source: string): void => {
        common.nsCompile(ns, source, qbPath);
        if (scripts.size) fs.writeFileSync(qbPath, scriptPort.appendScripts(fs.readFileSync(qbPath), scripts, names));
        if (newScripts !== null && port !== null) {
            // (TPN-THUG1-3: THUGPRO's level scripts set / emptied / added in the compiled .qb)
            const [data, lines] = applyNewScripts(newScripts, fs.readFileSync(qbPath), port, converted, names, sectors);
            fs.writeFileSync(qbPath, data);
            if (!newReported) report.push(...lines);
            newReported = true;
        }
    };

    writeQb(text);
    report.push(
        `  nodes: ${converted.length} of ${array.length} kept ${pyRepr(stats)}; ${triggers.size} kill zones; ` +
            `TrickObject taken off ${cleared} geometry nodes without a sector`,
    );
    if (made !== null) {
        const counts = [...made.counts].sort((a, b) => (a[0] < b[0] ? -1 : a[0] > b[0] ? 1 : a[1] - b[1]));
        report.push(`  made: ${counts.map(([k, v]) => `${k} ${v}`).join(", ")}`);
        report.push(`  ${scriptReport}`);
    }
    // (cmdConvert redoes the .qb with this once the collision is written: nodes.groundRestarts)
    const recheck = (levelDir: string): string[] => {
        if (!pc.on("PC-THUG1-5")) return []; // (our own change - port_changes.ts)
        const [changed, groundReport] = nodes.groundRestartsAt(text, levelDir, name);
        if (groundReport.length) writeQb(changed);
        return groundReport;
    };
    return { triggers, starts, recheck };
}

// Python's str.title() (ASCII): each run of letters capitalised.
function title(text: string): string {
    return text.replace(/[A-Za-z]+/g, (word) => word[0]!.toUpperCase() + word.slice(1).toLowerCase());
}

export function cmdConvert(args: Args): void {
    const out = args.out || pyJoin(args.thug2, STAGING);
    if (!fs.existsSync(args.ns)) pyExit(`${args.ns} not found: build it with tools\\build_neverscript.ps1`);
    if (!prePath(args, "qb.pre")) pyExit(`${args.thug1}: THUG1's Data\\pre not found`);
    const structs = levelStructs(args);
    setCompat(args.compat, !args.noThugproNew, !args.noPortChanges);
    for (const [id, what] of portChanges.IMPLEMENTED) {
        const e = COMPAT.portChanges.entries.get(id);
        if (e?.required) continue;
        if (!COMPAT.portChanges.on(id)) print(`our own change ${id} off: ${what}`);
    }
    const thug2Loads = thug2LoadScripts(args.thug2);
    // (--static: only the geometry's nodes, restarts, rails and kill zones, as before the scripts were brought)
    const port = args.static ? null : new thug1Port.Thug1Port(args.thug2, prePath(args, "qb.pre")!);
    const wanted = args.levels.length ? args.levels : LEVELS.filter((info) => info[5]).map((info) => info[0]);
    if (fs.existsSync(out) && fs.statSync(out).isDirectory()) fs.rmSync(out, { recursive: true });
    fs.mkdirSync(out, { recursive: true });
    // TPN-THUG1-1 ... TPN-THUG1-4: what is new in THUGPRO, read from its install (thugpro_new.ts); missing: warned, left out
    const install = new thugproNew.Install(args.thugpro);
    const okModels = new Set<string>();
    if (COMPAT.new.size && port !== null) {
        if (!install.exists()) {
            print(
                `warning: THUGPRO's install not found (${args.thugpro}): what is new in THUGPRO (TPN-THUG1-1 ... TPN-THUG1-4) left out`,
            );
            COMPAT.new = new Map();
        } else {
            port.extraRoots.push(out);
            okModels.clear();
            for (const requested of wanted) {
                const level = COMPAT.new.get(requested.toLowerCase());
                if (level === undefined) continue;
                const bad = new Set<string>();
                for (const e of level.entries) {
                    if (e.kind !== "asset") continue;
                    const data = install.read(level, e);
                    const why =
                        typeof data === "string" ? data : thugproNew.crc(data) !== e.crc32 ? "checksum differs" : null;
                    const base = e.target.replace(/\.[^.\\]+(\.xbx)?$/, "");
                    if (why !== null || typeof data === "string") {
                        print(`warning: ${requested} ${e.id} ${e.target}: ${why} - left out`);
                        bad.add(base);
                        continue;
                    }
                    const target = pyJoin(out, e.target);
                    fs.mkdirSync(path.win32.dirname(target), { recursive: true });
                    fs.writeFileSync(target, data);
                }
                for (const model of thugproNew.assetModels(level))
                    if (!bad.has(`models\\${model.replace(/\.(mdl|skin)$/, "")}`)) okModels.add(model);
            }
            for (const m of okModels) port.models.add(m);
        }
    }
    const modelOk = (model: string): boolean => {
        const k = model.toLowerCase().replaceAll("/", "\\");
        if (okModels.has(k)) return true;
        const file = pyJoin(args.thug2, "Data", "models", `${k}.xbx`);
        return fs.existsSync(file);
    };
    const manifest: { levels: Record<string, unknown>[] } = { levels: [] };
    let report: string[] = [];
    for (const requested of wanted) {
        const info = BY_CODE.get(requested.toLowerCase());
        if (!info) {
            pyExit(`${requested}: not a THUG1 level this tool knows (${LEVELS.map((c) => c[0]).join(", ")})`);
        }
        const [code, sign, location, icon, levelNum] = info;
        print("converting", code);
        const files = levelFiles(args, code);
        const struct = structs.get(code.toLowerCase());
        const name = levelName(code, struct, thug2Loads, args.legacyNames);
        const levelDir = pyJoin(out, "levels", name);
        let nodeReport: string[] = [];
        const newLevel = COMPAT.new.get(code.toLowerCase());
        let tpNew: NewContext | null = null;
        if (newLevel !== undefined && port !== null) {
            try {
                tpNew = { level: newLevel, side: thugproSide(args.thugpro, newLevel.key), install, modelOk };
            } catch (e) {
                print(
                    `warning: ${code}: THUGPRO's ${newLevel.archive} unreadable (${String(e)}) - TPN-THUG1-2 ... TPN-THUG1-4 left out`,
                );
            }
        }
        const { triggers, starts, recheck } = convertLevelNodes(
            files,
            code,
            name,
            args.ns,
            levelDir,
            nodeReport,
            port,
            struct,
            null,
            tpNew,
        );
        // (TPN-THUG1-4: THUGPRO's host-toggled level files, as they are)
        for (const e of tpNew?.level.entries ?? []) {
            if (e.kind !== "file") continue;
            const data = install.read(tpNew!.level, e);
            if (typeof data === "string" || thugproNew.crc(data) !== e.crc32) {
                nodeReport.push(
                    `  THUGPRO new, left out: ${e.id} ${e.target}: ${typeof data === "string" ? data : "checksum differs"}`,
                );
                continue;
            }
            const target = pyJoin(out, e.target.replaceAll("<level>", name));
            fs.mkdirSync(path.win32.dirname(target), { recursive: true });
            fs.writeFileSync(target, data);
            nodeReport.push(
                `  TPN-THUG1-4 ${e.target.replaceAll("<level>", name)} (THUGPRO's host-toggled level changes)`,
            );
        }
        convertGeometry(files, code, levelDir, name, report, (checksum) => triggers.has(checksum));
        nodeReport = nodeReport.concat(recheck(levelDir));
        report = report.concat(nodeReport);

        // The sky: Levels.qb's sky=, else <code>_sky when the scene archive has one.
        let sky: string | null = null;
        const skyValue = struct?.get("sky");
        const skyCode = typeof skyValue === "string" ? skyValue : `${code}_sky`;
        if (files.has(`${baseOf(skyCode)}.scn.xbx`)) {
            sky = skyName(code, name, skyCode);
            convertGeometry(files, skyCode, pyJoin(out, "levels", sky), sky, report);
        }
        const structName = struct?.get("name");
        const display = typeof structName === "string" ? structName : title(sign);
        const defaultStats = struct?.get("default_stats");
        const entry: Record<string, unknown> = {
            folder: code,
            level: name,
            sky,
            display: `THUG1 ${sign}`,
            name: display,
            game: "THUG1",
            location,
            icon,
            level_num: levelNum,
            default_stats: typeof defaultStats === "number" ? defaultStats : 6,
            lighting: struct ? common.lighting(struct) : {},
        };
        for (const [k, v] of starts) entry[k] = v; // (startup_script / setup_script: THUG1's own, brought along)
        manifest.levels.push(entry);
    }
    portedLevels.writeManifest(pyJoin(out, portedLevels.MANIFEST), manifest);
    print(report.join("\n"));
    print(`wrote ${out} (${manifest.levels.length} levels); install with tools\\menu_level\\make_menu_level.ts`);
}

// --- self-check -------------------------------------------------------------------------------------------------

export function cmdSelfcheck(args: Args): number {
    const pre = pyJoin(args.thug2, "Data", "pre");
    let [checked, failures] = common.thug2RoundTrips(pre, [
        "LAscn.prx",
        "LAcol.prx",
        "TestLevelscn.prx",
        "TestLevelcol.prx",
        ...args.extra,
    ]);
    const thug1Pre = pyJoin(resolveInstall(args.thug1, "thug1"), "Data", "pre");
    // Every level file of every THUG1 level archive (scenes, textures, collision, the levels' qb files).
    for (const archive of fs.readdirSync(thug1Pre).sort()) {
        const lowArchive = archive.toLowerCase();
        if (!lowArchive.endsWith(".pre") || lowArchive === "qb.pre") continue;
        const inLevels = (n: string): boolean => n.toLowerCase().replaceAll("/", "\\").startsWith("levels\\");
        for (const [name, data] of prx.entries(pyJoin(thug1Pre, archive), inLevels)) {
            const low = name.toLowerCase();
            let what: string;
            try {
                if (low.endsWith(".scn.xbx")) {
                    const s = thug1.readScene(data, name);
                    what = `${s.materials.length} materials, ${s.sectors.length} sectors`;
                } else if (low.endsWith(".col.xbx")) {
                    const c = thug1.readCol(data, name);
                    for (const o of c.objects) thug1.checkTree(o, name);
                    what = `${c.objects.length} objects`;
                } else if (low.endsWith(".tex.xbx")) {
                    const t = tex.readTex(data, name);
                    if (!tex.writeTex(t).equals(data)) throw new FormatError("not the same written again");
                    what = `${t.textures.length} textures`;
                } else if (low.endsWith(".qb")) {
                    const q = new qb.QbFile(data, name, true);
                    what = `${q.globals.length} globals`;
                } else {
                    continue;
                }
            } catch (e) {
                // (Python's FormatError, ValueError, IndexError)
                if (!(e instanceof FormatError || e instanceof RangeError)) throw e;
                print(`FAIL ${archive} ${name}: ${e.message}`);
                failures += 1;
                continue;
            }
            checked += 1;
            print(`ok   ${archive} ${name}: ${what}`);
        }
    }
    print(`${checked} files checked, ${failures} failures`);
    return failures ? 1 : 0;
}

export function main(argv: readonly string[] = process.argv.slice(2)): number {
    const parsed = parseConverterArgs(
        "thug1_levels.ts",
        ["convert", "selfcheck"],
        {
            thug2: { kind: "string", default: "C:\\Games\\THUG2\\Game" },
            thug1: { kind: "string", default: "C:\\Games\\THUG1" },
            out: { kind: "string" },
            ns: { kind: "string", default: common.NS },
            static: { kind: "flag" },
            extra: { kind: "list" },
            // compatibility profile (tools/compat): thugpro - level names as THUGPRO's THUG1 ports (default), or none
            compat: { kind: "string", default: "thugpro" },
            "legacy-names": { kind: "flag" }, // thug1_<code> for every level, as before
            // what is new in THUGPRO (thugpro_new: TPN-THUG1-1 ... TPN-THUG1-4) is read from its install; --no-thugpro-new leaves
            // it out (the hand edits TP-30 ... TP-39 stay)
            thugpro: { kind: "string", default: pyJoin(process.env.LOCALAPPDATA ?? "", "THUG Pro") },
            "no-thugpro-new": { kind: "flag" },
            // our own changes that THUG2 doesn't need to load the level off (port_changes.ts)
            "no-port-changes": { kind: "flag" },
        },
        argv,
    );
    const args: Args = {
        command: parsed.command,
        levels: parsed.positionals,
        thug2: parsed.strings.get("thug2")!,
        thug1: parsed.strings.get("thug1")!,
        out: parsed.strings.get("out"),
        ns: parsed.strings.get("ns")!,
        static: parsed.flags.get("static")!,
        extra: parsed.lists.get("extra")!,
        compat: parsed.strings.get("compat")!,
        legacyNames: parsed.flags.get("legacy-names")!,
        thugpro: parsed.strings.get("thugpro")!,
        noThugproNew: parsed.flags.get("no-thugpro-new")!,
        noPortChanges: parsed.flags.get("no-port-changes")!,
    };
    if (!(compat.PROFILES as readonly string[]).includes(args.compat)) {
        process.stderr.write(
            `thug1_levels.ts: error: argument --compat: invalid choice: '${args.compat}' (choose from ${compat.PROFILES.join(", ")})\n`,
        );
        pyExit(2);
    }
    // (the install root or the game folder itself: C:\Games\THUG2 -> C:\Games\THUG2\Game - tools/lib/install.ts)
    args.thug2 = resolveInstall(args.thug2, "thug2");
    if (args.command === "selfcheck") return cmdSelfcheck(args);
    cmdConvert(args);
    return 0;
}

if (isMain(import.meta.url)) await runMain(() => main());
