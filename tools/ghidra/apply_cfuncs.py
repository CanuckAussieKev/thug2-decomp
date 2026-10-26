# Bulk-rename THUG2 script CFunctions in the Ghidra project from cfuncs.csv.
# Run from the repo root while the Ghidra MCP server is NOT running (the project is locked otherwise):
#   set GHIDRA_INSTALL_DIR=C:\ghidra
#   uv run --no-project --python 3.13 --with pyghidra python tools/ghidra/apply_cfuncs.py data/derived/cfuncs.csv
import csv, sys, collections
import pyghidra

pyghidra.start()
from ghidra.program.flatapi import FlatProgramAPI
from ghidra.program.model.symbol import SourceType
from ghidra.util.task import TaskMonitor
from local_config import GHIDRA_PROGRAM, GHIDRA_PROJECT

PROJECT_DIR = r"C:\Games\THUG2-ghidra"
PROJECT_NAME = GHIDRA_PROJECT
PROGRAM_PATH = GHIDRA_PROGRAM

rows = list(csv.DictReader(open(sys.argv[1])))
names = collections.defaultdict(list)
for r in rows:
    names[int(r["address"], 16)].append(r["script_name"])

with pyghidra.open_project(PROJECT_DIR, PROJECT_NAME) as project:
    with pyghidra.program_context(project, PROGRAM_PATH) as prog:
        api = FlatProgramAPI(prog)
        tx = prog.startTransaction("Rename script CFunctions")
        renamed = created = skipped = 0
        ok = False
        try:
            for addr, script_names in sorted(names.items()):
                a = api.toAddr(addr)
                f = api.getFunctionAt(a)
                if f is None:
                    f = api.createFunction(a, None)
                    created += 1
                if f is None or not f.getName().startswith("FUN_"):
                    skipped += 1  # keep names already set by hand
                    continue
                f.setName("CFunc_" + script_names[0], SourceType.USER_DEFINED)
                if len(script_names) > 1:
                    f.setComment("Script CFunction for: " + ", ".join(script_names))
                renamed += 1
            ok = True
        finally:
            prog.endTransaction(tx, ok)
        if ok:
            prog.save("Rename script CFunctions", TaskMonitor.DUMMY)
        print(f"renamed {renamed}, created {created}, skipped {skipped}, saved {ok}")
