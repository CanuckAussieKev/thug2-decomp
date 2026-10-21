// NOT ORIGINAL - what makes a level's surfaces shimmer, per level folder: its textures' mip chains (how many stop
// before a shorter side of 4, as THUG2's own do) and its layers (how much surface of one material lies on another's
// with the same z bias - they z-fight - and how much with a different one). For comparing converted levels with
// THUG2's own (docs/pending/ported-shimmer.md).
//
//     node tools/level_port/layer_check.ts <levels folder> [<level> ...]
//
// e.g. node tools/level_port/layer_check.ts C:\Games\THUG2\Game\Data\levels LA AU
//      node tools/level_port/layer_check.ts C:\Games\THUG2\Game\thug2-decomp-data\levels foun alc

import * as fs from "node:fs";
import * as path from "node:path";
import { overlaps } from "./overlay_bias.ts";
import * as tex from "./tex.ts";
import * as thug2Scene from "./thug2_scene.ts";

function check(folder: string, level: string): string {
    const base = path.join(folder, level, level);
    const parts = [level.padEnd(20)];
    if (fs.existsSync(`${base}.tex.xbx`)) {
        const textures = tex.readTex(fs.readFileSync(`${base}.tex.xbx`), level).textures;
        // (a chain "stops short" when its last level's shorter side is still over 4 - single-level ones counted apart)
        let single = 0;
        let short = 0;
        for (const t of textures) {
            const n = t.levels.length;
            if (n === 1) single += Number(Math.min(t.width, t.height) > 4);
            else if (Math.min(t.width, t.height) >> (n - 1) > 4) short++;
        }
        parts.push(`textures ${textures.length}: ${short} mip chains stop short, ${single} without mips`);
    }
    if (fs.existsSync(`${base}.scn.xbx`)) {
        const scene = thug2Scene.readScene(fs.readFileSync(`${base}.scn.xbx`), level);
        const { overlap } = overlaps(scene);
        let same = 0;
        let biased = 0;
        for (const [a, row] of overlap) {
            for (const [b, area] of row) {
                if (scene.materials[a]!.zBias === scene.materials[b]!.zBias) same += area;
                else biased += area;
            }
        }
        const share = same + biased ? Math.round((100 * biased) / (same + biased)) : 0;
        parts.push(`layers: ${Math.round(biased + same)} sq units on other materials, ${share}% with a z bias between`);
    }
    return parts.join(" | ");
}

const [folder, ...levels] = process.argv.slice(2);
if (!folder) {
    console.log("usage: node tools/level_port/layer_check.ts <levels folder> [<level> ...]");
    process.exit(2);
}
const names = levels.length
    ? levels
    : fs.readdirSync(folder).filter((d) => fs.existsSync(path.join(folder, d, `${d}.scn.xbx`)));
for (const level of names) console.log(check(folder, level));
