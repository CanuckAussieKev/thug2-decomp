# Exe mapping - progress

Goal: split THUG2PM.exe's ~11.6k functions into library code vs game code, and group game code into
subsystems, so we know what to decompile and in what order. Output: `docs/subsystems.md`.

Restart rule: do the first unchecked chunk. Each chunk writes its outputs to the repo before being ticked.

Read-only analysis runs against a copy of the Ghidra project (`tools/ghidra/snapshot_project.ps1`),
so the MCP server can stay running. Only chunks that rename things in Ghidra need it disabled.

- [x] 1. Export function inventory (address, size, name, callers, callees, strings, imports)
      -> `data/derived/functions.json` (`tools/ghidra/export_functions.py`)
- [x] 2. Imports: which DLLs/APIs the game uses and which functions call them -> `docs/imports.md`
- [x] 3. Library code: identify CRT/STL/third-party ranges -> `docs/library_ranges.md`
- [x] 3b. Create the 686 functions Ghidra missed (only reached via vtables/tables). NEEDS MCP DISABLED.
      `python tools/find_missed_functions.py ...` (done, list in `data/derived/missed_functions.txt`), then
      `tools/ghidra/create_functions.py`, then re-snapshot + re-export (chunk 1 commands) and re-run
      find_missed_functions until it finds ~0 (new functions can reveal more).
- [x] 4. Translation units: split game code into contiguous address ranges (MSVC links each .cpp
      contiguously) and label them from CFunc names, strings and imports -> `data/derived/units.csv`
- [x] 5. Write `docs/subsystems.md` (subsystem -> ranges, key functions, size estimate, suggested order)
- [ ] 6. Apply unit/subsystem labels back into Ghidra (needs MCP disabled)

## Notes
- Chunk 1 (2026-10-14): 11,624 .text functions, 1.93 MB code. 8,013 FUN_, 1,162 CFunc_, 2,449 other named
  (Ghidra library IDs, thunks, names from PartyMod's source). 360 call imports directly, 872 reference strings.
- Exe-derived data lives in `data/derived/` (gitignored, regenerable).
- Chunk 2: proxy DLL choice -> dinput8.dll (1 export). Networking ~0x5f8170-0x622c90 (likely GameSpy),
  Bink player 0x44fcc0-0x453470.
- Chunk 3: game code = 0x401000-0x5f8130 (~8,390 funcs, ~1.65 MB). GameSpy 0x5f8160-0x622e80, CRT 0x622f80+.
- Chunk 4 attempt 1: call-graph-only split gave 1,368 tiny units (vtable calls are invisible to the call graph).
  Attempt 2: re-export with data-reference addresses + "pointed to from data" (vtables, tables), then merge
  neighbours that share a vtable or reference nearby data.
  Attempt 2 result: 3,038 units, 2,617 singletons - cutting at every "no evidence" gap is wrong. Next: cut only on
  positive boundary evidence (data anchors far apart on both sides), attach unclear functions to the previous
  unit; target ~480 units (Rich header: 483 C++ objects). Also blocked on 3b: missing vtable functions break
  the pointer-table runs (only 363 tables found).
- Chunk 3b: created 685 functions in one round (project 11,816 -> 12,502 functions; game code now 9,077).
  Re-scan finds nothing new. One failure: 0x5a6560 (`push esi; push 0x657848; call ...` - looks like a real
  function start, probably swallowed into the previous function's body; fix by hand later).
  functions.json was re-exported from the real project after this.
- Chunk 4 final: 222 units (median 14.5 funcs). Rule: cut only where both sides reference file-local data and
  the data ranges don't interleave, with no call/vtable tie across the gap; fold units of <3 funcs into the
  previous one. Checks: Theme_/CAT_/TODManager_ script funcs each land in 1 unit, GoalManager_ in 4.
  222 < 483 real .cpp files, so many units are several adjacent files merged - fine for a subsystem map.
  Dead end recorded: C++ static-initializer tables (0x67c16c, 0x67c17c) hold only 3+2 entries, no use.
- Chunk 5: 25 subsystems in `data/subsystems.csv` -> `docs/subsystems.md`. Found D3DX9 statically linked at
  0x5be670-0x5eef20 (712 funcs). Game code to decompile: ~8,365 funcs, ~1.6 MB.
- Chunk 6 is optional (labels in Ghidra); skip unless wanted - the CSV is the source of truth.
- Tooling gotcha: Python on this machine defaults to cp1252; always open text files with encoding="utf-8".
