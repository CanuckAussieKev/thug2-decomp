// NOT ORIGINAL - the light energy audit in the game (docs/design/vulkan.md "Each light counted once"): at each level's
// own time of day, the relit level with every ray-traced light on must look like the original's bake - a light counted
// twice shows as darker shadows and corners (the bake's shadow and a traced one), brighter lamp areas (a baked lamp and
// a traced one) or a brighter ground (a baked bounce and a traced one). And a census of what lights each thing.
//
//   node tools/game/light_audit.ts walk --game C:\Games\THUG2-<n> --build <x64 build> --out <folder>
//        [--levels BO,BE,LA,NO,nj | all] [--restarts 4] [--owner <name>] [--set Section.key=value]...
//     The new build (it has the console's `restart` and `rt census`): each level loaded in turn (one game), the skater
//     put at its first --restarts restarts (F8's walk: the log gives each one's place) and the census taken there
//     (`rt census`: the world's meshes by what lights them, the objects' / models' / particles' ways) with the `full`
//     look. Writes <out>/spots.json (the places) and <out>/census/<level>.txt.
//   node tools/game/light_audit.ts capture --game ... --build <x64 build> --out <folder> --config bake|full
//        [--tag <name>] [--levels ...] [--probe] [--owner <name>] [--set Section.key=value]...
//     One game per configuration: each level, each place of spots.json - the skater stood there (teleport, heading 0),
//     the game's own camera behind him, captured into <out>/<tag>-<config>/<level>-<k>.png. Any build (no console
//     command needed): run it with the build before a change and after. --probe (full only): `rt probe` on a 4 x 3
//     grid of each picture (what the pixel shows - mesh, material, relit's parts) - logged for `compare`'s worst cells.
//       bake  ray_tracing=1, rt_relit=0, rt_shadows / rt_ao / rt_gi / rt_lights 0: the original's bake
//       full  rt_relit=1, rt_shadows=1, rt_ao=1, rt_gi=1, rt_lights=auto - every light term on (rt_gi stays out of effect
//             where the settings model says relit owns the bounce)
//     Both: tod=level, and the look effects that don't light (HDR, bloom, fog, reflections, per-pixel lighting,
//     smoothing, depth of field) off, so only the lighting differs.
//   node tools/game/light_audit.ts compare <out> --a <tag-config> --b <tag-config> [--limits <json>]
//     Per picture, per region of `a` (the reference): the mean brightness of `b` over `a`'s - the whole picture,
//     `lit` (above the picture's Otsu split: sunlit), `shade` (below it), `corners` (its darkest tenth), `lamps` (its
//     brightest twentieth: under lamps at night), `skater` (the box round the skater the game's camera keeps in the
//     middle) - and a heat map (<out>/compare-<a>-<b>/<picture>-heat.png: blue darker, red brighter) and its worst
//     cells (a 16 x 9 grid; with --probe's lines, what the nearest probe hit). Report: report.txt / report.json. The
//     limits (each region's ratio within [low, high]; default LIMITS below) fail the run when broken.
//   node tools/game/light_audit.ts html <out>   A self-contained page (index.html): the census, the ratios, the worst
//     cells with their pictures (a, b, heat map).
//   node tools/game/light_audit.ts textures --game <folder> --out <folder> [--levels a,b]   Every level's texture (the
//     original levels' and the ported ones in thug2-decomp-data) by its file's alpha (opaque, binary, soft) against how
//     the level's materials draw it (solid - its alpha never looked at -, cut-out, blended, additive, only a later
//     pass): textures.tsv and textures.txt - e.g. how many "soft alpha" textures are drawn solid. Nothing run; the
//     game folder is only read (the main install's too).
// Workspaces only (game_lock.ps1 held for the run; deploy_build.ps1; run_game.ps1 with the automation channel), muted.

import * as fs from "node:fs";
import * as path from "node:path";
import * as hd from "thug2-level-converter-core/level_port/hd_textures.ts";
import * as texFile from "thug2-level-converter-core/level_port/tex.ts";
import * as scn from "thug2-level-converter-core/level_port/thug2_scene.ts";
import { exitWith, isMain, parseCli, print, runMain } from "thug2-level-converter-core/lib/cli.ts";
import { decodePng, encodePng, type Image } from "thug2-level-converter-core/lib/frame_diff.ts";
import { clearAutomationFiles } from "./edit_level.ts";
import { powershell } from "./renderer_parity.ts";
import { otsuThreshold, sameFile } from "./rt_shadows_check.ts";
import { ensureWorkspaceMuted, isWorkspace, setIniValue } from "./workspace_ini.ts";

const USAGE = `usage: light_audit.ts walk --game <workspace> --build <folder> --out <folder> [--levels a,b|all] [--restarts N] [--owner name] [--set S.key=v]...
       light_audit.ts capture --game <workspace> --build <folder> --out <folder> --config bake|full [--tag name] [--levels a,b|all] [--probe] [--owner name] [--set S.key=v]...
       light_audit.ts compare <out> --a <tag-config> --b <tag-config> [--limits <json>]
       light_audit.ts html <out>
       light_audit.ts textures --game <folder> --out <folder> [--levels a,b|all]

The light energy audit in the game: the relit level with every light term on against the original's bake, and the census (tools/game/README.md).`;

const GAME_EXE = "thug2-decomp.exe";

export const DEFAULT_LEVELS = ["BO", "BE", "LA", "NO", "nj"];
// Every original level (Data\levels, the skies and the editor's shells left out) - the census and the sweep's.
export const ORIGINAL_LEVELS = [
    "TR",
    "BO",
    "BA",
    "BE",
    "AU",
    "NO",
    "ST",
    "SE",
    "SE2",
    "SC",
    "PH",
    "DJ",
    "LA",
    "AP",
    "CA",
];

// What both configurations share: the level's own time, and the effects that don't light the level off.
export const SHARED: Record<string, string> = {
    "Graphics.ray_tracing": "1",
    "Graphics.tod": "level",
    "Graphics.tod_cycle": "0",
    "Graphics.rt_hdr": "0",
    "Graphics.rt_bloom": "0",
    "Graphics.rt_fog": "0",
    "Graphics.rt_shafts": "0",
    "Graphics.rt_reflections": "0",
    "Graphics.rt_pbr": "0",
    "Graphics.rt_tessellation": "0",
    "Graphics.rt_dof": "0",
    "Graphics.rt_motion_blur": "0",
    "Graphics.rt_cel": "0",
    "Graphics.rt_style": "off",
    "Graphics.rt_decals": "0",
    "Graphics.upscaler": "off",
    "Graphics.frame_generation": "off",
};
export const CONFIGS = {
    bake: {
        "Graphics.rt_relit": "0",
        "Graphics.rt_shadows": "0",
        "Graphics.rt_ao": "0",
        "Graphics.rt_gi": "0",
        "Graphics.rt_lights": "0",
    },
    full: {
        "Graphics.rt_relit": "1",
        "Graphics.rt_shadows": "1",
        "Graphics.rt_ao": "1",
        "Graphics.rt_gi": "1",
        "Graphics.rt_lights": "auto",
    },
} as const;
export type ConfigName = keyof typeof CONFIGS;

// --- the game's lines --------------------------------------------------------------------------------------------------

export interface Spot {
    x: number;
    y: number;
    z: number;
    name: string;
}
// The restart tester's lines ("restart tester: 1 / 12 <name> at (x, y, z), types [...]"), in order.
export function restartsOf(log: string): Spot[] {
    const out: Spot[] = [];
    for (const m of log.matchAll(/restart tester: \d+ \/ \d+ (.*?) at \((-?[\d.]+), (-?[\d.]+), (-?[\d.]+)\)/g))
        out.push({ name: m[1]!, x: Number(m[2]), y: Number(m[3]), z: Number(m[4]) });
    return out;
}
// The census blocks ("rt census: ..." lines) of a log, in order: each starts with its "world:" line.
export function censusBlocks(log: string): string[][] {
    const blocks: string[][] = [];
    for (const line of log.split(/\r?\n/)) {
        const at = line.indexOf("rt census: ");
        if (at < 0) continue;
        const text = line.slice(at);
        if (text.startsWith("rt census: world: ")) blocks.push([]);
        blocks[blocks.length - 1]?.push(text);
    }
    return blocks;
}
// The probes' answers ("automation: console rt probe <x> <y>:" then its lines up to the next "automation:").
export interface Probe {
    across: number;
    down: number;
    text: string;
}
export function probesOf(log: string): Probe[] {
    const out: Probe[] = [];
    const parts = log.split(/automation: console rt probe /);
    for (const part of parts.slice(1)) {
        const head = /^([\d.]+) ([\d.]+):/.exec(part);
        if (!head) continue;
        const end = part.search(/\r?\n[^ \r\n][^\r\n]*automation: /);
        out.push({
            across: Number(head[1]),
            down: Number(head[2]),
            text: (end > 0 ? part.slice(0, end) : part).split(/\r?\n/).slice(1, 6).join(" | ").slice(0, 600),
        });
    }
    return out;
}

// --- running -----------------------------------------------------------------------------------------------------------

interface RunOptions {
    game: string;
    build: string;
    out: string;
    levels: string[];
    owner: string;
    set: Record<string, string>;
}

function iniText(base: string, values: readonly Record<string, string>[]): string {
    let text = base;
    for (const set of values)
        for (const [where, value] of Object.entries(set)) {
            const dot = where.indexOf(".");
            text = setIniValue(text, where.slice(0, dot), where.slice(dot + 1), value);
        }
    return setIniValue(text, "Graphics", "renderer", "vulkan");
}

// Under the workspace's lock with the build deployed: `body` runs games; the lock released (the folder's own build and
// ini back) at the end whatever happens.
async function withGame(options: RunOptions, body: (ini: string, base: string) => Promise<number>): Promise<number> {
    const { game } = options;
    if (!isWorkspace(game)) exitWith(`${game} isn't a workspace (C:\\Games\\THUG2-<n>): this tool runs only there`);
    fs.mkdirSync(options.out, { recursive: true });
    const lock = await powershell(
        "game_lock.ps1",
        ["-Action", "acquire", "-Owner", options.owner, "-Game", game],
        process.env,
    );
    print(lock.out.trim());
    if (lock.code !== 0) return 1;
    try {
        const deploy = await powershell(
            "deploy_build.ps1",
            ["-Game", game, "-Arch", "x64", "-BuildDir", options.build, "-NoEditorTools"],
            process.env,
        );
        print(deploy.out.trim());
        if (deploy.code !== 0 || !sameFile(path.join(options.build, GAME_EXE), path.join(game, GAME_EXE))) return 1;
        ensureWorkspaceMuted(game);
        const ini = path.join(game, "thug2-decomp.ini");
        return await body(ini, fs.readFileSync(ini, "utf8"));
    } finally {
        const release = await powershell(
            "game_lock.ps1",
            ["-Action", "release", "-Owner", options.owner, "-Game", game],
            process.env,
        );
        print(release.out.trim());
    }
}

// One game for a level (started in it: a level changed in game can crash on what the one before left loaded, and one
// level's crash then keeps the others), its commands; its log kept as <logOut>.log.
async function oneGame(game: string, level: string, commands: string[], logOut: string): Promise<number> {
    const all = [`waitfor level ${level} 400`, "wait 900", ...commands, "quit"];
    clearAutomationFiles(game);
    const log = path.join(game, "thug2-decomp.log");
    fs.rmSync(log, { force: true });
    const args = ["-Game", game, "-Arch", "x64", "-Seconds", "600", "-HangSeconds", "180"];
    args.push("-Level", level, "-Commands", all.join(";"));
    const result = await powershell("run_game.ps1", args, process.env);
    fs.writeFileSync(`${logOut}.run.txt`, result.out);
    fs.writeFileSync(`${logOut}.log`, fs.existsSync(log) ? fs.readFileSync(log, "utf8") : "");
    print(`game ${level}: exit ${result.code}`);
    return result.code;
}

const seriesOf = (tag: string, config: string, level: string, k: number): string => `la-${tag}-${config}-${level}-${k}`;

async function walk(options: RunOptions, restarts: number): Promise<number> {
    return withGame(options, async (ini, base) => {
        fs.writeFileSync(ini, iniText(base, [SHARED, CONFIGS.full, options.set]), "utf8");
        ensureWorkspaceMuted(options.game);
        const commands = ["console rt census", "wait 120", "console rt census"];
        for (let k = 0; k < restarts; k++) commands.push("console restart", "wait 30");
        fs.mkdirSync(path.join(options.out, "census"), { recursive: true });
        fs.mkdirSync(path.join(options.out, "walk"), { recursive: true });
        const spotsFile = path.join(options.out, "spots.json");
        const spots: Record<string, Spot[]> = fs.existsSync(spotsFile)
            ? (JSON.parse(fs.readFileSync(spotsFile, "utf8")) as Record<string, Spot[]>)
            : {};
        let failures = 0;
        for (const level of options.levels) {
            const logBase = path.join(options.out, "walk", level);
            failures += (await oneGame(options.game, level, commands, logBase)) === 0 ? 0 : 1;
            const log = fs.readFileSync(`${logBase}.log`, "utf8");
            // (two census commands: the second's counts are over its 120 frames)
            const blocks = censusBlocks(log);
            const block = blocks[1] ?? blocks[0] ?? [];
            fs.writeFileSync(path.join(options.out, "census", `${level}.txt`), `${block.join("\n")}\n`);
            spots[level] = restartsOf(log).slice(0, restarts);
        }
        fs.writeFileSync(spotsFile, `${JSON.stringify(spots, null, 1)}\n`);
        print(
            `walked ${options.levels.length} levels: ${Object.values(spots).reduce((n, s) => n + s.length, 0)} places`,
        );
        return failures === 0 ? 0 : 1;
    });
}

export const PROBE_GRID: readonly [number, number][] = [0.15, 0.4, 0.65, 0.9].flatMap((x) =>
    [0.25, 0.55, 0.85].map((y) => [x, y] as [number, number]),
);

async function capture(options: RunOptions, config: ConfigName, tag: string, probe: boolean): Promise<number> {
    const spots = JSON.parse(fs.readFileSync(path.join(options.out, "spots.json"), "utf8")) as Record<string, Spot[]>;
    const levels = options.levels.filter((l) => spots[l]?.length);
    return withGame(options, async (ini, base) => {
        fs.writeFileSync(ini, iniText(base, [SHARED, CONFIGS[config], options.set]), "utf8");
        ensureWorkspaceMuted(options.game);
        const folder = path.join(options.out, `${tag}-${config}`);
        fs.mkdirSync(folder, { recursive: true });
        let failures = 0;
        let missing = 0;
        for (const level of levels) {
            const commands: string[] = [];
            spots[level]!.forEach((s, k) => {
                const name = seriesOf(tag, config, level, k + 1);
                fs.rmSync(path.join(options.game, "Screenshots", name), { recursive: true, force: true });
                commands.push(
                    `teleport ${s.x} ${s.y} ${s.z} 0`,
                    "camera off",
                    "wait 240",
                    `capture 1 ${name}`,
                    "wait 2",
                );
                if (probe) {
                    commands.push(`echo probes ${level}-${k + 1}`);
                    for (const [x, y] of PROBE_GRID) commands.push(`console rt probe ${x} ${y}`);
                }
            });
            failures +=
                (await oneGame(options.game, level, commands, path.join(folder, `game-${level}`))) === 0 ? 0 : 1;
            spots[level]!.forEach((_, k) => {
                const series = path.join(options.game, "Screenshots", seriesOf(tag, config, level, k + 1));
                const frame = path.join(series, "frame_0001.png");
                if (fs.existsSync(frame)) fs.copyFileSync(frame, path.join(folder, `${level}-${k + 1}.png`));
                else missing++;
                fs.rmSync(series, { recursive: true, force: true });
            });
        }
        print(`${tag}-${config}: ${missing} pictures missing`);
        return failures === 0 && missing === 0 ? 0 : 1;
    });
}

// --- comparing ---------------------------------------------------------------------------------------------------------

const luma = (rgb: Uint8Array, p: number): number =>
    0.2126 * rgb[p * 3]! + 0.7152 * rgb[p * 3 + 1]! + 0.0722 * rgb[p * 3 + 2]!;

export const REGIONS = ["whole", "lit", "shade", "corners", "lamps", "skater"] as const;
export type RegionName = (typeof REGIONS)[number];
// The skater's box: the game's camera keeps him in the middle, a little low (fractions of the picture).
export const SKATER_BOX = { left: 0.43, right: 0.57, top: 0.42, bottom: 0.88 };
export const LIMITS: Record<RegionName, [number, number]> = {
    whole: [0.9, 1.1],
    lit: [0.88, 1.12],
    shade: [0.85, 1.15],
    corners: [0.8, 1.25],
    lamps: [0.85, 1.15],
    skater: [0.8, 1.25],
};

export interface RegionRatios {
    ratio: Partial<Record<RegionName, number>>;
    pixels: Partial<Record<RegionName, number>>;
}

// Each region's mean brightness in `b` over `a`'s; regions from `a` (the reference).
export function regionRatios(a: Image, b: Image): RegionRatios {
    const n = a.width * a.height;
    const la = new Float32Array(n);
    const lb = new Float32Array(n);
    const histogram = new Array<number>(256).fill(0);
    for (let p = 0; p < n; p++) {
        la[p] = luma(a.rgb, p);
        lb[p] = luma(b.rgb, p);
        histogram[Math.min(255, Math.round(la[p]!))]! += 1;
    }
    const split = otsuThreshold(histogram);
    const sorted = Float32Array.from(la).sort();
    const darkest = sorted[Math.floor(n * 0.1)]!;
    const brightest = sorted[Math.floor(n * 0.95)]!;
    const sums: Record<RegionName, [number, number, number]> = {
        whole: [0, 0, 0],
        lit: [0, 0, 0],
        shade: [0, 0, 0],
        corners: [0, 0, 0],
        lamps: [0, 0, 0],
        skater: [0, 0, 0],
    };
    const add = (r: RegionName, p: number): void => {
        const s = sums[r];
        s[0] += la[p]!;
        s[1] += lb[p]!;
        s[2] += 1;
    };
    for (let p = 0; p < n; p++) {
        const x = (p % a.width) / a.width;
        const y = Math.floor(p / a.width) / a.height;
        add("whole", p);
        add(la[p]! > split ? "lit" : "shade", p);
        if (la[p]! <= darkest) add("corners", p);
        if (la[p]! >= brightest && la[p]! > 8) add("lamps", p);
        if (x >= SKATER_BOX.left && x <= SKATER_BOX.right && y >= SKATER_BOX.top && y <= SKATER_BOX.bottom)
            add("skater", p);
    }
    const out: RegionRatios = { ratio: {}, pixels: {} };
    for (const r of REGIONS) {
        const [sa, sb, count] = sums[r];
        out.pixels[r] = count;
        if (count > 50 && sa > count * 2) out.ratio[r] = sb / sa;
    }
    return out;
}

export interface Cell {
    cx: number;
    cy: number;
    ratio: number;
    weight: number;
}
// A 16 x 9 grid's cells by how far `b` strays from `a` (|log ratio| x the cell's brightness share), worst first; and
// the heat map (blue: darker in b, red: brighter; strength by the log ratio).
export function heatCells(a: Image, b: Image): { cells: Cell[]; heat: Image } {
    const columns = 16;
    const rows = 9;
    const sa = new Float64Array(columns * rows);
    const sb = new Float64Array(columns * rows);
    const heat: Image = { width: a.width, height: a.height, rgb: new Uint8Array(a.width * a.height * 3) };
    for (let p = 0; p < a.width * a.height; p++) {
        const x = p % a.width;
        const y = Math.floor(p / a.width);
        const va = luma(a.rgb, p);
        const vb = luma(b.rgb, p);
        const c =
            Math.min(columns - 1, Math.floor((x / a.width) * columns)) +
            columns * Math.min(rows - 1, Math.floor((y / a.height) * rows));
        sa[c]! += va;
        sb[c]! += vb;
        const r = Math.log((vb + 4) / (va + 4));
        const k = Math.min(1, Math.abs(r) / Math.log(2));
        const grey = va * 0.35;
        heat.rgb[p * 3] = Math.round(r > 0 ? grey + (255 - grey) * k : grey * (1 - k));
        heat.rgb[p * 3 + 1] = Math.round(grey * (1 - k));
        heat.rgb[p * 3 + 2] = Math.round(r < 0 ? grey + (255 - grey) * k : grey * (1 - k));
    }
    const cells: Cell[] = [];
    let total = 0;
    for (let c = 0; c < columns * rows; c++) total += sa[c]!;
    for (let c = 0; c < columns * rows; c++) {
        const ratio = (sb[c]! + 1) / (sa[c]! + 1);
        cells.push({
            cx: ((c % columns) + 0.5) / columns,
            cy: (Math.floor(c / columns) + 0.5) / rows,
            ratio,
            weight: Math.abs(Math.log(ratio)) * Math.sqrt(sa[c]! / Math.max(1, total / (columns * rows))),
        });
    }
    cells.sort((x, y) => y.weight - x.weight);
    return { cells, heat };
}

interface CompareResult {
    picture: string;
    ratios: RegionRatios;
    worst: (Cell & { probe?: string })[];
    problems: string[];
}

function readPictureFile(file: string): Image {
    return decodePng(fs.readFileSync(file));
}

function compare(out: string, aName: string, bName: string, limits: Record<RegionName, [number, number]>): number {
    const aDir = path.join(out, aName);
    const bDir = path.join(out, bName);
    const into = path.join(out, `compare-${aName}-${bName}`);
    fs.mkdirSync(into, { recursive: true });
    const probes = new Map<string, Probe[]>();
    for (const f of fs.readdirSync(bDir).filter((n) => n.startsWith("game-") && n.endsWith(".log"))) {
        const log = fs.readFileSync(path.join(bDir, f), "utf8");
        for (const part of log.split(/automation: echo probes /).slice(1)) {
            const name = /^(\S+)/.exec(part)?.[1];
            if (name) probes.set(name, probesOf(part));
        }
    }
    const results: CompareResult[] = [];
    const lines: string[] = [`light audit: ${bName} against ${aName}`];
    const pictures = fs.readdirSync(aDir).filter((f) => f.endsWith(".png") && fs.existsSync(path.join(bDir, f)));
    for (const file of pictures.sort()) {
        const a = readPictureFile(path.join(aDir, file));
        const b = readPictureFile(path.join(bDir, file));
        if (a.width !== b.width || a.height !== b.height) continue;
        const ratios = regionRatios(a, b);
        const { cells, heat } = heatCells(a, b);
        const name = file.replace(/\.png$/, "");
        fs.writeFileSync(path.join(into, `${name}-heat.png`), encodePng(heat));
        const near = probes.get(name) ?? [];
        const worst = cells.slice(0, 3).map((c) => {
            let best: Probe | undefined;
            let bestD = Infinity;
            for (const p of near) {
                const d = (p.across - c.cx) ** 2 + (p.down - c.cy) ** 2;
                if (d < bestD) {
                    bestD = d;
                    best = p;
                }
            }
            return best ? { ...c, probe: best.text } : c;
        });
        const problems: string[] = [];
        for (const r of REGIONS) {
            const v = ratios.ratio[r];
            const [low, high] = limits[r];
            if (v !== undefined && (v < low || v > high)) problems.push(`${r} ${v.toFixed(3)} outside ${low}-${high}`);
        }
        results.push({ picture: name, ratios, worst, problems });
        lines.push(
            `${name}: ${REGIONS.map((r) => `${r} ${ratios.ratio[r]?.toFixed(3) ?? "-"}`).join(", ")}` +
                `; worst cells ${worst.map((c) => `(${c.cx.toFixed(2)},${c.cy.toFixed(2)}) x${c.ratio.toFixed(2)}`).join(" ")}` +
                (problems.length ? `\n  FAIL ${problems.join("; ")}` : ""),
        );
    }
    // per region over every picture: the median ratio
    const medians: Partial<Record<RegionName, number>> = {};
    for (const r of REGIONS) {
        const values = results
            .map((x) => x.ratios.ratio[r])
            .filter((v): v is number => v !== undefined)
            .sort((x, y) => x - y);
        if (values.length) medians[r] = values[Math.floor(values.length / 2)]!;
    }
    lines.push(`medians: ${REGIONS.map((r) => `${r} ${medians[r]?.toFixed(3) ?? "-"}`).join(", ")}`);
    const failed = results.some((x) => x.problems.length) || results.length === 0;
    lines.push(failed ? "light audit: FAILED" : "light audit: passed");
    fs.writeFileSync(path.join(into, "report.txt"), `${lines.join("\n")}\n`);
    fs.writeFileSync(
        path.join(into, "report.json"),
        `${JSON.stringify({ a: aName, b: bName, limits, medians, results }, null, 1)}\n`,
    );
    for (const l of lines) print(l);
    return failed ? 1 : 0;
}

// --- the textures by how the levels draw them ----------------------------------------------------------------------------

// How a level draws a texture, from every material pass that names it: its first pass opaque (blend 0, no alpha test -
// the texture's alpha is never looked at), its first pass alpha-tested (a cut-out), its first pass blended by alpha,
// added, or only a later pass (an overlay, a light map, an environment map).
export type DrawKind = "solid" | "cut-out" | "blended" | "additive" | "later pass" | "mixed" | "unused";
export function drawKind(uses: readonly { pass: number; blend: number; cutoff: number }[]): DrawKind {
    if (!uses.length) return "unused";
    const kinds = new Set<DrawKind>();
    for (const u of uses) {
        const blend = u.blend & 0xff;
        if (u.pass > 0) kinds.add("later pass");
        else if (blend === 1 || blend === 2) kinds.add("additive");
        else if (blend !== 0) kinds.add("blended");
        else kinds.add(u.cutoff > 1 ? "cut-out" : "solid");
    }
    if (kinds.size === 1) return [...kinds][0]!;
    // (a first-pass use decides over later ones: the overlay use doesn't change how the surface is drawn)
    kinds.delete("later pass");
    return kinds.size === 1 ? [...kinds][0]! : "mixed";
}

interface TextureRow {
    level: string;
    checksum: string;
    size: string;
    alpha: "opaque" | "binary" | "smooth" | "?";
    draw: DrawKind;
}

function levelFolders(game: string, levels: string[] | null): { name: string; folder: string }[] {
    const roots = [path.join(game, "Data", "levels"), path.join(game, "thug2-decomp-data", "levels")];
    const out: { name: string; folder: string }[] = [];
    for (const root of roots) {
        if (!fs.existsSync(root)) continue;
        for (const name of fs.readdirSync(root).sort()) {
            if (/_sky$|shell|^sk5ed|^default|^mainmenu|^testlevel/i.test(name)) continue;
            if (levels && !levels.some((l) => l.toLowerCase() === name.toLowerCase())) continue;
            out.push({ name, folder: path.join(root, name) });
        }
    }
    return out;
}

function textureCensus(game: string, out: string, levels: string[] | null): number {
    const rows: TextureRow[] = [];
    for (const { name, folder } of levelFolders(game, levels)) {
        const files = fs.readdirSync(folder);
        const uses = new Map<number, { pass: number; blend: number; cutoff: number }[]>();
        for (const f of files.filter((n) => /\.scn\.xbx$/i.test(n))) {
            let scene: scn.Scene;
            try {
                scene = scn.readScene(fs.readFileSync(path.join(folder, f)), f);
            } catch {
                continue;
            }
            for (const m of scene.materials)
                m.passes.forEach((p, pass) => {
                    if (!p.texture) return;
                    const list = uses.get(p.texture) ?? [];
                    list.push({ pass, blend: p.blendMode, cutoff: m.alphaCutoff });
                    uses.set(p.texture, list);
                });
        }
        for (const f of files.filter((n) => /\.tex\.xbx$/i.test(n))) {
            let tex: texFile.TexFile;
            try {
                tex = texFile.readTex(fs.readFileSync(path.join(folder, f)), f);
            } catch {
                continue;
            }
            for (const t of tex.textures) {
                let alpha: TextureRow["alpha"] = "?";
                try {
                    alpha = hd.alphaKind(hd.topLevel(t));
                } catch {
                    alpha = "?";
                }
                rows.push({
                    level: name,
                    checksum: t.checksum.toString(16).padStart(8, "0"),
                    size: `${t.width}x${t.height}`,
                    alpha,
                    draw: drawKind(uses.get(t.checksum) ?? []),
                });
            }
        }
    }
    fs.mkdirSync(out, { recursive: true });
    fs.writeFileSync(
        path.join(out, "textures.tsv"),
        `level\tchecksum\tsize\tfile alpha\tdrawn\n${rows.map((r) => `${r.level}\t${r.checksum}\t${r.size}\t${r.alpha}\t${r.draw}`).join("\n")}\n`,
    );
    const table = new Map<string, number>();
    for (const r of rows) table.set(`${r.alpha}|${r.draw}`, (table.get(`${r.alpha}|${r.draw}`) ?? 0) + 1);
    const draws: DrawKind[] = ["solid", "cut-out", "blended", "additive", "later pass", "mixed", "unused"];
    const lines = [
        `textures of ${new Set(rows.map((r) => r.level)).size} levels: ${rows.length}`,
        `file alpha / drawn: ${draws.join(", ")}`,
    ];
    for (const alpha of ["opaque", "binary", "smooth", "?"])
        lines.push(`${alpha}: ${draws.map((d) => `${d} ${table.get(`${alpha}|${d}`) ?? 0}`).join(", ")}`);
    const smoothSolid = table.get("smooth|solid") ?? 0;
    const smoothCut = table.get("smooth|cut-out") ?? 0;
    lines.push(
        `soft alpha in the file but drawn solid (the alpha never looked at - a surface like any other: eligible for de-lighting): ${smoothSolid}`,
        `soft alpha in the file, drawn as a cut-out (alpha-tested: binary as drawn): ${smoothCut}`,
    );
    fs.writeFileSync(path.join(out, "textures.txt"), `${lines.join("\n")}\n`);
    for (const l of lines) print(l);
    return 0;
}

// --- the page ------------------------------------------------------------------------------------------------------------

const escapeHtml = (t: string): string => t.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
const dataUrl = (file: string): string =>
    fs.existsSync(file) ? `data:image/png;base64,${fs.readFileSync(file).toString("base64")}` : "";

function html(out: string): number {
    const parts: string[] = [];
    const census = path.join(out, "census");
    if (fs.existsSync(census)) {
        parts.push("<h2>Census</h2>");
        for (const f of fs.readdirSync(census).sort())
            parts.push(
                `<h3>${escapeHtml(f.replace(/\.txt$/, ""))}</h3><pre>${escapeHtml(fs.readFileSync(path.join(census, f), "utf8"))}</pre>`,
            );
    }
    for (const dir of fs
        .readdirSync(out)
        .filter((d) => d.startsWith("compare-"))
        .sort()) {
        const report = JSON.parse(fs.readFileSync(path.join(out, dir, "report.json"), "utf8")) as {
            a: string;
            b: string;
            medians: Record<string, number>;
            results: CompareResult[];
        };
        parts.push(`<h2>${escapeHtml(report.b)} against ${escapeHtml(report.a)}</h2>`);
        parts.push(
            `<table><tr><th>picture</th>${REGIONS.map((r) => `<th>${r}</th>`).join("")}</tr>` +
                report.results
                    .map(
                        (r) =>
                            `<tr${r.problems.length ? ' class="bad"' : ""}><td>${escapeHtml(r.picture)}</td>${REGIONS.map((g) => `<td>${r.ratios.ratio[g]?.toFixed(3) ?? "-"}</td>`).join("")}</tr>`,
                    )
                    .join("") +
                `<tr class="median"><td>median</td>${REGIONS.map((g) => `<td>${report.medians[g]?.toFixed(3) ?? "-"}</td>`).join("")}</tr></table>`,
        );
        const ranked = [...report.results].sort(
            (x, y) =>
                Math.abs(Math.log(y.ratios.ratio.whole ?? 1)) -
                Math.abs(Math.log(x.ratios.ratio.whole ?? 1)) +
                (y.worst[0]?.weight ?? 0) -
                (x.worst[0]?.weight ?? 0),
        );
        for (const r of ranked.slice(0, 12)) {
            parts.push(
                `<h3>${escapeHtml(r.picture)}</h3><div class="row"><figure><img src="${dataUrl(path.join(out, report.a, `${r.picture}.png`))}"><figcaption>${escapeHtml(report.a)}</figcaption></figure>` +
                    `<figure><img src="${dataUrl(path.join(out, report.b, `${r.picture}.png`))}"><figcaption>${escapeHtml(report.b)}</figcaption></figure>` +
                    `<figure><img src="${dataUrl(path.join(out, dir, `${r.picture}-heat.png`))}"><figcaption>blue darker, red brighter</figcaption></figure></div>` +
                    `<ul>${r.worst.map((c) => `<li>cell (${c.cx.toFixed(2)}, ${c.cy.toFixed(2)}) x${c.ratio.toFixed(2)}${c.probe ? ` - ${escapeHtml(c.probe)}` : ""}</li>`).join("")}</ul>`,
            );
        }
    }
    const page = `<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1"><title>Light audit</title>
<style>:root{--bg:#fff;--fg:#1b1b1f;--line:#d6d6dc;--bad:#fde2e2}@media (prefers-color-scheme: dark){:root{--bg:#141417;--fg:#e8e8ec;--line:#33333a;--bad:#4a2323}}
body{background:var(--bg);color:var(--fg);font:14px/1.45 system-ui,sans-serif;margin:0 auto;max-width:1400px;padding:16px}
table{border-collapse:collapse;margin:8px 0}td,th{border:1px solid var(--line);padding:2px 8px;text-align:right}td:first-child{text-align:left}
tr.bad{background:var(--bad)}tr.median{font-weight:600}.row{display:flex;gap:8px;flex-wrap:wrap}figure{margin:0;flex:1 1 300px}img{width:100%;height:auto}pre{white-space:pre-wrap;font-size:12px}</style></head>
<body><h1>Light audit</h1><p>Each light term counted once: the relit level with every ray-traced light on against the original's bake, at each level's own time of day. A ratio is the region's mean brightness over the reference's.</p>${parts.join("\n")}</body></html>`;
    fs.writeFileSync(path.join(out, "index.html"), page);
    print(`wrote ${path.join(out, "index.html")}`);
    return 0;
}

// --- the command line --------------------------------------------------------------------------------------------------

function parseSets(values: string[] | undefined): Record<string, string> {
    const out: Record<string, string> = {};
    for (const s of values ?? []) {
        const eq = s.indexOf("=");
        if (eq <= 0 || !s.slice(0, eq).includes(".")) exitWith(`--set takes Section.key=value (${s})`);
        out[s.slice(0, eq)] = s.slice(eq + 1);
    }
    return out;
}

async function main(): Promise<number> {
    const { values, positionals } = parseCli(USAGE, {
        options: {
            game: { type: "string" },
            build: { type: "string" },
            out: { type: "string" },
            levels: { type: "string" },
            restarts: { type: "string", default: "4" },
            owner: { type: "string", default: "light-audit" },
            set: { type: "string", multiple: true },
            config: { type: "string" },
            tag: { type: "string", default: "now" },
            probe: { type: "boolean", default: false },
            a: { type: "string" },
            b: { type: "string" },
            limits: { type: "string" },
        },
        allowPositionals: true,
    });
    const command = positionals[0];
    if (command === "textures") {
        const game = values.game ?? exitWith(USAGE);
        const out = path.resolve(values.out ?? exitWith(USAGE));
        return textureCensus(game, out, values.levels && values.levels !== "all" ? values.levels.split(",") : null);
    }
    if (command === "compare" || command === "html") {
        const out = positionals[1] ?? exitWith(USAGE);
        if (command === "html") return html(out);
        const limits = values.limits ? { ...LIMITS, ...(JSON.parse(values.limits) as Partial<typeof LIMITS>) } : LIMITS;
        return compare(out, values.a ?? exitWith(USAGE), values.b ?? exitWith(USAGE), limits);
    }
    if (command !== "walk" && command !== "capture") exitWith(USAGE);
    const levels =
        values.levels === "all"
            ? ORIGINAL_LEVELS
            : values.levels
              ? values.levels.split(",").filter(Boolean)
              : DEFAULT_LEVELS;
    const options: RunOptions = {
        game: values.game ?? exitWith(USAGE),
        build: path.resolve(values.build ?? exitWith(USAGE)),
        out: path.resolve(values.out ?? exitWith(USAGE)),
        levels,
        owner: values.owner,
        set: parseSets(values.set),
    };
    if (command === "walk") return walk(options, Math.max(1, Number(values.restarts) || 4));
    const config = values.config;
    if (config !== "bake" && config !== "full") exitWith("--config bake | full");
    return capture(options, config, values.tag, values.probe);
}

if (isMain(import.meta.url)) await runMain(main);
