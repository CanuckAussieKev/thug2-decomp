// NOT ORIGINAL - the compatibility profiles: data that makes our generated files agree with another game's or mod's
// for online play, kept apart from the converters so every such choice is listed in one place (<profile>\README.md,
// its register) and can be dropped or replaced (e.g. by a run-time translation of the network messages) later.
// (Ported from compat.py, deleted with its last importer, tools/thaw_levels/thaw_levels.py.)
//
//   import * as compat from "../compat/compat.ts";
//   const profile = compat.load("thugpro");        // or compat.load("none"): no compatibility data at all
//   profile.section("thaw_levels", "level_names")  ->  {Z_CH: "skatepark", ...}

import * as fs from "node:fs";
import * as path from "node:path";
import { fileURLToPath } from "node:url";
import { pyStrRepr } from "../lib/py.ts";

const HERE = path.dirname(fileURLToPath(import.meta.url));
export const PROFILES = ["thugpro", "none"] as const;
export type ProfileName = (typeof PROFILES)[number];

type JsonObject = Record<string, unknown>;

export class Profile {
    readonly name: string;
    private readonly files = new Map<string, JsonObject>();

    constructor(name: string) {
        this.name = name;
    }

    // <profile>\<tool>.json as an object ({} for "none" or when the profile has no such file).
    file(tool: string): JsonObject {
        let data = this.files.get(tool);
        if (data === undefined) {
            const file = path.join(HERE, this.name, `${tool}.json`);
            data = {};
            if (this.name !== "none" && fs.existsSync(file))
                data = JSON.parse(fs.readFileSync(file, "utf-8")) as JsonObject;
            this.files.set(tool, data);
        }
        return data;
    }

    // One section of a tool's file without its "_..." notes ({} when absent).
    section(tool: string, key: string): JsonObject {
        const section = (this.file(tool)[key] ?? {}) as JsonObject;
        return Object.fromEntries(Object.entries(section).filter(([k]) => !k.startsWith("_")));
    }
}

// The two sections of a differences file (<profile>\<tool>.json) written as a list of entries, each
// {id, op, target, why, ...}: op "add" (something the source game hasn't: a node, sector, collision object, script),
// "set" (fields of something that exists change, merged; set on something missing is an error the converter
// reports, never an add) or "remove" (something that exists left out or suppressed).
//   thugpro_new   THUGPRO's own content (not in any source game) - a THUGPRO profile section like the TP-n ones
//                 (`--compat none` leaves it out);
//   port_changes  this project's own changes to a source game's levels (neither the source game's nor THUGPRO's):
//                 not THUGPRO data, so applied whatever the profile (`--compat none` too); a converter's
//                 `--no-port-changes` leaves them out (the source game's data as it is).
export type Op = "add" | "set" | "remove";
export const OPS: readonly Op[] = ["add", "set", "remove"];
export interface Entry {
    id: string;
    op: Op;
    target: Record<string, unknown>;
    why: string;
    required?: boolean; // (port_changes: a level needs it to load / install - kept with --no-port-changes)
    [key: string]: unknown;
}

function entries(data: unknown, where: string): Entry[] {
    const list = (data as { entries?: unknown } | undefined)?.entries ?? [];
    if (!Array.isArray(list)) throw new TypeError(`${where}: entries is not a list`);
    for (const e of list as Entry[]) {
        if (
            typeof e.id !== "string" ||
            !OPS.includes(e.op) ||
            typeof e.target !== "object" ||
            typeof e.why !== "string"
        )
            throw new TypeError(
                `${where}: ${JSON.stringify(e).slice(0, 80)}: needs id, op (add / set / remove), target, why`,
            );
    }
    return list as Entry[];
}

// A tool's port_changes (from the THUGPRO profile's folder - the game's differences file - whatever profile is in use).
export function portChanges(tool: string): Entry[] {
    return entries(load("thugpro").file(tool).port_changes, `thugpro\\${tool}.json port_changes`);
}

// A profile's thugpro_new entries for a tool (none with --compat none).
export function thugproNew(profile: Profile, tool: string): Entry[] {
    return entries(profile.file(tool).thugpro_new, `${profile.name}\\${tool}.json thugpro_new`);
}

export function load(name: string = "thugpro"): Profile {
    if (!(PROFILES as readonly string[]).includes(name)) {
        throw new RangeError(`no compatibility profile ${pyStrRepr(name)} (${PROFILES.join(", ")})`);
    }
    return new Profile(name);
}
