// NOT ORIGINAL - what is NEW in THUGPRO's ports of THUG1's levels: content THUGPRO made that is in no source game's
// level (THUG1's, or THUG2's for the same place) - its models, nodes, level scripts and host-toggled level files. Morten
// (THUGPRO's author) agreed to the user that our ports may include it (2026-10-20); the user's decision: all of it in
// ONE separate section of the differences file, `thugpro_new` in tools/compat/thugpro/thug1_levels.json, apart from the
// hand edits that are data (TP-30 ... TP-39), so that it is easily identifiable (README.md there: "New in THUGPRO (not
// in any source game)", TP-40 ... TP-43).
//
// BY REFERENCE: the section holds references and checksums, never THUGPRO's bytes (the repo is public; the permission
// doesn't cover republishing THUGPRO's files). The converter reads each item from the user's THUGPRO install
// (--thugpro, default %LOCALAPPDATA%\THUG Pro) when it converts; a missing install, file or a checksum that differs is a
// warning and the item is left out - never a failure.
//
// Section form: thugpro_new.<THUG1 code> = {key: THUGPRO's level key, archive: its data\pre\<key>_scripts.prx, entries:
// [...]}; every entry {id, kind, op, target, why, crc32, ...}:
//   TP-40 asset   op add: a model file THUG2 hasn't (models\...: target = its overlay path), from `file` (a path in
//                 THUGPRO's install) / `entry` (the file in that archive) - written to <staging>\models\ and installed into
//                 the overlay by make_menu_level (tools/level_port/ported_levels.ts installFiles).
//   TP-41 node    op add: THUGPRO's node `target` appended to THUG1's node array (Links by name);
//                 op set: THUG1's node `target` given THUGPRO's values of `fields` (THUGPRO's node `node`, when renamed;
//                 a field THUGPRO's node hasn't is taken off). Both kept whole by our converter (thug1_port.NodePass
//                 `forced`: they are THUG2 nodes already). crc32: of the node's (or the fields') JSON form.
//   TP-42 script  op add: THUGPRO's script `target` added to the level's .qb (from `file`: a qb file in the level's
//                 archive, or `archive` + `file` - thugpro_qb.prx's qb\thugpro\thugpro_utils.qb); op set: our script
//                 replaced by THUGPRO's; op remove: our script emptied (THUGPRO emptied it). Applied only when safe in THUG2
//                 (scriptCheck: every command it calls THUG2 has or the level defines, none of THUG1's goal / flag
//                 commands, every node it names in the level) - else left out with a reason. crc32: THUGPRO's bytes.
//   TP-43 file    op add: THUGPRO's host-toggled level file (levels\<level>\<level>_thugpro.qb: its "level changes",
//                 switched by THUGPRO's protocol messages 155-159 - nothing runs them until a protocol layer does, so the
//                 default state, all off, is THUGPRO's default).
// Written by `node tools/thug1_levels/thugpro_compare.ts --extract-thugpro` (extract), never typed in.

import * as fs from "node:fs";
import * as path from "node:path";
import * as zlib from "node:zlib";
import { FormatError } from "../level_port/binio.ts";
import * as prx from "../level_port/prx.ts";
import * as qb from "../level_port/qb.ts";
import { Name, type QbValue, Struct } from "../level_port/qb.ts";
import * as scriptPort from "../level_port/script_port.ts";
import { pack } from "../lib/struct.ts";
import * as handEdits from "./hand_edits.ts";
import type { Op } from "./port_changes.ts";
import { LEFT_OUT_PREFIXES } from "./thug1_port.ts";
import { isSpawn, nodeName, type Side } from "./thugpro_edits.ts";

export type Kind = "asset" | "node" | "script" | "file";

export interface NewEntry {
    id: string;
    kind: Kind;
    op: Op;
    target: string;
    why: string;
    crc32: string;
    file?: string; // asset / file / script: where in THUGPRO's install (a file, or the file inside `archive`)
    archive?: string; // the archive `file` is in (a path in THUGPRO's install; the level's when not given)
    size?: number;
    node?: string; // node set: THUGPRO's name for it (renamed)
    fields?: string[]; // node set: the fields taken from THUGPRO's node
}

export interface NewLevel {
    key: string; // THUGPRO's level (its levels\<key>\)
    archive: string; // data\pre\<key>_scripts.prx (as the install names it)
    entries: NewEntry[];
}

export const IDS = {
    asset: "TP-40",
    node: "TP-41",
    script: "TP-42",
    file: "TP-43",
} as const satisfies Record<Kind, string>;

export const crc = (data: Uint8Array): string => (zlib.crc32(data) >>> 0).toString(16).padStart(8, "0");

// --- reading THUGPRO's install ---------------------------------------------------------------------------------------

// THUGPRO's files by reference, read once per archive (lower-case names).
export class Install {
    readonly root: string;
    private readonly archives = new Map<string, Map<string, Buffer> | null>();

    constructor(root: string) {
        this.root = root;
    }

    exists(): boolean {
        return fs.existsSync(path.join(this.root, "data", "pre"));
    }

    archive(rel: string): Map<string, Buffer> | null {
        const key = rel.toLowerCase();
        if (!this.archives.has(key)) {
            const file = path.join(this.root, rel);
            let files: Map<string, Buffer> | null = null;
            if (fs.existsSync(file)) {
                files = new Map();
                for (const [n, d] of prx.entries(file)) files.set(n.toLowerCase().replaceAll("/", "\\"), d);
            }
            this.archives.set(key, files);
        }
        return this.archives.get(key)!;
    }

    // An entry's bytes, or the reason it can't be had.
    read(level: NewLevel, e: NewEntry): Buffer | string {
        let data: Buffer | undefined;
        const archive = e.archive ?? level.archive;
        if (e.kind === "asset" && e.archive === undefined && e.file?.toLowerCase().startsWith("data\\models\\")) {
            const file = path.join(this.root, e.file);
            if (!fs.existsSync(file)) return `${e.file} not in THUGPRO's install`;
            data = fs.readFileSync(file);
        } else {
            const files = this.archive(archive);
            if (files === null) return `${archive} not in THUGPRO's install`;
            data = files.get((e.file ?? "").toLowerCase());
            if (data === undefined) return `${e.file} not in ${archive}`;
        }
        return data;
    }
}

// --- scripts ---------------------------------------------------------------------------------------------------------

// A THUG2-format .qb's scripts as they are (0x23 name ... 0x24, then a line end): name -> bytes.
export function rawScripts(data: Uint8Array): Map<number, Buffer> {
    const buf = Buffer.from(data);
    const toks = [...qb.tokens(buf, true)];
    const out = new Map<number, Buffer>();
    for (let i = 0; i < toks.length; i++) {
        const [start, t] = toks[i]!;
        if (t !== 0x23 || toks[i + 1]?.[1] !== 0x16) continue;
        const name = toks[i + 1]![2].readUInt32LE(0);
        let j = i + 2;
        while (j < toks.length && toks[j]![1] !== 0x24) j++;
        if (j >= toks.length) throw new FormatError(`script ${name.toString(16)} without its end`);
        out.set(name, Buffer.concat([buf.subarray(start, toks[j]![0] + 1), Buffer.from([0x01])]));
        i = j;
    }
    return out;
}

// An emptied script (THUGPRO's "remove": the script stays defined, doing nothing).
export const emptyScript = (name: number): Buffer =>
    Buffer.concat([Buffer.from([0x23, 0x16]), pack("<I", name), Buffer.from([0x01, 0x24, 0x01])]);

// A script's statements for comparing THUG1's with THUGPRO's: line ends and jump offsets left out, THUG1's long if /
// else (0x25 / 0x26) as THUG2's short ones (0x47 / 0x48) - what THUGPRO's recompile changed on every script.
const SHORT: Readonly<Record<number, number>> = { 0x25: 0x47, 0x26: 0x48 };
const NO_PAYLOAD = new Set([0x25, 0x26, 0x47, 0x48, 0x49, 0x2f, 0x37, 0x40, 0x41]);

export function scriptForm(lines: readonly qb.Statement[]): string {
    return lines
        .map((l) =>
            l.map(([t, p]) => `${(SHORT[t] ?? t).toString(16)}${NO_PAYLOAD.has(t) ? "" : `:${p.toString("hex")}`}`),
        )
        .map((l) => l.join(" "))
        .join("\n");
}

// Statements' first tokens: where a token is a command (a line's start, after if / not / while / a random's branch, or
// after `:` - a member function).
const CALL_AFTER = new Set([0x01, 0x02, 0x47, 0x39, 0x42]);
const SKATER = qb.checksum("skater");

// The commands a THUG2-format script calls, and the nodes it names (name= / node= / nodename= / target=, and the
// objects of `object:command`).
export function scriptUses(code: Uint8Array): { calls: Set<number>; nodeNames: Set<number> } {
    const toks = [...qb.tokens(Buffer.concat([Buffer.from(code), Buffer.from([0])]), true)];
    const calls = new Set<number>();
    const nodeNames = new Set<number>();
    const nodeKeys = new Set(["name", "node", "nodename", "target"].map(qb.checksum));
    for (let i = 2; i < toks.length; i++) {
        const [, t, p] = toks[i]!;
        if (t !== 0x16) continue;
        if (toks[i + 1]?.[1] === 0x42) {
            if (p.readUInt32LE(0) !== SKATER) nodeNames.add(p.readUInt32LE(0));
        } else if (CALL_AFTER.has(toks[i - 1]![1])) calls.add(p.readUInt32LE(0));
        if (nodeKeys.has(p.readUInt32LE(0)) && toks[i + 1]?.[1] === 0x07 && toks[i + 2]?.[1] === 0x16)
            nodeNames.add(toks[i + 2]![2].readUInt32LE(0));
    }
    return { calls, nodeNames };
}

// THUG1's commands left out of our ports (thug1_port.LEFT_OUT, its prefixes) that THUGPRO's scripts aren't trusted with
// either: goals, chapters, cutscenes and THUG1's own level flags (GetFlag of a THUG1 flag read far outside THUG2's -
// Manhattan crashed loading). THUG2's global flags (GetGlobalFlag) are THUG2's own here: allowed.
const REFUSED = new Set(
    [
        "GetFlag",
        "IsFlagSet",
        "SetFlag",
        "UnsetFlag",
        "StartGoal",
        "UnlockLevel",
        "PlayCutscene",
        "PlayMovie_ChapterMovie",
        "SpawnSkaterScript",
        "initialize_secret_tapes",
        "initialize_oldskool_icons",
        "goal_want_to_save",
    ].map(qb.checksum),
);

export interface ScriptContext {
    thug2: scriptPort.Thug2; // THUG2's commands and scripts
    defined: ReadonlySet<number>; // the scripts the level defines (ours, with the ones added)
    nodeNames: ReadonlySet<number>; // the nodes our converted level has
    levelNodes: ReadonlySet<number>; // every node name of the level (THUG1's, THUGPRO's): only these are checked
    names: ReadonlyMap<number, string>;
}

// The names THUG2 knows as commands: its C functions, member functions (the tables script_port finds and the ones run
// by name), scripts - and every name THUG2's own scripts use (a member function its tables don't show, a parameter
// read as a command at a line's start: lenient, but THUGPRO's own commands - its DLL's, its LoadZone - aren't there).
const KNOWN = new WeakMap<scriptPort.Thug2, Set<number>>();
function thug2Known(t: scriptPort.Thug2): Set<number> {
    let known = KNOWN.get(t);
    if (known === undefined) {
        known = new Set([...t.cfuncs, ...t.members.keys(), ...t.scripts]);
        for (const used of t.bodies.values()) for (const v of used) known.add(v);
        for (const list of Object.values(scriptPort.BY_NAME)) for (const n of list) known.add(qb.checksum(n));
        // (and the commands THUG2's own levels' scripts call: TriggerType, Obj_LockToObject... - its levels' archives)
        const pre = path.join(t.root, "Data", "pre");
        for (const f of fs.existsSync(pre) ? fs.readdirSync(pre) : []) {
            if (!/_scripts\.prx$/i.test(f)) continue;
            for (const [, d] of prx.entries(path.join(pre, f), (x) => x.toLowerCase().endsWith(".qb")))
                for (const code of rawScripts(d).values()) for (const v of scriptUses(code).calls) known.add(v);
        }
        KNOWN.set(t, known);
    }
    return known;
}

// Why a THUGPRO script can't run in our level (null: it can).
export function scriptCheck(code: Uint8Array, c: ScriptContext): string | null {
    const { calls, nodeNames } = scriptUses(code);
    const text = (v: number): string => c.names.get(v) ?? `#${v.toString(16)}`;
    const thug2 = thug2Known(c.thug2);
    for (const v of calls) {
        const t = text(v).toLowerCase();
        if (REFUSED.has(v) || LEFT_OUT_PREFIXES.some((p) => t.startsWith(p.toLowerCase())))
            return `calls ${text(v)} (THUG1's goals / flags: left out of our ports)`;
        const known = thug2.has(v) || c.defined.has(v);
        if (!known) return `calls ${text(v)}, which THUG2 hasn't (THUGPRO's own?)`;
    }
    for (const v of nodeNames) {
        if (c.levelNodes.has(v) && !c.nodeNames.has(v)) return `names node ${text(v)}, not in our level`;
    }
    return null;
}

// The level's .qb with scripts replaced (set: THUGPRO's bytes; remove: emptied) and added - each replaced script cut
// out where it was and the new bytes appended with the others (script_port.appendScripts: names for the checksums).
export function spliceScripts(
    compiled: Buffer,
    replaced: ReadonlyMap<number, Buffer>,
    appendScripts: (qbData: Buffer, scripts: ReadonlyMap<number, Uint8Array>) => Buffer,
): Buffer {
    const toks = [...qb.tokens(compiled, true)];
    const parts: Buffer[] = [];
    let from = 0;
    for (let i = 0; i < toks.length; i++) {
        const [start, t] = toks[i]!;
        if (t !== 0x23 || toks[i + 1]?.[1] !== 0x16) continue;
        const name = toks[i + 1]![2].readUInt32LE(0);
        let j = i + 2;
        while (j < toks.length && toks[j]![1] !== 0x24) j++;
        if (replaced.has(name)) {
            parts.push(compiled.subarray(from, start));
            from = toks[j]![0] + 1;
            if (compiled[from] === 0x01) from += 1;
        }
        i = j;
    }
    parts.push(compiled.subarray(from));
    return appendScripts(Buffer.concat(parts), replaced);
}

// --- nodes -----------------------------------------------------------------------------------------------------------

const POSITION_KEYS = new Set(["Pos", "Position", "Angles", "Name", "Links"].map(qb.checksum));

// A node's JSON form for its checksum (hand_edits.encode with every name as its checksum - "#XXXXXXXX" - so that it
// doesn't depend on which name texts are known; Links as the linked nodes' name checksums).
const NO_NAMES: ReadonlyMap<number, string> = new Map();
const nameKey = (n: Struct | undefined): string => {
    const v = n?.get("Name");
    return v instanceof Name ? handEdits.nameText(v.value, NO_NAMES) : "?";
};

function nodeForm(n: Struct, tp: Side): handEdits.Json {
    return n.entries.map(([k, v]) => [
        k === null ? null : handEdits.nameText(k.value, NO_NAMES),
        k !== null && k.value === qb.checksum("Links") && Array.isArray(v)
            ? v.map((i) => nameKey(typeof i === "number" ? tp.nodes[i] : undefined))
            : handEdits.encode(v, NO_NAMES),
    ]);
}

const fieldsForm = (n: Struct, fields: readonly string[]): handEdits.Json =>
    fields.map((f) => {
        const v = n.entries.find(([k]) => k !== null && k.value === handEdits.checksumOf(f))?.[1];
        return [handEdits.nameText(handEdits.checksumOf(f), NO_NAMES), v === undefined ? null : handEdits.encode(v, NO_NAMES)];
    });

export const nodeCrc = (n: Struct, tp: Side): string => crc(Buffer.from(JSON.stringify(nodeForm(n, tp))));

export const fieldsCrc = (n: Struct, fields: readonly string[]): string =>
    crc(Buffer.from(JSON.stringify(fieldsForm(n, fields))));

export interface NodeResult {
    forced: Set<Struct>; // the nodes made THUGPRO's: kept whole by the converter
    report: string[];
    skipped: string[];
}

// TP-41 on THUG1's node array (before conversion, after the hand edits): THUGPRO's nodes appended / values set.
// `usable`: the nodes whose model files could be had (an entry needing one that couldn't is left out).
export function applyNodes(
    array: QbValue[],
    names: Map<number, string>,
    level: NewLevel,
    tp: Side,
    modelOk: (model: string) => boolean,
): NodeResult {
    const out: NodeResult = { forced: new Set(), report: [], skipped: [] };
    const tpNames = new Map([...tp.names]);
    for (const [k, v] of names) if (!tpNames.has(k)) tpNames.set(k, v);
    const theirs = new Map(tp.nodes.map((n) => [nodeName(n, tpNames).toLowerCase(), n]));
    const index = new Map<number, number>();
    array.forEach((n, i) => {
        const v = n instanceof Struct ? n.get("Name") : undefined;
        if (v instanceof Name && !index.has(v.value)) index.set(v.value, i);
    });
    const pendingLinks: [Struct, string[]][] = [];
    let added = 0;
    let set = 0;
    for (const e of level.entries) {
        if (e.kind !== "node") continue;
        const source = theirs.get((e.node ?? e.target).toLowerCase());
        if (source === undefined) {
            out.skipped.push(`${e.id} ${e.op} ${e.target}: THUGPRO's node ${e.node ?? e.target} isn't in its level`);
            continue;
        }
        const model = source.get("model") ?? source.get("Model");
        if (typeof model === "string" && model.toLowerCase() !== "none" && !modelOk(model)) {
            out.skipped.push(`${e.id} ${e.op} ${e.target}: its model ${model} couldn't be had`);
            continue;
        }
        if (e.op === "add") {
            if (nodeCrc(source, tp) !== e.crc32) {
                out.skipped.push(`${e.id} add ${e.target}: THUGPRO's node differs from the data's (checksum)`);
                continue;
            }
            if (index.has(handEdits.checksumOf(e.target)))
                throw new FormatError(`${e.id}: adds ${e.target}, a name the level has`);
            const entries: qb.Entry[] = [];
            let links: string[] = [];
            for (const [k, v] of source.entries) {
                if (k !== null && k.value === qb.checksum("Links") && Array.isArray(v)) {
                    links = v.map((i) => (typeof i === "number" && tp.nodes[i] ? nodeName(tp.nodes[i]!, tpNames) : "?"));
                    continue;
                }
                entries.push([k, v]);
            }
            for (const [k, v] of tpNames) if (!names.has(k)) names.set(k, v);
            const n = new Struct(entries);
            index.set(handEdits.checksumOf(e.target), array.length);
            array.push(n);
            out.forced.add(n);
            if (links.length) pendingLinks.push([n, links]);
            added += 1;
        } else if (e.op === "set") {
            const at = index.get(handEdits.checksumOf(e.target));
            if (at === undefined) {
                // ("set" on something missing: an error the converter reports, not an add)
                out.skipped.push(`${e.id} set ${e.target}: ERROR - THUG1's level has no such node`);
                continue;
            }
            const fields = e.fields ?? [];
            if (fieldsCrc(source, fields) !== e.crc32) {
                out.skipped.push(`${e.id} set ${e.target}: THUGPRO's values differ from the data's (checksum)`);
                continue;
            }
            const n = new Struct([...(array[at] as Struct).entries]);
            for (const f of fields) {
                const key = handEdits.checksumOf(f);
                const v = source.entries.find(([k]) => k !== null && k.value === key)?.[1];
                const i = n.entries.findIndex(([k]) => k !== null && k.value === key);
                if (v === undefined) {
                    if (i >= 0) n.entries.splice(i, 1);
                } else if (i >= 0) n.entries[i] = [new Name(key), v];
                else n.entries.push([new Name(key), v]);
                if (!names.has(key)) names.set(key, f);
            }
            for (const [k, v] of tpNames) if (!names.has(k)) names.set(k, v);
            if (fields.includes("Name")) index.set(handEdits.checksumOf(e.node ?? e.target), at);
            array[at] = n;
            out.forced.add(n);
            set += 1;
        }
    }
    for (const [n, links] of pendingLinks) {
        const targets = links.map((l) => index.get(handEdits.checksumOf(l)));
        if (targets.some((t) => t === undefined)) {
            out.skipped.push(`TP-41 ${nodeName(n, names)}: links to ${links.join(" ")} - not all in the level`);
            continue;
        }
        n.entries.push([new Name(qb.checksum("Links")), targets as number[]]);
    }
    if (added || set) out.report.push(`TP-41 THUGPRO's nodes: ${added} added, ${set} set`);
    return out;
}

// --- extraction (thugpro_compare.ts --extract-thugpro) ----------------------------------------------------------------

export interface ExtractInputs {
    t1: Side;
    tp: Side;
    key: string;
    archive: string; // data\pre\<key>_scripts.prx as the install names it
    install: Install;
    thug2Models: ReadonlySet<string>; // THUG2's Data\models (lower case, without .xbx: "veh\\x\\x.mdl")
    thug2: scriptPort.Thug2;
    utils: Map<number, Buffer>; // thugpro_utils.qb's scripts
    utilsNames: ReadonlyMap<number, string>;
    ours: { scripts: ReadonlySet<number>; nodes: ReadonlySet<number> } | null; // our converted level (default output)
}

export interface Extracted {
    level: NewLevel;
    notes: string[]; // what isn't taken and why
}

const UTILS_ARCHIVE = "data\\pre\\thugpro_qb.prx";
const UTILS_FILE = "qb\\thugpro\\thugpro_utils.qb";

// Model base (lower case, without its extension): "veh\\a\\a.mdl" -> "veh\\a\\a"; the skin's too.
const modelBase = (m: string): string => m.toLowerCase().replaceAll("/", "\\").replace(/\.(mdl|skin)$/, "");

export function extract(x: ExtractInputs): Extracted {
    const { t1, tp } = x;
    const names = new Map([...t1.names, ...tp.names, ...x.utilsNames]);
    const text = (v: number): string => handEdits.nameText(v, names);
    const notes: string[] = [];
    const entries: NewEntry[] = [];
    const ours = new Map(t1.nodes.map((n) => [nodeName(n, names).toLowerCase(), n]));
    const theirs = new Map(tp.nodes.map((n) => [nodeName(n, names).toLowerCase(), n]));
    const theirFiles = x.install.archive(x.archive) ?? new Map<string, Buffer>();

    // TP-41 nodes
    const nodeEntries: NewEntry[] = [];
    const models = new Set<string>();
    const removedT1 = t1.nodes.filter((n) => nodeName(n, names) && !theirs.has(nodeName(n, names).toLowerCase()));
    const keysOf = (n: Struct): Set<number> =>
        new Set(n.entries.filter(([k]) => k !== null && !POSITION_KEYS.has(k.value)).map(([k]) => k!.value));
    const changedFields = (a: Struct, b: Struct): string[] =>
        [...new Set([...keysOf(a), ...keysOf(b)])]
            .filter((k) => {
                const va = a.entries.find(([e]) => e !== null && e.value === k)?.[1];
                const vb = b.entries.find(([e]) => e !== null && e.value === k)?.[1];
                return JSON.stringify(va === undefined ? null : handEdits.encode(va, names)) !==
                    JSON.stringify(vb === undefined ? null : handEdits.encode(vb, names));
            })
            .map(text);
    const modelOf = (n: Struct): string | null => {
        const m = n.get("model") ?? n.get("Model");
        return typeof m === "string" && m.toLowerCase() !== "none" ? m : null;
    };
    for (const m of tp.nodes) {
        const name = nodeName(m, names);
        if (!name) continue;
        const cls = (m.get("Class") instanceof Name ? text((m.get("Class") as Name).value) : "").toLowerCase();
        const mine = ours.get(name.toLowerCase());
        if (mine === undefined) {
            if (cls === "railnode") continue; // (TP-36)
            if (isSpawn(m, tp.names)) continue; // (TP-31)
            // a THUG1 node THUGPRO renamed and remade: the same class at the same place
            const p = m.get("Pos");
            const twin = removedT1.find(
                (n) =>
                    JSON.stringify(handEdits.encode(n.get("Pos") ?? 0, names)) ===
                        JSON.stringify(handEdits.encode(p ?? 1, names)) &&
                    n.get("Class") instanceof Name &&
                    m.get("Class") instanceof Name &&
                    (n.get("Class") as Name).value === (m.get("Class") as Name).value,
            );
            if (twin !== undefined) {
                const fields = ["Name", ...changedFields(twin, m)];
                nodeEntries.push({
                    id: IDS.node,
                    kind: "node",
                    op: "set",
                    target: nodeName(twin, names),
                    node: name,
                    fields,
                    why: `THUGPRO renamed THUG1's ${nodeName(twin, names)} ${name} and remade it (${fields.slice(1).join(", ")})`,
                    crc32: fieldsCrc(m, fields),
                });
            } else {
                nodeEntries.push({
                    id: IDS.node,
                    kind: "node",
                    op: "add",
                    target: name,
                    why: `THUGPRO's own ${text((m.get("Class") as Name | undefined)?.value ?? 0)}${modelOf(m) ? ` (${modelOf(m)})` : ""}`,
                    crc32: nodeCrc(m, tp),
                });
            }
            if (modelOf(m)) models.add(modelOf(m)!);
            continue;
        }
        // a THUG1 node THUGPRO gave another model (a real one, not "none")
        const before = modelOf(mine);
        const after = modelOf(m);
        if (after !== null && (before === null || modelBase(before) !== modelBase(after))) {
            const fields = changedFields(mine, m);
            nodeEntries.push({
                id: IDS.node,
                kind: "node",
                op: "set",
                target: name,
                fields,
                why: `THUGPRO remade it with ${after} (${fields.join(", ")})`,
                crc32: fieldsCrc(m, fields),
            });
            models.add(after);
        } else if (after !== null) models.add(after);
    }

    // TP-40 models: every model THUGPRO's nodes use that THUG2 hasn't and THUGPRO ships (the level's archive, or its
    // loose data\models)
    for (const model of [...models].sort()) {
        const base = modelBase(model);
        const ext = model.toLowerCase().endsWith(".skin") ? ".skin" : ".mdl";
        if (x.thug2Models.has(`${base}${ext}`)) continue;
        const inArchive = [...theirFiles.keys()].filter((f) => f.startsWith(`models\\${base}.`)).sort();
        const loose = path.join(x.install.root, "data", "models", ...base.split("\\").slice(0, -1));
        const looseFiles: string[] = [];
        if (!inArchive.length && fs.existsSync(loose)) {
            const stem = base.split("\\").at(-1)!;
            for (const f of fs.readdirSync(loose).sort())
                if (f.toLowerCase().startsWith(`${stem}.`)) looseFiles.push(f);
        }
        if (!inArchive.length && !looseFiles.length) {
            notes.push(`model ${model}: in neither THUG2 nor THUGPRO's install (its nodes left out)`);
            continue;
        }
        for (const f of inArchive) {
            const data = theirFiles.get(f)!;
            entries.push({
                id: IDS.asset,
                kind: "asset",
                op: "add",
                target: f,
                file: f,
                size: data.length,
                why: `THUGPRO's model ${model} (THUG2 hasn't it)`,
                crc32: crc(data),
            });
        }
        for (const f of looseFiles) {
            const dir = base.split("\\").slice(0, -1);
            const realDir = fs.readdirSync(path.join(x.install.root, "data", "models", ...dir.slice(0, -1))).find(
                (d) => d.toLowerCase() === dir.at(-1),
            );
            const file = ["data", "models", ...dir.slice(0, -1), realDir ?? dir.at(-1)!, f].join("\\");
            const data = fs.readFileSync(path.join(x.install.root, file));
            entries.push({
                id: IDS.asset,
                kind: "asset",
                op: "add",
                target: `models\\${[...dir, f].join("\\")}`.toLowerCase(),
                file,
                size: data.length,
                why: `THUGPRO's model ${model} (THUG2 hasn't it; THUGPRO's loose data\\models)`,
                crc32: crc(data),
            });
        }
    }
    entries.push(...nodeEntries);

    // TP-42 scripts: THUGPRO's added, changed and emptied level scripts
    const t1Forms = new Map<number, string>();
    for (const [n, d] of t1.qbs)
        for (const [s, lines] of new qb.QbFile(d, n, true).scripts) t1Forms.set(s, scriptForm(lines));
    const tpRaw = new Map<number, [string, Buffer]>();
    const tpForms = new Map<number, string>();
    for (const [n, d] of tp.qbs) {
        if (n.endsWith("_thugpro.qb")) continue; // (TP-43: kept as the file it is)
        for (const [s, lines] of new qb.QbFile(d, n, true).scripts) tpForms.set(s, scriptForm(lines));
        for (const [s, code] of rawScripts(d)) tpRaw.set(s, [n, code]);
    }
    const scriptEntries: NewEntry[] = [];
    const defined = new Set<number>([...(x.ours?.scripts ?? []), ...tpRaw.keys()]);
    const wantUtils = new Set<number>();
    const checkContext = (): ScriptContext => ({
        thug2: x.thug2,
        defined: new Set([...defined, ...wantUtils]),
        nodeNames: x.ours?.nodes ?? new Set(),
        levelNodes: new Set(),
        names,
    });
    for (const [s, [file, code]] of tpRaw) {
        const before = t1Forms.get(s);
        const form = tpForms.get(s)!;
        let op: Op;
        if (before === undefined) op = "add";
        else if (before === form) continue;
        else op = form.trim() ? "set" : "remove";
        if (op !== "add" && x.ours !== null && !x.ours.scripts.has(s)) {
            notes.push(`script ${text(s)} (${op}): not in our port (a goal script ours leaves out)`);
            continue;
        }
        // (THUGPRO's helpers its own scripts call: thugpro_utils.qb's, added with them)
        for (const v of scriptUses(code).calls) if (x.utils.has(v) && !x.thug2.scripts.has(v)) wantUtils.add(v);
        scriptEntries.push({
            id: IDS.script,
            kind: "script",
            op,
            target: text(s),
            file,
            why:
                op === "add"
                    ? "THUGPRO's own level script"
                    : op === "remove"
                      ? "THUGPRO emptied it"
                      : "THUGPRO changed it",
            crc32: crc(code),
        });
    }
    for (const s of [...wantUtils].sort((a, b) => a - b)) {
        scriptEntries.push({
            id: IDS.script,
            kind: "script",
            op: "add",
            target: text(s),
            archive: UTILS_ARCHIVE,
            file: UTILS_FILE,
            why: "THUGPRO's own script its level scripts call (its thugpro_utils.qb)",
            crc32: crc(x.utils.get(s)!),
        });
    }
    // (the ones that can't run in THUG2 - THUGPRO's own commands, THUG1's goals - noted, not listed: the converter
    // would only refuse them)
    const ctx = checkContext();
    for (const e of scriptEntries) {
        const s = handEdits.checksumOf(e.target);
        const code = e.archive === UTILS_ARCHIVE ? x.utils.get(s)! : tpRaw.get(s)![1];
        const why = e.op === "remove" ? null : scriptCheck(code, { ...ctx, nodeNames: x.ours?.nodes ?? new Set() });
        // (node names are checked when converting, against the converted level)
        if (why !== null && !why.startsWith("names node")) notes.push(`script ${e.target} (${e.op}): ${why}`);
        else entries.push(e);
    }

    // TP-43 host-toggled level files
    for (const [f, d] of theirFiles) {
        if (!f.endsWith("_thugpro.qb")) continue;
        entries.push({
            id: IDS.file,
            kind: "file",
            op: "add",
            target: f.replace(`levels\\${x.key}\\${x.key}`, "levels\\<level>\\<level>"),
            file: f,
            size: d.length,
            why: "THUGPRO's host-toggled level changes (its messages 155-159 switch them; all off by default)",
            crc32: crc(d),
        });
    }
    return { level: { key: x.key, archive: x.archive, entries }, notes };
}

// The model names (as nodes give them: "veh\\a\\a.mdl", lower case) the asset entries make.
export function assetModels(level: NewLevel): string[] {
    return level.entries
        .filter((e) => e.kind === "asset" && /\.(mdl|skin)\.xbx$/.test(e.target))
        .map((e) => e.target.replace(/^models\\/, "").replace(/\.xbx$/, ""));
}
