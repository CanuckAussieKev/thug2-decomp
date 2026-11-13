// NOT ORIGINAL - which of the levels' materials [Graphics] rt_water shades as water (docs/design/vulkan.md "Water";
// render_pc/water_materials.cpp is the rule): read from the game's own level files, nothing run.
//
//   node tools/game/water_survey.ts [--game <install>] [--levels AU,BA,...]
//
// For each level (Data\levels\<name>\<name>.scn.xbx and its .tex.xbx) every material whose passes move (a UV wibble -
// pass flag 0x1 - or a texture animation - 0x800): whether it counts as water (its first pass has a texture and is drawn
// as itself or blended by alpha: blend modes 0, 5, 6, 11) and its first texture's mean colour looks like water's (not
// warm, near white or near black - the shader's own test), and its meshes: how many, their bounding box (world units) and how flat it is (the
// box's height over its width: water is flat - the shader shades only pixels that face up).
import * as fs from "node:fs";
import * as path from "node:path";
import { decode } from "thug2-level-converter-core/level_port/dxt.ts";
import { readTex, type Texture } from "thug2-level-converter-core/level_port/tex.ts";
import { type Material, readScene } from "thug2-level-converter-core/level_port/thug2_scene.ts";
import { isMain, parseCli, print, runMain } from "thug2-level-converter-core/lib/cli.ts";
import { hex8 } from "thug2-level-converter-core/lib/text.ts";

const USAGE = `usage: water_survey.ts [--game <install>] [--levels AU,BA,...]

The levels' moving materials and whether rt_water shades them as water (tools/game/README.md).`;

const PASS_UV_WIBBLE = 0x1;
const PASS_ANIMATES = 0x800;
const WATER_BLENDS = new Set([0, 5, 6, 0xb]); // (render_pc/water_materials.cpp drawn_as_surface)
const BLEND_MASK = 0xffffff;

export function moving(material: Material): boolean {
    return material.passes.some((p) => (p.flags & (PASS_UV_WIBBLE | PASS_ANIMATES)) !== 0 || p.uvWibble !== undefined);
}

export function isWater(material: Material): boolean {
    const first = material.passes[0];
    return (
        first !== undefined && first.texture !== 0 && moving(material) && WATER_BLENDS.has(first.blendMode & BLEND_MASK)
    );
}

// A texture's mean colour (0 - 1 rgb) from its smallest level of at least 4 x 4 texels; null when it can't be read.
export function meanColour(texture: Texture): number[] | null {
    let level = 0;
    while (
        level + 1 < texture.levels.length &&
        Math.max(1, texture.width >> (level + 1)) >= 4 &&
        Math.max(1, texture.height >> (level + 1)) >= 4
    ) {
        level++;
    }
    const width = Math.max(1, texture.width >> level);
    const height = Math.max(1, texture.height >> level);
    const data = texture.levels[level];
    if (!data) return null;
    let bgra: Uint8Array;
    if (texture.dxt !== 0) {
        bgra = decode(data, width, height, texture.dxt);
    } else if (texture.texelDepth === 32) {
        bgra = data; // (the texels' order doesn't change the mean)
    } else {
        return null;
    }
    const sum = [0, 0, 0];
    const count = width * height;
    for (let i = 0; i < count; i++) {
        sum[0] = (sum[0] ?? 0) + (bgra[i * 4 + 2] ?? 0);
        sum[1] = (sum[1] ?? 0) + (bgra[i * 4 + 1] ?? 0);
        sum[2] = (sum[2] ?? 0) + (bgra[i * 4] ?? 0);
    }
    return sum.map((v) => v / count / 255);
}

const smoothstep = (low: number, high: number, x: number): number => {
    const t = Math.min(Math.max((x - low) / (high - low), 0), 1);
    return t * t * (3 - 2 * t);
};

// The shader's own test (gpu_vulkan/water_shader.cpp water_hue_weight): a texture's mean colour is water's (1) unless
// warm (red over blue), near white or near black (0).
export function hueWeight(mean: readonly number[]): number {
    const [r = 0, g = 0, b = 0] = mean;
    const luma = 0.2126 * r + 0.7152 * g + 0.0722 * b;
    return (1 - smoothstep(0.04, 0.12, r - b)) * smoothstep(0.06, 0.12, luma) * (1 - smoothstep(0.58, 0.68, luma));
}

interface Row {
    level: string;
    material: number;
    water: boolean;
    passes: string;
    mean: string;
    meshes: number;
    box: string;
    flatness: number;
}

function surveyLevel(levels: string, level: string): Row[] {
    const folder = path.join(levels, level);
    const scn = path.join(folder, `${level}.scn.xbx`);
    if (!fs.existsSync(scn)) return [];
    const scene = readScene(fs.readFileSync(scn), scn);
    const texPath = path.join(folder, `${level}.tex.xbx`);
    const textures = new Map<number, Texture>();
    if (fs.existsSync(texPath)) {
        for (const t of readTex(fs.readFileSync(texPath), texPath).textures) textures.set(t.checksum, t);
    }
    const rows: Row[] = [];
    for (const material of scene.materials) {
        if (!moving(material)) continue;
        const low = [Infinity, Infinity, Infinity];
        const high = [-Infinity, -Infinity, -Infinity];
        let meshes = 0;
        for (const sector of scene.sectors) {
            for (const mesh of sector.meshes) {
                if (mesh.material !== material.checksum) continue;
                meshes++;
                for (let axis = 0; axis < 3; axis++) {
                    low[axis] = Math.min(low[axis] ?? 0, mesh.bbox[axis] ?? 0);
                    high[axis] = Math.max(high[axis] ?? 0, mesh.bbox[axis + 3] ?? 0);
                }
            }
        }
        const first = material.passes[0];
        const texture = first ? textures.get(first.texture) : undefined;
        const mean = texture ? meanColour(texture) : null;
        const size = [0, 1, 2].map((a) => (high[a] ?? 0) - (low[a] ?? 0));
        const width = Math.max(size[0] ?? 0, size[2] ?? 0);
        rows.push({
            level,
            material: material.checksum,
            water: isWater(material) && (mean === null || hueWeight(mean) >= 0.5),
            passes: material.passes
                .map((p) => `${(p.flags & 0xffff).toString(16)}/${(p.blendMode & BLEND_MASK).toString(16)}`)
                .join(" "),
            mean: mean ? mean.map((v) => v.toFixed(2)).join(",") : "-",
            meshes,
            box: meshes
                ? `${low.map((v) => v.toFixed(0)).join(",")} .. ${high.map((v) => v.toFixed(0)).join(",")}`
                : "-",
            flatness: width > 0 ? (size[1] ?? 0) / width : 0,
        });
    }
    return rows;
}

async function main(): Promise<number> {
    const { values } = parseCli(USAGE, {
        options: { game: { type: "string", default: "C:\\Games\\THUG2\\Game" }, levels: { type: "string" } },
    });
    const levels = path.join(values.game, "Data", "levels");
    const names = values.levels
        ? values.levels.split(",")
        : fs.readdirSync(levels).filter((n) => !/_sky$|_shell$|^sk5ed/i.test(n) && n !== "default" && n !== "mainmenu");
    for (const level of names) {
        const rows = surveyLevel(levels, level);
        if (!rows.length) continue;
        print(`${level}: ${rows.filter((r) => r.water).length} water of ${rows.length} moving materials`);
        for (const row of rows) {
            print(
                `  ${row.water ? "water" : "-    "} ${hex8(row.material)} passes ${row.passes} mean ${row.mean} meshes ${row.meshes} box ${row.box} flat ${row.flatness.toFixed(3)}`,
            );
        }
    }
    return 0;
}

if (isMain(import.meta.url)) runMain(main);
