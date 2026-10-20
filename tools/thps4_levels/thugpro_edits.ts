// NOT ORIGINAL - THUGPRO's hand edits to its ports of THPS4's levels, as data in the compatibility profile
// (tools/compat/thugpro/thps4_levels.json, README.md there: TP-22 ... TP-29), and their applying to our converted node
// arrays and collision. The user, 2026-10-20: "Everything [THUGPRO] changed needs to be in the differences files that
// we can control with the compat flag" - `--compat thugpro` (the default) applies all of it, `--compat none` gives the
// output from THPS4's files alone.
//
// extractEdits reads THUGPRO's installed level (read only: <thugpro>\data\pre\<level>_scripts.prx, <level>col.prx)
// against THPS4's own (Game\data\levels\<Lvl>\<Lvl>.qb, <Lvl>col.dat) and keeps only node data - places, flags,
// names, rail terrains, added rails / restarts / lights as values; never THUGPRO's scripts or models (those are listed
// for the user: docs/pending/thps4-thugpro.md). It is run by `thugpro_compare.ts --extract-thugpro` (never typed in).
// applyEdits makes the converted nodes (thps4_levels.cmdConvert, our own node form) agree; checkEdits says per
// category how many of the profile's edits a converted node array matches (thugpro_inventory.ts --thug2).
//
//   TP-22 node_moves       shared nodes THUGPRO moved (> 0.1 unit) or turned (> 0.001 rad): its Pos / Angles
//   TP-23 node_renames     a THPS4 node THUGPRO renamed (same class, same place): applied where nothing of ours
//                          still names the old one
//   TP-24 node_flags       CreatedAtStart / AbsentInNetGames / NetEnabled / Permanent added or taken off shared
//                          restarts, rails, level geometry and level objects (what is there at the start / online)
//   TP-25 trick_objects    TrickObject (and its Cluster) on shared geometry and rails (graffiti / trick attack)
//   TP-26 rail_terrains    a shared rail's TerrainType (and Type): its grind sound and sparks
//   TP-27 removed_nodes / added_nodes   THPS4 nodes THUGPRO took out; nodes it added that are node data only: rails,
//                          generic restarts (not network spawns: TP-21), level lights
//   TP-28 collision_duplicates   collision objects whose duplicate faces THUGPRO removed (same surfaces)
//   TP-29 door_walls       (off: enabled false) the skate shop's invisible door walls kept as THUGPRO's - against the
//                          user's earlier request (the door open: thps4_levels.OPENINGS)

import * as fs from "node:fs";
import * as prx from "../level_port/prx.ts";
import * as qb from "../level_port/qb.ts";
import { Name, type QbValue, Struct, Vector } from "../level_port/qb.ts";
import * as thug2Col from "../level_port/thug2_col.ts";
import { Float, hex8, intFromBytesLE } from "../lib/py.ts";
import { findPath } from "../thaw_levels/thaw_levels.ts";
import { maxDiff, nodeArray, position, TOLERANCE } from "../thaw_levels/thugpro_compare.ts";
import * as thps4 from "./thps4.ts";

export const ANGLE_TOLERANCE = 0.001;

// The flags TP-24 carries (lower case -> as written) and the classes it looks at.
export const PRESENCE_FLAGS: ReadonlyMap<string, string> = new Map([
    ["createdatstart", "CreatedAtStart"],
    ["absentinnetgames", "AbsentInNetGames"],
    ["netenabled", "NetEnabled"],
    ["permanent", "Permanent"],
]);
export const SHAPE_CLASSES: ReadonlySet<string> = new Set(["restart", "railnode", "levelgeometry", "levelobject"]);
// Restart types a network game uses (TP-21 / the spawns: not TP-27's).
const NET_TYPES: ReadonlySet<string> = new Set(["multiplayer", "horse", "ctf", "ctf_1", "ctf_2", "team"]);

// --- the data -------------------------------------------------------------------------------------------------------

export interface Move {
    pos?: number[]; // THUG2's Pos axes (a legacy Position has z negated)
    angles?: number[];
}
export interface FlagEdit {
    add?: string[];
    remove?: string[];
}
export interface TrickEdit {
    trick_object: boolean;
    cluster: string | null;
}
export interface TerrainEdit {
    type: string | null;
    terrain: string;
}
export interface Light {
    brightness: number;
    inner_radius: number;
    outer_radius: number;
    color: number[];
    exclude_geometry: string[];
    flags: string[];
}
export interface AddedNode {
    name: string;
    class: "RailNode" | "Restart" | "LevelLight";
    type?: string;
    pos: number[];
    angles: number[];
    terrain?: string;
    links?: string[];
    created_at_start: boolean;
    light?: Light;
}
export interface LevelEdits {
    moves: Record<string, Move>;
    renames: Record<string, string>;
    flags: Record<string, FlagEdit>;
    tricks: Record<string, TrickEdit>;
    terrains: Record<string, TerrainEdit>;
    removed: string[];
    added: AddedNode[];
    duplicates: string[];
    // what extractEdits saw and left out (not data: THUGPRO's scripts' nodes, class changes...), for the report
    leftOut: string[];
}

export const emptyEdits = (): LevelEdits => ({
    moves: {},
    renames: {},
    flags: {},
    tricks: {},
    terrains: {},
    removed: [],
    added: [],
    duplicates: [],
    leftOut: [],
});

// The profile's sections (thps4_levels.json) and their register ids.
export const SECTIONS = {
    node_moves: "TP-22",
    node_renames: "TP-23",
    node_flags: "TP-24",
    trick_objects: "TP-25",
    rail_terrains: "TP-26",
    removed_nodes: "TP-27",
    added_nodes: "TP-27",
    collision_duplicates: "TP-28",
} as const;
export type SectionName = keyof typeof SECTIONS;

// One level's edits from the profile's sections ({section: {level: data}}; levels by lower-case name).
export function levelEdits(sections: ReadonlyMap<SectionName, ReadonlyMap<string, unknown>>, base: string): LevelEdits {
    const get = <T>(s: SectionName, empty: T): T =>
        (sections.get(s)?.get(base.toLowerCase()) as T | undefined) ?? empty;
    return {
        moves: get("node_moves", {}),
        renames: get("node_renames", {}),
        flags: get("node_flags", {}),
        tricks: get("trick_objects", {}),
        terrains: get("rail_terrains", {}),
        removed: get("removed_nodes", []),
        added: get("added_nodes", []),
        duplicates: get("collision_duplicates", []),
        leftOut: [],
    };
}

// --- extracting (THUGPRO's install against THPS4's) -------------------------------------------------------------------

type Names = Map<number, string>;

const text = (v: QbValue | undefined, names: Names): string =>
    v instanceof Name ? (names.get(v.value) ?? hex8(v.value)) : "";
const lower = (v: QbValue | undefined, names: Names): string => text(v, names).toLowerCase();

function anglesOf(n: Struct): number[] {
    const a = n.get("Angles");
    return a instanceof Vector ? [...a.values] : [0, 0, 0];
}

function flagSet(n: Struct, names: Names): Set<string> {
    const out = new Set<string>();
    for (const [k, v] of n.entries) if (k === null && v instanceof Name) out.add(lower(v, names));
    return out;
}

function restartTypes(n: Struct, names: Names): string[] {
    const t = n.get("restart_types");
    const list = Array.isArray(t) ? t.map((x) => lower(x, names)) : [];
    if (!list.length) list.push(lower(n.get("Type"), names));
    return list;
}

const LIGHT_KEYS = new Set(["pos", "position", "angles", "name", "class", "brightness", "innerradius", "outerradius"]);

export function extractEdits(base: string, key: string, thugpro: string, thps4Dir: string): LevelEdits {
    const out = emptyEdits();
    const scripts = findPath(thugpro, "data", "pre", `${key}_scripts.prx`);
    const data = scripts === null ? null : prx.find(scripts, `levels\\${key}\\${key}.qb`);
    if (data === null) throw new TypeError(`THUGPRO has no levels\\${key}\\${key}.qb`);
    const [tpArray, tpNames] = nodeArray(data, `${key}.qb`);
    const folder = findPath(thps4Dir, "Game", "data", "levels", base);
    const ownFile = folder === null ? null : findPath(folder, `${base}.qb`);
    if (folder === null || ownFile === null) throw new TypeError(`no THPS4 ${base}.qb in ${thps4Dir}`);
    const ownQ = new qb.QbFile(fs.readFileSync(ownFile), ownFile);
    const ownArray = ownQ.get("NodeArray") as QbValue[];
    // (one name table for both: THPS4's spelling first)
    const names: Names = new Map(ownQ.names);
    for (const [k, v] of tpNames) if (!names.has(k)) names.set(k, v);
    const own = ownArray.filter((n): n is Struct => n instanceof Struct);
    const tp = tpArray.filter((n): n is Struct => n instanceof Struct);
    const byName = (list: Struct[]): Map<number, Struct> => {
        const m = new Map<number, Struct>();
        for (const n of list) {
            const nm = n.get("Name");
            if (nm instanceof Name && !m.has(nm.value)) m.set(nm.value, n);
        }
        return m;
    };
    const ownBy = byName(own);
    const tpBy = byName(tp);
    const label = (n: Struct): string => text(n.get("Name"), names);

    // removed / added, renames (a removed node and an added one of the same class at the same place)
    const removed = own.filter((n) => {
        const nm = n.get("Name");
        return nm instanceof Name && !tpBy.has(nm.value);
    });
    const added = tp.filter((n) => {
        const nm = n.get("Name");
        return nm instanceof Name && !ownBy.has(nm.value);
    });
    const renamedTp = new Set<Struct>();
    for (const n of removed) {
        const p = position(n);
        const match =
            p === null
                ? undefined
                : added.find((t) => {
                      const q = position(t);
                      return (
                          !renamedTp.has(t) &&
                          lower(t.get("Class"), names) === lower(n.get("Class"), names) &&
                          q !== null &&
                          maxDiff(p, q) <= TOLERANCE
                      );
                  });
        if (match) {
            renamedTp.add(match);
            out.renames[label(n)] = label(match);
        } else out.removed.push(label(n));
    }

    // shared nodes
    for (const [nm, n] of ownBy) {
        const t = tpBy.get(nm);
        if (t === undefined) continue;
        const name = label(n);
        const cls = lower(n.get("Class"), names);
        if (cls !== lower(t.get("Class"), names)) {
            out.leftOut.push(`${name}: class ${cls} -> ${lower(t.get("Class"), names)}`);
            continue;
        }
        const pa = position(n)!;
        const pb = position(t);
        const move: Move = {};
        if (pb !== null && maxDiff(pa, pb) > TOLERANCE) move.pos = [...pb];
        if (maxDiff(anglesOf(n), anglesOf(t)) > ANGLE_TOLERANCE) move.angles = anglesOf(t);
        if (move.pos || move.angles) out.moves[name] = move;
        if (!SHAPE_CLASSES.has(cls)) continue;
        const fa = flagSet(n, names);
        const fb = flagSet(t, names);
        const flag: FlagEdit = {};
        for (const [f, written] of PRESENCE_FLAGS) {
            if (fb.has(f) && !fa.has(f)) flag.add = [...(flag.add ?? []), written];
            if (fa.has(f) && !fb.has(f)) flag.remove = [...(flag.remove ?? []), written];
        }
        if (flag.add || flag.remove) out.flags[name] = flag;
        const cluster = (s: Struct): string | null =>
            s.get("Cluster") instanceof Name ? text(s.get("Cluster"), names) : null;
        if (
            fa.has("trickobject") !== fb.has("trickobject") ||
            cluster(n)?.toLowerCase() !== cluster(t)?.toLowerCase()
        ) {
            out.tricks[name] = { trick_object: fb.has("trickobject"), cluster: cluster(t) };
        }
        if (cls === "railnode") {
            const ta = `${lower(n.get("Type"), names)}/${lower(n.get("TerrainType"), names)}`;
            const tb = `${lower(t.get("Type"), names)}/${lower(t.get("TerrainType"), names)}`;
            if (ta !== tb) {
                out.terrains[name] = {
                    type: t.get("Type") instanceof Name ? text(t.get("Type"), names) : null,
                    terrain: text(t.get("TerrainType"), names),
                };
            }
        }
    }

    // added nodes: node data only (rails, generic restarts, level lights) - the rest is listed as left out
    for (const t of added) {
        if (renamedTp.has(t)) continue;
        const name = label(t);
        const cls = lower(t.get("Class"), names);
        const p = position(t)!;
        const node: AddedNode = {
            name,
            class: "RailNode",
            pos: [...p],
            angles: anglesOf(t),
            created_at_start: t.hasFlag("CreatedAtStart"),
        };
        if (cls === "railnode") {
            if (t.get("Type") instanceof Name) node.type = text(t.get("Type"), names);
            node.terrain = text(t.get("TerrainType"), names) || "TERRAIN_METALSMOOTH";
            const links = t.get("Links");
            const targets = (Array.isArray(links) ? links : [])
                .map((i) => (typeof i === "number" ? tpArray[i] : undefined))
                .filter((x): x is Struct => x instanceof Struct && x.get("Name") instanceof Name)
                .map(label);
            if (targets.length) node.links = targets;
        } else if (cls === "restart" && !restartTypes(t, names).some((x) => NET_TYPES.has(x))) {
            node.class = "Restart";
            node.type = t.get("Type") instanceof Name ? text(t.get("Type"), names) : "Generic";
        } else if (cls === "levellight") {
            node.class = "LevelLight";
            const unknown = t.entries
                .filter(([k]) => k !== null && !LIGHT_KEYS.has(lower(k, names)))
                .map(([k]) => lower(k!, names))
                .filter((k) => !["color", "excludegeometry"].includes(k));
            if (unknown.length) throw new TypeError(`${base} ${name}: a light with ${unknown.join(", ")}: not handled`);
            const num = (v: QbValue | undefined): number =>
                v instanceof Float ? v.value : typeof v === "number" ? v : 0;
            const color = t.get("Color");
            const exclude = t.get("ExcludeGeometry");
            node.light = {
                brightness: num(t.get("Brightness")),
                inner_radius: num(t.get("InnerRadius")),
                outer_radius: num(t.get("OuterRadius")),
                color: Array.isArray(color) ? color.map((c) => num(c)) : [255, 255, 255],
                exclude_geometry: (Array.isArray(exclude) ? exclude : []).map((x) => text(x, names)),
                flags: [...flagSet(t, names)]
                    .filter((f) => f !== "createdatstart")
                    .map((f) => {
                        const v = t.entries.find(([k, x]) => k === null && x instanceof Name && lower(x, names) === f);
                        return text(v![1], names);
                    }),
            };
        } else {
            out.leftOut.push(`${name}: added ${cls}${t.get("Type") ? `/${lower(t.get("Type"), names)}` : ""}`);
            continue;
        }
        out.added.push(node);
    }

    // collision: objects whose faces THUGPRO's has fewer of, exactly by the duplicates (same corners) of THPS4's
    const colFile = findPath(folder, `${base}col.dat`);
    const tpCol = findPath(thugpro, "data", "pre", `${key}col.prx`);
    if (colFile !== null && tpCol !== null) {
        const theirs = new Map<number, thug2Col.ColObject>();
        try {
            for (const [n, d] of prx.entries(tpCol, (x) => x.toLowerCase().endsWith(".col.xbx")))
                for (const o of thug2Col.readCol(d, n).objects) theirs.set(o.checksum, o);
        } catch (e) {
            if (!(e instanceof Error) || e.name !== "FormatError") throw e;
            out.leftOut.push(`collision not compared: ${e.message}`);
        }
        const col = thps4.readCol(fs.readFileSync(colFile), colFile);
        for (const o of col.objects) {
            const t = theirs.get(o.checksum);
            if (t === undefined || !o.faces.length || t.faces.length >= o.faces.length) continue;
            if (withoutDuplicates(o).length === t.faces.length) {
                out.duplicates.push(names.get(o.checksum) ?? hex8(o.checksum));
            }
        }
    }
    return out;
}

// --- applying (our converted nodes) -----------------------------------------------------------------------------------

// A face's corners, to a hundredth of a unit, in any order: two faces with the same are duplicates (one surface twice).
function corners(o: { verts: readonly (readonly number[])[] }, f: readonly number[]): string {
    return [f[2]!, f[3]!, f[4]!]
        .map((i) => o.verts[i]!.map((k) => Math.round(k * 100)).join(","))
        .sort()
        .join("|");
}

export function withoutDuplicates<F extends readonly number[]>(o: {
    verts: readonly (readonly number[])[];
    faces: F[];
}): F[] {
    const seen = new Set<string>();
    return o.faces.filter((f) => {
        const k = corners(o, f);
        if (seen.has(k)) return false;
        seen.add(k);
        return true;
    });
}

// TP-28: the listed collision objects' duplicate faces left out (THPS4's collision, before converting).
export function removeDuplicateFaces(col: thps4.ColFile, edits: LevelEdits, base: string, report: string[]): void {
    if (!edits.duplicates.length) return;
    const wanted = new Set(edits.duplicates.map(nameValue));
    let faces = 0;
    let objects = 0;
    for (const o of col.objects) {
        if (!wanted.has(o.checksum)) continue;
        const kept = withoutDuplicates(o);
        faces += o.faces.length - kept.length;
        objects += 1;
        o.faces = kept;
    }
    report.push(`  ${base}: TP-28 - ${faces} duplicate collision face(s) of ${objects} object(s) left out`);
}

const nameValue = (t: string): number => (t.startsWith("#") ? Number.parseInt(t.slice(1), 16) : qb.checksum(t));

function key(names: Names, t: string): Name {
    const value = nameValue(t);
    if (!names.has(value) && !t.startsWith("#")) names.set(value, t);
    return new Name(value);
}

const vector = (values: readonly number[]): Vector => new Vector("vector", [...values]);

function setPlace(node: Struct, pos: readonly number[], names: Names): void {
    // (our nodes keep THPS4's legacy Position - z negated from THUG2's Pos - unless they have a Pos)
    if (node.get("Pos") instanceof Vector) node.set("Pos", vector(pos));
    else {
        key(names, "Position");
        node.set("Position", vector([pos[0]!, pos[1]!, -pos[2]!]));
    }
}

function setFlag(node: Struct, flag: string, on: boolean, names: Names): boolean {
    if (node.hasFlag(flag) === on) return false;
    if (on) node.entries.push([null, key(names, flag)]);
    else node.removeFlag(flag);
    return true;
}

// Everything that can still name a node in our level: other nodes' values (TriggerScript parameters, ...), the
// NeverScript text added to the node array, the compiled scripts brought along.
export interface References {
    text: string;
    scripts: ReadonlyMap<number, Uint8Array>;
}

function referenced(value: number, own: Struct, converted: readonly Struct[], refs: References, names: Names): boolean {
    const walk = (v: QbValue, top: boolean): boolean => {
        if (v instanceof Name) return v.value === value;
        if (Array.isArray(v)) return v.some((x) => walk(x, false));
        if (v instanceof Struct)
            return v.entries.some(
                ([k, x]) => !(top && k !== null && k.value === qb.checksum("Name")) && walk(x, false),
            );
        return false;
    };
    for (const n of converted) if (n !== own && walk(n, false)) return true;
    if (own.entries.some(([k, x]) => !(k !== null && k.value === qb.checksum("Name")) && walk(x, false))) return true;
    const t = names.get(value);
    if (t !== undefined && new RegExp(`(?<![\\w#])${t.replace(/[^A-Za-z0-9_]/g, "\\$&")}(?!\\w)`, "i").test(refs.text))
        return true;
    if (new RegExp(`#${hex8(value).slice(2)}\\b`, "i").test(refs.text)) return true;
    for (const code of refs.scripts.values()) {
        const buf = Buffer.from(code);
        for (let i = 0; i + 5 <= buf.length; i++)
            if (buf[i] === 0x16 && intFromBytesLE(buf.subarray(i + 1, i + 5)) === value) return true;
    }
    return false;
}

export interface Applied {
    category: string;
    register: string;
    applied: number;
    absent: string[]; // named nodes not in our node array (left out by the converter: nothing to change)
    kept: string[]; // (TP-23) renames not applied: something of ours still names the old one
}

// Our converted nodes (in place) made as THUGPRO's: the level's edits. Added nodes go at the end of `converted`
// (before the peds and cars, which cmdConvert appends after it). Returns what was done, per category.
export function applyEdits(
    base: string,
    edits: LevelEdits,
    converted: Struct[],
    names: Names,
    refs: References,
    report: string[],
): Applied[] {
    const byName = new Map<number, Struct>();
    for (const n of converted) {
        const nm = n.get("Name");
        if (nm instanceof Name && !byName.has(nm.value)) byName.set(nm.value, n);
    }
    const results: Applied[] = [];
    const each = <T>(
        category: string,
        register: string,
        items: Record<string, T>,
        fn: (node: Struct, item: T, name: string) => boolean,
    ): Applied => {
        const r: Applied = { category, register, applied: 0, absent: [], kept: [] };
        for (const [name, item] of Object.entries(items)) {
            const node = byName.get(nameValue(name));
            if (node === undefined) r.absent.push(name);
            else if (fn(node, item, name)) r.applied += 1;
        }
        results.push(r);
        return r;
    };
    each("moved / turned nodes", "TP-22", edits.moves, (node, m) => {
        if (m.pos) setPlace(node, m.pos, names);
        if (m.angles) node.set("Angles", vector(m.angles));
        if (m.angles) key(names, "Angles");
        return true;
    });
    each("flags (present at the start / online)", "TP-24", edits.flags, (node, f) => {
        let changed = false;
        for (const flag of f.add ?? []) changed = setFlag(node, flag, true, names) || changed;
        for (const flag of f.remove ?? []) changed = setFlag(node, flag, false, names) || changed;
        return changed || true;
    });
    each("trick objects", "TP-25", edits.tricks, (node, t) => {
        setFlag(node, "TrickObject", t.trick_object, names);
        if (t.cluster === null) node.remove("Cluster");
        else {
            key(names, "Cluster");
            node.set("Cluster", key(names, t.cluster));
        }
        return true;
    });
    each("rail terrains", "TP-26", edits.terrains, (node, t) => {
        key(names, "TerrainType");
        node.set("TerrainType", key(names, t.terrain));
        if (t.type === null) node.remove("Type");
        else {
            key(names, "Type");
            node.set("Type", key(names, t.type));
        }
        return true;
    });
    // TP-23 last of the shared nodes' edits (the ones above name the nodes by THPS4's names)
    const renamesKept: string[] = [];
    const renames = each("renamed nodes", "TP-23", edits.renames, (node, to, from) => {
        if (referenced(nameValue(from), node, converted, refs, names)) {
            renamesKept.push(from);
            return false;
        }
        node.set("Name", key(names, to));
        return true;
    });
    renames.kept = renamesKept;

    // TP-27: removed (the node taken out, the Links renumbered), then added
    const removed: Applied = { category: "removed nodes", register: "TP-27", applied: 0, absent: [], kept: [] };
    const gone = new Set<number>();
    for (const name of edits.removed) {
        const at = converted.findIndex((n) => {
            const nm = n.get("Name");
            return nm instanceof Name && nm.value === nameValue(name);
        });
        if (at < 0) removed.absent.push(name);
        else {
            gone.add(at);
            removed.applied += 1;
        }
    }
    if (gone.size) {
        const index = new Map<number, number>();
        let k = 0;
        converted.forEach((_, i) => {
            if (!gone.has(i)) index.set(i, k++);
        });
        const kept = converted.filter((_, i) => !gone.has(i));
        for (const n of kept) {
            const links = n.get("Links");
            if (!Array.isArray(links)) continue;
            const renumbered = links
                .filter((v): v is number => typeof v === "number" && index.has(v))
                .map((v) => index.get(v)!);
            if (renumbered.length) n.set("Links", renumbered);
            else n.remove("Links");
        }
        converted.splice(0, converted.length, ...kept);
    }
    results.push(removed);

    const added: Applied = { category: "added nodes", register: "TP-27", applied: 0, absent: [], kept: [] };
    const start = converted.length;
    const at = new Map<number, number>();
    edits.added.forEach((a, i) => {
        at.set(nameValue(a.name), start + i);
    });
    const index = new Map<number, number>();
    converted.forEach((n, i) => {
        const nm = n.get("Name");
        if (nm instanceof Name && !index.has(nm.value)) index.set(nm.value, i);
    });
    for (const a of edits.added) {
        if (index.has(nameValue(a.name))) throw new TypeError(`${base}: TP-27 adds ${a.name}, which the level has`);
        const entries: qb.Entry[] = [[key(names, "Position"), vector([a.pos[0]!, a.pos[1]!, -a.pos[2]!])]];
        // (zero Angles left out on rails as thps4_scripts.dropZeroAngles does: THUG2's vector pool)
        if (a.class !== "RailNode" || a.angles.some((v) => v !== 0))
            entries.push([key(names, "Angles"), vector(a.angles)]);
        entries.push([key(names, "Name"), key(names, a.name)], [key(names, "Class"), key(names, a.class)]);
        if (a.type !== undefined) entries.push([key(names, "Type"), key(names, a.type)]);
        if (a.created_at_start) entries.push([null, key(names, "CreatedAtStart")]);
        if (a.class === "Restart") {
            entries.push(
                [key(names, "RestartName"), a.name],
                [key(names, "restart_types"), [key(names, a.type ?? "Generic")]],
            );
        }
        if (a.terrain !== undefined) entries.push([key(names, "TerrainType"), key(names, a.terrain)]);
        if (a.light) {
            const l = a.light;
            entries.push([key(names, "ExcludeGeometry"), l.exclude_geometry.map((x) => key(names, x))]);
            for (const f of l.flags) entries.push([null, key(names, f)]);
            entries.push(
                [key(names, "Brightness"), l.brightness],
                [key(names, "InnerRadius"), new Float(l.inner_radius)],
                [key(names, "OuterRadius"), new Float(l.outer_radius)],
                [key(names, "Color"), [...l.color]],
            );
        }
        const links = (a.links ?? []).map((l) => at.get(nameValue(l)) ?? index.get(nameValue(l)));
        if (links.some((l) => l === undefined))
            throw new TypeError(`${base}: TP-27 ${a.name} links a node we don't have`);
        if (links.length) entries.push([key(names, "Links"), links as number[]]);
        converted.push(new Struct(entries));
        added.applied += 1;
    }
    results.push(added);

    const done = results.filter((r) => r.applied || r.kept.length);
    if (done.length) {
        report.push(
            `  ${base}: THUGPRO's edits - ${done.map((r) => `${r.register} ${r.category} ${r.applied}${r.kept.length ? ` (${r.kept.length} kept: still named)` : ""}`).join(", ")}`,
        );
    }
    return results;
}

// --- checking (a converted node array against the profile) ------------------------------------------------------------

export interface Check {
    category: string;
    register: string;
    total: number;
    matching: number;
    absent: string[];
    differing: string[];
}

const close = (a: readonly number[], b: readonly number[], tol: number): boolean => maxDiff(a, b) <= tol;

// How many of the level's edits `array` (a converted node array, as read back from its .qb) agrees with.
export function checkEdits(edits: LevelEdits, array: readonly QbValue[]): Check[] {
    const nodes = array.filter((n): n is Struct => n instanceof Struct);
    const by = new Map<number, Struct>();
    for (const n of nodes) {
        const nm = n.get("Name");
        if (nm instanceof Name && !by.has(nm.value)) by.set(nm.value, n);
    }
    const out: Check[] = [];
    const run = <T>(
        category: string,
        register: string,
        items: [string, T][],
        ok: (n: Struct, item: T) => boolean,
    ): void => {
        const c: Check = { category, register, total: items.length, matching: 0, absent: [], differing: [] };
        for (const [name, item] of items) {
            const n = by.get(nameValue(name));
            if (n === undefined) c.absent.push(name);
            else if (ok(n, item)) c.matching += 1;
            else c.differing.push(name);
        }
        out.push(c);
    };
    const isName = (v: QbValue | undefined, t: string | null): boolean =>
        t === null ? v === undefined : v instanceof Name && v.value === nameValue(t);
    run("moved / turned nodes", "TP-22", Object.entries(edits.moves), (n, m) => {
        const p = position(n);
        return (
            (!m.pos || (p !== null && close(p, m.pos, TOLERANCE))) &&
            (!m.angles || close(anglesOf(n), m.angles, ANGLE_TOLERANCE))
        );
    });
    // (renamed: the new name there and the old one gone)
    run(
        "renamed nodes",
        "TP-23",
        Object.entries(edits.renames).map(([from, to]) => [to, from] as [string, string]),
        (_n, from) => !by.has(nameValue(from)),
    );
    run(
        "flags (present at the start / online)",
        "TP-24",
        Object.entries(edits.flags),
        (n, f) => (f.add ?? []).every((x) => n.hasFlag(x)) && (f.remove ?? []).every((x) => !n.hasFlag(x)),
    );
    run(
        "trick objects",
        "TP-25",
        Object.entries(edits.tricks),
        (n, t) => n.hasFlag("TrickObject") === t.trick_object && isName(n.get("Cluster"), t.cluster),
    );
    run(
        "rail terrains",
        "TP-26",
        Object.entries(edits.terrains),
        (n, t) => isName(n.get("TerrainType"), t.terrain) && isName(n.get("Type"), t.type),
    );
    // removed: matching = not there
    const rem: Check = {
        category: "removed nodes",
        register: "TP-27",
        total: edits.removed.length,
        matching: 0,
        absent: [],
        differing: [],
    };
    for (const name of edits.removed) {
        if (by.has(nameValue(name))) rem.differing.push(name);
        else rem.matching += 1;
    }
    out.push(rem);
    run(
        "added nodes",
        "TP-27",
        edits.added.map((a) => [a.name, a] as [string, AddedNode]),
        (n, a) => {
            const p = position(n);
            return p !== null && close(p, a.pos, TOLERANCE) && isName(n.get("Class"), a.class);
        },
    );
    return out;
}

// TP-28's check: the listed objects' collidable face counts in our converted collision against THUGPRO's.
export function checkDuplicates(edits: LevelEdits, ours: thug2Col.ColFile, theirs: readonly thug2Col.ColFile[]): Check {
    const c: Check = {
        category: "collision duplicates",
        register: "TP-28",
        total: edits.duplicates.length,
        matching: 0,
        absent: [],
        differing: [],
    };
    const index = (files: readonly thug2Col.ColFile[]): Map<number, thug2Col.ColObject> => {
        const m = new Map<number, thug2Col.ColObject>();
        for (const f of files) for (const o of f.objects) m.set(o.checksum, o);
        return m;
    };
    const a = index([ours]);
    const b = index(theirs);
    for (const name of edits.duplicates) {
        const o = a.get(nameValue(name));
        const t = b.get(nameValue(name));
        if (o === undefined || t === undefined) c.absent.push(name);
        else if (o.faces.length === t.faces.length) c.matching += 1;
        else c.differing.push(`${name} (${o.faces.length} / ${t.faces.length})`);
    }
    return c;
}
