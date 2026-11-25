// NOT ORIGINAL - which of the levels' meshes are light volumes (docs/design/vulkan.md "Round FIXTURE"; the rule is
// src/engine/gfx/rt_lights.h light_volume, read by render_pc/rt_record.cpp light_volume_mesh): read from the game's own
// level files, nothing run.
//
//   node tools/game/light_volume_survey.ts [--game <install>] [--levels TR,BA,...] [--near]
//     (the game's Dataevels and the overlays' levels - <game>	hug2-decomp-*evels)
//
// For each level (Data\levels\<name>\<name>.scn.xbx and its .tex.xbx) every mesh the rule takes - a first pass blended by
// its alpha (blend mode 5), its texture still, every vertex one colour with its brightest channel at 0x80 or more, its
// vertex alpha fading to 0 (its least 0, its most above) and its first texture a plain light (every texel's linear
// luminance at least 0.5) - with its material, colour, alpha range, box and place. --near also lists the meshes that pass
// every test but the texture's (the shadow blobs, dirt and foliage fades the texture test keeps out) and the moving ones
// (water).
import * as fs from "node:fs";
import * as path from "node:path";
import { decode } from "thug2-level-converter-core/level_port/dxt.ts";
import { readTex, type Texture } from "thug2-level-converter-core/level_port/tex.ts";
import { FVF_DIFFUSE, type Material, type Mesh, readScene } from "thug2-level-converter-core/level_port/thug2_scene.ts";
import { isMain, parseCli, print, runMain } from "thug2-level-converter-core/lib/cli.ts";
import { hex8 } from "thug2-level-converter-core/lib/text.ts";

const USAGE = `usage: light_volume_survey.ts [--game <install>] [--levels TR,BA,...] [--near]

The levels' light volumes as the renderer's rule takes them (tools/game/README.md).`;

const BLEND_BY_ALPHA = 5;
const BLEND_MASK = 0xffffff;
const FULL_BRIGHT = 0x80; // (rt_lights.h kFullBrightByte)
const TEXEL_LIGHT = 0.5; // (rt_lights.h kVolumeTexelLight)
const PASS_UV_WIBBLE = 0x1;
const PASS_ANIMATES = 0x800;

// The facts the rule reads (rt_lights.h VolumeLook).
export interface VolumeLook {
    blend: number;
    moving: boolean;
    oneColour: boolean;
    brightest: number;
    leastAlpha: number;
    mostAlpha: number;
    leastTexelLuma: number;
}

// rt_lights.h light_volume.
export function isLightVolume(look: VolumeLook): boolean {
    return (
        look.blend === BLEND_BY_ALPHA &&
        !look.moving &&
        look.oneColour &&
        look.brightest >= FULL_BRIGHT &&
        look.leastAlpha === 0 &&
        look.mostAlpha > 0 &&
        look.leastTexelLuma >= TEXEL_LIGHT
    );
}

export function moving(material: Material): boolean {
    return material.passes.some(
        (p) =>
            (p.flags & (PASS_UV_WIBBLE | PASS_ANIMATES)) !== 0 || p.uvWibble !== undefined || p.animation !== undefined,
    );
}

// A mesh's vertex colours (rt_surface_class.h vertex_colours_in): one colour, its brightest byte, the alpha range.
export function vertexColours(
    mesh: Mesh,
): { oneColour: boolean; brightest: number; leastAlpha: number; mostAlpha: number; colour: number } | null {
    const buffer = mesh.buffers[0]?.[1];
    if (!buffer || ((mesh.vertexShader[0] ?? 0) & FVF_DIFFUSE) === 0 || mesh.vertexCount === 0) return null;
    const at = mesh.offsets[1] ?? 0;
    if (at + 4 > mesh.stride) return null;
    const first = buffer.readUInt32LE(at) & 0xffffff;
    let oneColour = true;
    let leastAlpha = 255;
    let mostAlpha = 0;
    for (let i = 0; i < mesh.vertexCount; i++) {
        const o = i * mesh.stride + at;
        if (o + 4 > buffer.length) break;
        if ((buffer.readUInt32LE(o) & 0xffffff) !== first) oneColour = false;
        const alpha = buffer[o + 3] ?? 0;
        leastAlpha = Math.min(leastAlpha, alpha);
        mostAlpha = Math.max(mostAlpha, alpha);
    }
    const brightest = Math.max(first & 0xff, (first >> 8) & 0xff, (first >> 16) & 0xff);
    return { oneColour, brightest, leastAlpha, mostAlpha, colour: first };
}

const linear = (byte: number): number => {
    const c = byte / 255;
    return c <= 0.04045 ? c / 12.92 : ((c + 0.055) / 1.055) ** 2.4;
};

// A texture's dimmest texel's linear luminance (level 0); -1 when it can't be read.
export function leastTexelLuma(texture: Texture): number {
    const data = texture.levels[0];
    if (!data) return -1;
    let bgra: Uint8Array;
    if (texture.dxt !== 0) bgra = decode(data, texture.width, texture.height, texture.dxt);
    else if (texture.texelDepth === 32) bgra = data;
    else return -1;
    let least = 1;
    for (let i = 0; i < texture.width * texture.height; i++) {
        const luma =
            0.2126 * linear(bgra[i * 4 + 2] ?? 0) +
            0.7152 * linear(bgra[i * 4 + 1] ?? 0) +
            0.0722 * linear(bgra[i * 4] ?? 0);
        if ((bgra[i * 4 + 3] ?? 0) !== 0) least = Math.min(least, luma); // (an alpha-0 texel shows nothing)
    }
    return least;
}

interface Row {
    verdict: "volume" | "dark texture" | "moving";
    material: number;
    colour: number;
    alpha: string;
    texel: number;
    box: string;
    at: string;
}

function surveyLevel(levels: string, level: string, near: boolean): Row[] {
    const folder = path.join(levels, level);
    const scn = path.join(folder, `${level}.scn.xbx`);
    if (!fs.existsSync(scn)) return [];
    const scene = readScene(fs.readFileSync(scn), scn);
    const materials = new Map(scene.materials.map((m) => [m.checksum, m]));
    let textures: Map<number, Texture> | null = null;
    const texelLumas = new Map<number, number>();
    const rows: Row[] = [];
    for (const sector of scene.sectors) {
        for (const mesh of sector.meshes) {
            const material = materials.get(mesh.material);
            const first = material?.passes[0];
            if (!material || !first || (first.blendMode & BLEND_MASK) !== BLEND_BY_ALPHA) continue;
            const colours = vertexColours(mesh);
            if (!colours) continue;
            if (!textures) {
                textures = new Map();
                const texPath = path.join(folder, `${level}.tex.xbx`);
                if (fs.existsSync(texPath))
                    for (const t of readTex(fs.readFileSync(texPath), texPath).textures) textures.set(t.checksum, t);
            }
            let texel = texelLumas.get(first.texture);
            if (texel === undefined) {
                const texture = textures.get(first.texture);
                texel = first.texture === 0 ? 1 : texture ? leastTexelLuma(texture) : -1;
                texelLumas.set(first.texture, texel);
            }
            const look: VolumeLook = {
                blend: BLEND_BY_ALPHA,
                moving: moving(material),
                oneColour: colours.oneColour,
                brightest: colours.brightest,
                leastAlpha: colours.leastAlpha,
                mostAlpha: colours.mostAlpha,
                leastTexelLuma: Math.max(texel, 0),
            };
            const volume = isLightVolume(look);
            const nearMiss =
                !volume && isLightVolume({ ...look, moving: false, leastTexelLuma: 1 }) && (look.moving || texel < 1);
            if (!volume && !(near && nearMiss)) continue;
            rows.push({
                verdict: volume ? "volume" : look.moving ? "moving" : "dark texture",
                material: mesh.material,
                colour: colours.colour,
                alpha: `${colours.leastAlpha}-${colours.mostAlpha}`,
                texel,
                box: [0, 1, 2].map((a) => ((mesh.bbox[a + 3] ?? 0) - (mesh.bbox[a] ?? 0)).toFixed(0)).join("x"),
                at: mesh.bbox
                    .slice(0, 3)
                    .map((v) => v.toFixed(0))
                    .join(","),
            });
        }
    }
    return rows;
}

async function main(): Promise<number> {
    const { values } = parseCli(USAGE, {
        options: {
            game: { type: "string", default: "C:\\Games\\THUG2\\Game" },
            levels: { type: "string" },
            near: { type: "boolean", default: false },
        },
    });
    // (the game's levels and the overlays' - the front end's, the ported levels': <game>\thug2-decomp-*\levels)
    const folders = [path.join(values.game, "Data", "levels")];
    for (const overlay of fs.existsSync(values.game) ? fs.readdirSync(values.game) : []) {
        for (const inner of [path.join(overlay, "levels"), path.join(overlay, "front_end", "levels")]) {
            if (overlay.startsWith("thug2-decomp-") && fs.existsSync(path.join(values.game, inner)))
                folders.push(path.join(values.game, inner));
        }
    }
    const wanted = values.levels ? new Set(values.levels.split(",")) : null;
    let total = 0;
    const perLevel: string[] = [];
    for (const [levels, level] of folders.flatMap((f) =>
        fs.existsSync(f) ? fs.readdirSync(f).map((n) => [f, n] as const) : [],
    )) {
        if (wanted && !wanted.has(level)) continue;
        const rows = surveyLevel(levels, level, values.near);
        if (!rows.length) continue;
        const volumes = rows.filter((r) => r.verdict === "volume").length;
        total += volumes;
        if (volumes) perLevel.push(`${level} ${volumes}`);
        print(`${level}: ${volumes} light volumes`);
        for (const row of rows) {
            print(
                `  ${row.verdict.padEnd(12)} ${hex8(row.material)} colour ${row.colour.toString(16).padStart(6, "0")} alpha ${row.alpha} texel ${row.texel.toFixed(3)} box ${row.box} at ${row.at}`,
            );
        }
    }
    print(`${total} light volumes${perLevel.length ? ` (${perLevel.join(", ")})` : ""}`);
    return 0;
}

if (isMain(import.meta.url)) runMain(main);
