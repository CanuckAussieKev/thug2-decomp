// NOT ORIGINAL - whether a long session stays as fast at its end as at its start: the game's log read for the renderer's
// windows ("vulkan: frame N: ... X ms a frame ... over the last K; uploads U (M MB)" and the ray-tracing scene's
// "B builds ... (asked for: n new meshes, g new geometry, w written)") over the level the session spent the longest in
// (the stretch between two loading screens with the most frames), and a verdict: the frame time, the uploads a frame
// and the BLAS builds a frame of the last third of it no more than kGrowth times the first third's (plus a little).
// The guard of the 3 fps Boston (a running time of day relit the same meshes, uploaded them and built them again every
// frame, more the slower the frames): run a session with frame_bench.ts --scenario
// tools/game/scenarios/live_settings_soak.txt, then this over its game.log.
//
//   node tools/game/soak_check.ts <game.log>
//
// It also fails a session that stalled: the stall watch's "stall:" lines (a main-loop loop past 1 s with no loading
// screen up) and the device watch's ("vulkan: frame N has waited X s for the device"). The College freeze's guard:
// tools/game/scenarios/college_tod_cycle_soak.txt.
//
// Exit 0 flat and no stall, 1 growing or stalled (the windows printed either way).

import * as fs from "node:fs";
import { exitWith, isMain, parseCli, print, runMain } from "thug2-level-converter-core/lib/cli.ts";

const USAGE = "usage: soak_check.ts <game.log>";

export const GROWTH = 1.5; // the last third over the first, at most ...
const SLACK_MS = 2; // ... plus this much frame time
const SLACK_UPLOADS = 50; // ... uploads a frame
const SLACK_BUILDS = 1; // ... BLAS builds a frame

export interface Window {
    frame: number; // the frame the line was written at
    frames: number; // the frames it covers
    ms: number; // a frame, on average
    uploads: number; // in the window
    megabytes: number;
    builds: number; // BLAS builds ever, at the scene's last line before it (or after it in the stretch)
    askedWritten: number; // of the builds asked for, by a mesh's vertices written (ever)
}

const FRAME_LINE =
    /^vulkan: frame (\d+): .*?; ([\d.]+) ms a frame .*? over the last (\d+); uploads (\d+) \(([\d.]+) MB\)/;
const SCENE_LINE =
    /^vulkan: ray tracing scene: .*?; (\d+) builds, \d+ refits, \d+ TLAS builds(?: \(asked for: \d+ new meshes, \d+ new geometry, (\d+) written[;)])?/;
const LOADING_LINE = /^loading console: the loading screen was up/;

// The log's stretches between loading screens, each its windows (the scene's counts as the last line before each).
export function stretches(log: string): Window[][] {
    const out: Window[][] = [[]];
    let builds = 0;
    let askedWritten = 0;
    for (const line of log.split(/\r?\n/)) {
        if (LOADING_LINE.test(line)) {
            out.push([]);
            continue;
        }
        const scene = SCENE_LINE.exec(line);
        if (scene) {
            builds = Number(scene[1]);
            askedWritten = scene[2] !== undefined ? Number(scene[2]) : askedWritten;
            continue;
        }
        const frame = FRAME_LINE.exec(line);
        if (frame) {
            out[out.length - 1]!.push({
                frame: Number(frame[1]),
                frames: Number(frame[3]),
                ms: Number(frame[2]),
                uploads: Number(frame[4]),
                megabytes: Number(frame[5]),
                builds,
                askedWritten,
            });
        }
    }
    return out.filter((windows) => windows.length > 0);
}

export interface Third {
    frames: number;
    ms: number; // a frame
    uploads: number; // a frame
    builds: number; // a frame
    askedWritten: number; // a frame
}

// A run of windows summed: frame time, uploads and builds a frame (the builds from the counts' growth across it).
export function third(windows: readonly Window[], before: Window | undefined): Third {
    const frames = windows.reduce((sum, w) => sum + w.frames, 0);
    const ms = windows.reduce((sum, w) => sum + w.ms * w.frames, 0);
    const uploads = windows.reduce((sum, w) => sum + w.uploads, 0);
    const last = windows[windows.length - 1];
    const first = before ?? windows[0];
    const builds = last && first ? last.builds - first.builds : 0;
    const asked = last && first ? last.askedWritten - first.askedWritten : 0;
    const per = (n: number): number => (frames > 0 ? n / frames : 0);
    return { frames, ms: per(ms), uploads: per(uploads), builds: per(builds), askedWritten: per(asked) };
}

export interface Verdict {
    first: Third;
    last: Third;
    growing: string[]; // what grew past the allowance
}

// The longest stretch (most frames) without its first window (the level's first frames), in thirds: the last against
// the first.
export function verdict(log: string): Verdict | null {
    const all = stretches(log);
    let longest: Window[] = [];
    for (const windows of all) {
        const frames = (w: Window[]): number => w.reduce((sum, x) => sum + x.frames, 0);
        if (frames(windows) > frames(longest)) longest = windows;
    }
    const windows = longest.slice(1);
    if (windows.length < 3) return null;
    const size = Math.floor(windows.length / 3);
    const first = third(windows.slice(0, size), longest[0]);
    const last = third(windows.slice(windows.length - size), windows[windows.length - size - 1]);
    const growing: string[] = [];
    if (last.ms > first.ms * GROWTH + SLACK_MS) growing.push("frame time");
    if (last.uploads > first.uploads * GROWTH + SLACK_UPLOADS) growing.push("uploads a frame");
    if (last.builds > first.builds * GROWTH + SLACK_BUILDS) growing.push("BLAS builds a frame");
    return { first, last, growing };
}

// The stall watch's lines (src/platform/frame_profile.h: a loop of the main loop past 1 s, no loading screen up) and the
// device watch's (src/gpu_vulkan/vulkan_frames.cpp: a frame's fence waited for past 2 s): how many, and the longest.
const STALL_LINE = /^stall: the main loop's loop \d+ has run ([\d.]+) s/;
const DEVICE_WAIT_LINE = /^vulkan: frame \d+ has waited ([\d.]+) s for the device/;

export interface Stalls {
    count: number; // each stall once (its first line; a stall's later lines say it lasts)
    longestSeconds: number; // the longest any line saw one run
    deviceWaits: number; // the device watch's lines
}

export function stalls(log: string): Stalls {
    const out: Stalls = { count: 0, longestSeconds: 0, deviceWaits: 0 };
    let inStall = false;
    for (const line of log.split(/\r?\n/)) {
        const stall = STALL_LINE.exec(line);
        if (stall) {
            out.count += inStall ? 0 : 1;
            inStall = true;
            out.longestSeconds = Math.max(out.longestSeconds, Number(stall[1]));
            continue;
        }
        if (line.startsWith("stall: over")) {
            inStall = false;
            continue;
        }
        const wait = DEVICE_WAIT_LINE.exec(line);
        if (wait) {
            out.deviceWaits += 1;
            out.longestSeconds = Math.max(out.longestSeconds, Number(wait[1]));
        }
    }
    return out;
}

// The part of the log the verdict judges: the stretch between loading screens with the most frames, from its first
// renderer window on (a level's first frames - pipelines made, the first light held, the start-up - left out).
export function judgedText(log: string): string {
    const segments: string[][] = [[]];
    for (const line of log.split(/\r?\n/)) {
        if (LOADING_LINE.test(line)) {
            segments.push([]);
            continue;
        }
        segments[segments.length - 1]!.push(line);
    }
    let best: string[] = [];
    let bestFrames = -1;
    for (const lines of segments) {
        let frames = 0;
        for (const line of lines) {
            const frame = FRAME_LINE.exec(line);
            if (frame) frames += Number(frame[3]);
        }
        if (frames > bestFrames) {
            bestFrames = frames;
            best = lines;
        }
    }
    const first = best.findIndex((line) => FRAME_LINE.test(line));
    return first < 0 ? "" : best.slice(first + 1).join("\n");
}

function thirdText(name: string, t: Third): string {
    return (
        `${name}: ${t.frames} frames, ${t.ms.toFixed(1)} ms a frame, ${t.uploads.toFixed(1)} uploads a frame, ` +
        `${t.builds.toFixed(2)} BLAS builds a frame (${t.askedWritten.toFixed(2)} asked for by vertices written)`
    );
}

async function main(): Promise<number> {
    const { positionals } = parseCli(USAGE, { options: {}, allowPositionals: true });
    const [file] = positionals;
    if (!file || positionals.length !== 1) exitWith(USAGE);
    const log = fs.readFileSync(file, "utf8");
    for (const w of stretches(log).flat()) {
        print(
            `frame ${w.frame}: ${w.ms.toFixed(1)} ms a frame over ${w.frames}, ` +
                `${(w.uploads / Math.max(w.frames, 1)).toFixed(1)} uploads a frame (${w.megabytes.toFixed(1)} MB), ` +
                `${w.builds} builds (${w.askedWritten} asked for by writes)`,
        );
    }
    const result = verdict(log);
    if (!result) exitWith("fewer than four renderer windows in one level - nothing to judge");
    print(thirdText("first third", result.first));
    print(thirdText("last third ", result.last));
    print(result.growing.length === 0 ? "flat" : `GROWING: ${result.growing.join(", ")}`);
    const stalled = stalls(judgedText(log)); // (the judged stretch: start-up and loading stalls left out)
    print(
        stalled.count + stalled.deviceWaits === 0
            ? "no stalls"
            : `STALLED: ${stalled.count} stall(s) of the main loop, ${stalled.deviceWaits} wait(s) for the device past ` +
                  `2 s; the longest seen ${stalled.longestSeconds.toFixed(1)} s (the log's "stall:" lines: phase and stack)`,
    );
    return result.growing.length === 0 && stalled.count + stalled.deviceWaits === 0 ? 0 : 1;
}

if (isMain(import.meta.url)) await runMain(() => main());
