"""Mutation testing: prove the self-tests catch deliberate bugs.

    python -I tools/mutate.py <spec.py> [names...]

<spec.py> defines MUTS = [(file, name, old_text, new_text), ...] (old_text must occur exactly once in the
file) and may define BUILD = a command that builds out/build/verify (default: tools/verify_build.cmd). Each
mutation is applied, built and checked with out/build/verify/thug2_check.exe, then the file is restored.
With names, only those mutations run.

A mutation that makes the checker hang counts as caught. The checker's output goes to a file rather than a
pipe: its helper child process would hold a pipe open past the timeout, and taskkill /T ends both.
"""
import os
import runpy
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
spec = runpy.run_path(sys.argv[1])
build = spec.get("BUILD", os.path.join(REPO, "tools", "verify_build.cmd"))
checker = os.path.join(REPO, "out", "build", "verify", "thug2_check.exe")
log_path = os.path.join(os.environ.get("TEMP", "."), "mutate_check_output.txt")
only = sys.argv[2:]

for path, name, old, new in spec["MUTS"]:
    if only and name not in only:
        continue
    original = open(path, encoding="utf-8").read()
    assert original.count(old) == 1, (name, original.count(old))
    open(path, "w", encoding="utf-8").write(original.replace(old, new, 1))
    try:
        subprocess.run(["cmd", "/c", build], capture_output=True, text=True)
        with open(log_path, "w") as out:
            process = subprocess.Popen([checker], stdout=out, stderr=out, cwd=REPO)
            try:
                code = process.wait(timeout=60)
                print(name, "CAUGHT" if code != 0 else "missed", code, flush=True)
            except subprocess.TimeoutExpired:
                subprocess.run(["taskkill", "/F", "/T", "/IM", "thug2_check.exe"], capture_output=True)
                process.wait()
                print(name, "CAUGHT (hang)", flush=True)
    finally:
        open(path, "w", encoding="utf-8").write(original)
