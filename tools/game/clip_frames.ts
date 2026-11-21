// NOT ORIGINAL - frames out of a clip the clip key recorded (src/devtools/clip.h), as PNGs.
//
//   node tools/game/clip_frames.ts <clip.mp4> <out folder> [--every N | --frames a,b,c | --at seconds,...]
//                                  [--crop x,y,w,h] [--scale PERCENT] [--game <folder>]
//
// The clip is decoded by the game's own exe (thug2-decomp.exe -clip-frames, FFmpeg's decoders: no ffmpeg.exe) - found
// next to the clip's Videos folder, or in --game. Writes <out>\vf_<n>.png (n: the video frame, from 0) and
// <out>\index.json / index.txt: each picture paired with its line of the clip's sidecar (<clip>.txt: the displayed
// frame, its time, the game's frame, the frame time and the camera's position and direction), so a picture can be matched
// with a pose. --every N: one video frame in N, from 0; --frames: those video frames; --at: the frames at those
// times (seconds); none of the three: --every 60 (about one a second). --crop cuts a rectangle (pixels of the video)
// from each frame first, --scale (a percentage, default 100) then scales it - to look closely at a part of the picture.

import { spawnSync } from "node:child_process";
import * as fs from "node:fs";
import * as path from "node:path";
import { exitWith, isMain, parseCli, print, runMain } from "thug2-level-converter-core/lib/cli.ts";
import { findLaunchExe, resolveGameFolder } from "./edit_level.ts";

const USAGE = `usage: clip_frames.ts [-h] CLIP OUT [--every N | --frames a,b,c | --at seconds,...] [--crop x,y,w,h] [--scale PERCENT]
                      [--game FOLDER]

Frames of a recorded clip as PNGs with an index pairing each with the sidecar's frame line (see the header of tools/game/clip_frames.ts).`;

const DEFAULT_EVERY = 60;
const MAX_FRAMES = 2000;

export interface SidecarLine {
    frame: number; // the displayed frame
    vf: number; // the video frame it first shows in
    time: number; // seconds into the clip
    game: number; // the game's frame
    dtMs: number;
    position: [number, number, number] | null;
    direction: [number, number, number] | null;
}

export interface ClipFacts {
    codec: string;
    width: number;
    height: number;
    fps: number;
    frames: number;
}

export interface Selection {
    every?: number;
    frames?: number[];
    at?: number[];
}

export interface Crop {
    x: number;
    y: number;
    w: number;
    h: number;
}

// "a,b,c" -> numbers; null if any is not a number (floats allowed with `integers` false).
export function parseNumberList(text: string, integers: boolean): number[] | null {
    const parts = text.split(",").map((p) => p.trim());
    if (parts.length === 0 || parts.some((p) => p === "")) return null;
    const numbers = parts.map((p) => Number(p));
    if (numbers.some((n) => !Number.isFinite(n) || n < 0 || (integers && !Number.isInteger(n)))) return null;
    return numbers;
}

export function parseCrop(text: string): Crop | null {
    const n = parseNumberList(text, true);
    if (n === null) return null;
    if (n.length !== 4 || n[2]! <= 0 || n[3]! <= 0) return null;
    return { x: n[0]!, y: n[1]!, w: n[2]!, h: n[3]! };
}

// The clip's sidecar's frame lines: those under "[frames]", 11 words each (the camera's 6 numbers "-" when unknown).
export function parseSidecarFrames(text: string): SidecarLine[] {
    const lines: SidecarLine[] = [];
    let inFrames = false;
    for (const raw of text.split(/\r?\n/)) {
        if (raw.startsWith("[")) {
            inFrames = raw.trim() === "[frames]";
            continue;
        }
        if (!inFrames) continue;
        const words = raw.trim().split(/\s+/);
        if (words.length !== 11 || !/^\d+$/.test(words[0]!)) continue;
        const n = words.map((w) => Number(w));
        const known = words.slice(5).every((w) => w !== "-");
        lines.push({
            frame: n[0]!,
            vf: n[1]!,
            time: n[2]!,
            game: n[3]!,
            dtMs: n[4]!,
            position: known ? [n[5]!, n[6]!, n[7]!] : null,
            direction: known ? [n[8]!, n[9]!, n[10]!] : null,
        });
    }
    return lines;
}

// The video frames a selection names, sorted and without repeats, within 0..count-1 (at most MAX_FRAMES).
export function selectFrames(selection: Selection, facts: Pick<ClipFacts, "fps" | "frames">): number[] {
    const last = facts.frames - 1;
    let picked: number[];
    if (selection.frames) {
        picked = selection.frames;
    } else if (selection.at) {
        picked = selection.at.map((s) => Math.round(s * facts.fps));
    } else {
        const every = Math.max(1, selection.every ?? DEFAULT_EVERY);
        picked = [];
        for (let f = 0; f <= last; f += every) picked.push(f);
    }
    return [...new Set(picked.map((f) => Math.min(Math.max(f, 0), last)))].sort((a, b) => a - b).slice(0, MAX_FRAMES);
}

// The sidecar line a video frame shows: the last displayed frame whose first video frame is at or before it.
export function lineForFrame(lines: readonly SidecarLine[], vf: number): SidecarLine | null {
    let found: SidecarLine | null = null;
    for (const line of lines) {
        if (line.vf <= vf) found = line;
        else break;
    }
    return found;
}

export interface IndexEntry {
    file: string;
    videoFrame: number;
    videoTime: number;
    line: SidecarLine | null;
}

export function buildIndex(frames: readonly number[], fps: number, lines: readonly SidecarLine[]): IndexEntry[] {
    return frames.map((vf) => ({
        file: `vf_${String(vf).padStart(6, "0")}.png`,
        videoFrame: vf,
        videoTime: vf / fps,
        line: lineForFrame(lines, vf),
    }));
}

function fixed(v: readonly number[] | null): string {
    return v ? v.map((x) => x.toFixed(2)).join(" ") : "-";
}

export function indexText(clip: string, facts: ClipFacts, entries: readonly IndexEntry[]): string {
    const out = [
        `${path.basename(clip)}: ${facts.width}x${facts.height}, ${facts.fps} a second, ${facts.frames} frames, ${facts.codec}`,
        "file            video time   displayed  game frame  dt ms   position                  direction",
    ];
    for (const e of entries) {
        const l = e.line;
        out.push(
            `${e.file.padEnd(15)} ${e.videoTime.toFixed(2).padStart(6)} s   ` +
                (l
                    ? `${String(l.frame).padStart(8)}  ${String(l.game).padStart(10)}  ${l.dtMs.toFixed(1).padStart(5)}   ${fixed(l.position).padEnd(25)} ${fixed(l.direction)}`
                    : "(no sidecar line)"),
        );
    }
    return `${out.join("\n")}\n`;
}

// The exe's stderr: "clip: <codec> <w>x<h> <fps> <frames>" or "clip: error - why".
export function parseFacts(stderr: string): ClipFacts | { error: string } {
    const error = /^clip: error - (.*)$/m.exec(stderr);
    if (error) return { error: error[1]! };
    const m = /^clip: (\S+) (\d+)x(\d+) ([\d.]+) (\d+)$/m.exec(stderr);
    if (!m) return { error: `no answer from the exe: ${stderr.trim() || "(nothing)"}` };
    return { codec: m[1]!, width: Number(m[2]), height: Number(m[3]), fps: Number(m[4]), frames: Number(m[5]) };
}

function runExe(exe: string, game: string, args: string[]): { facts: ClipFacts | { error: string }; stderr: string } {
    const run = spawnSync(exe, args, { cwd: game, encoding: "utf8", maxBuffer: 1 << 26 });
    const stderr = run.stderr ?? "";
    if (run.status !== 0)
        return {
            facts: { error: `the exe exited with ${run.status ?? run.error?.message}: ${stderr.trim()}` },
            stderr,
        };
    return { facts: parseFacts(stderr), stderr };
}

function findGame(clip: string, given: string | undefined): { exe: string; game: string } | null {
    const candidates = given ? [resolveGameFolder(given)] : [path.dirname(path.dirname(clip)), path.dirname(clip)];
    for (const game of candidates) {
        const exe = findLaunchExe(game);
        if (exe) return { exe, game };
    }
    return null;
}

function main(): undefined {
    const { values, positionals } = parseCli(USAGE, {
        options: {
            every: { type: "string" },
            frames: { type: "string" },
            at: { type: "string" },
            crop: { type: "string" },
            scale: { type: "string" },
            game: { type: "string" },
        },
        allowPositionals: true,
    });
    if (positionals.length !== 2) exitWith(`${USAGE}\nerror: a clip and an output folder are needed`);
    const clip = path.resolve(positionals[0]!);
    const out = path.resolve(positionals[1]!);
    if (!fs.existsSync(clip)) exitWith(`${clip}: no such clip`);
    const chosen = [values.every, values.frames, values.at].filter((v) => v !== undefined).length;
    if (chosen > 1) exitWith("--every, --frames and --at are alternatives");
    const selection: Selection = {};
    if (values.every !== undefined) {
        const every = Number(values.every);
        if (!Number.isInteger(every) || every < 1) exitWith("--every is a whole number, 1 or more");
        selection.every = every;
    }
    if (values.frames !== undefined) {
        selection.frames = parseNumberList(values.frames, true) ?? exitWith("--frames a,b,c: whole numbers");
    }
    if (values.at !== undefined) selection.at = parseNumberList(values.at, false) ?? exitWith("--at s,s: seconds");
    const crop =
        values.crop === undefined ? null : (parseCrop(values.crop) ?? exitWith("--crop x,y,w,h: whole numbers"));
    const scale = values.scale === undefined ? 100 : Number(values.scale);
    if (!Number.isInteger(scale) || scale < 1 || scale > 1600) exitWith("--scale is a percentage, 1-1600");
    const found =
        findGame(clip, values.game) ?? exitWith("no thug2-decomp.exe next to the clip's folder (--game <folder>)");

    // the clip's facts first (the exe decodes it whole), then the frames asked for
    const info = runExe(found.exe, found.game, ["-clip-frames", clip]);
    if ("error" in info.facts) exitWith(`clip: ${info.facts.error}`);
    const facts = info.facts;
    const frames = selectFrames(selection, facts);
    fs.mkdirSync(out, { recursive: true });
    const args = ["-clip-frames", clip, "-frames-out", out, "-frames", frames.join(",")];
    if (crop) args.push("-frames-crop", `${crop.x},${crop.y},${crop.w},${crop.h}`);
    if (scale !== 100) args.push("-frames-scale", String(scale));
    const run = runExe(found.exe, found.game, args);
    if ("error" in run.facts) exitWith(`clip: ${run.facts.error}`);
    const sidecar = clip.replace(/\.[^.]+$/, ".txt");
    const lines = fs.existsSync(sidecar) ? parseSidecarFrames(fs.readFileSync(sidecar, "utf8")) : [];
    const entries = buildIndex(frames, facts.fps, lines);
    fs.writeFileSync(
        path.join(out, "index.json"),
        `${JSON.stringify({ clip, ...facts, crop, scale, frames: entries }, null, 2)}\n`,
    );
    fs.writeFileSync(path.join(out, "index.txt"), indexText(clip, facts, entries));
    print(
        `${frames.length} frames of ${path.basename(clip)} in ${out} (index.txt, index.json)${lines.length === 0 ? " - no sidecar lines found" : ""}`,
    );
    return undefined;
}

if (isMain(import.meta.url)) await runMain(() => main());
