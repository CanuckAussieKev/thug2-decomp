#include "engine/memory/manager.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>

#include "engine/memory/arena_alloc.h"
#include "base/core/dbg.h"
#include "engine/memory/mem.h"
#include "engine/memory/memory_sizes.h"
#include "engine/sys/game_config.h"
#include "platform/crt.h"
#include <gsl/pointers>
#include <array>
#include "base/core/checked_index.h"
#include "base/core/no_destroy.h"
#include "base/core/checksums.h"
#include <memory>
#include <span>
#include <gsl/assert>

namespace memory {

namespace {
constexpr size_t kRegionAlignment = 16;
constexpr int kFrontEndRegionBootstrapBytes = 1000000;
constexpr int kFrontEndRegionBytes = 1100000;
constexpr int kSkaterInfoRegionBytes = 82000;
constexpr int kThemeRegionBytes = 0x3C00;
constexpr int kSkaterRegionBytes = 0x1AFA4;
constexpr int kSkaterGeomRegionBytes = 0x73F78;
constexpr uint32_t kPermanentTextureHeapId = 0x0B1BA9DE;   // PermanentTextureHeap
constexpr int kPermanentTextureHeapBytes = 0x16800;
constexpr uint32_t kUnloadableTextureHeapId = 0x33B42C96;  // UnloadableTextureHeap
constexpr int kUnloadableTextureHeapBytes = 0x49C00;
constexpr int kNetExtrasRegionBytes = 160000;
constexpr int kOnlineRegionBytes = 450000;

void* heap_allocate(MemSource* heap, uint32_t size, int halt_if_exhausted)
{
    return heap->carve(size, halt_if_exhausted != 0);
}

void delete_region(std::unique_ptr<Zone>& region) { memory::destroy_then_clear(region); }


// NOT ORIGINAL grouping - the memory manager, whether it is set up, the static storage it and its main region and heaps
// are made in, the block the main region is in and the program's segments. Made on first use, never destroyed
// (manager_state()).
struct ManagerState {
    // The one memory manager (made by the exe's static initialisers: standalone start-up order, step 6).
    Hub* memory_manager_{};
    bool manager_ready_{}; // the memory manager is set up
    // Where the manager and its main region and heaps live (static storage in the game, as words).
    std::array<uint32_t, (sizeof(Hub) + 3) / 4> manager_storage_{};
    std::array<uint32_t, (sizeof(Zone) + 3) / 4> main_region_storage_{};
    std::array<uint32_t, (sizeof(Arena) + 3) / 4> top_down_storage_{};
    std::array<uint32_t, (sizeof(Arena) + 3) / 4> bottom_up_storage_{};
    std::span<uint8_t> heap_block_; // the block the main region is in (the original kept its start, end and limit)
    uintptr_t data_end_{};
    uintptr_t data_start_{};
    uintptr_t text_seg_base_{};
};

ManagerState& manager_state()
{
    static core::NoDestroy<ManagerState> state;
    return state.get();
}

} // namespace

bool& manager_ready_flag() { return manager_state().manager_ready_; }

Hub* Hub::get_pull() { return manager_state().memory_manager_; }

// 0x005BAFC0
void* Hub::carve(uint32_t size, int halt_if_exhausted, MemSource* heap)
{
    if (!heap) {
        heap = *ongoing_;
    }
    void* block = heap_allocate(heap, size, halt_if_exhausted);
    if (block) {
        mark_block_in_use(block);
    }
    return block;
}

// 0x005BB000
void* Hub::allocate_zeroed(uint32_t size, int halt_if_exhausted, MemSource* heap)
{
    if (!heap) {
        heap = *ongoing_;
    }
    void* block = heap_allocate(heap, size, halt_if_exhausted);
    if (block) {
        mark_block_in_use(block);
        std::memset(block, 0, size);
    }
    return block;
}

// 0x005BB060
void* Hub::extend_block_upward(uint32_t size, void* block, MemSource* heap)
{
    if (!heap) {
        heap = *ongoing_;
    }
    void* result = heap->extend_block_upward(size, block);
    if (result) {
        mark_block_in_use(result);
    }
    return result;
}

// 0x005BB0A0
void* Hub::trim_block_end(uint32_t size, void* block, MemSource* heap)
{
    if (!heap) {
        heap = *ongoing_;
    }
    void* result = heap->trim_block_end(size, block);
    if (result) {
        mark_block_in_use(result);
    }
    return result;
}

// 0x005BABA0
void Hub::dealloc(void* block)
{
    if (block) {
        free_block(block);
    }
}

// 0x005BB050
uint32_t Hub::obtainable() { return (*ongoing_)->largest_free(); }

// 0x005BB0E0
void Hub::enter_heap_scope(MemSource* heap)
{
    ongoing_ = &core::at(alloc_source_stack_, context_depth_);
    *ongoing_ = heap;
    ++context_depth_;
}

// 0x005BB110
void Hub::leave_heap_scope()
{
    --context_depth_;
    core::at(alloc_source_stack_, context_depth_) = nullptr; // (the original left -1 there; never read)
    ongoing_ = (context_depth_ != 0) ? &core::at(alloc_source_stack_, context_depth_ - 1) : nullptr;
}

// 0x005BB150
const char* Hub::current_heap_name() { return (*ongoing_)->label(); }

// 0x005BAC10
MemSource* Hub::first_zone() { return (heap_count_ != 0) ? core::at(heaps_, 0) : nullptr; }

// 0x005BAC20 (the last heap is never found, so its "next" is null too)
MemSource* Hub::following_zone(MemSource* heap)
{
    for (int i = 0; i < heap_count_ - 1; ++i) {
        if (core::at(heaps_, i) == heap) {
            return core::at(heaps_, i + 1);
        }
    }
    return nullptr;
}

// 0x005BABC0
void Hub::delete_heap(MemSource* heap)
{
    for (int i = 0; i < heap_count_; ++i) {
        if (core::at(heaps_, i) == heap) {
            --heap_count_;
            core::at(heaps_, i) = core::at(heaps_, heap_count_);
            break;
        }
    }
    gsl::owner<MemSource*> owned = memory::owned_element(heap); // (the registry's)
    delete owned;
}

// 0x005BAF00
Hub::LabeledZone* Hub::find_named_heap(uint32_t name)
{
    for (LabeledZone& named : named_heaps_) {
        if (named.used && named.name == name) {
            return &named;
        }
    }
    return nullptr;
}

// 0x005BAF50
Arena* Hub::fetch_heap_by_key(uint32_t name, int /*unused*/)
{
    const LabeledZone* named = find_named_heap(name);
    return named ? named->heap : nullptr;
}

// 0x005BAF70
bool Hub::release_labeled_zone(uint32_t name, int /*unused*/)
{
    LabeledZone* named = find_named_heap(name);
    if (!named) {
        return false;
    }
    delete_heap(named->heap);
    if (named->region) {
        delete_region(named->region);
    }
    named->region = nullptr;
    named->heap = nullptr;
    named->name = 0;
    named->used = false;
    return true;
}

namespace {

// The standard heaps' name checksums.
constexpr uint32_t kTopDownHeap = 0x8FDB68AF;
constexpr uint32_t kBottomUpHeap = 0x0C7800B0;
constexpr uint32_t kFrontEndHeap = 0x62F3A0F3;
constexpr uint32_t kScriptHeap = 0x0FA33D9B;
constexpr uint32_t kNetworkHeap = 0xE3551D2E;
constexpr uint32_t kNetMiscHeap = 0x96D29D93;
constexpr uint32_t kInternetTopDownHeap = 0xFCD5166B;
constexpr uint32_t kInternetBottomUpHeap = 0x90020867;
constexpr uint32_t kCutsceneTopDown = 0x25D71469;
constexpr uint32_t kCutsceneBottomUp = 0x7ED56B49;
constexpr uint32_t kDebugHeap = 0x70CB0238;
constexpr uint32_t kSkater0 = 0xC3909393;
constexpr uint32_t kSkater1 = 0xB497A305;
constexpr uint32_t kSkater2 = 0x2D9EF2BF;
constexpr uint32_t kSkater3 = 0x5A99C229;
constexpr uint32_t kSkaterGeom0 = 0x572A9F4C;
constexpr uint32_t kSkaterGeom1 = 0x202DAFDA;
constexpr uint32_t kSkaterGeom2 = 0xB924FE60;
constexpr uint32_t kSkaterGeom3 = 0xCE23CEF6;
constexpr uint32_t kSkaterInfo = 0x08682D24;
constexpr uint32_t kThemeHeap = 0x17ECB880;

} // namespace

// 0x005BB310
Arena* Hub::get_memory_pool(uint32_t name)
{
    if (const LabeledZone* named = find_named_heap(name)) {
        return named->heap;
    }
    switch (name) {
    case kTopDownHeap:
        return top_down_heap_;
    case kBottomUpHeap:
        return bottom_up_heap_;
    case kFrontEndHeap:
        return front_end_heap_;
    case kScriptHeap:
        return script_heap_;
    case kNetworkHeap:
        return network_heap_;
    case kNetMiscHeap:
        return net_misc_heap_;
    case kInternetTopDownHeap:
        return internet_top_down_heap_;
    case kInternetBottomUpHeap:
        return internet_bottom_up_heap_;
    case kCutsceneTopDown:
        return cinematic_high_heap_;
    case kCutsceneBottomUp:
        return cinematic_low_heap_;
    case kDebugHeap:
        return debug_heap_;
    case kSkater0:
        return core::at(skater_heaps_, 0);
    case kSkater1:
        return core::at(skater_heaps_, 1);
    case kSkater2:
        return core::at(skater_heaps_, 2);
    case kSkater3:
        return core::at(skater_heaps_, 3);
    case kSkaterGeom0:
        return core::at(skater_geom_heaps_, 0);
    case kSkaterGeom1:
        return core::at(skater_geom_heaps_, 1);
    case kSkaterGeom2:
        return core::at(skater_geom_heaps_, 2);
    case kSkaterGeom3:
        return core::at(skater_geom_heaps_, 3);
    case kSkaterInfo:
        return skater_info_heap_;
    case kThemeHeap:
        return theme_heap_;
    default:
        break;
    }
    // (looked up again, as in the original)
    const LabeledZone* named = find_named_heap(name);
    return named ? named->heap : nullptr;
}

// 0x005BB160
const char* Hub::get_zone_label(uint32_t name)
{
    if (LabeledZone* named = find_named_heap(name)) {
        return named->text.data();
    }
    switch (name) {
    case kTopDownHeap:
        return "TopDownHeap";
    case kBottomUpHeap:
        return "BottomUpHeap";
    case kFrontEndHeap:
        return "FrontEndHeap";
    case kScriptHeap:
        return "ScriptHeap";
    case kNetworkHeap:
        return "NetworkHeap";
    case kNetMiscHeap:
        return "NetMiscHeap";
    case kInternetTopDownHeap:
        return "InternetTopDownHeap";
    case kInternetBottomUpHeap:
        return "InternetBottomUpHeap";
    case kCutsceneTopDown:
        return "CutsceneTopDown";
    case kCutsceneBottomUp:
        return "CutsceneBottomUp";
    case kDebugHeap:
        return "DebugHeap";
    case kSkater0:
        return "Skater0";
    case kSkater1:
        return "Skater1";
    case kSkater2:
        return "Skater2";
    case kSkater3:
        return "Skater3";
    case kSkaterGeom0:
        return "SkaterGeom0";
    case kSkaterGeom1:
        return "SkaterGeom1";
    case kSkaterGeom2:
        return "SkaterGeom2";
    case kSkaterGeom3:
        return "SkaterGeom3";
    case kSkaterInfo:
        return "SkaterInfo";
    case kThemeHeap:
        return "ThemeHeap";
    default:
        return "Unrecognized Heap";
    }
}

// 0x005BAC50
void Hub::release_net_scratch_zone()
{
    if (!network_extras_zone_) {
        return;
    }
    delete_heap(net_misc_heap_);
    if (network_extras_zone_) {
        delete_region(network_extras_zone_);
    }
    network_extras_zone_ = nullptr;
}

// 0x005BAC90
void Hub::delete_cutscene_heaps()
{
    if (!cinematic_zone_) {
        return;
    }
    delete_heap(cinematic_low_heap_);
    cinematic_low_heap_ = nullptr;
    delete_heap(cinematic_high_heap_);
    cinematic_high_heap_ = nullptr;
    if (cinematic_zone_) {
        delete_region(cinematic_zone_);
    }
    cinematic_zone_ = nullptr;
}

// 0x005BACF0: the heaps of skaters 1 to 7 (multiplayer)
void Hub::delete_extra_skater_heaps()
{
    for (int i = 1; i < kSkaterSlots; ++i) {
        if (core::at(skater_regions_, i)) {
            debug::printf("DELETING SKATER HEAP %d\n", i);
            delete_heap(core::at(skater_heaps_, i));
            if (core::at(skater_regions_, i)) {
                delete_region(core::at(skater_regions_, i));
            }
            core::at(skater_heaps_, i) = nullptr;
            core::at(skater_regions_, i) = nullptr;
        }
        if (core::at(skater_geom_regions_, i)) {
            debug::printf("DELETING SKATER GEOM HEAP %d\n", i);
            delete_heap(core::at(skater_geom_heaps_, i));
            if (core::at(skater_geom_regions_, i)) {
                delete_region(core::at(skater_geom_regions_, i));
            }
            core::at(skater_geom_heaps_, i) = nullptr;
            core::at(skater_geom_regions_, i) = nullptr;
        }
    }
}

// 0x005BADD0 (the pointers are left as they were)
void Hub::delete_standard_heaps()
{
    delete_heap(front_end_heap_);
    if (front_end_region_) {
        delete_region(front_end_region_);
    }
    delete_heap(script_heap_);
    if (script_region_) {
        delete_region(script_region_);
    }
    delete_heap(network_heap_);
    if (multiplayer_zone_) {
        delete_region(multiplayer_zone_);
    }
    delete_heap(skater_info_heap_);
    if (skater_info_region_) {
        delete_region(skater_info_region_);
    }
    delete_heap(core::at(skater_heaps_, 0));
    if (core::at(skater_regions_, 0)) {
        delete_region(core::at(skater_regions_, 0));
    }
    delete_heap(core::at(skater_geom_heaps_, 0));
    if (core::at(skater_geom_regions_, 0)) {
        delete_region(core::at(skater_geom_regions_, 0));
    }
    delete_extra_skater_heaps();
}

// 0x005BBE50
void Hub::destroy_main_heaps()
{
    // (made in static storage: destroyed, not freed)
    top_down_heap_->~Arena();
    bottom_up_heap_->~Arena();
    primary_zone_->~Zone();
}

// --- the global allocation functions (on the current heap) -------------------------------------------------

// 0x005BBAE0
void* alloc_block(unsigned int size) { return Hub::get_pull()->carve(size, 1, nullptr); }

// 0x005BBB10
uint32_t malloc_available() { return Hub::get_pull()->obtainable(); }

// 0x005BBB30
void* trim_block_end(unsigned int size, void* memory)
{
    void* result = (*Hub::get_pull()->ongoing())->trim_block_end(size, memory);
    if (result) {
        mark_block_in_use(result);
    }
    return result;
}

// 0x005BBB60
void free_malloc(void* memory)
{
    if (memory) {
        free_block(memory);
    }
}

// 0x005BBB80
unsigned int malloc_size(void* memory) { return memory ? get_block_size(memory) : 0; }

// 0x005BBBA0: a new block of `size` with the old one's contents (copies `size` bytes, even from a smaller
// block - as in the original)
void* regrow(void* memory, unsigned int size)
{
    void* block = nullptr;
    if (size != 0U) {
        block = heap_allocate(*Hub::get_pull()->ongoing(), size, 1);
        if (block) {
            mark_block_in_use(block);
        }
    }
    if (memory) {
        if (block) {
            std::memmove(block, memory, size);
        }
        free_block(memory);
    }
    return block;
}

} // namespace memory

// --- creating the manager and its heaps -----------------------------------------------------------------------

namespace memory {

namespace {

bool& manager_ready() { return manager_ready_flag(); }

// The game CRT's malloc (the main block comes from it).
gsl::owner<void*> get_crt_malloc(size_t size) { return crt::alloc_block(size); }

gsl::owner<MemoryArena*> new_alloc_region(uint32_t size) { return new MemoryArena(size); }

} // namespace

Hub*& Hub::sole() { return manager_state().memory_manager_; }

// 0x005BBC00
Hub::Hub() : network_extras_zone_(nullptr), online_zone_(nullptr), cinematic_zone_(nullptr)
{
    for (LabeledZone& named : named_heaps_) {
        named.region = nullptr;
        named.heap = nullptr;
        named.name = 0;
        named.used = false;
    }

    // Not original - the block is 64 MB and what the bigger script pools take more (engine/memory/memory_sizes.h:
    // pool sizes as THUGPRO's), so every heap in it keeps the room it had.
    // Original behaviour: 64 MB (0x4000000).
    const uint32_t block_size = sizes::main_block_size();
    manager_state().heap_block_ = std::span<uint8_t>(static_cast<uint8_t*>(get_crt_malloc(block_size)), block_size);
    // The region: the block's start rounded up and its end rounded down to 16 bytes.
    void* aligned = manager_state().heap_block_.data();
    size_t space = block_size;
    std::align(kRegionAlignment, 0, aligned, space);
    const std::span<uint8_t> main_memory = manager_state().heap_block_.subspan(block_size - space, space & ~size_t{15});
    set_heap_memory(main_memory);
    void* region = get_clear(sizeof(Zone), manager_state().main_region_storage_.data());
    primary_zone_ = region ? memory::construct_in<Zone>(region, main_memory) : nullptr;
    void* top = get_clear(sizeof(Arena), manager_state().top_down_storage_.data());
    top_down_heap_ = top ? memory::construct_in<Arena>(top, primary_zone_, MemSource::kGrowDown, static_cast<const char*>("Top Down")) : nullptr;
    void* bottom = get_clear(sizeof(Arena), manager_state().bottom_up_storage_.data());
    bottom_up_heap_ =
        bottom ? memory::construct_in<Arena>(bottom, primary_zone_, MemSource::kGrowUp, static_cast<const char*>("BottomUp")) : nullptr;
    core::at(heaps_, 1) = bottom_up_heap_;
    core::at(heaps_, 0) = top_down_heap_;

    // (the code and data bounds are never set in this build: all zero)
    auto code_start = manager_state().text_seg_base_;
    auto data_start = manager_state().data_start_;
    auto data_end = manager_state().data_end_;
    const uint32_t data_k = static_cast<uint32_t>(data_end - data_start) >> 10;
    const uint32_t code_k = static_cast<uint32_t>(data_start - code_start) >> 10;
    // (the bounds are addresses kept as numbers: %08X prints what the original's %p did)
    debug::printf("code [%08X - %08X] (%dK) + data [%08X - %08X] (%dK) = %dK \n", code_start, data_start, code_k,
                  data_start, data_end, data_k, data_k + code_k);

}

// 0x005BB4E0
Arena* Hub::add_heap(Zone* region, int direction, const char* name)
{
    gsl::owner<memory::Arena*> heap{memory::make_owned<Arena>(region, direction, name)};
    core::at(heaps_, heap_count_++) = heap;
    Arena* const registered = heap;
    return registered;
}

// 0x005BBE80
void Hub::init_standard_heaps()
{
    front_end_region_ = memory::take_unique<Zone>(new_alloc_region(game_config::bootstrap() ? kFrontEndRegionBootstrapBytes : kFrontEndRegionBytes));
    debug::printf("allocated mp_frontend_region at %p\n", front_end_region_.get());
    network_heap_ = add_heap(front_end_region_.get(), MemSource::kGrowDown, "Network");
    debug::printf("Setup TOP_DOWN mp_network_heap at %p\n", network_heap_);
    front_end_heap_ = add_heap(front_end_region_.get(), MemSource::kGrowUp, "FrontEnd");
    debug::printf("Setup mp_frontend_heap at %p\n", front_end_heap_);

    // Not original - grown by what the bigger script pools take more (engine/memory/memory_sizes.h: pool sizes as
    // THUGPRO's; the room left for scripts stays the original's).
    // Original behaviour: 0x434770 bytes.
    script_region_ = memory::take_unique<Zone>(new_alloc_region(sizes::script_region_size()));
    debug::printf("allocated mp_script_region at %p\n", script_region_.get());
    script_heap_ = add_heap(script_region_.get(), MemSource::kGrowUp, "Script");
    debug::printf("Setup mp_script_heap at %p\n", script_heap_);

    skater_info_region_ = memory::take_unique<Zone>(new_alloc_region(kSkaterInfoRegionBytes));
    debug::printf("allocated mp_skater_info_region at %p\n", skater_info_region_.get());
    skater_info_heap_ = add_heap(skater_info_region_.get(), MemSource::kGrowUp, "skt_info");
    debug::printf("Setup mp_skater_info_heap at %p\n", skater_info_heap_);

    theme_region_ = memory::take_unique<Zone>(new_alloc_region(kThemeRegionBytes));
    debug::printf("allocated mp_theme_region at %p\n", theme_region_.get());
    theme_heap_ = add_heap(theme_region_.get(), MemSource::kGrowUp, "theme");
    debug::printf("Setup mp_theme_heap at %p\n", theme_heap_);

    core::at(skater_regions_, 0) = memory::take_unique<Zone>(new_alloc_region(kSkaterRegionBytes));
    debug::printf("allocated mp_skater_region at %p\n", core::at(skater_regions_, 0).get());
    core::at(skater_heaps_, 0) = add_heap(core::at(skater_regions_, 0).get(), MemSource::kGrowUp, "skater");
    debug::printf("Setup mp_skater_heap at %p\n", core::at(skater_heaps_, 0));

    core::at(skater_geom_regions_, 0) = memory::take_unique<Zone>(new_alloc_region(kSkaterGeomRegionBytes));
    debug::printf("allocated mp_skater_geom_region at %p\n", core::at(skater_geom_regions_, 0).get());
    core::at(skater_geom_heaps_, 0) = add_heap(core::at(skater_geom_regions_, 0).get(), MemSource::kGrowUp, "skater_geom");
    debug::printf("Setup mp_skater_geom_heap at %p\n", core::at(skater_geom_heaps_, 0));

    for (int i = 1; i < kSkaterSlots; ++i) {
        core::at(skater_regions_, i) = nullptr;
        core::at(skater_heaps_, i) = nullptr;
        core::at(skater_geom_regions_, i) = nullptr;
        core::at(skater_geom_heaps_, i) = nullptr;
    }
    create_labeled_zone(checksums::Name{kPermanentTextureHeapId}, kPermanentTextureHeapBytes, "PermanentTextureHeap", 0);
    create_labeled_zone(checksums::Name{kUnloadableTextureHeapId}, kUnloadableTextureHeapBytes, "UnloadableTextureHeap", 0);
}

// 0x005BB560 (made with the bottom-up heap current, on the game's manager)
void Hub::create_net_scratch_zone()
{
    if (network_extras_zone_) {
        return;
    }
    Hub* manager = get_pull();
    manager->enter_heap_scope(manager->bottom_up_heap_);
    network_extras_zone_ = memory::take_unique<Zone>(new_alloc_region(kNetExtrasRegionBytes));
    net_misc_heap_ = add_heap(network_extras_zone_.get(), MemSource::kGrowUp, "NetMisc");
    get_pull()->leave_heap_scope();
}

// 0x005BB660 (made with the bottom-up heap current, on the game's manager)
void Hub::init_internet_heaps()
{
    if (online_zone_) {
        return;
    }
    Hub* manager = get_pull();
    manager->enter_heap_scope(manager->bottom_up_heap_);
    online_zone_ = memory::take_unique<Zone>(new_alloc_region(kOnlineRegionBytes));
    internet_top_down_heap_ = add_heap(online_zone_.get(), MemSource::kGrowDown, "InternetTopDown");
    internet_bottom_up_heap_ = add_heap(online_zone_.get(), MemSource::kGrowUp, "InternetBottomUp");
    get_pull()->leave_heap_scope();
}

// 0x005BB780
void Hub::init_cutscene_heaps(uint32_t size)
{
    if (cinematic_zone_) {
        return;
    }
    cinematic_zone_ = memory::take_unique<Zone>(new_alloc_region(size));
    cinematic_low_heap_ = add_heap(cinematic_zone_.get(), MemSource::kGrowUp, "CutsceneBottomUp");
    cinematic_high_heap_ = add_heap(cinematic_zone_.get(), MemSource::kGrowDown, "CutsceneTopDown");
}

// 0x005BB820
void Hub::init_extra_skater_heaps(int count)
{
    debug::printf("Init Skater Heaps\n");
    count = std::min(count, kSkaterSlots);
    for (int i = 1; i < count; ++i) {
        core::at(skater_regions_, i) = memory::take_unique<Zone>(new_alloc_region(kSkaterRegionBytes));
        debug::printf("EXTRA: allocated mp_skater_region at %p\n", core::at(skater_regions_, i).get());
        core::at(skater_heaps_, i) = add_heap(core::at(skater_regions_, i).get(), MemSource::kGrowUp, "skaterX");
        debug::printf("EXTRA: Setup mp_skater_heap at %p\n", core::at(skater_heaps_, i));
        core::at(skater_geom_regions_, i) = memory::take_unique<Zone>(new_alloc_region(kSkaterGeomRegionBytes));
        debug::printf("EXTRA: allocated mp_skater_geom_region at %p\n", core::at(skater_geom_regions_, i).get());
        core::at(skater_geom_heaps_, i) = add_heap(core::at(skater_geom_regions_, i).get(), MemSource::kGrowUp, "skaterGeomX");
        debug::printf("EXTRA: Setup mp_skater_geom_heap at %p\n", core::at(skater_geom_heaps_, i));
    }
    debug::printf("END Init Skater Heaps\n");
}

// 0x005BB9E0
void Hub::create_labeled_zone(checksums::Name zone_name, uint32_t size, const char* text, int /*unused*/)
{
    const uint32_t name = zone_name.value();
    LabeledZone* named = nullptr;
    for (LabeledZone& slot : named_heaps_) {
        if (!slot.used) {
            named = &slot;
            break;
        }
    }
    // (no free slot: crashes here, as in the original)
    Expects(named != nullptr);
    named->name = name;
    std::memcpy(named->text.data(), text, std::strlen(text) + 1); // (unbounded, as in the original)
    named->region = memory::take_unique<Zone>(new_alloc_region(size));
    named->heap = add_heap(named->region.get(), MemSource::kGrowUp, named->text.data());
    named->used = true;
}

// 0x005BC2C0
void set_up_manager()
{
    if (Hub::get_pull()) {
        return;
    }
    void* memory = get_clear(sizeof(Hub), manager_state().manager_storage_.data());
    Hub* manager = memory ? memory::construct_in<Hub>(memory) : nullptr;
    Hub::sole() = manager;
    Expects(manager != nullptr);
    manager->enter_heap_scope(manager->bottom_up_heap());
}

// 0x004E1950
int ensure_manager()
{
    if (!manager_ready()) {
        set_up_manager();
        manager_ready() = true;
    }
    return 0;
}

// 0x005BC340
void shut_down_manager()
{
    Hub* manager = Hub::get_pull();
    if (!manager) {
        return;
    }
    manager->delete_standard_heaps();
    manager->leave_heap_scope();
    manager->destroy_main_heaps();
    Hub::sole() = nullptr;
}

} // namespace memory
