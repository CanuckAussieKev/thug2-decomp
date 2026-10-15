# Checks that no PartyMod patch lands inside the first bytes of a function we replace.
#
# MinHook copies a replaced function's first instructions into its trampoline when our hooks are installed;
# PartyMod patches the exe after that. A PartyMod patch inside those first bytes would be lost from the
# trampoline (and broken by our jump), so the two must never overlap. Run from the repo root:
#   python tools/check_partymod_overlap.py
import glob
import re
import sys

WINDOW = 16  # more than any trampoline copies

src = "".join(open(f, encoding="utf-8", errors="replace").read()
              for f in glob.glob("src/partymod/**/*.[ch]", recursive=True))
names = {m.group(1): int(m.group(2), 16)
         for m in re.finditer(r"(addr_\w+)\s*=\s*\(?(?:void\s*\*\))?\s*\(?(0x[0-9a-fA-F]+)", src)}
patches, unresolved = set(), set()
for m in re.finditer(r"patch\w*\(\s*\(?(?:void\s*\*\)\s*)?\(?(0x[0-9a-fA-F]+|addr_\w+)\s*(?:\+\s*(\d+))?", src):
    target, offset = m.group(1), int(m.group(2) or 0)
    value = int(target, 16) if target.startswith("0x") else names.get(target)
    if value is None:
        unresolved.add(target)
    else:
        patches.add(value + offset)

replaced = set()
for path in glob.glob("src/**/*.cpp", recursive=True):
    for m in re.finditer(r"REPLACE\(0x([0-9A-Fa-f]{8})", open(path, encoding="utf-8").read()):
        replaced.add(int(m.group(1), 16))

overlaps = sorted((p, r) for p in patches for r in replaced if r <= p < r + WINDOW)
print(f"{len(patches)} PartyMod patch addresses, {len(replaced)} replaced functions")
for name in sorted(unresolved):
    print(f"  could not resolve {name}")
for p, r in overlaps:
    print(f"  OVERLAP: PartyMod patches 0x{p:08X}, inside the start of replaced 0x{r:08X}")
print("No overlaps." if not overlaps else f"{len(overlaps)} overlap(s).")
sys.exit(1 if overlaps or unresolved else 0)
