// NOT ORIGINAL - extracts THUGPRO's hand edits to its THAW level ports into the THUGPRO compatibility profile
// (tools/compat/thugpro/thaw_levels.json: TP-5 node_edits, TP-6 node_flags, TP-7 removed, TP-8 added, TP-9 collision;
// what isn't applied - THUGPRO's own meshes, models and scripts - in `awaiting`). Run by
// `node tools/thaw_levels/thugpro_compare.ts --extract-thugpro` (with TP-4's spawns). Read only on both installs:
// THUGPRO's <thugpro>\data\pre\<level>_scripts.prx / <level>scn.prx / <level>col.prx against THAW's own zone archives
// (<thaw>\Game\data\worlds\worldzones\<zone>\<zone>_net.pak.wpc - what thaw_levels converts - and <zone>.pak.wpc, the
// full one with the story's pieces), in THUGPRO's frame. Applied by thugpro_edits.ts.
//
// What counts as THUGPRO's edit (the rest of the differences come from THUGPRO converting THAW's full archive, not the
// net one, or from its toolchain - tools/thaw_levels/thugpro_inventory.ts lists everything):
//   - TP-5: a node of the net array at another place (> 0.1 units) or turned, or with other Links (by name); a scene
//     sector / collision object THUGPRO moved rigidly (the same vertices, moved and turned about y: fitted, the fit
//     checked to 0.1 units); a node renamed (THUGPRO's node of another name at its place, same class) when no THAW
//     script or node names it;
//   - TP-6: the flags CreatedAtStart / AbsentInNetGames / NetEnabled / TrickObject and the keys Class, Type,
//     TerrainType, Cluster, CollisionMode, restart_types / RestartName (not TP-4's spawns), TriggerScript (taken off,
//     or naming a THAW / THUG2 script; one naming THUGPRO's own script awaits the user); the scene sectors and
//     collision of nodes whose AbsentInNetGames THUGPRO took off, from THAW's full archive;
//   - TP-7: net-array nodes THUGPRO hasn't (and didn't rename), scene sectors and collision objects it took out;
//   - TP-8: nodes THUGPRO added (not in THAW's full archive) made only of what our side has: rails, waypoints,
//     restarts, generic nodes, objects with THUG2 models and THAW / THUG2 scripts;
//   - TP-9: per-face collision flags (THUG2's, 0x1FFF) and terrain on the collision objects whose faces are THAW's
//     (same count, same vertices).

import * as nodes from "../level_port/nodes.ts";
import type * as pedsCars from "../level_port/peds_cars.ts";
import * as prx from "../level_port/prx.ts";
import { checksum, Name, type QbValue, Struct, Vector } from "../level_port/qb.ts";
import type * as scriptPort from "../level_port/script_port.ts";
import * as thug2Col from "../level_port/thug2_col.ts";
import * as thug2Scene from "../level_port/thug2_scene.ts";
import { pyExit, pyRound } from "../lib/py.ts";
import * as thaw from "./thaw.ts";
import {
    COMPAT,
    findPath,
    frameShift,
    MULTIPLAYER_NAME,
    multiplayerNames,
    zoneArray,
    zoneFiles,
    zonePak,
} from "./thaw_levels.ts";
import { nodeArray } from "./thugpro_compare.ts";
import {
    type AddedNode,
    encodeValue,
    type FaceEdits,
    type FlagEdit,
    type JsonValue,
    movePoint,
    type NodeMove,
    nameOfValue,
    nameValue,
    type Transform,
} from "./thugpro_edits.ts";

const MOVED = 0.1; // units: a node further than this from THAW's place (in the same frame) is THUGPRO's move
const ANGLE = 0.001;
const FIT = 0.1; // units: the most a vertex may be off a rigid move (else THUGPRO changed the mesh; the frames differ by < 1/32)
const FACE_KEEP = 0x1fff; // (thaw_levels: THUG2's face flags)
export const APPLIED_FLAGS = ["CreatedAtStart", "AbsentInNetGames", "NetEnabled", "TrickObject"];
const APPLIED_KEYS = ["Class", "Type", "TerrainType", "Cluster", "CollisionMode", "restart_types", "RestartName"];
// Keys THUGPRO's toolchain changes on every node (thaw_node_array_converter.py: commented-out THPS leftovers, model /
// profile / sound type checks, the particle keys rewritten) and the ones compared otherwise (positions, Links): not
// hand edits.
export const SYSTEMATIC: ReadonlySet<string> = new Set([
    ...["ignoredq", "psylinder", "terrain1", "terrain2", "object", "shatter", "hitpoints", "push0", "push1", "push2"],
    ...["model", "profile", "soundtype", "skeletonname", "animname", "excludegeometry", "pos", "position", "links"],
    ...["boxdims", "boxpositions", "boxrgbas", "radii", "radiusspread", "startposition", "midposition", "endposition"],
    ...["boxdimsstart", "boxdimsmid", "boxdimsend", "startradius", "midradius", "endradius", "startradiusspread"],
    ...["midradiusspread", "endradiusspread", "startrgb", "midrgb", "endrgb", "startalpha", "midalpha", "endalpha"],
]);
const POSITION_KEYS = ["pos", "position", "angles", "links", "name"];
const ADDED_CLASSES = new Set([
    "railnode",
    "waypoint",
    "restart",
    "genericnode",
    "vehicle",
    "pedestrian",
    "gameobject",
]);

// One item the user decides on (not applied): what, how big, what each side sees without it.
export interface Awaiting {
    kind: string;
    what: string;
    size: string;
}

export interface ZoneExtract {
    node_edits: { moved: NodeMove[]; renamed: { node: string; to: string }[]; geometry: Record<string, Transform> };
    node_flags: { nodes: FlagEdit[]; clusters: Record<string, string>; imported: string[] };
    removed: { nodes: string[]; sectors: string[]; collision: string[] };
    added: { nodes: AddedNode[] };
    collision: { faces: Record<string, FaceEdits> };
    awaiting: Awaiting[];
    notes: string[];
    counts: Record<string, number>;
}

// What the extraction needs besides the zone: THUG2's scripts and models (what our side has).
export interface Sources {
    thugpro: string;
    thawFolder: string;
    names: Map<number, string>;
    thug2: scriptPort.Thug2;
    assets: pedsCars.Thug2Assets;
    thawScripts: Set<number>; // every script THAW's qb.pak defines
}

export function thawScriptNames(thawFolder: string, names: Map<number, string>): Set<number> {
    const out = new Set<number>();
    const pak = findPath(thawFolder, "Game", "data", "pak", "qb.pak.wpc");
    if (pak === null) pyExit(`${thawFolder}: no qb.pak.wpc`);
    for (const [entry, data] of thaw.pakFiles(pak)) {
        if (![".qb", ".nqb", ".sqb"].includes(entry.extension)) continue;
        try {
            for (const k of new thaw.QbItems(data, entry.path || "qb", names).scripts.keys()) out.add(k);
        } catch {
            // (a file that doesn't read: its scripts don't count)
        }
    }
    return out;
}

type Points = number[][];

// A sector's / collision object's vertices (all meshes, in order).
function sectorPoints(meshes: readonly { stride: number; vertexCount: number; data: Buffer }[]): Points {
    const out: Points = [];
    for (const m of meshes) {
        for (let i = 0; i < m.vertexCount; i++) {
            const at = i * m.stride;
            out.push([m.data.readFloatLE(at), m.data.readFloatLE(at + 4), m.data.readFloatLE(at + 8)]);
        }
    }
    return out;
}

const worst = (a: Points, b: Points, t: (p: number[]) => number[]): number => {
    let d = 0;
    a.forEach((p, i) => {
        const q = t(p);
        for (let k = 0; k < 3; k++) d = Math.max(d, Math.abs(q[k]! - b[i]![k]!));
    });
    return d;
};

// Points by the unit cell they're in, to find a point's neighbours.
function pointIndex(points: Points): Map<string, Points> {
    const out = new Map<string, Points>();
    for (const p of points) {
        const k = p.map((x) => Math.floor(x)).join(",");
        const list = out.get(k);
        if (list) list.push(p);
        else out.set(k, [p]);
    }
    return out;
}

// Whether every point (moved by `t`) has one of `index`'s within FIT (each axis).
function covered(points: Points, t: (p: number[]) => number[], index: Map<string, Points>): boolean {
    return points.every((p) => {
        const q = t(p);
        const c = q.map((x) => Math.floor(x));
        for (let dx = -1; dx <= 1; dx++) {
            for (let dy = -1; dy <= 1; dy++) {
                for (let dz = -1; dz <= 1; dz++) {
                    const near = index.get(`${c[0]! + dx},${c[1]! + dy},${c[2]! + dz}`);
                    if (near?.some((r) => Math.max(...[0, 1, 2].map((k) => Math.abs(r[k]! - q[k]!))) <= FIT))
                        return true;
                }
            }
        }
        return false;
    });
}

// Whether `a` moved by `t` and `b` are the same shape: the same points, whatever their order or repeats (THUGPRO
// converted THAW's Xbox files: their meshes weld and order their vertices otherwise).
function sameShape(a: Points, b: Points, t: (p: number[]) => number[]): boolean {
    return covered(a, t, pointIndex(b)) && covered(b, (p) => p, pointIndex(a.map(t)));
}

// `a` (THAW's frame) against `b` (THUGPRO's, = THAW's + shift): "same", the rigid move (rotation about y,
// translation) that takes the one onto the other, or null (not rigid: THUGPRO changed the mesh). Tried: the vertices
// in the same order, `candidates` (the node's own move), a translation of the box.
export function fitRigid(
    a: Points,
    b: Points,
    shift: readonly number[],
    candidates: readonly Transform[] = [],
): "same" | Transform | null {
    if (!a.length || !b.length) return a.length === b.length ? "same" : null;
    const inFrame = (p: number[]): number[] => [p[0]! + shift[0]!, p[1]! + shift[1]!, p[2]! + shift[2]!];
    if ((a.length === b.length && worst(a, b, inFrame) <= FIT) || sameShape(a, b, inFrame)) return "same";
    const moved = (t: Transform) => (p: number[]) => inFrame(movePoint(p, t));
    const box = (p: Points): number[] =>
        [0, 1, 2].flatMap((k) => [Math.min(...p.map((v) => v[k]!)), Math.max(...p.map((v) => v[k]!))]);
    const ba = box(a);
    const bb = box(b);
    const tries: Transform[] = [...candidates];
    if ([0, 1, 2].every((k) => Math.abs(ba[2 * k + 1]! - ba[2 * k]! - (bb[2 * k + 1]! - bb[2 * k]!)) <= FIT)) {
        tries.push({ translate: [0, 1, 2].map((k) => pyRound(bb[2 * k]! - ba[2 * k]! - shift[k]!, 4)) });
    }
    if (a.length === b.length) {
        const t = fitIndexed(a, b, shift);
        if (t !== null) tries.splice(candidates.length, 0, t);
    }
    for (const t of tries) if (sameShape(a, b, moved(t))) return t;
    return null;
}

// The rigid move taking each vertex of `a` onto the same one of `b` (least squares), or null.
function fitIndexed(a: Points, b: Points, shift: readonly number[]): Transform | null {
    const inFrame = (p: number[]): number[] => [p[0]! + shift[0]!, p[1]! + shift[1]!, p[2]! + shift[2]!];
    const ca = [0, 1, 2].map((k) => a.reduce((s, p) => s + p[k]!, 0) / a.length);
    const cb = [0, 1, 2].map((k) => b.reduce((s, p) => s + p[k]!, 0) / b.length);
    let dot = 0;
    let cross = 0;
    a.forEach((p, i) => {
        const [ax, az] = [p[0]! - ca[0]!, p[2]! - ca[2]!];
        const [bx, bz] = [b[i]![0]! - cb[0]!, b[i]![2]! - cb[2]!];
        dot += bx * ax + bz * az;
        cross += bx * az - bz * ax;
    });
    const angle = Math.atan2(cross, dot);
    const round = (v: readonly number[]): number[] => v.map((x) => pyRound(x, 4));
    const t: Transform = {
        translate: round([0, 1, 2].map((k) => cb[k]! - ca[k]! - shift[k]!)),
    };
    if (Math.abs(angle) > 1e-4) {
        t.rotate_y = pyRound(angle, 6);
        t.pivot = round(ca);
    }
    return worst(a, b, (p) => inFrame(movePoint(p, t))) <= FIT ? t : null;
}

const classOfT = (n: Struct, names: ReadonlyMap<number, string>): string => {
    const c = nodes.classOf(n, names);
    if (c) return c;
    // (THUGPRO's compressed components - ncomp_Waypoint_208 - not in its level file: the class from the name)
    for (const [k, v] of n.entries) {
        const m = k === null && v instanceof Name ? /^ncomp_([a-z]+)_\d+$/i.exec(names.get(v.value) ?? "") : null;
        if (m) return m[1]!.toLowerCase();
    }
    return "";
};

export function extractZone(zone: string, src: Sources): ZoneExtract {
    const names = src.names;
    const low = zone.toLowerCase();
    const prefix = `${low}_`;
    const norm = (text: string): string =>
        text.toLowerCase().startsWith(prefix) && MULTIPLAYER_NAME.test(text.slice(prefix.length))
            ? text.slice(prefix.length)
            : text;
    const out: ZoneExtract = {
        node_edits: { moved: [], renamed: [], geometry: {} },
        node_flags: { nodes: [], clusters: {}, imported: [] },
        removed: { nodes: [], sectors: [], collision: [] },
        added: { nodes: [] },
        collision: { faces: {} },
        awaiting: [],
        notes: [],
        counts: {},
    };
    const count = (what: string, n = 1): void => {
        out.counts[what] = (out.counts[what] ?? 0) + n;
    };
    const text = (v: number): string => nameOfValue(v, names);

    // --- THAW: the net archive (what we convert) and the full one
    const netPak = zonePak(src.thawFolder, zone);
    const fullPak = findPath(src.thawFolder, "Game", "data", "worlds", "worldzones", zone, `${zone}.pak.wpc`);
    if (netPak === null || fullPak === null) pyExit(`no THAW ${zone} archives in ${src.thawFolder}`);
    const netFiles = zoneFiles(netPak);
    const net = zoneArray(netFiles, zone, names);
    const shift = frameShift(zone, net, names);
    multiplayerNames(net, zone, names);
    const netGeom = thaw.readGeom(netFiles.get(".geom")!, zone);
    const netCol = thaw.readCol(netFiles.get(".col")!, zone);
    const fullFiles = zoneFiles(fullPak);
    const fullGeom = thaw.readGeom(fullFiles.get(".geom")!, zone);
    const fullCol = thaw.readCol(fullFiles.get(".col")!, zone);
    const fullNames = new Set<string>(); // every node name of THAW's full archive (with the _sfx sub-zone), ours
    const levelScripts = new Set<number>();
    const scriptRefs = new Set<number>(); // names THAW's level scripts use (their inline structures too)
    const nameKey = checksum("Name");
    const walkInto =
        (into: Set<number>) =>
        (v: QbValue): void => {
            if (v instanceof Name) into.add(v.value);
            else if (Array.isArray(v)) v.forEach(walkInto(into));
            else if (v instanceof Struct) {
                for (const [k, x] of v.entries) if (k === null || k.value !== nameKey) walkInto(into)(x);
            }
        };
    const readScripts = (q: thaw.QbItems): void => {
        for (const [s, body] of q.scripts) {
            levelScripts.add(s);
            for (const line of body) {
                for (const [t, p] of line) {
                    if (t === 0x16) scriptRefs.add(p.readUInt32LE(0));
                    else if (t === 0x4a) {
                        try {
                            walkInto(scriptRefs)(thaw.inlineStruct(p, names));
                        } catch {
                            // (an inline structure that doesn't read: its names left out)
                        }
                    }
                }
            }
        }
    };
    for (const [entry, data] of thaw.pakFiles(fullPak)) {
        if (![".qb", ".nqb", ".sqb"].includes(entry.extension) || !entry.path) continue;
        const q = new thaw.QbItems(data, entry.path, names);
        for (const [k, v] of q.globals) {
            if (!Array.isArray(v) || !text(k.value).toLowerCase().endsWith("_nodearray")) continue;
            for (const n of v) {
                const nm = n instanceof Struct ? n.get("Name") : undefined;
                if (nm instanceof Name) fullNames.add(norm(text(nm.value)).toLowerCase());
            }
        }
        readScripts(q);
    }
    for (const [k, v] of netFiles) if (k.endsWith(".qb")) readScripts(new thaw.QbItems(v, k, names));
    const referenced = new Set(scriptRefs); // ... and the nodes (a kill's restart, a LOD_Name...)
    net.forEach(walkInto(referenced));
    const scriptKnown = (v: number): boolean =>
        levelScripts.has(v) || src.thawScripts.has(v) || src.thug2.scripts.has(v) || src.thug2.cfuncs.has(v);

    // --- THUGPRO
    const key = COMPAT.levelNames.get(zone.toUpperCase()) ?? low;
    const scriptsPrx = findPath(src.thugpro, "data", "pre", `${key}_scripts.prx`);
    const data = scriptsPrx === null ? null : prx.find(scriptsPrx, `levels\\${key}\\${key}.qb`);
    if (data === null) pyExit(`THUGPRO has no levels\\${key}\\${key}.qb`);
    const [theirs, theirNames] = nodeArray(data, `${key}.qb`);
    for (const [k, v] of theirNames) if (!names.has(k)) names.set(k, v);
    const tpScene = new Map<number, thug2Scene.Sector>();
    for (const [, d] of prx.entries(findPath(src.thugpro, "data", "pre", `${key}scn.prx`)!, (n) =>
        n.toLowerCase().endsWith(".scn.xbx"),
    )) {
        for (const s of thug2Scene.readScene(d, key).sectors) tpScene.set(s.checksum, s);
    }
    const tpCol = new Map<number, thug2Col.ColObject>();
    for (const [, d] of prx.entries(findPath(src.thugpro, "data", "pre", `${key}col.prx`)!, (n) =>
        n.toLowerCase().endsWith(".col.xbx"),
    )) {
        for (const o of thug2Col.readCol(d, key).objects) tpCol.set(o.checksum, o);
    }

    // --- nodes by our names
    const nameOf = (n: Struct): number | null => {
        const v = n.get("Name");
        return v instanceof Name ? v.value : null;
    };
    const ourName = (n: Struct): number | null => {
        const v = nameOf(n);
        return v === null ? null : checksum(norm(names.get(v) ?? text(v)));
    };
    const thawBy = new Map<number, Struct>();
    for (const n of net)
        if (n instanceof Struct && nameOf(n) !== null && !thawBy.has(nameOf(n)!)) thawBy.set(nameOf(n)!, n);
    const tpBy = new Map<number, Struct>();
    for (const n of theirs) {
        if (n instanceof Struct && ourName(n) !== null && !tpBy.has(ourName(n)!)) tpBy.set(ourName(n)!, n);
    }
    const tpIndexName = (i: number): string | null => {
        const n = theirs[i];
        const v = n instanceof Struct ? ourName(n) : null;
        return v === null ? null : text(v);
    };
    const posOf = (n: Struct): number[] | null => {
        const v = n.get("Pos");
        if (v instanceof Vector) return [...v.values];
        const p = n.get("Position");
        return p instanceof Vector ? [p.values[0]!, p.values[1]!, -p.values[2]!] : null;
    };
    const angOf = (n: Struct): number[] => {
        const v = n.get("Angles");
        return v instanceof Vector ? [...v.values] : [0, 0, 0];
    };
    const d3 = (a: readonly number[], b: readonly number[]): number =>
        Math.max(...[0, 1, 2].map((k) => Math.abs(a[k]! - b[k]!)));
    const inFrame = (p: readonly number[]): number[] => [0, 1, 2].map((k) => p[k]! + shift[k]!);
    const toThaw = (p: readonly number[]): number[] => [0, 1, 2].map((k) => pyRound(p[k]! - shift[k]!, 4));
    const linkNames = (n: Struct, byIndex: (i: number) => string | null): string[] | null => {
        const l = n.get("Links");
        if (!Array.isArray(l)) return null;
        return l.map((i) => (typeof i === "number" ? (byIndex(i) ?? "?") : "?"));
    };
    const thawIndexName = (i: number): string | null => {
        const n = net[i];
        const v = n instanceof Struct ? nameOf(n) : null;
        return v === null ? null : text(v);
    };
    const spawnNodes = new Set<number>(); // TP-4's: their places and types are its
    const tp4 = COMPAT.spawns.get(zone.toUpperCase());
    for (const e of tp4?.edits ?? []) if (e.node) spawnNodes.add(checksum(e.node));
    for (const o of tp4?.order ?? []) spawnNodes.add(checksum(o));
    const flagsOf = (n: Struct): Set<number> =>
        new Set(n.entries.filter(([k, v]) => k === null && v instanceof Name).map(([, v]) => (v as Name).value));
    const levelObjects = new Set<number>(); // (their scene / collision in their own space: not compared)
    for (const n of net) {
        if (n instanceof Struct && nodes.classOf(n, names) === "levelobject" && nameOf(n) !== null)
            levelObjects.add(nameOf(n)!);
    }

    const imports = new Set<number>();
    const tpCheck = new Map<string, number>(); // other keys THUGPRO changed, not applied
    const clusterTo = new Map<string, Set<string>>(); // THAW's cluster -> THUGPRO's on its nodes
    const pending: FlagEdit[] = [];
    for (const [nm, o] of thawBy) {
        const t = tpBy.get(nm);
        if (t === undefined) continue;
        const node = text(nm);
        const isSpawn = spawnNodes.has(nm);
        // TP-5
        const move: NodeMove = { node };
        const po = posOf(o);
        const pt = posOf(t);
        if (!isSpawn && po !== null && pt !== null && d3(inFrame(po), pt) > MOVED) move.pos = toThaw(pt);
        if (!isSpawn && d3(angOf(o), angOf(t)) > ANGLE) move.angles = angOf(t).map((x) => pyRound(x, 6));
        const lo = linkNames(o, thawIndexName);
        const lt = linkNames(t, tpIndexName);
        if ((lo ?? []).join(",").toLowerCase() !== (lt ?? []).join(",").toLowerCase() && !(lt ?? []).includes("?")) {
            move.links = lt ?? [];
        }
        if (Object.keys(move).length > 1) {
            out.node_edits.moved.push(move);
            count("moved");
        }
        // TP-6
        const edit: FlagEdit = { node };
        const add: string[] = [];
        const remove: string[] = [];
        const unset: string[] = [];
        const set: Record<string, JsonValue> = {};
        const fo = flagsOf(o);
        const ft = flagsOf(t);
        for (const f of APPLIED_FLAGS) {
            const c = checksum(f);
            if (fo.has(c) && !ft.has(c)) remove.push(f);
            if (!fo.has(c) && ft.has(c)) add.push(f);
        }
        const valueText = (v: QbValue | undefined): string =>
            v === undefined ? "" : JSON.stringify(encodeValue(v, names)).toLowerCase();
        for (const k of APPLIED_KEYS) {
            if (isSpawn && ["restart_types", "RestartName"].includes(k)) continue;
            const a = o.get(k);
            const b = t.get(k);
            if (valueText(a) === valueText(b)) continue;
            if (b === undefined) unset.push(k);
            else set[k] = encodeValue(b, names);
        }
        const sa = o.get("TriggerScript");
        const sb = t.get("TriggerScript");
        if (valueText(sa) !== valueText(sb)) {
            if (sb === undefined) unset.push("TriggerScript");
            else if (sb instanceof Name && scriptKnown(sb.value)) set.TriggerScript = { n: text(sb.value) };
            else {
                out.awaiting.push({
                    kind: "script",
                    what: `${node}: TriggerScript ${sa instanceof Name ? text(sa.value) : "(none)"} -> THUGPRO's ${sb instanceof Name ? text(sb.value) : "?"}`,
                    size: "1 node",
                });
            }
        }
        // other keys (not applied: tallied)
        const keysOf = (n: Struct): Map<number, string> => {
            const m = new Map<number, string>();
            for (const [k, v] of n.entries) if (k !== null) m.set(k.value, valueText(v));
            return m;
        };
        const ko = keysOf(o);
        const kt = keysOf(t);
        const skip = new Set([...POSITION_KEYS, ...APPLIED_KEYS, "TriggerScript"].map((x) => checksum(x)));
        for (const k of new Set([...ko.keys(), ...kt.keys()])) {
            if (skip.has(k) || ko.get(k) === kt.get(k)) continue;
            const lowKey = text(k).toLowerCase();
            if (SYSTEMATIC.has(lowKey) || /^(vcwibble|uvwibble)/.test(lowKey)) continue;
            const label = `${nodes.classOf(o, names)} ${text(k).toLowerCase()}`;
            tpCheck.set(label, (tpCheck.get(label) ?? 0) + 1);
        }
        // (a cluster renamed the same way on every node: one entry in `clusters`, decided below)
        const ca = o.get("Cluster");
        const cb = t.get("Cluster");
        if (ca instanceof Name) {
            const old = text(ca.value);
            const now = unset.includes("Cluster")
                ? "?"
                : set.Cluster === undefined
                  ? old
                  : cb instanceof Name
                    ? text(cb.value)
                    : "?";
            if (!clusterTo.has(old)) clusterTo.set(old, new Set());
            clusterTo.get(old)!.add(now);
        }
        if (add.length) edit.add = add;
        if (remove.length) edit.remove = remove;
        if (Object.keys(set).length) edit.set = set;
        if (unset.length) edit.unset = unset;
        pending.push(edit);
        // the pieces of geometry THUGPRO shows in network games that the net archive hasn't
        if (remove.includes("AbsentInNetGames") && ["levelgeometry", "levelobject"].includes(nodes.classOf(o, names))) {
            const inNet =
                netGeom.sectors.some((s) => s.checksum === nm) || netCol.objects.some((c) => c.checksum === nm);
            if (!inNet) imports.add(nm);
        }
    }
    // clusters THUGPRO renamed on all their nodes (DN / JA / SZ: the zone prefixed - likely its toolchain): a map
    for (const [old, now] of clusterTo) {
        const only = [...now][0]!;
        if (now.size !== 1 || only === old || only === "?") continue;
        if (scriptRefs.has(nameValue(old))) {
            out.notes.push(`cluster not renamed (THAW's scripts name it): ${old} -> ${only}`);
            continue;
        }
        out.node_flags.clusters[old] = only;
    }
    for (const edit of pending) {
        const c = edit.set?.Cluster;
        const thawNode = thawBy.get(checksum(edit.node))!;
        const old = thawNode.get("Cluster");
        if (
            c !== undefined &&
            old instanceof Name &&
            (out.node_flags.clusters[text(old.value)] !== undefined || scriptRefs.has(old.value))
        ) {
            delete edit.set!.Cluster;
            if (!Object.keys(edit.set!).length) delete edit.set;
        }
        if (Object.keys(edit).length > 1) {
            out.node_flags.nodes.push(edit);
            count("flags");
        }
    }
    if (Object.keys(out.node_flags.clusters).length)
        count("clusters renamed", Object.keys(out.node_flags.clusters).length);
    // renames / removed / added
    const thawOnly = [...thawBy.keys()].filter((k) => !tpBy.has(k));
    const tpOnly = [...tpBy.keys()].filter((k) => !thawBy.has(k));
    const renamedFrom = new Set<number>();
    const renamedTo = new Set<number>();
    for (const k of tpOnly) {
        const t = tpBy.get(k)!;
        const pt = posOf(t);
        if (pt === null) continue;
        const hit = thawOnly.find((r) => {
            if (renamedFrom.has(r)) return false;
            const o = thawBy.get(r)!;
            const po = posOf(o);
            return po !== null && nodes.classOf(o, names) === classOfT(t, names) && d3(inFrame(po), pt) < MOVED;
        });
        if (hit === undefined) continue;
        renamedFrom.add(hit);
        renamedTo.add(k);
        if (referenced.has(hit)) {
            out.notes.push(`not renamed (THAW's scripts / nodes name it): ${text(hit)} -> ${text(k)}`);
            count("rename skipped");
            continue;
        }
        out.node_edits.renamed.push({ node: text(hit), to: text(k) });
        count("renamed");
    }
    for (const k of thawOnly) {
        if (renamedFrom.has(k)) continue;
        out.removed.nodes.push(text(k));
        count("removed nodes");
        if (referenced.has(k)) out.notes.push(`removed although THAW's scripts name it: ${text(k)}`);
    }
    const fromFull: string[] = [];
    for (const k of tpOnly) {
        if (renamedTo.has(k)) continue;
        const t = tpBy.get(k)!;
        if (fullNames.has(text(k).toLowerCase())) {
            fromFull.push(text(k));
            continue;
        }
        const cls = classOfT(t, names);
        const types = t.get("restart_types");
        if (
            cls === "restart" &&
            Array.isArray(types) &&
            types.some((x) => x instanceof Name && nodes.NET_RESTART_TYPES.has(text(x.value).toLowerCase()))
        ) {
            continue; // (TP-4's)
        }
        const script = t.get("TriggerScript");
        const model = t.get("model");
        const why: string[] = [];
        if (!ADDED_CLASSES.has(cls)) why.push(`class ${cls || "?"}`);
        if (script instanceof Name && !scriptKnown(script.value)) why.push(`THUGPRO's script ${text(script.value)}`);
        if (typeof model === "string" && !src.assets.modelExists(model.replace(/\.mdl$/i, ""))) {
            why.push(`model ${model} (not THUG2's)`);
        }
        if (cls === "gameobject" && typeof model !== "string") why.push("a game object of THUGPRO's");
        if (why.length) {
            out.awaiting.push({
                kind: "node",
                what: `added ${cls || "node"} ${text(k)}: ${why.join(", ")}`,
                size: "1 node",
            });
            count("added awaiting");
            continue;
        }
        const entries: [string | null, JsonValue][] = [];
        let hasClass = false;
        for (const [ek, ev] of t.entries) {
            if (ek === null && ev instanceof Name && /^ncomp_/i.test(names.get(ev.value) ?? "")) continue;
            if (ek !== null && ["links"].includes(text(ek.value).toLowerCase())) continue;
            if (ek !== null && text(ek.value).toLowerCase() === "class") hasClass = true;
            let v: QbValue = ev;
            if (ek !== null && text(ek.value).toLowerCase() === "pos" && ev instanceof Vector)
                v = new Vector("vector", toThaw(ev.values));
            if (ek !== null && text(ek.value).toLowerCase() === "name") v = new Name(k);
            entries.push([ek === null ? null : text(ek.value), encodeValue(v, names)]);
        }
        if (!hasClass) entries.push(["Class", { n: cls.charAt(0).toUpperCase() + cls.slice(1) }]);
        const links = linkNames(t, tpIndexName);
        const added: AddedNode = { entries };
        if (links?.length) added.links = links.filter((l) => l !== "?");
        out.added.nodes.push(added);
        count(`added ${cls}`);
    }
    // links of added nodes to nodes not added: dropped (awaiting ones)
    const present = new Set([...thawBy.keys()].filter((k) => !thawOnly.includes(k) || renamedFrom.has(k)).map(text));
    for (const r of out.node_edits.renamed) present.add(r.to);
    for (const a of out.added.nodes) {
        const nm = a.entries.find(([k]) => k?.toLowerCase() === "name")?.[1] as { n: string } | undefined;
        if (nm) present.add(nm.n);
    }
    const lower = new Set([...present].map((x) => x.toLowerCase()));
    for (const a of out.added.nodes) if (a.links) a.links = a.links.filter((l) => lower.has(l.toLowerCase()));
    if (fromFull.length) {
        out.notes.push(
            `${fromFull.length} node(s) THUGPRO has from THAW's full archive (not in the net archive we convert - not hand edits): ${fromFull.slice(0, 6).join(", ")}${fromFull.length > 6 ? "..." : ""}`,
        );
        count("from the full archive", fromFull.length);
    }

    // --- geometry
    const meshPoints = (s: thaw.GeomSector): Points =>
        sectorPoints(s.meshes.map((m) => ({ stride: m.stride, vertexCount: m.vertexCount, data: m.vertices })));
    const tpPoints = (s: thug2Scene.Sector): Points =>
        sectorPoints(s.meshes.map((m) => ({ stride: m.stride, vertexCount: m.vertexCount, data: m.buffers[0]![1] })));
    const fullSector = new Map(fullGeom.sectors.map((s) => [s.checksum, s] as const));
    const fullObject = new Map(fullCol.objects.map((o) => [o.checksum, o] as const));
    const ourSectors = new Map(netGeom.sectors.map((s) => [s.checksum, s] as const));
    const ourObjects = new Map(netCol.objects.map((o) => [o.checksum, o] as const));
    for (const c of imports) {
        let any = false;
        if (tpScene.has(c) && fullSector.has(c) && !ourSectors.has(c)) {
            ourSectors.set(c, fullSector.get(c)!);
            any = true;
        }
        if (tpCol.has(c) && fullObject.has(c) && !ourObjects.has(c)) {
            ourObjects.set(c, fullObject.get(c)!);
            any = true;
        }
        if (any) {
            out.node_flags.imported.push(text(c));
            count("imported");
        }
    }
    const isSfx = (c: number): boolean => /_sfx_/i.test(text(c));
    const mesh = (what: string, c: number, ours: number, theirsCount: number): void => {
        out.awaiting.push({
            kind: "mesh",
            what: `${what} ${text(c)} reshaped by THUGPRO`,
            size: `${ours} -> ${theirsCount} vertices`,
        });
        count(`${what} reshaped`);
    };
    // the node's own move as a candidate: its place, turned by its y angle's change about it
    const nodeMoves = (c: number): Transform[] => {
        const o = thawBy.get(c);
        const t = tpBy.get(c);
        const po = o && posOf(o);
        const pt = t && posOf(t);
        if (!o || !t || !po || !pt) return [];
        const translate = toThaw(pt).map((x, k) => pyRound(x - po[k]!, 4));
        const turn = angOf(t)[1]! - angOf(o)[1]!;
        const out: Transform[] = [{ translate }];
        if (Math.abs(turn) > ANGLE) {
            for (const sign of [1, -1])
                out.unshift({ translate, rotate_y: pyRound(sign * turn, 6), pivot: po.map((x) => pyRound(x, 4)) });
        }
        return out;
    };
    const transforms = new Map<number, Transform>();
    for (const [c, s] of ourSectors) {
        if (levelObjects.has(c)) continue;
        const p = tpScene.get(c);
        if (p === undefined) {
            out.removed.sectors.push(text(c));
            count("removed sectors");
            continue;
        }
        const a = meshPoints(s);
        const b = tpPoints(p);
        const fit = fitRigid(a, b, shift, nodeMoves(c));
        if (fit === null) mesh("scene sector", c, a.length, b.length);
        else if (fit !== "same") transforms.set(c, { ...fit, scene: true, collision: false });
    }
    for (const [c, o] of ourObjects) {
        if (levelObjects.has(c)) continue;
        const p = tpCol.get(c);
        if (p === undefined) {
            if (!isSfx(c)) {
                out.removed.collision.push(text(c));
                count("removed collision");
            }
            continue;
        }
        const known = transforms.get(c);
        const fit = fitRigid(o.verts, p.verts, shift, [...(known ? [known] : []), ...nodeMoves(c)]);
        if (fit === null) {
            mesh("collision object", c, o.verts.length, p.verts.length);
            continue;
        }
        if (fit !== "same") {
            const t = transforms.get(c);
            if (t === undefined) transforms.set(c, { ...fit, scene: false, collision: true });
            else if (fit === t) t.collision = true;
            else mesh("collision object (moved unlike its sector)", c, o.verts.length, p.verts.length);
        }
        // TP-9: the faces, when they are THAW's
        const same =
            o.faces.length === p.faces.length &&
            o.faces.every((f, i) => f[2] === p.faces[i]![2] && f[3] === p.faces[i]![3] && f[4] === p.faces[i]![4]);
        if (!same) {
            if (fit === "same" || fit !== null) {
                out.awaiting.push({
                    kind: "mesh",
                    what: `collision object ${text(c)}: faces rebuilt by THUGPRO`,
                    size: `${o.faces.length} -> ${p.faces.length} faces`,
                });
                count("collision faces rebuilt");
            }
            continue;
        }
        const edits: [number, number, number, number][] = [];
        o.faces.forEach((f, i) => {
            const g = p.faces[i]!;
            const a = f[0] & FACE_KEEP;
            const b = g[0] & FACE_KEEP;
            if (a !== b || f[1] !== g[1]) edits.push([i, b & ~a, a & ~b, f[1] !== g[1] ? g[1] : -1]);
        });
        if (edits.length) {
            out.collision.faces[text(c)] = { faces: o.faces.length, edits };
            count("collision faces", edits.length);
        }
    }
    for (const [c, t] of transforms) {
        const entry: Transform = { translate: t.translate };
        if (t.rotate_y !== undefined) {
            entry.rotate_y = t.rotate_y;
            entry.pivot = t.pivot!;
        }
        if (!t.scene) entry.scene = false;
        if (!t.collision) entry.collision = false;
        out.node_edits.geometry[text(c)] = entry;
        count("geometry moved");
    }
    // pieces THUGPRO has that THAW hasn't: its own
    for (const [c, s] of tpScene) {
        if (ourSectors.has(c) || fullSector.has(c) || isSfx(c)) continue;
        out.awaiting.push({
            kind: "new geometry",
            what: `scene sector ${text(c)} (THUGPRO's)`,
            size: `${tpPoints(s).length} vertices`,
        });
        count("THUGPRO's sectors");
    }
    for (const [c, o] of tpCol) {
        if (ourObjects.has(c) || fullObject.has(c) || isSfx(c)) continue;
        const collidable = o.faces.filter((f) => !(f[0] & thug2Col.FACE_NON_COLLIDABLE)).length;
        out.awaiting.push({
            kind: "new geometry",
            what: `collision object ${text(c)} (THUGPRO's)`,
            size: `${o.faces.length} faces (${collidable} collidable)`,
        });
        count("THUGPRO's collision objects");
    }
    const fullOnly = [...tpScene.keys()].filter((c) => !ourSectors.has(c) && fullSector.has(c)).length;
    if (fullOnly) {
        out.notes.push(
            `${fullOnly} scene sector(s) THUGPRO has from THAW's full archive (the story's pieces; absent in network games on both sides - not hand edits)`,
        );
    }
    for (const [k, n] of [...tpCheck].sort((a, b) => b[1] - a[1])) {
        out.notes.push(
            `not applied (a value our port doesn't carry - it makes these nodes its own way): ${k} on ${n} node(s)`,
        );
    }
    return out;
}
