// NOT ORIGINAL - THUGPRO's own content in its THAW level ports, brought into ours BY REFERENCE (register rows TPN-THAW-1 ...
// TPN-THAW-5 in tools/compat/thugpro/README.md, the profile's `thugpro_new` section). The user's decision (2026-10-20, with
// Morten1337's permission to include THUGPRO's own content): everything THUGPRO made that is in no source game - its
// meshes and collision, its nodes, its scripts - goes into our ports under the default `--compat thugpro`, kept apart
// from the hand edits to THAW's data (TP-5 ... TP-9) so it is easy to see and to switch off (`--no-thugpro-new`).
//
// Nothing of THUGPRO's is in the repo: the profile names what to take (which archive entry, which sector / collision
// object / node / script, a CRC-32 of THUGPRO's item to check it is still the one we chose, and how it is adapted),
// and the converter reads the bytes from the user's THUGPRO install (`--thugpro <dir>`, default
// %LOCALAPPDATA%\THUG Pro) while converting. An item whose CRC differs, or a missing install, is reported and skipped:
// conversion never fails because of it.
//
// Every entry has an `op` (the two newer sections, thugpro_new and port_changes, all use it):
//   add     create something THAW hasn't (a sector, collision object, node or script of THUGPRO's);
//   set     change something that exists (a sector / collision object THUGPRO reshaped: ours replaced by THUGPRO's; a
//           node's TriggerScript; a script of our port replaced by THUGPRO's version);
//   remove  take out something that exists (a sector THUGPRO emptied).
// `set` / `remove` on something the conversion hasn't is an error the converter reports (and skips), never an add.
//
// Where each kind goes (thaw_levels.ts):
//   - collision objects: into THAW's collision before converting (moved back into THAW's frame, so convertCol treats
//     them as THAW's: its frame shift, the trigger-face rule, breakables), every one with a new BSP tree and reaches;
//   - nodes: appended to THAW's node array before converting (Pos back in THAW's frame; Links by name), then made as
//     THAW's nodes are (peds and cars our port's own way); TriggerScripts set on the array too;
//   - scene sectors: into the converted scene (moved by our frame less THUGPRO's - none with the default frame), with
//     their materials and textures where ours haven't them (a texture of the same checksum but other pixels gets a new
//     checksum, so the two never clash);
//   - scripts: THUGPRO's compiled THUG2 bytes appended to the level's .qb (script_port.appendScripts), ours of the same
//     name taken out first; never one THUG2 itself defines (THUG2's stays);
//   - level options (THUGPRO's host options, SERVER_LEVEL_CHANGE_0n): their toggle scripts, and a setup script that
//     sets each to THUGPRO's default as its HandleLevelOptionObjects does after loading - switching them needs
//     THUGPRO's messages 155-159 (a later protocol layer).

import * as fs from "node:fs";
import { crc32 } from "node:zlib";
import type * as compat from "../compat/compat.ts";
import { Reader, Writer } from "../level_port/binio.ts";
import * as prx from "../level_port/prx.ts";
import * as qb from "../level_port/qb.ts";
import { checksum, Name, type QbValue, Struct, Vector } from "../level_port/qb.ts";
import * as tex from "../level_port/tex.ts";
import * as thug2Col from "../level_port/thug2_col.ts";
import * as thug2Scene from "../level_port/thug2_scene.ts";
import { pyJoin } from "../lib/py.ts";
import { encodeValue, nameValue } from "./thugpro_edits.ts";

export type Op = "add" | "set" | "remove";
export type Kind = "sector" | "collision" | "node" | "trigger_script" | "script" | "level_option";

export interface NewEntry {
    op: Op;
    id: string; // the register row (TPN-THAW-1 ...)
    kind: Kind;
    target: string; // the sector / object / node / script name (trigger_script: the node)
    why: string;
    source?: string; // the archive entry THUGPRO's item is read from
    crc?: number; // CRC-32 of THUGPRO's item (sectorCrc / colCrc / nodeCrc / the script's bytes)
    script?: string; // trigger_script: THUGPRO's script; level_option: the toggle script
    default?: "on" | "off"; // level_option: THUGPRO's state after loading
    flag?: string; // level_option: THUGPRO's global flag
    setup?: boolean; // script: called from the level's setup script (THUGPRO calls it from its <zone>_Setup)
}

export interface LeftOut {
    target: string;
    why: string;
}

export interface ZoneNew {
    entries: NewEntry[];
    left_out: LeftOut[];
    needs?: Record<string, string[]>; // a command THUG2 hasn't (THUGPRO's own C++) -> the scripts left out for it
}

export const DEFAULT_THUGPRO = pyJoin(process.env.LOCALAPPDATA ?? "", "THUG Pro");

// --- CRCs of THUGPRO's items (what the profile checks) -----------------------------------------------------------

export function sectorCrc(s: thug2Scene.Sector): number {
    const w = new Writer();
    thug2Scene.writeSector(w, s);
    return crc32(w.getvalue());
}

export function colCrc(o: thug2Col.ColObject): number {
    return crc32(JSON.stringify([o.verts, o.faces]));
}

// (names as checksums only: the same whatever name texts the reader has)
export function nodeCrc(node: Struct): number {
    return crc32(JSON.stringify(encodeValue(node, new Map())));
}

// --- THUGPRO's level, read from its install -------------------------------------------------------------------------

// Every script of a compiled THUG2 .qb: its bytes from `script` (0x23) to `endscript` (0x24) and a line end - as
// script_port's own, so they can be appended to another .qb as they are (jumps are relative).
export function scriptSpans(data: Buffer): Map<number, Buffer> {
    const out = new Map<number, Buffer>();
    const toks = [...qb.tokens(data, true)];
    for (let i = 0; i + 1 < toks.length; i++) {
        if (toks[i]![1] !== 0x23 || toks[i + 1]![1] !== 0x16) continue;
        const start = toks[i]![0];
        const name = toks[i + 1]![2].readUInt32LE(0);
        let j = i + 2;
        while (j < toks.length && toks[j]![1] !== 0x24) j++;
        if (j >= toks.length) break;
        out.set(name, Buffer.concat([data.subarray(start, toks[j]![0] + 1), Buffer.from([0x01])]));
        i = j;
    }
    return out;
}

// A compiled .qb without the named scripts (ours that THUGPRO's replace).
export function removeScripts(compiled: Buffer, drop: ReadonlySet<number>): Buffer {
    if (!drop.size) return compiled;
    const parts: Buffer[] = [];
    let at = 0;
    const toks = [...qb.tokens(compiled, true)];
    for (let i = 0; i + 1 < toks.length; i++) {
        if (toks[i]![1] !== 0x23 || toks[i + 1]![1] !== 0x16) continue;
        let j = i + 2;
        while (j < toks.length && toks[j]![1] !== 0x24) j++;
        if (j >= toks.length) break;
        if (drop.has(toks[i + 1]![2].readUInt32LE(0))) {
            parts.push(compiled.subarray(at, toks[i]![0]));
            at = toks[j]![0] + 1;
            if (toks[j + 1]?.[1] === 0x01) at = toks[j + 1]![0] + 1;
        }
        i = j;
    }
    parts.push(compiled.subarray(at));
    return Buffer.concat(parts);
}

export interface ThugproLevel {
    key: string;
    scene: thug2Scene.Scene | null;
    textures: tex.TexFile | null;
    col: thug2Col.ColFile | null;
    nodes: QbValue[]; // the node array, its compressed nodes expanded
    names: Map<number, string>; // the 0x2B names of its .qb files
    scripts: Map<number, { file: string; bytes: Buffer; q: qb.QbFile }>; // the level's scripts and THUGPRO's globals
}

const findFile = (folder: string, name: string): string | null => {
    try {
        const f = fs.readdirSync(folder).find((x) => x.toLowerCase() === name.toLowerCase());
        return f === undefined ? null : pyJoin(folder, f);
    } catch {
        return null;
    }
};

// THUGPRO's level `key` (its scene, textures, collision, node array and scripts - its global scripts too), or null
// when the install hasn't it. `globals`: also THUGPRO's qb\thugpro_qb.prx scripts (its own and its versions of
// THUG2's).
export function loadThugproLevel(thugpro: string, key: string, globals = true): ThugproLevel | null {
    const pre = pyJoin(thugpro, "data", "pre");
    const scriptsPrx = findFile(pre, `${key}_scripts.prx`);
    if (scriptsPrx === null) return null;
    const own = (ext: string) => (n: string) => n.toLowerCase() === `levels\\${key}\\${key}${ext}`.toLowerCase();
    const out: ThugproLevel = {
        key,
        scene: null,
        textures: null,
        col: null,
        nodes: [],
        names: new Map(),
        scripts: new Map(),
    };
    const scnPrx = findFile(pre, `${key}scn.prx`);
    if (scnPrx !== null) {
        for (const [, d] of prx.entries(scnPrx, own(".scn.xbx"))) out.scene = thug2Scene.readScene(d, key);
        for (const [, d] of prx.entries(scnPrx, own(".tex.xbx"))) out.textures = texturesOf(d, key);
    }
    const colPrx = findFile(pre, `${key}col.prx`);
    if (colPrx !== null) for (const [, d] of prx.entries(colPrx, own(".col.xbx"))) out.col = thug2Col.readCol(d, key);
    const files: [string, Buffer][] = [
        ...prx.entries(
            scriptsPrx,
            (n) => n.toLowerCase().startsWith(`levels\\${key.toLowerCase()}\\`) && n.toLowerCase().endsWith(".qb"),
        ),
    ];
    const qbPrx = findFile(pre, "thugpro_qb.prx");
    if (globals && qbPrx !== null) files.push(...prx.entries(qbPrx, (n) => n.toLowerCase().endsWith(".qb")));
    for (const [file, data] of files) {
        let q: qb.QbFile;
        try {
            q = new qb.QbFile(data, file, true);
        } catch {
            continue; // (a file our reader doesn't take: its scripts aren't offered)
        }
        for (const [k, v] of q.names) if (!out.names.has(k)) out.names.set(k, v);
        for (const [k, bytes] of scriptSpans(data)) if (!out.scripts.has(k)) out.scripts.set(k, { file, bytes, q });
        if (file.toLowerCase() === `levels\\${key}\\${key}.qb`.toLowerCase()) out.nodes = expandedArray(q);
    }
    return out;
}

// A texture dictionary as tex.readTex reads it, bytes after its last texture ignored (THUGPRO's z_bh.tex.xbx has
// 31 KB more).
function texturesOf(data: Buffer, name: string): tex.TexFile {
    try {
        return tex.readTex(data, name);
    } catch {
        const r = new Reader(data, 0, name);
        const [version, count] = r.read("2I") as [number, number];
        const textures: tex.Texture[] = [];
        for (let k = 0; k < count; k++) {
            const [checksum, width, height, levels, texelDepth, paletteDepth, dxt, paletteSize] = r.read(
                "8I",
            ) as number[];
            const palette = r.bytes(paletteSize!);
            const levelData: Buffer[] = [];
            for (let n = 0; n < levels!; n++) levelData.push(r.bytes(r.u32()));
            textures.push({
                checksum: checksum!,
                width: width!,
                height: height!,
                texelDepth: texelDepth!,
                paletteDepth: paletteDepth!,
                dxt: dxt!,
                palette,
                levels: levelData,
            });
        }
        return { version, textures };
    }
}

// The level's node array with its compressed nodes (a bare name of a global structure) expanded.
function expandedArray(q: qb.QbFile): QbValue[] {
    const array = (q.globals.find(
        ([k, v]) => Array.isArray(v) && (q.names.get(k.value) ?? "").toLowerCase().endsWith("_nodearray"),
    )?.[1] ?? []) as QbValue[];
    const structs = new Map<number, Struct>();
    for (const [k, v] of q.globals) if (v instanceof Struct) structs.set(k.value, v);
    return array.map((node) => {
        if (!(node instanceof Struct)) return node;
        const entries: qb.Entry[] = [];
        for (const [key, value] of node.entries) {
            const included = key === null && value instanceof Name ? structs.get(value.value) : undefined;
            if (included !== undefined) entries.push(...included.entries);
            else entries.push([key, value]);
        }
        return new Struct(entries);
    });
}

// --- scripts: what a script calls ----------------------------------------------------------------------------------

// Tokens after which a name is called (a command or script), not a value: a line end, if / fast if, not, elseif,
// else, ':' (an object's member function).
const CALL_AFTER = new Set([0x01, 0x02, 0x25, 0x47, 0x39, 0x27, 0x42, 0x48, 0x26]);

// The names a script calls (at a statement's start, after if / not / ':') - not an object before ':' nor a local
// being assigned (`x = ...`).
export function calledNames(bytes: Buffer): Set<number> {
    const out = new Set<number>();
    const toks = [...qb.tokens(Buffer.concat([bytes.subarray(6), Buffer.from([0])]), true)];
    let previous = 0x01;
    let depth = 0; // (inside { }, [ ], ( ): values, not calls)
    toks.forEach(([, t, p], i) => {
        const next = toks[i + 1]?.[1];
        if (t === 0x03 || t === 0x05 || t === 0x0e) depth += 1;
        else if (t === 0x04 || t === 0x06 || t === 0x0f) depth = Math.max(0, depth - 1);
        else if (t === 0x16 && !depth && CALL_AFTER.has(previous) && next !== 0x42 && next !== 0x07) {
            out.add(p.readUInt32LE(0));
        }
        if (t !== 0x01 && t !== 0x02) previous = t;
        else previous = 0x01;
    });
    return out;
}

// Every name a script uses.
export function usedNames(bytes: Buffer): Set<number> {
    const out = new Set<number>();
    for (const [, t, p] of qb.tokens(Buffer.concat([bytes.subarray(6), Buffer.from([0])]), true)) {
        if (t === 0x00) break;
        if (t === 0x16) out.add(p.readUInt32LE(0));
    }
    return out;
}

// --- the convert side ------------------------------------------------------------------------------------------------

// The profile's thugpro_new for one zone, checked against THUGPRO's install: what can be applied.
export interface Resolved {
    sectors: Map<number, { op: Op; sector: thug2Scene.Sector | null }>; // add / set (replace) / remove
    collision: Map<number, { op: Op; object: thug2Col.ColObject | null }>;
    nodes: Struct[]; // added nodes as THUGPRO has them (THUGPRO's frame; Links as names in `links`)
    links: Map<Struct, string[]>;
    triggerScripts: Map<string, string>; // node -> THUGPRO's script
    scripts: Map<number, Buffer>; // THUGPRO's scripts to append
    replaced: Set<number>; // ours of these names taken out (`set` scripts)
    options: { script: string; on: boolean }[];
    drivers: Set<number>; // THUGPRO's scripts that drive a car (our port's cars drive when their script does)
    names: Map<number, string>;
    scene: thug2Scene.Scene | null;
    textures: tex.TexFile | null;
    report: string[];
}

// Checks each entry against THUGPRO's level; a missing install, item or a CRC that differs: reported and skipped.
export function resolve(zone: ZoneNew, level: ThugproLevel | null, where: string): Resolved {
    const out: Resolved = {
        sectors: new Map(),
        collision: new Map(),
        nodes: [],
        links: new Map(),
        triggerScripts: new Map(),
        scripts: new Map(),
        replaced: new Set(),
        options: [],
        drivers: new Set(),
        names: new Map(),
        scene: level?.scene ?? null,
        textures: level?.textures ?? null,
        report: [],
    };
    if (level === null) {
        out.report.push(`  thugpro_new: no THUGPRO level in ${where} - ${zone.entries.length} item(s) skipped`);
        return out;
    }
    for (const [k, v] of level.names) out.names.set(k, v);
    const sectors = new Map((level.scene?.sectors ?? []).map((s) => [s.checksum, s] as const));
    const objects = new Map((level.col?.objects ?? []).map((o) => [o.checksum, o] as const));
    const byName = new Map<number, Struct>();
    const indexName = new Map<number, number>();
    level.nodes.forEach((n, i) => {
        const v = n instanceof Struct ? n.get("Name") : undefined;
        if (v instanceof Name) {
            if (!byName.has(v.value)) byName.set(v.value, n as Struct);
            indexName.set(i, v.value);
        }
    });
    const skipped: string[] = [];
    const skip = (e: NewEntry, why: string): void => {
        skipped.push(`${e.kind} ${e.target}: ${why}`);
    };
    const text = (v: number): string => level.names.get(v) ?? `#${v.toString(16).toUpperCase().padStart(8, "0")}`;
    for (const e of zone.entries) {
        const c = nameValue(e.target);
        if (e.kind === "sector") {
            if (e.op === "remove") {
                out.sectors.set(c, { op: "remove", sector: null });
                continue;
            }
            const s = sectors.get(c);
            if (s === undefined) skip(e, "not in THUGPRO's scene");
            else if (e.crc !== undefined && sectorCrc(s) !== e.crc) skip(e, "THUGPRO's sector differs (CRC)");
            else out.sectors.set(c, { op: e.op, sector: s });
        } else if (e.kind === "collision") {
            if (e.op === "remove") {
                out.collision.set(c, { op: "remove", object: null });
                continue;
            }
            const o = objects.get(c);
            if (o === undefined) skip(e, "not in THUGPRO's collision");
            else if (e.crc !== undefined && colCrc(o) !== e.crc) skip(e, "THUGPRO's collision object differs (CRC)");
            else out.collision.set(c, { op: e.op, object: o });
        } else if (e.kind === "node") {
            const n = byName.get(c);
            if (n === undefined) skip(e, "not in THUGPRO's node array");
            else if (e.crc !== undefined && nodeCrc(n) !== e.crc) skip(e, "THUGPRO's node differs (CRC)");
            else {
                const copy = new Struct(n.entries.map(([k, v]) => [k, v]));
                const l = n.get("Links");
                const links = Array.isArray(l)
                    ? l.flatMap((i) => (typeof i === "number" && indexName.has(i) ? [text(indexName.get(i)!)] : []))
                    : [];
                copy.remove("Links");
                out.nodes.push(copy);
                out.links.set(copy, links);
            }
        } else if (e.kind === "trigger_script") {
            out.triggerScripts.set(e.target, e.script!);
        } else if (e.kind === "script") {
            const s = level.scripts.get(c);
            if (s === undefined) skip(e, "not in THUGPRO's scripts");
            else if (e.crc !== undefined && crc32(s.bytes) !== e.crc) skip(e, "THUGPRO's script differs (CRC)");
            else {
                out.scripts.set(c, s.bytes);
                if (e.op === "set") out.replaced.add(c);
                const first = s.q.calls(c)[0]?.[0];
                const called = first === undefined ? "" : (level.names.get(first) ?? "").toLowerCase();
                if (called === "car_generic01" || called === "obj_followpathlinked") out.drivers.add(c);
            }
        } else if (e.kind === "level_option") {
            out.options.push({ script: e.script ?? e.target, on: e.default === "on" });
        }
    }
    // a trigger script whose script was skipped: the node keeps its own
    for (const [node, script] of [...out.triggerScripts]) {
        const own = zone.entries.some((e) => e.kind === "script" && nameValue(e.target) === nameValue(script));
        if (own && !out.scripts.has(nameValue(script))) {
            out.triggerScripts.delete(node);
            skipped.push(`trigger script of ${node}: ${script} skipped`);
        }
    }
    // a level option whose toggle script was skipped: not set
    out.options = out.options.filter((o) => out.scripts.has(nameValue(o.script)));
    if (skipped.length) {
        out.report.push(
            `  thugpro_new: ${skipped.length} item(s) skipped (warning): ${skipped.slice(0, 6).join("; ")}${skipped.length > 6 ? "..." : ""}`,
        );
    }
    return out;
}

// --- geometry: THUGPRO's pieces moved into our frame -----------------------------------------------------------------

const moved = (p: readonly number[], d: readonly number[]): number[] => [0, 1, 2].map((k) => p[k]! + d[k]!);

// A THUGPRO collision object for THAW's collision before converting: moved by `delta` (THUGPRO's frame -> THAW's),
// float vertices, a new box, BSP tree and reaches (every face reachable).
export function colForThaw(o: thug2Col.ColObject, delta: readonly number[]): thug2Col.ColObject {
    const verts = o.verts.map((v) => moved(v, delta).map((x) => Math.fround(x)));
    const lo = [0, 1, 2].map((k) => (verts.length ? Math.min(...verts.map((v) => v[k]!)) : o.bbox[k]!));
    const hi = [0, 1, 2].map((k) => (verts.length ? Math.max(...verts.map((v) => v[k]!)) : o.bbox[k + 4]!));
    const out: thug2Col.ColObject = {
        ...o,
        fixedVerts: 0,
        verts,
        bbox: [...lo, o.bbox[3]!, ...hi, o.bbox[7]!],
        bsp: thug2Col.buildTree(verts, o.faces),
    };
    delete out.bspOff;
    thug2Col.setReaches(out);
    return out;
}

// A THUGPRO scene sector moved by `delta` (a copy; positions in its vertex buffers, boxes and spheres).
export function moveSector(s: thug2Scene.Sector, delta: readonly number[]): thug2Scene.Sector {
    if (delta.every((x) => x === 0)) return s;
    const meshes = s.meshes.map((m) => {
        const buffers = m.buffers.map(([flag, data], b): [number, Buffer] => {
            if (b) return [flag, data];
            const v = Buffer.from(data);
            for (let i = 0; i < m.vertexCount; i++) {
                for (let k = 0; k < 3; k++) {
                    const at = i * m.stride + 4 * k;
                    v.writeFloatLE(v.readFloatLE(at) + delta[k]!, at);
                }
            }
            return [flag, v];
        });
        return {
            ...m,
            buffers,
            sphere: [...moved(m.sphere, delta), m.sphere[3]!],
            bbox: [...moved(m.bbox, delta), ...moved(m.bbox.slice(3), delta)],
        };
    });
    return {
        ...s,
        meshes,
        bbox: [...moved(s.bbox, delta), ...moved(s.bbox.slice(3), delta)],
        sphere: [...moved(s.sphere, delta), s.sphere[3]!],
    };
}

const sameTexture = (a: tex.Texture, b: tex.Texture): boolean =>
    a.width === b.width &&
    a.height === b.height &&
    a.dxt === b.dxt &&
    a.texelDepth === b.texelDepth &&
    a.palette.equals(b.palette) &&
    a.levels.length === b.levels.length &&
    a.levels.every((l, i) => l.equals(b.levels[i]!));

// The resolved sectors merged into our converted scene and textures (in place): added ones appended, reshaped ones
// replacing ours (only where ours is there - a sector the conversion left out stays out), emptied ones removed. A
// material ours has (same checksum) stays ours; THUGPRO's others are added with their textures, a texture whose
// checksum ours uses for other pixels getting a checksum of its own. Returns the counts for the report.
export function mergeScene(
    r: Resolved,
    scene: thug2Scene.Scene,
    textures: tex.TexFile,
    delta: readonly number[],
): string {
    if (!r.sectors.size) return "";
    const theirMaterials = new Map((r.scene?.materials ?? []).map((m) => [m.checksum, m] as const));
    const theirTextures = new Map((r.textures?.textures ?? []).map((t) => [t.checksum, t] as const));
    const ours = new Map(scene.materials.map((m) => [m.checksum, m] as const));
    const ourTextures = new Map(textures.textures.map((t) => [t.checksum, t] as const));
    const renamedTextures = new Map<number, number>();
    let added = 0;
    let replaced = 0;
    let removed = 0;
    let missing = 0;
    const takeMaterial = (c: number): boolean => {
        if (ours.has(c)) return true;
        const m = theirMaterials.get(c);
        if (m === undefined) return false;
        const copy: thug2Scene.Material = { ...m, passes: m.passes.map((p) => ({ ...p })) };
        for (const p of copy.passes) {
            if (!p.texture) continue;
            const t = theirTextures.get(p.texture);
            if (t === undefined) continue; // (a texture THUGPRO's level hasn't either: as THUGPRO draws it)
            const mine = ourTextures.get(p.texture);
            if (mine === undefined) {
                textures.textures.push(t);
                ourTextures.set(t.checksum, t);
            } else if (!sameTexture(mine, t)) {
                let to = renamedTextures.get(p.texture);
                if (to === undefined) {
                    to = checksum(`thugpro_new_${p.texture.toString(16)}`);
                    renamedTextures.set(p.texture, to);
                    const copyT = { ...t, checksum: to };
                    textures.textures.push(copyT);
                    ourTextures.set(to, copyT);
                }
                p.texture = to;
            }
        }
        scene.materials.push(copy);
        ours.set(c, copy);
        return true;
    };
    for (const [c, { op, sector }] of r.sectors) {
        const at = scene.sectors.findIndex((s) => s.checksum === c);
        if (op === "remove") {
            if (at >= 0) {
                scene.sectors.splice(at, 1);
                removed += 1;
            }
            continue;
        }
        if (sector === null) continue;
        if (op === "set" && at < 0) continue; // (ours left out: stays out)
        if (!sector.meshes.every((m) => takeMaterial(m.material))) {
            missing += 1;
            continue;
        }
        const s = moveSector(sector, delta);
        if (op === "add") {
            if (at >= 0) scene.sectors[at] = s;
            else scene.sectors.push(s);
            added += 1;
        } else {
            scene.sectors[at] = s;
            replaced += 1;
        }
    }
    return (
        `${added} sector(s) added, ${replaced} reshaped, ${removed} removed` +
        (missing ? `, ${missing} skipped (material missing)` : "") +
        (renamedTextures.size ? `, ${renamedTextures.size} texture checksum(s) kept apart` : "")
    );
}

// THUGPRO's position of a node in THAW's frame (`frame`: THUGPRO's shift; Position - THUG2's older key, z negated -
// made Pos as THAW's nodes have).
export function nodeForThaw(node: Struct, frame: readonly number[]): Struct {
    const out = new Struct(node.entries.map(([k, v]) => [k, v]));
    const pos = out.get("Pos");
    const position = out.get("Position");
    if (pos instanceof Vector)
        out.set(
            "Pos",
            new Vector(
                "vector",
                [0, 1, 2].map((k) => pos.values[k]! - frame[k]!),
            ),
        );
    else if (position instanceof Vector) {
        out.remove("Position");
        const p = [position.values[0]!, position.values[1]!, -position.values[2]!];
        out.set(
            "Pos",
            new Vector(
                "vector",
                [0, 1, 2].map((k) => p[k]! - frame[k]!),
            ),
        );
    }
    return out;
}

// The setup script that puts THUGPRO's host options in their default state after loading (as its
// HandleLevelOptionObjects does): NeverScript text, or "" when the level has none.
export function optionsSetupNs(
    name: string,
    options: readonly { script: string; on: boolean }[],
    extra: readonly string[],
): string {
    if (!options.length && !extra.length) return "";
    const lines = [...extra.map((s) => `    ${s} `), ...options.map((o) => `    ${o.script} ${o.on ? "on" : "off"} `)];
    return `script ${name} {\n${lines.join("\n")}\n}\n`;
}

// --- the profile's form (compat.Entry: the differences files' shared entry, target {zone, kind, name}) ------------

export function toEntries(zone: string, z: ZoneNew): compat.Entry[] {
    return z.entries.map(({ op, id, kind, target, why, ...rest }) => ({
        id,
        op,
        target: { zone, kind, name: target },
        why,
        ...rest,
    }));
}

// The section's entries by zone (ZoneNew; left_out isn't needed to convert).
export function fromEntries(list: readonly compat.Entry[]): Record<string, ZoneNew> {
    const out: Record<string, ZoneNew> = {};
    for (const { id, op, target, why, ...rest } of list) {
        const zone = String(target.zone).toUpperCase();
        if (out[zone] === undefined) out[zone] = { entries: [], left_out: [] };
        const z = out[zone];
        z.entries.push({
            ...(rest as Partial<NewEntry>),
            op,
            id,
            kind: target.kind as Kind,
            target: String(target.name),
            why,
        });
    }
    return out;
}
