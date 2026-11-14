// NOT ORIGINAL - the tick-rate yardstick on a recorded run (docs/design/tick-rate.md "Measuring"): a demo played at
// several [Graphics] tick_rate values (a demo recorded at 60 played at 120 / 144 / 240 holds each recorded frame over
// the ticks of its time - src/devtools/demo.cpp, "resampled"), and each run's check track compared with the 60 Hz one
// frame by frame (the tracks are in the recording's frames whatever the rate): the skater's position error over time,
// the velocity's, the state's, each jump's take-off, landing, airtime and apex, and the camera's.
//
//   node tools/game/tick_compare.ts --game <workspace> --demo <name> [--rates 60,120,240] [--out <folder>]
//                                   [--timeout 300] [--no-run]
//
// Each run: [Graphics] tick_rate=<rate> written to the ini (put back afterwards; a workspace's games run muted), the
// game started with -demo <name> -demo-quit, its <name>.play.track.txt / .play.run.json copied to <out>\<rate>\. Then
// <out>\report.md. --no-run: only the report, from the tracks already in <out>. Workspaces only - never the main install;
// take the workspace's lock first (tools/game/game_lock.ps1).
//
// The 60 Hz run is the reference: 60 stays the original (byte-identical), so "the same at any rate" means the same as it.

import { spawn } from "node:child_process";
import * as fs from "node:fs";
import * as path from "node:path";
import { exitWith, isMain, parseCli, print, runMain } from "thug2-level-converter-core/lib/cli.ts";
import { type Point, parseTrack } from "./demo_compare.ts";
import { clearAutomationFiles, launchExeOrError, resolveGameFolder, runningGames } from "./edit_level.ts";
import { ensureWorkspaceMuted, isWorkspace, setIniValue } from "./workspace_ini.ts";

const USAGE =
    "usage: node tools/game/tick_compare.ts --game <workspace> --demo <name> [--rates 60,120,240] [--out <folder>] " +
    "[--timeout 300] [--no-run]";

const HAS_CAMERA = 2; // (a check point's flags: demo_format::kHasCamera - the frame was drawn)
const AIR = 1; // (a check point's state: automation's trace - 0 ground, 1 air, 2 wall, 3 lip, 4 rail, 5 wallplant)
const DEMO_RATE = 60; // (the recording's frames a second: the tracks' frame numbers count them)

export interface Jump {
    takeoff: number; // the first frame in the air
    landing: number; // the first frame after it not in the air (-1: the track ends in the air)
    apex: number; // the highest y while in the air
}

// The jumps of a track: runs of frames in the air (state 1).
export function findJumps(track: Map<number, Point>): Jump[] {
    const frames = [...track.keys()].sort((a, b) => a - b);
    const jumps: Jump[] = [];
    let open: Jump | null = null;
    for (const frame of frames) {
        const point = track.get(frame)!;
        if (point.state === AIR) {
            if (!open) open = { takeoff: frame, landing: -1, apex: point.position[1] };
            else open.apex = Math.max(open.apex, point.position[1]);
        } else if (open) {
            open.landing = frame;
            jumps.push(open);
            open = null;
        }
    }
    if (open) jumps.push(open);
    return jumps;
}

export interface SecondError {
    second: number; // from the first frame compared
    maxPosition: number;
    maxVelocity: number;
}

export interface Comparison {
    compared: number;
    maxPosition: number;
    meanPosition: number;
    maxVelocity: number;
    maxCamera: number;
    stateMismatches: number;
    firstOver: number; // the first frame more than `tolerance` apart (-1: none)
    perSecond: SecondError[];
    jumps: { base: Jump; other: Jump | null }[];
}

function distance(a: readonly number[], b: readonly number[]): number {
    return Math.hypot(a[0]! - b[0]!, a[1]! - b[1]!, a[2]! - b[2]!);
}

// `other` against `base` (the 60 Hz run), frame by frame where both have a point; jumps matched in order by the nearest
// take-off.
export function compareRuns(base: Map<number, Point>, other: Map<number, Point>, tolerance = 0.01): Comparison {
    const out: Comparison = {
        compared: 0,
        maxPosition: 0,
        meanPosition: 0,
        maxVelocity: 0,
        maxCamera: 0,
        stateMismatches: 0,
        firstOver: -1,
        perSecond: [],
        jumps: [],
    };
    const frames = [...base.keys()].filter((f) => other.has(f)).sort((a, b) => a - b);
    const first = frames[0] ?? 0;
    let sum = 0;
    for (const frame of frames) {
        const a = base.get(frame)!;
        const b = other.get(frame)!;
        const position = distance(a.position, b.position);
        const velocity = distance(a.rest.slice(0, 3), b.rest.slice(0, 3));
        // (the camera only where both frames were drawn: an undrawn frame has none)
        const camera = (a.flags & HAS_CAMERA) !== 0 && (b.flags & HAS_CAMERA) !== 0 ? distance(a.camera, b.camera) : 0;
        out.compared++;
        sum += position;
        out.maxPosition = Math.max(out.maxPosition, position);
        out.maxVelocity = Math.max(out.maxVelocity, velocity);
        out.maxCamera = Math.max(out.maxCamera, camera);
        if (a.state !== b.state) out.stateMismatches++;
        if (out.firstOver < 0 && position > tolerance) out.firstOver = frame;
        const second = Math.floor((frame - first) / DEMO_RATE);
        while (out.perSecond.length <= second)
            out.perSecond.push({ second: out.perSecond.length, maxPosition: 0, maxVelocity: 0 });
        const bucket = out.perSecond[second]!;
        bucket.maxPosition = Math.max(bucket.maxPosition, position);
        bucket.maxVelocity = Math.max(bucket.maxVelocity, velocity);
    }
    out.meanPosition = out.compared ? sum / out.compared : 0;
    const theirs = findJumps(other);
    const used = new Set<number>();
    for (const jump of findJumps(base)) {
        let best = -1;
        for (let i = 0; i < theirs.length; i++) {
            if (used.has(i)) continue;
            const gap = Math.abs(theirs[i]!.takeoff - jump.takeoff);
            if (gap <= DEMO_RATE / 2 && (best < 0 || gap < Math.abs(theirs[best]!.takeoff - jump.takeoff))) best = i;
        }
        if (best >= 0) used.add(best);
        out.jumps.push({ base: jump, other: best >= 0 ? theirs[best]! : null });
    }
    return out;
}

function ms(frames: number): string {
    return `${((frames * 1000) / DEMO_RATE).toFixed(1)} ms`;
}

export function reportLines(demo: string, rates: readonly number[], comparisons: Map<number, Comparison>): string[] {
    const lines = [
        `# Tick-rate yardstick: ${demo}`,
        "",
        "Each rate's run against the 60 Hz run (frames are the recording's).",
        "",
    ];
    lines.push(
        "| rate | frames compared | first > 0.01 | max position | mean position | max velocity | max camera | state differs |",
        "|---|---|---|---|---|---|---|---|",
    );
    for (const rate of rates) {
        const c = comparisons.get(rate);
        if (!c) {
            lines.push(`| ${rate} | no track | | | | | | |`);
            continue;
        }
        lines.push(
            `| ${rate} | ${c.compared} | ${c.firstOver < 0 ? "none" : c.firstOver} | ${c.maxPosition.toFixed(3)} | ` +
                `${c.meanPosition.toFixed(3)} | ${c.maxVelocity.toFixed(2)} | ${c.maxCamera.toFixed(3)} | ` +
                `${c.stateMismatches} |`,
        );
    }
    for (const rate of rates) {
        const c = comparisons.get(rate);
        if (!c || rate === DEMO_RATE) continue;
        lines.push("", `## ${rate} Hz`, "", "Position error by second (the largest in it):", "");
        lines.push(`| second | position | velocity |`, "|---|---|---|");
        for (const s of c.perSecond)
            lines.push(`| ${s.second} | ${s.maxPosition.toFixed(3)} | ${s.maxVelocity.toFixed(2)} |`);
        lines.push("", "Jumps (60 Hz run / this run):", "");
        lines.push(
            "| take-off | landing | airtime | apex | take-off diff | landing diff | apex diff |",
            "|---|---|---|---|---|---|---|",
        );
        for (const { base, other } of c.jumps) {
            const air = base.landing >= 0 ? ms(base.landing - base.takeoff) : "-";
            if (!other) {
                lines.push(`| ${base.takeoff} | ${base.landing} | ${air} | ${base.apex.toFixed(2)} | missing | | |`);
                continue;
            }
            const landingDiff = base.landing >= 0 && other.landing >= 0 ? ms(other.landing - base.landing) : "-";
            lines.push(
                `| ${base.takeoff} | ${base.landing} | ${air} | ${base.apex.toFixed(2)} | ` +
                    `${ms(other.takeoff - base.takeoff)} | ${landingDiff} | ${(other.apex - base.apex).toFixed(3)} |`,
            );
        }
    }
    return lines;
}

async function runRate(exe: string, game: string, demo: string, rate: number, folder: string, timeout: number) {
    const ini = path.join(game, "thug2-decomp.ini");
    const before = fs.existsSync(ini) ? fs.readFileSync(ini, "utf8") : "";
    const demos = path.join(game, "thug2-decomp-demos");
    for (const ext of ["play.track.txt", "play.run.json"])
        fs.rmSync(path.join(demos, `${demo}.${ext}`), { force: true });
    fs.rmSync(folder, { recursive: true, force: true });
    fs.mkdirSync(folder, { recursive: true });
    try {
        fs.writeFileSync(ini, setIniValue(before, "Graphics", "tick_rate", String(rate)));
        ensureWorkspaceMuted(game);
        clearAutomationFiles(game);
        const started = Date.now();
        const child = spawn(exe, ["-demo", demo, "-demo-quit"], { cwd: game, stdio: "ignore" });
        const code = await new Promise<number | null>((resolve) => {
            const timer = setTimeout(() => {
                print(`${rate} Hz: still running after ${timeout} s - closed`);
                child.kill();
            }, timeout * 1000);
            child.on("exit", (exitCode) => {
                clearTimeout(timer);
                resolve(exitCode);
            });
        });
        print(`${rate} Hz: the game ended (exit code ${code}) after ${((Date.now() - started) / 1000).toFixed(1)} s`);
    } finally {
        fs.writeFileSync(ini, before);
    }
    for (const ext of ["play.track.txt", "play.run.json"]) {
        const from = path.join(demos, `${demo}.${ext}`);
        if (fs.existsSync(from)) fs.copyFileSync(from, path.join(folder, ext.replace("play.", "")));
    }
    const log = path.join(game, "thug2-decomp.log");
    if (fs.existsSync(log)) fs.copyFileSync(log, path.join(folder, "thug2-decomp.log"));
}

async function main(): Promise<number> {
    const { values } = parseCli(USAGE, {
        options: {
            game: { type: "string" },
            demo: { type: "string" },
            rates: { type: "string", default: "60,120,240" },
            out: { type: "string" },
            timeout: { type: "string", default: "300" },
            "no-run": { type: "boolean", default: false },
        },
    });
    if (!values.game || !values.demo) exitWith(`${USAGE}\nerror: --game and --demo are needed`);
    const game = resolveGameFolder(values.game);
    const demo = values.demo.replace(/\.demo$/i, "");
    const rates = values.rates
        .split(",")
        .map((r) => Number.parseInt(r, 10))
        .filter((r) => r > 0);
    if (!rates.includes(DEMO_RATE)) rates.unshift(DEMO_RATE);
    const out = path.resolve(values.out ?? path.join(game, "thug2-decomp-demos", `${demo}_ticks`));
    fs.mkdirSync(out, { recursive: true });
    if (!values["no-run"]) {
        if (!isWorkspace(game))
            exitWith(`${game} isn't a workspace (C:\\Games\\THUG2-n): the yardstick runs there only`);
        const found = launchExeOrError(values.game, game);
        if ("error" in found) exitWith(found.error);
        if (!fs.existsSync(path.join(game, "thug2-decomp-demos", `${demo}.demo`)))
            exitWith(`no ${path.join(game, "thug2-decomp-demos", `${demo}.demo`)}`);
        const here = path.resolve(game).toLowerCase();
        if (runningGames().some((p) => p === null || path.dirname(path.resolve(p)).toLowerCase() === here))
            exitWith(`THUG2 IS ALREADY RUNNING FROM ${game} - not started`);
        for (const rate of rates)
            await runRate(found.exe, game, demo, rate, path.join(out, String(rate)), Number(values.timeout));
    }
    const tracks = new Map<number, Map<number, Point>>();
    for (const rate of rates) {
        const file = path.join(out, String(rate), "track.txt");
        if (fs.existsSync(file)) tracks.set(rate, parseTrack(fs.readFileSync(file, "utf8")));
    }
    const base = tracks.get(DEMO_RATE);
    if (!base) exitWith(`no 60 Hz track in ${path.join(out, "60")}`);
    const comparisons = new Map<number, Comparison>();
    for (const [rate, track] of tracks) comparisons.set(rate, compareRuns(base, track));
    const lines = reportLines(demo, rates, comparisons);
    fs.writeFileSync(path.join(out, "report.md"), `${lines.join("\n")}\n`);
    print(lines.join("\n"));
    print(`\nreport: ${path.join(out, "report.md")}`);
    return 0;
}

if (isMain(import.meta.url)) await runMain(() => main());
