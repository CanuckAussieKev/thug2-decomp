// NOT ORIGINAL - which of the levels' reflective materials are shine masks (docs/design/vulkan.md "Round SHINE"; the rule
// is src/engine/gfx/rt_surface_class.h reflection_class_on, after render_pc/rt_record.cpp classify_reflective): read
// from the game's own level files, nothing run.
//
//   node tools/game/shine_mask_survey.ts [--game <install>] [--levels TR,BA,...] [--list]
//     (the game's Data\levels and the overlays' levels - <game>\thug2-decomp-*\levels)
//
// For each level's world (Data\levels\<name>\<name>.scn.xbx and its .tex.xbx) every mesh whose material reflects as glass
// that isn't painted into its texture (its environment pass covered by a later pass's alpha, or its first pass blended)
// - by the class its mesh takes for rays (rt_surface_class.h surface_class: the first pass's blend, its texels' least
// alpha, the mesh's least vertex alpha): on a solid mesh (opaque, blended opaque, a cut-out) a shine mask's coat, on a
// seen-through one glass. Per level: the meshes and materials of each; --list names the materials. Objects' models
// (their own scenes) aren't read.
import * as fs from "node:fs";
import * as path from "node:path";
import { decode } from "thug2-level-converter-core/level_port/dxt.ts";
import { readTex, type Texture } from "thug2-level-converter-core/level_port/tex.ts";
import {
    FVF_DIFFUSE,
    type Material,
    type Mesh,
    type Pass,
    readScene,
} from "thug2-level-converter-core/level_port/thug2_scene.ts";
import { isMain, parseCli, print, runMain } from "thug2-level-converter-core/lib/cli.ts";
import { hex8 } from "thug2-level-converter-core/lib/text.ts";

const USAGE = `usage: shine_mask_survey.ts [--game <install>] [--levels TR,BA,...] [--list]

The levels' shine masks and glass as the renderer's rule takes them (tools/game/README.md).`;

const BLEND_MASK = 0xffffff;
const PASS_UV_WIBBLE = 0x1;
const PASS_ENVIRONMENT = 0x8;
const PASS_TRANSPARENT = 0x40;
const PASS_ANIMATES = 0x800;
const PASS_TEXTURE_ALPHA = 0x1000;
// the game's blend modes (engine/gfx/mesh_formats.h)
const ADD = 1;
const FIXED_ADD = 2;
const ALPHA = 5;
const FIXED_BLEND = 6;
const MODULATE = 7;
const FIXED_MODULATE = 8;
const LERP_PREVIOUS = 12;
const LERP_FROM_PREVIOUS = 13;

export type Reflection = "none" | "water" | "painted glass" | "glass" | "shiny";

const blendOf = (p: Pass): number => p.blendMode & BLEND_MASK;
const clamp01 = (v: number): number => Math.min(Math.max(v, 0), 1);

// rt_record.cpp reflectivity_of / environment_share / classify_reflective: the material's reflection class (water by its
// moving passes; the water materials' own list isn't read here).
export function reflectionOf(material: Material): Reflection {
    const passes = material.passes.slice(0, 4);
    const env = passes.findIndex((p) => (p.flags & PASS_ENVIRONMENT) !== 0 && p.texture !== 0);
    if (env < 0) return "none";
    let factor = 1;
    let lerped = false;
    let covered = false;
    if (env > 0) {
        const mode = blendOf(passes[env] as Pass);
        if (mode === FIXED_ADD || mode === FIXED_BLEND) factor *= clamp01((passes[env] as Pass).fixedAlpha);
        else if (mode === LERP_PREVIOUS || mode === LERP_FROM_PREVIOUS) lerped = true;
        else if (mode !== ADD && mode !== ALPHA) return "none";
    }
    for (let k = env + 1; k < passes.length; k++) {
        const mode = blendOf(passes[k] as Pass);
        if (mode === ALPHA) covered = true;
        else if (mode === FIXED_BLEND) factor *= 1 - clamp01((passes[k] as Pass).fixedAlpha);
        else if (mode === FIXED_MODULATE) factor *= clamp01((passes[k] as Pass).fixedAlpha);
        else if (mode === LERP_PREVIOUS && k - 1 !== env) covered = true;
    }
    const colour = (passes[env] as Pass).color;
    const strength = Math.max(colour[0] ?? 0, colour[1] ?? 0, colour[2] ?? 0) * 2 * factor;
    if (!(factor > 0) || !(strength > 0)) return "none";
    const moving = passes.some(
        (p) =>
            (p.flags & (PASS_UV_WIBBLE | PASS_ANIMATES)) !== 0 || p.uvWibble !== undefined || p.animation !== undefined,
    );
    const first = passes[0] as Pass;
    const firstMode = blendOf(first);
    const seenThrough = (first.flags & PASS_TRANSPARENT) !== 0 || firstMode === ALPHA || firstMode === FIXED_BLEND;
    if (moving) return "water";
    if (lerped) return "painted glass";
    if (covered || seenThrough) return "glass";
    return "shiny";
}

// rt_surface_class.h surface_class, as solid / seen through / adds light (`leastTexel` the first texture's least alpha
// 0 - 1, `leastVertex` the mesh's least vertex alpha 0 - 1).
export type RayClass = "solid" | "seen through" | "adds light";
export function rayClassOf(material: Material, leastTexel: number, leastVertex: number): RayClass {
    const first = material.passes[0];
    if (!first || (first.flags & PASS_TRANSPARENT) === 0) return "solid";
    const mode = blendOf(first);
    if ((mode >= 1 && mode <= 4) || mode === 9 || mode === 10) return "adds light";
    if (mode === MODULATE || mode === FIXED_MODULATE || mode === 15) return "seen through";
    let drawn = 1;
    if (mode === ALPHA)
        drawn = (first.flags & PASS_TEXTURE_ALPHA) !== 0 ? leastTexel : Math.min(1, 2 * leastVertex * leastTexel);
    else if (mode === FIXED_BLEND) drawn = first.fixedAlpha;
    // (a blended cut-out - kAlphaTested - is never an environment-mapped material: rt_record.cpp cutout_material)
    return drawn >= 1 ? "solid" : "seen through";
}

// rt_surface_class.h reflection_class_on: glass (not painted) on a solid mesh is a shine mask's coat.
export function classOn(reflection: Reflection, ray: RayClass): Reflection | "coat" {
    return reflection === "glass" && ray === "solid" ? "coat" : reflection;
}

function leastVertexAlpha(mesh: Mesh): number {
    const buffer = mesh.buffers[0]?.[1];
    if (!buffer || ((mesh.vertexShader[0] ?? 0) & FVF_DIFFUSE) === 0 || mesh.vertexCount === 0) return 1;
    const at = mesh.offsets[1] ?? 0;
    if (at + 4 > mesh.stride) return 1;
    let least = 255;
    for (let i = 0; i < mesh.vertexCount; i++) {
        const o = i * mesh.stride + at + 3;
        if (o >= buffer.length) break;
        least = Math.min(least, buffer[o] ?? 255);
    }
    return least / 255;
}

function leastTexelAlpha(texture: Texture | undefined): number {
    const data = texture?.levels[0];
    if (!texture || !data) return 0; // (not known: 0, as the renderer takes it)
    let bgra: Uint8Array;
    if (texture.dxt !== 0) bgra = decode(data, texture.width, texture.height, texture.dxt);
    else if (texture.texelDepth === 32) bgra = data;
    else return 0;
    let least = 255;
    for (let i = 0; i < texture.width * texture.height; i++) least = Math.min(least, bgra[i * 4 + 3] ?? 255);
    return least / 255;
}

interface Tally {
    coatMeshes: number;
    glassMeshes: number;
    coat: Set<number>;
    glass: Set<number>;
}

function surveyLevel(levels: string, level: string): Tally | null {
    const folder = path.join(levels, level);
    const scn = path.join(folder, `${level}.scn.xbx`);
    if (!fs.existsSync(scn)) return null;
    const scene = readScene(fs.readFileSync(scn), scn);
    const materials = new Map(scene.materials.map((m) => [m.checksum, m]));
    let textures: Map<number, Texture> | null = null;
    const texelAlphas = new Map<number, number>();
    const tally: Tally = { coatMeshes: 0, glassMeshes: 0, coat: new Set(), glass: new Set() };
    for (const sector of scene.sectors) {
        for (const mesh of sector.meshes) {
            const material = materials.get(mesh.material);
            if (!material || reflectionOf(material) !== "glass") continue;
            if (!textures) {
                textures = new Map();
                const texPath = path.join(folder, `${level}.tex.xbx`);
                if (fs.existsSync(texPath))
                    for (const t of readTex(fs.readFileSync(texPath), texPath).textures) textures.set(t.checksum, t);
            }
            const firstTexture = material.passes[0]?.texture ?? 0;
            let texel = texelAlphas.get(firstTexture);
            if (texel === undefined) {
                texel = firstTexture === 0 ? 1 : leastTexelAlpha(textures.get(firstTexture));
                texelAlphas.set(firstTexture, texel);
            }
            const kind = classOn("glass", rayClassOf(material, texel, leastVertexAlpha(mesh)));
            if (kind === "coat") {
                tally.coatMeshes++;
                tally.coat.add(mesh.material);
            } else {
                tally.glassMeshes++;
                tally.glass.add(mesh.material);
            }
        }
    }
    return tally;
}

async function main(): Promise<number> {
    const { values } = parseCli(USAGE, {
        options: {
            game: { type: "string", default: "C:\\Games\\THUG2\\Game" },
            levels: { type: "string" },
            list: { type: "boolean", default: false },
        },
    });
    const folders = [path.join(values.game, "Data", "levels")];
    for (const overlay of fs.existsSync(values.game) ? fs.readdirSync(values.game) : []) {
        for (const inner of [path.join(overlay, "levels"), path.join(overlay, "front_end", "levels")]) {
            if (overlay.startsWith("thug2-decomp-") && fs.existsSync(path.join(values.game, inner)))
                folders.push(path.join(values.game, inner));
        }
    }
    const wanted = values.levels ? new Set(values.levels.split(",")) : null;
    let coatMeshes = 0;
    let glassMeshes = 0;
    let coats = 0;
    let glasses = 0;
    let levels = 0;
    for (const [root, level] of folders.flatMap((f) =>
        fs.existsSync(f) ? fs.readdirSync(f).map((n) => [f, n] as const) : [],
    )) {
        if (wanted && !wanted.has(level)) continue;
        const tally = surveyLevel(root, level);
        if (!tally || tally.coatMeshes + tally.glassMeshes === 0) continue;
        levels++;
        coatMeshes += tally.coatMeshes;
        glassMeshes += tally.glassMeshes;
        coats += tally.coat.size;
        glasses += tally.glass.size;
        const both = [...tally.coat].filter((m) => tally.glass.has(m));
        print(
            `${level}: coat ${tally.coatMeshes} meshes / ${tally.coat.size} materials, glass ${tally.glassMeshes} meshes / ${tally.glass.size} materials${both.length ? `, ${both.length} materials on both` : ""}`,
        );
        if (values.list) {
            if (tally.coat.size) print(`  coat:  ${[...tally.coat].map(hex8).join(" ")}`);
            if (tally.glass.size) print(`  glass: ${[...tally.glass].map(hex8).join(" ")}`);
        }
    }
    print(
        `${levels} levels with glass not painted into a texture: coat ${coatMeshes} meshes (${coats} level materials), glass kept ${glassMeshes} meshes (${glasses})`,
    );
    return 0;
}

if (isMain(import.meta.url)) runMain(main);
