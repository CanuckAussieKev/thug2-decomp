// NOT ORIGINAL - tests of overlay_bias.ts (z bias for layers lying on other surfaces) and of THPS3's mip chains.

import assert from "node:assert/strict";
import { test } from "node:test";
import { convertTextures } from "../thps3_levels/thps3_levels.ts";
import { biasOverlays } from "./overlay_bias.ts";
import * as thug2Scene from "./thug2_scene.ts";

function material(checksum: number, flags = thug2Scene.PASS_TEXTURED, drawOrder = 0, texture = checksum) {
    const pass: thug2Scene.Pass = {
        texture,
        flags,
        hasColor: 0,
        color: [0, 0, 0],
        blendMode: flags & thug2Scene.PASS_TRANSPARENT ? 5 : 0,
        fixedAlpha: 0,
        uAddress: 0,
        vAddress: 0,
        envmapTiling: [3, 3],
        filtering: 0x10004,
        mip: [1, 4, -8, -8],
    };
    return {
        checksum,
        nameChecksum: checksum,
        alphaCutoff: 1,
        sorted: 0,
        drawOrder,
        singleSided: 0,
        noBackfaceCulling: 0,
        zBias: 0,
        grassify: 0,
        specularPower: 0,
        passes: [pass],
    } satisfies thug2Scene.Material;
}

// A floor quad (y = height, facing up; down: a ceiling) from (x0, z0) to (x1, z1) as a 4-index strip, positions only.
function quad(
    materialChecksum: number,
    x0: number,
    z0: number,
    x1: number,
    z1: number,
    height = 0,
    down = false,
): thug2Scene.Mesh {
    const corners = [
        [x0, height, z0],
        [x0, height, z1],
        [x1, height, z0],
        [x1, height, z1],
    ];
    if (down) corners.splice(0, 4, corners[1]!, corners[0]!, corners[3]!, corners[2]!); // (each triangle wound the other way)
    const buffer = Buffer.alloc(corners.length * 12);
    corners.forEach((c, i) => {
        c.forEach((v, k) => {
            buffer.writeFloatLE(v, i * 12 + 4 * k);
        });
    });
    return {
        sphere: [0, 0, 0, 1],
        bbox: [0, 0, 0, 0, 0, 0],
        flags: 0,
        material: materialChecksum,
        lods: [[0, 1, 2, 3]],
        strips: [[0, 1, 2, 3]],
        stride: 12,
        vertexCount: 4,
        buffers: [[0, buffer]],
        vertexShader: [0x2, 0],
        offsets: [0, 12, 12],
        hasWibble: 0,
        indexSets: 1,
        pixelShader: 2,
    };
}

function scene(materials: thug2Scene.Material[], meshes: thug2Scene.Mesh[]): thug2Scene.Scene {
    return {
        versions: [1, 1, 1],
        materials,
        sectors: meshes.map((m, i) => ({
            checksum: i + 1,
            bone: -1,
            flags: 0,
            bbox: [0, 0, 0, 0, 0, 0],
            sphere: [0, 0, 0, 1],
            meshes: [m],
        })),
        hierarchyCount: 0,
        tail: Buffer.alloc(0),
    };
}

const zBiases = (s: thug2Scene.Scene) => s.materials.map((m) => m.zBias);
const LAYER = thug2Scene.PASS_TEXTURED | thug2Scene.PASS_TRANSPARENT | thug2Scene.PASS_DECAL;

test("a decal lying on a floor gets a z bias, the floor and what lies elsewhere don't", () => {
    const s = scene(
        [material(1), material(2, LAYER, 1500), material(3, LAYER, 1500)],
        [quad(1, 0, 0, 100, 100), quad(2, 10, 10, 30, 30, 0.25), quad(3, 200, 200, 220, 220)],
    );
    assert.equal(biasOverlays(s), 1);
    assert.deepEqual(zBiases(s), [0, 1, 0]);
});

test("a layer on a layer goes one higher; one further off the surface is left alone", () => {
    const s = scene(
        [material(1), material(2, LAYER, 1500), material(3, LAYER, 1501), material(4, LAYER, 1500)],
        [
            quad(1, 0, 0, 100, 100),
            quad(2, 10, 10, 50, 50, 0.1),
            quad(3, 20, 20, 30, 30, 0.2),
            quad(4, 60, 60, 70, 70, 5), // (5 units above: not on the floor)
        ],
    );
    biasOverlays(s);
    assert.deepEqual(zBiases(s), [0, 1, 2, 0]);
});

test("two opaque surfaces at the same draw order: the patch, not the surface it lies on", () => {
    const s = scene([material(1), material(2)], [quad(1, 0, 0, 100, 100), quad(2, 40, 40, 60, 60)]);
    biasOverlays(s);
    assert.deepEqual(zBiases(s), [0, 1]);
});

test("the later draw order is the top one, whatever the look; back to back surfaces don't count", () => {
    const s = scene(
        [material(1, LAYER, 0), material(2, thug2Scene.PASS_TEXTURED, 1)],
        [quad(1, 0, 0, 100, 100), quad(2, 40, 40, 60, 60)],
    );
    biasOverlays(s);
    assert.deepEqual(zBiases(s), [0, 1]);
    const backToBack = scene([material(1), material(2)], [quad(1, 0, 0, 100, 100), quad(2, 40, 40, 60, 60, 0, true)]);
    assert.equal(biasOverlays(backToBack), 0);
});

test("left alone: alternative geometry covering each other alike, same-looking copies, a bigger bias", () => {
    const alike = scene([material(1), material(2)], [quad(1, 0, 0, 100, 100), quad(2, 0, 0, 100, 100)]);
    assert.equal(biasOverlays(alike), 0);
    const copies = scene(
        [material(1), material(2, thug2Scene.PASS_TEXTURED, 0, 1)],
        [quad(1, 0, 0, 100, 100), quad(2, 40, 40, 60, 60)],
    );
    assert.equal(biasOverlays(copies), 0);
    const kept = scene([material(1), material(2, LAYER, 1500)], [quad(1, 0, 0, 100, 100), quad(2, 10, 10, 30, 30)]);
    kept.materials[1]!.zBias = 3;
    assert.equal(biasOverlays(kept), 0);
    assert.deepEqual(zBiases(kept), [0, 3]);
});

test("THPS3's textures get mip levels down to a shorter side of 4, each 2 x 2 averaged", () => {
    const texel = (v: number) => Buffer.from([v, v, v, 255]);
    const level0 = Buffer.concat(Array.from({ length: 128 * 64 }, (_, i) => texel(i % 2 ? 200 : 100)));
    const [t] = convertTextures([{ name: "grate", width: 128, height: 64, levels: [level0] }] as never).textures;
    assert.deepEqual(
        t!.levels.map((l) => l.length / 4),
        [128 * 64, 64 * 32, 32 * 16, 16 * 8, 8 * 4],
    );
    assert.deepEqual([...t!.levels[1]!.subarray(0, 4)], [150, 150, 150, 255]);
    const small = Buffer.concat(Array.from({ length: 16 * 4 }, () => texel(9)));
    assert.equal(
        convertTextures([{ name: "edge", width: 16, height: 4, levels: [small] }] as never).textures[0]!.levels.length,
        1,
    );
});

test("lookAlikes (THP8): same-looking copies and surfaces covering each other alike get a bias too, one over the other", () => {
    // (two copies of one surface overlapping in part: one goes over the other)
    const copies = scene(
        [material(1, thug2Scene.PASS_TEXTURED, 0, 9), material(2, thug2Scene.PASS_TEXTURED, 0, 9)],
        [quad(1, 0, 0, 100, 100), quad(2, 40, 40, 160, 160)],
    );
    assert.equal(biasOverlays(copies, true), 1);
    assert.equal(Math.max(...zBiases(copies)), 1);
    // (other looks, neither clearly on top: still told apart)
    const alike = scene([material(1), material(2)], [quad(1, 0, 0, 100, 100), quad(2, 40, 40, 160, 160)]);
    assert.equal(biasOverlays(alike, true), 1);
    assert.equal(Math.max(...zBiases(alike)), 1);
    // (off by default)
    const off = scene([material(1), material(2)], [quad(1, 0, 0, 100, 100), quad(2, 40, 40, 160, 160)]);
    assert.equal(biasOverlays(off), 0);
});
