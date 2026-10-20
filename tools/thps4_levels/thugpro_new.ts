// NOT ORIGINAL - what is new in THUGPRO's ports of THPS4's levels (in no source game: its teleports and elevator doors,
// its host level options and their default state, London's kill zones, the nodes it added, its banners), included in
// ours - the user's decision 2026-10-20 (Morten1337's permission). Data: tools/compat/thugpro/thps4_levels.json
// `thugpro_new` (README.md there: "New in THUGPRO (not in any source game)", TPN-THPS4-1 ... TPN-THPS4-5).
//
// By reference only: the profile names the files (THUGPRO's archive, the entry in it, its size and SHA-256) and what
// is taken from them (scripts, LevelChanges entries and nodes by name); the bytes are read from the user's THUGPRO
// install when converting (`--thugpro`, default %LOCALAPPDATA%\THUG Pro) and never stored in the repo. No install, or a
// file that differs: a warning and the level's THUGPRO content left out - never a failure.
//
// extractNew (thugpro_compare.ts --extract-thugpro) writes the section; applyNew (thps4_levels.cmdConvert) applies it.
// Entries ({id, op, rule, target, why, file...}: compat.ts), by rule:
//   script          add  a script of THUGPRO's level files, as THUGPRO compiled it, its THUGPRO-only helpers
//                        (THUGPRO_SafeKill, THUGPRO_ForceCreate, THUGPRO_Televator - scripts of THUGPRO's own qb) made
//                        ours (HELPERS below)
//   trigger_script  set  a THPS4 node's trigger script: THUGPRO's version (it calls THUGPRO's own scripts)
//   node            add  a node THUGPRO added (a waypoint, a ghost) in our node form
//   level_object    set  (<LVL>_LevelChanges `moveto`) a level node made a movable level object at a place, at the start
//   proxim_object   add  (<LVL>_LevelChanges `ProximObject`) a radius trigger running a script, made at the start
//   level_option    add  a host level option's script (<LVL>_LEVEL_CHANGE_0n), run with `off` at the start: THUGPRO's
//                        default state (its host flags SERVER_LEVEL_CHANGE_0n_ON off - HandleLevelOptionObjects, run at
//                        every game's start). Another state needs THUGPRO's protocol (its messages 155-159): not here
//   sound           add  a LoadSound of THUGPRO's level start-up that a taken script plays
//   model           add  (<LVL>_LevelChanges static / billboard models) shown_by_thugpro false: THUGPRO's shipped
//                        scripts skip them (thugpro_levelchanges_add_static_model returns for replace_texture; its
//                        billboard branch is compiled out - GIVE_ME_BILLBOARDS); placed only with --thugpro-banners
// The things made at the start (level objects, radius triggers, models, sounds, the options' default state) are done by
// the level's own `<level>_thugpro_new`, which the level's setup script spawns - as THUGPRO's scripts do them at the
// level's start (thugpro_init_dynamic_level_changes, HandleLevelOptionObjects), with THUG2's own commands.

import { createHash } from "node:crypto";
import * as fs from "node:fs";
import * as path from "node:path";
import * as prx from "../level_port/prx.ts";
import * as qb from "../level_port/qb.ts";
import { checksum, Name, type QbValue, Struct, Vector } from "../level_port/qb.ts";
import { plainTokens, u32 } from "../level_port/script_port.ts";
import { Float, hex8, pyJoin } from "../lib/py.ts";
import { pack } from "../lib/struct.ts";
import { findPath } from "../thaw_levels/thaw_levels.ts";
import { nodeArray } from "../thaw_levels/thugpro_compare.ts";

export const DEFAULT_THUGPRO = pyJoin(process.env.LOCALAPPDATA ?? "", "THUG Pro");

// --- the data -------------------------------------------------------------------------------------------------------

// A file of THUGPRO's install the entries read: its archive (relative to the install), the entry in it, the entry's
// size and SHA-256 (of the decompressed bytes).
export interface FileRef {
    prx: string;
    entry: string;
    size: number;
    sha256: string;
}

export type Rule =
    | "script"
    | "trigger_script"
    | "node"
    | "level_object"
    | "proxim_object"
    | "level_option"
    | "sound"
    | "model";

export interface NewEntry {
    id: string;
    op: "add" | "set" | "remove";
    rule: Rule;
    target: { level: string; [key: string]: unknown };
    why: string;
    file: string; // (FileRef id)
    array?: string; // level_object / proxim_object / model: the LevelChanges array
    index?: number; // its element
    models?: string[]; // model: the model's files (FileRef ids)
    shown_by_thugpro?: boolean;
}

export interface Section {
    files: Record<string, FileRef>;
    entries: NewEntry[];
    notTaken: Record<string, string[]>;
}

// The register rows (README.md): what each entry is part of.
export const ROWS = {
    teleports: "TPN-THPS4-1",
    options: "TPN-THPS4-2",
    kills: "TPN-THPS4-3",
    models: "TPN-THPS4-4",
    nodes: "TPN-THPS4-5",
} as const;

// THUGPRO's own scripts (its thugpro_qb.prx, not in a level's files) the taken scripts call, and ours for them.
export const HELPERS: Readonly<Record<string, string>> = {
    THUGPRO_SafeKill: "thps4_tp_safe_kill",
    THUGPRO_ForceCreate: "thps4_tp_force_create",
    THUGPRO_Televator: "thps4_tp_televator",
};

// Ours: THUG2's own commands doing what THUGPRO's helpers and its level-changes scripts do (the ideas from
// Morten1337's thugpro - code\qb\thugpro\thugpro_utils.q, thugpro_dynamic.q -, written our way; its auto-kick
// switching around the jump boost left out: THUGPRO's own setting, which THUG2 hasn't).
export const HELPER_SCRIPTS: Readonly<Record<string, string>> = {
    thps4_tp_safe_kill: `script thps4_tp_safe_kill {
    if IsAlive name=<name> {
        Kill name=<name>
    }
}
`,
    thps4_tp_force_create: `script thps4_tp_force_create {
    thps4_tp_safe_kill name=<name>
    Create name=<name>
}
`,
    // (run by the skater: a message, a sound, then the skater put at a node - with a jump boost if asked)
    thps4_tp_televator: `script thps4_tp_televator {
    if GotParam text {
        if ! InSplitScreenGame {
            create_panel_message id=leaving_message text=<text> pos=(320.0, 300.0) rgba=[118, 110, 14, 128]
        }
    }
    if GotParam sound {
        PlaySound <sound>
    }
    if GotParam node {
        SK3_TeleportToNode nodename=<node>
    }
    if GotParam jumpboost {
        thps4_tp_jump_boost <...>
    }
}
script thps4_tp_jump_boost {
    if Walking {
        CleanUp_WalkingtoSkating
    }
    if ! IsSkaterOnVehicle {
        MakeSkaterGoto GroundGone
    }
    Debounce X clear=1
    SetSpeed <ForWardPush>
    Jump
    trigger_jump_boost multi=<Boost>
}
`,
    // (a radius trigger: a ghost object whose tags say its radius and the script it runs on a skater coming in)
    thps4_tp_proxim_object: `script thps4_tp_proxim_object {
    if GotParam not_in_ctf_or_koth {
        if InNetGame {
            if GameModeEquals is_king {
                return
            }
            if GameModeEquals is_ctf {
                return
            }
        }
    }
    CreateFromStructure {pos=<pos> Angles=(0.0, 0.0, 0.0) name=<name> class=GameObject CreatedAtStart model="none" TriggerScript=thps4_tp_proxim}
    <name>:SetTags proxim_TriggerScript=<trigger> proxim_radius=<radius>
}
script thps4_tp_proxim {
    Wait 1 gameframe
    GetTags
    Obj_ClearExceptions
    Obj_SetInnerRadius <proxim_radius>
    Obj_SetOuterRadius <proxim_radius>
    SetEventHandler ex=AnyObjectInRadius scr=<proxim_TriggerScript> params={}
}
`,
    // (a level node made a level object - movable - at a place: its geometry is in world space, the object's place an
    // offset)
    thps4_tp_level_object: `script thps4_tp_level_object {
    thps4_tp_safe_kill name=<name>
    CreateFromStructure {pos=<pos> Angles=(0.0, 0.0, 0.0) name=<name> class=LevelObject CreatedAtStart CollisionMode=Geometry}
}
`,
    thps4_tp_model: `script thps4_tp_model {
    if GotParam hide {
        Invisible name=<hide>
    }
    CreateFromStructure {pos=<pos> Angles=(0.0, 0.0, 0.0) name=<name> class=GameObject CreatedAtStart CollisionMode=None model=<model>}
    if GotParam angle {
        <name>:Obj_SetOrientation y=<angle>
    }
}
`,
};
// (which helpers a helper needs)
const NEEDS: Readonly<Record<string, readonly string[]>> = {
    thps4_tp_force_create: ["thps4_tp_safe_kill"],
    thps4_tp_level_object: ["thps4_tp_safe_kill"],
};

export const startName = (level: string): string => `${level}_thugpro_new`;

// --- reading THUGPRO's files ----------------------------------------------------------------------------------------

export const sha256 = (data: Uint8Array): string => createHash("sha256").update(data).digest("hex");

// A script's whole code (0x23 ... 0x24) by its name, as compiled in the file.
export function rawScripts(data: Uint8Array): Map<number, Buffer> {
    const out = new Map<number, Buffer>();
    const buf = Buffer.from(data.buffer, data.byteOffset, data.byteLength);
    let start = -1;
    let name: number | null = null;
    let previous = -1;
    for (const [off, t, p] of qb.tokens(buf, true)) {
        if (t === 0x23) {
            start = off;
            name = null;
        } else if (t === 0x16 && start >= 0 && name === null && previous === 0x23) {
            name = u32(p);
        } else if (t === 0x24 && start >= 0 && name !== null) {
            out.set(name, buf.subarray(start, off + 1));
            start = -1;
        }
        previous = t;
    }
    return out;
}

// The names a script's code uses (0x16 tokens).
export function namesIn(code: Uint8Array): Set<number> {
    const out = new Set<number>();
    for (const [t, p] of plainTokens(code)) if (t === 0x16) out.add(u32(p));
    return out;
}

// The code with names replaced (0x16 tokens: same size, jumps unchanged).
export function renamed(code: Uint8Array, map: ReadonlyMap<number, number>): Buffer {
    const out = Buffer.from(code);
    for (const [off, t, p] of qb.tokens(out, true)) {
        if (t === 0x00) break;
        if (t !== 0x16) continue;
        const to = map.get(u32(p));
        if (to !== undefined) pack("<I", to).copy(out, off + 1);
    }
    return out;
}

export const HELPER_MAP: ReadonlyMap<number, number> = new Map(
    Object.entries(HELPERS).map(([from, to]) => [checksum(from), checksum(to)]),
);

interface Loaded {
    data: Buffer;
    file: qb.QbFile;
    scripts: Map<number, Buffer>;
}

// The level's files from THUGPRO's install, checked against the profile: a reason when one can't be used.
export function loadFiles(
    thugpro: string,
    files: Readonly<Record<string, FileRef>>,
    ids: Iterable<string>,
    cache: Map<string, Buffer | string> = new Map(),
): Map<string, Loaded> | string {
    const out = new Map<string, Loaded>();
    if (!fs.existsSync(thugpro)) return `no THUGPRO install at ${thugpro}`;
    for (const id of new Set(ids)) {
        const ref = files[id];
        if (ref === undefined) return `the profile has no file ${id}`;
        const key = `${ref.prx}|${ref.entry}`.toLowerCase();
        let data = cache.get(key);
        if (data === undefined) {
            const archive = findPath(thugpro, ...ref.prx.split("\\"));
            const found = archive === null ? null : prx.find(archive, ref.entry);
            data = found ?? `${ref.prx} has no ${ref.entry}`;
            if (archive === null) data = `no ${ref.prx}`;
            cache.set(key, data);
        }
        if (typeof data === "string") return data;
        if (data.length !== ref.size || sha256(data) !== ref.sha256)
            return `${ref.entry} isn't the one the profile was made from (size / SHA-256 differ)`;
        const isQb = ref.entry.toLowerCase().endsWith(".qb");
        out.set(id, {
            data,
            file: isQb ? new qb.QbFile(data, ref.entry, true) : new qb.QbFile(Buffer.from([0]), ref.entry),
            scripts: isQb ? rawScripts(data) : new Map(),
        });
    }
    return out;
}

// --- extracting (THUGPRO's install against THPS4's) -------------------------------------------------------------------

type Names = Map<number, string>;
const lower = (v: QbValue | undefined, names: Names): string =>
    v instanceof Name ? (names.get(v.value) ?? hex8(v.value)).toLowerCase() : "";
const text = (v: number, names: Names): string => names.get(v) ?? `#${hex8(v)}`;

// Levels and what of them is left out whatever THUGPRO has: the skate shop is our main menu's backdrop - its door
// stays open (TP-29 off, PC-THPS4-1 / PC-THPS4-2), so nothing of THUGPRO's may change it.
export const LEFT_OUT: Readonly<Record<string, Readonly<Record<string, string>>>> = {
    skateshop: {
        PRACTICE_LEVEL_CHANGE_01:
            "the skate shop's host option (Open Fence): its default state force-creates InvisColl01 - the door walls - " +
            "and the fences: the shop is our main menu's backdrop, its door stays open (TP-29 off)",
    },
};

const LEVEL_CHANGE = /^(\w+)_LEVEL_CHANGE_0([1-8])$/i;

export function extractNew(base: string, key: string, thugpro: string, thps4Dir: string, section: Section): void {
    const archive = findPath(thugpro, "data", "pre", `${key}_scripts.prx`);
    if (archive === null) return;
    const rel = path.relative(thugpro, archive).replaceAll("/", "\\");
    const notTaken: string[] = [];
    const skip = LEFT_OUT[base.toLowerCase()] ?? {};
    const level = base;
    // THUGPRO's level files
    const files = new Map<string, { id: string; data: Buffer; file: qb.QbFile; scripts: Map<number, Buffer> }>();
    for (const [name, data] of prx.entries(archive, (n) => n.toLowerCase().startsWith(`levels\\${key}\\`))) {
        if (!name.toLowerCase().endsWith(".qb")) continue;
        const id = path.win32.basename(name, ".qb").toLowerCase();
        files.set(id, { id, data, file: new qb.QbFile(data, name, true), scripts: rawScripts(data) });
        section.files[`${key}/${id}`] = { prx: rel, entry: name, size: data.length, sha256: sha256(data) };
    }
    const fileId = (id: string): string => `${key}/${id}`;
    const names: Names = new Map();
    for (const f of files.values()) for (const [k, v] of f.file.names) if (!names.has(k)) names.set(k, v);
    const definedIn = new Map<number, string>(); // script -> file id
    for (const f of files.values()) for (const s of f.scripts.keys()) if (!definedIn.has(s)) definedIn.set(s, f.id);
    // THPS4's own
    const folder = findPath(thps4Dir, "Game", "data", "levels", base);
    if (folder === null) throw new TypeError(`no THPS4 ${base} in ${thps4Dir}`);
    const thps4Scripts = new Set<number>();
    let thps4Nodes = new Set<number>();
    for (const file of fs.readdirSync(folder)) {
        if (!file.toLowerCase().endsWith(".qb")) continue;
        const q = new qb.QbFile(fs.readFileSync(path.join(folder, file)), file);
        for (const s of q.scripts.keys()) thps4Scripts.add(s);
        const array = q.get("NodeArray");
        if (Array.isArray(array))
            thps4Nodes = new Set(
                array.flatMap((n) => (n instanceof Struct && n.get("Name") instanceof Name ? [(n.get("Name") as Name).value] : [])),
            );
    }
    const tpOnly = (s: number): boolean => definedIn.has(s) && !thps4Scripts.has(s);
    const isThugproHelper = (s: number): boolean => text(s, names).toLowerCase().startsWith("thugpro_");

    const entries: NewEntry[] = [];
    const scriptRow = new Map<number, string>(); // script -> the row it's taken for (first)
    const roots: [number, string][] = [];
    const push = (e: NewEntry): void => {
        entries.push(e);
    };

    // 1. the node array: trigger scripts of THPS4's nodes calling THUGPRO's own scripts; nodes THUGPRO added
    const levelFile = files.get(key);
    if (levelFile !== undefined) {
        const [array, arrayNames] = nodeArray(levelFile.data, `${key}.qb`);
        for (const [k, v] of arrayNames) if (!names.has(k)) names.set(k, v);
        for (const n of array) {
            if (!(n instanceof Struct)) continue;
            const nm = n.get("Name");
            if (!(nm instanceof Name)) continue;
            const script = n.get("TriggerScript");
            const cls = lower(n.get("Class"), names);
            if (thps4Nodes.has(nm.value)) {
                if (!(script instanceof Name)) continue;
                const code = levelFile.scripts.get(script.value);
                if (code === undefined) continue;
                const calls = [...namesIn(code)].filter((x) => x !== script.value && (tpOnly(x) || isThugproHelper(x)));
                if (!calls.length) continue;
                const teleport = calls.some((x) => text(x, names).toLowerCase() === "thugpro_televator");
                const row = teleport ? ROWS.teleports : ROWS.kills;
                push({
                    id: row,
                    op: "set",
                    rule: "trigger_script",
                    target: { level, node: text(nm.value, names), script: text(script.value, names) },
                    why: teleport
                        ? "THUGPRO's teleport on it (THUGPRO_Televator: our thps4_tp_televator)"
                        : `THUGPRO's version: it calls THUGPRO's own ${calls.map((x) => text(x, names)).join(", ")}`,
                    file: fileId(key),
                });
                for (const c of calls) if (tpOnly(c)) roots.push([c, row]);
            } else if (cls === "waypoint" || (cls === "gameobject" && lower(n.get("Type"), names) === "ghost")) {
                const row = cls === "waypoint" ? ROWS.teleports : ROWS.nodes;
                push({
                    id: row,
                    op: "add",
                    rule: "node",
                    target: { level, node: text(nm.value, names) },
                    why:
                        cls === "waypoint"
                            ? "THUGPRO's waypoint (a teleport's target)"
                            : "THUGPRO's ghost object (its trigger script is empty in THUGPRO: it does nothing)",
                    file: fileId(key),
                });
                if (script instanceof Name && definedIn.has(script.value)) roots.push([script.value, row]);
            }
        }
    }

    // 2. <LVL>_LevelChanges: radius triggers, level objects, models
    for (const f of files.values()) {
        for (const [k, v] of f.file.globals) {
            const arrayName = text(k.value, names);
            if (!/_LevelChanges$/i.test(arrayName) || !Array.isArray(v)) continue;
            v.forEach((e, index) => {
                if (!(e instanceof Struct)) return;
                const base0 = { file: fileId(f.id), array: arrayName, index };
                const name = e.get("name");
                const node = e.get("node");
                if (e.hasFlag("ProximObject")) {
                    const trigger = e.get("Triggerscript");
                    push({
                        id: ROWS.teleports,
                        op: "add",
                        rule: "proxim_object",
                        target: { level, name: name instanceof Name ? text(name.value, names) : `#${index}` },
                        why: "THUGPRO's radius trigger (an elevator's teleport or its doors)",
                        ...base0,
                    });
                    if (trigger instanceof Name && definedIn.has(trigger.value)) roots.push([trigger.value, ROWS.teleports]);
                } else if (e.hasFlag("moveto") && e.hasFlag("relative") && node instanceof Name) {
                    push({
                        id: ROWS.teleports,
                        op: "set",
                        rule: "level_object",
                        target: { level, node: text(node.value, names) },
                        why: "Made a movable level object at the start (THUGPRO's elevator doors and its run-time moves)",
                        ...base0,
                    });
                } else if (typeof e.get("model") === "string") {
                    const model = String(e.get("model")).toLowerCase().replace(/\.mdl$/, "");
                    const models: string[] = [];
                    for (const [mname, mdata] of prx.entries(archive, (n) =>
                        [".mdl.xbx", ".tex.xbx"].some((x) => n.toLowerCase() === `models\\${model}${x}`),
                    )) {
                        const id = `${key}/${mname.toLowerCase()}`;
                        section.files[id] = { prx: rel, entry: mname, size: mdata.length, sha256: sha256(mdata) };
                        models.push(id);
                    }
                    push({
                        id: ROWS.models,
                        op: "add",
                        rule: "model",
                        target: {
                            level,
                            name: name instanceof Name ? text(name.value, names) : `${key}_thugpro_model_${index}`,
                            model: String(e.get("model")),
                        },
                        why:
                            "THUGPRO's banner / poster model - its shipped scripts don't place it (replace_texture / " +
                            "billboard: GIVE_ME_BILLBOARDS off), so off unless --thugpro-banners",
                        ...base0,
                        models: models.sort(),
                        shown_by_thugpro: false,
                    });
                } else {
                    notTaken.push(`${arrayName}[${index}]: not a kind handled here`);
                }
            });
        }
    }

    // 3. host level options: <key>_LEVEL_CHANGE_0n
    for (const [s, id] of definedIn) {
        const m = LEVEL_CHANGE.exec(text(s, names));
        if (!m || m[1]!.toLowerCase() !== key) continue;
        const scriptName = text(s, names);
        if (skip[scriptName] !== undefined) {
            notTaken.push(`${scriptName}: ${skip[scriptName]}`);
            continue;
        }
        push({
            id: ROWS.options,
            op: "add",
            rule: "level_option",
            target: { level, script: scriptName, flag: `SERVER_LEVEL_CHANGE_0${m[2]}_ON` },
            why: "THUGPRO's host level option, run with off at the start (its default state; switching needs its protocol)",
            file: fileId(id),
        });
        roots.push([s, ROWS.options]);
    }

    // 4. the scripts the entries run, and the ones those call (THUGPRO's level files only)
    const taken: number[] = [];
    for (let i = 0; i < roots.length; i++) {
        const [s, row] = roots[i]!;
        if (scriptRow.has(s)) continue;
        scriptRow.set(s, row);
        taken.push(s);
        const code = files.get(definedIn.get(s)!)!.scripts.get(s)!;
        for (const c of namesIn(code)) if (c !== s && definedIn.has(c) && tpOnly(c)) roots.push([c, row]);
    }
    const optionScripts = new Set(entries.filter((e) => e.rule === "level_option").map((e) => checksum(String(e.target.script))));
    for (const s of taken) {
        if (optionScripts.has(s)) continue; // (level_option adds its script)
        if (thps4Scripts.has(s)) continue; // (a trigger_script set replaces THPS4's)
        push({
            id: scriptRow.get(s)!,
            op: "add",
            rule: "script",
            target: { level, script: text(s, names) },
            why: "THUGPRO's script, run by the entries above",
            file: fileId(definedIn.get(s)!),
        });
    }

    // 5. the sounds the taken scripts play, as THUGPRO's level start-up loads them
    const played = new Set<string>();
    const playing = new Set(["playsound", "obj_playsound"].map(checksum));
    for (const s of [...taken, ...entries.filter((e) => e.rule === "trigger_script").map((e) => checksum(String(e.target.script)))]) {
        const code = files.get(definedIn.get(s) ?? key)?.scripts.get(s);
        if (code === undefined) continue;
        const toks = [...plainTokens(code)];
        toks.forEach(([t, p], i) => {
            const next = toks[i + 1];
            const after = toks[i + 2];
            if (t !== 0x16 || next === undefined) return;
            if (playing.has(u32(p)) && next[0] === 0x16) played.add(text(u32(next[1]), names).toLowerCase());
            if (u32(p) === checksum("sound") && next[0] === 0x07 && after?.[0] === 0x16)
                played.add(text(u32(after[1]), names).toLowerCase());
        });
    }
    for (const f of files.values()) {
        for (const [s, statements] of f.file.scripts) {
            if (!/_startup$/i.test(text(s, names))) continue;
            for (const st of statements) {
                if (st[0]?.[0] !== 0x16 || u32(st[0][1]) !== checksum("LoadSound") || st[1]?.[0] !== 0x1b) continue;
                const soundPath = st[1][1].toString("latin1").replace(/\0+$/, "");
                const file = soundPath.split("\\").pop()!.toLowerCase();
                if (!played.has(file)) continue;
                push({
                    id: ROWS.teleports,
                    op: "add",
                    rule: "sound",
                    target: { level, sound: soundPath },
                    why: "A sound THUGPRO's teleports / elevator doors play, loaded as THUGPRO's level start-up does",
                    file: fileId(f.id),
                });
                played.delete(file);
            }
        }
    }
    for (const p of played) notTaken.push(`sound ${p}: played, but THUGPRO's level start-up doesn't load it`);

    // only the files the entries use (and the level's node array: the node names checked when applying)
    if (entries.length) {
        const used = new Set([fileId(key), ...entries.flatMap((e) => [e.file, ...(e.models ?? [])])]);
        for (const id of Object.keys(section.files)) if (id.startsWith(`${key}/`) && !used.has(id)) delete section.files[id];
    } else {
        for (const id of Object.keys(section.files)) if (id.startsWith(`${key}/`)) delete section.files[id];
    }
    section.entries.push(...entries);
    if (notTaken.length) section.notTaken[base] = notTaken;
}

// --- applying ---------------------------------------------------------------------------------------------------------

export interface ApplyContext {
    base: string; // THPS4's level (folder)
    level: string; // ours (alc, thps4_skateshop...)
    converted: Struct[]; // our nodes (added nodes appended)
    names: Names;
    brought: Map<number, Buffer>; // the scripts brought along (THUGPRO's added / replacing)
    text: string; // the NeverScript text the level gets so far (to tell scripts it defines)
    report: string[];
    thugpro: string;
    banners: boolean; // place the models (--thugpro-banners)
    out: string; // the staging folder (the models' files)
}

export interface Applied {
    text: string; // NeverScript to add: our helpers and the start script
    start: string | null; // the start script to spawn from the level's setup script
    done: Map<Rule, number>;
}

const vec = (values: readonly number[]): string => `(${values.map(qb.fmtFloat).join(", ")})`;
const num = (v: QbValue | undefined): number | null =>
    v instanceof Float ? v.value : typeof v === "number" ? v : null;

function pickNode(array: readonly QbValue[], name: string): Struct | undefined {
    const k = checksum(name);
    return array.find((n): n is Struct => n instanceof Struct && n.get("Name") instanceof Name && (n.get("Name") as Name).value === k);
}

// Our node form of a THUGPRO node (Pos -> legacy Position, z negated; Links by name -> our indices).
function ourNode(n: Struct, theirs: readonly QbValue[], at: ReadonlyMap<number, number>, names: Names): Struct | string {
    const entries: qb.Entry[] = [];
    for (const [k, v] of n.entries) {
        if (k !== null && k.value === checksum("Pos") && v instanceof Vector) {
            if (!names.has(checksum("Position"))) names.set(checksum("Position"), "Position");
            entries.push([new Name(checksum("Position")), new Vector("vector", [v.values[0]!, v.values[1]!, -v.values[2]!])]);
        } else if (k !== null && k.value === checksum("Links") && Array.isArray(v)) {
            const links: number[] = [];
            for (const i of v) {
                const t = typeof i === "number" ? theirs[i] : undefined;
                const tn = t instanceof Struct ? t.get("Name") : undefined;
                const ours = tn instanceof Name ? at.get(tn.value) : undefined;
                if (ours === undefined) return "links a node we don't have";
                links.push(ours);
            }
            entries.push([k, links]);
        } else entries.push([k, v]);
    }
    return new Struct(entries);
}

export function applyNew(entries: readonly NewEntry[], files: Readonly<Record<string, FileRef>>, ctx: ApplyContext, cache?: Map<string, Buffer | string>): Applied {
    const done = new Map<Rule, number>();
    const mine = entries.filter((e) => e.target.level.toLowerCase() === ctx.base.toLowerCase());
    const empty: Applied = { text: "", start: null, done };
    if (!mine.length) return empty;
    const wanted = mine.filter((e) => e.rule !== "model" || ctx.banners);
    const ids = new Set(wanted.flatMap((e) => [e.file, ...(e.rule === "model" ? (e.models ?? []) : [])]));
    // (the level's own file too: its node array names the nodes the taken scripts use)
    const key = mine[0]!.file.split("/")[0]!;
    const own = files[`${key}/${key}`] === undefined ? undefined : `${key}/${key}`;
    if (own !== undefined) ids.add(own);
    const loaded = loadFiles(ctx.thugpro, files, ids, cache);
    if (typeof loaded === "string") {
        const warning = `  ${ctx.base}: THUGPRO's own content (thugpro_new, ${mine.length} entries) left out: ${loaded}`;
        ctx.report.push(warning);
        process.stderr.write(`warning:${warning}\n`);
        return empty;
    }
    for (const l of loaded.values()) for (const [k, v] of l.file.names) if (!ctx.names.has(k)) ctx.names.set(k, v);
    for (const [from, to] of Object.entries(HELPERS)) {
        ctx.names.set(checksum(to), to);
        ctx.names.set(checksum(from), from);
    }
    const ourNames = new Map<number, number>();
    ctx.converted.forEach((n, i) => {
        const nm = n.get("Name");
        if (nm instanceof Name && !ourNames.has(nm.value)) ourNames.set(nm.value, i);
    });
    const textDefines = (s: number): boolean =>
        new RegExp(`^script ${(ctx.names.get(s) ?? "\\0").replace(/[^A-Za-z0-9_]/g, "\\$&")} \\{`, "im").test(ctx.text);
    const errors: string[] = [];
    const lines: string[] = [];
    const helpers = new Set<string>();
    const taken: Buffer[] = [];
    const count = (r: Rule): void => {
        done.set(r, (done.get(r) ?? 0) + 1);
    };
    const takeScript = (e: NewEntry, name: string, replacing: boolean): boolean => {
        const s = checksum(name);
        const code = loaded.get(e.file)?.scripts.get(s);
        if (code === undefined) {
            errors.push(`${e.id} ${e.rule}: ${name} not in ${files[e.file]?.entry}`);
            return false;
        }
        if (!replacing && (ctx.brought.has(s) || textDefines(s))) {
            errors.push(`${e.id} ${e.rule}: adds ${name}, which the level has`);
            return false;
        }
        const code2 = renamed(code, HELPER_MAP);
        const used = namesIn(code2);
        for (const to of Object.values(HELPERS)) if (used.has(checksum(to))) helpers.add(to);
        ctx.brought.set(s, code2);
        taken.push(code2);
        return true;
    };
    const element = (e: NewEntry): Struct | null => {
        const array = loaded.get(e.file)?.file.get(e.array ?? "");
        const el = Array.isArray(array) ? array[e.index ?? -1] : undefined;
        if (!(el instanceof Struct)) {
            errors.push(`${e.id} ${e.rule}: ${e.array}[${e.index}] not in ${files[e.file]?.entry}`);
            return null;
        }
        return el;
    };
    // nodes first (the scripts name them)
    const theirArray = own ? nodeArray(loaded.get(own)!.data, "theirs")[0] : [];
    for (const e of wanted.filter((x) => x.rule === "node")) {
        const name = String(e.target.node);
        const n = pickNode(theirArray, name);
        if (n === undefined) errors.push(`${e.id} node: ${name} not in THUGPRO's node array`);
        else if (ourNames.has(checksum(name))) errors.push(`${e.id} node: adds ${name}, which the level has`);
        else {
            const made = ourNode(n, theirArray, ourNames, ctx.names);
            if (typeof made === "string") errors.push(`${e.id} node: ${name} ${made}`);
            else {
                ourNames.set(checksum(name), ctx.converted.length);
                ctx.converted.push(made);
                count("node");
            }
        }
    }
    for (const e of wanted) {
        switch (e.rule) {
            case "script":
                if (takeScript(e, String(e.target.script), false)) count("script");
                break;
            case "trigger_script": {
                const node = String(e.target.node);
                const i = ourNames.get(checksum(node));
                if (i === undefined) {
                    errors.push(`${e.id} trigger_script: sets ${node}'s, which the level hasn't`);
                    break;
                }
                if (!takeScript(e, String(e.target.script), true)) break;
                if (!ctx.names.has(checksum("TriggerScript"))) ctx.names.set(checksum("TriggerScript"), "TriggerScript");
                ctx.converted[i]!.set("TriggerScript", new Name(checksum(String(e.target.script))));
                count("trigger_script");
                break;
            }
            case "level_option":
                if (takeScript(e, String(e.target.script), false)) {
                    lines.push(`    ${String(e.target.script)} off `);
                    count("level_option");
                }
                break;
            case "level_object": {
                const el = element(e);
                if (el === null) break;
                const node = String(e.target.node);
                const pos = el.get("pos");
                if (!ourNames.has(checksum(node))) errors.push(`${e.id} level_object: sets ${node}, which the level hasn't`);
                else if (!(pos instanceof Vector)) errors.push(`${e.id} level_object: ${node} without a pos`);
                else {
                    helpers.add("thps4_tp_level_object");
                    lines.push(`    thps4_tp_level_object name=${node} pos=${vec(pos.values)} `);
                    count("level_object");
                }
                break;
            }
            case "proxim_object": {
                const el = element(e);
                if (el === null) break;
                const pos = el.get("pos");
                const radius = num(el.get("radius"));
                const trigger = el.get("Triggerscript");
                const name = String(e.target.name);
                if (!(pos instanceof Vector) || radius === null || !(trigger instanceof Name)) {
                    errors.push(`${e.id} proxim_object: ${name} without pos / radius / trigger script`);
                    break;
                }
                helpers.add("thps4_tp_proxim_object");
                const flag = el.hasFlag("not_in_ctf_or_koth") ? " not_in_ctf_or_koth" : "";
                lines.push(
                    `    thps4_tp_proxim_object name=${name} pos=${vec(pos.values)} radius=${qb.toNs(radius, ctx.names)} ` +
                        `trigger=${qb.nameText(trigger.value, ctx.names)}${flag} `,
                );
                count("proxim_object");
                break;
            }
            case "sound": {
                const want = String(e.target.sound).toLowerCase();
                let line: string | null = null;
                for (const statements of loaded.get(e.file)?.file.scripts.values() ?? []) {
                    for (const st of statements) {
                        if (st[0]?.[0] !== 0x16 || u32(st[0][1]) !== checksum("LoadSound") || st[1]?.[0] !== 0x1b) continue;
                        if (st[1][1].toString("latin1").replace(/\0+$/, "").toLowerCase() !== want) continue;
                        line = soundLine(st, ctx.names);
                    }
                }
                if (line === null) errors.push(`${e.id} sound: no LoadSound ${String(e.target.sound)} to take`);
                else {
                    lines.unshift(`    ${line} `);
                    count("sound");
                }
                break;
            }
            case "model": {
                const el = element(e);
                if (el === null) break;
                const pos = el.get("pos");
                const node = el.get("node");
                const nodePlace = node instanceof Name ? ctx.converted[ourNames.get(node.value) ?? -1] : undefined;
                const place = pos instanceof Vector ? pos.values : nodePlace ? placeOf(nodePlace) : null;
                if (place === null) {
                    errors.push(`${e.id} model: ${String(e.target.name)} has no place`);
                    break;
                }
                for (const id of e.models ?? []) {
                    const ref = files[id]!;
                    const target = path.join(ctx.out, ...ref.entry.split("\\"));
                    fs.mkdirSync(path.dirname(target), { recursive: true });
                    fs.writeFileSync(target, loaded.get(id)!.data);
                }
                helpers.add("thps4_tp_model");
                const angle = num(el.get("angle"));
                lines.push(
                    `    thps4_tp_model name=${String(e.target.name)} model="${String(e.target.model).replaceAll("\\", "\\\\")}" ` +
                        `pos=${vec(place)}${angle === null ? "" : ` angle=${qb.fmtFloat(angle)}`}` +
                        `${!el.hasFlag("static") && node instanceof Name ? ` hide=${qb.nameText(node.value, ctx.names)}` : ""} `,
                );
                count("model");
                break;
            }
            case "node":
                break;
        }
    }
    // the nodes the taken scripts name that THUGPRO's level has and ours hasn't (a teleport to nowhere...)
    const theirNodes = new Set(
        theirArray.flatMap((n) => (n instanceof Struct && n.get("Name") instanceof Name ? [(n.get("Name") as Name).value] : [])),
    );
    const missing = new Set<string>();
    for (const code of taken) for (const k of namesIn(code)) if (theirNodes.has(k) && !ourNames.has(k)) missing.add(qb.nameText(k, ctx.names));
    if (missing.size) ctx.report.push(`  ${ctx.base}: THUGPRO's scripts name nodes ours hasn't: ${[...missing].sort().join(", ")}`);
    for (const err of errors) {
        ctx.report.push(`  ${ctx.base}: error: ${err}`);
        process.stderr.write(`error: ${ctx.base}: ${err}\n`);
    }
    // our helpers (and what they need)
    for (const h of [...helpers]) for (const n of NEEDS[h] ?? []) helpers.add(n);
    let out = [...helpers]
        .sort()
        .map((h) => HELPER_SCRIPTS[h]!)
        .join("");
    let start: string | null = null;
    if (lines.length) {
        start = startName(ctx.level);
        out += `script ${start} {\n${lines.join("\n")}\n}\n`;
    }
    const skipped = mine.length - wanted.length;
    ctx.report.push(
        `  ${ctx.base}: THUGPRO's own content (thugpro_new) - ${[...done].map(([r, n]) => `${r} ${n}`).join(", ") || "none"}` +
            `${skipped ? `; ${skipped} model(s) not placed (THUGPRO doesn't show them: --thugpro-banners)` : ""}`,
    );
    return { text: out, start, done };
}

function placeOf(n: Struct): number[] | null {
    const p = n.get("Pos");
    if (p instanceof Vector) return [...p.values];
    const q = n.get("Position");
    return q instanceof Vector ? [q.values[0]!, q.values[1]!, -q.values[2]!] : null;
}

// A LoadSound statement as NeverScript: LoadSound "<path>" and its parameters.
function soundLine(st: qb.Statement, names: Names): string | null {
    const parts = ["LoadSound", `"${st[1]![1].toString("latin1").replace(/\0+$/, "").replaceAll("\\", "\\\\")}"`];
    for (let i = 2; i < st.length; i++) {
        const [t, p] = st[i]!;
        if (t !== 0x16) return null;
        const key = qb.nameText(u32(p), names);
        if (st[i + 1]?.[0] === 0x07) {
            const [vt, vp] = st[i + 2] ?? [0, Buffer.alloc(0)];
            if (vt === 0x17) parts.push(`${key}=${vp.readInt32LE(0)}`);
            else if (vt === 0x1a) parts.push(`${key}=${qb.fmtFloat(vp.readFloatLE(0))}`);
            else if (vt === 0x16) parts.push(`${key}=${qb.nameText(u32(vp), names)}`);
            else return null;
            i += 2;
        } else parts.push(key);
    }
    return parts.join(" ");
}

// The setup script text with the start script spawned (or a new setup script): [text, has setup now].
export function hookStart(text: string, setup: string, start: string, hasSetup: boolean): [string, boolean] {
    if (hasSetup) {
        const head = `script ${setup} {\n`;
        const at = text.indexOf(head);
        if (at < 0) throw new TypeError(`no setup script ${setup} to spawn ${start} from`);
        return [`${text.slice(0, at + head.length)}    SpawnScript ${start} \n${text.slice(at + head.length)}`, true];
    }
    return [`${text}script ${setup} {\n    SpawnScript ${start} \n}\n`, true];
}
