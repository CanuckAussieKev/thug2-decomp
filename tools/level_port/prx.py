# (Has a TypeScript twin, prx.ts, which the TypeScript tools use: until this file is removed - once the Python
# tools importing it are ported, docs/typescript.md - change both, and run tools/parity's cases.)
# The games' file archives: THUG2's .prx and THUG1's .pre are the same format (version 0xABCD0003).
#   i32 total size, u32 version, i32 file count, then per file {i32 size, i32 compressed size (0: stored),
#   i16 name size, u16 use count, u32 checksum}, its name, and its data (LZSS when compressed) padded to 4 bytes
#   (src/game/file/pip.cpp NextRawEntry; the LZSS: src/game/util/lzss.cpp).

import struct


def lzss_decode(data, size):
    # A 4096-byte window filled with spaces, writing from 4096 - 18 (as tools/menu_level/make_menu_level.py's).
    window = bytearray(b" " * 4096)
    r = 4096 - 18
    out = bytearray()
    i = 0
    flags = 0
    n = len(data)
    while len(out) < size and i < n:
        flags >>= 1
        if not flags & 0x100:
            flags = data[i] | 0xFF00
            i += 1
        if flags & 1:
            if i >= n:
                break
            c = data[i]
            i += 1
            out.append(c)
            window[r] = c
            r = (r + 1) & 4095
        else:
            if i + 1 >= n:
                break
            position = data[i] | ((data[i + 1] & 0xF0) << 4)
            last = (data[i + 1] & 0x0F) + 2
            i += 2
            for k in range(last + 1):
                c = window[(position + k) & 4095]
                out.append(c)
                window[r] = c
                r = (r + 1) & 4095
    return bytes(out)


def entries(path, wanted=None):
    # (name, data) for each file of the archive (or only those for which wanted(name) is true: the others aren't
    # decompressed).
    data = open(path, "rb").read()
    _, _, count = struct.unpack_from("<iii", data, 0)
    offset = 12
    for _ in range(count):
        size, compressed, name_size, _, _ = struct.unpack_from("<iihHI", data, offset)
        stored = compressed or size
        body = offset + 16 + name_size
        name = data[offset + 16:body].split(b"\0")[0].decode("ascii", "replace")
        if wanted is None or wanted(name):
            raw = data[body:body + stored]
            yield name, (lzss_decode(raw, size) if compressed else raw)
        offset = body + ((stored + 3) & ~3)


def find(path, name):
    # One file of an archive by its name (any case, either slash), or None.
    key = name.lower().replace("/", "\\")
    for _, data in entries(path, lambda n: n.lower().replace("/", "\\") == key):
        return data
    return None
