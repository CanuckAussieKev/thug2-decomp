// NOT ORIGINAL - the edges of THP8's network levels: ground of the city's other zones that lies inside a level's network
// barrier. THP8 streams the whole city and closes a network level off with its z_online_<x> barrier walls, which stand
// well beyond the level's own zones - SKATE & FUN PARKS' barrier is 600 inches (15 m) east of the skate park's floor,
// round z_outschool's ground; THP8's skater rides on over it. Our network level holds only its spawn_in zones' collision,
// so the floor ended in the void there and THUG2's uber frig (no ground under the skater: turn him round, tools/game
// collision_events.ts "uber-frig") bounced him back from nothing (the user, 2026-10-24, thp8_net_skatefunparks).
// Here: the ground the skater can roll to from the level's own zones before a barrier or a drop stops him, and the other
// zones' collision triangles over it, which convertLevel adds as collision (their drawn scenes stay the _lod stand-ins).

import * as thug2Col from "../level_port/thug2_col.ts";
import * as havok from "./havok.ts";
import * as thp8 from "./thp8.ts";

const NOT_SOLID = thug2Col.FACE_NON_COLLIDABLE | 0x40; // non-collidable, trigger

const centre = (t: havok.HkTriangle): [number, number] => [
    (t.a[0]! + t.b[0]! + t.c[0]!) / 3,
    (t.a[2]! + t.b[2]! + t.c[2]!) / 3,
];

// What a network barrier closes off is found by walking: a barrier isn't a closed fence (it stands only where THP8's
// level has no edge of its own - cliffs, buildings, water - so its plan leaks), but the skater can only roll where there
// is ground. From the ground of the level's own zones, cell by cell (REACH_CELL metres), onto any ground cell whose
// height is within REACH_STEP of the one he came from (a ledge he'd jump off, a ramp), not into a barrier wall's cells:
// the cells reached are what THP8's skater can roll to. The other zones' collision triangles over them (and REACH_ROOM
// cells round them: a wall or a kerb beside the ground) are the level's too.
export const REACH_CELL = 1; // metres
export const REACH_STEP = 2.5; // metres of height between neighbouring ground cells he can roll across
export const REACH_ROOM = 2; // cells
const REACH_FLAT = 0.5; // the least y of a ground triangle's normal (a quarter pipe's lower part still is)

const cellKey = (cx: number, cz: number): number => (cx + 32768) * 65536 + (cz + 32768);
const cellOf = (v: number): number => Math.floor(v / REACH_CELL);

function normalY(t: havok.HkTriangle): number {
    const u = [0, 1, 2].map((k) => t.b[k]! - t.a[k]!);
    const v = [0, 1, 2].map((k) => t.c[k]! - t.a[k]!);
    const n = [u[1]! * v[2]! - u[2]! * v[1]!, u[2]! * v[0]! - u[0]! * v[2]!, u[0]! * v[1]! - u[1]! * v[0]!];
    return Math.abs(n[1]!) / (Math.hypot(n[0]!, n[1]!, n[2]!) || 1);
}

function heightAt(t: havok.HkTriangle, x: number, z: number): number | null {
    const [a, p, c] = [t.a, t.b, t.c];
    const d = (p[0]! - a[0]!) * (c[2]! - a[2]!) - (c[0]! - a[0]!) * (p[2]! - a[2]!);
    if (Math.abs(d) < 1e-9) return null;
    const u = ((x - a[0]!) * (c[2]! - a[2]!) - (c[0]! - a[0]!) * (z - a[2]!)) / d;
    const v = ((p[0]! - a[0]!) * (z - a[2]!) - (x - a[0]!) * (p[2]! - a[2]!)) / d;
    if (u < -0.02 || v < -0.02 || u + v > 1.04) return null;
    return a[1]! + u * (p[1]! - a[1]!) + v * (c[1]! - a[1]!);
}

// The ground heights of each cell (the cell middle's height on each upward collidable triangle over it).
export type Ground = Map<number, number[]>;

export function groundCells(bodies: readonly (readonly havok.HkBody[])[], into: Ground = new Map()): Ground {
    for (const list of bodies) {
        for (const b of list) {
            for (const t of b.triangles) {
                if (t.material & NOT_SOLID || normalY(t) < REACH_FLAT) continue;
                const xs = [t.a[0]!, t.b[0]!, t.c[0]!];
                const zs = [t.a[2]!, t.b[2]!, t.c[2]!];
                for (let cx = cellOf(Math.min(...xs)); cx <= cellOf(Math.max(...xs)); cx++) {
                    for (let cz = cellOf(Math.min(...zs)); cz <= cellOf(Math.max(...zs)); cz++) {
                        const y = heightAt(t, (cx + 0.5) * REACH_CELL, (cz + 0.5) * REACH_CELL);
                        if (y === null) continue;
                        const k = cellKey(cx, cz);
                        const ys = into.get(k);
                        if (!ys) into.set(k, [y]);
                        else if (!ys.some((o) => Math.abs(o - y) < 0.3)) ys.push(y);
                    }
                }
            }
        }
    }
    return into;
}

// The cells of the barrier's walls (a face's footprint, one cell round).
export function wallCells(barrier: readonly havok.HkBody[]): Set<number> {
    const out = new Set<number>();
    for (const b of barrier) {
        for (const t of b.triangles) {
            if (t.material & NOT_SOLID || normalY(t) > 0.5) continue;
            const pts = [t.a, t.b, t.c];
            for (let i = 0; i < 3; i++) {
                const p = pts[i]!;
                const q = pts[(i + 1) % 3]!;
                const steps = Math.max(1, Math.ceil(Math.hypot(q[0]! - p[0]!, q[2]! - p[2]!) / (REACH_CELL / 4)));
                for (let s = 0; s <= steps; s++) {
                    const cx = cellOf(p[0]! + ((q[0]! - p[0]!) * s) / steps);
                    const cz = cellOf(p[2]! + ((q[2]! - p[2]!) * s) / steps);
                    for (let dx = -1; dx <= 1; dx++)
                        for (let dz = -1; dz <= 1; dz++) out.add(cellKey(cx + dx, cz + dz));
                }
            }
        }
    }
    return out;
}

// How far (cells, chessboard distance) the barrier's walls stand from the level's own ground: the largest of them
// (REACH_SLACK more) - what the barrier closes off lies within that of the ground, wherever its gaps lead. 0 when the
// walls are no further than REACH_FOUND from any.
export const REACH_FOUND = 160;
export const REACH_SLACK = 16;
export function barrierReach(seeds: Ground, walls: ReadonlySet<number>): number {
    const dist = new Map<number, number>();
    let frontier = [...seeds.keys()];
    for (const k of frontier) dist.set(k, 0);
    for (let d = 1; d <= REACH_FOUND && frontier.length; d++) {
        const next: number[] = [];
        for (const k of frontier) {
            const cz = (k % 65536) - 32768;
            const cx = Math.floor(k / 65536) - 32768;
            for (let dx = -1; dx <= 1; dx++) {
                for (let dz = -1; dz <= 1; dz++) {
                    const n = cellKey(cx + dx, cz + dz);
                    if (dist.has(n)) continue;
                    dist.set(n, d);
                    next.push(n);
                }
            }
        }
        frontier = next;
    }
    let most = 0;
    for (const k of walls) most = Math.max(most, dist.get(k) ?? 0);
    return most + REACH_SLACK;
}

// The cells the skater reaches from `seeds`' ground (cell keys) over `ground`, stopped by `walls`.
export function reached(ground: Ground, seeds: Ground, walls: ReadonlySet<number>, within: number): Set<number> {
    const seen = new Set<string>();
    const cells = new Set<number>();
    const queue: [number, number, number][] = [];
    const near = nearness(seeds, within);
    const push = (k: number, y: number): void => {
        const id = `${k}|${Math.round(y / 0.5)}`;
        if (seen.has(id)) return;
        seen.add(id);
        cells.add(k);
        queue.push([k, y, 0]);
    };
    for (const [k, ys] of seeds) if (!walls.has(k)) for (const y of ys) push(k, y);
    for (let i = 0; i < queue.length; i++) {
        const [k, y] = queue[i]!;
        const cz = (k % 65536) - 32768;
        const cx = Math.floor(k / 65536) - 32768;
        for (const [dx, dz] of [
            [1, 0],
            [-1, 0],
            [0, 1],
            [0, -1],
        ] as const) {
            const n = cellKey(cx + dx, cz + dz);
            if (walls.has(n) || !near.has(n)) continue;
            for (const ny of ground.get(n) ?? []) if (Math.abs(ny - y) <= REACH_STEP) push(n, ny);
        }
    }
    return cells;
}

// The cells within `within` cells (chessboard) of the seeds' ground.
function nearness(seeds: Ground, within: number): Set<number> {
    const seen = new Set<number>(seeds.keys());
    let frontier = [...seen];
    for (let d = 1; d <= within && frontier.length; d++) {
        const next: number[] = [];
        for (const k of frontier) {
            const cz = (k % 65536) - 32768;
            const cx = Math.floor(k / 65536) - 32768;
            for (let dx = -1; dx <= 1; dx++) {
                for (let dz = -1; dz <= 1; dz++) {
                    const n = cellKey(cx + dx, cz + dz);
                    if (seen.has(n)) continue;
                    seen.add(n);
                    next.push(n);
                }
            }
        }
        frontier = next;
    }
    return seen;
}

// The cells of `reach` and the cells round them.
export function withRoom(reach: ReadonlySet<number>): Set<number> {
    const out = new Set<number>();
    for (const k of reach) {
        const cz = (k % 65536) - 32768;
        const cx = Math.floor(k / 65536) - 32768;
        for (let dx = -REACH_ROOM; dx <= REACH_ROOM; dx++)
            for (let dz = -REACH_ROOM; dz <= REACH_ROOM; dz++) out.add(cellKey(cx + dx, cz + dz));
    }
    return out;
}

// The bodies of a zone (collidable triangles only) with a triangle whose middle lies in `cells`: whole - a wall box
// cut to the part over the cells would be a few loose faces (z_outschool's 1.2 m fence along the skate park's edge: only
// its inner face is over the reached cells; as a whole it is the wall the skater met there in THP8).
export function inCells(bodies: readonly havok.HkBody[], cells: ReadonlySet<number>): havok.HkBody[] {
    const out: havok.HkBody[] = [];
    for (const b of bodies) {
        const triangles = b.triangles.filter((t) => !(t.material & NOT_SOLID));
        const touches = triangles.some((t) => {
            const [x, z] = centre(t);
            return cells.has(cellKey(cellOf(x), cellOf(z)));
        });
        if (touches) out.push({ index: b.index, triangles });
    }
    return out;
}

// A zone's collision bodies alone (not its scene or textures), read once.
export class ZoneBodies {
    private readonly cache = new Map<string, havok.HkBody[]>();
    private readonly zonePak: (zone: string) => string | null;
    constructor(zonePak: (zone: string) => string | null) {
        this.zonePak = zonePak;
    }

    of(zone: string, warnings: string[]): havok.HkBody[] {
        let bodies = this.cache.get(zone);
        if (bodies === undefined) {
            const file = this.zonePak(zone);
            bodies = [];
            if (file !== null) {
                const pak = thp8.openPak(file);
                const h = pak.entries.find((e) => e.flags === 0 && e.extension === ".hkc");
                if (h) bodies = havok.readCollision(new havok.Packfile(thp8.entryData(pak, h), file), warnings);
            }
            this.cache.set(zone, bodies);
        }
        return bodies;
    }
}

// --- drawing the border zones -------------------------------------------------------------------------------------

// Whether a triangle (corners in THP8 metres) has a point over `cells`: its corners, middle and a grid on it no wider
// than DRAW_STEP metres (a big ground triangle with its corners outside the reached cells still lies over them).
export const DRAW_STEP = 2;
export function triangleInCells(corners: readonly (readonly number[])[], cells: ReadonlySet<number>): boolean {
    const [a, b, c] = corners as [readonly number[], readonly number[], readonly number[]];
    const edge = (p: readonly number[], q: readonly number[]): number => Math.hypot(p[0]! - q[0]!, p[2]! - q[2]!);
    const n = Math.max(2, Math.min(60, Math.ceil(Math.max(edge(a, b), edge(b, c), edge(c, a)) / DRAW_STEP)));
    for (let i = 0; i <= n; i++) {
        for (let j = 0; i + j <= n; j++) {
            const u = i / n;
            const v = j / n;
            const x = a[0]! + u * (b[0]! - a[0]!) + v * (c[0]! - a[0]!);
            const z = a[2]! + u * (b[2]! - a[2]!) + v * (c[2]! - a[2]!);
            if (cells.has(cellKey(cellOf(x), cellOf(z)))) return true;
        }
    }
    return false;
}
