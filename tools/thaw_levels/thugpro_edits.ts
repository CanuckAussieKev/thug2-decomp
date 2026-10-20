// NOT ORIGINAL - THUGPRO's hand edits to its THAW level ports, applied to ours from the THUGPRO compatibility profile
// (tools/compat/thugpro/thaw_levels.json, register entries TP-5...TP-9 in its README.md). The user's decision
// (2026-10-20): everything THUGPRO changed in its versions of our ported levels is data in the profile, so that
// `--compat thugpro` (the default) gives THUGPRO's level and `--compat none` THAW's own. The data is extracted from
// THUGPRO's installed files against THAW's own by tools/thaw_levels/thugpro_extract.ts (`thugpro_compare.ts
// --extract-thugpro`) - never typed in; what would copy THUGPRO's own content (its meshes, models, scripts) is listed
// there as awaiting the user, not applied.
//
// Where each edit goes (thaw_levels.ts):
//   - applyGeometryEdits: THAW's scene and collision before they are converted (positions in THAW's frame) -
//     TP-5 rigid moves of sectors / collision objects, TP-6 the pieces whose AbsentInNetGames THUGPRO took off
//     brought from THAW's full zone archive (the net archive we convert hasn't them), TP-7 collision objects removed;
//   - applyNodeEdits: THAW's node array before it is converted (our names: the game-mode prefix already taken off) -
//     TP-5 moves / turns / renames / Links, TP-6 flags and keys, TP-7 nodes removed (placeholders, so that the Links
//     stay right), TP-8 nodes added (at the end, their Links by name);
//   - applyFaceEdits: the converted collision - TP-9 per-face flags and terrain.
// Without the profile (`--compat none`) none of these is called with any data: the output is THAW's alone.
//
// This module has no import of thaw_levels.ts (which imports it): the zone's data is passed in.

import { FormatError } from "../level_port/binio.ts";
import { checksum, Name, type QbValue, Struct, Vector } from "../level_port/qb.ts";
import type * as tex from "../level_port/tex.ts";
import * as thug2Col from "../level_port/thug2_col.ts";
import { Float, pyIntHex } from "../lib/py.ts";
import type * as thaw from "./thaw.ts";

// --- the data ---------------------------------------------------------------------------------------------------

// A qb value in JSON: numbers as they are (integers), {"f": x} a float, {"n": "Text"} a name, {"s": "text"} a string,
// {"v": [x, y, z]} a vector, {"p": [x, y]} a pair, lists as lists, {"struct": [[key or null, value], ...]}.
export type JsonValue =
    | number
    | { f: number }
    | { n: string }
    | { s: string }
    | { v: number[] }
    | { p: number[] }
    | { struct: [string | null, JsonValue][] }
    | JsonValue[];

// TP-5: a node moved / turned (THAW's frame), its Links (by the linked nodes' names) as THUGPRO has them.
export interface NodeMove {
    node: string;
    pos?: number[];
    angles?: number[];
    links?: string[];
}
// TP-5: a scene sector and / or collision object moved rigidly: p -> R_y(rotate_y) (p - pivot) + pivot + translate
// (THAW's frame). `scene` / `collision`: which of the two THUGPRO moved.
export interface Transform {
    translate: number[];
    rotate_y?: number;
    pivot?: number[];
    scene?: boolean;
    collision?: boolean;
}
// TP-6: a node's flags and keys as THUGPRO's.
export interface FlagEdit {
    node: string;
    add?: string[];
    remove?: string[];
    set?: Record<string, JsonValue>;
    unset?: string[];
}
// TP-8: an added node (Pos in THAW's frame; Links by name).
export interface AddedNode {
    entries: [string | null, JsonValue][];
    links?: string[];
}
// TP-9: a collision object's faces: [face index, flags set, flags cleared, terrain (-1: unchanged)]; `faces`: the
// object's face count (checked).
export interface FaceEdits {
    faces: number;
    edits: [number, number, number, number][];
}

export interface ZoneEdits {
    moved: NodeMove[];
    renamed: { node: string; to: string }[];
    geometry: Record<string, Transform>;
    flags: FlagEdit[];
    clusters: Record<string, string>; // TP-6: clusters THUGPRO renamed on every node (THAW's -> THUGPRO's)
    imported: string[]; // TP-6: sectors / collision objects brought from THAW's full archive
    removedNodes: string[];
    removedCollision: string[];
    removedSectors: string[];
    added: AddedNode[];
    faces: Record<string, FaceEdits>;
}

export function emptyEdits(): ZoneEdits {
    return {
        moved: [],
        renamed: [],
        geometry: {},
        flags: [],
        clusters: {},
        imported: [],
        removedNodes: [],
        removedCollision: [],
        removedSectors: [],
        added: [],
        faces: {},
    };
}

// The profile's sections (thaw_levels.json) for one zone, as ZoneEdits (null: nothing for the zone).
export function zoneEdits(sections: Record<string, Record<string, unknown>>, zone: string): ZoneEdits | null {
    const z = zone.toUpperCase();
    const get = <T>(section: string): T | undefined => sections[section]?.[z] as T | undefined;
    const e5 = get<{
        moved?: NodeMove[];
        renamed?: { node: string; to: string }[];
        geometry?: Record<string, Transform>;
    }>("node_edits");
    const e6 = get<{ nodes?: FlagEdit[]; clusters?: Record<string, string>; imported?: string[] }>("node_flags");
    const e7 = get<{ nodes?: string[]; collision?: string[]; sectors?: string[] }>("removed");
    const e8 = get<{ nodes?: AddedNode[] }>("added");
    const e9 = get<{ faces?: Record<string, FaceEdits> }>("collision");
    if (!e5 && !e6 && !e7 && !e8 && !e9) return null;
    return {
        moved: e5?.moved ?? [],
        renamed: e5?.renamed ?? [],
        geometry: e5?.geometry ?? {},
        flags: e6?.nodes ?? [],
        clusters: e6?.clusters ?? {},
        imported: e6?.imported ?? [],
        removedNodes: e7?.nodes ?? [],
        removedCollision: e7?.collision ?? [],
        removedSectors: e7?.sectors ?? [],
        added: e8?.nodes ?? [],
        faces: e9?.faces ?? {},
    };
}

// --- qb values <-> JSON -----------------------------------------------------------------------------------------

// A name's checksum from its text ("#1234ABCD" for one without a known text).
export const nameValue = (text: string): number => (text.startsWith("#") ? pyIntHex(text.slice(1)) : checksum(text));

export function nameOfValue(value: number, names: ReadonlyMap<number, string>): string {
    const text = names.get(value);
    return text !== undefined && checksum(text) === value
        ? text
        : `#${value.toString(16).toUpperCase().padStart(8, "0")}`;
}

function nameFor(text: string, names: Map<number, string>): Name {
    const value = nameValue(text);
    if (!text.startsWith("#") && !names.has(value)) names.set(value, text);
    return new Name(value);
}

export function encodeValue(v: QbValue, names: ReadonlyMap<number, string>): JsonValue {
    if (v instanceof Name) return { n: nameOfValue(v.value, names) };
    if (v instanceof Float) return { f: v.value };
    if (typeof v === "number") return Number.isInteger(v) ? v : { f: v };
    if (typeof v === "string") return { s: v };
    if (v instanceof Vector) return v.kind === "vector" ? { v: [...v.values] } : { p: [...v.values] };
    if (Array.isArray(v)) return v.map((x) => encodeValue(x, names));
    if (v instanceof Struct) {
        return {
            struct: v.entries.map(([k, x]) => [k === null ? null : nameOfValue(k.value, names), encodeValue(x, names)]),
        };
    }
    throw new FormatError(`a node value of a kind the profile can't hold (${String(v)})`);
}

export function decodeValue(v: JsonValue, names: Map<number, string>): QbValue {
    if (typeof v === "number") return v;
    if (Array.isArray(v)) return v.map((x) => decodeValue(x, names));
    if ("f" in v) return new Float(v.f);
    if ("n" in v) return nameFor(v.n, names);
    if ("s" in v) return v.s;
    if ("v" in v) return new Vector("vector", v.v);
    if ("p" in v) return new Vector("pair", v.p);
    return new Struct(v.struct.map(([k, x]) => [k === null ? null : nameFor(k, names), decodeValue(x, names)]));
}

// --- geometry (TP-5 moves, TP-6 imports, TP-7 removals) ---------------------------------------------------------

// The rigid move of a point (THAW's frame).
export function movePoint(p: readonly number[], t: Transform): number[] {
    const pivot = t.pivot ?? [0, 0, 0];
    const a = t.rotate_y ?? 0;
    let [x, y, z] = [p[0]! - pivot[0]!, p[1]! - pivot[1]!, p[2]! - pivot[2]!];
    if (a) [x, z] = [x * Math.cos(a) + z * Math.sin(a), -x * Math.sin(a) + z * Math.cos(a)];
    return [x + pivot[0]! + t.translate[0]!, y + pivot[1]! + t.translate[1]!, z + pivot[2]! + t.translate[2]!];
}

function rotateNormal(n: readonly number[], a: number): number[] {
    return [n[0]! * Math.cos(a) + n[2]! * Math.sin(a), n[1]!, -n[0]! * Math.sin(a) + n[2]! * Math.cos(a)];
}

const FVF_NORMAL = 0x010;

function moveSector(s: thaw.GeomSector, t: Transform): void {
    for (const m of s.meshes) {
        const v = Buffer.from(m.vertices);
        for (let i = 0; i < m.vertexCount; i++) {
            const at = i * m.stride;
            const p = movePoint([v.readFloatLE(at), v.readFloatLE(at + 4), v.readFloatLE(at + 8)], t);
            for (let k = 0; k < 3; k++) v.writeFloatLE(p[k]!, at + 4 * k);
            if (m.fvf & FVF_NORMAL && t.rotate_y) {
                const n = rotateNormal(
                    [v.readFloatLE(at + 12), v.readFloatLE(at + 16), v.readFloatLE(at + 20)],
                    t.rotate_y,
                );
                for (let k = 0; k < 3; k++) v.writeFloatLE(n[k]!, at + 12 + 4 * k);
            }
        }
        m.vertices = v;
    }
}

// A collision object moved: float vertices (exact - the fixed-point ones are relative to a box on the 1/16 grid), a
// new box, BSP tree (its splits are world coordinates) and reaches.
export function moveColObject(o: thug2Col.ColObject, t: Transform): thug2Col.ColObject {
    const verts = o.verts.map((v) => movePoint(v, t).map((x) => Math.fround(x)));
    const lo = [0, 1, 2].map((k) => Math.min(...verts.map((v) => v[k]!)));
    const hi = [0, 1, 2].map((k) => Math.max(...verts.map((v) => v[k]!)));
    const out: thug2Col.ColObject = {
        ...o,
        fixedVerts: 0,
        verts,
        bbox: verts.length ? [...lo, o.bbox[3]!, ...hi, o.bbox[7]!] : o.bbox,
        bsp: thug2Col.buildTree(verts, o.faces),
    };
    thug2Col.setReaches(out);
    return out;
}

// Where the THAW full zone archive's pieces come from (read when an edit needs them).
export interface FullArchive {
    geom: thaw.Geom;
    col: thug2Col.ColFile;
    tex: tex.TexFile;
}

// The zone's scene / collision / textures (THAW's, before converting) with THUGPRO's geometry edits: in place.
export function applyGeometryEdits(
    edits: ZoneEdits,
    geom: thaw.Geom,
    col: thug2Col.ColFile,
    textures: tex.TexFile,
    full: () => FullArchive,
    report: string[],
): void {
    // TP-6: pieces brought from the full archive (sector, its materials and textures; collision object)
    let imported = 0;
    if (edits.imported.length) {
        const f = full();
        const sectors = new Set(geom.sectors.map((s) => s.checksum));
        const objects = new Set(col.objects.map((o) => o.checksum));
        const materials = new Set(geom.materials.map((m) => m.checksum));
        const texs = new Set(textures.textures.map((t) => t.checksum));
        for (const text of edits.imported) {
            const c = nameValue(text);
            const s = f.geom.sectors.find((x) => x.checksum === c);
            if (s !== undefined && !sectors.has(c)) {
                geom.sectors.push(s);
                sectors.add(c);
                for (const m of s.meshes) {
                    const material = f.geom.materials.find((x) => x.checksum === m.material);
                    if (material === undefined || materials.has(m.material)) continue;
                    geom.materials.push(material);
                    materials.add(m.material);
                    for (const p of material.passes) {
                        const t = f.tex.textures.find((x) => x.checksum === p.texture);
                        if (t !== undefined && !texs.has(p.texture)) {
                            textures.textures.push(t);
                            texs.add(p.texture);
                        }
                    }
                }
                imported += 1;
            }
            const o = f.col.objects.find((x) => x.checksum === c);
            if (o !== undefined && !objects.has(c)) {
                col.objects.push(o);
                objects.add(c);
                imported += 1;
            }
            if (s === undefined && o === undefined) {
                throw new FormatError(`TP-6: ${text} is in neither THAW's full scene nor its collision`);
            }
        }
    }
    // TP-7: collision objects and sectors THUGPRO took out
    const goneCol = new Set(edits.removedCollision.map(nameValue));
    const goneSec = new Set(edits.removedSectors.map(nameValue));
    const colBefore = col.objects.length;
    const secBefore = geom.sectors.length;
    col.objects = col.objects.filter((o) => !goneCol.has(o.checksum));
    geom.sectors = geom.sectors.filter((s) => !goneSec.has(s.checksum));
    // TP-5: rigid moves
    let movedSectors = 0;
    let movedObjects = 0;
    for (const [text, t] of Object.entries(edits.geometry)) {
        const c = nameValue(text);
        if (t.scene !== false) {
            const s = geom.sectors.find((x) => x.checksum === c);
            if (s !== undefined) {
                moveSector(s, t);
                movedSectors += 1;
            }
        }
        if (t.collision !== false) {
            const at = col.objects.findIndex((x) => x.checksum === c);
            if (at >= 0) {
                col.objects[at] = moveColObject(col.objects[at]!, t);
                movedObjects += 1;
            }
        }
    }
    const parts = [
        imported ? `TP-6: ${imported} piece(s) from THAW's full archive` : "",
        colBefore - col.objects.length || secBefore - geom.sectors.length
            ? `TP-7: ${colBefore - col.objects.length} collision object(s), ${secBefore - geom.sectors.length} sector(s) out`
            : "",
        movedSectors || movedObjects
            ? `TP-5: ${movedSectors} sector(s), ${movedObjects} collision object(s) moved`
            : "",
    ].filter((x) => x);
    if (parts.length) report.push(`  ${parts.join("; ")}`);
}

// --- nodes (TP-5 moves / renames, TP-6 flags, TP-7 removed, TP-8 added) -------------------------------------------

// The zone's node array (THAW's, includes expanded, the game-mode names already ours) with THUGPRO's node edits; a new
// array (the same nodes, changed in place; removed ones placeholders; added ones at the end).
export function applyNodeEdits(
    edits: ZoneEdits,
    array: readonly QbValue[],
    names: Map<number, string>,
    report: string[],
): QbValue[] {
    const out = [...array];
    const index = new Map<number, number>();
    const reindex = (): void => {
        index.clear();
        out.forEach((n, i) => {
            const nm = n instanceof Struct ? n.get("Name") : undefined;
            if (nm instanceof Name && !index.has(nm.value)) index.set(nm.value, i);
        });
    };
    reindex();
    const find = (text: string, what: string): Struct => {
        const i = index.get(nameValue(text));
        const n = i === undefined ? undefined : out[i];
        if (!(n instanceof Struct)) throw new FormatError(`${what}: no node ${text} in the level`);
        return n;
    };
    const key = (text: string): Name => nameFor(text, names);
    const linksOf = (list: readonly string[]): number[] =>
        list.map((t) => index.get(nameValue(t))).filter((i): i is number => i !== undefined);
    // TP-5 renames first (the other edits name the nodes by THUGPRO's names)
    for (const r of edits.renamed) find(r.node, "TP-5 rename").set("Name", key(r.to));
    reindex();
    for (const m of edits.moved) {
        const n = find(m.node, "TP-5");
        if (m.pos !== undefined) n.set("Pos", new Vector("vector", m.pos)); // (THAW's nodes have Pos)
        if (m.angles !== undefined) n.set("Angles", new Vector("vector", m.angles));
        if (m.links !== undefined) {
            const l = linksOf(m.links);
            if (l.length) n.set("Links", l);
            else n.remove("Links");
        }
    }
    // TP-6: clusters renamed on all their nodes, then each node's flags and keys
    const clusters = new Map(Object.entries(edits.clusters).map(([a, b]) => [nameValue(a), b] as const));
    let renamedClusters = 0;
    if (clusters.size) {
        for (const n of out) {
            const c = n instanceof Struct ? n.get("Cluster") : undefined;
            const to = c instanceof Name ? clusters.get(c.value) : undefined;
            if (n instanceof Struct && to !== undefined) {
                n.set("Cluster", key(to));
                renamedClusters += 1;
            }
        }
    }
    for (const f of edits.flags) {
        const n = find(f.node, "TP-6");
        for (const flag of f.remove ?? []) n.removeFlag(flag);
        for (const flag of f.add ?? []) if (!n.hasFlag(flag)) n.entries.push([null, key(flag)]);
        for (const k of f.unset ?? []) n.remove(k);
        for (const [k, v] of Object.entries(f.set ?? {})) {
            key(k);
            n.set(k, decodeValue(v, names));
        }
    }
    // TP-7: placeholders of a class nothing keeps (the Links are renumbered later)
    const placeholder = new Struct([[new Name(checksum("Class")), new Name(checksum("LeftOut"))]]);
    for (const text of edits.removedNodes) {
        const i = index.get(nameValue(text));
        if (i === undefined) throw new FormatError(`TP-7: no node ${text} in the level`);
        out[i] = placeholder;
    }
    // TP-8: added nodes, then their Links (to each other too)
    const added: [Struct, readonly string[]][] = [];
    for (const a of edits.added) {
        const s = new Struct(a.entries.map(([k, v]) => [k === null ? null : key(k), decodeValue(v, names)]));
        out.push(s);
        added.push([s, a.links ?? []]);
    }
    reindex();
    for (const [s, links] of added) {
        const l = linksOf(links);
        if (l.length) s.set("Links", l);
    }
    if (!names.has(checksum("Links"))) names.set(checksum("Links"), "Links");
    const counts = [
        edits.moved.length ? `TP-5: ${edits.moved.length} node(s) moved` : "",
        edits.renamed.length ? `${edits.renamed.length} renamed` : "",
        edits.flags.length ? `TP-6: ${edits.flags.length} node(s) with THUGPRO's flags / keys` : "",
        renamedClusters ? `${renamedClusters} node(s) in THUGPRO's cluster names` : "",
        edits.removedNodes.length ? `TP-7: ${edits.removedNodes.length} node(s) out` : "",
        edits.added.length ? `TP-8: ${edits.added.length} node(s) added` : "",
    ].filter((x) => x);
    if (counts.length) report.push(`  ${counts.join("; ")}`);
    return out;
}

// --- collision faces (TP-9) -------------------------------------------------------------------------------------

// The converted collision's faces with THUGPRO's per-face flag and terrain edits: in place.
export function applyFaceEdits(edits: ZoneEdits, col: thug2Col.ColFile, report: string[]): void {
    let faces = 0;
    let objects = 0;
    for (const [text, e] of Object.entries(edits.faces)) {
        const o = col.objects.find((x) => x.checksum === nameValue(text));
        if (o === undefined) continue; // (an object the conversion left out)
        if (o.faces.length !== e.faces) {
            throw new FormatError(`TP-9: ${text} has ${o.faces.length} faces, the profile's edits are for ${e.faces}`);
        }
        for (const [i, set, clear, terrain] of e.edits) {
            const f = o.faces[i]!;
            f[0] = (f[0] | set) & ~clear & 0xffff;
            if (terrain >= 0) f[1] = terrain;
            faces += 1;
        }
        objects += 1;
    }
    if (faces) report.push(`  TP-9: ${faces} collision face(s) in ${objects} object(s) with THUGPRO's flags / terrain`);
}
