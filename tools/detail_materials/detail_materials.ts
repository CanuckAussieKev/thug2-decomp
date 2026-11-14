// NOT ORIGINAL - the detail-material library for the Vulkan renderer's detail layer ([Graphics] rt_detail;
// src/gpu_vulkan/vulkan_detail.h, docs/design/vulkan.md "Detail materials and de-lighting"): one CC0 material scan
// per surface kind (materials.json: the sources, their MD5s and sizes, their licences), fetched to this machine and
// packed with the player's own levels into thug2-decomp-materials.t2dm in the game folder (src/formats/
// detail_material_format.h). Nothing of the scans is committed or shipped.
//
//   node tools/detail_materials/detail_materials.ts list                      the kinds, their scans and licences
//   node tools/detail_materials/detail_materials.ts fetch [--cache <folder>]  the scans downloaded (checked by MD5)
//   node tools/detail_materials/detail_materials.ts pack --game <folder> [--cache <folder>] [--side 256|512|1024|2048]
//        [--levels bo,au] [--out <file>] [--report <file.tsv>]
//   node tools/detail_materials/detail_materials.ts remove --game <folder>   the library removed from the game folder
//
// fetch: into --cache (default out\detail_materials in the repository), one folder per scan; a file already there with
// the right MD5 isn't downloaded again. pack: each kind's detail texture made from its scan (detail_texels.ts: the
// scan's grain - normal, roughness, the albedo's luminance - high-passed, --side texels square, default 1024), and
// each texture of the levels' dictionaries that gets a surface map (tools/pbr_maps classes.ts's rules: the same
// classification) assigned a kind - from the terrain types of the collision faces drawn with it where they say
// (texture_terrain.ts, kinds.ts decideKind), else its class -, its world size (the detail's tiles per unit of its
// texture coordinates) and its mean luminance (the de-lighting's target). --report: one line per texture (the level,
// checksum, class, kind, evidence, terrain areas). The file goes into the game folder (or --out); the game reads it
// when rt_detail is on.
import { createHash } from "node:crypto";
import { existsSync, mkdirSync, readdirSync, readFileSync, rmSync, statSync, writeFileSync } from "node:fs";
import { dirname, join, relative, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import * as prx from "thug2-level-converter-core/level_port/prx.ts";
import { readTex, type Texture } from "thug2-level-converter-core/level_port/tex.ts";
import { exitWith, isMain } from "thug2-level-converter-core/lib/cli.ts";
import { type ClassName, classify } from "../pbr_maps/classes.ts";
import { contentHash } from "../pbr_maps/pbr_file.ts";
import { DATA_OVERLAY, dictionaries, statsOf, textureRgba, usageOf } from "../pbr_maps/pbr_maps.ts";
import { gameRoot } from "../qbjs/game_scripts.ts";
import { type Assignment, type KindEntry, LIBRARY_FILE, writeLibrary } from "./detail_file.ts";
import { detailTexels, LUMA } from "./detail_texels.ts";
import { decideKind, KIND, type KindName } from "./kinds.ts";
import { readPng } from "./png_read.ts";
import { metresPerUv, type TextureUse, textureUses } from "./texture_terrain.ts";

const HERE = dirname(fileURLToPath(import.meta.url));
export const REPO = resolve(HERE, "..", "..");
export const DEFAULT_CACHE = join(REPO, "out", "detail_materials");

export interface ScanFile {
    url: string;
    md5: string;
    bytes: number;
}
export interface Material {
    kind: KindName;
    source: string;
    asset: string;
    name: string;
    authors: string[];
    page: string;
    licence: string;
    licenceUrl: string;
    tileMetres: number;
    roughness: number;
    roughnessSwing: number;
    f0: number;
    metalness: number;
    roughnessWeight: number;
    files: { colour: ScanFile; normal: ScanFile; roughness: ScanFile };
    // The kind whose scan gives this one its grain (its tile size with it): for scans with a large pattern of their own -
    // tile grids, tread plate, mortar - that the sixteenth-of-a-tile filter keeps (sharp lines are fine detail) and
    // that would print a second grid over the game's texture. The kind keeps its own roughness, F0 and metalness.
    grainFrom?: KindName;
}

export function materials(): Material[] {
    const list = JSON.parse(readFileSync(join(HERE, "materials.json"), "utf8")) as { materials: Material[] };
    for (const m of list.materials) {
        if (!(m.kind in KIND) || m.kind === "none") throw new Error(`materials.json: ${m.asset}: kind ${m.kind}`);
        if (!m.licence.startsWith("CC0")) throw new Error(`materials.json: ${m.asset}: ${m.licence} - only CC0 scans`);
    }
    return list.materials;
}

const MAPS = ["colour", "normal", "roughness"] as const;
const fileName = (file: ScanFile): string => file.url.slice(file.url.lastIndexOf("/") + 1);
export const scanPath = (cache: string, m: Material, map: (typeof MAPS)[number]): string =>
    join(cache, m.asset, fileName(m.files[map]));

const md5 = (data: Uint8Array): string => createHash("md5").update(data).digest("hex");

export async function fetchScans(cache: string, log: (line: string) => void): Promise<number> {
    let fetched = 0;
    for (const m of materials()) {
        for (const map of MAPS) {
            const file = m.files[map];
            const path = scanPath(cache, m, map);
            if (existsSync(path) && md5(readFileSync(path)) === file.md5) continue;
            log(`${m.kind}: ${m.asset} ${map} (${(file.bytes / 1e6).toFixed(1)} MB) <- ${file.url}`);
            const response = await fetch(file.url);
            if (!response.ok) throw new Error(`${file.url}: HTTP ${response.status}`);
            const data = new Uint8Array(await response.arrayBuffer());
            if (data.length !== file.bytes || md5(data) !== file.md5) {
                throw new Error(
                    `${file.url}: ${data.length} bytes, MD5 ${md5(data)} - not the file materials.json lists`,
                );
            }
            mkdirSync(dirname(path), { recursive: true });
            writeFileSync(path, data);
            fetched++;
        }
    }
    return fetched;
}

// The kinds' detail textures from the cached scans.
export function kindEntries(cache: string, side: number, log: (line: string) => void): KindEntry[] {
    const out: KindEntry[] = [];
    const all = materials();
    for (const m of all) {
        const grain = m.grainFrom ? all.find((other) => other.kind === m.grainFrom) : m;
        if (!grain) throw new Error(`materials.json: ${m.asset}: grainFrom ${m.grainFrom} - no such kind`);
        const read = (map: (typeof MAPS)[number]) => {
            const path = scanPath(cache, grain, map);
            if (!existsSync(path)) throw new Error(`${path} missing - run detail_materials.ts fetch first`);
            const data = readFileSync(path);
            if (md5(data) !== grain.files[map].md5)
                throw new Error(`${path}: not the file materials.json lists - fetch again`);
            return readPng(data);
        };
        out.push({
            kind: KIND[m.kind],
            side,
            tileMetres: grain.tileMetres,
            roughness: m.roughness,
            roughnessSwing: m.roughnessSwing,
            f0: m.f0,
            metalness: m.metalness,
            roughnessWeight: m.roughnessWeight,
            texels: detailTexels(read("colour"), read("normal"), read("roughness"), side),
        });
        log(
            `${m.kind}: ${grain.asset} (${grain.name}, ${grain.licence}) - ${side} x ${side}${grain === m ? "" : ` (grain of ${grain.kind})`}`,
        );
    }
    return out;
}

const walk = (path: string): string[] =>
    !existsSync(path)
        ? []
        : statSync(path).isDirectory()
          ? readdirSync(path).flatMap((name) => walk(join(path, name)))
          : [path];

// The levels' collision files the game reads (the overlay's, else an archive's, else the loose one), by name under Data
// ("levels/bo/bo.col.xbx").
export function collisionFiles(root: string): Map<string, () => Buffer> {
    const found = new Map<string, () => Buffer>();
    const isCol = (name: string): boolean => name.startsWith("levels/") && name.endsWith(".col.xbx");
    for (const path of walk(join(root, "Data", "levels"))) {
        const name = relative(join(root, "Data"), path).replaceAll("\\", "/").toLowerCase();
        if (isCol(name)) found.set(name, () => readFileSync(path));
    }
    const pre = join(root, "Data", "pre");
    for (const archive of existsSync(pre) ? readdirSync(pre).filter((f) => f.toLowerCase().endsWith(".prx")) : []) {
        try {
            for (const [entry, data] of prx.entries(join(pre, archive), (n) => /\.col\.xbx$/i.test(n))) {
                const name = entry.replaceAll("\\", "/").toLowerCase();
                if (isCol(name)) found.set(name, () => data);
            }
        } catch {}
    }
    if (existsSync(join(root, DATA_OVERLAY))) {
        for (const path of walk(join(root, DATA_OVERLAY, "levels"))) {
            const name = relative(join(root, DATA_OVERLAY), path).replaceAll("\\", "/").toLowerCase();
            if (isCol(name)) found.set(name, () => readFileSync(path));
        }
    }
    return found;
}

interface Gathered {
    checksum: number;
    contentHash: bigint;
    className: ClassName;
    meanLuma: number;
    level: string;
    use: TextureUse;
}

function mergeUse(into: TextureUse, from: TextureUse): void {
    into.area += from.area;
    into.terrain.unmatched += from.terrain.unmatched;
    into.terrain.level += from.terrain.level;
    into.terrain.total += from.terrain.total;
    for (const [name, area] of from.terrain.areas)
        into.terrain.areas.set(name, (into.terrain.areas.get(name) ?? 0) + area);
    for (const [bin, area] of from.density) into.density.set(bin, (into.density.get(bin) ?? 0) + area);
}

export interface PackResult {
    file: string;
    assignments: Assignment[];
    byKind: Map<KindName, number>;
    byEvidence: [number, number]; // class, terrain
    report: string[];
}

export function pack(
    game: string,
    options: {
        cache: string;
        side: number;
        levels?: string[] | undefined;
        out?: string | undefined;
        log: (line: string) => void;
    },
): PackResult {
    const root = gameRoot(game);
    const kinds = kindEntries(options.cache, options.side, options.log);
    const tileMetres = new Map(materials().map((m) => [m.kind, m.tileMetres]));
    const cols = collisionFiles(root);
    const gathered = new Map<string, Gathered>();
    for (const dictionary of dictionaries(root, options.levels)) {
        let textures: Texture[];
        try {
            textures = readTex(dictionary.tex(), dictionary.name).textures;
        } catch (error) {
            options.log(`${dictionary.name}: not read (${(error as Error).message})`);
            continue;
        }
        const scn = dictionary.scn();
        const usage = usageOf(scn);
        let uses = new Map<number, TextureUse>();
        if (scn) {
            const col = cols.get(dictionary.name.replace(/\.tex\.xbx$/, ".col.xbx"));
            try {
                uses = textureUses(scn, col ? col() : null, dictionary.name);
            } catch (error) {
                options.log(`${dictionary.name}: scene or collision not read (${(error as Error).message})`);
            }
        }
        for (const texture of textures) {
            const level0 = texture.levels[0];
            if (!level0) continue;
            const hash = contentHash(level0);
            const key = `${texture.checksum}:${hash}`;
            const use = uses.get(texture.checksum);
            const known = gathered.get(key);
            if (known) {
                if (use) mergeUse(known.use, use);
                continue;
            }
            const image = textureRgba(texture);
            if (!image) continue;
            const used = usage.get(texture.checksum) ?? {
                base: true,
                overlay: false,
                environment: false,
                shiny: false,
                decal: false,
                transparent: false,
            };
            const stats = statsOf(image);
            const verdict = classify(stats, used);
            if (verdict.skip) continue; // (no surface map: no detail either)
            let luma = 0;
            for (let i = 0; i < image.width * image.height; i++) {
                luma +=
                    LUMA[0] * image.data[i * 4]! + LUMA[1] * image.data[i * 4 + 1]! + LUMA[2] * image.data[i * 4 + 2]!;
            }
            const fresh: TextureUse = {
                terrain: { areas: new Map(), unmatched: 0, level: 0, total: 0 },
                area: 0,
                density: new Map(),
            };
            if (use) mergeUse(fresh, use);
            gathered.set(key, {
                checksum: texture.checksum,
                contentHash: hash,
                className: verdict.name,
                meanLuma: luma / (image.width * image.height * 255),
                level: dictionary.name.split("/")[1] ?? "",
                use: fresh,
            });
        }
    }
    const assignments: Assignment[] = [];
    const byKind = new Map<KindName, number>();
    const byEvidence: [number, number] = [0, 0];
    const report: string[] = ["level\tchecksum\tclass\tkind\tevidence\tmetres_per_uv\trepeats\tmean_luma\tterrain"];
    for (const g of [...gathered.values()].sort(
        (a, b) => a.checksum - b.checksum || (a.contentHash < b.contentHash ? -1 : 1),
    )) {
        const decision = decideKind(g.className, g.use.area > 0 ? g.use.terrain : undefined);
        const metres = metresPerUv(g.use);
        const tile = tileMetres.get(decision.kind) ?? 0;
        const repeats = metres > 0 && tile > 0 ? Math.min(256, Math.max(0.25, metres / tile)) : 0;
        assignments.push({
            checksum: g.checksum,
            contentHash: g.contentHash,
            kind: KIND[decision.kind],
            evidence: decision.evidence,
            repeats,
            meanLuma: g.meanLuma,
        });
        byKind.set(decision.kind, (byKind.get(decision.kind) ?? 0) + 1);
        byEvidence[decision.evidence]++;
        const terrain = [...g.use.terrain.areas]
            .sort((a, b) => b[1] - a[1])
            .map(([name, area]) => `${name}:${Math.round(area)}`)
            .join(" ");
        report.push(
            [
                g.level,
                g.checksum.toString(16).padStart(8, "0"),
                g.className,
                decision.kind,
                decision.evidence ? "terrain" : "class",
                metres.toFixed(3),
                repeats.toFixed(2),
                g.meanLuma.toFixed(3),
                `${terrain}${g.use.terrain.unmatched > 0 ? ` unmatched:${Math.round(g.use.terrain.unmatched)}` : ""}`,
            ].join("\t"),
        );
    }
    const file = options.out ?? join(root, LIBRARY_FILE);
    mkdirSync(dirname(file), { recursive: true });
    writeFileSync(file, writeLibrary(kinds, assignments));
    return { file, assignments, byKind, byEvidence, report };
}

function argValue(args: string[], name: string): string | undefined {
    const at = args.indexOf(name);
    return at >= 0 ? args[at + 1] : undefined;
}

const USAGE =
    "Usage: node tools/detail_materials/detail_materials.ts list\n" +
    "       node tools/detail_materials/detail_materials.ts fetch [--cache <folder>]\n" +
    "       node tools/detail_materials/detail_materials.ts pack --game <folder> [--cache <folder>] [--side 1024] " +
    "[--levels a,b] [--out <file>] [--report <file.tsv>]\n" +
    "       node tools/detail_materials/detail_materials.ts remove --game <folder>";

export async function main(args: string[]): Promise<void> {
    const [command] = args;
    const cache = argValue(args, "--cache") ?? DEFAULT_CACHE;
    const log = (line: string) => console.log(line);
    if (command === "list") {
        for (const m of materials()) {
            console.log(
                `${m.kind.padEnd(16)} ${m.asset.padEnd(20)} ${m.licence} (${m.licenceUrl}) ${m.authors.join(", ")} - ${m.page}`,
            );
        }
        return;
    }
    if (command === "fetch") {
        const fetched = await fetchScans(cache, log);
        console.log(`${fetched} files downloaded into ${cache} (the others were there)`);
        return;
    }
    const game = argValue(args, "--game");
    if (!game) exitWith(USAGE);
    if (command === "remove") {
        const file = join(gameRoot(game), LIBRARY_FILE);
        if (existsSync(file)) rmSync(file);
        console.log(`removed ${file}`);
        return;
    }
    if (command !== "pack") exitWith(USAGE);
    const side = Number(argValue(args, "--side") ?? 1024);
    if (![256, 512, 1024, 2048].includes(side)) exitWith(`--side ${side}: 256, 512, 1024 or 2048`);
    const result = pack(game, {
        cache,
        side,
        levels: argValue(args, "--levels")?.split(",").filter(Boolean),
        out: argValue(args, "--out"),
        log,
    });
    const reportFile = argValue(args, "--report");
    if (reportFile) writeFileSync(reportFile, `${result.report.join("\n")}\n`);
    console.log(`${result.file}: ${result.assignments.length} textures assigned`);
    console.log(`kinds: ${[...result.byKind].map(([k, n]) => `${k} ${n}`).join(", ")}`);
    console.log(`evidence: terrain ${result.byEvidence[1]}, class ${result.byEvidence[0]}`);
}

if (isMain(import.meta.url)) await main(process.argv.slice(2));
