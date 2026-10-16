# Script cache (worktree wt/scache)

- [x] Batch (NOT run in the game yet; early test passes offline): the script cache (src/game/script/cache.*), the
      whole class. Script::CScriptCache (0x24 bytes, an Mdl::Module: vtable 0x648468 {Destroy 0x4761E0, Start and
      Stop = the empty 0x5480A0}; +0x14 the entry table (Lst::HashTable, 8 bits, on the script heap), +0x18 / +0x1C
      the newest / oldest unused entry, +0x20 "free unused entries at once" - only ever cleared) and
      CScriptCacheEntry (0x14: name, code, usage, older, newer; pool 0x6F3560). Functions: CreateScriptCache 0x476040
      / ReleaseScriptCache 0x475A00 (the singleton 0x6F3558 with references 0x6F355C), Construct 0x475DD0,
      DestroyBody 0x476090, Destroy 0x4761E0, GetScript 0x475E80, ReleaseScript 0x475D60, Invalidate 0x475FD0,
      AddToUnused 0x475A40, RemoveFromUnused 0x475A70, DeleteEntry 0x475B60, MakeRoom 0x475BD0, the entry's
      operator new 0x475AC0 / delete 0x475B00 (the pool pair), and Lst::HashTable::GetItem(const key&, bool)
      0x462050 (the non-inlined GetItem; second argument always 1, unused). 15 functions.
      Callers switched to ours: RemoveFromScriptCache (qb.cpp) now calls CScriptCache::Invalidate (ScriptCache()
      returns CScriptCache*); Sys::Main creates/releases the cache with CreateScriptCache / ReleaseScriptCache (and
      selftests_misc.cpp's Main test maps 0x476040 / 0x475A00 to them).
      Conventions: everything on the cache is thiscall and callee-pops (GetScript, ReleaseScript, Invalidate,
      AddToUnused, RemoveFromUnused, DeleteEntry RET 4; MakeRoom RET 8, its second argument unused); Construct and
      DestroyBody take ECX only; new/delete and Create/Release are cdecl. Still the game's: Mdl::Module's
      constructor / destructor body 0x44F7E0 / 0x44F790 (also used by File::AsyncLoaderModule).
      Early test (src/hook/selftests_scache.cpp, TestScriptCache): a stand-in manager with a default heap and a
      192 KB script heap in a 256 KB arena, two entry pools (120 and 30), a stand-in symbol table (a C function -
      pre-processing rewrites calls to it -, compressed and stored scripts, a name naming a script, an integer) and
      LZSS data made by the test; original vs ours: references with and without a cache, uses/releases through
      the unused list (middle and both ends), invalidation of an unused and of a used script (new version), the
      table lookup, five ~40 KB scripts dropping the oldest unused entries for heap room, 110 small ones hitting
      the 100-entry limit, the other pool current, free-at-once, loose and pooled entry new/delete, destruction
      through the vtable (slot 0 jumps to ours for our run), and two scripts sized to the margin (the largest
      free block less 128 bytes, and one byte more). Results, the cache's lists, pool counts and the
      whole arena compared. 40 deliberate bugs caught, 1 after adding the margin-sized scripts (14 of them by
      crashing inside TestScriptCache, 1 by hanging).
      Checks: builds without warnings; thug2_check 0 MISMATCHED (722 replacements, 15 not checked - none new),
      early 85/85; register scan only the known 0x4EFF92 false alarm (0x475E80 and 0x462050 return values: yes);
      call audit no MISMATCH; check_partymod_overlap "No overlaps"; find_gaps: nothing left in 0x475A00-0x476210,
      but registering 0x462050 makes 0x4620A0-0x462710 (object/event CFunctions, not cache) show as a gap; the one
      thunk (0x48C1A0 -> SetLetterbox) was there before.

## What the cache does (script runtime semantics - for the QuickJS port)
- Scripts live in the symbol table compressed ({checksum, size, stored size, code}, LZSS when stored < size).
  A script only runs from the cache: GetScript decompresses it (into the script heap) and pre-processes it
  (PreProcessScript: statement-start names of C functions / member functions are turned into direct calls), once;
  later uses share that code.
- Use counting: every GetScript takes a use, ReleaseScript gives one back. CScript (0x473A80, its clear/reset)
  releases its current script (+0xC8) and every script on its call stack (frames +0x94, 0x30 bytes each, count
  +0x30) - so a script is "in use" while it runs or waits on a script it called. Other users: 0x4E4F50 (a recursive
  "does script X (or a script it calls) do Y" scan - gets then releases), 0x474540, 0x474B40, 0x475750,
  0x45C950 (not rewritten yet).
- A name naming another symbol is followed; the cache key is the script's own name (so an alias shares the
  entry). Names that aren't scripts give null (no entry). Release looks the name up directly (no alias step).
- Unused code stays cached (LRU list, newest first) and is only dropped when room is needed for another script:
  while the script heap's largest free block - 128 is smaller than the new script, and while 100+ entries of the
  current entry pool are in use. Nothing guards running out of unused entries while the heap is still too full:
  the original then dereferences null.
- Reloading a script (CreateScriptSymbol, the pending symbol changes in ProcessPendingSymbols) calls Invalidate:
  the cached copy is freed and the new version decompressed right away, with the old use count. The old code is
  gone even though scripts may still be running it; the loader restarts those (RestartChangedScripts). A script
  that was unused ends up with a count of 0 but not on the unused list, so it's never dropped again (quirk).
  Invalidating a name whose symbol is no longer a script would crash (GetItem null) - it's only called while the
  symbol is still a script.
- The cache is created at start-up (Sys::Main) as an engine module (registered, started - its Start does
  nothing) and released at shutdown; it isn't unregistered.

## Left in 0x475000-0x476FFF (all CScript / script manager, not the cache)
0x475050, 0x4750B0 (38 callers), 0x4752F0, 0x4753C0, 0x475420 (spawn script, 36 callers), 0x475670, 0x4756C0,
0x4756F0, 0x475700, 0x475750 (CScript: run a cached script - GetScript then 0x4748E0), 0x475790 (RunScript, 505
callers), 0x4758F0, 0x475920 (the script manager's update: runs every script, flags 0x6F3544 / 0x6F3545). These
belong with the CScript batch.
