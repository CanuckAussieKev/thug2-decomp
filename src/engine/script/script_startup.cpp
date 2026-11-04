// The script system's start-up, 0x005A5660-0x005A5F6F (see script_startup.h).

#include "engine/script/script_startup.h"

#include <cstdint>
#include <cstring>
#include <array>
#include <span>
#include <string_view>

#include "base/core/checksums.h"
#include "engine/memory/manager.h"
#include "engine/memory/mem.h"
#include "engine/memory/slab_bank.h"
#include "engine/file/packs.h"
#include "engine/gfx/frame_source.h"
#include "engine/gfx/anim_cache.h"
#include "engine/gfx/anim_track.h"
#include "engine/gfx/scene_load.h"
#include "engine/gfx/spawned_polys.h"
#include "engine/script/cell.h"
#include "engine/script/load.h"
#include "engine/script/names.h"
#include "engine/script/qb.h"
#include "engine/script/script_tables.h"
#include "engine/script/param_struct.h"
#include "base/core/checked_index.h"

namespace script {

namespace {
// Item sizes and counts of the pools made here.
constexpr int kBonedAnimControllerBytes = 0x28;
constexpr int kSpawnedPolyDetailsBytes = 0xB8;
constexpr int kEventReceiverListBytes = 0x14;
constexpr int kAnimCachePredictorCount = 200;
constexpr int kBlendChannelCount = 150;
constexpr int kBonedAnimControllerCount = 150;
constexpr int kAnimLookupCount = 175;
constexpr int kSpawnedPolyDetailsCount = 75;
constexpr int kEventReceiverListCount = 175;

constexpr uint32_t kNodeArray = 0xC472ECC5;     // "nodearray": looked up after each unload, the result unused
constexpr uint32_t kDebuggerNames = 0xC2F97BA7; // "debugger_names": removed once the start-up files are loaded

// Compiled-out debug output (the empty function 0x005480A0): a .qb file's name before it loads, and a call with
// nothing after.
inline void print_loading(const char* /*filename*/) {}
inline void print_done() {}

// The pool pairs of the classes AllocatePools makes pools for.
void create_pool_of(memory::PoolPair& pools, int item_size, int count, const char* name)
{
    create_pool(pools, item_size, count, name);
}

} // namespace

// 0x005A5660
uint32_t get_num_c_functions() { return static_cast<uint32_t>(kCFunctionCount); }
// 0x005A5670
uint32_t count_method_entries() { return static_cast<uint32_t>(kMemberFunctionNameCount); }

NativeCommand* find_c_function(const char* name)
{
    for (NativeCommand& entry : cfunction_table()) {
        if (std::strcmp(entry.name, name) == 0) {
            return &entry;
        }
    }
    return nullptr;
}

void bind_native_command(NativeCommand& entry, NativeFunction function) { entry.function = function; }

// 0x005A5960
void load_qb_file(const char* filename, int unused)
{
    print_loading(filename);
    load_qb(filename, unused, 0);
    print_done();
}

// 0x005A5990
void load_qb_file_data(const char* filename, std::span<uint8_t> data, int unused, int use_qb_names)
{
    load_qb_data(filename, data, unused, use_qb_names, 0);
}

// 0x005A59B0
void unload_qb_file(uint32_t file_checksum)
{
    unload_qb(file_checksum);
    fetch_list(kNodeArray);
}

// 0x005A59D0
void load_startup_qb_files()
{
    memory::Hub* manager = memory::Hub::get_pull();
    manager->enter_heap_scope(manager->top_down_heap());
    // (the reference build's name, 0x00657698; THUG2.exe has "qb_scripts.prx" there - the file layer opens a .pre
    // as .prx either way)
    packs::load_pre("qb_scripts.pre");
    memory::Hub::get_pull()->leave_heap_scope();
    const auto* text = static_cast<const char*>(packs::read_in("scripts\\qdir.txt", false, nullptr, nullptr, nullptr));
    const int size = packs::get_file_size("scripts\\qdir.txt");
    begin_permanent_strings();
    const std::string_view source(text, static_cast<size_t>(size));
    size_t position = 0;
    while (position < source.size()) {
        // One line (no length check, as in the original), and the character after it skipped.
        constexpr size_t kLineSize = 200;
        std::array<char, kLineSize> line{};
        int length = 0;
        while (position < source.size() && core::at(source, position) != '\r' && core::at(source, position) != '\n') {
            core::at(line, length++) = core::at(source, position++);
        }
        core::at(line, length) = '\0';
        ++position;
        if (core::at(line, 0) == '\0') {
            continue;
        }
        const std::string_view line_text(line.data());
        if (const size_t data = line_text.find("data\\"); data != std::string_view::npos) {
            const std::string_view rest = line_text.substr(data + 5);
            std::memmove(line.data(), rest.data(), rest.size() + 1);
        }
        unload_qb(checksums::hash_from_text(line.data()));
        fetch_list(kNodeArray);
        print_loading(line.data());
        load_qb(line.data(), 1, 0);
        print_done();
    }
    end_permanent_strings();
    packs::offload("scripts\\qdir.txt");
    packs::unload_pre("qb_scripts.pre");
    if (SymbolTableEntry* symbol = find_symbol(kDebuggerNames)) {
        clean_up_and_remove_symbol(symbol);
    }
}

// 0x005A5B20
void load_boot_script_files()
{
    memory::push_mem_profile("All startup QB files");
    load_startup_qb_files();
    memory::pop_mem_profile();
}

// 0x005A5B40 ... 0x005A5DF0
void create_anim_cache_predictor_pool(int count, const char* name)
{
    create_pool_of(gfx::anim_slot_pools(), static_cast<int>(sizeof(gfx::AnimCacheSlot)), count, name); // (0x1E4 in the original)
}
void create_blend_channel_pool(int count, const char* name)
{
    create_pool_of(gfx::anim_track_pools(), static_cast<int>(sizeof(gfx::AnimTrack)), count, name); // (0x74 in the original)
}
void create_boned_anim_controller_pool(int count, const char* name)
{
    create_pool_of(gfx::standard_pools(), kBonedAnimControllerBytes, count, name);
}
void create_anim_lookup_pool(int count, const char* name)
{
    create_pool_of(render::frame_source_pools(), sizeof(render::FrameSource), count, name);
}
void create_spawned_poly_details_pool(int count, const char* name)
{
    create_pool_of(render::spawned_poly_pools(), kSpawnedPolyDetailsBytes, count, name);
}
void create_event_receiver_list_pool(int count, const char* name)
{
    create_pool_of(objects::script_list_pools(), kEventReceiverListBytes, count, name);
}

// 0x005A5E70
void allocate_pools()
{
    memory::push_mem_profile("Script::AllocatePools");
    create_script_pools();
    memory::pop_mem_profile();
    memory::pop_mem_profile(); // (a second empty call)
    memory::push_mem_profile("CAnimCachePredictor");
    create_anim_cache_predictor_pool(kAnimCachePredictorCount, "CAnimCachePredictor");
    memory::pop_mem_profile();
    memory::push_mem_profile("CBlendChannel");
    create_blend_channel_pool(kBlendChannelCount, "CBlendChannel");
    memory::pop_mem_profile();
    memory::push_mem_profile("CBonedAnimController");
    create_boned_anim_controller_pool(kBonedAnimControllerCount, "CBonedAnimController");
    memory::pop_mem_profile();
    memory::push_mem_profile("CAnimLookup");
    create_anim_lookup_pool(kAnimLookupCount, "CAnimLookup");
    memory::pop_mem_profile();
    memory::push_mem_profile("sSpawnedPolyDetails");
    create_spawned_poly_details_pool(kSpawnedPolyDetailsCount, "sSpawnedPolyDetails");
    memory::pop_mem_profile();
    memory::push_mem_profile("Obj::CEventReceiverList");
    create_event_receiver_list_pool(kEventReceiverListCount, "Obj::CEventReceiverList");
    memory::pop_mem_profile();
    memory::push_mem_profile("Registering script functions");
    install_script_functions(std::span<const NativeCommand>(cfunction_table()).first(get_num_c_functions()));
    publish_method_table(std::span<const char* const>(member_function_names()).first(count_method_entries()));
    memory::pop_mem_profile();
}

} // namespace script
