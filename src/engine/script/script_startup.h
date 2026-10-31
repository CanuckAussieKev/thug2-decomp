#pragma once

#include <cstdint>

#include "engine/core/slab_bank.h"
#include "engine/gfx/scene_load.h"
#include "engine/gfx/spawned_polys.h"

// The script system's start-up, 0x005A5660-0x005A5F6F: the counts of the script functions, loading and unloading
// .qb files by name, loading every start-up .qb file (scripts\qdir.txt from qb_scripts.pre), and the pools of the
// animation and object classes made with the script pools, before the script functions are registered.
namespace gfx {
extern memory::PoolPair g_anim_slot_pools;  // 0x006F92B8 (CAnimCachePredictor, anim_cache.cpp)
extern memory::PoolPair g_standard_pools;   // 0x00701430 (CBonedAnimController, anim_controller.cpp)
extern memory::PoolPair g_anim_track_pools; // 0x006F92C8 (CBlendChannel, anim_track.cpp)
} // namespace gfx
namespace objects {
extern memory::PoolPair g_script_list_pools; // 0x006F0FC8 (CEventReceiverList, tracker.cpp)
} // namespace objects

namespace script {

// 0x005A5660 / 0x005A5670: the number of script functions (CFunctions, the table at 0x0067F3A0) and member
// functions (the names at 0x00681AD0).
uint32_t get_num_c_functions();
uint32_t count_method_entries();

// 0x005A5960: load the .qb file `filename` (LoadQB with `unused`, not temporary).
void load_qb_file(const char* filename, int unused);
// 0x005A5990: define the .qb file already in memory (load_qb_data, not temporary).
void load_qb_file_data(const char* filename, const uint8_t* data, int unused, int use_qb_names);
// 0x005A59B0: unload the .qb file whose name's checksum is `file_checksum`.
void unload_qb_file(uint32_t file_checksum);

// 0x005A59D0: every .qb file listed in scripts\qdir.txt (one per line, from the "data\" in the path on) unloaded
// and loaded again with the permanent string cache on; qb_scripts.pre loaded on the top-down heap for it and
// unloaded after; then the global symbol debugger_names removed.
void load_startup_qb_files();
// 0x005A5B20: the same (the "All startup QB files" memory profile around it).
void load_boot_script_files();

// 0x005A5E70: the script pools (CreateScriptPools), then the pools of CAnimCachePredictor (200),
// CBlendChannel (150), CBonedAnimController (150), CAnimLookup (175), sSpawnedPolyDetails (75) and
// GameObjects::CEventReceiverList (175), then the script functions and member functions registered.
void allocate_pools();

// 0x005A5B40, 0x005A5BD0, 0x005A5C50, 0x005A5CD0, 0x005A5D60, 0x005A5DF0: one pool creator per class (cdecl: the
// number of items, the pool's name), as the script pools' (Scripts::create_pool).
void create_anim_cache_predictor_pool(int count, const char* name);
void create_blend_channel_pool(int count, const char* name);
void create_boned_anim_controller_pool(int count, const char* name);
void create_anim_lookup_pool(int count, const char* name);
void create_spawned_poly_details_pool(int count, const char* name);
void create_event_receiver_list_pool(int count, const char* name);

} // namespace script
