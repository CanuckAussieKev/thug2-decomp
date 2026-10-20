# NOT ORIGINAL - the Python half of tools/thps3_levels' parity wrapper (wrap.ts is the TypeScript half): runs
# thps3_levels.py with the arguments given, prints its stdout with <out> written as "{out}" (the two sides write into
# different folders), passes its exit code on, and replaces each .qb it wrote under <out> by a .qb.txt in a stable
# form (driver.py's qb_norm: ns.exe writes its 0x2B checksum names in Go map order, different every run).
#
#   python -I tools/parity/thps3_levels/wrap.py <out> <thps3_levels.py arguments...>

import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(TOOLS, "level_port"))

import qb  # noqa: E402

CONVERTER = os.path.join(TOOLS, "thps3_levels", "thps3_levels.py")


def qb_norm(data):
    toks = list(qb.tokens(data, weights=True))
    first = next((off for off, t, _ in toks if t == 0x2B), len(data))
    names = sorted(p.hex() for _, t, p in toks if t == 0x2B)
    rest = [bytes([t]).hex() + p.hex() for off, t, p in toks if off >= first and t != 0x2B]
    return "%s\n%s\n%s\n" % (data[:first].hex(), "\n".join(names), "".join(rest))


def main():
    out = sys.argv[1]
    result = subprocess.run([sys.executable, "-I", CONVERTER] + sys.argv[2:], capture_output=True)
    sys.stdout.buffer.write(result.stdout.replace(out.encode("utf-8"), b"{out}"))
    sys.stderr.buffer.write(result.stderr)
    for root, _, files in os.walk(out):
        for name in files:
            if name.lower().endswith(".qb"):
                path = os.path.join(root, name)
                with open(path, "rb") as f:
                    data = f.read()
                with open(path + ".txt", "w", encoding="utf-8", newline="\n") as f:
                    f.write(qb_norm(data))
                os.remove(path)
    return result.returncode


if __name__ == "__main__":
    sys.exit(main())
