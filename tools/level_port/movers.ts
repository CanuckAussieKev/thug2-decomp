// NOT ORIGINAL - moving props of a ported level made solid THUG2's way: LevelObjects with CollisionMode=Geometry.
//
// THUGPRO made some moving props of the source games Vehicles with its own models (THPS3 Foundry's molten-metal bucket,
// Tokyo's bullet trains - TPN-THPS3-3): a Vehicle gets path movement and collision, but also everything THUG2's
// car_add_components (ped.qb) gives a car - the model's node array (models\<model>.qb, nodearray component), the car
// rail manager, the skitch hooks; a car rail manager with no node array is a crash (docs/pending/foun-crash.md).
//
// THUG2's own levels build a plain solid moving prop as a LevelObject with CollisionMode=Geometry (Boston's subway
// trains BO_Subway01 / 03 moved by Obj_MoveToNode, New Orleans' helicopter blades and fans, the Triangle's gates): the
// LevelObject draws the scene sector of its name, in its own space, and its collision component (CollisionMode, not
// none) takes that sector's collision - the level's collision object of the same name, in the same space
// (Obj::CCollisionComponent::InitFromStructure 0x0041C7B0: GetSector(Name)) - and places it at the object every frame
// (Update 0x0041C980). Our converters already make these props LevelObjects drawn with a copy of the source game's
// model; this adds the collision. A Vehicle stays THUG2's form for movers carrying rails or trigger nodes in their
// model's node array (Barcelona's drawbridges, boat and tram, New Orleans' streetcar) - those aren't listed.
//
// The objects are listed per game in the register's port_changes (PC-THPS3-15...: `objects`), not chosen here.

import { classOf } from "./nodes.ts";
import * as qb from "./qb.ts";
import { Name, type Struct } from "./qb.ts";

export type Names = Map<number, string>;

// One object of a port_changes entry's `objects` list.
export interface Mover {
    level: string; // the level (our folder name: foun, tok...)
    name: string; // the node's Name
    what?: string; // what it is (the register's inventory)
    thugpro_set?: string; // the THUGPRO entry (TPN-THPS3-3) that made it a Vehicle - not applied to it
}

// The entry's objects for a level (lower case).
export function moversFor(objects: unknown, level: string): Mover[] {
    if (!Array.isArray(objects)) return [];
    return (objects as Mover[]).filter((m) => String(m.level).toLowerCase() === level.toLowerCase());
}

// The movers that are LevelObjects in a converted node array get CollisionMode=Geometry (in place; a node with one
// already keeps it). `drawn`: the moving sectors the level has (a LevelObject without one has no collision to take -
// left as it is). Returns [the checksums made solid, the report lines].
export function makeSolid(
    converted: Struct[],
    names: Names,
    movers: readonly Mover[],
    drawn: ReadonlySet<number>,
    tag: string,
): [Set<number>, string[]] {
    const report: string[] = [];
    const solid = new Set<number>();
    const key = (text: string): Name => {
        const c = qb.checksum(text);
        if (!names.has(c)) names.set(c, text);
        return new Name(c);
    };
    for (const m of movers) {
        const value = qb.checksum(m.name);
        const node = converted.find((n) => {
            const nm = n.get("Name");
            return nm instanceof Name && nm.value === value;
        });
        if (node === undefined) {
            report.push(`  ${tag}: ${m.name} isn't in the node array - left as it is`);
            continue;
        }
        const cls = classOf(node, names);
        if (cls !== "levelobject") {
            report.push(`  ${tag}: ${m.name} is a ${cls || "node without a class"}, not a LevelObject - left as it is`);
            continue;
        }
        if (!drawn.has(value)) {
            report.push(`  ${tag}: ${m.name} has no drawn sector here - left without collision`);
            continue;
        }
        if (node.get("CollisionMode") === undefined) node.entries.push([key("CollisionMode"), key("Geometry")]);
        solid.add(value);
        report.push(`  ${tag}: ${m.name} a solid LevelObject (CollisionMode=Geometry)`);
    }
    return [solid, report];
}
