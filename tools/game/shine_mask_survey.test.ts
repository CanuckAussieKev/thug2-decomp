import assert from "node:assert/strict";
import { test } from "node:test";
import type { Material, Pass } from "thug2-level-converter-core/level_port/thug2_scene.ts";
import { classOn, rayClassOf, reflectionOf } from "./shine_mask_survey.ts";

// The same cases as rt_scene_check's check_shine_masks (rt_surface_class.h reflection_class_on, after rt_record.cpp
// classify_reflective).
const pass = (over: Partial<Pass>): Pass => ({
    texture: 0x1234,
    flags: 0,
    hasColor: 1,
    color: [0.5, 0.5, 0.5],
    blendMode: 0,
    fixedAlpha: 0,
    uAddress: 0,
    vAddress: 0,
    envmapTiling: [1, 1],
    filtering: 0,
    mip: [0, 0, 0, 0],
    ...over,
});
const material = (passes: Pass[]): Material => ({
    checksum: 0x4d1670ea,
    nameChecksum: 0,
    alphaCutoff: 1,
    sorted: 0,
    drawOrder: 0,
    singleSided: 0,
    noBackfaceCulling: 1,
    zBias: 0,
    grassify: 0,
    specularPower: 0,
    passes,
});

// Training's quarter pipe (4D1670EA): an environment-mapped first pass (flags 0x6C, blend 5), the ramp's texture
// blended over it by its alpha (flags 0x64, blend 5)
const ramp = material([pass({ flags: 0x6c, blendMode: 5 }), pass({ flags: 0x64, blendMode: 5 })]);

test("the quarter pipe's shine map under its cover is glass by the material alone", () => {
    assert.equal(reflectionOf(ramp), "glass");
});

test("on its blended-opaque mesh it is a coat, on a seen-through one glass", () => {
    const opaque = rayClassOf(ramp, 1, 128 / 255);
    assert.equal(opaque, "solid", "texels 255, vertex alpha 0x80: drawn opaque");
    assert.equal(classOn("glass", opaque), "coat");
    const faded = rayClassOf(ramp, 1, 64 / 255);
    assert.equal(faded, "seen through", "vertex alpha 0x40");
    assert.equal(classOn("glass", faded), "glass");
});

test("painted glass, water and the game's other shines keep their class", () => {
    const painted = material([pass({}), pass({ flags: 0x8, blendMode: 13 })]);
    assert.equal(reflectionOf(painted), "painted glass");
    assert.equal(classOn(reflectionOf(painted), "solid"), "painted glass");
    const water = material([pass({ flags: 0x8 | 0x1, blendMode: 5 })]);
    assert.equal(reflectionOf(water), "water");
    const shiny = material([pass({}), pass({ flags: 0x8, blendMode: 1 })]);
    assert.equal(reflectionOf(shiny), "shiny");
    assert.equal(classOn("shiny", "solid"), "shiny");
    const dull = material([pass({ flags: 0x8, blendMode: 5, color: [0, 0, 0] })]);
    assert.equal(reflectionOf(dull), "none");
});
