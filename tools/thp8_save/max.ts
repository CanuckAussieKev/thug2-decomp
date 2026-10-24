// "max": a THP8 save as a finished career - every goal beaten (SICK where the goal is graded), every story atom done
// (and what the atoms' scripts would have set: node flags, global flags), every unlock flag the game's data names
// set, gaps got, secret spots found, the shop's items bought, the stats' records past their last threshold, stokens
// and goal points up. Everything is derived from THP8's own data (game_data.ts); docs/pending/thp8-save.md lists what
// is set and why.

import { checksum, Name, type QbValue, Struct } from "../level_port/qb.ts";
import * as thaw from "../thaw_levels/thaw.ts";
import type { GameData } from "./game_data.ts";
import { type Component, type SaveFile, type SaveStruct, T_FLOAT, T_NAME, type Value } from "./save.ts";

// --- save access ------------------------------------------------------------------------------------------------

export function child(s: SaveStruct, name: string): Component | undefined {
    const k = checksum(name);
    return s.components.find((c) => c.name === k);
}

export function at(s: SaveStruct, path: string): Value {
    let cur: Value = { kind: "struct", value: s };
    for (const part of path.split(".")) {
        if (cur.kind !== "struct") throw new Error(`${path}: ${part} is not in a structure`);
        const c = child(cur.value, part);
        if (!c) throw new Error(`${path}: no ${part}`);
        cur = c.value;
    }
    return cur;
}

export function structAt(s: SaveStruct, path: string): SaveStruct {
    const v = at(s, path);
    if (v.kind !== "struct") throw new Error(`${path}: not a structure`);
    return v.value;
}

export function arrayAt(s: SaveStruct, path: string): Value[] {
    const v = at(s, path);
    if (v.kind !== "array") throw new Error(`${path}: not an array`);
    return v.value;
}

function intOf(s: SaveStruct, name: string): number | undefined {
    const c = child(s, name);
    return c?.value.kind === "int" ? c.value.value : undefined;
}

// Sets an existing integer / name / float component (the form it was read in kept as far as the value allows); returns
// whether it changed.
function setInt(s: SaveStruct, name: string, value: number): boolean {
    const c = child(s, name);
    if (!c) throw new Error(`no ${name}`);
    if (c.value.kind !== "int") throw new Error(`${name}: not an integer`);
    if (c.value.value === value) return false;
    c.value = { kind: "int", value };
    return true;
}

function setFloat(s: SaveStruct, name: string, value: number): boolean {
    const c = child(s, name);
    if (!c) throw new Error(`no ${name}`);
    if (c.value.kind !== "float") throw new Error(`${name}: not a float`);
    if (c.value.value === value) return false;
    c.value = { kind: "float", value };
    if ((c.type & 0x3f) !== T_FLOAT) c.type = (c.type & 0xc0) | T_FLOAT;
    return true;
}

function setName(s: SaveStruct, name: string, value: number): boolean {
    const c = child(s, name);
    if (!c) throw new Error(`no ${name}`);
    if (c.value.kind !== "name") throw new Error(`${name}: not a name`);
    if (c.value.value === value) return false;
    c.value = { kind: "name", value };
    return true;
}

// --- game data ----------------------------------------------------------------------------------------------------

function nameOf(v: QbValue | undefined): number | null {
    if (v instanceof Name) return v.value;
    const u: unknown = v;
    if (u instanceof thaw.Ref) return u.value;
    return null;
}

function structOf(v: QbValue | undefined): Struct {
    if (!(v instanceof Struct)) throw new Error("not a structure");
    return v;
}

// A global integer constant (a flag number) by its checksum.
function constant(game: GameData, key: number): number | null {
    const v = game.globals.get(key);
    return typeof v === "number" ? v : null;
}

function walk(v: QbValue, visit: (key: number | null, value: QbValue) => void): void {
    if (v instanceof Struct) {
        for (const [k, x] of v.entries) {
            visit(k?.value ?? null, x);
            walk(x, visit);
        }
    } else if (Array.isArray(v)) {
        for (const x of v) {
            visit(null, x);
            walk(x, visit);
        }
    }
}

// The global flags that unlock things - not the cheats that change the game, nor the debug menu.
const UNLOCK_PATTERN =
    /^(SKATER_UNLOCKED_|CAS_UNLOCK_\d|CAS_[A-Z]+_UNLOCKED$|MOVIE_UNLOCKED_|UNLOCKED_MOVIE_|CHEAT_UNLOCKED_\d|SECRET_UNLOCKED_|VIEWED_CUTSCENE_)/;
const COMPLETION_FLAGS = [
    "CAREER_STARTED",
    "CAREER_GOALS_COMPLETE",
    "GAME_COMPLETED_BEGINNER",
    "GAME_COMPLETED_NORMAL",
    "GAME_COMPLETED_SICK",
    "GOT_ALL_GAPS",
    "GOT_ALL_GOALS",
    // (the "you've finished" / tutorial pop-ups: seen)
    "CAREER_SEEN_GAME_COMPLETE_MESSAGE",
    "CAREER_SEEN_GOAL_COMPLETE_MESSAGE",
    "CAREER_SEEN_PIECE_GOAL_COMPLETE_MESSAGE",
    "GOALS_EXPLAINED",
    "TAP_BUTTONS_EXPLAINED",
    "RECOVERY_BUTTONS_EXPLAINED",
    "RANK_UP_EXPLAINED",
    "STAT_UP_EXPLAINED",
    "SWITCH_TO_SKATING_BUTTONS_EXPLAINED",
    "PRO_TRICKS_MENU_EXPLAINED",
    "TUTORIAL_EXPLAINED",
    "FLAG_SHOWN_VIDEOPHONE",
];
const NEVER =
    /^(cheat_on_|CHEAT_(FULL|ALWAYS|SLOWMO|MASCOT|UNLOCK_AND_OWN|UNLOCK_ALL|DEBUG|PERFECT)|HAS_ANTILEADERBOARD|CHEAT_CAS_VANS)/i;

export interface FlagSet {
    flags: Map<number, string>; // flag number -> name
}

// Every unlock flag of the data: the CAS parts' / skaters' unlock_flag / unlocked_flag fields, the movie lists' flags,
// the story atoms' unlock_skater / unlock_pro_movie flags, the flag constants by category.
export function unlockFlags(game: GameData): Map<number, string> {
    const out = new Map<number, string>();
    const add = (key: number): void => {
        const n = constant(game, key);
        const text = game.names.get(key) ?? `#${key.toString(16)}`;
        if (n === null || NEVER.test(text)) return;
        out.set(n, text);
    };
    const fieldKeys = new Set([checksum("unlock_flag"), checksum("unlocked_flag")]);
    for (const globals of [game.globals, game.casGlobals]) {
        for (const v of globals.values()) {
            walk(v, (k, x) => {
                if (k !== null && fieldKeys.has(k)) {
                    const n = nameOf(x);
                    if (n !== null) add(n);
                }
            });
        }
    }
    for (const list of [
        "ui_movies_profootage_list",
        "ui_movies_dayinthelife_list",
        "ui_movies_bonusfootage_list",
        "ui_movies_bailfootage_list",
        "ui_movies_sponsormovies_list",
    ]) {
        const v = game.globals.get(checksum(list));
        if (!Array.isArray(v)) continue;
        for (const e of v) if (e instanceof Struct) add(nameOf(e.get("flag")) ?? 0);
    }
    for (const atom of atoms(game)) {
        const params = atom.get("atom_params");
        const flags = params instanceof Struct ? params.get("flags") : undefined;
        if (Array.isArray(flags)) for (const f of flags) add(nameOf(f) ?? 0);
    }
    for (const [key, v] of game.globals) {
        if (typeof v !== "number") continue;
        const text = game.names.get(key);
        if (text !== undefined && UNLOCK_PATTERN.test(text)) add(key);
    }
    for (const text of COMPLETION_FLAGS) add(checksum(text));
    return out;
}

function atoms(game: GameData): Struct[] {
    const v = game.globals.get(checksum("chapter_info_data_internal"));
    if (!Array.isArray(v)) throw new Error("qb.pak.xen: no chapter_info_data_internal");
    return v.map(structOf);
}

// The ChangeNodeFlag calls of a script ([flag, on]): `ChangeNodeFlag {flag 1}` or `ChangeNodeFlag flag 1` (the atoms'
// unlock scripts change their flags directly - none calls another script that does).
export function scriptNodeFlags(game: GameData, script: number): [number, boolean][] {
    const body = game.scripts.get(script);
    if (!body) return [];
    const out: [number, boolean][] = [];
    const change = checksum("ChangeNodeFlag");
    for (const line of thaw.statements(body)) {
        const [first, second, third] = line;
        if (first?.[0] !== 0x16 || first[1].readUInt32LE(0) !== change || !second) continue;
        if (second[0] === 0x4a) {
            const s = thaw.inlineStruct(second[1], game.names, ">");
            const flag = s.entries.find(([k, v]) => k === null && v instanceof Name)?.[1];
            const on = s.entries.find(([k, v]) => k === null && typeof v === "number")?.[1];
            if (flag instanceof Name && typeof on === "number") out.push([flag.value, on !== 0]);
        } else if (second[0] === 0x16 && third && third[0] === 0x17) {
            out.push([second[1].readUInt32LE(0), third[1].readInt32LE(0) !== 0]);
        }
    }
    return out;
}

// The node flags the free-roam (non-story) modes set: every zone's barrier opened. As game_progress's
// set_non_story_mode_nodeflags, without its game-mode flags (story / multiplayer / permanent peds).
const MODE_FLAGS = new Set(["nodeflag_gamemode_multiplayer", "nodeflag_gamemode_story", "nodeflag_permanent_peds"]);

export function openWorldNodeFlags(game: GameData): [number, boolean][] {
    const body = game.scripts.get(checksum("game_progress_set_non_story_mode_nodeflags"));
    if (!body) throw new Error("qb.pak.xen: no game_progress_set_non_story_mode_nodeflags");
    // (`flags = [ {flag 0} ... ]`: the array is the line's 0x4A-less tokens - read them as name / integer pairs)
    const out: [number, boolean][] = [];
    let pending: number | null = null;
    for (const [, t, p] of thaw.tokens(body)) {
        if (t === 0x16) pending = p.readUInt32LE(0);
        else if (t === 0x17 && pending !== null) {
            const text = (game.names.get(pending) ?? "").toLowerCase();
            if (text.startsWith("nodeflag_") && !MODE_FLAGS.has(text)) out.push([pending, p.readInt32LE(0) !== 0]);
            pending = null;
        } else if (t !== 0x03 && t !== 0x04) pending = null;
    }
    return out;
}

// --- maxing -------------------------------------------------------------------------------------------------------

// Where max may change the save (thp8_save.ts's diffSaves paths): anything else changing is a bug - `max` refuses to
// write then.
export const MAX_CHANGES: readonly RegExp[] = [
    /^summary\.careermissionscomplete:/,
    /^main\.globalinfo\.career\.globalflags[:[]/,
    /^main\.globalinfo\.career\.nodeflags\.(On|Off):/,
    /^main\.globalinfo\.career\.story_atom_group\.atom_param_list\[\d+\]\.(state|sentorder|viewed|cleared):/,
    /^main\.globalinfo\.career\.story_atom_group\.referenced_goals\.(locked|unlocked|won):/,
    /^main\.globalinfo\.career\.gapchecklist\.gaps\[\d+\]\.gapcount:/,
    /^main\.globalinfo\.career\.(goalpoints|stokens|secret_spots_found|prosbeaten|beat_a_goal_sick|firststatchallengecomplete|careermissionscomplete|storystatscompleted):/,
    /^main\.globalinfo\.career\.classicstats\.\w+:/,
    /^main\.globalinfo\.training(\[\d+\])?:/,
    /^main\.globalinfo\.shop_(specials|decks|mocap)_bought\.\w+:/,
    /^main\.story\.goalmanagerparams\.\w+\.(islocked|hasseen|hasbeaten|grade_achieved|timeswon|timesstarted):/,
    /^main\.storyskater\.records\.\w+\.(total|curr_stat_level):/,
    /^main\.customskater\.custom_story\.info\.(air|lip_balance|rail_balance|manual_balance|wall|special|spin):/,
];

// A global flag set in the career's globalflags words: flag n is bit n & 31 of word n >> 5 (the save's flags agree:
// CAREER_STARTED 115, RANK_UP_EXPLAINED 119, MOVIE_UNLOCKED_DEFAULT 139...). Returns whether it was clear.
export function setFlag(words: Value[], n: number): boolean {
    const e = words[n >> 5];
    if (e === undefined) throw new Error(`flag ${n} past the ${words.length} words`);
    if (e.kind !== "name" && e.kind !== "int") throw new Error("globalflags: not a word array");
    const value = (e.value | (1 << (n & 31))) >>> 0;
    if (value === e.value >>> 0) return false;
    e.value = e.kind === "int" ? value | 0 : value;
    return true;
}

export function getFlag(words: Value[], n: number): boolean {
    const e = words[n >> 5];
    return (e?.kind === "name" || e?.kind === "int") && ((e.value >>> (n & 31)) & 1) === 1;
}

const GRADE_SICK = 3;
const MAX_STOKENS = 9999;
// (story stats: 3 + the record's level, the level the number of stat_levels_<record> thresholds passed)
const STAT_RECORDS: [string, string][] = [
    ["air", "air"],
    ["lip", "lip_balance"],
    ["Grind", "rail_balance"],
    ["Manual", "manual_balance"],
    ["wallride", "wall"],
    ["specials", "special"],
    ["spin", "spin"],
];
const STAT_BASE = 3;

export function maxSave(save: SaveFile, game: GameData): string[] {
    const report: string[] = [];
    const say = (what: string, n: number): void => {
        report.push(`${what}: ${n}`);
    };
    const main = save.main;
    const career = structAt(main, "globalinfo.career");
    const nm = (k: number): string => game.names.get(k) ?? `#${k.toString(16)}`;

    // goals
    const goalInfo = game.globals.get(checksum("story_mode_goal_info"));
    // (graded: the view-goals screen has its AM / PRO / SICK texts - story_mode_goal_info -, or its type's leaderboard
    // ranks by grade - goal_rewards: classic and comp goals have no texts of their own)
    const rewards = game.globals.get(checksum("goal_rewards"));
    const gradeMetric = checksum("grade");
    const rankedByGrade = (type: number | undefined): boolean => {
        const r = rewards instanceof Struct && type !== undefined ? rewards.get(type) : undefined;
        const board = r instanceof Struct ? r.get("leaderboard") : undefined;
        return (
            Array.isArray(board) && board.some((m) => m instanceof Struct && nameOf(m.get("metric")) === gradeMetric)
        );
    };
    const graded = (goal: number): boolean => {
        const info = goalInfo instanceof Struct ? goalInfo.get(goal) : undefined;
        if (info instanceof Struct && info.get("view_goals_text_sick") !== undefined) return true;
        return rankedByGrade(game.goalTypes.get(goal));
    };
    const goals = structAt(main, "story.goalmanagerparams");
    let goalsChanged = 0;
    let gradedCount = 0;
    const goalNames: number[] = [];
    for (const g of goals.components) {
        if (g.value.kind !== "struct" || !child(g.value.value, "hasbeaten")) continue;
        const s = g.value.value;
        goalNames.push(g.name);
        let changed = setInt(s, "islocked", 0);
        changed = setInt(s, "hasseen", 1) || changed;
        changed = setInt(s, "hasbeaten", 1) || changed;
        if (graded(g.name)) {
            gradedCount++;
            changed = setInt(s, "grade_achieved", GRADE_SICK) || changed;
        }
        const won = Math.max(1, intOf(s, "timeswon") ?? 0);
        changed = setInt(s, "timeswon", won) || changed;
        const started = Math.max(intOf(s, "timesstarted") ?? 0, won + (intOf(s, "timeslost") ?? 0));
        changed = setInt(s, "timesstarted", started) || changed;
        if (changed) goalsChanged++;
    }
    say(`goals beaten (${goalNames.length}, ${gradedCount} graded at SICK) - changed`, goalsChanged);

    // story atoms
    const defs = atoms(game);
    const list = arrayAt(career, "story_atom_group.atom_param_list");
    if (list.length !== defs.length)
        throw new Error(`${list.length} atoms in the save, ${defs.length} in chapter_info`);
    const complete = checksum("complete");
    const cleared = new Set<number>();
    for (const d of defs) {
        if (nameOf(d.get("atom_script")) === checksum("game_progress_clear_pager_message")) {
            const target = nameOf((d.get("atom_params") as Struct | undefined)?.get("atom"));
            if (target !== null) cleared.add(target);
        }
    }
    let atomsChanged = 0;
    let order = 0;
    for (const v of list) {
        if (v.kind !== "struct") continue;
        const sent = intOf(v.value, "sentorder");
        if (sent !== undefined && sent >= order) order = sent + 1;
    }
    const nodeFlags: [number, boolean][] = [];
    defs.forEach((d, i) => {
        const v = list[i]!;
        if (v.kind !== "struct") throw new Error(`atom ${i}: not a structure`);
        const wasComplete = child(v.value, "state")?.value.kind === "name" && at(v.value, "state").value === complete;
        let changed = setName(v.value, "state", complete);
        if (child(v.value, "sentorder")) {
            if ((intOf(v.value, "sentorder") ?? -1) < 0) changed = setInt(v.value, "sentorder", order++) || changed;
            changed = setName(v.value, "viewed", checksum("true")) || changed;
            const name = nameOf(d.get("Name"));
            if (name !== null && cleared.has(name)) changed = setName(v.value, "cleared", checksum("true")) || changed;
        }
        if (changed) atomsChanged++;
        // (what the atoms the career already did set is in the save already)
        const script = nameOf(d.get("atom_script"));
        if (script !== null && !wasComplete) nodeFlags.push(...scriptNodeFlags(game, script));
    });
    say(`story atoms complete (${defs.length}) - changed`, atomsChanged);
    const referenced = structAt(career, "story_atom_group.referenced_goals");
    const all: Value[] = [];
    for (const key of ["locked", "unlocked", "won"]) {
        const c = child(referenced, key);
        if (c?.value.kind !== "array") throw new Error(`referenced_goals.${key}: not an array`);
        all.push(...c.value.value);
        // (an empty array is written with element type 0, as the game writes its own empty ones - `sponsors`)
        if (key !== "won") c.value = { kind: "array", elementType: 0, value: [] };
        else c.value = { kind: "array", elementType: T_NAME, value: all };
    }
    say("referenced goals now all won", all.length);

    // node flags: the atoms' scripts' changes, then every barrier open. Only the flags the save tracks (in On or Off):
    // the others name no node of the world (ChangeNodeFlag of them does nothing and the game doesn't save them).
    nodeFlags.push(...openWorldNodeFlags(game));
    const on = arrayAt(career, "nodeflags.On");
    const off = arrayAt(career, "nodeflags.Off");
    let flagsMoved = 0;
    for (const [flag, value] of nodeFlags) {
        const [from, to] = value ? [off, on] : [on, off];
        const k = from.findIndex((e) => e.kind === "name" && e.value === flag);
        if (k < 0) continue;
        from.splice(k, 1);
        to.push({ kind: "name", value: flag });
        flagsMoved++;
        report.push(`  node flag ${nm(flag)} ${value ? "on" : "off"}`);
    }
    say("node flags changed", flagsMoved);

    // global flags
    const words = arrayAt(career, "globalflags");
    const unlocks = unlockFlags(game);
    let setFlags = 0;
    for (const [n, text] of [...unlocks].sort((a, b) => a[0] - b[0])) {
        if (n >> 5 >= words.length) throw new Error(`flag ${text} (${n}) past the ${words.length} words`);
        if (setFlag(words, n)) setFlags++;
    }
    say(`global unlock flags (${unlocks.size}) newly set`, setFlags);

    // gaps
    const gaps = arrayAt(career, "gapchecklist.gaps");
    let gapsGot = 0;
    for (const g of gaps) {
        if (g.kind !== "struct") continue;
        if ((intOf(g.value, "gapcount") ?? 0) < 1 && child(g.value, "gapcount")) {
            setInt(g.value, "gapcount", 1);
            gapsGot++;
        }
    }
    say(`gaps got (${gaps.length})`, gapsGot);

    // career counters
    const spots = game.globals.get(checksum("global_secret_spots"));
    const spotCount = spots instanceof Struct ? spots.entries.length : 0;
    const goalTypes = game.goalTypes;
    let points = 0;
    for (const g of goalNames) {
        const type = goalTypes.get(g);
        const r = rewards instanceof Struct && type !== undefined ? rewards.get(type) : undefined;
        const p = r instanceof Struct ? r.get("goal_points") : undefined;
        if (typeof p === "number") points += p;
    }
    points += gaps.length + 5 * spotCount;
    const pros = goalNames.filter((g) => nm(goalTypes.get(g) ?? 0) === "pro_challenge").length;
    const missions = intOf(career, "careermissionstotal") ?? 0;
    const counters: [string, number][] = [
        ["goalpoints", Math.max(points, intOf(career, "goalpoints") ?? 0)],
        ["stokens", Math.max(MAX_STOKENS, intOf(career, "stokens") ?? 0)],
        ["secret_spots_found", spotCount],
        ["prosbeaten", pros],
        ["beat_a_goal_sick", 1],
        ["firststatchallengecomplete", 1],
        ["careermissionscomplete", missions],
        ["storystatscompleted", STAT_RECORDS.length],
    ];
    for (const [k, v] of counters) if (setInt(career, k, v)) report.push(`career.${k} = ${v}`);
    if (child(save.summary, "careermissionscomplete") && setInt(save.summary, "careermissionscomplete", missions))
        report.push(`summary.careermissionscomplete = ${missions}`);
    const training = child(structAt(main, "globalinfo"), "training");
    if (training?.value.kind === "array") {
        for (const e of training.value.value) if (e.kind === "int") e.value = 1;
        report.push(`training lessons done: ${training.value.value.length}`);
    }

    // shop
    for (const shop of ["shop_specials_bought", "shop_decks_bought", "shop_mocap_bought"]) {
        const s = structAt(main, `globalinfo.${shop}`);
        let n = 0;
        for (const c of s.components) {
            if (c.value.kind === "int" && c.value.value !== 1) {
                c.value = { kind: "int", value: 1 };
                n++;
            }
        }
        say(`${shop} (${s.components.length}) newly bought`, n);
    }

    // stats: each record past its last threshold, the story skater's stat at 3 + levels
    const records = structAt(main, "storyskater.records");
    const story = structAt(main, "customskater.custom_story.info");
    for (const [record, stat] of STAT_RECORDS) {
        const levels = game.globals.get(checksum(`stat_levels_${record.toLowerCase()}`));
        if (!Array.isArray(levels) || levels.length === 0) throw new Error(`qb.pak.xen: no stat_levels_${record}`);
        const top = Math.max(...levels.map((x) => (typeof x === "number" ? x : (x as { value: number }).value)));
        const r = structAt(records, record);
        const total = child(r, "total");
        if (total?.value.kind === "float" && total.value.value < top) setFloat(r, "total", top);
        setInt(r, "curr_stat_level", levels.length);
        setInt(story, stat, STAT_BASE + levels.length);
    }
    report.push(`story stats maxed: ${STAT_RECORDS.map(([, s]) => `${s}=${STAT_BASE + 7}`).join(" ")}`);
    const classic = structAt(career, "classicstats");
    for (const c of classic.components) {
        if (c.value.kind === "int" && c.name !== checksum("points_available")) c.value = { kind: "int", value: 10 };
    }
    report.push("classic stats: 10");
    return report;
}
