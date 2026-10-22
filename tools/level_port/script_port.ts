// NOT ORIGINAL - another game's level scripts made runnable in THUG2 (tools/thps3_levels: THPS3's): a translation of
// the compiled scripts' token streams, script by script, from the level's node scripts down through everything they
// call, spawn or hand out as exception / gap scripts. Nothing of either game is stored: the scripts are read from the
// user's files each time and written into the converted level's .qb.
//
// The token streams are close: THPS3's names, numbers, strings, vectors, structures, arrays, <params> and line ends
// are THUG2's. The differences handled here:
//   - if / else / endif: THPS3's 0x25 / 0x26 / 0x28 carry no jump; THUG2's interpreter only runs 0x47 <u16 jump> /
//     0x48 <u16 jump> (it loops for ever on a 0x25) - made from the nesting.
//   - random: THPS3's 0x2F / 0x37 {u32 count, count x u32 offset} - THUG2's have a u16 weight per entry before the
//     offsets (each 1 here).
//   - every jump (the randoms' offsets, 0x2E) is made again from where its target token lands in the output, since
//     statements are left out and tokens change size.
//   - what THUG2 can't run (see `Translator.command`) is left out statement by statement: commands THUG2 doesn't
//     have (warnings in the log at best), member functions of components the object running the script doesn't have
//     (THUG2's CallMemberFunction doesn't check: a crash), node names the converted level doesn't have (Create of a
//     missing node reads through a null), scripts that can't be brought along (goals, menus, the skater's own), and
//     things a free-skate port shouldn't do (global flags, the skater's state machine, panel messages, cameras). An
//     `if` whose condition is left out becomes `if GotParam <never>` (false).
//   - THPS3's names THUG2 calls something else (exceptions: SkaterInRadius -> ObjectInRadius...).
//
// Scripts are run by an object or none: a game object's trigger script by that object (THUG2's LevelObject / ghost
// GameObject: motion, sound, proximity components), a sector's or rail's by the skater, a spawned one by none. Each
// script is checked against every way it's run (contexts): a member function stays only when every one of them has
// its component.
//
// The output: the scripts as THUG2 tokens (appended to the NeverScript-compiled node array .qb - see appendScripts),
// the THPS3 globals they use that THUG2 doesn't have (NeverScript text), and a report of what was left out.
//
// (Python iterates a script's contexts - a set of strings - in an order that changes from run to run (string hashes
// are randomised); where that order mattered there the Python tool wasn't deterministic. Here a set keeps the order
// the contexts were added in.)

import * as fs from "node:fs";
import * as path from "node:path";
import { decodeLatin1, encodeLatin1, hex8, sortedNumbers } from "../lib/py.ts";
import { pack, unpackNumbers } from "../lib/struct.ts";
import * as prx from "./prx.ts";
import * as qb from "./qb.ts";
import { checksum } from "./qb.ts";
import { NAMED_MEMBERS, TABLE_MEMBERS, COMPONENTS as THUG2_COMPONENTS } from "./thug2_members.ts";

export const EOL: readonly number[] = [0x01, 0x02];
export const RANDOMS: readonly number[] = [0x2f, 0x37, 0x40, 0x41];
export const CONTROL: readonly number[] = [0x26, 0x28, 0x20, 0x21, 0x22, 0x29];

// Contexts: who runs a script.
export const OBJECT = "object";
export const SKATER = "skater";
export const NONE = "none";
// (THUG1's levels keep their own peds, vehicles and bouncy LevelObjects: THUG2's objects of those classes)
export const PED = "ped";
export const VEHICLE = "vehicle";
export const BOUNCY = "bouncy";
export type Context = typeof OBJECT | typeof SKATER | typeof NONE | typeof PED | typeof VEHICLE | typeof BOUNCY;
const CONTEXTS: Context[] = [OBJECT, SKATER, NONE, PED, VEHICLE, BOUNCY];

// What each context's objects have: the names of THUG2's components (tools/level_port/thug2_members.ts, generated
// from the exe by tools/extract_members.ts) and two names of the objects themselves - "object" (every object: CObject's
// own members, Die, Obj_GetId... and the table 0x57525413) and "skater" (the skater object, CSkater's
// CallMemberFunction 0x005835D0: SparksOff, SkaterIsNamed...). "compositeobject": a composite object's own table
// (Obj_GetPosition, Hide...).
// The base component every composite object made by AddBaseComponents (0x0057B250: suspend and this one) and every
// game object (0x00468CB0) has: Obj_ObjectInRadius / Obj_ObjectInRect. No THUG2 script names its type 0xB87C8E43.
export const BASE = "b87c8e43";
// AddMotionComponents (0x0057B2F0): every moving object's, the skater's, a ped's and a car's.
export const MOTION_COMPONENTS: readonly string[] = ["motion", "objectproximity", "sound", "stream", "lockobj"];
// The skater (the local one: the converters' sector and rail scripts run on it): CSkater's constructor (0x00582D90:
// AddBaseComponents, then one component after another) and Init (0x00581850: AddMotionComponents, the animation
// blender, skeleton, model, shadow, vibration, stance panel, skater sound) - src/game/skater/skater.cpp.
export const SKATER_COMPONENTS: readonly string[] = [
    "compositeobject",
    "object",
    "skater",
    "suspend",
    BASE,
    "skaterstate",
    "specialitem",
    "input",
    "skaterscore",
    "skatermatrixqueries",
    "trick",
    "skaterphysicscontrol",
    "skatercorephysics",
    "skaterrotate",
    "skatergap",
    "skateradjustphysics",
    "trigger",
    "skaterfinalizephysics",
    "skatercleanupstate",
    "walk",
    "skaterlocalnetlogic",
    "skaterendrun",
    "skaterbalancetrick",
    "skaterloopingsound",
    "skatervehiclesound",
    "statsmanager",
    "movablecontact",
    "skaterruntimer",
    "taggingcontrol",
    "skaterstatehistory",
    "skaterflipandrotate",
    "tagging",
    "pagermessage",
    ...MOTION_COMPONENTS,
    "animblender",
    "skeleton",
    "model",
    "shadow",
    "vibration",
    "skaterstancepanel",
    "skatersound",
];
// A component with a table of members (TABLE_MEMBERS) must be there: the script runs the entry on it, a null when the
// object hasn't it (a crash). One whose members are all by name (NAMED_MEMBERS: model, shadow, lockobj, skitch...)
// does nothing when it isn't there - such a component is listed where the objects usually have it.
export const SAFE_COMPONENTS: Readonly<Record<Context, ReadonlySet<string>>> = {
    // A LevelObject / ghost GameObject (CreateGameObject 0x00468CB0): gameobj_composite_structure (suspend, motion,
    // objectproximity, sound), lockobj, the base component; gameobj_add_components' model (a LevelObject's, a ghost's
    // with a model - members by name: nothing happens without it). Not stream (only with SupportStreams).
    [OBJECT]: new Set([
        "motion",
        "compositeobject",
        "object",
        "objectproximity",
        "sound",
        "suspend",
        "lockobj",
        BASE,
        "model",
    ]),
    [SKATER]: new Set(SKATER_COMPONENTS),
    [NONE]: new Set(),
    // (CreatePed 0x0057B4E0: AddBaseComponents, AddMotionComponents and THUG2's ped_add_components - avoid, pedlogic,
    // fam, animblender, skeleton, model, shadow; CreateCar 0x00578170: AddBaseComponents, AddMotionComponents,
    // car_add_components - model, collision, nodearray, railmanager, objecthookmanager, skitch (by name; not with
    // NoSkitch), and the skeleton the converters' cars are given; a bouncy LevelObject: bouncyobj_add_components -
    // suspend, rigidbody, model, sound)
    [PED]: new Set([
        "compositeobject",
        "object",
        "suspend",
        BASE,
        ...MOTION_COMPONENTS,
        "animblender",
        "pedlogic",
        "skeleton",
        "avoid",
        "fam",
        "model",
        "shadow",
    ]),
    [VEHICLE]: new Set([
        "compositeobject",
        "object",
        "suspend",
        BASE,
        ...MOTION_COMPONENTS,
        "skeleton",
        "model",
        "skitch",
    ]),
    [BOUNCY]: new Set(["compositeobject", "object", "suspend", "rigidbody", "sound", "model"]),
};
// THUG2's component names (by their type checksums).
export const COMPONENT_NAMES: readonly string[] = [
    "object",
    "compositeobject",
    ...THUG2_COMPONENTS.map(([, name]) => name),
];
// Members the object runs itself by name besides THUG2's NAMED_MEMBERS, per context (a converter's own: addContext,
// TranslatorOptions.byName). THUG2's own - the skater's gap functions, Die, Obj_MoveToNode... - are in the member
// table now.
export const BY_NAME: Readonly<Record<Context, ReadonlySet<string>>> = {
    [OBJECT]: new Set(),
    [SKATER]: new Set(),
    [NONE]: new Set(),
    [PED]: new Set(),
    [VEHICLE]: new Set(),
    [BOUNCY]: new Set(),
};

// A converter's own context (THAW's skater: tools/thaw_levels/thaw_scripts.ts), with its SAFE_COMPONENTS and BY_NAME
// - as Python's SAFE_COMPONENTS.setdefault / BY_NAME.setdefault: a context already there is kept. Every Translator
// made after knows it. Returns the context.
export function addContext(context: string, components: ReadonlySet<string>, byName: ReadonlySet<string>): Context {
    const c = context as Context;
    if (!CONTEXTS.includes(c)) {
        CONTEXTS.push(c);
        (SAFE_COMPONENTS as Record<Context, ReadonlySet<string>>)[c] = components;
        (BY_NAME as Record<Context, ReadonlySet<string>>)[c] = byName;
    }
    return c;
}

// C functions / THUG2 scripts left out although THUG2 has them: they'd change the game outside the level (global
// flags are the save's: unlocks), drive the skater (THPS3's state machine names), start goals / cutscenes / cameras,
// show THPS3's panel messages, load THPS3's sounds and streams (not converted), or only print.
export const LEFT_OUT: ReadonlySet<string> = new Set(
    [
        "SetGlobalFlag",
        "UnSetGlobalFlag",
        "MakeSkaterGoto",
        "PauseSkaters",
        "UnPauseSkaters",
        "PauseGame",
        "UnPauseGame",
        "DisablePlayerInput",
        "EnablePlayerInput",
        "PlaySkaterCamAnim",
        "SetSkaterCamAnimSkippable",
        "KillSkaterCamAnim",
        "SetGoal",
        "AwardStatPoint",
        "LaunchPanelMessage",
        "LaunchLocalPanelMessage",
        "LaunchConsoleMessage",
        "Printf",
        "PrintStruct",
        "LoadSound",
        "LoadStreamHeader",
        "AddMusicTrack",
        "PlayStream",
        "Obj_PlayStream",
        "Obj_StopStream",
        "StopStream",
        "PlayMovie",
        "Vibrate",
        "DoFlash",
        "SetMusicVolume",
        "KillSkater",
        "SK3_KillSkater",
        "SK3_KillSkater_Water",
        "SpawnSkaterScript",
        "Goal_GotLetter",
        "SetTrickName",
        "SetTrickScore",
        "Display",
        "EndOfRunDone",
        "SetState",
        "Jump",
        "BloodOn",
        "BloodOff",
        "ClearPanel_Landed",
        "ClearPanel_Bailed",
        "NotifyBailDone",
        "SetRollingFriction",
        "BashOn",
        "BashOff",
        "ChangeLevel",
        "LoadLevel",
        "Obj_ApplyScaling",
        "SwitchOnAtomic",
        "SwitchOffAtomic",
        "Obj_PlayAnim",
        "Obj_WaitAnimFinished",
        "Obj_SetAnimCycleMode",
        "Obj_CycleAnim",
        "PlayAnim",
        "WaitAnim",
        "SetSkaterCamOverride",
        "ClearSkaterCamOverride",
        "ShowAllObjects",
        "EndRun",
        "Restart",
        "SetUpLights",
        "SetBackgroundColor",
        "SetClippingDistances",
        "SetBSPAmbientColor",
        "SetDynamicLightModulationFactor",
        "SetDFFAmbientColor",
        "SetDFFDirectColor",
        "SetDFFDirectRotation",
        "CareerStartLevel",
        "StartRunScript",
        "EndRunScript",
        // (THUG2's front-end / goal scripts the source games' level scripts call under the same names - script_port
        // took them for scripts the runner can't run until it counted only commands: they would change level, load
        // from the memory card, rebind the pause key, show the goal panels or unlock a level)
        "level_select_change_level",
        "maybe_auto_load_from_memory_card",
        "restore_start_key_binding",
        "create_speech_box",
        "GoalManager_HidePoints",
        "GoalManager_ShowPoints",
        "OldSkool_Icon",
    ].map((name) => name.toLowerCase()),
);

// Parameters whose value is a script (checked: there, brought along): the key -> who runs it ("same": the caller's
// runner).
export const SCRIPT_KEYS: ReadonlyMap<string, Context | "same"> = new Map<string, Context | "same">([
    ["scr", "same"],
    ["gapscript", SKATER],
    ["trickscript", SKATER],
    ["callback", NONE],
]);
// Commands whose first unnamed name is a script, and who runs it.
export const SCRIPT_COMMANDS: ReadonlyMap<string, Context | "same"> = new Map<string, Context | "same">([
    ["spawnscript", NONE],
    ["goto", "same"],
    ["gotopreserveparams", "same"],
    ["obj_spawnscript", "same"],
    ["obj_switchscript", "same"],
    ["gotorandomscript", "same"],
]);
// C functions that look a geometry node's scene sector up (Translator.withoutUndrawn).
export const SECTOR_COMMANDS: ReadonlySet<string> = new Set(["visible", "invisible", "create", "kill", "shatter"]);
// Parameters naming a node or object (the statement is left out when the converted level hasn't it).
export const NODE_KEYS: ReadonlySet<string> = new Set(["name", "node", "nodename", "target"]);

// THPS3 names THUG2 calls otherwise (values anywhere in a statement).
export const RENAMES: Readonly<Record<string, string>> = {
    SkaterInRadius: "ObjectInRadius",
    SkaterOutOfRadius: "ObjectOutofRadius",
};

// A condition that is always false (an `if` whose command is left out): GotParam of a name no script passes.
export const NEVER = "thps3_port_never";
// Markers a converter writes into the source game's scripts before translating them (THPS3's TP-41 edits -
// tools/thps3_levels editScripts): an `if` command decided true (ALWAYS) or false (NEVER), and a statement left out
// (DROPPED). The translator makes ALWAYS `not GotParam <never>` and NEVER `GotParam <never>`, and leaves DROPPED out.
export const ALWAYS = "thps3_port_always";
export const DROPPED = "thps3_port_dropped";

// THPS3's script files a level's scripts may be brought along from (the level's own .qb and the level designers'
// shared files); anything else (goals, menus, the skater, peds' model scripts...) is THPS3's game, not the level's.
export const DEFAULT_SOURCES = [
    "cjr_scripts.qb",
    "jku_scripts.qb",
    "ajc_scripts.qb",
    "alf_scripts.qb",
    "bdj_scripts.qb",
    "cpf_scripts.qb",
    "js_scripts.qb",
    "sk3_scripts.qb",
    "skutils.qb",
];

export function u32(b: Uint8Array, at = 0): number {
    return unpackNumbers("<I", b, at)[0]!;
}

// A token of a script: t (token), p (payload), i (its index in the source file's tokens), targets (jumps: the indices
// of the tokens they go to). weights: a random's own weights (THUG2-format sources: THAW's), else 1 each. adjust: a
// 0x49 short jump that lands one byte before its target token (a switch case's first short break: one before the
// next case).
export class Tok {
    t: number;
    p: Buffer;
    i: number;
    targets: number[] | null;
    weights: number[] | null;
    adjust: number;

    constructor(
        t: number,
        p: Buffer,
        i: number,
        targets: number[] | null = null,
        weights: number[] | null = null,
        adjust = 0,
    ) {
        this.t = t;
        this.p = p;
        this.i = i;
        this.targets = targets;
        this.weights = weights;
        this.adjust = adjust;
    }

    name(): number | null {
        return this.t === 0x16 ? u32(this.p) : null;
    }

    encode(): Buffer {
        if (this.t === 0x1b || this.t === 0x1c)
            return Buffer.concat([Buffer.from([this.t]), pack("<I", this.p.length), this.p]);
        if (EOL.includes(this.t)) return Buffer.from([0x01]);
        return Buffer.concat([Buffer.from([this.t]), this.p]);
    }
}

export function nameTok(value: number, i: number): Tok {
    return new Tok(0x16, pack("<I", value), i);
}

// --- TriggerType ----------------------------------------------------------------------------------------------------

// THUG2's TriggerType (the trigger component's member, its CallMemberFunction 0x00434970): true when the events that
// set the trigger off (+0x28: a bit per event number - CTriggerComponent::TriggerNode 0x00434A60) include every
// non-negative number of its first unnamed array - or of any array in it, for an array of arrays. Without an array it
// reads through a null (0x004349B2): every form a source game passes is made such an array here, or left out.
// The event numbers (skutils.qb's GetTriggerArray, which THUG2's own levels' scripts pass): walk 14, skate 1, jump 2,
// onto / on 3, off 4, offedge 5, wallplant 6, lip 7, land 8, bonk 9, climb / hang 10, graffiti 11, spin 12, grind 13,
// projectile 14. The exe's callers pass these events (each an .rdata triple pushed to 0x00434BF0 / 0x00434A60): skate
// onto {1,3} / off {1,4} / off the edge {1,5} (0x0050A1F9 / 0x0050A1D1 / 0x0050A2A4), jump off {2,4} (0x0050391A),
// wallplant onto / off {6,3} / {6,4} (0x00507D6C / 0x0050920B), land onto {8,3} (0x00508575), bonk {9}
// (0x005070F3), hang {10,4} (0x0043DB46), graffiti onto {11,3} (the tagging 0x0051E658), grind onto {13,3}
// (0x0050AAA3), walk onto / off {14,3} / {14,4} (the walk component 0x0052B55B), a projectile {14} (0x004F3A34).
// (lip 7 and spin 12: no caller pushes them as constants - GetTriggerArray's numbers.)

// THPS3's / THPS4's / THUG1's trigger types (their skutils.qb: TRIGGER_THROUGH 0 ... TRIGGER_LIP_JUMP 9, the same
// names in all three) as THUG2 event lists - all must hold; [-1]: none, i.e. any.
export const TRIGGER_ARRAYS: Readonly<Record<string, readonly number[]>> = {
    TRIGGER_THROUGH: [-1],
    TRIGGER_SKATE_OFF_EDGE: [5],
    TRIGGER_JUMP_OFF: [2, 4],
    TRIGGER_LAND_ON: [8],
    TRIGGER_SKATE_OFF: [1, 4],
    TRIGGER_SKATE_ONTO: [1, 3],
    TRIGGER_BONK: [9],
    TRIGGER_LIP_ON: [7, 3],
    TRIGGER_LIP_OFF: [7, 4],
    TRIGGER_LIP_JUMP: [7, 2],
};
// THAW's flags (`TriggerType land onto`, `TriggerType {Lip On}`) in GetTriggerArray's order - it takes the first three
// of them in this order, -1 for the rest. `through`: none (any).
export const TRIGGER_FLAGS: readonly (readonly [string, number])[] = [
    ["walk", 14],
    ["skate", 1],
    ["jump", 2],
    ["onto", 3],
    ["on", 3],
    ["off", 4],
    ["offedge", 5],
    ["wallplant", 6],
    ["lip", 7],
    ["land", 8],
    ["bonk", 9],
    ["climb", 10],
    ["hang", 10],
    ["graffiti", 11],
    ["spin", 12],
    ["grind", 13],
    ["projectile", 14],
    ["through", -1],
];
// THAW's flags THUG2 has no event for (THAW's smack, Natas spin, acid drop, stall): left out.
export const TRIGGER_NONE: readonly string[] = ["Smack", "Natas", "AcidDrop", "STALL"];

// THAW's flags as GetTriggerArray makes them: three numbers (-1 none), or the name of a flag THUG2 hasn't.
export function triggerFlags(flags: readonly string[]): number[] | string {
    const wanted = new Set(flags.map((f) => f.toLowerCase()));
    const known = new Set(TRIGGER_FLAGS.map(([f]) => f));
    const unknown = flags.find((f) => !known.has(f.toLowerCase()));
    if (unknown !== undefined) return unknown;
    const values: number[] = [];
    for (const [flag, value] of TRIGGER_FLAGS) {
        // (onto, else on - GetTriggerArray's `if GotParam onto ... else if GotParam on`; climb, else hang)
        if (!wanted.has(flag) || value < 0) continue;
        if ((flag === "on" && wanted.has("onto")) || (flag === "hang" && wanted.has("climb"))) continue;
        values.push(value);
    }
    const out = values.slice(0, 3);
    while (out.length < 3) out.push(-1);
    return out;
}

const TRIGGER_TYPE = checksum("TriggerType");
const TRIGGER_BY_CHECKSUM = new Map(Object.entries(TRIGGER_ARRAYS).map(([k, v]) => [checksum(k), v]));
const FLAG_BY_CHECKSUM = new Map(
    [...TRIGGER_FLAGS.map(([f]) => f), ...TRIGGER_NONE].map((f) => [checksum(f), f] as const),
);

// `TriggerType ...` as THUG2's (the statement's tokens), or why it is left out:
//   TriggerType TRIGGER_LIP_ON / [TRIGGER_A TRIGGER_B]  (THPS3, THPS4, THUG1) -> TriggerType [[7 3] ...]
//   TriggerType land onto / {Lip On}  (THAW's flags; a structure's are plain arguments by then) -> TriggerType [8 3 -1]
//   TriggerType <param>  (THAW's <trigger_array>: GetTriggerArray's, THUG2's script) -> kept
export function triggerType(toks: readonly Tok[]): Tok[] | string {
    const args = toks.slice(1).filter((t) => !EOL.includes(t.t));
    if (args.length === 2 && args[0]!.t === 0x2d && args[1]!.t === 0x16) return [...toks];
    const names: number[] = [];
    for (const t of args) {
        if (t.t === 0x16) names.push(t.name()!);
        else if (![0x03, 0x04, 0x05, 0x06, 0x09].includes(t.t)) return "TriggerType: form";
    }
    if (!names.length) return "TriggerType: form";
    const i = toks[0]!.i;
    const empty = Buffer.alloc(0);
    const int = (v: number): Tok => new Tok(0x17, pack("<i", v), i);
    if (names.every((n) => TRIGGER_BY_CHECKSUM.has(n))) {
        const out = [toks[0]!, new Tok(0x05, empty, i)];
        for (const n of names)
            out.push(new Tok(0x05, empty, i), ...TRIGGER_BY_CHECKSUM.get(n)!.map(int), new Tok(0x06, empty, i));
        out.push(new Tok(0x06, empty, i));
        return out;
    }
    const flags = names.map((n) => FLAG_BY_CHECKSUM.get(n));
    if (flags.some((f) => f === undefined)) return "TriggerType: form";
    const values = triggerFlags(flags as string[]);
    if (typeof values === "string") return `TriggerType: no THUG2 trigger event: ${values}`;
    return [toks[0]!, new Tok(0x05, empty, i), ...values.map(int), new Tok(0x06, empty, i)];
}

// A command a THUG2 script runs on the object running it (Thug2.uses): a member function or a THUG2 script (a call, or
// one it goes to / spawns on the same object). guards: the parameters of the `if GotParam X` branches it is in - it
// runs only when the caller passes all of them; passed: for a script call, the parameter names the statement passes
// (keys and flags), all: it passes the caller's own (<...>).
export interface Use {
    name: number;
    guards: readonly number[];
    passed: readonly number[];
    all: boolean;
}

// THUG2 commands whose first unnamed name is a script run on the same object (scriptNeeds follows them).
const SAME_OBJECT_SCRIPTS: ReadonlySet<number> = new Set(
    ["goto", "gotopreserveparams", "gotorandomscript", "obj_spawnscript", "obj_switchscript"].map(checksum),
);

// --- what THUG2 has ------------------------------------------------------------------------------------------------

// THUG2's commands, read from the user's game: its C functions (the CFunction table in THUG2PM.exe: name, function),
// its member functions (tools/level_port/thug2_members.ts: the components' tables and the members registered by name,
// with the components that run them) and its scripts and globals (Data\pre\qb_scripts.prx).
export class Thug2 {
    static readonly CFUNC_TABLE = 0x67f3a0;
    static readonly CFUNC_COUNT = 0x4e6;
    static readonly DESC = checksum("descChecksum");
    static readonly GOT_PARAM = checksum("GotParam");

    readonly root: string;
    readonly cfuncs: Set<number>;
    // member checksum -> the components (names, as SAFE_COMPONENTS') that run it: one for a table member, those whose
    // CallMemberFunction takes it for a member by name (none: no object of the converters' has it)
    readonly members: Map<number, readonly string[]>;
    // the members registered in tables (an object without the component crashes on them)
    readonly tableMembers: Set<number>;
    readonly scripts = new Set<number>();
    readonly globals = new Set<number>();
    readonly anims = new Set<number>();
    // every name a script uses (not `object:` calls) - what THUG2's scripts refer to
    readonly bodies = new Map<number, Set<number>>();
    // the commands a script runs on its object (scriptNeeds)
    readonly uses = new Map<number, Use[]>();
    readonly needs = new Map<string, Set<string>>();

    constructor(thug2: string) {
        let exe: Buffer | null = null;
        for (const name of ["THUG2PM.exe", "THUG2.exe"]) {
            const file = path.join(thug2, name);
            if (fs.existsSync(file)) {
                exe = fs.readFileSync(file);
                break;
            }
        }
        this.root = thug2;
        if (exe === null) throw new Error(`no THUG2PM.exe in ${thug2}`);
        this.cfuncs = new Set(Thug2.cfuncNames(exe).map(checksum));
        [this.members, this.tableMembers] = Thug2.memberTable();
        for (const [, data] of prx.entries(path.join(thug2, "Data", "pre", "qb_scripts.prx"), (n) =>
            n.toLowerCase().endsWith(".qb"),
        )) {
            this.readQb(data);
        }
    }

    static cfuncNames(exe: Buffer): string[] {
        const pe = u32(exe, 0x3c);
        const first = pe + 24 + unpackNumbers("<H", exe, pe + 20)[0]!;
        const count = unpackNumbers("<H", exe, pe + 6)[0]!;
        const sections: number[][] = [];
        for (let k = 0; k < count; k++) sections.push(unpackNumbers("<IIII", exe, first + k * 40 + 8));

        const offset = (va: number): number | null => {
            const rva = va - 0x400000;
            for (const [, virtual, rawSize, raw] of sections as [number, number, number, number][]) {
                if (virtual <= rva && rva < virtual + rawSize) return raw + rva - virtual;
            }
            return null;
        };

        const names: string[] = [];
        const table = offset(Thug2.CFUNC_TABLE);
        if (table === null) throw new TypeError("the CFunction table's address isn't in the exe");
        for (let k = 0; k < Thug2.CFUNC_COUNT; k++) {
            const text = offset(u32(exe, table + 8 * k));
            if (text === null) continue;
            const end = exe.indexOf(0, text);
            if (end < 0) throw new RangeError("subsection not found");
            names.push(decodeLatin1(exe.subarray(text, end)));
        }
        if (!names.length || names[0] !== "Wait") {
            throw new RangeError(
                `THUG2PM.exe: the CFunction table isn't at 0x${Thug2.CFUNC_TABLE.toString(16).toUpperCase()}`,
            );
        }
        return names;
    }

    // The member table (thug2_members.ts): {member checksum: its components} and the table members.
    static memberTable(): [Map<number, readonly string[]>, Set<number>] {
        const members = new Map<number, string[]>();
        const add = (name: number, who: readonly string[]): void => {
            const list = members.get(name) ?? [];
            for (const w of who) if (!list.includes(w)) list.push(w);
            members.set(name, list);
        };
        for (const [name, component] of TABLE_MEMBERS) add(name, [component]);
        for (const [name, who] of NAMED_MEMBERS) add(checksum(name), who);
        return [members, new Set(TABLE_MEMBERS.map(([name]) => name))];
    }

    private readQb(data: Buffer): void {
        const toks = [...qb.tokens(data, true)];
        let depth = 0;
        let current: number | null = null;
        toks.forEach(([, t, p], k) => {
            const next = toks[k + 1];
            if (t === 0x23 && next !== undefined && next[1] === 0x16) {
                current = u32(next[2]);
                this.scripts.add(current);
                this.bodies.set(current, new Set());
                depth += 1;
            } else if (t === 0x24) {
                depth -= 1;
            } else if (t === 0x16 && depth === 0 && next !== undefined && next[1] === 0x07) {
                this.globals.add(u32(p));
            } else if (
                t === 0x16 &&
                depth > 0 &&
                current !== null &&
                toks[k - 1]![1] !== 0x42 &&
                !(next !== undefined && next[1] === 0x42)
            ) {
                // (the names a script uses - not `object:` nor what follows it: those run on another object)
                this.bodies.get(current)!.add(u32(p));
            }
            if (t === 0x16 && u32(p) === Thug2.DESC && k + 2 < toks.length && toks[k + 2]![1] === 0x16) {
                // (the animations THUG2 loads: LoadAnim ... descChecksum=<anim> in its animload scripts)
                this.anims.add(u32(toks[k + 2]![2]));
            }
        });
        // the commands each script runs on its object
        for (let k = 0; k < toks.length; k++) {
            if (toks[k]![1] === 0x23 && toks[k + 1]?.[1] === 0x16) {
                let end = k + 2;
                while (end < toks.length && toks[end]![1] !== 0x24) end += 1;
                this.uses.set(u32(toks[k + 1]![2]), Thug2.commandsOf(toks.slice(k + 2, end).map(([, t, p]) => [t, p])));
                k = end;
            }
        }
    }

    // The commands a script body (THUG2 tokens) runs on its object: names in command position - a statement's first
    // name (after a line end, `if`, `not`, `else`, a random's branch), not `object:` calls nor `name = value` - and the
    // scripts it goes to / spawns on the same object. Each with the `if GotParam X` branches it is in (not their else
    // parts: those run without X).
    static commandsOf(toks: readonly (readonly [number, Buffer])[]): Use[] {
        const out: Use[] = [];
        const frames: { guard: number | null; inElse: boolean }[] = [];
        let condition: { guard: number | null } | null = null;
        let expect = true; // a command may come next
        let depth = 0; // inside ( ), [ ], { }
        const guards = (): number[] =>
            frames.filter((f) => f.guard !== null && !f.inElse).map((f) => f.guard as number);
        for (let k = 0; k < toks.length; k++) {
            const [t, p] = toks[k]!;
            if (t === 0x01 || t === 0x02) {
                if (condition !== null) {
                    frames.push({ guard: condition.guard, inElse: false });
                    condition = null;
                }
                expect = depth === 0;
                continue;
            }
            if (t === 0x47 || t === 0x25) {
                condition = { guard: null };
                expect = true;
                continue;
            }
            if (t === 0x48 || t === 0x26) {
                const top = frames[frames.length - 1];
                if (top) top.inElse = true;
                expect = true;
                continue;
            }
            if (t === 0x28) {
                frames.pop();
                expect = false;
                continue;
            }
            if (t === 0x39 || RANDOMS.includes(t) || t === 0x2e || t === 0x20 || t === 0x49) {
                // (not: the command follows; a random's / jump's branch starts a statement)
                continue;
            }
            if (t === 0x03 || t === 0x05 || t === 0x0e) depth += 1;
            else if (t === 0x04 || t === 0x06 || t === 0x0f) depth = Math.max(0, depth - 1);
            if (t !== 0x16 || !expect) {
                expect = false;
                continue;
            }
            expect = false;
            const next = toks[k + 1]?.[0];
            if (next === 0x42 || next === 0x07) continue; // (`object:...` runs on another object; `name = value`)
            const name = u32(p);
            // the statement's arguments
            const passed: number[] = [];
            let all = false;
            let firstUnnamed: number | null = null;
            let level = 0;
            for (let j = k + 1; j < toks.length; j++) {
                const [t2, p2] = toks[j]!;
                if ((t2 === 0x01 || t2 === 0x02) && level === 0) break;
                if (t2 === 0x03 || t2 === 0x05 || t2 === 0x0e) level += 1;
                else if (t2 === 0x04 || t2 === 0x06 || t2 === 0x0f) level -= 1;
                if (level > 0) continue;
                if (t2 === 0x2c) all = true;
                if (t2 === 0x16 && toks[j - 1]?.[0] !== 0x07 && toks[j - 1]?.[0] !== 0x2d) {
                    passed.push(u32(p2));
                    if (toks[j + 1]?.[0] !== 0x07 && firstUnnamed === null) firstUnnamed = u32(p2);
                }
            }
            if (condition !== null && name === Thug2.GOT_PARAM && toks[k + 1]?.[0] === 0x16) {
                // (`if GotParam X`: the branch runs only with X; not `if NOT GotParam X` - its command comes after a 0x39)
                if (toks[k - 1]?.[0] !== 0x39) condition.guard = u32(toks[k + 1]![1]);
            }
            const g = guards();
            out.push({ name, guards: g, passed, all });
            if (SAME_OBJECT_SCRIPTS.has(name) && firstUnnamed !== null)
                out.push({ name: firstUnnamed, guards: g, passed: [], all: false });
        }
        return out;
    }

    component(name: number): readonly string[] | undefined {
        return this.members.get(name);
    }

    scriptNeeds(name: number, passed: Iterable<number> | null = null, seenIn?: Set<string>): Set<string> {
        // What a THUG2 script needs of the object running it: for each member function it runs (and those of the
        // THUG2 scripts it calls or goes to on the same object, all the way down), the components that run it ("a|b":
        // either). Only commands in command position count (not the parameter names and values a script uses), and
        // those in an `if GotParam X` branch only when X is passed (null: the caller isn't known - every branch counts).
        const given = passed === null ? null : new Set(passed);
        const key = `${name}:${given === null ? "*" : sortedNumbers(given).join(",")}`;
        const cached = this.needs.get(key);
        if (cached !== undefined) return cached;
        const seen = seenIn ?? new Set<string>();
        seen.add(key);
        const out = new Set<string>();
        for (const use of this.uses.get(name) ?? []) {
            if (given !== null && !use.guards.every((g) => given.has(g))) continue;
            const who = this.members.get(use.name);
            if (who !== undefined) {
                out.add(who.length ? [...who].sort().join("|") : "(none)");
            } else if (this.scripts.has(use.name)) {
                const inner = use.all ? (given === null ? null : [...given, ...use.passed]) : use.passed;
                const innerKey = `${use.name}:${inner === null ? "*" : sortedNumbers(new Set(inner)).join(",")}`;
                if (seen.has(innerKey)) continue;
                for (const c of this.scriptNeeds(use.name, inner, seen)) out.add(c);
            }
        }
        if (seenIn === undefined) this.needs.set(key, out);
        return out;
    }
}

// --- THPS3's scripts -----------------------------------------------------------------------------------------------

// A .qb file's scripts as token lists (jumps as the indices of their target tokens) and its names / globals.
// weights: the random tokens carry u16 weights before their offsets (THUG1's, THUG2's - not THPS3's); kept.
export class Source {
    readonly label: string;
    readonly names = new Map<number, string>();
    readonly scripts = new Map<number, Tok[]>();
    readonly globals: Map<number, qb.QbValue>;

    constructor(data: Uint8Array, label: string, weights = false) {
        this.label = label;
        const toks = [...qb.tokens(data, weights)];
        const at = new Map<number, number>();
        toks.forEach(([off], k) => {
            at.set(off, k);
        });
        const target = (offset: number): number => {
            const k = at.get(offset);
            if (k === undefined) throw new RangeError(`no token at 0x${offset.toString(16)} (a jump's target)`); // (KeyError)
            return k;
        };
        let k = 0;
        while (k < toks.length) {
            const [, t, p] = toks[k]!;
            if (t === 0x2b) {
                this.names.set(u32(p), decodeLatin1(p.subarray(4, p.length - 1)));
            } else if (t === 0x23 && k + 1 < toks.length && toks[k + 1]![1] === 0x16) {
                const name = u32(toks[k + 1]![2]);
                const body: Tok[] = [];
                let j = k + 2;
                for (;;) {
                    const tok = toks[j];
                    if (tok === undefined) throw new RangeError("list index out of range");
                    const [o2, t2, p2] = tok;
                    if (t2 === 0x24) break;
                    let targets: number[] | null = null;
                    let w: number[] | null = null;
                    if (t2 === 0x2e) {
                        targets = [target(o2 + 5 + unpackNumbers("<i", p2)[0]!)];
                    } else if (RANDOMS.includes(t2)) {
                        const n = u32(p2);
                        const base = 4 + (weights ? 2 * n : 0);
                        targets = [];
                        for (let m = 0; m < n; m++)
                            targets.push(target(o2 + 1 + base + 4 * m + 4 + u32(p2, base + 4 * m)));
                        w = weights ? unpackNumbers(`<${n}H`, p2, 4) : null;
                    }
                    body.push(new Tok(t2, p2, j, targets, w));
                    j += 1;
                }
                this.scripts.set(name, body);
                k = j;
            }
            k += 1;
        }
        let globals = new Map<number, qb.QbValue>();
        try {
            globals = new Map(new qb.QbFile(data, label, weights).globals.map(([key, value]) => [key.value, value]));
        } catch {
            // (a file whose globals the reader can't take: its scripts are still usable)
            globals = new Map();
        }
        this.globals = globals;
    }
}

export function randomEnd(tok: Tok, body: readonly Tok[]): number {
    // The index of the token after an argument random (the target of its branches' closing jumps).
    if (tok.targets!.length < 2) return tok.i;
    for (const b of body) {
        if (b.i === tok.targets![1]! - 1 && b.t === 0x2e) return b.targets![0]!;
    }
    return tok.i;
}

export function statements(body: readonly Tok[]): Tok[][] {
    // The script body as statements (token lists, each with its line end): the default-parameters line first; a
    // statement-level random and each of its branches' closing jumps are statements of their own (their branches'
    // statements follow); an argument random (inside a statement) stays in its statement, line ends and all.
    const out: Tok[][] = [];
    let cur: Tok[] = [];
    let depth = 0;
    let until = -1;
    for (const tok of body) {
        const content = cur.some((x) => !EOL.includes(x.t));
        if (depth === 0 && tok.i >= until && ((RANDOMS.includes(tok.t) && !content) || tok.t === 0x2e)) {
            if (cur.length) out.push(cur);
            out.push([tok]);
            cur = [];
            continue;
        }
        cur.push(tok);
        if (tok.t === 0x03 || tok.t === 0x05 || tok.t === 0x0e) depth += 1;
        else if (tok.t === 0x04 || tok.t === 0x06 || tok.t === 0x0f) depth -= 1;
        else if (RANDOMS.includes(tok.t)) until = Math.max(until, randomEnd(tok, body));
        else if (EOL.includes(tok.t) && depth === 0 && tok.i >= until) {
            out.push(cur);
            cur = [];
        }
    }
    if (cur.length) out.push(cur);
    return out;
}

export type Arg = [number | null, Tok];

export function argsOf(toks: readonly Tok[]): Arg[] {
    // A statement's (key, value token) pairs and unnamed values (key null), from the tokens after the command.
    const out: Arg[] = [];
    let k = 0;
    while (k < toks.length) {
        const t = toks[k]!;
        if (t.t === 0x16 && k + 2 < toks.length && toks[k + 1]!.t === 0x07) {
            out.push([t.name(), toks[k + 2]!]);
            k += 3;
            continue;
        }
        out.push([null, t]);
        k += 1;
    }
    return out;
}

// An output item of a translated script: a token (re-encoded; randoms, jumps and short jumps made again), an if /
// else / case / default / switch jump (made from the labels), or a label.
type Item =
    | { kind: "tok" | "random" | "jump" | "short"; tok: Tok; orig: number }
    | { kind: "if" | "else" | "default" | "swjump"; n: number; orig: number }
    | { kind: "case"; id: number; k: number; orig: number }
    | { kind: "label"; key: string; orig: number };

interface Switch {
    id: number;
    k: number;
    open: "case" | "default" | null;
    body: number;
}

export type ObjectContext = Context | [Context, boolean];

export interface TranslatorOptions {
    // scripts the level's .qb defines otherwise (its kill scripts) - used as THUG2's own.
    provided?: Iterable<number>;
    // a call of a brought script with no arguments passes the caller's (`<...>` added): THPS3's scripts rely on it (a
    // proximity node's script calls ALF_SUB_Hide_Const_Pool, which tests `inside`); THUG2's callee gets only what it's
    // given.
    inheritParams?: boolean;
    // `if inside` is THUG2's own Inside command (0x00449950: whether the proximity node script running now was started
    // by entering), kept as it is: the level's proximity nodes are THUG2's own ProximNodes (THUG1's). Without it `if
    // inside` becomes `if GotParam inside` - the parameter our THPS3 proximity ghosts pass; a ProximNode passes none,
    // so that test would always be false (THUG1 Vancouver's hotel stayed hidden with the camera inside it).
    proximInside?: boolean;
    // geometry nodes the converted scene has no sector for (withoutUndrawn)
    undrawn?: Iterable<number>;
    // those of them that are collision-only and kept in the level: their Create / Kill are kept (THUG2's LevelGeometry
    // Create 0x005A2E50 and Kill 0x005A32C0 reach the sector's geometry only through CSector's SetVisibility /
    // SetActive / 0x0049EA30, which skip a null one - only Visible / Invisible / Shatter don't). THPS3's (PC-THPS3-17).
    collisionOnly?: Iterable<number>;
    // more of LEFT_OUT / RENAMES (another game's)
    leftOut?: Iterable<string>;
    renames?: Readonly<Record<string, string>> | null;
    // the converted level's objects `name:command` statements (token 0x42) may address - {node name checksum:
    // [context, created at start]} (THPS4's: checked as run by that object, kept only when it's there or a kept
    // Create makes it) or {node name checksum: context} (THUG1's: checked as run by it, run only while it is alive -
    // guarded with IsAlive); null: those statements are left out (THAW's, THPS3's).
    objects?: ReadonlyMap<number, ObjectContext> | null;
    // {context: more SAFE_COMPONENTS}
    components?: Partial<Record<Context, Iterable<string>>> | null;
    // `name = value` statements kept with their script values brought along, `not` conditions looked into, node
    // names passed on checked (THPS4's).
    full?: boolean;
    // {context: more BY_NAME}
    byName?: Partial<Record<Context, Iterable<string>>> | null;
    // {command checksum: fn(statement tokens) -> tokens} applied first.
    rewrites?: ReadonlyMap<number, (toks: Tok[]) => Tok[] | null> | null;
    // more commands left out by prefix (THUG1's goal manager calls...)
    leftOutPrefixes?: Iterable<string>;
    // a parameter whose key says anim (anim=, standing_anims=) naming an animation THUG2 doesn't load leaves its
    // statement out, one naming the other game's global (a list of its animations) is dropped (THUG2's scripts use
    // their default).
    animCheck?: boolean;
    // names of LEFT_OUT this game's scripts may use (THUG1's LoadSound: THUG2 has its sounds).
    keptIn?: Iterable<string>;
    // `not` conditions looked into, and a condition naming anything left out is false (THUG1's).
    strictConditions?: boolean;
    // sector name prefixes (any case) that must stay drawn: Invisible / Kill with a prefix= overlapping one are left
    // out (THPS3 Warehouse's lit set, kept in place of the dark set its flicker script switched to - PC-THPS3-1).
    shownPrefixes?: Iterable<string>;
}

export class Translator {
    readonly extraLeftOut: Set<string>;
    readonly leftOutPrefixes: string[];
    readonly objectCalls: boolean;
    readonly objects: Map<number, ObjectContext>;
    readonly objectStates: boolean;
    readonly full: boolean;
    readonly rewrites: ReadonlyMap<number, (toks: Tok[]) => Tok[] | null>;
    readonly safe: Record<Context, Set<string>>;
    readonly byName: Record<Context, Set<string>>;
    readonly animCheck: boolean;
    readonly keptIn: Set<string>;
    readonly strictConditions: boolean;
    readonly shownPrefixes: string[];
    guard: Tok[] | null = null;
    readonly created = new Set<number>(); // objects a kept Create makes (`name:` on them is kept)
    readonly createdPrefixes = new Set<string>(); // and the prefix= of kept Creates (lower case)
    readonly referenced = new Set<number>(); // node names kept statements pass as values (discovery)
    readonly thug2: Thug2;
    readonly inheritParams: boolean;
    readonly proximInside: boolean;
    readonly undrawn: Set<number>;
    readonly collisionOnly: Set<number>;
    readonly provided: Set<number>;
    readonly names: Map<number, string>;
    readonly where = new Map<number, Source>();
    readonly globals = new Map<number, qb.QbValue>();
    readonly translatable: (label: string) => boolean;
    readonly nodeNames: Set<number>;
    readonly allNodeNames: Set<number>;
    readonly contexts = new Map<number, Set<Context>>(); // script -> its contexts
    leftOut = new Map<string, number>(); // reason -> count
    usedGlobals = new Set<number>();
    readonly components: Map<number, string>;
    protected callArgs: number[] | null = null;
    readonly renames: Map<number, number>;
    readonly skaters: Set<number>;
    protected todo: number[] = [];

    constructor(
        sources: readonly Source[],
        thug2: Thug2,
        translatable: (label: string) => boolean,
        nodeNames: Iterable<number>,
        allNodeNames: Iterable<number>,
        names: Map<number, string>,
        options: TranslatorOptions = {},
    ) {
        // sources: Source list (later ones win); translatable(source label) -> may its scripts be brought along;
        // nodeNames: the converted level's nodes; allNodeNames: the other game's level's. The options: see
        // TranslatorOptions.
        this.extraLeftOut = new Set([...(options.leftOut ?? [])].map((n) => n.toLowerCase()));
        this.leftOutPrefixes = [...(options.leftOutPrefixes ?? [])].map((p) => p.toLowerCase());
        this.shownPrefixes = [...(options.shownPrefixes ?? [])].map((p) => p.toLowerCase());
        this.objectCalls = options.objects !== undefined && options.objects !== null;
        this.objects = new Map(options.objects ?? []);
        this.objectStates = [...this.objects.values()].some((v) => Array.isArray(v)); // THPS4's form
        this.full = options.full ?? false;
        this.rewrites = options.rewrites ?? new Map();
        const components = options.components ?? {};
        const byName = options.byName ?? {};
        this.safe = Object.fromEntries(
            CONTEXTS.map((c) => [c, new Set([...SAFE_COMPONENTS[c], ...(components[c] ?? [])])]),
        ) as Record<Context, Set<string>>;
        this.byName = Object.fromEntries(
            CONTEXTS.map((c) => [c, new Set([...BY_NAME[c], ...[...(byName[c] ?? [])].map((n) => n.toLowerCase())])]),
        ) as Record<Context, Set<string>>;
        this.animCheck = options.animCheck ?? false;
        this.keptIn = new Set([...(options.keptIn ?? [])].map((n) => n.toLowerCase()));
        this.strictConditions = options.strictConditions ?? false;
        this.thug2 = thug2;
        this.inheritParams = options.inheritParams ?? false;
        this.proximInside = options.proximInside ?? false;
        this.undrawn = new Set(options.undrawn ?? []);
        this.collisionOnly = new Set(options.collisionOnly ?? []);
        this.provided = new Set(options.provided ?? []);
        this.names = names;
        for (const s of sources) {
            for (const name of s.scripts.keys()) this.where.set(name, s);
            for (const [k, v] of s.globals) this.globals.set(k, v);
        }
        this.translatable = translatable;
        this.nodeNames = new Set(nodeNames);
        this.allNodeNames = new Set(allNodeNames);
        this.components = new Map(
            COMPONENT_NAMES.map((c) => [/^[0-9a-f]{8}$/.test(c) ? Number.parseInt(c, 16) : checksum(c), c]),
        );
        const allRenames = { ...RENAMES, ...(options.renames ?? {}) };
        this.renames = new Map(Object.entries(allRenames).map(([a, b]) => [checksum(a), checksum(b)]));
        const setDefault = (text: string): void => {
            if (!this.names.has(checksum(text))) this.names.set(checksum(text), text);
        };
        for (const b of Object.values(allRenames)) setDefault(b);
        for (const text of [NEVER, "GotParam", "IsAlive", "Wait", "frame", "Name"]) setDefault(text);
        this.skaters = new Set(["Skater", "Skater2"].map(checksum));
    }

    text(value: number): string {
        return this.names.get(value) ?? `#${hex8(value)}`;
    }

    note(reason: string): void {
        this.leftOut.set(reason, (this.leftOut.get(reason) ?? 0) + 1);
    }

    // -- what's available --

    isThps3Script(name: number): boolean {
        // (THUG2's own command of the name wins: THPS3's Goto is a script, THUG2's a C function)
        return (
            this.where.has(name) &&
            !this.thug2.scripts.has(name) &&
            !this.thug2.cfuncs.has(name) &&
            !this.thug2.members.has(name) &&
            !this.provided.has(name)
        );
    }

    canBring(name: number): boolean {
        return this.isThps3Script(name) && this.translatable(this.where.get(name)!.label);
    }

    scriptAvailable(name: number): boolean {
        return this.thug2.scripts.has(name) || this.provided.has(name) || this.canBring(name);
    }

    thug2ScriptOk(name: number, contexts: Iterable<Context>): boolean {
        // A THUG2 script run by these contexts' objects: every component it needs (Thug2.scriptNeeds) they have.
        if (this.provided.has(name)) return true;
        // (this.callArgs: the parameters the calling statement passes - `if GotParam X` branches without X don't count;
        // null: not known, every branch counts)
        const needs = [...this.thug2.scriptNeeds(name, this.callArgs)];
        const list = [...contexts];
        return needs.every((n) => list.every((c) => n.split("|").some((w) => this.safe[c].has(w))));
    }

    require(name: number, context: Context): boolean {
        // A script needed in a context: true when THUG2 has it (and the context's objects can run it) or it is
        // brought along (queued).
        if (this.provided.has(name)) return true;
        if (this.thug2.scripts.has(name)) return this.thug2ScriptOk(name, [context]);
        if (!this.canBring(name)) return false;
        let have = this.contexts.get(name);
        if (have === undefined) {
            have = new Set();
            this.contexts.set(name, have);
        }
        if (!have.has(context)) {
            have.add(context);
            this.todo.push(name);
        }
        return true;
    }

    // -- one statement --

    command(toks: Tok[], contexts: ReadonlySet<Context>, discover: boolean): Tok[] | null {
        // The statement's tokens (no line end) as THUG2 tokens, or null to leave it out (noted). `discover`: also
        // queue the scripts it needs.
        const first = toks[0]!;
        if (first.t === 0x2d && this.objects.size && toks.length > 3 && toks[1]!.t === 0x16 && toks[2]!.t === 0x42) {
            // (<param>:command - an object the script was handed: checked as a game object's, run while it's alive)
            if (toks[3]!.t !== 0x16) {
                this.note("object call form");
                return null;
            }
            const kept = this.command(toks.slice(3), new Set([OBJECT]), discover);
            if (kept === null) return null;
            this.guard = toks.slice(0, 2);
            return [...toks.slice(0, 3), ...kept];
        }
        if (toks.length > 1 && toks[1]!.t === 0x42) {
            if (!this.objectCalls) {
                // (`object:member ...` - THAW's, THPS3's: THUG2 runs the rest on the first skater when the object isn't
                // there, and crashes on a member the object hasn't)
                this.note("object member call");
                return null;
            }
            if (toks.length > 2 && this.objectStates) return this.onObject(toks, contexts, discover);
        }
        if (first.t !== 0x16) return toks;
        if (first.name() === checksum(DROPPED)) {
            this.note("left out by the converter's edit");
            return null;
        }
        const rewrite = this.rewrites.get(first.name()!);
        if (rewrite !== undefined) {
            const rewritten = rewrite(toks);
            if (rewritten === null) {
                this.note(`left out (rewrite): ${this.text(first.name()!)}`);
                return null;
            }
            toks = rewritten;
        }
        if (toks[0]!.t === 0x16 && toks[0]!.name() === TRIGGER_TYPE) {
            // (every game's TriggerType made THUG2's array - THUG2's reads through a null without one)
            const made = triggerType(toks);
            if (typeof made === "string") {
                this.note(made);
                return null;
            }
            toks = made;
        }
        if (this.full && toks.length > 2 && toks[1]!.t === 0x07) {
            // `name = value` (a parameter set): a script as the value brought along (run as the caller's: goto <it>)
            for (const value of toks.slice(2)) {
                if (value.t === 0x16 && this.isThps3Script(value.name()!)) {
                    const ok = discover
                        ? [...contexts].every((c) => this.require(value.name()!, c))
                        : this.scriptAvailable(value.name()!);
                    if (!ok) {
                        this.note(`script not available: ${this.text(value.name()!)}`);
                        return null;
                    }
                }
            }
            return toks.map((t) =>
                t.t === 0x16 && this.renames.has(t.name()!) ? nameTok(this.renames.get(t.name()!)!, t.i) : t,
            );
        }
        const name = first.name()!;
        const text = this.text(name).toLowerCase();
        if (toks.length > 1 && toks[1]!.t === 0x07) {
            // (`name = value`: sets a parameter of the running script; with `full` handled above)
            return toks;
        }
        if (this.isLeftOut(text)) {
            this.note(`left out: ${this.text(name)}`);
            return null;
        }
        if (toks.length > 2 && toks[1]!.t === 0x42) return this.objectCall(toks, discover);
        if (this.animCheck) toks = this.checkedAnims(toks);
        let args = argsOf(toks.slice(1));
        if ((text === "invisible" || text === "kill") && this.hidesShown(args)) return null;
        if (SECTOR_COMMANDS.has(text) && this.undrawn.size) {
            const kept = this.withoutUndrawn(toks, args);
            if (kept === null) return null;
            toks = kept;
            args = argsOf(toks.slice(1));
        }
        // node / object names the level hasn't
        for (const [key, value] of args) {
            if (
                key !== null &&
                NODE_KEYS.has(this.text(key).toLowerCase()) &&
                value.t === 0x16 &&
                this.allNodeNames.has(value.name()!) &&
                !this.nodeNames.has(value.name()!)
            ) {
                this.note(`node not converted: ${this.text(value.name()!)}`);
                return null;
            }
        }
        if (this.full) {
            // (any parameter: THPS4's scripts pass node names on - SF2_CheckThenCreateCar carname=X does
            // `IsAlive name=<carname>`, which reads through a null for a node the array hasn't; and global
            // structures of them - CJR_SF2_Fisherman_Box_Hit Fisherman03Params)
            for (const [key, value] of args) {
                if (value.t !== 0x16) continue;
                let found = [value.name()!];
                const global = this.globals.get(value.name()!);
                if (key === null && global instanceof qb.Struct) {
                    found = global.entries.filter(([, v]) => v instanceof qb.Name).map(([, v]) => (v as qb.Name).value);
                }
                const missing = found.filter((n) => this.allNodeNames.has(n) && !this.nodeNames.has(n));
                if (missing.length) {
                    this.note(`node not converted (passed on): ${this.text(missing[0]!)}`);
                    return null;
                }
            }
        }
        // scripts passed as values
        const firstUnnamed = args.find(([k]) => k === null)?.[1];
        for (const [key, value] of args) {
            if (value.t !== 0x16) continue;
            const keyText = key !== null ? this.text(key).toLowerCase() : null;
            let runner = keyText ? (SCRIPT_KEYS.get(keyText) ?? null) : null;
            if (key === null && SCRIPT_COMMANDS.has(text) && value === firstUnnamed)
                runner = SCRIPT_COMMANDS.get(text)!;
            if (runner === null) {
                if (key !== null && this.where.has(value.name()!) && !this.scriptAvailable(value.name()!)) {
                    this.note(`script value not available: ${this.text(value.name()!)}`);
                    return null;
                }
                continue;
            }
            if (this.thug2.scripts.has(value.name()!) && this.isLeftOut(this.text(value.name()!).toLowerCase())) {
                // (a THUG2 script left out as a command is left out as a script to run too - THPS4's
                // maybe_auto_load_from_memory_card handed out as an exception script)
                this.note(`left out: ${this.text(value.name()!)}`);
                return null;
            }
            for (const context of contexts) {
                const runs = runner === "same" ? context : runner;
                const v = value.name()!;
                const ok = discover
                    ? this.require(v, runs)
                    : this.scriptAvailable(v) && (!this.thug2.scripts.has(v) || this.thug2ScriptOk(v, [runs]));
                if (!ok) {
                    this.note(`script not available: ${this.text(v)}`);
                    return null;
                }
            }
        }
        // the command itself
        if (this.isThps3Script(name)) {
            if (!this.canBring(name)) {
                this.note(`THPS3 script not brought: ${this.text(name)} (${this.where.get(name)!.label})`);
                return null;
            }
            if (discover) for (const context of contexts) this.require(name, context);
        } else if (this.thug2.cfuncs.has(name) || this.provided.has(name)) {
            // (THUG2's C function, or a script the level's .qb has)
        } else if (this.thug2.scripts.has(name)) {
            // (the parameters passed: its keys and flags; <...> passes the caller's own - not known here)
            this.callArgs = toks.some((t) => t.t === 0x2c)
                ? null
                : args.filter(([, v]) => v.t === 0x16).map(([k, v]) => k ?? v.name()!);
            const ok = this.thug2ScriptOk(name, contexts);
            this.callArgs = null;
            if (!ok) {
                this.note(`THUG2 script needing components the runner hasn't: ${this.text(name)}`);
                return null;
            }
        } else if (this.thug2.members.has(name)) {
            // (a table member: its component; one by name: any of the components that take it)
            const who = this.thug2.members.get(name)!;
            if (![...contexts].every((c) => who.some((w) => this.safe[c].has(w)) || this.byName[c].has(text))) {
                this.note(`member of ${who.length ? who.join("/") : "no converted object"}: ${this.text(name)}`);
                return null;
            }
        } else if ([...contexts].every((c) => this.byName[c].has(text))) {
            // (run by the object itself by name)
        } else {
            this.note(`not in THUG2: ${this.text(name)}`);
            return null;
        }
        // globals THUG2 doesn't have
        for (const [, value] of args) {
            // (not THPS3's exception numbers EX_...: THUG2's events are names - left as names they stay distinct)
            const v = value.name();
            if (
                value.t === 0x16 &&
                this.globals.has(v!) &&
                !this.thug2.globals.has(v!) &&
                !this.where.has(v!) &&
                !this.text(v!).toUpperCase().startsWith("EX_")
            ) {
                this.usedGlobals.add(v!);
            }
        }
        if (discover) {
            for (const [, v] of args)
                if (v.t === 0x16 && this.allNodeNames.has(v.name()!)) this.referenced.add(v.name()!);
        }
        if (discover && text === "create") {
            for (const [k, v] of args) if (k === checksum("name") && v.t === 0x16) this.created.add(v.name()!);
            for (const [k, v] of args) {
                if (k === checksum("prefix") && (v.t === 0x1b || v.t === 0x1c)) {
                    this.createdPrefixes.add(decodeLatin1(qb.rstripNul(v.p)).toLowerCase());
                }
            }
        }
        // names THUG2 calls otherwise
        const out = toks.map((tok) =>
            tok.t === 0x16 && this.renames.has(tok.name()!) ? nameTok(this.renames.get(tok.name()!)!, tok.i) : tok,
        );
        if (this.inheritParams && toks.length === 1 && this.isThps3Script(name))
            out.push(new Tok(0x2c, Buffer.alloc(0), first.i));
        return out;
    }

    onObject(toks: Tok[], contexts: ReadonlySet<Context>, discover: boolean): Tok[] | null {
        // `object:command args` (THPS4): the command checked as run by that object - one of `objects`, there at the
        // start or made by a kept Create (THUG2 runs a missing object's command on object 0, the skater: a member
        // function it hasn't crashes).
        void contexts;
        const target = toks[0]!.name();
        const found = target !== null ? this.objects.get(target) : undefined;
        if (found === undefined) {
            this.note(`object not converted: ${target !== null ? this.text(target) : "<param>"}`);
            return null;
        }
        if (!Array.isArray(found)) throw new TypeError("objects: give [context, created at start] for every object");
        const [context, atStart] = found;
        if (!discover && !atStart && !this.created.has(target!)) {
            this.note(`object never created: ${this.text(target!)}`);
            return null;
        }
        const rest = this.command(toks.slice(2), new Set([context]), discover);
        return rest === null ? null : [...toks.slice(0, 2), ...rest];
    }

    checkedAnims(toks: Tok[]): Tok[] {
        // (animCheck) the statement without its key=<the other game's anim list> parameters, or none when it names
        // an animation THUG2 doesn't load.
        const out = toks.slice(0, 1);
        let k = 1;
        while (k < toks.length) {
            const t = toks[k]!;
            if (
                t.t === 0x16 &&
                k + 2 < toks.length &&
                toks[k + 1]!.t === 0x07 &&
                toks[k + 2]!.t === 0x16 &&
                this.text(t.name()!).toLowerCase().includes("anim")
            ) {
                const v = toks[k + 2]!.name()!;
                // (a single animation: dropped too, even one THUG2 has - it may be in a set the level doesn't load,
                // e.g. THPS6_human_unloadable, and playing an animation that isn't loaded crashes: 0x00482BA0 on a
                // null, seen with THUG1 Hawaii's peds)
                if (!this.thug2.globals.has(v) && !this.thug2.scripts.has(v) && !this.where.has(v)) {
                    this.note(
                        (this.globals.has(v) ? "anim list not in THUG2: " : "anim parameter dropped: ") + this.text(v),
                    );
                    k += 3;
                    continue;
                }
            }
            out.push(t);
            k += 1;
        }
        return out;
    }

    isLeftOut(text: string): boolean {
        return (
            (LEFT_OUT.has(text) && !this.keptIn.has(text)) ||
            this.extraLeftOut.has(text) ||
            (this.leftOutPrefixes.length > 0 && this.leftOutPrefixes.some((p) => text.startsWith(p)))
        );
    }

    objectCall(toks: Tok[], discover: boolean): Tok[] | null {
        // `object:command args` - on the skater (checked as run by the skater), or on one of the level's objects
        // (`objects`: checked as run by it; the statement is then run only while the object is alive - for a missing
        // object THUG2 runs the rest of the statement on object 0, the skater: this.guard asks for the IsAlive).
        const obj = toks[0]!.name()!;
        let context: Context;
        if (this.skaters.has(obj)) {
            context = SKATER;
        } else if (this.objects.has(obj)) {
            context = this.objects.get(obj) as Context;
        } else {
            if (this.allNodeNames.has(obj) && !this.nodeNames.has(obj))
                this.note(`node not converted: ${this.text(obj)}`);
            else this.note(`object call on: ${this.text(obj)}`);
            return null;
        }
        if (toks[2]!.t !== 0x16) {
            this.note("object call form");
            return null;
        }
        const kept = this.command(toks.slice(2), new Set([context]), discover);
        if (kept === null) return null;
        this.guard = this.skaters.has(obj) ? null : toks.slice(0, 1);
        return [...toks.slice(0, 2), ...kept];
    }

    hidesShown(args: readonly Arg[]): boolean {
        // An Invisible / Kill whose prefix= covers sectors that must stay drawn (shownPrefixes), or names a part of
        // them (`prefix=SkWare_Light`, `prefix=SkWare_Light_Wall` or `prefix=SkWare`). name= of one sector is kept (a
        // glass pane's Kill before its Shatter).
        if (!this.shownPrefixes.length) return false;
        for (const [key, value] of args) {
            if (key === null || this.text(key).toLowerCase() !== "prefix") continue;
            if (value.t !== 0x1b && value.t !== 0x1c) continue;
            const prefix = decodeLatin1(qb.rstripNul(value.p)).toLowerCase();
            const shown = this.shownPrefixes.find((p) => p.startsWith(prefix) || prefix.startsWith(p));
            if (shown !== undefined) {
                this.note(`hides sectors kept shown: ${shown}`);
                return true;
            }
        }
        return false;
    }

    withoutUndrawn(toks: Tok[], args: readonly Arg[]): Tok[] | null {
        // Visible / Invisible / Create / Kill / Shatter of geometry nodes with no scene sector (collision-only: THUG2
        // looks the sector up and reads through the null - THPS3 Airport crashed loading): name= one of them -> left
        // out; prefix= matching one -> the statement once per matching node that has a sector (name=), or left out.
        // Create / Kill of collision-only nodes (`collisionOnly`) are kept: those are safe.
        const command = toks[0]!.t === 0x16 ? this.text(toks[0]!.name()!).toLowerCase() : "";
        const safe = (n: number): boolean =>
            !this.undrawn.has(n) || ((command === "create" || command === "kill") && this.collisionOnly.has(n));
        for (const [key, value] of args) {
            if (key === null) continue;
            const keyText = this.text(key).toLowerCase();
            if (keyText === "name" && value.t === 0x16 && !safe(value.name()!)) {
                this.note(`geometry without a scene sector: ${this.text(value.name()!)}`);
                return null;
            }
            if (keyText === "prefix" && (value.t === 0x1b || value.t === 0x1c)) {
                const prefix = decodeLatin1(qb.rstripNul(value.p)).toLowerCase();
                const matching = sortedNumbers(this.allNodeNames).filter((n) =>
                    (this.names.get(n) ?? "").toLowerCase().startsWith(prefix),
                );
                if (matching.every(safe)) return toks;
                this.note(`prefix matching geometry without a scene sector: ${prefix}`);
                const keep = matching.filter((n) => safe(n) && this.nodeNames.has(n));
                if (!keep.length) return null;
                const out: Tok[] = [];
                for (const n of keep) {
                    if (out.length) out.push(new Tok(0x01, Buffer.alloc(0), toks[0]!.i));
                    for (const t of toks) {
                        if (t === value) out.push(nameTok(n, t.i));
                        else if (t.t === 0x16 && t.name() === key) out.push(nameTok(checksum("Name"), t.i));
                        else out.push(t);
                    }
                }
                return out;
            }
        }
        return toks;
    }

    protected never(i: number): Tok[] {
        return [nameTok(checksum("GotParam"), i), nameTok(checksum(NEVER), i)];
    }

    condition(toks: readonly Tok[], contexts: ReadonlySet<Context>, discover: boolean): Tok[] {
        // An if's condition: kept, or always false (`not <condition>`, 0x39, too).
        const content = toks.filter((t) => !EOL.includes(t.t));
        let k = 0;
        let negate = false;
        while (k < content.length && content[k]!.t === 0x39) {
            negate = !negate;
            k += 1;
        }
        const decided = content[k]?.name();
        if (decided === checksum(ALWAYS) || decided === checksum(NEVER)) {
            // (a condition the converter decided - its arguments, if any, don't matter)
            const at = content[k]!.i;
            if ((decided === checksum(ALWAYS)) === negate) return this.never(at);
            return [new Tok(0x39, Buffer.alloc(0), at), ...this.never(at)];
        }
        if (content.length > 1 && content[0]!.t === 0x39 && (this.full || this.strictConditions)) {
            const kept = this.condition(content.slice(1), contexts, discover);
            const last = kept[kept.length - 1]!;
            return last.t === 0x16 && last.name() === checksum(NEVER) ? kept : [content[0]!, ...kept];
        }
        if (content.length > 1 && content[0]!.t === 0x39 && content[1]!.t === 0x16) {
            // `if NOT <command>`: the command checked as any other (left out: the condition is false)
            const kept = this.command(content.slice(1), contexts, discover);
            if (kept === null) return this.never(content[0]!.i);
            return [content[0]!, ...kept];
        }
        if (
            this.strictConditions &&
            content.some((t) => t.t === 0x16 && this.isLeftOut(this.text(t.name()!).toLowerCase()))
        ) {
            // (an expression calling something left out: GoalManager_HasWonGoal inside (... or ...))
            this.note("condition uses something left out");
            return this.never(toks.length ? toks[0]!.i : 0);
        }
        if (
            !this.proximInside &&
            content.length === 1 &&
            content[0]!.t === 0x16 &&
            this.text(content[0]!.name()!).toLowerCase() === "inside"
        ) {
            // (a THPS3 proximity node's script: `if inside` - our proximity ghost passes `inside`)
            return [nameTok(checksum("GotParam"), content[0]!.i), content[0]!];
        }
        this.guard = null;
        let kept = content.length ? this.command(content, contexts, discover) : null;
        if (kept !== null && this.guard !== null) {
            // (a member of one of the level's objects as a condition: false - the object may not be there)
            this.note("condition on an object");
            kept = null;
        }
        this.guard = null;
        if (kept === null) return this.never(toks.length ? toks[0]!.i : 0);
        return kept;
    }

    // -- whole scripts --

    run(roots: Iterable<[number, Context]>): Map<number, Buffer> {
        // roots: (script, context) - the node scripts. Finds every script to bring along and its contexts.
        this.todo = [];
        for (const [name, context] of roots) this.require(name, context);
        while (this.todo.length) {
            const name = this.todo.pop()!;
            this.translate(name, true);
        }
        this.leftOut = new Map();
        this.usedGlobals = new Set();
        const out = new Map<number, Buffer>();
        for (const name of sortedNumbers(this.contexts.keys())) out.set(name, this.translate(name, false));
        return out;
    }

    translate(name: number, discover: boolean): Buffer {
        // The script as THUG2 bytes (0x23 name ... 0x24).
        const body = this.where.get(name)!.scripts.get(name)!;
        const contexts = this.contexts.get(name)!;
        const items: Item[] = [];
        const stmts = statements(body);
        const ifs: [number, boolean][] = [];
        const switches: Switch[] = [];
        const loops: boolean[] = []; // per open begin: whether a statement of it was left out
        let counter = 0;
        const tokItems = (toks: readonly Tok[]): void => {
            items.push(...Translator.tokenItems(toks));
        };
        stmts.forEach((st, n) => {
            const content = st.filter((t) => !EOL.includes(t.t));
            const eol = st.filter((t) => EOL.includes(t.t)).slice(-1);
            const orig = st[0]!.i;
            if (n === 0 || !content.length) {
                // (the default-parameters line, or an empty line)
                for (const t of st) items.push({ kind: "tok", tok: t, orig: t.i });
                return;
            }
            const head = content[0]!;
            if (head.t === 0x25) {
                const cond = this.condition(content.slice(1), contexts, discover);
                counter += 1;
                ifs.push([counter, false]);
                items.push({ kind: "if", n: counter, orig });
                tokItems(cond);
            } else if (head.t === 0x26) {
                if (!ifs.length) throw new RangeError(`${this.text(name)}: else without if`);
                ifs[ifs.length - 1]![1] = true;
                items.push({ kind: "else", n: ifs[ifs.length - 1]![0], orig });
                items.push({ kind: "label", key: `else:${ifs[ifs.length - 1]![0]}`, orig });
            } else if (head.t === 0x28) {
                if (!ifs.length) throw new RangeError(`${this.text(name)}: endif without if`);
                const [number, hasElse] = ifs.pop()!;
                if (!hasElse) items.push({ kind: "label", key: `else:${number}`, orig });
                items.push({ kind: "label", key: `end:${number}`, orig });
                items.push({ kind: "tok", tok: head, orig });
            } else if ((head.t === 0x3e || head.t === 0x3f) && content.length > 1 && content[1]!.t === 0x49) {
                // (a THUG2-format case / default - THAW's: its jump already there, kept as a short jump)
                tokItems(content);
            } else if (RANDOMS.includes(head.t)) {
                items.push({ kind: "random", tok: head, orig });
                return;
            } else if (head.t === 0x2e) {
                items.push({ kind: "jump", tok: head, orig });
                return;
            } else if (head.t === 0x3c) {
                // switch <value> (THPS4's, THUG1's: no jumps - THUG2's cases jump to the next case, their bodies past
                // the endswitch: made here, as the ifs'; THAW's cases carry theirs - above)
                if (switches.length) switches[switches.length - 1]!.body += 1;
                counter += 1;
                switches.push({ id: counter, k: 0, open: null, body: 0 });
                tokItems(content);
                for (const t of eol) items.push({ kind: "tok", tok: t, orig: t.i });
                return;
            } else if (head.t === 0x3e || head.t === 0x3f || head.t === 0x3d) {
                if (!switches.length) throw new RangeError(`${this.text(name)}: case outside a switch`);
                const sw = switches[switches.length - 1]!;
                Translator.closeCase(sw, items, orig);
                if (head.t === 0x3e) {
                    sw.k += 1;
                    items.push({ kind: "case", id: sw.id, k: sw.k, orig });
                    tokItems(content.slice(1));
                    sw.open = "case";
                    sw.body = 0;
                } else if (head.t === 0x3f) {
                    items.push({ kind: "default", n: sw.id, orig });
                    sw.open = "default";
                    sw.body = 0;
                } else {
                    items.push({ kind: "label", key: `end:${sw.id}`, orig });
                    items.push({ kind: "tok", tok: head, orig });
                    items.push({ kind: "label", key: `past:${sw.id}`, orig });
                    switches.pop();
                }
                for (const t of eol) items.push({ kind: "tok", tok: t, orig: t.i });
                return;
            } else if (CONTROL.includes(head.t)) {
                if (head.t === 0x20) {
                    loops.push(false);
                } else if (head.t === 0x21 && loops.length) {
                    const emptied = loops.pop()!;
                    if (emptied && content.length === 1) {
                        // (an endless loop that lost statements - its waits may have gone with them: THUG1's
                        // `while { random { Obj_PlayAnim ... } Obj_WaitAnimFinished }` became a loop that never yields
                        // and hung the game loading; a frame's wait keeps it from spinning)
                        tokItems([
                            nameTok(checksum("Wait"), orig),
                            new Tok(0x17, pack("<i", 1), orig),
                            nameTok(checksum("frame"), orig),
                            new Tok(0x01, Buffer.alloc(0), orig),
                        ]);
                    }
                }
                tokItems(content);
            } else {
                this.guard = null;
                const kept = this.command(content, contexts, discover);
                if (kept === null) {
                    loops.fill(true);
                    return;
                }
                if (this.guard !== null) {
                    // (a member of one of the level's objects: only while it is alive. A node's object: `IsAlive
                    // name=<node>`. An object the script was handed (`<ObjID>:member`): `ObjectExists id=<ObjID>` -
                    // the object tracker's lookup, the one the call itself makes (THUG2's own scripts' guard).
                    // IsAlive would look the id up as a node (0x005A4EF0 -> GetNode, which asserts): THAW's
                    // `Obj_GetID` `<ObjID>:Obj_GetPosition` run by the skater hands it 0 - IsAlive crashed on the
                    // missing node (Hollywood's scaffolding / roof shatter scripts).)
                    counter += 1;
                    const number = counter;
                    items.push({ kind: "if", n: number, orig });
                    tokItems([...Translator.guardCondition(this.guard, orig), new Tok(0x01, Buffer.alloc(0), orig)]);
                    tokItems([...kept, new Tok(0x01, Buffer.alloc(0), orig)]);
                    items.push({ kind: "label", key: `else:${number}`, orig });
                    items.push({ kind: "label", key: `end:${number}`, orig });
                    items.push({ kind: "tok", tok: new Tok(0x28, Buffer.alloc(0), orig), orig });
                    this.guard = null;
                } else {
                    tokItems(kept);
                }
            }
            if (switches.length) switches[switches.length - 1]!.body += 1;
            for (const t of eol) items.push({ kind: "tok", tok: t, orig: t.i });
        });
        if (ifs.length) throw new RangeError(`${this.text(name)}: if without endif`);
        if (switches.length) throw new RangeError(`${this.text(name)}: switch without endswitch`);
        return this.assemble(name, items);
    }

    // The condition guarding `object:member` (this.guard: the object's tokens - a node's name, or `<param>`).
    static guardCondition(guard: readonly Tok[], orig: number): Tok[] {
        const byId = guard[0]!.t === 0x2d;
        return [
            nameTok(checksum(byId ? "ObjectExists" : "IsAlive"), orig),
            nameTok(checksum(byId ? "id" : "name"), orig),
            new Tok(0x07, Buffer.alloc(0), orig),
            ...guard,
        ];
    }

    static closeCase(sw: Switch, items: Item[], orig: number): void {
        // The end of a case's body: a statement that does nothing when it has none (THUG2 takes case labels right
        // after a matched one as fall-through labels), the jump past the endswitch, and where the case's jump to the
        // next case lands.
        if (sw.open !== "case") return;
        if (sw.body === 0) {
            items.push({ kind: "tok", tok: nameTok(checksum("GotParam"), orig), orig });
            items.push({ kind: "tok", tok: nameTok(checksum(NEVER), orig), orig });
            items.push({ kind: "tok", tok: new Tok(0x01, Buffer.alloc(0), orig), orig });
        }
        items.push({ kind: "swjump", n: sw.id, orig });
        items.push({ kind: "label", key: `next:${sw.id}:${sw.k}`, orig });
    }

    static tokenItems(toks: readonly Tok[]): Item[] {
        // A statement's tokens as output items (an argument random's tokens re-encoded, jumps made again).
        return toks.map((t) => {
            const kind = RANDOMS.includes(t.t)
                ? "random"
                : t.t === 0x2e
                  ? "jump"
                  : t.t === 0x49 && t.targets && t.targets.length
                    ? "short"
                    : "tok";
            return { kind, tok: t, orig: t.i };
        });
    }

    assemble(name: number, items: readonly Item[]): Buffer {
        const head = Buffer.concat([Buffer.from([0x23, 0x16]), pack("<I", name)]);
        const size: Record<string, number> = {
            if: 3,
            else: 3,
            jump: 5,
            label: 0,
            case: 4,
            default: 4,
            swjump: 3,
            short: 3,
        };
        const positions: number[] = [];
        let pos = head.length;
        for (const item of items) {
            positions.push(pos);
            if (item.kind === "tok") pos += item.tok.encode().length;
            else if (item.kind === "random") pos += 5 + 6 * item.tok.targets!.length;
            else pos += size[item.kind]!;
        }
        const end = pos;
        const labels = new Map<string, number>();
        items.forEach((item, k) => {
            if (item.kind === "label") labels.set(item.key, positions[k]!);
        });
        const label = (key: string): number => {
            const value = labels.get(key);
            if (value === undefined) throw new RangeError(`no label ${key}`); // (KeyError)
            return value;
        };
        const at = (target: number): number => {
            // where the first output item from the target token on lands
            for (let k = 0; k < items.length; k++) if (items[k]!.orig >= target) return positions[k]!;
            return end;
        };
        const out: Buffer[] = [head];
        items.forEach((item, k) => {
            const here = positions[k]!;
            switch (item.kind) {
                case "tok":
                    out.push(item.tok.encode());
                    break;
                case "if":
                    out.push(Buffer.from([0x47]), pack("<H", label(`else:${item.n}`) - (here + 1)));
                    break;
                case "else":
                    out.push(Buffer.from([0x48]), pack("<H", label(`end:${item.n}`) - (here + 1)));
                    break;
                case "jump":
                    out.push(Buffer.from([0x2e]), pack("<i", at(item.tok.targets![0]!) - (here + 5)));
                    break;
                case "case":
                    // 3E 49 <to the next case / default / endswitch> (the offset from its own field, as the ifs')
                    out.push(Buffer.from([0x3e, 0x49]), pack("<H", label(`next:${item.id}:${item.k}`) - (here + 2)));
                    break;
                case "default":
                    out.push(Buffer.from([0x3f, 0x49]), pack("<H", label(`end:${item.n}`) - (here + 2)));
                    break;
                case "swjump":
                    out.push(Buffer.from([0x49]), pack("<H", label(`past:${item.n}`) - (here + 1)));
                    break;
                case "short":
                    // (a THUG2-format switch's own short jump - THAW's: its offset from the 0x49 itself)
                    out.push(Buffer.from([0x49]), pack("<H", at(item.tok.targets![0]!) - item.tok.adjust - here));
                    break;
                case "random": {
                    const targets = item.tok.targets!;
                    const n = targets.length;
                    const weights = item.tok.weights?.length ? item.tok.weights : new Array<number>(n).fill(1);
                    out.push(Buffer.from([item.tok.t]), pack("<I", n), pack(`<${n}H`, ...weights));
                    targets.forEach((target, m) => {
                        const field = here + 5 + 2 * n + 4 * m;
                        out.push(pack("<I", at(target) - (field + 4)));
                    });
                    break;
                }
                case "label":
                    break;
            }
        });
        out.push(Buffer.from([0x24, 0x01]));
        return Buffer.concat(out);
    }

    globalsNs(): string {
        // The THPS3 globals the scripts use (THUG2 hasn't them), as NeverScript text.
        const lines: string[] = [];
        for (const value of sortedNumbers(this.usedGlobals)) {
            const v = this.globals.get(value)!;
            if (hasLocal(v)) {
                // (NeverScript doesn't read 'local strings': THPS4's trick structures' names)
                continue;
            }
            try {
                lines.push(`${qb.nameText(value, this.names)} = ${qb.toNs(v, this.names)}`);
            } catch (e) {
                if (e instanceof TypeError) continue;
                throw e;
            }
        }
        return lines.join("\n") + (lines.length ? "\n" : "");
    }
}

function hasLocal(value: qb.QbValue): boolean {
    if (value instanceof qb.Local) return true;
    if (Array.isArray(value)) return value.some(hasLocal);
    if (value instanceof qb.Struct) return value.entries.some(([, v]) => hasLocal(v));
    return false;
}

export function appendScripts(
    compiled: Buffer,
    scripts: ReadonlyMap<number, Uint8Array>,
    names: ReadonlyMap<number, string>,
): Buffer {
    // A NeverScript-compiled .qb (ends with 0x00) with the scripts' bytes added, and the names of the checksums they
    // use (0x2B entries: for reading the file back; the game notes them).
    if (!compiled.length || compiled[compiled.length - 1] !== 0)
        throw new RangeError("compiled .qb doesn't end with 0x00");
    // (the scripts before the file's checksum names, as NeverScript writes a file: its decompiler stops at them)
    let namesAt = compiled.length - 1;
    for (const [off, t] of qb.tokens(compiled, true)) {
        if (t === 0x2b || t === 0x00) {
            namesAt = off;
            break;
        }
    }
    const out: Buffer[] = [compiled.subarray(0, namesAt)];
    const tail = compiled.subarray(namesAt, compiled.length - 1);
    const used = new Set<number>();
    for (const code of scripts.values()) {
        out.push(Buffer.from(code));
        for (const [t, p] of plainTokens(code)) if (t === 0x16) used.add(u32(p));
    }
    for (const value of sortedNumbers(used)) {
        const text = names.get(value);
        if (text && checksum(text) === value) {
            out.push(Buffer.from([0x2b]), pack("<I", value), encodeLatin1(text), Buffer.from([0]));
        }
    }
    out.push(tail, Buffer.from([0]));
    return Buffer.concat(out);
}

export function doesSomething(code: Uint8Array): boolean {
    // Whether a translated script (Translator.run's bytes) has a statement left besides if / else and their
    // conditions: a name starting a line (after a line end, not after an `if`).
    let previous: number | null = null;
    for (const [, t] of qb.tokens(Buffer.concat([code.subarray(6), Buffer.from([0])]), true)) {
        if (t === 0x00) break;
        if (t === 0x16 && previous !== null && EOL.includes(previous)) return true;
        previous = t;
    }
    return false;
}

export function* plainTokens(code: Uint8Array): Generator<[number, Buffer]> {
    // (token, payload) of THUG2-format code (with the random weights).
    for (const [, t, p] of qb.tokens(Buffer.concat([code, Buffer.from([0])]), true)) {
        if (t === 0x00) break;
        if (t === 0x47 || t === 0x48 || t === 0x49) continue;
        yield [t, p];
    }
}
