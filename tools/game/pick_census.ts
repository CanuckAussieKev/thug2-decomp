// The level editor's picking classes, measured offline from a level's own files (no game run): each scenery piece's
// box, whether it has collision, its materials' first passes (flags / blend mode) and what the editor's picking rules
// (src/game/front/level_editor_picking.h) make of it - see-through, structure, and the "level top" measure (how much
// of its upward-facing surface is level) the low-and-flat rule uses.
//
//   node tools/pipeline/game/pick_census.ts <level> [--game <folder>] [--all] [--name <substring>]
//       [--ray x,y,z --ray dx,dy,dz]
//
// <level>: TR, BO, foun, z_ma ... (looked for in the game's Data\levels and our overlays). Prints the pieces whose
// class differs between the old low-flat rule (height only) and the new one (height and a level top), the
// structure counts, the see-through counts, and with --name the pieces whose names contain the text. --ray lists
// every piece the ray meets (its triangles), nearest first, with its class - as the editor's Scenery::Pick sees it.
//
// Keep the thresholds here in step with level_editor_picking.h.

import * as fs from "node:fs";
import * as path from "node:path";
import { tokens } from "../level_port/qb.ts";
import { readCol } from "../level_port/thug2_col.ts";
import { type Material, type Mesh, readScene, type Sector } from "../level_port/thug2_scene.ts";
import { parseCli } from "../lib/cli.ts";
import { resolveInstall } from "../lib/install.ts";
import { hex8, isMain, print, pyExit, runMain } from "../lib/py.ts";

// level_editor_picking.h
const kWideSpan = 1500;
const kSlabSpan = 400;
const kSlabThickness = 0.1;
const kLowHeight = 80;
const kLowSpan = 350;
const kLevelNormalY = 0.996; // (within about 5 degrees of level)
const kLevelTopShare = 0.9;
const kUpNormalY = 0.25; // (faces turned more up than this count as its upward-facing surface)

type V3 = [number, number, number];

export interface TopMeasure {
    up: number; // upward-facing area
    level: number; // of it, level
}

// The level share of a piece's upward-facing surface, from its triangles (pure; the editor's TopOf does the same).
export function levelTopShare(m: TopMeasure): number {
    return m.up > 0 ? m.level / m.up : 1;
}

export function isStructure(min: V3, max: V3, top: TopMeasure | null): boolean {
    const e = [max[0] - min[0], max[1] - min[1], max[2] - min[2]];
    if (e[1]! <= kLowHeight && Math.min(e[0]!, e[2]!) >= kLowSpan && (!top || levelTopShare(top) >= kLevelTopShare))
        return true;
    const s = [...e].sort((a, b) => a - b);
    if (s[1]! >= kWideSpan) return true;
    return s[1]! >= kSlabSpan && s[0]! <= s[1]! * kSlabThickness;
}

function sub(a: V3, b: V3): V3 {
    return [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
}
function cross(a: V3, b: V3): V3 {
    return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
}
function dot(a: V3, b: V3): number {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

function* triangles(mesh: Mesh): Generator<[V3, V3, V3]> {
    const buffer = mesh.buffers[0]?.[1];
    const strip = mesh.strips[0];
    if (!buffer || !strip || mesh.stride < 12) return;
    const at = (i: number): V3 => {
        const o = i * mesh.stride;
        return [buffer.readFloatLE(o), buffer.readFloatLE(o + 4), buffer.readFloatLE(o + 8)];
    };
    for (let i = 2; i < strip.length; i++) {
        const a = strip[i - 2]!,
            b = strip[i - 1]!,
            c = strip[i]!;
        if (a === b || b === c || a === c || a >= mesh.vertexCount || b >= mesh.vertexCount || c >= mesh.vertexCount)
            continue;
        yield [at(a), at(b), at(c)];
    }
}

// Both sides count (strips alternate their winding): a face's normal is turned up.
export function measureTop(meshes: Mesh[]): TopMeasure {
    const m: TopMeasure = { up: 0, level: 0 };
    for (const mesh of meshes) {
        for (const [a, b, c] of triangles(mesh)) {
            const n = cross(sub(b, a), sub(c, a));
            const area2 = Math.sqrt(dot(n, n));
            if (area2 <= 0) continue;
            const ny = Math.abs(n[1]) / area2;
            if (ny <= kUpNormalY) continue;
            m.up += area2 / 2;
            if (ny >= kLevelNormalY) m.level += area2 / 2;
        }
    }
    return m;
}

function rayTriangle(o: V3, d: V3, a: V3, b: V3, c: V3): number | null {
    const e1 = sub(b, a),
        e2 = sub(c, a);
    const p = cross(d, e2);
    const det = dot(e1, p);
    if (Math.abs(det) < 1e-12) return null;
    const inv = 1 / det;
    const s = sub(o, a);
    const u = dot(s, p) * inv;
    if (u < 0 || u > 1) return null;
    const q = cross(s, e1);
    const v = dot(d, q) * inv;
    if (v < 0 || u + v > 1) return null;
    const t = dot(e2, q) * inv;
    return t > 0 ? t : null;
}

function findLevel(game: string, level: string): string | null {
    for (const root of [
        "Data/levels",
        "thug2-decomp-data/levels",
        "thug2-decomp-thaw/levels",
        "thug2-decomp-thps3/levels",
        "thug2-decomp-thps4/levels",
    ]) {
        const dir = path.join(game, root, level);
        if (fs.existsSync(path.join(dir, `${level}.scn.xbx`))) return dir;
    }
    return null;
}

interface Piece {
    name: string;
    sector: Sector;
    min: V3;
    max: V3;
    collision: boolean;
    seeThroughNow: boolean;
    seeThroughNew: boolean;
    top: TopMeasure;
    blends: string;
}

// The editor's see-through test (level_editor_scenery.cpp's GeomSeeThrough): every mesh's material drawn with the
// transparent pass (flag 0x40) or one whose first pass adds, subtracts, modulates or brightens (blend modes 1-4, 7-10:
// light added to or taken from what's behind - never a solid surface; 0 diffuse, 5 blend and 6 fixblend can be).
export function materialSeeThrough(material: Material | undefined, rule: "flag" | "blend"): boolean {
    const pass = material?.passes[0];
    if (!pass) return false;
    if (pass.flags & 0x40) return true;
    const mode = pass.blendMode & 0xffffff;
    return rule === "blend" && mode !== 0 && mode !== 5 && mode !== 6;
}

const USAGE =
    "usage: node tools/pipeline/game/pick_census.ts <level> [--game <folder>] [--all] [--lowflat] [--name <text>] [--ray x,y,z --ray dx,dy,dz]";

function main(): number {
    const { values, positionals } = parseCli(USAGE, {
        options: {
            game: { type: "string", default: "C:/Games/THUG2/Game" },
            all: { type: "boolean", default: false },
            lowflat: { type: "boolean", default: false },
            name: { type: "string" },
            ray: { type: "string", multiple: true },
        },
        allowPositionals: true,
    });
    const level = positionals[0];
    if (!level) return pyExit("give a level (TR, BO, foun, z_ma ...)");
    const dir = findLevel(resolveInstall(values.game as string, "thug2"), level);
    if (!dir) return pyExit(`no ${level}.scn.xbx under ${values.game}`);
    const scene = readScene(fs.readFileSync(path.join(dir, `${level}.scn.xbx`)), level);
    const colPath = path.join(dir, `${level}.col.xbx`);
    const withCollision = new Set<number>();
    if (fs.existsSync(colPath))
        for (const o of readCol(fs.readFileSync(colPath), colPath).objects) withCollision.add(o.checksum >>> 0);
    // (the names only - the 0x2B entries: not every ported level's node array parses as a whole)
    const names = new Map<number, string>();
    for (const [, t, payload] of tokens(fs.readFileSync(path.join(dir, `${level}.qb`))))
        if (t === 0x2b) names.set(payload.readUInt32LE(0), payload.subarray(4, payload.length - 1).toString("latin1"));
    const materials = new Map<number, Material>();
    for (const m of scene.materials) materials.set(m.checksum >>> 0, m);

    const pieces: Piece[] = scene.sectors.map((sector) => {
        const b = sector.bbox;
        const mats = sector.meshes.map((m) => materials.get(m.material >>> 0));
        return {
            name: names.get(sector.checksum >>> 0) ?? hex8(sector.checksum),
            sector,
            min: [b[0]!, b[1]!, b[2]!],
            max: [b[3]!, b[4]!, b[5]!],
            collision: withCollision.has(sector.checksum >>> 0),
            seeThroughNow: mats.length > 0 && mats.every((m) => materialSeeThrough(m, "flag")),
            seeThroughNew: mats.length > 0 && mats.every((m) => materialSeeThrough(m, "blend")),
            top: measureTop(sector.meshes),
            blends: [
                ...new Set(
                    mats.map((m) => (m ? `${m.passes[0]?.blendMode ?? "-"}/${hex8(m.passes[0]?.flags ?? 0)}` : "?")),
                ),
            ].join(","),
        };
    });
    const size = (p: Piece) =>
        `${(p.max[0] - p.min[0]).toFixed(0)} x ${(p.max[1] - p.min[1]).toFixed(0)} x ${(p.max[2] - p.min[2]).toFixed(0)}`;
    const describe = (p: Piece) =>
        `${p.name}  ${size(p)}  coll ${p.collision ? "y" : "n"}  level top ${(levelTopShare(p.top) * 100).toFixed(0)}% of ${p.top.up.toFixed(0)}  blend/flags ${p.blends}` +
        `  see-through ${p.seeThroughNow ? "yes" : "no"} -> ${p.seeThroughNew ? "yes" : "no"}` +
        `  structure ${isStructure(p.min, p.max, null) ? "yes" : "no"} -> ${isStructure(p.min, p.max, p.top) ? "yes" : "no"}`;

    let before = 0,
        after = 0,
        seeBefore = 0,
        seeAfter = 0;
    const changed: Piece[] = [];
    for (const p of pieces) {
        const a = isStructure(p.min, p.max, null),
            b = isStructure(p.min, p.max, p.top);
        before += a ? 1 : 0;
        after += b ? 1 : 0;
        seeBefore += p.seeThroughNow ? 1 : 0;
        seeAfter += p.seeThroughNew ? 1 : 0;
        if (a !== b || p.seeThroughNow !== p.seeThroughNew) changed.push(p);
    }
    print(
        `${level}: ${pieces.length} pieces; structure ${before} -> ${after}; see-through ${seeBefore} -> ${seeAfter}`,
    );
    for (const p of changed) print(`  changed: ${describe(p)}`);
    if (values.all) for (const p of pieces) print(`  ${describe(p)}`);
    // (the pieces the low-and-flat size test catches, whatever the other rules say)
    if (values.lowflat)
        for (const p of pieces)
            if (p.max[1] - p.min[1] <= kLowHeight && Math.min(p.max[0] - p.min[0], p.max[2] - p.min[2]) >= kLowSpan)
                print(`  low and flat: ${describe(p)}`);
    if (values.name) {
        const text = (values.name as string).toLowerCase();
        for (const p of pieces) if (p.name.toLowerCase().includes(text)) print(`  ${describe(p)}`);
    }
    const ray = values.ray as string[] | undefined;
    if (ray && ray.length === 2) {
        const o = ray[0]!.split(",").map(Number) as V3,
            d0 = ray[1]!.split(",").map(Number) as V3;
        const l = Math.sqrt(dot(d0, d0));
        const d: V3 = [d0[0] / l, d0[1] / l, d0[2] / l];
        const hits: [number, Piece][] = [];
        for (const p of pieces) {
            let best = Infinity;
            for (const mesh of p.sector.meshes)
                for (const [a, b, c] of triangles(mesh)) {
                    const t = rayTriangle(o, d, a, b, c);
                    if (t !== null && t < best) best = t;
                }
            if (best < Infinity) hits.push([best, p]);
        }
        hits.sort((x, y) => x[0] - y[0]);
        for (const [t, p] of hits) print(`  t ${t.toFixed(0)}: ${describe(p)}`);
    }
    return 0;
}

if (isMain(import.meta.url)) await runMain(() => main());
