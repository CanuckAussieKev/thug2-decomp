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
        return this.sections[data]!.objects.filter((o) => o.className === className).map((o) => ({ section: data, at: o.at }));
    }

    classOf(p: Ptr): string | null {
        return this.sections[p.section]!.objects.find((o) => o.at === p.at)?.className ?? null;
    }

    // An hkArray member: [pointer to the elements, count].
    array(obj: Ptr, className: string, member: string): [Ptr | null, number] {
        const m = this.member(className, member);
        if (m.type !== T.ARRAY && m.type !== T.SIMPLEARRAY) throw new FormatError(`${className}.${member}: not an array`);
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
