// NOT ORIGINAL - the level editor's saved layouts (thug2-decomp-layouts\<scene>.txt) read as the DLL reads them
// (src/game/front/level_editor_layout.h has the format; level_editor_fields.h the `set` record's f.<field>= values,
// level_editor_level_props.h the `level` record, level_editor_chains.h the phase 4 records, level_editor_walls.h the
// `wall` record). Pure: tools/game/apply_layout.ts bakes a layout into a level's files with it.
//
// A record the DLL would refuse (malformed) is an error here too, with its line; a record of a kind this reader
// doesn't know (a later version's) is kept in `unknown` - the bake warns about each instead of skipping it silently.
// Phase 5's `trigger` records are read by tools/game/triggers.ts (the DLL's Triggers::ReadRecord, ported).

import { checksum } from "../level_port/qb.ts";
import { readTriggerRecord, type TriggerSpec } from "./triggers.ts";

export interface V3 {
    x: number;
    y: number;
    z: number;
}
// A point x as loaded is at QuarterTurn(x * scale, quarters) + offset (level_editor.h's Placement).
export interface Placement {
    quarters: number;
    offset: V3;
    scale: number;
}
export type FieldKind =
    | "integer"
    | "float"
    | "name"
    | "string"
    | "local"
    | "vector"
    | "pair"
    | "flag"
    | "names"
    | "integers"
    | "floats";
export interface FieldValue {
    kind: FieldKind;
    integer?: number;
    numbers?: number[]; // float: [x]; pair: [x, y]; vector: [x, y, z]; floats: the list
    checksum?: number; // name
    names?: number[]; // names (checksums); integers: the ints
    text?: string; // string, local
    nameTexts?: (string | null)[]; // the names as written (name / names: the identifier, null for 0x hex)
    on?: boolean; // flag
}
export interface FieldEdit {
    key: number;
    keyText: string | null; // the field's name as written (null: 0x hex)
    removed: boolean;
    value?: FieldValue;
}
export interface SetRecord {
    name: number;
    pos?: V3;
    angles?: V3;
    fields: FieldEdit[];
}
export interface NodeRecord {
    name: string;
    cls: string;
    type: string;
    pos: V3;
    angles: V3;
    types: string[];
    colour: [number, number, number];
    brightness: number;
    inner: number;
    outer: number;
    radius: number;
}
export interface PointRecord {
    name: string;
    cls: string;
    pos: V3;
    like: number;
    terrain: string;
    links: number[];
    // (phase 4b - level_editor_chains.h) a ladder's heading; a rail's trick-object cluster (0: none), flat-ground grinding
    facing: number;
    cluster: number;
    grindFlat: boolean;
}
export interface LevelProps {
    name?: string;
    start?: number;
    tod?: string;
    time?: number;
    ambient?: [number, number, number];
    fog?: [number, number, number, number, number];
}
export interface Layout {
    version: number;
    scene: number;
    sectors: { checksum: number; placement: Placement; hidden: boolean }[];
    rails: { name: number; placement: Placement; hidden: boolean }[];
    clones: { source: number; placement: Placement }[];
    objects: { id: number; pos: V3; rows: number[] }[];
    copies: { name: number; source: number; pos: V3; rows: number[] }[];
    sets: SetRecord[];
    nodes: NodeRecord[];
    crownOrder: string[];
    hidden: number[];
    locks: { kind: string; id: number }[];
    points: PointRecord[];
    links: { name: number; links: number[] }[];
    dropped: number[];
    terrains: { name: number; terrain: string }[];
    facings: { name: number; facing: number }[]; // (phase 4b) level ladder points turned
    grindFlat: number[]; // (phase 4b) level rail points grindable on flat ground
    walls: { checksum: number; placement: Placement; off: boolean }[];
    level: LevelProps;
    unknown: { line: number; kind: string; text: string }[];
    // (phase 5) the editor's trigger volumes: name, kind and their key=value fields as written (level_editor_triggers.h),
    // and the record as the DLL reads it (`spec`)
    triggers: { line: number; name: string; kind: string; fields: Record<string, string>; spec: TriggerSpec }[];
}

export class LayoutError extends Error {
    readonly line: number;
    constructor(line: number, message: string) {
        super(`line ${line}: ${message}`);
        this.line = line;
    }
}

const HEADER = "thug2-decomp layout ";
export const TOD_STATES = ["morning", "afternoon", "evening", "night"] as const;

function parseHex8(text: string | undefined, loose = false): number | null {
    if (text === undefined) return null;
    if (!(loose ? /^[0-9A-Fa-f]{1,8}$/ : /^[0-9A-Fa-f]{8}$/).test(text)) return null;
    return Number.parseInt(text, 16) >>> 0;
}

function parseNumber(text: string | undefined): number | null {
    if (text === undefined || text.trim() === "") return null;
    const v = Number(text);
    return Number.isFinite(v) ? v : null;
}

function parseTriple(text: string): V3 | null {
    const parts = text.split(",");
    if (parts.length !== 3) return null;
    const n = parts.map(parseNumber);
    if (n.some((x) => x === null)) return null;
    return { x: n[0]!, y: n[1]!, z: n[2]! };
}

// "#XXXXXXXX" / "0xXXXXXXXX" / an identifier -> its checksum; null otherwise (level_editor_fields.h's ParseName).
export function parseName(text: string): number | null {
    const t = text.trim();
    let hex: string | null = null;
    if (t.length === 9 && t[0] === "#") hex = t.slice(1);
    else if (t.length === 10 && /^0[xX]/.test(t)) hex = t.slice(2);
    if (hex !== null) return parseHex8(hex);
    if (!/^[A-Za-z_][A-Za-z0-9_]*$/.test(t) || t.length > 120) return null;
    return checksum(t);
}

export function unescapeText(text: string): string | null {
    let out = "";
    for (let i = 0; i < text.length; i++) {
        const c = text[i]!;
        if (c !== "%") {
            out += c;
            continue;
        }
        const hex = text.slice(i + 1, i + 3);
        if (!/^[0-9A-Fa-f]{2}$/.test(hex)) return null;
        const v = Number.parseInt(hex, 16);
        if (v === 0) return null;
        out += String.fromCharCode(v);
        i += 2;
    }
    return out;
}

function listParts(body: string): string[] {
    return body.split(/[,\s]+/).filter((p) => p.length > 0);
}

function isInt(text: string): boolean {
    return /^\s*[-+]?\d+\s*$/.test(text) && Math.abs(Number(text)) <= 2147483648;
}

// A record's f.<field>= value (level_editor_fields.h: i: f: p: v: n: s: l: flag: an: ai: af:); null: malformed.
export function decodeValue(text: string): FieldValue | null {
    const colon = text.indexOf(":");
    if (colon < 0) return null;
    const type = text.slice(0, colon);
    const body = text.slice(colon + 1);
    const floats = (want: number): number[] | null => {
        const parts = listParts(body);
        if (parts.length !== want) return null;
        const n = parts.map(parseNumber);
        return n.some((x) => x === null) ? null : (n as number[]);
    };
    switch (type) {
        case "i":
            if (!isInt(body)) return null;
            {
                const v = Number(body);
                if (v < -2147483648 || v > 2147483647) return null;
                return { kind: "integer", integer: v };
            }
        case "f": {
            const n = floats(1);
            return n ? { kind: "float", numbers: n } : null;
        }
        case "p": {
            const n = floats(2);
            return n ? { kind: "pair", numbers: n } : null;
        }
        case "v": {
            const n = floats(3);
            return n ? { kind: "vector", numbers: n } : null;
        }
        case "n": {
            const c = parseName(body);
            return c === null ? null : { kind: "name", checksum: c, nameTexts: [/^0[xX#]/.test(body) ? null : body] };
        }
        case "s":
        case "l": {
            const t = unescapeText(body);
            return t === null || t.length > 255 ? null : { kind: type === "s" ? "string" : "local", text: t };
        }
        case "flag":
            return body === "0" || body === "1" ? { kind: "flag", on: body === "1" } : null;
        case "an": {
            const parts = listParts(body);
            const names = parts.map(parseName);
            if (names.some((n) => n === null)) return null;
            return {
                kind: "names",
                names: names as number[],
                nameTexts: parts.map((p) => (/^0[xX]|^#/.test(p) ? null : p)),
            };
        }
        case "ai": {
            const parts = listParts(body);
            if (!parts.every(isInt)) return null;
            return { kind: "integers", names: parts.map((p) => Number(p) | 0) };
        }
        case "af": {
            const parts = listParts(body);
            const n = parts.map(parseNumber);
            return n.some((x) => x === null) ? null : { kind: "floats", numbers: n as number[] };
        }
        default:
            return null;
    }
}

function readPlacement(words: string[], at: number): Placement | null {
    const q = Number(words[at]);
    const xyz = [words[at + 1], words[at + 2], words[at + 3]].map(parseNumber);
    if (!Number.isInteger(q) || q < 0 || q > 3 || xyz.some((v) => v === null)) return null;
    return { quarters: q, offset: { x: xyz[0]!, y: xyz[1]!, z: xyz[2]! }, scale: 1 };
}

function readScale(words: string[], at: number, p: Placement): boolean {
    for (const w of words.slice(at)) {
        if (w.startsWith("scale=")) {
            const s = parseNumber(w.slice(6));
            if (s === null || !(s > 0)) return false;
            p.scale = s;
        }
    }
    return true;
}

function validWord(w: string): boolean {
    return /^[A-Za-z0-9_]{1,63}$/.test(w);
}

function keyValues(words: string[], line: number): [string, string][] {
    const out: [string, string][] = [];
    for (const w of words) {
        const eq = w.indexOf("=");
        if (eq <= 0) throw new LayoutError(line, `"${w}" isn't key=value`);
        out.push([w.slice(0, eq), w.slice(eq + 1)]);
    }
    return out;
}

export function readLayout(text: string): Layout {
    const layout: Layout = {
        version: 0,
        scene: 0,
        sectors: [],
        rails: [],
        clones: [],
        objects: [],
        copies: [],
        sets: [],
        nodes: [],
        crownOrder: [],
        hidden: [],
        locks: [],
        points: [],
        links: [],
        dropped: [],
        terrains: [],
        facings: [],
        grindFlat: [],
        walls: [],
        level: {},
        unknown: [],
        triggers: [],
    };
    const lines = text.split("\n");
    let header = false;
    lines.forEach((raw, index) => {
        const number = index + 1;
        const hash = raw.indexOf("#");
        const line = (hash >= 0 ? raw.slice(0, hash) : raw).replace(/\r$/, "");
        const words = line.split(/\s+/).filter((w) => w.length > 0);
        if (!words.length) return;
        const fail = (why: string): never => {
            throw new LayoutError(number, why);
        };
        if (!header) {
            const m = /^thug2-decomp layout (\d+)\s*$/.exec(line.trim());
            if (!line.startsWith(HEADER) || !m || Number(m[1]) < 1) fail("not a layout file (no header)");
            layout.version = Number(m![1]);
            header = true;
            return;
        }
        const kind = words[0]!;
        const hex = (i: number): number => parseHex8(words[i], true) ?? fail(`bad checksum "${words[i] ?? ""}"`);
        switch (kind) {
            case "scene":
                layout.scene = hex(1);
                break;
            case "sector":
            case "rail": {
                const name = hex(1);
                const p = readPlacement(words, 2) ?? fail("bad placement");
                const hidden = words[6];
                if (hidden !== "0" && hidden !== "1") fail("bad hidden flag");
                if (!readScale(words, 7, p)) fail("bad scale=");
                if (kind === "sector") layout.sectors.push({ checksum: name, placement: p, hidden: hidden === "1" });
                else layout.rails.push({ name, placement: p, hidden: hidden === "1" });
                break;
            }
            case "clone": {
                const source = hex(1);
                const p = readPlacement(words, 2) ?? fail("bad placement");
                if (!readScale(words, 6, p)) fail("bad scale=");
                layout.clones.push({ source, placement: p });
                break;
            }
            case "object":
            case "copy": {
                const ids = kind === "copy" ? [hex(1), hex(2)] : [hex(1)];
                const numbers = words.slice(1 + ids.length).map(parseNumber);
                if (numbers.length < 12 || numbers.slice(0, 12).some((v) => v === null)) fail("needs 12 numbers");
                const n = numbers as number[];
                const pos = { x: n[0]!, y: n[1]!, z: n[2]! };
                const rows = n.slice(3, 12);
                if (kind === "object") layout.objects.push({ id: ids[0]!, pos, rows });
                else layout.copies.push({ name: ids[0]!, source: ids[1]!, pos, rows });
                break;
            }
            case "set": {
                const record: SetRecord = { name: hex(1), fields: [] };
                for (const [key, value] of keyValues(words.slice(2), number)) {
                    if (key === "pos" || key === "angles") {
                        if (record[key]) fail(`${key} twice`);
                        record[key] = parseTriple(value) ?? fail(`bad ${key}`);
                    } else if (key.startsWith("f.") && key.length > 2) {
                        const field = parseName(key.slice(2)) ?? fail(`bad field name ${key}`);
                        if (record.fields.some((f) => f.key === field)) fail(`${key} twice`);
                        const keyText = /^0[xX]/.test(key.slice(2)) ? null : key.slice(2);
                        if (value === "del") record.fields.push({ key: field, keyText, removed: true });
                        else {
                            const v = decodeValue(value) ?? fail(`bad value for ${key}`);
                            record.fields.push({ key: field, keyText, removed: false, value: v });
                        }
                    } // (other keys: a later version's - skipped, as the DLL does)
                }
                layout.sets.push(record);
                break;
            }
            case "node": {
                const name = words[1] ?? "";
                const cls = words[2] ?? "";
                if (!validWord(name) || !validWord(cls)) fail("bad node name / class");
                const node: NodeRecord = {
                    name,
                    cls,
                    type: "",
                    pos: { x: 0, y: 0, z: 0 },
                    angles: { x: 0, y: 0, z: 0 },
                    types: [],
                    colour: [255, 255, 255],
                    brightness: 0,
                    inner: 0,
                    outer: 0,
                    radius: 0,
                };
                const seen = new Set<string>();
                for (const [key, value] of keyValues(words.slice(3), number)) {
                    if (seen.has(key)) fail(`${key} twice`);
                    seen.add(key);
                    if (key === "type") node.type = validWord(value) ? value : fail("bad type");
                    else if (key === "pos" || key === "angles") node[key] = parseTriple(value) ?? fail(`bad ${key}`);
                    else if (key === "types")
                        node.types = value.split(",").map((t) => (validWord(t) ? t : fail("bad types")));
                    else if (key === "color") {
                        const c = parseTriple(value) ?? fail("bad color");
                        node.colour = [c.x, c.y, c.z];
                    } else if (key === "brightness" || key === "inner" || key === "outer" || key === "radius") {
                        node[key] = parseNumber(value) ?? fail(`bad ${key}`);
                    }
                }
                if (!seen.has("pos")) fail("a node record without pos");
                layout.nodes.push(node);
                break;
            }
            case "order":
                if (words[1] === "crown") {
                    layout.crownOrder = words.slice(2).map((w) => (validWord(w) ? w : fail("bad crown name")));
                }
                break;
            case "hide":
                layout.hidden.push(hex(1));
                break;
            case "lock":
                layout.locks.push({ kind: words[1] ?? "", id: hex(2) });
                break;
            case "point": {
                const name = words[1] ?? "";
                const cls = words[2] ?? "";
                if (!validWord(name) || (cls !== "RailNode" && cls !== "Waypoint" && cls !== "ClimbingNode"))
                    fail("bad point");
                const p: PointRecord = {
                    name,
                    cls,
                    pos: { x: 0, y: 0, z: 0 },
                    like: 0,
                    terrain: "",
                    links: [],
                    facing: 0,
                    cluster: 0,
                    grindFlat: false,
                };
                let havePos = false;
                for (const [key, value] of keyValues(words.slice(3), number)) {
                    if (key === "pos") {
                        p.pos = parseTriple(value) ?? fail("bad pos");
                        havePos = true;
                    } else if (key === "like") p.like = parseHex8(value) ?? fail("bad like");
                    else if (key === "terrain") p.terrain = validWord(value) ? value : fail("bad terrain");
                    else if (key === "links") p.links = value.split(",").map((w) => parseHex8(w) ?? fail("bad links"));
                    else if (key === "facing") p.facing = parseNumber(value) ?? fail("bad facing");
                    else if (key === "cluster") p.cluster = parseHex8(value) ?? fail("bad cluster");
                    else if (key === "grindflat")
                        p.grindFlat = value === "1" ? true : value === "0" ? false : fail("bad grindflat");
                }
                if (!havePos) fail("a point without pos");
                layout.points.push(p);
                break;
            }
            case "links": {
                const name = hex(1);
                const value = words[2] ?? fail("no links");
                const links = value === "-" ? [] : value.split(",").map((w) => parseHex8(w) ?? fail("bad links"));
                layout.links.push({ name, links });
                break;
            }
            case "drop":
                layout.dropped.push(hex(1));
                break;
            case "terrain":
                layout.terrains.push({
                    name: hex(1),
                    terrain: validWord(words[2] ?? "") ? words[2]! : fail("bad terrain"),
                });
                break;
            case "facing": {
                const name = hex(1);
                if (layout.facings.some((f) => f.name === name)) fail("a point's facing twice");
                layout.facings.push({ name, facing: parseNumber(words[2] ?? "") ?? fail("bad facing") });
                break;
            }
            case "grindflat": {
                const name = hex(1);
                if (!layout.grindFlat.includes(name)) layout.grindFlat.push(name);
                break;
            }
            case "wall": {
                const name = hex(1);
                const p = readPlacement(words, 2) ?? fail("bad placement");
                const off = words[6];
                if (off !== "0" && off !== "1") fail("bad off flag");
                layout.walls.push({ checksum: name, placement: p, off: off === "1" });
                break;
            }
            case "level": {
                const level: LevelProps = {};
                for (const [key, value] of keyValues(words.slice(1), number)) {
                    if (key === "name") {
                        const t = unescapeText(value);
                        level.name = t && t.length <= 64 ? t : fail("bad name");
                    } else if (key === "start") level.start = parseHex8(value) ?? fail("bad start");
                    else if (key === "tod") {
                        const t = value.toLowerCase();
                        level.tod = (TOD_STATES as readonly string[]).includes(t) ? t : fail("bad tod");
                    } else if (key === "time")
                        level.time = value === "0" || value === "1" ? Number(value) : fail("bad time");
                    else if (key === "ambient" || key === "fog") {
                        const n = value.split(",").map((v) => (isInt(v) ? Number(v) : fail(`bad ${key}`)));
                        if (n.length !== (key === "ambient" ? 3 : 5)) fail(`bad ${key}`);
                        if (key === "ambient") level.ambient = n as [number, number, number];
                        else level.fog = n as [number, number, number, number, number];
                    }
                }
                layout.level = level;
                break;
            }
            case "trigger": {
                // (phase 5: trigger <name> <kind> key=value... - text="..." may hold spaces)
                const name = words[1] ?? "";
                const triggerKind = words[2] ?? "";
                if (!validWord(name) || !triggerKind) fail("bad trigger");
                const fields: Record<string, string> = {};
                const rest = line.trim().split(/\s+/).slice(3).join(" ");
                for (const m of rest.matchAll(/(\w+)=("(?:[^"\\]|\\.)*"|\S+)/g)) fields[m[1]!] = m[2]!;
                if (!fields.pos) fail("a trigger without pos");
                // (the DLL's reader refuses the whole file over a record it can't read: a bad value, a repeated key)
                const spec = readTriggerRecord(line.trim().replace(/^trigger\s+/, "")) ?? fail("bad trigger");
                layout.triggers.push({ line: number, name, kind: triggerKind, fields, spec });
                break;
            }
            default:
                layout.unknown.push({ line: number, kind, text: line.trim() });
        }
    });
    if (!header) throw new LayoutError(0, "empty: not a layout file");
    return layout;
}
