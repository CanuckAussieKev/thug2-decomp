// NOT ORIGINAL - what a level's collision says about each of its textures: every triangle of the scene's meshes (its
// material's first pass's texture) is looked up among the collision faces by its centroid - the levels' collision is
// made from the same geometry, so a drawn triangle and its collision face share their corners - and its area counted
// under that face's terrain type (TERRAIN_CONCSMOOTH, _ASPHALT, _BRICK...). Also each texture's world size: how many
// metres one unit of its texture coordinates covers (the area-weighted median over its triangles), so a detail layer
// can be scaled to the real world.
import { readCol } from "thug2-level-converter-core/level_port/thug2_col.ts";
import { THUG2_TERRAIN_TYPES } from "thug2-level-converter-core/level_port/thug2_material.ts";
import { type Mesh, readScene } from "thug2-level-converter-core/level_port/thug2_scene.ts";
import type { TerrainEvidence } from "./kinds.ts";

// THUG2's units are inches.
export const METRES_PER_UNIT = 0.0254;
// The terrain names past THUG2_TERRAIN_TYPES' (the same order as the level editor's terrain list).
const MORE_TERRAINS = ["WHEELS", "WETCONC", "METALFENCE"];
const QUANTUM = 8; // (centroids compared to 1/8 unit: the fixed-point collision vertices are sixteenths)

export function terrainName(index: number): string {
    return THUG2_TERRAIN_TYPES[index] ?? MORE_TERRAINS[index - THUG2_TERRAIN_TYPES.length] ?? `TERRAIN_${index}`;
}

type V3 = [number, number, number];

const key = (x: number, y: number, z: number): string =>
    `${Math.round(x * QUANTUM)},${Math.round(y * QUANTUM)},${Math.round(z * QUANTUM)}`;

export interface TextureUse {
    terrain: TerrainEvidence;
    area: number; // the texture's drawn area (square units)
    density: Map<number, number>; // metres per texture unit (log2, in eighths of an octave) -> area
}

// The texture's metres per unit of its coordinates: the area-weighted median (0: unknown).
export function metresPerUv(use: TextureUse): number {
    const bins = [...use.density].sort((a, b) => a[0] - b[0]);
    const total = bins.reduce((sum, [, area]) => sum + area, 0);
    if (total <= 0) return 0;
    let seen = 0;
    for (const [bin, area] of bins) {
        seen += area;
        if (seen >= total / 2) return 2 ** (bin / 8);
    }
    return 0;
}

function* triangles(
    mesh: Mesh,
): Generator<{ p: [V3, V3, V3]; uv: [[number, number], [number, number], [number, number]] | null }> {
    const buffer = mesh.buffers[0]?.[1];
    const strip = mesh.strips[0];
    if (!buffer || !strip || mesh.stride < 12) return;
    const fvf = mesh.vertexShader[0] ?? 0;
    const hasUv = ((fvf >> 8) & 0xf) > 0;
    const uvAt = mesh.offsets[2] ?? 0;
    const position = (i: number): V3 => {
        const o = i * mesh.stride;
        return [buffer.readFloatLE(o), buffer.readFloatLE(o + 4), buffer.readFloatLE(o + 8)];
    };
    const uv = (i: number): [number, number] | null => {
        const o = i * mesh.stride + uvAt;
        return hasUv && uvAt + 8 <= mesh.stride && o + 8 <= buffer.length
            ? [buffer.readFloatLE(o), buffer.readFloatLE(o + 4)]
            : null;
    };
    for (let i = 2; i < strip.length; i++) {
        const a = strip[i - 2]!;
        const b = strip[i - 1]!;
        const c = strip[i]!;
        if (a === b || b === c || a === c || a >= mesh.vertexCount || b >= mesh.vertexCount || c >= mesh.vertexCount) {
            continue;
        }
        const uvs = [uv(a), uv(b), uv(c)];
        yield {
            p: [position(a), position(b), position(c)],
            uv: uvs.every((u) => u !== null) ? (uvs as [[number, number], [number, number], [number, number]]) : null,
        };
    }
}

// The triangle's unit normal's y (0 for a degenerate one).
function normalY(p: [V3, V3, V3]): number {
    const [a, b, c] = p;
    const u = [b[0] - a[0], b[1] - a[1], b[2] - a[2]];
    const v = [c[0] - a[0], c[1] - a[1], c[2] - a[2]];
    const x = u[1]! * v[2]! - u[2]! * v[1]!;
    const y = u[2]! * v[0]! - u[0]! * v[2]!;
    const z = u[0]! * v[1]! - u[1]! * v[0]!;
    const length = Math.sqrt(x * x + y * y + z * z);
    return length > 0 ? y / length : 0;
}

function area3(p: [V3, V3, V3]): number {
    const [a, b, c] = p;
    const u = [b[0] - a[0], b[1] - a[1], b[2] - a[2]];
    const v = [c[0] - a[0], c[1] - a[1], c[2] - a[2]];
    const x = u[1]! * v[2]! - u[2]! * v[1]!;
    const y = u[2]! * v[0]! - u[0]! * v[2]!;
    const z = u[0]! * v[1]! - u[1]! * v[0]!;
    return 0.5 * Math.sqrt(x * x + y * y + z * z);
}

// Each texture's use in a level (by checksum): its scene's triangles against its collision's faces. `col` null: no
// collision (every triangle unmatched; the world size still measured).
export function textureUses(scn: Uint8Array, col: Uint8Array | null, name = "level"): Map<number, TextureUse> {
    const faces = new Map<string, number>();
    if (col) {
        for (const object of readCol(col, `${name}.col`).objects) {
            for (const [, terrain, a, b, c] of object.faces) {
                const [pa, pb, pc] = [object.verts[a], object.verts[b], object.verts[c]];
                if (!pa || !pb || !pc) continue;
                faces.set(
                    key((pa[0]! + pb[0]! + pc[0]!) / 3, (pa[1]! + pb[1]! + pc[1]!) / 3, (pa[2]! + pb[2]! + pc[2]!) / 3),
                    terrain,
                );
            }
        }
    }
    const scene = readScene(scn, `${name}.scn`);
    const firstTexture = new Map<number, number>();
    for (const material of scene.materials) {
        const texture = material.passes[0]?.texture;
        if (texture) firstTexture.set(material.checksum, texture);
    }
    const uses = new Map<number, TextureUse>();
    for (const sector of scene.sectors) {
        for (const mesh of sector.meshes) {
            const texture = firstTexture.get(mesh.material);
            if (texture === undefined) continue;
            let use = uses.get(texture);
            if (!use) {
                use = { terrain: { areas: new Map(), unmatched: 0, level: 0, total: 0 }, area: 0, density: new Map() };
                uses.set(texture, use);
            }
            for (const { p, uv } of triangles(mesh)) {
                const area = area3(p);
                if (!(area > 0)) continue;
                use.area += area;
                use.terrain.total += area;
                if (Math.abs(normalY(p)) > 0.7) use.terrain.level += area;
                const [a, b, c] = p;
                const terrain = faces.get(
                    key((a[0] + b[0] + c[0]) / 3, (a[1] + b[1] + c[1]) / 3, (a[2] + b[2] + c[2]) / 3),
                );
                if (terrain === undefined) use.terrain.unmatched += area;
                else {
                    const tname = terrainName(terrain);
                    use.terrain.areas.set(tname, (use.terrain.areas.get(tname) ?? 0) + area);
                }
                if (uv) {
                    const [u0, u1, u2] = uv;
                    const uvArea =
                        0.5 * Math.abs((u1[0] - u0[0]) * (u2[1] - u0[1]) - (u2[0] - u0[0]) * (u1[1] - u0[1]));
                    if (uvArea > 1e-9) {
                        const metres = Math.sqrt(area / uvArea) * METRES_PER_UNIT;
                        const bin = Math.round(Math.log2(metres) * 8);
                        if (Number.isFinite(bin)) use.density.set(bin, (use.density.get(bin) ?? 0) + area);
                    }
                }
            }
        }
    }
    return uses;
}
