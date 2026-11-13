import assert from "node:assert/strict";
import { test } from "node:test";
import { classify, RULES, type Stats, type Usage } from "./classes.ts";
import { blurWrap, classicalMaps, heightFrom, normalsFrom, type Rgba } from "./classical.ts";
import { decodePng, encodePng } from "./png.ts";

function image(width: number, height: number, at: (x: number, y: number) => number[]): Rgba {
    const data = new Uint8Array(width * height * 4);
    for (let y = 0; y < height; y++) {
        for (let x = 0; x < width; x++) data.set(at(x, y), (y * width + x) * 4);
    }
    return { width, height, data };
}

const decodeN = (byte: number) => (byte - 127.5) / 127.5;

test("a flat texture: flat normals, height 0.5, roughness the class's base", () => {
    const maps = classicalMaps(
        image(32, 32, () => [120, 120, 120, 255]),
        RULES.concrete,
    );
    for (let i = 0; i < 32 * 32; i++) {
        assert.ok(Math.abs(decodeN(maps[i * 4]!)) < 0.01 && Math.abs(decodeN(maps[i * 4 + 1]!)) < 0.01);
        assert.equal(maps[i * 4 + 3], 128);
        assert.ok(Math.abs(maps[i * 4 + 2]! / 255 - RULES.concrete.base) < 0.01);
    }
});

test("a brightening ramp along u tilts the normals toward -u (bright is high), none along v", () => {
    // a ridge: bright stripe down the middle column band
    const ridge = image(64, 64, (x) => {
        const v = Math.round(255 * Math.max(0, 1 - Math.abs(x - 32) / 6));
        return [v, v, v, 255];
    });
    const luma = new Float32Array(64 * 64);
    for (let i = 0; i < luma.length; i++) luma[i] = ridge.data[i * 4]! / 255;
    const heights = heightFrom(luma, 64, 64);
    assert.ok(heights[10 * 64 + 32]! > heights[10 * 64 + 20]!, "the bright ridge is high");
    const normals = normalsFrom(heights, 64, 64, 1);
    const left = (10 * 64 + 29) * 3; // on the ridge's rising (left) side: height grows with u
    const right = (10 * 64 + 35) * 3;
    assert.ok(normals[left]! < -0.05, `rising side tilts to -u (${normals[left]})`);
    assert.ok(normals[right]! > 0.05, `falling side tilts to +u (${normals[right]})`);
    assert.ok(Math.abs(normals[left + 1]!) < 1e-3, "no tilt along v");
    for (let i = 0; i < 64 * 64; i++) {
        const length = Math.hypot(normals[i * 3]!, normals[i * 3 + 1]!, normals[i * 3 + 2]!);
        assert.ok(Math.abs(length - 1) < 1e-5 && normals[i * 3 + 2]! > 0);
    }
});

test("rows grow downward: a ramp along the rows tilts y, not x", () => {
    const ramp = image(32, 64, (_, y) => {
        const v = Math.round(255 * Math.max(0, 1 - Math.abs(y - 32) / 6));
        return [v, v, v, 255];
    });
    const maps = classicalMaps(ramp, RULES.stone);
    const above = (29 * 32 + 5) * 4; // height grows with v here
    assert.ok(decodeN(maps[above + 1]!) < -0.05);
    assert.ok(Math.abs(decodeN(maps[above]!)) < 0.01);
});

test("the blur wraps round the edges and keeps the mean", () => {
    const src = new Float32Array(16 * 8);
    src[0] = 16 * 8;
    const out = blurWrap(src, 16, 8, 1);
    const sum = out.reduce((a, b) => a + b, 0);
    assert.ok(Math.abs(sum - 16 * 8) < 1e-3);
    assert.ok(out[15]! > 0 && out[7 * 16]! > 0, "spread across both edges");
});

test("roughness stays within the class's range", () => {
    let seed = 7;
    const noisy = image(64, 64, () => {
        seed = (Math.imul(seed, 1103515245) + 12345) >>> 0;
        const v = seed >>> 24;
        return [v, v, v, 255];
    });
    for (const rule of Object.values(RULES)) {
        const maps = classicalMaps(noisy, rule);
        for (let i = 0; i < 64 * 64; i++) {
            const r = maps[i * 4 + 2]! / 255;
            assert.ok(r >= rule.min - 0.003 && r <= rule.max + 0.003);
        }
    }
});

const STATS: Stats = {
    width: 128,
    height: 128,
    cutout: 0,
    translucent: 0,
    meanLuma: 0.5,
    saturation: 0.05,
    hue: -1,
    detail: 0.02,
    colours: 500,
    streaks: 0.1,
};
const BASE: Usage = { base: true, overlay: false, environment: false, shiny: false, decal: false, transparent: false };

test("classes: the skips and the usage rules", () => {
    assert.equal(classify({ ...STATS, width: 8 }, BASE).skip, "smaller than 16 texels");
    assert.ok(classify(STATS, { ...BASE, base: false, environment: true }).skip);
    assert.ok(classify(STATS, { ...BASE, base: false, overlay: true }).skip);
    assert.ok(classify(STATS, { ...BASE, decal: true }).skip);
    assert.ok(classify({ ...STATS, translucent: 0.5 }, { ...BASE, transparent: true }).skip);
    assert.equal(classify({ ...STATS, translucent: 0.02 }, { ...BASE, transparent: true }).name, "concrete");
    assert.ok(classify({ ...STATS, colours: 10 }, BASE).skip, "flat art or text");
    assert.equal(classify({ ...STATS, cutout: 0.5, hue: 110, saturation: 0.5 }, BASE).name, "foliage");
    assert.equal(classify(STATS, { ...BASE, shiny: true }).name, "metal");
    assert.equal(classify({ ...STATS, meanLuma: 0.2, detail: 0.05 }, BASE).name, "asphalt");
    assert.equal(classify(STATS, BASE).name, "concrete");
    assert.equal(classify({ ...STATS, hue: 30, saturation: 0.5, streaks: 0.8 }, BASE).name, "wood");
    assert.equal(classify({ ...STATS, hue: 300, saturation: 0.6 }, BASE).name, "default");
});

test("png: written and read back", () => {
    const rgba = Uint8Array.from({ length: 5 * 3 * 4 }, (_, i) => (i * 37) & 0xff);
    const back = decodePng(encodePng(5, 3, rgba));
    assert.equal(back.width, 5);
    assert.deepEqual([...back.data], [...rgba]);
});
