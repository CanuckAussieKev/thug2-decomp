// Havok 4.0 packfiles (THP8's collision, .hkc): read with the class descriptions the file carries in its __types__
// section, so that objects are read by member name, not by guessed offsets. The layout here is the packfile's own
// (Havok's binary packfile format, version 4; THP8's are 32-bit pointers, big-endian):
//
//   0x00  u32 0x57E0E057, u32 0x10C0C010 (magic), u32 user tag, u32 file version (4)
//   0x10  layout rules: u8 bytes in a pointer (4), u8 little-endian (0), u8 reuse padding, u8 empty base class
//         optimisation; u32 section count, u32 contents section, u32 contents offset, u32 contents class name
//         section, u32 its offset, char[16] contents version ("Havok-4.0.0-r1"), u32 flags, u32 pad
//   0x40  sections, 0x30 bytes each: char[20] name ("__classnames__", "__data__", "__types__"), then u32 absolute
//         data start, and the offsets (from the start) of the local fixups, global fixups, virtual fixups, exports,
//         imports and the end.
//   local fixups   {u32 at, u32 to}: a pointer in the section at `at` points to `to` in the same section
//   global fixups  {u32 at, u32 section, u32 to}: a pointer to another section
//   virtual fixups {u32 at, u32 class name section, u32 class name offset}: an object at `at` of that class
//   (each list padded with 0xFFFFFFFF)
//   __classnames__: {u32 signature, u8 9, name, 0} per class.
//
// hkClass (0x30 bytes here): name, parent, object size, interface count, enums, enum count, members, member count,
// defaults, attributes / flags. hkClassMember (0x14 bytes): name, class, enum, u8 type, u8 subtype, i16 C array
// size, u16 flags, u16 offset. Member types (hkClassMember::Type, Havok 4.0): below (T).

import { FormatError } from "../level_port/binio.ts";

export const MAGIC = [0x57e0e057, 0x10c0c010];

// hkClassMember::Type (Havok 4.0)
export const T = {
    VOID: 0,
    BOOL: 1,
    CHAR: 2,
    INT8: 3,
    UINT8: 4,
    INT16: 5,
    UINT16: 6,
    INT32: 7,
    UINT32: 8,
    INT64: 9,
    UINT64: 10,
    REAL: 11,
    VECTOR4: 12,
    QUATERNION: 13,
    MATRIX3: 14,
    ROTATION: 15,
    QSTRANSFORM: 16,
    MATRIX4: 17,
    TRANSFORM: 18,
    ZERO: 19,
    POINTER: 20,
    FUNCTIONPOINTER: 21,
    ARRAY: 22,
    INPLACEARRAY: 23,
    ENUM: 24,
    STRUCT: 25,
    SIMPLEARRAY: 26,
    HOMOGENEOUSARRAY: 27,
    VARIANT: 28,
    CSTRING: 29,
    ULONG: 30,
    FLAGS: 31,
} as const;

// the size of a member of each type (0: the size comes from elsewhere - struct, enum)
const SIZES: Record<number, number> = {
    [T.BOOL]: 1,
    [T.CHAR]: 1,
    [T.INT8]: 1,
    [T.UINT8]: 1,
    [T.INT16]: 2,
    [T.UINT16]: 2,
    [T.INT32]: 4,
    [T.UINT32]: 4,
    [T.INT64]: 8,
    [T.UINT64]: 8,
    [T.REAL]: 4,
    [T.VECTOR4]: 16,
    [T.QUATERNION]: 16,
    [T.MATRIX3]: 48,
    [T.ROTATION]: 48,
    [T.QSTRANSFORM]: 48,
    [T.MATRIX4]: 64,
    [T.TRANSFORM]: 64,
    [T.POINTER]: 4,
    [T.FUNCTIONPOINTER]: 4,
    [T.ARRAY]: 12,
    [T.SIMPLEARRAY]: 8,
    [T.CSTRING]: 4,
    [T.ULONG]: 4,
};

export interface Section {
    name: string;
    start: number; // absolute
    size: number; // (to the local fixups: the data)
    local: Map<number, number>;
    global: Map<number, [number, number]>;
    objects: { at: number; className: string }[];
}

export interface ClassMember {
    name: string;
    type: number;
    subtype: number;
    cArraySize: number;
    offset: number;
    className: string | null; // a struct / pointer member's class
    enumSize: number; // (enum / flags: the stored size, from the subtype)
}

export interface HkClass {
    name: string;
    parent: string | null;
    size: number;
    members: ClassMember[]; // the declared ones (not the parent's)
}

// A pointer: the section and offset it points to.
export interface Ptr {
    section: number;
    at: number;
}

export class Packfile {
    readonly data: Buffer;
    readonly name: string;
    readonly sections: Section[] = [];
    readonly classes = new Map<string, HkClass>();
    readonly version: string;

    constructor(data: Buffer, name = "hkc") {
        this.data = data;
        this.name = name;
        if (data.readUInt32BE(0) !== MAGIC[0] || data.readUInt32BE(4) !== MAGIC[1]) {
            throw new FormatError(`${name}: not a Havok packfile`);
        }
        if (data[0x10] !== 4 || data[0x11] !== 0) throw new FormatError(`${name}: not 32-bit big-endian`);
        const count = data.readUInt32BE(0x14);
        this.version = data.subarray(0x28, 0x38).toString("latin1").replace(/\0.*$/s, "");
        for (let i = 0; i < count; i++) {
            const h = 0x40 + 0x30 * i;
            const nameField = data.subarray(h, h + 20);
            const sname = nameField.subarray(0, Math.max(0, nameField.indexOf(0))).toString("latin1");
            const w = (k: number): number => data.readUInt32BE(h + 20 + 4 * k);
            const start = w(0);
            const [local, global, virt, exports] = [w(1), w(2), w(3), w(4)] as [number, number, number, number];
            const s: Section = { name: sname, start, size: local, local: new Map(), global: new Map(), objects: [] };
            for (let at = start + local; at + 8 <= start + global; at += 8) {
                const src = data.readUInt32BE(at);
                if (src === 0xffffffff) continue;
                s.local.set(src, data.readUInt32BE(at + 4));
            }
            for (let at = start + global; at + 12 <= start + virt; at += 12) {
                const src = data.readUInt32BE(at);
                if (src === 0xffffffff) continue;
                s.global.set(src, [data.readUInt32BE(at + 4), data.readUInt32BE(at + 8)]);
            }
            this.sections.push(s);
            // (virtual fixups name a class in another section: read once all the section headers are in)
            (s as Section & { virt: [number, number] }).virt = [start + virt, start + exports];
        }
        for (const s of this.sections) {
            const [from, to] = (s as Section & { virt: [number, number] }).virt;
            for (let at = from; at + 12 <= to; at += 12) {
                const src = this.data.readUInt32BE(at);
                if (src === 0xffffffff) continue;
                const sec = this.sections[this.data.readUInt32BE(at + 4)];
                if (sec === undefined) throw new FormatError(`${name}: virtual fixup to a missing section`);
                s.objects.push({ at: src, className: this.cstring(sec.start + this.data.readUInt32BE(at + 8)) });
            }
        }
        const types = this.sections.findIndex((s) => s.name === "__types__");
        if (types >= 0) this.readClasses(types);
    }

    private cstring(at: number): string {
        const end = this.data.indexOf(0, at);
        return this.data.subarray(at, end).toString("latin1");
    }

    section(name: string): number {
        const i = this.sections.findIndex((s) => s.name === name);
        if (i < 0) throw new FormatError(`${this.name}: no ${name} section`);
        return i;
    }

    // The pointer stored at `at` in section `section` (null: a null pointer).
    ptr(section: number, at: number): Ptr | null {
        const s = this.sections[section]!;
        const local = s.local.get(at);
        if (local !== undefined) return { section, at: local };
        const global = s.global.get(at);
        if (global !== undefined) return { section: global[0], at: global[1] };
        return null;
    }

    abs(p: Ptr): number {
        return this.sections[p.section]!.start + p.at;
    }

    u8(p: Ptr, off = 0): number {
        return this.data[this.abs(p) + off]!;
    }
    u16(p: Ptr, off = 0): number {
        return this.data.readUInt16BE(this.abs(p) + off);
    }
    i16(p: Ptr, off = 0): number {
        return this.data.readInt16BE(this.abs(p) + off);
    }
    u32(p: Ptr, off = 0): number {
        return this.data.readUInt32BE(this.abs(p) + off);
    }
    f32(p: Ptr, off = 0): number {
        return this.data.readFloatBE(this.abs(p) + off);
    }
    str(p: Ptr | null): string | null {
        return p === null ? null : this.cstring(this.abs(p));
    }
    at(p: Ptr, off: number): Ptr {
        return { section: p.section, at: p.at + off };
    }
    // the pointer stored in an object's field
    field(p: Ptr, off: number): Ptr | null {
        return this.ptr(p.section, p.at + off);
    }

    private readClasses(types: number): void {
        const s = this.sections[types]!;
        for (const o of s.objects) {
            if (o.className !== "hkClass") continue;
            const p: Ptr = { section: types, at: o.at };
            const name = this.str(this.field(p, 0));
            if (name === null) throw new FormatError(`${this.name}: an hkClass without a name`);
            const parent = this.field(p, 4);
            const members: ClassMember[] = [];
            const mp = this.field(p, 24);
            const count = this.u32(p, 28);
            for (let i = 0; i < count && mp !== null; i++) {
                const m = this.at(mp, 0x14 * i);
                const cls = this.field(m, 4);
                const type = this.u8(m, 12);
                const subtype = this.u8(m, 13);
                members.push({
                    name: this.str(this.field(m, 0)) ?? "",
                    className: cls === null ? null : this.str(this.field(cls, 0)),
                    type,
                    subtype,
                    cArraySize: this.i16(m, 14),
                    offset: this.u16(m, 18),
                    enumSize: SIZES[subtype] ?? 0,
                });
            }
            this.classes.set(name, {
                name,
                parent: parent === null ? null : this.str(this.field(parent, 0)),
                size: this.u32(p, 8),
                members,
            });
        }
    }

    // A class's members, its parents' first.
    allMembers(className: string): ClassMember[] {
        const c = this.classes.get(className);
        if (c === undefined) throw new FormatError(`${this.name}: no description of class ${className}`);
        return [...(c.parent ? this.allMembers(c.parent) : []), ...c.members];
    }

    member(className: string, member: string): ClassMember {
        const m = this.allMembers(className).find((x) => x.name === member);
        if (m === undefined) throw new FormatError(`${this.name}: ${className} has no member ${member}`);
        return m;
    }

    // Objects of a class in the data section.
    objectsOf(className: string): Ptr[] {
        const data = this.section("__data__");
        return this.sections[data]!.objects.filter((o) => o.className === className).map((o) => ({
            section: data,
            at: o.at,
        }));
    }

    classOf(p: Ptr): string | null {
        return this.sections[p.section]!.objects.find((o) => o.at === p.at)?.className ?? null;
    }

    // An hkArray member: [pointer to the elements, count].
    array(obj: Ptr, className: string, member: string): [Ptr | null, number] {
        const m = this.member(className, member);
        if (m.type !== T.ARRAY && m.type !== T.SIMPLEARRAY)
            throw new FormatError(`${className}.${member}: not an array`);
        return [this.field(obj, m.offset), this.u32(obj, m.offset + 4)];
    }

    pointer(obj: Ptr, className: string, member: string): Ptr | null {
        return this.field(obj, this.member(className, member).offset);
    }

    int(obj: Ptr, className: string, member: string): number {
        const m = this.member(className, member);
        const size = m.type === T.ENUM || m.type === T.FLAGS ? m.enumSize : (SIZES[m.type] ?? 0);
        const at = this.abs(obj) + m.offset;
        switch (size) {
            case 1:
                return m.type === T.INT8 ? this.data.readInt8(at) : this.data[at]!;
            case 2:
                return m.type === T.INT16 ? this.data.readInt16BE(at) : this.data.readUInt16BE(at);
            case 4:
                return m.type === T.INT32 ? this.data.readInt32BE(at) : this.data.readUInt32BE(at);
            default:
                throw new FormatError(`${className}.${member}: not an integer (type ${m.type})`);
        }
    }

    real(obj: Ptr, className: string, member: string): number {
        return this.data.readFloatBE(this.abs(obj) + this.member(className, member).offset);
    }

    floats(obj: Ptr, off: number, n: number): number[] {
        const at = this.abs(obj) + off;
        return Array.from({ length: n }, (_, k) => this.data.readFloatBE(at + 4 * k));
    }
}

// --- THP8's collision ---------------------------------------------------------------------------------------------
//
// hkRootLevelContainer -> hkPhysicsData -> hkPhysicsSystem(s) -> hkRigidBody (fixed; identity transforms: the shapes
// are in world space, metres). A body's shape: an hkMoppBvTreeShape over an hkStorageMeshShape (triangles), or an
// hkListShape of hkConvexTransformShapes (boxes, cylinders, capsules: the node array's CollisionPrimitive nodes - a
// transform shape's userData is its node's name) and / or MOPP meshes. A mesh triangle's material index picks one of
// its subpart's `materials` words, a list child's collisionFilterInfo is its word: terrain << 16 | face flags.

export interface HkTriangle {
    a: number[];
    b: number[];
    c: number[];
    material: number;
    owner: number; // the primitive's node name checksum, 0 for mesh triangles
}

export interface HkBody {
    index: number;
    triangles: HkTriangle[];
}

type Mat = number[]; // 4 x 4, column-major as hkTransform (3 rotation columns, then the translation)

function apply(m: Mat, p: readonly number[]): number[] {
    return [0, 1, 2].map((k) => m[k]! * p[0]! + m[4 + k]! * p[1]! + m[8 + k]! * p[2]! + m[12 + k]!);
}

// a * b (b applied first)
function compose(a: Mat, b: Mat): Mat {
    const col = (c: number): number[] => {
        const v = [b[4 * c]!, b[4 * c + 1]!, b[4 * c + 2]!];
        const r = [0, 1, 2].map((k) => a[k]! * v[0]! + a[4 + k]! * v[1]! + a[8 + k]! * v[2]!);
        return c === 3 ? [r[0]! + a[12]!, r[1]! + a[13]!, r[2]! + a[14]!, 1] : [...r, 0];
    };
    return [...col(0), ...col(1), ...col(2), ...col(3)];
}

function transformAt(pf: Packfile, p: Ptr, off: number): Mat {
    const f = pf.floats(p, off, 16);
    return [f[0]!, f[1]!, f[2]!, 0, f[4]!, f[5]!, f[6]!, 0, f[8]!, f[9]!, f[10]!, 0, f[12]!, f[13]!, f[14]!, 1];
}

const SIDES = 8; // a cylinder's / capsule's facets

function boxTriangles(he: readonly number[]): number[][][] {
    const v = (x: number, y: number, z: number): number[] => [x * he[0]!, y * he[1]!, z * he[2]!];
    const quads = [
        [v(-1, -1, -1), v(-1, 1, -1), v(1, 1, -1), v(1, -1, -1)],
        [v(-1, -1, 1), v(1, -1, 1), v(1, 1, 1), v(-1, 1, 1)],
        [v(-1, -1, -1), v(-1, -1, 1), v(-1, 1, 1), v(-1, 1, -1)],
        [v(1, -1, -1), v(1, 1, -1), v(1, 1, 1), v(1, -1, 1)],
        [v(-1, -1, -1), v(1, -1, -1), v(1, -1, 1), v(-1, -1, 1)],
        [v(-1, 1, -1), v(-1, 1, 1), v(1, 1, 1), v(1, 1, -1)],
    ];
    return quads.flatMap((q) => [
        [q[0]!, q[1]!, q[2]!],
        [q[0]!, q[2]!, q[3]!],
    ]);
}

// A prism of SIDES faces round the segment a-b (radius r), its ends closed.
export function cylinderTriangles(a: readonly number[], b: readonly number[], r: number): number[][][] {
    const axis = [0, 1, 2].map((k) => b[k]! - a[k]!);
    const len = Math.hypot(...axis) || 1;
    const n = axis.map((x) => x / len);
    const other = Math.abs(n[0]!) < 0.9 ? [1, 0, 0] : [0, 1, 0];
    const cross = (p: readonly number[], q: readonly number[]): number[] => [
        p[1]! * q[2]! - p[2]! * q[1]!,
        p[2]! * q[0]! - p[0]! * q[2]!,
        p[0]! * q[1]! - p[1]! * q[0]!,
    ];
    const u0 = cross(n, other);
    const ul = Math.hypot(...u0);
    const u = u0.map((x) => x / ul);
    const w = cross(n, u);
    const ring = (c: readonly number[]): number[][] =>
        Array.from({ length: SIDES }, (_, i) => {
            const t = (2 * Math.PI * i) / SIDES;
            return [0, 1, 2].map((k) => c[k]! + r * (Math.cos(t) * u[k]! + Math.sin(t) * w[k]!));
        });
    const ra = ring(a);
    const rb = ring(b);
    const out: number[][][] = [];
    for (let i = 0; i < SIDES; i++) {
        const j = (i + 1) % SIDES;
        // (wound to face outwards, as the ends: THUG2's line tests only hit a face's front - see faceOutwards)
        out.push([ra[i]!, rb[j]!, rb[i]!], [ra[i]!, ra[j]!, rb[j]!]);
        if (i > 0 && i < SIDES - 1) out.push([ra[0]!, ra[j]!, ra[i]!], [rb[0]!, rb[i]!, rb[j]!]);
    }
    return out;
}

// A convex primitive's triangles (placed) turned to face away from its middle: THUG2's collision is one-sided - a line
// test only hits a face from the side its winding (b - a) x (c - a) faces (0x0040B420 rejects a determinant under
// 0.03), and the skater takes that normal as the way out (DoAirPhysics 0x0050CA00 puts him at the hit point plus the
// normal, then pushes him Skater_Min_Distance_To_Wall along it). A primitive wound inwards can't be hit from outside
// and traps whoever gets in: BEAVER VALLEY's tree trunks were cylinders with their sides wound inwards (the user's
// stuck-in-a-tree event - docs/pending/pole-tunnel.md). Havok's shapes have no winding; a mirroring transform would
// turn one inside out too. Returns how many were turned.
export function faceOutwards(tris: number[][][]): number {
    const centre = [0, 1, 2].map(
        (k) => tris.reduce((s, t) => s + t[0]![k]! + t[1]![k]! + t[2]![k]!, 0) / (3 * tris.length),
    );
    let turned = 0;
    for (const t of tris) {
        const [a, b, c] = [t[0]!, t[1]!, t[2]!];
        const u = [0, 1, 2].map((k) => b[k]! - a[k]!);
        const v = [0, 1, 2].map((k) => c[k]! - a[k]!);
        const n = [u[1]! * v[2]! - u[2]! * v[1]!, u[2]! * v[0]! - u[0]! * v[2]!, u[0]! * v[1]! - u[1]! * v[0]!];
        const out = [0, 1, 2].map((k) => (a[k]! + b[k]! + c[k]!) / 3 - centre[k]!);
        if (n[0]! * out[0]! + n[1]! * out[1]! + n[2]! * out[2]! < 0) {
            t[1] = c;
            t[2] = b;
            turned++;
        }
    }
    return turned;
}

// What readCollision did to the primitives (for the converter's report and the audit).
export interface PrimitiveStats {
    primitives: number;
    triangles: number;
    turned: number; // triangles faceOutwards turned (0 unless a transform mirrors)
}

// Every rigid body's triangles (world space; primitives made into triangles).
export function readCollision(
    pf: Packfile,
    warnings: string[] = [],
    stats: PrimitiveStats = { primitives: 0, triangles: 0, turned: 0 },
): HkBody[] {
    const childOf = (s: Ptr, c: string, member: string): Ptr | null =>
        pf.field(s, pf.member(c, member).offset + pf.member("hkSingleShapeContainer", "childShape").offset);
    const walk = (s: Ptr | null, m: Mat, material: number, out: HkBody, owner: number): void => {
        if (s === null) return;
        const c = pf.classOf(s);
        // (a primitive's triangles placed, then faced outwards)
        const push = (tris: number[][][]): void => {
            const placed = tris.map((t) => t.map((p) => apply(m, p)));
            stats.primitives++;
            stats.triangles += placed.length;
            stats.turned += faceOutwards(placed);
            for (const [a, b, cc] of placed) out.triangles.push({ a: a!, b: b!, c: cc!, material, owner });
        };
        switch (c) {
            case "hkMoppBvTreeShape":
                walk(childOf(s, c, "child"), m, material, out, owner);
                return;
            case "hkListShape": {
                const [kids, n] = pf.array(s, c, "childInfo");
                const size = pf.classes.get("hkListShapeChildInfo")?.size ?? 8;
                for (let k = 0; k < n; k++) {
                    const info = pf.at(kids!, size * k);
                    walk(pf.field(info, 0), m, pf.u32(info, 4), out, owner);
                }
                return;
            }
            case "hkConvexTransformShape": {
                const t = compose(m, transformAt(pf, s, pf.member(c, "transform").offset));
                const ud = pf.u32(s, pf.member(c, "userData").offset);
                walk(childOf(s, c, "childShape"), t, material, out, ud || owner);
                return;
            }
            case "hkConvexTranslateShape": {
                const v = pf.floats(s, pf.member(c, "translation").offset, 3);
                const t = compose(m, [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, v[0]!, v[1]!, v[2]!, 1]);
                const ud = pf.u32(s, pf.member(c, "userData").offset);
                walk(childOf(s, c, "childShape"), t, material, out, ud || owner);
                return;
            }
            case "hkSphereShape": {
                // (a prism as tall as it is wide)
                const r = pf.real(s, c, "radius");
                push(cylinderTriangles([0, -r, 0], [0, r, 0], r));
                return;
            }
            case "hkBoxShape":
                push(boxTriangles(pf.floats(s, pf.member(c, "halfExtents").offset, 3)));
                return;
            case "hkCylinderShape":
            case "hkCapsuleShape": {
                const a = pf.floats(s, pf.member(c, "vertexA").offset, 3);
                const b = pf.floats(s, pf.member(c, "vertexB").offset, 3);
                const r = c === "hkCylinderShape" ? pf.real(s, c, "cylRadius") : pf.real(s, c, "radius");
                push(cylinderTriangles(a, b, r));
                return;
            }
            case "hkStorageMeshShape":
                meshTriangles(pf, s, m, material, out, owner, warnings);
                return;
            default:
                warnings.push(`shape ${c} left out`);
        }
    };
    const collidable = pf.member("hkRigidBody", "collidable").offset;
    const motion =
        pf.member("hkRigidBody", "motion").offset +
        pf.member("hkMaxSizeMotion", "motionState").offset +
        pf.member("hkMotionState", "transform").offset;
    return pf.objectsOf("hkRigidBody").map((rb, index) => {
        const body: HkBody = { index, triangles: [] };
        walk(pf.field(rb, collidable), transformAt(pf, rb, motion), 0, body, 0);
        return body;
    });
}

function meshTriangles(
    pf: Packfile,
    s: Ptr,
    m: Mat,
    material: number,
    out: HkBody,
    owner: number,
    warnings: string[],
): void {
    const c = "hkStorageMeshShape";
    const sc = "hkStorageMeshShapeSubpartStorage";
    const sp = "hkMeshShapeSubpart";
    const [storage, n] = pf.array(s, c, "storage");
    const [subparts, ns] = pf.array(s, c, "subparts");
    if (n !== ns) throw new FormatError(`${pf.name}: ${ns} subparts, ${n} storages`);
    const scale = pf.floats(s, pf.member(c, "scaling").offset, 3);
    const subSize = pf.classes.get(sp)!.size;
    for (let k = 0; k < n; k++) {
        const st = pf.field(storage!, 4 * k)!;
        const sub = pf.at(subparts!, subSize * k);
        const vstride = pf.int(sub, sp, "vertexStriding");
        const nv = pf.int(sub, sp, "numVertices");
        const istride = pf.int(sub, sp, "indexStriding");
        const ntri = pf.int(sub, sp, "numTriangles");
        const itype = pf.u8(sub, pf.member(sp, "stridingType").offset); // 1: 16-bit indices, 2: 32-bit
        const mtype = pf.u8(sub, pf.member(sp, "materialIndexStridingType").offset); // 1: 8-bit, 2: 16-bit
        const mstride = pf.int(sub, sp, "materialIndexStriding");
        const matStride = pf.int(sub, sp, "materialStriding");
        const [verts, vcount] = pf.array(st, sc, "vertices");
        const [i16, n16] = pf.array(st, sc, "indices16");
        const [i32, n32] = pf.array(st, sc, "indices32");
        const [mi8, nmi8] = pf.array(st, sc, "materialIndices");
        const [mi16, nmi16] = pf.array(st, sc, "materialIndices16");
        const [mats, nmats] = pf.array(st, sc, "materials");
        if (vstride < 12 || (nv && (nv - 1) * vstride + 12 > vcount * 4)) {
            throw new FormatError(`${pf.name}: subpart of ${nv} vertices (stride ${vstride}), ${vcount} floats`);
        }
        const vertex = (i: number): number[] => {
            if (i >= nv) throw new FormatError(`${pf.name}: vertex ${i} of ${nv}`);
            const p = pf.floats(verts!, i * vstride, 3);
            return apply(m, [p[0]! * scale[0]!, p[1]! * scale[1]!, p[2]! * scale[2]!]);
        };
        const index = (t: number, j: number): number => {
            if (itype === 1) {
                if (!i16 || (t * istride) / 2 + j >= n16) throw new FormatError(`${pf.name}: index past indices16`);
                return pf.u16(i16, t * istride + 2 * j);
            }
            if (!i32 || (t * istride) / 4 + j >= n32) throw new FormatError(`${pf.name}: index past indices32`);
            return pf.u32(i32, t * istride + 4 * j);
        };
        const materialOf = (t: number): number => {
            if (!nmats) return material;
            let mi = 0;
            if (mtype === 1 && mi8 && t * mstride < nmi8) mi = pf.u8(mi8, t * mstride);
            else if (mtype === 2 && mi16 && t < nmi16) mi = pf.u16(mi16, t * mstride);
            if (mi >= nmats) {
                warnings.push(`material index ${mi} of ${nmats}`);
                return material;
            }
            return pf.u32(mats!, mi * matStride);
        };
        for (let t = 0; t < ntri; t++) {
            out.triangles.push({
                a: vertex(index(t, 0)),
                b: vertex(index(t, 1)),
                c: vertex(index(t, 2)),
                material: materialOf(t),
                owner,
            });
        }
    }
}
