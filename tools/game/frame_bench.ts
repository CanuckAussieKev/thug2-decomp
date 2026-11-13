// NOT ORIGINAL - frame times measured in the game: a fixed scenario per level run with `[debug] frame_profile`
// (src/platform/frame_profile.h) and reported - the drawn frames a second, their spacing (average, 99th percentile -
// the "1% low" - and the hitches over 33 ms), each loop's work and where it goes (the main loop's phases, the render's
// parts, the device's own time for the frame), and with --samples the functions the main thread spends its time in.
//
//   node tools/game/frame_bench.ts run --game C:\Games\THUG2-<n> --out <folder> [--build <x64 build folder>]
//        [--settings <ini>] [--levels BO,AU,z_dt,sch,foun] [--seconds 30] [--runs 1] [--samples] [--no-lock]
//        [--owner <name>] [--set Section.key=value]...
//   node tools/game/frame_bench.ts report <folder> [--skip 120]
//   node tools/game/frame_bench.ts compare <before folder> <after folder>
//
// run: under the workspace's lock (game_lock.ps1; --no-lock: the caller holds it), --build deployed (deploy_build.ps1
// -Arch x64; without it the folder's exe as it is), the ini's [Graphics] from --settings (default the main install's,
// read only - every key but the window's) and --set's, `[debug] frame_profile=1` (2 with --samples). Each level is
// started with -level and driven by the automation channel - pushing forward, an ollie and a turn every five seconds
// for --seconds - then quit; the logic is a fixed step a loop, so a run is the same each time. Gathered into
// <out>/<level>/run<k>/: frames.jsonl (a line a loop), game.log, samples.txt (+ samples-named.txt).
// report: per level and run, the loops from --skip after the last loading screen to the end of the scenario.
// Workspaces only; muted (workspace_ini.ts).

import { spawn } from "node:child_process";
import * as fs from "node:fs";
import * as path from "node:path";
import { fileURLToPath } from "node:url";
import { exitWith, isMain, parseCli, print, runMain } from "thug2-level-converter-core/lib/cli.ts";
import { clearAutomationFiles } from "./edit_level.ts";
import { graphicsOf } from "./first_frame_check.ts";
import { iniFor } from "./renderer_parity.ts";
import { ensureWorkspaceMuted, isWorkspace } from "./workspace_ini.ts";

const USAGE = `usage: frame_bench.ts run --game <workspace> --out <folder> [--build <folder>] [--settings <ini>]
                      [--levels BO,AU,z_dt,sch,foun] [--seconds 30] [--runs 1] [--samples] [--no-lock]
                      [--owner name] [--set S.key=v]...
       frame_bench.ts report <folder> [--skip 120]
       frame_bench.ts compare <before folder> <after folder> [--skip 120]

Frame times per level in the game with [debug] frame_profile (tools/game/README.md).`;

const HERE = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(HERE, "..", "..");
const MAIN_INI = "C:\\Games\\THUG2\\Game\\thug2-decomp.ini";
export const LEVELS = ["BO", "AU", "z_dt", "sch", "foun"];
export const HITCH_MS = 33.4;
const STEP_FRAMES = 300; // a scenario step: five seconds

// The scenario's commands for a level: up, then `seconds` of skating.
export function scenario(level: string, seconds: number): string[] {
    const out = [`waitfor level ${level} 300`, "wait 60"];
    for (let s = 0; s < seconds * 60; s += STEP_FRAMES) {
        out.push("hold up 280", "wait 120", "press x 4", "wait 100", "hold left 20", "wait 73");
    }
    out.push("quit");
    return out;
}

// --- reading a run ---------------------------------------------------------------------------------------------------

export interface Loop {
    frame: number;
    drawn: boolean;
    loading: boolean;
    ms: number; // since the loop before ended
    work: number; // less the pacing wait
    phases: number[];
}

export interface Frames {
    phases: string[];
    loops: Loop[];
}

export function parseFrames(text: string): Frames {
    let phases: string[] = [];
    const loops: Loop[] = [];
    for (const line of text.split(/\r?\n/)) {
        if (!line.trim()) continue;
        let row: unknown;
        try {
            row = JSON.parse(line);
        } catch {
            continue; // (the last line of a game closed mid-write)
        }
        const r = row as {
            phases?: string[];
            f?: number;
            d?: number;
            l?: number;
            t?: number;
            w?: number;
            p?: number[];
        };
        if (r.phases) {
            phases = r.phases;
            continue;
        }
        if (r.f === undefined) continue;
        loops.push({
            frame: r.f,
            drawn: r.d === 1,
            loading: r.l === 1,
            ms: r.t ?? 0,
            work: r.w ?? 0,
            phases: r.p ?? [],
        });
    }
    return { phases, loops };
}

export function percentile(values: readonly number[], p: number): number {
    if (!values.length) return 0;
    const sorted = [...values].sort((a, b) => a - b);
    return sorted[Math.min(sorted.length - 1, Math.floor(p * (sorted.length - 1)))]!;
}

const mean = (values: readonly number[]): number =>
    values.length ? values.reduce((a, b) => a + b, 0) / values.length : 0;

export interface Summary {
    loops: number;
    drawn: number;
    seconds: number;
    fps: number; // drawn frames a second
    interval: number; // drawn frames' spacing, ms
    interval99: number;
    worst: number;
    hitches: number; // drawn frames more than HITCH_MS apart
    work: number; // a drawn loop's work, ms
    work99: number;
    drawnPhases: Record<string, number>; // ms, a drawn loop's average
    skippedPhases: Record<string, number>;
}

// The loops measured: from `skip` after the last loading screen (the level's) to the end (the scenario's quit).
export function measured(frames: Frames, skip: number): Loop[] {
    let last = -1;
    frames.loops.forEach((loop, index) => {
        if (loop.loading) last = index;
    });
    // (the closing frames: the quit's last few)
    return frames.loops.slice(last + 1 + skip, Math.max(last + 1 + skip, frames.loops.length - 5));
}

export function summarise(frames: Frames, skip: number): Summary {
    const loops = measured(frames, skip);
    const intervals: number[] = [];
    const work: number[] = [];
    let since = 0;
    let first = true;
    const drawnSums = new Array<number>(frames.phases.length).fill(0);
    const skippedSums = new Array<number>(frames.phases.length).fill(0);
    let drawn = 0;
    let seconds = 0;
    for (const loop of loops) {
        since += loop.ms;
        seconds += loop.ms / 1000;
        const sums = loop.drawn ? drawnSums : skippedSums;
        loop.phases.forEach((ms, i) => {
            if (i < sums.length) sums[i] = sums[i]! + ms;
        });
        if (!loop.drawn) continue;
        drawn++;
        work.push(loop.work);
        if (!first) intervals.push(since);
        first = false;
        since = 0;
    }
    const skipped = loops.length - drawn;
    const phasesOf = (sums: number[], count: number): Record<string, number> => {
        const out: Record<string, number> = {};
        frames.phases.forEach((name, i) => {
            out[name] = count ? sums[i]! / count : 0;
        });
        return out;
    };
    return {
        loops: loops.length,
        drawn,
        seconds,
        fps: seconds > 0 ? drawn / seconds : 0,
        interval: mean(intervals),
        interval99: percentile(intervals, 0.99),
        worst: intervals.length ? Math.max(...intervals) : 0,
        hitches: intervals.filter((ms) => ms > HITCH_MS).length,
        work: mean(work),
        work99: percentile(work, 0.99),
        drawnPhases: phasesOf(drawnSums, drawn),
        skippedPhases: phasesOf(skippedSums, skipped),
    };
}

const f1 = (n: number): string => n.toFixed(1);
const f2 = (n: number): string => n.toFixed(2);

export function summaryText(name: string, s: Summary): string {
    const phases = Object.entries(s.drawnPhases)
        .filter(([, ms]) => ms >= 0.05)
        .map(([phase, ms]) => `${phase} ${f2(ms)}`)
        .join(", ");
    return [
        `${name}: ${s.loops} loops (${s.drawn} drawn) in ${f1(s.seconds)} s - ${f1(s.fps)} fps; frames ${f2(s.interval)} ms apart ` +
            `(99%: ${f2(s.interval99)}, worst ${f1(s.worst)}, ${s.hitches} over ${HITCH_MS} ms); work ${f2(s.work)} ms (99%: ${f2(s.work99)})`,
        `    a drawn loop: ${phases || "-"}`,
    ].join("\n");
}

// The runs of a folder: <folder>/<level>/run<k>/frames.jsonl.
export function runsOf(folder: string): { level: string; run: string; file: string }[] {
    const out: { level: string; run: string; file: string }[] = [];
    if (!fs.existsSync(folder)) return out;
    for (const level of fs.readdirSync(folder).sort()) {
        const dir = path.join(folder, level);
        if (!fs.statSync(dir).isDirectory()) continue;
        for (const run of fs.readdirSync(dir).sort()) {
            const file = path.join(dir, run, "frames.jsonl");
            if (fs.existsSync(file)) out.push({ level, run, file });
        }
    }
    return out;
}

function summariesOf(folder: string, skip: number): Map<string, Summary[]> {
    const out = new Map<string, Summary[]>();
    for (const { level, file } of runsOf(folder)) {
        const s = summarise(parseFrames(fs.readFileSync(file, "utf8")), skip);
        if (!s.loops) continue;
        out.set(level, [...(out.get(level) ?? []), s]);
    }
    return out;
}

// The run with the median average spacing (the runs' noise: the others are listed in the report).
function median(summaries: readonly Summary[]): Summary {
    const sorted = [...summaries].sort((a, b) => a.work - b.work);
    return sorted[Math.floor((sorted.length - 1) / 2)]!;
}

export function reportFolder(folder: string, skip: number): string {
    const lines: string[] = [];
    for (const { level, run, file } of runsOf(folder)) {
        const s = summarise(parseFrames(fs.readFileSync(file, "utf8")), skip);
        lines.push(summaryText(`${level} ${run}`, s));
    }
    const text = lines.join("\n");
    fs.writeFileSync(path.join(folder, "report.txt"), `${text}\n`);
    return text;
}

export function compareFolders(before: string, after: string, skip: number): string {
    const a = summariesOf(before, skip);
    const b = summariesOf(after, skip);
    const lines = [
        "level: fps, frame spacing avg / 99% (ms), work avg / 99% (ms), hitches - before -> after (median run)",
    ];
    for (const level of [...new Set([...a.keys(), ...b.keys()])]) {
        const x = a.get(level);
        const y = b.get(level);
        const text = (s: Summary | undefined): string =>
            s
                ? `${f1(s.fps)} fps, ${f2(s.interval)} / ${f2(s.interval99)}, ${f2(s.work)} / ${f2(s.work99)}, ${s.hitches}`
                : "-";
        lines.push(`${level}: ${text(x && median(x))} -> ${text(y && median(y))}`);
    }
    return lines.join("\n");
}

// --- running ---------------------------------------------------------------------------------------------------------

function powershell(script: string, args: string[]): Promise<{ code: number; out: string }> {
    return new Promise((resolve) => {
        const child = spawn(
            "powershell",
            ["-NoProfile", "-ExecutionPolicy", "Bypass", "-File", path.join(HERE, script), ...args],
            {
                windowsHide: true,
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

function python(args: string[]): Promise<{ code: number; out: string }> {
    return new Promise((resolve) => {
        const child = spawn("python", ["-I", ...args], { windowsHide: true });
        let text = "";
        child.stdout.on("data", (d: Buffer) => {
            text += d.toString("utf8");
        });
        child.on("close", (code) => resolve({ code: code ?? -1, out: text }));
    });
}

interface Sample {
    phase: string;
    module: string;
    rva: number;
    own: number;
    inclusive: number;
    exeOwn: number;
}

export function parseSamples(text: string): { total: number; samples: Sample[] } {
    let total = 0;
    const samples: Sample[] = [];
    for (const line of text.split(/\r?\n/)) {
        const head = /^# (\d+) samples/.exec(line);
        if (head) {
            total = Number(head[1]);
            continue;
        }
        const parts = line.split("\t");
        if (parts.length < 5) continue;
        samples.push({
            phase: parts[0]!,
            module: parts[1]!,
            rva: Number.parseInt(parts[2]!, 16),
            own: Number(parts[3]),
            inclusive: Number(parts[4]),
            exeOwn: Number(parts[5] ?? 0),
        });
    }
    return { total, samples };
}

interface FunctionSamples {
    module: string;
    rva: number;
    own: number;
    inclusive: number;
    exeOwn: number;
}

// The samples of the phases `wanted` picks, by function.
function byFunctionOf(samples: readonly Sample[], wanted: (phase: string) => boolean): FunctionSamples[] {
    const byFunction = new Map<string, FunctionSamples>();
    for (const s of samples) {
        if (!wanted(s.phase)) continue;
        const key = `${s.module}:${s.rva}`;
        const f = byFunction.get(key) ?? { module: s.module, rva: s.rva, own: 0, inclusive: 0, exeOwn: 0 };
        f.own += s.own;
        f.inclusive += s.inclusive;
        f.exeOwn += s.exeOwn;
        byFunction.set(key, f);
    }
    return [...byFunction.values()];
}

const isHitch = (phase: string): boolean => phase.startsWith("hitch ");

// The samples by function, named with the exe's PDB: the phases' shares, then the top `count` by own time, by own time
// with the system's and drivers' counted to the exe's function that called them, and by inclusive time - over every
// loop measured (and the busiest phases alone with `phaseDetail`), then over the hitches' loops alone (work past 33 ms).
export async function nameSamples(file: string, exe: string, count: number, phaseDetail = false): Promise<string> {
    const { total, samples } = parseSamples(fs.readFileSync(file, "utf8"));
    const byPhase = new Map<string, number>();
    for (const s of samples) byPhase.set(s.phase, (byPhase.get(s.phase) ?? 0) + s.own);
    const phases = [...byPhase.entries()].filter(([p]) => !isHitch(p)).sort((a, b) => b[1] - a[1]);
    const hitchPhases = [...byPhase.entries()].filter(([p]) => isHitch(p)).sort((a, b) => b[1] - a[1]);
    const hitchSamples = hitchPhases.reduce((sum, [, n]) => sum + n, 0);
    const groups = [{ title: "every loop", functions: byFunctionOf(samples, (p) => !isHitch(p)), of: total }];
    if (phaseDetail) {
        for (const [phase] of phases.slice(0, 8))
            groups.push({ title: `phase ${phase}`, functions: byFunctionOf(samples, (p) => p === phase), of: total });
    }
    if (hitchSamples)
        groups.push({ title: "the hitches", functions: byFunctionOf(samples, isHitch), of: hitchSamples });
    const exeName = path.basename(exe).toLowerCase();
    const tops = groups.map((g) => ({
        ...g,
        own: [...g.functions].sort((a, b) => b.own - a.own).slice(0, count),
        exe: [...g.functions].sort((a, b) => b.exeOwn - a.exeOwn).slice(0, count),
        incl: [...g.functions].sort((a, b) => b.inclusive - a.inclusive).slice(0, count),
    }));
    const rvas = new Set<number>();
    for (const t of tops)
        for (const f of [...t.own, ...t.exe, ...t.incl]) if (f.module.toLowerCase() === exeName) rvas.add(f.rva);
    const names = new Map<number, string>();
    const list = [...rvas];
    for (let at = 0; at < list.length; at += 200) {
        const resolved = await python([
            path.join(ROOT, "tools", "standalone", "addr2sym.py"),
            exe,
            ...list.slice(at, at + 200).map((r) => `0x${r.toString(16)}`),
        ]);
        for (const line of resolved.out.split(/\r?\n/)) {
            // (the name, without its source file and line)
            const m = /^0x([0-9a-fA-F]+) (.*?)(?: [A-Za-z]:[\\/].*)?$/.exec(line.trim());
            if (m) names.set(Number.parseInt(m[1]!, 16), m[2]!.replace(/\+0x0$/, ""));
        }
    }
    const name = (f: { module: string; rva: number }): string =>
        f.module.toLowerCase() === exeName
            ? (names.get(f.rva) ?? `0x${f.rva.toString(16)}`)
            : `${f.module}+0x${f.rva.toString(16)}`;
    const pct = (n: number, of: number): string => `${((100 * n) / Math.max(1, of)).toFixed(1)}%`.padStart(6);
    const lines = [`${total} samples (${hitchSamples} of them in hitches)`, "by phase (own samples):"];
    for (const [phase, n] of phases) lines.push(`  ${pct(n, total)} ${phase}`);
    if (hitchSamples) {
        lines.push("the hitches by phase:");
        for (const [phase, n] of hitchPhases) lines.push(`  ${pct(n, hitchSamples)} ${phase.slice("hitch ".length)}`);
    }
    for (const t of tops) {
        lines.push(`== ${t.title}: own`);
        for (const f of t.own) lines.push(`  ${pct(f.own, t.of)} ${name(f)}`);
        lines.push(`== ${t.title}: own, the system's and drivers' time counted to the exe's function that called them`);
        for (const f of t.exe) lines.push(`  ${pct(f.exeOwn, t.of)} ${name(f)}`);
        lines.push(`== ${t.title}: inclusive`);
        for (const f of t.incl) lines.push(`  ${pct(f.inclusive, t.of)} ${name(f)}`);
    }
    return lines.join("\n");
}

interface RunOptions {
    game: string;
    out: string;
    build: string | null;
    settings: string;
    levels: string[];
    seconds: number;
    runs: number;
    samples: boolean;
    lock: boolean;
    owner: string;
    set: Record<string, string>;
    skip: number;
}

async function runLevel(options: RunOptions, level: string, folder: string): Promise<string> {
    const { game } = options;
    clearAutomationFiles(game);
    const log = path.join(game, "thug2-decomp.log");
    const frames = path.join(game, "thug2-decomp-frames.jsonl");
    const samples = path.join(game, "thug2-decomp-samples.txt");
    for (const file of [log, frames, samples]) fs.rmSync(file, { force: true });
    const args = ["-Game", game, "-Arch", "x64", "-Seconds", String(options.seconds + 400), "-HangSeconds", "120"];
    args.push("-Shot", level, "-ShotDir", folder, "-Level", level);
    args.push("-Commands", scenario(level, options.seconds).join(";"));
    const result = await powershell("run_game.ps1", args);
    fs.mkdirSync(folder, { recursive: true });
    fs.writeFileSync(path.join(folder, "run.txt"), result.out);
    for (const [from, to] of [
        [log, "game.log"],
        [frames, "frames.jsonl"],
        [samples, "samples.txt"],
    ] as const) {
        if (fs.existsSync(from)) fs.copyFileSync(from, path.join(folder, to));
    }
    if (!fs.existsSync(path.join(folder, "frames.jsonl"))) return `exit ${result.code}; no frames (frame_profile off?)`;
    const summary = summarise(parseFrames(fs.readFileSync(path.join(folder, "frames.jsonl"), "utf8")), options.skip);
    if (options.samples && fs.existsSync(path.join(folder, "samples.txt"))) {
        const named = await nameSamples(
            path.join(folder, "samples.txt"),
            path.join(game, "thug2-decomp.exe"),
            30,
            true,
        );
        fs.writeFileSync(path.join(folder, "samples-named.txt"), `${named}\n`);
    }
    return `exit ${result.code}\n${summaryText(level, summary)}`;
}

async function runAll(options: RunOptions): Promise<number> {
    const { game, out } = options;
    if (!isWorkspace(game)) exitWith(`${game} isn't a workspace (C:\\Games\\THUG2-<n>): this tool runs only there`);
    const graphics = graphicsOf(fs.readFileSync(options.settings, "utf8"));
    fs.mkdirSync(out, { recursive: true });
    const journal = path.join(out, "run.txt");
    const note = (line: string): void => {
        print(line);
        fs.appendFileSync(journal, `${line}\n`);
    };
    if (options.lock) {
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
    }
    const ini = path.join(game, "thug2-decomp.ini");
    const base = fs.readFileSync(ini, "utf8");
    try {
        if (options.build) {
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
        }
        const profile = { "debug.frame_profile": options.samples ? "2" : "1" };
        fs.writeFileSync(ini, iniFor(base, [graphics, options.set, profile], "vulkan"), "utf8");
        ensureWorkspaceMuted(game);
        for (const level of options.levels) {
            for (let run = 1; run <= options.runs; run++) {
                note(await runLevel(options, level, path.join(out, level, `run${run}`)));
            }
        }
    } finally {
        fs.writeFileSync(ini, base, "utf8");
        if (options.lock) {
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
    }
    reportFolder(out, options.skip);
    return 0;
}

async function main(): Promise<number> {
    const { values, positionals } = parseCli(USAGE, {
        options: {
            game: { type: "string" },
            out: { type: "string" },
            build: { type: "string" },
            settings: { type: "string", default: MAIN_INI },
            levels: { type: "string", default: LEVELS.join(",") },
            seconds: { type: "string", default: "30" },
            runs: { type: "string", default: "1" },
            samples: { type: "boolean", default: false },
            "no-lock": { type: "boolean", default: false },
            owner: { type: "string", default: "frame-bench" },
            set: { type: "string", multiple: true },
            skip: { type: "string", default: "120" },
        },
        allowPositionals: true,
    });
    const [command, first, second] = positionals;
    const skip = Number(values.skip);
    const set: Record<string, string> = {};
    for (const s of values.set ?? []) {
        const eq = s.indexOf("=");
        if (eq <= 0) exitWith(`--set Section.key=value (${s})`);
        set[s.slice(0, eq)] = s.slice(eq + 1);
    }
    if (command === "report" && first && positionals.length === 2) {
        print(reportFolder(path.resolve(first), skip));
        return 0;
    }
    if (command === "compare" && first && second && positionals.length === 3) {
        print(compareFolders(path.resolve(first), path.resolve(second), skip));
        return 0;
    }
    if (command === "run" && positionals.length === 1) {
        if (!values.game || !values.out) exitWith("run needs --game and --out");
        return runAll({
            game: path.resolve(values.game),
            out: path.resolve(values.out),
            build: values.build ? path.resolve(values.build) : null,
            settings: path.resolve(values.settings),
            levels: values.levels.split(","),
            seconds: Number(values.seconds),
            runs: Number(values.runs),
            samples: values.samples === true,
            lock: values["no-lock"] !== true,
            owner: values.owner,
            set,
            skip,
        });
    }
    print(USAGE);
    return 2;
}

if (isMain(import.meta.url)) await runMain(() => main());
