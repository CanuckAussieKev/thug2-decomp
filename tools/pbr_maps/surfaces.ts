// NOT ORIGINAL - what the level says about the surfaces a texture is drawn on, for its surface map's bump strength
// (docs/design/vulkan.md "Per-pixel lighting and surface maps", "Bump"): the collision terrain under the faces that
// use it (the level designers' own word for what a surface is: smooth concrete, asphalt, sidewalk, brick, rock...) and
// its texel size in the world (an old 128-texel texture stretched over a street turns every speck of its grain into a
// lump a hand wide: high-frequency noise at that scale isn't relief).
//
//   terrain     each scene triangle (a sector's meshes, their first strip) is matched to a face of the collision object
//               of the same checksum: the face whose plane is nearest its centre (within 6 units, normals within ~45
//               degrees) and that holds the centre (within 2 units of its edges); the face's terrain byte counts with
//               the triangle's area
//   texel size  world units per texel: the area-weighted geometric mean of sqrt(world area / uv area) over the
//               texture's triangles, divided by sqrt(width x height)
//
// Only a material's first pass counts (the texture the map lights); skinned and billboard sectors are left out.

import * as col from "thug2-level-converter-core/level_port/thug2_col.ts";
import { THUG2_TERRAIN_TYPES } from "thug2-level-converter-core/level_port/thug2_material.ts";
import * as scene from "thug2-level-converter-core/level_port/thug2_scene.ts";
import type { ClassRule } from "./classes.ts";

export type TerrainName = (typeof THUG2_TERRAIN_TYPES)[number];

export interface SurfaceUse {
    area: number; // world area drawn with the texture as a first pass
    terrains: Map<number, number>; // terrain index -> the area matched to faces of that terrain
    logScale: number; // sum of area x ln(world units per uv unit)
    scaleArea: number; // the area counted in logScale (triangles with a usable uv area)
}

type Vec = [number, number, number];

const sub = (a: Vec, b: Vec): Vec => [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
const cross = (a: Vec, b: Vec): Vec => [
    a[1] * b[2] - a[2] * b[1],
    a[2] * b[0] - a[0] * b[2],
    a[0] * b[1] - a[1] * b[0],
];
const dot = (a: Vec, b: Vec): number => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
const length = (a: Vec): number => Math.hypot(a[0], a[1], a[2]);

const PLANE_TOLERANCE = 6; // units off a face's plane
const EDGE_TOLERANCE = 2; // units outside its edges
const NORMAL_AGREEMENT = 0.7; // |cos| between the triangle's and the face's normals

// A collision face ready for the point tests: its corners, unit normal, box and terrain.
interface ColFace {
    a: Vec;
    edges: [Vec, Vec, Vec]; // inward edge normals (unit, in the plane) - the distance inside each edge
    offsets: [number, number, number];
    normal: Vec;
    lo: Vec;
    hi: Vec;
    terrain: number;
}

function colFaces(object: col.ColObject): ColFace[] {
    const out: ColFace[] = [];
    for (const [flags, terrain, ia, ib, ic] of object.faces) {
        if (flags & col.FACE_NON_COLLIDABLE && !(flags & col.FACE_SKATABLE)) continue;
        const a = object.verts[ia] as Vec | undefined;
        const b = object.verts[ib] as Vec | undefined;
        const c = object.verts[ic] as Vec | undefined;
        if (!a || !b || !c) continue;
        const n = cross(sub(b, a), sub(c, a));
        const size = length(n);
        if (size < 1e-6) continue;
        const normal: Vec = [n[0] / size, n[1] / size, n[2] / size];
        const corners = [a, b, c];
        const edges: Vec[] = [];
        const offsets: number[] = [];
        for (let k = 0; k < 3; k++) {
            const p = corners[k]!;
            const q = corners[(k + 1) % 3]!;
            const inward = cross(normal, sub(q, p)); // (counter-clockwise about the normal: points inside)
            const l = length(inward);
            const unit: Vec = [inward[0] / l, inward[1] / l, inward[2] / l];
            edges.push(unit);
            offsets.push(dot(unit, p));
        }
        out.push({
            a,
            edges: edges as [Vec, Vec, Vec],
            offsets: offsets as [number, number, number],
            normal,
            lo: [
                Math.min(a[0], b[0], c[0]) - PLANE_TOLERANCE,
                Math.min(a[1], b[1], c[1]) - PLANE_TOLERANCE,
                Math.min(a[2], b[2], c[2]) - PLANE_TOLERANCE,
            ],
            hi: [
                Math.max(a[0], b[0], c[0]) + PLANE_TOLERANCE,
                Math.max(a[1], b[1], c[1]) + PLANE_TOLERANCE,
                Math.max(a[2], b[2], c[2]) + PLANE_TOLERANCE,
            ],
            terrain: terrain & 0xff,
        });
    }
    return out;
}

// The terrain of the face under `centre` (a triangle's, normal `normal`), or -1.
function terrainAt(faces: readonly ColFace[], centre: Vec, normal: Vec): number {
    let best = -1;
    let bestDistance = PLANE_TOLERANCE;
    for (const f of faces) {
        if (
            centre[0] < f.lo[0] ||
            centre[0] > f.hi[0] ||
            centre[1] < f.lo[1] ||
            centre[1] > f.hi[1] ||
            centre[2] < f.lo[2] ||
            centre[2] > f.hi[2]
        ) {
            continue;
        }
        if (Math.abs(dot(f.normal, normal)) < NORMAL_AGREEMENT) continue;
        const distance = Math.abs(dot(f.normal, sub(centre, f.a)));
        if (distance > bestDistance) continue;
        if (
            dot(f.edges[0], centre) - f.offsets[0] < -EDGE_TOLERANCE ||
            dot(f.edges[1], centre) - f.offsets[1] < -EDGE_TOLERANCE ||
            dot(f.edges[2], centre) - f.offsets[2] < -EDGE_TOLERANCE
        ) {
            continue;
        }
        best = f.terrain;
        bestDistance = distance;
    }
    return best;
}

// The vertex layout of a level mesh (geometry.ts): position, normal when the stride says so, colour, uv sets.
const uvOffset = (mesh: scene.Mesh): number => (mesh.stride >= 28 && (mesh.stride - 16) % 8 === 4 ? 28 : 16);

// What the level says about each texture drawn as a first pass: by texture checksum. Empty without a scene; no
// terrains without a collision file.
export function surfacesOf(scn: Uint8Array | null, colFile: Uint8Array | null): Map<number, SurfaceUse> {
    const out = new Map<number, SurfaceUse>();
    if (!scn) return out;
    let parsed: scene.Scene;
    try {
        parsed = scene.readScene(scn, "scene");
    } catch {
        return out;
    }
    const objects = new Map<number, ColFace[]>();
    if (colFile) {
        try {
            for (const object of col.readCol(colFile, "col").objects) {
                const faces = colFaces(object);
                if (faces.length) objects.set(object.checksum, [...(objects.get(object.checksum) ?? []), ...faces]);
            }
        } catch {
            objects.clear();
        }
    }
    const firstTexture = new Map<number, number>();
    for (const material of parsed.materials) {
        const pass = material.passes[0];
        if (pass?.texture && !(pass.flags & scene.PASS_ENVIRONMENT)) firstTexture.set(material.checksum, pass.texture);
    }
    for (const sector of parsed.sectors) {
        if (sector.flags & (scene.SEC_BILLBOARD | scene.SEC_WEIGHTS)) continue;
        const faces = objects.get(sector.checksum) ?? [];
        for (const mesh of sector.meshes) {
            const texture = firstTexture.get(mesh.material);
            const buffer = mesh.buffers[0]?.[1];
            const strip = mesh.strips[0] ?? [];
            if (texture === undefined || !buffer || mesh.stride < 24 || strip.length < 3) continue;
            const count = Math.min(mesh.vertexCount, Math.floor(buffer.length / mesh.stride));
            const uvAt = uvOffset(mesh);
            if (uvAt + 8 > mesh.stride) continue;
            let use = out.get(texture);
            if (!use) {
                use = { area: 0, terrains: new Map(), logScale: 0, scaleArea: 0 };
                out.set(texture, use);
            }
            const position = (i: number): Vec => {
                const at = i * mesh.stride;
                return [buffer.readFloatLE(at), buffer.readFloatLE(at + 4), buffer.readFloatLE(at + 8)];
            };
            const uv = (i: number): [number, number] => {
                const at = i * mesh.stride + uvAt;
                return [buffer.readFloatLE(at), buffer.readFloatLE(at + 4)];
            };
            for (let j = 2; j < strip.length; j++) {
                const ia = strip[j - 2]!;
                const ib = strip[j - 1]!;
                const ic = strip[j]!;
                if (ia === ib || ib === ic || ia === ic || ia >= count || ib >= count || ic >= count) continue;
                const a = position(ia);
                const b = position(ib);
                const c = position(ic);
                const n = cross(sub(b, a), sub(c, a));
                const twice = length(n);
                if (!(twice > 1e-6)) continue;
                const area = twice / 2;
                use.area += area;
                const ua = uv(ia);
                const ub = uv(ib);
                const uc = uv(ic);
                const uvArea = Math.abs((ub[0] - ua[0]) * (uc[1] - ua[1]) - (uc[0] - ua[0]) * (ub[1] - ua[1])) / 2;
                if (uvArea > 1e-7 && Number.isFinite(uvArea)) {
                    use.logScale += area * Math.log(Math.sqrt(area / uvArea));
                    use.scaleArea += area;
                }
                if (faces.length) {
                    const centre: Vec = [(a[0] + b[0] + c[0]) / 3, (a[1] + b[1] + c[1]) / 3, (a[2] + b[2] + c[2]) / 3];
                    const terrain = terrainAt(faces, centre, [n[0] / twice, n[1] / twice, n[2] / twice]);
                    if (terrain >= 0) use.terrains.set(terrain, (use.terrains.get(terrain) ?? 0) + area);
                }
            }
        }
    }
    return out;
}

// World units per texel of a `width` x `height` texture used so (undefined: no usable uvs).
export function texelSize(use: SurfaceUse | undefined, width: number, height: number): number | undefined {
    if (!use || use.scaleArea <= 0) return undefined;
    return Math.exp(use.logScale / use.scaleArea) / Math.sqrt(width * height);
}

// The bump each terrain allows (the classical generator's slope scale, as a class rule's bump): the smooth ones -
// poured or troweled concrete, asphalt, sidewalk, glazed tile, smooth metal, plexiglass, ice - nearly flat (the
// generator's grain on them is dirt and stains painted on, not relief); the rough ones - brick, rock, gravel, rough
// concrete, shingles, corrugated metal, grating - full. DEFAULT and the GENERICs say nothing.
export const TERRAIN_BUMP: Partial<Record<TerrainName, number>> = {
    CONCSMOOTH: 0.2,
    CONCROUGH: 0.6,
    METALSMOOTH: 0.15,
    METALROUGH: 0.4,
    METALCORRUGATED: 0.8,
    METALGRATING: 0.8,
    METALTIN: 0.3,
    WOOD: 0.6,
    WOODMASONITE: 0.2,
    WOODPLYWOOD: 0.35,
    WOODFLIMSY: 0.5,
    WOODSHINGLE: 1.0,
    WOODPIER: 0.8,
    BRICK: 1.3,
    TILE: 0.3,
    ASPHALT: 0.2,
    ROCK: 1.3,
    GRAVEL: 1.2,
    SIDEWALK: 0.25,
    GRASS: 0.6,
    GRASSDRIED: 0.6,
    DIRT: 0.9,
    DIRTPACKED: 0.6,
    WATER: 0.2,
    ICE: 0.1,
    SNOW: 0.4,
    SAND: 0.6,
    PLEXIGLASS: 0.0,
    FIBERGLASS: 0.2,
    CARPET: 0.5,
    CONVEYOR: 0.5,
    CHAINLINK: 0.6,
    METALFUTURE: 0.3,
};

export function terrainName(index: number): TerrainName | undefined {
    return THUG2_TERRAIN_TYPES[index];
}

// The texel size at which a texture's grain still reads as relief (world units - THUG2's are about an inch): finer
// keeps the full bump; coarser weakens it as REFERENCE_TEXEL / size (a bump's slope is its height over its width, and
// a speck's height doesn't grow with the texel it was stretched over), down to MIN_SCALE_FACTOR.
export const REFERENCE_TEXEL = 1.5;
export const MIN_SCALE_FACTOR = 0.25;
// The share of a texture's area on telling terrains for them to count fully (below: by that share over this).
export const TERRAIN_COVERAGE = 0.5;
const FLOOR = 0.02; // (the logarithms' floor)

export interface Bump {
    bump: number; // the generator's slope scale for this texture
    surface: number; // the bump before the texel size's factor
    terrain?: number | undefined; // the terrains' bump (when they said anything)
    coverage: number; // 0 - 1: the share of the texture's area on telling terrains
    texel?: number | undefined; // world units per texel
    scale: number; // the texel size's factor (MIN_SCALE_FACTOR - 1)
    why: string; // the evidence, short
}

// The bump of a texture of class `rule` drawn as `use` says. The terrains and the class are both evidence and both
// err (a terrain is picked for its sound - THPS3's LA paves its street with rough concrete -, a class from colour
// statistics), so where the terrains cover the texture the bump is the geometric mean of their (area-weighted) bump
// and the class's; partly covered, it leans to the class's by the uncovered share. A class of no bump (glass) stays
// flat. Then weakened for coarse texels.
export function bumpFor(rule: ClassRule, use: SurfaceUse | undefined, width: number, height: number): Bump {
    let telling = 0;
    let weighted = 0;
    const named: [string, number][] = [];
    for (const [index, area] of use?.terrains ?? []) {
        const name = terrainName(index);
        const value = name ? TERRAIN_BUMP[name] : undefined;
        if (value === undefined) continue;
        telling += area;
        weighted += value * area;
        named.push([name!, area]);
    }
    const coverage = use && use.area > 0 ? Math.min(1, telling / use.area) : 0;
    const terrain = telling > 0 ? weighted / telling : undefined;
    let surface = rule.bump;
    if (terrain !== undefined && rule.bump > 0) {
        const weight = Math.min(1, coverage / TERRAIN_COVERAGE);
        const own = Math.log(Math.max(FLOOR, rule.bump));
        const mean = (Math.log(Math.max(FLOOR, terrain)) + own) / 2;
        surface = Math.exp(weight * mean + (1 - weight) * own);
    }
    const texel = texelSize(use, width, height);
    const scale = texel === undefined ? 1 : Math.max(MIN_SCALE_FACTOR, Math.min(1, REFERENCE_TEXEL / texel));
    named.sort((a, b) => b[1] - a[1]);
    const why = [
        named.length
            ? `${named
                  .slice(0, 2)
                  .map(([n]) => n.toLowerCase())
                  .join("+")} ${Math.round(coverage * 100)}%`
            : "",
        texel === undefined ? "" : `${texel.toFixed(2)} u/texel`,
    ]
        .filter(Boolean)
        .join(", ");
    return { bump: surface * scale, surface, terrain, coverage, texel, scale, why: why || "no level evidence" };
}
