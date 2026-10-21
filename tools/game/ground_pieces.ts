// NOT ORIGINAL - the level editor's raised / lowered ground pieces for the bake (UX9): the DLL's generator
// (src/game/front/level_editor_ground.cpp - Ground::Build, QuarterProfile, Strip, LayoutFor, WriteVertices, ReadRecord)
// ported, and the piece made into the level's files: a new scene sector (the donor's first mesh's material and vertex
// format, the generated vertices and a strip drawing each face both ways) and a collision object (the generated faces,
// their flags and terrain, its tree and reaches built), both named after the piece (ed_ground01 ...); a pit's floor
// tiles (`hides`) left out of the scene and collision, as the DLL hides them.
//
// Record (level_editor_ground.h):
//   ground <name> raise|lower rect=x0,z0,x1,z1 base=y height=h ramps=<-x>,<+x>,<-z>,<+z> length=l material=<sector>
//          uv=u color=AARRGGBB terrain=t [hides=<sector>,...]

import { checksum } from "../level_port/qb.ts";
import { buildTree, type Face as ColFace, type ColObject, setReaches } from "../level_port/thug2_col.ts";
import { type Mesh as SceneMesh, SEC_BILLBOARD, type Sector } from "../level_port/thug2_scene.ts";

export interface V3 {
    x: number;
    y: number;
    z: number;
}
export type Ramp = "-" | "k" | "q";
export const MINUS_X = 0;
export const PLUS_X = 1;
export const MINUS_Z = 2;
export const PLUS_Z = 3;

export interface GroundSpec {
    name: string;
    lower: boolean;
    x0: number;
    z0: number;
    x1: number;
    z1: number;
    base: number;
    height: number;
    ramps: [Ramp, Ramp, Ramp, Ramp];
    length: number;
    material: number;
    uv: number;
    colour: number;
    terrain: number;
    hides: number[];
}

export const LIMITS = { minHeight: 10, maxHeight: 2000, minSide: 50, maxSide: 8000, maxLength: 4000 } as const;
export const SINK = 4;
export const QUARTER_SEGMENTS = 8;
export const FLAG_DECK = 0x0001;
export const FLAG_RAMP = 0x0001;
export const FLAG_VERT = 0x0009;
export const FLAG_WALL = 0x0004;

const clamp = (v: number, lo: number, hi: number): number => Math.max(lo, Math.min(hi, v));

export function normalise(s: GroundSpec): boolean {
    for (const v of [s.x0, s.x1, s.z0, s.z1, s.base, s.height, s.length, s.uv]) if (!Number.isFinite(v)) return false;
    if (s.x1 < s.x0) [s.x0, s.x1] = [s.x1, s.x0];
    if (s.z1 < s.z0) [s.z0, s.z1] = [s.z1, s.z0];
    s.x1 = s.x0 + clamp(s.x1 - s.x0, LIMITS.minSide, LIMITS.maxSide);
    s.z1 = s.z0 + clamp(s.z1 - s.z0, LIMITS.minSide, LIMITS.maxSide);
    s.height = clamp(s.height, LIMITS.minHeight, LIMITS.maxHeight);
    s.length = clamp(s.length, 0, LIMITS.maxLength);
    s.uv = clamp(s.uv, 16, 4096);
    return true;
}

export function rampLength(s: GroundSpec, side: number): number {
    const ramp = s.ramps[side]!;
    if (ramp === "-") return 0;
    let length = s.length > 0 ? s.length : ramp === "k" ? 2 * s.height : s.height;
    length = Math.max(10, length);
    if (s.lower) {
        const across = side === MINUS_X || side === PLUS_X ? s.x1 - s.x0 : s.z1 - s.z0;
        const room = s.ramps[side ^ 1] !== "-" ? across * 0.5 - 10 : across - 10;
        length = Math.max(10, Math.min(length, room));
    }
    return length;
}

export function quarterProfile(radius: number, height: number, segments: number): [number, number][] {
    const points: [number, number][] = [];
    segments = Math.max(1, segments);
    const rise = Math.min(height, radius);
    const top = Math.acos(clamp(1 - rise / radius, -1, 1));
    for (let i = 0; i <= segments; i++) {
        const t = (top * i) / segments;
        points.push(i === segments ? [0, rise] : [radius * (Math.sin(top) - Math.sin(t)), radius * (1 - Math.cos(t))]);
    }
    if (height > radius + 0.01) points.push([0, height]);
    return points;
}

export interface Vertex {
    pos: V3;
    normal: V3;
    u: number;
    v: number;
}
export interface Face {
    a: number;
    b: number;
    c: number;
    flags: number;
}
export interface GroundMesh {
    vertices: Vertex[];
    faces: Face[];
    min: V3;
    max: V3;
}

const add = (a: V3, b: V3): V3 => ({ x: a.x + b.x, y: a.y + b.y, z: a.z + b.z });
const sub = (a: V3, b: V3): V3 => ({ x: a.x - b.x, y: a.y - b.y, z: a.z - b.z });
const mul = (a: V3, s: number): V3 => ({ x: a.x * s, y: a.y * s, z: a.z * s });
const dot = (a: V3, b: V3): number => a.x * b.x + a.y * b.y + a.z * b.z;
const cross = (a: V3, b: V3): V3 => ({ x: a.y * b.z - a.z * b.y, y: a.z * b.x - a.x * b.z, z: a.x * b.y - a.y * b.x });
const at = (p: V3, y: number): V3 => ({ x: p.x, y, z: p.z });
const UP: V3 = { x: 0, y: 1, z: 0 };

function sideFrame(s: GroundSpec, side: number): { out: V3; e0: V3; e1: V3 } {
    switch (side) {
        case MINUS_X:
            return { out: { x: -1, y: 0, z: 0 }, e0: { x: s.x0, y: 0, z: s.z0 }, e1: { x: s.x0, y: 0, z: s.z1 } };
        case PLUS_X:
            return { out: { x: 1, y: 0, z: 0 }, e0: { x: s.x1, y: 0, z: s.z0 }, e1: { x: s.x1, y: 0, z: s.z1 } };
        case MINUS_Z:
            return { out: { x: 0, y: 0, z: -1 }, e0: { x: s.x0, y: 0, z: s.z0 }, e1: { x: s.x1, y: 0, z: s.z0 } };
        default:
            return { out: { x: 0, y: 0, z: 1 }, e0: { x: s.x0, y: 0, z: s.z1 }, e1: { x: s.x1, y: 0, z: s.z1 } };
    }
}

class Builder {
    mesh: GroundMesh = { vertices: [], faces: [], min: { x: 0, y: 0, z: 0 }, max: { x: 0, y: 0, z: 0 } };
    uv: number;
    constructor(uv: number) {
        this.uv = uv;
    }
    corner(p: V3, n: V3): void {
        const ax = Math.abs(n.x);
        const ay = Math.abs(n.y);
        const az = Math.abs(n.z);
        const [u, v] = ay >= ax && ay >= az ? [p.x, p.z] : ax >= az ? [p.z, p.y] : [p.x, p.y];
        this.mesh.vertices.push({ pos: p, normal: n, u: u / this.uv, v: -v / this.uv });
    }
    tri(a: V3, b: V3, c: V3, hint: V3, flags: number): void {
        let n = cross(sub(b, a), sub(c, a));
        const length = Math.sqrt(dot(n, n));
        if (!(length > 1e-3)) return;
        if (dot(n, hint) < 0) {
            [b, c] = [c, b];
            n = mul(n, -1);
        }
        n = mul(n, 1 / length);
        const first = this.mesh.vertices.length;
        this.corner(a, n);
        this.corner(b, n);
        this.corner(c, n);
        this.mesh.faces.push({ a: first, b: first + 1, c: first + 2, flags });
    }
    quad(a: V3, b: V3, c: V3, d: V3, hint: V3, flags: number): void {
        this.tri(a, b, c, hint, flags);
        this.tri(a, c, d, hint, flags);
    }
}

function addRamp(
    b: Builder,
    ramp: Ramp,
    out: V3,
    e0: V3,
    e1: V3,
    foot: number,
    top: number,
    length: number,
    cheeks: boolean,
): void {
    const rise = top - foot;
    const profile: [number, number][] =
        ramp === "k"
            ? [
                  [length, 0],
                  [0, rise],
              ]
            : quarterProfile(length, rise, QUARTER_SEGMENTS);
    for (let i = 0; i + 1 < profile.length; i++) {
        const [d0, r0] = profile[i]!;
        const [d1, r1] = profile[i + 1]!;
        const y0 = foot + r0;
        const y1 = foot + r1;
        const dd = d1 - d0;
        const dy = y1 - y0;
        const hint = add(mul(out, dy), mul(UP, -dd));
        const flags = ramp === "q" && dy >= -dd ? FLAG_VERT : FLAG_RAMP;
        b.quad(
            at(add(e0, mul(out, d0)), y0),
            at(add(e1, mul(out, d0)), y0),
            at(add(e1, mul(out, d1)), y1),
            at(add(e0, mul(out, d1)), y1),
            hint,
            flags,
        );
    }
    if (!cheeks) return;
    const along0 = sub(e1, e0);
    const along = mul(along0, 1 / Math.sqrt(dot(along0, along0)));
    for (const end of [0, 1]) {
        const e = end ? e1 : e0;
        const away = end ? along : mul(along, -1);
        const corner = at(e, foot);
        for (let i = 0; i + 1 < profile.length; i++) {
            const p = at(add(e, mul(out, profile[i]![0])), foot + profile[i]![1]);
            const q = at(add(e, mul(out, profile[i + 1]![0])), foot + profile[i + 1]![1]);
            b.tri(corner, p, q, away, FLAG_WALL);
        }
    }
}

export function build(input: GroundSpec): GroundMesh {
    const s: GroundSpec = { ...input, ramps: [...input.ramps], hides: [...input.hides] };
    const b = new Builder(s.uv > 0 ? s.uv : 256);
    if (!normalise(s)) return b.mesh;
    if (!s.lower) {
        const top = s.base + s.height;
        const bottom = s.base - SINK;
        b.quad(
            { x: s.x0, y: top, z: s.z0 },
            { x: s.x1, y: top, z: s.z0 },
            { x: s.x1, y: top, z: s.z1 },
            { x: s.x0, y: top, z: s.z1 },
            UP,
            FLAG_DECK,
        );
        for (let side = 0; side < 4; side++) {
            const { out, e0, e1 } = sideFrame(s, side);
            if (s.ramps[side] === "-") b.quad(at(e0, bottom), at(e1, bottom), at(e1, top), at(e0, top), out, FLAG_WALL);
            else addRamp(b, s.ramps[side]!, out, e0, e1, s.base, top, rampLength(s, side), true);
        }
    } else {
        const floor = s.base - s.height;
        b.quad(
            { x: s.x0, y: floor, z: s.z0 },
            { x: s.x1, y: floor, z: s.z0 },
            { x: s.x1, y: floor, z: s.z1 },
            { x: s.x0, y: floor, z: s.z1 },
            UP,
            FLAG_DECK,
        );
        for (let side = 0; side < 4; side++) {
            const { out, e0, e1 } = sideFrame(s, side);
            const inward = mul(out, -1);
            if (s.ramps[side] === "-")
                b.quad(at(e0, floor), at(e1, floor), at(e1, s.base), at(e0, s.base), inward, FLAG_WALL);
            else addRamp(b, s.ramps[side]!, inward, e0, e1, floor, s.base, rampLength(s, side), false);
        }
    }
    const m = b.mesh;
    if (m.vertices.length) {
        m.min = { ...m.vertices[0]!.pos };
        m.max = { ...m.vertices[0]!.pos };
        for (const v of m.vertices) {
            m.min = { x: Math.min(m.min.x, v.pos.x), y: Math.min(m.min.y, v.pos.y), z: Math.min(m.min.z, v.pos.z) };
            m.max = { x: Math.max(m.max.x, v.pos.x), y: Math.max(m.max.y, v.pos.y), z: Math.max(m.max.z, v.pos.z) };
        }
    }
    return m;
}

// Ground::Strip: degenerate joins, each face starting at an even position; `twoSided`: each face both ways.
export function strip(faces: readonly Face[], twoSided: boolean): number[] {
    const out: number[] = [];
    const put = (a: number, b: number, c: number): void => {
        if (out.length) {
            out.push(out[out.length - 1]!, a, a);
            if ((out.length - 1) % 2 !== 0) out.push(a);
        } else {
            out.push(a);
        }
        out.push(b, c);
    };
    for (const f of faces) {
        put(f.a, f.b, f.c);
        if (twoSided) put(f.a, f.c, f.b);
    }
    return out;
}

export function stripTriangles(s: readonly number[]): [number, number, number][] {
    const tris: [number, number, number][] = [];
    for (let i = 0; i + 2 < s.length; i++) {
        let a = s[i]!;
        let b = s[i + 1]!;
        const c = s[i + 2]!;
        if (a === b || b === c || a === c) continue;
        if (i % 2) [a, b] = [b, a];
        tris.push([a, b, c]);
    }
    return tris;
}

export interface VertexLayout {
    normal: number;
    size: number;
    diffuse: number;
    specular: number;
    tex: number;
    texCount: number;
}

// Ground::LayoutFor: a plain XYZ FVF's offsets (-1 none); null when not that or its size isn't `stride`.
export function layoutFor(fvf: number, stride: number): VertexLayout | null {
    if (fvf <= 6 || fvf >= 0x10000 || (fvf & 0x400e) !== 0x002) return null;
    const l: VertexLayout = { normal: -1, size: -1, diffuse: -1, specular: -1, tex: -1, texCount: 0 };
    let offset = 12;
    const take = (bit: number, size: number): number => {
        if (!(fvf & bit)) return -1;
        offset += size;
        return offset - size;
    };
    l.normal = take(0x010, 12);
    l.size = take(0x020, 4);
    l.diffuse = take(0x040, 4);
    l.specular = take(0x080, 4);
    l.texCount = (fvf >> 8) & 0xf;
    if (l.texCount) l.tex = offset;
    offset += 8 * l.texCount;
    return offset === stride ? l : null;
}

export function writeVertices(mesh: GroundMesh, l: VertexLayout, stride: number, colour: number): Buffer {
    const out = Buffer.alloc(mesh.vertices.length * stride);
    mesh.vertices.forEach((v, i) => {
        const o = i * stride;
        out.writeFloatLE(v.pos.x, o);
        out.writeFloatLE(v.pos.y, o + 4);
        out.writeFloatLE(v.pos.z, o + 8);
        if (l.normal >= 0) {
            out.writeFloatLE(v.normal.x, o + l.normal);
            out.writeFloatLE(v.normal.y, o + l.normal + 4);
            out.writeFloatLE(v.normal.z, o + l.normal + 8);
        }
        if (l.size >= 0) out.writeFloatLE(1, o + l.size);
        if (l.diffuse >= 0) out.writeUInt32LE(colour >>> 0, o + l.diffuse);
        for (let t = 0; t < l.texCount; t++) {
            out.writeFloatLE(v.u, o + l.tex + 8 * t);
            out.writeFloatLE(v.v, o + l.tex + 8 * t + 4);
        }
    });
    return out;
}

// --- the record (Ground::ReadRecord) -------------------------------------------------------------------------------

const VALID_WORD = /^[A-Za-z0-9_]{1,64}$/;
const HEX8 = /^0[xX][0-9A-Fa-f]{8}$/;
const nameChecksum = (text: string): number =>
    HEX8.test(text) ? Number.parseInt(text.slice(2), 16) >>> 0 : checksum(text);

function readFloat(text: string): number | null {
    if (!text.length || !/^[-+]?(\d+\.?\d*|\.\d+)([eE][-+]?\d+)?$/.test(text)) return null;
    const v = Number.parseFloat(text);
    return Number.isFinite(v) ? Math.fround(v) : null;
}

// The words after "ground" (comment cut); null when the DLL wouldn't read it.
export function readGroundRecord(text: string): GroundSpec | null {
    const words = text.trim().split(/\s+/);
    const name = words[0] ?? "";
    const kind = words[1] ?? "";
    if (!VALID_WORD.test(name) || (kind !== "raise" && kind !== "lower")) return null;
    const s: GroundSpec = {
        name,
        lower: kind === "lower",
        x0: 0,
        z0: 0,
        x1: 0,
        z1: 0,
        base: 0,
        height: 200,
        ramps: ["-", "-", "-", "-"],
        length: 0,
        material: 0,
        uv: 256,
        colour: 0xff808080,
        terrain: 0,
        hides: [],
    };
    const seen = new Set<string>();
    for (const word of words.slice(2)) {
        const eq = word.indexOf("=");
        if (eq <= 0) return null;
        const key = word.slice(0, eq);
        const value = word.slice(eq + 1);
        const known = ["rect", "base", "height", "ramps", "length", "material", "uv", "color", "terrain", "hides"];
        if (!known.includes(key)) continue; // (a later version's)
        if (seen.has(key)) return null;
        seen.add(key);
        switch (key) {
            case "rect": {
                const p = value.split(",").map(readFloat);
                if (p.length !== 4 || p.some((v) => v === null)) return null;
                [s.x0, s.z0, s.x1, s.z1] = p as number[] as [number, number, number, number];
                break;
            }
            case "base":
            case "height":
            case "length":
            case "uv": {
                const v = readFloat(value);
                if (
                    v === null ||
                    (key === "height" && !(v > 0)) ||
                    (key === "length" && v < 0) ||
                    (key === "uv" && !(v > 0))
                )
                    return null;
                s[key] = v;
                break;
            }
            case "ramps": {
                const p = value.split(",");
                if (p.length !== 4 || p.some((r) => r !== "-" && r !== "k" && r !== "q")) return null;
                s.ramps = p as [Ramp, Ramp, Ramp, Ramp];
                break;
            }
            case "material":
                if (!HEX8.test(value) && !VALID_WORD.test(value)) return null;
                s.material = nameChecksum(value);
                break;
            case "color":
                if (!/^[0-9A-Fa-f]{8}$/.test(value)) return null;
                s.colour = Number.parseInt(value, 16) >>> 0;
                break;
            case "terrain":
                if (!/^\d+$/.test(value) || Number(value) > 0xffff) return null;
                s.terrain = Number(value);
                break;
            case "hides":
                for (const p of value.split(",")) {
                    if (!HEX8.test(p)) return null;
                    s.hides.push(Number.parseInt(p.slice(2), 16) >>> 0);
                }
                break;
        }
    }
    if (!seen.has("rect") || !seen.has("base") || !seen.has("height") || !seen.has("material") || !s.material)
        return null;
    return normalise(s) ? s : null;
}

// --- the bake --------------------------------------------------------------------------------------------------

export const MAX_FACES = 2048;

// The piece as a scene sector and a collision object (null + why when its donor isn't there or can't lend its format).
export function groundSector(spec: GroundSpec, donor: Sector): { sector: Sector; collision: ColObject } | string {
    const first = donor.meshes[0];
    if (!first) return "its material's piece has no mesh";
    const stride = first.stride;
    const layout = layoutFor(first.vertexShader[0] ?? 0, stride);
    if (!layout || layout.texCount < 1) return "its material's piece has a vertex format the piece can't be written in";
    const mesh = build(spec);
    if (!mesh.faces.length || mesh.faces.length > MAX_FACES)
        return `it has ${mesh.faces.length} faces (1 - ${MAX_FACES})`;
    const indices = strip(mesh.faces, true);
    const centre = mul(add(mesh.min, mesh.max), 0.5);
    const half = sub(mesh.max, centre);
    const radius = Math.sqrt(dot(half, half));
    const name = checksum(spec.name);
    const m: SceneMesh = {
        ...first,
        sphere: [centre.x, centre.y, centre.z, radius],
        bbox: [mesh.min.x, mesh.min.y, mesh.min.z, mesh.max.x, mesh.max.y, mesh.max.z],
        lods: first.lods.length ? [indices] : [],
        strips: [indices],
        vertexCount: mesh.vertices.length,
        buffers: [[first.buffers[0]?.[0] ?? 0, writeVertices(mesh, layout, stride, spec.colour)]],
        hasWibble: 0,
        indexSets: Math.min(first.indexSets, 1),
    };
    delete m.wibble;
    delete m.indexSetValues;
    const sector: Sector = {
        checksum: name,
        bone: donor.bone,
        flags: (donor.flags & ~SEC_BILLBOARD) >>> 0,
        bbox: [mesh.min.x, mesh.min.y, mesh.min.z, mesh.max.x, mesh.max.y, mesh.max.z],
        sphere: [centre.x, centre.y, centre.z, radius],
        meshes: [m],
    };
    const extent = Math.max(mesh.max.x - mesh.min.x, mesh.max.y - mesh.min.y, mesh.max.z - mesh.min.z);
    const faces: ColFace[] = mesh.faces.map((f) => [f.flags, spec.terrain, f.a, f.b, f.c]);
    const collision: ColObject = {
        checksum: name,
        flags: 0,
        smallFaces: 0,
        fixedVerts: extent * 16 <= 65535 ? 1 : 0,
        bbox: [mesh.min.x, mesh.min.y, mesh.min.z, 1, mesh.max.x, mesh.max.y, mesh.max.z, 1],
        verts: mesh.vertices.map((v) => [v.pos.x, v.pos.y, v.pos.z]),
        intensities: Buffer.alloc(mesh.vertices.length, 0x80),
        faces,
        reach: Buffer.alloc(0),
        bsp: { kind: "leaf", faces: [], low: 3 },
    };
    collision.bsp = buildTree(collision.verts, collision.faces);
    setReaches(collision);
    return { sector, collision };
}
