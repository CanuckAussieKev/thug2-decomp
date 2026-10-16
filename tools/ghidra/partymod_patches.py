"""List PartyMod's patches by the game function they land in: what it would take to write PartyMod's behaviour
into the game's source (and drop src/partymod's run-time patching).

    GHIDRA_INSTALL_DIR=C:/ghidra uv run --no-project --python 3.13 --with pyghidra \
        python tools/ghidra/partymod_patches.py C:/Games/THUG2-ghidra-snapshot

Each patch: its kind (Call / Jmp = redirected into PartyMod's code; Nop / Byte / DWord / Float / Inst = an
edit in place; ThisToCdecl = a thiscall redirected), address, source file and line. Grouped by containing
function, with that function's size and whether it's rewritten already.
"""
import collections
import glob
import pathlib
import re
import sys

import pyghidra

pyghidra.start()

REPO = pathlib.Path(__file__).resolve().parents[2]
sources = {p: open(p, encoding="utf-8", errors="replace").read() for p in glob.glob(str(REPO / "src/partymod/**/*.c"), recursive=True)}
# (commented-out patches don't count; blanked so line numbers stay right)
sources = {p: re.sub(r"//[^\n]*", "", re.sub(r"/\*.*?\*/", lambda m: re.sub(r"[^\n]", " ", m.group(0)), t, flags=re.S))
           for p, t in sources.items()}
alltext = "".join(sources.values())
names = {m.group(1): int(m.group(2), 16)
         for m in re.finditer(r"(addr_\w+)\s*=\s*\(?(?:void\s*\*\))?\s*\(?(0x[0-9a-fA-F]+)", alltext)}
patches = []
for path, text in sources.items():
    for m in re.finditer(r"\bpatch(\w*)\(\s*\(?(?:void\s*\*\)\s*)?\(?(0x[0-9a-fA-F]+|addr_\w+)\s*(?:([+-])\s*(0x[0-9a-fA-F]+|\d+))?([^;]*)\);", text):
        kind, target, sign, offset, rest = m.group(1), m.group(2), m.group(3), m.group(4), m.group(5)
        value = int(target, 16) if target.startswith("0x") else names.get(target)
        if value is None:
            continue
        value += (int(offset, 0) if offset else 0) * (-1 if sign == "-" else 1)
        line = text.count("\n", 0, m.start()) + 1
        to = re.search(r"&?(\w+)\s*$", rest.strip().rstrip(")")) if kind in ("Call", "Jmp", "ThisToCdecl") else None
        patches.append((value, kind, pathlib.Path(path).name, line, to.group(1) if to else ""))

replaced = set()
for path in (REPO / "src" / "game").rglob("*.cpp"):
    for m in re.finditer(r"REPLACE\w*\(0x([0-9A-Fa-f]+)", path.read_text(encoding="utf-8")):
        replaced.add(int(m.group(1), 16))

with pyghidra.open_project(sys.argv[1], "THUG2PM_ghidra_mcp") as project, \
        pyghidra.program_context(project, "/THUG2PM.exe") as program:
    functions = program.getFunctionManager()
    addresses = program.getAddressFactory().getDefaultAddressSpace()
    groups = collections.OrderedDict()
    data = []
    for value, kind, file, line, to in sorted(patches):
        f = functions.getFunctionContaining(addresses.getAddress(value))
        if f is None:
            data.append((value, kind, file, line))
            continue
        entry = f.getEntryPoint().getOffset()
        groups.setdefault(entry, (f, []))[1].append((value, kind, file, line, to))
    total = 0
    whole = 0
    for entry, (f, items) in groups.items():
        size = f.getBody().getNumAddresses()
        total += size
        kinds = ",".join(sorted({k for _, k, _, _, _ in items}))
        redirected = any(k in ("Jmp",) and v == entry for v, k, _, _, _ in items)
        whole += redirected
        done = "REWRITTEN" if entry in replaced else ""
        print(f"{entry:08X} {size:6d} {f.getName()[:34]:34} {len(items):2d} patch(es) [{kinds}]"
              f"{' replaced whole' if redirected else ''} {done}")
        for v, k, file, line, to in items:
            print(f"      {v:08X} {k:12} {file}:{line} {to}")
    print(f"== {len(patches)} patches in {len(groups)} functions ({total} bytes), {whole} replaced whole by a jump "
          f"at their start; {len(data)} outside any function (data):")
    for v, k, file, line in data:
        print(f"      {v:08X} {k:12} {file}:{line}")
