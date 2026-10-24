// What tools/thp8_save takes from THP8's own data (C:\Games\THP8\game\DATA, read only): the checksum names
// (PAK\dbg.pak.xen), qb.pak's globals and scripts (the save writer's name tables, the career's atoms, goals, flags,
// shop lists, stat thresholds) and cas.pak's globals (the CAS parts' unlock flags).

import * as fs from "node:fs";
import * as path from "node:path";
import { checksum, Name, type QbValue, Struct } from "../level_port/qb.ts";
import * as thaw from "../thaw_levels/thaw.ts";
import * as thp8 from "../thp8_levels/thp8.ts";
import type { NameTables } from "./save.ts";

export interface GameData {
    names: Map<number, string>;
    tables: NameTables;
    // qb.pak's globals by checksum (the first definition) and its script bodies (THUG2 tokens + THP8's)
    globals: Map<number, QbValue>;
    scripts: Map<number, Buffer>;
    // cas.pak's globals
    casGlobals: Map<number, QbValue>;
    // goal_infos.pak's goals: name checksum -> goal_type (photo, chalk, story...)
    goalTypes: Map<number, number>;
}

function findCaseless(dir: string, ...parts: string[]): string {
    let at = dir;
    for (const part of parts) {
        const hit = fs.readdirSync(at).find((e) => e.toLowerCase() === part.toLowerCase());
        if (!hit) throw new Error(`${path.join(at, part)}: not found`);
        at = path.join(at, hit);
    }
    return at;
}

export function loadGameData(thp8Folder: string): GameData {
    const compressed = findCaseless(thp8Folder, "game", "DATA", "COMPRESSED");
    const names = thp8.loadNames(findCaseless(compressed, "PAK", "dbg.pak.xen"));
    const read = (file: string, globals: Map<number, QbValue>, scripts: Map<number, Buffer>): void => {
        const pak = thp8.openPak(findCaseless(compressed, "PAK", file));
        for (const e of pak.entries) {
            if (e.extension !== ".qb") continue;
            const q = new thaw.QbItems(thp8.entryData(pak, e), file, names, ">");
            for (const [k, v] of q.globals) if (!globals.has(k.value)) globals.set(k.value, v);
            for (const [k, v] of q.scriptData) if (!scripts.has(k)) scripts.set(k, v);
        }
    };
    const globals = new Map<number, QbValue>();
    const scripts = new Map<number, Buffer>();
    read("qb.pak.xen", globals, scripts);
    const casGlobals = new Map<number, QbValue>();
    read("cas.pak.xen", casGlobals, new Map());
    const goalGlobals = new Map<number, QbValue>();
    read("goal_infos.pak.xen", goalGlobals, new Map());
    const goalTypes = new Map<number, number>();
    for (const list of ["goal_infos", "goal_infos_pakless"]) {
        const v = goalGlobals.get(checksum(list));
        if (!Array.isArray(v)) continue;
        for (const g of v) {
            if (!(g instanceof Struct)) continue;
            const name = g.get("Name");
            const type = g.get("goal_type");
            if (typeof name === "string" && type instanceof Name) goalTypes.set(checksum(name), type.value);
        }
    }
    const table = (name: string): number[] => {
        const v = globals.get(checksum(name));
        if (!Array.isArray(v)) throw new Error(`qb.pak.xen: no ${name}`);
        return v.map((x) => (x instanceof Name ? x.value : 0));
    };
    return {
        names,
        tables: {
            short: table("writetobuffer_compressionlookuptable_8"),
            medium: table("writetobuffer_compressionlookuptable_16"),
        },
        globals,
        scripts,
        casGlobals,
        goalTypes,
    };
}
