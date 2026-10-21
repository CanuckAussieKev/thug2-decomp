// NOT ORIGINAL - the people a level port brings with its own models (docs/ped-models-plan.md): what it adds to THUG2's
// ped profile lists, kept in its staging folder until tools/menu_level/make_menu_level.ts puts them in the data overlay.
//
// A THUG2 ped is built from a profile (scripts\profile_ped.qb: a global structure naming a CAS part per key -
// ped_body / ped_m_head / ped_m_torso / ped_m_legs / ped_m_accessories / ped_f_*, the keys of cas_parts.qb's
// ped_editable_list) and the parts' entries in scripts\cas_ped_m.qb / cas_ped_f.qb ({desc_id mesh=...}: the model's
// files, models\...\<name>.skin.xbx / .tex.xbx / .col.xbx / .cas.xbx). A port adds entries to those files (the user's
// decision, 2026-10-21: add only, THUG2's own entries never changed): `<staging>\peds.json`
//
//   { "cas": { "<part list>": ["<NeverScript structure>", ...] }, "profiles": ["<name> = { ... }", ...] }
//
// written by the converter (writeAdditions), read from every staging folder and applied to the decompiled THUG2 files
// by make_menu_level (casEdit / profileEdit). The models and their animations are files of the staging's models\ and
// anims\ folders (ported_levels.installFiles copies them into the overlay).

import * as fs from "node:fs";
import * as path from "node:path";
import { STAGINGS } from "./ported_levels.ts";

export const FILE = "peds.json";

export interface Additions {
    cas: Record<string, string[]>; // part list (ped_m_head...) -> its new entries, NeverScript structures
    profiles: string[]; // NeverScript globals: `<name> = { ... }`
}

// THUG2's files the part lists are in (the rest: cas_ped_m.qb).
export const FEMALE_LISTS: ReadonlySet<string> = new Set([
    "ped_f_head",
    "ped_f_torso",
    "ped_f_legs",
    "ped_f_accessories",
]);
export const LISTS = [
    "ped_m_head",
    "ped_m_torso",
    "ped_m_legs",
    "ped_m_accessories",
    "ped_body",
    ...FEMALE_LISTS,
] as const;

export function fileOf(list: string): "cas_ped_m" | "cas_ped_f" {
    return FEMALE_LISTS.has(list.toLowerCase()) ? "cas_ped_f" : "cas_ped_m";
}

export function writeAdditions(staging: string, additions: Additions): void {
    fs.mkdirSync(staging, { recursive: true });
    fs.writeFileSync(path.join(staging, FILE), `${JSON.stringify(additions, null, 1)}\n`);
}

// Every staging folder's additions in the game folder, merged (in STAGINGS' order).
export function readAdditions(game: string): Additions {
    const all: Additions = { cas: {}, profiles: [] };
    for (const staging of STAGINGS) {
        const file = path.join(game, staging, FILE);
        if (!fs.existsSync(file)) continue;
        const one = JSON.parse(fs.readFileSync(file, "utf-8")) as Additions;
        for (const [list, entries] of Object.entries(one.cas)) all.cas[list] = [...(all.cas[list] ?? []), ...entries];
        all.profiles.push(...one.profiles);
    }
    return all;
}

export function empty(additions: Additions): boolean {
    return !additions.profiles.length && Object.values(additions.cas).every((e) => !e.length);
}

// The decompiled file's `<list> = [ ... ]` with `entries` added at its end (before the line that closes it: a `]` at
// the start of a line, as NeverScript writes top-level arrays).
export function appendToArray(text: string, list: string, entries: readonly string[]): string {
    if (!entries.length) return text;
    const start = new RegExp(`^${list} = \\[\\s*$`, "im").exec(text);
    if (start === null) throw new Error(`no ${list} = [ in the file`);
    const close = /^\][ \t]*$/m.exec(text.slice(start.index));
    if (close === null) throw new Error(`${list}: no closing ]`);
    const at = start.index + close.index;
    const added = entries.map((e) => `${e.replace(/^/gm, "    ")}\n`).join("");
    return text.slice(0, at) + added + text.slice(at);
}

// Globals added before the file's __register_checksums__ line (NeverScript's name list), or at its end.
export function appendGlobals(text: string, globals: readonly string[]): string {
    if (!globals.length) return text;
    const added = globals.map((g) => `${g}\n`).join("");
    const register = /^__register_checksums__/m.exec(text);
    if (register === null) return `${text}${text.endsWith("\n") ? "" : "\n"}${added}`;
    return text.slice(0, register.index) + added + text.slice(register.index);
}

// The edits make_menu_level applies (also to the Anteara Mod's copies of the files).
export function casEdit(additions: Additions, file: "cas_ped_m" | "cas_ped_f"): (text: string) => string {
    return (text) => {
        let out = text;
        for (const [list, entries] of Object.entries(additions.cas)) {
            if (fileOf(list) === file) out = appendToArray(out, list, entries);
        }
        return out;
    };
}

export function profileEdit(additions: Additions): (text: string) => string {
    return (text) => appendGlobals(text, additions.profiles);
}
