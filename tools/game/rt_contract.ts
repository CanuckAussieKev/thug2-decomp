// NOT ORIGINAL - ray tracing's contract checked in the game (src/devtools/rt_check.h: [debug] rt_contract_check=1 - every
// call render_pc makes to the ray-tracing backend checked, `rt-check:` lines in the log): levels played with each
// renderer, a level change and the skater riding, then the log read for violations.
//
//   node tools/game/rt_contract.ts run --game C:\Games\THUG2-<n> [--build <cmake build folder>] [--arch x64]
//        [--renderers vulkan,d3d9] [--scenarios levels,anteara] [--owner <name>] [--out <folder>]
//   node tools/game/rt_contract.ts read <thug2-decomp.log>
//
// run: under the workspace's lock (game_lock.ps1; released at the end - the exe and the ini put back), --build deployed
// (deploy_build.ps1, no editor tools), then for each renderer and scenario the ini written ([Graphics] renderer=,
// [debug] rt_contract_check=1, the scenario's values) and the game run (run_game.ps1 -Level, automation commands):
//   - levels: BO (the skater pushed off and riding), then LA, SE and the THPS3 port foun loaded in turn (the level
//     changes), riding again in foun; `console rt summary`, `console rt dump rt-frame-<renderer>.json`;
//   - anteara: [features] anteara_mod=1, BO, riding.
// Each run's log (and the frame dump) is kept in --out (default out\rt-contract), and read: the check must have been on,
// counted frames and draws, and found no violation; a crash or hang fails the run. Exit code 0 when every run passed.
// Only a workspace (C:\Games\THUG2-<n>) - never the main install. Muted (workspace_ini.ts).
//
// read: one log read as above (its summary lines, its violations) - for a run made by hand.

import { spawn } from "node:child_process";
import * as fs from "node:fs";
import * as path from "node:path";
import { fileURLToPath } from "node:url";
import { exitWith, isMain, parseCli, print, runMain } from "thug2-level-converter-core/lib/cli.ts";
import { clearAutomationFiles } from "./edit_level.ts";
import { ensureWorkspaceMuted, isWorkspace, setIniValue } from "./workspace_ini.ts";

const HERE = path.dirname(fileURLToPath(import.meta.url));
const RENDERERS = ["vulkan", "d3d9"] as const;

const USAGE = `usage:
  node tools/game/rt_contract.ts run --game C:\\Games\\THUG2-<n> [--build <folder>] [--arch x64] [--renderers vulkan,d3d9]
       [--scenarios levels,anteara] [--owner <name>] [--out <folder>]
  node tools/game/rt_contract.ts read <thug2-decomp.log>`;

export interface Scenario {
    name: string;
    level: string;
    ini: Record<string, string>; // "Section.key": value
    commands: (renderer: string) => string[];
    seconds: number;
}

// Riding: stood on the board at a spot, pushed off, a few seconds.
function ride(at: string): string[] {
    return [`teleport ${at} board`, "press up 60", "wait 240"];
}

export const SCENARIOS: readonly Scenario[] = [
    {
        name: "levels",
        level: "BO",
        ini: {},
        seconds: 600,
        commands: (renderer) => [
            "waitfor level BO 180",
            ...ride("1029.6 286.2 -2171.4 53.2"),
            "console rt summary",
            "level LA",
            "waitfor level LA 180",
            "wait 180",
            "level SE",
            "waitfor level SE 180",
            "wait 180",
            "level foun",
            "waitfor level foun 240",
            ...ride("-3568.4 -202 -959.1 105"),
            "console rt summary",
            `console rt dump rt-frame-${renderer}.json`,
            "quit",
        ],
    },
    {
        name: "anteara",
        level: "BO",
        ini: { "features.anteara_mod": "1" },
        seconds: 300,
        commands: () => ["waitfor level BO 180", ...ride("1029.6 286.2 -2171.4 53.2"), "console rt summary", "quit"],
    },
];

export interface LogResult {
    on: boolean; // the check said it was on
    summaries: string[]; // the summary lines
    frames: number; // the last summary's
    draws: number; // draws a frame, the last summary's
    violations: number; // the last summary's count (or the VIOLATION lines', if more)
    violationLines: string[]; // the first ones logged
    crash: string | null;
}

const BAD = /^(EXCEPTION|RUN-TIME CHECK|DEBUG CRT|PURE VIRTUAL|INVALID PARAMETER|ABORT|BUFFER OVERRUN)/m;

function field(line: string, name: string): number {
    const m = new RegExp(`\\b${name}=(\\d+)`).exec(line);
    return m ? Number(m[1]) : 0;
}

// A log's rt-check lines.
export function readLog(text: string): LogResult {
    const lines = text.split(/\r?\n/);
    const summaries = lines.filter((l) => l.includes("rt-check: summary"));
    const violationLines = lines.filter((l) => l.includes("rt-check: VIOLATION"));
    const last = summaries[summaries.length - 1] ?? "";
    const crash = BAD.exec(text);
    return {
        on: lines.some((l) => l.includes("rt-check: the contract check")),
        summaries,
        frames: field(last, "frames"),
        draws: field(last, "draws/frame"),
        violations: Math.max(field(last, "violations"), violationLines.length),
        violationLines: violationLines.slice(0, 20),
        crash: crash ? crash[0] : null,
    };
}

// What is wrong with a run ("" when it passed).
export function verdict(result: LogResult): string {
    if (result.crash) return `the game failed: ${result.crash}`;
    if (!result.on) return "the check was never on (no rt-check line)";
    if (!result.summaries.length || result.frames === 0) return "no frame was checked";
    if (result.draws === 0) return "no draw was reported";
    if (result.violations > 0) return `${result.violations} violation(s)`;
    return "";
}

export function iniFor(base: string, renderer: string, scenario: Scenario): string {
    let text = setIniValue(base, "Graphics", "renderer", renderer);
    text = setIniValue(text, "debug", "rt_contract_check", "1");
    for (const [where, value] of Object.entries(scenario.ini)) {
        const dot = where.indexOf(".");
        text = setIniValue(text, where.slice(0, dot), where.slice(dot + 1), value);
    }
    return text;
}

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

function report(name: string, result: LogResult): string {
    const problem = verdict(result);
    const head = `${name}: ${problem ? `FAILED - ${problem}` : "passed"} (${result.frames} frames, ${result.draws} draws a frame)`;
    return [head, ...result.violationLines.map((l) => `    ${l.trim()}`)].join("\n");
}

async function run(options: {
    game: string;
    build: string | null;
    arch: string;
    renderers: string[];
    scenarios: Scenario[];
    owner: string;
    out: string;
}): Promise<number> {
    const { game, out } = options;
    if (!isWorkspace(game)) exitWith(`${game} isn't a workspace (C:\\Games\\THUG2-<n>): this tool runs only there`);
    fs.mkdirSync(out, { recursive: true });
    const lock = await powershell("game_lock.ps1", ["-Action", "acquire", "-Owner", options.owner, "-Game", game]);
    print(lock.out.trim());
    if (lock.code !== 0) return 1;
    let failed = 0;
    try {
        if (options.build) {
            const deploy = await powershell("deploy_build.ps1", [
                "-Game",
                game,
                "-Arch",
                options.arch,
                "-BuildDir",
                options.build,
                "-NoEditorTools",
            ]);
            print(deploy.out.trim());
            if (deploy.code !== 0) return 1;
        }
        const ini = path.join(game, "thug2-decomp.ini");
        ensureWorkspaceMuted(game);
        const base = fs.readFileSync(ini, "utf8");
        for (const renderer of options.renderers) {
            for (const scenario of options.scenarios) {
                fs.writeFileSync(ini, iniFor(base, renderer, scenario), "utf8");
                ensureWorkspaceMuted(game);
                clearAutomationFiles(game);
                const log = path.join(game, "thug2-decomp.log");
                fs.rmSync(log, { force: true });
                const name = `${renderer}-${scenario.name}`;
                const result = await powershell("run_game.ps1", [
                    "-Game",
                    game,
                    "-Seconds",
                    String(scenario.seconds),
                    "-HangSeconds",
                    "120",
                    "-Level",
                    scenario.level,
                    "-Commands",
                    scenario.commands(renderer).join(";"),
                    "-ShotDir",
                    path.join(out, "window"),
                    "-Shot",
                    name,
                ]);
                fs.writeFileSync(path.join(out, `${name}.run.txt`), result.out);
                const text = fs.existsSync(log) ? fs.readFileSync(log, "utf8") : "";
                fs.writeFileSync(path.join(out, `${name}.log`), text);
                const dump = path.join(game, `rt-frame-${renderer}.json`);
                if (fs.existsSync(dump)) fs.renameSync(dump, path.join(out, `rt-frame-${renderer}.json`));
                const read = readLog(text);
                if (verdict(read)) ++failed;
                print(report(name, read));
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
        print(release.out.trim());
    }
    print(failed ? `${failed} run(s) failed` : "every run passed: 0 violations");
    return failed ? 1 : 0;
}

async function main(): Promise<number> {
    const { values, positionals } = parseCli(USAGE, {
        options: {
            game: { type: "string" },
            build: { type: "string" },
            arch: { type: "string", default: "x64" },
            renderers: { type: "string", default: RENDERERS.join(",") },
            scenarios: { type: "string", default: SCENARIOS.map((s) => s.name).join(",") },
            owner: { type: "string", default: "rt-contract" },
            out: { type: "string", default: path.join("out", "rt-contract") },
        },
        allowPositionals: true,
    });
    const [command, target] = positionals;
    if (command === "read" && target && positionals.length === 2) {
        const result = readLog(fs.readFileSync(target, "utf8"));
        print(report(path.basename(target), result));
        for (const s of result.summaries.slice(-1)) print(s);
        return verdict(result) ? 1 : 0;
    }
    if (command === "run" && positionals.length === 1) {
        if (!values.game) exitWith("run needs --game <workspace>");
        const renderers = values.renderers.split(",").filter((r) => r);
        for (const r of renderers) if (!(RENDERERS as readonly string[]).includes(r)) exitWith(`no renderer ${r}`);
        const names = values.scenarios.split(",").filter((s) => s);
        const scenarios = names.map((n) => SCENARIOS.find((s) => s.name === n) ?? exitWith(`no scenario ${n}`));
        return run({
            game: path.resolve(values.game),
            build: values.build ? path.resolve(values.build) : null,
            arch: values.arch,
            renderers,
            scenarios,
            owner: values.owner,
            out: path.resolve(values.out),
        });
    }
    print(USAGE);
    return 2;
}

if (isMain(import.meta.url)) await runMain(() => main());
