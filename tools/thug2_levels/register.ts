// NOT ORIGINAL - the register entries for THUGPRO's changes to THUG2's own levels (TP-42, tools/compat/thugpro/README.md;
// data: tools/compat/thugpro/thug2_levels.json, written by `node tools/thug2_levels/thugpro_compare.ts
// --extract-thugpro`, never by hand except each entry's `enabled`). A difference found by diff.ts becomes part of one
// entry: entries group the differences of one level by what they are (kind) and what they touch (what), each with its
// class (parity / visible / single_player / cosmetic / none), a description, the nodes / objects concerned and the
// data to apply it (`edits` in tools/thug1_levels/hand_edits.ts's form, THUGPRO's scripts by reference). Every entry is
// off (`enabled` false) until the user switches it on; re-extracting keeps each entry's `enabled` (matched by level,
// kind, what and nodes).

import * as fs from "node:fs";
import { fileURLToPath } from "node:url";
import { crc32 } from "node:zlib";
import * as compat from "../compat/compat.ts";
import * as qb from "../level_port/qb.ts";
import { Struct } from "../level_port/qb.ts";
import { nodeArray } from "../thaw_levels/thugpro_compare.ts";
import {
    type ColChange,
    type Context,
    type Entry,
    type GeometryRef,
    KLASSES,
    type Klass,
    type LevelReport,
    mergeEdits,
    type ScriptRef,
    scriptClosure,
    scriptTraits,
    whatNode,
} from "./diff.ts";
import { usedNames } from "./sources.ts";

export const REGISTER_FILE = fileURLToPath(new URL("../compat/thugpro/thug2_levels.json", import.meta.url));
export const REGISTER_ID = "TP-42";

export interface LevelRegister {
    sources: Record<string, string>;
    entries: Entry[];
}

export interface Register {
    _comment: string;
    _register: string;
    _source: string;
    levels: Record<string, LevelRegister>;
}

// The register as the compatibility profile has it (tools/compat/compat.ts: `none` has no entries), or null.
export function loadRegister(profile: compat.ProfileName = "thugpro"): Register | null {
    const data = compat.load(profile).file("thug2_levels");
    return "levels" in data ? (data as unknown as Register) : null;
}

// The register file as it is (for --extract-thugpro, which keeps each entry's `enabled`), or null.
export function readRegisterFile(file = REGISTER_FILE): Register | null {
    if (!fs.existsSync(file)) return null;
    return JSON.parse(fs.readFileSync(file, "utf8")) as Register;
}

const KIND_ORDER = [
    "level_structure",
    "level_option",
    "trigger_scripts",
    "killed",
    "scripts_changed",
    "nodes_added",
    "geometry_added",
    "nodes_removed",
    "net_presence",
    "created_at_start",
    "node_values",
    "collision_faces_removed",
    "collision_face_flags",
    "collision_terrain",
    "collision_reshaped",
    "collision_faces_added",
    "collision_objects_removed",
    "scene_changed",
    "banners",
    "level_changes_other",
];

const list = (names: readonly string[], n = 8): string =>
    names.length <= n ? names.join(", ") : `${names.slice(0, n).join(", ")} (+${names.length - n} more)`;

// Whether a set of scripts touches what players stand on: a name they use is a node of the level that is a spawn,
// rail, kill zone or piece of collision.
function touchesParity(roots: readonly number[], ctx: Context, nodes: ReadonlyMap<number, Struct>, deep = 3): string[] {
    const hits = new Set<string>();
    const seen = new Set<number>();
    const walk = (s: number, d: number): void => {
        if (seen.has(s) || d > deep) return;
        seen.add(s);
        const node = nodes.get(s);
        if (node !== undefined) {
            const w = whatNode(node, ctx);
            if (["spawns", "koth", "ctf", "rails", "collision", "kill_zones"].includes(w)) hits.add(w);
            return;
        }
        const item = ctx.scripts.get(s);
        if (!item) return;
        for (const [, t, p] of qb.tokens(Buffer.concat([item.bytes.subarray(6), Buffer.from([0])]), true)) {
            if (t === 0x00) break;
            if (t === 0x16) walk(p.readUInt32LE(0), d + 1);
        }
    };
    for (const r of roots) walk(r, 0);
    return [...hits].sort();
}

// The entries of one level.
export function levelEntries(r: LevelReport, thug2Nodes: Buffer): Entry[] {
    const L = r.code;
    const ctx = r.ctx;
    const out: Omit<Entry, "id">[] = [];
    const [array] = nodeArray(thug2Nodes, `${L}.qb`);
    const nodes = new Map<number, Struct>();
    for (const n of array) {
        const k = n instanceof Struct ? n.get("Name") : undefined;
        if (n instanceof Struct && k instanceof qb.Name) nodes.set(k.value, n);
    }
    const has = (name: string): boolean => nodes.has(qb.checksum(name));
    const ck = (s: string): number => qb.checksum(s);
    const base = (kind: string, what: string, klass: Klass, applicable: boolean, description: string) => ({
        level: L,
        kind,
        what,
        class: klass,
        enabled: false,
        applicable,
        description,
    });

    // --- the level structure and THUGPRO's host options
    for (const s of r.structure) {
        if (s.key.toLowerCase() === "setup_script") {
            const root = ck(s.theirs);
            const scripts = scriptClosure([root], ctx, r.tpScripts);
            const touched = touchesParity([root], ctx, nodes);
            out.push({
                ...base(
                    "level_structure",
                    touched.length ? touched.join("+") : "level_setup",
                    touched.length ? "parity" : "cosmetic",
                    scripts.length > 0 || ctx.thug2Scripts.has(root),
                    `Level_${L}'s setup_script ${s.ours} -> THUGPRO's ${s.theirs}` +
                        (touched.length ? ` (it creates / kills ${touched.join(", ")} nodes)` : ""),
                ),
                scripts,
                level_setup: { setup_script: s.theirs },
            });
        }
    }
    const settings = r.structure.filter((s) => s.key.toLowerCase() !== "setup_script");
    if (settings.length) {
        out.push({
            ...base(
                "level_structure",
                "level_settings",
                "cosmetic",
                false,
                `Level_${L}'s other settings as THUGPRO's Levels.qb has them (loading screen, music, menus, lights, ` +
                    `THUGPRO's own flags): ${settings.map((s) => `${s.key}: ${s.ours.slice(0, 60)} -> ${s.theirs.slice(0, 60)}`).join("; ")}`,
            ),
        });
    }
    for (const o of r.options) {
        const root = ck(o.script);
        const scripts = scriptClosure([root], ctx, r.tpScripts);
        const touched = touchesParity([root], ctx, nodes);
        out.push({
            ...base(
                "level_option",
                touched.length ? touched.join("+") : "level_option",
                touched.length ? "parity" : "cosmetic",
                scripts.length > 0,
                `THUGPRO's host option "${o.desc}" (${o.flag}${o.flags.length ? `, ${o.flags.join(", ")}` : ""}): ` +
                    `${o.script} ${o.dflt} after loading, THUGPRO's state unless the host switches it` +
                    (touched.length ? ` (creates / kills ${touched.join(", ")} nodes)` : ""),
            ),
            scripts,
            level_setup: { options: [{ script: o.script, params: [o.dflt] }] },
            note: "applied as the state after loading only; a host switching it needs THUGPRO's messages 155-159",
        });
    }

    // --- <L>_LevelChanges
    const replaced = r.levelChanges.filter((c) => c.action === "replace_script" || c.action === "remove_script");
    const byWhat = new Map<string, typeof replaced>();
    for (const c of replaced) {
        const k =
            c.script !== undefined && scriptTraits(ctx, ck(c.script)).kill
                ? "kill_zones"
                : c.script !== undefined && scriptTraits(ctx, ck(c.script)).gap
                  ? "gaps"
                  : "triggers";
        byWhat.set(k, [...(byWhat.get(k) ?? []), c]);
    }
    for (const [what, cs] of byWhat) {
        const missing = cs.flatMap((c) => c.nodes).filter((n) => !has(n));
        const scripts = scriptClosure(
            cs.flatMap((c) => (c.script ? [ck(c.script)] : [])),
            ctx,
            r.tpScripts,
        );
        out.push({
            ...base(
                "trigger_scripts",
                what,
                what === "kill_zones" ? "parity" : what === "gaps" ? "visible" : "cosmetic",
                missing.length === 0,
                `${L}_LevelChanges: trigger scripts of ${cs.length} nodes replaced by THUGPRO's` +
                    (what === "kill_zones" ? " - touching them kills the skater (sk3_killskater)" : "") +
                    `: ${list(
                        cs.map((c) => `${c.nodes.join("/")} -> ${c.script ?? "(none)"}`),
                        6,
                    )}` +
                    (missing.length ? `; not in THUG2's array: ${list(missing)}` : ""),
            ),
            nodes: cs.flatMap((c) => c.nodes),
            edits: mergeEdits(
                cs.flatMap((c) => c.nodes.map((n) => ({ node_values: { [n]: { TriggerScript: c.script ?? null } } }))),
            ),
            scripts,
        });
    }
    const kills = r.levelChanges.filter((c) => c.action === "kill");
    if (kills.length) {
        const killed = kills.flatMap((c) => c.nodes);
        const whats = [...new Set(killed.map((n) => (has(n) ? whatNode(nodes.get(ck(n))!, ctx) : "other")))];
        const klass = whats.some((w) => ["rails", "collision", "kill_zones", "spawns"].includes(w))
            ? "parity"
            : "cosmetic";
        out.push({
            ...base(
                "killed",
                whats.join("+"),
                klass,
                killed.every(has),
                `${L}_LevelChanges: THUGPRO kills ${killed.length} nodes after loading (${whats.join(", ")}): ${list(killed)} - applied as CreatedAtStart taken off`,
            ),
            nodes: killed,
            edits: { created_at_start: { off: killed } },
        });
    }
    const models = r.levelChanges.filter((c) => c.action === "model" || c.action === "static");
    if (models.length) {
        out.push({
            ...base(
                "banners",
                "scenery",
                "cosmetic",
                false,
                `${L}_LevelChanges: ${models.length} banner / billboard / poster models of THUGPRO's (THUGPRO's own textures and models, no collision): ${list(
                    models.map((m) => m.nodes[0] ?? m.detail),
                    4,
                )}`,
            ),
        });
    }
    const other = r.levelChanges.filter(
        (c) => !["replace_script", "remove_script", "kill", "model", "static"].includes(c.action),
    );
    for (const c of other) {
        out.push({
            ...base(
                "level_changes_other",
                c.action,
                "cosmetic",
                false,
                `${L}_LevelChanges: ${c.action} ${c.nodes.join(", ")}: ${c.detail}`,
            ),
        });
    }

    // --- a replaced level scripts file
    if (r.scripts.length) {
        const groups = new Map<string, typeof r.scripts>();
        for (const s of r.scripts) {
            // (a host option's toggle script is its level_option entry's)
            if (r.options.some((o) => o.script.toLowerCase() === s.name.toLowerCase())) continue;
            const k = s.how === "removed" ? "removed" : s.kill ? "kill_zones" : s.gap ? "gaps" : "scripts";
            groups.set(k, [...(groups.get(k) ?? []), s]);
        }
        for (const [what, ss] of groups) {
            const roots = ss.filter((s) => s.how !== "removed").map((s) => ck(s.name));
            const touched = touchesParity(roots, ctx, nodes, 2);
            const klass: Klass =
                what === "kill_zones" ? "parity" : what === "gaps" ? "visible" : touched.length ? "parity" : "cosmetic";
            const replace: ScriptRef[] = roots.map((k) => {
                const item = r.tpScripts.get(k)!;
                return { name: ss.find((s) => ck(s.name) === k)!.name, from: item.from, crc: crcOf(item.bytes) };
            });
            out.push({
                ...base(
                    "scripts_changed",
                    what === "scripts" && touched.length ? touched.join("+") : what,
                    klass,
                    what !== "removed",
                    `${L}'s level scripts as THUGPRO's files have them (${[r.sources.nodes, r.sources.scripts].filter(Boolean).join(", ")}): ${ss.length} ${what === "removed" ? "scripts THUGPRO's file hasn't (left as THUG2's)" : "scripts changed / added"}: ${list(ss.map((s) => `${s.name}${s.how === "added" ? " (new)" : ""}`))}` +
                        (touched.length ? ` - they create / kill ${touched.join(", ")} nodes` : ""),
                ),
                ...(what === "removed"
                    ? {}
                    : {
                          replace_scripts: replace,
                          scripts: scriptClosure(roots, ctx, r.tpScripts).filter(
                              (s) => !replace.some((x) => x.name === s.name),
                          ),
                      }),
            });
        }
    }

    // --- node arrays
    const geometryNodes = new Set<string>();
    if (r.collision) {
        for (const c of r.collision.changes) if (c.kind === "object_added") geometryNodes.add(c.object.toLowerCase());
    }
    if (r.nodes) {
        const groups = new Map<string, typeof r.nodes.changes>();
        for (const c of r.nodes.changes) {
            if (c.kind === "nodes_added" && geometryNodes.has(c.node.toLowerCase())) continue; // (with its geometry below)
            const kind =
                c.kind === "net_absent" || c.kind === "net_present"
                    ? "net_presence"
                    : c.kind === "created_on" || c.kind === "created_off"
                      ? "created_at_start"
                      : c.kind;
            const key = `${kind}|${c.what}|${c.klass}`;
            groups.set(key, [...(groups.get(key) ?? []), c]);
        }
        for (const [key, cs] of groups) {
            const [kind, what, klass] = key.split("|") as [string, string, Klass];
            const nodesOf = [...new Set(cs.map((c) => c.node))];
            const details = [...new Set(cs.map((c) => c.detail.replace(/: .*/, "")))];
            const sample = cs.slice(0, 4).map((c) => `${c.node} (${c.detail})`);
            out.push({
                ...base(
                    kind,
                    what,
                    klass,
                    kind !== "nodes_removed",
                    `${L}'s node array as THUGPRO's: ${nodesOf.length} ${what} nodes, ${details.slice(0, 4).join(" / ")}${details.length > 4 ? " ..." : ""}: ${list(sample, 4)}${cs.length > 4 ? ` (+${cs.length - 4} more)` : ""}`,
                ),
                nodes: nodesOf,
                edits: mergeEdits(cs.map((c) => c.edit)),
            });
        }
    }

    // --- collision and scene
    if (r.collision) {
        const added = r.collision.changes.filter((c) => c.kind === "object_added");
        for (const c of added) {
            const sector = r.scene?.added.find((s) => s.checksum === ck(c.object));
            const node = r.nodes?.changes.find(
                (n) => n.kind === "nodes_added" && n.node.toLowerCase() === c.object.toLowerCase(),
            );
            const geo: GeometryRef = { name: c.object };
            // (a node not created at start is there only when a script creates it - a host option)
            const flags = node?.edit.rails?.added?.[0]?.flags;
            const atStart = Array.isArray(flags) && flags.some((f) => String(f).toLowerCase() === "createdatstart");
            const creators = [...r.tpScripts.values()]
                .filter((s) => usedNames(s.bytes).has(ck(c.object)))
                .map((s) => ctx.names.get(s.name) ?? String(s.name));
            out.push({
                ...base(
                    "geometry_added",
                    "collision",
                    atStart || !node ? "parity" : "none",
                    true,
                    `${c.object}: a piece THUGPRO added to ${L} (collision ${c.detail}${sector ? ", its scene sector" : ""}${node ? ", its node" : ""}) - THUGPRO's own geometry, by reference from its install` +
                        (atStart || !node
                            ? ""
                            : `; not created at start - only ${creators.join(", ") || "a script"} makes it (a host option, off after loading): the same as THUG2's until a host switches it on`),
                ),
                nodes: [c.object],
                ...(node ? { edits: mergeEdits([node.edit]) } : {}),
                geometry: [geo],
            });
        }
        const kinds: [string, string, (c: ColChange) => Klass][] = [
            ["faces_removed", "collision_faces_removed", (c) => (c.solid ? "parity" : "visible")],
            [
                "face_flags",
                "collision_face_flags",
                (c) => (/skatable|wallride|vert|non-collidable|trigger/.test(c.detail) ? "parity" : "cosmetic"),
            ],
            ["terrain", "collision_terrain", () => "visible"],
            ["reshaped", "collision_reshaped", () => "parity"],
            ["faces_added", "collision_faces_added", () => "parity"],
            ["object_removed", "collision_objects_removed", (c) => (c.count ? "parity" : "none")],
        ];
        for (const [k, kind, klassOf] of kinds) {
            const cs = r.collision.changes.filter((c) => c.kind === k);
            const byKlass = new Map<Klass, typeof cs>();
            for (const c of cs) byKlass.set(klassOf(c), [...(byKlass.get(klassOf(c)) ?? []), c]);
            for (const [klass, group] of byKlass) {
                const total = group.reduce((s, c) => s + c.count, 0);
                const applicable = group.every((c) => c.edit !== undefined);
                out.push({
                    ...base(
                        kind,
                        "collision",
                        klass,
                        applicable && klass !== "none",
                        `${L}'s collision as THUGPRO's (${r.sources.collision}): ${group.length} objects, ${total} ${k === "object_removed" ? "faces" : k === "reshaped" ? "vertices" : "faces"} - ${list(
                            group.map((c) => `${c.object} (${c.detail})`),
                            5,
                        )}` + (klass === "none" ? " - empty objects: no effect" : ""),
                    ),
                    nodes: group.map((c) => c.object),
                    ...(applicable ? { edits: mergeEdits(group.map((c) => c.edit!)) } : {}),
                });
            }
        }
    }
    if (r.scene?.changed.length) {
        out.push({
            ...base(
                "scene_changed",
                "scenery",
                "cosmetic",
                false,
                `${L}'s scene as THUGPRO's (${r.sources.scene}): ${r.scene.changed.length} sectors drawn otherwise (materials / vertex colours; same places)${r.scene.removed.length ? `, ${r.scene.removed.length} taken out` : ""}`,
            ),
        });
    }
    out.sort(
        (a, b) =>
            KIND_ORDER.indexOf(a.kind) - KIND_ORDER.indexOf(b.kind) ||
            KLASSES.indexOf(a.class) - KLASSES.indexOf(b.class),
    );
    return out.map((e, i) => ({ id: `T2-${L}-${i + 1}`, ...e }));
}

function crcOf(b: Buffer): number {
    return crc32(b) >>> 0;
}

// Keep each entry's `enabled` across a re-extraction (the same level, kind, what and first node).
export function carryEnabled(fresh: Entry[], old: readonly Entry[] | undefined): void {
    if (!old) return;
    const key = (e: Entry): string => `${e.kind}|${e.what}|${e.class}|${e.nodes?.[0] ?? e.description.slice(0, 60)}`;
    const on = new Set(old.filter((e) => e.enabled).map(key));
    for (const e of fresh) if (on.has(key(e))) e.enabled = true;
}
