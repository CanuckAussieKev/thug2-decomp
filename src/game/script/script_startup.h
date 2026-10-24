#pragma once

#include <cstdint>

// The script system's start-up, 0x005A5660-0x005A5F6F: the counts of the script functions, loading and unloading
// .qb files by name, loading every start-up .qb file (scripts\qdir.txt from qb_scripts.pre), and the pools of the
// animation and object classes made with the script pools, before the script functions are registered.
namespace Script {

// 0x005A5660 / 0x005A5670: the number of script functions (CFunctions, the table at 0x0067F3A0) and member
// functions (the names at 0x00681AD0).
uint32_t GetNumCFunctions();
uint32_t GetNumMemberFunctions();

// 0x005A5960: load the .qb file `filename` (LoadQB with `unused`, not temporary).
void LoadQBFile(const char* filename, int unused);
// 0x005A5990: define the .qb file already in memory (LoadQBData, not temporary).
void LoadQBFileData(const char* filename, const uint8_t* data, int unused, int useQbNames);
// 0x005A59B0: unload the .qb file whose name's checksum is `fileChecksum`.
void UnloadQBFile(uint32_t fileChecksum);

// 0x005A59D0: every .qb file listed in scripts\qdir.txt (one per line, from the "data\" in the path on) unloaded
// and loaded again with the permanent string cache on; qb_scripts.pre loaded on the top-down heap for it and
// unloaded after; then the global symbol debugger_names removed.
void LoadStartupQBFiles();
// 0x005A5B20: the same (the "All startup QB files" memory profile around it).
void LoadAllStartupQBFiles();

// 0x005A5E70: the script pools (CreateScriptPools), then the pools of CAnimCachePredictor (200),
// CBlendChannel (150), CBonedAnimController (150), CAnimLookup (175), sSpawnedPolyDetails (75) and
// Obj::CEventReceiverList (175), then the script functions and member functions registered.
void AllocatePools();

// 0x005A5B40, 0x005A5BD0, 0x005A5C50, 0x005A5CD0, 0x005A5D60, 0x005A5DF0: one pool creator per class (cdecl: the
// number of items, the pool's name), as the script pools' (Script::CreatePool).
void CreateAnimCachePredictorPool(int count, const char* name);
void CreateBlendChannelPool(int count, const char* name);
void CreateBonedAnimControllerPool(int count, const char* name);
void CreateAnimLookupPool(int count, const char* name);
void CreateSpawnedPolyDetailsPool(int count, const char* name);
void CreateEventReceiverListPool(int count, const char* name);

} // namespace Script
