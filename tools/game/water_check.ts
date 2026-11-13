// NOT ORIGINAL - water shading ([Graphics] rt_water; docs/design/vulkan.md "Water") looked at in the game: the same
// spots with rt_water off and on, and what the water costs the device.
//
//   node tools/game/water_check.ts run --game C:\Games\THUG2-<n> --out <folder> --build <x64 build folder>
//        [--settings <ini>] [--levels BA,AU,BE,NO,SE] [--configs off,on] [--owner <name>] [--set Section.key=value]...
//        [--timed <frames>] [--spots <file.json>]
//
// Under the workspace's lock (game_lock.ps1; released at the end, which puts the folder's own build and ini back), the
// build deployed (deploy_build.ps1 -Arch x64), then for each configuration and level: the ini's [Graphics] the
// --settings ini's (default the main install's, read only - every key but the window's) with rt_water=0 (`off`) or 1
// (`on`), [debug] frame_profile=1 and rt_water_materials=1, --set's values over them; the game started straight into
// the level (run_game.ps1 -Level), each spot's camera held and one frame captured (`capture 1`), then the level's first
// spot held for kTimedFrames frames - the frame profile's "gpu" (the device's milliseconds a frame) over its last
// windows is the cost (--timed: how many frames, 0 none; --spots: a JSON list of {level, spots: [{name, camera}]} in
// place of the built-in spots). Pictures in <out>/<config>/<spot>.png, each level's log in <out>/<config>/<level>.log, and
// report.txt: per level the device's time off and on, and the log's rt_water lines.
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

const USAGE = `usage: water_check.ts run --game <workspace> --out <folder> --build <folder> [--settings <ini>]
                      [--levels BA,AU,BE,NO,SE] [--configs off,on] [--owner name] [--set S.key=v]...

The levels' water with rt_water off and on, and its cost (tools/game/README.md).`;

const HERE = path.dirname(fileURLToPath(import.meta.url));
const MAIN_INI = "C:\\Games\\THUG2\\Game\\thug2-decomp.ini";
export const kTimedFrames = 900;

export interface Spot {
    name: string;
    camera: string; // the automation channel's `camera x y z dx dy dz`
}
export interface LevelSpots {
    level: string;
    spots: readonly Spot[];
}
// The water of five levels (found with water_survey.ts: the materials' meshes): the open sea, harbours, a canal, a river,
// pools - and a shoreline close up.
export const LEVELS: readonly LevelSpots[] = [
    {
        level: "BA",
        spots: [
            { name: "BA-harbour", camera: "camera 3840 150 2300 0.0 -0.35 0.94" },
            { name: "BA-harbour-sun", camera: "camera 2891.8 116.4 3000.4 0.197 -0.179 0.964" },
            { name: "BA-close", camera: "camera 3000 40 3150 0.6 -0.5 0.62" },
            { name: "BA-sea", camera: "camera 6000 400 9000 0.55 -0.12 0.83" },
        ],
    },
    {
        level: "AU",
        spots: [
            { name: "AU-sea", camera: "camera -2599.5 247.8 7625.6 -0.286 -0.179 -0.941" },
            { name: "AU-beach", camera: "camera -2493.8 247.9 7732.8 0.074 -0.180 -0.981" },
            { name: "AU-shore", camera: "camera -2900 120 5600 -0.2 -0.4 -0.89" },
        ],
    },
    {
        level: "BE",
        spots: [
            { name: "BE-canal", camera: "camera 2000 200 -3300 0.0 -0.15 -0.99" },
            { name: "BE-canal-along", camera: "camera -4300 160 -4600 0.99 -0.1 0.0" },
        ],
    },
    {
        level: "NO",
        spots: [
            { name: "NO-water", camera: "camera 3300 120 -3000 0.0 -0.3 -0.95" },
            { name: "NO-river", camera: "camera 3300 120 -3000 0.95 -0.3 0.0" },
        ],
    },
    {
        level: "SE",
        spots: [{ name: "SE-pool", camera: "camera -350 -600 -350 0.3 -0.85 0.43" }],
    },
];

export const CONFIGS = ["off", "on"] as const;
export type Config = (typeof CONFIGS)[number];

export function configValues(config: Config, graphics: Record<string, string>): Record<string, string> {
    return {
        ...graphics,
        "Graphics.rt_water": config === "on" ? "1" : "0",
        "debug.frame_profile": "1",
        "debug.rt_water_materials": "1",
    };
}

export const seriesName = (config: Config, spot: string): string => `water-${config}-${spot}`;

export function levelCommands(level: LevelSpots, config: Config, timed = kTimedFrames): string[] {
    const commands = [`waitfor level ${level.level} 300`, "wait 120"];
    for (const spot of level.spots) {
        commands.push(spot.camera, "wait 90", `capture 1 ${seriesName(config, spot.name)}`, "wait 10");
    }
    const first = level.spots[0];
    if (first && timed > 0) commands.push(first.camera, `wait ${timed}`);
    return commands;
}

// The device's milliseconds a frame in the log's frame-profile windows ("a drawn loop: ... gpu 4.21 ...").
export function gpuTimes(log: string): number[] {
    const times: number[] = [];
    for (const line of log.split(/\r?\n/)) {
        if (!line.includes("frame-profile:   a drawn loop:")) continue;
        const found = /\bgpu ([\d.]+)/.exec(line);
        if (found?.[1]) times.push(Number(found[1]));
    }
    return times;
}

function powershell(script: string, args: string[]): Promise<{ code: number; out: string }> {
    return new Promise((resolve) => {
        const child = spawn("powershell.exe", ["-NoProfile", "-NonInteractive", "-File", path.join(HERE, script), ...args], {
            stdio: ["ignore", "pipe", "pipe"],
        });
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

async function runLevel(game: string, level: LevelSpots, config: Config, folder: string, timed: number): Promise<string> {
    clearAutomationFiles(game);
    const log = path.join(game, "thug2-decomp.log");
    fs.rmSync(log, { force: true });
    const args = ["-Game", game, "-Arch", "x64", "-Seconds", "600", "-HangSeconds", "120"];
    args.push("-Shot", `${config}-${level.level}`, "-ShotDir", path.join(folder, "window"), "-Level", level.level);
    args.push("-Commands", levelCommands(level, config, timed).join(";"));
    const result = await powershell("run_game.ps1", args);
    fs.mkdirSync(folder, { recursive: true });
    const text = fs.existsSync(log) ? fs.readFileSync(log, "utf8") : "";
    fs.writeFileSync(path.join(folder, `${level.level}.log`), text);
    fs.writeFileSync(path.join(folder, `${level.level}-run.txt`), result.out);
    let copied = 0;
    for (const spot of level.spots) {
        const series = path.join(game, "Screenshots", seriesName(config, spot.name));
        const file = path.join(series, "frame_0001.png");
        if (fs.existsSync(file)) {
            fs.copyFileSync(file, path.join(folder, `${spot.name}.png`));
            copied++;
        }
        fs.rmSync(series, { recursive: true, force: true });
    }
    const times = gpuTimes(text).slice(-2);
    const water = text.split(/\r?\n/).filter((l) => l.includes("rt_water:")).length;
    return `exit ${result.code}; ${copied} pictures; gpu ${times.map((t) => t.toFixed(2)).join(" / ") || "-"} ms; ${water} rt_water lines`;
}

interface RunOptions {
    game: string;
    out: string;
    build: string;
    settings: string;
    levels: string[] | null;
    configs: Config[];
    owner: string;
    set: Record<string, string>;
    timed: number;
    spots: readonly LevelSpots[];
}

async function runAll(options: RunOptions): Promise<number> {
    const { game, out } = options;
    if (!isWorkspace(game)) exitWith(`${game} isn't a workspace (C:\\Games\\THUG2-<n>): this tool runs only there`);
    const levels = options.spots.filter((l) => !options.levels || options.levels.includes(l.level));
    if (!levels.length) exitWith("no level chosen");
    const graphics = graphicsOf(fs.readFileSync(options.settings, "utf8"));
    fs.mkdirSync(out, { recursive: true });
    const report = path.join(out, "report.txt");
    const note = (line: string): void => {
        print(line);
        fs.appendFileSync(report, `${line}\n`);
    };
    const lock = await powershell("game_lock.ps1", ["-Action", "acquire", "-Owner", options.owner, "-Game", game, "-WaitMinutes", "30"]);
    note(lock.out.trim());
    if (lock.code !== 0) return 1;
    try {
        const deploy = await powershell("deploy_build.ps1", ["-Game", game, "-Arch", "x64", "-BuildDir", options.build, "-NoEditorTools"]);
        note(deploy.out.trim().split(/\r?\n/).slice(-3).join("\n"));
        if (deploy.code !== 0) return 1;
        const ini = path.join(game, "thug2-decomp.ini");
        ensureWorkspaceMuted(game);
        const base = fs.readFileSync(ini, "utf8");
        for (const config of options.configs) {
            fs.writeFileSync(ini, iniFor(base, [configValues(config, graphics), options.set], "vulkan"), "utf8");
            ensureWorkspaceMuted(game);
            for (const level of levels) {
                note(`${config} ${level.level}: ${await runLevel(game, level, config, path.join(out, config), options.timed)}`);
            }
        }
    } finally {
        const release = await powershell("game_lock.ps1", ["-Action", "release", "-Owner", options.owner, "-Game", game]);
        note(release.out.trim());
    }
    return 0;
}

async function main(): Promise<number> {
    const { values, positionals } = parseCli(USAGE, {
        allowPositionals: true,
        options: {
            game: { type: "string" },
            out: { type: "string" },
            build: { type: "string" },
            settings: { type: "string", default: MAIN_INI },
            levels: { type: "string" },
            configs: { type: "string", default: CONFIGS.join(",") },
            owner: { type: "string", default: "water-check" },
            set: { type: "string", multiple: true, default: [] },
            timed: { type: "string", default: String(kTimedFrames) },
            spots: { type: "string" },
        },
    });
    if (positionals[0] !== "run" || !values.game || !values.out || !values.build) exitWith(USAGE);
    const set: Record<string, string> = {};
    for (const item of values.set) {
        const eq = item.indexOf("=");
        if (eq <= 0) exitWith(`--set ${item}: not Section.key=value`);
        set[item.slice(0, eq)] = item.slice(eq + 1);
    }
    const configs = values.configs.split(",").filter((c): c is Config => (CONFIGS as readonly string[]).includes(c));
    return runAll({
        game: path.resolve(values.game),
        out: path.resolve(values.out),
        build: path.resolve(values.build),
        settings: path.resolve(values.settings),
        levels: values.levels ? values.levels.split(",") : null,
        configs,
        owner: values.owner,
        set,
        timed: Number(values.timed),
        spots: values.spots ? (JSON.parse(fs.readFileSync(values.spots, "utf8")) as LevelSpots[]) : LEVELS,
    });
}

if (isMain(import.meta.url)) runMain(main);
