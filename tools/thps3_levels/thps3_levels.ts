// NOT ORIGINAL - converts Tony Hawk's Pro Skater 3 (PC) levels to THUG2's formats so that THUG2 can load and play
// them (see README.md). Nothing of either game is stored in the repo: the files are made from the user's own copies.
//
//   node tools/thps3_levels/thps3_levels.ts selfcheck [--thug2 C:\Games\THUG2] [--thps3 C:\Games\THPS3]
//   node tools/thps3_levels/thps3_levels.ts convert [levels...] [--thug2 ...] [--thps3 ...] [--out <folder>]
//   then: node tools/menu_level/make_menu_level.ts   (installs them into the data overlay, with level select signs)
//
// convert writes <thug2>\thug2-decomp-thps3\ (not read by the game): levels\<level>\<level>.scn.xbx (levelName) /
// .tex.xbx / .col.xbx / .qb (the node array), the skies (scene and textures) and levels.json (what
// make_menu_level.ts adds to Levels.qb and the level select: tools/level_port/ported_levels.ts).
//
// THPS3's own formats (RenderWare worlds, texture dictionaries, Neversoft's plugin data) are read by thps3.ts; the
// THUG2 writers, the geometry conversion, the node arrays and the installing are shared with tools/thps4_levels and
// tools/thug1_levels (tools/level_port).

import * as fs from "node:fs";
import * as path from "node:path";
import * as compat from "../compat/compat.ts";
import { FormatError } from "../level_port/binio.ts";
import * as common from "../level_port/common.ts";
import * as geometry from "../level_port/geometry.ts";
import * as movers from "../level_port/movers.ts";
import * as nodes from "../level_port/nodes.ts";
import * as overlayBias from "../level_port/overlay_bias.ts";
import * as pedRigs from "../level_port/ped_rigs.ts";
import * as pedsCars from "../level_port/peds_cars.ts";
import * as portedLevels from "../level_port/ported_levels.ts";
import * as prx from "../level_port/prx.ts";
import * as qb from "../level_port/qb.ts";
import { Name, type QbFile, type QbValue, type Statement, Struct, Vector } from "../level_port/qb.ts";
import * as scriptPort from "../level_port/script_port.ts";
import type * as tex from "../level_port/tex.ts";
import type * as thug2Col from "../level_port/thug2_col.ts";
import * as thug2Scene from "../level_port/thug2_scene.ts";
import { resolveInstall } from "../lib/install.ts";
import {
    compareStr,
    decodeLatin1,
    Float,
    hex8,
    isMain,
    print,
    pyExit,
    pyFloatRepr,
    pyJoin,
    pyMax,
    pyMin,
    pyMod,
    pyRepr,
    pyRound,
    pyTruthy,
    roundHalfEven,
    runMain,
} from "../lib/py.ts";
import { StructError, unpackNumbers } from "../lib/struct.ts";
import * as thps3 from "./thps3.ts";
import { ROTATION_REWRITES } from "./thps3_rotation.ts";
import * as thugproNew from "./thugpro_new.ts";

export const STAGING = "thug2-decomp-thps3";

// The THPS3 levels: folder under Data\Levels, sign text, real-world place, THUG2 sign texture to use as an icon
// (the level select's own table in tools/menu_level/level_select.ts takes precedence), level select number (each its
// own; no career records - the info panel skips them), converted by default.
export type LevelRow = [string, string, string, string, number, boolean];
export const LEVELS: readonly LevelRow[] = [
    ["Foun", "FOUNDRY", "Foundry (no city named)", "sign_philly", 60, true],
    ["Can", "CANADA", "British Columbia, Canada", "sign_canada", 61, true],
    ["Rio", "RIO", "Rio de Janeiro, Brazil", "sign_barcelona", 62, true],
    ["Sub", "SUBURBIA", "Suburbia, USA", "sign_neworleans", 63, true],
    ["Ap", "AIRPORT", "Airport", "sign_airport", 64, true],
    ["SI", "SKATER ISLAND", "Skater Island, Southern California", "sign_australia", 65, true],
    ["LA", "LOS ANGELES", "Los Angeles, USA", "sign_la", 66, true],
    ["Tok", "TOKYO", "Tokyo, Japan", "sign_airport", 67, true],
    ["Shp", "CRUISE SHIP", "Cruise ship", "sign_jam", 68, true],
    ["Ware", "WAREHOUSE", "Warehouse, Woodland Hills, California", "sign_training", 69, true],
    ["Burn", "BURNSIDE", "Burnside, Portland, Oregon", "sign_skatopia", 70, true],
    ["Ros", "ROSWELL", "Roswell, New Mexico", "sign_airport", 71, true],
    ["Tut", "TUTORIALS", "Tutorials (no place)", "sign_training", 72, true],
    ["SkateShop", "SKATE SHOP", "THPS3's skate shop (no place)", "sign_training", 73, true],
    ["Sk3Ed_Bch", "PARK BEACH", "Park editor: beach", "sign_training", 74, false],
    ["Sk3Ed_Indr", "PARK INDOOR", "Park editor: indoor", "sign_training", 75, false],
    ["Sk3Ed_Schl", "PARK SCHOOL", "Park editor: school", "sign_training", 76, false],
];

type Names = Map<number, string>;

// --- level names ---------------------------------------------------------------------------------------------------

// A level's name in THUG2 (its folder under levels\, its files, Load_<name>, Level_<name>, <name>_NodeArray). Online
// a level is known only by its load script's checksum (change level 0x46, start info 0x33 - docs/pending/thaw.md
// "THUGPRO"): THPS3's network levels keep THPS3's own names (levels.qb's master_level_list: Load_Foun -> "foun"),
// the names THUGPRO's ports of them use too, so the same level loads on both sides. A checksum is case-blind
// (Load_Foun = load_foun); the folders are written in lower case. Kept as thps3_<folder>:
// - the levels THPS3 doesn't offer in network games (no regular_level in its master_level_list: the tutorials, the
//   skate shop; the park editor's sets have no entry) - nothing to agree on, and "skateshop" is a level of the other
//   ported games too;
// - a name THUG2 already has (its own Load_<name> / Level_<name> in Levels.qb: Load_AP, Load_LA - THUG2's classic
//   Airport and Los Angeles, THPS3's levels remade, which THUGPRO plays for those two and for Canada).
// `legacy`: every level thps3_<folder>, as before 2026-10-20 (--legacy-names).
export function legacyName(base: string): string {
    return `thps3_${base.toLowerCase()}`;
}

// The checksums of THUG2's own level scripts and structures (its Levels.qb's scripts and globals: Load_AP,
// Level_AP...), from the THUG2 install's Data\pre\qb_scripts.prx.
export function thug2LevelNames(thug2: string): Set<number> {
    const archive = pyJoin(thug2, "Data", "pre", "qb_scripts.prx");
    const data = fs.existsSync(archive) ? prx.find(archive, "scripts\\game\\levels.qb") : null;
    if (data === null) pyExit(`${archive}: no scripts\\game\\levels.qb (THUG2's own level names are needed)`);
    const q = new qb.QbFile(data, "Levels.qb", true);
    return new Set([...q.scripts.keys(), ...q.globals.map(([k]) => k.value)]);
}

// THPS3's master_level_list entries (levels.qb) by their load script's checksum.
export function masterLevels(levelsQ: QbFile): Map<number, Struct> {
    const master = new Map<number, Struct>();
    const list = levelsQ.get("master_level_list");
    for (const value of Array.isArray(list) ? list : []) {
        if (value instanceof Struct) {
            const load = value.get("load_script");
            if (load instanceof Name) master.set(load.value, value);
        }
    }
    return master;
}

export function levelName(base: string, entry: Struct | null, thug2Names: ReadonlySet<number>, legacy = false): string {
    const bare = base.toLowerCase();
    if (legacy || entry === null || !entry.hasFlag("regular_level")) return legacyName(base);
    if (thug2Names.has(qb.checksum(`Load_${bare}`)) || thug2Names.has(qb.checksum(`Level_${bare}`)))
        return legacyName(base);
    return bare;
}

// Every LEVELS row's name ({folder in lower case: name}), from THPS3's levels.qb and THUG2's Levels.qb.
export function levelNames(thps3Dir: string, thug2: string, legacy = false): Map<string, string> {
    const levelsQ = new qb.QbFile(
        read(pyJoin(resolveInstall(thps3Dir, "thps3"), "Data", "Scripts", "levels.qb")),
        "levels.qb",
    );
    const master = masterLevels(levelsQ);
    const thug2Names = thug2LevelNames(thug2);
    return new Map(
        LEVELS.map(([base]) => [
            base.toLowerCase(),
            levelName(base, master.get(qb.checksum(`Load_${base}`)) ?? null, thug2Names, legacy),
        ]),
    );
}

// --- files ---------------------------------------------------------------------------------------------------------

export function findFolder(levelsDir: string, name: string): string | null {
    for (const entry of fs.readdirSync(levelsDir)) {
        if (entry.toLowerCase() === name.toLowerCase()) return pyJoin(levelsDir, entry);
    }
    return null;
}

export function levelFiles(folder: string, base: string): Map<string, string> {
    // <base>.bsp / .dff / .tdx / .qb, any case.
    const found = new Map<string, string>();
    for (const entry of fs.readdirSync(folder)) {
        if (entry.startsWith(".")) continue; // (glob's "*" skips them)
        const name = entry.toLowerCase();
        for (const kind of ["bsp", "dff", "tdx", "qb"]) {
            if (name === `${base}.${kind}`.toLowerCase()) found.set(kind, pyJoin(folder, entry));
        }
    }
    return found;
}

const read = (file: string): Buffer => fs.readFileSync(file);

// --- materials and textures ----------------------------------------------------------------------------------------

const invisible = (m: thps3.RwMaterial): boolean => m.ns !== null && (m.ns.flags & thps3.MAT_INVISIBLE) !== 0;

export function drawnSectors(world: thps3.World): Set<number> {
    // The atomic sectors the scene will have a sector for (sceneSectors: at least one triangle of a material that
    // isn't invisible) - the others are collision-only.
    const out = new Set<number>();
    for (const sec of world.sectors) {
        for (const [material] of sec.triangles) {
            const m = world.materials[material + sec.materialBase];
            if (m === undefined) throw new RangeError("list index out of range");
            if (!invisible(m)) {
                out.add(sec.checksum);
                break;
            }
        }
    }
    return out;
}

export function withoutMissingTrickObjects(text: string, scenePath: string): [string, number] {
    // (nodes.stripMissingTrickObjects with the converted scene's sectors: THPS3 Airport crashed loading)
    const sectors = new Set(thug2Scene.readScene(read(scenePath)).sectors.map((s) => s.checksum));
    return nodes.stripMissingTrickObjects(text, sectors);
}

export function usableNormals(sec: thps3.Sector): number[][] | null {
    // The sector's normals, or null: THPS3's PC files leave them (near) zero on almost every sector (Foundry: 533 of
    // 534) - lit by zero normals in THUG2, a sector drew black (Foundry's halfpipe). Without normals THUG2 draws a
    // sector by its vertex colours (THPS3's prelighting), as the rest of the level.
    const normals = sec.normals;
    if (!normals?.length || normals.some((n) => n[0]! * n[0]! + n[1]! * n[1]! + n[2]! * n[2]! < 0.25)) return null;
    return normals;
}

export function blendMode(code: number): [number, boolean] {
    // THPS3's blend: the PS2's alpha register, (A - B) * C + D, two bits each from the bottom (A, B, D: 0 source
    // colour, 1 destination colour, 2 zero; C: 0 source alpha, 1 destination alpha, 2 the fixed alpha) - the
    // formulas THUG2's blend modes are named after: [mode, uses the fixed alpha].
    const a = code & 3;
    const b = (code >> 2) & 3;
    const c = (code >> 4) & 3;
    const d = (code >> 6) & 3;
    const fixed = c === 2;
    const abd = `${a}${b}${d}`;
    if (a === b) return d === 0 ? [0, false] : [5, false]; // just D: source (opaque) - or the rare others as blend
    if (abd === "011") return [fixed ? 6 : 5, fixed]; // blend
    if (abd === "021") return [fixed ? 2 : 1, fixed]; // add
    if (abd === "020") {
        // Cs * C with nothing of the destination: THPS3 uses it for its reflection / light overlays
        // (Foun_CG_QP_Reflect_01 on the quarter / half pipes, Lamp_02_reflect, the vats' ripples) - as opaque (the
        // fallback below) a dark gradient covered Foundry's halfpipe black. Added, as the overlays look.
        return [fixed ? 2 : 1, fixed];
    }
    if (abd === "201") return [fixed ? 4 : 3, fixed]; // subtract
    if (abd === "122") return [fixed ? 8 : 7, fixed]; // modulate
    if (abd === "121") return [fixed ? 10 : 9, fixed]; // brighten
    return d === 0 ? [0, false] : [5, false];
}

export function textureChecksum(name: string): number {
    return name ? qb.checksum(name) : 0;
}

export function address(mode: number): number {
    // RenderWare's addressing (1 wrap, 2 mirror, 3 clamp, 4 border) as THUG2's (0 wrap, 1 clamp).
    return mode === 3 || mode === 4 ? 1 : 0;
}

const DEFAULT_NS: thps3.NsMaterial = {
    id: 0,
    passCount: 0,
    flags: 0,
    terrain: 0,
    colors: [[128, 128, 128, 255]],
    blend: 0,
    fixedAlpha: 0,
    texture: null,
    uvFlags: 0,
};

export function toThug2Material(
    checksum: number,
    m: thps3.RwMaterial,
    textures: ReadonlySet<number>,
): thug2Scene.Material {
    // A THPS3 material (thps3.readMaterial) as THUG2's: one pass.
    const ns = m.ns ?? DEFAULT_NS;
    const texInfo = ns.texture ?? m.texture;
    let texture = texInfo ? textureChecksum(texInfo.name) : 0;
    if (texture && !textures.has(texture)) texture = 0;
    const [mode] = blendMode(ns.blend);
    let flags = thug2Scene.PASS_SMOOTH;
    if (texture) flags |= thug2Scene.PASS_TEXTURED;
    if (mode === 0) flags |= thug2Scene.PASS_IGNORE_VERTEX_ALPHA;
    else flags |= thug2Scene.PASS_TRANSPARENT;
    if (ns.flags & thps3.MAT_DECAL) flags |= thug2Scene.PASS_DECAL;
    const [r, g, b] = ns.colors[0]! as [number, number, number, number];
    const neutral = [r, g, b].every((x) => 0x7e <= x && x <= 0x81);
    const p: thug2Scene.Pass = {
        texture,
        flags,
        blendMode: mode,
        fixedAlpha: ns.fixedAlpha & 0xff,
        uAddress: texInfo ? address(texInfo.uAddress) : 0,
        vAddress: texInfo ? address(texInfo.vAddress) : 0,
        envmapTiling: [3.0, 3.0],
        filtering: 0x10004,
        mip: [1, 4, -8.0, -8.0],
        hasColor: neutral ? 0 : 1,
        color: neutral ? [0.5, 0.5, 0.5] : [r / 256.0, g / 256.0, b / 256.0],
    };
    // An environment-mapped pass (uv flag 0x80: Foundry's quarter pipe / lamp / cannister / window reflections, the
    // *_reflect textures - the only ones with it) as THUG2's: its uvs from the view (THUG2's own env passes: tiling 3 x 3,
    // no uv wibble). THPS3 gives them the uv wibble flag too, with "1 0 1 1 0 0 1 1" (or "2 0 0 ..."): taken as a uv
    // scroll of a texture a second over the mesh's own uvs, the reflections crawled over the ramps all the time - the
    // shimmer of docs/pending/shimmer-2.md. Not a wibble here (THUG2's renderer passes over the wibble of an env pass
    // anyway - 0x004D1F00 - unless env mapping is off).
    // (Python's any(): NaN is true, 0.0 and -0.0 false)
    if (ns.uvFlags & thps3.UV_ENVIRONMENT) {
        p.flags |= thug2Scene.PASS_ENVIRONMENT;
    } else if (ns.uvFlags & thps3.UV_WIBBLE && ns.uvWibble?.some((x) => x !== 0)) {
        p.flags |= thug2Scene.PASS_UV_WIBBLE;
        p.uvWibble = ns.uvWibble;
    }
    const transparent = mode !== 0 || (ns.flags & (thps3.MAT_TRANSPARENT | thps3.MAT_DECAL)) !== 0;
    return {
        checksum,
        nameChecksum: checksum,
        alphaCutoff: 1,
        sorted: 0,
        drawOrder: transparent ? 1500.0 : 0.0,
        singleSided: 0,
        noBackfaceCulling: 0,
        zBias: 0,
        grassify: 0,
        specularPower: 0.0,
        passes: [p],
    };
}

export function mipDown(level: Buffer, w: number, h: number): Buffer {
    // The next mip level of a 32-bit texture: each 2 x 2 block averaged (a 1-wide side halves the other only).
    const nw = Math.max(1, Math.floor(w / 2));
    const nh = Math.max(1, Math.floor(h / 2));
    const sx = w > 1 ? 2 : 1;
    const sy = h > 1 ? 2 : 1;
    const out = Buffer.alloc(nw * nh * 4);
    const row = w * 4;
    for (let y = 0; y < nh; y++) {
        const r0 = y * sy * row;
        const r1 = r0 + (sy === 2 ? row : 0);
        let o = y * nw * 4;
        for (let x = 0; x < nw; x++) {
            const c0 = r0 + x * sx * 4;
            const c1 = c0 + (sx === 2 ? 4 : 0);
            const d0 = r1 + x * sx * 4;
            const d1 = d0 + (sx === 2 ? 4 : 0);
            for (let k = 0; k < 4; k++) {
                out[o + k] = (level[c0 + k]! + level[c1 + k]! + level[d0 + k]! + level[d1 + k]! + 2) >> 2;
            }
            o += 4;
        }
    }
    return out;
}

export function convertTextures(tdx: readonly thps3.TdxTexture[], mips = true): tex.TexFile {
    // THPS3's 32-bit textures (BGRA, as Direct3D's A8R8G8B8) in THUG2's dictionary, checksum = the name's; mip levels
    // made down to the level whose shorter side is 4, as THUG2's own (THPS3's have one level: without them distant
    // textures shimmer; a chain stopped early - it was 4 levels, down to 8 x 8 - still does on tiled floors and ramps).
    const out: tex.Texture[] = [];
    const seen = new Set<number>();
    for (const t of tdx) {
        const checksum = textureChecksum(t.name);
        if (seen.has(checksum)) continue;
        seen.add(checksum);
        const first = t.levels[0];
        if (first === undefined) throw new RangeError("list index out of range");
        const levels = [first];
        let w = t.width;
        let h = t.height;
        while (mips && Math.min(w, h) > 4) {
            levels.push(mipDown(levels[levels.length - 1]!, w, h));
            w = Math.max(1, Math.floor(w / 2));
            h = Math.max(1, Math.floor(h / 2));
        }
        out.push({
            checksum,
            width: t.width,
            height: t.height,
            texelDepth: 32,
            paletteDepth: 32,
            dxt: 0,
            palette: Buffer.alloc(0),
            levels,
        });
    }
    return { version: 1, textures: out };
}

// --- scene and collision -------------------------------------------------------------------------------------------

export function trisToStrip(tris: readonly (readonly number[])[]): number[] {
    // A triangle list as one strip, the triangles joined by degenerate ones (each starts at an even index, so its
    // winding is kept).
    const s: number[] = [];
    for (const [a, b, c] of tris as [number, number, number][]) {
        if (s.length) {
            s.push(s[s.length - 1]!);
            s.push(a);
            if ((s.length - 1) % 2) s.push(a);
            s.push(b, c);
        } else {
            s.push(a, b, c);
        }
    }
    return s;
}

export const SECTOR_FLAGS = (0x80002000 | thug2Scene.SEC_TEXCOORDS | thug2Scene.SEC_COLORS) >>> 0; // (as tools/thps4_levels' sectors)

export function sceneSectors(
    sectors: readonly thps3.Sector[],
    materials: readonly thps3.RwMaterial[],
    checksums: readonly number[],
    skip: ReadonlySet<number>,
): geometry.SourceSector[] {
    // THPS3's atomic sectors as the shared scene converter takes them (tools/level_port/geometry.convertScene):
    // per material a strip (the bin mesh's, or its triangle list joined), invisible materials left out, colours
    // made THUG2's (BGRA, 128 = full).
    const out: geometry.SourceSector[] = [];
    for (const sec of sectors) {
        if (skip.has(sec.checksum)) continue;
        const meshes: geometry.SourceSector["meshes"] = [];
        const byMaterial = new Map<number, number[][]>();
        for (const [material, a, b, c] of sec.triangles) {
            const key = material + sec.materialBase;
            let list = byMaterial.get(key);
            if (list === undefined) {
                list = [];
                byMaterial.set(key, list);
            }
            list.push([a, b, c]);
        }
        const bm = sec.binmesh;
        let parts: [number, number[]][];
        if (bm?.strips) {
            parts = bm.meshes.map((mesh) => [mesh.material, [...mesh.indices]]);
        } else {
            parts = [...byMaterial.entries()]
                .sort((x, y) => x[0] - y[0])
                .map(([material, tris]) => [material, trisToStrip(tris)]);
        }
        for (const [material, strip] of parts) {
            const m = materials[material];
            if (m === undefined) throw new RangeError("list index out of range");
            if (invisible(m)) continue;
            if (strip.length < 3) continue;
            meshes.push({ flags: 0, material: checksums[material]!, strip });
        }
        if (!meshes.length) continue;
        const positions = sec.positions;
        const colors = sec.colors?.length ? sec.colors : positions.map(() => [255, 255, 255, 255]);
        const normals = usableNormals(sec);
        const flags = (SECTOR_FLAGS | (normals ? thug2Scene.SEC_NORMALS : 0)) >>> 0;
        const [lo, hi, center, radius] = geometry.bounds(positions);
        const uvs = sec.uvs.map((uv) => uv.slice(0, 2));
        const s: geometry.SourceSector = {
            checksum: sec.checksum,
            bone: -1,
            flags,
            bbox: [...lo, ...hi],
            sphere: [center[0]!, center[1]!, center[2]!, radius],
            positions,
            colors: colors.map(([r, g, b, a]) => [b! >> 1, g! >> 1, r! >> 1, a! >> 1]),
            // (the first uv set only: THPS3's second one is never written - uninitialised memory, NaNs)
            uvSets: 1,
            uvs: uvs.length ? uvs : positions.map(() => [0.0, 0.0]),
            meshes,
        };
        if (normals) s.normals = normals;
        out.push(s);
    }
    return out;
}

type Transform = (p: readonly number[]) => number[];

export function frameTransform(frames: readonly thps3.Frame[], index: number): Transform {
    // A clump frame's matrix with its parents' as (rotation rows right / up / at, position): a point p maps to
    // p.x * right + p.y * up + p.z * at + position.
    const apply = (frame: thps3.Frame, p: readonly number[]): number[] => {
        const r = frame.rotation;
        return [0, 1, 2].map((k) => p[0]! * r[k]! + p[1]! * r[3 + k]! + p[2]! * r[6 + k]! + frame.position[k]!);
    };
    const chain: thps3.Frame[] = [];
    let i = index;
    while (i >= 0 && i < frames.length && chain.length <= frames.length) {
        chain.push(frames[i]!);
        i = frames[i]!.parent;
    }
    return (point) => {
        let p = [...point];
        for (const frame of chain) p = apply(frame, p);
        return p;
    };
}

// [checksum, THPS3 material] of the game objects' geometry.
export type ObjectMaterial = [number, thps3.RwMaterial];

export function movingSector(
    clump: thps3.Clump,
    level: string,
    firstMaterial: number,
): [thps3.Sector, ObjectMaterial[]] | null {
    // A game object's clump as one sector in the object's own space (THUG2's LevelObject: the model component clones
    // the sector of the node's name and draws it at the object's position and orientation - THUG2's own LevelObject
    // sectors are centred on their origin), its atomics' geometry through their frames (a Z-up model turned Y-up:
    // (x, z, -y)) put together. Returns [sector as thps3.readBsp makes them, materials as objectSectors's] or null.
    const positions: number[][] = [];
    const colors: number[][] = [];
    const uvs: number[][] = [];
    const triangles: thps3.Triangle[] = [];
    const strips: thps3.BinMesh["meshes"] = [];
    const materials: ObjectMaterial[] = [];
    let allStrips = true;
    clump.atomics.forEach((atomic, n) => {
        if (!(atomic.geometry >= 0 && atomic.geometry < clump.geometries.length)) return;
        const g = clump.geometries[atomic.geometry]!;
        if (!g.positions.length || !g.triangles.length) return;
        const toObject = frameTransform(clump.frames, atomic.frame);
        const baseVertex = positions.length;
        const base = firstMaterial + materials.length;
        g.materials.forEach((m, i) => {
            materials.push([qb.checksum(`${level}_object_${hex8(clump.checksum)}_${n}_${i}`), m]);
        });
        for (const p of g.positions) positions.push(toObject(p));
        colors.push(...(g.colors.length ? g.colors : g.positions.map(() => [255, 255, 255, 255])));
        uvs.push(...(g.uvs.length ? g.uvs : g.positions.map(() => [0.0, 0.0])));
        for (const [m, a, b, c] of g.triangles)
            triangles.push([base + m, a + baseVertex, b + baseVertex, c + baseVertex]);
        if (g.binmesh?.strips) {
            for (const mesh of g.binmesh.meshes) {
                strips.push({ material: base + mesh.material, indices: mesh.indices.map((v) => v + baseVertex) });
            }
        } else {
            allStrips = false;
        }
    });
    if (!triangles.length) return null;
    const sec: thps3.Sector = {
        checksum: clump.checksum,
        materialBase: 0,
        positions,
        colors,
        uvs: uvs.map((uv) => uv.slice(0, 2)),
        uvSets: 1,
        triangles,
        binmesh: null,
    };
    if (allStrips && strips.length) sec.binmesh = { strips: true, meshes: strips };
    return [sec, materials];
}

export function objectSectors(
    clumps: readonly thps3.Clump[],
    nodeArray: QbValue | undefined,
    names: ReadonlyMap<number, string>,
    level: string,
    firstMaterial: number,
    moving: ReadonlySet<number> = new Set(),
): [thps3.Sector[], ObjectMaterial[]] {
    // The game objects there at the start (CreatedAtStart GameObjects / BouncyObjects without a model= of their own:
    // Foundry's fans, Tokyo's lamps, Skater Island's cannons...) as static scene sectors, no collision: each clump's
    // geometry through its frames (a Z-up model turned Y-up: (x, z, -y)), turned by the node's heading and moved to
    // its Position (z negated, as the world's geometry). The clumps of the nodes in `moving` (THUG2 LevelObjects:
    // moved by their scripts) are one sector each in their own space instead (movingSector), there at the start or
    // not. Returns [sectors as thps3.readBsp makes them, their materials as [checksum, THPS3 material], numbered from
    // firstMaterial].
    const byName = new Map<number, Struct>();
    if (!Array.isArray(nodeArray)) throw new TypeError("NodeArray is not a list");
    for (const node of nodeArray) {
        if (node instanceof Struct) {
            const name = node.get("Name");
            if (name instanceof Name) byName.set(name.value, node);
        }
    }
    const sectors: thps3.Sector[] = [];
    const materials: ObjectMaterial[] = [];
    for (const clump of clumps) {
        if (moving.has(clump.checksum)) {
            const made = movingSector(clump, level, firstMaterial + materials.length);
            if (made) {
                sectors.push(made[0]);
                materials.push(...made[1]);
            }
            continue;
        }
        const node = byName.get(clump.checksum);
        if (
            node === undefined ||
            !node.hasFlag("CreatedAtStart") ||
            node.get("model") !== undefined ||
            !["gameobject", "bouncyobject"].includes(nodes.classOf(node, names))
        ) {
            continue;
        }
        const position = node.get("Position");
        const angles = node.get("Angles");
        if (!(position instanceof Vector && position.kind === "vector")) continue;
        const [px, py, pz] = position.values as [number, number, number];
        const heading = angles instanceof Vector && angles.kind === "vector" ? angles.values[1]! : 0.0;
        const cos = Math.cos(heading);
        const sin = Math.sin(heading);
        clump.atomics.forEach((atomic, n) => {
            if (!(atomic.geometry >= 0 && atomic.geometry < clump.geometries.length)) return;
            const g = clump.geometries[atomic.geometry]!;
            if (!g.positions.length || !g.triangles.length) return;
            const toObject = frameTransform(clump.frames, atomic.frame);
            const positions = g.positions.map((p) => {
                const [x, y, z] = toObject(p) as [number, number, number];
                return [x * cos + z * sin + px, y + py, -x * sin + z * cos - pz];
            });
            const base = firstMaterial + materials.length;
            g.materials.forEach((m, i) => {
                materials.push([qb.checksum(`${level}_object_${hex8(clump.checksum)}_${n}_${i}`), m]);
            });
            const sec: thps3.Sector = {
                checksum: qb.checksum(`${level}_object_${hex8(clump.checksum)}_${n}`),
                materialBase: 0,
                positions,
                colors: g.colors.length ? g.colors : null,
                uvs: g.uvs,
                uvSets: g.uvSets,
                triangles: g.triangles.map(([m, a, b, c]) => [base + m, a, b, c]),
                binmesh: null,
            };
            if (g.binmesh?.strips) {
                sec.binmesh = {
                    strips: true,
                    meshes: g.binmesh.meshes.map((mesh) => ({ material: base + mesh.material, indices: mesh.indices })),
                };
            }
            sectors.push(sec);
        });
    }
    return [sectors, materials];
}

// THPS3's face flags have THUG2's meanings up to 0x1000 but 0x80, which means the reverse (as THPS4's): THPS3 sets it
// where the camera collides - the solid world, 225,343 of its levels' 577,634 faces (buildings, floors, walls, ramps),
// not props, decals, triggers or non-collidable faces - while THUG2's 0x80 is "the camera passes through" (its camera
// feelers 0x004E4425 / 0x004FB380 ignore 0x90; its own levels set it on lamps, poles, trees, fences, triggers):
// inverted (docs/pending/face-flag-0x80.md). 0x2000 / 0x4000 / 0x8000 are on a few hundred of THPS3's 580,016 faces
// each, with every other bit about half the time (stray values; THPS3 has no tagging): dropped - THUG2's 0x2000 is
// "not taggable", 0x4000 / 0x8000 do nothing in THUG2 (the graffiti feelers ignore 0x3000 at 0x0051DF71 / 0x0051E74B;
// no code tests 0x4000 / 0x8000 of a face).
export const FACE_CAMERA = 0x0080;
export const FACE_KEEP = 0x1fff;

export function faceFlags(flags: number): number {
    return (flags ^ FACE_CAMERA) & FACE_KEEP;
}

// Whether a face's corners are `corners` in the same turning order (any first corner) - TP-19's faces are THPS3's own
// vertices, so equal but for the JSON round trip.
function sameCorners(sec: thps3.Sector, face: readonly number[], corners: readonly (readonly number[])[]): boolean {
    const at = face.map((i) => sec.positions[i]!);
    const near = (a: readonly number[], b: readonly number[]): boolean =>
        a.every((x, k) => Math.abs(x - b[k]!) <= 0.001);
    return [0, 1, 2].some((r) => [0, 1, 2].every((k) => near(at[k]!, corners[(k + r) % 3]!)));
}

export function collisionObjects(
    sectors: readonly thps3.Sector[],
    materials: readonly thps3.RwMaterial[],
    skip: ReadonlySet<number>,
    edits: ReadonlyMap<number, CollisionEditCompat> = new Map(),
    counts: { dropped: number; faces: number; missing: number } = { dropped: 0, faces: 0, missing: 0 },
): { objects: geometry.SourceColObject[] } {
    // Each atomic sector one collision object (its name the sector's): every face, its flags and its material's
    // terrain. `edits`: TP-19 (THUGPRO's collision edits, by the sector's checksum) - an object's collision left out,
    // or some of its faces (`counts` says how many; a face not found is counted as missing).
    const out: geometry.SourceColObject[] = [];
    for (const sec of sectors) {
        if (skip.has(sec.checksum) || !sec.triangles.length) continue;
        const edit = edits.get(sec.checksum);
        if (edit?.drop) {
            counts.dropped += 1;
            continue;
        }
        const faces: thug2Col.Face[] = [];
        const flagList = sec.faceFlags ?? [];
        const count = Math.min(sec.triangles.length, flagList.length); // (zip)
        const removing = [...(edit?.remove ?? [])];
        for (let i = 0; i < count; i++) {
            const [material, a, b, c] = sec.triangles[i]!;
            const m = materials[material + sec.materialBase];
            if (m === undefined) throw new RangeError("list index out of range");
            const terrain = m.ns ? m.ns.terrain : 0;
            const gone = removing.findIndex((corners) => sameCorners(sec, [a, b, c], corners));
            if (gone >= 0) {
                removing.splice(gone, 1);
                counts.faces += 1;
                continue;
            }
            faces.push([flagList[i]!, terrain, a, b, c]);
        }
        counts.missing += removing.length;
        const colors = sec.colors?.length ? sec.colors : sec.positions.map(() => [255, 255, 255, 255]);
        const lo = [0, 1, 2].map((k) => pyMin(sec.positions.map((v) => v[k]!)));
        const hi = [0, 1, 2].map((k) => pyMax(sec.positions.map((v) => v[k]!)));
        out.push({
            checksum: sec.checksum,
            flags: 0,
            verts: sec.positions,
            intensities: Buffer.from(colors.map(([r, g, b]) => Math.max(r!, g!, b!) >> 1)),
            faces,
            bbox: [lo[0]!, lo[1]!, lo[2]!, 1.0, hi[0]!, hi[1]!, hi[2]!, 1.0],
        });
    }
    return { objects: out };
}

export interface ConvertWorldOptions {
    withCol?: boolean;
    keepTriggers?: (checksum: number) => boolean;
    skip?: ReadonlySet<number>;
    mips?: boolean;
    // objectSectors' [sectors, materials] - drawn, not collided with.
    objects?: [thps3.Sector[], ObjectMaterial[]] | null;
    // the prefix of the materials' checksums (<tag>_material_<n>; default the level's name)
    tag?: string;
    // TP-19: THUGPRO's collision edits by the sector's checksum (collisionObjects)
    colEdits?: ReadonlyMap<number, CollisionEditCompat>;
    // TPN-THPS3: THUGPRO's new sectors, textures and collision objects (thugpro_new.ts applyGeometry)
    thugproNew?: thugproNew.LevelNew;
    // PC-THPS3-15: the moving sectors (objects) that get collision of their own - in their own space, as drawn - for
    // their LevelObject's CollisionMode=Geometry (THPS3's shape: THUGPRO's collision of the name is left out)
    solid?: ReadonlySet<number>;
}

// The collision of the solid moving sectors (PC-THPS3-15): every face collidable (THPS3's clumps have no face flags:
// 0, as THPS3's plain world faces), the terrain of its material. `materials`: the world's, then the objects'.
export function solidCollision(
    sectors: readonly thps3.Sector[],
    materials: readonly thps3.RwMaterial[],
    solid: ReadonlySet<number>,
    skip: ReadonlySet<number>,
): geometry.SourceColObject[] {
    const chosen = sectors
        .filter((s) => solid.has(s.checksum) && !skip.has(s.checksum))
        .map((s) => ({ ...s, faceFlags: s.faceFlags ?? new Array<number>(s.triangles.length).fill(0) }));
    return collisionObjects(chosen, materials, new Set()).objects;
}

export function convertWorld(
    world: thps3.World,
    tdx: readonly thps3.TdxTexture[],
    name: string,
    outDir: string,
    report: string[],
    options: ConvertWorldOptions = {},
): void {
    const withCol = options.withCol ?? true;
    const keepTriggers = options.keepTriggers ?? (() => false);
    const skip = options.skip ?? new Set<number>();
    const objects = options.objects ?? null;
    const warnings: string[] = [];
    const textures = convertTextures(tdx, options.mips ?? true);
    const known = new Set(textures.textures.map((t) => t.checksum));
    const raw = [...world.materials];
    const checksums = raw.map((_, i) => qb.checksum(`${options.tag ?? name}_material_${i}`));
    let objectList: thps3.Sector[] = [];
    if (objects) {
        objectList = objects[0];
        raw.push(...objects[1].map(([, m]) => m));
        checksums.push(...objects[1].map(([c]) => c));
    }
    const materials = raw.map((m, i) => toThug2Material(checksums[i]!, m, known));
    for (const m of raw) {
        const info = m.ns?.texture ?? m.texture;
        if (info && !known.has(textureChecksum(info.name)))
            warnings.push(`texture ${info.name} not in the dictionary - untextured`);
    }
    const sceneIn = { sectors: sceneSectors([...world.sectors, ...objectList], raw, checksums, skip) };
    const used = new Set(sceneIn.sectors.flatMap((s) => s.meshes.map((mesh) => mesh.material)));
    const scene = geometry.convertScene(
        sceneIn,
        warnings,
        materials.filter((m) => used.has(m.checksum)),
    );
    let col: thug2Col.ColFile | null = null;
    const counts = { dropped: 0, faces: 0, missing: 0 };
    if (withCol) {
        const source = collisionObjects(world.sectors, world.materials, skip, options.colEdits, counts);
        if (options.solid?.size) {
            // (PC-THPS3-15; cmdConvert leaves THUGPRO's collision of the name out - should one remain, it wins)
            const theirs = new Set((options.thugproNew?.collision ?? []).map((o) => o.checksum));
            const ours = solidCollision(objectList, raw, options.solid, skip).filter((o) => !theirs.has(o.checksum));
            source.objects.push(...ours);
            report.push(`  PC-THPS3-15: ${ours.length} moving objects' collision made from their drawn geometry`);
        }
        col = geometry.convertCol(source, keepTriggers, warnings, faceFlags);
    }
    const tpNew = options.thugproNew;
    const newLine =
        tpNew !== undefined && (tpNew.sectors.length || tpNew.collision.length)
            ? thugproNew.applyGeometry(scene, textures, col, tpNew, warnings)
            : null;
    // (THPS3's materials have no z bias: the layers on other surfaces get one, or they shimmer - level_port/overlay_bias.ts)
    const biased = overlayBias.biasOverlays(scene);
    if (biased) report.push(`  ${biased} layer materials given a z bias over the surfaces under them`);
    common.writeLevel(outDir, name, scene, textures, col);
    common.reportLevel(report, name, scene, textures, col, [...new Set(warnings)].sort(compareStr));
    if (newLine !== null) report.push(newLine);
    if (options.colEdits?.size) {
        const missing = counts.missing ? `, ${counts.missing} faces not found` : "";
        report.push(
            `  TP-19 THUGPRO's collision edits: ${counts.dropped} objects without collision, ${counts.faces} faces left out${missing}`,
        );
    }
}

// --- nodes ---------------------------------------------------------------------------------------------------------

function sortedGlobQb(dir: string): string[] {
    // sorted(glob(<dir>\*.qb)): Windows' glob matches the extension in any case, skips names starting with "."
    if (!fs.existsSync(dir)) return [];
    return fs
        .readdirSync(dir, { withFileTypes: true })
        .filter((e) => !e.name.startsWith(".") && e.name.toLowerCase().endsWith(".qb"))
        .map((e) => pyJoin(dir, e.name))
        .sort(compareStr);
}

export function scriptNames(thps3Dir: string): QbFile[] {
    // The game's global scripts (Data\Scripts\*.qb): the level scripts that kill the skater are among them.
    const out: QbFile[] = [];
    for (const file of sortedGlobQb(pyJoin(resolveInstall(thps3Dir, "thps3"), "Data", "Scripts"))) {
        try {
            out.push(new qb.QbFile(read(file), file));
        } catch (e) {
            if (!(e instanceof FormatError)) throw e;
        }
    }
    return out;
}

export function nameValue(payload: Buffer): number {
    if (payload.length !== 4) throw new StructError("unpack requires a buffer of 4 bytes");
    return unpackNumbers("<I", payload)[0]!;
}

// Our own changes to THPS3's data (the register's port_changes, read by compat.portChanges whatever the profile): an
// entry applies when on - every entry, or with --no-port-changes (enabled false) the `required` ones - and to the
// levels its target.levels names (a list of folder names, or "*").
export class PortChanges {
    readonly entries: readonly compat.Entry[];
    readonly enabled: boolean;

    constructor(entries: readonly compat.Entry[], enabled = true) {
        this.entries = entries;
        this.enabled = enabled;
    }

    // The entry when it applies to `level` (lower case), else null; an id the register hasn't is an error.
    get(id: string, level?: string): compat.Entry | null {
        const e = this.entries.find((x) => x.id === id);
        if (e === undefined) throw new RangeError(`port_changes: no entry ${id}`);
        if (!this.enabled && e.required !== true) return null;
        const levels = e.target.levels;
        if (level !== undefined && Array.isArray(levels) && !levels.some((l) => String(l).toLowerCase() === level))
            return null;
        return e;
    }

    on(id: string, level?: string): boolean {
        return this.get(id, level) !== null;
    }
}

export const thps3PortChanges = (enabled = true): PortChanges =>
    new PortChanges(compat.portChanges("thps3_levels"), enabled);

// PC-THPS3-1 (port_changes, tools/compat/thugpro/thps3_levels.json: `prefixes` per level): sector name prefixes left out.
// Warehouse: its lights flicker in THPS3 (JKU_Ware_RoomFlicker toggles SkWare_Light_* / SkWare_Dark_*, the same
// surfaces lit and dark); without that script both sets were drawn - z-fighting. Kept lit, as THPS3's observer mode
// (JKU_Ware_IsObserver kills SkWare_Dark).
export function leftOutPrefixes(pc: PortChanges, base: string): readonly string[] {
    return (pc.get("PC-THPS3-1", base.toLowerCase())?.prefixes as string[] | undefined) ?? [];
}

// PC-THPS3-1's `shown`: the copy kept in place of the left-out one (Warehouse's SkWare_Light) - the scripts' Invisible /
// Kill prefix= of it are left out (script_port TranslatorOptions.shownPrefixes). JKU_Ware_RoomFlicker hides the lit set
// for 10-120 frames at a time to show the dark one; without the dark set that left the room and the floor black.
export function shownPrefixes(pc: PortChanges, base: string): readonly string[] {
    return (pc.get("PC-THPS3-1", base.toLowerCase())?.shown as string[] | undefined) ?? [];
}

// PC-THPS3-16: what an `if` in a start script is in offline free skate (null: unknown, both branches followed). Free
// skate is neither career, nor a goal, nor a multiplayer / network / split-screen game: those conditions are false
// (their `not` true). Anything else - a flag, a second condition (and / or) - is not decided here.
export function startCondition(cond: readonly [number, Buffer][], c: (name: string) => number): boolean | null {
    let i = 0;
    let negate = false;
    if (cond.length && cond[0]![0] === 0x39) {
        negate = true;
        i = 1;
    }
    if (i >= cond.length || cond[i]![0] !== 0x16) return null;
    for (const [t] of cond.slice(i)) if (![0x16, 0x07, 0x17, 0x18, 0x1a, 0x1b, 0x1c].includes(t)) return null;
    const command = nameValue(cond[i]![1]);
    const offline = ["IsCareerMode", "GetGoal", "InMultiPlayerGame", "InNetGame", "InSplitScreenGame"].map(c);
    return offline.includes(command) ? negate : null;
}

export function createdAtStart(
    q: QbFile,
    globalQbs: readonly QbFile[],
    namesAll: ReadonlyMap<number, string>,
    base: string,
    startScripts: readonly number[],
    pc: PortChanges = thps3PortChanges(),
): [Set<number>, string[]] {
    // What THPS3's scripts create when a level starts (Create name= / prefix=), followed statically from its start
    // scripts through the scripts they call or spawn (both branches of every if): Load_<base> (its <base>_Startup),
    // the level's once_on_startup, the Player1 restart's trigger script, and the start scripts of the goal /
    // competition parameters StartRunScript gives this level (Goal_Start_Script... - not the goal complete ones).
    // Airport's areas, the tutorials' rooms and the like are made there; a press's other position, an open door or
    // LA's earthquake rubble are not.
    const scripts = new Map<number, Statement[]>();
    const structs = new Map<number, Struct>();
    for (const g of [...globalQbs, q]) {
        for (const [k, v] of g.scripts) scripts.set(k, v);
        for (const [key, value] of g.globals) if (value instanceof Struct) structs.set(key.value, value);
    }
    const c = qb.checksum;
    const entries: (number | null)[] = [...startScripts, c(`Load_${base}`)];
    // StartRunScript's branch for this level: `if CareerLevelIs LevelNum_X` ... GameStart / CompIntroSequence <params>
    let levelNum: number | null = null;
    for (const [command, args] of statementsOf(scripts.get(c(`Load_${base}`)) ?? [])) {
        if (command === c("CareerStartLevel") && args.has(c("level"))) levelNum = args.get(c("level"))!;
    }
    let inBranch = false;
    for (const line of scripts.get(c("StartRunScript")) ?? []) {
        if (line.some(([t, p]) => t === 0x16 && nameValue(p) === c("CareerLevelIs"))) {
            inBranch = line.some(([t, p]) => t === 0x16 && nameValue(p) === levelNum);
            continue;
        }
        if (inBranch) {
            for (const [t, p] of line) {
                if (t === 0x16 && structs.has(nameValue(p))) {
                    for (const [key, value] of structs.get(nameValue(p))!.entries) {
                        const text = key !== null ? (namesAll.get(key.value) ?? "") : "";
                        const low = text.toLowerCase();
                        if (
                            value instanceof Name &&
                            low.includes("start_script") &&
                            !low.includes("complete") &&
                            !low.includes("scripted")
                        ) {
                            entries.push(value.value);
                        }
                    }
                }
            }
        }
    }
    const names = new Set<number>();
    const prefixes: string[] = [];
    const seen = new Set<number>();
    const todo = entries.filter((e): e is number => e !== null);
    while (todo.length) {
        const script = todo.pop()!;
        if (seen.has(script) || !scripts.has(script)) continue;
        seen.add(script);
        // PC-THPS3-16: only the branches offline free skate runs (see startCondition)
        const frames: { value: boolean | null; inElse: boolean }[] = [];
        for (const line of scripts.get(script)!) {
            const head = line.length ? line[0]![0] : -1;
            if (head === 0x25) {
                frames.push({ value: pc.on("PC-THPS3-16") ? startCondition(line.slice(1), c) : null, inElse: false });
                continue;
            }
            if (head === 0x26) {
                if (frames.length) frames[frames.length - 1]!.inElse = true;
                continue;
            }
            if (head === 0x28) {
                frames.pop();
                continue;
            }
            if (frames.some((f) => f.value !== null && f.value === f.inElse)) continue;
            if (!line.length || line[0]![0] !== 0x16) continue;
            const command = nameValue(line[0]![1]);
            if (scripts.has(command)) todo.push(command);
            if (
                (command === c("SpawnScript") || command === c("RunScriptOnObject")) &&
                line.length > 1 &&
                line[1]![0] === 0x16
            ) {
                todo.push(nameValue(line[1]![1]));
            }
            if (command === c("Create")) {
                for (let i = 1; i < line.length - 2; i++) {
                    if (line[i]![0] === 0x16 && line[i + 1]![0] === 0x07) {
                        const key = nameValue(line[i]![1]);
                        const [vt, vp] = line[i + 2]!;
                        if (key === c("Name") && vt === 0x16) names.add(nameValue(vp));
                        else if (key === c("prefix") && (vt === 0x1b || vt === 0x1c))
                            prefixes.push(decodeLatin1(qb.rstripNul(vp)).toLowerCase());
                    }
                }
            }
        }
    }
    return [names, prefixes];
}

export function statementsOf(lines: readonly Statement[]): [number, Map<number, number>][] {
    const out: [number, Map<number, number>][] = [];
    for (const line of lines) {
        if (!line.length || line[0]![0] !== 0x16) continue;
        const args = new Map<number, number>();
        for (let i = 1; i < line.length - 2; i++) {
            if (line[i]![0] === 0x16 && line[i + 1]![0] === 0x07 && line[i + 2]![0] === 0x16) {
                args.set(nameValue(line[i]![1]), nameValue(line[i + 2]![1]));
            }
        }
        out.push([nameValue(line[0]![1]), args]);
    }
    return out;
}

// A rigid model of Data\Models: its first clump and its textures.
type Model = [thps3.Clump, thps3.TdxTexture[]];

export class Port {
    // What bringing a level's scripts along needs: THUG2's commands (scriptPort.Thug2), THPS3's script files
    // (scriptPort.Source), levels.qb (the levels' load scripts), and per level its own .qb (Source) and the names of
    // its .dff's clumps.
    readonly thug2: scriptPort.Thug2;
    readonly sources: scriptPort.Source[];
    readonly levelsQ: QbFile;
    readonly modelsDir: string;
    level: scriptPort.Source | null = null;
    clumps = new Set<number>();
    moving = new Set<number>();
    vehicles = new Map<number, string>(); // node name -> model name (the level's vehicles made LevelObjects of their models)
    drawn = new Set<number>(); // the level's atomic sectors with a scene sector (drawnSectors)
    solid = new Set<number>(); // moving LevelObjects made solid (PC-THPS3-15): their sectors get collision
    copies = new Map<number, number>(); // node name -> clump checksum (game objects drawn with another clump of the level: model=)
    private readonly models = new Map<string, Model | null>();
    readonly assets: pedsCars.Thug2Assets;

    constructor(thug2: scriptPort.Thug2, sources: scriptPort.Source[], levelsQ: QbFile, thps3Dir: string) {
        this.thug2 = thug2;
        this.sources = sources;
        this.levelsQ = levelsQ;
        this.modelsDir = pyJoin(resolveInstall(thps3Dir, "thps3"), "Data", "Models");
        this.assets = new pedsCars.Thug2Assets(thug2.root);
    }

    model(name: string): Model | null {
        // THPS3's Data\Models\<name>\<name>.dff (a rigid clump: cars, trains, planes) and its .tdx, or null (skinned
        // models - .skn: peds, birds - aren't converted).
        const key = name.toLowerCase();
        if (!this.models.has(key)) {
            let found: Model | null = null;
            const isDir = fs.existsSync(this.modelsDir) && fs.statSync(this.modelsDir).isDirectory();
            const folder = isDir ? findFolder(this.modelsDir, name) : null;
            const files = folder ? levelFiles(folder, name) : new Map<string, string>();
            const dff = files.get("dff");
            const tdx = files.get("tdx");
            if (dff !== undefined && tdx !== undefined) {
                let clumps: thps3.Clump[] | null;
                try {
                    clumps = thps3.readDff(read(dff), dff);
                } catch (e) {
                    // (Barrier.dff: a chunk the reader doesn't take - left out)
                    if (!(e instanceof FormatError)) throw e;
                    clumps = null;
                }
                if (clumps?.length) found = [clumps[0]!, thps3.readTdx(read(tdx), tdx)];
            }
            this.models.set(key, found);
        }
        return this.models.get(key)!;
    }
}

function key(names: Names, text: string): Name {
    const value = qb.checksum(text);
    if (!names.has(value)) names.set(value, text);
    return new Name(value);
}

const nameOf = (node: Struct): number => (node.get("Name") as Name).value;
const isNumberValue = (v: QbValue | undefined): v is number | Float => typeof v === "number" || v instanceof Float;
const numberValue = (v: number | Float): number => (v instanceof Float ? v.value : v);

// The made peds' trigger scripts (THUG2's ped AI), in every level's .qb.
export const PED_WALK = "thps3_ped_walk";
export const PED_STAND = "thps3_ped_stand";
export const PED_SCRIPTS = `script ${PED_WALK} {\n    Ped_InitAI \n}\nscript ${PED_STAND} {\n    Ped_InitAI stand \n}\n`;
export const FEMALE = "random_female_profile";
// THPS3's cars as THUG2's (the models THUG2's own levels drive with Car_Generic01): (parts of THPS3's model name, THUG2
// models - one picked by the car's name), the last the default. Their trigger scripts: THUG2's Car_Generic01, silent
// (THPS3's car sounds aren't converted).
export const CAR_MODELS: readonly [readonly string[], readonly string[]][] = [
    [["police"], ["veh\\veh_cop\\veh_cop.mdl"]],
    [["van", "microbus", "icecream", "bus"], ["veh\\veh_van\\veh_van.mdl"]],
    [["pickup"], ["veh\\veh_rust\\veh_rust.mdl", "veh\\veh_van\\veh_van.mdl"]],
    [
        [],
        [
            "veh\\veh_sedan\\veh_sedan.mdl",
            "veh\\veh_galaxie\\veh_galaxie.mdl",
            "veh\\veh_chevy_impala\\veh_chevy_impala.mdl",
            "veh\\veh_custom\\veh_custom.mdl",
        ],
    ],
];
export const CAR = "thps3_car";
export const CAR_BUS = "thps3_car_bus";
export const CAR_SCRIPTS = `script ${CAR} {\n    Car_Generic01 NoSound \n}\nscript ${CAR_BUS} {\n    Car_Generic01 NoSound Bus \n}\n`;
// THPS3's goal models (career pickups): left out.
export const GOAL_MODELS = ["letter_", "stat_point", "deck_icon", "secret_tape"];

// THPS3's animated animals as THUG2's (Data\models\animals; THUG2's AU / SE2 place them the same way): type ->
// [THUG2 skeleton, trigger script]. The scripts are THUG2 AU's pigeons' (AU_Pigeon: fly up when the skater comes
// within 15 feet, down again once he's 20 away) and SE2's sharks' (SE2_Shark: swim the linked path), without their
// sounds.
export const ANIMAL_BIRD = "thps3_animal_bird";
export const ANIMAL_SHARK = "thps3_animal_shark";
export const ANIMALS: ReadonlyMap<string, [string, string]> = new Map([
    ["bird_a_resting", ["Anl_Pigeon", ANIMAL_BIRD]],
    ["bird_b_resting", ["Anl_Seagull", ANIMAL_BIRD]],
    ["shark_a", ["Anl_Shark", ANIMAL_SHARK]],
]);
const BIRD = ANIMAL_BIRD;
export const ANIMAL_SCRIPTS = `script ${BIRD} {
    Obj_ShadowOff
    Obj_ClearExceptions
    Obj_SetException Ex=ObjectInRadius Scr=${BIRD}_fly
    Obj_SetInnerRadius 15
    Obj_CycleAnim Anim=Idle
}
script ${BIRD}_fly {
    Obj_ClearExceptions
    Obj_PlayAnim Anim=Fly Cycle
    Obj_MoveToRelPos (0.0, 250.0, 0.0) Speed=12 Acceleration=2
    Obj_WaitMove
    Obj_SetOuterRadius 20
    Obj_SetException Ex=ObjectOutOfRadius Scr=${BIRD}_return
}
script ${BIRD}_return {
    Obj_ClearExceptions
    Obj_PlayAnim Anim=Fly Cycle
    Obj_MoveToRelPos (0.0, -250.0, 0.0) Speed=8 Acceleration=2
    Obj_WaitMove
    GoTo ${BIRD}
}
script ${ANIMAL_SHARK} {
    Obj_ShadowOff
    Obj_StickToGround Off
    Obj_SetPathVelocity 6 mph
    Obj_SetPathTurnDist 10
    Obj_PathHeading On
    Obj_FollowPathLinked Orient
    Obj_PlayAnim Anim=Swim Cycle
}
`;

// [node name, script, context] of a made node's trigger script.
type Trigger = [number, number, scriptPort.Context];

export class PortedNodes {
    // THPS3's game objects and waypoints made THUG2's nodes (kept whole by nodes.convertNodes):
    //   a GameObject with a clump of its name in the .dff (fans, presses, valve wheels, buckets): a LevelObject - the
    //       clump is a sector of that name in the scene (movingSector), moved by the object's scripts;
    //   a GameObject with neither a clump nor a model (a script's controller: Foundry's Obj_Bucket_Controller): a
    //       ghost (THUG2's Type=Ghost model="none" game objects) running its script;
    //   a GameObject with a model (goal letters, stat points, tapes: THPS3's goal models): left out;
    //   a Waypoint: THUG2's, its Links to waypoints kept (Obj_MoveToNode / Obj_FollowPathLinked targets).
    // Their trigger scripts are added once known to be brought along (portScripts).
    readonly names: Names;
    readonly array: readonly QbValue[];
    readonly port: Port | null;
    // the waypoints the peds walk (from their Links): THUG2's ped AI waypoints too
    readonly pedPaths = new Set<number>();
    readonly ids = new Set<Struct>();
    readonly triggers: Trigger[] = [];
    readonly scripts = new Set<number>();
    readonly moving = new Set<number>(); // the LevelObjects' names (their clumps become sectors in their own space)
    readonly animals = new Set<string>(); // THUG2 animal skeletons used (their animations loaded: loadAnimsNs)
    readonly proxims: [number, number, number][] = []; // [node name, THPS3 script, radius] - proximScriptsNs
    readonly counts = new Map<string, number>();
    private index: Map<QbValue, number> | null = null;

    constructor(names: Names, array: readonly QbValue[], port: Port | null = null) {
        this.names = names;
        this.array = array;
        this.port = port;
        for (const node of array) {
            if (node instanceof Struct && nodes.classOf(node, names) === "pedestrian") {
                for (const link of this.linksToWaypoints(node) ?? []) {
                    for (const i of pedsCars.follow(array, link, names)) this.pedPaths.add(i);
                }
            }
        }
    }

    private count(what: string): void {
        this.counts.set(what, (this.counts.get(what) ?? 0) + 1);
    }

    private linksToWaypoints(node: Struct): number[] | null {
        const links = node.get("Links");
        if (!Array.isArray(links)) return null;
        const out = links.filter(
            (v): v is number =>
                typeof v === "number" &&
                Number.isInteger(v) &&
                v >= 0 &&
                v < this.array.length &&
                this.array[v] instanceof Struct &&
                nodes.classOf(this.array[v] as Struct, this.names) === "waypoint",
        );
        const unique = [...new Set(out)];
        return unique.length ? unique : null;
    }

    private base(node: Struct, cls: string): qb.Entry[] {
        const n = this.names;
        return [
            [key(n, "Position"), node.get("Position")!],
            [key(n, "Angles"), node.get("Angles") ?? new Vector("vector", [0.0, 0.0, 0.0])],
            [key(n, "Name"), node.get("Name")!],
            [key(n, "Class"), key(n, cls)],
        ];
    }

    private made(entries: qb.Entry[], node: Struct, context: scriptPort.Context): Struct {
        const s = new Struct(entries);
        this.ids.add(s);
        const script = node.get("TriggerScript");
        const name = node.get("Name");
        if (script instanceof Name && name instanceof Name) {
            this.triggers.push([name.value, script.value, context]);
            this.scripts.add(script.value);
        }
        return s;
    }

    private flags(node: Struct): qb.Entry[] {
        return ["CreatedAtStart", "AbsentInNetGames"]
            .filter((f) => node.hasFlag(f))
            .map((f): qb.Entry => [null, key(this.names, f)]);
    }

    private levelObject(node: Struct, what: string): Struct {
        // A LevelObject (moved by its scripts; drawn with the sector of its name - movingSector).
        const n = this.names;
        this.moving.add(nameOf(node));
        this.count(what);
        const links = this.linksToWaypoints(node);
        return this.made(
            [
                ...this.base(node, "LevelObject"),
                [key(n, "Type"), key(n, "Normal")],
                ...this.flags(node),
                [key(n, "SuspendDistance"), 0],
                [key(n, "lod_dist1"), 10000],
                [key(n, "lod_dist2"), 10001],
                ...(links ? [[key(n, "Links"), links] as qb.Entry] : []),
            ],
            node,
            scriptPort.OBJECT,
        );
    }

    private leftOut(what: string): Struct {
        this.count(what);
        return new Struct([[key(this.names, "Class"), key(this.names, "LeftOut")]]);
    }

    private modelObject(node: Struct, clumps: ReadonlySet<number>, what: string): Struct {
        // A game / bouncy object drawn with a model: a clump of the level's .dff (model=<name>: Cruise Ship's
        // sailboats, deck chairs...) or a rigid model of Data\Models (model="police_car") - a LevelObject with a copy
        // of the geometry; THPS3's goal models (letters, stat points, tapes, deck icons) are left out. Else: no model.
        const model = node.get("model");
        const name = nameOf(node);
        if (model instanceof Name && clumps.has(model.value) && this.port !== null) {
            this.port.copies.set(name, model.value);
            return this.levelObject(node, what);
        }
        if (typeof model === "string") {
            if (GOAL_MODELS.some((g) => model.toLowerCase().startsWith(g)))
                return this.leftOut(`goal objects left out (${GOAL_MODELS.join("/")}...)`);
            if (this.port?.model(model)) {
                this.port.vehicles.set(name, model);
                return this.levelObject(node, what);
            }
        }
        return this.leftOut("objects whose model isn't converted left out");
    }

    convert(node: Struct, clumps: ReadonlySet<number>): Struct | null {
        const n = this.names;
        const cls = nodes.classOf(node, n);
        const name = node.get("Name");
        if (!(name instanceof Name) || !(node.get("Position") instanceof Vector)) return null;
        if (cls === "gameobject" || cls === "bouncyobject") {
            if (node.get("model") !== undefined) return this.modelObject(node, clumps, "moving objects (models)");
            if (clumps.has(name.value)) return this.levelObject(node, "moving objects");
            // (no geometry: nothing to show)
            if (cls === "bouncyobject") return null;
            const links = this.linksToWaypoints(node);
            const tail: qb.Entry[] = links ? [[key(n, "Links"), links]] : [];
            this.count("script objects");
            return this.made(
                [
                    ...this.base(node, "GameObject"),
                    [key(n, "Type"), key(n, "Ghost")],
                    ...this.flags(node),
                    [null, key(n, "NeverSuspend")],
                    [key(n, "model"), "none"],
                    [key(n, "lod_dist1"), 400],
                    [key(n, "lod_dist2"), 401],
                    ...tail,
                ],
                node,
                scriptPort.OBJECT,
            );
        }
        if (cls === "vehicle") {
            // THPS3's traffic (a trigger script calling Car_Generic01, as THUG2's): THUG2's own cars on the same
            // waypoints (car).
            if (this.callsCarGeneric(node)) return this.car(node);
            // THPS3's other vehicles (Tokyo's bullet trains and planes, Cruise Ship's boats, the snowmobiles) are path
            // followers moved by their scripts (Obj_FollowPathLinked...): LevelObjects of their models' geometry.
            const model = node.get("model");
            if (!(typeof model === "string" && this.port?.model(model)))
                return this.leftOut("vehicles without a rigid model left out");
            this.port.vehicles.set(name.value, model);
            return this.levelObject(node, "vehicles");
        }
        if (cls === "animatingobject") return this.animating(node);
        if (cls === "proximnode") return this.proxim(node);
        if (cls === "waypoint") {
            const links = this.linksToWaypoints(node);
            const flags: qb.Entry[] = node.hasFlag("CreatedAtStart") ? [[null, key(n, "CreatedAtStart")]] : [];
            this.count("waypoints");
            let ped: qb.Entry[] = [];
            const index = this.indexOf(node);
            if (index !== undefined && this.pedPaths.has(index)) {
                // (as THUG2's LA's ped paths; THPS3's objects moved along the same waypoints don't mind)
                ped = [
                    [key(n, "Type"), key(n, "PedAI")],
                    [null, key(n, "Continue")],
                    [key(n, "PedType"), key(n, "Walk")],
                    [key(n, "Priority"), key(n, "Normal")],
                    [key(n, "ContinueWeight"), new Float(1.0)],
                ];
            }
            const s = new Struct([
                ...this.base(node, "Waypoint"),
                ...flags,
                ...ped,
                ...(links ? [[key(n, "Links"), links] as qb.Entry] : []),
            ]);
            this.ids.add(s);
            return s;
        }
        if (cls === "pedestrian" || cls === "pedestrian_pro") return this.pedestrian(node);
        return null;
    }

    indexOf(node: Struct): number | undefined {
        if (this.index === null) {
            this.index = new Map();
            this.array.forEach((x, k) => {
                this.index!.set(x, k);
            });
        }
        return this.index.get(node);
    }

    profile(model: QbValue | undefined): string {
        // A THUG2 ped profile like THPS3's model: its pro, a worker, a guard, a woman, else any man.
        const assets = this.port !== null ? this.port.assets : null;
        const text = typeof model === "string" ? model.toLowerCase() : "";
        if (text.startsWith("pedpro_") && assets && typeof model === "string") {
            for (const candidate of [`PedPro_${model.slice(7)}`, `PedPro_${model.slice(7)}_skateboard`]) {
                if (assets.profileExists(candidate)) return candidate;
            }
        }
        for (const [part, profile] of [
            ["constr", "Ped_Construction_Worker_01"],
            ["welder", "Ped_Construction_Worker_01"],
            ["security", "Ped_Security_Guard_01"],
            ["fem", FEMALE],
            ["girl", FEMALE],
            ["steamer", FEMALE],
        ] as const) {
            if (text.includes(part) && (assets === null || assets.profileExists(profile))) return profile;
        }
        return "random_male_profile";
    }

    pedestrian(
        node: Struct,
        model: string | null = null,
        script: string | null = null,
        what: string | null = null,
    ): Struct {
        // THPS3's peds (skinned models, animations and scripts THUG2 can't run) as THUG2's own ped AI at the same
        // place: walking the waypoints they link to (THPS3's walkers), else standing (the workers at their posts,
        // the photographers, the crowds...). There at the start as in THPS3; the others when a script creates them.
        // Women as THUG2's levels write them (Ped_F_From_Profile, the Ped_Female skeleton and animations).
        const n = this.names;
        const links = script === null ? this.linksToWaypoints(node) : null;
        const walking = Boolean(links);
        // (Python's `model or node.get("model") or node.get("Model")`)
        let chosen: QbValue | undefined = model ?? undefined;
        if (!pyTruthy(chosen)) chosen = node.get("model");
        if (!pyTruthy(chosen)) chosen = node.get("Model");
        const profile = this.profile(chosen);
        const female = profile === FEMALE;
        const entries: qb.Entry[] = [
            ...this.base(node, "Pedestrian"),
            [key(n, "Type"), key(n, female ? "Ped_F_From_Profile" : "Ped_From_Profile")],
            ...this.flags(node),
            [key(n, "PedAI_Type"), key(n, "Stand")],
            [key(n, "skeletonName"), key(n, female ? "Ped_Female" : "THPS6_human")],
            [key(n, "AnimName"), key(n, female ? "animload_ped_female" : "animload_THPS6_human")],
            [key(n, "profile"), key(n, profile)],
            [key(n, "SuspendDistance"), 0],
            [key(n, "lod_dist1"), 200],
            [key(n, "lod_dist2"), 201],
            [key(n, "TriggerScript"), key(n, script || (walking ? PED_WALK : PED_STAND))],
        ];
        if (links) entries.push([key(n, "Links"), links]);
        this.count(what || (walking ? "walking peds" : "standing peds"));
        const s = new Struct(entries);
        this.ids.add(s);
        return s;
    }

    callsCarGeneric(node: Struct): boolean {
        // Whether the node's trigger script calls Car_Generic01 (THPS3's traffic script; THUG2 has its own of the name,
        // for its Vehicle objects - run by a LevelObject it crashes: no vehicle components).
        const script = node.get("TriggerScript");
        if (!(script instanceof Name) || this.port === null) return false;
        const car = qb.checksum("Car_Generic01");
        for (const source of [this.port.level, ...this.port.sources]) {
            if (source === null) throw new TypeError("'NoneType' object has no attribute 'scripts'");
            const body = source.scripts.get(script.value);
            if (body !== undefined) return body.some((t) => t.t === 0x16 && t.name() === car);
        }
        return false;
    }

    car(node: Struct): Struct {
        // A THPS3 car as THUG2's level cars (THUG2's LA: Class=Vehicle, a THUG2 car model, skeletonName=car, running
        // THUG2's Car_Generic01 along the linked waypoints) - a THUG2 model like THPS3's (THUG2 can't draw THPS3's
        // models as a vehicle: skeleton). Only the ones there at the start: THPS3's spare cars (not CreatedAtStart)
        // are made by its car scripts, not brought along.
        const n = this.names;
        const links = this.linksToWaypoints(node);
        if (!node.hasFlag("CreatedAtStart") || !links) return this.leftOut("cars not there at the start left out");
        const modelValue = node.get("model");
        const model = typeof modelValue === "string" ? modelValue : "";
        const text = model.toLowerCase();
        const name = n.get(nameOf(node)) ?? "";
        let choices = (CAR_MODELS.find(([parts]) => parts.some((p) => text.includes(p))) ??
            CAR_MODELS[CAR_MODELS.length - 1]!)[1] as readonly string[];
        const assets = this.port!.assets;
        const existing = choices.filter((c) => assets.modelExists(c));
        choices = existing.length ? existing : [pedsCars.FALLBACK_CAR];
        const chosen = choices[qb.checksum(name) % choices.length]!;
        this.count("cars (THUG2's)");
        const s = new Struct([
            ...this.base(node, "Vehicle"),
            [key(n, "Type"), key(n, "Car")],
            ...this.flags(node),
            [key(n, "model"), chosen],
            [key(n, "skeletonName"), key(n, "car")],
            [key(n, "SuspendDistance"), 10000],
            [key(n, "lod_dist1"), 400],
            [key(n, "lod_dist2"), 401],
            [key(n, "TriggerScript"), key(n, text.includes("bus") ? CAR_BUS : CAR)],
            [key(n, "Links"), links],
        ]);
        this.ids.add(s);
        return s;
    }

    animating(node: Struct): Struct {
        // THPS3's AnimatingObjects (skinned, animated by its own scripts): the crowds as THUG2's standing peds (a mix
        // of men and women, watching), the resting birds as THUG2's pigeons / seagulls (flying off when the skater
        // comes near and back again: ANIMAL_SCRIPTS), the sharks as THUG2's swimming their waypoints. THPS3's flying
        // birds (made by the resting ones' scripts) are left out.
        const n = this.names;
        const kind = nodes.typeOf(node, n);
        if (kind.startsWith("crowd")) {
            const model =
                !kind.startsWith("crowd_judge") && qb.checksum(n.get(nameOf(node)) ?? "") & 1 ? "pedfem" : "crowd";
            return this.pedestrian(node, model, PED_STAND, "crowds as standing peds");
        }
        const animal = ANIMALS.get(kind);
        if (animal === undefined)
            return this.leftOut(`animated objects left out (${kind.startsWith("bird") ? kind : "other"})`);
        const [skeleton, script] = animal;
        this.animals.add(skeleton);
        const links = script === ANIMAL_SHARK ? this.linksToWaypoints(node) : null;
        if (script === ANIMAL_SHARK && !links) return this.leftOut("animated objects left out (shark without a path)");
        const entries: qb.Entry[] = [
            ...this.base(node, "Pedestrian"),
            [key(n, "Type"), key(n, skeleton)],
            ...this.flags(node),
            ...(script === ANIMAL_SHARK ? [[null, key(n, "NoPedLogic")] as qb.Entry] : []),
            [key(n, "PedAI_Type"), key(n, "Stand")],
            [key(n, "SkeletonName"), key(n, skeleton)],
            [key(n, "AnimName"), key(n, `animload_${skeleton}`)],
            [key(n, "model"), `animals\\${skeleton.toLowerCase()}\\${skeleton.toLowerCase()}.skin`],
            [key(n, "SuspendDistance"), 0],
            [key(n, "lod_dist1"), 400],
            [key(n, "lod_dist2"), 401],
            [key(n, "TriggerScript"), key(n, script)],
            ...(links ? [[key(n, "Links"), links] as qb.Entry] : []),
        ];
        this.count(`animals (${skeleton})`);
        const s = new Struct(entries);
        this.ids.add(s);
        return s;
    }

    proxim(node: Struct): Struct {
        // A THPS3 proximity node (a sphere round the node: its script runs with `inside` when the camera comes within
        // `radius`, without when it leaves - Airport's escalators swap static and moving steps, Canada's chimneys
        // smoke): a ghost game object of the same name (Create / Kill of it still work) whose own script watches the
        // radius (THUG2's ObjectInRadius / ObjectOutOfRadius exceptions) and spawns the THPS3 script
        // (proximScriptsNs, once the script is known to be brought along).
        const n = this.names;
        const script = node.get("TriggerScript");
        const radius = node.get("radius");
        if (!(script instanceof Name) || !isNumberValue(radius))
            return this.leftOut("proximity nodes without a script / radius left out");
        this.proxims.push([nameOf(node), script.value, numberValue(radius)]);
        this.scripts.add(script.value);
        this.count("proximity nodes");
        const s = new Struct([
            ...this.base(node, "GameObject"),
            [key(n, "Type"), key(n, "Ghost")],
            ...this.flags(node),
            [null, key(n, "NeverSuspend")],
            [key(n, "model"), "none"],
            [key(n, "lod_dist1"), 400],
            [key(n, "lod_dist2"), 401],
        ]);
        this.ids.add(s);
        return s;
    }
}

export function loadAnimsNs(animals: Iterable<string>): string {
    // THUG2's load_level runs the level's load_level_anims (every THUG2 level has one): the peds' animations
    // (THPS6_human, Ped_Female) and the animals'.
    const all = ["THPS6_human", "ped_female", ...[...animals].sort(compareStr)];
    return `script load_level_anims {\n${all.map((a) => `    animload_${a} \n`).join("")}}\n`;
}

export function proximScriptsNs(name: number, script: number, radius: number, names: Names): [string, string] {
    // A proximity node's ghost's scripts (PortedNodes.proxim): THPS3's radius is in inches, THUG2's Obj_Set...Radius
    // in feet; the way out 2 feet further (no flicker on the edge). THPS3's script is spawned (run by no object, as
    // THPS3 ran it), with `inside` on the way in.
    const b = `thps3_proxim_${hex8(name)}`;
    const inner = pyRound(radius / 12.0, 2);
    const s = qb.nameText(script, names);
    const r = pyFloatRepr(inner);
    const o = pyFloatRepr(inner + 2.0);
    const text = `script ${b} {
    Obj_ClearExceptions
    Obj_SetInnerRadius ${r}
    Obj_SetException Ex=ObjectInRadius Scr=${b}_in
}
script ${b}_in {
    Obj_ClearExceptions
    SpawnScript ${s} params={inside}
    Obj_SetOuterRadius ${o}
    Obj_SetException Ex=ObjectOutOfRadius Scr=${b}_out
}
script ${b}_out {
    Obj_ClearExceptions
    SpawnScript ${s}
    GoTo ${b}
}
`;
    return [text, b];
}

export function portScripts(
    port: Port,
    converted: Struct[],
    array: readonly QbValue[],
    kills: ReadonlyMap<number, string>,
    names: Names,
    level: string,
    base: string,
    player1: Struct | null,
    made: PortedNodes,
    undrawn: Iterable<number> = [],
    extra: ExtraScripts = { provided: [], roots: [], replacing: new Map() },
    shown: readonly string[] = [],
    freeSkate: FreeSkateCompat = { flags: [], kill: [] }, // (TP-41: flags set before the start scripts run, nodes killed after)
): [Map<number, Buffer>, string | null, string, string] {
    // The level's scripts brought along (tools/level_port/script_port.ts) from its nodes' trigger scripts - the
    // sectors' and rails' (run by the skater), the made objects' (run by them) - and its start scripts: the scripts
    // THPS3's Load_<base> calls (Foun_Startup...) and the Player1 restart's trigger script (THPS3 runs it when the
    // skater starts: Foundry's sets the press up, makes the bucket). Those run from a setup script THUG2's load_level
    // calls (the level structure's setup_script). Trigger scripts that aren't brought along are taken off their nodes
    // (THUG2 runs a TriggerScript without checking it exists). Returns [{name: bytes}, the setup script as
    // NeverScript text or null, the THPS3 globals the scripts use as NeverScript text, a report line].
    if (port.level === null) throw new TypeError("no level source");
    const sources = [...port.sources, port.level];
    const allowed = new Set([...scriptPort.DEFAULT_SOURCES, "level"]);
    const allNames = new Set<number>();
    for (const n of array) if (n instanceof Struct && n.get("Name") instanceof Name) allNames.add(nameOf(n));
    const nodeNames = new Set<number>();
    for (const n of converted) if (n.get("Name") instanceof Name) nodeNames.add(nameOf(n));
    // (the kill scripts and LoadTerrain are the node array text's own - nodes.nodeArrayNs)
    const provided = new Set<number>([
        ...kills.keys(),
        ...["LoadTerrain", PED_WALK, PED_STAND, ANIMAL_BIRD, ANIMAL_SHARK, CAR, CAR_BUS].map(qb.checksum),
        ...extra.provided, // (TPN-THPS3: THUGPRO's new scripts, added to the file after)
    ]);
    const tr = new scriptPort.Translator(
        sources,
        port.thug2,
        (label) => allowed.has(label),
        nodeNames,
        allNames,
        names,
        // (THPS3's rotation deceleration in THUG2's terms - thps3_rotation.ts)
        { provided, inheritParams: true, undrawn, rewrites: ROTATION_REWRITES, shownPrefixes: shown },
    );
    const roots: [number, scriptPort.Context][] = [];
    for (const node of converted) {
        const script = node.get("TriggerScript");
        if (
            script instanceof Name &&
            !kills.has(script.value) &&
            ["levelgeometry", "railnode"].includes(nodes.classOf(node, names))
        ) {
            roots.push([script.value, scriptPort.SKATER]);
        }
    }
    for (const [, script, context] of made.triggers) roots.push([script, context]);
    for (const [, script] of made.proxims) roots.push([script, scriptPort.NONE]);
    const starts: number[] = [];
    for (const [command] of statements(port.levelsQ, qb.checksum(`Load_${base}`))) {
        if (tr.canBring(command)) starts.push(command);
    }
    const p1Script = player1?.get("TriggerScript");
    if (p1Script instanceof Name) starts.push(p1Script.value);
    for (const s of starts) roots.push([s, scriptPort.NONE]);
    // (TPN-THPS3: the THPS3 scripts THUGPRO's new nodes and scripts use)
    for (const [s, context] of extra.roots) if (tr.canBring(s)) roots.push([s, context]);
    const out = tr.run(roots);
    // (TPN-THPS3 `set` scripts: THUGPRO's versions of THPS3 scripts ours brings - a script ours hasn't is reported)
    const missing: string[] = [];
    for (const [s, code] of extra.replacing) {
        if (out.has(s)) out.set(s, code);
        else missing.push(qb.nameText(s, names));
    }

    const available = (script: number): boolean =>
        out.has(script) || port.thug2.scripts.has(script) || provided.has(script);

    const trigger = qb.checksum("TriggerScript");
    let dropped = 0;
    for (const node of converted) {
        const script = node.get("TriggerScript");
        if (script instanceof Name && !available(script.value)) {
            node.remove("TriggerScript");
            dropped += 1;
        }
    }
    const byName = new Map<number, Struct>();
    for (const n of converted) if (n.get("Name") instanceof Name) byName.set(nameOf(n), n);
    for (const [nodeName, script] of made.triggers) {
        const node = byName.get(nodeName);
        if (node !== undefined && available(script)) node.entries.push([new Name(trigger), new Name(script)]);
    }
    // the proximity nodes' ghosts: watching their radius when their THPS3 script is brought along and does something
    let proximText = "";
    let proxims = 0;
    for (const [nodeName, script, radius] of made.proxims) {
        const node = byName.get(nodeName);
        const code = out.get(script);
        if (node !== undefined && code !== undefined && scriptPort.doesSomething(code)) {
            const [text, wrapper] = proximScriptsNs(nodeName, script, radius, names);
            proximText += text;
            node.entries.push([new Name(trigger), key(names, wrapper)]);
            proxims += 1;
        }
    }
    let setup: string | null = null;
    const run = starts.filter((s) => out.has(s));
    if (run.length) {
        // (TP-41: THUGPRO's free skate is a network game - THPS3's multiplayer state; the flag is set before the
        // start scripts that read it run)
        const flags = freeSkate.flags.map((f) => `    SetFlag flag=${f} \n`).join("");
        // (nodes THUGPRO's free skate hasn't: killed a second after the start scripts made them)
        const killer = freeSkate.kill.length ? `    SpawnScript ${level}_free_skate \n` : "";
        setup = `script ${level}_setup {\n${flags}${run.map((s) => `    SpawnScript ${qb.nameText(s, names)} \n`).join("")}${killer}}\n`;
        if (freeSkate.kill.length) {
            setup += `script ${level}_free_skate {\n    Wait 1 Second \n${freeSkate.kill.map((k) => `    Kill Name=${k} \n`).join("")}}\n`;
        }
    }
    const worst = [...tr.leftOut].sort((a, b) => b[1] - a[1]);
    let bytes = 0;
    for (const code of out.values()) bytes += code.length;
    let leftOutCount = 0;
    for (const v of tr.leftOut.values()) leftOutCount += v;
    const report =
        (missing.length ? `TPN-THPS3: set on scripts ours hasn't (not added): ${missing.join(", ")}\n  ` : "") +
        `scripts: ${out.size} brought along (${bytes} bytes), ${dropped} trigger scripts not; ${proxims} proximity ` +
        `nodes watching; made ${pyRepr(made.counts)}; left out ${leftOutCount} statements: ` +
        worst
            .slice(0, 12)
            .map(([k, v]) => `${k} x${v}`)
            .join(", ");
    return [out, setup, tr.globalsNs() + proximText, report];
}

// TPN-THPS3: what portScripts takes from THUGPRO's new content - the script names it adds to the file (a trigger script
// naming one stays), the THPS3 scripts they use (brought along: [name, how it runs]), and THUGPRO's versions of THPS3
// scripts ours brings ({name: THUG2 bytes}, replacing ours).
export interface ExtraScripts {
    provided: number[];
    roots: [number, scriptPort.Context][];
    replacing: Map<number, Buffer>;
}

// --- capture the flag ----------------------------------------------------------------------------------------------

// THPS3's capture the flag, as THUG2's. THPS3 (2 teams): each team's flag base is a GenericNode of Type CTF_1 / CTF_2
// (gamemode.qb's flag1_info / flag2_info: {name="blue flag" ... node_type_checksum=ctf_1}, {name="red flag" ...
// ctf_2}), and the team's players start at its restarts of type CTF_1 / CTF_2 (four per level). THUG2 (net\ctf.qb,
// SK4_Scripts' Create_Team_Flags / Team_Flag) looks the game objects up by name: TRG_CTF_<Colour> (the flag, its
// trigger script calling Team_Flag <colour>) and TRG_CTF_<Colour>_Base, and its skater code (0x00566100) puts a team
// at TRG_CTF_Restart_<Colour> (team 0 Red, 1 Blue, 2 Green, 3 Yellow). Made from THPS3's nodes in THUG2's own levels'
// form (its Airport's: Type Flag_<Colour> / Flag_<Colour>_Base, NeverSuspend, THUG2's gameobjects\flags\ models,
// lod_dist 800 / 801; the restart Type CTF, restart_types [CTF MultiPlayer Horse], "Team: CTF <Colour>"): the flag and
// its base at THPS3's base node, the team's restart at THPS3's restart of the team's type nearest to its base. THPS3
// has no Green / Yellow team and no lobby team flags (TRG_Flag_*) or team restart (TRG_Team_Restart): none is made.
export const CTF_COLOURS = ["Red", "Blue", "Green", "Yellow"]; // THUG2's teams 0-3 (net\ctf.qb relocate_flag)

export interface FlagTeam {
    colour: string; // THUG2's: "Blue"
    type: number; // THPS3's node type: checksum of CTF_1
}

// The teams of THPS3's gamemode.qb (flag<n>_info): [{colour, node type}].
export function flagTeams(globalQbs: readonly QbFile[]): FlagTeam[] {
    const teams: FlagTeam[] = [];
    for (const q of globalQbs) {
        for (const [key, value] of q.globals) {
            const text = q.names.get(key.value) ?? "";
            if (!/^flag\d+_info$/i.test(text) || !(value instanceof Struct)) continue;
            const label = value.get("name");
            const type = value.get("node_type_checksum");
            const m = typeof label === "string" ? /^(\w+) flag$/i.exec(label) : null;
            if (!m || !(type instanceof Name)) continue;
            const colour = CTF_COLOURS.find((c) => c.toLowerCase() === m[1]!.toLowerCase());
            if (colour !== undefined && !teams.some((t) => t.type === type.value))
                teams.push({ colour, type: type.value });
        }
    }
    return teams;
}

const vectorOf = (node: Struct, key: string): readonly number[] | null => {
    const v = node.get(key);
    return v instanceof Vector && v.kind === "vector" ? v.values : null;
};

// A node's place as THUGPRO's compatibility data gives it (tools/compat/thugpro/thps3_levels.json): THUG2's world (Pos).
export interface CompatPlacement {
    pos: number[];
    angles: number[];
}

// One level's compatibility data (compat profile thugpro; {} with --compat none): TP-10 `flags` - the Red / Blue flags
// and bases where THUGPRO has them (TRG_CTF_Red...); TP-11 `extra` - the team nodes THPS3 has no counterpart for
// (TRG_CTF_Green / _Yellow (+ _Base), the lobby's TRG_Flag_<colour> (+ _Base), TRG_Team_Restart); TP-12 `spawns` -
// the network restarts as THUGPRO has them (applySpawns).
export interface LevelCompat {
    flags: Record<string, CompatPlacement>;
    extra: Record<string, CompatPlacement>;
    spawns?: SpawnCompat;
    edits?: NodeEditsCompat; // TP-13 .. TP-18
    collision?: Record<string, CollisionEditCompat>; // TP-19
    freeSkate?: FreeSkateCompat; // TP-41
}

// TP-41: THUGPRO's free skate is a network game (THPS3's multiplayer state): level flags set at the start of ours
// (`flags`, before the start scripts) and nodes killed once the start scripts have made them (`kill`).
export interface FreeSkateCompat {
    flags: string[];
    kill: string[];
}

// THUGPRO's hand edits of a level's nodes (tools/thps3_levels/thugpro_compare.ts nodeEdits, the user's decision
// 2026-10-20: everything THUGPRO changed, as data under the compatibility profile): TP-13 `moved` - {name: place in
// THUG2's world, angles}; TP-14 `flags` - {name: flags added / removed}; TP-15 `terrain` - {name: TerrainType};
// TP-16 `removed` - names; TP-17 `renamed` - {THPS3's name: THUGPRO's}; TP-18 `rails` - added rail nodes.
export interface NodeEditsCompat {
    moved: Record<string, CompatPlacement>;
    flags: Record<string, { add: string[]; remove: string[] }>;
    terrain: Record<string, string>;
    removed: string[];
    renamed: Record<string, string>;
    rails: CompatRail[];
}

export interface CompatRail extends CompatPlacement {
    name: string;
    terrain: string | null;
    flags: string[];
    cluster: string | null;
    links: string[]; // by name
}

// TP-19: a collision object's collision left out (`drop`), or some of its faces (`remove`: each face's three corners
// in THPS3's order, THPS3's own vertices).
export interface CollisionEditCompat {
    drop?: boolean;
    remove?: number[][][];
}

// TP-12: a spawn as THUGPRO has it (place in THUG2's world, angles, Type, restart_types, RestartName, CreatedAtStart).
export interface SpawnPlacement extends CompatPlacement {
    type: string | null;
    restart_types: string[];
    restart_name: string | null;
    created: boolean;
}

export interface SpawnCompat {
    changed: Record<string, SpawnPlacement>; // by node name
    removed: string[]; // THPS3's network spawns THUGPRO doesn't have
    // every network spawn's name in THUGPRO's node-array order: THUG2 starts player n at the n-th Multiplayer restart of
    // the array and respawns at one of the first 8 (Skate 0x00566100 -> GetRestartNode 0x0046B790;
    // CSkater::MoveToRandomRestart 0x00580830: Rnd(8)) - the order decides who starts where
    order?: string[];
}

// TP-12 (approved by the user 2026-10-20: both sides should spawn players alike): the converted node array's network
// restarts made as THUGPRO's - a restart THUGPRO changed gets its place (Pos), angles, Type, restart_types, RestartName
// and CreatedAtStart; one THUGPRO added is appended in our restart form; one THUGPRO hasn't stops being a network spawn
// (Type / restart_types Generic: kept for the kills and scripts that name it). In place; returns a report line or "".
export function applySpawns(converted: Struct[], names: Names, spawns: SpawnCompat | undefined): string {
    if (spawns === undefined) return "";
    const n = (text: string): Name => {
        const c = text.startsWith("#") ? Number.parseInt(text.slice(1), 16) >>> 0 : qb.checksum(text);
        if (!text.startsWith("#") && !names.has(c)) names.set(c, text);
        return new Name(c);
    };
    const byName = new Map<number, Struct>();
    for (const node of converted) {
        const nm = node.get("Name");
        if (nm instanceof Name && nodes.classOf(node, names) === "restart") byName.set(nm.value, node);
    }
    let moved = 0;
    let added = 0;
    let removed = 0;
    for (const [name, s] of Object.entries(spawns.changed)) {
        const entries: qb.Entry[] = [
            [n("Pos"), new Vector("vector", [...s.pos])],
            [n("Angles"), new Vector("vector", [...s.angles])],
            [n("Name"), n(name)],
            [n("Class"), n("Restart")],
            ...(s.type !== null ? ([[n("Type"), n(s.type)]] as qb.Entry[]) : []),
            ...(s.created ? ([[null, n("CreatedAtStart")]] as qb.Entry[]) : []),
            ...(s.restart_name !== null ? ([[n("RestartName"), s.restart_name]] as qb.Entry[]) : []),
            [n("restart_types"), s.restart_types.map(n)],
        ];
        const mine = byName.get(qb.checksum(name));
        if (mine === undefined) {
            converted.push(new Struct(entries));
            added += 1;
            continue;
        }
        // (the node's other entries - TriggerScript, Links... - kept, after ours)
        const replaced = new Set([
            "pos",
            "position",
            "angles",
            "name",
            "class",
            "type",
            "restartname",
            "restart_types",
        ]);
        const rest = mine.entries.filter(([k, v]) =>
            k === null
                ? !(v instanceof Name && v.value === qb.checksum("CreatedAtStart"))
                : ![...replaced].some((r) => qb.checksum(r) === k.value),
        );
        mine.entries = [...entries, ...rest];
        moved += 1;
    }
    for (const name of spawns.removed) {
        const mine = byName.get(qb.checksum(name));
        if (mine === undefined) continue;
        mine.set("Type", n("Generic"));
        mine.set("restart_types", [n("Generic")]);
        removed += 1;
    }
    let ordered = "";
    if (spawns.order !== undefined) {
        // (the spawns' slots in the array, refilled in THUGPRO's order; every node's Links follow - as
        // tools/thug1_levels' TP-31)
        const isNetSpawn = (node: Struct): boolean => {
            const types = node.get("restart_types");
            return (
                nodes.classOf(node, names) === "restart" &&
                Array.isArray(types) &&
                types.some(
                    (x) => x instanceof Name && nodes.NET_RESTART_TYPES.has((names.get(x.value) ?? "").toLowerCase()),
                )
            );
        };
        const slots = converted.flatMap((node, i) => (isNetSpawn(node) ? [i] : []));
        const index = new Map<number, number>();
        converted.forEach((node, i) => {
            const nm = node.get("Name");
            if (nm instanceof Name && !index.has(nm.value)) index.set(nm.value, i);
        });
        const wanted = spawns.order.map((name) => index.get(qb.checksum(name)) ?? -1);
        if ([...wanted].sort((a, b) => a - b).join() !== slots.join())
            throw new FormatError("TP-12's spawn order names other restarts than the level's spawns");
        const moveTo = new Map(wanted.map((from, k) => [from, slots[k]!] as const)); // old index -> new
        const before = [...converted];
        for (const [from, to] of moveTo) converted[to] = before[from]!;
        const links = qb.checksum("Links");
        for (const node of converted) {
            node.entries = node.entries.map(([k, v]) => [
                k,
                k !== null && k.value === links && Array.isArray(v)
                    ? v.map((x) => (typeof x === "number" && moveTo.has(x) ? moveTo.get(x)! : x))
                    : v,
            ]);
        }
        if (wanted.some((from, k) => from !== slots[k])) ordered = `, ${slots.length} spawns in THUGPRO's order`;
    }
    return `TP-12 spawns as THUGPRO's: ${moved} changed, ${added} added, ${removed} no longer network spawns${ordered}`;
}

export const NO_COMPAT: LevelCompat = { flags: {}, extra: {} };

// THPS3's Position (z negated in the game) or THUG2's Pos, in THUG2's world.
function gamePosition(node: Struct): readonly number[] | null {
    const pos = vectorOf(node, "Pos");
    if (pos !== null) return pos;
    const p = vectorOf(node, "Position");
    return p === null ? null : [p[0]!, p[1]!, -p[2]!];
}

// The THUG2 game-mode nodes for THPS3's flag bases (appended to the node array: no Links move) and their trigger
// scripts (NeverScript text). `array`: THPS3's node array; `names` gets the new names. `compat`: the level's THUGPRO
// compatibility data (TP-10 / TP-11, approved by the user 2026-10-20): only places and colours come from it - the
// nodes' form stays THUG2's own (as above); the team restarts are THPS3's restarts here (TP-12's applySpawns then
// places them as THUGPRO's).
export function ctfNodes(
    array: readonly QbValue[],
    names: Names,
    teams: readonly FlagTeam[],
    compat: LevelCompat = NO_COMPAT,
): [Struct[], string, string] {
    const out: Struct[] = [];
    const scripts: string[] = [];
    const made: string[] = [];
    const n = (text: string): Name => {
        const c = qb.checksum(text);
        if (!names.has(c)) names.set(c, text);
        return new Name(c);
    };
    const nodesOf = (test: (s: Struct) => boolean): Struct[] =>
        array.filter((x): x is Struct => x instanceof Struct && test(x));
    const restartNodes = nodesOf((s) => nodes.classOf(s, names) === "restart");
    const textOf = (node: Struct): string => names.get(nameOf(node)) ?? hex8(nameOf(node));
    const placed = (where: qb.Entry[], name: string, type: string, model: string, script: string | null): Struct =>
        new Struct([
            ...where,
            [n("Name"), n(name)],
            [n("Class"), n("GameObject")],
            [n("Type"), n(type)],
            [null, n("NeverSuspend")],
            [n("Model"), `gameobjects\\flags\\${model}\\${model}.mdl`],
            [n("lod_dist1"), 800],
            [n("lod_dist2"), 801],
            ...(script !== null ? ([[n("TriggerScript"), n(script)]] as qb.Entry[]) : []),
        ]);
    const at = (key: string, values: readonly number[]): qb.Entry => [n(key), new Vector("vector", [...values])];
    const fromCompat = (p: CompatPlacement): qb.Entry[] => [at("Pos", p.pos), at("Angles", p.angles)];
    const teamRestart = (c: string, r: Struct): Struct =>
        new Struct([
            [n("Position"), new Vector("vector", [...vectorOf(r, "Position")!])],
            [n("Angles"), new Vector("vector", [...(vectorOf(r, "Angles") ?? [0, 0, 0])])],
            [n("Name"), n(`TRG_CTF_Restart_${c}`)],
            [n("Class"), n("Restart")],
            [n("Type"), n("CTF")],
            [null, n("CreatedAtStart")],
            [n("RestartName"), `Team: CTF ${c}`],
            [n("restart_types"), [n("CTF"), n("MultiPlayer"), n("Horse")]],
        ]);
    // THPS3's restart nearest to `p` (THUG2's world) whose restart_types has one of `types`, not in `used` (the first
    // of equals)
    const used = new Set<Struct>();
    const nearest = (p: readonly number[], types: ReadonlySet<number>): Struct | null => {
        let best: [number, Struct] | null = null;
        for (const r of restartNodes) {
            const kinds = r.get("restart_types");
            const q = gamePosition(r);
            if (used.has(r) || !Array.isArray(kinds) || q === null) continue;
            if (!kinds.some((t) => t instanceof Name && types.has(t.value))) continue;
            const [dx, dy, dz] = [q[0]! - p[0]!, q[1]! - p[1]!, q[2]! - p[2]!];
            const d = dx * dx + dy * dy + dz * dz;
            if (best === null || d < best[0]) best = [d, r];
        }
        if (best !== null) used.add(best[1]);
        return best === null ? null : best[1];
    };
    const teamTypes = new Set(teams.map((t) => t.type));
    for (const team of teams) {
        const base = nodesOf(
            (s) =>
                nodes.classOf(s, names) === "genericnode" &&
                s.get("Type") instanceof Name &&
                (s.get("Type") as Name).value === team.type,
        )[0];
        const c = team.colour;
        const lower = c.toLowerCase();
        const theirs = compat.flags[`TRG_CTF_${c}`];
        const theirBase = compat.flags[`TRG_CTF_${c}_Base`] ?? theirs;
        if (theirs !== undefined && theirBase !== undefined) {
            // TP-10: the flag and its base where THUGPRO has them; the team's restart THPS3's team restart (either
            // team's type) nearest to that flag
            out.push(placed(fromCompat(theirs), `TRG_CTF_${c}`, `Flag_${c}`, `flag_${lower}`, `TRG_CTF_${c}Script`));
            out.push(placed(fromCompat(theirBase), `TRG_CTF_${c}_Base`, `Flag_${c}_Base`, `flag_${lower}_base`, null));
            scripts.push(`script TRG_CTF_${c}Script {\n    Team_Flag ${lower} \n}\n`);
            const r = nearest(theirs.pos, teamTypes);
            if (r !== null) out.push(teamRestart(c, r));
            made.push(`${c} (THUGPRO's place - TP-10, restart at ${r ? textOf(r) : "none"})`);
            continue;
        }
        const where = base ? vectorOf(base, "Position") : null;
        if (!base || where === null) continue;
        const angles = vectorOf(base, "Angles") ?? [0, 0, 0];
        const position: qb.Entry[] = [at("Position", where), at("Angles", angles)];
        out.push(placed(position, `TRG_CTF_${c}`, `Flag_${c}`, `flag_${lower}`, `TRG_CTF_${c}Script`));
        out.push(placed(position, `TRG_CTF_${c}_Base`, `Flag_${c}_Base`, `flag_${lower}_base`, null));
        scripts.push(`script TRG_CTF_${c}Script {\n    Team_Flag ${lower} \n}\n`);
        // the team's restart: THPS3's restart of the team's type nearest to the base (the first of equals)
        let best: [number, Struct] | null = null;
        for (const r of restartNodes) {
            const types = r.get("restart_types");
            const p = vectorOf(r, "Position");
            if (!Array.isArray(types) || p === null || !types.some((t) => t instanceof Name && t.value === team.type))
                continue;
            const [dx, dy, dz] = [p[0]! - where[0]!, p[1]! - where[1]!, p[2]! - where[2]!];
            const d = dx * dx + dy * dy + dz * dz;
            if (best === null || d < best[0]) best = [d, r];
        }
        let restartNote = "no restart of its type";
        if (best !== null) {
            used.add(best[1]);
            out.push(teamRestart(c, best[1]));
            restartNote = `restart at ${textOf(best[1])}`;
        }
        made.push(`${c} (base ${textOf(base)}, ${restartNote})`);
    }
    // TP-11: the teams THPS3 doesn't have (their restarts THPS3's multiplayer restarts nearest to their flags), the
    // lobby's team flags and the team restart, where THUGPRO has them
    const multiplayer = new Set([qb.checksum("MultiPlayer")]);
    const extraMade: string[] = [];
    for (const c of CTF_COLOURS) {
        const flag = compat.extra[`TRG_CTF_${c}`];
        if (flag === undefined || teams.some((t) => t.colour === c)) continue;
        const lower = c.toLowerCase();
        const base = compat.extra[`TRG_CTF_${c}_Base`] ?? flag;
        out.push(placed(fromCompat(flag), `TRG_CTF_${c}`, `Flag_${c}`, `flag_${lower}`, `TRG_CTF_${c}Script`));
        out.push(placed(fromCompat(base), `TRG_CTF_${c}_Base`, `Flag_${c}_Base`, `flag_${lower}_base`, null));
        scripts.push(`script TRG_CTF_${c}Script {\n    Team_Flag ${lower} \n}\n`);
        const r = nearest(flag.pos, multiplayer);
        if (r !== null) out.push(teamRestart(c, r));
        extraMade.push(c);
    }
    for (const c of CTF_COLOURS) {
        const lower = c.toLowerCase();
        const flag = compat.extra[`TRG_Flag_${c}`];
        if (flag !== undefined) {
            out.push(placed(fromCompat(flag), `TRG_Flag_${c}`, `Flag_${c}`, `flag_${lower}`, `TRG_Flag_${c}Script`));
            scripts.push(`script TRG_Flag_${c}Script {\n    Team_Flag ${lower} \n}\n`);
            extraMade.push(`TRG_Flag_${c}`);
        }
        const flagBase = compat.extra[`TRG_Flag_${c}_Base`];
        if (flagBase !== undefined) {
            out.push(placed(fromCompat(flagBase), `TRG_Flag_${c}_Base`, `Flag_${c}_Base`, `flag_${lower}_base`, null));
            extraMade.push(`TRG_Flag_${c}_Base`);
        }
    }
    const team = compat.extra.TRG_Team_Restart;
    if (team !== undefined) {
        out.push(
            new Struct([
                ...fromCompat(team),
                [n("Name"), n("TRG_Team_Restart")],
                [n("Class"), n("Restart")],
                [n("Type"), n("Team")],
                [n("RestartName"), "Team: Restart"],
                [n("restart_types"), [n("Team")]],
            ]),
        );
        extraMade.push("TRG_Team_Restart");
    }
    if (extraMade.length) made.push(`TP-11: ${extraMade.join(" ")}`);
    const report = made.length ? `capture the flag: ${made.join(", ")}` : "capture the flag: no THPS3 flag bases";
    return [out, scripts.join(""), report];
}

// The level's compatibility data from the profile (tools/compat/compat.ts; TP-10 ctf_flags, TP-11 team_nodes,
// TP-12 spawns).
export function levelCompat(profile: compat.Profile, level: string): LevelCompat {
    const get = (key: string): Record<string, CompatPlacement> =>
        (profile.section("thps3_levels", key)[level] ?? {}) as Record<string, CompatPlacement>;
    const spawns = profile.section("thps3_levels", "spawns")[level] as SpawnCompat | undefined;
    const out: LevelCompat = {
        flags: get("ctf_flags"),
        extra: get("team_nodes"),
        ...(spawns !== undefined ? { spawns } : {}),
    };
    // TP-13 .. TP-18 (each section {level: data}; a level without any keeps out.edits undefined)
    const of = <T>(key: string, empty: T): T => (profile.section("thps3_levels", key)[level] as T | undefined) ?? empty;
    const edits: NodeEditsCompat = {
        moved: of("moved", {}),
        flags: of("flags", {}),
        terrain: of("terrain", {}),
        removed: of("removed", []),
        renamed: of("renamed", {}),
        rails: of("rails", []),
    };
    if (Object.values(edits).some((v) => (Array.isArray(v) ? v.length : Object.keys(v).length))) out.edits = edits;
    const collision = profile.section("thps3_levels", "collision")[level] as
        | Record<string, CollisionEditCompat>
        | undefined;
    if (collision !== undefined) out.collision = collision;
    // TP-41 (the user's decision 2026-10-21 pattern: THUGPRO's state for online parity, documented as differing)
    const freeSkate = profile.section("thps3_levels", "free_skate")[level] as
        | { set_flags?: string[]; kill?: string[] }
        | undefined;
    if (freeSkate?.set_flags?.length || freeSkate?.kill?.length)
        out.freeSkate = { flags: freeSkate.set_flags ?? [], kill: freeSkate.kill ?? [] };
    return out;
}

// A name as the compatibility data writes it ("#1234ABCD" for a checksum without a name) -> its checksum; a new name
// goes into `names`.
function compatName(text: string, names: Names): Name {
    const c = text.startsWith("#") ? Number.parseInt(text.slice(1), 16) >>> 0 : qb.checksum(text);
    if (!text.startsWith("#") && !names.has(c)) names.set(c, text);
    return new Name(c);
}

// TP-13 .. TP-17 (the user's decision 2026-10-20): THUGPRO's hand edits applied to THPS3's own node array, in place
// (each changed node replaced by an edited copy), before anything is made of it - the converter then treats the
// nodes as if THPS3 had them so (a node made CreatedAtStart is there at the start, game objects are drawn where they
// were moved to...). Moved nodes keep THPS3's Position (z negated from THUG2's world). Returns [report, the names of
// the removed nodes (TP-16: left out as the converter leaves out its hidden ones, sectors and nodes)].
export function applyNodeEdits(
    array: QbValue[],
    names: Names,
    edits: NodeEditsCompat | undefined,
): [string, Set<number>] {
    const removed = new Set<number>();
    if (edits === undefined) return ["", removed];
    for (const name of edits.removed) removed.add(compatName(name, names).value);
    const counts = { moved: 0, flags: 0, terrain: 0, renamed: 0 };
    const byName = new Map<number, number>();
    array.forEach((n, i) => {
        if (n instanceof Struct && n.get("Name") instanceof Name && !byName.has(nameOf(n))) byName.set(nameOf(n), i);
    });
    const edit = (name: string, change: (node: Struct) => void): boolean => {
        const i = byName.get(compatName(name, names).value);
        if (i === undefined) return false;
        const node = new Struct([...(array[i] as Struct).entries]);
        change(node);
        array[i] = node;
        return true;
    };
    for (const [old, now] of Object.entries(edits.renamed)) {
        if (edit(old, (node) => node.set("Name", compatName(now, names)))) counts.renamed += 1;
    }
    for (const [name, p] of Object.entries(edits.moved)) {
        const done = edit(name, (node) => {
            if (node.get("Pos") instanceof Vector) node.set("Pos", new Vector("vector", [...p.pos]));
            else node.set("Position", new Vector("vector", [p.pos[0]!, p.pos[1]!, -p.pos[2]!]));
            node.set("Angles", new Vector("vector", [...p.angles]));
        });
        if (done) counts.moved += 1;
    }
    for (const [name, f] of Object.entries(edits.flags)) {
        const done = edit(name, (node) => {
            for (const flag of f.remove) node.removeFlag(flag);
            for (const flag of f.add) if (!node.hasFlag(flag)) node.entries.push([null, compatName(flag, names)]);
        });
        if (done) counts.flags += 1;
    }
    for (const [name, terrain] of Object.entries(edits.terrain)) {
        if (edit(name, (node) => node.set("TerrainType", compatName(terrain, names)))) counts.terrain += 1;
    }
    if (!names.has(qb.checksum("TerrainType"))) names.set(qb.checksum("TerrainType"), "TerrainType");
    const parts = [
        `${counts.moved} moved (TP-13)`,
        `${counts.flags} flags changed (TP-14)`,
        `${counts.terrain} terrains (TP-15)`,
        `${removed.size} removed (TP-16)`,
        `${counts.renamed} renamed (TP-17)`,
    ];
    return [`THUGPRO's node edits: ${parts.join(", ")}`, removed];
}

// TP-18: the rails THUGPRO added, as rail nodes of ours appended to the converted node array (Pos, Angles, Name,
// Class, TerrainType, flags, Cluster, Links). A Cluster naming no node of the level (Foundry's wires: THUGPRO's own
// foun_wire mesh, not ours) is left out with the TrickObject flag (a trick object's cluster has to exist). Returns a
// report line or "".
export function appendRails(converted: Struct[], names: Names, rails: readonly CompatRail[]): string {
    if (!rails.length) return "";
    const index = new Map<number, number>();
    const at = (node: Struct, i: number): void => {
        const nm = node.get("Name");
        if (nm instanceof Name && !index.has(nm.value)) index.set(nm.value, i);
    };
    converted.forEach(at);
    const start = converted.length;
    rails.forEach((r, k) => {
        index.set(compatName(r.name, names).value, start + k);
    });
    let unclustered = 0;
    for (const r of rails) {
        const cluster = r.cluster !== null ? compatName(r.cluster, names) : null;
        const keep = cluster !== null && index.has(cluster.value);
        if (cluster !== null && !keep) unclustered += 1;
        const flags = r.flags.filter((f) => keep || cluster === null || f !== "TrickObject");
        const links = r.links.map((l) => {
            const i = index.get(compatName(l, names).value);
            if (i === undefined) throw new FormatError(`TP-18: rail ${r.name} links to ${l}, not in the level`);
            return i;
        });
        converted.push(
            new Struct([
                [compatName("Pos", names), new Vector("vector", [...r.pos])],
                [compatName("Angles", names), new Vector("vector", [...r.angles])],
                [compatName("Name", names), compatName(r.name, names)],
                [compatName("Class", names), compatName("RailNode", names)],
                ...(r.terrain !== null
                    ? ([[compatName("TerrainType", names), compatName(r.terrain, names)]] as qb.Entry[])
                    : []),
                ...flags.map((f): qb.Entry => [null, compatName(f, names)]),
                ...(keep ? ([[compatName("Cluster", names), cluster]] as qb.Entry[]) : []),
                ...(links.length ? ([[compatName("Links", names), links]] as qb.Entry[]) : []),
            ]),
        );
    }
    const note = unclustered ? ` (${unclustered} without their cluster: not in the level)` : "";
    return `TP-18: ${rails.length} rails THUGPRO added${note}`;
}

export function convertNodeArray(
    q: QbFile,
    globalQbs: readonly QbFile[],
    name: string,
    base: string,
    startScripts: readonly number[],
    port: Port | null = null,
    teams: readonly FlagTeam[] = [],
    ctfCompat: LevelCompat = NO_COMPAT,
    tpNew: thugproNew.LevelNew = thugproNew.emptyNew(),
    pc: PortChanges = thps3PortChanges(),
): [string, Set<number>, Set<number>, string, Map<number, Buffer>, boolean] {
    // port: a Port (the level's scripts brought along, its game objects, waypoints, peds...) or null (static: only
    // the geometry's nodes, restarts, rails and kill zones). Returns [NeverScript text, sectors left out, sectors
    // keeping their trigger faces, report, the brought scripts {name: THUG2 bytes}, whether there's a setup script].
    const later = new Set<number>();
    const array = q.get("NodeArray");
    if (!Array.isArray(array)) throw new FormatError(`${q.name}: no NodeArray`);
    const names: Names = new Map();
    for (const g of globalQbs) for (const [k, v] of g.names) names.set(k, v);
    for (const [k, v] of q.names) names.set(k, v);
    // TP-13 .. TP-17: THUGPRO's hand edits, into THPS3's array itself (q's: objectSectors places the game objects
    // from it too)
    const [editReport, removedNames] = applyNodeEdits(array, names, ctfCompat.edits);
    const environment = qb.checksum("EnvironmentObject");
    const isEnvironment = (n: QbValue): n is Struct => {
        if (!(n instanceof Struct)) return false;
        const cls = n.get("Class");
        return cls instanceof Name && cls.value === environment;
    };
    // Environment objects not there at the start and not made by the level's start scripts (a press's other
    // position, a door open, LA's earthquake rubble: made by goal scripts THUG2 doesn't have) are left out, sectors
    // and nodes.
    const player1 =
        (array.find((n) => {
            if (!(n instanceof Struct) || nodes.classOf(n, names) !== "restart") return false;
            const type = n.get("Type");
            return type instanceof Name && type.value === qb.checksum("Player1");
        }) as Struct | undefined) ?? null;
    const starts = [...startScripts];
    const p1Script = player1?.get("TriggerScript");
    if (p1Script instanceof Name) starts.push(p1Script.value);
    // The ProximNodes' trigger scripts too: THPS3 swaps level-of-detail with them - Airport's escalators: outside, the
    // static escalator (Create name=Escalator01, not CreatedAtStart); inside, moving game objects (the steps, not
    // converted). Left out, the static escalators were holes.
    const proxim = qb.checksum("ProximNode");
    for (const n of array) {
        if (!(n instanceof Struct)) continue;
        const cls = n.get("Class");
        const script = n.get("TriggerScript");
        if (cls instanceof Name && cls.value === proxim && script instanceof Name) starts.push(script.value);
    }
    const [created, prefixes] = createdAtStart(q, globalQbs, names, base, starts, pc);
    const hidden = new Set<number>();
    // Sector sets a level's scripts keep switching between, left out (LEFT_OUT_PREFIXES): with both drawn they
    // fight over the same surfaces (flicker).
    const prefixesOut = leftOutPrefixes(pc, base); // (PC-THPS3-1)
    for (const node of array) {
        if (node instanceof Struct && node.get("Name") instanceof Name) {
            const text = (names.get(nameOf(node)) ?? "").toLowerCase();
            if (prefixesOut.some((p) => text.startsWith(p))) hidden.add(nameOf(node));
        }
    }
    for (const node of array) {
        if (isEnvironment(node) && node.get("Name") instanceof Name && !node.hasFlag("CreatedAtStart")) {
            const checksum = nameOf(node);
            const text = (names.get(checksum) ?? "").toLowerCase();
            if (created.has(checksum) || (text && prefixes.some((p) => text.startsWith(p)))) continue;
            if (port === null) hidden.add(checksum);
            // (kept, not there at the start: the level's scripts create them - a press's other position)
            else later.add(checksum);
        }
    }
    // (TP-16: the nodes THUGPRO took out, left out as the hidden ones)
    for (const r of removedNames) {
        hidden.add(r);
        later.delete(r);
    }
    // (every node keeps its place: the Links are indices into the array - nodes.convertNodes renumbers them; one
    // left out here is a placeholder of a class it drops)
    const kept: QbValue[] = [];
    const made = port !== null ? new PortedNodes(names, array, port) : null;
    for (let node of array) {
        if (!(node instanceof Struct) || (node.get("Name") instanceof Name && hidden.has(nameOf(node)))) {
            kept.push(new Struct([[new Name(qb.checksum("Class")), new Name(qb.checksum("LeftOut"))]]));
            continue;
        }
        if (made !== null) {
            const made1 = made.convert(node, port!.clumps);
            if (made1 !== null) {
                kept.push(made1);
                continue;
            }
        }
        if (isEnvironment(node)) {
            // (THPS3's sector nodes: THUG2's LevelGeometry)
            node = new Struct([...node.entries]);
            node.set("Class", new Name(qb.checksum("LevelGeometry")));
            if (!names.has(qb.checksum("LevelGeometry"))) names.set(qb.checksum("LevelGeometry"), "LevelGeometry");
            if (!node.hasFlag("CreatedAtStart") && !later.has(nameOf(node))) {
                // (made by the level's start scripts)
                node.entries.push([null, new Name(qb.checksum("CreatedAtStart"))]);
                if (!names.has(qb.checksum("CreatedAtStart")))
                    names.set(qb.checksum("CreatedAtStart"), "CreatedAtStart");
            }
        }
        let links = node.get("Links");
        if (links !== undefined && nodes.classOf(node, names) !== "railnode") {
            // (sector nodes link to restarts and each other for THPS3's scripts: nothing for THUG2 - its node
            // loader follows non-rail links as paths)
            node = new Struct([...node.entries]);
            node.remove("Links");
            links = undefined;
        }
        if (Array.isArray(links) && new Set(links).size !== links.length) {
            // (Foundry's TRG_117 has Links=[70, 70]: once is enough)
            node = new Struct([...node.entries]);
            node.set("Links", [...new Set(links)]);
        }
        kept.push(node);
    }
    const restarts = new Set<number>();
    for (const n of kept) {
        if (n instanceof Struct && nodes.classOf(n, names) === "restart" && n.get("Name") instanceof Name)
            restarts.add(nameOf(n));
    }
    let kills = nodes.killCommands([q, ...globalQbs], restarts, names);
    const wanted = new Set<number>();
    if (made !== null) {
        // the trigger scripts of the sectors and rails (run by the skater) and of the made nodes
        for (const node of kept) {
            if (!(node instanceof Struct)) continue;
            const script = node.get("TriggerScript");
            if (
                script instanceof Name &&
                !kills.has(script.value) &&
                ["levelgeometry", "railnode"].includes(nodes.classOf(node, names))
            ) {
                wanted.add(script.value);
            }
        }
        for (const s of made.scripts) wanted.add(s);
    }
    // (TP-12: the restarts keep THPS3's order - no Player1-first swap, 0x00566100 finds restarts by type - and the
    // spawns are then put in THUGPRO's order by applySpawns)
    const [converted, stats] = nodes.convertNodes(
        kept,
        names,
        kills,
        [],
        made ? made.ids : [],
        wanted,
        ctfCompat.spawns === undefined && pc.on("PC-THPS3-9"),
    );
    kills = nodes.addKillRestarts(converted, kills, names);
    // TPN-THPS3: THUGPRO's new nodes appended (their Links by name) and its `set` fields written into ours - before the
    // scripts are brought along (their trigger scripts count) and before TP-18's rails (Foundry's wires find their
    // foun_wire cluster)
    const newReport = tpNew.nodes.length || tpNew.sets.length ? thugproNew.applyNodes(converted, names, tpNew) : [];
    // PC-THPS3-15: plain moving props (Foundry's bucket, Tokyo's bullet trains) solid as THUG2 builds them: our
    // LevelObjects with CollisionMode=Geometry - their collision made in convertWorld (Port.solid) from THPS3's drawn
    // model; THUGPRO's Vehicle `set` and its collision of the name (TPN-THPS3-1: the bucket's) are left out for them
    // (cmdConvert)
    const moverChange = pc.get("PC-THPS3-15", base.toLowerCase());
    if (moverChange !== null && port !== null && made !== null) {
        const [solid, solidReport] = movers.makeSolid(
            converted,
            names,
            movers.moversFor(moverChange.objects, name),
            made.moving,
            "PC-THPS3-15",
        );
        for (const s of solid) port.solid.add(s);
        newReport.push(...solidReport);
    }
    if (port !== null) {
        // a ped profile THUG2 hasn't (THUGPRO's random_sk3_male_profile on its Rio ped, TPN-THPS3-2) crashes the game
        // when the ped is made (nodes.fixProfiles): THUG2's random one instead
        const fixed = nodes.fixProfiles(converted, names, (v) => port.assets.profiles.has(v));
        if (fixed.length) newReport.push(`  ped profiles THUG2 hasn't, made THUG2's: ${fixed.join(", ")}`);
        // every human ped (THUGPRO's added ones too) on the skeleton its profile's model is rigged to - a woman's
        // on THPS6_human is twisted (tools/level_port/ped_rigs.ts; load_level_anims loads both: loadAnimsNs)
        const rigs = pedRigs.fixPedRigs(converted, names, (v) => port.assets.femaleRigs.has(v));
        if (rigs.changed.length) newReport.push(`  peds given their profile's skeleton: ${rigs.changed.join(", ")}`);
    }
    if (port === null) {
        // (static: no scripts - THUGPRO's nodes keep none either)
        const added = new Set(tpNew.nodes.map((n) => n.node));
        for (const n of converted) if (added.has(n)) n.remove("TriggerScript");
    }
    let scripts = new Map<number, Buffer>();
    let setup: string | null = null;
    let globalsText = "";
    let scriptReport = "";
    if (port !== null && made !== null) {
        port.moving = made.moving;
        // the geometry nodes the scene has no sector for (collision-only, or left out): their Visible / Kill...
        // crash THUG2 (script_port Translator.withoutUndrawn)
        const undrawn = new Set<number>();
        for (const n of array) {
            if (isEnvironment(n) && n.get("Name") instanceof Name) {
                const v = nameOf(n);
                if (!port.drawn.has(v) || hidden.has(v)) undrawn.add(v);
            }
        }
        [scripts, setup, globalsText, scriptReport] = portScripts(
            port,
            converted,
            array,
            kills,
            names,
            name,
            base,
            player1,
            made,
            undrawn,
            thugproNew.extraScripts(tpNew, names),
            shownPrefixes(pc, base), // (PC-THPS3-1)
            ctfCompat.freeSkate,
        );
    }
    // (after the scripts: their trigger scripts are THUG2's Team_Flag, not THPS3 scripts to bring along)
    // (PC-THPS3-8: our CTF from THPS3's bases - TP-10 .. TP-12 build on it, so it stays with their data)
    const tpCtf = Object.keys(ctfCompat.flags).length || Object.keys(ctfCompat.extra).length || ctfCompat.spawns;
    const [ctf, ctfScripts, ctfReport] =
        pc.on("PC-THPS3-8") || tpCtf
            ? ctfNodes(array, names, teams, ctfCompat)
            : [[] as Struct[], "", "capture the flag: not made (PC-THPS3-8 off)"];
    converted.push(...ctf);
    const railReport = appendRails(converted, names, ctfCompat.edits?.rails ?? []); // (TP-18)
    const spawnReport = applySpawns(converted, names, ctfCompat.spawns); // (TP-12)
    const triggers = nodes.killTriggerNames(converted, kills);
    if (port !== null) {
        // (the sectors whose trigger script runs keep their trigger faces)
        for (const n of converted) {
            if (
                n.get("TriggerScript") instanceof Name &&
                n.get("Name") instanceof Name &&
                nodes.classOf(n, names) === "levelgeometry"
            ) {
                triggers.add(nameOf(n));
            }
        }
    }
    let text = nodes.nodeArrayNs(`${name}_NodeArray`, converted, names, kills) + ctfScripts;
    if (port !== null && made !== null) {
        text += globalsText + (setup ?? "") + PED_SCRIPTS + ANIMAL_SCRIPTS + CAR_SCRIPTS + loadAnimsNs(made.animals);
    }
    let report =
        `nodes: ${converted.length} of ${array.length} kept ${pyRepr(stats)}; ${triggers.size} kill zones, ` +
        `${hidden.size + later.size} sectors not there at the start`;
    if (scriptReport) report += `\n  ${scriptReport}`;
    report += `\n  ${ctfReport}`;
    if (spawnReport) report += `\n  ${spawnReport}`;
    if (editReport) report += `\n  ${editReport}`;
    if (railReport) report += `\n  ${railReport}`;
    for (const line of newReport) report += `\n${line}`;
    return [text, hidden, triggers, report, scripts, setup !== null];
}

export function writeLevelQb(
    ns: string,
    text: string,
    file: string,
    scripts: ReadonlyMap<number, Buffer>,
    port: Port | null,
    tpNew: thugproNew.LevelNew | null = null,
    report: string[] | null = null,
    netRestarts = true,
): void {
    // The node array's NeverScript text compiled, the brought scripts added (scriptPort.appendScripts); `tpNew`:
    // THUGPRO's new scripts too (TPN-THPS3: those whose name the file has already are left out and reported).
    // (network games: path followers, peds and cars kept out - no path manager or ped animations there -, restarts for the players)
    const [netText, marked] = nodes.netGameNodes(text, scripts, netRestarts); // (PC-THPS3-4: netRestarts)
    if (marked) print(`  ${path.win32.basename(file)}: ${marked} node(s) changed for network games`);
    common.nsCompile(ns, netText, file);
    let all: ReadonlyMap<number, Buffer> = scripts;
    if (tpNew !== null && port !== null && tpNew.scripts.size) {
        const [added, clashes] = thugproNew.addedScripts(
            tpNew,
            new Set([...new qb.QbFile(read(file), file, true).scripts.keys(), ...scripts.keys()]),
        );
        all = new Map([...scripts, ...added]);
        if (report !== null) {
            report.push(`  TPN-THPS3 THUGPRO's new scripts: ${added.size} added`);
            if (clashes.length) report.push(`  TPN-THPS3: left out, the name ours uses already: ${clashes.join(", ")}`);
        }
    }
    if (all.size && port !== null) {
        const names: Names = new Map();
        for (const s of [...port.sources, port.level]) {
            if (s === null) throw new TypeError("no level source");
            for (const [k, v] of s.names) names.set(k, v);
        }
        if (tpNew !== null) for (const [k, v] of tpNew.names) if (!names.has(k)) names.set(k, v);
        const data = scriptPort.appendScripts(read(file), all, names);
        fs.writeFileSync(file, data);
    }
}

// --- level structures ----------------------------------------------------------------------------------------------

// A statement's simple value: an int (plain number), a float (Float), a string.
export type SimpleValue = number | Float | string;

export function statements(q: QbFile, script: number): [number, Map<number, SimpleValue>][] {
    // A script's statements as [command name, {key: value}] with the simple values (numbers, strings, names).
    const out: [number, Map<number, SimpleValue>][] = [];
    for (const line of q.scripts.get(script) ?? []) {
        if (!line.length || line[0]![0] !== 0x16) continue;
        const command = nameValue(line[0]![1]);
        const args = new Map<number, SimpleValue>();
        let i = 1;
        while (i < line.length) {
            const [t, payload] = line[i]!;
            if (t === 0x16 && i + 2 < line.length && line[i + 1]![0] === 0x07) {
                const [vt, vp] = line[i + 2]!;
                const k = nameValue(payload);
                if (vt === 0x17 || vt === 0x18) args.set(k, unpackNumbers("<i", vp)[0]!);
                else if (vt === 0x1a) args.set(k, new Float(unpackNumbers("<f", vp)[0]!));
                else if (vt === 0x1b || vt === 0x1c) args.set(k, decodeLatin1(qb.rstripNul(vp)));
                else if (vt === 0x16) args.set(k, nameValue(vp));
                i += 3;
                continue;
            }
            i += 1;
        }
        out.push([command, args]);
    }
    return out;
}

function stripExtension(file: string): string {
    // os.path.splitext(name)[0] of a base name (leading dots aren't an extension)
    const dot = file.lastIndexOf(".");
    if (dot <= 0 || /^\.+$/.test(file.slice(0, dot + 1))) return file;
    return file.slice(0, dot);
}

export function loadScriptInfo(levelsQ: QbFile, base: string): [string | null, number | null] {
    // From THPS3's Load_<base> (levels.qb): the sky (the single-player branch's loadlevelgeometry Sky=) and the
    // lights script it calls (SetUp...Lights).
    let sky: string | null = null;
    let lights: number | null = null;
    const c = qb.checksum;
    for (const [command, args] of statements(levelsQ, c(`Load_${base}`))) {
        const text = (levelsQ.names.get(command) ?? "").toLowerCase();
        if (command === c("loadlevelgeometry")) {
            const value = args.get(c("Sky"));
            if (typeof value === "string" && value)
                sky = stripExtension(path.win32.basename(value.replaceAll("\\", "/")));
        } else if (text.startsWith("setup") && text.endsWith("lights")) {
            lights = command;
        }
    }
    return [sky, lights];
}

export function wrapDegrees(value: number): number {
    return pyMod(value + 180.0, 360.0) - 180.0;
}

// An int or a float of a statement's values (Python's isinstance(x, (int, float))), else undefined.
const numeric = (v: SimpleValue | undefined): number | undefined =>
    typeof v === "number" ? v : v instanceof Float ? v.value : undefined;

export function lighting(lightsQ: QbFile | null, script: number | null): Record<string, number | Float> {
    // THPS3's light set-up (SetDFFAmbientColor, SetDFFDirectColor / Rotation for lights 0 and 1,
    // SetDynamicLightModulationFactor) in THUG2's level structure keys. THPS3's pitch is down from the horizon
    // (90 = straight down); THUG2's the other way (THPS4's levels have -127 ...).
    const out: Record<string, number | Float> = {};
    if (!lightsQ || script === null) return out;
    const c = qb.checksum;
    for (const [command, args] of statements(lightsQ, script)) {
        const index = numeric(args.get(c("lightIndex")));
        const light = index === 0 || index === 1;
        if (command === c("SetDFFDirectColor") && light) {
            for (const [k, word] of [
                ["r", "red"],
                ["g", "green"],
                ["b", "blue"],
            ] as const) {
                const v = args.get(c(k));
                if (typeof v === "number") out[`${word}_${index}`] = v;
            }
        } else if (command === c("SetDFFDirectRotation") && light) {
            const pitch = numeric(args.get(c("pitch")));
            if (pitch !== undefined) out[`pitch_${index}`] = roundHalfEven(wrapDegrees(-pitch));
            const heading = numeric(args.get(c("heading")));
            if (heading !== undefined) out[`heading_${index}`] = roundHalfEven(wrapDegrees(heading));
        } else if (command === c("SetDynamicLightModulationFactor")) {
            const value = numeric(args.get(c("value")));
            if (value === undefined) continue;
            let k: string;
            if (args.has(c("directional"))) {
                const directional = numeric(args.get(c("directional")));
                if (directional !== 0 && directional !== 1) continue;
                k = `mod_factor_${directional}`;
            } else {
                k = "ambient_mod_factor";
            }
            out[`${k}_lo`] = new Float(pyRound(value, 4));
            out[`${k}_hi`] = new Float(pyRound(value, 4));
        }
    }
    // SetDFFAmbientColor's values are in a structure ({r= g= b=}): read from the statement's tokens.
    for (const line of lightsQ.scripts.get(script) ?? []) {
        if (line.length && line[0]![0] === 0x16 && nameValue(line[0]![1]) === c("SetDFFAmbientColor")) {
            const values = new Map<number, number>();
            for (let i = 0; i < line.length - 2; i++) {
                if (line[i]![0] === 0x16 && line[i + 1]![0] === 0x07 && line[i + 2]![0] === 0x17) {
                    values.set(nameValue(line[i]![1]), unpackNumbers("<i", line[i + 2]![1])[0]!);
                }
            }
            for (const [k, outKey] of [
                ["r", "ambient_red"],
                ["g", "ambient_green"],
                ["b", "ambient_blue"],
            ] as const) {
                const v = values.get(c(k));
                if (v !== undefined) out[outKey] = v;
            }
        }
    }
    return out;
}

// --- convert -------------------------------------------------------------------------------------------------------

export interface Args {
    command: "convert" | "selfcheck";
    levels: string[];
    thug2: string;
    thps3: string;
    out: string | null;
    ns: string;
    noObjects: boolean;
    noScripts: boolean;
    noMips: boolean;
    legacyNames: boolean;
    compat: string;
    extra: string[];
    thugpro: string; // TPN-THPS3: THUGPRO's install, read at convert time
    noThugproNew: boolean;
    noPortChanges: boolean; // PC-n: only the required ones
}

function isDir(p: string): boolean {
    return fs.existsSync(p) && fs.statSync(p).isDirectory();
}

// str.title(): each run of letters starts upper case, the rest lower.
function title(text: string): string {
    let out = "";
    let previous = false;
    for (const ch of text) {
        const cased = ch.toLowerCase() !== ch.toUpperCase();
        out += cased ? (previous ? ch.toLowerCase() : ch.toUpperCase()) : ch;
        previous = cased;
    }
    return out;
}

export function cmdConvert(args: Args): void {
    const levelsDir = pyJoin(resolveInstall(args.thps3, "thps3"), "Data", "Levels");
    const out = args.out || pyJoin(args.thug2, STAGING);
    if (!fs.existsSync(args.ns)) pyExit(`${args.ns} not found: build it with tools\\build_neverscript.ps1`);
    const table = new Map(LEVELS.map((row) => [row[0].toLowerCase(), row]));
    const wanted = args.levels.length ? args.levels : LEVELS.filter((row) => row[5]).map((row) => row[0]);
    const scriptsDir = pyJoin(resolveInstall(args.thps3, "thps3"), "Data", "Scripts");
    const levelsQ = new qb.QbFile(read(pyJoin(scriptsDir, "levels.qb")), "levels.qb");
    const lightsQ = new qb.QbFile(read(pyJoin(scriptsDir, "lights.qb")), "lights.qb");
    const globalQbs = scriptNames(args.thps3);
    let port: Port | null = null;
    if (!args.noScripts) {
        port = new Port(
            new scriptPort.Thug2(args.thug2),
            sortedGlobQb(scriptsDir).map((p) => new scriptPort.Source(read(p), path.win32.basename(p).toLowerCase())),
            levelsQ,
            args.thps3,
        );
    }
    const master = masterLevels(levelsQ);
    const thug2Names = thug2LevelNames(args.thug2);
    const teams = flagTeams(globalQbs);
    // the compatibility profile (tools/compat: thugpro - TP-10 / TP-11 - or none)
    const profile = compat.load(args.compat);
    // our own changes to THPS3's data (PC-n: kept under --compat none; --no-port-changes keeps only the required ones)
    const pc = thps3PortChanges(!args.noPortChanges);
    if (isDir(out)) fs.rmSync(out, { recursive: true, force: true });
    fs.mkdirSync(out, { recursive: true });
    const manifest: { levels: Record<string, unknown>[] } = { levels: [] };
    const report: string[] = [];
    const skiesDone = new Set<string>();
    for (const folderName of wanted) {
        const row = table.get(folderName.toLowerCase());
        const folder = findFolder(levelsDir, folderName);
        if (!folder || !row) {
            report.push(`${folderName}: not a THPS3 level folder in ${levelsDir}`);
            continue;
        }
        const [base, sign, location, icon, levelNum] = row;
        const entryStruct = master.get(qb.checksum(`Load_${base}`)) ?? null;
        const name = levelName(base, entryStruct, thug2Names, args.legacyNames);
        // (the generated materials' and objects' checksums keep the old name: the scene and collision stay as they
        // were, and so do the sectors a saved level-editor layout names)
        const tag = legacyName(base);
        const files = levelFiles(folder, base);
        for (const kind of ["bsp", "tdx", "qb"]) {
            if (!files.has(kind)) throw new FormatError(`${folder}: no .${kind} file`);
        }
        const levelDir = pyJoin(out, "levels", name);
        fs.mkdirSync(levelDir, { recursive: true });

        // The node array first: the collision keeps the trigger faces of the kill zones it keeps, and the sectors
        // not there at the start are left out of both.
        const qbFile = files.get("qb")!;
        const q = new qb.QbFile(read(qbFile), qbFile);
        const once = entryStruct ? entryStruct.get("once_on_startup") : undefined;
        const dffFile = files.get("dff");
        const clumps = dffFile !== undefined && !args.noObjects ? thps3.readDff(read(dffFile), dffFile) : [];
        const bspFile = files.get("bsp")!;
        const world = thps3.readBsp(read(bspFile), bspFile);
        if (port !== null) {
            port.drawn = drawnSectors(world);
            port.level = new scriptPort.Source(read(qbFile), "level");
            port.clumps = new Set(clumps.map((c) => c.checksum));
            port.moving = new Set();
            port.vehicles = new Map();
            port.copies = new Map();
            port.solid = new Set();
        }
        const compatData = levelCompat(profile, name);
        // TPN-THPS3: THUGPRO's own content, read from its install (--thugpro; --no-thugpro-new or --compat none: none)
        const newEntries = args.noThugproNew
            ? []
            : thugproNew.levelEntries(compat.thugproNew(profile, "thps3_levels"), name);
        const tpNew = thugproNew.loadNew(newEntries, args.thugpro);
        // (PC-THPS3-15: the plain moving props stay our solid LevelObjects - THUGPRO's Vehicle `set` left out for them)
        const solidMovers = movers.moversFor(pc.get("PC-THPS3-15", base.toLowerCase())?.objects, name);
        if (solidMovers.length && port !== null) {
            const listed = new Set(solidMovers.map((m) => qb.checksum(m.name)));
            const kept = tpNew.sets.filter((s) => !listed.has(s.name));
            if (kept.length !== tpNew.sets.length)
                report.push(`  PC-THPS3-15: ${tpNew.sets.length - kept.length} TPN-THPS3-3 Vehicle sets left out`);
            tpNew.sets = kept;
            // (their collision is THPS3's own shape, made from the drawn model in convertWorld - the user's decision
            // 2026-10-21: THUGPRO's smaller collision of the name (TPN-THPS3-1, the bucket's 134 faces) left out)
            const keptCol = tpNew.collision.filter((o) => !listed.has(o.checksum));
            if (keptCol.length !== tpNew.collision.length)
                report.push(
                    `  PC-THPS3-15: ${tpNew.collision.length - keptCol.length} TPN-THPS3-1 collision objects left out (THPS3's shape instead)`,
                );
            tpNew.collision = keptCol;
        }
        const [nodeText, hidden, triggers, nodeReport, scripts, hasSetup] = convertNodeArray(
            q,
            globalQbs,
            name,
            base,
            once instanceof Name ? [once.value] : [],
            port,
            teams,
            compatData,
            tpNew,
            pc,
        );
        let text = nodeText;
        const qbPath = pyJoin(levelDir, `${name}.qb`);
        const qbReport: string[] = [];
        writeLevelQb(args.ns, text, qbPath, scripts, port, tpNew, qbReport, pc.on("PC-THPS3-4"));
        const tdxFile = files.get("tdx")!;
        let tdx = thps3.readTdx(read(tdxFile), tdxFile);
        let objects: [thps3.Sector[], ObjectMaterial[]] | null = null;
        // (each vehicle its own copy of its model: a LevelObject draws the sector of its node's name)
        const vehicles: thps3.Clump[] = [];
        const modelsAdded = new Set<string>();
        // (TPN-THPS3 `set`: objects THUGPRO draws with its own model - ours's copy of THPS3's left out)
        const dropped = new Set(tpNew.sets.filter((s) => s.dropSector).map((s) => s.name));
        if (port !== null) {
            const byVehicle = [...port.vehicles].sort((a, b) => a[0] - b[0] || compareStr(a[1], b[1]));
            for (const [nodeName, model] of byVehicle) {
                const [clump, modelTdx] = port.model(model)!;
                vehicles.push({ ...clump, checksum: nodeName });
                if (!modelsAdded.has(model.toLowerCase())) {
                    modelsAdded.add(model.toLowerCase());
                    tdx = [...tdx, ...modelTdx];
                }
            }
            const byChecksum = new Map(clumps.map((c) => [c.checksum, c]));
            for (const [nodeName, source] of [...port.copies].sort((a, b) => a[0] - b[0] || a[1] - b[1])) {
                const clump = byChecksum.get(source);
                if (clump === undefined) throw new RangeError(`KeyError: ${source}`);
                vehicles.push({ ...clump, checksum: nodeName });
            }
        }
        if (clumps.length || vehicles.length) {
            const names: Names = new Map(q.names);
            for (const g of globalQbs) for (const [k, v] of g.names) names.set(k, v);
            objects = objectSectors(
                [...clumps, ...vehicles].filter((c) => !dropped.has(c.checksum)),
                q.get("NodeArray"),
                names,
                tag,
                world.materials.length,
                port !== null ? port.moving : new Set(),
            );
            report.push(
                `  game objects: ${objects[0].length} sectors (${port !== null ? port.moving.size : 0} moving; no collision)`,
            );
        }
        convertWorld(world, tdx, name, levelDir, report, {
            keepTriggers: (checksum) => triggers.has(checksum),
            skip: hidden,
            mips: !args.noMips,
            objects,
            tag,
            colEdits: new Map(
                Object.entries(compatData.collision ?? {}).map(([k, v]) => [compatName(k, new Map()).value, v]),
            ),
            thugproNew: tpNew,
            solid: port !== null ? port.solid : new Set(),
        });
        const [withoutTricks, cleared] = withoutMissingTrickObjects(text, pyJoin(levelDir, `${name}.scn.xbx`));
        // (restarts over nothing or over a kill zone: no network spawns, no kill targets - nodes.groundRestarts)
        const [grounded, groundReport] = pc.on("PC-THPS3-7")
            ? nodes.groundRestartsAt(withoutTricks, levelDir, name)
            : [withoutTricks, [] as string[]];
        text = grounded;
        if (cleared || groundReport.length)
            writeLevelQb(args.ns, text, qbPath, scripts, port, tpNew, null, pc.on("PC-THPS3-4"));
        if (cleared) report.push(`  ${cleared} geometry nodes without a scene sector: TrickObject / Cluster dropped`);
        report.push(...groundReport);
        report.push(`  ${nodeReport}`);
        report.push(...qbReport);
        // TPN-THPS3: THUGPRO's models (<staging>\models\..., installed into the overlay's models folder) and what of
        // THUGPRO's wasn't found or didn't match
        for (const [file, data] of tpNew.models) {
            const target = pyJoin(out, "models", file);
            fs.mkdirSync(path.dirname(target), { recursive: true });
            fs.writeFileSync(target, data);
        }
        if (newEntries.length) {
            const counts = Object.entries(tpNew.counts).map(([k, v]) => `${v} ${k}`);
            report.push(
                `  TPN-THPS3 THUGPRO's new content: ${counts.join(", ") || "nothing"} of ${newEntries.length} entries`,
            );
            for (const w of tpNew.warnings) report.push(`  TPN-THPS3 warning: ${w}`);
        }

        // The sky (scene and textures, no collision - as THUG2's own skies).
        const [skyBase, lightsScript] = loadScriptInfo(levelsQ, base);
        let sky: string | null = null;
        if (skyBase) {
            const skyFolder = findFolder(levelsDir, skyBase);
            const skyFiles = skyFolder ? levelFiles(skyFolder, skyBase) : new Map<string, string>();
            const skyBsp = skyFiles.get("bsp");
            const skyTdx = skyFiles.get("tdx");
            if (skyBsp !== undefined && skyTdx !== undefined) {
                // (named as its level: rio_sky with rio, thps3_sk3ed_bch_sky with thps3_ware)
                sky = name === tag ? legacyName(skyBase) : skyBase.toLowerCase();
                if (!skiesDone.has(sky)) {
                    convertWorld(
                        thps3.readBsp(read(skyBsp), skyBsp),
                        thps3.readTdx(read(skyTdx), skyTdx),
                        sky,
                        pyJoin(out, "levels", sky),
                        report,
                        { withCol: false, mips: !args.noMips, tag: legacyName(skyBase) },
                    );
                    skiesDone.add(sky);
                }
            }
        }
        const ownName = entryStruct?.get("level_name");
        const displayName = typeof ownName === "string" ? ownName : title(sign);
        const entry: Record<string, unknown> = {
            folder: base,
            level: name,
            sky,
            display: `THPS3 ${sign}`,
            name: displayName,
            game: "THPS3",
            location,
            icon,
            level_num: levelNum,
            default_stats: 6,
            lighting: lighting(lightsQ, lightsScript),
        };
        if (hasSetup) entry.setup_script = `${name}_setup`;
        manifest.levels.push(entry);
    }
    portedLevels.writeManifest(pyJoin(out, portedLevels.MANIFEST), manifest);
    print(report.join("\n"));
    print(`wrote ${out} (${manifest.levels.length} levels); install with tools\\menu_level\\make_menu_level.ts`);
}

// --- self-check ----------------------------------------------------------------------------------------------------

function sortedGlobAll(dir: string): string[] {
    // sorted(glob(<dir>\*\*)) (names starting with "." skipped, as glob)
    const out: string[] = [];
    if (!isDir(dir)) return out;
    for (const a of fs.readdirSync(dir, { withFileTypes: true })) {
        if (a.name.startsWith(".") || !a.isDirectory()) continue;
        for (const b of fs.readdirSync(path.join(dir, a.name))) {
            if (!b.startsWith(".")) out.push(pyJoin(dir, a.name, b));
        }
    }
    return out.sort(compareStr);
}

export function cmdSelfcheck(args: Args): number {
    const pre = pyJoin(args.thug2, "Data", "pre");
    let [checked, failures] = common.thug2RoundTrips(pre, [
        "LAscn.prx",
        "LAcol.prx",
        "TestLevelscn.prx",
        "TestLevelcol.prx",
        ...args.extra,
    ]);
    const levelsDir = pyJoin(resolveInstall(args.thps3, "thps3"), "Data", "Levels");
    const paths = [
        ...sortedGlobAll(levelsDir),
        ...sortedGlobQb(pyJoin(resolveInstall(args.thps3, "thps3"), "Data", "Scripts")),
    ];
    for (const file of paths) {
        const low = file.toLowerCase();
        let what: string;
        try {
            if (low.endsWith(".bsp")) {
                const w = thps3.readBsp(read(file), file);
                what = `${w.materials.length} materials, ${w.sectors.length} sectors, ${w.triangles} triangles`;
            } else if (low.endsWith(".dff")) {
                what = `${thps3.readDff(read(file), file).length} clumps`;
            } else if (low.endsWith(".tdx")) {
                what = `${thps3.readTdx(read(file), file).length} textures`;
            } else if (low.endsWith(".qb")) {
                const q = new qb.QbFile(read(file), file);
                what = `${q.globals.length} globals, ${q.scripts.size} scripts`;
            } else {
                continue;
            }
        } catch (e) {
            // (Python: FormatError, ValueError, IndexError, struct.error)
            if (!(e instanceof FormatError || e instanceof StructError || e instanceof RangeError)) throw e;
            print(`FAIL ${file}: ${e.message}`);
            failures += 1;
            continue;
        }
        checked += 1;
        print(`ok   ${path.win32.relative(args.thps3, file)}: ${what}`);
    }
    print(`${checked} files checked, ${failures} failures`);
    return failures ? 1 : 0;
}

// --- the command line (argparse's) ---------------------------------------------------------------------------------

const USAGE =
    "usage: thps3_levels.ts [-h] [--thug2 THUG2] [--thps3 THPS3] [--out OUT] [--ns NS] [--no-objects] " +
    "[--no-scripts] [--no-mips] [--legacy-names] [--compat {thugpro,none}] [--thugpro THUGPRO] [--no-thugpro-new] [--no-port-changes] [--extra [EXTRA ...]] {convert,selfcheck} [levels ...]";

const HELP = `${USAGE}

positional arguments:
  {convert,selfcheck}
  levels               THPS3 level folders (default: all but the park editor's)

options:
  -h, --help           show this help message and exit
  --thug2 THUG2
  --thps3 THPS3
  --out OUT            where convert writes (default: <thug2>\\${STAGING})
  --ns NS
  --no-objects         convert: the game objects (.dff) left out
  --no-scripts         convert: the level's scripts, moving objects and waypoints left out (static levels)
  --no-mips            convert: no mip levels made (faster)
  --legacy-names       convert: every level named thps3_<folder> (as before THUGPRO's names)
  --compat {thugpro,none}
                       convert: compatibility profile (tools/compat) - thugpro: CTF flags / team nodes,
                       spawns and hand edits as THUGPRO's (TP-10 .. TP-19; default), none: THPS3's own
  --thugpro THUGPRO    convert: THUGPRO's install, read for its new content (TPN-THPS3; default %LOCALAPPDATA%\\THUG Pro)
  --no-thugpro-new     convert: THUGPRO's new content left out, its hand edits kept
  --no-port-changes    convert: our own changes to THPS3's data left out (PC-n) but those the game needs
  --extra [EXTRA ...]  selfcheck: more THUG2 archives (Data\\pre\\*.prx)`;

const VALUE_OPTIONS = ["--thug2", "--thps3", "--out", "--ns", "--compat", "--thugpro"] as const;
const FLAG_OPTIONS = [
    "--no-objects",
    "--no-scripts",
    "--no-mips",
    "--legacy-names",
    "--no-thugpro-new",
    "--no-port-changes",
] as const;
const ALL_OPTIONS = [...VALUE_OPTIONS, ...FLAG_OPTIONS, "--extra", "--help"];

function usageError(message: string): never {
    process.stderr.write(`${USAGE}\nthps3_levels.ts: error: ${message}\n`);
    pyExit(2);
}

export function parseCommandLine(argv: readonly string[]): Args {
    const values: Record<string, string> = {};
    const flags = new Set<string>();
    const extra: string[] = [];
    const positionals: string[] = [];
    let i = 0;
    const looksLikeOption = (a: string): boolean => a.startsWith("-") && a.length > 1 && !/^-\d/.test(a);
    while (i < argv.length) {
        const arg = argv[i]!;
        i += 1;
        if (arg === "--") {
            positionals.push(...argv.slice(i));
            break;
        }
        if (!looksLikeOption(arg)) {
            positionals.push(arg);
            continue;
        }
        if (arg === "-h") {
            print(HELP);
            pyExit(0);
        }
        const eq = arg.indexOf("=");
        const given = eq < 0 ? arg : arg.slice(0, eq);
        // (argparse takes a unique prefix of a long option)
        const matches = ALL_OPTIONS.filter((o) => o === given || (given.startsWith("--") && o.startsWith(given)));
        const option = matches.includes(given) ? given : matches.length === 1 ? matches[0]! : null;
        if (option === null) {
            if (matches.length > 1) usageError(`ambiguous option: ${given} could match ${matches.join(", ")}`);
            usageError(`unrecognized arguments: ${arg}`);
        }
        if (option === "--help") {
            print(HELP);
            pyExit(0);
        }
        if ((VALUE_OPTIONS as readonly string[]).includes(option)) {
            if (eq >= 0) values[option] = arg.slice(eq + 1);
            else if (i < argv.length && !looksLikeOption(argv[i]!)) values[option] = argv[i++]!;
            else usageError(`argument ${option}: expected one argument`);
        } else if (option === "--extra") {
            if (eq >= 0) extra.push(arg.slice(eq + 1));
            while (eq < 0 && i < argv.length && !looksLikeOption(argv[i]!)) extra.push(argv[i++]!);
        } else {
            if (eq >= 0) usageError(`argument ${option}: ignored explicit argument '${arg.slice(eq + 1)}'`);
            flags.add(option);
        }
    }
    const command = positionals[0];
    if (command === undefined) usageError("the following arguments are required: command");
    if (command !== "convert" && command !== "selfcheck")
        usageError(`argument command: invalid choice: '${command}' (choose from convert, selfcheck)`);
    const profile = values["--compat"];
    if (profile !== undefined && !(compat.PROFILES as readonly string[]).includes(profile))
        usageError(`argument --compat: invalid choice: '${profile}' (choose from ${compat.PROFILES.join(", ")})`);
    return {
        command,
        levels: positionals.slice(1),
        thug2: values["--thug2"] ?? "C:\\Games\\THUG2\\Game",
        thps3: values["--thps3"] ?? "C:\\Games\\THPS3",
        out: values["--out"] ?? null,
        ns: values["--ns"] ?? common.NS,
        noObjects: flags.has("--no-objects"),
        noScripts: flags.has("--no-scripts"),
        noMips: flags.has("--no-mips"),
        legacyNames: flags.has("--legacy-names"),
        compat: values["--compat"] ?? "thugpro",
        extra,
        thugpro: values["--thugpro"] ?? thugproNew.DEFAULT_THUGPRO,
        noThugproNew: flags.has("--no-thugpro-new"),
        noPortChanges: flags.has("--no-port-changes"),
    };
}

export function main(argv: readonly string[] = process.argv.slice(2)): number {
    const args = parseCommandLine(argv);
    // (the install root or the game folder itself: C:\Games\THUG2 -> C:\Games\THUG2\Game - tools/lib/install.ts)
    args.thug2 = resolveInstall(args.thug2, "thug2");
    if (args.command === "selfcheck") return cmdSelfcheck(args);
    cmdConvert(args);
    return 0;
}

if (isMain(import.meta.url)) await runMain(() => main());
