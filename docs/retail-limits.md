# The retail-limits DLL: the original game's limits raised for large levels

Levels ported from other games (THUG1's Moscow and New York, THAW's Kyoto, Project 8's town, Proving Ground's world)
are bigger than anything THUG2 shipped with: more script objects than its pools hold, more named nodes than its node
name table, more rails than its rail name table. thug2-decomp.exe raises those limits in its own source
(`src/engine/memory/memory_sizes.cpp`, `src/engine/script/node_manager.cpp`, `src/engine/objects/rail_manager.cpp`).
The original game can have them raised too: `src/retail_limits` builds a small DLL that changes the limits in the
original THUG2.exe while it runs. Nothing on disk is changed; take the DLL out and the game is as it was.

## Installing

Build the x86 configuration (the original game is 32-bit; the DLL is built only there - a Release build for players):
the build writes `retail_limits\dinput8.dll` in the build folder. Copy `dinput8.dll` next to THUG2.exe. The next start
writes `thug2-limits.ini` (the settings) and `thug2-limits.log` (what was found and changed) beside it. The DLL needs no
other DLL (its C runtime is linked in).

**Another dinput8.dll already there** (some mods are one): rename that one (e.g. `dinput8_mod.dll`), put this one in
its place and set `[loader] chain=dinput8_mod.dll`: the game's DirectInput goes through the renamed DLL, which is loaded
at the game's first DirectInput call.

**Why a dinput8.dll:** the game imports DirectInput's dinput8.dll, so Windows loads the one in the game's folder before
any of the game's code runs - the ordinary way such a DLL is loaded. A launcher that starts the game and puts a DLL
into it would have to write into another process's memory, which security software watches for (and may stop); this
way the game starts exactly as it always does.

## How the exe is recognised

Every place the DLL changes, and every game function it calls, is checked first against the bytes the original game
has there. If any differs (another build of the exe, a patched one, another program that loads the DLL), nothing at
all is changed, the log says which site differed, and the game runs as it is. It recognises the two builds of
THUG2.exe it was checked against (they have the same bytes at every site). `thug2-limits-check.exe` (built next to
the DLL) runs the same check against exe files without starting the game:

```
out\build\<preset>\retail_limits\thug2-limits-check.exe C:\Games\THUG2\Game\THUG2.exe
```

It lists every change it would make, checks that a changed or different exe is refused, and tests the node name
table's code against stand-in node arrays.

## What is raised

The values are in `src/retail_limits/limits.h` (named constants, the original's beside ours - tools that check a level
against the original game's limits can mirror them). They are the same as thug2-decomp.exe's own defaults, so a level
that loads in thug2-decomp.exe fits in the original game with the DLL.

| Limit | Original | With the DLL | ini key (`[limits]`) |
|---|---|---|---|
| CScriptCacheEntry pool | 300 | 1,200 | `script_pools` |
| CComponent pool | 86,000 | 360,000 | `script_pools` |
| CStruct pool | 19,800 | 64,000 | `script_pools` |
| CVector pool | 9,500 | 48,000 | `script_pools` |
| CPair pool | 800 | 1,600 | `script_pools` |
| CArray pool | 7,000 | 26,000 | `script_pools` |
| CSymbolTableEntry pool | 12,500 | 45,000 | `script_pools` |
| CScript pool | 300 | 1,200 | `script_pools` |
| permanent string table | 190,000 bytes, 7,000 entries | 1 MB, 28,000 entries | `script_pools` |
| HashItem pool | 10,000 | 56,000 | `hash_items` |
| script heap beside the pools | ~2 MB | 16 MB more | `script_heap` |
| script region | 4,409,200 bytes | 28,695,888 bytes | (follows the three above) |
| main block | 64 MB | 92,078,080 bytes (87.8 MB) | (follows all of them) |
| node name hash | 16,384 slots, 16-bit node indexes; a full table hangs the load | 65,536 slots, 32-bit indexes; a node past a full table is left out and logged | `node_hash` |
| rail name table | 16,384 slots; more rail nodes hang the load | 65,536 slots, up to 32,767 rail nodes | `rail_table` |

`[limits] enabled=0` changes nothing. The script region grows by exactly what the bigger pools and the extra heap take,
and the main block by exactly what the region, the HashItem pool and the bigger rail table take (rounded up to 64 KB),
so every other heap keeps the room it had.

How each is done:

- **Pools and heaps:** the original passes each size as an immediate operand (the pool's count pushed for its
  constructor, the region's and the main block's sizes); the DLL writes the bigger number there. The approach is
  THUGPRO's, whose DLL writes bigger pool counts over the same instructions.
- **Rail name table** (CRailManager::AddFromNodeArray): made and freed during the rails' set-up; its size, its fill
  count and its four masks are immediates. Its slots stay 16 bits, so a level may have up to 32,767 rail nodes.
- **Node name hash:** the original keeps it inside the node manager (its first 64 KB, the walk's place right after), so
  it can't grow there. The DLL keeps a bigger one and the eight functions that touch the slots or the walk (the
  constructor, HashNode, UnhashNodeArray, GetNode, StartWalk, EndWalk, NextNode, RemoveNodeArray) jump to the DLL's
  (`src/retail_limits/node_hash.h`), which do what the originals do with no index cut to 16 bits, and never loop for
  ever on a full table.

## What it doesn't do

- It doesn't put levels into the game's menus or make the game read thug2-decomp's data overlay: a ported level for the
  original game has to be packed the way the original game reads its levels.
- Other limits of the original (its texture heaps, the park editor's, the network's) are left as they are.
