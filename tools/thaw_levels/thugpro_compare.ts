// NOT ORIGINAL - compares our converted THAW levels with THUGPRO's own ports of them, read from the user's THUGPRO
// install (read only: <thugpro>\data\pre\<level>_scripts.prx and <level>col.prx), for online play between the two
// (docs/pending/thaw.md "THUGPRO"). What has to agree for a THUGPRO player and one of ours to share a level:
//   - the load script's name (the change-level message 0x46 / start info 0x33 carry its checksum): Load_<level>
//     (ours: thaw_levels.levelName; THUGPRO's: THUGPRO_NAMES, only to find its files - reported when they differ);
//   - the world frame: skater updates carry world positions, so every node (and the scene / collision with them) has
//     to sit where THUGPRO's does;
//   - the King of the Hill crowns in the same order (the crown is picked by its index among the Crown nodes) and at
//     the same places; the game-mode nodes under the names THUG2's scripts look for (TRG_CTF_Red...).
// Also reported: the multiplayer restarts (order and places), and how many of each side's collision faces can't be
// found through their BSP trees (tools/level_port/col_check.ts).
//
//   node tools/thaw_levels/thugpro_compare.ts [levels...] [--thug2 C:\Games\THUG2-1] [--thugpro <dir>] [--json out]

import * as fs from "node:fs";
import { parseArgs } from "node:util";
import * as colCheck from "../level_port/col_check.ts";
import * as prx from "../level_port/prx.ts";
import * as qb from "../level_port/qb.ts";
import { Name, type QbValue, Struct, Vector } from "../level_port/qb.ts";
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
import { COMPAT, findPath, LEVELS, levelName, STAGING } from "./thaw_levels.ts";

// THUGPRO's names for the levels it renamed (its data\pre file names; the others are the zone's): to find its files.
const THUGPRO_NAMES = COMPAT.levelNames; // (tools/compat/thugpro/thaw_levels.json)
const DEFAULT_THUGPRO = pyJoin(process.env.LOCALAPPDATA ?? "", "THUG Pro");
export const TOLERANCE = 0.1; // units (the frames differ by under 1/32: ours in whole sixteenths)
export const CTF_NAMES = [
    ...["Red", "Blue", "Green", "Yellow"].flatMap((c) => ["", "_Base"].map((s) => `TRG_CTF_${c}${s}`)),
    ...["Red", "Blue", "Green", "Yellow"].map((c) => `TRG_Flag_${c}`),
];

// The file's node array, each node's compressed components (a flag naming a global structure: THUG2's
// ncomp_* - THUGPRO's compiler makes them) merged in.
export function nodeArray(data: Buffer, label: string): [QbValue[], Map<number, string>] {
    const q = new qb.QbFile(data, label, true); // (THUG2 random tokens carry weights)
    let array: QbValue[] | undefined = q.globals.find(
        ([k, v]) => Array.isArray(v) && (q.names.get(k.value) ?? "").toLowerCase().endsWith("_nodearray"),
    )?.[1] as QbValue[] | undefined;
    if (array === undefined) {
        array = (q.globals.find(([, v]) => Array.isArray(v) && v.length && v[0] instanceof Struct)?.[1] ??
            []) as QbValue[];
    }
    const structs = new Map<number, Struct>();
    for (const [k, v] of q.globals) if (v instanceof Struct) structs.set(k.value, v);
    const out: QbValue[] = [];
    for (let node of array) {
        if (node instanceof Struct) {
            const entries: qb.Entry[] = [];
            for (const [key, value] of node.entries) {
                const included = key === null && value instanceof Name ? structs.get(value.value) : undefined;
                if (included !== undefined) entries.push(...included.entries);
                else entries.push([key, value]);
            }
            node = new Struct(entries);
        }
        out.push(node);
    }
    return [out, q.names];
}

// In-game position: THUG2 takes Pos as it is and negates z of the older Position.
export function position(node: Struct): readonly number[] | null {
    let v = node.get("Pos");
    if (v instanceof Vector && v.kind === "vector") return v.values;
    v = node.get("Position");
    if (v instanceof Vector && v.kind === "vector") return [v.values[0]!, v.values[1]!, -v.values[2]!];
    return null;
}

export function nameOf(node: Struct): number | null {
    const v = node.get("Name");
    return v instanceof Name ? v.value : null;
}

export const isName = (v: QbValue | undefined, text: string): boolean =>
    v instanceof Name && v.value === qb.checksum(text);

export const isCrown = (node: Struct): boolean => isName(node.get("Type"), "Crown");

export function isMpRestart(node: Struct): boolean {
    const types = node.get("restart_types");
    return isName(node.get("Class"), "Restart") && Array.isArray(types) && types.some((t) => isName(t, "Multiplayer"));
}

export type Placed = [number | null, readonly number[] | null];

export function sequence(array: readonly QbValue[], test: (n: Struct) => boolean): Placed[] {
    return array.filter((n): n is Struct => n instanceof Struct && test(n)).map((n) => [nameOf(n), position(n)]);
}

export const maxDiff = (a: readonly number[], b: readonly number[]): number =>
    Math.max(...[0, 1, 2].map((k) => Math.abs(a[k]! - b[k]!)));

// [verdict, differing count]
export function sameSequence(a: readonly Placed[], b: readonly Placed[]): [string, number] {
    if (a.length !== b.length) return [`count ${a.length} vs ${b.length}`, Math.max(a.length, b.length)];
    let bad = 0;
    a.forEach(([, pa], i) => {
        const pb = b[i]![1];
        if (pa === null || pb === null || maxDiff(pa, pb) > TOLERANCE) bad += 1;
    });
    return [bad ? `${bad} of ${a.length} differ` : "same", bad];
}

const prxFile = (folder: string, wanted: string): string | null => findPath(folder, "data", "pre", wanted);

export type Result = Record<string, unknown> & { zone: string; level: string };

export function compare(zone: string, oursDir: string, thugpro: string): Result {
    const mine = levelName(zone);
    const key = THUGPRO_NAMES.get(zone) ?? zone.toLowerCase();
    const out: Result = { zone, level: mine, thugpro_level: key, same_name: mine === key };
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
    if (data === null) throw new TypeError(`${scripts}: no levels\\${key}\\${key}.qb`); // (Python: QbFile(None))
    const [theirs, theirNames] = nodeArray(data, `${key}.qb`);
    for (const [k, v] of theirNames) names.set(k, v);
    // positions by name
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
    for (const n of ours) {
        if (!(n instanceof Struct)) continue;
        const p = position(n);
        const nm = nameOf(n);
        if (p === null || nm === null || !byName.has(nm)) continue;
        shared += 1;
        const d = maxDiff(p, byName.get(nm)!);
        if (d <= TOLERANCE) same += 1;
        else if (d > worst[0]) worst = [d, names.get(nm) ?? hex8(nm)];
    }
    out.nodes_ours = ours.length;
    out.nodes_thugpro = theirs.length;
    out.shared = shared;
    out.same_place = same;
    out.worst = [new Float(pyRound(worst[0], 3)), worst[1]];
    out.crowns = sameSequence(sequence(ours, isCrown), sequence(theirs, isCrown))[0];
    out.mp_restarts = sameSequence(sequence(ours, isMpRestart), sequence(theirs, isMpRestart))[0];
    const ourNames = new Set(ours.filter((n): n is Struct => n instanceof Struct).map(nameOf));
    const theirNameSet = new Set(theirs.filter((n): n is Struct => n instanceof Struct).map(nameOf));
    out.ctf_ours = CTF_NAMES.filter((c) => ourNames.has(qb.checksum(c))).length;
    out.ctf_thugpro = CTF_NAMES.filter((c) => theirNameSet.has(qb.checksum(c))).length;
    // collision
    const ourCol = pyJoin(oursDir, mine, `${mine}.col.xbx`);
    if (fs.existsSync(ourCol)) {
        const [faces, missing] = colCheck.check(thug2Col.readCol(fs.readFileSync(ourCol), ourCol));
        out.col_ours = [faces, missing];
    }
    const theirCol = prxFile(thugpro, `${key}col.prx`);
    if (theirCol) {
        for (const [name, cdata] of colCheck.colFiles(theirCol)) {
            const [faces, missing] = colCheck.check(thug2Col.readCol(cdata, name));
            out.col_thugpro = [faces, missing];
        }
    }
    return out;
}

const USAGE =
    "usage: thugpro_compare.ts [-h] [--thug2 THUG2] [--thugpro THUGPRO] [--staging STAGING] [--json JSON] [levels ...]";

const OPTIONS = {
    thug2: { type: "string" },
    thugpro: { type: "string" },
    staging: { type: "string" }, // our converted levels (default <thug2>\thug2-decomp-thaw\levels)
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
    let zones = LEVELS.map((l) => l[0]);
    if (parsed.positionals.length) {
        const wanted = new Set(parsed.positionals.map((x) => x.toLowerCase()));
        zones = zones.filter((z) => wanted.has(z.toLowerCase()) || wanted.has(z.toLowerCase().slice(2)));
    }
    const results: Result[] = [];
    let bad = 0;
    for (const zone of zones) {
        const r = compare(zone, oursDir, thugpro);
        results.push(r);
        if ("error" in r) {
            print(`${pad(zone, 6)} ${pad(r.level, 10)} ${String(r.error)}`);
            bad += 1;
            continue;
        }
        const worst = r.worst as [Float, string | null];
        const colOurs = (r.col_ours as number[] | undefined) ?? ["-", "-"];
        const colTheirs = (r.col_thugpro as number[] | undefined) ?? ["-", "-"];
        print(
            `${pad(zone, 6)} ${pad(r.level, 6)}${r.same_name ? "" : ` (THUGPRO: ${String(r.thugpro_level)})`} ` +
                `nodes ${r.nodes_ours}/${r.nodes_thugpro}, ${r.shared} shared, ${r.same_place} at the same place ` +
                `(worst ${formatFixed(worst[0].value, 3)} ${worst[1] || ""}); crowns ${r.crowns}; mp restarts ` +
                `${r.mp_restarts}; ctf nodes ${r.ctf_ours}/${r.ctf_thugpro}; collision unreachable ours ` +
                `${colOurs[1]}, THUGPRO ${colTheirs[1]}`,
        );
        const shared = r.shared as number;
        if (shared - (r.same_place as number) > Math.floor(shared / 100) || r.crowns !== "same") bad += 1;
    }
    if (v.json) writeTextPy(v.json, pyJsonDumps(results, 1));
    return bad ? 1 : 0;
}

if (isMain(import.meta.url)) await runMain(() => main());
