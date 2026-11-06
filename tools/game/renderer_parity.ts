// NOT ORIGINAL - the renderers side by side: the same spot and frame drawn by Direct3D 9 and by Vulkan
// ([Graphics] renderer=d3d9 | vulkan), each captured in several runs, and the two compared outside what differs between
// a renderer's own runs (its noise: animation, pedestrians, the loading bar's timing...).
//
//   node tools/game/renderer_parity.ts run <plan.json> --game C:\Games\THUG2-<n> --out <folder>
//        [--runs 2] [--renderers d3d9,vulkan] [--groups a,b,...] [--build <folder>] [--exp-build <folder>]
//        [--owner <name>] [--set Section.key=value]...
//   node tools/game/renderer_parity.ts compare <folder> [--threshold 8] [--grow 1] [--pictures]
//
// run: under the workspace's lock (game_lock.ps1; released at the end, which puts the exe and the ini back), for each
// run, renderer and group of the plan: the ini written (the workspace's own + the plan's, the group's and --set's
// values + renderer=), the game started (run_game.ps1: -Level, the group's automation commands, its window pictured at
// the end) and its pictures gathered into <out>/<renderer>/run<k>/:
//   - a level group's spots: `waitfor level <level>`, then per spot its commands (teleport / camera / wait...) and
//     `capture 1` (the frame the game presents - the screenshot key's capture), the group's commands, then `quit`;
//   - commands (a group with or without a level): as given (the front end: `waitfor ready`, `choose ...`, `capture 1 <spot>`
//     with the spot's name as the series' folder);
//   - `window`: the window's PrintWindow picture (full size) at the end - for what the channel can't drive: the level
//     editor (the channel stops while it has the game);
//   - `collect`: every .png / .ppm the game wrote into {dir} (a fresh folder per run, given to the game through the
//     group's `env` values - a measuring build's dumps: `exp` groups run --exp-build's exe);
//   - `frametime`: the group's `wait <frames>` timed from outside (the status file's frame count, polled: the game's
//     frames, not the presented ones), the Vulkan log's own "ms a frame" line, and a measuring build's "exp-frames"
//     lines (the presents of either renderer);
// Then compare (below). Only a workspace (C:\Games\THUG2-<n>) - never the main install. Muted (workspace_ini.ts).
//
// compare: for each picture name both renderers have: per renderer the pixels that differ by more than the threshold
// between any two of its runs (its noise), grown by --grow pixels; outside the union of both masks, run 1 of each
// compared: the mean channel difference, the mean signed (Vulkan - Direct3D 9), the largest, the pixels over the
// threshold, their bounding rectangle and the 80x80-pixel cells holding most of them. report.txt / report.json in the
// folder; --pictures adds sbs/<name>.png: Direct3D 9 | Vulkan | the difference x4, the noise mask in blue.
//
// A plan (JSON): { "ini": { "Section.key": "value" }, "groups": [ { "name", "level"?, "ini"?, "env"?, "exp"?,
//   "seconds"?, "commands"?, "spots"?: [ { "name", "commands" } ], "window"?, "collect"?, "frametime"?: <frames> } ] }

import { spawn } from "node:child_process";
import * as fs from "node:fs";
import * as path from "node:path";
import { fileURLToPath } from "node:url";
import { exitWith, isMain, parseCli, print, runMain } from "thug2-level-converter-core/lib/cli.ts";
import { decodePng, encodePng, type Image } from "thug2-level-converter-core/lib/frame_diff.ts";
import { clearAutomationFiles } from "./edit_level.ts";
import { ensureWorkspaceMuted, isWorkspace, setIniValue } from "./workspace_ini.ts";

const USAGE = `usage: renderer_parity.ts run <plan.json> --game <workspace> --out <folder> [--runs N] [--renderers d3d9,vulkan]
                          [--groups a,b] [--build <folder>] [--exp-build <folder>] [--owner name] [--set S.key=v]...
       renderer_parity.ts compare <folder> [--threshold N] [--grow N] [--pictures]

The same spots and frames captured with renderer=d3d9 and renderer=vulkan, N runs each, and compared outside each
renderer's own run-to-run noise (tools/game/README.md).`;

const HERE = path.dirname(fileURLToPath(import.meta.url));
export const RENDERERS = ["d3d9", "vulkan"] as const;
// (the files the lock backs up and puts back - copying others in would leave them there)
const DEPLOYED = ["thug2-decomp.exe", "thug2-decomp.pdb", "d3dx9_43.dll"];

export interface PlanSpot {
    name: string;
    commands: string[];
}

export interface PlanGroup {
    name: string;
    level?: string;
    ini?: Record<string, string>;
    env?: Record<string, string>;
    exp?: boolean;
    seconds?: number;
    commands?: string[];
    spots?: PlanSpot[];
    window?: boolean;
    collect?: boolean;
    frametime?: number;
}

export interface Plan {
    ini?: Record<string, string>;
    groups: PlanGroup[];
}

// --- the plan ------------------------------------------------------------------------------------------------------

const NAME = /^[A-Za-z0-9_.-]+$/;

export function readPlan(text: string): Plan {
    const data = JSON.parse(text) as Plan;
    if (!Array.isArray(data.groups)) throw new Error("a plan needs a groups list");
    const seen = new Set<string>();
    for (const g of data.groups) {
        if (!NAME.test(g.name ?? "")) throw new Error(`a group's name must be letters, digits, _ . - (${g.name})`);
        if (seen.has(g.name)) throw new Error(`two groups named ${g.name}`);
        seen.add(g.name);
        for (const s of g.spots ?? [])
            if (!NAME.test(s.name ?? "")) throw new Error(`${g.name}: a spot's name (${s.name})`);
    }
    return data;
}

// A series folder's name: one per renderer, run and spot (the game writes Screenshots\<it>\frame_0001.png).
export function seriesName(renderer: string, run: number, spot: string): string {
    return `parity-${renderer}-${run}-${spot}`;
}

// The automation commands of a group: `waitfor level`, the spots (each its commands and `capture 1`), the group's own
// commands (`capture <n> <spot>` given the series folder), a frame-time group's `wait <frames>`; then quit.
export function groupCommands(group: PlanGroup, renderer: string, run: number): string[] {
    const out: string[] = [];
    if (group.level) out.push(`waitfor level ${group.level} 300`);
    for (const s of group.spots ?? []) {
        out.push(...s.commands, `capture 1 ${seriesName(renderer, run, s.name)}`, "wait 2");
    }
    for (const c of group.commands ?? []) {
        const capture = /^capture\s+(\d+)\s+(\S+)\s*$/.exec(c.trim());
        out.push(capture ? `capture ${capture[1]} ${seriesName(renderer, run, capture[2]!)}` : c);
    }
    if (group.frametime) out.push(`wait ${group.frametime}`);
    if (!group.window) out.push("quit");
    return out;
}

// The spots' names a group's commands capture (its spots, and the `capture <n> <name>` lines of its commands).
export function groupSpots(group: PlanGroup): string[] {
    const names = (group.spots ?? []).map((s) => s.name);
    for (const c of group.commands ?? []) {
        const capture = /^capture\s+\d+\s+(\S+)\s*$/.exec(c.trim());
        if (capture) names.push(capture[1]!);
    }
    return names;
}

// The ini's text for one game: the workspace's own with every Section.key=value set, then the renderer.
export function iniFor(base: string, values: readonly Record<string, string>[], renderer: string): string {
    let text = base;
    for (const set of values) {
        for (const [where, value] of Object.entries(set)) {
            const dot = where.indexOf(".");
            if (dot <= 0) throw new Error(`an ini value is Section.key (${where})`);
            text = setIniValue(text, where.slice(0, dot), where.slice(dot + 1), value);
        }
    }
    return setIniValue(text, "Graphics", "renderer", renderer);
}

// --- pictures --------------------------------------------------------------------------------------------------------

// A binary PPM (P6, maxval 255): the measuring builds' dumps.
export function decodePpm(data: Buffer): Image {
    const header = /^P6\s+(\d+)\s+(\d+)\s+(\d+)\s/.exec(data.subarray(0, 64).toString("latin1"));
    if (!header) throw new Error("not a P6 PPM");
    if (header[3] !== "255") throw new Error("a PPM whose maxval isn't 255");
    const width = Number(header[1]);
    const height = Number(header[2]);
    const at = header[0].length;
    const rgb = new Uint8Array(data.subarray(at, at + width * height * 3));
    if (rgb.length !== width * height * 3) throw new Error("a short PPM");
    return { width, height, rgb };
}

export function readPicture(file: string): Image {
    const data = fs.readFileSync(file);
    return file.toLowerCase().endsWith(".ppm") ? decodePpm(data) : decodePng(data);
}

// Per pixel: true where any two of the pictures differ by more than `threshold` in a channel.
export function noiseMask(images: readonly Image[], threshold: number): Uint8Array {
    const first = images[0]!;
    const mask = new Uint8Array(first.width * first.height);
    for (let a = 0; a < images.length; a++) {
        for (let b = a + 1; b < images.length; b++) {
            const pa = images[a]!.rgb;
            const pb = images[b]!.rgb;
            for (let p = 0; p < mask.length; p++) {
                const i = p * 3;
                if (
                    Math.abs(pa[i]! - pb[i]!) > threshold ||
                    Math.abs(pa[i + 1]! - pb[i + 1]!) > threshold ||
                    Math.abs(pa[i + 2]! - pb[i + 2]!) > threshold
                )
                    mask[p] = 1;
            }
        }
    }
    return mask;
}

// The mask grown by `by` pixels (a square neighbourhood).
export function growMask(mask: Uint8Array, width: number, height: number, by: number): Uint8Array {
    if (by <= 0) return mask;
    const rows = new Uint8Array(mask.length);
    for (let y = 0; y < height; y++)
        for (let x = 0; x < width; x++) {
            if (!mask[y * width + x]) continue;
            for (let dx = Math.max(0, x - by); dx <= Math.min(width - 1, x + by); dx++) rows[y * width + dx] = 1;
        }
    const out = new Uint8Array(mask.length);
    for (let y = 0; y < height; y++)
        for (let x = 0; x < width; x++) {
            if (!rows[y * width + x]) continue;
            for (let dy = Math.max(0, y - by); dy <= Math.min(height - 1, y + by); dy++) out[dy * width + x] = 1;
        }
    return out;
}

export interface Difference {
    pixels: number; // compared (outside the mask)
    mean: number; // channel difference, over the compared pixels' channels
    signed: number; // b - a, the same
    largest: number;
    over: number; // pixels differing by more than the threshold
    region: [number, number, number, number] | null; // x, y, w, h around them
    cells: { x: number; y: number; count: number }[]; // the 80x80 cells holding most of them (their corners)
}

export const CELL = 80;

// a against b outside the mask (null: every pixel).
export function difference(a: Image, b: Image, mask: Uint8Array | null, threshold: number): Difference {
    if (a.width !== b.width || a.height !== b.height)
        throw new Error(`sizes differ: ${a.width}x${a.height} / ${b.width}x${b.height}`);
    const { width, height } = a;
    const cellsX = Math.ceil(width / CELL);
    const cellCounts = new Map<number, number>();
    let pixels = 0;
    let sum = 0;
    let signed = 0;
    let largest = 0;
    let over = 0;
    let x0 = width;
    let y0 = height;
    let x1 = -1;
    let y1 = -1;
    for (let y = 0; y < height; y++)
        for (let x = 0; x < width; x++) {
            const p = y * width + x;
            if (mask?.[p]) continue;
            pixels++;
            let d = 0;
            for (let c = 0; c < 3; c++) {
                const delta = b.rgb[p * 3 + c]! - a.rgb[p * 3 + c]!;
                signed += delta;
                sum += Math.abs(delta);
                d = Math.max(d, Math.abs(delta));
            }
            largest = Math.max(largest, d);
            if (d > threshold) {
                over++;
                x0 = Math.min(x0, x);
                y0 = Math.min(y0, y);
                x1 = Math.max(x1, x);
                y1 = Math.max(y1, y);
                const cell = Math.floor(y / CELL) * cellsX + Math.floor(x / CELL);
                cellCounts.set(cell, (cellCounts.get(cell) ?? 0) + 1);
            }
        }
    const cells = [...cellCounts.entries()]
        .sort((p, q) => q[1] - p[1] || p[0] - q[0])
        .slice(0, 3)
        .map(([cell, count]) => ({ x: (cell % cellsX) * CELL, y: Math.floor(cell / cellsX) * CELL, count }));
    const channels = Math.max(1, pixels * 3);
    return {
        pixels,
        mean: sum / channels,
        signed: signed / channels,
        largest,
        over,
        region: over ? [x0, y0, x1 - x0 + 1, y1 - y0 + 1] : null,
        cells,
    };
}

export interface SpotResult {
    name: string;
    runs: Record<string, number>; // pictures per renderer
    noise: Record<string, { masked: number; mean: number }>; // a renderer's runs 1 and 2: share masked, mean
    masked: number; // share of the pixels in the union mask (grown)
    all: Difference; // run 1 against run 1, every pixel
    outside: Difference; // the same outside the noise
}

// Every renderer's noise mask together, grown by `grow` pixels.
export function unionMask(pictures: Record<string, Image[]>, threshold: number, grow: number): Uint8Array {
    const first = Object.values(pictures)[0]![0]!;
    const union = new Uint8Array(first.width * first.height);
    for (const images of Object.values(pictures)) {
        const mask = noiseMask(images, threshold);
        for (let p = 0; p < mask.length; p++) if (mask[p]) union[p] = 1;
    }
    return growMask(union, first.width, first.height, grow);
}

export function compareSpot(
    name: string,
    pictures: Record<string, Image[]>,
    threshold: number,
    grow: number,
): SpotResult {
    const a = pictures.d3d9!;
    const b = pictures.vulkan!;
    const noise: SpotResult["noise"] = {};
    const runs: Record<string, number> = {};
    for (const [renderer, images] of Object.entries(pictures)) {
        runs[renderer] = images.length;
        const mask = noiseMask(images, threshold);
        let masked = 0;
        for (const m of mask) masked += m;
        noise[renderer] = {
            masked: masked / mask.length,
            mean: images.length > 1 ? difference(images[0]!, images[1]!, null, threshold).mean : 0,
        };
    }
    const grown = unionMask(pictures, threshold, grow);
    let masked = 0;
    for (const m of grown) masked += m;
    return {
        name,
        runs,
        noise,
        masked: masked / grown.length,
        all: difference(a[0]!, b[0]!, null, threshold),
        outside: difference(a[0]!, b[0]!, grown, threshold),
    };
}

// Direct3D 9 | Vulkan | the difference x4 (the noise mask's pixels tinted blue).
export function sideBySide(a: Image, b: Image, mask: Uint8Array): Image {
    const { width, height } = a;
    const outWidth = width * 3;
    const rgb = new Uint8Array(outWidth * height * 3);
    for (let y = 0; y < height; y++)
        for (let x = 0; x < width; x++) {
            const p = y * width + x;
            const o = y * outWidth + x;
            for (let c = 0; c < 3; c++) {
                rgb[o * 3 + c] = a.rgb[p * 3 + c]!;
                rgb[(o + width) * 3 + c] = b.rgb[p * 3 + c]!;
                const d = Math.min(255, Math.abs(a.rgb[p * 3 + c]! - b.rgb[p * 3 + c]!) * 4);
                rgb[(o + 2 * width) * 3 + c] = mask[p] && c === 2 ? Math.max(d, 96) : d;
            }
        }
    return { width: outWidth, height, rgb };
}

// --- frame times ----------------------------------------------------------------------------------------------------

export interface StatusSample {
    ms: number;
    frames: number;
    current: string;
}

// The game's frames (its main loop's count in the status file - not the frames presented: a slow renderer draws fewer
// of them) over the samples taken while `wait <frames>` ran (the first and the last such sample).
export function frameTime(samples: readonly StatusSample[], frames: number): { ms: number; frames: number } | null {
    const during = samples.filter((s) => s.current.trim() === `wait ${frames}`);
    if (during.length < 2) return null;
    const first = during[0]!;
    const last = during[during.length - 1]!;
    const counted = last.frames - first.frames;
    return counted > 0 ? { ms: (last.ms - first.ms) / counted, frames: counted } : null;
}

// The Vulkan Gpu's own line (vulkan_draws.cpp): "<x> ms a frame (<y> ms of it waiting ...), <d> draws and <p> passes".
export function vulkanFrameLine(log: string): string | null {
    const lines = log.split(/\r?\n/).filter((l) => /^vulkan: frame \d+:.* ms a frame /.test(l));
    const last = lines[lines.length - 1];
    if (!last) return null;
    const m = /([\d.]+) ms a frame \(([\d.]+) ms of it waiting[^)]*\), (\d+) draws and ([\d.]+) passes/.exec(last);
    return m ? `${m[1]} ms a frame (${m[2]} waiting), ${m[3]} draws, ${m[4]} passes` : last.trim();
}

// A measuring build's presents (both renderers: "exp-frames: 300 presents, <x> ms a frame" every 300): the last
// line's milliseconds a presented frame.
export function presentTime(log: string): number | null {
    const found = [...log.matchAll(/^exp-frames: \d+ presents, ([\d.]+) ms a frame/gm)];
    const last = found[found.length - 1];
    return last ? Number(last[1]) : null;
}

// --- running the game -------------------------------------------------------------------------------------------------

function powershell(
    script: string,
    args: string[],
    env: NodeJS.ProcessEnv,
    onTick?: () => void,
): Promise<{ code: number; out: string }> {
    return new Promise((resolve) => {
        const child = spawn(
            "powershell.exe",
            ["-NoProfile", "-NonInteractive", "-File", path.join(HERE, script), ...args],
            {
                env,
                stdio: ["ignore", "pipe", "pipe"],
            },
        );
        let out = "";
        child.stdout.on("data", (d: Buffer) => {
            out += d.toString("utf8");
        });
        child.stderr.on("data", (d: Buffer) => {
            out += d.toString("utf8");
        });
        const timer = onTick ? setInterval(onTick, 100) : null;
        child.on("close", (code) => {
            if (timer) clearInterval(timer);
            resolve({ code: code ?? -1, out });
        });
    });
}

function readStatus(game: string): { frames: number; current: string } | null {
    try {
        const text = fs.readFileSync(path.join(game, "thug2-decomp-status.json"), "utf8");
        const status = JSON.parse(text) as { frames?: number; current?: string };
        return { frames: status.frames ?? 0, current: status.current ?? "" };
    } catch {
        return null;
    }
}

function copyBuild(from: string, game: string): void {
    for (const f of DEPLOYED) {
        const source = path.join(from, f);
        if (!fs.existsSync(source)) exitWith(`${source} isn't there`);
        fs.copyFileSync(source, path.join(game, f));
    }
}

const BAD = /^(EXCEPTION|RUN-TIME CHECK|DEBUG CRT|PURE VIRTUAL|INVALID PARAMETER|ABORT|BUFFER OVERRUN)/m;

async function runGroup(
    game: string,
    group: PlanGroup,
    renderer: string,
    run: number,
    folder: string,
    env: NodeJS.ProcessEnv,
): Promise<string> {
    const dump = path.join(folder, `${group.name}-dump`);
    const groupEnv: NodeJS.ProcessEnv = { ...env };
    for (const [k, v] of Object.entries(group.env ?? {})) groupEnv[k] = v.replaceAll("{dir}", dump);
    if (group.window) groupEnv.THUG2_SHOT_FULL = "1"; // (window_shot.ps1: full size)
    if (group.collect) fs.mkdirSync(dump, { recursive: true });
    const commands = groupCommands(group, renderer, run);
    for (const s of groupSpots(group))
        fs.rmSync(path.join(game, "Screenshots", seriesName(renderer, run, s)), { recursive: true, force: true });
    clearAutomationFiles(game);
    const log = path.join(game, "thug2-decomp.log");
    fs.rmSync(log, { force: true });
    const args = ["-Game", game, "-Seconds", String(group.seconds ?? 240), "-HangSeconds", "120"];
    args.push("-Shot", group.name, "-ShotDir", path.join(folder, "window"));
    if (group.level) args.push("-Level", group.level);
    if (commands.length) args.push("-Commands", commands.join(";"));
    const samples: StatusSample[] = [];
    const start = Date.now();
    const result = await powershell("run_game.ps1", args, groupEnv, () => {
        const s = readStatus(game);
        if (s) samples.push({ ms: Date.now() - start, ...s });
    });
    fs.writeFileSync(path.join(folder, `${group.name}.run.txt`), result.out);
    const text = fs.existsSync(log) ? fs.readFileSync(log, "utf8") : "";
    fs.writeFileSync(path.join(folder, `${group.name}.log`), text);
    const notes: string[] = [`exit ${result.code}`];
    const bad = BAD.exec(text);
    if (bad) notes.push(`LOG: ${bad[0]}`);
    for (const s of groupSpots(group)) {
        const series = path.join(game, "Screenshots", seriesName(renderer, run, s));
        const frame = path.join(series, "frame_0001.png");
        if (fs.existsSync(frame)) fs.copyFileSync(frame, path.join(folder, `${s}.png`));
        else notes.push(`no picture ${s}`);
        fs.rmSync(series, { recursive: true, force: true });
    }
    if (group.window) {
        const shot = fs.readdirSync(path.join(folder, "window")).find((f) => f.startsWith(group.name));
        if (shot) fs.copyFileSync(path.join(folder, "window", shot), path.join(folder, `${group.name}.png`));
        else notes.push("no window picture");
    }
    if (group.collect) {
        for (const f of fs.readdirSync(dump))
            if (/\.(png|ppm)$/i.test(f)) fs.renameSync(path.join(dump, f), path.join(folder, `${group.name}-${f}`));
        fs.rmSync(dump, { recursive: true, force: true });
    }
    if (group.frametime) {
        const measured = frameTime(samples, group.frametime);
        const vk = vulkanFrameLine(text);
        fs.writeFileSync(
            path.join(folder, `${group.name}.frametime.json`),
            `${JSON.stringify({ renderer, run, measured, vulkan: vk }, null, 2)}\n`,
        );
        notes.push(
            measured ? `${measured.ms.toFixed(2)} ms a frame over ${measured.frames}` : "frame time not measured",
        );
    }
    return notes.join("; ");
}

async function runPlan(
    planFile: string,
    options: {
        game: string;
        out: string;
        runs: number;
        renderers: string[];
        groups: string[] | null;
        build: string | null;
        expBuild: string | null;
        owner: string;
        set: Record<string, string>;
    },
): Promise<number> {
    const { game, out } = options;
    if (!isWorkspace(game)) exitWith(`${game} isn't a workspace (C:\\Games\\THUG2-<n>): this tool runs only there`);
    const plan = readPlan(fs.readFileSync(planFile, "utf8"));
    const groups = plan.groups.filter((g) => !options.groups || options.groups.includes(g.name));
    if (!groups.length) exitWith("no group of the plan chosen");
    if (groups.some((g) => g.exp) && !options.expBuild) exitWith("the plan's exp groups need --exp-build");
    fs.mkdirSync(out, { recursive: true });
    const journal = path.join(out, "run.txt");
    const note = (line: string): void => {
        print(line);
        fs.appendFileSync(journal, `${line}\n`);
    };
    const lock = await powershell(
        "game_lock.ps1",
        ["-Action", "acquire", "-Owner", options.owner, "-Game", game],
        process.env,
    );
    note(lock.out.trim());
    if (lock.code !== 0) return 1;
    try {
        const ini = path.join(game, "thug2-decomp.ini");
        ensureWorkspaceMuted(game);
        const base = fs.readFileSync(ini, "utf8");
        let deployed: string | null = null;
        for (let run = 1; run <= options.runs; run++) {
            for (const renderer of options.renderers) {
                for (const group of groups) {
                    const exe = group.exp ? options.expBuild : options.build;
                    if (exe && exe !== deployed) {
                        copyBuild(exe, game);
                        deployed = exe;
                    }
                    const values = [plan.ini ?? {}, group.ini ?? {}, options.set];
                    fs.writeFileSync(ini, iniFor(base, values, renderer), "utf8");
                    ensureWorkspaceMuted(game);
                    const folder = path.join(out, renderer, `run${run}`);
                    fs.mkdirSync(path.join(folder, "window"), { recursive: true });
                    const result = await runGroup(game, group, renderer, run, folder, process.env);
                    note(`${renderer} run ${run} ${group.name}: ${result}`);
                }
            }
        }
    } finally {
        const release = await powershell(
            "game_lock.ps1",
            ["-Action", "release", "-Owner", options.owner, "-Game", game],
            process.env,
        );
        note(release.out.trim());
    }
    return 0;
}

// --- comparing ---------------------------------------------------------------------------------------------------------

function pictureNames(folder: string): string[] {
    return fs.existsSync(folder) ? fs.readdirSync(folder).filter((f) => /\.(png|ppm)$/i.test(f)) : [];
}

function runFolders(out: string, renderer: string): string[] {
    const root = path.join(out, renderer);
    if (!fs.existsSync(root)) return [];
    return fs
        .readdirSync(root)
        .filter((d) => /^run\d+$/.test(d))
        .sort((p, q) => Number(p.slice(3)) - Number(q.slice(3)))
        .map((d) => path.join(root, d));
}

const pct = (v: number): string => `${(100 * v).toFixed(2)}%`;

export function formatResult(r: SpotResult): string {
    const o = r.outside;
    const region = o.region ? `${o.region.join(",")}` : "-";
    const cells = o.cells.map((c) => `${c.x},${c.y}:${c.count}`).join(" ") || "-";
    return [
        `${r.name}: runs d3d9 ${r.runs.d3d9} / vulkan ${r.runs.vulkan}; noise d3d9 ${pct(r.noise.d3d9?.masked ?? 0)} (mean ${(r.noise.d3d9?.mean ?? 0).toFixed(2)}), vulkan ${pct(r.noise.vulkan?.masked ?? 0)} (mean ${(r.noise.vulkan?.mean ?? 0).toFixed(2)}); masked ${pct(r.masked)}`,
        `  all pixels:    mean ${r.all.mean.toFixed(2)} signed ${r.all.signed.toFixed(2)} max ${r.all.largest} over ${r.all.over} (${pct(r.all.over / Math.max(1, r.all.pixels))})`,
        `  outside noise: mean ${o.mean.toFixed(2)} signed ${o.signed.toFixed(2)} max ${o.largest} over ${o.over} (${pct(o.over / Math.max(1, o.pixels))}) region ${region} cells ${cells}`,
    ].join("\n");
}

function compareFolder(out: string, threshold: number, grow: number, pictures: boolean): number {
    const runs = Object.fromEntries(RENDERERS.map((r) => [r, runFolders(out, r)])) as Record<string, string[]>;
    if (!runs.d3d9!.length || !runs.vulkan!.length) exitWith(`${out} needs d3d9/run<k> and vulkan/run<k> folders`);
    const names = pictureNames(runs.d3d9![0]!).filter((n) => fs.existsSync(path.join(runs.vulkan![0]!, n)));
    const results: SpotResult[] = [];
    const lines: string[] = [`threshold ${threshold}, mask grown by ${grow}`];
    for (const name of names.sort()) {
        const set: Record<string, Image[]> = {};
        for (const r of RENDERERS)
            set[r] = runs[r]!.map((f) => path.join(f, name))
                .filter((f) => fs.existsSync(f))
                .map(readPicture);
        const sizes = new Set(Object.values(set).flatMap((images) => images.map((i) => `${i.width}x${i.height}`)));
        if (sizes.size !== 1) {
            lines.push(`${name}: sizes differ (${[...sizes].join(", ")}) - skipped`);
            continue;
        }
        const result = compareSpot(name.replace(/\.(png|ppm)$/i, ""), set, threshold, grow);
        results.push(result);
        lines.push(formatResult(result));
        if (pictures) {
            fs.mkdirSync(path.join(out, "sbs"), { recursive: true });
            const first = set.d3d9![0]!;
            const union = unionMask(set, threshold, grow);
            fs.writeFileSync(
                path.join(out, "sbs", `${result.name}.png`),
                encodePng(sideBySide(first, set.vulkan![0]!, union)),
            );
        }
    }
    for (const r of RENDERERS)
        for (const f of runs[r]!)
            for (const t of fs.readdirSync(f).filter((n) => n.endsWith(".frametime.json")))
                lines.push(
                    `frame time ${r} ${path.basename(f)} ${t}: ${fs.readFileSync(path.join(f, t), "utf8").replace(/\s+/g, " ")}`,
                );
    fs.writeFileSync(path.join(out, "report.txt"), `${lines.join("\n")}\n`);
    fs.writeFileSync(path.join(out, "report.json"), `${JSON.stringify(results, null, 1)}\n`);
    for (const l of lines) print(l);
    return 0;
}

async function main(): Promise<number> {
    const { values, positionals } = parseCli(USAGE, {
        options: {
            game: { type: "string" },
            out: { type: "string" },
            runs: { type: "string", default: "2" },
            renderers: { type: "string", default: RENDERERS.join(",") },
            groups: { type: "string" },
            build: { type: "string" },
            "exp-build": { type: "string" },
            owner: { type: "string", default: "renderer-parity" },
            set: { type: "string", multiple: true },
            threshold: { type: "string", default: "8" },
            grow: { type: "string", default: "1" },
            pictures: { type: "boolean", default: false },
        },
        allowPositionals: true,
    });
    const [command, target] = positionals;
    if (command === "compare" && target && positionals.length === 2)
        return compareFolder(target, Number(values.threshold), Number(values.grow), values.pictures);
    if (command === "run" && target && positionals.length === 2) {
        if (!values.game || !values.out) exitWith("run needs --game <workspace> and --out <folder>");
        const set: Record<string, string> = {};
        for (const s of values.set ?? []) {
            const eq = s.indexOf("=");
            if (eq <= 0) exitWith(`--set Section.key=value (${s})`);
            set[s.slice(0, eq)] = s.slice(eq + 1);
        }
        const renderers = values.renderers.split(",").filter((r) => r);
        for (const r of renderers) if (!(RENDERERS as readonly string[]).includes(r)) exitWith(`no renderer ${r}`);
        const code = await runPlan(target, {
            game: path.resolve(values.game),
            out: path.resolve(values.out),
            runs: Number(values.runs),
            renderers,
            groups: values.groups ? values.groups.split(",") : null,
            build: values.build ? path.resolve(values.build) : null,
            expBuild: values["exp-build"] ? path.resolve(values["exp-build"]) : null,
            owner: values.owner,
            set,
        });
        if (code !== 0) return code;
        return renderers.length === RENDERERS.length && Number(values.runs) >= 1
            ? compareFolder(path.resolve(values.out), Number(values.threshold), Number(values.grow), values.pictures)
            : 0;
    }
    print(USAGE);
    return 2;
}

if (isMain(import.meta.url)) await runMain(() => main());
