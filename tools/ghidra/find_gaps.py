"""Find game functions that still have no source in this repo, near what has been rewritten.

The game is to be built from this source, so nothing may be skipped (see CLAUDE.md). This lists:

  * gaps: un-replaced functions inside the address ranges we've worked through (between rewritten
    functions no more than --span bytes apart);
  * thunks: functions that are just a jump to a rewritten function (aliases needing a REPLACE_SOURCE_ONLY).

Only src/game counts (src/hook's registrations are test-runner hooks, not source). Known exceptions:
0x00401020 is the compiler's `vector constructor iterator' (docs/library_ranges.md).

    GHIDRA_INSTALL_DIR=C:/ghidra uv run --no-project --python 3.13 --with pyghidra \
        python tools/ghidra/find_gaps.py C:/Games/THUG2-ghidra-snapshot [--span 0x800]
"""
import bisect
import pathlib
import re
import sys

import pyghidra

pyghidra.start()

REPO = pathlib.Path(__file__).resolve().parents[2]
snapshot = sys.argv[1]
span = int(sys.argv[sys.argv.index("--span") + 1], 16) if "--span" in sys.argv else 0x800

replaced = set()
for path in (REPO / "src" / "game").rglob("*.cpp"):
    for match in re.finditer(r"REPLACE\w*\(0x([0-9A-Fa-f]+)", path.read_text(encoding="utf-8")):
        replaced.add(int(match.group(1), 16))
# Libraries we link instead of decompiling (docs/library_ranges.md).
libraries = [(0x5BE670, 0x5EEF20), (0x5F8160, 0x622E80), (0x622F80, 0x640000)]
COMPILER_GENERATED = {0x401020}
in_library = lambda a: any(lo <= a < hi for lo, hi in libraries)

with pyghidra.open_project(snapshot, "THUG2PM_ghidra_mcp") as project, \
        pyghidra.program_context(project, "/THUG2PM.exe") as program:
    functions = program.getFunctionManager()
    listing = program.getListing()
    every = [(f.getEntryPoint().getOffset(), f) for f in functions.getFunctions(True)]
    every = [(a, f) for a, f in every if 0x401000 <= a < 0x640000]
    done = sorted(a for a, _ in every if a in replaced)

    print("== gaps (un-replaced functions between rewritten ones)")
    gap_count = 0
    for a, f in every:
        if a in replaced or in_library(a) or a in COMPILER_GENERATED:
            continue
        i = bisect.bisect_left(done, a)
        before = done[i - 1] if i > 0 else None
        after = done[i] if i < len(done) else None
        if before is not None and after is not None and after - before <= span:
            size = f.getBody().getNumAddresses()
            print(f"  {a:08X} {size:5d} {f.getName()}   (between {before:08X} and {after:08X})")
            gap_count += 1
    print(f"  {gap_count} gaps")

    print("== thunks to rewritten functions")
    thunk_count = 0
    for a, f in every:
        if a in replaced:
            continue
        instructions = list(listing.getInstructions(f.getBody(), True))
        if len(instructions) == 1 and instructions[0].getMnemonicString() == "JMP":
            refs = instructions[0].getReferencesFrom()
            target = refs[0].getToAddress().getOffset() if refs else None
            if target in replaced:
                print(f"  {a:08X} -> {target:08X} {f.getName()}")
                thunk_count += 1
    print(f"  {thunk_count} thunks")
