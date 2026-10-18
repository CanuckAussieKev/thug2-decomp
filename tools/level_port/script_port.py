# NOT ORIGINAL - another game's level scripts made runnable in THUG2 (tools/thps3_levels: THPS3's): a translation of
# the compiled scripts' token streams, script by script, from the level's node scripts down through everything they
# call, spawn or hand out as exception / gap scripts. Nothing of either game is stored: the scripts are read from the
# user's files each time and written into the converted level's .qb.
#
# The token streams are close: THPS3's names, numbers, strings, vectors, structures, arrays, <params> and line ends
# are THUG2's. The differences handled here:
#   - if / else / endif: THPS3's 0x25 / 0x26 / 0x28 carry no jump; THUG2's interpreter only runs 0x47 <u16 jump> /
#     0x48 <u16 jump> (it loops for ever on a 0x25) - made from the nesting.
#   - random: THPS3's 0x2F / 0x37 {u32 count, count x u32 offset} - THUG2's have a u16 weight per entry before the
#     offsets (each 1 here).
#   - every jump (the randoms' offsets, 0x2E) is made again from where its target token lands in the output, since
#     statements are left out and tokens change size.
#   - what THUG2 can't run (see `Translator.command`) is left out statement by statement: commands THUG2 doesn't
#     have (warnings in the log at best), member functions of components the object running the script doesn't have
#     (THUG2's CallMemberFunction doesn't check: a crash), node names the converted level doesn't have (Create of a
#     missing node reads through a null), scripts that can't be brought along (goals, menus, the skater's own), and
#     things a free-skate port shouldn't do (global flags, the skater's state machine, panel messages, cameras). An
#     `if` whose condition is left out becomes `if GotParam <never>` (false).
#   - THPS3's names THUG2 calls something else (exceptions: SkaterInRadius -> ObjectInRadius...).
#
# Scripts are run by an object or none: a game object's trigger script by that object (THUG2's LevelObject / ghost
# GameObject: motion, sound, proximity components), a sector's or rail's by the skater, a spawned one by none. Each
# script is checked against every way it's run (contexts): a member function stays only when every one of them has
# its component.
#
# The output: the scripts as THUG2 tokens (appended to the NeverScript-compiled node array .qb - see append_scripts),
# the THPS3 globals they use that THUG2 doesn't have (NeverScript text), and a report of what was left out.

import os
import struct

import prx
import qb
from qb import checksum

EOL = (0x01, 0x02)
RANDOMS = (0x2F, 0x37, 0x40, 0x41)
CONTROL = (0x26, 0x28, 0x20, 0x21, 0x22, 0x29)

# Contexts: who runs a script.
OBJECT, SKATER, NONE = "object", "skater", "none"

# Member function components (THUG2's member function tables) each context's objects have. A LevelObject / ghost
# GameObject is a composite object with motion, suspend, sound and object proximity components (and a model); the
# skater's components are its own (none of THPS3's object scripts are meant for it).
SAFE_COMPONENTS = {
    OBJECT: {"motion", "compositeobject", "object", "objectproximity", "sound", "suspend"},
    SKATER: {"compositeobject", "object"},
    NONE: set(),
}
COMPONENT_NAMES = ("motion", "compositeobject", "object", "objectproximity", "sound", "suspend", "stream",
                   "skeleton", "animblender", "pedlogic", "vehicle", "rigidbody", "walk", "skatercorephysics",
                   "skaterstate", "input", "particle", "flexibleparticle", "avoid", "skaterphysicscontrol",
                   "statsmanager", "goaleditor", "trick", "skaterruntimer", "pagermessage", "vibration", "fam",
                   "carphysics", "skaterbalancetrick", "skatervehiclesound")
# Members the object runs itself by name (no table entry), per context: the skater's gap functions; Die.
BY_NAME = {
    OBJECT: {"die", "obj_movetonode", "obj_stopsound"},
    SKATER: {"startgap", "endgap"},
    NONE: set(),
}

# C functions / THUG2 scripts left out although THUG2 has them: they'd change the game outside the level (global
# flags are the save's: unlocks), drive the skater (THPS3's state machine names), start goals / cutscenes / cameras,
# show THPS3's panel messages, load THPS3's sounds and streams (not converted), or only print.
LEFT_OUT = {name.lower() for name in (
    "SetGlobalFlag", "UnSetGlobalFlag", "MakeSkaterGoto", "PauseSkaters", "UnPauseSkaters", "PauseGame",
    "UnPauseGame", "DisablePlayerInput", "EnablePlayerInput", "PlaySkaterCamAnim", "SetSkaterCamAnimSkippable",
    "KillSkaterCamAnim", "SetGoal", "AwardStatPoint", "LaunchPanelMessage", "LaunchLocalPanelMessage",
    "LaunchConsoleMessage", "Printf", "PrintStruct", "LoadSound", "LoadStreamHeader", "AddMusicTrack", "PlayStream",
    "Obj_PlayStream", "Obj_StopStream", "StopStream", "PlayMovie", "Vibrate", "DoFlash", "SetMusicVolume",
    "KillSkater", "SK3_KillSkater", "SK3_KillSkater_Water", "SpawnSkaterScript", "Goal_GotLetter", "SetTrickName",
    "SetTrickScore", "Display", "EndOfRunDone", "SetState", "Jump", "BloodOn", "BloodOff", "ClearPanel_Landed",
    "ClearPanel_Bailed", "NotifyBailDone", "SetRollingFriction", "BashOn", "BashOff", "ChangeLevel",
    "LoadLevel", "Obj_ApplyScaling", "SwitchOnAtomic", "SwitchOffAtomic", "Obj_PlayAnim", "Obj_WaitAnimFinished",
    "Obj_SetAnimCycleMode", "Obj_CycleAnim", "PlayAnim", "WaitAnim", "SetSkaterCamOverride",
    "ClearSkaterCamOverride", "ShowAllObjects", "EndRun", "Restart", "SetUpLights", "SetBackgroundColor",
    "SetClippingDistances", "SetBSPAmbientColor", "SetDynamicLightModulationFactor", "SetDFFAmbientColor",
    "SetDFFDirectColor", "SetDFFDirectRotation", "CareerStartLevel", "StartRunScript", "EndRunScript")}

# Parameters whose value is a script (checked: there, brought along): the key -> who runs it ("same": the caller's
# runner).
SCRIPT_KEYS = {"scr": "same", "gapscript": SKATER, "trickscript": SKATER, "callback": NONE}
# Commands whose first unnamed name is a script, and who runs it.
SCRIPT_COMMANDS = {"spawnscript": NONE, "goto": "same", "gotopreserveparams": "same", "obj_spawnscript": "same",
                   "obj_switchscript": "same", "gotorandomscript": "same"}
# C functions that look a geometry node's scene sector up (Translator.without_undrawn).
SECTOR_COMMANDS = {"visible", "invisible", "create", "kill", "shatter"}
# Parameters naming a node or object (the statement is left out when the converted level hasn't it).
NODE_KEYS = {"name", "node", "nodename", "target"}

# THPS3 names THUG2 calls otherwise (values anywhere in a statement).
RENAMES = {"SkaterInRadius": "ObjectInRadius", "SkaterOutOfRadius": "ObjectOutofRadius"}

# A condition that is always false (an `if` whose command is left out): GotParam of a name no script passes.
NEVER = "thps3_port_never"

# THPS3's script files a level's scripts may be brought along from (the level's own .qb and the level designers'
# shared files); anything else (goals, menus, the skater, peds' model scripts...) is THPS3's game, not the level's.
DEFAULT_SOURCES = ("cjr_scripts.qb", "jku_scripts.qb", "ajc_scripts.qb", "alf_scripts.qb", "bdj_scripts.qb",
                   "cpf_scripts.qb", "js_scripts.qb", "sk3_scripts.qb", "skutils.qb")


def u32(b, at=0):
    return struct.unpack_from("<I", b, at)[0]


class Tok:
    __slots__ = ("t", "p", "i", "targets")

    def __init__(self, t, p, i, targets=None):
        self.t, self.p, self.i, self.targets = t, p, i, targets

    def name(self):
        return u32(self.p) if self.t == 0x16 else None

    def encode(self):
        if self.t in (0x1B, 0x1C):
            return bytes([self.t]) + struct.pack("<I", len(self.p)) + self.p
        if self.t in EOL:
            return b"\x01"
        return bytes([self.t]) + self.p


def name_tok(value, i):
    return Tok(0x16, struct.pack("<I", value), i)


# --- what THUG2 has ------------------------------------------------------------------------------------------------

class Thug2:
    # THUG2's commands, read from the user's game: its C functions (the CFunction table in THUG2PM.exe: name, function),
    # its member functions (the components' tables: {u32 name checksum, u32 component, 16 bytes}) and its scripts and
    # globals (Data\pre\qb_scripts.prx).
    CFUNC_TABLE, CFUNC_COUNT = 0x67F3A0, 0x4E6

    def __init__(self, thug2):
        exe = None
        for name in ("THUG2PM.exe", "THUG2.exe"):
            path = os.path.join(thug2, name)
            if os.path.exists(path):
                exe = open(path, "rb").read()
                break
        self.root = thug2
        if exe is None:
            raise FileNotFoundError("no THUG2PM.exe in %s" % thug2)
        self.cfuncs = {checksum(n) for n in self._cfunc_names(exe)}
        self.members = self._members(exe)
        self.scripts, self.globals = set(), set()
        self.bodies, self.needs = {}, {}
        for name, data in prx.entries(os.path.join(thug2, "Data", "pre", "qb_scripts.prx"),
                                      lambda n: n.lower().endswith(".qb")):
            self._read_qb(data)

    @staticmethod
    def _cfunc_names(exe):
        pe = u32(exe, 0x3C)
        first = pe + 24 + struct.unpack_from("<H", exe, pe + 20)[0]
        sections = [struct.unpack_from("<8sIIII", exe, first + k * 40)
                    for k in range(struct.unpack_from("<H", exe, pe + 6)[0])]

        def offset(va):
            rva = va - 0x400000
            for _, _, virtual, raw_size, raw in sections:
                if virtual <= rva < virtual + raw_size:
                    return raw + rva - virtual
            return None

        names = []
        table = offset(Thug2.CFUNC_TABLE)
        for k in range(Thug2.CFUNC_COUNT):
            text = offset(u32(exe, table + 8 * k))
            if text is None:
                continue
            names.append(exe[text:exe.index(b"\0", text)].decode("latin-1"))
        if not names or names[0] != "Wait":
            raise ValueError("THUG2PM.exe: the CFunction table isn't at 0x%X" % Thug2.CFUNC_TABLE)
        return names

    @staticmethod
    def _members(exe):
        # {checksum: component checksum}: runs of 24-byte entries {name, component, 16 zero bytes} with one component.
        out = {}
        zero = b"\0" * 16
        for off in range(0x200000, len(exe) - 48, 4):
            if exe[off + 8:off + 24] != zero or exe[off + 32:off + 48] != zero:
                continue
            n, t = struct.unpack_from("<II", exe, off)
            n2, t2 = struct.unpack_from("<II", exe, off + 24)
            if t == t2 and n and t and n != n2:
                out[n] = t
                out[n2] = t2
        return out

    def _read_qb(self, data):
        toks = list(qb.tokens(data, weights=True))
        depth = 0
        current = None
        for k, (_, t, p) in enumerate(toks):
            if t == 0x23 and k + 1 < len(toks) and toks[k + 1][1] == 0x16:
                current = u32(toks[k + 1][2])
                self.scripts.add(current)
                self.bodies[current] = set()
                depth += 1
            elif t == 0x24:
                depth -= 1
            elif t == 0x16 and depth == 0 and k + 1 < len(toks) and toks[k + 1][1] == 0x07:
                self.globals.add(u32(p))
            elif t == 0x16 and depth > 0 and current is not None and toks[k - 1][1] != 0x42 and \
                    not (k + 1 < len(toks) and toks[k + 1][1] == 0x42):
                # (the names a script uses - not `object:` nor what follows it: those run on another object)
                self.bodies[current].add(u32(p))

    def component(self, name):
        return self.members.get(name)

    def script_needs(self, name, _seen=None):
        # The components (member function table checksums) a THUG2 script needs of the object running it: its member
        # function calls and those of the THUG2 scripts it names (calls, exception scripts), all the way down.
        if name in self.needs:
            return self.needs[name]
        seen = _seen if _seen is not None else set()
        seen.add(name)
        out = set()
        for used in self.bodies.get(name, ()):
            if used in self.members:
                out.add(self.members[used])
            elif used in self.scripts and used not in seen:
                out |= self.script_needs(used, seen)
        if _seen is None:
            self.needs[name] = out
        return out


# --- THPS3's scripts -----------------------------------------------------------------------------------------------

class Source:
    # A .qb file's scripts as token lists (jumps as the indices of their target tokens) and its names / globals.
    def __init__(self, data, label):
        self.label = label
        self.names = {}
        self.scripts = {}
        toks = list(qb.tokens(data))
        at = {off: k for k, (off, _, _) in enumerate(toks)}
        k = 0
        while k < len(toks):
            off, t, p = toks[k]
            if t == 0x2B:
                self.names[u32(p)] = p[4:-1].decode("latin-1")
            elif t == 0x23 and k + 1 < len(toks) and toks[k + 1][1] == 0x16:
                name = u32(toks[k + 1][2])
                body, j = [], k + 2
                while toks[j][1] != 0x24:
                    o2, t2, p2 = toks[j]
                    targets = None
                    if t2 == 0x2E:
                        targets = [at[o2 + 5 + struct.unpack("<i", p2)[0]]]
                    elif t2 in RANDOMS:
                        n = u32(p2)
                        targets = [at[o2 + 5 + 4 * m + 4 + u32(p2, 4 + 4 * m)] for m in range(n)]
                    body.append(Tok(t2, p2, j, targets))
                    j += 1
                self.scripts[name] = body
                k = j
            k += 1
        try:
            self.globals = dict((key.value, value) for key, value in qb.QbFile(data, label).globals)
        except Exception:  # (a file whose globals the reader can't take: its scripts are still usable)
            self.globals = {}


def random_end(tok, body):
    # The index of the token after an argument random (the target of its branches' closing jumps).
    if len(tok.targets) < 2:
        return tok.i
    for b in body:
        if b.i == tok.targets[1] - 1 and b.t == 0x2E:
            return b.targets[0]
    return tok.i


def statements(body):
    # The script body as statements (token lists, each with its line end): the default-parameters line first; a
    # statement-level random and each of its branches' closing jumps are statements of their own (their branches'
    # statements follow); an argument random (inside a statement) stays in its statement, line ends and all.
    out, cur, depth, until = [], [], 0, -1
    for tok in body:
        content = any(x.t not in EOL for x in cur)
        if depth == 0 and tok.i >= until and (tok.t in RANDOMS and not content or tok.t == 0x2E):
            if cur:
                out.append(cur)
            out.append([tok])
            cur = []
            continue
        cur.append(tok)
        if tok.t in (0x03, 0x05, 0x0E):
            depth += 1
        elif tok.t in (0x04, 0x06, 0x0F):
            depth -= 1
        elif tok.t in RANDOMS:
            until = max(until, random_end(tok, body))
        elif tok.t in EOL and depth == 0 and tok.i >= until:
            out.append(cur)
            cur = []
    if cur:
        out.append(cur)
    return out


def args_of(toks):
    # A statement's (key, value token) pairs and unnamed values (key None), from the tokens after the command.
    out, k = [], 0
    while k < len(toks):
        t = toks[k]
        if t.t == 0x16 and k + 2 < len(toks) and toks[k + 1].t == 0x07:
            out.append((t.name(), toks[k + 2]))
            k += 3
            continue
        out.append((None, t))
        k += 1
    return out


class Translator:
    def __init__(self, sources, thug2, translatable, node_names, all_node_names, names, provided=(),
                 inherit_params=False, undrawn=(), left_out=(), renames=None, objects=None, components=None,
                 full=False, by_name=None, rewrites=None):
        # sources: Source list (later ones win); translatable(source label) -> may its scripts be brought along;
        # node_names: the converted level's nodes; all_node_names: the THPS3 level's; provided: scripts the level's
        # .qb defines otherwise (its kill scripts) - used as THUG2's own. inherit_params: a call of a brought script
        # with no arguments passes the caller's (`<...>` added): THPS3's scripts rely on it (a proximity node's script
        # calls ALF_SUB_Hide_Const_Pool, which tests `inside`); THUG2's callee gets only what it's given.
        # left_out / renames: more of LEFT_OUT / RENAMES (another game's). objects: {node name checksum: (context,
        # created at start)} - the objects `name:command` statements (token 0x42, THPS4's) may address; None: those
        # statements aren't looked at. components: {context: more SAFE_COMPONENTS}; by_name: {context: more
        # BY_NAME}. full: `name = value` statements kept and `not` conditions looked into (THPS4's; off: as before,
        # left out / kept unchecked). rewrites: {command checksum: fn(statement tokens) -> tokens} applied first.
        self.extra_left_out = {n.lower() for n in left_out}
        self.objects = objects
        self.full = full
        self.rewrites = rewrites or {}
        self.safe = {c: set(SAFE_COMPONENTS[c]) | set((components or {}).get(c, ())) for c in SAFE_COMPONENTS}
        self.by_name = {c: set(BY_NAME[c]) | {n.lower() for n in (by_name or {}).get(c, ())} for c in BY_NAME}
        self.created = set()         # objects a kept Create makes (`name:` on them is kept)
        self.created_prefixes = set()  # and the prefix= of kept Creates (lower case)
        self.referenced = set()        # node names kept statements pass as values (discovery)
        self.thug2 = thug2
        self.inherit_params = inherit_params
        # undrawn: geometry nodes the converted scene has no sector for (without_undrawn)
        self.undrawn = set(undrawn)
        self.provided = set(provided)
        self.names = names
        self.where = {}
        self.globals = {}
        for s in sources:
            for name in s.scripts:
                self.where[name] = s
            self.globals.update(s.globals)
        self.translatable = translatable
        self.node_names = set(node_names)
        self.all_node_names = set(all_node_names)
        self.contexts = {}           # script -> set of contexts
        self.left_out = {}           # reason -> count
        self.used_globals = set()
        self.components = {checksum(c): c for c in COMPONENT_NAMES}
        all_renames = dict(RENAMES, **(renames or {}))
        self.renames = {checksum(a): checksum(b) for a, b in all_renames.items()}
        for b in all_renames.values():
            self.names.setdefault(checksum(b), b)
        self.names.setdefault(checksum(NEVER), NEVER)
        self.names.setdefault(checksum("GotParam"), "GotParam")
        self.names.setdefault(checksum("Name"), "Name")

    def text(self, value):
        return self.names.get(value, "#%08X" % value)

    def note(self, reason):
        self.left_out[reason] = self.left_out.get(reason, 0) + 1

    # -- what's available --

    def is_thps3_script(self, name):
        # (THUG2's own command of the name wins: THPS3's Goto is a script, THUG2's a C function)
        return name in self.where and name not in self.thug2.scripts and name not in self.thug2.cfuncs and \
            name not in self.thug2.members and name not in self.provided

    def can_bring(self, name):
        return self.is_thps3_script(name) and self.translatable(self.where[name].label)

    def script_available(self, name):
        return name in self.thug2.scripts or name in self.provided or self.can_bring(name)

    def thug2_script_ok(self, name, contexts):
        # A THUG2 script run by these contexts' objects: every component it needs (Thug2.script_needs) they have.
        if name in self.provided:
            return True
        needs = [self.components.get(c, "%08X" % c) for c in self.thug2.script_needs(name)]
        return all(n in SAFE_COMPONENTS[c] for n in needs for c in contexts)

    def require(self, name, context):
        # A script needed in a context: True when THUG2 has it (and the context's objects can run it) or it is
        # brought along (queued).
        if name in self.provided:
            return True
        if name in self.thug2.scripts:
            return self.thug2_script_ok(name, [context])
        if not self.can_bring(name):
            return False
        have = self.contexts.setdefault(name, set())
        if context not in have:
            have.add(context)
            self._todo.append(name)
        return True

    # -- one statement --

    def command(self, toks, contexts, discover):
        # The statement's tokens (no line end) as THUG2 tokens, or None to leave it out (noted). `discover`: also
        # queue the scripts it needs.
        first = toks[0]
        if self.objects is not None and len(toks) > 2 and toks[1].t == 0x42:
            return self.on_object(toks, contexts, discover)
        if first.t != 0x16:
            return toks
        if first.name() in self.rewrites:
            toks = self.rewrites[first.name()](toks)
            if toks is None:
                self.note("left out (rewrite): " + self.text(first.name()))
                return None
        if self.full and len(toks) > 2 and toks[1].t == 0x07:
            # `name = value` (a parameter set): a script as the value brought along (run as the caller's: goto <it>)
            for value in toks[2:]:
                if value.t == 0x16 and self.is_thps3_script(value.name()):
                    if not (all(self.require(value.name(), c) for c in contexts) if discover else
                            self.script_available(value.name())):
                        self.note("script not available: " + self.text(value.name()))
                        return None
            return [name_tok(self.renames[t.name()], t.i) if t.t == 0x16 and t.name() in self.renames else t
                    for t in toks]
        name = first.name()
        text = self.text(name).lower()
        if text in LEFT_OUT or text in self.extra_left_out:
            self.note("left out: " + self.text(name))
            return None
        args = args_of(toks[1:])
        if text in SECTOR_COMMANDS and self.undrawn:
            toks = self.without_undrawn(toks, args)
            if toks is None:
                return None
            args = args_of(toks[1:])
        # node / object names the level hasn't
        for key, value in args:
            if key is not None and self.text(key).lower() in NODE_KEYS and value.t == 0x16 and \
                    value.name() in self.all_node_names and value.name() not in self.node_names:
                self.note("node not converted: " + self.text(value.name()))
                return None
        if self.full:
            # (any parameter: THPS4's scripts pass node names on - SF2_CheckThenCreateCar carname=X does
            # `IsAlive name=<carname>`, which reads through a null for a node the array hasn't; and global
            # structures of them - CJR_SF2_Fisherman_Box_Hit Fisherman03Params)
            for key, value in args:
                if value.t != 0x16:
                    continue
                found = [value.name()]
                if key is None and isinstance(self.globals.get(value.name()), qb.Struct):
                    found = [v.value for _, v in self.globals[value.name()].entries if isinstance(v, qb.Name)]
                missing = [n for n in found if n in self.all_node_names and n not in self.node_names]
                if missing:
                    self.note("node not converted (passed on): " + self.text(missing[0]))
                    return None
        # scripts passed as values
        for key, value in args:
            if value.t != 0x16:
                continue
            key_text = self.text(key).lower() if key is not None else None
            runner = SCRIPT_KEYS.get(key_text) if key_text else None
            if key is None and text in SCRIPT_COMMANDS and value is next(v for k, v in args if k is None):
                runner = SCRIPT_COMMANDS[text]
            if runner is None:
                if key is not None and value.name() in self.where and not self.script_available(value.name()):
                    self.note("script value not available: " + self.text(value.name()))
                    return None
                continue
            for context in contexts:
                runs = context if runner == "same" else runner
                ok = self.require(value.name(), runs) if discover else self.script_available(value.name()) and (
                    value.name() not in self.thug2.scripts or self.thug2_script_ok(value.name(), [runs]))
                if not ok:
                    self.note("script not available: " + self.text(value.name()))
                    return None
        # the command itself
        if self.is_thps3_script(name):
            if not self.can_bring(name):
                self.note("THPS3 script not brought: %s (%s)" % (self.text(name), self.where[name].label))
                return None
            if discover:
                for context in contexts:
                    self.require(name, context)
        elif name in self.thug2.cfuncs or name in self.provided:
            pass
        elif name in self.thug2.scripts:
            if not self.thug2_script_ok(name, contexts):
                self.note("THUG2 script needing components the runner hasn't: " + self.text(name))
                return None
        elif name in self.thug2.members:
            component = self.components.get(self.thug2.members[name], "%08X" % self.thug2.members[name])
            if not all(component in self.safe[c] for c in contexts):
                self.note("member of %s: %s" % (component, self.text(name)))
                return None
        elif all(text in self.by_name[c] for c in contexts):
            pass
        else:
            self.note("not in THUG2: " + self.text(name))
            return None
        # globals THUG2 doesn't have
        for key, value in args:
            # (not THPS3's exception numbers EX_...: THUG2's events are names - left as names they stay distinct)
            if value.t == 0x16 and value.name() in self.globals and value.name() not in self.thug2.globals and \
                    value.name() not in self.where and not self.text(value.name()).upper().startswith("EX_"):
                self.used_globals.add(value.name())
        if discover:
            self.referenced.update(v.name() for _, v in args if v.t == 0x16 and v.name() in self.all_node_names)
        if discover and text == "create":
            self.created.update(v.name() for k, v in args if k == checksum("name") and v.t == 0x16)
            self.created_prefixes.update(v.p.rstrip(b"\0").decode("latin-1").lower() for k, v in args
                                         if k == checksum("prefix") and v.t in (0x1B, 0x1C))
        # names THUG2 calls otherwise
        out = []
        for tok in toks:
            if tok.t == 0x16 and tok.name() in self.renames:
                tok = name_tok(self.renames[tok.name()], tok.i)
            out.append(tok)
        if self.inherit_params and len(toks) == 1 and self.is_thps3_script(name):
            out.append(Tok(0x2C, b"", first.i))
        return out

    def on_object(self, toks, contexts, discover):
        # `object:command args` (THPS4): the command checked as run by that object - one of `objects`, there at the
        # start or made by a kept Create (THUG2 runs a missing object's command on object 0, the skater: a member
        # function it hasn't crashes).
        target = toks[0].name()
        found = self.objects.get(target) if target is not None else None
        if found is None:
            self.note("object not converted: " + (self.text(target) if target is not None else "<param>"))
            return None
        context, at_start = found
        if not discover and not at_start and target not in self.created:
            self.note("object never created: " + self.text(target))
            return None
        rest = self.command(toks[2:], {context}, discover)
        return None if rest is None else toks[:2] + rest

    def without_undrawn(self, toks, args):
        # Visible / Invisible / Create / Kill / Shatter of geometry nodes with no scene sector (collision-only: THUG2
        # looks the sector up and reads through the null - THPS3 Airport crashed loading): name= one of them -> left
        # out; prefix= matching one -> the statement once per matching node that has a sector (name=), or left out.
        for key, value in args:
            if key is None:
                continue
            key_text = self.text(key).lower()
            if key_text == "name" and value.t == 0x16 and value.name() in self.undrawn:
                self.note("geometry without a scene sector: " + self.text(value.name()))
                return None
            if key_text == "prefix" and value.t in (0x1B, 0x1C):
                prefix = value.p.rstrip(b"\0").decode("latin-1").lower()
                matching = [n for n in sorted(self.all_node_names)
                            if self.names.get(n, "").lower().startswith(prefix)]
                if not any(n in self.undrawn for n in matching):
                    return toks
                self.note("prefix matching geometry without a scene sector: " + prefix)
                keep = [n for n in matching if n not in self.undrawn and n in self.node_names]
                if not keep:
                    return None
                out = []
                for n in keep:
                    if out:
                        out.append(Tok(0x01, b"", toks[0].i))
                    for t in toks:
                        if t is value:
                            out.append(name_tok(n, t.i))
                        elif t.t == 0x16 and t.name() == key:
                            out.append(name_tok(checksum("Name"), t.i))
                        else:
                            out.append(t)
                return out
        return toks

    def condition(self, toks, contexts, discover):
        # An if's condition: kept, or always false (`not <condition>`, 0x39, too).
        content = [t for t in toks if t.t not in EOL]
        if len(content) > 1 and content[0].t == 0x39 and self.full:
            kept = self.condition(content[1:], contexts, discover)
            return kept if kept[-1].t == 0x16 and kept[-1].name() == checksum(NEVER) else content[:1] + kept
        if len(content) == 1 and content[0].t == 0x16 and self.text(content[0].name()).lower() == "inside":
            # (a THPS3 proximity node's script: `if inside` - our proximity ghost passes `inside`)
            return [name_tok(checksum("GotParam"), content[0].i), content[0]]
        kept = self.command(content, contexts, discover) if content else None
        if kept is None:
            i = toks[0].i if toks else 0
            return [name_tok(checksum("GotParam"), i), name_tok(checksum(NEVER), i)]
        return kept

    # -- whole scripts --

    def run(self, roots):
        # roots: (script, context) - the node scripts. Finds every script to bring along and its contexts.
        self._todo = []
        for name, context in roots:
            self.require(name, context)
        while self._todo:
            name = self._todo.pop()
            self.translate(name, discover=True)
        self.left_out = {}
        self.used_globals = set()
        out = {name: self.translate(name, discover=False) for name in sorted(self.contexts)}
        return out

    def translate(self, name, discover):
        # The script as THUG2 bytes (0x23 name ... 0x24).
        body = self.where[name].scripts[name]
        contexts = self.contexts[name]
        items = []   # (kind, value, orig index)
        stmts = statements(body)
        ifs = []
        switches = []
        counter = [0]
        for n, st in enumerate(stmts):
            content = [t for t in st if t.t not in EOL]
            eol = [t for t in st if t.t in EOL][-1:]
            orig = st[0].i
            if n == 0 or not content:
                # (the default-parameters line, or an empty line)
                for t in st:
                    items.append(("tok", t, t.i))
                continue
            head = content[0]
            if head.t == 0x25:
                cond = self.condition(content[1:], contexts, discover)
                counter[0] += 1
                ifs.append([counter[0], False])
                items.append(("if", counter[0], orig))
                items += self.token_items(cond)
            elif head.t == 0x26:
                if not ifs:
                    raise ValueError("%s: else without if" % self.text(name))
                ifs[-1][1] = True
                items.append(("else", ifs[-1][0], orig))
                items.append(("label", ("else", ifs[-1][0]), orig))
            elif head.t == 0x28:
                if not ifs:
                    raise ValueError("%s: endif without if" % self.text(name))
                number, has_else = ifs.pop()
                if not has_else:
                    items.append(("label", ("else", number), orig))
                items.append(("label", ("end", number), orig))
                items.append(("tok", head, orig))
            elif head.t == 0x3C:
                # switch (THPS4): THPS4's case / default carry no jumps; THUG2's are `3E 49 <to the next case>
                # value` and `3F 49 <to the endswitch>`, each case body but the last ended by `49 <to the endswitch>`.
                counter[0] += 1
                switches.append([counter[0], 0])
                items += self.token_items(content)
            elif head.t in (0x3E, 0x3F):
                if not switches:
                    raise ValueError("%s: case without switch" % self.text(name))
                sw = switches[-1]
                if sw[1]:
                    items.append(("break", sw[0], orig))
                items.append(("label", ("case", sw[0], sw[1]), orig))
                items.append(("case" if head.t == 0x3E else "default", (sw[0], sw[1]), orig))
                sw[1] += 1
                items += self.token_items(content[1:])
            elif head.t == 0x3D:
                if not switches:
                    raise ValueError("%s: endswitch without switch" % self.text(name))
                number, cases = switches.pop()
                items.append(("label", ("case", number, cases), orig))
                items.append(("label", ("endswitch", number), orig))
                items.append(("tok", head, orig))
            elif head.t in RANDOMS:
                items.append(("random", head, orig))
                continue
            elif head.t == 0x2E:
                items.append(("jump", head, orig))
                continue
            elif head.t in CONTROL:
                items += self.token_items(content)
            else:
                kept = self.command(content, contexts, discover)
                if kept is None:
                    continue
                items += self.token_items(kept)
            for t in eol:
                items.append(("tok", t, t.i))
        if ifs:
            raise ValueError("%s: if without endif" % self.text(name))
        return self.assemble(name, items)

    @staticmethod
    def token_items(toks):
        # A statement's tokens as output items (an argument random's tokens re-encoded, jumps made again).
        out = []
        for t in toks:
            kind = "random" if t.t in RANDOMS else "jump" if t.t == 0x2E else "tok"
            out.append((kind, t, t.i))
        return out

    def assemble(self, name, items):
        head = b"\x23\x16" + struct.pack("<I", name)
        size = {"if": 3, "else": 3, "jump": 5, "label": 0, "break": 3, "case": 4, "default": 4}
        positions, pos = [], len(head)
        for kind, value, orig in items:
            positions.append(pos)
            if kind == "tok":
                pos += len(value.encode())
            elif kind == "random":
                pos += 5 + 6 * len(value.targets)
            else:
                pos += size[kind]
        end = pos
        labels = {value: positions[k] for k, (kind, value, _) in enumerate(items) if kind == "label"}
        origs = [orig for _, _, orig in items]

        def at(target):
            # where the first output item from the target token on lands
            for k, o in enumerate(origs):
                if o >= target:
                    return positions[k]
            return end

        out = bytearray(head)
        for k, (kind, value, orig) in enumerate(items):
            here = positions[k]
            if kind == "tok":
                out += value.encode()
            elif kind == "if":
                out += b"\x47" + struct.pack("<H", labels[("else", value)] - (here + 1))
            elif kind == "else":
                out += b"\x48" + struct.pack("<H", labels[("end", value)] - (here + 1))
            elif kind == "jump":
                out += b"\x2E" + struct.pack("<i", at(value.targets[0]) - (here + 5))
            elif kind == "break":
                out += b"\x49" + struct.pack("<H", labels[("endswitch", value)] - (here + 1))
            elif kind == "case":
                out += b"\x3E\x49" + struct.pack("<H", labels[("case", value[0], value[1] + 1)] - (here + 2))
            elif kind == "default":
                out += b"\x3F\x49" + struct.pack("<H", labels[("endswitch", value[0])] - (here + 2))
            elif kind == "random":
                n = len(value.targets)
                out += bytes([value.t]) + struct.pack("<I", n) + struct.pack("<%dH" % n, *([1] * n))
                for m, target in enumerate(value.targets):
                    field = here + 5 + 2 * n + 4 * m
                    out += struct.pack("<I", at(target) - (field + 4))
        out += b"\x24\x01"
        return bytes(out)

    def globals_ns(self):
        # The THPS3 globals the scripts use (THUG2 hasn't them), as NeverScript text.
        lines = []
        for value in sorted(self.used_globals):
            v = self.globals[value]
            if _has_local(v):
                # (NeverScript doesn't read 'local strings': THPS4's trick structures' names)
                continue
            try:
                lines.append("%s = %s" % (qb.name_text(value, self.names), qb.to_ns(v, self.names)))
            except TypeError:
                continue
        return "\n".join(lines) + ("\n" if lines else "")


def _has_local(value):
    if isinstance(value, qb.Local):
        return True
    if isinstance(value, list):
        return any(_has_local(v) for v in value)
    if isinstance(value, qb.Struct):
        return any(_has_local(v) for _, v in value.entries)
    return False


def append_scripts(compiled, scripts, names):
    # A NeverScript-compiled .qb (ends with 0x00) with the scripts' bytes added, and the names of the checksums they
    # use (0x2B entries: for reading the file back; the game notes them).
    if not compiled.endswith(b"\0"):
        raise ValueError("compiled .qb doesn't end with 0x00")
    # (the scripts before the file's checksum names, as NeverScript writes a file: its decompiler stops at them)
    names_at = next((off for off, t, _ in qb.tokens(compiled, weights=True) if t in (0x2B, 0x00)), len(compiled) - 1)
    out = bytearray(compiled[:names_at])
    tail = compiled[names_at:-1]
    used = set()
    for code in scripts.values():
        out += code
        for t, p in _plain_tokens(code):
            if t == 0x16:
                used.add(u32(p))
    for value in sorted(used):
        text = names.get(value)
        if text and checksum(text) == value:
            out += b"\x2B" + struct.pack("<I", value) + text.encode("latin-1") + b"\0"
    out += tail + b"\0"
    return bytes(out)


def does_something(code):
    # Whether a translated script (Translator.run's bytes) has a statement left besides if / else and their
    # conditions: a name starting a line (after a line end, not after an `if`).
    previous = None
    for _, t, _ in qb.tokens(code[6:] + b"\0", weights=True):
        if t == 0x00:
            break
        if t == 0x16 and previous in EOL:
            return True
        previous = t
    return False


def _plain_tokens(code):
    # (token, payload) of THUG2-format code (with the random weights).
    for _, t, p in qb.tokens(code + b"\0", weights=True):
        if t == 0x00:
            break
        if t in (0x47, 0x48, 0x49):
            continue
        yield t, p
