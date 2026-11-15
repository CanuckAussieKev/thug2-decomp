// pbr_maps.ts --delight: the image de-lit through the pipeline's delight.ts with its channels kept in RGBA order (a red
// surface stays red), its painted light taken out where the level says which way is up, and the maps then made from
// it; without the level's evidence, or in a model's dictionary, the image as it was.

import assert from "node:assert/strict";
import { test } from "node:test";
import type { SceneGeometry } from "../pipeline/hd_textures/delight_geometry.ts";
import { CLASS, RULES } from "./classes.ts";
import { classicalMaps, type Rgba } from "./classical.ts";
import { type DelightSource, delightedImage } from "./pbr_maps.ts";

const N = 64;

// a red wall lit brighter toward its top (linear light x 1.35 - x 0.74)
function litRed(): Rgba {
    const data = new Uint8Array(N * N * 4);
    for (let y = 0; y < N; y++)
        for (let x = 0; x < N; x++) {
            const light = Math.exp(-0.6 * ((y + 0.5) / N - 0.5));
            const grain = 1 + 0.1 * Math.sin(x * 2.1 + y * 1.3);
            const byte = (v: number): number => Math.min(255, Math.round((v * light * grain) ** (1 / 2.2) * 255));
            data.set([byte(0.4), byte(0.05), byte(0.03), 255], (y * N + x) * 4);
        }
    return { width: N, height: N, data };
}

// the level: the texture on one wall, its picture upright, mapped once (0 - 1: not tiled)
function source(up: boolean): DelightSource {
    const geometry = {
        facts: () => ({ area: 10, moving: false, backdrop: 0 }),
        evidence: () => ({ light: up ? [0, -1] : [0, 0], lines: [], faces: 2 }),
    } as unknown as SceneGeometry;
    return {
        orientations: new Map([[7, { area: 10, coherence: 1, surface: true }]]),
        uses: new Map([[7, [{ u0: 0, u1: 1, v0: 0, v1: 1, clampU: false, clampV: false }]]]),
        geometry,
    };
}

const row = (img: Rgba, y: number): number => {
    let s = 0;
    for (let x = 0; x < N; x++) s += img.data[(y * N + x) * 4]!;
    return s / N;
};

test("the de-lit image keeps its channels and loses its painted light", () => {
    const image = litRed();
    const delit = delightedImage(image, "levels/xx/xx.tex.xbx", 7, source(true));
    // red stays the strongest channel
    for (let i = 0; i < N * N; i++) assert.ok(delit.data[i * 4]! >= delit.data[i * 4 + 2]!);
    // the bright top and the dark foot closer together
    const before = row(image, 0) - row(image, N - 1);
    const after = row(delit, 0) - row(delit, N - 1);
    assert.ok(after < 0.6 * before, `light down the wall ${before.toFixed(1)} -> ${after.toFixed(1)}`);
    const name = Object.keys(CLASS)[0] as keyof typeof RULES;
    assert.equal(classicalMaps(delit, RULES[name]).length, N * N * 4);
});

test("without the level's evidence, or in a model's dictionary, the image as it was", () => {
    const image = litRed();
    assert.equal(delightedImage(image, "levels/xx/xx.tex.xbx", 7, source(false)), image);
    assert.equal(delightedImage(image, "models/skater/a.tex.xbx", 7, source(true)), image);
});
