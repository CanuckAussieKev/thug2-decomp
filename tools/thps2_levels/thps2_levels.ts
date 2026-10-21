// NOT ORIGINAL - converts Tony Hawk's Pro Skater 2 (PC) levels to THUG2's formats so that THUG2 can load and play
// them (README.md). Nothing of either game is stored in the repo: the files are made from the user's own copies.
//
//   node tools/thps2_levels/thps2_levels.ts convert [levels...] [--thug2 C:\Games\THUG2] [--thps2 C:\Games\THPS2]
//   node tools/thps2_levels/thps2_levels.ts selfcheck | list
//   then: node tools/menu_level/make_menu_level.ts   (installs them into the data overlay, with level select signs)
//
// convert reads <thps2>\ALL.PKR (pkr.ts: data\<base>.psx, _l.psx, _t.trg, newtex\<Folder>\*.bmp - formats in
// thps2.ts) and writes <thug2>\thug2-decomp-thps2\ (not read by the game): levels\<level>\<level>.scn.xbx / .tex.xbx /
// .col.xbx / .qb (the node array) and levels.json (what make_menu_level.ts adds to Levels.qb and the level select:
// tools/level_port/ported_levels.ts). Every level is THPS2's own data - never THUG1's or THUGPRO's remakes (the user's
// decision, 2026-10-21): its own entries, thps2_<name>, in a THPS2 section of the level select.
//
// What a level becomes:
//   - each .psx object (a model at a position; THPS2 never turns them) a THUG2 sector and a collision object of the
//     same name; quads split in two, faces flagged 0x80 (collision only) not drawn;
//   - the space: THPS2's (x, y down, z) in vertex units as THUG2's (x, y up, z) in inches: (x, -y, -z) /
//     UNITS_PER_INCH, a half turn about x (io_thps_scene's importer does the same). (x, -y, z) - a reflection - drew
//     Hangar's MAKE 'EM PAY sign mirrored in the game. Triangles turn the other way round: THPS2's outward normal is
//     -(v1 - v0) x (v2 - v0), THUG2's (b - a) x (c - a) (its collision);
//   - textures: the PC version's newtex BMPs where there is one (twice the PlayStation's size), with the PlayStation
//     texture's transparency (0x0000 and magenta) as their alpha; else the PlayStation texture; 32-bit with mip levels;
//   - vertex colours: THPS2's (128 = neutral) as THUG2's (128 = full) - the same numbers;
//   - nodes: restarts (the level's start first, as Player1; 2P / horse ones for those modes) and rails.

import * as fs from "node:fs";
import * as common from "../level_port/common.ts";
import * as geometry from "../level_port/geometry.ts";
import * as nodes from "../level_port/nodes.ts";
import * as portedLevels from "../level_port/ported_levels.ts";
import * as qb from "../level_port/qb.ts";
import type * as tex from "../level_port/tex.ts";
import * as thug2Col from "../level_port/thug2_col.ts";
import * as thug2Scene from "../level_port/thug2_scene.ts";
import { parseCli } from "../lib/cli.ts";
import { resolveInstall } from "../lib/install.ts";
import { isMain, print, pyExit, pyJoin, runMain } from "../lib/py.ts";
import { mipDown } from "../thps3_levels/thps3_levels.ts";
import { Pkr } from "./pkr.ts";
import * as thps2 from "./thps2.ts";

export const STAGING = "thug2-decomp-thps2";

// THPS2's vertex units per THUG2 inch. THPS2's Hangar is 2.26 times THUG1's remake of it across (THUG1's HN
// collision: 11,100 inches; THPS2's: 25,093 units) - io_thps_scene's importer uses 2.25 too.
export const UNITS_PER_INCH = 2.25;

// The THPS2 levels: base name in ALL.PKR, our level name (thps2_<name>), sign text, place, newtex folder, level
// select number (each its own: THPS3's are 60-76, THAW's 80-94), THUG2 sign texture (a placeholder icon).
export interface LevelInfo {
    base: string;
    name: string;
    sign: string;
    location: string;
    newtex: string;
    levelNum: number;
    icon: string;
}

export const LEVELS: readonly LevelInfo[] = [
    {
        base: "skhan",
        name: "hangar",
        sign: "HANGAR",
        location: "Mullet Falls, Montana, USA",
        newtex: "Hangar",
        levelNum: 100,
        icon: "sign_airport",
    },
    {
        base: "sksl2",
        name: "school2",
        sign: "SCHOOL II",
        location: "Southern California, USA",
        newtex: "School2",
        levelNum: 101,
        icon: "sign_training",
    },
    {
        base: "skmar",
        name: "marseille",
        sign: "MARSEILLE",
        location: "Marseille, France",
        newtex: "Mar",
        levelNum: 102,
        icon: "sign_barcelona",
    },
    {
        base: "skny",
        name: "ny",
        sign: "NY CITY",
        location: "New York, USA",
        newtex: "NY",
        levelNum: 103,
        icon: "sign_boston",
    },
    {
        base: "skven",
        name: "venice",
        sign: "VENICE BEACH",
        location: "Venice Beach, Los Angeles, USA",
        newtex: "Venice",
        levelNum: 104,
        icon: "sign_la",
    },
    {
        base: "skss",
        name: "skatestreet",
        sign: "SKATESTREET",
        location: "Skatestreet, Ventura, California, USA",
        newtex: "SS",
        levelNum: 105,
        icon: "sign_training",
    },
    {
        base: "skph",
        name: "philly",
        sign: "PHILADELPHIA",
        location: "Philadelphia, USA",
        newtex: "Philly",
        levelNum: 106,
        icon: "sign_philly",
    },
    {
        base: "skbul",
        name: "bullring",
        sign: "THE BULLRING",
        location: "a bullring, Mexico",
        newtex: "Bullring",
        levelNum: 107,
        icon: "sign_barcelona",
    },
    {
        base: "skb1",
        name: "chopperdrop",
        sign: "CHOPPER DROP",
        location: "Hawaii, USA",
        newtex: "Hawaii",
        levelNum: 108,
        icon: "sign_australia",
    },
    {
        base: "skhvn",
        name: "skateheaven",
        sign: "SKATE HEAVEN",
        location: "Skate Heaven (no place)",
        newtex: "Heaven",
        levelNum: 109,
        icon: "sign_skatopia",
    },
];

export function levelName(info: LevelInfo): string {
    return `thps2_${info.name}`;
}

// --- the space -----------------------------------------------------------------------------------------------------

export type Vec3 = [number, number, number];

// A THPS2 point (vertex units) in THUG2's geometry (inches).
export function toThug(p: readonly number[]): Vec3 {
    return [p[0]! / UNITS_PER_INCH, 0 - p[1]! / UNITS_PER_INCH, 0 - p[2]! / UNITS_PER_INCH];
}

// The same point as a THUG2 node's Position (THUG2 reads a node's z negated from the geometry's - as THPS3's /
// THPS4's node arrays, tools/thps3_levels).
export function toNodePosition(p: readonly number[]): Vec3 {
    const [x, y, z] = toThug(p);
    return [x, y, 0 - z];
}

// A THPS2 plane normal (any length) as a THUG2 unit normal.
export function toThugNormal(n: readonly number[]): Vec3 {
    const length = Math.hypot(n[0]!, n[1]!, n[2]!) || 1;
    return [n[0]! / length, 0 - n[1]! / length, 0 - n[2]! / length];
}

// A restart's heading (THPS2's angle about its y axis, 4096 = a turn) as THUG2's Angles y (radians). In the game a
// node's Angles y of h faces the skater h from the geometry's +z towards +x (the screenshot sidecar's heading); THPS2's
// angle 0 faces THPS2's -z - THUG2's +z (Hangar's start, down its drop-in) - and turns from +z towards -x: 16 of the 20
// restarts whose heading isn't on an axis face open floor that way, 4 the other (docs/pending/thps2.md).
export function heading(angle: number): number {
    const turn = (((angle % 4096) + 4096) % 4096) / 4096;
    let h = -turn * 2 * Math.PI;
    while (h > Math.PI) h -= 2 * Math.PI;
    while (h <= -Math.PI) h += 2 * Math.PI;
    return h;
}

// --- faces ---------------------------------------------------------------------------------------------------------

// A face's triangles as corner indices (0-3), turned for THUG2: THPS2's 0 1 2 (a quad, 0 1 / 2 3, is 0 1 2 and
// 1 3 2) has its outward normal along -(v1 - v0) x (v2 - v0); after the half turn THUG2's (b - a) x (c - a) has to
// point out - 0 2 1 (and 1 2 3).
export function faceTriangles(face: thps2.PsxFace): [number, number, number][] {
    return face.vertices.length === 3
        ? [[0, 2, 1]]
        : [
              [0, 2, 1],
              [1, 2, 3],
          ];
}

// A face's uvs in THUG2's units (v down, as THPS2's): THPS2's are texel numbers, n / size. They tile: more than half
// of the ten levels' textured faces reach past their texture (u up to 255 on a 16-texel texture; the spans mostly
// n x size - 2: 126, 190, 254 on 64-wide textures), so the texture repeats, and the mapping stays linear (neighbouring
// faces share their edge's numbers - Hangar's signs: 0..63 and 63..126 of a 128-wide texture). Phase 1 took the last
// texel as the far edge and clamped: every number past the texture collapsed onto its last column - the smeared
// stripes and flat-looking faces the user saw (2026-10-21).
export function faceUvs(face: thps2.PsxFace, width: number, height: number): [number, number][] {
    const out: [number, number][] = [];
    for (let k = 0; k < face.vertices.length; k++) {
        out.push([(face.uvs[2 * k] ?? 0) / Math.max(1, width), (face.uvs[2 * k + 1] ?? 0) / Math.max(1, height)]);
    }
    return out;
}

// Whether a face's uvs reach past its texture (the texture repeats there).
export function faceTiles(face: thps2.PsxFace, width: number, height: number): boolean {
    for (let k = 0; k < face.vertices.length; k++) {
        if ((face.uvs[2 * k] ?? 0) > width || (face.uvs[2 * k + 1] ?? 0) > height) return true;
    }
    return false;
}

// A face's corner colours (r, g, b; 128 = neutral).
export function faceColors(face: thps2.PsxFace, rgbs: readonly (readonly number[])[]): Vec3[] {
    const out: Vec3[] = [];
    for (let k = 0; k < face.vertices.length; k++) {
        if (face.flags & thps2.FACE_GOURAUD) {
            const c = rgbs[face.shade[k]!] ?? [128, 128, 128];
            out.push([c[0]!, c[1]!, c[2]!]);
        } else {
            out.push([face.shade[0], face.shade[1], face.shade[2]]);
        }
    }
    return out;
}

// A face's collision in THUG2's flags, or null: not collided with (THPS2's trigger planes and the invisible walls on
// vert lips - THUG2 keeps a skater in the air over a vert face itself). `up`: the THUG2 normal's y.
export function collisionFlags(face: thps2.PsxFace, up: number): number | null {
    const invisible = (face.flags & thps2.FACE_INVISIBLE) !== 0;
    if (invisible && face.surface & (thps2.SURFACE_TRIGGER | thps2.SURFACE_VERT_WALL)) return null;
    let flags = 0;
    if (invisible) flags |= thug2Col.FACE_CAMERA_PASSES;
    if (face.surface & thps2.SURFACE_VERT) flags |= thug2Col.FACE_VERT;
    else if (Math.abs(up) < 0.3) flags |= thug2Col.FACE_WALL_RIDABLE;
    return flags;
}

// --- textures ------------------------------------------------------------------------------------------------------

export interface Bmp {
    width: number;
    height: number;
    rgb: Buffer; // top row first, r g b
}

// A 24-bit Windows BMP (newtex's).
export function readBmp(data: Buffer, name = "bmp"): Bmp {
    if (data.length < 54 || data.toString("latin1", 0, 2) !== "BM") throw new thps2.FormatError(`${name}: not a BMP`);
    const offset = data.readUInt32LE(10);
    const width = data.readInt32LE(18);
    const rawHeight = data.readInt32LE(22);
    const bits = data.readUInt16LE(28);
    const compression = data.readUInt32LE(30);
    if (bits !== 24 || compression !== 0 || width <= 0 || rawHeight === 0)
        throw new thps2.FormatError(`${name}: ${bits}-bit / compression ${compression} BMP`);
    const height = Math.abs(rawHeight);
    const row = (width * 3 + 3) & ~3;
    if (offset + row * (height - 1) + width * 3 > data.length) throw new thps2.FormatError(`${name}: BMP cut short`);
    const rgb = Buffer.alloc(width * height * 3);
    for (let y = 0; y < height; y++) {
        const src = offset + (rawHeight > 0 ? height - 1 - y : y) * row;
        for (let x = 0; x < width; x++) {
            rgb[(y * width + x) * 3] = data[src + x * 3 + 2]!;
            rgb[(y * width + x) * 3 + 1] = data[src + x * 3 + 1]!;
            rgb[(y * width + x) * 3 + 2] = data[src + x * 3]!;
        }
    }
    return { width, height, rgb };
}

// A texture's first level in THUG2's 32-bit form (B G R A): the BMP's colours with the PlayStation texture's alpha
// (nearest texel), or the PlayStation texture itself. Transparent texels take the colour of an opaque neighbour, so
// that filtering doesn't draw dark fringes around cut-outs.
export function textureLevel(t: thps2.PsxTexture, bmp: Bmp | null): [number, number, Buffer] {
    const width = bmp ? bmp.width : t.width;
    const height = bmp ? bmp.height : t.height;
    const out = Buffer.alloc(width * height * 4);
    for (let y = 0; y < height; y++) {
        const sy = Math.min(t.height - 1, Math.floor((y * t.height) / height));
        for (let x = 0; x < width; x++) {
            const sx = Math.min(t.width - 1, Math.floor((x * t.width) / width));
            const s = (sy * t.width + sx) * 4;
            const o = (y * width + x) * 4;
            const alpha = t.rgba[s + 3]!;
            if (bmp) {
                const b = (y * width + x) * 3;
                out[o] = bmp.rgb[b + 2]!;
                out[o + 1] = bmp.rgb[b + 1]!;
                out[o + 2] = bmp.rgb[b]!;
            } else {
                out[o] = t.rgba[s + 2]!;
                out[o + 1] = t.rgba[s + 1]!;
                out[o + 2] = t.rgba[s]!;
            }
            out[o + 3] = alpha;
        }
    }
    bleed(out, width, height);
    return [width, height, out];
}

// Transparent texels' colour from their opaque neighbours (a few passes), alpha left 0.
export function bleed(bgra: Buffer, width: number, height: number, passes = 4): void {
    for (let pass = 0; pass < passes; pass++) {
        const fill: [number, number, number, number][] = [];
        for (let y = 0; y < height; y++) {
            for (let x = 0; x < width; x++) {
                const o = (y * width + x) * 4;
                if (bgra[o + 3] !== 0 || (bgra[o] ?? 0) + (bgra[o + 1] ?? 0) + (bgra[o + 2] ?? 0) > 0) continue;
                let n = 0;
                const sum = [0, 0, 0];
                for (const [dx, dy] of [
                    [1, 0],
                    [-1, 0],
                    [0, 1],
                    [0, -1],
                ] as const) {
                    const nx = x + dx;
                    const ny = y + dy;
                    if (nx < 0 || ny < 0 || nx >= width || ny >= height) continue;
                    const q = (ny * width + nx) * 4;
                    if (bgra[q + 3] === 0 && bgra[q]! + bgra[q + 1]! + bgra[q + 2]! === 0) continue;
                    sum[0]! += bgra[q]!;
                    sum[1]! += bgra[q + 1]!;
                    sum[2]! += bgra[q + 2]!;
                    n++;
                }
                if (n) fill.push([o, Math.round(sum[0]! / n), Math.round(sum[1]! / n), Math.round(sum[2]! / n)]);
            }
        }
        if (!fill.length) return;
        for (const [o, b, g, r] of fill) {
            bgra[o] = Math.max(1, b);
            bgra[o + 1] = g;
            bgra[o + 2] = r;
        }
    }
}

export function hasTransparency(t: thps2.PsxTexture): boolean {
    for (let i = 3; i < t.rgba.length; i += 4) if (t.rgba[i] === 0) return true;
    return false;
}

export function makeTexture(checksum: number, t: thps2.PsxTexture, bmp: Bmp | null, mips: boolean): tex.Texture {
    const [width, height, first] = textureLevel(t, bmp);
    const levels = [first];
    let w = width;
    let h = height;
    while (mips && Math.min(w, h) > 4) {
        levels.push(mipDown(levels[levels.length - 1]!, w, h));
        w = Math.max(1, Math.floor(w / 2));
        h = Math.max(1, Math.floor(h / 2));
    }
    return { checksum, width, height, texelDepth: 32, paletteDepth: 32, dxt: 0, palette: Buffer.alloc(0), levels };
}

// --- materials -----------------------------------------------------------------------------------------------------

// `wrap`: the texture repeats (some face reaches past it); else clamped (cut-outs and signs: nothing bleeds in from
// the far side at their edges).
export function makeMaterial(checksum: number, texture: number, semi: boolean, wrap = false): thug2Scene.Material {
    let flags = thug2Scene.PASS_SMOOTH;
    if (texture) flags |= thug2Scene.PASS_TEXTURED;
    // (semi-transparent: THUG2's blend by the vertex alpha - a half, the PlayStation's usual 0.5 B + 0.5 F)
    flags |= semi ? thug2Scene.PASS_TRANSPARENT : thug2Scene.PASS_IGNORE_VERTEX_ALPHA;
    return {
        checksum,
        nameChecksum: checksum,
        alphaCutoff: 1,
        sorted: 0,
        drawOrder: semi ? 1500.0 : 0.0,
        singleSided: 0,
        noBackfaceCulling: 0,
        zBias: 0,
        grassify: 0,
        specularPower: 0.0,
        passes: [
            {
                texture,
                flags,
                blendMode: semi ? 5 : 0,
                fixedAlpha: 0,
                uAddress: wrap ? 0 : 1,
                vAddress: wrap ? 0 : 1,
                envmapTiling: [3.0, 3.0],
                filtering: 0x10004,
                mip: [1, 4, -8.0, -8.0],
                hasColor: 0,
                color: [0.5, 0.5, 0.5],
            },
        ],
    };
}

// --- scene and collision -------------------------------------------------------------------------------------------

export const SECTOR_FLAGS = (0x80002000 | thug2Scene.SEC_TEXCOORDS | thug2Scene.SEC_COLORS) >>> 0; // (as THPS3's)
export const SEMI_ALPHA = 64; // a half, in THUG2's 128 = full

export function trisToStrip(tris: readonly (readonly number[])[]): number[] {
    // A triangle list as one strip, the triangles joined by degenerate ones (each starts at an even index, so its
    // turning order is kept) - as tools/thps3_levels' trisToStrip.
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

export interface LevelTextures {
    byName: Map<number, thps2.PsxTexture>; // the PlayStation textures by name checksum
    bmps: Map<number, Bmp>; // newtex's, by name checksum
}

export interface ConvertedWorld {
    scene: thug2Scene.Scene;
    textures: tex.TexFile;
    col: thug2Col.ColFile;
    warnings: string[];
    counts: { objects: number; faces: number; hidden: number; noCollision: number; bmps: number };
}

export function convertWorld(world: thps2.Psx, level: string, textures: LevelTextures, mips = true): ConvertedWorld {
    const warnings: string[] = [];
    const counts = { objects: 0, faces: 0, hidden: 0, noCollision: 0, bmps: 0 };
    const materials = new Map<string, thug2Scene.Material>();
    const texOut = new Map<number, tex.Texture>();
    // the textures some face repeats
    const tiled = new Set<number>();
    for (const model of world.models) {
        for (const face of model.faces) {
            if (!(face.flags & thps2.FACE_TEXTURED)) continue;
            const name = world.textureNames[face.texture];
            const t = name === undefined ? undefined : textures.byName.get(name);
            if (t !== undefined && faceTiles(face, t.width, t.height)) tiled.add(name!);
        }
    }
    const materialFor = (texture: number, semi: boolean): number => {
        const key = `${texture}|${semi ? 1 : 0}`;
        let m = materials.get(key);
        if (m === undefined) {
            m = makeMaterial(qb.checksum(`${level}_material_${materials.size}`), texture, semi, tiled.has(texture));
            materials.set(key, m);
        }
        return m.checksum;
    };
    const sectors: geometry.SourceSector[] = [];
    const colObjects: geometry.SourceColObject[] = [];
    world.objects.forEach((object, index) => {
        const model = world.models[object.model]!;
        const checksum = qb.checksum(`${level}_object_${index}`);
        const place = (v: readonly number[]): Vec3 =>
            toThug([v[0]! + object.position[0], v[1]! + object.position[1], v[2]! + object.position[2]]);
        // scene: a vertex per (model vertex, colour, uv)
        const positions: number[][] = [];
        const colors: number[][] = [];
        const uvs: number[][] = [];
        const keyed = new Map<string, number>();
        const byMaterial = new Map<number, number[][]>();
        // collision: the model's vertices
        const verts = model.vertices.map(place);
        const intensity = new Array<number>(verts.length).fill(-1);
        const faces: thug2Col.Face[] = [];
        for (const face of model.faces) {
            counts.faces++;
            const colorsOf = faceColors(face, world.rgbs);
            const normal = toThugNormal(model.normals[face.plane] ?? [0, -4096, 0]);
            face.vertices.forEach((v, k) => {
                const c = colorsOf[k]!;
                intensity[v] = Math.max(intensity[v]!, Math.min(255, Math.max(c[0], c[1], c[2])));
            });
            const flags = collisionFlags(face, normal[1]);
            if (flags === null) counts.noCollision++;
            else
                for (const [a, b, c] of faceTriangles(face))
                    faces.push([flags, 0, face.vertices[a]!, face.vertices[b]!, face.vertices[c]!]);
            if (face.flags & thps2.FACE_INVISIBLE) {
                counts.hidden++;
                continue;
            }
            let texture = 0;
            let size: [number, number] = [1, 1];
            if (face.flags & thps2.FACE_TEXTURED) {
                const name = world.textureNames[face.texture];
                const t = name === undefined ? undefined : textures.byName.get(name);
                if (t === undefined) {
                    warnings.push(
                        `texture ${name === undefined ? `#${face.texture}` : hex(name)} not found - untextured`,
                    );
                } else {
                    texture = name!;
                    size = [t.width, t.height];
                    if (!texOut.has(texture)) {
                        const bmp = textures.bmps.get(texture) ?? null;
                        if (bmp) counts.bmps++;
                        texOut.set(texture, makeTexture(texture, t, bmp, mips));
                    }
                }
            }
            const semi = (face.flags & thps2.FACE_SEMI_TRANSPARENT) !== 0;
            const material = materialFor(texture, semi);
            const cornerUvs = texture
                ? faceUvs(face, size[0], size[1])
                : face.vertices.map((): [number, number] => [0, 0]);
            const corner = (k: number): number => {
                const c = colorsOf[k]!;
                const uv = cornerUvs[k]!;
                const alpha = semi ? SEMI_ALPHA : 128;
                const key = `${face.vertices[k]}|${c.join(",")},${alpha}|${uv[0]},${uv[1]}`;
                let i = keyed.get(key);
                if (i === undefined) {
                    i = positions.length;
                    keyed.set(key, i);
                    positions.push(verts[face.vertices[k]!]!);
                    colors.push([c[2], c[1], c[0], alpha]); // (B G R A)
                    uvs.push([uv[0], uv[1]]);
                }
                return i;
            };
            let list = byMaterial.get(material);
            if (list === undefined) {
                list = [];
                byMaterial.set(material, list);
            }
            for (const [a, b, c] of faceTriangles(face)) list.push([corner(a), corner(b), corner(c)]);
        }
        counts.objects++;
        if (positions.length) {
            const [lo, hi, center, radius] = geometry.bounds(positions);
            sectors.push({
                checksum,
                bone: -1,
                flags: SECTOR_FLAGS,
                bbox: [...lo, ...hi],
                sphere: [center[0]!, center[1]!, center[2]!, radius],
                positions,
                colors,
                uvSets: 1,
                uvs,
                meshes: [...byMaterial].map(([material, tris]) => ({ flags: 0, material, strip: trisToStrip(tris) })),
            });
        }
        if (faces.length) {
            const lo = [0, 1, 2].map((k) => Math.min(...verts.map((v) => v[k]!)));
            const hi = [0, 1, 2].map((k) => Math.max(...verts.map((v) => v[k]!)));
            colObjects.push({
                checksum,
                flags: 0,
                verts,
                intensities: Buffer.from(intensity.map((i) => (i < 0 ? 128 : i))),
                faces,
                bbox: [lo[0]!, lo[1]!, lo[2]!, 1.0, hi[0]!, hi[1]!, hi[2]!, 1.0],
            });
        }
    });
    const scene = geometry.convertScene({ sectors }, warnings, [...materials.values()]);
    const col = geometry.convertCol(
        { objects: colObjects },
        () => false,
        warnings,
        (f) => f,
    );
    return { scene, textures: { version: 1, textures: [...texOut.values()] }, col, warnings, counts };
}

function hex(value: number): string {
    return value.toString(16).padStart(8, "0");
}

// The textures a level can use: its library's (<base>_l.psx), then the world's own (Chopper Drop has some); the
// newtex BMPs of its folder (and newtex's top folder, which some levels share) by checksum.
export function levelTextures(pkr: Pkr, info: LevelInfo, world: thps2.Psx): LevelTextures {
    const byName = new Map<number, thps2.PsxTexture>();
    const add = (psx: thps2.Psx): void => {
        for (const t of psx.textures) {
            const name = psx.textureNames[t.index];
            if (name !== undefined && !byName.has(name)) byName.set(name, t);
        }
    };
    const libName = `data/${info.base}_l.psx`;
    if (pkr.has(libName)) add(thps2.readPsx(pkr.read(libName), libName));
    add(world);
    const bmps = new Map<number, Bmp>();
    for (const folder of [`newtex/${info.newtex}/`, "newtex/"]) {
        for (const e of pkr.list(folder)) {
            const m = /^([0-9a-f]{8})\.bmp$/i.exec(e.name);
            if (!m) continue;
            const name = Number.parseInt(m[1]!, 16);
            if (bmps.has(name) || !byName.has(name)) continue;
            bmps.set(name, readBmp(pkr.readEntry(e), e.path));
        }
    }
    return { byName, bmps };
}

// --- nodes ---------------------------------------------------------------------------------------------------------

export type RestartKind = "Player1" | "Player2" | "MultiPlayer" | "Horse" | "Generic";

// What a THPS2 restart is for, by its name: the level's start (Start, Re_Start, Re_Start_Skate - else the first
// restart), the second player's / multiplayer starts (2P in the name), HORSE's spots (Ho_), the rest generic.
export function restartKinds(restarts: readonly thps2.Restart[]): RestartKind[] {
    const isStart = (n: string): boolean => /^(re_)?start(_skate)?$/i.test(n.trim());
    let first = restarts.findIndex((r) => isStart(r.name));
    if (first < 0) first = 0;
    return restarts.map((r, i): RestartKind => {
        if (i === first) return "Player1";
        if (/2p/i.test(r.name)) return /start_2p|2p start/i.test(r.name) ? "Player2" : "MultiPlayer";
        if (/^ho_/i.test(r.name)) return "Horse";
        return "Generic";
    });
}

// A restart's name for THUG2's restart list: THPS2's without its prefixes ("Ho_Start_Bike_Racks" -> "Bike Racks").
export function restartTitle(name: string): string {
    const bare = name
        .replace(/^(re_start_|re_|ho_start_|ho_)/i, "")
        .replaceAll("_", " ")
        .trim();
    return bare || name;
}

const fixed = (v: number): string => {
    const t = v.toFixed(4);
    return t === "-0.0000" ? "0.0" : t.replace(/0+$/, "").replace(/\.$/, ".0");
};
const vec = (v: readonly number[]): string => `(${v.map(fixed).join(", ")})`;

export interface NodeArrayResult {
    text: string;
    restarts: number;
    rails: number;
}

// The level's node array as NeverScript text: the restarts (Player1 first), then the rails (each point a RailNode
// linked to the next points of its rail).
export function nodeArrayText(level: string, trg: readonly thps2.TrgNode[]): NodeArrayResult {
    const restartNodes = trg.filter((n) => n.type === thps2.NODE_RESTART);
    const restarts = restartNodes.map(thps2.restartOf);
    const kinds = restartKinds(restarts);
    const order = restarts.map((_, i) => i).filter((i) => !/dummy/i.test(restarts[i]!.name));
    order.sort((a, b) => (kinds[a] === "Player1" ? -1 : kinds[b] === "Player1" ? 1 : a - b));
    const blocks: string[] = [];
    const types: Record<RestartKind, string> = {
        Player1: "[Player1, MultiPlayer, Horse]",
        Player2: "[Player2, MultiPlayer]",
        MultiPlayer: "[MultiPlayer]",
        Horse: "[Horse]",
        Generic: "[Generic]",
    };
    let n = 0;
    for (const i of order) {
        const r = restarts[i]!;
        const kind = kinds[i]!;
        n++;
        blocks.push(
            [
                "    {",
                `        Position=${vec(toNodePosition(r.position))} `,
                `        Angles=${vec([0, heading(r.angles[1]), 0])} `,
                `        Name=TRG_Restart_${String(n).padStart(2, "0")} `,
                "        Class=Restart ",
                `        Type=${kind} `,
                "        CreatedAtStart ",
                `        RestartName="${restartTitle(r.name).replaceAll('"', "")}" `,
                `        restart_types=${types[kind]} `,
                "    } ",
            ].join("\n"),
        );
    }
    // rails: their points' indices in the output, links to other rail points only
    const railNodes = trg.filter((t) => t.type === thps2.NODE_RAIL_POINT);
    const indexOf = new Map<number, number>();
    railNodes.forEach((t, k) => {
        indexOf.set(t.index, blocks.length + k);
    });
    for (const t of railNodes) {
        const p = thps2.railPointOf(t);
        const links = t.links.filter((l) => indexOf.has(l)).map((l) => indexOf.get(l)!);
        const lines = [
            "    {",
            `        Position=${vec(toNodePosition(p.position))} `,
            "        Angles=(0.0, 0.0, 0.0) ",
            `        Name=TRG_Rail_${t.index} `,
            "        Class=RailNode ",
            "        Type=Metal ",
            "        CreatedAtStart ",
            "        TerrainType=TERRAIN_METALSMOOTH ",
        ];
        if (links.length) lines.push(`        Links=[${links.join(", ")}] `);
        lines.push("    } ");
        blocks.push(lines.join("\n"));
    }
    const text = `${level}_NodeArray = [\n${blocks.join("\n")}\n]\n${nodes.loadTerrainNs()}\n`;
    return { text, restarts: order.length, rails: railNodes.length };
}

// The height of the highest collision face under (x, z) at or below `top` (THUG2's geometry), or null.
export function floorBelow(col: thug2Col.ColFile, x: number, z: number, top: number): number | null {
    let best: number | null = null;
    for (const o of col.objects) {
        const [lx, , lz, , hx, , hz] = o.bbox as number[];
        if (x < lx! || x > hx! || z < lz! || z > hz!) continue;
        for (const f of o.faces) {
            if (f[0] & thug2Col.FACE_NON_COLLIDABLE) continue;
            const a = o.verts[f[2]]!;
            const b = o.verts[f[3]]!;
            const c = o.verts[f[4]]!;
            const d = (b[2]! - c[2]!) * (a[0]! - c[0]!) + (c[0]! - b[0]!) * (a[2]! - c[2]!);
            if (Math.abs(d) < 1e-9) continue;
            const l1 = ((b[2]! - c[2]!) * (x - c[0]!) + (c[0]! - b[0]!) * (z - c[2]!)) / d;
            const l2 = ((c[2]! - a[2]!) * (x - c[0]!) + (a[0]! - c[0]!) * (z - c[2]!)) / d;
            const l3 = 1 - l1 - l2;
            if (l1 < 0 || l2 < 0 || l3 < 0) continue;
            const y = l1 * a[1]! + l2 * b[1]! + l3 * c[1]!;
            if (y <= top && (best === null || y > best)) best = y;
        }
    }
    return best;
}

// Restarts with no floor under them, or high above it (the converted collision): a restart that doesn't work.
export function checkRestarts(trg: readonly thps2.TrgNode[], col: thug2Col.ColFile): string[] {
    const out: string[] = [];
    for (const n of trg) {
        if (n.type !== thps2.NODE_RESTART) continue;
        const r = thps2.restartOf(n);
        if (/dummy/i.test(r.name)) continue;
        const [x, y, z] = toThug(r.position);
        const floor = floorBelow(col, x, z, y + 24);
        if (floor === null) out.push(`${r.name}: no floor under it`);
        else if (y - floor > 120) out.push(`${r.name}: ${Math.round(y - floor)} inches above the floor`);
    }
    return out;
}

// --- convert -------------------------------------------------------------------------------------------------------

export interface Args {
    command: "convert" | "selfcheck" | "list";
    levels: string[];
    thug2: string;
    thps2: string;
    out: string | null;
    ns: string;
    noMips: boolean;
}

export function findLevel(name: string): LevelInfo | undefined {
    const n = name.toLowerCase();
    return LEVELS.find((l) => l.base === n || l.name === n || levelName(l) === n);
}

export function cmdConvert(args: Args): number {
    const pkr = new Pkr(pyJoin(resolveInstall(args.thps2, "thps2"), "ALL.PKR"));
    const out = args.out || pyJoin(args.thug2, STAGING);
    if (!fs.existsSync(args.ns)) pyExit(`${args.ns} not found: build it with tools\\build_neverscript.ps1`);
    const wanted: LevelInfo[] = [];
    for (const name of args.levels.length ? args.levels : LEVELS.map((l) => l.base)) {
        const info = findLevel(name);
        if (info === undefined) pyExit(`${name}: not a THPS2 level (${LEVELS.map((l) => l.name).join(", ")})`);
        wanted.push(info);
    }
    if (fs.existsSync(out)) fs.rmSync(out, { recursive: true, force: true });
    fs.mkdirSync(out, { recursive: true });
    const manifest: { levels: Record<string, unknown>[] } = { levels: [] };
    const report: string[] = [];
    for (const info of wanted) {
        const name = levelName(info);
        const worldFile = `data/${info.base}.psx`;
        const world = thps2.readPsx(pkr.read(worldFile), worldFile);
        const textures = levelTextures(pkr, info, world);
        const done = convertWorld(world, name, textures, !args.noMips);
        const levelDir = pyJoin(out, "levels", name);
        common.writeLevel(levelDir, name, done.scene, done.textures, done.col);
        common.reportLevel(report, name, done.scene, done.textures, done.col, [...new Set(done.warnings)].sort());
        const c = done.counts;
        report.push(
            `  ${c.objects} objects, ${c.faces} faces (${c.hidden} not drawn, ${c.noCollision} not collided with), ` +
                `${c.bmps} of ${done.textures.textures.length} textures from newtex`,
        );
        const trgFile = `data/${info.base}_t.trg`;
        const trg = thps2.readTrg(pkr.read(trgFile), trgFile);
        const nodeArray = nodeArrayText(name, trg);
        common.nsCompile(args.ns, nodeArray.text, pyJoin(levelDir, `${name}.qb`));
        report.push(`  nodes: ${nodeArray.restarts} restarts, ${nodeArray.rails} rail points`);
        for (const problem of checkRestarts(trg, done.col)) report.push(`  restart ${problem}`);
        manifest.levels.push({
            folder: info.base,
            level: name,
            sky: null,
            display: `THPS2 ${info.sign}`,
            name: titleCase(info.sign),
            game: "THPS2",
            location: info.location,
            icon: info.icon,
            level_num: info.levelNum,
            default_stats: 6,
            lighting: {},
        });
    }
    pkr.close();
    portedLevels.writeManifest(pyJoin(out, portedLevels.MANIFEST), manifest);
    print(report.join("\n"));
    print(`wrote ${out} (${manifest.levels.length} levels); install with tools\\menu_level\\make_menu_level.ts`);
    return 0;
}

function titleCase(text: string): string {
    return text.toLowerCase().replace(/\b[a-z]/g, (c) => c.toUpperCase());
}

// Parses every level file of the ten levels (worlds, libraries, low-detail and object files, nodes, newtex BMPs) and
// THUG2's own LA / TestLevel scene / collision / textures round trip (common.thug2RoundTrips).
export function cmdSelfcheck(args: Args): number {
    let [checked, failures] = common.thug2RoundTrips(pyJoin(args.thug2, "Data", "pre"), [
        "LAscn.prx",
        "LAcol.prx",
        "TestLevelscn.prx",
        "TestLevelcol.prx",
    ]);
    const pkr = new Pkr(pyJoin(resolveInstall(args.thps2, "thps2"), "ALL.PKR"));
    for (const info of LEVELS) {
        const files = pkr.list("data/").filter((e) => e.name.toLowerCase().startsWith(info.base));
        for (const e of files) {
            let what: string;
            try {
                if (/\.psx$/i.test(e.name)) {
                    if (e.size <= 16) continue; // (skbul_o2.psx: an empty stub)
                    const p = thps2.readPsx(pkr.readEntry(e), e.path);
                    what = `${p.objects.length} objects, ${p.models.length} models, ${p.textures.length} textures`;
                } else if (/_t\.trg$/i.test(e.name)) {
                    const t = thps2.readTrg(pkr.readEntry(e), e.path);
                    const restarts = t.filter((n) => n.type === thps2.NODE_RESTART).map(thps2.restartOf).length;
                    const rails = t.filter((n) => n.type === thps2.NODE_RAIL_POINT).map(thps2.railPointOf).length;
                    what = `${t.length} nodes, ${restarts} restarts, ${rails} rail points`;
                } else continue;
            } catch (err) {
                if (!(err instanceof thps2.FormatError)) throw err;
                print(`FAIL ${e.path}: ${err.message}`);
                failures++;
                continue;
            }
            checked++;
            print(`ok   ${e.path}: ${what}`);
        }
        for (const e of pkr.list(`newtex/${info.newtex}/`)) {
            try {
                readBmp(pkr.readEntry(e), e.path);
                checked++;
            } catch (err) {
                if (!(err instanceof thps2.FormatError)) throw err;
                print(`FAIL ${e.path}: ${err.message}`);
                failures++;
            }
        }
    }
    pkr.close();
    print(`${checked} files checked, ${failures} failures`);
    return failures ? 1 : 0;
}

function cmdList(args: Args): number {
    const pkr = new Pkr(pyJoin(resolveInstall(args.thps2, "thps2"), "ALL.PKR"));
    for (const info of LEVELS) {
        const have = ["", "_l", "_o", "_2"].map((s) => `data/${info.base}${s}.psx`).filter((f) => pkr.has(f));
        print(
            `${levelName(info).padEnd(20)} ${info.sign.padEnd(14)} ${info.base}: ${have.length} psx files, ` +
                `${pkr.has(`data/${info.base}_t.trg`) ? "nodes" : "NO NODES"}, ${pkr.list(`newtex/${info.newtex}/`).length} newtex BMPs`,
        );
    }
    pkr.close();
    return 0;
}

const USAGE = `usage: thps2_levels.ts {convert,selfcheck,list} [levels ...] [--thug2 THUG2] [--thps2 THPS2] [--out OUT]
                       [--ns NS] [--no-mips]

  convert     the levels (default all ten: ${LEVELS.map((l) => l.name).join(", ")}) into <thug2>\\${STAGING}
  selfcheck   parse every THPS2 level file, round-trip THUG2's LA / TestLevel files
  list        the levels and their files

  --thug2     THUG2's folder (default C:\\Games\\THUG2) - the install root or its Game folder
  --thps2     THPS2's folder (default C:\\Games\\THPS2) - where ALL.PKR is
  --out       where convert writes (default <thug2>\\${STAGING})
  --ns        NeverScript's compiler (default out\\neverscript\\ns.exe)
  --no-mips   no mip levels (faster)`;

export function parseCommandLine(argv: readonly string[]): Args {
    const { values, positionals } = parseCli(USAGE, {
        args: [...argv],
        allowPositionals: true,
        options: {
            thug2: { type: "string" },
            thps2: { type: "string" },
            out: { type: "string" },
            ns: { type: "string" },
            "no-mips": { type: "boolean" },
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
        thps2: values.thps2 ?? "C:\\Games\\THPS2",
        out: values.out ?? null,
        ns: values.ns ?? common.NS,
        noMips: values["no-mips"] ?? false,
    };
}

export function main(argv: readonly string[] = process.argv.slice(2)): number {
    const args = parseCommandLine(argv);
    if (args.command === "list") return cmdList(args);
    args.thug2 = resolveInstall(args.thug2, "thug2");
    if (args.command === "selfcheck") return cmdSelfcheck(args);
    return cmdConvert(args);
}

if (isMain(import.meta.url)) await runMain(() => main());
