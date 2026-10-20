# (Has a TypeScript twin, script_diff.ts, which the TypeScript tools use: until this file is removed - once the Python
# tools importing it are ported, docs/typescript.md - change both, and run tools/parity's cases.)
# Compares converted levels' .qb script by script (NeverScript decompiles), new vs reference staging folders.
# usage: script_diff.py <new staging> <reference staging> [show N]
import glob
import os
import re
import subprocess
import sys
import tempfile

NS = r"C:\Dev\thug2-decomp\out\neverscript\ns.exe"
new_root, ref_root = sys.argv[1], sys.argv[2]
show = int(sys.argv[3]) if len(sys.argv) > 3 else 0
tmp = tempfile.mkdtemp()


def scripts(path):
    out = os.path.join(tmp, "x.ns")
    subprocess.run([NS, "-d", path, "-o", out], capture_output=True)
    text = open(out, encoding="utf-8", errors="replace").read()
    found = {}
    for m in re.finditer(r"^script (\S+) \{(.*?)^\} *$", text, re.S | re.M):
        found[m.group(1)] = m.group(0)
    text = re.sub(r"^__register_checksums__.*$", "", text, flags=re.M)
    rest = re.sub(r"^script (\S+) \{(.*?)^\} *$", "", text, flags=re.S | re.M)
    return found, rest


total = {"same": 0, "changed": 0, "only new": 0, "only ref": 0, "data differs": 0}
examples = []
for f in sorted(glob.glob(os.path.join(new_root, "levels", "*", "*.qb"))):
    r = os.path.join(ref_root, os.path.relpath(f, new_root))
    if not os.path.exists(r):
        print("no reference:", r)
        continue
    if open(f, "rb").read() == open(r, "rb").read():
        continue
    a, ra = scripts(f)
    b, rb = scripts(r)
    if ra != rb:
        total["data differs"] += 1
    for name in set(a) | set(b):
        if name not in b:
            total["only new"] += 1
            examples.append((os.path.basename(f), name, "only new", a[name], ""))
        elif name not in a:
            total["only ref"] += 1
            examples.append((os.path.basename(f), name, "only ref", "", b[name]))
        elif a[name] != b[name]:
            total["changed"] += 1
            examples.append((os.path.basename(f), name, "changed", a[name], b[name]))
        else:
            total["same"] += 1
print(total)
for level, name, kind, x, y in examples[:show]:
    print("=====", level, name, kind)
    import difflib
    for line in difflib.unified_diff(y.splitlines(), x.splitlines(), "ref", "new", n=1, lineterm=""):
        print(line)
