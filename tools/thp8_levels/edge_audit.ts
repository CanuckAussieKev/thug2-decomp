// NOT ORIGINAL - offline audit of a converted level's collision for open floor edges: ground the skater can ride off with
// no wall in the way and no ground below or beyond, where THUG2's uber frig (no collision within uber_frig_current_height
// feet under the skater: put him back, negate his velocity - src/game/skater/collision_events.h) bounces him from nothing.
// The user met one at SKATE & FUN PARKS' east edge (2026-10-24, docs/pending/thp8-edges.md).
//
//   node tools/thp8_levels/edge_audit.ts [levels...] [--staging DIR] [--top N]
//
// Levels are THP8 level names (thp8_net_skatefunparks, net_skatefunparks, skatefunparks; default every network level in
// the staging folder). Ground: upward collidable faces (normal y >= GROUND_NORMAL), by CELL-inch cells with their heights.
// An edge cell: ground with a side neighbour that has no ground between h - drop and h + STEP_UP in the next REACH cells
// and no wall (a steeper collidable face reaching up over h .. h + WALL_TALL) in them. Edge cells that touch are one
// spot. Not decided: whether THP8 has water or a kill zone there on purpose (the trigger faces aren't ground here, so a
// lake's shore counts) - the spots are where to look.

import * as fs from "node:fs";
import * as path from "node:path";
import * as thug2Col from "../level_port/thug2_col.ts";
import { parseCli } from "../lib/cli.ts";
import { isMain, print, runMain } from "../lib/py.ts";

export const CELL = 24; // inches
export const GROUND_NORMAL = 0.7;
export const DROP = 120; // inches: uber_frig_current_height is feet; its default is about 10
export const STEP_UP = 30;
export const WALL_TALL = 60;
export const REACH = 2; // cells beyond the edge looked at
export const FAR = 20; // cells (about 12 m): ground this far along, at or below the edge, is somewhere to land
export const MIN_COMPONENT = 1500; // ground cells of one connected surface (about 560 m2): smaller ones (a roof, a ledge) aren't looked at
export const LINK_STEP = 24; // inches: neighbouring cells this close in height are one surface
export const BARRIER = thug2Col.FACE_INVISIBLE | thug2Col.FACE_NON_COLLIDABLE | thug2Col.FACE_CAMERA_PASSES;
export const BARRIER_FAR = 80; // cells (about 50 m)
export const MIN_CELLS = 2; // an edge spot is at least this many cells

interface Tri {
    a: number[];
    b: number[];
    c: number[];
}

const key = (cx: number, cz: number): number => (cx + 32768) * 65536 + (cz + 32768);

function normalOf(t: Tri): number[] {
    const u = [0, 1, 2].map((k) => t.b[k]! - t.a[k]!);
    const v = [0, 1, 2].map((k) => t.c[k]! - t.a[k]!);
    const n = [u[1]! * v[2]! - u[2]! * v[1]!, u[2]! * v[0]! - u[0]! * v[2]!, u[0]! * v[1]! - u[1]! * v[0]!];
    const len = Math.hypot(n[0]!, n[1]!, n[2]!) || 1;
    return n.map((x) => x / len);
}

function heightAt(t: Tri, x: number, z: number): number | null {
    const d = (t.b[0]! - t.a[0]!) * (t.c[2]! - t.a[2]!) - (t.c[0]! - t.a[0]!) * (t.b[2]! - t.a[2]!);
    if (Math.abs(d) < 1e-9) return null;
    const u = ((x - t.a[0]!) * (t.c[2]! - t.a[2]!) - (t.c[0]! - t.a[0]!) * (z - t.a[2]!)) / d;
    const v = ((t.b[0]! - t.a[0]!) * (z - t.a[2]!) - (x - t.a[0]!) * (t.b[2]! - t.a[2]!)) / d;
    if (u < -0.01 || v < -0.01 || u + v > 1.02) return null;
    return t.a[1]! + u * (t.b[1]! - t.a[1]!) + v * (t.c[1]! - t.a[1]!);
}

export interface Spot {
    cells: number;
    at: [number, number, number]; // x, y, z of its middle (inches)
    lengthM: number;
    box: [number, number, number, number]; // x0, z0, x1, z1 of its cells (inches)
}

export interface Audit {
    groundCells: number;
    edgeCells: number;
    spots: Spot[]; // open edges with nothing below
    drops: Spot[]; // open edges with ground to land on within FAR cells
    gaps: Spot[]; // open edges with a network barrier ahead and nothing between: the user's case
}

// The ground (cell|height index) of connected surfaces of MIN_COMPONENT cells or more.
function bigSurfaces(ground: ReadonlyMap<number, number[]>): Set<string> {
    const seen = new Set<string>();
    const big = new Set<string>();
    for (const [k0, ys0] of ground) {
        for (let i0 = 0; i0 < ys0.length; i0++) {
            const id0 = `${k0}|${i0}`;
            if (seen.has(id0)) continue;
            seen.add(id0);
            const members = [id0];
            const stack: [number, number][] = [[k0, i0]];
            while (stack.length) {
                const [k, i] = stack.pop()!;
                const h = ground.get(k)![i]!;
                const cz = (k % 65536) - 32768;
                const cx = Math.floor(k / 65536) - 32768;
                for (const [dx, dz] of [
                    [1, 0],
                    [-1, 0],
                    [0, 1],
                    [0, -1],
                ] as const) {
                    const n = key(cx + dx, cz + dz);
                    for (const [j, y] of (ground.get(n) ?? []).entries()) {
                        const id = `${n}|${j}`;
                        if (seen.has(id) || Math.abs(y - h) > LINK_STEP) continue;
                        seen.add(id);
                        members.push(id);
                        stack.push([n, j]);
                    }
                }
            }
            if (members.length >= MIN_COMPONENT) for (const m of members) big.add(m);
        }
    }
    return big;
}

export function audit(file: thug2Col.ColFile): Audit {
    const ground = new Map<number, number[]>();
    const barrier = new Set<number>(); // cells under the network barriers' veil faces
    const walls = new Map<number, [number, number][]>(); // y ranges
    for (const o of file.objects) {
        for (const f of o.faces) {
            const flags = f[0];
            if ((flags & BARRIER) === BARRIER) {
                // (a network barrier's veil: invisible, non-collidable, camera passes - the collidable wall stands in it)
                const p = [o.verts[f[2]]!, o.verts[f[3]]!, o.verts[f[4]]!];
                for (let i = 0; i < 3; i++) {
                    const a = p[i]!;
                    const b = p[(i + 1) % 3]!;
                    const steps = Math.max(1, Math.ceil(Math.hypot(b[0]! - a[0]!, b[2]! - a[2]!) / (CELL / 2)));
                    for (let s = 0; s <= steps; s++) {
                        const x = a[0]! + ((b[0]! - a[0]!) * s) / steps;
                        const z = a[2]! + ((b[2]! - a[2]!) * s) / steps;
                        barrier.add(key(Math.floor(x / CELL), Math.floor(z / CELL)));
                    }
                }
                continue;
            }
            if (flags & (thug2Col.FACE_NON_COLLIDABLE | thug2Col.FACE_TRIGGER)) {
                // (a kill zone's / trigger volume's face: the skater is caught there - a lake's shore is no edge)
                for (const v of [o.verts[f[2]]!, o.verts[f[3]]!, o.verts[f[4]]!]) {
                    const k = key(Math.floor(v[0]! / CELL), Math.floor(v[2]! / CELL));
                    const list = walls.get(k);
                    if (list) list.push([-1e9, 1e9]);
                    else walls.set(k, [[-1e9, 1e9]]);
                }
                continue;
            }
            const t: Tri = {
                a: o.verts[f[2]]!,
                b: o.verts[f[3]]!,
                c: o.verts[f[4]]!,
            };
            const xs = [t.a[0]!, t.b[0]!, t.c[0]!];
            const zs = [t.a[2]!, t.b[2]!, t.c[2]!];
            const ny = Math.abs(normalOf(t)[1]!);
            const cx0 = Math.floor(Math.min(...xs) / CELL);
            const cx1 = Math.floor(Math.max(...xs) / CELL);
            const cz0 = Math.floor(Math.min(...zs) / CELL);
            const cz1 = Math.floor(Math.max(...zs) / CELL);
            if (ny >= GROUND_NORMAL) {
                for (let cx = cx0; cx <= cx1; cx++) {
                    for (let cz = cz0; cz <= cz1; cz++) {
                        for (const [sx, sz] of [
                            [0.25, 0.25],
                            [0.75, 0.25],
                            [0.25, 0.75],
                            [0.75, 0.75],
                        ] as const) {
                            const y = heightAt(t, (cx + sx) * CELL, (cz + sz) * CELL);
                            if (y === null) continue;
                            const k = key(cx, cz);
                            const ys = ground.get(k);
                            if (!ys) ground.set(k, [y]);
                            else if (!ys.some((o2) => Math.abs(o2 - y) < 6)) ys.push(y);
                        }
                    }
                }
            } else {
                const y0 = Math.min(t.a[1]!, t.b[1]!, t.c[1]!);
                const y1 = Math.max(t.a[1]!, t.b[1]!, t.c[1]!);
                for (let cx = cx0; cx <= cx1; cx++) {
                    for (let cz = cz0; cz <= cz1; cz++) {
                        const k = key(cx, cz);
                        const list = walls.get(k);
                        if (list) list.push([y0, y1]);
                        else walls.set(k, [[y0, y1]]);
                    }
                }
            }
        }
    }
    const gap = new Set<number>(); // the floor ends and the barrier is further on, nothing between (the user's case)
    const edge = new Set<number>(); // nothing below at all
    const drop = new Set<number>(); // ground below, further down than DROP
    const at = new Map<number, number>(); // the edge cell's ground height
    const big = bigSurfaces(ground);
    for (const [k, ys] of ground) {
        const cz = (k % 65536) - 32768;
        const cx = Math.floor(k / 65536) - 32768;
        for (const [i, h] of ys.entries()) {
            if (!big.has(`${k}|${i}`)) continue;
            for (const [dx, dz] of [
                [1, 0],
                [-1, 0],
                [0, 1],
                [0, -1],
            ] as const) {
                let open = true;
                for (let r = 0; r <= REACH && open; r++) {
                    const n = key(cx + dx * r, cz + dz * r);
                    if (r > 0 && (ground.get(n) ?? []).some((y) => y >= h - DROP && y <= h + STEP_UP)) open = false;
                    if ((walls.get(n) ?? []).some(([y0, y1]) => y1 >= h && y0 <= h + WALL_TALL)) open = false;
                }
                if (!open) continue;
                // (what lies ahead: ground to land on = a drop; the barrier with nothing between = a gap; else void)
                let kind: "drop" | "gap" | "void" = "void";
                for (let r = REACH + 1; r <= BARRIER_FAR; r++) {
                    const n = key(cx + dx * r, cz + dz * r);
                    if (r <= FAR && (ground.get(n) ?? []).some((y) => y <= h + STEP_UP)) {
                        kind = "drop";
                        break;
                    }
                    if (barrier.has(n)) {
                        kind = "gap";
                        break;
                    }
                }
                (kind === "drop" ? drop : kind === "gap" ? gap : edge).add(k);
                at.set(k, h);
            }
        }
    }
    // (touching edge cells: one spot)
    const cluster = (edge: ReadonlySet<number>): Spot[] => {
        const spots: Spot[] = [];
        const seen = new Set<number>();
        for (const start of edge) {
            if (seen.has(start)) continue;
            const stack = [start];
            seen.add(start);
            const cells: number[] = [];
            while (stack.length) {
                const k = stack.pop()!;
                cells.push(k);
                const cz = (k % 65536) - 32768;
                const cx = Math.floor(k / 65536) - 32768;
                for (let dx = -1; dx <= 1; dx++) {
                    for (let dz = -1; dz <= 1; dz++) {
                        const n = key(cx + dx, cz + dz);
                        if (edge.has(n) && !seen.has(n)) {
                            seen.add(n);
                            stack.push(n);
                        }
                    }
                }
            }
            if (cells.length < MIN_CELLS) continue;
            const mid = (f: (k: number) => number): number => cells.reduce((s, k) => s + f(k), 0) / cells.length;
            spots.push({
                cells: cells.length,
                at: [
                    (mid((k) => Math.floor(k / 65536) - 32768) + 0.5) * CELL,
                    mid((k) => at.get(k)!),
                    (mid((k) => (k % 65536) - 32768) + 0.5) * CELL,
                ],
                lengthM: (cells.length * CELL) / 39.37,
                box: [
                    Math.min(...cells.map((k) => Math.floor(k / 65536) - 32768)) * CELL,
                    Math.min(...cells.map((k) => (k % 65536) - 32768)) * CELL,
                    Math.max(...cells.map((k) => Math.floor(k / 65536) - 32768)) * CELL,
                    Math.max(...cells.map((k) => (k % 65536) - 32768)) * CELL,
                ],
            });
        }
        spots.sort((a, b) => b.cells - a.cells);
        return spots;
    };
    return {
        groundCells: ground.size,
        edgeCells: edge.size,
        spots: cluster(edge),
        drops: cluster(drop),
        gaps: cluster(gap),
    };
}

const USAGE = "usage: edge_audit.ts [levels...] [--staging DIR] [--top N]";

export function main(argv: readonly string[] = process.argv.slice(2)): number {
    const { values, positionals } = parseCli(USAGE, {
        args: [...argv],
        allowPositionals: true,
        options: { staging: { type: "string" }, top: { type: "string" } },
    });
    const staging = values.staging ?? "C:\\Games\\THUG2-4\\thug2-decomp-thp8";
    const levelsDir = path.join(staging, "levels");
    const all = fs
        .readdirSync(levelsDir)
        .filter((d) => d.startsWith("thp8_net_"))
        .sort();
    const wanted = positionals.length
        ? positionals.map((p) => (p.startsWith("thp8_") ? p : p.startsWith("net_") ? `thp8_${p}` : `thp8_net_${p}`))
        : all;
    const top = Number(values.top ?? 8);
    for (const level of wanted) {
        const file = path.join(levelsDir, level, `${level}.col.xbx`);
        if (!fs.existsSync(file)) {
            print(`${level}: no ${file}`);
            continue;
        }
        const a = audit(thug2Col.readCol(fs.readFileSync(file), level));
        const metres = (l: readonly Spot[]): string => l.reduce((n, s) => n + s.lengthM, 0).toFixed(0);
        print(
            `${level}: ${a.gaps.length} floor edges short of a barrier (${metres(a.gaps)} m), ` +
                `${a.spots.length} open edges with nothing below (${metres(a.spots)} m), ` +
                `${a.drops.length} drops to ground within ${FAR} cells (${metres(a.drops)} m); ${a.groundCells} ground cells`,
        );
        for (const [name, list] of [
            ["gap", a.gaps],
            ["void", a.spots],
        ] as const) {
            for (const s of list.slice(0, top))
                print(
                    `    ${name} ${s.lengthM.toFixed(0)} m at ${s.at.map((v) => v.toFixed(0)).join(",")} (x ${s.box[0]}..${s.box[2]}, z ${s.box[1]}..${s.box[3]})`,
                );
        }
    }
    return 0;
}

if (isMain(import.meta.url)) runMain(main);
