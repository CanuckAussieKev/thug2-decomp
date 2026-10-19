# Checks the game source's run-time writes into the exe's code and data stay clear of rewritten functions.
#
# Game source that has to change code it doesn't own yet writes into the exe at start-up: the GameSpy SDK's host
# names (src/game/net/online_hosts.cpp, until the SDK is linked), the online identity's game name and key when
# [Miscellaneous] OnlineGameName / OnlineGameKey are set (src/game/net/online_identity.cpp: the name's address over 6
# instructions' operands, the key over its 3 stored copies - data), the CFunction table's IsPS2 entry and (debug mode)
# its CD / NotCD entries (src/game/sys/platform_cfuncs.cpp, debug_mode.cpp, until that table is source). Such a write
#   - inside a rewritten function would be bypassed (the game runs our version, not the code written into), and
#   - inside the first bytes of a hooked function would be lost from MinHook's trampoline and break our jump.
# So none may land in any function registered with REPLACE* in src/game. Run from the repo root:
#   python -I tools/check_code_writes.py
import glob
import json
import re
import sys

WINDOW = 16  # more than any trampoline copies

writes = []  # (address of the bytes written, how many, where it's listed)
for name in ("online_hosts.cpp", "online_identity.cpp"):
    text = open("src/game/net/" + name, encoding="utf-8").read()
    # {address, operand offset}: a 32-bit operand
    for m in re.finditer(r"\{\s*0x([0-9A-Fa-f]{8}),\s*(\d+)\s*\}", text):
        writes.append((int(m.group(1), 16) + int(m.group(2)), 4, name))
    # {address, inverted}: a 6-byte key
    for m in re.finditer(r"\{\s*0x([0-9A-Fa-f]{8}),\s*(?:true|false)\s*\}", text):
        writes.append((int(m.group(1), 16), 6, name))
text = open("src/game/sys/platform_cfuncs.cpp", encoding="utf-8").read()
m = re.search(r"kIsPS2Entry = 0x([0-9A-Fa-f]{8})", text)
if m:
    writes.append((int(m.group(1), 16) + 4, 4, "platform_cfuncs.cpp"))
text = open("src/game/sys/debug_mode.cpp", encoding="utf-8").read()
m = re.search(r"kCDEntry = 0x([0-9A-Fa-f]{8})", text)
if m:
    writes.append((int(m.group(1), 16) + 4, 4, "debug_mode.cpp"))
m = re.search(r"kNotCDEntry = 0x([0-9A-Fa-f]{8})", text)
if m:
    writes.append((int(m.group(1), 16) + 4, 4, "debug_mode.cpp"))
if len(writes) != 26:
    print(f"expected 26 writes (14 host names, 6 game names, 3 keys, 3 table entries), found {len(writes)}: update this script")
    sys.exit(1)

registered = set()
for path in glob.glob("src/game/**/*.cpp", recursive=True):
    for m in re.finditer(r"REPLACE\w*\(0x([0-9A-Fa-f]{8})", open(path, encoding="utf-8").read()):
        registered.add(int(m.group(1), 16))
bodies = {f["addr"]: f["end"] for f in json.load(open("data/derived/functions.json", encoding="utf-8"))}
# Functions Ghidra doesn't define (it takes them as part of the function that jumps to them): start -> end.
bodies.update({
    0x00452200: 0x00452677,  # Pcm::PlayNextTrack, reached by a jump from 0x00452910
})

problems = []
for address, size, where in sorted(writes):
    for r in sorted(registered):
        end = max(bodies.get(r, r), r + WINDOW)
        if r <= address + size - 1 and address < end:
            problems.append(f"  0x{address:08X} ({where}) is inside rewritten 0x{r:08X}")
print(f"{len(writes)} run-time writes, {len(registered)} rewritten functions")
for p in problems:
    print(p)
print("No overlaps." if not problems else f"{len(problems)} overlap(s).")
sys.exit(1 if problems else 0)
