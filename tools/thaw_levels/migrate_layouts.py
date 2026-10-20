# NOT ORIGINAL - moves the level editor's saved layouts of the THAW levels (<game>\thug2-decomp-layouts\<scene
# checksum>.txt, src/game/front/level_editor_layout.cpp) from the levels as this converter made them before THUGPRO's
# frame and names (thaw_<xx>, recentred by thaw_levels.recentre_shift) to the levels as it makes them now
# (level_name, frame_shift): the file renamed after the new scene's checksum, its scene line changed and every position
# moved by the difference between the two frames.
#
#   python -I tools/thaw_levels/migrate_layouts.py --game C:\Games\THUG2-1 [--dry-run] [levels...]
#
# A layout's records (all world coordinates):
#   sector / rail / clone <checksum> <quarters> <offset x y z> ...: x' = Q x + offset (Q: quarter turns about y,
#       about the world origin) - in a frame moved by d: offset' = offset + d - Q d
#   object <id> <x y z> <rows...> / copy <name> <source> <x y z> <rows...>: the position moved by d
# The old file is kept as <name>.txt.before-thugpro-frame; a level whose new file already exists is skipped.

import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "level_port"))

import qb  # noqa: E402
import thaw  # noqa: E402
from thaw_levels import (LEVELS, find_path, frame_shift, level_name, recentre_shift, zone_array, zone_files,  # noqa: E402
                         zone_pak)

FOLDER = "thug2-decomp-layouts"
KEEP = ".before-thugpro-frame"


def quarter_turn(v, quarters):
    # As the level editor's QuarterTurn (level_editor_scenery.cpp).
    x, y, z = v
    q = quarters & 3
    if q == 1:
        return (z, y, -x)
    if q == 2:
        return (-x, y, -z)
    if q == 3:
        return (-z, y, x)
    return v


def number(v):
    return "%.9g" % v


def migrate_text(text, old_scene, new_scene, d, new_name=None):
    # The layout's text with its scene and positions changed. Returns (text, records moved).
    out, moved = [], 0
    for line in text.splitlines(keepends=True):
        body, hash_, comment = line.partition("#")
        ending = "\n" if line.endswith("\n") else ""
        words = body.split()
        if not words:
            out.append(line)
            continue
        kind = words[0]
        if kind == "scene":
            if int(words[1], 16) != old_scene:
                raise ValueError("scene %s, not %08X" % (words[1], old_scene))
            words[1] = "%08X" % new_scene
            if new_name and hash_:
                comment = " " + new_name
        elif kind in ("sector", "rail", "clone"):
            q = int(words[2])
            offset = tuple(float(w) for w in words[3:6])
            turned = quarter_turn(d, q)
            words[3:6] = [number(offset[k] + d[k] - turned[k]) for k in range(3)]
            moved += 1
        elif kind in ("object", "copy"):
            at = 2 if kind == "object" else 3
            words[at:at + 3] = [number(float(words[at + k]) + d[k]) for k in range(3)]
            moved += 1
        rebuilt = " ".join(words)
        if hash_:
            rebuilt += "  #" + comment.rstrip("\r\n")
        out.append(rebuilt + ending)
    return "".join(out), moved


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("levels", nargs="*", help="THAW zones (default: all)")
    parser.add_argument("--game", required=True, help="the game folder whose thug2-decomp-layouts to migrate")
    parser.add_argument("--thaw", default=r"C:\Games\THAW")
    parser.add_argument("--recentre", type=float, default=20000.0, help="the old frame's --recentre")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    folder = find_path(args.game, FOLDER) or find_path(args.game, "Game", FOLDER)
    if not folder:
        print("no %s in %s: nothing to migrate" % (FOLDER, args.game))
        return 0
    names = thaw.load_names(args.thaw)
    wanted = {x.lower() for x in args.levels}
    for zone, *_ in LEVELS:
        if wanted and zone.lower() not in wanted and zone.lower()[2:] not in wanted:
            continue
        old_name, new_name = level_name(zone, legacy=True), level_name(zone)
        old_scene, new_scene = qb.checksum(old_name), qb.checksum(new_name)
        old_path = os.path.join(folder, "%08X.txt" % old_scene)
        new_path = os.path.join(folder, "%08X.txt" % new_scene)
        if not os.path.exists(old_path):
            continue
        if os.path.exists(new_path):
            print("%s: %s already has a layout - %s left as it is" % (zone, new_name, os.path.basename(old_path)))
            continue
        files = zone_files(zone_pak(args, zone))
        old = recentre_shift(thaw.read_col(files[".col"], zone + ".col"), args.recentre)
        new = frame_shift(zone, zone_array(files, zone, names), names)
        d = tuple(new[k] - old[k] for k in range(3))
        text = open(old_path, encoding="utf-8").read()
        changed, moved = migrate_text(text, old_scene, new_scene, d, new_name)
        print("%s: %s -> %s, moved by (%g, %g, %g): %d records%s"
              % (zone, old_name, new_name, d[0], d[1], d[2], moved, " (dry run)" if args.dry_run else ""))
        if not args.dry_run:
            open(new_path, "w", encoding="utf-8", newline="").write(changed)
            os.replace(old_path, old_path + KEEP)
    return 0


if __name__ == "__main__":
    sys.exit(main())
