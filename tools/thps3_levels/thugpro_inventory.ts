// NOT ORIGINAL - an inventory of THUGPRO's hand edits in its THPS3 level ports, against THPS3's own data (read only:
// the user's THPS3 install and THUGPRO's data\pre). For the user to decide what else to agree on (docs/pending/
// thps3-thugpro.md "THUGPRO hand edits: inventory"); nothing here changes our converter. The edits the compatibility
// profile applies (TP-13 .. TP-19: tools/compat/thugpro/thps3_levels.json, made by thugpro_compare.ts
// --extract-thugpro) are checked in our converted levels with --applied.
//
// Per level (the 9 THUGPRO shares: foun rio sub si tok shp ware burn ros), node arrays by node name:
// - moved: in both, placed elsewhere (> 0.1 unit) or turned (> 0.001 rad) - by THUGPRO's class;
// - renamed / reused: a THUGPRO-only node at the place of a THPS3-only node of the same kind;
// - added / removed: only in THUGPRO's / only in THPS3's;
// - changed: flags (CreatedAtStart, AbsentInNetGames, TrickObject...), TerrainType, a rail's Links, the class beyond
//   the usual THPS3 -> THUG2 renames (EnvironmentObject -> LevelGeometry, GameObject -> LevelObject...);
// - scripts: THUGPRO's level scripts (<level>.qb, <level>_scripts.qb) against THPS3's level .qb by name: added, dropped,
//   and changed (the names each statement starts with differ - a translation THPS3 -> THUG2 counts too);
// - collision: THUGPRO's <level>col.prx against ours (THPS3's geometry, unchanged by the converter): objects only one
//   side has, objects with another face count or bounding box (> 1 unit).
// "play": whether a kind can make a difference between our players and THUGPRO's - rails (one side grinds, the other
// doesn't), geometry and collision (a wall or floor on one side only), spawns (TP-12). Peds, particles, sounds,
// scripted decoration: visual or local only.
//
//   node tools/thps3_levels/thugpro_inventory.ts [--thug2 C:\Games\THUG2-1] [--thps3 <dir>] [--thugpro <dir>]
//        [--json out] [--examples N] [--staging <folder>] [--applied] [levels...]
// (--staging: our converted levels, default <thug2>\thug2-decomp-thps3; --applied: each TP-13 .. TP-19 edit found in
// them or not, per level and category)

import * as fs from "node:fs";
import { parseArgs } from "node:util";
import * as compat from "../compat/compat.ts";
import * as colCheck from "../level_port/col_check.ts";
import * as prx from "../level_port/prx.ts";
import * as qb from "../level_port/qb.ts";
import { Name, type QbValue, Struct, Vector } from "../level_port/qb.ts";
import * as thug2Col from "../level_port/thug2_col.ts";
import { hex8, isMain, print, pyExit, pyJoin, runMain } from "../lib/py.ts";
import { findPath } from "../thaw_levels/thaw_levels.ts";
import { maxDiff, nameOf, nodeArray, position } from "../thaw_levels/thugpro_compare.ts";
import { findFolder, LEVELS, levelFiles, STAGING, scriptNames } from "./thps3_levels.ts";
import { isSpawn, sameFace, USUAL_CLASSES } from "./thugpro_compare.ts";

export const SHARED = ["foun", "rio", "sub", "si", "tok", "shp", "ware", "burn", "ros"];
const DEFAULT_THUGPRO = pyJoin(process.env.LOCALAPPDATA ?? "", "THUG Pro");

// (THPS3 class -> the THUG2 classes a port makes of it, not counted as an edit: thugpro_compare's USUAL_CLASSES)
const USUAL = USUAL_CLASSES;
const FLAGS = ["CreatedAtStart", "AbsentInNetGames", "TrickObject", "NetEnabled", "Permanent", "NeverSuspend"];

export interface Category {
    count: number;
    byKind: Record<string, number>;
    examples: string[];
}

const newCategory = (): Category => ({ count: 0, byKind: {}, examples: [] });

export interface LevelInventory {
    level: string;
    moved: Category;
    renamed: Category;
    added: Category;
    removed: Category;
    changed: Category;
    scripts: { added: Category; dropped: Category; changed: Category };
    collision: Category;
}

function classText(node: Struct, names: ReadonlyMap<number, string>): string {
    const c = node.get("Class");
    return c instanceof Name ? (names.get(c.value) ?? hex8(c.value)).toLowerCase() : "-";
}

// The kind shown: the class, rails and network spawns apart.
function kind(node: Struct, names: ReadonlyMap<number, string>): string {
    const c = classText(node, names);
    if (c === "restart") return isSpawn(node) ? "spawn" : "restart";
    if (c === "environmentobject") return "levelgeometry";
    return c;
}

function angles(node: Struct): readonly number[] {
    const a = node.get("Angles");
    return a instanceof Vector && a.kind === "vector" ? a.values : [0, 0, 0];
}

function flagSet(node: Struct): string {
    return FLAGS.filter((f) => node.hasFlag(f)).join(" ");
}

function add(cat: Category, k: string, example: string, limit: number): void {
    cat.count += 1;
    cat.byKind[k] = (cat.byKind[k] ?? 0) + 1;
    if (cat.examples.length < limit) cat.examples.push(example);
}

// The names each statement of a script starts with, joined (for "changed").
function shape(q: qb.QbFile, script: number): string {
    return q
        .calls(script)
        .map(([c]) => c)
        .join(",");
}

export function inventory(
    level: string,
    thps3Dir: string,
    thugpro: string,
    ours: string,
    globalNames: ReadonlyMap<number, string>,
    limit = 8,
): LevelInventory | null {
    const row = LEVELS.find(([b]) => b.toLowerCase() === level);
    const folder = row ? findFolder(pyJoin(thps3Dir, "Data", "Levels"), row[0]) : null;
    const scriptsPrx = findPath(thugpro, "data", "pre", `${level}_scripts.prx`);
    if (!row || !folder || !scriptsPrx) return null;
    const ownQ = new qb.QbFile(fs.readFileSync(levelFiles(folder, row[0]).get("qb")!), level);
    const ownArray = ownQ.get("NodeArray");
    const own = (Array.isArray(ownArray) ? ownArray : []).filter((n): n is Struct => n instanceof Struct);
    const theirFiles: qb.QbFile[] = [];
    let theirArray: QbValue[] = [];
    const names = new Map<number, string>(globalNames);
    for (const [k, v] of ownQ.names) names.set(k, v);
    for (const [file, data] of prx.entries(scriptsPrx, (n) => n.toLowerCase().endsWith(".qb"))) {
        const low = file.toLowerCase();
        if (low !== `levels\\${level}\\${level}.qb` && !low.endsWith("_scripts.qb")) continue;
        theirFiles.push(new qb.QbFile(data, file, true));
        const [array, n] = nodeArray(data, file);
        if (array.length > theirArray.length) theirArray = array;
        for (const [k, v] of n) names.set(k, v);
    }
    const theirs = theirArray.filter((n): n is Struct => n instanceof Struct);
    const text = (v: number): string => names.get(v) ?? hex8(v);
    const byName = (array: readonly Struct[]): Map<number, Struct> => {
        const out = new Map<number, Struct>();
        for (const n of array) if (nameOf(n) !== null && !out.has(nameOf(n)!)) out.set(nameOf(n)!, n);
        return out;
    };
    const ownBy = byName(own);
    const theirBy = byName(theirs);
    const inv: LevelInventory = {
        level,
        moved: newCategory(),
        renamed: newCategory(),
        added: newCategory(),
        removed: newCategory(),
        changed: newCategory(),
        scripts: { added: newCategory(), dropped: newCategory(), changed: newCategory() },
        collision: newCategory(),
    };
    for (const [nm, t] of theirBy) {
        const o = ownBy.get(nm);
        if (o === undefined) continue;
        const k = kind(t, names);
        const po = position(o);
        const pt = position(t);
        if (po !== null && pt !== null) {
            const d = maxDiff(po, pt);
            const a = maxDiff(angles(o), angles(t));
            if (d > 0.1 || a > 0.001)
                add(inv.moved, k, `${text(nm)} (${k}, ${d > 0.1 ? `${d.toFixed(0)} units` : "turned"})`, limit);
        }
        const changes: string[] = [];
        const co = classText(o, names);
        const ct = classText(t, names);
        if (co !== ct && !(USUAL.get(co) ?? []).includes(ct)) changes.push(`class ${co} -> ${ct}`);
        if (flagSet(o) !== flagSet(t)) changes.push(`flags [${flagSet(o)}] -> [${flagSet(t)}]`);
        const terrain = (n: Struct): string => {
            const v = n.get("TerrainType");
            return v instanceof Name ? text(v.value) : "";
        };
        if (terrain(o) !== terrain(t)) changes.push(`terrain ${terrain(o) || "-"} -> ${terrain(t) || "-"}`);
        if (ct === "railnode") {
            const linkNames = (n: Struct, array: readonly Struct[]): string => {
                const v = n.get("Links");
                return Array.isArray(v)
                    ? v
                          .map((i) => (typeof i === "number" && array[i] ? text(nameOf(array[i]!) ?? 0) : "?"))
                          .sort()
                          .join()
                    : "";
            };
            if (linkNames(o, own) !== linkNames(t, theirs)) changes.push("rail links");
        }
        if (changes.length) {
            // (by kind and what changed: "railnode terrain", "levelgeometry flags+class"...)
            const what = changes.map((c) => c.split(" ")[0]).join("+");
            add(inv.changed, `${k} ${what}`, `${text(nm)} (${changes.join("; ")})`, limit);
        }
    }
    // renamed / reused: a node only one side has, at the same place as one only the other has, of the same kind
    const onlyTheirs = [...theirBy].filter(([nm]) => !ownBy.has(nm)).map(([, n]) => n);
    const onlyOwn = [...ownBy].filter(([nm]) => !theirBy.has(nm)).map(([, n]) => n);
    const paired = new Set<Struct>();
    for (const t of onlyTheirs) {
        const pt = position(t);
        const match =
            pt === null
                ? undefined
                : onlyOwn.find((o) => {
                      const po = position(o);
                      return (
                          !paired.has(o) && po !== null && maxDiff(po, pt) <= 0.1 && kind(o, names) === kind(t, names)
                      );
                  });
        if (match !== undefined) {
            paired.add(match);
            paired.add(t);
            add(inv.renamed, kind(t, names), `${text(nameOf(match)!)} -> ${text(nameOf(t)!)}`, limit);
        } else {
            add(inv.added, kind(t, names), `${text(nameOf(t)!)} (${kind(t, names)})`, limit);
        }
    }
    for (const o of onlyOwn)
        if (!paired.has(o)) add(inv.removed, kind(o, names), `${text(nameOf(o)!)} (${kind(o, names)})`, limit);
    // scripts
    const ownScripts = new Set(ownQ.scripts.keys());
    const theirScripts = new Map<number, qb.QbFile>();
    for (const q of theirFiles) for (const k of q.scripts.keys()) theirScripts.set(k, q);
    for (const [k, q] of theirScripts) {
        if (!ownScripts.has(k))
            add(inv.scripts.added, /_TP_/i.test(text(k)) ? "THUGPRO-named (_TP_)" : "other", text(k), limit);
        else if (shape(ownQ, k) !== shape(q, k))
            add(inv.scripts.changed, /restart/i.test(text(k)) ? "restart scripts" : "other", text(k), limit);
    }
    for (const k of ownScripts) if (!theirScripts.has(k)) add(inv.scripts.dropped, "THPS3 script", text(k), limit);
    // collision
    const ourCol = pyJoin(ours, level, `${level}.col.xbx`);
    const theirCol = findPath(thugpro, "data", "pre", `${level}col.prx`);
    if (fs.existsSync(ourCol) && theirCol) {
        const objects = (cols: [string, Buffer][]): Map<number, thug2Col.ColObject> => {
            const out = new Map<number, thug2Col.ColObject>();
            for (const [name, data] of cols)
                for (const o of thug2Col.readCol(data, name).objects) out.set(o.checksum, o);
            return out;
        };
        const a = objects([[ourCol, fs.readFileSync(ourCol)]]);
        const b = objects(colCheck.colFiles(theirCol));
        for (const [c, o] of b) {
            const m = a.get(c);
            if (m === undefined)
                add(inv.collision, "object only THUGPRO's", `${text(c)} (${o.faces.length} faces)`, limit);
            else if (m.faces.length !== o.faces.length)
                add(inv.collision, "face count", `${text(c)} (${m.faces.length} -> ${o.faces.length})`, limit);
            else if (
                maxDiff(m.bbox.slice(0, 3), o.bbox.slice(0, 3)) > 1 ||
                maxDiff(m.bbox.slice(4, 7), o.bbox.slice(4, 7)) > 1
            ) {
                // (the same faces with another bounding box: THUGPRO's toolchain counts vertices no face uses - Tokyo's
                // road lines, Suburbia's gutter... - not a move)
                const corners = (x: thug2Col.ColObject, f: thug2Col.Face): number[][] =>
                    [f[2], f[3], f[4]].map((i) => x.verts[i]!);
                const same = o.faces.every((f) =>
                    m.faces.some(
                        (g) =>
                            sameFace(corners(m, g), corners(o, f), 0.15) ||
                            sameFace(corners(m, g), [...corners(o, f)].reverse(), 0.15),
                    ),
                );
                add(inv.collision, same ? "bounding box only" : "moved", text(c), limit);
            }
        }
        for (const [c, o] of a)
            if (!b.has(c)) add(inv.collision, "object only ours", `${text(c)} (${o.faces.length} faces)`, limit);
    }
    return inv;
}

// Can a kind of edit change play between the two sides? (see the header)
export function affectsPlay(category: string, kindText: string): string {
    const [k, what = ""] = kindText.split(" ");
    if (category === "collision")
        return kindText === "bounding box only" ? "no - toolchain" : "yes - a wall or floor differs";
    if (k === "spawn") return "spawns - unified (TP-12)";
    if (category.startsWith("scripts")) return "mostly no - local";
    if (k === "railnode") {
        if (category === "changed" && what === "terrain") return "grind sound / sparks only";
        return "yes - a rail one side can grind and the other can't";
    }
    if (k === "levelgeometry") {
        if (category === "changed" && !what.includes("flags") && !what.includes("class")) return "no";
        return "maybe - geometry present on one side only";
    }
    return "no - visual / local";
}

// --- --applied: the compatibility profile's edits (TP-13 .. TP-19) checked in our converted level --------------------

export interface AppliedCategory {
    count: number;
    matching: number;
    mismatches: string[];
}

// Each edit of the profile's data (tools/compat/thugpro/thps3_levels.json) looked up in our converted level
// (<staging>\levels\<level>\<level>.qb / .col.xbx): moved nodes at the data's place and angles, flags added / taken
// off, terrains, removed nodes gone, renamed nodes under their new name only, added rails there (place, Links),
// collision objects dropped and faces gone (corners in the same turning). A node our converter leaves out on its own
// (Warehouse's dark set) counts as a mismatch with "(not in ours)".
export function appliedCheck(
    level: string,
    profile: compat.Profile,
    ours: string,
    thugpro: string,
    limit = 8,
): Record<string, AppliedCategory> | null {
    const qbFile = pyJoin(ours, level, `${level}.qb`);
    if (!fs.existsSync(qbFile)) return null;
    const [array] = nodeArray(fs.readFileSync(qbFile), qbFile);
    const nodes = array.filter((n): n is Struct => n instanceof Struct);
    const byName = new Map<number, Struct>();
    for (const n of nodes) if (nameOf(n) !== null && !byName.has(nameOf(n)!)) byName.set(nameOf(n)!, n);
    const index = new Map<Struct, number>(nodes.map((n, i) => [n, i]));
    const c = (text: string): number =>
        text.startsWith("#") ? Number.parseInt(text.slice(1), 16) >>> 0 : qb.checksum(text);
    const out: Record<string, AppliedCategory> = {};
    const check = (category: string, label: string, ok: boolean, why = ""): void => {
        const cat = out[category] ?? { count: 0, matching: 0, mismatches: [] };
        out[category] = cat;
        cat.count += 1;
        if (ok) cat.matching += 1;
        else if (cat.mismatches.length < limit) cat.mismatches.push(why ? `${label} (${why})` : label);
    };
    const section = <T>(key: string): T | undefined => profile.section("thps3_levels", key)[level] as T | undefined;
    for (const [name, p] of Object.entries(
        section<Record<string, { pos: number[]; angles: number[] }>>("moved") ?? {},
    )) {
        const node = byName.get(c(name));
        const at = node ? position(node) : null;
        check(
            "moved (TP-13)",
            name,
            at !== null && maxDiff(at, p.pos) <= 0.1 && maxDiff(angles(node!), p.angles) <= 0.001,
            node ? "elsewhere" : "not in ours",
        );
    }
    for (const [name, f] of Object.entries(
        section<Record<string, { add: string[]; remove: string[] }>>("flags") ?? {},
    )) {
        const node = byName.get(c(name));
        const ok = node !== undefined && f.add.every((x) => node.hasFlag(x)) && f.remove.every((x) => !node.hasFlag(x));
        check("flags (TP-14)", name, ok, node ? `[${flagSet(node)}]` : "not in ours");
    }
    for (const [name, t] of Object.entries(section<Record<string, string>>("terrain") ?? {})) {
        const node = byName.get(c(name));
        const v = node?.get("TerrainType");
        check("terrain (TP-15)", name, v instanceof Name && v.value === c(t), node ? "other terrain" : "not in ours");
    }
    for (const name of section<string[]>("removed") ?? [])
        check("removed (TP-16)", name, !byName.has(c(name)), "still there");
    for (const [old, now] of Object.entries(section<Record<string, string>>("renamed") ?? {}))
        check("renamed (TP-17)", `${old} -> ${now}`, !byName.has(c(old)) && byName.has(c(now)));
    for (const r of section<{ name: string; pos: number[]; links: string[] }[]>("rails") ?? []) {
        const node = byName.get(c(r.name));
        const links = node?.get("Links");
        const linked = (Array.isArray(links) ? links : []).map((i) =>
            typeof i === "number" && nodes[i] ? nameOf(nodes[i]!) : null,
        );
        const ok =
            node !== undefined &&
            index.has(node) &&
            maxDiff(position(node) ?? [], r.pos) <= 0.1 &&
            r.links.every((l) => linked.includes(c(l))) &&
            linked.length === r.links.length;
        check("rails (TP-18)", r.name, ok, node ? "elsewhere or other links" : "not in ours");
    }
    const colFile = pyJoin(ours, level, `${level}.col.xbx`);
    const colEdits = section<Record<string, { drop?: boolean; remove?: number[][][] }>>("collision") ?? {};
    if (Object.keys(colEdits).length && fs.existsSync(colFile)) {
        const objects = new Map(
            thug2Col.readCol(fs.readFileSync(colFile), colFile).objects.map((o) => [o.checksum, o]),
        );
        const theirCol = new Map<number, thug2Col.ColObject>();
        const theirFile = findPath(thugpro, "data", "pre", `${level}col.prx`);
        for (const [file, data] of theirFile ? colCheck.colFiles(theirFile) : [])
            for (const o of thug2Col.readCol(data, file).objects) theirCol.set(o.checksum, o);
        for (const [name, e] of Object.entries(colEdits)) {
            const o = objects.get(c(name));
            if (e.drop) {
                check("collision (TP-19)", `${name} dropped`, o === undefined, "still collides");
                continue;
            }
            // (a face taken out can have a twin with the same corners and turning: as many such faces as THUGPRO's)
            const t = theirCol.get(c(name));
            const count = (obj: thug2Col.ColObject | undefined, corners: readonly (readonly number[])[]): number =>
                obj === undefined
                    ? 0
                    : obj.faces.filter((f) =>
                          sameFace(
                              [f[2], f[3], f[4]].map((i) => obj.verts[i]!),
                              corners,
                              0.15,
                          ),
                      ).length;
            for (const corners of e.remove ?? []) {
                const ok = o !== undefined && t !== undefined && count(o, corners) === count(t, corners);
                check("collision (TP-19)", `${name} face`, ok, o ? "still there" : "no object");
            }
        }
    }
    return out;
}

const USAGE =
    "usage: thugpro_inventory.ts [-h] [--thug2 THUG2] [--thps3 THPS3] [--thugpro THUGPRO] [--json JSON] [--examples N] [--applied] [--staging STAGING] [levels ...]";

const OPTIONS = {
    thug2: { type: "string" },
    thps3: { type: "string" },
    thugpro: { type: "string" },
    json: { type: "string" },
    examples: { type: "string" },
    applied: { type: "boolean" },
    staging: { type: "string" },
    help: { type: "boolean", short: "h" },
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
    if (v.help) {
        print(USAGE);
        return 0;
    }
    let thug2 = v.thug2 ?? "C:\\Games\\THUG2\\Game";
    if (!fs.existsSync(pyJoin(thug2, "Data")) && fs.existsSync(pyJoin(thug2, "Game", "Data")))
        thug2 = pyJoin(thug2, "Game");
    const thps3 = v.thps3 ?? "C:\\Games\\THPS3";
    const thugpro = v.thugpro ?? DEFAULT_THUGPRO;
    const limit = v.examples ? Number.parseInt(v.examples, 10) : 8;
    const globalNames = new Map<number, string>();
    for (const q of scriptNames(thps3)) for (const [k, t] of q.names) globalNames.set(k, t);
    const levels = parsed.positionals.length ? parsed.positionals.map((x) => x.toLowerCase()) : SHARED;
    const ours = pyJoin(v.staging || pyJoin(thug2, STAGING), "levels");
    if (v.applied) {
        // (the profile's edits in our converted levels: what of THUGPRO's changes we have now)
        const profile = compat.load("thugpro");
        const all: Record<string, Record<string, AppliedCategory>> = {};
        for (const level of levels) {
            const result = appliedCheck(level, profile, ours, thugpro, limit);
            if (result === null) {
                print(`${level}: not converted`);
                continue;
            }
            all[level] = result;
            const parts = Object.entries(result).map(([k, r]) => `${k} ${r.matching}/${r.count}`);
            print(`${level}: ${parts.join(", ") || "no edits"}`);
            for (const [k, r] of Object.entries(result))
                if (r.mismatches.length) print(`    ${k} not matching: ${r.mismatches.join("; ")}`);
        }
        if (v.json) fs.writeFileSync(v.json, `${JSON.stringify(all, null, 1)}\n`, "utf-8");
        return 0;
    }
    const out: LevelInventory[] = [];
    for (const level of levels) {
        const inv = inventory(level, thps3, thugpro, ours, globalNames, limit);
        if (inv === null) {
            print(`${level}: not in both installs`);
            continue;
        }
        out.push(inv);
        print(`${level}:`);
        const show = (label: string, cat: Category, category: string): void => {
            if (!cat.count) return;
            const kinds = Object.entries(cat.byKind)
                .sort((a, b) => b[1] - a[1])
                .map(([k, c]) => `${k} ${c} [${affectsPlay(category, k)}]`)
                .join(", ");
            print(`  ${label}: ${cat.count} - ${kinds}`);
            print(`      e.g. ${cat.examples.join("; ")}`);
        };
        show("moved", inv.moved, "moved");
        show("renamed / reused", inv.renamed, "renamed");
        show("added", inv.added, "added");
        show("removed", inv.removed, "removed");
        show("changed", inv.changed, "changed");
        show("scripts added", inv.scripts.added, "scripts added");
        show("scripts dropped", inv.scripts.dropped, "scripts dropped");
        show("scripts changed", inv.scripts.changed, "scripts changed");
        show("collision", inv.collision, "collision");
    }
    if (v.json) fs.writeFileSync(v.json, `${JSON.stringify(out, null, 1)}\n`, "utf-8");
    return 0;
}

if (isMain(import.meta.url)) await runMain(() => main());
