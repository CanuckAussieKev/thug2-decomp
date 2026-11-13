// NOT ORIGINAL - one sun for every effect, checked in the game ([Graphics] rt_sun_source, engine/gfx/rt_scene.h
// scene_sun): every main level with rt_sun_source=level and =game, its shadows, lit faces and sun pictured side by side,
// and the sun each effect used read from the console (`rt sun`).
//
//   node tools/game/sun_source_check.ts run --game C:\Games\THUG2-<n> --out <folder> --build <folder> [--groups AP,BO]
//        [--owner <name>] [--set Section.key=value]...
//   node tools/game/sun_source_check.ts compare <folder>
//
// run: under the workspace's lock (rt_shadows_check.ts runAll - its ini, deployment and lock), first each level once
// with rt_sun_source=level to learn its suns (the console's `rt sun` after a few seconds: the game's and the baked one),
// then a plan made from them (<out>/plan.json): from each level's first restart (rt_sky.json's ground spot) the ground
// toward its heading, the ground seen from 700 units up looking down along it (the shadows), and from 300 up toward
// the game's sun and toward the baked sun (the disc, its glare and rays, the lit faces); each level run with `off`
// (rt_sun_source=level) and `on` (=game), `rt sun` again at the end. The other effects as the workspace's ini and
// --set have them (the user's look: --set Graphics.rt_shadows=1 ...).
//
// compare (run does it at the end): per level
//   - every effect that asked used the scene's sun: the shadows, the bounce, the fog, the drawn sun, the sky and relit
//     within 0.5 degrees of it (in a level with none: the shadows, bounce and fog none - the drawn sun and the sky
//     may show the game's sun, lighting nothing, and relit keeps the bake);
//   - off: the baked sun (or none: a level whose bake shows no sun); on: the game's - or the baked one, the level's
//     fallback (its game sun too low, or "sun": "level" in rt_relit_levels.json), reported;
//   - every picture: black (every channel 2 or less) at most 1% more of it than in off's (a NaN, a zero tint); its mean
//     luma within 0.6 - 1.6 times off's (brightness bounded);
// and side-by-side pictures <out>/side/<spot>.png (level | game). report.txt / report.json; exit 1 when a check fails.

import * as fs from "node:fs";
import * as path from "node:path";
import { fileURLToPath } from "node:url";
import { exitWith, isMain, parseCli, print, runMain } from "thug2-level-converter-core/lib/cli.ts";
import { encodePng, type Image } from "thug2-level-converter-core/lib/frame_diff.ts";
import { type Plan, type PlanGroup, readPicture, readPlan } from "./renderer_parity.ts";
import { runAll } from "./rt_shadows_check.ts";

const USAGE = `usage: sun_source_check.ts run --game <workspace> --out <folder> --build <folder> [--groups a,b] [--owner name] [--set S.key=v]...
       sun_source_check.ts compare <folder>

One sun for every effect: each main level with rt_sun_source=level and =game, pictured side by side (tools/game/README.md).`;

const HERE = path.dirname(fileURLToPath(import.meta.url));
const DEGREES = 180 / Math.PI;
const MOST_APART_DEGREES = 0.5;
const RELIT_FIT_DEGREES = 5;
const BLACK_LEVEL = 2;
const MOST_MORE_BLACK = 0.01;
const LUMA_RANGE: readonly [number, number] = [0.6, 1.6];
const SIDE_GAP = 8;

export type Vec3 = [number, number, number];
export interface SunPart {
    name: string;
    toward: Vec3 | null; // null: "none"
}
// The console's `rt sun` line: "sun: <origin> x y z (heading h, elevation e); game ...; baked ...; <effect> ...".
export interface SunReport {
    origin: string;
    scene: Vec3 | null;
    game: Vec3 | null;
    baked: Vec3 | null;
    users: SunPart[];
}

const PART = /^(.*?) (-?[\d.]+) (-?[\d.]+) (-?[\d.]+) \(heading (-?[\d.]+), elevation (-?[\d.]+)\)$/;
const ORIGINS = [
    "none (the bake shows no sun)",
    "waiting for the baked sun's fit",
    "the console's",
    "the game's",
    "the level's baked sun",
    "rt_sun_source's fixed direction",
];

export function parsePart(text: string): SunPart {
    const m = PART.exec(text.trim());
    if (m) return { name: m[1]!, toward: [Number(m[2]), Number(m[3]), Number(m[4])] };
    const none = /^(.*) none$/.exec(text.trim());
    return { name: none ? none[1]! : text.trim(), toward: null };
}

export function parseSunLine(line: string): SunReport | null {
    const at = line.indexOf("rt: sun: ");
    if (at < 0) return null;
    const parts = line
        .slice(at + "rt: sun: ".length)
        .trim()
        .split("; ");
    const first = parsePart(parts[0]!);
    const origin = ORIGINS.find((o) => parts[0]!.startsWith(o)) ?? first.name;
    const report: SunReport = { origin, scene: first.toward, game: null, baked: null, users: [] };
    for (const part of parts.slice(1)) {
        const p = parsePart(part);
        if (p.name === "game") report.game = p.toward;
        else if (p.name === "baked") report.baked = p.toward;
        else report.users.push(p);
    }
    return report;
}

// The last `rt sun` report of a log.
export function lastSunReport(log: string): SunReport | null {
    let last: SunReport | null = null;
    for (const line of log.split(/\r?\n/)) last = parseSunLine(line) ?? last;
    return last;
}

export function degreesApart(one: Vec3, other: Vec3): number {
    const length = (v: Vec3): number => Math.hypot(v[0], v[1], v[2]);
    const dot = (one[0] * other[0] + one[1] * other[1] + one[2] * other[2]) / (length(one) * length(other));
    return Math.acos(Math.max(-1, Math.min(1, dot))) * DEGREES;
}

export function headingOf(v: Vec3): number {
    return Math.atan2(v[0], v[2]) * DEGREES;
}
export function elevationOf(v: Vec3): number {
    return Math.asin(Math.max(-1, Math.min(1, v[1] / Math.hypot(v[0], v[1], v[2])))) * DEGREES;
}

// Where a level's spots stand: its rt_sky.json ground spot's teleport (x y z heading).
interface Stand {
    x: number;
    y: number;
    z: number;
    heading: number;
}
function standOf(group: PlanGroup): Stand | null {
    for (const s of group.spots ?? [])
        for (const c of s.commands) {
            const m = /^teleport (\S+) (\S+) (\S+) (\S+)$/.exec(c.trim());
            if (m) return { x: Number(m[1]), y: Number(m[2]), z: Number(m[3]), heading: Number(m[4]) };
        }
    return null;
}

const f3 = (n: number): string => n.toFixed(3);
// A camera command at `stand` raised by `up`, looking along heading / pitch (degrees).
function camera(stand: Stand, up: number, heading: number, pitch: number): string {
    const h = heading / DEGREES;
    const p = pitch / DEGREES;
    return `camera ${stand.x.toFixed(1)} ${(stand.y + up).toFixed(1)} ${stand.z.toFixed(1)} ${f3(Math.sin(h) * Math.cos(p))} ${f3(Math.sin(p))} ${f3(Math.cos(h) * Math.cos(p))}`;
}

// The discovery plan: each level's ground spot, then `rt sun`.
export function discoveryPlan(sky: Plan): Plan {
    return {
        groups: sky.groups.map((g) => ({
            name: g.name,
            ...(g.level ? { level: g.level } : {}),
            // (in the spot's commands: rt_shadows_check runs a group's spots only)
            spots: (g.spots ?? [])
                .slice(0, 1)
                .map((s) => ({ name: s.name, commands: [...s.commands, "wait 120", "console rt sun"] })),
        })),
    };
}

// The check's plan from each level's suns: the ground, the ground from above, toward the game's sun and the baked one.
export function sunPlan(sky: Plan, suns: Record<string, SunReport>): Plan {
    const groups: PlanGroup[] = [];
    for (const g of sky.groups) {
        const stand = standOf(g);
        if (!stand || !g.level) continue;
        const ground = (g.spots ?? [])[0]!;
        const spots = [
            { name: `${g.name}-ground`, commands: ground.commands },
            { name: `${g.name}-above`, commands: [camera(stand, 700, stand.heading, -40), "wait 60"] },
        ];
        const report = suns[g.name];
        for (const [which, toward] of [
            ["game", report?.game ?? null],
            ["baked", report?.baked ?? null],
        ] as const) {
            if (!toward) continue;
            // (the disc in the picture's upper part, the ground toward it below)
            const pitch = Math.max(-10, Math.min(55, elevationOf(toward) - 14));
            spots.push({
                name: `${g.name}-sun-${which}`,
                commands: [camera(stand, 300, headingOf(toward), pitch), "wait 60"],
            });
        }
        // (`rt sun` at the end of the last spot: rt_shadows_check runs a group's spots only)
        const last = spots[spots.length - 1]!;
        last.commands = [...last.commands, "console rt sun"];
        groups.push({ name: g.name, level: g.level, spots });
    }
    return { groups };
}

// --- comparing ---------------------------------------------------------------------------------------------------------

const lumaAt = (rgb: Uint8Array, p: number): number =>
    0.299 * rgb[p * 3]! + 0.587 * rgb[p * 3 + 1]! + 0.114 * rgb[p * 3 + 2]!;

export interface PictureStats {
    luma: number;
    black: number; // share
}
export function pictureStats(image: Image): PictureStats {
    const pixels = image.width * image.height;
    let luma = 0;
    let black = 0;
    for (let p = 0; p < pixels; p++) {
        luma += lumaAt(image.rgb, p);
        const i = p * 3;
        if (image.rgb[i]! <= BLACK_LEVEL && image.rgb[i + 1]! <= BLACK_LEVEL && image.rgb[i + 2]! <= BLACK_LEVEL)
            black++;
    }
    return { luma: luma / Math.max(1, pixels), black: black / Math.max(1, pixels) };
}

// Two pictures side by side (the left's height; a grey gap between).
export function sideBySide(left: Image, right: Image): Image {
    const height = Math.max(left.height, right.height);
    const width = left.width + SIDE_GAP + right.width;
    const rgb = new Uint8Array(width * height * 3).fill(64);
    for (const [image, x0] of [
        [left, 0],
        [right, left.width + SIDE_GAP],
    ] as const)
        for (let y = 0; y < image.height; y++)
            rgb.set(image.rgb.subarray(y * image.width * 3, (y + 1) * image.width * 3), (y * width + x0) * 3);
    return { width, height, rgb };
}

// The effects' suns against the scene's: the problems (none: they agree).
export function sunProblems(report: SunReport): string[] {
    const problems: string[] = [];
    for (const user of report.users) {
        if (!report.scene) {
            // (no scene sun: the lighting effects have none; the drawn sun and the sky may show the game's, lighting
            // nothing, and relit keeps the bake)
            if (user.toward && (user.name === "shadows" || user.name === "bounce" || user.name === "fog"))
                problems.push(`${user.name} has a sun where the scene has none`);
            continue;
        }
        // (relit at the baked sun lights by its model's own fit - the cache's, kept so the bake comes back exactly -
        // which a new load's fit finds again within a few degrees)
        const most =
            user.name === "relit" && report.origin === "the level's baked sun" ? RELIT_FIT_DEGREES : MOST_APART_DEGREES;
        if (!user.toward) problems.push(`${user.name} has no sun`);
        else if (degreesApart(user.toward, report.scene) > most)
            problems.push(`${user.name} ${degreesApart(user.toward, report.scene).toFixed(2)} degrees off the scene's`);
    }
    return problems;
}

function describe(v: Vec3 | null): string {
    return v ? `heading ${headingOf(v).toFixed(0)}, ${elevationOf(v).toFixed(0)} up` : "none";
}

function compareFolder(out: string): number {
    const lines: string[] = [];
    let failed = false;
    const fail = (line: string): void => {
        failed = true;
        lines.push(`FAIL ${line}`);
    };
    const folder = (config: string): string => path.join(out, config, "run1");
    if (!fs.existsSync(folder("off")) || !fs.existsSync(folder("on"))) exitWith(`${out} has no off/run1 and on/run1`);
    fs.mkdirSync(path.join(out, "side"), { recursive: true });
    const levels = fs
        .readdirSync(folder("off"))
        .filter((f) => f.endsWith(".log"))
        .map((f) => f.slice(0, -4))
        .sort();
    const json: Record<string, unknown> = {};
    for (const level of levels) {
        const reports: Record<string, SunReport | null> = {};
        for (const config of ["off", "on"]) {
            const log = path.join(folder(config), `${level}.log`);
            reports[config] = fs.existsSync(log) ? lastSunReport(fs.readFileSync(log, "utf8")) : null;
        }
        const off = reports.off;
        const on = reports.on;
        if (!off || !on) {
            fail(`${level}: no \`rt sun\` line in ${!off ? "off" : "on"}'s log`);
            continue;
        }
        lines.push(
            `${level}: game sun ${describe(on.game)}, baked ${describe(on.baked)} - level: ${off.origin}; game: ${on.origin}`,
        );
        for (const [config, report] of [
            ["level", off],
            ["game", on],
        ] as const) {
            const problems = sunProblems(report);
            if (problems.length) fail(`${level} (${config}): ${problems.join(", ")}`);
            else
                lines.push(
                    `  ${config}: ${report.users.length} effects on the scene's sun (${report.users.map((u) => u.name).join(", ")})`,
                );
        }
        if (off.origin !== "the level's baked sun" && !off.origin.startsWith("none"))
            fail(`${level}: level's sun is ${off.origin}`);
        if (on.origin !== "the game's" && on.origin !== "the level's baked sun" && !on.origin.startsWith("none"))
            fail(`${level}: game's sun is ${on.origin}`);
        const pictures: Record<string, unknown> = {};
        for (const file of fs
            .readdirSync(folder("off"))
            .filter((f) => f.endsWith(".png") && f.startsWith(`${level}-`))) {
            const other = path.join(folder("on"), file);
            if (!fs.existsSync(other)) {
                fail(`${file}: no picture in on`);
                continue;
            }
            const left = readPicture(path.join(folder("off"), file));
            const right = readPicture(other);
            fs.writeFileSync(path.join(out, "side", file), encodePng(sideBySide(left, right)));
            const a = pictureStats(left);
            const b = pictureStats(right);
            const ratio = b.luma / Math.max(1, a.luma);
            pictures[file] = { level: a, game: b, ratio };
            const note = `  ${file}: mean luma ${a.luma.toFixed(1)} -> ${b.luma.toFixed(1)} (x${ratio.toFixed(2)}), black ${(100 * a.black).toFixed(2)}% -> ${(100 * b.black).toFixed(2)}%`;
            if (b.black > a.black + MOST_MORE_BLACK) fail(`${note} - turned black`);
            else if (ratio < LUMA_RANGE[0] || ratio > LUMA_RANGE[1]) fail(`${note} - brightness out of bounds`);
            else lines.push(note);
        }
        json[level] = { level: off, game: on, pictures };
    }
    lines.push(failed ? "rt_sun_source check: FAILED" : "rt_sun_source check: passed");
    fs.writeFileSync(path.join(out, "report.txt"), `${lines.join("\n")}\n`);
    fs.writeFileSync(path.join(out, "report.json"), `${JSON.stringify(json, null, 1)}\n`);
    for (const l of lines) print(l);
    return failed ? 1 : 0;
}

// --- running -----------------------------------------------------------------------------------------------------------

async function runCheck(options: {
    game: string;
    out: string;
    build: string;
    groups: string[] | null;
    owner: string;
    set: Record<string, string>;
}): Promise<number> {
    const sky = readPlan(fs.readFileSync(path.join(HERE, "rt_sky.json"), "utf8"));
    sky.groups = sky.groups.filter((g) => g.level && (!options.groups || options.groups.includes(g.name)));
    fs.mkdirSync(options.out, { recursive: true });
    const discover = path.join(options.out, "discover");
    const discoverPlan = path.join(options.out, "discover.json");
    fs.writeFileSync(discoverPlan, `${JSON.stringify(discoveryPlan(sky), null, 1)}\n`);
    const common = {
        game: options.game,
        build: options.build,
        baselineBuild: null,
        groups: null,
        runs: 1,
        owner: options.owner,
        set: options.set,
        effect: "sun-source" as const,
    };
    let code = await runAll({ ...common, out: discover, plan: discoverPlan, configs: ["off"] });
    if (code !== 0) return code;
    const suns: Record<string, SunReport> = {};
    for (const g of sky.groups) {
        const log = path.join(discover, "off", "run1", `${g.name}.log`);
        const report = fs.existsSync(log) ? lastSunReport(fs.readFileSync(log, "utf8")) : null;
        if (report) suns[g.name] = report;
        else print(`${g.name}: no \`rt sun\` line in the discovery run - its sun spots left out`);
    }
    fs.writeFileSync(path.join(options.out, "suns.json"), `${JSON.stringify(suns, null, 1)}\n`);
    const planFile = path.join(options.out, "plan.json");
    fs.writeFileSync(planFile, `${JSON.stringify(sunPlan(sky, suns), null, 1)}\n`);
    code = await runAll({ ...common, out: options.out, plan: planFile, configs: ["off", "on"] });
    return code !== 0 ? code : compareFolder(options.out);
}

async function main(): Promise<number> {
    const { values, positionals } = parseCli(USAGE, {
        options: {
            game: { type: "string" },
            out: { type: "string" },
            build: { type: "string" },
            groups: { type: "string" },
            owner: { type: "string", default: "sun-source-check" },
            set: { type: "string", multiple: true },
        },
        allowPositionals: true,
    });
    const [command, target] = positionals;
    const set: Record<string, string> = {};
    for (const s of values.set ?? []) {
        const eq = s.indexOf("=");
        if (eq <= 0) exitWith(`--set Section.key=value (${s})`);
        set[s.slice(0, eq)] = s.slice(eq + 1);
    }
    if (command === "compare" && target && positionals.length === 2) return compareFolder(path.resolve(target));
    if (command === "run" && positionals.length === 1) {
        if (!values.game || !values.out || !values.build) exitWith("run needs --game, --out and --build");
        return runCheck({
            game: path.resolve(values.game),
            out: path.resolve(values.out),
            build: path.resolve(values.build),
            groups: values.groups ? values.groups.split(",") : null,
            owner: values.owner,
            set,
        });
    }
    print(USAGE);
    return 2;
}

if (isMain(import.meta.url)) await runMain(() => main());
