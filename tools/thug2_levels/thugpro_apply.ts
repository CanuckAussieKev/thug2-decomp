// NOT ORIGINAL - applies the register's entries for THUGPRO's changes to THUG2's own levels (TP-42,
// tools/compat/thugpro/thug2_levels.json - thugpro_compare.ts --extract-thugpro writes it) through our data overlay:
// never the game's files. Each changed file is made from the user's THUG2 files (Data\pre) plus the entries' data and
// written where the DLL reads it first (<game>\thug2-decomp-data - src/game/front/data_overlay.h):
//   levels\<L>\<L>.qb          the node array (edited nodes in their places, added nodes appended - every other node's
//                              bytes kept: qb_write.replaceGlobalArray) and the trigger scripts THUGPRO's file changed;
//   levels\<L>\<L>_scripts.qb  THUGPRO's scripts the entries need (by reference: read from the THUGPRO install, checked
//                              by CRC-32) and the level's setup wrapper (THUGPRO's host options' state after loading);
//   levels\<L>\<L>.col.xbx     the collision with the entries' face / object edits (tools/thug1_levels/hand_edits.ts's
//                              applyColEdits) and THUGPRO's added objects (by reference);
//   levels\<L>\<L>.scn.xbx     THUGPRO's added sectors (by reference) - only when an entry adds geometry;
//   scripts\game\Levels.qb     (the overlay's, and the Anteara Mod's copy) Level_<L>'s setup_script, when an entry
//                              changes it.
// Which entries: those `enabled` in the register (none until the user switches them on), or --enable <ids / classes>.
// make_menu_level.ts calls installEnabled() after making the overlay (so a rebuilt overlay keeps them); this tool's
// --install writes into a workspace's overlay directly (make_menu_level.ts empties the overlay: install again after).
//
//   node tools/thug2_levels/thugpro_apply.ts [--enable T2-BO-4,T2-AU-6 | parity | all] [--level BO] [--game <folder>]
//        [--thugpro <dir>] [--out <folder>] [--install] [--allow-main] [--dry-run]
// Without --install the files go to --out (default out\thug2_levels in the repo).

import * as fs from "node:fs";
import * as path from "node:path";
import { fileURLToPath } from "node:url";
import { crc32 } from "node:zlib";
import * as qb from "../level_port/qb.ts";
import { Name, type QbValue, Struct } from "../level_port/qb.ts";
import { putScripts, replaceGlobalArray, scriptSpans, setStructEntries } from "../level_port/qb_write.ts";
import * as thug2Col from "../level_port/thug2_col.ts";
import * as thug2Scene from "../level_port/thug2_scene.ts";
import { parseCli } from "../lib/cli.ts";
import { findCaseless, resolveInstall } from "../lib/install.ts";
import { isMain, print, pyJoin, runMain } from "../lib/py.ts";
import { nodeArray } from "../thaw_levels/thugpro_compare.ts";
import { applyColEdits, applyNodeEdits, type LevelEdits } from "../thug1_levels/hand_edits.ts";
import { type Entry, mergeEdits, type ScriptRef } from "./diff.ts";
import { loadRegister, type Register } from "./register.ts";
import { Archives, DEFAULT_GAME, DEFAULT_THUGPRO, LEVELS } from "./sources.ts";

const REPO = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..", "..");

const hasNodeEdits = (e: LevelEdits): boolean =>
    !!(e.moved_nodes || e.net_presence || e.created_at_start || e.node_values || e.rails);
const hasColEdits = (e: LevelEdits): boolean => !!(e.face_values || e.collision_removed || e.collision_reshaped);

// --- choosing entries -----------------------------------------------------------------------------------------------

// The entries to apply: `enable` ("T2-BO-4,T2-AU-6", "parity", "all") or else those `enabled` in the register; only
// applicable ones, of `levels` when given.
export function chooseEntries(reg: Register, enable: string | undefined, levels?: readonly string[]): Entry[] {
    const wanted = enable?.split(",").map((s) => s.trim().toLowerCase()) ?? [];
    const out: Entry[] = [];
    for (const [L, lr] of Object.entries(reg.levels)) {
        if (levels?.length && !levels.includes(L)) continue;
        for (const e of lr.entries) {
            const on = wanted.length
                ? wanted.includes("all") || wanted.includes(e.class) || wanted.includes(e.id.toLowerCase())
                : e.enabled;
            if (on && e.applicable) out.push(e);
        }
    }
    return out;
}

// --- by reference ---------------------------------------------------------------------------------------------------

export class ThugproFiles {
    private archives: Archives | null = null;
    private cache = new Map<string, Buffer | null>();
    private readonly dir: string;
    constructor(dir: string) {
        this.dir = dir;
    }
    file(from: string): Buffer | null {
        // from: "<archive>: <entry>"
        const at = from.indexOf(": ");
        const entry = at < 0 ? from : from.slice(at + 2);
        if (!this.cache.has(entry)) {
            if (this.archives === null) {
                const pre = findCaseless(this.dir, "data", "pre");
                if (pre === null)
                    throw new Error(`${this.dir}: no THUGPRO install (data\\pre) - its own items can't be read`);
                this.archives = new Archives(pre);
            }
            this.cache.set(entry, this.archives.read(entry));
        }
        return this.cache.get(entry) ?? null;
    }
    script(ref: ScriptRef): Buffer {
        const data = this.file(ref.from);
        if (data === null) throw new Error(`THUGPRO's ${ref.from} not found (for ${ref.name})`);
        const span = scriptSpans(data).get(qb.checksum(ref.name));
        if (!span) throw new Error(`THUGPRO's ${ref.from} has no script ${ref.name}`);
        const bytes = data.subarray(span[0], span[1]);
        if (crc32(bytes) >>> 0 !== ref.crc)
            throw new Error(`THUGPRO's ${ref.name} changed since it was compared (CRC)`);
        return bytes;
    }
}

// A script's body as putScripts takes it (after 23 16 <name> 01, before 24).
function bodyOf(bytes: Buffer): Buffer {
    let start = 6;
    while (start < bytes.length && (bytes[start] === 0x01 || bytes[start] === 0x02))
        start += bytes[start] === 0x02 ? 5 : 1;
    return bytes.subarray(start, bytes.length - 1);
}

// `script <name>` calling each [script, flags...] in turn (the setup wrapper): 16 <script> [16 <flag>...] 01 per line.
export function callsBody(calls: readonly (readonly string[])[]): Buffer {
    const parts: Buffer[] = [];
    for (const call of calls) {
        for (const word of call) {
            const b = Buffer.alloc(5);
            b[0] = 0x16;
            b.writeUInt32LE(qb.checksum(word) >>> 0, 1);
            parts.push(b);
        }
        parts.push(Buffer.from([0x01]));
    }
    return Buffer.concat(parts);
}

// --- one level --------------------------------------------------------------------------------------------------------

export interface LevelOutput {
    files: Map<string, Buffer>; // overlay path -> bytes
    levelsQb: [number, QbValue][]; // Level_<L> entries to set in Levels.qb
    report: string[];
}

export function applyLevel(
    L: string,
    entries: readonly Entry[],
    game: Archives,
    thugpro: ThugproFiles,
    gameLevels: Buffer,
): LevelOutput {
    const out: LevelOutput = { files: new Map(), levelsQb: [], report: [] };
    const edits = mergeEdits(entries.flatMap((e) => (e.edits ? [e.edits] : [])));
    const read = (name: string): Buffer => {
        const b = game.read(name);
        if (b === null) throw new Error(`THUG2: no ${name}`);
        return b;
    };
    const nodesName = `levels\\${L}\\${L}.qb`;
    const scriptsName = `levels\\${L}\\${L}_scripts.qb`;
    let nodes = read(nodesName);
    let scripts = game.read(scriptsName);
    const names = new Map<number, string>();
    // node array
    if (hasNodeEdits(edits)) {
        const [array, arrayNames] = nodeArray(nodes, nodesName);
        for (const [k, v] of arrayNames) names.set(k, v);
        const before = [...array];
        const lines = applyNodeEdits(array, names, edits, L);
        const q = new qb.QbFile(nodes, nodesName, true);
        const arrayKey = q.globals.find(
            ([k, v]) => Array.isArray(v) && (q.names.get(k.value) ?? "").toLowerCase().endsWith("_nodearray"),
        )?.[0];
        if (!arrayKey) throw new Error(`${nodesName}: no node array`);
        const elements = array.map((n, i) => (i < before.length && n === before[i] ? { keep: i } : { value: n }));
        nodes = replaceGlobalArray(nodes, arrayKey.value, elements, names, true);
        out.report.push(...lines.map((l) => l.replace("THUGPRO hand edits", "node array")));
    }
    // scripts: THUGPRO's (by reference) - a script of the same name in the node file or the scripts file is replaced
    // there, a new one goes into the scripts file
    const refs = new Map<string, ScriptRef>();
    for (const e of entries)
        for (const r of [...(e.replace_scripts ?? []), ...(e.scripts ?? [])]) refs.set(r.name.toLowerCase(), r);
    const inNodes: [number, Buffer][] = [];
    const inScripts: [number, Buffer][] = [];
    const nodeSpans = scriptSpans(nodes);
    for (const r of refs.values()) {
        const k = qb.checksum(r.name);
        names.set(k, r.name);
        (nodeSpans.has(k) ? inNodes : inScripts).push([k, bodyOf(thugpro.script(r))]);
    }
    // THUGPRO's level setup (its setup_script) and host options' state after loading
    const setup = entries.flatMap((e) => (e.level_setup ? [e.level_setup] : []));
    if (setup.length) {
        const levelsQ = new qb.QbFile(gameLevels, "Levels.qb", true);
        const struct = levelsQ.globals.find(([k]) => k.value === qb.checksum(`Level_${L}`))?.[1];
        const own = struct instanceof Struct ? struct.get("setup_script") : undefined;
        const ownName = own instanceof Name ? (levelsQ.names.get(own.value) ?? `#${own.value.toString(16)}`) : null;
        const base = setup.find((s) => s.setup_script)?.setup_script ?? ownName;
        const options = setup.flatMap((s) => s.options ?? []);
        let target = base;
        if (options.length) {
            target = `thug2decomp_thugpro_${L}_setup`;
            const calls: string[][] = [];
            if (base !== null && !base.startsWith("#")) calls.push([base]);
            for (const o of options) calls.push([o.script, ...o.params]);
            const k = qb.checksum(target);
            names.set(k, target);
            for (const c of calls) for (const w of c) names.set(qb.checksum(w), w);
            inScripts.push([k, callsBody(calls)]);
            out.report.push(`  setup: ${target} = ${calls.map((c) => c.join(" ")).join("; ")}`);
        }
        if (target !== null && !target.startsWith("#")) {
            out.levelsQb.push([qb.checksum("setup_script"), new Name(qb.checksum(target))]);
            names.set(qb.checksum(target), target);
            out.report.push(`  Levels.qb: Level_${L} setup_script = ${target}`);
        }
    }
    if (inNodes.length) nodes = putScripts(nodes, inNodes, names);
    if (inScripts.length) {
        if (scripts === null) throw new Error(`THUG2: no ${scriptsName} for THUGPRO's scripts`);
        scripts = putScripts(scripts, inScripts, names);
    }
    if (inNodes.length || inScripts.length)
        out.report.push(`  scripts: ${inNodes.length + inScripts.length} of THUGPRO's (by reference)`);
    if (hasNodeEdits(edits) || inNodes.length) out.files.set(nodesName, nodes);
    if (inScripts.length && scripts !== null) out.files.set(scriptsName, scripts);
    // collision and scene
    const geometry = entries.flatMap((e) => e.geometry ?? []);
    if (hasColEdits(edits) || geometry.length) {
        const colName = `levels\\${L}\\${L}.col.xbx`;
        let [col, lines] = applyColEdits(thug2Col.readCol(read(colName), colName), edits, L);
        out.report.push(...lines.map((l) => l.replace("THUGPRO hand edits", "collision")));
        let scene: thug2Scene.Scene | null = null;
        for (const g of geometry) {
            const k = qb.checksum(g.name);
            const tpCol = thugpro.file(`levels\\${L}\\${L}.col.xbx`);
            const o =
                tpCol === null
                    ? undefined
                    : thug2Col.readCol(tpCol, "THUGPRO col").objects.find((x) => x.checksum === k);
            if (o === undefined) throw new Error(`THUGPRO's ${L} collision has no ${g.name}`);
            col = { ...col, objects: [...col.objects.filter((x) => x.checksum !== k), o] };
            const tpScn = thugpro.file(`levels\\${L}\\${L}.scn.xbx`);
            if (tpScn !== null) {
                const theirs = thug2Scene.readScene(tpScn, "THUGPRO scn");
                const sector = theirs.sectors.find((s) => s.checksum === k);
                if (sector) {
                    const current: thug2Scene.Scene =
                        scene ?? thug2Scene.readScene(read(`levels\\${L}\\${L}.scn.xbx`), `${L}.scn`);
                    const have = new Set<number>(current.materials.map((m) => m.checksum));
                    const used = new Set<number>(sector.meshes.map((m) => m.material));
                    const missing: thug2Scene.Material[] = theirs.materials.filter(
                        (m) => used.has(m.checksum) && !have.has(m.checksum),
                    );
                    scene = {
                        ...current,
                        materials: [...current.materials, ...missing],
                        sectors: [...current.sectors.filter((s) => s.checksum !== k), sector],
                    };
                    out.report.push(
                        `  geometry: ${g.name} (collision ${o.faces.length} faces, sector of ${sector.meshes.length} meshes${missing.length ? `, ${missing.length} materials` : ""})`,
                    );
                }
            }
        }
        out.files.set(colName, thug2Col.writeCol(col));
        if (scene) out.files.set(`levels\\${L}\\${L}.scn.xbx`, thug2Scene.writeScene(scene));
    }
    return out;
}

// --- installing -------------------------------------------------------------------------------------------------------

// Folders nothing is written into, and the main install (only with allowMain) - as tools/game/apply_layout.ts.
export function refusal(folder: string, allowMain: boolean): string | null {
    const norm = path.resolve(folder).toLowerCase().replace(/\\+$/, "");
    for (const never of ["c:\\games\\thug2_pm_bak", "c:\\games\\thug2-thpsx-original", "c:\\games\\thugpro"])
        if (norm === never || norm.startsWith(`${never}\\`)) return `${folder}: never written into`;
    if (norm.includes("\\thug pro")) return `${folder}: THUGPRO's folder - never written into`;
    if ((norm.startsWith("c:\\games\\thug2\\") || norm === "c:\\games\\thug2") && !allowMain)
        return "C:\\Games\\THUG2 is the main install: writing there needs --allow-main (the user's own runs only)";
    return null;
}

// Applies `entries` and writes the files under `out` (the overlay folder or a staging folder). Returns report lines.
export function install(
    gameDir: string,
    out: string,
    entries: readonly Entry[],
    thugproDir = DEFAULT_THUGPRO,
    dryRun = false,
): string[] {
    const root = resolveInstall(gameDir, "thug2");
    const pre = findCaseless(root, "Data", "pre");
    if (pre === null) throw new Error(`${root}: no Data\\pre`);
    const game = new Archives(pre);
    const thugpro = new ThugproFiles(thugproDir);
    const gameLevels = game.read("scripts\\game\\Levels.qb");
    if (gameLevels === null) throw new Error("THUG2: no scripts\\game\\Levels.qb");
    const report: string[] = [];
    const levelsEdits: [string, [number, QbValue][]][] = [];
    for (const L of LEVELS) {
        const mine = entries.filter((e) => e.level === L);
        if (!mine.length) continue;
        const r = applyLevel(L, mine, game, thugpro, gameLevels);
        report.push(`${L}: ${mine.map((e) => e.id).join(", ")}`, ...r.report);
        for (const [name, data] of r.files) {
            const target = pyJoin(out, name);
            report.push(`  ${dryRun ? "would write" : "wrote"} ${target}`);
            if (!dryRun) {
                fs.mkdirSync(path.dirname(target), { recursive: true });
                fs.writeFileSync(target, data);
            }
        }
        if (r.levelsQb.length) levelsEdits.push([L, r.levelsQb]);
    }
    if (levelsEdits.length) {
        // the overlay's Levels.qb (and the Anteara Mod's), else the game's copied in
        const targets = [
            pyJoin(out, "scripts", "game", "Levels.qb"),
            pyJoin(out, "anteara", "scripts", "game", "Levels.qb"),
        ];
        const existing = targets.filter((t) => fs.existsSync(t));
        for (const t of existing.length ? existing : [targets[0]!]) {
            let data = fs.existsSync(t) ? fs.readFileSync(t) : gameLevels;
            for (const [L, set] of levelsEdits) data = setStructEntries(data, qb.checksum(`Level_${L}`), set);
            report.push(`  ${dryRun ? "would write" : "wrote"} ${t}`);
            if (!dryRun) {
                fs.mkdirSync(path.dirname(t), { recursive: true });
                fs.writeFileSync(t, data);
            }
        }
    }
    return report;
}

// For make_menu_level.ts: the register's switched-on entries into the overlay it has just made.
export function installEnabled(gameDir: string, overlay: string, thugproDir = DEFAULT_THUGPRO): string[] {
    const reg = loadRegister();
    if (reg === null) return ["THUG2 levels as THUGPRO's (TP-42): no register"];
    const entries = chooseEntries(reg, undefined);
    if (!entries.length)
        return ["THUG2 levels as THUGPRO's (TP-42): none switched on (tools/compat/thugpro/thug2_levels.json)"];
    try {
        return ["THUG2 levels as THUGPRO's (TP-42):", ...install(gameDir, overlay, entries, thugproDir)];
    } catch (e) {
        return [`THUG2 levels as THUGPRO's (TP-42): not applied - ${e instanceof Error ? e.message : String(e)}`];
    }
}

const USAGE =
    "usage: thugpro_apply.ts [--enable <ids|parity|all>] [--level <L>] [--game <folder>] [--thugpro <dir>] " +
    "[--out <folder>] [--install] [--allow-main] [--dry-run]";

export function main(argv: readonly string[] = process.argv.slice(2)): number {
    const { values } = parseCli(USAGE, {
        args: [...argv],
        options: {
            enable: { type: "string" },
            level: { type: "string", multiple: true },
            game: { type: "string", default: DEFAULT_GAME },
            thugpro: { type: "string", default: DEFAULT_THUGPRO },
            out: { type: "string" },
            install: { type: "boolean", default: false },
            "allow-main": { type: "boolean", default: false },
            "dry-run": { type: "boolean", default: false },
        },
    });
    const reg = loadRegister();
    if (reg === null) throw new Error("no register: run thugpro_compare.ts --extract-thugpro first");
    const levels = (values.level ?? []).map((l) => l.toUpperCase());
    const entries = chooseEntries(reg, values.enable, levels);
    if (!entries.length) {
        print("nothing to apply: no entry switched on (the register's `enabled`, or --enable)");
        return 0;
    }
    let out = values.out ?? pyJoin(REPO, "out", "thug2_levels");
    if (values.install) {
        const root = resolveInstall(values.game, "thug2");
        const why = refusal(root, values["allow-main"]);
        if (why) throw new Error(why);
        out = pyJoin(root, "thug2-decomp-data");
    }
    for (const l of install(values.game, out, entries, values.thugpro, values["dry-run"])) print(l);
    return 0;
}

if (isMain(import.meta.url)) await runMain(() => main());
