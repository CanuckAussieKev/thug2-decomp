// NOT ORIGINAL - a demo rendered under several sets of settings, side by side (src/devtools/demo.h, docs/demos.md).
//
//   node tools/game/demo_compare.ts --demo <name> --game <folder> --set A:Graphics.ray_tracing=0
//                                   --set B:Graphics.ray_tracing=1 [--set B:Graphics.renderer=d3d9 ...]
//                                   [--every N] [--layout side|split|wipe|diff] [--out <folder>] [--ffmpeg <exe>]
//                                   [--timeout <seconds>] [--no-run]
//
// For each set (A, B, ... - the order of their first --set), in turn: the settings written into the game folder's
// thug2-decomp.ini ([section] key=value; the ini put back as it was after each run), the game started with
// `-demo <name> -demo-capture <out>\<set> -demo-every N -demo-quit`, and waited for: it plays the demo from
// thug2-decomp-demos\<name>.demo, saves one frame in N from the level's first (frame_0001.png ...) with the run's check
// track (track.txt) and summary (run.json), and quits. Then:
//   - the runs' check tracks compared with each other (the skater and the camera, frame by frame) - the settings must
//     not change the run: any difference is reported with its first frame - and each run's drift from the recording;
//   - the pictures put together (<out>\compare\frame_0001.png ...): `side` (default) every set side by side, `split`
//     the first set's left half and the second's right half, `wipe` the line between them moving across over the run,
//     `diff` the two sets' difference, brightened; and how much each pair differs per frame;
//   - with FFmpeg (--ffmpeg, or ffmpeg on the PATH; the game's own FFmpeg DLLs decode only) an .mp4 of them, else the
//     command to make one;
//   - <out>\report.md and report.json.
// --every: one frame in N (default 2: 30 pictures a second; 1 = every frame - about 1.5 MB a picture at 1280x720).
// --no-run: only the comparison, from captures made before. Default --out: <game>\thug2-decomp-demos\<name>_compare.
// Refuses when a game is already running from that folder (the user may be playing). Nothing goes online: a demo is
// played in free skate.

import { execFileSync, spawn } from "node:child_process";
import * as fs from "node:fs";
import * as path from "node:path";
import { exitWith, isMain, parseCli, print, runMain } from "thug2-level-converter-core/lib/cli.ts";
import { decodePng, encodePng, type Image } from "thug2-level-converter-core/lib/frame_diff.ts";
import { clearAutomationFiles, launchExeOrError, resolveGameFolder, runningGames } from "./edit_level.ts";
import { ensureWorkspaceMuted, setIniValue } from "./workspace_ini.ts";

const USAGE = `usage: demo_compare.ts [-h] --demo DEMO --game GAME --set SET:SECTION.KEY=VALUE [--set ...] [--every N]
                       [--layout side|split|wipe|diff] [--out OUT] [--ffmpeg FFMPEG] [--timeout SECONDS] [--no-run]

A demo rendered under several sets of settings, side by side (see the header of tools/game/demo_compare.ts).`;

export interface Setting {
    set: string;
    section: string;
    key: string;
    value: string;
}

// "A:Graphics.ray_tracing=1" -> the set A's [Graphics] ray_tracing=1. Null if it isn't that form.
export function parseSet(text: string): Setting | null {
    const m = /^([^:=\s]+):([^.=\s]+)\.([^=\s]+)=(.*)$/.exec(text.trim());
    if (!m) return null;
    return { set: m[1]!, section: m[2]!, key: m[3]!, value: m[4]! };
}

// The sets in the order they were first named, each with its settings.
export function groupSets(settings: readonly Setting[]): Map<string, Setting[]> {
    const sets = new Map<string, Setting[]>();
    for (const s of settings) {
        const list = sets.get(s.set) ?? [];
        list.push(s);
        sets.set(s.set, list);
    }
    return sets;
}

// A check point of a track (formats/demo_format.h check_line).
export interface Point {
    frame: number;
    flags: number;
    position: [number, number, number];
    state: number;
    camera: [number, number, number];
    rest: number[]; // velocity, at, up, camera direction
}

export function parseTrack(text: string): Map<number, Point> {
    const points = new Map<number, Point>();
    for (const line of text.split(/\r?\n/)) {
        if (!line.trim() || line.startsWith("#")) continue;
        const v = line.trim().split(/\s+/).map(Number);
        if (v.length < 21 || v.some((n) => Number.isNaN(n))) continue;
        points.set(v[0]!, {
            frame: v[0]!,
            flags: v[1]!,
            position: [v[2]!, v[3]!, v[4]!],
            state: v[14]!,
            camera: [v[15]!, v[16]!, v[17]!],
            rest: [...v.slice(5, 14), ...v.slice(18, 21)],
        });
    }
    return points;
}

function distance(a: readonly number[], b: readonly number[]): number {
    return Math.hypot(a[0]! - b[0]!, a[1]! - b[1]!, a[2]! - b[2]!);
}

export interface TrackDrift {
    compared: number;
    missing: number; // frames in one track only
    firstFrame: number | null;
    firstSkater: number;
    firstCamera: number;
    maxSkater: number;
    maxCamera: number;
}

// Two runs' tracks frame by frame: the first frame where the skater or the camera is more than `tolerance` apart (or
// the state, flags or another row differs), and the largest distances.
export function compareTracks(a: Map<number, Point>, b: Map<number, Point>, tolerance = 0.01): TrackDrift {
    const drift: TrackDrift = {
        compared: 0,
        missing: 0,
        firstFrame: null,
        firstSkater: 0,
        firstCamera: 0,
        maxSkater: 0,
        maxCamera: 0,
    };
    const frames = [...new Set([...a.keys(), ...b.keys()])].sort((x, y) => x - y);
    for (const frame of frames) {
        const p = a.get(frame);
        const q = b.get(frame);
        if (!p || !q) {
            drift.missing++;
            continue;
        }
        drift.compared++;
        const skater = distance(p.position, q.position);
        const camera = distance(p.camera, q.camera);
        drift.maxSkater = Math.max(drift.maxSkater, skater);
        drift.maxCamera = Math.max(drift.maxCamera, camera);
        const other =
            p.flags !== q.flags || p.state !== q.state || p.rest.some((v, i) => Math.abs(v - q.rest[i]!) > tolerance);
        if (drift.firstFrame === null && (skater > tolerance || camera > tolerance || other)) {
            drift.firstFrame = frame;
            drift.firstSkater = skater;
            drift.firstCamera = camera;
        }
    }
    return drift;
}

export type Layout = "side" | "split" | "wipe" | "diff";

const DIVIDER = 4; // pixels of white between pictures
const DIFF_GAIN = 4;

// The pictures of one frame put together. `progress` (0..1): how far through the run (the wipe's line).
export function compose(images: readonly Image[], layout: Layout, progress: number): Image {
    const first = images[0]!;
    const { width, height } = first;
    if (layout === "side") {
        const out: Image = {
            width: width * images.length + DIVIDER * (images.length - 1),
            height,
            rgb: new Uint8Array((width * images.length + DIVIDER * (images.length - 1)) * height * 3).fill(255),
        };
        images.forEach((image, i) => {
            const left = i * (width + DIVIDER);
            for (let y = 0; y < height; y++) {
                const from = y * width * 3;
                out.rgb.set(image.rgb.subarray(from, from + width * 3), (y * out.width + left) * 3);
            }
        });
        return out;
    }
    const second = images[1] ?? first;
    const out: Image = { width, height, rgb: new Uint8Array(width * height * 3) };
    if (layout === "diff") {
        for (let i = 0; i < out.rgb.length; i++)
            out.rgb[i] = Math.min(255, Math.abs(first.rgb[i]! - second.rgb[i]!) * DIFF_GAIN);
        return out;
    }
    const split = layout === "split" ? Math.floor(width / 2) : Math.round(Math.min(1, Math.max(0, progress)) * width);
    for (let y = 0; y < height; y++) {
        const row = y * width * 3;
        out.rgb.set(first.rgb.subarray(row, row + split * 3), row);
        out.rgb.set(second.rgb.subarray(row + split * 3, row + width * 3), row + split * 3);
        for (let x = Math.max(0, split - DIVIDER / 2); x < Math.min(width, split + DIVIDER / 2); x++)
            out.rgb.fill(255, row + x * 3, row + x * 3 + 3);
    }
    return out;
}

// How much two pictures differ: the mean |difference| per channel (0-255) and the share of pixels whose summed
// |dR|+|dG|+|dB| is over `threshold`.
export function pictureDifference(a: Image, b: Image, threshold = 24): { mean: number; changed: number } {
    if (a.width !== b.width || a.height !== b.height) return { mean: 255, changed: 1 };
    let sum = 0;
    let changed = 0;
    for (let i = 0; i < a.rgb.length; i += 3) {
        const d =
            Math.abs(a.rgb[i]! - b.rgb[i]!) +
            Math.abs(a.rgb[i + 1]! - b.rgb[i + 1]!) +
            Math.abs(a.rgb[i + 2]! - b.rgb[i + 2]!);
        sum += d;
        if (d > threshold) changed++;
    }
    const pixels = a.rgb.length / 3;
    return { mean: sum / (pixels * 3), changed: changed / pixels };
}

function frameName(n: number): string {
    return `frame_${String(n).padStart(4, "0")}.png`;
}

function findFfmpeg(given: string | undefined): string | null {
    if (given) return fs.existsSync(given) ? given : null;
    try {
        const out = execFileSync("where.exe", ["ffmpeg"], { encoding: "utf8", stdio: ["ignore", "pipe", "ignore"] });
        return out.split(/\r?\n/).find((l) => l.trim()) ?? null;
    } catch {
        return null;
    }
}

// One run: the set's settings in the ini (put back afterwards), the game started, waited for.
async function runSet(
    exe: string,
    game: string,
    demo: string,
    set: string,
    settings: readonly Setting[],
    folder: string,
    every: number,
    timeoutSeconds: number,
): Promise<void> {
    const ini = path.join(game, "thug2-decomp.ini");
    const before = fs.existsSync(ini) ? fs.readFileSync(ini, "utf8") : "";
    let text = before;
    for (const s of settings) text = setIniValue(text, s.section, s.key, s.value);
    fs.rmSync(folder, { recursive: true, force: true });
    fs.mkdirSync(folder, { recursive: true });
    try {
        fs.writeFileSync(ini, text);
        clearAutomationFiles(game);
        const args = ["-demo", demo, "-demo-capture", folder, "-demo-every", String(every), "-demo-quit"];
        print(
            `set ${set}: ${settings.map((s) => `[${s.section}] ${s.key}=${s.value}`).join(", ") || "(as the ini is)"}`,
        );
        const started = Date.now();
        const child = spawn(exe, args, { cwd: game, stdio: "ignore" });
        const code = await new Promise<number | null>((resolve) => {
            const timer = setTimeout(() => {
                print(`set ${set}: still running after ${timeoutSeconds} s - closed`);
                child.kill();
            }, timeoutSeconds * 1000);
            child.on("exit", (exitCode) => {
                clearTimeout(timer);
                resolve(exitCode);
            });
        });
        print(`set ${set}: the game ended (exit code ${code}) after ${((Date.now() - started) / 1000).toFixed(1)} s`);
    } finally {
        fs.writeFileSync(ini, before);
    }
}

interface RunSummary {
    first_divergence_frame: number;
    max_skater_distance: number;
    max_camera_distance: number;
    compared: number;
    up_frame: number;
    frames: number;
    ended: boolean;
}

function readSummary(folder: string): RunSummary | null {
    const file = path.join(folder, "run.json");
    return fs.existsSync(file) ? (JSON.parse(fs.readFileSync(file, "utf8")) as RunSummary) : null;
}

function picturesIn(folder: string): number {
    let n = 0;
    while (fs.existsSync(path.join(folder, frameName(n + 1)))) n++;
    return n;
}

async function main(): Promise<number> {
    const { values } = parseCli(USAGE, {
        options: {
            demo: { type: "string" },
            game: { type: "string" },
            set: { type: "string", multiple: true },
            every: { type: "string", default: "2" },
            layout: { type: "string", default: "side" },
            out: { type: "string" },
            ffmpeg: { type: "string" },
            timeout: { type: "string", default: "900" },
            "no-run": { type: "boolean", default: false },
        },
    });
    if (!values.demo || !values.game) exitWith(`${USAGE}\nerror: --demo and --game are needed`);
    const settings = (values.set ?? []).map((s) => parseSet(s) ?? exitWith(`--set ${s}: not SET:SECTION.KEY=VALUE`));
    const sets = groupSets(settings);
    if (sets.size < 2) exitWith("at least two sets (--set A:... --set B:...)");
    const layout = values.layout as Layout;
    if (!["side", "split", "wipe", "diff"].includes(layout))
        exitWith(`--layout ${values.layout}: side, split, wipe or diff`);
    const every = Math.max(1, Number.parseInt(values.every, 10) || 1);
    const game = resolveGameFolder(values.game);
    const demo = values.demo.replace(/\.demo$/i, "");
    const out = path.resolve(values.out ?? path.join(game, "thug2-decomp-demos", `${demo}_compare`));
    fs.mkdirSync(out, { recursive: true });
    const names = [...sets.keys()];

    if (!values["no-run"]) {
        const found = launchExeOrError(values.game, game);
        if ("error" in found) exitWith(found.error);
        if (!fs.existsSync(path.join(game, "thug2-decomp-demos", `${demo}.demo`)))
            exitWith(`no ${path.join(game, "thug2-decomp-demos", `${demo}.demo`)}`);
        const here = path.resolve(game).toLowerCase();
        const running = runningGames().filter(
            (p) => p === null || path.dirname(path.resolve(p)).toLowerCase() === here,
        );
        if (running.length) exitWith(`THUG2 IS ALREADY RUNNING FROM ${game} - not started (the user may be playing)`);
        if (ensureWorkspaceMuted(game)) print("a workspace: [features] mute_audio=1 (its games always run muted)");
        for (const name of names)
            await runSet(
                found.exe,
                game,
                demo,
                name,
                sets.get(name)!,
                path.join(out, name),
                every,
                Number(values.timeout),
            );
    }

    // the tracks
    const tracks = new Map<string, Map<number, Point>>();
    const summaries = new Map<string, RunSummary | null>();
    for (const name of names) {
        const file = path.join(out, name, "track.txt");
        tracks.set(name, fs.existsSync(file) ? parseTrack(fs.readFileSync(file, "utf8")) : new Map());
        summaries.set(name, readSummary(path.join(out, name)));
    }
    const lines: string[] = [`# Demo ${demo}: ${names.join(" vs ")}`, ""];
    lines.push("| set | settings | frames played | drift from the recording | pictures |", "|---|---|---|---|---|");
    for (const name of names) {
        const s = summaries.get(name);
        const drift = !s
            ? "no run.json"
            : s.first_divergence_frame < 0
              ? `none (${s.compared} check points)`
              : `from frame ${s.first_divergence_frame}, at most ${s.max_skater_distance.toFixed(3)} (skater) / ${s.max_camera_distance.toFixed(3)} (camera)`;
        const conf = sets
            .get(name)!
            .map((x) => `${x.section}.${x.key}=${x.value}`)
            .join(", ");
        lines.push(
            `| ${name} | ${conf} | ${s ? `${s.frames}${s.ended ? "" : " (stopped)"}` : "-"} | ${drift} | ${picturesIn(path.join(out, name))} |`,
        );
    }
    lines.push("", "## The runs against each other (check tracks)", "");
    const pairs: { a: string; b: string; drift: TrackDrift }[] = [];
    for (let i = 1; i < names.length; i++) {
        const drift = compareTracks(tracks.get(names[0]!)!, tracks.get(names[i]!)!);
        pairs.push({ a: names[0]!, b: names[i]!, drift });
        lines.push(
            `- ${names[0]} vs ${names[i]}: ${drift.compared} frames compared${drift.missing ? `, ${drift.missing} in one only` : ""}; ` +
                (drift.firstFrame === null
                    ? "identical"
                    : `first differs at frame ${drift.firstFrame} (skater ${drift.firstSkater.toFixed(3)}, camera ${drift.firstCamera.toFixed(3)} apart); largest: skater ${drift.maxSkater.toFixed(3)}, camera ${drift.maxCamera.toFixed(3)}`),
        );
    }

    // the pictures
    const count = Math.min(...names.map((n) => picturesIn(path.join(out, n))));
    const compareFolder = path.join(out, "compare");
    fs.rmSync(compareFolder, { recursive: true, force: true });
    fs.mkdirSync(compareFolder, { recursive: true });
    const differences: { mean: number; changed: number }[] = [];
    for (let k = 1; k <= count; k++) {
        const images = names.map((n) => decodePng(fs.readFileSync(path.join(out, n, frameName(k)))));
        differences.push(pictureDifference(images[0]!, images[1]!));
        fs.writeFileSync(
            path.join(compareFolder, frameName(k)),
            encodePng(compose(images, layout, count > 1 ? (k - 1) / (count - 1) : 0)),
        );
    }
    lines.push("", `## Pictures (${layout}, one frame in ${every})`, "");
    if (count === 0) lines.push("- none captured");
    else {
        const meanOfMeans = differences.reduce((t, d) => t + d.mean, 0) / count;
        const most = differences.reduce((m, d, i) => (d.mean > m.mean ? { mean: d.mean, index: i + 1 } : m), {
            mean: -1,
            index: 0,
        });
        const changed = differences.reduce((t, d) => t + d.changed, 0) / count;
        lines.push(
            `- ${count} pictures in ${compareFolder} (left to right: ${layout === "side" ? names.join(" | ") : `${names[0]} | ${names[1]}`})`,
            `- ${names[0]} vs ${names[1]}: mean difference ${meanOfMeans.toFixed(2)} / 255 a channel, ${(changed * 100).toFixed(1)}% of the pixels changed on average; most at picture ${most.index} (${most.mean.toFixed(2)})`,
        );
        const fps = 60 / every;
        const video = path.join(out, `${demo}_${names.join("_vs_")}_${layout}.mp4`);
        const ffArgs = [
            "-y",
            "-framerate",
            String(fps),
            "-i",
            path.join(compareFolder, "frame_%04d.png"),
            "-vf",
            "pad=ceil(iw/2)*2:ceil(ih/2)*2",
            "-c:v",
            "libx264",
            "-pix_fmt",
            "yuv420p",
            "-crf",
            "18",
            video,
        ];
        const ffmpeg = findFfmpeg(values.ffmpeg);
        if (ffmpeg) {
            execFileSync(ffmpeg, ffArgs, { stdio: "ignore" });
            lines.push(`- video: ${video}`);
        } else {
            lines.push(
                "- no video: FFmpeg isn't on the PATH (the game's FFmpeg DLLs only decode). To make one:",
                `  \`ffmpeg ${ffArgs.map((a) => (/[\s%*()]/.test(a) ? `"${a}"` : a)).join(" ")}\``,
            );
        }
    }
    const report = `${lines.join("\n")}\n`;
    fs.writeFileSync(path.join(out, "report.md"), report);
    fs.writeFileSync(
        path.join(out, "report.json"),
        `${JSON.stringify(
            {
                demo,
                sets: Object.fromEntries(names.map((n) => [n, sets.get(n)])),
                runs: Object.fromEntries(names.map((n) => [n, summaries.get(n)])),
                pairs,
                pictures: count,
                every,
                layout,
                differences,
            },
            null,
            2,
        )}\n`,
    );
    print(report);
    print(`report: ${path.join(out, "report.md")}`);
    const diverged = pairs.some((p) => p.drift.firstFrame !== null);
    return diverged ? 1 : 0;
}

if (isMain(import.meta.url)) await runMain(() => main());
