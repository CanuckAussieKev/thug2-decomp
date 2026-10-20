# NOT ORIGINAL - reading compiled .qb scripts the way the game walks them, for CREATE 100% CAS (career_complete.py):
#   - gap_list(): a level's gap checklist as the game builds it when the level loads (CGapChecklist::Build,
#     src/game/skate/gap_checklist.cpp, through SkateScript::ForEachNodeScriptCall / ForEachScriptCall,
#     src/game/script/nodes.cpp): every node's TriggerScript followed into the scripts it calls, every EndGap call
#     with a score= and a non-empty text= is a gap (name and score; the same name with the same score once);
#   - classic_goal_calls(): the AddGoal_* calls of a level's <prefix>_Goals script inside its
#     `if ((GameModeEquals is_classic) or (InNetGame))` block (NeverScript can't decompile every level's scripts -
#     CA_scripts.qb stops it - so this reads the tokens).
# The token format is src/game/script/qb.h's; SkipToken / SkipToEndOfStatement / the argument parsing follow
# src/game/script/qb.cpp and parse.cpp (ParseArguments, ParseStructure).

import struct
import zlib


def checksum(name):
    return (zlib.crc32(name.lower().encode("latin-1")) ^ 0xFFFFFFFF) & 0xFFFFFFFF


END_GAP = checksum("EndGap")
SET_EVENT_HANDLER = checksum("SetEventHandler")
TRIGGER_SCRIPT = checksum("TriggerScript")
GAP_SCRIPT = checksum("gapscript")
SCR = checksum("scr")
SCORE = checksum("score")
TEXT = checksum("text")
VERSION = checksum("version")
POS = checksum("pos")
POSITION = checksum("position")
MAX_DEPTH = 5  # ForEachScriptCall's kMaxDepth

_ONE = {0x01, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x11, 0x12, 0x13, 0x14,
        0x15, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2C, 0x2D, 0x30, 0x32, 0x33, 0x34, 0x35,
        0x36, 0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F, 0x42, 0x45}
_FIVE = {0x02, 0x16, 0x17, 0x18, 0x1A, 0x2E, 0x43, 0x44, 0x46}


def u32(d, i):
    return struct.unpack_from("<I", d, i)[0]


def skip_token(d, i):
    # Script::SkipToken (0 for tokens it doesn't step over: the caller stops).
    t = d[i]
    if t in _ONE:
        return i + 1
    if t in _FIVE:
        return i + 5
    if t in (0x1B, 0x1C):
        return i + 5 + u32(d, i + 1)
    if t == 0x1E:
        return i + 13
    if t == 0x1F:
        return i + 9
    if t == 0x2B:
        return d.index(b"\0", i + 5) + 1
    if t in (0x2F, 0x37, 0x40, 0x41):
        return i + 5 + u32(d, i + 1) * 6
    if t in (0x47, 0x48, 0x49):
        return i + 3
    return i


def skip_statement(d, i):
    # Script::SkipToEndOfStatement: past the end of line outside any brackets.
    depth = 0
    while True:
        t = d[i]
        if t in (0x03, 0x05, 0x0E):
            depth += 1
        elif t in (0x04, 0x06, 0x0F):
            depth -= 1
        if depth == 0 and t in (0x00, 0x01, 0x02):
            return skip_token(d, i)
        nxt = skip_token(d, i)
        if nxt == i:
            raise ValueError("token 0x%02X at 0x%X" % (t, i))
        i = nxt


def scripts_in(d):
    # {name checksum: (data, offset of the script's code: after `script <name>`)} of a .qb file.
    out = {}
    i = 0
    while i < len(d) and d[i] != 0x00:
        if d[i] == 0x23 and d[i + 1] == 0x16:
            out[u32(d, i + 2)] = (d, i + 6)
            i += 6
            while d[i] != 0x24:
                nxt = skip_token(d, i)
                if nxt == i:
                    raise ValueError("token 0x%02X at 0x%X" % (d[i], i))
                i = nxt
            continue
        nxt = skip_token(d, i)
        if nxt == i:
            raise ValueError("token 0x%02X at 0x%X" % (d[i], i))
        i = nxt
    return out


def is_name(value):
    # A name (checksum) value: ("name", checksum).
    return isinstance(value, tuple) and value[0] == "name"


class Unknown:
    # A value this reader doesn't work out (an expression, a random choice).
    pass


def _skip_end_of_lines(d, i):
    while d[i] in (0x01, 0x02):
        i = skip_token(d, i)
    return i


def parse_value(d, i, args):
    # One value: (value, next offset); a missing <param> gives (None, next).
    t = d[i]
    if t in (0x17, 0x18):
        return struct.unpack_from("<i", d, i + 1)[0], i + 5
    if t == 0x1A:
        return struct.unpack_from("<f", d, i + 1)[0], i + 5
    if t in (0x1B, 0x1C):
        n = u32(d, i + 1)
        return d[i + 5:i + 5 + n].rstrip(b"\0").decode("latin-1"), i + 5 + n
    if t == 0x16:
        return ("name", u32(d, i + 1)), i + 5
    if t == 0x1E:
        return ("vector", struct.unpack_from("<3f", d, i + 1)), i + 13
    if t == 0x1F:
        return Unknown(), i + 9
    if t == 0x2D and d[i + 1] == 0x16:
        name = u32(d, i + 2)
        return (args.get(name) if args else None), i + 6
    if t == 0x03:
        dest = {}
        return dest, parse_structure(d, i, dest, args)
    if t == 0x05:
        items = []
        i += 1
        while True:
            i = _skip_end_of_lines(d, i)
            if d[i] == 0x06:
                return items, i + 1
            if d[i] == 0x09:
                i += 1
                continue
            value, i = parse_value(d, i, args)
            items.append(value)
    if t == 0x0A and d[i + 1] in (0x17, 0x1A):
        value, j = parse_value(d, i + 1, args)
        return -value, j
    if t == 0x0E:
        depth, j = 0, i
        while True:
            if d[j] == 0x0E:
                depth += 1
            elif d[j] == 0x0F:
                depth -= 1
                if depth == 0:
                    return Unknown(), j + 1
            j = skip_token(d, j)
    return Unknown(), skip_token(d, i)


def _add(dest, name, value, args):
    # AddParameter: a named value; an unnamed structure (or <param> naming one) merges in; flags are left out.
    if name:
        if value is not None:
            dest[name] = value
    elif isinstance(value, dict):
        dest.update(value)


def _parse_entry(d, i, dest, args, in_structure):
    t = d[i]
    if t == 0x03:
        return parse_structure(d, i, dest, args)
    if t == 0x09:
        return i + 1
    if t == 0x2C:
        if args:
            dest.update(args)
        return i + 1
    if t == 0x16:
        name = u32(d, i + 1)
        j = _skip_end_of_lines(d, i + 5) if in_structure else i + 5
        if d[j] != 0x07:
            return i + 5  # a flag
        j = _skip_end_of_lines(d, j + 1) if in_structure else j + 1
        value, j = parse_value(d, j, args)
        _add(dest, name, value, args)
        return j
    value, j = parse_value(d, i, args)
    _add(dest, 0, value, args)
    return j


def parse_structure(d, i, dest, args):
    # ParseStructure: the { ... } at `i` added into `dest`; the offset past its '}'.
    i += 1
    while True:
        i = _skip_end_of_lines(d, i)
        if d[i] == 0x04:
            return i + 1
        i = _parse_entry(d, i, dest, args, True)


def parse_arguments(d, i, args):
    # ParseArguments: the call's parameters up to the end of the line; (parameters, offset of the end of line).
    dest = {}
    while d[i] not in (0x00, 0x01, 0x02, 0x0F):
        i = _parse_entry(d, i, dest, args, False)
    return dest, i


def walk_script(symbols, script, target, callback, parent, depth):
    # SkateScript::ForEachScriptCall: every call to `target` in `script` and the scripts it calls (statements are
    # followed in order, both ways of every `if`). symbols: {checksum: (data, offset)} of the scripts.
    if depth > MAX_DEPTH or script not in symbols:
        return
    # The game knows each name's symbol type: a C function or member function that isn't `target` (or
    # SetEventHandler) has its statement skipped, any other name is stepped over. This reader doesn't have the
    # function tables, so a name that isn't a script counts as a function where a statement starts (after an end
    # of line, `if`, `not` or an object's ':' - where the game's scripts call them) and is stepped over elsewhere.
    d, i = symbols[script]
    i = skip_statement(d, i)  # (past the first line: the script's default parameters)
    at_start = True
    while True:
        t = d[i]
        starts = at_start
        at_start = t in (0x01, 0x02, 0x39, 0x42, 0x47, 0x48, 0x49)
        if t == 0x16:
            name = u32(d, i + 1)
            i += 5
            if d[i] == 0x07:
                continue
            if name in symbols:
                params, i = parse_arguments(d, i, parent)
                if name == target:
                    callback(params)
                walk_script(symbols, name, target, callback, params, depth + 1)
            elif name == target:
                params, i = parse_arguments(d, i, parent)
                callback(params)
                if name == END_GAP and is_name(params.get(GAP_SCRIPT)):
                    walk_script(symbols, params[GAP_SCRIPT][1], target, callback, None, depth + 1)
            elif name == SET_EVENT_HANDLER:
                params, i = parse_arguments(d, i, parent)
                handler = params.get(SCR)
                if is_name(handler):
                    walk_script(symbols, handler[1], target, callback, None, depth + 1)
            elif starts and d[i] != 0x42:
                i = skip_statement(d, i)  # a C function or member function
                at_start = True
            # (else: an object's name before ':', or a name inside a statement: stepped over)
        elif t == 0x21:
            i = skip_statement(d, skip_token(d, i))
            at_start = True
        elif t == 0x47:
            i = skip_token(d, i)
            if d[i] == 0x39:
                i = skip_token(d, i)
            if d[i] == 0x0E:
                i = skip_statement(d, i)
                at_start = True
        elif t == 0x24:
            return
        else:
            nxt = skip_token(d, i)
            if nxt == i:
                raise ValueError("token 0x%02X at 0x%X" % (t, i))
            i = nxt


def global_value(d, name):
    # The top-level `name = value` of a .qb file (None: not there).
    i = 0
    while i < len(d) and d[i] != 0x00:
        if d[i] == 0x23:
            while d[i] != 0x24:
                i = skip_token(d, i)
            i += 1
            continue
        if d[i] == 0x16 and u32(d, i + 1) == name:
            j = _skip_end_of_lines(d, i + 5)
            if d[j] == 0x07:
                return parse_value(d, _skip_end_of_lines(d, j + 1), None)[0]
        nxt = skip_token(d, i)
        if nxt == i:
            raise ValueError("token 0x%02X at 0x%X" % (d[i], i))
        i = nxt
    return None


def gap_list(symbols, node_array):
    # The gaps CGapChecklist::Build finds: [(name, score, position)] in the order found, each (name, score) once;
    # position: (x, y, z) of the first node whose TriggerScript leads to the gap's EndGap (where the gap ends - the
    # game keeps where the skater started it, once landed, for the gap menu's camera), or None.
    gaps, seen = [], set()
    position = [None]

    def found(params):
        score, text = params.get(SCORE), params.get(TEXT)
        if isinstance(score, int) and score and isinstance(text, str) and text and (text, score) not in seen:
            seen.add((text, score))
            gaps.append((text, score, position[0]))

    for node in node_array:
        if isinstance(node, dict) and is_name(node.get(TRIGGER_SCRIPT)):
            pos = node.get(POS, node.get(POSITION))
            position[0] = pos[1] if isinstance(pos, tuple) and pos[0] == "vector" else None
            walk_script(symbols, node[TRIGGER_SCRIPT][1], END_GAP, found, None, 0)
    return gaps


def classic_goal_calls(symbols, goals_script, add_goal_names):
    # The AddGoal_* calls in the classic block of `goals_script` (the first `if` testing is_classic): [(AddGoal
    # checksum, version or None)]. add_goal_names: the AddGoal_* scripts' checksums.
    is_classic = checksum("is_classic")
    d, start = symbols[goals_script]
    i = skip_statement(d, start)
    calls, depth, inside = [], 0, False
    while d[i] != 0x24:
        t = d[i]
        end = skip_statement(d, i)
        if t == 0x47:
            if inside:
                depth += 1
            elif is_classic.to_bytes(4, "little") in d[i:end]:
                inside, depth = True, 0
        elif t == 0x48 and inside and depth == 0:
            break
        elif t == 0x49 and inside:
            if depth == 0:
                break
            depth -= 1
        elif t == 0x16 and inside and u32(d, i + 1) in add_goal_names:
            params, _ = parse_arguments(d, i + 5, None)
            version = params.get(VERSION)
            calls.append((u32(d, i + 1), version if isinstance(version, int) else None))
        i = end  # (an `if` line is only its condition: its body follows as statements)
    return calls
