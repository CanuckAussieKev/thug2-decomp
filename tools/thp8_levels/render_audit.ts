// NOT ORIGINAL - what THP8's converted levels draw wrong: z-fighting and missing textures, traced back to THP8's data
// and to what tools/thp8_levels/thp8_levels.ts made of it. Reads the converted levels (the staging folder convert
// writes) and THP8's zone archives; writes nothing.
//
//   node tools/thp8_levels/render_audit.ts textures [levels...] [--top N]
//       Per level: every material of its zones (and stand-ins / barriers) as convertScenes draws it - pass 0's texture
//       (baseTexture) - checked against THP8's data: a blank picked (a flat colour or THP8's stipple placeholder) while
//       the material lists a real one, a material with nothing but blanks, a texture not in the zone's .tex (and
//       where it is), a format not converted, and further layers not drawn (templates whose passes aren't read).
//       Then the converted files checked as THUG2 sees them (tools/level_port/texture_check.ts).
//   node tools/thp8_levels/render_audit.ts zfight [levels...] [--top N] [--csv FILE] [--tolerance IN] [--view M]
//       Per level: tools/level_port/zfight.ts over its .scn.xbx, the pairs named after THP8's sectors and grouped by
//       why they overlap - our own additions (the zones' _lod stand-ins, the network barriers' veils), two zones'
//       copies at a seam, THP8's decals and layers (THP8 draws them with its own depth bias; we give none), duplicates.
//   node tools/thp8_levels/render_audit.ts spot <level> <x,y,z> [radius]
//       The drawn meshes at a place (a screenshot sidecar's skater or camera position, THUG2 units): sector, THP8
//       template, every texture of the material and what pass 0 is drawn with.
//
//   [--thp8 C:\Games\THP8] [--thug2 C:\Games\THUG2 | --staging <folder>]   (default staging: <thug2>\thug2-decomp-thp8)
//
// Levels as convert names them (beavervalley, net_skatepark, training...); the default is all, BEAVER VALLEY first.

import * as fs from "node:fs";
import * as path from "node:path";
import { classOf as nodeClass } from "../level_port/nodes.ts";
import { checksum, type QbValue, Struct } from "../level_port/qb.ts";
import * as tex from "../level_port/tex.ts";
import * as textureCheck from "../level_port/texture_check.ts";
import * as thug2Scene from "../level_port/thug2_scene.ts";
import * as zfight from "../level_port/zfight.ts";
import { parseCli } from "../lib/cli.ts";
import { findCaseless, resolveInstall } from "../lib/install.ts";
import { isMain, print, pyExit, runMain } from "../lib/py.ts";
import * as thaw from "../thaw_levels/thaw.ts";
import * as dxt from "./dxt.ts";
import * as geom from "./geom.ts";
import * as mats from "./materials.ts";
import * as thp8 from "./thp8.ts";
import {
    allSpecs,
    baseTexture,
    compressedFolder,
    findSpec,
    instances,
    type LevelSpec,
    nodeArrayOf,
    nodeName,
    STAGING,
    ZONES,
    zonePak,
} from "./thp8_levels.ts";
import * as xenos from "./xenos.ts";

// --- THP8's side --------------------------------------------------------------------------------------------------

export type PartKind = "zone" | "stand-in" | "barrier";

// One scene part of a level as convertLevel puts it together (props' models left out: small, their own textures).
export interface AuditPart {
    zone: string; // z_houses, z_houses_lod, z_online_suburbs
    kind: PartKind;
    geom: geom.Geom;
    textures: Map<number, xenos.XenosTexture>; // the zone's own .tex (what convertScenes reads)
    elsewhere: Map<number, string>; // textures of the archive's other entries (sub-zones, models): checksum -> entry
    nodes: Set<number>; // node names of its node array (instances, objects)
    masters: Map<number, number>; // instance -> its master sector
    local: Set<number>; // objects drawn in their own space (LevelObjects, game objects): placed by the game, not here
}

export function readAuditPart(
    file: string,
    zone: string,
    kind: PartKind,
    names: Map<number, string>,
): AuditPart | null {
    const pak = thp8.openPak(file);
    const main = pak.entries.filter((e) => e.flags === 0);
    const g = main.find((e) => e.extension === ".geom");
    if (!g) return null;
    const textures = new Map<number, xenos.XenosTexture>();
    const elsewhere = new Map<number, string>();
    for (const e of pak.entries) {
        if (e.extension !== ".tex" && e.extension !== ".stex") continue;
        const own = e.flags === 0 && e.extension === ".tex";
        for (const x of xenos.readXenosTex(thp8.entryData(pak, e), file)) {
            if (own) textures.set(x.checksum, x);
            else if (!elsewhere.has(x.checksum)) elsewhere.set(x.checksum, `${zone} ${e.extension} (flags ${e.flags})`);
        }
    }
    const nodes = new Set<number>();
    const masters = new Map<number, number>();
    const local = new Set<number>();
    const n = main.find((e) => e.extension === ".nqb");
    if (n && kind === "zone") {
        const q = new thaw.QbItems(thp8.entryData(pak, n), `${file} nqb`, names, ">");
        const zoneName = names.get(checksum(zone)) ?? zone;
        let array: QbValue[] = [];
        try {
            array = nodeArrayOf(q, zoneName);
        } catch {
            array = [];
        }
        for (const node of array) {
            if (!(node instanceof Struct) || nodeName(node) === null) continue;
            nodes.add(nodeName(node)!);
            if (["levelobject", "gameobject"].includes(nodeClass(node, names))) local.add(nodeName(node)!);
        }
        for (const [k, v] of instances(array)) masters.set(k, v.master);
    }
    return {
        zone,
        kind,
        geom: geom.readGeom(thp8.entryData(pak, g), file),
        textures,
        elsewhere,
        nodes,
        masters,
        local,
    };
}

// The parts of a level in convertLevel's order: its zones, the city's other zones' stand-ins (a level of the city),
// its barriers.
export function levelParts(spec: LevelSpec, compressed: string, names: Map<number, string>): AuditPart[] {
    const out: AuditPart[] = [];
    const add = (zone: string, kind: PartKind): void => {
        const file = zonePak(compressed, zone);
        const p = file ? readAuditPart(file, zone, kind, names) : null;
        if (p) out.push(p);
    };
    for (const z of spec.zones) add(z, "zone");
    if (spec.zones.some((z) => ZONES.some((x) => x.zone === z && !x.standalone))) {
        for (const info of ZONES.filter((x) => !x.standalone && !spec.zones.includes(x.zone))) {
            add(`${info.zone}_lod`, "stand-in");
        }
    }
    for (const b of spec.barriers) add(b, "barrier");
    return out;
}

// The shader templates' names (FXFILES\MaterialLibrary.bin's strings): checksum -> name.
export function templateNames(compressed: string): Map<number, string> {
    const out = new Map<number, string>();
    const file = findCaseless(compressed, "FXFILES", "MaterialLibrary.bin.xen");
    if (!file) return out;
    const data = thp8.unpack(file);
    let start = -1;
    for (let i = 0; i <= data.length; i++) {
        const c = i < data.length ? data[i]! : 0;
        if (c >= 0x20 && c < 0x7f) {
            if (start < 0) start = i;
        } else {
            if (start >= 0 && i - start >= 2) {
                const s = data.toString("latin1", start, i);
                out.set(checksum(s), s);
            }
            start = -1;
        }
    }
    return out;
}

// --- textures -----------------------------------------------------------------------------------------------------

export type TextureCause =
    | "blank picked, real texture listed"
    | "only blanks listed"
    | "not in the zone's .tex"
    | "format not converted"
    | "no texture"
    | "layers dropped"
    | "layer drawn with a stipple placeholder"
    | "layer drawn with a flat colour"
    | "other textures not drawn (passes not read)";

export interface MaterialVerdict {
    part: AuditPart;
    material: geom.GeomMaterial;
    template: string;
    cause: TextureCause;
    picked: number | undefined;
    pickedLook: textureCheck.Look | null;
    better?: number; // the real texture it should be drawn with
    layer?: number; // the layer (pass 1...) texture the cause is about
    where?: string; // a texture not in the zone's .tex: where it is
    triangles: number;
    area: number; // m2 (THP8 metres; a stand-in's before standInOutside drops what covers the level)
    sectors: number[]; // its sectors (example places)
}

// A Xenos texture's look (null: a normal map / format not decoded).
export function xenosLook(x: xenos.XenosTexture): textureCheck.Look | null {
    const d = mats.decode(x);
    return d ? textureCheck.lookOf(d.bgra, d.width, d.height) : null;
}

const isBlank = (l: textureCheck.Look | null): boolean => l !== null && (l.flat || l.stipple);

// Every material of the parts that convertScenes draws without the texture THP8 does (also the ones whose further
// layers aren't drawn - a template whose passes aren't read listing more than one real texture).
export function auditMaterials(parts: readonly AuditPart[], templates: ReadonlyMap<number, string>): MaterialVerdict[] {
    const out: MaterialVerdict[] = [];
    const looks = new Map<string, textureCheck.Look | null>();
    for (const part of parts) {
        if (part.kind === "barrier") continue; // (drawn untextured on purpose: the veil)
        const look = (t: number): textureCheck.Look | null => {
            const key = `${part.zone}|${t}`;
            if (!looks.has(key)) {
                const x = part.textures.get(t);
                looks.set(key, x ? xenosLook(x) : null);
            }
            return looks.get(key)!;
        };
        const maps = mats.lightMaps(part.geom.materials);
        const used = new Map<
            number,
            { tris: number; area: number; slots: number; sectors: number[]; meshes: geom.GeomMesh[] }
        >();
        for (const s of part.geom.sectors) {
            for (const mesh of s.meshes) {
                const u = used.get(mesh.material) ?? { tris: 0, area: 0, slots: 0, sectors: [], meshes: [] };
                u.meshes.push(mesh);
                if (!u.slots) u.slots = geom.uvSlots(mesh.layout).length;
                const verts = geom.readVertices(mesh);
                for (const t of geom.triangles(mesh)) {
                    const [a, b, c] = t.map((i) => verts[i]!.position) as [number[], number[], number[]];
                    const e1 = [b[0]! - a[0]!, b[1]! - a[1]!, b[2]! - a[2]!];
                    const e2 = [c[0]! - a[0]!, c[1]! - a[1]!, c[2]! - a[2]!];
                    u.area +=
                        Math.hypot(
                            e1[1]! * e2[2]! - e1[2]! * e2[1]!,
                            e1[2]! * e2[0]! - e1[0]! * e2[2]!,
                            e1[0]! * e2[1]! - e1[1]! * e2[0]!,
                        ) / 2;
                    u.tris++;
                }
                if (!u.sectors.includes(s.checksum)) u.sectors.push(s.checksum);
                used.set(mesh.material, u);
            }
        }
        for (const m of part.geom.materials) {
            const u = used.get(m.checksum);
            if (!u || !m.checksum) continue;
            const plan = mats.planMaterial(m, part.textures, maps, u.slots);
            const picked = baseTexture(m, plan, part.textures, maps);
            const template = templates.get(m.template) ?? thp8.hex(m.template);
            const base = {
                part,
                material: m,
                template,
                picked,
                triangles: u.tris,
                area: u.area,
                sectors: u.sectors,
            };
            // the material's real textures: not a normal map, not the zone's light maps, not blank
            const lightMap = (t: number): boolean => maps.ambient.has(t) || maps.shadow.has(t);
            const real = [...new Set(m.textures)].filter(
                (t) =>
                    part.textures.has(t) &&
                    part.textures.get(t)!.format !== xenos.FMT_DXN &&
                    !lightMap(t) &&
                    !isBlank(look(t)),
            );
            if (picked === undefined) {
                // (nothing but dithers listed: drawn untextured, vertex colours only - as intended)
                const listed = m.textures.filter((t) => part.textures.get(t)?.format !== xenos.FMT_DXN);
                if (listed.length && listed.every((t) => look(t)?.stipple === true || lightMap(t))) continue;
                out.push({ ...base, cause: "no texture", pickedLook: null });
                continue;
            }
            const x = part.textures.get(picked);
            if (!x) {
                const where = parts.map((p) => p.elsewhere.get(picked)).find((w) => w !== undefined);
                out.push({
                    ...base,
                    cause: "not in the zone's .tex",
                    pickedLook: null,
                    ...(where !== undefined ? { where } : { where: "nowhere in the level's archives" }),
                });
                continue;
            }
            if (xenos.unconverted(x) !== null) {
                out.push({ ...base, cause: "format not converted", pickedLook: null, where: xenos.unconverted(x)! });
                continue;
            }
            const l = look(picked);
            if (isBlank(l)) {
                // (the biggest real texture: the diffuse map, not a mask)
                const better = real.sort((p, q) => {
                    const a = part.textures.get(p)!;
                    const b = part.textures.get(q)!;
                    return b.width * b.height - a.width * a.height;
                })[0];
                out.push({
                    ...base,
                    cause: better !== undefined ? "blank picked, real texture listed" : "only blanks listed",
                    pickedLook: l,
                    ...(better !== undefined ? { better } : {}),
                });
                continue;
            }
            if (plan.layers.length > 1) {
                const drops = layerDrops(plan, x, u.meshes, part.textures, look);
                if (drops.dropped) out.push({ ...base, cause: "layers dropped", pickedLook: l, where: drops.why });
                if (drops.stipple !== undefined) {
                    const layer = drops.stipple;
                    out.push({
                        ...base,
                        cause: "layer drawn with a stipple placeholder",
                        pickedLook: look(layer),
                        layer,
                    });
                }
                if (drops.flat !== undefined) {
                    const layer = drops.flat;
                    out.push({ ...base, cause: "layer drawn with a flat colour", pickedLook: look(layer), layer });
                }
            } else if (!mats.LAYERED.has(m.template) && real.filter((t) => t !== picked).length) {
                out.push({ ...base, cause: "other textures not drawn (passes not read)", pickedLook: l });
            }
        }
    }
    return out;
}

// What convertScenes does with a plan's further layers (pass 1...): a blended base keeps none (its vertex alpha is its
// own); a layer whose mask shares its uvs is a texture made of both (dropped when either doesn't decode); one whose mask
// has uvs of its own goes into the vertex alpha - one layer a material can (the others dropped).
export function layerDrops(
    plan: mats.MaterialPlan,
    base: xenos.XenosTexture,
    meshes: readonly geom.GeomMesh[],
    textures: ReadonlyMap<number, xenos.XenosTexture>,
    look: (t: number) => textureCheck.Look | null,
): { dropped: number; stipple?: number; flat?: number; why: string } {
    const layers = plan.layers.slice(1);
    const blended =
        base.format === xenos.FMT_DXT5 || (base.format === xenos.FMT_DXT1 && dxt.hasAlpha1(xenos.baseLevel(base)));
    if (blended) return { dropped: layers.length, why: "base texture has alpha" };
    const decodes = (t: number | null): boolean =>
        t !== null && textures.has(t) && mats.decode(textures.get(t)!) !== null;
    const converts = (t: number): boolean => textures.has(t) && xenos.unconverted(textures.get(t)!) === null;
    let dropped = 0;
    let stipple: number | undefined;
    let flat: number | undefined;
    let vertexMask = false;
    const why = new Set<string>();
    for (const layer of layers) {
        // (a dither layer is left out by convertScenes - no longer a finding)
        if (look(layer.diffuse)?.stipple) continue;
        if (look(layer.diffuse)?.flat) flat ??= layer.diffuse;
        const shared =
            layer.maskSlot === layer.slot ||
            meshes.every((mesh) =>
                geom.readVertices(mesh).every((v) => {
                    const a = v.uvs[layer.slot];
                    const b = v.uvs[layer.maskSlot];
                    return a && b && Math.abs(a[0]! - b[0]!) < 1e-3 && Math.abs(a[1]! - b[1]!) < 1e-3;
                }),
            );
        if (shared) {
            if (!decodes(layer.diffuse) || !decodes(layer.mask)) {
                dropped++;
                why.add("a layer's texture doesn't decode");
            }
        } else if (vertexMask || !converts(layer.diffuse) || !decodes(layer.mask)) {
            dropped++;
            why.add(
                vertexMask
                    ? "a second mask with uvs of its own (one fits the vertex alpha)"
                    : "a layer's texture doesn't decode",
            );
        } else vertexMask = true;
    }
    return {
        dropped,
        ...(stipple !== undefined ? { stipple } : {}),
        ...(flat !== undefined ? { flat } : {}),
        why: [...why].join("; "),
    };
}

// --- the converted level ------------------------------------------------------------------------------------------

export interface Staged {
    scene: thug2Scene.Scene;
    textures: tex.TexFile;
    centres: Map<number, number[]>; // sector -> its box's middle (THUG2 units)
}

export function readStaged(staging: string, level: string): Staged | null {
    const base = path.join(staging, "levels", level, level);
    if (!fs.existsSync(`${base}.scn.xbx`)) return null;
    const scene = thug2Scene.readScene(fs.readFileSync(`${base}.scn.xbx`), level);
    const textures = fs.existsSync(`${base}.tex.xbx`)
        ? tex.readTex(fs.readFileSync(`${base}.tex.xbx`), level)
        : { version: 1, textures: [] };
    const centres = new Map<number, number[]>();
    for (const s of scene.sectors)
        centres.set(
            s.checksum,
            [0, 1, 2].map((k) => (s.bbox[k]! + s.bbox[k + 3]!) / 2),
        );
    return { scene, textures, centres };
}

const m2s = (x: number): string => x.toFixed(0);

export function textureReport(
    level: string,
    verdicts: readonly MaterialVerdict[],
    names: ReadonlyMap<number, string>,
    staged: Staged | null,
    top: number,
): string[] {
    const nm = (c: number): string => names.get(c) ?? thp8.hex(c);
    const lines: string[] = [`${level} - THP8's materials as converted:`];
    const causes = new Map<TextureCause, MaterialVerdict[]>();
    for (const v of verdicts) causes.set(v.cause, [...(causes.get(v.cause) ?? []), v]);
    if (!verdicts.length) lines.push("  every material drawn with a real texture");
    for (const [cause, list] of [...causes].sort(
        (a, b) => b[1].reduce((n, v) => n + v.area, 0) - a[1].reduce((n, v) => n + v.area, 0),
    )) {
        const area = list.reduce((n, v) => n + v.area, 0);
        const tris = list.reduce((n, v) => n + v.triangles, 0);
        lines.push(`  ${cause}: ${list.length} materials, ${tris} triangles, ${m2s(area)} m2`);
        // by template
        const byTemplate = new Map<string, MaterialVerdict[]>();
        for (const v of list) byTemplate.set(v.template, [...(byTemplate.get(v.template) ?? []), v]);
        for (const [template, vs] of [...byTemplate].sort(
            (a, b) => b[1].reduce((n, v) => n + v.area, 0) - a[1].reduce((n, v) => n + v.area, 0),
        )) {
            lines.push(
                `    ${template}: ${vs.length} materials, ${vs.reduce((n, v) => n + v.triangles, 0)} triangles, ${m2s(vs.reduce((n, v) => n + v.area, 0))} m2`,
            );
        }
        // by texture picked
        const byTexture = new Map<string, MaterialVerdict[]>();
        for (const v of list) {
            const shown = v.layer ?? v.picked;
            const key = shown === undefined ? "(none)" : `${v.layer !== undefined ? "layer " : ""}${thp8.hex(shown)}`;
            byTexture.set(key, [...(byTexture.get(key) ?? []), v]);
        }
        lines.push(`    largest, by the ${list.some((v) => v.layer !== undefined) ? "layer's " : ""}texture drawn:`);
        for (const [key, vs] of [...byTexture]
            .sort((a, b) => b[1].reduce((n, v) => n + v.area, 0) - a[1].reduce((n, v) => n + v.area, 0))
            .slice(0, top)) {
            const v = vs.reduce((p, q) => (q.area > p.area ? q : p));
            const sector = v.sectors.find((s) => staged?.centres.has(s)) ?? v.sectors[0]!;
            const at = staged?.centres.get(sector);
            const zones = [...new Set(vs.map((x) => x.part.zone))].join(", ");
            lines.push(
                `      ${key}${textureCheck.describeLook(v.pickedLook ?? undefined)}${v.better !== undefined ? ` instead of ${thp8.hex(v.better)}` : ""}${v.where ? ` [${v.where}]` : ""}: ` +
                    `${vs.length} materials (${zones}), ${vs.reduce((n, x) => n + x.triangles, 0)} triangles, ${m2s(vs.reduce((n, x) => n + x.area, 0))} m2 - ` +
                    `e.g. ${nm(sector)} (${v.template})${at ? ` at try_spot ${level} ${at.map((x) => x.toFixed(0)).join(",")},0` : ""}`,
            );
        }
    }
    return lines;
}

// --- z-fighting ---------------------------------------------------------------------------------------------------

export interface Origin {
    part: AuditPart;
    master?: number; // an instance's master sector
}

// Sector checksum -> the part it comes from (a sector of its scene, or an instance / object node of its node array).
export function sectorOrigins(parts: readonly AuditPart[]): Map<number, Origin> {
    const out = new Map<number, Origin>();
    for (const part of parts) {
        for (const s of part.geom.sectors) if (!out.has(s.checksum)) out.set(s.checksum, { part });
        for (const n of part.nodes) {
            if (out.has(n)) continue;
            const master = part.masters.get(n);
            out.set(n, master !== undefined ? { part, master } : { part });
        }
    }
    return out;
}

// Material checksum as converted -> THP8's material and its part (convertScenes names a material a later part
// defines otherwise thp8_material_<checksum>_<part index>; the index counts the props' parts too, so a range is tried).
export function materialOrigins(parts: readonly AuditPart[]): Map<number, { part: AuditPart; m: geom.GeomMaterial }> {
    const out = new Map<number, { part: AuditPart; m: geom.GeomMaterial }>();
    for (const part of parts) {
        for (const m of part.geom.materials) {
            if (!out.has(m.checksum)) out.set(m.checksum, { part, m });
        }
    }
    for (const part of parts) {
        for (const m of part.geom.materials) {
            for (let k = 0; k < parts.length + 64; k++) {
                const c = checksum(`thp8_material_${thp8.hex(m.checksum)}_${k}`);
                if (!out.has(c)) out.set(c, { part, m });
            }
        }
    }
    return out;
}

export function fightCause(
    f: zfight.Fight,
    scene: thug2Scene.Scene,
    origins: ReadonlyMap<number, Origin>,
    materials: ReadonlyMap<number, { part: AuditPart; m: geom.GeomMaterial }>,
    templates: ReadonlyMap<number, string>,
): string {
    const sa = scene.sectors[f.a.sector]!;
    const sb = scene.sectors[f.b.sector]!;
    const oa = origins.get(sa.checksum);
    const ob = origins.get(sb.checksum);
    const ma = scene.materials[f.a.material];
    const mb = scene.materials[f.b.material];
    const kinds = [oa?.part.kind, ob?.part.kind];
    if (kinds.includes("stand-in")) {
        return kinds[0] === kinds[1] ? "stand-ins over each other (ours)" : "stand-in (_lod) over the level (ours)";
    }
    if (kinds[0] === "barrier" && kinds[1] === "barrier") return "barrier veils over each other (ours)";
    if (kinds.includes("barrier")) return "barrier veil on the level (ours)";
    if (oa && ob && oa.part.zone !== ob.part.zone) return "zone seam: two zones draw it";
    const template = (m: thug2Scene.Material | undefined): string => {
        const o = m ? materials.get(m.checksum) : undefined;
        return o ? (templates.get(o.m.template) ?? "") : "";
    };
    const layered = (m: thug2Scene.Material | undefined): boolean =>
        m !== undefined && Boolean((m.passes[0]?.flags ?? 0) & (thug2Scene.PASS_TRANSPARENT | thug2Scene.PASS_DECAL));
    if (/decal|poster/i.test(template(ma)) || /decal|poster/i.test(template(mb)))
        return "THP8 decal (its depth bias not converted)";
    if (layered(ma) || layered(mb)) return "see-through layer on a surface (no z bias)";
    if (f.a.sector === f.b.sector) return "two materials of one object";
    if (oa?.master !== undefined && oa.master === ob?.master) return "two copies of one instance master";
    if (ma?.passes[0]?.texture === mb?.passes[0]?.texture) return "duplicate surface in one zone (same texture)";
    return "overlapping objects in one zone";
}

// --- spot -----------------------------------------------------------------------------------------------------------

export function spotReport(
    level: string,
    staged: Staged,
    parts: readonly AuditPart[],
    names: ReadonlyMap<number, string>,
    templates: ReadonlyMap<number, string>,
    at: readonly number[],
    radius: number,
): string[] {
    const nm = (c: number): string => names.get(c) ?? thp8.hex(c);
    const materialsBy = materialOrigins(parts);
    const verdicts = new Map<string, MaterialVerdict>();
    for (const v of auditMaterials(parts, templates)) verdicts.set(`${v.part.zone}|${v.material.checksum}`, v);
    const texLooks = new Map(staged.textures.textures.map((t) => [t.checksum, t]));
    const lines = [`${level}: drawn meshes within ${radius} units of ${at.map((x) => x.toFixed(1)).join(", ")}`];
    for (const s of staged.scene.sectors) {
        s.meshes.forEach((mesh) => {
            const b = mesh.bbox;
            const d = [0, 1, 2].map((k) => Math.max(b[k]! - at[k]!, 0, at[k]! - b[k + 3]!));
            if (Math.hypot(d[0]!, d[1]!, d[2]!) > radius) return;
            const mat = staged.scene.materials.find((m) => m.checksum === mesh.material);
            const o = materialsBy.get(mesh.material);
            const t = mat?.passes[0]?.texture ?? 0;
            const st = texLooks.get(t);
            const data = st ? textureCheck.texels(st) : null;
            const look = data && st ? textureCheck.lookOf(data, st.width, st.height) : undefined;
            const v = o ? verdicts.get(`${o.part.zone}|${o.m.checksum}`) : undefined;
            lines.push(
                `  ${nm(s.checksum)} mesh box ${b.map((x) => x.toFixed(0)).join(",")}: material ${thp8.hex(mesh.material)}` +
                    (o
                        ? ` = ${o.part.zone} ${thp8.hex(o.m.checksum)} ${templates.get(o.m.template) ?? thp8.hex(o.m.template)}`
                        : ""),
            );
            lines.push(
                `      drawn with ${thp8.hex(t)}${st ? ` ${st.width}x${st.height}` : " (not in the .tex)"}${textureCheck.describeLook(look)}` +
                    `${mat && mat.passes.length > 1 ? ` + ${mat.passes.length - 1} layers` : ""}${v ? ` - ${v.cause}${v.better !== undefined ? ` (real: ${thp8.hex(v.better)})` : ""}` : ""}`,
            );
            if (o) {
                const list = o.m.textures.map((x) => {
                    const xt = o.part.textures.get(x);
                    return `${thp8.hex(x)}${xt ? ` ${xt.width}x${xt.height}${xt.format === xenos.FMT_DXN ? " normal" : ""}` : " -"}`;
                });
                lines.push(`      THP8 lists: ${list.join(", ")}`);
            }
        });
    }
    return lines;
}

// --- command line -------------------------------------------------------------------------------------------------

const USAGE = `usage: render_audit.ts {textures,zfight,spot} [levels...] [--thp8 THP8] [--thug2 THUG2] [--staging DIR] [--top N]
                       [--csv FILE] [--tolerance IN] [--view M] [--near IN] [--min-area M2]

THP8's converted levels: z-fighting and missing textures traced to THP8's data (writes nothing).
  textures [levels...]               materials drawn without THP8's texture, by cause
  zfight [levels...]                 coplanar overlapping surfaces, by cause
  spot <level> <x,y,z> [radius]      the drawn meshes at a place and their textures
                                     (a negative x: write the place as "(x, y, z)")`;

export function main(argv: readonly string[] = process.argv.slice(2)): number {
    const { values, positionals } = parseCli(USAGE, {
        args: [...argv],
        allowPositionals: true,
        options: {
            thp8: { type: "string" },
            thug2: { type: "string" },
            staging: { type: "string" },
            top: { type: "string" },
            csv: { type: "string" },
            tolerance: { type: "string" },
            view: { type: "string" },
            near: { type: "string" },
            "min-area": { type: "string" },
        },
    });
    const [command, ...rest] = positionals;
    if (command !== "textures" && command !== "zfight" && command !== "spot") {
        process.stderr.write(`${USAGE}\nerror: the command is textures, zfight or spot\n`);
        return 2;
    }
    const compressed = compressedFolder(values.thp8 ?? "C:\\Games\\THP8");
    const staging = values.staging ?? path.join(resolveInstall(values.thug2 ?? "C:\\Games\\THUG2", "thug2"), STAGING);
    const names = thp8.loadNames(findCaseless(compressed, "PAK", "dbg.pak.xen")!);
    const templates = templateNames(compressed);
    const specs = allSpecs(compressed, names);
    const top = Number(values.top ?? (command === "zfight" ? 25 : 12));
    if (command === "spot") {
        const [levelArg, where, radius] = rest;
        const spec = levelArg ? findSpec(specs, levelArg) : undefined;
        const at = (where ?? "")
            .replace(/[()\s]/g, "")
            .split(",")
            .map(Number);
        if (!spec || at.length < 3 || at.some((x) => !Number.isFinite(x)))
            pyExit(`${USAGE}\nerror: spot <level> <x,y,z>`);
        const staged = readStaged(staging, spec.level);
        if (!staged) pyExit(`${spec.level}: not converted in ${staging}`);
        const parts = levelParts(spec, compressed, names);
        print(spotReport(spec.level, staged, parts, names, templates, at, Number(radius ?? 60)).join("\n"));
        return 0;
    }
    // (BEAVER VALLEY first, then the other network levels, then the standalone ones)
    const ordered = [
        ...specs.filter((s) => s.level.startsWith("thp8_net_")),
        ...specs.filter((s) => !s.level.startsWith("thp8_net_")),
    ];
    const wanted = rest.length
        ? rest.map(
              (n) => findSpec(specs, n) ?? pyExit(`${n}: not a THP8 level (${specs.map((s) => s.level).join(", ")})`),
          )
        : ordered;
    const csv: string[] = [zfight.CSV_HEADER];
    for (const spec of wanted) {
        const parts = levelParts(spec, compressed, names);
        const staged = readStaged(staging, spec.level);
        if (command === "textures") {
            print(textureReport(spec.level, auditMaterials(parts, templates), names, staged, top).join("\n"));
            if (staged) {
                print(
                    textureCheck
                        .report(
                            spec.level,
                            textureCheck.checkTextures(staged.scene, staged.textures),
                            top,
                            (c) => names.get(c) ?? thp8.hex(c),
                            `${spec.level} - as THUG2 draws it (its .scn.xbx against its .tex.xbx)`,
                        )
                        .join("\n"),
                );
            }
            continue;
        }
        if (!staged) {
            print(`${spec.level}: not converted in ${staging} - skipped`);
            continue;
        }
        const options = zfight.optionsFrom(values);
        // (objects in their own space - moving objects, props - stand at the origin in the scene file; the game puts
        // them at their nodes: left out)
        const local = new Set(parts.flatMap((p) => [...p.local]));
        const tris = zfight
            .sceneTriangles(staged.scene)
            .filter((t) => !local.has(staged.scene.sectors[t.sector]!.checksum));
        const fights = zfight.findZFights(staged.scene, options, tris);
        const origins = sectorOrigins(parts);
        const materials = materialOrigins(parts);
        const labels: zfight.Labels = {
            sector: (c) => {
                const o = origins.get(c);
                return `${names.get(c) ?? thp8.hex(c)}${o ? ` (${o.part.zone})` : ""}`;
            },
            material: (c) => {
                const o = materials.get(c);
                const m = staged.scene.materials.find((x) => x.checksum === c);
                return `${o ? (templates.get(o.m.template) ?? thp8.hex(o.m.template)) : thp8.hex(c)} tex ${thp8.hex(m?.passes[0]?.texture ?? 0)}`;
            },
            cause: (f, scene) => fightCause(f, scene, origins, materials, templates),
        };
        print(zfight.report(spec.level, staged.scene, fights, labels, top).join("\n"));
        csv.push(...zfight.csvRows(spec.level, staged.scene, fights, labels));
    }
    if (values.csv && command === "zfight") fs.writeFileSync(values.csv, `${csv.join("\n")}\n`);
    return 0;
}

if (isMain(import.meta.url)) await runMain(() => main());
