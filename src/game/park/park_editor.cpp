// The park editor's manager and cursor, 0x0059C900-0x0059E0F7 (see park_editor.h).

#include "game/park/park_editor.h"

#include <cstdint>
#include <cstring>

#include "engine/core/manager.h"
#include "game/park_editing/park_editor.h"
#include "game/park/park_memory.h"
#include "game/park_editing/rail_editor.h"
#include "engine/core/raw_view.h"

namespace park_editing {

namespace {

constexpr uint32_t kNewPark = 0x1A4E0EF9;
constexpr uint16_t kParkInfoVersion = 0x4E24;
constexpr uint32_t kMetaFloor = 0x8;      // a meta piece's flag: part of the floor
constexpr uint32_t kMetaLocked = 0x100;   // (not destroyed by an area's deletion when asked)
constexpr uint32_t kMetaInCursor = 0x200; // held by the cursor

uint32_t& get_meta_flags(void* meta) { return *raw::field_at<uint32_t>(static_cast<uint8_t*>(meta), 0xC); }
CellBox& meta_footprint(void* meta) { return *raw::field_at<CellBox>(static_cast<uint8_t*>(meta), 0x4); }

// The scene is rebuilt while this is set (0x0067F398; cleared around the floor's rebuild).
bool& scene_updates() { return g_collision_allowed; }

void call_destroy_concrete_meta(ParkLayout* manager, void* meta, int flags)
{
    manager->free_placed_piece(static_cast<PieceGroup*>(meta), static_cast<uint32_t>(flags));
}
void rebuild_floor_and_pieces(ParkLayout* manager, const CellBox* area, int flags)
{
    manager->regenerate_ground(area, flags);
}
MapListNode* get_metas_within(ParkLayout* manager, CellBox area) { return manager->collect_pieces_in_region(area); }
// The generator's update of the pieces it builds and its rebuild of the scene.
void update_pieces(void* generator) { static_cast<ParkAssembler*>(generator)->clean_up_out_refs(); }
void rebuild_scene(void* generator) { static_cast<ParkAssembler*>(generator)->rebuild_collision(); }
// The engine's scene (0x004F4970).
park_editing::RailAuthoring* get_engine() { return park_editing::RailAuthoring::sole(); }

// A meta piece of a list node (0x005480C0: the node's +4; then the concrete piece - the same pointer).
void* node_meta(MapListNode* node)
{
    void* meta = node->get_piece_group();
    return static_cast<PieceGroup*>(meta)->get_cast();
}
void free_node_chain(CellChainScratch& temp) { temp.teardown(); }

} // namespace

// 0x0059CB10
void ParkLayout::rebuild_park(uint32_t how)
{
    set_up_lights();
    if (!(current_flags() & 5) && floor_built())
        write_compressed_map();
    destroy_concrete_metas(0);
    clear_stats();
    if (how == kNewPark || !(current_flags() & 2)) {
        // An empty grid.
        for (auto& column : floor_) {
            for (auto& cell : column) {
                cell.level_y_ = 0;
                cell.heights_used_ = 0;
                cell.touched_ = false;
                cell.delta_ = 0;
            }
        }
        write_compressed_map();
        if (map_.rev_ == kParkInfoVersion) {
            const char* unnamed = "unnamed park"; // "unnamed park"
            std::memcpy(map_.label_.data(), unnamed, std::strlen(unnamed) + 1);
            std::memcpy(park_name_.data(), unnamed, std::strlen(unnamed) + 1);
        }
        current_flags() &= ~8u;
        how = kNewPark;
    } else {
        read_from_compressed_map(); // rebuilt from the saved map
    }
    scene_updates() = false;
    rebuild_inner_shell(park_dims());
    rebuild_floor_and_pieces(this, nullptr, 0);
    scene_updates() = true;
    rebuild_scene(get_generator());
    floor_built() = true;
    if (how == kNewPark)
        current_flags() |= 1;
    scene_updates() = false;
    get_engine()->reassemble();
    scene_updates() = true;
    rebuild_scene(get_generator());
}

// 0x0059CC70
bool ParkLayout::rebuild_area(const CellBox* area, bool check_memory)
{
    rebuild_floor_and_pieces(this, area, 0);
    if (!check_memory)
        return true;
    MemoryStats stats{};
    get_memory_stats(&stats);
    // The park editor's singleton (a reference taken and dropped): is the park over its budget?
    const bool full = ParkBuilderMode::fabricate(false)->is_park_full() != 0;
    ParkBuilderMode::get_release();
    return !full;
}

// 0x0059CD20
bool ParkLayout::destroy_metas_in_area_and_rebuild(CellBox area, bool skip_restarts, bool only_within, bool skip_locked)
{
    bool destroyed = false;
    CellChainScratch metas = {get_metas_within(this, area)};
    if (metas.list) {
        for (MapListNode* node = metas.list; node; node = node->following_) {
            void* meta = node_meta(node);
            if (get_meta_flags(meta) & kMetaFloor)
                continue;
            if (skip_restarts && static_cast<PieceGroup*>(meta)->is_spawn_marker())
                continue;
            if (only_within && !static_cast<PieceGroup*>(meta)->fits_inside_region(&area))
                continue;
            if (skip_locked && (get_meta_flags(meta) & kMetaLocked))
                continue;
            call_destroy_concrete_meta(this, meta, 1);
            destroyed = true;
        }
        // The area's corner and size in the world (0x00595A90), and the scene updated over it.
        maths::Vec4 size; // (x, y and z written)
        const maths::Vec4 corner = sole()->cell_to_world_pos(area, &size);
        const float x0 = corner.x;
        const float z0 = corner.z;
        auto z1 = static_cast<float>(static_cast<double>(size.z) + z0);
        auto x1 = static_cast<float>(static_cast<double>(size.x) + x0);
        get_engine()->delete_points(x0, z0, x1, z1, 1);
        update_pieces(get_generator());
        if (destroyed) {
            rebuild_floor_and_pieces(this, &area, 0);
            free_node_chain(metas);
            return true;
        }
    }
    free_node_chain(metas);
    return false;
}

// 0x0059CEC0
void ParkLayout::clear_area(CellBox area)
{
    CellChainScratch metas = {get_metas_within(this, area)};
    for (MapListNode* node = metas.list; node; node = node->following_) {
        void* meta = node_meta(node);
        if (get_meta_flags(meta) & kMetaFloor)
            continue;
        const CellBox at = meta_footprint(meta);
        shift_placed_piece(static_cast<PieceGroup*>(meta), 0, -at.y, 0);
        for (int x = at.x; x < at.x + at.w; ++x) {
            for (int z = at.z; z < at.z + at.l; ++z)
                floor_[x][z].level_y_ = 0;
        }
    }
    for (int x = area.x; x < area.x + area.w; ++x) {
        for (int z = area.z; z < area.z + area.l; ++z)
            floor_[x][z].level_y_ = 0;
    }
    current_flags() &= ~5u;
    rebuild_floor_and_pieces(this, &area, 0);
    free_node_chain(metas);
}

// 0x0059D080
bool ParkLayout::raise_or_lower_floor(CellBox area, int dir)
{
    // The area's highest floor, and whether it's all at one height.
    bool level = false;
    const int height = get_ground_level(area, &level);
    const bool changed = raise_or_lower_floor(area, area.y, height, dir > 0, !level);
    if (!changed)
        return false;
    if (rebuild_area(&area, true)) {
        highlight_gap_pieces(false, nullptr);
        return true;
    }
    undo_floor_change(); // undone
    rebuild_area(&area, false);
    return false;
}

// 0x0059C900
void BuildPointer::teardown()
{
    BuildPointer* cursor = sole();
    if (!cursor)
        return;
    if (cursor->get_meta()) {
        get_meta_flags(cursor->get_meta()) &= ~kMetaInCursor;
        call_destroy_concrete_meta(cursor->hub(), cursor->get_meta(), 0);
    }
    cursor->free_copy_buffers(); // ~BuildPointer
    memory::Hub::get_pull()->dealloc(cursor);
    sole() = nullptr;
}

// 0x0059C950
void BuildPointer::step_piece_choice(int dir)
{
    TileFamilyRef* set = hub()->get_piece_family(set_index(), &visible_set_slot());
    int selected = set->selected_ + dir;
    if (selected < 0)
        selected = set->entry_total_ - 1;
    else if (selected >= set->entry_total_)
        selected = 0;
    set->selected_ = selected;
    if (set->is_clipboard_)
        select_clipboard(selected);
    else
        change_piece(set->entries_[selected].checksum);
}

// 0x0059C9B0
void BuildPointer::step_piece_family(int dir)
{
    // The number of visible sets before the current one.
    auto count_visible = [this]() {
        int visible = 0;
        for (int i = 0; i < set_index(); ++i) {
            if (get_sets(hub())[i].is_displayed_)
                ++visible;
        }
        visible_set_slot() = visible;
    };
    for (int tries = hub()->num_sets(); tries > 0; --tries) {
        set_index() += dir;
        if (set_index() < 0)
            set_index() = hub()->num_sets() - 1;
        if (set_index() >= hub()->num_sets())
            set_index() = 0;
        count_visible();
        if (get_sets(hub())[set_index()].is_displayed_)
            break;
    }
    count_visible();
    const TileFamilyRef& set = get_sets(hub())[set_index()];
    if (!set.is_clipboard_) {
        change_piece(set.entries_[set.selected_].checksum);
        return;
    }
    const int selected = set.selected_;
    for (Clipboard* clipboard = get_clipboards(); clipboard; clipboard = clipboard->following_)
        clipboard->free_all_piece_groups();
    Clipboard* clipboard = get_clipboards();
    for (int i = 0; i < selected && clipboard; ++i)
        clipboard = clipboard->following_;
    active_copy_buffer() = clipboard;
    if (get_meta()) {
        get_meta_flags(get_meta()) &= ~kMetaInCursor;
        call_destroy_concrete_meta(hub(), get_meta(), 0);
        update_pieces(hub()->get_generator());
        get_meta() = nullptr;
    }
    if (get_mode() != 5) {
        get_mode() = 5;
        on_mode_changed();
    }
}

} // namespace park_editing
