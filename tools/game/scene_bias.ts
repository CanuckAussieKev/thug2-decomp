// NOT ORIGINAL - the realtime picture's whole-scene light against the path-traced reference (round BOUNCE): one fixed
// procedure, so every lighting round measures the same thing and its numbers can be compared with the last round's.
//
//   node tools/game/scene_bias.ts plan [--spots no,p1,ba,au,tr,bah] [--tods noon,night] [--samples 256] [--tag t]
//        [--out commands.txt]
//     the automation commands of the session (run_game.ps1 -Commands (Get-Content commands.txt) in a workspace): for each
//     time of day (`console rt tod <tod>` before the levels load), for each spot: the level, the skater teleported, the
//     camera held at the spot's pose, `console measure on` (the world frozen, every frame drawn), kConvergeFrames frames
//     for the reservoirs, the radiance cache and Ray Reconstruction to settle, then `pt ref <samples> as
//     scene_<tag>_<spot>_<tod>` - the reference and the realtime frame (Ray Reconstruction's output, linear, in the global
//     unit) saved side by side.
//   node tools/game/scene_bias.ts terms <spot> [--tods night] [--samples 256] [--tag t] [--out commands.txt]
//     one spot's light term by term at one time of day (motion_error.ts terms-analyse <Screenshots> <spot> --tag t reads
//     it): every rt_debug_view converged, Ray Reconstruction's input against its output (`pt upscaler`), the full view's
//     inputs, the references of motion_error.ts's terms + next-event / BSDF alone + each kind's whole light
//     (--refs full,direct,...: only those references; --realtime-only: none - a variant measured against references
//     taken before).
//   node tools/game/scene_bias.ts terms-analyse <Screenshots folder> <spot> [--tag t] [--ref-tag r] [--grid 4x3] [--json f]
//     the term table (realtime views of --tag, each view's frames averaged, against the references of --ref-tag - the
//     same by default), whole and per cell, and the reference's own split (next-event alone, BSDF alone, each kind).
//   node tools/game/scene_bias.ts analyse <log> [--screenshots <folder>] [--tag t] [--json out.json]
//     the table: per spot and time of day the log's mean-luma ratio (realtime / reference, the `pt: saved` line) and,
//     from the EXRs, the trimmed ratio - the pixels where either picture is over 20 x the reference's 99th percentile
//     (emitters seen directly, sparks, spray: a few pixels that decide a whole-frame mean) left out of both.
//
// The spots: motion_error.ts kSpots (BO p1 / p2 / p3, BA's fountain, NO's street, AU's lamp pool), Training's (the
// user's 2026-11-23 18-33-42 sidecar) and BA's harbour (round WATER: the user's 2026-11-25 17-16-26 - at dusk as well).
// The reference's spp, the converge frames and the trim are the procedure: change
// them only with a note in the round's record (the numbers stop being comparable with the rounds before).
import * as fs from "node:fs";
import * as path from "node:path";
import { isMain, parseCli, print, runMain } from "thug2-level-converter-core/lib/cli.ts";
import {
    cellMeans,
    globalUnit,
    kSpots,
    kTermConvergeFrames,
    kTermReferences,
    kTermViews,
    luma,
    type Picture,
    readExr,
    type Spot,
    termRows,
    type Vec3,
} from "./motion_error.ts";

const USAGE =
    "usage: scene_bias.ts plan [--spots no,p1,ba,au,tr,bah] [--tods noon,night] [--samples 256] [--tag t] [--out file]\n" +
    "       scene_bias.ts terms <spot> [--tods night] [--samples 256] [--tag t] [--realtime-only] [--out file]\n" +
    "       scene_bias.ts terms-analyse <Screenshots folder> <spot> [--tag t] [--ref-tag r] [--grid CxR] [--json file]\n" +
    "       scene_bias.ts analyse <thug2-decomp.log> [--screenshots folder] [--tag t] [--json out.json]";

// A spot of the plan: motion_error.ts's, with the times of day it is measured at besides the plan's.
export interface SceneSpot extends Spot {
    tods?: string[];
}
export const kSceneSpots: SceneSpot[] = [
    ...kSpots,
    {
        // Training's start (the user's 2026-11-23 18-33-42 sidecar): indoors, its light through the roof openings
        name: "tr",
        level: "tr",
        skater: { x: -2670.9, y: -663.2, z: -1168.3 },
        heading: 8.4,
        camera: { x: -2687.7, y: -585.9, z: -1309.0 },
        direction: { x: 0.117, y: -0.179, z: 0.977 },
    },
    {
        // (round WATER) Barcelona's harbour from the pier (the user's 2026-11-25 17-16-26 sidecar): the open sea at a
        // grazing angle - its reflection of the sky and the buildings; measured at dusk too (the sunset in the water)
        name: "bah",
        level: "ba",
        skater: { x: 2155.8, y: 0.0, z: 3617.7 },
        heading: -124.5,
        camera: { x: 2038.4, y: 77.4, z: 3538.3 },
        direction: { x: 0.815, y: -0.179, z: 0.551 },
        tods: ["dusk"],
    },
];
export const kDefaultSpots = ["no", "p1", "ba", "au", "tr", "bah"];
export const kDefaultTods = ["noon", "night"];
export const kConvergeFrames = 500;
export const kDefaultSamples = 256;
export const kTrimFactor = 20; // x the reference's 99th percentile
export const kTermFrames = 8; // (a term view: its frames averaged - one Ray Reconstruction frame keeps a few % of noise)

function fixed(v: Vec3, digits: number): string {
    return `${v.x.toFixed(digits)} ${v.y.toFixed(digits)} ${v.z.toFixed(digits)}`;
}

export function shotName(tag: string, spot: string, tod: string): string {
    return `scene_${tag ? tag + "_" : ""}${spot}_${tod}`;
}

// A spot is measured at the plan's times of day and at its own (SceneSpot.tods - after the plan's).
export function planCommands(spots: SceneSpot[], tods: string[], samples: number, tag: string): string[] {
    const out: string[] = ["waitfor ready 600"];
    const all = [...tods];
    for (const spot of spots) {
        for (const tod of spot.tods ?? []) {
            if (!all.includes(tod)) all.push(tod);
        }
    }
    for (const tod of all) {
        const here = spots.filter((s) => tods.includes(tod) || (s.tods ?? []).includes(tod));
        if (here.length === 0) continue;
        out.push(`console rt tod ${tod}`, "wait 5");
        for (const spot of here) {
            const level = spot.level ?? "bo";
            out.push(
                `level ${level}`,
                `waitfor level ${level} 600`,
                "wait 120",
                `teleport ${fixed(spot.skater, 1)} ${spot.heading}`,
                "wait 90",
                `camera ${fixed(spot.camera, 5)} ${fixed(spot.direction, 6)}`,
                "console measure on",
                `wait ${kConvergeFrames}`,
                `console pt ref ${samples} as ${shotName(tag, spot.name, tod)}`,
                "waitfor pt 3000",
                "console pt off",
                "wait 5",
                "console measure off",
                "camera off",
                "wait 5",
            );
        }
    }
    return out;
}

// The references a term session takes besides motion_error.ts kTermReferences: the light by next-event estimation
// alone and by the BSDF's rays alone (each the whole - their difference is light the list can't sample), and each kind's
// whole light (every bounce).
export const kExtraReferences: { name: string; words: string }[] = [
    { name: "nee", words: "nee" },
    { name: "bsdf", words: "bsdf" },
    { name: "emit", words: "only emit" },
    { name: "lamps", words: "only lamps" },
    { name: "distant", words: "only disc,sky" },
];
// One spot's light term by term (motion_error.ts terms-analyse reads the result: --tag the same), at a time of day: the
// level loaded at it, the realtime views converged, Ray Reconstruction's input against its output (`pt upscaler`), the
// full view's inputs saved, then the references.
// `references`: the names of the references to take (every one of kTermReferences + kExtraReferences by default; none:
// the realtime side alone).
export function planTerms(
    spot: Spot,
    tod: string,
    samples: number,
    tag: string,
    references: string[] = [...kTermReferences, ...kExtraReferences].map((r) => r.name),
): string[] {
    const level = spot.level ?? "bo";
    const folder = `bias/${spot.name}${tag}`;
    const out: string[] = [
        "waitfor ready 600",
        `console rt tod ${tod}`,
        "wait 5",
        `level ${level}`,
        `waitfor level ${level} 600`,
        "wait 120",
        `teleport ${fixed(spot.skater, 1)} ${spot.heading}`,
        "wait 90",
        `camera ${fixed(spot.camera, 5)} ${fixed(spot.direction, 6)}`,
        "console measure on",
        `wait ${kConvergeFrames}`,
        "console pt upscaler 120",
        "wait 130",
    ];
    for (const view of kTermViews) {
        out.push(`console set rt_debug_view ${view}`, `wait ${kTermConvergeFrames}`);
        out.push(
            `console pt frames ${kTermFrames} ${folder}/rt_${view} 1${view === "off" ? " inputs" : ""}`,
            `wait ${kTermFrames + 4}`,
        );
    }
    out.push("console set rt_debug_view off", `wait ${kTermConvergeFrames}`);
    for (const ref of [...kTermReferences, ...kExtraReferences].filter((r) => references.includes(r.name))) {
        const words = ref.words ? ` ${ref.words}` : "";
        out.push(
            `console pt ref ${samples}${words} as bias_${spot.name}${tag}_${ref.name}`,
            "waitfor pt 3000",
            "console pt off",
            "wait 5",
        );
    }
    out.push("console measure off", "camera off", "wait 5");
    return out;
}

export interface SavedLine {
    name: string;
    reference: number;
    realtime: number;
    ratio: number;
}
// `pt: saved <folder>\<name>_reference / _realtime ... mean luma reference R, realtime T (x Q of the reference)`
export function parseSaved(log: string): SavedLine[] {
    const out: SavedLine[] = [];
    const re =
        /pt: saved (.*?)_reference \/ _realtime .*?mean luma reference ([-+0-9.eE]+), realtime ([-+0-9.eE]+) \(x ([-+0-9.eE]+) of the reference\)/g;
    for (const m of log.matchAll(re)) {
        out.push({
            name: path.win32.basename(m[1]!),
            reference: Number(m[2]),
            realtime: Number(m[3]),
            ratio: Number(m[4]),
        });
    }
    return out;
}

function lumas(p: Picture): Float64Array {
    const out = new Float64Array(p.width * p.height);
    for (let i = 0; i < out.length; i++) out[i] = luma(p.rgb[i * 3]!, p.rgb[i * 3 + 1]!, p.rgb[i * 3 + 2]!);
    return out;
}
export function percentile(values: Float64Array, q: number): number {
    const sorted = Float64Array.from(values.filter((v) => Number.isFinite(v))).sort();
    if (sorted.length === 0) return 0;
    return sorted[Math.min(sorted.length - 1, Math.floor(q * sorted.length))]!;
}
// The whole ratio and the trimmed one (pixels where either picture is over kTrimFactor x the reference's p99 left out).
export function ratios(
    realtime: Float64Array,
    reference: Float64Array,
): { whole: number; trimmed: number; kept: number } {
    const limit = kTrimFactor * percentile(reference, 0.99);
    let a = 0;
    let b = 0;
    let ta = 0;
    let tb = 0;
    let kept = 0;
    for (let i = 0; i < reference.length; i++) {
        const x = realtime[i]!;
        const y = reference[i]!;
        if (!Number.isFinite(x) || !Number.isFinite(y)) continue;
        a += x;
        b += y;
        if (x > limit || y > limit) continue;
        ta += x;
        tb += y;
        kept++;
    }
    return { whole: b > 0 ? a / b : 0, trimmed: tb > 0 ? ta / tb : 0, kept: kept / Math.max(1, reference.length) };
}

function analyse(logFile: string, screenshots: string, tag: string, json?: string): number {
    const saved = parseSaved(fs.readFileSync(logFile, "utf8"));
    const prefix = `scene_${tag ? tag + "_" : ""}`;
    const rows: Record<string, unknown>[] = [];
    print(`spot / tod        reference   realtime   log ratio   EXR whole  trimmed (kept)`);
    const latest = new Map<string, SavedLine>();
    for (const s of saved) if (s.name.startsWith(prefix)) latest.set(s.name, s); // a rerun's last
    for (const [name, s] of latest) {
        const key = name.slice(prefix.length);
        const ref = path.join(screenshots, `${name}_reference.exr`);
        const rt = path.join(screenshots, `${name}_realtime.exr`);
        let exr = { whole: Number.NaN, trimmed: Number.NaN, kept: Number.NaN };
        if (fs.existsSync(ref) && fs.existsSync(rt))
            exr = ratios(lumas(readExr(fs.readFileSync(rt))), lumas(readExr(fs.readFileSync(ref))));
        print(
            `${key.padEnd(16)} ${s.reference.toPrecision(4).padStart(10)} ${s.realtime.toPrecision(4).padStart(10)}` +
                `   ${s.ratio.toFixed(3).padStart(8)}   ${exr.whole.toFixed(3).padStart(8)}  ${exr.trimmed.toFixed(3)}` +
                ` (${(exr.kept * 100).toFixed(2)} %)`,
        );
        rows.push({ spot: key, ...s, exr });
    }
    if (json) fs.writeFileSync(json, JSON.stringify(rows, null, 1));
    return 0;
}

// A view's frames (frame_NNNN.exr in its folder, each in the global unit) averaged per cell.
function viewMeans(folder: string, cols: number, rows: number): number[] | undefined {
    if (!fs.existsSync(folder)) return undefined;
    const files = fs
        .readdirSync(folder)
        .filter((f) => /^frame_\d+\.exr$/.test(f))
        .sort();
    if (files.length === 0) return undefined;
    let sum: number[] | undefined;
    for (const f of files) {
        const m = cellMeans(globalUnit(path.join(folder, f)), cols, rows);
        sum = sum ? sum.map((v, i) => v + m[i]!) : m;
    }
    return sum!.map((v) => v / files.length);
}

// The term table (motion_error.ts termRows) with the realtime views of `tag` against the references of `refTag`, then
// the reference's own split: next-event estimation alone and the BSDF's rays alone against the whole, each kind's share.
function termsAnalyse(
    root: string,
    spotName: string,
    tag: string,
    refTag: string,
    grid: string,
    json?: string,
): number {
    const [cols, rows] = grid.split("x").map(Number) as [number, number];
    const rt = new Map<string, number[]>();
    const ref = new Map<string, number[]>();
    for (const view of kTermViews) {
        const m = viewMeans(path.join(root, "bias", `${spotName}${tag}`, `rt_${view}`), cols, rows);
        if (m) rt.set(view, m);
    }
    for (const r of [...kTermReferences, ...kExtraReferences]) {
        const file = path.join(root, `bias_${spotName}${refTag}_${r.name}_reference.exr`);
        if (fs.existsSync(file)) ref.set(r.name, cellMeans(readExr(fs.readFileSync(file)), cols, rows));
    }
    const n = cols * rows;
    const f = (v: number): string => (Number.isFinite(v) ? v.toPrecision(3) : "-");
    print(
        `${spotName}${tag} against the references ${spotName}${refTag}: realtime / reference (mean luma, global unit)`,
    );
    const out = termRows(rt, ref);
    for (const row of out) {
        const a = row.realtime[n]!;
        const b = row.reference[n]!;
        const ratio = Number.isFinite(a) && Number.isFinite(b) && b !== 0 ? (a / b).toFixed(3) : "-";
        print(`${row.term.padEnd(20)} ${f(a).padStart(9)} ${f(b).padStart(9)}  x ${ratio}`);
        if (!Number.isFinite(a) || !Number.isFinite(b)) continue;
        for (let r = 0; r < rows; r++) {
            const cells: string[] = [];
            for (let c = 0; c < cols; c++) {
                const i = r * cols + c;
                const x = row.reference[i]! !== 0 ? row.realtime[i]! / row.reference[i]! : Number.NaN;
                cells.push((Number.isFinite(x) ? x.toFixed(2) : "-").padStart(6));
            }
            print(`${"".padEnd(24)}${cells.join("")}`);
        }
    }
    const whole = ref.get("full")?.[n];
    if (whole) {
        print(`the reference's split (of its whole ${f(whole)}):`);
        for (const name of ["full2", "nee", "bsdf", "direct", "b2", "emit", "lamps", "distant"]) {
            const v = ref.get(name)?.[n];
            if (v !== undefined) print(`  ${name.padEnd(10)} ${f(v).padStart(9)}  x ${(v / whole).toFixed(3)}`);
        }
    }
    if (json) fs.writeFileSync(json, JSON.stringify({ spot: spotName + tag, refTag, grid, rows: out }, null, 1));
    return 0;
}

function main(): number {
    const { values, positionals } = parseCli(USAGE, {
        options: {
            spots: { type: "string", default: kDefaultSpots.join(",") },
            tods: { type: "string", default: kDefaultTods.join(",") },
            samples: { type: "string", default: String(kDefaultSamples) },
            tag: { type: "string", default: "" },
            out: { type: "string" },
            screenshots: { type: "string" },
            json: { type: "string" },
            "realtime-only": { type: "boolean", default: false },
            "ref-tag": { type: "string" },
            refs: { type: "string" },
            grid: { type: "string", default: "4x3" },
        },
        allowPositionals: true,
    });
    const [verb, ...rest] = positionals;
    const tag = values.tag as string;
    if (verb === "plan" && rest.length === 0) {
        const names = (values.spots as string).split(",").filter(Boolean);
        const spots: SceneSpot[] = [];
        for (const n of names) {
            const s = kSceneSpots.find((x) => x.name === n);
            if (!s) {
                print(`no spot ${n} (${kSceneSpots.map((x) => x.name).join(", ")})`);
                return 2;
            }
            spots.push(s);
        }
        const tods = (values.tods as string).split(",").filter(Boolean);
        const text = planCommands(spots, tods, Number(values.samples), tag).join("\n") + "\n";
        if (values.out) fs.writeFileSync(values.out as string, text);
        else process.stdout.write(text);
        return 0;
    }
    if (verb === "terms" && rest.length === 1) {
        const spot = kSceneSpots.find((x) => x.name === rest[0]);
        const tod = (values.tods as string).split(",")[0]!;
        if (!spot) {
            print(`no spot ${rest[0]} (${kSceneSpots.map((x) => x.name).join(", ")})`);
            return 2;
        }
        const text =
            planTerms(
                spot,
                tod,
                Number(values.samples),
                tag,
                values["realtime-only"] ? [] : values.refs ? (values.refs as string).split(",") : undefined,
            ).join("\n") + "\n";
        if (values.out) fs.writeFileSync(values.out as string, text);
        else process.stdout.write(text);
        return 0;
    }
    if (verb === "terms-analyse" && rest.length === 2) {
        const refTag = (values["ref-tag"] as string | undefined) ?? tag;
        return termsAnalyse(rest[0]!, rest[1]!, tag, refTag, values.grid as string, values.json as string | undefined);
    }
    if (verb === "analyse" && rest.length === 1) {
        const log = rest[0]!;
        const shots = (values.screenshots as string | undefined) ?? path.join(path.dirname(log), "Screenshots");
        return analyse(log, shots, tag, values.json as string | undefined);
    }
    print(USAGE);
    return 2;
}

if (isMain(import.meta.url)) await runMain(() => main());
