# THPS4 node arrays -> THUG2: keep what THUG2 understands without THPS4's scripts and assets (restarts, rails, level
# geometry, KOTH crowns), drop the rest (peds, vehicles, game objects with THPS4 models, goal nodes, waypoints),
# renumber the Links.

import qb4
from qb4 import Name, Struct, checksum

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


def convert_nodes(array, names, kills=None):
    kills = kills or {}
    kept, index_map = [], {}
    for i, node in enumerate(array):
        if not isinstance(node, Struct):
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
        entries = []
        for key, value in node.entries:
            if key is None:
                if isinstance(value, Name) and value.value in _FLAGS:
                    entries.append((None, value))
                continue
            if key.value == checksum("TriggerScript") and isinstance(value, Name) and value.value in kills:
                entries.append((key, value))
                continue
            if key.value not in _KEYS:
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


def kill_trigger_names(nodes, kills):
    # The checksums of the kept nodes whose TriggerScript is a kill script: their collision keeps its trigger faces.
    found = set()
    for node in nodes:
        script, name = node.get("TriggerScript"), node.get("Name")
        if isinstance(script, Name) and script.value in kills and isinstance(name, Name):
            found.add(name.value)
    return found


def node_array_ns(array_name, nodes, names, kills=None):
    lines = ["%s = [" % array_name]
    for node in nodes:
        lines.append("    " + qb4.to_ns(node, names, 1))
    lines.append("]")
    used = {node.get("TriggerScript").value for node in nodes if isinstance(node.get("TriggerScript"), Name)}
    for script, command in sorted((kills or {}).items()):
        if script in used:
            lines.append("script %s {\n    %s\n}" % (qb4.name_text(script, names), command))
    return "\n".join(lines) + "\n"
