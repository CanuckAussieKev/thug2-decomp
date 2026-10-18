# THPS4 / THUG1 node arrays -> THUG2: keep what THUG2 understands without the other game's scripts and assets
# (restarts, rails, level geometry, KOTH crowns), drop the rest (peds, vehicles, game objects with the game's models,
# goal nodes, waypoints, particles, lights...), renumber the Links.

import re

import qb
from qb import Name, Struct, checksum

KEEP_CLASSES = {"restart", "railnode", "levelgeometry", "levelobject", "genericnode"}
GENERIC_TYPES = {"crown"}  # GenericNode kept: KOTH crowns
# Keys and flags kept on a node; everything else (TriggerScript, models, ped / object settings...) goes.
KEEP_KEYS = {"position", "angles", "name", "class", "type", "restartname", "restart_types", "terraintype", "links",
             "cluster", "collisionmode"}
KEEP_FLAGS = {"createdatstart", "trickobject", "absentinnetgames", "netenabled"}
_KEYS = {checksum(k) for k in KEEP_KEYS}
_FLAGS = {checksum(k) for k in KEEP_FLAGS}


def class_of(node, names):
    value = node.get("Class")
    return (names.get(value.value, "") if isinstance(value, Name) else "").lower()


def type_of(node, names):
    value = node.get("Type")
    return (names.get(value.value, "") if isinstance(value, Name) else "").lower()


def kill_scripts(node_q, level_q):
    # THPS4's trigger scripts that kill the skater (out of bounds, water): TriggerScript -> the THUG2 command that
    # does the same (THUG2's global SK3_KillSkater / SK3_KillSkater_Water: back to a restart). A trigger script
    # counts when it calls a script or command with "kill" in its name (ALC_KillWater, SK3_KillSkater,
    # xxx_Kill_Plane...; not the Kill command, which removes objects, or KillSpawnedScript).
    names = dict(level_q.names) if level_q else {}
    names.update(node_q.names)
    found = {}
    for script in node_q.scripts:
        for called, line in node_q.calls(script):
            text = names.get(called, "").lower()
            if "kill" not in text or text in ("kill", "killspawnedscript") or text.startswith("killspawned"):
                continue
            water = "water" in text or any(t == 0x16 and names.get(int.from_bytes(p, "little"), "").lower() == "water"
                                           for t, p in line)
            found[script] = "SK3_KillSkater_Water" if water else "SK3_KillSkater"
            break
    return found


# The kill call's parameters kept (THUG2's SK3_KillSkater takes the same: the restart to go back to, the messages, the
# fade colour, bail; THUG1's own death sounds aren't in THUG2).
KILL_VALUES = {"nodename", "node", "message1", "message2", "message3", "message4", "r", "g", "b"}
KILL_FLAGS = {"bail", "nomessage"}


def kill_args(line, restarts, names):
    # The parameters of a kill call (a statement's tokens after the called name) that THUG2's SK3_KillSkater takes,
    # as {key: NeverScript text}: nodename (node= too) only when it names a restart, messages, colour, Bail /
    # NoMessage.
    args = {}
    i = 1
    while i < len(line):
        t, payload = line[i]
        if t == 0x16:
            key_text = names.get(int.from_bytes(payload, "little"), "").lower()
            if i + 2 < len(line) and line[i + 1][0] == 0x07:
                vt, vp = line[i + 2]
                if key_text in ("nodename", "node") and vt == 0x16:
                    node = int.from_bytes(vp, "little")
                    if node in restarts:
                        args["nodename"] = "nodename=%s" % qb.name_text(node, names)
                elif key_text in KILL_VALUES and vt == 0x1B:
                    args[key_text] = '%s="%s"' % (key_text, vp.rstrip(b"\0").decode("latin-1")
                                                  .replace("\\", "\\\\").replace('"', '\\"'))
                elif key_text in KILL_VALUES and vt == 0x17:
                    args[key_text] = "%s=%d" % (key_text, int.from_bytes(vp, "little", signed=True))
                i += 3
                continue
            if key_text in KILL_FLAGS:
                args[key_text] = {"bail": "Bail", "nomessage": "NoMessage"}[key_text]
        i += 1
    return args


def kill_commands(script_qbs, restarts, names):
    # THUG1's trigger scripts that kill the skater: TriggerScript -> the THUG2 command line that does the same. A
    # script counts when one of its statements calls something with "killskater" in its name (SK3_Killskater,
    # Sk3_Killskater_Water, the levels' own RU_KillSkater / HI_KillSkater_LittleDogs) or a level script that does
    # (NY_KillWater, HI_Pool_Water: goal checks around Sk3_Killskater_Water): it becomes THUG2's SK3_KillSkater
    # (_Water when the name says water, or the level script's call does) with the call's parameters that THUG2's
    # takes (kill_args); a level script's own kill call gives the ones the trigger script's call doesn't
    # (HI_Pool_Water's nodename=TRG_Restart_Boardwalk).
    wrappers = {}
    for q in script_qbs:
        for script in q.scripts:
            for called, line in q.calls(script):
                text = names.get(called, "").lower()
                if "killskater" in text and script not in wrappers:
                    wrappers[script] = ("water" in text, kill_args(line, restarts, names))
    found = {}
    for q in script_qbs:
        for script in q.scripts:
            if script in found:
                continue
            for called, line in q.calls(script):
                text = names.get(called, "").lower()
                if "killskater" not in text and called not in wrappers:
                    continue
                water, args = wrappers.get(called, (False, {}))
                water = water or "water" in text
                args = dict(args)
                args.update(kill_args(line, restarts, names))
                command = "SK3_KillSkater_Water" if water else "SK3_KillSkater"
                found[script] = " ".join([command] + list(args.values()))
                break
    return found


def expand_includes(array, globals_by_checksum):
    # THUG1's node arrays share their common parts: a node lists a global structure by name (ncomp_RailNode_574 =
    # {Class=RailNode Type=Metal ...}) - an unnamed Name entry naming a global structure, whose entries the node gets
    # (the script's AppendStructure). They are put in the nodes themselves here.
    out = []
    for node in array:
        if isinstance(node, Struct):
            entries = []
            for key, value in node.entries:
                included = globals_by_checksum.get(value.value) if key is None and isinstance(value, Name) else None
                if isinstance(included, Struct):
                    entries.extend(included.entries)
                else:
                    entries.append((key, value))
            node = Struct(entries)
        out.append(node)
    return out


def convert_nodes(array, names, kills=None, extra_keys=(), as_is=(), scripts=()):
    # extra_keys: more keys kept (THUG1's Pos). as_is: nodes (by id) already made THUG2's - kept whole, their Links
    # renumbered (THPS3's game objects, waypoints, peds). scripts: trigger scripts that are kept besides the kill
    # scripts (THPS3's, brought along by tools/level_port/script_port.py).
    kills = kills or {}
    as_is = set(as_is)
    keep_keys = _KEYS | {checksum(k) for k in extra_keys}
    kept, index_map = [], {}
    for i, node in enumerate(array):
        if not isinstance(node, Struct):
            continue
        if id(node) in as_is:
            index_map[i] = len(kept)
            kept.append(node)
            continue
        cls = class_of(node, names)
        if cls not in KEEP_CLASSES:
            continue
        if cls == "genericnode" and type_of(node, names) not in GENERIC_TYPES:
            continue
        index_map[i] = len(kept)
        kept.append(node)
    out = []
    stats = {}
    for node in kept:
        cls = class_of(node, names)
        stats[cls] = stats.get(cls, 0) + 1
        if id(node) in as_is:
            links = checksum("Links")
            entries = []
            for key, value in node.entries:
                if key is not None and key.value == links:
                    value = [index_map[v] for v in value if isinstance(v, int) and v in index_map]
                    if not value:
                        continue
                entries.append((key, value))
            out.append(Struct(entries))
            continue
        entries = []
        for key, value in node.entries:
            if key is None:
                if isinstance(value, Name) and value.value in _FLAGS:
                    entries.append((None, value))
                continue
            if key.value == checksum("TriggerScript") and isinstance(value, Name) and \
                    (value.value in kills or value.value in scripts):
                entries.append((key, value))
                continue
            if key.value not in keep_keys:
                continue
            if key.value == checksum("Links"):
                value = [index_map[v] for v in value if isinstance(v, int) and v in index_map]
                if not value:
                    continue
            if key.value == checksum("Class") and cls == "levelobject":
                # (a movable object in THPS4, driven by its scripts: static geometry here)
                value = Name(checksum("LevelGeometry"))
                names.setdefault(value.value, "LevelGeometry")
            entries.append((key, value))
        new = Struct(entries)
        if cls == "restart" and new.get("restart_types") is None and isinstance(new.get("Type"), Name):
            new.entries.append((Name(checksum("restart_types")), [new.get("Type")]))
            names.setdefault(checksum("restart_types"), "restart_types")
        out.append(new)
    # The skater starts at the first node of the array that is a player restart (as make_menu_level.py's skate
    # shop): THPS4's Player1 restart is swapped with the first node, and the Links follow.
    player1 = checksum("Player1")
    first = next((i for i, n in enumerate(out) if isinstance(n.get("Type"), Name) and n.get("Type").value == player1
                  and class_of(n, names) == "restart"), None)
    if first:
        out[0], out[first] = out[first], out[0]
        swap = {0: first, first: 0}
        links = checksum("Links")
        for node in out:
            node.entries = [(k, [swap.get(v, v) for v in value] if k is not None and k.value == links else value)
                            for k, value in node.entries]
    return out, stats


def node_position(node):
    value = node.get("Pos")
    if value is None:
        value = node.get("Position")
    return value[1] if isinstance(value, tuple) and value[0] == "vector" else None


def add_kill_restarts(nodes, kills, names):
    # THUG2's SK3_KillSkater needs a restart: without nodename= it passes the global kill_skater_nodename (0 until a
    # kill names one) to KillSkater node=, which finds no node - the skater stays dead, never put back (seen in THUG1
    # Hawaii's pool). A kill command without one gets the restart nearest to the first trigger node that runs it.
    restarts = [(node_position(n), n.get("Name")) for n in nodes
                if class_of(n, names) == "restart" and node_position(n) and isinstance(n.get("Name"), Name)]
    if not restarts:
        return kills
    out = dict(kills)
    for script, command in kills.items():
        if "nodename=" in command:
            continue
        where = next((node_position(n) for n in nodes if isinstance(n.get("TriggerScript"), Name)
                      and n.get("TriggerScript").value == script and node_position(n)), None)
        if where is None:
            continue
        _, restart = min(restarts, key=lambda r: sum((r[0][k] - where[k]) ** 2 for k in range(3)))
        out[script] = command.split(" ", 1)[0] + " nodename=%s" % qb.name_text(restart.value, names) + \
            (" " + command.split(" ", 1)[1] if " " in command else "")
    return out


def kill_trigger_names(nodes, kills):
    # The checksums of the kept nodes whose TriggerScript is a kill script: their collision keeps its trigger faces.
    found = set()
    for node in nodes:
        script, name = node.get("TriggerScript"), node.get("Name")
        if isinstance(script, Name) and script.value in kills and isinstance(name, Name):
            found.add(name.value)
    return found


# The terrains whose sounds (rolling, landing, grinding) a ported level loads: load_level runs SetTerrainDefault, then
# the level's own LoadTerrain script - without it only TERRAIN_DEFAULT's sounds are loaded, and rolling over wood,
# metal, concrete... is silent. THUG2's own LA loads these.
TERRAINS = ("DEFAULT", "CONCSMOOTH", "CONCROUGH", "METALSMOOTH", "METALGRATING", "WOOD", "BRICK", "TILE", "ASPHALT",
            "GRAVEL", "SIDEWALK", "GRASS", "WATER", "PLEXIGLASS", "CHAINLINK", "GRINDWIRE", "GRINDCONC",
            "GRINDCHAINLINK", "GRINDMETAL", "GRINDWOOD", "GRINDELECTRICWIRE", "GRINDGLASSMONSTER", "GRINDBANYONTREE")


def load_terrain_ns():
    return "script LoadTerrain {\n%s}" % "".join("    LoadTerrainSounds terrain=TERRAIN_%s \n" % t for t in TERRAINS)


# THUG2's terrain types (scripts\game\terrain.qb): a node's TerrainType has to be one of them.
THUG2_TERRAINS = (
    "ASPHALT BRICK CARPET CHAINLINK CONCROUGH CONCSMOOTH CONVEYOR DEFAULT DIRT DIRTPACKED FIBERGLASS GENERIC1 GENERIC2 "
    "GRASS GRASSDRIED GRAVEL GRINDBANYONTREE GRINDBRASSRAIL GRINDCABLE GRINDCATWALK GRINDCHAIN GRINDCHAINLINK "
    "GRINDCONC GRINDELECTRICWIRE GRINDGLASSMONSTER GRINDMETAL GRINDNEONLIGHT GRINDPLASTIC GRINDPLASTICBARRIER GRINDROPE "
    "GRINDROUNDMETALPOLE GRINDTANKTURRET GRINDTRAIN GRINDWIRE GRINDWOOD GRINDWOODLOG GRINDWOODRAILING ICE "
    "METALCORRUGATED METALFENCE METALFUTURE METALGRATING METALROUGH METALSMOOTH METALTIN PLEXIGLASS ROCK SAND SIDEWALK "
    "SNOW TILE WATER WETCONC WHEELS WOOD WOODFLIMSY WOODMASONITE WOODPIER WOODPLYWOOD WOODSHINGLE").split()


def fix_terrains(nodes, names):
    # THUG2 looks a rail node's TerrainType up in its terrain table and reads through a null pointer when it's missing
    # or not one of THUG2_TERRAINS (THPS3 Rio: a rail without one; Tokyo: THPS3's own typo TERRAIN_MEATALSMOOTH -
    # both crashed the game loading the level). Missing on a rail: TERRAIN_METALSMOOTH; unknown on any node: THUG2's
    # nearest name (difflib), else TERRAIN_METALSMOOTH. Returns how many were changed.
    import difflib
    key = checksum("TerrainType")
    names.setdefault(key, "TerrainType")
    known = {checksum("TERRAIN_" + t): "TERRAIN_" + t for t in THUG2_TERRAINS}

    def terrain(text):
        value = checksum(text)
        names.setdefault(value, text)
        return Name(value)

    count = 0
    for node in nodes:
        value = node.get("TerrainType")
        if value is None:
            if class_of(node, names) == "railnode":
                node.entries.append((Name(key), terrain("TERRAIN_METALSMOOTH")))
                count += 1
            continue
        if isinstance(value, Name) and value.value in known:
            continue
        text = names.get(value.value, "").upper() if isinstance(value, Name) else ""
        close = difflib.get_close_matches(text, list(known.values()), n=1, cutoff=0.8) if text else []
        replacement = terrain(close[0] if close else "TERRAIN_METALSMOOTH")
        node.entries = [(k, replacement if k is not None and k.value == key else v) for k, v in node.entries]
        count += 1
    return count


def node_array_ns(array_name, nodes, names, kills=None):
    fix_terrains(nodes, names)
    lines = ["%s = [" % array_name]
    for node in nodes:
        lines.append("    " + qb.to_ns(node, names, 1))
    lines.append("]")
    used = {node.get("TriggerScript").value for node in nodes if isinstance(node.get("TriggerScript"), Name)}
    for script, command in sorted((kills or {}).items()):
        if script in used:
            lines.append("script %s {\n    %s\n}" % (qb.name_text(script, names), command))
    lines.append(load_terrain_ns())
    return "\n".join(lines) + "\n"


def strip_missing_trick_objects(text, sectors):
    # The node array (NeverScript text) with TrickObject / Cluster taken off the LevelGeometry nodes whose sector isn't
    # in the converted scene (`sectors`: collision-only or invisible pieces, left out of the scene): ParseNodeArray
    # registers a TrickObject's sector without checking it's there (0x00588630 reads through the null - THPS3
    # Airport and THAW Minneapolis crashed loading). Rails keep theirs (no sectors). Returns (text, nodes changed).
    cleared = 0

    def fix(match):
        nonlocal cleared
        node = match.group(0)
        name = re.search(r"\bName=(#?\w+)", node, re.IGNORECASE)
        if not name or not re.search(r"\bClass=LevelGeometry\b", node, re.IGNORECASE) or \
                not re.search(r"\bTrickObject\b|\bCluster=", node, re.IGNORECASE):
            return node
        value = name.group(1)
        value = int(value[1:], 16) if value.startswith("#") else checksum(value)
        if value in sectors:
            return node
        cleared += 1
        node = re.sub(r"\n[ \t]*TrickObject[ \t]*(?=\n)", "", node, flags=re.IGNORECASE)
        return re.sub(r"\n[ \t]*Cluster=\S+[ \t]*(?=\n)", "", node, flags=re.IGNORECASE)

    return re.sub(r"\{[^{}]*\}", fix, text), cleared


# The path commands that need the path manager, which THUG2 makes only outside network games (load_level's
# AllocatePathManMemory): an object following a path in a network game reads through the missing manager
# (0x004695D0 / 0x00469620 - THPS4 College's blimp crashed a network game, as TestLevel's beach ball did).
PATH_COMMANDS = ("Obj_FollowPath", "Obj_FollowPathLinked", "Obj_FollowPathStored")
_PATH_COMMANDS = {checksum(c) for c in PATH_COMMANDS}


def _name_value(text):
    return int(text[1:], 16) if text.startswith("#") else checksum(text)


def absent_path_followers_in_net(text, scripts=None):
    # The node array (NeverScript text) with AbsentInNetGames on every CreatedAtStart node whose TriggerScript
    # follows a path (itself or through scripts it names) - what the shipped levels do for such objects. `text`'s
    # own scripts are read, and `scripts` ({checksum: compiled code}, added after compiling:
    # script_port.append_scripts). Returns (text, nodes marked).
    calls = {}  # script -> the names it uses
    for name, body in re.findall(r"(?:^|\n)script (#?\w+) \{(.*?)\n\}", text, re.S):
        calls[_name_value(name)] = {_name_value(w) for w in re.findall(r"#[0-9A-Fa-f]{8}\b|\b[A-Za-z_]\w*", body)}
    for name, code in (scripts or {}).items():
        used = set()
        for _, t, p in qb.tokens(bytes(code) + b"\0", weights=True):
            if t == 0x00:
                break
            if t == 0x16:
                used.add(int.from_bytes(p[:4], "little"))
        calls[name] = calls.get(name, set()) | used
    followers = {name for name, used in calls.items() if used & _PATH_COMMANDS}
    grew = True
    while grew:
        grew = False
        for name, used in calls.items():
            if name not in followers and used & followers:
                followers.add(name)
                grew = True
    marked = 0

    def fix(match):
        nonlocal marked
        node = match.group(0)
        script = re.search(r"\bTriggerScript=(#?\w+)", node)
        if not script or _name_value(script.group(1)) not in followers or \
                not re.search(r"\bCreatedAtStart\b", node) or re.search(r"\bAbsentInNetGames\b", node):
            return node
        marked += 1
        return re.sub(r"\bCreatedAtStart\b", "CreatedAtStart AbsentInNetGames", node, count=1)

    return re.sub(r"\{[^{}]*\}", fix, text), marked
