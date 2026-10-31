#include "engine/core/manager.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>

#include "engine/core/dbg.h"
#include "engine/core/mem.h"
#include "engine/core/memory_sizes.h"
#include "engine/sys/game_config.h"
#include "platform/crt.h"

namespace memory {

namespace {

void* heap_allocate(MemSource* heap, uint32_t size, int halt_if_exhausted)
{
    return heap->carve(size, halt_if_exhausted != 0);
}

void delete_region(Zone* region) { delete region; }

MemSource* current_heap(Hub* manager) { return *manager->ongoing_; }

} // namespace

// The one memory manager (made by the exe's static initialisers: standalone start-up order, step 6).
Hub* g_memory_manager;
bool g_manager_ready; // the memory manager is set up

Hub* Hub::get_pull() { return g_memory_manager; }

// 0x005BAFC0
void* Hub::carve(uint32_t size, int halt_if_exhausted, MemSource* heap)
{
    if (!heap)
        heap = current_heap(this);
    void* block = heap_allocate(heap, size, halt_if_exhausted);
    if (block)
        mark_block_in_use(block);
    return block;
}

// 0x005BB000
void* Hub::allocate_zeroed(uint32_t size, int halt_if_exhausted, MemSource* heap)
{
    if (!heap)
        heap = current_heap(this);
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
    if (!heap)
        heap = current_heap(this);
    void* result = heap->extend_block_upward(size, block);
    if (result)
        mark_block_in_use(result);
    return result;
}

// 0x005BB0A0
void* Hub::trim_block_end(uint32_t size, void* block, MemSource* heap)
{
    if (!heap)
        heap = current_heap(this);
    void* result = heap->trim_block_end(size, block);
    if (result)
        mark_block_in_use(result);
    return result;
}

// 0x005BABA0
void Hub::dealloc(void* block)
{
    if (block)
        free_block(block);
}

// 0x005BB050
uint32_t Hub::obtainable() { return current_heap(this)->largest_free(); }

// 0x005BB0E0
void Hub::enter_heap_scope(MemSource* heap)
{
    ongoing_ = &alloc_source_stack_[context_depth_];
    *ongoing_ = heap;
    ++context_depth_;
}

// 0x005BB110
void Hub::leave_heap_scope()
{
    --context_depth_;
    alloc_source_stack_[context_depth_] = reinterpret_cast<MemSource*>(-1);
    ongoing_ = context_depth_ ? &alloc_source_stack_[context_depth_ - 1] : nullptr;
}

// 0x005BB150
const char* Hub::current_heap_name() { return current_heap(this)->label_; }

// 0x005BAC10
MemSource* Hub::first_zone() { return heap_count_ ? heaps_[0] : nullptr; }

// 0x005BAC20 (the last heap is never found, so its "next" is null too)
MemSource* Hub::following_zone(MemSource* heap)
{
    for (int i = 0; i < heap_count_ - 1; ++i) {
        if (heaps_[i] == heap)
            return heaps_[i + 1];
    }
    return nullptr;
}

// 0x005BABC0
void Hub::delete_heap(MemSource* heap)
{
    for (int i = 0; i < heap_count_; ++i) {
        if (heaps_[i] == heap) {
            --heap_count_;
            heaps_[i] = heaps_[heap_count_];
            break;
        }
    }
    delete heap;
}

// 0x005BAF00
Hub::LabeledZone* Hub::find_named_heap(uint32_t name)
{
    for (LabeledZone& named : named_heaps_) {
        if (named.used && named.name == name)
            return &named;
    }
    return nullptr;
}

// 0x005BAF50
Arena* Hub::fetch_heap_by_key(uint32_t name, int /*unused*/)
{
    LabeledZone* named = find_named_heap(name);
    return named ? named->heap : nullptr;
}

// 0x005BAF70
bool Hub::release_labeled_zone(uint32_t name, int /*unused*/)
{
    LabeledZone* named = find_named_heap(name);
    if (!named)
        return false;
    delete_heap(named->heap);
    if (named->region)
        delete_region(named->region);
    named->region = nullptr;
    named->heap = nullptr;
    named->name = 0;
    named->used = false;
    return true;
}

namespace {

// The standard heaps' name checksums.
enum HeapName : uint32_t {
    kTopDownHeap = 0x8FDB68AF,
    kBottomUpHeap = 0x0C7800B0,
    kFrontEndHeap = 0x62F3A0F3,
    kScriptHeap = 0x0FA33D9B,
    kNetworkHeap = 0xE3551D2E,
    kNetMiscHeap = 0x96D29D93,
    kInternetTopDownHeap = 0xFCD5166B,
    kInternetBottomUpHeap = 0x90020867,
    kCutsceneTopDown = 0x25D71469,
    kCutsceneBottomUp = 0x7ED56B49,
    kDebugHeap = 0x70CB0238,
    kSkater0 = 0xC3909393,
    kSkater1 = 0xB497A305,
    kSkater2 = 0x2D9EF2BF,
    kSkater3 = 0x5A99C229,
    kSkaterGeom0 = 0x572A9F4C,
    kSkaterGeom1 = 0x202DAFDA,
    kSkaterGeom2 = 0xB924FE60,
    kSkaterGeom3 = 0xCE23CEF6,
    kSkaterInfo = 0x08682D24,
    kThemeHeap = 0x17ECB880,
};

} // namespace

// 0x005BB310
Arena* Hub::get_memory_pool(uint32_t name)
{
    if (LabeledZone* named = find_named_heap(name))
        return named->heap;
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
        return skater_heaps_[0];
    case kSkater1:
        return skater_heaps_[1];
    case kSkater2:
        return skater_heaps_[2];
    case kSkater3:
        return skater_heaps_[3];
    case kSkaterGeom0:
        return skater_geom_heaps_[0];
    case kSkaterGeom1:
        return skater_geom_heaps_[1];
    case kSkaterGeom2:
        return skater_geom_heaps_[2];
    case kSkaterGeom3:
        return skater_geom_heaps_[3];
    case kSkaterInfo:
        return skater_info_heap_;
    case kThemeHeap:
        return theme_heap_;
    default:
        break;
    }
    // (looked up again, as in the original)
    LabeledZone* named = find_named_heap(name);
    return named ? named->heap : nullptr;
}

// 0x005BB160
const char* Hub::get_zone_label(uint32_t name)
{
    if (LabeledZone* named = find_named_heap(name))
        return named->text;
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
    if (!network_extras_zone_)
        return;
    delete_heap(net_misc_heap_);
    if (network_extras_zone_)
        delete_region(network_extras_zone_);
    network_extras_zone_ = nullptr;
}

// 0x005BAC90
void Hub::delete_cutscene_heaps()
{
    if (!cinematic_zone_)
        return;
    delete_heap(cinematic_low_heap_);
    cinematic_low_heap_ = nullptr;
    delete_heap(cinematic_high_heap_);
    cinematic_high_heap_ = nullptr;
    if (cinematic_zone_)
        delete_region(cinematic_zone_);
    cinematic_zone_ = nullptr;
}

// 0x005BACF0: the heaps of skaters 1 to 7 (multiplayer)
void Hub::delete_extra_skater_heaps()
{
    for (int i = 1; i < kSkaterSlots; ++i) {
        if (skater_regions_[i]) {
            debug::printf("DELETING SKATER HEAP %d\n", i);
            delete_heap(skater_heaps_[i]);
            if (skater_regions_[i])
                delete_region(skater_regions_[i]);
            skater_heaps_[i] = nullptr;
            skater_regions_[i] = nullptr;
        }
        if (skater_geom_regions_[i]) {
            debug::printf("DELETING SKATER GEOM HEAP %d\n", i);
            delete_heap(skater_geom_heaps_[i]);
            if (skater_geom_regions_[i])
                delete_region(skater_geom_regions_[i]);
            skater_geom_heaps_[i] = nullptr;
            skater_geom_regions_[i] = nullptr;
        }
    }
}

// 0x005BADD0 (the pointers are left as they were)
void Hub::delete_standard_heaps()
{
    delete_heap(front_end_heap_);
    if (front_end_region_)
        delete_region(front_end_region_);
    delete_heap(script_heap_);
    if (script_region_)
        delete_region(script_region_);
    delete_heap(network_heap_);
    if (multiplayer_zone_)
        delete_region(multiplayer_zone_);
    delete_heap(skater_info_heap_);
    if (skater_info_region_)
        delete_region(skater_info_region_);
    delete_heap(skater_heaps_[0]);
    if (skater_regions_[0])
        delete_region(skater_regions_[0]);
    delete_heap(skater_geom_heaps_[0]);
    if (skater_geom_regions_[0])
        delete_region(skater_geom_regions_[0]);
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
    void* result = current_heap(Hub::get_pull())->trim_block_end(size, memory);
    if (result)
        mark_block_in_use(result);
    return result;
}

// 0x005BBB60
void free_malloc(void* memory)
{
    if (memory)
        free_block(memory);
}

// 0x005BBB80
unsigned int malloc_size(void* memory) { return memory ? get_block_size(memory) : 0; }

// 0x005BBBA0: a new block of `size` with the old one's contents (copies `size` bytes, even from a smaller
// block - as in the original)
void* regrow(void* memory, unsigned int size)
{
    void* block = nullptr;
    if (size) {
        block = heap_allocate(current_heap(Hub::get_pull()), size, 1);
        if (block)
            mark_block_in_use(block);
    }
    if (memory) {
        if (block)
            std::memmove(block, memory, size);
        free_block(memory);
    }
    return block;
}

} // namespace memory

// --- creating the manager and its heaps -----------------------------------------------------------------------

namespace memory {

// Where the manager and its main region and heaps live (static storage in the game, as words).
uint32_t g_manager_storage[(sizeof(Hub) + 3) / 4];
uint32_t g_main_region_storage[(sizeof(Zone) + 3) / 4];
uint32_t g_top_down_storage[(sizeof(Arena) + 3) / 4];
uint32_t g_bottom_up_storage[(sizeof(Arena) + 3) / 4];

namespace {

void* const kManagerStorage = g_manager_storage;
void* const kMainRegionStorage = g_main_region_storage;
void* const kTopDownStorage = g_top_down_storage;
void* const kBottomUpStorage = g_bottom_up_storage;

bool& manager_ready() { return g_manager_ready; }

// The game CRT's malloc (the main block comes from it).
void* get_crt_malloc(size_t size) { return crt::alloc_block(size); }

MemoryArena* new_alloc_region(uint32_t size) { return new MemoryArena(size); }

} // namespace

Hub*& Hub::sole() { return g_memory_manager; }

uint8_t* g_heap_block_limit;
uint8_t* g_heap_block_end;
uint8_t* g_heap_block_start;
uintptr_t g_data_end;
uintptr_t g_data_start;
uintptr_t g_text_seg_base;

// 0x005BBC00
Hub* Hub::make_new()
{
    ongoing_ = nullptr;
    for (LabeledZone& named : named_heaps_) {
        named.region = nullptr;
        named.heap = nullptr;
        named.name = 0;
        named.used = false;
    }
    unknown5e4_ = 0;
    unknown5e0_ = 0;

    // Not original - the block is 64 MB and what the bigger script pools take more (engine/core/memory_sizes.h:
    // pool sizes as THUGPRO's), so every heap in it keeps the room it had.
    // Original behaviour: 64 MB (0x4000000).
    const uint32_t block_size = sizes::main_block_size();
    auto* block = static_cast<uint8_t*>(get_crt_malloc(block_size));
    g_heap_block_start = block;
    g_heap_block_end = block + block_size;
    g_heap_block_limit = block + block_size;
    uint8_t* end = g_heap_block_limit;
    void* region = get_clear(sizeof(Zone), kMainRegionStorage);
    primary_zone_ = region ? new (region)
                                 Zone(reinterpret_cast<uint8_t*>((reinterpret_cast<uintptr_t>(block) + 15) & ~15u),
                                      reinterpret_cast<uint8_t*>(reinterpret_cast<uintptr_t>(end) & ~15u))
                           : nullptr;
    void* top = get_clear(sizeof(Arena), kTopDownStorage);
    top_down_heap_ = top ? new (top) Arena(primary_zone_, MemSource::kGrowDown, "Top Down") : nullptr;
    void* bottom = get_clear(sizeof(Arena), kBottomUpStorage);
    bottom_up_heap_ = bottom ? new (bottom) Arena(primary_zone_, MemSource::kGrowUp, "BottomUp") : nullptr;
    heaps_[1] = bottom_up_heap_;
    heaps_[0] = top_down_heap_;
    heap_count_ = 2;

    // (the code and data bounds are never set in this build: all zero)
    auto code_start = g_text_seg_base;
    auto data_start = g_data_start;
    auto data_end = g_data_end;
    uint32_t data_k = static_cast<uint32_t>(data_end - data_start) >> 10;
    uint32_t code_k = static_cast<uint32_t>(data_start - code_start) >> 10;
    debug::printf("code [%p - %p] (%dK) + data [%p - %p] (%dK) = %dK \n", code_start, data_start, code_k, data_start,
                  data_end, data_k, data_k + code_k);

    context_depth_ = 0;
    online_zone_ = nullptr;
    network_extras_zone_ = nullptr;
    cinematic_zone_ = nullptr;
    cinematic_low_heap_ = nullptr;
    cinematic_high_heap_ = nullptr;
    return this;
}

// 0x005BB4E0
Arena* Hub::add_heap(Zone* region, int direction, const char* name)
{
    Arena* heap = new Arena(region, direction, name);
    heaps_[heap_count_++] = heap;
    return heap;
}

// 0x005BBE80
void Hub::init_standard_heaps()
{
    front_end_region_ = new_alloc_region(game_config::g_config_bootstrap ? 1000000 : 1100000);
    debug::printf("allocated mp_frontend_region at %p\n", front_end_region_);
    network_heap_ = add_heap(front_end_region_, MemSource::kGrowDown, "Network");
    debug::printf("Setup TOP_DOWN mp_network_heap at %p\n", network_heap_);
    front_end_heap_ = add_heap(front_end_region_, MemSource::kGrowUp, "FrontEnd");
    debug::printf("Setup mp_frontend_heap at %p\n", front_end_heap_);

    // Not original - grown by what the bigger script pools take more (engine/core/memory_sizes.h: pool sizes as
    // THUGPRO's; the room left for scripts stays the original's).
    // Original behaviour: 0x434770 bytes.
    script_region_ = new_alloc_region(sizes::script_region_size());
    debug::printf("allocated mp_script_region at %p\n", script_region_);
    script_heap_ = add_heap(script_region_, MemSource::kGrowUp, "Script");
    debug::printf("Setup mp_script_heap at %p\n", script_heap_);

    skater_info_region_ = new_alloc_region(82000);
    debug::printf("allocated mp_skater_info_region at %p\n", skater_info_region_);
    skater_info_heap_ = add_heap(skater_info_region_, MemSource::kGrowUp, "skt_info");
    debug::printf("Setup mp_skater_info_heap at %p\n", skater_info_heap_);

    theme_region_ = new_alloc_region(0x3C00);
    debug::printf("allocated mp_theme_region at %p\n", theme_region_);
    theme_heap_ = add_heap(theme_region_, MemSource::kGrowUp, "theme");
    debug::printf("Setup mp_theme_heap at %p\n", theme_heap_);

    skater_regions_[0] = new_alloc_region(0x1AFA4);
    debug::printf("allocated mp_skater_region at %p\n", skater_regions_[0]);
    skater_heaps_[0] = add_heap(skater_regions_[0], MemSource::kGrowUp, "skater");
    debug::printf("Setup mp_skater_heap at %p\n", skater_heaps_[0]);

    skater_geom_regions_[0] = new_alloc_region(0x73F78);
    debug::printf("allocated mp_skater_geom_region at %p\n", skater_geom_regions_[0]);
    skater_geom_heaps_[0] = add_heap(skater_geom_regions_[0], MemSource::kGrowUp, "skater_geom");
    debug::printf("Setup mp_skater_geom_heap at %p\n", skater_geom_heaps_[0]);

    for (int i = 1; i < kSkaterSlots; ++i) {
        skater_regions_[i] = nullptr;
        skater_heaps_[i] = nullptr;
        skater_geom_regions_[i] = nullptr;
        skater_geom_heaps_[i] = nullptr;
    }
    create_labeled_zone(0x0B1BA9DE, 0x16800, "PermanentTextureHeap", 0);
    create_labeled_zone(0x33B42C96, 0x49C00, "UnloadableTextureHeap", 0);
}

// 0x005BB560 (made with the bottom-up heap current, on the game's manager)
void Hub::create_net_scratch_zone()
{
    if (network_extras_zone_)
        return;
    Hub* manager = get_pull();
    manager->enter_heap_scope(manager->bottom_up_heap_);
    network_extras_zone_ = new_alloc_region(160000);
    net_misc_heap_ = add_heap(network_extras_zone_, MemSource::kGrowUp, "NetMisc");
    get_pull()->leave_heap_scope();
}

// 0x005BB660 (made with the bottom-up heap current, on the game's manager)
void Hub::init_internet_heaps()
{
    if (online_zone_)
        return;
    Hub* manager = get_pull();
    manager->enter_heap_scope(manager->bottom_up_heap_);
    online_zone_ = new_alloc_region(450000);
    internet_top_down_heap_ = add_heap(online_zone_, MemSource::kGrowDown, "InternetTopDown");
    internet_bottom_up_heap_ = add_heap(online_zone_, MemSource::kGrowUp, "InternetBottomUp");
    get_pull()->leave_heap_scope();
}

// 0x005BB780
void Hub::init_cutscene_heaps(uint32_t size)
{
    if (cinematic_zone_)
        return;
    cinematic_zone_ = new_alloc_region(size);
    cinematic_low_heap_ = add_heap(cinematic_zone_, MemSource::kGrowUp, "CutsceneBottomUp");
    cinematic_high_heap_ = add_heap(cinematic_zone_, MemSource::kGrowDown, "CutsceneTopDown");
}

// 0x005BB820
void Hub::init_extra_skater_heaps(int count)
{
    debug::printf("Init Skater Heaps\n");
    if (count > kSkaterSlots)
        count = kSkaterSlots;
    for (int i = 1; i < count; ++i) {
        skater_regions_[i] = new_alloc_region(0x1AFA4);
        debug::printf("EXTRA: allocated mp_skater_region at %p\n", skater_regions_[i]);
        skater_heaps_[i] = add_heap(skater_regions_[i], MemSource::kGrowUp, "skaterX");
        debug::printf("EXTRA: Setup mp_skater_heap at %p\n", skater_heaps_[i]);
        skater_geom_regions_[i] = new_alloc_region(0x73F78);
        debug::printf("EXTRA: allocated mp_skater_geom_region at %p\n", skater_geom_regions_[i]);
        skater_geom_heaps_[i] = add_heap(skater_geom_regions_[i], MemSource::kGrowUp, "skaterGeomX");
        debug::printf("EXTRA: Setup mp_skater_geom_heap at %p\n", skater_geom_heaps_[i]);
    }
    debug::printf("END Init Skater Heaps\n");
}

// 0x005BB9E0
void Hub::create_labeled_zone(uint32_t name, uint32_t size, const char* text, int /*unused*/)
{
    LabeledZone* named = nullptr;
    for (LabeledZone& slot : named_heaps_) {
        if (!slot.used) {
            named = &slot;
            break;
        }
    }
    // (no free slot: crashes here, as in the original)
    named->name = name;
    std::memcpy(named->text, text, std::strlen(text) + 1); // (unbounded, as in the original)
    named->region = new_alloc_region(size);
    named->heap = add_heap(named->region, MemSource::kGrowUp, named->text);
    named->used = true;
}

// 0x005BC2C0
void set_up_manager()
{
    if (Hub::get_pull())
        return;
    void* memory = get_clear(sizeof(Hub), kManagerStorage);
    Hub* manager = memory ? static_cast<Hub*>(memory)->make_new() : nullptr;
    Hub::sole() = manager;
    manager->enter_heap_scope(manager->bottom_up_heap_);
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
    if (!manager)
        return;
    manager->delete_standard_heaps();
    manager->leave_heap_scope();
    manager->destroy_main_heaps();
    Hub::sole() = nullptr;
}

} // namespace memory
