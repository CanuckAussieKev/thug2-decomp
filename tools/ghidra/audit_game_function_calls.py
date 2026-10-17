"""Check every GameFunction<...>(address) call in src/ against the original function's RET n.

Calls from our code into game functions that are not rewritten yet are invisible to thug2_check (it only
checks REPLACE). A wrong calling convention there (a __thiscall method called as __cdecl, a wrong argument
count) corrupts the stack and only shows in the game (the Debug build reports it as a run-time check
failure). Run after adding GameFunction calls:

    GHIDRA_INSTALL_DIR=C:/ghidra uv run --no-project --python 3.13 --with pyghidra
        python tools/ghidra/audit_game_function_calls.py C:/Games/THUG2-ghidra-snapshot

MISMATCH lines must be fixed; "?" means the original has no RET (a tail jump): check those by hand.
"""
import sys, re, pathlib, pyghidra
pyghidra.start()
src = pathlib.Path(__file__).resolve().parents[2] / "src"  # this checkout's source (works in a worktree)
pat = re.compile(r"GameFunction<\s*([^>]*?)\(\s*(__\w+)\s*\*\s*\)\s*\(([^)]*)\)\s*>\s*\(\s*(0x[0-9A-Fa-f]+)\s*\)")
uses = []
for f in src.rglob("*.*"):
    if f.suffix not in (".cpp", ".h"): continue
    text = f.read_text(encoding="utf-8")
    for m in pat.finditer(text):
        ret, conv, args, addr = m.group(1).strip(), m.group(2), m.group(3).strip(), int(m.group(4), 16)
        params = [a.strip() for a in args.split(",")] if args else []
        line = text[:m.start()].count("\n") + 1
        uses.append((f.name, line, conv, params, addr))
def size(p):
    p = p.replace("const", "").strip()
    if ("VectorValue" in p or p in ("Mth::Vector", "Vector")) and "*" not in p and "&" not in p: return 16
    if p in ("double", "int64_t", "uint64_t", "GridDims", "Ed::GridDims"): return 8  # (GridDims: 6 bytes, 2 slots)
    return 4
with pyghidra.open_project(sys.argv[1], "THUG2PM_ghidra_mcp") as project, pyghidra.program_context(project, "/THUG2PM.exe") as program:
    fm = program.getFunctionManager(); sp = program.getAddressFactory().getDefaultAddressSpace(); listing = program.getListing()
    for name, line, conv, params, addr in uses:
        fn = fm.getFunctionAt(sp.getAddress(addr))
        pops = set()
        if fn:
            for ins in listing.getInstructions(fn.getBody(), True):
                if ins.getMnemonicString() == "RET":
                    pops.add(ins.getScalar(0).getValue() if ins.getNumOperands() else 0)
        stack = params[1:] if conv == "__thiscall" else params
        expect = sum(size(p) for p in stack) if conv in ("__thiscall", "__stdcall") else 0
        status = "ok" if pops == {expect} else ("?" if not pops else "MISMATCH")
        if status != "ok":
            print(f"{status} {name}:{line} 0x{addr:08X} {conv}({', '.join(params)}) we pop {expect}, original RET {sorted(pops)}")
    print(f"{len(uses)} GameFunction uses checked")
