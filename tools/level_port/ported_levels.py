# NOT ORIGINAL - what tools/menu_level/make_menu_level.py adds to the data overlay for the levels converted from other
# games (tools/thps4_levels, tools/thug1_levels, tools/thps3_levels): their files, their level structures in Levels.qb and their signs in
# the level select.
#
# Each converter writes a staging folder in the THUG2 folder (not read by the game): <thug2>\<staging>\levels\<level>\
# (the converted files) and <thug2>\<staging>\levels.json, a list of levels:
#   folder        the level's name in its own game (THPS4's folder, THUG1's level code)
#   level         the level's name in THUG2 (its folder under levels\, its files, Load_<level>, Level_<level>)
#   sky           the sky level's name, or null
#   display       the level select's sign text ("THUG1 NEW JERSEY")
#   name          the level's own name (the level structure's name=)
#   game          the game it comes from ("THPS4", "THUG1", "THPS3")
#   location      where it is in the real world ("New Jersey, USA") - for the level select
#   icon          a level select sign texture that suits it (THUG2's own, e.g. "sign_philly")
#   level_num     the level select's number for its sign (unique; no career records - the info panel skips them)
#   default_stats the skater's stats there
#   lighting      THUG2 level structure keys (ambient / directional lights)

import json
import os
import shutil
import sys

MANIFEST = "levels.json"
# The staging folders, in the order their levels are listed.
STAGINGS = ("thug2-decomp-thps4", "thug2-decomp-thug1", "thug2-decomp-thps3")


def read_manifest(path):
    if not os.path.exists(path):
        return None
    return json.load(open(path, encoding="utf-8"))


def write_manifest(path, manifest):
    json.dump(manifest, open(path, "w", encoding="utf-8"), indent=1)


def load_manifest(game):
    # Every converted level of every staging folder, or None when there are none.
    levels = []
    for staging in STAGINGS:
        manifest = read_manifest(os.path.join(game, staging, MANIFEST))
        if not manifest:
            continue
        for entry in manifest["levels"]:
            entry = dict(entry)
            entry["staging"] = staging
            levels.append(entry)
    numbers = [e["level_num"] for e in levels]
    if len(set(numbers)) != len(numbers):
        sys.exit("converted levels: a level select number is used twice (%s)" % numbers)
    return {"levels": levels} if levels else None


def struct_name(entry):
    return "Level_" + entry["level"]


def levels_qb_text(manifest):
    # The level structures and load scripts, as NeverScript text for Levels.qb (LevelNum_Default as the shop's).
    parts = []
    for entry in manifest["levels"]:
        s = struct_name(entry)
        load = "Load_" + entry["level"]
        lines = ["script %s {" % load, "    load_level %s " % s, "} ", "%s = {" % s,
                 "    structure_name=%s " % s,
                 "    load_script=%s " % load,
                 '    name="%s" ' % entry["name"].replace('"', ""),
                 '    loading_screen="loadscrn_generic" ',
                 "    loading_time=8.0 ",
                 "    COIM_size=1331200 ",
                 '    level_name="%s" ' % entry["level"],
                 '    level="%s" ' % entry["level"]]
        if entry.get("sky"):
            lines.append('    sky="%s" ' % entry["sky"])
        lines += ['    qb="levels\\\\%s\\\\%s.qb" ' % (entry["level"], entry["level"]),
                  "    default_stats=%d " % entry["default_stats"],
                  "    default_stats_classic=%d " % entry["default_stats"],
                  "    level_number=LevelNum_Default ",
                  "    skip_teammate_pro "]
        for key, value in entry["lighting"].items():
            lines.append("    %s=%s " % (key, repr(float(value)) if isinstance(value, float) else value))
        lines.append("} ")
        parts.append("\n".join(lines) + "\n")
    return "".join(parts)


def add_to_levels(text, manifest):
    anchor = "\nscript Load_TestLevel {"
    if text.count(anchor) != 1:
        sys.exit("Levels.qb: Load_TestLevel not found once")
    return text.replace(anchor, "\n" + levels_qb_text(manifest) + anchor[1:], 1)


def signs_text(manifest, indent):
    return "".join('%s{text="%s" level_num=%d num_goals=0 flag=LEVEL_UNLOCKED_TR level=Load_%s '
                   'sign_texture=sign_training dot_pos=(89.0, 82.0)} \n'
                   % (indent, e["display"].replace('"', ""), e["level_num"], e["level"]) for e in manifest["levels"])


def sign_numbers(manifest):
    return [e["level_num"] for e in manifest["levels"]]


def install_files(game, overlay, manifest=None):
    # The converted levels (every staging folder's) into the overlay's levels folder.
    installed = []
    for staging in STAGINGS:
        source = os.path.join(game, staging, "levels")
        if not os.path.isdir(source):
            continue
        for entry in os.listdir(source):
            target = os.path.join(overlay, "levels", entry)
            if os.path.isdir(target):
                shutil.rmtree(target)
            shutil.copytree(os.path.join(source, entry), target)
            installed.append(entry)
    return sorted(installed)
