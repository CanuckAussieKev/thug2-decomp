# NOT ORIGINAL - GAME OPTIONS' "CREATE 100% CAS" for make_menu_level.py's data overlay (the user's request): a Story
# Mode and Classic Mode career as a player who finished both completely (every goal and gap, everything they
# unlock) would have it, and the created skater's stats maxed.
#
# What the option does (the script cas100_apply, after a Yes in a warning dialog), with the game's own scripts and
# commands wherever there are any:
#   - reset_gamemode in story mode (as START NEW STORY MODE: chapter 0, chapter status, every profile's stats and
#     tricks back to default, CareerReset / GoalManager_ResetCareer career, the story flags cleared), difficulty
#     Normal (GoalManager_SetDifficultyLevel 1, skater:StatsManager_ReInit, as set_gamemode_difficulty);
#   - every chapter of CHAPTER_INFO complete with its intro and outro seen, its cutscenes' VIEWED_CUTSCENE flags, its
#     level unlocked and seen in the level select (as UnlockNextChapter); the current chapter the one the game goes
#     back to after the last (GoToNextChapterPostCutscene's SetCurrentChapter);
#   - what the levels' stories give on the way (from the level scripts: the GUEST_FOUND_ / PRO_FOUND_ /
#     SKATER_UNLOCKED_ / TRICK_UNLOCKED_ flags they set, the special tricks they award to all profiles);
#   - the story's completion rewards on all three difficulties (story_game_complete_rewards: GAME_COMPLETED_*,
#     the PRO SKATER level, the WDT movie, the secret skaters) and for all goals (story_goal_complete_rewards),
#     through SetRewardFlagArray, with CAREER_SEEN_GAME_COMPLETE_MESSAGE / _GOAL_ so they aren't announced again;
#   - every story goal of every level beaten and the story points full: Career100_Apply (ours, C++ -
#     src/game/front/career_complete.h; no script command sets those) with, per level, its number, the reward points
#     of its story goals, its points potential (the last level_total_points its chapters set) and its story goals'
#     names (worked out here as GoalManager_CreateGoalName makes them: "<level prefix>_goal_<goal type><version>");
#   - the created skater (profile `custom`) with every stat (stat_names) at 10 (SetSkaterProfileInfoByName);
#   - every level the classic mode's progress opens (cas100_classic_levels, as CheckClassicAdvancementStatus does when
#     each bracket's goals are done: every bracket's flag and its levels' LEVEL_SEEN_ flags, and the last bracket's
#     THE TRIANGLE - GAME_CLASSIC_UNLOCKED_SE2, LEVEL_SEEN_SE2, SetClassicTotalGoals 140). Outside story and classic
#     mode (free skate, 2 player...) the level select goes by the LEVEL_SEEN_ flags (level_select_should_use_seen_flags):
#     the story only gives the 7 story levels and PRO SKATER; SCHOOL, PHILADELPHIA, DOWNHILL JAM, LOS ANGELES, CANADA,
#     AIRPORT and THE TRIANGLE only come from classic mode. So all 16 THUG2 levels are open there.
#   - Classic Mode complete: every classic goal of the 14 classic levels beaten (Career100_Apply classic_goals=: the
#     goal manager's store, tagged `classic` as a classic goal's save tags it, and each level's classic goal
#     counter - what GetClassicLevelGoalCount reads), SetClassicTotalGoals 140, CLASSIC_STARTED (so CONTINUE
#     CLASSIC MODE is offered), classic_game_complete_rewards on Normal and Sick (GAME_COMPLETED_CLASSIC_*, the pro
#     bails movie, the classic secret skaters) and classic_goal_complete_rewards, with
#     CLASSIC_SEEN_GAME_COMPLETE_MESSAGE / _GOAL_. The classic goals' names come from each classic level's
#     <prefix>_Goals script's classic block, read from the compiled .qb (qb_walk.classic_goal_calls - NeverScript
#     stops in CA_scripts.qb): "<prefix>_classic_goal_<goal type><version>", GoalManager_CreateGoalName's rule;
#   - every gap of the 15 levels of levels_with_gaps got (Career100_Apply gaps=: each level's gap checklist and the
#     level marked visited - CSkaterCareer::AllGapsGot), worked out the way the game builds a level's checklist when
#     it loads (qb_walk.gap_list: every node's TriggerScript followed to its EndGap calls), and the all-gaps reward
#     as ProcessAllGapsReward gives it (unlock_all_gameplay_cheat_codes, all_gaps_complete_rewards).
# Not part of it: the stat challenges' own progress (the stats are set directly); what only cheat codes or dead
# scripts unlock (CHEAT_UNLOCKED_1/3/5/10 - Cool Specials, Kid Mode, RollerSkates, Flame -, GOT_ALL_GOALS,
# SKATER_UNLOCKED_PEDS / _PEDGROUP9; docs/pending/cas100.md has the audit). Nothing is saved: the player saves with
# SAVE GAME.
#
# make_menu_level.py calls options_menu() with the front end's GAME OPTIONS scripts (levels\mainmenu\
# mainmenu_options.qb) and writes the result into the overlay.

import re

import qb_walk

# The stats' maximum (the stats menu's "/10"; profile stats are clamped to 0..10 - 0x00585FD0).
STAT_MAX = 10
# Flags a level's story sets on the way that belong to a finished story (guests and pros found, story skaters,
# special tricks).
STORY_FLAG_PREFIXES = ("GUEST_FOUND_", "PRO_FOUND_", "SKATER_UNLOCKED_", "TRICK_UNLOCKED_")


def scripts(text):
    # {lower-case name: body} of the `script name ... { ... }` definitions in NeverScript text.
    out = {}
    for match in re.finditer(r"^script (\S+)[^\n]*\{", text, re.M):
        _, end = braces(text, match.end() - 1)
        out[match.group(1).lower()] = text[match.end():end - 1]
    return out


def braces(text, at):
    # The { ... } that starts at the first '{' at or after `at`: (start, end past its '}').
    start = text.index("{", at)
    depth, i = 1, start + 1
    while depth:
        c = text[i]
        if c == '"':
            i = text.index('"', i + 1)
        elif c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
        i += 1
    return start, i


def global_struct(text, name):
    # The text of the global structure `name = { ... }`.
    found = re.search(r"^%s = \{" % re.escape(name), text, re.M | re.I)
    if not found:
        raise ValueError("%s not found" % name)
    start, end = braces(text, found.start())
    return text[start:end]


def goal_types(goal_utilities):
    # {addgoal_x: (type with a version, type without)} from the AddGoal_* scripts of goal_utilities.qb.
    types = {}
    for name, body in scripts(goal_utilities).items():
        if not name.startswith("addgoal_"):
            continue
        versioned = re.search(r'FormatText TextName=goal_type "([^"%]*)%v"', body, re.I)
        plain = (re.search(r'<goal_type> = "([^"]*)"', body)
                 or re.search(r'GoalManager_CreateGoalName <\.\.\.> goal_type="([^"]*)"', body, re.I))
        if plain:
            types[name] = (versioned.group(1) if versioned else None, plain.group(1))
    return types


def chapters(chapter_info):
    # CHAPTER_INFO's chapters in order: (level load script, level_total_points, goals_script or None).
    found = re.search(r"^CHAPTER_INFO = \[", chapter_info, re.M)
    body = chapter_info[found.end():]
    out = []
    i = 0
    while True:
        nxt = re.search(r"\S", body[i:])
        if not nxt or body[i + nxt.start()] != "{":
            break
        start, end = braces(body, i + nxt.start())
        chapter = body[start:end]
        # (the top level only: skip nested structures' fields)
        top = re.sub(r"\{[^{}]*\}", "", chapter[1:-1])
        while re.search(r"\{[^{}]*\}", top):
            top = re.sub(r"\{[^{}]*\}", "", top)
        level = re.search(r"\blevel=(\w+)", top).group(1)
        total = re.search(r"\blevel_total_points=(\d+)", top)
        goals = re.search(r"\bgoals_script=(\w+)", top)
        out.append((level.lower(), int(total.group(1)) if total else None, goals.group(1) if goals else None))
        i = end
    return out


def story_goals(level_text, prefix, goals_root, chapter_goal_scripts, types):
    # The story goals of a level's scripts: [(goal name, reward points)]. Its <prefix>_Goals script's story part
    # (after the classic / net games' `if`) and the chapters' goals scripts, followed through the level's own
    # scripts they call, to the AddGoal_* calls (the type from goal_utilities, `version=` appended).
    level_scripts = scripts(level_text)
    roots = []
    body = level_scripts.get(goals_root.lower())
    if body is not None:
        classic = re.search(r"GameModeEquals is_classic\) or \(InNetGame\)\)", body, re.I)
        if classic:
            _, end = braces(body, classic.end())
            body = body[end:]
        roots.append(body)
    roots += [level_scripts[name.lower()] for name in chapter_goal_scripts if name.lower() in level_scripts]
    goals, seen_scripts = [], set()

    def walk(body):
        for line in re.finditer(r"^\s*(\w+)", body, re.M):
            word = line.group(1).lower()
            if word in types:
                start, end = braces(body, line.end())
                block = body[start:end]
                version = re.search(r"\bversion=(\d+)", block, re.I)
                points = re.search(r"\breward_points=(\d+)", block, re.I)
                with_version, plain = types[word]
                goal_type = with_version + version.group(1) if version and with_version is not None else plain
                name = "%s_goal_%s" % (prefix, goal_type)
                if name.lower() not in [g[0].lower() for g in goals]:
                    goals.append((name, int(points.group(1)) if points else 0))
            elif word in level_scripts and word not in seen_scripts and word.startswith(prefix.lower() + "_"):
                seen_scripts.add(word)
                walk(level_scripts[word])

    for root in roots:
        walk(root)
    return goals


def story_flags_and_tricks(texts):
    # The story flags (STORY_FLAG_PREFIXES) the texts set, and their special tricks awarded to all profiles
    # [(trick, key_combo)], in order, once each.
    flags, tricks = [], []
    for text in texts:
        for match in re.finditer(r"SetGlobalFlag flag=(\w+)", text, re.I):
            flag = match.group(1)
            if flag.upper().startswith(STORY_FLAG_PREFIXES) and flag.upper() not in [f.upper() for f in flags]:
                flags.append(flag)
        for match in re.finditer(r"AwardNewSpecialTrickToProfile \{", text):
            start, end = braces(text, match.start())
            block = text[start:end]
            trick = re.search(r"\btrick=(\w+)", block)
            combo = re.search(r"\bkey_combo=(\w+)", block)
            if "all_profiles" in block and trick and combo and (trick.group(1), combo.group(1)) not in tricks:
                tricks.append((trick.group(1), combo.group(1)))
    return flags, tricks


def level_structures(levels_qb):
    # Levels.qb's level structures: [{load, prefix, number, pre, qb (the node array's file), goals_root}] (those
    # with a level number).
    out = []
    for match in re.finditer(r"^(Level_\w+) = \{", levels_qb, re.M):
        start, end = braces(levels_qb, match.start())
        text = levels_qb[start:end]
        fields = {key: re.search(pattern, text) for key, pattern in (
            ("load", r"\bload_script=(\w+)"), ("prefix", r'\blevel="(\w+)"'), ("pre", r'\bpre="([^"]+)"'),
            ("qb", r'\bqb="([^"]+)"'), ("number", r"\blevel_number=(\w+)"), ("goals_root", r"\bgoals_script=(\w+)"))}
        if not all(fields.values()):
            continue
        number = re.search(r"^%s = (\d+)" % re.escape(fields["number"].group(1)), levels_qb, re.M | re.I)
        if not number:
            continue
        out.append({"load": fields["load"].group(1).lower(), "prefix": fields["prefix"].group(1),
                    "number": int(number.group(1)), "pre": fields["pre"].group(1),
                    # (NeverScript writes a string's '\' as '\\')
                    "qb": fields["qb"].group(1).replace("\\\\", "\\"), "goals_root": fields["goals_root"].group(1)})
    return out


def classic_and_gaps(levels_qb, level_select, goal_utilities, global_qbs, level_qbs):
    # The classic goals of each classic mode level and the gaps of each level of levels_with_gaps:
    # ({level number: [classic goal name]}, {level number: [(gap name, score)]}, [(prefix, number)] of the classic
    # levels, [(prefix, number)] of the gap levels). level_select: gamemenu_levelselect.qb's text; global_qbs:
    # {name: data} of qb_scripts.prx's .qb files; level_qbs(pre) -> {name: data} of a level's scripts archive.
    structures = level_structures(levels_qb)
    by_load = {s["load"]: s for s in structures}
    by_number = {}
    for s in structures:
        by_number.setdefault(s["number"], s)
    classic_list = global_struct_array(level_select, "level_select_menu_classic_level_info")
    classic_levels = []
    for load in re.findall(r"\blevel=(\w+)", classic_list):
        if by_load[load.lower()] not in classic_levels:
            classic_levels.append(by_load[load.lower()])
    gap_list_text = global_struct_array(levels_qb, "levels_with_gaps")
    gap_levels = []
    for name in re.findall(r"\b(LevelNum_\w+)", gap_list_text):
        number = int(re.search(r"^%s = (\d+)" % re.escape(name), levels_qb, re.M | re.I).group(1))
        gap_levels.append(by_number[number])
    # The game's scripts as loaded (qb_scripts.prx's), then each level's own over them.
    symbols = {}
    for data in global_qbs.values():
        symbols.update(qb_walk.scripts_in(data))
    types = {qb_walk.checksum(name): value for name, value in goal_types(goal_utilities).items()}
    classic, gaps = {}, {}
    for level in classic_levels + [g for g in gap_levels if g not in classic_levels]:
        files = {name.lower(): data for name, data in level_qbs(level["pre"]).items()}
        level_symbols = dict(symbols)
        for data in files.values():
            level_symbols.update(qb_walk.scripts_in(data))
        if level in classic_levels:
            names = []
            for call, version in qb_walk.classic_goal_calls(level_symbols, qb_walk.checksum(level["goals_root"]),
                                                             set(types)):
                with_version, plain = types[call]
                goal_type = with_version + str(version) if version is not None and with_version is not None else plain
                name = "%s_classic_goal_%s" % (level["prefix"], goal_type)
                if name.lower() not in [n.lower() for n in names]:
                    names.append(name)
            classic[level["number"]] = names
        if level in gap_levels:
            nodes = qb_walk.global_value(files[level["qb"].lower()], qb_walk.checksum(level["prefix"] + "_NodeArray"))
            gaps[level["number"]] = qb_walk.gap_list(level_symbols, nodes or [])
    return (classic, gaps, [(s["prefix"], s["number"]) for s in classic_levels],
            [(s["prefix"], s["number"]) for s in gap_levels])


def global_struct_array(text, name):
    # The text of the global array `name = [ ... ]`.
    found = re.search(r"^%s = \[" % re.escape(name), text, re.M | re.I)
    if not found:
        raise ValueError("%s not found" % name)
    depth, i = 0, found.end() - 1
    while True:
        c = text[i]
        if c == '"':
            i = text.index('"', i + 1)
        elif c == "[":
            depth += 1
        elif c == "]":
            depth -= 1
            if depth == 0:
                return text[found.end() - 1:i + 1]
        i += 1


def career_data(levels_qb, chapter_info, goal_utilities, stats, level_scripts, level_select, global_qbs, level_qbs):
    # Everything options_menu needs. levels_qb, chapter_info, goal_utilities, stats, level_select: the texts of
    # scripts\game\levels.qb, menu\chapter_info.qb, goal_utilities.qb, menu\stats.qb, menu\gamemenu_levelselect.qb;
    # level_scripts(prefix, path, pre) -> the text of that level's scripts .qb; global_qbs: {name: data} of
    # qb_scripts.prx's .qb files; level_qbs(pre) -> {name: data} of a level's scripts archive's .qb files.
    types = goal_types(goal_utilities)
    chapter_list = chapters(chapter_info)
    levels = []
    for load, _, _ in chapter_list:
        if load in [entry["load"] for entry in levels]:
            continue
        structure = None
        for match in re.finditer(r"^(Level_\w+) = \{", levels_qb, re.M):
            start, end = braces(levels_qb, match.start())
            text = levels_qb[start:end]
            if re.search(r"\bload_script=%s\b" % re.escape(load), text, re.I):
                structure = text
                break
        if structure is None:
            raise ValueError("no level structure loads %s" % load)
        prefix = re.search(r'\blevel="(\w+)"', structure).group(1)
        number = re.search(r"^LevelNum_%s = (\d+)" % re.escape(prefix), levels_qb, re.M | re.I)
        levels.append({"load": load, "prefix": prefix, "number": int(number.group(1)),
                       # (NeverScript writes a string's '\' as '\\')
                       "qb": re.search(r'\blevel_qb="([^"]+)"', structure).group(1).replace("\\\\", "\\"),
                       "pre": re.search(r'\bpre="([^"]+)"', structure).group(1),
                       "goals_root": re.search(r"\bgoals_script=(\w+)", structure).group(1)})
    texts = [chapter_info]
    for level in levels:
        mine = [c for c in chapter_list if c[0] == level["load"]]
        level["total"] = [c[1] for c in mine if c[1] is not None][-1]
        text = level_scripts(level["prefix"], level["qb"], level["pre"])
        texts.append(text)
        level["goals"] = story_goals(text, level["prefix"], level["goals_root"], [c[2] for c in mine if c[2]], types)
        level["points"] = sum(points for _, points in level["goals"])
    flags, tricks = story_flags_and_tricks(texts)
    last = re.search(r"script GoToNextChapterPostCutscene \{.*?SetCurrentChapter chapter=(\d+)", chapter_info, re.S)
    stat_list = re.search(r"^stat_names = \[(.*?)^\]", stats, re.M | re.S).group(1)
    stat_names = re.findall(r"\{name=(`?\w+`?)", stat_list)
    classic, gaps, classic_levels, gap_levels = classic_and_gaps(levels_qb, level_select, goal_utilities, global_qbs,
                                                                 level_qbs)
    return {"levels": levels, "flags": flags, "tricks": tricks, "final_chapter": int(last.group(1)),
            "stats": stat_names, "classic": classic, "gaps": gaps, "classic_levels": classic_levels,
            "gap_levels": gap_levels}


SCRIPTS = r"""script cas100_confirm {
    if ObjectExists id=current_menu_anchor {
        DestroyScreenElement id=current_menu_anchor
    }
    create_snazzy_dialog_box {
        title="Create 100%% CAS"
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
%(story)s    SetRewardFlagArray flags=((story_game_complete_rewards [0]).flags)
    SetRewardFlagArray flags=((story_game_complete_rewards [1]).flags)
    SetRewardFlagArray flags=((story_game_complete_rewards [2]).flags)
    SetRewardFlagArray flags=(story_goal_complete_rewards.flags)
    SetGlobalFlag flag=CAREER_SEEN_GAME_COMPLETE_MESSAGE
    SetGlobalFlag flag=CAREER_SEEN_GOAL_COMPLETE_MESSAGE
    SetGlobalFlag flag=CAREER_STARTED
    SetClassicTotalGoals %(classic_total)d
    SetRewardFlagArray flags=((classic_game_complete_rewards [0]).flags)
    SetRewardFlagArray flags=((classic_game_complete_rewards [1]).flags)
    SetRewardFlagArray flags=(classic_goal_complete_rewards.flags)
    SetGlobalFlag flag=CLASSIC_SEEN_GAME_COMPLETE_MESSAGE
    SetGlobalFlag flag=CLASSIC_SEEN_GOAL_COMPLETE_MESSAGE
    SetGlobalFlag flag=CLASSIC_STARTED
    unlock_all_gameplay_cheat_codes
    SetRewardFlagArray flags=(all_gaps_complete_rewards.flags)
    Career100_Apply levels=[
%(levels)s    ]
    SetCurrentChapter chapter=%(final_chapter)d
    SetSkaterProfileInfoByName name=custom params={%(stats)s}
    printf "Create 100 percent CAS: done"
    create_snazzy_dialog_box {
        title="100%% CAS"
        text="Story Mode and Classic Mode are 100%% complete with every gap, all 16 levels are open and your created skater's stats are maxed.\\n\\nUse SAVE GAME to keep it."
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
"""

ITEM = """    if Career100_Available {
        add_roundbar_menu_item {
            text="CREATE 100%% CAS"
            id=options_cas100
            pad_choose_script=cas100_confirm
        }
    }
"""


def apply_script(data):
    story = "".join("    SetGlobalFlag flag=%s \n" % flag for flag in data["flags"])
    story += "".join("    AwardNewSpecialTrickToProfile {all_profiles trick=%s key_combo=%s} \n" % trick
                     for trick in data["tricks"])
    # One entry per level number: its story part, classic goals and gaps (Career100_Apply).
    entries = {}
    for level in data["levels"]:
        entries.setdefault(level["number"], []).append(
            "points=%d total=%d goals=[%s]" % (level["points"], level["total"], " ".join(g for g, _ in level["goals"])))
    for number, names in data["classic"].items():
        entries.setdefault(number, []).append("classic_goals=[%s]" % " ".join(names))
    for number, gaps in data["gaps"].items():
        entries.setdefault(number, []).append("gaps=[%s]" % " ".join(
            '{name="%s" score=%d%s}' % (ns_string(name), score,
                                        " pos=(%.3f, %.3f, %.3f)" % pos if pos else "")
            for name, score, pos in gaps))
    levels = "".join("        {level=%d %s} \n" % (number, " ".join(parts)) for number, parts in entries.items())
    stats = " ".join("%s=%d" % (name, STAT_MAX) for name in data["stats"])
    return SCRIPTS % {"story": story, "levels": levels, "final_chapter": data["final_chapter"], "stats": stats,
                      "classic_total": sum(len(names) for names in data["classic"].values())}


def ns_string(text):
    # A string as NeverScript reads it between double quotes ('\' written '\\'); the game compares gap names byte for
    # byte, so only plain ASCII without '"' is passed through (none of the game's has any other).
    if any(ord(c) > 126 or ord(c) < 32 or c == '"' for c in text):
        raise ValueError("gap name %r: not plain ASCII" % text)
    return text.replace("\\", "\\\\")


def options_menu(text, data):
    # mainmenu_options.qb's text with CREATE 100% CAS after GAME PROGRESS in create_setup_options_menu (shown while
    # Career100_Available: the DLL's [features] create_100_cas) and the scripts.
    progress = re.search(r'\n( *)add_roundbar_menu_item \{ *\n *text="GAME PROGRESS"', text)
    if not progress or len(re.findall(r'\n *text="GAME PROGRESS"', text)) != 1:
        raise ValueError("mainmenu_options.qb: the GAME PROGRESS item not found once")
    _, end = braces(text, progress.start())
    end = text.index("\n", end) + 1
    indent = progress.group(1)
    item = "".join(indent[4:] + line + "\n" if line else "" for line in (ITEM % ()).split("\n"))
    text = text[:end] + item + text[end:]
    register = text.find("\n__register_checksums__")
    at = register + 1 if register >= 0 else len(text)
    return text[:at] + apply_script(data) + text[at:]


def describe(data):
    lines = []
    for level in data["levels"]:
        lines.append("  %s (level %d): %d story goals, %d / %d points" % (level["prefix"], level["number"],
                                                                         len(level["goals"]), level["points"],
                                                                         level["total"]))
    lines.append("  story flags: %d, special tricks: %d, final chapter %d, stats %s"
                 % (len(data["flags"]), len(data["tricks"]), data["final_chapter"], ", ".join(data["stats"])))
    lines.append("  classic goals: %s = %d" % (", ".join("%s %d" % (prefix, len(data["classic"][number]))
                                                         for prefix, number in data["classic_levels"]),
                                               sum(len(names) for names in data["classic"].values())))
    lines.append("  gaps: %s = %d" % (", ".join("%s %d" % (prefix, len(data["gaps"][number]))
                                                for prefix, number in data["gap_levels"]),
                                      sum(len(gaps) for gaps in data["gaps"].values())))
    return "\n".join(lines)
