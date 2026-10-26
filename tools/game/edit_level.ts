// NOT ORIGINAL - start the game straight into the level editor (src/game/front/start_level.h: `-edit <level>`).
//
//   node tools/pipeline/game/edit_level.ts                     the installed levels, by game (as --list)
//   node tools/pipeline/game/edit_level.ts --list              the same - prints only
//   node tools/pipeline/game/edit_level.ts <level>             THUG2 started with -edit <level>: free skate there, the level
//                                                     editor opened once the level is up
//   [--game <folder>]                                 default C:\Games\THUG2 (a stock install: its Game folder)
//
// <level>: a level's load script without Load_ (foun, z_ma, BO, TR...) or with it, as the list shows it. The levels
// are the ones the game knows: the data overlay's Levels.qb (tools/pipeline/menu_level/make_menu_level.ts: THUG2's own, the
// skate shop, TEST LEVEL and the ported levels), else the game's own Data\scripts\game\Levels.qb; the ported levels'
// games come from the converters' levels.json files (tools/pipeline/level_port/ported_levels.ts). A level whose scene file
// isn't there (Default: the PC release ships only its scripts) is left out - the game would refuse it too.
//
// Refuses when a game is already running from that folder (the user may be playing) - it never closes or kills one.
// The game reads nothing from this tool but its command line: the ini's [features] level_editor=0 makes it plain
// free skate (said here), and nothing here goes online.

import { execFileSync, spawn } from "node:child_process";
import * as fs from "node:fs";
import * as path from "node:path";
import { readManifest, STAGINGS } from "../level_port/ported_levels.ts";
import { Name, QbFile, Struct } from "../level_port/qb.ts";
import { parseCli } from "../lib/cli.ts";
import { GAME_EXE_NAMES } from "../lib/decomp.ts";
import { getCloseMatches } from "../lib/difflib.ts";
import { findInstall } from "../lib/install.ts";
import { isMain, print, pyExit, runMain } from "../lib/py.ts";
import { ensureWorkspaceMuted } from "./workspace_ini.ts";

const USAGE = `usage: edit_level.ts [-h] [--game GAME] [--list] [level]

Start THUG2 straight into the level editor in a level (-edit <level>), or list the installed levels.

positional arguments:
  level        a level's load script, with or without Load_ (foun, z_ma, BO, Load_TR)

options:
  -h, --help   show this help message and exit
  --game GAME  the game folder (default C:\\Games\\THUG2): the install root or its Game folder
  --list       only list the installed levels`;

const EXE_NAMES = GAME_EXE_NAMES; // (tools/pipeline/game/game_exe.ps1's order)
const OVERLAY = "thug2-decomp-data";
// Not editor levels: the front end, and the park editor's created park.
const SKIPPED = new Set(["load_mainmenu", "load_sk5ed"]);

export interface Level {
    load: string; // the load script's name without Load_ (what -edit takes)
    name: string; // the level structure's name= ("Boston", "Foundry")
    game: string; // "THUG2", or the ported level's game ("THPS3", "THPS4", "THUG1", "THAW")
    installed: boolean;
}

// The game folder: `folder` itself or its Game folder (a stock install) - tools/pipeline/lib/install.ts; `folder` (without
// trailing separators) when neither holds THUG2.
export function resolveGameFolder(folder: string): string {
    return findInstall(folder, "thug2") ?? folder.replace(/[\\/]+$/, "");
}

export function findExe(folder: string, exists: (p: string) => boolean = fs.existsSync): string | null {
    for (const name of EXE_NAMES) {
        const exe = path.join(folder, name);
        if (exists(exe)) return exe;
    }
    return null;
}

function text(file: QbFile, value: unknown): string | null {
    if (typeof value === "string") return value;
    if (value instanceof Name) return file.names.get(value.value) ?? null;
    return null;
}

// The levels of a Levels.qb (structures with a load_script), in file order. `ported`: lower-case load name -> game;
// `sceneThere(level)`: whether levels\<level>\<level>.scn.xbx is installed (asked only for levels without a scene
// pre - as the DLL's StartLevel::SceneInstalled).
export function levelsOf(
    file: QbFile,
    ported: ReadonlyMap<string, string>,
    sceneThere: (level: string) => boolean,
): Level[] {
    const levels: Level[] = [];
    const seen = new Set<string>();
    for (const [, value] of file.globals) {
        if (!(value instanceof Struct)) continue;
        const loadScript = text(file, value.get("load_script"));
        if (!loadScript || !/^load_/i.test(loadScript)) continue;
        const key = loadScript.toLowerCase();
        if (SKIPPED.has(key) || seen.has(key)) continue;
        seen.add(key);
        const load = loadScript.slice(5);
        const scene = text(file, value.get("level"));
        const installed = value.get("scnpre") !== undefined || (scene !== null && sceneThere(scene));
        const name = text(file, value.get("name")) ?? load;
        levels.push({ load, name, game: ported.get(load.toLowerCase()) ?? "THUG2", installed });
    }
    return levels;
}

// The level `wanted` names (with or without Load_, any case), or null.
export function findLevel(levels: readonly Level[], wanted: string): Level | null {
    const key = wanted.replace(/^load_/i, "").toLowerCase();
    return levels.find((l) => l.load.toLowerCase() === key) ?? null;
}

// The list as printed: THUG2's levels first, then each ported game's, the installed ones only.
export function formatList(levels: readonly Level[]): string[] {
    const games: string[] = [];
    for (const level of levels) if (!games.includes(level.game)) games.push(level.game);
    games.sort((a, b) => (a === "THUG2" ? -1 : b === "THUG2" ? 1 : 0));
    const lines: string[] = [];
    for (const game of games) {
        const mine = levels.filter((l) => l.game === game && l.installed);
        if (!mine.length) continue;
        lines.push(`${game === "THUG2" ? "THUG2 (its own, and the overlay's)" : `${game} (ported)`} - ${mine.length}:`);
        const width = Math.max(...mine.map((l) => l.load.length));
        for (const level of mine) lines.push(`  ${level.load.padEnd(width)}  ${level.name}`);
    }
    const missing = levels.filter((l) => !l.installed).map((l) => l.load);
    if (missing.length) lines.push(`(not installed - no scene file - left out: ${missing.join(", ")})`);
    return lines;
}

export function readLevels(game: string): { levels: Level[]; source: string } {
    const overlayQb = path.join(game, OVERLAY, "scripts", "game", "Levels.qb");
    const ownQb = path.join(game, "Data", "scripts", "game", "Levels.qb");
    const source = fs.existsSync(overlayQb) ? overlayQb : ownQb;
    if (!fs.existsSync(source)) pyExit(`no Levels.qb in ${game} (looked for ${overlayQb} and ${ownQb})`);
    const ported = new Map<string, string>();
    for (const staging of STAGINGS) {
        for (const entry of readManifest(path.join(game, staging, "levels.json"))?.levels ?? []) {
            ported.set(
                (entry.load ?? entry.level).toLowerCase(),
                entry.game ?? staging.replace("thug2-decomp-", "").toUpperCase(),
            );
        }
    }
    const sceneThere = (level: string): boolean =>
        [path.join(game, "Data"), path.join(game, OVERLAY)].some((root) =>
            fs.existsSync(path.join(root, "levels", level, `${level}.scn.xbx`)),
        );
    const file = new QbFile(fs.readFileSync(source), path.basename(source), true);
    return { levels: levelsOf(file, ported, sceneThere), source };
}

// Before a start: the last run's test-automation files gone (automation.ps1's Clear-AutomationFiles) - a game started
// here would otherwise run what an earlier run left in thug2-decomp-commands.txt (a `quit`, once anything is added).
export function clearAutomationFiles(game: string): void {
    for (const f of ["thug2-decomp-commands.txt", "thug2-decomp-status.json"])
        fs.rmSync(path.join(game, f), { force: true });
}

// The THUG2 processes' exe paths (null: one whose path can't be read). (`exit 0`: Get-Process finding none still
// fails the command, and execFileSync throws on that.)
export function runningGames(): (string | null)[] {
    const script =
        `Get-Process -Name ${EXE_NAMES.map((n) => n.replace(/\.exe$/i, "")).join(", ")} -ErrorAction SilentlyContinue | ` +
        "ForEach-Object { if ($_.Path) { $_.Path } else { '?' } }; exit 0";
    const out = execFileSync("powershell.exe", ["-NoProfile", "-NonInteractive", "-Command", script], {
        encoding: "utf8",
    });
    return out
        .split(/\r?\n/)
        .map((l) => l.trim())
        .filter((l) => l)
        .map((l) => (l === "?" ? null : l));
}

// The ini's [features] level_editor (true unless it says 0).
function editorOn(game: string): boolean {
    const ini = path.join(game, "thug2-decomp.ini");
    if (!fs.existsSync(ini)) return true;
    let section = "";
    for (const raw of fs.readFileSync(ini, "utf8").split(/\r?\n/)) {
        const line = raw.trim();
        const header = /^\[(.+)\]$/.exec(line);
        if (header) section = header[1]!.trim().toLowerCase();
        const setting = /^level_editor\s*=\s*(\S+)/i.exec(line);
        if (section === "features" && setting) return Number.parseInt(setting[1]!, 10) !== 0;
    }
    return true;
}

function main(): number {
    const { values, positionals } = parseCli(USAGE, {
        options: {
            game: { type: "string", default: "C:\\Games\\THUG2" },
            list: { type: "boolean", default: false },
        },
        allowPositionals: true,
    });
    if (positionals.length > 1) {
        process.stderr.write(`${USAGE}\nerror: one level at a time\n`);
        return 2;
    }
    const game = resolveGameFolder(values.game);
    const exe = findExe(game);
    if (!exe) pyExit(`no game exe (the reference build or THUG2.exe) in ${values.game} (nor in its Game folder)`);
    const { levels, source } = readLevels(game);
    const wanted = positionals[0];
    if (values.list || wanted === undefined) {
        print(
            `Levels in ${game} (from ${path.relative(game, source)}) - edit one with: node tools/pipeline/game/edit_level.ts <level>`,
        );
        for (const line of formatList(levels)) print(line);
        return 0;
    }
    const level = findLevel(levels, wanted);
    if (!level) {
        const close = getCloseMatches(
            wanted.replace(/^load_/i, "").toLowerCase(),
            levels.filter((l) => l.installed).map((l) => l.load.toLowerCase()),
        );
        pyExit(`no level ${wanted} in ${game}${close.length ? ` - did you mean ${close.join(", ")}?` : ""} (--list)`);
    }
    if (!level.installed) pyExit(`${level.load}: its scene file isn't installed - the game can't load it`);
    const here = path.resolve(game).toLowerCase();
    const running = runningGames().filter((p) => p === null || path.dirname(path.resolve(p)).toLowerCase() === here);
    if (running.length) {
        pyExit(
            `THUG2 IS ALREADY RUNNING FROM ${game}${running.includes(null) ? " (or a copy whose folder can't be read)" : ""} - ` +
                "not started (the user may be playing); close it first",
        );
    }
    if (!editorOn(game)) print("note: the ini has [features] level_editor=0 - the game starts in free skate there");
    if (ensureWorkspaceMuted(game)) print(`a workspace: [features] mute_audio=1 (its games always run muted)`);
    clearAutomationFiles(game);
    const child = spawn(exe, ["-edit", level.load], { cwd: game, detached: true, stdio: "ignore" });
    child.unref();
    print(`started ${exe} -edit ${level.load} (${level.name}, ${level.game}; pid ${child.pid ?? "?"})`);
    print(`the level editor opens once the level is up; ${path.join(game, "thug2-decomp.log")} says how it went`);
    return 0;
}

if (isMain(import.meta.url)) await runMain(() => main());
