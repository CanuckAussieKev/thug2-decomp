#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include <memory>

#include "engine/core/arena_alloc.h"
#include <gsl/pointers>

// The memory manager: owns the game's regions and heaps, keeps a stack of "current" heaps (where allocations
// without an explicit heap go), and finds heaps by name checksum. One instance (0x007D1800).
namespace memory {

class Hub {
public:
    // A heap created at run time by name (cfunc_mem_init_heap and friends).
    struct LabeledZone {
        std::unique_ptr<Zone> region;
        Arena* heap; // (the heap registry's)
        uint32_t name;   // checksum
        std::array<char, 0x80> text; // the name
        bool used;
        std::array<uint8_t, 3> pad;
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
    // A new heap in `region`, registered (heaps_ owns the heaps it registers: delete_heap frees one).
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

    // The fields other code reads and sets.
    [[nodiscard]] MemSource** ongoing() const { return ongoing_; }
    [[nodiscard]] Zone* primary_zone() const { return primary_zone_; }
    [[nodiscard]] Arena* top_down_heap() const { return top_down_heap_; }
    [[nodiscard]] Arena* bottom_up_heap() const { return bottom_up_heap_; }
    [[nodiscard]] Arena* front_end_heap() const { return front_end_heap_; }
    std::unique_ptr<Zone>& script_region() { return script_region_; }
    [[nodiscard]] const std::unique_ptr<Zone>& script_region() const { return script_region_; }
    [[nodiscard]] Arena* script_heap() const { return script_heap_; }
    [[nodiscard]] Arena* network_heap() const { return network_heap_; }
    [[nodiscard]] Arena* net_misc_heap() const { return net_misc_heap_; }
    [[nodiscard]] Arena* internet_top_down_heap() const { return internet_top_down_heap_; }
    [[nodiscard]] Arena* internet_bottom_up_heap() const { return internet_bottom_up_heap_; }
    [[nodiscard]] Arena* cinematic_high_heap() const { return cinematic_high_heap_; }
    [[nodiscard]] Arena* cinematic_low_heap() const { return cinematic_low_heap_; }
    [[nodiscard]] Arena* debug_heap() const { return debug_heap_; }
    [[nodiscard]] Arena* unused_heap() const { return unused_heap_; }
    [[nodiscard]] const std::array<Arena*, kSkaterSlots>& skater_heaps() const { return skater_heaps_; }
    [[nodiscard]] const std::array<Arena*, kSkaterSlots>& skater_geom_heaps() const { return skater_geom_heaps_; }
    [[nodiscard]] Arena* skater_info_heap() const { return skater_info_heap_; }

private:
    std::array<MemSource*, kMaxHeaps> heaps_{}; // the registered heaps (made by add_heap: these own them)
    int heap_count_;
    MemSource** ongoing_{nullptr}; // the top of alloc_source_stack_
    std::array<MemSource*, 16> alloc_source_stack_{};
    int context_depth_;
    Zone* primary_zone_;
    Arena* top_down_heap_;
    Arena* bottom_up_heap_;
    std::unique_ptr<Zone> front_end_region_;
    Arena* front_end_heap_{}; // (the heap registry heaps_ owns it)
    std::unique_ptr<Zone> script_region_;
    Arena* script_heap_{}; // (the heap registry heaps_ owns it)
    std::unique_ptr<Zone> multiplayer_zone_; // (never made: always null)
    Arena* network_heap_{}; // (the heap registry heaps_ owns it)
    std::unique_ptr<Zone> network_extras_zone_;
    Arena* net_misc_heap_{}; // (the heap registry heaps_ owns it)
    std::unique_ptr<Zone> online_zone_;
    Arena* internet_top_down_heap_{}; // (the heap registry heaps_ owns it)
    Arena* internet_bottom_up_heap_{}; // (the heap registry heaps_ owns it)
    std::unique_ptr<Zone> cinematic_zone_;
    Arena* cinematic_high_heap_{}; // (the heap registry heaps_ owns it)
    Arena* cinematic_low_heap_{}; // (the heap registry heaps_ owns it)
    Zone* dev_tools_zone_{};
    Arena* debug_heap_{};
    Zone* unused_zone_{};    // (never made: its heap, below, is always null)
    Arena* unused_heap_{};   // (the screen elements' heap 0x03C84A59 names it)
    std::array<std::unique_ptr<Zone>, kSkaterSlots> skater_regions_;
    std::array<Arena*, kSkaterSlots> skater_heaps_{};
    std::array<std::unique_ptr<Zone>, kSkaterSlots> skater_geom_regions_;
    std::array<Arena*, kSkaterSlots> skater_geom_heaps_{};
    std::array<LabeledZone, kMaxNamedHeaps> named_heaps_{};
    std::unique_ptr<Zone> skater_info_region_;
    Arena* skater_info_heap_{}; // (the heap registry heaps_ owns it)
    std::unique_ptr<Zone> theme_region_;
    Arena* theme_heap_{}; // (the heap registry heaps_ owns it)
    uint32_t unknown5e0_{0};
    uint32_t unknown5e4_{0};
};

// 0x005BC2C0: create the manager (on the first allocation), with the bottom-up heap current.
void set_up_manager();
// 0x004E1950: set_up_manager, once.
int ensure_manager();
// 0x005BC340
void shut_down_manager();

// NOT ORIGINAL - whether the memory manager is set up.
bool& manager_ready_flag();
} // namespace memory
