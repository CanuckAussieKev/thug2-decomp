// NOT ORIGINAL - compares our converted THPS4 levels with THUGPRO's own ports of them, read from the user's THUGPRO
// install (read only: <thugpro>\data\pre\<level>_scripts.prx and <level>col.prx), for online play between the two
// (docs/pending/thps4-thugpro.md). The THPS4 counterpart of tools/thaw_levels/thugpro_compare.ts (its helpers are
// imported from there). What has to agree for a THUGPRO player and one of ours to share a level:
//   - the load script's name (the change-level message 0x46 / start info 0x33 carry its checksum): Load_<name>;
//   - the world frame: skater updates carry world positions, so every node has to sit where THUGPRO's does;
//   - the King of the Hill crowns in the same order (the crown is picked by its index among the Crown nodes) and at
//     the same places; the game-mode nodes under the names THUG2's scripts look for (TRG_CTF_Red...).
// Also reported: the multiplayer restarts (order and places), the CTF / flag nodes, what THUGPRO's node array has
// that ours doesn't (by class / type, and the trigger scripts its nodes run that ours don't), and how many of each
// side's collision faces can't be found through their BSP trees (tools/level_port/col_check.ts).
// THUGPRO's files are only read to compare: nothing of them goes into our output.
//
//   node tools/thps4_levels/thugpro_compare.ts [levels...] [--thug2 C:\Games\THUG2-2] [--thugpro <dir>]
//        [--staging <dir>] [--legacy-names] [--json out]

import * as fs from "node:fs";
import { parseArgs } from "node:util";
import * as colCheck from "../level_port/col_check.ts";
import * as prx from "../level_port/prx.ts";
import * as qb from "../level_port/qb.ts";
import { Name, type QbValue, Struct } from "../level_port/qb.ts";
import * as thug2Col from "../level_port/thug2_col.ts";
import {
    Float,
    formatFixed,
    hex8,
    isMain,
    print,
    pyExit,
    pyJoin,
    pyJsonDumps,
    pyRound,
    runMain,
    writeTextPy,
} from "../lib/py.ts";
import { findPath } from "../thaw_levels/thaw_levels.ts";
import {
    CTF_NAMES,
    isCrown,
    isMpRestart,
    maxDiff,
    nameOf,
    nodeArray,
    position,
    sameSequence,
    sequence,
    TOLERANCE,
} from "../thaw_levels/thugpro_compare.ts";
import { LEVELS, levelName, loadName, STAGING } from "./thps4_levels.ts";

const DEFAULT_THUGPRO = pyJoin(process.env.LOCALAPPDATA ?? "", "THUG Pro");

// THUGPRO's file names for a THPS4 level: its load script's name without Load_ (THUGPRO names its files after it).
const thugproName = (base: string): string => loadName(base).toLowerCase();

const prxFile = (folder: string, wanted: string): string | null => findPath(folder, "data", "pre", wanted);

function textOf(v: QbValue | undefined, names: ReadonlyMap<number, string>): string {
    return v instanceof Name ? (names.get(v.value) ?? hex8(v.value)) : "";
}

// "class/type" of each node, for the audit.
function kindOf(n: Struct, names: ReadonlyMap<number, string>): string {
    const cls = textOf(n.get("Class"), names) || "?";
    const type = textOf(n.get("Type"), names);
    return type ? `${cls}/${type}` : cls;
}

function counts(array: readonly QbValue[], names: ReadonlyMap<number, string>): Map<string, number> {
    const out = new Map<string, number>();
    for (const n of array) {
        if (!(n instanceof Struct)) continue;
        const k = kindOf(n, names).toLowerCase();
        out.set(k, (out.get(k) ?? 0) + 1);
    }
    return out;
}

export type Result = Record<string, unknown> & { base: string; level: string };

export function compare(base: string, oursDir: string, thugpro: string, legacy = false): Result {
    const mine = levelName(base, legacy);
    const load = loadName(base, legacy);
    const key = thugproName(base);
    const out: Result = { base, level: mine, load: `Load_${load}`, thugpro_load: `Load_${key}` };
    out.same_load = load.toLowerCase() === key;
    const oursQb = pyJoin(oursDir, mine, `${mine}.qb`);
    if (!fs.existsSync(oursQb)) {
        out.error = `not converted (${oursQb})`;
        return out;
    }
    const [ours, names] = nodeArray(fs.readFileSync(oursQb), oursQb);
    const scripts = prxFile(thugpro, `${key}_scripts.prx`);
    if (!scripts) {
        out.error = `THUGPRO has no ${key}_scripts.prx`;
        return out;
    }
    const data = prx.find(scripts, `levels\\${key}\\${key}.qb`);
    if (data === null) throw new TypeError(`${scripts}: no levels\\${key}\\${key}.qb`);
    const [theirs, theirNames] = nodeArray(data, `${key}.qb`);
    for (const [k, v] of theirNames) names.set(k, v);
    const byName = new Map<number, readonly number[]>();
    for (const n of theirs) {
        if (!(n instanceof Struct)) continue;
        const nm = nameOf(n);
        const p = position(n);
        if (nm !== null && p !== null && !byName.has(nm)) byName.set(nm, p);
    }
    let shared = 0;
    let same = 0;
    let worst: [number, string | null] = [0.0, null];
    const moved: [string, Float][] = []; // (the shared nodes not at the same place: THUGPRO's hand moves)
    for (const n of ours) {
        if (!(n instanceof Struct)) continue;
        const p = position(n);
        const nm = nameOf(n);
        if (p === null || nm === null || !byName.has(nm)) continue;
        shared += 1;
        const d = maxDiff(p, byName.get(nm)!);
        if (d <= TOLERANCE) same += 1;
        else {
            moved.push([names.get(nm) ?? hex8(nm), new Float(pyRound(d, 3))]);
            if (d > worst[0]) worst = [d, names.get(nm) ?? hex8(nm)];
        }
    }
    out.moved = moved;
    out.nodes_ours = ours.length;
    out.nodes_thugpro = theirs.length;
    out.shared = shared;
    out.same_place = same;
    out.worst = [new Float(pyRound(worst[0], 3)), worst[1]];
    const crownsOurs = sequence(ours, isCrown);
    const crownsTheirs = sequence(theirs, isCrown);
    out.crowns = sameSequence(crownsOurs, crownsTheirs)[0];
    out.crown_counts = [crownsOurs.length, crownsTheirs.length];
    const mpOurs = sequence(ours, isMpRestart);
    const mpTheirs = sequence(theirs, isMpRestart);
    out.mp_restarts = sameSequence(mpOurs, mpTheirs)[0];
    out.mp_restart_names = [
        mpOurs.map(([n]) => (n === null ? "?" : (names.get(n) ?? hex8(n)))),
        mpTheirs.map(([n]) => (n === null ? "?" : (names.get(n) ?? hex8(n)))),
    ];
    const ourNames = new Set(ours.filter((n): n is Struct => n instanceof Struct).map(nameOf));
    const theirNameSet = new Set(theirs.filter((n): n is Struct => n instanceof Struct).map(nameOf));
    out.ctf_ours = CTF_NAMES.filter((c) => ourNames.has(qb.checksum(c))).length;
    out.ctf_thugpro = CTF_NAMES.filter((c) => theirNameSet.has(qb.checksum(c))).length;
    // the audit: node kinds THUGPRO has more of, its nodes' trigger scripts ours don't run
    const oc = counts(ours, names);
    const tc = counts(theirs, names);
    const more: Record<string, string> = {};
    for (const [k, v] of [...tc].sort((a, b) => b[1] - a[1])) {
        const o = oc.get(k) ?? 0;
        if (v > o) more[k] = `${o} / ${v}`;
    }
    out.kinds_thugpro_more = more;
    const fewer: Record<string, string> = {};
    for (const [k, v] of [...oc].sort((a, b) => b[1] - a[1])) {
        const t = tc.get(k) ?? 0;
        if (v > t) fewer[k] = `${v} / ${t}`;
    }
    out.kinds_ours_more = fewer;
    const ourTriggers = new Map<number, number>();
    for (const n of ours) {
        const s = n instanceof Struct ? n.get("TriggerScript") : undefined;
        const nm = n instanceof Struct ? nameOf(n) : null;
        if (s instanceof Name && nm !== null) ourTriggers.set(nm, s.value);
    }
    const missing = new Map<string, number>();
    for (const n of theirs) {
        const s = n instanceof Struct ? n.get("TriggerScript") : undefined;
        const nm = n instanceof Struct ? nameOf(n) : null;
        if (!(s instanceof Name) || nm === null || ourTriggers.has(nm)) continue;
        const k = names.get(s.value) ?? hex8(s.value);
        missing.set(k, (missing.get(k) ?? 0) + 1);
    }
    out.trigger_scripts_thugpro_only = Object.fromEntries([...missing].sort((a, b) => b[1] - a[1]));
    // collision
    const ourCol = pyJoin(oursDir, mine, `${mine}.col.xbx`);
    if (fs.existsSync(ourCol)) {
        const [faces, missingFaces] = colCheck.check(thug2Col.readCol(fs.readFileSync(ourCol), ourCol));
        out.col_ours = [faces, missingFaces];
    }
    const theirCol = prxFile(thugpro, `${key}col.prx`);
    if (theirCol) {
        for (const [name, cdata] of colCheck.colFiles(theirCol)) {
            try {
                const [faces, missingFaces] = colCheck.check(thug2Col.readCol(cdata, name));
                out.col_thugpro = [faces, missingFaces];
            } catch (e) {
                // (some of THUGPRO's collision files don't read as THUG2's format: reported, not compared)
                if (!(e instanceof Error) || e.name !== "FormatError") throw e;
                out.col_thugpro_error = e.message;
            }
        }
    }
    return out;
}

const USAGE =
    "usage: thugpro_compare.ts [-h] [--thug2 THUG2] [--thugpro THUGPRO] [--staging STAGING] [--legacy-names] " +
    "[--json JSON] [levels ...]";

const OPTIONS = {
    thug2: { type: "string" },
    thugpro: { type: "string" },
    staging: { type: "string" }, // our converted levels (default <thug2>\thug2-decomp-thps4\levels)
    "legacy-names": { type: "boolean" }, // ours converted with --legacy-names (thps4_<lvl>)
    json: { type: "string" },
} as const;

const pad = (text: string, width: number): string => text.padEnd(width);

export function main(argv: readonly string[] = process.argv.slice(2)): number {
    let parsed: ReturnType<typeof parseArgs<{ options: typeof OPTIONS; allowPositionals: true }>>;
    try {
        parsed = parseArgs({ args: [...argv], options: OPTIONS, allowPositionals: true, strict: true });
    } catch (e) {
        process.stderr.write(`${USAGE}\nthugpro_compare.ts: error: ${(e as Error).message}\n`);
        pyExit(2);
    }
    const v = parsed.values;
    const thug2 = v.thug2 ?? "C:\\Games\\THUG2\\Game";
    const thugpro = v.thugpro ?? DEFAULT_THUGPRO;
    const oursDir = v.staging || pyJoin(thug2, STAGING, "levels");
    const legacy = !!v["legacy-names"];
    let bases = [...LEVELS];
    if (parsed.positionals.length) {
        const wanted = new Set(parsed.positionals.map((x) => x.toLowerCase()));
        bases = bases.filter((b) => wanted.has(b.toLowerCase()));
    }
    const results: Result[] = [];
    let bad = 0;
    for (const base of bases) {
        const r = compare(base, oursDir, thugpro, legacy);
        results.push(r);
        if ("error" in r) {
            print(`${pad(base, 10)} ${pad(r.level, 16)} ${String(r.error)}`);
            if (!String(r.error).startsWith("THUGPRO has no")) bad += 1;
            continue;
        }
        const worst = r.worst as [Float, string | null];
        const colOurs = (r.col_ours as number[] | undefined) ?? ["-", "-"];
        const colTheirs = (r.col_thugpro as number[] | undefined) ?? ["-", "-"];
        print(
            `${pad(base, 10)} ${String(r.load)}${r.same_load ? "" : ` (THUGPRO: ${String(r.thugpro_load)})`} ` +
                `nodes ${r.nodes_ours}/${r.nodes_thugpro}, ${r.shared} shared, ${r.same_place} at the same place ` +
                `(worst ${formatFixed(worst[0].value, 3)} ${worst[1] || ""}); crowns ${r.crowns}; mp restarts ` +
                `${r.mp_restarts}; ctf nodes ${r.ctf_ours}/${r.ctf_thugpro}; collision unreachable ours ` +
                `${colOurs[1]}, THUGPRO ${colTheirs[1]}`,
        );
        const shared = r.shared as number;
        if (!r.same_load || shared - (r.same_place as number) > Math.floor(shared / 100) || r.crowns !== "same") {
            bad += 1;
        }
    }
    if (v.json) writeTextPy(v.json, pyJsonDumps(results, 1));
    return bad ? 1 : 0;
}

if (isMain(import.meta.url)) await runMain(() => main());
