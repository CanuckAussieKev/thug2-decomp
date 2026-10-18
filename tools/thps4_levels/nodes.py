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


def convert_nodes(array, names):
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


def node_array_ns(array_name, nodes, names):
    lines = ["%s = [" % array_name]
    for node in nodes:
        lines.append("    " + qb4.to_ns(node, names, 1))
    lines.append("]")
    return "\n".join(lines) + "\n"
