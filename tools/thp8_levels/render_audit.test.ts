// NOT ORIGINAL - tests of render_audit.ts: why two THP8 surfaces overlap (fightCause), what convertScenes draws a
// material with (baseTexture) and what it does with a blended base's layers (layerDrops) - on data built here.

import assert from "node:assert/strict";
import { test } from "node:test";
import type * as thug2Scene from "../level_port/thug2_scene.ts";
import type * as zfight from "../level_port/zfight.ts";
import type * as geom from "./geom.ts";
import type * as mats from "./materials.ts";
import { type AuditPart, fightCause, layerDrops, materialOrigins, type Origin } from "./render_audit.ts";
import { baseTexture } from "./thp8_levels.ts";
import * as xenos from "./xenos.ts";

const geomMaterial = (checksum: number, template: number, textures: number[]): geom.GeomMaterial => ({
    checksum,
    flags: 0,
    template,
    textures,
    terrain: 0,
    state: 0,
    params: Buffer.alloc(0x80),
});

const part = (zone: string, kind: AuditPart["kind"], materials: geom.GeomMaterial[] = []): AuditPart => ({
    zone,
    kind,
    geom: { materials, sectors: [], bbox: [0, 0, 0, 0, 0, 0] },
    textures: new Map(),
    elsewhere: new Map(),
    nodes: new Set(),
    masters: new Map(),
    local: new Set(),
});

const sceneMaterial = (checksum: number, texture: number, flags = 4): thug2Scene.Material => ({
    checksum,
    nameChecksum: checksum,
    alphaCutoff: 1,
    sorted: 0,
    drawOrder: 0,
    singleSided: 0,
    noBackfaceCulling: 0,
    zBias: 0,
    grassify: 0,
    specularPower: 0,
    passes: [
        {
            texture,
            flags,
            hasColor: 0,
            color: [0, 0, 0],
            blendMode: 0,
            fixedAlpha: 0,
            uAddress: 0,
            vAddress: 0,
            envmapTiling: [0, 0],
            filtering: 0,
            mip: [0, 0, 0, 0],
        },
    ],
});

const fight = (a: number, b: number, ma: number, mb: number): zfight.Fight => ({
    a: { sector: a, mesh: 0, material: ma },
    b: { sector: b, mesh: 0, material: mb },
    area: 1,
    seenArea: 1,
    maxSeparation: 0,
    minSeparation: 0,
    fightsFrom: 0,
    centre: [0, 0, 0],
    normal: [0, 1, 0],
    opposite: false,
    biased: false,
    triangles: 1,
    identicalArea: 0,
});

test("why two THP8 surfaces overlap: ours (stand-ins, veils), zone seams, decals, duplicates", () => {
    const DECAL = 0xdec;
    const PLAIN = 0x1;
    const templates = new Map([
        [DECAL, "Decal"],
        [PLAIN, "DiffuseTerrain_3Pass"],
    ]);
    const houses = part("z_houses", "zone", [geomMaterial(10, PLAIN, [0x100]), geomMaterial(11, DECAL, [0x101])]);
    const shops = part("z_shops", "zone", [geomMaterial(12, PLAIN, [0x100])]);
    const lod = part("z_shops_lod", "stand-in", [geomMaterial(13, PLAIN, [0x102])]);
    const veil = part("z_online_suburbs", "barrier");
    // sectors 1, 2: houses; 3: shops; 4: the stand-in; 5: the veil
    const origins = new Map<number, Origin>([
        [1, { part: houses }],
        [2, { part: houses }],
        [3, { part: shops }],
        [4, { part: lod }],
        [5, { part: veil }],
    ]);
    const scene: thug2Scene.Scene = {
        versions: [1, 1, 1],
        materials: [
            sceneMaterial(10, 0x100),
            sceneMaterial(11, 0x101),
            sceneMaterial(12, 0x100),
            sceneMaterial(13, 0x102),
        ],
        sectors: [1, 2, 3, 4, 5].map((checksum) => ({
            checksum,
            bone: -1,
            flags: 0,
            bbox: [0, 0, 0, 0, 0, 0],
            sphere: [0, 0, 0, 0],
            meshes: [],
        })),
        hierarchyCount: 0,
        tail: Buffer.alloc(0),
    };
    const materials = materialOrigins([houses, shops, lod, veil]);
    const cause = (a: number, b: number, ma: number, mb: number) =>
        fightCause(fight(a, b, ma, mb), scene, origins, materials, templates);
    assert.equal(cause(0, 3, 0, 3), "stand-in (_lod) over the level (ours)");
    assert.equal(cause(0, 4, 0, 0), "barrier veil on the level (ours)");
    assert.equal(cause(0, 2, 0, 2), "zone seam: two zones draw it");
    assert.equal(cause(0, 1, 0, 1), "THP8 decal (its depth bias not converted)");
    assert.equal(cause(0, 1, 0, 0), "duplicate surface in one zone (same texture)");
    assert.equal(cause(0, 0, 0, 1), "THP8 decal (its depth bias not converted)");
});

test("the texture pass 0 is drawn with: the plan's first layer, else the first that isn't a normal map", () => {
    const tex = (checksum: number, format: number): xenos.XenosTexture => ({
        checksum,
        width: 4,
        height: 4,
        format,
        endian: 0,
        tiled: false,
        pitch: 32,
        data: Buffer.alloc(0),
        cube: false,
    });
    const textures = new Map([
        [1, tex(1, xenos.FMT_DXN)],
        [2, tex(2, xenos.FMT_DXT1)],
    ]);
    const m = geomMaterial(5, 0, [1, 2]);
    assert.equal(baseTexture(m, null, textures), 2);
    const plan: mats.MaterialPlan = {
        layers: [{ diffuse: 7, mask: null, slot: 0, maskSlot: 0, colour: [1, 1, 1, 1] }],
        ambient: null,
        shadow: null,
    };
    assert.equal(baseTexture(m, plan, textures), 7);
    // (a blended base - DXT5 - keeps none of its layers)
    const layered: mats.MaterialPlan = {
        layers: [...plan.layers, { diffuse: 2, mask: 2, slot: 1, maskSlot: 1, colour: [1, 1, 1, 1] }],
        ambient: null,
        shadow: null,
    };
    const drops = layerDrops(layered, tex(7, xenos.FMT_DXT5), [], textures, () => null);
    assert.equal(drops.dropped, 1);
    assert.equal(drops.why, "base texture has alpha");
});

test("pass 0 skips blanks: a flat grey or a dither for the material's real texture; a dither alone -> untextured", () => {
    const bgra = (w: number, h: number, texel: (x: number, y: number) => number[]): Buffer => {
        const b = Buffer.alloc(w * h * 4);
        for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) Buffer.from(texel(x, y)).copy(b, 4 * (y * w + x));
        return b;
    };
    const make = (checksum: number, w: number, h: number, data: Buffer): xenos.XenosTexture => ({
        checksum,
        width: w,
        height: h,
        format: xenos.FMT_8888,
        endian: 0,
        tiled: false,
        pitch: w,
        data,
        cube: false,
    });
    // 1: flat grey 32 x 32; 2: the grass (noisy) 64 x 64; 3: a 16 x 16 dither; 4: white; 5: a smaller real texture
    const noise = (x: number, y: number): number[] => [
        (x * 37 + y * 11) % 256,
        (x * 5 + y * 71) % 256,
        (x * 13) % 256,
        255,
    ];
    const textures = new Map([
        [
            1,
            make(
                1,
                32,
                32,
                bgra(32, 32, () => [129, 128, 129, 255]),
            ),
        ],
        [2, make(2, 64, 64, bgra(64, 64, noise))],
        [
            3,
            make(
                3,
                16,
                16,
                bgra(16, 16, (x) => [200, 200, 200, x % 2 ? 255 : 0]),
            ),
        ],
        [
            4,
            make(
                4,
                32,
                32,
                bgra(32, 32, () => [255, 255, 255, 255]),
            ),
        ],
        [5, make(5, 16, 16, bgra(16, 16, noise))],
    ]);
    // the lawn: grey ahead of the grass (and a smaller real one)
    assert.equal(baseTexture(geomMaterial(1, 0, [1, 5, 2]), null, textures), 2);
    // a dither ahead of a real texture
    assert.equal(baseTexture(geomMaterial(1, 0, [3, 5]), null, textures), 5);
    // a light map isn't taken for the real texture
    const maps = { ambient: new Set([2]), shadow: new Set<number>() };
    assert.equal(baseTexture(geomMaterial(1, 0, [1, 2]), null, textures, maps), 1);
    // only a dither: untextured; only flat colours: the flat one stays (paint tinted by the pass colour)
    assert.equal(baseTexture(geomMaterial(1, 0, [3]), null, textures), undefined);
    assert.equal(baseTexture(geomMaterial(1, 0, [4]), null, textures), 4);
    assert.equal(baseTexture(geomMaterial(1, 0, [3, 4]), null, textures), 4);
    // a real texture is left alone
    assert.equal(baseTexture(geomMaterial(1, 0, [5, 2]), null, textures), 5);
});
