"""List the game's do-nothing functions: at most two instructions ending in RET (a bare return, or setting the
return value first). The linker folded identical functions into one, so each address stands for many functions
of the source (compiled-out debug output, empty virtual methods, script commands that are stubs on PC). Each
needs a REPLACE_SOURCE_ONLY - it's too short to hook. Marks those already registered under src/game.

    GHIDRA_INSTALL_DIR=C:/ghidra uv run --no-project --python 3.13 --with pyghidra \
        python tools/ghidra/find_stubs.py C:/Games/THUG2-ghidra-snapshot
"""
import pathlib
import re
import sys

import pyghidra

pyghidra.start()

REPO = pathlib.Path(__file__).resolve().parents[2]
replaced = set()
for path in (REPO / "src" / "game").rglob("*.cpp"):
    for match in re.finditer(r"REPLACE\w*\(0x([0-9A-Fa-f]+)", path.read_text(encoding="utf-8")):
        replaced.add(int(match.group(1), 16))
# Game code only (docs/library_ranges.md).
game = [(0x401000, 0x5BEAAE), (0x5EEF20, 0x5F8130)]

with pyghidra.open_project(sys.argv[1], "THUG2PM_ghidra_mcp") as project, \
        pyghidra.program_context(project, "/THUG2PM.exe") as program:
    listing = program.getListing()
    references = program.getReferenceManager()
    found = 0
    for f in program.getFunctionManager().getFunctions(True):
        a = f.getEntryPoint().getOffset()
        if not any(lo <= a < hi for lo, hi in game):
            continue
        instructions = []
        for instruction in listing.getInstructions(f.getEntryPoint(), True):
            instructions.append(instruction)
            if instruction.getMnemonicString() == "RET" or len(instructions) > 2:
                break
        if len(instructions) > 2 or instructions[-1].getMnemonicString() != "RET":
            continue
        callers = sum(1 for _ in references.getReferencesTo(f.getEntryPoint()))
        text = "; ".join(str(i) for i in instructions)
        mark = "done" if a in replaced else "TODO"
        print(f"{mark} {a:08X} {callers:5d} refs  {text}")
        found += 1
    print(f"{found} do-nothing functions")
