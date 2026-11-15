// NOT ORIGINAL - the simulation's exactness check on recorded runs (docs/design/tick-rate.md "The proof"): the demos in
// tools/game/sim_reference played at 60 Hz with several display rates ([Graphics] max_fps: 0 = the original's one frame
// a tick, 120 / 144 / 240 = render-only frames blended between the ticks), and each run's simulation state compared
// frame by frame, bit for bit, with the reference recorded from the build before a change.
//
//   node tools/game/sim_check.ts --game <workspace> [--demos a,b] [--fps 0,120,144,240] [--out <folder>]
//                                [--timeout 300] [--no-run] [--record]
//
// The state is the playback's <demo>.play.state.txt (src/devtools/demo.cpp state_line): per frame FNV-1a hashes of the
// skater's bits (position, velocity, matrix, state), of every moving object's, of the game's random streams, and the
// clock's milliseconds. A run passes when every frame of the reference is there with the same line. --record writes
// the max_fps=0 run as the new reference (sim_reference/<demo>.state.txt) - only from the build the reference is meant
// to be (main before a change). The reference was made in a workspace's own data (scripts, levels): compare in the same
// kind of folder. Workspaces only - never the main install; take the workspace's lock first (tools/game/game_lock.ps1).

import { spawn } from "node:child_process";
import * as fs from "node:fs";
import * as path from "node:path";
import { fileURLToPath } from "node:url";
import { exitWith, isMain, parseCli, print, runMain } from "thug2-level-converter-core/lib/cli.ts";
import { clearAutomationFiles, launchExeOrError, resolveGameFolder, runningGames } from "./edit_level.ts";
import { ensureWorkspaceMuted, isWorkspace, setIniValue } from "./workspace_ini.ts";

const USAGE =
    "usage: node tools/game/sim_check.ts --game <workspace> [--demos a,b] [--fps 0,120,144,240] [--out <folder>] " +
    "[--timeout 300] [--no-run] [--record]";

export const REFERENCE_FOLDER = path.join(path.dirname(fileURLToPath(import.meta.url)), "sim_reference");

export interface StateLine {
    frame: number;
    text: string; // the line after its frame: skater objects random clock_ms
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

const FIELDS = ["skater", "objects", "random", "clock"];

// A run's states against the reference's, frame by frame.
export function compareStates(reference: Map<number, string>, run: Map<number, string>): StateComparison {
    const out: StateComparison = { frames: reference.size, matched: 0, missing: 0, firstDiffers: -1, firstWhat: "" };
    for (const frame of [...reference.keys()].sort((a, b) => a - b)) {
        const theirs = run.get(frame);
        if (theirs === undefined) {
            out.missing++;
            continue;
        }
        const ours = reference.get(frame)!;
        if (ours === theirs) {
            out.matched++;
            continue;
        }
        if (out.firstDiffers < 0) {
            out.firstDiffers = frame;
            const a = ours.split(/\s+/);
            const b = theirs.split(/\s+/);
            out.firstWhat = FIELDS.filter((_, i) => a[i] !== b[i]).join(", ");
        }
    }
    return out;
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

export function reportLine(demo: string, fps: number, c: StateComparison | null, undrawn = -1): string {
    const shown = fps === 0 ? "60 (max_fps=0)" : String(fps);
    const late = undrawn < 0 ? "?" : String(undrawn);
    if (!c) return `| ${demo} | ${shown} | no state file | | | ${late} | FAIL |`;
    const first = c.firstDiffers < 0 ? "none" : `${c.firstDiffers} (${c.firstWhat})`;
    return `| ${demo} | ${shown} | ${c.matched} / ${c.frames} | ${c.missing} | ${first} | ${late} | ${passed(c) ? "ok" : "FAIL"} |`;
}

async function playOnce(exe: string, game: string, demo: string, fps: number, folder: string, timeout: number) {
    const ini = path.join(game, "thug2-decomp.ini");
    const before = fs.existsSync(ini) ? fs.readFileSync(ini, "utf8") : "";
    const demos = path.join(game, "thug2-decomp-demos");
    for (const ext of ["play.track.txt", "play.run.json", "play.state.txt"])
        fs.rmSync(path.join(demos, `${demo}.${ext}`), { force: true });
    fs.rmSync(folder, { recursive: true, force: true });
    fs.mkdirSync(folder, { recursive: true });
    try {
        let text = setIniValue(before, "Graphics", "max_fps", String(fps));
        // (render-only frames aren't held back by the display's refresh: the rate asked for is drawn)
        text = setIniValue(text, "Graphics", "present_mode", fps === 0 ? "vsync" : "immediate");
        fs.writeFileSync(ini, text);
        ensureWorkspaceMuted(game);
        clearAutomationFiles(game);
        const started = Date.now();
        const child = spawn(exe, ["-demo", demo, "-demo-quit"], { cwd: game, stdio: "ignore" });
        const code = await new Promise<number | null>((resolve) => {
            const timer = setTimeout(() => {
                print(`${demo} at max_fps=${fps}: still running after ${timeout} s - closed`);
                child.kill();
            }, timeout * 1000);
            child.on("exit", (exitCode) => {
                clearTimeout(timer);
                resolve(exitCode);
            });
        });
        print(
            `${demo} at max_fps=${fps}: the game ended (exit code ${code}) after ${((Date.now() - started) / 1000).toFixed(1)} s`,
        );
    } finally {
        fs.writeFileSync(ini, before);
    }
    for (const ext of ["play.track.txt", "play.run.json", "play.state.txt"]) {
        const from = path.join(demos, `${demo}.${ext}`);
        if (fs.existsSync(from)) fs.copyFileSync(from, path.join(folder, ext.replace("play.", "")));
    }
    const log = path.join(game, "thug2-decomp.log");
    if (fs.existsSync(log)) fs.copyFileSync(log, path.join(folder, "thug2-decomp.log"));
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
            out: { type: "string" },
            timeout: { type: "string", default: "300" },
            "no-run": { type: "boolean", default: false },
            record: { type: "boolean", default: false },
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
            for (const fps of rates)
                await playOnce(found.exe, game, demo, fps, path.join(out, demo, String(fps)), Number(values.timeout));
        }
    }
    const lines = [
        "# Simulation check",
        "",
        "Each run's state (src/devtools/demo.cpp state_line) against the reference, frame by frame, bit for bit.",
        "",
        "| demo | display rate | frames equal | missing | first differs | late frames | result |",
        "|---|---|---|---|---|---|---|",
    ];
    let failures = 0;
    for (const demo of demos) {
        if (values.record) {
            const made = path.join(out, demo, "0", "state.txt");
            if (!fs.existsSync(made)) exitWith(`no ${made} to record`);
            fs.copyFileSync(made, path.join(REFERENCE_FOLDER, `${demo}.state.txt`));
            print(`recorded ${path.join(REFERENCE_FOLDER, `${demo}.state.txt`)}`);
        }
        const refFile = path.join(REFERENCE_FOLDER, `${demo}.state.txt`);
        if (!fs.existsSync(refFile)) exitWith(`no reference ${refFile} (--record makes it)`);
        const reference = parseStates(fs.readFileSync(refFile, "utf8"));
        for (const fps of rates) {
            const file = path.join(out, demo, String(fps), "state.txt");
            const c = fs.existsSync(file) ? compareStates(reference, parseStates(fs.readFileSync(file, "utf8"))) : null;
            if (!c || !passed(c)) failures++;
            const log = path.join(out, demo, String(fps), "thug2-decomp.log");
            lines.push(reportLine(demo, fps, c, fs.existsSync(log) ? undrawnFrames(fs.readFileSync(log, "utf8")) : -1));
        }
    }
    fs.writeFileSync(path.join(out, "report.md"), `${lines.join("\n")}\n`);
    print(lines.join("\n"));
    print(`\nreport: ${path.join(out, "report.md")}`);
    return failures === 0 ? 0 : 1;
}

if (isMain(import.meta.url)) await runMain(() => main());
