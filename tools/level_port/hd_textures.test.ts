// hd_textures.ts with a fake upscaler (nearest-neighbour x4): alpha handling, wrap / clamp padding, the size cap,
// mip chains, DXT round trips and the cache.

import assert from "node:assert/strict";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { test } from "node:test";
import type { Image } from "../game/frame_diff.ts";
import * as dxt from "./dxt.ts";
import * as hd from "./hd_textures.ts";
import type { TexFile, Texture } from "./tex.ts";

function nearest4(images: Map<string, Image>): Map<string, Image> {
    const out = new Map<string, Image>();
    for (const [name, img] of images) {
        const w = img.width * 4;
        const h = img.height * 4;
        const rgb = new Uint8Array(w * h * 3);
        for (let y = 0; y < h; y++)
            for (let x = 0; x < w; x++)
                for (let k = 0; k < 3; k++)
                    rgb[(y * w + x) * 3 + k] = img.rgb[((y >> 2) * img.width + (x >> 2)) * 3 + k]!;
        out.set(name, { width: w, height: h, rgb });
    }
    return out;
}

// BGRA texels from a function of (x, y)
function texels(w: number, h: number, f: (x: number, y: number) => [number, number, number, number]): Buffer {
    const out = Buffer.alloc(w * h * 4);
    for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) out.set(f(x, y), (y * w + x) * 4);
    return out;
}

function texture(checksum: number, w: number, h: number, data: Buffer, levels = 2, format = 0): Texture {
    const first = format ? dxt.encode(data, w, h, format) : data;
    const all = [first];
    for (let n = 1; n < levels; n++) all.push(Buffer.alloc(1));
    return {
        checksum,
        width: w,
        height: h,
        texelDepth: 32,
        paletteDepth: 32,
        dxt: format,
        palette: Buffer.alloc(0),
        levels: all,
    };
}

function options(cacheDir: string, scale = 4): hd.HdOptions {
    return { ...hd.defaultOptions(scale), cacheDir, minSize: 4 };
}

const tempDirs: string[] = [];
function tempCache(): string {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), "hd-test-"));
    tempDirs.push(dir);
    return dir;
}
process.on("exit", () => {
    for (const d of tempDirs) fs.rmSync(d, { recursive: true, force: true });
});

test("alpha kinds", () => {
    assert.equal(hd.alphaKind(texels(2, 2, () => [1, 2, 3, 255])), "opaque");
    assert.equal(hd.alphaKind(texels(2, 2, (x) => [1, 2, 3, x ? 255 : 0])), "binary");
    assert.equal(hd.alphaKind(texels(2, 2, (x) => [1, 2, 3, x ? 250 : 4])), "binary");
    assert.equal(hd.alphaKind(texels(2, 2, (x) => [1, 2, 3, x ? 255 : 100])), "smooth");
});

test("padding wraps or clamps per axis", () => {
    // 4 x 2: x in blue, y in green
    const t = texels(4, 2, (x, y) => [x * 10, y * 10, 0, 255]);
    const wrapped = hd.padded(t, 4, 2, 2, true, false, "rgb");
    assert.equal(wrapped.width, 8);
    assert.equal(wrapped.height, 6);
    const at = (img: Image, x: number, y: number): number[] => [
        ...img.rgb.subarray((y * img.width + x) * 3, (y * img.width + x) * 3 + 3),
    ];
    // RGB: red = BGRA[2] = 0, green = y * 10, blue = x * 10
    assert.deepEqual(at(wrapped, 0, 2), [0, 0, 20]); // x = -2 wraps to 2
    assert.deepEqual(at(wrapped, 7, 2), [0, 0, 10]); // x = 5 wraps to 1
    assert.deepEqual(at(wrapped, 2, 0), [0, 0, 0]); // y = -2 clamps to 0
    assert.deepEqual(at(wrapped, 2, 5), [0, 10, 0]); // y = 3 clamps to 1
    const alpha = hd.padded(
        texels(2, 2, (x) => [0, 0, 0, x ? 200 : 50]),
        2,
        2,
        1,
        false,
        false,
        "alpha",
    );
    assert.deepEqual(at(alpha, 0, 0), [50, 50, 50]);
    assert.deepEqual(at(alpha, 3, 0), [200, 200, 200]);
});

test("hidden texels take their visible neighbours' colour", () => {
    // left column visible red, the rest transparent black
    const t = texels(4, 1, (x) => (x === 0 ? [0, 0, 200, 255] : [0, 0, 0, 0]));
    const filled = hd.fillHidden(t, 4, 1, false, false);
    for (let x = 0; x < 4; x++) assert.deepEqual([...filled.subarray(x * 4, x * 4 + 4)], [0, 0, 200, x ? 0 : 255]);
});

test("a wrapped texture upscaled by the fake model has no seam and keeps its pattern", () => {
    const cache = tempCache();
    const src = texels(16, 16, (x, y) => [(x * 16) & 255, (y * 16) & 255, (x ^ y) & 255, 255]);
    const tex: TexFile = { version: 1, textures: [texture(1, 16, 16, src, 3)] };
    const stats = hd.newStats();
    hd.upscaleTexFile(tex, new Map(), options(cache), stats, nearest4);
    const t = tex.textures[0]!;
    assert.equal(t.width, 64);
    assert.equal(t.height, 64);
    assert.equal(t.levels.length, 5); // 3 + log2(4)
    assert.equal(t.levels[0]!.length, 64 * 64 * 4);
    assert.equal(t.levels[4]!.length, 4 * 4 * 4);
    // nearest x4: texel (x, y) is the source's (x >> 2, y >> 2)
    for (const [x, y] of [
        [0, 0],
        [63, 0],
        [0, 63],
        [37, 21],
    ] as const) {
        const s = ((y >> 2) * 16 + (x >> 2)) * 4;
        assert.deepEqual(
            [...t.levels[0]!.subarray((y * 64 + x) * 4, (y * 64 + x) * 4 + 4)],
            [...src.subarray(s, s + 4)],
        );
    }
    assert.equal(stats.generated, 1);
    assert.equal(stats.upscaled, 1);
});

test("cut-out alpha stays 0 / 255, smooth alpha goes through the model", () => {
    const cache = tempCache();
    const cut = texels(16, 16, (x) => [50, 60, 70, x < 8 ? 255 : 0]);
    const soft = texels(16, 16, (x) => [50, 60, 70, x * 16]);
    const tex: TexFile = { version: 1, textures: [texture(1, 16, 16, cut, 1), texture(2, 16, 16, soft, 1)] };
    hd.upscaleTexFile(tex, new Map([[1, { wrapU: false, wrapV: false }]]), options(cache, 2), hd.newStats(), nearest4);
    const [a, b] = tex.textures as [Texture, Texture];
    assert.equal(a.width, 32);
    assert.equal(a.levels.length, 1);
    const alphas = new Set<number>();
    for (let i = 3; i < a.levels[0]!.length; i += 4) alphas.add(a.levels[0]![i]!);
    assert.deepEqual(
        [...alphas].sort((p, q) => p - q),
        [0, 255],
    );
    // the cut edge stays at x = 16 of 32
    assert.equal(a.levels[0]![15 * 4 + 3], 255);
    assert.equal(a.levels[0]![16 * 4 + 3], 0);
    // the hidden half's colour is the visible half's, not black
    assert.deepEqual([...a.levels[0]!.subarray(31 * 4, 31 * 4 + 3)], [50, 60, 70]);
    // smooth alpha: the model's (nearest) result, 2 x 2 averaged: texel x has source x >> 1's alpha
    assert.equal(b.levels[0]![(10 * 32 + 9) * 4 + 3], 4 * 16);
});

test("the size cap halves the scale; tiny and single-colour textures are left", () => {
    const cache = tempCache();
    const o = { ...options(cache), maxSize: 64, minSize: 8 };
    const big = texels(32, 16, (x, y) => [x, y, 0, 255]);
    const capped = texels(64, 64, (x, y) => [x, y, 0, 255]);
    const tex: TexFile = {
        version: 1,
        textures: [
            texture(1, 32, 16, big),
            texture(2, 64, 64, capped),
            texture(
                3,
                4,
                4,
                texels(4, 4, (x) => [x, 0, 0, 255]),
            ),
            texture(
                4,
                32,
                32,
                texels(32, 32, () => [9, 9, 9, 255]),
            ),
        ],
    };
    const stats = hd.newStats();
    hd.upscaleTexFile(tex, new Map(), o, stats, nearest4);
    assert.deepEqual(
        tex.textures.map((t) => [t.width, t.height]),
        [
            [64, 32],
            [64, 64],
            [4, 4],
            [32, 32],
        ],
    );
    assert.equal(stats.upscaled, 1);
    assert.equal(stats.skipped.size, 3);
});

test("DXT textures stay DXT; the cache answers a second run", () => {
    const cache = tempCache();
    const src = texels(16, 16, (x, y) => [x * 15, y * 15, 128, x < 12 ? 255 : 0]);
    const make = (): TexFile => ({
        version: 1,
        textures: [texture(1, 16, 16, src, 3, 2), texture(2, 16, 16, src, 3, 5)],
    });
    const first = make();
    const stats = hd.newStats();
    let calls = 0;
    const counting: hd.Upscaler = (images) => {
        calls += 1;
        return nearest4(images);
    };
    hd.upscaleTexFile(first, new Map(), options(cache), stats, counting);
    const [d1, d5] = first.textures as [Texture, Texture];
    assert.equal(d1.dxt, 2);
    assert.equal(d5.dxt, 5);
    assert.equal(d1.levels[0]!.length, dxt.levelSize(64, 64, 2));
    assert.equal(d5.levels[0]!.length, dxt.levelSize(64, 64, 5));
    // DXT1's transparent texels come back transparent
    const decoded = dxt.decode(d1.levels[0]!, 64, 64, 2);
    assert.equal(decoded[(0 * 64 + 60) * 4 + 3], 0);
    assert.equal(decoded[(0 * 64 + 10) * 4 + 3], 255);
    assert.equal(calls, 1);
    assert.equal(stats.generated, 2);
    const second = make();
    const again = hd.newStats();
    hd.upscaleTexFile(second, new Map(), options(cache), again, counting);
    assert.equal(calls, 1);
    assert.equal(again.cacheHits, 2);
    assert.deepEqual(second.textures[1]!.levels, first.textures[1]!.levels);
    // another setting (the scale) is another entry
    hd.upscaleTexFile(make(), new Map(), options(cache, 2), hd.newStats(), counting);
    assert.equal(calls, 2);
});

test("matching pulls a model's colour shift back to the original; --hd-no-match keeps it", () => {
    const src = texels(16, 16, (x, y) => [40 + x * 8, 60 + y * 8, 120, 255]);
    const darker: hd.Upscaler = (images) => {
        const out = nearest4(images);
        for (const img of out.values())
            for (let i = 0; i < img.rgb.length; i++) img.rgb[i] = Math.round(img.rgb[i]! * 0.8);
        return out;
    };
    const run = (match: boolean): Buffer => {
        const tex: TexFile = { version: 1, textures: [texture(1, 16, 16, src, 1)] };
        hd.upscaleTexFile(tex, new Map(), { ...options(tempCache()), match }, hd.newStats(), darker);
        return tex.textures[0]!.levels[0]!;
    };
    // the 4 x 4 block of texel (5, 7), averaged
    const block = (level: Buffer, k: number): number => {
        let s = 0;
        for (let y = 28; y < 32; y++) for (let x = 20; x < 24; x++) s += level[(y * 64 + x) * 4 + k]!;
        return s / 16;
    };
    const original = src[(7 * 16 + 5) * 4 + 1]!;
    assert.ok(Math.abs(block(run(true), 1) - original) <= 1);
    assert.ok(Math.abs(block(run(false), 1) - original * 0.8) <= 1);
});

test("DXT encode / decode round trip stays close", () => {
    // (colours along a line in each block - what DXT can hold: one rising, one falling with x + y)
    const src = texels(8, 8, (x, y) => [(x + y) * 15, 210 - (x + y) * 15, 100, 255]);
    for (const format of [1, 5]) {
        const back = dxt.decode(dxt.encode(src, 8, 8, format), 8, 8, format);
        let worst = 0;
        for (let i = 0; i < src.length; i++) worst = Math.max(worst, Math.abs(src[i]! - back[i]!));
        assert.ok(worst <= 24, `DXT${format} worst error ${worst}`);
    }
});
