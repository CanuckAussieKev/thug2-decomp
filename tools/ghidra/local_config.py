"""NOT ORIGINAL - this machine's Ghidra project and program names, from the checkout's local.json (gitignored; README
"Contributing"): ghidraProject (the project's name, default THUG2) and ghidraProgram (the program's path in it,
default /THUG2.exe)."""
import json
import os

_LOCAL = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "local.json")
_config = {}
if os.path.exists(_LOCAL):
    with open(_LOCAL, encoding="utf-8") as f:
        _config = json.load(f)

GHIDRA_PROJECT = _config.get("ghidraProject", "THUG2")
GHIDRA_PROGRAM = _config.get("ghidraProgram", "/THUG2.exe")
REFERENCE_EXE = _config.get("referenceExe", "")
