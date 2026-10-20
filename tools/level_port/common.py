# (Has a TypeScript twin, common.ts, which the TypeScript tools use: until this file is removed - once the Python
# tools importing it are ported, docs/typescript.md - change both, and run tools/parity's cases.)
# What the level converters (tools/thps4_levels, tools/thug1_levels) share besides the formats: compiling NeverScript,
# writing a converted level's files (each read back first), the level structure's lighting keys, and the self-check's
# THUG2 round trips.

import os
import subprocess
import sys
import tempfile

import prx
import tex
import thug2_col
import thug2_scene
from binio import FormatError

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
NS = os.path.join(REPO, "out", "neverscript", "ns.exe")


def ns_compile(ns, text, target):
    with tempfile.TemporaryDirectory() as work:
        source = os.path.join(work, "source.ns")
        open(source, "w", encoding="utf-8").write(text)
        result = subprocess.run([ns, "-c", source, "-o", target, "-targetGame", "thug2"], capture_output=True,
                                text=True)
        if result.returncode != 0 or not os.path.exists(target):
            sys.exit("ns failed on %s:\n%s%s" % (target, result.stdout, result.stderr))


def write_level(out_dir, out_name, scene, textures, col):
    # <out_dir>\<out_name>.scn.xbx / .tex.xbx / .col.xbx (none when col is None: THUG1's skies have no collision,
    # like THUG2's), each read back by THUG2's readers first.
    os.makedirs(out_dir, exist_ok=True)
    data = thug2_scene.write_scene(scene)
    thug2_scene.read_scene(data, out_name)
    open(os.path.join(out_dir, out_name + ".scn.xbx"), "wb").write(data)
    tex.check_loadable(textures, out_name)
    data = tex.write_tex(textures)
    tex.read_tex(data, out_name)
    open(os.path.join(out_dir, out_name + ".tex.xbx"), "wb").write(data)
    if col is None:
        return
    data = thug2_col.write_col(col)
    thug2_col.read_col(data, out_name)
    open(os.path.join(out_dir, out_name + ".col.xbx"), "wb").write(data)


def report_level(report, out_name, scene, textures, col, warnings, converted=0):
    report.append("%s: %d materials, %d sectors (%d meshes), %d textures (%d depalettised), %d collision objects"
                  % (out_name, len(scene["materials"]), len(scene["sectors"]),
                     sum(len(s["meshes"]) for s in scene["sectors"]), len(textures["textures"]), converted,
                     len(col["objects"]) if col else 0))
    for warning in warnings[:10]:
        report.append("  warning: " + warning)
    if len(warnings) > 10:
        report.append("  (%d more warnings)" % (len(warnings) - 10))


def lighting(struct):
    # A level structure's lighting (ambient + two directional lights) in THUG2's keys (each mod factor as a lo / hi
    # pair, as THUG2's own levels have them).
    out = {}
    for key in ("ambient_red", "ambient_green", "ambient_blue", "heading_0", "pitch_0", "red_0", "green_0", "blue_0",
                "heading_1", "pitch_1", "red_1", "green_1", "blue_1"):
        value = struct.get(key)
        if isinstance(value, (int, float)):
            out[key] = int(round(value))
    for key in ("ambient_mod_factor", "mod_factor_0", "mod_factor_1"):
        value = struct.get(key)
        if isinstance(value, (int, float)):
            out[key + "_lo"] = round(float(value), 4)
            out[key + "_hi"] = round(float(value), 4)
        else:
            for suffix in ("_lo", "_hi"):
                value = struct.get(key + suffix)
                if isinstance(value, (int, float)):
                    out[key + suffix] = round(float(value), 4)
    return out


def thug2_round_trips(pre, archives):
    # THUG2's scene / collision / texture files in the archives read and written again: they must come out
    # byte-identical. Returns (files checked, failures).
    checked = failures = 0
    for archive in archives:
        for name, data in prx.entries(os.path.join(pre, archive)):
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
    return checked, failures
