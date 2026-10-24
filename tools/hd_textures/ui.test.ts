// ui.ts with a fake upscaler (nearest-neighbour x4): images (paletted, 32-bit, swizzled or not, the picture smaller
// than its texture) and fonts come back `scale` times as large with the markers the DLL reads (src/game/gfx/hd_ui.h).

import assert from "node:assert/strict";
import * as fs from "node:fs";
import * as os from "node:os";
import * as path from "node:path";
import { test } from "node:test";
import type { Image } from "../game/frame_diff.ts";
import { writeImg } from "../menu_level/level_select_icons.ts";
import * as ui from "./ui.ts";

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

const caches: string[] = [];
function options(scale = 4) {
    const cacheDir = fs.mkdtempSync(path.join(os.tmpdir(), "hd-ui-test-"));
    caches.push(cacheDir);
    return { ...ui.uiOptions(scale), cacheDir, minSize: 4 };
}
test.after(() => {
    for (const dir of caches) fs.rmSync(dir, { recursive: true, force: true });
});

// A 16-colour 16 x 16 paletted image with a 12 x 10 picture (the header's image size changed by hand).
function paletted(): Buffer {
    const palette: [number, number, number, number][] = [];
    for (let i = 0; i < 16; i++) palette.push([i * 16, 255 - i * 16, (i * 37) & 255, 255]);
    const indices: number[] = [];
    for (let y = 0; y < 16; y++) for (let x = 0; x < 16; x++) indices.push((x >> 2) + 4 * (y >> 2));
    const data = writeImg(16, 16, palette, indices);
    data.writeUInt16LE(12, 24);
    data.writeUInt16LE(10, 26);
    return data;
}

test("a paletted image: 32-bit, x4, marked, the picture where it was", () => {
    const original = paletted();
    const { out, stats } = ui.upscaleUi(new Map([["images\\a.img.xbx", original]]), options(4), nearest4);
    assert.equal(stats.upscaled, 1);
    const hd = out.get("images\\a.img.xbx")!;
    assert.equal(hd.readUInt32LE(20), (ui.HD_IMG_MARKER | 4) >>> 0);
    assert.equal(hd.readUInt32LE(16), 0);
    assert.equal(hd.length, 32 + 64 * 64 * 4);
    const a = ui.decodeImg(original);
    const b = ui.decodeImg(hd);
    assert.deepEqual([b.tw, b.th, b.iw, b.ih], [64, 64, 48, 40]);
    for (let y = 0; y < b.ih; y++)
        for (let x = 0; x < b.iw; x++) {
            const s = ((y >> 2) * a.tw + (x >> 2)) * 4;
            const o = (y * b.tw + x) * 4;
            assert.deepEqual([...b.bgra.subarray(o, o + 4)], [...a.bgra.subarray(s, s + 4)], `texel ${x},${y}`);
        }
    // outside the picture: its edge repeated
    const edge = ((b.ih - 1) * b.tw + b.iw - 1) * 4;
    const corner = ((b.th - 1) * b.tw + b.tw - 1) * 4;
    assert.deepEqual([...b.bgra.subarray(corner, corner + 4)], [...b.bgra.subarray(edge, edge + 4)]);
});

test("x2, and a 32-bit image that isn't a power of two (rows unswizzled)", () => {
    const w = 12;
    const h = 6;
    const head = Buffer.alloc(32);
    for (const [at, v] of [
        [0, 2],
        [4, 8],
        [8, w],
        [12, h],
    ] as const)
        head.writeUInt32LE(v, at);
    head.writeUInt16LE(w, 24);
    head.writeUInt16LE(h, 26);
    const texels = Buffer.alloc(w * h * 4);
    for (let i = 0; i < w * h; i++) texels.set([i * 3, i, 255 - i, 255], i * 4);
    const original = Buffer.concat([head, texels]);
    const { out } = ui.upscaleUi(new Map([["x.img.xbx", original]]), options(2), nearest4);
    const b = ui.decodeImg(out.get("x.img.xbx")!);
    const a = ui.decodeImg(original);
    assert.deepEqual([b.tw, b.th, b.iw, b.ih], [24, 12, 24, 12]);
    assert.equal(out.get("x.img.xbx")!.readUInt32LE(20), (ui.HD_IMG_MARKER | 2) >>> 0);
    // (nearest x4 then 2 x 2 averaged: each texel's 2 x 2 block is the original's colour)
    for (let y = 0; y < 12; y++)
        for (let x = 0; x < 24; x++) {
            const s = ((y >> 1) * w + (x >> 1)) * 4;
            const o = (y * 24 + x) * 4;
            assert.deepEqual([...b.bgra.subarray(o, o + 3)], [...a.bgra.subarray(s, s + 3)]);
        }
});

function font(): Buffer {
    const count = 2;
    const head = Buffer.alloc(0x14 + count * 6);
    head.writeUInt32LE(1, 4);
    head.writeUInt32LE(count, 8);
    head.writeUInt32LE(16, 12);
    head.writeUInt32LE(12, 16);
    head.writeUInt16LE(12, 0x14);
    head.writeUInt16LE(65, 0x16);
    const page = Buffer.alloc(0x10);
    page.writeUInt16LE(16, 4);
    page.writeUInt16LE(16, 6);
    page.writeUInt32LE(0x00450048, 8);
    const indices = Buffer.alloc(256);
    for (let i = 0; i < 256; i++) indices[i] = (i >> 4) & 1 ? 1 : 0;
    const palette = Buffer.alloc(0x400);
    palette.set([255, 255, 255, 0x80], 4); // RGBA, PS2 alpha
    const rects = Buffer.alloc(4 + count * 8);
    rects.writeUInt32LE(count, 0);
    rects.writeUInt16LE(8, 4 + 4);
    rects.writeUInt16LE(8, 4 + 6);
    return Buffer.concat([head, page, indices, palette, rects]);
}

test("a font: the page x4 as 32-bit, marked; everything else as it was", () => {
    const original = font();
    const f = ui.decodeFnt(original);
    assert.deepEqual([...f.bgra.subarray(16 * 4, 16 * 4 + 4)], [255, 255, 255, 255]); // (alpha 0x80 -> 0xFF)
    const { out } = ui.upscaleUi(new Map([["fonts\\t.fnt.xbx", original]]), options(4), nearest4);
    const hd = out.get("fonts\\t.fnt.xbx")!;
    const at = f.head.length;
    assert.deepEqual(hd.subarray(0, at), original.subarray(0, at));
    assert.equal(hd.readUInt16LE(at + 4), 64);
    assert.equal(hd.readUInt16LE(at + 6), 64);
    assert.equal(hd.readUInt32LE(at + 8), ui.HD_FNT_MARKER);
    assert.equal(hd.readUInt32LE(at + 12), 4);
    assert.equal(hd.length, at + 0x10 + 64 * 64 * 4 + f.rects.length);
    assert.deepEqual(hd.subarray(hd.length - f.rects.length), f.rects);
    assert.throws(() => ui.decodeFnt(hd), /already/);
});

test("a model's texture dictionary: upscaled in place (no marker), the checksums kept; the menu model names", async () => {
    const { readTex, writeTex } = await import("../level_port/tex.ts");
    const bgra = Buffer.alloc(16 * 16 * 4);
    for (let i = 0; i < 256; i++) bgra.set([i, 255 - i, (i * 7) & 255, 255], i * 4);
    const texture = (checksum: number) => ({
        checksum,
        width: 16,
        height: 16,
        texelDepth: 32,
        paletteDepth: 0,
        dxt: 0,
        palette: Buffer.alloc(0),
        levels: [bgra],
    });
    const original = writeTex({
        version: 1,
        textures: [texture(0xaabbccdd), texture(0x11223344)],
    });
    const name = "models\\UI_bg\\UI_bg.tex.xbx";
    const { out, stats } = ui.upscaleUi(new Map([[name, original]]), options(4), nearest4);
    assert.equal(stats.models, 1);
    assert.equal(stats.texUpscaled, 2);
    const hd = readTex(out.get(name)!);
    assert.deepEqual(
        hd.textures.map((t) => [t.checksum, t.width, t.height]),
        [
            [0xaabbccdd, 64, 64],
            [0x11223344, 64, 64],
        ],
    );
    assert.ok(ui.MENU_MODELS.test("Models\\UI_bg\\UI_bg.tex.xbx"));
    assert.ok(ui.MENU_MODELS.test("levels\\mainmenu\\mainmenu.tex.xbx"));
    assert.ok(ui.MENU_MODELS.test("models/menu3d/AP_menu3d/AP_menu3d.tex.xbx"));
    assert.ok(!ui.MENU_MODELS.test("models\\Skater_Male\\hat\\hat.tex.xbx"));
});

// a 640 x 448 loading screen: the top half red, the bottom half blue
function loadingScreen(): Buffer {
    const w = 640;
    const h = 448;
    const head = Buffer.alloc(32);
    head.writeUInt32LE(2, 0);
    head.writeUInt32LE(8, 4);
    head.writeUInt32LE(w, 8);
    head.writeUInt32LE(h, 12);
    head.writeUInt16LE(w, 24);
    head.writeUInt16LE(h, 26);
    const texels = Buffer.alloc(w * h * 4);
    // (stored bottom-up: the first stored rows are the picture's bottom)
    for (let i = 0; i < w * h; i++) {
        const bottom = i < (w * h) / 2;
        texels.set(bottom ? [255, 0, 0, 255] : [0, 0, 255, 255], i * 4); // BGRA: blue, red
    }
    return Buffer.concat([head, texels]);
}

test("a loading screen: x4 (2560 x 1792, its own 4096 cap) as DXT1 blocks with the image marker", async () => {
    const dxt = await import("../level_port/dxt.ts");
    const name = "images\\loadscrn_x.img.xbx";
    const { out, stats } = ui.upscaleUi(new Map([[name, loadingScreen()]]), options(4), nearest4);
    assert.equal(stats.loadingScreens, 1);
    const data = out.get(name)!;
    assert.equal(data.readUInt32LE(8), 2560);
    assert.equal(data.readUInt32LE(12), 1792);
    assert.equal(data.readUInt32LE(16), ui.HD_IMG_DXT1);
    assert.equal(data.readUInt32LE(20), (ui.HD_IMG_MARKER | 4) >>> 0);
    assert.equal(data.readUInt16LE(24), 2560);
    assert.equal(data.length, 32 + (2560 * 1792) / 2);
    // the blocks as the loader copies them: the first rows are the picture's bottom (blue), the last its top (red)
    const texels = dxt.decode(data.subarray(32), 2560, 1792, 1);
    assert.deepEqual([...texels.subarray(0, 3)], [255, 0, 0]);
    const last = (2560 * 1791 + 100) * 4;
    assert.deepEqual([...texels.subarray(last, last + 3)], [0, 0, 255]);
});

test("a loading screen with the DXT off: 32-bit, as the other images", () => {
    const name = "images\\loadscrn_x.img.xbx";
    const { out } = ui.upscaleUi(
        new Map([[name, loadingScreen()]]),
        options(4),
        nearest4,
        ui.loadingOptions(options(4)),
        false,
    );
    const data = out.get(name)!;
    assert.equal(data.readUInt32LE(16), 0);
    assert.equal(data.length, 32 + 2560 * 1792 * 4);
});

test("loading screens use the x4plus model, the rest of the UI the anime one", () => {
    assert.equal(ui.loadingOptions(options(4)).model, "realesrgan-x4plus");
    assert.equal(options(4).model, "realesrgan-x4plus-anime");
});
