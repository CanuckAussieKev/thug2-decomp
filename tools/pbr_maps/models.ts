// NOT ORIGINAL - surface maps for the characters' models ([Graphics] rt_characters; docs/design/vulkan.md "Characters"):
// the skaters', the CAS parts', the peds', the cutscenes' and the skating animals' texture dictionaries
// (models\<folder>\<name>.tex.xbx). Their textures are classified as skin, cloth, denim, leather, rubber, hair (or a
// world class - a board's wood, a watch's metal) from the dictionary's name - the CAS parts are one dictionary each:
// Hair_M_Long2, pant_leather, Shirt_Longsleeve, Hands, Face_Male_01 - and each texture's colour statistics (the
// skin's hue and saturation, the jeans' blue); the whole-body ones (Skater_Hawk, the peds, the cutscenes) by the
// statistics alone. Every entry is flagged FLAG_MODEL: the game uses it only with rt_characters on. A hair texture
// records which way its strands run (FLAG_STRANDS_U: its luma varies more along v than along u) for the anisotropic
// highlight.
import { existsSync, readdirSync, readFileSync, statSync } from "node:fs";
import { join, relative } from "node:path";
import * as prx from "thug2-level-converter-core/level_port/prx.ts";
import { type ClassName, classify, type Stats, type Usage, type Verdict } from "./classes.ts";
import type { Rgba } from "./classical.ts";

// A model dictionary as the game reads it (pbr_maps.ts Dictionary's shape): its name under Data (lower case, / between
// parts), its bytes, its model's (<x>.skin.xbx beside it - the materials, read as a scene: which textures are base
// passes, overlays, environment maps), and the folder its maps go to.
export interface ModelDictionary {
    name: string;
    tex: () => Buffer;
    scn: () => Buffer | null;
    col: () => Buffer | null;
    folder: string;
}

// The characters' folders under models\ (lower case): the skaters' (Skater_Male, Skater_female, Skater_Pro,
// Skater_Secret), the peds' (Ped_Male, Ped_Female, Peds), the cutscenes' people and the animals (the skating goat,
// the dogs). Everything else under models\ (arrows, cones, menu backdrops, vehicles) is left alone.
const CHARACTER_PATH = /^models\/(skater_[^/]+|ped_[^/]+|peds|cutscenes|animals)\//;
const SHADOW = /shadow/;

export function isCharacterDictionary(name: string): boolean {
    const lower = name.toLowerCase();
    return CHARACTER_PATH.test(lower) && lower.endsWith(".tex.xbx") && !SHADOW.test(lower);
}

const walk = (path: string): string[] =>
    !existsSync(path)
        ? []
        : statSync(path).isDirectory()
          ? readdirSync(path).flatMap((name) => walk(join(path, name)))
          : [path];

// The character model dictionaries: loose under Data\models, in Data\pre's archives, in the data overlay - the overlay's
// copy first (as the game reads them), the maps beside the copy read (an archive's: in Data).
export function modelDictionaries(root: string, overlays: readonly string[]): ModelDictionary[] {
    const found = new Map<string, ModelDictionary>();
    const skinOf = (name: string): string => name.replace(/\.tex\.xbx$/, ".skin.xbx");
    const add = (base: string, folder: string) => {
        const files = new Map<string, string>();
        for (const path of walk(join(base, "models"))) {
            files.set(relative(base, path).replaceAll("\\", "/").toLowerCase(), path);
        }
        for (const [name, path] of files) {
            if (!isCharacterDictionary(name)) continue;
            const skin = files.get(skinOf(name));
            found.set(name, {
                name,
                tex: () => readFileSync(path),
                scn: () => (skin ? readFileSync(skin) : null),
                col: () => null,
                folder,
            });
        }
    };
    add(join(root, "Data"), "Data");
    const pre = join(root, "Data", "pre");
    const archives = existsSync(pre) ? readdirSync(pre).filter((f) => f.toLowerCase().endsWith(".prx")) : [];
    for (const archive of archives) {
        const inside = new Map<string, Buffer>();
        try {
            for (const [entry, data] of prx.entries(join(pre, archive), (n) => /\.(tex|skin)\.xbx$/i.test(n))) {
                inside.set(entry.replaceAll("\\", "/").toLowerCase(), data);
            }
        } catch {
            continue;
        }
        for (const [name, data] of inside) {
            if (!isCharacterDictionary(name)) continue;
            const skin = inside.get(skinOf(name)) ?? null;
            found.set(name, { name, tex: () => data, scn: () => skin, col: () => null, folder: "Data" });
        }
    }
    for (const overlay of overlays) add(join(root, overlay), overlay);
    return [...found.values()].sort((a, b) => a.name.localeCompare(b.name));
}

// What a dictionary's name says its textures are (the CAS parts' names): its file name's words.
export type Hint =
    | "hair"
    | "skin"
    | "legwear"
    | "denim"
    | "leather"
    | "rubber"
    | "metal"
    | "wood"
    | "cloth"
    | "fur"
    | "none";
const HINTS: [Hint, RegExp][] = [
    ["metal", /(watch|ring\d|ring_|chain|necklace|earring|piercing|glasses|shades|bracelet)/],
    ["wood", /(board|deck)/],
    ["hair", /(hair|bangs|mohawk|afro|ponytail|pigtail|mullet|dread|beard|goatee|moustache|mustache|sideburn|eyebrow)/],
    ["leather", /leather/],
    ["denim", /(denim|jean)/],
    ["rubber", /(shoe|sneaker|boot|sole|kneepad|elbowpad|wristband)/],
    ["legwear", /(pant|short|cargo|skirt|bellbottom|legging)/],
    ["fur", /^anl_/],
    [
        "cloth",
        /(shirt|hood|jacket|sweater|jersey|vest|top|tee|coat|sock|glove|beanie|hat|cap|scarf|bra|tube|halter|tank|suit|jogger|collar)/,
    ],
    ["skin", /(hand|head|face|leg|arm|feet|foot)/],
];

export function hintOf(name: string): Hint {
    const base = (name.split("/").pop() ?? "").replace(/\.tex\.xbx$/, "").toLowerCase();
    if (/skeleton/.test(base)) return "none"; // (the skeleton arms and hands: bone, not skin)
    for (const [hint, pattern] of HINTS) if (pattern.test(base)) return hint;
    return "none";
}

// Whether a texel's colour (0 - 1 sRGB) is skin's: red over green over blue (any tone - the game's skins run from pale
// to dark), the hue (g - b) / (r - b) 0.1 - 0.9 (6 - 54 degrees), the saturation (max - min) / max 0.11 - 0.75, not
// near black. The shader's per-texel weight (ff_shaders.cpp "the characters") is this test with soft edges.
export function skinTexel(r: number, g: number, b: number): boolean {
    if (!(r >= g && g >= b) || r < 0.15) return false;
    const saturation = (r - b) / r;
    const hue = r > b ? (g - b) / (r - b) : 0;
    return saturation >= 0.11 && saturation <= 0.75 && hue >= 0.1 && hue <= 0.9;
}

// The share of a texture's visible texels (alpha past a quarter of its opaque value) that are skin-coloured.
export function skinShare(image: Rgba): number {
    let alphaMax = 0;
    for (let i = 3; i < image.data.length; i += 4) alphaMax = Math.max(alphaMax, image.data[i]!);
    const visible = (alphaMax >= 200 ? 255 : 128) / 4;
    let skin = 0;
    let seen = 0;
    for (let i = 0; i < image.data.length; i += 4) {
        if (image.data[i + 3]! < visible) continue;
        seen++;
        if (skinTexel(image.data[i]! / 255, image.data[i + 1]! / 255, image.data[i + 2]! / 255)) skin++;
    }
    return seen ? skin / seen : 0;
}

// The jeans' blue.
export function denimLike(stats: Stats): boolean {
    return stats.hue >= 195 && stats.hue <= 240 && stats.saturation >= 0.15 && stats.saturation <= 0.65;
}

export const SKIN_WHOLE_BODY = 0.5; // ... in a whole body's dictionary
export const SKIN_NAMED = 0.3; // ... in a dictionary named for skin (hands, a head: eyes, brows and lips are in it)
export const SKIN_TEXELS = 0.15; // a garment's share from which its skin-coloured texels are shaded as skin (an atlas)
export const SKIN_GARMENT = 0.85; // ... and up to which (past it the garment is skin-coloured: a brown jacket, a tan hat)

export interface CharacterVerdict extends Verdict {
    skinTexels?: boolean; // a garment with skin in it: its skin-coloured texels shaded as skin (FLAG_SKIN_TEXELS)
}

// A character texture's class, or why it gets no map: the level rules' skips first (too small, overlays, environment
// maps, decals, see-through or cut-out textures, flat art) - but hair keeps its cut-outs (strands on clear, as foliage);
// a shiny material (an environment pass) is metal when grey (buckles, chains), else leather; then the dictionary's
// hint with the texture's skin share (skinShare - the whole bodies' and pros' textures are atlases of skin and clothes):
//   hair - hair (brown hair is skin-coloured: no test);
//   skin (hands, heads, faces, legs) - skin from SKIN_NAMED, else default (eyes, teeth, brows: plain per-pixel
//     lighting with a class bump that doesn't emboss an eye);
//   legwear - denim when blue, else cloth; denim, leather, rubber, cloth - that, never skin: a garment's colour can't
//     tell a brown jacket from bare arms, so its skin-coloured texels are shaded as skin only when they are part of it
//     (SKIN_TEXELS - SKIN_GARMENT: an atlas of a shirt and the arms in it);
//   fur (the animals) - cloth;
//   metal, wood - that class;
//   none (a whole body: the pros, the peds, the cutscenes) - skin from SKIN_WHOLE_BODY, else denim when blue, else
//     cloth, skin texels from SKIN_TEXELS.
export function classifyCharacter(stats: Stats, usage: Usage, hint: Hint, skin: number): CharacterVerdict {
    const level = classify(stats, usage);
    if (level.skip) {
        const strands =
            hint === "hair" &&
            (level.skip === "cut out by alpha" || level.skip === "see-through and blended") &&
            stats.cutout < 0.85;
        if (!strands) return level;
    }
    if (usage.shiny) return { name: stats.saturation < 0.15 ? "metal" : "leather" };
    const garment = (name: ClassName): CharacterVerdict => ({
        name,
        skinTexels: skin >= SKIN_TEXELS && skin < SKIN_GARMENT,
    });
    switch (hint) {
        case "hair":
            return { name: "hair" };
        case "skin":
            return { name: skin >= SKIN_NAMED ? "skin" : "default" };
        case "legwear":
            return garment(denimLike(stats) ? "denim" : "cloth");
        case "denim":
        case "leather":
        case "rubber":
        case "cloth":
            return garment(hint);
        case "metal":
        case "wood":
            return { name: hint };
        case "fur":
            return { name: "cloth" };
        case "none":
            if (skin >= SKIN_WHOLE_BODY) return { name: "skin" };
            return garment(denimLike(stats) ? "denim" : "cloth");
    }
}

// Whether a texture's strands run along u: its luma changes more along v (down the rows) than along u - the strands'
// edges cross the direction they don't run in.
export function strandsAlongU(image: Rgba): boolean {
    const w = image.width;
    const h = image.height;
    const luma = (x: number, y: number): number => {
        const i = (((y + h) % h) * w + ((x + w) % w)) * 4;
        return 0.2126 * image.data[i]! + 0.7152 * image.data[i + 1]! + 0.0722 * image.data[i + 2]!;
    };
    let alongU = 0;
    let alongV = 0;
    for (let y = 0; y < h; y++) {
        for (let x = 0; x < w; x++) {
            const c = luma(x, y);
            alongU += (luma(x + 1, y) - c) ** 2;
            alongV += (luma(x, y + 1) - c) ** 2;
        }
    }
    return alongV > alongU;
}
