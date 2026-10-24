// NOT ORIGINAL - tests of THP8's material reading (materials.ts) on synthetic data (no game data): light maps found by
// their place in the texture lists, the terrain templates' layers and uv slots, pass colours, the masked layer
// texture, the light from the maps and the refining of lit triangles.

import assert from "node:assert/strict";
import { test } from "node:test";
import { checksum } from "../level_port/qb.ts";
import type * as geom from "./geom.ts";
import * as mats from "./materials.ts";
import * as xenos from "./xenos.ts";

const DIFFUSE = 0x12;
// a material of `template` with these textures and float4 parameters
const material = (template: number, textures: number[], float4s: number[][] = []): geom.GeomMaterial => {
    const params = Buffer.alloc(mats.PARAM_HEADER + 16 * float4s.length + 4 * textures.length);
    params.writeUInt32BE(textures.length, 0x08);
    params.writeUInt32BE(float4s.length, mats.PARAM_FLOAT4S);
    float4s.forEach((f, i) => {
        f.forEach((x, c) => {
            params.writeFloatBE(x, mats.PARAM_HEADER + 16 * i + 4 * c);
        });
    });
    return { checksum: 1, flags: 0, template, textures, terrain: 0, state: 0, params };
};
// textures by checksum: 0x9x are normal maps (DXN), the rest DXT1
const textures = new Map<number, xenos.XenosTexture>(
    [0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0xa0, 0xa1, 0x90, 0x91].map((c) => [
        c,
        { checksum: c, format: c >= 0x90 && c < 0xa0 ? xenos.FMT_DXN : DIFFUSE } as unknown as xenos.XenosTexture,
    ]),
);
const MAPS = { ambient: new Set([0xa0]), shadow: new Set([0xa1]) };

test("light maps: the textures (nearly) always second to last / last of three or more", () => {
    const list = [
        material(0, [1, 0xa0, 0xa1]),
        material(0, [2, 3, 0xa0, 0xa1]),
        material(0, [3, 0xa0, 0xa1]),
        material(0, [4, 0xa0, 0xa1]),
        material(0, [0xa1, 0xa0]), // (two textures: neither place counts)
        material(0, [7, 8]),
    ];
    // 0xa0 second to last 4 times of 5, 0xa1 last 4 of 5: light maps; 3 (once second to last of two uses) not
    const m = mats.lightMaps(list);
    assert.deepEqual([...m.ambient], [0xa0]);
    assert.deepEqual([...m.shadow], [0xa1]);
    // under the share: not a light map
    const rare = mats.lightMaps([...list, material(0, [0xa0, 9]), material(0, [0xa0, 9])]);
    assert.deepEqual([...rare.ambient], []);
});

test("DiffuseTerrain_3Pass: pass 0, (diffuse, mask) pairs, light maps; seven uv slots, one each", () => {
    const m = material(
        mats.T_DIFFUSE_TERRAIN,
        [0x10, 0x11, 0x12, 0x13, 0x14, 0xa0, 0xa1],
        [
            [1, 1, 1, 1],
            [0.5, 0.25, 0.25, 0.75],
            [1, 1, 1, 1],
        ],
    );
    const p = mats.planMaterial(m, textures, MAPS, 7);
    assert.equal(p.ambient, 0xa0);
    assert.equal(p.shadow, 0xa1);
    assert.deepEqual(
        p.layers.map((l) => [l.diffuse, l.mask, l.slot, l.maskSlot]),
        [
            [0x10, null, 0, 0],
            [0x11, 0x12, 1, 2],
            [0x13, 0x14, 3, 4],
        ],
    );
    assert.deepEqual(p.layers[1]!.colour, [0.5, 0.25, 0.25, 0.75]);
    // one pass + light maps: pass 0 alone, still lit
    const one = mats.planMaterial(
        material(mats.T_DIFFUSE_TERRAIN, [0x10, 0xa0, 0xa1], [[1, 1, 1, 1]]),
        textures,
        MAPS,
        7,
    );
    assert.deepEqual(
        one.layers.map((l) => l.diffuse),
        [0x10],
    );
    assert.equal(one.shadow, 0xa1);
    // a list that doesn't read as pairs: pass 0 alone
    const odd = mats.planMaterial(material(mats.T_DIFFUSE_TERRAIN, [0x10, 0x11, 0xa0, 0xa1]), textures, MAPS, 7);
    assert.equal(odd.layers.length, 1);
});

test("NormalMapTerrain_3Pass: normal + specular maps skipped; masks get their own slot while slots are spare", () => {
    // D0 N0 S0 | D1 N1 S1 M1 | D2 M2 | amb shadow, six slots: 0 | 1, 2 | 3 (shared) | 4, 5
    const m = material(
        mats.T_NORMAL_TERRAIN_3,
        [0x10, 0x90, 0x10, 0x11, 0x91, 0x12, 0x13, 0x14, 0x15, 0xa0, 0xa1],
        [
            [15, 0, 0, 0],
            [0.8, 0.6, 0.4, 1],
            [1, 1, 1, 1],
            [0.5, 0.5, 0.5, 1],
            [1, 1, 1, 0.5],
        ],
    );
    const p = mats.planMaterial(m, textures, MAPS, 6);
    assert.deepEqual(
        p.layers.map((l) => [l.diffuse, l.mask, l.slot, l.maskSlot]),
        [
            [0x10, null, 0, 0],
            [0x11, 0x13, 1, 2],
            [0x14, 0x15, 3, 3],
        ],
    );
    // (the second float4: the first is the specular power)
    assert.deepEqual(
        p.layers[0]!.colour.map((x) => +x.toFixed(3)),
        [0.8, 0.6, 0.4, 1],
    );
    assert.deepEqual(p.layers[1]!.colour, [1, 1, 1, 0.5]);
    // fewer slots than passes need: pass 0 alone
    assert.equal(mats.planMaterial(m, textures, MAPS, 4).layers.length, 1);
    // unlit (no light maps): pass 0 alone, no light maps
    const unlit = mats.planMaterial(
        material(mats.T_NORMAL_TERRAIN_3, [0x10, 0x90, 0x10, 0x11, 0x12]),
        textures,
        MAPS,
        3,
    );
    assert.deepEqual([unlit.layers.length, unlit.ambient, unlit.shadow], [1, null, null]);
});

test("pass colours: only where the template's are known and they read as colours", () => {
    assert.equal(mats.passColour(material(0x1234, [0x10], [[0.5, 0.5, 0.5, 1]]), 0), null);
    assert.equal(mats.passColour(material(mats.T_DIFFUSE_TERRAIN, [0x10], [[5, 5, 5, 1]]), 0), null);
    // normal-mapped templates lead with the specular power (x, 0, 0, 0): otherwise not read
    assert.equal(
        mats.passColour(
            material(
                mats.T_NORMAL_TERRAIN_4,
                [0x10],
                [
                    [1, 1, 1, 1],
                    [0.5, 0.5, 0.5, 1],
                ],
            ),
            0,
        ),
        null,
    );
});

test("masked layer: the diffuse map's alpha times its mask's brightness (sampled at the same place) and opacity", () => {
    const diffuse = { width: 2, height: 1, bgra: Buffer.from([10, 20, 30, 255, 40, 50, 60, 128]) };
    const mask = { width: 1, height: 1, bgra: Buffer.from([255, 255, 255, 255]) };
    assert.deepEqual([...mats.maskedLayer(diffuse, mask)], [10, 20, 30, 255, 40, 50, 60, 128]);
    const half = { width: 2, height: 1, bgra: Buffer.from([0, 0, 0, 255, 255, 255, 255, 255]) };
    assert.deepEqual([...mats.maskedLayer(diffuse, half, 0.5)], [10, 20, 30, 0, 40, 50, 60, 64]);
});

test("light: ambient map scaled plus the sun's colour times the shadow map; bilinear samples wrap", () => {
    const l = mats.light([0.2, 0.1, 0.0, 1], [0.5, 0.5, 0.5, 1], { sun: [1, 0.5, 0.25], ambientScale: 2, sunScale: 1 });
    assert.deepEqual(
        l.map((x) => +x.toFixed(3)),
        [0.5, 0.45, 0.525],
    );
    const d = { width: 2, height: 1, bgra: Buffer.from([0, 0, 0, 0, 255, 255, 255, 255]) };
    assert.deepEqual(mats.sample(d, 0.25, 0.5), [0, 0, 0, 0]);
    assert.deepEqual(
        mats.sample(d, 0.5, 0.5).map((x) => +x.toFixed(3)),
        [0.5, 0.5, 0.5, 0.5],
    );
    assert.deepEqual(mats.sample(d, 1.25, 0.5), [0, 0, 0, 0]); // (wraps)
});

test("refine: triangles split where the light inside differs; shared edges split for both; winding kept", () => {
    // two triangles sharing the edge 1-2 over a light that is dark only near the middle of that edge
    type V = { p: number[] };
    const verts: V[] = [{ p: [0, 0] }, { p: [10, 0] }, { p: [0, 10] }, { p: [10, 10] }];
    const lightAt = (p: readonly number[]): number => (Math.hypot(p[0]! - 5, p[1]! - 5) < 2 ? 0 : 1);
    const tris = mats.refine(
        verts,
        [
            [0, 1, 2],
            [1, 3, 2],
        ],
        (a, b) => ({ p: a.p.map((x, k) => (x + b.p[k]!) / 2) }),
        (v) => lightAt(v.p),
        (a, b, c, wa, wb) => lightAt(a.p.map((x, k) => x * wa + b.p[k]! * wb + c.p[k]! * (1 - wa - wb))),
        (a, b) => Math.hypot(a.p[0]! - b.p[0]!, a.p[1]! - b.p[1]!),
        { threshold: 0.5, minLength: 1, depth: 1, maxVertices: 100 },
    );
    assert.ok(tris.length > 2);
    // the middle of the shared edge (5, 5) is a vertex, used by triangles on both sides
    const mid = verts.findIndex((v) => v.p[0] === 5 && v.p[1] === 5);
    assert.ok(mid >= 0);
    const users = tris.filter((t) => t.includes(mid));
    assert.ok(users.some((t) => t.includes(0)) && users.some((t) => t.includes(3)));
    // winding: every triangle turns the same way as the originals (signed area > 0 with y up... here < 0)
    const area = (t: number[]): number => {
        const [a, b, c] = t.map((i) => verts[i]!.p) as [number[], number[], number[]];
        return (b[0]! - a[0]!) * (c[1]! - a[1]!) - (c[0]! - a[0]!) * (b[1]! - a[1]!);
    };
    assert.ok(tris.every((t) => area(t) > 0));
    // total area kept
    assert.equal(
        tris.reduce((s, t) => s + area(t), 0),
        200,
    );
    // nothing varies: nothing split
    const flat = mats.refine(
        [{ p: [0, 0] }, { p: [10, 0] }, { p: [0, 10] }] as V[],
        [[0, 1, 2]],
        (a, b) => ({ p: a.p.map((x, k) => (x + b.p[k]!) / 2) }),
        () => 1,
        () => 1,
        () => 100,
        { threshold: 0.1, minLength: 1, depth: 3, maxVertices: 100 },
    );
    assert.deepEqual(flat, [[0, 1, 2]]);
});

test("alpha use: a diffuse alpha is transparency only with a blend word or an alpha reference (or a decal / water template)", () => {
    const withState = (template: number, blend: number, ref: number): geom.GeomMaterial => {
        const m = material(template, [1]);
        m.params.writeUInt32BE(blend, mats.PARAM_BLEND);
        m.params.writeUInt32BE((ref << 24) >>> 0, mats.PARAM_ALPHA_REF);
        return m;
    };
    const solid = checksum("NormalMapEnvMap_3Pass");
    assert.equal(mats.drawsAlpha(withState(solid, 0, 0)), false); // (the gold piles: alpha is a mask)
    assert.equal(mats.drawsAlpha(withState(solid, mats.BLEND_ALPHA, 0)), true); // glass
    assert.equal(mats.drawsAlpha(withState(solid, 0, 0x96)), true); // alpha-tested: leaves, fences
    assert.equal(mats.drawsAlpha(withState(solid, mats.BLEND_ADD, 0)), true);
    assert.equal(mats.drawsAlpha(withState(checksum("Decal"), 0, 0)), true); // a decal by its nature
    assert.equal(mats.drawsAlpha(withState(checksum("WaterCubeRefraction"), 0, 0)), true);
    // (a block too short to hold the state words: kept, as before)
    const short = material(solid, []);
    short.params = short.params.subarray(0, 0x30);
    assert.equal(mats.drawsAlpha(short), true);
});

test("alpha test: a texture's alpha is made 0 / 255 by the reference before its mips", () => {
    const bgra = Buffer.alloc(8 * 8 * 4, 200);
    for (let i = 0; i < 64; i++) bgra[4 * i + 3] = i < 32 ? 100 : 200; // (top half under 150, bottom over)
    const t: xenos.XenosTexture = {
        checksum: 5,
        width: 8,
        height: 8,
        format: xenos.FMT_8888,
        endian: 0,
        tiled: false,
        pitch: 8,
        data: bgra,
        cube: false,
    };
    const kept = xenos.toThug2(t, true).texture!;
    assert.equal(kept.levels[0]![3], 100); // (untouched without a reference)
    const cut = xenos.toThug2(t, true, 150).texture!;
    assert.equal(cut.levels[0]![3], 0);
    assert.equal(cut.levels[0]![4 * 40 + 3], 255);
    assert.equal(cut.levels.length, 2); // (8 -> 4)
});

// --- environment maps (round 5) ----------------------------------------------------------------------------------------

// a 2 x 2 cube map of six flat faces (B, G, R, A), its fetch constant's "cube": the faces follow each other
const FACE_COLOURS = [
    [10, 20, 30], // +X
    [40, 50, 60], // -X
    [70, 80, 90], // +Y (the sky)
    [100, 110, 120], // -Y
    [130, 140, 150], // +Z
    [160, 170, 180], // -Z
];
const cubeTexture = (checksum: number): xenos.XenosTexture => {
    const size = 32; // (a surface is at least 32 x 32 texels: one face is 4096 bytes)
    const data = Buffer.alloc(6 * size * size * 4);
    FACE_COLOURS.forEach((c, f) => {
        for (let i = 0; i < size * size; i++) data.set([c[0]!, c[1]!, c[2]!, 255], (f * size * size + i) * 4);
    });
    return {
        checksum,
        width: size,
        height: size,
        format: xenos.FMT_8888,
        endian: 0,
        tiled: false,
        pitch: size,
        data: data.subarray(0, size * size * 4),
        cube: true,
        faces: data,
    };
};

test("cube map: six faces, one after the other, each as a plain base level", () => {
    const faces = xenos.cubeFaces(cubeTexture(1))!;
    assert.equal(faces.length, 6);
    faces.forEach((f, k) => {
        assert.deepEqual([...f.subarray(0, 4)], [...FACE_COLOURS[k]!, 255]);
    });
    assert.equal(xenos.cubeFaces({ ...cubeTexture(1), cube: false }), null);
});

test("cube lookup: Direct3D's faces by the major axis; the 2D map holds the forward hemisphere, v growing with the vector's y", () => {
    const faces = mats.decodeCube(cubeTexture(1))!;
    const colour = (x: number, y: number, z: number): number[] => mats.sampleCube(faces, x, y, z).slice(0, 3);
    assert.deepEqual(colour(1, 0, 0), FACE_COLOURS[0]);
    assert.deepEqual(colour(-1, 0.2, 0.1), FACE_COLOURS[1]);
    assert.deepEqual(colour(0.1, 1, 0), FACE_COLOURS[2]);
    assert.deepEqual(colour(0, -1, 0.3), FACE_COLOURS[3]);
    assert.deepEqual(colour(0.2, 0.1, 1), FACE_COLOURS[4]);
    assert.deepEqual(colour(0, 0, -1), FACE_COLOURS[5]);
    const n = 16;
    const map = mats.sphereMap(faces, 0.4, n);
    const at = (x: number, y: number): number[] => [...map.subarray(4 * (y * n + x), 4 * (y * n + x) + 4)];
    assert.deepEqual(at(8, 8), [...FACE_COLOURS[4]!, 102]); // (the middle: straight ahead, +z; alpha = the strength)
    assert.deepEqual(at(8, 15).slice(0, 3), FACE_COLOURS[2]); // (v grows with ry: the bottom row looks up, at the sky)
    assert.deepEqual(at(8, 0).slice(0, 3), FACE_COLOURS[3]);
    assert.deepEqual(at(15, 8).slice(0, 3), FACE_COLOURS[0]);
    assert.deepEqual(at(0, 8).slice(0, 3), FACE_COLOURS[1]);
});

test("environment layer: RemapX x the mask's brightness x ENV_STRENGTH; none for a cut-out or a faint reflection; water fixed", () => {
    const envTemplate = checksum("NormalMapEnvMap_3Pass");
    const white = (c: number): xenos.XenosTexture => ({
        checksum: c,
        width: 2,
        height: 2,
        format: xenos.FMT_8888,
        endian: 0,
        tiled: false,
        pitch: 2,
        data: Buffer.alloc(16, 255),
        cube: false,
    });
    const grey = (c: number): xenos.XenosTexture => ({ ...white(c), data: Buffer.alloc(16, 128) });
    const list = new Map<number, xenos.XenosTexture>([
        [0x10, white(0x10)],
        [0x12, white(0x12)],
        [0x13, cubeTexture(0x13)],
        [0x14, white(0x14)], // the mask
        [0x15, grey(0x15)], // a grey mask
        [0xa0, white(0xa0)],
        [0xa1, white(0xa1)],
    ]);
    // diffuse, normal, specular, cube, mask, ambient, shadow (the shader's order); float4s: spec power, colour, tiling,
    // spec colour, Remap
    const f4 = (remapX: number): number[][] => [
        [15, 0, 0, 0],
        [1, 1, 1, 1],
        [1, 1, 1, 0],
        [1, 1, 1, 1],
        [remapX, 1, 0, 0],
    ];
    const make = (remapX: number, mask: number, template = envTemplate): geom.GeomMaterial =>
        material(template, [0x10, 0x91, 0x12, 0x13, mask, 0xa0, 0xa1], f4(remapX));
    assert.deepEqual(mats.envLayer(make(1, 0x14), list), { cube: 0x13, strength: 0.5, water: false, body: null });
    assert.equal(mats.envLayer(make(2, 0x14), list)!.strength, 0.5); // (RemapX over 1 clamped)
    assert.equal(mats.envLayer(make(1, 0x15), list)!.strength, 0.3); // (a grey mask: a quarter of the surface, rounded to 0.1)
    assert.equal(mats.envLayer(make(0.01, 0x14), list), null); // (reflects nothing)
    // alpha-tested (a reference over 1, no blend word): a cut-out would show the reflection through its holes
    const tested = make(1, 0x14);
    tested.params = Buffer.concat([tested.params, Buffer.alloc(0x40)]);
    tested.params.writeUInt32BE((0x96 << 24) >>> 0, mats.PARAM_ALPHA_REF);
    assert.equal(mats.envLayer(tested, list), null);
    // not a reflective template, or no cube map fourth
    assert.equal(mats.envLayer(make(1, 0x14, checksum("DiffuseTerrain_3Pass")), list), null);
    assert.equal(mats.envLayer(material(envTemplate, [0x10, 0x91, 0x12, 0x14, 0x14, 0xa0, 0xa1], f4(1)), list), null);
    // water: fixed, the body colour the mean of the two colour float4s
    const water = material(
        checksum("WaterCubeRefraction"),
        [0x90, 0x90, 0x12, 0x13],
        [
            [15, 0, 0, 0],
            [1, 0, 0, 0],
            [0.4, 0.6, 0.2, 1],
            [0.0, 0.2, 0.2, 1],
        ],
    );
    const w = mats.envLayer(water, list)!;
    assert.equal(w.water, true);
    assert.equal(w.strength, 0.5);
    assert.deepEqual(
        w.body!.map((x) => +x.toFixed(2)),
        [0.2, 0.4, 0.2],
    );
});
