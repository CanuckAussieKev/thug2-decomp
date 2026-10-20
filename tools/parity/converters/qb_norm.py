# NOT ORIGINAL - the Python half of the level converters' parity runs (tools/parity/<converter>/cases.ts): runs a
# converter (python -I <script> <args...>, its exit code passed on, its stdout with the <out> folder's path written
# as "<out>" - the two sides write to different folders), then rewrites every .qb it wrote under
# <out> with its 0x2B name entries sorted - ns.exe (Go) writes them in map order, different on every run - so the two
# languages' outputs compare byte for byte. The TypeScript half: qb_norm.ts (the same normalisation).
#
#   python -I tools/parity/converters/qb_norm.py <out> <script> [args...]

import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(HERE)), "level_port"))

import qb  # noqa: E402


def normalise(data):
    toks = list(qb.tokens(data, weights=True))
    ends = [off for off, _, _ in toks[1:]] + [None]
    raw = [(t, data[off:end] if end is not None else None, off) for (off, t, _), end in zip(toks, ends)]
    # (the last token runs to the token reader's stop: the end token, 0x00, is one byte)
    if raw:
        t, _, off = raw[-1]
        size = 1 if t == 0 else len(data) - off
        raw[-1] = (t, data[off:off + size], off)
    names = sorted(r for t, r, _ in raw if t == 0x2B)
    first = next((off for t, _, off in raw if t == 0x2B), None)
    if first is None:
        return data
    rest = b"".join(r for t, r, off in raw if off >= first and t != 0x2B)
    end = raw[-1][2] + len(raw[-1][1])
    return data[:first] + b"".join(names) + rest + data[end:]


def main():
    out, script, args = sys.argv[1], sys.argv[2], sys.argv[3:]
    result = subprocess.run([sys.executable, "-I", script] + args, stdout=subprocess.PIPE)
    sys.stdout.buffer.write(result.stdout.replace(out.encode("utf-8"), b"<out>"))
    sys.stdout.flush()
    code = result.returncode
    for folder, _, files in os.walk(out):
        for f in files:
            if f.lower().endswith(".qb"):
                path = os.path.join(folder, f)
                data = open(path, "rb").read()
                open(path, "wb").write(normalise(data))
    sys.exit(code)


if __name__ == "__main__":
    main()
