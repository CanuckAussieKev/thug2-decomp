# NOT ORIGINAL - the data overlay (see src/game/front/data_overlay.h): generates, from the game's own files, a
# folder the DLL reads before the game's archives and data folder:
#   - always used: the skate shop as a playable level (Level_SkateShopLevel, in the level select as "SKATE SHOP"),
#     TestLevel in the level select, the pause menu's LEVEL EDITOR, the THPS4 levels;
#   - front_end\, used first while the skate-shop front end is on (thug2-decomp.ini [features] menu_backdrop, any
#     value but 0 - src/game/front/menu_backdrop.h): the standard main menu.
#
#   python -I tools/menu_level/make_menu_level.py [--game C:\Games\THUG2] [--out <folder>]
#
# The overlay (default: <game>\thug2-decomp-data; emptied and made again each run) gets:
#   scripts\game\Levels.qb     the game's + Load_SkateShopLevel / Level_SkateShopLevel (the shop's scene, collision and its own
#                              node array; level number LevelNum_Default, as the game's test levels).
#   levels\skateshop\skateshop.qb  the shop's node array (skateshop_NodeArray) without the front end's parts: the
#                              shadow skater's trigger script and the menus' 3D backgrounds.
#   levels\TestLevel\TestLevel.qb  the developers' test level's nodes (path followers kept out of net games).
#   scripts\game\menu\gamemenu_levelselect.qb  the level select with SKATE SHOP and TEST LEVEL signs, in sections per
#                              game (THUG2, THUG1, THPS4) with stars and signs for our levels (level_select.py);
#                              scripts\game\menu\sprites.qb and images\LevelSelectSprites\sign_*.img.xbx for those signs.
#   scripts\game\menu\gamemenu_pause.qb  the pause menu with LEVEL EDITOR (src/game/front/level_editor.h) and its
#                              scripts level_editor_enter / level_editor_exit.
#   levels\thps4_*\, thug1_*\  the THPS4 / THUG1 levels converted by tools/thps4_levels / tools/thug1_levels, when
#                              they have made <game>\thug2-decomp-thps4 / -thug1: copied in, their level structures
#                              added to both Levels.qb and their signs to the level select's THUG1 / THPS4 sections
#                              (tools/level_port/ported_levels.py).
#   front_end\levels\mainmenu\mainmenu_scripts.qb  (when the THPS4 skate shop is among them: the front end's backdrop)
#                              the front end's scripts with create_main_menu building a standard vertical menu (a
#                              roundbar menu, as Game Options and Create-A-Modes) instead of the spinning wheel - the
#                              same items doing the same things, after SKATE NOW (the menu slides away and the skater
#                              drops in on the outside halfpipe, as THPS4's practice; Esc brings the menu back); the shop's collision loaded at the
#                              start; the skater standing idle.
#   front_end\levels\mainmenu\mainmenu.qb  the front end's nodes with the skater's restarts moved into THPS4's shop.
# Nothing of the game is stored in the repo: the files are made from the game folder each time.
# Needs NeverScript (tools/build_neverscript.ps1 -> out/neverscript/ns.exe).

import argparse
import math
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(REPO, "tools", "level_port"))
import ported_levels  # noqa: E402 (the converted THPS4 / THUG1 levels: added when <game>\thug2-decomp-thps4 / -thug1 exist)
import thug2_col  # noqa: E402 (the front end's collision without THUG2's shop)
import nodes as level_port_nodes  # noqa: E402 (the terrain sounds a level loads)
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import level_select  # noqa: E402 (the level select's sections, stars and signs)

def checksum(name):
    return (zlib.crc32(name.lower().encode()) ^ 0xFFFFFFFF) & 0xFFFFFFFF


def lzss_decode(data, size):
    # src/game/util/lzss.cpp Decode: a 4096-byte window filled with spaces, writing from 4096 - 18.
    window = bytearray(b" " * 4096)
    r = 4096 - 18
    out = bytearray()
    i = 0
    flags = 0
    while len(out) < size and i < len(data):
        flags >>= 1
        if not flags & 0x100:
            flags = data[i] | 0xFF00
            i += 1
        if flags & 1:
            if i >= len(data):
                break
            c = data[i]
            i += 1
            out.append(c)
            window[r] = c
            r = (r + 1) & 4095
        else:
            if i + 1 >= len(data):
                break
            position = data[i] | ((data[i + 1] & 0xF0) << 4)
            last = (data[i + 1] & 0x0F) + 2
            i += 2
            for k in range(last + 1):
                c = window[(position + k) & 4095]
                out.append(c)
                window[r] = c
                r = (r + 1) & 4095
    return bytes(out[:size])


def prx_file(path, name):
    # An archive as stored: {total size, version, file count}, then per file {size, compressed size, name size
    # (int16), use count, checksum}, its name, and its data padded to 4 bytes (src/game/file/pip.cpp NextRawEntry).
    data = open(path, "rb").read()
    _, _, count = struct.unpack_from("<iii", data, 0)
    offset = 12
    wanted = checksum(name)
    for _ in range(count):
        size, compressed, name_size, _, file_checksum = struct.unpack_from("<iihHI", data, offset)
        stored = compressed or size
        body = offset + 16 + name_size
        entry_name = data[offset + 16:body].split(b"\0")[0].decode("ascii", "replace")
        if file_checksum == wanted or entry_name.lower().replace("/", "\\") == name.lower():
            raw = data[body:body + stored]
            return lzss_decode(raw, size) if compressed else raw
        offset = body + ((stored + 3) & ~3)
    sys.exit("%s: %s not found" % (path, name))


MARKER = "made-by-make_menu_level.txt"
# The level numbers of the SKATE SHOP and TEST LEVEL signs (unused / the test level's slot: their records, unlock and
# dot lookups take them; each sign needs its own - the level select tells signs apart by it).
SKATESHOP_SIGN_LEVEL = 18
TESTLEVEL_SIGN_LEVEL = 21

# --- the playable skate shop ------------------------------------------------------------------------------------

def block(text, name):
    # The text of a top-level `name = { ... }` structure.
    start = text.index("\n%s = {" % name) + 1
    return text[start:text.index("\n}", start) + 2]


def add_skateshop_level(text):
    # (Not Load_SkateShop: CheckForInvalidLevels turns the obsolete load_skateshop into the main menu.)
    # A gameplay level made of the front end's skate shop: its scene, collision and archives, its own node array
    # (skateshop.qb), the front end's lighting; LevelNum_Default as the game's test levels (not a career level).
    mainmenu = block(text, "level_Mainmenu")
    lighting = mainmenu[mainmenu.index("    ambient_red="):mainmenu.rindex("}")]
    level = ('script Load_SkateShopLevel {\n    load_level Level_SkateShopLevel \n} \n'
             'Level_SkateShopLevel = {\n'
             '    structure_name=Level_SkateShopLevel \n'
             '    load_script=Load_SkateShopLevel \n'
             '    name="Skate Shop" \n'
             '    loading_screen="loadscrn_generic" \n'
             '    loading_time=6.75 \n'
             '    COIM_size=614400 \n'
             '    level_name="skateshop" \n'
             '    pre="mainmenu_scripts.pre" \n'
             '    scnpre="mainmenuscn.pre" \n'
             '    level="mainmenu" \n'
             '    qb="levels\\\\skateshop\\\\skateshop.qb" \n'
             '    colpre="mainmenucol.pre" \n'
             '    default_stats=5 \n'
             '    default_stats_classic=7 \n'
             '    level_number=LevelNum_Default \n'
             '    skip_teammate_pro \n' + lighting + '} \n')
    anchor = "\nscript Load_TestLevel {"
    if text.count(anchor) != 1:
        sys.exit("Levels.qb: Load_TestLevel not found once")
    return text.replace(anchor, "\n" + level + anchor[1:], 1)


# Front-end-only parts of the shop's nodes: the shadow skater's trigger (its script runs the options menu's skater)
# and the menus' 3D backgrounds.
SHOP_FRONT_END_TRIGGERS = ("TriggerScript=TRGP_shadowtriggerScript",)
SHOP_FRONT_END_OBJECTS = ("options_clousd_trigger", "ui_bg_trigger")


def skateshop_nodes(text):
    # The shop's node array as skateshop.qb's (its array is named after the file).
    text = text.replace("mainmenu_NodeArray =", "skateshop_NodeArray =", 1)
    for trigger in SHOP_FRONT_END_TRIGGERS:
        if text.count(trigger) != 1:
            sys.exit("mainmenu.qb: %s found %d times" % (trigger, text.count(trigger)))
        text = text.replace(trigger, "", 1)
    elements, _, _ = array_elements(text, "skateshop_NodeArray")
    for element in elements:
        name = re.search(r"\bName=(\w+)", element)
        if name and name.group(1) in SHOP_FRONT_END_OBJECTS and "CreatedAtStart" in element:
            text = text.replace(element, element.replace("CreatedAtStart", "", 1), 1)
    # The skater starts at the first player restart: the shop's SS_player_restart (y -375, below the shop - the front
    # end always moves the skater to TRG_Playerrestart by script) comes first. The two swap places (nothing links to
    # nodes by index).
    elements, _, _ = array_elements(text, "skateshop_NodeArray")
    named = {re.search(r"\bName=(\w+)", e).group(1): e for e in elements if re.search(r"\bName=(\w+)", e)}
    first, wanted = named.get("SS_player_restart"), named.get("TRG_Playerrestart")
    if not first or not wanted or text.index(first) > text.index(wanted):
        sys.exit("mainmenu.qb: the shop's restarts not as expected")
    text = text.replace(wanted, "\0", 1).replace(first, wanted, 1).replace("\0", first, 1)
    return text


def add_skateshop_sign(text, thps4=None):
    # The level select's SKATE SHOP sign, after TRAINING (its unlock flag and sign; level 21's records, the test
    # level's slot - LevelNum_Default is shared); the converted THPS4 levels' signs after it (tools/thps4_levels).
    training = re.search(r'\n( *)\{text="TRAINING" level_num=LevelNum_TR level_prefix="TR"[^\n]*\n', text)
    if not training:
        sys.exit("gamemenu_levelselect.qb: the TRAINING entry not found")
    sign = ('%s{text="SKATE SHOP" level_num=%d num_goals=0 flag=LEVEL_UNLOCKED_TR level=Load_SkateShopLevel '
            'sign_texture=sign_training dot_pos=(89.0, 82.0)} \n' % (training.group(1), SKATESHOP_SIGN_LEVEL))
    extra_numbers = []
    if thps4:
        sign += ported_levels.signs_text(thps4, training.group(1))
        extra_numbers = ported_levels.sign_numbers(thps4)
    text = text[:training.end()] + sign + text[training.end():]
    # The developers' TestLevel (all its data ships: scene, collision, peds, scripts) in the list too: its entry was
    # DEVKIT_ONLY (dropped: CD is always true) and had no sign.
    test = re.search(r'\{text="TestLevel" level_num=21 [^\n]*DEVKIT_ONLY[^\n]*\}', text)
    if not test:
        sys.exit("gamemenu_levelselect.qb: the TestLevel entry not found")
    text = text.replace(test.group(0), '{text="TEST LEVEL" level_num=21 num_goals=0 flag=LEVEL_UNLOCKED_TR '
                        'level=load_TestLevel sign_texture=sign_training dot_pos=(89.0, 82.0)}', 1)
    # The info panel's gaps and high score: levels 18 and 21 have no records (GetLevelGapTotals reads through a null
    # for them): skipped, as the created park's are.
    for script in ("level_select_info_add_gaps", "level_select_info_add_highscore"):
        head = "script %s {\n    if (<level_num> = <created_park_number>) {\n        return \n    } \n" % script
        if text.count(head) != 1:
            sys.exit("gamemenu_levelselect.qb: %s's start not as expected" % script)
        skips = "".join("    if (<level_num> = %d) {\n        return \n    } \n" % n
                        for n in [SKATESHOP_SIGN_LEVEL, TESTLEVEL_SIGN_LEVEL] + extra_numbers)
        text = text.replace(head, head + skips, 1)
    return text


def testlevel_nodes(text):
    # TestLevel in network games: load_level makes no path memory there (AllocatePathManMemory only outside them), and
    # an object following a path crashes (0x00469620 reads through the missing path manager). The shipped levels keep
    # such objects out of network games (AbsentInNetGames); the developers' test level didn't for Obj_BeachBall07
    # (Obj_FollowPathLinked). Every node whose trigger script follows a path gets AbsentInNetGames.
    followers = set(re.findall(r"\nscript (\w+) \{[^}]*Obj_FollowPath", text))
    elements, _, _ = array_elements(text, "TestLevel_NodeArray")
    marked = 0
    for element in elements:
        script = re.search(r"\bTriggerScript=(\w+)", element)
        if script and script.group(1) in followers and "AbsentInNetGames" not in element:
            text = text.replace(element, element.replace("CreatedAtStart", "CreatedAtStart AbsentInNetGames", 1), 1)
            marked += 1
    print("TestLevel: %d path-following node(s) kept out of network games" % marked)
    return text


# --- the level editor (src/game/front/level_editor.h) -------------------------------------------------------------

LEVEL_EDITOR_SCRIPTS = """script level_editor_enter {
    if ! LevelEditor_Start {
        return
    }
    if ObjectExists id=current_menu_anchor {
        DestroyScreenElement id=current_menu_anchor
    }
    if ObjectExists id=helper_text_anchor {
        DestroyScreenElement id=helper_text_anchor
    }
    pause_menu_gradient off
    menu3d_hide
    PauseSkaters
    HideSkaterAndMiscSkaterEffects
    SetScreenElementProps {
        id=root_window
        replace_handlers
        event_handlers=[{pad_start level_editor_exit}]
    }
    SetScreenElementProps {
        id=root_window
        replace_handlers
        event_handlers=[{pad_select null_script}]
    }
    level_editor_hud_create
}
script level_editor_exit {
    level_editor_hud_destroy
    LevelEditor_Stop
    Skater:AddSkaterToWorld
    create_pause_menu
}
script level_editor_hud_create {
    level_editor_hud_destroy
    SetScreenElementLock id=root_window off
    CreateScreenElement {
        type=ContainerElement
        parent=root_window
        id=level_editor_hud
        pos=(320.0, 218.0)
        dims=(640.0, 480.0)
    }
    CreateScreenElement {
        type=SpriteElement
        parent=level_editor_hud
        texture=white2
        pos=(6.0, 6.0)
        just=[left top]
        rgba=[2 0 1 90]
        scale=(118.0, 6.9)
        z_priority=990
    }
    CreateScreenElement {
        type=SpriteElement
        parent=level_editor_hud
        texture=white3
        scale=(11.8, 0.03)
        rgba=[128 110 0 90]
        pos=(6.0, 20.0)
        just=[left top]
        z_priority=991
    }
    CreateScreenElement {
        type=SpriteElement
        parent=level_editor_hud
        texture=white2
        pos=(320.0, 480.0)
        just=[center bottom]
        rgba=[2 0 1 90]
        scale=(200.5, 3.8)
        z_priority=990
    }
    CreateScreenElement {
        type=SpriteElement
        parent=level_editor_hud
        texture=white3
        scale=(40.0, 0.03)
        rgba=[13 56 82 80]
        pos=(320.0, 450.0)
        z_priority=991
    }
    <index> = 1
    while {
        FormatText ChecksumName=line_id "level_editor_hud_%i" i=<index>
        CreateScreenElement {
            type=TextElement
            parent=level_editor_hud
            id=<line_id>
            font=small
            text=""
            pos=((14.0, 0.0) + (0.0, 11.0) * <index>)
            just=[left top]
            scale=0.56
            rgba=[118 118 118 128]
            z_priority=1000
        }
        <index> = (<index> + 1)
    } 4
    SetScreenElementProps id=level_editor_hud_1 rgba=[128 110 0 128] scale=0.62 pos=(14.0, 9.0)
    SetScreenElementProps id=level_editor_hud_3 rgba=[96 100 104 128]
    SetScreenElementProps id=level_editor_hud_4 rgba=[80 90 100 128]
    CreateScreenElement {
        type=TextElement
        parent=level_editor_hud
        id=level_editor_hud_5
        font=small
        text=""
        pos=(14.0, 455.0)
        just=[left top]
        scale=0.55
        rgba=[104 104 104 128]
        z_priority=1000
    }
    CreateScreenElement {
        type=TextElement
        parent=level_editor_hud
        id=level_editor_hud_6
        font=small
        text=""
        pos=(14.0, 466.0)
        just=[left top]
        scale=0.55
        rgba=[104 104 104 128]
        z_priority=1000
    }
    CreateScreenElement {
        type=TextElement
        parent=level_editor_hud
        id=level_editor_hud_7
        font=small
        text=""
        pos=(320.0, 92.0)
        just=[center top]
        scale=0.8
        rgba=[128 120 64 128]
        z_priority=1000
    }
}
script level_editor_hud_set {
    if ObjectExists id=<id> {
        SetScreenElementProps id=<id> text=<text>
    }
}
script level_editor_hud_destroy {
    level_editor_panel_hide
    if ObjectExists id=level_editor_hud {
        DestroyScreenElement id=level_editor_hud
    }
}
script level_editor_panel_show {
    level_editor_panel_hide
    if ! ObjectExists id=level_editor_hud {
        return
    }
    CreateScreenElement {
        type=ContainerElement
        parent=level_editor_hud
        id=level_editor_panel
        pos=(320.0, 240.0)
        dims=(640.0, 480.0)
    }
    CreateScreenElement {
        type=SpriteElement
        parent=level_editor_panel
        texture=white2
        pos=<pos>
        just=[left top]
        rgba=[2 0 1 104]
        scale=<scale>
        z_priority=992
    }
    <index> = 0
    while {
        FormatText ChecksumName=line_id "level_editor_panel_%i" i=<index>
        CreateScreenElement {
            type=TextElement
            parent=level_editor_panel
            id=<line_id>
            font=small
            text=""
            pos=(<pos> + (8.0, 6.0) + (0.0, 12.5) * <index>)
            just=[left top]
            scale=0.56
            rgba=[118 118 118 128]
            z_priority=1001
        }
        <index> = (<index> + 1)
    } <lines>
    SetScreenElementProps id=level_editor_panel_0 rgba=[128 110 0 128]
}
script level_editor_panel_hide {
    if ObjectExists id=level_editor_panel {
        DestroyScreenElement id=level_editor_panel
    }
}
"""


def add_level_editor(text):
    # The pause menu's LEVEL EDITOR item (after YOUR OPTIONS: single player only - net games don't get that item, the
    # park editor's own pause menu returns earlier) while LevelEditor_Available (the [features] level_editor setting,
    # in a level), and the two scripts. level_editor_enter takes the pause menu down as the goal editor's
    # my_exit_pause_menu does (the game stays paused: no UnpauseGame), the skater out of the world as its edit_goal
    # does (PauseSkaters, HideSkaterAndMiscSkaterEffects), and Start / Select over to the editor (Start leaves it).
    # level_editor_exit brings the skater back and the pause menu again (its CONTINUE unpauses as ever).
    options = re.search(r'\n( *)add_roundbar_menu_item text="YOUR OPTIONS" id=menu_your_options '
                        r'pad_choose_script=create_your_options_menu \n', text)
    if not options or text.count('text="YOUR OPTIONS"') != 1:
        sys.exit("gamemenu_pause.qb: the YOUR OPTIONS item not found once")
    indent = options.group(1)
    item = ('%sif LevelEditor_Available {\n%s    add_roundbar_menu_item text="LEVEL EDITOR" id=menu_level_editor '
            'pad_choose_script=level_editor_enter \n%s} \n' % (indent, indent, indent))
    text = text[:options.end()] + item + text[options.end():]
    register = text.find("\n__register_checksums__")
    at = register + 1 if register >= 0 else len(text)
    return text[:at] + LEVEL_EDITOR_SCRIPTS + text[at:]


# --- the standard main menu (src/game/front/menu_backdrop.h) ------------------------------------------------------

# The front end's backdrop: the THPS4 skate shop (tools/thps4_levels), shown behind the menus by the DLL.
BACKDROP_LEVEL = "thps4_skateshop"

# create_main_menu as the game's (mainmenu_scripts.qb), but a roundbar menu (make_new_roundbar_menu /
# add_roundbar_menu_item, as Game Options and Create-A-Modes) where it made the spinning wheel (make_spin_menu: a
# turned VMenu with filler items, a copy of the items, the highlight bar and icon, the wheel's level objects turned
# with it). The menu's ids stay main_menu / main_vmenu (scripts find main_vmenu); its place on screen is
# main_menu_list_pos, the 640 x 480 layout's top left of the items, on a dark backing (main_menu_backing: the shop's
# wall of decks behind made the items hard to read): centred, above the THPS4 shop's counter in the
# held view (its glass counter's top - the wheels and decks on it, ~y 20 - projects to screen y ~300; worked out
# from the node positions, not yet seen in the game). The skater, its camera (mainmenu_camera02 - the
# DLL holds the view while it plays), the wheel's objects (left as the game shows them: in THUG2's shop, out of
# sight), the attract mode timer, the second controller check and the rest are as the game's.
# The items are add_main_menu_items' (the same conditions, ids, scripts and parameters), each chosen through
# main_menu_choose: main_menu_exit without the wheel's spin (the skater's spin animation, the click, the turns) -
# a short fade instead, then the item's script. The item chosen is focused when the menu comes back (as the game's
# spin_menu_selected_index). Items the game makes inactive (active=0) are shown but not focusable; the 2 player
# item's tag `active` is switched by the game's enable / disable_two_player_option as before, and main_menu_choose
# doesn't run an inactive item.
# SKATE NOW (main_menu_skate_now): the menu slides out, the skater gets skating physics (main_menu_skater_skate: the
# front end's skater is in walking physics mode), then THPS4's practice start: the restart TRG_SS_SkaterDropIn
# (front_end_nodes) and DropIn. The restart is KillSkater node= - the skater's real restart (ResetSkaters skips the
# front end's skater; Obj_MoveToNode left the physics behind: the drop-in ended with a snap); it restarts the skater's
# script, so DropIn is started after it. Esc (root_window's pad_start) brings the menu back (main_menu_skate_stop)
# behind a short fade to black: built as it is, the menu came back in pieces over a few frames (the items sliding in
# over the skating view, then the camera, the skater still on his board, then the title).
MAIN_MENU_SCRIPTS = r"""main_menu_list_pos = (229.0, 98.0)
main_menu_list_width = 220.0
main_menu_list_spacing = 2
main_menu_text_scale = 0.85
main_menu_backing_pad_x = 16.0
main_menu_backing_pad_y = 9.0
main_menu_list_index = 0
main_menu_list_items = 0
main_menu_skating = 0
script create_main_menu {
    change main_menu_return_to_createamodes=0
    SetNetworkMode
    UnpauseMusicAndStreams
    UnPauseObjects
    change in_options_menu=0
    SetNetworkMode
    SetMemThreadSafe off
    main_menu_wait_until_all_clear
    spin_menu_skater_script reset
    KillSpawnedScript name=TemporarilyDisableInput
    SpawnScript TemporarilyDisableInput params={time=400}
    SetMenuPadMappings [active
        use_as_first
    ]
    kill_start_key_binding
    make_new_roundbar_menu {
        menu_id=main_menu
        vmenu_id=main_vmenu
        title="MAIN MENU"
        pos=main_menu_list_pos
        dims=(main_menu_list_width * (1.0, 0.0) + (0.0, 480.0))
        spacing=main_menu_list_spacing
        use_icons
        helper_text=generic_helper_text_no_back
        pad_back_sound=nullscript
    }
    spin_menu_visible
    SetScreenElementProps {
        id=current_menu
        event_handlers=[
            {pad_up reset_attract_mode_timer}
            {pad_down reset_attract_mode_timer}
        ]
    }
    change main_menu_list_items=0
    add_main_menu_list_items
    CreateScreenElement {
        type=ContainerElement
        parent=current_menu_anchor
        id=attract_container
        event_handlers=[{pad_back reset_attract_mode_timer}
            {pad_left reset_attract_mode_timer}
            {pad_right reset_attract_mode_timer}
        ]
    }
    roundbar_menu_finish build_pause_parts=0 data={grid_index=(main_menu_list_index)}
    backing_width = (main_menu_list_width + 40.0 + 2.0 * main_menu_backing_pad_x)
    backing_height = ((main_menu_list_items) * (18.0 + main_menu_list_spacing) - main_menu_list_spacing + 2.0 * main_menu_backing_pad_y)
    CreateScreenElement {
        type=SpriteElement
        parent=current_menu_anchor
        id=main_menu_backing
        texture=white2
        pos=(main_menu_list_pos - (20.0 + main_menu_backing_pad_x) * (1.0, 0.0) - main_menu_backing_pad_y * (0.0, 1.0))
        just=[left top]
        rgba=[0 0 0 112]
        scale=((<backing_width> / 8.0) * (1.0, 0.0) + (<backing_height> / 8.0) * (0.0, 1.0))
        z_priority=-1
    }
    FireEvent type=focus target=attract_container
    unpauseskaters
    UnpauseMusicAndStreams
    spin_menu_camera_init
    pulse_blur speed=2 start=255
    KillSpawnedScript name=attract_mode_timer
    SpawnScript attract_mode_timer
    if isNgc {
        MemPushContext TopDownHeap
    }
    SpawnSecondControllerCheck
    if isNgc {
        MemPopContext
    }
    if (layer_infos_need_resetting = 1) {
        edit_graphic_reset_layer_infos
        edit_graphic_apply_graphic_from_save
        change layer_infos_need_resetting=0
    }
    wait 5 gameframe
    kill_start_key_binding
}
script add_main_menu_list_items {
    demo_build_item_active = 1
    main_menu_list_item {
        text="SKATE NOW"
        id=menu_skate_now
        pad_choose_script=main_menu_skate_now
        mark_first_input
        icon=mainicon_score
    }
    main_menu_list_item {
        text="STORY MODE"
        id=menu_career
        pad_choose_params={
            new_menu_script=create_gamemode_options_menu
            change_gamemode=change_gamemode_career
            hold_camera
        }
        mark_first_input
        icon=mainicon_story
    }
    main_menu_list_item {
        text="CLASSIC MODE"
        id=menu_classic
        pad_choose_params={
            new_menu_script=create_gamemode_options_menu
            change_gamemode=change_gamemode_classic
            hold_camera
        }
        mark_first_input
        icon=mainicon_classic
    }
    if OnXbox {
        if ! (DEMO_BUILD) {
            if NetworkAdaptorInstalled {
                <net_active> = 1
            } else {
                if ! CD {
                    <net_active> = 1
                } else {
                    <net_active> = 0
                }
            }
            main_menu_list_item {
                text="MULTIPLAYER"
                id=menu_net
                pad_choose_params={
                    new_menu_script=select_network_play_multi
                    change_gamemode=change_gamemode_net
                    hold_camera
                    do_loading_logo
                }
                mark_first_input
                active=<net_active>
                icon=mainicon_online
            }
        } else {
            main_menu_list_item {
                text="MULTIPLAYER"
                id=menu_net
                active=0
                icon=mainicon_online
            }
        }
    }
    main_menu_list_item {
        text="HIGH SCORE / FREE SKATE"
        id=menu_singlesess
        pad_choose_params={
            new_menu_script=launch_select_skater_menu
            change_gamemode=change_gamemode_singlesession
            hold_camera
            do_loading_logo
        }
        mark_first_input
        icon=mainicon_score
        active=<demo_build_item_active>
    }
    main_menu_list_item {
        text="2 PLAYER"
        id=main_menu_2_player_option
        pad_choose_params={
            new_menu_script=launch_select_skater_menu
            change_gamemode=change_gamemode_freeskate_2p
            hold_camera
            do_loading_logo
        }
        mark_first_input
        icon=mainicon_2player
        active=<demo_build_item_active>
    }
    main_menu_list_item {
        text="CREATE-A-MODES"
        id=menu_createamodes
        pad_choose_params={
            new_menu_script=create_createamodes_menu
            change_gamemode=change_gamemode_singlesession
            hold_camera
        }
        mark_first_input
        icon=mainicon_mode
    }
    main_menu_list_item {
        text="GAME OPTIONS"
        id=menu_options
        pad_choose_params={
            new_menu_script=create_setup_options_menu
            hold_camera
        }
        mark_first_input
        icon=mainicon_options
    }
    main_menu_list_item {
        text="QUIT"
        id=menu_quit
        pad_choose_params={
            new_menu_script=main_menu_exit_game
            hold_camera
        }
        icon=mainicon_mode
    }
}
script main_menu_list_item {active = 1 pad_choose_params = {} pad_choose_script = main_menu_choose
    if (<active> = 1) {
        add_roundbar_menu_item {
            text=<text>
            id=<id>
            pad_choose_script=<pad_choose_script>
            pad_choose_params=<pad_choose_params>
            icon=<icon>
            scale=main_menu_text_scale
        }
    } else {
        add_roundbar_menu_item {
            text=<text>
            id=<id>
            icon=<icon>
            scale=main_menu_text_scale
            not_focusable
        }
    }
    SetScreenElementProps {
        id=<id>
        tags={active=<active> list_index=(main_menu_list_items)}
    }
    change main_menu_list_items=(main_menu_list_items + 1)
    if GotParam mark_first_input {
        SetScreenElementProps {
            id=<id>
            event_handlers=[
                {pad_choose mark_first_input_received}
                {pad_start mark_first_input_received}
            ]
        }
    }
}
script main_menu_choose {
    if (main_menu_spinning = 1) {
        return
    }
    GetSingleTag active
    if (<active> = 0) {
        return
    }
    GetSingleTag list_index
    change main_menu_list_index=<list_index>
    RemoveParameter active
    RemoveParameter list_index
    change main_menu_spinning=1
    KillSpawnedScript name=TemporarilyDisableInput
    SetButtonEventMappings block_menu_input
    if ! GotParam hold_camera {
        skater:reset_model_lights
        skater:PausePhysics
        skater:CancelRotateDisplay
        play_no_skater_cam
    }
    KillSpawnedScript name=attract_mode_timer
    if GotParam change_cas_from_main {
        change entered_cas_from_main=1
    }
    if ! GotParam ignore_second_controller {
        StopSecondControllerCheck
    }
    if GotParam kill_clouds {
        if ObjectExists id=cloud_anchor {
            DestroyScreenElement id=cloud_anchor
        }
    }
    if GotParam set_cat_mode_flag {
        change launch_to_createatrick=1
    } else {
        change launch_to_createatrick=0
    }
    if ScreenElementExists id=current_menu_anchor {
        main_menu_loading_screen time=0.25
    }
    wait 0.25 seconds
    if ObjectExists id=current_menu_anchor {
        DestroyScreenElement id=current_menu_anchor
    }
    change main_menu_spinning=0
    SetButtonEventMappings unblock_menu_input
    if GotParam new_menu_script {
        <new_menu_script> <...>
    }
}
script main_menu_skate_now {
    if (main_menu_spinning = 1) {
        return
    }
    change main_menu_list_index=0
    change main_menu_spinning=1
    KillSpawnedScript name=TemporarilyDisableInput
    KillSpawnedScript name=attract_mode_timer
    SetButtonEventMappings block_menu_input
    generic_menu_animate_out force
    if ObjectExists id=current_menu_anchor {
        DestroyScreenElement id=current_menu_anchor
    }
    change main_menu_spinning=0
    SetButtonEventMappings unblock_menu_input
    change main_menu_skating=1
    KillSkaterCamAnim all
    MakeSkaterGoto main_menu_skater_skate
    wait 2 gameframes
    skater:KillSkater node=TRG_SS_SkaterDropIn
    wait 1 gameframe
    MakeSkaterGoto DropIn
    SetScreenElementProps {
        id=root_window
        event_handlers=[{pad_start main_menu_skate_stop}]
        replace_handlers
    }
    FireEvent type=focus target=root_window
}
script main_menu_skater_skate {
    SkaterInit NoEndRun ReturnControl
    if walking {
        CleanUp_WalkingtoSkating
        SwitchToSkatingPhysics
    }
    UnpausePhysics
    SetRollingFriction `default`
    AllowRailTricks
    ClearEventBuffer
    goto OnGroundAI
}
script main_menu_skate_stop {
    if (main_menu_skating = 0) {
        return
    }
    change main_menu_skating=0
    kill_start_key_binding
    SetButtonEventMappings block_menu_input
    fadetoblack on time=0.15 alpha=1.0 id=main_menu_skate_fader z_priority=9500
    wait 0.15 seconds
    spin_menu_skater_script reset
    spin_menu_camera_init
    create_main_menu
    wait 0.4 seconds
    SetButtonEventMappings unblock_menu_input
    fadetoblack off time=0.2 id=main_menu_skate_fader
}
"""

# The front end's skater stands in THPS4's shop (the DLL draws only that shop in the front end): its restarts
# (TRG_Playerrestart - the main menu, Create-A-Skater and the other menus' skater; SS_player_restart - the skate
# shop menu's) moved to the floor on the left of the held view, a few steps in front of the counter, facing the
# camera (yaw:
# the skater faces (sin yaw, 0, cos yaw); the held view's camera is at (565, 74, -1114)).
FRONT_END_SKATER_POS = (467.0, 0.0, -1304.0)
FRONT_END_SKATER_ANGLES = (0.0, 0.474, 0.0)


# THPS4's practice start (its start_practice_run_skater: ResetSkaters node_name=TRG_SS_SkaterDropIn, MakeSkaterGoto
# Dropin): the restart on the outside halfpipe's coping that SKATE NOW drops in from.
DROP_IN_NODE = "TRG_SS_SkaterDropIn"


def front_end_nodes(text, backdrop_nodes):
    # The front end's node array (levels\mainmenu\mainmenu.qb) with its restarts moved into THPS4's shop, and THPS4's
    # drop-in restart added at the end (from the converted shop's node array `backdrop_nodes`, as NeverScript text;
    # added last, the front end's node indices and links are unchanged).
    # The front end loaded only TERRAIN_DEFAULT's sounds (its LoadTerrain): THPS4's shop is wood, metal... - silent
    # rolling after SKATE NOW. Every terrain's, as the ported levels (level_port nodes.load_terrain_ns).
    terrain = re.search(r"script LoadTerrain \{\n.*?\n\} \n", text, re.S)
    if not terrain:
        sys.exit("mainmenu.qb: LoadTerrain not found")
    text = text[:terrain.start()] + level_port_nodes.load_terrain_ns() + " \n" + text[terrain.end():]
    for name in ("TRG_Playerrestart", "SS_player_restart"):
        node = re.search(r"\n( *)Pos=\([^)]*\) *\n *Angles=\([^)]*\) *\n *Name=%s *\n" % name, text)
        if not node:
            sys.exit("mainmenu.qb: the restart %s not found" % name)
        indent = node.group(1)
        moved = "\n%sPos=(%.1f, %.1f, %.1f) \n%sAngles=(%.6f, %.6f, %.6f) \n%sName=%s \n" % (
            (indent,) + FRONT_END_SKATER_POS + (indent,) + FRONT_END_SKATER_ANGLES + (indent, name))
        text = text[:node.start()] + moved + text[node.end():]
    node = re.search(r"\n *Position=\(([^)]*)\) *\n *Angles=\(([^)]*)\) *\n *Name=%s *\n" % DROP_IN_NODE, backdrop_nodes)
    end = text.find("\n] \n")
    if not node or end < 0 or text.find("\n] \n", end + 1) >= 0:
        sys.exit("the front end's node array / %s's %s not as expected" % (BACKDROP_LEVEL, DROP_IN_NODE))
    # Written as THUG2's own nodes are: Pos = the legacy Position with z negated (as the engine reads it), THPS4's
    # angles kept (the skater pitched 90 degrees against the vert at the coping: DropIn's animation - THPS4's, never
    # used by THUG2 itself - is made for that). Copied verbatim instead (Position= with CreatedAtStart), the game quit
    # at start-up.
    x, y, z = (float(v) for v in node.group(1).split(","))
    pitch, yaw, roll = (float(v) for v in node.group(2).split(","))
    drop_in = ("    {\n        Pos=(%.3f, %.5f, %.3f) \n        Angles=(%.6f, %.6f, %.6f) \n        Name=%s \n"
               "        Class=Restart \n        Type=Generic \n        CreatedAtStart \n        RestartName=\"DropIn\" \n"
               "        restart_types=[Generic] \n    } \n" % (x, y, -z, pitch, yaw, roll, DROP_IN_NODE))
    return text[:end + 1] + drop_in + text[end + 1:]


# The converted shop's peds, cars and their waypoints (tools/level_port/peds_cars.py: the last nodes of its array).
MOVING_CLASSES = {"pedestrian", "vehicle", "waypoint"}


def front_end_peds_cars(text, backdrop_nodes):
    # The front end's node array with the converted shop's peds and cars (the pros on the outside halfpipe, the people
    # walking by, the cars on the road: seen after SKATE NOW) added at its end - their Links renumbered, their trigger
    # scripts copied.
    elements, _, _ = array_elements(backdrop_nodes, BACKDROP_LEVEL + "_NodeArray")
    kinds = [m.group(1).lower() if m else "" for m in (re.search(r"\bClass=(\w+)", e) for e in elements)]
    first = next((i for i, kind in enumerate(kinds) if kind in MOVING_CLASSES), None)
    if first is None:
        print("%s has no peds or cars: none in the front end" % BACKDROP_LEVEL)
        return text
    if any(kind not in MOVING_CLASSES for kind in kinds[first:]):
        sys.exit("%s: its peds and cars aren't the last nodes of its array" % BACKDROP_LEVEL)
    own, _, close = array_elements(text, "mainmenu_NodeArray")
    base = len(own)

    def renumber(match):
        return "Links=[%s]" % ", ".join(str(int(v) - first + base) for v in re.split(r"[,\s]+", match.group(1).strip()))

    added = "".join("    %s \n" % re.sub(r"Links=\[([^\]]*)\]", renumber, e) for e in elements[first:])
    text = text[:close] + added + text[close:]
    scripts = []
    for name in dict.fromkeys(re.findall(r"\bTriggerScript=(\w+)", "".join(elements[first:]))):
        found = re.search(r"(?:^|\n)(script %s \{\n.*?\n\} \n)" % re.escape(name), backdrop_nodes, re.S)
        if not found:
            sys.exit("%s: the trigger script %s not found" % (BACKDROP_LEVEL, name))
        scripts.append(found.group(1))
    register = text.find("\n__register_checksums__")
    at = register + 1 if register >= 0 else len(text)
    print("front end: %d peds / cars / waypoints from %s" % (len(elements) - first, BACKDROP_LEVEL))
    return text[:at] + "".join(scripts) + text[at:]


def front_end_scripts(text):
    # The front end's scripts for THPS4's shop: the standard main menu; at the start, the shop's collision (the DLL
    # loads its scene; the skater skates there after SKATE NOW) and the skater's gameplay animations
    # (do_load_unloadable: load_level skips them for the front end - without them the skater skates in its bind
    # pose; once loaded they stay, as after any level); the skater standing idle instead of reaching for the wheel
    # (the main menu's skater animation: WStand, standing on foot, the board in his hand).
    text = standard_main_menu(text)
    start = "script mainmenu_startup {\n"
    if text.count(start) != 1:
        sys.exit("mainmenu_scripts.qb: mainmenu_startup not found")
    text = text.replace(start, start + "    LoadCollision scene=\"%s\" \n    do_load_unloadable \n" % BACKDROP_LEVEL, 1)
    anim = re.search(r"script SpinMenuGetSkaterAnim \{\n.*?\n\} \n", text, re.S)
    if not anim:
        sys.exit("mainmenu_scripts.qb: SpinMenuGetSkaterAnim not found")
    return text[:anim.start()] + "script SpinMenuGetSkaterAnim {\n    return anim=WStand \n} \n" + text[anim.end():]


def standard_main_menu(text):
    # The front end's scripts with create_main_menu replaced by MAIN_MENU_SCRIPTS (the wheel's scripts stay: other
    # scripts call them).
    start = text.index("script create_main_menu {")
    end = text.index("script spin_menu_init {", start)
    return text[:start] + MAIN_MENU_SCRIPTS + text[end:]


def array_elements(text, name):
    # The top-level { ... } elements of the array `name = [ ... ]` and where the array's [ ... ] is in `text`.
    found = re.search(re.escape(name) + " =", text, re.IGNORECASE) # (names are checksums: any case)
    if not found:
        raise ValueError("%s not found" % name)
    start = found.start()
    open_at = text.index("[", start)
    elements, depth, element_start, i = [], 0, None, open_at + 1
    while True:
        c = text[i]
        if c == '"':
            i = text.index('"', i + 1)
        elif c in "{[(":
            if depth == 0 and c == "{":
                element_start = i
            depth += 1
        elif c in "}])":
            if depth == 0:
                return elements, open_at, i
            depth -= 1
            if depth == 0 and c == "}":
                elements.append(text[element_start:i + 1])
        i += 1


def ns(tool, *args):
    result = subprocess.run([tool, *args], capture_output=True, text=True)
    if result.returncode != 0:
        sys.exit("ns %s failed:\n%s%s" % (" ".join(args), result.stdout, result.stderr))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--game", default=r"C:\Games\THUG2")
    parser.add_argument("--out", help="the overlay folder (default: <game>\\thug2-decomp-data)")
    parser.add_argument("--ns", default=os.path.join(REPO, "out", "neverscript", "ns.exe"))
    args = parser.parse_args()
    out = args.out or os.path.join(args.game, "thug2-decomp-data")
    if not os.path.exists(args.ns):
        sys.exit("%s not found: build it with tools\\build_neverscript.ps1" % args.ns)

    pre = os.path.join(args.game, "Data", "pre")
    qb_scripts = os.path.join(pre, "qb_scripts.prx")
    menu_scripts = os.path.join(pre, "mainmenu_scripts.prx")

    # The folder is ours: emptied first (a file left from an earlier layout would still be read).
    marker = os.path.join(out, MARKER)
    if os.path.isdir(out):
        if not os.path.exists(marker) and not set(os.listdir(out)) <= {"scripts", "levels", "front_end"}:
            sys.exit("%s has files this tool didn't make: not emptying it" % out)
        shutil.rmtree(out)
    os.makedirs(out)
    open(marker, "w", encoding="utf-8").write("Made by tools/menu_level/make_menu_level.py from the game's files; "
                                              "emptied and made again on each run.\n")

    with tempfile.TemporaryDirectory() as work:
        def decompile(data, name):
            path = os.path.join(work, name)
            open(path, "wb").write(data)
            ns(args.ns, "-d", path, "-o", path + ".ns")
            return open(path + ".ns", encoding="utf-8").read()

        def write(text, name):
            path = os.path.join(work, "new_" + name.replace("\\", "_") + ".ns")
            open(path, "w", encoding="utf-8").write(text)
            target = os.path.join(out, name)
            os.makedirs(os.path.dirname(target), exist_ok=True)
            ns(args.ns, "-c", path, "-o", target, "-targetGame", "thug2")
            print("wrote", target)

        levels = decompile(prx_file(qb_scripts, r"scripts\game\levels.qb"), "Levels.qb")
        shop = decompile(prx_file(menu_scripts, r"levels\mainmenu\mainmenu.qb"), "mainmenu.qb")
        front_end = decompile(prx_file(menu_scripts, r"levels\mainmenu\mainmenu_scripts.qb"), "mainmenu_scripts.qb")
        select = decompile(prx_file(qb_scripts, r"scripts\game\menu\gamemenu_levelselect.qb"), "levelselect.qb")
        pause = decompile(prx_file(qb_scripts, r"scripts\game\menu\gamemenu_pause.qb"), "pause.qb")

        # Always: the playable skate shop; and the THPS4 levels when tools/thps4_levels has converted them.
        levels_with_shop = add_skateshop_level(levels)
        thps4 = ported_levels.load_manifest(args.game)
        if thps4:
            levels_with_shop = ported_levels.add_to_levels(levels_with_shop, thps4)
            installed = ported_levels.install_files(args.game, out)
            print("Converted levels (THPS4 / THUG1): %d added to the level select; copied %s" % (len(thps4["levels"]), ", ".join(installed)))
        write(levels_with_shop, r"scripts\game\Levels.qb")
        write(skateshop_nodes(shop), r"levels\skateshop\skateshop.qb")
        # The level select in sections, one per game, with stars and signs for our levels (level_select.py).
        by_game = {}
        for entry in (thps4 or {"levels": []})["levels"]:
            by_game.setdefault(entry["staging"], {"levels": []})["levels"].append(entry)
        section_list = level_select.sections(thps4=by_game.get("thug2-decomp-thps4"),
                                             thug1=by_game.get("thug2-decomp-thug1"),
                                             thps3=by_game.get("thug2-decomp-thps3"),
                                             thaw=by_game.get("thug2-decomp-thaw"))
        write(level_select.edit_level_select(add_skateshop_sign(select), section_list),
              r"scripts\game\menu\gamemenu_levelselect.qb")
        sprites = decompile(prx_file(qb_scripts, r"scripts\game\menu\sprites.qb"), "sprites.qb")
        write(level_select.edit_sprites(sprites, section_list), r"scripts\game\menu\sprites.qb")
        frame = prx_file(os.path.join(pre, "Levelselect.prx"), level_select.level_select_icons.FRAME_SOURCE)
        print("level select signs: %s" % ", ".join(level_select.write_signs(frame, section_list, out)))
        print(level_select.describe(section_list))
        testlevel = decompile(prx_file(os.path.join(pre, "TestLevel_scripts.prx"), r"levels\testlevel\testlevel.qb"),
                              "TestLevel.qb")
        write(testlevel_nodes(testlevel), r"levels\TestLevel\TestLevel.qb")
        # Always: the pause menu's LEVEL EDITOR (the DLL's [features] level_editor decides whether it shows).
        write(add_level_editor(pause), r"scripts\game\menu\gamemenu_pause.qb")

        # The standard main menu, for the front end in THPS4's skate shop (the DLL uses front_end\ while
        # menu_backdrop is on and the shop is installed).
        if thps4 and any(level["level"] == BACKDROP_LEVEL for level in thps4["levels"]):
            write(front_end_scripts(front_end), r"front_end\levels\mainmenu\mainmenu_scripts.qb")
            backdrop_nodes = decompile(open(os.path.join(out, "levels", BACKDROP_LEVEL, BACKDROP_LEVEL + ".qb"), "rb").read(),
                                       "backdrop.qb")
            write(front_end_peds_cars(front_end_nodes(shop, backdrop_nodes), backdrop_nodes),
                  r"front_end\levels\mainmenu\mainmenu.qb")
            # THUG2's own shop's collision without its objects: the DLL hides THUG2's shop, but its collision stayed -
            # x -1372..764, z -1377..471 overlaps THPS4's outside halfpipe (x 465..1155): invisible walls after SKATE
            # NOW. The front end's skater stands in THPS4's shop (its collision: mainmenu_startup's LoadCollision).
            target = os.path.join(out, "front_end", "levels", "mainmenu", "mainmenu.col.xbx")
            open(target, "wb").write(thug2_col.write_col({"objects": [], "pad": 0}))
            print("wrote", target)
        else:
            print("%s not converted (tools/thps4_levels): no front end backdrop, the game's main menu kept" % BACKDROP_LEVEL)


if __name__ == "__main__":
    main()
