// NOT ORIGINAL - reads the game's [debug] collision_events lines (src/game/skater/collision_events.h) out of a
// thug2-decomp.log and groups them: the spots in a level where the skater's velocity turned round, with what did it.
//
//   node tools/pipeline/game/collision_events.ts <thug2-decomp.log> [--level <name>] [--col <level | file.col.xbx>]
//                                       [--root <game or staging folder>]... [--radius <inches>] [--json]
//
// Each line "collision-event: key=value ..." is one reversal: the level, the reason (the physics path that ran, or
// "slope" when none did - the ground's own gravity), the face involved and six probe lines. The events are grouped by
// level and spot (within --radius, default 80 inches - about 2 m) and printed as a table: level, spot (x,y,z), count,
// the reasons, the skater's state, the face (sector / object, face index), its flags, and what the probes say (the
// verdict below: invisible / trigger / unskatable faces, nothing under the next step, an open edge of the face's
// object - a dropped piece - and turns the player probably made himself). Then a line per event: probe_dir (the
// travel BEFORE the reversal - the probes and the screenshot face that way), the deciding ray, the face's nearest
// edge, the input held and the approach (from the collision-event-detail line with the same n=). The count includes
// the events the game's rate limit left out (repeats=).
//
// --col: also read that level's converted collision (.col.xbx - read only): the converters' staging folders
// (<root>\thug2-decomp-<game>\levels\<level>\) or a game folder's overlay (<root>[\Game]\thug2-decomp-data\levels\
// <level>\), the roots being --root folders or C:\Games\THUG2-1..4 and C:\Games\THUG2. For each spot: the collision
// object (by the sector's checksum: index, name, the source zone its name starts with), the face's flags in the file
// and whether its normal is the one the game saw, and - when the face isn't found - the faces nearest the spot. A
// warning when the file isn't the one the game loaded (col_file: its size and time).

import * as fs from "node:fs";
import * as path from "node:path";
import { parseArgs } from "node:util";
import { checksum } from "../level_port/qb.ts";
import * as col from "../level_port/thug2_col.ts";
import { helpFromHeader } from "../lib/cli.ts";
import { isMain, runMain } from "../lib/py.ts";

export type Vec3 = [number, number, number];

export interface FaceInfo {
    kind: "sector" | "object";
    name: string;
    face: number;
    flags: number;
    decoded: string;
    terrain: number;
    n: Vec3;
    at: Vec3;
}

export interface CollisionEvent {
    fields: Record<string, string>;
    n: number;
    level: string;
    frame: number;
    reason: string;
    paths: string[];
    spot: Vec3;
    face: FaceInfo | null;
    probes: Record<string, FaceInfo | null>;
    repeats: number; // events the game's rate limit left out at this spot before this one
    detail: Record<string, string> | null; // the collision-event-detail line with the same n
}

export const PROBES = [
    "probe_down_hit",
    "probe_down_hit_all",
    "probe_down_beyond",
    "probe_down_beyond_all",
    "probe_ahead",
    "probe_ahead_all",
] as const;

const TAG = "collision-event:";
const DETAIL_TAG = "collision-event-detail:";

function fieldsOf(text: string): Record<string, string> {
    const fields: Record<string, string> = {};
    for (const word of text.trim().split(/\s+/)) {
        const eq = word.indexOf("=");
        if (eq > 0) fields[word.slice(0, eq)] = word.slice(eq + 1);
    }
    return fields;
}

// A collision-event-detail line's fields (n=, state=, input=, ray=, corners=, open_edge=, col_file=, approach=,
// camera=...); null if it isn't one.
export function parseDetailLine(line: string): Record<string, string> | null {
    const at = line.indexOf(DETAIL_TAG);
    return at < 0 ? null : fieldsOf(line.slice(at + DETAIL_TAG.length));
}

function vec(text: string | undefined): Vec3 {
    const parts = (text ?? "").split(",").map(Number);
    return [parts[0] ?? 0, parts[1] ?? 0, parts[2] ?? 0];
}

// A face value: "none" or "sector:<name>;face:<i>;flags:0x<f>;decoded:<names>;terrain:<t>;n:<x,y,z>;at:<x,y,z>".
export function parseFace(text: string | undefined): FaceInfo | null {
    if (!text || text === "none") return null;
    const parts = new Map<string, string>();
    for (const part of text.split(";")) {
        const colon = part.indexOf(":");
        if (colon > 0) parts.set(part.slice(0, colon), part.slice(colon + 1));
    }
    const kind = parts.has("object") ? "object" : "sector";
    return {
        kind,
        name: parts.get(kind) ?? "",
        face: Number(parts.get("face") ?? -1),
        flags: Number.parseInt(parts.get("flags") ?? "0", 16),
        decoded: parts.get("decoded") ?? "",
        terrain: Number(parts.get("terrain") ?? 0),
        n: vec(parts.get("n")),
        at: vec(parts.get("at")),
    };
}

// One log line (the log's own prefix before the tag is skipped); null if it isn't an event.
export function parseEventLine(line: string): CollisionEvent | null {
    const at = line.indexOf(TAG);
    if (at < 0) return null;
    const fields = fieldsOf(line.slice(at + TAG.length));
    if (!("reason" in fields)) return null;
    const probes: Record<string, FaceInfo | null> = {};
    for (const p of PROBES) probes[p] = parseFace(fields[p]);
    return {
        fields,
        n: Number(fields.n ?? 0),
        level: fields.level ?? "-",
        frame: Number(fields.frame ?? 0),
        reason: fields.reason ?? "",
        paths: fields.paths && fields.paths !== "-" ? fields.paths.split(",") : [],
        spot: vec(fields.spot),
        face: parseFace(fields.face),
        probes,
        repeats: Number(fields.repeats ?? 0),
        detail: null,
    };
}

export function readEvents(text: string): CollisionEvent[] {
    const out: CollisionEvent[] = [];
    for (const line of text.split(/\r?\n/)) {
        const detail = parseDetailLine(line);
        if (detail) {
            const last = out[out.length - 1];
            if (last && String(last.n) === detail.n) last.detail = detail;
            continue;
        }
        const e = parseEventLine(line);
        if (e) out.push(e);
    }
    return out;
}

const FACE_NON_COLLIDABLE = col.FACE_NON_COLLIDABLE;

// What the probes say about the spot, in a few words.
export function verdict(e: CollisionEvent): string {
    const say: string[] = [];
    const f = e.face;
    if (f) {
        if (f.flags & col.FACE_INVISIBLE) say.push("invisible face");
        if (f.flags & col.FACE_TRIGGER) say.push("trigger face");
        if (f.flags & col.FACE_NOT_SKATABLE) say.push("face flagged not skatable");
        if (e.reason === "slope" && f.n[1] < 0.95) say.push(`ground at ${slopeOf(f.n).toFixed(0)} deg`);
        else if (e.reason.startsWith("wall") && f.n[1] > 0.5) say.push("a wall that faces up (ramp-like)");
    } else if (e.reason === "uber-frig") {
        say.push("no collision under the skater's next step (uber frig)");
    } else if (e.reason !== "slope") {
        say.push("no face recorded");
    }
    const probe = (skater: FaceInfo | null, all: FaceInfo | null, what: string): void => {
        if (!skater && !all) say.push(`nothing ${what}`);
        else if (!skater && all && all.flags & FACE_NON_COLLIDABLE) say.push(`only non-collidable ${what}`);
    };
    probe(e.probes.probe_down_hit ?? null, e.probes.probe_down_hit_all ?? null, "under the hit");
    probe(e.probes.probe_down_beyond ?? null, e.probes.probe_down_beyond_all ?? null, "beyond it");
    if (e.reason !== "uber-frig" && (e.fields.flipped ?? "").startsWith("update-rest>update-start"))
        say.push("turned round between frames, outside the core physics");
    const ahead = e.probes.probe_ahead ?? null;
    if (ahead && ahead.flags & col.FACE_INVISIBLE) say.push("invisible face ahead");
    if (e.detail?.open_edge === "1")
        say.push(`an open edge ${e.detail.edge_dist ?? "?"} from the spot (a dropped piece?)`);
    const player = playerTurn(e);
    if (player) say.push(player);
    return say.length ? say.join(", ") : "-";
}

// Whether the player made the turn, not the collision: the game logs "slope" only on ground steeper than 5 degrees
// (older logs: on any ground); with turning / braking held, a trick or a balance trick in the window it may still be
// the player's own.
export function playerTurn(e: CollisionEvent): string | null {
    if (e.reason !== "slope") return null;
    if (Number(e.fields.ground_min_y ?? 0) >= 0.996) return "flat ground: the player's own turn";
    const d = e.detail;
    if (!d) return null;
    const held = (d.input_window ?? "-").split("+").filter((b) => ["left", "right", "down"].includes(b));
    if (held.length) return `player turn? (${held.join("+")} held)`;
    if (d.trick === "1") return "player turn? (trick)";
    if ((d.balance ?? "-") !== "-") return `player turn? (${d.balance})`;
    return null;
}

export function stateOf(e: CollisionEvent): string {
    if (!e.detail) return "-";
    const balance = e.detail.balance ?? "-";
    return `${e.detail.state ?? "-"}${balance !== "-" ? `/${balance}` : ""}`;
}

export function slopeOf(n: Vec3): number {
    return (Math.acos(Math.max(-1, Math.min(1, n[1]))) * 180) / Math.PI;
}

export interface Spot {
    level: string;
    at: Vec3;
    events: CollisionEvent[];
}

// Grouped by level, then by spot: each event joins the first spot of its level within `radius` of it.
export function groupSpots(events: readonly CollisionEvent[], radius = 80): Spot[] {
    const spots: Spot[] = [];
    for (const e of events) {
        const near = spots.find((s) => s.level === e.level && Math.hypot(...sub(s.at, e.spot)) <= radius);
        if (near) near.events.push(e);
        else spots.push({ level: e.level, at: e.spot, events: [e] });
    }
    return spots.sort((a, b) => a.level.localeCompare(b.level) || b.events.length - a.events.length);
}

const sub = (a: Vec3, b: Vec3): Vec3 => [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
const fmt = (v: Vec3, digits = 0): string => v.map((x) => x.toFixed(digits)).join(",");

function count(values: readonly string[]): string {
    const counts = new Map<string, number>();
    for (const v of values) counts.set(v, (counts.get(v) ?? 0) + 1);
    return [...counts]
        .sort((a, b) => b[1] - a[1])
        .map(([v, n]) => (n > 1 ? `${v} x${n}` : v))
        .join(", ");
}

export function spotRows(spots: readonly Spot[]): string[][] {
    return spots.map((s) => {
        const first = s.events.find((e) => e.face) ?? s.events[0]!;
        const f = first.face;
        return [
            s.level,
            fmt(s.at),
            String(s.events.reduce((n, e) => n + 1 + e.repeats, 0)),
            count(s.events.map((e) => e.reason)),
            count(s.events.map(stateOf)),
            f ? `${f.kind === "object" ? "object " : ""}${f.name} #${f.face}` : "-",
            f ? `0x${f.flags.toString(16).padStart(4, "0")} ${f.decoded}` : "-",
            count(s.events.map(verdict)),
        ];
    });
}

export function table(rows: readonly string[][], head: readonly string[]): string[] {
    const widths = head.map((h, i) => Math.max(h.length, ...rows.map((r) => (r[i] ?? "").length)));
    const line = (r: readonly string[]): string =>
        r
            .map((c, i) => (i === r.length - 1 ? c : c.padEnd(widths[i]!)))
            .join("  ")
            .trimEnd();
    return [line(head), line(widths.map((w) => "-".repeat(w))), ...rows.map(line)];
}

// --- the level's collision ---

const DEFAULT_ROOTS = [
    "C:\\Games\\THUG2-1",
    "C:\\Games\\THUG2-2",
    "C:\\Games\\THUG2-3",
    "C:\\Games\\THUG2-4",
    "C:\\Games\\THUG2",
];

export function findCol(level: string, roots: readonly string[]): string | null {
    if (level.toLowerCase().endsWith(".col.xbx")) return fs.existsSync(level) ? level : null;
    const file = `${level}.col.xbx`;
    for (const root of roots) {
        for (const base of [root, path.join(root, "Game")]) {
            if (!fs.existsSync(base)) continue;
            const dirs = fs
                .readdirSync(base, { withFileTypes: true })
                .filter((d) => d.isDirectory() && d.name.startsWith("thug2-decomp-"))
                .map((d) => d.name)
                .sort((a, b) => Number(a === "thug2-decomp-data") - Number(b === "thug2-decomp-data")); // staging first
            for (const dir of dirs) {
                const candidate = path.join(base, dir, "levels", level, file);
                if (fs.existsSync(candidate)) return candidate;
            }
        }
    }
    return null;
}

const ZONE = /^z_([a-z0-9]+)_/i;

export function zoneOf(name: string): string {
    return ZONE.exec(name)?.[1]?.toLowerCase() ?? "-";
}

function nameChecksum(name: string): number {
    return /^0x[0-9a-f]{8}$/i.test(name) ? Number.parseInt(name.slice(2), 16) : checksum(name);
}

// Names the converters give collision objects that no node names (the game can't name them: it logs 0x<checksum>):
// THP8's gap fills, <level>_GapFill_<n> (tools/pipeline/thp8_levels GAP_FILL), and its bodies no node or volume owns,
// <level>_Col_<n> (convertCollision: n counts every body).
export function converterNames(level: string, most = 20000): Map<number, string> {
    const names = new Map<number, string>();
    for (const kind of ["GapFill", "Col"])
        for (let n = 0; n < most; n++) {
            const name = `${level}_${kind}_${n}`;
            names.set(checksum(name) >>> 0, name);
        }
    return names;
}

export function describeFace(c: col.ColFile, e: CollisionEvent, names = new Map<number, string>()): string[] {
    const out: string[] = [];
    const f = e.face;
    if (f && f.kind === "sector") {
        const want = nameChecksum(f.name) >>> 0;
        const index = c.objects.findIndex((o) => o.checksum >>> 0 === want);
        if (index >= 0) {
            const o = c.objects[index]!;
            const face = o.faces[f.face];
            const name = names.get(want) ?? f.name;
            out.push(`col object #${index} ${name} (zone ${zoneOf(name)}), ${o.faces.length} faces`);
            if (face) {
                const [flags, terrain, a, b, cc] = face;
                const p = [o.verts[a]!, o.verts[b]!, o.verts[cc]!] as [Vec3, Vec3, Vec3];
                const n = unit(cross(sub(p[1], p[0]), sub(p[2], p[0])));
                const same = n[0] * f.n[0] + n[1] * f.n[1] + n[2] * f.n[2] > 0.99;
                out.push(
                    `face #${f.face}: file flags 0x${flags.toString(16).padStart(4, "0")} terrain ${terrain}, normal ${fmt(n, 2)}${same ? " (as seen)" : ` (the game saw ${fmt(f.n, 2)})`}, corners ${p.map((q) => fmt(q)).join(" / ")}`,
                );
            } else {
                out.push(`face #${f.face}: not in the file's object`);
            }
            return out;
        }
        out.push(`no col object named ${f.name} (a movable's, or another file's)`);
    }
    // The nearest faces.
    const near: [number, string][] = [];
    c.objects.forEach((o, oi) => {
        o.faces.forEach(([flags, , a, b, cc], fi) => {
            const p = [o.verts[a]!, o.verts[b]!, o.verts[cc]!] as Vec3[];
            const centre: Vec3 = [0, 1, 2].map((k) => (p[0]![k]! + p[1]![k]! + p[2]![k]!) / 3) as Vec3;
            const d = Math.hypot(...sub(centre, e.spot));
            if (d < 120) {
                const n = unit(cross(sub(p[1]!, p[0]!), sub(p[2]!, p[0]!)));
                near.push([
                    d,
                    `  obj #${oi} ${names.get(o.checksum >>> 0) ?? `0x${(o.checksum >>> 0).toString(16).padStart(8, "0")}`} face ${fi} at ${d.toFixed(0)}: flags 0x${flags.toString(16).padStart(4, "0")} slope ${slopeOf(n).toFixed(0)}`,
                ]);
            }
        });
    });
    near.sort((x, y) => x[0] - y[0]);
    out.push(`faces nearest the spot: ${near.length ? "" : "none within 120"}`);
    for (const [, text] of near.slice(0, 6)) out.push(text);
    return out;
}

const cross = (a: Vec3, b: Vec3): Vec3 => [
    a[1] * b[2] - a[2] * b[1],
    a[2] * b[0] - a[0] * b[2],
    a[0] * b[1] - a[1] * b[0],
];
const unit = (a: Vec3): Vec3 => {
    const l = Math.hypot(...a);
    return l ? [a[0] / l, a[1] / l, a[2] / l] : [0, 0, 0];
};

// A spot's events, one line each: the probes' direction (the travel before the reversal: the probes and the
// screenshot face that way), the deciding ray, the face's edge, the input and the approach.
export function spotDetails(s: Spot): string[] {
    return s.events.map((e) => {
        const d = e.detail;
        const parts = [`n=${e.n} ${e.reason}`, `probe_dir ${e.fields.probe_dir ?? "-"}`];
        if (e.repeats) parts.push(`+${e.repeats} left out before it`);
        if (d) {
            parts.push(
                `${stateOf(e)} speed ${d.speed ?? "-"}`,
                `input ${d.input ?? "-"} (window ${d.input_window ?? "-"})`,
            );
            parts.push(`ray ${d.ray ?? "-"}`);
            if ((d.edge ?? "-") !== "-") parts.push(`edge ${d.edge} at ${d.edge_dist} open ${d.open_edge}`);
            parts.push(`approach ${d.approach ?? "-"}`);
        }
        return parts.join("; ");
    });
}

// A file's size and time as the game writes them ("<size>@YYYY-MM-DDTHH:MM:SS", local time).
export function fileStamp(file: string): string {
    const st = fs.statSync(file);
    const t = st.mtime;
    const p = (n: number): string => String(n).padStart(2, "0");
    return `${st.size}@${t.getFullYear()}-${p(t.getMonth() + 1)}-${p(t.getDate())}T${p(t.getHours())}:${p(t.getMinutes())}:${p(t.getSeconds())}`;
}

// The events whose level's collision file (col_file, as the game loaded it) isn't the file being compared.
export function fileMismatches(events: readonly CollisionEvent[], stamp: string, level: string): string[] {
    const seen = new Set<string>();
    for (const e of events)
        if (e.level === level && e.detail?.col_file && e.detail.col_file !== "-" && e.detail.col_file !== stamp)
            seen.add(e.detail.col_file);
    return [...seen].map(
        (s) => `the game loaded ${level}'s collision as ${s}, this file is ${stamp}: converted again since?`,
    );
}

function main(argv: string[]): number {
    const { values, positionals } = parseArgs({
        args: argv,
        allowPositionals: true,
        options: {
            level: { type: "string" },
            col: { type: "string" },
            root: { type: "string", multiple: true },
            radius: { type: "string" },
            json: { type: "boolean" },
        },
    });
    const logPath = positionals[0];
    if (!logPath) {
        console.error(
            "usage: collision_events.ts <thug2-decomp.log> [--level <name>] [--col <level | file.col.xbx>] [--root <folder>]... [--radius <inches>] [--json]",
        );
        return 2;
    }
    let events = readEvents(fs.readFileSync(logPath, "utf-8"));
    if (values.level) events = events.filter((e) => e.level === values.level);
    const spots = groupSpots(events, Number(values.radius ?? 80));
    if (values.json) {
        console.log(JSON.stringify(spots, null, 1));
        return 0;
    }
    console.log(`${events.length} collision events, ${spots.length} spots`);
    const head = ["level", "spot", "n", "reason", "state", "face", "flags", "probes"];
    for (const line of table(spotRows(spots), head)) console.log(line);
    for (const s of spots) {
        console.log(`\n${s.level} at ${fmt(s.at)}:`);
        for (const line of spotDetails(s)) console.log(`  ${line}`);
    }
    if (values.col) {
        const level = values.col;
        const file = findCol(level, values.root ?? DEFAULT_ROOTS);
        if (!file) {
            console.error(`no ${level}.col.xbx found`);
            return 1;
        }
        console.log(`\ncollision: ${file} (${fileStamp(file)})`);
        for (const warning of fileMismatches(events, fileStamp(file), path.basename(file).replace(/\.col\.xbx$/i, "")))
            console.log(`WARNING: ${warning}`);
        const c = col.readCol(fs.readFileSync(file), file);
        const name = path.basename(file).replace(/\.col\.xbx$/i, "");
        const names = converterNames(name);
        for (const s of spots.filter((x) => x.level === name || values.col!.toLowerCase().endsWith(".col.xbx"))) {
            console.log(`\n${s.level} at ${fmt(s.at)} (${s.events.length}):`);
            const first = s.events.find((e) => e.face) ?? s.events[0]!;
            for (const line of describeFace(c, first, names)) console.log(`  ${line}`);
        }
    }
    return 0;
}

if (isMain(import.meta.url)) {
    helpFromHeader(import.meta.url);
    await runMain(() => main(process.argv.slice(2)));
}
