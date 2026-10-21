// NOT ORIGINAL - tests of THP8's material reading (materials.ts) on synthetic data (no game data): light maps found by
// their place in the texture lists, the terrain templates' layers and uv slots, pass colours, the masked layer
// texture, the light from the maps and the refining of lit triangles.

import assert from "node:assert/strict";
import { test } from "node:test";
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
