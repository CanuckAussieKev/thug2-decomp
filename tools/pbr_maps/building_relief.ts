// NOT ORIGINAL - building relief ([Graphics] building_relief; docs/design/vulkan.md "Building relief"): the facade
// structure of the level textures drawn on building fronts (facades.ts - windows, frames, sills, bands as rectangles
// at heights), written beside each level texture dictionary as "<dictionary>.relief" (relief_file.ts) for the game to
// build real relief geometry from. From the player's own files; textures without a clear structure get no entry.
//
//   node tools/pbr_maps/building_relief.ts make --game <folder> [--levels bo,be] [--set <name> | --set none]
//        [--out <folder>] [--pictures <folder>] [--force]
//   node tools/pbr_maps/building_relief.ts survey --game <folder> [--levels bo,be] [--set ...] [--samples N] [--json <file>]
//   node tools/pbr_maps/building_relief.ts remove --game <folder>
//
// Which textures: a material's first pass (the texture the walls are drawn with), not a decal, transparent, cut out or
// an environment map; drawn mostly (60% of its area) on walls (faces within 15 degrees of upright); and not mostly on
// skyline cards (below). The structure is found in the texture set's texels where one serves the dictionary (--set:
// the ini's [Graphics] texture_set by default; their 4x texels give the windows' edges more precisely) and in the
// original's otherwise; the entry is keyed to the original's texels (checksum and content hash), as the surface maps
// are, so the game finds it for the original and any set alike. A dictionary whose sources (its bytes, the set's, the
// detector's version) haven't changed since its file was made is skipped (--force: made again).
//
// survey: per level, the building fronts found - the textures, their wall triangles and area, split into the edge of
// the playable area (sectors with collision) and the background (without) - with samples; and separately the skyline
// cards (stage 2, not relieved): flat sectors without collision outside the playable area's bounds (the collision's,
// grown by 500 units) - cut-out pictures of distant buildings.
import { existsSync, mkdirSync, readdirSync, readFileSync, rmSync, statSync, writeFileSync } from "node:fs";
import { dirname, join, relative } from "node:path";
import { readTex, type Texture } from "thug2-level-converter-core/level_port/tex.ts";
import * as col from "thug2-level-converter-core/level_port/thug2_col.ts";
import * as scene from "thug2-level-converter-core/level_port/thug2_scene.ts";
import { exitWith, isMain } from "thug2-level-converter-core/lib/cli.ts";
import { gameRoot } from "../qbjs/game_scripts.ts";
import type { Rgba } from "./classical.ts";
import { detectFacade, type Facade, heightMap } from "./facades.ts";
import { contentHash } from "./pbr_file.ts";
import { DATA_OVERLAY, type Dictionary, dictionaries, textureRgba } from "./pbr_maps.ts";
import { encodePng } from "./png.ts";
import { type ReliefEntry, readRelief, SUFFIX, writeRelief } from "./relief_file.ts";

// The detector's version: raise it when facades.ts finds anything differently (the files made before are made again).
export const DETECTOR_VERSION = 1;

export const WALL_SHARE = 0.6; // of a texture's drawn area on walls
export const WALL_UPRIGHT = 0.26; // |normal.y| under this: within 15 degrees of upright
export const CARD_MARGIN = 500; // units outside the playable area's bounds
const CUT_OUT_ALPHA_REF = 8;
const TEXTURE_SETS = "thug2-decomp-textures";

type Vec = [number, number, number];

// How a level draws one texture as a first pass.
export interface TextureUse {
    area: number;
    wallArea: number;
    wallTriangles: number;
    edgeArea: number; // walls in sectors with collision: the playable area's edge
    backgroundArea: number; // walls in sectors without
    cardArea: number; // on skyline cards
    sectors: Set<number>;
    seeThrough: boolean; // transparent, cut out or a decal somewhere
    centre: Vec; // (the first wall triangle's: a sample's place)
    logU: number; // walls: sum of area x ln(world units per texture width), per height
    logV: number;
    scaleArea: number; // the area counted in them
}

// World units the texture's whole width and height are drawn across on its walls (the area-weighted geometric means),
// or null without usable texture coordinates.
export function worldSize(use: TextureUse | undefined): { u: number; v: number } | null {
    if (!use || use.scaleArea <= 0) return null;
    return { u: Math.exp(use.logU / use.scaleArea), v: Math.exp(use.logV / use.scaleArea) };
}

// A window's size in the world, for it to be one: at least a hand's breadths wide and a forearm tall, at most a
// storefront (THUG2's units are about an inch) - signs' letters, vents, tiles and patterns are smaller or larger.
export const WINDOW_MIN_WIDTH = 16;
export const WINDOW_MIN_HEIGHT = 20;
export const WINDOW_MAX_SIDE = 480;

export function windowsInWorld(facade: Facade, size: { u: number; v: number } | null): string | null {
    if (!size) return "no texture coordinates on its walls";
    const windows = facade.rects.filter((r) => r.kind === "window");
    const widths = windows.map((r) => ((r.u1 - r.u0) / facade.repeatU) * size.u);
    const heights = windows.map((r) => ((r.v1 - r.v0) / facade.repeatV) * size.v);
    if (Math.min(...widths) < WINDOW_MIN_WIDTH || Math.min(...heights) < WINDOW_MIN_HEIGHT) {
        return `windows too small in the world (${Math.round(Math.min(...widths))} x ${Math.round(Math.min(...heights))})`;
    }
    if (Math.max(...widths, ...heights) > WINDOW_MAX_SIDE) return "windows too large in the world";
    return null;
}

export interface SkylineCard {
    sector: number;
    centre: Vec;
    size: Vec;
    triangles: number;
    textures: number[];
}

export interface LevelUses {
    uses: Map<number, TextureUse>;
    cards: SkylineCard[];
    bounds: { lo: Vec; hi: Vec } | null; // the playable area's (the collision's)
}

// The vertex layout of a level mesh (as surfaces.ts reads it): the first texture coordinates after the position, the
// normal when the stride has one, and the colour.
const uvOffset = (mesh: scene.Mesh): number => (mesh.stride >= 28 && (mesh.stride - 16) % 8 === 4 ? 28 : 16);

// World units per texture width and height along a triangle: the lengths of its position's derivatives by u and v
// ([0, 0] when its texture coordinates are degenerate or missing).
function perTexture(
    a: Vec,
    b: Vec,
    c: Vec,
    ta: [number, number] | null,
    tb: [number, number] | null,
    tc: [number, number] | null,
): [number, number] {
    if (!ta || !tb || !tc) return [0, 0];
    const du1 = tb[0] - ta[0];
    const dv1 = tb[1] - ta[1];
    const du2 = tc[0] - ta[0];
    const dv2 = tc[1] - ta[1];
    const det = du1 * dv2 - du2 * dv1;
    if (!(Math.abs(det) > 1e-9) || !Number.isFinite(det)) return [0, 0];
    const e1: Vec = [b[0] - a[0], b[1] - a[1], b[2] - a[2]];
    const e2: Vec = [c[0] - a[0], c[1] - a[1], c[2] - a[2]];
    const dpdu: Vec = [
        (e1[0] * dv2 - e2[0] * dv1) / det,
        (e1[1] * dv2 - e2[1] * dv1) / det,
        (e1[2] * dv2 - e2[2] * dv1) / det,
    ];
    const dpdv: Vec = [
        (e2[0] * du1 - e1[0] * du2) / det,
        (e2[1] * du1 - e1[1] * du2) / det,
        (e2[2] * du1 - e1[2] * du2) / det,
    ];
    return [Math.hypot(...dpdu), Math.hypot(...dpdv)];
}

// How the level (its scene and collision) draws its textures; the skyline cards.
export function levelUses(scnFile: Buffer | null, colFile: Buffer | null): LevelUses {
    const empty: LevelUses = { uses: new Map(), cards: [], bounds: null };
    if (!scnFile) return empty;
    let parsed: scene.Scene;
    try {
        parsed = scene.readScene(scnFile, "scene");
    } catch {
        return empty;
    }
    const collided = new Set<number>();
    let lo: Vec = [Infinity, Infinity, Infinity];
    let hi: Vec = [-Infinity, -Infinity, -Infinity];
    if (colFile) {
        try {
            for (const object of col.readCol(colFile, "col").objects) {
                const solid = object.faces.some(([flags]) => !(flags & col.FACE_NON_COLLIDABLE));
                if (!solid) continue;
                collided.add(object.checksum);
                for (const v of object.verts) {
                    lo = [Math.min(lo[0], v[0]!), Math.min(lo[1], v[1]!), Math.min(lo[2], v[2]!)];
                    hi = [Math.max(hi[0], v[0]!), Math.max(hi[1], v[1]!), Math.max(hi[2], v[2]!)];
                }
            }
        } catch {
            collided.clear();
        }
    }
    const bounds = Number.isFinite(lo[0]) ? { lo, hi } : null;
    const outside = (p: Vec): boolean =>
        !!bounds &&
        [0, 1, 2].some((k) => p[k]! < bounds.lo[k]! - CARD_MARGIN || p[k]! > bounds.hi[k]! + CARD_MARGIN) &&
        // (height alone doesn't take a card out: a tall building over the level is still beside it)
        (p[0] < bounds.lo[0] - CARD_MARGIN ||
            p[0] > bounds.hi[0] + CARD_MARGIN ||
            p[2] < bounds.lo[2] - CARD_MARGIN ||
            p[2] > bounds.hi[2] + CARD_MARGIN);
    const materials = new Map(parsed.materials.map((m) => [m.checksum, m]));
    const uses = new Map<number, TextureUse>();
    const cards: SkylineCard[] = [];
    for (const sector of parsed.sectors) {
        if (sector.flags & (scene.SEC_BILLBOARD | scene.SEC_WEIGHTS)) continue;
        const hasCollision = collided.has(sector.checksum);
        // (a skyline card: no collision, outside the bounds, every triangle in about one plane)
        const triangles: {
            a: Vec;
            b: Vec;
            c: Vec;
            normal: Vec;
            area: number;
            texture: number | undefined;
            perU: number; // world units per texture width / height along the triangle (0: unknown)
            perV: number;
        }[] = [];
        for (const mesh of sector.meshes) {
            const material = materials.get(mesh.material);
            const pass = material?.passes[0];
            const texture = pass?.texture && !(pass.flags & scene.PASS_ENVIRONMENT) ? pass.texture : undefined;
            const buffer = mesh.buffers[0]?.[1];
            const strip = mesh.strips[0] ?? [];
            if (!buffer || mesh.stride < 12) continue;
            const count = Math.min(mesh.vertexCount, Math.floor(buffer.length / mesh.stride));
            const position = (i: number): Vec => {
                const o = i * mesh.stride;
                return [buffer.readFloatLE(o), buffer.readFloatLE(o + 4), buffer.readFloatLE(o + 8)];
            };
            const uvAt = uvOffset(mesh);
            const uv = (i: number): [number, number] | null => {
                if (uvAt + 8 > mesh.stride) return null;
                const o = i * mesh.stride + uvAt;
                return [buffer.readFloatLE(o), buffer.readFloatLE(o + 4)];
            };
            if (texture !== undefined) {
                let use = uses.get(texture);
                if (!use) {
                    use = {
                        area: 0,
                        wallArea: 0,
                        wallTriangles: 0,
                        edgeArea: 0,
                        backgroundArea: 0,
                        cardArea: 0,
                        sectors: new Set(),
                        seeThrough: false,
                        centre: [0, 0, 0],
                        logU: 0,
                        logV: 0,
                        scaleArea: 0,
                    };
                    uses.set(texture, use);
                }
                if (
                    material &&
                    (scene.materialSeeThrough(material, "flag") ||
                        (pass!.flags & (scene.PASS_DECAL | scene.PASS_TRANSPARENT)) !== 0 ||
                        material.alphaCutoff > CUT_OUT_ALPHA_REF)
                ) {
                    use.seeThrough = true;
                }
            }
            for (let j = 2; j < strip.length; j++) {
                const ia = strip[j - 2]!;
                const ib = strip[j - 1]!;
                const ic = strip[j]!;
                if (ia === ib || ib === ic || ia === ic || ia >= count || ib >= count || ic >= count) continue;
                const a = position(ia);
                const b = position(ib);
                const c = position(ic);
                const e1: Vec = [b[0] - a[0], b[1] - a[1], b[2] - a[2]];
                const e2: Vec = [c[0] - a[0], c[1] - a[1], c[2] - a[2]];
                const n: Vec = [
                    e1[1] * e2[2] - e1[2] * e2[1],
                    e1[2] * e2[0] - e1[0] * e2[2],
                    e1[0] * e2[1] - e1[1] * e2[0],
                ];
                const twice = Math.hypot(n[0], n[1], n[2]);
                if (!(twice > 1e-6)) continue;
                const scale = perTexture(a, b, c, uv(ia), uv(ib), uv(ic));
                triangles.push({
                    a,
                    b,
                    c,
                    normal: [n[0] / twice, n[1] / twice, n[2] / twice],
                    area: twice / 2,
                    texture,
                    perU: scale[0],
                    perV: scale[1],
                });
            }
        }
        if (!triangles.length) continue;
        let slo: Vec = [Infinity, Infinity, Infinity];
        let shi: Vec = [-Infinity, -Infinity, -Infinity];
        for (const t of triangles) {
            for (const p of [t.a, t.b, t.c]) {
                slo = [Math.min(slo[0], p[0]), Math.min(slo[1], p[1]), Math.min(slo[2], p[2])];
                shi = [Math.max(shi[0], p[0]), Math.max(shi[1], p[1]), Math.max(shi[2], p[2])];
            }
        }
        const centre: Vec = [(slo[0] + shi[0]) / 2, (slo[1] + shi[1]) / 2, (slo[2] + shi[2]) / 2];
        const first = triangles[0]!.normal;
        const flat = triangles.every(
            (t) => Math.abs(t.normal[0] * first[0] + t.normal[1] * first[1] + t.normal[2] * first[2]) > 0.95,
        );
        const card = !hasCollision && flat && outside(centre);
        if (card) {
            cards.push({
                sector: sector.checksum,
                centre,
                size: [shi[0] - slo[0], shi[1] - slo[1], shi[2] - slo[2]],
                triangles: triangles.length,
                textures: [...new Set(triangles.flatMap((t) => (t.texture === undefined ? [] : [t.texture])))],
            });
        }
        for (const t of triangles) {
            if (t.texture === undefined) continue;
            const use = uses.get(t.texture)!;
            use.area += t.area;
            use.sectors.add(sector.checksum);
            if (Math.abs(t.normal[1]) >= WALL_UPRIGHT) continue;
            if (!use.wallTriangles) {
                use.centre = [
                    (t.a[0] + t.b[0] + t.c[0]) / 3,
                    (t.a[1] + t.b[1] + t.c[1]) / 3,
                    (t.a[2] + t.b[2] + t.c[2]) / 3,
                ];
            }
            use.wallArea += t.area;
            use.wallTriangles++;
            if (t.perU > 0 && t.perV > 0) {
                use.logU += t.area * Math.log(t.perU);
                use.logV += t.area * Math.log(t.perV);
                use.scaleArea += t.area;
            }
            if (card) use.cardArea += t.area;
            else if (hasCollision) use.edgeArea += t.area;
            else use.backgroundArea += t.area;
        }
    }
    return { uses, cards, bounds };
}

// Whether a texture so drawn is a building front's (its structure aside).
export function frontUse(use: TextureUse | undefined): string | null {
    if (!use || use.area <= 0) return "not drawn as a first pass";
    if (use.seeThrough) return "see-through (decal, transparent or cut out)";
    if (use.wallArea < WALL_SHARE * use.area) return "not mostly on walls";
    if (use.cardArea >= 0.5 * use.wallArea) return "mostly on skyline cards";
    return null;
}

// The ini's [Graphics] texture_set (the set the player draws with), or null.
function iniTextureSet(root: string): string | null {
    const ini = join(root, "thug2-decomp.ini");
    if (!existsSync(ini)) return null;
    let section = "";
    for (const raw of readFileSync(ini, "utf8").split(/\r?\n/)) {
        const line = raw.trim();
        const header = /^\[(.+)\]$/.exec(line);
        if (header) section = header[1]!.toLowerCase();
        const pair = /^([^=;#]+)=(.*)$/.exec(line);
        if (section === "graphics" && pair && pair[1]!.trim().toLowerCase() === "texture_set") {
            const value = pair[2]!.trim();
            return value && value !== "none" && value !== "0" ? value : null;
        }
    }
    return null;
}

// The sets to look in, in order: --set's alone (none: null), or the ini's [Graphics] texture_set then the other
// installed sets (thug2-decomp-textures/<set>).
function setsToTry(root: string, option: string | null | undefined): string[] {
    if (option === null) return [];
    if (option !== undefined) return [option];
    const folder = join(root, TEXTURE_SETS);
    const installed = existsSync(folder)
        ? readdirSync(folder).filter((name) => statSync(join(folder, name)).isDirectory())
        : [];
    const first = iniTextureSet(root);
    return [...(first ? [first] : []), ...installed.filter((name) => name !== first).sort()];
}

// The first set's copy of a dictionary (thug2-decomp-textures/<set>/<name>), or null.
function setDictionary(root: string, sets: readonly string[], name: string): { set: string; bytes: Buffer } | null {
    for (const set of sets) {
        const path = join(root, TEXTURE_SETS, set, name);
        if (existsSync(path)) return { set, bytes: readFileSync(path) };
    }
    return null;
}

const fnv = (parts: readonly Uint8Array[]): bigint => {
    let hash = 0xcbf29ce484222325n;
    for (const part of parts) hash = ((hash ^ contentHash(part)) * 0x100000001b3n) & 0xffffffffffffffffn;
    return hash;
};

interface Found {
    texture: Texture;
    image: Rgba; // what the structure was found in (the set's texels or the original's)
    fromSet: boolean;
    facade: Facade | null;
    why: string;
}

// The dictionary's textures with their structure found (those drawn as building fronts; the rest left out).
function findFronts(dictionary: Dictionary, setBytes: Buffer | null, uses: Map<number, TextureUse>): Found[] {
    let textures: Texture[];
    try {
        textures = readTex(dictionary.tex(), dictionary.name).textures;
    } catch {
        return [];
    }
    const setTextures = new Map<number, Texture>();
    if (setBytes) {
        try {
            for (const t of readTex(setBytes, dictionary.name).textures) setTextures.set(t.checksum, t);
        } catch {
            setTextures.clear();
        }
    }
    const out: Found[] = [];
    const seen = new Set<number>();
    for (const texture of textures) {
        if (seen.has(texture.checksum)) continue;
        seen.add(texture.checksum);
        if (frontUse(uses.get(texture.checksum))) continue;
        const upscaled = setTextures.get(texture.checksum);
        const image = (upscaled && textureRgba(upscaled)) || textureRgba(texture);
        if (!image) continue;
        const fromSet = !!upscaled && image.width !== texture.width;
        const detection = detectFacade(image);
        const inWorld = detection.facade
            ? windowsInWorld(detection.facade, worldSize(uses.get(texture.checksum)))
            : null;
        out.push({
            texture,
            image,
            fromSet,
            facade: detection.facade && !inWorld ? detection.facade : null,
            why: detection.facade ? (inWorld ?? detection.facade.why) : detection.skip,
        });
    }
    return out;
}

export interface MakeOptions {
    levels?: string[] | undefined;
    set?: string | null | undefined; // undefined: the ini's
    out?: string | undefined;
    pictures?: string | undefined;
    force?: boolean;
    log?: (line: string) => void;
}

export interface MakeResult {
    made: string[];
    skipped: string[]; // unchanged since they were made
    entries: number;
    looked: number;
}

// A picture: the texture | its height map (grey: the wall; dark: into it; light: out of it).
function picture(found: Found): { width: number; height: number; rgba: Uint8Array } {
    const { image } = found;
    const heights = found.facade ? heightMap(found.facade, image.width, image.height, 0.05) : null;
    const width = image.width * 2;
    const rgba = new Uint8Array(width * image.height * 4);
    for (let y = 0; y < image.height; y++) {
        for (let x = 0; x < image.width; x++) {
            const s = (y * image.width + x) * 4;
            const o = (y * width + x) * 4;
            rgba.set([image.data[s]!, image.data[s + 1]!, image.data[s + 2]!, 255], o);
            const g = heights ? heights[y * image.width + x]! : 128;
            rgba.set([g, g, heights ? g : 255, 255], o + image.width * 4);
        }
    }
    return { width, height: image.height, rgba };
}

export function make(game: string, options: MakeOptions): MakeResult {
    const root = gameRoot(game);
    const log = options.log ?? (() => {});
    const sets = setsToTry(root, options.set);
    const result: MakeResult = { made: [], skipped: [], entries: 0, looked: 0 };
    for (const dictionary of dictionaries(root, options.levels)) {
        const out = join(options.out ?? root, dictionary.folder, `${dictionary.name}${SUFFIX}`);
        const tex = dictionary.tex();
        const fromSet = setDictionary(root, sets, dictionary.name);
        const setBytes = fromSet?.bytes ?? null;
        const sources = fnv([tex, setBytes ?? new Uint8Array(), Uint8Array.of(DETECTOR_VERSION)]);
        if (!options.force && existsSync(out)) {
            try {
                const before = readRelief(readFileSync(out));
                if (before.sources === sources && before.detector === DETECTOR_VERSION) {
                    result.skipped.push(relative(options.out ?? root, out));
                    continue;
                }
            } catch {
                // (not ours or damaged: made again)
            }
        }
        const { uses } = levelUses(dictionary.scn(), dictionary.col());
        const found = findFronts(dictionary, setBytes, uses);
        result.looked += found.length;
        const entries: ReliefEntry[] = found
            .filter((f) => f.facade)
            .map((f) => ({
                checksum: f.texture.checksum,
                contentHash: contentHash(f.texture.levels[0]!),
                facade: f.facade!,
            }));
        if (options.pictures) {
            const folder = join(options.pictures, dictionary.name.split("/")[1] ?? "level");
            mkdirSync(folder, { recursive: true });
            for (const f of found) {
                const p = picture(f);
                const key = f.texture.checksum.toString(16).padStart(8, "0");
                writeFileSync(
                    join(folder, `${f.facade ? "front" : "flat"}_${key}.png`),
                    encodePng(p.width, p.height, p.rgba),
                );
            }
        }
        if (existsSync(out)) rmSync(out);
        if (!entries.length) continue;
        mkdirSync(dirname(out), { recursive: true });
        writeFileSync(out, writeRelief({ detector: DETECTOR_VERSION, sources, entries }));
        result.made.push(relative(options.out ?? root, out));
        result.entries += entries.length;
        log(
            `${dictionary.name}: ${entries.length} building fronts of ${found.length} wall textures${fromSet ? ` (${fromSet.set}'s texels)` : ""}`,
        );
    }
    return result;
}

export interface SurveyLevel {
    level: string;
    fronts: {
        texture: string;
        windows: number;
        wallTriangles: number;
        wallArea: number;
        edgeArea: number;
        backgroundArea: number;
        at: Vec;
        why: string;
        fromSet: boolean;
    }[];
    flat: number; // wall textures without a structure
    cards: SkylineCard[];
}

export function survey(
    game: string,
    options: { levels?: string[] | undefined; set?: string | null | undefined },
): SurveyLevel[] {
    const root = gameRoot(game);
    const sets = setsToTry(root, options.set);
    const out: SurveyLevel[] = [];
    for (const dictionary of dictionaries(root, options.levels)) {
        const { uses, cards } = levelUses(dictionary.scn(), dictionary.col());
        const found = findFronts(dictionary, setDictionary(root, sets, dictionary.name)?.bytes ?? null, uses);
        const fronts = found
            .filter((f) => f.facade)
            .map((f) => {
                const use = uses.get(f.texture.checksum)!;
                return {
                    texture: f.texture.checksum.toString(16).padStart(8, "0"),
                    windows: f.facade!.windows,
                    wallTriangles: use.wallTriangles,
                    wallArea: Math.round(use.wallArea),
                    edgeArea: Math.round(use.edgeArea),
                    backgroundArea: Math.round(use.backgroundArea),
                    at: use.centre.map((v) => Math.round(v)) as Vec,
                    why: f.why,
                    fromSet: f.fromSet,
                };
            })
            .sort((a, b) => b.wallArea - a.wallArea);
        out.push({ level: dictionary.name, fronts, flat: found.length - fronts.length, cards });
    }
    return out;
}

// Every building-relief file under the levels (Data's and the overlay's) removed (the magic checked). Returns how many.
export function remove(game: string): number {
    const root = gameRoot(game);
    const walk = (path: string): string[] =>
        !existsSync(path)
            ? []
            : statSync(path).isDirectory()
              ? readdirSync(path).flatMap((name) => walk(join(path, name)))
              : [path];
    let removed = 0;
    for (const path of [...walk(join(root, "Data", "levels")), ...walk(join(root, DATA_OVERLAY, "levels"))]) {
        if (!path.toLowerCase().endsWith(`.tex.xbx${SUFFIX}`)) continue;
        try {
            readRelief(readFileSync(path));
        } catch {
            continue;
        }
        rmSync(path, { force: true });
        removed++;
    }
    return removed;
}

function argValue(args: string[], name: string): string | undefined {
    const at = args.indexOf(name);
    return at >= 0 ? args[at + 1] : undefined;
}

export function main(args: string[]): void {
    const [command] = args;
    const game = argValue(args, "--game");
    if (!game || !["make", "survey", "remove"].includes(command ?? "")) {
        exitWith(
            "Usage: node tools/pbr_maps/building_relief.ts make --game <folder> [--levels a,b] [--set <name> | none] " +
                "[--out <folder>] [--pictures <folder>] [--force]\n" +
                "       node tools/pbr_maps/building_relief.ts survey --game <folder> [--levels a,b] [--set ...] [--samples N] [--json <file>]\n" +
                "       node tools/pbr_maps/building_relief.ts remove --game <folder>",
        );
    }
    const levels = argValue(args, "--levels")?.split(",").filter(Boolean);
    const setArg = argValue(args, "--set");
    const set = setArg === undefined ? undefined : setArg === "none" ? null : setArg;
    if (command === "remove") {
        console.log(`removed ${remove(game)} building-relief files`);
        return;
    }
    if (command === "survey") {
        const samples = Number(argValue(args, "--samples") ?? 5);
        const levelsFound = survey(game, { levels, set });
        const json = argValue(args, "--json");
        if (json) writeFileSync(json, `${JSON.stringify(levelsFound, (_, v) => (v instanceof Set ? [...v] : v), 1)}\n`);
        let fronts = 0;
        let triangles = 0;
        let cards = 0;
        for (const level of levelsFound) {
            if (!level.fronts.length && !level.cards.length) continue;
            const tris = level.fronts.reduce((s, f) => s + f.wallTriangles, 0);
            const edge = level.fronts.reduce((s, f) => s + f.edgeArea, 0);
            const background = level.fronts.reduce((s, f) => s + f.backgroundArea, 0);
            fronts += level.fronts.length;
            triangles += tris;
            cards += level.cards.length;
            console.log(
                `${level.level}: ${level.fronts.length} facade textures (${level.flat} wall textures flat), ${tris} wall triangles, ` +
                    `area ${Math.round(edge)} at the playable edge / ${Math.round(background)} background; ${level.cards.length} skyline cards`,
            );
            for (const f of level.fronts.slice(0, samples)) {
                console.log(
                    `    ${f.texture} ${f.wallTriangles} tris, area ${f.wallArea} at ${f.at.join(",")}: ${f.why}${f.fromSet ? " (set texels)" : ""}`,
                );
            }
        }
        console.log(`all: ${fronts} facade textures, ${triangles} wall triangles; ${cards} skyline cards (stage 2)`);
        return;
    }
    const result = make(game, {
        levels,
        set,
        out: argValue(args, "--out"),
        pictures: argValue(args, "--pictures"),
        force: args.includes("--force"),
        log: (line) => console.log(line),
    });
    console.log(
        `${result.entries} building fronts in ${result.made.length} files (${result.looked} wall textures looked at; ` +
            `${result.skipped.length} dictionaries unchanged)`,
    );
}

if (isMain(import.meta.url)) main(process.argv.slice(2));
