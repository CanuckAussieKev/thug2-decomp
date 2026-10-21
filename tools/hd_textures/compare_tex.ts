// NOT ORIGINAL - side-by-side pictures of a level's textures before and after the --hd-textures pass
// (tools/level_port/hd_textures.ts): each texture's original (scaled up to the new size, nearest texel) left, the
// upscaled one right, over a checkerboard where they're transparent.
//
//     node tools/hd_textures/compare_tex.ts <original.tex.xbx> <upscaled.tex.xbx> <out folder> [--count N] [--min N]
//
// Writes <out>\<checksum>.png for the N largest textures (default 24) whose longer side is at least --min (default
// 64) in the original, and <out>\sheet.png - all of them, each pair 256 pixels high, one under the other.

import * as fs from "node:fs";
import * as path from "node:path";
import { encodePng, type Image } from "../game/frame_diff.ts";
import { topLevel } from "../level_port/hd_textures.ts";
import * as tex from "../level_port/tex.ts";
import { hex8, isMain, print, pyExit, runMain } from "../lib/py.ts";

// BGRA over a checkerboard, as RGB, resampled (nearest) to w x h
function flatten(bgra: Uint8Array, sw: number, sh: number, w: number, h: number): Uint8Array {
    const rgb = new Uint8Array(w * h * 3);
    for (let y = 0; y < h; y++) {
        const sy = Math.min(sh - 1, Math.floor((y * sh) / h));
        for (let x = 0; x < w; x++) {
            const s = (sy * sw + Math.min(sw - 1, Math.floor((x * sw) / w))) * 4;
            const a = bgra[s + 3]! / 255;
            const back = ((x >> 3) + (y >> 3)) & 1 ? 200 : 140;
            const o = (y * w + x) * 3;
            rgb[o] = Math.round(bgra[s + 2]! * a + back * (1 - a));
            rgb[o + 1] = Math.round(bgra[s + 1]! * a + back * (1 - a));
            rgb[o + 2] = Math.round(bgra[s]! * a + back * (1 - a));
        }
    }
    return rgb;
}

export function pair(a: tex.Texture, b: tex.Texture, height: number | null = null): Image {
    const h = height ?? b.height;
    const w = Math.max(1, Math.round((b.width * h) / b.height));
    const gap = 8;
    const out = new Uint8Array((2 * w + gap) * h * 3).fill(255);
    const left = flatten(topLevel(a), a.width, a.height, w, h);
    const right = flatten(topLevel(b), b.width, b.height, w, h);
    const stride = (2 * w + gap) * 3;
    for (let y = 0; y < h; y++) {
        out.set(left.subarray(y * w * 3, (y + 1) * w * 3), y * stride);
        out.set(right.subarray(y * w * 3, (y + 1) * w * 3), y * stride + (w + gap) * 3);
    }
    return { width: 2 * w + gap, height: h, rgb: out };
}

function stack(images: readonly Image[]): Image {
    const width = Math.max(...images.map((i) => i.width));
    const gap = 8;
    const height = images.reduce((n, i) => n + i.height + gap, 0);
    const rgb = new Uint8Array(width * height * 3).fill(255);
    let y = 0;
    for (const img of images) {
        for (let r = 0; r < img.height; r++)
            rgb.set(img.rgb.subarray(r * img.width * 3, (r + 1) * img.width * 3), (y + r) * width * 3);
        y += img.height + gap;
    }
    return { width, height, rgb };
}

export function main(argv: readonly string[] = process.argv.slice(2)): number {
    const positional: string[] = [];
    let count = 24;
    let min = 64;
    for (let i = 0; i < argv.length; i++) {
        const arg = argv[i]!;
        if (arg === "--count") count = Number(argv[++i]);
        else if (arg === "--min") min = Number(argv[++i]);
        else positional.push(arg);
    }
    if (positional.length !== 3 || !(count > 0) || !(min > 0))
        pyExit("usage: compare_tex.ts <original.tex.xbx> <upscaled.tex.xbx> <out folder> [--count N] [--min N]");
    const [origFile, hdFile, outDir] = positional as [string, string, string];
    const orig = tex.readTex(fs.readFileSync(origFile), origFile);
    const hd = new Map(tex.readTex(fs.readFileSync(hdFile), hdFile).textures.map((t) => [t.checksum, t]));
    const chosen = orig.textures
        .filter((t) => Math.max(t.width, t.height) >= min && hd.has(t.checksum))
        .sort((p, q) => q.width * q.height - p.width * p.height || p.checksum - q.checksum)
        .slice(0, count);
    fs.mkdirSync(outDir, { recursive: true });
    const rows: Image[] = [];
    for (const t of chosen) {
        const u = hd.get(t.checksum)!;
        fs.writeFileSync(path.join(outDir, `${hex8(t.checksum)}.png`), encodePng(pair(t, u)));
        rows.push(pair(t, u, 256));
    }
    if (rows.length) fs.writeFileSync(path.join(outDir, "sheet.png"), encodePng(stack(rows)));
    print(`${chosen.length} textures compared in ${outDir}`);
    return 0;
}

if (isMain(import.meta.url)) await runMain(() => main());
