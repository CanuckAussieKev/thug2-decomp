// NOT ORIGINAL - THUGPRO's own content in its THPS3 level ports: what is new in THUGPRO, in no source game (Morten1337
// allowed the user to include it, 2026-10-20). The register (tools/compat/thugpro/thps3_levels.json) holds only
// references in its `thugpro_new` section - archive, file, node / sector / script names, a CRC-32 of each item to
// verify - written by `thugpro_compare.ts --extract-thugpro` (thugpro_new_extract.ts); the bytes are read from the
// user's THUGPRO install (--thugpro, default %LOCALAPPDATA%\THUG Pro) each time a level is converted, never copied
// into the repository. A missing install, archive or item, or an item whose CRC differs, is warned about and skipped;
// nothing fails the conversion. Register row: TP-N3 (tools/compat/thugpro/README.md, "New in THUGPRO").
//
// Every entry has `op` (the register's form for the two newer sections, thugpro_new and port_changes):
//   add     - something THPS3 hasn't: a scene sector (+ its materials and textures), a collision object, a node, a
//             script, a model's files;
//   set     - fields of something our port has (a THPS3 node THUGPRO made a Vehicle with its own model): only the
//             given fields change; a target our port hasn't is reported, not added;
//   remove  - (not used by THPS3's entries so far).
// `kind` says what the target is; `id` the register row; `why` one line.

import * as fs from "node:fs";
import { crc32 } from "node:zlib";
import * as geometry from "../level_port/geometry.ts";
import * as prx from "../level_port/prx.ts";
import * as qb from "../level_port/qb.ts";
import { Name, type QbValue, Struct } from "../level_port/qb.ts";
import type * as tex from "../level_port/tex.ts";
import * as texFile from "../level_port/tex.ts";
import * as thug2Col from "../level_port/thug2_col.ts";
import * as thug2Scene from "../level_port/thug2_scene.ts";
import { hex8, pyJoin } from "../lib/py.ts";
import { findPath } from "../thaw_levels/thaw_levels.ts";
import { nameOf, nodeArray } from "../thaw_levels/thugpro_compare.ts";

export const DEFAULT_THUGPRO = pyJoin(process.env.LOCALAPPDATA ?? "", "THUG Pro");
export const REGISTER_ID = "TP-N3";

export type Op = "add" | "set" | "remove";
export type Kind = "sector" | "collision" | "node" | "script" | "model";

export interface NewEntry {
    op: Op;
    id: string;
    kind: Kind;
    target: string;
    why: string;
    archive: string; // THUGPRO's data\pre\<archive>
    file?: string; // the file in it (sector, collision, node, script)
    crc32?: string; // of the item (itemCrc), 8 hex digits
    // sector: its textures {checksum (hex): crc32}; collision: its face count
    textures?: Record<string, string>;
    faces?: number;
    // set: the fields given (Class, model, skeletonName...: THUGPRO's values, a model global resolved to its text)
    fields?: Record<string, string>;
    // set: our drawn copy of the object (THPS3's .dff clump, objectSectors) left out - THUGPRO's model draws it
    drop_sector?: boolean;
    // node: its Links by name (resolved again in our array); keys of THUGPRO's node left out
    links?: string[];
    drop_keys?: string[];
    // node: flags added (an adaptation: adapt_why says why)
    add_flags?: string[];
    adapt_why?: string;
    // script: THUGPRO's DLL commands swapped for THUG2's equivalents ({THUGPRO's: THUG2's}, thugpro_new_extract ADAPT)
    replace_calls?: Record<string, string>;
    // script: a THPS3 script of the same name ours brings is replaced by THUGPRO's (it calls THUGPRO's new content)
    replaces?: boolean;
    // script: how it runs (the translator's context for the THPS3 scripts it calls)
    context?: "object" | "skater" | "none";
    // model: its files {file in the archive: crc32}, written to <staging>\models\...
    files?: Record<string, string>;
}

const crcHex = (data: Uint8Array): string => hex8(crc32(data) >>> 0).toLowerCase();

// The bytes an item's CRC is taken over: re-serialised, so the same content always gives the same CRC.
export function sectorBytes(sector: thug2Scene.Sector, materials: readonly thug2Scene.Material[]): Buffer {
    return thug2Scene.writeScene({ versions: [1, 1, 1], materials: [...materials], sectors: [sector], hierarchyCount: 0, tail: Buffer.alloc(0) });
}
export function colBytes(o: thug2Col.ColObject): Buffer {
    return thug2Col.writeCol({ objects: [o], pad: 0 });
}
export function nodeBytes(node: Struct): Buffer {
    return Buffer.from(qb.toNs(node, new Map()), "latin1");
}
export function itemCrc(data: Uint8Array): string {
    return crcHex(data);
}

// A script's bytes in a THUG2 .qb (0x23 name ... 0x24, then a line end), by name; and the file's top-level globals'
// names.
export function scriptBytes(data: Buffer): Map<number, Buffer> {
    const out = new Map<number, Buffer>();
    const toks = [...qb.tokens(data, true)];
    for (let i = 0; i + 1 < toks.length; i++) {
        if (toks[i]![1] !== 0x23 || toks[i + 1]![1] !== 0x16) continue;
        const name = toks[i + 1]![2].readUInt32LE(0);
        let k = i + 2;
        while (k < toks.length && toks[k]![1] !== 0x24) k++;
        if (k >= toks.length) break;
        out.set(name, Buffer.concat([data.subarray(toks[i]![0], toks[k]![0] + 1), Buffer.from([0x01])]));
        i = k;
    }
    return out;
}

// The names a script's bytes use: `calls` - a name starting a statement or a condition (after a line end, if, else,
// not, and, or, while; outside { } [ ] ( ); not `object:` nor what follows the colon - those run on another object);
// `refs` - every name. (A whole .qb works too: each script's header line is skipped.)
export function scriptNames(code: Uint8Array): { calls: Set<number>; refs: Set<number> } {
    const calls = new Set<number>();
    const refs = new Set<number>();
    const after = new Set([0x01, 0x02, 0x47, 0x48, 0x25, 0x26, 0x39, 0x32, 0x33, 0x20]);
    const toks = [...qb.tokens(Buffer.concat([code, Buffer.from([0])]), true)];
    let depth = 0;
    let header = false; // (on a script's `script <name> <params>` line)
    toks.forEach(([, t, p], k) => {
        if (t === 0x23) header = true;
        else if (t === 0x01 || t === 0x02) header = false;
        else if (t === 0x03 || t === 0x05 || t === 0x0e) depth += 1;
        else if (t === 0x04 || t === 0x06 || t === 0x0f) depth = Math.max(0, depth - 1);
        if (t !== 0x16) return;
        const previous = toks[k - 1]?.[1] ?? -1;
        if (previous === 0x23) return; // (the script's own name)
        const v = p.readUInt32LE(0);
        refs.add(v);
        const next = toks[k + 1]?.[1];
        if (!header && depth === 0 && after.has(previous) && next !== 0x42) calls.add(v);
    });
    return { calls, refs };
}

// --- reading THUGPRO's files at convert time ------------------------------------------------------------------------

// What a level gets from THUGPRO (loadNew): the nodes to add (Links by name), the nodes to set, the scripts, the
// geometry, the models' files.
export interface LevelNew {
    nodes: { node: Struct; links: string[]; entry: NewEntry }[];
    sets: { name: number; fields: Record<string, string>; dropSector: boolean; entry: NewEntry }[];
    scripts: Map<number, { code: Buffer; entry: NewEntry }>;
    sectors: thug2Scene.Sector[];
    materials: thug2Scene.Material[];
    textures: tex.Texture[];
    collision: geometry.SourceColObject[];
    models: [string, Buffer][]; // [path under models\, data]
    names: Map<number, string>; // THUGPRO's checksum names (for writing the node array)
    warnings: string[];
    counts: Record<string, number>;
}

export const emptyNew = (): LevelNew => ({
    nodes: [],
    sets: [],
    scripts: new Map(),
    sectors: [],
    materials: [],
    textures: [],
    collision: [],
    models: [],
    names: new Map(),
    warnings: [],
    counts: {},
});

class Archives {
    private readonly cache = new Map<string, Map<string, Buffer> | null>();
    readonly thugpro: string;
    constructor(thugpro: string) {
        this.thugpro = thugpro;
    }
    // A file of THUGPRO's data\pre\<archive> (case-blind), or null.
    get(archive: string, file: string): Buffer | null {
        let files = this.cache.get(archive.toLowerCase());
        if (files === undefined) {
            const p = findPath(this.thugpro, "data", "pre", archive);
            files = null;
            if (p !== null) {
                files = new Map();
                for (const [n, d] of prx.entries(p)) files.set(n.toLowerCase(), d);
            }
            this.cache.set(archive.toLowerCase(), files);
        }
        return files?.get(file.toLowerCase()) ?? null;
    }
}

// The level's `thugpro_new` entries read from THUGPRO's install. Never throws for THUGPRO's side: what can't be read or
// doesn't match its CRC is a warning and skipped.
export function loadNew(entries: readonly NewEntry[], thugpro: string): LevelNew {
    const out = emptyNew();
    if (!entries.length) return out;
    if (!fs.existsSync(pyJoin(thugpro, "data", "pre")) && findPath(thugpro, "data", "pre") === null) {
        out.warnings.push(`THUGPRO not found at ${thugpro}: its ${entries.length} new items left out`);
        return out;
    }
    const archives = new Archives(thugpro);
    const qbs = new Map<string, { nodes: Map<number, Struct>; scripts: Map<number, Buffer>; names: Map<number, string> }>();
    const qbOf = (archive: string, file: string) => {
        const key = `${archive}|${file}`.toLowerCase();
        let q = qbs.get(key);
        if (q === undefined) {
            const data = archives.get(archive, file);
            if (data === null) return null;
            const [array, names] = nodeArray(data, file);
            const nodes = new Map<number, Struct>();
            for (const n of array as QbValue[]) if (n instanceof Struct && nameOf(n) !== null) nodes.set(nameOf(n)!, n);
            q = { nodes, scripts: scriptBytes(data), names };
            qbs.set(key, q);
        }
        return q;
    };
    const scenes = new Map<string, thug2Scene.Scene | null>();
    const sceneOf = (archive: string, file: string): thug2Scene.Scene | null => {
        const key = `${archive}|${file}`.toLowerCase();
        if (!scenes.has(key)) {
            const data = archives.get(archive, file);
            scenes.set(key, data === null ? null : thug2Scene.readScene(data, file));
        }
        return scenes.get(key)!;
    };
    const texOf = new Map<string, tex.TexFile | null>();
    const textureFile = (archive: string, sceneFile: string): tex.TexFile | null => {
        const file = sceneFile.replace(/\.scn\.xbx$/i, ".tex.xbx");
        const key = `${archive}|${file}`.toLowerCase();
        if (!texOf.has(key)) {
            const data = archives.get(archive, file);
            texOf.set(key, data === null ? null : texFile.readTex(data, file));
        }
        return texOf.get(key)!;
    };
    const cols = new Map<string, Map<number, thug2Col.ColObject> | null>();
    const colOf = (archive: string, file: string): Map<number, thug2Col.ColObject> | null => {
        const key = `${archive}|${file}`.toLowerCase();
        if (!cols.has(key)) {
            const data = archives.get(archive, file);
            cols.set(key, data === null ? null : new Map(thug2Col.readCol(data, file).objects.map((o) => [o.checksum, o])));
        }
        return cols.get(key)!;
    };
    const target = (text: string): number =>
        text.startsWith("#") ? Number.parseInt(text.slice(1), 16) >>> 0 : qb.checksum(text);
    const skip = (e: NewEntry, why: string): void => {
        out.warnings.push(`${e.kind} ${e.target}: ${why} - left out`);
    };
    const count = (k: string): void => {
        out.counts[k] = (out.counts[k] ?? 0) + 1;
    };
    const materialSeen = new Set<number>();
    const textureSeen = new Set<number>();
    for (const e of entries) {
        if (e.op === "remove") {
            skip(e, "remove isn't used for THUGPRO's new content");
            continue;
        }
        try {
            if (e.kind === "model") {
                const files: [string, Buffer][] = [];
                let ok = true;
                for (const [file, crc] of Object.entries(e.files ?? {})) {
                    const data = archives.get(e.archive, file);
                    if (data === null || itemCrc(data) !== crc) {
                        skip(e, data === null ? `${file} not in ${e.archive}` : `${file} differs (CRC)`);
                        ok = false;
                        break;
                    }
                    files.push([file.replace(/^models\\/i, ""), data]);
                }
                if (ok) {
                    out.models.push(...files);
                    count("models");
                }
                continue;
            }
            if (e.file === undefined) {
                skip(e, "no file");
                continue;
            }
            if (e.kind === "sector") {
                const scene = sceneOf(e.archive, e.file);
                const sec = scene?.sectors.find((s) => s.checksum === target(e.target));
                if (!scene || !sec) {
                    skip(e, `not in ${e.archive} ${e.file}`);
                    continue;
                }
                const used = new Set(sec.meshes.map((m) => m.material));
                const mats = scene.materials.filter((m) => used.has(m.checksum));
                if (itemCrc(sectorBytes(sec, mats)) !== e.crc32) {
                    skip(e, "differs from the register (CRC)");
                    continue;
                }
                const texs = textureFile(e.archive, e.file);
                const wanted = Object.entries(e.textures ?? {});
                const found: tex.Texture[] = [];
                for (const [c, crc] of wanted) {
                    const t = texs?.textures.find((x) => x.checksum === Number.parseInt(c, 16) >>> 0);
                    if (t === undefined || itemCrc(texFile.writeTex({ version: 1, textures: [t] })) !== crc) {
                        out.warnings.push(`sector ${e.target}: texture ${c} missing or different - drawn without it`);
                        continue;
                    }
                    found.push(t);
                }
                out.sectors.push(sec);
                for (const m of mats) if (!materialSeen.has(m.checksum)) materialSeen.add(m.checksum), out.materials.push(m);
                for (const t of found) if (!textureSeen.has(t.checksum)) textureSeen.add(t.checksum), out.textures.push(t);
                count("sectors");
            } else if (e.kind === "collision") {
                const o = colOf(e.archive, e.file)?.get(target(e.target));
                if (o === undefined) {
                    skip(e, `not in ${e.archive} ${e.file}`);
                    continue;
                }
                if (itemCrc(colBytes(o)) !== e.crc32) {
                    skip(e, "differs from the register (CRC)");
                    continue;
                }
                // (as a source object: geometry.convertCol puts it on the grid and builds its BSP tree and reaches
                // again, so every face is reachable - THUGPRO's own trees miss some)
                out.collision.push({
                    checksum: o.checksum,
                    flags: o.flags,
                    bbox: [...o.bbox],
                    verts: o.verts.map((v) => [...v]),
                    intensities: Buffer.from(o.intensities),
                    faces: o.faces.map((f) => [...f] as thug2Col.Face),
                });
                count("collision");
            } else if (e.kind === "node") {
                const q = qbOf(e.archive, e.file);
                const node = q?.nodes.get(target(e.target));
                if (!q || !node) {
                    skip(e, `not in ${e.archive} ${e.file}`);
                    continue;
                }
                if (itemCrc(nodeBytes(node)) !== e.crc32) {
                    skip(e, "differs from the register (CRC)");
                    continue;
                }
                for (const [k, v] of q.names) out.names.set(k, v);
                if (e.op === "add") {
                    const copy = new Struct(
                        node.entries.filter(([k]) => {
                            if (k === null) return true;
                            const text = (q.names.get(k.value) ?? "").toLowerCase();
                            return text !== "links" && !(e.drop_keys ?? []).some((d) => d.toLowerCase() === text);
                        }),
                    );
                    for (const flag of e.add_flags ?? []) {
                        // (an adaptation: adapt_why)
                        const c = qb.checksum(flag);
                        if (!copy.entries.some(([k, v]) => k === null && v instanceof Name && v.value === c)) {
                            copy.entries.push([null, new Name(c)]);
                            out.names.set(c, flag);
                        }
                    }
                    out.nodes.push({ node: copy, links: e.links ?? [], entry: e });
                    count("nodes added");
                } else {
                    out.sets.push({
                        name: target(e.target),
                        fields: e.fields ?? {},
                        dropSector: e.drop_sector === true,
                        entry: e,
                    });
                    count("nodes set");
                }
            } else if (e.kind === "script") {
                const q = qbOf(e.archive, e.file);
                const code = q?.scripts.get(target(e.target));
                if (!q || code === undefined) {
                    skip(e, `not in ${e.archive} ${e.file}`);
                    continue;
                }
                if (itemCrc(code) !== e.crc32) {
                    skip(e, "differs from the register (CRC)");
                    continue;
                }
                for (const [k, v] of q.names) out.names.set(k, v);
                out.scripts.set(target(e.target), { code, entry: e });
                count("scripts");
            }
        } catch (err) {
            // (a THUGPRO file we can't read: warned, never fatal)
            skip(e, `unreadable (${(err as Error).message})`);
        }
    }
    return out;
}

// --- applying -------------------------------------------------------------------------------------------------------

// The added nodes appended to the converted node array (their Links by name, resolved in it - a link to a node our
// array hasn't is dropped and reported) and the `set` fields written into our nodes. Returns the report lines.
export function applyNodes(converted: Struct[], names: Map<number, string>, n: LevelNew): string[] {
    const report: string[] = [];
    for (const [k, v] of n.names) if (!names.has(k)) names.set(k, v);
    const key = (text: string): Name => {
        const c = qb.checksum(text);
        if (!names.has(c)) names.set(c, text);
        return new Name(c);
    };
    const index = new Map<number, number>();
    converted.forEach((node, i) => {
        const nm = node.get("Name");
        if (nm instanceof Name && !index.has(nm.value)) index.set(nm.value, i);
    });
    const start = converted.length;
    let clashes = 0;
    const adding = n.nodes.filter(({ node, entry }) => {
        const nm = nameOf(node);
        if (nm !== null && index.has(nm)) {
            report.push(`  ${REGISTER_ID}: node ${entry.target} is in our array already - THUGPRO's left out`);
            clashes += 1;
            return false;
        }
        return true;
    });
    adding.forEach(({ node }, k) => {
        const nm = nameOf(node);
        if (nm !== null) index.set(nm, start + k);
    });
    let droppedLinks = 0;
    for (const { node, links } of adding) {
        const resolved: number[] = [];
        for (const l of links) {
            const i = index.get(l.startsWith("#") ? Number.parseInt(l.slice(1), 16) >>> 0 : qb.checksum(l));
            if (i === undefined) droppedLinks += 1;
            else resolved.push(i);
        }
        if (resolved.length) node.entries.push([key("Links"), resolved]);
        converted.push(node);
    }
    let set = 0;
    for (const s of n.sets) {
        const i = index.get(s.name);
        if (i === undefined) {
            report.push(`  ${REGISTER_ID}: set ${s.entry.target}: not in our node array - reported, not added`);
            continue;
        }
        const node = new Struct([...converted[i]!.entries]);
        for (const [k, v] of Object.entries(s.fields)) {
            // (Class and other names as names, a model as its path text)
            node.set(k, /model/i.test(k) ? v : key(v));
            key(k);
        }
        converted[i] = node;
        set += 1;
    }
    report.push(
        `  ${REGISTER_ID} THUGPRO's new nodes: ${adding.length} added, ${set} set` +
            (droppedLinks ? `, ${droppedLinks} links to nodes we haven't dropped` : "") +
            (clashes ? `, ${clashes} name clashes` : ""),
    );
    return report;
}

// THUGPRO's sectors, materials and textures added to a converted scene and texture dictionary: a material whose
// checksum ours already uses gets a new one (THUGPRO's sectors' meshes follow); a texture ours already has is ours.
export function applyGeometry(
    scene: thug2Scene.Scene,
    textures: tex.TexFile,
    col: thug2Col.ColFile | null,
    n: LevelNew,
    warnings: string[],
): string {
    const ourMaterials = new Set(scene.materials.map((m) => m.checksum));
    const renamed = new Map<number, number>();
    for (const m of n.materials) {
        let c = m.checksum;
        if (ourMaterials.has(c)) {
            c = qb.checksum(`thugpro_new_material_${hex8(m.checksum)}`);
            renamed.set(m.checksum, c);
        }
        ourMaterials.add(c);
        scene.materials.push({ ...m, checksum: c });
    }
    const ourSectors = new Set(scene.sectors.map((s) => s.checksum));
    let sectors = 0;
    for (const s of n.sectors) {
        if (ourSectors.has(s.checksum)) {
            warnings.push(`${REGISTER_ID}: sector ${hex8(s.checksum)} is ours already - THUGPRO's left out`);
            continue;
        }
        scene.sectors.push({
            ...s,
            meshes: s.meshes.map((mesh) => ({ ...mesh, material: renamed.get(mesh.material) ?? mesh.material })),
        });
        sectors += 1;
    }
    const ourTextures = new Set(textures.textures.map((t) => t.checksum));
    let added = 0;
    for (const t of n.textures) {
        if (ourTextures.has(t.checksum)) continue;
        textures.textures.push(t);
        added += 1;
    }
    let objects = 0;
    if (col !== null && n.collision.length) {
        const ourCol = new Set(col.objects.map((o) => o.checksum));
        const fresh = n.collision.filter((o) => {
            if (!ourCol.has(o.checksum)) return true;
            warnings.push(`${REGISTER_ID}: collision ${hex8(o.checksum)} is ours already - THUGPRO's left out`);
            return false;
        });
        const made = geometry.convertCol({ objects: fresh }, () => true, warnings, (flags) => flags);
        col.objects.push(...made.objects);
        objects = made.objects.length;
    }
    return (
        `  ${REGISTER_ID} THUGPRO's new geometry: ${sectors} sectors (${n.materials.length} materials, ${added} textures), ` +
        `${objects} collision objects` +
        (renamed.size ? ` (${renamed.size} materials renumbered: ours used their checksums)` : "")
    );
}

// A script's bytes with the entry's replace_calls applied (each name swapped where it is a name token: the bytes keep
// their length, so if / else jumps stay right).
export function adaptScript(code: Buffer, replace: Readonly<Record<string, string>> | undefined): Buffer {
    if (replace === undefined || !Object.keys(replace).length) return code;
    const swap = new Map(Object.entries(replace).map(([a, b]) => [qb.checksum(a), qb.checksum(b)]));
    const out = Buffer.from(code);
    for (const [offset, t, p] of qb.tokens(Buffer.concat([code, Buffer.from([0])]), true)) {
        if (t === 0x00) break;
        if (t !== 0x16) continue;
        const to = swap.get(p.readUInt32LE(0));
        if (to !== undefined) out.writeUInt32LE(to, offset + 1);
    }
    return out;
}

// What portScripts needs of THUGPRO's scripts (thps3_levels ExtraScripts): the names added to the file, the THPS3
// scripts they and the added nodes' trigger scripts may use (the translator brings those it can), and the `set`
// scripts replacing ours.
export function extraScripts(
    n: LevelNew,
    names: Map<number, string>,
): { provided: number[]; roots: [number, "object" | "skater" | "none"][]; replacing: Map<number, Buffer> } {
    const provided: number[] = [];
    const roots: [number, "object" | "skater" | "none"][] = [];
    const replacing = new Map<number, Buffer>();
    for (const [k, { code, entry }] of n.scripts) {
        if (entry.op === "add") provided.push(k);
        else replacing.set(k, adaptScript(code, entry.replace_calls));
        const context = entry.context ?? "none";
        for (const r of scriptNames(code).refs) roots.push([r, context]);
    }
    for (const { node } of n.nodes) {
        const t = node.get("TriggerScript");
        if (t instanceof Name) roots.push([t.value, "object"]);
    }
    for (const [k, v] of n.names) if (!names.has(k)) names.set(k, v);
    return { provided, roots, replacing };
}

// THUGPRO's added scripts (adapted), but those whose name the level's file has already: [{name: bytes}, clashes].
export function addedScripts(n: LevelNew, existing: ReadonlySet<number>): [Map<number, Buffer>, string[]] {
    const out = new Map<number, Buffer>();
    const clashes: string[] = [];
    for (const [k, { code, entry }] of n.scripts) {
        if (entry.op !== "add") continue;
        if (existing.has(k)) clashes.push(entry.target);
        else out.set(k, adaptScript(code, entry.replace_calls));
    }
    return [out, clashes];
}
