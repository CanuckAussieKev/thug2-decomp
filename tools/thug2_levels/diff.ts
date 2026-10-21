// NOT ORIGINAL - THUGPRO's versions of THUG2's own levels against THUG2's (the user's decision 2026-10-21: "For THUG2
// node data, compare"): every difference found in THUGPRO's install, classified and grouped into register entries
// (tools/compat/thugpro/thug2_levels.json, README.md there: TP-42). What is compared, per level (sources.ts):
//   - the node array THUGPRO loads instead of THUG2's (TR, BO, BA, AU, LA): node by node by name - flags
//     (CreatedAtStart, AbsentInNetGames, TrickObject...), values (TriggerScript, Cluster, Pos...), Links by name,
//     nodes THUGPRO added (rails, a quarter pipe);
//   - <L>_LevelChanges (the level's <l>_thugpro.qb, thugpro_dynamic.qb), which THUGPRO's scripts apply after loading:
//     trigger scripts replaced (kill zones), nodes killed, banner models;
//   - <L>_option_items: THUGPRO's host options for the level (their toggle scripts, the state they're put in at load);
//   - the level structure (setup_script...) and a replaced level script file (LA): scripts THUGPRO changed;
//   - the collision THUGPRO loads instead of THUG2's (AP, TR, DJ, SC): faces taken out, face flags / terrain changed,
//     objects added / taken out; and TR's scene (an added quarter pipe, re-lit sectors).
// Each entry's class: "parity" (online play with THUGPRO players: spawns, restarts in net games, crowns, CTF, rails,
// kill zones, collision players touch, graffiti objects), "visible" (seen differently but nothing to stand on: gaps,
// vehicles, loose objects), "cosmetic" (looks / sound / physics of props only) or "single_player" (only outside
// network games). Values that differ only by float rounding (THUGPRO's compiler) aren't differences (TOLERANCE).

import { crc32 } from "node:zlib";
import * as qb from "../level_port/qb.ts";
import { Name, type QbValue, Struct, Vector } from "../level_port/qb.ts";
import * as thug2Col from "../level_port/thug2_col.ts";
import * as thug2Scene from "../level_port/thug2_scene.ts";
import { Float } from "../lib/py.ts";
import { nodeArray } from "../thaw_levels/thugpro_compare.ts";
import { encode, type Json, type LevelEdits } from "../thug1_levels/hand_edits.ts";
import {
    type LevelFiles,
    nameKey,
    namesOf,
    type ScriptItem,
    scriptShape,
    scriptsOf,
    stringOf,
    usedNames,
} from "./sources.ts";

export const TOLERANCE = 0.01; // units: THUGPRO's compiler rounds floats otherwise than Neversoft's

export type Klass = "parity" | "visible" | "cosmetic" | "single_player" | "none";
export const KLASSES: readonly Klass[] = ["parity", "visible", "single_player", "cosmetic", "none"];

// A script THUGPRO has and THUG2 hasn't, brought in by reference: its name, where it is in THUGPRO's install and a
// CRC-32 of its bytes (to check it is still the one compared).
export interface ScriptRef {
    name: string;
    from: string;
    crc: number;
}

// A collision object / scene sector of THUGPRO's brought in by reference (TR's added quarter pipe).
export interface GeometryRef {
    name: string;
    collision?: { from: string; crc: number; faces: number };
    sector?: { from: string; crc: number; materials: number[] };
}

export interface Entry {
    id: string;
    level: string;
    kind: string; // node_presence, node_values, nodes_added, trigger_scripts, killed, collision_faces, ...
    what: string; // spawns, rails, collision, kill_zones, ...
    class: Klass;
    enabled: boolean; // off by default: the user decides (applied by thugpro_apply.ts when on)
    applicable: boolean; // whether thugpro_apply.ts can apply it
    description: string;
    nodes?: string[]; // the nodes / objects it concerns (all of them)
    edits?: LevelEdits; // node / collision edits (tools/thug1_levels/hand_edits.ts's form)
    scripts?: ScriptRef[]; // THUGPRO's scripts it needs (by reference)
    replace_scripts?: ScriptRef[]; // scripts of THUG2's level THUGPRO's file replaces (by reference)
    geometry?: GeometryRef[];
    level_setup?: { setup_script?: string; options?: { script: string; params: string[] }[] };
    note?: string;
}

// --- helpers -----------------------------------------------------------------------------------------------------

const lower = (s: string): string => s.toLowerCase();

function nameText(value: number, names: ReadonlyMap<number, string>): string {
    const t = names.get(value);
    return t !== undefined && qb.checksum(t) === value ? t : `#${(value >>> 0).toString(16).padStart(8, "0")}`;
}

function nodeName(n: QbValue): number | null {
    return n instanceof Struct ? nameKey(n, "Name") : null;
}

function flagsOf(n: Struct): Set<number> {
    const out = new Set<number>();
    for (const [k, v] of n.entries) if (k === null && v instanceof Name) out.add(v.value);
    return out;
}

// Values equal but for float rounding.
export function sameValue(a: QbValue | undefined, b: QbValue | undefined): boolean {
    if (a === undefined || b === undefined) return a === b;
    const num = (v: QbValue): number | null => (v instanceof Float ? v.value : typeof v === "number" ? v : null);
    const na = num(a);
    const nb = num(b);
    if (na !== null && nb !== null) return Math.abs(na - nb) < TOLERANCE;
    if (a instanceof Name && b instanceof Name) return a.value === b.value;
    if (typeof a === "string" && typeof b === "string") return a === b;
    if (a instanceof Vector && b instanceof Vector)
        return (
            a.kind === b.kind &&
            a.values.length === b.values.length &&
            a.values.every((x, i) => Math.abs(x - b.values[i]!) < TOLERANCE)
        );
    if (Array.isArray(a) && Array.isArray(b)) return a.length === b.length && a.every((x, i) => sameValue(x, b[i]));
    if (a instanceof Struct && b instanceof Struct)
        return (
            a.entries.length === b.entries.length &&
            a.entries.every(
                ([k, v], i) =>
                    (k?.value ?? null) === (b.entries[i]![0]?.value ?? null) && sameValue(v, b.entries[i]![1]),
            )
        );
    return false;
}

// --- what a node is ------------------------------------------------------------------------------------------------

const NET_TYPES = new Set(["multiplayer", "horse", "ctf", "ctf_1", "ctf_2", "team", "king"].map(qb.checksum));
const CTF_NAMES = new Set(
    ["Red", "Blue", "Green", "Yellow"]
        .flatMap((c) => [`TRG_CTF_${c}`, `TRG_CTF_${c}_Base`, `TRG_Flag_${c}`, `TRG_CTF_Restart_${c}`])
        .map((n) => qb.checksum(n)),
);

export interface Context {
    names: Map<number, string>;
    colFaces: Map<number, number>; // THUG2's collision objects (checksum -> faces)
    scripts: Map<number, ScriptItem>; // every script (THUG2's level and global, THUGPRO's)
    thug2Scripts: Set<number>; // the scripts THUG2 defines
}

const traitsCache = new WeakMap<Context, Map<number, { kill: boolean; gap: boolean }>>();

// Whether a script (or what it runs, 4 deep) kills the skater / starts or ends a gap.
export function scriptTraits(ctx: Context, script: number): { kill: boolean; gap: boolean } {
    let cache = traitsCache.get(ctx);
    if (!cache) {
        cache = new Map();
        traitsCache.set(ctx, cache);
    }
    const hit = cache.get(script);
    if (hit) return hit;
    const out = { kill: false, gap: false };
    const seen = new Set<number>();
    const walk = (s: number, depth: number): void => {
        if (seen.has(s) || depth > 4) return;
        seen.add(s);
        const t = lower(ctx.names.get(s) ?? "");
        if (t.includes("killskater") || t === "sk3_killskater") out.kill = true;
        if (t === "startgap" || t === "endgap" || t.startsWith("gap_gen")) out.gap = true;
        const item = ctx.scripts.get(s);
        if (item) for (const u of usedNames(item.bytes)) walk(u, depth + 1);
    };
    walk(script, 0);
    cache.set(script, out);
    return out;
}

export function whatNode(n: Struct, ctx: Context): string {
    const cls = lower(nameText(nameKey(n, "Class") ?? 0, ctx.names));
    const type = lower(nameText(nameKey(n, "Type") ?? 0, ctx.names));
    const name = nameKey(n, "Name") ?? 0;
    const text = lower(ctx.names.get(name) ?? "");
    if (cls === "restart") {
        const types = n.get("restart_types");
        return Array.isArray(types) && types.some((t) => t instanceof Name && NET_TYPES.has(t.value))
            ? "spawns"
            : "restarts";
    }
    if (type === "crown") return "koth";
    if (CTF_NAMES.has(name)) return "ctf";
    if (cls === "railnode" || cls === "climbingnode") return "rails";
    const trigger = nameKey(n, "TriggerScript");
    if (trigger !== null) {
        const t = scriptTraits(ctx, trigger);
        if (t.kill) return "kill_zones";
    }
    if (cls === "levelgeometry" || cls === "levelobject")
        return (ctx.colFaces.get(name) ?? 0) > 0 ? "collision" : "scenery";
    if (cls === "pedestrian") return "peds";
    if (cls === "vehicle") return "vehicles";
    if (cls === "particleobject" || text.startsWith("particlebox") || text.includes("_fire") || text.includes("smoke"))
        return "effects";
    if (text.includes("sfx") || text.includes("sound")) return "sounds";
    if (cls === "levellight" || text.includes("light") || text.includes("glow")) return "lights";
    if (cls === "waypoint") return "paths";
    if (cls === "gameobject") {
        const model = stringOf(n, "model") ?? "";
        if (lower(model).startsWith("veh\\") || text.includes("car")) return "vehicles";
        return "objects";
    }
    if (trigger !== null) return scriptTraits(ctx, trigger).gap ? "gaps" : "triggers";
    return "other";
}

const PARITY_WHATS = new Set(["spawns", "koth", "ctf", "rails", "collision", "kill_zones", "graffiti"]);
const VISIBLE_WHATS = new Set(["gaps", "vehicles", "objects", "triggers"]);

// The class of a change to `what` - net: only network games see it (AbsentInNetGames); single: only outside them.
export function klassOf(what: string, scope: "all" | "net" | "single" = "all"): Klass {
    if (scope === "single") return "single_player";
    if (PARITY_WHATS.has(what)) return "parity";
    if (what === "restarts") return scope === "net" ? "none" : "single_player";
    if (VISIBLE_WHATS.has(what)) return "visible";
    return "cosmetic";
}

// --- node arrays ---------------------------------------------------------------------------------------------------

interface Change {
    node: string;
    kind: string;
    what: string;
    klass: Klass;
    detail: string;
    edit: LevelEdits;
}

const FLAG_ABSENT = qb.checksum("AbsentInNetGames");
const FLAG_CREATED = qb.checksum("CreatedAtStart");
const KEY_LINKS = qb.checksum("Links");
const KEY_NAME = qb.checksum("Name");
const COSMETIC_KEYS = new Set(
    ["contacts", "SuspendDistance", "lod_dist1", "lod_dist2", "model", "Profile", "SkeletonName", "AnimName"].map((k) =>
        qb.checksum(k),
    ),
);

function keysOf(n: Struct): Map<number, QbValue> {
    const out = new Map<number, QbValue>();
    for (const [k, v] of n.entries) if (k !== null) out.set(k.value, v);
    return out;
}

function linkNames(v: QbValue | undefined, array: readonly QbValue[], names: ReadonlyMap<number, string>): string[] {
    if (!Array.isArray(v)) return [];
    return v.map((i) => {
        const n = typeof i === "number" ? array[i] : undefined;
        const k = n === undefined ? null : nodeName(n);
        return k === null ? `#index${String(i)}` : nameText(k, names);
    });
}

// The node array's differences, node by node (matched by name).
export function nodeChanges(ours: readonly QbValue[], theirs: readonly QbValue[], ctx: Context): Change[] {
    const out: Change[] = [];
    const nm = ctx.names;
    const byName = new Map<number, Struct>();
    for (const n of theirs) {
        const k = nodeName(n);
        if (k !== null && n instanceof Struct && !byName.has(k)) byName.set(k, n);
    }
    const ourNames = new Set<number>();
    for (const a of ours) {
        const k = nodeName(a);
        if (k === null || !(a instanceof Struct)) continue;
        ourNames.add(k);
        const name = nameText(k, nm);
        const b = byName.get(k);
        const what = whatNode(a, ctx);
        if (b === undefined) {
            out.push({
                node: name,
                kind: "nodes_removed",
                what,
                klass: klassOf(what),
                detail: "not in THUGPRO's array",
                edit: { created_at_start: { off: [name] }, net_presence: { absent: [name] } },
            });
            continue;
        }
        const fa = flagsOf(a);
        const fb = flagsOf(b);
        for (const f of new Set([...fa, ...fb])) {
            if (fa.has(f) === fb.has(f)) continue;
            const on = fb.has(f);
            const flag = nameText(f, nm);
            if (f === FLAG_ABSENT) {
                out.push({
                    node: name,
                    kind: on ? "net_absent" : "net_present",
                    what,
                    klass: klassOf(what, "net"),
                    detail: `AbsentInNetGames ${on ? "added" : "taken off"}`,
                    edit: { net_presence: on ? { absent: [name] } : { present: [name] } },
                });
            } else if (f === FLAG_CREATED) {
                out.push({
                    node: name,
                    kind: on ? "created_on" : "created_off",
                    what,
                    klass: klassOf(what),
                    detail: `CreatedAtStart ${on ? "added" : "taken off"}`,
                    edit: { created_at_start: on ? { on: [name] } : { off: [name] } },
                });
            } else {
                const w = lower(flag) === "trickobject" ? "graffiti" : what;
                out.push({
                    node: name,
                    kind: "node_values",
                    what: w,
                    klass: lower(flag) === "trickobject" ? "parity" : klassOf(w) === "parity" ? "parity" : "cosmetic",
                    detail: `${flag} ${on ? "added" : "taken off"}`,
                    edit: { node_values: { [name]: { [`${on ? "+" : "-"}${flag}`]: true } } },
                });
            }
        }
        const ka = keysOf(a);
        const kb = keysOf(b);
        for (const key of new Set([...ka.keys(), ...kb.keys()])) {
            if (key === KEY_NAME) continue;
            const va = ka.get(key);
            const vb = kb.get(key);
            const keyText = nameText(key, nm);
            if (key === KEY_LINKS) {
                const la = linkNames(va, ours, nm);
                const lb = linkNames(vb, theirs, nm);
                if (la.join(",") === lb.join(",")) continue;
                out.push({
                    node: name,
                    kind: "node_values",
                    what,
                    klass: klassOf(what),
                    detail: `Links [${la.join(", ")}] -> [${lb.join(", ")}]`,
                    edit: { rails: { links: { [name]: lb } } },
                });
                continue;
            }
            if (sameValue(va, vb)) continue;
            // (a model path that differs only in case is the same file)
            if (typeof va === "string" && typeof vb === "string" && lower(va) === lower(vb)) continue;
            let w = what;
            let klass = klassOf(what);
            if (lower(keyText) === "cluster") {
                w = "graffiti";
                klass = "parity";
            } else if (lower(keyText) === "triggerscript") {
                const traits = [va, vb].map((v) =>
                    v instanceof Name ? scriptTraits(ctx, v.value) : { kill: false, gap: false },
                );
                if (traits.some((t) => t.kill)) {
                    w = "kill_zones";
                    klass = "parity";
                } else if (traits.some((t) => t.gap)) {
                    w = "gaps";
                    klass = "visible";
                }
            } else if (COSMETIC_KEYS.has(key)) {
                w = lower(keyText) === "contacts" ? "physics" : what;
                klass = "cosmetic";
            }
            const show = (v: QbValue | undefined): string =>
                v === undefined ? "(none)" : qb.toNs(v, nm).replace(/\s+/g, " ").slice(0, 80);
            out.push({
                node: name,
                kind: "node_values",
                what: w,
                klass,
                detail: `${keyText}: ${show(va)} -> ${show(vb)}`,
                edit: { node_values: { [name]: { [keyText]: vb === undefined ? null : encode(vb, nm) } } },
            });
        }
    }
    for (const b of theirs) {
        const k = nodeName(b);
        if (k === null || !(b instanceof Struct) || ourNames.has(k)) continue;
        const name = nameText(k, nm);
        const what = whatNode(b, ctx);
        const fields: Record<string, Json> = {};
        const flags: string[] = [];
        for (const [key, v] of b.entries) {
            if (key === null) {
                if (v instanceof Name) flags.push(nameText(v.value, nm));
                continue;
            }
            if (key.value === KEY_LINKS) fields.Links = linkNames(v, theirs, nm);
            else fields[nameText(key.value, nm)] = encode(v, nm);
        }
        if (flags.length) fields.flags = flags;
        out.push({
            node: name,
            kind: "nodes_added",
            what,
            klass: klassOf(what, b.hasFlag("AbsentInNetGames") ? "single" : "all"),
            detail: `added (${nameText(nameKey(b, "Class") ?? 0, nm)})`,
            edit: { rails: { added: [fields] } },
        });
    }
    return out;
}

// --- collision -----------------------------------------------------------------------------------------------------

export interface ColChange {
    object: string;
    kind: "faces_removed" | "face_flags" | "terrain" | "reshaped" | "faces_added" | "object_removed" | "object_added";
    detail: string;
    count: number;
    solid?: number; // faces_removed: how many of them are solid (not trigger / non-collidable)
    edit?: LevelEdits;
}

const BITS: [number, string][] = [
    [thug2Col.FACE_SKATABLE, "skatable"],
    [thug2Col.FACE_NOT_SKATABLE, "not skatable"],
    [thug2Col.FACE_WALL_RIDABLE, "wallride"],
    [thug2Col.FACE_VERT, "vert"],
    [thug2Col.FACE_NON_COLLIDABLE, "non-collidable"],
    [thug2Col.FACE_DECAL, "decal"],
    [thug2Col.FACE_TRIGGER, "trigger"],
    [thug2Col.FACE_CAMERA_PASSES, "camera passes"],
    [thug2Col.FACE_NO_SKATER_SHADOW, "no shadow"],
    [thug2Col.FACE_SKATER_SHADOW, "shadow"],
    [thug2Col.FACE_NO_SKATER_SHADOW_WALL, "no wall shadow"],
    [thug2Col.FACE_UNDER_OK, "under ok"],
    [thug2Col.FACE_INVISIBLE, "invisible"],
];

export function bitText(bits: number): string {
    const parts = BITS.filter(([b]) => bits & b).map(([, t]) => t);
    const rest = bits & ~BITS.reduce((s, [b]) => s | b, 0);
    if (rest) parts.push(`0x${rest.toString(16)}`);
    return parts.join("+") || "0";
}

export function colCrc(o: thug2Col.ColObject): number {
    const one = thug2Col.writeCol({ objects: [o] });
    return crc32(one) >>> 0;
}

// The collision's differences, object by object (checksum), faces matched by their corners (THUGPRO's files keep
// THUG2's vertices and the order of the faces it kept).
export function colChanges(
    ours: thug2Col.ColFile,
    theirs: thug2Col.ColFile,
    names: ReadonlyMap<number, string>,
): ColChange[] {
    const out: ColChange[] = [];
    const b = new Map(theirs.objects.map((o) => [o.checksum, o]));
    const ourKeys = new Set<number>();
    for (const o of ours.objects) {
        ourKeys.add(o.checksum);
        const name = nameText(o.checksum, names);
        const t = b.get(o.checksum);
        if (t === undefined) {
            out.push({
                object: name,
                kind: "object_removed",
                detail: `${o.faces.length} faces`,
                count: o.faces.length,
                ...(o.faces.length
                    ? { edit: { collision_removed: { [name]: { faces: o.faces.length, remove: "all" as const } } } }
                    : {}),
            });
            continue;
        }
        if (o.verts.length !== t.verts.length) {
            out.push({
                object: name,
                kind: "reshaped",
                detail: `vertices ${o.verts.length} -> ${t.verts.length}`,
                count: 1,
            });
            continue;
        }
        const moved: [number, number, number, number][] = [];
        o.verts.forEach((v, i) => {
            const w = t.verts[i]!;
            if (v.some((x, k) => Math.abs(x - w[k]!) >= TOLERANCE)) moved.push([i, w[0]!, w[1]!, w[2]!]);
        });
        // faces: THUGPRO's matched to ours by their corners, in order
        const key = (f: thug2Col.Face): string => `${f[2]},${f[3]},${f[4]}`;
        const pool = new Map<string, number[]>();
        t.faces.forEach((f, j) => {
            pool.set(key(f), [...(pool.get(key(f)) ?? []), j]);
        });
        const removed: number[] = [];
        const set: Record<string, number[]> = {};
        const clear: Record<string, number[]> = {};
        const terrain: [number, number][] = [];
        const used = new Set<number>();
        o.faces.forEach((f, i) => {
            const list = pool.get(key(f));
            const j = list?.shift();
            if (j === undefined) {
                removed.push(i);
                return;
            }
            used.add(j);
            const g = t.faces[j]!;
            const on = g[0] & ~f[0];
            const off = f[0] & ~g[0];
            for (let bit = 1; bit <= 0x8000; bit <<= 1) {
                const hex = `0x${bit.toString(16).padStart(4, "0")}`;
                if (on & bit) set[hex] = [...(set[hex] ?? []), i];
                if (off & bit) clear[hex] = [...(clear[hex] ?? []), i];
            }
            if (g[1] !== f[1]) terrain.push([i, g[1]]);
        });
        const added = t.faces.length - used.size;
        const faces = o.faces.length;
        if (removed.length) {
            // (a face taken out whose corners a face THUGPRO kept has, in another order: the back of a two-sided
            // surface)
            const corners = (f: thug2Col.Face): string => [f[2], f[3], f[4]].sort((x, y) => x - y).join(",");
            const kept = new Set(o.faces.filter((_, i) => !removed.includes(i)).map(corners));
            const backs = removed.filter((i) => kept.has(corners(o.faces[i]!))).length;
            const backText = backs ? `, ${backs === removed.length ? "all" : backs} backs of two-sided faces` : "";
            // (trigger / non-collidable faces: nothing a skater stands on or hits - gap and event triggers)
            const solid = removed.filter(
                (i) => !(o.faces[i]![0] & (thug2Col.FACE_NON_COLLIDABLE | thug2Col.FACE_TRIGGER)),
            ).length;
            out.push({
                object: name,
                kind: "faces_removed",
                detail: `${removed.length} of ${faces} faces taken out${backText}${solid ? "" : ", trigger faces only"}`,
                count: removed.length,
                solid,
                edit: { collision_removed: { [name]: { faces, remove: removed } } },
            });
        }
        const flagCount = new Set([...Object.values(set).flat(), ...Object.values(clear).flat()]).size;
        if (flagCount) {
            const describe = (m: Record<string, number[]>): string =>
                Object.entries(m)
                    .map(([bit, l]) => `${bitText(Number(bit))} ${l.length}`)
                    .join(", ");
            out.push({
                object: name,
                kind: "face_flags",
                detail: [
                    Object.keys(set).length ? `+ ${describe(set)}` : "",
                    Object.keys(clear).length ? `- ${describe(clear)}` : "",
                ]
                    .filter(Boolean)
                    .join("; "),
                count: flagCount,
                edit: {
                    face_values: {
                        [name]: {
                            faces,
                            ...(Object.keys(set).length ? { set } : {}),
                            ...(Object.keys(clear).length ? { clear } : {}),
                        },
                    },
                },
            });
        }
        if (terrain.length) {
            out.push({
                object: name,
                kind: "terrain",
                detail: `terrain on ${terrain.length} faces`,
                count: terrain.length,
                edit: { face_values: { [name]: { faces, terrain } } },
            });
        }
        if (moved.length) {
            out.push({
                object: name,
                kind: "reshaped",
                detail: `${moved.length} vertices moved`,
                count: moved.length,
                edit: { collision_reshaped: { [name]: { faces, verts: moved } } },
            });
        }
        if (added > 0)
            out.push({ object: name, kind: "faces_added", detail: `${added} faces THUG2 hasn't`, count: added });
    }
    for (const t of theirs.objects) {
        if (ourKeys.has(t.checksum)) continue;
        out.push({
            object: nameText(t.checksum, names),
            kind: "object_added",
            detail: `${t.faces.length} faces`,
            count: t.faces.length,
        });
    }
    return out;
}

// --- scene ---------------------------------------------------------------------------------------------------------

export interface ScnChanges {
    added: thug2Scene.Sector[];
    removed: number[];
    changed: number[];
}

function sectorKey(s: thug2Scene.Sector): string {
    return s.meshes
        .map((m) => `${m.material}:${m.vertexCount}:${crc32(Buffer.concat(m.buffers.map(([, b]) => b)))}`)
        .join("|");
}

export function scnChanges(ours: thug2Scene.Scene, theirs: thug2Scene.Scene): ScnChanges {
    const a = new Map(ours.sectors.map((s) => [s.checksum, s]));
    const b = new Map(theirs.sectors.map((s) => [s.checksum, s]));
    const out: ScnChanges = { added: [], removed: [], changed: [] };
    for (const [k, s] of a) {
        const t = b.get(k);
        if (t === undefined) out.removed.push(k);
        else if (sectorKey(s) !== sectorKey(t) || s.flags !== t.flags) out.changed.push(k);
    }
    for (const [k, t] of b) if (!a.has(k)) out.added.push(t);
    return out;
}

export function sectorCrc(scene: thug2Scene.Scene, s: thug2Scene.Sector): number {
    return crc32(thug2Scene.writeScene({ ...scene, materials: [], sectors: [s] })) >>> 0;
}

// --- level changes (THUGPRO's <L>_LevelChanges) --------------------------------------------------------------------

export interface LevelChange {
    action: string; // replace_script, remove_script, kill, model, ...
    nodes: string[];
    script?: string;
    detail: string;
    source: string;
}

const ACTIONS = [
    "replace_script",
    "remove_script",
    "kill",
    "create",
    "moveto",
    "MoveNode",
    "ProximObject",
    "SoundEmitter",
    "RunScript",
    "UVWibble",
    "InitModelFromProfile",
    "ammo_pickup",
    "replace_test",
    "static",
];

export function levelChanges(
    items: readonly QbValue[],
    names: ReadonlyMap<number, string>,
    source: string,
): LevelChange[] {
    const out: LevelChange[] = [];
    for (const it of items) {
        if (!(it instanceof Struct)) continue;
        const action = ACTIONS.find((a) => it.hasFlag(a)) ?? (stringOf(it, "model") !== null ? "model" : "other");
        const nodes: string[] = [];
        const one = it.get("node");
        if (one instanceof Name) nodes.push(nameText(one.value, names));
        const many = it.get("nodes");
        if (Array.isArray(many)) for (const n of many) if (n instanceof Name) nodes.push(nameText(n.value, names));
        const ts = it.get("triggerscript") ?? it.get("TriggerScript");
        const model = stringOf(it, "model");
        const entry: LevelChange = {
            action,
            nodes,
            detail: qb.toNs(it, names).replace(/\s+/g, " ").slice(0, 200),
            source,
        };
        if (ts instanceof Name) entry.script = nameText(ts.value, names);
        if (model !== null && action !== "model") entry.detail = `${action} ${model}`;
        out.push(entry);
    }
    return out;
}

// --- the level as a whole -----------------------------------------------------------------------------------------

export interface LevelReport {
    code: string;
    sources: Record<string, string>;
    nodes: { ours: number; theirs: number; changes: Change[] } | null;
    collision: { ours: number; theirs: number; changes: ColChange[] } | null;
    scene: (ScnChanges & { ours: number; theirs: number }) | null;
    levelChanges: LevelChange[];
    options: { desc: string; flag: string; script: string; flags: string[]; dflt: "on" | "off" }[];
    structure: { key: string; ours: string; theirs: string }[];
    scripts: { name: string; how: "changed" | "added" | "removed"; kill: boolean; gap: boolean }[];
    ctx: Context;
    tpScripts: Map<number, ScriptItem>; // THUGPRO's level's own scripts (its extras and replaced level scripts)
}

// Keys of the level structure that only say where THUGPRO loads its files from (not a difference in the level).
const LOADER_KEYS = new Set(
    ["qb", "level_qb", "more_level_qbs", "thugpro_prx", "pre", "scnpre", "colpre", "pedpre", "level_sfx_qb"].map(lower),
);

export function compareLevel(
    files: LevelFiles,
    globals: { thug2: Map<number, ScriptItem>; thugpro: Map<number, ScriptItem>; names: Map<number, string> },
): LevelReport {
    const L = files.code;
    const names = new Map(globals.names);
    const datas = [files.nodes, files.scripts, files.tp.nodes, files.tp.scripts, ...files.tp.extras.map((x) => x[2])];
    namesOf(
        datas.filter((d): d is Buffer => d !== null),
        names,
    );
    // (a level's scripts are in both its files: the node array's .qb has the trigger scripts)
    const thug2Files: [string, string, Buffer][] = [[`levels\\${L}\\${L}.qb`, `${L}_scripts.prx`, files.nodes]];
    if (files.scripts) thug2Files.push([`levels\\${L}\\${L}_scripts.qb`, `${L}_scripts.prx`, files.scripts]);
    const thug2Level = scriptsOf(thug2Files);
    const tpFiles: [string, string, Buffer][] = [];
    if (files.tp.nodes !== null && files.tp.nodesFrom !== null)
        tpFiles.push(splitFrom(files.tp.nodesFrom, files.tp.nodes));
    if (files.tp.scripts !== null && files.tp.scriptsFrom !== null)
        tpFiles.push(splitFrom(files.tp.scriptsFrom, files.tp.scripts));
    tpFiles.push(...files.tp.extras.map(([e, a, d]) => [e, a, d] as [string, string, Buffer]));
    const tpLevel = scriptsOf(tpFiles);
    const all = new Map<number, ScriptItem>([...globals.thugpro, ...tpLevel]);
    for (const [k, v] of [...globals.thug2, ...thug2Level]) if (!all.has(k)) all.set(k, v);
    const col = thug2Col.readCol(files.col, `${L}.col`);
    const ctx: Context = {
        names,
        // (faces a skater touches: solid, or triggers - warps, kill boxes; not the glows' non-collidable quads)
        colFaces: new Map(
            col.objects.map((o) => [
                o.checksum,
                o.faces.filter((f) => !(f[0] & thug2Col.FACE_NON_COLLIDABLE) || f[0] & thug2Col.FACE_TRIGGER).length,
            ]),
        ),
        scripts: all,
        thug2Scripts: new Set([...globals.thug2.keys(), ...thug2Level.keys()]),
    };
    const report: LevelReport = {
        code: L,
        sources: {},
        nodes: null,
        collision: null,
        scene: null,
        levelChanges: [],
        options: [],
        structure: [],
        scripts: [],
        ctx,
        tpScripts: tpLevel,
    };
    if (files.tp.nodes !== null) {
        report.sources.nodes = files.tp.nodesFrom ?? "";
        const [ours] = nodeArray(files.nodes, `${L}.qb`);
        const [theirs] = nodeArray(files.tp.nodes, files.tp.nodesFrom ?? "");
        report.nodes = { ours: ours.length, theirs: theirs.length, changes: nodeChanges(ours, theirs, ctx) };
    }
    if (files.tp.col !== null) {
        report.sources.collision = files.tp.colFrom ?? "";
        const theirs = thug2Col.readCol(files.tp.col, files.tp.colFrom ?? "");
        report.collision = {
            ours: col.objects.length,
            theirs: theirs.objects.length,
            changes: colChanges(col, theirs, names),
        };
    }
    if (files.tp.scn !== null && files.scn !== null) {
        report.sources.scene = files.tp.scnFrom ?? "";
        const a = thug2Scene.readScene(files.scn, `${L}.scn`);
        const b = thug2Scene.readScene(files.tp.scn, files.tp.scnFrom ?? "");
        report.scene = { ...scnChanges(a, b), ours: a.sectors.length, theirs: b.sectors.length };
    }
    // LevelChanges / option items from the level's extra files and thugpro_dynamic.qb
    const changeKey = `${stringOf(files.tp.struct, "level") ?? L}_LevelChanges`;
    const optionKey = `${stringOf(files.tp.struct, "level") ?? L}_option_items`;
    for (const [entry, archive, data] of files.tp.extras) {
        report.sources[`extra ${entry}`] = archive;
        const q = new qb.QbFile(data, entry, true);
        for (const [k, v] of q.globals) {
            if (k.value === qb.checksum(changeKey) && Array.isArray(v))
                report.levelChanges.push(...levelChanges(v, names, `${archive}: ${entry}`));
            if (k.value === qb.checksum(optionKey) && Array.isArray(v)) {
                for (const it of v) {
                    if (!(it instanceof Struct)) continue;
                    const toggle = it.get("toggle_script");
                    const script = toggle instanceof Struct ? nameKey(toggle, "off_script") : null;
                    const flags = ["net_only", "freeskate_only", "inverted", "not_in_ctf_or_koth"].filter((f) =>
                        it.hasFlag(f),
                    );
                    report.options.push({
                        desc: typeof it.get("desc") === "string" ? (it.get("desc") as string) : "",
                        flag: nameText(nameKey(it, "FLAG") ?? 0, names),
                        script: script === null ? "" : nameText(script, names),
                        flags,
                        dflt: "off",
                    });
                }
            }
        }
    }
    if (files.tp.dynamic !== null)
        report.levelChanges.push(
            ...levelChanges(files.tp.dynamic, names, `thugpro_qb.prx: qb\\thugpro\\thugpro_dynamic.qb`),
        );
    // <key>_LEVEL_CHANGE_0n scripts without a menu item (THUGPRO's host options are those scripts by name, as THAW's -
    // tools/thaw_levels/thugpro_extract.ts)
    const levelKey = stringOf(files.tp.struct, "level") ?? L;
    for (let i = 1; i <= 8; i++) {
        const script = qb.checksum(`${levelKey}_LEVEL_CHANGE_0${i}`);
        if (!tpLevel.has(script) || report.options.some((o) => qb.checksum(o.script) === script)) continue;
        report.options.push({
            desc: "(no menu item)",
            flag: `SERVER_LEVEL_CHANGE_0${i}_ON`,
            script: nameText(script, names),
            flags: [],
            dflt: "off",
        });
    }
    // the state THUGPRO puts each option in at load: on when the level's setup sets its flag (as THAW's - see
    // tools/thaw_levels/thugpro_extract.ts), otherwise off
    const setup = nameKey(files.tp.struct, "setup_script");
    const setupNames = setup !== null && all.has(setup) ? usedNames(all.get(setup)!.bytes) : new Set<number>();
    for (const o of report.options) {
        if (setupNames.has(qb.checksum("SetGlobalFlag")) && setupNames.has(qb.checksum(o.flag))) o.dflt = "on";
    }
    // the level structure (keys that aren't where-from)
    const keys = new Map<string, [string, string]>();
    const text = (v: QbValue): string => qb.toNs(v, names).replace(/\s+/g, " ");
    for (const [side, s] of [
        [0, files.struct],
        [1, files.tp.struct],
    ] as const) {
        for (const [k, v] of s.entries) {
            const key = k === null ? `flag ${text(v)}` : nameText(k.value, names);
            if (LOADER_KEYS.has(lower(key)) || lower(key).startsWith("flag thugpro_")) continue;
            const pair = keys.get(lower(key)) ?? ["", ""];
            pair[side] = k === null ? "yes" : text(v);
            keys.set(lower(key), pair);
            if (!keys.has(`name:${lower(key)}`)) keys.set(`name:${lower(key)}`, [key, key]);
        }
    }
    for (const [k, [a, b]] of keys) {
        if (k.startsWith("name:")) continue;
        if (
            a.replace(/-?\d+\.\d+/g, (x) => Number(x).toFixed(2)) ===
            b.replace(/-?\d+\.\d+/g, (x) => Number(x).toFixed(2))
        )
            continue;
        report.structure.push({ key: keys.get(`name:${k}`)![0], ours: a || "(none)", theirs: b || "(none)" });
    }
    // the level's script files THUGPRO replaces (its node array's .qb, its scripts .qb): their scripts against THUG2's
    const pairs: [Buffer | null, Buffer | null, string | null][] = [
        [files.nodes, files.tp.nodes, files.tp.nodesFrom],
        [files.scripts, files.tp.scripts, files.tp.scriptsFrom],
    ];
    for (const [ours, data, from] of pairs) {
        if (data === null || from === null) continue;
        if (from === files.tp.scriptsFrom) report.sources.scripts = from;
        const mine = ours === null ? new Map<number, ScriptItem>() : scriptsOf([["", "", ours]]);
        const theirs = scriptsOf([splitFrom(from, data)]);
        for (const [k, s] of theirs) {
            const o = thug2Level.get(k);
            const t = scriptTraits(ctx, k);
            if (o === undefined) report.scripts.push({ name: nameText(k, names), how: "added", ...t });
            else if (scriptShape(o.bytes) !== scriptShape(s.bytes))
                report.scripts.push({ name: nameText(k, names), how: "changed", ...t });
        }
        for (const k of mine.keys()) {
            if (!theirs.has(k))
                report.scripts.push({ name: nameText(k, names), how: "removed", kill: false, gap: false });
        }
    }
    return report;
}

// "<archive>: <entry>" and the file's bytes as scriptsOf takes them.
function splitFrom(from: string, data: Buffer): [string, string, Buffer] {
    const at = from.indexOf(": ");
    return at < 0 ? [from, "", data] : [from.slice(at + 2), from.slice(0, at), data];
}

// The scripts THUGPRO has and THUG2 hasn't that `roots` need (THUGPRO's own, by reference), in a stable order.
export function scriptClosure(
    roots: readonly number[],
    ctx: Context,
    tpScripts: ReadonlyMap<number, ScriptItem>,
): ScriptRef[] {
    const out = new Map<number, ScriptRef>();
    const stack = [...roots];
    while (stack.length) {
        const s = stack.pop()!;
        if (out.has(s) || ctx.thug2Scripts.has(s)) continue;
        const item = tpScripts.get(s) ?? ctx.scripts.get(s);
        if (item === undefined) continue;
        out.set(s, { name: nameText(s, ctx.names), from: item.from, crc: crc32(item.bytes) >>> 0 });
        for (const u of usedNames(item.bytes)) stack.push(u);
    }
    return [...out.values()].sort((a, b) => a.name.localeCompare(b.name));
}

// --- edits ---------------------------------------------------------------------------------------------------------

function joined<T>(a: readonly T[] | undefined, b: readonly T[] | undefined): T[] | undefined {
    if (!b?.length) return a ? [...a] : undefined;
    return [...(a ?? []), ...b];
}

// The edits of several changes / entries as one (hand_edits.ts's LevelEdits; a node or object in two gets both).
export function mergeEdits(list: readonly LevelEdits[]): LevelEdits {
    const out: LevelEdits = {};
    for (const e of list) {
        if (e.moved_nodes) out.moved_nodes = { ...out.moved_nodes, ...e.moved_nodes };
        if (e.net_presence) {
            const absent = joined(out.net_presence?.absent, e.net_presence.absent);
            const present = joined(out.net_presence?.present, e.net_presence.present);
            out.net_presence = { ...(absent ? { absent } : {}), ...(present ? { present } : {}) };
        }
        if (e.created_at_start) {
            const on = joined(out.created_at_start?.on, e.created_at_start.on);
            const off = joined(out.created_at_start?.off, e.created_at_start.off);
            out.created_at_start = { ...(on ? { on } : {}), ...(off ? { off } : {}) };
        }
        if (e.node_values) {
            const values = { ...out.node_values };
            for (const [n, v] of Object.entries(e.node_values)) values[n] = { ...values[n], ...v };
            out.node_values = values;
        }
        if (e.rails) {
            const added = joined(out.rails?.added, e.rails.added);
            const links = e.rails.links || out.rails?.links ? { ...out.rails?.links, ...e.rails.links } : undefined;
            const reused =
                e.rails.reused || out.rails?.reused ? { ...out.rails?.reused, ...e.rails.reused } : undefined;
            out.rails = { ...(added ? { added } : {}), ...(links ? { links } : {}), ...(reused ? { reused } : {}) };
        }
        if (e.face_values) {
            const faces = { ...out.face_values };
            for (const [o, v] of Object.entries(e.face_values)) {
                const have = faces[o];
                if (!have) {
                    faces[o] = { ...v };
                    continue;
                }
                const terrain = joined(have.terrain, v.terrain);
                faces[o] = {
                    ...have,
                    ...(v.set || have.set ? { set: { ...have.set, ...v.set } } : {}),
                    ...(v.clear || have.clear ? { clear: { ...have.clear, ...v.clear } } : {}),
                    ...(terrain ? { terrain } : {}),
                };
            }
            out.face_values = faces;
        }
        if (e.collision_removed) out.collision_removed = { ...out.collision_removed, ...e.collision_removed };
        if (e.collision_reshaped) out.collision_reshaped = { ...out.collision_reshaped, ...e.collision_reshaped };
    }
    return out;
}
