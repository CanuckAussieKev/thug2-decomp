# Read-only: report function count and a few known names in the reference build program.
import pyghidra
pyghidra.start()
import datetime
from local_config import GHIDRA_PROGRAM, GHIDRA_PROJECT
with pyghidra.open_project(r"C:\Games\THUG2-ghidra", GHIDRA_PROJECT) as project:
    df = project.getProjectData().getFile(GHIDRA_PROGRAM)
    print("last modified:", datetime.datetime.fromtimestamp(df.getLastModifiedTime()/1000))
    with pyghidra.program_context(project, GHIDRA_PROGRAM) as prog:
        fm = prog.getFunctionManager()
        print("functions:", fm.getFunctionCount())
        af = prog.getAddressFactory().getDefaultAddressSpace()
        for a in (0x4e2680, 0x5b7c00, 0x5b9760):
            f = fm.getFunctionAt(af.getAddress(a))
            print(hex(a), f.getName() if f else None)
        print("open count/changed:", prog.isChanged())
