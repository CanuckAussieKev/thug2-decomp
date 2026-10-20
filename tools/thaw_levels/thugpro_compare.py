# NOT ORIGINAL - compares our converted THAW levels with THUGPRO's own ports of them, read from the user's THUGPRO
# install (read only: <thugpro>\data\pre\<level>_scripts.prx and <level>col.prx), for online play between the two
# (docs/pending/thaw.md "THUGPRO"). What has to agree for a THUGPRO player and one of ours to share a level:
#   - the load script's name (the change-level message 0x46 / start info 0x33 carry its checksum): Load_<level>
#     (ours: thaw_levels.level_name; THUGPRO's: THUGPRO_NAMES, only to find its files - reported when they differ);
#   - the world frame: skater updates carry world positions, so every node (and the scene / collision with them) has
#     to sit where THUGPRO's does;
#   - the King of the Hill crowns in the same order (the crown is picked by its index among the Crown nodes) and at
#     the same places; the game-mode nodes under the names THUG2's scripts look for (TRG_CTF_Red...).
# Also reported: the multiplayer restarts (order and places), and how many of each side's collision faces can't be
# found through their BSP trees (tools/level_port/col_check.py).
#
#   python -I tools/thaw_levels/thugpro_compare.py [levels...] [--thug2 C:\Games\THUG2-1] [--thugpro <dir>] [--json out]

import argparse
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "level_port"))

import col_check  # noqa: E402
import prx  # noqa: E402
import qb  # noqa: E402
import thug2_col  # noqa: E402
from thaw_levels import LEVELS, STAGING, find_path, level_name  # noqa: E402

# THUGPRO's names for the levels it renamed (its data\pre file names; the others are the zone's): to find its files.
THUGPRO_NAMES = {"Z_CH": "skatepark", "Z_JA": "kyoto", "Z_SZ": "sz", "Z_SV": "vans"}
DEFAULT_THUGPRO = os.path.join(os.environ.get("LOCALAPPDATA", ""), "THUG Pro")
TOLERANCE = 0.1     # units (the frames differ by under 1/32: ours in whole sixteenths)
CTF_NAMES = ["TRG_CTF_%s%s" % (c, s) for c in ("Red", "Blue", "Green", "Yellow") for s in ("", "_Base")] + \
            ["TRG_Flag_%s" % c for c in ("Red", "Blue", "Green", "Yellow")]


def node_array(data, label):
    # The file's node array, each node's compressed components (a flag naming a global structure: THUG2's
    # ncomp_* - THUGPRO's compiler makes them) merged in.
    q = qb.QbFile(data, label, weights=True)  # (THUG2 random tokens carry weights)
    array = next((v for k, v in q.globals if isinstance(v, list) and
                  q.names.get(k.value, "").lower().endswith("_nodearray")), None)
    if array is None:
        array = next((v for k, v in q.globals if isinstance(v, list) and v and isinstance(v[0], qb.Struct)), [])
    structs = {k.value: v for k, v in q.globals if isinstance(v, qb.Struct)}
    out = []
    for node in array:
        if isinstance(node, qb.Struct):
            entries = []
            for key, value in node.entries:
                if key is None and isinstance(value, qb.Name) and value.value in structs:
                    entries += structs[value.value].entries
                else:
                    entries.append((key, value))
            node = qb.Struct(entries)
        out.append(node)
    return out, q.names


def position(node):
    # In-game position: THUG2 takes Pos as it is and negates z of the older Position.
    v = node.get("Pos")
    if isinstance(v, tuple) and v[0] == "vector":
        return v[1]
    v = node.get("Position")
    if isinstance(v, tuple) and v[0] == "vector":
        return (v[1][0], v[1][1], -v[1][2])
    return None


def name_of(node):
    v = node.get("Name")
    return v.value if isinstance(v, qb.Name) else None


def is_crown(node):
    return node.get("Type") == qb.Name(qb.checksum("Crown"))


def is_mp_restart(node):
    types = node.get("restart_types")
    return node.get("Class") == qb.Name(qb.checksum("Restart")) and isinstance(types, list) and \
        qb.Name(qb.checksum("Multiplayer")) in types


def sequence(array, test):
    return [(name_of(n), position(n)) for n in array if isinstance(n, qb.Struct) and test(n)]


def same_sequence(a, b):
    # (verdict, differing count)
    if len(a) != len(b):
        return "count %d vs %d" % (len(a), len(b)), max(len(a), len(b))
    bad = 0
    for (_, pa), (_, pb) in zip(a, b):
        if pa is None or pb is None or max(abs(pa[k] - pb[k]) for k in range(3)) > TOLERANCE:
            bad += 1
    return ("same" if not bad else "%d of %d differ" % (bad, len(a))), bad


def prx_file(folder, wanted):
    path = find_path(folder, "data", "pre", wanted)
    return path


def compare(zone, ours_dir, thugpro):
    mine = level_name(zone)
    key = THUGPRO_NAMES.get(zone, zone.lower())
    out = {"zone": zone, "level": mine, "thugpro_level": key, "same_name": mine == key}
    ours_qb = os.path.join(ours_dir, mine, mine + ".qb")
    if not os.path.exists(ours_qb):
        out["error"] = "not converted (%s)" % ours_qb
        return out
    ours, names = node_array(open(ours_qb, "rb").read(), ours_qb)
    scripts = prx_file(thugpro, key + "_scripts.prx")
    if not scripts:
        out["error"] = "THUGPRO has no %s_scripts.prx" % key
        return out
    data = prx.find(scripts, "levels\\%s\\%s.qb" % (key, key))
    theirs, their_names = node_array(data, key + ".qb")
    names.update(their_names)
    # positions by name
    by_name = {}
    for n in theirs:
        if isinstance(n, qb.Struct) and name_of(n) is not None and position(n) is not None:
            by_name.setdefault(name_of(n), position(n))
    shared = same = 0
    worst = (0.0, None)
    for n in ours:
        if not isinstance(n, qb.Struct):
            continue
        p, nm = position(n), name_of(n)
        if p is None or nm not in by_name:
            continue
        shared += 1
        d = max(abs(p[k] - by_name[nm][k]) for k in range(3))
        if d <= TOLERANCE:
            same += 1
        elif d > worst[0]:
            worst = (d, names.get(nm, "%08X" % nm))
    out.update(nodes_ours=len(ours), nodes_thugpro=len(theirs), shared=shared, same_place=same,
               worst=[round(worst[0], 3), worst[1]])
    out["crowns"] = same_sequence(sequence(ours, is_crown), sequence(theirs, is_crown))[0]
    out["mp_restarts"] = same_sequence(sequence(ours, is_mp_restart), sequence(theirs, is_mp_restart))[0]
    our_names = {name_of(n) for n in ours if isinstance(n, qb.Struct)}
    their_names_set = {name_of(n) for n in theirs if isinstance(n, qb.Struct)}
    out["ctf_ours"] = sum(qb.checksum(c) in our_names for c in CTF_NAMES)
    out["ctf_thugpro"] = sum(qb.checksum(c) in their_names_set for c in CTF_NAMES)
    # collision
    our_col = os.path.join(ours_dir, mine, mine + ".col.xbx")
    if os.path.exists(our_col):
        faces, missing, _ = col_check.check(thug2_col.read_col(open(our_col, "rb").read(), our_col))
        out["col_ours"] = [faces, missing]
    their_col = prx_file(thugpro, key + "col.prx")
    if their_col:
        for name, cdata in col_check.col_files(their_col):
            faces, missing, _ = col_check.check(thug2_col.read_col(cdata, name))
            out["col_thugpro"] = [faces, missing]
    return out


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("levels", nargs="*")
    parser.add_argument("--thug2", default=r"C:\Games\THUG2\Game")
    parser.add_argument("--thugpro", default=DEFAULT_THUGPRO)
    parser.add_argument("--staging", help="our converted levels (default <thug2>\\%s\\levels)" % STAGING)
    parser.add_argument("--json")
    args = parser.parse_args()
    ours_dir = args.staging or os.path.join(args.thug2, STAGING, "levels")
    zones = [z for z, *_ in LEVELS]
    if args.levels:
        zones = [z for z in zones if z.lower() in {x.lower() for x in args.levels} or
                 z.lower()[2:] in {x.lower() for x in args.levels}]
    results = []
    bad = 0
    for zone in zones:
        r = compare(zone, ours_dir, args.thugpro)
        results.append(r)
        if "error" in r:
            print("%-6s %-10s %s" % (zone, r["level"], r["error"]))
            bad += 1
            continue
        print("%-6s %-6s%s nodes %d/%d, %d shared, %d at the same place (worst %.3f %s); crowns %s; mp restarts %s; "
              "ctf nodes %d/%d; collision unreachable ours %s, THUGPRO %s"
              % (zone, r["level"], "" if r["same_name"] else " (THUGPRO: %s)" % r["thugpro_level"], r["nodes_ours"], r["nodes_thugpro"], r["shared"], r["same_place"], r["worst"][0],
                 r["worst"][1] or "", r["crowns"], r["mp_restarts"], r["ctf_ours"], r["ctf_thugpro"],
                 r.get("col_ours", ["-", "-"])[1], r.get("col_thugpro", ["-", "-"])[1]))
        if r["shared"] - r["same_place"] > r["shared"] // 100 or r["crowns"] != "same":
            bad += 1
    if args.json:
        json.dump(results, open(args.json, "w", encoding="utf-8"), indent=1)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
