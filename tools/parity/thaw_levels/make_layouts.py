# NOT ORIGINAL - made-up level editor layouts of the THAW levels under their old names (thaw_<xx>), for the
# migrate_layouts parity cases (cases.ts): <folder>\game\thug2-decomp-layouts\<old scene>.txt. Goes with the Python
# twins.
#
#   python -I tools/parity/thaw_levels/make_layouts.py <folder> [bad]

import os
import sys
import zlib


def checksum(name):
    return zlib.crc32(name.lower().encode("ascii")) ^ 0xFFFFFFFF


def layout(scene_name, crlf=False):
    lines = [
        "# a layout made up for the parity cases",
        "scene %08X  # %s" % (checksum(scene_name), scene_name),
        "",
        "sector 1234ABCD 0 0 0 0",
        "sector 0BADF00D 1 120.5 -3.25 4000.0625  # turned once",
        "rail DEADBEEF 2 -12660.4375 864.125 61514.9 extra words",
        "clone CAFEF00D 3 1e-07 0.1 100.0625 7",
        "clone 00000001 5 99999.99 -99999.99 0.000123",
        "\tobject 17 1.5 2.5 3.5 row1 row2",
        "copy MyCopy SourceThing -100 200 -300 r",
        "   # only a comment",
        "unknown record 1 2 3  #  kept as it is",
        "object 3 0 0 0#tight comment",
    ]
    end = "\r\n" if crlf else "\n"
    return end.join(lines) + end


def main():
    folder = os.path.join(sys.argv[1], "game", "thug2-decomp-layouts")
    os.makedirs(folder, exist_ok=True)
    if len(sys.argv) > 2 and sys.argv[2] == "bad":
        # (a scene line that isn't the file's: migrate_layouts raises)
        open(os.path.join(folder, "%08X.txt" % checksum("thaw_ma")), "w", encoding="utf-8", newline="").write(
            layout("thaw_ch"))
        return
    for k, old in enumerate(("thaw_ma", "thaw_oi", "thaw_sv2", "thaw_ch", "thaw_sv", "thaw_el", "thaw_dn")):
        open(os.path.join(folder, "%08X.txt" % checksum(old)), "w", encoding="utf-8", newline="").write(
            layout(old, crlf=k % 2 == 1))
    # Minneapolis already has a layout under its new name: left as it is
    open(os.path.join(folder, "%08X.txt" % checksum("z_dn")), "w", encoding="utf-8", newline="").write(
        layout("z_dn"))


main()
