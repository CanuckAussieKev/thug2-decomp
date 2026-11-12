// NOT ORIGINAL - ray-traced sun shadows checked in the game ([Graphics] rt_shadows, src/gpu_vulkan/vulkan_rt_shadows.h):
// the same spots captured with the shadows off and on (and, optionally, with another build - main's - as a baseline),
// and the pictures compared outside each configuration's run-to-run noise.
//
//   node tools/game/rt_shadows_check.ts run --game C:\Games\THUG2-<n> --out <folder> --build <folder>
//        [--baseline-build <folder>] [--plan <plan.json>] [--groups a,b] [--runs 2] [--owner <name>]
//        [--set Section.key=value]...
//   node tools/game/rt_shadows_check.ts compare <folder> [--expect shadows|none] [--strength 0.5] [--threshold 8]
//        [--grow 1]
//
// run: under the workspace's lock (game_lock.ps1, released at the end - which puts the folder's own build and ini back),
// the x64 build deployed (deploy_build.ps1 -Arch x64: the exe and every DLL beside it), then for each run and each
// configuration the ini written (the workspace's own + renderer=vulkan + the configuration's values + --set's) and each
// level group of the plan run (run_game.ps1 -Level, the automation channel's teleport / camera / capture 1 per spot),
// its pictures gathered into <out>/<config>/run<k>/<spot>.png and its log into <out>/<config>/run<k>/<group>.log:
//   off       ray_tracing=1, rt_shadows=0
//   on        ray_tracing=1, rt_shadows=1 (strength, softness... from --set)
//   baseline  --baseline-build's exe with the ini as the workspace has it (no ray tracing): only with --baseline-build
// Workspaces only; muted (workspace_ini.ts). The plan: rt_shadows.json beside this file (renderer_parity.ts's format; its
// level groups' spots are used, their commands and frame times aren't).
//
// compare (run does it at the end): per spot, the pixels outside every configuration's noise (any two of its runs
// differing by more than --threshold in a channel, grown by --grow), run 1 of each compared:
//   --expect shadows (after the TLAS's builder and the frame record are merged): on against off
//     - brighter: pixels where on is lighter than off by more than the threshold - at most 0.1% (the pass only darkens);
//     - darkened: pixels darker by more than the threshold - at most 80% of the picture (a spot can stand in the bake's
//       own shadow: z_ho's start), and at least 0.5% in one spot or more (shadows are drawn somewhere);
//     - the darkened pixels' median brightness ratio (on / off) from 1 - strength - 0.2 to 0.9, where 1% of the picture
//       or more is darkened (1 - strength is a full shadow; the sky test and the bake's agreement let in shares of one);
//     - the sun lines up with the bake: over every spot, the pixels the level lit darker in `off` (its luma under the
//       picture's Otsu threshold: baked-dark) darkened at least 1.5 times as often as the ones it lit brighter
//       (baked-bright) - a sun the bake wasn't made with darkens both alike;
//     - a level whose bake shows no sun (its `on` log: "rt: the baked sun: none" - indoors) changed in at most 0.1%;
//     - reported, not checked: the share of the darkened pixels in the picture's lower half (the ground, mostly);
//     - the logs: "rt_shadows: the first shadow pass" in every `on` group's (or the baked sun's "none"), no "skipped" /
//       "couldn't" line of the pass, no crash line.
//   --expect none (before them: no scene to trace - the pass is skipped): on against off differ in at most 0.1% of the
//     pixels, and the `on` logs say the pass was skipped.
//   baseline (when run): off against baseline differ in at most 0.1% of the pixels.
// report.txt / report.json in the folder; exit 1 when a check fails.

import { spawn } from "node:child_process";
import * as fs from "node:fs";
import * as path from "node:path";
import { fileURLToPath } from "node:url";
import { exitWith, isMain, parseCli, print, runMain } from "thug2-level-converter-core/lib/cli.ts";
import type { Image } from "thug2-level-converter-core/lib/frame_diff.ts";
import { clearAutomationFiles } from "./edit_level.ts";
import {
    groupCommands,
    groupSpots,
    growMask,
    iniFor,
    noiseMask,
    type PlanGroup,
    readPicture,
    readPlan,
    seriesName,
} from "./renderer_parity.ts";
import { ensureWorkspaceMuted, isWorkspace } from "./workspace_ini.ts";

const USAGE = `usage: rt_shadows_check.ts run --game <workspace> --out <folder> --build <folder> [--baseline-build <folder>]
                           [--plan <plan.json>] [--groups a,b] [--runs N] [--owner name] [--set S.key=v]...
       rt_shadows_check.ts compare <folder> [--expect shadows|none] [--strength 0.5] [--threshold 8] [--grow 1]

Ray-traced sun shadows in the game: the same spots with rt_shadows off and on, compared (tools/game/README.md).`;

const HERE = path.dirname(fileURLToPath(import.meta.url));
export const CONFIGS = ["off", "on", "baseline"] as const;
export type Config = (typeof CONFIGS)[number];

// The ini values a configuration adds.
export function configValues(config: Config): Record<string, string> {
    if (config === "off") return { "Graphics.ray_tracing": "1", "Graphics.rt_shadows": "0" };
    if (config === "on") return { "Graphics.ray_tracing": "1", "Graphics.rt_shadows": "1" };
    return {};
}

// --- comparing ---------------------------------------------------------------------------------------------------------

const luma = (rgb: Uint8Array, p: number): number =>
    0.299 * rgb[p * 3]! + 0.587 * rgb[p * 3 + 1]! + 0.114 * rgb[p * 3 + 2]!;

export interface ShadowDifference {
    pixels: number; // compared (outside the mask)
    brighter: number; // pixels where `on` is lighter by more than the threshold in a channel
    darkened: number; // darker by more than the threshold in a channel (and no channel lighter)
    lowerHalf: number; // of the darkened, those in the picture's lower half
    medianRatio: number | null; // the darkened pixels' median luma ratio on / off
    // The `off` picture's pixels split into baked-bright and baked-dark at its luma's Otsu threshold (the level's own
    // lighting: sunlit against shaded); how many of each the pass darkened. A sun that matches the bake darkens mostly
    // what is already dark - its shadows fall on the baked ones.
    split: number; // the threshold (luma, 0..255)
    bright: number;
    brightDarkened: number;
    dark: number;
    darkDarkened: number;
}

// The luma threshold that best splits a histogram in two (Otsu's: the largest between-class variance).
export function otsuThreshold(histogram: readonly number[]): number {
    let total = 0;
    let sum = 0;
    for (let i = 0; i < histogram.length; i++) {
        total += histogram[i]!;
        sum += i * histogram[i]!;
    }
    let below = 0;
    let belowSum = 0;
    let best = 0;
    let bestVariance = -1;
    for (let t = 0; t < histogram.length; t++) {
        below += histogram[t]!;
        if (below === 0) continue;
        const above = total - below;
        if (above === 0) break;
        belowSum += t * histogram[t]!;
        const meanBelow = belowSum / below;
        const meanAbove = (sum - belowSum) / above;
        const variance = below * above * (meanBelow - meanAbove) ** 2;
        if (variance > bestVariance) {
            bestVariance = variance;
            best = t;
        }
    }
    return best;
}

// `on` against `off` outside the mask.
export function shadowDifference(off: Image, on: Image, mask: Uint8Array | null, threshold: number): ShadowDifference {
    if (off.width !== on.width || off.height !== on.height)
        throw new Error(`sizes differ: ${off.width}x${off.height} / ${on.width}x${on.height}`);
    const { width, height } = off;
    let pixels = 0;
    let brighter = 0;
    let darkened = 0;
    let lowerHalf = 0;
    const ratios: number[] = [];
    const histogram = new Array<number>(256).fill(0);
    for (let p = 0; p < width * height; p++) if (!mask?.[p]) histogram[Math.min(255, Math.round(luma(off.rgb, p)))]!++;
    const split = otsuThreshold(histogram);
    let bright = 0;
    let brightDarkened = 0;
    let dark = 0;
    let darkDarkened = 0;
    for (let y = 0; y < height; y++)
        for (let x = 0; x < width; x++) {
            const p = y * width + x;
            if (mask?.[p]) continue;
            pixels++;
            let up = 0;
            let down = 0;
            for (let c = 0; c < 3; c++) {
                const delta = on.rgb[p * 3 + c]! - off.rgb[p * 3 + c]!;
                up = Math.max(up, delta);
                down = Math.max(down, -delta);
            }
            const isBright = Math.round(luma(off.rgb, p)) > split;
            if (isBright) bright++;
            else dark++;
            if (up > threshold) {
                brighter++;
            } else if (down > threshold) {
                darkened++;
                if (isBright) brightDarkened++;
                else darkDarkened++;
                if (y >= height / 2) lowerHalf++;
                const before = luma(off.rgb, p);
                if (before > 0) ratios.push(luma(on.rgb, p) / before);
            }
        }
    ratios.sort((a, b) => a - b);
    return {
        pixels,
        brighter,
        darkened,
        lowerHalf,
        medianRatio: ratios.length ? ratios[Math.floor(ratios.length / 2)]! : null,
        split,
        bright,
        brightDarkened,
        dark,
        darkDarkened,
    };
}

// The union of the configurations' noise masks, grown.
export function noiseOf(pictures: readonly Image[][], threshold: number, grow: number): Uint8Array {
    const first = pictures[0]![0]!;
    const union = new Uint8Array(first.width * first.height);
    for (const images of pictures) {
        if (images.length < 2) continue;
        const mask = noiseMask(images, threshold);
        for (let p = 0; p < mask.length; p++) if (mask[p]) union[p] = 1;
    }
    return growMask(union, first.width, first.height, grow);
}

export interface Limits {
    expect: "shadows" | "none";
    strength: number;
    maxBrighter: number; // shares of the compared pixels
    maxDarkened: number;
    minDarkenedSomewhere: number;
    ratioSlack: number;
    maxRatio: number; // the median ratio at most this ...
    minRatioShare: number; // ... both checked where at least this share is darkened
    minDarkOverBright: number; // over every spot: baked-dark darkened / baked-bright darkened, at least
    maxChanged: number; // --expect none and the baseline
}

export const LIMITS: Omit<Limits, "expect" | "strength"> = {
    maxBrighter: 0.001,
    maxDarkened: 0.8,
    minDarkenedSomewhere: 0.005,
    ratioSlack: 0.2,
    maxRatio: 0.9,
    minRatioShare: 0.01,
    minDarkOverBright: 1.5,
    maxChanged: 0.001,
};

export interface SpotVerdict {
    name: string;
    difference: ShadowDifference;
    baseline: ShadowDifference | null; // off against the baseline (brighter + darkened = changed)
    problems: string[];
}

const share = (count: number, d: ShadowDifference): number => count / Math.max(1, d.pixels);
const pct = (v: number): string => `${(100 * v).toFixed(2)}%`;

// One spot's verdict (the "somewhere" check is the folder's: anyShadow).
export function judgeSpot(
    name: string,
    difference: ShadowDifference,
    baseline: ShadowDifference | null,
    limits: Limits,
    sunless = false, // the level's bake shows no sun: no pass there
): SpotVerdict {
    const problems: string[] = [];
    if (limits.expect === "shadows" && sunless) {
        const changed = share(difference.brighter + difference.darkened, difference);
        if (changed > limits.maxChanged) problems.push(`changed in a level with no baked sun: ${pct(changed)}`);
    } else if (limits.expect === "shadows") {
        if (share(difference.brighter, difference) > limits.maxBrighter)
            problems.push(`brighter with the shadows on: ${pct(share(difference.brighter, difference))}`);
        if (share(difference.darkened, difference) > limits.maxDarkened)
            problems.push(`darkened: ${pct(share(difference.darkened, difference))} of the picture`);
        const full = 1 - limits.strength;
        const ratio = difference.medianRatio;
        if (
            ratio !== null &&
            share(difference.darkened, difference) >= limits.minRatioShare &&
            (ratio < full - limits.ratioSlack || ratio > limits.maxRatio)
        )
            problems.push(
                `the shadows' median ratio ${ratio.toFixed(3)}, ${(full - limits.ratioSlack).toFixed(3)} - ${limits.maxRatio} wanted`,
            );
    } else {
        const changed = share(difference.brighter + difference.darkened, difference);
        if (changed > limits.maxChanged) problems.push(`changed with the pass skipped: ${pct(changed)}`);
    }
    if (baseline) {
        const changed = share(baseline.brighter + baseline.darkened, baseline);
        if (changed > limits.maxChanged) problems.push(`off differs from the baseline: ${pct(changed)}`);
    }
    return { name, difference, baseline, problems };
}

// Over every spot: how many times as often the baked-dark pixels were darkened as the baked-bright ones (null: none
// darkened).
export function darkOverBright(verdicts: readonly SpotVerdict[]): number | null {
    let bright = 0;
    let brightDarkened = 0;
    let dark = 0;
    let darkDarkened = 0;
    for (const { difference: d } of verdicts) {
        bright += d.bright;
        brightDarkened += d.brightDarkened;
        dark += d.dark;
        darkDarkened += d.darkDarkened;
    }
    if (!brightDarkened && !darkDarkened) return null;
    return darkDarkened / Math.max(1, dark) / Math.max(1e-9, brightDarkened / Math.max(1, bright));
}

export function anyShadow(verdicts: readonly SpotVerdict[], limits: Limits): boolean {
    return verdicts.some((v) => share(v.difference.darkened, v.difference) >= limits.minDarkenedSomewhere);
}

// What a configuration's logs must (not) say.
const FIRST_PASS = "rt_shadows: the first shadow pass";
// a level whose bake shows no sun (indoors: src/engine/gfx/rt_sun.h) has no shadow pass - by design
const NO_BAKED_SUN = "rt: the baked sun: none";
const PASS_SKIPPED = /rt_shadows: .*(skipped|couldn't)/;
const BAD = /^(EXCEPTION|RUN-TIME CHECK|DEBUG CRT|PURE VIRTUAL|INVALID PARAMETER|ABORT|BUFFER OVERRUN)/m;

export function logProblems(config: Config, log: string, expect: Limits["expect"]): string[] {
    const problems: string[] = [];
    const bad = BAD.exec(log);
    if (bad) problems.push(`log: ${bad[0]}`);
    if (config !== "on") return problems;
    const skipped = PASS_SKIPPED.exec(log);
    if (expect === "shadows") {
        if (!log.includes(FIRST_PASS) && !log.includes(NO_BAKED_SUN)) problems.push("no shadow pass in the log");
        if (skipped) problems.push(`log: ${skipped[0]}`);
    } else if (!skipped) {
        problems.push("the log doesn't say the pass was skipped");
    }
    return problems;
}

function runFolders(out: string, config: Config): string[] {
    const root = path.join(out, config);
    if (!fs.existsSync(root)) return [];
    return fs
        .readdirSync(root)
        .filter((d) => /^run\d+$/.test(d))
        .sort((p, q) => Number(p.slice(3)) - Number(q.slice(3)))
        .map((d) => path.join(root, d));
}

function compareFolder(out: string, limits: Limits, threshold: number, grow: number): number {
    const runs = Object.fromEntries(CONFIGS.map((c) => [c, runFolders(out, c)])) as Record<Config, string[]>;
    if (!runs.off.length || !runs.on.length) exitWith(`${out} needs off/run<k> and on/run<k> folders`);
    const names = fs
        .readdirSync(runs.off[0]!)
        .filter((f) => f.endsWith(".png") && fs.existsSync(path.join(runs.on[0]!, f)))
        .sort();
    const lines: string[] = [
        `expect ${limits.expect}, strength ${limits.strength}, threshold ${threshold}, mask grown by ${grow}`,
    ];
    const verdicts: SpotVerdict[] = [];
    // (a spot's group: its name up to the first "-"; the group's `on` log says whether the bake showed a sun)
    const sunless = (spot: string): boolean => {
        const log = path.join(runs.on[0]!, `${spot.split("-")[0]}.log`);
        return fs.existsSync(log) && fs.readFileSync(log, "utf8").includes(NO_BAKED_SUN);
    };
    for (const name of names) {
        const load = (config: Config): Image[] =>
            runs[config]
                .map((f) => path.join(f, name))
                .filter((f) => fs.existsSync(f))
                .map(readPicture);
        const off = load("off");
        const on = load("on");
        const baseline = load("baseline");
        const mask = noiseOf(baseline.length ? [off, on, baseline] : [off, on], threshold, grow);
        const verdict = judgeSpot(
            name.replace(/\.png$/i, ""),
            shadowDifference(off[0]!, on[0]!, mask, threshold),
            baseline.length ? shadowDifference(baseline[0]!, off[0]!, mask, threshold) : null,
            limits,
            sunless(name),
        );
        verdicts.push(verdict);
        const d = verdict.difference;
        lines.push(
            `${verdict.name}: compared ${d.pixels} pixels; darkened ${pct(share(d.darkened, d))} (lower half ${pct(d.darkened ? d.lowerHalf / d.darkened : 0)}), brighter ${pct(share(d.brighter, d))}, median ratio ${d.medianRatio?.toFixed(3) ?? "-"}; baked-bright darkened ${pct(d.brightDarkened / Math.max(1, d.bright))}, baked-dark ${pct(d.darkDarkened / Math.max(1, d.dark))} (split at luma ${d.split})` +
                (verdict.baseline
                    ? `; off vs baseline changed ${pct(share(verdict.baseline.brighter + verdict.baseline.darkened, verdict.baseline))}`
                    : "") +
                (verdict.problems.length ? `\n  FAIL ${verdict.problems.join("; ")}` : ""),
        );
    }
    let failed = verdicts.some((v) => v.problems.length > 0) || names.length === 0;
    if (names.length === 0) lines.push("FAIL no spot pictured in both configurations");
    if (limits.expect === "shadows" && !anyShadow(verdicts, limits)) {
        failed = true;
        lines.push(`FAIL no spot darkened by ${pct(limits.minDarkenedSomewhere)} or more: no shadows drawn`);
    }
    if (limits.expect === "shadows") {
        const lined = darkOverBright(verdicts);
        const ok = lined === null || lined >= limits.minDarkOverBright;
        lines.push(
            `${ok ? "" : "FAIL "}the sun against the bake: baked-dark pixels darkened ${lined === null ? "-" : lined.toFixed(2)} times as often as baked-bright ones (${limits.minDarkOverBright} wanted)`,
        );
        failed ||= !ok;
    }
    for (const config of CONFIGS)
        for (const folder of runs[config])
            for (const log of fs.readdirSync(folder).filter((f) => f.endsWith(".log"))) {
                const problems = logProblems(config, fs.readFileSync(path.join(folder, log), "utf8"), limits.expect);
                if (problems.length) {
                    failed = true;
                    lines.push(`FAIL ${config}/${path.basename(folder)}/${log}: ${problems.join("; ")}`);
                }
            }
    lines.push(failed ? "rt_shadows check: FAILED" : "rt_shadows check: passed");
    fs.writeFileSync(path.join(out, "report.txt"), `${lines.join("\n")}\n`);
    fs.writeFileSync(path.join(out, "report.json"), `${JSON.stringify({ limits, verdicts }, null, 1)}\n`);
    for (const l of lines) print(l);
    return failed ? 1 : 0;
}

// --- running -----------------------------------------------------------------------------------------------------------

function powershell(script: string, args: string[]): Promise<{ code: number; out: string }> {
    return new Promise((resolve) => {
        const child = spawn(
            "powershell.exe",
            ["-NoProfile", "-NonInteractive", "-File", path.join(HERE, script), ...args],
            {
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
        child.on("close", (code) => resolve({ code: code ?? -1, out }));
    });
}

async function runGroup(game: string, group: PlanGroup, config: Config, run: number, folder: string): Promise<string> {
    // (the group's spots only: its own commands and frame time are renderer_parity's)
    const spots: PlanGroup = {
        name: group.name,
        ...(group.level ? { level: group.level } : {}),
        spots: group.spots ?? [],
    };
    const commands = groupCommands(spots, config, run);
    for (const s of groupSpots(spots))
        fs.rmSync(path.join(game, "Screenshots", seriesName(config, run, s)), { recursive: true, force: true });
    clearAutomationFiles(game);
    const log = path.join(game, "thug2-decomp.log");
    fs.rmSync(log, { force: true });
    const args = ["-Game", game, "-Arch", "x64", "-Seconds", String(group.seconds ?? 240), "-HangSeconds", "120"];
    args.push("-Shot", group.name, "-ShotDir", path.join(folder, "window"));
    if (group.level) args.push("-Level", group.level);
    args.push("-Commands", commands.join(";"));
    const result = await powershell("run_game.ps1", args);
    fs.writeFileSync(path.join(folder, `${group.name}.run.txt`), result.out);
    fs.writeFileSync(path.join(folder, `${group.name}.log`), fs.existsSync(log) ? fs.readFileSync(log, "utf8") : "");
    const notes = [`exit ${result.code}`];
    for (const s of groupSpots(spots)) {
        const series = path.join(game, "Screenshots", seriesName(config, run, s));
        const frame = path.join(series, "frame_0001.png");
        if (fs.existsSync(frame)) fs.copyFileSync(frame, path.join(folder, `${s}.png`));
        else notes.push(`no picture ${s}`);
        fs.rmSync(series, { recursive: true, force: true });
    }
    return notes.join("; ");
}

interface RunOptions {
    game: string;
    out: string;
    build: string;
    baselineBuild: string | null;
    plan: string;
    groups: string[] | null;
    runs: number;
    owner: string;
    set: Record<string, string>;
}

async function runAll(options: RunOptions): Promise<number> {
    const { game, out } = options;
    if (!isWorkspace(game)) exitWith(`${game} isn't a workspace (C:\\Games\\THUG2-<n>): this tool runs only there`);
    const plan = readPlan(fs.readFileSync(options.plan, "utf8"));
    const groups = plan.groups.filter(
        (g) => g.level && g.spots?.length && (!options.groups || options.groups.includes(g.name)),
    );
    if (!groups.length) exitWith("no level group of the plan chosen");
    const configs: Config[] = options.baselineBuild ? ["baseline", "off", "on"] : ["off", "on"];
    fs.mkdirSync(out, { recursive: true });
    const journal = path.join(out, "run.txt");
    const note = (line: string): void => {
        print(line);
        fs.appendFileSync(journal, `${line}\n`);
    };
    const lock = await powershell("game_lock.ps1", ["-Action", "acquire", "-Owner", options.owner, "-Game", game]);
    note(lock.out.trim());
    if (lock.code !== 0) return 1;
    try {
        const ini = path.join(game, "thug2-decomp.ini");
        ensureWorkspaceMuted(game);
        const base = fs.readFileSync(ini, "utf8");
        let deployed: string | null = null;
        for (let run = 1; run <= options.runs; run++) {
            for (const config of configs) {
                const build = config === "baseline" ? options.baselineBuild! : options.build;
                if (build !== deployed) {
                    const deploy = await powershell("deploy_build.ps1", [
                        "-Game",
                        game,
                        "-Arch",
                        "x64",
                        "-BuildDir",
                        build,
                        "-NoEditorTools",
                    ]);
                    note(deploy.out.trim());
                    if (deploy.code !== 0) return 1;
                    deployed = build;
                }
                const values = [plan.ini ?? {}, configValues(config), config === "baseline" ? {} : options.set];
                fs.writeFileSync(ini, iniFor(base, values, "vulkan"), "utf8");
                ensureWorkspaceMuted(game);
                const folder = path.join(out, config, `run${run}`);
                fs.mkdirSync(path.join(folder, "window"), { recursive: true });
                for (const group of groups) {
                    note(`${config} run ${run} ${group.name}: ${await runGroup(game, group, config, run, folder)}`);
                }
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
            "baseline-build": { type: "string" },
            plan: { type: "string", default: path.join(HERE, "rt_shadows.json") },
            groups: { type: "string" },
            runs: { type: "string", default: "2" },
            owner: { type: "string", default: "rt-shadows-check" },
            set: { type: "string", multiple: true },
            expect: { type: "string", default: "shadows" },
            strength: { type: "string", default: "0.5" },
            threshold: { type: "string", default: "8" },
            grow: { type: "string", default: "1" },
        },
        allowPositionals: true,
    });
    const [command, target] = positionals;
    if (values.expect !== "shadows" && values.expect !== "none") exitWith("--expect shadows | none");
    const set: Record<string, string> = {};
    for (const s of values.set ?? []) {
        const eq = s.indexOf("=");
        if (eq <= 0) exitWith(`--set Section.key=value (${s})`);
        set[s.slice(0, eq)] = s.slice(eq + 1);
    }
    const strength = Number(set["Graphics.rt_shadow_strength"] ?? values.strength);
    const limits: Limits = { ...LIMITS, expect: values.expect, strength };
    const threshold = Number(values.threshold);
    const grow = Number(values.grow);
    if (command === "compare" && target && positionals.length === 2)
        return compareFolder(path.resolve(target), limits, threshold, grow);
    if (command === "run" && positionals.length === 1) {
        if (!values.game || !values.out || !values.build) exitWith("run needs --game, --out and --build");
        const out = path.resolve(values.out);
        const code = await runAll({
            game: path.resolve(values.game),
            out,
            build: path.resolve(values.build),
            baselineBuild: values["baseline-build"] ? path.resolve(values["baseline-build"]) : null,
            plan: path.resolve(values.plan),
            groups: values.groups ? values.groups.split(",") : null,
            runs: Number(values.runs),
            owner: values.owner,
            set,
        });
        return code !== 0 ? code : compareFolder(out, limits, threshold, grow);
    }
    print(USAGE);
    return 2;
}

if (isMain(import.meta.url)) await runMain(() => main());
