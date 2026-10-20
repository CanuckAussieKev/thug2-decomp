// NOT ORIGINAL - converts Tony Hawk's American Wasteland (PC) levels to THUG2's formats so that THUG2 can load and
// play them (see README.md). Nothing of either game is stored in the repo: the files are made from the user's copies.
//
//   node tools/thaw_levels/thaw_levels.ts selfcheck [--thug2 C:\Games\THUG2] [--thaw C:\Games\THAW]
//   node tools/thaw_levels/thaw_levels.ts convert [levels...] [--thug2 ...] [--thaw ...] [--out <folder>]
//   then: node tools/menu_level/make_menu_level.ts   (installs them into the data overlay, with level select signs)
//
// convert reads each level's "net" archive (<thaw>\Game\data\worlds\worldzones\<zone>\<zone>_net.pak.wpc: the area
// alone, as THAW loads it for classic and online games), the sky (data\skies\TestLevel_Sky), data\pak\qb.pak.wpc's
// levels.qb and data\pak\dbg.pak.wpc's checksum names, and writes <thug2>\thug2-decomp-thaw\ (not read by the game):
// levels\<level>\<level>.scn.xbx / .tex.xbx / .col.xbx / .qb (the node array; <level>: levelName), the sky's scene and
// textures, and levels.json (what make_menu_level.ts adds to Levels.qb and the level select:
// tools/level_port/ported_levels.ts).

import * as fs from "node:fs";
import * as path from "node:path";
import { parseArgs } from "node:util";
import * as compat from "../compat/compat.ts";
import { FormatError } from "../level_port/binio.ts";
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
import {
    formatG,
    isMain,
    print,
    pyExit,
    pyJoin,
    pyMax,
    pyMin,
    pyParseFloat,
    pyRepr,
    pyTitle,
    roundHalfEven,
    runMain,
} from "../lib/py.ts";
import { StructError } from "../lib/struct.ts";
import * as thaw from "./thaw.ts";
import * as thawScripts from "./thaw_scripts.ts";

export const STAGING = "thug2-decomp-thaw";
const SKY = "TestLevel_Sky"; // every THAW level's sky (levels.qb), coloured by the time of day

// THAW's levels: zone (the folder, archives and levels.qb's level=), the level select's sign text, where it is, a
// THUG2 level select sign that suits it, the sign's level number (unique: THPS4 19-31, THUG1 40-58, THPS3 60-76) and
// whether `convert` makes it by default. Classic mode's levels first, then the story's areas of Los Angeles.
export type Level = [zone: string, sign: string, location: string, icon: string, num: number, byDefault: boolean];
export const LEVELS: readonly Level[] = [
    // zone      sign text            location                                  icon              num  default
    ["Z_CH", "CHICAGO", "Chicago, Illinois, USA", "sign_boston", 80, true],
    ["Z_MA", "THE MALL", "a shopping mall (no place named)", "sign_training", 81, true],
    ["Z_DN", "MINNEAPOLIS", "Minneapolis, Minnesota, USA", "sign_boston", 82, true],
    ["Z_JA", "KYOTO", "Kyoto, Japan", "sign_airport", 83, true],
    ["Z_SZ", "SANTA CRUZ", "Santa Cruz, California, USA", "sign_la", 84, true],
    ["Z_SV2", "THE RUINS", "ruins (no place named)", "sign_skatopia", 85, true],
    ["Z_SV", "VANS PARK", "Los Angeles, California, USA", "sign_la", 86, true],
    ["Z_HO", "HOLLYWOOD", "Hollywood, Los Angeles, USA", "sign_la", 87, true],
    ["Z_BH", "BEVERLY HILLS", "Beverly Hills, Los Angeles, USA", "sign_la", 88, true],
    ["Z_DT", "DOWNTOWN", "Downtown Los Angeles, USA", "sign_la", 89, true],
    ["Z_EL", "EAST LOS ANGELES", "East Los Angeles, USA", "sign_la", 90, true],
    ["Z_SM", "SANTA MONICA", "Santa Monica, Los Angeles, USA", "sign_la", 91, true],
    ["Z_SR", "SKATE RANCH", "Los Angeles, California, USA", "sign_la", 92, true],
    ["Z_LV", "CASINO", "Los Angeles, California, USA", "sign_la", 93, true],
    ["Z_OI", "OIL RIG", "off Los Angeles, California, USA", "sign_la", 94, true],
];
const BY_ZONE = new Map(LEVELS.map((level) => [level[0].toLowerCase(), level]));

// THUGPRO compatibility (online play with THUGPRO's own THAW ports - docs/pending/thaw.md "THUGPRO"). Everything
// here is worked out from THAW's own files; THUGPRO's toolchain and installed levels were only the reference for what
// is needed (thugpro_compare.ts compares the result with them) - nothing of theirs is copied.
// - levelName: the level's name in THUG2 - its folder and files, Load_<name> / Level_<name>, <name>_NodeArray: THAW's
//   zone name in lower case (z_ma). A network game's change-level message (0x46) and start info (0x33) carry only the
//   load script's checksum and the other side runs the script of that checksum, so the two sides' names have to be
//   the same. THUGPRO names 11 of the 15 the same way; Chicago, Kyoto, Santa Cruz and Vans Park it renamed (those
//   need a translation - see thaw.md).
// - frameShift: where the area sits. Skater positions travel as world coordinates (x / z in half units, y in
//   quarters, signed 16 bits), so both sides have to put the level at the same place. Each area that has a
//   <zone>_Zone_Origin node is moved by minus its Pos (every axis; nodes, particles, scene and collision alike) - the
//   rule THUGPRO's toolchain applies (approach from Morten1337's thugpro_toolchain: find_zone_origin.py +
//   node_position_patcher.py + THUGPRO_SceneObjectTool -origin).
// What THUGPRO chose that isn't THAW data - four level names, Vans Park left unmoved, East LA's and Santa Monica's own
// offsets - is in the compatibility profile tools/compat/thugpro/thaw_levels.json (its README.md lists every such
// choice, TP-1...; --compat none leaves them out): setCompat() loads it.
export const COMPAT = {
    levelNames: new Map<string, string>(),
    frameKept: new Set<string>(),
    frameAdjust: new Map<string, number[]>(),
    spawns: new Map<string, SpawnEdit[]>(),
};

// TP-4 (tools/compat/thugpro/thaw_levels.json `spawns`): a network spawn - a restart with Multiplayer / Horse / CTF /
// Team in its restart_types - as THUGPRO's THAW port has it where THUGPRO hand-edited it (moved it, changed its
// types, or added it). `node`: our node of that name changed (pos / angles in THAW's own frame - the level's frame
// shift is added as for every node; restart_types: the new list, [] = none); without `node`: a spawn added in our
// form (Restart, CreatedAtStart, `type`, `restart_types`; `dropin`: it runs the level's drop-in script, THUG2's
// MakeSkaterGoto DropIn). Extracted by `node tools/thaw_levels/thugpro_compare.ts --extract-thugpro`.
export interface SpawnEdit {
    node?: string;
    pos?: number[];
    angles?: number[];
    type?: string;
    restart_types?: string[];
    dropin?: boolean;
}

// The compatibility profile's THAW data (tools/compat/compat.ts): "thugpro" (the default) or "none".
export function setCompat(name: string): void {
    const profile = compat.load(name);
    COMPAT.levelNames = new Map(Object.entries(profile.section("thaw_levels", "level_names")) as [string, string][]);
    COMPAT.frameKept = new Set(
        ((profile.section("thaw_levels", "frame_kept") as { zones?: string[] }).zones ?? []) as string[],
    );
    COMPAT.frameAdjust = new Map(
        Object.entries(profile.section("thaw_levels", "frame_adjust")) as [string, number[]][],
    );
    COMPAT.spawns = new Map(Object.entries(profile.section("thaw_levels", "spawns")) as [string, SpawnEdit[]][]);
}

// The level's drop-in script (TP-4's added spawns run it: the skater drops in, as THUG2's MakeSkaterGoto DropIn).
export const dropInName = (level: string): string => `${level}_net_dropin`;

// TP-4: the profile's spawn edits applied to the converted nodes (before the frame shift: positions are THAW's
// frame). Added spawns go at the end, in the profile's order (their order among the spawns is THUGPRO's). Returns
// the NeverScript to add (the drop-in script) or "".
export function applySpawns(
    zone: string,
    level: string,
    converted: Struct[],
    names: Map<number, string>,
    report: string[],
): string {
    const edits = COMPAT.spawns.get(zone.toUpperCase());
    if (edits === undefined || !edits.length) return "";
    const key = (text: string): Name => {
        const value = checksum(text);
        if (!names.has(value)) names.set(value, text);
        return new Name(value);
    };
    const vector = (v: readonly number[]): Vector => new Vector("vector", [...v]);
    const setKey = (node: Struct, k: string, value: QbValue | null): void => {
        const c = checksum(k);
        const at = node.entries.findIndex(([n]) => n !== null && n.value === c);
        if (value === null) {
            if (at >= 0) node.entries.splice(at, 1);
        } else if (at >= 0) node.entries[at] = [node.entries[at]![0], value];
        else node.entries.push([key(k), value]);
    };
    let changed = 0;
    let added = 0;
    let dropIn = false;
    for (const e of edits) {
        let node: Struct;
        if (e.node !== undefined) {
            const wanted = checksum(e.node);
            const found = converted.find((n) => {
                const nm = n.get("Name");
                return nm instanceof Name && nm.value === wanted && nodes.classOf(n, names) === "restart";
            });
            if (found === undefined) throw new FormatError(`${zone}: TP-4 names ${e.node}, not a restart of the level`);
            node = found;
            changed += 1;
        } else {
            added += 1;
            node = new Struct([
                [key("Pos"), vector(e.pos ?? [0, 0, 0])],
                [key("Angles"), vector(e.angles ?? [0, 0, 0])],
                [key("Name"), key(`TRG_Net_Spawn${String(added).padStart(2, "0")}`)],
                [key("Class"), key("Restart")],
                [key("Type"), key(e.type ?? "Multiplayer")],
                [null, key("CreatedAtStart")],
                [key("RestartName"), `Multi: Spawn ${added}`],
            ]);
            converted.push(node);
        }
        if (e.pos !== undefined) setKey(node, "Pos", vector(e.pos));
        if (e.angles !== undefined) setKey(node, "Angles", vector(e.angles));
        if (e.restart_types !== undefined) {
            setKey(node, "restart_types", e.restart_types.length ? e.restart_types.map(key) : null);
        }
        if (e.dropin) {
            setKey(node, "TriggerScript", key(dropInName(level)));
            dropIn = true;
        }
    }
    report.push(`  TP-4: ${changed} spawn(s) as THUGPRO's, ${added} added`);
    return dropIn ? `script ${dropInName(level)} {\n    MakeSkaterGoto DropIn \n}\n` : "";
}

setCompat("thugpro"); // (the default, also for the tools that import this one: migrate_layouts, thugpro_compare)

// The level's THUG2 name: THAW's zone name (z_ma) or the profile's (level_names), or (legacy) the thaw_<xx> this
// converter used before.
export function levelName(zone: string, legacy = false): string {
    if (legacy) return `thaw_${zone.toLowerCase().slice(2)}`;
    return COMPAT.levelNames.get(zone.toUpperCase()) ?? zone.toLowerCase();
}

// A shift in whole sixteenths: collision BSP split points are world coordinates in sixteenths, moved exactly so
// (a face lying on a split stays on its side). Under 1/32 of a unit off the exact origin - far below what the
// network carries (half units).
export function sixteenths(v: readonly number[]): number[] {
    return v.map((x) => roundHalfEven(x * 16.0) / 16.0);
}

// The zone's frame: minus the Pos of its <zone>_Zone_Origin node plus the profile's frame_adjust (in whole
// sixteenths), or no move when it has none or is in the profile's frame_kept.
export function frameShift(zone: string, array: readonly QbValue[], _names: ReadonlyMap<number, string>): number[] {
    if (COMPAT.frameKept.has(zone.toUpperCase())) return [0.0, 0.0, 0.0];
    const origin = checksum(`${zone}_Zone_Origin`);
    const adjust = COMPAT.frameAdjust.get(zone.toUpperCase()) ?? [0.0, 0.0, 0.0];
    for (const node of array) {
        const name = node instanceof Struct ? node.get("Name") : undefined;
        if (node instanceof Struct && name instanceof Name && name.value === origin) {
            const pos = node.get("Pos");
            if (pos instanceof Vector && pos.kind === "vector") {
                // (zip: as many as both have)
                const n = Math.min(pos.values.length, adjust.length);
                return sixteenths(pos.values.slice(0, n).map((x, k) => -x + adjust[k]!));
            }
        }
    }
    return [0.0, 0.0, 0.0];
}

// Collision face flags: THUG2's meanings up to 0x1000 (invisible); 0x2000, 0x4000 and 0x8000 aren't THUG2's - dropped.
const FACE_KEEP = 0x1fff;
// Material pass flags THUG2 has the data for here (uv wibble only with its values, below): textured, environment,
// decal, smooth, transparent, ignore vertex alpha. (0x80, texture animation 0x800, 0x80000... dropped.)
const PASS_KEEP =
    thug2Scene.PASS_TEXTURED |
    thug2Scene.PASS_ENVIRONMENT |
    thug2Scene.PASS_DECAL |
    thug2Scene.PASS_SMOOTH |
    thug2Scene.PASS_TRANSPARENT |
    thug2Scene.PASS_IGNORE_VERTEX_ALPHA;
const BLEND_MODES = 14; // THUG2's blend modes 0..13; THAW's others (rare) become 5 (blend)
// Blend modes that mix by the previous pass's alpha: THUG2's pixel shader (0x004DD2C0) reads it from r1 after a pass
// that wrote r1.a - after another of these (which writes only r1.rgb) the ps.1.1 doesn't assemble and the game
// crashes loading (Santa Monica: one material 0, 13, 13). Such a pass is left out.
const BLEND_BY_PREVIOUS = [12, 13];
const DEFAULT_MIP = [1, 4, -8.0, -8.0]; // as nearly every material of THUG2's own levels

// --- files ------------------------------------------------------------------------------------------------------

const isDir = (p: string): boolean => {
    try {
        return fs.statSync(p).isDirectory();
    } catch {
        return false;
    }
};

// A path under `folder`, each part matched without regard to case; null when it isn't there.
export function findPath(folder: string, ...parts: string[]): string | null {
    let p = folder;
    for (const part of parts) {
        if (!isDir(p)) return null;
        const match = fs.readdirSync(p).find((e) => e.toLowerCase() === part.toLowerCase());
        if (match === undefined) return null;
        p = pyJoin(p, match);
    }
    return p;
}

export function zonePak(thawFolder: string, zone: string): string | null {
    return findPath(thawFolder, "Game", "data", "worlds", "worldzones", zone, `${zone}_net.pak.wpc`);
}

// {".geom" / ".tex" / ".col": data, "<file name>.qb": data} of a zone's archive, without its _sfx sub-zone.
export function zoneFiles(file: string): Map<string, Buffer> {
    const files = new Map<string, Buffer>();
    for (const [entry, data] of thaw.pakFiles(file)) {
        if (entry.flags & thaw.PAK_SFX) continue;
        if (entry.path) files.set(path.win32.basename(entry.path).toLowerCase().replaceAll(".wpc", ""), data);
        else if ([".geom", ".tex", ".col"].includes(entry.extension)) files.set(entry.extension, data);
    }
    return files;
}

// files[key], raising as Python's dict does
export function need(files: ReadonlyMap<string, Buffer>, key: string): Buffer {
    const data = files.get(key);
    if (data === undefined) throw new RangeError(`KeyError: '${key}'`);
    return data;
}

// The zone's node array as THAW has it (its includes expanded).
export function zoneArray(files: ReadonlyMap<string, Buffer>, zone: string, names: Map<number, string>): QbValue[] {
    const low = zone.toLowerCase();
    const data = files.get(`${low}.qb`);
    if (data === undefined) throw new FormatError(`${zone}: no ${low}.qb in its archive`);
    const q = new thaw.QbItems(data, `${low}.qb`, names);
    const array = q.get(`${zone}_NodeArray`);
    if (!Array.isArray(array)) throw new FormatError(`${zone}: no ${zone}_NodeArray`);
    return nodes.expandIncludes(array, new Map(q.globals.map(([key, value]) => [key.value, value])));
}

function levelStructs(thawFolder: string, names: Map<number, string>): Map<string, Struct> {
    const found = thaw
        .pakFiles(findPath(thawFolder, "Game", "data", "pak", "qb.pak.wpc")!)
        .find(([e]) => e.path?.toLowerCase().endsWith("\\levels.qb.wpc"));
    if (found === undefined) throw new RangeError("StopIteration: no levels.qb");
    const q = new thaw.QbItems(found[1], "levels.qb", names);
    const out = new Map<string, Struct>();
    for (const [, value] of q.globals) {
        const level = value instanceof Struct ? value.get("level") : undefined;
        if (value instanceof Struct && typeof level === "string" && !out.has(level.toLowerCase())) {
            out.set(level.toLowerCase(), value);
        }
    }
    return out;
}

// --- scene ------------------------------------------------------------------------------------------------------

export function toThug2Material(m: thaw.GeomMaterial): thug2Scene.Material {
    const passes: thug2Scene.Pass[] = [];
    for (const p of m.passes) {
        let flags = p.flags & PASS_KEEP;
        if (p.flags & thug2Scene.PASS_UV_WIBBLE && p.uvWibble !== undefined) flags |= thug2Scene.PASS_UV_WIBBLE;
        const mode = p.blend & 0xff;
        const [r, g, b] = p.color as [number, number, number, number];
        const out: thug2Scene.Pass = {
            texture: p.texture,
            flags,
            hasColor: [r, g, b].some((c) => Math.abs(c - 0.5) > 1e-6) ? 1 : 0,
            color: [r, g, b],
            blendMode: mode < BLEND_MODES ? mode : 5,
            fixedAlpha: p.blend >>> 24,
            uAddress: p.address[0]!,
            vAddress: p.address[1]!,
            envmapTiling: p.envTiling,
            filtering: p.filtering,
            mip: DEFAULT_MIP,
        };
        if (flags & thug2Scene.PASS_UV_WIBBLE) out.uvWibble = p.uvWibble!.slice(0, 8);
        if (
            passes.length >= 2 &&
            BLEND_BY_PREVIOUS.includes(out.blendMode) &&
            BLEND_BY_PREVIOUS.includes(passes[passes.length - 1]!.blendMode)
        ) {
            continue;
        }
        passes.push(out);
    }
    return {
        checksum: m.checksum,
        nameChecksum: m.nameChecksum,
        passes,
        alphaCutoff: m.alphaCutoff,
        sorted: 0,
        drawOrder: m.drawOrder,
        singleSided: 0,
        noBackfaceCulling: m.doubleSided ? 1 : 0,
        zBias: m.zBias,
        grassify: 0,
        specularPower: 0.0,
    };
}

const FVF_TEXCOUNT = 0xf00;

// THAW's FVF vertices as THUG2's (position, normal when there are some, colour, at least one uv set), moved by
// `shift`. Returns [data, stride, fvf, hasNormals].
export function meshVertices(m: thaw.GeomMesh, shift: readonly number[]): [Buffer, number, number, boolean] {
    const fvf = m.fvf;
    const hasNormals = Boolean(fvf & thug2Scene.FVF_NORMAL);
    let sets = (fvf & FVF_TEXCOUNT) >> thug2Scene.FVF_TEXCOUNT_SHIFT;
    const hasDiffuse = Boolean(fvf & thug2Scene.FVF_DIFFUSE);
    const expected = 12 + (hasNormals ? 12 : 0) + (hasDiffuse ? 4 : 0) + 8 * sets;
    if (
        fvf & ~(thug2Scene.FVF_XYZ | thug2Scene.FVF_NORMAL | thug2Scene.FVF_DIFFUSE | FVF_TEXCOUNT) ||
        expected !== m.stride
    ) {
        throw new FormatError(`mesh FVF ${fvf.toString(16).toUpperCase()} with stride ${m.stride}`);
    }
    const src = m.vertices;
    const stride = m.stride;
    const newStride = 12 + (hasNormals ? 12 : 0) + 4 + 8 * Math.max(sets, 1);
    const out = Buffer.alloc(m.vertexCount * newStride);
    let o = 0;
    for (let i = 0; i < m.vertexCount; i++) {
        const v = i * stride;
        for (let k = 0; k < 3; k++) out.writeFloatLE(src.readFloatLE(v + 4 * k) + shift[k]!, o + 4 * k);
        o += 12;
        let at = v + 12;
        if (hasNormals) {
            src.copy(out, o, at, at + 12);
            o += 12;
            at += 12;
        }
        if (hasDiffuse) {
            src.copy(out, o, at, at + 4);
            o += 4;
            at += 4;
        } else {
            out.fill(0x80, o, o + 4);
            o += 4;
        }
        if (sets) {
            src.copy(out, o, at, v + stride);
            o += v + stride - at;
        } else {
            o += 8; // (zeros)
        }
    }
    sets = Math.max(sets, 1);
    let newFvf = thug2Scene.FVF_XYZ | thug2Scene.FVF_DIFFUSE | (sets << thug2Scene.FVF_TEXCOUNT_SHIFT);
    if (hasNormals) newFvf |= thug2Scene.FVF_NORMAL;
    return [out, newStride, newFvf, hasNormals];
}

// THAW's scene as THUG2's: the materials mapped (toThug2Material), a THUG2 sector per THAW sector with its meshes'
// vertices and strips as they are. `drop`: sector checksums left out (the far-away LOD copies). `local`: sectors in
// their own space (the LevelObjects'), not moved by `shift`.
export function convertScene(
    geom: thaw.Geom,
    warnings: string[],
    shift: readonly number[] = [0.0, 0.0, 0.0],
    drop: ReadonlySet<number> = new Set(),
    local: ReadonlySet<number> = new Set(),
): thug2Scene.Scene {
    const materials = geom.materials.map(toThug2Material);
    const byChecksum = new Map(materials.map((m) => [m.checksum, m]));
    const sectors: thug2Scene.Sector[] = [];
    const hex8 = (n: number): string => n.toString(16).toUpperCase().padStart(8, "0");
    for (const s of geom.sectors) {
        if (drop.has(s.checksum)) continue;
        if (s.flags & thug2Scene.SEC_BILLBOARD) {
            // (every vertex at the pivot: THAW's shader spreads them out to face the camera)
            warnings.push(`sector ${hex8(s.checksum)}: billboard - left out`);
            continue;
        }
        const out: thug2Scene.Sector = {
            checksum: s.checksum,
            bone: -1,
            flags: thug2Scene.SEC_TEXCOORDS | thug2Scene.SEC_COLORS,
            bbox: [],
            sphere: [],
            meshes: [],
        };
        const points: number[][] = [];
        for (const m of s.meshes) {
            if (m.strip.length < 3 || !m.vertexCount) continue;
            const material = byChecksum.get(m.material);
            if (material === undefined) {
                warnings.push(`sector ${hex8(s.checksum)}: material ${hex8(m.material)} missing - mesh left out`);
                continue;
            }
            const [data, stride, fvf, hasNormals] = meshVertices(m, local.has(s.checksum) ? [0.0, 0.0, 0.0] : shift);
            if (hasNormals) out.flags |= thug2Scene.SEC_NORMALS;
            const positions: number[][] = [];
            for (let i = 0; i < m.vertexCount; i++) {
                const at = i * stride;
                positions.push([data.readFloatLE(at), data.readFloatLE(at + 4), data.readFloatLE(at + 8)]);
            }
            points.push(...positions);
            const [lo, hi, center, radius] = geometry.bounds(positions);
            const mesh: thug2Scene.Mesh = {
                sphere: [center[0]!, center[1]!, center[2]!, radius],
                bbox: [...lo, ...hi],
                flags: geometry.MESH_THUG2,
                material: m.material,
                lods: [m.strip],
                strips: [m.strip],
                stride,
                vertexCount: m.vertexCount,
                buffers: [[0, data]],
                vertexShader: [fvf, 0],
                offsets: hasNormals ? [12, 24, 28] : [0, 12, 16],
                hasWibble: 0,
                indexSets: 1,
                pixelShader: geometry.pixelShader(material),
            };
            if (mesh.pixelShader === geometry.PS_FROM_MATERIAL) {
                mesh.psValue = 0;
                mesh.psData = Buffer.alloc(0);
            }
            out.meshes.push(mesh);
        }
        if (!out.meshes.length) continue;
        if (out.meshes.some((mesh) => !mesh.offsets[0])) out.flags &= ~thug2Scene.SEC_NORMALS;
        const [lo, hi, center, radius] = geometry.bounds(points);
        out.bbox = [...lo, ...hi];
        out.sphere = [center[0]!, center[1]!, center[2]!, radius];
        sectors.push(out);
    }
    return { versions: [1, 1, 1], materials, sectors, hierarchyCount: 0, tail: Buffer.alloc(0) };
}

const NIGHT_NAMES = ["nighton", "_tod_light"];

// The sectors THAW shows only at night (its time of day scripts: NightOn_* lamps and glows, *_tod_light_*
// glows): left out, the levels being lit as by day.
export function nightSectors(geom: thaw.Geom, names: ReadonlyMap<number, string>): Set<number> {
    const out = new Set<number>();
    for (const s of geom.sectors) {
        const text = (names.get(s.checksum) ?? "").toLowerCase();
        if (NIGHT_NAMES.some((word) => text.includes(word))) out.add(s.checksum);
    }
    return out;
}

// --- collision --------------------------------------------------------------------------------------------------

// Per zone: pieces left out altogether (scene, collision, node) - story blockers THAW's goal scripts take away, which
// free skate here doesn't run. The Mall: an invisible wall across the glass doors by the start (the way in shut).
const LEFT_OUT = new Map([["Z_MA", ["Z_MA_Mall_First_Rail_Blocker"]]]);

export function leftOut(zone: string): Set<number> {
    return new Set((LEFT_OUT.get(zone.toUpperCase()) ?? []).map(checksum));
}

// A breakable's faces, as THUG2's own (its Airport's AP_Shatter_Roof pieces): non-collidable, trigger, no skater
// shadow, invisible - THAW's had decal and camera collidable too (0xF0).
const BREAKABLE_FACE = 0x1150;

// A BSP tree moved by `shift`: its split points are world coordinates (in sixteenths), not relative to the box.
// Left where they were, a moved object's queries all went down one side of each split and never found the faces
// on the other: The Mall's floors at both ends of its escalator were missing (the walker stopped there, as at a
// drop with no bottom) - 198 of its 1074 objects have splits.
export function moveTree(node: thug2Col.BspNode, shift: readonly number[]): thug2Col.BspNode {
    if (node.kind !== "split") return node;
    return {
        kind: "split",
        axis: node.axis,
        point: node.point + roundHalfEven(shift[node.axis]! * 16.0),
        left: moveTree(node.left, shift),
        right: moveTree(node.right, shift),
    };
}

// THAW's collision is THUG2's (with the reaches and BSP trees): the face flags THUG2 doesn't have dropped, trigger
// faces only for kill zones (THUG2 runs a trigger face's node's TriggerScript without checking it exists), moved
// by `shift` (whole sixteenths: the fixed-point vertices are relative to the box, which moves with them; the BSP
// trees' split points are world coordinates and move too).
export function convertCol(
    col: thug2Col.ColFile,
    keepTriggers: (checksum: number) => boolean,
    shift: readonly number[],
    drop: ReadonlySet<number> = new Set(),
    breakables: ReadonlySet<number> = new Set(),
    local: ReadonlySet<number> = new Set(),
): thug2Col.ColFile {
    const objects: thug2Col.ColObject[] = [];
    const moved = shift.some((x) => x !== 0);
    for (const original of col.objects) {
        if (drop.has(original.checksum)) continue;
        const o = { ...original };
        const keep = keepTriggers(o.checksum);
        o.faces = o.faces.map(([flags, terrain, a, b, c]) => [
            keep ? flags & FACE_KEEP : flags & FACE_KEEP & ~thug2Col.FACE_TRIGGER,
            terrain,
            a,
            b,
            c,
        ]);
        if (breakables.has(o.checksum)) {
            o.faces = o.faces.map(([, terrain, a, b, c]) => [BREAKABLE_FACE, terrain, a, b, c]);
        }
        if (moved && !local.has(o.checksum)) {
            // (not the LevelObjects': in their own space)
            const b = o.bbox;
            o.bbox = [
                b[0]! + shift[0]!,
                b[1]! + shift[1]!,
                b[2]! + shift[2]!,
                b[3]!,
                b[4]! + shift[0]!,
                b[5]! + shift[1]!,
                b[6]! + shift[2]!,
                b[7]!,
            ];
            o.verts = o.verts.map((v) => [v[0]! + shift[0]!, v[1]! + shift[1]!, v[2]! + shift[2]!]);
            o.bsp = moveTree(o.bsp, shift);
        }
        objects.push(o);
    }
    const out: thug2Col.ColFile = { objects };
    if (col.pad !== undefined) out.pad = col.pad;
    return out;
}

// --- nodes ------------------------------------------------------------------------------------------------------

// Every Pos / Position vector of the nodes moved by `shift`.
export function movePositions(value: readonly QbValue[], shift: readonly number[]): void {
    const keys = [checksum("Pos"), checksum("Position")];
    for (const node of value) {
        if (!(node instanceof Struct)) continue;
        node.entries.forEach(([key, v], i) => {
            if (key !== null && keys.includes(key.value) && v instanceof Vector && v.kind === "vector") {
                node.entries[i] = [
                    key,
                    new Vector(
                        "vector",
                        [0, 1, 2].map((k) => v.values[k]! + shift[k]!),
                    ),
                ];
            }
        });
    }
}

export const MULTIPLAYER_NAME = /^trg_(ctf_\w+|flag_(red|blue|green|yellow)(_base)?|koth\d+|team_restart)$/i;

// The game modes' nodes under the names THUG2's own scripts look for (net\ctf.qb: TRG_CTF_Red...; SK4_Scripts'
// team flags: TRG_Flag_Red...) and its levels use (TRG_KOTH01..., TRG_Team_Restart): THAW prefixes them with the
// zone (Z_JA_TRG_CTF_Blue). THUGPRO's toolchain strips the prefix from the same names (thaw_node_array_converter.py
// fix_node_name - approach from Morten1337's thugpro_toolchain). In place; returns how many.
export function multiplayerNames(array: readonly QbValue[], zone: string, names: Map<number, string>): number {
    const prefix = `${zone.toLowerCase()}_`;
    let renamed = 0;
    for (const node of array) {
        const name = node instanceof Struct ? node.get("Name") : undefined;
        if (!(node instanceof Struct) || !(name instanceof Name)) continue;
        const text = names.get(name.value) ?? "";
        if (text.toLowerCase().startsWith(prefix) && MULTIPLAYER_NAME.test(text.slice(prefix.length))) {
            const short = text.slice(prefix.length);
            if (!names.has(checksum(short))) names.set(checksum(short), short);
            node.set("Name", new Name(checksum(short)));
            renamed += 1;
        }
    }
    return renamed;
}

const SHATTERS = ["ShatterAndDie", "Shatter", "ShatterFromSkater"].map(checksum);

// Breakable pieces (The Mall's glass doors and panes...): a node whose TriggerScript - or a script it calls -
// shatters the node (THAW: MA_BreakGlass name=<the node> = Kill + ShatterAndDie, a THAW command). As THUG2's own
// breakables (its Airport's AP_Shatter): Shatter name=<the node> ..., then Kill. Returns {trigger script: the
// commands}, kept with the nodes' trigger faces as the kill zones are (without them the glass was a wall: the
// Mall's way in from the start was shut).
export function breakableScripts(
    array: readonly QbValue[],
    scriptQbs: readonly thaw.QbItems[],
    names: Map<number, string>,
    drawn: ReadonlySet<number> | null = null,
): Map<number, string> {
    const scripts = new Map<number, qb.Statement[]>();
    for (const q of scriptQbs) for (const [k, v] of q.scripts) scripts.set(k, v);

    const shatters = (script: number, depth = 0): boolean => {
        for (const line of scripts.get(script) ?? []) {
            for (const [t, p] of line) {
                if (t === 0x16) {
                    const value = scriptPort.u32(p);
                    if (SHATTERS.includes(value)) return true;
                    if (depth < 2 && scripts.has(value) && value !== script && shatters(value, depth + 1)) return true;
                }
            }
        }
        return false;
    };

    const out = new Map<number, string>();
    for (const node of array) {
        if (!(node instanceof Struct) || nodes.classOf(node, names) !== "levelgeometry") continue;
        const trigger = node.get("TriggerScript");
        const name = node.get("Name");
        if (drawn !== null && name instanceof Name && !drawn.has(name.value)) {
            // (collision-only: Shatter / Kill look the scene sector up and read through the null)
            continue;
        }
        if (trigger instanceof Name && name instanceof Name && shatters(trigger.value)) {
            const text = qb.nameText(name.value, names);
            out.set(
                trigger.value,
                "PlaySound HitGlassPane2x vol=100\n" +
                    `    Shatter name=${text} area=4000 variance=4 vel_x=50 vel_y=50 vel_z=50 spread=1\n` +
                    `    Kill name=${text}`,
            );
        }
    }
    return out;
}

// What bringing a level's scripts, objects, peds and cars along needs (thaw_scripts.ts): THUG2's commands
// (script_port.Thug2) and assets (peds_cars.Thug2Assets), THAW's game script files brought along.
export class Port {
    readonly thug2: scriptPort.Thug2;
    readonly assets: pedsCars.Thug2Assets;
    readonly globals: thawScripts.ThawSource[];
    readonly particles: Map<string, string>;

    constructor(args: Args, names: Map<number, string>) {
        this.thug2 = new scriptPort.Thug2(args.thug2);
        this.assets = new pedsCars.Thug2Assets(args.thug2);
        this.globals = thawScripts.globalSources(findPath(args.thaw, "Game", "data", "pak", "qb.pak.wpc")!, names);
        this.particles = thawScripts.particleTextures(args.thug2);
    }
}

// The node array's NeverScript text compiled, the brought scripts added (script_port.appendScripts).
// (network games: path followers, peds and cars kept out - no path manager or ped animations there -, restarts for
// the players)
function writeLevelQb(
    ns: string,
    text: string,
    file: string,
    scripts: ReadonlyMap<number, Buffer>,
    names: Map<number, string>,
): void {
    const [netText, marked] = nodes.netGameNodes(text, scripts);
    if (marked) print(`  ${path.win32.basename(file)}: ${marked} node(s) changed for network games`);
    common.nsCompile(ns, netText, file);
    if (scripts.size) fs.writeFileSync(file, scriptPort.appendScripts(fs.readFileSync(file), scripts, names));
}

const levelGeometryNames = (list: readonly Struct[], names: Map<number, string>, test: (n: Struct) => boolean) => {
    const out = new Set<number>();
    for (const n of list) {
        const name = n.get("Name");
        if (name instanceof Name && nodes.classOf(n, names) === "levelgeometry" && test(n)) out.add(name.value);
    }
    return out;
};

// What convertLevelNodes returns: the checksums of the nodes whose collision keeps its trigger faces, the LOD sectors
// to leave out, the breakables, the node array text, the brought scripts, the LevelObjects in their own space, the
// moving objects left out.
export interface LevelNodes {
    triggers: Set<number>;
    lods: Set<number>;
    breakables: Set<number>;
    text: string;
    scripts: Map<number, Buffer>;
    local: Set<number>;
    objectsGone: Set<number>;
}

// The node array: kept nodes and kill zones (THUG2's SK3_KillSkater, each naming a restart) as <name>.qb; with a
// Port, the level's scripts, moving objects, peds and cars too (thaw_scripts.ts; sectors / colObjects: the checksums
// the scene and collision have).
export function convertLevelNodes(
    files: ReadonlyMap<string, Buffer>,
    zone: string,
    name: string,
    ns: string,
    outDir: string,
    report: string[],
    names: Map<number, string>,
    shift: readonly number[],
    drop: ReadonlySet<number> = new Set(),
    port: Port | null = null,
    sectors: ReadonlySet<number> = new Set(),
    colObjects: ReadonlySet<number> = new Set(),
): LevelNodes {
    const low = zone.toLowerCase();
    const data = files.get(`${low}.qb`);
    if (data === undefined) throw new FormatError(`${zone}: no ${low}.qb in its archive`);
    const q = new thaw.QbItems(data, `${low}.qb`, names);
    const scriptQbs = [q];
    const scriptsQb = files.get(`${low}_scripts.qb`);
    if (scriptsQb !== undefined) scriptQbs.push(new thaw.QbItems(scriptsQb, `${low}_scripts.qb`, names));
    const raw = q.get(`${zone}_NodeArray`);
    if (!Array.isArray(raw)) throw new FormatError(`${zone}: no ${zone}_NodeArray`);
    let array = nodes.expandIncludes(raw, new Map(q.globals.map(([key, value]) => [key.value, value])));
    const stripped = multiplayerNames(array, zone, names);
    if (stripped) {
        report.push(
            `  ${stripped} game-mode nodes renamed without the zone prefix (TRG_CTF_*, TRG_Flag_*, TRG_KOTH*, ` +
                "TRG_Team_Restart)",
        );
    }
    const nameOf = (n: QbValue): number | null => {
        const v = n instanceof Struct ? n.get("Name") : undefined;
        return v instanceof Name ? v.value : null;
    };
    // LOD copies: a node's LOD_Name is the sector drawn instead of it far away; THUG2 would draw both.
    const lods = new Set<number>();
    for (const node of array) {
        const lod = node instanceof Struct ? node.get("LOD_Name") : undefined;
        if (lod instanceof Name) lods.add(lod.value);
    }
    const restarts = new Set<number>();
    for (const n of array) {
        const v = nameOf(n);
        if (v !== null && nodes.classOf(n as Struct, names) === "restart") restarts.add(v);
    }
    let kills = nodes.killCommands(scriptQbs, restarts, names);
    const drawn = sectors.size ? new Set([...sectors].filter((s) => !lods.has(s) && !drop.has(s))) : null;
    const breakables = breakableScripts(array, scriptQbs, names, drawn);
    // The nodes left out (LOD copies, `drop`) become placeholders of a class convertNodes drops, before it renumbers
    // the Links - removed after, every node after one pointed its Links one place off (rails linked to the wrong
    // nodes: The Mall hung loading once its story blocker was left out).
    const gone = new Set([...lods, ...drop]);
    const placeholder = new Struct([[new Name(checksum("Class")), new Name(checksum("LeftOut"))]]);
    const original = array;
    array = array.map((n) => {
        const v = nameOf(n);
        return v !== null && gone.has(v) ? placeholder : n;
    });
    let made: thawScripts.PortedNodes | null = null;
    const wanted = new Set<number>();
    const madeGone = new Set<number>();
    if (port !== null) {
        // (in place: the Links stay indices into the array)
        made = new thawScripts.PortedNodes(
            array,
            names,
            new Set([...sectors].filter((s) => !gone.has(s))),
            colObjects,
            scriptQbs,
            port.thug2,
            port.assets,
            port.particles,
            shift,
        );
        const m = made;
        array = array.map((n) => (n instanceof Struct ? (m.convert(n) ?? n) : n));
        for (const node of array) {
            const script = node instanceof Struct ? node.get("TriggerScript") : undefined;
            if (
                script instanceof Name &&
                ["levelgeometry", "railnode"].includes(nodes.classOf(node as Struct, names))
            ) {
                wanted.add(script.value);
            }
        }
    } else {
        // (static: THAW's LevelObjects are in their own space - left out rather than drawn at the origin)
        for (const n of array) {
            const v = nameOf(n);
            if (n instanceof Struct && nodes.classOf(n, names) === "levelobject" && v !== null) madeGone.add(v);
        }
        array = array.map((n) => {
            const v = nameOf(n);
            return v !== null && madeGone.has(v) ? placeholder : n;
        });
    }
    const [converted, stats] = nodes.convertNodes(
        array,
        names,
        new Map([...kills, ...breakables]),
        ["pos"],
        made ? made.ids : [],
        wanted,
    );
    const spawnScripts = applySpawns(zone, name, converted, names, report);
    if (shift.some((x) => x !== 0)) movePositions(converted, shift);
    kills = nodes.addKillRestarts(converted, kills, names);
    let scripts = new Map<number, Buffer>();
    let extra = "";
    let scriptReport = "";
    let local = new Set<number>();
    let objectsGone: Set<number>;
    if (made !== null && port !== null) {
        const sources = [
            ...scriptQbs.map((x) => new thawScripts.ThawSource(x, thawScripts.LEVEL, names)),
            ...port.globals,
        ];
        const provided = new Set([
            ...kills.keys(),
            ...breakables.keys(),
            ...[
                "LoadTerrain",
                thawScripts.PED_WALK,
                thawScripts.PED_STAND,
                thawScripts.CAR_DRIVE,
                thawScripts.RAIL_ACCELERATE,
            ].map(checksum),
        ]);
        const m = made;
        let globalsText: string;
        [scripts, globalsText, scriptReport] = thawScripts.portScripts(
            converted,
            original,
            made,
            [...port.globals, ...sources],
            port.thug2,
            names,
            provided,
            levelGeometryNames(converted, names, (n) => !m.sectors.has((n.get("Name") as Name).value)),
        );
        extra =
            globalsText +
            thawScripts.MADE_SCRIPTS +
            thawScripts.RAIL_SCRIPTS +
            thawScripts.loadParticleTexturesNs(made.particleTextures);
        local = made.local;
        objectsGone = made.gone;
    } else {
        objectsGone = madeGone;
    }
    for (const [k, v] of breakables) kills.set(k, v);
    const breakingNames = nodes.killTriggerNames(converted, breakables);
    // (a breakable keeps no Type: THAW's Type=Crate - its physics breakables - made the glass a solid object here)
    const typeKey = checksum("Type");
    for (const node of converted) {
        const v = node.get("Name");
        if (v instanceof Name && breakingNames.has(v.value)) {
            node.entries = node.entries.filter(([k]) => k === null || k.value !== typeKey);
        }
    }
    const triggers = nodes.killTriggerNames(converted, kills);
    const killZones = triggers.size - breakingNames.size;
    if (made !== null) {
        // (the geometry whose trigger script runs keeps its trigger faces: gaps, sounds...)
        for (const v of levelGeometryNames(converted, names, (n) => n.get("TriggerScript") instanceof Name)) {
            triggers.add(v);
        }
    }
    let text = nodes.nodeArrayNs(`${name}_NodeArray`, converted, names, kills) + extra + spawnScripts;
    if (breakingNames.size) {
        // The breakables' glass sound (THAW's MA_BreakGlass plays HitGlassPane2x; THUG2 has it too, loaded per level):
        // loaded by the level's LoadTerrain, which load_level runs while loading (the level-specific sounds script
        // goes by the career level's prefix, shared by every ported level).
        const start = "script LoadTerrain {\n";
        if (text.split(start).length - 1 !== 1) throw new FormatError(`${name}: LoadTerrain not found once`);
        text = text.replace(
            start,
            () => `${start}    LoadSound "Shared\\\\Hits\\\\HitGlassPane2x" vol=100 dropoff=0 \n`,
        );
    }
    fs.mkdirSync(outDir, { recursive: true });
    writeLevelQb(ns, text, pyJoin(outDir, `${name}.qb`), scripts, names);
    report.push(
        `  nodes: ${converted.length} of ${array.length} kept ${pyRepr(stats)}; ${killZones} kill zones, ` +
            `${breakingNames.size} breakables; ${lods.size} LOD sectors left out`,
    );
    if (scriptReport) report.push(`  ${scriptReport}`);
    return { triggers, lods, breakables: breakingNames, text, scripts, local, objectsGone };
}

// --- converting -------------------------------------------------------------------------------------------------

// --frame recentre (the frame this converter used before THUGPRO's; migrate_layouts.ts works out from it where
// layouts saved then were): areas far from the origin (Oil Rig reaches z 112,000) moved so the middle of their
// collision is at the origin (x / z, in whole 16-unit steps), when it is further than `limit`.
export function recentreShift(col: thug2Col.ColFile, limit: number): number[] {
    const boxes = col.objects.filter((o) => o.faces.length).map((o) => o.bbox);
    if (!boxes.length) return [0.0, 0.0, 0.0];
    const lo = [0, 1, 2].map((k) => pyMin(boxes.map((b) => b[k]!)));
    const hi = [0, 1, 2].map((k) => pyMax(boxes.map((b) => b[k + 4]!)));
    const far = (v: number[]): number => Math.max(Math.abs(v[0]!), Math.abs(v[2]!));
    if (far(lo) < limit && far(hi) < limit) return [0.0, 0.0, 0.0];
    return [-16.0 * roundHalfEven((lo[0]! + hi[0]!) / 32.0), 0.0, -16.0 * roundHalfEven((lo[2]! + hi[2]!) / 32.0)];
}

function convertSky(args: Args, out: string, report: string[]): string | null {
    const file = findPath(args.thaw, "Game", "data", "skies", SKY, `${SKY.toLowerCase()}.pak.wpc`);
    if (!file) return null;
    const files = zoneFiles(file);
    const warnings: string[] = [];
    const name = "thaw_sky";
    const scene = convertScene(thaw.readGeom(need(files, ".geom"), SKY), warnings);
    const textures = thaw.readTex(need(files, ".tex"), SKY);
    common.writeLevel(pyJoin(out, "levels", name), name, scene, textures, null);
    common.reportLevel(report, name, scene, textures, null, warnings);
    return name;
}

export interface Args {
    command: "convert" | "selfcheck";
    levels: string[];
    thug2: string;
    thaw: string;
    out: string | null;
    ns: string;
    frame: "thugpro" | "recentre";
    recentre: number;
    compat: string;
    legacyNames: boolean;
    static: boolean;
}

function cmdConvert(args: Args): void {
    const out = args.out || pyJoin(args.thug2, STAGING);
    if (!fs.existsSync(args.ns)) pyExit(`${args.ns} not found: build it with tools\\build_neverscript.ps1`);
    if (!findPath(args.thaw, "Game", "data", "pak", "dbg.pak.wpc"))
        pyExit(`${args.thaw}: THAW's Game\\data\\pak not found`);
    const names = thaw.loadNames(args.thaw);
    const structs = levelStructs(args.thaw, names);
    const wanted = args.levels.length ? args.levels : LEVELS.filter((l) => l[5]).map((l) => l[0]);
    if (isDir(out)) fs.rmSync(out, { recursive: true });
    fs.mkdirSync(out, { recursive: true });
    const manifest: { levels: Record<string, unknown>[] } = { levels: [] };
    const report: string[] = [];
    const port = args.static ? null : new Port(args, names);
    const sky = convertSky(args, out, report);
    for (const requested of wanted) {
        const info = BY_ZONE.get(requested.toLowerCase()) ?? BY_ZONE.get(`z_${requested.toLowerCase()}`);
        if (!info) {
            pyExit(`${requested}: not a THAW level this tool knows (${LEVELS.map((l) => l[0]).join(", ")})`);
        }
        const [zone, sign, location, icon, levelNum] = info;
        const file = zonePak(args.thaw, zone);
        if (!file) {
            print(`${zone}: no ${zone.toLowerCase()}_net.pak.wpc - skipped`);
            continue;
        }
        print("converting", zone);
        const files = zoneFiles(file);
        const name = levelName(zone, args.legacyNames);
        const levelDir = pyJoin(out, "levels", name);
        const colIn = thaw.readCol(need(files, ".col"), `${zone}.col`);
        const shift =
            args.frame === "thugpro"
                ? frameShift(zone, zoneArray(files, zone, names), names)
                : recentreShift(colIn, args.recentre);
        if (shift.some((x) => x !== 0)) {
            report.push(`${name}: moved by (${formatG(shift[0]!)}, ${formatG(shift[1]!)}, ${formatG(shift[2]!)})`);
        }
        const geom = thaw.readGeom(need(files, ".geom"), `${zone}.geom`);
        const night = new Set([...nightSectors(geom, names), ...leftOut(zone)]);
        const nodeReport = night.size ? [`  ${night.size} night-time sectors left out`] : [];
        const drawn = new Set(
            geom.sectors
                .filter(
                    (s) =>
                        !(s.flags & thug2Scene.SEC_BILLBOARD) &&
                        s.meshes.some((m) => m.strip.length >= 3 && m.vertexCount),
                )
                .map((s) => s.checksum),
        );
        const level = convertLevelNodes(
            files,
            zone,
            name,
            args.ns,
            levelDir,
            nodeReport,
            names,
            shift,
            night,
            port,
            drawn,
            new Set(colIn.objects.map((o) => o.checksum)),
        );
        const warnings: string[] = [];
        const scene = convertScene(
            geom,
            warnings,
            shift,
            new Set([...level.lods, ...night, ...level.objectsGone]),
            level.local,
        );
        const sceneSectors = new Set(scene.sectors.map((s) => s.checksum));
        const missing = [...level.local].filter((c) => !sceneSectors.has(c));
        if (missing.length) {
            throw new FormatError(
                `${zone}: LevelObjects without a sector: ${missing
                    .sort((a, b) => a - b)
                    .map((c) => qb.nameText(c, names))
                    .join(", ")}`,
            );
        }
        let [nodeText, cleared] = nodes.stripMissingTrickObjects(level.text, sceneSectors);
        if (cleared) {
            writeLevelQb(args.ns, nodeText, pyJoin(levelDir, `${name}.qb`), level.scripts, names);
            nodeReport.push(`  ${cleared} geometry nodes without a scene sector: TrickObject / Cluster dropped`);
        }
        const textures: tex.TexFile = thaw.readTex(need(files, ".tex"), `${zone}.tex`);
        const col = convertCol(
            colIn,
            (c) => level.triggers.has(c),
            shift,
            new Set([...leftOut(zone), ...level.objectsGone]),
            level.breakables,
            level.local,
        );
        // (restarts over nothing - the shops' interiors - or over a kill zone: no network spawns, no kill targets)
        let groundReport: string[];
        [nodeText, groundReport] = nodes.groundRestarts(nodeText, col);
        if (groundReport.length) {
            writeLevelQb(args.ns, nodeText, pyJoin(levelDir, `${name}.qb`), level.scripts, names);
            nodeReport.push(...groundReport);
        }
        common.writeLevel(levelDir, name, scene, textures, col);
        common.reportLevel(report, name, scene, textures, col, warnings);
        report.push(...nodeReport);
        const levelStruct = structs.get(zone.toLowerCase());
        const structName = levelStruct?.get("name");
        const defaultStats = levelStruct?.get("default_stats");
        manifest.levels.push({
            folder: zone,
            level: name,
            sky,
            display: `THAW ${sign}`,
            name: typeof structName === "string" ? structName : pyTitle(sign),
            game: "THAW",
            location,
            icon,
            level_num: levelNum,
            default_stats: typeof defaultStats === "number" && Number.isInteger(defaultStats) ? defaultStats : 10,
            lighting: {},
        });
    }
    portedLevels.writeManifest(pyJoin(out, portedLevels.MANIFEST), manifest);
    print(report.join("\n"));
    print(`wrote ${out} (${manifest.levels.length} levels); install with tools\\menu_level\\make_menu_level.ts`);
}

// --- self-check -------------------------------------------------------------------------------------------------

function cmdSelfcheck(args: Args): number {
    const pre = pyJoin(args.thug2, "Data", "pre");
    let [checked, failures] = common.thug2RoundTrips(pre, ["LAscn.prx", "LAcol.prx"]);
    const names = thaw.loadNames(args.thaw);
    print(`${names.size} checksum names from dbg.pak`);
    const data = findPath(args.thaw, "Game", "data")!;
    const paks = [findPath(data, "pak", "qb.pak.wpc")!];
    for (const folder of ["worlds\\worldzones", "skies"]) {
        const root = pyJoin(data, folder);
        for (const zone of fs.readdirSync(root).sort()) {
            for (const entry of fs.readdirSync(pyJoin(root, zone)).sort()) {
                const lower = entry.toLowerCase();
                if (lower.endsWith("_net.pak.wpc") || (folder === "skies" && lower.endsWith(".pak.wpc"))) {
                    paks.push(pyJoin(root, zone, entry));
                }
            }
        }
    }
    for (const file of paks) {
        const counts = new Map<string, number>();
        try {
            for (const [entry, bytes] of thaw.pakFiles(file)) {
                const ext = entry.extension;
                if ([".qb", ".nqb", ".sqb"].includes(ext)) {
                    new thaw.QbItems(bytes, entry.path || ext, names);
                } else if (ext === ".tex") {
                    thaw.readTex(bytes, ext);
                } else if (ext === ".col") {
                    const stripped = thaw.stripCol(bytes);
                    if (!thug2Col.writeCol(thug2Col.readCol(stripped, ext)).equals(stripped)) {
                        throw new FormatError("collision not the same written again");
                    }
                } else if (ext === ".geom") {
                    convertScene(thaw.readGeom(bytes, ext), []);
                } else {
                    continue;
                }
                counts.set(ext, (counts.get(ext) ?? 0) + 1);
            }
        } catch (e) {
            // (Python's FormatError, ValueError, IndexError, struct.error)
            if (!(e instanceof FormatError || e instanceof StructError || e instanceof RangeError)) throw e;
            print(`FAIL ${file}: ${e.message}`);
            failures += 1;
            continue;
        }
        checked += [...counts.values()].reduce((a, b) => a + b, 0);
        const listed = [...counts]
            .sort((a, b) => (a[0] < b[0] ? -1 : a[0] > b[0] ? 1 : 0))
            .map(([e, n]) => `${n} ${e}`)
            .join(", ");
        print(`ok   ${path.win32.relative(data, file)}: ${listed}`);
    }
    print(`${checked} files checked, ${failures} failures`);
    return failures ? 1 : 0;
}

// --- the command line -------------------------------------------------------------------------------------------

const USAGE =
    "usage: thaw_levels.ts [-h] [--thug2 THUG2] [--thaw THAW] [--out OUT] [--ns NS] [--frame {thugpro,recentre}]\n" +
    "                      [--recentre RECENTRE] [--compat {thugpro,none}] [--legacy-names] [--static]\n" +
    "                      {convert,selfcheck} [levels ...]";

// argparse's error: the usage and the message on stderr, exit code 2.
function usageError(message: string): never {
    process.stderr.write(`${USAGE}\nthaw_levels.ts: error: ${message}\n`);
    pyExit(2);
}

export function parseCommandLine(argv: readonly string[]): Args {
    let parsed: ReturnType<typeof parseArgs<{ options: typeof OPTIONS; allowPositionals: true }>>;
    try {
        parsed = parseArgs({ args: [...argv], options: OPTIONS, allowPositionals: true, strict: true });
    } catch (e) {
        usageError((e as Error).message);
    }
    const v = parsed.values;
    if (v.help) {
        print(USAGE);
        pyExit(0);
    }
    const [command, ...levels] = parsed.positionals;
    if (command !== "convert" && command !== "selfcheck") {
        usageError(
            command === undefined
                ? "the following arguments are required: command"
                : `argument command: invalid choice: '${command}' (choose from convert, selfcheck)`,
        );
    }
    const frame = v.frame ?? "thugpro";
    if (frame !== "thugpro" && frame !== "recentre") {
        usageError(`argument --frame: invalid choice: '${frame}' (choose from thugpro, recentre)`);
    }
    const compatName = v.compat ?? "thugpro";
    if (!(compat.PROFILES as readonly string[]).includes(compatName)) {
        usageError(`argument --compat: invalid choice: '${compatName}' (choose from ${compat.PROFILES.join(", ")})`);
    }
    let recentre = 20000.0;
    if (v.recentre !== undefined) {
        try {
            recentre = pyParseFloat(v.recentre);
        } catch {
            usageError(`argument --recentre: invalid float value: '${v.recentre}'`);
        }
    }
    return {
        command,
        levels,
        thug2: v.thug2 ?? "C:\\Games\\THUG2\\Game",
        thaw: v.thaw ?? "C:\\Games\\THAW",
        out: v.out ?? null,
        ns: v.ns ?? common.NS,
        frame,
        recentre,
        compat: compatName,
        legacyNames: v["legacy-names"] ?? false,
        static: v.static ?? false,
    };
}

const OPTIONS = {
    help: { type: "boolean", short: "h" },
    thug2: { type: "string" },
    thaw: { type: "string" },
    out: { type: "string" }, // where convert writes (default: <thug2>\thug2-decomp-thaw)
    ns: { type: "string" },
    // where each area sits: thugpro - moved by its Zone_Origin node as THUGPRO's toolchain does (frameShift;
    // default), or recentre (the older one: an area reaching further than --recentre moved to the origin, x / z)
    frame: { type: "string" },
    recentre: { type: "string" }, // --frame recentre: move an area whose collision reaches further than this (x / z)
    // compatibility profile (tools/compat): thugpro - names and frames as THUGPRO's THAW ports (default), or none
    compat: { type: "string" },
    "legacy-names": { type: "boolean" }, // name the levels thaw_<xx> as before, not by their zones (z_ma)
    static: { type: "boolean" }, // convert: the levels' scripts, moving objects, peds and cars left out
} as const;

export function main(argv: readonly string[] = process.argv.slice(2)): number {
    const args = parseCommandLine(argv);
    setCompat(args.compat);
    // (a stock install keeps the game in a Game subfolder: C:\Games\THUG2 -> C:\Games\THUG2\Game)
    if (!isDir(pyJoin(args.thug2, "Data")) && isDir(pyJoin(args.thug2, "Game", "Data"))) {
        args.thug2 = pyJoin(args.thug2, "Game");
    }
    if (args.command === "selfcheck") return cmdSelfcheck(args);
    cmdConvert(args);
    return 0;
}

if (isMain(import.meta.url)) await runMain(() => main());
