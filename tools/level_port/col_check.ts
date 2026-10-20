// NOT ORIGINAL - offline checks of a THUG2 collision file (levels\<x>\<x>.col.xbx, tools/level_port/thug2_col.ts's
// form): whether every face can be found through its object's BSP tree, as THUG2's queries walk it.
// (TypeScript twin of col_check.py.)
//
//   node tools/level_port/col_check.ts <file.col.xbx | file.prx> ...
//
// A split {axis, point in sixteenths} sends a query to the left child when its low end is below the split, to the
// right one when its high end is at or above it (the trees are built so: a face is in the left child when its lowest
// vertex is below the split, in the right one when its highest is at or above). The query here is a short vertical
// segment through each face's centroid (+-1 unit on y, the axis a skater's feelers mostly run along). A face that
// no such query reaches can't be hit: a floor the skater falls through, a wall he passes (the recentred levels'
// trees once kept their old split points - tools/thaw_levels/thaw_levels.ts moveTree).

import * as fs from "node:fs";
import * as path from "node:path";
import { isMain, print, runMain } from "../lib/py.ts";
import * as prx from "./prx.ts";
import * as thug2Col from "./thug2_col.ts";

function leaves(node: thug2Col.BspNode, lo: readonly number[], hi: readonly number[], out: Set<number>): void {
    if (node.kind === "leaf") {
        for (const f of node.faces) out.add(f);
        return;
    }
    const split = node.point / 16.0;
    if (lo[node.axis]! < split) leaves(node.left, lo, hi, out);
    if (hi[node.axis]! >= split) leaves(node.right, lo, hi, out);
}

// The indices of the faces of collision object `o` that a segment through their centroid doesn't reach.
export function unreachable(o: thug2Col.ColObject): number[] {
    const missing: number[] = [];
    const verts = o.verts;
    o.faces.forEach(([, , a, b, c], i) => {
        const centre = [0, 1, 2].map((k) => (verts[a]![k]! + verts[b]![k]! + verts[c]![k]!) / 3.0);
        const lo = [centre[0]!, centre[1]! - 1.0, centre[2]!];
        const hi = [centre[0]!, centre[1]! + 1.0, centre[2]!];
        const found = new Set<number>();
        leaves(o.bsp, lo, hi, found);
        if (!found.has(i)) missing.push(i);
    });
    return missing;
}

// [faces, unreachable faces, objects with some] of a read collision file.
export function check(col: thug2Col.ColFile): [number, number, number] {
    let faces = 0;
    let missing = 0;
    let objects = 0;
    for (const o of col.objects) {
        faces += o.faces.length;
        const m = unreachable(o).length;
        missing += m;
        objects += m ? 1 : 0;
    }
    return [faces, missing, objects];
}

// [name, data] of the collision files in a .col.xbx or a .prx.
export function colFiles(file: string): [string, Buffer][] {
    if (file.toLowerCase().endsWith(".prx")) return [...prx.entries(file, (n) => n.toLowerCase().endsWith(".col.xbx"))];
    return [[path.basename(file), fs.readFileSync(file)]];
}

export function main(argv: readonly string[] = process.argv.slice(2)): number {
    let bad = 0;
    for (const file of argv) {
        for (const [name, data] of colFiles(file)) {
            const [faces, missing, objects] = check(thug2Col.readCol(data, name));
            print(`${name}: ${faces} faces, ${missing} unreachable (in ${objects} objects)`);
            bad += missing;
        }
    }
    return bad ? 1 : 0;
}

if (isMain(import.meta.url)) await runMain(() => main());
