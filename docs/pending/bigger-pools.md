# Bigger script pools (as THUGPRO's) - 2026-10-21

The user's decision after the Kyoto crash (docs/pending/kyoto-crash.md: `pool CComponent full`) and the lead's report of
THUG1 Moscow (ru) crashing the main install with `pool HashItem full: all 10000 items` (Lst::HashTable::PutItem writing
through the null item, during ru's load). NOT ORIGINAL: the counts are ours, set in one place, every one overridable.

## What THUGPRO does (read-only inspection, for insight)
- `THUGPro.exe` on disk: the main block 256 MB (0x10000000, Construct's push at 0x5BBCBF), the script region 6,613,800
  (0x64EB28), CComponent 92,000 etc.
- Its shipped `thugpro.dll` (file offset ~0x14920-0x14CB9: VirtualProtect + `mov [addr], imm` per immediate) then
  writes, over THUG2's own code: front-end region 0x5BBEE4 -> 8 MB, script region 0x5BBF77 -> 16 MB, skater info
  0x5BBFDF -> 16 MB, theme 0x5BC047 -> 0x8000, skater 0x5BC0AF -> 0x20000, skater geom 0x5BC14F -> 0x80000, net misc
  0x5BB5CD -> 0x40000, internet 0x5BB6CD -> 1 MB, PermanentTextureHeap 0x5BC1DE -> 0x40000, UnloadableTextureHeap
  0x5BC29D -> 2 MB; the CreateScriptPools pushes (table below); Main's HashItem push 0x4E211B -> 40,000; the engine
  start's argument 0x4E246B 350 -> 700 (0x0048C1D0 -> 0x0048D1C0; not identified, not taken).
- Morten1337's thugpro_proxy `Code/SkPro/Miscellaneous.cpp` has an older, commented-out version (CComponent 100,000).

## Ours (game/core/memory_sizes.h / .cpp; credit in the comments: "pool sizes as THUGPRO's")

| pool (item bytes) | original | ours (THUGPRO's) | ini `[memory]` key |
|---|---|---|---|
| CComponent (16) | 86,000 | 184,000 | component_pool |
| CStruct (8) | 19,800 | 49,600 | struct_pool |
| CVector (16) | 9,500 | 19,000 | vector_pool |
| CPair (12) | 800 | 1,600 | pair_pool |
| CArray (12) | 7,000 | 16,000 | array_pool |
| CSymbolTableEntry (20) | 12,500 | 45,000 | symbol_pool |
| CScript (244) | 300 | 1,200 | script_pool |
| CScriptCacheEntry (20) | 300 | 1,200 | script_cache_pool |
| CStoredRandom (96) | 100 | 100 (THUGPRO leaves it) | - |
| permanent string heap | 190,000 bytes / 7,000 | 1 MB / 28,000 | string_heap / string_count |
| HashItem (12, Main's PoolManager::SetUp) | 10,000 | 40,000 | hash_item_pool |

`[memory] original_sizes=1` puts all of them (and the heaps) back to the original's. Values are clamped to 1..4,000,000.

**Memory:** the script pools + string heap take 2,364,800 bytes originally, 6,354,976 now (+3,990,176). They come from
the script region, which can't hold that (0x434770 = 4,409,200 bytes, already holding the original pools and the
scripts), so the script region grows by exactly the pools' growth + 4 KB of block headers: **8,403,472 bytes**
(InitStandardHeaps, ours). The script region and the HashItem pool (+360,000, bottom-up heap) come out of the main
block, so the main block grows by the same (rounded to 64 KB): **64 MB -> 71,499,776 bytes** (Mem::Manager::Construct,
ours) - every other heap keeps exactly the room it had; the scripts keep the original's headroom. THUGPRO's other heap
changes (front end, skater info, textures, network, its 256 MB main block) aren't taken: nothing here needed them.

Where it's used: `Script::CreateScriptPools` (qb.cpp), `Mem::Manager::Construct` / `InitStandardHeaps` (manager.cpp),
`Sys::Main` (main.cpp, HashItem). Readers of a pool's size read `m_count` (the level editor's budget meter, the park
editor's memory figures, CFunc memory dumps) - nothing assumed 86,000; the budget meter's tooltips now say "original
86000" and the ini key. Note: the park editor's limits come from the pools too, so bigger parks fit (as in THUGPRO).

Also (log only): `Mem::Pool::Allocate` logs `pool <name> 80% full: <n> of <count>` once per pool (pools of 50+ items) -
which pools a level comes near, next to the existing `pool <name> full`.

## Every pool in the game (callers of Pool::Pool 0x005BA420 / PoolManager::SetUp)
Script pools and HashItem above. The rest (unchanged, also unchanged by THUGPRO): CAnimCachePredictor 200 x 0x1E4,
CBlendChannel 150 x 0x74, CBonedAnimController 150 x 0x28, CAnimLookup 175 x 0x214, sSpawnedPolyDetails 75 x 0xB8,
Obj::CEventReceiverList 175 x 0x14 (0x005A5B20's six, at start-up); CEditedRailPoint 400 x 0x28 / CEditedRail 200 x 0x10
(rail editor, 0x004F7CB3); the park editor's CMapListNode 16,000, CClipboardEntry 1,000, CClipboard 6; the park
generator's "%s RailPoint" / "%s RailString" (0x0058B0C0, counts from its caller). None of them logged 80 % in any run
below (they aren't sized by a level's node array).

## Verified
- Build (out/build/verify, own PDB endpoint), no warnings. thug2_check.exe on THUG2PM.exe and on
  C:\Games\THUG2\Game\THUG2.exe: early self-tests all passed (513), 0 mismatched. check_code_writes: no overlaps.
- New self-tests (src/hook/selftests_memory_sizes.cpp): TestOriginalPoolSizes (Early: our table of the original's
  counts equals the exe's push immediates, the original sizes give 0x434770 / 64 MB, THUGPRO's counts, the region and
  block grow by at least the pools' growth); TestPoolSizesInGame (EngineStarted: the live CComponent pool and script
  region are this run's sizes). TestMain and TestMemoryManagerHeapCreation compare at the original sizes
  (`Mem::Sizes::UseOriginal`). Mutations (out/scripts/mut_pools.ts): 10 of 10 caught.
- Game, C:\Games\THUG2-1 under its lock (ru and ny reconverted with this branch's = main's THUG1 converter, Kyoto
  = Z_JA with main's THAW converter, make_menu_level), `-level`, 60 s each:
  - original sizes (`original_sizes=1`): ru: CSymbolTableEntry, CStruct, CComponent and **HashItem** past 80 %
    (HashItem 8,000 of 10,000 from a fresh `-level` start; the main install crashed going there through the menus);
    ny: the same plus CArray; kyoto: CSymbolTableEntry, CStruct, CComponent.
  - ours: log `script pools: CComponent 184000, ... script region 8403472 bytes`; ru, ny, kyoto loaded, responding,
    no EXCEPTION, no pool at 80 %; engine-started self-tests (3) passed.
- NOT tried: loading through the menus as the main install did, the park editor with the bigger pools, online.

## To switch off
`[memory] original_sizes=1` (or single keys). The code: game/core/memory_sizes.cpp's defaults.
