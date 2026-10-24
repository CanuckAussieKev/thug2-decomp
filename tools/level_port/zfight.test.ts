// NOT ORIGINAL - tests of zfight.ts (coplanar overlapping surfaces) and texture_check.ts (surfaces drawn without a
// real texture), on scenes built here.

import assert from "node:assert/strict";
import { test } from "node:test";
import type * as tex from "./tex.ts";
import { checkTextures, lookOf } from "./texture_check.ts";
import * as thug2Scene from "./thug2_scene.ts";
import {
    causeOf,
    clipPolygon,
    fightDistance,
    fightSeparation,
    findZFights,
    flickers,
    INCHES_PER_METRE,
    polygonArea,
    report,
    spotFor,
} from "./zfight.ts";

function material(checksum: number, texture = checksum, twoSided = false, zBias = 0): thug2Scene.Material {
    return {
        checksum,
        nameChecksum: checksum,
        alphaCutoff: 1,
        sorted: 0,
        drawOrder: 0,
        singleSided: 0,
        noBackfaceCulling: twoSided ? 1 : 0,
        zBias,
        grassify: 0,
        specularPower: 0,
        passes: [
            {
                texture,
                flags: texture ? thug2Scene.PASS_TEXTURED : 0,
                hasColor: 0,
                color: [0, 0, 0],
                blendMode: 0,
                fixedAlpha: 0,
                uAddress: 0,
                vAddress: 0,
                envmapTiling: [3, 3],
                filtering: 0x10004,
                mip: [1, 4, -8, -8],
            },
        ],
    };
}

// A floor quad (y = height; down: facing down) from (x0, z0) to (x1, z1), its corners as `order` gives them.
function quad(m: number, x0: number, z0: number, x1: number, z1: number, height = 0, down = false): thug2Scene.Mesh {
    const corners = [
        [x0, height, z0],
        [x0, height, z1],
        [x1, height, z0],
        [x1, height, z1],
    ];
    if (down) corners.splice(0, 4, corners[1]!, corners[0]!, corners[3]!, corners[2]!);
    const buffer = Buffer.alloc(corners.length * 12);
    corners.forEach((c, i) => {
        c.forEach((v, k) => {
            buffer.writeFloatLE(v, i * 12 + 4 * k);
        });
    });
    return {
        sphere: [0, 0, 0, 1],
        bbox: [x0, height, z0, x1, height, z1],
        flags: 0,
        material: m,
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

function scene(materials: thug2Scene.Material[], sectors: thug2Scene.Mesh[][]): thug2Scene.Scene {
    return {
        versions: [1, 1, 1],
        materials,
        sectors: sectors.map((meshes, i) => ({
            checksum: i + 1,
            bone: -1,
            flags: 0,
            bbox: [0, 0, 0, 0, 0, 0],
            sphere: [0, 0, 0, 1],
            meshes,
        })),
        hierarchyCount: 0,
        tail: Buffer.alloc(0),
    };
}

const M = INCHES_PER_METRE;

test("depth precision: the separation that fights from a distance and back", () => {
    for (const d of [100, 1000, 4000]) assert.ok(Math.abs(fightDistance(fightSeparation(d)) - d) < 1e-6);
    // (24 bits, a 12-inch near plane: a quarter inch apart fights from about 90 m)
    assert.ok(Math.abs(fightDistance(0.25) / M - 90) < 2);
});

test("polygon clipping: two squares overlapping by a quarter", () => {
    const a: [number, number][] = [
        [0, 0],
        [2, 0],
        [2, 2],
        [0, 2],
    ];
    const b: [number, number][] = [
        [1, 1],
        [3, 1],
        [3, 3],
        [1, 3],
    ];
    assert.equal(polygonArea(clipPolygon(a, b)), 1);
});

test("coplanar floors of two objects overlapping: one fight, its overlap area and place", () => {
    const s = scene([material(1), material(2)], [[quad(1, 0, 0, 200, 200)], [quad(2, 100, 0, 300, 200, 0.01)]]);
    const fights = findZFights(s, { minArea: 1 });
    assert.equal(fights.length, 1);
    const f = fights[0]!;
    assert.ok(Math.abs(f.area - 100 * 200) < 1, `area ${f.area}`);
    assert.equal(f.seenArea, f.area); // (0.01 inch apart: fights from ~16 m)
    assert.ok(Math.abs(f.maxSeparation - 0.01) < 1e-4);
    assert.ok(f.centre[0] > 100 && f.centre[0] < 200);
    assert.equal(f.opposite, false);
    assert.equal(f.biased, false);
    assert.equal(f.identicalArea, 0); // (different triangles: they shimmer)
    assert.deepEqual(spotFor(f.centre, f.normal).slice(1, 3).map(Math.round), [0, Math.round(f.centre[2])]);
});

test("not fights: apart more than the tolerance, side by side, within one mesh, one culled from behind", () => {
    const apart = scene([material(1), material(2)], [[quad(1, 0, 0, 200, 200)], [quad(2, 0, 0, 200, 200, 5)]]);
    assert.equal(findZFights(apart, { minArea: 1 }).length, 0);
    // (tilted: 6 inches up over its 200 - inside each other's box, but apart by more than the tolerance at one end)
    const tilted = quad(2, 0, 0, 200, 200);
    for (const i of [2, 3]) tilted.buffers[0]![1].writeFloatLE(6, i * 12 + 4);
    const tilt = scene([material(1), material(2)], [[quad(1, 0, 0, 200, 200, 1)], [tilted]]);
    assert.equal(findZFights(tilt, { minArea: 1, tolerance: 2 }).length, 0);
    assert.equal(findZFights(tilt, { minArea: 1, tolerance: 8 }).length, 1);
    const beside = scene([material(1), material(2)], [[quad(1, 0, 0, 200, 200)], [quad(2, 200, 0, 400, 200)]]);
    assert.equal(findZFights(beside, { minArea: 1 }).length, 0);
    const oneMesh = scene([material(1)], [[quad(1, 0, 0, 200, 200)]]);
    oneMesh.sectors[0]!.meshes.push(quad(1, 0, 0, 200, 200));
    assert.equal(findZFights(oneMesh, { minArea: 1 }).length, 1); // (two meshes of one sector do count)
    const sameMesh = scene([material(1)], [[quad(1, 0, 0, 200, 200)]]);
    assert.equal(findZFights(sameMesh, { minArea: 1 }).length, 0);
    const culled = scene([material(1), material(2)], [[quad(1, 0, 0, 200, 200)], [quad(2, 0, 0, 200, 200, 0, true)]]);
    assert.equal(findZFights(culled, { minArea: 1 }).length, 0);
});

test("flagged apart: opposite-facing two-sided, decided by a z bias, the same triangles", () => {
    const opposite = scene(
        [material(1), material(2, 2, true)],
        [[quad(1, 0, 0, 200, 200)], [quad(2, 0, 0, 200, 200, 0, true)]],
    );
    const o = findZFights(opposite, { minArea: 1 });
    assert.equal(o.length, 1);
    assert.equal(o[0]!.opposite, true);
    assert.equal(causeOf(o[0]!, opposite), "opposite-facing, two-sided");
    const biased = scene(
        [material(1), material(2, 2, false, 1)],
        [[quad(1, 0, 0, 200, 200)], [quad(2, 0, 50, 200, 250)]],
    );
    const b = findZFights(biased, { minArea: 1 });
    assert.equal(b[0]!.biased, true);
    assert.equal(causeOf(b[0]!, biased), "decided by a z bias");
    const same = scene([material(1), material(2)], [[quad(1, 0, 0, 200, 200)], [quad(2, 0, 0, 200, 200)]]);
    const d = findZFights(same, { minArea: 1 });
    assert.ok(Math.abs(d[0]!.identicalArea - d[0]!.area) < 1e-6);
    assert.match(causeOf(d[0]!, same), /same triangles/);
    // (equal depths on the same corners are not safe: the game's copies are triangulated and lit on their own)
    assert.equal(flickers(d[0]!), true);
    assert.equal(flickers(b[0]!), false);
    assert.equal(flickers(o[0]!), false);
    assert.match(report("level", same, d).join("\n"), /FLICKER .*1 pairs/);
});

test("walls: the pair found across x, the spot 120 inches out facing it", () => {
    // (the floor quads turned into walls: x = 0, facing +x)
    const wall = (m: number, z0: number, z1: number): thug2Scene.Mesh => {
        const q = quad(m, 0, z0, 100, z1);
        const b = q.buffers[0]![1];
        for (let i = 0; i < 4; i++) {
            const x = b.readFloatLE(i * 12);
            b.writeFloatLE(0, i * 12);
            b.writeFloatLE(x, i * 12 + 4);
        }
        return q;
    };
    const s = scene([material(1), material(2)], [[wall(1, 0, 200)], [wall(2, 100, 300)]]);
    const f = findZFights(s, { minArea: 1 });
    assert.equal(f.length, 1);
    const spot = spotFor(f[0]!.centre, f[0]!.normal);
    assert.equal(Math.round(Math.abs(spot[0])), 120);
    assert.ok([90, 270].includes(Math.round(spot[3])));
});

function texture(checksum: number, w: number, h: number, texel: (x: number, y: number) => number[]): tex.Texture {
    const level = Buffer.alloc(w * h * 4);
    for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) level.set(texel(x, y), 4 * (y * w + x));
    return {
        checksum,
        width: w,
        height: h,
        texelDepth: 32,
        paletteDepth: 32,
        dxt: 0,
        palette: Buffer.alloc(0),
        levels: [level],
    };
}

test("texture looks: one colour, a see-through dither, a cut-out (not a placeholder), a real texture", () => {
    const look = (t: tex.Texture) => lookOf(t.levels[0]!, t.width, t.height);
    const grey = look(texture(1, 32, 32, () => [128, 128, 128, 255]));
    assert.equal(grey.flat, true);
    const dither = look(texture(2, 16, 16, (x, y) => [200, 210, 220, (x + y) % 2 ? 0 : 255]));
    assert.equal(dither.stipple, true);
    const cutout = look(texture(3, 32, 32, (x) => [50, 90, 40, x < 16 ? 255 : 0]));
    assert.equal(cutout.stipple, false);
    assert.equal(cutout.flat, false);
    const real = look(texture(4, 32, 32, (x, y) => [(x * 8) & 255, (y * 8) & 255, 100, 255]));
    assert.equal(real.flat || real.stipple, false);
});

test("missing textures: a checksum the .tex hasn't, no texture, a flat colour; a real one isn't reported", () => {
    const s = scene(
        [material(1, 0x111), material(2, 0), material(3, 0x333), material(4, 0x444)],
        [
            [quad(1, 0, 0, 100, 100)],
            [quad(2, 200, 0, 300, 100)],
            [quad(3, 400, 0, 500, 100)],
            [quad(4, 600, 0, 700, 100)],
        ],
    );
    const textures: tex.TexFile = {
        version: 1,
        textures: [
            texture(0x333, 32, 32, () => [90, 90, 90, 255]),
            texture(0x444, 32, 32, (x, y) => [x * 8, y * 8, 0, 255]),
        ],
    };
    const findings = checkTextures(s, textures);
    const by = new Map(findings.map((f) => [f.cause, f]));
    assert.deepEqual([...by.keys()].sort(), ["flat colour", "missing", "untextured"]);
    assert.equal(by.get("missing")!.texture, 0x111);
    assert.equal(by.get("flat colour")!.texture, 0x333);
    assert.equal(by.get("untextured")!.triangles, 2);
    assert.ok(Math.abs(by.get("missing")!.area - 100 * 100) < 1e-6);
    assert.ok(by.get("flat colour")!.example!.centre[0]! > 400);
});
