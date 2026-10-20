// NOT ORIGINAL - a THPS4 level's scripts, game objects, waypoints, peds and vehicles for free skate in THUG2
// (tools/thps4_levels/thps4_levels.ts). Nothing of either game is stored: everything is read from the user's files.
//
// THPS4's node arrays are close to THUG2's (the same classes and keys, legacy Position); its compiled scripts too:
// tools/level_port/script_port.ts translates them (THPS4's long-form if / else, its random tokens without weights,
// its switch without jumps, `object:command`). What's made of the nodes:
//   LevelObject        THUG2's LevelObject (THPS4's sectors are in the object's own space, as THUG2's): moved by its
//                      trigger script (run by the object).
//   GameObject Ghost   THUG2's ghost game object (model="none"): a script's controller.
//   GameObject (model) the game modes' flags (TRG_CTF_Red / _Base, TRG_Flag_Red...: capture the flag, the lobby's
//                      team flags) kept - THPS4's are THUG2's own (Type Flag_Red, gameobjects\flags\ models THUG2 has,
//                      trigger scripts calling Team_Flag / Team_Flag_Base, which THUG2 has), and THUG2's net\ctf.qb
//                      and SK4_Scripts create them by those names; the goal pick-ups (money, letters, icons, tapes)
//                      left out - goals aren't ported.
//   Vehicle            Car_Generic01 cars: THUG2's own cars (peds_cars.ts); the others (trams, rides, trains, cranes:
//                      moved by their scripts) LevelObjects of their THPS4 model (vehicle_models: the model's sectors
//                      in the level's scene, as one sector of the node's name in the object's own space).
//   Pedestrian         walkers (Obj_FollowPathLinked): THUG2's ped AI walking (peds_cars.ts); the others standing
//                      THUG2 peds at their place (THUG2's ped AI, a profile like THPS4's model); animals left out.
//   Waypoint           kept (the objects' paths), ped paths as peds_cars.ts makes them.
//   ProximNode         left out (THUG2's proximity nodes are boxes of the scene - BoundingBox - not THPS4's radius
//                      spheres); their trigger scripts run once at the start (they swap objects in and out).
//   ParticleEmitter    left out (THPS4's script-driven emitters: no THUG2 counterpart).
// The scripts brought along: the nodes' trigger scripts (sectors' and rails' run by the skater, objects' by the
// object) and THPS4's level structure's setup_script / startup_script, run from the level's own setup script.

import * as fs from "node:fs";
import * as path from "node:path";
import * as nodes from "../level_port/nodes.ts";
import * as pedsCars from "../level_port/peds_cars.ts";
import * as qb from "../level_port/qb.ts";
import { checksum, Name, Struct, Vector } from "../level_port/qb.ts";
import * as scriptPort from "../level_port/script_port.ts";
import * as tex from "../level_port/tex.ts";
import { pyJoin, pyMax, pyMin, pyParseInt, pyRepr, pySum, pyTruthy } from "../lib/py.ts";
import { pack } from "../lib/struct.ts";
import * as thps4 from "./thps4.ts";

// THPS4's script files whose scripts may be brought along (the level's own files: "level"; the designers' shared
// files). Goals, menus, peds' and the skater's scripts are THPS4's game, not the level's.
export const SOURCES = [
    "ajc_scripts.qb",
    "alf_scripts.qb",
    "bdj_scripts.qb",
    "cjr_scripts.qb",
    "cpf_scripts.qb",
    "jku_scripts.qb",
    "js_scripts.qb",
    "sk4_scripts.qb",
];
// THPS4's commands left out although THUG2 has them (besides script_port.LEFT_OUT) - goals, the skater's career,
// cameras, THPS4's panels: a port change (PC-THPS4-8), its list in tools/compat/thugpro/thps4_levels.json port_changes, given
// to convert() as options.leftOut by thps4_levels.ts.
// Member functions the skater may run besides script_port's (the gap / trigger scripts of sectors and rails ask it
// `if onground`, `if skitching`, `if doingtrick`...): THUG2's skater has these components.
export const COMPONENTS = { [scriptPort.SKATER]: ["skaterstate", "skatercorephysics"] };
// The skater's member functions by name (no table entry): TriggerType - THUG2's own LA trigger scripts ask it too
// (`if TriggerType <trigger_array>`).
export const BY_NAME = { [scriptPort.SKATER]: ["TriggerType"] };
// THPS4's trigger types (skutils.qb's TRIGGER_* - `if TriggerType TRIGGER_LIP_ON`, `if TriggerType [A B]`) as THUG2's
// trigger arrays (skutils.qb's GetTriggerArray: each an array of conditions that must all hold - skate 1, jump 2,
// onto 3, off 4, offedge 5, lip 7, land 8, bonk 9; -1 none, i.e. any; an array of them: any of them). THUG2's
// TriggerType reads its first argument as such an array: THPS4's bare name crashed it (null read, 0x004349B2).
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

export function triggerType(toks: scriptPort.Tok[]): scriptPort.Tok[] | null {
    // `TriggerType <a THPS4 trigger type or a list of them>` -> `TriggerType [[...] ...]`, or null (left out).
    const byValue = new Map(Object.entries(TRIGGER_ARRAYS).map(([k, v]) => [checksum(k), v]));
    const values: (readonly number[])[] = [];
    for (const t of toks.slice(1)) {
        if (t.t === 0x16) {
            const found = byValue.get(t.name()!);
            if (found === undefined) return null;
            values.push(found);
        } else if (t.t !== 0x05 && t.t !== 0x06 && t.t !== 0x09) {
            return null;
        }
    }
    if (!values.length) return null;
    const i = toks[0]!.i;
    const empty = Buffer.alloc(0);
    const out = [toks[0]!, new scriptPort.Tok(0x05, empty, i)];
    for (const conditions of values) {
        out.push(new scriptPort.Tok(0x05, empty, i));
        out.push(...conditions.map((c) => new scriptPort.Tok(0x17, pack("<i", c), i)));
        out.push(new scriptPort.Tok(0x06, empty, i));
    }
    out.push(new scriptPort.Tok(0x06, empty, i));
    return out;
}

// MakeSkaterGoto (script_port's LEFT_OUT: goals and cutscenes send the skater into their own scripts) kept for the
// skater scripts THUG2 has under THPS4's names - DropIn (a restart on a ramp's lip: THPS4's restarts' trigger scripts
// drop the skater in, as THUG2's own skater script does) and StartSkating1 -, any other target left out.
export const KEPT_IN = ["MakeSkaterGoto"];
export const SKATER_GOTO: ReadonlySet<number> = new Set(["DropIn", "StartSkating1"].map(checksum));

export function makeSkaterGoto(toks: scriptPort.Tok[]): scriptPort.Tok[] | null {
    const target = toks[1];
    if (toks.length !== 2 || target === undefined || target.t !== 0x16 || !SKATER_GOTO.has(target.name()!)) return null;
    return toks;
}

// A translated script (THUG2 code: script header, statements, end) with no statement left.
export function isEmptyScript(code: Uint8Array): boolean {
    let tokensLeft = 0;
    for (const [t] of scriptPort.plainTokens(code)) if (![0x01, 0x02, 0x23, 0x24].includes(t)) tokensLeft += 1;
    return tokensLeft <= 1; // (the script's name)
}

export const REWRITES = new Map([
    [checksum("TriggerType"), triggerType],
    [checksum("MakeSkaterGoto"), makeSkaterGoto],
]);
// The made peds' trigger script (THUG2's ped AI, standing).
export const PED_STAND = "thps4_ped_stand";

// The level's own setup script (runs THPS4's setup / startup scripts): not <level>_setup - with THPS4's level names
// (alc) that is THPS4's own setup script's name (its Levels.qb: setup_script=alc_setup), which it spawns.
export const setupName = (level: string): string => `${level}_ported_setup`;
// The game modes' flags (see the top): THPS4's own nodes under the names THUG2's scripts look for, with THUG2's
// flag models. (As tools/thaw_levels/thaw_scripts.ts keeps THAW's.)
export const FLAG_MODELS = "gameobjects\\flags\\";
export const GAME_MODE_NAMES = /^trg_(ctf_(red|blue|green|yellow)(_base)?|flag_(red|blue|green|yellow))$/i;
const FLAG_KEYS = new Set(["Type", "model", "lod_dist1", "lod_dist2"].map(checksum));
// The animals THUG2 has itself (THPS4's Type: THUG2's skeleton and animations for it, as THUG2's own levels set its
// pigeons up - Boston's TRG_Pigeon: Class=Pedestrian Type=Anl_Pigeon NoPedLogic dont_target SkeletonName=Anl_Pigeon
// AnimName=animload_anl_pigeon, the same model path as THPS4's): kept with THPS4's trigger script (Pigeon_Generic,
// THUG2 has it), when THUG2 has the model. The other animals (THPS4's own models and skeletons) stay left out.
export const ANIMALS: Readonly<Record<string, { skeleton: string; anims: string }>> = {
    anl_pigeon: { skeleton: "Anl_Pigeon", anims: "animload_anl_pigeon" },
};
// How many kinds of left-out statements the report lists.
export const REPORTED = pyParseInt(process.env.THPS4_SCRIPTS_REPORTED ?? "15");
export const PED_SCRIPTS = `script ${PED_STAND} {\n    Ped_InitAI stand \n}\n`;

function read(file: string): Buffer {
    return fs.readFileSync(file);
}

function key(names: Map<number, string>, text: string): Name {
    const value = checksum(text);
    if (!names.has(value)) names.set(value, text);
    return new Name(value);
}

const nameOf = (node: Struct): number | null => {
    const name = node.get("Name");
    return name instanceof Name ? name.value : null;
};

const isInt = (v: unknown): v is number => typeof v === "number" && Number.isInteger(v);

// (Python's `x.get("Links") or []`: the links when there are some)
function linksOf(node: Struct): qb.QbValue[] {
    const links = node.get("Links");
    if (!pyTruthy(links)) return [];
    if (!Array.isArray(links)) throw new TypeError("Links is not a list");
    return links;
}

export class Port {
    // What bringing THPS4 levels' scripts along needs, read once: THUG2's commands (script_port.Thug2), THPS4's script
    // files (script_port.Source), THUG2's assets (profiles, models).
    readonly thug2: scriptPort.Thug2;
    readonly assets: pedsCars.Thug2Assets;
    readonly vehicles: VehicleModels;
    readonly sources: scriptPort.Source[];

    constructor(thps4Dir: string, thug2Dir: string) {
        this.thug2 = new scriptPort.Thug2(thug2Dir);
        this.assets = new pedsCars.Thug2Assets(thug2Dir);
        this.vehicles = new VehicleModels(thps4Dir);
        const folder = path.join(thps4Dir, "Game", "data", "scripts");
        this.sources = fs
            .readdirSync(folder)
            .sort()
            .filter((f) => f.toLowerCase().endsWith(".qb"))
            .map((f) => new scriptPort.Source(read(path.join(folder, f)), f.toLowerCase()));
    }
}

export function profile(model: string, assets: pedsCars.Thug2Assets): string {
    // A THUG2 ped profile like THPS4's ped type: its pro, a guard, a worker, a woman, else any man.
    const text = model.toLowerCase();
    if (text.startsWith("pedpro_")) {
        for (const candidate of [`PedPro_${model.slice(7)}`, `PedPro_${model.slice(7)}_skateboard`]) {
            if (assets.profileExists(candidate)) return candidate;
        }
    }
    for (const [part, found] of [
        ["security", "Ped_Security_Guard_01"],
        ["guard", "Ped_Security_Guard_01"],
        ["cop", "Ped_Security_Guard_01"],
        ["welder", "Ped_Construction_Worker_01"],
        ["worker", "Ped_Construction_Worker_01"],
        ["female", "random_female_profile"],
        ["girl", "random_female_profile"],
        ["skank", "random_female_profile"],
        ["skateboard", "Ped_Skateboard_a"],
        ["grommit", "Ped_Skateboard_a"],
    ] as const) {
        if (text.includes(part) && assets.profileExists(found)) return found;
    }
    return "random_male_profile";
}

type Trigger = [number, number, scriptPort.Context]; // (node name, script, context)

export class Nodes {
    // The THPS4 node array's nodes made THUG2's (see the top); kept whole by nodes.convertNodes (asIs), placeholders
    // (Class=LeftOut) where left out, so the Links (indices) stay right.
    readonly array: readonly qb.QbValue[];
    readonly names: Map<number, string>;
    readonly sectors: ReadonlySet<number>;
    readonly assets: pedsCars.Thug2Assets;
    readonly collides: ReadonlySet<number>; // the collision objects' names (CollisionMode=Geometry needs one)
    readonly movingNames: ReadonlySet<number>; // nodes peds_cars.ts made again (peds, cars)
    readonly pedPaths: ReadonlySet<number>; // array indices of the waypoints peds_cars.ts made again
    readonly vehicleSectors: ReadonlySet<number>; // vehicles made LevelObjects (their model's sector in the scene)
    readonly ids = new Set<Struct>();
    readonly triggers: Trigger[] = [];
    readonly objects = new Map<number, [scriptPort.Context, boolean]>(); // node name -> (context, created at start)
    readonly proximScripts: number[] = [];
    readonly gameModeObjects = new Set<number>(); // the game-mode flags (created by THUG2's game modes, not at start)
    readonly counts = new Map<string, number>();
    private index: Map<qb.QbValue, number> | null = null;

    constructor(
        array: readonly qb.QbValue[],
        names: Map<number, string>,
        sectors: ReadonlySet<number>,
        movingNames: ReadonlySet<number>,
        pedPaths: ReadonlySet<number>,
        assets: pedsCars.Thug2Assets,
        vehicleSectors: ReadonlySet<number> = new Set(),
        collides: ReadonlySet<number> = new Set(),
    ) {
        this.array = array;
        this.names = names;
        this.sectors = sectors;
        this.assets = assets;
        this.collides = collides;
        this.movingNames = movingNames;
        this.pedPaths = pedPaths;
        this.vehicleSectors = vehicleSectors;
    }

    count(what: string): void {
        this.counts.set(what, (this.counts.get(what) ?? 0) + 1);
    }

    leftOut(what: string | null): Struct {
        if (what) this.count(what);
        return new Struct([[key(this.names, "Class"), key(this.names, "LeftOut")]]);
    }

    private links(node: Struct): number[] {
        const links = node.get("Links");
        if (!Array.isArray(links)) return [];
        const out = links.filter(
            (v): v is number =>
                isInt(v) &&
                0 <= v &&
                v < this.array.length &&
                this.array[v] instanceof Struct &&
                nodes.classOf(this.array[v] as Struct, this.names) === "waypoint",
        );
        return [...new Set(out)];
    }

    private made(node: Struct, entries: qb.Entry[], context: scriptPort.Context | null, script = true): Struct {
        const s = new Struct(entries);
        this.ids.add(s);
        const name = node.get("Name");
        const trigger = node.get("TriggerScript");
        if (script && trigger instanceof Name && name instanceof Name && context !== null) {
            this.triggers.push([name.value, trigger.value, context]);
        }
        if (name instanceof Name && context !== null)
            this.objects.set(name.value, [context, node.hasFlag("CreatedAtStart")]);
        return s;
    }

    private base(node: Struct, cls: string, keys: readonly string[] = []): qb.Entry[] {
        // Position, Angles, Name, Class, flags, the listed keys as they are; Links to waypoints.
        const n = this.names;
        const angles = node.get("Angles");
        const entries: qb.Entry[] = [
            [key(n, "Position"), node.get("Position")!],
            [key(n, "Angles"), pyTruthy(angles) ? angles! : new Vector("vector", [0.0, 0.0, 0.0])],
            [key(n, "Name"), node.get("Name")!],
            [key(n, "Class"), key(n, cls)],
        ];
        for (const f of ["CreatedAtStart", "AbsentInNetGames"]) if (node.hasFlag(f)) entries.push([null, key(n, f)]);
        for (const k of keys) {
            const value = node.get(k);
            if (value !== undefined) entries.push([key(n, k), value]);
        }
        const links = this.links(node);
        if (links.length) entries.push([key(n, "Links"), links]);
        return entries;
    }

    private collision(node: Struct): qb.Entry[] {
        const name = node.get("Name");
        if (name instanceof Name && this.collides.has(name.value)) {
            return [[key(this.names, "CollisionMode"), key(this.names, "Geometry")]];
        }
        return [];
    }

    convert(node: Struct): Struct | null {
        // The THUG2 node, a placeholder, or null (nodes.convertNodes's own: restarts, rails, sectors, crowns).
        const n = this.names;
        const cls = nodes.classOf(node, n);
        const name = node.get("Name");
        if (["restart", "railnode", "levelgeometry", "genericnode"].includes(cls)) return null;
        if (!(name instanceof Name) || !(node.get("Position") instanceof Vector)) {
            return this.leftOut("nodes without a name or position");
        }
        const objectEntries = (): qb.Entry[] => [
            [key(n, "Type"), key(n, "Normal")],
            [key(n, "SuspendDistance"), 0],
            [key(n, "lod_dist1"), 10000],
            [key(n, "lod_dist2"), 10001],
        ];
        if (cls === "levelobject") {
            if (!this.sectors.has(name.value)) return this.leftOut("level objects without a sector");
            this.count("level objects");
            return this.made(
                node,
                [...this.base(node, "LevelObject"), ...this.collision(node), ...objectEntries()],
                scriptPort.OBJECT,
            );
        }
        if (cls === "gameobject") {
            const model = node.get("model");
            if (
                typeof model === "string" &&
                model.toLowerCase().startsWith(FLAG_MODELS) &&
                GAME_MODE_NAMES.test(qb.nameText(name.value, n)) &&
                this.assets.modelExists(model)
            ) {
                // (kept although not created at the start: THUG2's game modes create them by name)
                this.count("game-mode flags");
                this.gameModeObjects.add(name.value);
                return this.made(
                    node,
                    [
                        ...this.base(node, "GameObject"),
                        ...node.entries.filter(([k]) => k !== null && FLAG_KEYS.has(k.value)),
                        [null, key(n, "NeverSuspend")],
                    ],
                    scriptPort.OBJECT,
                );
            }
            if (nodes.typeOf(node, n) !== "ghost") return this.leftOut("goal objects");
            this.count("script objects");
            return this.made(
                node,
                [
                    ...this.base(node, "GameObject"),
                    [key(n, "Type"), key(n, "Ghost")],
                    [null, key(n, "NeverSuspend")],
                    [key(n, "model"), "none"],
                    [key(n, "lod_dist1"), 400],
                    [key(n, "lod_dist2"), 401],
                ],
                scriptPort.OBJECT,
            );
        }
        if (cls === "waypoint") {
            const at = this.indexOf(node);
            if (at !== undefined && this.pedPaths.has(at)) return this.leftOut(null);
            this.count("waypoints");
            return this.made(node, this.base(node, "Waypoint"), scriptPort.OBJECT);
        }
        if (cls === "vehicle") {
            if (this.movingNames.has(name.value)) return this.leftOut(null);
            if (this.vehicleSectors.has(name.value)) {
                this.count("vehicles (their models)");
                return this.made(
                    node,
                    [...this.base(node, "LevelObject"), ...this.collision(node), ...objectEntries()],
                    scriptPort.OBJECT,
                );
            }
            return this.leftOut("vehicles without a model");
        }
        if (cls === "pedestrian") {
            if (this.movingNames.has(name.value)) return this.leftOut(null);
            const kind = nodes.typeOf(node, n);
            const animal = ANIMALS[kind];
            const model = node.get("model");
            if (animal && typeof model === "string" && this.assets.modelExists(model)) {
                this.count("animals (THUG2's)");
                return this.made(
                    node,
                    [
                        ...this.base(node, "Pedestrian"),
                        [key(n, "Type"), node.get("Type")!],
                        [null, key(n, "NoPedLogic")],
                        [null, key(n, "dont_target")],
                        [key(n, "SkeletonName"), key(n, animal.skeleton)],
                        [key(n, "AnimName"), key(n, animal.anims)],
                        [key(n, "model"), model],
                        [key(n, "SuspendDistance"), 0],
                        [key(n, "lod_dist1"), 100],
                        [key(n, "lod_dist2"), 120],
                    ],
                    scriptPort.PED,
                );
            }
            if (kind.startsWith("anl_")) return this.leftOut("animals");
            this.count("standing peds");
            const type = node.get("Type");
            if (!(type instanceof Name)) throw new TypeError("a pedestrian's Type is not a name");
            let entries: qb.Entry[] = [
                ...this.base(node, "Pedestrian"),
                [key(n, "Type"), key(n, "Ped_From_Profile")],
                [key(n, "PedAI_Type"), key(n, "Stand")],
                [key(n, "skeletonName"), key(n, "THPS6_human")],
                [key(n, "AnimName"), key(n, "animload_THPS6_human")],
                [key(n, "profile"), key(n, profile(n.get(type.value) ?? "", this.assets))],
                [key(n, "SuspendDistance"), 10000],
                [key(n, "lod_dist1"), 200],
                [key(n, "lod_dist2"), 201],
                [key(n, "TriggerScript"), key(n, PED_STAND)],
            ];
            const links = checksum("Links");
            entries = entries.filter(([k]) => !(k !== null && k.value === links));
            return this.made(node, entries, scriptPort.NONE, false);
        }
        if (cls === "proximnode") {
            const script = node.get("TriggerScript");
            if (script instanceof Name) this.proximScripts.push(script.value);
            return this.leftOut("proximity nodes");
        }
        return this.leftOut(`${cls || "unknown"} nodes`);
    }

    indexOf(node: Struct): number | undefined {
        if (this.index === null) {
            this.index = new Map();
            this.array.forEach((x, k) => {
                this.index!.set(x, k);
            });
        }
        return this.index.get(node);
    }
}

// --- vehicles' models ---------------------------------------------------------------------------------------------

function boundsOf(positions: readonly number[][]): [number[], number[]] {
    const lo = [0, 1, 2].map((k) => pyMin(positions.map((p) => p[k]!)));
    const hi = [0, 1, 2].map((k) => pyMax(positions.map((p) => p[k]!)));
    const center = [0, 1, 2].map((k) => (lo[k]! + hi[k]!) / 2);
    // (Python: sum(...) - compensated (pySum) - of (p[k] - c[k]) ** 2, then ** 0.5)
    const radius = Math.sqrt(
        pyMax(
            positions.map((p) =>
                pySum(
                    [0, 1, 2].map((k) => {
                        const d = p[k]! - center[k]!;
                        return d * d;
                    }),
                ),
            ),
        ),
    );
    return [
        [...lo, ...hi],
        [...center, radius],
    ];
}

export function mergeSectors(sectorsIn: readonly thps4.Sector[], name: number): [thps4.Sector, boolean] | null {
    // A model's sectors as one sector of the checksum `name` (a THUG2 LevelObject draws the one sector of its name):
    // their vertex arrays one after another, the strips moved along. Skinned models (THPS4's cars and trams: a
    // skeleton - shadow quad, body, four wheels) keep only the body, the largest, turned from their Z-up bone space
    // to the level's Y-up (x, z, -y) - the wheels sit at bones not converted. null when nothing is left.
    let sectors = sectorsIn;
    let turn = false;
    if (sectors.some((s) => s.bone >= 0)) {
        let largest = sectors[0]!;
        for (const s of sectors) if (s.positions.length > largest.positions.length) largest = s;
        sectors = [largest];
        turn = true;
    }
    sectors = sectors.filter((s) => s.positions.length && s.meshes.length);
    if (!sectors.length) return null;
    const normals = sectors.every((s) => thps4.SEC_NORMALS & s.flags && s.normals !== undefined);
    const textured = sectors.filter((s) => s.flags & thps4.SEC_TEXCOORDS);
    const sets = textured.length ? pyMax(textured.map((s) => s.uvSets || 0)) : 0;
    const out: thps4.Sector = {
        checksum: name,
        bone: -1,
        positions: [],
        colors: [],
        meshes: [],
        flags: thps4.SEC_COLORS | (sets ? thps4.SEC_TEXCOORDS : 0) | (normals ? thps4.SEC_NORMALS : 0),
        bbox: [],
        sphere: [],
    };
    if (normals) out.normals = [];
    if (sets) {
        out.uvSets = sets;
        out.uvs = [];
    }
    const turned = (v: number[]): number[] => thps4.floats(v[0]!, v[2]!, -v[1]!);
    for (const s of sectors) {
        const base = out.positions.length;
        const count = s.positions.length;
        if (turn) {
            out.positions.push(...s.positions.map(turned));
            if (normals) out.normals!.push(...s.normals!.map(turned));
        } else {
            out.positions.push(...s.positions);
            if (normals) out.normals!.push(...s.normals!);
        }
        if (s.colors !== undefined) out.colors!.push(...s.colors);
        else for (let k = 0; k < count; k++) out.colors!.push([255, 255, 255, 128]);
        if (sets) {
            const have = s.flags & thps4.SEC_TEXCOORDS ? s.uvSets || 0 : 0;
            for (let k = 0; k < count; k++) {
                const uv = have ? thps4.floats(...s.uvs![k]!) : thps4.floats();
                while (uv.length < 2 * sets) uv.push(...(uv.length ? uv.slice(0, 2) : thps4.floats(0.0, 0.0)));
                out.uvs!.push(uv.slice(0, 2 * sets));
            }
        }
        for (const mesh of s.meshes) {
            out.meshes.push({ flags: mesh.flags, material: mesh.material, strip: mesh.strip.map((v) => v + base) });
        }
    }
    [out.bbox, out.sphere] = boundsOf(out.positions);
    return [out, turn];
}

export function mergeCol(objects: readonly thps4.ColObject[], name: number, turn: boolean): thps4.ColObject | null {
    // A model's collision objects as one object of the checksum `name` (THUG2's LevelObject collides with the
    // object of its name), turned as mergeSectors turned the scene.
    const verts: number[][] = [];
    const intensities: number[][] = [];
    const faces: [number, number, number, number, number][] = [];
    for (const o of objects) {
        const base = verts.length;
        verts.push(...o.verts.map(([x, y, z]) => (turn ? thps4.floats(x!, z!, -y!) : thps4.floats(x!, y!, z!))));
        intensities.push(...o.intensities);
        faces.push(
            ...o.faces.map(([f, t, a, b, c]): [number, number, number, number, number] => [
                f,
                t,
                a + base,
                b + base,
                c + base,
            ]),
        );
    }
    if (!verts.length || !faces.length) return null;
    const lo = [0, 1, 2].map((k) => pyMin(verts.map((v) => v[k]!)));
    const hi = [0, 1, 2].map((k) => pyMax(verts.map((v) => v[k]!)));
    return {
        checksum: name,
        flags: objects[0]!.flags,
        smallFaces: 0,
        fixedVerts: 0,
        bbox: [lo[0]!, lo[1]!, lo[2]!, 1.0, hi[0]!, hi[1]!, hi[2]!, 1.0],
        value: objects[0]!.value,
        verts,
        intensities,
        faces,
    };
}

export type Model = [thps4.Scene, tex.TexFile, thps4.ColFile | null];

// os.path.splitext(p)[0]
function withoutExtension(p: string): string {
    const slash = Math.max(p.lastIndexOf("\\"), p.lastIndexOf("/"));
    const dot = p.lastIndexOf(".");
    if (dot <= slash) return p;
    // (a name of only leading dots has no extension)
    const nameStart = slash + 1;
    if (/^\.+$/.test(p.slice(nameStart, dot + 1))) return p;
    return p.slice(0, dot);
}

export class VehicleModels {
    // THPS4's vehicles moved by their scripts (not Car_Generic01's: those are THUG2's cars), each made a LevelObject
    // of its model (Game\data\models\<model>mdl.dat / tex.dat / col.dat): its sector, materials, textures and
    // collision added to the level's (addTo).
    readonly modelsDir: string;
    private readonly cache = new Map<string, Model | null>();

    constructor(thps4Dir: string) {
        this.modelsDir = path.join(thps4Dir, "Game", "data", "models");
    }

    model(file: string): Model | null {
        const k = file.toLowerCase();
        if (!this.cache.has(k)) {
            const base = pyJoin(this.modelsDir, withoutExtension(file.replaceAll("/", "\\")));
            let found: Model | null = null;
            if (fs.existsSync(`${base}mdl.dat`) && fs.existsSync(`${base}tex.dat`)) {
                const scene = thps4.readScene(read(`${base}mdl.dat`), `${base}mdl.dat`);
                const col = fs.existsSync(`${base}col.dat`)
                    ? thps4.readCol(read(`${base}col.dat`), `${base}col.dat`)
                    : null;
                found = [scene, tex.readTex(read(`${base}tex.dat`), `${base}tex.dat`), col];
            }
            this.cache.set(k, found);
        }
        return this.cache.get(k)!;
    }

    plan(
        array: readonly qb.QbValue[],
        names: ReadonlyMap<number, string>,
        movingNames: ReadonlySet<number>,
    ): Map<number, string> {
        // {node name: model path} for the vehicles that get their model.
        const out = new Map<number, string>();
        for (const node of array) {
            if (!(node instanceof Struct) || nodes.classOf(node, names) !== "vehicle") continue;
            const name = node.get("Name");
            const file = node.get("model");
            if (name instanceof Name && !movingNames.has(name.value) && typeof file === "string" && this.model(file)) {
                out.set(name.value, file);
            }
        }
        return out;
    }

    addTo(
        plan: Map<number, string>,
        scene4: thps4.Scene,
        textures: tex.TexFile,
        col4: thps4.ColFile | null,
    ): Set<number> {
        // The planned vehicles' sectors / materials / textures / collision into the level's THPS4 scene, its THUG2
        // texture dictionary and its THPS4 collision. Returns the names of those that got collision.
        const materials = new Set(scene4.materials.map((m) => m.checksum));
        const haveTex = new Set(textures.textures.map((t) => t.checksum));
        const collides = new Set<number>();
        const ordered = [...plan].sort((a, b) => a[0] - b[0] || (a[1] < b[1] ? -1 : a[1] > b[1] ? 1 : 0));
        for (const [name, file] of ordered) {
            const [scene, modelTex, col] = this.model(file)!;
            const merged = mergeSectors(scene.sectors, name);
            if (merged === null) {
                // (no sector: no LevelObject - THUG2's creates one's collision from its sector, a null read)
                plan.delete(name);
                continue;
            }
            const [sector, turn] = merged;
            scene4.sectors.push(sector);
            for (const m of scene.materials) {
                if (!materials.has(m.checksum)) {
                    scene4.materials.push(m);
                    materials.add(m.checksum);
                }
            }
            for (const t of modelTex.textures) {
                if (!haveTex.has(t.checksum)) {
                    textures.textures.push(t);
                    haveTex.add(t.checksum);
                }
            }
            if (col4 !== null && col !== null) {
                const obj = mergeCol(col.objects, name, turn);
                if (obj !== null) {
                    col4.objects.push(obj);
                    collides.add(name);
                }
            }
        }
        return collides;
    }
}

export function pedPaths(
    array: readonly qb.QbValue[],
    names: ReadonlyMap<number, string>,
    moving: readonly Struct[],
): Set<number> {
    // The array indices of the waypoints peds_cars.ts made again (reached from the peds' / cars' Links).
    const made = movingNames(moving);
    const out = new Set<number>();
    for (const node of array) {
        if (!(node instanceof Struct)) continue;
        const name = nameOf(node);
        if (name !== null && made.has(name) && ["pedestrian", "vehicle"].includes(nodes.classOf(node, names))) {
            for (const link of linksOf(node)) {
                if (isInt(link)) for (const i of pedsCars.follow(array, link, names)) out.add(i);
            }
        }
    }
    return out;
}

export function dropZeroAngles(converted: readonly Struct[], names: ReadonlyMap<number, string>): number {
    // Angles=(0, 0, 0) taken off the rails and level geometry nodes (THUG2's CVector pool: 9500 for all scripts).
    let dropped = 0;
    for (const node of converted) {
        const angles = node.get("Angles");
        if (
            ["railnode", "levelgeometry"].includes(nodes.classOf(node, names)) &&
            angles instanceof Vector &&
            !angles.values.some((v) => v !== 0)
        ) {
            node.remove("Angles");
            dropped += 1;
        }
    }
    return dropped;
}

export function movingNames(moving: readonly Struct[]): Set<number> {
    const out = new Set<number>();
    for (const n of moving) {
        const name = nameOf(n);
        if (name !== null) out.add(name);
    }
    return out;
}

export interface ConvertResult {
    converted: Struct[];
    kills: Map<number, string>;
    scripts: Map<number, Buffer>;
    text: string;
    hasSetup: boolean;
    report: string;
    translator: scriptPort.Translator;
}

// This project's own choices in convert() (port changes - tools/compat/thugpro/thps4_levels.json port_changes, README
// "Our own changes"; thps4_levels.ts passes them, all off with --no-port-changes):
//   leftOut              (PC-THPS4-8) THPS4 commands left out of the brought scripts besides script_port's
//   neverCreated         (PC-THPS4-6) the made objects no script creates and the waypoints nothing uses left out
//   emptyRestartScripts  (PC-THPS4-9) a restart's trigger script with nothing left in it dropped
export interface ConvertOptions {
    leftOut?: readonly string[];
    neverCreated?: boolean;
    emptyRestartScripts?: boolean;
}

export function convert(
    port: Port,
    array: readonly qb.QbValue[],
    names: Map<number, string>,
    levelSources: readonly scriptPort.Source[],
    killsIn: Map<number, string>,
    sectors: ReadonlySet<number>,
    moving: readonly Struct[],
    struct: Struct | undefined,
    level: string,
    vehicleSectors: ReadonlySet<number> = new Set(),
    collides: ReadonlySet<number> = new Set(),
    options: ConvertOptions = {},
): ConvertResult {
    // The level's node array made THUG2's with its scripts (see the top). levelSources: script_port.Source of the
    // level's own .qb files; killsIn: the kill scripts (nodes.killScripts); sectors: the scene's sector checksums;
    // moving: peds_cars.ts's nodes for the level; struct: THPS4's level structure; vehicleSectors: the vehicles
    // whose model's sector is in the scene (VehicleModels); collides: the collision objects' names. Returns the
    // converted nodes, kills, the scripts {name: THUG2 bytes}, NeverScript text to add (globals, setup script, ped
    // scripts), whether there's a setup script, the report line and the Translator.
    const made = new Nodes(
        array,
        names,
        sectors,
        movingNames(moving),
        pedPaths(array, names, moving),
        port.assets,
        vehicleSectors,
        collides,
    );
    const madeKept: Struct[] = [];
    for (const node of array) {
        const replaced = node instanceof Struct ? made.convert(node) : made.leftOut(null);
        madeKept.push(replaced === null ? (node as Struct) : replaced);
    }
    // trigger scripts of the sectors and rails (the skater's) and the made nodes', wanted before convertNodes
    // (and the restarts': run by the skater put there - THPS4's drop-ins, MakeSkaterGoto DropIn)
    const wanted = new Set<number>();
    for (const node of madeKept) {
        const script = node.get("TriggerScript");
        if (script instanceof Name && ["levelgeometry", "railnode", "restart"].includes(nodes.classOf(node, names))) {
            if (!killsIn.has(script.value)) wanted.add(script.value);
        }
    }
    const sources = [...port.sources, ...levelSources];
    const allowed = new Set([...SOURCES, "level"]);
    const allNames = new Set<number>();
    for (const n of array) if (n instanceof Struct && nameOf(n) !== null) allNames.add(nameOf(n)!);
    // geometry nodes the converted scene has no sector for (script_port's undrawn: Visible / Kill... of them crash)
    const undrawn = new Set<number>();
    for (const n of array) {
        if (!(n instanceof Struct)) continue;
        const name = nameOf(n);
        if (name !== null && ["levelgeometry", "levelobject"].includes(nodes.classOf(n, names)) && !sectors.has(name)) {
            undrawn.add(name);
        }
    }
    // Twice: the made objects not there at the start that no brought script creates (goals' peds, pick-ups, the
    // goals' level objects...) are left out the second time - THUG2's script heap didn't hold College's node
    // array with all of them (the array came out null: a crash loading).
    let never = new Set<number>();
    let converted: Struct[] = [];
    let stats = new Map<string, number>();
    let kills = killsIn;
    let provided = new Set<number>();
    let starts: number[] = [];
    let out = new Map<number, Buffer>();
    let tr: scriptPort.Translator | null = null;
    for (let pass = 0; pass < 2; pass++) {
        const kept = madeKept.map((n) => {
            const name = nameOf(n);
            return made.ids.has(n) && name !== null && never.has(name) ? made.leftOut(null) : n;
        });
        const ids = kept.filter((n) => made.ids.has(n));
        [converted, stats] = nodes.convertNodes(kept, names, killsIn, [], ids, wanted);
        kills = nodes.addKillRestarts(converted, killsIn, names);

        // the scripts
        const nodeNames = new Set<number>();
        for (const n of [...converted, ...moving]) if (nameOf(n) !== null) nodeNames.add(nameOf(n)!);
        const objects = new Map<number, scriptPort.ObjectContext>();
        for (const [k, v] of made.objects) if (!never.has(k)) objects.set(k, v);
        for (const node of moving) {
            const name = nameOf(node);
            if (name !== null && ["pedestrian", "vehicle"].includes(nodes.classOf(node, names))) {
                objects.set(name, [scriptPort.NONE, node.hasFlag("CreatedAtStart")]);
            }
        }
        objects.set(checksum("Skater"), [scriptPort.SKATER, true]);
        provided = new Set([...kills.keys(), ...["LoadTerrain", PED_STAND].map(checksum)]);
        const translator = new scriptPort.Translator(
            sources,
            port.thug2,
            (label) => allowed.has(label),
            nodeNames,
            allNames,
            names,
            {
                provided,
                undrawn,
                leftOut: options.leftOut ?? [],
                objects,
                components: COMPONENTS,
                full: true,
                byName: BY_NAME,
                rewrites: REWRITES,
                keptIn: KEPT_IN,
            },
        );
        tr = translator;
        const roots: [number, scriptPort.Context][] = [...wanted].map((s) => [s, scriptPort.SKATER]);
        for (const [nodeName, script, context] of made.triggers)
            if (!never.has(nodeName)) roots.push([script, context]);
        starts = [];
        for (const k of ["setup_script", "startup_script"]) {
            const value = struct?.get(k);
            if (value instanceof Name && translator.canBring(value.value)) starts.push(value.value);
        }
        starts.push(...made.proximScripts.filter((s) => translator.canBring(s)));
        for (const s of starts) roots.push([s, scriptPort.NONE]);
        out = translator.run(roots);
        if (never.size || !options.neverCreated) break;
        const waypoints = new Set<number>();
        for (const n of madeKept) {
            const name = nameOf(n);
            if (made.ids.has(n) && name !== null && nodes.classOf(n, names) === "waypoint") waypoints.add(name);
        }
        never = new Set();
        for (const [k, [, atStart]] of made.objects) {
            if (
                !atStart &&
                !made.gameModeObjects.has(k) &&
                !translator.created.has(k) &&
                !translator.referenced.has(k) &&
                !waypoints.has(k) &&
                ![...translator.createdPrefixes].some((p) => (names.get(k) ?? "").toLowerCase().startsWith(p))
            ) {
                never.add(k);
            }
        }
        made.counts.set("not created (left out)", never.size);
        // The waypoints no kept object's path and no brought script reaches, left out too (THUG2's script pools
        // are fixed: College's node array with every node didn't fit - it came out null, a crash loading).
        const named = new Set<number>();
        for (const code of out.values()) {
            for (const [t, p] of scriptPort.plainTokens(code)) if (t === 0x16) named.add(scriptPort.u32(p));
        }
        const index = new Map<number, number>();
        madeKept.forEach((n, i) => {
            const name = nameOf(n);
            if (name !== null) index.set(name, i);
        });
        const todo: number[] = [];
        for (const k of named) if (waypoints.has(k) && index.has(k)) todo.push(index.get(k)!);
        for (const n of madeKept) {
            const name = nameOf(n);
            if (nodes.classOf(n, names) !== "waypoint" && name !== null && !never.has(name)) {
                todo.push(...linksOf(n).filter(isInt));
            }
        }
        const reached = new Set<number>();
        while (todo.length) {
            const i = todo.pop()!;
            if (reached.has(i) || !(0 <= i && i < madeKept.length)) continue;
            reached.add(i);
            todo.push(...linksOf(madeKept[i]!).filter(isInt));
        }
        const unused = [...waypoints].filter((k) => {
            const i = index.get(k);
            return i === undefined || !reached.has(i);
        });
        made.counts.set("waypoints no path or script uses (left out)", unused.length);
        for (const k of unused) never.add(k);
        if (!never.size) break;
    }
    const translator = tr!;

    const available = (script: number): boolean =>
        out.has(script) || port.thug2.scripts.has(script) || provided.has(script);

    // (a restart's trigger script with nothing left in it - THPS4's menu restart: FreezeSkater, its camera - goes, and
    // the empty script with it when nothing else runs it)
    const emptyRestartScripts = new Set<number>();
    for (const node of options.emptyRestartScripts ? converted : []) {
        const script = node.get("TriggerScript");
        const code = script instanceof Name ? out.get(script.value) : undefined;
        if (code !== undefined && nodes.classOf(node, names) === "restart" && isEmptyScript(code)) {
            node.remove("TriggerScript");
            emptyRestartScripts.add((script as Name).value);
        }
    }
    for (const s of emptyRestartScripts) {
        const used = converted.some((n) => {
            const t = n.get("TriggerScript");
            return t instanceof Name && t.value === s;
        });
        if (!used && !starts.includes(s)) out.delete(s);
    }
    let dropped = 0;
    for (const node of converted) {
        const script = node.get("TriggerScript");
        if (script instanceof Name && !available(script.value)) {
            node.remove("TriggerScript");
            dropped += 1;
        }
    }
    const byName = new Map<number, Struct>();
    for (const n of converted) if (nameOf(n) !== null) byName.set(nameOf(n)!, n);
    const trigger = checksum("TriggerScript");
    for (const [nodeName, script] of made.triggers) {
        const node = byName.get(nodeName);
        if (node !== undefined && available(script) && node.get("TriggerScript") === undefined) {
            node.entries.push([new Name(trigger), new Name(script)]);
        }
    }
    const run = [...new Set(starts)].filter((s) => out.has(s));
    let text = translator.globalsNs() + PED_SCRIPTS;
    if (run.length) {
        text += `script ${setupName(level)} {\n${run.map((s) => `    SpawnScript ${qb.nameText(s, names)} \n`).join("")}}\n`;
    }
    const worst = [...translator.leftOut].sort((a, b) => b[1] - a[1]);
    let bytes = 0;
    for (const code of out.values()) bytes += code.length;
    let leftOutCount = 0;
    for (const v of translator.leftOut.values()) leftOutCount += v;
    const report =
        `nodes: ${converted.length} of ${array.length} kept ${pyRepr(stats)}\n  made ${pyRepr(made.counts)}\n` +
        `  scripts: ${out.size} brought along (${bytes} bytes), ${run.length} start scripts, ${dropped} trigger ` +
        `scripts not; left out ${leftOutCount} statements: ${worst
            .slice(0, REPORTED)
            .map(([k, v]) => `${k} x${v}`)
            .join(", ")}`;
    return { converted, kills, scripts: out, text, hasSetup: run.length > 0, report, translator };
}
