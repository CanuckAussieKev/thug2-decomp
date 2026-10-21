// NOT ORIGINAL - converts Tony Hawk's Pro Skater 4 (PC) levels to THUG2's formats so that THUG2 can load and play
// them (see README.md). Nothing of either game is stored in the repo: the files are made from the user's own copies.
//
//   node tools/thps4_levels/thps4_levels.ts selfcheck [--thug2 C:\Games\THUG2] [--thps4 C:\Games\THPS4]
//   node tools/thps4_levels/thps4_levels.ts convert [levels...] [--thug2 ...] [--thps4 ...] [--out <folder>]
//        [--compat thugpro|none] [--no-thugpro-new] [--no-port-changes]
//   then: node tools/menu_level/make_menu_level.ts   (installs them into the data overlay, with level select signs)
//
// convert writes <thug2>\thug2-decomp-thps4\ (not read by the game): levels\thps4_<lvl>\thps4_<lvl>.scn.xbx /
// .tex.xbx / .col.xbx / .qb (the node array), the skies the same way, and levels.json (what make_menu_level.ts adds to
// Levels.qb and the level select: tools/level_port/ported_levels.ts). make_menu_level.ts copies the levels folder into
// the overlay (<thug2>\thug2-decomp-data), which the DLL serves in place of the game's files.
//
// The THUG2 formats, the geometry conversion, the node arrays and the installing are shared with tools/thug1_levels
// (tools/level_port); this folder has THPS4's own formats (thps4.ts) and its levels.

import * as fs from "node:fs";
import * as path from "node:path";
import * as compat from "../compat/compat.ts";
import { FormatError } from "../level_port/binio.ts";
import * as common from "../level_port/common.ts";
import * as geometry from "../level_port/geometry.ts";
import * as nodes from "../level_port/nodes.ts";
import * as overlayBias from "../level_port/overlay_bias.ts";
import * as pedRigs from "../level_port/ped_rigs.ts";
import * as pedsCars from "../level_port/peds_cars.ts";
import * as portedLevels from "../level_port/ported_levels.ts";
import * as qb from "../level_port/qb.ts";
import { Name, Struct, Vector } from "../level_port/qb.ts";
import * as scriptPort from "../level_port/script_port.ts";
import * as tex from "../level_port/tex.ts";
import { parseConverterArgs } from "../lib/converter_args.ts";
import { isMain, print, pyExit, pyJoin, pyRepr, runMain } from "../lib/py.ts";
import { search, sub } from "../lib/re.ts";
import * as thps4 from "./thps4.ts";
import * as thps4Scripts from "./thps4_scripts.ts";
import * as thugproEdits from "./thugpro_edits.ts";
import * as thugproNew from "./thugpro_new.ts";

export const STAGING = "thug2-decomp-thps4";

// The THPS4 levels converted by default (folder under Game\data\levels): everything with a scene except the park
// editor's sets. Foo has only a node array (no scene).
export const LEVELS = ["Alc", "Cnv", "Hof", "Jnk", "Kon", "Lon", "SF2", "Sch", "Zoo", "SkateShop", "Motox"];
// The level select's numbers for the signs (each sign needs its own; none of them has career records - the info
// panel skips them, as make_menu_level.ts's SKATE SHOP / TEST LEVEL at 18 / 21).
export const SIGN_NUMBERS = [19, 20, 22, 23, 24, 25, 26, 27, 28, 29, 31];
// Every level's peds and cars are brought over (tools/level_port/peds_cars.ts): the skate shop's pros on the
// halfpipe, the people walking by and the cars on the road (also the front end's: make_menu_level.ts copies them);
// every level's walkers and Car_Generic01 cars - but where a port change (PC-THPS4-4, rule peds_cars) leaves them out.
export const pedsCarsWanted = (base: string): boolean => !portChanges("peds_cars", base).length;
// For the level select: where each level is, and a THUG2 sign that suits it.
export const INFO: Readonly<Record<string, readonly [string, string]>> = {
    alc: ["San Francisco Bay, USA", "sign_skatopia"],
    cnv: ["Carnival", "sign_neworleans"],
    hof: ["Chicago, USA", "sign_philly"],
    jnk: ["Shipyard", "sign_airport"],
    kon: ["Kona, Hawaii, USA", "sign_australia"],
    lon: ["London, England", "sign_berlin"],
    sf2: ["San Francisco, USA", "sign_la"],
    sch: ["College", "sign_school"],
    zoo: ["Zoo", "sign_australia"],
    skateshop: ["Skate shop", "sign_training"],
    motox: ["Motocross track", "sign_jam"],
};

// The levels' names in THUG2 (THUGPRO compatibility - docs/pending/thps4-thugpro.md). A network game's change-level
// message (0x46) and start info (0x33) carry only the load script's checksum and the other side runs the script of
// that checksum, so both sides have to name a level alike. THPS4's own names are its levels' folders, as its
// Levels.qb loads them (Load_Alc, level="Alc"...); checksums ignore case, so the level is Load_alc / Level_alc,
// levels\alc\alc.*, alc_NodeArray - what THUGPRO's THPS4 ports use too. None of them is a THUG2 level's name.
// Kept prefixed (thps4_<lvl>): the names THUG2 itself uses - the skate shop (THUG2's own levels\skateshop, and its
// CheckForInvalidLevels turns Load_SkateShop into the main menu); it also stays thps4_skateshop on disk because the
// front end's backdrop loads its scene and sky by those names (src/game/front/menu_backdrop.h, make_menu_level.ts).
// A load script name that is THUGPRO's choice, not THPS4's (the skate shop's Load_practice), comes from the
// compatibility profile tools/compat/thugpro/thps4_levels.json (README.md there: TP-20...; --compat none leaves it out).
export const THUG2_NAMES: ReadonlySet<string> = new Set(["skateshop"]);
// The network-game nodes a level gets from the profile (TP-21: THUGPRO's King of the Hill crown and multiplayer
// spawns in the skate shop, which THPS4 - single player there - has none of): places only (world frame, THUG2's Pos
// axes, and Angles), extracted from THUGPRO's install by thugpro_compare.ts --extract-thugpro; the nodes are made
// here in our own form. `multiplayer`: THPS4's own restarts that become multiplayer spawns too.
export interface Placement {
    pos: number[];
    angles: number[];
}
export interface NetNodes {
    crowns: Placement[];
    restarts: Placement[];
    multiplayer: string[];
}
// THUGPRO's hand edits to its ports (TP-22 ... TP-28: thugpro_edits.ts), by section and level (lower case), and
// whether TP-29 (the skate shop's door walls kept closed, as THUGPRO's) is switched on - off unless the profile says so.
export const COMPAT = {
    loadNames: new Map<string, string>(),
    netNodes: new Map<string, NetNodes>(),
    edits: new Map<thugproEdits.SectionName, Map<string, unknown>>(),
    doorWalls: false,
    thugproNew: [] as thugproNew.NewEntry[],
    newFiles: {} as Record<string, thugproNew.FileRef>,
};

// The compatibility profile's THPS4 data (tools/compat/compat.ts): "thugpro" (the default) or "none".
export function setCompat(name: string, withNew = true): void {
    const profile = compat.load(name);
    // (THUGPRO's own content - thugpro_new, thugpro_new.ts: none with --compat none or --no-thugpro-new)
    COMPAT.thugproNew = withNew ? (compat.thugproNew(profile, "thps4_levels") as unknown as thugproNew.NewEntry[]) : [];
    const section = profile.file("thps4_levels").thugpro_new as
        | { files?: Record<string, thugproNew.FileRef> }
        | undefined;
    COMPAT.newFiles = (withNew ? section?.files : undefined) ?? {};
    COMPAT.loadNames = new Map(
        Object.entries(profile.section("thps4_levels", "load_names")).map(([k, v]) => [k.toLowerCase(), String(v)]),
    );
    COMPAT.netNodes = new Map(
        Object.entries(profile.section("thps4_levels", "net_nodes")).map(([k, v]) => [k.toLowerCase(), v as NetNodes]),
    );
    COMPAT.edits = new Map(
        (Object.keys(thugproEdits.SECTIONS) as thugproEdits.SectionName[]).map((s) => [
            s,
            new Map(Object.entries(profile.section("thps4_levels", s)).map(([k, v]) => [k.toLowerCase(), v])),
        ]),
    );
    const door = profile.file("thps4_levels").door_walls as { enabled?: boolean } | undefined;
    COMPAT.doorWalls = door?.enabled === true;
}

// This project's own changes to THPS4's levels (`port_changes` in tools/compat/thugpro/thps4_levels.json -
// PC-THPS4-n, README there, "Our own changes"): neither THPS4's data nor THUGPRO's, so applied whatever --compat says;
// --no-port-changes leaves out all but the `required` ones (what a level needs to load or make_menu_level to install
// it: the crash guards). Each entry names the converter step that applies it (`rule`) and the levels (target.level,
// or target.levels: a list or "*").
export const PORT = { changes: [] as compat.Entry[] };

export function setPortChanges(on: boolean): void {
    PORT.changes = compat.portChanges("thps4_levels").filter((e) => on || e.required === true);
}

// The port changes of a rule for a level.
export function portChanges(rule: string, base: string): compat.Entry[] {
    return PORT.changes.filter((e) => {
        if (e.rule !== rule) return false;
        const t = e.target as { level?: string; levels?: string | string[] };
        if (t.levels === "*") return true;
        const list = Array.isArray(t.levels) ? t.levels : t.level !== undefined ? [t.level] : [];
        return list.some((l) => l.toLowerCase() === base.toLowerCase());
    });
}

// Whether a rule's port change applies to a level.
export const portChange = (rule: string, base: string): boolean => portChanges(rule, base).length > 0;

// The profile's THUGPRO edits of a level (empty with --compat none).
export const editsOf = (base: string): thugproEdits.LevelEdits => thugproEdits.levelEdits(COMPAT.edits, base);

const vector = (values: readonly number[]): Vector => new Vector("vector", [...values]);

// The drop-in every added (or made multiplayer) spawn runs: THPS4's practice run started the skater at
// TRG_SS_SkaterDropIn with MakeSkaterGoto DropIn (start_practice_run_skater); THUG2's skater has DropIn.
export const dropInName = (level: string): string => `${level}_net_dropin`;

// TP-21 (tools/compat/thugpro/thps4_levels.json net_nodes): the profile's crowns and multiplayer spawns added to the
// converted nodes (in place: their order is the order online - a crown is picked by its index), THPS4's restarts it
// names made multiplayer ones. Returns the NeverScript to add (the drop-in script) or "".
export function addNetNodes(
    base: string,
    level: string,
    converted: Struct[],
    names: Map<number, string>,
    report: string[],
): string {
    const net = COMPAT.netNodes.get(base.toLowerCase());
    if (net === undefined) return "";
    const key = (text: string): Name => {
        const value = qb.checksum(text);
        if (!names.has(value)) names.set(value, text);
        return new Name(value);
    };
    const dropIn = key(dropInName(level));
    // (a legacy Position: THPS4's own nodes' form here, z negated from THUG2's Pos)
    const position = (p: readonly number[]): Vector => vector([p[0]!, p[1]!, -p[2]!]);
    for (const wanted of net.multiplayer) {
        const node = converted.find((n) => {
            const nm = n.get("Name");
            return nm instanceof Name && nm.value === qb.checksum(wanted) && nodes.classOf(n, names) === "restart";
        });
        if (node === undefined) throw new FormatError(`${base}: TP-21 names ${wanted}, not a restart of the level`);
        const types = node.get("restart_types");
        const listed = Array.isArray(types) ? [...types] : [];
        for (const t of ["Multiplayer", "Horse"]) {
            if (!listed.some((x) => x instanceof Name && x.value === qb.checksum(t))) listed.push(key(t));
        }
        node.entries = node.entries.filter(([k]) => !(k !== null && k.value === qb.checksum("restart_types")));
        node.entries.push([key("restart_types"), listed]);
        if (node.get("TriggerScript") === undefined) node.entries.push([key("TriggerScript"), dropIn]);
    }
    net.restarts.forEach((r, i) => {
        const n = String(i + 1).padStart(2, "0");
        converted.push(
            new Struct([
                [key("Position"), position(r.pos)],
                [key("Angles"), vector(r.angles)],
                [key("Name"), key(`TRG_Net_Restart${n}`)],
                [key("Class"), key("Restart")],
                [key("Type"), key("Multiplayer")],
                [null, key("CreatedAtStart")],
                [key("RestartName"), `Multi: Restart ${n}`],
                [key("restart_types"), [key("Multiplayer"), key("Horse")]],
                [key("TriggerScript"), dropIn],
            ]),
        );
    });
    net.crowns.forEach((c, i) => {
        converted.push(
            new Struct([
                [key("Position"), position(c.pos)],
                [key("Angles"), vector(c.angles)],
                [key("Name"), key(`TRG_KOTH${String(i + 1).padStart(2, "0")}`)],
                [key("Class"), key("GenericNode")],
                [key("Type"), key("Crown")],
                [null, key("CreatedAtStart")],
            ]),
        );
    });
    report.push(
        `  ${base}: TP-21 - ${net.multiplayer.length} restart(s) made multiplayer, ${net.restarts.length} spawn(s) and ` +
            `${net.crowns.length} crown(s) added`,
    );
    return `script ${dropInName(level)} {\n    MakeSkaterGoto DropIn \n}\n`;
}

setCompat("thugpro"); // (the default, also for the tools that import this one: thugpro_compare, migrate_layouts)
setPortChanges(true);

// The level's THUG2 name - its folder, files, node array: THPS4's (lower case), or thps4_<lvl> where that is THUG2's
// own or (legacy) as this converter named every level before.
export function levelName(base: string, legacy = false): string {
    const low = base.toLowerCase();
    return legacy || THUG2_NAMES.has(low) ? `thps4_${low}` : low;
}

// The name of its load script and level structure (Load_<name>, Level_<name>): the level's, or the profile's.
export function loadName(base: string, legacy = false): string {
    return (legacy ? undefined : COMPAT.loadNames.get(base.toLowerCase())) ?? levelName(base, legacy);
}

// A sky's THUG2 name: prefixed as its level's is.
export function skyName(sky: string, base: string, legacy = false): string {
    return levelName(base, legacy).startsWith("thps4_") ? `thps4_${sky.toLowerCase()}` : sky.toLowerCase();
}

// The folder's files as Python's glob(join(folder, "*")) lists them: the directory's order, no dot files.
function globAll(folder: string): string[] {
    if (!fs.existsSync(folder) || !fs.statSync(folder).isDirectory()) return [];
    return fs
        .readdirSync(folder)
        .filter((name) => !name.startsWith("."))
        .map((name) => pyJoin(folder, name));
}

// glob(join(folder, "*<suffix>")) on Windows: case-insensitive.
function globSuffix(folder: string, suffix: string): string[] {
    return globAll(folder).filter((p) => path.win32.basename(p).toLowerCase().endsWith(suffix.toLowerCase()));
}

export type LevelFiles = Partial<Record<"scn" | "col" | "tex" | "qb", string>>;

export function levelFiles(folder: string, base: string): LevelFiles {
    // <base>scn.dat / col.dat / tex.dat / .qb, any case.
    const found: LevelFiles = {};
    for (const file of globAll(folder)) {
        const name = path.win32.basename(file).toLowerCase();
        for (const [kind, suffix] of [
            ["scn", "scn.dat"],
            ["col", "col.dat"],
            ["tex", "tex.dat"],
            ["qb", ".qb"],
        ] as const) {
            if (name === (base + suffix).toLowerCase()) found[kind] = file;
        }
    }
    return found;
}

export function findFolder(levelsDir: string, name: string): string | null {
    for (const entry of fs.readdirSync(levelsDir)) {
        if (entry.toLowerCase() === name.toLowerCase()) return pyJoin(levelsDir, entry);
    }
    return null;
}

export function levelStructs(thps4Dir: string): Map<string, Struct> {
    // THPS4's Levels.qb: its level structures by their level= name.
    const q = new qb.QbFile(fs.readFileSync(path.join(thps4Dir, "Game", "data", "scripts", "Levels.qb")), "Levels.qb");
    const found = new Map<string, Struct>();
    for (const [, value] of q.globals) {
        if (value instanceof Struct) {
            const level = value.get("level");
            if (typeof level === "string") found.set(level.toLowerCase(), value);
        }
    }
    return found;
}

// Invisible walls opened (port changes, rule open_walls - PC-THPS4-1, PC-THPS4-2): target.collision, a collision object, and
// target.faces - "<axis>-min" / "<axis>-max" (the object's faces lying in its box's plane on that side are left out) or
// "invisible" (its invisible faces: THPS4's face flag 0x1000). THPS4's skate shop was a menu: InvisColl01 boxes in the
// skate park outside the shop (sides, roof, both ends) - its z-min face is the shop's side, across the door too; the
// door itself is also closed by Object10's (the shop's) two invisible faces, x 699-777 at z -1074 (the Occlusion plane
// at z -1082 has the doorway's hole there). The user, 2026-10-19: the door open; InvisColl01's z-max face (the fence's
// gap at the end of the halfpipe) kept.
export const FACE_INVISIBLE = 0x1000;
const AXES: Readonly<Record<string, number>> = { x: 0, y: 1, z: 2 };

// Nodes the playable level has at the start although THPS4's node array doesn't create them: its game scripts did
// when play started (port changes, rule created_in_play - PC-THPS4-3: target.node, set.CreatedAtStart). The skate shop: the
// menu kills SSO_Fence05 (create_ss_menu) and the practice run creates it again (start_practice_run_skater) - the
// fence piece at the end of the halfpipe (the user, 2026-10-19: a gap there).
const reEscape = (text: string): string => text.replace(/[^A-Za-z0-9_]/g, "\\$&");

export function createdInPlay(textIn: string, base: string, report: string[]): string {
    let text = textIn;
    for (const change of portChanges("created_in_play", base)) {
        const name = String((change.target as { node: string }).node);
        const node = search(new RegExp(`\\{[^{}]*\\bName=${reEscape(name)}\\b[^{}]*\\}`), text);
        // (a "set" on a node the level hasn't is an error, never an add)
        if (!node) throw new FormatError(`${base}: ${change.id} sets ${name}, which the node array hasn't`);
        if (!search(/\bCreatedAtStart\b/, node.group(0)!)) {
            const fixed = sub(
                new RegExp(`\\bName=${reEscape(name)}\\b`),
                () => `Name=${name} CreatedAtStart`,
                node.group(0)!,
                1,
            );
            text = text.slice(0, node.start()) + fixed + text.slice(node.end());
            report.push(`  ${base}: ${name} created at the start (THPS4's practice run created it)`);
        }
    }
    return text;
}

export function openWalls(col4: thps4.ColFile, base: string, report: string[]): void {
    if (COMPAT.doorWalls && base.toLowerCase() === "skateshop") {
        // (TP-29, off by default: THUGPRO keeps THPS4's invisible door walls - against the user's request above)
        report.push(`  ${base}: TP-29 - the door walls kept closed (THUGPRO's)`);
        return;
    }
    for (const change of portChanges("open_walls", base)) {
        const { collision: name, faces } = change.target as { collision: string; faces: string };
        const wanted = qb.checksum(name);
        const side = /^([xyz])-(min|max)$/.exec(faces);
        if (faces !== "invisible" && !side) throw new FormatError(`${change.id}: faces ${faces}: not handled`);
        let found = false;
        for (const o of col4.objects) {
            if (o.checksum !== wanted || !o.verts.length) continue;
            let kept: typeof o.faces;
            if (side === null) {
                kept = o.faces.filter((f) => !(f[0] & FACE_INVISIBLE));
            } else {
                const axis = AXES[side[1]!]!;
                const values = o.verts.map((v) => v[axis]!);
                let plane = values[0]!;
                for (const v of values) if (side[2] === "max" ? v > plane : v < plane) plane = v;
                kept = o.faces.filter(
                    (f) => ![f[2], f[3], f[4]].every((i) => Math.abs(o.verts[i]![axis]! - plane) < 0.01),
                );
            }
            report.push(`  ${base}: ${o.faces.length - kept.length} face(s) of ${name} left out (an opening)`);
            if (kept.length === o.faces.length) report.push(`  ${base}: ${change.id} - no face of ${name} to remove`);
            o.faces = kept;
            found = true;
        }
        if (!found) report.push(`  ${base}: ${change.id} - no collision object ${name} to remove faces of`);
    }
}

export type Geometry = [thps4.Scene, tex.TexFile, thps4.ColFile];

function requireFiles(
    files: LevelFiles,
    folder: string,
    base: string,
): Required<Pick<LevelFiles, "scn" | "col" | "tex">> {
    for (const kind of ["scn", "col", "tex"] as const) {
        if (files[kind] === undefined) throw new FormatError(`${folder}: no ${kind} file for ${base}`);
    }
    return files as Required<Pick<LevelFiles, "scn" | "col" | "tex">>;
}

export function readGeometry(folder: string, base: string): Geometry {
    // The level's THPS4 scene, its texture dictionary (THUG2's format) and its THPS4 collision.
    const files = requireFiles(levelFiles(folder, base), folder, base);
    return [
        thps4.readScene(fs.readFileSync(files.scn), files.scn),
        tex.readTex(fs.readFileSync(files.tex), files.tex),
        thps4.readCol(fs.readFileSync(files.col), files.col),
    ];
}

export function convertGeometry(
    folder: string,
    base: string,
    outDir: string,
    outName: string,
    report: string[],
    keepTriggers: (checksum: number) => boolean = () => false,
    read: Geometry | null = null,
): LevelFiles {
    // read: readGeometry's [scene, textures, collision], changed (the vehicles' models added), or null.
    const files = levelFiles(folder, base);
    requireFiles(files, folder, base);
    const warnings: string[] = [];
    const [scene4, textures, col4] = read ?? readGeometry(folder, base);
    // THPS4's vertex colours are 255 = full (rgb; alpha 128 = full), levels and skies alike: halved for THUG2.
    const scene = geometry.convertScene(scene4, warnings, scene4.materials.map(thps4.toThug2Material), true);
    const converted = tex.depalettise(textures);
    const col = geometry.convertCol(col4, keepTriggers, warnings, thps4.faceFlags);
    // (THPS4's materials have no z bias: the layers on other surfaces get one, or they shimmer - level_port/overlay_bias.ts)
    const biased = overlayBias.biasOverlays(scene);
    if (biased) report.push(`  ${biased} layer materials given a z bias over the surfaces under them`);
    common.writeLevel(outDir, outName, scene, textures, col);
    common.reportLevel(report, outName, scene, textures, col, warnings, converted);
    return files;
}

interface Args {
    command: string;
    levels: string[];
    thug2: string;
    thps4: string;
    out: string | undefined;
    ns: string;
    noScripts: boolean;
    thugpro: string; // THUGPRO's install (thugpro_new's files are read from it)
    thugproBanners: boolean;
    legacyNames: boolean;
    extra: string[];
}

const sceneSectors = (scene4: thps4.Scene): Set<number> =>
    new Set(scene4.sectors.filter((s) => s.bone === -1 && !(s.flags & thps4.SEC_WEIGHTS)).map((s) => s.checksum));

// THUGPRO's files read this run (thugpro_new.loadFiles' cache).
const NEW_FILES = new Map<string, Buffer | string>();

export function cmdConvert(args: Args): void {
    const levelsDir = pyJoin(args.thps4, "Game", "data", "levels");
    const out = args.out || pyJoin(args.thug2, STAGING);
    if (!fs.existsSync(args.ns)) pyExit(`${args.ns} not found: build it with tools\\build_neverscript.ps1`);
    const structs = levelStructs(args.thps4);
    const thug2 = new pedsCars.Thug2Assets(args.thug2);
    const port = args.noScripts ? null : new thps4Scripts.Port(args.thps4, args.thug2);
    const wanted = args.levels.length ? args.levels : LEVELS;
    if (fs.existsSync(out) && fs.statSync(out).isDirectory()) fs.rmSync(out, { recursive: true });
    fs.mkdirSync(out, { recursive: true });
    const manifest: { levels: Record<string, unknown>[] } = { levels: [] };
    let report: string[] = [];
    const skiesDone = new Set<string>();
    wanted.forEach((folderName, index) => {
        const folder = findFolder(levelsDir, folderName);
        if (!folder) {
            report.push(`${folderName}: not found in ${levelsDir}`);
            return;
        }
        const base = folderName;
        const struct = structs.get(base.toLowerCase());
        const name = levelName(base, args.legacyNames);
        const load = loadName(base, args.legacyNames);

        // The node array (first: the collision keeps the trigger faces of the kill zones it keeps).
        const files = levelFiles(folder, base);
        if (files.qb === undefined) throw new TypeError(`${folder}: no ${base}.qb`);
        const q = new qb.QbFile(fs.readFileSync(files.qb), files.qb);
        const array = q.get("NodeArray");
        if (!Array.isArray(array)) throw new FormatError(`${files.qb}: no NodeArray`);
        let levelScripts: qb.QbFile | null = null;
        for (const p of globSuffix(folder, "_scripts.qb")) levelScripts = new qb.QbFile(fs.readFileSync(p), p);
        let kills = nodes.killScripts(q, levelScripts);
        const names = new Map(q.names);
        if (levelScripts) for (const [k, v] of levelScripts.names) if (!names.has(k)) names.set(k, v);
        // The peds and cars (THUG2's own ped AI and cars, tools/level_port/peds_cars.ts), after the converted nodes.
        let moving: Struct[] = [];
        let movingScripts = new Map<string, string>();
        if (pedsCarsWanted(base)) {
            // (THPS4's animals - Zoo's penguins, Carnival's pigs... - walk their paths too: not as people)
            const people = array.map((n) =>
                n instanceof Struct && nodes.typeOf(n, names).startsWith("anl_") ? new Struct([]) : n,
            );
            let movingStats: pedsCars.PedsCarsStats;
            [moving, movingScripts, movingStats] = pedsCars.convert(
                people,
                names,
                [q, ...(levelScripts ? [levelScripts] : [])],
                (n) => thug2.profileExists(n),
                (m) => thug2.modelExists(m),
            );
            report.push(`  peds and cars: ${moving.length} nodes, ${pyRepr(movingStats)}`);
        }
        const geometryRead = readGeometry(folder, base);
        const [scene4, textures4, col4] = geometryRead;
        const edits = editsOf(base);
        openWalls(col4, base, report);
        thugproEdits.removeDuplicateFaces(col4, edits, base, report); // (TP-28)
        let brought = new Map<number, Buffer>();
        let extraText = "";
        let hasSetup = false;
        let stats: Map<string, number> | null = null;
        let converted: Struct[];
        let levelSources: scriptPort.Source[] = [];
        if (port !== null) {
            // The level's scripts, game objects, waypoints, peds and vehicles (thps4_scripts.ts); the vehicles moved
            // by its scripts get their models (sectors, textures, collision added to the level's).
            const plan = port.vehicles.plan(array, names, thps4Scripts.movingNames(moving));
            const vehicleCollides = port.vehicles.addTo(plan, scene4, textures4, col4);
            levelSources = [files.qb, ...globSuffix(folder, "_scripts.qb")].map(
                (p) => new scriptPort.Source(fs.readFileSync(p), "level"),
            );
            const collides = new Set(col4.objects.filter((o) => o.faces.length).map((o) => o.checksum));
            for (const c of vehicleCollides) collides.add(c);
            const leftOut = portChanges("left_out_commands", base).flatMap(
                (c) => (c.target as { script_commands: string[] }).script_commands,
            );
            const result = thps4Scripts.convert(
                port,
                array,
                names,
                levelSources,
                kills,
                sceneSectors(scene4),
                moving,
                struct,
                name,
                new Set(plan.keys()),
                collides,
                {
                    leftOut, // (PC-THPS4-8)
                    neverCreated: portChange("never_created", base), // (PC-THPS4-6)
                    emptyRestartScripts: portChange("empty_restart_scripts", base), // (PC-THPS4-9)
                },
            );
            ({ converted, kills, scripts: brought, text: extraText, hasSetup } = result);
            report.push(`  ${result.report}`);
        } else {
            [converted, stats] = nodes.convertNodes(array, names, kills);
            kills = nodes.addKillRestarts(converted, kills, names);
        }
        const triggers = nodes.killTriggerNames(converted, kills);
        if (port !== null) {
            // (THUG2's script pools are fixed - 9500 vectors for every script's: College's node array with the
            // objects, waypoints and peds didn't fit and came out null, a crash loading. The rails' and sectors'
            // zero Angles - most of them - aren't needed.)
            if (portChange("zero_angles", base)) thps4Scripts.dropZeroAngles(converted, names); // (PC-THPS4-5)
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
        }
        // (TP-22 ... TP-27: THUGPRO's hand edits - thugpro_edits.ts; added nodes before the peds and cars too)
        thugproEdits.applyEdits(
            base,
            edits,
            converted,
            names,
            {
                text: [extraText, pedsCars.scriptsNs(movingScripts), ...kills.values()].join("\n"),
                scripts: brought,
            },
            report,
        );
        // (TP-21: the profile's network-game nodes - before the peds and cars, which stay the array's last nodes)
        const netScripts = addNetNodes(base, name, converted, names, report);
        // (THUGPRO's own content - thugpro_new.ts: its scripts, nodes and the start script making its teleports, level
        // objects, radius triggers and its host options' default state; after the network nodes, before the peds and
        // cars. Needs the scripts: --no-scripts leaves it out)
        if (port !== null) {
            const made = thugproNew.applyNew(
                COMPAT.thugproNew,
                COMPAT.newFiles,
                {
                    base,
                    level: name,
                    converted,
                    names,
                    brought,
                    text: extraText,
                    report,
                    thugpro: args.thugpro,
                    banners: args.thugproBanners,
                    out,
                },
                NEW_FILES,
            );
            if (made.start !== null) {
                [extraText, hasSetup] = thugproNew.hookStart(
                    extraText,
                    thps4Scripts.setupName(name),
                    made.start,
                    hasSetup,
                );
            }
            extraText += made.text;
        }
        // Every human ped on the skeleton its THUG2 profile's model is rigged to (a woman's on THPS6_human is twisted),
        // and the level's load_level_anims for the Ped_Female animations (tools/level_port/ped_rigs.ts).
        const rigs = pedRigs.fixPedRigs([...converted, ...moving], names, (v) => thug2.femaleRigs.has(v));
        if (rigs.changed.length) report.push(`  peds given their profile's skeleton: ${rigs.changed.join(", ")}`);
        if (/^script load_level_anims\b/im.test(extraText)) throw new FormatError(`${name}: load_level_anims defined`);
        const animsText = rigs.human ? pedRigs.loadLevelAnimsNs(rigs.female > 0) : "";
        pedsCars.offsetLinks(moving, converted.length);
        let text = nodes.nodeArrayNs(`${name}_NodeArray`, [...converted, ...moving], names, kills);
        text += pedsCars.scriptsNs(movingScripts) + extraText + netScripts + animsText;
        // (a TrickObject without its sector crashes the loader: convertScene leaves skinned sectors out)
        text = createdInPlay(text, base, report);
        // (PC-THPS4-7)
        const [stripped, cleared] = portChange("trick_objects_without_sector", base)
            ? nodes.stripMissingTrickObjects(text, sceneSectors(scene4))
            : [text, 0];
        text = stripped;
        if (cleared) report.push(`  ${cleared} geometry nodes without a scene sector: TrickObject / Cluster dropped`);
        const levelDir = pyJoin(out, "levels", name);
        fs.mkdirSync(levelDir, { recursive: true });
        const qbPath = pyJoin(levelDir, `${name}.qb`);
        // (network games: path followers, peds and cars kept out - no path manager or ped animations there -, restarts for the players)
        // (PC-THPS4-11)
        const [netText, marked] = portChange("net_game_nodes", base) ? nodes.netGameNodes(text, brought) : [text, 0];
        text = netText;
        if (marked) report.push(`  ${marked} node(s) changed for network games`);

        const writeQb = (source: string): void => {
            common.nsCompile(args.ns, source, qbPath);
            if (brought.size) {
                const allNames = new Map<number, string>();
                for (const s of [...port!.sources, ...levelSources]) for (const [k, v] of s.names) allNames.set(k, v);
                for (const [k, v] of names) allNames.set(k, v);
                fs.writeFileSync(qbPath, scriptPort.appendScripts(fs.readFileSync(qbPath), brought, allNames));
            }
        };

        writeQb(text);
        convertGeometry(folder, base, levelDir, name, report, (checksum) => triggers.has(checksum), geometryRead);
        // (restarts over nothing or over a kill zone: no network spawns, no kill targets - nodes.groundRestarts)
        let groundReport: string[];
        // (PC-THPS4-10)
        [text, groundReport] = portChange("ground_restarts", base)
            ? nodes.groundRestartsAt(text, levelDir, name)
            : [text, []];
        if (groundReport.length) {
            writeQb(text);
            report = report.concat(groundReport);
        }
        if (stats !== null) report.push(`  nodes: ${converted.length} of ${array.length} kept ${pyRepr(stats)}`);
        report.push(`  ${triggers.size} kill zones`);

        // The sky.
        let sky: string | null = null;
        const skyValue = struct?.get("sky");
        if (typeof skyValue === "string") {
            const skyFolder = findFolder(levelsDir, skyValue);
            if (skyFolder) {
                sky = skyName(skyValue, base, args.legacyNames);
                if (!skiesDone.has(sky)) {
                    convertGeometry(skyFolder, skyValue, pyJoin(out, "levels", sky), sky, report);
                    skiesDone.add(sky);
                }
            }
        }
        const structName = struct?.get("name");
        const display = typeof structName === "string" ? structName : base;
        const [location, icon] = INFO[base.toLowerCase()] ?? ["", "sign_training"];
        const defaultStats = struct?.get("default_stats");
        const entry: Record<string, unknown> = {
            folder: base,
            level: name,
            sky,
            display: `THPS4 ${display.toUpperCase()}`,
            name: display,
            game: "THPS4",
            location,
            icon,
            level_num: SIGN_NUMBERS[index % SIGN_NUMBERS.length],
            default_stats: typeof defaultStats === "number" ? defaultStats : 6,
            lighting: struct ? common.lighting(struct) : {},
        };
        if (load !== name) entry.load = load; // (TP-20: tools/compat/thugpro/thps4_levels.json)
        if (hasSetup) entry.setup_script = thps4Scripts.setupName(name);
        manifest.levels.push(entry);
    });
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
    const levelsDir = pyJoin(args.thps4, "Game", "data", "levels");
    for (const folder of fs.readdirSync(levelsDir).sort()) {
        for (const file of globAll(pyJoin(levelsDir, folder)).sort()) {
            const low = file.toLowerCase();
            let what: string;
            try {
                const data = fs.readFileSync(file);
                if (low.endsWith("scn.dat")) {
                    const s = thps4.readScene(data, file);
                    if (!s.tail.equals(Buffer.alloc(4)))
                        throw new FormatError(`${s.tail.length} bytes after the sectors`);
                    what = `${s.materials.length} materials, ${s.sectors.length} sectors`;
                } else if (low.endsWith("col.dat")) {
                    const c = thps4.readCol(data, file);
                    what = `${c.objects.length} objects`;
                } else if (low.endsWith("tex.dat")) {
                    const t = tex.readTex(data, file);
                    if (!tex.writeTex(t).equals(data)) throw new FormatError("not the same written again");
                    what = `${t.textures.length} textures`;
                } else if (low.endsWith(".qb")) {
                    const q = new qb.QbFile(data, file);
                    what = `${q.globals.length} globals`;
                } else {
                    continue;
                }
            } catch (e) {
                // (Python's FormatError, ValueError, IndexError)
                if (!(e instanceof FormatError || e instanceof RangeError)) throw e;
                print(`FAIL ${file}: ${e.message}`);
                failures += 1;
                continue;
            }
            checked += 1;
            print(`ok   ${path.win32.relative(levelsDir, file)}: ${what}`);
        }
    }
    print(`${checked} files checked, ${failures} failures`);
    return failures ? 1 : 0;
}

export function main(argv: readonly string[] = process.argv.slice(2)): number {
    const parsed = parseConverterArgs(
        "thps4_levels.ts",
        ["convert", "selfcheck"],
        {
            thug2: { kind: "string", default: "C:\\Games\\THUG2\\Game" },
            thps4: { kind: "string", default: "C:\\Games\\THPS4" },
            out: { kind: "string" },
            ns: { kind: "string", default: common.NS },
            "no-scripts": { kind: "flag" },
            // compatibility profile (tools/compat): thugpro - the skate shop's load script as THUGPRO's (default),
            // or none
            compat: { kind: "string", default: "thugpro" },
            // the names this converter used before: thps4_<lvl> for every level
            "legacy-names": { kind: "flag" },
            // THPS4's data as it is: none of this project's own changes (port_changes, PC-THPS4-n: the skate shop's
            // open door...) but the required ones (the crash guards)
            "no-port-changes": { kind: "flag" },
            // the THUGPRO profile without THUGPRO's own content (thugpro_new: its teleports, level options...)
            "no-thugpro-new": { kind: "flag" },
            // THUGPRO's install, which thugpro_new's files are read from (missing / different: left out, a warning)
            thugpro: { kind: "string", default: thugproNew.DEFAULT_THUGPRO },
            // also place THUGPRO's banners / posters (thugpro_new models) - THUGPRO itself doesn't show them
            "thugpro-banners": { kind: "flag" },
            extra: { kind: "list" },
        },
        argv,
    );
    const compatName = parsed.strings.get("compat")!;
    if (!(compat.PROFILES as readonly string[]).includes(compatName)) {
        process.stderr.write(
            `thps4_levels.ts: error: argument --compat: invalid choice: '${compatName}' (choose from ${compat.PROFILES.join(", ")})\n`,
        );
        pyExit(2);
    }
    setCompat(compatName, !parsed.flags.get("no-thugpro-new"));
    setPortChanges(!parsed.flags.get("no-port-changes"));
    const args: Args = {
        command: parsed.command,
        levels: parsed.positionals,
        thug2: parsed.strings.get("thug2")!,
        thps4: parsed.strings.get("thps4")!,
        out: parsed.strings.get("out"),
        ns: parsed.strings.get("ns")!,
        noScripts: parsed.flags.get("no-scripts")!,
        legacyNames: parsed.flags.get("legacy-names")!,
        thugpro: parsed.strings.get("thugpro")!,
        thugproBanners: parsed.flags.get("thugpro-banners")!,
        extra: parsed.lists.get("extra")!,
    };
    // (a stock install keeps the game in a Game subfolder: C:\Games\THUG2 -> C:\Games\THUG2\Game)
    const isDir = (p: string): boolean => fs.existsSync(p) && fs.statSync(p).isDirectory();
    if (!isDir(path.join(args.thug2, "Data")) && isDir(path.join(args.thug2, "Game", "Data"))) {
        args.thug2 = pyJoin(args.thug2, "Game");
    }
    if (args.command === "selfcheck") return cmdSelfcheck(args);
    cmdConvert(args);
    return 0;
}

if (isMain(import.meta.url)) await runMain(() => main());
