// NOT ORIGINAL - compares THUGPRO's versions of THUG2's own 15 levels with THUG2's, read from the user's installs (read
// only: THUG2's Data\pre, THUGPRO's data\pre - sources.ts), for online play with THUGPRO players
// (docs/thugpro-online.md section 5; the user's decision 2026-10-21). Prints, per level, every difference grouped into
// the register's entries (register.ts; class parity / visible / single_player / cosmetic / none), and with
// --extract-thugpro writes them into tools/compat/thugpro/thug2_levels.json (TP-42) - each entry off until the user
// switches it on (`enabled`, kept across re-extractions); tools/thug2_levels/thugpro_apply.ts applies the switched-on
// ones through our data overlay.
//
//   node tools/thug2_levels/thugpro_compare.ts [levels...] [--game C:\Games\THUG2] [--thugpro <dir>] [--details]
//        [--json <file>]
//   node tools/thug2_levels/thugpro_compare.ts --extract-thugpro [levels...]   (writes the register's data)

import * as fs from "node:fs";
import { parseCli } from "../lib/cli.ts";
import { isMain, print, runMain } from "../lib/py.ts";
import { compactJson } from "../thaw_levels/thugpro_compare.ts";
import { compareLevel, type Entry, type LevelReport } from "./diff.ts";
import { carryEnabled, levelEntries, REGISTER_FILE, REGISTER_ID, type Register, readRegisterFile } from "./register.ts";
import {
    DEFAULT_GAME,
    DEFAULT_THUGPRO,
    type Installs,
    LEVELS,
    levelFiles,
    namesOf,
    openInstalls,
    scriptsOf,
} from "./sources.ts";

const USAGE =
    "usage: thugpro_compare.ts [levels...] [--game <THUG2 folder>] [--thugpro <dir>] [--details] [--json <file>] " +
    "[--extract-thugpro]";

export interface Compared {
    report: LevelReport;
    entries: Entry[];
}

// Every level's comparison (THUG2's global scripts and THUGPRO's read once).
export function compareAll(inst: Installs, levels: readonly string[]): Compared[] {
    const isQb = (n: string): boolean => n.endsWith(".qb");
    const thug2Files = [...inst.game.matching((n) => n.startsWith("scripts\\") && isQb(n))].map(
        ([n, d]) => [n, "qb_scripts.prx", d] as [string, string, Buffer],
    );
    const tpFiles = [...inst.thugpro.matching((n) => n.startsWith("qb\\") && isQb(n))].map(
        ([n, d]) => [n, "thugpro_qb.prx", d] as [string, string, Buffer],
    );
    const globals = {
        thug2: scriptsOf(thug2Files),
        thugpro: scriptsOf(tpFiles),
        names: namesOf([...thug2Files, ...tpFiles].map((f) => f[2])),
    };
    return levels.map((L) => {
        const files = levelFiles(inst, L);
        const report = compareLevel(files, globals);
        return { report, entries: levelEntries(report, files.nodes) };
    });
}

export function printLevel(c: Compared, details: boolean): void {
    const r = c.report;
    const src = Object.entries(r.sources);
    print(
        `== ${r.code}: ${src.length ? src.map(([k, v]) => `${k} <- ${v}`).join("; ") : "THUGPRO loads THUG2's own files"}`,
    );
    if (r.nodes)
        print(`   nodes: THUG2 ${r.nodes.ours}, THUGPRO ${r.nodes.theirs}, ${r.nodes.changes.length} differences`);
    if (r.collision)
        print(
            `   collision: THUG2 ${r.collision.ours} objects, THUGPRO ${r.collision.theirs}, ${r.collision.changes.length} differences`,
        );
    if (r.scene)
        print(
            `   scene: THUG2 ${r.scene.ours} sectors, THUGPRO ${r.scene.theirs}: ${r.scene.added.length} added, ${r.scene.changed.length} changed, ${r.scene.removed.length} removed`,
        );
    if (!c.entries.length) print("   no differences");
    for (const e of c.entries) {
        print(
            `   ${e.id} [${e.class}${e.applicable ? "" : ", not applicable"}] ${e.kind} / ${e.what}: ${e.description}`,
        );
        if (details && e.nodes && e.nodes.length > 8) print(`      nodes: ${e.nodes.join(", ")}`);
        if (details && e.scripts?.length) print(`      THUGPRO's scripts: ${e.scripts.map((s) => s.name).join(", ")}`);
    }
    if (details && r.nodes)
        for (const ch of r.nodes.changes) print(`      ${ch.node}: ${ch.detail} [${ch.what}, ${ch.klass}]`);
    if (details && r.collision)
        for (const ch of r.collision.changes) print(`      col ${ch.object}: ${ch.kind} ${ch.detail}`);
}

export function summary(all: readonly Compared[]): string[] {
    const lines: string[] = [];
    const count = (k: string): number => all.reduce((s, c) => s + c.entries.filter((e) => e.class === k).length, 0);
    lines.push(
        `${all.length} levels, ${all.reduce((s, c) => s + c.entries.length, 0)} entries: ${["parity", "visible", "single_player", "cosmetic", "none"].map((k) => `${k} ${count(k)}`).join(", ")}`,
    );
    return lines;
}

export function buildRegister(all: readonly Compared[], old: Register | null): Register {
    const levels: Register["levels"] = {};
    for (const c of all) {
        carryEnabled(c.entries, old?.levels[c.report.code]?.entries);
        levels[c.report.code] = { sources: c.report.sources, entries: c.entries };
    }
    return {
        _comment:
            "THUGPRO's changes to THUG2's own levels (TP-42 in README.md here; the user's decision 2026-10-21: compare " +
            "THUG2's node data). Written by `node tools/thug2_levels/thugpro_compare.ts --extract-thugpro`, never by hand " +
            "except `enabled`: every entry is off until the user switches it on; `node tools/thug2_levels/thugpro_apply.ts` " +
            "(and make_menu_level.ts) apply the switched-on entries through the data overlay (levels\\<L>\\<L>.qb / " +
            ".col.xbx / .scn.xbx / _scripts.qb, scripts\\game\\Levels.qb), made from the user's THUG2 files - never the game's " +
            "files. class: parity (online play with THUGPRO players: spawns, crowns, CTF, rails, kill zones, collision " +
            "players touch, graffiti objects), visible (seen differently, nothing to stand on: gaps, vehicles, loose " +
            "objects), single_player, cosmetic, none. `scripts` / `replace_scripts` / `geometry`: THUGPRO's own items BY " +
            "REFERENCE (archive entry and CRC-32; read from the THUGPRO install when applied, never stored here). " +
            "`edits`: tools/thug1_levels/hand_edits.ts's form (names, {float}, {vector}, {string}).",
        _register: REGISTER_ID,
        _source:
            "THUGPRO's installed data\\pre (thugpro_qb.prx's Levels.qb: per level its node array qb=, level_qb=, " +
            "more_level_qbs, thugpro_col_pre / thugpro_scn_pre; thugpro_dynamic.qb's <L>_LevelChanges) against THUG2's " +
            "Data\\pre (<L>_scripts.prx, <L>col.prx, <L>scn.prx, qb_scripts.prx's Levels.qb)",
        levels,
    };
}

export function main(argv: readonly string[] = process.argv.slice(2)): number {
    const { values, positionals } = parseCli(USAGE, {
        args: [...argv],
        allowPositionals: true,
        options: {
            game: { type: "string", default: DEFAULT_GAME },
            thugpro: { type: "string", default: DEFAULT_THUGPRO },
            details: { type: "boolean", default: false },
            json: { type: "string" },
            "extract-thugpro": { type: "boolean", default: false },
        },
    });
    const levels = positionals.length ? positionals.map((l) => l.toUpperCase()) : LEVELS;
    for (const l of levels)
        if (!LEVELS.includes(l)) throw new Error(`${l}: not one of THUG2's levels (${LEVELS.join(" ")})`);
    const inst = openInstalls(values.game, values.thugpro);
    const all = compareAll(inst, levels);
    for (const c of all) printLevel(c, values.details);
    for (const l of summary(all)) print(l);
    if (values.json) {
        fs.writeFileSync(
            values.json,
            compactJson(all.map((c) => ({ level: c.report.code, entries: c.entries }))) + "\n",
        );
    }
    if (values["extract-thugpro"]) {
        const old = readRegisterFile();
        const reg = buildRegister(all, old);
        if (old && positionals.length) {
            // (only the levels asked for are made again)
            for (const [k, v] of Object.entries(old.levels)) if (!(k in reg.levels)) reg.levels[k] = v;
            reg.levels = Object.fromEntries(LEVELS.filter((l) => l in reg.levels).map((l) => [l, reg.levels[l]!]));
        }
        fs.writeFileSync(REGISTER_FILE, compactJson(reg) + "\n");
        print(`wrote ${REGISTER_FILE}`);
    }
    return 0;
}

if (isMain(import.meta.url)) await runMain(() => main());
