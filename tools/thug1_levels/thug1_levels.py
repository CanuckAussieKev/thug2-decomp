# NOT ORIGINAL - converts Tony Hawk's Underground (PC) levels to THUG2's formats so that THUG2 can load and play them
# (see README.md). Nothing of either game is stored in the repo: the files are made from the user's own copies.
#
#   python -I tools/thug1_levels/thug1_levels.py selfcheck [--thug2 C:\Games\THUG2] [--thug1 C:\Games\THUG1]
#   python -I tools/thug1_levels/thug1_levels.py convert [levels...] [--thug2 ...] [--thug1 ...] [--out <folder>]
#   then: python -I tools/menu_level/make_menu_level.py   (installs them into the data overlay, with level select signs)
#
# convert reads <thug1>\Game\Data\pre (the level archives <LV>scn.pre, <LV>col.pre, <LV>.pre and qb.pre's Levels.qb)
# and writes <thug2>\thug2-decomp-thug1\ (not read by the game): levels\thug1_<lv>\thug1_<lv>.scn.xbx / .tex.xbx /
# .col.xbx / .qb (the node array), the skies (scene and textures) the same way, and levels.json (what
# make_menu_level.py adds to Levels.qb and the level select: tools/level_port/ported_levels.py).
#
# The THUG2 formats, the geometry conversion, the node arrays and the installing are shared with tools/thps4_levels
# (tools/level_port); this folder has THUG1's own formats (thug1.py) and its levels.

import argparse
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
import prx  # noqa: E402
import qb  # noqa: E402
import script_port  # noqa: E402
import tex  # noqa: E402
import thug1  # noqa: E402
import thug1_port  # noqa: E402
import thug2_col  # noqa: E402
import thug2_scene  # noqa: E402
from binio import FormatError  # noqa: E402

STAGING = "thug2-decomp-thug1"

# THUG1's levels: code (the archives' and Levels.qb's name), the level select's sign text, where it is, a THUG2
# level select sign that suits it, the sign's level number (unique: THPS4's levels use 19-31; none has career
# records), and whether `convert` makes it by default. Read by the level select (levels.json carries the same).
LEVELS = [
    # code            sign text                 location                         icon               num  default
    ("NJ",            "NEW JERSEY",             "New Jersey, USA",               "sign_philly",      40, True),
    ("NY",            "MANHATTAN",              "Manhattan, New York, USA",      "sign_boston",      41, True),
    ("FL",            "TAMPA",                  "Tampa, Florida, USA",           "sign_neworleans",  42, True),
    ("SD",            "SAN DIEGO",              "San Diego, California, USA",    "sign_la",          43, True),
    ("HI",            "HAWAII",                 "Hawaii, USA",                   "sign_australia",   44, True),
    ("VC",            "VANCOUVER",              "Vancouver, Canada",             "sign_canada",      45, True),
    ("SJ",            "SLAM CITY JAM",          "Vancouver, Canada",             "sign_canada",      46, True),
    ("RU",            "MOSCOW",                 "Moscow, Russia",                "sign_berlin",      47, True),
    ("SE",            "HOTTER THAN HELL",       "Hell (secret level)",           "sign_secret1",     48, True),
    ("SC",            "OLD SCHOOL",             "Miami, Florida, USA (THPS1)",   "sign_school",      49, True),
    ("DJ",            "OLD DOWNHILL JAM",       "Phoenix, Arizona, USA (THPS1)", "sign_jam",         50, True),
    ("PH",            "OLD PHILLY",             "Philadelphia, USA (THPS2)",     "sign_philly",      51, True),
    ("VN",            "OLD VENICE",             "Venice Beach, California, USA (THPS2)", "sign_la",  52, True),
    ("HN",            "OLD HANGAR",             "Mullet Falls, Montana, USA (THPS2)", "sign_airport", 53, True),
    ("SC2",           "SCHOOL II",              "Southern California, USA (THPS2)", "sign_school",  54, True),
    ("NJ_Skateshop",  "NJ SKATE SHOP",          "New Jersey, USA",               "sign_training",    55, True),
    ("skateshop",     "SKATE SHOP",             "THUG1's front end skate shop (THPS4's)", "sign_training", 56, False),
    ("CAS_BEDROOM",   "BEDROOM",                "Create-a-skater bedroom",       "sign_training",    57, False),
    ("TestLevel",     "TEST LEVEL",             "The developers' test level",    "sign_training",    58, False),
]
BY_CODE = {code.lower(): (code, sign, location, icon, num, default)
           for code, sign, location, icon, num, default in LEVELS}

# THUG1's collision face flags in THUG2: the same meanings (0x80: the camera collides, as THUG2's - not THPS4's
# reverse); 0x2000 (on every face), 0x4000 and 0x8000 aren't in THUG2's files - dropped.
FACE_KEEP = 0x1FFF


def face_flags(flags):
    return flags & FACE_KEEP


def pre_path(args, name):
    path = os.path.join(args.thug1, "Game", "Data", "pre", name)
    if os.path.exists(path):
        return path
    folder = os.path.dirname(path)
    for entry in os.listdir(folder):
        if entry.lower() == name.lower():
            return os.path.join(folder, entry)
    return None


def level_files(args, code):
    # The level's files from its archives: {name in lower case: data} for levels\<code>\* and levels\<code>_sky\*
    # (scene, textures, collision, qb).
    files = {}
    prefix = ("levels\\%s\\" % code).lower()
    sky_prefix = ("levels\\%s_sky\\" % code).lower()

    def wanted(name):
        low = name.lower().replace("/", "\\")
        return low.startswith(prefix) or low.startswith(sky_prefix)

    for archive in ("%sscn.pre" % code, "%scol.pre" % code, "%s.pre" % code):
        path = pre_path(args, archive)
        if not path:
            continue
        for name, data in prx.entries(path, wanted):
            files[name.lower().replace("/", "\\")] = data
    return files


def level_structs(args):
    # THUG1's Levels.qb (in qb.pre): its level structures by their level= name.
    data = prx.find(pre_path(args, "qb.pre"), r"scripts\game\levels.qb")
    q = qb.QbFile(data, "Levels.qb", weights=True)
    found = {}
    for key, value in q.globals:
        if isinstance(value, qb.Struct) and isinstance(value.get("level"), str):
            found.setdefault(value.get("level").lower(), value)
    return found


def scene_sectors(files, code):
    # The checksums of the sectors the converted scene has (geometry.convert_scene leaves the skinned ones out).
    base = "levels\\%s\\%s" % (code.lower(), code.lower())
    scene1 = thug1.read_scene(files[base + ".scn.xbx"], base + ".scn.xbx")
    return {s["checksum"] for s in scene1["sectors"]
            if not (s["flags"] & thug2_scene.SEC_WEIGHTS or s["bone"] != -1)}


def convert_geometry(files, code, out_dir, out_name, report, keep_triggers=None):
    # The scene, textures and (for a level, not a sky) collision of levels\<code>\<code>.*.
    base = "levels\\%s\\%s" % (code.lower(), code.lower())
    warnings = []
    if base + ".scn.xbx" not in files or base + ".tex.xbx" not in files:
        raise FormatError("%s: no scene / textures in the archives" % code)
    scene1 = thug1.read_scene(files[base + ".scn.xbx"], base + ".scn.xbx")
    scene = geometry.convert_scene(scene1, warnings, scene1["materials"])
    textures = tex.read_tex(files[base + ".tex.xbx"], base + ".tex.xbx")
    converted = tex.depalettise(textures)
    col = None
    if keep_triggers is not None:
        if base + ".col.xbx" not in files:
            raise FormatError("%s: no collision in the archives" % code)
        col1 = thug1.read_col(files[base + ".col.xbx"], base + ".col.xbx")
        col = thug1.to_thug2_col(col1, keep_triggers, face_flags, base)
    common.write_level(out_dir, out_name, scene, textures, col)
    common.report_level(report, out_name, scene, textures, col, warnings, converted)


def convert_level_nodes(files, code, name, ns, out_dir, report, port=None, struct=None, sectors=None):
    # sectors: the converted scene's sector checksums (scene_sectors).
    if sectors is None:
        sectors = scene_sectors(files, code)
    # The node array: kept nodes and kill zones (THUG2's SK3_KillSkater) as <name>.qb; with a port
    # (thug1_port.Thug1Port) also the level's peds, vehicles, objects, particles and its scripts (thug1_port.py).
    # Returns the checksums of the nodes whose collision keeps its trigger faces, and the level structure's
    # startup_script / setup_script kept ({key: name}).
    base = "levels\\%s\\%s" % (code.lower(), code.lower())
    if base + ".qb" not in files:
        raise FormatError("%s: no %s.qb" % (code, base))
    q = qb.QbFile(files[base + ".qb"], base + ".qb", weights=True)
    script_qbs = [q]
    if base + "_scripts.qb" in files:
        script_qbs.append(qb.QbFile(files[base + "_scripts.qb"], base + "_scripts.qb", weights=True))
    names = {}
    for f in reversed(script_qbs):
        names.update(f.names)
    array = q.get("NodeArray")
    if not isinstance(array, list):
        raise FormatError("%s: no NodeArray" % base)
    array = nodes.expand_includes(array, {key.value: value for key, value in q.globals})
    restarts = {n.get("Name").value for n in array if isinstance(n, qb.Struct) and isinstance(n.get("Name"), qb.Name)
                and nodes.class_of(n, names) == "restart"}
    kills = nodes.kill_commands(script_qbs, restarts, names)
    made, scripts, extra, starts, script_report = None, {}, "", {}, ""
    kept, wanted = array, set()
    level_sources = []
    if port is not None:
        level_sources = [script_port.Source(files[base + suffix], thug1_port.LEVEL, weights=True)
                         for suffix in (".qb", "_scripts.qb", "_sfx.qb") if base + suffix in files]
        for s in list(port.shared) + level_sources:
            for value, text in s.names.items():
                names.setdefault(value, text)
        duplicates = thug1_port.resolve_duplicate_triggers(array, script_qbs, names)
        made = thug1_port.NodePass(port, array, names, sectors)
        if duplicates:
            made.counts["DuplicateTrigger nodes given their link's script"] = duplicates
        kept = made.kept
        wanted = {n.get("TriggerScript").value for n in kept if isinstance(n, qb.Struct)
                  and isinstance(n.get("TriggerScript"), qb.Name) and n.get("TriggerScript").value not in kills}
    converted, stats = nodes.convert_nodes(kept, names, kills, extra_keys=("pos",),
                                           as_is=made.as_is if made else (), scripts=wanted)
    kills = nodes.add_kill_restarts(converted, kills, names)
    triggers = nodes.kill_trigger_names(converted, kills)
    if port is not None:
        scripts, globals_text, starts, script_report, _ = thug1_port.port_scripts(
            port, level_sources, converted, array, kills, names, struct, sectors)
        # (the sectors whose trigger script runs keep their trigger faces)
        triggers |= {n.get("Name").value for n in converted if isinstance(n.get("TriggerScript"), qb.Name)
                     and isinstance(n.get("Name"), qb.Name) and nodes.class_of(n, names) == "levelgeometry"}
        anims = [qb.name_text(c, names) for c, _ in q.calls(qb.checksum("load_level_anims"))
                 if c in port.thug2.scripts]
        extra = globals_text + thug1_port.level_scripts_ns(made, anims)
    text = nodes.node_array_ns(name + "_NodeArray", converted, names, kills) + extra
    # (a LevelGeometry TrickObject / Cluster whose sector the scene hasn't crashes ParseNodeArray)
    text, cleared = nodes.strip_missing_trick_objects(text, sectors)
    os.makedirs(out_dir, exist_ok=True)
    path = os.path.join(out_dir, name + ".qb")
    common.ns_compile(ns, text, path)
    if scripts:
        data = script_port.append_scripts(open(path, "rb").read(), scripts, names)
        open(path, "wb").write(data)
    report.append("  nodes: %d of %d kept %s; %d kill zones; TrickObject taken off %d geometry nodes without a sector"
                  % (len(converted), len(array), stats, len(triggers), cleared))
    if made is not None:
        report.append("  made: %s" % ", ".join("%s %d" % kv for kv in sorted(made.counts.items())))
        report.append("  " + script_report)
    return triggers, starts


def cmd_convert(args):
    out = args.out or os.path.join(args.thug2, STAGING)
    if not os.path.exists(args.ns):
        sys.exit("%s not found: build it with tools\\build_neverscript.ps1" % args.ns)
    if not pre_path(args, "qb.pre"):
        sys.exit("%s: THUG1's Data\\pre not found" % args.thug1)
    structs = level_structs(args)
    # (--static: only the geometry's nodes, restarts, rails and kill zones, as before the scripts were brought)
    port = None if args.static else thug1_port.Thug1Port(args.thug2, pre_path(args, "qb.pre"))
    wanted = args.levels or [code for code, _, _, _, _, default in LEVELS if default]
    if os.path.isdir(out):
        shutil.rmtree(out)
    os.makedirs(out)
    manifest = {"levels": []}
    report = []
    for requested in wanted:
        info = BY_CODE.get(requested.lower())
        if not info:
            sys.exit("%s: not a THUG1 level this tool knows (%s)" % (requested, ", ".join(c for c, *_ in LEVELS)))
        code, sign, location, icon, level_num, _ = info
        print("converting", code)
        files = level_files(args, code)
        struct = structs.get(code.lower())
        name = "thug1_" + code.lower()
        level_dir = os.path.join(out, "levels", name)
        node_report = []
        triggers, starts = convert_level_nodes(files, code, name, args.ns, level_dir, node_report, port, struct)
        convert_geometry(files, code, level_dir, name, report, keep_triggers=lambda checksum: checksum in triggers)
        report += node_report

        # The sky: Levels.qb's sky=, else <code>_sky when the scene archive has one.
        sky = None
        sky_code = struct.get("sky") if struct and isinstance(struct.get("sky"), str) else code + "_sky"
        sky_base = "levels\\%s\\%s" % (sky_code.lower(), sky_code.lower())
        if sky_base + ".scn.xbx" in files:
            sky = "thug1_" + sky_code.lower()
            convert_geometry(files, sky_code, os.path.join(out, "levels", sky), sky, report)
        display = struct.get("name") if struct and isinstance(struct.get("name"), str) else sign.title()
        entry = {
            "folder": code,
            "level": name,
            "sky": sky,
            "display": "THUG1 " + sign,
            "name": display,
            "game": "THUG1",
            "location": location,
            "icon": icon,
            "level_num": level_num,
            "default_stats": struct.get("default_stats") if struct and isinstance(struct.get("default_stats"), int)
            else 6,
            "lighting": common.lighting(struct) if struct else {},
        }
        entry.update(starts)   # (startup_script / setup_script: THUG1's own, brought along)
        manifest["levels"].append(entry)
    ported_levels.write_manifest(os.path.join(out, ported_levels.MANIFEST), manifest)
    print("\n".join(report))
    print("wrote %s (%d levels); install with tools\\menu_level\\make_menu_level.py" % (out, len(manifest["levels"])))


# --- self-check -------------------------------------------------------------------------------------------------

def cmd_selfcheck(args):
    pre = os.path.join(args.thug2, "Data", "pre")
    checked, failures = common.thug2_round_trips(
        pre, ("LAscn.prx", "LAcol.prx", "TestLevelscn.prx", "TestLevelcol.prx") + tuple(args.extra))
    thug1_pre = os.path.join(args.thug1, "Game", "Data", "pre")
    # Every level file of every THUG1 level archive (scenes, textures, collision, the levels' qb files).
    for archive in sorted(os.listdir(thug1_pre)):
        low_archive = archive.lower()
        if not low_archive.endswith(".pre") or low_archive in ("qb.pre",):
            continue
        for name, data in prx.entries(os.path.join(thug1_pre, archive),
                                      lambda n: n.lower().replace("/", "\\").startswith("levels\\")):
            low = name.lower()
            try:
                if low.endswith(".scn.xbx"):
                    s = thug1.read_scene(data, name)
                    what = "%d materials, %d sectors" % (len(s["materials"]), len(s["sectors"]))
                elif low.endswith(".col.xbx"):
                    c = thug1.read_col(data, name)
                    for o in c["objects"]:
                        thug1.check_tree(o, name)
                    what = "%d objects" % len(c["objects"])
                elif low.endswith(".tex.xbx"):
                    t = tex.read_tex(data, name)
                    if tex.write_tex(t) != data:
                        raise FormatError("not the same written again")
                    what = "%d textures" % len(t["textures"])
                elif low.endswith(".qb"):
                    q = qb.QbFile(data, name, weights=True)
                    what = "%d globals" % len(q.globals)
                else:
                    continue
            except (FormatError, ValueError, IndexError) as e:
                print("FAIL %s %s: %s" % (archive, name, e))
                failures += 1
                continue
            checked += 1
            print("ok   %s %s: %s" % (archive, name, what))
    print("%d files checked, %d failures" % (checked, failures))
    return 1 if failures else 0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("command", choices=("convert", "selfcheck"))
    parser.add_argument("levels", nargs="*", help="THUG1 level codes (default: the main, classic and shop levels)")
    parser.add_argument("--thug2", default=r"C:\Games\THUG2")
    parser.add_argument("--thug1", default=r"C:\Games\THUG1")
    parser.add_argument("--out", help="where convert writes (default: <thug2>\\%s)" % STAGING)
    parser.add_argument("--ns", default=common.NS)
    parser.add_argument("--static", action="store_true",
                        help="convert: no scripts, peds, vehicles or objects (only geometry, rails, restarts, kills)")
    parser.add_argument("--extra", nargs="*", default=[], help="selfcheck: more THUG2 archives (Data\\pre\\*.prx)")
    args = parser.parse_args()
    if args.command == "selfcheck":
        sys.exit(cmd_selfcheck(args))
    cmd_convert(args)


if __name__ == "__main__":
    main()
