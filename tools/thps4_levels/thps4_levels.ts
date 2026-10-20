// NOT ORIGINAL - converts Tony Hawk's Pro Skater 4 (PC) levels to THUG2's formats so that THUG2 can load and play
// them (see README.md). Nothing of either game is stored in the repo: the files are made from the user's own copies.
//
//   node tools/thps4_levels/thps4_levels.ts selfcheck [--thug2 C:\Games\THUG2] [--thps4 C:\Games\THPS4]
//   node tools/thps4_levels/thps4_levels.ts convert [levels...] [--thug2 ...] [--thps4 ...] [--out <folder>]
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
import { FormatError } from "../level_port/binio.ts";
import * as common from "../level_port/common.ts";
import * as geometry from "../level_port/geometry.ts";
import * as nodes from "../level_port/nodes.ts";
import * as pedsCars from "../level_port/peds_cars.ts";
import * as portedLevels from "../level_port/ported_levels.ts";
import * as qb from "../level_port/qb.ts";
import { Name, Struct } from "../level_port/qb.ts";
import * as scriptPort from "../level_port/script_port.ts";
import * as tex from "../level_port/tex.ts";
import { parseConverterArgs } from "../lib/converter_args.ts";
import { isMain, print, pyExit, pyJoin, pyRepr, runMain } from "../lib/py.ts";
import { search, sub } from "../lib/re.ts";
import * as thps4 from "./thps4.ts";
import * as thps4Scripts from "./thps4_scripts.ts";

export const STAGING = "thug2-decomp-thps4";

// The THPS4 levels converted by default (folder under Game\data\levels): everything with a scene except the park
// editor's sets. Foo has only a node array (no scene).
export const LEVELS = ["Alc", "Cnv", "Hof", "Jnk", "Kon", "Lon", "SF2", "Sch", "Zoo", "SkateShop", "Motox"];
// The level select's numbers for the signs (each sign needs its own; none of them has career records - the info
// panel skips them, as make_menu_level.ts's SKATE SHOP / TEST LEVEL at 18 / 21).
export const SIGN_NUMBERS = [19, 20, 22, 23, 24, 25, 26, 27, 28, 29, 31];
// The levels whose peds and cars are brought over (tools/level_port/peds_cars.ts): the skate shop's pros on the
// halfpipe, the people walking by and the cars on the road (also the front end's: make_menu_level.ts copies them);
// every level's walkers and Car_Generic01 cars.
// College left out: with its walker and car its node array came out null in the game (a crash loading; cause not
// found - its other peds are THUG2 standing peds, its car a LevelObject of its model).
export const PEDS_CARS: ReadonlySet<string> = new Set(
    LEVELS.map((name) => name.toLowerCase()).filter((n) => n !== "sch"),
);
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

// Invisible walls opened: [collision object, axis, "min" / "max"] - the object's faces lying in its box's plane on that
// side are left out -, or [collision object, "invisible"] - its invisible faces (THPS4's face flag 0x1000) are.
// THPS4's skate shop was a menu: InvisColl01 boxes in the skate park outside the shop (sides, roof, both ends) - its
// z-min face is the shop's side, across the door too; the door itself is also closed by Object10's (the shop's) two
// invisible faces, x 699-777 at z -1074 (the Occlusion plane at z -1082 has the doorway's hole there). The user,
// 2026-10-19: the door open; InvisColl01's z-max face (the fence's gap at the end of the halfpipe) kept.
type Opening = readonly [string, number, "min" | "max"] | readonly [string, "invisible"];
export const OPENINGS: Readonly<Record<string, readonly Opening[]>> = {
    skateshop: [
        ["InvisColl01", 2, "min"],
        ["Object10", "invisible"],
    ],
};
export const FACE_INVISIBLE = 0x1000;

// Nodes the playable level has at the start although THPS4's node array doesn't create them: its game scripts did
// when play started. The skate shop: the menu kills SSO_Fence05 (create_ss_menu) and the practice run creates it
// again (start_practice_run_skater) - the fence piece at the end of the halfpipe (the user, 2026-10-19: a gap there).
export const CREATED_IN_PLAY: Readonly<Record<string, readonly string[]>> = { skateshop: ["SSO_Fence05"] };

const reEscape = (text: string): string => text.replace(/[^A-Za-z0-9_]/g, "\\$&");

export function createdInPlay(textIn: string, base: string, report: string[]): string {
    let text = textIn;
    for (const name of CREATED_IN_PLAY[base.toLowerCase()] ?? []) {
        const node = search(new RegExp(`\\{[^{}]*\\bName=${reEscape(name)}\\b[^{}]*\\}`), text);
        if (node && !search(/\bCreatedAtStart\b/, node.group(0)!)) {
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
    for (const opening of OPENINGS[base.toLowerCase()] ?? []) {
        const name = opening[0];
        const wanted = qb.checksum(name);
        for (const o of col4.objects) {
            if (o.checksum !== wanted || !o.verts.length) continue;
            let kept: typeof o.faces;
            if (opening[1] === "invisible") {
                kept = o.faces.filter((f) => !(f[0] & FACE_INVISIBLE));
            } else {
                const [, axis, side] = opening as readonly [string, number, "min" | "max"];
                const values = o.verts.map((v) => v[axis]!);
                let plane = values[0]!;
                for (const v of values) if (side === "max" ? v > plane : v < plane) plane = v;
                kept = o.faces.filter(
                    (f) => ![f[2], f[3], f[4]].every((i) => Math.abs(o.verts[i]![axis]! - plane) < 0.01),
                );
            }
            report.push(`  ${base}: ${o.faces.length - kept.length} face(s) of ${name} left out (an opening)`);
            o.faces = kept;
        }
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
    extra: string[];
}

const sceneSectors = (scene4: thps4.Scene): Set<number> =>
    new Set(scene4.sectors.filter((s) => s.bone === -1 && !(s.flags & thps4.SEC_WEIGHTS)).map((s) => s.checksum));

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
        const name = `thps4_${base.toLowerCase()}`;

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
        if (PEDS_CARS.has(base.toLowerCase())) {
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
        openWalls(col4, base, report);
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
            thps4Scripts.dropZeroAngles(converted, names);
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
        pedsCars.offsetLinks(moving, converted.length);
        let text = nodes.nodeArrayNs(`${name}_NodeArray`, [...converted, ...moving], names, kills);
        text += pedsCars.scriptsNs(movingScripts) + extraText;
        // (a TrickObject without its sector crashes the loader: convertScene leaves skinned sectors out)
        text = createdInPlay(text, base, report);
        const [stripped, cleared] = nodes.stripMissingTrickObjects(text, sceneSectors(scene4));
        text = stripped;
        if (cleared) report.push(`  ${cleared} geometry nodes without a scene sector: TrickObject / Cluster dropped`);
        const levelDir = pyJoin(out, "levels", name);
        fs.mkdirSync(levelDir, { recursive: true });
        const qbPath = pyJoin(levelDir, `${name}.qb`);
        // (network games: path followers, peds and cars kept out - no path manager or ped animations there -, restarts for the players)
        const [netText, marked] = nodes.netGameNodes(text, brought);
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
        [text, groundReport] = nodes.groundRestartsAt(text, levelDir, name);
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
                sky = `thps4_${skyValue.toLowerCase()}`;
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
        if (hasSetup) entry.setup_script = `${name}_setup`;
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
            extra: { kind: "list" },
        },
        argv,
    );
    const args: Args = {
        command: parsed.command,
        levels: parsed.positionals,
        thug2: parsed.strings.get("thug2")!,
        thps4: parsed.strings.get("thps4")!,
        out: parsed.strings.get("out"),
        ns: parsed.strings.get("ns")!,
        noScripts: parsed.flags.get("no-scripts")!,
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
