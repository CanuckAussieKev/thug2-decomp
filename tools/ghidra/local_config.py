"""NOT ORIGINAL - this machine's Ghidra project and program names, from the checkout's local.json (gitignored; README
"Contributing"): ghidraProject (the project's name, default THUG2) and ghidraProgram (the program's path in it,
default /THUG2.exe)."""
import json
import os

_CHECKOUT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))


def _main_checkout(checkout):
    """The main checkout of a linked git worktree (its .git is a file "gitdir: <main>/.git/worktrees/<name>", whose
    commondir names <main>/.git), or None: local.json is gitignored, so a worktree has none of its own."""
    dot_git = os.path.join(checkout, ".git")
    if not os.path.isfile(dot_git):
        return None
    with open(dot_git, encoding="utf-8") as f:
        line = f.read().strip()
    if not line.startswith("gitdir:"):
        return None
    gitdir = os.path.join(checkout, line[len("gitdir:"):].strip())
    commondir = os.path.join(gitdir, "commondir")
    if not os.path.isfile(commondir):
        return None
    with open(commondir, encoding="utf-8") as f:
        return os.path.dirname(os.path.normpath(os.path.join(gitdir, f.read().strip())))


_LOCAL = os.path.join(_CHECKOUT, "local.json")
if not os.path.exists(_LOCAL) and _main_checkout(_CHECKOUT):
    _LOCAL = os.path.join(_main_checkout(_CHECKOUT), "local.json")
_config = {}
if os.path.exists(_LOCAL):
    with open(_LOCAL, encoding="utf-8") as f:
        _config = json.load(f)

GHIDRA_PROJECT = _config.get("ghidraProject", "THUG2")
GHIDRA_PROGRAM = _config.get("ghidraProgram", "/THUG2.exe")
REFERENCE_EXE = _config.get("referenceExe", "")
