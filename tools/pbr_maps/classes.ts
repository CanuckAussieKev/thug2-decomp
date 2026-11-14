// NOT ORIGINAL - what a texture shows, and the roughness / F0 / bump each class allows (docs/design/vulkan.md
// "Per-pixel lighting and surface maps", "Roughness"). The game's dictionaries keep no texture names, so a texture is
// classified from how its level uses it (the scene's material passes) and from its own pixels. Every generator's
// roughness - the classical one's and a model's - is clamped into its class's range: a model's roughness is often
// wrong on low-resolution game art (too glossy asphalt, rough glass), so the class decides the range and the generator
// only the detail inside it.

// (surface_map_format.h MaterialClass)
export const CLASS = {
    default: 0,
    asphalt: 1,
    concrete: 2,
    stone: 3,
    metal: 4,
    wood: 5,
    glass: 6,
    water: 7,
    foliage: 8,
    ground: 9,
    fabric: 10,
    tile: 11,
} as const;
export type ClassName = keyof typeof CLASS;

export interface ClassRule {
    min: number; // roughness range (perceptual: GGX's alpha is its square)
    max: number;
    base: number; // the roughness of an average texel
    f0: number; // reflectance at normal incidence
    bump: number; // the normals' strength: the classical generator's slope scale (0: flat)
}

// The rules. F0: 0.04 for dielectrics (water 0.02, glass 0.04); a metal's is its colour in reality - one number here,
// 0.6, as the per-pixel lighting keeps a scalar F0. Bump: how much relief the class shows at the game's texel sizes -
// a texture's own bump starts there and its level's terrains and texel size move it (surfaces.ts bumpFor). Asphalt and
// concrete are low: a street or a poured slab is smooth by definition, its grain is stains and dirt painted on.
export const RULES: Record<ClassName, ClassRule> = {
    default: { min: 0.55, max: 0.85, base: 0.72, f0: 0.04, bump: 0.5 },
    asphalt: { min: 0.75, max: 0.95, base: 0.88, f0: 0.04, bump: 0.4 },
    concrete: { min: 0.7, max: 0.95, base: 0.85, f0: 0.04, bump: 0.6 },
    stone: { min: 0.6, max: 0.95, base: 0.8, f0: 0.04, bump: 1.3 },
    metal: { min: 0.25, max: 0.6, base: 0.4, f0: 0.6, bump: 0.4 },
    wood: { min: 0.55, max: 0.85, base: 0.7, f0: 0.04, bump: 0.7 },
    glass: { min: 0.05, max: 0.2, base: 0.1, f0: 0.04, bump: 0.0 },
    water: { min: 0.03, max: 0.12, base: 0.06, f0: 0.02, bump: 0.2 },
    foliage: { min: 0.6, max: 0.85, base: 0.75, f0: 0.04, bump: 0.6 },
    ground: { min: 0.8, max: 1.0, base: 0.92, f0: 0.04, bump: 1.0 },
    fabric: { min: 0.7, max: 0.95, base: 0.85, f0: 0.04, bump: 0.5 },
    tile: { min: 0.2, max: 0.5, base: 0.35, f0: 0.04, bump: 0.6 },
};

export function className(value: number): ClassName {
    const found = (Object.keys(CLASS) as ClassName[]).find((name) => CLASS[name] === value);
    return found ?? "default";
}

// How the level's materials use a texture (the scene's passes: thug2_scene.ts PASS_*).
export interface Usage {
    base: boolean; // a material's first pass
    overlay: boolean; // only ever a later pass (a blended layer, a detail or light map)
    environment: boolean; // drawn as an environment map (the sky picture sphere-mapped on: not a surface)
    shiny: boolean; // the first pass of a material that has an environment pass (the game's shiny surfaces)
    decal: boolean; // a decal pass (PASS_DECAL)
    transparent: boolean; // a blended pass (PASS_TRANSPARENT: most of the game's materials - its alpha decides)
}

export interface Stats {
    width: number;
    height: number;
    cutout: number; // share of texels with alpha under 0.5
    translucent: number; // share of texels with alpha under 0.9
    meanLuma: number; // 0 - 1
    saturation: number; // mean HSV saturation
    hue: number; // the mean colour's hue in degrees (-1: grey)
    detail: number; // the luma's high-pass deviation (fine grain)
    colours: number; // distinct 5-bit colours (flat art and text have few)
    streaks: number; // |log(energy along x / energy along y)|: grain, planks, brick courses
}

export interface Verdict {
    skip?: string; // why no map is made
    name: ClassName;
}

// The class of a texture, or why it gets no map. Rules in order:
//   skipped - too small (< 16 texels a side); an environment map; only a later pass (overlays, light maps); a decal
//     pass, or a blended one whose texture is see-through (> 10% of it under 0.9 alpha: glass, glows, signs on
//     clear) - unless green (foliage); cut out by alpha (> 20%) unless green; flat art or text (fewer than 24
//     distinct 5-bit colours) - the classical normals would emboss its edges;
//   metal / glass - a shiny material's (an environment pass on it): grey (saturation < 0.15) is metal, else glass
//     when transparent, else tile (glazed, painted shiny);
//   foliage - green hue (70-170 degrees), saturated (> 0.25);
//   water - blue hue (180-240), smooth (detail < 0.03), transparent use;
//   wood - orange-brown hue (15-45), saturation 0.25-0.75, streaked (> 0.35);
//   ground - brown or yellow hue (20-60), saturation 0.15-0.5, grainy (detail > 0.05);
//   asphalt - grey (saturation < 0.12), dark (luma < 0.32), grainy (detail > 0.035);
//   concrete - grey (< 0.15), luma 0.32 - 0.8;
//   stone - grainy (detail > 0.06) or streaked (> 0.5) at saturation < 0.35 (brick courses, rock);
//   default (painted) otherwise.
export function classify(stats: Stats, usage: Usage): Verdict {
    if (stats.width < 16 || stats.height < 16) return { skip: "smaller than 16 texels", name: "default" };
    if (usage.environment && !usage.base) return { skip: "an environment map", name: "default" };
    if (usage.overlay && !usage.base) return { skip: "only a later pass (an overlay)", name: "default" };
    const green = stats.hue >= 70 && stats.hue <= 170 && stats.saturation > 0.25;
    if (usage.decal && !green) return { skip: "a decal pass", name: "default" };
    if (usage.transparent && stats.translucent > 0.1 && !green)
        return { skip: "see-through and blended", name: "default" };
    if (stats.cutout > 0.2 && !green) return { skip: "cut out by alpha", name: "default" };
    if (stats.colours < 24) return { skip: "flat art or text", name: "default" };
    if (usage.shiny) {
        if (stats.saturation < 0.15) return { name: "metal" };
        return { name: usage.transparent && stats.translucent > 0.1 ? "glass" : "tile" };
    }
    if (green) return { name: "foliage" };
    if (stats.hue >= 180 && stats.hue <= 240 && stats.detail < 0.03 && usage.transparent) return { name: "water" };
    if (
        stats.hue >= 15 &&
        stats.hue <= 45 &&
        stats.saturation > 0.25 &&
        stats.saturation < 0.75 &&
        stats.streaks > 0.35
    ) {
        return { name: "wood" };
    }
    if (
        stats.hue >= 20 &&
        stats.hue <= 60 &&
        stats.saturation > 0.15 &&
        stats.saturation < 0.5 &&
        stats.detail > 0.05
    ) {
        return { name: "ground" };
    }
    if (stats.saturation < 0.12 && stats.meanLuma < 0.32 && stats.detail > 0.035) return { name: "asphalt" };
    if (stats.saturation < 0.15 && stats.meanLuma >= 0.32 && stats.meanLuma <= 0.8) return { name: "concrete" };
    if ((stats.detail > 0.06 || stats.streaks > 0.5) && stats.saturation < 0.35) return { name: "stone" };
    return { name: "default" };
}

// A generator's roughness for a texel brought into its class's range.
export function clampRoughness(value: number, rule: ClassRule): number {
    return Math.min(rule.max, Math.max(rule.min, Number.isFinite(value) ? value : rule.base));
}
