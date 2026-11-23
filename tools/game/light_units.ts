// NOT ORIGINAL - the light units census (docs/design/vulkan.md "Round UNITS"): every level's lamps, emitting surfaces
// and fog through the one physical-unit conversion the renderer applies - candela and lumens for the lamps, cd/m^2
// (nits) for the emitting surfaces, extinction per metre for the fog -, with the minimum, median, maximum and the
// outliers of each.
//
//   node tools/game/light_units.ts census --game <folder> [--json <file>] [--levels BO,BA,...]
//     <folder>: a game install (the main one is only read). The lamps and emitting triangles come from the relit data
//     (thug2-decomp-relit/<scene>.relit - engine/gfx/relit_data.cpp's layout, version 18), the fog from the level
//     structures and the time of day's states in the decompiled scripts (Data/scripts/game/Levels.ns,
//     Data/scripts/engine/tod_manager.ns), where the install has them.
//
// The conversions (the C++ is the source; these mirror it):
//   - a lamp: its intensity (cd) and flux (lm) as the bake wrote them (rt_relit.cpp relit_lamp_photometry); its reach
//     before - the data's (its size's, or the original light's outer radius) - and the illuminance its light still had
//     there (I / r^2: the edge a pool showed); after, rt_lights.h lamp_reach: sqrt(I / kLampReachLux) - I the brightest
//     channel's intensity.
//   - an emitting triangle: its radiance in the global unit x unit_illuminance / pi (nits); unit_illuminance (lux on a
//     level surface a unit lights) told from the data's own lamps (candela = luma x unit_lux x core^2).
//   - the fog (rt_volume_light.h level_fog_extinction): the game's SetFogDistance start, end = start x 4 (or start +
//     7200 with distance clipping - pc_fog.cpp); extinction 3.912 / (end - start) per world unit (Koschmieder), the
//     visibility (MOR at 2 %) end - start; against clear night air (40 km) the ratio.

import * as fs from "node:fs";
import * as path from "node:path";
import { checksum } from "thug2-level-converter-core/level_port/qb.ts";
import { exitWith, isMain, parseCli, print, runMain } from "thug2-level-converter-core/lib/cli.ts";

const USAGE = `usage: node tools/game/light_units.ts census --game <folder> [--json <file>] [--levels a,b]`;

const RELIT_MAGIC = 0x44523254; // "T2RD"
const RELIT_VERSION = 18;
const RELIT_OLDEST = 17; // (v17: the lamps as v18 has them)
const METRES_PER_UNIT = 1 / 39.37;
const LAMP_REACH_LUX = 1.0e-3; // rt_lights.h kLampReachLux
const KOSCHMIEDER = 3.912; // rt_sky.h kKoschmieder
const CLEAR_VISIBILITY = 40000; // rt_sky.h kClearVisibility (metres)
const DISTANCE_CLIPPING_EXTRA = 7200; // pc_fog.cpp

const SHAPES = ["point", "panel", "line"] as const;
const KINDS = ["glow", "self-lit", "point", "textured", "pool"] as const;
const SOURCES = ["none", "solved", "model", "implausible", "own surface", "not a fixture", "game light"] as const;

export interface Lamp {
    position: [number, number, number];
    light: [number, number, number];
    core: number;
    reach: number;
    shape: number;
    halfLength: number;
    candela: number;
    lumens: number;
    kelvin: number;
    kind: number;
    source: number;
    luminance: number;
    gameLight: number;
}
export interface Emissive {
    corners: number[];
    radiance: [number, number, number];
    lamp: number;
}
export interface RelitHead {
    version: number;
    scene: number;
    lamps: Lamp[];
    emissive: Emissive[];
}

// Reads a relit file up to its emitting triangles (the meshes after them aren't needed).
export function readRelit(bytes: Buffer): RelitHead | null {
    let at = 0;
    const u32 = () => {
        const v = bytes.readUInt32LE(at);
        at += 4;
        return v;
    };
    const f32 = () => {
        const v = bytes.readFloatLE(at);
        at += 4;
        return v;
    };
    const f3 = (): [number, number, number] => [f32(), f32(), f32()];
    if (bytes.length < 64 || bytes.readUInt32LE(0) !== RELIT_MAGIC) return null;
    u32();
    const version = u32();
    if (version < RELIT_OLDEST || version > RELIT_VERSION) return null; // (v18 changed only the meshes)
    f32(); // sun radius
    const scene = u32();
    at += 8; // content
    at += 4 * 4; // verdict, sky rays, sun rays, lamps
    at += 4 * 3; // margins, facing
    at += 4 + 12 + 4 + 4 + 4; // the baked sun: found, toward, confidence, ground known, ground
    at += 4 + 4 + 12 * 3 + 4 * 3 + 12 * 2 + 4 + 8 + 4; // the model
    const lampCount = u32();
    const lamps: Lamp[] = [];
    for (let i = 0; i < lampCount; ++i) {
        const position = f3();
        f32(); // size
        const light = f3();
        const core = f32();
        const reach = f32();
        const shape = u32();
        f3(); // axis
        const halfLength = f32();
        const candela = f32();
        const lumens = f32();
        const kelvin = f32();
        u32(); // solved
        const kind = u32();
        f32(); // switch sky
        const source = u32();
        u32(); // fixture
        f32(); // area
        const luminance = f32();
        const gameLight = u32();
        lamps.push({
            position,
            light,
            core,
            reach,
            shape,
            halfLength,
            candela,
            lumens,
            kelvin,
            kind,
            source,
            luminance,
            gameLight,
        });
    }
    const emissiveCount = u32();
    const emissive: Emissive[] = [];
    for (let i = 0; i < emissiveCount; ++i) {
        const corners: number[] = [];
        for (let k = 0; k < 9; ++k) corners.push(f32());
        const radiance = f3();
        const lamp = u32();
        emissive.push({ corners, radiance, lamp });
    }
    return { version, scene, lamps, emissive };
}

const luma = (c: readonly number[]) => 0.2126 * c[0]! + 0.7152 * c[1]! + 0.0722 * c[2]!;
const brightest = (c: readonly number[]) => Math.max(c[0]!, c[1]!, c[2]!);

// rt_lights.h lamp_reach, from the data's photometry: the brightest channel's intensity (cd) -> metres.
export function lampReachMetres(lamp: Lamp): number {
    const y = luma(lamp.light);
    const scale = y > 0 ? brightest(lamp.light) / y : 1;
    const metres = Math.sqrt(Math.max(lamp.candela * scale, 0) / LAMP_REACH_LUX);
    return Math.max(metres, 2 * lamp.core * METRES_PER_UNIT);
}

// The lux on a level surface one global unit lights, from the data's point lamps (candela = luma x unit x core^2).
export function unitLux(lamps: readonly Lamp[]): number {
    const told: number[] = [];
    for (const l of lamps) {
        const r = l.core * METRES_PER_UNIT;
        const y = luma(l.light);
        if (l.shape !== 2 && l.candela > 0 && y > 0 && r > 0) told.push(l.candela / (y * r * r));
    }
    return median(told);
}

export function median(values: readonly number[]): number {
    if (values.length === 0) return 0;
    const s = [...values].sort((a, b) => a - b);
    const m = s.length >> 1;
    return s.length % 2 ? s[m]! : 0.5 * (s[m - 1]! + s[m]!);
}
export interface Spread {
    count: number;
    min: number;
    median: number;
    max: number;
}
export function spread(values: readonly number[]): Spread {
    const finite = values.filter((v) => Number.isFinite(v));
    return {
        count: finite.length,
        min: finite.length ? Math.min(...finite) : 0,
        median: median(finite),
        max: finite.length ? Math.max(...finite) : 0,
    };
}
const fmt = (v: number) =>
    v === 0 ? "0" : Math.abs(v) >= 1e5 || Math.abs(v) < 1e-2 ? v.toExponential(2) : v.toPrecision(3);
const spreadText = (s: Spread, unit: string) =>
    s.count ? `${fmt(s.min)} / ${fmt(s.median)} / ${fmt(s.max)} ${unit}` : "-";

// --- the fog ---------------------------------------------------------------------------------------------------------
export interface FogParams {
    red: number;
    green: number;
    blue: number;
    alpha: number;
    dist: number;
}
export interface FogMedium {
    start: number;
    end: number;
    endClipped: number;
    extinctionPerMetre: number; // (end = start x 4)
    extinctionClippedPerMetre: number; // (end = start + 7200)
    visibilityMetres: number;
    overClearAir: number;
}
// rt_volume_light.h level_fog_extinction for the game's SetFogDistance(start).
export function fogMedium(start: number): FogMedium | null {
    if (!(start > 0)) return null;
    const end = start * 4;
    const endClipped = start + DISTANCE_CLIPPING_EXTRA;
    const perUnit = KOSCHMIEDER / (end - start);
    const perUnitClipped = KOSCHMIEDER / (endClipped - start);
    const perMetre = perUnit / METRES_PER_UNIT;
    return {
        start,
        end,
        endClipped,
        extinctionPerMetre: perMetre,
        extinctionClippedPerMetre: perUnitClipped / METRES_PER_UNIT,
        visibilityMetres: KOSCHMIEDER / perMetre,
        overClearAir: perMetre / (KOSCHMIEDER / CLEAR_VISIBILITY),
    };
}

// The `name = { ... }` blocks of a decompiled script with their fog fields (fog_red, ... fog_dist).
export function fogBlocks(text: string): Map<string, FogParams> {
    const out = new Map<string, FogParams>();
    const re = /^(\w+)\s*=\s*\{([\s\S]*?)^\}/gm;
    for (const m of text.matchAll(re)) {
        const body = m[2]!;
        const field = (name: string) => {
            const f = new RegExp(`\\b${name}\\s*=\\s*(-?[0-9.]+)`).exec(body);
            return f ? Number(f[1]) : Number.NaN;
        };
        const dist = field("fog_dist");
        if (Number.isNaN(dist)) continue;
        out.set(m[1]!, {
            red: field("fog_red"),
            green: field("fog_green"),
            blue: field("fog_blue"),
            alpha: field("fog_alpha"),
            dist,
        });
    }
    return out;
}

// --- the census ------------------------------------------------------------------------------------------------------
interface LevelReport {
    level: string;
    scene: string;
    lamps: number;
    bySource: Record<string, number>;
    candela: Spread;
    lumens: Spread;
    luminance: Spread;
    reachBefore: Spread; // metres
    edgeLuxBefore: Spread; // lux at the old reach
    reachAfter: Spread; // metres
    sharpEdges: number; // lamps whose light at their old reach was over 0.1 lux
    emitters: number;
    emitterNits: Spread;
    unitLux: number;
    outliers: string[];
}

function findGameFolder(folder: string): string {
    for (const f of [folder, path.join(folder, "Game")])
        if (fs.existsSync(path.join(f, "thug2-decomp-relit"))) return f;
    return folder;
}

function levelCodes(game: string): Map<number, string> {
    const out = new Map<number, string>();
    // (the game's levels and the data overlay's - the ported ones)
    for (const levels of [path.join(game, "Data", "levels"), path.join(game, "thug2-decomp-data", "levels")]) {
        if (!fs.existsSync(levels)) continue;
        for (const d of fs.readdirSync(levels)) if (!/_sky$/i.test(d)) out.set(checksum(d) >>> 0, d);
    }
    return out;
}

function report(name: string, head: RelitHead): LevelReport {
    const shining = head.lamps.filter((l) => brightest(l.light) > 0);
    const bySource: Record<string, number> = {};
    for (const l of shining) {
        const k = SOURCES[l.source] ?? String(l.source);
        bySource[k] = (bySource[k] ?? 0) + 1;
    }
    const unit = unitLux(head.lamps);
    const edgeLux = shining.map((l) => {
        const r = l.reach * METRES_PER_UNIT;
        const y = luma(l.light);
        return r > 0 && y > 0 ? (l.candela * brightest(l.light)) / y / (r * r) : 0;
    });
    const nits = head.emissive.map((e) => (luma(e.radiance) * unit) / Math.PI);
    const outliers: string[] = [];
    shining.forEach((l, i) => {
        const where = l.position.map((v) => v.toFixed(0)).join(",");
        const what = `${SHAPES[l.shape] ?? l.shape} ${KINDS[l.kind] ?? l.kind} (${SOURCES[l.source] ?? l.source}) at ${where}`;
        if (l.lumens > 50000) outliers.push(`lamp ${i}: ${fmt(l.lumens)} lm - ${what}`);
        if (l.luminance > 1e6) outliers.push(`lamp ${i}: ${fmt(l.luminance)} nits at its surface - ${what}`);
    });
    const brightestEmitter = Math.max(0, ...nits);
    if (brightestEmitter > 1e5) outliers.push(`an emitting triangle at ${fmt(brightestEmitter)} nits`);
    return {
        level: name,
        scene: (head.scene >>> 0).toString(16).padStart(8, "0"),
        lamps: shining.length,
        bySource,
        candela: spread(shining.map((l) => l.candela)),
        lumens: spread(shining.map((l) => l.lumens)),
        luminance: spread(shining.map((l) => l.luminance)),
        reachBefore: spread(shining.map((l) => l.reach * METRES_PER_UNIT)),
        edgeLuxBefore: spread(edgeLux),
        reachAfter: spread(shining.map(lampReachMetres)),
        sharpEdges: edgeLux.filter((e) => e > 0.1).length,
        emitters: head.emissive.length,
        emitterNits: spread(nits),
        unitLux: unit,
        outliers,
    };
}

function census(game: string, levelsWanted: Set<string> | null, json: string | undefined): number {
    const folder = findGameFolder(game);
    const relit = path.join(folder, "thug2-decomp-relit");
    if (!fs.existsSync(relit)) return exitWith(`no thug2-decomp-relit under ${game}`);
    const codes = levelCodes(folder);
    const reports: LevelReport[] = [];
    const stale: string[] = [];
    for (const file of fs
        .readdirSync(relit)
        .filter((f) => f.endsWith(".relit"))
        .sort()) {
        const head = readRelit(fs.readFileSync(path.join(relit, file)));
        if (!head) {
            stale.push(codes.get(Number.parseInt(file, 16) >>> 0) ?? file.replace(/\.relit$/, ""));
            continue;
        }
        const name = codes.get(head.scene >>> 0) ?? file.replace(/\.relit$/, "");
        if (levelsWanted && !levelsWanted.has(name.toLowerCase())) continue;
        reports.push(report(name, head));
    }
    reports.sort((a, b) => a.level.localeCompare(b.level));
    if (stale.length)
        print(
            `Relit data older than v${RELIT_OLDEST} (the game reads it as out of date - the level keeps the original's ` +
                `lighting; its lamps not physical until it is baked again): ${stale.length} - ${stale.sort().join(", ")}\n`,
        );
    print(
        "Lamps (shining), per level: count; intensity cd, flux lm, surface nits min / median / max; reach before (m)",
    );
    print(
        "and the light at it (lux - over 0.1 lux a visible edge at night), reach after (m); emitting triangles (nits).",
    );
    for (const r of reports) {
        print(
            `\n${r.level} (${r.scene}): ${r.lamps} lamps - ${Object.entries(r.bySource)
                .map(([k, v]) => `${v} ${k}`)
                .join(", ")}`,
        );
        print(`  candela   ${spreadText(r.candela, "cd")}`);
        print(`  flux      ${spreadText(r.lumens, "lm")}`);
        print(`  surface   ${spreadText(r.luminance, "nits")}`);
        print(
            `  reach     before ${spreadText(r.reachBefore, "m")}, its edge ${spreadText(r.edgeLuxBefore, "lux")} (${r.sharpEdges} over 0.1 lux)`,
        );
        print(`            after  ${spreadText(r.reachAfter, "m")}`);
        print(`  emitters  ${r.emitters} triangles, ${spreadText(r.emitterNits, "nits")} (unit ${fmt(r.unitLux)} lux)`);
        for (const o of r.outliers.slice(0, 8)) print(`  outlier   ${o}`);
        if (r.outliers.length > 8) print(`  outlier   ... ${r.outliers.length - 8} more`);
    }
    // the fog
    const fogs: Record<string, { params: FogParams; medium: FogMedium | null }> = {};
    const levelsNs = path.join(folder, "Data", "scripts", "game", "Levels.ns");
    const todNs = path.join(folder, "Data", "scripts", "engine", "tod_manager.ns");
    const sources = [levelsNs, todNs].filter((f) => fs.existsSync(f));
    if (sources.length === 0) {
        print("\nThe fog: no decompiled scripts (Data/scripts/*.ns) in this install - not reported.");
    } else {
        print(
            "\nThe fog (level structures and the time of day's states with a fog distance): start, end, extinction /",
        );
        print(
            "m (end = start x 4; with distance clipping end = start + 7200), visibility (m), x clear night air (40 km).",
        );
        for (const f of sources) {
            for (const [name, params] of fogBlocks(fs.readFileSync(f, "utf-8"))) {
                if (!(params.dist > 0)) continue;
                const medium = fogMedium(params.dist);
                fogs[name] = { params, medium };
                if (!medium) continue;
                print(
                    `  ${name.padEnd(34)} colour ${params.red},${params.green},${params.blue} a ${params.alpha}: ` +
                        `${params.dist} - ${medium.end}: ${fmt(medium.extinctionPerMetre)} /m (clipped ${fmt(medium.extinctionClippedPerMetre)}), ` +
                        `visibility ${fmt(medium.visibilityMetres)} m, x ${fmt(medium.overClearAir)}`,
                );
            }
        }
        const ratios = Object.values(fogs).flatMap((f) => (f.medium ? [f.medium.overClearAir] : []));
        print(`  over clear night air: ${spreadText(spread(ratios), "x")}`);
    }
    if (json) fs.writeFileSync(json, `${JSON.stringify({ lamps: reports, fog: fogs }, null, 1)}\n`);
    return 0;
}

function main(): number {
    const { values, positionals } = parseCli(USAGE, {
        options: {
            game: { type: "string", default: "C:/Games/THUG2/Game" },
            json: { type: "string" },
            levels: { type: "string" },
        },
        allowPositionals: true,
    });
    if (positionals[0] !== "census") return exitWith(USAGE);
    const wanted = values.levels ? new Set(values.levels.split(",").map((s) => s.trim().toLowerCase())) : null;
    return census(values.game, wanted, values.json);
}

if (isMain(import.meta.url)) await runMain(() => main());
