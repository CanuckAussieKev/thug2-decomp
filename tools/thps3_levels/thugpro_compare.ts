// NOT ORIGINAL - compares our converted THPS3 levels with THUGPRO's own ports of them, read from the user's THUGPRO
// install (read only: <thugpro>\data\pre\<level>_scripts.prx and <level>col.prx), for online play between the two
// (docs/pending/thps3-thugpro.md). The THAW twin is tools/thaw_levels/thugpro_compare.ts; its pieces are reused here.
//
// What has to agree for a THUGPRO player and one of ours to share a level (see that tool's header): the load script's
// name (0x46 / 0x33 carry its checksum only), the world frame (0x23 skater updates carry world positions), the King of
// the Hill crowns in the same order and places, the game-mode nodes under the names THUG2's scripts look for. Also
// reported: the multiplayer restarts (which only one side has), the node classes, the nodes with trigger scripts only
// one side has (what THUGPRO brings that we don't, and the other way round), the scripts, the collision faces that
// can't be found through their BSP trees.
//
// THUGPRO doesn't port THPS3's Canada, Airport and Los Angeles: it plays THUG2's own classic versions (CA, AP, LA -
// THUG2's Levels.qb has game=THPS3 on them). Those three are compared with THUG2's own (its Data\pre) for information.
//
//   node tools/thps3_levels/thugpro_compare.ts [levels...] [--thug2 C:\Games\THUG2-1] [--thugpro <dir>]
//        [--staging <folder>] [--json out] [--details]

import * as fs from "node:fs";
import { parseArgs } from "node:util";
import * as colCheck from "../level_port/col_check.ts";
import * as portedLevels from "../level_port/ported_levels.ts";
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
import { STAGING } from "./thps3_levels.ts";

const DEFAULT_THUGPRO = pyJoin(process.env.LOCALAPPDATA ?? "", "THUG Pro");
// THPS3 levels THUGPRO leaves to THUG2's own classic remakes (compared with THUG2's files instead).
const THUG2_OWN: ReadonlyMap<string, string> = new Map([
    ["can", "CA"],
    ["ap", "AP"],
    ["la", "LA"],
]);

// THPS3 levels THUGPRO doesn't port (its Levels.qb, 2026-10-20): the tutorials, the skate shop, the park editor's sets.
const NO_COUNTERPART: ReadonlySet<string> = new Set(["tut", "skateshop", "sk3ed_bch", "sk3ed_indr", "sk3ed_schl"]);

export type Result = Record<string, unknown> & { folder: string; level: string };

interface Side {
    array: QbValue[];
    names: Map<number, string>;
    scripts: Set<number>;
}

function readSide(files: [Buffer, string][]): Side {
    let array: QbValue[] = [];
    const names = new Map<number, string>();
    const scripts = new Set<number>();
    for (const [data, label] of files) {
        const [nodes, n] = nodeArray(data, label);
        if (nodes.length > array.length) array = nodes;
        for (const [k, v] of n) names.set(k, v);
        for (const k of new qb.QbFile(data, label, true).scripts.keys()) scripts.add(k);
    }
    return { array, names, scripts };
}

const structs = (array: readonly QbValue[]): Struct[] => array.filter((n): n is Struct => n instanceof Struct);

function text(value: QbValue | undefined, names: ReadonlyMap<number, string>): string {
    return value instanceof Name ? (names.get(value.value) ?? hex8(value.value)) : "-";
}

function classCounts(array: readonly QbValue[], names: ReadonlyMap<number, string>): Map<string, number> {
    const out = new Map<string, number>();
    for (const n of structs(array)) {
        const key = text(n.get("Class"), names).toLowerCase();
        out.set(key, (out.get(key) ?? 0) + 1);
    }
    return out;
}

// Node name -> trigger script name, for the nodes with one.
function triggers(array: readonly QbValue[], names: ReadonlyMap<number, string>): Map<number, string> {
    const out = new Map<number, string>();
    for (const n of structs(array)) {
        const nm = nameOf(n);
        const t = n.get("TriggerScript");
        if (nm !== null && t instanceof Name) out.set(nm, text(t, names));
    }
    return out;
}

function mpRestartNames(array: readonly QbValue[]): Set<number> {
    return new Set(
        structs(array)
            .filter(isMpRestart)
            .map(nameOf)
            .filter((n): n is number => n !== null),
    );
}

const nameList = (keys: Iterable<number>, names: ReadonlyMap<number, string>): string[] =>
    [...keys].map((k) => names.get(k) ?? hex8(k)).sort();

export function compare(entry: portedLevels.LevelEntry, oursDir: string, thugpro: string, thug2: string): Result {
    const folder = String(entry.folder ?? entry.level);
    const mine = entry.level;
    const own = THUG2_OWN.get(folder.toLowerCase());
    const key = own ?? folder.toLowerCase();
    const out: Result = {
        folder,
        level: mine,
        reference: own ? `THUG2's own ${own}` : `THUGPRO ${key}`,
        // (a checksum is case-blind: Load_Foun = load_foun)
        same_load_script: qb.checksum(`Load_${mine}`) === qb.checksum(`Load_${key}`),
    };
    if (!own && NO_COUNTERPART.has(folder.toLowerCase())) {
        // (THUGPRO's "skateshop" is THUG1's, its front end)
        out.error = "THUGPRO has no port of it";
        return out;
    }
    const oursQb = pyJoin(oursDir, mine, `${mine}.qb`);
    if (!fs.existsSync(oursQb)) {
        out.error = `not converted (${oursQb})`;
        return out;
    }
    const scriptsPrx = own
        ? findPath(thug2, "Data", "pre", `${key}_scripts.prx`)
        : findPath(thugpro, "data", "pre", `${key}_scripts.prx`);
    if (!scriptsPrx) {
        out.error = `no ${key}_scripts.prx (${own ? "THUG2" : "THUGPRO"})`;
        return out;
    }
    const theirFiles: [Buffer, string][] = [];
    for (const [name, data] of prx.entries(scriptsPrx, (n) => n.toLowerCase().endsWith(".qb"))) {
        const low = name.toLowerCase();
        if (low === `levels\\${key}\\${key}.qb`.toLowerCase() || low.endsWith("_scripts.qb"))
            theirFiles.push([data, name]);
    }
    const ours = readSide([[fs.readFileSync(oursQb), oursQb]]);
    const theirs = readSide(theirFiles);
    const names = new Map([...theirs.names, ...ours.names]);

    // positions by name
    const byName = new Map<number, readonly number[]>();
    for (const n of structs(theirs.array)) {
        const nm = nameOf(n);
        const p = position(n);
        if (nm !== null && p !== null && !byName.has(nm)) byName.set(nm, p);
    }
    let shared = 0;
    let same = 0;
    let worst: [number, string | null] = [0.0, null];
    for (const n of structs(ours.array)) {
        const p = position(n);
        const nm = nameOf(n);
        if (p === null || nm === null || !byName.has(nm)) continue;
        shared += 1;
        const d = maxDiff(p, byName.get(nm)!);
        if (d <= TOLERANCE) same += 1;
        else if (d > worst[0]) worst = [d, names.get(nm) ?? hex8(nm)];
    }
    out.nodes_ours = ours.array.length;
    out.nodes_theirs = theirs.array.length;
    out.shared = shared;
    out.same_place = same;
    out.worst = [new Float(pyRound(worst[0], 3)), worst[1]];
    out.crowns = sameSequence(sequence(ours.array, isCrown), sequence(theirs.array, isCrown))[0];
    out.mp_restarts = sameSequence(sequence(ours.array, isMpRestart), sequence(theirs.array, isMpRestart))[0];
    const ourMp = mpRestartNames(ours.array);
    const theirMp = mpRestartNames(theirs.array);
    out.mp_restarts_only_ours = nameList(
        [...ourMp].filter((k) => !theirMp.has(k)),
        names,
    );
    out.mp_restarts_only_theirs = nameList(
        [...theirMp].filter((k) => !ourMp.has(k)),
        names,
    );
    const ourNames = new Set(structs(ours.array).map(nameOf));
    const theirNameSet = new Set(structs(theirs.array).map(nameOf));
    const ctf = [
        ...CTF_NAMES,
        ...["Red", "Blue", "Green", "Yellow"].map((c) => `TRG_CTF_Restart_${c}`),
        "TRG_Team_Restart",
    ];
    out.ctf_ours = ctf.filter((c) => ourNames.has(qb.checksum(c)));
    out.ctf_theirs = ctf.filter((c) => theirNameSet.has(qb.checksum(c)));
    // the game-mode nodes both have: at the same place?
    out.ctf_moved = ctf
        .filter((c) => ourNames.has(qb.checksum(c)) && byName.has(qb.checksum(c)))
        .filter((c) => {
            const n = structs(ours.array).find((x) => nameOf(x) === qb.checksum(c));
            const p = n ? position(n) : null;
            return p === null || maxDiff(p, byName.get(qb.checksum(c))!) > TOLERANCE;
        });
    // classes
    const oc = classCounts(ours.array, names);
    const tc = classCounts(theirs.array, names);
    const classes: Record<string, string> = {};
    for (const k of [...new Set([...oc.keys(), ...tc.keys()])].sort()) {
        if ((oc.get(k) ?? 0) !== (tc.get(k) ?? 0)) classes[k] = `${oc.get(k) ?? 0}/${tc.get(k) ?? 0}`;
    }
    out.classes_differ = classes;
    // trigger scripts
    const ot = triggers(ours.array, names);
    const tt = triggers(theirs.array, names);
    out.triggers = `${ot.size}/${tt.size}`;
    const onlyTheirs = new Map<string, number>();
    for (const [node, script] of tt) {
        if (!ot.has(node) && ourNames.has(node)) onlyTheirs.set(script, (onlyTheirs.get(script) ?? 0) + 1);
    }
    out.trigger_scripts_only_theirs = [...onlyTheirs].sort((a, b) => b[1] - a[1]).map(([s, c]) => `${s} x${c}`);
    const onlyOurs = new Map<string, number>();
    for (const [node, script] of ot) {
        if (!tt.has(node) && theirNameSet.has(node)) onlyOurs.set(script, (onlyOurs.get(script) ?? 0) + 1);
    }
    out.trigger_scripts_only_ours = [...onlyOurs].sort((a, b) => b[1] - a[1]).map(([s, c]) => `${s} x${c}`);
    out.nodes_only_theirs = nameList(
        [...theirNameSet].filter((k): k is number => k !== null && !ourNames.has(k)),
        names,
    );
    out.scripts = `${ours.scripts.size}/${theirs.scripts.size}`;
    // collision
    const ourCol = pyJoin(oursDir, mine, `${mine}.col.xbx`);
    if (fs.existsSync(ourCol)) {
        const [faces, missing] = colCheck.check(thug2Col.readCol(fs.readFileSync(ourCol), ourCol));
        out.col_ours = [faces, missing];
    }
    const theirCol = own
        ? findPath(thug2, "Data", "pre", `${key}col.prx`)
        : findPath(thugpro, "data", "pre", `${key}col.prx`);
    if (theirCol) {
        let faces = 0;
        let missing = 0;
        for (const [name, cdata] of colCheck.colFiles(theirCol)) {
            const [f, m] = colCheck.check(thug2Col.readCol(cdata, name));
            faces += f;
            missing += m;
        }
        out.col_theirs = [faces, missing];
    }
    return out;
}

const USAGE =
    "usage: thugpro_compare.ts [-h] [--thug2 THUG2] [--thugpro THUGPRO] [--staging STAGING] [--json JSON] [--details] [levels ...]";

const OPTIONS = {
    thug2: { type: "string" },
    thugpro: { type: "string" },
    staging: { type: "string" }, // our converted levels (default <thug2>\thug2-decomp-thps3)
    json: { type: "string" },
    details: { type: "boolean" },
    help: { type: "boolean", short: "h" },
} as const;

export function main(argv: readonly string[] = process.argv.slice(2)): number {
    let parsed: ReturnType<typeof parseArgs<{ options: typeof OPTIONS; allowPositionals: true }>>;
    try {
        parsed = parseArgs({ args: [...argv], options: OPTIONS, allowPositionals: true, strict: true });
    } catch (e) {
        process.stderr.write(`${USAGE}\nthugpro_compare.ts: error: ${(e as Error).message}\n`);
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
    const thugpro = v.thugpro ?? DEFAULT_THUGPRO;
    const staging = v.staging || pyJoin(thug2, STAGING);
    const manifest = portedLevels.readManifest(pyJoin(staging, portedLevels.MANIFEST));
    if (!manifest) pyExit(`${staging}: no ${portedLevels.MANIFEST} (convert first)`);
    let entries = manifest.levels;
    if (parsed.positionals.length) {
        const wanted = new Set(parsed.positionals.map((x) => x.toLowerCase()));
        entries = entries.filter(
            (e) => wanted.has(String(e.folder).toLowerCase()) || wanted.has(e.level.toLowerCase()),
        );
    }
    const results: Result[] = [];
    let bad = 0;
    for (const entry of entries) {
        const r = compare(entry, pyJoin(staging, "levels"), thugpro, thug2);
        results.push(r);
        if ("error" in r) {
            print(`${r.folder.padEnd(10)} ${r.level.padEnd(16)} ${String(r.error)}`);
            continue;
        }
        const worst = r.worst as [Float, string | null];
        const colOurs = (r.col_ours as number[] | undefined) ?? ["-", "-"];
        const colTheirs = (r.col_theirs as number[] | undefined) ?? ["-", "-"];
        print(
            `${r.folder.padEnd(10)} ${r.level.padEnd(16)} vs ${String(r.reference)}: load script ${r.same_load_script ? "same" : "DIFFERS"}; ` +
                `nodes ${r.nodes_ours}/${r.nodes_theirs}, ${r.shared} shared, ${r.same_place} at the same place ` +
                `(worst ${formatFixed(worst[0].value, 3)} ${worst[1] || ""}); crowns ${r.crowns}; mp restarts ` +
                `${r.mp_restarts}; ctf ${(r.ctf_ours as string[]).length}/${(r.ctf_theirs as string[]).length}` +
                `${(r.ctf_moved as string[]).length ? ` (moved: ${(r.ctf_moved as string[]).join(" ")})` : ""}; triggers ${r.triggers}; ` +
                `scripts ${r.scripts}; collision unreachable ours ${colOurs[1]}, theirs ${colTheirs[1]}`,
        );
        if (v.details) {
            for (const k of [
                "mp_restarts_only_ours",
                "mp_restarts_only_theirs",
                "ctf_ours",
                "ctf_theirs",
                "classes_differ",
                "trigger_scripts_only_theirs",
                "trigger_scripts_only_ours",
            ]) {
                const value = r[k];
                const shown = Array.isArray(value) ? value.join(", ") : JSON.stringify(value);
                if (shown && shown !== "{}") print(`    ${k}: ${shown}`);
            }
            const only = r.nodes_only_theirs as string[];
            if (only.length)
                print(
                    `    nodes_only_theirs (${only.length}): ${only.slice(0, 40).join(", ")}${only.length > 40 ? " ..." : ""}`,
                );
        }
        if (!r.same_load_script && !THUG2_OWN.has(r.folder.toLowerCase())) bad += 1;
        if (r.crowns !== "same" && !THUG2_OWN.has(r.folder.toLowerCase())) bad += 1;
    }
    if (v.json) writeTextPy(v.json, pyJsonDumps(results, 1));
    return bad ? 1 : 0;
}

if (isMain(import.meta.url)) await runMain(() => main());
