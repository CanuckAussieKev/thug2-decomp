// NOT ORIGINAL - surface maps (normal, roughness, height) made from the player's own textures for the Vulkan renderer's
// per-pixel lighting ([Graphics] rt_pbr; docs/design/vulkan.md "Per-pixel lighting and surface maps"). For each level
// texture dictionary (Data\levels\<x>\<x>.tex.xbx, the skies left out) a file beside it - the dictionary's name +
// ".pbr" (src/formats/surface_map_format.h) - holds a map per texture worth one: each texture is classified from its
// level's materials and its pixels (classes.ts: text, decals, UI, overlays and atlases skipped), its maps made by the
// generator, the roughness clamped into the class's range. The game matches a map to its texture by checksum and the
// texture's content hash, so a texture changed later (an upscaled one, a mod's) keeps no stale map - make them again
// after replacing textures.
//
//   node tools/pbr_maps/pbr_maps.ts make --game <folder> [--generator classical|chord] [--levels bo,au,foun]
//        [--pictures <folder>] [--samples N] [--chord <folder>] [--out <folder>]
//   node tools/pbr_maps/pbr_maps.ts remove --game <folder>      every .pbr made removed
//
// --generator: classical (the default; classical.ts) or chord (Ubisoft La Forge's CHORD model, chord.ts: research-only
// weights the player fetches and runs locally - textures it does badly on fall back to classical). --pictures: for
// N textures a dictionary (default 6), a picture side by side - the texture, its normals, roughness, height, and the
// texture lit flat and with the normals from the upper left. --out: the files under that folder (the game's layout),
// not beside the dictionaries - the game folder only read. unpack_game.ts --pbr-maps runs `make` and records the
// files for --restore.
import { existsSync, mkdirSync, readdirSync, readFileSync, rmSync, statSync, writeFileSync } from "node:fs";
import { dirname, join, relative } from "node:path";
import { decode } from "thug2-level-converter-core/level_port/dxt.ts";
import * as prx from "thug2-level-converter-core/level_port/prx.ts";
import { readTex, type Texture } from "thug2-level-converter-core/level_port/tex.ts";
import * as scene from "thug2-level-converter-core/level_port/thug2_scene.ts";
import { exitWith, isMain } from "thug2-level-converter-core/lib/cli.ts";
import { gameRoot } from "../qbjs/game_scripts.ts";
import { CLASS, type ClassName, classify, RULES, type Stats, type Usage } from "./classes.ts";
import { classicalMaps, type Rgba, scaleNormals } from "./classical.ts";
import {
    contentHash,
    GENERATOR_CHORD,
    GENERATOR_CLASSICAL,
    type MapEntry,
    readPbr,
    SUFFIX,
    writePbr,
} from "./pbr_file.ts";
import { encodePng } from "./png.ts";
import { bumpFor, surfacesOf } from "./surfaces.ts";

export type GeneratorName = "classical" | "chord";
// A model generator: the textures' maps (RGBA as pbr_file's texels), or null for those it can't do (classical then).
export type ModelGenerator = (
    images: Map<string, Rgba>,
    names: Map<string, ClassName>,
) => Map<string, Uint8Array | null>;

export interface MakeOptions {
    generator: GeneratorName;
    levels?: string[] | undefined; // lower-case level folder names (all when absent)
    pictures?: string | undefined;
    samples?: number;
    model?: ModelGenerator | undefined; // (chord: chord.ts's runner)
    out?: string | undefined; // the files written under this folder (the game's layout) instead of beside the dictionaries
    log?: (line: string) => void;
}

export interface MakeResult {
    made: string[]; // the .pbr files, relative to the game's root
    maps: number;
    skipped: Map<string, number>; // why, how many
    classes: Map<ClassName, number>;
}

const walk = (path: string): string[] =>
    !existsSync(path)
        ? []
        : statSync(path).isDirectory()
          ? readdirSync(path).flatMap((name) => walk(join(path, name)))
          : [path];

// A level dictionary as the game reads it: its name under Data ("levels/bo/bo.tex.xbx", lower case, / between parts),
// its bytes, its scene's (<x>.scn.xbx beside it; null: none) and its collision's (<x>.col.xbx: the terrains).
export interface Dictionary {
    name: string;
    tex: () => Buffer;
    scn: () => Buffer | null;
    col: () => Buffer | null;
    folder: string; // where its maps go, relative to the game's root: "Data" or the overlay's folder
}

const levelOf = (name: string): string => name.split("/")[1] ?? "";

// The data overlay the game reads before Data (src/engine/file/data_overlay.cpp: thug2-decomp-data - the ported levels,
// the new front end's files); the converters' staging folders beside it aren't read.
export const DATA_OVERLAY = "thug2-decomp-data";
const dataOverlays = (root: string): string[] => (existsSync(join(root, DATA_OVERLAY)) ? [DATA_OVERLAY] : []);

// The level dictionaries (levels/<x>/*.tex.xbx, the skies' left out): loose under Data\levels, in the archives of
// Data\pre, and in the data overlay (thug2-decomp-data: the ported levels) - the game reads the overlay's copy
// first, then its loaded archives', then Data's; the maps go beside the copy it reads (an archive's: in Data).
export function dictionaries(root: string, levels?: string[]): Dictionary[] {
    const wanted = levels?.map((l) => l.toLowerCase());
    const keep = (name: string): boolean =>
        name.startsWith("levels/") &&
        name.endsWith(".tex.xbx") &&
        !levelOf(name).endsWith("_sky") &&
        (!wanted || wanted.includes(levelOf(name)));
    const found = new Map<string, Dictionary>();
    const loose = new Map<string, string>();
    for (const path of walk(join(root, "Data", "levels"))) {
        loose.set(relative(join(root, "Data"), path).replaceAll("\\", "/").toLowerCase(), path);
    }
    const beside = (name: string, kind: "scn" | "col"): string => name.replace(/\.tex\.xbx$/, `.${kind}.xbx`);
    const read = (path: string | undefined): Buffer | null => (path ? readFileSync(path) : null);
    for (const [name, path] of loose) {
        if (!keep(name)) continue;
        found.set(name, {
            name,
            tex: () => readFileSync(path),
            scn: () => read(loose.get(beside(name, "scn"))),
            col: () => read(loose.get(beside(name, "col"))),
            folder: "Data",
        });
    }
    const pre = join(root, "Data", "pre");
    const archives = existsSync(pre) ? readdirSync(pre).filter((f) => f.toLowerCase().endsWith(".prx")) : [];
    for (const archive of archives) {
        const inside = new Map<string, Buffer>();
        try {
            for (const [entry, data] of prx.entries(join(pre, archive), (n) => /\.(tex|scn|col)\.xbx$/i.test(n))) {
                inside.set(entry.replaceAll("\\", "/").toLowerCase(), data);
            }
        } catch {
            continue;
        }
        for (const [name, data] of inside) {
            if (!keep(name)) continue;
            const scn = inside.get(beside(name, "scn"));
            const col = inside.get(beside(name, "col"));
            found.set(name, {
                name,
                tex: () => data,
                scn: () => scn ?? read(loose.get(beside(name, "scn"))),
                col: () => col ?? read(loose.get(beside(name, "col"))),
                folder: "Data",
            });
        }
    }
    for (const overlay of dataOverlays(root)) {
        const levelsFolder = join(root, overlay, "levels");
        const files = new Map<string, string>();
        for (const path of walk(levelsFolder)) {
            files.set(relative(join(root, overlay), path).replaceAll("\\", "/").toLowerCase(), path);
        }
        for (const [name, path] of files) {
            if (!keep(name)) continue;
            found.set(name, {
                name,
                tex: () => readFileSync(path),
                scn: () => read(files.get(beside(name, "scn"))),
                col: () => read(files.get(beside(name, "col"))),
                folder: overlay,
            });
        }
    }
    return [...found.values()].sort((a, b) => a.name.localeCompare(b.name));
}

// The first level's texels as RGBA (null: a format not read - palettes).
export function textureRgba(texture: Texture): Rgba | null {
    const level = texture.levels[0];
    if (!level || texture.palette.length) return null;
    const width = texture.width;
    const height = Math.max(4, texture.height);
    let bgra: Uint8Array;
    if (texture.dxt === 0 && texture.texelDepth === 32) {
        if (level.length < width * height * 4) return null;
        bgra = level;
    } else if (texture.dxt === 1 || texture.dxt === 2 || texture.dxt === 5) {
        try {
            bgra = decode(level, width, height, texture.dxt === 5 ? 5 : 1);
        } catch {
            return null;
        }
    } else return null;
    const data = new Uint8Array(width * height * 4);
    for (let i = 0; i < width * height; i++) {
        data[i * 4] = bgra[i * 4 + 2]!;
        data[i * 4 + 1] = bgra[i * 4 + 1]!;
        data[i * 4 + 2] = bgra[i * 4]!;
        data[i * 4 + 3] = bgra[i * 4 + 3]!;
    }
    return { width, height, data };
}

// How the dictionary's level uses each texture (its scene: <x>.scn.xbx for <x>.tex.xbx).
export function usageOf(scn: Buffer | null): Map<number, Usage> {
    const usage = new Map<number, Usage>();
    if (!scn) return usage;
    let parsed: scene.Scene;
    try {
        parsed = scene.readScene(scn, "scene");
    } catch {
        return usage;
    }
    const of = (checksum: number): Usage => {
        let u = usage.get(checksum);
        if (!u) {
            u = { base: false, overlay: false, environment: false, shiny: false, decal: false, transparent: false };
            usage.set(checksum, u);
        }
        return u;
    };
    for (const material of parsed.materials) {
        const shiny = material.passes.some((pass) => (pass.flags & scene.PASS_ENVIRONMENT) !== 0);
        material.passes.forEach((pass, index) => {
            if (!pass.texture) return;
            const u = of(pass.texture);
            if ((pass.flags & scene.PASS_ENVIRONMENT) !== 0) u.environment = true;
            else if (index === 0) u.base = true;
            else u.overlay = true;
            if (index === 0 && shiny) u.shiny = true;
            if ((pass.flags & scene.PASS_DECAL) !== 0) u.decal = true;
            if ((pass.flags & scene.PASS_TRANSPARENT) !== 0) u.transparent = true;
        });
    }
    return usage;
}

function hueOf(r: number, g: number, b: number): number {
    const max = Math.max(r, g, b);
    const min = Math.min(r, g, b);
    if (max - min < 1e-3) return -1;
    let h: number;
    if (max === r) h = ((g - b) / (max - min)) % 6;
    else if (max === g) h = (b - r) / (max - min) + 2;
    else h = (r - g) / (max - min) + 4;
    return (h * 60 + 360) % 360;
}

export function statsOf(image: Rgba): Stats {
    const n = image.width * image.height;
    let cutout = 0;
    let translucent = 0;
    let lumaSum = 0;
    let satSum = 0;
    let r = 0;
    let g = 0;
    let b = 0;
    const colours = new Set<number>();
    const luma = new Float32Array(n);
    // The game's passes double the texture's alpha (2 x vertex alpha x texel alpha: 128 is opaque) unless they take it
    // alone; a texture whose alpha never goes past ~half is read the doubled way.
    let alphaMax = 0;
    for (let i = 0; i < n; i++) alphaMax = Math.max(alphaMax, image.data[i * 4 + 3]!);
    const opaque = alphaMax >= 200 ? 255 : 128;
    for (let i = 0; i < n; i++) {
        const R = image.data[i * 4]! / 255;
        const G = image.data[i * 4 + 1]! / 255;
        const B = image.data[i * 4 + 2]! / 255;
        if (image.data[i * 4 + 3]! < 0.5 * opaque) cutout++;
        if (image.data[i * 4 + 3]! < 0.9 * opaque) translucent++;
        luma[i] = 0.2126 * R + 0.7152 * G + 0.0722 * B;
        lumaSum += luma[i]!;
        const max = Math.max(R, G, B);
        satSum += max > 0 ? (max - Math.min(R, G, B)) / max : 0;
        r += R;
        g += G;
        b += B;
        colours.add(
            (image.data[i * 4]! >> 3) | ((image.data[i * 4 + 1]! >> 3) << 5) | ((image.data[i * 4 + 2]! >> 3) << 10),
        );
    }
    let detail = 0;
    let ex = 0;
    let ey = 0;
    const w = image.width;
    const h = image.height;
    for (let y = 0; y < h; y++) {
        for (let x = 0; x < w; x++) {
            const c = luma[y * w + x]!;
            const right = luma[y * w + ((x + 1) % w)]!;
            const down = luma[((y + 1) % h) * w + x]!;
            const left = luma[y * w + ((x + w - 1) % w)]!;
            const up = luma[((y + h - 1) % h) * w + x]!;
            detail += Math.abs(c - (right + down + left + up) / 4);
            ex += (right - c) ** 2;
            ey += (down - c) ** 2;
        }
    }
    return {
        width: w,
        height: h,
        cutout: cutout / n,
        translucent: translucent / n,
        meanLuma: lumaSum / n,
        saturation: satSum / n,
        hue: hueOf(r / n, g / n, b / n),
        detail: detail / n,
        colours: colours.size,
        streaks: Math.abs(Math.log((ex + 1e-6) / (ey + 1e-6))),
    };
}

// A side-by-side picture: texture | normals | roughness | height | lit flat | lit with the normals (light from the
// upper left, 45 degrees up), each scaled (nearest) to `size` texels tall.
export function sideBySide(
    image: Rgba,
    maps: Uint8Array,
    size = 192,
): { width: number; height: number; rgba: Uint8Array } {
    const scale = size / image.height;
    const cellW = Math.max(1, Math.round(image.width * scale));
    const panels = 6;
    const width = cellW * panels;
    const out = new Uint8Array(width * size * 4);
    const light = [-0.5, -0.5, Math.SQRT1_2];
    for (let y = 0; y < size; y++) {
        const sy = Math.min(image.height - 1, Math.floor(y / scale));
        for (let x = 0; x < cellW; x++) {
            const sx = Math.min(image.width - 1, Math.floor(x / scale));
            const i = (sy * image.width + sx) * 4;
            const nx = (maps[i]! - 127.5) / 127.5;
            const ny = (maps[i + 1]! - 127.5) / 127.5;
            const nz = Math.sqrt(Math.max(0, 1 - nx * nx - ny * ny));
            const lit = Math.max(0, nx * light[0]! + ny * light[1]! + nz * light[2]!);
            const flat = light[2]!;
            const colour = [image.data[i]!, image.data[i + 1]!, image.data[i + 2]!];
            const cells: number[][] = [
                colour,
                [maps[i]!, maps[i + 1]!, Math.round(nz * 255)],
                [maps[i + 2]!, maps[i + 2]!, maps[i + 2]!],
                [maps[i + 3]!, maps[i + 3]!, maps[i + 3]!],
                colour.map((c) => Math.min(255, Math.round(c * (0.45 + 0.55 * flat)))),
                colour.map((c) => Math.min(255, Math.round(c * (0.45 + 0.55 * lit)))),
            ];
            cells.forEach((rgb, panel) => {
                const o = (y * width + panel * cellW + x) * 4;
                out[o] = rgb[0]!;
                out[o + 1] = rgb[1]!;
                out[o + 2] = rgb[2]!;
                out[o + 3] = 255;
            });
        }
    }
    return { width, height: size, rgba: out };
}

export function make(game: string, options: MakeOptions): MakeResult {
    const root = gameRoot(game);
    const log = options.log ?? (() => {});
    const result: MakeResult = { made: [], maps: 0, skipped: new Map(), classes: new Map() };
    const skip = (why: string) => result.skipped.set(why, (result.skipped.get(why) ?? 0) + 1);
    for (const dictionary of dictionaries(root, options.levels)) {
        let textures: Texture[];
        try {
            textures = readTex(dictionary.tex(), dictionary.name).textures;
        } catch (error) {
            log(`${dictionary.name}: not read (${(error as Error).message})`);
            continue;
        }
        const usage = usageOf(dictionary.scn());
        const images = new Map<string, Rgba>();
        const names = new Map<string, ClassName>();
        const keyed = new Map<string, Texture>();
        for (const texture of textures) {
            const key = texture.checksum.toString(16).padStart(8, "0");
            if (keyed.has(key)) continue;
            const image = textureRgba(texture);
            if (!image) {
                skip("a format not read");
                continue;
            }
            const used = usage.get(texture.checksum) ?? {
                base: true,
                overlay: false,
                environment: false,
                shiny: false,
                decal: false,
                transparent: false,
            };
            const verdict = classify(statsOf(image), used);
            if (verdict.skip) {
                skip(verdict.skip);
                continue;
            }
            images.set(key, image);
            names.set(key, verdict.name);
            keyed.set(key, texture);
        }
        const modelled = options.generator === "chord" && options.model ? options.model(images, names) : new Map();
        const entries: MapEntry[] = [];
        const surfaces = surfacesOf(dictionary.scn(), dictionary.col());
        const areaOf = (key: string): number => surfaces.get(keyed.get(key)!.checksum)?.area ?? 0;
        // (the pictures: the textures covering the most of the level first)
        const pictured = new Set(
            options.pictures
                ? [...images]
                      .filter(([, image]) => image.width >= 64)
                      .map(([key]) => key)
                      .sort((a, b) => areaOf(b) - areaOf(a))
                      .slice(0, options.samples ?? 6)
                : [],
        );
        let bumps = 0;
        for (const [key, image] of images) {
            const texture = keyed.get(key)!;
            const name = names.get(key)!;
            const rule = RULES[name];
            const bump = bumpFor(rule, surfaces.get(texture.checksum), image.width, image.height);
            bumps += bump.bump;
            const fromModel = modelled.get(key);
            const texels = fromModel
                ? scaleNormals(fromModel, rule.bump > 0 ? bump.bump / rule.bump : 1)
                : classicalMaps(image, rule, bump.bump);
            entries.push({
                checksum: texture.checksum,
                contentHash: contentHash(texture.levels[0]!),
                width: image.width,
                height: image.height,
                materialClass: CLASS[name],
                generator: fromModel ? GENERATOR_CHORD : GENERATOR_CLASSICAL,
                f0: rule.f0,
                texels,
            });
            result.classes.set(name, (result.classes.get(name) ?? 0) + 1);
            if (options.pictures && pictured.has(key)) {
                const picture = sideBySide(image, texels);
                const folder = join(options.pictures, levelOf(dictionary.name));
                mkdirSync(folder, { recursive: true });
                const evidence = bump.why.replace(/[^a-z0-9.+%]+/gi, "_");
                writeFileSync(
                    join(folder, `${key}_${name}_bump${bump.bump.toFixed(2)}_${evidence}.png`),
                    encodePng(picture.width, picture.height, picture.rgba),
                );
            }
        }
        if (!entries.length) continue;
        const out = join(options.out ?? root, dictionary.folder, `${dictionary.name}${SUFFIX}`);
        mkdirSync(dirname(out), { recursive: true });
        writeFileSync(out, writePbr(entries));
        result.made.push(options.out ? out : relative(root, out));
        result.maps += entries.length;
        log(
            `${relative(root, out)}: ${entries.length} maps of ${textures.length} textures, ` +
                `mean bump ${(bumps / entries.length).toFixed(2)}`,
        );
    }
    return result;
}

// Every surface-map file under Data\levels and the overlays' levels removed (only files that are ours: the magic checked). Returns how many.
export function remove(game: string): number {
    const root = gameRoot(game);
    let removed = 0;
    const folders = [join(root, "Data", "levels"), ...dataOverlays(root).map((o) => join(root, o, "levels"))];
    for (const path of folders.flatMap(walk)) {
        if (!path.toLowerCase().endsWith(`.tex.xbx${SUFFIX}`)) continue;
        try {
            readPbr(readFileSync(path));
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

export async function main(args: string[]): Promise<void> {
    const [command] = args;
    const game = argValue(args, "--game");
    if (!game || (command !== "make" && command !== "remove")) {
        exitWith(
            "Usage: node tools/pbr_maps/pbr_maps.ts make --game <folder> [--generator classical|chord] [--levels a,b] " +
                "[--pictures <folder>] [--samples N] [--chord <folder>]\n" +
                "       node tools/pbr_maps/pbr_maps.ts remove --game <folder>",
        );
    }
    if (command === "remove") {
        console.log(`removed ${remove(game)} surface-map files`);
        return;
    }
    const generator = (argValue(args, "--generator") ?? "classical") as GeneratorName;
    if (generator !== "classical" && generator !== "chord") exitWith(`--generator ${generator}: classical or chord`);
    let model: ModelGenerator | undefined;
    if (generator === "chord") {
        const { chordGenerator } = await import("./chord.ts");
        model = chordGenerator(argValue(args, "--chord"));
    }
    const levels = argValue(args, "--levels")?.split(",").filter(Boolean);
    const result = make(game, {
        generator,
        levels,
        pictures: argValue(args, "--pictures"),
        samples: Number(argValue(args, "--samples") ?? 6),
        model,
        out: argValue(args, "--out"),
        log: (line) => console.log(line),
    });
    console.log(`${result.maps} maps in ${result.made.length} files`);
    console.log(`classes: ${[...result.classes].map(([k, v]) => `${k} ${v}`).join(", ")}`);
    console.log(`skipped: ${[...result.skipped].map(([k, v]) => `${k} ${v}`).join(", ") || "none"}`);
}

if (isMain(import.meta.url)) await main(process.argv.slice(2));
