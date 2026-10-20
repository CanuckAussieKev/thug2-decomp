// THUG2 PC collision files (levels\<x>\<x>.col.xbx), version 10. The layout, as the game's loader takes it apart
// (0x0049CBB0, the per-object fix-ups 0x004169C0 and 0x00410BC0): (TypeScript twin of thug2_col.py)
//
//   header      i32 version (10), object count, total vertices, large faces, small faces, large (float) vertices,
//               small (fixed-point) vertices, 0
//   objects     64 bytes each: u32 checksum (the scene sector / node it belongs to), u16 flags, u16 vertex count,
//               u16 face count, u8 small faces, u8 fixed-point vertices, u32 first face (byte offset in the faces),
//               4f box min, 4f box max, u32 first vertex (byte offset), u32 BSP root (byte offset in the nodes),
//               u32 first intensity (index), f32 reach scale (below)
//   (aligned to 16)
//   vertices    float: 3f; fixed-point: 3 x u16 sixteenths above the object's box minimum
//   intensities a byte per vertex
//   (aligned to 4)
//   faces       large: u16 flags, u16 terrain, 3 x u16 vertex; small: u16 flags, u16 terrain, 3 x u8 vertex, u8 reach
//   reaches     a byte per large face (aligned to 4 after)
//   u32 size of the BSP nodes, nodes (8 bytes: a split {i32 axis | (sixteenths of the split point << 2),
//               u32 byte offset of the left child, the right one after it} or a leaf {u8 3, u8 0, u16 face count,
//               u32 first face index}), then the leaves' face indices (u16, per object)
//
// A face's reach byte is how far its farthest vertex is from the object's box minimum, times the object's reach
// scale (254 / the farthest vertex of the object), rounded up. A ray test (0x00413B50) skips the faces whose reach is
// below the ray's distance from the box minimum times the scale: 0 there makes a face all but uncollidable.

import { hex8, pyDist, pyMax, pyMin, roundHalfEven } from "../lib/py.ts";
import { f32, pack } from "../lib/struct.ts";
import { FormatError, Reader, Writer } from "./binio.ts";

export const VERSION = 10;

// Face flags.
export const FACE_SKATABLE = 0x0001;
export const FACE_NOT_SKATABLE = 0x0002;
export const FACE_WALL_RIDABLE = 0x0004;
export const FACE_VERT = 0x0008;
export const FACE_NON_COLLIDABLE = 0x0010;
export const FACE_DECAL = 0x0020;
export const FACE_TRIGGER = 0x0040;
export const FACE_CAMERA_COLLIDABLE = 0x0080;
export const FACE_NO_SKATER_SHADOW = 0x0100;
export const FACE_SKATER_SHADOW = 0x0200;
export const FACE_NO_SKATER_SHADOW_WALL = 0x0400;
export const FACE_UNDER_OK = 0x0800;
export const FACE_INVISIBLE = 0x1000;

// A face: (flags, terrain, a, b, c).
export type Face = [number, number, number, number, number];

// A BSP node: a leaf (its face indices; `low`: the low 16 bits of its first word, 3 as built) or a split.
export type BspNode =
    | { kind: "leaf"; faces: number[]; low: number }
    | { kind: "split"; axis: number; point: number; left: BspNode; right: BspNode };

export interface ColObject {
    checksum: number;
    flags: number;
    smallFaces: number;
    fixedVerts: number;
    bbox: number[]; // 8f: min (x, y, z, 1), max (x, y, z, 1)
    value?: Buffer; // 4 bytes: the reach scale (f32)
    verts: number[][];
    intensities: Buffer;
    faces: Face[];
    reach: Buffer;
    bsp: BspNode;
    bspOff?: number; // (read: the root's offset)
}

export interface ColFile {
    objects: ColObject[];
    pad?: number;
    version?: number;
    nodeSize?: number; // (read: the nodes' size)
    indexCount?: number; // (read: the face indices after the nodes)
}

export function readCol(data: Uint8Array, name = "col", versions: readonly number[] = [VERSION]): ColFile {
    // versions: [9] reads THUG1's collision, which is version 10 without the reaches: no byte array after the large
    // faces, the small faces' 4th byte 0 (padding), the objects' reach scale 0.
    const r = new Reader(data, 0, name);
    const [version, count, totalVerts, largeFaces, smallFaces, largeVerts, smallVerts, pad] = r.read(
        "8i",
    ) as number[] as [number, number, number, number, number, number, number, number];
    if (!versions.includes(version)) {
        throw new FormatError(`${name}: collision version ${version}, not ${versions.join(" / ")}`);
    }
    const hasReaches = version >= 10;
    const objects: ColObject[] = [];
    const counts: [number, number][] = [];
    const offsets: [number, number, number, number][] = [];
    for (let k = 0; k < count; k++) {
        const [checksum, flags, nverts, nfaces, small, fixed, faceOff] = r.read("IHHHBBI") as [
            number,
            number,
            number,
            number,
            number,
            number,
            number,
        ];
        const bbox = r.read("8f");
        const [vertOff, bspOff, intensityOff] = r.read("3I") as [number, number, number];
        const value = r.bytes(4);
        objects.push({
            checksum,
            flags,
            smallFaces: small,
            fixedVerts: fixed,
            bbox,
            value,
            verts: [],
            intensities: Buffer.alloc(0),
            faces: [],
            reach: Buffer.alloc(0),
            bsp: { kind: "leaf", faces: [], low: 3 },
        });
        counts.push([nverts, nfaces]);
        offsets.push([faceOff, vertOff, bspOff, intensityOff]);
    }
    const vertBase = (r.offset + 15) & ~15;
    const intensityBase = vertBase + largeVerts * 12 + smallVerts * 6;
    const faceBase = (intensityBase + totalVerts + 3) & ~3;
    const faceSize = largeFaces * 10 + smallFaces * 8;
    const faceBytesBase = faceBase + faceSize;
    const nodeSizeAt = (faceBytesBase + (hasReaches ? largeFaces : 0) + 3) & ~3;
    r.offset = nodeSizeAt;
    const nodeSize = r.u32();
    const nodeBase = r.offset;
    const indexBase = nodeBase + nodeSize;
    let faceBytesAt = faceBytesBase;
    const bytes = Buffer.isBuffer(data) ? data : Buffer.from(data.buffer, data.byteOffset, data.byteLength);
    objects.forEach((o, k) => {
        const [nverts, nfaces] = counts[k]!;
        const [faceOff, vertOff, bspOff, intensityOff] = offsets[k]!;
        const v = new Reader(data, vertBase + vertOff, name);
        if (o.fixedVerts) {
            for (let n = 0; n < nverts; n++) o.verts.push(v.read("3H").map((c, i) => o.bbox[i]! + c / 16.0));
        } else {
            for (let n = 0; n < nverts; n++) o.verts.push(v.read("3f"));
        }
        o.intensities = Buffer.from(
            bytes.subarray(intensityBase + intensityOff, intensityBase + intensityOff + nverts),
        );
        const f = new Reader(data, faceBase + faceOff, name);
        const reaches: number[] = [];
        for (let n = 0; n < nfaces; n++) {
            if (o.smallFaces) {
                const [flags, terrain, a, b, c, reach] = f.read("HH4B") as [
                    number,
                    number,
                    number,
                    number,
                    number,
                    number,
                ];
                reaches.push(reach);
                o.faces.push([flags, terrain, a, b, c]);
            } else {
                const [flags, terrain, a, b, c] = f.read("HH3H") as Face;
                o.faces.push([flags, terrain, a, b, c]);
            }
        }
        if (o.smallFaces) {
            o.reach = Buffer.from(reaches);
        } else if (!hasReaches) {
            o.reach = Buffer.alloc(nfaces);
        } else {
            o.reach = Buffer.from(bytes.subarray(faceBytesAt, faceBytesAt + nfaces));
            faceBytesAt += nfaces;
        }
        o.bsp = readTree(data, nodeBase, bspOff, indexBase, name);
        o.bspOff = bspOff;
    });
    // (the nodes are read per object from its root; any not reached are a format surprise)
    return { objects, pad, version, nodeSize, indexCount: Math.floor((data.length - indexBase) / 2) };
}

export function readTree(data: Uint8Array, nodeBase: number, offset: number, indexBase: number, name: string): BspNode {
    const r = new Reader(data, nodeBase + offset, name);
    const [word, second] = r.read("iI") as [number, number];
    if ((word & 3) === 3) {
        const count = (word >> 16) & 0xffff;
        const ir = new Reader(data, indexBase + second * 2, name);
        return { kind: "leaf", faces: ir.read(`${count}H`), low: word & 0xffff };
    }
    const axis = word & 3;
    const point = word >> 2;
    const left = readTree(data, nodeBase, second, indexBase, name);
    const right = readTree(data, nodeBase, second + 8, indexBase, name);
    return { kind: "split", axis, point, left, right };
}

export function writeCol(col: ColFile): Buffer {
    const objects = col.objects;
    const w = new Writer();
    let totalVerts = 0;
    let largeFaces = 0;
    let smallFaces = 0;
    let largeVerts = 0;
    let smallVerts = 0;
    for (const o of objects) {
        totalVerts += o.verts.length;
        if (o.smallFaces) smallFaces += o.faces.length;
        else largeFaces += o.faces.length;
        if (o.fixedVerts) smallVerts += o.verts.length;
        else largeVerts += o.verts.length;
    }
    w.write("8i", VERSION, objects.length, totalVerts, largeFaces, smallFaces, largeVerts, smallVerts, col.pad ?? 0);
    // (float vertices first, then the fixed-point ones, as the shipped files)
    const largeVertsOut = new Writer();
    const smallVertsOut = new Writer();
    const intensities = new Writer();
    const faces = new Writer();
    const faceBytes = new Writer();
    const nodes = new Writer();
    const indices = new Writer();
    for (const o of objects) {
        if (o.verts.length > 0xffff || o.faces.length > 0xffff) {
            throw new FormatError(`collision object ${hex8(o.checksum)} too big`);
        }
        const root = nodes.tell();
        writeTree(o.bsp, nodes, indices);
        w.write(
            "IHHHBBI",
            o.checksum,
            o.flags,
            o.verts.length,
            o.faces.length,
            o.smallFaces,
            o.fixedVerts,
            faces.tell(),
        );
        w.write("8f", ...o.bbox);
        const verts = o.fixedVerts ? smallVertsOut : largeVertsOut;
        w.write("3I", verts.tell() + (o.fixedVerts ? largeVerts * 12 : 0), root, intensities.tell());
        w.raw(o.value ?? Buffer.alloc(4));
        for (const v of o.verts) {
            if (o.fixedVerts) {
                verts.write("3H", ...[0, 1, 2].map((k) => roundHalfEven((v[k]! - o.bbox[k]!) * 16.0)));
            } else {
                verts.write("3f", ...v);
            }
        }
        intensities.raw(o.intensities);
        const reach = o.reach;
        if (reach.length !== o.faces.length) {
            throw new FormatError(
                `collision object ${hex8(o.checksum)}: ${reach.length} reaches for ${o.faces.length} faces`,
            );
        }
        o.faces.forEach(([flags, terrain, a, b, c], i) => {
            if (o.smallFaces) faces.write("HH4B", flags, terrain, a, b, c, reach[i]!);
            else faces.write("HH3H", flags, terrain, a, b, c);
        });
        if (!o.smallFaces) faceBytes.raw(reach);
    }
    w.padTo(16);
    w.raw(largeVertsOut.getvalue());
    w.raw(smallVertsOut.getvalue());
    w.raw(intensities.getvalue());
    w.padTo(4);
    w.raw(faces.getvalue());
    w.raw(faceBytes.getvalue());
    w.padTo(4);
    w.u32(nodes.tell());
    w.raw(nodes.getvalue());
    w.raw(indices.getvalue());
    return w.getvalue();
}

export function writeTree(tree: BspNode, nodes: Writer, indices: Writer): void {
    // Depth first, as the shipped files: a split's two children side by side, then the left child's subtree, then
    // the right's; the leaves' face indices in the order of the leaves' nodes.
    const leaves: [number, { faces: number[]; low: number }][] = [];

    const emit = (node: BspNode, at: number): void => {
        if (node.kind === "leaf") {
            leaves.push([at, node]);
        } else {
            const children = nodes.tell();
            nodes.raw(Buffer.alloc(16));
            // ((point << 2) | axis: the same for a negative point, without JavaScript's 32-bit wrap)
            nodes.patch(at, "iI", node.point * 4 + node.axis, children);
            emit(node.left, children);
            emit(node.right, children + 8);
        }
    };

    const root = nodes.tell();
    nodes.raw(Buffer.alloc(8));
    emit(tree, root);
    leaves.sort((p, q) => p[0] - q[0]);
    for (const [at, node] of leaves) {
        const faces = node.faces;
        nodes.patch(at, "iI", faces.length * 65536 + (node.low & 0xffff), Math.floor(indices.tell() / 2));
        indices.write(`${faces.length}H`, ...faces);
    }
}

// --- building collision for new geometry ----------------------------------------------------------------------

export function setReaches(o: ColObject): void {
    // The reach scale and each face's reach (see the top): as the shipped files (to within one; one more is safe -
    // it only lets the ray test look at the face).
    const origin = o.bbox.slice(0, 3);
    if (!o.faces.length) {
        o.value = pack("<f", 1.0);
        o.reach = Buffer.alloc(0);
        return;
    }
    // (Python's math.dist, not a plain square root of the sum: pyDist)
    let farthest = pyMax(o.verts.map((v) => pyDist(v, origin)));
    if (!farthest) farthest = 1.0;
    const scale = f32(254.0 / farthest);
    o.value = pack("<f", scale);
    o.reach = Buffer.from(
        o.faces.map((face) => {
            const far = pyMax([face[2], face[3], face[4]].map((i) => pyDist(vertex(o.verts, i), origin)));
            const value = Math.min(255, Math.ceil(far * scale) + 1);
            if (Number.isNaN(value)) throw new RangeError("cannot convert float NaN to integer");
            return value;
        }),
    );
}

// verts[i], raising past the end as Python's list (a missing vertex would otherwise be undefined -> NaN).
function vertex(verts: readonly (readonly number[])[], i: number): readonly number[] {
    const v = verts[i < 0 ? verts.length + i : i];
    if (v === undefined) throw new RangeError("list index out of range");
    return v;
}

export const LEAF_FACES = 50; // faces in a leaf before it's split (as the shipped trees: leaves of up to ~50 faces)

export function buildTree(verts: readonly (readonly number[])[], faces: readonly Face[]): BspNode {
    // A BSP tree over the faces (indices into `faces`): split at the median of the vertex coordinates on the axis that
    // duplicates fewest faces, the split point in sixteenths of a unit (the file's fixed point).
    const coord = (f: number, i: number, axis: number): number => verts[faces[f]![i]!]![axis]!;
    const build = (faceIds: number[], depth: number, stuck: ReadonlySet<number>): BspNode => {
        if (faceIds.length <= LEAF_FACES || depth > 40) return { kind: "leaf", faces: [...faceIds], low: 3 };
        let best: [number, number, number, number[], number[]] | null = null;
        for (let axis = 0; axis < 3; axis++) {
            if (stuck.has(axis)) continue;
            const coords: number[] = [];
            for (const f of faceIds) for (const i of [2, 3, 4]) coords.push(coord(f, i, axis));
            coords.sort((a, b) => a - b);
            const point = Math.floor(coords[Math.floor(coords.length / 2)]! * 16.0);
            const split = point / 16.0;
            const left: number[] = [];
            const right: number[] = [];
            for (const f of faceIds) {
                const values = [coord(f, 2, axis), coord(f, 3, axis), coord(f, 4, axis)];
                if (pyMin(values) < split) left.push(f);
                if (pyMax(values) >= split) right.push(f);
            }
            const duplicates = left.length + right.length - faceIds.length;
            if (best === null || duplicates < best[0]) best = [duplicates, axis, point, left, right];
        }
        if (best === null) return { kind: "leaf", faces: [...faceIds], low: 3 };
        const [duplicates, axis, point, left, right] = best;
        if (duplicates >= Math.floor(faceIds.length / 2) || !left.length || !right.length) {
            return { kind: "leaf", faces: [...faceIds], low: 3 };
        }
        const stuckLeft = left.length === faceIds.length ? new Set([...stuck, axis]) : stuck;
        const stuckRight = right.length === faceIds.length ? new Set([...stuck, axis]) : stuck;
        return {
            kind: "split",
            axis,
            point,
            left: build(left, depth + 1, stuckLeft),
            right: build(right, depth + 1, stuckRight),
        };
    };
    return build(
        faces.map((_, i) => i),
        0,
        new Set(),
    );
}
