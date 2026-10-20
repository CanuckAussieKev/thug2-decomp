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

// --- port_changes: this project's own changes to a source game's data (not THUGPRO's) ----------------------------------
// Kept in the same file as the game's THUGPRO data (thugpro\<tool>.json, section `port_changes`: {_comment, entries}),
// but read whatever the profile - `--compat none` keeps them. Each entry: id (PC-n), op (add / set / remove), target,
// why, and optionally `required` (a change the game needs to load or run the level: applied even with
// --no-port-changes) and `levels` (the levels it applies to; every level when absent), plus the data the converter
// takes from it.
export interface PortChange {
    id: string;
    op: "add" | "set" | "remove";
    target: string;
    why: string;
    required?: boolean;
    levels?: string[];
    [data: string]: unknown;
}

export class PortChanges {
    readonly entries: PortChange[];
    readonly enabled: boolean;

    constructor(entries: PortChange[], enabled: boolean) {
        this.entries = entries;
        this.enabled = enabled;
    }

    // The entry when it applies (to `level`, lower case) - on, or required - else null. An id the file hasn't is an
    // error: the converter asks only for its own.
    get(id: string, level?: string): PortChange | null {
        const e = this.entries.find((x) => x.id === id);
        if (e === undefined) throw new RangeError(`port_changes: no entry ${id}`);
        if (!this.enabled && !e.required) return null;
        if (level !== undefined && e.levels !== undefined && !e.levels.some((l) => l.toLowerCase() === level))
            return null;
        return e;
    }

    on(id: string, level?: string): boolean {
        return this.get(id, level) !== null;
    }
}

export function portChanges(tool: string, enabled = true): PortChanges {
    const file = path.join(HERE, "thugpro", `${tool}.json`);
    const data = fs.existsSync(file) ? (JSON.parse(fs.readFileSync(file, "utf-8")) as JsonObject) : {};
    const section = (data.port_changes ?? {}) as { entries?: PortChange[] };
    return new PortChanges(section.entries ?? [], enabled);
}

export function load(name: string = "thugpro"): Profile {
    if (!(PROFILES as readonly string[]).includes(name)) {
        throw new RangeError(`no compatibility profile ${pyStrRepr(name)} (${PROFILES.join(", ")})`);
    }
    return new Profile(name);
}
