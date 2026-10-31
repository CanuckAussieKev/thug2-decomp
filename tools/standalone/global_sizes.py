"""NOT ORIGINAL - every GAME_GLOBAL's size as the standalone build lays it out (its PDB), for tools/global_gaps.ts:
"<address> <size> <name>" per line (size -1: not found in the PDB; when several symbols have the name, the smallest).
Reads a copy of the exe and its PDB (so a build can still write them).

    python -I tools/standalone/global_sizes.py <thug2_standalone.exe> <globals.txt: "<address> <name>" lines>
"""
import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from addr2sym import Resolver  # noqa: E402


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    exe = sys.argv[1]
    folder = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "out", "global_sizes")
    os.makedirs(folder, exist_ok=True)
    copy = os.path.join(folder, os.path.basename(exe))
    shutil.copy(exe, copy)
    shutil.copy(os.path.splitext(exe)[0] + ".pdb", os.path.splitext(copy)[0] + ".pdb")
    r = Resolver(copy)
    if not r.ok:
        raise SystemExit(f"couldn't load {exe}")
    sizes = {}
    for _rva, size, name in r.all_symbols("*"):
        short = name.rsplit("::", 1)[-1]
        if size > 0:
            sizes.setdefault(short, []).append(size)
    for line in open(sys.argv[2], encoding="utf-8"):
        parts = line.split()
        if len(parts) != 2:
            continue
        address, name = parts
        found = sizes.get(name)
        print(f"{address} {min(found) if found else -1} {name}")


if __name__ == "__main__":
    main()
