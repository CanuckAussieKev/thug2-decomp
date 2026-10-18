# Texture dictionaries (THUG2 levels\<x>\<x>.tex.xbx; THPS4 PC <lvl>tex.dat has the same layout and version):
#   u32 version (1), u32 count, then per texture:
#   u32 checksum, width, height, mip levels, texel depth, palette depth, DXT version (1, 2 = DXT1 with alpha, 5),
#   palette size, the palette, then per level {u32 size, data}.

from binio import Reader, Writer, FormatError


def read_tex(data, name="tex"):
    r = Reader(data, name=name)
    version, count = r.read("2I")
    textures = []
    for _ in range(count):
        t = {}
        (t["checksum"], t["width"], t["height"], levels, t["texel_depth"], t["palette_depth"], t["dxt"],
         palette_size) = r.read("8I")
        t["palette"] = r.bytes(palette_size)
        t["levels"] = [r.bytes(r.u32()) for _ in range(levels)]
        textures.append(t)
    r.expect_end()
    return {"version": version, "textures": textures}


def write_tex(tex):
    w = Writer()
    w.write("2I", tex["version"], len(tex["textures"]))
    for t in tex["textures"]:
        w.write("8I", t["checksum"], t["width"], t["height"], len(t["levels"]), t["texel_depth"], t["palette_depth"],
                t["dxt"], len(t["palette"]))
        w.raw(t["palette"])
        for level in t["levels"]:
            w.u32(len(level))
            w.raw(level)
    return w.getvalue()


def unswizzle_order(w, h):
    # THPS4's 8-bit textures are stored swizzled, as on the Xbox: a texel's index has the bits of x and y
    # interleaved (x first) while both have bits left, then the rest of the longer one. Returns, for each texel in
    # row order, its index in the stored data.
    order = []
    for y in range(h):
        for x in range(w):
            index, shift, bit = 0, 0, 0
            while (1 << bit) < w or (1 << bit) < h:
                if (1 << bit) < w:
                    index |= ((x >> bit) & 1) << shift
                    shift += 1
                if (1 << bit) < h:
                    index |= ((y >> bit) & 1) << shift
                    shift += 1
                bit += 1
            order.append(index)
    return order


def depalettise(tex):
    # THPS4's 8-bit paletted textures (a few skies, one London texture: 256 x BGRA palette, indices row by row)
    # as 32-bit A8R8G8B8 - the texture loader (0x004CAE90) makes paletted ones 32-bit textures but its palette path
    # isn't the plain one the shipped THUG2 files use; 32-bit (texel depth 32, DXT 0) is.
    changed = 0
    for t in tex["textures"]:
        if not t["palette"]:
            continue
        if t["texel_depth"] != 8 or len(t["palette"]) != 1024:
            raise FormatError("texture %08X: %d-bit with a %d-byte palette" % (t["checksum"], t["texel_depth"],
                                                                                len(t["palette"])))
        colors = [t["palette"][i:i + 4] for i in range(0, 1024, 4)]
        levels = []
        for n, level in enumerate(t["levels"]):
            w, h = max(1, t["width"] >> n), max(1, t["height"] >> n)
            if len(level) != w * h:
                raise FormatError("texture %08X level %d: %d bytes for %dx%d" % (t["checksum"], n, len(level), w, h))
            order = unswizzle_order(w, h)
            levels.append(b"".join(colors[level[i]] for i in order))
        t["levels"] = levels
        t["palette"] = b""
        t["texel_depth"] = 32
        t["palette_depth"] = 32  # (as every texture of the shipped files)
        t["dxt"] = 0
        changed += 1
    return changed


def check_loadable(tex, name):
    # What THUG2's loader takes plainly (0x004CAE90): DXT1/2/5, or uncompressed 32-bit; no palettes.
    for t in tex["textures"]:
        if t["palette"] or t["dxt"] not in (0, 1, 2, 5) or (t["dxt"] == 0 and t["texel_depth"] != 32):
            raise FormatError("%s: texture %08X is %d-bit / palette %d / DXT %d" % (
                name, t["checksum"], t["texel_depth"], len(t["palette"]), t["dxt"]))
