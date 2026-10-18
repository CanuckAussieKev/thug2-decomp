# NOT ORIGINAL - a THPS4 level's scripts, game objects, waypoints, peds and vehicles for free skate in THUG2
# (tools/thps4_levels/thps4_levels.py). Nothing of either game is stored: everything is read from the user's files.
#
# THPS4's node arrays are close to THUG2's (the same classes and keys, legacy Position); its compiled scripts too:
# tools/level_port/script_port.py translates them (THPS4's long-form if / else, its random tokens without weights,
# its switch without jumps, `object:command`). What's made of the nodes:
#   LevelObject        THUG2's LevelObject (THPS4's sectors are in the object's own space, as THUG2's): moved by its
#                      trigger script (run by the object).
#   GameObject Ghost   THUG2's ghost game object (model="none"): a script's controller.
#   GameObject (model) left out: goal pick-ups (money, letters, icons, flags, tapes) - goals aren't ported.
#   Vehicle            Car_Generic01 cars: THUG2's own cars (peds_cars.py); the others (trams, rides, trains, cranes:
#                      moved by their scripts) LevelObjects of their THPS4 model (vehicle_models: the model's sectors
#                      in the level's scene, as one sector of the node's name in the object's own space).
#   Pedestrian         walkers (Obj_FollowPathLinked): THUG2's ped AI walking (peds_cars.py); the others standing
#                      THUG2 peds at their place (THUG2's ped AI, a profile like THPS4's model); animals left out.
#   Waypoint           kept (the objects' paths), ped paths as peds_cars.py makes them.
#   ProximNode         left out (THUG2's proximity nodes are boxes of the scene - BoundingBox - not THPS4's radius
#                      spheres); their trigger scripts run once at the start (they swap objects in and out).
#   ParticleEmitter    left out (THPS4's script-driven emitters: no THUG2 counterpart).
# The scripts brought along: the nodes' trigger scripts (sectors' and rails' run by the skater, objects' by the
# object) and THPS4's level structure's setup_script / startup_script, run from the level's own setup script.

import os

import nodes
import peds_cars
import qb
import script_port
import tex
import thps4
from qb import Name, Struct, checksum

# THPS4's script files whose scripts may be brought along (the level's own files: "level"; the designers' shared
# files). Goals, menus, peds' and the skater's scripts are THPS4's game, not the level's.
SOURCES = ("ajc_scripts.qb", "alf_scripts.qb", "bdj_scripts.qb", "cjr_scripts.qb", "cpf_scripts.qb", "jku_scripts.qb",
           "js_scripts.qb", "sk4_scripts.qb")
# THPS4's commands left out although THUG2 has them (besides script_port.LEFT_OUT): goals, the skater's career,
# cameras, THPS4's panels.
LEFT_OUT = ("GoalManager_ActivateGoal", "GoalManager_DeactivateGoal", "GoalManager_EditGoal", "GoalManager_AddGoal",
            "GoalManager_WinGoal", "GoalManager_LoseGoal", "GoalManager_UnlockGoal", "GoalManager_SetGoalTimer",
            "GoalManager_ShowGoalPoints", "GoalManager_ShowPoints", "GoalManager_UninitializeAllGoals",
            "GoalManager_InitializeAllGoals", "Create_Panel_Message", "create_panel_message", "create_panel_block",
            "Create_Panel_Block", "PlayCutscene", "PlaySkaterCamAnim", "UnpauseSkaters", "SetSkaterCamAnimShouldPause",
            # goal arrows, minigame prompts and other HUD (THPS4's goals' and minigames')
            "CreateScreenElement", "SetScreenElementLock", "DestroyScreenElement", "SetScreenElementProps",
            "RunScriptOnScreenElement", "DoScreenElementMorph", "setup_ped_speech", "speech_box_exit",
            "SetFlag", "UnSetFlag")
# Member functions the skater may run besides script_port's (the gap / trigger scripts of sectors and rails ask it
# `if onground`, `if skitching`, `if doingtrick`...): THUG2's skater has these components.
COMPONENTS = {script_port.SKATER: {"skaterstate", "skatercorephysics"}}
# The skater's member functions by name (no table entry): TriggerType - THUG2's own LA trigger scripts ask it too
# (`if TriggerType <trigger_array>`).
BY_NAME = {script_port.SKATER: {"TriggerType"}}
# The made peds' trigger script (THUG2's ped AI, standing).
PED_STAND = "thps4_ped_stand"
# How many kinds of left-out statements the report lists.
REPORTED = int(os.environ.get("THPS4_SCRIPTS_REPORTED", "15"))
PED_SCRIPTS = "script %s {\n    Ped_InitAI stand \n}\n" % PED_STAND


def read(path):
    return open(path, "rb").read()


def _key(names, text):
    value = checksum(text)
    names.setdefault(value, text)
    return Name(value)


class Port:
    # What bringing THPS4 levels' scripts along needs, read once: THUG2's commands (script_port.Thug2), THPS4's script
    # files (script_port.Source), THUG2's assets (profiles, models).
    def __init__(self, thps4_dir, thug2_dir):
        self.thug2 = script_port.Thug2(thug2_dir)
        self.assets = peds_cars.Thug2Assets(thug2_dir)
        self.vehicles = VehicleModels(thps4_dir)
        folder = os.path.join(thps4_dir, "Game", "data", "scripts")
        self.sources = [script_port.Source(read(os.path.join(folder, f)), f.lower())
                        for f in sorted(os.listdir(folder)) if f.lower().endswith(".qb")]


def profile(model, assets):
    # A THUG2 ped profile like THPS4's ped type: its pro, a guard, a worker, a woman, else any man.
    text = model.lower()
    if text.startswith("pedpro_"):
        for candidate in ("PedPro_%s" % model[7:], "PedPro_%s_skateboard" % model[7:]):
            if assets.profile_exists(candidate):
                return candidate
    for part, found in (("security", "Ped_Security_Guard_01"), ("guard", "Ped_Security_Guard_01"),
                        ("cop", "Ped_Security_Guard_01"), ("welder", "Ped_Construction_Worker_01"),
                        ("worker", "Ped_Construction_Worker_01"), ("female", "random_female_profile"),
                        ("girl", "random_female_profile"), ("skank", "random_female_profile"),
                        ("skateboard", "Ped_Skateboard_a"), ("grommit", "Ped_Skateboard_a")):
        if part in text and assets.profile_exists(found):
            return found
    return "random_male_profile"


class Nodes:
    # The THPS4 node array's nodes made THUG2's (see the top); kept whole by nodes.convert_nodes (as_is), placeholders
    # (Class=LeftOut) where left out, so the Links (indices) stay right.
    def __init__(self, array, names, sectors, moving_names, ped_paths, assets, vehicle_sectors=frozenset(),
                 collides=frozenset()):
        self.array, self.names, self.sectors, self.assets = array, names, sectors, assets
        self.collides = collides              # the collision objects' names (CollisionMode=Geometry needs one)
        self.moving_names = moving_names      # nodes peds_cars.py made again (peds, cars)
        self.ped_paths = ped_paths            # array indices of the waypoints peds_cars.py made again
        self.vehicle_sectors = vehicle_sectors  # vehicles made LevelObjects (their model's sector in the scene)
        self.ids = set()
        self.triggers = []   # (node name, script, context)
        self.objects = {}    # node name -> (context, created at start): `name:command` targets
        self.proxim_scripts = []
        self.counts = {}

    def _count(self, what):
        self.counts[what] = self.counts.get(what, 0) + 1

    def _left_out(self, what):
        if what:
            self._count(what)
        return Struct([(_key(self.names, "Class"), _key(self.names, "LeftOut"))])

    def _links(self, node):
        links = node.get("Links")
        if not isinstance(links, list):
            return []
        out = [v for v in links if isinstance(v, int) and 0 <= v < len(self.array) and
               isinstance(self.array[v], Struct) and nodes.class_of(self.array[v], self.names) == "waypoint"]
        return sorted(set(out), key=out.index)

    def _made(self, node, entries, context, script=True):
        s = Struct(entries)
        self.ids.add(id(s))
        name = node.get("Name")
        trigger = node.get("TriggerScript")
        if script and isinstance(trigger, Name) and isinstance(name, Name):
            self.triggers.append((name.value, trigger.value, context))
        if isinstance(name, Name) and context is not None:
            self.objects[name.value] = (context, node.has_flag("CreatedAtStart"))
        return s

    def _base(self, node, cls, keys=()):
        # Position, Angles, Name, Class, flags, the listed keys as they are; Links to waypoints.
        n = self.names
        entries = [(_key(n, "Position"), node.get("Position")),
                   (_key(n, "Angles"), node.get("Angles") or ("vector", (0.0, 0.0, 0.0))),
                   (_key(n, "Name"), node.get("Name")), (_key(n, "Class"), _key(n, cls))]
        entries += [(None, _key(n, f)) for f in ("CreatedAtStart", "AbsentInNetGames") if node.has_flag(f)]
        for k in keys:
            if node.get(k) is not None:
                entries.append((_key(n, k), node.get(k)))
        links = self._links(node)
        if links:
            entries.append((_key(n, "Links"), links))
        return entries

    def _collision(self, node):
        name = node.get("Name")
        if isinstance(name, Name) and name.value in self.collides:
            return [(_key(self.names, "CollisionMode"), _key(self.names, "Geometry"))]
        return []

    def convert(self, node):
        # The THUG2 node, a placeholder, or None (nodes.convert_nodes's own: restarts, rails, sectors, crowns).
        n = self.names
        cls = nodes.class_of(node, n)
        name = node.get("Name")
        if cls in ("restart", "railnode", "levelgeometry", "genericnode"):
            return None
        if not isinstance(name, Name) or not isinstance(node.get("Position"), tuple):
            return self._left_out("nodes without a name or position")
        if cls == "levelobject":
            if name.value not in self.sectors:
                return self._left_out("level objects without a sector")
            self._count("level objects")
            return self._made(node, self._base(node, "LevelObject") + self._collision(node) + [
                (_key(n, "Type"), _key(n, "Normal")), (_key(n, "SuspendDistance"), 0),
                (_key(n, "lod_dist1"), 10000), (_key(n, "lod_dist2"), 10001)], script_port.OBJECT)
        if cls == "gameobject":
            if nodes.type_of(node, n) != "ghost":
                return self._left_out("goal objects")
            self._count("script objects")
            return self._made(node, self._base(node, "GameObject") + [
                (_key(n, "Type"), _key(n, "Ghost")), (None, _key(n, "NeverSuspend")), (_key(n, "model"), "none"),
                (_key(n, "lod_dist1"), 400), (_key(n, "lod_dist2"), 401)], script_port.OBJECT)
        if cls == "waypoint":
            if self.index_of(node) in self.ped_paths:
                return self._left_out(None)
            self._count("waypoints")
            return self._made(node, self._base(node, "Waypoint"), script_port.OBJECT)
        if cls == "vehicle":
            if name.value in self.moving_names:
                return self._left_out(None)
            if name.value in self.vehicle_sectors:
                self._count("vehicles (their models)")
                return self._made(node, self._base(node, "LevelObject") + self._collision(node) + [
                    (_key(n, "Type"), _key(n, "Normal")), (_key(n, "SuspendDistance"), 0),
                    (_key(n, "lod_dist1"), 10000), (_key(n, "lod_dist2"), 10001)], script_port.OBJECT)
            return self._left_out("vehicles without a model")
        if cls == "pedestrian":
            if name.value in self.moving_names:
                return self._left_out(None)
            kind = nodes.type_of(node, n)
            if kind.startswith("anl_"):
                return self._left_out("animals")
            self._count("standing peds")
            entries = self._base(node, "Pedestrian") + [
                (_key(n, "Type"), _key(n, "Ped_From_Profile")), (_key(n, "PedAI_Type"), _key(n, "Stand")),
                (_key(n, "skeletonName"), _key(n, "THPS6_human")),
                (_key(n, "AnimName"), _key(n, "animload_THPS6_human")),
                (_key(n, "profile"), _key(n, profile(n.get(node.get("Type").value, ""), self.assets))),
                (_key(n, "SuspendDistance"), 10000), (_key(n, "lod_dist1"), 200), (_key(n, "lod_dist2"), 201),
                (_key(n, "TriggerScript"), _key(n, PED_STAND))]
            entries = [e for e in entries if not (e[0] is not None and e[0].value == checksum("Links"))]
            return self._made(node, entries, script_port.NONE, script=False)
        if cls == "proximnode":
            if isinstance(node.get("TriggerScript"), Name):
                self.proxim_scripts.append(node.get("TriggerScript").value)
            return self._left_out("proximity nodes")
        return self._left_out("%s nodes" % (cls or "unknown"))

    def index_of(self, node):
        if not hasattr(self, "_index"):
            self._index = {id(x): k for k, x in enumerate(self.array)}
        return self._index.get(id(node))


# --- vehicles' models ---------------------------------------------------------------------------------------------

def _bounds(positions):
    lo = [min(p[k] for p in positions) for k in range(3)]
    hi = [max(p[k] for p in positions) for k in range(3)]
    center = [(lo[k] + hi[k]) / 2 for k in range(3)]
    radius = max(sum((p[k] - center[k]) ** 2 for k in range(3)) for p in positions) ** 0.5
    return tuple(lo) + tuple(hi), tuple(center) + (radius,)


def merge_sectors(sectors, name):
    # A model's sectors as one sector of the checksum `name` (a THUG2 LevelObject draws the one sector of its name):
    # their vertex arrays one after another, the strips moved along. Skinned models (THPS4's cars and trams: a
    # skeleton - shadow quad, body, four wheels) keep only the body, the largest, turned from their Z-up bone space
    # to the level's Y-up (x, z, -y) - the wheels sit at bones not converted. None when nothing is left.
    if any(s["bone"] >= 0 for s in sectors):
        sectors = [max(sectors, key=lambda s: len(s["positions"]))]
        turn = True
    else:
        turn = False
    sectors = [s for s in sectors if s["positions"] and s["meshes"]]
    if not sectors:
        return None
    normals = all(thps4.SEC_NORMALS & s["flags"] and "normals" in s for s in sectors)
    sets = max((s.get("uv_sets") or 0) for s in sectors if s["flags"] & thps4.SEC_TEXCOORDS) \
        if any(s["flags"] & thps4.SEC_TEXCOORDS for s in sectors) else 0
    out = {"checksum": name, "bone": -1, "positions": [], "colors": [], "meshes": [],
           "flags": thps4.SEC_COLORS | (thps4.SEC_TEXCOORDS if sets else 0) | (thps4.SEC_NORMALS if normals else 0)}
    if normals:
        out["normals"] = []
    if sets:
        out["uv_sets"], out["uvs"] = sets, []
    for s in sectors:
        base = len(out["positions"])
        count = len(s["positions"])
        if turn:
            out["positions"] += [(x, z, -y) for x, y, z in s["positions"]]
            if normals:
                out["normals"] += [(x, z, -y) for x, y, z in s["normals"]]
        else:
            out["positions"] += list(s["positions"])
            if normals:
                out["normals"] += list(s["normals"])
        out["colors"] += list(s["colors"]) if "colors" in s else [(255, 255, 255, 128)] * count
        if sets:
            have = s.get("uv_sets") or 0 if s["flags"] & thps4.SEC_TEXCOORDS else 0
            for k in range(count):
                uv = list(s["uvs"][k]) if have else []
                while len(uv) < 2 * sets:
                    uv += uv[:2] if uv else [0.0, 0.0]
                out["uvs"].append(tuple(uv[:2 * sets]))
        for mesh in s["meshes"]:
            out["meshes"].append({"flags": mesh["flags"], "material": mesh["material"],
                                  "strip": tuple(v + base for v in mesh["strip"])})
    out["bbox"], out["sphere"] = _bounds(out["positions"])
    return out, turn


def merge_col(objects, name, turn):
    # A model's collision objects as one object of the checksum `name` (THUG2's LevelObject collides with the
    # object of its name), turned as merge_sectors turned the scene.
    verts, intensities, faces = [], [], []
    for o in objects:
        base = len(verts)
        verts += [(x, z, -y) if turn else (x, y, z) for x, y, z in o["verts"]]
        intensities += list(o["intensities"])
        faces += [(f, t, a + base, b + base, c + base) for f, t, a, b, c in o["faces"]]
    if not verts or not faces:
        return None
    lo = [min(v[k] for v in verts) for k in range(3)]
    hi = [max(v[k] for v in verts) for k in range(3)]
    return {"checksum": name, "flags": objects[0]["flags"], "small_faces": 0, "fixed_verts": 0,
            "bbox": (lo[0], lo[1], lo[2], 1.0, hi[0], hi[1], hi[2], 1.0), "value": objects[0]["value"],
            "verts": verts, "intensities": intensities, "faces": faces}


class VehicleModels:
    # THPS4's vehicles moved by their scripts (not Car_Generic01's: those are THUG2's cars), each made a LevelObject
    # of its model (Game\data\models\<model>mdl.dat / tex.dat / col.dat): its sector, materials, textures and
    # collision added to the level's (add_to).
    def __init__(self, thps4_dir):
        self.models_dir = os.path.join(thps4_dir, "Game", "data", "models")
        self._cache = {}

    def model(self, path):
        key = path.lower()
        if key not in self._cache:
            base = os.path.join(self.models_dir, os.path.splitext(path.replace("/", "\\"))[0])
            found = None
            if os.path.exists(base + "mdl.dat") and os.path.exists(base + "tex.dat"):
                scene = thps4.read_scene(read(base + "mdl.dat"), base + "mdl.dat")
                col = thps4.read_col(read(base + "col.dat"), base + "col.dat") if os.path.exists(base + "col.dat") \
                    else None
                found = (scene, tex.read_tex(read(base + "tex.dat"), base + "tex.dat"), col)
            self._cache[key] = found
        return self._cache[key]

    def plan(self, array, names, moving_names):
        # {node name: model path} for the vehicles that get their model.
        out = {}
        for node in array:
            if not isinstance(node, Struct) or nodes.class_of(node, names) != "vehicle":
                continue
            name, path = node.get("Name"), node.get("model")
            if isinstance(name, Name) and name.value not in moving_names and isinstance(path, str) and \
                    self.model(path):
                out[name.value] = path
        return out

    def add_to(self, plan, scene4, textures, col4):
        # The planned vehicles' sectors / materials / textures / collision into the level's THPS4 scene, its THUG2
        # texture dictionary and its THPS4 collision. Returns the names of those that got collision.
        materials = {m["checksum"] for m in scene4["materials"]}
        have_tex = {t["checksum"] for t in textures["textures"]}
        collides = set()
        for name, path in sorted(plan.items()):
            scene, model_tex, col = self.model(path)
            merged = merge_sectors(scene["sectors"], name)
            if merged is None:
                continue
            sector, turn = merged
            scene4["sectors"].append(sector)
            for m in scene["materials"]:
                if m["checksum"] not in materials:
                    scene4["materials"].append(m)
                    materials.add(m["checksum"])
            for t in model_tex["textures"]:
                if t["checksum"] not in have_tex:
                    textures["textures"].append(t)
                    have_tex.add(t["checksum"])
            if col4 is not None and col is not None:
                obj = merge_col(col["objects"], name, turn)
                if obj is not None:
                    col4["objects"].append(obj)
                    collides.add(name)
        return collides


def ped_paths(array, names, moving):
    # The array indices of the waypoints peds_cars.py made again (reached from the peds' / cars' Links).
    made = {n.get("Name").value for n in moving if isinstance(n.get("Name"), Name)}
    out = set()
    for node in array:
        if isinstance(node, Struct) and isinstance(node.get("Name"), Name) and node.get("Name").value in made and \
                nodes.class_of(node, names) in ("pedestrian", "vehicle"):
            for link in node.get("Links") or []:
                if isinstance(link, int):
                    out.update(peds_cars.follow(array, link, names))
    return out


def moving_names(moving):
    return {n.get("Name").value for n in moving if isinstance(n.get("Name"), Name)}


def convert(port, array, names, level_sources, kills, sectors, moving, struct, level, vehicle_sectors=frozenset(),
            collides=frozenset()):
    # The level's node array made THUG2's with its scripts (see the top). level_sources: script_port.Source of the
    # level's own .qb files; kills: the kill scripts (nodes.kill_scripts); sectors: the scene's sector checksums;
    # moving: peds_cars.py's nodes for the level; struct: THPS4's level structure; vehicle_sectors: the vehicles
    # whose model's sector is in the scene (VehicleModels); collides: the collision objects' names. Returns
    # (converted nodes, kills, the scripts {name: THUG2 bytes}, NeverScript text to add (globals, setup script, ped
    # scripts), whether there's a setup script, report line, the Translator).
    made = Nodes(array, names, sectors, moving_names(moving), ped_paths(array, names, moving), port.assets,
                 vehicle_sectors, collides)
    kept = []
    for node in array:
        new = made.convert(node) if isinstance(node, Struct) else made._left_out(None)
        kept.append(node if new is None else new)
    # trigger scripts of the sectors and rails (the skater's) and the made nodes', wanted before convert_nodes
    wanted = {node.get("TriggerScript").value for node in kept if isinstance(node.get("TriggerScript"), Name)
              and nodes.class_of(node, names) in ("levelgeometry", "railnode")} - set(kills)
    converted, stats = nodes.convert_nodes(kept, names, kills, as_is=made.ids, scripts=wanted)
    kills = nodes.add_kill_restarts(converted, kills, names)

    # the scripts
    sources = list(port.sources) + list(level_sources)
    allowed = set(SOURCES) | {"level"}
    all_names = {n.get("Name").value for n in array if isinstance(n, Struct) and isinstance(n.get("Name"), Name)}
    node_names = {n.get("Name").value for n in list(converted) + list(moving) if isinstance(n.get("Name"), Name)}
    objects = dict(made.objects)
    for node in moving:
        if isinstance(node.get("Name"), Name) and nodes.class_of(node, names) in ("pedestrian", "vehicle"):
            objects[node.get("Name").value] = (script_port.NONE, node.has_flag("CreatedAtStart"))
    objects[checksum("Skater")] = (script_port.SKATER, True)
    provided = set(kills) | {checksum(s) for s in ("LoadTerrain", PED_STAND)}
    tr = script_port.Translator(sources, port.thug2, lambda label: label in allowed, node_names, all_names, names,
                                provided=provided, left_out=LEFT_OUT, objects=objects, components=COMPONENTS,
                                full=True, by_name=BY_NAME)
    roots = [(s, script_port.SKATER) for s in wanted]
    roots += [(script, context) for _, script, context in made.triggers]
    starts = []
    for key in ("setup_script", "startup_script"):
        value = struct.get(key) if struct else None
        if isinstance(value, Name) and tr.can_bring(value.value):
            starts.append(value.value)
    starts += [s for s in made.proxim_scripts if tr.can_bring(s)]
    roots += [(s, script_port.NONE) for s in starts]
    out = tr.run(roots)

    def available(script):
        return script in out or script in port.thug2.scripts or script in provided

    dropped = 0
    for node in converted:
        script = node.get("TriggerScript")
        if isinstance(script, Name) and not available(script.value):
            node.remove("TriggerScript")
            dropped += 1
    by_name = {n.get("Name").value: n for n in converted if isinstance(n.get("Name"), Name)}
    trigger = checksum("TriggerScript")
    for node_name, script, _ in made.triggers:
        if node_name in by_name and available(script) and by_name[node_name].get("TriggerScript") is None:
            by_name[node_name].entries.append((Name(trigger), Name(script)))
    run = [s for s in dict.fromkeys(starts) if s in out]
    text = tr.globals_ns() + PED_SCRIPTS
    if run:
        text += "script %s_setup {\n%s}\n" % (level, "".join("    SpawnScript %s \n" % qb.name_text(s, names)
                                                            for s in run))
    worst = sorted(tr.left_out.items(), key=lambda kv: -kv[1])
    report = ("nodes: %d of %d kept %s\n  made %s\n  scripts: %d brought along (%d bytes), %d start scripts, %d "
              "trigger scripts not; left out %d statements: %s") % (
        len(converted), len(array), stats, made.counts, len(out), sum(map(len, out.values())), len(run), dropped,
        sum(tr.left_out.values()), ", ".join("%s x%d" % kv for kv in worst[:REPORTED]))
    return converted, kills, out, text, bool(run), report, tr
