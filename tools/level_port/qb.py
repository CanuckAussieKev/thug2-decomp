# THPS4 / THUG1 .qb files: read the global data (`name = value` at the top level: node arrays, level structures) and
# the checksum names, and write values as NeverScript text (tools/neverscript compiles that to THUG2 .qb).
#
# The token stream is THUG2's (src/game/script/qb.h) in both. THUG1's is THUG2's exactly; THPS4's differs in the
# random tokens: its 0x2F / 0x37 / 0x40 / 0x41 are {u32 count, count x u32 jump offset} - THUG1 / THUG2 have a u16
# weight per entry before the offsets - which is why NeverScript's THUG2 decompiler stops in THPS4 files (QbFile's
# `weights`). Scripts (0x23 ... 0x24) are split into statements, not decoded further.

import struct
import zlib

from binio import FormatError


def checksum(name):
    return (zlib.crc32(name.lower().encode("ascii")) ^ 0xFFFFFFFF) & 0xFFFFFFFF


class Name:
    # A checksum value (a name in the script).
    __slots__ = ("value",)

    def __init__(self, value):
        self.value = value

    def __eq__(self, other):
        return isinstance(other, Name) and other.value == self.value

    def __hash__(self):
        return self.value

    def __repr__(self):
        return "Name(%08X)" % self.value


class Local:
    # A local string ('text').
    __slots__ = ("text",)

    def __init__(self, text):
        self.text = text


class Arg:
    # A parameter reference (<name>).
    __slots__ = ("value",)

    def __init__(self, value):
        self.value = value


class Expr:
    __slots__ = ("left", "op", "right")

    def __init__(self, left, op, right):
        self.left, self.op, self.right = left, op, right


class Struct:
    # A structure: entries (key Name or None for a flag / unnamed value, value).
    def __init__(self, entries=None):
        self.entries = entries or []

    def get(self, key, default=None):
        k = checksum(key) if isinstance(key, str) else key
        for name, value in self.entries:
            if name is not None and name.value == k:
                return value
        return default

    def has_flag(self, flag):
        k = checksum(flag)
        return any(name is None and isinstance(value, Name) and value.value == k for name, value in self.entries)

    def remove(self, key):
        k = checksum(key)
        self.entries = [(n, v) for n, v in self.entries if not (n is not None and n.value == k)]

    def remove_flag(self, flag):
        k = checksum(flag)
        self.entries = [(n, v) for n, v in self.entries
                        if not (n is None and isinstance(v, Name) and v.value == k)]

    def set(self, key, value):
        k = checksum(key)
        for i, (name, _) in enumerate(self.entries):
            if name is not None and name.value == k:
                self.entries[i] = (name, value)
                return
        self.entries.append((Name(k), value))


# Token sizes after the token byte, for skipping (scripts).
FIXED = {0x02: 4, 0x16: 4, 0x17: 4, 0x18: 4, 0x19: 4, 0x1A: 4, 0x1E: 12, 0x1F: 8, 0x2E: 4, 0x43: 4, 0x44: 4,
         0x45: 4, 0x46: 4, 0x47: 2, 0x48: 2, 0x49: 2}


def tokens(data, weights=False):
    # (offset, token, payload) for every token; `weights`: the random tokens have the u16 weights (THUG1 / THUG2).
    i, n = 0, len(data)
    while i < n:
        t = data[i]
        start = i
        i += 1
        if t in FIXED:
            payload = data[i:i + FIXED[t]]
            i += FIXED[t]
        elif t in (0x1B, 0x1C):
            length = struct.unpack_from("<I", data, i)[0]
            payload = data[i + 4:i + 4 + length]
            i += 4 + length
        elif t == 0x2B:
            end = data.index(b"\0", i + 4)
            payload = data[i:end + 1]
            i = end + 1
        elif t in (0x2F, 0x37, 0x40, 0x41):
            count = struct.unpack_from("<I", data, i)[0]
            size = 4 + (6 if weights else 4) * count
            payload = data[i:i + size]
            i += size
        else:
            payload = b""
        yield start, t, payload
        if t == 0x00:
            break


class QbFile:
    def __init__(self, data, name="qb", weights=False):
        self.name = name
        self.names = {}       # checksum -> text, from the file's 0x2B entries
        self.globals = []     # (Name, value) in file order
        toks = list(tokens(data, weights))
        for _, t, payload in toks:
            if t == 0x2B:
                value = struct.unpack_from("<I", payload, 0)[0]
                self.names[value] = payload[4:-1].decode("latin-1")
        # Scripts: name -> statements, each the list of its (token, payload) - not decoded further.
        self.scripts = {}
        i = 0
        while i < len(toks):
            if toks[i][1] == 0x23 and i + 1 < len(toks) and toks[i + 1][1] == 0x16:
                name = struct.unpack("<I", toks[i + 1][2])[0]
                statements, line = [], []
                i += 2
                while i < len(toks) and toks[i][1] != 0x24:
                    if toks[i][1] in (0x01, 0x02):
                        if line:
                            statements.append(line)
                        line = []
                    else:
                        line.append((toks[i][1], toks[i][2]))
                    i += 1
                if line:
                    statements.append(line)
                self.scripts[name] = statements
            i += 1
        self._toks = [x for x in toks if x[1] not in (0x01, 0x02, 0x2B)]
        self._parse()

    def calls(self, script):
        # The names each statement of `script` starts with (the commands / scripts it calls), and the whole statement.
        out = []
        for line in self.scripts.get(script, []):
            if line and line[0][0] == 0x16:
                out.append((struct.unpack("<I", line[0][1])[0], line))
        return out

    def _parse(self):
        toks = self._toks
        i = 0
        while i < len(toks):
            _, t, payload = toks[i]
            if t == 0x23:  # script ... endscript
                while toks[i][1] != 0x24:
                    i += 1
                i += 1
            elif t == 0x16 and i + 1 < len(toks) and toks[i + 1][1] == 0x07:
                key = Name(struct.unpack("<I", payload)[0])
                value, i = self._value(i + 2)
                self.globals.append((key, value))
            elif t == 0x00:
                break
            else:
                raise FormatError("%s: token 0x%02X at 0x%X at the top level" % (self.name, t, toks[i][0]))

    def _value(self, i):
        value, i = self._single(i)
        # a + b / a - b (seen in a park editor shell's node array): kept as an expression
        while self._toks[i][1] in (0x0A, 0x0B) and self._toks[i + 1][1] not in (0x04, 0x06):
            op = "-" if self._toks[i][1] == 0x0A else "+"
            right, i = self._single(i + 1)
            value = Expr(value, op, right)
        return value, i

    def _single(self, i):
        toks = self._toks
        _, t, payload = toks[i]
        if t == 0x16:
            return Name(struct.unpack("<I", payload)[0]), i + 1
        if t in (0x17, 0x18):
            return struct.unpack("<i", payload)[0], i + 1
        if t == 0x1A:
            return struct.unpack("<f", payload)[0], i + 1
        if t == 0x1E:
            return ("vector", struct.unpack("<3f", payload)), i + 1
        if t == 0x1F:
            return ("pair", struct.unpack("<2f", payload)), i + 1
        if t == 0x1B:
            return payload.rstrip(b"\0").decode("latin-1"), i + 1
        if t == 0x1C:
            return Local(payload.rstrip(b"\0").decode("latin-1")), i + 1
        if t == 0x0A and toks[i + 1][1] in (0x17, 0x18, 0x1A):  # a negative number
            value, i = self._single(i + 1)
            return -value, i
        if t == 0x2D and toks[i + 1][1] == 0x16:  # <name>: a parameter
            return Arg(struct.unpack("<I", toks[i + 1][2])[0]), i + 2
        if t == 0x03:
            entries = []
            i += 1
            while toks[i][1] != 0x04:
                if toks[i][1] == 0x09:
                    i += 1
                    continue
                if toks[i][1] == 0x16 and toks[i + 1][1] == 0x07:
                    key = Name(struct.unpack("<I", toks[i][2])[0])
                    value, i = self._value(i + 2)
                    entries.append((key, value))
                else:
                    value, i = self._value(i)
                    entries.append((None, value))
            return Struct(entries), i + 1
        if t == 0x05:
            items = []
            i += 1
            while toks[i][1] != 0x06:
                if toks[i][1] == 0x09:
                    i += 1
                    continue
                value, i = self._value(i)
                items.append(value)
            return items, i + 1
        if t == 0x0E:  # a parenthesised expression: kept only when it is a single value
            value, j = self._value(i + 1)
            if toks[j][1] == 0x0F:
                return value, j + 1
        raise FormatError("%s: value token 0x%02X at 0x%X" % (self.name, t, toks[i][0]))

    def get(self, name):
        k = checksum(name)
        for key, value in self.globals:
            if key.value == k:
                return value
        return None

    def name_of(self, value):
        return self.names.get(value)


# --- NeverScript text -------------------------------------------------------------------------------------------

def fmt_float(f):
    text = repr(float(struct.unpack("<f", struct.pack("<f", f))[0]))
    if "e" in text or "inf" in text or "nan" in text:
        text = "%.9f" % f
    if "." not in text:
        text += ".0"
    return text


def to_ns(value, names, indent=0):
    pad = "    " * indent
    if isinstance(value, Name):
        return name_text(value.value, names)
    if isinstance(value, Arg):
        return "<%s>" % name_text(value.value, names)
    if isinstance(value, Expr):
        return "(%s %s %s)" % (to_ns(value.left, names), value.op, to_ns(value.right, names))
    if isinstance(value, bool):
        return str(int(value))
    if isinstance(value, int):
        return str(value)
    if isinstance(value, float):
        return fmt_float(value)
    if isinstance(value, str):
        return '"%s"' % value.replace("\\", "\\\\").replace('"', '\\"')
    if isinstance(value, Local):
        return "'%s'" % value.text.replace("\\", "\\\\").replace("'", "\\'")
    if isinstance(value, tuple):
        return "(%s)" % ", ".join(fmt_float(v) for v in value[1])
    if isinstance(value, list):
        if not value:
            return "[]"
        if all(not isinstance(v, (Struct, list)) for v in value):
            return "[%s]" % ", ".join(to_ns(v, names) for v in value)
        inner = "\n".join(pad + "    " + to_ns(v, names, indent + 1) for v in value)
        return "[\n%s\n%s]" % (inner, pad)
    if isinstance(value, Struct):
        lines = []
        for key, v in value.entries:
            text = to_ns(v, names, indent + 1)
            lines.append(pad + "    " + (text if key is None else "%s=%s" % (name_text(key.value, names), text)))
        return "{\n%s\n%s}" % ("\n".join(lines), pad)
    raise TypeError("no NeverScript form for %r" % (value,))


def name_text(value, names):
    text = names.get(value)
    if text and all(c.isalnum() or c == "_" for c in text) and not text[0].isdigit() and \
            checksum(text) == value and text.lower() not in RESERVED:
        return text
    return "#%08X" % value


# Words NeverScript reads as keywords, not names.
RESERVED = {"script", "endscript", "if", "else", "elseif", "endif", "while", "loop", "break", "return", "switch",
            "case", "default", "begin", "repeat", "random", "not", "and", "or", "true", "false", "endswitch",
            "useheap", "random2", "randomrange", "randomrange2", "randompermute", "randomnorepeat"}
