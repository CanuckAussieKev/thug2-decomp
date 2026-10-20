# NOT ORIGINAL - the Python half of tools/level_port's parity driver (driver.ts is the TypeScript half): each command
# runs the library's functions on real game data and writes what they made (canonical dumps, written files, NeverScript
# text) into <out>, for tools/parity/parity.ts to compare with the TypeScript twin's. Kept while tools/level_port/*.py
# exist (docs/typescript.md).
#
#   python -I tools/parity/level_port/driver.py <command> <out> [inputs...]
#
# commands: archives, files, qb, thug2, translate, ground, lighting, manifest, geometry (its inputs: driver.ts
# prepare), colgeometry, compat, common

import hashlib
import json
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.dirname(HERE))
sys.path.insert(0, os.path.join(TOOLS, "level_port"))
sys.path.insert(0, os.path.join(TOOLS, "compat"))

import canon as C  # noqa: E402
import col_check  # noqa: E402
import common  # noqa: E402
import compat  # noqa: E402
import geometry  # noqa: E402
import nodes  # noqa: E402
import peds_cars  # noqa: E402
import ported_levels  # noqa: E402
import prx  # noqa: E402
import qb  # noqa: E402
import script_port  # noqa: E402
import tex  # noqa: E402
import thug2_col  # noqa: E402
import thug2_scene  # noqa: E402
from binio import FormatError  # noqa: E402

NS = r"C:\Dev\thug2-decomp\out\neverscript\ns.exe"


def write(out, name, text):
    path = os.path.join(out, name)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)


def error_line(e):
    return "error FormatError %s" % e if isinstance(e, FormatError) else "error"


def inputs(paths, suffixes):
    # (label, data) of the loose files and the archives' files with these suffixes, in the order given.
    for path in paths:
        low = path.lower()
        if low.endswith((".prx", ".pre")):
            for name, data in prx.entries(path, lambda n: n.lower().endswith(suffixes)):
                yield os.path.basename(path) + "|" + name, data
        elif low.endswith(suffixes):
            yield os.path.basename(path), open(path, "rb").read()


def qb_norm(data):
    # A compiled .qb as text without NeverScript's own nondeterminism: ns.exe writes its 0x2B checksum names in Go map
    # order (different every run) - the code before them as it is, the names sorted, the rest after.
    toks = list(qb.tokens(data, weights=True))
    first = next((off for off, t, _ in toks if t == 0x2B), len(data))
    names = sorted(p.hex() for _, t, p in toks if t == 0x2B)
    rest = [bytes([t]).hex() + p.hex() for off, t, p in toks if off >= first and t != 0x2B]
    return "%s\n%s\n%s\n" % (data[:first].hex(), "\n".join(names), "".join(rest))


def safe(label):
    return "".join(c if c.isalnum() or c in "._-" else "_" for c in label)


# --- archives, scene / collision / texture files --------------------------------------------------------------------

def cmd_archives(out, paths):
    lines = []
    for path in paths:
        files = list(prx.entries(path))
        lines.append("archive %s %d" % (os.path.basename(path), len(files)))
        lines += ["%s %s" % (name, C.blob(data)) for name, data in files]
        found = prx.find(path, files[len(files) // 2][0].upper().replace("\\", "/")) if files else None
        lines.append("find %s" % (C.blob(found) if found is not None else None))
    write(out, "archives.txt", "\n".join(lines) + "\n")


def cmd_files(out, paths):
    lines = []
    for label, data in inputs(paths, (".scn.xbx", ".mdl.xbx", ".skin.xbx", ".col.xbx", ".tex.xbx", "tex.dat")):
        low = label.lower()
        try:
            if low.endswith(".col.xbx"):
                col = thug2_col.read_col(data, label, versions=(9, 10))
                again = thug2_col.write_col(col)
                faces, missing, objects = col_check.check(col)
                lines.append("%s col %s %s same=%d unreachable=%d/%d/%d"
                             % (label, C.digest(C.canon(col)), C.blob(again), again == data, faces, missing, objects))
            elif low.endswith(("tex.xbx", "tex.dat")):
                t = tex.read_tex(data, label)
                again = tex.write_tex(t)
                line = "%s tex %s %s same=%d" % (label, C.digest(C.canon(t)), C.blob(again), again == data)
                try:
                    changed = tex.depalettise(t)
                    tex.check_loadable(t, label)
                    line += " depal=%d %s" % (changed, C.blob(tex.write_tex(t)))
                except FormatError as e:
                    line += " depal " + error_line(e)
                lines.append(line)
            else:
                s = thug2_scene.read_scene(data, label)
                again = thug2_scene.write_scene(s)
                lines.append("%s scn %s %s same=%d" % (label, C.digest(C.canon(s)), C.blob(again), again == data))
        except FormatError as e:
            lines.append("%s %s" % (label, error_line(e)))
    write(out, "files.txt", "\n".join(lines) + "\n")


# --- qb ---------------------------------------------------------------------------------------------------------------

def cmd_qb(out, weights, paths):
    weights = weights == "1"
    lines = []
    for label, data in inputs(paths, (".qb",)):
        lines.append("file %s" % label)
        try:
            toks = list(qb.tokens(data, weights))
            lines.append("tokens %d %s" % (len(toks), C.digest([[o, t, C.blob(p)] for o, t, p in toks])))
        except Exception:
            lines.append("tokens error")
        try:
            q = qb.QbFile(data, label, weights)
        except Exception as e:
            lines.append("qbfile " + error_line(e))
            q = None
        if q is not None:
            lines.append("names %d %s" % (len(q.names), C.digest(C.canon(q.names))))
            lines.append("globals %d %s" % (len(q.globals), C.digest([[C.qcanon(k), C.qcanon(v)] for k, v in q.globals])))
            lines.append("scripts %d %s" % (len(q.scripts), C.digest(
                {"%08X" % k: [[[t, C.blob(p)] for t, p in st] for st in v] for k, v in q.scripts.items()})))
            calls = {}
            for k in q.scripts:
                calls["%08X" % k] = [["%08X" % c, len(line)] for c, line in q.calls(k)]
            lines.append("calls %s" % C.digest(calls))
            text = []
            for key, value in q.globals:
                try:
                    text.append("%s = %s" % (qb.name_text(key.value, q.names), qb.to_ns(value, q.names)))
                except TypeError:
                    text.append("# %08X: no NeverScript form" % key.value)
                except UnicodeEncodeError:
                    text.append("# %08X: not ASCII" % key.value)
            write(out, os.path.join("ns", safe(label) + ".ns"), "\n".join(text) + "\n")
            for name in ("NodeArray", "TriggerScripts", "Level_AP"):
                v = q.get(name)
                lines.append("get %s %s" % (name, C.digest(C.qcanon(v)) if v is not None else None))
        try:
            s = script_port.Source(data, label, weights)
            dumped = {}
            for k, body in s.scripts.items():
                stmts = script_port.statements(body)
                dumped["%08X" % k] = [
                    [[t.t, t.i, t.targets, t.weights] for t in st] for st in stmts] + [
                    [[k2, a.t, a.i] for k2, a in script_port.args_of(st[1:])] for st in stmts] + [
                    [script_port.random_end(t, body) for t in body if t.t in script_port.RANDOMS]]
            lines.append("source %d %s globals %d %s" % (len(s.scripts), C.digest(dumped), len(s.globals),
                                                       C.digest({"%08X" % k: C.qcanon(v) for k, v in s.globals.items()})))
        except Exception as e:
            lines.append("source " + error_line(e))
    write(out, "qb.txt", "\n".join(lines) + "\n")


# --- THUG2's commands --------------------------------------------------------------------------------------------------

def cmd_thug2(out, game):
    t = script_port.Thug2(game)
    lines = ["cfuncs %d %s" % (len(t.cfuncs), C.digest(sorted(t.cfuncs))),
             "members %d %s" % (len(t.members), C.digest(sorted(t.members.items()))),
             "scripts %d %s" % (len(t.scripts), C.digest(sorted(t.scripts))),
             "globals %d %s" % (len(t.globals), C.digest(sorted(t.globals))),
             "anims %d %s" % (len(t.anims), C.digest(sorted(t.anims))),
             "bodies %s" % C.digest({"%08X" % k: sorted(v) for k, v in t.bodies.items()})]
    needs = {}
    for name in sorted(t.scripts):
        needs["%08X" % name] = sorted(t.script_needs(name))
    lines.append("needs %s" % C.digest(needs))
    lines.append("component %s" % [t.component(n) for n in sorted(t.members)[:50]])
    write(out, "thug2.txt", "\n".join(lines) + "\n")


# --- node arrays and the script translator -------------------------------------------------------------------------

def node_array_of(q, level=None):
    array = q.get("NodeArray")
    if array is None and level is not None:
        array = q.get(level + "_NodeArray")
    return array if isinstance(array, list) else []


def context_of(cls, game):
    if cls in ("levelgeometry", "railnode", "restart"):
        return script_port.SKATER
    if cls in ("gameobject", "levelobject"):
        return script_port.OBJECT
    if cls == "pedestrian":
        return script_port.PED if game == "thug1" else script_port.OBJECT
    if cls == "vehicle":
        return script_port.VEHICLE if game == "thug1" else script_port.OBJECT
    return script_port.NONE


def cmd_translate(out, game_dir, game, level, shared):
    # game: thps3 / thps4 / thug1; level: the level's .qb (a file, or archive|name); shared: the game's script files.
    weights = game == "thug1"
    if "|" in level:
        archive, name = level.split("|", 1)
        level_label, level_data = name, prx.find(archive, name)
    else:
        level_label, level_data = os.path.basename(level), open(level, "rb").read()
    q = qb.QbFile(level_data, level_label, weights)
    shared_sources, names = [], {}
    for label, data in inputs(shared, (".qb",)):
        try:
            shared_sources.append(script_port.Source(data, os.path.basename(label.replace("/", "\\").split("|")[-1]).lower(),
                                                     weights))
        except (KeyError, ValueError, IndexError, FormatError):
            continue
    for s in shared_sources:
        names.update(s.names)
    names.update(q.names)
    array = node_array_of(q)
    lines = ["array %d" % len(array)]
    if game == "thug1":
        array = nodes.expand_includes(array, {k.value: v for k, v in q.globals})
        restarts = {n.get("Name").value for n in array if isinstance(n, qb.Struct) and
                    nodes.class_of(n, names) == "restart" and isinstance(n.get("Name"), qb.Name)}
        kills = nodes.kill_commands([q], restarts, names)
    else:
        kills = nodes.kill_scripts(q, None)
    lines.append("kills %s" % sorted(("%08X" % k, v) for k, v in kills.items()))
    converted, stats = nodes.convert_nodes(array, names, kills, extra_keys=("Pos",) if game == "thug1" else ())
    lines.append("converted %d %s %s" % (len(converted), C.digest([C.qcanon(n) for n in converted]), sorted(stats.items())))
    kills = nodes.add_kill_restarts(converted, kills, names)
    lines.append("kill restarts %s" % sorted(("%08X" % k, v) for k, v in kills.items()))
    lines.append("kill triggers %s" % sorted(nodes.kill_trigger_names(converted, kills)))
    # (typos in some rails' terrains: fix_terrains' nearest names)
    rails = [n for n in converted if nodes.class_of(n, names) == "railnode"]
    terrains = sorted(t for t in nodes.THUG2_TERRAINS)
    for k, node in enumerate(rails[::7]):
        t = terrains[k % len(terrains)]
        typo = ["TERRAIN_" + t[:-1] + "X", "TERRAIN_" + t[1:], "terrain_" + t.lower() + "s", "TERRAIN_ZZZ" + t[:3]][k % 4]
        value = qb.checksum(typo)
        names[value] = typo
        node.entries = [(key, qb.Name(value) if key is not None and key.value == qb.checksum("TerrainType") else v)
                        for key, v in node.entries]
    text = nodes.node_array_ns("Parity_NodeArray", converted, names, kills)
    write(out, "nodes.ns", text)
    sectors = {n.get("Name").value for n in converted if isinstance(n.get("Name"), qb.Name) and
               nodes.class_of(n, names) == "levelgeometry" and n.get("Name").value % 3}
    stripped, cleared = nodes.strip_missing_trick_objects(text, sectors)
    write(out, "stripped.ns", stripped)
    lines.append("stripped %d" % cleared)
    # The translator, configured as each game's converter configures it (roughly).
    all_names = {n.get("Name").value for n in array if isinstance(n, qb.Struct) and isinstance(n.get("Name"), qb.Name)}
    node_names = {n.get("Name").value for n in converted if isinstance(n.get("Name"), qb.Name)}
    level_source = script_port.Source(level_data, "level", weights)
    sources = shared_sources + [level_source]
    thug2 = script_port.Thug2(game_dir)
    roots, objects = [], {}
    for n in array:
        if not isinstance(n, qb.Struct):
            continue
        cls = nodes.class_of(n, names)
        script, name = n.get("TriggerScript"), n.get("Name")
        context = context_of(cls, game)
        if isinstance(name, qb.Name) and cls in ("gameobject", "levelobject", "pedestrian", "vehicle"):
            objects[name.value] = (context, n.has_flag("CreatedAtStart")) if game == "thps4" else context
        if isinstance(script, qb.Name) and script.value not in kills:
            roots.append((script.value, context))
    allowed = set(script_port.DEFAULT_SOURCES) | {"level"}
    if game == "thps3":
        undrawn = {v for v in node_names if v % 5 == 0}
        tr = script_port.Translator(sources, thug2, lambda label: label in allowed, node_names, all_names, names,
                                    provided=set(kills), inherit_params=True, undrawn=undrawn)
    elif game == "thps4":
        tr = script_port.Translator(sources, thug2, lambda label: "goal" not in label, node_names, all_names, names,
                                    provided=set(kills), full=True, objects=objects)
    else:
        tr = script_port.Translator(sources, thug2, lambda label: "goal" not in label, node_names, all_names, names,
                                    provided=set(kills), objects=objects, strict_conditions=True, anim_check=True,
                                    kept_in=("LoadSound",), left_out_prefixes=("GoalManager_",),
                                    components={script_port.PED: ("vehicle",)}, by_name={script_port.VEHICLE: ("Obj_Hover",)},
                                    renames={"Obj_Hover": "Obj_HoverX"})
    scripts = tr.run(roots)
    for name, code in scripts.items():
        lines.append("script %08X %s %d" % (name, C.blob(code), script_port.does_something(code)))
    lines.append("left out %s" % list(tr.left_out.items()))
    lines.append("used globals %s" % sorted(tr.used_globals))
    lines.append("referenced %s created %s prefixes %s" % (sorted(tr.referenced), sorted(tr.created),
                                                          sorted(tr.created_prefixes)))
    lines.append("contexts %s" % sorted(("%08X" % k, sorted(v)) for k, v in tr.contexts.items()))
    write(out, "globals.ns", tr.globals_ns())
    compiled = os.path.join(out, "nodes.qb")
    common.ns_compile(NS, text, compiled)
    data = open(compiled, "rb").read()
    os.remove(compiled)
    full = script_port.append_scripts(data, scripts, names)
    write(out, "nodes.qb.txt", qb_norm(data))
    write(out, "full.qb.txt", qb_norm(full))
    net, changed = nodes.net_game_nodes(text, scripts)
    write(out, "net.ns", net)
    lines.append("net %d" % changed)
    absent, marked = nodes.absent_in_net_games(text)
    lines.append("absent %d %s" % (marked, C.digest(absent)))
    if game == "thps4":
        assets = peds_cars.Thug2Assets(game_dir)
        made, made_scripts, made_stats = peds_cars.convert(array, names, [q], assets.profile_exists,
                                                           assets.model_exists)
        peds_cars.offset_links(made, len(converted))
        write(out, "peds.ns", nodes.node_array_ns("Parity_Peds", made, names) + peds_cars.scripts_ns(made_scripts))
        lines.append("peds %s" % sorted(made_stats.items()))
    write(out, "translate.txt", "\n".join(lines) + "\n")


def cmd_ground(out, game_dir, levels):
    # THUG2's own levels: their node arrays as NeverScript text against their collision.
    lines = []
    for level in levels:
        folder = os.path.join(game_dir, "Data", "levels", level)
        q = qb.QbFile(open(os.path.join(folder, level + ".qb"), "rb").read(), level, True)
        names = dict(q.names)
        array = [n for n in node_array_of(q, level) if isinstance(n, qb.Struct)]
        # (some of THUG2's restarts moved under the world, and a kill script naming one: ground_restarts' work)
        for k, node in enumerate(n for n in array if nodes.class_of(n, names) == "restart"):
            if k % 4 == 0:
                pos = node.get("Pos")
                if isinstance(pos, tuple):
                    node.set("Pos", ("vector", (pos[1][0], pos[1][1] - 5000.0, pos[1][2])))
        text = nodes.node_array_ns(level + "_NodeArray", array, names)
        first = next((n for n in array if nodes.class_of(n, names) == "restart" and isinstance(n.get("Name"), qb.Name)), None)
        if first is not None:
            text += "script Parity_Kill {\n    SK3_KillSkater nodename=%s\n}\n" % qb.name_text(first.get("Name").value, names)
        text, report = nodes.ground_restarts_at(text, folder, level)
        write(out, level + ".ns", text)
        text, changed = nodes.net_game_nodes(text)
        write(out, level + "_net.ns", text)
        lines.append("%s %d %s" % (level, changed, report))
    write(out, "ground.txt", "\n".join(lines) + "\n")


# --- level structures, manifests ------------------------------------------------------------------------------------

def tagged(d):
    return [[k, {"f": repr(v)} if isinstance(v, float) else {"i": v}] for k, v in d.items()]


def cmd_lighting(out, weights, paths):
    lines = []
    for label, data in inputs(paths, (".qb",)):
        try:
            q = qb.QbFile(data, label, weights == "1")
        except Exception:
            continue
        for key, value in q.globals:
            if isinstance(value, qb.Struct):
                light = common.lighting(value)
                if light:
                    lines.append("%s %08X %s" % (label, key.value, json.dumps(tagged(light))))
    write(out, "lighting.txt", "\n".join(lines) + "\n")


def cmd_manifest(out, games):
    lines = []
    for game in games:
        tag = safe(game)
        for staging in ported_levels.STAGINGS:
            path = os.path.join(game, staging, ported_levels.MANIFEST)
            manifest = ported_levels.read_manifest(path)
            if manifest is None:
                continue
            target = os.path.join(out, tag, staging + ".json")
            os.makedirs(os.path.dirname(target), exist_ok=True)
            ported_levels.write_manifest(target, manifest)
            lines.append("%s %s same=%d" % (tag, staging, open(target, "rb").read() == open(path, "rb").read()))
        manifest = ported_levels.load_manifest(game)
        if manifest is None:
            lines.append("%s none" % tag)
            continue
        write(out, os.path.join(tag, "levels.ns"), ported_levels.levels_qb_text(manifest))
        write(out, os.path.join(tag, "signs.ns"), ported_levels.signs_text(manifest, "        "))
        lines.append("%s numbers %s" % (tag, ported_levels.sign_numbers(manifest)))
        lines.append("%s names %s" % (tag, [ported_levels.struct_name(e) for e in manifest["levels"]]))
        base = "script Load_A {\n}\nscript Load_TestLevel {\n    load_level Level_TestLevel\n}\n"
        write(out, os.path.join(tag, "added.ns"), ported_levels.add_to_levels(base, manifest))
    # install_files from a small made-up game folder
    fake = os.path.join(out, "fake_game")
    for staging, level in (("thug2-decomp-thps4", "thps4_alc"), ("thug2-decomp-thaw", "thaw_z_bh")):
        os.makedirs(os.path.join(fake, staging, "levels", level, "sub"), exist_ok=True)
        open(os.path.join(fake, staging, "levels", level, level + ".qb"), "wb").write(b"qb " + level.encode())
        open(os.path.join(fake, staging, "levels", level, "sub", "x.bin"), "wb").write(bytes(range(256)))
    os.makedirs(os.path.join(out, "overlay", "levels", "thps4_alc"), exist_ok=True)
    open(os.path.join(out, "overlay", "levels", "thps4_alc", "old.txt"), "wb").write(b"old")
    lines.append("installed %s" % ported_levels.install_files(fake, os.path.join(out, "overlay")))
    write(out, "manifest.txt", "\n".join(lines) + "\n")


# --- geometry ---------------------------------------------------------------------------------------------------------

def unjson(x):
    if isinstance(x, dict):
        if set(x) == {"hex"}:
            return bytes.fromhex(x["hex"])
        return {k: unjson(v) for k, v in x.items()}
    if isinstance(x, list):
        return [unjson(v) for v in x]
    return x


def cmd_geometry(out, paths):
    # paths: driver.ts prepare's JSON files, or folders of them
    lines = []
    expanded = []
    for path in paths:
        if os.path.isdir(path):
            expanded += [os.path.join(path, f) for f in sorted(os.listdir(path)) if f.endswith(".json")]
        else:
            expanded.append(path)
    for path in expanded:
        item = unjson(json.load(open(path)))
        warnings = []
        scene = geometry.convert_scene({"sectors": item["sectors"]}, warnings, item["materials"],
                                       halve_colors=item["game"] == "thps4")
        data = thug2_scene.write_scene(scene)
        lines.append("%s scene %s %s %s" % (item["name"], C.digest(C.canon(scene)), C.blob(data), warnings[:20]))
        lines.append("%s shaders %s" % (item["name"], [geometry.pixel_shader(m) for m in item["materials"]]))
        if "col" in item:
            warnings = []
            col = geometry.convert_col({"objects": item["col"]}, lambda c: c % 2 == 0, warnings,
                                       lambda f: (f ^ 0x80) & 0x15FC)
            data = thug2_col.write_col(col)
            again = thug2_col.read_col(data, item["name"])
            lines.append("%s col %s %s %s %s" % (item["name"], C.digest(C.canon(col)), C.blob(data),
                                                  col_check.check(again), warnings))
    write(out, "geometry.txt", "\n".join(lines) + "\n")


def cmd_colgeometry(out, paths):
    # THUG2's own collision objects made again (fixed-point grid, reaches, BSP trees) from their vertices and faces.
    lines = []
    for label, data in inputs(paths, (".col.xbx",)):
        col = thug2_col.read_col(data, label)
        source = {"objects": [{"checksum": o["checksum"], "flags": o["flags"], "bbox": o["bbox"], "verts": o["verts"],
                               "intensities": o["intensities"], "faces": o["faces"]} for o in col["objects"]]}
        made = geometry.convert_col(source, lambda c: c % 3 != 0, [], lambda f: f)
        data = thug2_col.write_col(made)
        lines.append("%s %s %s %s" % (label, C.digest(C.canon(made)), C.blob(data),
                                      col_check.check(thug2_col.read_col(data, label))))
        for o in made["objects"][:3]:
            lines.append("  %s %s" % (C.blob(o["value"]), C.blob(o["reach"])))
    write(out, "colgeometry.txt", "\n".join(lines) + "\n")


# --- the rest -----------------------------------------------------------------------------------------------------------

def cmd_compat(out):
    lines = []
    for name in compat.PROFILES:
        p = compat.load(name)
        for key in ("level_names", "frame_kept", "frame_adjust", "missing"):
            lines.append("%s %s %s" % (name, key, json.dumps(p.section("thaw_levels", key), sort_keys=True)))
        lines.append("%s file %s" % (name, json.dumps(p.file("nothing"))))
    try:
        compat.load("bad")
    except ValueError as e:
        lines.append(str(e))
    write(out, "compat.txt", "\n".join(lines) + "\n")


def cmd_common(out, game_dir, archives):
    pre = os.path.join(game_dir, "Data", "pre")
    checked, failures = common.thug2_round_trips(pre, archives)
    print("checked %d failures %d" % (checked, failures))
    folder = os.path.join(game_dir, "Data", "levels", "TR")
    scene = thug2_scene.read_scene(open(os.path.join(folder, "TR.scn.xbx"), "rb").read())
    textures = tex.read_tex(open(os.path.join(folder, "TR.tex.xbx"), "rb").read())
    col = thug2_col.read_col(open(os.path.join(folder, "TR.col.xbx"), "rb").read())
    common.write_level(os.path.join(out, "level"), "tr", scene, textures, col)
    common.write_level(os.path.join(out, "sky"), "tr_sky", scene, textures, None)
    report = []
    common.report_level(report, "tr", scene, textures, col, ["w%d" % k for k in range(13)], 2)
    common.report_level(report, "tr_sky", scene, textures, None, [])
    write(out, "report.txt", "\n".join(report) + "\n")


def main():
    command, out, rest = sys.argv[1], sys.argv[2], sys.argv[3:]
    os.makedirs(out, exist_ok=True)
    if command == "archives":
        cmd_archives(out, rest)
    elif command == "files":
        cmd_files(out, rest)
    elif command == "qb":
        cmd_qb(out, rest[0], rest[1:])
    elif command == "thug2":
        cmd_thug2(out, rest[0])
    elif command == "translate":
        cmd_translate(out, rest[0], rest[1], rest[2], rest[3:])
    elif command == "ground":
        cmd_ground(out, rest[0], rest[1:])
    elif command == "lighting":
        cmd_lighting(out, rest[0], rest[1:])
    elif command == "manifest":
        cmd_manifest(out, rest)
    elif command == "geometry":
        cmd_geometry(out, rest)
    elif command == "colgeometry":
        cmd_colgeometry(out, rest)
    elif command == "compat":
        cmd_compat(out)
    elif command == "common":
        cmd_common(out, rest[0], rest[1:])
    else:
        sys.exit("unknown command %s" % command)


if __name__ == "__main__":
    main()
