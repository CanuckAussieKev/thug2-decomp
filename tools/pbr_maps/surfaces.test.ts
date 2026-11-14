import assert from "node:assert/strict";
import { test } from "node:test";
import * as col from "thug2-level-converter-core/level_port/thug2_col.ts";
import { makeMaterial, THUG2_TERRAIN_TYPES } from "thug2-level-converter-core/level_port/thug2_material.ts";
import * as scene from "thug2-level-converter-core/level_port/thug2_scene.ts";
import { RULES } from "./classes.ts";
import { classicalMaps, type Rgba, scaleNormals } from "./classical.ts";
import { bumpFor, MIN_SCALE_FACTOR, type SurfaceUse, surfacesOf, TERRAIN_BUMP, texelSize } from "./surfaces.ts";

const terrain = (name: (typeof THUG2_TERRAIN_TYPES)[number]) => THUG2_TERRAIN_TYPES.indexOf(name);
const TEXTURE = 0x1234abcd;
const SECTOR = 0x5eed0001;

// A level of one quad, `size` units square on the ground (y up), its uvs 0 - `repeat`; its collision two faces of
// `faceTerrain`.
function level(size: number, repeat: number, faceTerrain: number): { scn: Buffer; col: Buffer } {
    const corners = [
        [0, 0, 0, 0, 0],
        [size, 0, 0, repeat, 0],
        [0, 0, size, 0, repeat],
        [size, 0, size, repeat, repeat],
    ];
    const vertex = (c: number[]) => {
        const b = Buffer.alloc(24);
        b.writeFloatLE(c[0]!, 0);
        b.writeFloatLE(c[1]!, 4);
        b.writeFloatLE(c[2]!, 8);
        b.writeUInt32LE(0x80808080, 12);
        b.writeFloatLE(c[3]!, 16);
        b.writeFloatLE(c[4]!, 20);
        return b;
    };
    const strip = [0, 1, 2, 3];
    const mesh: scene.Mesh = {
        sphere: [size / 2, 0, size / 2, size],
        bbox: [0, 0, 0, size, 0, size],
        flags: 0,
        material: 0x77,
        lods: [strip],
        strips: [strip, [], [], [], [], [], [], []],
        stride: 24,
        vertexCount: 4,
        buffers: [[0, Buffer.concat(corners.map(vertex))]],
        vertexShader: [0, 0],
        offsets: [0, 12, 16],
        hasWibble: 0,
        indexSets: 1,
        pixelShader: 0,
    };
    const scn = scene.writeScene({
        versions: [1, 1, 1],
        materials: [makeMaterial(0x77, TEXTURE, false, true)],
        sectors: [
            {
                checksum: SECTOR,
                bone: -1,
                flags: scene.SEC_TEXCOORDS,
                bbox: [0, 0, 0, size, 0, size],
                sphere: [size / 2, 0, size / 2, size],
                meshes: [mesh],
            },
        ],
        hierarchyCount: 0,
        tail: Buffer.alloc(0),
    });
    const verts = corners.map((c) => [c[0]!, c[1]!, c[2]!]);
    const faces: col.Face[] = [
        [col.FACE_SKATABLE, faceTerrain, 0, 2, 1],
        [col.FACE_SKATABLE, faceTerrain, 1, 2, 3],
    ];
    const object: col.ColObject = {
        checksum: SECTOR,
        flags: 0,
        smallFaces: 0,
        fixedVerts: 0,
        bbox: [0, 0, 0, 1, size, 0, size, 1],
        verts,
        intensities: Buffer.alloc(4, 128),
        faces,
        reach: Buffer.alloc(0),
        bsp: col.buildTree(verts, faces),
    };
    col.setReaches(object);
    return { scn, col: col.writeCol({ objects: [object] }) };
}

test("a texture's surfaces: its area, the terrain under it, its texel size", () => {
    const { scn, col: colFile } = level(512, 2, terrain("ASPHALT"));
    const use = surfacesOf(scn, colFile).get(TEXTURE);
    assert.ok(use);
    assert.ok(Math.abs(use.area - 512 * 512) < 1);
    assert.deepEqual([...use.terrains.keys()], [terrain("ASPHALT")]);
    assert.ok(Math.abs(use.terrains.get(terrain("ASPHALT"))! - 512 * 512) < 1);
    // 2 repeats of a 64-texel texture over 512 units: 4 units a texel
    assert.ok(Math.abs(texelSize(use, 64, 64)! - 4) < 1e-6);
    // no collision: no terrains, still the size
    const plain = surfacesOf(scn, null).get(TEXTURE)!;
    assert.equal(plain.terrains.size, 0);
    assert.ok(Math.abs(texelSize(plain, 64, 64)! - 4) < 1e-6);
});

const use = (terrains: [string, number][], area: number, unitsPerUv: number): SurfaceUse => ({
    area,
    terrains: new Map(
        terrains.map(([name, a]) => [THUG2_TERRAIN_TYPES.indexOf(name as (typeof THUG2_TERRAIN_TYPES)[number]), a]),
    ),
    logScale: area * Math.log(unitsPerUv),
    scaleArea: area,
});

test("smooth terrains calm a texture, rough ones keep it, coarse texels weaken it", () => {
    // fine texels (1 unit): no texel factor
    const road = bumpFor(RULES.concrete, use([["ASPHALT", 100]], 100, 128), 128, 128);
    const brick = bumpFor(RULES.stone, use([["BRICK", 100]], 100, 128), 128, 128);
    assert.equal(road.scale, 1);
    assert.ok(Math.abs(road.bump - Math.sqrt(RULES.concrete.bump * TERRAIN_BUMP.ASPHALT!)) < 1e-9);
    assert.ok(road.bump < 0.4 && brick.bump > 1.2, `${road.bump} ${brick.bump}`);
    // no evidence: the class's
    assert.equal(bumpFor(RULES.stone, undefined, 64, 64).bump, RULES.stone.bump);
    // DEFAULT says nothing
    assert.equal(bumpFor(RULES.stone, use([["DEFAULT", 100]], 100, 64), 64, 64).bump, RULES.stone.bump);
    // a quarter covered: halfway (in logarithms) to the full blend
    const part = bumpFor(RULES.concrete, use([["ASPHALT", 25]], 100, 128), 128, 128);
    assert.ok(part.bump < RULES.concrete.bump && part.bump > road.bump);
    // stretched: 3 units a texel halves it at the reference 1.5; far coarser stops at the floor
    const stretched = bumpFor(RULES.stone, use([], 100, 3 * 128), 128, 128);
    assert.ok(Math.abs(stretched.scale - 0.5) < 1e-9);
    assert.equal(bumpFor(RULES.stone, use([], 100, 50 * 128), 128, 128).scale, MIN_SCALE_FACTOR);
    // glass stays flat
    assert.equal(bumpFor(RULES.glass, use([["BRICK", 100]], 100, 128), 128, 128).bump, 0);
});

test("a lower bump tilts the classical normals less; scaleNormals agrees with it", () => {
    const size = 32;
    const data = new Uint8Array(size * size * 4);
    for (let i = 0; i < size * size; i++) {
        const v = ((i * 2654435761) >>> 24) & 0xff; // noise
        data.set([v, v, v, 255], i * 4);
    }
    const image: Rgba = { width: size, height: size, data };
    const tilt = (maps: Uint8Array) => {
        let sum = 0;
        for (let i = 0; i < size * size; i++)
            sum += Math.abs(maps[i * 4]! - 127.5) + Math.abs(maps[i * 4 + 1]! - 127.5);
        return sum / (size * size);
    };
    const full = classicalMaps(image, RULES.asphalt, 1);
    const calm = classicalMaps(image, RULES.asphalt, 0.2);
    assert.ok(tilt(calm) < tilt(full) * 0.4, `${tilt(calm)} ${tilt(full)}`);
    assert.ok(Math.abs(tilt(scaleNormals(full, 0.2)) - tilt(calm)) < 1.5);
    assert.equal(scaleNormals(full, 1), full);
});
