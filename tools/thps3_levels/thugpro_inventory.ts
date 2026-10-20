// NOT ORIGINAL - an inventory of THUGPRO's hand edits in its THPS3 level ports, against THPS3's own data (read only:
// the user's THPS3 install and THUGPRO's data\pre). For the user to decide what else to agree on (docs/pending/
// thps3-thugpro.md "THUGPRO hand edits: inventory"); nothing here changes our converter.
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
//        [--json out] [--examples N]

import * as fs from "node:fs";
import { parseArgs } from "node:util";
import * as colCheck from "../level_port/col_check.ts";
import * as prx from "../level_port/prx.ts";
import * as qb from "../level_port/qb.ts";
import { Name, type QbValue, Struct, Vector } from "../level_port/qb.ts";
import * as thug2Col from "../level_port/thug2_col.ts";
import { hex8, isMain, print, pyExit, pyJoin, runMain } from "../lib/py.ts";
import { findPath } from "../thaw_levels/thaw_levels.ts";
import { maxDiff, nameOf, nodeArray, position } from "../thaw_levels/thugpro_compare.ts";
import { findFolder, LEVELS, levelFiles, STAGING, scriptNames } from "./thps3_levels.ts";
import { isSpawn } from "./thugpro_compare.ts";

export const SHARED = ["foun", "rio", "sub", "si", "tok", "shp", "ware", "burn", "ros"];
const DEFAULT_THUGPRO = pyJoin(process.env.LOCALAPPDATA ?? "", "THUG Pro");

// THPS3 class -> the THUG2 classes a port makes of it (not counted as an edit).
const USUAL = new Map<string, string[]>([
    ["environmentobject", ["levelgeometry"]],
    ["gameobject", ["levelobject", "gameobject"]],
    ["bouncyobject", ["gameobject", "levelobject"]],
    ["vehicle", ["vehicle", "levelobject"]],
    ["animatingobject", ["animatingobject", "levelobject", "gameobject"]],
]);
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
            )
                add(inv.collision, "moved", text(c), limit);
        }
        for (const [c, o] of a)
            if (!b.has(c)) add(inv.collision, "object only ours", `${text(c)} (${o.faces.length} faces)`, limit);
    }
    return inv;
}

// Can a kind of edit change play between the two sides? (see the header)
export function affectsPlay(category: string, kindText: string): string {
    const [k, what = ""] = kindText.split(" ");
    if (category === "collision") return "yes - a wall or floor differs";
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

const USAGE =
    "usage: thugpro_inventory.ts [-h] [--thug2 THUG2] [--thps3 THPS3] [--thugpro THUGPRO] [--json JSON] [--examples N] [levels ...]";

const OPTIONS = {
    thug2: { type: "string" },
    thps3: { type: "string" },
    thugpro: { type: "string" },
    json: { type: "string" },
    examples: { type: "string" },
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
    const out: LevelInventory[] = [];
    for (const level of levels) {
        const inv = inventory(level, thps3, thugpro, pyJoin(thug2, STAGING, "levels"), globalNames, limit);
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
