// NOT ORIGINAL - the level surfaces' albedo by material class (round MATALB; docs/design/vulkan.md "One albedo
// definition"): for each level texture drawn as a material's first pass, its class (the surface map's - tools/pbr_maps
// classes.ts), its texels' mean albedo (linear, the drawn alpha's opaque texels), the area the level draws it over, the
// collision terrain under it and what the original's vertex colours put on it (their light factor and how much their
// hue varies across the texture's surfaces - genuine painted colour, or light?). Summed per class, area-weighted, against
// the measured range the engine's material table gives the class's kind (src/engine/gfx/rt_material_table.h).
//
//   node tools/pbr_maps/albedo_census.ts --game <folder> [--levels no,bo,ba,au,tr] [--json <file>] [--top N]
import { existsSync, readFileSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { readTex } from "thug2-level-converter-core/level_port/tex.ts";
import * as scene from "thug2-level-converter-core/level_port/thug2_scene.ts";
import { exitWith, isMain } from "thug2-level-converter-core/lib/cli.ts";
import { gameRoot } from "../qbjs/game_scripts.ts";
import { CLASS, type ClassName, classify, className } from "./classes.ts";
import { readPbr, SUFFIX } from "./pbr_file.ts";
import { dictionaries, meanAlbedo, statsOf, textureRgba, usageOf } from "./pbr_maps.ts";
import { surfacesOf, terrainName } from "./surfaces.ts";

// The measured ranges of the kinds the classes map to (rt_material_table.h kMaterialTable's `albedo`, by
// kind_of_map_class) - for the report only; the engine's table is the one the game reads.
export const MEASURED: Partial<Record<ClassName, [number, number]>> = {
    default: [0.05, 0.85],
    asphalt: [0.04, 0.18],
    concrete: [0.25, 0.45],
    stone: [0.15, 0.4],
    wood: [0.1, 0.5],
    foliage: [0.15, 0.26],
    ground: [0.1, 0.4],
    tile: [0.3, 0.8],
};

const luma = (r: number, g: number, b: number): number => 0.2126 * r + 0.7152 * g + 0.0722 * b;

// The engine's rule (rt_material_table.h material_albedo_scale): a texture's mean albedo held in its class's range.
export function calibrated(row: { className: ClassName; texel: number }): number {
    const range = MEASURED[row.className];
    if (!range || !(row.texel > 0)) return row.texel;
    return Math.min(range[1], Math.max(range[0], row.texel));
}

export interface TextureRow {
    level: string;
    checksum: string;
    className: ClassName;
    mapped: boolean; // a surface map (class) in the level's .pbr; else classified here
    area: number;
    texel: number; // mean linear albedo (luma) of its opaque texels
    colour: number; // the first pass's colour factor (luma, 1: neutral)
    terrain: string; // the collision terrain under most of it
    light: number; // the original's vertex colours' light on it: area-weighted mean of (2 x vc)^2.2 (luma), linear
    hueSpread: number; // area-weighted mean distance of the vertex colours' chromaticity from the texture's median
    hueVaried: number; // share of its area whose vertex colours' chromaticity is over 0.05 from the median
    // share of its area whose vertex colours' chromaticity is over 0.06 from grey (1/3, 1/3) - further than daylight's
    // own colours (a warm sun, a blue sky: ~0.03 - 0.04) take it: a colour painted into the vertices, not light
    painted: number;
    // of the hue-varied area, the share brighter than the texture's median light by 1.25 x or more - a coloured light's
    // pool (a sodium lamp's on the road) varies hue with brightness; a colour painted into the vertices needn't
    variedBright: number;
}

// The vertex layout (surfaces.ts uvOffset): position, [normal], colour (B, G, R, A - 128 is 1), uv sets.
const colourOffset = (mesh: scene.Mesh): number => (mesh.stride >= 28 && (mesh.stride - 16) % 8 === 4 ? 24 : 12);

interface VertexLight {
    area: number;
    light: number;
    chroma: [number, number][];
    weights: number[];
    lumas: number[];
}

function vertexLights(parsed: scene.Scene): Map<number, VertexLight> {
    const firstTexture = new Map<number, number>();
    for (const material of parsed.materials) {
        const pass = material.passes[0];
        if (pass?.texture && !(pass.flags & scene.PASS_ENVIRONMENT)) firstTexture.set(material.checksum, pass.texture);
    }
    const out = new Map<number, VertexLight>();
    for (const sector of parsed.sectors) {
        if (sector.flags & (scene.SEC_BILLBOARD | scene.SEC_WEIGHTS)) continue;
        for (const mesh of sector.meshes) {
            const texture = firstTexture.get(mesh.material);
            const buffer = mesh.buffers[0]?.[1];
            const strip = mesh.strips[0] ?? [];
            if (texture === undefined || !buffer || mesh.stride < 20 || strip.length < 3) continue;
            const count = Math.min(mesh.vertexCount, Math.floor(buffer.length / mesh.stride));
            const at = colourOffset(mesh);
            let entry = out.get(texture);
            if (!entry) {
                entry = { area: 0, light: 0, chroma: [], weights: [], lumas: [] };
                out.set(texture, entry);
            }
            const vertexArea = new Float64Array(count);
            for (let j = 2; j < strip.length; j++) {
                const ia = strip[j - 2]!;
                const ib = strip[j - 1]!;
                const ic = strip[j]!;
                if (ia === ib || ib === ic || ia === ic || ia >= count || ib >= count || ic >= count) continue;
                const p = (i: number): number[] => [0, 4, 8].map((o) => buffer.readFloatLE(i * mesh.stride + o));
                const a = p(ia);
                const b = p(ib);
                const c = p(ic);
                const u = [b[0]! - a[0]!, b[1]! - a[1]!, b[2]! - a[2]!];
                const v = [c[0]! - a[0]!, c[1]! - a[1]!, c[2]! - a[2]!];
                const area =
                    Math.hypot(
                        u[1]! * v[2]! - u[2]! * v[1]!,
                        u[2]! * v[0]! - u[0]! * v[2]!,
                        u[0]! * v[1]! - u[1]! * v[0]!,
                    ) / 2;
                if (!(area > 0)) continue;
                vertexArea[ia] = vertexArea[ia]! + area / 3;
                vertexArea[ib] = vertexArea[ib]! + area / 3;
                vertexArea[ic] = vertexArea[ic]! + area / 3;
            }
            for (let i = 0; i < count; i++) {
                const w = vertexArea[i]!;
                if (!(w > 0)) continue;
                const o = i * mesh.stride + at;
                // (128 is 1: the passes double it - the light factor, display-referred, to linear)
                const factor = (byte: number): number => Math.min(2, (2 * byte) / 255) ** 2.2;
                const r = factor(buffer[o + 2]!);
                const g = factor(buffer[o + 1]!);
                const b = factor(buffer[o]!);
                entry.area += w;
                entry.light += w * luma(r, g, b);
                const sum = r + g + b;
                if (sum > 1e-4) {
                    entry.chroma.push([r / sum, g / sum]);
                    entry.weights.push(w);
                    entry.lumas.push(luma(r, g, b));
                }
            }
        }
    }
    return out;
}

const weightedMedian = (values: number[], weights: number[]): number => {
    const order = values.map((_, i) => i).sort((a, b) => values[a]! - values[b]!);
    const total = weights.reduce((s, w) => s + w, 0);
    let run = 0;
    for (const i of order) {
        run += weights[i]!;
        if (run >= total / 2) return values[i]!;
    }
    return values[order[order.length - 1]!] ?? 0;
};

export function censusOfLevel(root: string, level: string): TextureRow[] {
    const rows: TextureRow[] = [];
    for (const dictionary of dictionaries(root, [level])) {
        if (dictionary.name.endsWith("_net.tex.xbx")) continue; // (the online variant: the same textures)
        const scn = dictionary.scn();
        if (!scn) continue;
        let parsed: scene.Scene;
        try {
            parsed = scene.readScene(scn, "scene");
        } catch {
            continue;
        }
        const pbrPath = join(root, dictionary.folder, `${dictionary.name}${SUFFIX}`);
        const classes = new Map<number, number>();
        if (existsSync(pbrPath)) {
            for (const entry of readPbr(readFileSync(pbrPath))) classes.set(entry.checksum, entry.materialClass);
        }
        const usage = usageOf(scn);
        const surfaces = surfacesOf(scn, dictionary.col());
        const lights = vertexLights(parsed);
        const colourOf = new Map<number, number>();
        for (const material of parsed.materials) {
            const pass = material.passes[0];
            if (!pass?.texture || colourOf.has(pass.texture)) continue;
            const c = pass.color.map((x) => Math.min(1, 2 * x));
            colourOf.set(pass.texture, luma(c[0] ?? 1, c[1] ?? 1, c[2] ?? 1) ** 2.2);
        }
        const seen = new Set<number>();
        for (const texture of readTex(dictionary.tex(), dictionary.name).textures) {
            if (seen.has(texture.checksum)) continue;
            seen.add(texture.checksum);
            const use = surfaces.get(texture.checksum);
            if (!use || !(use.area > 0)) continue;
            const image = textureRgba(texture);
            if (!image) continue;
            const mapped = classes.get(texture.checksum);
            let name: ClassName;
            if (mapped !== undefined) name = className(mapped);
            else {
                const used = usage.get(texture.checksum);
                const verdict = used
                    ? classify(statsOf(image), used)
                    : { name: "default" as ClassName, skip: "unused" };
                if (verdict.skip) continue; // (no map: its albedo the texture as painted - not a class's)
                name = verdict.name;
            }
            const texel = meanAlbedo(image);
            if (!(texel > 0)) continue;
            let terrain = "-";
            let most = 0;
            for (const [index, area] of use.terrains) {
                if (area > most) {
                    most = area;
                    terrain = terrainName(index) ?? String(index);
                }
            }
            const vl = lights.get(texture.checksum);
            let hueSpread = 0;
            let hueVaried = 0;
            let painted = 0;
            let variedBright = 0;
            if (vl?.chroma.length) {
                let all = 0;
                vl.chroma.forEach((c, i) => {
                    const w = vl.weights[i]!;
                    all += w;
                    if (Math.hypot(c[0] - 1 / 3, c[1] - 1 / 3) > 0.06) painted += w;
                });
                painted /= all;
                const mr = weightedMedian(
                    vl.chroma.map((c) => c[0]),
                    vl.weights,
                );
                const mg = weightedMedian(
                    vl.chroma.map((c) => c[1]),
                    vl.weights,
                );
                let total = 0;
                const ml = weightedMedian(vl.lumas, vl.weights);
                vl.chroma.forEach((c, i) => {
                    const d = Math.hypot(c[0] - mr, c[1] - mg);
                    const w = vl.weights[i]!;
                    hueSpread += w * d;
                    if (d > 0.05) {
                        hueVaried += w;
                        if (vl.lumas[i]! >= 1.25 * ml) variedBright += w;
                    }
                    total += w;
                });
                variedBright = hueVaried > 0 ? variedBright / hueVaried : 0;
                hueSpread /= total;
                hueVaried /= total;
            }
            rows.push({
                level,
                checksum: texture.checksum.toString(16).toUpperCase().padStart(8, "0"),
                className: name,
                mapped: mapped !== undefined,
                area: use.area,
                texel,
                colour: colourOf.get(texture.checksum) ?? 1,
                terrain,
                light: vl && vl.area > 0 ? vl.light / vl.area : 1,
                hueSpread,
                hueVaried,
                painted,
                variedBright,
            });
        }
    }
    return rows;
}

// Area-weighted quantiles of `value` over `rows`.
export function quantiles(rows: readonly TextureRow[], value: (row: TextureRow) => number, at: number[]): number[] {
    const sorted = [...rows].sort((a, b) => value(a) - value(b));
    const total = sorted.reduce((s, r) => s + r.area, 0);
    return at.map((q) => {
        let run = 0;
        for (const row of sorted) {
            run += row.area;
            if (run >= q * total) return value(row);
        }
        return sorted.length ? value(sorted[sorted.length - 1]!) : 0;
    });
}

function argValue(args: string[], name: string): string | undefined {
    const at = args.indexOf(name);
    return at >= 0 ? args[at + 1] : undefined;
}

export function main(args: string[]): void {
    const game = argValue(args, "--game");
    if (!game) exitWith("usage: albedo_census.ts --game <folder> [--levels no,bo] [--json <file>] [--top N]");
    const root = gameRoot(game);
    const levels = (argValue(args, "--levels") ?? "no,bo,ba,au,tr").split(",").map((l) => l.trim().toLowerCase());
    const top = Number(argValue(args, "--top") ?? "0");
    const all: TextureRow[] = [];
    for (const level of levels) {
        const rows = censusOfLevel(root, level);
        all.push(...rows);
        const total = rows.reduce((s, r) => s + r.area, 0);
        console.log(`\n${level}: ${rows.length} textures, area ${total.toFixed(0)}`);
        console.log(
            "class      share  n    texel p10/p50/p90    calibrated p10/p50/p90  vc light p50  texel in range        " +
                "hue-varied (brighter) / vc-coloured area",
        );
        const byClass = new Map<ClassName, TextureRow[]>();
        for (const row of rows) byClass.set(row.className, [...(byClass.get(row.className) ?? []), row]);
        for (const [name, group] of [...byClass].sort((a, b) => CLASS[a[0]] - CLASS[b[0]])) {
            const area = group.reduce((s, r) => s + r.area, 0);
            const [t10, t50, t90] = quantiles(group, (r) => r.texel, [0.1, 0.5, 0.9]);
            const [c10, c50, c90] = quantiles(group, calibrated, [0.1, 0.5, 0.9]);
            const [l50] = quantiles(group, (r) => r.light, [0.5]);
            const range = MEASURED[name];
            const inside = range ? group.filter((r) => r.texel >= range[0] && r.texel <= range[1]) : [];
            const insideShare = inside.reduce((s, r) => s + r.area, 0) / area;
            const varied = group.reduce((s, r) => s + r.area * r.hueVaried, 0) / area;
            const coloured = group.reduce((s, r) => s + r.area * r.painted, 0) / area;
            const brightVaried =
                group.reduce((s, r) => s + r.area * r.hueVaried * r.variedBright, 0) /
                Math.max(
                    1e-9,
                    group.reduce((s, r) => s + r.area * r.hueVaried, 0),
                );
            console.log(
                `${name.padEnd(10)} ${((100 * area) / total).toFixed(1).padStart(5)}% ${String(group.length).padStart(4)}  ` +
                    `${t10!.toFixed(3)}/${t50!.toFixed(3)}/${t90!.toFixed(3)}  ${c10!.toFixed(3)}/${c50!.toFixed(3)}/${c90!.toFixed(3)}  ` +
                    `  ${l50!.toFixed(2)}        ${range ? `${(100 * insideShare).toFixed(0).padStart(3)}% of ${range.join("-")}` : "-".padEnd(17)}` +
                    `   ${(100 * varied).toFixed(1)}% (${(100 * brightVaried).toFixed(0)}% brighter) / ${(100 * coloured).toFixed(1)}%`,
            );
        }
        if (top > 0) {
            for (const row of [...rows].sort((a, b) => b.area - a.area).slice(0, top)) {
                console.log(
                    `  ${row.checksum} ${row.className.padEnd(9)} ${row.mapped ? "map" : "   "} ` +
                        `${((100 * row.area) / total).toFixed(1).padStart(5)}%  texel ${row.texel.toFixed(3)} ` +
                        `colour ${row.colour.toFixed(2)} vc ${row.light.toFixed(2)} hue ${row.hueSpread.toFixed(3)} ` +
                        `(${(100 * row.hueVaried).toFixed(0)}%) vc-coloured ${(100 * row.painted).toFixed(0)}% ${row.terrain}`,
                );
            }
        }
    }
    const json = argValue(args, "--json");
    if (json) writeFileSync(json, JSON.stringify(all, null, 1));
}

if (isMain(import.meta.url)) main(process.argv.slice(2));
