// NOT ORIGINAL - Tony Hawk's Project 8's levels (the Xbox 360 game's data) converted to THUG2's formats, for its level
// select's PROJECT 8 section (tools/menu_level/make_menu_level.ts). Nothing of either game is in the repo: the files
// are made from the user's own copies each time. README.md has how to run it and what is converted.
//
// THP8's world is one city streamed in zones (game\DATA\COMPRESSED\ZONES\z_<zone>.pak.xen, each with its _lod
// stand-in). THP8's own levels are made: each of its network levels (its zones merged in their shared frame, the
// other zones' _lod stand-ins around: networkAreas / convertLevel - BEAVER VALLEY is the whole town) and its
// standalone levels (training, skate shop, bedroom); the city's zones aren't levels by themselves (RETIRED). A zone's
// archive holds the zone and its _gfx / _sfx sub-zones; the main one (pak flags 0) is converted: its scene (.geom),
// textures (.tex), collision (.hkc, Havok) and node array (.nqb). The network areas' barriers (z_online_<x>.pak.xen)
// are added to the level they close off.
//
// Space: THP8's metres (y up) as THUG2's inches (x 39.37); each zone moved (x / z, whole sixteenths of an inch) so the
// middle of its collision is at the origin.

import * as fs from "node:fs";
import * as common from "../level_port/common.ts";
import * as geometry from "../level_port/geometry.ts";
import * as nodes from "../level_port/nodes.ts";
import * as overlayBias from "../level_port/overlay_bias.ts";
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
    sign: string; // the level select's sign text when THP8 names none (zoneNames: THP8's own area names win)
    suffix?: string; // added to THP8's area name: a second zone of the same area
    location: string;
    levelNum: number;
    standalone?: boolean; // a level of its own in THP8 too (not part of the city's open world)
}

// THP8's zones: the city's (its open world, streamed - parts of the network levels, not levels by themselves), then
// the separate ones that are levels of their own in THP8 too (standalone: the training area, the skate shop and the
// skater's bedroom - THP8 loads each alone: Level_Z_Training / _Skateshop / _Bedroom, none in any network level's
// spawn_in). The cartoon world is part of the city (the fun park's and the whole town's spawn_in). Level select
// numbers 110... (the standalone ones; the network levels from NET_LEVEL_NUM).
const city = (zone: string, sign: string, location: string, suffix?: string): ZoneInfo => ({
    zone,
    sign,
    location: `THP8's city: ${location}`,
    levelNum: 0,
    ...(suffix ? { suffix } : {}),
});
export const ZONES: readonly ZoneInfo[] = [
    city("z_houses", "SUBURBS", "the suburbs"),
    city("z_outschool", "SCHOOL", "the high school"),
    city("z_inschool", "SCHOOL INSIDE", "inside the high school", " INSIDE"),
    city("z_shops", "SHOPS", "the shops"),
    city("z_ghetto", "SLUMS", "the slums"),
    city("z_factory", "FACTORY", "the factory"),
    city("z_center", "CAPITOL", "the capitol"),
    city("z_citypark", "CITY PARK", "the city park"),
    city("z_cretepark", "SKATE PARK", "the skate park"),
    city("z_funpark", "FUN PARK", "the fun park"),
    city("z_parking", "PARKING", "the parking lot"),
    city("z_cartoon", "CARTOON", "the cartoon world"),
    {
        zone: "z_training",
        sign: "TRAINING",
        location: "THP8's training area",
        levelNum: 110,
        standalone: true,
    },
    {
        zone: "z_skateshop",
        sign: "SKATE SHOP",
        location: "THP8's skate shop",
        levelNum: 111,
        standalone: true,
    },
    {
        zone: "z_bedroom",
        sign: "BEDROOM",
        location: "THP8's bedroom",
        levelNum: 112,
        standalone: true,
    },
];

// THP8's English names of its city's areas (qb.pak's zone_info: {id=[z_center] Name="THE CAPITOL"} - its map's
// labels): zone -> name. The Fun Park's entry also lists the cartoon world (a world of its own here: our sign).
export function zoneNames(compressed: string, names: Map<number, string>): Map<string, string> {
    const pak = thp8.openPak(findCaseless(compressed, "PAK", "qb.pak.xen")!);
    const key = checksum("zone_info");
    const out = new Map<string, string>();
    for (const e of pak.entries) {
        if (e.extension !== ".qb") continue;
        const q = new thaw.QbItems(thp8.entryData(pak, e), "qb.pak", names, ">");
        const table = q.globals.find(([n]) => n.value === key)?.[1];
        if (!Array.isArray(table)) continue;
        for (const entry of table) {
            if (!(entry instanceof Struct)) continue;
            const ids = entry.get("id");
            const name = entry.get("Name");
            const text = name instanceof qb.Local ? name.text : typeof name === "string" ? name : null;
            if (!Array.isArray(ids) || text === null) continue;
            for (const id of ids) {
                const zone = id instanceof Name ? (names.get(id.value) ?? "").toLowerCase() : "";
                if (zone && zone !== "z_cartoon") out.set(zone, text.toUpperCase().replace(/[^ -~]/g, ""));
            }
        }
        return out;
    }
    return out;
}

// The sign's text: THP8's area name (with the zone's suffix), else ours.
export function signText(info: ZoneInfo, areaNames: ReadonlyMap<string, string>): string {
    const area = areaNames.get(info.zone);
    return area !== undefined ? area + (info.suffix ?? "") : info.sign;
}

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
    return {
        sun: [0, 0, 0],
        ambientScale: INTERIOR_SCALE,
        sunScale: 0,
        ambientKnee: INTERIOR_KNEE,
    };
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
    hiddenMeshes?: ReadonlySet<geom.GeomMesh>; // meshes not drawn (repeatedSurfaces): a surface another mesh draws
    dome?: boolean; // untextured materials coloured as a clear sky (THP8's atmosphere shader - the sky's dome)
    additive?: ReadonlySet<number>; // materials drawn adding (glowMaterials)
    lighting?: mats.Lighting; // THP8's light maps baked into the vertex colours (materials.ts)
    techniques?: ReadonlyMap<number, readonly string[]>; // the material library's (materials.readTechniques)
    local?: ReadonlySet<number>; // sectors in their own space (localObjects): not moved
    // the triangles kept, by their corners (THP8's metres, placed) and the opaque triangles the earlier parts drew: the
    // stand-ins' over the level's own ground or surfaces go
    keepTriangle?: (corners: readonly (readonly number[])[], drawn: SurfaceIndex) => boolean;
    veil?: boolean; // drawn as a faint see-through wall, untextured (the network barriers: VEIL_COLOUR)
}

// THP8 draws its network barriers as a shimmer (a distortion shader over a normal map - z_online_<x>'s material, its
// texture DXN): THUG2 has no such pass; a faint see-through blue instead, B G R A with 128 = full.
export const VEIL_COLOUR: readonly number[] = [128, 100, 80, 22];

// A material's layers as converted: the uv slot each THUG2 pass reads, the light maps, and the mask read into the
// vertex alpha (a layer whose mask has a uv field of its own).
interface Plan {
    plan: mats.MaterialPlan;
    slots: number[];
    // (inside: an interior's lighting - interiorLighting - else the part's own: its zone's sun)
    light?: { ambient: mats.Decoded; shadow: mats.Decoded; inside: boolean };
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

const BILLBOARD = checksum("billboard");
// The template of THP8's see-through surfaces drawn over a DXN normal map - water (the suburbs' koi ponds) and the
// network barriers' shimmer: no ground of their own (uncoveredGround passes them over).
export const SHIMMER_TEMPLATE = 0x2858c985;

// What a material is (its textures, template and parameter block): two zones' materials of one checksum are one
// material only when these agree.
export function materialKey(m: geom.GeomMaterial): string {
    return `${m.template}|${m.flags}|${m.terrain}|${m.state}|${m.textures.join(",")}|${m.params.toString("hex")}`;
} // the shader template of THP8's camera-facing quads

const SECTOR_FLAGS = (0x80002000 | thug2Scene.SEC_TEXCOORDS | thug2Scene.SEC_COLORS) >>> 0;

// The texture a material's THUG2 pass 0 is drawn with: its plan's first layer, else the first texture that isn't a
// normal map (a few shaders list theirs first). THP8 lists blanks too - a flat grey stand-in for a missing map (the
// suburb lawns' 2511D697 ahead of their grass), 16 x 16 see-through dithers: one of those is passed over for the
// material's biggest real texture (not a normal map, not a light map, not blank); with none, a flat colour stays (white
// paint layers tinted by the pass colour) and a dither goes: undefined, the material drawn untextured (vertex colours).
// (Also what render_audit.ts checks.)
export function baseTexture(
    m: geom.GeomMaterial,
    plan: mats.MaterialPlan | null,
    textures: ReadonlyMap<number, xenos.XenosTexture>,
    maps?: { ambient: ReadonlySet<number>; shadow: ReadonlySet<number> },
): number | undefined {
    const picked =
        plan?.layers[0]?.diffuse ??
        m.textures.find((t) => textures.has(t) && textures.get(t)!.format !== xenos.FMT_DXN);
    const kind = picked !== undefined && textures.has(picked) ? mats.blankKind(textures.get(picked)!) : null;
    if (kind === null) return picked;
    let best: number | undefined;
    let flat: number | undefined;
    for (const t of m.textures) {
        const x = textures.get(t);
        if (!x || x.format === xenos.FMT_DXN || maps?.ambient.has(t) || maps?.shadow.has(t)) continue;
        const k = mats.blankKind(x);
        if (k === null) {
            if (best === undefined || x.width * x.height > textures.get(best)!.width * textures.get(best)!.height)
                best = t;
        } else if (k === "flat") flat ??= t;
    }
    return best ?? (kind === "flat" ? picked : flat);
}

export interface ConvertedScene {
    scene: thug2Scene.Scene;
    textures: tex.TexFile;
    sectors: Set<number>; // the sector checksums written
    // the drawn ground: up-facing triangles of opaque, seen materials (THUG2 inches) and their sector - for
    // uncoveredGround
    ground: { tri: number[][]; sector: number }[];
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
        dropped: number; // triangles left out by a part's keepTriangle
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
        dropped: 0,
    };
    const plans = new Map<number, Plan>();
    const ground: ConvertedScene["ground"] = [];
    const drawnIndex = new SurfaceIndex(); // the opaque world drawn so far, THP8 metres (for the stand-ins' keepTriangle)
    const defined = new Map<number, string>(); // material -> its definition (materialKey): the first part's
    parts.forEach((part, partIndex) => {
        const g = part.geom;
        // (zones merged into one level: a material of a later part whose checksum an earlier part defined otherwise -
        // other light maps, another tint - is made under a checksum of its own; the same material is shared)
        const rename = new Map<number, number>();
        for (const m of g.materials) {
            const key = materialKey(m);
            const seen = defined.get(m.checksum);
            if (seen === undefined) defined.set(m.checksum, key);
            else if (seen !== key)
                rename.set(m.checksum, checksum(`thp8_material_${thp8.hex(m.checksum)}_${partIndex}`));
        }
        const matOf = (c: number): number => rename.get(c) ?? c;
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
            const id = matOf(m.checksum);
            if (materials.has(id) || !m.checksum) continue;
            if (m.template === BILLBOARD) billboards.add(id);
            const used = meshesOf.get(m.checksum) ?? [];
            const slots = used.length ? geom.uvSlots(used[0]!.layout).length : 0;
            const plan =
                part.dome || part.additive?.has(m.checksum) ? null : mats.planMaterial(m, part.textures, maps, slots);
            let texture = 0;
            const first = baseTexture(m, plan, part.textures, maps);
            if (first !== undefined && !part.veil && convertTexture(first)) texture = first;
            const t = texOut.get(texture);
            const add = texture !== 0 && (part.additive?.has(m.checksum) ?? false);
            const alpha = (!add && t !== undefined && (t.dxt === 5 || t.dxt === 2)) || (part.veil ?? false);
            if (alpha) transparent.add(id);
            const mat = makeMaterial(id, texture, add ? "add" : alpha, true);
            if (alpha || add) mat.noBackfaceCulling = 1;
            if (add) {
                // (in the transparent bucket, after the opaque scene: drawn there without it, a halo's black
                // square stood on the floor)
                mat.passes[0]!.flags |= thug2Scene.PASS_TRANSPARENT;
                mat.drawOrder = 1500.0;
                glows.set(id, glowScale(m));
            }
            if (plan && texture && plan.layers.length) {
                const out: Plan = {
                    plan,
                    slots: [plan.layers[0]!.slot],
                    vertexMask: null,
                };
                // (THUG2's pass colour: 0.5 = as the texture)
                const tint = (pass: thug2Scene.Pass, c: readonly number[]): void => {
                    if (c.slice(0, 3).every((x) => x === 1)) return;
                    pass.hasColor = 1;
                    pass.color = c.slice(0, 3).map((x) => x * 0.5);
                };
                tint(mat.passes[0]!, plan.layers[0]!.colour);
                // the further layers (an opaque pass 0 only: a blended one's vertex alpha is its own)
                for (const layer of alpha ? [] : plan.layers.slice(1)) {
                    // (THP8's 16 x 16 dither placeholders in a pass's diffuse slot: a dotted overlay, left out)
                    const diffuse = part.textures.get(layer.diffuse);
                    if (diffuse && mats.blankKind(diffuse) === "stipple") continue;
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
                        out.light = { ambient: a, shadow: s, inside };
                    }
                }
                plans.set(id, out);
            }
            materials.set(id, mat);
        }
        const bySector = new Map(g.sectors.map((s) => [s.checksum, s]));
        const templateOf = new Map(g.materials.map((m) => [m.checksum, m.template]));
        const masters = new Set([...part.placed.values()].map((p) => p.master));
        const emit = (checksumOut: number, s: geom.GeomSector, at: Placement | null): void => {
            const move = part.local?.has(checksumOut) ? [0, 0, 0] : shift;
            const positions: number[][] = [];
            const colors: number[][] = [];
            const uvs: number[][] = [];
            const meshes: { flags: number; material: number; strip: number[] }[] = [];
            // (one uv set per pass: the sector's vertices carry as many as its materials' most)
            const sets = Math.max(1, ...s.meshes.map((m) => plans.get(matOf(m.material))?.slots.length ?? 1));
            for (const m of s.meshes) {
                if (part.hiddenMeshes?.has(m)) continue;
                const id = matOf(m.material);
                if (!materials.has(id)) {
                    counts.skipped++;
                    continue;
                }
                if (billboards.has(id)) {
                    // (every corner at the pivot, spread by THP8's billboard shader to face the camera: left out, as
                    // THAW's are)
                    counts.billboards++;
                    continue;
                }
                const base = positions.length;
                const blend = transparent.has(id);
                const glow = glows.get(id);
                const dome = part.dome && !materials.get(id)!.passes[0]!.texture;
                const plan = plans.get(id);
                const verts = geom.readVertices(m);
                let meshTris = geom.triangles(m);
                if (part.keepTriangle) {
                    const keep = part.keepTriangle;
                    const before = meshTris.length;
                    meshTris = meshTris.filter((t) =>
                        keep(
                            t.map((i) => place(verts[i]!.position, at)),
                            drawnIndex,
                        ),
                    );
                    counts.dropped += before - meshTris.length;
                }
                // (light maps: the mesh's last two uv fields - a mesh with fewer fields than its material's first isn't
                // lit)
                const lit = plan?.light && part.lighting && (verts[0]?.uvs.length ?? 0) >= 3 ? plan.light : null;
                const lighting = lit?.inside ? interiorLighting() : part.lighting;
                if (lit && lighting) {
                    counts.refinedFrom += meshTris.length;
                    meshTris = refineLit(verts, meshTris, lit, lighting, refineOptions);
                    counts.refined += meshTris.length;
                }
                for (const v of verts) {
                    const q = place(v.position, at);
                    positions.push([q[0] * unit + move[0]!, q[1] * unit + move[1]!, q[2] * unit + move[2]!]);
                    if (part.veil) {
                        colors.push([...VEIL_COLOUR]);
                        uvs.push(Array<number>(2 * sets).fill(0));
                        continue;
                    }
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
                    if (lit && lighting) {
                        // THP8's light maps baked into the vertex colour
                        const am = lit.ambient;
                        const sh = lit.shadow;
                        const [au, av] = uvOf(n - 2) as [number, number];
                        const [su, sv] = uvOf(n - 1) as [number, number];
                        k = mats.light(mats.sample(am, au, av), mats.sample(sh, su, sv), lighting);
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
                if (tris.length) meshes.push({ flags: 0, material: id, strip: trisToStrip(tris) });
                // (the drawn world, for the stand-ins' keepTriangle (see-through too: a backdrop drawn twice blends twice) and
                // the drawn ground for the audit (opaque only): not a glow, not the sky, not a barrier, the world's own)
                if (
                    !glow &&
                    !part.dome &&
                    !part.veil &&
                    !part.keepTriangle &&
                    !part.local?.has(checksumOut) &&
                    templateOf.get(m.material) !== SHIMMER_TEMPLATE
                ) {
                    for (const t of tris) {
                        const [a, b, c] = t.map((i) => positions[i]!) as [number[], number[], number[]];
                        const flat = surfaceTri(
                            [a, b, c].map((q) => [0, 1, 2].map((k) => (q[k]! - move[k]!) / unit)) as [Vec3, Vec3, Vec3],
                        );
                        if (flat) drawnIndex.add(flat);
                        if (blend) continue;
                        const u = [b[0]! - a[0]!, b[1]! - a[1]!, b[2]! - a[2]!];
                        const v = [c[0]! - a[0]!, c[1]! - a[1]!, c[2]! - a[2]!];
                        const ny = u[2]! * v[0]! - u[0]! * v[2]!;
                        const len = Math.hypot(u[1]! * v[2]! - u[2]! * v[1]!, ny, u[0]! * v[1]! - u[1]! * v[0]!);
                        if (len && Math.abs(ny / len) > 0.8) ground.push({ tri: [a, b, c], sector: checksumOut });
                    }
                }
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
    });
    const scene = geometry.convertScene({ sectors }, warnings, [...materials.values()]);
    // (meshes of more than 65,535 vertices can't be: split long sectors? - THP8's meshes are far smaller)
    return {
        scene,
        textures: { version: 1, textures: [...texOut.values()] },
        sectors: written,
        counts,
        ground,
    };
}

// --- drawn ground with nothing to stand on ---------------------------------------------------------------------------

// The places (AUDIT_CELL squares, inches) where the level draws ground - an up-facing triangle of an opaque, seen
// material - and its collision has no collidable face within AUDIT_REACH of that height: the skater sinks through or
// THUG2's "uber frig" turns him round there (round 5: 40 m of the suburb road). The gap filler's faces don't count
// (a road must have a floor of its own). Returns the cells' count and the largest connected patches.
export const AUDIT_CELL = 40;
export const AUDIT_REACH = 24;
export const HOLE_RIM = 0.8; // the share of a patch's rim that has collision for it to count as a hole
export interface Uncovered {
    cells: number; // in holes (patches mostly ringed by collision)
    patches: { cells: number; at: number[]; sector: number }[]; // the largest holes
}

export function uncoveredGround(
    ground: ConvertedScene["ground"],
    objects: readonly thug2Col.ColObject[],
    ignore: (checksum: number) => boolean = () => false,
): Uncovered {
    const key = (i: number, k: number): number => i * 1048576 + k;
    // the collision's heights per cell
    const heights = new Map<number, number[]>();
    const each = (tri: readonly number[][], f: (i: number, k: number, y: number) => void): void => {
        const [a, b, c] = tri as [number[], number[], number[]];
        const d = (b[0]! - a[0]!) * (c[2]! - a[2]!) - (c[0]! - a[0]!) * (b[2]! - a[2]!);
        if (Math.abs(d) < 1e-9) return;
        const i0 = Math.ceil(Math.min(a[0]!, b[0]!, c[0]!) / AUDIT_CELL - 0.5);
        const i1 = Math.floor(Math.max(a[0]!, b[0]!, c[0]!) / AUDIT_CELL - 0.5);
        const k0 = Math.ceil(Math.min(a[2]!, b[2]!, c[2]!) / AUDIT_CELL - 0.5);
        const k1 = Math.floor(Math.max(a[2]!, b[2]!, c[2]!) / AUDIT_CELL - 0.5);
        for (let i = i0; i <= i1; i++) {
            const x = (i + 0.5) * AUDIT_CELL;
            for (let k = k0; k <= k1; k++) {
                const z = (k + 0.5) * AUDIT_CELL;
                const s1 = ((x - a[0]!) * (c[2]! - a[2]!) - (c[0]! - a[0]!) * (z - a[2]!)) / d;
                const s2 = ((b[0]! - a[0]!) * (z - a[2]!) - (x - a[0]!) * (b[2]! - a[2]!)) / d;
                if (s1 < -1e-6 || s2 < -1e-6 || s1 + s2 > 1 + 1e-6) continue;
                f(i, k, a[1]! + s1 * (b[1]! - a[1]!) + s2 * (c[1]! - a[1]!));
            }
        }
    };
    for (const o of objects) {
        if (ignore(o.checksum)) continue;
        for (const fc of o.faces) {
            if (fc[0] & (thug2Col.FACE_NON_COLLIDABLE | thug2Col.FACE_TRIGGER)) continue;
            each([o.verts[fc[2]]!, o.verts[fc[3]]!, o.verts[fc[4]]!], (i, k, y) => {
                const list = heights.get(key(i, k));
                if (list) list.push(y);
                else heights.set(key(i, k), [y]);
            });
        }
    }
    const open = new Map<number, number>(); // cell -> sector
    for (const g of ground) {
        each(g.tri, (i, k, y) => {
            const list = heights.get(key(i, k));
            if (!list?.some((h) => Math.abs(h - y) <= AUDIT_REACH)) open.set(key(i, k), g.sector);
        });
    }
    // patches: 4-connected
    const seen = new Set<number>();
    const patches: Uncovered["patches"] = [];
    for (const start of open.keys()) {
        if (seen.has(start)) continue;
        const stack = [start];
        seen.add(start);
        let cells = 0;
        let sx = 0;
        let sz = 0;
        let rimCovered = 0;
        let rimOpen = 0;
        while (stack.length) {
            const c = stack.pop()!;
            cells++;
            const i = Math.round(c / 1048576);
            const k = c - i * 1048576;
            sx += (i + 0.5) * AUDIT_CELL;
            sz += (k + 0.5) * AUDIT_CELL;
            for (const n of [key(i + 1, k), key(i - 1, k), key(i, k + 1), key(i, k - 1)]) {
                if (open.has(n)) {
                    if (!seen.has(n)) {
                        seen.add(n);
                        stack.push(n);
                    }
                } else if (heights.has(n)) rimCovered++;
                else rimOpen++;
            }
        }
        // (a hole: its rim is mostly ground the skater has - drawn ground with no collision out past the world's edge,
        // the hills around the town, is scenery)
        if (rimCovered >= HOLE_RIM * (rimCovered + rimOpen)) {
            patches.push({
                cells,
                at: [sx / cells, sz / cells],
                sector: open.get(start)!,
            });
        }
    }
    patches.sort((x, y) => y.cells - x.cells);
    return {
        cells: patches.reduce((n, pt) => n + pt.cells, 0),
        patches: patches.slice(0, 40),
    };
}

// Lit triangles split where the light maps vary inside them (materials.refine), so shadows keep their shape in the
// vertex colours.
export const REFINE: mats.RefineOptions = {
    threshold: 0.2,
    minLength: 6,
    depth: 3,
    maxVertices: 60000,
};

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
// (zones: a level of several zones gets each one's budget)
export function refineBudget(counts: ConvertedScene["counts"], zones = 1): number {
    const before = counts.triangles - counts.refined + counts.refinedFrom;
    return Math.max(
        before,
        Math.min(MAX_TRIANGLES * zones, before + Math.max(REFINE_EXTRA * zones, Math.round(before * 0.5))),
    );
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
// THP8's 0x40 faces stay solid: they are not pass-through volumes but solid ground that also triggers - the suburb
// roundabout's skirt is four invisible 0x1140 ramps (z_houses bodies 166-169) over its drawn concrete; the town has 81
// such bodies (the funpark's and cartoon world's ramps, the school's bleachers, the capitol's lawns: 65-100 % of their
// area ground-like). Round 4 made the 0x1040 ones non-collidable, and the skater went through the skirt into the planter
// and the tree's roots (the user, round 6) - undone.
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

// --- the walls between zones ---------------------------------------------------------------------------------------

// Each THP8 zone's collision carries invisible walls (every face 0x1000 invisible, collidable; most also 0x80, the
// camera passes) along its edges where it meets the next zone - z_houses' 115 m long, 70 m high plane at x = 0 where
// z_shops begins. They aren't tied to any node (no sector, no node at them: THP8 must turn them off at run time when the
// neighbour is streamed in - not in its data). In a level of several zones they stood across the streets as invisible
// walls (the user, BEAVER VALLEY 2026-10-24). A seam wall: a body all of whose collidable faces are invisible, mostly
// steep, with another of the level's zones' ground on its far side (probes SEAM_PROBE metres either side of its faces)
// at half or more of the places looked at. Kept in one-zone levels (the zone's edge) and where nothing of another zone
// is beyond (the town's outer edge).
export const FACE_INVISIBLE = 0x1000;
export const SEAM_PROBE = 4; // metres either side of the wall
export const SEAM_SAMPLES = 9;
const SEAM_CELL = 8; // metres: the ground index's cells

// The upward-facing collidable triangles of some bodies by x / z cell, for "is there ground here".
export class GroundIndex {
    private readonly cells = new Map<string, havok.HkTriangle[]>();
    constructor(bodies: readonly havok.HkBody[]) {
        for (const b of bodies) {
            for (const t of b.triangles) {
                if (t.material & (thug2Col.FACE_NON_COLLIDABLE | 0x40)) continue;
                const n = normalOf(t);
                if (n[1]! < 0.5) continue;
                const lo = [Math.min(t.a[0]!, t.b[0]!, t.c[0]!), Math.min(t.a[2]!, t.b[2]!, t.c[2]!)];
                const hi = [Math.max(t.a[0]!, t.b[0]!, t.c[0]!), Math.max(t.a[2]!, t.b[2]!, t.c[2]!)];
                for (let x = Math.floor(lo[0]! / SEAM_CELL); x <= Math.floor(hi[0]! / SEAM_CELL); x++) {
                    for (let z = Math.floor(lo[1]! / SEAM_CELL); z <= Math.floor(hi[1]! / SEAM_CELL); z++) {
                        const k = `${x},${z}`;
                        const list = this.cells.get(k);
                        if (list) list.push(t);
                        else this.cells.set(k, [t]);
                    }
                }
            }
        }
    }

    // Whether there's ground under (x, z) between heights lo and hi.
    groundAt(x: number, z: number, lo: number, hi: number): boolean {
        const list = this.cells.get(`${Math.floor(x / SEAM_CELL)},${Math.floor(z / SEAM_CELL)}`) ?? [];
        return list.some((t) => {
            const y = heightIn(t, x, z);
            return y !== null && y >= lo && y <= hi;
        });
    }
}

function normalOf(t: havok.HkTriangle): number[] {
    const u = [0, 1, 2].map((k) => t.b[k]! - t.a[k]!);
    const v = [0, 1, 2].map((k) => t.c[k]! - t.a[k]!);
    const n = [u[1]! * v[2]! - u[2]! * v[1]!, u[2]! * v[0]! - u[0]! * v[2]!, u[0]! * v[1]! - u[1]! * v[0]!];
    const len = Math.hypot(n[0]!, n[1]!, n[2]!) || 1;
    // (THP8's windings vary: "up" is the y-positive one)
    const s = n[1]! < 0 ? -1 : 1;
    return n.map((x) => (s * x) / len);
}

function heightIn(t: havok.HkTriangle, x: number, z: number): number | null {
    const [a, p, c] = [t.a, t.b, t.c];
    const d = (p[0]! - a[0]!) * (c[2]! - a[2]!) - (c[0]! - a[0]!) * (p[2]! - a[2]!);
    if (Math.abs(d) < 1e-9) return null;
    const u = ((x - a[0]!) * (c[2]! - a[2]!) - (c[0]! - a[0]!) * (z - a[2]!)) / d;
    const v = ((p[0]! - a[0]!) * (z - a[2]!) - (x - a[0]!) * (p[2]! - a[2]!)) / d;
    if (u < 0 || v < 0 || u + v > 1) return null;
    return a[1]! + u * (p[1]! - a[1]!) + v * (c[1]! - a[1]!);
}

const triArea = (t: havok.HkTriangle): number => {
    const u = [0, 1, 2].map((k) => t.b[k]! - t.a[k]!);
    const v = [0, 1, 2].map((k) => t.c[k]! - t.a[k]!);
    return Math.hypot(u[1]! * v[2]! - u[2]! * v[1]!, u[2]! * v[0]! - u[0]! * v[2]!, u[0]! * v[1]! - u[1]! * v[0]!) / 2;
};

// The seam walls of each zone of a level (indexes into its bodies), given every zone's bodies. One zone: none.
export function seamWalls(zones: readonly havok.HkBody[][]): Set<number>[] {
    const out = zones.map(() => new Set<number>());
    if (zones.length < 2) return out;
    const grounds = zones.map((bodies) => new GroundIndex(bodies));
    zones.forEach((bodies, z) => {
        bodies.forEach((body, i) => {
            const solid = body.triangles.filter((t) => !(t.material & (thug2Col.FACE_NON_COLLIDABLE | 0x40)));
            if (!solid.length || !solid.every((t) => t.material & FACE_INVISIBLE)) return;
            const steep = solid.filter((t) => Math.abs(normalOf(t)[1]!) < 0.3);
            const area = solid.reduce((s, t) => s + triArea(t), 0);
            if (steep.reduce((s, t) => s + triArea(t), 0) < 0.6 * area) return;
            // probes either side of the largest steep faces' middles
            const looked = [...steep].sort((a, b) => triArea(b) - triArea(a)).slice(0, SEAM_SAMPLES);
            let seams = 0;
            for (const t of looked) {
                const n = normalOf(t);
                const h = Math.hypot(n[0]!, n[2]!) || 1;
                const mid = [0, 1, 2].map((k) => (t.a[k]! + t.b[k]! + t.c[k]!) / 3);
                const lo = Math.min(t.a[1]!, t.b[1]!, t.c[1]!) - 15;
                const hi = Math.max(t.a[1]!, t.b[1]!, t.c[1]!) + 2;
                const beyond = [-1, 1].some((side) => {
                    const x = mid[0]! + (side * SEAM_PROBE * n[0]!) / h;
                    const zz = mid[2]! + (side * SEAM_PROBE * n[2]!) / h;
                    return grounds.some((g, other) => other !== z && g.groundAt(x, zz, lo, hi));
                });
                if (beyond) seams++;
            }
            if (seams * 2 >= looked.length) out[z]!.add(i);
        });
    });
    return out;
}

// --- narrow holes in the ground ------------------------------------------------------------------------------------

// THP8's collision has narrow slits with nothing below them where two bodies meet - z_houses' kerbs: the kerb body
// ends 0.6 m short of the road's box (THP8 data, -46.2, -58.8). THP8's skater is a Havok shape that rides over them;
// THUG2's skater stands on the one point under it and fell in, out of the world, hanging from the kerb's underside
// (the user, BEAVER VALLEY 2026-10-24). Filled here: on a GAP_CELL grid (inches) the cells with no collidable,
// not-steep face at any height, between ground no more than GAP_WIDE cells away on both sides (along x or z) whose
// heights differ by GAP_STEP or less, get a floor at the lower side's height - only void is filled, never a dip.
export const GAP_CELL = 10; // inches (a quarter metre)
export const GAP_WIDE = 5; // cells either side: holes up to ~2.5 m across
export const GAP_STEP = 24; // inches
export const GAP_FILL = "GapFill";
// Void between ground and a wall standing on it (a house's invisible shell under its roof's eaves, where the yard ends
// a metre or more short of it: the skater slid off the roof into it - the user, BEAVER VALLEY round 4) is floored too,
// up to WALL_WIDE cells across; looked at along x, z and both diagonals.
export const WALL_WIDE = 16;
export const LEAF_MOST = 0x800; // faces a collision BSP leaf may hold (THUG2's line test)
export const GAP_TILE = 1600; // inches: the fill objects' squares
export const GAP_MOST = 1500; // faces a fill object

export function gapFills(objects: readonly geometry.SourceColObject[], level: string): geometry.SourceColObject[] {
    const lo = [Infinity, Infinity];
    const hi = [-Infinity, -Infinity];
    for (const o of objects) {
        lo[0] = Math.min(lo[0]!, o.bbox[0]!);
        lo[1] = Math.min(lo[1]!, o.bbox[2]!);
        hi[0] = Math.max(hi[0]!, o.bbox[4]!);
        hi[1] = Math.max(hi[1]!, o.bbox[6]!);
    }
    if (!Number.isFinite(lo[0]!)) return [];
    const nx = Math.ceil((hi[0]! - lo[0]!) / GAP_CELL) + 1;
    const nz = Math.ceil((hi[1]! - lo[1]!) / GAP_CELL) + 1;
    const height = new Float32Array(nx * nz).fill(Number.NaN); // the highest ground at the cell's points
    const terrain = new Uint8Array(nx * nz);
    // (four points a cell, at its quarters: a cell is ground when all four are - a slit narrower than a cell between
    // two cells' middles was missed; round 5, the kerb by the suburb's flower ring: 10 inches)
    const covered = new Uint8Array(nx * nz); // a bit per point
    for (const o of objects) {
        for (const f of o.faces) {
            if (f[0] & (thug2Col.FACE_NON_COLLIDABLE | thug2Col.FACE_TRIGGER)) continue;
            const [a, b, c] = [o.verts[f[2]]!, o.verts[f[3]]!, o.verts[f[4]]!];
            const d = (b[0]! - a[0]!) * (c[2]! - a[2]!) - (c[0]! - a[0]!) * (b[2]! - a[2]!);
            const u = [b[0]! - a[0]!, b[1]! - a[1]!, b[2]! - a[2]!];
            const v = [c[0]! - a[0]!, c[1]! - a[1]!, c[2]! - a[2]!];
            const n = [u[1]! * v[2]! - u[2]! * v[1]!, u[2]! * v[0]! - u[0]! * v[2]!, u[0]! * v[1]! - u[1]! * v[0]!];
            const len = Math.hypot(n[0]!, n[1]!, n[2]!);
            if (!len || Math.abs(n[1]! / len) < 0.5 || Math.abs(d) < 1e-9) continue; // (steep: no ground)
            const x0 = Math.max(0, Math.floor((Math.min(a[0]!, b[0]!, c[0]!) - lo[0]!) / GAP_CELL));
            const x1 = Math.min(nx - 1, Math.floor((Math.max(a[0]!, b[0]!, c[0]!) - lo[0]!) / GAP_CELL));
            const z0 = Math.max(0, Math.floor((Math.min(a[2]!, b[2]!, c[2]!) - lo[1]!) / GAP_CELL));
            const z1 = Math.min(nz - 1, Math.floor((Math.max(a[2]!, b[2]!, c[2]!) - lo[1]!) / GAP_CELL));
            for (let i = x0; i <= x1; i++) {
                for (let k = z0; k <= z1; k++) {
                    const at = i * nz + k;
                    for (let q = 0; q < 4; q++) {
                        const x = lo[0]! + (i + (q & 1 ? 0.75 : 0.25)) * GAP_CELL;
                        const z = lo[1]! + (k + (q & 2 ? 0.75 : 0.25)) * GAP_CELL;
                        const s = ((x - a[0]!) * (c[2]! - a[2]!) - (c[0]! - a[0]!) * (z - a[2]!)) / d;
                        const t = ((b[0]! - a[0]!) * (z - a[2]!) - (x - a[0]!) * (b[2]! - a[2]!)) / d;
                        if (s < -1e-6 || t < -1e-6 || s + t > 1 + 1e-6) continue;
                        const y = a[1]! + s * u[1]! + t * v[1]!;
                        covered[at] = covered[at]! | (1 << q);
                        if (!(height[at]! >= y)) {
                            height[at] = y;
                            terrain[at] = f[1];
                        }
                    }
                }
            }
        }
    }
    // the walls: cells a steep collidable face passes through, with its lowest and highest point there
    const wallLo = new Float32Array(nx * nz).fill(Number.NaN);
    const wallHi = new Float32Array(nx * nz).fill(Number.NaN);
    for (const o of objects) {
        for (const f of o.faces) {
            if (f[0] & (thug2Col.FACE_NON_COLLIDABLE | thug2Col.FACE_TRIGGER)) continue;
            const [a, b, c] = [o.verts[f[2]]!, o.verts[f[3]]!, o.verts[f[4]]!];
            const u = [b[0]! - a[0]!, b[1]! - a[1]!, b[2]! - a[2]!];
            const v = [c[0]! - a[0]!, c[1]! - a[1]!, c[2]! - a[2]!];
            const n = [u[1]! * v[2]! - u[2]! * v[1]!, u[2]! * v[0]! - u[0]! * v[2]!, u[0]! * v[1]! - u[1]! * v[0]!];
            const len = Math.hypot(n[0]!, n[1]!, n[2]!);
            if (!len || Math.abs(n[1]! / len) >= 0.5) continue;
            const span = Math.max(Math.hypot(u[0]!, u[1]!, u[2]!), Math.hypot(v[0]!, v[1]!, v[2]!));
            const steps = Math.max(1, Math.min(2000, Math.ceil(span / (GAP_CELL / 2))));
            for (let p = 0; p <= steps; p++) {
                for (let q = 0; p + q <= steps; q++) {
                    const x = a[0]! + (u[0]! * p + v[0]! * q) / steps;
                    const y = a[1]! + (u[1]! * p + v[1]! * q) / steps;
                    const z = a[2]! + (u[2]! * p + v[2]! * q) / steps;
                    const i = Math.floor((x - lo[0]!) / GAP_CELL);
                    const k = Math.floor((z - lo[1]!) / GAP_CELL);
                    if (i < 0 || k < 0 || i >= nx || k >= nz) continue;
                    const at = i * nz + k;
                    if (!(wallLo[at]! <= y)) wallLo[at] = y;
                    if (!(wallHi[at]! >= y)) wallHi[at] = y;
                }
            }
        }
    }
    // the holes: void between ground close by on both sides, or between ground and a wall standing on it
    const fill = new Float32Array(nx * nz).fill(Number.NaN);
    const fillTerrain = new Uint8Array(nx * nz);
    const inside = (i: number, k: number): boolean => i >= 0 && k >= 0 && i < nx && k < nz;
    // (a wall: taller than a step - a kerb's face in a slit isn't one)
    const tallWall = (at: number): boolean => !Number.isNaN(wallLo[at]!) && wallHi[at]! - wallLo[at]! > GAP_STEP;
    // the nearest non-void cell from (i, k) stepping (di, dk), at most `most` steps: [ground height, terrain, wall?]
    const nearest = (i: number, k: number, di: number, dk: number, most: number): [number, number, boolean] | null => {
        for (let s = 1; s <= most; s++) {
            const x = i + di * s;
            const z = k + dk * s;
            if (!inside(x, z)) return null;
            const at = x * nz + z;
            if (covered[at] === 15) return [height[at]!, terrain[at]!, false];
            if (tallWall(at)) return [wallLo[at]!, 0, true];
        }
        return null;
    };
    for (let i = 0; i < nx; i++) {
        for (let k = 0; k < nz; k++) {
            const at0 = i * nz + k;
            if (covered[at0] === 15 || tallWall(at0)) continue;
            let best = Number.NaN;
            let bestTerrain = 0;
            for (const [di, dk] of [
                [1, 0],
                [0, 1],
                [1, 1],
                [1, -1],
            ] as const) {
                const back = nearest(i, k, -di, -dk, WALL_WIDE);
                const ahead = nearest(i, k, di, dk, WALL_WIDE);
                if (!back || !ahead || (back[2] && ahead[2])) continue;
                let y: number;
                let t: number;
                if (!back[2] && !ahead[2]) {
                    // ground both sides: close by, a small step
                    const reach = (ii: number, kk: number, d1: number, d2: number): boolean =>
                        nearest(ii, kk, d1, d2, GAP_WIDE) !== null;
                    if (!reach(i, k, -di, -dk) || !reach(i, k, di, dk)) continue;
                    if (Math.abs(back[0] - ahead[0]) > GAP_STEP) continue;
                    y = Math.min(back[0], ahead[0]);
                    t = back[0] <= ahead[0] ? back[1] : ahead[1];
                } else {
                    // ground one side, a wall the other that reaches down to about its height (it stands on it)
                    const ground = back[2] ? ahead : back;
                    const wall = back[2] ? back : ahead;
                    if (wall[0] > ground[0] + GAP_STEP) continue;
                    y = ground[0];
                    t = ground[1];
                }
                if (Number.isNaN(best) || y < best) {
                    best = y;
                    bestTerrain = t;
                }
            }
            if (!Number.isNaN(best)) {
                fill[i * nz + k] = best;
                fillTerrain[i * nz + k] = bestTerrain;
            }
        }
    }
    // the filled cells as quads, runs along z of one height merged (at most a tile long); an object per GAP_TILE
    // square, GAP_MOST faces at most - THUG2's collision objects want to be small: a town-wide one of 30,000 faces
    // crashed its line test (CCollStatic::CollideWithLine, round 4)
    const tiles = new Map<string, { verts: number[][]; faces: thug2Col.Face[] }[]>();
    const tileCells = Math.max(1, Math.floor(GAP_TILE / GAP_CELL));
    for (let i = 0; i < nx; i++) {
        let k = 0;
        while (k < nz) {
            const y = fill[i * nz + k]!;
            if (Number.isNaN(y)) {
                k++;
                continue;
            }
            const tileEnd = (Math.floor(k / tileCells) + 1) * tileCells;
            let e = k + 1;
            while (e < nz && e < tileEnd && fill[i * nz + e] === y) e++;
            // (half a cell wider all round: a cell is only known empty in its middle - the ground's edge can be
            // anywhere in the next one)
            const x0 = lo[0]! + (i - 0.5) * GAP_CELL;
            const x1 = x0 + 2 * GAP_CELL;
            const z0 = lo[1]! + (k - 0.5) * GAP_CELL;
            const z1 = lo[1]! + (e + 0.5) * GAP_CELL;
            const key = `${Math.floor(i / tileCells)},${Math.floor(k / tileCells)}`;
            let list = tiles.get(key);
            if (!list) {
                list = [];
                tiles.set(key, list);
            }
            let piece = list[list.length - 1];
            if (!piece || piece.faces.length >= GAP_MOST) {
                piece = { verts: [], faces: [] };
                list.push(piece);
            }
            const base = piece.verts.length;
            piece.verts.push([x0, y, z0], [x1, y, z0], [x1, y, z1], [x0, y, z1]);
            const t = fillTerrain[i * nz + k]!;
            piece.faces.push([0, t, base, base + 2, base + 1], [0, t, base, base + 3, base + 2]);
            k = e;
        }
    }
    const out: geometry.SourceColObject[] = [];
    for (const list of tiles.values()) {
        for (const { verts, faces } of list) {
            const blo = [0, 1, 2].map((k) => Math.min(...verts.map((p) => p[k]!)));
            const bhi = [0, 1, 2].map((k) => Math.max(...verts.map((p) => p[k]!)));
            out.push({
                checksum: checksum(`${level}_${GAP_FILL}_${out.length}`),
                flags: 0,
                verts,
                intensities: Buffer.alloc(verts.length, 128),
                faces,
                bbox: [blo[0]!, blo[1]!, blo[2]!, 1, bhi[0]!, bhi[1]!, bhi[2]!, 1],
            });
        }
    }
    return out;
}

// --- invisible shells under roofs ----------------------------------------------------------------------------------

// Some of THP8's buildings have no wall collision of their own: an invisible open-top shell (every face 0x1000) stands
// inside the drawn walls, its top at the roof's eaves and gables, the roof a slab of its own (z_houses' Grecian house:
// the shell's walls to 9.30 m, the roof's top 9.30-11.0 m). THP8's skater slides over where they meet; THUG2's caught on
// the shell's edge under the roof (the user, BEAVER VALLEY round 4). The shell's steep faces are cut SHELL_CLEAR below
// every roof face over them (a seen, collidable, not-steep face whose footprint holds the shell face's highest point,
// within SHELL_REACH above it): no edge comes up to the roof's surface. The way in it might open is a slit narrower
// than the skater under the roof.
export const SHELL_CLEAR = 6; // inches
export const SHELL_REACH = 48; // inches

function planeHeight(t: readonly (readonly number[])[], x: number, z: number): number | null {
    const [a, b, c] = t as [readonly number[], readonly number[], readonly number[]];
    const u = [b[0]! - a[0]!, b[1]! - a[1]!, b[2]! - a[2]!];
    const v = [c[0]! - a[0]!, c[1]! - a[1]!, c[2]! - a[2]!];
    const n = [u[1]! * v[2]! - u[2]! * v[1]!, u[2]! * v[0]! - u[0]! * v[2]!, u[0]! * v[1]! - u[1]! * v[0]!];
    if (Math.abs(n[1]!) < 1e-9) return null;
    return a[1]! - (n[0]! * (x - a[0]!) + n[2]! * (z - a[2]!)) / n[1]!;
}

function inFootprint(t: readonly (readonly number[])[], x: number, z: number): boolean {
    const [a, b, c] = t as [readonly number[], readonly number[], readonly number[]];
    const d = (b[0]! - a[0]!) * (c[2]! - a[2]!) - (c[0]! - a[0]!) * (b[2]! - a[2]!);
    if (Math.abs(d) < 1e-9) return false;
    const s = ((x - a[0]!) * (c[2]! - a[2]!) - (c[0]! - a[0]!) * (z - a[2]!)) / d;
    const r = ((b[0]! - a[0]!) * (z - a[2]!) - (x - a[0]!) * (b[2]! - a[2]!)) / d;
    return s >= -1e-6 && r >= -1e-6 && s + r <= 1 + 1e-6;
}

// A triangle cut to its part at or below `top(x, z)`: 0-2 triangles.
export function clipBelow(tri: readonly number[][], top: (x: number, z: number) => number): number[][][] {
    const f = (p: readonly number[]): number => top(p[0]!, p[2]!) - p[1]!;
    const out: number[][] = [];
    for (let i = 0; i < 3; i++) {
        const p = tri[i]!;
        const q = tri[(i + 1) % 3]!;
        const fp = f(p);
        const fq = f(q);
        if (fp >= 0) out.push([...p]);
        if (fp >= 0 !== fq >= 0) {
            const t = fp / (fp - fq);
            out.push([0, 1, 2].map((k) => p[k]! + t * (q[k]! - p[k]!)));
        }
    }
    const tris: number[][][] = [];
    for (let i = 1; i + 1 < out.length; i++) tris.push([out[0]!, out[i]!, out[i + 1]!]);
    return tris;
}

// The shells' faces cut under the roofs, in place. Returns how many shells were cut.
export function trimShells(objects: geometry.SourceColObject[]): number {
    const solid = (f: thug2Col.Face): boolean => !(f[0] & (thug2Col.FACE_NON_COLLIDABLE | thug2Col.FACE_TRIGGER));
    const normalY = (t: readonly (readonly number[])[]): number => {
        const [a, b, c] = t as [readonly number[], readonly number[], readonly number[]];
        const u = [b[0]! - a[0]!, b[1]! - a[1]!, b[2]! - a[2]!];
        const v = [c[0]! - a[0]!, c[1]! - a[1]!, c[2]! - a[2]!];
        const n = [u[1]! * v[2]! - u[2]! * v[1]!, u[2]! * v[0]! - u[0]! * v[2]!, u[0]! * v[1]! - u[1]! * v[0]!];
        return Math.abs(n[1]!) / (Math.hypot(n[0]!, n[1]!, n[2]!) || 1);
    };
    // the roofs: seen, collidable, not steep faces, by 64-inch cell
    const roofs = new Map<string, number[][][]>();
    for (const o of objects) {
        for (const f of o.faces) {
            if (!solid(f) || f[0] & FACE_INVISIBLE) continue;
            const t = [o.verts[f[2]]!, o.verts[f[3]]!, o.verts[f[4]]!];
            if (normalY(t) < 0.3) continue;
            const x0 = Math.floor(Math.min(...t.map((p) => p[0]!)) / 64);
            const x1 = Math.floor(Math.max(...t.map((p) => p[0]!)) / 64);
            const z0 = Math.floor(Math.min(...t.map((p) => p[2]!)) / 64);
            const z1 = Math.floor(Math.max(...t.map((p) => p[2]!)) / 64);
            if ((x1 - x0 + 1) * (z1 - z0 + 1) > 4000) continue; // (huge ground planes: no roof)
            for (let x = x0; x <= x1; x++) {
                for (let z = z0; z <= z1; z++) {
                    const k = `${x},${z}`;
                    const list = roofs.get(k);
                    if (list) list.push(t);
                    else roofs.set(k, [t]);
                }
            }
        }
    }
    let cut = 0;
    for (const o of objects) {
        const faces = o.faces.filter(solid);
        if (!faces.length || !faces.every((f) => f[0] & FACE_INVISIBLE)) continue;
        let changed = false;
        const newFaces: thug2Col.Face[] = [];
        for (const f of o.faces) {
            const t = [o.verts[f[2]]!, o.verts[f[3]]!, o.verts[f[4]]!];
            if (!solid(f) || normalY(t) >= 0.3) {
                newFaces.push(f);
                continue;
            }
            // the roof faces over the shell face's highest point, within reach
            const topPoint = t.reduce((m, p) => (p[1]! > m[1]! ? p : m));
            const over = (roofs.get(`${Math.floor(topPoint[0]! / 64)},${Math.floor(topPoint[2]! / 64)}`) ?? []).filter(
                (r) => {
                    if (!inFootprint(r, topPoint[0]!, topPoint[2]!)) return false;
                    const h = planeHeight(r, topPoint[0]!, topPoint[2]!);
                    return h !== null && h > topPoint[1]! - SHELL_REACH && h < topPoint[1]! + SHELL_REACH;
                },
            );
            if (!over.length) {
                newFaces.push(f);
                continue;
            }
            const top = (x: number, z: number): number =>
                Math.min(...over.map((r) => planeHeight(r, x, z) ?? Number.POSITIVE_INFINITY)) - SHELL_CLEAR;
            const pieces = clipBelow(t, top);
            if (pieces.length === 1 && pieces[0]!.every((p, k) => p.every((x, j) => x === t[k]![j]))) {
                newFaces.push(f);
                continue;
            }
            changed = true;
            for (const piece of pieces) {
                const base = o.verts.length;
                o.verts.push(...piece);
                newFaces.push([f[0], f[1], base, base + 1, base + 2]);
            }
        }
        if (changed) {
            o.faces = newFaces;
            if (Buffer.isBuffer(o.intensities)) o.intensities = Buffer.alloc(o.verts.length, 128);
            cut++;
        }
    }
    return cut;
}

export interface CollisionResult {
    col: thug2Col.ColFile;
    objects: Map<number, number>; // checksum -> faces
    named: number; // bodies named after their scene sector / node
    hidden: number; // bodies of nodes not made, left out
    filled: number; // faces filling narrow holes in the ground (gapFills)
    shells: number; // invisible shells cut under their roofs (trimShells)
    fillObjects: Set<number>; // gapFills' objects
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
        drop?: ReadonlySet<number>; // bodies left out, by index (seamWalls)
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
    for (const { bodies: unsorted, sectors, hidden, local, volumes, drop } of sets) {
        const whole = drop?.size ? unsorted.filter((b) => !drop.has(b.index)) : unsorted;
        // (a body holding trigger-volume faces and others - the Capitol's lake kill inside its reflection's body - as
        // two: the volume's faces their own object, named after the volume's node)
        const isVolume = (t: havok.HkTriangle): boolean =>
            (t.material & FACE_VOLUME) !== 0 && (t.material & thug2Col.FACE_NON_COLLIDABLE) !== 0;
        const bodies = volumes?.length
            ? whole.flatMap((b) => {
                  const v = b.triangles.filter(isVolume);
                  if (!v.length || v.length === b.triangles.length) return [b];
                  return [
                      {
                          index: b.index,
                          triangles: b.triangles.filter((t) => !isVolume(t)),
                      },
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
                // (only a body of trigger / non-collidable faces is a volume: a solid one named after a trigger node
                // in its box was dropped with that node when the node isn't made - z_houses' suburb road, body 160,
                // took the name of the chalk goal trigger standing on it and went: a 40 m hole, round 5)
                const volumeLike = body.triangles.every(
                    (t) => (t.material & (thug2Col.FACE_NON_COLLIDABLE | thug2Col.FACE_TRIGGER)) !== 0,
                );
                const inside = (volumeLike ? (volumes ?? []) : []).filter(
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
    // (the invisible shells cut under their roofs: trimShells; the narrow holes in the ground filled: gapFills)
    const shells = trimShells(objects);
    const fills = gapFills(objects, level);
    objects.push(...fills);
    const col = geometry.convertCol({ objects }, keepTriggers, warnings, (f) => f);
    return {
        col,
        objects: new Map(objects.map((o) => [o.checksum, o.faces.length])),
        named,
        hidden: hiddenBodies,
        filled: fills.reduce((n, o) => n + o.faces.length, 0),
        fillObjects: new Set(fills.map((o) => o.checksum)),
        shells,
    };
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

// A zone's node array (`<zone>_NodeArray`), its includes (the compressed nodes) put in the nodes - a new copy each time.
export function nodeArrayOf(nqb: thaw.QbItems, zone: string): QbValue[] {
    const key = checksum(`${zone}_NodeArray`);
    const raw = nqb.globals.find(([n]) => n.value === key)?.[1];
    if (!Array.isArray(raw)) pyExit(`${zone}: no ${zone}_NodeArray`);
    return nodes.expandIncludes(raw, new Map(nqb.globals.map(([k, v]) => [k.value, v])));
}

// Zones' node arrays as one (zones merged into one level): one after the other, each one's Links (indices into its own
// array) moved up by the nodes before it. THP8's node names are the zone's (Z_Houses_...): none repeats across the
// city's zones.
export function mergeNodeArrays(arrays: readonly QbValue[][]): QbValue[] {
    const LINKS = checksum("Links");
    const out: QbValue[] = [];
    for (const array of arrays) {
        const base = out.length;
        for (const node of array) {
            if (base && node instanceof Struct) {
                node.entries = node.entries.map(([k, v]) =>
                    k !== null && k.value === LINKS && Array.isArray(v)
                        ? [k, v.map((i) => (typeof i === "number" ? i + base : i))]
                        : [k, v],
                );
            }
            out.push(node);
        }
    }
    return out;
}

export function zoneNodes(
    nodeArray: QbValue[], // the zone's (zones') node array: nodeArrayOf / mergeNodeArrays
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
    let array = nodeArray;
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

export function readZone(file: string, names: Map<number, string>, warnings: string[]): ZoneData {
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

// A THUG2 level made of THP8 zones: one zone (the zone levels: ZONES), or several in THP8's shared frame (its network
// levels: networkAreas). `zones` first is the level's own: its lights.
export interface LevelSpec {
    level: string; // thp8_houses, thp8_net_skatecentral
    folder: string; // the manifest's folder (level_select.ts's THP8_LEVELS key): z_houses, net_skatecentral
    zones: readonly string[];
    barriers: readonly string[]; // network areas (z_online_<x>) whose barrier walls are added, as collision only
    sign: string;
    location: string;
    levelNum: number;
    big?: boolean; // wider than THUG2's network positions reach: IsBigLevel (level_select.ts)
}

export function zoneSpec(info: ZoneInfo, areaNames: ReadonlyMap<string, string>): LevelSpec {
    return {
        level: levelName(info),
        folder: info.zone,
        zones: [info.zone],
        barriers: [],
        sign: signText(info, areaNames),
        location: info.location,
        levelNum: info.levelNum,
    };
}

// --- THP8's network levels ----------------------------------------------------------------------------------------

// THP8's network levels as its own data has them (qb.pak's online_level_select_menu_level_info, each entry naming a
// multiplayer_level_<x> structure): its text, its id (`checksum`), the zones whose network spawns it uses
// (`spawn_in` - the zones it is made of), the network area whose barriers close it off (`area_pak`: z_online_<x>; none
// for the whole town, BEAVER VALLEY) and the zone that area belongs to (`area_pak_zone`).
export interface NetArea {
    id: string; // THP8's checksum name: SkateCentral
    text: string; // SKATE CENTRAL
    zones: string[]; // spawn_in, lower case: z_houses...
    barriers: string; // area_pak: z_online_central, or ""
    mainZone: string; // area_pak_zone, lower case, or ""
    description: string; // the level select's (level_select_ui_<x>)
}

export const NET_LEVEL_LIST = "online_level_select_menu_level_info";

export function networkAreas(compressed: string, names: Map<number, string>): NetArea[] {
    const pak = thp8.openPak(findCaseless(compressed, "PAK", "qb.pak.xen")!);
    const globals = new Map<number, QbValue>();
    for (const e of pak.entries) {
        if (e.extension !== ".qb") continue;
        for (const [k, v] of new thaw.QbItems(thp8.entryData(pak, e), "qb.pak", names, ">").globals)
            globals.set(k.value, v);
    }
    return readNetworkAreas(globals, names);
}

export function readNetworkAreas(globals: ReadonlyMap<number, QbValue>, names: ReadonlyMap<number, string>): NetArea[] {
    const list = globals.get(checksum(NET_LEVEL_LIST));
    if (!Array.isArray(list)) pyExit(`qb.pak: no ${NET_LEVEL_LIST}`);
    const name = (v: QbValue | undefined): string =>
        v instanceof Name ? (names.get(v.value) ?? thp8.hex(v.value)) : "";
    const text = (v: QbValue | undefined): string => (v instanceof qb.Local ? v.text : typeof v === "string" ? v : "");
    const out: NetArea[] = [];
    for (const entry of list) {
        if (!(entry instanceof Struct)) continue;
        // (the entry's unnamed Name: the multiplayer_level_<x> structure)
        const ref = entry.entries.find(([k, v]) => k === null && v instanceof Name)?.[1];
        const level = ref instanceof Name ? globals.get(ref.value) : undefined;
        if (!(level instanceof Struct)) continue;
        const spawn = level.get("spawn_in");
        const ui = level.get("ui_struct");
        const uiStruct = ui instanceof Name ? globals.get(ui.value) : undefined;
        out.push({
            id: name(level.get("checksum")),
            text: text(level.get("text"))
                .toUpperCase()
                .replace(/[^ -~]/g, ""),
            zones: Array.isArray(spawn) ? spawn.map((z) => name(z).toLowerCase()).filter((z) => z) : [],
            barriers: text(level.get("area_pak")).toLowerCase(),
            mainZone: text(level.get("area_pak_zone")).toLowerCase(),
            description: uiStruct instanceof Struct ? text(uiStruct.get("description")) : "",
        });
    }
    return out;
}

// Every one of THP8's network levels is a level of its own (one zone or several), numbered from NET_LEVEL_NUM in
// THP8's order (BEAVER VALLEY, the whole town, first); the sign is THP8's text (" AREA" added where a standalone
// level has the same - none do).
export const NET_LEVEL_NUM = 113;
// THUG2 sends a skater's x / z in network games in half units (+-16384 units: src/game/net/thugpro_compat.h); a level
// reaching past this from its middle gets IsBigLevel (whole units).
export const NET_HALF_REACH = 16000;

export function netLevelName(area: NetArea): string {
    return `thp8_net_${area.id.toLowerCase()}`;
}

export function netSpecs(areas: readonly NetArea[], zoneSigns: ReadonlySet<string>): LevelSpec[] {
    const out: LevelSpec[] = [];
    for (const area of areas.filter((a) => a.zones.length)) {
        // (the area's own zone first: its lights; the others in THP8's order)
        const zones = area.zones.includes(area.mainZone)
            ? [area.mainZone, ...area.zones.filter((z) => z !== area.mainZone)]
            : [...area.zones];
        out.push({
            level: netLevelName(area),
            folder: `net_${area.id.toLowerCase()}`,
            zones,
            barriers: area.barriers ? [area.barriers] : [],
            sign: zoneSigns.has(area.text) ? `${area.text} AREA` : area.text,
            location: `THP8's network level ${area.text}: ${zones.join(" + ")}`,
            levelNum: NET_LEVEL_NUM + out.length,
        });
    }
    return out;
}

// --- converting ---------------------------------------------------------------------------------------------------

// What every level's conversion shares (read once).
interface Shared {
    args: Args;
    compressed: string;
    out: string;
    names: Map<number, string>;
    terrains: string[];
    flags: Map<number, boolean>;
    managers: Map<number, Struct>;
    techniques: Map<number, string[]>;
    port: ScriptPort;
    zoneScripts: ZoneScripts;
    globalProps: Map<number, PropModel>;
    sky: string | null;
    report: string[];
}

// One zone of a level as read and prepared: its scene part, its collision set, its node array's pieces.
interface ZonePart {
    zone: string;
    zoneName: string; // THP8's spelling (Z_Houses)
    data: ZoneData;
    array: QbValue[];
    placed: Map<number, { master: number; at: Placement }>;
    hidden: Set<number>;
    hiddenMeshes: Set<geom.GeomMesh>; // meshes drawing a surface another does (repeatedSurfaces): not drawn
    duplicate: Set<number>; // sectors drawn by an earlier zone under its prefix (zoneDuplicates): not drawn, collision kept
    local: Set<number>;
    made: Map<number, number>; // props: node -> type
    props: Map<number, PropModel>;
    flagged: number;
}

function readPart(zone: string, ctx: Shared, warnings: string[]): ZonePart | null {
    const file = zonePak(ctx.compressed, zone);
    if (file === null) return null;
    const data = readZone(file, ctx.names, warnings);
    if (data.geom === null || data.nqb === null) return null;
    const zoneName = ctx.names.get(checksum(zone)) ?? zone;
    const array = nodeArrayOf(data.nqb, zoneName);
    // (the node flags first: they decide which instances and sectors are made)
    const flagged = applyNodeFlags(array, ctx.flags);
    const placed = new Map<number, { master: number; at: Placement }>();
    for (const [k, v] of instances(array)) {
        const node = array.find((n) => n instanceof Struct && nodeName(n) === k) as Struct | undefined;
        if (node?.hasFlag("CreatedAtStart")) placed.set(k, v);
    }
    // (the props first: they become LevelObjects - local, drawn and collided as theirs)
    const props = new Map([...ctx.globalProps, ...propModels(data.pak)]);
    const made = bouncyProps(array, ctx.names, props);
    const hidden = notMade(array, ctx.names);
    const local = localObjects(array, ctx.names);
    if (ctx.args.leaveOut) {
        for (const s of data.geom.sectors)
            if (ctx.args.leaveOut.test(ctx.names.get(s.checksum) ?? "")) hidden.add(s.checksum);
    }
    return {
        zone,
        zoneName,
        data,
        array,
        placed,
        hidden,
        hiddenMeshes: new Set(),
        duplicate: new Set(),
        local,
        made,
        props,
        flagged,
    };
}

// Sectors a later zone of a merged level draws that an earlier zone draws already, under another zone's prefix and with
// the same bounding box (the cartoon world's archive carries a copy of the fun park's surroundings:
// z_cartoon_g_funpark_perif_peninsula = Z_Funpark_g_funpark_perif_peninsula; the school's library windows). Per part,
// the later ones' checksums. `twins` counts what THP8 ships twice inside one zone (VC_ / G_ pairs: the same box and
// triangle count, other names) - listed only, not removed.
export function zoneDuplicates(
    zones: readonly { geom: geom.Geom | null }[],
    names: ReadonlyMap<number, string>,
): { duplicates: Set<number>[]; twins: number } {
    const strip = (n: string): string => n.toLowerCase().replace(/^z_[a-z0-9]+?_/, "");
    const boxKey = (bbox: readonly number[]): string => bbox.map((x) => Math.round(x * 100)).join(",");
    const seen = new Map<string, string>(); // stripped name + box -> the prefix it was first drawn under
    const duplicates = zones.map(() => new Set<number>());
    let twins = 0;
    zones.forEach((zone, k) => {
        const added: [string, string][] = [];
        const boxes = new Set<string>();
        for (const s of zone.geom?.sectors ?? []) {
            const name = names.get(s.checksum);
            if (name === undefined) continue;
            const stripped = strip(name);
            const prefix = name.toLowerCase().slice(0, name.length - stripped.length);
            const key = `${stripped}|${boxKey(s.bbox)}`;
            const first = seen.get(key);
            if (first !== undefined && first !== prefix) duplicates[k]!.add(s.checksum);
            else if (first === undefined) added.push([key, prefix]);
            const twin = `${boxKey(s.bbox)}|${s.meshes.reduce((n, m) => n + m.indexCount, 0)}`;
            if (boxes.has(twin)) twins++;
            else boxes.add(twin);
        }
        for (const [key, prefix] of added) seen.set(key, prefix);
    });
    return { duplicates, twins };
}

// THP8 ships a floater (a sector named F_<...>: z_shops_F_G_TM_Shops830, F_VC_G_..., F_JAD_pile_of_leaves - "floaters":
// the geometry laid over a surface, the sign cells on a wall, the dirt and blend patches on the ground) beside the g_
// sector it was cut from, and the g_ one keeps the very same triangles: the sheet Z_Shops_g_TM_Shops830 and
// Z_Shops_F_G_TM_Shops830 have the same four vertices and the same material (only the light map coordinates differ -
// each has its own bake); the sign Z_Shops_g_TM_Shops808_to still has the layer 1e24b995 (the whole sign atlas on its
// 0-1 uv) its floater F_G_..._to draws as its cell. Other sectors come twice too (z_cartoon_VC_G_RM_MAINST_PIRATE_INSIDE
// / _G_VC_: one wall tessellated twice). THP8 draws both at one depth (the same vertices through the same shader);
// THUG2's triangulation and lighting differ per copy, so the two fight (the user's z-fighting in the shops alley).
// Here a mesh is left out when (nearly) all of its surface lies in the plane of, and inside, meshes of the same
// material drawn before it - facing the same way: a floater's first, then the larger meshes, then the earlier sectors.
// Sectors in `skip` (not made, instance masters and their instances, local objects) neither hide nor are hidden.
// `floaters`: the left-out meshes a floater's twin replaced.
export const REPEAT_PLANE = 0.003; // metres: how far off its plane a point may lie
export const REPEAT_SHARE = 0.95; // of a mesh's area that is covered
const REPEAT_CELL = 4; // metres
const REPEAT_SAMPLES: readonly (readonly [number, number, number])[] = [
    [1 / 3, 1 / 3, 1 / 3],
    [0.9, 0.05, 0.05],
    [0.05, 0.9, 0.05],
    [0.05, 0.05, 0.9],
    [0.45, 0.45, 0.1],
    [0.1, 0.45, 0.45],
    [0.45, 0.1, 0.45],
];

// A triangle with its plane (THP8 metres).
export interface SurfaceTri {
    p: [Vec3, Vec3, Vec3];
    n: Vec3;
    d: number;
    area: number;
}

export function surfaceTri(p: [Vec3, Vec3, Vec3]): SurfaceTri | null {
    const e1 = [p[1][0] - p[0][0], p[1][1] - p[0][1], p[1][2] - p[0][2]];
    const e2 = [p[2][0] - p[0][0], p[2][1] - p[0][1], p[2][2] - p[0][2]];
    const c = [e1[1]! * e2[2]! - e1[2]! * e2[1]!, e1[2]! * e2[0]! - e1[0]! * e2[2]!, e1[0]! * e2[1]! - e1[1]! * e2[0]!];
    const len = Math.hypot(c[0]!, c[1]!, c[2]!);
    if (len < 1e-9) return null;
    const n: Vec3 = [c[0]! / len, c[1]! / len, c[2]! / len];
    return { p, n, d: n[0] * p[0][0] + n[1] * p[0][1] + n[2] * p[0][2], area: len / 2 };
}

// Triangles drawn so far, looked up by place: which of a triangle's sample points lie on (in the plane of, and inside)
// one of them.
export class SurfaceIndex {
    // (grids of 4, 64 and 1024 m cells: a triangle goes in the finest where it touches few cells - a backdrop
    // kilometres across in the coarsest)
    private static readonly SIZES = [REPEAT_CELL, 64, 1024];
    private readonly grids = SurfaceIndex.SIZES.map(() => new Map<string, { t: SurfaceTri; tag: boolean }[]>());
    private readonly large: { t: SurfaceTri; tag: boolean }[] = [];

    // `tag`: marks the triangle (a floater's) for `covered`.
    add(t: SurfaceTri, tag = false): void {
        const lo = [0, 1, 2].map((k) => Math.min(t.p[0][k]!, t.p[1][k]!, t.p[2][k]!));
        const hi = [0, 1, 2].map((k) => Math.max(t.p[0][k]!, t.p[1][k]!, t.p[2][k]!));
        for (const [level, size] of SurfaceIndex.SIZES.entries()) {
            const c0 = lo.map((x) => Math.floor(x / size));
            const c1 = hi.map((x) => Math.floor(x / size));
            if ((c1[0]! - c0[0]! + 1) * (c1[1]! - c0[1]! + 1) * (c1[2]! - c0[2]! + 1) > 64) continue;
            for (let x = c0[0]!; x <= c1[0]!; x++)
                for (let y = c0[1]!; y <= c1[1]!; y++)
                    for (let z = c0[2]!; z <= c1[2]!; z++) {
                        const key = `${x},${y},${z}`;
                        const list = this.grids[level]!.get(key);
                        if (list) list.push({ t, tag });
                        else this.grids[level]!.set(key, [{ t, tag }]);
                    }
            return;
        }
        this.large.push({ t, tag });
    }

    // null: q isn't on a triangle (facing the way of `n`; `either`: or the opposite); true: on a tagged one; false: on
    // another.
    covered(q: readonly number[], n: readonly number[], either = false): boolean | null {
        const lists = SurfaceIndex.SIZES.map(
            (size, level) =>
                this.grids[level]!.get(
                    `${Math.floor(q[0]! / size)},${Math.floor(q[1]! / size)},${Math.floor(q[2]! / size)}`,
                ) ?? [],
        );
        lists.push(this.large);
        let by: boolean | null = null;
        for (const { t, tag } of lists.flat()) {
            const dot = t.n[0] * n[0]! + t.n[1] * n[1]! + t.n[2] * n[2]!;
            if (either ? Math.abs(dot) < 0.99 : dot < 0.99) continue;
            if (Math.abs(t.n[0] * q[0]! + t.n[1] * q[1]! + t.n[2] * q[2]! - t.d) > REPEAT_PLANE) continue;
            const [a, b, c] = t.p;
            // (inside, by the barycentric weights, a little over the edges)
            const v0 = [b[0] - a[0], b[1] - a[1], b[2] - a[2]];
            const v1 = [c[0] - a[0], c[1] - a[1], c[2] - a[2]];
            const v2 = [q[0]! - a[0], q[1]! - a[1], q[2]! - a[2]];
            const d00 = v0[0]! * v0[0]! + v0[1]! * v0[1]! + v0[2]! * v0[2]!;
            const d01 = v0[0]! * v1[0]! + v0[1]! * v1[1]! + v0[2]! * v1[2]!;
            const d11 = v1[0]! * v1[0]! + v1[1]! * v1[1]! + v1[2]! * v1[2]!;
            const d20 = v2[0]! * v0[0]! + v2[1]! * v0[1]! + v2[2]! * v0[2]!;
            const d21 = v2[0]! * v1[0]! + v2[1]! * v1[1]! + v2[2]! * v1[2]!;
            const den = d00 * d11 - d01 * d01;
            if (Math.abs(den) < 1e-12) continue;
            const v = (d11 * d20 - d01 * d21) / den;
            const w = (d00 * d21 - d01 * d20) / den;
            if (v >= -1e-3 && w >= -1e-3 && v + w <= 1 + 1e-3) {
                if (tag) return true;
                by = false;
            }
        }
        return by;
    }

    // The area of the triangles `tris` lying on drawn ones (sampled at 7 points each) and whether all of it is on
    // tagged ones.
    coverage(tris: readonly SurfaceTri[], either = false): { area: number; tagged: boolean } {
        let area = 0;
        let tagged = true;
        for (const t of tris) {
            const [a, b, c] = t.p;
            let hit = 0;
            for (const [wa, wb, wc] of REPEAT_SAMPLES) {
                const q = [0, 1, 2].map((k) => wa * a[k]! + wb * b[k]! + wc * c[k]!);
                const by = this.covered(q, t.n, either);
                if (by !== null) {
                    hit++;
                    if (!by) tagged = false;
                }
            }
            area += (t.area * hit) / REPEAT_SAMPLES.length;
        }
        return { area, tagged };
    }
}

export function repeatedSurfaces(
    zones: readonly { geom: geom.Geom; skip: ReadonlySet<number> }[],
    names: ReadonlyMap<number, string>,
): { hide: Set<geom.GeomMesh>[]; meshes: number; floaters: number } {
    const hide = zones.map(() => new Set<geom.GeomMesh>());
    const isFloater = (n: string): boolean => /^z_[a-z0-9]+?_f_/i.test(n);
    interface Candidate {
        zone: number;
        mesh: geom.GeomMesh;
        floater: boolean;
        tris: SurfaceTri[];
        area: number;
    }
    const candidates: Candidate[] = [];
    zones.forEach((z, zone) => {
        for (const s of z.geom.sectors) {
            if (z.skip.has(s.checksum)) continue;
            const name = names.get(s.checksum) ?? "";
            if (name.includes("__MASTERNODE_")) continue;
            for (const mesh of s.meshes) {
                const verts = geom.readVertices(mesh);
                const tris = geom
                    .triangles(mesh)
                    .map((t) => surfaceTri(t.map((i) => verts[i]!.position) as [Vec3, Vec3, Vec3]))
                    .filter((t): t is SurfaceTri => t !== null);
                if (tris.length) {
                    candidates.push({
                        zone,
                        mesh,
                        floater: isFloater(name),
                        tris,
                        area: tris.reduce((n, t) => n + t.area, 0),
                    });
                }
            }
        }
    });
    // (floaters first, then the larger; a stable sort keeps the zone and sector order within each)
    candidates.sort((a, b) => Number(b.floater) - Number(a.floater) || b.area - a.area);
    const drawn = new Map<number, SurfaceIndex>(); // by material
    let meshes = 0;
    let floaters = 0;
    for (const c of candidates) {
        const index = drawn.get(c.mesh.material) ?? new SurfaceIndex();
        drawn.set(c.mesh.material, index);
        const cover = index.coverage(c.tris);
        if (cover.area >= REPEAT_SHARE * c.area) {
            hide[c.zone]!.add(c.mesh);
            meshes++;
            if (!c.floater && cover.tagged) floaters++;
            continue;
        }
        for (const t of c.tris) index.add(t, c.floater);
    }
    return { hide, meshes, floaters };
}

// The scene parts of a zone: the zone's own, then its props' models (each drawn under its node's name, in its own
// space).
function sceneParts(p: ZonePart, ctx: Shared): SceneParts[] {
    const g = p.data.geom!;
    return [
        {
            geom: g,
            textures: p.data.textures,
            placed: p.placed,
            hidden: new Set([...p.hidden, ...p.duplicate]),
            hiddenMeshes: p.hiddenMeshes,
            additive: glowMaterials(g, ctx.names),
            lighting: zoneLighting(zoneTod(ctx.managers, p.zoneName)),
            techniques: ctx.techniques,
            local: p.local,
        },
        ...[...new Set(p.made.values())].map(
            (type): SceneParts => ({
                geom: p.props.get(type)!.geom,
                textures: p.props.get(type)!.textures,
                placed: new Map(
                    [...p.made]
                        .filter(([, t]) => t === type)
                        .map(([node]) => [
                            node,
                            {
                                master: p.props.get(type)!.geom.sectors[0]!.checksum,
                                at: { pos: [0, 0, 0], angles: [0, 0, 0], scale: [1, 1, 1] },
                            },
                        ]),
                ),
                local: new Set(p.made.keys()),
            }),
        ),
    ];
}

// A stand-in archive's instance placements (its node array's CreatedAtStart instances, as readPart takes a zone's): the
// _lod archives hold instance masters too (__MASTERNODE_ sectors), which stood stacked at THP8's origin while their
// instances weren't drawn.
export function standInPlaced(nqb: thaw.QbItems | null, zone: string): Map<number, { master: number; at: Placement }> {
    const placed = new Map<number, { master: number; at: Placement }>();
    if (nqb === null || !nqb.globals.some(([n]) => n.value === checksum(`${zone}_NodeArray`))) return placed;
    const array = nodeArrayOf(nqb, zone);
    for (const [k, v] of instances(array)) {
        const node = array.find((n) => n instanceof Struct && nodeName(n) === k) as Struct | undefined;
        if (node?.hasFlag("CreatedAtStart")) placed.set(k, v);
    }
    return placed;
}

// Whether a stand-in's triangle (THP8 metres) lies on the surfaces the level draws (its opaque triangles so far, either
// way round): the backdrop z_factory_lod's TK_G_Industrial_Map_01 over the level's TK_G_Industrial_Map pieces, the same
// corners in other strips - drawn twice they fight, and the level's own is the one to keep.
export function standInOnDrawn(corners: readonly (readonly number[])[], drawn: SurfaceIndex): boolean {
    const t = surfaceTri(corners as [Vec3, Vec3, Vec3]);
    return t !== null && drawn.coverage([t], true).area >= REPEAT_SHARE * t.area;
}

// Whether a stand-in's triangle (THP8 metres) is clear of the level's own ground: no point of it, looked at every
// STAND_IN_STEP metres or closer (corners, edges and inside), over ground of the level's zones (at any height). (Some
// stand-ins are huge: z_center_lod's lake plane is two triangles 900 m across.)
export const STAND_IN_STEP = 4;
export function standInOutside(corners: readonly (readonly number[])[], ground: GroundIndex): boolean {
    const [a, b, c] = corners as [readonly number[], readonly number[], readonly number[]];
    const edge = (p: readonly number[], q: readonly number[]): number => Math.hypot(p[0]! - q[0]!, p[2]! - q[2]!);
    const n = Math.max(1, Math.min(400, Math.ceil(Math.max(edge(a, b), edge(b, c), edge(c, a)) / STAND_IN_STEP)));
    for (let i = 0; i <= n; i++) {
        for (let j = 0; i + j <= n; j++) {
            const u = i / n;
            const v = j / n;
            const x = a[0]! + u * (b[0]! - a[0]!) + v * (c[0]! - a[0]!);
            const z = a[2]! + u * (b[2]! - a[2]!) + v * (c[2]! - a[2]!);
            if (ground.groundAt(x, z, -1e9, 1e9)) return false;
        }
    }
    return true;
}

// The level's furthest reach from its middle on x / z (inches), its collision moved by `shift`.
export function horizontalReach(bodies: readonly havok.HkBody[], shift: readonly number[]): number {
    let reach = 0;
    for (const b of bodies) {
        for (const t of b.triangles) {
            for (const p of [t.a, t.b, t.c]) {
                const q = toThug(p, shift);
                reach = Math.max(reach, Math.abs(q[0]), Math.abs(q[2]));
            }
        }
    }
    return reach;
}

// One level: its zones' scenes, collision and node arrays as one, in THP8's shared frame moved so the middle of the
// collision is at the origin. Returns its manifest entry, or null (skipped).
function convertLevel(spec: LevelSpec, ctx: Shared): Record<string, unknown> | null {
    const { names, report } = ctx;
    const warnings: string[] = [];
    const parts: ZonePart[] = [];
    for (const zone of spec.zones) {
        const p = readPart(zone, ctx, warnings);
        if (p === null) {
            report.push(
                `${spec.level}: ${zone} not in ${ctx.compressed}\\ZONES or without a scene / node array - skipped`,
            );
            return null;
        }
        parts.push(p);
    }
    const barriers: ZoneData[] = [];
    for (const area of spec.barriers) {
        const file = zonePak(ctx.compressed, area);
        const b = file ? readZone(file, names, warnings) : null;
        if (b?.geom) barriers.push(b);
        else report.push(`${spec.level}: ${area} not found - no barriers`);
    }
    const name = spec.level;
    const allBodies = parts.flatMap((p) => p.data.bodies);
    const shift = centreShift(allBodies);
    const tod = zoneTod(ctx.managers, parts[0]!.zoneName);
    // (a level of the city: the city's other zones as THP8 shows them from afar - their _lod stand-ins, drawn only -
    // instead of the void beyond the barriers. A stand-in also covers places of other zones - z_citypark_lod's Box03
    // stands where the factory is, z_center_lod's LakeKill_02 is a lake plane reaching over the factory's floor: the
    // stand-ins' triangles over the level's own ground are left out, standInOutside)
    const lods: SceneParts[] = [];
    const ownGround = new GroundIndex(allBodies);
    if (spec.zones.some((z) => ZONES.some((x) => x.zone === z && !x.standalone))) {
        for (const info of ZONES.filter((x) => !x.standalone && !spec.zones.includes(x.zone))) {
            const file = zonePak(ctx.compressed, `${info.zone}_lod`);
            const lod = file ? readZone(file, names, warnings) : null;
            if (!lod?.geom) continue;
            lods.push({
                geom: lod.geom,
                textures: lod.textures,
                placed: standInPlaced(lod.nqb, `${info.zone}_lod`),
                additive: glowMaterials(lod.geom, names),
                lighting: zoneLighting(zoneTod(ctx.managers, names.get(checksum(info.zone)) ?? info.zone)),
                techniques: ctx.techniques,
                keepTriangle: (corners, drawn) => standInOutside(corners, ownGround) && !standInOnDrawn(corners, drawn),
            });
        }
    }
    // (the network barriers drawn too, as THP8 does - a faint wall: VEIL_COLOUR)
    for (const b of barriers) {
        lods.push({
            geom: b.geom!,
            textures: b.textures,
            placed: new Map(),
            veil: true,
        });
    }
    // (zones merged: a later zone's sector that an earlier zone draws under its own prefix is left out of the scene)
    const dup = zoneDuplicates(
        parts.map((p) => p.data),
        names,
    );
    for (const [k, p] of parts.entries()) p.duplicate = dup.duplicates[k]!;
    const dupCount = dup.duplicates.reduce((n, x) => n + x.size, 0);
    if (dupCount) report.push(`  ${dupCount} sectors a later zone repeats from an earlier one left out of the scene`);
    const rep = repeatedSurfaces(
        parts.map((p) => ({
            geom: p.data.geom!,
            skip: new Set([
                ...p.hidden,
                ...p.duplicate,
                ...p.local,
                ...p.made.keys(),
                ...p.placed.keys(),
                ...[...p.placed.values()].map((x) => x.master),
            ]),
        })),
        names,
    );
    for (const [k, p] of parts.entries()) p.hiddenMeshes = rep.hide[k]!;
    if (rep.meshes) {
        report.push(
            `  ${rep.meshes} meshes drawing a surface another sector draws too (THP8's floaters F_ over their g_ twin, ` +
                `VC_ / G_ twins: same material, same triangles) left out of the scene, ${rep.floaters} of them for a floater's`,
        );
    }
    const scene = (options: mats.RefineOptions): ConvertedScene =>
        convertScenes(
            [...parts.flatMap((p) => sceneParts(p, ctx)), ...lods],
            shift,
            warnings,
            !ctx.args.noMips,
            SCALE,
            options,
        );
    // (refining the light keeps to a budget: the threshold raised until the level fits)
    let refineOptions = { ...REFINE };
    let done = scene(refineOptions);
    for (let tries = 0; tries < 4 && done.counts.triangles > refineBudget(done.counts, parts.length); tries++) {
        refineOptions = {
            ...refineOptions,
            threshold: refineOptions.threshold * 1.6,
        };
        done = scene(refineOptions);
    }
    if (refineOptions.threshold !== REFINE.threshold) {
        report.push(`  light refined at a threshold of ${refineOptions.threshold.toFixed(2)} (the triangle budget)`);
    }
    // (THP8 draws its decals with a depth bias of its own, not decoded: the layers lying on other surfaces get a z bias
    // here, or they shimmer - level_port/overlay_bias.ts, as THPS3 / THPS4)
    const biased = overlayBias.biasOverlays(done.scene);
    if (biased) report.push(`  ${biased} layer materials given a z bias over the surfaces under them`);
    // nodes (the kill zones' trigger faces kept): the zones' arrays as one, a fresh copy (zoneNodes makes it THUG2's)
    const inLevel = new Set(spec.zones);
    const scriptQbs = parts.flatMap((p) => [p.data.nqb!, ...p.data.qbs]);
    const moreQbs = [
        ...parts.flatMap((p) => p.data.subQbs),
        ...ZONES.filter((x) => !inLevel.has(x.zone)).flatMap((x) => ctx.zoneScripts.of(x.zone)),
    ];
    const props = new Map(parts.flatMap((p) => [...p.props]));
    const nodeResult = zoneNodes(
        mergeNodeArrays(parts.map((p) => nodeArrayOf(p.data.nqb!, p.zoneName))),
        name,
        names,
        shift,
        done.sectors,
        scriptQbs,
        allBodies,
        ctx.flags,
        ctx.port,
        moreQbs,
        props,
    );
    // collision: each zone's bodies (but the invisible walls at its seams with the level's other zones: seamWalls),
    // its props' boxes (in their own space), the barriers
    const seams = seamWalls(parts.map((p) => p.data.bodies));
    const sets: Parameters<typeof convertCollision>[0][number][] = [];
    parts.forEach((p, k) => {
        const g = p.data.geom!;
        sets.push({
            bodies: p.data.bodies,
            sectors: g.sectors,
            hidden: p.hidden,
            local: p.local,
            volumes: triggerVolumes(p.array, names, new Set(g.sectors.map((x) => x.checksum))),
            drop: seams[k]!,
        });
        sets.push({
            bodies: [...p.made].map(([node, type], i) => ({
                index: -1 - i,
                triangles: boxTriangles(p.props.get(type)!.box, node),
            })),
            sectors: [],
            hidden: new Set(),
            local: new Set(p.made.keys()),
            volumes: [],
        });
    });
    const seamCount = seams.reduce((n, x) => n + x.size, 0);
    if (seamCount) {
        report.push(
            `  ${seamCount} invisible walls at the seams between its zones left out (${parts.map((p, k) => `${p.zone} ${seams[k]!.size}`).join(", ")})`,
        );
    }
    // (the network barriers: collision only - THP8 draws them as a shimmering wall when the skater is near)
    for (const b of barriers) {
        sets.push({
            bodies: b.bodies,
            sectors: b.geom!.sectors,
            hidden: new Set(),
            local: new Set(),
            volumes: [],
        });
    }
    const col = convertCollision(sets, name, shift, ctx.terrains, (c) => nodeResult.triggers.has(c), warnings);
    // (THUG2's line test gathers a BSP leaf's faces into a buffer of 0x800 - CCollStatic::CollideWithLine: a bigger
    // leaf overran it and crashed the game, round 4's town-wide fill object)
    const leafMost = (n: thug2Col.BspNode): number =>
        n.kind === "leaf" ? n.faces.length : Math.max(leafMost(n.left), leafMost(n.right));
    // (drawn ground with nothing to stand on - not counting the gap filler's floors)
    const audit = uncoveredGround(done.ground, col.col.objects, (c) => col.fillObjects.has(c));
    const square = (cells: number): string => ((cells * AUDIT_CELL * AUDIT_CELL) / (SCALE * SCALE)).toFixed(0);
    report.push(
        `  holes - drawn ground with no collision under it, ringed by ground that has: ${square(audit.cells)} m2 in ${audit.cells} cells` +
            (audit.patches.length
                ? ` (largest: ${audit.patches
                      .filter((pt) => Number(square(pt.cells)) >= 25)
                      .slice(0, 16)
                      .map(
                          (pt) =>
                              `${square(pt.cells)} m2 at ${pt.at.map((x) => x.toFixed(0)).join(",")} ${names.get(pt.sector) ?? thp8.hex(pt.sector)}`,
                      )
                      .join("; ")})`
                : ""),
    );
    for (const o of col.col.objects) {
        if (leafMost(o.bsp) > LEAF_MOST)
            warnings.push(`collision object ${thp8.hex(o.checksum)}: a BSP leaf of more than ${LEAF_MOST} faces`);
    }
    const levelDir = pyJoin(ctx.out, "levels", name);
    common.writeLevel(levelDir, name, done.scene, done.textures, col.col);
    common.reportLevel(report, name, done.scene, done.textures, col.col, [...new Set(warnings)].sort());
    const c = done.counts;
    const made = parts.flatMap((p) => [...p.made.values()]);
    if (made.length) {
        report.push(
            `  ${made.length} props as bouncy objects (${[...new Set(made.map((t) => names.get(t)))].join(", ")})`,
        );
    }
    const reach = horizontalReach(allBodies, shift);
    const big = spec.big ?? reach > NET_HALF_REACH;
    report.push(
        `  ${spec.zones.join(" + ")}${spec.barriers.length ? ` + ${spec.barriers.join(" + ")}` : ""}: ${c.sectors} sectors, ` +
            `${c.instances} instances of ${c.masters} masters, ${c.billboards} billboard meshes left out, ` +
            `${c.triangles} triangles, ${c.layers} further layers, ${c.lit} vertices lit from light maps (${c.refined} lit triangles after refining); collision ${col.objects.size} objects (${col.filled} faces filling narrow holes; ${col.shells} invisible shells cut under their roofs; ${col.named} ` +
            `named after their sector / node; ${col.hidden} of nodes not made left out); ` +
            `${parts.reduce((n, p) => n + p.flagged, 0)} nodes made from THP8's non-story node flags, ` +
            `${parts.reduce((n, p) => n + p.hidden.size, 0)} not made; moved by (${shift.map((x) => x.toFixed(1)).join(", ")}); ` +
            `reaches ${reach.toFixed(0)} units from the middle${big ? " (IsBigLevel: network positions in whole units)" : ""}`,
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
    common.nsCompile(ctx.args.ns, text, qbFile);
    if (nodeResult.scripts.size) {
        fs.writeFileSync(qbFile, scriptPort.appendScripts(fs.readFileSync(qbFile), nodeResult.scripts, names));
    }
    if (nodeResult.scriptReport) report.push(`  ${nodeResult.scriptReport}`);
    report.push(
        `  nodes: ${nodeResult.restarts} restarts${nodeResult.added ? " (one made: it had none)" : ""}, ` +
            `${nodeResult.rails} rail nodes, ${nodeResult.kills} kill zones ` +
            `(${[...nodeResult.stats].map(([k, v]) => `${k} ${v}`).join(", ")})`,
    );
    return {
        folder: spec.folder,
        level: name,
        sky: ctx.sky,
        display: `THP8 ${spec.sign}`,
        name: spec.sign.toLowerCase().replace(/\b\w/g, (ch) => ch.toUpperCase()),
        game: "THP8",
        location: spec.location,
        icon: "sign_training",
        level_num: spec.levelNum,
        default_stats: 6,
        lighting: tod ? common.lighting(tod) : {},
        ...(big ? { flags: ["IsBigLevel"] } : {}),
    };
}

// Every level this converter makes: the zones', then THP8's network levels of several zones.
// Every level this converter makes: THP8's standalone levels (ZoneInfo.standalone), then its network levels - BEAVER
// VALLEY (the whole town) and the twelve areas. The city's zones aren't levels by themselves (until 2026-10-24 they
// were: our own split, cut off at the zone's edge - RETIRED).
export function allSpecs(compressed: string, names: Map<number, string>): LevelSpec[] {
    const areaNames = zoneNames(compressed, names);
    const standalone = ZONES.filter((z) => z.standalone).map((z) => zoneSpec(z, areaNames));
    return [...standalone, ...netSpecs(networkAreas(compressed, names), new Set(standalone.map((z) => z.sign)))];
}

// The levels this converter made before that it doesn't any more (the city's zones as levels of their own): their
// staging folders and levels.json entries are removed by convert.
export const RETIRED: readonly string[] = ZONES.filter((z) => !z.standalone).map(levelName);

export function findSpec(specs: readonly LevelSpec[], name: string): LevelSpec | undefined {
    const n = name.toLowerCase();
    const zone = findZone(n);
    if (zone) return specs.find((s) => s.folder === zone.zone);
    return specs.find((s) => s.level === n || s.folder === n || s.level === `thp8_net_${n}` || s.level === `thp8_${n}`);
}

// levels.json without the entries of levels this converter no longer makes (RETIRED, or not in `specs` at all), and
// their staging folders deleted. Returns their names.
export function pruneStaging(out: string, specs: readonly LevelSpec[]): string[] {
    const file = pyJoin(out, portedLevels.MANIFEST);
    const made = new Set(specs.map((x) => x.level.toLowerCase()));
    const old = portedLevels.readManifest(file);
    const gone = new Set<string>(RETIRED);
    for (const e of old?.levels ?? []) {
        const level = String(e.level).toLowerCase();
        if (level !== SKY && !made.has(level)) gone.add(level);
    }
    const removed: string[] = [];
    for (const level of gone) {
        const dir = pyJoin(out, "levels", level);
        const listed = (old?.levels ?? []).some((e) => String(e.level).toLowerCase() === level);
        if (fs.existsSync(dir) || listed) removed.push(level);
        fs.rmSync(dir, { recursive: true, force: true });
    }
    if (old) {
        old.levels = old.levels.filter((e) => !gone.has(String(e.level).toLowerCase()));
        fs.writeFileSync(file, JSON.stringify(old, null, 1));
    }
    return removed.sort();
}

export function cmdConvert(args: Args): number {
    const compressed = compressedFolder(args.thp8);
    const out = args.out || pyJoin(args.thug2, STAGING);
    if (!fs.existsSync(args.ns)) pyExit(`${args.ns} not found: build it with tools\\build_neverscript.ps1`);
    const names = thp8.loadNames(findCaseless(compressed, "PAK", "dbg.pak.xen")!);
    const specs = allSpecs(compressed, names);
    const wanted: LevelSpec[] = [];
    for (const name of args.levels.length ? args.levels : specs.map((s) => s.level)) {
        const spec = findSpec(specs, name);
        if (spec === undefined)
            pyExit(`${name}: not a THP8 zone or network level (${specs.map((s) => s.level).join(", ")})`);
        wanted.push(spec);
    }
    const library = findCaseless(compressed, "FXFILES", "MaterialLibrary.bin.xen");
    const globalPak = zonePak(compressed, "global");
    // (only the levels converted now are made again: a partial convert keeps the others, as levels.json does -
    // ported_levels.writeManifest)
    for (const spec of wanted)
        fs.rmSync(pyJoin(out, "levels", spec.level), {
            recursive: true,
            force: true,
        });
    // (the city's zones as levels of their own, from before: gone - make_menu_level's overlay is made again from the
    // staging folders, so they leave it on its next run)
    const pruned = pruneStaging(out, specs);
    fs.mkdirSync(out, { recursive: true });
    const report: string[] = [];
    const ctx: Shared = {
        args,
        compressed,
        out,
        names,
        terrains: terrainTable(compressed, names),
        flags: nonStoryNodeFlags(compressed, names),
        managers: todManagers(compressed, names),
        techniques: library ? mats.readTechniques(thp8.unpack(library)) : new Map<number, string[]>(),
        port: makeScriptPort(args.thug2, compressed, names),
        zoneScripts: new ZoneScripts(compressed, names),
        globalProps: globalPak ? propModels(thp8.openPak(globalPak)) : new Map<number, PropModel>(),
        sky: convertSky(compressed, out, report, !args.noMips, args.leaveOut, names),
        report,
    };
    const manifest: { levels: Record<string, unknown>[] } = { levels: [] };
    for (const spec of wanted) {
        const entry = convertLevel(spec, ctx);
        if (entry) manifest.levels.push(entry);
        // (the report as it goes: the whole town takes minutes)
        print(report.splice(0).join("\n"));
    }
    portedLevels.writeManifest(pyJoin(out, portedLevels.MANIFEST), manifest);
    if (pruned.length) print(`removed (no longer made): ${pruned.join(", ")}`);
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
    const names = thp8.loadNames(findCaseless(compressed, "PAK", "dbg.pak.xen")!);
    for (const spec of allSpecs(compressed, names)) {
        const missing = spec.zones.filter((z) => zonePak(compressed, z) === null);
        print(
            `${spec.level.padEnd(26)} ${String(spec.levelNum).padEnd(4)} ${spec.sign.padEnd(20)} ${[...spec.zones, ...spec.barriers].join(" + ")}${missing.length ? ` (missing ${missing.join(", ")})` : ""}`,
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
