// NOT ORIGINAL - how far the drawn surfaces are from the game's collision, with the smoothed curves ([Graphics]
// rt_tessellation, src/engine/gfx/smooth_tess.h) off and on: the skater's wheels ride the collision, so where the drawn
// surface moved away from it they float over it or sink into it.
//
// run: under the workspace's lock (game_lock.ps1, released at the end), the x64 build deployed (deploy_build.ps1), then
// for rt_tessellation 0 and 1 the level loaded (run_game.ps1 -Level, renderer=vulkan, ray_tracing=1 - the probes need
// the ray-traced scene, whose acceleration structures hold the meshes as they are drawn: render_pc/rt_record.h), the
// skater stood at --at, and a fan of rays cast from above him both ways: `colprobe` (the game's collision feeler) and
// `rtprobe` (the ray-traced scene) - the automation channel's (tools/game/README.md "One ray, two ways"), their results
// in the log. Each configuration's log in <out>/tess<0|1>.log.
//
// report (run does it at the end): per ray hitting a sloped surface (the collision's normal 0.2 - 0.97 up: quarter pipes,
// banks, the curves; floors and walls left out) on both, the same surface (within 24 units): the gap = the drawn
// surface's distance - the collision's, along the collision's normal (positive: the drawn surface behind the collision - the wheels float over it;
// negative: in front - they sink into it). Per configuration: how many, the mean and largest |gap|, the largest each
// way; and the change smoothing made per ray (1 against 0: the drawn meshes and the collision differ a little anyway).
// report.txt in the folder; exit 1 when a smoothed gap is over --cap (default 2: rt_tess_max_deviation's default) more
// than the unsmoothed one, or no ray hit a slope.

import { spawn } from "node:child_process";
import * as fs from "node:fs";
import * as path from "node:path";
import { fileURLToPath } from "node:url";
import { exitWith, isMain, parseCli, print, runMain } from "thug2-level-converter-core/lib/cli.ts";
import { clearAutomationFiles } from "./edit_level.ts";
import { iniFor } from "./renderer_parity.ts";
import { ensureWorkspaceMuted, isWorkspace } from "./workspace_ini.ts";

const USAGE = `usage: tess_gap.ts run --game <workspace> --build <folder> --level <name> --at x,y,z,heading --out <folder>
                    [--owner name] [--set S.key=v]... [--cap 2]
       tess_gap.ts report <folder> [--cap 2]

The drawn surfaces against the collision with rt_tessellation off and on (tools/game/README.md).`;

const HERE = path.dirname(fileURLToPath(import.meta.url));
const CONFIGS = ["0", "1"] as const;
const EYE = 150; // the rays start this far above the skater's feet (over his head: he is in the scene too)
const AZIMUTH_STEP = 10; // degrees round him
const PITCHES = [-4, -10, -18, -28]; // degrees below the horizon
const REACH = 1500; // units a ray goes
const SLOPE_LOW = 0.2; // the collision's normal's y: a slope between these
const SLOPE_HIGH = 0.97;
const SAME_SURFACE = 24; // two hits further apart than this: different surfaces
const SETTLE_FRAMES = 150; // after the teleport: the TLAS built, the skater still

export interface Ray {
    origin: [number, number, number];
    direction: [number, number, number];
}

// The fan of rays from above a point: every AZIMUTH_STEP degrees round it, at each pitch.
export function fan(x: number, y: number, z: number): Ray[] {
    const rays: Ray[] = [];
    for (let azimuth = 0; azimuth < 360; azimuth += AZIMUTH_STEP)
        for (const pitch of PITCHES) {
            const a = (azimuth * Math.PI) / 180;
            const p = (pitch * Math.PI) / 180;
            rays.push({
                origin: [x, y + EYE, z],
                direction: [Math.cos(p) * Math.sin(a), Math.sin(p), Math.cos(p) * Math.cos(a)],
            });
        }
    return rays;
}

const rayText = (r: Ray): string => [...r.origin, ...r.direction].map((v) => v.toFixed(4)).join(" ");

export interface Hit {
    distance: number;
    normal: [number, number, number] | null; // (the collision's; null for the ray-traced scene)
}

// The probes' results in a log ("automation: colprobe <ray> <reach> -> hit <d> point ... normal x y z ..." and
// "automation: rtprobe <ray> <reach> -> hit <d> ..."), by ray text; misses left out.
export function probeResults(log: string): { col: Map<string, Hit>; rt: Map<string, Hit> } {
    const col = new Map<string, Hit>();
    const rt = new Map<string, Hit>();
    for (const m of log.matchAll(/automation: (colprobe|rtprobe) (.+?) \d+ -> hit ([-0-9.e]+)(.*)$/gm)) {
        const kind = m[1]!;
        const ray = m[2]!;
        const distance = Number(m[3]);
        if (kind === "colprobe") {
            const normal = /normal ([-0-9.e]+) ([-0-9.e]+) ([-0-9.e]+)/.exec(m[4]!);
            col.set(ray, {
                distance,
                normal: normal ? [Number(normal[1]), Number(normal[2]), Number(normal[3])] : null,
            });
        } else {
            rt.set(ray, { distance, normal: null });
        }
    }
    return { col, rt };
}

// Per ray hitting a slope on both, the same surface: the drawn surface's distance - the collision's.
export function slopeGaps(log: string): Map<string, number> {
    const { col, rt } = probeResults(log);
    const gaps = new Map<string, number>();
    for (const [ray, hit] of col) {
        const drawn = rt.get(ray);
        const up = hit.normal === null ? 0 : Math.abs(hit.normal[1]);
        if (!drawn || up < SLOPE_LOW || up > SLOPE_HIGH) continue;
        // (along the surface's normal: the distance along the ray times the cosine between the ray and the normal)
        const direction = ray.split(" ").slice(3, 6).map(Number);
        const length = Math.hypot(...direction) || 1;
        const normal = hit.normal ?? [0, 1, 0];
        const cosine = Math.abs(direction.reduce((s, v, i) => s + v * normal[i]!, 0)) / length;
        const along = drawn.distance - hit.distance;
        const gap = along * cosine;
        if (Math.abs(along) <= SAME_SURFACE) gaps.set(ray, gap);
    }
    return gaps;
}

export interface GapSummary {
    count: number;
    meanAbs: number;
    maxAbs: number;
    mostBehind: number; // the largest positive gap (floating)
    mostInFront: number; // the most negative (sinking)
}

export function summarise(gaps: Iterable<number>): GapSummary {
    const values = [...gaps];
    if (!values.length) return { count: 0, meanAbs: 0, maxAbs: 0, mostBehind: 0, mostInFront: 0 };
    return {
        count: values.length,
        meanAbs: values.reduce((s, v) => s + Math.abs(v), 0) / values.length,
        maxAbs: Math.max(...values.map(Math.abs)),
        mostBehind: Math.max(0, ...values),
        mostInFront: Math.min(0, ...values),
    };
}

const describe = (s: GapSummary): string =>
    `${s.count} rays on slopes; |gap| mean ${s.meanAbs.toFixed(3)}, largest ${s.maxAbs.toFixed(3)}; drawn behind the collision by up to ${s.mostBehind.toFixed(3)}, in front by up to ${(-s.mostInFront).toFixed(3)}`;

export function report(out: string, cap: number): number {
    const logs = Object.fromEntries(
        CONFIGS.map((c) => {
            const file = path.join(out, `tess${c}.log`);
            return [c, fs.existsSync(file) ? fs.readFileSync(file, "utf8") : ""];
        }),
    ) as Record<(typeof CONFIGS)[number], string>;
    const off = slopeGaps(logs["0"]);
    const on = slopeGaps(logs["1"]);
    const changes: number[] = [];
    for (const [ray, gap] of on) {
        const before = off.get(ray);
        if (before !== undefined) changes.push(gap - before);
    }
    const lines = [
        `rt_tessellation=0: ${describe(summarise(off.values()))}`,
        `rt_tessellation=1: ${describe(summarise(on.values()))}`,
        `the smoothing's change per ray (${changes.length} rays on a slope in both): ${describe(summarise(changes))}`,
    ];
    const change = summarise(changes);
    let failed = false;
    if (!on.size || !off.size) {
        failed = true;
        lines.push("FAIL no ray hit a slope (no quarter pipe or bank round --at?)");
    }
    if (change.maxAbs > cap + 0.01) {
        failed = true;
        lines.push(
            `FAIL the smoothing moved a drawn surface ${change.maxAbs.toFixed(3)} from where it was (cap ${cap})`,
        );
    }
    lines.push(failed ? "tess_gap: FAILED" : "tess_gap: passed");
    fs.writeFileSync(path.join(out, "report.txt"), `${lines.join("\n")}\n`);
    for (const l of lines) print(l);
    return failed ? 1 : 0;
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

interface RunOptions {
    game: string;
    build: string;
    level: string;
    at: [number, number, number, number];
    out: string;
    owner: string;
    set: Record<string, string>;
}

async function run(options: RunOptions): Promise<number> {
    const { game, out } = options;
    if (!isWorkspace(game)) exitWith(`${game} isn't a workspace (C:\\Games\\THUG2-<n>): this tool runs only there`);
    fs.mkdirSync(out, { recursive: true });
    const lock = await powershell("game_lock.ps1", ["-Action", "acquire", "-Owner", options.owner, "-Game", game]);
    print(lock.out.trim());
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
        print(deploy.out.trim());
        if (deploy.code !== 0) return 1;
        const ini = path.join(game, "thug2-decomp.ini");
        ensureWorkspaceMuted(game);
        const base = fs.readFileSync(ini, "utf8");
        const [x, y, z, heading] = options.at;
        const commands = [
            `waitfor level ${options.level} 150`,
            `teleport ${x} ${y} ${z} ${heading}`,
            `wait ${SETTLE_FRAMES}`,
            ...fan(x, y, z).flatMap((r) => [`colprobe ${rayText(r)} ${REACH}`, `rtprobe ${rayText(r)} ${REACH}`]),
        ];
        for (const config of CONFIGS) {
            const values = [{ "Graphics.ray_tracing": "1", "Graphics.rt_tessellation": config }, options.set];
            fs.writeFileSync(ini, iniFor(base, values, "vulkan"), "utf8");
            ensureWorkspaceMuted(game);
            clearAutomationFiles(game);
            const log = path.join(game, "thug2-decomp.log");
            fs.rmSync(log, { force: true });
            const result = await powershell("run_game.ps1", [
                "-Game",
                game,
                "-Arch",
                "x64",
                "-Seconds",
                "240",
                "-HangSeconds",
                "120",
                "-Level",
                options.level,
                "-Commands",
                commands.join(";"),
            ]);
            fs.writeFileSync(path.join(out, `tess${config}.run.txt`), result.out);
            fs.writeFileSync(
                path.join(out, `tess${config}.log`),
                fs.existsSync(log) ? fs.readFileSync(log, "utf8") : "",
            );
            print(`rt_tessellation=${config}: exit ${result.code}`);
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
    return 0;
}

async function main(): Promise<number> {
    const { values, positionals } = parseCli(USAGE, {
        options: {
            game: { type: "string" },
            build: { type: "string" },
            level: { type: "string" },
            at: { type: "string" },
            out: { type: "string" },
            owner: { type: "string", default: "tess-gap" },
            set: { type: "string", multiple: true },
            cap: { type: "string", default: "2" },
        },
        allowPositionals: true,
    });
    const [command, target] = positionals;
    const cap = Number(values.cap);
    if (command === "report" && target && positionals.length === 2) return report(path.resolve(target), cap);
    if (command !== "run" || positionals.length !== 1) {
        print(USAGE);
        return 2;
    }
    if (!values.game || !values.build || !values.level || !values.at || !values.out)
        exitWith("run needs --game, --build, --level, --at and --out");
    const at = values.at.split(",").map(Number);
    if (at.length !== 4 || at.some((v) => !Number.isFinite(v))) exitWith("--at x,y,z,heading");
    const set: Record<string, string> = {};
    for (const s of values.set ?? []) {
        const eq = s.indexOf("=");
        if (eq <= 0) exitWith(`--set Section.key=value (${s})`);
        set[s.slice(0, eq)] = s.slice(eq + 1);
    }
    const out = path.resolve(values.out);
    const code = await run({
        game: path.resolve(values.game),
        build: path.resolve(values.build),
        level: values.level,
        at: at as [number, number, number, number],
        out,
        owner: values.owner,
        set,
    });
    return code !== 0 ? code : report(out, cap);
}

if (isMain(import.meta.url)) await runMain(() => main());
