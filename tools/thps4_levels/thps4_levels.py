# NOT ORIGINAL - converts Tony Hawk's Pro Skater 4 (PC) levels to THUG2's formats so that THUG2 can load and play
# them (see README.md). Nothing of either game is stored in the repo: the files are made from the user's own copies.
#
#   python -I tools/thps4_levels/thps4_levels.py selfcheck [--thug2 C:\Games\THUG2] [--thps4 C:\Games\THPS4]
#   python -I tools/thps4_levels/thps4_levels.py convert [levels...] [--thug2 ...] [--thps4 ...] [--out <folder>]
#   then: python -I tools/menu_level/make_menu_level.py   (installs them into the data overlay, with level select signs)
#
# convert writes <thug2>\thug2-decomp-thps4\ (not read by the game): levels\thps4_<lvl>\thps4_<lvl>.scn.xbx /
# .tex.xbx / .col.xbx / .qb (the node array), the skies the same way, and levels.json (what make_menu_level.py adds to
# Levels.qb and the level select). make_menu_level.py copies the levels folder into the overlay
# (<thug2>\thug2-decomp-data), which the DLL serves in place of the game's files.

import argparse
import glob
import json
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
REPO = os.path.dirname(os.path.dirname(HERE))

import convert  # noqa: E402
import nodes  # noqa: E402
import qb4  # noqa: E402
import tex  # noqa: E402
import thps4  # noqa: E402
import thug2_col  # noqa: E402
import thug2_scene  # noqa: E402
from binio import FormatError  # noqa: E402

STAGING = "thug2-decomp-thps4"
MANIFEST = "levels.json"

# The THPS4 levels converted by default (folder under Game\data\levels): everything with a scene except the park
# editor's sets. Foo has only a node array (no scene).
LEVELS = ["Alc", "Cnv", "Hof", "Jnk", "Kon", "Lon", "SF2", "Sch", "Zoo", "SkateShop", "Motox"]
# The level select's numbers for the signs (each sign needs its own; none of them has career records - the info
# panel skips them, as make_menu_level.py's SKATE SHOP / TEST LEVEL at 18 / 21).
SIGN_NUMBERS = [19, 20, 22, 23, 24, 25, 26, 27, 28, 29, 31]


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
    q = qb4.QbFile(open(os.path.join(thps4_dir, "Game", "data", "scripts", "Levels.qb"), "rb").read(), "Levels.qb")
    found = {}
    for key, value in q.globals:
        if isinstance(value, qb4.Struct) and isinstance(value.get("level"), str):
            found[value.get("level").lower()] = value
    return found


def ns_compile(ns, text, target):
    with tempfile.TemporaryDirectory() as work:
        source = os.path.join(work, "source.ns")
        open(source, "w", encoding="utf-8").write(text)
        result = subprocess.run([ns, "-c", source, "-o", target, "-targetGame", "thug2"], capture_output=True,
                                text=True)
        if result.returncode != 0 or not os.path.exists(target):
            sys.exit("ns failed on %s:\n%s%s" % (target, result.stdout, result.stderr))


def convert_geometry(folder, base, out_dir, out_name, report, keep_triggers=lambda checksum: False):
    files = level_files(folder, base)
    for kind in ("scn", "col", "tex"):
        if kind not in files:
            raise FormatError("%s: no %s file for %s" % (folder, kind, base))
    warnings = []
    os.makedirs(out_dir, exist_ok=True)
    scene4 = thps4.read_scene(open(files["scn"], "rb").read(), files["scn"])
    scene = convert.convert_scene(scene4, warnings)
    data = thug2_scene.write_scene(scene)
    thug2_scene.read_scene(data, out_name)  # (reads back)
    open(os.path.join(out_dir, out_name + ".scn.xbx"), "wb").write(data)

    textures = tex.read_tex(open(files["tex"], "rb").read(), files["tex"])
    converted = tex.depalettise(textures)
    tex.check_loadable(textures, files["tex"])
    open(os.path.join(out_dir, out_name + ".tex.xbx"), "wb").write(tex.write_tex(textures))

    col4 = thps4.read_col(open(files["col"], "rb").read(), files["col"])
    col = convert.convert_col(col4, keep_triggers, warnings)
    data = thug2_col.write_col(col)
    thug2_col.read_col(data, out_name)
    open(os.path.join(out_dir, out_name + ".col.xbx"), "wb").write(data)
    report.append("%s: %d materials, %d sectors (%d meshes), %d textures (%d depalettised), %d collision objects"
                  % (out_name, len(scene["materials"]), len(scene["sectors"]),
                     sum(len(s["meshes"]) for s in scene["sectors"]), len(textures["textures"]), converted,
                     len(col["objects"])))
    for warning in warnings[:10]:
        report.append("  warning: " + warning)
    if len(warnings) > 10:
        report.append("  (%d more warnings)" % (len(warnings) - 10))
    return files


def lighting(struct):
    # THPS4's level lighting (ambient + two directional lights) in THUG2's keys (each factor as a lo / hi pair).
    out = {}
    for key in ("ambient_red", "ambient_green", "ambient_blue", "heading_0", "pitch_0", "red_0", "green_0", "blue_0",
                "heading_1", "pitch_1", "red_1", "green_1", "blue_1"):
        value = struct.get(key)
        if isinstance(value, (int, float)):
            out[key] = int(round(value))
    for key, new in (("ambient_mod_factor", "ambient_mod_factor"), ("mod_factor_0", "mod_factor_0"),
                     ("mod_factor_1", "mod_factor_1")):
        value = struct.get(key)
        if isinstance(value, (int, float)):
            out[new + "_lo"] = round(float(value), 4)
            out[new + "_hi"] = round(float(value), 4)
    return out


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
        convert_geometry(folder, base, os.path.join(out, "levels", name), name, report)

        # The node array.
        files = level_files(folder, base)
        q = qb4.QbFile(open(files["qb"], "rb").read(), files["qb"])
        array = q.get("NodeArray")
        if not isinstance(array, list):
            raise FormatError("%s: no NodeArray" % files["qb"])
        names = dict(q.names)
        converted, stats = nodes.convert_nodes(array, names)
        text = nodes.node_array_ns(name + "_NodeArray", converted, names)
        ns_compile(args.ns, text, os.path.join(out, "levels", name, name + ".qb"))
        report.append("  nodes: %d of %d kept %s" % (len(converted), len(array), stats))

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
        entry = {
            "folder": base,
            "level": name,
            "sky": sky,
            "display": "THPS4 " + display.upper(),
            "name": display,
            "level_num": SIGN_NUMBERS[index % len(SIGN_NUMBERS)],
            "default_stats": struct.get("default_stats") if struct and isinstance(struct.get("default_stats"), int)
            else 6,
            "lighting": lighting(struct) if struct else {},
        }
        manifest["levels"].append(entry)
    json.dump(manifest, open(os.path.join(out, MANIFEST), "w", encoding="utf-8"), indent=1)
    print("\n".join(report))
    print("wrote %s (%d levels); install with tools\\menu_level\\make_menu_level.py" % (out, len(manifest["levels"])))


# --- what make_menu_level.py adds -------------------------------------------------------------------------------

def load_manifest(game):
    path = os.path.join(game, STAGING, MANIFEST)
    if not os.path.exists(path):
        return None
    return json.load(open(path, encoding="utf-8"))


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


def install_files(game, overlay):
    # The converted levels into the overlay's levels folder.
    source = os.path.join(game, STAGING, "levels")
    for entry in os.listdir(source):
        target = os.path.join(overlay, "levels", entry)
        if os.path.isdir(target):
            shutil.rmtree(target)
        shutil.copytree(os.path.join(source, entry), target)
    return sorted(os.listdir(source))


# --- self-check -------------------------------------------------------------------------------------------------

def prx_entries(path):
    sys.path.insert(0, os.path.join(REPO, "tools", "menu_level"))
    from make_menu_level import lzss_decode
    import struct
    data = open(path, "rb").read()
    _, _, count = struct.unpack_from("<iii", data, 0)
    offset = 12
    for _ in range(count):
        size, compressed, name_size, _, _ = struct.unpack_from("<iihHI", data, offset)
        stored = compressed or size
        body = offset + 16 + name_size
        name = data[offset + 16:body].split(b"\0")[0].decode("ascii", "replace")
        raw = data[body:body + stored]
        yield name, (lzss_decode(raw, size) if compressed else raw)
        offset = body + ((stored + 3) & ~3)


def cmd_selfcheck(args):
    failures = 0
    pre = os.path.join(args.thug2, "Data", "pre")
    checked = 0
    for archive in ("LAscn.prx", "LAcol.prx", "TestLevelscn.prx", "TestLevelcol.prx") + tuple(args.extra):
        for name, data in prx_entries(os.path.join(pre, archive)):
            low = name.lower()
            try:
                if low.endswith(".scn.xbx"):
                    again = thug2_scene.write_scene(thug2_scene.read_scene(data, name))
                elif low.endswith(".col.xbx"):
                    again = thug2_col.write_col(thug2_col.read_col(data, name))
                elif low.endswith(".tex.xbx"):
                    again = tex.write_tex(tex.read_tex(data, name))
                else:
                    continue
            except FormatError as e:
                print("FAIL %s: %s" % (name, e))
                failures += 1
                continue
            checked += 1
            same = again == data
            print("%s %s (%d bytes) read and written again" % ("ok  " if same else "FAIL", name, len(data)))
            failures += not same
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
                    q = qb4.QbFile(data, path)
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
    parser.add_argument("--ns", default=os.path.join(REPO, "out", "neverscript", "ns.exe"))
    parser.add_argument("--extra", nargs="*", default=[], help="selfcheck: more THUG2 archives (Data\\pre\\*.prx)")
    args = parser.parse_args()
    if args.command == "selfcheck":
        sys.exit(cmd_selfcheck(args))
    cmd_convert(args)


if __name__ == "__main__":
    main()
