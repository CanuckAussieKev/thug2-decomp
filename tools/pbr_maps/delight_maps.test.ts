// pbr_maps.ts --delight: the image de-lit through the pipeline's delight.ts with its channels kept in RGBA order (a red
// surface stays red), its broad light taken out, and the maps then made from it.

import assert from "node:assert/strict";
import { test } from "node:test";
import { CLASS, RULES } from "./classes.ts";
import { classicalMaps, type Rgba } from "./classical.ts";
import { delightedImage } from "./pbr_maps.ts";

const N = 64;

function litRed(): Rgba {
    const data = new Uint8Array(N * N * 4);
    for (let y = 0; y < N; y++)
        for (let x = 0; x < N; x++) {
            const light = Math.exp(0.5 * Math.cos((2 * Math.PI * x) / N));
            const grain = 1 + 0.1 * Math.sin(x * 2.1 + y * 1.3);
            const byte = (v: number): number => Math.min(255, Math.round((v * light * grain) ** (1 / 2.2) * 255));
            data.set([byte(0.4), byte(0.05), byte(0.03), 255], (y * N + x) * 4);
        }
    return { width: N, height: N, data };
}

test("the de-lit image keeps its channels and loses its broad light", () => {
    const image = litRed();
    const delit = delightedImage(image, 1);
    const column = (img: Rgba, x: number): number => {
        let s = 0;
        for (let y = 0; y < N; y++) s += img.data[(y * N + x) * 4]!;
        return s / N;
    };
    // red stays the strongest channel
    for (let i = 0; i < N * N; i++) assert.ok(delit.data[i * 4]! >= delit.data[i * 4 + 2]!);
    // the bright middle (x 0) and the dark edge (x N/2) closer together
    const before = column(image, 0) - column(image, N / 2);
    const after = column(delit, 0) - column(delit, N / 2);
    assert.ok(after < 0.6 * before, `light across the tile ${before.toFixed(1)} -> ${after.toFixed(1)}`);
    const name = Object.keys(CLASS)[0] as keyof typeof RULES;
    assert.equal(classicalMaps(delit, RULES[name]).length, N * N * 4);
});
