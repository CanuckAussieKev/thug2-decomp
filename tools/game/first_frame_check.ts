// NOT ORIGINAL - the first frame after a load checked in the game (docs/design/vulkan.md "The first frame after a load"):
// with the ray-traced effects on, the first frame the player sees must already look as it does a second later - its
// shadows, bounce light, occlusion and fog there at once, nothing fading in.
//
//   node tools/game/first_frame_check.ts run --game C:\Games\THUG2-<n> --out <folder> --build <folder>
//        [--settings <ini>] [--levels BO,AU,z_dt,foun] [--owner <name>] [--set Section.key=value]...
//   node tools/game/first_frame_check.ts compare <folder> [--threshold 12] [--grow 2]
//
// run: under the workspace's lock (game_lock.ps1, released at the end - which puts the folder's own build and ini back),
// the x64 build deployed (deploy_build.ps1 -Arch x64), then for each configuration and level the ini written and the
// game started straight into the level (run_game.ps1 -Level) with these automation commands (src/devtools/automation.h):
//   camera <the level's spot - held through the load>; capture 61 <series> afterload; waitfor level <it>; wait 240
// - `capture ... afterload` takes the first frame drawn after the level's loading screen is taken down and the 60 after
// it, so
// frame_0001.png is the level's first frame and frame_0060.png the 60th, from the same still camera. Gathered into
// <out>/<config>/<level>/frame_0001.png, frame_0050.png ... frame_0060.png, with the level's log.
// The configurations:
//   on   the [Graphics] section of --settings (default: the main install's thug2-decomp.ini, read only) - every key but
//        the window's (ResolutionX / Y, Windowed, Borderless, WindowDisplay: the workspace's own) - and --set's;
//   off  the same with ray_tracing=0 and every rt_ effect 0: what moves in the level by itself between frame 1 and 60
//        (peds, water, the skater idling, animated textures) - the scene's own noise.
// Workspaces only; muted (workspace_ini.ts).
//
// compare (run does it at the end), per level:
//   - the mask: the pixels that differ by more than --threshold in a channel between frame 1 and frame 60 in `off` (the
//     scene's own motion) or between any two of frames 50 - 60 in `on` (the effects' converged frame-to-frame noise and
//     motion), grown by --grow;
//   - outside it, `on`'s frame 1 against its frame 60: the share of pixels differing by more than --threshold (at most
//     kMaxChangedShare) and the mean channel difference (at most kMaxMean) - an effect still fading in (no shadows yet,
//     the bounce light half there) changes large areas by many steps;
//   - the logs: `on`'s "rt: a new world's first frame" line before "screenshot: series - the first frame after the
//     loading screen" (the world's structures and sun made before its first frame was drawn), no crash line; the times
//     reported - the structures and the sun's fit, the first frame, the loading screen.
// report.txt / report.json in the folder; exit 1 when a check fails.

import { spawn } from "node:child_process";
import * as fs from "node:fs";
import * as path from "node:path";
import { fileURLToPath } from "node:url";
import { exitWith, isMain, parseCli, print, runMain } from "thug2-level-converter-core/lib/cli.ts";
import type { Image } from "thug2-level-converter-core/lib/frame_diff.ts";
import { clearAutomationFiles } from "./edit_level.ts";
import { growMask, iniFor, noiseMask, readPicture } from "./renderer_parity.ts";
import { ensureWorkspaceMuted, isWorkspace } from "./workspace_ini.ts";

const USAGE = `usage: first_frame_check.ts run --game <workspace> --out <folder> --build <folder> [--settings <ini>]
                           [--levels BO,AU,z_dt,foun] [--owner name] [--set S.key=v]...
       first_frame_check.ts compare <folder> [--threshold 12] [--grow 2]

A level's first frame against its 60th from a still camera, ray-traced effects on (tools/game/README.md).`;

const HERE = path.dirname(fileURLToPath(import.meta.url));
const MAIN_INI = "C:\\Games\\THUG2\\Game\\thug2-decomp.ini";
export const CONFIGS = ["off", "on"] as const;
export type Config = (typeof CONFIGS)[number];
export const FRAMES = 61;
export const LAST = 60;
export const STEADY_FROM = 50; // (frames 50 - 60: the converged noise)
export const kMaxChangedShare = 0.005;
export const kMaxMean = 0.6;

// The spots: a camera held through the load (rt_shadows.json's, without the teleports - the skater stays at the
// level's start).
export interface Spot {
    level: string;
    camera: string;
}
export const SPOTS: readonly Spot[] = [
    { level: "BO", camera: "camera 1003.8 308.6 -1989.1 0.158 0.145 -0.977" },
    { level: "AU", camera: "camera -2599.5 247.8 7625.6 -0.286 -0.179 -0.941" },
    { level: "z_dt", camera: "camera -1338.5 83.4 2291.3 -0.570 -0.178 0.802" },
    { level: "foun", camera: "camera -4080.9 -293.7 2523.3 -0.044 -0.311 -0.949" },
];

// The keys of the user's [Graphics] the workspace keeps its own of (its window).
const WINDOW_KEYS = new Set(["resolutionx", "resolutiony", "windowed", "borderless", "windowdisplay"]);

// An ini's [Graphics] section as Section.key values.
export function graphicsOf(text: string): Record<string, string> {
    const out: Record<string, string> = {};
    let inside = false;
    for (const raw of text.split(/\r?\n/)) {
        const line = raw.trim();
        if (line.startsWith("[")) {
            inside = line.toLowerCase() === "[graphics]";
            continue;
        }
        if (!inside || !line || line.startsWith(";") || line.startsWith("#")) continue;
        const eq = line.indexOf("=");
        if (eq <= 0) continue;
        const key = line.slice(0, eq).trim();
        if (WINDOW_KEYS.has(key.toLowerCase())) continue;
        out[`Graphics.${key}`] = line.slice(eq + 1).trim();
    }
    return out;
}

// A configuration's values from the user's [Graphics]: `on` as they are, `off` with ray tracing and every rt_ effect off.
export function configValues(config: Config, graphics: Record<string, string>): Record<string, string> {
    if (config === "on") return { ...graphics };
    const out: Record<string, string> = {};
    for (const [key, value] of Object.entries(graphics)) {
        const name = key.slice("Graphics.".length).toLowerCase();
        out[key] = name === "ray_tracing" || name.startsWith("rt_") ? "0" : value;
    }
    out["Graphics.ray_tracing"] = "0";
    return out;
}

export function seriesName(config: Config, level: string): string {
    return `firstframe-${config}-${level}`;
}

export const frameName = (n: number): string => `frame_${String(n).padStart(4, "0")}.png`;

export function levelCommands(spot: Spot, config: Config): string[] {
    return [
        spot.camera,
        `capture ${FRAMES} ${seriesName(config, spot.level)} afterload`,
        `waitfor level ${spot.level} 300`,
        "wait 240",
    ];
}

// --- the comparison ------------------------------------------------------------------------------------------------

export interface FrameDifference {
    compared: number; // pixels outside the mask
    masked: number;
    changed: number; // of the compared, differing by more than the threshold in a channel
    mean: number; // channel difference over the compared pixels
}

export function difference(a: Image, b: Image, mask: Uint8Array, threshold: number): FrameDifference {
    let compared = 0;
    let changed = 0;
    let sum = 0;
    for (let p = 0; p < mask.length; p++) {
        if (mask[p]) continue;
        compared++;
        const i = p * 3;
        let most = 0;
        for (let c = 0; c < 3; c++) {
            const d = Math.abs(a.rgb[i + c]! - b.rgb[i + c]!);
            sum += d;
            most = Math.max(most, d);
        }
        if (most > threshold) changed++;
    }
    return { compared, masked: mask.length - compared, changed, mean: sum / Math.max(1, compared * 3) };
}

// The two masks combined and grown.
export function combinedMask(masks: readonly Uint8Array[], width: number, height: number, grow: number): Uint8Array {
    const out = new Uint8Array(width * height);
    for (const m of masks) for (let p = 0; p < out.length; p++) if (m[p]) out[p] = 1;
    return growMask(out, width, height, grow);
}

export interface LogTimes {
    work: number | null; // ms: the structures and the sun before the first frame was drawn
    firstFrame: number | null; // ms: the first frame, record to record
    loading: number | null; // s: the loading screen
    workBeforeFirst: boolean; // the new world's work logged before the series' first frame
    crashed: boolean;
}

const WORK = /rt: a new world's first frame: .* (\d+) ms in all before it is drawn/;
const FIRST = /rt: the new world's first frame took (\d+) ms/;
const LOADING = /loading console: the loading screen was up ([\d.]+) s/g;
const SERIES_FIRST = "screenshot: series - the first frame after the loading screen";
const BAD = /^(EXCEPTION|RUN-TIME CHECK|DEBUG CRT|PURE VIRTUAL|INVALID PARAMETER|ABORT|BUFFER OVERRUN)/m;

export function logTimes(log: string): LogTimes {
    const series = log.indexOf(SERIES_FIRST);
    // (the level's: the last of each before the series' first frame - the front end has its own)
    const before = series >= 0 ? log.slice(0, series) : log;
    const works = [...before.matchAll(new RegExp(WORK.source, "g"))];
    const lastWork = works.at(-1);
    const after = lastWork ? log.slice(lastWork.index) : "";
    const first = FIRST.exec(after);
    const loadings = [...before.matchAll(LOADING)];
    return {
        work: lastWork ? Number(lastWork[1]) : null,
        firstFrame: first ? Number(first[1]) : null,
        loading: loadings.length ? Number(loadings.at(-1)![1]) : null,
        workBeforeFirst: series >= 0 && lastWork !== undefined,
        crashed: BAD.test(log),
    };
}

export interface LevelVerdict {
    level: string;
    ok: boolean;
    notes: string[];
    first: FrameDifference | null;
    times: LogTimes | null;
}

function picturesOf(folder: string, frames: readonly number[]): Image[] | null {
    const files = frames.map((n) => path.join(folder, frameName(n)));
    if (!files.every((f) => fs.existsSync(f))) return null;
    return files.map((f) => readPicture(f));
}

export function judgeLevel(out: string, level: string, threshold: number, grow: number): LevelVerdict {
    const notes: string[] = [];
    const onFolder = path.join(out, "on", level);
    const offFolder = path.join(out, "off", level);
    const steady: number[] = [];
    for (let n = STEADY_FROM; n <= LAST; n++) steady.push(n);
    const on = picturesOf(onFolder, [1, ...steady]);
    const off = picturesOf(offFolder, [1, LAST]);
    if (!on || !off) {
        return {
            level,
            ok: false,
            notes: ["pictures missing (on: frames 1, 50 - 60; off: 1 and 60)"],
            first: null,
            times: null,
        };
    }
    const width = on[0]!.width;
    const height = on[0]!.height;
    const mask = combinedMask([noiseMask(off, threshold), noiseMask(on.slice(1), threshold)], width, height, grow);
    const first = difference(on[0]!, on.at(-1)!, mask, threshold);
    const share = first.changed / Math.max(1, first.compared);
    let ok = true;
    if (share > kMaxChangedShare) {
        ok = false;
        notes.push(
            `frame 1 differs from frame ${LAST} in ${(100 * share).toFixed(2)}% of the pixels (at most ${100 * kMaxChangedShare}%)`,
        );
    }
    if (first.mean > kMaxMean) {
        ok = false;
        notes.push(`frame 1's mean difference from frame ${LAST} ${first.mean.toFixed(3)} (at most ${kMaxMean})`);
    }
    if (first.compared < 0.5 * width * height)
        notes.push(`only ${first.compared} pixels compared (the mask took the rest)`);
    const logFile = path.join(onFolder, "game.log");
    const times = fs.existsSync(logFile) ? logTimes(fs.readFileSync(logFile, "utf8")) : null;
    if (!times) {
        ok = false;
        notes.push("no log");
    } else {
        if (!times.workBeforeFirst) {
            ok = false;
            notes.push("the new world's work wasn't logged before the series' first frame");
        }
        if (times.crashed) {
            ok = false;
            notes.push("a crash line in the log");
        }
    }
    return { level, ok, notes, first, times };
}

function compareFolder(out: string, threshold: number, grow: number): number {
    const levels = fs
        .readdirSync(path.join(out, "on"), { withFileTypes: true })
        .filter((d) => d.isDirectory())
        .map((d) => d.name);
    const verdicts = levels.map((l) => judgeLevel(out, l, threshold, grow));
    const lines: string[] = [`first frame against frame ${LAST} (threshold ${threshold}, mask grown ${grow})`];
    for (const v of verdicts) {
        const d = v.first;
        const t = v.times;
        lines.push(
            `${v.ok ? "ok  " : "FAIL"} ${v.level}: ` +
                (d
                    ? `${d.changed} of ${d.compared} pixels changed (${((100 * d.changed) / Math.max(1, d.compared)).toFixed(3)}%), mean ${d.mean.toFixed(3)}, ${d.masked} masked`
                    : "-") +
                (t
                    ? `; work before the first frame ${t.work ?? "?"} ms, the first frame ${t.firstFrame ?? "?"} ms, the loading screen ${t.loading ?? "?"} s`
                    : ""),
        );
        for (const n of v.notes) lines.push(`       ${n}`);
    }
    const failed = verdicts.filter((v) => !v.ok).length;
    lines.push(failed ? `${failed} of ${verdicts.length} levels failed` : `all ${verdicts.length} levels passed`);
    fs.writeFileSync(path.join(out, "report.txt"), `${lines.join("\n")}\n`);
    fs.writeFileSync(path.join(out, "report.json"), `${JSON.stringify(verdicts, null, 2)}\n`);
    for (const l of lines) print(l);
    return failed ? 1 : 0;
}

// --- the run -------------------------------------------------------------------------------------------------------

function powershell(script: string, args: string[]): Promise<{ code: number; out: string }> {
    return new Promise((resolve) => {
        const child = spawn(
            "powershell.exe",
            ["-NoProfile", "-NonInteractive", "-File", path.join(HERE, script), ...args],
            {
                stdio: ["ignore", "pipe", "pipe"],
            },
        );
        let text = "";
        child.stdout.on("data", (d: Buffer) => {
            text += d.toString("utf8");
        });
        child.stderr.on("data", (d: Buffer) => {
            text += d.toString("utf8");
        });
        child.on("close", (code) => resolve({ code: code ?? -1, out: text }));
    });
}

async function runLevel(game: string, spot: Spot, config: Config, folder: string): Promise<string> {
    const series = path.join(game, "Screenshots", seriesName(config, spot.level));
    fs.rmSync(series, { recursive: true, force: true });
    clearAutomationFiles(game);
    const log = path.join(game, "thug2-decomp.log");
    fs.rmSync(log, { force: true });
    const args = ["-Game", game, "-Arch", "x64", "-Seconds", "400", "-HangSeconds", "120"];
    args.push("-Shot", spot.level, "-ShotDir", path.join(folder, "window"), "-Level", spot.level);
    args.push("-Commands", levelCommands(spot, config).join(";"));
    const result = await powershell("run_game.ps1", args);
    fs.mkdirSync(folder, { recursive: true });
    fs.writeFileSync(path.join(folder, "run.txt"), result.out);
    fs.writeFileSync(path.join(folder, "game.log"), fs.existsSync(log) ? fs.readFileSync(log, "utf8") : "");
    const notes = [`exit ${result.code}`];
    let copied = 0;
    for (let n = 1; n <= FRAMES; n++) {
        const file = path.join(series, frameName(n));
        if (n !== 1 && n < STEADY_FROM) continue;
        if (fs.existsSync(file)) {
            fs.copyFileSync(file, path.join(folder, frameName(n)));
            copied++;
        }
    }
    notes.push(`${copied} pictures`);
    fs.rmSync(series, { recursive: true, force: true });
    return notes.join("; ");
}

interface RunOptions {
    game: string;
    out: string;
    build: string;
    settings: string;
    levels: string[] | null;
    owner: string;
    set: Record<string, string>;
}

async function runAll(options: RunOptions): Promise<number> {
    const { game, out } = options;
    if (!isWorkspace(game)) exitWith(`${game} isn't a workspace (C:\\Games\\THUG2-<n>): this tool runs only there`);
    const spots = SPOTS.filter((s) => !options.levels || options.levels.includes(s.level));
    if (!spots.length) exitWith("no level chosen");
    const graphics = graphicsOf(fs.readFileSync(options.settings, "utf8"));
    fs.mkdirSync(out, { recursive: true });
    const journal = path.join(out, "run.txt");
    const note = (line: string): void => {
        print(line);
        fs.appendFileSync(journal, `${line}\n`);
    };
    const lock = await powershell("game_lock.ps1", [
        "-Action",
        "acquire",
        "-Owner",
        options.owner,
        "-Game",
        game,
        "-WaitMinutes",
        "30",
    ]);
    note(lock.out.trim());
    if (lock.code !== 0) return 1;
    try {
        const deploy = await powershell("deploy_build.ps1", [
            "-Game",
            game,
            "-Arch",
            "x64",
            "-BuildDir",
            options.build,
            "-NoEditorTools",
        ]);
        note(deploy.out.trim());
        if (deploy.code !== 0) return 1;
        const ini = path.join(game, "thug2-decomp.ini");
        ensureWorkspaceMuted(game);
        const base = fs.readFileSync(ini, "utf8");
        for (const config of CONFIGS) {
            fs.writeFileSync(ini, iniFor(base, [configValues(config, graphics), options.set], "vulkan"), "utf8");
            ensureWorkspaceMuted(game);
            for (const spot of spots) {
                note(
                    `${config} ${spot.level}: ${await runLevel(game, spot, config, path.join(out, config, spot.level))}`,
                );
            }
        }
    } finally {
        const release = await powershell("game_lock.ps1", [
            "-Action",
            "release",
            "-Owner",
            options.owner,
            "-Game",
            game,
        ]);
        note(release.out.trim());
    }
    return 0;
}

async function main(): Promise<number> {
    const { values, positionals } = parseCli(USAGE, {
        options: {
            game: { type: "string" },
            out: { type: "string" },
            build: { type: "string" },
            settings: { type: "string", default: MAIN_INI },
            levels: { type: "string" },
            owner: { type: "string", default: "first-frame-check" },
            set: { type: "string", multiple: true },
            threshold: { type: "string", default: "12" },
            grow: { type: "string", default: "2" },
        },
        allowPositionals: true,
    });
    const [command, target] = positionals;
    const threshold = Number(values.threshold);
    const grow = Number(values.grow);
    const set: Record<string, string> = {};
    for (const s of values.set ?? []) {
        const eq = s.indexOf("=");
        if (eq <= 0) exitWith(`--set Section.key=value (${s})`);
        set[s.slice(0, eq)] = s.slice(eq + 1);
    }
    if (command === "compare" && target && positionals.length === 2)
        return compareFolder(path.resolve(target), threshold, grow);
    if (command === "run" && positionals.length === 1) {
        if (!values.game || !values.out || !values.build) exitWith("run needs --game, --out and --build");
        const out = path.resolve(values.out);
        const code = await runAll({
            game: path.resolve(values.game),
            out,
            build: path.resolve(values.build),
            settings: path.resolve(values.settings),
            levels: values.levels ? values.levels.split(",") : null,
            owner: values.owner,
            set,
        });
        return code !== 0 ? code : compareFolder(out, threshold, grow);
    }
    print(USAGE);
    return 2;
}

if (isMain(import.meta.url)) await runMain(() => main());
