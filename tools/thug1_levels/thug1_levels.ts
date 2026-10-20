// NOT ORIGINAL - converts Tony Hawk's Underground (PC) levels to THUG2's formats so that THUG2 can load and play them
// (see README.md). Nothing of either game is stored in the repo: the files are made from the user's own copies.
//
//   node tools/thug1_levels/thug1_levels.ts selfcheck [--thug2 C:\Games\THUG2] [--thug1 C:\Games\THUG1]
//   node tools/thug1_levels/thug1_levels.ts convert [levels...] [--thug2 ...] [--thug1 ...] [--out <folder>]
//   then: python -I tools/menu_level/make_menu_level.py   (installs them into the data overlay, with level select signs)
//
// convert reads <thug1>\Game\Data\pre (the level archives <LV>scn.pre, <LV>col.pre, <LV>.pre and qb.pre's Levels.qb)
// and writes <thug2>\thug2-decomp-thug1\ (not read by the game): levels\thug1_<lv>\thug1_<lv>.scn.xbx / .tex.xbx /
// .col.xbx / .qb (the node array), the skies (scene and textures) the same way, and levels.json (what
// make_menu_level.py adds to Levels.qb and the level select: tools/level_port/ported_levels.ts).
//
// The THUG2 formats, the geometry conversion, the node arrays and the installing are shared with tools/thps4_levels
// (tools/level_port); this folder has THUG1's own formats (thug1.ts) and its levels.

import * as fs from "node:fs";
import * as path from "node:path";
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
import { isMain, print, pyExit, pyJoin, pyRepr, runMain } from "../lib/py.ts";
import * as thug1 from "./thug1.ts";
import * as thug1Port from "./thug1_port.ts";

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

// THUG1's collision face flags in THUG2: the same meanings (0x80: the camera collides, as THUG2's - not THPS4's
// reverse); 0x2000 (on every face), 0x4000 and 0x8000 aren't in THUG2's files - dropped.
export const FACE_KEEP = 0x1fff;

export function faceFlags(flags: number): number {
    return flags & FACE_KEEP;
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
}

export function prePath(args: { thug1: string }, name: string): string | null {
    const file = pyJoin(args.thug1, "Game", "Data", "pre", name);
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
    if (keepTriggers !== null) {
        if (!files.has(`${base}.col.xbx`)) throw new FormatError(`${code}: no collision in the archives`);
        const col1 = thug1.readCol(need(files, `${base}.col.xbx`), `${base}.col.xbx`);
        col = thug1.toThug2Col(col1, keepTriggers, faceFlags, base);
    }
    common.writeLevel(outDir, outName, scene, textures, col);
    common.reportLevel(report, outName, scene, textures, col, warnings, converted);
}

export interface LevelNodes {
    triggers: Set<number>; // the checksums of the nodes whose collision keeps its trigger faces
    starts: Map<string, string>; // the level structure's startup_script / setup_script kept
    recheck: (levelDir: string) => string[];
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
): LevelNodes {
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
        const duplicates = thug1Port.resolveDuplicateTriggers(array, scriptQbs, names);
        made = new thug1Port.NodePass(port, array, names, sectors);
        if (duplicates) made.counts.set("DuplicateTrigger nodes given their link's script", duplicates);
        kept = made.kept;
        for (const n of kept) {
            const script = n instanceof Struct ? n.get("TriggerScript") : undefined;
            if (script instanceof Name && !kills.has(script.value)) wanted.add(script.value);
        }
    }
    const convertedStats = nodes.convertNodes(kept, names, kills, ["pos"], made ? made.asIs : [], wanted);
    let converted = convertedStats[0];
    const stats = convertedStats[1];
    kills = nodes.addKillRestarts(converted, kills, names);
    const triggers = nodes.killTriggerNames(converted, kills);
    let scriptReport = "";
    if (port !== null && made !== null) {
        const ported = thug1Port.portScripts(port, levelSources, converted, array, kills, names, struct, sectors);
        scripts = ported.scripts;
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
    const [netText, marked] = nodes.netGameNodes(text, scripts);
    text = netText;
    if (marked) report.push(`  ${marked} node(s) changed for network games`);

    const writeQb = (source: string): void => {
        common.nsCompile(ns, source, qbPath);
        if (scripts.size) fs.writeFileSync(qbPath, scriptPort.appendScripts(fs.readFileSync(qbPath), scripts, names));
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
    // (--static: only the geometry's nodes, restarts, rails and kill zones, as before the scripts were brought)
    const port = args.static ? null : new thug1Port.Thug1Port(args.thug2, prePath(args, "qb.pre")!);
    const wanted = args.levels.length ? args.levels : LEVELS.filter((info) => info[5]).map((info) => info[0]);
    if (fs.existsSync(out) && fs.statSync(out).isDirectory()) fs.rmSync(out, { recursive: true });
    fs.mkdirSync(out, { recursive: true });
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
        const name = `thug1_${code.toLowerCase()}`;
        const levelDir = pyJoin(out, "levels", name);
        let nodeReport: string[] = [];
        const { triggers, starts, recheck } = convertLevelNodes(
            files,
            code,
            name,
            args.ns,
            levelDir,
            nodeReport,
            port,
            struct,
        );
        convertGeometry(files, code, levelDir, name, report, (checksum) => triggers.has(checksum));
        nodeReport = nodeReport.concat(recheck(levelDir));
        report = report.concat(nodeReport);

        // The sky: Levels.qb's sky=, else <code>_sky when the scene archive has one.
        let sky: string | null = null;
        const skyValue = struct?.get("sky");
        const skyCode = typeof skyValue === "string" ? skyValue : `${code}_sky`;
        if (files.has(`${baseOf(skyCode)}.scn.xbx`)) {
            sky = `thug1_${skyCode.toLowerCase()}`;
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
    print(`wrote ${out} (${manifest.levels.length} levels); install with tools\\menu_level\\make_menu_level.py`);
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
    const thug1Pre = pyJoin(args.thug1, "Game", "Data", "pre");
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
