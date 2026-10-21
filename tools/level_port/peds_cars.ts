// THPS4 node arrays' pedestrians and cars -> THUG2's own: the nodes nodes.ts leaves out (peds, vehicles, waypoints),
// made again as THUG2's levels write them, driven by THUG2's scripts (none of THPS4's ped / car scripts run in THUG2):
//
//   cars     a Vehicle whose trigger script calls Car_Generic01 (THPS4's and THUG2's share it: follow the linked
//            waypoints, stop for the skater and for other cars) - its model if THUG2 has it (Data\models\veh), else
//            FALLBACK_CAR; the waypoints it drives along, as plain waypoints.
//   walkers  a Pedestrian whose trigger script follows its linked path (Obj_FollowPathLinked: THPS4's walking peds) -
//            THUG2's ped AI walking (Ped_InitAI; PedAI waypoints, PedType=Walk) along the same path, its profile if
//            THUG2 has it (random_male_profile...).
//   pros     THPS4's skate shop's pros (Skateshop_PedProTest Path1= Path2=: two waypoint lines down one side of the
//            halfpipe and up the other) - THUG2's ped AI skating (Ped_InitAI skater; PedType=Skate waypoints): the two
//            lines made one loop, each lip a Vert_Flip jumping to the start of the other line, the next node down a
//            Vert_Land (as THUG2's LA vert skaters). The pros THUG2 has (PedPro_<name>, the _skateboard one first)
//            skate from their place on the deck; the others stand there watching as random skateboarders.
//
// Positions are written as THUG2's Pos (the legacy Position's z negated, as CNodeManager::AddNodeArray reads it);
// Angles as they are. The new nodes' Links are indices into the returned list: add the index of its first node
// (offsetLinks) when they are appended to an array. Each node's trigger script is returned as NeverScript text.

import * as fs from "node:fs";
import * as path from "node:path";
import { decodeLatin1, Float, hex8, intFromBytesLE, pyJoin, pyTruthy } from "../lib/py.ts";
import { unpack } from "../lib/struct.ts";
import { FormatError } from "./binio.ts";
import * as pedRigs from "./ped_rigs.ts";
import * as prx from "./prx.ts";
import * as qb from "./qb.ts";
import { checksum, type Entry, Name, type QbFile, type QbValue, type Statement, Struct, Vector } from "./qb.ts";

// THUG2's vehicle used when THPS4's model isn't in THUG2.
export const FALLBACK_CAR = "veh\\veh_sedan\\veh_sedan.mdl";
// A pro without a THUG2 PedPro profile: a random skateboarder (THUG2's profiles.qb).
export const FALLBACK_SKATER = "Ped_Skateboard_a";
// Pros left out altogether (the user: THUG2's Bam Margera skates the vert loop with broken-looking animations).
export const LEFT_OUT_PROS: ReadonlySet<string> = new Set(["margera"]);
// Vert jumps: the height over the lip (THPS4's pros: Obj_Jump speed=350 gravity=-400, ~150).
export const JUMP_HEIGHT = 100.0;

type Names = Map<number, string>;

function nameOf(names: Names, text: string): Name {
    const value = checksum(text);
    if (!names.has(value)) names.set(value, text);
    return new Name(value);
}

function textOf(value: QbValue | undefined, names: ReadonlyMap<number, string>): string {
    return value instanceof Name ? (names.get(value.value) ?? "") : "";
}

// A call statement's parameters: [flags, {key lower: value}] - flags as text, values as Name / int / Float / string.
export function callArgs(line: Statement, names: ReadonlyMap<number, string>): [string[], Map<string, QbValue>] {
    const flags: string[] = [];
    const values = new Map<string, QbValue>();
    let i = 1;
    while (i < line.length) {
        const [t, payload] = line[i]!;
        if (t === 0x16 && i + 2 < line.length && line[i + 1]![0] === 0x07) {
            const [vt, vp] = line[i + 2]!;
            const k = intFromBytesLE(payload);
            const key = names.get(k) ?? hex8(k);
            if (vt === 0x16) values.set(key.toLowerCase(), new Name(intFromBytesLE(vp)));
            else if (vt === 0x17) values.set(key.toLowerCase(), intFromBytesLE(vp, true));
            else if (vt === 0x1a) values.set(key.toLowerCase(), new Float(unpack("<f", vp)[0] as number));
            else if (vt === 0x1b) values.set(key.toLowerCase(), decodeLatin1(qb.rstripNul(vp)));
            i += 3;
            continue;
        }
        if (t === 0x16) {
            const k = intFromBytesLE(payload);
            flags.push(names.get(k) ?? `#${hex8(k)}`);
        }
        i += 1;
    }
    return [flags, values];
}

// The first statement of `script` that calls something: [called name lower, the statement], or [null, null].
export function firstCall(
    script: number,
    scriptQbs: readonly QbFile[],
    names: ReadonlyMap<number, string>,
): [string, Statement] | [null, null] {
    for (const q of scriptQbs) {
        for (const [called, line] of q.calls(script)) return [(names.get(called) ?? "").toLowerCase(), line];
    }
    return [null, null];
}

// The names (lower) every statement of the named script starts with.
export function bodyCalls(
    scriptName: string,
    scriptQbs: readonly QbFile[],
    names: ReadonlyMap<number, string>,
): Set<string> {
    const k = checksum(scriptName);
    for (const q of scriptQbs) {
        if (q.scripts.has(k)) return new Set(q.calls(k).map(([c]) => (names.get(c) ?? "").toLowerCase()));
    }
    return new Set();
}

export function thug2Pos(node: Struct): Vector | null {
    const pos = node.get("Pos");
    if (pos instanceof Vector) return pos;
    const position = node.get("Position");
    if (position instanceof Vector) {
        if (position.values.length !== 3) {
            throw new RangeError(`not enough values to unpack (expected 3, got ${position.values.length})`);
        }
        const [x, y, z] = position.values as [number, number, number];
        return new Vector("vector", [x, y, -z]);
    }
    return null;
}

export class Builder {
    readonly names: Names;
    readonly nodes: Struct[] = []; // Links as local indices
    readonly scripts = new Map<string, string>(); // script name -> NeverScript body line
    readonly bySource = new Map<number, number>(); // THPS4 array index -> local index (waypoints shared by several objects)

    constructor(names: Names) {
        this.names = names;
    }

    add(
        pos: QbValue | null,
        angles: QbValue | undefined,
        name: QbValue | string | undefined,
        entries: readonly Entry[],
    ): number {
        const n = this.names;
        const s = new Struct([
            // (a missing position stays missing, as Python's None: toNs raises on it)
            [nameOf(n, "Pos"), pos as QbValue],
            [nameOf(n, "Angles"), pyTruthy(angles) ? angles! : new Vector("vector", [0.0, 0.0, 0.0])],
            [nameOf(n, "Name"), name instanceof Name ? name : nameOf(n, name as string)],
            ...entries,
        ]);
        this.nodes.push(s);
        return this.nodes.length - 1;
    }

    flag(text: string): Entry {
        return [null, nameOf(this.names, text)];
    }

    // key=value with a text value as a name
    kv(key: string, value: QbValue): Entry {
        return [nameOf(this.names, key), typeof value === "string" ? nameOf(this.names, value) : value];
    }

    setLinks(index: number, links: readonly number[]): void {
        if (links.length) this.nodes[index]!.entries.push([nameOf(this.names, "Links"), [...links]]);
    }

    script(name: string, body: string): Entry {
        nameOf(this.names, name);
        this.scripts.set(name, body);
        return [nameOf(this.names, "TriggerScript"), nameOf(this.names, name)];
    }
}

const isInt = (v: QbValue): v is number => typeof v === "number" && Number.isInteger(v);

function linksOf(node: Struct): QbValue[] {
    const links = node.get("Links");
    if (!pyTruthy(links)) return [];
    if (!Array.isArray(links)) throw new TypeError("Links is not a list");
    return links;
}

// The waypoints reachable from array index `start` (itself included), in order found.
export function follow(array: readonly QbValue[], start: number, names: ReadonlyMap<number, string>): number[] {
    const seen = new Set<number>();
    const order: number[] = [];
    const todo = [start];
    while (todo.length) {
        const i = todo.shift()!;
        if (seen.has(i) || !(0 <= i && i < array.length) || !(array[i] instanceof Struct)) continue;
        const node = array[i] as Struct;
        if (textOf(node.get("Class"), names).toLowerCase() !== "waypoint") continue;
        seen.add(i);
        order.push(i);
        todo.push(...linksOf(node).filter(isInt));
    }
    return order;
}

// The waypoints reachable from `startLinks` as new waypoints (each once), their Links kept; the local indices of
// the start links.
export function addPath(
    b: Builder,
    array: readonly QbValue[],
    startLinks: readonly number[],
    names: ReadonlyMap<number, string>,
    extra: readonly Entry[],
): number[] {
    const reach: number[] = [];
    for (const link of startLinks) {
        for (const i of follow(array, link, names)) if (!reach.includes(i)) reach.push(i);
    }
    const fresh = reach.filter((i) => !b.bySource.has(i));
    for (const i of fresh) {
        const w = array[i] as Struct;
        b.bySource.set(i, b.add(thug2Pos(w), w.get("Angles"), w.get("Name"), [b.kv("Class", "Waypoint"), ...extra]));
    }
    for (const i of fresh) {
        const node = array[i] as Struct;
        b.setLinks(
            b.bySource.get(i)!,
            linksOf(node)
                .filter((v): v is number => typeof v === "number" && b.bySource.has(v))
                .map((v) => b.bySource.get(v)!),
        );
    }
    return startLinks.filter((v) => b.bySource.has(v)).map((v) => b.bySource.get(v)!);
}

// A THPS4 pro path: the waypoints from the named one along the first links to the end.
export function chain(array: readonly QbValue[], startName: QbValue, byName: ReadonlyMap<number, number>): number[] {
    const order: number[] = [];
    let i: number | undefined = byName.get(typeof startName === "string" ? checksum(startName) : (startName as number));
    while (i !== undefined && !order.includes(i)) {
        order.push(i);
        const links = linksOf(array[i] as Struct);
        i = links.length ? (links[0] as number) : undefined;
    }
    return order;
}

export type PedsCarsStats = { cars: number; walkers: number; "pros skating": number; "pros standing": number };

export function convert(
    array: readonly QbValue[],
    names: Names,
    scriptQbs: readonly QbFile[],
    profileExists: (name: string) => boolean,
    modelExists: (model: string) => boolean,
): [Struct[], Map<string, string>, PedsCarsStats] {
    // The new nodes for `array`'s cars, walkers and pros (see the top). scriptQbs: the level's .qb files whose
    // scripts the nodes' trigger scripts are in; profileExists(name) / modelExists(path): what THUG2 has.
    const b = new Builder(names);
    const byName = new Map<number, number>();
    array.forEach((node, i) => {
        if (node instanceof Struct) {
            const name = node.get("Name");
            if (name instanceof Name) byName.set(name.value, i);
        }
    });
    const stats: PedsCarsStats = { cars: 0, walkers: 0, "pros skating": 0, "pros standing": 0 };
    for (const node of array) {
        if (!(node instanceof Struct)) continue;
        const cls = textOf(node.get("Class"), names).toLowerCase();
        const trigger = node.get("TriggerScript");
        if (
            (cls !== "vehicle" && cls !== "pedestrian") ||
            !(trigger instanceof Name) ||
            !node.hasFlag("CreatedAtStart")
        ) {
            continue;
        }
        const [called, line] = firstCall(trigger.value, scriptQbs, names);
        if (called === null) continue;
        const [flags, values] = callArgs(line, names);
        const name = node.get("Name");
        const base = [b.flag("CreatedAtStart"), b.flag("AbsentInNetGames")];
        const startLinks = (): number[] => linksOf(node).filter(isInt);

        if (cls === "vehicle" && called === "car_generic01") {
            let model = node.get("model");
            if (!(typeof model === "string" && modelExists(model))) model = FALLBACK_CAR;
            const links = addPath(b, array, startLinks(), names, [b.flag("CreatedAtStart")]);
            const numeric = [...values].filter(
                ([, v]) => typeof v === "number" || typeof v === "boolean" || v instanceof Float,
            );
            const args = [...flags, ...numeric.map(([k, v]) => `${k}=${qb.toNs(v, names)}`)].join(" ");
            const script = b.script(`${textOf(name, names)}Script`, `Car_Generic01 ${args}`);
            const entries: Entry[] = [
                b.kv("Class", "Vehicle"),
                b.kv("Type", textOf(node.get("Type"), names) || "Car"),
                ...base,
                [nameOf(names, "model"), model],
                b.kv("skeletonName", "car"),
                [nameOf(names, "SuspendDistance"), 10000],
                [nameOf(names, "lod_dist1"), 400],
                [nameOf(names, "lod_dist2"), 401],
                script,
            ];
            const index = b.add(thug2Pos(node), node.get("Angles"), name, entries);
            b.setLinks(index, links);
            stats.cars += 1;
            continue;
        }

        if (cls !== "pedestrian") continue;
        let profile = node.get("profile");
        const ped = [b.kv("Class", "Pedestrian")];
        const common = [
            b.kv("PedAI_Type", "Stand"),
            b.kv("skeletonName", "THPS6_human"),
            b.kv("AnimName", "animload_THPS6_human"),
        ];
        const far: Entry[] = [
            [nameOf(names, "SuspendDistance"), 10000],
            [nameOf(names, "lod_dist1"), 200],
            [nameOf(names, "lod_dist2"), 201],
        ];

        if (values.has("path1") && values.has("path2")) {
            // A pro: the THUG2 pro with the THPS4 pro's name skates; the others stand watching.
            const proValue = node.get("pro_name");
            const pro = proValue instanceof Name ? textOf(proValue, names) : "";
            if (LEFT_OUT_PROS.has(pro.toLowerCase())) continue;
            let skating: string | null = null;
            for (const candidate of [`PedPro_${pro}_skateboard`, `PedPro_${pro}`]) {
                if (pro && profileExists(candidate)) {
                    skating = candidate;
                    break;
                }
            }
            if (skating === null) {
                const script = b.script(`${textOf(name, names)}Script`, "Ped_InitAI stand");
                b.add(thug2Pos(node), node.get("Angles"), name, [
                    ...ped,
                    b.kv("Type", "Ped_Skateboard_A"),
                    ...base,
                    ...common,
                    b.kv("profile", FALLBACK_SKATER),
                    ...far,
                    script,
                ]);
                stats["pros standing"] += 1;
                continue;
            }
            const first = vertLoop(b, array, values.get("path1")!, values.get("path2")!, byName, names);
            if (first === null) continue;
            const script = b.script(`${textOf(name, names)}Script`, "Ped_InitAI skater");
            const index = b.add(thug2Pos(node), node.get("Angles"), name, [
                ...ped,
                b.kv("Type", "Ped_Skateboard_A"),
                ...base,
                ...common,
                b.kv("profile", skating),
                ...far,
                script,
            ]);
            b.setLinks(index, [first]);
            stats["pros skating"] += 1;
            continue;
        }

        if (bodyCalls(called, scriptQbs, names).has("obj_followpathlinked") || called === "obj_followpathlinked") {
            if (!(profile instanceof Name && profileExists(textOf(profile, names)))) {
                profile = nameOf(names, "random_male_profile");
            }
            const links = addPath(b, array, startLinks(), names, [
                b.kv("Type", "PedAI"),
                b.flag("CreatedAtStart"),
                b.flag("Continue"),
                b.kv("PedType", "Walk"),
                b.kv("Priority", "Normal"),
                [nameOf(names, "ContinueWeight"), new Float(1.0)],
            ]);
            if (!links.length) continue;
            const script = b.script(`${textOf(name, names)}Script`, "Ped_InitAI");
            const index = b.add(thug2Pos(node), node.get("Angles"), name, [
                ...ped,
                b.kv("Type", "Ped_From_Profile"),
                ...base,
                ...common,
                [nameOf(names, "profile"), profile],
                ...far,
                script,
            ]);
            b.setLinks(index, links);
            stats.walkers += 1;
        }
    }
    return [b.nodes, b.scripts, stats];
}

export function vertLoop(
    b: Builder,
    array: readonly QbValue[],
    path1: QbValue,
    path2: QbValue,
    byName: ReadonlyMap<number, number>,
    names: ReadonlyMap<number, string>,
): number | null {
    // Path1 and Path2 (THPS4's pro lines, each from one lip across the flat to the other lip) as one THUG2 skate loop:
    // every node PedType=Skate; each line's last node (a lip) a Vert_Flip jumping to the other line's first (the lip
    // beside it), each line's second node a Vert_Land. The local index of path1's first node.
    void names;
    const lines = [path1, path2].map((p) => chain(array, p instanceof Name ? p.value : p, byName));
    if (lines.some((line) => line.length < 3)) return null;
    const starts: number[][] = [];
    for (const line of lines) {
        const indices: number[] = [];
        line.forEach((i, k) => {
            const w = array[i] as Struct;
            const entries: Entry[] = [
                b.kv("Class", "Waypoint"),
                b.kv("Type", "PedAI"),
                b.flag("CreatedAtStart"),
                b.flag("Continue"),
            ];
            if (k === line.length - 1) entries.push(b.flag("JumpToNextNode"));
            entries.push(b.kv("PedType", "Skate"), b.kv("Priority", "Normal"), [
                nameOf(b.names, "ContinueWeight"),
                new Float(1.0),
            ]);
            const action = k === line.length - 1 ? "Vert_Flip" : k === 1 ? "Vert_Land" : "Continue";
            entries.push(b.kv("SkateAction", action));
            if (action === "Vert_Flip") entries.push([nameOf(b.names, "JumpHeight"), new Float(JUMP_HEIGHT)]);
            // (named after the THPS4 waypoint: a pro's lines are its own)
            indices.push(b.add(thug2Pos(w), w.get("Angles"), w.get("Name"), entries));
        });
        for (let k = 0; k + 1 < indices.length; k++) b.setLinks(indices[k]!, [indices[k + 1]!]);
        starts.push(indices);
    }
    b.setLinks(starts[0]![starts[0]!.length - 1]!, [starts[1]![0]!]);
    b.setLinks(starts[1]![starts[1]!.length - 1]!, [starts[0]![0]!]);
    return starts[0]![0]!;
}

// What THUG2 has: its ped profiles (scripts\profile_ped.qb in Data\pre\qb_scripts.prx) and its models (the PC
// game reads them from Data\models: <model>.xbx).
export class Thug2Assets {
    readonly models: string;
    readonly profiles: Set<number>;
    readonly femaleRigs: Set<number>; // the profiles whose models are rigged to Ped_Female (ped_rigs.ts)

    constructor(thug2: string) {
        this.models = path.join(thug2, "Data", "models");
        const data = prx.find(path.join(thug2, "Data", "pre", "qb_scripts.prx"), "scripts\\profile_ped.qb");
        if (data === null) throw new FormatError("qb_scripts.prx: scripts\\profile_ped.qb not found");
        const globals = new qb.QbFile(data, "profile_ped.qb", true).globals;
        this.profiles = new Set(globals.map(([key]) => key.value));
        this.femaleRigs = pedRigs.femaleRigProfiles(globals);
    }

    profileExists(name: string): boolean {
        return this.profiles.has(checksum(name));
    }

    // The rig (skeleton, animations, generic Type) the profile's model needs (ped_rigs.ts).
    rig(profile: string | number): pedRigs.Rig {
        return this.femaleRigs.has(typeof profile === "string" ? checksum(profile) : profile)
            ? pedRigs.FEMALE
            : pedRigs.MALE;
    }

    modelExists(model: string): boolean {
        return fs.existsSync(pyJoin(this.models, `${model.replaceAll("/", "\\")}.xbx`));
    }
}

export function offsetLinks(nodes: Struct[], base: number): Struct[] {
    const links = checksum("Links");
    for (const node of nodes) {
        node.entries = node.entries.map(([k, value]) => [
            k,
            k !== null && k.value === links ? (value as QbValue[]).map((v) => (v as number) + base) : value,
        ]);
    }
    return nodes;
}

export function scriptsNs(scripts: ReadonlyMap<string, string>): string {
    return [...scripts]
        .sort((a, b) => (a[0] < b[0] ? -1 : a[0] > b[0] ? 1 : 0))
        .map(([name, body]) => `script ${name} {\n    ${body} \n}\n`)
        .join("");
}
