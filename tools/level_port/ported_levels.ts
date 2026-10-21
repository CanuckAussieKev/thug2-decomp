// NOT ORIGINAL - what tools/menu_level/make_menu_level.ts adds to the data overlay for the levels converted from other
// games (tools/thps4_levels, tools/thug1_levels, tools/thps3_levels): their files, their level structures in Levels.qb
// and their signs in the level select.
//
// Each converter writes a staging folder in the THUG2 folder (not read by the game): <thug2>\<staging>\levels\<level>\
// (the converted files) and <thug2>\<staging>\levels.json, a list of levels:
//   folder        the level's name in its own game (THPS4's folder, THUG1's level code)
//   level         the level's name in THUG2 (its folder under levels\, its files, Load_<level>, Level_<level>)
//   load          (optional) the name of its load script and level structure (Load_<load>, Level_<load>) when it
//                 isn't `level` (THPS4's skate shop: Load_practice, a THUGPRO compatibility name - TP-20)
//   sky           the sky level's name, or null
//   display       the level select's sign text ("THUG1 NEW JERSEY")
//   name          the level's own name (the level structure's name=)
//   game          the game it comes from ("THPS4", "THUG1", "THPS3")
//   location      where it is in the real world ("New Jersey, USA") - for the level select
//   icon          a level select sign texture that suits it (THUG2's own, e.g. "sign_philly")
//   level_num     the level select's number for its sign (unique; no career records - the info panel skips them)
//   default_stats the skater's stats there
//   lighting      THUG2 level structure keys (ambient / directional lights)
//   setup_script  (optional) a script of the level's .qb the level structure names as its setup_script (THPS3's
//                 start scripts)
//   startup_script (optional) the same for the level structure's startup_script (THUG1's: loads its sounds)
//
// The manifest is read with pyJsonLoads and written with pyJsonDumps (tools/lib/py.ts): a float stays a float (the
// lighting's mod factors, "1.0"), and the file's bytes are Python's (indent 1, ASCII, CRLF line ends on Windows).

import * as fs from "node:fs";
import * as path from "node:path";
import {
    Float,
    pyExit,
    pyFloatRepr,
    pyJoin,
    pyJsonDumps,
    pyJsonLoads,
    pyRepr,
    readTextPy,
    writeTextPy,
} from "../lib/py.ts";

export const MANIFEST = "levels.json";
// The staging folders, in the order their levels are listed.
export const STAGINGS = [
    "thug2-decomp-thps4",
    "thug2-decomp-thug1",
    "thug2-decomp-thps3",
    "thug2-decomp-thaw",
    "thug2-decomp-thps2",
    "thug2-decomp-thp8",
] as const;

export interface LevelEntry {
    folder?: string;
    level: string;
    load?: string;
    sky?: string | null;
    display: string;
    name: string;
    game?: string;
    location?: string;
    icon?: string;
    level_num: number;
    default_stats: number | Float;
    lighting: Record<string, number | Float>;
    setup_script?: string;
    startup_script?: string;
    staging?: string;
    [key: string]: unknown;
}

export interface Manifest {
    levels: LevelEntry[];
    [key: string]: unknown;
}

export function readManifest(file: string): Manifest | null {
    if (!fs.existsSync(file)) return null;
    return pyJsonLoads(readTextPy(file)) as Manifest | null;
}

export function writeManifest(file: string, manifest: unknown): void {
    writeTextPy(file, pyJsonDumps(manifest, 1));
}

function truthy(value: unknown): boolean {
    // (Python's truth of a json value: an empty dict / list / string, 0, None are false)
    if (value === null || value === undefined || value === false || value === 0 || value === "") return false;
    if (Array.isArray(value)) return value.length > 0;
    if (value instanceof Float) return value.value !== 0;
    if (typeof value === "object") return Object.keys(value).length > 0;
    return true;
}

export function loadManifest(game: string): Manifest | null {
    // Every converted level of every staging folder, or null when there are none.
    const levels: LevelEntry[] = [];
    for (const staging of STAGINGS) {
        const manifest = readManifest(pyJoin(game, staging, MANIFEST));
        if (!truthy(manifest)) continue;
        for (const entry of manifest!.levels) levels.push({ ...entry, staging });
    }
    const numbers = levels.map((e) => e.level_num);
    if (new Set(numbers).size !== numbers.length) {
        pyExit(`converted levels: a level select number is used twice (${pyRepr(numbers)})`);
    }
    // (the level names are the load scripts' and folders' names: two staging folders naming a level alike - e.g. two
    // converters using a game's bare level name - would replace one with the other in Levels.qb)
    // (a sky may serve several levels of one staging folder - THPS4's)
    const seen = new Map<string, string>();
    for (const e of levels) {
        const staging = e.staging ?? "";
        for (const name of new Set([e.level, loadOf(e)].map((n) => n.toLowerCase()))) {
            const level = seen.get(name);
            if (level !== undefined) pyExit(`converted levels: ${name} is named twice (${level} and ${staging})`);
            seen.set(name, staging);
        }
    }
    for (const e of levels) {
        if (typeof e.sky !== "string") continue;
        const mine = `sky:${e.staging ?? ""}`;
        const other = seen.get(e.sky.toLowerCase());
        if (other !== undefined && other !== mine) {
            pyExit(`converted levels: ${e.sky} is named twice (${other.replace(/^sky:/, "")} and ${e.staging})`);
        }
        seen.set(e.sky.toLowerCase(), mine);
    }
    return levels.length ? { levels } : null;
}

export function structName(entry: LevelEntry): string {
    return `Level_${loadOf(entry)}`;
}

// The name after Load_ / Level_: the entry's `load`, or its level's.
export function loadOf(entry: LevelEntry): string {
    return entry.load ?? entry.level;
}

// Python's "%d" % value: an int, or a float truncated.
function percentD(value: number | Float): string {
    const n = value instanceof Float ? value.value : value;
    return String(Math.trunc(n));
}

// Python's "%s" % value of a lighting value: repr(float(value)) for a float, the int as it is.
function lightingText(value: unknown): string {
    if (value instanceof Float) return pyFloatRepr(value.value);
    if (typeof value === "number") return Number.isInteger(value) ? String(value) : pyFloatRepr(value);
    return String(value);
}

export function levelsQbText(manifest: Manifest): string {
    // The level structures and load scripts, as NeverScript text for Levels.qb (LevelNum_Default as the shop's).
    const parts: string[] = [];
    for (const entry of manifest.levels) {
        const s = structName(entry);
        const load = `Load_${loadOf(entry)}`;
        const lines = [
            `script ${load} {`,
            `    load_level ${s} `,
            "} ",
            `${s} = {`,
            `    structure_name=${s} `,
            `    load_script=${load} `,
            `    name="${entry.name.replaceAll('"', "")}" `,
            '    loading_screen="loadscrn_generic" ',
            "    loading_time=8.0 ",
            "    COIM_size=1331200 ",
            `    level_name="${entry.level}" `,
            `    level="${entry.level}" `,
        ];
        if (truthy(entry.sky)) lines.push(`    sky="${entry.sky}" `);
        if (truthy(entry.startup_script)) {
            // (load_level runs it before the node array is parsed: THUG1's load its sounds there)
            lines.push(`    startup_script=${entry.startup_script} `);
        }
        if (truthy(entry.setup_script)) {
            // (load_level runs it once the node array is up, and reset_level again)
            lines.push(`    setup_script=${entry.setup_script} `);
        }
        lines.push(
            `    qb="levels\\\\${entry.level}\\\\${entry.level}.qb" `,
            `    default_stats=${percentD(entry.default_stats)} `,
            `    default_stats_classic=${percentD(entry.default_stats)} `,
            "    level_number=LevelNum_Default ",
            "    skip_teammate_pro ",
        );
        for (const [key, value] of Object.entries(entry.lighting)) lines.push(`    ${key}=${lightingText(value)} `);
        lines.push("} ");
        parts.push(`${lines.join("\n")}\n`);
    }
    return parts.join("");
}

export function addToLevels(text: string, manifest: Manifest): string {
    const anchor = "\nscript Load_TestLevel {";
    if (text.split(anchor).length - 1 !== 1) pyExit("Levels.qb: Load_TestLevel not found once");
    // (a function as the replacement: a string one would read "$&" / "$1")
    return text.replace(anchor, () => `\n${levelsQbText(manifest)}${anchor.slice(1)}`);
}

export function signsText(manifest: Manifest, indent: string): string {
    return manifest.levels
        .map(
            (e) =>
                `${indent}{text="${e.display.replaceAll('"', "")}" level_num=${percentD(e.level_num)} num_goals=0 flag=LEVEL_UNLOCKED_TR level=Load_${loadOf(e)} ` +
                "sign_texture=sign_training dot_pos=(89.0, 82.0)} \n",
        )
        .join("");
}

export function signNumbers(manifest: Manifest): number[] {
    return manifest.levels.map((e) => e.level_num);
}

export function installFiles(game: string, overlay: string, manifest: Manifest | null = null): string[] {
    // The converted levels (every staging folder's) into the overlay's levels folder.
    void manifest;
    const installed: string[] = [];
    for (const staging of STAGINGS) {
        const source = path.join(game, staging, "levels");
        if (!fs.existsSync(source) || !fs.statSync(source).isDirectory()) continue;
        for (const entry of fs.readdirSync(source)) {
            const target = path.join(overlay, "levels", entry);
            if (fs.existsSync(target) && fs.statSync(target).isDirectory()) fs.rmSync(target, { recursive: true });
            const from = path.join(source, entry);
            // (shutil.copytree: a directory only, into a target that isn't there, file times kept)
            if (!fs.statSync(from).isDirectory()) throw new Error(`copytree: ${from} is not a directory`);
            if (fs.existsSync(target)) throw new Error(`copytree: ${target} exists`);
            fs.cpSync(from, target, { recursive: true, preserveTimestamps: true, errorOnExist: true, force: false });
            installed.push(entry);
        }
        // Game scripts a converter brings along (<staging>\scripts\...: THUG1's driving scripts, PC-THUG1-22 -
        // tools/thug1_levels drivingScripts) into the overlay's scripts folder, where they replace the game's file of
        // the same name (THUG2's empty player_controlled_vehicle.qb / misc\car_scripts.qb).
        const scripts = path.join(game, staging, "scripts");
        if (fs.existsSync(scripts) && fs.statSync(scripts).isDirectory()) {
            fs.cpSync(scripts, path.join(overlay, "scripts"), {
                recursive: true,
                preserveTimestamps: true,
                force: true,
            });
            installed.push(`scripts (${staging})`);
        }
        // Models a converter brings along (<staging>\models\<name>\...: THPS3's TPN-THPS3, THUGPRO's own models read from
        // its install) into the overlay's models folder, where the game looks for models\<name>\<name>.mdl.
        // (and the animations a converter brings for its own people: <staging>\anims\<game>\... - ped_models.ts)
        for (const folder of ["models", "anims"]) {
            const from = path.join(game, staging, folder);
            if (!fs.existsSync(from) || !fs.statSync(from).isDirectory()) continue;
            for (const entry of fs.readdirSync(from)) {
                const target = path.join(overlay, folder, entry);
                // (merged, not replaced: two stagings may both bring models\veh\... - THUG1's El train and leaf blower,
                // say; make_menu_level empties the overlay before each run anyway)
                fs.cpSync(path.join(from, entry), target, { recursive: true, preserveTimestamps: true, force: true });
                installed.push(`${folder}\\${entry}`);
            }
        }
    }
    return installed.sort((a, b) => (a < b ? -1 : a > b ? 1 : 0));
}
