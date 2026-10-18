# NOT ORIGINAL - the level select in sections, one per game, for make_menu_level.py's data overlay:
#   - Up / Down switch between the games (THUG2, then THUG1 when it has been converted, then THPS4); Left / Right
#     move through the section's levels as before. A header above the signs names the section; the helper text
#     says "Up/Down = Game". Classic mode (its brackets use Up / Down) keeps the game's own list.
#   - every level we add gets a star (the game's level_dot) at its real-world place on the globe, which turns to it
#     as to THUG2's own levels; a level with no place (the skate shops, TEST LEVEL) spins the globe, as THUG2's
#     PRO SKATER does;
#   - and a sign of its own (level_select_icons.py: THUG2's sign frame with a pictogram) instead of sign_training.
#
# The sections are data (SECTIONS below, built by sections()): a section is a title and a list of levels; a level
# is its sign text, level number, load script, icon and place. Adding a game is adding a section.
#
# What it changes:
#   scripts\game\menu\gamemenu_levelselect.qb  (after make_menu_level.add_skateshop_sign) - edit_level_select()
#   scripts\game\menu\sprites.qb               the new signs in load_level_select_textures_to_main_memory's list
#   images\LevelSelectSprites\sign_*.img.xbx   the signs - write_signs()
#
# The script side (gamemenu_levelselect.qb):
#   level_select_section (global, kept between visits) picks the section; level_select_get_section_list returns
#   its list (level_list, the same kind of value as the game's own `level_list = level_select_menu_level_info`) and
#   title. create_level_select_menu takes the list from it outside classic mode, adds pad_up / pad_down handlers
#   (level_select_section_change: the next / previous section, the menu built again - the globe keeps turning from
#   where it was) and the header. The dots are worked out again on every build (level_select_calculate_dots caches
#   them by game mode and dot count, which two sections can share). TRAINING loses its new_vmenu (a one-item
#   column of its own whose pad_up / pad_down handlers would get the section switch first; nothing else was in it
#   outside development builds). level_select_valid_choice's dot animation goes over the section's list.

import math
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import level_select_icons  # noqa: E402

# --- where the levels are ----------------------------------------------------------------------------------------

# THUG2's own stars (level_select_menu_level_info's dot_pos) and the real places they stand for: the map is drawn
# freehand (a 512 x 256 strip of 64-pixel tiles, level_map_pieces), so a place is put where THUG2 would put it -
# a longitude / latitude fit through these, plus the nearby stars' own offsets from that fit (weighted by
# closeness), so a place near Los Angeles lands where LOS ANGELES's star is relative to it.
THUG2_STARS = {
    # name: (longitude, latitude, map x, map y)
    "LOS ANGELES": (-118.24, 34.05, 78, 102),
    "BOSTON": (-71.06, 42.36, 135, 80),
    "NEW ORLEANS": (-90.07, 29.95, 104, 94),
    "PHILADELPHIA": (-75.17, 39.95, 119, 80),
    "SKATOPIA (Rutland, Ohio)": (-82.0, 39.0, 111, 83),
    "BARCELONA": (2.17, 41.39, 223, 84),
    "BERLIN": (13.40, 52.52, 243, 68),
    "AIRPORT (Tokyo)": (139.7, 35.7, 410, 87),
    "AUSTRALIA (Melbourne)": (144.96, -37.8, 426, 194),
}
MAP_WIDTH = 512


def _fit(pairs):
    n = len(pairs)
    sx, sy = sum(p[0] for p in pairs), sum(p[1] for p in pairs)
    sxx, sxy = sum(p[0] ** 2 for p in pairs), sum(p[0] * p[1] for p in pairs)
    a = (n * sxy - sx * sy) / (n * sxx - sx * sx)
    return a, (sy - a * sx) / n


_AX, _BX = _fit([(v[0], v[2]) for v in THUG2_STARS.values()])
_AY, _BY = _fit([(v[1], v[3]) for v in THUG2_STARS.values()])


def map_position(longitude, latitude):
    # (x, y) on the level select's map for a place.
    gx, gy = _AX * longitude + _BX, _AY * latitude + _BY
    total = cx = cy = 0.0
    for lon, lat, x, y in THUG2_STARS.values():
        d2 = ((longitude - lon) * math.cos(math.radians(latitude))) ** 2 + (latitude - lat) ** 2
        w = 1.0 / (d2 + 4.0)
        cx += w * (x - (_AX * lon + _BX))
        cy += w * (y - (_AY * lat + _BY))
        total += w
    x, y = gx + cx / total, gy + cy / total
    return round(x % MAP_WIDTH, 1), round(min(max(y, 4.0), 252.0), 1)


# --- the sections ------------------------------------------------------------------------------------------------

# A level we add: `text` the sign's name, `level_num` (unique within its section: the star's id is
# globe_dot_<level_num>; none of these have career records, so the info panel skips them), `load` its load script,
# `icon` a level_select_icons.PICTOGRAMS name (a new sign_<sign> is made) or "texture:<name>" (a game texture, e.g.
# "texture:sign_canada"), `sign` the new texture's name, `place` (text, for the notes) and `location`
# ((longitude, latitude), or None: no star, the globe spins).


def level(text, level_num, load, icon, sign, place, location):
    return dict(text=text, level_num=level_num, load=load, icon=icon, sign=sign, place=place, location=location)


# THPS4's levels (tools/thps4_levels' manifest, by its folder), in THPS4's own level select order (a table's
# order is its section's; levels not in it come last): the sign's name as THPS4's own level select has it
# (gamemenu.qb level_select_menu_level_info: Hof is "Chicago"), the place from the levels themselves:
#   - Alcatraz and San Francisco: San Francisco Bay.
#   - College: THPS4's level list calls it "California College"; it has the Coliseum by its campus - the
#     University of Southern California by the Los Angeles Memorial Coliseum.
#   - Kona: Kona Skatepark (its snake run, Street / Vert competitions, skate camp) - Jacksonville, Florida.
#   - Zoo: THPS4 places it in San Diego.
#   - Chicago (Hof; its loading screen loadscrn_chicago) and Motox (Chicago's loading screen and sky).
#   - Shipyard: no city in its scripts but Baltimore (Bucky Lasek's pro challenge there: "Raised near Baltimore's
#     Landsdown Cement Park ... if a ramp is still standing"): Baltimore's harbour.
#   - Carnival: a county fair with a swamp and alligators (CNV_Gap_SwampEscape, Anl_Gator) - no city named: the
#     Louisiana bayou country.
#   - The THPS4 skate shop: no place.
THPS4_LEVELS = {
    "Sch": dict(text="COLLEGE", icon="mortarboard", place="University of Southern California, Los Angeles",
                location=(-118.285, 34.022)),
    "SF2": dict(text="SAN FRANCISCO", icon="bridge", place="San Francisco", location=(-122.418, 37.775)),
    "Alc": dict(text="ALCATRAZ", icon="bars", place="Alcatraz Island, San Francisco", location=(-122.423, 37.827)),
    "Kon": dict(text="KONA", icon="palm", place="Kona Skatepark, Jacksonville, Florida", location=(-81.66, 30.33)),
    "Jnk": dict(text="SHIPYARD", icon="anchor", place="Baltimore harbour (inferred)", location=(-76.61, 39.27)),
    "Lon": dict(text="LONDON", icon="bigben", place="London", location=(-0.128, 51.507)),
    "Zoo": dict(text="ZOO", icon="paw", place="San Diego Zoo", location=(-117.149, 32.735)),
    "Cnv": dict(text="CARNIVAL", icon="ferris", place="a Louisiana bayou county fair (inferred)",
                location=(-91.5, 30.0)),
    "Hof": dict(text="CHICAGO", icon="skyline", place="Chicago", location=(-87.63, 41.88)),
    "SkateShop": dict(text="SKATE SHOP", icon="skateboard", place="THPS4's skate shop (no place)", location=None),
    "Motox": dict(text="MOTOX", icon="motorbike", place="Chicago (its loading screen and sky)",
                  location=(-87.65, 41.92)),
}

# THUG1's levels (its conversion's manifest, <game>\thug2-decomp-thug1\levels.json, by folder): signs only - the
# sign text comes from the manifest's display name, the place from its location text (PLACES). Classic levels
# that THUG2 has signs for keep THUG2's.
THUG1_LEVELS = {
    "NJ": dict(icon="house"),
    "NY": dict(icon="torch"),
    "FL": dict(icon="palm"),
    "SD": dict(icon="sun"),
    "HI": dict(icon="wave"),
    "VC": dict(icon="texture:sign_canada"),
    "SJ": dict(icon="ramp"),
    "RU": dict(icon="dome"),
    "SE": dict(icon="flame"),
    "SC": dict(icon="texture:sign_school"),
    "DJ": dict(icon="texture:sign_jam"),
    "PH": dict(icon="texture:sign_philly"),
    "VN": dict(icon="palm"),
    "HN": dict(icon="texture:sign_airport"),
    "SC2": dict(icon="texture:sign_school"),
    "NJ_Skateshop": dict(icon="skateboard"),
}

# Places a manifest's location text may name (the first found, longest names first), as (longitude, latitude).
# A text naming none of them (a skate shop, Hell) gets no star.
PLACES = {
    "Alcatraz": (-122.423, 37.827), "San Francisco": (-122.418, 37.775), "Los Angeles": (-118.24, 34.05),
    "Venice Beach": (-118.47, 33.99), "Southern California": (-117.92, 33.84), "San Diego": (-117.16, 32.72),
    "Phoenix": (-112.07, 33.45), "Montana": (-110.36, 46.88), "Vancouver": (-123.12, 49.28),
    "Hawaii": (-157.86, 21.31), "Chicago": (-87.63, 41.88), "New Orleans": (-90.07, 29.95),
    "Tampa": (-82.46, 27.95), "Miami": (-80.19, 25.76), "Jacksonville": (-81.66, 30.33),
    "Baltimore": (-76.61, 39.27), "Philadelphia": (-75.17, 39.95), "New Jersey": (-74.41, 40.06),
    "Manhattan": (-73.97, 40.78), "New York": (-74.0, 40.71), "Boston": (-71.06, 42.36),
    "London": (-0.128, 51.507), "Barcelona": (2.17, 41.39), "Berlin": (13.40, 52.52), "Moscow": (37.62, 55.75),
    "Tokyo": (139.7, 35.7), "Melbourne": (144.96, -37.8),
}


def find_place(text):
    for name in sorted(PLACES, key=len, reverse=True):
        if name.lower() in (text or "").lower():
            return PLACES[name]
    return None


def manifest_levels(manifest, table, prefix):
    # A converted game's levels from its manifest (as tools/thps4_levels writes it: per level folder, level (the
    # load script is Load_<level>), display, level_num, location (text) and icon (a THUG2 sign to use)), with
    # `table`'s entries (by folder: text / icon / place / location) taking precedence.
    out = []
    keys = list(table)
    entries = sorted(manifest["levels"],
                     key=lambda e: keys.index(e.get("folder")) if e.get("folder") in keys else len(keys))
    for entry in entries:
        mine = table.get(entry.get("folder"), {})
        text = mine.get("text") or re.sub(r"^%s " % re.escape(prefix), "", entry["display"])
        icon = mine.get("icon") or ("texture:" + entry["icon"] if entry.get("icon") else "skateboard")
        place = mine.get("place") or entry.get("location") or ""
        location = mine["location"] if "location" in mine else find_place(entry.get("location"))
        out.append(level(text.replace('"', ""), entry["level_num"], "Load_" + entry["level"], icon,
                         "sign_" + entry["level"].lower(), place, location))
    return out


# THUG2's own list (level_select_menu_level_info) with what make_menu_level adds to it: SKATE SHOP and TEST LEVEL
# get signs of their own and no star (they were TRAINING's sign and star).
THUG2_EXTRA = {
    "SKATE SHOP": dict(icon="skateboard", sign="sign_skateshop", place="THUG2's skate shop (no place)"),
    "TEST LEVEL": dict(icon="cone", sign="sign_testlevel", place="Neversoft's test level (no place)"),
}


def sections(thps4=None, thug1=None):
    # [(id, title, array name, levels we add to it or list)] in Up / Down order: newest game first. THUG2's array is
    # the game's own; the others are new arrays of our levels.
    out = [("thug2", "TONY HAWK'S UNDERGROUND 2", "level_select_menu_level_info", None)]
    if thug1:
        out.append(("thug1", "TONY HAWK'S UNDERGROUND", "level_select_menu_thug1_level_info",
                    manifest_levels(thug1, THUG1_LEVELS, "THUG1")))
    if thps4:
        out.append(("thps4", "TONY HAWK'S PRO SKATER 4", "level_select_menu_thps4_level_info",
                    manifest_levels(thps4, THPS4_LEVELS, "THPS4")))
    return out


# --- gamemenu_levelselect.qb -------------------------------------------------------------------------------------

# The section header: the title centred above the row of signs, an up arrow left of it and a down arrow right of it
# (the menus' up_arrow / down_arrow), on the 640 x 480 menu layout.
HEADER_POS = (320.0, 16.0)
HEADER_SCALE = 1.0
HEADER_ARROW_DX = 125.0


def _indent(block, spaces=4):
    return "".join(" " * spaces + line + "\n" for line in block.splitlines())


def _replace_once(text, old, new, what):
    if text.count(old) != 1:
        sys.exit("gamemenu_levelselect.qb: %s found %d times" % (what, text.count(old)))
    return text.replace(old, new, 1)


def sign_entry(lv):
    texture = lv["icon"][len("texture:"):] if lv["icon"].startswith("texture:") else lv["sign"]
    dot = " dot_pos=(%.1f, %.1f)" % map_position(*lv["location"]) if lv["location"] else ""
    return ('    {text="%s" level_num=%d num_goals=0 flag=LEVEL_UNLOCKED_TR level=%s sign_texture=%s%s} \n'
            % (lv["text"], lv["level_num"], lv["load"], texture, dot))


def edit_level_select(text, section_list):
    # `text`: gamemenu_levelselect.qb as NeverScript text, after make_menu_level.add_skateshop_sign (without THPS4).
    count = len(section_list)

    # THUG2's added entries: their own signs, no star.
    for name, extra in THUG2_EXTRA.items():
        entry = re.search(r'\{text="%s" [^\n]*\}' % name, text)
        if not entry:
            sys.exit("gamemenu_levelselect.qb: the %s entry not found" % name)
        new = re.sub(r"sign_texture=\w+", "sign_texture=" + extra["sign"], entry.group(0))
        new = re.sub(r" dot_pos=\([^)]*\)", "", new)
        text = text.replace(entry.group(0), new, 1)
    # TRAINING out of its one-item column (see the top).
    training = re.search(r'\{text="TRAINING" level_num=LevelNum_TR [^\n]* new_vmenu\}', text)
    if not training:
        sys.exit("gamemenu_levelselect.qb: TRAINING's entry not as expected")
    text = text.replace(training.group(0), training.group(0).replace(" new_vmenu}", "}"), 1)

    # The sections' arrays (after THUG2's) and the scripts.
    arrays = ""
    numbers = []
    for _, _, array, levels in section_list[1:]:
        arrays += "%s = [\n%s] \n" % (array, "".join(sign_entry(lv) for lv in levels))
        numbers += [lv["level_num"] for lv in levels]
    get_list = ["script level_select_get_section_list {",
                "    if ((level_select_section < 0) or (level_select_section > %d)) {" % (count - 1),
                "        change level_select_section=0 ",
                "    } "]
    for n, (_, title, array, _) in enumerate(section_list):
        indent = "    " if n == 0 else "        "
        if n:
            get_list.append("    if (level_select_section = %d) {" % n)
        get_list += [indent + "level_list = %s " % array, indent + 'section_title = "%s" ' % title]
        if n:
            get_list.append("    } ")
    get_list += ["    return level_list=<level_list> section_title=<section_title> section_count=%d " % count, "} "]
    scripts = "\n".join([
        "level_select_section = 0 ",
        "\n".join(get_list),
        "script level_select_section_change {",
        "    if ! ScreenElementExists id=current_menu {",
        "        return ",
        "    } ",
        "    new_section = (level_select_section + <dir>) ",
        "    if (<new_section> < 0) {",
        "        new_section = %d " % (count - 1),
        "    } ",
        "    if (<new_section> > %d) {" % (count - 1),
        "        new_section = 0 ",
        "    } ",
        "    if (<new_section> = level_select_section) {",
        "        return ",
        "    } ",
        "    change level_select_section=<new_section> ",
        "    if (<dir> < 0) {",
        "        generic_menu_up_or_down_sound up ",
        "    } else {",
        "        generic_menu_up_or_down_sound down ",
        "    } ",
        "    kill_level_select_scripts ",
        "    if ScreenElementExists id=level_select_anchor {",
        "        DestroyScreenElement id=level_select_anchor ",
        "    } ",
        "    create_level_select_menu <...> keep_globe ",
        "} ",
        "script level_select_build_section_header {",
        "    if ! ScreenElementExists id=level_select_anchor {",
        "        return ",
        "    } ",
        "    SetScreenElementLock id=level_select_anchor off ",
        "    CreateScreenElement {",
        "        type=TextElement ",
        "        parent=level_select_anchor ",
        "        id=level_select_section_header ",
        "        text=<section_title> ",
        "        font=small ",
        "        rgba=[128 128 128 128] ",
        "        scale=%.2f " % HEADER_SCALE,
        "        pos=(%.1f, %.1f) " % HEADER_POS,
        "        just=[center center] ",
        "        z_priority=5 ",
        "        shadow ",
        "        shadow_rgba=UI_text_shadow_color ",
        "        shadow_offs=UI_text_shadow_offset ",
        "    } ",
        "    if (<section_count> > 1) {",
        "        Theme_GetMenuTitleColor return_value=arrow_color ",
        "        CreateScreenElement {",
        "            type=SpriteElement ",
        "            parent=level_select_anchor ",
        "            texture=up_arrow ",
        "            rgba=<arrow_color> ",
        "            scale=1.0 ",
        "            pos=(%.1f, %.1f) " % (HEADER_POS[0] - HEADER_ARROW_DX, HEADER_POS[1]),
        "            just=[center center] ",
        "            z_priority=5 ",
        "        } ",
        "        CreateScreenElement {",
        "            type=SpriteElement ",
        "            parent=level_select_anchor ",
        "            texture=down_arrow ",
        "            rgba=<arrow_color> ",
        "            scale=1.0 ",
        "            pos=(%.1f, %.1f) " % (HEADER_POS[0] + HEADER_ARROW_DX, HEADER_POS[1]),
        "            just=[center center] ",
        "            z_priority=5 ",
        "        } ",
        "    } ",
        "} ",
        "script level_select_choice_dots {",
        "    GetArraySize <dots_list> ",
        "    index = 0 ",
        "    while {",
        "        GetDotNameFromNum num=(<dots_list> [<index>].level_num) ",
        "        if ScreenElementExists id=<dot_name> {",
        "            if ((<dots_list> [<index>].level_num) = <level_num>) {",
        "                DoScreenElementMorph id=<dot_name> alpha=1 time=0 ",
        "                DoScreenElementMorph id=<dot_name> alpha=0 scale=4 relative_scale time=0.15 ",
        "            } else {",
        "                DoScreenElementMorph id=<dot_name> scale=0 time=0.15 ",
        "            } ",
        "        } ",
        "        index = (<index> + 1) ",
        "    } <array_size> ",
        "} ",
        ""])
    anchor = "\nlevel_select_menu_level_info = ["
    text = _replace_once(text, anchor, "\n" + scripts + arrays + anchor[1:], "level_select_menu_level_info")

    # create_level_select_menu: the section's list; the globe kept turning on a section change; the dots worked out
    # again; Up / Down, the header and the helper text outside classic mode.
    text = _replace_once(text, "    } else {\n        level_list = level_select_menu_level_info \n    } \n"
                               "    change globe_pos=256.0 \n    change globe_target_x=89.0 \n",
                         "    } else {\n        level_select_get_section_list \n    } \n"
                         "    if ! GotParam keep_globe {\n        change globe_pos=256.0 \n"
                         "        change globe_target_x=89.0 \n    } \n",
                         "create_level_select_menu's level list")
    text = _replace_once(text, "    level_select_build_map level_list=<level_list> \n",
                         "    change level_select_last_dot_count=-1 \n"
                         "    level_select_build_map level_list=<level_list> \n", "level_select_build_map's call")
    helper_old = ('    create_helper_text helper_text_elements=[{text="\\\\b6/\\\\b5 = Choose Level"} \n'
                  '        {text="\\\\bm = Select"} \n        {text="\\\\bn = Back"} \n    ] \n')
    helper_sections = helper_old.replace('[{text="\\\\b6/\\\\b5', '[{text="\\\\b7/\\\\b4 = Game"} \n'
                                         '        {text="\\\\b6/\\\\b5', 1) if count > 1 else helper_old
    helper_new = ("    if GameModeEquals is_classic {\n" + _indent(helper_old) + "    } else {\n"
                  "        SetScreenElementProps {\n"
                  "            id=current_menu \n"
                  "            event_handlers=[{pad_up level_select_section_change params={<...> dir=-1}} \n"
                  "                {pad_down level_select_section_change params={<...> dir=1}} \n"
                  "            ] \n"
                  "        } \n"
                  "        level_select_build_section_header section_title=<section_title> section_count=<section_count> \n"
                  + _indent(helper_sections) + "    } \n")
    text = _replace_once(text, helper_old, helper_new, "create_level_select_menu's helper text")

    # level_select_valid_choice: the dot animation over the section's list.
    loop = re.search(r"        GetArraySize level_select_menu_level_info \n        index = 0 \n        while \{\n"
                     r"            GetDotNameFromNum num=\(level_select_menu_level_info \[<index>\]\.level_num\) \n"
                     r".*?\n        \} <array_size> \n", text, re.S)
    if not loop or "level_select_valid_choice" not in text[text.rindex("\nscript ", 0, loop.start()):loop.start()]:
        sys.exit("gamemenu_levelselect.qb: level_select_valid_choice's dot loop not as expected")
    text = text.replace(loop.group(0),
                        "        if GameModeEquals is_classic {\n"
                        "            level_select_choice_dots dots_list=level_select_menu_classic_level_info level_num=<level_num> \n"
                        "        } else {\n"
                        "            level_select_get_section_list \n"
                        "            level_select_choice_dots dots_list=<level_list> level_num=<level_num> \n"
                        "        } \n", 1)

    # The info panel's gaps and high score: no records for our levels (as make_menu_level does for 18 / 21).
    for script in ("level_select_info_add_gaps", "level_select_info_add_highscore"):
        head = "script %s {\n    if (<level_num> = <created_park_number>) {\n        return \n    } \n" % script
        # (the numbers this script skips already - its own body only: the first script's skips are in `text` by the
        # time the second is done; looking in all of `text` left the high score's out: GetLevelRecords crashed)
        at = text.find(head)
        body = text[at:text.find("\nscript ", at + 1)] if at >= 0 else ""
        skips = "".join("    if (<level_num> = %d) {\n        return \n    } \n" % n
                        for n in sorted(set(numbers)) if "(<level_num> = %d)" % n not in body)
        text = _replace_once(text, head, head + skips, script + "'s start")
    return text


# --- sprites.qb and the signs ------------------------------------------------------------------------------------

def new_signs(section_list):
    # {texture name: pictogram} for every sign we make.
    signs = {extra["sign"]: extra["icon"] for extra in THUG2_EXTRA.values()}
    for _, _, _, levels in section_list[1:]:
        for lv in levels:
            if not lv["icon"].startswith("texture:"):
                signs[lv["sign"]] = lv["icon"]
    return signs


def edit_sprites(text, section_list):
    last = '        "LevelSelectSprites/st_secret" \n'
    names = "".join('        "LevelSelectSprites/%s" \n' % name for name in sorted(new_signs(section_list)))
    if text.count(last) != 1:
        sys.exit("sprites.qb: the level select's texture list not as expected")
    return text.replace(last, last + names, 1)


def write_signs(frame_data, section_list, overlay):
    folder = os.path.join(overlay, "images", "LevelSelectSprites")
    os.makedirs(folder, exist_ok=True)
    signs = new_signs(section_list)
    for name, pictogram in sorted(signs.items()):
        with open(os.path.join(folder, name + ".img.xbx"), "wb") as f:
            f.write(level_select_icons.make_sign(frame_data, pictogram))
    return sorted(signs)


def describe(section_list):
    # The sections, their levels, signs and stars (for the tool's output).
    lines = []
    for _, title, _, levels in section_list:
        lines.append("level select section: %s" % title)
        for lv in levels or []:
            star = "star at %s" % (map_position(*lv["location"]),) if lv["location"] else "no star (spins)"
            lines.append("    %-16s sign %-12s %s - %s" % (lv["text"], lv["icon"], star, lv["place"]))
    return "\n".join(lines)
