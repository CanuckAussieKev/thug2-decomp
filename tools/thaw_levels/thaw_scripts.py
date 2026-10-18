# NOT ORIGINAL - a THAW level's scripts, moving objects, peds and cars in THUG2's free skate (tools/thaw_levels).
# Nothing of either game is stored: everything is read from the user's files each time.
#
# Scripts: THAW's compiled scripts are THUG2's token stream (the same if / else / random / switch tokens) with one more
# token, 0x4A: an inline structure in THAW's item format - a call's arguments, mostly. They are read here into
# tools/level_port/script_port.py's form (ThawSource: if / else as its 0x25 / 0x26, jumps as target indices, the
# randoms' own weights, switch short jumps, the inline structures as THUG2's { } tokens - a call's own one as plain
# arguments) and brought along by its Translator: from the level's node scripts down through everything they call,
# statement by statement left out where THUG2 hasn't the command (or the member function for the object running it,
# or the node). THAW's game scripts are brought along only from GLOBAL_SOURCES (the gap scripts); everything else
# THAW-only (goals, the story, its ped AI, cameras) is left out.
#
# Nodes (made THUG2's, kept whole by nodes.convert_nodes - see PortedNodes):
#   LevelObject   THAW's moving / bouncy objects: THUG2 has the same class with the same keys (BE's bouncy trash
#                 cans: bouncy, contacts, coeff_...). Their sectors and collision are in their own space (centred
#                 on the origin) - left where they are when a level is recentred (local_names).
#   GameObject    the script objects (Type=Ghost, model="none") with their trigger scripts; the ones with THAW models
#                 (goal letters, chairs...) are left out (THAW's .mdl isn't converted).
#   Waypoint      kept (the paths scripts and cars follow); the ones peds walk as THUG2's ped AI waypoints.
#   Pedestrian    THAW's ped AI and models don't run in THUG2: THUG2's ped AI at the same place (walking the
#                 waypoints the ped links to, else standing) with a THUG2 profile like THAW's - the peds there in
#                 free skate (CreatedAtStart, or created from NODEFLAG_PERMANENT_PEDS); goal / story peds not.
#   LifeNode      THAW's ambient ped spots (ProfileName, Num_Objects): a standing THUG2 ped each (at most
#                 MAX_LIFE_PEDS per level).
#   Vehicle       a car whose script follows its path (Car_Generic01 / Obj_FollowPathLinked): THUG2's Car_Generic01
#                 on the same waypoints; other cars there at the start parked. THUG2's model of the name, else
#                 peds_cars.FALLBACK_CAR.

import os
import struct

import nodes
import peds_cars
import qb
import script_port
import thaw
from binio import FormatError
from qb import Name, Struct, checksum
from script_port import Tok

# THAW's game script files brought along (besides the level's own): the gap helpers (Gap_Gen_Rail...).
GLOBAL_SOURCES = ("scripts\\game\\misc\\gap_scripts.qb",)
LEVEL = "level"
MAX_LIFE_PEDS = 24

# The made nodes' trigger scripts (THUG2's own ped AI and cars), in every converted level's .qb.
PED_WALK, PED_STAND, CAR_DRIVE = "thaw_ped_walk", "thaw_ped_stand", "thaw_car_drive"
MADE_SCRIPTS = ("script %s {\n    Ped_InitAI \n}\nscript %s {\n    Ped_InitAI stand \n}\n"
                "script %s {\n    Car_Generic01 \n}\n" % (PED_WALK, PED_STAND, CAR_DRIVE))


def u32(b, at=0):
    return struct.unpack_from("<I", b, at)[0]


# --- scripts ------------------------------------------------------------------------------------------------------

def inline_struct(payload, names):
    # A 0x4A token's structure (THAW's item format, offsets from its own start).
    reader = thaw.QbItems.__new__(thaw.QbItems)
    reader.data, reader.name, reader.end, reader.names = payload, "inline structure", 0, names
    return reader.struct(0)


def value_toks(value, i):
    # A value of an item-format structure as THUG2 tokens.
    if isinstance(value, (Name, thaw.Ref)):
        return [Tok(0x16, struct.pack("<I", value.value), i)]
    if isinstance(value, bool) or isinstance(value, int):
        return [Tok(0x17, struct.pack("<i", int(value)), i)]
    if isinstance(value, float):
        return [Tok(0x1A, struct.pack("<f", value), i)]
    if isinstance(value, str):
        return [Tok(0x1B, value.encode("latin-1") + b"\0", i)]
    if isinstance(value, qb.Local):
        return [Tok(0x1C, value.text.encode("latin-1") + b"\0", i)]
    if isinstance(value, tuple):
        kind, v = value
        return [Tok(0x1E if kind == "vector" else 0x1F, struct.pack("<%df" % len(v), *v), i)]
    if isinstance(value, Struct):
        return [Tok(0x03, b"", i)] + entries_toks(value, i) + [Tok(0x04, b"", i)]
    if isinstance(value, list):
        out = [Tok(0x05, b"", i)]
        for v in value:
            out += value_toks(v, i)
        return out + [Tok(0x06, b"", i)]
    raise FormatError("inline structure value %r" % (value,))


def entries_toks(s, i):
    out = []
    for key, value in s.entries:
        if key is not None:
            out += [Tok(0x16, struct.pack("<I", key.value), i), Tok(0x07, b"", i)]
        out += value_toks(value, i)
    return out


# THAW's commands THUG2 has under another name (its "later" / "now" spawns are THUG2's spawn; ShatterFromSkater is
# THUG2's Shatter, without the skater's speed).
RENAME_TEXT = (("SpawnScriptLater", "SpawnScript"), ("SpawnScriptNow", "SpawnScript"),
               ("Obj_SpawnScriptLater", "Obj_SpawnScript"), ("Obj_SpawnScriptNow", "Obj_SpawnScript"),
               ("ShatterFromSkater", "Shatter"))
RENAMES = {checksum(a): checksum(b) for a, b in RENAME_TEXT}


def script_toks(body, names, label="script"):
    # A THAW script body as script_port's token list (Tok.i: the index of the THAW token it comes from).
    for _, new in RENAME_TEXT:
        names.setdefault(checksum(new), new)
    toks = [(off, t, struct.pack("<I", RENAMES[u32(p)]) if t == 0x16 and u32(p) in RENAMES else p)
            for off, t, p in thaw.tokens(body)]
    at = {off: k for k, (off, _, _) in enumerate(toks)}

    def target(pos):
        if pos not in at:
            raise FormatError("%s: a jump to 0x%X lands inside a token" % (label, pos))
        return at[pos]

    out, depth = [], 0
    for k, (off, t, p) in enumerate(toks):
        if t == 0x24:
            break
        if t == 0x47:
            out.append(Tok(0x25, b"", k))
        elif t == 0x48:
            out.append(Tok(0x26, b"", k))
        elif t == 0x2E:
            out.append(Tok(0x2E, p, k, [target(off + 5 + struct.unpack("<i", p)[0])]))
        elif t in script_port.RANDOMS:
            n = u32(p)
            weights = list(struct.unpack_from("<%dH" % n, p, 4))
            targets = []
            for m in range(n):
                field = off + 1 + 4 + 2 * n + 4 * m
                targets.append(target(field + 4 + struct.unpack_from("<i", p, 4 + 2 * n + 4 * m)[0]))
            out.append(Tok(t, p, k, targets, weights))
        elif t == 0x49:
            pos = off + struct.unpack("<H", p)[0]
            if pos in at:
                out.append(Tok(0x49, p, k, [at[pos]]))
            else:
                out.append(Tok(0x49, p, k, [target(pos + 1)], adjust=1))
        elif t == 0x4A:
            s = inline_struct(p, names)
            previous = toks[k - 1][1] if k else None
            if depth == 0 and previous != 0x07:
                # (a call's arguments: as plain arguments, so that the translator sees the names and scripts in them)
                out += entries_toks(s, k)
            else:
                out += value_toks(s, k)
        else:
            out.append(Tok(t, p, k))
        if t in (0x03, 0x05, 0x0E):
            depth += 1
        elif t in (0x04, 0x06, 0x0F):
            depth -= 1
    return out


def plain(value):
    # A global's value with THAW's $references as names (NeverScript has no form for them).
    if isinstance(value, thaw.Ref):
        return Name(value.value)
    if isinstance(value, Struct):
        return Struct([(k, plain(v)) for k, v in value.entries])
    if isinstance(value, list):
        return [plain(v) for v in value]
    return value


class ThawSource:
    # A THAW qb file (thaw.QbItems) as a script_port.Source: its scripts as token lists, its globals.
    def __init__(self, q, label, names):
        self.label = label
        self.names = {}
        self.scripts = {}
        for name, body in q.script_data.items():
            self.scripts[name] = script_toks(body, names, "%s: %s" % (q.name, names.get(name, "%08X" % name)))
        self.globals = {key.value: plain(value) for key, value in q.globals}


def global_sources(qb_pak, names):
    # THAW's game script files (data\pak\qb.pak.wpc) whose scripts may be brought along (GLOBAL_SOURCES).
    out = []
    for entry, data in thaw.pak_files(qb_pak):
        if not entry.path:
            continue
        label = entry.path.lower().replace(".wpc", "")
        if label in GLOBAL_SOURCES:
            out.append(ThawSource(thaw.QbItems(data, entry.path, names), label, names))
    return out


# --- nodes --------------------------------------------------------------------------------------------------------

def _key(names, text):
    value = checksum(text)
    names.setdefault(value, text)
    return Name(value)


# A LevelObject's keys and flags THUG2's own LevelObjects have (BE's bouncy objects, LA's movers).
OBJECT_KEYS = {checksum(k) for k in (
    "Pos", "Angles", "Name", "Class", "Type", "CollisionMode", "SuspendDistance", "lod_dist1", "lod_dist2", "contacts",
    "center_of_mass", "coeff_restitution", "coeff_friction", "skater_collision_impulse_factor",
    "skater_collision_rotation_factor", "skater_collision_assent", "skater_collision_radius")}
OBJECT_FLAGS = {checksum(k) for k in ("CreatedAtStart", "AbsentInNetGames", "bouncy", "DieUponSettling")}

FEMALE = ("fem", "girl", "chick", "woman", "lady", "supermodel", "mindy", "slut", "hooker", "mom")
PROFILES = (("skater", "Ped_Skateboard_a"), ("sk8", "Ped_Skateboard_a"), ("security", "Ped_Security_Guard_01"),
            ("guard", "Ped_Security_Guard_01"), ("constr", "Ped_Construction_Worker_01"),
            ("worker", "Ped_Construction_Worker_01"), ("graf", "Ped_graffiti02"), ("tagger", "Ped_graffiti02"),
            ("photo", "Ped_Photo"), ("camera", "Ped_CameraMan"), ("nurse", "Ped_Nurse_01"),
            ("paramedic", "Ped_Paramedic_01"), ("bum", "Ped_germandrunk"), ("hobo", "Ped_germandrunk"),
            ("drunk", "Ped_germandrunk"), ("hippie", "Ped_guitarist"), ("cop", "Ped_germancop"),
            ("police", "Ped_germancop"), ("soldier", "Ped_Soldier_01"), ("zombie", "Ped_zombiemale01"))


def profile_for(texts, assets):
    # A THUG2 ped profile like THAW's (its profile / type / life node profile names): by the words in them.
    text = " ".join(t.lower() for t in texts if t)
    candidates = []
    if any(w in text for w in FEMALE):
        candidates.append("random_female_profile")
    candidates += [profile for word, profile in PROFILES if word in text]
    for c in candidates:
        if assets is None or assets.profile_exists(c):
            return c
    return "random_male_profile"


class PortedNodes:
    # THAW's game objects, waypoints, peds, life nodes and cars made THUG2's nodes (see the top), in place in the array
    # (the Links stay indices into it; nodes.convert_nodes renumbers them).
    def __init__(self, array, names, sectors, col_objects, script_qbs, thug2, assets):
        self.array, self.names = array, names
        self.sectors, self.col_objects = sectors, col_objects
        self.script_qbs = script_qbs
        self.thug2, self.assets = thug2, assets
        self.ids = set()
        self.triggers = []      # (node name, script, context)
        self.scripts = set()
        self.local = set()      # the LevelObjects' names: their sectors and collision in their own space
        self.gone = set()       # LevelObjects not made (no sector): their collision left out too
        self.counts = {}
        self.life_peds = 0
        self.ped_paths = set()
        for node in array:
            if isinstance(node, Struct) and nodes.class_of(node, names) in ("pedestrian",) and self._wanted_ped(node):
                for link in self._waypoint_links(node) or []:
                    self.ped_paths.update(peds_cars.follow(array, link, names))

    def _count(self, what):
        self.counts[what] = self.counts.get(what, 0) + 1

    def _waypoint_links(self, node):
        links = node.get("Links")
        if not isinstance(links, list):
            return None
        out = [v for v in links if isinstance(v, int) and 0 <= v < len(self.array) and
               isinstance(self.array[v], Struct) and nodes.class_of(self.array[v], self.names) == "waypoint"]
        return sorted(set(out), key=out.index) or None

    def _text(self, value):
        return self.names.get(value.value, "") if isinstance(value, Name) else ""

    def _base(self, node, cls):
        n = self.names
        return [(_key(n, "Pos"), node.get("Pos")), (_key(n, "Angles"), node.get("Angles") or ("vector", (0.0, 0.0, 0.0))),
                (_key(n, "Name"), node.get("Name")), (_key(n, "Class"), _key(n, cls))]

    def _flags(self, node, *flags):
        return [(None, _key(self.names, f)) for f in flags if node.has_flag(f)]

    def _made(self, entries, node=None, context=None, script=None):
        s = Struct(entries)
        self.ids.add(id(s))
        script = script if script is not None else (node.get("TriggerScript") if node is not None else None)
        if context is not None and isinstance(script, Name) and isinstance(node.get("Name"), Name):
            self.triggers.append((node.get("Name").value, script.value, context))
            self.scripts.add(script.value)
        return s

    def _left_out(self, what):
        self._count(what)
        return Struct([(Name(checksum("Class")), Name(checksum("LeftOut")))])

    def _wanted_ped(self, node):
        created = node.get("CreatedFromVariable")
        if not (node.has_flag("CreatedAtStart") or
                (isinstance(created, Name) and self._text(created).lower() == "nodeflag_permanent_peds")):
            return False
        words = (self._text(node.get("Type")) + " " + self._text(node.get("profile"))).lower()
        return "anl_" not in words and "goal" not in words

    def convert(self, node):
        # The node made THUG2's, a LeftOut placeholder, or None (left to nodes.convert_nodes).
        n = self.names
        cls = nodes.class_of(node, n)
        name = node.get("Name")
        if cls not in ("levelobject", "gameobject", "waypoint", "pedestrian", "lifenode", "vehicle"):
            return None
        if not isinstance(name, Name) or not isinstance(node.get("Pos"), tuple):
            return self._left_out("%s nodes without a name or Pos" % cls)
        if cls == "levelobject":
            return self.level_object(node)
        if cls == "gameobject":
            if node.get("model") != "none" or node.get("profile") is not None:
                # (a model, or a profile naming one: THAW's bouncy barricades, chairs...)
                return self._left_out("game objects with THAW models left out")
            self._count("script objects")
            return self._made(self._base(node, "GameObject") + [(_key(n, "Type"), _key(n, "Ghost"))] +
                              self._flags(node, "CreatedAtStart", "AbsentInNetGames") +
                              [(None, _key(n, "NeverSuspend")), (_key(n, "model"), "none"),
                               (_key(n, "lod_dist1"), 400), (_key(n, "lod_dist2"), 401)] + self._links(node),
                              node, script_port.OBJECT)
        if cls == "waypoint":
            return self.waypoint(node)
        if cls == "pedestrian":
            return self.pedestrian(node)
        if cls == "lifenode":
            return self.life_node(node)
        return self.vehicle(node)

    def _links(self, node):
        links = self._waypoint_links(node)
        return [(_key(self.names, "Links"), links)] if links else []

    def level_object(self, node):
        name = node.get("Name").value
        if name not in self.sectors:
            # (THAW's bouncy props of the story's scene: not in the net scene)
            self.gone.add(name)
            return self._left_out("moving objects without a sector left out")
        entries = []
        for key, value in node.entries:
            if key is None:
                if isinstance(value, Name) and value.value in OBJECT_FLAGS:
                    entries.append((key, value))
            elif key.value in OBJECT_KEYS:
                if key.value == checksum("CollisionMode") and name not in self.col_objects:
                    continue
                entries.append((key, value))
        entries += self._links(node)
        self.local.add(name)
        self._count("bouncy objects" if node.has_flag("bouncy") else "moving objects")
        return self._made(entries, node, script_port.OBJECT)

    def waypoint(self, node):
        n = self.names
        flags = [(None, _key(n, "CreatedAtStart"))] if node.has_flag("CreatedAtStart") else []
        ped = []
        if self._index(node) in self.ped_paths:
            ped = [(_key(n, "Type"), _key(n, "PedAI")), (None, _key(n, "Continue")), (_key(n, "PedType"), _key(n, "Walk")),
                   (_key(n, "Priority"), _key(n, "Normal")), (_key(n, "ContinueWeight"), 1.0)]
        self._count("waypoints")
        return self._made(self._base(node, "Waypoint") + flags + ped + self._links(node))

    def _index(self, node):
        if not hasattr(self, "_indices"):
            self._indices = {id(x): k for k, x in enumerate(self.array)}
        return self._indices.get(id(node))

    def _ped(self, node, profile, walking, links, name=None):
        n = self.names
        entries = self._base(node, "Pedestrian") + [(_key(n, "Type"), _key(n, "Ped_From_Profile")),
                                                     (None, _key(n, "CreatedAtStart"))]
        if name is not None:
            entries[2] = (_key(n, "Name"), name)
        entries += [(_key(n, "PedAI_Type"), _key(n, "Stand")), (_key(n, "skeletonName"), _key(n, "THPS6_human")),
                    (_key(n, "AnimName"), _key(n, "animload_THPS6_human")), (_key(n, "profile"), _key(n, profile)),
                    (_key(n, "SuspendDistance"), 10000), (_key(n, "lod_dist1"), 200), (_key(n, "lod_dist2"), 201),
                    (_key(n, "TriggerScript"), _key(n, PED_WALK if walking else PED_STAND))]
        if links:
            entries.append((_key(n, "Links"), links))
        self._count("walking peds" if walking else "standing peds")
        return self._made(entries)

    def pedestrian(self, node):
        if not self._wanted_ped(node):
            return self._left_out("goal / story / animal peds left out")
        links = self._waypoint_links(node)
        profile = profile_for([self._text(node.get("profile")), self._text(node.get("Type"))], self.assets)
        return self._ped(node, profile, bool(links), links)

    def life_node(self, node):
        if not node.has_flag("CreatedAtStart"):
            return self._left_out("life nodes not there at the start")
        if self.life_peds >= MAX_LIFE_PEDS:
            return self._left_out("life nodes over %d left out" % MAX_LIFE_PEDS)
        self.life_peds += 1
        profile = profile_for([self._text(node.get("ProfileName")), self._text(node.get("Type"))], self.assets)
        return self._ped(node, profile, False, None)

    def _moves(self, script, depth=0):
        # Does the script (or one it calls, 2 deep) drive a car along its path?
        for q in self.script_qbs:
            for called, _ in q.calls(script):
                text = self.names.get(called, "").lower()
                if text in ("car_generic01", "obj_followpathlinked"):
                    return True
                if depth < 2 and called != script and self._moves(called, depth + 1):
                    return True
        return False

    def vehicle(self, node):
        n = self.names
        if not node.has_flag("CreatedAtStart"):
            return self._left_out("cars not there at the start left out")
        model = node.get("model")
        if not (isinstance(model, str) and self.assets is not None and self.assets.model_exists(model)):
            model = peds_cars.FALLBACK_CAR
        trigger = node.get("TriggerScript")
        links = self._waypoint_links(node)
        driving = bool(links) and isinstance(trigger, Name) and self._moves(trigger.value)
        entries = self._base(node, "Vehicle") + [(_key(n, "Type"), _key(n, "Car"))] + \
            self._flags(node, "CreatedAtStart", "AbsentInNetGames") + [
            (_key(n, "model"), model), (_key(n, "skeletonName"), _key(n, "car")), (_key(n, "SuspendDistance"), 10000),
            (_key(n, "lod_dist1"), 400), (_key(n, "lod_dist2"), 401)]
        if driving:
            entries += [(_key(n, "TriggerScript"), _key(n, CAR_DRIVE)), (_key(n, "Links"), links)]
        self._count("driving cars" if driving else "parked cars")
        return self._made(entries)


# --- the level's scripts ------------------------------------------------------------------------------------------

# THUG2 commands left out of THAW's scripts besides script_port.LEFT_OUT: the goal manager (THAW's goals aren't
# THUG2's: a goal it doesn't know may be read through a null), screen elements and panels (THAW's ids and
# parents), the game's global state (flags, globals, score, slow motion: a left-out statement could leave it set).
LEFT_OUT = {name.lower() for name in (
    "init_goal_manager", "play_cutscene", "create_panel_message", "create_panel_block", "show_panel_messages",
    "show_goal_panel_messages", "kill_panel_message_if_it_exists", "CreateScreenElement", "SetScreenElementLock",
    "SetScreenElementProps", "DestroyScreenElement", "RunScriptOnScreenElement", "SetSlomo", "Unpause_Balance_Meter",
    "setup_ped_speech", "ResetScore", "Change", "SetFlag", "UnSetFlag", "GetFlag", "SetGlobalFlag", "GetGlobalFlag")}
LEFT_OUT_PREFIXES = ("goalmanager_", "goal_", "igc_", "playigccam", "cinematiccamera", "cutscene")


# THAW's geometry and rail scripts run on the skater (THUG2 runs a trigger's script on the skater that set it off): its
# own components' members too (THAW's speed boosts: GetSpeed / SetSpeed; `if Walking`...), not just script_port's
# safe few. A context of its own (script_port's SKATER stays as the other converters have it).
SKATER = "thaw_skater"
script_port.SAFE_COMPONENTS.setdefault(SKATER, script_port.SAFE_COMPONENTS[script_port.SKATER] | {
    "skatercorephysics", "skaterstate", "skaterphysicscontrol", "trick", "skaterbalancetrick", "skaterruntimer",
    "statsmanager", "walk"})
script_port.BY_NAME.setdefault(SKATER, script_port.BY_NAME[script_port.SKATER])


class Translator(script_port.Translator):
    def command(self, toks, contexts, discover):
        if toks and toks[0].t == 0x16:
            text = self.text(toks[0].name()).lower()
            if text in LEFT_OUT or text.startswith(LEFT_OUT_PREFIXES):
                self.note("left out: " + self.text(toks[0].name()))
                return None
        return super().command(toks, contexts, discover)


def port_scripts(converted, array, made, sources, thug2, names, provided):
    # The scripts brought along from the kept nodes' trigger scripts: the geometry's and rails' (run by the skater),
    # the made objects' (run by them). Trigger scripts that can't be are taken off their nodes. Returns ({name: THUG2
    # bytes}, the THAW globals they use as NeverScript text, a report line).
    all_names = {x.get("Name").value for x in array if isinstance(x, Struct) and isinstance(x.get("Name"), Name)}
    node_names = {x.get("Name").value for x in converted if isinstance(x.get("Name"), Name)}
    allowed = set(GLOBAL_SOURCES) | {LEVEL}
    tr = Translator(sources, thug2, lambda label: label in allowed, node_names, all_names, names,
                                provided=provided)
    roots = []
    for node in converted:
        script = node.get("TriggerScript")
        if isinstance(script, Name) and script.value not in provided and \
                nodes.class_of(node, names) in ("levelgeometry", "railnode"):
            roots.append((script.value, SKATER))
    roots += [(script, context) for _, script, context in made.triggers]
    out = tr.run(roots)

    def available(script):
        return script in out or script in thug2.scripts or script in provided

    dropped = 0
    for node in converted:
        script = node.get("TriggerScript")
        if isinstance(script, Name) and not available(script.value):
            node.remove("TriggerScript")
            dropped += 1
    trigger = checksum("TriggerScript")
    by_name = {x.get("Name").value: x for x in converted if isinstance(x.get("Name"), Name)}
    for node_name, script, _ in made.triggers:
        node = by_name.get(node_name)
        if node is not None and available(script) and node.get("TriggerScript") is None:
            node.entries.append((Name(trigger), Name(script)))
    worst = sorted(tr.left_out.items(), key=lambda kv: -kv[1])
    report = "scripts: %d brought along (%d bytes), %d trigger scripts not; made %s; left out %d statements: %s" % (
        len(out), sum(map(len, out.values())), dropped, dict(sorted(made.counts.items())), sum(tr.left_out.values()),
        ", ".join("%s x%d" % kv for kv in worst[:12]))
    return out, tr.globals_ns(), report, tr.left_out
