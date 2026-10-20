// NOT ORIGINAL - an inventory of THUGPRO's own edits to its THPS4 level ports (docs/pending/thps4-thugpro.md
// "THUGPRO hand edits: inventory"): its node arrays, level scripts and collision against THPS4's own files (and our
// converted collision, which is THPS4's). Read only on both installs; this is the list the user decides on. What of it
// is node data is in the compatibility profile (TP-22 ... TP-29: thugpro_edits.ts, extracted by thugpro_compare.ts
// --extract-thugpro); with --thug2 each level's line is followed by how many of those edits our converted level there
// agrees with, per category ("ours with the profile").
//
//   node tools/thps4_levels/thugpro_inventory.ts [levels...] [--thps4 C:\Games\THPS4] [--thugpro <dir>]
//        [--thug2 C:\Games\THUG2-2] [--json out]
//
// Nodes are matched by name. Per level:
//   moved      shared nodes more than 0.1 unit apart (in THUG2's axes: THPS4's legacy Position has z negated)
//   rotated    shared nodes whose Angles differ (by more than 0.001 rad)
//   renamed    a THPS4 node missing in THUGPRO's array where THUGPRO has a node of the same class at the same place
//   added      THUGPRO's nodes with a name THPS4's array hasn't (and not a rename)
//   removed    THPS4's nodes THUGPRO's array hasn't (and not a rename)
//   changed    shared nodes with another Class / Type / restart_types / TriggerScript / TerrainType / Links (by the
//              linked nodes' names) / flags
// The spawns (restarts a network game uses - Multiplayer, Horse, CTF, Team types - on either side) are listed apart.
// Scripts: the level's script names on both sides, and for shared ones whether their statements' commands differ.
// Collision: per object (by name) against our converted collision - added, removed, other face count, other bounds.

import * as fs from "node:fs";
import { parseArgs } from "node:util";
import * as prx from "../level_port/prx.ts";
import * as qb from "../level_port/qb.ts";
import { Name, type QbValue, Struct, Vector } from "../level_port/qb.ts";
import * as thug2Col from "../level_port/thug2_col.ts";
import { hex8, isMain, print, pyExit, pyJoin, pyJsonDumps, runMain, writeTextPy } from "../lib/py.ts";
import { findPath } from "../thaw_levels/thaw_levels.ts";
import { maxDiff, nodeArray, position } from "../thaw_levels/thugpro_compare.ts";
import { COMPAT, editsOf, LEVELS, levelName, loadName, STAGING } from "./thps4_levels.ts";
import { type Check, checkDuplicates, checkEdits } from "./thugpro_edits.ts";
import { checkNew } from "./thugpro_new.ts";

const DEFAULT_THUGPRO = pyJoin(process.env.LOCALAPPDATA ?? "", "THUG Pro");
const TOLERANCE = 0.1;
const ANGLE_TOLERANCE = 0.001;
export const NET_TYPES: ReadonlySet<string> = new Set(["multiplayer", "horse", "ctf", "ctf_1", "ctf_2", "team"]);
const EXAMPLES = 12;

type Names = Map<number, string>;

const text = (v: QbValue | undefined, names: Names): string =>
    v instanceof Name ? (names.get(v.value) ?? hex8(v.value)) : "";

interface Side {
    array: QbValue[];
    names: Names;
    byName: Map<number, Struct>;
    index: Map<Struct, number>;
}

function side(array: QbValue[], names: Names): Side {
    const byName = new Map<number, Struct>();
    const index = new Map<Struct, number>();
    array.forEach((n, i) => {
        if (!(n instanceof Struct)) return;
        index.set(n, i);
        const nm = n.get("Name");
        if (nm instanceof Name && !byName.has(nm.value)) byName.set(nm.value, n);
    });
    return { array, names, byName, index };
}

const cls = (n: Struct, s: Side): string => text(n.get("Class"), s.names).toLowerCase();
const nameText = (n: Struct, s: Side): string => text(n.get("Name"), s.names);

function angles(n: Struct): number[] {
    const a = n.get("Angles");
    return a instanceof Vector ? [...a.values] : [0, 0, 0];
}

function flags(n: Struct, s: Side): Set<string> {
    const out = new Set<string>();
    for (const [k, v] of n.entries) if (k === null && v instanceof Name) out.add(text(v, s.names).toLowerCase());
    return out;
}

function restartTypes(n: Struct, s: Side): string[] {
    const t = n.get("restart_types");
    const list = Array.isArray(t) ? t.map((x) => text(x, s.names).toLowerCase()) : [];
    if (!list.length && cls(n, s) === "restart") list.push(text(n.get("Type"), s.names).toLowerCase());
    return list.sort();
}

function links(n: Struct, s: Side): string[] {
    const l = n.get("Links");
    if (!Array.isArray(l)) return [];
    return l
        .map((i) => {
            const t = typeof i === "number" ? s.array[i] : undefined;
            return t instanceof Struct ? nameText(t, s).toLowerCase() : `#${String(i)}`;
        })
        .sort();
}

const isNetSpawn = (n: Struct, s: Side): boolean =>
    cls(n, s) === "restart" && restartTypes(n, s).some((t) => NET_TYPES.has(t));

// THUGPRO's toolchain changes some keys on every node of a kind (models' lod distances, skeleton names...): reported
// as counts, not per node. These are compared one by one.
const COMPARED = ["Class", "Type", "TriggerScript", "TerrainType"];

interface Change {
    name: string;
    cls: string;
    what: string[];
}

type Bucket = { count: number; examples: string[] };
const bucket = (): Bucket => ({ count: 0, examples: [] });
function add(b: Bucket, example: string): void {
    b.count += 1;
    if (b.examples.length < EXAMPLES) b.examples.push(example);
}

export interface Inventory {
    base: string;
    thugpro_level: string;
    nodes: Record<string, unknown>;
    spawns: Record<string, unknown>;
    scripts: Record<string, unknown>;
    collision: Record<string, unknown>;
    thugpro_files: string[];
    thugpro_extras?: Record<string, unknown>;
    applied?: Check[] | null;
    error?: string;
}

function byClass(items: [string, string][]): Record<string, Bucket> {
    const out: Record<string, Bucket> = {};
    for (const [c, ex] of items) {
        out[c] ??= bucket();
        add(out[c]!, ex);
    }
    return Object.fromEntries(Object.entries(out).sort((a, b) => b[1].count - a[1].count));
}

function scriptCommands(q: qb.QbFile, script: number): string[] {
    return q.calls(script).map(([c]) => (q.names.get(c) ?? hex8(c)).toLowerCase());
}

export function inventory(base: string, thps4Dir: string, thugpro: string, thug2: string): Inventory {
    const key = loadName(base).toLowerCase();
    const out: Inventory = {
        base,
        thugpro_level: key,
        nodes: {},
        spawns: {},
        scripts: {},
        collision: {},
        thugpro_files: [],
    };
    const scriptsPrx = findPath(thugpro, "data", "pre", `${key}_scripts.prx`);
    const folder = findPath(thps4Dir, "Game", "data", "levels", base);
    if (scriptsPrx === null || folder === null) {
        out.error = scriptsPrx === null ? `THUGPRO has no ${key}_scripts.prx` : `no THPS4 ${base}`;
        return out;
    }
    // THUGPRO's level files
    const files = new Map<string, Buffer>();
    for (const [n, data] of prx.entries(scriptsPrx)) files.set(n.toLowerCase(), data);
    out.thugpro_files = [...files.keys()].filter((n) => n.startsWith(`levels\\${key}\\`));
    const tpQb = files.get(`levels\\${key}\\${key}.qb`);
    if (tpQb === undefined) pyExit(`${scriptsPrx}: no levels\\${key}\\${key}.qb`);
    const [tpArray, tpNames] = nodeArray(tpQb, `${key}.qb`);
    // THPS4's
    const ownFile = findPath(folder, `${base}.qb`)!;
    const ownQ = new qb.QbFile(fs.readFileSync(ownFile), ownFile);
    const ownArray = ownQ.get("NodeArray") as QbValue[];
    const ownScriptsFile = fs.readdirSync(folder).find((f) => f.toLowerCase() === `${base.toLowerCase()}_scripts.qb`);
    const ownScriptsQ = ownScriptsFile
        ? new qb.QbFile(fs.readFileSync(pyJoin(folder, ownScriptsFile)), ownScriptsFile)
        : null;
    const ownNames: Names = new Map(ownQ.names);
    if (ownScriptsQ) for (const [k, v] of ownScriptsQ.names) ownNames.set(k, v);
    for (const [k, v] of tpNames) if (!ownNames.has(k)) ownNames.set(k, v);
    for (const [k, v] of ownNames) if (!tpNames.has(k)) tpNames.set(k, v);
    const own = side(ownArray, ownNames);
    const tp = side(tpArray, tpNames);

    // --- nodes
    const moved: [string, string][] = [];
    const rotated: [string, string][] = [];
    const changed: Change[] = [];
    const keyCounts = new Map<string, number>();
    const flagCounts = new Map<string, number>();
    const shapeFlags = new Map<string, string[]>(); // (rails, geometry, objects, restarts: "class +flag" -> names)
    const removedNodes: Struct[] = [];
    for (const [nm, n] of own.byName) {
        const t = tp.byName.get(nm);
        if (t === undefined) {
            removedNodes.push(n);
            continue;
        }
        const label = nameText(n, own);
        const c = cls(n, own);
        const pa = position(n);
        const pb = position(t);
        if (pa && pb && maxDiff(pa, pb) > TOLERANCE) moved.push([c, `${label} (${maxDiff(pa, pb).toFixed(1)})`]);
        if (maxDiff(angles(n), angles(t)) > ANGLE_TOLERANCE) rotated.push([c, label]);
        const what: string[] = [];
        for (const k of COMPARED) {
            const a = text(n.get(k), own.names).toLowerCase();
            const b = text(t.get(k), tp.names).toLowerCase();
            if (a !== b) what.push(`${k} ${a || "-"} -> ${b || "-"}`);
        }
        if (c === "restart" && restartTypes(n, own).join() !== restartTypes(t, tp).join()) {
            what.push(`restart_types [${restartTypes(n, own).join(", ")}] -> [${restartTypes(t, tp).join(", ")}]`);
        }
        if (links(n, own).join() !== links(t, tp).join()) what.push("Links");
        const fa = flags(n, own);
        const fb = flags(t, tp);
        for (const f of fb) if (!fa.has(f)) flagCounts.set(`+${f}`, (flagCounts.get(`+${f}`) ?? 0) + 1);
        for (const f of fa) if (!fb.has(f)) flagCounts.set(`-${f}`, (flagCounts.get(`-${f}`) ?? 0) + 1);
        const fdiff = [
            ...[...fb].filter((f) => !fa.has(f)).map((f) => `+${f}`),
            ...[...fa].filter((f) => !fb.has(f)).map((f) => `-${f}`),
        ];
        if (fdiff.length && ["restart", "railnode", "levelgeometry"].includes(c)) what.push(`flags ${fdiff.join(" ")}`);
        if (["restart", "railnode", "levelgeometry", "levelobject"].includes(c)) {
            for (const f of fdiff) {
                const k = `${c} ${f}`;
                if (!shapeFlags.has(k)) shapeFlags.set(k, []);
                shapeFlags.get(k)!.push(label);
            }
        }
        // keys present on one side only (the toolchain's systematic ones show as big counts)
        const ka = new Set(n.entries.filter(([k]) => k !== null).map(([k]) => text(k!, own.names).toLowerCase()));
        const kb = new Set(t.entries.filter(([k]) => k !== null).map(([k]) => text(k!, tp.names).toLowerCase()));
        for (const k of kb) if (!ka.has(k)) keyCounts.set(`+${k}`, (keyCounts.get(`+${k}`) ?? 0) + 1);
        for (const k of ka) if (!kb.has(k)) keyCounts.set(`-${k}`, (keyCounts.get(`-${k}`) ?? 0) + 1);
        if (what.length) changed.push({ name: label, cls: c, what });
    }
    // renames: a removed THPS4 node and an added THUGPRO node of the same class at the same place
    const addedNodes = tpArray.filter((n): n is Struct => {
        if (!(n instanceof Struct)) return false;
        const nm = n.get("Name");
        return !(nm instanceof Name) || !own.byName.has(nm.value);
    });
    const renamed: [string, string][] = [];
    const renamedOwn = new Set<Struct>();
    const renamedTp = new Set<Struct>();
    for (const n of removedNodes) {
        const p = position(n);
        if (p === null) continue;
        const match = addedNodes.find(
            (t) =>
                !renamedTp.has(t) && cls(t, tp) === cls(n, own) && position(t) && maxDiff(p, position(t)!) <= TOLERANCE,
        );
        if (match) {
            renamedOwn.add(n);
            renamedTp.add(match);
            renamed.push([cls(n, own), `${nameText(n, own)} -> ${nameText(match, tp)}`]);
        }
    }
    const kind = (n: Struct, s: Side): string => {
        const t = text(n.get("Type"), s.names);
        return t ? `${cls(n, s)}/${t.toLowerCase()}` : cls(n, s) || "?";
    };
    const changedBy: Record<string, Bucket> = {};
    for (const c of changed) {
        changedBy[c.cls] ??= bucket();
        add(changedBy[c.cls]!, `${c.name}: ${c.what.join("; ")}`);
    }
    out.nodes = {
        thps4: ownArray.length,
        thugpro: tpArray.length,
        moved: byClass(moved),
        rotated: byClass(rotated),
        renamed: byClass(renamed),
        added: byClass(addedNodes.filter((n) => !renamedTp.has(n)).map((n) => [kind(n, tp), nameText(n, tp)])),
        removed: byClass(removedNodes.filter((n) => !renamedOwn.has(n)).map((n) => [kind(n, own), nameText(n, own)])),
        changed: changedBy,
        flags_changed: Object.fromEntries([...flagCounts].sort((a, b) => b[1] - a[1])),
        shape_flags_changed: Object.fromEntries(
            [...shapeFlags].map(([k, v]) => [k, { count: v.length, examples: v.slice(0, EXAMPLES * 2) }]),
        ),
        keys_changed: Object.fromEntries([...keyCounts].sort((a, b) => b[1] - a[1])),
    };

    // --- spawns (restarts a network game uses, on either side)
    const spawn = (n: Struct, s: Side): Record<string, unknown> => ({
        name: nameText(n, s),
        pos: position(n)?.map((v) => Math.round(v * 1000) / 1000) ?? null,
        angles: angles(n).map((v) => Math.round(v * 10000) / 10000),
        types: restartTypes(n, s),
    });
    const spawns: Record<string, unknown>[] = [];
    for (const [nm, n] of own.byName) {
        const t = tp.byName.get(nm);
        const ownNet = cls(n, own) === "restart" && isNetSpawn(n, own);
        const tpNet = t !== undefined && isNetSpawn(t, tp);
        if (!ownNet && !tpNet) continue;
        const notes: string[] = [];
        if (t === undefined) notes.push(renamedOwn.has(n) ? "renamed" : "removed");
        else {
            const pa = position(n)!;
            const pb = position(t)!;
            if (maxDiff(pa, pb) > TOLERANCE) notes.push(`moved ${maxDiff(pa, pb).toFixed(1)}`);
            if (maxDiff(angles(n), angles(t)) > ANGLE_TOLERANCE) notes.push("rotated");
            if (restartTypes(n, own).join() !== restartTypes(t, tp).join()) notes.push("types");
            if (cls(t, tp) !== "restart") notes.push(`class -> ${cls(t, tp)}`);
        }
        if (notes.length)
            spawns.push({ change: notes.join(", "), thps4: spawn(n, own), thugpro: t ? spawn(t, tp) : null });
    }
    for (const t of addedNodes) {
        if (isNetSpawn(t, tp))
            spawns.push({
                change: renamedTp.has(t) ? "renamed (new name)" : "added",
                thps4: null,
                thugpro: spawn(t, tp),
            });
    }
    const goalRestarts = moved.filter(
        ([c, ex]) =>
            c === "restart" && !spawns.some((s) => (s.thps4 as { name?: string } | null)?.name === ex.split(" ")[0]),
    );
    out.spawns = { changed: spawns, other_restarts_moved: goalRestarts.map(([, ex]) => ex) };

    // --- THUGPRO's own level files (<level>_thugpro.qb...): what they add at run time (LevelChanges: props, banners,
    // proximity triggers, teleports) - not in the node array
    const extras: Record<string, unknown> = {};
    for (const [n, data] of files) {
        if (!n.startsWith(`levels\\${key}\\`) || !n.endsWith("_thugpro.qb")) continue;
        try {
            const q = new qb.QbFile(data, n, true);
            for (const [k, v] of q.globals) {
                if (!Array.isArray(v)) continue;
                const kinds: Record<string, number> = {};
                for (const e of v) {
                    if (!(e instanceof Struct)) continue;
                    const type = e.get("_type");
                    const label = e.hasFlag("ProximObject")
                        ? "proximity trigger"
                        : type instanceof Name
                          ? text(type, q.names).toLowerCase()
                          : typeof e.get("model") === "string"
                            ? `model ${String(e.get("model"))}`
                            : "other";
                    kinds[label] = (kinds[label] ?? 0) + 1;
                }
                extras[q.names.get(k.value) ?? hex8(k.value)] = kinds;
            }
            extras[`${n} scripts`] = [...q.scripts.keys()].map((s) => q.names.get(s) ?? hex8(s));
        } catch (e) {
            extras[n] = `not read: ${(e as Error).message}`;
        }
    }
    out.thugpro_extras = extras;

    // --- scripts
    const tpScripts = new Map<number, qb.QbFile>();
    for (const [n, data] of files) {
        if (!n.startsWith(`levels\\${key}\\`) || !n.endsWith(".qb")) continue;
        try {
            const q = new qb.QbFile(data, n, true);
            for (const s of q.scripts.keys()) tpScripts.set(s, q);
        } catch {
            // (a file our reader can't parse: its scripts aren't compared)
        }
    }
    const ownScripts = new Map<number, qb.QbFile>();
    for (const q of [ownQ, ...(ownScriptsQ ? [ownScriptsQ] : [])])
        for (const s of q.scripts.keys()) ownScripts.set(s, q);
    const sname = (s: number): string => ownNames.get(s) ?? tpNames.get(s) ?? hex8(s);
    const addedScripts = [...tpScripts.keys()].filter((s) => !ownScripts.has(s)).map(sname);
    const removedScripts = [...ownScripts.keys()].filter((s) => !tpScripts.has(s)).map(sname);
    const changedScripts: string[] = [];
    for (const [s, q] of ownScripts) {
        const t = tpScripts.get(s);
        if (t === undefined) continue;
        if (scriptCommands(q, s).join() !== scriptCommands(t, s).join()) changedScripts.push(sname(s));
    }
    out.scripts = {
        thps4: ownScripts.size,
        thugpro: tpScripts.size,
        added: { count: addedScripts.length, examples: addedScripts.slice(0, EXAMPLES) },
        removed: { count: removedScripts.length, examples: removedScripts.slice(0, EXAMPLES) },
        changed: { count: changedScripts.length, examples: changedScripts.slice(0, EXAMPLES) },
    };

    // --- collision: THUGPRO's against ours (THPS4's, converted)
    const mine = levelName(base);
    const ourCol = pyJoin(thug2, STAGING, "levels", mine, `${mine}.col.xbx`);
    const tpColPrx = findPath(thugpro, "data", "pre", `${key}col.prx`);
    if (fs.existsSync(ourCol) && tpColPrx) {
        try {
            const theirs = [...prx.entries(tpColPrx, (n) => n.toLowerCase().endsWith(".col.xbx"))].map(([n, d]) =>
                thug2Col.readCol(d, n),
            );
            const ours = thug2Col.readCol(fs.readFileSync(ourCol), ourCol);
            const index = (c: thug2Col.ColFile[]): Map<number, thug2Col.ColObject> => {
                const m = new Map<number, thug2Col.ColObject>();
                for (const f of c) for (const o of f.objects) if (o.faces.length) m.set(o.checksum, o);
                return m;
            };
            const a = index([ours]);
            const b = index(theirs);
            const objName = (c: number): string => ownNames.get(c) ?? tpNames.get(c) ?? hex8(c);
            const collidable = (o: thug2Col.ColObject): number => o.faces.filter((f) => !(f[0] & 0x10)).length;
            const addedObj = [...b.keys()].filter((c) => !a.has(c)).map(objName);
            const removedObj = [...a.keys()].filter((c) => !b.has(c)).map(objName);
            const faceChanged: string[] = [];
            const boundsChanged: string[] = [];
            let facesOurs = 0;
            let facesTheirs = 0;
            for (const [c, o] of a) {
                facesOurs += collidable(o);
                const t = b.get(c);
                if (!t) continue;
                if (collidable(o) !== collidable(t))
                    faceChanged.push(`${objName(c)} (${collidable(o)} -> ${collidable(t)})`);
                // (the vertices as a set, to a tenth of a unit: the bounding boxes the two toolchains write differ by
                // their own padding, so they aren't compared)
                const verts = (x: thug2Col.ColObject): string =>
                    [...new Set(x.verts.map((v) => v.map((k) => Math.round(k * 10)).join(",")))].sort().join(" ");
                if (verts(o) !== verts(t)) boundsChanged.push(objName(c));
            }
            for (const o of b.values()) facesTheirs += collidable(o);
            out.collision = {
                objects: [a.size, b.size],
                collidable_faces: [facesOurs, facesTheirs],
                added: { count: addedObj.length, examples: addedObj.slice(0, EXAMPLES) },
                removed: { count: removedObj.length, examples: removedObj.slice(0, EXAMPLES) },
                face_count_changed: { count: faceChanged.length, examples: faceChanged.slice(0, EXAMPLES) },
                vertices_changed: { count: boundsChanged.length, examples: boundsChanged.slice(0, EXAMPLES) },
            };
        } catch (e) {
            if (!(e instanceof Error) || e.name !== "FormatError") throw e;
            out.collision = { error: e.message };
        }
    }
    out.applied = applied(base, thugpro, thug2);
    return out;
}

// The profile's edits of the level (TP-22 ... TP-28: tools/compat/thugpro/thps4_levels.json) against our converted
// level in <thug2>\thug2-decomp-thps4: per category, how many our node array / collision agrees with (null: not
// converted there).
export function applied(base: string, thugpro: string, thug2: string): Check[] | null {
    const mine = levelName(base);
    const ourQb = pyJoin(thug2, STAGING, "levels", mine, `${mine}.qb`);
    if (!fs.existsSync(ourQb)) return null;
    const edits = editsOf(base);
    const [array] = nodeArray(fs.readFileSync(ourQb), ourQb);
    const checks = checkEdits(edits, array);
    const ourCol = pyJoin(thug2, STAGING, "levels", mine, `${mine}.col.xbx`);
    const tpColPrx = findPath(thugpro, "data", "pre", `${loadName(base).toLowerCase()}col.prx`);
    if (edits.duplicates.length && fs.existsSync(ourCol) && tpColPrx) {
        const theirs = [...prx.entries(tpColPrx, (n) => n.toLowerCase().endsWith(".col.xbx"))].map(([n, d]) =>
            thug2Col.readCol(d, n),
        );
        checks.push(checkDuplicates(edits, thug2Col.readCol(fs.readFileSync(ourCol), ourCol), theirs));
    }
    // (THUGPRO's own content: thugpro_new.ts)
    checks.push(...checkNew(COMPAT.thugproNew, base, mine, fs.readFileSync(ourQb)));
    return checks.filter((c) => c.total);
}

export function appliedText(checks: readonly Check[]): string {
    return checks
        .map((c) => {
            const notes: string[] = [];
            if (c.absent.length) notes.push(`${c.absent.length} not in ours`);
            if (c.differing.length) notes.push(`${c.differing.length} differ: ${c.differing.slice(0, 4).join(", ")}`);
            return `${c.register} ${c.category} ${c.matching}/${c.total}${notes.length ? ` (${notes.join("; ")})` : ""}`;
        })
        .join("; ");
}

const USAGE =
    "usage: thugpro_inventory.ts [-h] [--thps4 THPS4] [--thugpro THUGPRO] [--thug2 THUG2] [--json JSON] [levels ...]";
const OPTIONS = {
    thps4: { type: "string" },
    thugpro: { type: "string" },
    thug2: { type: "string" }, // where our converted levels are (<thug2>\thug2-decomp-thps4): the collision compared
    json: { type: "string" },
} as const;

export function main(argv: readonly string[] = process.argv.slice(2)): number {
    let parsed: ReturnType<typeof parseArgs<{ options: typeof OPTIONS; allowPositionals: true }>>;
    try {
        parsed = parseArgs({ args: [...argv], options: OPTIONS, allowPositionals: true, strict: true });
    } catch (e) {
        process.stderr.write(`${USAGE}\nthugpro_inventory.ts: error: ${(e as Error).message}\n`);
        pyExit(2);
    }
    const v = parsed.values;
    const wanted = new Set(parsed.positionals.map((x) => x.toLowerCase()));
    const results: Inventory[] = [];
    for (const base of LEVELS) {
        if (wanted.size && !wanted.has(base.toLowerCase())) continue;
        const r = inventory(
            base,
            v.thps4 ?? "C:\\Games\\THPS4",
            v.thugpro ?? DEFAULT_THUGPRO,
            v.thug2 ?? "C:\\Games\\THUG2\\Game",
        );
        results.push(r);
        if (r.error) {
            print(`${base}: ${r.error}`);
            continue;
        }
        const n = r.nodes as Record<string, Record<string, Bucket> | number>;
        const sum = (b: unknown): number => Object.values(b as Record<string, Bucket>).reduce((x, y) => x + y.count, 0);
        const sc = r.scripts as Record<string, { count: number }>;
        const col = r.collision as Record<string, unknown>;
        print(
            `${base}: nodes ${String(n.thps4)} / ${String(n.thugpro)}; moved ${sum(n.moved)}, rotated ${sum(n.rotated)}, ` +
                `renamed ${sum(n.renamed)}, added ${sum(n.added)}, removed ${sum(n.removed)}, changed ${sum(n.changed)}; ` +
                `spawns changed ${(r.spawns.changed as unknown[]).length}; scripts +${sc.added!.count} -${sc.removed!.count} ` +
                `~${sc.changed!.count}; collision ${col.error ? `not compared (${String(col.error)})` : `objects +${(col.added as { count: number }).count} -${(col.removed as { count: number }).count}, faces changed ${(col.face_count_changed as { count: number }).count}`}`,
        );
        if (r.applied?.length) print(`  ours with the profile: ${appliedText(r.applied)}`);
    }
    if (v.json) writeTextPy(v.json, pyJsonDumps(results, 1));
    return 0;
}

if (isMain(import.meta.url)) await runMain(() => main());
