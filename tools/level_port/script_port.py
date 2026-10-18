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
# (THUG1's levels keep their own peds, vehicles and bouncy LevelObjects: THUG2's objects of those classes)
PED, VEHICLE, BOUNCY = "ped", "vehicle", "bouncy"

# Member function components (THUG2's member function tables) each context's objects have. A LevelObject / ghost
# GameObject is a composite object with motion, suspend, sound and object proximity components (and a model); the
# skater's components are its own (none of THPS3's object scripts are meant for it).
SAFE_COMPONENTS = {
    OBJECT: {"motion", "compositeobject", "object", "objectproximity", "sound", "suspend"},
    SKATER: {"compositeobject", "object"},
    NONE: set(),
    # (THUG2's ped_add_components and the C++ base: the components THUG2's own levels' ped / car scripts use on them;
    # a bouncy LevelObject: bouncyobj_add_components - suspend, rigidbody, model, sound)
    PED: {"compositeobject", "object", "objectproximity", "motion", "animblender", "pedlogic", "suspend", "skeleton",
          "avoid", "fam"},
    VEHICLE: {"compositeobject", "object", "motion", "sound", "suspend", "skeleton"},
    BOUNCY: {"compositeobject", "object", "suspend", "rigidbody", "sound"},
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
    PED: {"die", "obj_movetonode"},
    VEHICLE: {"die", "obj_movetonode"},
    BOUNCY: {"die"},
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
    __slots__ = ("t", "p", "i", "targets", "weights")

    def __init__(self, t, p, i, targets=None, weights=None):
        self.t, self.p, self.i, self.targets, self.weights = t, p, i, targets, weights

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
        self.scripts, self.globals, self.anims = set(), set(), set()
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
        for k, (_, t, p) in enumerate(toks):
            if t == 0x23 and k + 1 < len(toks) and toks[k + 1][1] == 0x16:
                self.scripts.add(u32(toks[k + 1][2]))
                depth += 1
            elif t == 0x24:
                depth -= 1
            elif t == 0x16 and depth == 0 and k + 1 < len(toks) and toks[k + 1][1] == 0x07:
                self.globals.add(u32(p))
            elif t == 0x16 and u32(p) == self.DESC and k + 2 < len(toks) and toks[k + 2][1] == 0x16:
                # (the animations THUG2 loads: LoadAnim ... descChecksum=<anim> in its animload scripts)
                self.anims.add(u32(toks[k + 2][2]))

    DESC = checksum("descChecksum")

    def component(self, name):
        return self.members.get(name)


# --- THPS3's scripts -----------------------------------------------------------------------------------------------

class Source:
    # A .qb file's scripts as token lists (jumps as the indices of their target tokens) and its names / globals.
    # weights: the random tokens carry u16 weights before their offsets (THUG1's, THUG2's - not THPS3's); kept.
    def __init__(self, data, label, weights=False):
        self.label = label
        self.names = {}
        self.scripts = {}
        toks = list(qb.tokens(data, weights))
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
                    targets = w = None
                    if t2 == 0x2E:
                        targets = [at[o2 + 5 + struct.unpack("<i", p2)[0]]]
                    elif t2 in RANDOMS:
                        n = u32(p2)
                        base = 4 + (2 * n if weights else 0)
                        targets = [at[o2 + 1 + base + 4 * m + 4 + u32(p2, base + 4 * m)] for m in range(n)]
                        w = list(struct.unpack_from("<%dH" % n, p2, 4)) if weights else None
                    body.append(Tok(t2, p2, j, targets, w))
                    j += 1
                self.scripts[name] = body
                k = j
            k += 1
        try:
            self.globals = dict((key.value, value) for key, value in qb.QbFile(data, label, weights).globals)
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
    def __init__(self, sources, thug2, translatable, node_names, all_node_names, names, provided=(), objects=None,
                 left_out=(), left_out_prefixes=(), anim_check=False, kept_in=()):
        # sources: Source list (later ones win); translatable(source label) -> may its scripts be brought along;
        # node_names: the converted level's nodes; all_node_names: the THPS3 level's; provided: scripts the level's
        # .qb defines otherwise (its kill scripts) - used as THUG2's own. objects: {node name checksum: context} of
        # the converted level's objects - `name:command` calls on them are kept (checked against that context, run
        # only while the object is alive: guarded with IsAlive); without it they're left out. left_out /
        # left_out_prefixes: more commands left out (THUG1's goal manager calls...). anim_check: a parameter whose
        # key says anim (anim=, standing_anims=) naming an animation THUG2 doesn't load leaves its statement out, one
        # naming the other game's global (a list of its animations) is dropped (THUG2's scripts use their default).
        # kept_in: names of LEFT_OUT this game's scripts may use (THUG1's LoadSound: THUG2 has its sounds).
        self.objects = dict(objects or {})
        self.anim_check = anim_check
        self.kept_in = {n.lower() for n in kept_in}
        self.extra_left_out = {n.lower() for n in left_out}
        self.left_out_prefixes = tuple(p.lower() for p in left_out_prefixes)
        self.guard = None
        self.thug2 = thug2
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
        self.renames = {checksum(a): checksum(b) for a, b in RENAMES.items()}
        for b in RENAMES.values():
            self.names.setdefault(checksum(b), b)
        self.names.setdefault(checksum(NEVER), NEVER)
        self.names.setdefault(checksum("GotParam"), "GotParam")
        self.names.setdefault(checksum("IsAlive"), "IsAlive")
        self.names.setdefault(checksum("name"), "name")
        self.skaters = {checksum(n) for n in ("Skater", "Skater2")}

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

    def require(self, name, context):
        # A script needed in a context: True when THUG2 has it or it is brought along (queued).
        if name in self.thug2.scripts or name in self.provided:
            return True
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
        if first.t == 0x2D and self.objects and len(toks) > 3 and toks[1].t == 0x16 and toks[2].t == 0x42:
            # (<param>:command - an object the script was handed: checked as a game object's, run while it's alive)
            if toks[3].t != 0x16:
                self.note("object call form")
                return None
            kept = self.command(toks[3:], {OBJECT}, discover)
            if kept is None:
                return None
            self.guard = toks[:2]
            return toks[:3] + kept
        if first.t != 0x16:
            return toks
        name = first.name()
        text = self.text(name).lower()
        if self.is_left_out(text):
            self.note("left out: " + self.text(name))
            return None
        if len(toks) > 1 and toks[1].t == 0x07:
            # (name = value: a parameter set)
            return toks
        if len(toks) > 2 and toks[1].t == 0x42:
            return self.object_call(toks, discover)
        if self.anim_check:
            toks = self.checked_anims(toks)
            if toks is None:
                return None
        args = args_of(toks[1:])
        # node / object names the level hasn't
        for key, value in args:
            if key is not None and self.text(key).lower() in NODE_KEYS and value.t == 0x16 and \
                    value.name() in self.all_node_names and value.name() not in self.node_names:
                self.note("node not converted: " + self.text(value.name()))
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
                ok = self.require(value.name(), context if runner == "same" else runner) if discover else \
                    self.script_available(value.name())
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
        elif name in self.thug2.scripts or name in self.thug2.cfuncs or name in self.provided:
            pass
        elif name in self.thug2.members:
            component = self.components.get(self.thug2.members[name], "%08X" % self.thug2.members[name])
            if not all(component in SAFE_COMPONENTS[c] for c in contexts):
                self.note("member of %s: %s" % (component, self.text(name)))
                return None
        elif all(text in BY_NAME[c] for c in contexts):
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
        # names THUG2 calls otherwise
        out = []
        for tok in toks:
            if tok.t == 0x16 and tok.name() in self.renames:
                tok = name_tok(self.renames[tok.name()], tok.i)
            out.append(tok)
        return out

    def checked_anims(self, toks):
        # (anim_check) the statement without its key=<the other game's anim list> parameters, or None when it names
        # an animation THUG2 doesn't load.
        out, k = list(toks[:1]), 1
        while k < len(toks):
            t = toks[k]
            if t.t == 0x16 and k + 2 < len(toks) and toks[k + 1].t == 0x07 and toks[k + 2].t == 0x16 and \
                    "anim" in self.text(t.name()).lower():
                v = toks[k + 2].name()
                if v not in self.thug2.anims and v not in self.thug2.globals and v not in self.thug2.scripts and \
                        v not in self.where:
                    if v in self.globals:
                        self.note("anim list not in THUG2: " + self.text(v))
                        k += 3
                        continue
                    self.note("anim not in THUG2: " + self.text(v))
                    return None
            out.append(t)
            k += 1
        return out

    def is_left_out(self, text):
        return (text in LEFT_OUT and text not in self.kept_in) or text in self.extra_left_out or \
            (bool(self.left_out_prefixes) and text.startswith(self.left_out_prefixes))

    def object_call(self, toks, discover):
        # `object:command args` - on the skater (checked as run by the skater), or on one of the level's objects
        # (`objects`: checked as run by it; the statement is then run only while the object is alive - for a missing
        # object THUG2 runs the rest of the statement on object 0, the skater: self.guard asks for the IsAlive).
        obj = toks[0].name()
        if obj in self.skaters:
            context = SKATER
        elif obj in self.objects:
            context = self.objects[obj]
        else:
            if obj in self.all_node_names and obj not in self.node_names:
                self.note("node not converted: " + self.text(obj))
            else:
                self.note("object call on: " + self.text(obj))
            return None
        if toks[2].t != 0x16:
            self.note("object call form")
            return None
        kept = self.command(toks[2:], {context}, discover)
        if kept is None:
            return None
        self.guard = None if obj in self.skaters else toks[:1]
        return toks[:2] + kept

    def condition(self, toks, contexts, discover):
        # An if's condition: kept, or always false.
        content = [t for t in toks if t.t not in EOL]
        if content and content[0].t == 0x39:
            # (not <condition>: the condition checked; when it is left out, the whole condition is false)
            kept = self.condition(content[1:], contexts, discover)
            if kept[-1].t == 0x16 and kept[-1].name() == checksum(NEVER):
                return kept
            return content[:1] + kept
        if any(t.t == 0x16 and self.is_left_out(self.text(t.name()).lower()) for t in content):
            # (an expression calling something left out: GoalManager_HasWonGoal inside (... or ...))
            self.note("condition uses something left out")
            i = toks[0].i if toks else 0
            return [name_tok(checksum("GotParam"), i), name_tok(checksum(NEVER), i)]
        if len(content) == 1 and content[0].t == 0x16 and self.text(content[0].name()).lower() == "inside":
            # (a THPS3 proximity node's script: `if inside` - our proximity ghost passes `inside`)
            return [name_tok(checksum("GotParam"), content[0].i), content[0]]
        self.guard = None
        kept = self.command(content, contexts, discover) if content else None
        if kept is not None and self.guard is not None:
            # (a member of one of the level's objects as a condition: false - the object may not be there)
            self.note("condition on an object")
            kept = None
        self.guard = None
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
            elif head.t in RANDOMS:
                items.append(("random", head, orig))
                continue
            elif head.t == 0x2E:
                items.append(("jump", head, orig))
                continue
            elif head.t == 0x3C:
                # switch <value> (THUG1's: no jumps - THUG2's cases jump to the next case, their bodies past the
                # endswitch: made here, as the ifs')
                if switches:
                    switches[-1]["body"] += 1
                counter[0] += 1
                switches.append({"id": counter[0], "k": 0, "open": None, "body": 0})
                items += self.token_items(content)
                for t in eol:
                    items.append(("tok", t, t.i))
                continue
            elif head.t in (0x3E, 0x3F, 0x3D):
                if not switches:
                    raise ValueError("%s: case outside a switch" % self.text(name))
                sw = switches[-1]
                self.close_case(sw, items, orig)
                if head.t == 0x3E:
                    sw["k"] += 1
                    items.append(("case", (sw["id"], sw["k"]), orig))
                    items += self.token_items(content[1:])
                    sw["open"], sw["body"] = "case", 0
                elif head.t == 0x3F:
                    items.append(("default", sw["id"], orig))
                    sw["open"], sw["body"] = "default", 0
                else:
                    items.append(("label", ("end", sw["id"]), orig))
                    items.append(("tok", head, orig))
                    items.append(("label", ("past", sw["id"]), orig))
                    switches.pop()
                for t in eol:
                    items.append(("tok", t, t.i))
                continue
            elif head.t in CONTROL:
                items += self.token_items(content)
            else:
                self.guard = None
                kept = self.command(content, contexts, discover)
                if kept is None:
                    continue
                if self.guard is not None:
                    # (a member of one of the level's objects: only while it is alive)
                    counter[0] += 1
                    number = counter[0]
                    items.append(("if", number, orig))
                    items += self.token_items([name_tok(checksum("IsAlive"), orig), name_tok(checksum("name"), orig),
                                               Tok(0x07, b"", orig)] + list(self.guard) + [Tok(0x01, b"", orig)])
                    items += self.token_items(kept + [Tok(0x01, b"", orig)])
                    items.append(("label", ("else", number), orig))
                    items.append(("label", ("end", number), orig))
                    items.append(("tok", Tok(0x28, b"", orig), orig))
                    self.guard = None
                else:
                    items += self.token_items(kept)
            if switches:
                switches[-1]["body"] += 1
            for t in eol:
                items.append(("tok", t, t.i))
        if ifs:
            raise ValueError("%s: if without endif" % self.text(name))
        if switches:
            raise ValueError("%s: switch without endswitch" % self.text(name))
        return self.assemble(name, items)

    @staticmethod
    def close_case(sw, items, orig):
        # The end of a case's body: a statement that does nothing when it has none (THUG2 takes case labels right
        # after a matched one as fall-through labels), the jump past the endswitch, and where the case's jump to the
        # next case lands.
        if sw["open"] != "case":
            return
        if sw["body"] == 0:
            items.append(("tok", name_tok(checksum("GotParam"), orig), orig))
            items.append(("tok", name_tok(checksum(NEVER), orig), orig))
            items.append(("tok", Tok(0x01, b"", orig), orig))
        items.append(("swjump", sw["id"], orig))
        items.append(("label", ("next", sw["id"], sw["k"]), orig))

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
        size = {"if": 3, "else": 3, "jump": 5, "label": 0, "case": 4, "default": 4, "swjump": 3}
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
            elif kind == "case":
                # 3E 49 <to the next case / default / endswitch> (the offset from its own field, as the ifs')
                out += b"\x3E\x49" + struct.pack("<H", labels[("next",) + value] - (here + 2))
            elif kind == "default":
                out += b"\x3F\x49" + struct.pack("<H", labels[("end", value)] - (here + 2))
            elif kind == "swjump":
                out += b"\x49" + struct.pack("<H", labels[("past", value)] - (here + 1))
            elif kind == "random":
                n = len(value.targets)
                out += bytes([value.t]) + struct.pack("<I", n) + struct.pack("<%dH" % n, *(value.weights or [1] * n))
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
            try:
                lines.append("%s = %s" % (qb.name_text(value, self.names), qb.to_ns(v, self.names)))
            except TypeError:
                continue
        return "\n".join(lines) + ("\n" if lines else "")


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


def _plain_tokens(code):
    # (token, payload) of THUG2-format code (with the random weights).
    for _, t, p in qb.tokens(code + b"\0", weights=True):
        if t == 0x00:
            break
        if t in (0x47, 0x48, 0x49):
            continue
        yield t, p
