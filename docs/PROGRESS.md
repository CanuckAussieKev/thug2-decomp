# Decompilation progress

Generated 2026-10-16 by `tools/progress_report.py` - regenerate after each batch.

**Game code: ░░░░░░░░░░░░░░░░░░░░ 3.2%** of the code rewritten (426 of 8,365 functions, 52.4 KB of 1,616.9 KB).

Progress is by code size: function counts make a 10-byte helper look like as much work as a 2 KB routine. Subsystem boundaries come from the exe map (`docs/subsystems.md`) and are approximate (give or take a few functions); totals are exact.

| Subsystem | Range | Progress (code size) | Functions | Code |
|---|---|---|---:|---:|
| Core utilities (CRC/checksums, string helpers, rect, random) | `0x401000`-`0x406460` | ███████████████████░ 99% | 127 / 130 | 19.7 KB / 19.8 KB |
| Animation & skeleton / skinned model data | `0x406460`-`0x41a6d0` | ░░░░░░░░░░░░░░░░░░░░ 0% | 4 / 423 | 0.3 KB / 75.5 KB |
| Object components & level/scene loading | `0x41a6d0`-`0x445b50` | ░░░░░░░░░░░░░░░░░░░░ 0% | 0 / 584 | 0.0 KB / 157.7 KB |
| Engine script commands (lighting/fog, memory heaps, script utils, arrays, music & streams) | `0x445b50`-`0x44e6b0` | ░░░░░░░░░░░░░░░░░░░░ 0% | 2 / 246 | 0.1 KB / 32.5 KB |
| Movies (Bink) & streamed audio | `0x44e6b0`-`0x456910` | ░░░░░░░░░░░░░░░░░░░░ 0% | 0 / 277 | 0.0 KB / 30.3 KB |
| Low-level networking (engine net library) | `0x456910`-`0x45efd0` | ░░░░░░░░░░░░░░░░░░░░ 0% | 0 / 183 | 0.0 KB / 28.3 KB |
| Object system & script runtime (composite objects, events, object anims, particles, pathfinding, QB parser/executor) | `0x45efd0`-`0x47dc40` | █░░░░░░░░░░░░░░░░░░░ 7% | 86 / 685 | 8.4 KB / 112.3 KB |
| Models / faces / textures for skaters (CAS) | `0x47dc40`-`0x490d80` | ░░░░░░░░░░░░░░░░░░░░ 0% | 0 / 523 | 0.0 KB / 67.5 KB |
| Renderer: textures, geometry, particles, 2D/3D screen | `0x490d80`-`0x4a4500` | ░░░░░░░░░░░░░░░░░░░░ 0% | 2 / 559 | 0.2 KB / 73.1 KB |
| Front-end UI: screen elements & text | `0x4a4500`-`0x4b2280` | ░░░░░░░░░░░░░░░░░░░░ 0% | 0 / 228 | 0.0 KB / 52.5 KB |
| Renderer: screen FX, image/texture formats, fonts, grass | `0x4b2280`-`0x4d6d40` | ░░░░░░░░░░░░░░░░░░░░ 0% | 0 / 488 | 0.0 KB / 114.8 KB |
| Application main & game object rendering glue | `0x4d6d40`-`0x4e6b80` | ░░░░░░░░░░░░░░░░░░░░ 0% | 1 / 202 | 0.0 KB / 59.5 KB |
| Goal & rail editors / skater camera | `0x4e6b80`-`0x500020` | ░░░░░░░░░░░░░░░░░░░░ 0% | 0 / 341 | 0.0 KB / 85.7 KB |
| Skater (physics, tricks, walking, stats, sticker tags) | `0x500020`-`0x533f20` | ░░░░░░░░░░░░░░░░░░░░ 0% | 0 / 544 | 0.0 KB / 186.9 KB |
| Online & network game modes (CTF, servers, lobby, buddies, NAT negotiation) | `0x533f20`-`0x54ef60` | ░░░░░░░░░░░░░░░░░░░░ 0% | 1 / 457 | 0.4 KB / 91.9 KB |
| Menu input mapping | `0x54ef60`-`0x550040` | ░░░░░░░░░░░░░░░░░░░░ 0% | 0 / 20 | 0.0 KB / 4.0 KB |
| Goals & career (GoalManager, create-a-trick, trick objects, level launching) | `0x550040`-`0x56c920` | ░░░░░░░░░░░░░░░░░░░░ 0% | 0 / 666 | 0.0 KB / 103.8 KB |
| Time-of-day manager & world state | `0x56c920`-`0x577300` | ░░░░░░░░░░░░░░░░░░░░ 0% | 0 / 137 | 0.0 KB / 38.1 KB |
| Viewer / gaps / records / career save info | `0x577300`-`0x588180` | ░░░░░░░░░░░░░░░░░░░░ 0% | 0 / 304 | 0.0 KB / 60.7 KB |
| Park editor & graffiti | `0x588180`-`0x595030` | ░░░░░░░░░░░░░░░░░░░░ 0% | 1 / 289 | 0.1 KB / 47.3 KB |
| Lighting / FX / park memory | `0x595030`-`0x59f610` | ░░░░░░░░░░░░░░░░░░░░ 0% | 0 / 169 | 0.0 KB / 40.1 KB |
| Game script commands (skater queries, save/memcard, profiles, appearance, cutscenes, weather) | `0x59f610`-`0x5b6560` | ░░░░░░░░░░░░░░░░░░░░ 0% | 18 / 574 | 0.4 KB / 87.1 KB |
| File system (PIP/PRE archives) & skater memory heaps | `0x5b6560`-`0x5bdad0` | ████████████████░░░░ 83% | 184 / 222 | 22.6 KB / 27.1 KB |
| Input devices (SIO) | `0x5bdad0`-`0x5be670` | ░░░░░░░░░░░░░░░░░░░░ 0% | 0 / 23 | 0.0 KB / 2.7 KB |
| Win32 platform layer (window, D3D9/DirectSound/DirectInput setup, registry options) | `0x5eef20`-`0x5f8130` | ░░░░░░░░░░░░░░░░░░░░ 0% | 0 / 91 | 0.0 KB / 17.4 KB |

## Not decompiled (libraries)

Linked as libraries in the final build rather than rewritten:

- D3DX9 static library (shader assembler/compiler, math helpers) (`0x5be670`-`0x5eef20`): 712 functions, 158.8 KB
- GameSpy SDK, MSVC C runtime, STL helpers and compiler-generated code after `0x5F8130` (see `docs/library_ranges.md`)

## PartyMod

Built in from source (`src/partymod/`, MIT). Its byte patches are not counted as progress; they turn into rewrites as the subsystems they touch are decompiled.
