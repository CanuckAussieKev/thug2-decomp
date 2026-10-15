# List programs in the Ghidra project and their function counts.
import sys
import pyghidra
pyghidra.start()
with pyghidra.open_project(r"C:\Games\THUG2-ghidra", "THUG2PM_ghidra_mcp") as project:
    def walk(folder):
        for f in folder.getFiles():
            print(f.getPathname(), f.getContentType(), "modified:", f.getLastModifiedTime())
        for sub in folder.getFolders():
            walk(sub)
    walk(project.getProjectData().getRootFolder())
