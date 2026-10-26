// NOT ORIGINAL - the level editor's trigger volumes (phase 5) as level data: the pure parts of
// src/game/front/level_editor_triggers.{h,cpp} ported (the record, the compiled script - byte for byte the DLL's
// Triggers::Body / QbFile, which its self-test compares with Boston's own compiled gap scripts) and what
// tools/pipeline/game/apply_layout.ts bakes from them (a node, a trigger box in the collision, the script).
//
// In the game (the DLL) an editor trigger is a run-time thing: a node in the editor's overlay node array (GenericNode,
// type ed_trigger, TriggerScript=<name>Script), a movable collision box (Nx::CCollMovBBox, every face 0x50, named
// after the node) and a script compiled by the editor and defined through Script::LoadQBData. Baked, the same three are
// in the level's own files:
//   - the node: appended to the level's node array, the fields NodeCreate::Fields gives the DLL's node (Pos, Angles
//     (0, yaw, 0), Name, Class GenericNode, Type ed_trigger, TriggerScript <name>Script);
//   - the box: a collision object named after the node - 8 corners, 12 triangles, in the corner and face order of
//     THUG2's own trigger boxes (Boston's TRGP_ShoppingCart), every face 0x1150 as theirs: trigger (0x40), not
//     collidable (0x10), no skater shadow (0x100), invisible (0x1000). The trigger component's feeler only asks for
//     0x50 (SetIgnore(0, 0x50) - level_editor_triggers.h), so it meets the box as it meets the DLL's;
//   - the script: `script <name>Script <body> endscript` - the DLL's tokens - in the level's scripts .qb.
// Both fire on the way in only: the DLL's movable box is met where a line enters it (Mth::BBox::LineIntersect), and a
// static box's faces are one-sided - the static line test (CCollStatic::CollideWithLine 0x00413B50) takes a face only
// through RayHitsTriangle (0x0040B420), which refuses a determinant below 0.03: a line crossing a face from behind (its
// normal (v1 - v0) x (v2 - v0) along the line) doesn't meet it. The boxes' faces point outwards (as THUG2's own), so a
// line leaving the box meets nothing. An exit trigger (exit=1: the DLL emulates leaving with a test of the skater's
// place every frame - Triggers::Play) is baked with the 12 faces again, wound the other way: a line leaving the box
// meets those, and the node's TriggerScript runs again, as the DLL runs it on leaving.

import { checksum, Name, Struct, Vector } from "../level_port/qb.ts";
import { buildTree, type ColObject, type Face, setReaches } from "../level_port/thug2_col.ts";
import { f32, pack } from "../lib/struct.ts";

// (the feature map added boost - a jump boost: trigger_jump_boost speed=<strength> - and break - a breakable: its piece
// shattered and killed, its collision off while it's breakable through `replaces`)
export type Kind = "kill" | "teleport" | "script" | "gapstart" | "gapend" | "boost" | "break" | "camera";
// (the DLL's KindWord order; "other" - a level trigger without a script - is never a record)
export const KINDS: readonly Kind[] = ["kill", "teleport", "script", "gapstart", "gapend", "boost", "break", "camera"];
export const KIND_NAMES: Readonly<Record<Kind, string>> = {
    kill: "Kill zone",
    teleport: "Teleport",
    script: "Runs a script",
    gapstart: "Gap start",
    gapend: "Gap end",
    boost: "Jump boost",
    break: "Breakable",
    camera: "Camera",
};
export const BOOST_MIN = 100;
export const BOOST_MAX = 5000;
// (map4) a camera trigger's seconds (Triggers::kCameraMinSeconds / kCameraMaxSeconds)
export const CAMERA_MIN_SECONDS = 0.5;
export const CAMERA_MAX_SECONDS = 60;

export interface V3 {
    x: number;
    y: number;
    z: number;
}

// A trigger record (Triggers::Placed: its Spec and its node's place).
export interface TriggerSpec {
    name: string;
    kind: Kind;
    pos: V3;
    yaw: number;
    size: V3;
    water: boolean;
    target: string;
    script: string;
    gap: string;
    flags: string[];
    text: string;
    score: number;
    replaces: number;
    // (editor-polish) also fires on leaving the box: the record's exit=1
    exit: boolean;
    // (the feature map) a boost's strength (its upward speed), a breakable's sound ("": none)
    strength: number;
    sound: string;
    // (map4) a camera trigger's shot length: LevelEditor_PlayCamera camera=<target> time=<seconds>
    seconds: number;
}

// Triggers::ClampSeconds: within CAMERA_MIN_SECONDS .. CAMERA_MAX_SECONDS (a float; not a number: 3).
export function clampSeconds(seconds: number): number {
    if (!Number.isFinite(seconds)) return 3;
    return f32(Math.max(CAMERA_MIN_SECONDS, Math.min(CAMERA_MAX_SECONDS, f32(seconds))));
}

export const FACE_FLAGS = 0x1150; // (THUG2's own trigger boxes': 0x40 | 0x10 | 0x100 | 0x1000)

// NodeCreate::ValidName: letters, digits, '_', 1-63 characters.
export function validName(name: string): boolean {
    return /^[A-Za-z0-9_]{1,63}$/.test(name);
}

// "0x" and 8 hex digits: a checksum without a known name.
function hexForm(name: string): number | null {
    return /^0[xX][0-9A-Fa-f]{8}$/.test(name) ? Number.parseInt(name.slice(2), 16) >>> 0 : null;
}

// Triggers::ChecksumOf: the hex form's value, else the name's checksum (0 for none).
export function checksumOf(name: string): number {
    const v = hexForm(name);
    if (v !== null) return v;
    return name ? checksum(name) : 0;
}

export function scriptName(spec: { name: string }): string {
    return `${spec.name}Script`;
}

// Triggers::CleanText: printable, '"' as '\'', no '#', at most 60 characters, no spaces at either end.
export function cleanText(text: string): string {
    let out = "";
    for (const c of text) {
        const u = c.charCodeAt(0);
        if (u < 0x20 || u === 0x7f) continue;
        if (c === "#") continue;
        out += c === '"' ? "'" : c;
        if (out.length >= 60) break;
    }
    return out.replace(/^ +| +$/g, "");
}

// Triggers::ClampSize: each extent within 10 .. 50000 (a float).
export function clampSize(size: V3): V3 {
    const clamp = (v: number): number => (Number.isFinite(v) ? Math.max(10, Math.min(50000, f32(v))) : 10);
    return { x: clamp(size.x), y: clamp(size.y), z: clamp(size.z) };
}

// Triggers::Problem: why it does nothing (null: it works).
export function problem(spec: TriggerSpec): string | null {
    switch (spec.kind) {
        case "kill":
            return spec.target ? null : "Choose the restart the skater goes back to";
        case "teleport":
            return spec.target ? null : "Choose where it sends the skater";
        case "script":
            return spec.script ? null : "Choose the script it runs";
        case "gapstart":
            return spec.gap ? null : "It has no gap";
        case "gapend":
            if (!spec.gap) return "It has no gap";
            if (!spec.text || spec.score <= 0)
                return "A gap needs its name and a score (the gap list takes only those)";
            return null;
        case "boost":
            return null;
        case "break":
            return spec.target ? null : "Choose the piece it breaks";
        case "camera":
            return spec.target ? null : "Choose the camera it plays";
    }
}

// --- the record (Triggers::ReadRecord) ------------------------------------------------------------------------------

function readNumber(text: string): number | null {
    if (!/^[+-]?(\d+\.?\d*|\.\d+)([eE][+-]?\d+)?$/.test(text)) return null;
    const v = f32(Number(text));
    return Number.isFinite(v) ? v : null;
}

function readTriple(text: string): V3 | null {
    const parts = text.split(",");
    if (parts.length !== 3) return null;
    const v = parts.map(readNumber);
    if (v.some((x) => x === null)) return null;
    return { x: v[0]!, y: v[1]!, z: v[2]! };
}

// The record's text after "trigger " (null: one the DLL refuses - a bad value, a repeated key, no pos, kind `other`).
export function readTriggerRecord(line: string): TriggerSpec | null {
    let text = line;
    const spec: TriggerSpec = {
        name: "",
        kind: "kill",
        pos: { x: 0, y: 0, z: 0 },
        yaw: 0,
        size: { x: 400, y: 200, z: 400 },
        water: false,
        target: "",
        script: "",
        gap: "",
        flags: [],
        text: "",
        score: 100,
        replaces: 0,
        exit: false,
        strength: 900,
        seconds: 3,
        sound: "",
    };
    // text="..." (last: it may hold spaces)
    const quote = text.indexOf(' text="');
    if (quote >= 0) {
        const start = quote + 7;
        const end = text.indexOf('"', start);
        if (end < 0 || /[^ \t\r]/.test(text.slice(end + 1))) return null;
        spec.text = cleanText(text.slice(start, end));
        text = text.slice(0, quote);
    }
    const words = text.split(/\s+/).filter((w) => w.length > 0);
    const [name, word] = words;
    if (name === undefined || word === undefined || !validName(name)) return null;
    if (!(KINDS as readonly string[]).includes(word)) return null;
    spec.name = name;
    spec.kind = word as Kind;
    const seen = new Set<string>();
    for (const field of words.slice(2)) {
        const equals = field.indexOf("=");
        if (equals <= 0) return null;
        const key = field.slice(0, equals);
        const value = field.slice(equals + 1);
        const once = (): boolean => {
            if (seen.has(key)) return false;
            seen.add(key);
            return true;
        };
        let ok = true;
        if (key === "pos") {
            const v = readTriple(value);
            ok = once() && v !== null;
            if (v) spec.pos = v;
        } else if (key === "yaw") {
            const v = readNumber(value);
            ok = once() && v !== null;
            if (v !== null) spec.yaw = v;
        } else if (key === "size") {
            const v = readTriple(value);
            ok = once() && v !== null;
            if (v) spec.size = clampSize(v);
        } else if (key === "target") {
            ok = once() && validName(value);
            spec.target = value;
        } else if (key === "water") {
            ok = once() && (value === "0" || value === "1");
            spec.water = value === "1";
        } else if (key === "script") {
            ok = once() && validName(value);
            spec.script = value;
        } else if (key === "gap") {
            ok = once() && validName(value);
            spec.gap = value;
        } else if (key === "flags") {
            ok = once();
            spec.flags = value.split(",");
            ok = ok && spec.flags.every(validName);
        } else if (key === "score") {
            const score = /^[+-]?\d+$/.test(value) ? Number(value) : -1;
            ok = once() && score >= 0 && score <= 1000000;
            spec.score = score;
        } else if (key === "replaces") {
            ok = once() && /^(0[xX])?[0-9A-Fa-f]+$/.test(value) && value.length <= 8;
            spec.replaces = Number.parseInt(value, 16) >>> 0;
        } else if (key === "exit") {
            ok = once() && (value === "0" || value === "1");
            spec.exit = value === "1";
        } else if (key === "strength") {
            const strength = /^[+-]?\d+$/.test(value) ? Number(value) : -1;
            ok = once() && strength >= BOOST_MIN && strength <= BOOST_MAX;
            spec.strength = strength;
        } else if (key === "sound") {
            ok = once() && validName(value);
            spec.sound = value;
        } else if (key === "seconds") {
            const v = readNumber(value);
            ok = once() && v !== null && v >= CAMERA_MIN_SECONDS && v <= CAMERA_MAX_SECONDS;
            if (v !== null) spec.seconds = v;
        } // (another key: a later version's - skipped)
        if (!ok) return null;
    }
    return seen.has("pos") ? spec : null;
}

// --- the compiled script (Triggers::Body / QbFile) -------------------------------------------------------------------

const NAME = 0x16;
const EQUALS = 0x07;
const LINE = 0x01;

function nameToken(c: number): Buffer {
    return Buffer.concat([Buffer.from([NAME]), pack("<I", c >>> 0)]);
}
function nameValue(key: string, value: number): Buffer {
    return Buffer.concat([nameToken(checksum(key)), Buffer.from([EQUALS]), nameToken(value)]);
}

// The script's statements, each ending with an end of line - empty while `problem` says why.
export function body(spec: TriggerSpec): Buffer {
    if (problem(spec)) return Buffer.alloc(0);
    const parts: Buffer[] = [];
    switch (spec.kind) {
        case "kill":
            parts.push(nameToken(checksum(spec.water ? "SK3_KillSkater_Water" : "SK3_KillSkater")));
            parts.push(nameValue("nodename", checksumOf(spec.target)), Buffer.from([LINE]));
            break;
        case "teleport":
            parts.push(nameToken(checksum("TeleportSkaterToNode")));
            parts.push(nameValue("nodename", checksumOf(spec.target)), Buffer.from([LINE]));
            break;
        case "script":
            parts.push(nameToken(checksumOf(spec.script)), Buffer.from([LINE]));
            break;
        case "gapstart":
            parts.push(nameToken(checksum("StartGap")), nameValue("GapID", checksumOf(spec.gap)));
            if (spec.flags.length) {
                parts.push(nameToken(checksum("flags")), Buffer.from([EQUALS, 0x05]));
                spec.flags.forEach((f, i) => {
                    if (i) parts.push(Buffer.from([0x09]));
                    parts.push(nameToken(checksumOf(f)));
                });
                parts.push(Buffer.from([0x06]));
            }
            parts.push(Buffer.from([LINE]));
            break;
        case "gapend": {
            parts.push(nameToken(checksum("EndGap")), nameValue("GapID", checksumOf(spec.gap)));
            parts.push(nameToken(checksum("score")), Buffer.from([EQUALS, 0x17]), pack("<I", spec.score >>> 0));
            const text = Buffer.from(cleanText(spec.text), "latin1");
            parts.push(nameToken(checksum("text")), Buffer.from([EQUALS, 0x1b]), pack("<I", text.length + 1));
            parts.push(text, Buffer.from([0x00, LINE]));
            break;
        }
        case "boost":
            // trigger_jump_boost speed=<n>
            parts.push(nameToken(checksum("trigger_jump_boost")), nameToken(checksum("speed")));
            parts.push(
                Buffer.from([EQUALS, 0x17]),
                pack("<i", Math.max(BOOST_MIN, Math.min(BOOST_MAX, spec.strength))),
            );
            parts.push(Buffer.from([LINE]));
            break;
        case "break": {
            // [PlaySound <sound>] Shatter name=<piece> area=5000 variance=1.0 spread=1.5 life=3 bounce=0 scale=0.4
            // vel_x=0 vel_y=60 vel_z=0, Kill name=<piece> (BO_ShatteringGlass's)
            const piece = checksumOf(spec.target);
            if (spec.sound)
                parts.push(nameToken(checksum("PlaySound")), nameToken(checksumOf(spec.sound)), Buffer.from([LINE]));
            const integer = (key: string, v: number): Buffer =>
                Buffer.concat([nameToken(checksum(key)), Buffer.from([EQUALS, 0x17]), pack("<i", v)]);
            const real = (key: string, v: number): Buffer =>
                Buffer.concat([nameToken(checksum(key)), Buffer.from([EQUALS, 0x1a]), pack("<f", v)]);
            parts.push(nameToken(checksum("Shatter")), nameValue("name", piece));
            parts.push(integer("area", 5000), real("variance", 1.0), real("spread", 1.5), integer("life", 3));
            parts.push(integer("bounce", 0), real("scale", 0.4), integer("vel_x", 0), integer("vel_y", 60));
            parts.push(integer("vel_z", 0), Buffer.from([LINE]));
            parts.push(nameToken(checksum("Kill")), nameValue("name", piece), Buffer.from([LINE]));
            break;
        }
        case "camera":
            // LevelEditor_PlayCamera camera=<node> time=<seconds> (the DLL's level_editor_cameras.h: the game's
            // virtual_cam through PlaySkaterCamAnim - a baked level needs this project's DLL for it)
            parts.push(nameToken(checksum("LevelEditor_PlayCamera")), nameValue("camera", checksumOf(spec.target)));
            parts.push(
                nameToken(checksum("time")),
                Buffer.from([EQUALS, 0x1a]),
                pack("<f", clampSeconds(spec.seconds)),
            );
            parts.push(Buffer.from([LINE]));
            break;
    }
    return Buffer.concat(parts);
}

// The names the DLL's QbFile writes for a spec's script (the script's, its target / script / gap when it has a body);
// `flags`: the gap's flags too (for a level file's name table - not in the DLL's).
export function scriptNames(spec: TriggerSpec, flags = false): Map<number, string> {
    const out = new Map<number, string>();
    const note = (name: string): void => {
        if (name && hexForm(name) === null && !out.has(checksum(name))) out.set(checksum(name), name);
    };
    note(scriptName(spec));
    if (body(spec).length) {
        if (spec.kind === "kill" || spec.kind === "teleport") note(spec.target);
        if (spec.kind === "script") note(spec.script);
        if (spec.kind === "gapstart" || spec.kind === "gapend") note(spec.gap);
        if (spec.kind === "break") {
            note(spec.target);
            note(spec.sound);
        }
        if (spec.kind === "camera") {
            note("LevelEditor_PlayCamera");
            note(spec.target);
        }
        if (flags && spec.kind === "gapstart") for (const f of spec.flags) note(f);
    }
    return out;
}

// Triggers::QbFile: every spec's `script <name>Script ... endscript` (23 16 <name> 01 <body> 24 01), the names
// (2B <checksum> <text> 00), the end (00).
export function qbFile(specs: readonly TriggerSpec[]): Buffer {
    const parts: Buffer[] = [];
    const names = new Map<number, string>();
    for (const spec of specs) {
        parts.push(Buffer.from([0x23]), nameToken(checksum(scriptName(spec))), Buffer.from([LINE]));
        parts.push(body(spec), Buffer.from([0x24, LINE]));
        for (const [c, n] of scriptNames(spec)) if (!names.has(c)) names.set(c, n);
    }
    for (const [c, n] of names) parts.push(Buffer.from([0x2b]), pack("<I", c), Buffer.from(`${n}\0`, "latin1"));
    parts.push(Buffer.from([0x00]));
    return Buffer.concat(parts);
}

// --- the node and the box ---------------------------------------------------------------------------------------------

// The node the DLL makes in its overlay (Triggers::NodeSpec through NodeCreate::Fields), as a node array entry.
export function triggerNode(spec: TriggerSpec): Struct {
    const v = (x: number, y: number, z: number): Vector => new Vector("vector", [x, y, z]);
    const n = (text: string): Name => new Name(checksum(text));
    return new Struct([
        [n("Pos"), v(spec.pos.x, spec.pos.y, spec.pos.z)],
        [n("Angles"), v(0, spec.yaw, 0)],
        [n("Name"), n(spec.name)],
        [n("Class"), n("GenericNode")],
        [n("Type"), n("ed_trigger")],
        [n("TriggerScript"), n(scriptName(spec))],
    ]);
}
export const NODE_NAMES = ["Pos", "Angles", "Name", "Class", "GenericNode", "Type", "ed_trigger", "TriggerScript"];

// TurnAboutUp (level_editor.cpp): `v` turned by `yaw` about the world's up axis.
export function turnAboutUp(v: V3, yaw: number): V3 {
    const c = Math.cos(yaw);
    const s = Math.sin(yaw);
    return { x: v.x * c + v.z * s, y: v.y, z: -v.x * s + v.z * c };
}

// The box's corners (Triggers::BoxOf: centred on pos, turned by yaw), in THUG2's own trigger boxes' order: the bottom
// (x-, z+) (x-, z-) (x+, z-) (x+, z+), the top (x-, z+) (x+, z+) (x+, z-) (x-, z-) - and their 12 triangles.
export function boxCorners(spec: Pick<TriggerSpec, "pos" | "yaw" | "size">): V3[] {
    const h = { x: spec.size.x / 2, y: spec.size.y / 2, z: spec.size.z / 2 };
    const local: [number, number, number][] = [
        [-1, -1, 1],
        [-1, -1, -1],
        [1, -1, -1],
        [1, -1, 1],
        [-1, 1, 1],
        [1, 1, 1],
        [1, 1, -1],
        [-1, 1, -1],
    ];
    return local.map(([a, b, c]) => {
        const t = turnAboutUp({ x: a * h.x, y: b * h.y, z: c * h.z }, spec.yaw);
        return { x: f32(spec.pos.x + t.x), y: f32(spec.pos.y + t.y), z: f32(spec.pos.z + t.z) };
    });
}
export const BOX_FACES: readonly [number, number, number][] = [
    [0, 1, 2],
    [2, 3, 0],
    [4, 5, 6],
    [6, 7, 4],
    [0, 3, 5],
    [5, 4, 0],
    [3, 2, 6],
    [6, 5, 3],
    [2, 1, 7],
    [7, 6, 2],
    [1, 0, 4],
    [4, 7, 1],
];

// The trigger's collision object: the box's 12 faces, flags FACE_FLAGS, terrain 0, intensities 128 (as THUG2's own),
// fixed-point corners while the box fits 16-bit sixteenths (as the shipped boxes), its tree and reaches built. An exit
// trigger's has the 12 faces again wound the other way (facing in: met on the way out - see the top).
export function triggerCollision(spec: TriggerSpec): ColObject {
    const corners = boxCorners(spec);
    const min = {
        x: Math.min(...corners.map((c) => c.x)),
        y: Math.min(...corners.map((c) => c.y)),
        z: Math.min(...corners.map((c) => c.z)),
    };
    const max = {
        x: Math.max(...corners.map((c) => c.x)),
        y: Math.max(...corners.map((c) => c.y)),
        z: Math.max(...corners.map((c) => c.z)),
    };
    const fixed = Math.max(max.x - min.x, max.y - min.y, max.z - min.z) * 16 <= 65535;
    const verts = corners.map((c) => [c.x, c.y, c.z]);
    const faces: Face[] = BOX_FACES.map(([a, b, c]) => [FACE_FLAGS, 0, a, b, c]);
    if (spec.exit) faces.push(...BOX_FACES.map(([a, b, c]): Face => [FACE_FLAGS, 0, a, c, b]));
    const o: ColObject = {
        checksum: checksum(spec.name),
        flags: 0,
        smallFaces: 1,
        fixedVerts: fixed ? 1 : 0,
        bbox: [min.x, min.y, min.z, 1, max.x, max.y, max.z, 1],
        verts,
        intensities: Buffer.alloc(8, 0x80),
        faces,
        reach: Buffer.alloc(0),
        bsp: { kind: "leaf", faces: [], low: 3 },
    };
    o.bsp = buildTree(o.verts, o.faces);
    setReaches(o);
    return o;
}

// The other end of a gap (Triggers::Partner): the index in `specs` of a trigger with the same GapID (case doesn't
// matter) and the other end's kind; -1 none.
export function partner(specs: readonly TriggerSpec[], index: number): number {
    const s = specs[index];
    if (!s || (s.kind !== "gapstart" && s.kind !== "gapend") || !s.gap) return -1;
    const other = s.kind === "gapstart" ? "gapend" : "gapstart";
    return specs.findIndex((t, i) => i !== index && t.kind === other && t.gap.toLowerCase() === s.gap.toLowerCase());
}
