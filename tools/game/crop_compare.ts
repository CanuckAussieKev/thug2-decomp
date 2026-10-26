// A zoomed before / after picture: the same rectangle of two screenshots side by side (nearest-neighbour, so every
// screen pixel stays visible) - what the HD UI checks look at.
//   node tools/pipeline/game/crop_compare.ts <before.png> <after.png> <x,y,w,h> <out.png> [--zoom 3]
import * as fs from "node:fs";
import { parseCli } from "../lib/cli.ts";
import { isMain, print, runMain } from "../lib/py.ts";
import { decodePng, encodePng } from "./frame_diff.ts";

const USAGE = "usage: crop_compare.ts before.png after.png x,y,w,h out.png [--zoom N]";

function main(): number {
    const { values, positionals } = parseCli(USAGE, {
        options: { zoom: { type: "string", default: "3" } },
        allowPositionals: true,
    });
    if (positionals.length !== 4) {
        print(USAGE);
        return 2;
    }
    const [before, after, rect, out] = positionals as [string, string, string, string];
    const [x, y, w, h] = rect.split(",").map(Number) as [number, number, number, number];
    const zoom = Number(values.zoom);
    const images = [before, after].map((file) => decodePng(fs.readFileSync(file)));
    const gap = 4;
    const width = w * 2 * zoom + gap;
    const height = h * zoom;
    const rgb = new Uint8Array(width * height * 3).fill(255);
    images.forEach((img, side) => {
        for (let j = 0; j < height; j++)
            for (let i = 0; i < w * zoom; i++) {
                const s = ((y + Math.floor(j / zoom)) * img.width + x + Math.floor(i / zoom)) * 3;
                const d = (j * width + side * (w * zoom + gap) + i) * 3;
                rgb.set(img.rgb.subarray(s, s + 3), d);
            }
    });
    fs.writeFileSync(out, encodePng({ width, height, rgb }));
    print(`wrote ${out} (${width}x${height})`);
    return 0;
}

if (isMain(import.meta.url)) await runMain(() => main());
