// NOT ORIGINAL - the detail materials' surface kinds (src/formats/detail_material_format.h Kind: the same numbers) and how
// a texture's kind is decided from the evidence: the terrain types of the collision faces drawn with it
// (texture_terrain.ts), else its surface map's class (tools/pbr_maps/classes.ts).
import type { ClassName } from "../pbr_maps/classes.ts";

export const KIND = {
    none: 0,
    asphalt: 1,
    concrete_smooth: 2,
    concrete_rough: 3,
    brick: 4,
    stone: 5,
    metal: 6,
    wood: 7,
    ground: 8,
    grass: 9,
    tile: 10,
    plaster: 11,
    gravel: 12,
} as const;
export type KindName = keyof typeof KIND;

export const EVIDENCE_CLASS = 0;
export const EVIDENCE_TERRAIN = 1;

// THUG2's terrain names (scripts\game\terrain.qb's Terrain_Types, without TERRAIN_) as kinds; null: says nothing about
// the surface's look (DEFAULT, the grind terrains, glass, water, carpet...).
export function kindOfTerrain(name: string): KindName | null {
    if (name === "ASPHALT") return "asphalt";
    if (name === "CONCSMOOTH" || name === "SIDEWALK" || name === "WETCONC") return "concrete_smooth";
    if (name === "CONCROUGH") return "concrete_rough";
    if (name === "BRICK") return "brick";
    if (name === "ROCK") return "stone";
    if (name.startsWith("METAL")) return "metal";
    if (name.startsWith("WOOD")) return "wood";
    if (name === "DIRT" || name === "DIRTPACKED" || name === "SAND") return "ground";
    if (name === "GRASS" || name === "GRASSDRIED") return "grass";
    if (name === "TILE") return "tile";
    if (name === "GRAVEL") return "gravel";
    return null;
}

// A surface map's class as a kind (the game does the same for a texture the library doesn't list: vulkan_detail.cpp
// kind_of_class).
export function kindOfClass(name: ClassName): KindName {
    switch (name) {
        case "asphalt":
            return "asphalt";
        case "concrete":
            return "concrete_smooth";
        case "stone":
            return "stone";
        case "metal":
            return "metal";
        case "wood":
            return "wood";
        case "foliage":
            return "grass";
        case "ground":
            return "ground";
        case "tile":
            return "tile";
        default:
            return "none";
    }
}

// The terrain evidence for one texture: the area (square units) of its triangles over collision faces of each terrain
// name, and of those with no collision face found.
export interface TerrainEvidence {
    areas: Map<string, number>;
    unmatched: number;
    level: number; // the area of its triangles facing up or down (floors, ceilings, roofs: |normal.y| > 0.7), matched or not
    total: number; // all its triangles' area
}

export interface Decision {
    kind: KindName;
    evidence: typeof EVIDENCE_CLASS | typeof EVIDENCE_TERRAIN;
}

// The share of a texture's matched area one kind needs for the terrain to decide it.
export const TERRAIN_SHARE = 0.5;

// The most of a texture's area that may lie level for DEFAULT faces to make it plaster (walls, not floors).
export const WALL_LEVEL_SHARE = 0.3;

// A texture's kind: glass and water (see-through or liquid) never get one; else the terrain kind covering at least
// TERRAIN_SHARE of its matched area; else - mostly over DEFAULT faces, mostly upright and a painted (default) class -
// plaster (the game's painted walls); else its class's kind.
export function decideKind(className: ClassName, terrain: TerrainEvidence | undefined): Decision {
    const fromClass: Decision = { kind: kindOfClass(className), evidence: EVIDENCE_CLASS };
    if (className === "glass" || className === "water") return { kind: "none", evidence: EVIDENCE_CLASS };
    if (!terrain) return fromClass;
    let matched = 0;
    const byKind = new Map<KindName, number>();
    for (const [name, area] of terrain.areas) {
        matched += area;
        const kind = kindOfTerrain(name);
        if (kind) byKind.set(kind, (byKind.get(kind) ?? 0) + area);
    }
    if (matched <= 0) return fromClass;
    let best: KindName | null = null;
    let bestArea = 0;
    for (const [kind, area] of [...byKind].sort((a, b) => KIND[a[0]] - KIND[b[0]])) {
        if (area > bestArea) {
            best = kind;
            bestArea = area;
        }
    }
    if (best && bestArea >= TERRAIN_SHARE * matched) return { kind: best, evidence: EVIDENCE_TERRAIN };
    const upright = terrain.total > 0 && terrain.level <= WALL_LEVEL_SHARE * terrain.total;
    if ((terrain.areas.get("DEFAULT") ?? 0) >= TERRAIN_SHARE * matched && className === "default" && upright) {
        return { kind: "plaster", evidence: EVIDENCE_TERRAIN };
    }
    return fromClass;
}
