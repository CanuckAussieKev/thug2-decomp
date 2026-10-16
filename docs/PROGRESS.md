# Decompilation progress

Generated 2026-10-16 by `tools/progress_report.py` - regenerate after each batch.

**Game code: █░░░░░░░░░░░░░░░░░░░ 7.9%** of the code rewritten (812 of 8,382 functions, 127.8 KB of 1,617.8 KB).

Progress is by code size: function counts make a 10-byte helper look like as much work as a 2 KB routine. Subsystem boundaries come from the exe map (`docs/subsystems.md`) and are approximate (give or take a few functions); totals are exact.

| Subsystem | Range | Progress (code size) | Functions | Code |
|---|---|---|---:|---:|
| Core utilities (CRC/checksums, string helpers, rect, random) | `0x401000`-`0x406460` | ███████████████████░ 99% | 129 / 130 | 19.8 KB / 19.8 KB |
| Animation & skeleton / skinned model data | `0x406460`-`0x41a6d0` | ░░░░░░░░░░░░░░░░░░░░ 0% | 5 / 423 | 0.3 KB / 75.5 KB |
| Object components & level/scene loading | `0x41a6d0`-`0x445b50` | ░░░░░░░░░░░░░░░░░░░░ 0% | 1 / 584 | 0.0 KB / 157.7 KB |
| Engine script commands (lighting/fog, memory heaps, script utils, arrays, music & streams) | `0x445b50`-`0x44e6b0` | ░░░░░░░░░░░░░░░░░░░░ 1% | 5 / 246 | 0.3 KB / 32.5 KB |
| Movies (Bink) & streamed audio | `0x44e6b0`-`0x456910` | ████░░░░░░░░░░░░░░░░ 20% | 40 / 277 | 6.3 KB / 30.3 KB |
| Low-level networking (engine net library) | `0x456910`-`0x45efd0` | ░░░░░░░░░░░░░░░░░░░░ 0% | 0 / 183 | 0.0 KB / 28.3 KB |
| Object system & script runtime (composite objects, events, object anims, particles, pathfinding, QB parser/executor) | `0x45efd0`-`0x47dc40` | ██████░░░░░░░░░░░░░░ 31% | 244 / 685 | 35.8 KB / 112.3 KB |
| Models / faces / textures for skaters (CAS) | `0x47dc40`-`0x490d80` | ░░░░░░░░░░░░░░░░░░░░ 1% | 7 / 523 | 0.7 KB / 67.5 KB |
| Renderer: textures, geometry, particles, 2D/3D screen | `0x490d80`-`0x4a4500` | ░░░░░░░░░░░░░░░░░░░░ 0% | 4 / 559 | 0.2 KB / 73.1 KB |
| Front-end UI: screen elements & text | `0x4a4500`-`0x4b2280` | ░░░░░░░░░░░░░░░░░░░░ 0% | 3 / 228 | 0.1 KB / 52.5 KB |
| Renderer: screen FX, image/texture formats, fonts, grass | `0x4b2280`-`0x4d6d40` | █░░░░░░░░░░░░░░░░░░░ 7% | 17 / 488 | 9.1 KB / 114.8 KB |
| Application main & game object rendering glue | `0x4d6d40`-`0x4e6b80` | ██░░░░░░░░░░░░░░░░░░ 14% | 55 / 202 | 8.9 KB / 59.5 KB |
| Goal & rail editors / skater camera | `0x4e6b80`-`0x500020` | ░░░░░░░░░░░░░░░░░░░░ 0% | 1 / 341 | 0.7 KB / 85.7 KB |
| Skater (physics, tricks, walking, stats, sticker tags) | `0x500020`-`0x533f20` | █░░░░░░░░░░░░░░░░░░░ 5% | 9 / 544 | 10.1 KB / 186.9 KB |
| Online & network game modes (CTF, servers, lobby, buddies, NAT negotiation) | `0x533f20`-`0x54ef60` | ░░░░░░░░░░░░░░░░░░░░ 2% | 4 / 457 | 2.3 KB / 91.9 KB |
| Menu input mapping | `0x54ef60`-`0x550040` | ░░░░░░░░░░░░░░░░░░░░ 0% | 1 / 20 | 0.0 KB / 4.0 KB |
| Goals & career (GoalManager, create-a-trick, trick objects, level launching) | `0x550040`-`0x56c920` | ░░░░░░░░░░░░░░░░░░░░ 0% | 0 / 666 | 0.0 KB / 103.8 KB |
| Time-of-day manager & world state | `0x56c920`-`0x577300` | ░░░░░░░░░░░░░░░░░░░░ 0% | 0 / 137 | 0.0 KB / 38.1 KB |
| Viewer / gaps / records / career save info | `0x577300`-`0x588180` | ░░░░░░░░░░░░░░░░░░░░ 0% | 0 / 304 | 0.0 KB / 60.7 KB |
| Park editor & graffiti | `0x588180`-`0x595030` | ░░░░░░░░░░░░░░░░░░░░ 0% | 1 / 289 | 0.1 KB / 47.3 KB |
| Lighting / FX / park memory | `0x595030`-`0x59f610` | ░░░░░░░░░░░░░░░░░░░░ 0% | 1 / 169 | 0.0 KB / 40.1 KB |
| Game script commands (skater queries, save/memcard, profiles, appearance, cutscenes, weather) | `0x59f610`-`0x5b6560` | ░░░░░░░░░░░░░░░░░░░░ 1% | 20 / 574 | 1.6 KB / 87.1 KB |
| File system (PIP/PRE archives) & skater memory heaps | `0x5b6560`-`0x5bdad0` | ████████████████████ 100% | 222 / 222 | 27.1 KB / 27.1 KB |
| Input devices (SIO) | `0x5bdad0`-`0x5be6f0` | ████████████████████ 100% | 25 / 25 | 2.8 KB / 2.8 KB |
| Game clock (Tmr) | `0x5be6f0`-`0x5beaae` | ████████████████████ 100% | 15 / 15 | 0.8 KB / 0.8 KB |
| Win32 platform layer (window, D3D9/DirectSound/DirectInput setup, registry options) | `0x5eef20`-`0x5f8130` | ░░░░░░░░░░░░░░░░░░░░ 4% | 3 / 91 | 0.7 KB / 17.4 KB |

## Not decompiled (libraries)

Linked as libraries in the final build rather than rewritten:

- Import thunks (Winsock / Direct3DCreate9) (`0x5beaae`-`0x5beb1a`): 18 functions, 0.1 KB
- D3DX9 static library (shader assembler/compiler, math helpers) (`0x5beb1a`-`0x5eef20`): 677 functions, 157.8 KB
- GameSpy SDK, MSVC C runtime, STL helpers and compiler-generated code after `0x5F8130` (see `docs/library_ranges.md`)

## PartyMod

Built in from source (`src/partymod/`, MIT). Its byte patches are not counted as progress; they turn into rewrites as the subsystems they touch are decompiled.
