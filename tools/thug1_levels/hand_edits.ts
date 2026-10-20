// NOT ORIGINAL - THUGPRO's hand edits to its ports of THUG1's levels, as data our converter applies (the THUGPRO
// compatibility register, tools/compat/thugpro/README.md, TP-32 ... TP-39; the user's decision 2026-10-20: everything
// THUGPRO changed in our ported levels is data in the register, switched by --compat). The data is extracted from
// THUGPRO's install against THUG1's own files by `node tools/thug1_levels/thugpro_compare.ts --extract-thugpro`
// (thugpro_edits.handEdits) into tools/compat/thugpro/thug1_levels.json - never typed in. Sections (each by level code):
//   moved_nodes        TP-32  {node: {pos, angles}}: nodes THUGPRO moved (not the spawns: TP-31's)
//   net_presence       TP-33  {absent: [...], present: [...]}: AbsentInNetGames put on / taken off
//   created_at_start   TP-34  {on: [...], off: [...]}: CreatedAtStart put on / taken off
//   node_values        TP-35  {node: {key: value | null, "+Flag" / "-Flag": true}}: classes, rail / waypoint types and
//                             terrains, clusters, other flags and values
//   rails              TP-36  {added: [node...], reused: {node: {pos, angles, values}}, links: {node: [names]}}: rails
//                             THUGPRO added (node data only; "reused": a THUG1 rail point it moved into a new rail),
//                             and links it changed
//   face_values        TP-37  {object: {faces, set: {bit: [faces]}, clear: {bit: [faces]}, terrain: [[face, t]...]}}:
//                             per-face collision flags (the wall-ride flag 0x4...) and terrain
//   collision_removed  TP-38  {object: {faces, remove: "all" | [faces]}}: collision taken out
//   collision_reshaped TP-39  {object: {faces, verts: [[vertex, x, y, z]...]}}: collision vertices moved
// The node edits are made to THUG1's node array before it is converted (applyNodeEdits: our converter then treats them
// as THUG1's own), the collision edits to the converted collision (applyColEdits: faces and vertices are THUG1's,
// one for one). Values in JSON (encode / decode): a name (checksum) is its text ("#XXXXXXXX" when unknown), an integer
// a number, a float {"float": x}, a string {"string": s}, a vector {"vector": [x, y, z]} ({"pair": [a, b]}), a list a
// list. `--compat none` leaves every section out (then the output is the same as before these entries).

import { FormatError } from "../level_port/binio.ts";
import * as qb from "../level_port/qb.ts";
import { Name, type QbValue, Struct, Vector } from "../level_port/qb.ts";
import * as thug2Col from "../level_port/thug2_col.ts";
import { Float, hex8 } from "../lib/py.ts";

export type Json = string | number | boolean | null | Json[] | { [key: string]: Json };

export interface Placement {
    pos: number[];
    angles: number[];
}

export interface FaceValues {
    faces: number; // the object's face count (THUG1's = ours): the edits apply only when it agrees
    set?: Record<string, number[]>; // flag bit ("0x0004") -> faces getting it
    clear?: Record<string, number[]>; // flag bit -> faces losing it
    terrain?: [number, number][]; // [face, terrain]
}

export interface ColRemoval {
    faces: number;
    remove: "all" | number[];
}

export interface ColReshape {
    faces: number;
    verts: [number, number, number, number][]; // [vertex, x, y, z]
}

// One level's edits (each field one register entry).
export interface LevelEdits {
    moved_nodes?: Record<string, Placement>;
    net_presence?: { absent?: string[]; present?: string[] };
    created_at_start?: { on?: string[]; off?: string[] };
    node_values?: Record<string, Record<string, Json>>;
    rails?: {
        added?: Record<string, Json>[];
        reused?: Record<string, Placement & { values: Record<string, Json> }>;
        links?: Record<string, string[]>;
    };
    face_values?: Record<string, FaceValues>;
    collision_removed?: Record<string, ColRemoval>;
    collision_reshaped?: Record<string, ColReshape>;
}

export type Section = keyof LevelEdits;

// The sections and their register entries, in the order they are applied.
export const SECTIONS: readonly (readonly [Section, string, string])[] = [
    ["moved_nodes", "TP-32", "nodes THUGPRO moved"],
    ["net_presence", "TP-33", "AbsentInNetGames put on / taken off"],
    ["created_at_start", "TP-34", "CreatedAtStart put on / taken off"],
    ["node_values", "TP-35", "classes, rail / waypoint types and terrains, other flags and values"],
    ["rails", "TP-36", "rails added from node data, links changed"],
    ["face_values", "TP-37", "per-face collision flags and terrain"],
    ["collision_removed", "TP-38", "collision objects and faces taken out"],
    ["collision_reshaped", "TP-39", "collision vertices moved"],
];

// --- values ---------------------------------------------------------------------------------------------------------

export function nameText(value: number, names: ReadonlyMap<number, string>): string {
    const text = names.get(value);
    return text !== undefined && qb.checksum(text) === value ? text : `#${hex8(value)}`;
}

// A name's checksum ("#XXXXXXXX" as it is).
export const checksumOf = (text: string): number =>
    text.startsWith("#") ? Number.parseInt(text.slice(1), 16) >>> 0 : qb.checksum(text);

export function nameOf(text: string, names: Map<number, string>): Name {
    const value = checksumOf(text);
    if (!text.startsWith("#") && !names.has(value)) names.set(value, text);
    return new Name(value);
}

export function encode(v: QbValue, names: ReadonlyMap<number, string>): Json {
    if (v instanceof Name) return nameText(v.value, names);
    if (v instanceof Float) return { float: v.value };
    if (typeof v === "number") return v;
    if (typeof v === "string") return { string: v };
    if (v instanceof Vector) return { [v.kind]: [...v.values] };
    if (Array.isArray(v)) return v.map((x) => encode(x, names));
    if (v instanceof Struct) {
        return {
            struct: v.entries.map(([k, x]) => [k === null ? null : nameText(k.value, names), encode(x, names)]),
        };
    }
    throw new FormatError(`no JSON form for ${String(v)}`);
}

export function decode(j: Json, names: Map<number, string>): QbValue {
    if (typeof j === "string") return nameOf(j, names);
    if (typeof j === "number") return j;
    if (Array.isArray(j)) return j.map((x) => decode(x, names));
    if (j !== null && typeof j === "object") {
        if (typeof j.float === "number") return new Float(j.float);
        if (typeof j.string === "string") return j.string;
        if (Array.isArray(j.vector)) return new Vector("vector", j.vector as number[]);
        if (Array.isArray(j.pair)) return new Vector("pair", j.pair as number[]);
        if (Array.isArray(j.struct)) {
            return new Struct(
                (j.struct as [string | null, Json][]).map(([k, x]) => [
                    k === null ? null : nameOf(k, names),
                    decode(x, names),
                ]),
            );
        }
    }
    throw new FormatError(`no qb value for ${JSON.stringify(j)}`);
}

// --- nodes ------------------------------------------------------------------------------------------------------------

const LINKS = qb.checksum("Links");

function nodeChecksum(n: Struct): number | null {
    const v = n.get("Name");
    return v instanceof Name ? v.value : null;
}

function setFlag(n: Struct, flag: string, on: boolean, names: Map<number, string>): void {
    const value = nameOf(flag, names).value;
    n.entries = n.entries.filter(([k, v]) => !(k === null && v instanceof Name && v.value === value));
    if (on) n.entries.push([null, new Name(value)]);
}

function setValue(n: Struct, key: string, value: Json, names: Map<number, string>): void {
    const k = nameOf(key, names);
    if (value === null) {
        n.entries = n.entries.filter(([e]) => !(e !== null && e.value === k.value));
        return;
    }
    const decoded = decode(value, names);
    const at = n.entries.findIndex(([e]) => e !== null && e.value === k.value);
    if (at >= 0) n.entries[at] = [k, decoded];
    else n.entries.push([k, decoded]);
}

// THUGPRO's node edits on THUG1's node array (before conversion): each edited node is a fresh copy in its place; rails
// are appended (their Links by name). Returns report lines (per section, what was done).
export function applyNodeEdits(
    array: QbValue[],
    names: Map<number, string>,
    edits: LevelEdits,
    code: string,
): string[] {
    const index = new Map<number, number>(); // node name checksum -> its place in the array (the first)
    array.forEach((n, i) => {
        const key = n instanceof Struct ? nodeChecksum(n) : null;
        if (key !== null && !index.has(key)) index.set(key, i);
    });
    const copies = new Set<Struct>(); // (the nodes already copied: edited in place from then on)
    const node = (name: string, entry: string): Struct => {
        const i = index.get(checksumOf(name));
        if (i === undefined) throw new FormatError(`${code}: ${entry} names ${name}, not a node of the level`);
        let n = array[i] as Struct;
        if (!copies.has(n)) {
            n = new Struct([...n.entries]);
            copies.add(n);
            array[i] = n;
        }
        return n;
    };
    const report: string[] = [];
    const moved = Object.entries(edits.moved_nodes ?? {});
    for (const [name, place] of moved) {
        const n = node(name, "TP-32");
        n.set("Pos", new Vector("vector", place.pos));
        n.set("Angles", new Vector("vector", place.angles));
    }
    if (moved.length) report.push(`TP-32 moved ${moved.length}`);
    const net = edits.net_presence ?? {};
    for (const name of net.absent ?? []) setFlag(node(name, "TP-33"), "AbsentInNetGames", true, names);
    for (const name of net.present ?? []) setFlag(node(name, "TP-33"), "AbsentInNetGames", false, names);
    if (net.absent?.length || net.present?.length)
        report.push(`TP-33 net games: ${net.present?.length ?? 0} present, ${net.absent?.length ?? 0} absent`);
    const created = edits.created_at_start ?? {};
    for (const name of created.on ?? []) setFlag(node(name, "TP-34"), "CreatedAtStart", true, names);
    for (const name of created.off ?? []) setFlag(node(name, "TP-34"), "CreatedAtStart", false, names);
    if (created.on?.length || created.off?.length)
        report.push(`TP-34 created at start: ${created.on?.length ?? 0} on, ${created.off?.length ?? 0} off`);
    const values = Object.entries(edits.node_values ?? {});
    const change = (n: Struct, changes: Record<string, Json>): void => {
        for (const [key, value] of Object.entries(changes)) {
            if (key.startsWith("+") || key.startsWith("-")) setFlag(n, key.slice(1), key.startsWith("+"), names);
            else setValue(n, key, value, names);
        }
    };
    for (const [name, changes] of values) change(node(name, "TP-35"), changes);
    if (values.length) report.push(`TP-35 values on ${values.length} nodes`);
    const rails = edits.rails ?? {};
    const reused = Object.entries(rails.reused ?? {});
    for (const [name, r] of reused) {
        const n = node(name, "TP-36");
        n.set("Pos", new Vector("vector", r.pos));
        n.set("Angles", new Vector("vector", r.angles));
        change(n, r.values);
    }
    const added = rails.added ?? [];
    const pending: [Struct, string[]][] = [];
    for (const fields of added) {
        const entries: qb.Entry[] = [];
        let links: string[] = [];
        for (const [key, value] of Object.entries(fields)) {
            if (key === "flags") for (const f of value as string[]) entries.push([null, nameOf(f, names)]);
            else if (key === "Links") links = value as string[];
            else entries.push([nameOf(key, names), decode(value, names)]);
        }
        const n = new Struct(entries);
        const key = nodeChecksum(n);
        if (key === null || index.has(key))
            throw new FormatError(`${code}: TP-36 adds ${String(fields.Name)}, unnamed or a name the level has`);
        index.set(key, array.length);
        copies.add(n);
        array.push(n);
        pending.push([n, links]);
    }
    const linkTo = (targets: readonly string[], entry: string): number[] =>
        targets.map((l) => {
            const i = index.get(checksumOf(l));
            if (i === undefined) throw new FormatError(`${code}: ${entry} links to ${l}, not a node of the level`);
            return i;
        });
    for (const [n, links] of pending)
        if (links.length) n.entries.push([nameOf("Links", names), linkTo(links, "TP-36")]);
    const relinked = Object.entries(rails.links ?? {});
    for (const [name, links] of relinked) {
        const n = node(name, "TP-36");
        n.entries = n.entries.filter(([k]) => !(k !== null && k.value === LINKS));
        if (links.length) n.entries.push([nameOf("Links", names), linkTo(links, "TP-36")]);
    }
    if (added.length || reused.length || relinked.length)
        report.push(`TP-36 rails: ${added.length} added, ${reused.length} reused, ${relinked.length} relinked`);
    return report.length ? [`  THUGPRO hand edits: ${report.join("; ")}`] : [];
}

// --- collision --------------------------------------------------------------------------------------------------------

function checked(
    objects: ReadonlyMap<number, thug2Col.ColObject>,
    name: string,
    faces: number,
    entry: string,
    code: string,
): thug2Col.ColObject {
    const o = objects.get(checksumOf(name));
    if (o === undefined) throw new FormatError(`${code}: ${entry} names collision ${name}, not in the level`);
    if (o.faces.length !== faces) {
        throw new FormatError(`${code}: ${entry}: ${name} has ${o.faces.length} faces, the data ${faces}`);
    }
    return o;
}

function rebuild(o: thug2Col.ColObject): void {
    thug2Col.setReaches(o);
    o.bsp = o.faces.length ? thug2Col.buildTree(o.verts, o.faces) : { kind: "leaf", faces: [], low: 3 };
}

// THUGPRO's collision edits on the converted collision (THUG1's objects, faces and vertices one for one). Returns
// [collision, report lines].
export function applyColEdits(col: thug2Col.ColFile, edits: LevelEdits, code: string): [thug2Col.ColFile, string[]] {
    const objects = new Map(col.objects.map((o) => [o.checksum, o]));
    const report: string[] = [];
    let faces = 0;
    for (const [name, e] of Object.entries(edits.face_values ?? {})) {
        const o = checked(objects, name, e.faces, "TP-37", code);
        const f = o.faces.map((x) => [...x] as thug2Col.Face);
        for (const [bit, list] of Object.entries(e.set ?? {})) for (const i of list) f[i]![0] |= Number(bit);
        for (const [bit, list] of Object.entries(e.clear ?? {})) for (const i of list) f[i]![0] &= ~Number(bit);
        for (const [i, terrain] of e.terrain ?? []) f[i]![1] = terrain;
        faces += f.filter((x, i) => x[0] !== o.faces[i]![0] || x[1] !== o.faces[i]![1]).length;
        o.faces = f;
    }
    if (faces) report.push(`TP-37 ${faces} faces' flags / terrain`);
    const drop = new Set<thug2Col.ColObject>();
    let removedFaces = 0;
    for (const [name, e] of Object.entries(edits.collision_removed ?? {})) {
        const o = checked(objects, name, e.faces, "TP-38", code);
        if (e.remove === "all") {
            drop.add(o);
            continue;
        }
        const gone = new Set(e.remove);
        o.faces = o.faces.filter((_, i) => !gone.has(i));
        removedFaces += gone.size;
        rebuild(o);
    }
    if (drop.size || removedFaces) report.push(`TP-38 ${drop.size} collision objects and ${removedFaces} faces out`);
    let verts = 0;
    for (const [name, e] of Object.entries(edits.collision_reshaped ?? {})) {
        const o = checked(objects, name, e.faces, "TP-39", code);
        o.verts = o.verts.map((v) => [...v]);
        for (const [i, x, y, z] of e.verts) {
            if (o.verts[i] === undefined) throw new FormatError(`${code}: TP-39: ${name} has no vertex ${i}`);
            if (o.fixedVerts && [x, y, z].some((c, k) => c < o.bbox[k]! || c - o.bbox[k]! >= 4095)) {
                throw new FormatError(`${code}: TP-39: ${name}'s vertex ${i} leaves its fixed-point box`);
            }
            o.verts[i] = [x, y, z];
            verts += 1;
        }
        rebuild(o);
    }
    if (verts) report.push(`TP-39 ${verts} collision vertices moved`);
    const out: thug2Col.ColFile = { ...col, objects: col.objects.filter((o) => !drop.has(o)) };
    return [out, report.length ? [`  THUGPRO hand edits: ${report.join("; ")}`] : []];
}

// --- counting and checking --------------------------------------------------------------------------------------------

// How many edits of each section a level has (the unit: nodes, rails, faces, objects, vertices).
export function editCounts(e: LevelEdits): string[] {
    const n = (o: object | undefined): number => (o === undefined ? 0 : Object.keys(o).length);
    const faces = Object.values(e.face_values ?? {}).reduce(
        (sum, f) =>
            sum +
            new Set([
                ...Object.values(f.set ?? {}).flat(),
                ...Object.values(f.clear ?? {}).flat(),
                ...(f.terrain ?? []).map(([i]) => i),
            ]).size,
        0,
    );
    const removed = Object.values(e.collision_removed ?? {});
    const counts: [string, number, string][] = [
        ["TP-32", n(e.moved_nodes), "moved"],
        ["TP-33", (e.net_presence?.present?.length ?? 0) + (e.net_presence?.absent?.length ?? 0), "net presence"],
        ["TP-34", (e.created_at_start?.on?.length ?? 0) + (e.created_at_start?.off?.length ?? 0), "created at start"],
        ["TP-35", n(e.node_values), "nodes' values"],
        ["TP-36", (e.rails?.added?.length ?? 0) + n(e.rails?.reused) + n(e.rails?.links), "rail nodes / links"],
        ["TP-37", faces, "faces"],
        ["TP-38", removed.length, `collision objects (${removed.filter((r) => r.remove === "all").length} whole)`],
        ["TP-39", Object.values(e.collision_reshaped ?? {}).reduce((s, r) => s + r.verts.length, 0), "vertices"],
    ];
    return counts.filter(([, c]) => c).map(([tp, c, what]) => `${tp} ${c} ${what}`);
}

export interface SectionCheck {
    total: number; // edits looked at
    same: number; // as the data (= THUGPRO's) in our converted level
    differ: Map<string, string[]>; // reason -> names
}

// Our converted level against the data, edit by edit: whether each applied edit shows in the output (ourNodes: the
// converted node array, its includes merged; ourCol: the converted collision). Misses are explained by our converter
// (a node it leaves out, a key or flag it doesn't keep, its network-game safety marks).
export function checkEdits(
    e: LevelEdits,
    ourNodes: readonly Struct[],
    names: ReadonlyMap<number, string>,
    ourCol: thug2Col.ColFile | null,
): Map<string, SectionCheck> {
    const out = new Map<string, SectionCheck>();
    const byName = new Map<number, Struct>();
    for (const n of ourNodes) {
        const key = nodeChecksum(n);
        if (key !== null && !byName.has(key)) byName.set(key, n);
    }
    const tally = (tp: string): SectionCheck => {
        let c = out.get(tp);
        if (c === undefined) {
            c = { total: 0, same: 0, differ: new Map() };
            out.set(tp, c);
        }
        return c;
    };
    const result = (tp: string, ok: boolean, reason: string, name: string): void => {
        const c = tally(tp);
        c.total += 1;
        if (ok) c.same += 1;
        else c.differ.set(reason, [...(c.differ.get(reason) ?? []), name]);
    };
    const pos = (n: Struct): readonly number[] | null => {
        const v = n.get("Pos");
        return v instanceof Vector ? v.values : null;
    };
    const near = (a: readonly number[] | null, b: readonly number[], tolerance = 0.1): boolean =>
        a?.every((x, i) => Math.abs(x - b[i]!) <= tolerance) ?? false;
    const flag = (n: Struct, f: string): boolean => {
        const value = checksumOf(f);
        return n.entries.some(([k, v]) => k === null && v instanceof Name && v.value === value);
    };
    const linkNames = (n: Struct): string =>
        (Array.isArray(n.get("Links")) ? (n.get("Links") as QbValue[]) : [])
            .map((i) => {
                const m = typeof i === "number" ? ourNodes[i] : undefined;
                const k = m ? nodeChecksum(m) : null;
                return k === null ? "?" : nameText(k, names).toLowerCase();
            })
            .join(",");
    const valueSame = (n: Struct, key: string, value: Json): boolean => {
        const k = checksumOf(key);
        const ours = n.entries.find(([e]) => e !== null && e.value === k)?.[1];
        if (value === null) return ours === undefined;
        return ours !== undefined && JSON.stringify(encode(ours, names)) === JSON.stringify(normal(value));
    };
    // (a value as encode writes it: names in their checksum's text, so "#..." and names compare alike)
    const normal = (v: Json): Json => encode(decode(v, new Map(names)), names);
    const each = (tp: string, name: string, test: (n: Struct) => [boolean, string]): void => {
        const n = byName.get(checksumOf(name));
        if (n === undefined) {
            result(tp, false, "node not in our port (our converter leaves it out)", name);
            return;
        }
        const [ok, reason] = test(n);
        result(tp, ok, reason, name);
    };
    for (const [name, p] of Object.entries(e.moved_nodes ?? {}))
        each("TP-32", name, (n) => [near(pos(n), p.pos), "not at THUGPRO's place"]);
    const flags = (tp: string, f: string, on: readonly string[] | undefined, off: readonly string[] | undefined) => {
        for (const name of on ?? []) each(tp, name, (n) => [flag(n, f), `${f} not on (our converter's)`]);
        for (const name of off ?? []) each(tp, name, (n) => [!flag(n, f), `${f} still on (our converter's)`]);
    };
    flags("TP-33", "AbsentInNetGames", e.net_presence?.absent, e.net_presence?.present);
    flags("TP-34", "CreatedAtStart", e.created_at_start?.on, e.created_at_start?.off);
    const values = (n: Struct, changes: Record<string, Json>): [boolean, string] => {
        const bad = Object.entries(changes)
            .filter(([k, v]) =>
                k.startsWith("+")
                    ? !flag(n, k.slice(1))
                    : k.startsWith("-")
                      ? flag(n, k.slice(1))
                      : !valueSame(n, k, v),
            )
            .map(([k]) => k);
        return [!bad.length, `not kept by our converter: ${bad.join(" ")}`];
    };
    for (const [name, changes] of Object.entries(e.node_values ?? {})) each("TP-35", name, (n) => values(n, changes));
    for (const fields of e.rails?.added ?? []) {
        const name = String(fields.Name);
        each("TP-36", name, (n) => {
            const p = (fields.Pos as { vector: number[] }).vector;
            const links = ((fields.Links as string[] | undefined) ?? []).join(",").toLowerCase();
            return [near(pos(n), p) && linkNames(n) === links, "not at its place or links differ"];
        });
    }
    for (const [name, r] of Object.entries(e.rails?.reused ?? {}))
        each("TP-36", name, (n) => {
            const [ok, reason] = values(n, r.values);
            return [ok && near(pos(n), r.pos), near(pos(n), r.pos) ? reason : "not at its new place"];
        });
    for (const [name, links] of Object.entries(e.rails?.links ?? {}))
        each("TP-36", name, (n) => [linkNames(n) === links.join(",").toLowerCase(), "links differ"]);
    const objects = new Map((ourCol?.objects ?? []).map((o) => [o.checksum, o]));
    const object = (tp: string, name: string, faces: number, test: (o: thug2Col.ColObject) => [boolean, string]) => {
        const o = objects.get(checksumOf(name));
        if (o === undefined) result(tp, false, "collision object not in our port", name);
        else if (o.faces.length !== faces) result(tp, false, `our object has ${o.faces.length} faces`, name);
        else {
            const [ok, reason] = test(o);
            result(tp, ok, reason, name);
        }
    };
    for (const [name, f] of Object.entries(e.face_values ?? {})) {
        object("TP-37", name, f.faces, (o) => {
            const ok =
                Object.entries(f.set ?? {}).every(([bit, list]) => list.every((i) => o.faces[i]![0] & Number(bit))) &&
                Object.entries(f.clear ?? {}).every(([bit, list]) =>
                    list.every((i) => !(o.faces[i]![0] & Number(bit))),
                ) &&
                (f.terrain ?? []).every(([i, t]) => o.faces[i]![1] === t);
            return [ok, "face flags / terrain differ"];
        });
    }
    for (const [name, r] of Object.entries(e.collision_removed ?? {})) {
        if (r.remove === "all") result("TP-38", !objects.has(checksumOf(name)), "collision object still there", name);
        else object("TP-38", name, r.faces - r.remove.length, () => [true, ""]);
    }
    for (const [name, r] of Object.entries(e.collision_reshaped ?? {}))
        object("TP-39", name, r.faces, (o) => [
            r.verts.every(([i, x, y, z]) => near(o.verts[i] ?? null, [x, y, z])),
            "vertices differ",
        ]);
    return out;
}
