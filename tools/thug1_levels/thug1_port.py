# NOT ORIGINAL - a THUG1 level's node array kept nearly whole in THUG2, with its scripts (tools/level_port/
# script_port.py translates them): THUG1's nodes, scripts and objects are THUG2's own predecessors (the same classes,
# keys, ped AI - Ped_InitAI -, car paths, bouncy objects, proximity nodes, sound emitters), so the level's peds,
# vehicles, moving objects, particles and scripts come along as they are, checked against what THUG2 has:
#
#   nodes      restarts, rails, level geometry and KOTH crowns as before (tools/level_port/nodes.py); waypoints,
#              climbing nodes (ledges, ladders), proximity nodes, sound emitters whole; LevelObjects as LevelObjects
#              (their sectors are in their own space - static geometry put them at the level's origin), bouncy ones
#              with their rigid body settings; peds with THUG2's skeleton and animations (THPS6_human) and a THUG2
#              profile (THUG1's own when THUG2 has it); vehicles and game objects whose model THUG2 has
#              (Data\models); particle objects whose texture THUG2 has (Data\images\particles). Lights, goal nodes
#              (GenericNodes other than crowns) and objects of models THUG2 lacks are left out (placeholders: the
#              Links are indices).
#   scripts    from the node trigger scripts (the sectors', rails' and proximity nodes' run by the skater, the
#              objects' by them) and the level structure's startup_script / setup_script, through everything they
#              call, from the level's own .qb files and THUG1's level designers' shared files. THUG1's goals (the
#              goal manager), cutscenes, panels and the skater's state are left out (script_port's list and LEFT_OUT
#              here); LoadSound of a sound THUG2 doesn't have (Data\sounds) too.
#
# Nothing of either game is stored: everything is read from the user's installs each time.

import os

import peds_cars
import prx
import qb
import script_port
import nodes
from qb import Name, Struct, checksum

# THUG1's shared script files a level's scripts may be brought along from (besides the level's own .qb files).
SHARED_SOURCES = ("ajc_scripts.qb", "alf_scripts.qb", "bdj_scripts.qb", "cjr_scripts.qb", "cpf_scripts.qb",
                  "jeg_scripts.qb", "jku_scripts.qb", "js_scripts.qb", "skutils.qb", "object.qb")
LEVEL = "level"

# THUG1 commands left out besides script_port's (goals, chapters, cutscenes, saves, career-only things).
LEFT_OUT = ("initialize_secret_tapes", "initialize_oldskool_icons", "goal_want_to_save", "Create_Panel_Message",
            "Create_Panel_Block", "Create_Panel_Sprite", "PlaySkaterCamAnim", "ClearExceptionGroup",
            "SetSkaterCamAnimShouldPause", "PlayCutscene", "PlayMovie_ChapterMovie", "UnlockLevel", "SetFlag",
            "UnsetFlag", "StartGoal", "SpawnSkaterScript")
LEFT_OUT_PREFIXES = ("GoalManager_", "Goal_", "Chapter_", "CS_", "Cutscene")
# script_port's left-out commands THUG1's level scripts may use: THUG2 has their sounds (LoadSound is checked against
# Data\sounds) and the objects' animations are checked (anim_check).
KEPT_IN = ("LoadSound", "Obj_PlayAnim", "Obj_WaitAnimFinished", "Obj_CycleAnim", "Obj_SetAnimCycleMode")

# Skeletons / animations THUG2's peds use (THUG1's THPS5_human ones aren't loaded by THUG2).
HUMAN = ("THPS6_human", "animload_THPS6_human")


def _name(names, text):
    value = checksum(text)
    names.setdefault(value, text)
    return Name(value)


def _text(value, names):
    return names.get(value.value, "") if isinstance(value, Name) else ""


def placeholder(names):
    return Struct([(_name(names, "Class"), _name(names, "LeftOut"))])


class Thug1Translator(script_port.Translator):
    # script_port's translator, plus: LoadSound of a sound THUG2 doesn't have is left out.
    def __init__(self, *args, sounds=frozenset(), **kwargs):
        super().__init__(*args, **kwargs)
        self.sounds = sounds
        self.load_sound = checksum("LoadSound")

    def command(self, toks, contexts, discover):
        if toks and toks[0].t == 0x16 and toks[0].name() == self.load_sound:
            path = next((t.p.rstrip(b"\0").decode("latin-1") for t in toks[1:] if t.t in (0x1B, 0x1C)), None)
            if path is None or path.lower().replace("/", "\\") not in self.sounds:
                self.note("sound not in THUG2")
                return None
        return super().command(toks, contexts, discover)


class Thug1Port:
    # What bringing THUG1 levels' scripts and objects along needs, read once: THUG2's commands, ped profiles, models,
    # particle textures and sounds; THUG1's shared script files (qb.pre).
    def __init__(self, thug2, qb_pre):
        self.thug2 = script_port.Thug2(thug2)
        self.assets = peds_cars.Thug2Assets(thug2)
        data = os.path.join(thug2, "Data")
        self.particles = {f.lower()[:-len(".img.xbx")] for f in os.listdir(os.path.join(data, "images", "particles"))
                          if f.lower().endswith(".img.xbx")}
        self.sounds = set()
        root = os.path.join(data, "sounds")
        for folder, _, files in os.walk(root):
            for f in files:
                if f.lower().endswith(".snd"):
                    rel = os.path.relpath(os.path.join(folder, f), root)
                    self.sounds.add(rel[:-4].lower())
        self.models = set()
        root = os.path.join(data, "models")
        for folder, _, files in os.walk(root):
            for f in files:
                if f.lower().endswith(".mdl.xbx"):
                    self.models.add(os.path.relpath(os.path.join(folder, f), root)[:-4].lower())
        self.shared = []
        for name, blob in prx.entries(qb_pre, lambda n: n.lower().endswith(".qb")):
            label = os.path.basename(name.replace("/", "\\")).lower()
            try:
                self.shared.append(script_port.Source(blob, label, weights=True))
            except (KeyError, ValueError, IndexError):
                continue

    def model_exists(self, model):
        return isinstance(model, str) and model.lower().replace("/", "\\") in self.models


def object_context(node, names):
    cls = nodes.class_of(node, names)
    if cls == "levelobject":
        return script_port.BOUNCY if node.has_flag("bouncy") else script_port.OBJECT
    return {"gameobject": script_port.OBJECT, "pedestrian": script_port.PED,
            "vehicle": script_port.VEHICLE}.get(cls)


class NodePass:
    # THUG1's nodes for THUG2 (see the top): each node in its place (placeholders for those left out), the ones kept
    # whole (as_is ids for nodes.convert_nodes), the objects' contexts by name and counts.
    def __init__(self, port, array, names, sectors):
        # sectors: the converted scene's sector checksums (a LevelObject is drawn from the sector of its name)
        self.port, self.names, self.sectors = port, names, sectors
        self.as_is = set()
        self.objects = {}
        self.counts = {}
        self.particle_textures = set()
        self.extra_anims = set()
        self.kept = [self.node(n) for n in array]

    def count(self, what):
        self.counts[what] = self.counts.get(what, 0) + 1

    def whole(self, node, what):
        s = Struct(list(node.entries))
        self.as_is.add(id(s))
        self.count(what)
        name = node.get("Name")
        context = object_context(node, self.names)
        if context and isinstance(name, Name):
            self.objects[name.value] = context
        return s

    def left_out(self, what):
        self.count("left out: " + what)
        return placeholder(self.names)

    def node(self, node):
        if not isinstance(node, Struct):
            return node
        n = self.names
        cls = nodes.class_of(node, n)
        if cls in ("restart", "railnode", "levelgeometry", "genericnode"):
            return node   # (nodes.convert_nodes' own handling)
        if cls in ("waypoint", "proximnode", "emitterobject"):
            return self.whole(node, cls + "s")
        if cls == "climbingnode":
            s = self.whole(node, "climbing nodes")
            # (TrickObject / Cluster: THUG2 registers the cluster's sector without checking it - not needed here)
            s.remove_flag("TrickObject")
            s.remove("Cluster")
            return s
        if cls == "levelobject":
            name = node.get("Name")
            if not (isinstance(name, Name) and name.value in self.sectors):
                return self.left_out("moving objects without a sector")
            s = self.whole(node, "bouncy objects" if node.has_flag("bouncy") else "moving objects")
            sound = s.get("SoundType")
            if isinstance(sound, Name) and sound.value not in self.port.thug2.globals:
                s.remove("SoundType")
            return s
        if cls == "pedestrian":
            return self.pedestrian(node)
        if cls == "vehicle":
            if not self.port.model_exists(node.get("model")):
                return self.left_out("vehicles (model not in THUG2)")
            return self.whole(node, "vehicles")
        if cls == "gameobject":
            model = node.get("model")
            if isinstance(model, str) and model.lower() != "none" and not self.port.model_exists(model):
                return self.left_out("game objects (model not in THUG2)")
            return self.whole(node, "game objects")
        if cls == "particleobject":
            texture = node.get("Texture")
            text = _text(texture, n).lower()
            if text not in self.port.particles:
                return self.left_out("particles (texture not in THUG2)")
            self.particle_textures.add(text)
            return self.whole(node, "particles")
        return self.left_out(cls or "?")

    def pedestrian(self, node):
        n = self.names
        s = self.whole(node, "peds")
        skeleton = _text(s.get("SkeletonName"), n).lower()
        if skeleton in ("thps5_human", ""):
            # (THUG2's humans: THPS6_human; THUG1's THPS5_human animations aren't loaded)
            s.remove("SkeletonName")
            s.entries.append((_name(n, "SkeletonName"), _name(n, HUMAN[0])))
            s.remove("AnimName")
            s.entries.append((_name(n, "AnimName"), _name(n, HUMAN[1])))
        anim = s.get("AnimName")
        if isinstance(anim, Name):
            if anim.value in self.port.thug2.scripts:
                self.extra_anims.add(_text(anim, n) or qb.name_text(anim.value, n))
            else:
                s.remove("AnimName")
        extra = s.get("Extra_Anims")
        if isinstance(extra, Name):
            if extra.value in self.port.thug2.scripts:
                self.extra_anims.add(_text(extra, n) or qb.name_text(extra.value, n))
            else:
                s.remove("Extra_Anims")
        profile = s.get("profile")
        if not (isinstance(profile, Name) and self.port.assets.profile_exists(_text(profile, n))):
            female = "female" in skeleton or "_f_" in _text(s.get("Type"), n).lower()
            s.remove("profile")
            s.entries.append((_name(n, "profile"),
                              _name(n, "random_female_profile" if female else "random_male_profile")))
            self.count("peds given a THUG2 profile")
        return s


def resolve_duplicate_triggers(array, script_qbs, names):
    # THUG1's classic levels (THPS1 / 2's) give a gap's second trigger sector a trigger script that only calls
    # DuplicateTrigger (not in THUG2): it runs the trigger script of the node it links to (Gap_CO_01 <-> Plane01). Such
    # nodes get that node's trigger script itself. Returns how many.
    duplicate = checksum("DuplicateTrigger")
    only = set()
    for q in script_qbs:
        for script, lines in q.scripts.items():
            calls = [line for line in lines if line]
            if len(calls) == 1 and calls[0][0][0] == 0x16 and int.from_bytes(calls[0][0][1], "little") == duplicate:
                only.add(script)
    count = 0
    for node in array:
        script = node.get("TriggerScript") if isinstance(node, Struct) else None
        if not (isinstance(script, Name) and script.value in only):
            continue
        links = node.get("Links")
        for link in links if isinstance(links, list) else []:
            other = array[link] if isinstance(link, int) and 0 <= link < len(array) else None
            target = other.get("TriggerScript") if isinstance(other, Struct) else None
            if isinstance(target, Name) and target.value not in only:
                node.set("TriggerScript", target)
                count += 1
                break
        else:
            node.remove("TriggerScript")
    return count


def runs_by(node, names):
    # Who runs a node's trigger script: the skater (sectors, rails, climbing nodes, proximity nodes), the object
    # itself, or nobody.
    cls = nodes.class_of(node, names)
    if cls in ("levelgeometry", "railnode", "climbingnode", "proximnode"):
        return script_port.SKATER
    return object_context(node, names) or script_port.NONE


def port_scripts(port, level_sources, converted, array, kills, names, struct):
    # The scripts brought along (see the top). Returns ({name: THUG2 bytes}, the THUG1 globals they use as NeverScript
    # text, the level structure's startup / setup script names kept, report line). Trigger scripts not brought along
    # are taken off their nodes.
    all_names = {x.get("Name").value for x in array if isinstance(x, Struct) and isinstance(x.get("Name"), Name)}
    node_names = {x.get("Name").value for x in converted if isinstance(x.get("Name"), Name)}
    objects = {x.get("Name").value: object_context(x, names) for x in converted
               if isinstance(x.get("Name"), Name) and object_context(x, names)}
    provided = set(kills) | {checksum(s) for s in ("LoadTerrain", "LoadAllParticleTextures", "load_level_anims")}
    allowed = set(SHARED_SOURCES) | {LEVEL}
    tr = Thug1Translator(list(port.shared) + list(level_sources), port.thug2, lambda label: label in allowed,
                         node_names, all_names, names, provided=provided, objects=objects, left_out=LEFT_OUT,
                         left_out_prefixes=LEFT_OUT_PREFIXES, anim_check=True, kept_in=KEPT_IN, sounds=port.sounds)
    roots = []
    for node in converted:
        script = node.get("TriggerScript")
        if isinstance(script, Name) and script.value not in kills:
            roots.append((script.value, runs_by(node, names)))
    starts = []
    for key in ("startup_script", "setup_script"):
        value = struct.get(key) if struct else None
        if isinstance(value, Name) and tr.can_bring(value.value):
            starts.append((key, value.value))
            roots.append((value.value, script_port.NONE))
    out = tr.run(roots)

    def available(script):
        return script in out or script in port.thug2.scripts or script in provided

    dropped = 0
    for node in converted:
        script = node.get("TriggerScript")
        if isinstance(script, Name) and not available(script.value):
            node.remove("TriggerScript")
            dropped += 1
    kept_starts = {key: qb.name_text(value, names) for key, value in starts if value in out}
    worst = sorted(tr.left_out.items(), key=lambda kv: -kv[1])
    report = "scripts: %d brought along (%d bytes), %d trigger scripts not; %s; left out %d statements: %s" % (
        len(out), sum(map(len, out.values())), dropped,
        ", ".join("%s %s" % (k, v) for k, v in kept_starts.items()) or "no startup / setup script",
        sum(tr.left_out.values()), ", ".join("%s x%d" % kv for kv in worst[:15]))
    return out, tr.globals_ns(), kept_starts, report, tr


def level_scripts_ns(made, thug1_anims):
    # The load scripts THUG2's load_level runs when the level's .qb has them: the particle textures of the kept
    # particle objects, the peds' extra animations (the THUG1 level's load_level_anims list where THUG2 has them).
    parts = []
    if made.particle_textures:
        parts.append("script LoadAllParticleTextures {\n%s}\n" % "".join(
            '    LoadParticleTexture "particles\\\\%s" \n' % t for t in sorted(made.particle_textures)))
    anims = sorted(made.extra_anims | set(thug1_anims))
    if anims:
        parts.append("script load_level_anims {\n%s}\n" % "".join("    %s \n" % a for a in anims))
    return "".join(parts)
