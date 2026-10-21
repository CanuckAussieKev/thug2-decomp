// NOT ORIGINAL - Tony Hawk's Project 8's levels (the Xbox 360 game's data) converted to THUG2's formats, for its level
// select's PROJECT 8 section (tools/menu_level/make_menu_level.ts). Nothing of either game is in the repo: the files
// are made from the user's own copies each time. README.md has how to run it and what is converted.
//
// THP8's world is one city streamed in zones (game\DATA\COMPRESSED\ZONES\z_<zone>.pak.xen, each with its _lod
// stand-in); each zone becomes a THUG2 level here (the whole city is ~10 times what THUG2's memory takes). A zone's
// archive holds the zone and its _gfx / _sfx sub-zones; the main one (pak flags 0) is converted: its scene (.geom),
// textures (.tex), collision (.hkc, Havok) and node array (.nqb). The network areas' barriers (z_online_<x>.pak.xen)
// are added to the zone they close off.
//
// Space: THP8's metres (y up) as THUG2's inches (x 39.37); each zone moved (x / z, whole sixteenths of an inch) so the
// middle of its collision is at the origin.

import * as fs from "node:fs";
import * as common from "../level_port/common.ts";
import * as geometry from "../level_port/geometry.ts";
import * as nodes from "../level_port/nodes.ts";
import * as pedsCars from "../level_port/peds_cars.ts";
import * as portedLevels from "../level_port/ported_levels.ts";
import * as qb from "../level_port/qb.ts";
import { checksum, Name, type QbValue, Struct, Vector } from "../level_port/qb.ts";
import * as scriptPort from "../level_port/script_port.ts";
import type * as tex from "../level_port/tex.ts";
import * as thug2Col from "../level_port/thug2_col.ts";
import * as thug2Scene from "../level_port/thug2_scene.ts";
import { parseCli } from "../lib/cli.ts";
import { findCaseless, resolveInstall } from "../lib/install.ts";
import { Float, isMain, print, pyExit, pyJoin, pyMinBy, runMain } from "../lib/py.ts";
import * as thaw from "../thaw_levels/thaw.ts";
import { applyNodeFlags, nodeFlagScript } from "../thaw_levels/thaw_levels.ts";
import * as thawScripts from "../thaw_levels/thaw_scripts.ts";
import { makeMaterial, THUG2_TERRAIN_TYPES, trisToStrip } from "../thps2_levels/thps2_levels.ts";
import * as geom from "./geom.ts";
import * as havok from "./havok.ts";
import * as mats from "./materials.ts";
import * as thp8 from "./thp8.ts";
import * as xenos from "./xenos.ts";

export const STAGING = "thug2-decomp-thp8";
export const SCALE = 39.37; // THP8's metres in THUG2's inches

export interface ZoneInfo {
    zone: string; // THP8's zone (z_houses)
    sign: string; // the level select's sign text
    location: string;
    levelNum: number;
    online?: string; // the network area whose barriers close it off (z_online_<x>)
}

// The city's zones (a name each from THP8's network areas: z_online_suburbs is the houses' barriers...), then the
// separate ones. Level select numbers 110...
export const ZONES: readonly ZoneInfo[] = [
    {
        zone: "z_houses",
        sign: "SUBURBS",
        location: "THP8's city: the suburbs",
        levelNum: 110,
        online: "z_online_suburbs",
    },
    {
        zone: "z_outschool",
        sign: "SCHOOL",
        location: "THP8's city: the high school",
        levelNum: 111,
        online: "z_online_school",
    },
    { zone: "z_inschool", sign: "SCHOOL INSIDE", location: "THP8's city: inside the high school", levelNum: 112 },
    { zone: "z_shops", sign: "SHOPS", location: "THP8's city: the shops", levelNum: 113, online: "z_online_shops" },
    { zone: "z_ghetto", sign: "SLUMS", location: "THP8's city: the slums", levelNum: 114, online: "z_online_ghetto" },
    {
        zone: "z_factory",
        sign: "FACTORY",
        location: "THP8's city: the factory",
        levelNum: 115,
        online: "z_online_factory",
    },
    {
        zone: "z_center",
        sign: "CAPITOL",
        location: "THP8's city: the capitol",
        levelNum: 116,
        online: "z_online_capitol",
    },
    { zone: "z_citypark", sign: "CITY PARK", location: "THP8's city: the city park", levelNum: 117 },
    {
        zone: "z_cretepark",
        sign: "SKATE PARK",
        location: "THP8's city: the skate park",
        levelNum: 118,
        online: "z_online_skatepark",
    },
    {
        zone: "z_funpark",
        sign: "FUN PARK",
        location: "THP8's city: the fun park",
        levelNum: 119,
        online: "z_online_funpark",
    },
    { zone: "z_parking", sign: "PARKING", location: "THP8's city: the parking lot", levelNum: 120 },
    { zone: "z_training", sign: "TRAINING", location: "THP8's training area", levelNum: 121 },
    { zone: "z_cartoon", sign: "CARTOON", location: "THP8's cartoon world", levelNum: 122 },
    { zone: "z_skateshop", sign: "SKATE SHOP", location: "THP8's skate shop", levelNum: 123 },
    { zone: "z_bedroom", sign: "BEDROOM", location: "THP8's bedroom", levelNum: 124 },
];

export function levelName(info: ZoneInfo): string {
    return `thp8_${info.zone.replace(/^z_/, "")}`;
}

export function findZone(name: string): ZoneInfo | undefined {
    const n = name.toLowerCase();
    return ZONES.find((z) => z.zone === n || z.zone === `z_${n}` || levelName(z) === n);
}

// --- the install ------------------------------------------------------------------------------------------------

export function compressedFolder(thp8Root: string): string {
    const root = resolveInstall(thp8Root, "thp8");
    const found = findCaseless(root, "DATA", "COMPRESSED");
    if (found === null) pyExit(`${root}: no DATA\\COMPRESSED`);
    return found;
}

export function zonePak(compressed: string, zone: string): string | null {
    return findCaseless(compressed, "ZONES", `${zone}.pak.xen`);
}

// --- terrains -----------------------------------------------------------------------------------------------------

// THP8's terrain numbers (a collision material's bits 16-23): its terrain_types table (scripts\...\terrain.qb, read
// from qb.pak.xen at run time - terrainTable) by name. As THUG2's: the nearest material.
const THP8_TO_THUG2: Record<string, (typeof THUG2_TERRAIN_TYPES)[number]> = {
    DEFAULT: "DEFAULT",
    ASPHALT: "ASPHALT",
    ASPHALTSMOOTH: "ASPHALT",
    ASPHALTROUGH: "ASPHALT",
    ASPHALTSMOOTH_HF: "ASPHALT",
    BRICK: "BRICK",
    BRICK_6IN: "BRICK",
    BRICK_1FT: "BRICK",
    BRICK_2FT: "BRICK",
    BRICK_4FT: "BRICK",
    COBBLESTONE: "BRICK",
    CONCSMOOTH: "CONCSMOOTH",
    CONCSMOOTHTRAINING: "CONCSMOOTH",
    CONCROUGH: "CONCROUGH",
    CONCROUGHTRAINING: "CONCROUGH",
    RUBBLE: "ROCK",
    GRAVEL: "GRAVEL",
    DIRT: "DIRT",
    MUD: "DIRTPACKED",
    FLOWERBED: "DIRT",
    SAND: "SAND",
    GRASS: "GRASS",
    GRASS_HF: "GRASS",
    GRASSDRIED: "GRASSDRIED",
    LEAVES: "GRASSDRIED",
    WATER: "WATER",
    WATERSHALLOW: "WATER",
    WATERPUDDLE: "WATER",
    SHALLOWWATERPOOLTILE: "TILE",
    SIDEWALK: "SIDEWALK",
    SIDEWALK_1FT: "SIDEWALK",
    SIDEWALK_2FT: "SIDEWALK",
    SIDEWALK_4FT: "SIDEWALK",
    SIDEWALK_8FT: "SIDEWALK",
    SIDEWALK_4FT_HF: "SIDEWALK",
    TILE_6IN: "TILE",
    TILE_2FT: "TILE",
    TILE_4FT: "TILE",
    METAL: "METALSMOOTH",
    METALPOLE: "METALSMOOTH",
    METALPIPELARGE: "METALSMOOTH",
    METALCAR: "METALSMOOTH",
    METALLOCKERS: "METALSMOOTH",
    METALTHIN: "METALTIN",
    METALTIN: "METALTIN",
    METALGRATE: "METALGRATING",
    MAILBOX: "METALTIN",
    CHAIN: "CHAINLINK",
    CHAINLINKFENCE: "CHAINLINK",
    ELECTRICWIRE: "METALSMOOTH",
    ELECTRICFLOOR: "METALSMOOTH",
    WOOD: "WOOD",
    WOODPAINTED_W: "WOOD",
    WOODDECK: "WOODPIER",
    WOOD_4FT: "WOOD",
    WOOD_8FT: "WOOD",
    WOODFENCE: "WOODFLIMSY",
    WOODSIDING: "WOODMASONITE",
    WOODTABLE: "WOOD",
    WOODPLYWOOD: "WOODPLYWOOD",
    WOODGYMFLOOR: "WOODMASONITE",
    WOODBAMBOOPILLONS: "WOOD",
    SHINGLESCOMPOSITE: "WOODSHINGLE",
    SHINGLESWOOD: "WOODSHINGLE",
    SHINGLESTILE: "WOODSHINGLE",
    ROOFTOP: "CONCROUGH",
    PLASTICHOLLOW: "FIBERGLASS",
    PLASTICHARD: "FIBERGLASS",
    PLASTICPOOLCOVER: "FIBERGLASS",
    PLASTICPICNICTABLE: "FIBERGLASS",
    COUNTERTOPLAB: "FIBERGLASS",
    CHALKBOARD: "WOODMASONITE",
    CANVASAWNING: "CARPET",
    MATTRESS: "CARPET",
    RUBBER: "CARPET",
    GRIPTAPE: "CARPET",
    ROPE: "GENERIC1",
    GLASS: "PLEXIGLASS",
    GLASSWINDSHIELD: "PLEXIGLASS",
    ARCADE: "FIBERGLASS",
    GOLDCOINS: "METALTIN",
    ZEROFRICTION: "ICE",
    HIGHFRICTION: "CARPET",
};

// --- node flags ----------------------------------------------------------------------------------------------------

// THP8 makes much of its zones' geometry from node flags (`CreatedFromVariable=NODEFLAG_...`: the gates between zones,
// goal pieces, the turf war's signs), as THAW does. The flags as THP8's non-story games set them (qb.pak's
// game_progress_set_non_story_mode_nodeflags - the unlocked state: gates open, barriers down) - read with
// tools/thaw_levels' nodeFlagScript; the nodes they make get CreatedAtStart (applyNodeFlags).
export const NODEFLAG_SCRIPT = "game_progress_set_non_story_mode_nodeflags";

export function nonStoryNodeFlags(compressed: string, names: Map<number, string>): Map<number, boolean> {
    const pak = thp8.openPak(findCaseless(compressed, "PAK", "qb.pak.xen")!);
    const key = checksum(NODEFLAG_SCRIPT);
    for (const e of pak.entries) {
        if (e.extension !== ".qb") continue;
        const q = new thaw.QbItems(thp8.entryData(pak, e), "qb.pak", names, ">");
        if (q.scriptData.has(key)) return nodeFlagScript(q.scriptData, key, "qb.pak.xen");
    }
    pyExit(`qb.pak.xen: no ${NODEFLAG_SCRIPT}`);
}

// The zones' time-of-day settings (qb.pak's `<zone>_tod_manager` structures; `Default_TOD_Manager` for zones without
// one): their `tod` structure has THUG2's level lighting keys (ambient + two directional lights) and the sun's
// colour (sun_red / _green / _blue), which lights the shadow maps.
export function todManagers(compressed: string, names: Map<number, string>): Map<number, Struct> {
    const pak = thp8.openPak(findCaseless(compressed, "PAK", "qb.pak.xen")!);
    const out = new Map<number, Struct>();
    for (const e of pak.entries) {
        if (e.extension !== ".qb") continue;
        const q = new thaw.QbItems(thp8.entryData(pak, e), "qb.pak", names, ">");
        for (const [k, v] of q.globals) {
            const tod = v instanceof Struct ? v.get("tod") : undefined;
            if (tod instanceof Struct && /_tod_manager$/i.test(names.get(k.value) ?? "")) out.set(k.value, tod);
        }
    }
    return out;
}

export function zoneTod(managers: ReadonlyMap<number, Struct>, zone: string): Struct | undefined {
    return managers.get(checksum(`${zone}_tod_manager`)) ?? managers.get(checksum("Default_TOD_Manager"));
}

// How bright THP8's light maps are made (materials.light): the ambient map x AMBIENT_SCALE plus the sun's colour x
// the shadow map x SUN_SCALE. Ours (THP8's shaders aren't read): sunlit ground near THUG2's full brightness, shade
// about half.
export const AMBIENT_SCALE = 1.5;
export const SUN_SCALE = 0.6;
// The sun's colour halfway to white (THP8 takes the saturation of its frames down after drawing - its Bright_Sat
// screen effect, saturation 0.7 - which THUG2 doesn't: the full colour came out orange).
export const SUN_WHITEN = 0.5;

// Interiors (the materials whose technique is an ..._InteriorLighting one: the factory, the school inside, the skate
// shop, the bedroom) are lit by their ambient map alone - their shadow map is the white stand-in - which holds the
// rooms' light (warm, darker than outdoors': the factory's averages 0.18): x INTERIOR_SCALE / (1 + INTERIOR_KNEE x)
// - ours, as the outdoor scales: dim rooms brought up, the bright ones not burnt white (x 4 straight left the
// factory's floor white).
export const INTERIOR_SCALE = 6.0;
export const INTERIOR_KNEE = 4.0;
// Light maps this small (texels) are THP8's stand-ins (32 x 32, one colour: 128 grey ambient, white shadow).
export const STAND_IN = 32 * 32;
export function interiorLighting(): mats.Lighting {
    return { sun: [0, 0, 0], ambientScale: INTERIOR_SCALE, sunScale: 0, ambientKnee: INTERIOR_KNEE };
}

export function zoneLighting(tod: Struct | undefined): mats.Lighting {
    const num = (key: string, fallback: number): number => {
        const v = tod?.get(key);
        return typeof v === "number" ? v : v instanceof Float ? v.value : fallback;
    };
    return {
        sun: [num("sun_red", 255), num("sun_green", 211), num("sun_blue", 165)].map(
            (c) => (c / 255) * (1 - SUN_WHITEN) + SUN_WHITEN,
        ),
        ambientScale: AMBIENT_SCALE,
        sunScale: num("sun_on", 1) ? SUN_SCALE : 0,
    };
}

// The sectors of LevelGeometry nodes left without CreatedAtStart (THUG2 never makes them: their geometry isn't drawn or
// collided with) - left out of the scene and collision rather than kept unseen.
export function notMade(array: readonly QbValue[], names: ReadonlyMap<number, string>): Set<number> {
    const out = new Set<number>();
    for (const n of array) {
        if (!(n instanceof Struct) || !["levelgeometry", "levelobject"].includes(nodes.classOf(n, names))) continue;
        const name = nodeName(n);
        if (name !== null && !n.hasFlag("CreatedAtStart") && n.get("MasterName") === undefined) out.add(name);
    }
    return out;
}

// THP8's moving objects (LevelObject: the gym's roof fans, doors): their sectors and collision are in their own space
// (centred on the origin) and THUG2 puts them at the node's Pos - left there, not moved with the zone. The ones made.
export function localObjects(array: readonly QbValue[], names: ReadonlyMap<number, string>): Set<number> {
    const out = new Set<number>();
    for (const n of array) {
        if (!(n instanceof Struct) || nodes.classOf(n, names) !== "levelobject" || !n.hasFlag("CreatedAtStart"))
            continue;
        const name = nodeName(n);
        if (name !== null) out.add(name);
    }
    return out;
}

// THP8's terrain_types names in order (the qb item format, PAK\qb.pak.xen).
export function terrainTable(compressed: string, names: Map<number, string>): string[] {
    const pak = thp8.openPak(findCaseless(compressed, "PAK", "qb.pak.xen")!);
    const key = checksum("terrain_types");
    for (const e of pak.entries) {
        if (e.extension !== ".qb") continue;
        const data = thp8.entryData(pak, e);
        const k = Buffer.alloc(4);
        k.writeUInt32BE(key);
        if (data.indexOf(k) < 0) continue;
        const q = new thaw.QbItems(data, "terrain.qb", names, ">");
        const table = q.globals.find(([n]) => n.value === key)?.[1];
        if (!Array.isArray(table)) continue;
        return table.map((t) => {
            const c = t instanceof Struct ? t.get("checksum") : undefined;
            return c instanceof Name ? (names.get(c.value) ?? "").replace(/^TERRAIN_/i, "").toUpperCase() : "";
        });
    }
    pyExit("qb.pak.xen: no terrain_types");
}

export function thug2Terrain(table: readonly string[], index: number): number {
    const name = table[index] ?? "DEFAULT";
    return THUG2_TERRAIN_TYPES.indexOf(THP8_TO_THUG2[name] ?? "DEFAULT");
}

// --- transforms ---------------------------------------------------------------------------------------------------

export type Vec3 = [number, number, number];

// A node's rotation (Angles, radians: about x, y, z) applied to a point: R = Ry * Rx * Rz.
export function rotate(p: readonly number[], angles: readonly number[]): Vec3 {
    let [x, y, z] = p as [number, number, number];
    const [ax, ay, az] = angles as [number, number, number];
    // z
    [x, y] = [x * Math.cos(az) - y * Math.sin(az), x * Math.sin(az) + y * Math.cos(az)];
    // x
    [y, z] = [y * Math.cos(ax) - z * Math.sin(ax), y * Math.sin(ax) + z * Math.cos(ax)];
    // y
    [x, z] = [x * Math.cos(ay) + z * Math.sin(ay), -x * Math.sin(ay) + z * Math.cos(ay)];
    return [x, y, z];
}

export interface Placement {
    pos: number[];
    angles: number[];
    scale: number[];
}

export function place(p: readonly number[], at: Placement | null): Vec3 {
    if (at === null) return [p[0]!, p[1]!, p[2]!];
    const s = [p[0]! * at.scale[0]!, p[1]! * at.scale[1]!, p[2]! * at.scale[2]!];
    const r = rotate(s, at.angles);
    return [r[0] + at.pos[0]!, r[1] + at.pos[1]!, r[2] + at.pos[2]!];
}

// THP8 metres -> THUG2 inches, moved by `shift` (inches).
export const toThug = (p: readonly number[], shift: readonly number[]): Vec3 => [
    p[0]! * SCALE + shift[0]!,
    p[1]! * SCALE + shift[1]!,
    p[2]! * SCALE + shift[2]!,
];

// --- nodes --------------------------------------------------------------------------------------------------------

const vec = (v: QbValue | undefined): number[] | null =>
    v instanceof Vector && v.kind === "vector" ? [...v.values] : null;

export function nodeName(n: Struct): number | null {
    const v = n.get("Name");
    return v instanceof Name ? v.value : null;
}

// Instances: nodes naming a MasterName (a __MASTERNODE_ sector at the origin) and where to put a copy of it.
export function instances(array: readonly QbValue[]): Map<number, { master: number; at: Placement }> {
    const out = new Map<number, { master: number; at: Placement }>();
    for (const n of array) {
        if (!(n instanceof Struct)) continue;
        const master = n.get("MasterName");
        const name = nodeName(n);
        if (!(master instanceof Name) || name === null) continue;
        out.set(name, {
            master: master.value,
            at: {
                pos: vec(n.get("Pos")) ?? [0, 0, 0],
                angles: vec(n.get("Angles")) ?? [0, 0, 0],
                scale: vec(n.get("scale")) ?? [1, 1, 1],
            },
        });
    }
    return out;
}

// --- scene --------------------------------------------------------------------------------------------------------

export interface SceneParts {
    geom: geom.Geom;
    textures: Map<number, xenos.XenosTexture>;
    // sectors to draw as copies elsewhere (instances): name -> [master sector, placement]
    placed: Map<number, { master: number; at: Placement }>;
    hidden?: ReadonlySet<number>; // sectors not made (notMade)
    dome?: boolean; // untextured materials coloured as a clear sky (THP8's atmosphere shader - the sky's dome)
    additive?: ReadonlySet<number>; // materials drawn adding (glowMaterials)
    lighting?: mats.Lighting; // THP8's light maps baked into the vertex colours (materials.ts)
    techniques?: ReadonlyMap<number, readonly string[]>; // the material library's (materials.readTechniques)
    local?: ReadonlySet<number>; // sectors in their own space (localObjects): not moved
}

// A material's layers as converted: the uv slot each THUG2 pass reads, the light maps, and the mask read into the
// vertex alpha (a layer whose mask has a uv field of its own).
interface Plan {
    plan: mats.MaterialPlan;
    slots: number[];
    light?: { ambient: mats.Decoded; shadow: mats.Decoded; lighting: mats.Lighting };
    vertexMask: { slot: number; mask: mats.Decoded; opacity: number } | null;
}

// THP8's glows and light shafts (lamp halos, sun shafts: textures on black that its shaders add) by their sectors'
// names - their blend mode is somewhere in the material's parameter block, not found yet. A material is drawn adding
// when every sector using it is one of these; blended by its alpha they stood as white haze.
export const GLOW_SECTOR = /(glow\d*$|_glo\d*$|glow_|_glo_|lightshaft|light_shaft)/i;
export function glowMaterials(g: geom.Geom, names: ReadonlyMap<number, string>): Set<number> {
    const glow = new Map<number, boolean>();
    for (const s of g.sectors) {
        const is = GLOW_SECTOR.test(names.get(s.checksum) ?? "");
        for (const m of s.meshes) glow.set(m.material, (glow.get(m.material) ?? true) && is);
    }
    return new Set([...glow].filter(([, is]) => is).map(([m]) => m));
}

// A material's colour: in many parameter blocks the four floats (R, G, B, A) just before the first texture's
// checksum - a tint, or for glows their strength in A (0.09 for the skate shop's lamp halos). Not every block has it
// (and in some the floats are 0, 0, 0 - another parameter): null when missing or out of range.
export function materialColour(m: geom.GeomMaterial): number[] | null {
    if (!m.textures.length) return null;
    const p = m.params;
    for (let at = 0x20; at + 4 <= p.length; at += 4) {
        if (p.readUInt32BE(at) !== m.textures[0]) continue;
        const c = [0, 1, 2, 3].map((i) => p.readFloatBE(at - 16 + 4 * i));
        return c.every((x) => Number.isFinite(x) && x >= 0 && x <= 4) ? c : null;
    }
    return null;
}

// How bright a glow is drawn (its vertex colours scaled): its colour times its alpha, or a stand-in when its
// parameter block has none.
export const GLOW_DEFAULT = 0.35;
export function glowScale(m: geom.GeomMaterial): number[] {
    const c = materialColour(m);
    if (!c) return [GLOW_DEFAULT, GLOW_DEFAULT, GLOW_DEFAULT];
    return [c[0]! * c[3]!, c[1]! * c[3]!, c[2]! * c[3]!].map((x) => Math.min(1, x));
}

const BILLBOARD = checksum("billboard"); // the shader template of THP8's camera-facing quads

const SECTOR_FLAGS = (0x80002000 | thug2Scene.SEC_TEXCOORDS | thug2Scene.SEC_COLORS) >>> 0;

export interface ConvertedScene {
    scene: thug2Scene.Scene;
    textures: tex.TexFile;
    sectors: Set<number>; // the sector checksums written
    counts: {
        sectors: number;
        instances: number;
        masters: number;
        triangles: number;
        skipped: number;
        hidden: number;
        billboards: number;
        layers: number; // further passes made (materials.ts)
        lit: number; // vertices lit from light maps
        refined: number; // lit triangles after refineLit
        refinedFrom: number; // ... and before
    };
}

// The materials and textures a set of THP8 scenes uses, as THUG2's: each material one pass with its first texture
// (THP8's shaders layer normal / specular / ambient maps over it), blended when the texture has alpha.
export function convertScenes(
    parts: readonly SceneParts[],
    shift: readonly number[],
    warnings: string[],
    mips = true,
    unit = SCALE, // inches per metre (the sky: SKY_UNIT)
    refineOptions: mats.RefineOptions = REFINE,
): ConvertedScene {
    const materials = new Map<number, thug2Scene.Material>();
    const texOut = new Map<number, tex.Texture>();
    const transparent = new Set<number>();
    const billboards = new Set<number>();
    const glows = new Map<number, number[]>(); // additive materials -> their R, G, B scale (glowScale)
    const sectors: geometry.SourceSector[] = [];
    const written = new Set<number>();
    const counts = {
        sectors: 0,
        instances: 0,
        masters: 0,
        triangles: 0,
        skipped: 0,
        hidden: 0,
        billboards: 0,
        layers: 0,
        lit: 0,
        refined: 0,
        refinedFrom: 0,
    };
    const plans = new Map<number, Plan>();
    for (const part of parts) {
        const g = part.geom;
        const maps = mats.lightMaps(g.materials);
        const decoded = new Map<number, mats.Decoded | null>();
        const decode = (t: number): mats.Decoded | null => {
            if (!decoded.has(t)) {
                const x = part.textures.get(t);
                decoded.set(t, x ? mats.decode(x) : null);
            }
            return decoded.get(t)!;
        };
        const meshesOf = new Map<number, geom.GeomMesh[]>();
        for (const s of g.sectors)
            for (const m of s.meshes) meshesOf.set(m.material, [...(meshesOf.get(m.material) ?? []), m]);
        const convertTexture = (t: number): boolean => {
            if (!texOut.has(t) && part.textures.has(t)) {
                const c = xenos.toThug2(part.textures.get(t)!, mips);
                if (c.texture) texOut.set(t, c.texture);
                else warnings.push(`texture ${thp8.hex(t)}: ${c.note} - left out`);
            }
            return texOut.has(t);
        };
        // materials
        for (const m of g.materials) {
            if (materials.has(m.checksum) || !m.checksum) continue;
            if (m.template === BILLBOARD) billboards.add(m.checksum);
            const used = meshesOf.get(m.checksum) ?? [];
            const slots = used.length ? geom.uvSlots(used[0]!.layout).length : 0;
            const plan =
                part.dome || part.additive?.has(m.checksum) ? null : mats.planMaterial(m, part.textures, maps, slots);
            let texture = 0;
            // (the first texture that isn't a normal map: a few shaders list theirs first)
            const first =
                plan?.layers[0]?.diffuse ??
                m.textures.find((t) => part.textures.has(t) && part.textures.get(t)!.format !== xenos.FMT_DXN);
            if (first !== undefined && convertTexture(first)) texture = first;
            const t = texOut.get(texture);
            const add = texture !== 0 && (part.additive?.has(m.checksum) ?? false);
            const alpha = !add && t !== undefined && (t.dxt === 5 || t.dxt === 2);
            if (alpha) transparent.add(m.checksum);
            const mat = makeMaterial(m.checksum, texture, add ? "add" : alpha, true);
            if (alpha || add) mat.noBackfaceCulling = 1;
            if (add) {
                // (in the transparent bucket, after the opaque scene: drawn there without it, a halo's black
                // square stood on the floor)
                mat.passes[0]!.flags |= thug2Scene.PASS_TRANSPARENT;
                mat.drawOrder = 1500.0;
                glows.set(m.checksum, glowScale(m));
            }
            if (plan && texture && plan.layers.length) {
                const out: Plan = { plan, slots: [plan.layers[0]!.slot], vertexMask: null };
                // (THUG2's pass colour: 0.5 = as the texture)
                const tint = (pass: thug2Scene.Pass, c: readonly number[]): void => {
                    if (c.slice(0, 3).every((x) => x === 1)) return;
                    pass.hasColor = 1;
                    pass.color = c.slice(0, 3).map((x) => x * 0.5);
                };
                tint(mat.passes[0]!, plan.layers[0]!.colour);
                // the further layers (an opaque pass 0 only: a blended one's vertex alpha is its own)
                for (const layer of alpha ? [] : plan.layers.slice(1)) {
                    const shared =
                        layer.maskSlot === layer.slot ||
                        used.every((mesh) =>
                            geom.readVertices(mesh).every((v) => {
                                const a = v.uvs[layer.slot];
                                const b = v.uvs[layer.maskSlot];
                                return a && b && Math.abs(a[0]! - b[0]!) < 1e-3 && Math.abs(a[1]! - b[1]!) < 1e-3;
                            }),
                        );
                    let layerTexture: number;
                    if (shared) {
                        // (mask in the texture's alpha)
                        const opacity = Math.round(layer.colour[3]! * 100);
                        layerTexture = checksum(
                            `thp8_layer_${thp8.hex(layer.diffuse)}_${thp8.hex(layer.mask!)}_${opacity}`,
                        );
                        if (!texOut.has(layerTexture)) {
                            const d = decode(layer.diffuse);
                            const k = decode(layer.mask!);
                            if (!d || !k) continue;
                            texOut.set(
                                layerTexture,
                                xenos.fromBgra(
                                    layerTexture,
                                    d.width,
                                    d.height,
                                    mats.maskedLayer(d, k, opacity / 100),
                                    mips,
                                ),
                            );
                        }
                    } else {
                        // (mask in the vertex alpha: one layer can)
                        if (out.vertexMask !== null || !convertTexture(layer.diffuse) || !decode(layer.mask!)) continue;
                        out.vertexMask = {
                            slot: layer.maskSlot,
                            mask: decode(layer.mask!)!,
                            opacity: layer.colour[3]!,
                        };
                        layerTexture = layer.diffuse;
                    }
                    const pass = makeMaterial(0, layerTexture, false, true).passes[0]!;
                    pass.blendMode = 5; // (THUG2's blend: by the pass's texture alpha times the vertex alpha x 2)
                    if (shared) pass.flags |= thug2Scene.PASS_IGNORE_VERTEX_ALPHA;
                    else pass.flags &= ~thug2Scene.PASS_IGNORE_VERTEX_ALPHA;
                    tint(pass, layer.colour);
                    mat.passes.push(pass);
                    out.slots.push(layer.slot);
                    counts.layers++;
                }
                if (part.lighting && plan.ambient !== null && plan.shadow !== null) {
                    const a = decode(plan.ambient);
                    const s = decode(plan.shadow);
                    // (an interior technique, or a real ambient map with the shadow map's stand-in - the factory's
                    // floors, a plain technique: under the sun they came out white - lit by the ambient map alone,
                    // interiorLighting)
                    const standIn = (d: mats.Decoded | null): boolean => d !== null && d.width * d.height <= STAND_IN;
                    const inside =
                        (part.techniques ? mats.interior(m, part.techniques) : false) || (standIn(s) && !standIn(a));
                    if (a && s) {
                        out.light = { ambient: a, shadow: s, lighting: inside ? interiorLighting() : part.lighting };
                    }
                }
                plans.set(m.checksum, out);
            }
            materials.set(m.checksum, mat);
        }
        const bySector = new Map(g.sectors.map((s) => [s.checksum, s]));
        const masters = new Set([...part.placed.values()].map((p) => p.master));
        const emit = (checksumOut: number, s: geom.GeomSector, at: Placement | null): void => {
            const move = part.local?.has(checksumOut) ? [0, 0, 0] : shift;
            const positions: number[][] = [];
            const colors: number[][] = [];
            const uvs: number[][] = [];
            const meshes: { flags: number; material: number; strip: number[] }[] = [];
            // (one uv set per pass: the sector's vertices carry as many as its materials' most)
            const sets = Math.max(1, ...s.meshes.map((m) => plans.get(m.material)?.slots.length ?? 1));
            for (const m of s.meshes) {
                if (!materials.has(m.material)) {
                    counts.skipped++;
                    continue;
                }
                if (billboards.has(m.material)) {
                    // (every corner at the pivot, spread by THP8's billboard shader to face the camera: left out, as
                    // THAW's are)
                    counts.billboards++;
                    continue;
                }
                const base = positions.length;
                const blend = transparent.has(m.material);
                const glow = glows.get(m.material);
                const dome = part.dome && !materials.get(m.material)!.passes[0]!.texture;
                const plan = plans.get(m.material);
                const verts = geom.readVertices(m);
                let meshTris = geom.triangles(m);
                // (light maps: the mesh's last two uv fields - a mesh with fewer fields than its material's first isn't
                // lit)
                const lit = plan?.light && part.lighting && (verts[0]?.uvs.length ?? 0) >= 3 ? plan.light : null;
                if (lit && part.lighting) {
                    counts.refinedFrom += meshTris.length;
                    meshTris = refineLit(verts, meshTris, lit, lit.lighting, refineOptions);
                    counts.refined += meshTris.length;
                }
                for (const v of verts) {
                    const q = place(v.position, at);
                    positions.push([q[0] * unit + move[0]!, q[1] * unit + move[1]!, q[2] * unit + move[2]!]);
                    if (dome) {
                        colors.push(skyColour(v.position));
                        uvs.push(Array<number>(2 * sets).fill(0));
                        continue;
                    }
                    // (255 = full as THUG2's 128; opaque materials' vertex alpha - THP8's layer blend weights - full)
                    const [b, g2, r, a] = v.color as [number, number, number, number];
                    // (the sky's cloud layers faded out towards the horizon: THP8's city and haze hide their low
                    // edges, which in THUG2's small sky stood up as grey spikes)
                    const fade = part.dome ? cloudFade(v.position) : 1;
                    let k = glow ?? [1, 1, 1];
                    const uvOf = (slot: number): number[] => {
                        const uv = v.uvs[slot] ?? [0, 0];
                        return uv.map((x) => (Number.isFinite(x) ? x : 0));
                    };
                    const n = v.uvs.length;
                    if (lit && part.lighting) {
                        // THP8's light maps baked into the vertex colour
                        const am = lit.ambient;
                        const sh = lit.shadow;
                        const [au, av] = uvOf(n - 2) as [number, number];
                        const [su, sv] = uvOf(n - 1) as [number, number];
                        k = mats.light(mats.sample(am, au, av), mats.sample(sh, su, sv), lit.lighting);
                        counts.lit++;
                    }
                    let alpha = blend ? Math.round((a >> 1) * fade) : 128;
                    if (plan?.vertexMask) {
                        const [mu, mv] = uvOf(plan.vertexMask.slot) as [number, number];
                        const s2 = mats.sample(plan.vertexMask.mask, mu, mv);
                        alpha = Math.round(((s2[0]! + s2[1]! + s2[2]!) / 3) * plan.vertexMask.opacity * 128);
                    }
                    colors.push([
                        Math.min(255, Math.round((b >> 1) * k[2]!)),
                        Math.min(255, Math.round((g2 >> 1) * k[1]!)),
                        Math.min(255, Math.round((r >> 1) * k[0]!)),
                        alpha,
                    ]);
                    const flat: number[] = [];
                    const slots = plan?.slots ?? [0];
                    for (let set = 0; set < sets; set++) flat.push(...uvOf(slots[set] ?? slots[0]!));
                    uvs.push(flat);
                }
                let tris = meshTris.map((t) => t.map((i) => i + base));
                if (part.dome && !dome) {
                    // (the sky's cloud layers reach below the horizon, where THP8's city hides them - THUG2's sky
                    // turns with the camera, and looking down it showed them as sheets: their triangles wholly
                    // below the horizon left out, the rest faded out towards it - cutting at the triangles' middles
                    // had left the dome showing through as grey spikes)
                    tris = tris.filter((t) => t.some((i) => positions[i]![1]! > 0));
                }
                counts.triangles += tris.length;
                if (tris.length) meshes.push({ flags: 0, material: m.material, strip: trisToStrip(tris) });
            }
            if (!meshes.length) return;
            const [lo, hi, center, radius] = geometry.bounds(positions);
            sectors.push({
                checksum: checksumOut,
                bone: -1,
                flags: SECTOR_FLAGS,
                bbox: [...lo, ...hi],
                sphere: [center[0]!, center[1]!, center[2]!, radius],
                positions,
                colors,
                uvSets: sets,
                uvs,
                meshes,
            });
            written.add(checksumOut);
        };
        for (const s of g.sectors) {
            if (masters.has(s.checksum)) {
                counts.masters++;
                continue;
            }
            if (part.hidden?.has(s.checksum)) {
                counts.hidden++;
                continue;
            }
            emit(s.checksum, s, null);
            counts.sectors++;
        }
        for (const [name, p] of part.placed) {
            const master = bySector.get(p.master);
            if (master === undefined) {
                warnings.push(`instance ${thp8.hex(name)}: master ${thp8.hex(p.master)} not in the scene`);
                continue;
            }
            emit(name, master, p.at);
            counts.instances++;
        }
    }
    const scene = geometry.convertScene({ sectors }, warnings, [...materials.values()]);
    // (meshes of more than 65,535 vertices can't be: split long sectors? - THP8's meshes are far smaller)
    return {
        scene,
        textures: { version: 1, textures: [...texOut.values()] },
        sectors: written,
        counts,
    };
}

// Lit triangles split where the light maps vary inside them (materials.refine), so shadows keep their shape in the
// vertex colours.
export const REFINE: mats.RefineOptions = { threshold: 0.2, minLength: 6, depth: 3, maxVertices: 60000 };

export function refineLit(
    verts: geom.Vertex[],
    tris: number[][],
    maps: { ambient: mats.Decoded; shadow: mats.Decoded },
    lighting: mats.Lighting,
    options: mats.RefineOptions = REFINE,
): number[][] {
    const n = verts[0]!.uvs.length;
    const lerp = (a: readonly number[], b: readonly number[], c: readonly number[], wa: number, wb: number): number[] =>
        a.map((x, k) => x * wa + b[k]! * wb + c[k]! * (1 - wa - wb));
    const lum = (ambientUv: readonly number[], shadowUv: readonly number[]): number => {
        const l = mats.light(
            mats.sample(maps.ambient, ambientUv[0]!, ambientUv[1]!),
            mats.sample(maps.shadow, shadowUv[0]!, shadowUv[1]!),
            lighting,
        );
        return 0.299 * l[0]! + 0.587 * l[1]! + 0.114 * l[2]!;
    };
    return mats.refine(
        verts,
        tris,
        (a, b) => ({
            position: lerp(a.position, b.position, b.position, 0.5, 0.5),
            color: lerp(a.color, b.color, b.color, 0.5, 0.5).map(Math.round),
            uvs: a.uvs.map((uv, k) => lerp(uv, b.uvs[k]!, b.uvs[k]!, 0.5, 0.5)),
        }),
        (v) => lum(v.uvs[n - 2]!, v.uvs[n - 1]!),
        (a, b, c, wa, wb) =>
            lum(
                lerp(a.uvs[n - 2]!, b.uvs[n - 2]!, c.uvs[n - 2]!, wa, wb),
                lerp(a.uvs[n - 1]!, b.uvs[n - 1]!, c.uvs[n - 1]!, wa, wb),
            ),
        (a, b) =>
            Math.hypot(a.uvs[n - 1]![0]! - b.uvs[n - 1]![0]!, a.uvs[n - 1]![1]! - b.uvs[n - 1]![1]!) *
            Math.max(maps.shadow.width, maps.shadow.height),
        options,
    );
}

// --- game objects: THP8's physics props ------------------------------------------------------------------------------

// THP8's props (GameObject nodes of Type GO_Cafe_Chair, GO_TrafficCone, GO_Aluminum_Can...: Havok rigid bodies its
// profiles bounce) have their model under the Type's name - most in ZONES\global.pak, the rest in the zone's archive:
// .mdl (the scene layout), .stex / .tex and a Havok packfile (their physics shape). THUG2 has the same kind of thing
// as a LevelObject with `bouncy` (THAW's bouncy boxes): its sector and collision in their own space, rigid-body
// contacts and coefficients.
// Each prop made one: the model's sector drawn under the node's name, a box of the model's size as its collision, the
// box's corners its contacts, THAW's bouncy boxes' coefficients (THP8's profiles - mass, friction in qb.pak's
// profile_GO_* structures - not read).
export interface PropModel {
    geom: geom.Geom;
    textures: Map<number, xenos.XenosTexture>;
    box: number[]; // the model's box, metres (min x y z, max x y z)
}

export function propModels(pak: thp8.Pak): Map<number, PropModel> {
    const out = new Map<number, PropModel>();
    for (const e of pak.entries) {
        if (e.extension !== ".mdl") continue;
        // (its textures: the entry the model names - a .stex in a zone's archive, a .tex in global.pak)
        const stex = pak.entries.find((x) => (x.extension === ".stex" || x.extension === ".tex") && x.full === e.own);
        const g = geom.readGeom(thp8.entryData(pak, e), "mdl");
        if (!g.sectors.length) continue;
        const textures = new Map<number, xenos.XenosTexture>();
        if (stex) for (const t of xenos.readXenosTex(thp8.entryData(pak, stex), "stex")) textures.set(t.checksum, t);
        const box = [0, 1, 2, 3, 4, 5].map((k) =>
            k < 3 ? Math.min(...g.sectors.map((x) => x.bbox[k]!)) : Math.max(...g.sectors.map((x) => x.bbox[k]!)),
        );
        out.set(e.name, { geom: g, textures, box });
    }
    return out;
}

export const BOUNCY_KEYS: readonly [string, QbValue][] = [
    ["coeff_restitution", new Float(0.4)],
    ["coeff_friction", new Float(0.5)],
    ["skater_collision_impulse_factor", new Float(1.0)],
    ["skater_collision_rotation_factor", new Float(1.0)],
    ["skater_collision_assent", new Float(25.0)],
    ["skater_collision_radius", new Float(48.0)],
];

// The props made bouncy LevelObjects (in place): node name -> its model's Type. Only the ones made at the start.
export function bouncyProps(
    array: readonly QbValue[],
    names: Map<number, string>,
    models: ReadonlyMap<number, PropModel>,
): Map<number, number> {
    const out = new Map<number, number>();
    const key = (text: string): Name => {
        const v = checksum(text);
        if (!names.has(v)) names.set(v, text);
        return new Name(v);
    };
    for (const node of array) {
        if (!(node instanceof Struct) || nodes.classOf(node, names) !== "gameobject") continue;
        const type = node.get("Type");
        const name = nodeName(node);
        // (THP8's own Bouncy ones; not the scripted ones - the Capitol's pendulum swings on its script and fell flat)
        if (!(type instanceof Name) || name === null || !node.hasFlag("CreatedAtStart") || !node.hasFlag("Bouncy"))
            continue;
        if (node.get("TriggerScript") !== undefined) continue;
        const model = models.get(type.value);
        if (!/^go_/i.test(names.get(type.value) ?? "") || model === undefined) continue;
        const [x0, y0, z0, x1, y1, z1] = model.box.map((v) => v * SCALE) as [
            number,
            number,
            number,
            number,
            number,
            number,
        ];
        const contacts = [x0, x1].flatMap((x) =>
            [y0, y1].flatMap((y) => [z0, z1].map((z) => new Vector("vector", [x, y, z]))),
        );
        node.entries = [
            [key("Pos"), node.get("Pos") ?? new Vector("vector", [0, 0, 0])],
            [key("Angles"), node.get("Angles") ?? new Vector("vector", [0, 0, 0])],
            [key("Name"), new Name(name)],
            [key("Class"), key("LevelObject")],
            [key("Type"), key("Bouncy_Default")],
            [null, key("CreatedAtStart")],
            [null, key("bouncy")],
            [key("CollisionMode"), key("Geometry")],
            [key("SuspendDistance"), 0],
            [key("lod_dist1"), 300],
            [key("lod_dist2"), 400],
            [key("contacts"), contacts],
            [key("center_of_mass"), new Vector("vector", [0, 0, 0])],
            ...BOUNCY_KEYS.map(([k, v]): qb.Entry => [key(k), v]),
        ];
        out.set(name, type.value);
    }
    return out;
}

// A box's twelve triangles (metres), as collision owned by `owner`.
export function boxTriangles(box: readonly number[], owner: number): havok.HkTriangle[] {
    const [x0, y0, z0, x1, y1, z1] = box as [number, number, number, number, number, number];
    const p = (i: number): number[] => [i & 1 ? x1 : x0, i & 2 ? y1 : y0, i & 4 ? z1 : z0];
    const quads = [
        [0, 2, 3, 1],
        [4, 5, 7, 6],
        [0, 1, 5, 4],
        [2, 6, 7, 3],
        [0, 4, 6, 2],
        [1, 3, 7, 5],
    ];
    return quads.flatMap(([a, b, c, d]) => [
        { a: p(a!), b: p(b!), c: p(c!), material: 0, owner },
        { a: p(a!), b: p(c!), c: p(d!), material: 0, owner },
    ]);
}

// The most triangles a zone's scene may have after refining: half as many again as THP8's or REFINE_EXTRA more, at most
// MAX_TRIANGLES
// (the largest zones as THP8 has them - the Capitol, 178,000 - load in THUG2 without a pool running dry; more wasn't
// tried).
export const MAX_TRIANGLES = 190000;
export const REFINE_EXTRA = 40000;
export function refineBudget(counts: ConvertedScene["counts"]): number {
    const before = counts.triangles - counts.refined + counts.refinedFrom;
    return Math.max(before, Math.min(MAX_TRIANGLES, before + Math.max(REFINE_EXTRA, Math.round(before * 0.5))));
}

// --- collision ----------------------------------------------------------------------------------------------------

// THP8's face flags that mean the same in THUG2 (skatable, not skatable, wallridable, vert, not collidable, decal,
// trigger, camera passes, no skater shadow, skater shadow, no shadow wall, under ok, invisible).
export const FACE_KEEP = 0x1fff;

// THP8's trigger volumes (the Capitol's lake kill, its eternal flame) are faces marked 0x20 and non-collidable (0x10),
// no 0x40: THUG2's trigger is 0x40 - given them (convertCol keeps trigger faces only on the objects whose node's
// script is brought along or kills).
export const FACE_VOLUME = 0x20;
export const VOLUME_DROP = 10; // metres
export function faceFlags(material: number): number {
    let f = material & FACE_KEEP;
    if (f & FACE_VOLUME && f & thug2Col.FACE_NON_COLLIDABLE) f = (f & ~FACE_VOLUME) | 0x40;
    return f;
}

// A LevelGeometry node with a TriggerScript and no scene sector (a collision-only volume): its name and Pos (metres).
export interface Volume {
    name: number;
    pos: readonly number[];
}

export function triggerVolumes(
    array: readonly QbValue[],
    names: ReadonlyMap<number, string>,
    sectors: ReadonlySet<number>,
): Volume[] {
    const out: Volume[] = [];
    for (const n of array) {
        if (!(n instanceof Struct) || nodes.classOf(n, names) !== "levelgeometry") continue;
        const name = nodeName(n);
        const pos = vec(n.get("Pos"));
        if (name === null || pos === null || sectors.has(name) || !(n.get("TriggerScript") instanceof Name)) continue;
        out.push({ name, pos });
    }
    return out;
}

export interface CollisionResult {
    col: thug2Col.ColFile;
    objects: Map<number, number>; // checksum -> faces
    named: number; // bodies named after their scene sector / node
    hidden: number; // bodies of nodes not made, left out
}

const boxOf = (tris: readonly havok.HkTriangle[]): number[] => {
    const lo = [Infinity, Infinity, Infinity];
    const hi = [-Infinity, -Infinity, -Infinity];
    for (const t of tris) {
        for (const p of [t.a, t.b, t.c]) {
            for (let k = 0; k < 3; k++) {
                lo[k] = Math.min(lo[k]!, p[k]!);
                hi[k] = Math.max(hi[k]!, p[k]!);
            }
        }
    }
    return [...lo, ...hi];
};

// The bodies as THUG2 collision objects, each named after the scene sector its box matches (THP8's bodies are one
// per collidable object, in world space, unnamed - the boxes agree to a few millimetres), else after its first
// primitive's node, else <level>_Col_<n>. Faces: THP8's flags (FACE_KEEP; trigger faces only on `keepTriggers`'
// objects - THUG2 runs a trigger face's node's script without checking it's there), terrain by thug2Terrain.
export function convertCollision(
    sets: readonly {
        bodies: havok.HkBody[];
        sectors: readonly geom.GeomSector[];
        hidden?: ReadonlySet<number>;
        local?: ReadonlySet<number>; // bodies in their own space (localObjects): not moved
        volumes?: readonly Volume[]; // collision-only trigger nodes (triggerVolumes)
    }[],
    level: string,
    shift: readonly number[],
    terrains: readonly string[],
    keepTriggers: (checksum: number) => boolean,
    warnings: string[],
): CollisionResult {
    const objects: geometry.SourceColObject[] = [];
    const used = new Set<number>();
    let named = 0;
    let n = 0;
    let hiddenBodies = 0;
    for (const { bodies: whole, sectors, hidden, local, volumes } of sets) {
        // (a body holding trigger-volume faces and others - the Capitol's lake kill inside its reflection's body - as
        // two: the volume's faces their own object, named after the volume's node)
        const isVolume = (t: havok.HkTriangle): boolean =>
            (t.material & FACE_VOLUME) !== 0 && (t.material & thug2Col.FACE_NON_COLLIDABLE) !== 0;
        const bodies = volumes?.length
            ? whole.flatMap((b) => {
                  const v = b.triangles.filter(isVolume);
                  if (!v.length || v.length === b.triangles.length) return [b];
                  return [
                      { index: b.index, triangles: b.triangles.filter((t) => !isVolume(t)) },
                      { index: b.index, triangles: v },
                  ];
              })
            : whole;
        for (const body of bodies) {
            if (!body.triangles.length) continue;
            const box = boxOf(body.triangles);
            const size = Math.max(0.1, ...[0, 1, 2].map((k) => box[3 + k]! - box[k]!));
            let best: number | null = null;
            let bestD = Infinity;
            for (const s of sectors) {
                const d = [0, 1, 2, 3, 4, 5].reduce((acc, k) => acc + Math.abs(s.bbox[k]! - box[k]!), 0);
                if (d < bestD) {
                    bestD = d;
                    best = s.checksum;
                }
            }
            let name: number;
            if (best !== null && bestD < 0.02 * size + 0.05 && !used.has(best)) {
                name = best;
                named++;
            } else {
                const owner = body.triangles.find((t) => t.owner)?.owner;
                // (a collision-only trigger node - THP8's water kills - whose Pos is inside the body's box)
                // (the one nearest the box's middle: goal triggers stand inside the big boxes too)
                const middle = [0, 1, 2].map((k) => (box[k]! + box[3 + k]!) / 2);
                const inside = (volumes ?? []).filter(
                    (v) =>
                        !used.has(v.name) &&
                        // (x / z inside; y within VOLUME_DROP: a water kill's faces are its surface, its node lower)
                        [0, 1, 2].every((k) => {
                            const slack = k === 1 ? VOLUME_DROP : 0.5;
                            return v.pos[k]! >= box[k]! - slack && v.pos[k]! <= box[3 + k]! + slack;
                        }),
                );
                const volume = inside.length
                    ? pyMinBy(inside, (v) => Math.hypot(...[0, 1, 2].map((k) => v.pos[k]! - middle[k]!)))
                    : undefined;
                if (owner !== undefined && !used.has(owner)) name = owner;
                else if (volume !== undefined) name = volume.name;
                else name = checksum(`${level}_Col_${n}`);
                if ((owner !== undefined && name === owner) || volume?.name === name) named++;
            }
            n++;
            if (hidden?.has(name)) {
                hiddenBodies++;
                continue;
            }
            used.add(name);
            const move = local?.has(name) ? [0, 0, 0] : shift;
            const verts: number[][] = [];
            const index = new Map<string, number>();
            const vertex = (p: readonly number[]): number => {
                const q = toThug(p, move);
                const key = q.map((x) => x.toFixed(3)).join(",");
                let i = index.get(key);
                if (i === undefined) {
                    i = verts.length;
                    verts.push(q);
                    index.set(key, i);
                }
                return i;
            };
            const faces: thug2Col.Face[] = [];
            for (const t of body.triangles) {
                const a = vertex(t.a);
                const b = vertex(t.b);
                const c = vertex(t.c);
                if (a === b || b === c || a === c) continue;
                faces.push([faceFlags(t.material), thug2Terrain(terrains, (t.material >>> 16) & 0xff), a, b, c]);
            }
            if (!faces.length) continue;
            if (faces.length > 0xffff || verts.length > 0xffff) {
                warnings.push(`collision body ${body.index}: ${faces.length} faces - left out`);
                continue;
            }
            const lo = [0, 1, 2].map((k) => Math.min(...verts.map((v) => v[k]!)));
            const hi = [0, 1, 2].map((k) => Math.max(...verts.map((v) => v[k]!)));
            objects.push({
                checksum: name,
                flags: 0,
                verts,
                intensities: Buffer.alloc(verts.length, 128),
                faces,
                bbox: [lo[0]!, lo[1]!, lo[2]!, 1.0, hi[0]!, hi[1]!, hi[2]!, 1.0],
            });
        }
    }
    const col = geometry.convertCol({ objects }, keepTriggers, warnings, (f) => f);
    return { col, objects: new Map(objects.map((o) => [o.checksum, o.faces.length])), named, hidden: hiddenBodies };
}

// The x / z shift (inches, whole sixteenths) that puts the middle of the collision at the origin.
export function centreShift(bodies: readonly havok.HkBody[]): number[] {
    const box = boxOf(bodies.flatMap((b) => b.triangles));
    if (!Number.isFinite(box[0]!)) return [0, 0, 0];
    const mid = (k: number): number => -Math.round(((box[k]! + box[3 + k]!) / 2) * SCALE * 16) / 16;
    return [mid(0), 0, mid(2)];
}

// --- the node array ---------------------------------------------------------------------------------------------

export interface NodeResult {
    text: string;
    stats: Map<string, number>;
    kills: number;
    restarts: number;
    rails: number;
    triggers: Set<number>;
    added: boolean; // a restart made (the zone had none)
    scripts: Map<number, Buffer>; // the scripts brought along (THUG2 bytes, appended to the compiled .qb)
    scriptReport: string;
}

// The highest collision face under (x, z) (metres), or null.
export function floorAt(bodies: readonly havok.HkBody[], x: number, z: number): number | null {
    let best: number | null = null;
    for (const b of bodies) {
        for (const t of b.triangles) {
            if (t.material & thug2Col.FACE_NON_COLLIDABLE) continue;
            const [a, p, c] = [t.a, t.b, t.c];
            const d = (p[0]! - a[0]!) * (c[2]! - a[2]!) - (c[0]! - a[0]!) * (p[2]! - a[2]!);
            if (Math.abs(d) < 1e-9) continue;
            const u = ((x - a[0]!) * (c[2]! - a[2]!) - (c[0]! - a[0]!) * (z - a[2]!)) / d;
            const v = ((p[0]! - a[0]!) * (z - a[2]!) - (x - a[0]!) * (p[2]! - a[2]!)) / d;
            if (u < 0 || v < 0 || u + v > 1) continue;
            const y = a[1]! + u * (p[1]! - a[1]!) + v * (c[1]! - a[1]!);
            if (best === null || y > best) best = y;
        }
    }
    return best;
}

// A Player1 restart for a zone that has none (THP8's skate shop: a menu scene), over the floor nearest the middle of
// its collision.
export function addRestart(
    converted: Struct[],
    bodies: readonly havok.HkBody[],
    level: string,
    shift: readonly number[],
    names: Map<number, string>,
): boolean {
    if (converted.some((n) => nodes.classOf(n, names) === "restart")) return false;
    const box = boxOf(bodies.flatMap((b) => b.triangles));
    if (!Number.isFinite(box[0]!)) return false;
    const [cx, cz] = [(box[0]! + box[3]!) / 2, (box[2]! + box[5]!) / 2];
    let at: number[] | null = null;
    for (let r = 0; r < 20 && at === null; r++) {
        for (const [dx, dz] of [
            [0, 0],
            [1, 0],
            [-1, 0],
            [0, 1],
            [0, -1],
        ] as const) {
            const x = cx + dx * r * 0.25;
            const z = cz + dz * r * 0.25;
            const y = floorAt(bodies, x, z);
            if (y !== null) {
                at = [x, y + 0.05, z];
                break;
            }
        }
    }
    if (at === null) return false;
    const n = (text: string): Name => {
        const v = checksum(text);
        if (!names.has(v)) names.set(v, text);
        return new Name(v);
    };
    converted.unshift(
        new Struct([
            [n("Pos"), new Vector("vector", toThug(at, shift))],
            [n("Angles"), new Vector("vector", [0, 0, 0])],
            [n("Name"), n(`${level}_TRG_Restart_Start`)],
            [n("Class"), n("Restart")],
            [n("Type"), n("Player1")],
            [null, n("CreatedAtStart")],
            [n("restart_types"), [n("Player1"), n("MultiPlayer"), n("Horse")]],
        ]),
    );
    // (the Links are node indices: every one moves up by the new first node)
    const LINKS = checksum("Links");
    for (const node of converted.slice(1)) {
        node.entries = node.entries.map(([k, v]) =>
            k !== null && k.value === LINKS && Array.isArray(v)
                ? [k, v.map((i) => (typeof i === "number" ? i + 1 : i))]
                : [k, v],
        );
    }
    return true;
}

// THP8's zones have one or two single-player restarts each and about ten network spawns (Multiplayer); THUG2's
// restart choices (free skate's restart list, the F8 restart tester) go by Player1 - the spawns get Player1 too.
// Returns [text, restarts changed].
export function soloRestarts(text: string): [string, number] {
    let changed = 0;
    const out = text.replace(/\{[^{}]*\}/g, (node) => {
        if (!/\bClass=Restart\b/i.test(node)) return node;
        return node.replace(/\brestart_types=\[([^\]]*)\]/, (whole, types: string) => {
            const listed = types.split(/\s+/).filter((x) => x);
            if (!listed.some((t) => /^multiplayer$/i.test(t)) || listed.some((t) => /^player1$/i.test(t))) return whole;
            changed++;
            return `restart_types=[${["Player1", ...listed].join(" ")}]`;
        });
    });
    return [out, changed];
}

// What bringing a zone's scripts along needs (tools/thaw_levels/thaw_scripts.ts, which reads THP8's scripts too: the
// same token stream, big-endian inline structures): THUG2's commands and assets.
export interface ScriptPort {
    thug2: scriptPort.Thug2;
    assets: pedsCars.Thug2Assets | null;
    particles: Map<string, string>;
    text: ReadonlyMap<number, string>; // englishText
}

export function makeScriptPort(thug2: string, compressed: string, names: Map<number, string>): ScriptPort {
    return {
        thug2: new scriptPort.Thug2(thug2),
        assets: new pedsCars.Thug2Assets(thug2),
        particles: thawScripts.particleTextures(thug2),
        text: englishText(compressed, names),
    };
}

// THP8's English text: qb.pak's globals whose value is a (wide) string - the gaps' names (`str_z_cretepark_scripts_2 =
// "Over the Steps"`), which THP8's scripts name instead of giving the text (EndGap text=str_z_cretepark_scripts_2).
// The other languages' are in the _f / _g / _i / _s paks.
export function englishText(compressed: string, names: Map<number, string>): Map<number, string> {
    const pak = thp8.openPak(findCaseless(compressed, "PAK", "qb.pak.xen")!);
    const out = new Map<number, string>();
    for (const e of pak.entries) {
        if (e.extension !== ".qb") continue;
        const q = new thaw.QbItems(thp8.entryData(pak, e), "qb.pak", names, ">");
        for (const [k, v] of q.globals) {
            if (v instanceof qb.Local) out.set(k.value, v.text);
            else if (typeof v === "string") out.set(k.value, v);
        }
    }
    return out;
}

// A THP8 qb file's scripts for the translator, the names of English text in them as the text itself (THUG2's
// commands take the string: a name there was no text at all).
export function scriptSource(
    q: thaw.QbItems,
    names: Map<number, string>,
    text: ReadonlyMap<number, string>,
): thawScripts.ThawSource {
    const source = new thawScripts.ThawSource(q, thawScripts.LEVEL, names);
    for (const [name, toks] of source.scripts) source.scripts.set(name, flattenArgs(toks));
    for (const toks of source.scripts.values()) {
        toks.forEach((t, k) => {
            const s = t.t === 0x16 ? text.get(t.p.readUInt32LE(0)) : undefined;
            if (s === undefined) return;
            const latin = s.replace(/[^ -ÿ]/g, "");
            toks[k] = new scriptPort.Tok(0x1b, Buffer.concat([Buffer.from(latin, "latin1"), Buffer.from([0])]), t.i);
        });
    }
    return source;
}

// THP8 writes some calls' arguments as a structure over several lines (funpark_RollerCoasterMovement:
// `RunScriptOnObject {` / `id=...` / `funpark_RollerCoasterBonker` / `}`) - THUG2 takes the structure as one unnamed
// argument (RunScriptOnObject then ran a script that wasn't there: the fun park crashed). As plain arguments: a
// command followed straight by `{` has the braces and the line ends inside them dropped.
export function flattenArgs(toks: readonly scriptPort.Tok[]): scriptPort.Tok[] {
    const out: scriptPort.Tok[] = [];
    for (let k = 0; k < toks.length; k++) {
        const t = toks[k]!;
        out.push(t);
        const lineStart = k === 0 || toks[k - 1]!.t === 0x01 || toks[k - 1]!.t === 0x02;
        if (!(lineStart && t.t === 0x16 && toks[k + 1]?.t === 0x03)) continue;
        let depth = 0;
        let end = -1;
        for (let j = k + 1; j < toks.length; j++) {
            if (toks[j]!.t === 0x03 || toks[j]!.t === 0x05) depth++;
            else if (toks[j]!.t === 0x04 || toks[j]!.t === 0x06) depth--;
            if (depth === 0) {
                end = j;
                break;
            }
        }
        if (end < 0 || toks[end]!.t !== 0x04) continue;
        for (let j = k + 2; j < end; j++) {
            const x = toks[j]!;
            if ((x.t === 0x01 || x.t === 0x02) && !x.targets) continue;
            out.push(x);
        }
        k = end;
    }
    return out;
}

// A brought script that would hang THUG2: an endless `while` (no count after its `repeat`) with no wait that can run -
// none at all, or only under conditions the translator made never true (`if GotParam thps3_port_never`: THP8's
// IsCreated / goal checks THUG2 can't ask). The slums' Z_Ghetto_Pipe_Box_Launch loops over its boxes that way, the
// wait inside the never-true `if`: the game hung loading. Waits: any command whose name has "wait" or "block" in it.
export const NEVER = checksum("thps3_port_never");
export function endlessLoop(code: Uint8Array, names: ReadonlyMap<number, string>): boolean {
    const toks = [...qb.tokens(Buffer.concat([code, Buffer.from([0])]), true)].map(([, t, p]) => [t, p] as const);
    const name = (k: number): number | null => (toks[k]?.[0] === 0x16 ? toks[k]![1].readUInt32LE(0) : null);
    const waits = (k: number): boolean => {
        const n = name(k);
        return n !== null && /wait|block/i.test(names.get(n) ?? "");
    };
    // per token: inside a never-true branch?
    const never: boolean[] = [];
    const ifs: boolean[] = []; // each open if: its branch never runs
    for (let k = 0; k < toks.length; k++) {
        const t = toks[k]![0];
        if (t === 0x47 || t === 0x25) {
            let n = false;
            for (let j = k + 1; j < toks.length && toks[j]![0] !== 0x01 && toks[j]![0] !== 0x02; j++) {
                if (name(j) === NEVER) n = true;
            }
            ifs.push(n);
        } else if (t === 0x48 || t === 0x26) {
            if (ifs.length) ifs[ifs.length - 1] = false;
        } else if (t === 0x28) {
            ifs.pop();
        }
        never.push(ifs.some((x) => x));
    }
    const loops: number[] = [];
    for (let k = 0; k < toks.length; k++) {
        const t = toks[k]![0];
        if (t === 0x20) loops.push(k);
        else if (t === 0x21 && loops.length) {
            const start = loops.pop()!;
            const next = toks[k + 1]?.[0];
            const counted = next !== undefined && next !== 0x01 && next !== 0x02 && next !== 0x24 && next !== 0x00;
            if (counted || never[start]) continue;
            let wait = false;
            for (let j = start + 1; j < k && !wait; j++) if (!never[j] && waits(j)) wait = true;
            if (!wait) return true;
        }
    }
    return false;
}

// RunScriptOnObject naming a script that isn't there (the fun park's coaster: funpark_RollerCoasterBonker, a THP8 game
// script not brought) crashed THUG2 (ParseArguments on nothing). Such a statement's command is made GotParam - the same
// length in the bytes (no jump moves), a question with no effect. Returns the code (patched in place) and how many.
export const RUN_ON_OBJECT = checksum("RunScriptOnObject");
export function unrunnable(code: Buffer, available: (script: number) => boolean): number {
    const toks = [...qb.tokens(Buffer.concat([code, Buffer.from([0])]), true)];
    let patched = 0;
    for (let k = 0; k < toks.length; k++) {
        const [off, t, p] = toks[k]!;
        const lineStart = k === 0 || toks[k - 1]![1] === 0x01 || toks[k - 1]![1] === 0x02;
        if (!lineStart || t !== 0x16 || p.readUInt32LE(0) !== RUN_ON_OBJECT) continue;
        const scripts: number[] = [];
        for (let j = k + 1; j < toks.length && toks[j]![1] !== 0x01 && toks[j]![1] !== 0x02; j++) {
            const [, tj, pj] = toks[j]!;
            if (tj !== 0x16 || toks[j + 1]?.[1] === 0x07 || toks[j - 1]?.[1] === 0x07) continue;
            scripts.push(pj.readUInt32LE(0));
        }
        if (scripts.some(available)) continue;
        code.writeUInt32LE(checksum("GotParam"), off + 1);
        patched++;
    }
    return patched;
}

// The script emptied (its name kept: what calls it still finds it).
export function emptyScript(code: Uint8Array): Buffer {
    const data = Buffer.from(code);
    for (const [off, t, p] of qb.tokens(Buffer.concat([data, Buffer.from([0])]), true)) {
        if (t === 0x16) return Buffer.concat([data.subarray(0, off + 1 + p.length), Buffer.from([0x01, 0x24])]);
    }
    return data;
}

// THP8's kill scripts also kill through the skater object (`Skater:KillSkater node=...`, Z_Houses_2P_Kill) - the
// call's statement as nodes.killCommands sees a plain call: the member's name first, the object and colon dropped.
export function memberCalls(q: thaw.QbItems): Pick<thaw.QbItems, "scripts" | "calls"> {
    return {
        scripts: q.scripts,
        calls: (script: number) => {
            const out = q.calls(script);
            for (const line of q.scripts.get(script) ?? []) {
                if (line.length >= 3 && line[0]![0] === 0x16 && line[1]![0] === 0x42 && line[2]![0] === 0x16) {
                    out.push([line[2]![1].readUInt32LE(0), line.slice(2)]);
                }
            }
            return out;
        },
    };
}

// Positions: THP8's metres as THUG2's inches, moved by `shift` - every node's Pos (in place, before the nodes are
// made THUG2's: the made objects copy theirs).
export function movePositions(array: readonly QbValue[], shift: readonly number[]): void {
    const POS = checksum("Pos");
    for (const node of array) {
        if (!(node instanceof Struct)) continue;
        node.entries = node.entries.map(([k, v]) =>
            k !== null && k.value === POS && v instanceof Vector && v.kind === "vector"
                ? [k, new Vector("vector", toThug(v.values, shift))]
                : [k, v],
        );
    }
}

export function zoneNodes(
    nqb: thaw.QbItems,
    zone: string,
    level: string,
    names: Map<number, string>,
    shift: readonly number[],
    sectors: ReadonlySet<number>,
    scriptQbs: readonly thaw.QbItems[],
    bodies: readonly havok.HkBody[] = [],
    flags: ReadonlyMap<number, boolean> = new Map(),
    port: ScriptPort | null = null,
    moreQbs: readonly thaw.QbItems[] = [], // more scripts the zone's may call (sub-zones', other zones')
    props: ReadonlyMap<number, PropModel> = new Map(), // the zone's prop models (propModels)
): NodeResult {
    const key = checksum(`${zone}_NodeArray`);
    const raw = nqb.globals.find(([n]) => n.value === key)?.[1];
    if (!Array.isArray(raw)) pyExit(`${zone}: no ${zone}_NodeArray`);
    const globals = new Map(nqb.globals.map(([k, v]) => [k.value, v]));
    let array = nodes.expandIncludes(raw, globals);
    applyNodeFlags(array, flags);
    bouncyProps(array, names, props);
    movePositions(array, shift);
    const restartNames = new Set<number>();
    for (const n of array) {
        const v = n instanceof Struct ? nodeName(n) : null;
        if (v !== null && nodes.classOf(n as Struct, names) === "restart") restartNames.add(v);
    }
    let kills = nodes.killCommands(scriptQbs.map(memberCalls), restartNames, names);
    const original = array;
    // the scripts' objects, peds, cars (thaw_scripts.PortedNodes: THUG2's forms of THAW's classes - THP8's are the
    // same); the geometry's and rails' trigger scripts brought along (gaps)
    let made: thawScripts.PortedNodes | null = null;
    const wanted = new Set<number>();
    // THP8's Vehicle nodes are its grid pieces (veh_schoolkicker, veh_ghetto_QP...: kickers, quarter pipes and rails
    // its grid scripts place), not cars - left out (PortedNodes would park THUG2's cars there)
    const placeholder = new Struct([[new Name(checksum("Class")), new Name(checksum("LeftOut"))]]);
    let gridPieces = 0;
    array = array.map((n) => {
        if (!(n instanceof Struct) || nodes.classOf(n, names) !== "vehicle") return n;
        gridPieces++;
        return placeholder;
    });
    if (port !== null) {
        made = new thawScripts.PortedNodes(
            array,
            names,
            sectors,
            sectors,
            scriptQbs,
            port.thug2,
            port.assets,
            port.particles,
        );
        const m = made;
        array = array.map((n) => (n instanceof Struct ? (m.convert(n) ?? n) : n));
        for (const node of array) {
            const script = node instanceof Struct ? node.get("TriggerScript") : undefined;
            if (
                script instanceof Name &&
                !kills.has(script.value) &&
                ["levelgeometry", "railnode"].includes(nodes.classOf(node as Struct, names))
            ) {
                wanted.add(script.value);
            }
        }
    }
    const [converted, stats] = nodes.convertNodes(array, names, kills, ["pos"], made ? made.ids : [], wanted);
    const RESTART_NAME = checksum("RestartName");
    for (const node of converted) {
        // (THP8's RestartName is a wide local string: as THUG2's plain one - left out when empty, NeverScript takes no
        // empty string)
        node.entries = node.entries
            .filter(([k, v]) => !(k !== null && k.value === RESTART_NAME && v instanceof qb.Local && !v.text))
            .map(([k, v]) =>
                k !== null && k.value === RESTART_NAME && v instanceof qb.Local
                    ? [k, v.text.replace(/[^ -~]/g, "").replaceAll('"', "")]
                    : [k, v],
            );
    }
    // (LevelGeometry nodes whose sector isn't drawn - collision-only pieces, masters - lose TrickObject / Cluster:
    // stripMissingTrickObjects, on the text)
    const added = addRestart(converted, bodies, level, shift, names);
    kills = nodes.addKillRestarts(converted, kills, names);
    let scripts = new Map<number, Buffer>();
    let extraText = "";
    let scriptReport = "";
    const emptied: string[] = [];
    let unrun = 0;
    if (made !== null && port !== null) {
        const sources = [...scriptQbs, ...moreQbs].map((x) => scriptSource(x, names, port.text));
        const provided = new Set([
            ...kills.keys(),
            ...[thawScripts.PED_WALK, thawScripts.PED_STAND, thawScripts.CAR_DRIVE, thawScripts.RAIL_ACCELERATE].map(
                checksum,
            ),
        ]);
        const undrawn = new Set<number>();
        for (const n of converted) {
            const v = nodeName(n);
            if (v !== null && nodes.classOf(n, names) === "levelgeometry" && !sectors.has(v)) undrawn.add(v);
        }
        let globalsText: string;
        [scripts, globalsText, scriptReport] = thawScripts.portScripts(
            converted,
            original,
            made,
            sources,
            port.thug2,
            names,
            provided,
            undrawn,
        );
        // (THAW's rail speed-up, RailAccelerate: THP8's uphill rails name it too)
        extraText =
            globalsText +
            thawScripts.MADE_SCRIPTS +
            thawScripts.RAIL_SCRIPTS +
            thawScripts.loadParticleTexturesNs(made.particleTextures);
        // (THP8's own tokens - 0x4B "$", 0x4C wide strings - are made THUG2's by thaw_scripts.scriptToks: none may be
        // left in what THUG2 runs)
        for (const [script, code] of scripts) {
            if (endlessLoop(code, names)) {
                scripts.set(script, emptyScript(code));
                emptied.push(names.get(script) ?? thp8.hex(script));
                continue;
            }
            const available = (x: number): boolean => scripts.has(x) || port.thug2.scripts.has(x) || provided.has(x);
            const fixed = Buffer.from(code);
            const n = unrunnable(fixed, available);
            if (n) {
                scripts.set(script, fixed);
                unrun += n;
            }
            for (const [t] of scriptPort.plainTokens(code)) {
                if (t >= 0x4a)
                    pyExit(`${level}: script ${names.get(script) ?? thp8.hex(script)}: token 0x${t.toString(16)}`);
            }
        }
    }
    const triggers = nodes.killTriggerNames(converted, kills);
    // (the geometry whose trigger script is brought along keeps its trigger faces: gaps)
    for (const n of converted) {
        const v = nodeName(n);
        const t = n.get("TriggerScript");
        if (v !== null && t instanceof Name && nodes.classOf(n, names) === "levelgeometry" && scripts.has(t.value)) {
            triggers.add(v);
        }
    }
    let text = nodes.nodeArrayNs(`${level}_NodeArray`, converted, names, kills) + extraText;
    [text] = nodes.stripMissingTrickObjects(text, sectors);
    const count = (cls: string): number => converted.filter((n) => nodes.classOf(n, names) === cls).length;
    return {
        text,
        stats,
        kills: nodes.killTriggerNames(converted, kills).size,
        restarts: count("restart"),
        rails: count("railnode"),
        triggers,
        added,
        scripts,
        scriptReport:
            scriptReport +
            (emptied.length ? `; emptied (endless loops without a wait): ${emptied.join(", ")}` : "") +
            (unrun ? `; ${unrun} RunScriptOnObject of scripts not there made harmless` : "") +
            (gridPieces ? `; ${gridPieces} grid pieces (Vehicle nodes) left out` : ""),
    };
}

// --- converting ---------------------------------------------------------------------------------------------------

export interface Args {
    command: "convert" | "selfcheck" | "list";
    levels: string[];
    thug2: string;
    thp8: string;
    out: string | null;
    ns: string;
    noMips: boolean;
    leaveOut: RegExp | null; // sectors (and their collision) left out by name
}

interface ZoneData {
    pak: thp8.Pak;
    geom: geom.Geom | null;
    textures: Map<number, xenos.XenosTexture>;
    bodies: havok.HkBody[];
    nqb: thaw.QbItems | null;
    qbs: thaw.QbItems[];
    subQbs: thaw.QbItems[]; // the _gfx / _sfx sub-zones' scripts
}

function readZone(file: string, names: Map<number, string>, warnings: string[]): ZoneData {
    const pak = thp8.openPak(file);
    const main = pak.entries.filter((e) => e.flags === 0);
    const one = (ext: string): thp8.PakEntry | undefined => main.find((e) => e.extension === ext);
    const g = one(".geom");
    const t = one(".tex");
    const h = one(".hkc");
    const n = one(".nqb");
    const textures = new Map<number, xenos.XenosTexture>();
    if (t) for (const x of xenos.readXenosTex(thp8.entryData(pak, t), file)) textures.set(x.checksum, x);
    return {
        pak,
        geom: g ? geom.readGeom(thp8.entryData(pak, g), file) : null,
        textures,
        bodies: h ? havok.readCollision(new havok.Packfile(thp8.entryData(pak, h), file), warnings) : [],
        nqb: n ? new thaw.QbItems(thp8.entryData(pak, n), `${file} nqb`, names, ">") : null,
        qbs: main
            .filter((e) => e.extension === ".qb")
            .map((e) => new thaw.QbItems(thp8.entryData(pak, e), `${file} qb`, names, ">")),
        subQbs: pak.entries
            .filter((e) => e.flags !== 0 && (e.extension === ".qb" || e.extension === ".nqb"))
            .map((e) => new thaw.QbItems(thp8.entryData(pak, e), `${file} sub-zone qb`, names, ">")),
    };
}

// Every zone's scripts (its own and its sub-zones'), read once: a zone's gaps can end in the next zone's scripts
// (the Capitol's boat terrace gap ends with the city park's Z_Park_CZGapBoatTerraceEnd).
export class ZoneScripts {
    private readonly cache = new Map<string, thaw.QbItems[]>();
    private readonly compressed: string;
    private readonly names: Map<number, string>;
    constructor(compressed: string, names: Map<number, string>) {
        this.compressed = compressed;
        this.names = names;
    }

    of(zone: string): thaw.QbItems[] {
        let qbs = this.cache.get(zone);
        if (qbs === undefined) {
            const file = zonePak(this.compressed, zone);
            qbs = [];
            if (file !== null) {
                const pak = thp8.openPak(file);
                for (const e of pak.entries) {
                    if (e.extension !== ".qb" && e.extension !== ".nqb") continue;
                    qbs.push(new thaw.QbItems(thp8.entryData(pak, e), `${zone} qb`, this.names, ">"));
                }
            }
            this.cache.set(zone, qbs);
        }
        return qbs;
    }
}

export function cmdConvert(args: Args): number {
    const compressed = compressedFolder(args.thp8);
    const out = args.out || pyJoin(args.thug2, STAGING);
    if (!fs.existsSync(args.ns)) pyExit(`${args.ns} not found: build it with tools\\build_neverscript.ps1`);
    const wanted: ZoneInfo[] = [];
    for (const name of args.levels.length ? args.levels : ZONES.map((z) => z.zone)) {
        const info = findZone(name);
        if (info === undefined) pyExit(`${name}: not a THP8 zone (${ZONES.map((z) => z.zone).join(", ")})`);
        wanted.push(info);
    }
    const names = thp8.loadNames(findCaseless(compressed, "PAK", "dbg.pak.xen")!);
    const terrains = terrainTable(compressed, names);
    const flags = nonStoryNodeFlags(compressed, names);
    const managers = todManagers(compressed, names);
    const library = findCaseless(compressed, "FXFILES", "MaterialLibrary.bin.xen");
    const techniques = library ? mats.readTechniques(thp8.unpack(library)) : new Map<number, string[]>();
    const port = makeScriptPort(args.thug2, compressed, names);
    const zoneScripts = new ZoneScripts(compressed, names);
    const globalPak = zonePak(compressed, "global");
    const globalProps = globalPak ? propModels(thp8.openPak(globalPak)) : new Map<number, PropModel>();
    // (only the levels converted now are made again: a partial convert keeps the others, as levels.json does -
    // ported_levels.writeManifest)
    for (const info of wanted) {
        fs.rmSync(pyJoin(out, "levels", levelName(info)), { recursive: true, force: true });
    }
    fs.mkdirSync(out, { recursive: true });
    const manifest: { levels: Record<string, unknown>[] } = { levels: [] };
    const report: string[] = [];
    const sky = convertSky(compressed, out, report, !args.noMips, args.leaveOut, names);
    for (const info of wanted) {
        const file = zonePak(compressed, info.zone);
        if (file === null) {
            report.push(`${info.zone}: not in ${compressed}\\ZONES - skipped`);
            continue;
        }
        const name = levelName(info);
        const warnings: string[] = [];
        const z = readZone(file, names, warnings);
        if (z.geom === null || z.nqb === null) {
            report.push(`${info.zone}: no scene or node array - skipped`);
            continue;
        }
        const zoneName = names.get(checksum(info.zone)) ?? info.zone;
        const onlineFile = info.online ? zonePak(compressed, info.online) : null;
        const online = onlineFile ? readZone(onlineFile, names, warnings) : null;
        const shift = centreShift(z.bodies);
        const tod = zoneTod(managers, zoneName);
        // scene
        const placed = new Map<number, { master: number; at: Placement }>();
        const key = checksum(`${zoneName}_NodeArray`);
        const rawArray = z.nqb.globals.find(([n]) => n.value === key)?.[1];
        const array = Array.isArray(rawArray)
            ? nodes.expandIncludes(rawArray, new Map(z.nqb.globals.map(([k, v]) => [k.value, v])))
            : [];
        // (the node flags first: they decide which instances and sectors are made)
        const flagged = applyNodeFlags(array, flags);
        for (const [k, v] of instances(array)) {
            const node = array.find((n) => n instanceof Struct && nodeName(n) === k) as Struct | undefined;
            if (node?.hasFlag("CreatedAtStart")) placed.set(k, v);
        }
        // (the props first: they become LevelObjects - local, drawn and collided as theirs)
        const props = new Map([...globalProps, ...propModels(z.pak)]);
        const made = bouncyProps(array, names, props);
        const hidden = notMade(array, names);
        const local = localObjects(array, names);
        if (args.leaveOut) {
            for (const s of z.geom.sectors) if (args.leaveOut.test(names.get(s.checksum) ?? "")) hidden.add(s.checksum);
        }
        // (the network barriers: collision only - THP8 draws them as a shimmering wall when the skater is near)
        const zoneGeom = z.geom;
        const scene = (options: mats.RefineOptions): ConvertedScene =>
            convertScenes(
                [
                    {
                        geom: zoneGeom,
                        textures: z.textures,
                        placed,
                        hidden,
                        additive: glowMaterials(zoneGeom, names),
                        lighting: zoneLighting(tod),
                        techniques,
                        local,
                    },
                    // (the props: their model's sector drawn under each node's name, in its own space)
                    ...[...new Set(made.values())].map(
                        (type): SceneParts => ({
                            geom: props.get(type)!.geom,
                            textures: props.get(type)!.textures,
                            placed: new Map(
                                [...made]
                                    .filter(([, t]) => t === type)
                                    .map(([node]) => [
                                        node,
                                        {
                                            master: props.get(type)!.geom.sectors[0]!.checksum,
                                            at: { pos: [0, 0, 0], angles: [0, 0, 0], scale: [1, 1, 1] },
                                        },
                                    ]),
                            ),
                            local: new Set(made.keys()),
                        }),
                    ),
                ],
                shift,
                warnings,
                !args.noMips,
                SCALE,
                options,
            );
        // (refining the light keeps to a budget: the threshold raised until the zone fits)
        let refineOptions = { ...REFINE };
        let done = scene(refineOptions);
        for (let tries = 0; tries < 4 && done.counts.triangles > refineBudget(done.counts); tries++) {
            refineOptions = { ...refineOptions, threshold: refineOptions.threshold * 1.6 };
            done = scene(refineOptions);
        }
        if (refineOptions.threshold !== REFINE.threshold) {
            report.push(
                `  light refined at a threshold of ${refineOptions.threshold.toFixed(2)} (the triangle budget)`,
            );
        }
        // nodes (the kill zones' trigger faces kept)
        const scriptQbs = [z.nqb, ...z.qbs];
        const moreQbs = [
            ...z.subQbs,
            ...ZONES.filter((x) => x.zone !== info.zone).flatMap((x) => zoneScripts.of(x.zone)),
        ];
        const nodeResult = zoneNodes(
            z.nqb,
            zoneName,
            name,
            names,
            shift,
            done.sectors,
            scriptQbs,
            z.bodies,
            flags,
            port,
            moreQbs,
            props,
        );
        // collision
        const volumes = triggerVolumes(array, names, new Set(z.geom.sectors.map((x) => x.checksum)));
        const sets = [{ bodies: z.bodies, sectors: z.geom.sectors, hidden, local, volumes }];
        // (the props' boxes, in their own space)
        sets.push({
            bodies: [...made].map(([node, type], i) => ({
                index: -1 - i,
                triangles: boxTriangles(props.get(type)!.box, node),
            })),
            sectors: [],
            hidden: new Set(),
            local: new Set(made.keys()),
            volumes: [],
        });
        if (online?.geom) {
            sets.push({
                bodies: online.bodies,
                sectors: online.geom.sectors,
                hidden: new Set(),
                local: new Set(),
                volumes: [],
            });
        }
        const col = convertCollision(sets, name, shift, terrains, (c) => nodeResult.triggers.has(c), warnings);
        const levelDir = pyJoin(out, "levels", name);
        common.writeLevel(levelDir, name, done.scene, done.textures, col.col);
        common.reportLevel(report, name, done.scene, done.textures, col.col, [...new Set(warnings)].sort());
        const c = done.counts;
        if (made.size)
            report.push(
                `  ${made.size} props as bouncy objects (${[...new Set([...made.values()].map((t) => names.get(t)))].join(", ")})`,
            );
        report.push(
            `  ${info.zone}${online ? ` + ${info.online}` : ""}: ${c.sectors} sectors, ${c.instances} instances of ` +
                `${c.masters} masters, ${c.billboards} billboard meshes left out, ` +
                `${c.triangles} triangles, ${c.layers} further layers, ${c.lit} vertices lit from light maps (${c.refined} lit triangles after refining); collision ${col.objects.size} objects (${col.named} ` +
                `named after their sector / node; ${col.hidden} of nodes not made left out); ${flagged} nodes made ` +
                `from THP8's non-story node flags, ${hidden.size} not made; moved by (${shift.map((x) => x.toFixed(1)).join(", ")})`,
        );
        const qbFile = pyJoin(levelDir, `${name}.qb`);
        // restarts: THP8's network spawns usable in single play too (soloRestarts); against the collision just made -
        // restarts over nothing / a kill zone are no network spawns, kills go to grounded ones (nodes.groundRestarts);
        // network games' needs (nodes.netGameNodes: Player1 restarts as spawns where a zone has none)
        const [soloText, solo] = soloRestarts(nodeResult.text);
        const [groundedText, grounded] = nodes.groundRestarts(soloText, col.col);
        const [text] = nodes.netGameNodes(groundedText, nodeResult.scripts);
        if (solo) report.push(`  ${solo} network restarts also single-player ones`);
        report.push(...grounded);
        common.nsCompile(args.ns, text, qbFile);
        if (nodeResult.scripts.size) {
            fs.writeFileSync(qbFile, scriptPort.appendScripts(fs.readFileSync(qbFile), nodeResult.scripts, names));
        }
        if (nodeResult.scriptReport) report.push(`  ${nodeResult.scriptReport}`);
        report.push(
            `  nodes: ${nodeResult.restarts} restarts${nodeResult.added ? " (one made: it had none)" : ""}, ` +
                `${nodeResult.rails} rail nodes, ${nodeResult.kills} kill zones ` +
                `(${[...nodeResult.stats].map(([k, v]) => `${k} ${v}`).join(", ")})`,
        );
        manifest.levels.push({
            folder: info.zone,
            level: name,
            sky,
            display: `THP8 ${info.sign}`,
            name: `${info.sign.charAt(0)}${info.sign.slice(1).toLowerCase()}`,
            game: "THP8",
            location: info.location,
            icon: "sign_training",
            level_num: info.levelNum,
            default_stats: 6,
            lighting: tod ? common.lighting(tod) : {},
        });
    }
    portedLevels.writeManifest(pyJoin(out, portedLevels.MANIFEST), manifest);
    print(report.join("\n"));
    print(`wrote ${out} (${manifest.levels.length} levels); install with tools\\menu_level\\make_menu_level.ts`);
    return 0;
}

export const SKY = "thp8_sky";
// THUG2 draws a sky around the camera, small: THAW's reaches ~720 units. THP8's dome (1,456 m) made as big.
export const SKY_UNIT = 0.5;

// A stand-in for THP8's atmosphere shader on the sky's domes: a clear sky by the vertex's height (horizon pale, zenith
// blue), B G R A with 128 = full.
export function skyColour(p: readonly number[]): number[] {
    const r = Math.hypot(p[0]!, p[1]!, p[2]!) || 1;
    const t = Math.max(0, Math.min(1, p[1]! / r));
    const mix = (a: number, b: number): number => Math.round((a + (b - a) * t ** 0.6) / 2);
    return [mix(220, 190), mix(205, 120), mix(190, 70), 128];
}

// How much of a cloud layer's vertex shows by its height over the horizon (as the sine of the angle): none at the
// horizon, all from CLOUD_FADE_TOP up, smoothly between.
export const CLOUD_FADE_TOP = 0.35;
export function cloudFade(p: readonly number[]): number {
    const r = Math.hypot(p[0]!, p[1]!, p[2]!) || 1;
    const t = Math.max(0, Math.min(1, p[1]! / r / CLOUD_FADE_TOP));
    return t * t * (3 - 2 * t);
}

// THP8's default sky (SKIES\DEFAULT_SKY\default_sky.pak.xen: a dome scene) as the levels' sky, in the same units.
export function convertSky(
    compressed: string,
    out: string,
    report: string[],
    mips: boolean,
    leaveOut: RegExp | null = null,
    names: ReadonlyMap<number, string> = new Map(),
): string | null {
    const file = findCaseless(compressed, "SKIES", "DEFAULT_SKY", "default_sky.pak.xen");
    if (file === null) {
        report.push("default_sky.pak.xen not found - no sky");
        return null;
    }
    const warnings: string[] = [];
    const pak = thp8.openPak(file);
    const g = pak.entries.find((e) => e.extension === ".geom");
    const t = pak.entries.find((e) => e.extension === ".tex");
    if (!g || !t) return null;
    const textures = new Map(xenos.readXenosTex(thp8.entryData(pak, t), file).map((x) => [x.checksum, x]));
    const sky = geom.readGeom(thp8.entryData(pak, g), file);
    const hidden = new Set(
        sky.sectors.filter((s) => leaveOut?.test(names.get(s.checksum) ?? "")).map((s) => s.checksum),
    );
    const done = convertScenes(
        [{ geom: sky, textures, placed: new Map(), dome: true, hidden }],
        [0, 0, 0],
        warnings,
        mips,
        SKY_UNIT,
    );
    common.writeLevel(pyJoin(out, "levels", SKY), SKY, done.scene, done.textures, null);
    common.reportLevel(report, SKY, done.scene, done.textures, null, [...new Set(warnings)].sort());
    return SKY;
}

// Every zone archive: its scene, textures (base levels untiled), collision and qb files read; THUG2's LA / TestLevel
// files round-trip (common.thug2RoundTrips).
export function cmdSelfcheck(args: Args): number {
    let [checked, failures] = common.thug2RoundTrips(pyJoin(args.thug2, "Data", "pre"), [
        "LAscn.prx",
        "LAcol.prx",
        "TestLevelscn.prx",
        "TestLevelcol.prx",
    ]);
    const compressed = compressedFolder(args.thp8);
    const names = thp8.loadNames(findCaseless(compressed, "PAK", "dbg.pak.xen")!);
    // every zone archive, the skies, and qb.pak (the scripts and globals)
    const zones = findCaseless(compressed, "ZONES")!;
    const files = fs
        .readdirSync(zones)
        .filter((x) => /\.pak\.xen$/i.test(x))
        .map((x) => pyJoin(zones, x));
    const skies = findCaseless(compressed, "SKIES");
    for (const d of skies ? fs.readdirSync(skies) : []) {
        for (const x of fs.readdirSync(pyJoin(skies!, d))) if (/\.pak\.xen$/i.test(x)) files.push(pyJoin(skies!, d, x));
    }
    files.push(findCaseless(compressed, "PAK", "qb.pak.xen")!);
    for (const file of files) {
        const f = file.slice(compressed.length + 1);
        const pak = thp8.openPak(file);
        for (const e of pak.entries) {
            const what = `${f} ${e.extension} ${thp8.hex(e.full)}`;
            try {
                const data = thp8.entryData(pak, e);
                if (e.extension === ".geom") {
                    const g = geom.readGeom(data, what);
                    for (const s of g.sectors) for (const m of s.meshes) geom.triangles(m);
                } else if (e.extension === ".tex") {
                    for (const t of xenos.readXenosTex(data, what)) if (t.format !== xenos.FMT_DXN) xenos.baseLevel(t);
                } else if (e.extension === ".hkc") {
                    havok.readCollision(new havok.Packfile(data, what));
                } else if (e.extension === ".qb" || e.extension === ".nqb") {
                    new thaw.QbItems(data, what, names, ">");
                } else continue;
            } catch (err) {
                if (!(err instanceof Error) || !/FormatError|RangeError/.test(err.name + err.constructor.name))
                    throw err;
                print(`FAIL ${what}: ${err.message}`);
                failures++;
                continue;
            }
            checked++;
        }
    }
    print(`${checked} files checked, ${failures} failures`);
    return failures ? 1 : 0;
}

function cmdList(args: Args): number {
    const compressed = compressedFolder(args.thp8);
    for (const info of ZONES) {
        const file = zonePak(compressed, info.zone);
        print(
            `${levelName(info).padEnd(18)} ${info.sign.padEnd(14)} ${info.zone}${file ? "" : " (missing)"}${info.online ? ` + ${info.online}` : ""}`,
        );
    }
    return 0;
}

const USAGE = `usage: thp8_levels.ts {convert,selfcheck,list} [zones ...] [--thug2 THUG2] [--thp8 THP8] [--out OUT]
                     [--ns NS] [--no-mips]

  convert     the zones (default all: ${ZONES.map((z) => z.zone).join(", ")}) into <thug2>\\${STAGING}
  selfcheck   read every zone archive's scene / textures / collision / qb files; round-trip THUG2's LA / TestLevel
  list        the zones and their levels

  --thug2     THUG2's folder (default C:\\Games\\THUG2) - the install root or its Game folder
  --thp8      THP8's folder (default C:\\Games\\THP8) - holding game\\DATA\\COMPRESSED
  --out       where convert writes (default <thug2>\\${STAGING})
  --ns        NeverScript's compiler (default out\\neverscript\\ns.exe)
  --no-mips   no mip levels (faster)
  --leave-out a regular expression: sectors whose names match are left out (scene and collision)`;

export function parseCommandLine(argv: readonly string[]): Args {
    const { values, positionals } = parseCli(USAGE, {
        args: [...argv],
        allowPositionals: true,
        options: {
            thug2: { type: "string" },
            thp8: { type: "string" },
            out: { type: "string" },
            ns: { type: "string" },
            "no-mips": { type: "boolean" },
            "leave-out": { type: "string" },
        },
    });
    const command = positionals[0];
    if (command !== "convert" && command !== "selfcheck" && command !== "list") {
        process.stderr.write(`${USAGE}\nerror: the command is convert, selfcheck or list\n`);
        pyExit(2);
    }
    return {
        command,
        levels: positionals.slice(1),
        thug2: values.thug2 ?? "C:\\Games\\THUG2",
        thp8: values.thp8 ?? "C:\\Games\\THP8",
        out: values.out ?? null,
        ns: values.ns ?? common.NS,
        noMips: values["no-mips"] ?? false,
        leaveOut: values["leave-out"] ? new RegExp(values["leave-out"], "i") : null,
    };
}

export function main(argv: readonly string[] = process.argv.slice(2)): number {
    const args = parseCommandLine(argv);
    if (args.command === "list") return cmdList(args);
    args.thug2 = resolveInstall(args.thug2, "thug2");
    if (args.command === "selfcheck") return cmdSelfcheck(args);
    return cmdConvert(args);
}

void qb;

if (isMain(import.meta.url)) await runMain(() => main());
