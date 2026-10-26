// NOT ORIGINAL - the level editor's saved layouts (thug2-decomp-layouts\<scene>.txt) read as the DLL reads them
// (src/game/front/level_editor_layout.h has the format; level_editor_fields.h the `set` record's f.<field>= values,
// level_editor_level_props.h the `level` record, level_editor_chains.h the phase 4 records, level_editor_walls.h the
// `wall` record). Pure: tools/pipeline/game/apply_layout.ts bakes a layout into a level's files with it.
//
// A record the DLL would refuse (malformed) is an error here too, with its line; a record of a kind this reader
// doesn't know (a later version's) is kept in `unknown` - the bake warns about each instead of skipping it silently.
// Phase 5's `trigger` records are read by tools/pipeline/game/triggers.ts (the DLL's Triggers::ReadRecord, ported).

import { checksum } from "../level_port/qb.ts";
import { type GroundSpec, readGroundRecord } from "./ground_pieces.ts";
import { readTriggerRecord, type TriggerSpec } from "./triggers.ts";

export interface V3 {
    x: number;
    y: number;
    z: number;
}
// A point x as loaded is at QuarterTurn(PerAxis(x, scale), quarters) + offset (level_editor.h's Placement): `scale` is
// along the piece's own axes as loaded - uniform (`scale=s`) or stretched (`scale=x,y,z`); 1, 1, 1 unless scaled.
export interface Placement {
    quarters: number;
    offset: V3;
    scale: V3;
}
export const UNIT_SCALE: Readonly<V3> = { x: 1, y: 1, z: 1 };
export function isUnitScale(s: V3): boolean {
    return s.x === 1 && s.y === 1 && s.z === 1;
}
export function isUniformScale(s: V3): boolean {
    return s.x === s.y && s.y === s.z;
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
    // (the feature map) a ClimbingNode's Type ("" a ladder, "Ledge"), a ledge's side (0, 1 left, 2 right), a rail
    // point's speed-up rate (0: none)
    type: string;
    hang: number;
    speed: number;
}
// (the feature map - level_editor_triggers.h's ProximEdit) a proximity node's scripts and shape
export interface ProximRecord {
    node: number;
    enter: string;
    exit: string;
    shape: "" | "sphere" | "box";
    radius: number;
    size: V3;
    offset: V3;
}
export const SPEED_MIN = 0.005;
export const SPEED_MAX = 0.5;
export const PROXIM_MIN = 10;
export const PROXIM_MAX = 20000;
export interface LevelProps {
    name?: string;
    start?: number;
    tod?: string;
    time?: number;
    ambient?: [number, number, number];
    fog?: [number, number, number, number, number];
    sky?: string; // (map3) another level's sky scene - LevelProps::ValidSky
}
// (map3 - level_editor_faces.h) a sector's collision faces' flags as edited: [face index, flags], ascending.
export interface FaceRecord {
    checksum: number;
    faces: [number, number][];
}
// (map3 - level_editor_clusters.h) a scenery piece's trick-object cluster (trick false: not a trick object).
export interface ClusterRecord {
    sector: number;
    trick: boolean;
    cluster: number;
}
// (map3 - level_editor_cameras.h) a camera node.
export interface CameraRecord {
    name: string;
    pos: V3;
    angles: V3;
    distance: number;
}
export const CAMERA_DISTANCE = { min: 50, max: 20000, default: 1000 } as const;
// (UX5 - level_editor_notes.h) a note pin: editor only - never baked (apply_layout.ts passes it over silently).
// `colour`: RGB; `text` as typed (the record percent-encodes % " # and control characters).
export interface NoteRecord {
    name: string;
    pos: V3;
    colour: number;
    text: string;
}
export const NOTE_MAX_TEXT = 500; // (bytes - Notes::kMaxText)
// Notes::Decode: %XX escapes back into bytes (the rest as UTF-8); null for a bad escape or a raw " / #.
export function decodeNoteText(encoded: string): string | null {
    const bytes: number[] = [];
    const encoder = new TextEncoder();
    for (let i = 0; i < encoded.length; i++) {
        const c = encoded[i]!;
        if (c === '"' || c === "#") return null;
        if (c !== "%") {
            const code = encoded.codePointAt(i)!;
            const char = String.fromCodePoint(code);
            bytes.push(...encoder.encode(char));
            i += char.length - 1;
            continue;
        }
        const hex = encoded.slice(i + 1, i + 3);
        if (!/^[0-9A-Fa-f]{2}$/.test(hex)) return null;
        bytes.push(Number.parseInt(hex, 16));
        i += 2;
    }
    return new TextDecoder().decode(Uint8Array.from(bytes.slice(0, NOTE_MAX_TEXT)));
}
// Notes::ReadRecord ported: the words after "note" as the line has them (comment cut) - a name, pos= (required),
// color=RRGGBB, text="..." last; a repeated key, a bad value, text= not last or not quoted refuses it (null); other
// keys skipped.
export function readNoteRecord(rest: string): NoteRecord | null {
    const m = /^\s*(\S+)(.*)$/.exec(rest);
    if (!m || !validWord(m[1]!)) return null;
    const note: NoteRecord = { name: m[1]!, pos: { x: 0, y: 0, z: 0 }, colour: 0xffd84a, text: "" };
    const tail = m[2]!;
    let at = 0;
    let pos = false,
        colour = false,
        text = false;
    for (;;) {
        while (at < tail.length && tail[at] === " ") at++;
        if (at >= tail.length) break;
        if (text) return null;
        const equals = tail.indexOf("=", at);
        if (equals < 0) return null;
        const key = tail.slice(at, equals);
        if (key.includes(" ")) return null;
        if (key === "text") {
            if (tail[equals + 1] !== '"') return null;
            const close = tail.indexOf('"', equals + 2);
            if (close < 0) return null;
            const decoded = decodeNoteText(tail.slice(equals + 2, close));
            if (decoded === null) return null;
            note.text = decoded;
            text = true;
            at = close + 1;
            continue;
        }
        const end = tail.indexOf(" ", equals);
        const value = tail.slice(equals + 1, end < 0 ? undefined : end);
        at = end < 0 ? tail.length : end;
        if (key === "pos") {
            const p = parseTriple(value);
            if (pos || !p) return null;
            note.pos = p;
            pos = true;
        } else if (key === "color") {
            if (colour || !/^[0-9A-Fa-f]{6}$/.test(value)) return null;
            note.colour = Number.parseInt(value, 16);
            colour = true;
        } // (another key: a later version's - skipped)
    }
    return pos ? note : null;
}
// LevelProps::ValidSky: 1-64 letters, digits, _, holding "_sky" (what the load takes for a sky).
export function validSky(sky: string): boolean {
    return /^[A-Za-z0-9_]{1,64}$/.test(sky) && sky.toLowerCase().includes("_sky");
}
export interface Layout {
    version: number;
    scene: number;
    sectors: { checksum: number; placement: Placement; hidden: boolean }[];
    rails: { name: number; placement: Placement; hidden: boolean }[];
    clones: { source: number; placement: Placement }[];
    // (scale: the object's model and collision along its own axes - a trailing scale= on the record, 1, 1, 1 without)
    objects: { id: number; pos: V3; rows: number[]; scale: V3 }[];
    copies: { name: number; source: number; pos: V3; rows: number[]; scale: V3 }[];
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
    railSpeeds: { name: number; rate: number }[]; // (the feature map) level rail points given a speed-up
    proxims: ProximRecord[]; // (the feature map) proximity nodes' scripts / shapes
    walls: { checksum: number; placement: Placement; off: boolean }[];
    level: LevelProps;
    unknown: { line: number; kind: string; text: string }[];
    // (phase 5) the editor's trigger volumes: name, kind and their key=value fields as written (level_editor_triggers.h),
    // and the record as the DLL reads it (`spec`)
    triggers: { line: number; name: string; kind: string; fields: Record<string, string>; spec: TriggerSpec }[];
    // (level_editor_entities.h) moving objects' path speeds (mph) and sound emitters' sounds (sound 0: none; radius 0:
    // the node's own)
    speeds: { name: number; mph: number }[];
    emitters: { name: number; sound: number; volume: number; radius: number }[];
    // (map3) collision faces' flags, scenery clusters, camera nodes
    faces: FaceRecord[];
    clusters: ClusterRecord[];
    cameras: CameraRecord[];
    // (UX5) note pins - editor only, never baked
    notes: NoteRecord[];
    // (UX7) linked copies' groups - the copies are `clone` / `copy` records of their own; the bake writes them as they
    // are (plain copies): a link only matters to the editor
    linkGroups: LinkRecord[];
    // (UX9) raised / lowered ground pieces (level_editor_ground.h - tools/pipeline/game/ground_pieces.ts); a record the DLL
    // wouldn't read is skipped there (logged) - here listed as unknown
    grounds: GroundSpec[];
}

// (UX7 - level_editor_links.h) a group of linked copies: `link sector|object <source> <member>[@q],...`. A sector is
// referred to by its checksum or (`clone` >= 0) by the index of its layout's `clone` record; `quarters` (sectors only):
// the source's quarter turns when that copy was made.
export interface LinkRef {
    id: number;
    clone: number;
    quarters: number;
}
export interface LinkRecord {
    scenery: boolean;
    source: LinkRef;
    members: LinkRef[];
}
function parseLinkRef(text: string, member: boolean, scenery: boolean): LinkRef | null {
    const m = /^(c\d+|[0-9A-Fa-f]{8})(?:@([0-3]))?$/.exec(text);
    if (!m) return null;
    if (m[2] !== undefined && (!member || !scenery)) return null;
    const quarters = m[2] === undefined ? 0 : Number(m[2]);
    if (m[1]!.startsWith("c")) {
        if (!scenery) return null;
        return { id: 0, clone: Number(m[1]!.slice(1)), quarters };
    }
    const id = Number.parseInt(m[1]!, 16);
    return id ? { id, clone: -1, quarters } : null;
}
// Links::ReadRecord ported: the words after "link" (null: malformed; later words skipped).
export function readLinkRecord(words: string[]): LinkRecord | null {
    const [kind, source, members] = words;
    if ((kind !== "sector" && kind !== "object") || source === undefined || members === undefined) return null;
    const scenery = kind === "sector";
    const src = parseLinkRef(source, false, scenery);
    if (!src) return null;
    const refs: LinkRef[] = [];
    for (const word of members.split(",")) {
        const ref = parseLinkRef(word, true, scenery);
        if (!ref) return null;
        refs.push(ref);
    }
    return { scenery, source: src, members: refs };
}

// The DLL's limits for those (level_editor_entities.h kMinMph...).
export const ENTITY_LIMITS = {
    minMph: 0.1,
    maxMph: 200,
    minVolume: 0,
    maxVolume: 400,
    minRadius: 1,
    maxRadius: 100000,
};

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
    return { quarters: q, offset: { x: xyz[0]!, y: xyz[1]!, z: xyz[2]! }, scale: { ...UNIT_SCALE } };
}

// A `scale=` token (level_editor_scale.h's Scale::ReadToken): one number (uniform) or exactly three separated by commas
// (x, y, z along the piece's own axes), each a float above 0; null: malformed.
export function readScaleToken(word: string): V3 | null {
    if (!word.startsWith("scale=") || word.length === 6) return null;
    const parts = word.slice(6).split(",");
    if (parts.length !== 1 && parts.length !== 3) return null;
    const n = parts.map((t) => {
        const v = parseNumber(t);
        return v === null ? null : Math.fround(v);
    });
    if (n.some((v) => v === null || !(v > 0) || !Number.isFinite(v))) return null;
    return parts.length === 1 ? { x: n[0]!, y: n[0]!, z: n[0]! } : { x: n[0]!, y: n[1]!, z: n[2]! };
}

// The key=value fields after a sector / rail / clone / object / copy record's numbers (ReadScaleFields): scale=;
// others (a later version's) skipped. Null: a bad scale.
function readScale(words: string[], at: number): V3 | null {
    let scale: V3 = { ...UNIT_SCALE };
    for (const w of words.slice(at)) {
        if (w.startsWith("scale=")) {
            const s = readScaleToken(w);
            if (!s) return null;
            scale = s;
        }
    }
    return scale;
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

// A `proxim` record's words after "proxim" (the comment already cut) - Triggers::ReadProximRecord ported: the node in 8
// hex digits, enter= / exit= names, shape=sphere|box with its radius= / size= (sizes and radii 10 .. 20000), offset=;
// a repeated key or a bad value refuses it (null); unknown keys skipped.
export function readProximRecord(words: string[]): ProximRecord | null {
    const node = parseHex8(words[0]);
    if (node === null) return null;
    const p: ProximRecord = {
        node,
        enter: "",
        exit: "",
        shape: "",
        radius: 0,
        size: { x: 0, y: 0, z: 0 },
        offset: { x: 0, y: 0, z: 0 },
    };
    const seen = new Set<string>();
    const inRange = (v: number): boolean => Math.fround(v) >= PROXIM_MIN && Math.fround(v) <= PROXIM_MAX;
    for (const field of words.slice(1)) {
        const equals = field.indexOf("=");
        if (equals <= 0) return null;
        const key = field.slice(0, equals);
        const value = field.slice(equals + 1);
        if (["enter", "exit", "shape", "radius", "size", "offset"].includes(key)) {
            if (seen.has(key)) return null;
            seen.add(key);
        }
        if (key === "enter" || key === "exit") {
            if (!validWord(value)) return null;
            p[key] = value;
        } else if (key === "shape") {
            if (value !== "sphere" && value !== "box") return null;
            p.shape = value;
        } else if (key === "radius") {
            const r = parseNumber(value);
            if (r === null || !inRange(r)) return null;
            p.radius = r;
        } else if (key === "size") {
            const s = parseTriple(value);
            if (!s || !inRange(s.x) || !inRange(s.y) || !inRange(s.z)) return null;
            p.size = s;
        } else if (key === "offset") {
            const o = parseTriple(value);
            if (!o) return null;
            p.offset = o;
        }
    }
    if (p.shape === "box" && !seen.has("size")) return null;
    if (p.shape === "sphere" && !seen.has("radius")) return null;
    return p;
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
        railSpeeds: [],
        proxims: [],
        walls: [],
        level: {},
        unknown: [],
        triggers: [],
        speeds: [],
        emitters: [],
        faces: [],
        clusters: [],
        cameras: [],
        notes: [],
        linkGroups: [],
        grounds: [],
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
                p.scale = readScale(words, 7) ?? fail("bad scale=");
                if (kind === "sector") layout.sectors.push({ checksum: name, placement: p, hidden: hidden === "1" });
                else layout.rails.push({ name, placement: p, hidden: hidden === "1" });
                break;
            }
            case "clone": {
                const source = hex(1);
                const p = readPlacement(words, 2) ?? fail("bad placement");
                p.scale = readScale(words, 6) ?? fail("bad scale=");
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
                const scale = readScale(words, 1 + ids.length + 12) ?? fail("bad scale=");
                if (kind === "object") layout.objects.push({ id: ids[0]!, pos, rows, scale });
                else layout.copies.push({ name: ids[0]!, source: ids[1]!, pos, rows, scale });
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
                    type: "",
                    hang: 0,
                    speed: 0,
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
                    else if (key === "type")
                        p.type = value.toLowerCase() === "ledge" && cls === "ClimbingNode" ? "Ledge" : fail("bad type");
                    else if (key === "hang") p.hang = value === "left" ? 1 : value === "right" ? 2 : fail("bad hang");
                    else if (key === "speed") {
                        const rate = parseNumber(value);
                        p.speed =
                            rate !== null &&
                            Math.fround(rate) >= Math.fround(SPEED_MIN) &&
                            Math.fround(rate) <= Math.fround(SPEED_MAX) &&
                            cls === "RailNode"
                                ? rate
                                : fail("bad speed");
                    }
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
            case "speed": {
                // (level_editor_entities.h: a node's path speed in mph - the DLL refuses the file over a bad one)
                const name = parseHex8(words[1]) ?? fail("bad speed node");
                const mph = Math.fround(parseNumber(words[2]) ?? fail("bad speed"));
                if (mph < Math.fround(ENTITY_LIMITS.minMph) || mph > ENTITY_LIMITS.maxMph) fail("speed out of range");
                if (layout.speeds.some((s) => s.name === name)) fail("a node's speed twice");
                layout.speeds.push({ name, mph });
                break;
            }
            case "emitter": {
                const name = parseHex8(words[1]) ?? fail("bad emitter node");
                if (name === 0) fail("bad emitter node");
                if (layout.emitters.some((e) => e.name === name)) fail("an emitter twice");
                const e = { name, sound: 0, volume: 100, radius: 0 };
                const seen = new Set<string>();
                for (const [key, value] of keyValues(words.slice(2), number)) {
                    if (seen.has(key)) fail(`${key} twice`);
                    seen.add(key);
                    if (key === "sound") e.sound = parseHex8(value) ?? fail("bad sound");
                    else if (key === "vol") {
                        e.volume = Math.fround(parseNumber(value) ?? fail("bad vol"));
                        if (e.volume < ENTITY_LIMITS.minVolume || e.volume > ENTITY_LIMITS.maxVolume)
                            fail("vol out of range");
                    } else if (key === "radius") {
                        e.radius = Math.fround(parseNumber(value) ?? fail("bad radius"));
                        if (
                            e.radius !== 0 &&
                            (e.radius < ENTITY_LIMITS.minRadius || e.radius > ENTITY_LIMITS.maxRadius)
                        )
                            fail("radius out of range");
                    } // (other keys: a later version's - skipped)
                }
                layout.emitters.push(e);
                break;
            }
            case "grindflat": {
                const name = hex(1);
                if (!layout.grindFlat.includes(name)) layout.grindFlat.push(name);
                break;
            }
            case "railspeed": {
                // (the feature map: railspeed <node> <rate> - Chains::ReadRecord's: 8 hex digits, a rate in range, once)
                const name = parseHex8(words[1]) ?? fail("bad checksum");
                const rate = parseNumber(words[2]);
                if (
                    rate === null ||
                    Math.fround(rate) < Math.fround(SPEED_MIN) ||
                    Math.fround(rate) > Math.fround(SPEED_MAX)
                )
                    fail("bad speed");
                if (layout.railSpeeds.some((s) => s.name === name)) fail("a point's speed twice");
                layout.railSpeeds.push({ name, rate: rate! });
                break;
            }
            case "proxim":
                layout.proxims.push(readProximRecord(words.slice(1)) ?? fail("bad proxim"));
                break;
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
                    else if (key === "sky") {
                        // (map3) a sky scene (%-escaped like name=; LevelProps::ValidSky)
                        const t = unescapeText(value);
                        level.sky = t !== null && validSky(t) ? t : fail("bad sky");
                    } else if (key === "ambient" || key === "fog") {
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
            case "faces": {
                // (map3: Faces::ReadRecord) faces <checksum> <face>:<flags>,... - faces 0..65535 once each, flags 1-4 hex
                const sector = parseHex8(words[1]) ?? fail("bad checksum");
                if (words.length !== 3) fail("faces: <checksum> <face>:<flags>,...");
                const faces: [number, number][] = [];
                for (const item of words[2]!.split(",")) {
                    const m = /^(\d+):([0-9A-Fa-f]{1,4})$/.exec(item);
                    if (!m) fail(`bad face "${item}"`);
                    const face = Number(m![1]);
                    if (face > 0xffff) fail(`bad face "${item}"`);
                    if (faces.some(([f]) => f === face)) fail(`face ${face} twice`);
                    faces.push([face, Number.parseInt(m![2]!, 16)]);
                }
                faces.sort((a, b) => a[0] - b[0]);
                layout.faces.push({ checksum: sector, faces });
                break;
            }
            case "cluster": {
                // (map3: Clusters::ReadRecord) cluster <sector> <cluster> | -
                if (words.length !== 3) fail("cluster: <sector> <cluster> | -");
                const sector = parseHex8(words[1]) ?? fail("bad checksum");
                if (!sector) fail("bad checksum");
                if (words[2] === "-") layout.clusters.push({ sector, trick: false, cluster: 0 });
                else {
                    const cluster = parseHex8(words[2]) ?? fail("bad cluster");
                    if (!cluster) fail("bad cluster");
                    layout.clusters.push({ sector, trick: true, cluster });
                }
                break;
            }
            case "link": {
                // (UX7: Links::ReadRecord) link sector|object <source> <member>,...
                layout.linkGroups.push(readLinkRecord(words.slice(1)) ?? fail("bad link"));
                break;
            }
            case "ground": {
                // (UX9: Ground::ReadRecord) ground <name> raise|lower rect= base= height= ramps= ... material= ...
                const ground = readGroundRecord(line.trim().replace(/^ground\s+/, ""));
                if (ground) layout.grounds.push(ground);
                else layout.unknown.push({ line: number, kind, text: line.trim() });
                break;
            }
            case "note": {
                // (UX5: Notes::ReadRecord) note <name> pos=x,y,z [color=RRGGBB] [text="..."] - editor only
                const note = readNoteRecord(line.trim().replace(/^note\s+/, "")) ?? fail("bad note");
                layout.notes.push(note);
                break;
            }
            case "camera": {
                // (map3: Cameras::ReadRecord) camera <name> pos=x,y,z [angles=x,y,z] [dist=d]
                const name = words[1] ?? "";
                if (!validWord(name)) fail("bad camera name");
                const camera: CameraRecord = {
                    name,
                    pos: { x: 0, y: 0, z: 0 },
                    angles: { x: 0, y: 0, z: 0 },
                    distance: CAMERA_DISTANCE.default,
                };
                const seen = new Set<string>();
                for (const [key, value] of keyValues(words.slice(2), number)) {
                    if (key === "pos" || key === "angles" || key === "dist") {
                        if (seen.has(key)) fail(`${key} twice`);
                        seen.add(key);
                    }
                    if (key === "pos" || key === "angles") camera[key] = parseTriple(value) ?? fail(`bad ${key}`);
                    else if (key === "dist") {
                        const d = parseNumber(value) ?? fail("bad dist");
                        camera.distance = Math.min(CAMERA_DISTANCE.max, Math.max(CAMERA_DISTANCE.min, d));
                    } // (another key: a later version's - skipped)
                }
                if (!seen.has("pos")) fail("a camera without pos");
                layout.cameras.push(camera);
                break;
            }
            default:
                layout.unknown.push({ line: number, kind, text: line.trim() });
        }
    });
    if (!header) throw new LayoutError(0, "empty: not a layout file");
    return layout;
}
