// NOT ORIGINAL - finds what is new in THUGPRO's THPS3 level ports (in no source game) and writes the references the
// register's `thugpro_new` section holds (thugpro_new.ts reads the bytes at convert time). Called by
// `thugpro_compare.ts --extract-thugpro`: nothing here is typed in. Rules (each item found is an `add` unless said):
// - sectors: THUGPRO's level scene sectors that aren't THPS3's (its .bsp) - not the 3-vertex stand-ins its toolchain
//   gives THPS3 objects without geometry (a sector named as a THPS3 node: ours makes those objects itself) and not its
//   sky's (THPS3's sky under other checksums: compared by vertex count with THPS3's sky);
// - collision: THUGPRO's collision objects that aren't THPS3's sectors (fixes, invisible walls, the bucket's);
// - nodes: THUGPRO's nodes whose name THPS3's node array hasn't and the other register entries don't make (TP-10 ..
//   TP-12's CTF / team / spawn nodes, TP-18's rails) - not unnamed ones without geometry (toolchain leftovers), not
//   ones whose trigger script can't run in THUG2 (below);
// - set: a THPS3 object THUGPRO made a Vehicle with a model (its trains, planes, cars, the furnace bucket) where ours
//   draws THPS3's .dff clump: Class / model / skeletonName as THUGPRO's, our drawn copy left out;
// - scripts: THUGPRO's level scripts in no THPS3 file, not THUG2's own, not ones ours makes (CTF / team flags), and
//   THPS3-named ones THUGPRO rewrote to use its new nodes or scripts (they replace ours); left out when they call
//   what THUG2 hasn't (THUGPRO's DLL commands, its global scripts) - listed with the calls in `_left_out`;
// - models: the files of the models those nodes name that THUGPRO's level archive carries (models\<name>\..., the
//   model's .qb node array with them: a Vehicle can't load without it).

import * as fs from "node:fs";
import * as path from "node:path";
import * as prx from "../level_port/prx.ts";
import * as qb from "../level_port/qb.ts";
import { Name, type QbValue, Struct } from "../level_port/qb.ts";
import type * as scriptPort from "../level_port/script_port.ts";
import * as texFile from "../level_port/tex.ts";
import * as thug2Col from "../level_port/thug2_col.ts";
import * as thug2Scene from "../level_port/thug2_scene.ts";
import { resolveInstall } from "../lib/install.ts";
import { hex8, pyJoin } from "../lib/py.ts";
import { findPath } from "../thaw_levels/thaw_levels.ts";
import { nameOf, nodeArray } from "../thaw_levels/thugpro_compare.ts";
import * as thps3 from "./thps3.ts";
import {
    ANIMAL_BIRD,
    ANIMAL_SHARK,
    CAR,
    CAR_BUS,
    CTF_COLOURS,
    findFolder,
    levelFiles,
    loadScriptInfo,
    PED_STAND,
    PED_WALK,
} from "./thps3_levels.ts";
import {
    colBytes,
    itemCrc,
    type NewEntry,
    nodeBytes,
    REGISTER_ID,
    rowId,
    scriptBytes,
    scriptNames,
    sectorBytes,
} from "./thugpro_new.ts";

export interface LeftOut {
    kind: string;
    target: string;
    why: string;
}

const lower = (s: string): string => s.toLowerCase();

// The script names our converter makes itself (thps3_levels: the made peds / cars / animals, CTF and team flags, the
// level's load_level_anims and <level>_setup).
function ourScriptNames(level: string): Set<number> {
    const out = new Set(
        [PED_WALK, PED_STAND, CAR, CAR_BUS, ANIMAL_BIRD, ANIMAL_SHARK, "load_level_anims", `${level}_setup`].map(
            qb.checksum,
        ),
    );
    for (const c of CTF_COLOURS) {
        out.add(qb.checksum(`TRG_CTF_${c}Script`));
        out.add(qb.checksum(`TRG_Flag_${c}Script`));
    }
    return out;
}

// Every file name in THUG2's Data\pre archives (lower case): which models THUG2 has itself.
let thug2Files: Set<string> | null = null;
function thug2Has(thug2: string, file: string): boolean {
    if (thug2Files === null) {
        thug2Files = new Set();
        const pre = pyJoin(thug2, "Data", "pre");
        for (const a of fs.existsSync(pre) ? fs.readdirSync(pre) : []) {
            if (!a.toLowerCase().endsWith(".prx")) continue;
            try {
                for (const [n] of prx.entries(pyJoin(pre, a), () => false)) thug2Files.add(n.toLowerCase());
            } catch {
                // (an archive we can't list: skipped)
            }
        }
        // loose files (Data\models\...)
        const walk = (dir: string, rel: string): void => {
            for (const e of fs.existsSync(dir) ? fs.readdirSync(dir, { withFileTypes: true }) : []) {
                if (e.isDirectory()) walk(path.join(dir, e.name), `${rel}${e.name}\\`);
                else thug2Files!.add(`${rel}${e.name}`.toLowerCase());
            }
        };
        walk(pyJoin(thug2, "Data", "models"), "models\\");
    }
    return thug2Files.has(file.toLowerCase());
}

// Every name THUG2's own scripts call (qb_scripts.prx and its levels' *_scripts.prx): what THUG2 can run - its member
// functions too, which the exe's tables (scriptPort.Thug2.members) only partly give.
let thug2CallSet: Set<number> | null = null;
export function thug2Calls(thug2: string): Set<number> {
    if (thug2CallSet === null) {
        thug2CallSet = new Set();
        const pre = pyJoin(thug2, "Data", "pre");
        for (const a of fs.existsSync(pre) ? fs.readdirSync(pre).sort() : []) {
            if (!/(^qb_scripts|_scripts)\.prx$/i.test(a)) continue;
            for (const [, d] of prx.entries(pyJoin(pre, a), (n) => n.toLowerCase().endsWith(".qb"))) {
                try {
                    for (const c of scriptNames(d).calls) thug2CallSet.add(c);
                } catch {
                    // (a file the tokenizer can't read: skipped)
                }
            }
        }
    }
    return thug2CallSet;
}

// THUGPRO's DLL commands with a THUG2 equivalent close enough for its scripts here (the adaptation written into an
// entry's `replace_calls`, applied at convert time by swapping the name: same bytes otherwise): THUGPRO_ForceCreate is
// a THUGPRO global script (its thugpro_utils.q): Create the node, or Kill and Create it again when it is alive - a
// plain Create underneath, so AbsentInNetGames still keeps the node out of a network game (THUG2's Create: 0x005A4DA0
// asks 0x00565BB0, true for AbsentInNetGames in a network or multi-player game); ours differs only in not re-creating
// a live object. edmsg2 / Output are debug prints (THUG2's printf).
export const ADAPT: Readonly<Record<string, string>> = {
    THUGPRO_ForceCreate: "Create",
    edmsg2: "printf",
    Output: "printf",
};

export interface ExtractInput {
    level: string; // our (= THUGPRO's = THPS3's) level name
    base: string; // THPS3's folder name
    thps3Dir: string;
    thugpro: string;
    thug2: scriptPort.Thug2;
    thug2Dir: string;
    globalScripts: ReadonlySet<number>; // THPS3's Data\Scripts script names
    globalQbs: readonly qb.QbFile[]; // THPS3's Data\Scripts files
    globalNames: ReadonlyMap<number, string>;
    handled: ReadonlySet<number>; // node names the other register entries make or place (TP-10 .. TP-18)
}

export function extractNew(input: ExtractInput): { entries: NewEntry[]; leftOut: LeftOut[] } {
    const { level, base, thugpro, thug2 } = input;
    const entries: NewEntry[] = [];
    const leftOut: LeftOut[] = [];
    const archiveName = (suffix: string): string | null => {
        const p = findPath(thugpro, "data", "pre", `${level}${suffix}`);
        return p === null ? null : path.win32.basename(p);
    };
    const scriptsArchive = archiveName("_scripts.prx");
    const scnArchive = archiveName("scn.prx");
    const colArchive = archiveName("col.prx");
    const folder = findFolder(pyJoin(resolveInstall(input.thps3Dir, "thps3"), "Data", "Levels"), base);
    if (scriptsArchive === null || folder === null) return { entries, leftOut };
    const pre = (a: string): string => findPath(thugpro, "data", "pre", a)!;

    // THPS3's side
    const files = levelFiles(folder, base);
    const ownQ = new qb.QbFile(fs.readFileSync(files.get("qb")!), base);
    const ownArray = ownQ.get("NodeArray");
    const own = new Map<number, Struct>();
    for (const n of Array.isArray(ownArray) ? ownArray : [])
        if (n instanceof Struct && n.get("Name") instanceof Name) own.set(nameOf(n)!, n);
    const thps3Scripts = new Set([...input.globalScripts, ...ownQ.scripts.keys()]);
    const world = thps3.readBsp(fs.readFileSync(files.get("bsp")!), files.get("bsp")!);
    const thps3Sectors = new Set(world.sectors.map((s) => s.checksum));
    const dff = files.get("dff");
    const clumps = new Set(dff !== undefined ? thps3.readDff(fs.readFileSync(dff), dff).map((c) => c.checksum) : []);

    // THUGPRO's level scripts files
    const qbFiles: { file: string; data: Buffer }[] = [];
    const names = new Map<number, string>(input.globalNames);
    for (const [k, v] of ownQ.names) names.set(k, v);
    const archiveFiles = new Map<string, Buffer>();
    for (const [n, d] of prx.entries(pre(scriptsArchive))) archiveFiles.set(n, d);
    for (const [n, d] of archiveFiles) {
        if (!n.toLowerCase().startsWith(`levels\\${level}\\`) || !n.toLowerCase().endsWith(".qb")) continue;
        qbFiles.push({ file: n, data: d });
        for (const [k, v] of new qb.QbFile(d, n, true).names) names.set(k, v);
    }
    const text = (v: number): string => names.get(v) ?? `#${hex8(v)}`;
    const levelQb = qbFiles.find((f) => lower(f.file) === `levels\\${level}\\${level}.qb`);
    if (levelQb === undefined) return { entries, leftOut };
    const [theirArray] = nodeArray(levelQb.data, levelQb.file);
    const theirs = new Map<number, Struct>();
    for (const n of theirArray as QbValue[]) if (n instanceof Struct && nameOf(n) !== null) theirs.set(nameOf(n)!, n);
    const scripts = new Map<number, { file: string; code: Buffer }>();
    const theirGlobals = new Map<number, QbValue>();
    for (const f of qbFiles) {
        for (const [k, code] of scriptBytes(f.data)) if (!scripts.has(k)) scripts.set(k, { file: f.file, code });
        for (const [k, v] of new qb.QbFile(f.data, f.file, true).globals) theirGlobals.set(k.value, v);
    }
    const classOf = (n: Struct): string => {
        const c = n.get("Class");
        return c instanceof Name ? lower(text(c.value)) : "-";
    };
    const keyText = (n: Struct, key: string): string | null => {
        const v = n.get(key);
        if (typeof v === "string") return v;
        if (v instanceof Name) {
            const g = theirGlobals.get(v.value); // (a model named by a global: rio_vehicle_model_... = "veh\\...")
            return typeof g === "string" ? g : text(v.value);
        }
        return null;
    };

    // geometry
    const theirSectors = new Map<number, { file: string }>();
    const sceneFiles: [string, thug2Scene.Scene][] = [];
    if (scnArchive !== null) {
        for (const [n, d] of prx.entries(pre(scnArchive), (x) => /\.scn\.xbx$/i.test(x))) {
            const scene = thug2Scene.readScene(d, n);
            sceneFiles.push([n, scene]);
        }
    }
    const [skyBase] = loadScriptInfo(
        new qb.QbFile(
            fs.readFileSync(pyJoin(resolveInstall(input.thps3Dir, "thps3"), "Data", "Scripts", "levels.qb")),
            "levels.qb",
        ),
        base,
    );
    let skyVerts = -1;
    if (skyBase) {
        const skyFolder = findFolder(pyJoin(resolveInstall(input.thps3Dir, "thps3"), "Data", "Levels"), skyBase);
        const skyBsp = skyFolder ? levelFiles(skyFolder, skyBase).get("bsp") : undefined;
        if (skyBsp !== undefined)
            skyVerts = thps3
                .readBsp(fs.readFileSync(skyBsp), skyBsp)
                .sectors.reduce((a, s) => a + s.positions.length, 0);
    }
    let objectSectors = 0;
    for (const [file, scene] of sceneFiles) {
        const isSky = /_sky\\/i.test(file);
        if (isSky) {
            const fresh = scene.sectors.filter((s) => !thps3Sectors.has(s.checksum));
            if (fresh.length) {
                const verts = scene.sectors.reduce((a, s) => a + s.meshes.reduce((b, m) => b + m.vertexCount, 0), 0);
                leftOut.push({
                    kind: "sector",
                    target: `${fresh.length} sky sectors (${path.win32.basename(file)})`,
                    why:
                        `THPS3's own sky (${skyBase ?? "?"}) under THUGPRO's toolchain's checksums - ${verts} vertices against ` +
                        `THPS3's ${skyVerts} (welded per mesh): not new content; ours converts THPS3's sky`,
                });
            }
            continue;
        }
        const texRaw = (() => {
            for (const [n, d] of prx.entries(
                pre(scnArchive!),
                (x) => lower(x) === lower(file.replace(/\.scn\.xbx$/i, ".tex.xbx")),
            ))
                return texFile.readTex(d, n);
            return null;
        })();
        for (const sec of scene.sectors) {
            if (thps3Sectors.has(sec.checksum)) continue;
            const verts = sec.meshes.reduce((a, m) => a + m.vertexCount, 0);
            if (clumps.has(sec.checksum)) {
                // (THPS3's game objects: ours draws them from THPS3's .dff itself)
                objectSectors += 1;
                continue;
            }
            if (own.has(sec.checksum) && verts <= 3) {
                leftOut.push({
                    kind: "sector",
                    target: text(sec.checksum),
                    why: `a ${verts}-vertex stand-in THUGPRO's toolchain gives THPS3's model-less object ${text(sec.checksum)} (a script counter / switch): toolchain leftover, ours makes the object itself`,
                });
                continue;
            }
            const used = new Set(sec.meshes.map((m) => m.material));
            const mats = scene.materials.filter((m) => used.has(m.checksum));
            const textures: Record<string, string> = {};
            for (const m of mats) {
                for (const p of m.passes) {
                    const t = texRaw?.textures.find((x) => x.checksum === p.texture);
                    if (t !== undefined)
                        textures[hex8(t.checksum).toLowerCase()] = itemCrc(
                            texFile.writeTex({ version: 1, textures: [t] }),
                        );
                }
            }
            theirSectors.set(sec.checksum, { file });
            entries.push({
                op: "add",
                id: REGISTER_ID,
                kind: "sector",
                target: { level, name: text(sec.checksum) },
                why: `THUGPRO's own mesh (${verts} vertices) - in no THPS3 file`,
                archive: scnArchive!,
                file,
                crc32: itemCrc(sectorBytes(sec, mats)),
                textures,
            });
        }
    }
    if (objectSectors)
        leftOut.push({
            kind: "sector",
            target: `${objectSectors} game objects' sectors`,
            why: "THPS3's own .dff objects as THUGPRO's toolchain converted them: not new content; ours converts THPS3's .dff",
        });
    const theirCol = new Set<number>();
    if (colArchive !== null) {
        for (const [file, d] of prx.entries(pre(colArchive), (x) => /\.col\.xbx$/i.test(x))) {
            for (const o of thug2Col.readCol(d, file).objects) {
                if (thps3Sectors.has(o.checksum)) continue;
                theirCol.add(o.checksum);
                entries.push({
                    op: "add",
                    id: REGISTER_ID,
                    kind: "collision",
                    target: { level, name: text(o.checksum) },
                    why: own.has(o.checksum)
                        ? `THUGPRO's collision for THPS3's ${text(o.checksum)} (${o.faces.length} faces) - THPS3's has none`
                        : `THUGPRO's own collision (${o.faces.length} faces): a fix or invisible wall in no THPS3 file`,
                    archive: colArchive,
                    file,
                    crc32: itemCrc(colBytes(o)),
                    faces: o.faces.length,
                });
            }
        }
    }

    // scripts: candidates, then what can run in THUG2
    const ours = ourScriptNames(level);
    const candidates = new Set<number>();
    for (const [k] of scripts) {
        if (thps3Scripts.has(k) || thug2.scripts.has(k)) continue;
        if (ours.has(k)) {
            leftOut.push({
                kind: "script",
                target: text(k),
                why: "ours makes a script of this name itself (load_level_anims / <level>_setup / CTF flags)",
            });
            continue;
        }
        candidates.add(k);
    }
    // nodes THUGPRO added
    const addedNodes = new Map<number, Struct>();
    for (const [nm, n] of theirs) {
        if (own.has(nm) || input.handled.has(nm)) continue;
        const geometry = theirSectors.has(nm) || theirCol.has(nm);
        if (!names.has(nm) && !geometry) {
            leftOut.push({
                kind: "node",
                target: text(nm),
                why: `an unnamed ${classOf(n)} without a sector or collision anywhere: a toolchain leftover`,
            });
            continue;
        }
        addedNodes.set(nm, n);
    }
    // THPS3's traffic (a trigger script calling Car_Generic01): ours makes THUG2's cars of them with our own trigger
    // script, so THUGPRO's rewrites of those scripts never run here (thps3_levels PortedNodes.car)
    const carGeneric = qb.checksum("Car_Generic01");
    const isCar = (n: Struct): boolean => {
        const t = n.get("TriggerScript");
        if (!(t instanceof Name)) return false;
        for (const q of [ownQ, ...input.globalQbs]) {
            const body = q.scripts.get(t.value);
            if (body !== undefined)
                return body.some((line) => line.some(([tok, pl]) => tok === 0x16 && pl.readUInt32LE(0) === carGeneric));
        }
        return false;
    };
    const carScripts = new Set<number>();
    for (const n of own.values()) {
        const t = n.get("TriggerScript");
        if (t instanceof Name && isCar(n)) carScripts.add(t.value);
    }
    const adapt = new Map(Object.entries(ADAPT).map(([from, to]) => [qb.checksum(from), qb.checksum(to)]));
    const adapted = (code: Buffer): { calls: Set<number>; refs: Set<number>; replaced: Set<number> } => {
        const { calls, refs } = scriptNames(code);
        const replaced = new Set([...calls].filter((c) => adapt.has(c)));
        return { calls: new Set([...calls].map((c) => adapt.get(c) ?? c)), refs, replaced };
    };
    const overrides = new Set<number>();
    for (const [k, s] of scripts) {
        if (!thps3Scripts.has(k) || carScripts.has(k)) continue;
        const { refs } = scriptNames(s.code);
        if ([...refs].some((r) => (addedNodes.has(r) && !own.has(r)) || candidates.has(r))) overrides.add(k);
    }
    const used = thug2Calls(input.thug2Dir);
    const member = (v: number): boolean => thug2.members.has(v) || used.has(v);
    const excluded = new Map<number, string>();
    for (;;) {
        let changed = false;
        for (const k of [...candidates, ...overrides]) {
            if (excluded.has(k)) continue;
            const { calls, refs } = adapted(scripts.get(k)!.code);
            const unknown = [...calls].filter(
                (c) =>
                    !thug2.cfuncs.has(c) &&
                    !member(c) &&
                    !thug2.scripts.has(c) &&
                    !thps3Scripts.has(c) &&
                    !((candidates.has(c) || overrides.has(c)) && !excluded.has(c)),
            );
            const needs = [...refs].filter((r) => excluded.has(r));
            if (unknown.length || needs.length) {
                excluded.set(
                    k,
                    unknown.length
                        ? `calls what THUG2 hasn't: ${unknown.map(text).join(", ")}`
                        : `uses left-out ${needs.map(text).join(", ")}`,
                );
                changed = true;
            }
        }
        if (!changed) break;
    }
    // context: how the script runs (from the node naming it)
    const contexts = new Map<number, "object" | "skater" | "none">();
    const contextOf = (n: Struct): "object" | "skater" | "none" => {
        const c = classOf(n);
        if (["levelgeometry", "railnode"].includes(c)) return "skater";
        if (["gameobject", "levelobject", "pedestrian", "vehicle", "emitterobject"].includes(c)) return "object";
        return "none";
    };
    for (const n of [...theirs.values()]) {
        const t = n.get("TriggerScript");
        if (t instanceof Name && !contexts.has(t.value)) contexts.set(t.value, contextOf(n));
    }
    const nodeEntry = (nm: number, n: Struct, op: "add" | "set", why: string, extra: Partial<NewEntry>): NewEntry => ({
        op,
        id: REGISTER_ID,
        kind: "node",
        target: { level, name: text(nm) },
        why,
        archive: scriptsArchive,
        file: levelQb.file,
        crc32: itemCrc(nodeBytes(n)),
        ...extra,
    });
    const models = new Map<string, string>(); // folder -> why
    const modelOf = (n: Struct): string | null => keyText(n, "model") ?? keyText(n, "Model");
    const wantModel = (model: string, user: string): boolean => {
        if (lower(model) === "none") return false;
        const folder = model.includes("\\") ? model.slice(0, model.lastIndexOf("\\")) : model;
        const prefix = lower(`models\\${folder}\\`);
        if ([...archiveFiles.keys()].some((f) => lower(f).startsWith(prefix))) {
            if (!models.has(prefix)) models.set(prefix, `THUGPRO's model for ${user}`);
            return true;
        }
        if (thug2Has(input.thug2Dir, `models\\${model}.xbx`)) return true;
        leftOut.push({
            kind: "model",
            target: model,
            why: `named by ${user}: neither THUGPRO's level archive nor THUG2 has it`,
        });
        return false;
    };
    // A shadow vehicle: an added Vehicle its own script locks onto another object (Obj_LockToObject) whose model holds
    // nothing but trigger nodes running what THUG2 hasn't - THUGPRO's carplant_collision boxes on Rio's cars and
    // Suburbia's ice-cream truck (CarPlantNode: one trigger, TRGP_CarPlant01 -> THUGPRO_TriggerCarPlant, a THUGPRO
    // global script launching the skater's Carplant event). Ours would be an invisible box whose trigger does nothing,
    // locked on a car whose own model already carries its collision, car-hop triggers and car plants (THUG2's cars,
    // PC-THPS3-10) - left out, with its script and model (the user's decision 2026-10-21).
    const lockToObject = qb.checksum("Obj_LockToObject");
    const shadowVehicle = (n: Struct, code: Buffer | undefined): string | null => {
        if (classOf(n) !== "vehicle" || code === undefined || !scriptNames(code).calls.has(lockToObject)) return null;
        const model = modelOf(n);
        if (model === null) return null;
        const dot = model.indexOf(".");
        const arrayFile = lower(`models\\${dot < 0 ? model : model.slice(0, dot)}.qb`);
        const data = [...archiveFiles].find(([f]) => lower(f) === arrayFile)?.[1];
        if (data === undefined) return null;
        const modelQb = new qb.QbFile(data, arrayFile, true);
        const array = modelQb.globals.find(([, v]) => Array.isArray(v))?.[1];
        if (!Array.isArray(array) || array.length === 0) return null;
        const modelScripts = scriptBytes(data);
        const missing = new Set<number>();
        for (const node of array) {
            const ts = node instanceof Struct ? node.get("TriggerScript") : undefined;
            const body = ts instanceof Name ? modelScripts.get(ts.value) : undefined;
            if (body === undefined) return null;
            const unknown = [...scriptNames(body).calls].filter(
                (c) => !thug2.cfuncs.has(c) && !member(c) && !thug2.scripts.has(c) && !thps3Scripts.has(c),
            );
            if (!unknown.length) return null;
            for (const c of unknown) missing.add(c);
        }
        return (
            `a shadow Vehicle locked onto another object (Obj_LockToObject) whose model ${model} holds only triggers ` +
            `running what THUG2 hasn't (${[...missing].map(text).join(", ")}): an inert invisible box in ours, on a car ` +
            "that carries its own collision and triggers"
        );
    };
    for (const [nm, n] of addedNodes) {
        const t = n.get("TriggerScript");
        const geometry = theirSectors.has(nm) || theirCol.has(nm);
        const shadow = shadowVehicle(n, t instanceof Name ? scripts.get(t.value)?.code : undefined);
        if (shadow !== null) {
            leftOut.push({ kind: "node", target: text(nm), why: shadow });
            if (t instanceof Name) excluded.set(t.value, `the trigger script of left-out ${text(nm)}`);
            continue;
        }
        const dropKeys: string[] = [];
        if (t instanceof Name && excluded.has(t.value)) {
            if (!geometry) {
                leftOut.push({
                    kind: "node",
                    target: text(nm),
                    why: `its trigger script ${text(t.value)} ${excluded.get(t.value)!}`,
                });
                continue;
            }
            dropKeys.push("TriggerScript");
        }
        const links = n.get("Links");
        const linkNames = (Array.isArray(links) ? links : []).map((i) => {
            const target = typeof i === "number" ? (theirArray as QbValue[])[i] : undefined;
            return target instanceof Struct && nameOf(target) !== null ? text(nameOf(target)!) : "?";
        });
        const model = modelOf(n);
        if (model !== null) wantModel(model, text(nm));
        const what = geometry ? "geometry" : classOf(n);
        // (made only by THUGPRO's rewrites of THPS3's car scripts, which never run here - above: there from the start
        // instead, as our cars are)
        const creators = [...scripts].filter(
            ([k, s2]) => k !== (t instanceof Name ? t.value : -1) && scriptNames(s2.code).refs.has(nm),
        );
        const atStart =
            !n.hasFlag("CreatedAtStart") && creators.length > 0 && creators.every(([k]) => carScripts.has(k));
        entries.push(
            nodeEntry(nm, n, "add", `THUGPRO's own ${what} node - in no THPS3 file`, {
                ...(linkNames.length ? { links: linkNames } : {}),
                ...(dropKeys.length ? { drop_keys: dropKeys } : {}),
                ...(atStart
                    ? {
                          add_flags: ["CreatedAtStart"],
                          adapt_why:
                              "THUGPRO creates it from its rewrite of the car's THPS3 trigger script (THUGPRO_ForceCreate); ours makes THUG2's cars with our own script, there from the start - so is this",
                      }
                    : {}),
            }),
        );
    }
    // THPS3 objects THUGPRO made Vehicles with a model, drawn by ours from THPS3's .dff
    for (const [nm, n] of theirs) {
        const o = own.get(nm);
        if (o === undefined || classOf(n) !== "vehicle" || isCar(o)) continue;
        const model = modelOf(n);
        if (model === null || lower(model) === "none") continue;
        if (!wantModel(model, text(nm))) continue;
        // A Vehicle needs its model's node array: THUG2's car_add_components gives every car a nodearray component
        // (0x00427F20 loads models\<model up to its first '.'>.qb) and a railmanager, whose AddFromNodeArray
        // (0x0057E960) reads the array's size with no null check - a model without its .qb crashes the level's load
        // (Foundry's Obj_Bucket did, 2026-10-21). THUGPRO's archive carries it with the model (taken below with the
        // model's files); THUG2's own models have theirs.
        const dot = model.indexOf(".");
        const arrayFile = lower(`models\\${dot < 0 ? model : model.slice(0, dot)}.qb`);
        if (![...archiveFiles.keys()].some((f) => lower(f) === arrayFile) && !thug2Has(input.thug2Dir, arrayFile)) {
            leftOut.push({
                kind: "node",
                target: text(nm),
                why: `THUGPRO made it a Vehicle with ${model}, but neither THUGPRO's level archive nor THUG2 has its node array ${arrayFile} (a car's rail manager crashes without it): ours keeps THPS3's object`,
            });
            continue;
        }
        const fields: Record<string, string> = { Class: "Vehicle", model };
        const skeleton = keyText(n, "skeletonName");
        if (skeleton !== null) fields.skeletonName = skeleton;
        entries.push(
            nodeEntry(
                nm,
                n,
                "set",
                `THUGPRO made THPS3's ${classOf(o)} a Vehicle with its own model (collides, moves as one)`,
                {
                    fields,
                    drop_sector: true,
                },
            ),
        );
    }
    for (const [prefix, why] of models) {
        const filesOf: Record<string, string> = {};
        // (its .qb too: the model's own node array, which a Vehicle's nodearray / railmanager components load - above)
        for (const [f, d] of archiveFiles) if (lower(f).startsWith(prefix)) filesOf[f] = itemCrc(d);
        entries.push({
            op: "add",
            id: REGISTER_ID,
            kind: "model",
            target: { level, name: prefix.slice("models\\".length, -1) },
            why,
            archive: scriptsArchive,
            files: filesOf,
        });
    }
    // scripts
    for (const k of [...candidates, ...overrides]) {
        const s = scripts.get(k)!;
        if (excluded.has(k)) {
            leftOut.push({ kind: "script", target: text(k), why: excluded.get(k)! });
            continue;
        }
        const replaces = overrides.has(k);
        const { replaced } = adapted(s.code);
        entries.push({
            op: replaces ? "set" : "add",
            id: REGISTER_ID,
            kind: "script",
            target: { level, name: text(k) },
            why: replaces
                ? "THUGPRO's version of a THPS3 script, using its new nodes or scripts (replaces ours)"
                : "THUGPRO's own script - in no THPS3 file",
            archive: scriptsArchive,
            file: s.file,
            crc32: itemCrc(s.code),
            ...(replaces ? { replaces: true } : {}),
            context: contexts.get(k) ?? "none",
            ...(replaced.size
                ? {
                      replace_calls: Object.fromEntries(
                          Object.entries(ADAPT).filter(([from]) => replaced.has(qb.checksum(from))),
                      ),
                  }
                : {}),
        });
    }
    for (const e of entries) e.id = rowId(e.kind, e.op);
    return { entries, leftOut };
}
