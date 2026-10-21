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
import * as portedLevels from "../level_port/ported_levels.ts";
import * as qb from "../level_port/qb.ts";
import { checksum, Name, type QbValue, Struct, Vector } from "../level_port/qb.ts";
import type * as tex from "../level_port/tex.ts";
import * as thug2Col from "../level_port/thug2_col.ts";
import * as thug2Scene from "../level_port/thug2_scene.ts";
import { parseCli } from "../lib/cli.ts";
import { findCaseless, resolveInstall } from "../lib/install.ts";
import { isMain, print, pyExit, pyJoin, runMain } from "../lib/py.ts";
import * as thaw from "../thaw_levels/thaw.ts";
import { applyNodeFlags, nodeFlagScript } from "../thaw_levels/thaw_levels.ts";
import { makeMaterial, THUG2_TERRAIN_TYPES, trisToStrip } from "../thps2_levels/thps2_levels.ts";
import * as geom from "./geom.ts";
import * as havok from "./havok.ts";
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

// The sectors of LevelGeometry nodes left without CreatedAtStart (THUG2 never makes them: their geometry isn't drawn or
// collided with) - left out of the scene and collision rather than kept unseen.
export function notMade(array: readonly QbValue[], names: ReadonlyMap<number, string>): Set<number> {
    const out = new Set<number>();
    for (const n of array) {
        if (!(n instanceof Struct) || nodes.classOf(n, names) !== "levelgeometry") continue;
        const name = nodeName(n);
        if (name !== null && !n.hasFlag("CreatedAtStart") && n.get("MasterName") === undefined) out.add(name);
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
): ConvertedScene {
    const materials = new Map<number, thug2Scene.Material>();
    const texOut = new Map<number, tex.Texture>();
    const transparent = new Set<number>();
    const billboards = new Set<number>();
    const glows = new Map<number, number[]>(); // additive materials -> their R, G, B scale (glowScale)
    const sectors: geometry.SourceSector[] = [];
    const written = new Set<number>();
    const counts = { sectors: 0, instances: 0, masters: 0, triangles: 0, skipped: 0, hidden: 0, billboards: 0 };
    for (const part of parts) {
        const g = part.geom;
        // materials
        for (const m of g.materials) {
            if (materials.has(m.checksum) || !m.checksum) continue;
            if (m.template === BILLBOARD) billboards.add(m.checksum);
            let texture = 0;
            // (the first texture that isn't a normal map: a few shaders list theirs first)
            const first = m.textures.find(
                (t) => part.textures.has(t) && part.textures.get(t)!.format !== xenos.FMT_DXN,
            );
            if (first !== undefined) {
                if (!texOut.has(first)) {
                    const c = xenos.toThug2(part.textures.get(first)!, mips);
                    if (c.texture) texOut.set(first, c.texture);
                    else warnings.push(`texture ${thp8.hex(first)}: ${c.note} - left out`);
                }
                if (texOut.has(first)) texture = first;
            }
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
            materials.set(m.checksum, mat);
        }
        const bySector = new Map(g.sectors.map((s) => [s.checksum, s]));
        const masters = new Set([...part.placed.values()].map((p) => p.master));
        const emit = (checksumOut: number, s: geom.GeomSector, at: Placement | null): void => {
            const positions: number[][] = [];
            const colors: number[][] = [];
            const uvs: number[][] = [];
            const meshes: { flags: number; material: number; strip: number[] }[] = [];
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
                for (const v of geom.readVertices(m)) {
                    const q = place(v.position, at);
                    positions.push([q[0] * unit + shift[0]!, q[1] * unit + shift[1]!, q[2] * unit + shift[2]!]);
                    if (dome) {
                        colors.push(skyColour(v.position));
                        uvs.push([0, 0]);
                        continue;
                    }
                    // (255 = full as THUG2's 128; opaque materials' vertex alpha - THP8's layer blend weights - full)
                    const [b, g2, r, a] = v.color as [number, number, number, number];
                    // (the sky's cloud layers faded out towards the horizon: THP8's city and haze hide their low
                    // edges, which in THUG2's small sky stood up as grey spikes)
                    const fade = part.dome ? cloudFade(v.position) : 1;
                    const k = glow ?? [1, 1, 1];
                    colors.push([
                        Math.round((b >> 1) * k[2]!),
                        Math.round((g2 >> 1) * k[1]!),
                        Math.round((r >> 1) * k[0]!),
                        blend ? Math.round((a >> 1) * fade) : 128,
                    ]);
                    uvs.push(v.uvs[0] ?? [0, 0]);
                }
                let tris = geom.triangles(m).map((t) => t.map((i) => i + base));
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
                uvSets: 1,
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

// --- collision ----------------------------------------------------------------------------------------------------

// THP8's face flags that mean the same in THUG2 (skatable, not skatable, wallridable, vert, not collidable, decal,
// trigger, camera passes, no skater shadow, skater shadow, no shadow wall, under ok, invisible).
export const FACE_KEEP = 0x1fff;

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
    sets: readonly { bodies: havok.HkBody[]; sectors: readonly geom.GeomSector[]; hidden?: ReadonlySet<number> }[],
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
    for (const { bodies, sectors, hidden } of sets) {
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
                name = owner !== undefined && !used.has(owner) ? owner : checksum(`${level}_Col_${n}`);
                if (owner !== undefined && name === owner) named++;
            }
            n++;
            if (hidden?.has(name)) {
                hiddenBodies++;
                continue;
            }
            used.add(name);
            const verts: number[][] = [];
            const index = new Map<string, number>();
            const vertex = (p: readonly number[]): number => {
                const q = toThug(p, shift);
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
                faces.push([t.material & FACE_KEEP, thug2Terrain(terrains, (t.material >>> 16) & 0xff), a, b, c]);
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
): NodeResult {
    const key = checksum(`${zone}_NodeArray`);
    const raw = nqb.globals.find(([n]) => n.value === key)?.[1];
    if (!Array.isArray(raw)) pyExit(`${zone}: no ${zone}_NodeArray`);
    const globals = new Map(nqb.globals.map(([k, v]) => [k.value, v]));
    const array = nodes.expandIncludes(raw, globals);
    applyNodeFlags(array, flags);
    const restartNames = new Set<number>();
    for (const n of array) {
        const v = n instanceof Struct ? nodeName(n) : null;
        if (v !== null && nodes.classOf(n as Struct, names) === "restart") restartNames.add(v);
    }
    let kills = nodes.killCommands(scriptQbs, restartNames, names);
    const [converted, stats] = nodes.convertNodes(array, names, kills, ["pos"]);
    // positions: metres -> inches
    const POS = checksum("Pos");
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
        node.entries = node.entries.map(([k, v]) =>
            k !== null && k.value === POS && v instanceof Vector && v.kind === "vector"
                ? [k, new Vector("vector", toThug(v.values, shift))]
                : [k, v],
        );
    }
    // (LevelGeometry nodes whose sector isn't drawn - collision-only pieces, masters - lose TrickObject / Cluster:
    // stripMissingTrickObjects, on the text)
    const added = addRestart(converted, bodies, level, shift, names);
    kills = nodes.addKillRestarts(converted, kills, names);
    const triggers = nodes.killTriggerNames(converted, kills);
    let text = nodes.nodeArrayNs(`${level}_NodeArray`, converted, names, kills);
    [text] = nodes.stripMissingTrickObjects(text, sectors);
    const count = (cls: string): number => converted.filter((n) => nodes.classOf(n, names) === cls).length;
    return {
        text,
        stats,
        kills: triggers.size,
        restarts: count("restart"),
        rails: count("railnode"),
        triggers,
        added,
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
    };
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
        const hidden = notMade(array, names);
        if (args.leaveOut) {
            for (const s of z.geom.sectors) if (args.leaveOut.test(names.get(s.checksum) ?? "")) hidden.add(s.checksum);
        }
        // (the network barriers: collision only - THP8 draws them as a shimmering wall when the skater is near)
        const done = convertScenes(
            [{ geom: z.geom, textures: z.textures, placed, hidden, additive: glowMaterials(z.geom, names) }],
            shift,
            warnings,
            !args.noMips,
        );
        // nodes (the kill zones' trigger faces kept)
        const scriptQbs = [z.nqb, ...z.qbs];
        const nodeResult = zoneNodes(z.nqb, zoneName, name, names, shift, done.sectors, scriptQbs, z.bodies, flags);
        // collision
        const sets = [{ bodies: z.bodies, sectors: z.geom.sectors, hidden }];
        if (online?.geom) sets.push({ bodies: online.bodies, sectors: online.geom.sectors, hidden: new Set() });
        const col = convertCollision(sets, name, shift, terrains, (c) => nodeResult.triggers.has(c), warnings);
        const levelDir = pyJoin(out, "levels", name);
        common.writeLevel(levelDir, name, done.scene, done.textures, col.col);
        common.reportLevel(report, name, done.scene, done.textures, col.col, [...new Set(warnings)].sort());
        const c = done.counts;
        report.push(
            `  ${info.zone}${online ? ` + ${info.online}` : ""}: ${c.sectors} sectors, ${c.instances} instances of ` +
                `${c.masters} masters, ${c.billboards} billboard meshes left out, ` +
                `${c.triangles} triangles; collision ${col.objects.size} objects (${col.named} ` +
                `named after their sector / node; ${col.hidden} of nodes not made left out); ${flagged} nodes made ` +
                `from THP8's non-story node flags, ${hidden.size} not made; moved by (${shift.map((x) => x.toFixed(1)).join(", ")})`,
        );
        common.nsCompile(args.ns, nodeResult.text, pyJoin(levelDir, `${name}.qb`));
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
            lighting: {},
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
