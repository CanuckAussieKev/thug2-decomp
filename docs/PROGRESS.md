# Decompilation progress

Generated 2026-10-29 by `tools/progress_report.ts` - regenerate after each batch.

**Game code: ███████████████████░ 95.9%** of the code rewritten (7,996 of 8,482 functions, 1,555.3 KB of 1,621.4 KB).

Progress is by code size: function counts make a 10-byte helper look like as much work as a 2 KB routine. Subsystem boundaries come from the exe map (`docs/subsystems.md`) and are approximate (give or take a few functions); totals are exact.

| Subsystem | Range | Progress (code size) | Functions | Code |
|---|---|---|---:|---:|
| Core utilities (CRC/checksums, string helpers, rect, random) | `0x401000`-`0x406460` | ████████████████████ 100% | 129 / 129 | 19.8 KB / 19.8 KB |
| Animation & skeleton / skinned model data | `0x406460`-`0x41a6d0` | ████████████████████ 100% | 423 / 423 | 75.5 KB / 75.5 KB |
| Object components & level/scene loading | `0x41a6d0`-`0x445b50` | ████████████████████ 100% | 584 / 584 | 157.7 KB / 157.7 KB |
| Engine script commands (lighting/fog, memory heaps, script utils, arrays, music & streams) | `0x445b50`-`0x44e6b0` | ████████████████████ 100% | 246 / 246 | 32.5 KB / 32.5 KB |
| Movies (Bink) & streamed audio | `0x44e6b0`-`0x4544e0` | ████████████████████ 100% | 167 / 167 | 22.1 KB / 22.1 KB |
| Low-level networking (engine net library) | `0x4544e0`-`0x45bda0` | ████████████████████ 100% | 205 / 205 | 26.5 KB / 26.5 KB |
| Level nodes & composite objects (node array, node names/prefixes, CCompositeObject, components) | `0x45bda0`-`0x45efd0` | ████████████████████ 100% | 88 / 88 | 10.0 KB / 10.0 KB |
| Object system & script runtime (composite objects, events, object anims, particles, pathfinding, QB parser/executor) | `0x45efd0`-`0x47dc40` | ████████████████████ 100% | 685 / 685 | 112.3 KB / 112.3 KB |
| Models / faces / textures for skaters (CAS) | `0x47dc40`-`0x490d80` | ░░░░░░░░░░░░░░░░░░░░ 2% | 37 / 523 | 1.4 KB / 67.5 KB |
| Renderer: textures, geometry, particles, 2D/3D screen | `0x490d80`-`0x4a4500` | ████████████████████ 100% | 559 / 559 | 73.1 KB / 73.1 KB |
| Front-end UI: screen elements & text | `0x4a4500`-`0x4b2280` | ████████████████████ 100% | 228 / 228 | 52.5 KB / 52.5 KB |
| Renderer: screen FX, image/texture formats, fonts, grass | `0x4b2280`-`0x4d6d40` | ████████████████████ 100% | 475 / 475 | 113.7 KB / 113.7 KB |
| Application main & game object rendering glue | `0x4d6d40`-`0x4e6b80` | ████████████████████ 100% | 202 / 202 | 59.5 KB / 59.5 KB |
| Goal & rail editors / skater camera | `0x4e6b80`-`0x500020` | ████████████████████ 100% | 507 / 507 | 97.5 KB / 97.5 KB |
| Skater (physics, tricks, walking, stats, sticker tags) | `0x500020`-`0x533f20` | ████████████████████ 100% | 543 / 543 | 186.9 KB / 186.9 KB |
| Online & network game modes (CTF, servers, lobby, buddies, NAT negotiation) | `0x533f20`-`0x54ef60` | ████████████████████ 100% | 455 / 455 | 91.9 KB / 91.9 KB |
| Menu input mapping | `0x54ef60`-`0x550040` | ████████████████████ 100% | 20 / 20 | 4.0 KB / 4.0 KB |
| Goals & career (GoalManager, create-a-trick, trick objects, level launching) | `0x550040`-`0x56c920` | ████████████████████ 100% | 666 / 666 | 103.8 KB / 103.8 KB |
| Time-of-day manager & world state | `0x56c920`-`0x577300` | ████████████████████ 100% | 137 / 137 | 38.1 KB / 38.1 KB |
| Viewer / gaps / records / career save info | `0x577300`-`0x588180` | ████████████████████ 100% | 304 / 304 | 60.7 KB / 60.7 KB |
| Park editor & graffiti | `0x588180`-`0x595030` | ████████████████████ 100% | 289 / 289 | 47.3 KB / 47.3 KB |
| Park editor (Create-A-Park) & skate script commands | `0x595030`-`0x59f610` | ████████████████████ 100% | 169 / 169 | 40.1 KB / 40.1 KB |
| Game script commands (skater queries, save/memcard, profiles, appearance, cutscenes, weather) | `0x59f610`-`0x5b6560` | ████████████████████ 100% | 574 / 574 | 87.1 KB / 87.1 KB |
| File system (PIP/PRE archives) & skater memory heaps | `0x5b6560`-`0x5bdad0` | ████████████████████ 100% | 222 / 222 | 27.1 KB / 27.1 KB |
| Input devices (SIO) | `0x5bdad0`-`0x5be6f0` | ████████████████████ 100% | 25 / 25 | 2.8 KB / 2.8 KB |
| Game clock (Tmr) | `0x5be6f0`-`0x5beaae` | ████████████████████ 100% | 15 / 15 | 0.8 KB / 0.8 KB |
| Win32 platform layer (window, D3D9/DirectSound/DirectInput setup, registry options) | `0x5f2e00`-`0x5f6640` | ████████████████████ 100% | 42 / 42 | 10.4 KB / 10.4 KB |

## Not decompiled (libraries)

Linked as libraries in the final build rather than rewritten:

- Import thunks (Winsock / Direct3DCreate9) (`0x5beaae`-`0x5beb1a`): 18 functions, 0.1 KB
- D3DX9 static library (shader assembler/compiler, math helpers) (`0x5beb1a`-`0x5eef20`): 677 functions, 157.8 KB
- D3DX9's JPEG codec (IJG libjpeg inside the D3DX9 static library) (`0x5eef20`-`0x5f2e00`): 22 functions, 5.1 KB
- Data in .text (a table of .rdata pointers, offsets and flags; no code) (`0x5f6640`-`0x5f8130`): 1 functions, 0.0 KB
- C++ library template code inside the game's ranges (`data/library_functions.csv`): 40 functions, 3.0 KB
- Not counted: 3 Ghidra entries in the game's ranges that aren't game functions (a split inside a rewritten function, PartyMod's code cave: `data/not_functions.csv`)
- GameSpy SDK, MSVC C runtime, STL helpers and compiler-generated code after `0x5F8130` (see `docs/library_ranges.md`)

## PartyMod

PartyMod's changes (MIT, (c) 2024 PARTYMANX, see `THIRD_PARTY_NOTICES.md`) are part of the game's source: the SDL window and input, widescreen, the settings file and the rest, by subsystem. Still written into the exe at start-up: the GameSpy SDK's host names (library code, `src/game/net/online_hosts.cpp`), the online identity when it's set (`src/game/net/online_identity.cpp`) and, with PS2 controls, the CFunction table's IsPS2 entry (`src/game/sys/platform_cfuncs.cpp`).
