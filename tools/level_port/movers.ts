// NOT ORIGINAL - non-car Vehicles of a ported level made THUG2's way: ordinary moving game objects.
//
// THUGPRO made some moving props of the source games Vehicles with its own models (THPS3 Foundry's molten-metal bucket,
// Tokyo's bullet trains and jets, Subway's plane, the invisible "carplant" boxes riding on Rio's and Subway's cars -
// TPN-THPS3-3 / TPN-THPS3-2): a Vehicle gets path movement and collision, but also everything THUG2's
// car_add_components (ped.qb) gives a car - the model's node array (models\<model>.qb, nodearray component), the car
// rail manager, the skitch hooks - and a car rail manager with no node array is a crash (docs/pending/foun-crash.md).
// THUG2's own levels build solid moving props as game objects (BO's subway trains, NO's helicopter blades: LevelObjects
// with CollisionMode=Geometry); a GameObject with a model gets the same pieces (gameobj_add_components, ped.qb):
// motion (gameobj_composite_structure, skutils.qb: Obj_MoveToNode / Obj_FollowPathLinked / Obj_RotX), lockobj
// (Obj_LockToObject - CreateGameObject 0x00468CB0 adds it), the model, and with a CollisionMode the collision
// component: the model's own collision (models\<model>.col, the asset's data - Obj::CCollisionComponent::
// InitCollision 0x0041C890), placed at the object every frame (Update 0x0041C980). So the object keeps its model, its
// path and timing (its scripts are unchanged) and is solid where it is.
//
// What the model's node array did for a Vehicle: its trigger nodes. A trigger face of a moving object's collision runs
// the TriggerScript of the node named like the face's collision object - looked up in the object's own node array when
// it has a nodearray component, else in the level's (0x00434BF0 -> CTriggerComponent::TriggerNode 0x00434A60 with a
// null array: GetNode). A game object has none, so each trigger node of the model's node array gets a stand-in of the
// same name in the level's array (Class=LevelGeometry, no sector - as the model's own node): with a kill script where
// the register says the trigger kills (the bucket's TRGP_KillSkaterBucket: THUGPRO's script calls a command of its own
// DLL, not THUG2's - THUG2's SK3_KillSkater to the nearest restart instead), else none (nothing runs; the original
// TriggerNode would read through a missing node). A level node of the name already there is kept and reported.
//
// Real road traffic stays Vehicles (cars, buses, the ice-cream truck): THUG2's car AI, skitching, rails on the roof.
// So do non-cars whose model's node array gives them rails, ladders or skitch hooks (THUG2's own BA boat and NO
// streetcar are Vehicles for that): that is Vehicle-only. The objects are listed per game in the register's
// port_changes (PC-THPS3-15...: `objects`), not chosen here.

import { pyMinBy } from "../lib/py.ts";
import { classOf, nodePosition } from "./nodes.ts";
import * as qb from "./qb.ts";
import { Name, type QbValue, Struct, Vector } from "./qb.ts";
import * as thug2Col from "./thug2_col.ts";

export type Names = Map<number, string>;

// One object of a port_changes entry's `objects` list.
export interface Mover {
    level: string; // the level (our folder name: foun, tok...)
    name: string; // the node's Name
    model: string; // its model (as the node names it, under models\) - checked against the node
    what?: string; // what it is (the register's inventory)
    kill_triggers?: string[]; // the model's trigger nodes that kill the skater (THUG2's SK3_KillSkater here)
}

// The entry's objects for a level (lower case).
export function moversFor(objects: unknown, level: string): Mover[] {
    if (!Array.isArray(objects)) return [];
    return (objects as Mover[]).filter((m) => String(m.level).toLowerCase() === level.toLowerCase());
}

const VEHICLE = qb.checksum("Vehicle");
const CLASS = qb.checksum("Class");
// Car-only keys a game object doesn't read (car_add_components' skeleton): left out.
const CAR_KEYS: ReadonlySet<number> = new Set(["skeletonName", "NoRail", "NoSkitch", "NoCollision"].map(qb.checksum));

// A model's file next to its .mdl (the models map's key form: the path under models\, lower case, backslashes).
export function modelFile(model: string, extension: string): string {
    return model
        .replaceAll("/", "\\")
        .replace(/\.mdl$/i, "")
        .toLowerCase()
        .concat(extension);
}

// The trigger nodes of a model's node array (its .qb: the first array of structures) - the nodes with a TriggerScript
// - by name; and whether its collision (.col) has any trigger face (0x40) at all.
export function modelTriggers(qbData: Buffer | undefined, colData: Buffer | undefined): string[] {
    const out: string[] = [];
    if (qbData !== undefined) {
        const q = new qb.QbFile(qbData, "model", true);
        const array = q.globals.find(([, v]) => Array.isArray(v))?.[1];
        for (const n of Array.isArray(array) ? array : []) {
            if (!(n instanceof Struct)) continue;
            const name = n.get("Name");
            if (name instanceof Name && n.get("TriggerScript") instanceof Name)
                out.push(q.names.get(name.value) ?? qb.nameText(name.value, q.names));
        }
    }
    if (colData !== undefined) {
        // (a trigger collision object without a node in the model's array: named all the same)
        const col = thug2Col.readCol(colData, "model col", [9, 10]);
        for (const o of col.objects) {
            if (!o.faces.some((f) => (f[0] & thug2Col.FACE_TRIGGER) !== 0)) continue;
            if (!out.some((t) => qb.checksum(t) === o.checksum))
                out.push(`#${o.checksum.toString(16).padStart(8, "0")}`);
        }
    }
    return out;
}

const nameValue = (text: string): number =>
    text.startsWith("#") ? Number.parseInt(text.slice(1), 16) >>> 0 : qb.checksum(text);

// The movers made game objects in a converted node array (in place); their models' trigger nodes as stand-ins and kill
// scripts in `kills` ({script: command}, written by nodes.nodeArrayNs). `models`: the model files the level brings
// ({path under models\, lower case: data}). Returns the report lines.
export function makeMovers(
    converted: Struct[],
    names: Names,
    movers: readonly Mover[],
    models: ReadonlyMap<string, Buffer>,
    kills: Map<number, string>,
    tag: string,
): string[] {
    const report: string[] = [];
    const key = (text: string): Name => {
        const c = qb.checksum(text);
        if (!names.has(c)) names.set(c, text);
        return new Name(c);
    };
    const index = new Map<number, number>();
    converted.forEach((n, i) => {
        const nm = n.get("Name");
        if (nm instanceof Name && !index.has(nm.value)) index.set(nm.value, i);
    });
    const restarts: [readonly number[], number][] = [];
    for (const n of converted) {
        const p = nodePosition(n);
        const nm = n.get("Name");
        if (classOf(n, names) === "restart" && p && nm instanceof Name) restarts.push([p, nm.value]);
    }
    const added = new Set<number>();
    let made = 0;
    for (const m of movers) {
        const i = index.get(qb.checksum(m.name));
        if (i === undefined) {
            report.push(`  ${tag}: ${m.name} isn't in the node array - left as it is`);
            continue;
        }
        const node = converted[i]!;
        const cls = node.entries.find(([k]) => k !== null && k.value === CLASS)?.[1];
        if (!(cls instanceof Name) || cls.value !== VEHICLE) {
            // (--compat none / --no-thugpro-new: THPS3's own object, our converter's level object - nothing to change)
            report.push(`  ${tag}: ${m.name} isn't a Vehicle here - left as it is`);
            continue;
        }
        const model = node.get("model");
        if (typeof model !== "string" || modelFile(model, "") !== modelFile(m.model, "")) {
            report.push(`  ${tag}: ${m.name}'s model is ${String(model)}, not ${m.model} - left as it is`);
            continue;
        }
        const entries: [Name | null, QbValue][] = [];
        for (const [k, v] of node.entries) {
            if (k !== null && CAR_KEYS.has(k.value)) continue;
            entries.push(k !== null && k.value === CLASS ? [k, key("GameObject")] : [k, v]);
        }
        const object = new Struct(entries);
        if (object.get("CollisionMode") === undefined) object.entries.push([key("CollisionMode"), key("Geometry")]);
        converted[i] = object;
        made += 1;
        // the model's trigger nodes: stand-ins in the level's array
        const triggers = modelTriggers(models.get(modelFile(model, ".qb")), models.get(modelFile(model, ".col.xbx")));
        const at = nodePosition(object);
        for (const t of triggers) {
            const value = nameValue(t);
            if (added.has(value)) continue;
            if (index.has(value)) {
                report.push(`  ${tag}: ${m.name}'s trigger ${t}: the level has a node of that name - its script runs`);
                continue;
            }
            const kill = (m.kill_triggers ?? []).some((k) => nameValue(k) === value);
            const stand: [Name | null, QbValue][] = [
                [key("Position"), new Vector("vector", at ? [...at] : [0, 0, 0])],
                [key("Angles"), new Vector("vector", [0, 0, 0])],
                [key("Name"), t.startsWith("#") ? new Name(value) : key(t)],
                [key("Class"), key("LevelGeometry")],
            ];
            if (kill && at && restarts.length) {
                const script = `${t.startsWith("#") ? t.slice(1) : t}_PortKill`;
                const [, restart] = pyMinBy(restarts, (r) =>
                    [0, 1, 2].reduce((s, k) => s + (r[0][k]! - at[k]!) * (r[0][k]! - at[k]!), 0),
                );
                stand.push([key("TriggerScript"), key(script)]);
                kills.set(qb.checksum(script), `SK3_KillSkater nodename=${qb.nameText(restart, names)}`);
            }
            converted.push(new Struct(stand));
            added.add(value);
            index.set(value, converted.length - 1);
        }
        report.push(
            `  ${tag}: ${m.name} a GameObject (${model}, CollisionMode=Geometry)` +
                (triggers.length ? `; its model's triggers in the level's array: ${triggers.join(", ")}` : ""),
        );
    }
    if (made) report.unshift(`  ${tag}: ${made} non-car Vehicles made game objects`);
    return report;
}
