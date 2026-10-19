"""Mutation testing: prove the self-tests catch deliberate bugs.

    python -I tools/mutate.py <spec.py> [names...]

<spec.py> defines MUTS = [(file, name, old_text, new_text), ...] (old_text must occur exactly once in the
file) and may define BUILD = a command that builds out/build/verify (default: tools/verify_build.cmd), and
TESTS = "TestA,TestB" - only the self-tests whose names contain one of those words run (THUG2_SELFTESTS; all of
them run otherwise: a full check takes ~25 s, a batch's own tests a second or two). Each mutation is applied, built
and checked with out/build/verify/thug2_check.exe, then the file is restored. BUILD needn't configure cmake again:
`cmake --build` alone (after VsDevCmd) is enough once the folder is configured.
With names, only those mutations run.

A mutation that makes the checker hang counts as caught. The checker's output goes to a file rather than a
pipe: its helper child process would hold a pipe open past the timeout, and taskkill /T (by our process id)
ends both.
"""
import os
import runpy
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
spec = runpy.run_path(sys.argv[1])
build = spec.get("BUILD", os.path.join(REPO, "tools", "verify_build.cmd"))
checker = os.path.join(REPO, "out", "build", "verify", "thug2_check.exe")
log_path = os.path.join(REPO, "out", "mutate_check_output.txt")  # (per checkout: parallel runs don't clash)
only = sys.argv[2:]
env = dict(os.environ)
if spec.get("TESTS"):
    env["THUG2_SELFTESTS"] = spec["TESTS"]

for path, name, old, new in spec["MUTS"]:
    if only and name not in only:
        continue
    original = open(path, encoding="utf-8").read()
    assert original.count(old) == 1, (name, original.count(old))
    open(path, "w", encoding="utf-8").write(original.replace(old, new, 1))
    try:
        result = subprocess.run(["cmd", "/c", build], capture_output=True, text=True)
        if result.returncode != 0:
            print(name, "BUILD FAILED (fix the mutation)", flush=True)
            continue
        with open(log_path, "w") as out:
            process = subprocess.Popen([checker], stdout=out, stderr=out, cwd=REPO, env=env)
            try:
                code = process.wait(timeout=60)
                failed = [line.strip() for line in open(log_path, errors="replace")
                          if "SELFTEST FAIL" in line or "SELFTEST CRASH" in line]
                # (every failing test, so an unrelated flaky one doesn't hide which test caught it)
                print(name, "CAUGHT" if code != 0 else "missed", code, ("- " + " | ".join(failed[:4])) if failed else "",
                      flush=True)
            except subprocess.TimeoutExpired:
                # (this run's checker only, by its id: other checkouts' checks may be running)
                subprocess.run(["taskkill", "/F", "/T", "/PID", str(process.pid)], capture_output=True)
                process.wait()
                print(name, "CAUGHT (hang)", flush=True)
    finally:
        open(path, "w", encoding="utf-8").write(original)

# Leave a build of the unmutated code behind (the last build was of a mutation).
subprocess.run(["cmd", "/c", build], capture_output=True, text=True)
