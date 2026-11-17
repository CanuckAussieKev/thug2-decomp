// NOT ORIGINAL - what the levels' sky scenes hold, and what the renderer's sky makes of each mesh (docs/design/vulkan.md
// "The sky scene's parts"; engine/gfx/sky_parts.h is the rule - this its twin over the levels' files): read from the
// game's own levels and the data overlay's ported ones, nothing run.
//
//   node tools/game/sky_survey.ts [--game <install>] [--levels AP_Sky,can_sky,...] [--meshes]
//
// For each sky scene (a level folder whose name ends in _sky, any case) the count of each part, and with --meshes every
// mesh: its first pass's blend mode, its texture (the share of holes, of faint texels, of opaque texels standing on a
// ground line at the card's foot, which way v runs), where its opaque part lies seen from the origin (elevations,
// azimuth span) and its part:
//   - glow: a pass that adds, subtracts, multiplies or brightens - light of the air;
//   - painted: no silhouette (under kLeastHoles of holes) - the dome, its bands, a painted ground ring;
//   - scenery: a solid silhouette (its edges a cut) standing on a ground line at its foot, its opaque part reaching the
//     horizon or below - or the top of grounded scenery in front of it - mountains, a skyline, a tree line;
//   - cloud: any other silhouette - clouds, a painted moon, haze cards.
import * as fs from "node:fs";
import * as path from "node:path";
import { decode } from "thug2-level-converter-core/level_port/dxt.ts";
import { depalettise, readTex, type Texture } from "thug2-level-converter-core/level_port/tex.ts";
import { type Mesh, readScene } from "thug2-level-converter-core/level_port/thug2_scene.ts";
import { isMain, parseCli, print, runMain } from "thug2-level-converter-core/lib/cli.ts";
import { hex8 } from "thug2-level-converter-core/lib/text.ts";

const USAGE = `usage: sky_survey.ts [--game <install>] [--levels AP_Sky,can_sky,...] [--meshes]

The levels' sky scenes, mesh by mesh, and what the renderer's sky makes of each (tools/game/README.md).`;

// (engine/gfx/sky_parts.h's twins)
export const kHoleAlpha = 0.5;
export const kFaintAlpha = 0.1;
export const kLeastHoles = 0.02;
export const kMostFaint = 0.05;
export const kLeastStanding = 0.6;
export const kBaseSpread = 0.1;
export const kLeastCorrelation = 0.5;
export const kSamplesPerSide = 8;
export const kAzimuthBins = 72;
const BLEND_MASK = 0xffffff;
// 1 add, 2 add fixed, 3 subtract, 4 subtract fixed, 7 modulate, 8 modulate fixed, 9 brighten, 10 brighten fixed
export const kGlowBlends = new Set([1, 2, 3, 4, 7, 8, 9, 10]);

export type SkyPart = "painted" | "glow" | "cloud" | "scenery";
const PARTS: SkyPart[] = ["painted", "glow", "cloud", "scenery"];

// A texture's coverage: its first level's alpha (0 - 1), rows top first.
export interface Coverage {
    width: number;
    height: number;
    alpha: Float32Array;
}

export function coverageOf(texture: Texture): Coverage | null {
    const data = texture.levels[0];
    if (!data) return null;
    let bgra: Uint8Array;
    if (texture.dxt !== 0) bgra = decode(data, texture.width, texture.height, texture.dxt);
    else if (texture.texelDepth === 32) bgra = data;
    else return null;
    const count = texture.width * texture.height;
    const alpha = new Float32Array(count);
    for (let i = 0; i < count; i++) alpha[i] = (bgra[i * 4 + 3] ?? 0) / 255;
    return { width: texture.width, height: texture.height, alpha };
}

export function alphaAt(c: Coverage, u: number, v: number): number {
    const x = Math.min(Math.floor((u - Math.floor(u)) * c.width), c.width - 1);
    const y = Math.min(Math.floor((v - Math.floor(v)) * c.height), c.height - 1);
    return c.alpha[y * c.width + x] ?? 1;
}

export function holeShare(c: Coverage): number {
    let holes = 0;
    for (const a of c.alpha) if (a < kHoleAlpha) holes++;
    return holes / Math.max(c.alpha.length, 1);
}

export function faintShare(c: Coverage): number {
    let covered = 0;
    let faint = 0;
    for (const a of c.alpha) {
        if (a >= kFaintAlpha) covered++;
        if (a >= kFaintAlpha && a < kHoleAlpha) faint++;
    }
    return covered > 0 ? faint / covered : 0;
}

// The share of the opaque texels standing on one ground line at the card's foot (`down`: +1 when v grows downward on
// the mesh, -1 upward, 0 unknown).
export function standingShare(c: Coverage, down: number): number {
    if (down === 0) return 0;
    let opaque = 0;
    const feet: number[] = [];
    const runs: number[] = [];
    for (let x = 0; x < c.width; x++) {
        let footed = false;
        let running = false;
        let run = 0;
        for (let k = 0; k < c.height; k++) {
            const y = down > 0 ? c.height - 1 - k : k;
            const solid = (c.alpha[y * c.width + x] ?? 0) >= kHoleAlpha;
            if (!solid) {
                running = false;
                continue;
            }
            opaque++;
            if (!footed) {
                footed = true;
                running = true;
                feet.push(k);
            }
            if (running) run++;
        }
        if (footed) runs.push(run);
    }
    if (opaque === 0 || feet.length === 0) return 0;
    const sorted = [...feet].sort((a, b) => a - b);
    const line = sorted[Math.floor(sorted.length / 2)] ?? 0;
    const spread = kBaseSpread * c.height;
    if (line > spread) return 0;
    let standing = 0;
    feet.forEach((foot, i) => {
        if (Math.abs(foot - line) <= spread) standing += runs[i] ?? 0;
    });
    return standing / opaque;
}

export interface MeshFacts {
    blend: number;
    holes: number;
    faint: number;
    standing: number;
    down: number;
    opaqueLowest: number; // degrees (Infinity: none)
    opaqueHighest: number;
    azimuths: Set<number>;
    azimuthSpan: number; // degrees of azimuth its vertices cover
}

type V3 = [number, number, number];
const elevation = (p: V3): number => (Math.atan2(p[1], Math.hypot(p[0], p[2])) * 180) / Math.PI;
const azimuthBin = (p: V3): number =>
    Math.floor(((Math.atan2(p[0], p[2]) * 180) / Math.PI + 360) / (360 / kAzimuthBins)) % kAzimuthBins;

export function measure(mesh: Mesh, blend: number, coverage: Coverage | null): MeshFacts {
    const buffer = mesh.buffers[0]?.[1];
    const positions: V3[] = [];
    const uvs: [number, number][] = [];
    const uvAt = mesh.offsets[2] ?? 0;
    const hasUv = uvAt >= 12 && uvAt + 8 <= mesh.stride;
    if (buffer && mesh.stride >= 12) {
        for (let i = 0; i < mesh.vertexCount; i++) {
            const o = i * mesh.stride;
            positions.push([buffer.readFloatLE(o), buffer.readFloatLE(o + 4), buffer.readFloatLE(o + 8)]);
            uvs.push(hasUv ? [buffer.readFloatLE(o + uvAt), buffer.readFloatLE(o + uvAt + 4)] : [0, 0]);
        }
    }
    let down = 0;
    if (positions.length >= 2) {
        const e = positions.map(elevation);
        const v = uvs.map((t) => t[1]);
        const me = e.reduce((a, b) => a + b, 0) / e.length;
        const mv = v.reduce((a, b) => a + b, 0) / v.length;
        let together = 0;
        let se = 0;
        let sv = 0;
        e.forEach((x, i) => {
            const dv = (v[i] ?? 0) - mv;
            together += (x - me) * dv;
            se += (x - me) ** 2;
            sv += dv * dv;
        });
        const r = se > 0 && sv > 0 ? together / Math.sqrt(se * sv) : 0;
        down = r <= -kLeastCorrelation ? 1 : r >= kLeastCorrelation ? -1 : 0;
    }
    const facts: MeshFacts = {
        blend,
        holes: coverage ? holeShare(coverage) : 0,
        faint: coverage ? faintShare(coverage) : 0,
        standing: coverage ? standingShare(coverage, down) : 0,
        down,
        opaqueLowest: Infinity,
        opaqueHighest: -Infinity,
        azimuths: new Set(),
        azimuthSpan: new Set(positions.map((p) => azimuthBin(p))).size * (360 / kAzimuthBins),
    };
    const strip = mesh.strips[0] ?? [];
    for (let i = 2; i < strip.length; i++) {
        const ia = strip[i - 2] ?? 0;
        const ib = strip[i - 1] ?? 0;
        const ic = strip[i] ?? 0;
        const a = positions[ia];
        const b = positions[ib];
        const c = positions[ic];
        if (ia === ib || ib === ic || ia === ic || !a || !b || !c) continue;
        const ua = uvs[ia] ?? [0, 0];
        const ub = uvs[ib] ?? [0, 0];
        const uc = uvs[ic] ?? [0, 0];
        for (let s = 0; s < kSamplesPerSide; s++) {
            for (let t = 0; s + t < kSamplesPerSide; t++) {
                const wb = (s + 1 / 3) / kSamplesPerSide;
                const wc = (t + 1 / 3) / kSamplesPerSide;
                const wa = 1 - wb - wc;
                const p: V3 = [0, 1, 2].map((k) => wa * (a[k] ?? 0) + wb * (b[k] ?? 0) + wc * (c[k] ?? 0)) as V3;
                const u = wa * ua[0] + wb * ub[0] + wc * uc[0];
                const v = wa * ua[1] + wb * ub[1] + wc * uc[1];
                if (coverage && alphaAt(coverage, u, v) < kHoleAlpha) continue;
                const e = elevation(p);
                facts.opaqueLowest = Math.min(facts.opaqueLowest, e);
                facts.opaqueHighest = Math.max(facts.opaqueHighest, e);
                facts.azimuths.add(azimuthBin(p));
            }
        }
    }
    return facts;
}

// The scene's meshes' parts (sky_parts.h classify's twin).
export function classify(meshes: readonly MeshFacts[]): SkyPart[] {
    const parts: SkyPart[] = meshes.map(() => "cloud");
    const candidate = meshes.map(() => false);
    const grounded = meshes.map(() => false);
    meshes.forEach((m, i) => {
        if (kGlowBlends.has(m.blend)) parts[i] = "glow";
        else if (m.holes < kLeastHoles) parts[i] = "painted";
        else {
            candidate[i] = m.faint <= kMostFaint && m.standing >= kLeastStanding && Number.isFinite(m.opaqueLowest);
            grounded[i] = candidate[i] === true && m.opaqueLowest <= 0;
        }
    });
    for (let more = true; more; ) {
        more = false;
        meshes.forEach((m, i) => {
            if (!candidate[i] || grounded[i]) return;
            const behind = meshes.some(
                (o, j) =>
                    grounded[j] === true &&
                    [...m.azimuths].some((bin) => o.azimuths.has(bin)) &&
                    m.opaqueLowest <= o.opaqueHighest,
            );
            if (behind) {
                grounded[i] = true;
                more = true;
            }
        });
    }
    return parts.map((p, i) => (grounded[i] ? "scenery" : p));
}

interface Row {
    sector: number;
    material: number;
    texture: string;
    facts: MeshFacts;
    part: SkyPart;
}

function surveyScene(folder: string, name: string): Row[] | null {
    const scn = path.join(folder, `${name}.scn.xbx`);
    if (!fs.existsSync(scn)) return null;
    const scene = readScene(fs.readFileSync(scn), scn);
    const coverages = new Map<number, { size: string; coverage: Coverage | null }>();
    const texPath = path.join(folder, `${name}.tex.xbx`);
    if (fs.existsSync(texPath)) {
        const tex = readTex(fs.readFileSync(texPath), texPath);
        depalettise(tex);
        for (const t of tex.textures) {
            coverages.set(t.checksum, { size: `${t.width}x${t.height}`, coverage: coverageOf(t) });
        }
    }
    const materials = new Map(scene.materials.map((m) => [m.checksum, m]));
    const rows: Omit<Row, "part">[] = [];
    for (const sector of scene.sectors) {
        for (const mesh of sector.meshes) {
            const first = materials.get(mesh.material)?.passes[0];
            const blend = (first?.blendMode ?? 0) & BLEND_MASK;
            const found = first && first.texture !== 0 ? coverages.get(first.texture) : undefined;
            rows.push({
                sector: sector.checksum,
                material: mesh.material,
                texture: first && first.texture !== 0 ? `${hex8(first.texture)} ${found?.size ?? "?"}` : "-",
                facts: measure(mesh, blend, found?.coverage ?? null),
            });
        }
    }
    const parts = classify(rows.map((r) => r.facts));
    return rows.map((r, i) => ({ ...r, part: parts[i] ?? "cloud" }));
}

const degrees = (v: number): string => (Number.isFinite(v) ? v.toFixed(1) : "-");

async function main(): Promise<number> {
    const { values } = parseCli(USAGE, {
        options: {
            game: { type: "string", default: "C:\\Games\\THUG2\\Game" },
            levels: { type: "string" },
            meshes: { type: "boolean", default: false },
        },
    });
    const roots = [path.join(values.game, "Data", "levels"), path.join(values.game, "thug2-decomp-data", "levels")];
    const wanted = values.levels ? new Set(values.levels.split(",").map((n) => n.toLowerCase())) : null;
    const totals = new Map<SkyPart, number>();
    for (const root of roots) {
        if (!fs.existsSync(root)) continue;
        for (const name of fs.readdirSync(root).sort()) {
            if (!/_sky$/i.test(name) || (wanted && !wanted.has(name.toLowerCase()))) continue;
            const rows = surveyScene(path.join(root, name), name);
            if (!rows) continue;
            const counts = new Map<SkyPart, number>();
            for (const row of rows) {
                counts.set(row.part, (counts.get(row.part) ?? 0) + 1);
                totals.set(row.part, (totals.get(row.part) ?? 0) + 1);
            }
            print(`${name}: ${rows.length} meshes - ${PARTS.map((k) => `${counts.get(k) ?? 0} ${k}`).join(", ")}`);
            if (!values.meshes) continue;
            for (const row of rows) {
                const f = row.facts;
                print(
                    `  ${row.part.padEnd(7)} ${hex8(row.sector)} ${hex8(row.material)} blend ${f.blend} tex ${row.texture} holes ${f.holes.toFixed(2)} faint ${f.faint.toFixed(2)} standing ${f.standing.toFixed(2)} down ${f.down} opaque ${degrees(f.opaqueLowest)}..${degrees(f.opaqueHighest)} az ${f.azimuthSpan}`,
                );
            }
        }
    }
    print(`all: ${PARTS.map((k) => `${totals.get(k) ?? 0} ${k}`).join(", ")}`);
    return 0;
}

if (isMain(import.meta.url)) runMain(main);
