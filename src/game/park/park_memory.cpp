// The park editor's memory budget (see park_memory.h).

#include "game/park/park_memory.h"

#include <cstdint>
#include <cstring>
#include <new>

#include "engine/core/arena_alloc.h"
#include "engine/core/checksums.h"
#include "engine/core/dbg.h"
#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/core/slab_bank.h"
#include "game/park_editing/park.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"

namespace park_editing {

double g_park_memory_full_usage{0.95};
double g_park_memory_nearly_full_usage{0.9};
MemoryStats g_park_memory_stats;
memory::Zone* g_park_region;
memory::Arena* g_park_heap;
int g_park_memory_unknown_7c_e_68c;
int g_dma_pieces;
bool g_counts_dirty;

namespace {

constexpr uint32_t kPieceDmaTable = 0x9547DA8D; // the global structure of DMA pieces per piece type
constexpr uint32_t kMemoryValueScript = 0x3CB03619;
constexpr uint32_t kValueName = 0x756F5456;
constexpr uint32_t kValueIndex = 0x7F8C98FE;
constexpr uint32_t kValueResult = 0x083FDB95;
constexpr uint32_t kTopDownLevel = 0xCD3682C1; // the level whose park heap is in the top-down heap
constexpr int kFloorPieceMem = 40000;

// A park piece as these functions see it (the park editor's piece class, not decompiled yet).
struct PieceView {
    uint8_t unknown0[0xC];
    uint32_t flags; // +0x0C bit 2: its DMA pieces were counted; bit 8: they don't count
    uint8_t unknown10[0x28];
    uint32_t type; // +0x38 the piece type's checksum
};

// The script's object pools (CPoolable pairs): components, vectors, arrays.
memory::PoolPair& component_pools() { return script::g_component_pools; }
memory::PoolPair& vector_pools() { return script::g_vector_pools; }
memory::PoolPair& array_pools() { return script::g_array_pools; }
// The park editor's: CClipboard, CClipboardEntry, CMapListNode. The second word's bytes say whether each pool
// was made here (and is deleted by power_down).
memory::PoolPair& clipboard_entry_pools() { return g_clipboard_entry_pools; }
memory::PoolPair& map_list_node_pools() { return g_node_pools; }
uint8_t& pool_made_here(memory::PoolPair& pair) { return reinterpret_cast<uint8_t*>(&pair.unknown4)[pair.current]; }
memory::SlabBank* active_slab_pool(memory::PoolPair& pair) { return pair.pools[pair.current]; }

int& unknown_7c_e_68c() { return g_park_memory_unknown_7c_e_68c; }

// The park manager and its generator, whose +0x20 is the most players the park is set up for.
park_editing::ParkAssembler* park_assembler_ref() { return park_editing::ParkLayout::singleton()->builder_; }
int park_max_players() { return park_assembler_ref()->get_player_limit(); }

// A heap's free memory: its region's unclaimed space and its free blocks.
int free_memory(memory::Arena* heap) { return heap->region_->free_byte_count() + heap->free_size_; }

void make_pool(memory::PoolPair& pair, int item_size, int count, const char* name)
{
    void* memory = memory::alloc(sizeof(memory::SlabBank));
    memory::SlabBank* pool = memory ? new (memory) memory::SlabBank(item_size, count, name) : nullptr;
    pair.pools[pair.current] = pool;
    pool_made_here(pair) = 1;
}

void delete_pool(memory::PoolPair& pair)
{
    memory::SlabBank* pool = active_slab_pool(pair);
    if (!pool)
        return;
    if (pool_made_here(pair)) {
        memory::dealloc(pool->outset_); // ~Pool (0x005BA4E0)
        memory::Hub::get_pull()->dealloc(pool);
    }
    pair.pools[pair.current] = nullptr;
}

// 1 - used fraction style figures: the x87 code rounds the free amount to a float, divides in double.
double get_usage(float base, int free, int total)
{
    return base - static_cast<double>(static_cast<float>(free)) / total;
}

void update_max(MemoryStats& stats, double value, float stored)
{
    if (value > stats.max_usage_) // (compared before rounding)
        stats.max_usage_ = stored;
}

// A pool's figure: the park's items over the pool's room left (less a base the script gives); the pool is read
// before the script runs, the park's count after.
float pool_usage(memory::PoolPair& pair, const char* base_name, const int& items)
{
    memory::SlabBank* pool = active_slab_pool(pair);
    int count = pool->num_;
    int used = pool->taken_count_;
    int base = get_memory_value(base_name, -1);
    return static_cast<float>(static_cast<double>(items) / (count - used - base));
}

} // namespace

memory::PoolPair& clipboard_pools() { return g_clipboard_pools; }
MemoryStats& get_attributes() { return g_park_memory_stats; }
memory::Zone*& park_region() { return g_park_region; }
memory::Arena*& park_heap() { return g_park_heap; }
int& dma_pieces() { return g_dma_pieces; }
bool& counts_dirty() { return g_counts_dirty; }

// 0x0059E100
memory::Arena* __cdecl get_park_heap() { return park_heap(); }

// 0x0059E180
memory::Arena* __cdecl get_main_heap() { return memory::Hub::get_pull()->bottom_up_heap_; }

// 0x0059E110
void __cdecl clear_stats()
{
    std::memset(&get_attributes(), 0, sizeof(MemoryStats));
    dma_pieces() = 0;
}

// 0x0059E170
void __cdecl set_counts_dirty() { counts_dirty() = true; }

// 0x0059E130
void __cdecl remove_dma(uint32_t segment_kind)
{
    script::Struct* table = script::read_sub_struct(kPieceDmaTable);
    int pieces = 1;
    table->fetch_int_value(segment_kind, &pieces, 0);
    dma_pieces() -= pieces;
}

// 0x0059E190
void __cdecl add_piece_dma(const void* piece)
{
    auto* view = static_cast<const PieceView*>(piece);
    if (view->flags & 0x100)
        return;
    uint32_t type = view->type;
    script::Struct* table = script::read_sub_struct(kPieceDmaTable);
    int pieces = 1;
    table->fetch_int_value(type, &pieces, 0);
    dma_pieces() += pieces;
}

// 0x0059E1E0
void __cdecl remove_piece_dma(const void* piece)
{
    auto* view = static_cast<const PieceView*>(piece);
    if (!(view->flags & 0x100) && (view->flags & 0x4))
        remove_dma(view->type);
}

// 0x0059E210
void __cdecl create_map_list_node_pool(int count, const char* name)
{
    make_pool(map_list_node_pools(), 0xC, count, name);
}

// 0x0059E290
void __cdecl create_clipboard_entry_pool(int count, const char* name)
{
    make_pool(clipboard_entry_pools(), 0x18, count, name);
}

// 0x0059E310
void __cdecl create_clipboard_pool(int count, const char* name) { make_pool(clipboard_pools(), 0x18, count, name); }

// 0x0059E390
void __cdecl get_marker(uint32_t marker)
{
    memory::Arena* heap = memory::Hub::get_pull()->bottom_up_heap_;
    MemoryStats& stats = get_attributes();
    switch (marker) {
    case 0xAF6240B2:
        clear_stats();
        break;
    case 0x2D987043:
        stats.outer_shell_mem_ = free_memory(heap);
        break;
    case 0x7DE18688:
        stats.outer_shell_mem_ -= free_memory(heap);
        debug::printf("OUTER SHELL MEM = %d !!!!!!!!!!!!!!!!!!!!!\n",
                      stats.outer_shell_mem_); // "OUTER SHELL MEM = %d !!!..."
        break;
    case 0xAFFF3067:
        stats.net_anims_mem_ = free_memory(heap);
        break;
    case 0x65B63305:
        stats.net_anims_mem_ -= free_memory(heap);
        debug::printf("NETANIMS MEM = %d !!!!!!!!!!!!!!!!!!!!!\n", stats.net_anims_mem_); // "NETANIMS MEM = %d !!!..."
        break;
    case 0x9BF91DBE:
        stats.marker48 = free_memory(heap);
        stats.components_used_at_marker_ = active_slab_pool(component_pools())->taken_count_;
        stats.vectors_used_at_marker_ = active_slab_pool(vector_pools())->taken_count_;
        break;
    case 0xCC63F828:
        stats.marker_5c_ = free_memory(heap);
        stats.marker58_free_size_ = heap->free_size_;
        break;
    case 0xE113C765:
        stats.marker_4c_ = free_memory(heap);
        break;
    default:
        break;
    }
}

// 0x0059E4E0
void __cdecl power_down()
{
    delete_pool(clipboard_pools());
    delete_pool(clipboard_entry_pools());
    delete_pool(map_list_node_pools());
    memory::Hub::get_pull()->delete_heap(reinterpret_cast<memory::MemSource*>(park_heap()));
    delete park_region();
    park_heap() = nullptr;
    park_region() = nullptr;
}

// 0x0059E5C0
int __cdecl get_memory_value(const char* name, int index)
{
    script::Struct* params = script::spawn_param_bag();
    if (params)
        params->prepare_all();
    script::Struct* results = script::spawn_param_bag();
    if (results)
        results->prepare_all();
    int value = 0;
    params->put_name_key(kValueName, checksums::hash_from_text(name));
    if (index > -1)
        params->put_int(kValueIndex, index);
    script::run_script_once(kMemoryValueScript, params, nullptr, results);
    results->fetch_int_value(kValueResult, &value, 1);
    if (results) {
        results->get_clear();
        script::delete_struct(results);
    }
    if (params) {
        params->get_clear();
        script::delete_struct(params);
    }
    return value;
}

// 0x0059E730
MemoryStats* __cdecl get_memory_stats(MemoryStats* out)
{
    MemoryStats& stats = get_attributes();
    if (counts_dirty()) {
        counts_dirty() = false;
        park_assembler_ref()->get_node_counts(stats.piece_counts_);
    }
    memory::Arena* main_heap = memory::Hub::get_pull()->bottom_up_heap_;
    memory::Zone* main_region = main_heap->region_;
    // The main heap's pad grows by 0x8EF1C bytes for each player after the first.
    int main_pad = get_memory_value("main_heap_pad", -1); // "main_heap_pad"
    main_pad += park_max_players() * 0x8EF1C - 0x8EF1C;
    stats.main_heap_free_ = main_region->free_byte_count() + main_heap->free_size_;
    stats.main_heap_free_ -= get_memory_value("anim_set_mem", -1) + main_pad; // "anim_set_mem"
    int park_pad = get_memory_value("park_heap_pad", -1);                     // "park_heap_pad"
    stats.park_bytes_in_use_ = park_heap()->used_size_;
    stats.park_heap_free_ = park_region()->free_byte_count() + (park_heap()->free_size_ - park_pad);

    // Fragmented: the largest block is all that can be counted on.
    if (static_cast<int>(main_heap->largest_free()) < stats.main_heap_free_ &&
        static_cast<int>(main_heap->largest_free()) < main_pad && stats.main_heap_free_ >= main_pad) {
        stats.main_heap_free_ = static_cast<int>(main_heap->largest_free());
        stats.main_heap_fragmented_ = true;
    } else {
        stats.main_heap_fragmented_ = false;
    }
    int theme = park_editing::ParkLayout::singleton()->get_shell_size();
    stats.main_heap_free_ -= get_memory_value("theme_pad", theme); // "theme_pad"

    float main_usage = 1.0f;
    if (stats.main_heap_free_ > 0) {
        float free = static_cast<float>(stats.main_heap_free_);
        int base = get_memory_value("main_heap_base", -1); // "main_heap_base"
        main_usage = static_cast<float>(1.0 - static_cast<double>(free) / base);
    }
    stats.main_heap_usage_ = main_usage;
    stats.max_usage_ = main_usage;

    float park_free = static_cast<float>(stats.park_heap_free_);
    double park = 1.0 - static_cast<double>(park_free) / get_memory_value("park_heap_base", -1); // "park_heap_base"
    stats.park_heap_usage_ = static_cast<float>(park);
    update_max(stats, park, stats.park_heap_usage_);

    float top_down_free = static_cast<float>(memory::Hub::get_pull()->top_down_heap_->region_->free_byte_count());
    double top_down =
        2.0 - static_cast<double>(top_down_free) / get_memory_value("top_down_required", -1); // "top_down_required"
    stats.top_down_usage_ = static_cast<float>(top_down);
    if (top_down > stats.max_usage_)
        stats.max_usage_ = stats.top_down_usage_;
    else
        stats.top_down_usage_ = 0.0f;

    // The pools: compared after rounding.
    stats.component_usage_ =
        pool_usage(component_pools(), "component_use_base", stats.components); // "component_use_base"
    if (stats.max_usage_ < stats.component_usage_)
        stats.max_usage_ = stats.component_usage_;
    stats.vector_usage_ = pool_usage(vector_pools(), "vector_use_base", stats.vectors); // "vector_use_base"
    if (stats.max_usage_ < stats.vector_usage_)
        stats.max_usage_ = stats.vector_usage_;
    stats.array_usage_ = pool_usage(array_pools(), "CArray_use_base", stats.arrays); // "CArray_use_base"
    if (stats.max_usage_ < stats.array_usage_)
        stats.max_usage_ = stats.array_usage_;

    float dma = static_cast<float>(dma_pieces());
    double dma_usage = static_cast<double>(dma) / get_memory_value("max_dma_pieces", -1); // "max_dma_pieces"
    stats.dma_usage_ = static_cast<float>(dma_usage);
    update_max(stats, dma_usage, stats.dma_usage_);

    *out = stats;
    return out;
}

// 0x0059EAD0
bool __cdecl main_heap_has_room(int count, int size)
{
    int free = get_attributes().main_heap_free_ - count * size * kFloorPieceMem;
    double usage = get_usage(1.0f, free, get_memory_value("main_heap_base", -1)); // "main_heap_base"
    return !(usage >= g_park_memory_full_usage);                                  // 0.95 (unordered: true)
}

// 0x0059EB30
bool __cdecl pieces_fit(int first, int last)
{
    int dma = dma_pieces();
    int pieces = last - first;
    if (pieces > get_memory_value("max_dma_pieces", -1) - dma) // "max_dma_pieces"
        return false;
    int park = get_memory_value("floor_piece_size_park", -1) * pieces; // "floor_piece_size_park"
    int main = get_memory_value("floor_piece_size_main", -1) * pieces; // "floor_piece_size_main"
    MemoryStats stats;
    get_memory_stats(&stats);
    if (get_attributes().park_heap_free_ < park || get_attributes().main_heap_free_ < main)
        return false;
    double usage = get_usage(1.0f, get_attributes().main_heap_free_ - main,
                             get_memory_value("main_heap_base", -1)); // "main_heap_base"
    return !(usage >= g_park_memory_nearly_full_usage);               // 0.9 (unordered: true)
}

// 0x0059EC00
void __cdecl prepare_all(uint32_t level)
{
    memory::Hub* manager = memory::Hub::get_pull();
    const char* name = "Park Editor"; // "Park Editor"
    int direction;
    if (level == kTopDownLevel) {
        manager->enter_heap_scope(manager->top_down_heap_);
        direction = -1;
    } else {
        manager->enter_heap_scope(manager->bottom_up_heap_);
        direction = 1;
    }
    void* memory = memory::alloc_zeroed(sizeof(memory::MemoryArena));
    memory::MemoryArena* region = nullptr;
    if (memory) {
        int size = get_memory_value("park_heap_size", -1); // "park_heap_size"
        region = new (memory) memory::MemoryArena(static_cast<uint32_t>(size));
    }
    park_region() = region;
    park_heap() = manager->add_heap(region, direction, name);
    create_map_list_node_pool(16000, "CMapListNode");                          // "CMapListNode"
    debug::printf("pool is at %p\n", active_slab_pool(map_list_node_pools())); // "pool is at %p\n"
    create_clipboard_entry_pool(1000, "CClipboardEntry");                      // "CClipboardEntry"
    create_clipboard_pool(6, "CClipboard");                                    // "CClipboard"
    manager->leave_heap_scope();
    unknown_7c_e_68c() = 0;
    dma_pieces() = 0;
    std::memset(&get_attributes(), 0, sizeof(MemoryStats));
}

} // namespace park_editing
