// NOT ORIGINAL - where THUG2's own levels and THUGPRO's versions of them are, read from the user's installs (read only):
// THUG2's Data\pre archives (the level's <L>_scripts.prx node array and scripts, <L>col.prx collision, <L>scn.prx scene,
// qb_scripts.prx's Levels.qb) and THUGPRO's data\pre (thugpro_qb.prx's Levels.qb names, per level, what THUGPRO loads
// instead: its node array `qb=`, its scripts `level_qb=`, its `more_level_qbs` - the <l>_thugpro.qb holding
// <L>_LevelChanges / <L>_option_items and their scripts -, its collision / scene archives `thugpro_col_pre` /
// `thugpro_scn_pre`, and thugpro_dynamic.qb's <l>_LevelChanges). THUGPRO reads every other file of these levels from
// the THUG2 install it runs on: a file THUGPRO hasn't is THUG2's, the same as ours.
// Used by tools/thug2_levels/thugpro_compare.ts (the comparison and the register's data) and thugpro_apply.ts.

import * as fs from "node:fs";
import * as prx from "../level_port/prx.ts";
import * as qb from "../level_port/qb.ts";
import { Name, type QbValue, Struct } from "../level_port/qb.ts";
import { scriptSpans } from "../level_port/qb_write.ts";
import { findCaseless, resolveInstall } from "../lib/install.ts";
import { pyJoin } from "../lib/py.ts";

// THUG2's own levels (Levels.qb's Level_<code>), in the level select's order.
export const LEVELS = ["TR", "BO", "BA", "BE", "AU", "NO", "ST", "SE", "SE2", "SC", "PH", "DJ", "LA", "CA", "AP"];

export const DEFAULT_THUGPRO = pyJoin(process.env.LOCALAPPDATA ?? "", "THUG Pro");
export const DEFAULT_GAME = "C:\\Games\\THUG2";

const norm = (name: string): string => name.toLowerCase().replaceAll("/", "\\").replace(/^\\+/, "");

// Every file of every archive in a data\pre folder: its name (lower case, '\') -> the archive. The first archive (in
// name order) having it wins.
export function archiveIndex(pre: string): Map<string, string> {
    const out = new Map<string, string>();
    const files = fs
        .readdirSync(pre)
        .filter((f) => f.toLowerCase().endsWith(".prx"))
        .sort();
    for (const f of files) {
        const p = pyJoin(pre, f);
        // (nothing is decompressed: the test only records each name)
        Array.from(
            prx.entries(p, (n) => {
                const k = norm(n);
                if (!out.has(k)) out.set(k, p);
                return false;
            }),
        );
    }
    return out;
}

export class Archives {
    readonly index: Map<string, string>;
    readonly folder: string;
    constructor(folder: string) {
        this.folder = folder;
        this.index = archiveIndex(folder);
    }
    has(name: string): boolean {
        return this.index.has(norm(name));
    }
    // [archive file name, entry] for a file name, or null.
    where(name: string): string | null {
        const p = this.index.get(norm(name));
        return p === undefined ? null : p;
    }
    read(name: string): Buffer | null {
        const p = this.where(name);
        return p === null ? null : prx.find(p, name);
    }
    // Every entry whose name matches.
    *matching(test: (name: string) => boolean): Generator<[string, Buffer]> {
        const byArchive = new Map<string, string[]>();
        for (const [k, p] of this.index) if (test(k)) byArchive.set(p, [...(byArchive.get(p) ?? []), k]);
        for (const [p, keys] of byArchive) {
            const want = new Set(keys);
            yield* prx.entries(p, (n) => want.has(norm(n)));
        }
    }
}

// A .qb file's top-level global by name (case-insensitive: checksums), or undefined.
export function globalOf(data: Buffer, name: string): QbValue | undefined {
    const q = new qb.QbFile(data, name, true);
    const k = qb.checksum(name);
    return q.globals.find(([n]) => n.value === k)?.[1];
}

export function stringOf(s: Struct, key: string): string | null {
    const v = s.get(key);
    return typeof v === "string" ? v : null;
}

export function nameKey(s: Struct, key: string): number | null {
    const v = s.get(key);
    return v instanceof Name ? v.value : null;
}

// One level's files on both sides.
export interface LevelFiles {
    code: string;
    // THUG2's (what our game loads)
    nodes: Buffer; // levels\<L>\<L>.qb
    scripts: Buffer | null; // levels\<L>\<L>_scripts.qb
    col: Buffer; // levels\<L>\<L>.col.xbx
    scn: Buffer | null; // levels\<L>\<L>.scn.xbx
    struct: Struct; // Level_<L> of THUG2's Levels.qb
    // THUGPRO's (null where THUGPRO loads THUG2's file)
    tp: {
        struct: Struct; // Level_<L> of THUGPRO's Levels.qb
        nodes: Buffer | null;
        nodesFrom: string | null; // "<archive>: <entry>"
        scripts: Buffer | null;
        scriptsFrom: string | null;
        col: Buffer | null;
        colFrom: string | null;
        scn: Buffer | null;
        scnFrom: string | null;
        extras: [string, string, Buffer][]; // more_level_qbs THUGPRO adds: [entry, archive, data]
        dynamic: QbValue[] | null; // thugpro_dynamic.qb's <l>_LevelChanges
    };
}

export interface Installs {
    game: Archives; // THUG2's Data\pre
    thugpro: Archives; // THUGPRO's data\pre
    gameLevels: Buffer;
    tpLevels: Buffer;
    tpDynamic: Buffer | null;
}

export function openInstalls(game: string, thugpro: string): Installs {
    const root = resolveInstall(game, "thug2");
    const pre = findCaseless(root, "Data", "pre");
    if (pre === null) throw new Error(`${root}: no Data\\pre`);
    const tpPre = findCaseless(thugpro, "data", "pre");
    if (tpPre === null) throw new Error(`${thugpro} doesn't look like a THUGPRO install: no data\\pre`);
    const g = new Archives(pre);
    const t = new Archives(tpPre);
    const gameLevels = g.read("scripts\\game\\Levels.qb");
    const tpLevels = t.read("qb\\game\\Levels.qb");
    if (gameLevels === null) throw new Error(`${pre}: no scripts\\game\\Levels.qb`);
    if (tpLevels === null) throw new Error(`${tpPre}: no qb\\game\\Levels.qb`);
    return { game: g, thugpro: t, gameLevels, tpLevels, tpDynamic: t.read("qb\\thugpro\\thugpro_dynamic.qb") };
}

function levelStruct(levels: Buffer, code: string, side: string): Struct {
    const v = globalOf(levels, `Level_${code}`);
    if (!(v instanceof Struct)) throw new Error(`${side} Levels.qb: no Level_${code}`);
    return v;
}

const archiveName = (p: string): string => p.split(/[\\/]/).pop() ?? p;

export function levelFiles(inst: Installs, code: string): LevelFiles {
    const L = code;
    const need = (name: string): Buffer => {
        const b = inst.game.read(name);
        if (b === null) throw new Error(`THUG2: no ${name} in Data\\pre`);
        return b;
    };
    const struct = levelStruct(inst.gameLevels, L, "THUG2's");
    const tpStruct = levelStruct(inst.tpLevels, L, "THUGPRO's");
    const own = (name: string | null): [Buffer | null, string | null] => {
        // a file THUGPRO's archives have (THUGPRO's own), else null (THUG2's)
        if (name === null) return [null, null];
        const where = inst.thugpro.where(name);
        return where === null ? [null, null] : [inst.thugpro.read(name), `${archiveName(where)}: ${name}`];
    };
    const [tpNodes, nodesFrom] = own(stringOf(tpStruct, "qb"));
    const [tpScripts, scriptsFrom] = own(stringOf(tpStruct, "level_qb"));
    const flag = (s: Struct, f: string): boolean => s.hasFlag(f);
    const [tpCol, colFrom] = flag(tpStruct, "thugpro_col_pre") ? own(`levels\\${L}\\${L}.col.xbx`) : [null, null];
    const [tpScn, scnFrom] = flag(tpStruct, "thugpro_scn_pre") ? own(`levels\\${L}\\${L}.scn.xbx`) : [null, null];
    const extras: [string, string, Buffer][] = [];
    const more = tpStruct.get("more_level_qbs");
    if (Array.isArray(more)) {
        for (const m of more) {
            if (typeof m !== "string") continue;
            const where = inst.thugpro.where(m);
            if (where !== null) extras.push([m, archiveName(where), inst.thugpro.read(m)!]);
        }
    }
    let dynamic: QbValue[] | null = null;
    if (inst.tpDynamic !== null) {
        const key = stringOf(tpStruct, "level") ?? L;
        const v = globalOf(inst.tpDynamic, `${key}_LevelChanges`);
        if (Array.isArray(v)) dynamic = v;
    }
    return {
        code: L,
        nodes: need(`levels\\${L}\\${L}.qb`),
        scripts: inst.game.read(`levels\\${L}\\${L}_scripts.qb`),
        col: need(`levels\\${L}\\${L}.col.xbx`),
        scn: inst.game.read(`levels\\${L}\\${L}.scn.xbx`),
        struct,
        tp: {
            struct: tpStruct,
            nodes: tpNodes,
            nodesFrom,
            scripts: tpScripts,
            scriptsFrom,
            col: tpCol,
            colFrom,
            scn: tpScn,
            scnFrom,
            extras,
            dynamic,
        },
    };
}

// --- scripts ------------------------------------------------------------------------------------------------------

// A script's bytes as a file has them (23 16 <name> ... 24) and where from.
export interface ScriptItem {
    name: number;
    bytes: Buffer;
    from: string; // "<archive>: <entry>"
}

// Every script of every .qb in a set of entries (first definition wins).
export function scriptsOf(files: Iterable<[string, string, Buffer]>): Map<number, ScriptItem> {
    const out = new Map<number, ScriptItem>();
    for (const [entry, archive, data] of files) {
        let spans: Map<number, [number, number]>;
        try {
            spans = scriptSpans(data);
        } catch {
            continue;
        }
        for (const [name, [a, b]] of spans) {
            if (!out.has(name)) out.set(name, { name, bytes: data.subarray(a, b), from: `${archive}: ${entry}` });
        }
    }
    return out;
}

// Every name a script's bytes use (0x16 tokens after its own name).
export function usedNames(bytes: Buffer): Set<number> {
    const out = new Set<number>();
    for (const [, t, p] of qb.tokens(Buffer.concat([bytes.subarray(6), Buffer.from([0])]), true)) {
        if (t === 0x00) break;
        if (t === 0x16) out.add(p.readUInt32LE(0));
    }
    return out;
}

// A script body without its line tokens (0x01 / 0x02 + number), to compare two compilations of the same text.
export function scriptShape(bytes: Buffer): string {
    const parts: string[] = [];
    for (const [, t, p] of qb.tokens(Buffer.concat([bytes, Buffer.from([0])]), true)) {
        if (t === 0x00) break;
        if (t === 0x01 || t === 0x02) continue;
        parts.push(`${t.toString(16)}:${p.toString("hex")}`);
    }
    return parts.join(" ");
}

// The name table of a set of .qb files (checksum -> text).
export function namesOf(datas: Iterable<Buffer>, into = new Map<number, string>()): Map<number, string> {
    for (const d of datas) {
        try {
            for (const [, t, p] of qb.tokens(d, true)) {
                if (t === 0x2b) into.set(p.readUInt32LE(0), p.subarray(4, p.length - 1).toString("latin1"));
            }
        } catch {
            // (a file the tokenizer can't read: its names are left out)
        }
    }
    return into;
}
