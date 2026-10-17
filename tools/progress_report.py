# Writes docs/PROGRESS.md: decompilation progress per subsystem of THUG2PM.exe.
#   python tools/progress_report.py
# Inputs: data/subsystems.csv (the exe map), data/derived/functions.json (function inventory from
# tools/ghidra/export_functions.py) and the REPLACE(...) lines under src/game/ (what we have rewritten).
import csv, datetime, glob, json, re

BAR_WIDTH = 20


def bar(fraction):
    # Round down, so a system only shows a full bar / 100% when it really is complete.
    filled = int(fraction * BAR_WIDTH)
    return "█" * filled + "░" * (BAR_WIDTH - filled)


def percent(fraction):
    return "100%" if fraction >= 1.0 else f"{int(fraction * 100)}%"


def kb(n):
    return f"{n / 1024:,.1f} KB"


functions = {f["addr"]: f for f in json.load(open("data/derived/functions.json", encoding="utf-8"))}
rewritten = set()
for path in glob.glob("src/game/**/*.cpp", recursive=True):
    for m in re.finditer(r"REPLACE(?:_SHADOW|_UNCHECKED|_SOURCE_ONLY)?\(0x([0-9A-Fa-f]{8})", open(path, encoding="utf-8").read()):
        rewritten.add(int(m.group(1), 16))

rows, libraries = [], []
total = {"funcs": 0, "done_funcs": 0, "bytes": 0, "done_bytes": 0}
for r in csv.DictReader(open("data/subsystems.csv", encoding="utf-8")):
    lo, hi = int(r["start"], 16), int(r["end"], 16)
    funcs = [f for a, f in functions.items() if lo <= a < hi]
    size = sum(f["size"] for f in funcs)
    if r["kind"] != "game":
        libraries.append((r, len(funcs), size))
        continue
    done = [f for f in funcs if f["addr"] in rewritten]
    entry = {"row": r, "funcs": len(funcs), "done_funcs": len(done), "bytes": size,
             "done_bytes": sum(f["size"] for f in done)}
    rows.append(entry)
    for k in total:
        total[k] += entry[k]

out = []
out.append("# Decompilation progress\n")
out.append(f"Generated {datetime.date.today()} by `tools/progress_report.py` - regenerate after each batch.\n")
overall = total["done_bytes"] / total["bytes"]
out.append(f"**Game code: {bar(overall)} {overall:.1%}** of the code rewritten "
           f"({total['done_funcs']:,} of {total['funcs']:,} functions, "
           f"{kb(total['done_bytes'])} of {kb(total['bytes'])}).\n")
out.append("Progress is by code size: function counts make a 10-byte helper look like as much work as a 2 KB "
           "routine. Subsystem boundaries come from the exe map (`docs/subsystems.md`) and are approximate "
           "(give or take a few functions); totals are exact.\n")
out.append("| Subsystem | Range | Progress (code size) | Functions | Code |")
out.append("|---|---|---|---:|---:|")
for e in rows:
    r = e["row"]
    fraction = e["done_bytes"] / e["bytes"] if e["bytes"] else 0.0
    out.append(f"| {r['subsystem']} | `{r['start']}`-`{r['end']}` | {bar(fraction)} {percent(fraction)} | "
               f"{e['done_funcs']} / {e['funcs']} | {kb(e['done_bytes'])} / {kb(e['bytes'])} |")
out.append("")
out.append("## Not decompiled (libraries)\n")
out.append("Linked as libraries in the final build rather than rewritten:\n")
for r, n, size in libraries:
    out.append(f"- {r['subsystem']} (`{r['start']}`-`{r['end']}`): {n} functions, {kb(size)}")
out.append("- GameSpy SDK, MSVC C runtime, STL helpers and compiler-generated code after `0x5F8130` "
           "(see `docs/library_ranges.md`)")
out.append("")
out.append("## PartyMod\n")
out.append("PartyMod's changes (MIT, (c) 2024 PARTYMANX, see `THIRD_PARTY_NOTICES.md`) are part of the game's source: the SDL window and "
           "input, widescreen, the settings file and the rest, by subsystem. Still written into the exe at start-up: "
           "the GameSpy SDK's host names (library code, `src/game/net/online_hosts.cpp`) and, with PS2 controls, "
           "the CFunction table's IsPS2 entry (`src/game/sys/platform_cfuncs.cpp`).")

open("docs/PROGRESS.md", "w", encoding="utf-8").write("\n".join(out) + "\n")
print(f"docs/PROGRESS.md: {total['done_funcs']}/{total['funcs']} functions, "
      f"{total['done_bytes']}/{total['bytes']} bytes ({overall:.2%})")
