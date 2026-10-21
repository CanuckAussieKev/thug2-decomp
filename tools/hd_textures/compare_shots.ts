// NOT ORIGINAL - side-by-side comparison pictures of game screenshots: each row the same view in several runs
// (e.g. a level converted without and with --hd-textures), left to right in the order given, 8 pixels of grey between.
//
//     node tools/hd_textures/compare_shots.ts <out.png> <a.png> <b.png> [<c.png> ...] [--crop x,y,w,h] [--zoom N]
//
// --crop cuts the same rectangle out of every picture first (for a close look at a surface), --zoom N then enlarges
// it N times (nearest pixel) so texel detail stays visible. The pictures must be the same size.

import * as fs from "node:fs";
import { decodePng, encodePng, type Image } from "../game/frame_diff.ts";
import { isMain, print, pyExit, runMain } from "../lib/py.ts";

const GAP = 8;

export function crop(img: Image, x0: number, y0: number, w: number, h: number): Image {
    if (x0 < 0 || y0 < 0 || x0 + w > img.width || y0 + h > img.height) throw new Error("crop outside the picture");
    const rgb = new Uint8Array(w * h * 3);
    for (let y = 0; y < h; y++)
        rgb.set(img.rgb.subarray(((y0 + y) * img.width + x0) * 3, ((y0 + y) * img.width + x0 + w) * 3), y * w * 3);
    return { width: w, height: h, rgb };
}

export function zoom(img: Image, n: number): Image {
    const w = img.width * n;
    const h = img.height * n;
    const rgb = new Uint8Array(w * h * 3);
    for (let y = 0; y < h; y++)
        for (let x = 0; x < w; x++) {
            const s = (Math.floor(y / n) * img.width + Math.floor(x / n)) * 3;
            rgb.set(img.rgb.subarray(s, s + 3), (y * w + x) * 3);
        }
    return { width: w, height: h, rgb };
}

// The pictures side by side (same height), GAP grey pixels between them.
export function sideBySide(images: readonly Image[]): Image {
    const h = images[0]!.height;
    if (images.some((i) => i.height !== h)) throw new Error("pictures of different heights");
    const w = images.reduce((n, i) => n + i.width, 0) + GAP * (images.length - 1);
    const rgb = new Uint8Array(w * h * 3).fill(96);
    let x0 = 0;
    for (const img of images) {
        for (let y = 0; y < h; y++)
            rgb.set(img.rgb.subarray(y * img.width * 3, (y + 1) * img.width * 3), (y * w + x0) * 3);
        x0 += img.width + GAP;
    }
    return { width: w, height: h, rgb };
}

export async function main(argv: readonly string[] = process.argv.slice(2)): Promise<number> {
    const files: string[] = [];
    let box: number[] | null = null;
    let factor = 1;
    for (let i = 0; i < argv.length; i++) {
        const a = argv[i]!;
        if (a === "--crop") {
            box = (argv[++i] ?? "").split(",").map(Number);
            if (box.length !== 4 || box.some((n) => !Number.isInteger(n) || n < 0)) pyExit("--crop x,y,w,h");
        } else if (a === "--zoom") {
            factor = Number(argv[++i]);
            if (!Number.isInteger(factor) || factor < 1) pyExit("--zoom N (a whole number)");
        } else files.push(a);
    }
    if (files.length < 3) pyExit("usage: compare_shots.ts <out.png> <a.png> <b.png> [...] [--crop x,y,w,h] [--zoom N]");
    const [out, ...inputs] = files as [string, ...string[]];
    const images = inputs.map((f) => {
        let img = decodePng(fs.readFileSync(f));
        if (box) img = crop(img, box[0]!, box[1]!, box[2]!, box[3]!);
        return factor > 1 ? zoom(img, factor) : img;
    });
    const result = sideBySide(images);
    fs.writeFileSync(out, encodePng(result));
    print(`wrote ${out} (${result.width}x${result.height})`);
    return 0;
}

if (isMain(import.meta.url)) await runMain(() => main());
