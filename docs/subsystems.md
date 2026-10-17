# THUG2 subsystem map

First-pass map of `THUG2PM.exe`'s code, built in chunks 1-5 (see `mapping-progress.md`). Ranges come from
`data/subsystems.csv`; sizes are measured from the Ghidra project. Boundaries are approximate (+/- a few functions)
and labels come from script-command names, strings, imports and PartyMod's source. Refine as code is read.

**Game code to decompile: ~8,365 functions, ~1.6 MB of x86.**
Not decompiled: GameSpy SDK, D3DX9, MSVC CRT, STL helpers, compiler funclets (see `library_ranges.md`).

| Range | Subsystem | Funcs | Code | Named |
|---|---|---:|---:|---:|
| 0x401000-0x406460 | Core utilities (CRC/checksums, string helpers, rect, random) | 130 | 19 KB | 3% |
| 0x406460-0x41a6d0 | Animation & skeleton / skinned model data | 423 | 75 KB | 0% |
| 0x41a6d0-0x445b50 | Object components & level/scene loading | 584 | 157 KB | 3% |
| 0x445b50-0x44e6b0 | Engine script commands (lighting/fog, memory heaps, script utils, arrays, music & streams) | 246 | 32 KB | 89% |
| 0x44e6b0-0x4544e0 | Movies (Bink) & streamed audio | 167 | 22 KB | 1% |
| 0x4544e0-0x45bda0 | Low-level networking (engine net library: Net::Manager getters, message types, Conn, App, Client/Server, Dispatcher; boundary moved from 0x456910 in batch 86) | 205 | 26 KB | 0% |
| 0x45bda0-0x45efd0 | Level nodes & composite objects (node array, node names/prefixes, CCompositeObject, components) | 88 | 10 KB | 0% |
| 0x45efd0-0x47dc40 | Object system & script runtime (composite objects, events, particles, pathfinding, QB parser/executor; runtime behaviour: `script-runtime.md`) | 685 | 112 KB | 1% |
| 0x47dc40-0x490d80 | Skater models / faces / textures (create-a-skater) | 523 | 67 KB | 3% |
| 0x490d80-0x4a4500 | Renderer: textures, geometry, particles, 2D/3D screen | 559 | 73 KB | 0% |
| 0x4a4500-0x4b2280 | Front-end UI: screen elements & text | 228 | 52 KB | 8% |
| 0x4b2280-0x4d6d40 | Renderer: screen FX, image formats, fonts, grass | 488 | 114 KB | 1% |
| 0x4d6d40-0x4e6b80 | Application main (WinMain) & game-object rendering glue | 202 | 59 KB | 1% |
| 0x4e6b80-0x500020 | Goal & rail editors / skater camera | 341 | 85 KB | 0% |
| 0x500020-0x533f20 | Skater: physics, tricks, walking, stats, sticker tags | 544 | 186 KB | 0% |
| 0x533f20-0x54ef60 | Online & network game modes (CTF, servers, lobby, buddies, NAT negotiation) | 457 | 91 KB | 24% |
| 0x54ef60-0x550040 | Menu input mapping | 20 | 4 KB | 20% |
| 0x550040-0x56c920 | Goals & career (GoalManager, create-a-trick, level launching) | 666 | 103 KB | 26% |
| 0x56c920-0x577300 | Time-of-day manager & world state | 137 | 38 KB | 13% |
| 0x577300-0x588180 | Viewer / gaps / records / career save info | 304 | 60 KB | 1% |
| 0x588180-0x595030 | Park editor & graffiti | 289 | 47 KB | 14% |
| 0x595030-0x59f610 | Park editor (Create-A-Park) & skate script commands | 169 | 40 KB | 13% |
| 0x59f610-0x5b6560 | Game script commands (skater queries, save/memcard, profiles, appearance, cutscenes, weather) | 574 | 87 KB | 85% |
| 0x5b6560-0x5bdad0 | File system (PIP/PRE archives) & skater memory heaps | 222 | 27 KB | 6% |
| 0x5bdad0-0x5be6f0 | Input devices (SIO) | 25 | 2 KB | 8% |
| 0x5be6f0-0x5beaae | Game clock (Tmr) | 16 | 1 KB | - |
| 0x5beaae-0x5beb1a | *Import thunks (Winsock, Direct3DCreate9)* | 18 | - | - |
| 0x5beb1a-0x5eef20 | *Library: D3DX9 (shader assembler/compiler, math)* | 694 | 158 KB | - |
| 0x5eef20-0x5f8130 | Win32 platform layer (window, D3D9/DSound/DInput setup, registry options) | 91 | 17 KB | 5% |

## Suggested order for the hook DLL

Start where code is small, self-contained and easy to check, then work inwards:

1. **Core utilities.** Small, used everywhere, and mostly pure functions. Each rewritten function can be tested
   automatically by calling the original and ours with the same inputs and comparing results.
2. **File system (PIP/PRE).** Small, and immediately visible: if it's wrong, nothing loads.
3. **Script command libraries** (both blocks, 85-89% already named). Hundreds of small, independent functions
   with known names: steady, low-risk progress, and they document what the game logic needs.
4. **Win32 platform layer + input devices.** Small; this is where PartyMod's fixes (SDL2 window/input,
   resolution, aspect ratio) are built into the source (done: src/game/sys/sdl_*, config, pc_setup;
   src/game/sio/sdl_input; src/game/gfx/settings, glyphs, screen, render; src/game/net/online_hosts).
5. Then the big interlinked cores: **script runtime & object system -> skater -> goals/career -> renderer**.
6. **Networking last.** GameSpy is gone; online play goes through OpenSpy, so revisit once everything else works.

## Known gaps
- Unit boundaries inside each subsystem are in `data/derived/units.csv` (222 units vs. 483 real .cpp files,
  so many units are several files merged).
- One function Ghidra couldn't create: 0x5a6560 (fix by hand).
- C++ class structure (vtables, member layouts) is not mapped yet; the exe has no RTTI for game classes.
