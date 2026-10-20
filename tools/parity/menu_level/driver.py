# NOT ORIGINAL - the Python half of tools/menu_level's unit-level parity cases (cases.ts; driver.ts the same in
# TypeScript): dumps what career_complete / qb_walk, level_select and level_select_icons work out from a workspace's
# files (read only) into <out>, as text both languages write the same.
#
#   python -I tools/parity/menu_level/driver.py career|map|sections|signs <workspace> <ns.exe> <out>

import os
import subprocess
import sys
import tempfile

TOOLS = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(TOOLS, "level_port"))
sys.path.insert(0, os.path.join(TOOLS, "menu_level"))
import career_complete  # noqa: E402
import level_select  # noqa: E402
import level_select_icons  # noqa: E402
import make_menu_level  # noqa: E402
import ported_levels  # noqa: E402


def decompiler(ns, work):
    def decompile(data, name):
        path = os.path.join(work, name)
        open(path, "wb").write(data)
        subprocess.run([ns, "-d", path, "-o", path + ".ns"], check=True, capture_output=True)
        return open(path + ".ns", encoding="utf-8").read()
    return decompile


def write(out, name, text):
    with open(os.path.join(out, name), "w", encoding="utf-8", newline="\n") as f:
        f.write(text)


def career(ws, ns, out):
    pre = os.path.join(ws, "Data", "pre")
    with tempfile.TemporaryDirectory() as work:
        decompile = decompiler(ns, work)
        qb_scripts = os.path.join(pre, "qb_scripts.prx")

        def script_file(name):
            return decompile(make_menu_level.prx_file(qb_scripts, name), os.path.basename(name))

        def level_scripts(prefix, qb, pre_name):
            return decompile(make_menu_level.prx_file(os.path.join(pre, os.path.splitext(pre_name)[0] + ".prx"), qb),
                             prefix + "_scripts.qb")

        def level_qbs(pre_name):
            return make_menu_level.prx_files(os.path.join(pre, os.path.splitext(pre_name)[0] + ".prx"))

        levels = script_file(r"scripts\game\levels.qb")
        data = career_complete.career_data(levels, script_file(r"scripts\game\menu\chapter_info.qb"),
                                           script_file(r"scripts\game\goal_utilities.qb"),
                                           script_file(r"scripts\game\menu\stats.qb"), level_scripts,
                                           script_file(r"scripts\game\menu\gamemenu_levelselect.qb"),
                                           make_menu_level.prx_files(qb_scripts), level_qbs)
        lines = [career_complete.describe(data)]
        for level in data["levels"]:
            lines.append("level %s %s %d %s %s %s total=%d points=%d" % (level["load"], level["prefix"], level["number"],
                                                                         level["qb"], level["pre"], level["goals_root"],
                                                                         level["total"], level["points"]))
            lines += ["  goal %s %d" % goal for goal in level["goals"]]
        lines += ["flag %s" % flag for flag in data["flags"]]
        lines += ["trick %s %s" % trick for trick in data["tricks"]]
        lines.append("final chapter %d, stats %s" % (data["final_chapter"], " ".join(data["stats"])))
        for prefix, number in data["classic_levels"]:
            lines.append("classic %s %d: %s" % (prefix, number, " ".join(data["classic"][number])))
        lines.append("classic goals: %d" % sum(len(v) for v in data["classic"].values()))
        for prefix, number in data["gap_levels"]:
            lines.append("gaps %s %d: %d" % (prefix, number, len(data["gaps"][number])))
            for name, score, pos in data["gaps"][number]:
                lines.append("  %s|%d|%s" % (name, score, ",".join(repr(v) for v in pos) if pos else "None"))
        lines.append("gaps: %d over %d levels" % (sum(len(v) for v in data["gaps"].values()), len(data["gaps"])))
        write(out, "career.txt", "\n".join(lines) + "\n")
        write(out, "apply.ns", career_complete.apply_script(data))
        options = decompile(make_menu_level.prx_file(os.path.join(pre, "mainmenu_scripts.prx"),
                                                     make_menu_level.OPTIONS_MENU_QB), "mainmenu_options.qb")
        write(out, "options.ns", career_complete.options_menu(options, data))


def map_positions(out):
    lines = []
    for i in range(-24, 25):
        for j in range(-16, 17):
            lon, lat = i * 7.5 + 0.13, j * 5.0 - 0.07
            lines.append("%r %r -> %r" % (lon, lat, level_select.map_position(lon, lat)))
    for name, (lon, lat) in level_select.PLACES.items():
        lines.append("%s -> %r" % (name, level_select.map_position(lon, lat)))
    write(out, "map.txt", "\n".join(lines) + "\n")


def sections(ws, ns, out):
    manifest = ported_levels.load_manifest(ws)
    by_game = {}
    for entry in (manifest or {"levels": []})["levels"]:
        by_game.setdefault(entry["staging"], {"levels": []})["levels"].append(entry)
    games = {"thps4": "thug2-decomp-thps4", "thug1": "thug2-decomp-thug1", "thps3": "thug2-decomp-thps3",
             "thaw": "thug2-decomp-thaw"}
    pre = os.path.join(ws, "Data", "pre")
    with tempfile.TemporaryDirectory() as work:
        decompile = decompiler(ns, work)
        qb_scripts = os.path.join(pre, "qb_scripts.prx")
        select = decompile(make_menu_level.prx_file(qb_scripts, r"scripts\game\menu\gamemenu_levelselect.qb"),
                           "levelselect.qb")
        sprites = decompile(make_menu_level.prx_file(qb_scripts, r"scripts\game\menu\sprites.qb"), "sprites.qb")
    for n, subset in enumerate([[], ["thps4"], ["thug1", "thaw"], ["thps3"], list(games)]):
        section_list = level_select.sections(**{g: by_game.get(games[g]) for g in subset})
        write(out, "describe_%d.txt" % n, level_select.describe(section_list) + "\n")
        write(out, "signs_%d.txt" % n, "\n".join("%s %s" % kv for kv in sorted(level_select.new_signs(section_list).items())) + "\n")
        edited = level_select.edit_level_select(make_menu_level.add_skateshop_sign(select), section_list)
        write(out, "levelselect_%d.ns" % n, edited)
        write(out, "sprites_%d.ns" % n, level_select.edit_sprites(sprites, section_list))


def signs(ws, out):
    frame = make_menu_level.prx_file(os.path.join(ws, "Data", "pre", "Levelselect.prx"),
                                     level_select_icons.FRAME_SOURCE)
    for name in level_select_icons.PICTOGRAMS:
        open(os.path.join(out, "sign_%s.img.xbx" % name), "wb").write(level_select_icons.make_sign(frame, name))
        ink = level_select_icons.coverage(level_select_icons.PICTOGRAMS[name]())
        write(out, "ink_%s.txt" % name, " ".join(repr(v) for v in ink) + "\n")


def main():
    what, ws, ns, out = sys.argv[1:5]
    if what == "career":
        career(ws, ns, out)
    elif what == "map":
        map_positions(out)
    elif what == "sections":
        sections(ws, ns, out)
    else:
        signs(ws, out)


if __name__ == "__main__":
    main()
