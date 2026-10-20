// NOT ORIGINAL - GAME OPTIONS' "CREATE 100% CAS" for make_menu_level.ts's data overlay (the user's request): a Story
// Mode and Classic Mode career as a player who finished both completely (every goal and gap, everything they
// unlock) would have it, and the created skater's stats maxed.
//
// What the option does (the script cas100_apply, after a Yes in a warning dialog), with the game's own scripts and
// commands wherever there are any:
//   - reset_gamemode in story mode (as START NEW STORY MODE: chapter 0, chapter status, every profile's stats and
//     tricks back to default, CareerReset / GoalManager_ResetCareer career, the story flags cleared), difficulty
//     Normal (GoalManager_SetDifficultyLevel 1, skater:StatsManager_ReInit, as set_gamemode_difficulty);
//   - every chapter of CHAPTER_INFO complete with its intro and outro seen, its cutscenes' VIEWED_CUTSCENE flags, its
//     level unlocked and seen in the level select (as UnlockNextChapter); the current chapter the one the game goes
//     back to after the last (GoToNextChapterPostCutscene's SetCurrentChapter);
//   - what the levels' stories give on the way (from the level scripts: the GUEST_FOUND_ / PRO_FOUND_ /
//     SKATER_UNLOCKED_ / TRICK_UNLOCKED_ flags they set, the special tricks they award to all profiles);
//   - the story's completion rewards on all three difficulties (story_game_complete_rewards: GAME_COMPLETED_*,
//     the PRO SKATER level, the WDT movie, the secret skaters) and for all goals (story_goal_complete_rewards),
//     through SetRewardFlagArray, with CAREER_SEEN_GAME_COMPLETE_MESSAGE / _GOAL_ so they aren't announced again;
//   - every story goal of every level beaten and the story points full: Career100_Apply (ours, C++ -
//     src/game/front/career_complete.h; no script command sets those) with, per level, its number, the reward points
//     of its story goals, its points potential (the last level_total_points its chapters set) and its story goals'
//     names (worked out here as GoalManager_CreateGoalName makes them: "<level prefix>_goal_<goal type><version>");
//   - the created skater (profile `custom`) with every stat (stat_names) at 10 (SetSkaterProfileInfoByName);
//   - every level the classic mode's progress opens (cas100_classic_levels, as CheckClassicAdvancementStatus does when
//     each bracket's goals are done: every bracket's flag and its levels' LEVEL_SEEN_ flags, and the last bracket's
//     THE TRIANGLE - GAME_CLASSIC_UNLOCKED_SE2, LEVEL_SEEN_SE2, SetClassicTotalGoals 140). Outside story and classic
//     mode (free skate, 2 player...) the level select goes by the LEVEL_SEEN_ flags (level_select_should_use_seen_flags):
//     the story only gives the 7 story levels and PRO SKATER; SCHOOL, PHILADELPHIA, DOWNHILL JAM, LOS ANGELES, CANADA,
//     AIRPORT and THE TRIANGLE only come from classic mode. So all 16 THUG2 levels are open there.
//   - Classic Mode complete: every classic goal of the 14 classic levels beaten (Career100_Apply classic_goals=: the
//     goal manager's store, tagged `classic` as a classic goal's save tags it, and each level's classic goal
//     counter - what GetClassicLevelGoalCount reads), SetClassicTotalGoals 140, CLASSIC_STARTED (so CONTINUE
//     CLASSIC MODE is offered), classic_game_complete_rewards on Normal and Sick (GAME_COMPLETED_CLASSIC_*, the pro
//     bails movie, the classic secret skaters) and classic_goal_complete_rewards, with
//     CLASSIC_SEEN_GAME_COMPLETE_MESSAGE / _GOAL_. The classic goals' names come from each classic level's
//     <prefix>_Goals script's classic block, read from the compiled .qb (qb_walk.classicGoalCalls - NeverScript
//     stops in CA_scripts.qb): "<prefix>_classic_goal_<goal type><version>", GoalManager_CreateGoalName's rule;
//   - every gap of the 15 levels of levels_with_gaps got (Career100_Apply gaps=: each level's gap checklist and the
//     level marked visited - CSkaterCareer::AllGapsGot), worked out the way the game builds a level's checklist when
//     it loads (qb_walk.gapList: every node's TriggerScript followed to its EndGap calls), and the all-gaps reward
//     as ProcessAllGapsReward gives it (unlock_all_gameplay_cheat_codes, all_gaps_complete_rewards).
// Not part of it: the stat challenges' own progress (the stats are set directly); what only cheat codes or dead
// scripts unlock (CHEAT_UNLOCKED_1/3/5/10 - Cool Specials, Kid Mode, RollerSkates, Flame -, GOT_ALL_GOALS,
// SKATER_UNLOCKED_PEDS / _PEDGROUP9; docs/pending/cas100.md has the audit). Nothing is saved: the player saves with
// SAVE GAME.
//
// make_menu_level.ts calls optionsMenu() with the front end's GAME OPTIONS scripts (levels\mainmenu\
// mainmenu_options.qb) and writes the result into the overlay.

import { formatFixed, pyStrRepr } from "../lib/py.ts";
import { findall, finditer, search } from "../lib/re.ts";
import * as qbWalk from "./qb_walk.ts";

// The stats' maximum (the stats menu's "/10"; profile stats are clamped to 0..10 - 0x00585FD0).
export const STAT_MAX = 10;
// Flags a level's story sets on the way that belong to a finished story (guests and pros found, story skaters,
// special tricks).
export const STORY_FLAG_PREFIXES = ["GUEST_FOUND_", "PRO_FOUND_", "SKATER_UNLOCKED_", "TRICK_UNLOCKED_"] as const;

// A regex's text with its special characters escaped (Python's re.escape).
export function reEscape(text: string): string {
    return text.replace(/[.*+?^${}()|[\]\\/-]/g, "\\$&");
}

function indexOrThrow(text: string, sub: string, from = 0): number {
    const at = text.indexOf(sub, from);
    if (at < 0) throw new Error("substring not found");
    return at;
}

function charAt(text: string, i: number): string {
    if (i >= text.length) throw new RangeError("string index out of range");
    return text[i]!;
}

// {lower-case name: body} of the `script name ... { ... }` definitions in NeverScript text.
export function scripts(text: string): Map<string, string> {
    const out = new Map<string, string>();
    for (const match of finditer(/^script (\S+)[^\n]*\{/m, text)) {
        const [, end] = braces(text, match.end() - 1);
        out.set(match.str(1).toLowerCase(), text.slice(match.end(), end - 1));
    }
    return out;
}

// The { ... } that starts at the first '{' at or after `at`: [start, end past its '}'].
export function braces(text: string, at: number): [number, number] {
    const start = indexOrThrow(text, "{", at);
    let depth = 1;
    let i = start + 1;
    while (depth) {
        const c = charAt(text, i);
        if (c === '"') i = indexOrThrow(text, '"', i + 1);
        else if (c === "{") depth += 1;
        else if (c === "}") depth -= 1;
        i += 1;
    }
    return [start, i];
}

// The text of the global structure `name = { ... }`.
export function globalStruct(text: string, name: string): string {
    const found = search(new RegExp(`^${reEscape(name)} = \\{`, "mi"), text);
    if (!found) throw new Error(`${name} not found`);
    const [start, end] = braces(text, found.start());
    return text.slice(start, end);
}

// [type with a version or null, type without]
export type GoalType = [string | null, string];

// {addgoal_x: [type with a version, type without]} from the AddGoal_* scripts of goal_utilities.qb.
export function goalTypes(goalUtilities: string): Map<string, GoalType> {
    const types = new Map<string, GoalType>();
    for (const [name, body] of scripts(goalUtilities)) {
        if (!name.startsWith("addgoal_")) continue;
        const versioned = search(/FormatText TextName=goal_type "([^"%]*)%v"/i, body);
        const plain =
            search(/<goal_type> = "([^"]*)"/, body) ??
            search(/GoalManager_CreateGoalName <\.\.\.> goal_type="([^"]*)"/i, body);
        if (plain) types.set(name, [versioned ? versioned.str(1) : null, plain.str(1)]);
    }
    return types;
}

// A chapter: [level load script, level_total_points or null, goals_script or null].
export type Chapter = [string, number | null, string | null];

// CHAPTER_INFO's chapters in order.
export function chapters(chapterInfo: string): Chapter[] {
    const found = search(/^CHAPTER_INFO = \[/m, chapterInfo);
    if (!found) throw new Error("CHAPTER_INFO not found");
    const body = chapterInfo.slice(found.end());
    const out: Chapter[] = [];
    let i = 0;
    for (;;) {
        const nxt = search(/\S/, body.slice(i));
        if (!nxt || body[i + nxt.start()] !== "{") break;
        const [start, end] = braces(body, i + nxt.start());
        const chapter = body.slice(start, end);
        // (the top level only: skip nested structures' fields)
        let top = chapter.slice(1, -1).replace(/\{[^{}]*\}/g, "");
        while (/\{[^{}]*\}/.test(top)) top = top.replace(/\{[^{}]*\}/g, "");
        const level = search(/\blevel=(\w+)/, top);
        if (!level) throw new Error("a chapter without level=");
        const total = search(/\blevel_total_points=(\d+)/, top);
        const goals = search(/\bgoals_script=(\w+)/, top);
        out.push([
            level.str(1).toLowerCase(),
            total ? Number.parseInt(total.str(1), 10) : null,
            goals ? goals.str(1) : null,
        ]);
        i = end;
    }
    return out;
}

// A story goal: [goal name, reward points].
export type StoryGoal = [string, number];

// The story goals of a level's scripts. Its <prefix>_Goals script's story part (after the classic / net games' `if`)
// and the chapters' goals scripts, followed through the level's own scripts they call, to the AddGoal_* calls (the
// type from goal_utilities, `version=` appended).
export function storyGoals(
    levelText: string,
    prefix: string,
    goalsRoot: string,
    chapterGoalScripts: readonly string[],
    types: ReadonlyMap<string, GoalType>,
): StoryGoal[] {
    const levelScripts = scripts(levelText);
    const roots: string[] = [];
    let body = levelScripts.get(goalsRoot.toLowerCase());
    if (body !== undefined) {
        const classic = search(/GameModeEquals is_classic\) or \(InNetGame\)\)/i, body);
        if (classic) {
            const [, end] = braces(body, classic.end());
            body = body.slice(end);
        }
        roots.push(body);
    }
    for (const name of chapterGoalScripts) {
        const found = levelScripts.get(name.toLowerCase());
        if (found !== undefined) roots.push(found);
    }
    const goals: StoryGoal[] = [];
    const seenScripts = new Set<string>();
    const walk = (text: string): void => {
        for (const line of finditer(/^\s*(\w+)/m, text)) {
            const word = line.str(1).toLowerCase();
            const type = types.get(word);
            if (type !== undefined) {
                const [start, end] = braces(text, line.end());
                const block = text.slice(start, end);
                const version = search(/\bversion=(\d+)/i, block);
                const points = search(/\breward_points=(\d+)/i, block);
                const [withVersion, plain] = type;
                const goalType = version && withVersion !== null ? withVersion + version.str(1) : plain;
                const name = `${prefix}_goal_${goalType}`;
                if (!goals.some((g) => g[0].toLowerCase() === name.toLowerCase())) {
                    goals.push([name, points ? Number.parseInt(points.str(1), 10) : 0]);
                }
            } else if (
                levelScripts.has(word) &&
                !seenScripts.has(word) &&
                word.startsWith(`${prefix.toLowerCase()}_`)
            ) {
                seenScripts.add(word);
                walk(levelScripts.get(word)!);
            }
        }
    };
    for (const root of roots) walk(root);
    return goals;
}

// The story flags (STORY_FLAG_PREFIXES) the texts set, and their special tricks awarded to all profiles
// [trick, key_combo], in order, once each.
export function storyFlagsAndTricks(texts: readonly string[]): [string[], [string, string][]] {
    const flags: string[] = [];
    const tricks: [string, string][] = [];
    for (const text of texts) {
        for (const match of finditer(/SetGlobalFlag flag=(\w+)/i, text)) {
            const flag = match.str(1);
            const upper = flag.toUpperCase();
            if (STORY_FLAG_PREFIXES.some((p) => upper.startsWith(p)) && !flags.some((f) => f.toUpperCase() === upper)) {
                flags.push(flag);
            }
        }
        for (const match of finditer(/AwardNewSpecialTrickToProfile \{/, text)) {
            const [start, end] = braces(text, match.start());
            const block = text.slice(start, end);
            const trick = search(/\btrick=(\w+)/, block);
            const combo = search(/\bkey_combo=(\w+)/, block);
            if (
                block.includes("all_profiles") &&
                trick &&
                combo &&
                !tricks.some(([t, c]) => t === trick.str(1) && c === combo.str(1))
            ) {
                tricks.push([trick.str(1), combo.str(1)]);
            }
        }
    }
    return [flags, tricks];
}

// A level structure of Levels.qb (those with a level number).
export interface LevelStructure {
    load: string;
    prefix: string;
    number: number;
    pre: string;
    qb: string; // (the node array's file)
    goalsRoot: string;
}

// Levels.qb's level structures.
export function levelStructures(levelsQb: string): LevelStructure[] {
    const out: LevelStructure[] = [];
    for (const match of finditer(/^(Level_\w+) = \{/m, levelsQb)) {
        const [start, end] = braces(levelsQb, match.start());
        const text = levelsQb.slice(start, end);
        const load = search(/\bload_script=(\w+)/, text);
        const prefix = search(/\blevel="(\w+)"/, text);
        const pre = search(/\bpre="([^"]+)"/, text);
        const qb = search(/\bqb="([^"]+)"/, text);
        const number = search(/\blevel_number=(\w+)/, text);
        const goalsRoot = search(/\bgoals_script=(\w+)/, text);
        if (!load || !prefix || !pre || !qb || !number || !goalsRoot) continue;
        const value = search(new RegExp(`^${reEscape(number.str(1))} = (\\d+)`, "mi"), levelsQb);
        if (!value) continue;
        out.push({
            load: load.str(1).toLowerCase(),
            prefix: prefix.str(1),
            number: Number.parseInt(value.str(1), 10),
            pre: pre.str(1),
            // (NeverScript writes a string's '\' as '\\')
            qb: qb.str(1).replaceAll("\\\\", "\\"),
            goalsRoot: goalsRoot.str(1),
        });
    }
    return out;
}

// [prefix, level number]
export type LevelRef = [string, number];

// The classic goals of each classic mode level and the gaps of each level of levels_with_gaps:
// [{level number: [classic goal name]}, {level number: [gap]}, [prefix, number] of the classic levels,
// [prefix, number] of the gap levels]. levelSelect: gamemenu_levelselect.qb's text; globalQbs: {name: data} of
// qb_scripts.prx's .qb files; levelQbs(pre) -> {name: data} of a level's scripts archive.
export function classicAndGaps(
    levelsQb: string,
    levelSelect: string,
    goalUtilities: string,
    globalQbs: ReadonlyMap<string, Buffer>,
    levelQbs: (pre: string) => ReadonlyMap<string, Buffer>,
): [Map<number, string[]>, Map<number, qbWalk.Gap[]>, LevelRef[], LevelRef[]] {
    const structures = levelStructures(levelsQb);
    const byLoad = new Map(structures.map((s) => [s.load, s]));
    const byNumber = new Map<number, LevelStructure>();
    for (const s of structures) if (!byNumber.has(s.number)) byNumber.set(s.number, s);
    const classicList = globalStructArray(levelSelect, "level_select_menu_classic_level_info");
    const classicLevels: LevelStructure[] = [];
    for (const load of findall(/\blevel=(\w+)/, classicList)) {
        const s = byLoad.get(load.toLowerCase());
        if (s === undefined) throw new Error(`KeyError: ${pyStrRepr(load.toLowerCase())}`);
        if (!classicLevels.includes(s)) classicLevels.push(s);
    }
    const gapListText = globalStructArray(levelsQb, "levels_with_gaps");
    const gapLevels: LevelStructure[] = [];
    for (const name of findall(/\b(LevelNum_\w+)/, gapListText)) {
        const value = search(new RegExp(`^${reEscape(name)} = (\\d+)`, "mi"), levelsQb);
        if (!value) throw new Error(`${name} not found`);
        const s = byNumber.get(Number.parseInt(value.str(1), 10));
        if (s === undefined) throw new Error(`KeyError: ${value.str(1)}`);
        gapLevels.push(s);
    }
    // The game's scripts as loaded (qb_scripts.prx's), then each level's own over them.
    const symbols: qbWalk.Symbols = new Map();
    for (const data of globalQbs.values()) for (const [k, v] of qbWalk.scriptsIn(data)) symbols.set(k, v);
    const types = new Map<number, GoalType>();
    for (const [name, value] of goalTypes(goalUtilities)) types.set(qbWalk.checksum(name), value);
    const classic = new Map<number, string[]>();
    const gaps = new Map<number, qbWalk.Gap[]>();
    for (const level of [...classicLevels, ...gapLevels.filter((g) => !classicLevels.includes(g))]) {
        const files = new Map<string, Buffer>();
        for (const [name, data] of levelQbs(level.pre)) files.set(name.toLowerCase(), data);
        const levelSymbols: qbWalk.Symbols = new Map(symbols);
        for (const data of files.values()) for (const [k, v] of qbWalk.scriptsIn(data)) levelSymbols.set(k, v);
        if (classicLevels.includes(level)) {
            const names: string[] = [];
            const calls = qbWalk.classicGoalCalls(
                levelSymbols,
                qbWalk.checksum(level.goalsRoot),
                new Set(types.keys()),
            );
            for (const [call, version] of calls) {
                const [withVersion, plain] = types.get(call)!;
                const goalType = version !== null && withVersion !== null ? withVersion + String(version) : plain;
                const name = `${level.prefix}_classic_goal_${goalType}`;
                if (!names.some((n) => n.toLowerCase() === name.toLowerCase())) names.push(name);
            }
            classic.set(level.number, names);
        }
        if (gapLevels.includes(level)) {
            const file = files.get(level.qb.toLowerCase());
            if (file === undefined) throw new Error(`KeyError: ${pyStrRepr(level.qb.toLowerCase())}`);
            const nodes = qbWalk.globalValue(file, qbWalk.checksum(`${level.prefix}_NodeArray`));
            gaps.set(level.number, qbWalk.gapList(levelSymbols, Array.isArray(nodes) && nodes.length ? nodes : []));
        }
    }
    return [
        classic,
        gaps,
        classicLevels.map((s): LevelRef => [s.prefix, s.number]),
        gapLevels.map((s): LevelRef => [s.prefix, s.number]),
    ];
}

// The text of the global array `name = [ ... ]`.
export function globalStructArray(text: string, name: string): string {
    const found = search(new RegExp(`^${reEscape(name)} = \\[`, "mi"), text);
    if (!found) throw new Error(`${name} not found`);
    let depth = 0;
    let i = found.end() - 1;
    for (;;) {
        const c = charAt(text, i);
        if (c === '"') i = indexOrThrow(text, '"', i + 1);
        else if (c === "[") depth += 1;
        else if (c === "]") {
            depth -= 1;
            if (depth === 0) return text.slice(found.end() - 1, i + 1);
        }
        i += 1;
    }
}

// A story level as career_data works it out.
export interface StoryLevel {
    load: string;
    prefix: string;
    number: number;
    qb: string;
    pre: string;
    goalsRoot: string;
    total: number;
    goals: StoryGoal[];
    points: number;
}

export interface CareerData {
    levels: StoryLevel[];
    flags: string[];
    tricks: [string, string][];
    finalChapter: number;
    stats: string[];
    classic: Map<number, string[]>;
    gaps: Map<number, qbWalk.Gap[]>;
    classicLevels: LevelRef[];
    gapLevels: LevelRef[];
}

// Everything optionsMenu needs. levelsQb, chapterInfo, goalUtilities, stats, levelSelect: the texts of
// scripts\game\levels.qb, menu\chapter_info.qb, goal_utilities.qb, menu\stats.qb, menu\gamemenu_levelselect.qb;
// levelScripts(prefix, path, pre) -> the text of that level's scripts .qb; globalQbs: {name: data} of
// qb_scripts.prx's .qb files; levelQbs(pre) -> {name: data} of a level's scripts archive's .qb files.
export function careerData(
    levelsQb: string,
    chapterInfo: string,
    goalUtilities: string,
    stats: string,
    levelScripts: (prefix: string, qb: string, pre: string) => string,
    levelSelect: string,
    globalQbs: ReadonlyMap<string, Buffer>,
    levelQbs: (pre: string) => ReadonlyMap<string, Buffer>,
): CareerData {
    const types = goalTypes(goalUtilities);
    const chapterList = chapters(chapterInfo);
    const levels: Omit<StoryLevel, "total" | "goals" | "points">[] = [];
    const need = <T>(value: T | null | undefined, what: string): T => {
        if (value === null || value === undefined) throw new Error(`${what} not found`);
        return value;
    };
    for (const [load] of chapterList) {
        if (levels.some((entry) => entry.load === load)) continue;
        let structure: string | null = null;
        for (const match of finditer(/^(Level_\w+) = \{/m, levelsQb)) {
            const [start, end] = braces(levelsQb, match.start());
            const text = levelsQb.slice(start, end);
            if (new RegExp(`\\bload_script=${reEscape(load)}\\b`, "i").test(text)) {
                structure = text;
                break;
            }
        }
        if (structure === null) throw new Error(`no level structure loads ${load}`);
        const prefix = need(search(/\blevel="(\w+)"/, structure), "level=").str(1);
        const number = need(search(new RegExp(`^LevelNum_${reEscape(prefix)} = (\\d+)`, "mi"), levelsQb), "LevelNum_");
        levels.push({
            load,
            prefix,
            number: Number.parseInt(number.str(1), 10),
            // (NeverScript writes a string's '\' as '\\')
            qb: need(search(/\blevel_qb="([^"]+)"/, structure), "level_qb=")
                .str(1)
                .replaceAll("\\\\", "\\"),
            pre: need(search(/\bpre="([^"]+)"/, structure), "pre=").str(1),
            goalsRoot: need(search(/\bgoals_script=(\w+)/, structure), "goals_script=").str(1),
        });
    }
    const texts = [chapterInfo];
    const storyLevels: StoryLevel[] = [];
    for (const level of levels) {
        const mine = chapterList.filter((c) => c[0] === level.load);
        const totals = mine.map((c) => c[1]).filter((t): t is number => t !== null);
        if (!totals.length) throw new Error("list index out of range");
        const text = levelScripts(level.prefix, level.qb, level.pre);
        texts.push(text);
        const goals = storyGoals(
            text,
            level.prefix,
            level.goalsRoot,
            mine.map((c) => c[2]).filter((g): g is string => !!g),
            types,
        );
        storyLevels.push({
            ...level,
            total: totals[totals.length - 1]!,
            goals,
            points: goals.reduce((sum, [, points]) => sum + points, 0),
        });
    }
    const [flags, tricks] = storyFlagsAndTricks(texts);
    const last = need(
        search(/script GoToNextChapterPostCutscene \{.*?SetCurrentChapter chapter=(\d+)/s, chapterInfo),
        "GoToNextChapterPostCutscene",
    );
    const statList = need(search(/^stat_names = \[(.*?)^\]/ms, stats), "stat_names").str(1);
    const statNames = findall(/\{name=(`?\w+`?)/, statList);
    const [classic, gaps, classicLevels, gapLevels] = classicAndGaps(
        levelsQb,
        levelSelect,
        goalUtilities,
        globalQbs,
        levelQbs,
    );
    return {
        levels: storyLevels,
        flags,
        tricks,
        finalChapter: Number.parseInt(last.str(1), 10),
        stats: statNames,
        classic,
        gaps,
        classicLevels,
        gapLevels,
    };
}

interface ScriptsParts {
    story: string;
    levels: string;
    finalChapter: number;
    stats: string;
    classicTotal: number;
}

function scriptsText(p: ScriptsParts): string {
    return String.raw`script cas100_confirm {
    if ObjectExists id=current_menu_anchor {
        DestroyScreenElement id=current_menu_anchor
    }
    create_snazzy_dialog_box {
        title="Create 100% CAS"
        text="This replaces your current Story Mode and Classic Mode progress with both fully completed (every chapter, goal and gap, with all they unlock), opens all 16 levels and maxes your created skater's stats.\\n\\nSave with SAVE GAME afterwards to keep it.\\nContinue?"
        text_dims=(400.0, 0.0)
        no_bg
        pad_back_script=cas100_back
        buttons=[{font=small text="No" pad_choose_script=cas100_back}
            {font=small text="Yes" pad_choose_script=cas100_apply}
        ]
    }
}
script cas100_back {
    dialog_box_exit
    create_setup_options_menu
}
script cas100_apply {
    dialog_box_exit
    printf "Create 100 percent CAS: applying"
    change_gamemode_career
    reset_gamemode
    GoalManager_SetDifficultyLevel 1
    skater:StatsManager_ReInit
    GetArraySize CHAPTER_STATUS
    chapter_count = <array_size>
    status_array = CHAPTER_STATUS
    intro_array = CHAPTER_INTROS_VIEWED
    outtro_array = CHAPTER_OUTTROS_VIEWED
    index = 0
    while {
        SetArrayElement ArrayName=status_array index=<index> NewValue=CHAPTER_COMPLETE
        SetArrayElement ArrayName=intro_array index=<index> NewValue=1
        SetArrayElement ArrayName=outtro_array index=<index> NewValue=1
        cas100_chapter_done chapter=<index>
        index = (<index> + 1)
    } <chapter_count>
    cas100_classic_levels
${p.story}    SetRewardFlagArray flags=((story_game_complete_rewards [0]).flags)
    SetRewardFlagArray flags=((story_game_complete_rewards [1]).flags)
    SetRewardFlagArray flags=((story_game_complete_rewards [2]).flags)
    SetRewardFlagArray flags=(story_goal_complete_rewards.flags)
    SetGlobalFlag flag=CAREER_SEEN_GAME_COMPLETE_MESSAGE
    SetGlobalFlag flag=CAREER_SEEN_GOAL_COMPLETE_MESSAGE
    SetGlobalFlag flag=CAREER_STARTED
    SetClassicTotalGoals ${String(p.classicTotal)}
    SetRewardFlagArray flags=((classic_game_complete_rewards [0]).flags)
    SetRewardFlagArray flags=((classic_game_complete_rewards [1]).flags)
    SetRewardFlagArray flags=(classic_goal_complete_rewards.flags)
    SetGlobalFlag flag=CLASSIC_SEEN_GAME_COMPLETE_MESSAGE
    SetGlobalFlag flag=CLASSIC_SEEN_GOAL_COMPLETE_MESSAGE
    SetGlobalFlag flag=CLASSIC_STARTED
    unlock_all_gameplay_cheat_codes
    SetRewardFlagArray flags=(all_gaps_complete_rewards.flags)
    Career100_Apply levels=[
${p.levels}    ]
    SetCurrentChapter chapter=${String(p.finalChapter)}
    SetSkaterProfileInfoByName name=custom params={${p.stats}}
    printf "Create 100 percent CAS: done"
    create_snazzy_dialog_box {
        title="100% CAS"
        text="Story Mode and Classic Mode are 100% complete with every gap, all 16 levels are open and your created skater's stats are maxed.\\n\\nUse SAVE GAME to keep it."
        text_dims=(400.0, 0.0)
        no_bg
        pad_back_script=cas100_back
        buttons=[{font=small text="OK" pad_choose_script=cas100_back}]
    }
}
script cas100_chapter_done {
    if StructureContains structure=(CHAPTER_INFO [<chapter>]) start_cutscenes {
        cas100_cutscene_flags list=((CHAPTER_INFO [<chapter>]).start_cutscenes)
    }
    if StructureContains structure=(CHAPTER_INFO [<chapter>]) end_cutscenes {
        cas100_cutscene_flags list=((CHAPTER_INFO [<chapter>]).end_cutscenes)
    }
    level = ((CHAPTER_INFO [<chapter>]).level)
    cas100_unlock_level level=<level>
}
script cas100_cutscene_flags {
    GetArraySize <list>
    index = 0
    while {
        if StructureContains structure=(<list> [<index>]) flag {
            SetGlobalFlag flag=((<list> [<index>]).flag)
        }
        index = (<index> + 1)
    } <array_size>
}
script cas100_classic_levels {
    GetArraySize level_select_menu_classic_level_info
    index = 0
    while {
        if StructureContains structure=(level_select_menu_classic_level_info [<index>]) flag {
            flag = ((level_select_menu_classic_level_info [<index>]).flag)
            if ! (<flag> = GAME_CLASSIC_UNLOCKED_SE2) {
                SetGlobalFlag flag=<flag>
            }
            if StructureContains structure=(level_select_menu_classic_level_info [<index>]) seen_flag {
                seen_flag = ((level_select_menu_classic_level_info [<index>]).seen_flag)
                if ! (<seen_flag> = LEVEL_SEEN_SE2) {
                    SetGlobalFlag flag=<seen_flag>
                }
            }
        }
        index = (<index> + 1)
    } <array_size>
    if ! GetGlobalFlag flag=GAME_CLASSIC_UNLOCKED_SE2 {
        SetGlobalFlag flag=GAME_CLASSIC_UNLOCKED_SE2
        SetGlobalFlag flag=LEVEL_SEEN_SE2
        SetClassicTotalGoals 140
    }
}
script cas100_unlock_level {
    GetArraySize level_select_menu_level_info
    index = 0
    while {
        if ((level_select_menu_level_info [<index>].level) = <level>) {
            SetGlobalFlag flag=(level_select_menu_level_info [<index>].flag)
            if StructureContains structure=(level_select_menu_level_info [<index>]) seen_flag {
                SetGlobalFlag flag=(level_select_menu_level_info [<index>].seen_flag)
            }
        }
        index = (<index> + 1)
    } <array_size>
}
`;
}

const ITEM = `    if Career100_Available {
        add_roundbar_menu_item {
            text="CREATE 100% CAS"
            id=options_cas100
            pad_choose_script=cas100_confirm
        }
    }
`;

export function applyScript(data: CareerData): string {
    let story = data.flags.map((flag) => `    SetGlobalFlag flag=${flag} \n`).join("");
    story += data.tricks
        .map(
            ([trick, combo]) => `    AwardNewSpecialTrickToProfile {all_profiles trick=${trick} key_combo=${combo}} \n`,
        )
        .join("");
    // One entry per level number: its story part, classic goals and gaps (Career100_Apply).
    const entries = new Map<number, string[]>();
    const entry = (number: number): string[] => {
        let parts = entries.get(number);
        if (parts === undefined) {
            parts = [];
            entries.set(number, parts);
        }
        return parts;
    };
    for (const level of data.levels) {
        entry(level.number).push(
            `points=${level.points} total=${level.total} goals=[${level.goals.map(([g]) => g).join(" ")}]`,
        );
    }
    for (const [number, names] of data.classic) entry(number).push(`classic_goals=[${names.join(" ")}]`);
    for (const [number, gaps] of data.gaps) {
        entry(number).push(
            `gaps=[${gaps
                .map(([name, score, pos]) => {
                    const where = pos
                        ? ` pos=(${formatFixed(pos[0], 3)}, ${formatFixed(pos[1], 3)}, ${formatFixed(pos[2], 3)})`
                        : "";
                    return `{name="${nsString(name)}" score=${score}${where}}`;
                })
                .join(" ")}]`,
        );
    }
    const levels = [...entries].map(([number, parts]) => `        {level=${number} ${parts.join(" ")}} \n`).join("");
    const stats = data.stats.map((name) => `${name}=${STAT_MAX}`).join(" ");
    let classicTotal = 0;
    for (const names of data.classic.values()) classicTotal += names.length;
    return scriptsText({ story, levels, finalChapter: data.finalChapter, stats, classicTotal });
}

// A string as NeverScript reads it between double quotes ('\' written '\\'); the game compares gap names byte for
// byte, so only plain ASCII without '"' is passed through (none of the game's has any other).
export function nsString(text: string): string {
    for (const c of text) {
        const code = c.codePointAt(0)!;
        if (code > 126 || code < 32 || c === '"') throw new Error(`gap name ${pyStrRepr(text)}: not plain ASCII`);
    }
    return text.replaceAll("\\", "\\\\");
}

// mainmenu_options.qb's text with CREATE 100% CAS after GAME PROGRESS in create_setup_options_menu (shown while
// Career100_Available: the DLL's [features] create_100_cas) and the scripts.
export function optionsMenu(text: string, data: CareerData): string {
    const progress = search(/\n( *)add_roundbar_menu_item \{ *\n *text="GAME PROGRESS"/, text);
    if (!progress || findall(/\n *text="GAME PROGRESS"/, text).length !== 1) {
        throw new Error("mainmenu_options.qb: the GAME PROGRESS item not found once");
    }
    let [, end] = braces(text, progress.start());
    end = indexOrThrow(text, "\n", end) + 1;
    const indent = progress.str(1);
    const item = ITEM.split("\n")
        .map((line) => (line ? `${indent.slice(4)}${line}\n` : ""))
        .join("");
    text = text.slice(0, end) + item + text.slice(end);
    const register = text.indexOf("\n__register_checksums__");
    const at = register >= 0 ? register + 1 : text.length;
    return text.slice(0, at) + applyScript(data) + text.slice(at);
}

export function describe(data: CareerData): string {
    const lines: string[] = [];
    for (const level of data.levels) {
        lines.push(
            `  ${level.prefix} (level ${level.number}): ${level.goals.length} story goals, ${level.points} / ${level.total} points`,
        );
    }
    lines.push(
        `  story flags: ${data.flags.length}, special tricks: ${data.tricks.length}, final chapter ${data.finalChapter}, stats ${data.stats.join(", ")}`,
    );
    let classicTotal = 0;
    for (const names of data.classic.values()) classicTotal += names.length;
    lines.push(
        `  classic goals: ${data.classicLevels
            .map(([prefix, number]) => `${prefix} ${data.classic.get(number)!.length}`)
            .join(", ")} = ${classicTotal}`,
    );
    let gapTotal = 0;
    for (const gaps of data.gaps.values()) gapTotal += gaps.length;
    lines.push(
        `  gaps: ${data.gapLevels.map(([prefix, number]) => `${prefix} ${data.gaps.get(number)!.length}`).join(", ")} = ${gapTotal}`,
    );
    return lines.join("\n");
}
