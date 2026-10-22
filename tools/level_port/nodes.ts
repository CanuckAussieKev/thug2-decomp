// THPS4 / THUG1 node arrays -> THUG2: keep what THUG2 understands without the other game's scripts and assets
// (restarts, rails, level geometry, KOTH crowns), drop the rest (peds, vehicles, game objects with the game's models,
// goal nodes, waypoints, particles, lights...), renumber the Links.
//
// The functions working on NeverScript text use JavaScript regexes for Python's: \w and \b are ASCII here, Unicode in
// Python - the same for this text, whose names are ASCII (qb.nameText writes others as #checksums).

import * as fs from "node:fs";
import * as path from "node:path";
import { getCloseMatches } from "../lib/difflib.ts";
import { decodeLatin1, intFromBytesLE, pyMinBy, pyParseFloat, pySum } from "../lib/py.ts";
import { findall, type Match, search, sub } from "../lib/re.ts";
import { pack } from "../lib/struct.ts";
import { FormatError } from "./binio.ts";
import * as qb from "./qb.ts";
import { checksum, Name, type QbFile, type QbValue, type Statement, Struct, Vector } from "./qb.ts";
import { type Element, encodeValue, globalArraySpan, replaceGlobalArray } from "./qb_write.ts";
import * as thug2Col from "./thug2_col.ts";
import { TABLE_MEMBERS } from "./thug2_members.ts";

export type Names = Map<number, string>;

export const KEEP_CLASSES: ReadonlySet<string> = new Set([
    "restart",
    "railnode",
    "levelgeometry",
    "levelobject",
    "genericnode",
]);
export const GENERIC_TYPES: ReadonlySet<string> = new Set(["crown"]); // GenericNode kept: KOTH crowns
// Keys and flags kept on a node; everything else (TriggerScript, models, ped / object settings...) goes.
export const KEEP_KEYS: ReadonlySet<string> = new Set([
    "position",
    "angles",
    "name",
    "class",
    "type",
    "restartname",
    "restart_types",
    "terraintype",
    "links",
    "cluster",
    "collisionmode",
]);
export const KEEP_FLAGS: ReadonlySet<string> = new Set([
    "createdatstart",
    "trickobject",
    "absentinnetgames",
    "netenabled",
]);
const KEYS: ReadonlySet<number> = new Set([...KEEP_KEYS].map(checksum));
const FLAGS: ReadonlySet<number> = new Set([...KEEP_FLAGS].map(checksum));

export function classOf(node: Struct, names: ReadonlyMap<number, string>): string {
    const value = node.get("Class");
    return (value instanceof Name ? (names.get(value.value) ?? "") : "").toLowerCase();
}

export function typeOf(node: Struct, names: ReadonlyMap<number, string>): string {
    const value = node.get("Type");
    return (value instanceof Name ? (names.get(value.value) ?? "") : "").toLowerCase();
}

// Names with "kill" in them that don't kill the skater: the Kill command (removes objects), KillSpawnedScript,
// KillSkaterCamAnim (stops a camera animation - THPS4's skate shop's start point runs it: taken as a kill, it made the
// start point respawn the skater on itself forever), and THPS4's key bindings, kill radius / clusters, goal adding,
// arrows, cars, race checks.
export const NOT_KILLS = [
    "camanim",
    "killspawned",
    "key_binding",
    "radius",
    "cluster",
    "addgoal",
    "arrow",
    "cars",
    "checkfor",
];

export function isKillCall(text: string): boolean {
    // Whether a called name (lower case) is a kill of the skater (SK3_KillSkater, ALC_KillWater, xxx_Kill_Plane...).
    return text.includes("kill") && text !== "kill" && !NOT_KILLS.some((word) => text.includes(word));
}

export function killScripts(nodeQ: QbFile, levelQ: QbFile | null): Map<number, string> {
    // THPS4's trigger scripts that kill the skater (out of bounds, water): TriggerScript -> the THUG2 command that
    // does the same (THUG2's global SK3_KillSkater / SK3_KillSkater_Water: back to a restart). A trigger script
    // counts when it calls a script or command with "kill" in its name (ALC_KillWater, SK3_KillSkater,
    // xxx_Kill_Plane...; not the Kill command, which removes objects, or KillSpawnedScript).
    const names: Names = new Map(levelQ ? levelQ.names : []);
    for (const [k, v] of nodeQ.names) names.set(k, v);
    const found = new Map<number, string>();
    for (const script of nodeQ.scripts.keys()) {
        for (const [called, line] of nodeQ.calls(script)) {
            const text = (names.get(called) ?? "").toLowerCase();
            if (!text.includes("kill") || !isKillCall(text)) continue;
            const water =
                text.includes("water") ||
                line.some(([t, p]) => t === 0x16 && (names.get(intFromBytesLE(p)) ?? "").toLowerCase() === "water");
            found.set(script, water ? "SK3_KillSkater_Water" : "SK3_KillSkater");
            break;
        }
    }
    return found;
}

// The kill call's parameters kept (THUG2's SK3_KillSkater takes the same: the restart to go back to, the messages, the
// fade colour, bail; THUG1's own death sounds aren't in THUG2).
export const KILL_VALUES: ReadonlySet<string> = new Set([
    "nodename",
    "node",
    "message1",
    "message2",
    "message3",
    "message4",
    "r",
    "g",
    "b",
]);
export const KILL_FLAGS: ReadonlySet<string> = new Set(["bail", "nomessage"]);

export function killArgs(
    line: Statement,
    restarts: { has(value: number): boolean },
    names: ReadonlyMap<number, string>,
): Map<string, string> {
    // The parameters of a kill call (a statement's tokens after the called name) that THUG2's SK3_KillSkater takes,
    // as {key: NeverScript text}: nodename (node= too) only when it names a restart, messages, colour, Bail /
    // NoMessage.
    const args = new Map<string, string>();
    let i = 1;
    while (i < line.length) {
        const [t, payload] = line[i]!;
        if (t === 0x16) {
            const keyText = (names.get(intFromBytesLE(payload)) ?? "").toLowerCase();
            if (i + 2 < line.length && line[i + 1]![0] === 0x07) {
                const [vt, vp] = line[i + 2]!;
                if ((keyText === "nodename" || keyText === "node") && vt === 0x16) {
                    const node = intFromBytesLE(vp);
                    if (restarts.has(node)) args.set("nodename", `nodename=${qb.nameText(node, names)}`);
                } else if (KILL_VALUES.has(keyText) && vt === 0x1b) {
                    const text = decodeLatin1(qb.rstripNul(vp)).replaceAll("\\", "\\\\").replaceAll('"', '\\"');
                    args.set(keyText, `${keyText}="${text}"`);
                } else if (KILL_VALUES.has(keyText) && vt === 0x17) {
                    args.set(keyText, `${keyText}=${intFromBytesLE(vp, true)}`);
                }
                i += 3;
                continue;
            }
            if (KILL_FLAGS.has(keyText)) args.set(keyText, keyText === "bail" ? "Bail" : "NoMessage");
        }
        i += 1;
    }
    return args;
}

export function killCommands(
    // (QbFile, or anything with its scripts and calls: THAW's QbItems)
    scriptQbs: readonly Pick<QbFile, "scripts" | "calls">[],
    restarts: { has(value: number): boolean },
    names: ReadonlyMap<number, string>,
): Map<number, string> {
    // THUG1's trigger scripts that kill the skater: TriggerScript -> the THUG2 command line that does the same. A
    // script counts when one of its statements calls something with "killskater" in its name (SK3_Killskater,
    // Sk3_Killskater_Water, the levels' own RU_KillSkater / HI_KillSkater_LittleDogs) or a level script that does
    // (NY_KillWater, HI_Pool_Water: goal checks around Sk3_Killskater_Water): it becomes THUG2's SK3_KillSkater
    // (_Water when the name says water, or the level script's call does) with the call's parameters that THUG2's
    // takes (killArgs); a level script's own kill call gives the ones the trigger script's call doesn't
    // (HI_Pool_Water's nodename=TRG_Restart_Boardwalk).
    const wrappers = new Map<number, [boolean, Map<string, string>]>();
    for (const q of scriptQbs) {
        for (const script of q.scripts.keys()) {
            for (const [called, line] of q.calls(script)) {
                const text = (names.get(called) ?? "").toLowerCase();
                if (text.includes("killskater") && isKillCall(text) && !wrappers.has(script)) {
                    wrappers.set(script, [text.includes("water"), killArgs(line, restarts, names)]);
                }
            }
        }
    }
    const found = new Map<number, string>();
    for (const q of scriptQbs) {
        for (const script of q.scripts.keys()) {
            if (found.has(script)) continue;
            for (const [called, line] of q.calls(script)) {
                const text = (names.get(called) ?? "").toLowerCase();
                if ((!text.includes("killskater") || !isKillCall(text)) && !wrappers.has(called)) continue;
                const [wrapperWater, wrapperArgs] = wrappers.get(called) ?? [false, new Map<string, string>()];
                const water = wrapperWater || text.includes("water");
                const args = new Map(wrapperArgs);
                for (const [k, v] of killArgs(line, restarts, names)) args.set(k, v);
                const command = water ? "SK3_KillSkater_Water" : "SK3_KillSkater";
                found.set(script, [command, ...args.values()].join(" "));
                break;
            }
        }
    }
    return found;
}

// Keys every node keeps for itself when the rest is shared (shareParts): what THUG2's own levels keep on the node
// (their ncomp_* hold the rest) - a node's identity, place and links. The level editor reads a node's fields through
// its includes and edits them as the node's own overrides (src/game/front/level_editor_fields.cpp), as on THUG2's own
// levels; tools/game/apply_layout.ts the same.
export const OWN_KEYS: ReadonlySet<number> = new Set(
    ["Pos", "Position", "Angles", "Name", "Links", "TriggerScript"].map(checksum),
);

export function shareParts(
    converted: readonly Struct[],
    prefix: string,
    shared: (node: Struct) => boolean = () => true,
): [Struct[], [string, Struct][]] {
    // expandIncludes undone: the parts nodes have in common (everything but OWN_KEYS) made global structures that the
    // nodes name - as THUG2's own levels have them (BE: `ncomp_RailNode_1 = {Class=RailNode Type=Concrete
    // CreatedAtStart TrickObject Cluster=... TerrainType=...}`, the rail nodes listing just Pos / Angles / Name, the
    // include, Links; THUGPRO's level compiler does it for every node kind too). THUG2's structure lookups follow the
    // include (Search / GetChecksum / ContainsFlag: src/game/script/struct.cpp IncludedStructure). One
    // `<prefix>_inc_<n>` per set of identical parts (the same tokens) that at least two nodes have; `shared` picks the
    // nodes that may take part. Every node's entries cost a script component (the CComponent pool, 86000 for
    // everything loaded): expanded node arrays bigger than THUG2's ran it dry while loading (THUG1 Hawaii / New
    // Jersey; THAW Kyoto). Returns [the nodes - the unshared ones the same objects -, the structures in order].
    const groups = new Map<string, [number, Struct]>();
    const sharedOf: (string | null)[] = [];
    for (const node of converted) {
        const parts = node.entries.filter(([k]) => k === null || !OWN_KEYS.has(k.value));
        let k: string | null = null;
        if (shared(node) && parts.length > 1) {
            const value = new Struct(parts);
            k = encodeValue(value).toString("hex");
            const group = groups.get(k);
            if (group) group[0] += 1;
            else groups.set(k, [1, value]);
        }
        sharedOf.push(k);
    }
    const made = new Map<string, Name>();
    const includes: [string, Struct][] = [];
    for (const [k, [count, value]] of groups) {
        if (count < 2) continue;
        const inc = `${prefix}_inc_${made.size}`;
        made.set(k, new Name(checksum(inc)));
        includes.push([inc, value]);
    }
    const linksKey = checksum("Links");
    const triggerKey = checksum("TriggerScript");
    const out = converted.map((node, i) => {
        const k = sharedOf[i]!;
        const include = k === null ? undefined : made.get(k);
        if (include === undefined) return node;
        const own = node.entries.filter(([key]) => key !== null && OWN_KEYS.has(key.value));
        const late = (e: qb.Entry): boolean => e[0]!.value === linksKey || e[0]!.value === triggerKey;
        return new Struct([...own.filter((e) => !late(e)), [null, include], ...own.filter(late)]);
    });
    return [out, includes];
}

// What shareNodeFields did to a level's .qb.
export interface Shared {
    data: Buffer;
    includes: number; // structures made
    nodes: number; // nodes naming one
}

export function shareNodeFields(data: Buffer, arrayName: string, prefix: string, file = "qb"): Shared {
    // A compiled level .qb (`data`, its scripts appended) with its node array's common parts shared (shareParts) - every
    // node kind: the include structures defined just before the array, the nodes that name one rewritten, every other
    // byte kept (the unshared nodes too). The converters' LAST step on a level's .qb, after every pass that reads nodes'
    // own fields (the text passes stripMissingTrickObjects, absentInNetGames / netRestarts, groundRestarts; the profile
    // fix, the THUGPRO register's node edits...): a field moved into an include would be invisible to them
    // (assertUnshared stops a text pass given shared nodes). A node array naming a global structure already is an
    // error: sharing runs once.
    const q = new qb.QbFile(data, file, true);
    const arrayKey = checksum(arrayName);
    const array = q.globals.find(([k, v]) => k.value === arrayKey && Array.isArray(v))?.[1];
    if (!Array.isArray(array)) throw new FormatError(`${file}: no node array ${arrayName}`);
    const structures = new Set(q.globals.filter(([, v]) => v instanceof Struct).map(([k]) => k.value));
    const list: Struct[] = [];
    for (const node of array) {
        if (!(node instanceof Struct)) throw new FormatError(`${file}: ${arrayName} has an element that isn't a node`);
        for (const [k, v] of node.entries) {
            if (k === null && v instanceof Name && structures.has(v.value)) {
                throw new FormatError(
                    `${file}: ${arrayName}'s nodes name shared structures already (${qb.nameText(v.value, q.names)}): ` +
                        "sharing runs once, last",
                );
            }
        }
        list.push(node);
    }
    const [out, includes] = shareParts(list, prefix);
    if (!includes.length) return { data, includes: 0, nodes: 0 };
    for (const [inc] of includes) {
        if (q.globals.some(([k]) => k.value === checksum(inc)))
            throw new FormatError(`${file}: ${inc} defined already`);
    }
    const elements: Element[] = out.map((n, i) => (n === list[i] ? { keep: i } : { value: n }));
    const newNames = new Map(includes.map(([inc]): [number, string] => [checksum(inc), inc]));
    const replaced = replaceGlobalArray(data, arrayKey, elements, newNames, true);
    // (the array's `name =`: its name token before the value; the bytes before it are the same in `replaced`)
    const span = globalArraySpan(data, arrayKey)!;
    let at = -1;
    for (const [offset, t, payload] of qb.tokens(data, true)) {
        if (offset >= span[0]) break;
        if (t === 0x16 && payload.readUInt32LE(0) === arrayKey >>> 0) at = offset;
    }
    if (at < 0) throw new FormatError(`${file}: ${arrayName}'s name not found`);
    const definitions = includes.flatMap(([inc, value]) => [
        Buffer.from([0x16]),
        pack("<I", checksum(inc) >>> 0),
        Buffer.from([0x07]),
        encodeValue(value),
        Buffer.from([0x01]),
    ]);
    const result = Buffer.concat([replaced.subarray(0, at), ...definitions, replaced.subarray(at)]);
    const differ = sharedDifferences(data, result, arrayName, file);
    if (differ.length)
        throw new FormatError(`${file}: sharing changed nodes' fields: ${differ.slice(0, 3).join("; ")}`);
    return { data: result, includes: includes.length, nodes: out.filter((n, i) => n !== list[i]).length };
}

// A node's fields as THUG2 resolves them, in a form to compare: its own (OWN_KEYS) entries in order, and the rest in
// order with every global structure it names put in place (what a structure lookup sees through the include).
export function resolvedFields(node: QbValue, globals: ReadonlyMap<number, QbValue>): string {
    if (!(node instanceof Struct)) return encodeValue(node).toString("hex");
    const [expanded] = expandIncludes([node], globals) as Struct[];
    const own = expanded!.entries.filter(([k]) => k !== null && OWN_KEYS.has(k.value));
    const rest = expanded!.entries.filter(([k]) => k === null || !OWN_KEYS.has(k.value));
    return `${encodeValue(new Struct(own)).toString("hex")}|${encodeValue(new Struct(rest)).toString("hex")}`;
}

export function sharedDifferences(before: Buffer, after: Buffer, arrayName: string, file = "qb"): string[] {
    // The structural check of sharing (shareNodeFields runs it on every level it writes): every node of `arrayName` in
    // `after` resolves to the same fields as the same node in `before` (resolvedFields), and every other global is
    // the same. Returns the differences (none: the same behaviour).
    const a = new qb.QbFile(before, file, true);
    const b = new qb.QbFile(after, file, true);
    const ga = new Map(a.globals.map(([k, v]): [number, QbValue] => [k.value, v]));
    const gb = new Map(b.globals.map(([k, v]): [number, QbValue] => [k.value, v]));
    const arrayKey = checksum(arrayName);
    const na = ga.get(arrayKey);
    const nb = gb.get(arrayKey);
    if (!Array.isArray(na) || !Array.isArray(nb)) return [`no node array ${arrayName}`];
    const out: string[] = [];
    if (na.length !== nb.length) out.push(`${na.length} nodes before, ${nb.length} after`);
    for (let i = 0; i < Math.min(na.length, nb.length); i++) {
        if (resolvedFields(na[i]!, ga) !== resolvedFields(nb[i]!, gb)) out.push(`node ${i}`);
    }
    for (const [k, v] of ga) {
        if (k === arrayKey) continue;
        const w = gb.get(k);
        if (w === undefined || encodeValue(v).toString("hex") !== encodeValue(w).toString("hex"))
            out.push(`global ${qb.nameText(k, a.names)}`);
    }
    return out;
}

export function shareNodeFieldsIn(file: string, arrayName: string, prefix: string): Shared {
    // shareNodeFields on the .qb file `file`, written back.
    const shared = shareNodeFields(fs.readFileSync(file), arrayName, prefix, path.basename(file));
    if (shared.includes) fs.writeFileSync(file, shared.data);
    return shared;
}

export function unshareNodeFields(data: Buffer, arrayName: string, file = "qb"): Buffer {
    // shareNodeFields undone for a reader of a converted level's nodes as text (make_menu_level's front end takes
    // the converted skate shop's rails, peds and triggers by their Class): the nodes that name a global structure get
    // its entries in its place (expandIncludes); every other byte kept (the structures stay defined).
    const q = new qb.QbFile(data, file, true);
    const arrayKey = checksum(arrayName);
    const array = q.globals.find(([k, v]) => k.value === arrayKey && Array.isArray(v))?.[1];
    if (!Array.isArray(array)) throw new FormatError(`${file}: no node array ${arrayName}`);
    const globals = new Map(q.globals.map(([k, v]): [number, QbValue] => [k.value, v]));
    const includes = (node: QbValue): boolean =>
        node instanceof Struct &&
        node.entries.some(([k, v]) => k === null && v instanceof Name && globals.get(v.value) instanceof Struct);
    const expanded = expandIncludes(array, globals);
    const elements: Element[] = expanded.map((n, i) => (includes(array[i]!) ? { value: n } : { keep: i }));
    return replaceGlobalArray(data, arrayKey, elements, new Map(), true);
}

export function assertUnshared(text: string, pass: string): void {
    // The passes reading nodes' own fields (NeverScript text) run before sharing (shareNodeFields): a node naming a
    // structure the text defines (`<level>_inc_<n>`) is an error here, not a field silently missed.
    const defined = new Set<string>();
    for (const m of text.matchAll(/^(#?\w+) = \{/gm)) defined.add(m[1]!);
    if (!defined.size) return;
    for (const m of text.matchAll(/^[ \t]+(#?\w+)[ \t]*$/gm)) {
        if (defined.has(m[1]!)) {
            throw new FormatError(`${pass}: the nodes name ${m[1]}, shared already - the pass must run before sharing`);
        }
    }
}

export function expandIncludes(array: readonly QbValue[], globalsByChecksum: ReadonlyMap<number, QbValue>): QbValue[] {
    // THUG1's node arrays share their common parts: a node lists a global structure by name (ncomp_RailNode_574 =
    // {Class=RailNode Type=Metal ...}) - an unnamed Name entry naming a global structure, whose entries the node gets
    // (the script's AppendStructure). They are put in the nodes themselves here.
    const out: QbValue[] = [];
    for (let node of array) {
        if (node instanceof Struct) {
            const entries: qb.Entry[] = [];
            for (const [key, value] of node.entries) {
                const included = key === null && value instanceof Name ? globalsByChecksum.get(value.value) : undefined;
                if (included instanceof Struct) entries.push(...included.entries);
                else entries.push([key, value]);
            }
            node = new Struct(entries);
        }
        out.push(node);
    }
    return out;
}

const isInt = (v: QbValue): v is number => typeof v === "number" && Number.isInteger(v);
const asList = (v: QbValue): QbValue[] => {
    // (Python iterates the value: a list; anything else isn't iterable there)
    if (!Array.isArray(v)) throw new TypeError("Links is not a list");
    return v;
};

export function convertNodes(
    array: readonly QbValue[],
    names: Names,
    kills: { has(value: number): boolean } | null = null,
    extraKeys: readonly string[] = [],
    asIs: Iterable<Struct> = [],
    scripts: { has(value: number): boolean } = new Set<number>(),
    player1First = true,
): [Struct[], Map<string, number>] {
    // kills: the kill scripts (a map or set of their checksums). extraKeys: more keys kept (THUG1's Pos). asIs: nodes
    // (the objects themselves - Python's id()s) already made THUG2's - kept whole, their Links renumbered (THPS3's game
    // objects, waypoints, peds). scripts: trigger scripts that are kept besides the kill scripts (THPS3's, brought along
    // by tools/level_port/script_port.ts). player1First: false keeps the array's own order (the swap below left out -
    // tools/thug1_levels' TP-31: THUG2's net spawns go by the restarts' order).
    const killMap = kills ?? new Set<number>();
    const asIsSet = new Set<Struct>(asIs);
    const keepKeys = new Set([...KEYS, ...extraKeys.map(checksum)]);
    const kept: Struct[] = [];
    const indexMap = new Map<number, number>();
    array.forEach((node, i) => {
        if (!(node instanceof Struct)) return;
        if (asIsSet.has(node)) {
            indexMap.set(i, kept.length);
            kept.push(node);
            return;
        }
        const cls = classOf(node, names);
        if (!KEEP_CLASSES.has(cls)) return;
        if (cls === "genericnode" && !GENERIC_TYPES.has(typeOf(node, names))) return;
        indexMap.set(i, kept.length);
        kept.push(node);
    });
    const renumber = (value: QbValue): number[] =>
        asList(value)
            .filter((v): v is number => isInt(v) && indexMap.has(v))
            .map((v) => indexMap.get(v)!);
    const LINKS = checksum("Links");
    const TRIGGER = checksum("TriggerScript");
    const CLASS = checksum("Class");
    const out: Struct[] = [];
    const stats = new Map<string, number>();
    for (const node of kept) {
        const cls = classOf(node, names);
        stats.set(cls, (stats.get(cls) ?? 0) + 1);
        if (asIsSet.has(node)) {
            const entries: qb.Entry[] = [];
            for (let [key, value] of node.entries) {
                if (key !== null && key.value === LINKS) {
                    value = renumber(value);
                    if (!value.length) continue;
                }
                entries.push([key, value]);
            }
            out.push(new Struct(entries));
            continue;
        }
        const entries: qb.Entry[] = [];
        for (let [key, value] of node.entries) {
            if (key === null) {
                if (value instanceof Name && FLAGS.has(value.value)) entries.push([null, value]);
                continue;
            }
            if (
                key.value === TRIGGER &&
                value instanceof Name &&
                (killMap.has(value.value) || scripts.has(value.value))
            ) {
                entries.push([key, value]);
                continue;
            }
            if (!keepKeys.has(key.value)) continue;
            if (key.value === LINKS) {
                value = renumber(value);
                if (!value.length) continue;
            }
            if (key.value === CLASS && cls === "levelobject") {
                // (a movable object in THPS4, driven by its scripts: static geometry here)
                value = new Name(checksum("LevelGeometry"));
                if (!names.has(value.value)) names.set(value.value, "LevelGeometry");
            }
            entries.push([key, value]);
        }
        const fresh = new Struct(entries);
        if (cls === "restart" && fresh.get("restart_types") === undefined && fresh.get("Type") instanceof Name) {
            fresh.entries.push([new Name(checksum("restart_types")), [fresh.get("Type")!]]);
            if (!names.has(checksum("restart_types"))) names.set(checksum("restart_types"), "restart_types");
        }
        out.push(fresh);
    }
    // The skater starts at the first node of the array that is a player restart (as make_menu_level.ts's skate
    // shop): THPS4's Player1 restart is swapped with the first node, and the Links follow.
    const player1 = checksum("Player1");
    const first = out.findIndex((n) => {
        const type = n.get("Type");
        return type instanceof Name && type.value === player1 && classOf(n, names) === "restart";
    });
    if (player1First && first > 0) {
        [out[0], out[first]] = [out[first]!, out[0]!];
        const swap = new Map([
            [0, first],
            [first, 0],
        ]);
        for (const node of out) {
            node.entries = node.entries.map(([k, value]) => [
                k,
                k !== null && k.value === LINKS
                    ? asList(value).map((v) => (typeof v === "number" && swap.has(v) ? swap.get(v)! : v))
                    : value,
            ]);
        }
    }
    return [out, stats];
}

export function nodePosition(node: Struct): readonly number[] | null {
    let value = node.get("Pos");
    if (value === undefined) value = node.get("Position");
    return value instanceof Vector && value.kind === "vector" ? value.values : null;
}

const distance2 = (a: readonly number[], b: readonly number[]): number =>
    // (Python: sum((a[k] - b[k]) ** 2 for k in range(3)) - compensated sum; ** 2 vs * : docs/typescript.md)
    pySum(
        [0, 1, 2].map((k) => {
            const d = a[k]! - b[k]!;
            return d * d;
        }),
    );

export function addKillRestarts(
    nodes: readonly Struct[],
    kills: ReadonlyMap<number, string>,
    names: ReadonlyMap<number, string>,
): Map<number, string> {
    // THUG2's SK3_KillSkater needs a restart: without nodename= it passes the global kill_skater_nodename (0 until a
    // kill names one) to KillSkater node=, which finds no node - the skater stays dead, never put back (seen in THUG1
    // Hawaii's pool). A kill command without one gets the restart nearest to the first trigger node that runs it.
    // A restart's own TriggerScript runs when the skater is put there: never a kill (it would put the skater back on
    // itself forever).
    const triggerOf = (n: Struct): number | null => {
        const t = n.get("TriggerScript");
        return t instanceof Name ? t.value : null;
    };
    const filtered = new Map(
        [...kills].filter(([script]) => !nodes.some((n) => classOf(n, names) === "restart" && triggerOf(n) === script)),
    );
    const restarts: [readonly number[], Name][] = [];
    for (const n of nodes) {
        const position = nodePosition(n);
        const name = n.get("Name");
        if (classOf(n, names) === "restart" && position && name instanceof Name) restarts.push([position, name]);
    }
    if (!restarts.length) return filtered;
    const out = new Map(filtered);
    for (const [script, command] of filtered) {
        if (command.includes("nodename=")) continue;
        let where: readonly number[] | null = null;
        for (const n of nodes) {
            if (triggerOf(n) === script && nodePosition(n)) {
                where = nodePosition(n);
                break;
            }
        }
        if (where === null) continue;
        const at = where;
        const [, restart] = pyMinBy(restarts, (r) => distance2(r[0], at));
        const space = command.indexOf(" ");
        const head = space < 0 ? command : command.slice(0, space);
        const rest = space < 0 ? "" : ` ${command.slice(space + 1)}`;
        out.set(script, `${head} nodename=${qb.nameText(restart.value, names)}${rest}`);
    }
    return out;
}

export function killTriggerNames(nodes: readonly Struct[], kills: { has(value: number): boolean }): Set<number> {
    // The checksums of the kept nodes whose TriggerScript is a kill script: their collision keeps its trigger faces.
    const found = new Set<number>();
    for (const node of nodes) {
        const script = node.get("TriggerScript");
        const name = node.get("Name");
        if (script instanceof Name && kills.has(script.value) && name instanceof Name) found.add(name.value);
    }
    return found;
}

// The terrains whose sounds (rolling, landing, grinding) a ported level loads: load_level runs SetTerrainDefault, then
// the level's own LoadTerrain script - without it only TERRAIN_DEFAULT's sounds are loaded, and rolling over wood,
// metal, concrete... is silent. THUG2's own LA loads these.
export const TERRAINS = [
    "DEFAULT",
    "CONCSMOOTH",
    "CONCROUGH",
    "METALSMOOTH",
    "METALGRATING",
    "WOOD",
    "BRICK",
    "TILE",
    "ASPHALT",
    "GRAVEL",
    "SIDEWALK",
    "GRASS",
    "WATER",
    "PLEXIGLASS",
    "CHAINLINK",
    "GRINDWIRE",
    "GRINDCONC",
    "GRINDCHAINLINK",
    "GRINDMETAL",
    "GRINDWOOD",
    "GRINDELECTRICWIRE",
    "GRINDGLASSMONSTER",
    "GRINDBANYONTREE",
];

export function loadTerrainNs(): string {
    return `script LoadTerrain {\n${TERRAINS.map((t) => `    LoadTerrainSounds terrain=TERRAIN_${t} \n`).join("")}}`;
}

// THUG2's terrain types (scripts\game\terrain.qb): a node's TerrainType has to be one of them.
export const THUG2_TERRAINS = (
    "ASPHALT BRICK CARPET CHAINLINK CONCROUGH CONCSMOOTH CONVEYOR DEFAULT DIRT DIRTPACKED FIBERGLASS GENERIC1 GENERIC2 " +
    "GRASS GRASSDRIED GRAVEL GRINDBANYONTREE GRINDBRASSRAIL GRINDCABLE GRINDCATWALK GRINDCHAIN GRINDCHAINLINK " +
    "GRINDCONC GRINDELECTRICWIRE GRINDGLASSMONSTER GRINDMETAL GRINDNEONLIGHT GRINDPLASTIC GRINDPLASTICBARRIER GRINDROPE " +
    "GRINDROUNDMETALPOLE GRINDTANKTURRET GRINDTRAIN GRINDWIRE GRINDWOOD GRINDWOODLOG GRINDWOODRAILING ICE " +
    "METALCORRUGATED METALFENCE METALFUTURE METALGRATING METALROUGH METALSMOOTH METALTIN PLEXIGLASS ROCK SAND SIDEWALK " +
    "SNOW TILE WATER WETCONC WHEELS WOOD WOODFLIMSY WOODMASONITE WOODPIER WOODPLYWOOD WOODSHINGLE"
).split(" ");

export function fixTerrains(nodes: readonly Struct[], names: Names): number {
    // THUG2 looks a rail node's TerrainType up in its terrain table and reads through a null pointer when it's missing
    // or not one of THUG2_TERRAINS (THPS3 Rio: a rail without one; Tokyo: THPS3's own typo TERRAIN_MEATALSMOOTH -
    // both crashed the game loading the level). Missing on a rail: TERRAIN_METALSMOOTH; unknown on any node: THUG2's
    // nearest name (difflib), else TERRAIN_METALSMOOTH. Returns how many were changed.
    const key = checksum("TerrainType");
    if (!names.has(key)) names.set(key, "TerrainType");
    const known = new Map<number, string>();
    for (const t of THUG2_TERRAINS) known.set(checksum(`TERRAIN_${t}`), `TERRAIN_${t}`);

    const terrain = (text: string): Name => {
        const value = checksum(text);
        if (!names.has(value)) names.set(value, text);
        return new Name(value);
    };

    let count = 0;
    for (const node of nodes) {
        const value = node.get("TerrainType");
        if (value === undefined) {
            if (classOf(node, names) === "railnode") {
                node.entries.push([new Name(key), terrain("TERRAIN_METALSMOOTH")]);
                count += 1;
            }
            continue;
        }
        if (value instanceof Name && known.has(value.value)) continue;
        const text = value instanceof Name ? (names.get(value.value) ?? "").toUpperCase() : "";
        const close = text ? getCloseMatches(text, [...known.values()], 1, 0.8) : [];
        const replacement = terrain(close.length ? close[0]! : "TERRAIN_METALSMOOTH");
        node.entries = node.entries.map(([k, v]) => [k, k !== null && k.value === key ? replacement : v]);
        count += 1;
    }
    return count;
}

export function fixProfiles(nodes: readonly Struct[], names: Names, known: (value: number) => boolean): string[] {
    // A ped's `profile` has to name a structure THUG2 defines (scripts\profile_ped.qb, or the level's own globals):
    // ped_init_model runs `Obj_InitModelFromProfile struct=<profile>`, and with a name that isn't a structure its
    // GetStructure (0x004767C0) fails without writing, so 0x0042270C passes an uninitialised local on to 0x00488F40,
    // whose ExpandInto reads through it - the original crashes the same way (THPS3 Rio: THUGPRO's TRG_Ped_2084 with
    // THUGPRO's random_sk3_male_profile, TPN-THPS3-2). An unknown one: THUG2's random_female_profile for a woman
    // (Ped_F_From_Profile), else random_male_profile. Returns the changed nodes' names.
    const key = checksum("profile");
    const profile = (text: string): Name => {
        const value = checksum(text);
        if (!names.has(value)) names.set(value, text);
        return new Name(value);
    };
    const changed: string[] = [];
    for (const node of nodes) {
        const value = node.get("profile");
        if (!(value instanceof Name) || known(value.value)) continue;
        const replacement = profile(
            typeOf(node, names) === "ped_f_from_profile" ? "random_female_profile" : "random_male_profile",
        );
        node.entries = node.entries.map(([k, v]) => [k, k !== null && k.value === key ? replacement : v]);
        const nm = node.get("Name");
        changed.push(
            `${nm instanceof Name ? qb.nameText(nm.value, names) : "?"}: ${qb.nameText(value.value, names)} -> ` +
                qb.nameText(replacement.value, names),
        );
    }
    return changed;
}

export function nodeArrayNs(
    arrayName: string,
    nodes: readonly Struct[],
    names: Names,
    kills: ReadonlyMap<number, string> | null = null,
): string {
    fixTerrains(nodes, names);
    const lines = [`${arrayName} = [`];
    for (const node of nodes) lines.push(`    ${qb.toNs(node, names, 1)}`);
    lines.push("]");
    const used = new Set<number>();
    for (const node of nodes) {
        const script = node.get("TriggerScript");
        if (script instanceof Name) used.add(script.value);
    }
    const sorted = [...(kills ?? new Map<number, string>())].sort(
        (a, b) => a[0] - b[0] || (a[1] < b[1] ? -1 : a[1] > b[1] ? 1 : 0),
    );
    for (const [script, command] of sorted) {
        if (used.has(script)) lines.push(`script ${qb.nameText(script, names)} {\n    ${command}\n}`);
    }
    lines.push(loadTerrainNs());
    return `${lines.join("\n")}\n`;
}

const ELEMENT = /\{[^{}]*\}/;

export function stripMissingTrickObjects(text: string, sectors: { has(value: number): boolean }): [string, number] {
    assertUnshared(text, "stripMissingTrickObjects");
    // The node array (NeverScript text) with TrickObject / Cluster taken off the LevelGeometry nodes whose sector isn't
    // in the converted scene (`sectors`: collision-only or invisible pieces, left out of the scene): ParseNodeArray
    // registers a TrickObject's sector without checking it's there (0x00588630 reads through the null - THPS3
    // Airport and THAW Minneapolis crashed loading). Rails keep theirs (no sectors). Returns [text, nodes changed].
    let cleared = 0;
    const fix = (match: Match): string => {
        let node = match.str(0);
        const name = search(/\bName=(#?\w+)/i, node);
        if (!name || !search(/\bClass=LevelGeometry\b/i, node) || !search(/\bTrickObject\b|\bCluster=/i, node))
            return node;
        const value = nameValue(name.str(1));
        if (sectors.has(value)) return node;
        cleared += 1;
        node = sub(/\n[ \t]*TrickObject[ \t]*(?=\n)/i, "", node);
        return sub(/\n[ \t]*Cluster=\S+[ \t]*(?=\n)/i, "", node);
    };
    return [sub(ELEMENT, fix, text), cleared];
}

// The path commands that need the path manager, which THUG2 makes only outside network games (load_level's
// AllocatePathManMemory): an object following a path in a network game reads through the missing manager
// (0x004695D0 / 0x00469620 - THPS4 College's blimp crashed a network game, as TestLevel's beach ball did).
export const PATH_COMMANDS = ["Obj_FollowPath", "Obj_FollowPathLinked", "Obj_FollowPathStored"];
const PATH_COMMAND_SET: ReadonlySet<number> = new Set(PATH_COMMANDS.map(checksum));

export function nameValue(text: string): number {
    // (#xxxxxxxx as qb.nameText writes it - NeverScript's: the checksum's bytes in file order, little-endian)
    return text.startsWith("#") ? Buffer.from(text.slice(1), "hex").readUInt32LE(0) : checksum(text);
}

const SCRIPT_BLOCK = /(?:^|\n)script (#?\w+) \{(.*?)\n\}/s;

export function absentInNetGames(
    text: string,
    scripts: ReadonlyMap<number, Uint8Array> | null = null,
): [string, number] {
    assertUnshared(text, "absentInNetGames");
    // The node array (NeverScript text) with AbsentInNetGames on every CreatedAtStart node a network game can't
    // have - what the shipped levels do for such objects:
    // - one whose TriggerScript follows a path (itself or through scripts it names): no path manager there;
    // - a pedestrian or vehicle: THUG2 loads no ped animations in network games (skutils.qb do_load_unloadable:
    //   `if inNetGame { load_peds = 0 }`), and a ped's first animation reads through the missing one (0x00482BA0 -
    //   THPS4's skate shop's standing ped crashed a network game). THUG2's own levels keep all of them out.
    // `text`'s own scripts are read, and `scripts` ({checksum: compiled code}, added after compiling:
    // script_port.appendScripts). Returns [text, nodes marked].
    const followers = scriptsUsing(text, scripts, PATH_COMMAND_SET, null);
    let marked = 0;
    const fix = (match: Match): string => {
        const node = match.str(0);
        if (!search(/\bCreatedAtStart\b/, node) || search(/\bAbsentInNetGames\b/, node)) return node;
        const script = search(/\bTriggerScript=(#?\w+)/, node);
        const follows = script !== null && followers.has(nameValue(script.str(1)));
        if (!follows && !search(/\bClass=(Pedestrian|Vehicle)\b/i, node)) return node;
        marked += 1;
        return sub(/\bCreatedAtStart\b/, "CreatedAtStart AbsentInNetGames", node, 1);
    };
    return [sub(ELEMENT, fix, text), marked];
}

export function scriptsUsing(
    text: string,
    scripts: ReadonlyMap<number, Uint8Array> | null,
    commands: ReadonlySet<number>,
    more: ReadonlyMap<number, ReadonlySet<number>> | null,
): Set<number> {
    // The scripts that run one of `commands` (checksums), themselves or through scripts they name (any name a script
    // uses counts: a call, a spawned script, a parameter): `text`'s own scripts (NeverScript), `scripts`
    // ({checksum: compiled code}) and `more` (other scripts: {checksum: the names each uses} - THUG2's own,
    // script_port.Thug2.bodies).
    const calls = new Map<number, Set<number>>(); // script -> the names it uses
    for (const [name, body] of findall(SCRIPT_BLOCK, text, "tuples") as [string, string][]) {
        calls.set(nameValue(name), new Set(findall(/#[0-9A-Fa-f]{8}\b|\b[A-Za-z_]\w*/, body).map(nameValue)));
    }
    for (const [name, code] of scripts ?? new Map<number, Uint8Array>()) {
        const used = new Set<number>();
        for (const [, t, p] of qb.tokens(Buffer.concat([code, Buffer.from([0])]), true)) {
            if (t === 0x00) break;
            if (t === 0x16) used.add(intFromBytesLE(p.subarray(0, 4)));
        }
        calls.set(name, new Set([...(calls.get(name) ?? []), ...used]));
    }
    for (const [name, used] of more ?? new Map<number, ReadonlySet<number>>()) {
        if (!calls.has(name)) calls.set(name, new Set(used));
    }
    const intersects = (a: ReadonlySet<number>, b: ReadonlySet<number>): boolean => [...a].some((x) => b.has(x));
    const followers = new Set<number>();
    for (const [name, used] of calls) if (intersects(used, commands)) followers.add(name);
    let grew = true;
    while (grew) {
        grew = false;
        for (const [name, used] of calls) {
            if (!followers.has(name) && intersects(used, followers)) {
                followers.add(name);
                grew = true;
            }
        }
    }
    return followers;
}

// The model component's light members: each runs on the model's lights (CModel +0x2C, 0x00532EB0) without checking
// they're there (the member function 0x00421AE0: Obj_EnableAmbientLight at 0x00421D68 calls the lights' virtual +0x24
// through 0x0048DE30 - a null read). A model has lights only when its object was made with UseModelLights (the model
// component's init 0x004214F0 makes them, or takes them away without the flag). Obj_HasModelLights checks; not listed.
export const MODEL_LIGHT_COMMANDS = [
    "Obj_EnableAmbientLight",
    "Obj_DisableAmbientLight",
    "Obj_EnableDiffuseLight",
    "Obj_DisableDiffuseLight",
    "Obj_SetLightAmbientColor",
    "Obj_SetLightDiffuseColor",
    "Obj_SetLightDirection",
];
const MODEL_LIGHT_SET: ReadonlySet<number> = new Set(MODEL_LIGHT_COMMANDS.map(checksum));

// The node array (NeverScript text) with `flag` after the Class of every node of `classes` (lower case; null: any)
// whose TriggerScript is one of `users` and that hasn't the flag yet. Returns [text, nodes marked].
function flagNodes(
    text: string,
    users: ReadonlySet<number>,
    flag: string,
    classes: ReadonlySet<string> | null,
): [string, number] {
    const has = new RegExp(`\\b${flag}\\b`, "i");
    let marked = 0;
    const fix = (match: Match): string => {
        const node = match.str(0);
        const script = search(/\bTriggerScript=(#?\w+)/, node);
        if (script === null || !users.has(nameValue(script.str(1))) || search(has, node)) return node;
        const cls = search(/\bClass=(\w+)/, node);
        if (cls === null || (classes !== null && !classes.has(cls.str(1).toLowerCase()))) return node;
        marked += 1;
        return `${node.slice(0, cls.end())} ${flag}${node.slice(cls.end())}`;
    };
    return [sub(ELEMENT, fix, text), marked];
}

export function useModelLights(
    text: string,
    scripts: ReadonlyMap<number, Uint8Array> | null = null,
    more: ReadonlyMap<number, ReadonlySet<number>> | null = null,
): [string, number] {
    // The node array (NeverScript text) with UseModelLights on every node whose TriggerScript sets its model's lights
    // (MODEL_LIGHT_COMMANDS, itself or through scripts it names: scriptsUsing) - as THUG1's and THUG2's own objects
    // that do carry it. Without it the first such command reads through the missing lights (THPS4 Carnival's rides,
    // Mdl_NoDynLighting: a crash at 0x0048DE30 when the level loads). Returns [text, nodes marked].
    return flagNodes(text, scriptsUsing(text, scripts, MODEL_LIGHT_SET, more), "UseModelLights", null);
}

// The stream component's members (thug2_members.ts): a table's - an object without the component crashes on them
// (Script::CallMemberFunction 0x00472980 runs the entry on its null component).
const STREAM_MEMBER_SET: ReadonlySet<number> = new Set(
    TABLE_MEMBERS.filter(([, component]) => component === "stream").map(([name]) => name),
);
// The classes gameobj_add_components (THUG2's ped.qb) makes: a stream component only with SupportStreams. Peds and
// cars always have one (AddMotionComponents 0x0057B2F0).
const GAME_OBJECT_CLASSES: ReadonlySet<string> = new Set(["levelobject", "gameobject"]);

export function supportStreams(
    text: string,
    scripts: ReadonlyMap<number, Uint8Array> | null = null,
    more: ReadonlyMap<number, ReadonlySet<number>> | null = null,
): [string, number] {
    // The node array (NeverScript text) with SupportStreams on every LevelObject / GameObject whose TriggerScript plays
    // or stops a stream on it (the stream members, itself or through the scripts it names - `more`: THUG2's own,
    // whose SpawnSound runs a script on the same object). THPS4's trams run THUG2's Car_Generic01, whose tire squeal
    // (SpawnSound Auto_SFX_Car_Tire_Squeal) plays a stream: on a LevelObject without the component, a crash when the
    // car brakes for the skater (Alcatraz, San Francisco). Returns [text, nodes marked].
    return flagNodes(text, scriptsUsing(text, scripts, STREAM_MEMBER_SET, more), "SupportStreams", GAME_OBJECT_CLASSES);
}

// Python's re.split(r"[\s,]+", text.strip()) without the empty parts.
function splitTypes(text: string): string[] {
    return text
        .trim()
        .split(/[\s,]+/)
        .filter((t) => t);
}

export function netRestarts(text: string): [string, number] {
    assertUnshared(text, "netRestarts");
    // A level with no Multiplayer restart (THPS4's skate shop and MotoX, THPS3's tutorials, THUG1's Downhill Jam,
    // Philly and School: single-player levels in their games) has nowhere for a network game to put the players: its
    // Player1 restarts become Multiplayer and Horse ones too, as THUG2's own skate shop's are. Returns [text, changed].
    if (search(/\brestart_types=\[[^\]]*\bMultiplayer\b/i, text)) return [text, 0];
    let changed = 0;
    const fix = (match: Match): string => {
        const node = match.str(0);
        const types = search(/\brestart_types=\[([^\]]*)\]/, node);
        if (!search(/\bClass=Restart\b/i, node) || !types || !search(/\bPlayer1\b/i, types.str(1))) return node;
        changed += 1;
        const listed = splitTypes(types.str(1));
        const lower = new Set(listed.map((x) => x.toLowerCase()));
        for (const t of ["Multiplayer", "Horse"]) if (!lower.has(t.toLowerCase())) listed.push(t);
        return `${node.slice(0, types.start())}restart_types=[${listed.join(" ")}]${node.slice(types.end())}`;
    };
    return [sub(ELEMENT, fix, text), changed];
}

export function netGameNodes(
    text: string,
    scripts: ReadonlyMap<number, Uint8Array> | null = null,
    withRestarts = true,
): [string, number] {
    // What a network game needs of a ported level's node array: absentInNetGames, then netRestarts (withRestarts:
    // false leaves those out - tools/thps3_levels' PC-4 under --no-port-changes). Returns [text, nodes changed].
    const [absentText, absent] = absentInNetGames(text, scripts);
    const [restartText, restarts] = withRestarts ? netRestarts(absentText) : [absentText, 0];
    return [restartText, absent + restarts];
}

// --- restarts against the converted collision -----------------------------------------------------------------------

export const NET_RESTART_TYPES: ReadonlySet<string> = new Set([
    "multiplayer",
    "horse",
    "ctf",
    "ctf_1",
    "ctf_2",
    "team",
]);

function ground(col: thug2Col.ColFile, p: readonly number[]): [number, number, number] | null {
    // The highest collidable face below (or 30 units above) the point p, as [height, object checksum, face flags],
    // or null: nothing under it.
    let best: [number, number, number] | null = null;
    const [px, py, pz] = [p[0]!, p[1]!, p[2]!];
    for (const o of col.objects) {
        const box = o.bbox;
        if (!(box[0]! - 1 <= px && px <= box[4]! + 1 && box[2]! - 1 <= pz && pz <= box[6]! + 1)) continue;
        const vs = o.verts;
        for (const f of o.faces) {
            if (f[0] & 0x0010) continue; // non-collidable
            const a = vs[f[2]]!;
            const b = vs[f[3]]!;
            const c = vs[f[4]]!;
            const den = (b[2]! - c[2]!) * (a[0]! - c[0]!) + (c[0]! - b[0]!) * (a[2]! - c[2]!);
            if (Math.abs(den) < 1e-9) continue;
            const l1 = ((b[2]! - c[2]!) * (px - c[0]!) + (c[0]! - b[0]!) * (pz - c[2]!)) / den;
            const l2 = ((c[2]! - a[2]!) * (px - c[0]!) + (a[0]! - c[0]!) * (pz - c[2]!)) / den;
            if (l1 < 0 || l2 < 0 || l1 + l2 > 1) continue;
            const y = l1 * a[1]! + l2 * b[1]! + (1 - l1 - l2) * c[1]!;
            if (y <= py + 30 && (best === null || y > best[0])) best = [y, o.checksum, f[0]];
        }
    }
    return best;
}

function nodePos(node: string): number[] | null {
    // A node's position in THUG2's space: Pos= as it is, Position= (THPS4 / THUG1 style) with z negated.
    let m = search(/\bPos=\(([^)]*)\)/, node);
    if (m) return m.str(1).split(",").map(pyParseFloat);
    m = search(/\bPosition=\(([^)]*)\)/, node);
    if (m) {
        const values = m.str(1).split(",").map(pyParseFloat);
        if (values.length !== 3) throw new RangeError(`not enough values to unpack (expected 3, got ${values.length})`);
        const [x, y, z] = values as [number, number, number];
        return [x, y, -z];
    }
    return null;
}

export function groundRestarts(text: string, col: thug2Col.ColFile): [string, string[]] {
    assertUnshared(text, "groundRestarts");
    // The node array (NeverScript text) checked against the converted collision (thug2_col's form): a restart with
    // nothing under it (THAW's shop interiors - separate zones we don't port - are at y -600 over nothing) or over a
    // kill zone's trigger faces (East L.A.'s TransitionParkTeleport restarts, a network game's spawns, above the
    // freeway kill that took THAW to the CAS shop) is no network spawn (its Multiplayer / Horse / CTF / Team types
    // go), and the kills sending the skater to one send it to the nearest grounded restart instead (East L.A.'s
    // freeway sent it to the CAS shop's interior: under the world for good). Returns [text, report lines].
    const scripts = new Map<string, string>();
    for (const [name, body] of findall(SCRIPT_BLOCK, text, "tuples") as [string, string][]) scripts.set(name, body);
    const elements = findall(ELEMENT, text);
    const killNodes = new Map<number, string>();
    for (const e of elements) {
        const script = search(/\bTriggerScript=(#?\w+)/, e);
        const name = search(/\bName=(#?\w+)/, e);
        if (script && name && search(/\bSK3_KillSkater/i, scripts.get(script.str(1)) ?? "")) {
            killNodes.set(nameValue(name.str(1)), script.str(1));
        }
    }
    const restarts = new Map<string, number[]>();
    const bad = new Set<string>();
    for (const e of elements) {
        if (!search(/\bClass=Restart\b/i, e)) continue;
        const name = search(/\bName=(#?\w+)/, e);
        const p = nodePos(e);
        if (!name || !p?.length) continue;
        restarts.set(name.str(1), p);
        const g = ground(col, p);
        if (g === null || (killNodes.has(g[1]) && g[2] & 0x0040)) bad.add(name.str(1));
    }
    const report: string[] = [];
    if (!bad.size) return [text, report];

    const fixNode = (match: Match): string => {
        const node = match.str(0);
        const name = search(/\bName=(#?\w+)/, node);
        const types = search(/\brestart_types=\[([^\]]*)\]/, node);
        if (!name || !bad.has(name.str(1)) || !types) return node;
        const listed = splitTypes(types.str(1));
        let kept = listed.filter((t) => !NET_RESTART_TYPES.has(t.toLowerCase()));
        if (!kept.length) kept = ["Generic"];
        if (kept.length !== listed.length || kept.some((t, i) => t !== listed[i])) {
            report.push(`  restart ${name.str(1)}: no ground or over a kill zone - no network spawn`);
        }
        return `${node.slice(0, types.start())}restart_types=[${kept.join(" ")}]${node.slice(types.end())}`;
    };

    text = sub(ELEMENT, fixNode, text);
    const good = new Map([...restarts].filter(([n]) => !bad.has(n)));
    if (!good.size) return [text, report];
    // Each kill script's trigger node positions (the nearest good restart to the first).
    const where = new Map<string, number[]>();
    for (const e of elements) {
        const script = search(/\bTriggerScript=(#?\w+)/, e);
        const p = nodePos(e);
        if (script && p?.length && !where.has(script.str(1))) where.set(script.str(1), p);
    }

    const fixScript = (match: Match): string => {
        const name = match.str(1);
        let body = match.str(2);
        const target = search(/\b(SK3_KillSkater\w*\s+nodename=)(#?\w+)/i, body);
        if (!target || !bad.has(target.str(2))) return match.str(0);
        const whereP = where.get(name);
        const p = whereP?.length ? whereP : restarts.get(target.str(2))!;
        const best = pyMinBy(good.keys(), (n) => distance2(good.get(n)!, p));
        report.push(`  kill ${name}: to ${best}, not ${target.str(2)} (no ground there)`);
        body = body.slice(0, target.start(2)) + best + body.slice(target.end(2));
        const whole = match.str(0);
        return whole.slice(0, match.start(2) - match.start(0)) + body + whole.slice(match.end(2) - match.start(0));
    };

    text = sub(/(?:(?<=^)|(?<=\n))script (#?\w+) \{(.*?)\n\}/s, fixScript, text);
    return [text, report];
}

export function groundRestartsAt(text: string, levelDir: string, name: string): [string, string[]] {
    // groundRestarts against the collision just written (<levelDir>\<name>.col.xbx). Returns [text, report lines].
    const file = path.join(levelDir, `${name}.col.xbx`);
    if (!fs.existsSync(file)) return [text, []];
    return groundRestarts(text, thug2Col.readCol(fs.readFileSync(file), file));
}
