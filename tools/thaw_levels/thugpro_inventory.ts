// NOT ORIGINAL - an inventory of THUGPRO's own edits to its THAW level ports (docs/pending/thaw.md "THUGPRO's other
// hand edits in its THAW levels - inventory"): its node arrays, level scripts and collision (<thugpro>\data\pre\
// <level>_scripts.prx / <level>col.prx) against THAW's own full zone archives with their _sfx sub-zones
// (<thaw>\Game\data\worlds\worldzones\<zone>\<zone>.pak.wpc), in THUGPRO's frame (thaw_levels.frameShift with the
// thugpro profile). Read only on both installs; nothing of THUGPRO's goes into our output - this is the list the user
// decides on, and the base for register entries (TP-4: the spawns). Nodes are matched by name (the zone prefix of the
// game-mode nodes ignored, as THUGPRO strips it); per area: moved / turned, renamed (an added node at a removed one's
// place, same class), added, removed, changed keys and flags (by class and direction; the keys THUGPRO's toolchain
// changes on every node left out), Links (by the linked nodes' names), the spawns, scripts (by the names each calls)
// and collision objects (added, removed, other face count, moved). THUGPRO converted THAW's Xbox files: some
// differences may be that build's rather than hand edits.
//
//   node tools/thaw_levels/thugpro_inventory.ts [levels...] [--thaw C:\Games\THAW] [--thugpro <dir>] [--json out]

import { parseArgs } from "node:util";
import * as nodes from "../level_port/nodes.ts";
import * as prx from "../level_port/prx.ts";
import * as qb from "../level_port/qb.ts";
import { Name, type QbValue, Struct, Vector } from "../level_port/qb.ts";
import * as thug2Col from "../level_port/thug2_col.ts";
import { hex8, isMain, print, pyExit, pyJoin, pyJsonDumps, runMain, writeTextPy } from "../lib/py.ts";
import * as thaw from "./thaw.ts";
import { COMPAT, findPath, frameShift, LEVELS, MULTIPLAYER_NAME, setCompat } from "./thaw_levels.ts";
import { nodeArray } from "./thugpro_compare.ts";

const DEFAULT_THUGPRO = pyJoin(process.env.LOCALAPPDATA ?? "", "THUG Pro");
const TOLERANCE = 0.1;
const ANGLE_TOLERANCE = 0.001;
const EXAMPLES = 6;
const NET_TYPES: ReadonlySet<string> = new Set(["multiplayer", "horse", "ctf", "ctf_1", "ctf_2", "team"]);
// Keys THUGPRO's toolchain changes on every node (thaw_node_array_converter.py: commented-out THPS leftovers, model /
// profile / sound type checks, the particle keys rewritten) and the ones compared otherwise (positions, Links).
const SYSTEMATIC: ReadonlySet<string> = new Set([
    ...["ignoredq", "psylinder", "terrain1", "terrain2", "object", "shatter", "hitpoints", "push0", "push1", "push2"],
    ...["model", "profile", "soundtype", "skeletonname", "animname", "excludegeometry", "pos", "position", "links"],
    ...["boxdims", "boxpositions", "boxrgbas", "radii", "radiusspread", "startposition", "midposition", "endposition"],
    ...["boxdimsstart", "boxdimsmid", "boxdimsend", "startradius", "midradius", "endradius", "startradiusspread"],
    ...["midradiusspread", "endradiusspread", "startrgb", "midrgb", "endrgb", "startalpha", "midalpha", "endalpha"],
]);

type Names = Map<number, string>;
interface Tally {
    count: number;
    examples: string[];
}
interface ColInfo {
    faces: number;
    collidable: number;
    bbox: readonly number[];
}

const tally = (map: Map<string, Tally>, key: string, example: string): void => {
    const t = map.get(key) ?? { count: 0, examples: [] };
    t.count += 1;
    if (t.examples.length < EXAMPLES) t.examples.push(example);
    map.set(key, t);
};
const sorted = (map: Map<string, Tally>): Record<string, Tally> =>
    Object.fromEntries([...map].sort((a, b) => b[1].count - a[1].count));

function inventory(zone: string, thawFolder: string, thugpro: string, names: Names): Record<string, unknown> {
    const low = zone.toLowerCase();
    const key = COMPAT.levelNames.get(zone) ?? low;
    const text = (v: number): string => names.get(v) ?? `#${hex8(v)}`;
    const nameOf = (n: Struct): string | null => {
        const v = n.get("Name");
        return v instanceof Name ? text(v.value) : null;
    };
    const cls = (n: Struct): string => {
        const c = n.get("Class");
        return c instanceof Name ? text(c.value).toLowerCase() : "?";
    };
    const norm = (name: string): string => {
        const l = name.toLowerCase();
        return l.startsWith(`${low}_`) && MULTIPLAYER_NAME.test(l.slice(low.length + 1)) ? l.slice(low.length + 1) : l;
    };
    const posOf = (n: Struct): number[] | null => {
        let v = n.get("Pos");
        if (v instanceof Vector) return [...v.values];
        v = n.get("Position");
        return v instanceof Vector ? [v.values[0]!, v.values[1]!, -v.values[2]!] : null;
    };
    const angOf = (n: Struct): number[] => {
        const v = n.get("Angles");
        return v instanceof Vector ? [...v.values] : [0, 0, 0];
    };
    const d3 = (a: readonly number[], b: readonly number[]): number =>
        Math.max(...[0, 1, 2].map((k) => Math.abs(a[k]! - b[k]!)));
    const repr = (v: unknown): string => {
        if (v instanceof Name) return text(v.value).toLowerCase();
        if (v instanceof Vector) return `(${v.values.map((x) => x.toFixed(2)).join(",")})`;
        if (Array.isArray(v)) return `[${v.map(repr).join(",")}]`;
        if (v instanceof Struct)
            return `{${v.entries.map(([k, x]) => (k ? `${text(k.value)}=` : "") + repr(x)).join(" ")}}`;
        if (typeof v === "number") return Number.isInteger(v) ? String(v) : v.toFixed(3);
        if (v !== null && typeof v === "object" && "value" in v)
            return Number((v as { value: number }).value).toFixed(3);
        return JSON.stringify(v);
    };
    const types = (n: Struct): string[] | null => {
        const t = n.get("restart_types");
        if (t === undefined) return [];
        return Array.isArray(t)
            ? t.filter((x): x is Name => x instanceof Name).map((x) => text(x.value).toLowerCase())
            : null;
    };
    const isSpawn = (n: Struct): boolean => cls(n) === "restart" && (types(n) ?? []).some((t) => NET_TYPES.has(t));
    const calledNames = (body: readonly qb.Statement[], thawSide: boolean): Set<string> => {
        const out = new Set<string>();
        const walk = (v: QbValue): void => {
            if (v instanceof Name) out.add(norm(text(v.value)));
            else if (Array.isArray(v)) v.forEach(walk);
            else if (v instanceof Struct) {
                for (const [k, x] of v.entries) {
                    if (k) out.add(norm(text(k.value)));
                    walk(x);
                }
            }
        };
        for (const line of body) {
            for (const [t, p] of line) {
                if (t === 0x16) out.add(norm(text(p.readUInt32LE(0))));
                else if (t === 0x4a && thawSide) {
                    try {
                        walk(thaw.inlineStruct(p, names));
                    } catch {
                        // (an inline structure that doesn't read: its names left out)
                    }
                }
            }
        }
        return out;
    };

    // THAW: the full archive with its _sfx sub-zone
    const pak = findPath(thawFolder, "Game", "data", "worlds", "worldzones", zone, `${zone}.pak.wpc`);
    if (pak === null) pyExit(`no THAW ${zone}.pak.wpc in ${thawFolder}`);
    const thawNodes: Struct[] = [];
    const sfxNodes = new Set<string>();
    const thawScripts = new Map<string, Set<string>>();
    const thawCol = new Map<number, ColInfo>();
    let main: QbValue[] = [];
    for (const [entry, data] of thaw.pakFiles(pak)) {
        const sfx = (entry.flags & thaw.PAK_SFX) !== 0;
        if (entry.extension === ".col") {
            for (const o of thaw.readCol(data, zone).objects) {
                thawCol.set(o.checksum, {
                    faces: o.faces.length,
                    collidable: o.faces.filter((f) => !(f[0] & 0x10)).length,
                    bbox: o.bbox,
                });
            }
        } else if ([".qb", ".nqb", ".sqb"].includes(entry.extension) && entry.path) {
            const q = new thaw.QbItems(data, entry.path, names);
            const globals = new Map(q.globals.map(([k, v]) => [k.value, v] as const));
            for (const [k, v] of q.globals) {
                if (!Array.isArray(v) || !text(k.value).toLowerCase().endsWith("_nodearray")) continue;
                const array = nodes.expandIncludes(v, globals);
                for (const n of array) {
                    if (!(n instanceof Struct)) continue;
                    thawNodes.push(n);
                    if (sfx) sfxNodes.add(norm(nameOf(n) ?? ""));
                }
                if (!sfx && text(k.value).toLowerCase() === `${low}_nodearray`) main = array;
            }
            for (const [s, body] of q.scripts) thawScripts.set(norm(text(s)), calledNames(body, true));
        }
    }
    const shift = frameShift(zone, main, names);
    // THUGPRO
    const scriptsPrx = findPath(thugpro, "data", "pre", `${key}_scripts.prx`);
    if (scriptsPrx === null) pyExit(`THUGPRO has no ${key}_scripts.prx`);
    const tpNodes: Struct[] = [];
    const tpScripts = new Map<string, Set<string>>();
    for (const [file, data] of prx.entries(scriptsPrx)) {
        const lowFile = file.toLowerCase();
        if (!lowFile.endsWith(".qb") || !lowFile.startsWith(`levels\\${key}\\`)) continue;
        const q = new qb.QbFile(data, file, true);
        for (const [k, v] of q.names) if (!names.has(k)) names.set(k, v);
        if (lowFile === `levels\\${key}\\${key}.qb`) {
            for (const n of nodeArray(data, file)[0]) if (n instanceof Struct) tpNodes.push(n);
        }
        for (const [s, body] of q.scripts) tpScripts.set(norm(text(s)), calledNames(body, false));
    }

    // --- nodes
    const byName = (list: Struct[]): Map<string, Struct> => {
        const out = new Map<string, Struct>();
        for (const n of list) {
            const nm = nameOf(n);
            if (nm !== null && !out.has(norm(nm))) out.set(norm(nm), n);
        }
        return out;
    };
    const thawBy = byName(thawNodes);
    const tpBy = byName(tpNodes);
    const inFrame = (p: readonly number[]): number[] => [0, 1, 2].map((k) => p[k]! + shift[k]!);
    const moved: { name: string; class: string; by: number }[] = [];
    const turned: string[] = [];
    const classChanged: string[] = [];
    const flags = new Map<string, Tally>();
    const links: string[] = [];
    const linkNames = (n: Struct, all: readonly QbValue[]): string => {
        const l = n.get("Links");
        if (!Array.isArray(l)) return "";
        return l
            .map((i) => {
                const target = typeof i === "number" ? all[i] : undefined;
                return target instanceof Struct ? norm(nameOf(target) ?? "?") : "?";
            })
            .sort()
            .join(",");
    };
    const keys = (n: Struct): Map<string, string> => {
        const m = new Map<string, string>();
        for (const [k, v] of n.entries) {
            if (k === null) {
                if (v instanceof Name) m.set(`flag:${text(v.value).toLowerCase()}`, "1");
            } else m.set(text(k.value).toLowerCase(), repr(v));
        }
        return m;
    };
    for (const [nm, t] of tpBy) {
        const o = thawBy.get(nm);
        if (o === undefined) continue;
        const po = posOf(o);
        const pt = posOf(t);
        if (po !== null && pt !== null) {
            const d = d3(inFrame(po), pt);
            if (d > TOLERANCE) moved.push({ name: nameOf(t)!, class: cls(t), by: Math.round(d * 10) / 10 });
            else if (d3(angOf(o), angOf(t)) > ANGLE_TOLERANCE) turned.push(nameOf(t)!);
        }
        if (cls(o) !== cls(t)) classChanged.push(`${nameOf(t)}: ${cls(o)} -> ${cls(t)}`);
        const ko = keys(o);
        const kt = keys(t);
        for (const k of new Set([...ko.keys(), ...kt.keys()])) {
            const base = k.replace(/^flag:/, "");
            if (k === "name" || SYSTEMATIC.has(base) || /^(vcwibble|uvwibble)/.test(base)) continue;
            if (/^flag:(-?\d|#)/.test(k) || ko.get(k) === kt.get(k)) continue;
            const dir = !ko.has(k) ? "added" : !kt.has(k) ? "removed" : "changed";
            tally(
                flags,
                `${cls(t)} ${k} ${dir}`,
                `${nameOf(t)}${dir === "changed" ? `: ${ko.get(k)} -> ${kt.get(k)}` : ""}`,
            );
        }
        if (linkNames(o, main) !== linkNames(t, tpNodes)) links.push(nameOf(t)!);
    }
    const removedLeft = new Set([...thawBy.keys()].filter((k) => !tpBy.has(k) && !sfxNodes.has(k)));
    const renamed: string[] = [];
    const added = new Map<string, Tally>();
    for (const k of tpBy.keys()) {
        if (thawBy.has(k)) continue;
        const t = tpBy.get(k)!;
        const pt = posOf(t);
        const hit =
            pt === null
                ? undefined
                : [...removedLeft].find((r) => {
                      const o = thawBy.get(r)!;
                      const po = posOf(o);
                      return po !== null && cls(o) === cls(t) && d3(inFrame(po), pt) < TOLERANCE;
                  });
        if (hit !== undefined) {
            renamed.push(`${nameOf(thawBy.get(hit)!)} -> ${nameOf(t)}`);
            removedLeft.delete(hit);
        } else tally(added, cls(t), nameOf(t)!);
    }
    const removed = new Map<string, Tally>();
    for (const k of removedLeft) tally(removed, cls(thawBy.get(k)!), nameOf(thawBy.get(k)!)!);
    // spawns
    const spawnsTp = tpNodes.filter(isSpawn);
    const spawns = {
        thaw: thawNodes.filter((n) => isSpawn(n) && !sfxNodes.has(norm(nameOf(n) ?? ""))).length,
        thugpro: spawnsTp.length,
        moved: spawnsTp
            .filter((t) => {
                const o = thawBy.get(norm(nameOf(t)!));
                const po = o && posOf(o);
                const pt = posOf(t);
                return po && pt && d3(inFrame(po), pt) > TOLERANCE;
            })
            .map((t) => nameOf(t)),
        added: spawnsTp.filter((t) => !thawBy.has(norm(nameOf(t)!))).map((t) => nameOf(t)),
        types_changed: spawnsTp.flatMap((t) => {
            const o = thawBy.get(norm(nameOf(t)!));
            if (o === undefined) return [];
            const a = types(o);
            const b = types(t) ?? [];
            return a !== null && a.join(" ") === b.join(" ")
                ? []
                : [
                      `${nameOf(t)}: ${a === null ? repr(o.get("restart_types")) : `[${a.join(" ")}]`} -> [${b.join(" ")}]`,
                  ];
        }),
        no_longer: thawNodes
            .filter((o) => isSpawn(o) && !sfxNodes.has(norm(nameOf(o) ?? "")))
            .filter((o) => {
                const t = tpBy.get(norm(nameOf(o)!));
                return t === undefined || !isSpawn(t);
            })
            .map((o) => nameOf(o)),
    };
    // scripts
    const scriptsChanged: string[] = [];
    for (const [s, called] of tpScripts) {
        const o = thawScripts.get(s);
        if (o === undefined) continue;
        const minus = [...o].filter((x) => !called.has(x));
        const plus = [...called].filter((x) => !o.has(x));
        if (minus.length || plus.length) scriptsChanged.push(`${s}: -[${minus.slice(0, 5)}] +[${plus.slice(0, 5)}]`);
    }
    const scriptsAdded = [...tpScripts.keys()].filter((s) => !thawScripts.has(s));
    const scriptsRemoved = [...thawScripts.keys()].filter((s) => !tpScripts.has(s));
    // collision
    const col = { added: [] as string[], removed: [] as string[], faces: [] as string[], moved: [] as string[] };
    const colPrx = findPath(thugpro, "data", "pre", `${key}col.prx`);
    if (colPrx !== null) {
        const tpCol = new Map<number, ColInfo>();
        for (const [, data] of prx.entries(colPrx, (n) => n.toLowerCase().endsWith(".col.xbx"))) {
            for (const o of thug2Col.readCol(data, key).objects) {
                tpCol.set(o.checksum, {
                    faces: o.faces.length,
                    collidable: o.faces.filter((f) => !(f[0] & 0x10)).length,
                    bbox: o.bbox,
                });
            }
        }
        const mark = (c: number, info: ColInfo): string => `${text(c)}${info.collidable ? "" : " (not collidable)"}`;
        for (const [c, o] of tpCol) {
            const t = thawCol.get(c);
            if (t === undefined) col.added.push(mark(c, o));
            else if (t.faces !== o.faces) col.faces.push(`${text(c)} ${t.faces} -> ${o.faces}`);
            else if (d3(inFrame(t.bbox), o.bbox) > 1.0) col.moved.push(text(c));
        }
        for (const [c, t] of thawCol) {
            if (!tpCol.has(c) && !/_sfx_/i.test(text(c))) col.removed.push(mark(c, t));
        }
    }
    const list = (items: readonly unknown[]): { count: number; examples: unknown[] } => ({
        count: items.length,
        examples: items.slice(0, EXAMPLES),
    });
    return {
        zone,
        thugpro_level: key,
        shift,
        nodes: { thaw: thawNodes.length, thaw_sfx: sfxNodes.size, thugpro: tpNodes.length },
        moved: list([...moved].sort((a, b) => b.by - a.by)),
        turned: list(turned),
        renamed: list(renamed),
        class_changed: list(classChanged),
        added: sorted(added),
        removed: sorted(removed),
        keys: sorted(flags),
        links_changed: list(links),
        spawns,
        scripts: {
            thaw: thawScripts.size,
            thugpro: tpScripts.size,
            added: list(scriptsAdded),
            removed: list(scriptsRemoved),
            changed: list(scriptsChanged),
        },
        collision: {
            added: list(col.added),
            removed_not_sfx: list(col.removed),
            face_count_changed: list(col.faces),
            moved: list(col.moved),
        },
    };
}

function summary(r: Record<string, unknown>): string[] {
    const counts = (m: unknown): string =>
        Object.entries(m as Record<string, Tally>)
            .map(([c, t]) => `${c} ${t.count} (${t.examples.slice(0, 2).join(", ")})`)
            .join("; ");
    const l = (v: unknown): string => {
        const x = v as { count: number; examples: unknown[] };
        return `${x.count}${
            x.count
                ? `: ${x.examples
                      .slice(0, 4)
                      .map((e) => (typeof e === "string" ? e : JSON.stringify(e)))
                      .join(", ")}`
                : ""
        }`;
    };
    const n = r.nodes as Record<string, number>;
    const s = r.scripts as Record<string, unknown>;
    const c = r.collision as Record<string, unknown>;
    const out = [
        `== ${r.zone} (THUGPRO: ${r.thugpro_level}) nodes THAW ${n.thaw} (${n.thaw_sfx} _sfx), THUGPRO ${n.thugpro}`,
        `  moved ${l(r.moved)}`,
        `  turned ${l(r.turned)}; renamed ${l(r.renamed)}`,
        `  class changed ${l(r.class_changed)}`,
        `  added: ${counts(r.added) || "none"}`,
        `  removed: ${counts(r.removed) || "none"}`,
    ];
    for (const [k, t] of Object.entries(r.keys as Record<string, Tally>)) {
        out.push(`  ${k}: ${t.count} (${t.examples.slice(0, 3).join(", ")})`);
    }
    out.push(
        `  links changed ${l(r.links_changed)}`,
        `  spawns ${JSON.stringify(r.spawns)}`,
        `  scripts THAW ${s.thaw}, THUGPRO ${s.thugpro}: added ${l(s.added)}; removed ${l(s.removed)}; changed ${l(s.changed)}`,
        `  collision: added ${l(c.added)}; removed ${l(c.removed_not_sfx)}; faces ${l(c.face_count_changed)}; moved ${l(c.moved)}`,
    );
    return out;
}

const USAGE = "usage: thugpro_inventory.ts [-h] [--thaw THAW] [--thugpro THUGPRO] [--json JSON] [levels ...]";
const OPTIONS = { thaw: { type: "string" }, thugpro: { type: "string" }, json: { type: "string" } } as const;

export function main(argv: readonly string[] = process.argv.slice(2)): number {
    let parsed: ReturnType<typeof parseArgs<{ options: typeof OPTIONS; allowPositionals: true }>>;
    try {
        parsed = parseArgs({ args: [...argv], options: OPTIONS, allowPositionals: true, strict: true });
    } catch (e) {
        process.stderr.write(`${USAGE}\nthugpro_inventory.ts: error: ${(e as Error).message}\n`);
        pyExit(2);
    }
    const v = parsed.values;
    const thawFolder = v.thaw ?? "C:\\Games\\THAW";
    const thugpro = v.thugpro ?? DEFAULT_THUGPRO;
    setCompat("thugpro"); // (THUGPRO's names and frame: the comparison is in its frame)
    const names = thaw.loadNames(thawFolder);
    let zones = LEVELS.map((l) => l[0]);
    if (parsed.positionals.length) {
        const wanted = new Set(parsed.positionals.map((x) => x.toLowerCase()));
        zones = zones.filter((z) => wanted.has(z.toLowerCase()) || wanted.has(z.toLowerCase().slice(2)));
    }
    const results: Record<string, unknown> = {};
    for (const zone of zones) {
        const r = inventory(zone, thawFolder, thugpro, names);
        results[zone] = r;
        for (const line of summary(r)) print(line);
    }
    if (v.json) writeTextPy(v.json, pyJsonDumps(results, 1));
    return 0;
}

if (isMain(import.meta.url)) await runMain(() => main());
