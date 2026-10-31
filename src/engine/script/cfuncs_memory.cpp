// Script commands of the engine, 0x00447FF0-0x00448840: the memory manager's (profiling, heap contexts, named
// heaps, reports) and the pool-usage dump (see cfuncs_engine.h).

#include "engine/script/cfuncs_engine.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "engine/core/arena_alloc.h"
#include "engine/core/checksums.h"
#include "engine/core/dbg.h"
#include "engine/core/dump_text.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/core/slab_bank.h"
#include "engine/core/stubs.h"
#include "engine/gfx/engine_glue.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "engine/script/script_startup.h"
#include "engine/script/cfuncs_scene.h"

namespace commands {

using script::Script;
using script::Struct;

namespace {

constexpr uint32_t kOff = 0xD443A2BC;            // "off"
constexpr uint32_t kName = 0xA1DC81F9;           // "name"
constexpr uint32_t kSize = 0x083FDB95;           // "size"
constexpr uint32_t kBottomUpHeap = 0x0C7800B0;   // "BottomUpHeap"
constexpr uint32_t kDumpHeapsBytes = 0xD65B9AC7; // (name unknown) DumpHeaps: sizes in bytes rather than K

// Compiled out of this build (calls of the empty function 0x005480A0).
inline void set_mem_thread_safe(bool /*safe*/) {}
inline void dump_fragments(int /*size*/, bool /*kilobytes*/) {}
inline void analyze_heap(memory::Arena* /*heap*/) {}
inline void print_mem_info(const char* /*heapName*/) {}
inline void dump_heaps_begin() {}

memory::Hub* hub() { return memory::Hub::get_pull(); }

// The heap a command names: name="<text>" or name=<checksum>.
uint32_t get_heap_name(Struct* params)
{
    const char* text;
    uint32_t name = 0; // (uninitialised in the original: asserted)
    if (params->fetch_text(kName, &text, 0))
        name = checksums::hash_from_text(text);
    else
        params->read_hash_value(kName, &name, 1);
    return name;
}

// The sound manager's free memory (0x0047AE10 on 0x006F85A8: `xor eax, eax; ret`, folded with stubs::return_zero -
// 0 in this build).
int free_sound_memory() { return stubs::return_zero(); }

} // namespace

// 0x00447FF0: PushMemProfile "<name>" (compiled out). Always true.
bool __cdecl cfunc_push_mem_profile(Struct* params, Script*)
{
    const char* name = nullptr;
    params->fetch_text(0u, &name, 1);
    memory::push_mem_profile(name);
    return true;
}

// 0x00448020: PopMemProfile (compiled out). Always true.
bool __cdecl cfunc_pop_mem_profile(Struct*, Script*)
{
    memory::pop_mem_profile();
    return true;
}

// 0x00448030: SetMemThreadSafe [off] (compiled out). Always true.
bool __cdecl cfunc_set_mem_thread_safe(Struct* params, Script*)
{
    if (params->has_bare_flag(kOff))
        set_mem_thread_safe(false);
    else
        set_mem_thread_safe(true);
    return true;
}

// 0x00448060: DumpFragments [<size> (0x3C00)] [K] (compiled out). Always true.
bool __cdecl cfunc_dump_fragments(Struct* params, Script*)
{
    int size = 0x3C00;
    params->fetch_int_value(0u, &size, 0);
    dump_fragments(size, params->has_bare_flag("K"));
    return true;
}

// 0x00448090: MemViewToggle. Always true.
bool __cdecl cfunc_mem_view_toggle(Struct*, Script*)
{
    render::toggle_mem_view();
    return true;
}

// 0x004480A0: GotExtraMemory: whether the machine has the extra memory (0x007CEFAC).
bool __cdecl cfunc_got_extra_memory(Struct*, Script*) { return g_got_extra_memory != 0; }

// 0x004480B0: MemPushContext <heap name> | 0 (the bottom-up heap; another number does nothing): allocations go to
// that heap until MemPopContext. Always true.
bool __cdecl cfunc_mem_push_context(Struct* params, Script*)
{
    int number = 0;
    if (params->fetch_int_value(0u, &number, 0)) {
        if (number == 0)
            hub()->enter_heap_scope(hub()->bottom_up_heap_);
        return true;
    }
    uint32_t name = 0; // (uninitialised in the original: asserted)
    params->read_hash_value(0u, &name, 1);
    hub()->enter_heap_scope(hub()->get_memory_pool(name));
    return true;
}

// 0x00448130: MemPopContext. Always true.
bool __cdecl cfunc_mem_pop_context(Struct*, Script*)
{
    hub()->leave_heap_scope();
    return true;
}

// 0x00448140: MemInitHeap name="<text>" size=<bytes>: a named heap of its own region. Always true.
bool __cdecl cfunc_mem_init_heap(Struct* params, Script*)
{
    const char* text = nullptr; // (both asserted; uninitialised in the original)
    int size = 0;
    params->fetch_text(kName, &text, 1);
    params->fetch_int_value(kSize, &size, 1);
    hub()->create_labeled_zone(checksums::hash_from_text(text), static_cast<uint32_t>(size), text, 0);
    return true;
}

// 0x004481A0: MemDeleteHeap name=...: whether the named heap was there (and is deleted).
bool __cdecl cfunc_mem_delete_heap(Struct* params, Script*)
{
    return hub()->release_labeled_zone(get_heap_name(params), 0);
}

// 0x00448200: MemHeapExists name=...: whether there is such a named heap.
bool __cdecl cfunc_mem_heap_exists(Struct* params, Script*)
{
    return hub()->fetch_heap_by_key(get_heap_name(params), 0) != nullptr;
}

// 0x00448270: AnalyzeHeap [<heap name> (BottomUpHeap)] (compiled out; params may be null). Always true.
bool __cdecl cfunc_analyze_heap(Struct* params, Script*)
{
    uint32_t name = kBottomUpHeap;
    if (params)
        params->read_hash_value(0u, &name, 0);
    if (memory::Arena* heap = hub()->get_memory_pool(name))
        analyze_heap(heap);
    return true;
}

// 0x004482C0: PrintMemInfo [<heap name> (BottomUpHeap)]: the heap looked up and its name copied for a compiled-out
// report (params may be null). Always true.
bool __cdecl cfunc_print_mem_info(Struct* params, Script*)
{
    uint32_t name = kBottomUpHeap;
    if (params)
        params->read_hash_value(0u, &name, 0);
    hub()->get_memory_pool(name);
    char heap_name[64];
    std::snprintf(heap_name, sizeof(heap_name), "%s",
                  hub()->get_zone_label(name)); // (the original's copy is unbounded)
    print_mem_info(heap_name);
    return true;
}

// 0x00448330 (the flag in BL; dump_pool_usage_stub): the pools' usage (used of size, the most used) - the script
// system's, the hash items', the animation system's, spawned polygons', event receiver lists' - written to the dump
// file (memory::dump_text) and / or printed.
void __cdecl dump_pool_usage(bool to_dump_file, bool print)
{
    char line[256];
    auto emit = [&]() {
        if (to_dump_file)
            memory::dump_text(line);
        if (print)
            debug::printf(line); // (the line as the format, as in the original)
    };
    // (the texts are the exe's)
    auto text = [&](const char* message) {
        std::snprintf(line, sizeof(line), message);
        emit();
    };
    auto pool = [&](const char* format, const memory::SlabBank* p) {
        std::snprintf(line, sizeof(line), format, p->taken_count_, p->num_, p->peak_used_);
        emit();
    };
    auto current = [](const memory::PoolPair& pools) {
        return pools.pools[pools.current]; // (not checked: the pools are set up by then)
    };
    text("\nScript pool usage:\n"); // "\nScript pool usage:\n"
    pool("CStruct:           %5d of %5d, max %5d\n",
         current(script::g_struct_pools)); // " | CStruct:           %5d of %5d, max %5d\n"
    pool("CComponent:        %5d of %5d, max %5d\n", current(script::g_component_pools));       // CComponent
    pool("CSymbolTableEntry: %5d of %5d, max %5d\n", current(script::g_symbol_pools));          // CSymbolTableEntry
    pool("CVector:           %5d of %5d, max %5d\n", current(script::g_vector_pools));          // CVector
    pool("CPair:             %5d of %5d, max %5d\n", current(script::g_pair_pools));            // CPair
    pool("CArray:            %5d of %5d, max %5d\n", current(script::g_array_pools));           // CArray
    pool("CScript:           %5d of %5d, max %5d\n", current(script::g_script_pools));          // CScript
    pool("CHashItem:\t%5d of %5d, max %5d\n", memory::slot_pool_setup::get_pool(0));            // CHashItem
    pool("CAnimCachePredictor:\t%5d of %5d, max %5d\n", current(gfx::g_anim_slot_pools));       // CAnimCachePredictor
    pool("CBlendChannel:\t%5d of %5d, max %5d\n", current(gfx::g_anim_track_pools));            // CBlendChannel
    pool("CBonedAnimController:\t%5d of %5d, max %5d\n", current(gfx::g_standard_pools));       // CBonedAnimController
    pool("CAnimLookup:\t%5d of %5d, max %5d\n", current(render::g_frame_source_pools));         // CAnimLookup
    pool("sSpawnedPolyDetails:\t%5d of %5d, max %5d\n", current(render::g_spawned_poly_pools)); // sSpawnedPolyDetails
    pool("Obj::CEventReceiverList:\t%5d of %5d, max %5d\n",
         current(objects::g_script_list_pools)); // GameObjects::CEventReceiverList
    text(
        "\n(HashItems are init in main.cpp, others are in init.cpp)\n\n\n"); // "\n(HashItems are init in main.cpp, others are in init.cpp)\n\n\n"
}

// 0x00448840: DumpHeaps [<flag>]: the current heap, then each heap's used / fragmented / free / minimum free sizes and
// block count (in K, or in bytes with the flag), the sound memory free and the pools' usage - printed (params may be
// null). Always true.
bool __cdecl cfunc_dump_heaps(Struct* params, Script*)
{
    auto format = [](const char* text) { return text; }; // (the exe's texts)
    dump_heaps_begin();
    memory::Hub* manager = hub();
    debug::printf(format("Current Heap: %s\n\n"), manager->current_heap_name()); // "Current Heap: %s\n"
    bool bytes = params && params->has_bare_flag(kDumpHeapsBytes);
    debug::printf(format(
        bytes
            ? "Name                       Used    Frag    Free    Min Blocks\n"
            : "Name                       Used  Frag  Free   Min  Blocks\n")); // "Name                       Used ..." (two layouts)
    debug::printf(format(
        bytes ? "------------------------- ------ ------- ------- ------ ------\n"
              : "------------------------- ----- ----- ---- ------ ------\n")); // "------------------------- -----..."
    for (memory::MemSource* heap = manager->first_zone(); heap; heap = manager->following_zone(heap)) {
        memory::Zone* region = heap->region_;
        int minimum = region->min_available_; // (read before free_byte_count)
        int blocks = heap->used_blocks_;
        int available = region->free_byte_count();
        if (bytes)
            debug::printf(format("%23s: %8d %7d %7d %7d  %5d \n"), heap->label_, heap->used_size_, heap->free_size_,
                          available, minimum,
                          blocks); // "%23s: %8d %7d %7d %7d  %5d \n"
        else
            debug::printf(format("%23s: %5dK %4dK %4dK %4dK  %5d \n"), heap->label_, heap->used_size_ / 1024,
                          heap->free_size_ / 1024, available / 1024, minimum / 1024,
                          blocks); // "%23s: %5dK %4dK %4dK %4dK  %5d \n"
    }
    int sound_free = free_sound_memory();
    debug::printf(format("\nSound mem free %d K  (%d bytes)\n"), free_sound_memory() / 1024,
                  sound_free); // "\nSound mem free %d K  (%d bytes)\n"
    dump_pool_usage(false, true);
    return true;
}

// 0x00448330: the "to the dump file" flag in BL, the "print" flag on the stack (the caller pops it).
__declspec(naked) void dump_pool_usage_stub()
{
    __asm {
        push dword ptr [esp + 4]
        movzx eax, bl
        push eax
        call dump_pool_usage
        add esp, 8
        ret
    }
}

} // namespace commands
