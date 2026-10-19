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
import peds_cars  # noqa: E402
import ported_levels  # noqa: E402
import qb  # noqa: E402
import script_port  # noqa: E402
import tex  # noqa: E402
import thps4  # noqa: E402
import thps4_scripts  # noqa: E402
from binio import FormatError  # noqa: E402

STAGING = "thug2-decomp-thps4"

# The THPS4 levels converted by default (folder under Game\data\levels): everything with a scene except the park
# editor's sets. Foo has only a node array (no scene).
LEVELS = ["Alc", "Cnv", "Hof", "Jnk", "Kon", "Lon", "SF2", "Sch", "Zoo", "SkateShop", "Motox"]
# The level select's numbers for the signs (each sign needs its own; none of them has career records - the info
# panel skips them, as make_menu_level.py's SKATE SHOP / TEST LEVEL at 18 / 21).
SIGN_NUMBERS = [19, 20, 22, 23, 24, 25, 26, 27, 28, 29, 31]
# The levels whose peds and cars are brought over (tools/level_port/peds_cars.py): the skate shop's pros on the
# halfpipe, the people walking by and the cars on the road (also the front end's: make_menu_level.py copies them);
# every level's walkers and Car_Generic01 cars.
# College left out: with its walker and car its node array came out null in the game (a crash loading; cause not
# found - its other peds are THUG2 standing peds, its car a LevelObject of its model).
PEDS_CARS = {name.lower() for name in LEVELS} - {"sch"}
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


# Invisible walls opened: (collision object, axis, "min" / "max") - the object's faces lying in its box's plane on that
# side are left out. THPS4's skate shop was a menu: InvisColl01 boxes in the skate park outside the shop (sides,
# roof, both ends); its z-min face closes the shop's door (Object10, the shop, is behind it - the Occlusion plane
# at z -1082 has the doorway's hole, x 699-788), its z-max face the fence's gap at the end of the halfpipe (the user,
# 2026-10-19: both open).
OPENINGS = {"skateshop": [("InvisColl01", 2, "max"), ("InvisColl01", 2, "min")]}


def open_walls(col4, base, report):
    for name, axis, side in OPENINGS.get(base.lower(), ()):
        wanted = qb.checksum(name)
        for o in col4["objects"]:
            if o["checksum"] != wanted or not o["verts"]:
                continue
            plane = (max if side == "max" else min)(v[axis] for v in o["verts"])
            kept = [f for f in o["faces"] if not all(abs(o["verts"][i][axis] - plane) < 0.01 for i in f[2:5])]
            report.append("  %s: %d face(s) of %s left out (an opening)" % (base, len(o["faces"]) - len(kept), name))
            o["faces"] = kept


def read_geometry(folder, base):
    # The level's THPS4 scene, its texture dictionary (THUG2's format) and its THPS4 collision.
    files = level_files(folder, base)
    for kind in ("scn", "col", "tex"):
        if kind not in files:
            raise FormatError("%s: no %s file for %s" % (folder, kind, base))
    return (thps4.read_scene(open(files["scn"], "rb").read(), files["scn"]),
            tex.read_tex(open(files["tex"], "rb").read(), files["tex"]),
            thps4.read_col(open(files["col"], "rb").read(), files["col"]))


def convert_geometry(folder, base, out_dir, out_name, report, keep_triggers=lambda checksum: False, read=None):
    # read: read_geometry's (scene, textures, collision), changed (the vehicles' models added), or None.
    files = level_files(folder, base)
    for kind in ("scn", "col", "tex"):
        if kind not in files:
            raise FormatError("%s: no %s file for %s" % (folder, kind, base))
    warnings = []
    scene4, textures, col4 = read or read_geometry(folder, base)
    # THPS4's vertex colours are 255 = full (rgb; alpha 128 = full), levels and skies alike: halved for THUG2.
    scene = geometry.convert_scene(scene4, warnings, [thps4.to_thug2_material(m) for m in scene4["materials"]],
                                   halve_colors=True)
    converted = tex.depalettise(textures)
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
    thug2 = peds_cars.Thug2Assets(args.thug2)
    port = None if args.no_scripts else thps4_scripts.Port(args.thps4, args.thug2)
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
        if level_scripts:
            for key, value in level_scripts.names.items():
                names.setdefault(key, value)
        # The peds and cars (THUG2's own ped AI and cars, tools/level_port/peds_cars.py), after the converted nodes.
        moving, moving_scripts = [], {}
        if base.lower() in PEDS_CARS:
            # (THPS4's animals - Zoo's penguins, Carnival's pigs... - walk their paths too: not as people)
            people = [qb.Struct([]) if isinstance(n, qb.Struct) and nodes.type_of(n, names).startswith("anl_")
                      else n for n in array]
            moving, moving_scripts, moving_stats = peds_cars.convert(
                people, names, [q] + ([level_scripts] if level_scripts else []), thug2.profile_exists,
                thug2.model_exists)
            report.append("  peds and cars: %d nodes, %s" % (len(moving), moving_stats))
        geometry_read = read_geometry(folder, base)
        scene4, textures4, col4 = geometry_read
        open_walls(col4, base, report)
        brought, extra_text, has_setup, stats = {}, "", False, None
        if port is not None:
            # The level's scripts, game objects, waypoints, peds and vehicles (thps4_scripts.py); the vehicles moved
            # by its scripts get their models (sectors, textures, collision added to the level's).
            plan = port.vehicles.plan(array, names, thps4_scripts.moving_names(moving))
            vehicle_collides = port.vehicles.add_to(plan, scene4, textures4, col4)
            level_sources = [script_port.Source(open(p, "rb").read(), "level")
                             for p in [files["qb"]] + glob.glob(os.path.join(folder, "*_scripts.qb"))]
            converted, kills, brought, extra_text, has_setup, line, _ = thps4_scripts.convert(
                port, array, names, level_sources, kills,
                {s["checksum"] for s in scene4["sectors"] if s["bone"] == -1 and not s["flags"] & thps4.SEC_WEIGHTS},
                moving, struct,
                name, vehicle_sectors=set(plan), collides={o["checksum"] for o in col4["objects"]
                                                           if o["faces"]} | vehicle_collides)
            report.append("  " + line)
        else:
            converted, stats = nodes.convert_nodes(array, names, kills)
            kills = nodes.add_kill_restarts(converted, kills, names)
        triggers = nodes.kill_trigger_names(converted, kills)
        if port is not None:
            # (THUG2's script pools are fixed - 9500 vectors for every script's: College's node array with the
            # objects, waypoints and peds didn't fit and came out null, a crash loading. The rails' and sectors'
            # zero Angles - most of them - aren't needed.)
            thps4_scripts.drop_zero_angles(converted, names)
            # (the sectors whose trigger script runs keep their trigger faces)
            triggers |= {n.get("Name").value for n in converted if isinstance(n.get("TriggerScript"), qb.Name)
                         and isinstance(n.get("Name"), qb.Name) and nodes.class_of(n, names) == "levelgeometry"}
        peds_cars.offset_links(moving, len(converted))
        text = nodes.node_array_ns(name + "_NodeArray", converted + moving, names, kills)
        text += peds_cars.scripts_ns(moving_scripts) + extra_text
        # (a TrickObject without its sector crashes the loader: convert_scene leaves skinned sectors out)
        text, cleared = nodes.strip_missing_trick_objects(text, {
            s["checksum"] for s in scene4["sectors"] if s["bone"] == -1 and not s["flags"] & thps4.SEC_WEIGHTS})
        if cleared:
            report.append("  %d geometry nodes without a scene sector: TrickObject / Cluster dropped" % cleared)
        os.makedirs(os.path.join(out, "levels", name), exist_ok=True)
        qb_path = os.path.join(out, "levels", name, name + ".qb")
        # (network games: path followers, peds and cars kept out - no path manager or ped animations there -, restarts for the players)
        text, marked = nodes.net_game_nodes(text, brought)
        if marked:
            report.append("  %d node(s) changed for network games" % marked)

        def write_qb(text):
            common.ns_compile(args.ns, text, qb_path)
            if brought:
                all_names = {}
                for s in port.sources + level_sources:
                    all_names.update(s.names)
                all_names.update(names)
                data = script_port.append_scripts(open(qb_path, "rb").read(), brought, all_names)
                open(qb_path, "wb").write(data)

        write_qb(text)
        convert_geometry(folder, base, os.path.join(out, "levels", name), name, report,
                         keep_triggers=lambda checksum: checksum in triggers, read=geometry_read)
        # (restarts over nothing or over a kill zone: no network spawns, no kill targets - nodes.ground_restarts)
        text, ground_report = nodes.ground_restarts_at(text, os.path.join(out, "levels", name), name)
        if ground_report:
            write_qb(text)
            report += ground_report
        if stats is not None:
            report.append("  nodes: %d of %d kept %s" % (len(converted), len(array), stats))
        report.append("  %d kill zones" % len(triggers))

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
        if has_setup:
            entry["setup_script"] = name + "_setup"
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
    parser.add_argument("--no-scripts", action="store_true",
                        help="convert: the levels' scripts, game objects and vehicles left out (static levels)")
    parser.add_argument("--extra", nargs="*", default=[], help="selfcheck: more THUG2 archives (Data\\pre\\*.prx)")
    args = parser.parse_args()
    if args.command == "selfcheck":
        sys.exit(cmd_selfcheck(args))
    cmd_convert(args)


if __name__ == "__main__":
    main()
