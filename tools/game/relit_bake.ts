// NOT ORIGINAL - the relit bake for every level of a game folder (docs/design/vulkan.md "Relit: the bake taken apart,
// offline"): each level's relit data - its baked lighting taken apart into the artists' colours, the sun's, the sky's,
// the bounce's and the lamps' light (src/engine/gfx/relit_data.h) - made by the game itself in a batch run per level:
// `thug2-decomp.exe -relit-bake <level>` (src/render_pc/relit_bake.h) starts straight in the level, muted, windowed and
// behind the user's windows, with its own settings over the ini (the user's ini isn't changed), writes
// thug2-decomp-relit\<scene>.relit beside the exe and closes. A level without data keeps the original game's lighting
// in the game (the relit mode, the time of day and the ray-traced lights out of effect there, the Options screen saying
// to run this).
//
//   node tools/game/relit_bake.ts --game <folder> [--levels all | a,b,...] [--skip-built] [--minutes 20]
//   node tools/game/relit_bake.ts --game <folder> --list
//
// --game: the game folder (the install root or its Game folder; default C:\Games\THUG2) - thug2-decomp.exe and its DLLs
// in it, built with the Vulkan renderer (ray tracing: a ray tracing GPU and the 64-bit game). --levels: the levels' load
// scripts without Load_ (BO, TR, foun...; default all: every installed level - THUG2's own, the overlay's and the
// ported ones, edit_level.ts --list's). --skip-built: a level whose data is there already is left as it is (its data
// made from the meshes the game makes now is the game's own check: an out-of-date file is reported in the game).
// --minutes: a level's bake given up after this long (the game closed). One game at a time; refused while a game runs
// from the folder (the user may be playing) - it never closes a game it didn't start. Nothing here goes online.
// Writes <game>\thug2-decomp-relit\bake-report.txt (a line per level: its verdict, its counts), state-table.txt (lighting
// round A2: each level's lighting state from its own data - stateRow - with the levels whose evidence disagrees flagged)
// and each level's game log
// as thug2-decomp-relit\logs\<level>.log. A level is made only when the game wrote its data and ended with 0; the game
// bakes only the level asked for (its load script running, its world the scene its level structure names) and ends with
// 2 when the level never came up, 3 when it came up with another scene - FAILED in the report, with the log's reason.

import { spawn } from "node:child_process";
import * as fs from "node:fs";
import * as path from "node:path";
import { exitWith, isMain, parseCli, print, runMain } from "thug2-level-converter-core/lib/cli.ts";
import {
    clearAutomationFiles,
    type Level,
    launchExeOrError,
    readLevels,
    resolveGameFolder,
    runningGames,
} from "./edit_level.ts";

const USAGE = `usage: relit_bake.ts [-h] [--game GAME] [--levels LEVELS] [--skip-built] [--minutes N] [--list]

Make every level's relit data (thug2-decomp.exe -relit-bake <level>, one level at a time).

options:
  -h, --help       show this help message and exit
  --game GAME      the game folder (default C:\\Games\\THUG2): the install root or its Game folder
  --levels LEVELS  all (default), or load scripts without Load_, comma-separated (BO,TR,foun)
  --skip-built     leave a level whose data file is there already
  --minutes N      give a level's bake up after N minutes (default 20)
  --list           only list the levels it would bake`;

export const DATA_FOLDER = "thug2-decomp-relit";

// What a bake's log says (src/render_pc/relit_bake.cpp's "relit bake:" lines).
export interface BakeResult {
    scene: string | null; // the level's scene checksum (8 hex: the data file's name)
    verdict: string | null; // "relit", "relit (no sun in the bake)", "the bake couldn't be taken apart"
    meshes: number | null;
    vertices: number | null;
    lamps: number | null;
    file: string | null; // the data file written
    written: boolean;
    sun: string | null; // the model's sun (r g b)
    lampTerm: string | null; // the model's lamps' c (r g b)
    // (lighting round A) the level's lighting state as its own data shows it: its time-of-day state at load, the baked
    // sun's confidence, the brightest surfaces against the median, its glows / self-lit triangles / lamps
    evidence: string | null;
    // (lighting round A2) the evidence's parts (stateOf) and the lamps' solve
    state: LevelState | null;
    lampsSolved: string | null; // "<told> of <all>"
    failure: string | null; // why nothing was written ("relit bake: FAILED - ..." / "start level: ...")
}

// The game's exit codes for a batch run (src/platform/test_workspace.h kBatch...).
export const EXIT_CODES: Readonly<Record<number, string>> = {
    2: "the level never came up",
    3: "the level came up with another scene",
};

// (lighting round A2) A level's lighting state as its bake's evidence line gives it - from its own data, never assumed.
export interface LevelState {
    tod: string; // its own time-of-day state at load
    sun: boolean; // the relit fit found a sun ...
    confidence: number; // ... carrying this share of the light
    elevation: number; // ... this many degrees up
    lamps: number;
    dark: boolean; // its median vertex brightness at most rt_lights.h kDarkLevelLuma
    openSky: number; // the median share of the sky its upward faces see (0 under a roof)
    sky: number; // the bake's own sky light on a level surface in the global unit (green)
    daylight: number; // the bake's own daylight over the physical at its sun (0: no sun)
    brightest: number; // its brightest 5% over its median
}

export function stateOf(evidence: string): LevelState | null {
    const m =
        /own time of day (\S+); the baked sun's confidence ([\d.]+) \((a sun|no sun), (-?[\d.]+) degrees up\); vertex light median [\d.]+, brightest 5% from [\d.]+ \(x([\d.]+)\); \d+ glow and \d+ self-lit triangles, (\d+) lamps \((a dark|a lit) level\); upward faces see ([\d.]+) of the sky \(median\); the bake's sky [\d.]+ ([\d.]+) [\d.]+ in the global unit, its daylight ([\d.]+) of the physical/.exec(
            evidence,
        );
    if (!m) return null;
    return {
        tod: m[1]!,
        sun: m[2] !== undefined && m[3] === "a sun",
        confidence: Number(m[2]),
        elevation: Number(m[4]),
        brightest: Number(m[5]),
        lamps: Number(m[6]),
        dark: m[7] === "a dark",
        openSky: Number(m[8]),
        sky: Number(m[9]),
        daylight: Number(m[10]),
    };
}

// Indoors when the level's upward faces see under this share of the sky (median); outdoors over OUTDOOR_SKY.
export const INDOOR_SKY = 0.15;
export const OUTDOOR_SKY = 0.4;

// Where a level's own evidence disagrees with itself (lighting round A2: "every level's day/night state must be verified
// from its own data"): its time-of-day state against the sun its bake shows, the sky its floors see, its brightness.
export function disagreements(state: LevelState): string[] {
    const out: string[] = [];
    const day = state.tod === "morning" || state.tod === "afternoon";
    if (state.tod === "night" && state.sun && state.elevation > 10)
        out.push(`a night state, but its bake shows a sun ${state.elevation.toFixed(0)} degrees up`);
    if (state.tod === "evening" && state.sun && state.elevation > 30)
        out.push(`an evening state, but its bake's sun is ${state.elevation.toFixed(0)} degrees up`);
    if (day && !state.sun && state.openSky >= OUTDOOR_SKY)
        out.push(
            `a ${state.tod} state outdoors (its floors see ${state.openSky.toFixed(2)} of the sky), but no sun found`,
        );
    if (state.sun && state.openSky < INDOOR_SKY)
        out.push(`a sun found, but its floors see only ${state.openSky.toFixed(2)} of the sky (indoors)`);
    if (day && state.dark && state.openSky >= OUTDOOR_SKY) out.push(`a ${state.tod} state outdoors, but a dark level`);
    if (!day && !state.dark && !state.sun && state.openSky >= OUTDOOR_SKY && state.lamps === 0)
        out.push(`a ${state.tod} state outdoors, lit, with no sun and no lamps`);
    return out;
}

export function stateRow(level: string, result: BakeResult): string {
    const st = result.state;
    if (!st) return `${level}	-	-	-	-	-	-	-	-	(no evidence line)`;
    const place = st.openSky < INDOOR_SKY ? "indoors" : st.openSky >= OUTDOOR_SKY ? "outdoors" : "partly roofed";
    const flags = disagreements(st);
    return [
        level,
        st.tod,
        st.sun
            ? `sun ${st.elevation.toFixed(0)} deg (${st.confidence.toFixed(2)})`
            : `no sun (${st.confidence.toFixed(2)})`,
        `sky ${st.sky.toFixed(3)}, daylight x${st.daylight.toFixed(2)}`,
        `${st.lamps} lamps${result.lampsSolved ? ` (${result.lampsSolved} solved)` : ""}`,
        `${place} (${st.openSky.toFixed(2)})`,
        st.dark ? "dark" : "lit",
        `brightest x${st.brightest.toFixed(2)}`,
        flags.length ? `DISAGREES: ${flags.join("; ")}` : "agrees",
    ].join("\t");
}

export function parseBakeLog(log: string): BakeResult {
    const out: BakeResult = {
        scene: null,
        verdict: null,
        meshes: null,
        vertices: null,
        lamps: null,
        file: null,
        written: false,
        sun: null,
        lampTerm: null,
        evidence: null,
        state: null,
        lampsSolved: null,
        failure: null,
    };
    const failure =
        /relit bake: FAILED - (.*)/.exec(log) ?? /start level: (.*(?:isn't|can't|the main menu stays).*)/.exec(log);
    if (failure) out.failure = failure[1]!.trim();
    const head =
        /relit bake: level ([0-9a-f]{8}) \([^)]*\) - ([^:]+): (\d+) meshes \(\d+ not relightable\), (\d+) vertices;.*?; (\d+) lamps;/.exec(
            log,
        );
    if (head) {
        out.scene = head[1]!;
        out.verdict = head[2]!.trim();
        out.meshes = Number(head[3]);
        out.vertices = Number(head[4]);
        out.lamps = Number(head[5]);
    }
    const model =
        /relit bake: the model sun ([\d.]+ [\d.]+ [\d.]+) sky [\d. ]+ bounce [\d. ]+ lamps ([\d.]+ [\d.]+ [\d.]+)/.exec(
            log,
        );
    if (model) {
        out.sun = model[1]!;
        out.lampTerm = model[2]!;
    }
    const evidence = /relit bake: evidence - (.*)/.exec(log);
    if (evidence) {
        out.evidence = evidence[1]!.trim();
        out.state = stateOf(out.evidence);
    }
    const solved = /relit bake: the lamps' solve - [\d ]+ edges, (\d+ of \d+) lamps told/.exec(log);
    if (solved) out.lampsSolved = solved[1]!;
    const file = /relit bake: (wrote|couldn't write) (\S+\.relit)/.exec(log);
    if (file) {
        out.written = file[1] === "wrote";
        out.file = file[2]!;
    }
    return out;
}

// The levels to bake: every installed one, or those named (with or without Load_, any case); unknown names reported.
export function chooseLevels(levels: readonly Level[], wanted: string): { chosen: Level[]; unknown: string[] } {
    const installed = levels.filter((l) => l.installed);
    if (wanted === "all") return { chosen: installed, unknown: [] };
    const chosen: Level[] = [];
    const unknown: string[] = [];
    for (const name of wanted
        .split(",")
        .map((n) => n.trim())
        .filter(Boolean)) {
        const key = name.replace(/^load_/i, "").toLowerCase();
        const level = installed.find((l) => l.load.toLowerCase() === key);
        if (level) {
            if (!chosen.includes(level)) chosen.push(level);
        } else unknown.push(name);
    }
    return { chosen, unknown };
}

// Whether a run's log shows its level started (src/game/front_end/start_level.cpp "start level: Load_<level> (free
// skate)"): a log without it baked whatever world was up - the main menu's, in builds before the bake checked its level -
// and its data isn't the level's.
export function startedLevel(log: string, level: string): boolean {
    const name = level.replace(/^load_/i, "").toLowerCase();
    for (const match of log.matchAll(/start level: Load_(\S+) \(free skate/gi)) {
        if (match[1]!.toLowerCase() === name) return true;
    }
    return false;
}

// Whether a run made its level's data: the file written and the game ended with 0 (any other end - an exit code of a
// failed batch run, a crash, killed - is a failure, whatever its log says).
export function bakeMade(code: number | null, result: BakeResult): boolean {
    return result.written && code === 0;
}

export function reportLine(level: string, code: number | null, result: BakeResult): string {
    if (!bakeMade(code, result)) {
        const why =
            result.failure ??
            (code !== null ? EXIT_CODES[code] : undefined) ??
            result.verdict ??
            "no bake line in the log";
        return `${level}\tFAILED\texit ${code ?? "killed"}\t${why}`;
    }
    return [
        level,
        result.scene ?? "?",
        result.verdict ?? "?",
        `${result.meshes ?? "?"} meshes`,
        `${result.vertices ?? "?"} vertices`,
        `${result.lamps ?? "?"} lamps`,
        `sun ${result.sun ?? "-"}`,
        `lamps' c ${result.lampTerm ?? "-"}`,
        result.evidence ?? "-",
    ].join("\t");
}

// One level's batch run: the exe started with -relit-bake <level> in the folder, waited for (given up after `minutes`:
// killed - it is our own run), its log read.
async function bakeOne(
    exe: string,
    game: string,
    level: string,
    minutes: number,
): Promise<{ code: number | null; log: string }> {
    clearAutomationFiles(game);
    const logFile = path.join(game, "thug2-decomp.log");
    fs.rmSync(logFile, { force: true });
    const child = spawn(exe, ["-relit-bake", level], { cwd: game, stdio: "ignore", windowsHide: false });
    const code = await new Promise<number | null>((resolve) => {
        const timer = setTimeout(() => {
            print(`  ${level}: no end after ${minutes} min - closed`);
            child.kill();
        }, minutes * 60_000);
        child.on("exit", (exitCode) => {
            clearTimeout(timer);
            resolve(exitCode);
        });
        child.on("error", () => {
            clearTimeout(timer);
            resolve(null);
        });
    });
    const log = fs.existsSync(logFile) ? fs.readFileSync(logFile, "utf8") : "";
    return { code, log };
}

async function main(): Promise<number> {
    const { values } = parseCli(USAGE, {
        options: {
            game: { type: "string", default: "C:\\Games\\THUG2" },
            levels: { type: "string", default: "all" },
            "skip-built": { type: "boolean", default: false },
            minutes: { type: "string", default: "20" },
            list: { type: "boolean", default: false },
        },
    });
    const game = resolveGameFolder(values.game);
    const found = launchExeOrError(values.game, game);
    if ("error" in found) exitWith(found.error);
    const { levels } = readLevels(game);
    const { chosen, unknown } = chooseLevels(levels, values.levels);
    if (unknown.length)
        exitWith(`no installed level ${unknown.join(", ")} in ${game} (node tools/game/edit_level.ts --list)`);
    if (values.list) {
        print(`${chosen.length} levels in ${game}: ${chosen.map((l) => l.load).join(" ")}`);
        return 0;
    }
    const minutes = Math.max(1, Number(values.minutes) || 20);
    const here = path.resolve(game).toLowerCase();
    const folder = path.join(game, DATA_FOLDER);
    const logs = path.join(folder, "logs");
    fs.mkdirSync(logs, { recursive: true });
    const lines: string[] = [];
    const states: string[] = [];
    let failed = 0;
    for (const level of chosen) {
        const running = runningGames().filter(
            (p) => p === null || path.dirname(path.resolve(p)).toLowerCase() === here,
        );
        if (running.length)
            exitWith(`THUG2 IS RUNNING FROM ${game} - not started (the user may be playing); close it first`);
        if (values["skip-built"]) {
            const beforeLog = fs.existsSync(path.join(logs, `${level.load}.log`))
                ? fs.readFileSync(path.join(logs, `${level.load}.log`), "utf8")
                : null;
            const before = beforeLog !== null ? parseBakeLog(beforeLog) : null;
            if (
                beforeLog !== null &&
                before?.written &&
                before.file &&
                fs.existsSync(before.file) &&
                startedLevel(beforeLog, level.load)
            ) {
                print(`${level.load}: built already (${path.basename(before.file)}) - left`);
                lines.push(reportLine(level.load, 0, before));
                states.push(stateRow(level.load, before));
                continue;
            }
        }
        const started = Date.now();
        print(`${level.load} (${level.name}, ${level.game}) ...`);
        const { code, log } = await bakeOne(found.exe, game, level.load, minutes);
        fs.writeFileSync(path.join(logs, `${level.load}.log`), log);
        const result = parseBakeLog(log);
        const line = reportLine(level.load, code, result);
        lines.push(line);
        if (bakeMade(code, result)) states.push(stateRow(level.load, result));
        failed += bakeMade(code, result) ? 0 : 1;
        print(`  ${line.replaceAll("\t", "  ")}  (${((Date.now() - started) / 1000).toFixed(0)} s)`);
    }
    fs.writeFileSync(path.join(folder, "bake-report.txt"), `${lines.join("\n")}\n`);
    const header =
        "level\ttod at load\tthe relit fit's sun (share)\tthe bake's sky\tlamps\tindoors / outdoors (floors' sky)\tdark / lit\tbrightest\tevidence";
    fs.writeFileSync(path.join(folder, "state-table.txt"), `${[header, ...states].join("\n")}\n`);
    const flagged = states.filter((row) => row.includes("DISAGREES")).length;
    print(`the levels' lighting states: state-table.txt (${flagged} whose evidence disagrees)`);
    print(`${chosen.length - failed} of ${chosen.length} levels' relit data made in ${folder} (bake-report.txt)`);
    return failed === 0 ? 0 : 1;
}

if (isMain(import.meta.url)) await runMain(() => main());
