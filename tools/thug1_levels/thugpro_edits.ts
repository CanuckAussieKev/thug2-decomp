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

import { NET_RESTART_TYPES } from "../level_port/nodes.ts";
import * as prx from "../level_port/prx.ts";
import * as qb from "../level_port/qb.ts";
import { Name, type QbValue, Struct, Vector } from "../level_port/qb.ts";
import * as thug2Col from "../level_port/thug2_col.ts";
import * as thug2Scene from "../level_port/thug2_scene.ts";
import { hex8, pyJoin } from "../lib/py.ts";
import { maxDiff, nodeArray, position, TOLERANCE } from "../thaw_levels/thugpro_compare.ts";
import * as thug1 from "./thug1.ts";
import { prePath } from "./thug1_levels.ts";

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
