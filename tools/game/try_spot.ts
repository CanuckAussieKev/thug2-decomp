// NOT ORIGINAL - start the game in a level with the skater standing at a spot, to try something there by hand
// (src/game/front/start_level.h: `-level <level> -at x,y,z[,heading]`).
//
//   node tools/pipeline/game/try_spot.ts <level> <x,y,z[,heading]>   THUG2 started in free skate there; once the level is up
//                                                            the skater stands at the spot (on the ground below it),
//                                                            facing `heading` (degrees: 0 = +z, 90 = +x)
//   node tools/pipeline/game/try_spot.ts --list <list>               a list of spots (tools/pipeline/game/spots.json) and the command
//                                                            for each - `collfix`: THUGPRO's collision overlays in the
//                                                            THPS3 ports
//   node tools/pipeline/game/try_spot.ts --list <list> <n>           start the list's n-th spot (1-based)
//   [--game <folder>]                                       default C:\Games\THUG2 (a stock install: its Game folder)
//
// A spot is what the screenshot sidecar's skater position gives (x, y, z, or "(x, y, z)") and optionally its heading.
// Refuses when a game is already running from that folder (the user may be playing) - it never closes or kills one.
// Nothing here goes online: the level is loaded in free skate.

import { spawn } from "node:child_process";
import * as fs from "node:fs";
import * as path from "node:path";
import { fileURLToPath } from "node:url";
import { parseCli } from "../lib/cli.ts";
import { getCloseMatches } from "../lib/difflib.ts";
import { isMain, print, pyExit, runMain } from "../lib/py.ts";
import { clearAutomationFiles, findExe, findLevel, readLevels, resolveGameFolder, runningGames } from "./edit_level.ts";
import { ensureWorkspaceMuted } from "./workspace_ini.ts";

const USAGE = `usage: try_spot.ts [-h] [--game GAME] [--list LIST] [level] [spot]

Start THUG2 in a level with the skater standing still at a spot (-level <level> -at x,y,z[,heading]).

positional arguments:
  level        a level's load script, with or without Load_ (foun, sub, BO, z_ma)
  spot         x,y,z or x,y,z,heading (degrees: 0 = +z, 90 = +x); quote it if it has spaces

options:
  -h, --help   show this help message and exit
  --game GAME  the game folder (default C:\\Games\\THUG2): the install root or its Game folder (before the spot)
  --list LIST  print a list of spots from tools/pipeline/game/spots.json (collfix: THUGPRO's collision overlays) with the
               command for each; with a number instead of the level, start that spot of the list`;

export interface Spot {
    x: number;
    y: number;
    z: number;
    heading?: number;
}

// As the DLL's StartLevel::ParseSpot: three or four numbers separated by commas and / or spaces, optionally in
// parentheses. Null for anything else.
export function parseSpot(text: string): Spot | null {
    let s = text.trim();
    if (s.startsWith("(") && s.endsWith(")")) s = s.slice(1, -1);
    const fields = s.split(/\s*,\s*|\s+/);
    if (fields.length < 3 || fields.length > 4) return null;
    const numbers: number[] = [];
    for (const f of fields) {
        if (!/^[-+]?(\d+\.?\d*|\.\d+)([eE][-+]?\d+)?$/.test(f)) return null;
        const v = Number(f);
        if (!Number.isFinite(v)) return null;
        numbers.push(v);
    }
    const [x, y, z, heading] = numbers as [number, number, number, number | undefined];
    return heading === undefined ? { x, y, z } : { x, y, z, heading };
}

// The spot as -at takes it: no spaces, so it needs no quotes.
export function formatSpot(spot: Spot): string {
    return [spot.x, spot.y, spot.z, ...(spot.heading === undefined ? [] : [spot.heading])].join(",");
}

export interface ListedSpot {
    level: string;
    name: string;
    at: number[];
    note?: string;
}

const SPOTS = path.join(path.dirname(fileURLToPath(import.meta.url)), "spots.json");

// The command line with "--" before the first word that is a negative number (a spot such as -1584,25.8,-723.9 isn't
// an option): what comes after it is positional, so --game goes before the spot.
export function withSpotGuard(argv: readonly string[]): string[] {
    const i = argv.findIndex((a) => /^-[\d.]/.test(a));
    return i < 0 || argv.slice(0, i).includes("--") ? [...argv] : [...argv.slice(0, i), "--", ...argv.slice(i)];
}

export function readList(name: string, file: string = SPOTS): ListedSpot[] {
    const data = JSON.parse(fs.readFileSync(file, "utf8")) as Record<string, unknown>;
    const lists = Object.keys(data).filter((k) => !k.startsWith("_"));
    const list = data[name];
    if (!Array.isArray(list)) pyExit(`no list ${name} in ${file} (lists: ${lists.join(", ")})`);
    return list as ListedSpot[];
}

// The list as printed: a numbered line per spot and its command.
export function formatList(name: string, spots: readonly ListedSpot[]): string[] {
    const lines: string[] = [];
    spots.forEach((s, i) => {
        const spot = parseSpot(s.at.join(","));
        lines.push(`${i + 1}. ${s.level} ${s.name}${s.note ? ` - ${s.note}` : ""}`);
        lines.push(`   node tools/pipeline/game/try_spot.ts ${s.level} ${spot ? formatSpot(spot) : s.at.join(",")}`);
        lines.push(`   (or: node tools/pipeline/game/try_spot.ts --list ${name} ${i + 1})`);
    });
    return lines;
}

function main(): number {
    const { values, positionals } = parseCli(USAGE, {
        args: withSpotGuard(process.argv.slice(2)),
        options: {
            game: { type: "string", default: "C:\\Games\\THUG2" },
            list: { type: "string" },
        },
        allowPositionals: true,
    });
    let levelName: string | undefined;
    let spotText: string | undefined;
    if (values.list !== undefined) {
        const spots = readList(values.list);
        if (positionals.length === 0) {
            print(`Spots in the list ${values.list} (tools/pipeline/game/spots.json) - start one with its command:`);
            for (const line of formatList(values.list, spots)) print(line);
            return 0;
        }
        const n = Number(positionals[0]);
        const chosen = Number.isInteger(n) ? spots[n - 1] : undefined;
        if (positionals.length > 1 || !chosen) pyExit(`--list ${values.list} takes a number from 1 to ${spots.length}`);
        levelName = chosen.level;
        spotText = chosen.at.join(",");
        print(`${n}. ${chosen.level} ${chosen.name}${chosen.note ? ` - ${chosen.note}` : ""}`);
    } else {
        // (a spot with spaces given unquoted: the words after the level joined again)
        if (positionals.length < 2) {
            process.stderr.write(`${USAGE}\nerror: a level and a spot (or --list collfix)\n`);
            return 2;
        }
        levelName = positionals[0]!;
        spotText = positionals.slice(1).join(" ");
    }
    const spot = parseSpot(spotText);
    if (!spot) pyExit(`"${spotText}" isn't x,y,z or x,y,z,heading`);
    const game = resolveGameFolder(values.game);
    const exe = findExe(game);
    if (!exe) pyExit(`no game exe (the reference build or THUG2.exe) in ${values.game} (nor in its Game folder)`);
    const { levels } = readLevels(game);
    const level = findLevel(levels, levelName);
    if (!level) {
        const close = getCloseMatches(
            levelName.replace(/^load_/i, "").toLowerCase(),
            levels.filter((l) => l.installed).map((l) => l.load.toLowerCase()),
        );
        pyExit(`no level ${levelName} in ${game}${close.length ? ` - did you mean ${close.join(", ")}?` : ""}`);
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
    const at = formatSpot(spot);
    if (ensureWorkspaceMuted(game)) print(`a workspace: [features] mute_audio=1 (its games always run muted)`);
    clearAutomationFiles(game);
    const child = spawn(exe, ["-level", level.load, "-at", at], { cwd: game, detached: true, stdio: "ignore" });
    child.unref();
    print(`started ${exe} -level ${level.load} -at ${at} (${level.name}, ${level.game}; pid ${child.pid ?? "?"})`);
    print(`the skater stands there once the level is up; ${path.join(game, "thug2-decomp.log")} says how it went`);
    return 0;
}

if (isMain(import.meta.url)) await runMain(() => main());
