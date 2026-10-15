# Split game code into likely translation units (contiguous address ranges) and label them.
# MSVC links each .cpp's functions contiguously, and also keeps each .cpp's constants/strings together,
# so we cut between neighbouring functions only when nothing ties them together:
#   - direct calls between nearby functions
#   - membership of the same small pointer table (vtables, callback tables), found by scanning the exe
#   - references to nearby data (each file's strings/constants sit together in .rdata/.data)
# The Rich header says ~483 C++ objects were linked, which is a sanity check for the unit count.
#   python tools/map_units.py C:\Games\THUG2\THUG2PM.exe data/derived/functions.json data/derived/units.csv
import json, re, sys, struct, collections, csv, statistics

GAME_LO, GAME_HI = 0x401000, 0x5f8130
CFUNC_TABLE = (0x67F3A0, 0x67F3A0 + 0x4E6 * 8)
WINDOW = 6            # how far apart two functions can be and still tie a gap
MAX_TABLE = 80        # pointer runs longer than this are global tables, not one class
DATA_NEAR = 0x300     # data references this close count as "same file"
COMMON_DATA = 12      # data used by more functions than this is shared globals, ignored
LOCAL_SPAN = 60       # data counts as a file's own if all its users are within this many functions
MIN_FUNCS = 3         # units smaller than this are merged into the previous one
DATA_WINDOW = 8       # functions on each side of a gap whose data is compared

exe_path, funcs_path, out_path = sys.argv[1:4]
allf = json.load(open(funcs_path, encoding="utf-8"))
funcs = sorted((f for f in allf if GAME_LO <= f["addr"] < GAME_HI), key=lambda f: f["addr"])
index = {f["addr"]: i for i, f in enumerate(funcs)}
entries = {f["addr"] for f in allf}

# --- pointer tables: runs of consecutive dwords in .rdata/.data that are function entry points
d = open(exe_path, "rb").read()
pe = struct.unpack_from("<I", d, 0x3C)[0]
o = pe + 24 + struct.unpack_from("<H", d, pe + 20)[0]
tables = []
for k in range(struct.unpack_from("<H", d, pe + 6)[0]):
    name, vsz, va, rsz, rp = struct.unpack_from("<8sIIII", d, o + k * 40)
    if name.rstrip(b"\0") not in (b".rdata", b".data"):
        continue
    run, start = [], None
    for off in range(0, min(vsz, rsz) - 3, 4):
        v = struct.unpack_from("<I", d, rp + off)[0]
        addr = 0x400000 + va + off
        if v in index:
            run.append(index[v])
            start = start or addr
        else:
            if 2 <= len(run) <= MAX_TABLE and not (CFUNC_TABLE[0] <= start < CFUNC_TABLE[1]):
                tables.append(run)
            run, start = [], None

# --- evidence that the gap after function i lies inside one unit
tie = collections.Counter()
def link(a, b):
    a, b = sorted((a, b))
    if 0 < b - a <= WINDOW:
        for g in range(a, b):
            tie[g] += 1

for i, f in enumerate(funcs):
    for c in f["callees"]:
        if c in index:
            link(i, index[c])
for t in tables:
    s = sorted(set(t))
    for a, b in zip(s, s[1:]):
        link(a, b)

data_users = collections.Counter(a for f in funcs for a in f["data_refs"])
anchors = [sorted(a for a in f["data_refs"] if data_users[a] <= COMMON_DATA) for f in funcs]
for i in range(len(funcs)):
    for j in range(i + 1, min(i + WINDOW + 1, len(funcs))):
        if any(abs(a - b) <= DATA_NEAR for a in anchors[i] for b in anchors[j]):
            link(i, j)

# --- evidence that the gap after function i is a file boundary: both sides reference local data
# (used only by nearby functions) and the data ranges don't interleave. Each .obj's data is contiguous and
# linked in the same order as its code, so data addresses step upwards at a boundary.
SECTIONS = ((0x645000, 0x67C000), (0x67C000, 0x7DE000))
users = collections.defaultdict(set)
for i, f in enumerate(funcs):
    for a in f["data_refs"]:
        users[a].add(i)
local = [[[a for a in f["data_refs"] if lo <= a < hi and max(users[a]) - min(users[a]) <= LOCAL_SPAN]
          for lo, hi in SECTIONS] for f in funcs]

def separable(g):
    """True if some section shows left data strictly below right data around gap g, none shows overlap."""
    verdict = None
    for s in range(len(SECTIONS)):
        left = [a for i in range(max(0, g - DATA_WINDOW + 1), g + 1) for a in local[i][s]]
        right = [a for i in range(g + 1, min(len(funcs), g + 1 + DATA_WINDOW)) for a in local[i][s]]
        if left and right:
            if max(left) < min(right):
                verdict = True if verdict is None else verdict
            else:
                return False
    return bool(verdict)

cuts = [0] + [g + 1 for g in range(len(funcs) - 1) if tie[g] == 0 and separable(g)] + [len(funcs)]
# A unit of one or two functions is almost always a helper split off by its own string/constant;
# fold it into the previous unit.
merged = [cuts[0]]
for c in cuts[1:]:
    if c - merged[-1] >= MIN_FUNCS or c == cuts[-1]:
        merged.append(c)
cuts = merged

# --- labels
STOP = set("the a an of to in is for and or not on with from at by be this that error out memory unknown "
           "invalid failed can't couldn't get set".split())
def tokens(f):
    out, n = [], f["name"]
    if n.startswith("CFunc_"):
        n = n[6:]
        if "_" in n:
            out += [n.split("_")[0].lower()] * 3
        out += [w.lower() for w in re.findall(r"[A-Z][a-z]+|[A-Z]+(?![a-z])", n)]
    elif not n.startswith(("FUN_", "thunk_")):
        out += [w.lower() for w in re.findall(r"[A-Z][a-z]+|[A-Z]+(?![a-z])|[a-z]+", n)]
    for s in f["strings"]:
        out += [w.lower() for w in re.findall(r"[A-Za-z]{3,}", s)]
    out += [x.split("!")[1].lower() for x in f["imports"]]
    return [t for t in out if t not in STOP and len(t) > 2]

with open(out_path, "w", newline="", encoding="utf-8") as fh:
    w = csv.writer(fh)
    w.writerow(["unit", "start", "end", "funcs", "bytes", "named", "keywords", "examples"])
    for u, (a, b) in enumerate(zip(cuts, cuts[1:])):
        seg = funcs[a:b]
        kw = collections.Counter(t for f in seg for t in tokens(f)).most_common(6)
        named = [f["name"] for f in seg if not f["name"].startswith(("FUN_", "thunk_"))]
        w.writerow([u, f"{seg[0]['addr']:#x}", f"{seg[-1]['end']:#x}", len(seg), sum(f["size"] for f in seg),
                    len(named), " ".join(k for k, _ in kw), "; ".join(named[:4])])

sizes = [b - a for a, b in zip(cuts, cuts[1:])]
print(f"{len(tables)} pointer tables; {len(sizes)} units from {len(funcs)} functions; "
      f"median {statistics.median(sizes)} funcs/unit, max {max(sizes)}, singletons {sizes.count(1)}")
