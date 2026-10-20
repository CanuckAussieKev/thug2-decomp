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

export function load(name: string = "thugpro"): Profile {
    if (!(PROFILES as readonly string[]).includes(name)) {
        throw new RangeError(`no compatibility profile ${pyStrRepr(name)} (${PROFILES.join(", ")})`);
    }
    return new Profile(name);
}
