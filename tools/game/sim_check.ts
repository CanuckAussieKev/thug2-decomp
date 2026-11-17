// NOT ORIGINAL - the simulation's exactness check on recorded runs (docs/design/simulation-rate.md "The proof"): the demos in
// tools/game/sim_reference played at 60 Hz with several display rates ([Graphics] max_fps: 0 = the original's one frame
// a tick, 120 / 144 / 240 = render-only frames blended between the ticks), and each run's simulation state compared
// frame by frame, bit for bit, with the reference recorded from the build before a change.
//
//   node tools/game/sim_check.ts --game <workspace> [--demos a,b] [--fps 0,120,144,240] [--load 0,7] [--out <folder>]
//                                [--timeout 300] [--no-run] [--record] [--trace] [--scripts qb|ts] [--rules single,net]
//
// --rules: the objects' dormancy each demo is played under (default both, each with its own reference set):
//   single - the single-player rule (every object updated and animated every step - engine/objects/dormancy.h),
//            references sim_reference/<demo>.state.txt;
//   net    - the net-game rule, the original's dormancy by distance (-demo-net-rule: forced outside a net game, in a
//            test workspace only), references sim_reference/net/<demo>.state.txt - the guard that online / LAN play
//            stays the original game's simulation. Those references equal, on the four original fields, the ones
//            recorded before the single-player rule (docs/design/simulation-rate.md "Objects' dormancy").
//
// --scripts: the scripts the workspace must run - qb (the .qb files, packed) or ts ([features] ts_scripts=1, the folder
// unpacked: tools/qbjs/unpack_game.ts) - refused when it doesn't; the report names the mode either way. The simulation
// is the same in both, bit for bit (docs/design/simulation-rate.md "QB and TypeScript scripts"): a change to the scripts' runtime is checked in both.
//
// --trace: each run also writes the simulation trace (-demo-trace: every script command with its arguments, every
// random draw, each check point's moving objects - src/devtools/demo.cpp) as trace.txt in its folder; two runs' traces
// diffed show the first command, draw or object that parts them.
//
// --load: each run also with [debug] load_test=N (N > 0: a 30 ms stall every Nth loop - late steps, not drawn), the
// simulation compared with the same reference: it must not depend on whether a step was drawn
// (docs/design/simulation-rate.md "Determinism under load"). 0 (the default alone) is the normal run.
//
// The state is the playback's <demo>.play.state.txt (src/devtools/demo.cpp state_line): per frame FNV-1a hashes of the
// skater's bits (position, velocity, matrix, state), of every moving object's, of the game's random streams, and the
// clock's milliseconds. With the time of day's clock running ([Graphics] tod / tod_cycle in the workspace's ini) each
// check point also has a "#tod <frame> <hours> <state> <sun x y z>" line (engine/gfx/day_clock.h state_line): the
// reference doesn't have them (a comment to it), and each run's are compared with the demo's first run - the same time,
// state and sun on every frame at every display rate and load. A run passes when every frame of the reference is there with the same line. --record writes
// the max_fps=0 run as the new reference (sim_reference/<demo>.state.txt) - only from the build the reference is meant
// to be (main before a change). The reference was made in a workspace's own data (scripts, levels): compare in the same
// kind of folder. Every run (--record's too) is played with REFERENCE_SETTINGS below set in the ini - the original
// game's simulation, no NOT ORIGINAL feature that adds to it (THAW's parked BMX) - and the report names those the
// workspace's ini had otherwise. Workspaces only - never the main install; take the workspace's lock first (tools/game/game_lock.ps1).

import { spawn } from "node:child_process";
import * as fs from "node:fs";
import * as path from "node:path";
import { fileURLToPath } from "node:url";
import { exitWith, isMain, parseCli, print, runMain } from "thug2-level-converter-core/lib/cli.ts";
import { clearAutomationFiles, launchExeOrError, resolveGameFolder, runningGames } from "./edit_level.ts";
import { ensureWorkspaceMuted, isWorkspace, setIniValue } from "./workspace_ini.ts";

const USAGE =
    "usage: node tools/game/sim_check.ts --game <workspace> [--demos a,b] [--fps 0,120,144,240] [--load 0,7] [--out <folder>] " +
    "[--timeout 300] [--no-run] [--record] [--trace] [--scripts qb|ts] [--rules single,net]";

export const REFERENCE_FOLDER = path.join(path.dirname(fileURLToPath(import.meta.url)), "sim_reference");

// The objects' dormancy a run is played under (--rules).
export type Rule = "single" | "net";
export const RULES: readonly Rule[] = ["single", "net"];

// A rule's reference folder: the single-player rule's at the top, the net-game rule's in net/.
export function referenceFolder(rule: Rule, folder = REFERENCE_FOLDER): string {
    return rule === "net" ? path.join(folder, "net") : folder;
}

// The comment demo.cpp writes at the top of a state file played under the net-game rule (-demo-net-rule honoured).
export const NET_RULE_MARK = "# dormancy: the net-game rule";

// A run's state played under the rule asked for: the net rule's mark there exactly when it was asked (a game that
// refused -demo-net-rule - not a test workspace - or an older build would otherwise pass as the other rule).
export function ruleHonoured(rule: Rule, stateText: string): boolean {
    const marked = stateText.split(/\r?\n/).some((line) => line.trim().startsWith(NET_RULE_MARK));
    return marked === (rule === "net");
}

export interface StateLine {
    frame: number;
    text: string; // the line after its frame: skater objects random clock_ms anim
}

// A state file's lines by frame (comments and blank lines skipped).
export function parseStates(text: string): Map<number, string> {
    const out = new Map<number, string>();
    for (const line of text.split(/\r?\n/)) {
        const trimmed = line.trim();
        if (!trimmed || trimmed.startsWith("#")) continue;
        const space = trimmed.indexOf(" ");
        if (space <= 0) continue;
        const frame = Number.parseInt(trimmed.slice(0, space), 10);
        if (Number.isNaN(frame)) continue;
        out.set(frame, trimmed.slice(space + 1).trim());
    }
    return out;
}

export interface StateComparison {
    frames: number; // the reference's
    matched: number;
    missing: number; // reference frames the run doesn't have
    firstDiffers: number; // -1: none
    firstWhat: string; // which hashes differ there (skater, objects, random, clock)
}

// (anim since 2026-11-17: every skeleton's bones and the animation LOD counters. A state recorded before it has the
// first four only, and a run is compared with it on those.)
const FIELDS = ["skater", "objects", "random", "clock", "anim"];

// A run's states against the reference's, frame by frame (on the fields the reference has).
export function compareStates(reference: Map<number, string>, run: Map<number, string>): StateComparison {
    const out: StateComparison = { frames: reference.size, matched: 0, missing: 0, firstDiffers: -1, firstWhat: "" };
    for (const frame of [...reference.keys()].sort((a, b) => a - b)) {
        const theirs = run.get(frame);
        if (theirs === undefined) {
            out.missing++;
            continue;
        }
        const a = reference.get(frame)!.split(/\s+/);
        const b = theirs.split(/\s+/).slice(0, a.length);
        if (a.join(" ") === b.join(" ")) {
            out.matched++;
            continue;
        }
        if (out.firstDiffers < 0) {
            out.firstDiffers = frame;
            out.firstWhat = FIELDS.filter((_, i) => i < a.length && a[i] !== b[i]).join(", ");
        }
    }
    return out;
}

// The time of day's lines (#tod <frame> <rest>) by frame.
export function parseTod(text: string): Map<number, string> {
    const out = new Map<number, string>();
    for (const line of text.split(/\r?\n/)) {
        const found = /^#tod (\d+) (.+)$/.exec(line.trim());
        if (found) out.set(Number.parseInt(found[1]!, 10), found[2]!.trim());
    }
    return out;
}

export interface TodComparison {
    frames: number; // the first run's
    matched: number;
    firstDiffers: number; // -1: none (a frame missing from the run counts as differing)
}

// A run's time of day against the demo's first run, frame by frame.
export function compareTod(first: Map<number, string>, run: Map<number, string>): TodComparison {
    const out: TodComparison = { frames: first.size, matched: 0, firstDiffers: -1 };
    for (const frame of [...first.keys()].sort((a, b) => a - b)) {
        if (run.get(frame) === first.get(frame)) out.matched++;
        else if (out.firstDiffers < 0) out.firstDiffers = frame;
    }
    return out;
}

export function todLine(demo: string, fps: number, c: TodComparison, load = 0): string {
    const shown = (fps === 0 ? "60 (max_fps=0)" : String(fps)) + (load > 0 ? `, load ${load}` : "");
    const ok = c.frames > 0 && c.firstDiffers < 0;
    return `| ${demo} | ${shown} | ${c.matched} / ${c.frames} | ${c.firstDiffers < 0 ? "none" : c.firstDiffers} | ${ok ? "ok" : "FAIL"} |`;
}

export function passed(c: StateComparison): boolean {
    return c.frames > 0 && c.missing === 0 && c.firstDiffers < 0;
}

// The log's count of the playback's steps whose frame wasn't drawn (late - "N of M frames not drawn"; -1: no line).
// The original's simulation reads whether a step's frame is drawn (it skips work when it isn't), so a run with late
// steps may part from the reference whatever the display rate: docs/design/simulation-rate.md "Late frames".
export function undrawnFrames(log: string): number {
    const found = /(\d+) of \d+ frames not drawn/.exec(log);
    return found ? Number.parseInt(found[1]!, 10) : -1;
}

// A run's folder name under the demo's: its display rate, its load when there is one and "net-" for the net-game rule
// ("144", "144-load7", "net-0").
export function runName(fps: number, load = 0, rule: Rule = "single"): string {
    const name = load > 0 ? `${fps}-load${load}` : String(fps);
    return rule === "net" ? `net-${name}` : name;
}

export function reportLine(demo: string, fps: number, c: StateComparison | null, undrawn = -1, load = 0): string {
    const shown = (fps === 0 ? "60 (max_fps=0)" : String(fps)) + (load > 0 ? `, load ${load}` : "");
    const late = undrawn < 0 ? "?" : String(undrawn);
    if (!c) return `| ${demo} | ${shown} | no state file | | | ${late} | FAIL |`;
    const first = c.firstDiffers < 0 ? "none" : `${c.firstDiffers} (${c.firstWhat})`;
    return `| ${demo} | ${shown} | ${c.matched} / ${c.frames} | ${c.missing} | ${first} | ${late} | ${passed(c) ? "ok" : "FAIL"} |`;
}

// The settings every run is played with, whatever the workspace's ini says (put back after the run with the rest): the
// references are the original game's simulation, so nothing that adds to it may be on. Each was found by a run:
// - [features] thaw_bmx=0: THAW's BMX parks its bike (`thaw_bmx_home`, game/rider/thaw_bmx_parked.h) beside the
//   skater at the level's start - a NOT ORIGINAL object with a motion component, so the objects' hash parts from
//   frame 30 in every demo (the skater, the random streams and the clock stay equal). An overlay made before the
//   parked bike (make_menu_level) has no such script, which is why a workspace with thaw_bmx=1 could still match.
// - [features] anteara_mod=0: the mod's scripts replace the game's.
// - [features] start_level / start_at / edit_level emptied: a start level keeps the demo from playing.
// Settings proven not to matter (every demo bit-identical with them on): ray tracing and the Vulkan renderer's
// options, [Graphics] tod / tod_cycle, lod_distance_scale 1 or 3, draw_distance_scale, QB or TypeScript scripts.
export const REFERENCE_SETTINGS: ReadonlyArray<readonly [section: string, key: string, value: string]> = [
    ["features", "thaw_bmx", "0"],
    ["features", "anteara_mod", "0"],
    ["features", "start_level", ""],
    ["features", "start_at", ""],
    ["features", "edit_level", ""],
];

// The ini a run is played with: the workspace's, its display rate and load, and the reference's settings.
export function runIni(text: string, fps: number, load: number): string {
    let out = setIniValue(text, "Graphics", "max_fps", String(fps));
    // (render-only frames aren't held back by the display's refresh: the rate asked for is drawn)
    out = setIniValue(out, "Graphics", "present_mode", fps === 0 ? "vsync" : "immediate");
    out = setIniValue(out, "debug", "load_test", String(load));
    for (const [section, key, value] of REFERENCE_SETTINGS) out = setIniValue(out, section, key, value);
    return out;
}

// The reference's settings the workspace's ini has otherwise (the report names them: set for the runs). A key not in
// the ini is its default: off, none.
export function settingsOverridden(text: string): string[] {
    const differs = ([section, key, value]: readonly [string, string, string]) => {
        const have = iniValue(text, section, key);
        return have !== value && !(have === "" && value === "0");
    };
    return REFERENCE_SETTINGS.filter(differs).map(
        ([section, key, value]) =>
            `[${section}] ${key}=${iniValue(text, section, key)} (played with ${value === "" ? "none" : value})`,
    );
}

// A playback's files (the trace only with --trace), copied into the run's folder without "play.".
const OUTPUTS = ["play.track.txt", "play.run.json", "play.state.txt", "play.trace.txt"];

async function playOnce(
    exe: string,
    game: string,
    demo: string,
    fps: number,
    load: number,
    folder: string,
    timeout: number,
    trace = false,
    rule: Rule = "single",
) {
    const ini = path.join(game, "thug2-decomp.ini");
    const before = fs.existsSync(ini) ? fs.readFileSync(ini, "utf8") : "";
    const demos = path.join(game, "thug2-decomp-demos");
    for (const ext of OUTPUTS) fs.rmSync(path.join(demos, `${demo}.${ext}`), { force: true });
    fs.rmSync(folder, { recursive: true, force: true });
    fs.mkdirSync(folder, { recursive: true });
    try {
        fs.writeFileSync(ini, runIni(before, fps, load));
        ensureWorkspaceMuted(game);
        clearAutomationFiles(game);
        const started = Date.now();
        const args = [
            "-demo",
            demo,
            "-demo-quit",
            ...(trace ? ["-demo-trace"] : []),
            ...(rule === "net" ? ["-demo-net-rule"] : []),
        ];
        const child = spawn(exe, args, { cwd: game, stdio: "ignore" });
        const code = await new Promise<number | null>((resolve) => {
            const timer = setTimeout(() => {
                print(`${demo} at ${runName(fps, load, rule)}: still running after ${timeout} s - closed`);
                child.kill();
            }, timeout * 1000);
            child.on("exit", (exitCode) => {
                clearTimeout(timer);
                resolve(exitCode);
            });
        });
        print(
            `${demo} at ${runName(fps, load, rule)}: the game ended (exit code ${code}) after ${((Date.now() - started) / 1000).toFixed(1)} s`,
        );
    } finally {
        fs.writeFileSync(ini, before);
    }
    for (const ext of OUTPUTS) {
        const from = path.join(demos, `${demo}.${ext}`);
        if (fs.existsSync(from)) fs.copyFileSync(from, path.join(folder, ext.replace("play.", "")));
    }
    const log = path.join(game, "thug2-decomp.log");
    if (fs.existsSync(log)) fs.copyFileSync(log, path.join(folder, "thug2-decomp.log"));
}

// A key's value in an ini's section (the last one there; "" when none) - the game's settings file.
export function iniValue(text: string, section: string, key: string): string {
    let inSection = false;
    let found = "";
    for (const raw of text.split(/\r?\n/)) {
        const line = raw.trim();
        const header = /^\[(.*)\]$/.exec(line);
        if (header) inSection = header[1]!.trim().toLowerCase() === section.toLowerCase();
        else if (inSection) {
            const eq = line.indexOf("=");
            if (eq > 0 && line.slice(0, eq).trim().toLowerCase() === key.toLowerCase())
                found = line.slice(eq + 1).trim();
        }
    }
    return found;
}

// The scripts a game folder runs: "ts" when [features] ts_scripts is set (not 0) and the folder is unpacked, "qb" when
// neither, "mismatch" otherwise (the game refuses to start: src/script_qb/ts_bridge.cpp check_folder_matches).
export function scriptsMode(iniText: string, unpacked: boolean): "qb" | "ts" | "mismatch" {
    const value = iniValue(iniText, "features", "ts_scripts");
    const ts = value !== "" && value !== "0";
    if (ts !== unpacked) return "mismatch";
    return ts ? "ts" : "qb";
}

export function folderScriptsMode(game: string): "qb" | "ts" | "mismatch" {
    const ini = path.join(game, "thug2-decomp.ini");
    const unpacked =
        fs.existsSync(path.join(game, "thug2-decomp-backup", "unpacked.json")) &&
        !fs.existsSync(path.join(game, "Data", "pre", "qb_scripts.prx"));
    return scriptsMode(fs.existsSync(ini) ? fs.readFileSync(ini, "utf8") : "", unpacked);
}

export function referenceDemos(folder = REFERENCE_FOLDER): string[] {
    if (!fs.existsSync(folder)) return [];
    return fs
        .readdirSync(folder)
        .filter((f) => f.endsWith(".demo"))
        .map((f) => f.replace(/\.demo$/, ""))
        .sort();
}

async function main(): Promise<number> {
    const { values } = parseCli(USAGE, {
        options: {
            game: { type: "string" },
            demos: { type: "string" },
            fps: { type: "string", default: "0,120,144,240" },
            load: { type: "string", default: "0" },
            out: { type: "string" },
            timeout: { type: "string", default: "300" },
            "no-run": { type: "boolean", default: false },
            record: { type: "boolean", default: false },
            trace: { type: "boolean", default: false },
            scripts: { type: "string" },
            rules: { type: "string", default: RULES.join(",") },
        },
    });
    if (!values.game) exitWith(`${USAGE}\nerror: --game is needed`);
    const game = resolveGameFolder(values.game);
    const demos = values.demos ? values.demos.split(",").map((d) => d.replace(/\.demo$/i, "")) : referenceDemos();
    if (demos.length === 0) exitWith(`no demos in ${REFERENCE_FOLDER}`);
    const rates = values.fps
        .split(",")
        .map((r) => Number.parseInt(r, 10))
        .filter((r) => r >= 0);
    if (values.record && !rates.includes(0)) rates.unshift(0);
    const loads = values.load
        .split(",")
        .map((r) => Number.parseInt(r, 10))
        .filter((r) => r >= 0);
    if (values.record && !loads.includes(0)) loads.unshift(0);
    const rules = values.rules.split(",").map((r) => r.trim());
    for (const rule of rules)
        if (!(RULES as readonly string[]).includes(rule))
            exitWith(`${USAGE}\nerror: --rules takes ${RULES.join(", ")}`);
    const ruleList = rules as Rule[];
    const mode = folderScriptsMode(game);
    if (values.scripts !== undefined && values.scripts !== mode)
        exitWith(
            `${game} runs ${mode === "mismatch" ? "neither (its ini and folder disagree)" : `${mode} scripts`}, not ${values.scripts} - ` +
                "switch it: node tools/qbjs/unpack_game.ts --game <folder> (ts; [features] ts_scripts=1) or --restore (qb; ts_scripts=0)",
        );
    const iniPath = path.join(game, "thug2-decomp.ini");
    const overridden = settingsOverridden(fs.existsSync(iniPath) ? fs.readFileSync(iniPath, "utf8") : "");
    const out = path.resolve(values.out ?? path.join(game, "thug2-decomp-demos", "sim_check"));
    fs.mkdirSync(out, { recursive: true });
    if (!values["no-run"]) {
        if (!isWorkspace(game)) exitWith(`${game} isn't a workspace (C:\\Games\\THUG2-n): the check runs there only`);
        const found = launchExeOrError(values.game, game);
        if ("error" in found) exitWith(found.error);
        const here = path.resolve(game).toLowerCase();
        if (runningGames().some((p) => p === null || path.dirname(path.resolve(p)).toLowerCase() === here))
            exitWith(`THUG2 IS ALREADY RUNNING FROM ${game} - not started`);
        const folder = path.join(game, "thug2-decomp-demos");
        fs.mkdirSync(folder, { recursive: true });
        for (const demo of demos) {
            const source = path.join(REFERENCE_FOLDER, `${demo}.demo`);
            if (fs.existsSync(source)) fs.copyFileSync(source, path.join(folder, `${demo}.demo`));
            if (!fs.existsSync(path.join(folder, `${demo}.demo`))) exitWith(`no ${demo}.demo`);
            for (const rule of ruleList)
                for (const load of loads)
                    for (const fps of rates)
                        await playOnce(
                            found.exe,
                            game,
                            demo,
                            fps,
                            load,
                            path.join(out, demo, runName(fps, load, rule)),
                            Number(values.timeout),
                            values.trace,
                            rule,
                        );
        }
    }
    const lines = [
        "# Simulation check",
        "",
        "Each run's state (src/devtools/demo.cpp state_line) against the reference, frame by frame, bit for bit.",
        "",
        `Scripts: ${mode === "ts" ? "TypeScript ([features] ts_scripts=1, the folder unpacked)" : mode === "qb" ? "QB (the .qb files)" : "the ini and the folder disagree"}.`,
        "",
        `Played with the reference's settings (REFERENCE_SETTINGS): ${REFERENCE_SETTINGS.map(([s, k, v]) => `[${s}] ${k}=${v}`).join(", ")}` +
            (overridden.length > 0
                ? ` - the workspace's ini differs: ${overridden.join(", ")}.`
                : " - the workspace's ini has them."),
        "",
        `Rules: ${ruleList.map((r) => (r === "net" ? "net (the net-game dormancy, -demo-net-rule; sim_reference/net)" : "single (the single-player rule; sim_reference)")).join(", ")}.`,
        "",
        "| demo | display rate | frames equal | missing | first differs | late frames | result |",
        "|---|---|---|---|---|---|---|",
    ];
    let failures = 0;
    const todLines: string[] = [];
    for (const demo of demos) {
        let firstTod: Map<number, string> | null = null;
        for (const rule of ruleList) {
            const refFolder = referenceFolder(rule);
            const label = rule === "net" ? `${demo} (net)` : demo;
            if (values.record) {
                const made = path.join(out, demo, runName(0, 0, rule), "state.txt");
                if (!fs.existsSync(made)) exitWith(`no ${made} to record`);
                fs.mkdirSync(refFolder, { recursive: true });
                fs.copyFileSync(made, path.join(refFolder, `${demo}.state.txt`));
                print(`recorded ${path.join(refFolder, `${demo}.state.txt`)}`);
            }
            const refFile = path.join(refFolder, `${demo}.state.txt`);
            if (!fs.existsSync(refFile)) exitWith(`no reference ${refFile} (--record makes it)`);
            const reference = parseStates(fs.readFileSync(refFile, "utf8"));
            for (const load of loads) {
                for (const fps of rates) {
                    const run = path.join(out, demo, runName(fps, load, rule));
                    const file = path.join(run, "state.txt");
                    const read = fs.existsSync(file) ? fs.readFileSync(file, "utf8") : null;
                    // (a run not played under its rule counts as no state: the game refused -demo-net-rule)
                    const text = read !== null && ruleHonoured(rule, read) ? read : null;
                    if (read !== null && text === null)
                        print(`${label} at ${runName(fps, load, rule)}: not played under the ${rule} rule`);
                    const c = text !== null ? compareStates(reference, parseStates(text)) : null;
                    const tod = text !== null ? parseTod(text) : new Map<number, string>();
                    firstTod ??= tod;
                    if (firstTod.size > 0) {
                        const clock = compareTod(firstTod, tod);
                        if (clock.firstDiffers >= 0) failures++;
                        todLines.push(todLine(label, fps, clock, load));
                    }
                    if (!c || !passed(c)) failures++;
                    const log = path.join(run, "thug2-decomp.log");
                    const late = fs.existsSync(log) ? undrawnFrames(fs.readFileSync(log, "utf8")) : -1;
                    lines.push(reportLine(label, fps, c, late, load));
                }
            }
        }
    }
    if (todLines.length > 0)
        lines.push(
            "",
            "Time of day (the clock's time, the level's state and the sun at each check point, against the demo's first run):",
            "",
            "| demo | display rate | frames equal | first differs | result |",
            "|---|---|---|---|---|",
            ...todLines,
        );
    fs.writeFileSync(path.join(out, "report.md"), `${lines.join("\n")}\n`);
    print(lines.join("\n"));
    print(`\nreport: ${path.join(out, "report.md")}`);
    return failures === 0 ? 0 : 1;
}

if (isMain(import.meta.url)) await runMain(() => main());
