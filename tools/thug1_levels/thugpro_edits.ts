// NOT ORIGINAL - what THUGPRO changed in its ports of THUG1's levels against THUG1's own data (read only: THUG1's
// <LV>.pre / <LV>scn.pre / <LV>col.pre, THUGPRO's <level>_scripts.prx / <level>scn.prx / <level>col.prx):
//   - spawnEdits: its edits to the network spawns (restarts with Multiplayer / Horse / CTF / Team types) - moved,
//     re-typed, renamed, added, removed, reordered. TP-31 (tools/compat/thugpro/thug1_levels.json "spawns", approved by
//     the user 2026-10-20) gives our ports the same spawns: THUG2 starts player n at the n-th Multiplayer restart of
//     the node array and a respawn at one of the first 8 (Skate 0x00566100 -> GetRestartNode 0x0046B790;
//     CSkater::MoveToRandomRestart 0x00580830 passes Rnd(8)), so the set, the places and the order all count.
//   - inventory: every other hand edit, by kind (nodes moved / renamed / reused / added / removed / changed, level
//     scripts, scene sectors, collision objects), for the user to decide on (docs/pending/thug1.md).
// Used by thugpro_compare.ts (--extract-thugpro, --details).

import { NET_RESTART_TYPES, THUG2_TERRAINS } from "../level_port/nodes.ts";
import * as prx from "../level_port/prx.ts";
import * as qb from "../level_port/qb.ts";
import { Name, type QbValue, Struct, Vector } from "../level_port/qb.ts";
import * as thug2Col from "../level_port/thug2_col.ts";
import * as thug2Scene from "../level_port/thug2_scene.ts";
import { hex8, pyJoin } from "../lib/py.ts";
import { maxDiff, nodeArray, position, TOLERANCE } from "../thaw_levels/thugpro_compare.ts";
import * as handEditsLib from "./hand_edits.ts";
import * as thug1 from "./thug1.ts";
import { FACE_KEEP, prePath } from "./thug1_levels.ts";

// One side's level: its node array (structures), names, level qb files (by lower-case path), and archives.
export interface Side {
    nodes: Struct[];
    names: Map<number, string>;
    qbs: Map<string, Buffer>;
    scn: string | null;
    col: string | null;
}

function qbFiles(archive: string | null, folder: string): Map<string, Buffer> {
    const out = new Map<string, Buffer>();
    if (archive === null) return out;
    const prefix = `levels\\${folder.toLowerCase()}\\`;
    for (const [n, d] of prx.entries(archive, (x) => x.toLowerCase().replaceAll("/", "\\").startsWith(prefix))) {
        if (n.toLowerCase().endsWith(".qb")) out.set(n.toLowerCase().replaceAll("/", "\\"), d);
    }
    return out;
}

function sideOf(qbs: Map<string, Buffer>, main: string, scn: string | null, col: string | null): Side {
    const data = qbs.get(main);
    if (data === undefined) throw new TypeError(`no ${main}`);
    const [array, names] = nodeArray(data, main);
    for (const d of qbs.values())
        for (const [k, v] of new qb.QbFile(d, main, true).names) if (!names.has(k)) names.set(k, v);
    return { nodes: array.filter((n): n is Struct => n instanceof Struct), names, qbs, scn, col };
}

export function thug1Side(thug1: string, code: string): Side {
    const qbs = qbFiles(prePath({ thug1 }, `${code}.pre`), code);
    const lc = code.toLowerCase();
    return sideOf(
        qbs,
        `levels\\${lc}\\${lc}.qb`,
        prePath({ thug1 }, `${code}scn.pre`),
        prePath({ thug1 }, `${code}col.pre`),
    );
}

export function thugproSide(thugpro: string, key: string): Side {
    const pre = (f: string): string => pyJoin(thugpro, "data", "pre", f);
    const qbs = qbFiles(pre(`${key}_scripts.prx`), key);
    return sideOf(qbs, `levels\\${key}\\${key}.qb`, pre(`${key}scn.prx`), pre(`${key}col.prx`));
}

const text = (v: QbValue | undefined, names: ReadonlyMap<number, string>): string =>
    v instanceof Name ? (names.get(v.value) ?? hex8(v.value)) : "";

export const nodeName = (n: Struct, names: ReadonlyMap<number, string>): string => text(n.get("Name"), names);
const classText = (n: Struct, names: ReadonlyMap<number, string>): string => text(n.get("Class"), names);

// A restart's types (restart_types, else its Type), as written.
export function restartTypes(n: Struct, names: ReadonlyMap<number, string>): string[] {
    const types = n.get("restart_types");
    if (Array.isArray(types)) return types.map((t) => text(t, names));
    const t = n.get("Type");
    return t instanceof Name ? [text(t, names)] : [];
}

const isRestart = (n: Struct, names: ReadonlyMap<number, string>): boolean =>
    classText(n, names).toLowerCase() === "restart";

// A network spawn: a restart with a Multiplayer / Horse / CTF / Team type (nodes.NET_RESTART_TYPES).
export function isSpawn(n: Struct, names: ReadonlyMap<number, string>): boolean {
    return isRestart(n, names) && restartTypes(n, names).some((t) => NET_RESTART_TYPES.has(t.toLowerCase()));
}

const angles = (n: Struct): number[] => {
    const a = n.get("Angles");
    return a instanceof Vector ? [...a.values] : [0.0, 0.0, 0.0];
};

const sameTypes = (a: readonly string[], b: readonly string[]): boolean =>
    a.length === b.length && a.every((t, i) => t.toLowerCase() === b[i]!.toLowerCase());

export interface Placement {
    pos: number[];
    angles: number[];
}

// TP-31's data for one level (each field only when THUGPRO changed something of that kind).
export interface SpawnEdits {
    moved?: Record<string, Placement>; // THUG1's spawn -> THUGPRO's place (Pos axes, Angles)
    types?: Record<string, string[]>; // THUG1's restart -> THUGPRO's restart_types
    renamed?: Record<string, string>; // THUG1's name -> THUGPRO's
    added?: (Placement & { name: string; types: string[] })[]; // THUGPRO's own spawns (appended, in its order)
    removed?: string[]; // THUG1's spawns THUGPRO hasn't at all (their network types dropped)
    order?: string[]; // THUGPRO's spawn order (by THUG1's names)
}

export const spawnOrder = (nodes: readonly Struct[], names: ReadonlyMap<number, string>): string[] =>
    nodes.filter((n) => isSpawn(n, names)).map((n) => nodeName(n, names));

export function spawnEdits(t1: Side, tp: Side): SpawnEdits {
    const byName = (s: Side): Map<string, Struct> =>
        new Map(s.nodes.map((n) => [nodeName(n, s.names).toLowerCase(), n]));
    const ours = byName(t1);
    const theirs = byName(tp);
    const out: SpawnEdits = {};
    const moved: Record<string, Placement> = {};
    const types: Record<string, string[]> = {};
    const renamed: Record<string, string> = {};
    const removed: string[] = [];
    const renameTargets = new Set<string>();
    for (const n of t1.nodes) {
        if (!isRestart(n, t1.names)) continue;
        const name = nodeName(n, t1.names);
        const them = theirs.get(name.toLowerCase());
        const mine = restartTypes(n, t1.names);
        if (them === undefined) {
            if (!isSpawn(n, t1.names)) continue;
            // (renamed: a THUGPRO-only restart at the same place with the same types)
            const p = position(n)!;
            const twin = tp.nodes.find(
                (m) =>
                    isRestart(m, tp.names) &&
                    !ours.has(nodeName(m, tp.names).toLowerCase()) &&
                    maxDiff(position(m)!, p) <= TOLERANCE &&
                    sameTypes(restartTypes(m, tp.names), mine),
            );
            if (twin) {
                renamed[name] = nodeName(twin, tp.names);
                renameTargets.add(nodeName(twin, tp.names).toLowerCase());
            } else removed.push(name);
            continue;
        }
        if (!isSpawn(n, t1.names) && !isSpawn(them, tp.names)) continue;
        const theirTypes = restartTypes(them, tp.names);
        if (!sameTypes(mine, theirTypes)) types[name] = theirTypes;
        const p = position(them)!;
        if (maxDiff(position(n)!, p) > TOLERANCE || maxDiff(angles(n), angles(them)) > 0.001) {
            moved[name] = { pos: [...p], angles: angles(them) };
        }
    }
    const added = tp.nodes
        .filter((m) => isSpawn(m, tp.names) && !ours.has(nodeName(m, tp.names).toLowerCase()))
        .filter((m) => !renameTargets.has(nodeName(m, tp.names).toLowerCase()))
        .map((m) => ({
            name: nodeName(m, tp.names),
            pos: [...position(m)!],
            angles: angles(m),
            types: restartTypes(m, tp.names),
        }));
    if (Object.keys(moved).length) out.moved = moved;
    if (Object.keys(types).length) out.types = types;
    if (Object.keys(renamed).length) out.renamed = renamed;
    if (added.length) out.added = added;
    if (removed.length) out.removed = removed;
    // (always: our converter's own order can differ from THUG1's - its Player1-first swap; by THUG1's names)
    const back = new Map(Object.entries(renamed).map(([a, b]) => [b.toLowerCase(), a]));
    out.order = spawnOrder(tp.nodes, tp.names).map((n) => back.get(n.toLowerCase()) ?? n);
    return out;
}

// --- the inventory of THUGPRO's other edits ----------------------------------------------------------------------

export interface Item {
    kind: string; // the category (e.g. "moved RailNode")
    name: string;
    detail: string;
}

// The keys compared besides the position (flags by name).
function props(n: Struct, names: ReadonlyMap<number, string>): Map<string, string> {
    const out = new Map<string, string>();
    for (const [k, v] of n.entries) {
        if (k === null) {
            out.set(`flag ${text(v, names)}`, "on");
            continue;
        }
        const key = names.get(k.value) ?? hex8(k.value);
        if (["pos", "position", "angles", "name", "links"].includes(key.toLowerCase())) continue;
        out.set(key, show(v, names));
    }
    return out;
}

function show(v: QbValue, names: ReadonlyMap<number, string>): string {
    if (v instanceof Name) return names.get(v.value) ?? hex8(v.value);
    if (v instanceof Vector) return `(${v.values.map((x) => Math.round(x * 100) / 100).join(", ")})`;
    if (v instanceof Struct)
        return `{${v.entries.map(([k, x]) => `${k ? `${names.get(k.value) ?? hex8(k.value)}=` : ""}${show(x, names)}`).join(" ")}}`;
    if (Array.isArray(v)) return `[${v.map((x) => show(x, names)).join(" ")}]`;
    if (typeof v === "object" && v !== null && "value" in v) return String((v as { value: unknown }).value);
    return typeof v === "string" ? JSON.stringify(v) : String(v);
}

// Scripts as comparable text: each statement's tokens (the line-number tokens left out).
function scriptTexts(qbs: ReadonlyMap<string, Buffer>, label: string): Map<number, string> {
    const out = new Map<number, string>();
    for (const [n, d] of qbs) {
        const f = new qb.QbFile(d, `${label} ${n}`, true);
        for (const [s, lines] of f.scripts) {
            out.set(
                s,
                lines
                    .filter((l) => l.length)
                    .map((l) => l.map(([t, p]) => `${t}:${p.toString("hex")}`).join(" "))
                    .join("\n"),
            );
        }
    }
    return out;
}

function sectors(file: string | null, thug1Format: boolean): Map<number, number[]> {
    const out = new Map<number, number[]>();
    if (file === null) return out;
    for (const [n, d] of prx.entries(
        file,
        (x) => x.toLowerCase().endsWith(".scn.xbx") && !x.toLowerCase().includes("_sky"),
    )) {
        const scene = thug1Format ? thug1.readScene(d, n) : thug2Scene.readScene(d, n);
        for (const s of scene.sectors) out.set(s.checksum, [...s.bbox]);
    }
    return out;
}

function colObjects(file: string | null, thug1Format: boolean): Map<number, thug2Col.ColObject> {
    const out = new Map<number, thug2Col.ColObject>();
    if (file === null) return out;
    for (const [n, d] of prx.entries(file, (x) => x.toLowerCase().endsWith(".col.xbx"))) {
        const col = thug1Format ? thug1.readCol(d, n) : thug2Col.readCol(d, n);
        for (const o of col.objects) out.set(o.checksum, o);
    }
    return out;
}

const boxDiff = (a: readonly number[], b: readonly number[]): number =>
    Math.max(...[0, 1, 2, 4, 5, 6].map((k) => Math.abs((a[k] ?? 0) - (b[k] ?? 0))));

export function inventory(t1: Side, tp: Side): Item[] {
    const items: Item[] = [];
    const names = new Map([...t1.names, ...tp.names]);
    const theirs = new Map(tp.nodes.map((n) => [nodeName(n, tp.names).toLowerCase(), n]));
    const ours = new Map(t1.nodes.map((n) => [nodeName(n, t1.names).toLowerCase(), n]));
    for (const n of t1.nodes) {
        const name = nodeName(n, t1.names);
        const cls = classText(n, t1.names);
        const them = theirs.get(name.toLowerCase());
        if (them === undefined) {
            items.push({ kind: `removed ${cls}`, name, detail: "" });
            continue;
        }
        const theirCls = classText(them, tp.names);
        const d = maxDiff(position(n) ?? [0, 0, 0], position(them) ?? [0, 0, 0]);
        const a = props(n, names);
        const b = props(them, names);
        const changed = [...new Set([...a.keys(), ...b.keys()])]
            .filter((k) => a.get(k) !== b.get(k))
            .map((k) =>
                k.startsWith("flag ")
                    ? `${a.has(k) ? "-" : "+"}${k.slice(5)}`
                    : `${k} ${a.get(k) ?? "-"} -> ${b.get(k) ?? "-"}`,
            );
        if (theirCls.toLowerCase() !== cls.toLowerCase()) {
            items.push({
                kind: `reused ${cls} as ${theirCls}`,
                name,
                detail: `moved ${d.toFixed(1)}; ${changed.join("; ")}`,
            });
            continue;
        }
        if (d > TOLERANCE) {
            const reused = changed.some((c) => /^(Cluster|TerrainType|Type|model) /.test(c)) && d > 500;
            items.push({
                kind: `${reused ? "reused" : "moved"} ${cls}`,
                name,
                detail: `${d.toFixed(1)}${changed.length ? `; ${changed.join("; ")}` : ""}`,
            });
        } else if (changed.length) {
            items.push({ kind: `changed ${cls}`, name, detail: changed.join("; ") });
        }
        const la = n.get("Links");
        const lb = them.get("Links");
        const linkNames = (l: QbValue | undefined, s: Side): string =>
            (Array.isArray(l) ? l : [])
                .map((i) => (typeof i === "number" && s.nodes[i] ? nodeName(s.nodes[i]!, s.names) : "?"))
                .join(",");
        if (linkNames(la, t1).toLowerCase() !== linkNames(lb, tp).toLowerCase()) {
            items.push({ kind: `relinked ${cls}`, name, detail: `${linkNames(la, t1)} -> ${linkNames(lb, tp)}` });
        }
    }
    for (const m of tp.nodes) {
        const name = nodeName(m, tp.names);
        if (!ours.has(name.toLowerCase()))
            items.push({ kind: `added ${classText(m, tp.names)}`, name, detail: show(m, names).slice(0, 160) });
    }
    // level scripts
    const sa = scriptTexts(t1.qbs, "THUG1");
    const sb = scriptTexts(tp.qbs, "THUGPRO");
    for (const [s, body] of sb) {
        const label = names.get(s) ?? hex8(s);
        if (!sa.has(s)) items.push({ kind: "added script", name: label, detail: "" });
        else if (sa.get(s) !== body) items.push({ kind: "changed script", name: label, detail: "" });
    }
    for (const s of sa.keys())
        if (!sb.has(s)) items.push({ kind: "removed script", name: names.get(s) ?? hex8(s), detail: "" });
    // scene sectors and collision objects
    const ga = sectors(t1.scn, true);
    const gb = sectors(tp.scn, false);
    for (const [c, box] of gb) {
        if (!ga.has(c)) items.push({ kind: "added scene sector", name: names.get(c) ?? hex8(c), detail: "" });
        else if (boxDiff(ga.get(c)!, box) > 1)
            items.push({
                kind: "moved / reshaped scene sector",
                name: names.get(c) ?? hex8(c),
                detail: `box ${boxDiff(ga.get(c)!, box).toFixed(1)}`,
            });
    }
    for (const c of ga.keys())
        if (!gb.has(c)) items.push({ kind: "removed scene sector", name: names.get(c) ?? hex8(c), detail: "" });
    const ca = colObjects(t1.col, true);
    const cb = colObjects(tp.col, false);
    for (const [c, o] of cb) {
        const old = ca.get(c);
        const label = names.get(c) ?? hex8(c);
        if (old === undefined) items.push({ kind: "added collision", name: label, detail: `${o.faces.length} faces` });
        else if (
            old.faces.length !== o.faces.length ||
            old.verts.length !== o.verts.length ||
            boxDiff(old.bbox, o.bbox) > 1
        ) {
            items.push({
                kind: "changed collision",
                name: label,
                detail: `faces ${old.faces.length} -> ${o.faces.length}, box ${boxDiff(old.bbox, o.bbox).toFixed(1)}`,
            });
        } else {
            const flags = o.faces.filter((f, i) => (f[0] & 0x1fff) !== ((old.faces[i]?.[0] ?? 0) & 0x1fff)).length;
            if (flags)
                items.push({
                    kind: "changed collision face flags",
                    name: label,
                    detail: `${flags} of ${o.faces.length} faces`,
                });
        }
    }
    for (const [c, o] of ca)
        if (!cb.has(c))
            items.push({ kind: "removed collision", name: names.get(c) ?? hex8(c), detail: `${o.faces.length} faces` });
    return items;
}

// --- THUGPRO's hand edits as data (TP-32 ... TP-39: hand_edits.ts) ---------------------------------------------------
//
// handEdits: every edit THUGPRO made to a THUG1 level that is data - nodes moved, flags, values, rails added from node
// data, links, collision face flags / terrain, collision taken out or reshaped - in hand_edits.LevelEdits' form, and
// notes on what it leaves out and why: the toolchain's translation (peds' THPS5 -> THPS6 skeletons and their AI
// settings, goal pick-ups' models set to "none", goal trigger scripts dropped, rigid-body settings dropped, the
// SelectRenderOnly flag misspelt sRenderOnly, empty collision objects, zero-area and duplicate faces dropped) and
// THUGPRO's own content (nodes other than rails it added, trigger scripts it wrote, collision faces of its own).

// Keys left to their own entries or not taken (see above), by checksum.
const KEYS_ELSEWHERE = new Set(["Pos", "Position", "Angles", "Name", "Links"].map(qb.checksum));
const RIGID_BODY = new Set(
    [
        "Mass",
        "Ellasticity",
        "Friction",
        "Optimization_Level",
        "Unyielding",
        "Inactive",
        "Disable_Collisions",
        "Display_Children",
        "Display_Proxy",
        "Proxy_Geometry",
        "Simulation_Geometry",
        "Use_Display_Proxy",
    ].map(qb.checksum),
);
const ABSENT = qb.checksum("AbsentInNetGames"); // (TP-33)
const CREATED = qb.checksum("CreatedAtStart"); // (TP-34)
const MISSPELT = new Set(["SelectRenderOnly", "sRenderOnly"].map(qb.checksum)); // (THUGPRO's compiler's)
const TERRAIN_TYPE = qb.checksum("TerrainType");
const THUG2_TERRAIN_SET = new Set(THUG2_TERRAINS.map((t) => qb.checksum(`TERRAIN_${t}`)));
const REUSED_DISTANCE = 500; // a rail point moved farther than this into a new rail: TP-36's (as the inventory's)
const VERTEX_TOLERANCE = 0.1; // (THUGPRO's fixed-point vertices: within 1/32)

const flagsOf = (n: Struct): Set<number> =>
    new Set(n.entries.filter(([k, v]) => k === null && v instanceof Name).map(([, v]) => (v as Name).value));

const keysOf = (n: Struct): Map<number, QbValue> =>
    new Map(n.entries.filter(([k]) => k !== null).map(([k, v]) => [k!.value, v] as const));

const same = (a: QbValue | undefined, b: QbValue | undefined, names: ReadonlyMap<number, string>): boolean =>
    a === undefined || b === undefined
        ? a === b
        : JSON.stringify(handEditsLib.encode(a, names)) === JSON.stringify(handEditsLib.encode(b, names));

function linkNames(n: Struct, s: Side, names: ReadonlyMap<number, string>): string[] | null {
    const links = n.get("Links");
    if (!Array.isArray(links)) return [];
    const out: string[] = [];
    for (const i of links) {
        const m = typeof i === "number" ? s.nodes[i] : undefined;
        if (m === undefined) return null;
        out.push(nodeName(m, names));
    }
    return out;
}

export interface HandEdits {
    edits: handEditsLib.LevelEdits;
    notes: string[]; // what isn't taken, by kind ("kind: count - examples")
}

export function handEdits(t1: Side, tp: Side): HandEdits {
    const names = new Map([...t1.names, ...tp.names]);
    const text = (v: number): string => handEditsLib.nameText(v, names);
    const notes = new Map<string, string[]>();
    const note = (kind: string, what: string): void => {
        const list = notes.get(kind) ?? [];
        if (!list.includes(what)) notes.set(kind, [...list, what]);
    };
    const count = new Map<string, number>();
    for (const n of t1.nodes) {
        const k = nodeName(n, names).toLowerCase();
        if (k) count.set(k, (count.get(k) ?? 0) + 1);
    }
    const theirs = new Map<string, Struct>();
    for (const m of tp.nodes) {
        const k = nodeName(m, names).toLowerCase();
        if (k && !theirs.has(k)) theirs.set(k, m);
    }
    const ours = new Set(count.keys());
    const moved: Record<string, handEditsLib.Placement> = {};
    const absent: string[] = [];
    const present: string[] = [];
    const on: string[] = [];
    const off: string[] = [];
    const values: Record<string, Record<string, handEditsLib.Json>> = {};
    const reused: Record<string, handEditsLib.Placement & { values: Record<string, handEditsLib.Json> }> = {};
    const links: Record<string, string[]> = {};
    const added: Record<string, handEditsLib.Json>[] = [];
    const addedNames = new Set(
        tp.nodes
            .filter((m) => classText(m, tp.names).toLowerCase() === "railnode")
            .map((m) => nodeName(m, names).toLowerCase())
            .filter((k) => k && !ours.has(k)),
    );
    for (const n of t1.nodes) {
        const name = nodeName(n, names);
        const them = theirs.get(name.toLowerCase());
        if (!name || them === undefined) continue;
        if ((count.get(name.toLowerCase()) ?? 0) > 1) {
            note("names used by several THUG1 nodes (not matched)", name);
            continue;
        }
        if (isSpawn(n, t1.names) || isSpawn(them, tp.names)) continue; // (TP-31's)
        const cls = classText(n, t1.names).toLowerCase();
        const classChanged = cls !== classText(them, tp.names).toLowerCase();
        const ka = keysOf(n);
        const kb = keysOf(them);
        if (cls === "pedestrian" && !classChanged) {
            // (THPS5 -> THPS6 skeletons as ours, profiles and AI settings: peds are kept out of network games)
            if ([...new Set([...ka.keys(), ...kb.keys()])].some((key) => !same(ka.get(key), kb.get(key), names)))
                note("peds' settings (skeletons, profiles, AI: translation / kept out of net games)", name);
            continue;
        }
        // flags
        const fa = flagsOf(n);
        const fb = flagsOf(them);
        if (fa.has(ABSENT) !== fb.has(ABSENT)) (fb.has(ABSENT) ? absent : present).push(name);
        if (fa.has(CREATED) !== fb.has(CREATED)) (fb.has(CREATED) ? on : off).push(name);
        const change: Record<string, handEditsLib.Json> = {};
        for (const f of new Set([...fa, ...fb])) {
            if (fa.has(f) === fb.has(f) || f === ABSENT || f === CREATED) continue;
            if (MISSPELT.has(f)) {
                note("SelectRenderOnly written sRenderOnly (THUGPRO's compiler)", name);
                continue;
            }
            if (text(f).toUpperCase().startsWith("TERRAIN_")) {
                note("a terrain written as a flag (THUGPRO's slip; the TerrainType stays)", name);
                continue;
            }
            change[`${fb.has(f) ? "+" : "-"}${text(f)}`] = true;
        }
        // values
        for (const key of new Set([...ka.keys(), ...kb.keys()])) {
            const a = ka.get(key);
            const b = kb.get(key);
            if (KEYS_ELSEWHERE.has(key) || same(a, b, names)) continue;
            if (key === qb.checksum("TriggerScript")) {
                note(
                    b === undefined
                        ? "trigger scripts taken off (goals: THUGPRO doesn't port them)"
                        : "trigger scripts of THUGPRO's own",
                    name,
                );
                continue;
            }
            if (RIGID_BODY.has(key)) {
                note("rigid-body settings dropped (THUGPRO's toolchain)", name);
                continue;
            }
            if (!classChanged && key === qb.checksum("model") && typeof b === "string" && b.toLowerCase() === "none") {
                note('goal models set to "none" (not in THUG2: translation)', name);
                continue;
            }
            if (key === TERRAIN_TYPE && b instanceof Name && !THUG2_TERRAIN_SET.has(b.value)) {
                // (THUG2 reads through a null for a terrain its table hasn't - nodes.fixTerrains: ours keeps THUG1's)
                note(`terrains THUG2 hasn't (${text(b.value)}: not taken)`, name);
                continue;
            }
            if (!classChanged && key === qb.checksum("Extra_Anims") && b === undefined) {
                note("extra animations dropped (not in THUG2: translation)", name);
                continue;
            }
            change[text(key)] = b === undefined ? null : handEditsLib.encode(b, names);
        }
        const p = position(n);
        const q = position(them);
        const far = p !== null && q !== null ? maxDiff(p, q) : 0;
        const turned = maxDiff(angles(n), angles(them)) > 0.001;
        if (cls === "railnode" && far > REUSED_DISTANCE) {
            reused[name] = { pos: [...q!], angles: angles(them), values: change };
        } else {
            if (q !== null && (far > TOLERANCE || turned)) moved[name] = { pos: [...q], angles: angles(them) };
            if (Object.keys(change).length) values[name] = change;
        }
        // links (by name; only to nodes ours will have)
        const la = linkNames(n, t1, names);
        const lb = linkNames(them, tp, names);
        if (la !== null && lb !== null && la.join(",").toLowerCase() !== lb.join(",").toLowerCase()) {
            if (lb.every((l) => ours.has(l.toLowerCase()) || addedNames.has(l.toLowerCase()))) links[name] = lb;
            else note("links to nodes of THUGPRO's own (not taken)", name);
        }
    }
    // nodes THUGPRO added: rails from their node data; the rest listed
    for (const m of tp.nodes) {
        const name = nodeName(m, names);
        if (!name || ours.has(name.toLowerCase())) continue;
        if (!addedNames.has(name.toLowerCase())) {
            note(`nodes of THUGPRO's own (${classText(m, tp.names)})`, name);
            continue;
        }
        const fields: Record<string, handEditsLib.Json> = {};
        const flags: string[] = [];
        for (const [k, v] of m.entries) {
            if (k === null) {
                if (v instanceof Name) flags.push(text(v.value));
            } else if (k.value !== qb.checksum("Links")) fields[text(k.value)] = handEditsLib.encode(v, names);
        }
        if (flags.length) fields.flags = flags;
        const l = linkNames(m, tp, names);
        if (l === null || !l.every((x) => ours.has(x.toLowerCase()) || addedNames.has(x.toLowerCase())))
            throw new TypeError(`THUGPRO's rail ${name} links to a node of its own that isn't a rail`);
        if (l.length) fields.Links = l;
        added.push(fields);
    }
    for (const n of t1.nodes) {
        const name = nodeName(n, names);
        if (name && !theirs.has(name.toLowerCase()) && !isSpawn(n, t1.names))
            note(`THUG1 nodes THUGPRO took out or renamed (${classText(n, t1.names)})`, name);
    }
    const edits: handEditsLib.LevelEdits = {};
    if (Object.keys(moved).length) edits.moved_nodes = moved;
    if (absent.length || present.length) {
        edits.net_presence = {};
        if (absent.length) edits.net_presence.absent = absent;
        if (present.length) edits.net_presence.present = present;
    }
    if (on.length || off.length) {
        edits.created_at_start = {};
        if (on.length) edits.created_at_start.on = on;
        if (off.length) edits.created_at_start.off = off;
    }
    if (Object.keys(values).length) edits.node_values = values;
    if (added.length || Object.keys(reused).length || Object.keys(links).length) {
        edits.rails = {};
        if (added.length) edits.rails.added = added;
        if (Object.keys(reused).length) edits.rails.reused = reused;
        if (Object.keys(links).length) edits.rails.links = links;
    }
    colEdits(t1, tp, names, edits, note);
    return {
        edits,
        notes: [...notes].map(([k, list]) => `${k}: ${list.length} - ${list.slice(0, 5).join(" ")}`),
    };
}

// The collision part of handEdits: faces matched by index when both have the same faces (vertex indices), else by
// their vertices' places.
function colEdits(
    t1: Side,
    tp: Side,
    names: ReadonlyMap<number, string>,
    edits: handEditsLib.LevelEdits,
    note: (kind: string, what: string) => void,
): void {
    const ca = colObjects(t1.col, true);
    const cb = colObjects(tp.col, false);
    const faceValues: Record<string, handEditsLib.FaceValues> = {};
    const removed: Record<string, handEditsLib.ColRemoval> = {};
    const reshaped: Record<string, handEditsLib.ColReshape> = {};
    const near = (a: readonly number[], b: readonly number[]): boolean => maxDiff(a, b) <= VERTEX_TOLERANCE;
    const corners = (o: thug2Col.ColObject, f: thug2Col.Face): number[][] => [f[2], f[3], f[4]].map((i) => o.verts[i]!);
    const sameFace = (a: number[][], b: number[][]): boolean => {
        const used = new Set<number>();
        return a.every((v) => {
            const j = b.findIndex((w, k) => !used.has(k) && near(v, w));
            if (j < 0) return false;
            used.add(j);
            return true;
        });
    };
    const area = (c: number[][]): number => {
        const u = [0, 1, 2].map((k) => c[1]![k]! - c[0]![k]!);
        const v = [0, 1, 2].map((k) => c[2]![k]! - c[0]![k]!);
        const x = [u[1]! * v[2]! - u[2]! * v[1]!, u[2]! * v[0]! - u[0]! * v[2]!, u[0]! * v[1]! - u[1]! * v[0]!];
        return Math.sqrt(x[0]! * x[0]! + x[1]! * x[1]! + x[2]! * x[2]!) / 2;
    };
    for (const [c, old] of ca) {
        const label = handEditsLib.nameText(c, names);
        const o = cb.get(c);
        if (o === undefined) {
            if (old.faces.length) removed[label] = { faces: old.faces.length, remove: "all" };
            else note("empty collision objects dropped (THUGPRO's exporter)", label);
            continue;
        }
        const pairs: [number, number][] = []; // [THUG1 face, THUGPRO face]
        const indexed =
            old.faces.length === o.faces.length &&
            old.verts.length === o.verts.length &&
            old.faces.every((f, i) => f[2] === o.faces[i]![2] && f[3] === o.faces[i]![3] && f[4] === o.faces[i]![4]);
        if (indexed) {
            for (let i = 0; i < old.faces.length; i++) pairs.push([i, i]);
            const verts: [number, number, number, number][] = [];
            old.verts.forEach((v, i) => {
                const w = o.verts[i]!;
                if (!near(v, w)) verts.push([i, w[0]!, w[1]!, w[2]!]);
            });
            if (verts.length) reshaped[label] = { faces: old.faces.length, verts };
        } else {
            const used = new Set<number>();
            o.faces.forEach((f, j) => {
                const cf = corners(o, f);
                const i = old.faces.findIndex((g, k) => !used.has(k) && sameFace(corners(old, g), cf));
                if (i < 0) note("collision faces of THUGPRO's own (not expressible)", label);
                else {
                    used.add(i);
                    pairs.push([i, j]);
                }
            });
            const gone: number[] = [];
            old.faces.forEach((g, i) => {
                if (used.has(i)) return;
                const cg = corners(old, g);
                const duplicate = [...used].some((k) => sameFace(corners(old, old.faces[k]!), cg));
                if (area(cg) < 0.01 || duplicate)
                    note("zero-area / duplicate faces dropped (THUGPRO's exporter)", label);
                else gone.push(i);
            });
            if (gone.length) removed[label] = { faces: old.faces.length, remove: gone };
        }
        const set: Record<string, number[]> = {};
        const clear: Record<string, number[]> = {};
        const terrain: [number, number][] = [];
        for (const [i, j] of pairs) {
            const a = old.faces[i]![0] & FACE_KEEP;
            const b = o.faces[j]![0] & FACE_KEEP;
            for (let bit = 1; bit <= FACE_KEEP; bit <<= 1) {
                const key = `0x${bit.toString(16).padStart(4, "0")}`;
                if (b & bit && !(a & bit)) set[key] = [...(set[key] ?? []), i];
                if (a & bit && !(b & bit)) clear[key] = [...(clear[key] ?? []), i];
            }
            if (old.faces[i]![1] !== o.faces[j]![1]) terrain.push([i, o.faces[j]![1]]);
        }
        if (Object.keys(set).length || Object.keys(clear).length || terrain.length) {
            const e: handEditsLib.FaceValues = { faces: old.faces.length };
            if (Object.keys(set).length) e.set = set;
            if (Object.keys(clear).length) e.clear = clear;
            if (terrain.length) e.terrain = terrain;
            faceValues[label] = e;
        }
    }
    for (const c of cb.keys())
        if (!ca.has(c)) note("collision objects of THUGPRO's own", handEditsLib.nameText(c, names));
    if (Object.keys(faceValues).length) edits.face_values = faceValues;
    if (Object.keys(removed).length) edits.collision_removed = removed;
    if (Object.keys(reshaped).length) edits.collision_reshaped = reshaped;
}
