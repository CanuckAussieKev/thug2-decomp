// NOT ORIGINAL - missing-texture finder for converted levels (any port): per level folder, the surfaces THUG2 draws
// without a real texture.
//
//     node tools/level_port/texture_check.ts <levels folder> [<level> ...] [--top N]
//
// e.g. node tools/level_port/texture_check.ts C:\Games\THUG2-2\thug2-decomp-thp8\levels thp8_net_beavervalley
//
// In the game's terms a texture is missing when a material's pass names a checksum the level's .tex.xbx doesn't hold
// (THUG2 draws that pass untextured: its vertex colour only), or a pass has none at all. Converted textures can also
// be there and still show nothing: a flat colour (a converter took a source's blank stand-in - THP8's 32 x 32 grey,
// its white 32 x 32) or a tiny stipple (THP8's 16 x 16 dither of see-through texels) - found from the texels.
// Reported by cause and texture, with the triangles and area drawn with it and an example place for
// tools/game/try_spot.ts.

import * as fs from "node:fs";
import * as path from "node:path";
import { parseArgs } from "node:util";
import { isMain, print, runMain } from "../lib/py.ts";
import * as dxt from "./dxt.ts";
import * as tex from "./tex.ts";
import * as thug2Scene from "./thug2_scene.ts";
import { fmtSpot, INCHES_PER_METRE, sceneTriangles, spotFor, type Tri } from "./zfight.ts";

export const FLAT_DEVIATION = 2.0; // brightness standard deviation (0-255) under which a texture is one colour
export const STIPPLE_MOST = 32 * 32; // texels: a see-through dither bigger than this isn't taken for a placeholder
export const STIPPLE_ALTERNATION = 0.4; // share of neighbouring texels (along rows) one see-through, one not

export interface Look {
    mean: number[]; // B, G, R, A 0-255
    deviation: number; // of the brightness (R + G + B) / 3
    clear: number; // share of texels with alpha under 128
    alternation: number; // share of row neighbours of which one is see-through and the other not
    flat: boolean; // one colour (and alpha all one way)
    stipple: boolean; // tiny, a dither of opaque and see-through texels
}

// What a texture's texels look like (B, G, R, A bytes).
export function lookOf(bgra: Uint8Array, width: number, height: number): Look {
    const n = width * height;
    const sum = [0, 0, 0, 0];
    let s = 0;
    let s2 = 0;
    let clear = 0;
    for (let i = 0; i < n; i++) {
        for (let c = 0; c < 4; c++) sum[c]! += bgra[4 * i + c]!;
        const v = (bgra[4 * i]! + bgra[4 * i + 1]! + bgra[4 * i + 2]!) / 3;
        s += v;
        s2 += v * v;
        if (bgra[4 * i + 3]! < 128) clear++;
    }
    // (a dither alternates see-through and opaque from texel to texel; a cut-out - a distant tree line - has runs)
    let changes = 0;
    let pairs = 0;
    for (let y = 0; y < height; y++) {
        for (let x = 0; x + 1 < width; x++) {
            pairs++;
            const at = 4 * (y * width + x) + 3;
            if (bgra[at]! < 128 !== bgra[at + 4]! < 128) changes++;
        }
    }
    const mean = sum.map((x) => x / Math.max(1, n));
    const m = s / Math.max(1, n);
    const deviation = Math.sqrt(Math.max(0, s2 / Math.max(1, n) - m * m));
    const share = clear / Math.max(1, n);
    const alternation = changes / Math.max(1, pairs);
    return {
        mean,
        deviation,
        clear: share,
        alternation,
        flat: deviation < FLAT_DEVIATION && (share === 0 || share === 1),
        stipple: n <= STIPPLE_MOST && share > 0.2 && share < 0.8 && alternation >= STIPPLE_ALTERNATION,
    };
}

// A THUG2 texture's base level as B, G, R, A (null: a kind this doesn't read).
export function texels(t: tex.Texture): Buffer | null {
    const level = t.levels[0];
    if (level === undefined || t.palette.length) return null;
    if (t.dxt === 0) return t.texelDepth === 32 ? Buffer.from(level) : null;
    return dxt.decode(level, t.width, t.height, t.dxt === 2 ? 1 : t.dxt);
}

export type Cause = "missing" | "untextured" | "flat colour" | "stipple placeholder";

export interface Finding {
    cause: Cause;
    texture: number; // 0 for untextured
    pass: number;
    materials: Set<number>; // material checksums
    meshes: number;
    triangles: number;
    area: number; // square inches
    example: { sector: number; centre: number[]; normal: number[]; area: number } | null; // its largest triangle's
    look?: Look;
}

const triArea = (t: Tri): number => {
    const [a, b, c] = t.p;
    const u = [b[0] - a[0], b[1] - a[1], b[2] - a[2]];
    const v = [c[0] - a[0], c[1] - a[1], c[2] - a[2]];
    return Math.hypot(u[1]! * v[2]! - u[2]! * v[1]!, u[2]! * v[0]! - u[0]! * v[2]!, u[0]! * v[1]! - u[1]! * v[0]!) / 2;
};

// The level's textureless surfaces: a finding per (cause, texture), most area first.
export function checkTextures(scene: thug2Scene.Scene, textures: tex.TexFile, tris = sceneTriangles(scene)): Finding[] {
    const byChecksum = new Map(textures.textures.map((t) => [t.checksum, t]));
    const looks = new Map<number, Look | null>();
    const lookAt = (c: number): Look | null => {
        if (!looks.has(c)) {
            const t = byChecksum.get(c)!;
            const data = texels(t);
            looks.set(c, data ? lookOf(data, t.width, t.height) : null);
        }
        return looks.get(c)!;
    };
    // per material: its passes' problems
    const problems = new Map<number, { cause: Cause; texture: number; pass: number; look?: Look }[]>();
    scene.materials.forEach((m, index) => {
        const out: { cause: Cause; texture: number; pass: number; look?: Look }[] = [];
        m.passes.forEach((p, k) => {
            if (!p.texture || !(p.flags & thug2Scene.PASS_TEXTURED)) {
                if (k === 0) out.push({ cause: "untextured", texture: 0, pass: k });
                return;
            }
            if (!byChecksum.has(p.texture)) {
                out.push({ cause: "missing", texture: p.texture, pass: k });
                return;
            }
            const look = lookAt(p.texture);
            if (look?.flat) out.push({ cause: "flat colour", texture: p.texture, pass: k, look });
            else if (look?.stipple) out.push({ cause: "stipple placeholder", texture: p.texture, pass: k, look });
        });
        if (out.length) problems.set(index, out);
    });
    const findings = new Map<string, Finding>();
    const meshSeen = new Set<string>();
    for (const t of tris) {
        const list = problems.get(t.material);
        if (!list) continue;
        const area = triArea(t);
        const centre = [0, 1, 2].map((k) => (t.p[0][k]! + t.p[1][k]! + t.p[2][k]!) / 3);
        for (const p of list) {
            const key = `${p.cause}|${p.texture}|${p.pass}`;
            let f = findings.get(key);
            if (f === undefined) {
                f = {
                    cause: p.cause,
                    texture: p.texture,
                    pass: p.pass,
                    materials: new Set(),
                    meshes: 0,
                    triangles: 0,
                    area: 0,
                    example: null,
                    ...(p.look ? { look: p.look } : {}),
                };
                findings.set(key, f);
            }
            f.materials.add(scene.materials[t.material]!.checksum);
            const meshKey = `${key}|${t.sector}:${t.mesh}`;
            if (!meshSeen.has(meshKey)) {
                meshSeen.add(meshKey);
                f.meshes++;
            }
            f.triangles++;
            f.area += area;
            if (!f.example || area > f.example.area)
                f.example = { sector: scene.sectors[t.sector]!.checksum, centre, normal: t.n, area };
        }
    }
    return [...findings.values()].sort((a, b) => b.area - a.area);
}

const hex = (x: number): string => x.toString(16).toUpperCase().padStart(8, "0");

export function describeLook(look: Look | undefined): string {
    if (!look) return "";
    const [b, g, r, a] = look.mean.map((x) => x.toFixed(0));
    return look.stipple
        ? ` (stipple: ${(look.clear * 100).toFixed(0)}% see-through texels)`
        : ` (RGB ${r},${g},${b} alpha ${a})`;
}

export function report(
    level: string,
    findings: readonly Finding[],
    top = 15,
    sectorName: (c: number) => string = hex,
    title = level,
): string[] {
    const m2 = (x: number): string => (x / (INCHES_PER_METRE * INCHES_PER_METRE)).toFixed(0);
    const lines = [`${title}:`];
    if (!findings.length) lines.push("  every pass's texture is there and has texels that vary");
    const causes = new Map<Cause, Finding[]>();
    for (const f of findings) causes.set(f.cause, [...(causes.get(f.cause) ?? []), f]);
    for (const [cause, list] of causes) {
        const area = list.reduce((a, f) => a + f.area, 0);
        const tris = list.reduce((a, f) => a + f.triangles, 0);
        const mats = new Set(list.flatMap((f) => [...f.materials]));
        lines.push(`  ${cause}: ${list.length} textures, ${mats.size} materials, ${tris} triangles, ${m2(area)} m2`);
        for (const f of list.slice(0, top)) {
            const spot = f.example ? fmtSpot(spotFor(f.example.centre, f.example.normal)) : "-";
            lines.push(
                `    ${f.texture ? hex(f.texture) : "(none)"} pass ${f.pass}${describeLook(f.look)}: ${f.materials.size} materials, ${f.meshes} meshes, ${f.triangles} triangles, ${m2(f.area)} m2` +
                    (f.example ? ` - e.g. ${sectorName(f.example.sector)} at try_spot ${level} ${spot}` : ""),
            );
        }
    }
    return lines;
}

export function main(argv: readonly string[] = process.argv.slice(2)): number {
    const { values, positionals } = parseArgs({
        args: [...argv],
        allowPositionals: true,
        options: { top: { type: "string", default: "15" } },
    });
    const [folder, ...levels] = positionals;
    if (!folder) {
        print("usage: node tools/level_port/texture_check.ts <levels folder> [<level> ...] [--top N]");
        return 2;
    }
    const names = levels.length
        ? levels
        : fs.readdirSync(folder).filter((d) => fs.existsSync(path.join(folder, d, `${d}.scn.xbx`)));
    for (const level of names) {
        const base = path.join(folder, level, level);
        const scene = thug2Scene.readScene(fs.readFileSync(`${base}.scn.xbx`), level);
        const textures = fs.existsSync(`${base}.tex.xbx`)
            ? tex.readTex(fs.readFileSync(`${base}.tex.xbx`), level)
            : { version: 1, textures: [] };
        print(report(level, checkTextures(scene, textures), Number(values.top)).join("\n"));
    }
    return 0;
}

if (isMain(import.meta.url)) await runMain(() => main());
