# List programs in the Ghidra project and their function counts.
import sys
import pyghidra
from local_config import GHIDRA_PROGRAM, GHIDRA_PROJECT
pyghidra.start()
with pyghidra.open_project(r"C:\Games\THUG2-ghidra", GHIDRA_PROJECT) as project:
    def walk(folder):
        for f in folder.getFiles():
            print(f.getPathname(), f.getContentType(), "modified:", f.getLastModifiedTime())
        for sub in folder.getFolders():
            walk(sub)
    walk(project.getProjectData().getRootFolder())
