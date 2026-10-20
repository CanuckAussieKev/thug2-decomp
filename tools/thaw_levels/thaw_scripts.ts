// NOT ORIGINAL - a THAW level's scripts, moving objects, peds and cars in THUG2's free skate (tools/thaw_levels).
// Nothing of either game is stored: everything is read from the user's files each time.
//
// Scripts: THAW's compiled scripts are THUG2's token stream (the same if / else / random / switch tokens) with one more
// token, 0x4A: an inline structure in THAW's item format - a call's arguments, mostly. They are read here into
// tools/level_port/script_port.ts's form (ThawSource: if / else as its 0x25 / 0x26, jumps as target indices, the
// randoms' own weights, switch short jumps, the inline structures as THUG2's { } tokens - a call's own one as plain
// arguments) and brought along by its Translator: from the level's node scripts down through everything they call,
// statement by statement left out where THUG2 hasn't the command (or the member function for the object running it,
// or the node). THAW's game scripts are brought along only from GLOBAL_SOURCES (the gap scripts); everything else
// THAW-only (goals, the story, its ped AI, cameras) is left out.
//
// Nodes (made THUG2's, kept whole by nodes.convertNodes - see PortedNodes):
//   LevelObject   THAW's moving / bouncy objects: THUG2 has the same class with the same keys (BE's bouncy trash
//                 cans: bouncy, contacts, coeff_...). Their sectors and collision are in their own space (centred
//                 on the origin) - left where they are when a level is recentred (local_names).
//   GameObject    the script objects (Type=Ghost, model="none") with their trigger scripts; the ones with THAW models
//                 (goal letters, chairs...) are left out (THAW's .mdl isn't converted).
//   Waypoint      kept (the paths scripts and cars follow); the ones peds walk as THUG2's ped AI waypoints.
//   Pedestrian    THAW's ped AI and models don't run in THUG2: THUG2's ped AI at the same place (walking the
//                 waypoints the ped links to, else standing) with a THUG2 profile like THAW's - the peds there in
//                 free skate (CreatedAtStart, or created from NODEFLAG_PERMANENT_PEDS); goal / story peds not.
//   LifeNode      THAW's ambient ped spots (ProfileName, Num_Objects): a standing THUG2 ped each (at most
//                 MAX_LIFE_PEDS per level).
//   Vehicle       a car whose script follows its path (Car_Generic01 / Obj_FollowPathLinked): THUG2's Car_Generic01
//                 on the same waypoints; other cars there at the start parked. THUG2's model of the name, else
//                 peds_cars.FALLBACK_CAR.

import * as fs from "node:fs";
import { FormatError } from "../level_port/binio.ts";
import * as nodes from "../level_port/nodes.ts";
import * as pedsCars from "../level_port/peds_cars.ts";
import * as qb from "../level_port/qb.ts";
import { checksum, type Entry, Name, type QbValue, Struct, Vector } from "../level_port/qb.ts";
import * as scriptPort from "../level_port/script_port.ts";
import { type Context, type ObjectContext, Tok } from "../level_port/script_port.ts";
import { encodeLatin1, Float, hex8, pyRepr } from "../lib/py.ts";
import { pack, unpackNumbers } from "../lib/struct.ts";
import * as thaw from "./thaw.ts";

// THAW's game script files brought along (besides the level's own): the gap helpers (Gap_Gen_Rail...).
export const GLOBAL_SOURCES = ["scripts\\game\\misc\\gap_scripts.qb"];
export const LEVEL = "level";
export const MAX_LIFE_PEDS = 24;

// The made nodes' trigger scripts (THUG2's own ped AI and cars), in every converted level's .qb.
export const PED_WALK = "thaw_ped_walk";
export const PED_STAND = "thaw_ped_stand";
export const CAR_DRIVE = "thaw_car_drive";
export const MADE_SCRIPTS =
    `script ${PED_WALK} {\n    Ped_InitAI \n}\nscript ${PED_STAND} {\n    Ped_InitAI stand \n}\n` +
    `script ${CAR_DRIVE} {\n    Car_Generic01 \n}\n`;

// THAW's rail speed-up (skutils.qb's RailAccelerate, the trigger script of uphill rails: while the skater grinds the
// rail and climbs, 5% more speed a frame). THAW's checks the trigger type first (`if TriggerType {land onto}`): THUG2's
// TriggerType with GetTriggerArray's numbers for those flags (script_port.triggerFlags). THAW's spawns the loop through
// the skater's id (`<objid>:Obj_SpawnScriptLater`); a rail's trigger script runs on the skater here, so
// Obj_SpawnScript. (Written from THAW's own script; THUGPRO has one of the same shape in its thugpro_utils.q.)
export const RAIL_ACCELERATE = "RailAccelerate";
const LAND_ONTO = scriptPort.triggerFlags(["land", "onto"]) as number[];
export const RAIL_SCRIPTS = `script RailAccelerate {
    if TriggerType [${LAND_ONTO.join(", ")}] {
        KillSpawnedScript name=RailAccelerate2
        Obj_SpawnScript RailAccelerate2
    }
}
script RailAccelerate2 {rate=0.05
    OldPos = 0
    while {
        if OnRail {
            Obj_GetPosition
            NewPos = (<pos>.(0.0, 1.0, 0.0))
            if (<NewPos> > <OldPos>) {
                GetSpeed
                SetSpeed (<speed> * (1.0 + <rate>))
            }
            OldPos = <NewPos>
            wait 1 gameframe
        } else {
            break
        }
    }
}
`;

// The game-mode flags (capture the flag, the lobby's team flags): THAW's are THUG2's own game objects (Type Flag_Red /
// Flag_Red_Base..., gameobjects\flags\ models THUG2 has, a trigger script calling THUG2's Team_Flag) - kept, so that
// THUG2's net\ctf.qb and SK4_Scripts' Create_Team_Flags / Team_Flag_Trigger find them by name (TRG_CTF_Red... -
// thaw_levels.multiplayerNames takes the zone prefix off).
const FLAG_MODELS = "gameobjects\\flags\\";
const FLAG_KEYS = new Set(["Type", "model", "lod_dist1", "lod_dist2"].map(checksum));

const u32 = (b: Uint8Array, at = 0): number => unpackNumbers("<I", b, at)[0]!;
const i32 = (b: Uint8Array, at = 0): number => unpackNumbers("<i", b, at)[0]!;

// --- scripts ------------------------------------------------------------------------------------------------------

// A value of an item-format structure as THUG2 tokens.
function valueToks(value: QbValue, i: number): Tok[] {
    const v: unknown = value;
    if (v instanceof Name || v instanceof thaw.Ref) return [new Tok(0x16, pack("<I", v.value), i)];
    if (typeof v === "boolean" || (typeof v === "number" && Number.isInteger(v)))
        return [new Tok(0x17, pack("<i", Number(v)), i)];
    if (v instanceof Float) return [new Tok(0x1a, pack("<f", v.value), i)];
    if (typeof v === "string") return [new Tok(0x1b, Buffer.concat([encodeLatin1(v), Buffer.from([0])]), i)];
    if (v instanceof qb.Local) return [new Tok(0x1c, Buffer.concat([encodeLatin1(v.text), Buffer.from([0])]), i)];
    if (v instanceof Vector) {
        return [new Tok(v.kind === "vector" ? 0x1e : 0x1f, pack(`<${v.values.length}f`, ...v.values), i)];
    }
    if (v instanceof Struct)
        return [new Tok(0x03, Buffer.alloc(0), i), ...entriesToks(v, i), new Tok(0x04, Buffer.alloc(0), i)];
    if (Array.isArray(v)) {
        const out = [new Tok(0x05, Buffer.alloc(0), i)];
        for (const x of v as QbValue[]) out.push(...valueToks(x, i));
        out.push(new Tok(0x06, Buffer.alloc(0), i));
        return out;
    }
    throw new FormatError(`inline structure value ${String(v)}`);
}

function entriesToks(s: Struct, i: number): Tok[] {
    const out: Tok[] = [];
    for (const [key, value] of s.entries) {
        if (key !== null) out.push(new Tok(0x16, pack("<I", key.value), i), new Tok(0x07, Buffer.alloc(0), i));
        out.push(...valueToks(value, i));
    }
    return out;
}

// THAW's commands THUG2 has under another name (its "later" / "now" spawns are THUG2's spawn; ShatterFromSkater is
// THUG2's Shatter, without the skater's speed).
const RENAME_TEXT: [string, string][] = [
    ["SpawnScriptLater", "SpawnScript"],
    ["SpawnScriptNow", "SpawnScript"],
    ["Obj_SpawnScriptLater", "Obj_SpawnScript"],
    ["Obj_SpawnScriptNow", "Obj_SpawnScript"],
    ["ShatterFromSkater", "Shatter"],
];
const RENAMES = new Map(RENAME_TEXT.map(([a, b]) => [checksum(a), checksum(b)]));

function setDefault(names: Map<number, string>, value: number, text: string): void {
    if (!names.has(value)) names.set(value, text);
}

const IN_MANUAL = checksum("InManual");
const SKATER_NAME = checksum("Skater");
const NAMES_MADE = ["GetSingleTag", "state", "Skater_InManual", "IsAlive"];

// THAW's InManual (`if [not] [skater:]InManual`), a skater member THUG2 hasn't: THUG2 keeps the skater's state in its
// tags - its own ShatterAndDie asks `GetSingleTag state` and compares `<state> = Skater_InManual`. The `if` (THAW
// tokens from k: 0x47, then [0x39] [skater 0x42] InManual and the line's end) as `[skater:]GetSingleTag state` and
// `if [not] (<state> = Skater_InManual)`; returns the index of the line's end, or null when the condition isn't that.
function inManual(toks: readonly qb.Token[], k: number, out: Tok[]): number | null {
    let j = k + 1;
    const not = toks[j]?.[1] === 0x39;
    if (not) j += 1;
    let prefix: number | null = null;
    if (toks[j]?.[1] === 0x16 && u32(toks[j]![2]) === SKATER_NAME && toks[j + 1]?.[1] === 0x42) {
        prefix = SKATER_NAME;
        j += 2;
    }
    const name = toks[j];
    const end = toks[j + 1];
    if (name === undefined || name[1] !== 0x16 || u32(name[2]) !== IN_MANUAL) return null;
    if (end === undefined || !scriptPort.EOL.includes(end[1])) return null;
    const none = Buffer.alloc(0);
    const n = (text: string, i: number): Tok => scriptPort.nameTok(checksum(text), i);
    if (prefix !== null) out.push(scriptPort.nameTok(prefix, k), new Tok(0x42, none, k));
    out.push(n("GetSingleTag", k), n("state", k), new Tok(0x01, none, k), new Tok(0x25, none, k));
    if (not) out.push(new Tok(0x39, none, k));
    out.push(
        new Tok(0x0e, none, j),
        new Tok(0x2d, none, j),
        n("state", j),
        new Tok(0x07, none, j),
        n("Skater_InManual", j),
        new Tok(0x0f, none, j),
    );
    return j + 1;
}

// A THAW script body as script_port's token list (Tok.i: the index of the THAW token it comes from).
export function scriptToks(body: Uint8Array, names: Map<number, string>, label = "script"): Tok[] {
    for (const [, renamed] of RENAME_TEXT) setDefault(names, checksum(renamed), renamed);
    for (const text of NAMES_MADE) setDefault(names, checksum(text), text);
    const toks: qb.Token[] = [...thaw.tokens(body)].map(([off, t, p]) => {
        const renamed = t === 0x16 ? RENAMES.get(u32(p)) : undefined;
        return [off, t, renamed !== undefined ? pack("<I", renamed) : p];
    });
    const at = new Map<number, number>();
    toks.forEach(([off], k) => {
        at.set(off, k);
    });
    const target = (pos: number): number => {
        const k = at.get(pos);
        if (k === undefined)
            throw new FormatError(`${label}: a jump to 0x${pos.toString(16).toUpperCase()} lands inside a token`);
        return k;
    };
    const out: Tok[] = [];
    let depth = 0;
    for (let k = 0; k < toks.length; k++) {
        const [off, t, p] = toks[k]!;
        if (t === 0x24) break;
        if (t === 0x47) {
            const end = inManual(toks, k, out);
            if (end !== null) {
                k = end - 1; // (on with the line's end)
                continue;
            }
            out.push(new Tok(0x25, Buffer.alloc(0), k));
        } else if (t === 0x48) {
            out.push(new Tok(0x26, Buffer.alloc(0), k));
        } else if (t === 0x2e) {
            out.push(new Tok(0x2e, p, k, [target(off + 5 + i32(p))]));
        } else if (scriptPort.RANDOMS.includes(t)) {
            const n = u32(p);
            const weights = unpackNumbers(`<${n}H`, p, 4);
            const targets: number[] = [];
            for (let m = 0; m < n; m++) {
                const field = off + 1 + 4 + 2 * n + 4 * m;
                targets.push(target(field + 4 + i32(p, 4 + 2 * n + 4 * m)));
            }
            out.push(new Tok(t, p, k, targets, weights));
        } else if (t === 0x49) {
            const pos = off + unpackNumbers("<H", p)[0]!;
            const there = at.get(pos);
            if (there !== undefined) out.push(new Tok(0x49, p, k, [there]));
            else out.push(new Tok(0x49, p, k, [target(pos + 1)], null, 1));
        } else if (t === 0x4a) {
            const s = thaw.inlineStruct(p, names);
            const previous = k ? toks[k - 1]![1] : null;
            if (depth === 0 && previous !== 0x07) {
                // (a call's arguments: as plain arguments, so that the translator sees the names and scripts in them)
                out.push(...entriesToks(s, k));
            } else {
                out.push(...valueToks(s, k));
            }
        } else {
            out.push(new Tok(t, p, k));
        }
        if (t === 0x03 || t === 0x05 || t === 0x0e) depth += 1;
        else if (t === 0x04 || t === 0x06 || t === 0x0f) depth -= 1;
    }
    return out;
}

// A global's value with THAW's $references as names (NeverScript has no form for them).
export function plain(value: QbValue): QbValue {
    const v: unknown = value;
    if (v instanceof thaw.Ref) return new Name(v.value);
    if (v instanceof Struct) return new Struct(v.entries.map(([k, x]) => [k, plain(x)]));
    if (Array.isArray(v)) return (v as QbValue[]).map(plain);
    return value;
}

// A THAW qb file (thaw.QbItems) as a script_port.Source: its scripts as token lists, its globals.
export class ThawSource {
    readonly label: string;
    readonly names = new Map<number, string>();
    readonly scripts = new Map<number, Tok[]>();
    readonly globals: Map<number, QbValue>;

    constructor(q: thaw.QbItems, label: string, names: Map<number, string>) {
        this.label = label;
        for (const [name, body] of q.scriptData) {
            this.scripts.set(name, scriptToks(body, names, `${q.name}: ${names.get(name) ?? hex8(name)}`));
        }
        this.globals = new Map(q.globals.map(([key, value]) => [key.value, plain(value)]));
    }
}

// THAW's game script files (data\pak\qb.pak.wpc) whose scripts may be brought along (GLOBAL_SOURCES).
export function globalSources(qbPak: string, names: Map<number, string>): ThawSource[] {
    const out: ThawSource[] = [];
    for (const [entry, data] of thaw.pakFiles(qbPak)) {
        if (!entry.path) continue;
        const label = entry.path.toLowerCase().replaceAll(".wpc", "");
        if (GLOBAL_SOURCES.includes(label))
            out.push(new ThawSource(new thaw.QbItems(data, entry.path, names), label, names));
    }
    return out;
}

// --- nodes --------------------------------------------------------------------------------------------------------

function key(names: Map<number, string>, text: string): Name {
    const value = checksum(text);
    setDefault(names, value, text);
    return new Name(value);
}

// A LevelObject's keys and flags THUG2's own LevelObjects have (BE's bouncy objects, LA's movers).
const OBJECT_KEYS = new Set(
    [
        "Pos",
        "Angles",
        "Name",
        "Class",
        "Type",
        "CollisionMode",
        "SuspendDistance",
        "lod_dist1",
        "lod_dist2",
        "contacts",
        "center_of_mass",
        "coeff_restitution",
        "coeff_friction",
        "skater_collision_impulse_factor",
        "skater_collision_rotation_factor",
        "skater_collision_assent",
        "skater_collision_radius",
    ].map(checksum),
);
const OBJECT_FLAGS = new Set(["CreatedAtStart", "AbsentInNetGames", "bouncy", "DieUponSettling"].map(checksum));

const FEMALE = ["fem", "girl", "chick", "woman", "lady", "supermodel", "mindy", "slut", "hooker", "mom"];
const PROFILES: [string, string][] = [
    ["skater", "Ped_Skateboard_a"],
    ["sk8", "Ped_Skateboard_a"],
    ["security", "Ped_Security_Guard_01"],
    ["guard", "Ped_Security_Guard_01"],
    ["constr", "Ped_Construction_Worker_01"],
    ["worker", "Ped_Construction_Worker_01"],
    ["graf", "Ped_graffiti02"],
    ["tagger", "Ped_graffiti02"],
    ["photo", "Ped_Photo"],
    ["camera", "Ped_CameraMan"],
    ["nurse", "Ped_Nurse_01"],
    ["paramedic", "Ped_Paramedic_01"],
    ["bum", "Ped_germandrunk"],
    ["hobo", "Ped_germandrunk"],
    ["drunk", "Ped_germandrunk"],
    ["hippie", "Ped_guitarist"],
    ["cop", "Ped_germancop"],
    ["police", "Ped_germancop"],
    ["soldier", "Ped_Soldier_01"],
    ["zombie", "Ped_zombiemale01"],
];

// A THUG2 ped profile like THAW's (its profile / type / life node profile names): by the words in them.
export function profileFor(texts: readonly string[], assets: pedsCars.Thug2Assets | null): string {
    const text = texts
        .filter((t) => t)
        .map((t) => t.toLowerCase())
        .join(" ");
    const candidates: string[] = [];
    if (FEMALE.some((w) => text.includes(w))) candidates.push("random_female_profile");
    for (const [word, profile] of PROFILES) if (text.includes(word)) candidates.push(profile);
    for (const c of candidates) if (assets === null || assets.profileExists(c)) return c;
    return "random_male_profile";
}

// THAW particle textures THUG2 hasn't, and the THUG2 one used instead.
const PARTICLE_STAND_INS = new Map([
    ["apm_smokepuff_rot_01", "BA_APM_smokePuff01"],
    ["apm_flame_rot01", "apm_flame_01"],
]);

// THUG2's particle textures (Data\images\particles\<name>.img.xbx): name lower case -> name.
export function particleTextures(thug2: string): Map<string, string> {
    const folder = `${thug2}\\Data\\images\\particles`;
    const out = new Map<string, string>();
    if (fs.existsSync(folder) && fs.statSync(folder).isDirectory()) {
        for (const entry of fs.readdirSync(folder)) {
            if (entry.toLowerCase().endsWith(".img.xbx")) {
                const name = entry.slice(0, -".img.xbx".length);
                out.set(name.toLowerCase(), name);
            }
        }
    }
    return out;
}

// THAW's packed particle colour (r << 24 | g << 16 | b << 8 | a) as [[r, g, b], a].
export function rgba(value: QbValue): [number[], number] {
    if (typeof value !== "number" || !Number.isInteger(value)) throw new TypeError("a particle colour isn't an int");
    const v = value >>> 0;
    return [[v >>> 24, (v >>> 16) & 0xff, (v >>> 8) & 0xff], v & 0xff];
}

// THUG2's load_level runs a level's LoadAllParticleTextures (when it has one) before its nodes.
export function loadParticleTexturesNs(textures: ReadonlySet<string>): string {
    if (!textures.size) return "";
    const lines = [...textures]
        .sort()
        .map((t) => `    LoadParticleTexture "particles\\\\${t}" \n`)
        .join("");
    return `script LoadAllParticleTextures {\n${lines}}\n`;
}

// list[k], raising past the end as Python's (a pair where a vector was expected)
function element(values: readonly number[], k: number): number {
    const v = values[k];
    if (v === undefined) throw new RangeError("tuple index out of range");
    return v;
}

const isInt = (v: QbValue | undefined): v is number => typeof v === "number" && Number.isInteger(v);

// A THAW particle box's keys -> THUG2's (LA's falling rocks): THAW keeps the start / mid / end values in arrays
// (BoxDims, BoxPositions, Radii, RadiusSpread, BoxRGBAs), THUG2 in keys of their own.
const PARTICLE_KEYS = new Set(
    [
        "Type",
        "BlendMode",
        "FixedAlpha",
        "AlphaCutoff",
        "MaxStreams",
        "SuspendDistance",
        "lod_dist1",
        "lod_dist2",
        "EmitRate",
        "Lifetime",
        "MidPointPCT",
        "ColorMidTime",
    ].map(checksum),
);
const PARTICLE_FLAGS = new Set(["CreatedAtStart", "AbsentInNetGames", "UseMidPoint", "UseColorMidTime"].map(checksum));

// A made node's trigger: [node name, script, context].
export type Trigger = [number, number, Context];

// THAW's game objects, waypoints, peds, life nodes and cars made THUG2's nodes (see the top), in place in the array
// (the Links stay indices into it; nodes.convertNodes renumbers them).
export class PortedNodes {
    readonly array: readonly QbValue[];
    readonly names: Map<number, string>;
    readonly particles: Map<string, string>; // THUG2's particle textures (particleTextures)
    readonly shift: readonly number[]; // (the particles' mid / end positions: Pos is moved later)
    readonly particleTextures = new Set<string>(); // the ones the made particle objects use
    readonly sectors: ReadonlySet<number>;
    readonly colObjects: ReadonlySet<number>;
    readonly scriptQbs: readonly thaw.QbItems[];
    readonly thug2: scriptPort.Thug2;
    readonly assets: pedsCars.Thug2Assets | null;
    readonly ids = new Set<Struct>();
    readonly triggers: Trigger[] = [];
    readonly scripts = new Set<number>();
    readonly local = new Set<number>(); // the LevelObjects' names: their sectors and collision in their own space
    readonly objectContexts = new Map<number, ObjectContext>(); // the made objects scripts may address (`name:member`)
    readonly gone = new Set<number>(); // LevelObjects not made (no sector): their collision left out too
    readonly counts = new Map<string, number>();
    lifePeds = 0;
    readonly pedPaths = new Set<number>();
    // TPN-THAW-4: THUGPRO's car scripts (not THAW's, so not in scriptQbs) that drive the car along its path
    readonly drivers = new Set<number>();
    private indices: Map<QbValue, number> | null = null;

    constructor(
        array: readonly QbValue[],
        names: Map<number, string>,
        sectors: ReadonlySet<number>,
        colObjects: ReadonlySet<number>,
        scriptQbs: readonly thaw.QbItems[],
        thug2: scriptPort.Thug2,
        assets: pedsCars.Thug2Assets | null,
        particles: Map<string, string> | null = null,
        shift: readonly number[] = [0.0, 0.0, 0.0],
    ) {
        this.array = array;
        this.names = names;
        this.particles = particles ?? new Map();
        this.shift = shift;
        this.sectors = sectors;
        this.colObjects = colObjects;
        this.scriptQbs = scriptQbs;
        this.thug2 = thug2;
        this.assets = assets;
        for (const node of array) {
            if (node instanceof Struct && nodes.classOf(node, names) === "pedestrian" && this.wantedPed(node)) {
                for (const link of this.waypointLinks(node) ?? []) {
                    for (const i of pedsCars.follow(array, link, names)) this.pedPaths.add(i);
                }
            }
        }
    }

    private count(what: string): void {
        this.counts.set(what, (this.counts.get(what) ?? 0) + 1);
    }

    private waypointLinks(node: Struct): number[] | null {
        const links = node.get("Links");
        if (!Array.isArray(links)) return null;
        const out = links.filter(
            (v): v is number =>
                isInt(v) &&
                v >= 0 &&
                v < this.array.length &&
                this.array[v] instanceof Struct &&
                nodes.classOf(this.array[v] as Struct, this.names) === "waypoint",
        );
        const unique = [...new Set(out)];
        return unique.length ? unique : null;
    }

    private text(value: QbValue | undefined): string {
        return value instanceof Name ? (this.names.get(value.value) ?? "") : "";
    }

    private base(node: Struct, cls: string): Entry[] {
        const n = this.names;
        const angles = node.get("Angles");
        return [
            [key(n, "Pos"), node.get("Pos")!],
            [key(n, "Angles"), angles !== undefined && truthy(angles) ? angles : new Vector("vector", [0.0, 0.0, 0.0])],
            [key(n, "Name"), node.get("Name")!],
            [key(n, "Class"), key(n, cls)],
        ];
    }

    private flags(node: Struct, ...flags: string[]): Entry[] {
        return flags.filter((f) => node.hasFlag(f)).map((f) => [null, key(this.names, f)]);
    }

    private made(entries: Entry[], node: Struct | null = null, context: Context | null = null): Struct {
        const s = new Struct(entries);
        this.ids.add(s);
        const script = node !== null ? node.get("TriggerScript") : undefined;
        const name = node?.get("Name");
        if (context !== null && script instanceof Name && name instanceof Name) {
            this.triggers.push([name.value, script.value, context]);
            this.scripts.add(script.value);
        }
        return s;
    }

    private leftOut(what: string): Struct {
        this.count(what);
        return new Struct([[new Name(checksum("Class")), new Name(checksum("LeftOut"))]]);
    }

    private wantedPed(node: Struct): boolean {
        const created = node.get("CreatedFromVariable");
        if (
            !(
                node.hasFlag("CreatedAtStart") ||
                (created instanceof Name && this.text(created).toLowerCase() === "nodeflag_permanent_peds")
            )
        ) {
            return false;
        }
        const words = `${this.text(node.get("Type"))} ${this.text(node.get("profile"))}`.toLowerCase();
        return !words.includes("anl_") && !words.includes("goal");
    }

    // The node made THUG2's, a LeftOut placeholder, or null (left to nodes.convertNodes).
    convert(node: Struct): Struct | null {
        const n = this.names;
        const cls = nodes.classOf(node, n);
        const name = node.get("Name");
        if (
            !["levelobject", "gameobject", "waypoint", "pedestrian", "lifenode", "vehicle", "particleobject"].includes(
                cls,
            )
        ) {
            return null;
        }
        if (!(name instanceof Name) || !(node.get("Pos") instanceof Vector)) {
            return this.leftOut(`${cls} nodes without a name or Pos`);
        }
        if (cls === "levelobject") return this.levelObject(node);
        if (cls === "gameobject") {
            const model = node.get("model");
            if (
                typeof model === "string" &&
                model.toLowerCase().startsWith(FLAG_MODELS) &&
                node.get("profile") === undefined &&
                (this.assets === null || this.assets.modelExists(model))
            ) {
                this.count("game-mode flags");
                return this.made(
                    [
                        ...this.base(node, "GameObject"),
                        ...node.entries.filter(([k]) => k !== null && FLAG_KEYS.has(k.value)),
                        ...this.flags(node, "CreatedAtStart", "AbsentInNetGames", "NeverSuspend"),
                    ],
                    node,
                    scriptPort.OBJECT,
                );
            }
            if (node.get("model") !== "none" || node.get("profile") !== undefined) {
                // (a model, or a profile naming one: THAW's bouncy barricades, chairs...)
                return this.leftOut("game objects with THAW models left out");
            }
            this.count("script objects");
            return this.made(
                [
                    ...this.base(node, "GameObject"),
                    [key(n, "Type"), key(n, "Ghost")],
                    ...this.flags(node, "CreatedAtStart", "AbsentInNetGames"),
                    [null, key(n, "NeverSuspend")],
                    [key(n, "model"), "none"],
                    [key(n, "lod_dist1"), 400],
                    [key(n, "lod_dist2"), 401],
                    ...this.links(node),
                ],
                node,
                scriptPort.OBJECT,
            );
        }
        if (cls === "waypoint") return this.waypoint(node);
        if (cls === "pedestrian") return this.pedestrian(node);
        if (cls === "lifenode") return this.lifeNode(node);
        if (cls === "particleobject") return this.particle(node);
        return this.vehicle(node);
    }

    particle(node: Struct): Struct {
        const n = this.names;
        if (!node.hasFlag("CreatedAtStart")) {
            // (Kyoto's neon sparks, made by its scripts: the level crashed loading with them - in ParseQB / the node
            // array's load, writing through a null; the ones there at the start load fine, as in The Ruins)
            return this.leftOut("particle boxes not there at the start left out");
        }
        const wanted = this.text(node.get("Texture")).toLowerCase();
        // (Python's `a or b`: an empty name counts as none found)
        const texture =
            this.particles.get(wanted) || this.particles.get((PARTICLE_STAND_INS.get(wanted) ?? "").toLowerCase());
        const dims = node.get("BoxDims");
        const positions = node.get("BoxPositions");
        const radii = node.get("Radii");
        const spread = node.get("RadiusSpread");
        const colours = node.get("BoxRGBAs");
        const list3 = (v: QbValue | undefined): v is QbValue[] => Array.isArray(v) && v.length === 3;
        if (
            texture === undefined ||
            !list3(dims) ||
            !list3(positions) ||
            !list3(colours) ||
            !(radii instanceof Vector) ||
            !(spread instanceof Vector)
        ) {
            return this.leftOut("particle boxes left out (no THUG2 texture)");
        }
        this.particleTextures.add(texture);
        const moved = positions.map((p) => {
            if (!(p instanceof Vector)) throw new TypeError("a particle box position isn't a vector");
            return new Vector(
                "vector",
                [0, 1, 2].map((k) => p.values[k]! + this.shift[k]!),
            );
        });
        const entries = this.base(node, "ParticleObject");
        for (const [k, value] of node.entries) {
            if (k === null) {
                if (value instanceof Name && PARTICLE_FLAGS.has(value.value)) entries.push([k, value]);
            } else if (PARTICLE_KEYS.has(k.value)) {
                entries.push([k, value]);
            }
        }
        entries.push([key(n, "Texture"), key(n, texture)]);
        ["Start", "Mid", "End"].forEach((part, k) => {
            entries.push([key(n, `BoxDims${part}`), dims[k]!]);
            if (k) entries.push([key(n, `${part}Position`), moved[k]!]);
            entries.push([key(n, `${part}Radius`), new Float(element(radii.values, k))]);
            entries.push([key(n, `${part}RadiusSpread`), new Float(element(spread.values, k))]);
            const [rgb, alpha] = rgba(colours[k]!);
            entries.push([key(n, `${part}RGB`), rgb], [key(n, `${part}Alpha`), alpha]);
        });
        this.count("particle boxes");
        return this.made(entries);
    }

    private links(node: Struct): Entry[] {
        const links = this.waypointLinks(node);
        return links ? [[key(this.names, "Links"), links]] : [];
    }

    levelObject(node: Struct): Struct {
        const name = (node.get("Name") as Name).value;
        if (!this.sectors.has(name)) {
            // (THAW's bouncy props of the story's scene: not in the net scene)
            this.gone.add(name);
            return this.leftOut("moving objects without a sector left out");
        }
        const entries: Entry[] = [];
        const collisionMode = checksum("CollisionMode");
        for (const [k, value] of node.entries) {
            if (k === null) {
                if (value instanceof Name && OBJECT_FLAGS.has(value.value)) entries.push([k, value]);
            } else if (OBJECT_KEYS.has(k.value)) {
                if (k.value === collisionMode && !this.colObjects.has(name)) continue;
                entries.push([k, value]);
            }
        }
        entries.push(...this.links(node));
        this.local.add(name);
        // (THAW's scripts kick its bouncy boxes - The Mall's Obj_Bouncy_Box_Low01:Rigidbody_Kick - and move its
        // movers - Obj_Skidmark_Grate01:Obj_MoveToRelPos: kept, checked as run by that object)
        this.objectContexts.set(name, node.hasFlag("bouncy") ? scriptPort.BOUNCY : scriptPort.OBJECT);
        this.count(node.hasFlag("bouncy") ? "bouncy objects" : "moving objects");
        return this.made(entries, node, scriptPort.OBJECT);
    }

    waypoint(node: Struct): Struct {
        const n = this.names;
        const flags: Entry[] = node.hasFlag("CreatedAtStart") ? [[null, key(n, "CreatedAtStart")]] : [];
        let ped: Entry[] = [];
        const index = this.index(node);
        if (index !== undefined && this.pedPaths.has(index)) {
            ped = [
                [key(n, "Type"), key(n, "PedAI")],
                [null, key(n, "Continue")],
                [key(n, "PedType"), key(n, "Walk")],
                [key(n, "Priority"), key(n, "Normal")],
                [key(n, "ContinueWeight"), new Float(1.0)],
            ];
        }
        this.count("waypoints");
        return this.made([...this.base(node, "Waypoint"), ...flags, ...ped, ...this.links(node)]);
    }

    private index(node: Struct): number | undefined {
        if (this.indices === null) {
            // (the last index of an object that's there more than once, as Python's {id(x): k})
            this.indices = new Map();
            this.array.forEach((x, k) => {
                this.indices!.set(x, k);
            });
        }
        return this.indices.get(node);
    }

    private ped(node: Struct, profile: string, walking: boolean, links: number[] | null): Struct {
        const n = this.names;
        const entries: Entry[] = [
            ...this.base(node, "Pedestrian"),
            [key(n, "Type"), key(n, "Ped_From_Profile")],
            [null, key(n, "CreatedAtStart")],
            [key(n, "PedAI_Type"), key(n, "Stand")],
            [key(n, "skeletonName"), key(n, "THPS6_human")],
            [key(n, "AnimName"), key(n, "animload_THPS6_human")],
            [key(n, "profile"), key(n, profile)],
            [key(n, "SuspendDistance"), 10000],
            [key(n, "lod_dist1"), 200],
            [key(n, "lod_dist2"), 201],
            [key(n, "TriggerScript"), key(n, walking ? PED_WALK : PED_STAND)],
        ];
        if (links) entries.push([key(n, "Links"), links]);
        this.count(walking ? "walking peds" : "standing peds");
        return this.made(entries);
    }

    pedestrian(node: Struct): Struct {
        if (!this.wantedPed(node)) return this.leftOut("goal / story / animal peds left out");
        const links = this.waypointLinks(node);
        const profile = profileFor([this.text(node.get("profile")), this.text(node.get("Type"))], this.assets);
        return this.ped(node, profile, links !== null, links);
    }

    lifeNode(node: Struct): Struct {
        if (!node.hasFlag("CreatedAtStart")) return this.leftOut("life nodes not there at the start");
        if (this.lifePeds >= MAX_LIFE_PEDS) return this.leftOut(`life nodes over ${MAX_LIFE_PEDS} left out`);
        this.lifePeds += 1;
        const profile = profileFor([this.text(node.get("ProfileName")), this.text(node.get("Type"))], this.assets);
        return this.ped(node, profile, false, null);
    }

    // Does the script (or one it calls, 2 deep) drive a car along its path?
    private moves(script: number, depth = 0): boolean {
        if (this.drivers.has(script)) return true;
        for (const q of this.scriptQbs) {
            for (const [called] of q.calls(script)) {
                const text = (this.names.get(called) ?? "").toLowerCase();
                if (text === "car_generic01" || text === "obj_followpathlinked") return true;
                if (depth < 2 && called !== script && this.moves(called, depth + 1)) return true;
            }
        }
        return false;
    }

    vehicle(node: Struct): Struct {
        const n = this.names;
        if (!node.hasFlag("CreatedAtStart")) return this.leftOut("cars not there at the start left out");
        let model = node.get("model");
        if (!(typeof model === "string" && this.assets !== null && this.assets.modelExists(model))) {
            model = pedsCars.FALLBACK_CAR;
        }
        const trigger = node.get("TriggerScript");
        const links = this.waypointLinks(node);
        const driving = links !== null && trigger instanceof Name && this.moves(trigger.value);
        const entries: Entry[] = [
            ...this.base(node, "Vehicle"),
            [key(n, "Type"), key(n, "Car")],
            ...this.flags(node, "CreatedAtStart", "AbsentInNetGames"),
            [key(n, "model"), model],
            [key(n, "skeletonName"), key(n, "car")],
            [key(n, "SuspendDistance"), 10000],
            [key(n, "lod_dist1"), 400],
            [key(n, "lod_dist2"), 401],
        ];
        if (driving) entries.push([key(n, "TriggerScript"), key(n, CAR_DRIVE)], [key(n, "Links"), links]);
        this.count(driving ? "driving cars" : "parked cars");
        return this.made(entries);
    }
}

// Python's truth of a node value (`x or default`): 0, "", empty lists / structures are false.
function truthy(value: QbValue): boolean {
    if (value === 0 || value === "" || value === false) return false;
    if (Array.isArray(value)) return value.length > 0;
    if (value instanceof Float) return value.value !== 0;
    return true;
}

// --- the level's scripts ------------------------------------------------------------------------------------------

// THUG2 commands left out of THAW's scripts besides script_port.LEFT_OUT: the goal manager (THAW's goals aren't
// THUG2's: a goal it doesn't know may be read through a null), screen elements and panels (THAW's ids and
// parents), the game's global state (flags, globals, score, slow motion: a left-out statement could leave it set).
const LEFT_OUT = new Set(
    [
        "init_goal_manager",
        "play_cutscene",
        "create_panel_message",
        "create_panel_block",
        "show_panel_messages",
        "show_goal_panel_messages",
        "kill_panel_message_if_it_exists",
        "CreateScreenElement",
        "SetScreenElementLock",
        "SetScreenElementProps",
        "DestroyScreenElement",
        "RunScriptOnScreenElement",
        "SetSlomo",
        "Unpause_Balance_Meter",
        "setup_ped_speech",
        "ResetScore",
        "Change",
        "SetFlag",
        "UnSetFlag",
        "GetFlag",
        "SetGlobalFlag",
        "GetGlobalFlag",
    ].map((name) => name.toLowerCase()),
);
const LEFT_OUT_PREFIXES = ["goalmanager_", "goal_", "igc_", "playigccam", "cinematiccamera", "cutscene"];

// THAW's geometry and rail scripts run on the skater (THUG2 runs a trigger's script on the skater that set it off): a
// context of its own, with the whole skater's components (script_port's SKATER_COMPONENTS: its speed boosts' GetSpeed /
// SetSpeed, `if Walking`, TeleportSkaterToNode's animblender / input / vibration, the breakables' Obj_PlaySound...).
export const SKATER: Context = scriptPort.addContext(
    "thaw_skater",
    new Set(scriptPort.SAFE_COMPONENTS[scriptPort.SKATER]),
    scriptPort.BY_NAME[scriptPort.SKATER],
);

// (THUG2 scripts the skater runs - trigger_jump_boost, ShatterAndDie, BloodSplat, which THAW's breakables call - pass
// script_port's check by themselves now: it counts only commands, and GotParam branches only when passed.)

// THAW's IsCreated (is that node's object / sector there) as THUG2's IsAlive name=<it>: CFunc_IsAlive (0x005A5380 ->
// 0x005A4EF0 -> 0x005A3D10) looks the node up and goes by its Class - for these classes an object's composite object,
// a LevelGeometry's sector (THUG2's AP: `IsAlive name=palms_01`), a rail's; any other class is false. It reads the
// node's Class without checking the node is there (0x0045CFC0 returns null for a name the node array hasn't): only
// for the converted level's nodes of these classes - IsCreated of anything else (a parameter, a node not kept, the
// skater...) is left out as before (a condition on it false).
const IS_CREATED = checksum("IsCreated");
const IS_ALIVE_CLASSES = new Set([
    "gameobject",
    "pedestrian",
    "ped",
    "vehicle",
    "levelobject",
    "particleobject",
    "skater",
    "levelgeometry",
    "railnode",
    "climbingnode",
]);

export class Translator extends scriptPort.Translator {
    // the converted level's nodes: name -> class (lower case), for IsCreated
    nodeClasses = new Map<number, string>();

    // `IsCreated X` / `IsCreated {X}` / `IsCreated name=X` as `IsAlive name=X` (above), or null (noted).
    isCreated(toks: Tok[]): Tok[] | null {
        if (toks.some((t) => t.t === 0x2d || t.t === 0x2c)) {
            // (`IsCreated <water_node_1>`: whatever the caller passed - maybe not a node)
            this.note("IsCreated: of a parameter");
            return null;
        }
        const args = scriptPort.argsOf(toks.slice(1));
        const [key, value] = args[0] ?? [null, null];
        if (args.length !== 1 || (key !== null && key !== checksum("name")) || value?.t !== 0x16) {
            // (East LA's `IsCreated Z_EL rail_446`: two names - THAW's slip)
            this.note("IsCreated: form");
            return null;
        }
        const target = value.name()!;
        const cls = this.nodeClasses.get(target);
        if (cls === undefined || !IS_ALIVE_CLASSES.has(cls)) {
            this.note(
                cls === undefined
                    ? `IsCreated: not a converted node: ${this.text(target)}`
                    : `IsCreated: a ${cls} (IsAlive is false for it): ${this.text(target)}`,
            );
            return null;
        }
        const i = toks[0]!.i;
        return [
            scriptPort.nameTok(checksum("IsAlive"), i),
            scriptPort.nameTok(checksum("Name"), i),
            new Tok(0x07, Buffer.alloc(0), i),
            scriptPort.nameTok(target, i),
        ];
    }

    override command(toks: Tok[], contexts: ReadonlySet<Context>, discover: boolean): Tok[] | null {
        if (toks.some((t) => t.t === 0x16 && this.undrawn.has(t.name()!))) {
            // (a collision-only geometry node named anywhere - passed on to a script's Kill / Visible / Shatter
            // name=<x> too: THUG2 looks its scene sector up and reads through the null)
            this.note("geometry without a scene sector named");
            return null;
        }
        if (toks.length && toks[0]!.t === 0x16 && toks[0]!.name() === IS_CREATED) {
            const alive = this.isCreated(toks);
            if (alive === null) return null;
            toks = alive;
        }
        if (toks.length && toks[0]!.t === 0x16) {
            const text = this.text(toks[0]!.name()!).toLowerCase();
            if (LEFT_OUT.has(text) || LEFT_OUT_PREFIXES.some((p) => text.startsWith(p))) {
                this.note(`left out: ${this.text(toks[0]!.name()!)}`);
                return null;
            }
        }
        return super.command(toks, contexts, discover);
    }
}

// The scripts brought along from the kept nodes' trigger scripts: the geometry's and rails' (run by the skater), the
// made objects' (run by them). Trigger scripts that can't be are taken off their nodes. undrawn: the geometry nodes
// with no scene sector (collision-only): no statement names them (see Translator). Returns [{name: THUG2 bytes}, the
// THAW globals they use as NeverScript text, a report line, the statements left out].
export function portScripts(
    converted: readonly Struct[],
    array: readonly QbValue[],
    made: PortedNodes,
    sources: readonly ThawSource[],
    thug2: scriptPort.Thug2,
    names: Map<number, string>,
    provided: ReadonlySet<number>,
    undrawn: ReadonlySet<number> = new Set(),
): [Map<number, Buffer>, string, string, Map<string, number>] {
    const allNames = new Set<number>();
    for (const x of array) {
        const name = x instanceof Struct ? x.get("Name") : undefined;
        if (name instanceof Name) allNames.add(name.value);
    }
    const nodeNames = new Set<number>();
    for (const x of converted) {
        const name = x.get("Name");
        if (name instanceof Name) nodeNames.add(name.value);
    }
    const allowed = new Set([...GLOBAL_SOURCES, LEVEL]);
    const tr = new Translator(sources, thug2, (label) => allowed.has(label), nodeNames, allNames, names, {
        provided,
        undrawn,
        objects: made.objectContexts,
    });
    for (const node of converted) {
        const name = node.get("Name");
        if (name instanceof Name) tr.nodeClasses.set(name.value, nodes.classOf(node, names));
    }
    const roots: [number, Context][] = [];
    for (const node of converted) {
        const script = node.get("TriggerScript");
        if (
            script instanceof Name &&
            !provided.has(script.value) &&
            ["levelgeometry", "railnode"].includes(nodes.classOf(node, names))
        ) {
            roots.push([script.value, SKATER]);
        }
    }
    for (const [, script, context] of made.triggers) roots.push([script, context]);
    const out = tr.run(roots);
    // (checked: no brought script names a collision-only geometry node anywhere - default parameters included)
    for (const [script, code] of out) {
        for (const [t, p] of scriptPort.plainTokens(code)) {
            if (t === 0x16 && undrawn.has(u32(p))) {
                throw new FormatError(
                    `script ${names.get(script) ?? hex8(script)} names ${names.get(u32(p)) ?? hex8(u32(p))}, which has no scene sector`,
                );
            }
        }
    }
    const available = (script: number): boolean => out.has(script) || thug2.scripts.has(script) || provided.has(script);
    let dropped = 0;
    for (const node of converted) {
        const script = node.get("TriggerScript");
        if (script instanceof Name && !available(script.value)) {
            node.remove("TriggerScript");
            dropped += 1;
        }
    }
    const trigger = checksum("TriggerScript");
    const byName = new Map<number, Struct>();
    for (const x of converted) {
        const name = x.get("Name");
        if (name instanceof Name) byName.set(name.value, x);
    }
    for (const [nodeName, script] of made.triggers) {
        const node = byName.get(nodeName);
        if (node !== undefined && available(script) && node.get("TriggerScript") === undefined) {
            node.entries.push([new Name(trigger), new Name(script)]);
        }
    }
    const worst = [...tr.leftOut].sort((a, b) => b[1] - a[1]);
    const counts = new Map([...made.counts].sort((a, b) => (a[0] < b[0] ? -1 : a[0] > b[0] ? 1 : 0)));
    const bytes = [...out.values()].reduce((s, code) => s + code.length, 0);
    const leftOutTotal = [...tr.leftOut.values()].reduce((s, n) => s + n, 0);
    const report =
        `scripts: ${out.size} brought along (${bytes} bytes), ${dropped} trigger scripts not; made ${pyRepr(counts)}; ` +
        `left out ${leftOutTotal} statements: ${worst
            .slice(0, 12)
            .map(([reason, n]) => `${reason} x${n}`)
            .join(", ")}`;
    return [out, tr.globalsNs(), report, tr.leftOut];
}
