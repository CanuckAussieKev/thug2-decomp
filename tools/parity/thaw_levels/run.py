# NOT ORIGINAL - the Python side of a THAW parity case (cases.ts): runs a tool and then normalises the compiled .qb
# files it wrote (qbnorm.ts, the same code for both sides); its stdout passed on with the side's own output folder
# written "{out}" (the two sides' folders differ), its exit code passed on. --copy <from> <to>: a folder copied first
# (the layouts migrate_layouts.py changes, each side its own copy). Goes with the Python twins.
#
#   python -I tools/parity/thaw_levels/run.py <out> [--copy <from> <to>] <tool.py> [args...]

import os
import shutil
import subprocess
import sys

out, rest = sys.argv[1], sys.argv[2:]
if rest[0] == "--copy":
    shutil.copytree(rest[1], rest[2])
    rest = rest[3:]
result = subprocess.run([sys.executable, "-I"] + rest, stdout=subprocess.PIPE)
sys.stdout.buffer.write(result.stdout.replace(out.encode("utf-8"), b"{out}"))
sys.stdout.flush()
here = os.path.dirname(os.path.abspath(__file__))
subprocess.run(["node", os.path.join(here, "qbnorm.ts"), out], check=True)
sys.exit(result.returncode)
