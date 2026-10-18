# THPS4 node arrays' pedestrians and cars -> THUG2's own: the nodes nodes.py leaves out (peds, vehicles, waypoints),
# made again as THUG2's levels write them, driven by THUG2's scripts (none of THPS4's ped / car scripts run in THUG2):
#
#   cars     a Vehicle whose trigger script calls Car_Generic01 (THPS4's and THUG2's share it: follow the linked
#            waypoints, stop for the skater and for other cars) - its model if THUG2 has it (Data\models\veh), else
#            FALLBACK_CAR; the waypoints it drives along, as plain waypoints.
#   walkers  a Pedestrian whose trigger script follows its linked path (Obj_FollowPathLinked: THPS4's walking peds) -
#            THUG2's ped AI walking (Ped_InitAI; PedAI waypoints, PedType=Walk) along the same path, its profile if
#            THUG2 has it (random_male_profile...).
#   pros     THPS4's skate shop's pros (Skateshop_PedProTest Path1= Path2=: two waypoint lines down one side of the
#            halfpipe and up the other) - THUG2's ped AI skating (Ped_InitAI skater; PedType=Skate waypoints): the two
#            lines made one loop, each lip a Vert_Flip jumping to the start of the other line, the next node down a
#            Vert_Land (as THUG2's LA vert skaters). The pros THUG2 has (PedPro_<name>, the _skateboard one first)
#            skate from their place on the deck; the others stand there watching as random skateboarders.
#
# Positions are written as THUG2's Pos (the legacy Position's z negated, as CNodeManager::AddNodeArray reads it);
# Angles as they are. The new nodes' Links are indices into the returned list: add the index of its first node
# (offset_links) when they are appended to an array. Each node's trigger script is returned as NeverScript text.

import os
import struct

import prx
import qb
from binio import FormatError
from qb import Name, Struct, checksum

# THUG2's vehicle used when THPS4's model isn't in THUG2.
FALLBACK_CAR = "veh\\veh_sedan\\veh_sedan.mdl"
# A pro without a THUG2 PedPro profile: a random skateboarder (THUG2's profiles.qb).
FALLBACK_SKATER = "Ped_Skateboard_a"
# Pros left out altogether (the user: THUG2's Bam Margera skates the vert loop with broken-looking animations).
LEFT_OUT_PROS = {"margera"}
# Vert jumps: the height over the lip (THPS4's pros: Obj_Jump speed=350 gravity=-400, ~150).
JUMP_HEIGHT = 100.0


def _name(names, text):
    value = checksum(text)
    names.setdefault(value, text)
    return Name(value)


def _text(value, names):
    return names.get(value.value, "") if isinstance(value, Name) else ""


def call_args(line, names):
    # A call statement's parameters: (flags, {key lower: value}) - flags as text, values as Name / int / float / str.
    flags, values = [], {}
    i = 1
    while i < len(line):
        t, payload = line[i]
        if t == 0x16 and i + 2 < len(line) and line[i + 1][0] == 0x07:
            vt, vp = line[i + 2]
            key = names.get(int.from_bytes(payload, "little"), "%08X" % int.from_bytes(payload, "little"))
            if vt == 0x16:
                values[key.lower()] = Name(int.from_bytes(vp, "little"))
            elif vt == 0x17:
                values[key.lower()] = int.from_bytes(vp, "little", signed=True)
            elif vt == 0x1A:
                values[key.lower()] = struct.unpack("<f", vp)[0]
            elif vt == 0x1B:
                values[key.lower()] = vp.rstrip(b"\0").decode("latin-1")
            i += 3
            continue
        if t == 0x16:
            flags.append(names.get(int.from_bytes(payload, "little"), "#%08X" % int.from_bytes(payload, "little")))
        i += 1
    return flags, values


def first_call(script, script_qbs, names):
    # The first statement of `script` that calls something: (called name lower, the statement), or (None, None).
    for q in script_qbs:
        for called, line in q.calls(script):
            return names.get(called, "").lower(), line
    return None, None


def body_calls(script_name, script_qbs, names):
    # The names (lower) every statement of the named script starts with.
    k = checksum(script_name)
    for q in script_qbs:
        if k in q.scripts:
            return {names.get(c, "").lower() for c, _ in q.calls(k)}
    return set()


def thug2_pos(node):
    pos = node.get("Pos")
    if isinstance(pos, tuple):
        return pos
    pos = node.get("Position")
    if isinstance(pos, tuple):
        x, y, z = pos[1]
        return ("vector", (x, y, -z))
    return None


class Builder:
    def __init__(self, names):
        self.names = names
        self.nodes = []      # Structs; Links as local indices
        self.scripts = {}    # script name -> NeverScript body line
        self.by_source = {}  # THPS4 array index -> local index (waypoints shared by several objects)

    def add(self, pos, angles, name, entries):
        n = self.names
        s = Struct([(_name(n, "Pos"), pos), (_name(n, "Angles"), angles or ("vector", (0.0, 0.0, 0.0))),
                    (_name(n, "Name"), name if isinstance(name, Name) else _name(n, name))] + list(entries))
        self.nodes.append(s)
        return len(self.nodes) - 1

    def flag(self, text):
        return (None, _name(self.names, text))

    def kv(self, key, value):
        # key=value with a text value as a name
        if isinstance(value, str):
            value = _name(self.names, value)
        return (_name(self.names, key), value)

    def set_links(self, index, links):
        if links:
            self.nodes[index].entries.append((_name(self.names, "Links"), list(links)))

    def script(self, name, body):
        _name(self.names, name)
        self.scripts[name] = body
        return (_name(self.names, "TriggerScript"), _name(self.names, name))


def follow(array, start, names):
    # The waypoints reachable from array index `start` (itself included), in order found.
    seen, order, todo = set(), [], [start]
    while todo:
        i = todo.pop(0)
        if i in seen or not (0 <= i < len(array)) or not isinstance(array[i], Struct):
            continue
        if _text(array[i].get("Class"), names).lower() != "waypoint":
            continue
        seen.add(i)
        order.append(i)
        todo.extend(v for v in (array[i].get("Links") or []) if isinstance(v, int))
    return order


def add_path(b, array, start_links, names, extra):
    # The waypoints reachable from `start_links` as new waypoints (each once), their Links kept; the local indices of
    # the start links.
    reach = []
    for link in start_links:
        for i in follow(array, link, names):
            if i not in reach:
                reach.append(i)
    new = [i for i in reach if i not in b.by_source]
    for i in new:
        w = array[i]
        b.by_source[i] = b.add(thug2_pos(w), w.get("Angles"), w.get("Name"), [b.kv("Class", "Waypoint")] + extra)
    for i in new:
        b.set_links(b.by_source[i], [b.by_source[v] for v in (array[i].get("Links") or []) if v in b.by_source])
    return [b.by_source[v] for v in start_links if v in b.by_source]


def chain(array, start_name, by_name):
    # A THPS4 pro path: the waypoints from the named one along the first links to the end.
    order, i = [], by_name.get(checksum(start_name) if isinstance(start_name, str) else start_name)
    while i is not None and i not in order:
        order.append(i)
        links = array[i].get("Links") or []
        i = links[0] if links else None
    return order


def convert(array, names, script_qbs, profile_exists, model_exists):
    # The new nodes for `array`'s cars, walkers and pros (see the top). script_qbs: the level's .qb files whose
    # scripts the nodes' trigger scripts are in; profile_exists(name) / model_exists(path): what THUG2 has.
    b = Builder(names)
    by_name = {}
    for i, node in enumerate(array):
        if isinstance(node, Struct) and isinstance(node.get("Name"), Name):
            by_name[node.get("Name").value] = i
    stats = {"cars": 0, "walkers": 0, "pros skating": 0, "pros standing": 0}
    for i, node in enumerate(array):
        if not isinstance(node, Struct):
            continue
        cls = _text(node.get("Class"), names).lower()
        trigger = node.get("TriggerScript")
        if cls not in ("vehicle", "pedestrian") or not isinstance(trigger, Name) or not node.has_flag("CreatedAtStart"):
            continue
        called, line = first_call(trigger.value, script_qbs, names)
        if called is None:
            continue
        flags, values = call_args(line, names)
        name = node.get("Name")
        base = [b.flag("CreatedAtStart"), b.flag("AbsentInNetGames")]

        if cls == "vehicle" and called == "car_generic01":
            model = node.get("model")
            if not (isinstance(model, str) and model_exists(model)):
                model = FALLBACK_CAR
            links = add_path(b, array, [v for v in (node.get("Links") or []) if isinstance(v, int)], names, [
                b.flag("CreatedAtStart")])
            args = " ".join(flags + ["%s=%s" % (k, qb.to_ns(v, names)) for k, v in values.items()
                                     if isinstance(v, (int, float))])
            script = b.script(_text(name, names) + "Script", "Car_Generic01 " + args)
            entries = [b.kv("Class", "Vehicle"), b.kv("Type", _text(node.get("Type"), names) or "Car")] + base + [
                (_name(names, "model"), model), b.kv("skeletonName", "car"), (_name(names, "SuspendDistance"), 10000),
                (_name(names, "lod_dist1"), 400), (_name(names, "lod_dist2"), 401), script]
            index = b.add(thug2_pos(node), node.get("Angles"), name, entries)
            b.set_links(index, links)
            stats["cars"] += 1
            continue

        if cls != "pedestrian":
            continue
        profile = node.get("profile")
        ped = [b.kv("Class", "Pedestrian")]
        common = [b.kv("PedAI_Type", "Stand"), b.kv("skeletonName", "THPS6_human"),
                  b.kv("AnimName", "animload_THPS6_human")]
        far = [(_name(names, "SuspendDistance"), 10000), (_name(names, "lod_dist1"), 200),
               (_name(names, "lod_dist2"), 201)]

        if "path1" in values and "path2" in values:
            # A pro: the THUG2 pro with the THPS4 pro's name skates; the others stand watching.
            pro = node.get("pro_name")
            pro = _text(pro, names) if isinstance(pro, Name) else ""
            if pro.lower() in LEFT_OUT_PROS:
                continue
            skating = None
            for candidate in ("PedPro_%s_skateboard" % pro, "PedPro_%s" % pro):
                if pro and profile_exists(candidate):
                    skating = candidate
                    break
            if skating is None:
                script = b.script(_text(name, names) + "Script", "Ped_InitAI stand")
                b.add(thug2_pos(node), node.get("Angles"), name, ped + [b.kv("Type", "Ped_Skateboard_A")] + base +
                      common + [b.kv("profile", FALLBACK_SKATER)] + far + [script])
                stats["pros standing"] += 1
                continue
            first = vert_loop(b, array, values["path1"], values["path2"], by_name, names)
            if first is None:
                continue
            script = b.script(_text(name, names) + "Script", "Ped_InitAI skater")
            index = b.add(thug2_pos(node), node.get("Angles"), name, ped + [b.kv("Type", "Ped_Skateboard_A")] + base +
                          common + [b.kv("profile", skating)] + far + [script])
            b.set_links(index, [first])
            stats["pros skating"] += 1
            continue

        if "obj_followpathlinked" in body_calls(called, script_qbs, names) or called == "obj_followpathlinked":
            if not (isinstance(profile, Name) and profile_exists(_text(profile, names))):
                profile = _name(names, "random_male_profile")
            links = add_path(b, array, [v for v in (node.get("Links") or []) if isinstance(v, int)], names, [
                b.kv("Type", "PedAI"), b.flag("CreatedAtStart"), b.flag("Continue"), b.kv("PedType", "Walk"),
                b.kv("Priority", "Normal"), (_name(names, "ContinueWeight"), 1.0)])
            if not links:
                continue
            script = b.script(_text(name, names) + "Script", "Ped_InitAI")
            index = b.add(thug2_pos(node), node.get("Angles"), name, ped + [b.kv("Type", "Ped_From_Profile")] + base +
                          common + [(_name(names, "profile"), profile)] + far + [script])
            b.set_links(index, links)
            stats["walkers"] += 1
    return b.nodes, b.scripts, stats


def vert_loop(b, array, path1, path2, by_name, names):
    # Path1 and Path2 (THPS4's pro lines, each from one lip across the flat to the other lip) as one THUG2 skate loop:
    # every node PedType=Skate; each line's last node (a lip) a Vert_Flip jumping to the other line's first (the lip
    # beside it), each line's second node a Vert_Land. The local index of path1's first node.
    lines = [chain(array, p.value if isinstance(p, Name) else p, by_name) for p in (path1, path2)]
    if any(len(line) < 3 for line in lines):
        return None
    starts = []
    for line in lines:
        indices = []
        for k, i in enumerate(line):
            w = array[i]
            entries = [b.kv("Class", "Waypoint"), b.kv("Type", "PedAI"), b.flag("CreatedAtStart"), b.flag("Continue")]
            if k == len(line) - 1:
                entries.append(b.flag("JumpToNextNode"))
            entries += [b.kv("PedType", "Skate"), b.kv("Priority", "Normal"), (_name(b.names, "ContinueWeight"), 1.0)]
            action = "Vert_Flip" if k == len(line) - 1 else "Vert_Land" if k == 1 else "Continue"
            entries.append(b.kv("SkateAction", action))
            if action == "Vert_Flip":
                entries.append((_name(b.names, "JumpHeight"), JUMP_HEIGHT))
            # (named after the THPS4 waypoint: a pro's lines are its own)
            indices.append(b.add(thug2_pos(w), w.get("Angles"), w.get("Name"), entries))
        for a, c in zip(indices, indices[1:]):
            b.set_links(a, [c])
        starts.append(indices)
    b.set_links(starts[0][-1], [starts[1][0]])
    b.set_links(starts[1][-1], [starts[0][0]])
    return starts[0][0]


class Thug2Assets:
    # What THUG2 has: its ped profiles (scripts\profile_ped.qb in Data\pre\qb_scripts.prx) and its models (the PC
    # game reads them from Data\models: <model>.xbx).
    def __init__(self, thug2):
        self.models = os.path.join(thug2, "Data", "models")
        data = prx.find(os.path.join(thug2, "Data", "pre", "qb_scripts.prx"), r"scripts\profile_ped.qb")
        if data is None:
            raise FormatError("qb_scripts.prx: scripts\\profile_ped.qb not found")
        self.profiles = {key.value for key, _ in qb.QbFile(data, "profile_ped.qb", weights=True).globals}

    def profile_exists(self, name):
        return checksum(name) in self.profiles

    def model_exists(self, model):
        return os.path.exists(os.path.join(self.models, model.replace("/", "\\") + ".xbx"))


def offset_links(nodes, base):
    links = checksum("Links")
    for node in nodes:
        node.entries = [(k, [v + base for v in value] if k is not None and k.value == links else value)
                        for k, value in node.entries]
    return nodes


def scripts_ns(scripts):
    return "".join("script %s {\n    %s \n}\n" % (name, body) for name, body in sorted(scripts.items()))
