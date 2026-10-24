// NOT ORIGINAL - HD menu UI (an experiment the user asked for, 2026-10-24): the menus' 2D images (.img.xbx sprites)
// and bitmap fonts (.fnt.xbx glyph pages) upscaled with the HD texture pass (tools/level_port/hd_textures.ts:
// Real-ESRGAN, the cache, back-projection to the original's colours) into the data overlay. Off by default:
// tools/menu_level/make_menu_level.ts --hd-ui [2|4] adds them, a run without it leaves the overlay without them.
//
// The images keep their on-screen size only with the DLL built with the CMake option THUG2_HD_UI (default OFF -
// src/game/gfx/hd_ui.h): a sprite's size comes from its texture's image size, a glyph's from the font's rectangles,
// so the upscaled files carry a marker the DLL reads and divides by:
//   .img.xbx  header +0x14 (0 in every game file) = HD_IMG_MARKER | scale; the texels are 32-bit (format 0, the
//             8-bit paletted ones expanded - the game makes every image A8R8G8B8 anyway), every size in the header
//             `scale` times the original's. The DLL divides the texture's sizes again after loading.
//   .fnt.xbx  the glyph page's header +8 = HD_FNT_MARKER, +0xC the scale; the page `scale` times the original's,
//             32-bit BGRA texels (no palette: the final alpha, the game's doubled PS2 alpha applied); the glyph
//             rectangles, height and base unchanged (the original's pixels): the DLL makes the texture coordinates
//             from the page's original size.
// Without THUG2_HD_UI the DLL ignores the markers, so the upscaled files show `scale` times as large - the two
// toggles go together.
//
// Which files: every image and font in the archives the front end and the pause menus load (UI_PACKS), and the
// overlay's own images (the level select's added signs - written first by make_menu_level, upscaled here). Not
// included: the loading screens (full-screen 512 x 512 / 640 x 480 pictures, loose in Data\images; up to 80 MB of
// texture memory each at x4), Create-A-Graphic's pieces (edited texel by texel), PermTex / Bits (level and skater
// textures), levels' .tex.xbx files.
//
//   node tools/hd_textures/ui.ts <game folder> <out folder> [--scale 4] [--model M] [--max N]
// writes the upscaled files under <out folder> (what make_menu_level --hd-ui does into the overlay).

import * as fs from "node:fs";
import * as path from "node:path";
import * as hd from "../level_port/hd_textures.ts";
import { entries } from "../level_port/prx.ts";
import type { TexFile, Texture } from "../level_port/tex.ts";
import { unswizzleOrder } from "../level_port/tex.ts";
import { parseCli } from "../lib/cli.ts";
import { resolveInstall } from "../lib/install.ts";
import { isMain, print, runMain } from "../lib/py.ts";

export const HD_IMG_MARKER = 0x48440000; // "HD" in the high half; the low byte the scale (src/game/gfx/hd_ui.h)
export const HD_FNT_MARKER = 0x49554448; // "HDUI"
// Archives the front end and the pause menus load their images and fonts from.
export const UI_PACKS = [
    "mainmenusprites.prx",
    "panelsprites.prx",
    "Levelselect.prx",
    "multiplayersprites.prx",
    "Themes.prx",
    "fonts.prx",
] as const;

// The defaults for UI: the anime model (flat colours, outlines, text), the longer side up to 2048.
export function uiOptions(scale = 4): hd.HdOptions {
    return { ...hd.defaultOptions(scale), model: "realesrgan-x4plus-anime", maxSize: 2048, minSize: 8 };
}

const isPow2 = (n: number): boolean => n > 0 && (n & (n - 1)) === 0;

// An image's texels top-down (the stored rows are bottom-up: the sprite's top samples v = 1), the picture at the top
// left: tw x th BGRA.
export interface UiImage {
    tw: number;
    th: number;
    iw: number;
    ih: number;
    bgra: Buffer;
}

function storedOrder(w: number, h: number): number[] {
    // swizzled when both sides are powers of two (the loader's "plain" test), rows as stored otherwise
    if (isPow2(w) && isPow2(h)) return unswizzleOrder(w, h);
    return Array.from({ length: w * h }, (_, i) => i);
}

export function decodeImg(data: Buffer): UiImage {
    const tw = data.readUInt32LE(8);
    const th = data.readUInt32LE(12);
    const format = data.readUInt32LE(16);
    const iw = data.readUInt16LE(24);
    const ih = data.readUInt16LE(26);
    const paletteSize = data.readUInt32LE(28);
    const bgra = Buffer.alloc(tw * th * 4);
    const order = storedOrder(tw, th);
    if (format === 0) {
        const texels = data.subarray(32);
        for (let y = 0; y < th; y++)
            for (let x = 0; x < tw; x++) {
                const s = order[(th - 1 - y) * tw + x]! * 4;
                texels.copy(bgra, (y * tw + x) * 4, s, s + 4);
            }
    } else if (format === 0x13 || format === 8) {
        const palette = data.subarray(32, 32 + paletteSize);
        const indices = data.subarray(32 + paletteSize);
        for (let y = 0; y < th; y++)
            for (let x = 0; x < tw; x++) {
                const p = indices[order[(th - 1 - y) * tw + x]!]! * 4;
                palette.copy(bgra, (y * tw + x) * 4, p, p + 4);
            }
    } else throw new Error(`img format 0x${format.toString(16)} not handled`);
    return { tw, th, iw, ih, bgra };
}

// The picture alone (iw x ih).
export function picture(img: UiImage): Buffer {
    const out = Buffer.alloc(img.iw * img.ih * 4);
    for (let y = 0; y < img.ih; y++) img.bgra.copy(out, y * img.iw * 4, y * img.tw * 4, (y * img.tw + img.iw) * 4);
    return out;
}

// An HD image file: the upscaled picture (iw * scale x ih * scale BGRA) at the top left of a texture `scale` times
// the original's, the rest its edge texels repeated (what bilinear filtering at the picture's edge sees).
export function encodeImg(original: UiImage, hdPicture: Buffer, scale: number): Buffer {
    const tw = original.tw * scale;
    const th = original.th * scale;
    const iw = original.iw * scale;
    const ih = original.ih * scale;
    if (hdPicture.length !== iw * ih * 4) throw new Error("upscaled picture of the wrong size");
    const head = Buffer.alloc(32);
    head.writeUInt32LE(2, 0);
    head.writeUInt32LE(8, 4);
    head.writeUInt32LE(tw, 8);
    head.writeUInt32LE(th, 12);
    head.writeUInt32LE(0, 16);
    head.writeUInt32LE((HD_IMG_MARKER | scale) >>> 0, 20);
    head.writeUInt16LE(iw, 24);
    head.writeUInt16LE(ih, 26);
    head.writeUInt32LE(0, 28);
    const texels = Buffer.alloc(tw * th * 4);
    const order = storedOrder(tw, th);
    for (let y = 0; y < th; y++) {
        const sy = Math.min(y, ih - 1);
        for (let x = 0; x < tw; x++) {
            const s = (sy * iw + Math.min(x, iw - 1)) * 4;
            hdPicture.copy(texels, order[(th - 1 - y) * tw + x]! * 4, s, s + 4);
        }
    }
    return Buffer.concat([head, texels]);
}

// A font: everything before the glyph page's header, the page (w x h BGRA, rows as stored = as drawn), and the glyph
// rectangles after it.
export interface UiFont {
    head: Buffer;
    pageHeader: Buffer;
    w: number;
    h: number;
    bgra: Buffer;
    rects: Buffer;
}

export function decodeFnt(data: Buffer): UiFont {
    const count = data.readUInt32LE(8);
    const at = 0x14 + count * 6;
    const pageHeader = data.subarray(at, at + 0x10);
    const w = pageHeader.readUInt16LE(4);
    const h = pageHeader.readUInt16LE(6);
    if (pageHeader.readUInt32LE(8) === HD_FNT_MARKER) throw new Error("already an HD font");
    const size = (w * h + 3) & ~3;
    const indices = data.subarray(at + 0x10, at + 0x10 + size);
    const palette = data.subarray(at + 0x10 + size, at + 0x10 + size + 0x400);
    const rects = data.subarray(at + 0x10 + size + 0x400);
    if (rects.length !== 4 + count * 8) throw new Error(`font layout: ${rects.length} bytes of rectangles`);
    // the palette's RGBA (PS2 alpha) as the game makes it: BGRA, alpha doubled (0x80 and over opaque)
    const colours = Buffer.alloc(0x400);
    for (let i = 0; i < 256; i++) {
        const a = palette[i * 4 + 3]!;
        colours[i * 4] = palette[i * 4 + 2]!;
        colours[i * 4 + 1] = palette[i * 4 + 1]!;
        colours[i * 4 + 2] = palette[i * 4]!;
        colours[i * 4 + 3] = a >= 0x80 ? 0xff : a << 1;
    }
    const bgra = Buffer.alloc(w * h * 4);
    for (let i = 0; i < w * h; i++) colours.copy(bgra, i * 4, indices[i]! * 4, indices[i]! * 4 + 4);
    return { head: data.subarray(0, at), pageHeader, w, h, bgra, rects };
}

export function encodeFnt(font: UiFont, hdPage: Buffer, scale: number): Buffer {
    const w = font.w * scale;
    const h = font.h * scale;
    if (hdPage.length !== w * h * 4) throw new Error("upscaled page of the wrong size");
    const pageHeader = Buffer.from(font.pageHeader);
    pageHeader.writeUInt32LE(0x10 + hdPage.length + font.rects.length, 0); // (the page's own size, as the game's)
    pageHeader.writeUInt16LE(w, 4);
    pageHeader.writeUInt16LE(h, 6);
    pageHeader.writeUInt32LE(HD_FNT_MARKER, 8);
    pageHeader.writeUInt32LE(scale, 12);
    return Buffer.concat([font.head, pageHeader, hdPage, font.rects]);
}

export interface UiStats {
    images: number;
    fonts: number;
    upscaled: number;
    kept: number; // too small / one colour: the original file, unmarked
    bytesBefore: number;
    bytesAfter: number;
    hd: hd.HdStats;
}

// The upscaled files for `files` (game file name -> data): only the ones upscaled are returned.
export function upscaleUi(
    files: ReadonlyMap<string, Buffer>,
    options: hd.HdOptions,
    upscaler: hd.Upscaler = hd.realEsrgan,
): { out: Map<string, Buffer>; stats: UiStats } {
    type Item = { name: string; texture: Texture } & ({ img: UiImage } | { font: UiFont });
    const items: Item[] = [];
    const stats: UiStats = {
        images: 0,
        fonts: 0,
        upscaled: 0,
        kept: 0,
        bytesBefore: 0,
        bytesAfter: 0,
        hd: hd.newStats(),
    };
    const texture = (n: number, w: number, h: number, bgra: Buffer): Texture => ({
        checksum: n,
        width: w,
        height: h,
        texelDepth: 32,
        paletteDepth: 0,
        dxt: 0,
        palette: Buffer.alloc(0),
        levels: [bgra],
    });
    for (const [name, data] of files) {
        const lower = name.toLowerCase();
        if (lower.endsWith(".img.xbx")) {
            stats.images += 1;
            const img = decodeImg(data);
            if (data.readUInt32LE(20) !== 0) throw new Error(`${name}: already marked`);
            items.push({ name, img, texture: texture(items.length, img.iw, img.ih, picture(img)) });
        } else if (lower.endsWith(".fnt.xbx")) {
            stats.fonts += 1;
            const font = decodeFnt(data);
            items.push({ name, font, texture: texture(items.length, font.w, font.h, font.bgra) });
        } else continue;
        stats.bytesBefore += data.length;
    }
    // (UI pieces are drawn clamped: no wrapping when padding)
    const addressing = new Map(items.map((item) => [item.texture.checksum, { wrapU: false, wrapV: false }]));
    const dictionary: TexFile = { version: 1, textures: items.map((item) => item.texture) };
    const before = new Map(items.map((item) => [item, item.texture.width]));
    hd.upscaleTexFile(dictionary, addressing, options, stats.hd, upscaler);
    const out = new Map<string, Buffer>();
    for (const item of items) {
        const scale = item.texture.width / before.get(item)!;
        if (scale <= 1) {
            stats.kept += 1;
            continue;
        }
        const data =
            "img" in item
                ? encodeImg(item.img, item.texture.levels[0]!, scale)
                : encodeFnt(item.font, item.texture.levels[0]!, scale);
        out.set(item.name, data);
        stats.upscaled += 1;
        stats.bytesAfter += data.length;
    }
    return { out, stats };
}

// The UI files of a game folder: its UI archives' images and fonts, by the names the game asks for them.
export function gameUiFiles(game: string): Map<string, Buffer> {
    const files = new Map<string, Buffer>();
    for (const pack of UI_PACKS) {
        const file = path.join(game, "Data", "pre", pack);
        if (!fs.existsSync(file)) continue;
        for (const [name, data] of entries(file, (n) => /\.(img|fnt)\.xbx$/i.test(n))) files.set(name, data);
    }
    return files;
}

// The overlay's own images (relative names), which win over the game's.
function overlayImages(overlay: string): Map<string, Buffer> {
    const files = new Map<string, Buffer>();
    const images = path.join(overlay, "images");
    if (!fs.existsSync(images)) return files;
    for (const entry of fs.readdirSync(images, { recursive: true, withFileTypes: true })) {
        if (!entry.isFile() || !/\.img\.xbx$/i.test(entry.name)) continue;
        const full = path.join(entry.parentPath, entry.name);
        files.set(path.relative(overlay, full), fs.readFileSync(full));
    }
    return files;
}

// Upscales the game's UI files and the overlay's own images into `overlay` (the overlay's own replaced in place).
// Returns the report's lines.
export function install(game: string, overlay: string, options: hd.HdOptions): string[] {
    const files = gameUiFiles(game);
    const own = overlayImages(overlay);
    // (one key per file whatever its case: the overlay's version replaces the game's)
    const byKey = new Map<string, [string, Buffer]>();
    for (const [name, data] of files) byKey.set(name.toLowerCase(), [name, data]);
    for (const [name, data] of own) byKey.set(name.toLowerCase(), [name, data]);
    const { out, stats } = upscaleUi(new Map([...byKey.values()]), options);
    for (const [name, data] of out) {
        const target = path.join(overlay, ...name.split(/[\\/]/));
        fs.mkdirSync(path.dirname(target), { recursive: true });
        fs.writeFileSync(target, data);
    }
    const mb = (n: number): string => (n / 1048576).toFixed(1);
    return [
        `HD UI (${options.model}, x${options.scale}): ${stats.upscaled} of ${stats.images} images + ${stats.fonts} fonts ` +
            `upscaled (${stats.kept} left as they were: too small or one colour; ${own.size} of them the overlay's own)`,
        `  ${mb(stats.bytesBefore)} MB -> ${mb(stats.bytesAfter)} MB of files (texture memory: each image ${options.scale}x${options.scale} ` +
            `times; 32-bit as before); ${stats.hd.cacheHits} from the cache, ${stats.hd.generated} made, ` +
            `${stats.hd.seconds.toFixed(1)} s (upscaler ${stats.hd.toolSeconds.toFixed(1)} s)`,
        "  needs the DLL built with -DTHUG2_HD_UI=ON, or the upscaled images and text show larger",
    ];
}

const USAGE = "usage: ui.ts game out [--scale 2|4] [--model M] [--max N]";

function main(): number {
    const { values, positionals } = parseCli(USAGE, {
        options: {
            scale: { type: "string", default: "4" },
            model: { type: "string" },
            max: { type: "string" },
        },
        allowPositionals: true,
    });
    if (positionals.length !== 2) {
        print(USAGE);
        return 2;
    }
    const scale = Number(values.scale);
    if (scale !== 2 && scale !== 4) throw new Error("--scale is 2 or 4");
    const options = uiOptions(scale);
    if (values.model) options.model = values.model;
    if (values.max) options.maxSize = Number(values.max);
    for (const line of install(resolveInstall(positionals[0]!, "thug2"), positionals[1]!, options)) print(line);
    return 0;
}

if (isMain(import.meta.url)) await runMain(() => main());
