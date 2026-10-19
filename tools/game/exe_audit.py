"""Audits two builds of the game's exe against this project's rewritten functions: every differing byte, which
function or data it's in, whether we rewrote that function, and which functions read differing data.

    python -I tools/game/exe_audit.py [--a C:\\Games\\THUG2_PM_BAK\\THUG2PM.exe] [--b C:\\Games\\THUG2\\Game\\THUG2.exe]

Needs data/derived/functions.json (tools/ghidra/export_functions.py). Rewritten = a REPLACE* line under src/game.
"""
import argparse
import bisect
import glob
import json
import os
import re
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def sections(data):
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    count = struct.unpack_from("<H", data, pe + 6)[0]
    base = struct.unpack_from("<I", data, pe + 24 + 28)[0]
    table = pe + 24 + struct.unpack_from("<H", data, pe + 20)[0]
    out = []
    for i in range(count):
        name, vsize, va, rsize, raw = struct.unpack_from("<8sIIII", data, table + 40 * i)
        out.append((name.rstrip(b"\0").decode("latin-1"), base + va, vsize, raw, rsize))
    return base, out


def to_va(secs, offset):
    for name, va, vsize, raw, rsize in secs:
        if raw <= offset < raw + rsize:
            return va + offset - raw, name
    return None, "header"


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--a", default=r"C:\Games\THUG2_PM_BAK\THUG2PM.exe")
    p.add_argument("--b", default=r"C:\Games\THUG2\Game\THUG2.exe")
    args = p.parse_args()
    a, b = open(args.a, "rb").read(), open(args.b, "rb").read()
    base_a, secs_a = sections(a)
    base_b, secs_b = sections(b)
    print("A %s: %d bytes; B %s: %d bytes" % (args.a, len(a), args.b, len(b)))
    same_layout = len(a) == len(b) and secs_a == secs_b and base_a == base_b
    print("same size, image base and section table: %s" % same_layout)
    if not same_layout:
        sys.exit("different layouts - this audit only compares same-layout builds")

    # the differing runs
    runs, start = [], None
    for i in range(len(a)):
        if a[i] != b[i]:
            if start is None:
                start = i
        elif start is not None:
            runs.append((start, i))
            start = None
    if start is not None:
        runs.append((start, len(a)))
    print("differing bytes: %d in %d runs" % (sum(e - s for s, e in runs), len(runs)))

    # functions (Ghidra's) and ours
    funcs = json.load(open(os.path.join(ROOT, "data", "derived", "functions.json"), encoding="utf-8"))
    funcs.sort(key=lambda f: f["addr"])
    starts = [f["addr"] for f in funcs]
    replaced = {}
    for path in glob.glob(os.path.join(ROOT, "src", "game", "**", "*.cpp"), recursive=True):
        text = open(path, encoding="utf-8", errors="replace").read()
        for m in re.finditer(r"\b(REPLACE\w*)\(\s*0x([0-9A-Fa-f]+)\s*,\s*([^;]*?)\);", text):
            replaced[int(m.group(2), 16)] = (m.group(1), m.group(3).strip()[:70], os.path.relpath(path, ROOT))
    print("rewritten functions (REPLACE* under src/game): %d" % len(replaced))

    def function_at(va):
        i = bisect.bisect_right(starts, va) - 1
        if i >= 0 and funcs[i]["addr"] <= va < funcs[i]["end"]:
            # Ghidra sometimes makes one huge function over many: prefer a rewritten start just before
            return funcs[i]
        return None

    # 1. every rewritten function's original bytes, A against B
    by_addr = {f["addr"]: f for f in funcs}
    differing_rewritten = []
    unsized = []
    for addr in sorted(replaced):
        f = by_addr.get(addr)
        if not f:
            unsized.append(addr)
            continue
        end = f["end"]
        # (a function Ghidra over-extended: up to the next rewritten / known function start)
        nxt = starts[bisect.bisect_right(starts, addr)] if bisect.bisect_right(starts, addr) < len(starts) else end
        end = min(end, nxt)
        lo, hi = addr - base_a, end - base_a  # .text is raw == va - base here (checked below)
        if a[lo:hi] != b[lo:hi]:
            differing_rewritten.append((addr, end))
    text_sec = next(s for s in secs_a if s[0] == ".text")
    assert text_sec[1] - base_a == text_sec[3], "text raw offset != rva: offsets need mapping"
    print("\n1. rewritten functions whose original code differs between A and B: %d of %d"
          % (len(differing_rewritten), len(replaced) - len(unsized)))
    for addr, end in differing_rewritten:
        kind, name, path = replaced[addr]
        diffs = [i for i in range(addr - base_a, end - base_a) if a[i] != b[i]]
        print("   0x%08X-0x%08X %s %s (%s): %d bytes differ at %s" % (
            addr, end, kind, name, path, len(diffs), ", ".join("0x%08X" % (base_a + i) for i in diffs[:8])))
    if unsized:
        print("   (%d rewritten addresses Ghidra has no function at - checked by the runs below: %s)" % (
            len(unsized), ", ".join("0x%08X" % x for x in unsized[:10])))

    # 2. each run: where, whose
    print("\n2. every differing run:")
    data_runs = []
    for s, e in runs:
        va, sec = to_va(secs_a, s)
        f = function_at(va) if sec == ".text" else None
        owner = ""
        if f:
            rewritten_start = max((x for x in replaced if x <= va), default=None)
            r = replaced.get(f["addr"])
            if r:
                owner = "in %s (rewritten: %s %s)" % (f["name"], r[0], r[1])
            elif rewritten_start is not None and by_addr.get(rewritten_start) is None and va - rewritten_start < 0x40:
                owner = "near rewritten 0x%08X" % rewritten_start
            else:
                owner = "in %s 0x%08X-0x%08X (NOT rewritten)" % (f["name"], f["addr"], f["end"])
        elif sec != ".text":
            data_runs.append((va, va + e - s))
            owner = "data"
        else:
            owner = "in no function (padding / unreached)"
        print("   0x%08X %-6s %3d bytes  A=%s B=%s  %s" % (va, sec, e - s, a[s:s + min(e - s, 10)].hex(),
                                                         b[s:s + min(e - s, 10)].hex(), owner))

    # 3. which functions read the differing data (Ghidra's data references), and whether we rewrote them
    print("\n3. functions referring to the differing data:")
    for lo, hi in data_runs:
        users = [f for f in funcs if any(lo - 0x40 <= r < hi for r in f.get("data_refs", []))]
        text = ", ".join("%s%s" % (f["name"], " (REWRITTEN %s)" % replaced[f["addr"]][1] if f["addr"] in replaced else "")
                         for f in users[:6]) or "none found"
        print("   0x%08X-0x%08X: %s" % (lo, hi, text))


if __name__ == "__main__":
    main()
