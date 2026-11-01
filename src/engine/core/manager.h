#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/core/arena_alloc.h"

// The memory manager: owns the game's regions and heaps, keeps a stack of "current" heaps (where allocations
// without an explicit heap go), and finds heaps by name checksum. One instance (0x007D1800).
namespace memory {

class Hub {
public:
    // A heap created at run time by name (cfunc_mem_init_heap and friends).
    struct LabeledZone {
        Zone* region;
        Arena* heap;
        uint32_t name;   // checksum
        char text[0x80]; // the name
        bool used;
        uint8_t pad[3];
    };
    static constexpr int kMaxHeaps = 48;
    static constexpr int kMaxNamedHeaps = 7;
    static constexpr int kSkaterSlots = 8;

    static Hub* get_pull();
    static Hub*& sole();

    // 0x005BBC00: the 64 MB block (ours bigger: engine/core/memory_sizes.h), the main region and its two heaps (in static storage).
    Hub();
    // The rest of the standard heaps (front end, network, script, skater info, theme, skater, named texture heaps).
    void init_standard_heaps();
    // A new heap in `region`, registered.
    Arena* add_heap(Zone* region, int direction, const char* name);
    void create_net_scratch_zone();
    void init_internet_heaps();
    void init_cutscene_heaps(uint32_t size);
    // Heaps for skaters 1 to count-1 (multiplayer; at most 8 skaters).
    void init_extra_skater_heaps(int count);
    // A heap of its own region, found by name checksum.
    void create_labeled_zone(uint32_t name, uint32_t size, const char* text, int unused);

    // Allocation through a heap (null: the current one); `halt_if_exhausted` reaches the heap as is.
    void* carve(uint32_t size, int halt_if_exhausted, MemSource* heap);
    void* allocate_zeroed(uint32_t size, int halt_if_exhausted, MemSource* heap);
    void* extend_block_upward(uint32_t size, void* block, MemSource* heap);
    void* trim_block_end(uint32_t size, void* block, MemSource* heap);
    void dealloc(void* block);
    // The current heap's largest possible allocation.
    uint32_t obtainable();

    // The current-heap stack.
    void enter_heap_scope(MemSource* heap);
    void leave_heap_scope();
    const char* current_heap_name();

    // The registry of all heaps.
    MemSource* first_zone();
    MemSource* following_zone(MemSource* heap);
    // Take it out of the registry and delete it.
    void delete_heap(MemSource* heap);

    // Heaps by name checksum.
    LabeledZone* find_named_heap(uint32_t name);
    Arena* fetch_heap_by_key(uint32_t name, int unused);
    bool release_labeled_zone(uint32_t name, int unused);
    Arena* get_memory_pool(uint32_t name);
    const char* get_zone_label(uint32_t name);

    // Deleting the optional heaps.
    void release_net_scratch_zone();
    void delete_cutscene_heaps();
    void delete_extra_skater_heaps();
    void delete_standard_heaps();
    // The three that are constructed in place (not freed): the main heaps and region.
    void destroy_main_heaps();

    MemSource* heaps_[kMaxHeaps]{};
    int heap_count_;
    MemSource** ongoing_{nullptr}; // the top of alloc_source_stack_
    MemSource* alloc_source_stack_[16]{};
    int context_depth_;
    Zone* primary_zone_;
    Arena* top_down_heap_;
    Arena* bottom_up_heap_;
    Zone* front_end_region_{};
    Arena* front_end_heap_{};
    Zone* script_region_{};
    Arena* script_heap_{};
    Zone* multiplayer_zone_{};
    Arena* network_heap_{};
    Zone* network_extras_zone_;
    Arena* net_misc_heap_{};
    Zone* online_zone_;
    Arena* internet_top_down_heap_{};
    Arena* internet_bottom_up_heap_{};
    Zone* cinematic_zone_;
    Arena* cinematic_high_heap_;
    Arena* cinematic_low_heap_;
    Zone* dev_tools_zone_{};
    Arena* debug_heap_{};
    uint32_t unknown158_[2]{};
    Zone* skater_regions_[kSkaterSlots]{};
    Arena* skater_heaps_[kSkaterSlots]{};
    Zone* skater_geom_regions_[kSkaterSlots]{};
    Arena* skater_geom_heaps_[kSkaterSlots]{};
    LabeledZone named_heaps_[kMaxNamedHeaps]{};
    Zone* skater_info_region_{};
    Arena* skater_info_heap_{};
    Zone* theme_region_{};
    Arena* theme_heap_{};
    uint32_t unknown5e0_{0};
    uint32_t unknown5e4_{0};
};

// 0x005BC2C0: create the manager (on the first allocation), with the bottom-up heap current.
void set_up_manager();
// 0x004E1950: set_up_manager, once.
int ensure_manager();
// 0x005BC340
void shut_down_manager();

extern bool g_manager_ready;
} // namespace memory
