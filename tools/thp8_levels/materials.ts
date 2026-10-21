// THP8's level materials read for THUG2: which of a material's textures is which (the layers, their blend masks, the
// zone's light maps) and the vertex uv field each one reads - worked out from the data (no code of the game used).
//
// A material names a shader template (FXFILES\MaterialLibrary.bin: its templates by name checksum - "DiffuseTerrain_3Pass",
// "NormalMapTerrain_4Pass"...) and lists its textures in the template's order. The terrain templates draw up to four
// layers ("passes"): pass 0's diffuse map (then, by template, its normal map - DXN - and specular map), each further
// pass a diffuse map (some with a normal + specular map) and a blend mask, then the zone's two light maps: the ambient
// map (sky light, coloured) and the shadow map (the sun's light, grey), atlases shared by the zone's lit materials
// (b6c03d1 / e4cc61f3 in the skate park: 1024 / 2048 texels), or small stand-ins (16 x 16 grey) for unlit ones.
//
// The vertex carries one uv field per "slot": slot 0 is pass 0's (diffuse, normal and specular share it), the last
// two the light maps' (ambient, shadow); between them each further pass has one (diffuse, normal, specular) and,
// while there are slots to spare, its mask one of its own - else the mask shares the pass's. (DiffuseTerrain_3Pass
// meshes always have all seven: 0, 1 / 2, 3 / 4, 5, 6 with unused passes' fields NaN.) Checked against every lit
// material of the skate park: the light-map fields land in [0, 1] atlases, the masks' fields line up with their
// passes' where they share.
//
// A further pass covers the ones below it by its mask's brightness times its diffuse map's alpha (the stickers on the
// snack hut: a sticker sheet over the planks through a mask of white rectangles; "RENTALS" letters by their alpha).
//
// The parameter block (geom.ts MAT_PARAMS): 0x50 bytes of header (+0x08 texture count, +0x18 the number of float4
// parameters, +0x20 the template's technique, +0x28 terrain), then the float4s, then the texture checksums. In
// DiffuseTerrain_3Pass the float4s are the passes' colours (tints: 0.94, 0.89, 0.85 on the skate park's stairs); in
// the specular / normal-mapped ones the first is the specular power (15, 0, 0, 0) and the second pass 0's colour.

import { checksum } from "../level_port/qb.ts";
import * as dxt from "./dxt.ts";
import type * as geom from "./geom.ts";
import * as xenos from "./xenos.ts";

export const T_DIFFUSE_TERRAIN = checksum("DiffuseTerrain_3Pass");
export const T_DIFFUSE_SPEC_TERRAIN = checksum("DiffuseSpecTerrain_3Pass");
export const T_NORMAL_TERRAIN_3 = checksum("NormalMapTerrain_3Pass");
export const T_NORMAL_TERRAIN_4 = checksum("NormalMapTerrain_4Pass");
export const T_NORMAL_PARALLAX_TERRAIN = checksum("NormalMapParallaxTerrain_3Pass");
export const T_NORMAL_ROAD = checksum("NormalMapRoad_3Pass");
export const T_NORMAL_POSTER = checksum("NormalMapTerrainPoster_4Pass");

// Templates whose passes are read (the rest: pass 0's diffuse map only, as before).
const LAYERED = new Set([
    T_DIFFUSE_TERRAIN,
    T_DIFFUSE_SPEC_TERRAIN,
    T_NORMAL_TERRAIN_3,
    T_NORMAL_TERRAIN_4,
    T_NORMAL_PARALLAX_TERRAIN,
    T_NORMAL_ROAD,
    T_NORMAL_POSTER,
]);
// Templates whose pass 0 lists a specular map after its diffuse (and normal) map, and whose pass 1 does too.
const SPECULAR_FIRST_TWO = new Set([T_DIFFUSE_SPEC_TERRAIN]);

export const PARAM_FLOAT4S = 0x18;
export const PARAM_HEADER = 0x50;

export interface Layer {
    diffuse: number; // texture checksum
    mask: number | null; // the blend mask (passes 1...)
    slot: number; // uv slot of the diffuse map
    maskSlot: number; // uv slot of the mask (= slot when shared)
    colour: number[]; // R, G, B tint, A opacity (passColour; 1, 1, 1, 1 when unknown)
}

export interface MaterialPlan {
    layers: Layer[]; // pass 0 first
    ambient: number | null; // light maps
    shadow: number | null;
}

const isNormalMap = (t: number, textures: ReadonlyMap<number, xenos.XenosTexture>): boolean =>
    textures.get(t)?.format === xenos.FMT_DXN;

// The zone's light maps: textures that stand (nearly always - LIGHT_MAP_SHARE of their uses) second to last
// (ambient) or last (shadow) of materials of at least three textures. (A few materials list them elsewhere or swap
// the small stand-ins: the slums' 2048 shadow map once second to last, its 32 x 32 white stand-in in five places.)
export const LIGHT_MAP_SHARE = 0.8;
export function lightMaps(materials: readonly geom.GeomMaterial[]): { ambient: Set<number>; shadow: Set<number> } {
    const uses = new Map<number, number[]>(); // [as ambient, as shadow, elsewhere]
    for (const m of materials) {
        const n = m.textures.length;
        m.textures.forEach((t, i) => {
            const u = uses.get(t) ?? [0, 0, 0];
            u[n >= 3 && i === n - 2 ? 0 : n >= 3 && i === n - 1 ? 1 : 2]!++;
            uses.set(t, u);
        });
    }
    const ambient = new Set<number>();
    const shadow = new Set<number>();
    for (const [t, [a, s, e]] of uses as Map<number, [number, number, number]>) {
        if (a >= LIGHT_MAP_SHARE * (a + s + e)) ambient.add(t);
        else if (s >= LIGHT_MAP_SHARE * (a + s + e)) shadow.add(t);
    }
    return { ambient, shadow };
}

// The float4 parameters before the texture list.
export function float4s(m: geom.GeomMaterial): number[][] {
    const p = m.params;
    if (p.length < PARAM_HEADER) return [];
    const k = p.readUInt32BE(PARAM_FLOAT4S);
    const out: number[][] = [];
    for (let i = 0; i < k && PARAM_HEADER + 16 * (i + 1) <= p.length; i++) {
        out.push([0, 1, 2, 3].map((c) => p.readFloatBE(PARAM_HEADER + 16 * i + 4 * c)));
    }
    return out;
}

// Which float4 holds pass p's colour (R, G, B tint, A the layer's opacity), by template: DiffuseTerrain_3Pass lists
// the passes' colours alone; the specular / normal-mapped ones lead with the specular power and give each pass its
// colour, a tiling float4 (not applied) and, passes 0 and 1, a specular colour - read from how the values vary over
// the zones' materials (colours vary in R, G, B, the others stay grey or (u, v, 0, 0)).
const COLOUR_AT: ReadonlyMap<number, readonly number[]> = new Map([
    [T_DIFFUSE_TERRAIN, [0, 1, 2]],
    [T_DIFFUSE_SPEC_TERRAIN, [1, 4, 7]],
    [T_NORMAL_TERRAIN_3, [1, 4, 7]],
    [T_NORMAL_PARALLAX_TERRAIN, [1, 4, 7]],
    [T_NORMAL_ROAD, [1, 4, 7]],
    [T_NORMAL_TERRAIN_4, [1, 4, 6, 8]],
    [T_NORMAL_POSTER, [1, 4, 6, 8]],
]);

// Pass p's colour [R, G, B, A], or null when the template's isn't known or the value doesn't read as one (a few
// DiffuseTerrain_3Pass materials have 5, 5, 5 there).
export function passColour(m: geom.GeomMaterial, pass: number): number[] | null {
    const at = COLOUR_AT.get(m.template)?.[pass];
    if (at === undefined) return null;
    const f = float4s(m);
    if (m.template !== T_DIFFUSE_TERRAIN && !(f[0] !== undefined && f[0][1] === 0 && f[0][2] === 0)) return null;
    const c = f[at];
    if (c === undefined) return null;
    const ok = c.slice(0, 3).every((x) => Number.isFinite(x) && x >= 0 && x <= 2) && c[3]! >= 0 && c[3]! <= 1;
    return ok ? c : null;
}

// The material's layers and light maps (uv slots for a mesh of `slots` uv fields). Layers past pass 0 only for the
// terrain templates, and only when the texture list reads as their layout (else pass 0 alone).
export function planMaterial(
    m: geom.GeomMaterial,
    textures: ReadonlyMap<number, xenos.XenosTexture>,
    maps: { ambient: ReadonlySet<number>; shadow: ReadonlySet<number> },
    slots: number,
): MaterialPlan {
    const t = m.textures;
    const n = t.length;
    const lit = n >= 3 && maps.ambient.has(t[n - 2]!) && maps.shadow.has(t[n - 1]!) && slots >= 3;
    const ambient = lit ? t[n - 2]! : null;
    const shadow = lit ? t[n - 1]! : null;
    const colour = (p: number): number[] => passColour(m, p) ?? [1, 1, 1, 1];
    // pass 0: the first texture that isn't a normal map (a few shaders list theirs first)
    const first = t.find((x, i) => !isNormalMap(x, textures) && (!lit || i < n - 2));
    if (first === undefined) return { layers: [], ambient, shadow };
    const base: Layer = { diffuse: first, mask: null, slot: 0, maskSlot: 0, colour: colour(0) };
    const single: MaterialPlan = { layers: [base], ambient, shadow };
    if (!LAYERED.has(m.template) || !lit) return single;
    // the passes: walk the list
    const list = t.slice(0, n - 2);
    const spec = SPECULAR_FIRST_TWO.has(m.template);
    let i = 0;
    const take = (p: number): number | null => {
        const d = list[i++];
        if (d === undefined) return null;
        if (list[i] !== undefined && isNormalMap(list[i]!, textures))
            i += 2; // normal + specular map
        else if (spec && p <= 1) i++; // specular map
        return d;
    };
    if (take(0) !== first) return single;
    const passes: { diffuse: number; mask: number }[] = [];
    while (i < list.length) {
        const d = take(passes.length + 1);
        const mask = list[i++];
        if (d === null || mask === undefined || isNormalMap(d, textures) || isNormalMap(mask, textures)) return single;
        passes.push({ diffuse: d, mask });
    }
    if (i !== list.length || passes.length > 3) return single;
    // uv slots: 0 = pass 0, then each pass one, its mask its own while slots are spare, light maps last
    const fixed = m.template === T_DIFFUSE_TERRAIN && slots === 7;
    let spare = slots - 3 - passes.length;
    if (spare < 0) return single;
    let s = 1;
    const layers = [base];
    for (const [k, p] of passes.entries()) {
        if (fixed) {
            layers.push({
                diffuse: p.diffuse,
                mask: p.mask,
                slot: 1 + 2 * k,
                maskSlot: 2 + 2 * k,
                colour: colour(k + 1),
            });
            continue;
        }
        const own = spare > 0;
        if (own) spare--;
        layers.push({ diffuse: p.diffuse, mask: p.mask, slot: s, maskSlot: own ? s + 1 : s, colour: colour(k + 1) });
        s += own ? 2 : 1;
    }
    return { layers, ambient, shadow };
}

// --- the material library ----------------------------------------------------------------------------------------

// FXFILES\MaterialLibrary.bin (big-endian): "MATL", u32 2, u32 template count, ..., from 0x108 0x28 bytes per
// template (+0x0C its name's checksum, +0x1C its technique count, +0x20 where its techniques' 0x140-byte
// descriptions start, from 0x108); a technique's +0x08 is its name's checksum ("_ENGINE_Default_Pass0_Pass1",
// "..._InteriorLighting"). A material's parameter block names its technique by number (+0x20). The names are among
// the file's own strings. Returns template checksum -> technique names (the checksum in hex where none matches).
export const LIBRARY_TEMPLATES = 0x108;
export function readTechniques(data: Buffer): Map<number, string[]> {
    if (data.toString("latin1", 0, 4) !== "MATL") throw new Error("MaterialLibrary: no MATL");
    const strings = new Map<number, string>();
    let start = -1;
    for (let i = 0; i <= data.length; i++) {
        const c = i < data.length ? data[i]! : 0;
        if (c >= 0x20 && c < 0x7f) {
            if (start < 0) start = i;
        } else if (start >= 0) {
            if (i - start >= 2) {
                const s = data.toString("latin1", start, i);
                strings.set(checksum(s), s);
            }
            start = -1;
        }
    }
    const out = new Map<number, string[]>();
    const count = data.readUInt32BE(8);
    for (let k = 0; k < count; k++) {
        const at = LIBRARY_TEMPLATES + 0x28 * k;
        const n = data.readUInt32BE(at + 0x1c);
        const first = LIBRARY_TEMPLATES + data.readUInt32BE(at + 0x20);
        const names: string[] = [];
        for (let i = 0; i < n && first + 0x140 * (i + 1) <= data.length; i++) {
            const c = data.readUInt32BE(first + 0x140 * i + 8);
            names.push(strings.get(c) ?? c.toString(16));
        }
        out.set(data.readUInt32BE(at + 0x0c), names);
    }
    return out;
}

// Whether the material's technique lights it as an interior: by its ambient map alone (no sun - its shadow map is the
// white stand-in).
export function interior(m: geom.GeomMaterial, techniques: ReadonlyMap<number, readonly string[]>): boolean {
    if (m.params.length < 0x24) return false;
    const name = techniques.get(m.template)?.[m.params.readUInt32BE(0x20)] ?? "";
    return /InteriorLighting/i.test(name);
}

// --- light maps -------------------------------------------------------------------------------------------------

// A texture decoded (B, G, R, A bytes), for sampling.
export interface Decoded {
    width: number;
    height: number;
    bgra: Buffer;
}

export function decode(t: xenos.XenosTexture): Decoded | null {
    if (t.format === xenos.FMT_DXN) return null;
    const base = xenos.baseLevel(t);
    const kind =
        t.format === xenos.FMT_DXT1 ? 1 : t.format === xenos.FMT_DXT3 ? 3 : t.format === xenos.FMT_DXT5 ? 5 : 0;
    if (!kind && t.format !== xenos.FMT_8888) return null;
    return { width: t.width, height: t.height, bgra: kind ? dxt.decode(base, t.width, t.height, kind) : base };
}

// Bilinear sample (wrapping) -> [B, G, R, A] 0-1.
export function sample(d: Decoded, u: number, v: number): number[] {
    if (!Number.isFinite(u) || !Number.isFinite(v)) return [1, 1, 1, 1];
    const x = u * d.width - 0.5;
    const y = v * d.height - 0.5;
    const x0 = Math.floor(x);
    const y0 = Math.floor(y);
    const fx = x - x0;
    const fy = y - y0;
    const out = [0, 0, 0, 0];
    for (const [dx, dy, w] of [
        [0, 0, (1 - fx) * (1 - fy)],
        [1, 0, fx * (1 - fy)],
        [0, 1, (1 - fx) * fy],
        [1, 1, fx * fy],
    ] as const) {
        const px = (((x0 + dx) % d.width) + d.width) % d.width;
        const py = (((y0 + dy) % d.height) + d.height) % d.height;
        const o = 4 * (py * d.width + px);
        for (let c = 0; c < 4; c++) out[c]! += (w * d.bgra[o + c]!) / 255;
    }
    return out;
}

export interface Lighting {
    sun: number[]; // R, G, B 0-1 (the zone's time-of-day sun colour)
    ambientScale: number;
    sunScale: number;
}

// THP8's light at a vertex from its light maps: the ambient map (sky light) scaled, plus the sun's colour times the
// shadow map. The scales are ours (THP8's shaders weren't read): set so that sunlit ground comes out near THUG2's
// full brightness and shade at about half - see docs/level-port-compromises.md section 10. R, G, B.
export function light(ambient: number[], shadow: number[], l: Lighting): number[] {
    const s = shadow[2]!; // (grey: red)
    return [0, 1, 2].map((c) => ambient[2 - c]! * l.ambientScale + l.sun[c]! * s * l.sunScale);
}

// Light baked into vertex colours is only as fine as the mesh: a big floor triangle under a tree spans hundreds of
// shadow-map texels. `refine` splits triangles where the light inside them isn't what the corners' light, blended,
// gives: an edge is split at its middle when the light along it (at 1/4, 1/2, 3/4) differs from the blend by more
// than `threshold`, or when a triangle's inside (its middle and the middles of its halves) does - then all three of
// its edges. A split edge is split for both triangles on it (edges by their vertex numbers), so no cracks open
// inside a mesh; triangles are cut into 2, 3 or 4 keeping their winding. Rounds until nothing is split, `depth`
// rounds, or `maxVertices`.
export interface RefineOptions {
    threshold: number; // light difference that splits
    minLength: number; // edges shorter than this (texels) never split
    depth: number;
    maxVertices: number;
}

export function refine<V>(
    verts: V[],
    tris: number[][],
    mid: (a: V, b: V) => V,
    value: (v: V) => number, // the light at a vertex
    at: (a: V, b: V, c: V, wa: number, wb: number) => number, // the light inside (barycentric wa, wb, 1 - wa - wb)
    length: (a: V, b: V) => number, // an edge's length in light-map texels
    opts: RefineOptions,
): number[][] {
    const values = verts.map(value);
    let current = tris;
    for (let round = 0; round < opts.depth; round++) {
        const split = new Set<string>();
        const key = (i: number, j: number): string => (i < j ? `${i},${j}` : `${j},${i}`);
        const edgeVaries = (i: number, j: number): boolean => {
            const a = verts[i]!;
            const b = verts[j]!;
            if (length(a, b) < opts.minLength) return false;
            for (const t of [0.25, 0.5, 0.75]) {
                const blend = values[i]! * (1 - t) + values[j]! * t;
                if (Math.abs(at(a, b, b, 1 - t, t) - blend) > opts.threshold) return true;
            }
            return false;
        };
        for (const [a, b, c] of current as [number, number, number][]) {
            const longest = Math.max(
                length(verts[a]!, verts[b]!),
                length(verts[b]!, verts[c]!),
                length(verts[c]!, verts[a]!),
            );
            if (longest < opts.minLength) continue;
            let inside = false;
            for (const [wa, wb] of [
                [1 / 3, 1 / 3],
                [2 / 3, 1 / 6],
                [1 / 6, 2 / 3],
                [1 / 6, 1 / 6],
            ] as const) {
                const blend = values[a]! * wa + values[b]! * wb + values[c]! * (1 - wa - wb);
                if (Math.abs(at(verts[a]!, verts[b]!, verts[c]!, wa, wb) - blend) > opts.threshold) inside = true;
            }
            for (const [i, j] of [
                [a, b],
                [b, c],
                [c, a],
            ] as const) {
                if (inside ? length(verts[i]!, verts[j]!) >= opts.minLength : edgeVaries(i, j)) split.add(key(i, j));
            }
        }
        if (!split.size || verts.length + split.size > opts.maxVertices) break;
        const middle = new Map<string, number>();
        for (const k of split) {
            const [i, j] = k.split(",").map(Number) as [number, number];
            const v = mid(verts[i]!, verts[j]!);
            middle.set(k, verts.length);
            verts.push(v);
            values.push(value(v));
        }
        const out: number[][] = [];
        for (const t of current) {
            // rotate so the split edges come first: (a b), then (b c)
            let [a, b, c] = t as [number, number, number];
            const m = (i: number, j: number): number | undefined => middle.get(key(i, j));
            const count = [m(a, b), m(b, c), m(c, a)].filter((x) => x !== undefined).length;
            for (let r = 0; r < 3; r++) {
                const ok =
                    count === 0 ||
                    count === 3 ||
                    (count === 1 && m(a, b) !== undefined) ||
                    (count === 2 && m(c, a) === undefined);
                if (ok) break;
                [a, b, c] = [b, c, a];
            }
            if (count === 0) out.push([a, b, c]);
            else if (count === 1) {
                const ab = m(a, b)!;
                out.push([a, ab, c], [ab, b, c]);
            } else if (count === 2) {
                const ab = m(a, b)!;
                const bc = m(b, c)!;
                out.push([a, ab, bc], [ab, b, bc], [a, bc, c]);
            } else {
                const ab = m(a, b)!;
                const bc = m(b, c)!;
                const ca = m(c, a)!;
                out.push([a, ab, ca], [ab, b, bc], [ca, bc, c], [ab, bc, ca]);
            }
        }
        current = out;
    }
    return current;
}

// A layer's texture for THUG2: its diffuse map's colour, alpha = its alpha times its mask's brightness (the mask read
// at the same place - they share the uv field). B, G, R, A at the diffuse map's size.
export function maskedLayer(diffuse: Decoded, mask: Decoded, opacity = 1): Buffer {
    const out = Buffer.from(diffuse.bgra);
    for (let y = 0; y < diffuse.height; y++) {
        for (let x = 0; x < diffuse.width; x++) {
            const mx = Math.min(mask.width - 1, Math.floor(((x + 0.5) * mask.width) / diffuse.width));
            const my = Math.min(mask.height - 1, Math.floor(((y + 0.5) * mask.height) / diffuse.height));
            const o = 4 * (my * mask.width + mx);
            const lum = (mask.bgra[o]! + mask.bgra[o + 1]! + mask.bgra[o + 2]!) / 3;
            const at = 4 * (y * diffuse.width + x) + 3;
            out[at] = Math.round((out[at]! * lum * opacity) / 255);
        }
    }
    return out;
}
