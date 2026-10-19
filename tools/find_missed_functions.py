# Find code that data points to (vtables, callback tables) but Ghidra never made into a function:
# 16-byte aligned, preceded by CC padding, inside game code, not inside any known function.
#   python tools/find_missed_functions.py C:\Games\THUG2_PM_BAK\THUG2PM.exe data/derived/functions.json data/derived/missed_functions.txt
import json, struct, sys, bisect

exe_path, funcs_path, out_path = sys.argv[1:4]
d = open(exe_path, "rb").read()
F = json.load(open(funcs_path, encoding="utf-8"))
entries = {f["addr"] for f in F}
spans = sorted((f["addr"], f["end"]) for f in F)
starts = [s for s, _ in spans]

def inside_function(v):
    i = bisect.bisect_right(starts, v) - 1
    return i >= 0 and spans[i][0] < v <= spans[i][1]

pe = struct.unpack_from("<I", d, 0x3C)[0]
o = pe + 24 + struct.unpack_from("<H", d, pe + 20)[0]
found = set()
for k in range(struct.unpack_from("<H", d, pe + 6)[0]):
    name, vsz, va, rsz, rp = struct.unpack_from("<8sIIII", d, o + k * 40)
    if name.rstrip(b"\0") not in (b".rdata", b".data"):
        continue
    for off in range(0, min(vsz, rsz) - 3, 4):
        v = struct.unpack_from("<I", d, rp + off)[0]
        if (0x401000 <= v < 0x5f8130 and v not in entries and v % 16 == 0
                and d[v - 0x400000 - 1] == 0xCC and not inside_function(v)):
            found.add(v)

with open(out_path, "w", encoding="utf-8") as fh:
    fh.writelines(f"{v:#x}\n" for v in sorted(found))
print(f"{len(found)} missed functions -> {out_path}")
