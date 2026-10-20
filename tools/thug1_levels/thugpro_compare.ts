// NOT ORIGINAL - compares our converted THUG1 levels with THUGPRO's own ports of them, read from the user's THUGPRO
// install (read only: <thugpro>\data\pre\<level>_scripts.prx, <level>col.prx and thugpro_qb.prx's Levels.qb), and both
// with THUG1's own node arrays (C:\Games\THUG1, read only), for online play between the two (docs/pending/thug1.md
// "THUGPRO"). What has to agree for a THUGPRO player and one of ours to share a level, and what else is reported, as
// tools/thaw_levels/thugpro_compare.ts (whose pieces this uses):
//   - the load script (0x46 / 0x33 carry its checksum): ours Load_<thug1_levels.levelName>, THUGPRO's the load_script
//     of its level structure for the same THUG1 level (found by the level's name=, else its folder);
//   - the world frame (skater updates carry world positions): every node shared by name at the same place;
//   - King of the Hill crowns in the same order and places; the game-mode nodes (CTF, flags, team restart);
//   - the multiplayer restarts (order, places, and which differ by name), collision faces unreachable through their
//     BSP trees (tools/level_port/col_check.ts);
//   - an audit of the level's life: node classes, the trigger scripts THUGPRO's nodes run that ours don't, the level
//     scripts THUGPRO defines that we don't bring;
//   - THUGPRO's hand edits the profile applies (TP-32 ... TP-39, hand_edits.checkEdits): per register entry, how many
//     show in our converted level as THUGPRO has them ("hand edits as THUGPRO's: TP-32 5/5, ..."), with --details why
//     the others don't (a node our converter leaves out, a key it doesn't keep).
//   - what is new in THUGPRO (TPN-THUG1-1 ... 4, thugpro_new.checkNew): each model, node, script and file as THUGPRO
//     has it in our converted level ("new in THUGPRO as THUGPRO's: TPN-THUG1-1 4/4, ..."), with --details why not.
//
//   node tools/thug1_levels/thugpro_compare.ts [levels...] [--thug2 C:\Games\THUG2-3] [--thug1 C:\Games\THUG1]
//        [--thugpro <dir>] [--staging <dir>] [--legacy-names] [--compat thugpro|none] [--json out] [--details]
//   node tools/thug1_levels/thugpro_compare.ts --extract-thugpro [levels...]
//        (writes THUGPRO's edits into tools/compat/thugpro/thug1_levels.json - never typed in: TP-31's "spawns"
//        (thugpro_edits.spawnEdits), TP-32 ... TP-39's sections (thugpro_edits.handEdits) and thugpro_new (thugpro_new.extract,
//        against --staging's converted levels); prints what it leaves out
//        and why: translation by THUGPRO's toolchain, THUGPRO's own content)
//   --details also lists THUGPRO's other edits against THUG1's own data, by kind (thugpro_edits.inventory).

import * as fs from "node:fs";
import * as path from "node:path";
import * as colCheck from "../level_port/col_check.ts";
import * as prx from "../level_port/prx.ts";
import * as qb from "../level_port/qb.ts";
import { Name, type QbValue, Struct } from "../level_port/qb.ts";
import * as scriptPort from "../level_port/script_port.ts";
import * as thug2Col from "../level_port/thug2_col.ts";
import { parseCli } from "../lib/cli.ts";
import {
    Float,
    formatFixed,
    hex8,
    isMain,
    print,
    pyJoin,
    pyJsonDumps,
    pyRound,
    runMain,
    writeTextPy,
} from "../lib/py.ts";
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
import { checkEdits, editCounts, SECTIONS, type SectionCheck } from "./hand_edits.ts";
import { BY_CODE, COMPAT, LEVELS, levelNames, levelStructs, prePath, STAGING, setCompat } from "./thug1_levels.ts";
import {
    handEdits,
    type Item,
    inventory,
    isSpawn,
    nodeName,
    spawnEdits,
    thug1Side,
    thugproSide,
} from "./thugpro_edits.ts";
import * as thugproNew from "./thugpro_new.ts";

const DEFAULT_THUGPRO = pyJoin(process.env.LOCALAPPDATA ?? "", "THUG Pro");
// The game-mode nodes THUG2's scripts look up by name: CTF bases / flags, their restarts, the team restart.
export const MODE_NAMES = [
    ...CTF_NAMES,
    ...["Red", "Blue", "Green", "Yellow"].map((c) => `TRG_CTF_Restart_${c}`),
    ...["Red", "Blue", "Green", "Yellow"].map((c) => `TRG_Flag_${c}_Base`),
    "TRG_Team_Restart",
];

// THUGPRO's level structures (its Levels.qb in thugpro_qb.prx): [structure, name text] by lower-case level=.
interface TheirLevel {
    level: string;
    load: number;
    loadText: string;
    name: string;
    game: string;
}

export function thugproLevels(thugpro: string): TheirLevel[] {
    const archive = pyJoin(thugpro, "data", "pre", "thugpro_qb.prx");
    const data = fs.existsSync(archive) ? prx.find(archive, "qb\\game\\levels.qb") : null;
    if (data === null) return [];
    const q = new qb.QbFile(data, "Levels.qb", true);
    const out: TheirLevel[] = [];
    for (const [, v] of q.globals) {
        if (!(v instanceof Struct)) continue;
        const level = v.get("level");
        const load = v.get("load_script");
        const name = v.get("name");
        const game = v.get("game");
        if (typeof level !== "string" || !(load instanceof Name)) continue;
        out.push({
            level: level.toLowerCase(),
            load: load.value,
            loadText: q.names.get(load.value) ?? hex8(load.value),
            name: typeof name === "string" ? name : "",
            game: game instanceof Name ? (q.names.get(game.value) ?? "") : "",
        });
    }
    return out;
}

// THUGPRO's port of a THUG1 level: the THUG1 / THPS2 structure with the same name= (Hotter Than Hell is its hh),
// else the one in the THUG1 level's own folder, else any in that folder (THUGPRO's SC / PH / DJ: THUG2's own levels).
export function theirLevel(all: readonly TheirLevel[], code: string, thug1Name: string): TheirLevel | null {
    const ported = all.filter((t) => t.game.startsWith("THUG1") || t.game.startsWith("THPS2"));
    return (
        ported.find((t) => t.name.toLowerCase() === thug1Name.toLowerCase()) ??
        ported.find((t) => t.level === code.toLowerCase()) ??
        all.find((t) => t.level === code.toLowerCase()) ??
        null
    );
}

const structs = (array: readonly QbValue[]): Struct[] => array.filter((n): n is Struct => n instanceof Struct);

function textOf(v: QbValue | undefined, names: ReadonlyMap<number, string>): string {
    return v instanceof Name ? (names.get(v.value) ?? hex8(v.value)) : "-";
}

function countBy(array: readonly Struct[], names: ReadonlyMap<number, string>): Map<string, number> {
    const out = new Map<string, number>();
    for (const n of array) {
        const key = textOf(n.get("Class"), names).toLowerCase();
        out.set(key, (out.get(key) ?? 0) + 1);
    }
    return out;
}

function triggerScripts(array: readonly Struct[]): Set<number> {
    const out = new Set<number>();
    for (const n of array) {
        const s = n.get("TriggerScript");
        if (s instanceof Name) out.add(s.value);
    }
    return out;
}

function mpNames(array: readonly Struct[], names: ReadonlyMap<number, string>): string[] {
    return array.filter(isMpRestart).map((n) => textOf(n.get("Name"), names));
}

export type Result = Record<string, unknown> & { code: string; level: string };

export function compare(
    code: string,
    mine: string,
    oursDir: string,
    thugpro: string,
    thug1: string,
    all: readonly TheirLevel[],
    thug1Name: string,
): Result {
    const out: Result = { code, level: mine };
    const them = theirLevel(all, code, thug1Name);
    if (them === null) {
        out.error = "THUGPRO has no port of it";
        return out;
    }
    const key = them.level;
    out.thugpro_level = key;
    out.thugpro_load = them.loadText;
    out.same_name = qb.checksum(`Load_${mine}`) === them.load;
    const oursQb = pyJoin(oursDir, mine, `${mine}.qb`);
    if (!fs.existsSync(oursQb)) {
        out.error = `not converted (${oursQb})`;
        return out;
    }
    const [oursArray, names] = nodeArray(fs.readFileSync(oursQb), oursQb);
    const oursQbFile = new qb.QbFile(fs.readFileSync(oursQb), oursQb, true);
    const scripts = pyJoin(thugpro, "data", "pre", `${key}_scripts.prx`);
    if (!fs.existsSync(scripts)) {
        out.error = `THUGPRO's ${them.loadText} (game=${them.game}) is THUG2's own level, not a port (no ${key}_scripts.prx)`;
        return out;
    }
    const theirFiles = new Map<string, Buffer>();
    for (const [n, d] of prx.entries(scripts, (n) => n.toLowerCase().endsWith(".qb")))
        theirFiles.set(n.toLowerCase(), d);
    const data = theirFiles.get(`levels\\${key}\\${key}.qb`);
    if (data === undefined) throw new TypeError(`${scripts}: no levels\\${key}\\${key}.qb`);
    const [theirArray, theirNames] = nodeArray(data, `${key}.qb`);
    for (const [k, v] of theirNames) names.set(k, v);
    // THUG1's own
    const t1pre = prePath({ thug1 }, `${code}.pre`);
    const t1data = t1pre ? prx.find(t1pre, `levels\\${code}\\${code}.qb`) : null;
    const [origArray, origNames] = t1data ? nodeArray(t1data, `${code}.qb`) : [[], new Map<number, string>()];
    for (const [k, v] of origNames) if (!names.has(k)) names.set(k, v);
    const ours = structs(oursArray);
    const theirs = structs(theirArray);
    const orig = structs(origArray);

    // positions by name
    const byName = new Map<number, readonly number[]>();
    for (const n of theirs) {
        const nm = nameOf(n);
        const p = position(n);
        if (nm !== null && p !== null && !byName.has(nm)) byName.set(nm, p);
    }
    let shared = 0;
    let same = 0;
    let worst: [number, string | null] = [0.0, null];
    const moved: [string, Float][] = [];
    for (const n of ours) {
        const p = position(n);
        const nm = nameOf(n);
        if (p === null || nm === null || !byName.has(nm)) continue;
        shared += 1;
        const d = maxDiff(p, byName.get(nm)!);
        if (d <= TOLERANCE) same += 1;
        else moved.push([names.get(nm) ?? hex8(nm), new Float(pyRound(d, 3))]);
        if (d > TOLERANCE && d > worst[0]) worst = [d, names.get(nm) ?? hex8(nm)];
    }
    out.moved = moved;
    out.nodes = [ours.length, theirs.length, orig.length];
    out.shared = shared;
    out.same_place = same;
    out.worst = [new Float(pyRound(worst[0], 3)), worst[1]];
    out.crowns = sameSequence(sequence(ours, isCrown), sequence(theirs, isCrown))[0];
    out.mp_restarts = sameSequence(sequence(ours, isMpRestart), sequence(theirs, isMpRestart))[0];
    out.mp_restarts_vs_thug1 = sameSequence(sequence(ours, isMpRestart), sequence(orig, isMpRestart))[0];
    // every network spawn (Multiplayer / Horse / CTF / Team restarts): the same names, places and order (TP-31)
    const spawnSeq = (a: readonly Struct[]) => a.filter((n) => isSpawn(n, names));
    const [sa, sb] = [spawnSeq(ours), spawnSeq(theirs)];
    const differ = sb
        .map((n, i) => [n, sa[i]] as const)
        .filter(
            ([t, o]) =>
                o === undefined ||
                nodeName(o, names).toLowerCase() !== nodeName(t, names).toLowerCase() ||
                maxDiff(position(o) ?? [0, 0, 0], position(t) ?? [0, 0, 0]) > TOLERANCE,
        )
        .map(([t]) => nodeName(t, names));
    out.spawns =
        sa.length === sb.length && !differ.length
            ? "same"
            : `${sa.length} vs ${sb.length}, differ: ${differ.join(" ")}`;
    const ourMp = mpNames(ours, names);
    const theirMp = mpNames(theirs, names);
    out.mp_ours_only = ourMp.filter((n) => !theirMp.includes(n));
    out.mp_thugpro_only = theirMp.filter((n) => !ourMp.includes(n));
    const has = (array: readonly Struct[]) => {
        const set = new Set(array.map(nameOf));
        return MODE_NAMES.filter((c) => set.has(qb.checksum(c)));
    };
    const [modeOurs, modeTheirs, modeOrig] = [has(ours), has(theirs), has(orig)];
    out.mode_nodes = [modeOurs.length, modeTheirs.length, modeOrig.length];
    out.mode_missing = modeOrig.filter((n) => !modeOurs.includes(n));
    const flags = (array: readonly Struct[]) =>
        array.filter((n) => textOf(n.get("Type"), names).toLowerCase().startsWith("flag_")).length;
    out.flag_objects = [flags(ours), flags(theirs), flags(orig)];
    // the level's life
    const classes = [countBy(ours, names), countBy(theirs, names)];
    const keys = [...new Set([...classes[0]!.keys(), ...classes[1]!.keys()])].sort();
    out.classes = Object.fromEntries(keys.map((k) => [k, [classes[0]!.get(k) ?? 0, classes[1]!.get(k) ?? 0]]));
    const ourTriggers = triggerScripts(ours);
    const theirTriggers = triggerScripts(theirs);
    out.trigger_scripts = [ourTriggers.size, theirTriggers.size];
    out.triggers_thugpro_only = [...theirTriggers]
        .filter((s) => !ourTriggers.has(s))
        .map((s) => names.get(s) ?? hex8(s))
        .sort();
    const theirDefined = new Set<number>();
    for (const [n, d] of theirFiles) {
        if (!n.startsWith(`levels\\${key}\\`)) continue;
        const f = new qb.QbFile(d, n, true);
        for (const s of f.scripts.keys()) theirDefined.add(s);
        for (const [k, v] of f.names) if (!names.has(k)) names.set(k, v);
    }
    const ourDefined = new Set(oursQbFile.scripts.keys());
    out.scripts = [ourDefined.size, theirDefined.size];
    // collision
    const ourCol = pyJoin(oursDir, mine, `${mine}.col.xbx`);
    let ourColFile: thug2Col.ColFile | null = null;
    if (fs.existsSync(ourCol)) {
        ourColFile = thug2Col.readCol(fs.readFileSync(ourCol), ourCol);
        const [faces, missing] = colCheck.check(ourColFile);
        out.col_ours = [faces, missing];
    }
    // THUGPRO's hand edits the profile applies (TP-32 ... TP-39): each as THUGPRO has it in our converted level?
    const edits = COMPAT.edits.get(code.toLowerCase());
    if (edits !== undefined) out.hand_edits = checkEdits(edits, ours, names, ourColFile);
    // What is new in THUGPRO (TPN-THUG1-1 ... 4, thugpro_new.ts): each item in our converted level as THUGPRO has it?
    const fresh = COMPAT.new.get(code.toLowerCase());
    if (fresh !== undefined) {
        const install = new thugproNew.Install(thugpro);
        out.thugpro_new = thugproNew.checkNew(
            fresh,
            path.dirname(oursDir),
            mine,
            fs.readFileSync(oursQb),
            ours,
            theirs,
            (e) => install.read(fresh, e),
        );
    }
    const theirCol = pyJoin(thugpro, "data", "pre", `${key}col.prx`);
    if (fs.existsSync(theirCol)) {
        for (const [name, cdata] of colCheck.colFiles(theirCol)) {
            const [faces, missing] = colCheck.check(thug2Col.readCol(cdata, name));
            out.col_thugpro = [faces, missing];
        }
    }
    return out;
}

const USAGE =
    "usage: thugpro_compare.ts [-h] [--thug2 THUG2] [--thug1 THUG1] [--thugpro THUGPRO] [--staging STAGING] " +
    "[--legacy-names] [--compat {thugpro,none}] [--json JSON] [--details] [--extract-thugpro] [levels ...]";

const OPTIONS = {
    thug2: { type: "string" },
    thug1: { type: "string" },
    thugpro: { type: "string" },
    staging: { type: "string" }, // our converted levels (default <thug2>\thug2-decomp-thug1\levels)
    "legacy-names": { type: "boolean" }, // ours converted with --legacy-names (thug1_<code>)
    compat: { type: "string" },
    json: { type: "string" },
    details: { type: "boolean" }, // also the classes, THUGPRO-only trigger scripts and the inventory of its edits
    // TP-31 ... TP-39: (re)write the profile's spawns and hand edits from THUGPRO's install (against THUG1's)
    "extract-thugpro": { type: "boolean" },
} as const;

export const PROFILE_FILE = new URL("../compat/thugpro/thug1_levels.json", import.meta.url);

// The THUGPRO port's key (its data\pre\<key>_scripts.prx) of every THUG1 level THUGPRO ports, by THUG1 code.
function portKeys(thug1: string, thugpro: string, codes: readonly string[]): Map<string, string> {
    const all = thugproLevels(thugpro);
    const structs = levelStructs({ thug1 });
    const out = new Map<string, string>();
    for (const code of codes) {
        const nm = structs.get(code.toLowerCase())?.get("name");
        const them = theirLevel(all, code, typeof nm === "string" ? nm : "");
        if (them && fs.existsSync(pyJoin(thugpro, "data", "pre", `${them.level}_scripts.prx`)))
            out.set(code, them.level);
    }
    return out;
}

// THUG2's models (Data\models: "veh\\a\\a.mdl" / "x\\y.skin", lower case).
function thug2Models(thug2: string): Set<string> {
    const out = new Set<string>();
    const root = path.join(thug2, "Data", "models");
    const walk = (dir: string, rel: string): void => {
        for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
            const r = rel ? `${rel}\\${e.name}` : e.name;
            if (e.isDirectory()) walk(path.join(dir, e.name), r);
            else if (/\.(mdl|skin)\.xbx$/i.test(e.name)) out.add(r.slice(0, -4).toLowerCase());
        }
    };
    if (fs.existsSync(root)) walk(root, "");
    return out;
}

// TPN-THUG1-1 ... TPN-THUG1-4: what is new in THUGPRO (thugpro_new.extract) - against our converted level in the staging (the
// scripts it defines and the nodes it has: a THUGPRO edit of a script ours leaves out isn't listed). Returns the
// section; prints what isn't taken and why.
function extractNew(
    thug1: string,
    thugpro: string,
    thug2: string,
    staging: string,
    codes: readonly string[],
): Record<string, unknown> {
    const section: Record<string, unknown> = {
        _register: "TPN-THUG1-1 ... TPN-THUG1-4",
        _comment:
            "NEW in THUGPRO - content THUGPRO made that is in no source game (the user's decision 2026-10-20, with " +
            "Morten's agreement): BY REFERENCE ONLY - references and checksums of THUGPRO's files, read from the " +
            "user's THUGPRO install when converting (--thugpro), never their bytes. Written by `node " +
            "tools/thug1_levels/thugpro_compare.ts --extract-thugpro` (thugpro_new.extract); its form: " +
            "tools/thug1_levels/thugpro_new.ts. --no-thugpro-new or --compat none leave it out.",
    };
    const install = new thugproNew.Install(thugpro);
    const models = thug2Models(thug2);
    const t2 = new scriptPort.Thug2(thug2);
    const utilsData = install.archive("data\\pre\\thugpro_qb.prx")?.get("qb\\thugpro\\thugpro_utils.qb");
    const utils = utilsData ? thugproNew.rawScripts(utilsData) : new Map<number, Buffer>();
    const utilsNames = utilsData ? new qb.QbFile(utilsData, "thugpro_utils.qb", true).names : new Map<number, string>();
    const mine = levelNames(thug1, thug2);
    const pre = path.join(thugpro, "data", "pre");
    const listing = fs.existsSync(pre) ? fs.readdirSync(pre) : [];
    for (const [code, key] of portKeys(thug1, thugpro, codes)) {
        const level = mine.get(code.toLowerCase()) ?? `thug1_${code.toLowerCase()}`;
        const oursQb = pyJoin(staging, level, `${level}.qb`);
        let ours: { scripts: Set<number>; nodes: Set<number> } | null = null;
        if (fs.existsSync(oursQb)) {
            const data = fs.readFileSync(oursQb);
            const [array] = nodeArray(data, oursQb);
            ours = {
                scripts: new Set(new qb.QbFile(data, oursQb, true).scripts.keys()),
                nodes: new Set(structs(array).flatMap((n) => (nameOf(n) === null ? [] : [nameOf(n)!]))),
            };
        } else print(`    (no converted ${oursQb}: script edits listed without checking ours has them)`);
        const archiveName = listing.find((f) => f.toLowerCase() === `${key}_scripts.prx`.toLowerCase());
        const got = thugproNew.extract({
            t1: thug1Side(thug1, code),
            tp: thugproSide(thugpro, key),
            key,
            archive: `data\\pre\\${archiveName ?? `${key}_scripts.prx`}`,
            install,
            thug2Models: models,
            thug2: t2,
            utils,
            utilsNames,
            ours,
        });
        section[code] = got.level;
        const counts = new Map<string, number>();
        for (const e of got.level.entries) counts.set(`${e.id} ${e.op}`, (counts.get(`${e.id} ${e.op}`) ?? 0) + 1);
        print(`${code}: new in THUGPRO: ${[...counts].map(([k, n]) => `${k} ${n}`).join(", ") || "none"}`);
        for (const n of got.notes) print(`    new, not taken: ${n}`);
    }
    return section;
}

function extract(thug1: string, thugpro: string, codes: readonly string[], thug2: string, staging: string): number {
    const profile = JSON.parse(fs.readFileSync(PROFILE_FILE, "utf-8")) as Record<string, unknown>;
    const section: Record<string, unknown> = {
        _register: "TP-31",
        _source:
            "THUGPRO's installed data\\pre\\<level>_scripts.prx node arrays against THUG1's own, extracted by " +
            "`node tools/thug1_levels/thugpro_compare.ts --extract-thugpro` (pos = THUG2's Pos axes; order: every " +
            "Multiplayer / Horse / CTF / Team restart in THUGPRO's order, by THUG1's names)",
    };
    // TP-32 ... TP-39: THUGPRO's other hand edits (thugpro_edits.handEdits, hand_edits.ts)
    const sections = new Map<string, Record<string, unknown>>(
        SECTIONS.map(([key, register, what]) => [
            key,
            {
                _register: register,
                _source:
                    `${what}: THUGPRO's installed data\\pre\\<level>_scripts.prx node arrays and <level>col.prx collision ` +
                    "against THUG1's own, extracted by `node tools/thug1_levels/thugpro_compare.ts --extract-thugpro` " +
                    "(thugpro_edits.handEdits; never typed in)",
            },
        ]),
    );
    for (const [code, key] of portKeys(thug1, thugpro, codes)) {
        const t1 = thug1Side(thug1, code);
        const tp = thugproSide(thugpro, key);
        const edits = spawnEdits(t1, tp);
        section[code] = edits;
        const kinds = Object.entries(edits)
            .filter(([k]) => k !== "order")
            .map(([k, v]) => `${k} ${Array.isArray(v) ? v.length : Object.keys(v as object).length}`);
        print(`${code}: ${kinds.join(", ") || "no edits"}; ${edits.order?.length ?? 0} spawns in order`);
        const hand = handEdits(t1, tp);
        for (const [k, v] of Object.entries(hand.edits)) sections.get(k)![code] = v;
        print(`    hand edits: ${editCounts(hand.edits).join(", ") || "none"}`);
        for (const n of hand.notes) print(`    not taken: ${n}`);
    }
    profile.spawns = section;
    for (const [k, v] of sections) profile[k] = v;
    // (TPN-THUG1-1 ... TPN-THUG1-4, before port_changes: the user's order - THUGPRO's data, what is new in THUGPRO, our own)
    const own = profile.port_changes;
    delete profile.port_changes;
    profile.thugpro_new = extractNew(thug1, thugpro, thug2, staging, codes);
    if (own !== undefined) profile.port_changes = own;
    fs.writeFileSync(PROFILE_FILE, `${formatJson(profile)}\n`, "utf-8");
    print(`wrote ${path.normalize(decodeURIComponent(PROFILE_FILE.pathname.slice(1)))}`);
    return 0;
}

// JSON as JSON.stringify(value, null, 2) writes it, but a list of numbers (a position, a face list), a short list of
// names and a short object on one line: the profile's thousands of face indices and names stay readable.
export function formatJson(value: unknown, indent = ""): string {
    const inner = `${indent}  `;
    if (Array.isArray(value)) {
        if (!value.length) return "[]";
        const flat = value.every((v) => typeof v === "number" || (Array.isArray(v) && v.every(Number.isFinite)));
        const line = `[${value.map((v) => JSON.stringify(v).replaceAll(",", ", ")).join(", ")}]`;
        if (flat || (value.every((v) => typeof v === "string") && line.length + indent.length <= 120)) return line;
        return `[\n${value.map((v) => inner + formatJson(v, inner)).join(",\n")}\n${indent}]`;
    }
    if (value !== null && typeof value === "object") {
        const entries = Object.entries(value);
        if (!entries.length) return "{}";
        const line = `{${entries.map(([k, v]) => `${JSON.stringify(k)}: ${formatJson(v, inner)}`).join(", ")}}`;
        if (!line.includes("\n") && line.length + indent.length <= 100) return line;
        return `{\n${entries.map(([k, v]) => `${inner}${JSON.stringify(k)}: ${formatJson(v, inner)}`).join(",\n")}\n${indent}}`;
    }
    return JSON.stringify(value);
}

// One level's hand-edit check (checkEdits) as text: per register entry, same / looked at, and with `details` the misses.
export function checkLines(checks: ReadonlyMap<string, SectionCheck>, details: boolean): string[] {
    const lines = [`hand edits as THUGPRO's: ${[...checks].map(([tp, c]) => `${tp} ${c.same}/${c.total}`).join(", ")}`];
    if (details) {
        for (const [tp, c] of checks)
            for (const [reason, list] of c.differ)
                lines.push(`  ${tp} ${reason}: ${list.length} - ${list.slice(0, 6).join(" ")}`);
    }
    return lines;
}

// The inventory's lines for one level: per kind, the count and up to `examples` names.
export function inventoryLines(items: readonly Item[], examples = 5): string[] {
    const byKind = new Map<string, Item[]>();
    for (const i of items) byKind.set(i.kind, [...(byKind.get(i.kind) ?? []), i]);
    return [...byKind]
        .sort((a, b) => (a[0] < b[0] ? -1 : 1))
        .map(
            ([kind, list]) =>
                `${kind}: ${list.length} - ${list
                    .slice(0, examples)
                    .map((i) => `${i.name}${i.detail ? ` (${i.detail.slice(0, 120)})` : ""}`)
                    .join("; ")}`,
        );
}

export function main(argv: readonly string[] = process.argv.slice(2)): number {
    const parsed = parseCli(USAGE, { args: [...argv], options: OPTIONS, allowPositionals: true, strict: true });
    const v = parsed.values;
    let thug2 = v.thug2 ?? "C:\\Games\\THUG2\\Game";
    if (!fs.existsSync(path.join(thug2, "Data")) && fs.existsSync(path.join(thug2, "Game", "Data"))) {
        thug2 = pyJoin(thug2, "Game");
    }
    const thug1 = v.thug1 ?? "C:\\Games\\THUG1";
    const thugpro = v.thugpro ?? DEFAULT_THUGPRO;
    setCompat(v.compat ?? "thugpro");
    if (v["extract-thugpro"]) {
        const wanted = parsed.positionals.length
            ? parsed.positionals.map((c) => BY_CODE.get(c.toLowerCase())?.[0] ?? c)
            : LEVELS.filter((l) => l[5]).map((l) => l[0]);
        return extract(thug1, thugpro, wanted, thug2, v.staging || pyJoin(thug2, STAGING, "levels"));
    }
    const oursDir = v.staging || pyJoin(thug2, STAGING, "levels");
    const mineByCode = levelNames(thug1, thug2, v["legacy-names"] ?? false);
    const t1structs = levelStructs({ thug1 });
    const all = thugproLevels(thugpro);
    let codes = LEVELS.filter((l) => l[5]).map((l) => l[0]);
    if (parsed.positionals.length) codes = parsed.positionals.map((c) => BY_CODE.get(c.toLowerCase())?.[0] ?? c);
    const results: Result[] = [];
    let bad = 0;
    for (const code of codes) {
        const nm = t1structs.get(code.toLowerCase())?.get("name");
        const r = compare(
            code,
            mineByCode.get(code.toLowerCase()) ?? `thug1_${code.toLowerCase()}`,
            oursDir,
            thugpro,
            thug1,
            all,
            typeof nm === "string" ? nm : "",
        );
        results.push(r);
        if ("error" in r) {
            print(`${code.padEnd(13)} ${r.level.padEnd(19)} ${String(r.error)}`);
            continue;
        }
        const worst = r.worst as [Float, string | null];
        const nodes = r.nodes as number[];
        const mode = r.mode_nodes as number[];
        const fl = r.flag_objects as number[];
        const colOurs = (r.col_ours as number[] | undefined) ?? ["-", "-"];
        const colTheirs = (r.col_thugpro as number[] | undefined) ?? ["-", "-"];
        print(
            `${code.padEnd(13)} ${r.level}${r.same_name ? "" : ` (THUGPRO: ${String(r.thugpro_load)} - differs)`}: ` +
                `nodes ${nodes[0]}/${nodes[1]} (THUG1 ${nodes[2]}), ${r.shared} shared, ${r.same_place} at the same place ` +
                `(worst ${formatFixed(worst[0].value, 3)} ${worst[1] || ""}); crowns ${r.crowns}; mp restarts ` +
                `${r.mp_restarts} (vs THUG1 ${r.mp_restarts_vs_thug1}); spawns ${String(r.spawns)}` +
                `${(r.mp_ours_only as string[]).length ? `, ours only ${(r.mp_ours_only as string[]).join(" ")}` : ""}` +
                `${(r.mp_thugpro_only as string[]).length ? `, THUGPRO only ${(r.mp_thugpro_only as string[]).join(" ")}` : ""}; ` +
                `mode nodes ${mode[0]}/${mode[1]} (THUG1 ${mode[2]}), flags ${fl[0]}/${fl[1]} (THUG1 ${fl[2]}); ` +
                `trigger scripts ${(r.trigger_scripts as number[]).join("/")}; collision unreachable ours ` +
                `${colOurs[1]}, THUGPRO ${colTheirs[1]}`,
        );
        const checks = r.hand_edits as Map<string, SectionCheck> | undefined;
        if (checks !== undefined) {
            for (const line of checkLines(checks, v.details ?? false)) print(`    ${line}`);
            // (the JSON report: plain objects)
            r.hand_edits = Object.fromEntries(
                [...checks].map(([tp, c]) => [tp, { ...c, differ: Object.fromEntries(c.differ) }]),
            );
        }
        const fresh = r.thugpro_new as Map<string, thugproNew.NewCheck> | undefined;
        if (fresh !== undefined) {
            print(
                `    new in THUGPRO as THUGPRO's: ${[...fresh].map(([id, c]) => `${id} ${c.same}/${c.total}`).join(", ")}`,
            );
            if (v.details) {
                for (const [id, c] of fresh)
                    for (const [reason, list] of c.differ)
                        print(`      ${id} ${reason}: ${list.length} - ${list.slice(0, 8).join(" ")}`);
            }
            r.thugpro_new = Object.fromEntries(
                [...fresh].map(([id, c]) => [id, { ...c, differ: Object.fromEntries(c.differ) }]),
            );
        }
        if (v.details) {
            const classes = r.classes as Record<string, number[]>;
            print(
                `    classes (ours/THUGPRO): ${Object.entries(classes)
                    .filter(([, c]) => c[0] !== c[1])
                    .map(([k, c]) => `${k} ${c[0]}/${c[1]}`)
                    .join(", ")}`,
            );
            const moved = r.moved as [string, Float][];
            if (moved.length)
                print(`    moved: ${moved.map(([n, d]) => `${n} ${formatFixed(d.value, 3)}`).join(", ")}`);
            const only = r.triggers_thugpro_only as string[];
            print(`    THUGPRO-only trigger scripts (${only.length}): ${only.join(" ")}`);
            if ((r.mode_missing as string[]).length)
                print(`    THUG1 mode nodes missing: ${(r.mode_missing as string[]).join(" ")}`);
            // (THUGPRO's edits against THUG1's own data: thugpro_edits.inventory)
            const items = inventory(thug1Side(thug1, code), thugproSide(thugpro, String(r.thugpro_level)));
            r.inventory = items;
            print("    THUGPRO's edits against THUG1:");
            for (const line of inventoryLines(items)) print(`      ${line}`);
        }
        const shared = r.shared as number;
        if (shared - (r.same_place as number) > Math.floor(shared / 100) || r.crowns !== "same" || !r.same_name)
            bad += 1;
    }
    if (v.json) writeTextPy(v.json, pyJsonDumps(results, 1));
    return bad ? 1 : 0;
}

if (isMain(import.meta.url)) await runMain(() => main());
