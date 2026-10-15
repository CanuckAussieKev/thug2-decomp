# Read-only: report function count and a few known names in the THUG2PM.exe program.
import pyghidra
pyghidra.start()
import datetime
with pyghidra.open_project(r"C:\Games\THUG2-ghidra", "THUG2PM_ghidra_mcp") as project:
    df = project.getProjectData().getFile("/THUG2PM.exe")
    print("last modified:", datetime.datetime.fromtimestamp(df.getLastModifiedTime()/1000))
    with pyghidra.program_context(project, "/THUG2PM.exe") as prog:
        fm = prog.getFunctionManager()
        print("functions:", fm.getFunctionCount())
        af = prog.getAddressFactory().getDefaultAddressSpace()
        for a in (0x4e2680, 0x5b7c00, 0x5b9760):
            f = fm.getFunctionAt(af.getAddress(a))
            print(hex(a), f.getName() if f else None)
        print("open count/changed:", prog.isChanged())
