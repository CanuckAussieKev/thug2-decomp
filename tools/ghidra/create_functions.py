# Disassemble and create functions at the addresses listed in a file (one hex address per line).
# Run from the repo root while the Ghidra MCP server is NOT running (the project is locked otherwise):
#   set GHIDRA_INSTALL_DIR=C:\ghidra
#   uv run --no-project --python 3.13 --with pyghidra python tools/ghidra/create_functions.py data/derived/missed_functions.txt
import sys
import pyghidra

pyghidra.start()
from ghidra.program.flatapi import FlatProgramAPI
from ghidra.util.task import TaskMonitor

addrs = [int(line, 16) for line in open(sys.argv[1], encoding="utf-8") if line.strip()]

with pyghidra.open_project(r"C:\Games\THUG2-ghidra", "THUG2PM_ghidra_mcp") as project:
    with pyghidra.program_context(project, "/THUG2PM.exe") as prog:
        api = FlatProgramAPI(prog)
        before = prog.getFunctionManager().getFunctionCount()
        tx = prog.startTransaction("Create functions missed by auto-analysis")
        ok, created, failed = False, 0, []
        try:
            for v in addrs:
                a = api.toAddr(v)
                if api.getFunctionAt(a) is not None:
                    continue
                api.disassemble(a)
                if api.createFunction(a, None) is not None:
                    created += 1
                else:
                    failed.append(v)
            ok = True
        finally:
            prog.endTransaction(tx, ok)
        if ok:
            prog.save("Create functions missed by auto-analysis", TaskMonitor.DUMMY)
        after = prog.getFunctionManager().getFunctionCount()
        print(f"created {created}, failed {len(failed)}, functions {before} -> {after}, saved {ok}")
        if failed:
            print("failed:", " ".join(f"{v:#x}" for v in failed[:20]))
