# NOT ORIGINAL - converts Tony Hawk's Pro Skater 4 (PC) levels to THUG2's formats so that THUG2 can load and play
# them (see README.md). Nothing of either game is stored in the repo: the files are made from the user's own copies.
#
#   python -I tools/thps4_levels/thps4_levels.py selfcheck [--thug2 C:\Games\THUG2] [--thps4 C:\Games\THPS4]
#   python -I tools/thps4_levels/thps4_levels.py convert [levels...] [--thug2 ...] [--thps4 ...] [--out <folder>]
#   then: python -I tools/menu_level/make_menu_level.py   (installs them into the data overlay, with level select signs)
#
# convert writes <thug2>\thug2-decomp-thps4\ (not read by the game): levels\thps4_<lvl>\thps4_<lvl>.scn.xbx /
# .tex.xbx / .col.xbx / .qb (the node array), the skies the same way, and levels.json (what make_menu_level.py adds to
# Levels.qb and the level select: tools/level_port/ported_levels.py). make_menu_level.py copies the levels folder into
# the overlay (<thug2>\thug2-decomp-data), which the DLL serves in place of the game's files.
#
# The THUG2 formats, the geometry conversion, the node arrays and the installing are shared with tools/thug1_levels
# (tools/level_port); this folder has THPS4's own formats (thps4.py) and its levels.

import argparse
import glob
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "level_port"))

import common  # noqa: E402
import geometry  # noqa: E402
import nodes  # noqa: E402
import ported_levels  # noqa: E402
import qb  # noqa: E402
import tex  # noqa: E402
import thps4  # noqa: E402
from binio import FormatError  # noqa: E402

STAGING = "thug2-decomp-thps4"

# The THPS4 levels converted by default (folder under Game\data\levels): everything with a scene except the park
# editor's sets. Foo has only a node array (no scene).
LEVELS = ["Alc", "Cnv", "Hof", "Jnk", "Kon", "Lon", "SF2", "Sch", "Zoo", "SkateShop", "Motox"]
# The level select's numbers for the signs (each sign needs its own; none of them has career records - the info
# panel skips them, as make_menu_level.py's SKATE SHOP / TEST LEVEL at 18 / 21).
SIGN_NUMBERS = [19, 20, 22, 23, 24, 25, 26, 27, 28, 29, 31]
# For the level select: where each level is, and a THUG2 sign that suits it.
INFO = {
    "alc": ("San Francisco Bay, USA", "sign_skatopia"),
    "cnv": ("Carnival", "sign_neworleans"),
    "hof": ("Chicago, USA", "sign_philly"),
    "jnk": ("Shipyard", "sign_airport"),
    "kon": ("Kona, Hawaii, USA", "sign_australia"),
    "lon": ("London, England", "sign_berlin"),
    "sf2": ("San Francisco, USA", "sign_la"),
    "sch": ("College", "sign_school"),
    "zoo": ("Zoo", "sign_australia"),
    "skateshop": ("Skate shop", "sign_training"),
    "motox": ("Motocross track", "sign_jam"),
}


def level_files(folder, base):
    # <base>scn.dat / col.dat / tex.dat / .qb, any case.
    found = {}
    for path in glob.glob(os.path.join(folder, "*")):
        name = os.path.basename(path).lower()
        for kind, suffix in (("scn", "scn.dat"), ("col", "col.dat"), ("tex", "tex.dat"), ("qb", ".qb")):
            if name == (base + suffix).lower():
                found[kind] = path
    return found


def find_folder(levels_dir, name):
    for entry in os.listdir(levels_dir):
        if entry.lower() == name.lower():
            return os.path.join(levels_dir, entry)
    return None


def level_structs(thps4_dir):
    # THPS4's Levels.qb: its level structures by their level= name.
    q = qb.QbFile(open(os.path.join(thps4_dir, "Game", "data", "scripts", "Levels.qb"), "rb").read(), "Levels.qb")
    found = {}
    for key, value in q.globals:
        if isinstance(value, qb.Struct) and isinstance(value.get("level"), str):
            found[value.get("level").lower()] = value
    return found


def convert_geometry(folder, base, out_dir, out_name, report, keep_triggers=lambda checksum: False):
    files = level_files(folder, base)
    for kind in ("scn", "col", "tex"):
        if kind not in files:
            raise FormatError("%s: no %s file for %s" % (folder, kind, base))
    warnings = []
    scene4 = thps4.read_scene(open(files["scn"], "rb").read(), files["scn"])
    is_sky = out_name.lower().endswith("_sky")
    scene = geometry.convert_scene(scene4, warnings, [thps4.to_thug2_material(m) for m in scene4["materials"]],
                                   halve_colors=is_sky)
    textures = tex.read_tex(open(files["tex"], "rb").read(), files["tex"])
    converted = tex.depalettise(textures)
    col4 = thps4.read_col(open(files["col"], "rb").read(), files["col"])
    col = geometry.convert_col(col4, keep_triggers, warnings, thps4.face_flags)
    common.write_level(out_dir, out_name, scene, textures, col)
    common.report_level(report, out_name, scene, textures, col, warnings, converted)
    return files


def cmd_convert(args):
    levels_dir = os.path.join(args.thps4, "Game", "data", "levels")
    out = args.out or os.path.join(args.thug2, STAGING)
    if not os.path.exists(args.ns):
        sys.exit("%s not found: build it with tools\\build_neverscript.ps1" % args.ns)
    structs = level_structs(args.thps4)
    wanted = args.levels or LEVELS
    if os.path.isdir(out):
        shutil.rmtree(out)
    os.makedirs(out)
    manifest = {"levels": []}
    report = []
    skies_done = set()
    for index, folder_name in enumerate(wanted):
        folder = find_folder(levels_dir, folder_name)
        if not folder:
            report.append("%s: not found in %s" % (folder_name, levels_dir))
            continue
        base = folder_name
        struct = structs.get(base.lower())
        name = "thps4_" + base.lower()

        # The node array (first: the collision keeps the trigger faces of the kill zones it keeps).
        files = level_files(folder, base)
        q = qb.QbFile(open(files["qb"], "rb").read(), files["qb"])
        array = q.get("NodeArray")
        if not isinstance(array, list):
            raise FormatError("%s: no NodeArray" % files["qb"])
        level_scripts = None
        for path in glob.glob(os.path.join(folder, "*_scripts.qb")):
            level_scripts = qb.QbFile(open(path, "rb").read(), path)
        kills = nodes.kill_scripts(q, level_scripts)
        names = dict(q.names)
        converted, stats = nodes.convert_nodes(array, names, kills)
        triggers = nodes.kill_trigger_names(converted, kills)
        text = nodes.node_array_ns(name + "_NodeArray", converted, names, kills)
        os.makedirs(os.path.join(out, "levels", name), exist_ok=True)
        common.ns_compile(args.ns, text, os.path.join(out, "levels", name, name + ".qb"))
        convert_geometry(folder, base, os.path.join(out, "levels", name), name, report,
                         keep_triggers=lambda checksum: checksum in triggers)
        report.append("  nodes: %d of %d kept %s; %d kill zones" % (len(converted), len(array), stats, len(triggers)))

        # The sky.
        sky = None
        sky_value = struct.get("sky") if struct else None
        if isinstance(sky_value, str):
            sky_folder = find_folder(levels_dir, sky_value)
            if sky_folder:
                sky = "thps4_" + sky_value.lower()
                if sky not in skies_done:
                    convert_geometry(sky_folder, sky_value, os.path.join(out, "levels", sky), sky, report)
                    skies_done.add(sky)
        display = struct.get("name") if struct and isinstance(struct.get("name"), str) else base
        location, icon = INFO.get(base.lower(), ("", "sign_training"))
        entry = {
            "folder": base,
            "level": name,
            "sky": sky,
            "display": "THPS4 " + display.upper(),
            "name": display,
            "game": "THPS4",
            "location": location,
            "icon": icon,
            "level_num": SIGN_NUMBERS[index % len(SIGN_NUMBERS)],
            "default_stats": struct.get("default_stats") if struct and isinstance(struct.get("default_stats"), int)
            else 6,
            "lighting": common.lighting(struct) if struct else {},
        }
        manifest["levels"].append(entry)
    ported_levels.write_manifest(os.path.join(out, ported_levels.MANIFEST), manifest)
    print("\n".join(report))
    print("wrote %s (%d levels); install with tools\\menu_level\\make_menu_level.py" % (out, len(manifest["levels"])))


# --- self-check -------------------------------------------------------------------------------------------------

def cmd_selfcheck(args):
    pre = os.path.join(args.thug2, "Data", "pre")
    checked, failures = common.thug2_round_trips(
        pre, ("LAscn.prx", "LAcol.prx", "TestLevelscn.prx", "TestLevelcol.prx") + tuple(args.extra))
    levels_dir = os.path.join(args.thps4, "Game", "data", "levels")
    for folder in sorted(os.listdir(levels_dir)):
        for path in sorted(glob.glob(os.path.join(levels_dir, folder, "*"))):
            low = path.lower()
            try:
                data = open(path, "rb").read()
                if low.endswith("scn.dat"):
                    s = thps4.read_scene(data, path)
                    if s["tail"] != b"\0\0\0\0":
                        raise FormatError("%d bytes after the sectors" % len(s["tail"]))
                    what = "%d materials, %d sectors" % (len(s["materials"]), len(s["sectors"]))
                elif low.endswith("col.dat"):
                    c = thps4.read_col(data, path)
                    what = "%d objects" % len(c["objects"])
                elif low.endswith("tex.dat"):
                    t = tex.read_tex(data, path)
                    if tex.write_tex(t) != data:
                        raise FormatError("not the same written again")
                    what = "%d textures" % len(t["textures"])
                elif low.endswith(".qb"):
                    q = qb.QbFile(data, path)
                    what = "%d globals" % len(q.globals)
                else:
                    continue
            except (FormatError, ValueError, IndexError) as e:
                print("FAIL %s: %s" % (path, e))
                failures += 1
                continue
            checked += 1
            print("ok   %s: %s" % (os.path.relpath(path, levels_dir), what))
    print("%d files checked, %d failures" % (checked, failures))
    return 1 if failures else 0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("command", choices=("convert", "selfcheck"))
    parser.add_argument("levels", nargs="*", help="THPS4 level folders (default: all)")
    parser.add_argument("--thug2", default=r"C:\Games\THUG2")
    parser.add_argument("--thps4", default=r"C:\Games\THPS4")
    parser.add_argument("--out", help="where convert writes (default: <thug2>\\%s)" % STAGING)
    parser.add_argument("--ns", default=common.NS)
    parser.add_argument("--extra", nargs="*", default=[], help="selfcheck: more THUG2 archives (Data\\pre\\*.prx)")
    args = parser.parse_args()
    if args.command == "selfcheck":
        sys.exit(cmd_selfcheck(args))
    cmd_convert(args)


if __name__ == "__main__":
    main()
