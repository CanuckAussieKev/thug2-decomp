# Makes src/game/front/automation_tables.inc - the test-automation channel's tables (src/game/front/automation.h):
#  - kDeniedScripts: scripts the channel never runs and menu items it never chooses when their handlers name them -
#    every script of the game's network / online script files, and every script whose name has an online word;
#  - kDeniedIds: screen element ids it never chooses (the ids only those files make, and ids with an online word);
#  - kKnownNames: names for the status file (menu / element ids, game modes, levels' scenes) - only shown.
# Reads the game's loose scripts (never writes there): decompiled with NeverScript into a temporary folder.
#
#   python -I tools/game/make_automation_tables.py [--game C:\Games\THUG2] [--ns out\neverscript\ns.exe]
import argparse, os, re, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", ".."))
OUT = os.path.join(REPO, "src", "game", "front", "automation_tables.inc")
MENU_LEVEL = os.path.join(REPO, "tools", "menu_level", "make_menu_level.py")

# The network / online script files: all their scripts and ids are denied.
NET_FILES = [
    r"scripts\game\net\net.qb",
    r"scripts\game\net\netoptions.qb",
    r"scripts\game\menu\netmenu.qb",
    r"scripts\game\menu\onlinestats.qb",
    r"scripts\game\menu\net_vault_menu.qb",
    r"scripts\game\menu\pager_menu.qb",
    r"levels\mainmenu\mainmenu_net.qb",
]
# Gateways into the network game elsewhere, named by hand.
EXTRA_SCRIPTS = [
    "change_gamemode_net", "select_network_play_multi", "select_network_play", "select_xbox_multiplayer",
    "start_internet_game", "launch_save_internet_settings", "retry_launch_save_internet_settings",
    "destroy_internet_options_menu", "back_to_internet_options_menu", "toggle_net_connection_type",
    "select_internet_play", "select_lan_play", "select_thug_online_play", "chose_internet", "chose_xbox_online",
    "launch_xbox_online_menu", "connected_to_internet", "do_network_setup", "test_network_setup",
    "prompt_for_net_config", "launch_network_select_lan_games_menu", "create_network_select_games_menu",
    "create_network_options_menu", "launch_profile_menu", "profile_connect", "chosen_host_game",
    "spawned_chosen_host_game", "create_lobby_list_menu", "choose_selected_lobby", "rejoin_lobby",
    "launch_homie_list", "create_online_stats_menu",
]
EXTRA_IDS = ["menu_net", "mm_multi_play", "mm_net_play", "menu_network_options", "menu_connection",
             "menu_network_select_hang_up"]
# The same rule as Front::Automation::NameLooksOnline (automation.cpp) - keep them together.
ONLINE_WORDS = {"net", "online", "internet", "lobby", "lobbies", "login", "logon", "buddy", "buddies", "homie",
                "homies", "gamespy", "network", "server", "servers", "join", "host", "lan"}
ONLINE_PARTS = ("internet", "lobby", "gamespy", "network", "login")  # ("online" only as a word: DrawInstructionLine)
# Scripts the offline menus need, with online-looking words (none known so far).
ALLOWED = set()


def looks_online(name):
    low = name.lower()
    return any(p in low for p in ONLINE_PARTS) or any(w in ONLINE_WORDS for w in low.split("_"))


def decompile(ns, qb, tmp):
    out = os.path.join(tmp, os.path.basename(qb) + ".ns")
    r = subprocess.run([ns, "-d", qb, "-o", out], capture_output=True, text=True)
    if r.returncode != 0 or not os.path.exists(out):
        sys.exit("ns failed on %s: %s" % (qb, (r.stdout + r.stderr)[-300:]))
    return open(out, encoding="utf-8", errors="surrogateescape").read()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--game", default=r"C:\Games\THUG2")
    ap.add_argument("--ns", default=None)
    a = ap.parse_args()
    data = a.game
    for sub in ("Game\\Data", "Data"):
        if os.path.isdir(os.path.join(a.game, sub)):
            data = os.path.join(a.game, sub)
            break
    ns = a.ns
    if not ns:
        for cand in (os.path.join(REPO, "out", "neverscript", "ns.exe"), r"C:\Dev\thug2-decomp\out\neverscript\ns.exe"):
            if os.path.exists(cand):
                ns = cand
                break
    if not ns:
        sys.exit("ns.exe not found (tools\\build_neverscript.ps1)")

    denied_scripts, denied_ids, known = set(), set(), set()
    net_ids, offline_ids = set(), set()
    script_re = re.compile(r"^script ([A-Za-z_][A-Za-z0-9_]*)", re.M)
    id_re = re.compile(r"\bid=([A-Za-z_][A-Za-z0-9_]*)")
    level_re = re.compile(r'\blevel="([A-Za-z0-9_]+)"')
    load_re = re.compile(r"\bload_script=([A-Za-z0-9_]+)")
    mode_re = re.compile(r"^mode_[A-Za-z0-9_]+ = \{\s*name=([A-Za-z0-9_]+)", re.M)
    with tempfile.TemporaryDirectory() as tmp:
        net = {os.path.normcase(os.path.join(data, f)) for f in NET_FILES}
        files = []
        for root in (os.path.join(data, "scripts"), os.path.join(data, "levels", "mainmenu")):
            for dirpath, _, names in os.walk(root):
                files += [os.path.join(dirpath, n) for n in names if n.lower().endswith(".qb")]
        missing = [f for f in net if f not in {os.path.normcase(p) for p in files}]
        if missing:
            sys.exit("missing network scripts: %s" % missing)
        for qb in files:
            if os.path.basename(qb).lower().startswith("localtext"):
                continue
            text = decompile(ns, qb, tmp)
            scripts = script_re.findall(text)
            ids = id_re.findall(text)
            if os.path.normcase(qb) in net:
                denied_scripts.update(scripts)
                net_ids.update(ids)
            else:
                offline_ids.update(ids)
            denied_scripts.update(s for s in scripts if looks_online(s))
            denied_ids.update(i for i in ids if looks_online(i))
            known.update(ids)
            known.update(mode_re.findall(text))
            known.update(level_re.findall(text))
            known.update(load_re.findall(text))
    menu = open(MENU_LEVEL, encoding="utf-8").read()
    known.update(id_re.findall(menu))
    offline_ids.update(id_re.findall(menu))
    # (ids the network files share with the offline menus - current_menu, main_menu... - stay allowed)
    denied_ids.update(net_ids - offline_ids)
    denied_scripts.update(EXTRA_SCRIPTS)
    denied_ids.update(EXTRA_IDS)
    denied_scripts -= ALLOWED
    known.update(denied_ids)

    def array(name, items):
        lines = ["static const char* const %s[] = {" % name]
        row = "   "
        for s in sorted(items, key=str.lower):
            item = ' "%s",' % s
            if len(row) + len(item) > 118:
                lines.append(row)
                row = "   "
            row += item
        lines.append(row)
        lines.append("};")
        return "\n".join(lines)

    text = ("// Generated by tools/game/make_automation_tables.py from the game's scripts - don't edit. NOT ORIGINAL\n"
            "// (the test-automation channel's tables, game/front/automation.h).\n\n"
            + array("kDeniedScripts", denied_scripts) + "\n\n" + array("kDeniedIds", denied_ids) + "\n\n"
            + array("kKnownNames", known) + "\n")
    with open(OUT, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    print("%s: %d denied scripts, %d denied ids, %d known names" % (OUT, len(denied_scripts), len(denied_ids),
                                                                  len(known)))


if __name__ == "__main__":
    main()
